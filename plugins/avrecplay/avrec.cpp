/*
 * This file is part of switcher-avrecplay.
 *
 * switcher-avrecplay is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "avrec.hpp"

#include <sys/stat.h>

#include "switcher/quiddity/property/gprop-to-prop.hpp"
#include "switcher/shmdata/caps/utils.hpp"
#include "switcher/utils/scope-exit.hpp"
#include "switcher/utils/string-utils.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_DECLARE_PLUGIN(AVRecorder);
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(AVRecorder,
                                     "avrec",
                                     "Audio/Video shmdata recorder",
                                     "Records connected audio/video shmdata",
                                     "LGPL",
                                     "Jérémie Soria");

const std::string AVRecorder::kConnectionSpec(R"(
{
"follower":
  [
    {
      "label": "stream%",
      "description": "Streams to record",
      "can_do": ["video/x-h264", "video/x-h265", "audio/x-raw"]
    }
  ]
)");

std::vector<std::string> AVRecorder::kGstMuxers = {};
const std::vector<std::string> AVRecorder::kPropertiesBlackList = {"name", "parent"};
const std::string AVRecorder::kRecordModeDate = "Date suffix";
const std::string AVRecorder::kRecordModeLabel = "Label suffix";
const std::string AVRecorder::kRecordModeOverwrite = "Overwrite";

AVRecorder::AVRecorder(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf),
               {kConnectionSpec,
                [this](const std::string& shmpath, claw::sfid_t sfid) {
                  return on_shmdata_connect(shmpath, sfid);
                },
                [this](claw::sfid_t sfid) { return on_shmdata_disconnect(sfid); }}),
      Startable(this),
      gst_pipeline_(std::make_unique<gst::Pipeliner>(nullptr, nullptr)) {
  // Create a list of all available gstreamer muxers if not already done.
  auto mux_factories =
      gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_MUXER, GST_RANK_SECONDARY);
  On_scope_exit {
    if (nullptr != mux_factories) gst_plugin_feature_list_free(mux_factories);
  };
  if (!mux_factories || g_list_length(mux_factories) == 0) {
    sw_warning("ERROR: Could not find any gstreamer muxer (avrec)");
    is_valid_ = false;
    return;
  }

  if (AVRecorder::kGstMuxers.empty()) {
    for (GList* fac = mux_factories; fac != nullptr; fac = g_list_next(fac)) {
      auto current_factory = static_cast<GstElementFactory*>(fac->data);
      AVRecorder::kGstMuxers.push_back(GST_ELEMENT_NAME(current_factory));
    }
  }

  pmanage<&property::PBag::make_string>(
      "recpath",
      [this](const std::string& val) {
        if (val.empty()) {
          sw_warning("Empty folder provided for shmdata recorder.");
          return false;
        }

        struct stat st;
        if (stat(val.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
          if (-1 == mkdir(val.c_str(), S_IRWXU | S_IRUSR | S_IWUSR)) {
            sw_warning("The specified folder does not exist and could not be created (avrec).");
            return false;
          }
        }

        recpath_ = val;
        return true;
      },
      [this]() { return recpath_; },
      "Record folder",
      "Location of the folder in which the shmdata will be recorded",
      recpath_);

  pmanage<&property::PBag::make_selection<>>(
      "record_mode",
      [this](const quiddity::property::IndexOrName& val) {
        record_mode_.select(val);
        for (auto& shmdata : connected_shmdata_) {
          if (record_mode_.get_current() == AVRecorder::kRecordModeLabel) {
            shmdata->create_label_property();
          } else
            pmanage<&property::PBag::remove>(shmdata->label_id_);
        }
        return true;
      },
      [this]() { return record_mode_.get(); },
      "Record mode",
      "Recording mode: Date (new file each recording "
      "suffixed with timestamp), Label(new file each "
      "recording suffixed with custom label), Overwrite "
      "(same file overwritten at each recording)",
      record_mode_);
}

std::string AVRecorder::generate_pipeline_description() {
  std::string description = "";

  // Get the time to create the suffix in case of by-date recording mode.
  std::stringstream date;
  if (record_mode_.get_current() == AVRecorder::kRecordModeDate) {
    time_t raw_time;
    time(&raw_time);
    struct tm* t = localtime(&raw_time);
    date << t->tm_mday << "-" << t->tm_mon + 1 << "-" << t->tm_year + 1900 << "-" << t->tm_hour
         << ":" << t->tm_min << ":" << t->tm_sec;
  }

  for (auto& shmdata : connected_shmdata_) {
    // If the muxer name ends in mux or enc take the name before the suffix as an extension,
    // otherwise, use .avrec.
    std::string extension = ".";
    size_t ext_pos = shmdata->muxer_selection_.get_current().find("enc");
    if (ext_pos == std::string::npos) ext_pos = shmdata->muxer_selection_.get_current().find("mux");
    if (ext_pos != std::string::npos)
      extension += std::string(shmdata->muxer_selection_.get_current(), 0, ext_pos);
    else
      extension += "avrec";

    // Add the proper suffix if in date or label recording mode.
    std::string suffix;
    if (record_mode_.get_current() == AVRecorder::kRecordModeDate) {
      suffix = "-" + date.str();
    } else if (record_mode_.get_current() == AVRecorder::kRecordModeLabel)
      suffix = "-" + shmdata->label_;

    // Create the pipeline description for the current shmdata and add it to the global pipeline.
    description +=
        "shmdatasrc name=shmsrc_" + shmdata->shmdata_name_ + " ! " + shmdata->caps_ + " ! ";

    // For video,we need either an h264 or h265 parser.
    if (shmdata->caps_.find("video") == 0) {
      auto encpos = shmdata->caps_.find("h26");
      auto parser = std::string(shmdata->caps_, encpos, 4) + "parse";
      description += parser + " ! ";
    }

    description += "queue ! " + shmdata->muxer_selection_.get_current() + " name=mux_" +
                   shmdata->shmdata_name_ + " ! queue ! filesink name=sink_" +
                   shmdata->shmdata_name_ + " location=" + recpath_ + "/" + shmdata->recfile_ +
                   suffix + extension + " ";
  }
  return description;
}

bool AVRecorder::start() {
  if (recpath_.empty()) return false;
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(
      nullptr, [this](GstMessage* msg) { return this->bus_sync(msg); });

  GError* error = nullptr;
  avrec_bin_ =
      gst_parse_bin_from_description(generate_pipeline_description().c_str(), FALSE, &error);
  bool success = true;
  On_scope_exit {
    if (!success) gst_object_unref(avrec_bin_);
  };

  if (error) {
    sw_warning("Could not create shmdata recorder: {}", std::string(error->message));
    g_error_free(error);
    return false;
  }

  // Look for an audio shmdata to enable timestamping on it. Otherwise we use any video shmdata.
  bool timestamping_done = false;
  auto audio_shmdata = std::find_if(connected_shmdata_.begin(),
                                    connected_shmdata_.end(),
                                    [](const std::unique_ptr<ConnectedShmdata>& shmdata) {
                                      return shmdata->caps_.find("audio/x-raw") == 0;
                                    });
  if (audio_shmdata != connected_shmdata_.end()) {
    auto shmdatasrc = gst_bin_get_by_name(
        GST_BIN(avrec_bin_), std::string("shmsrc_" + audio_shmdata->get()->shmdata_name_).c_str());
    if (shmdatasrc) {
      g_object_set(G_OBJECT(shmdatasrc), "do-timestamp", TRUE, nullptr);
      timestamping_done = true;
    }
  }

  for (auto& shmdata : connected_shmdata_) {
    auto shmdatasrc = gst_bin_get_by_name(GST_BIN(avrec_bin_),
                                          std::string("shmsrc_" + shmdata->shmdata_name_).c_str());
    auto mux = gst_bin_get_by_name(GST_BIN(avrec_bin_),
                                   std::string("mux_" + shmdata->shmdata_name_).c_str());
    if (!shmdatasrc || !mux) continue;

    shmdata->apply_gst_properties(mux);
    g_object_set(G_OBJECT(shmdatasrc), "socket-path", shmdata->shmpath_.c_str(), nullptr);
    g_object_set(G_OBJECT(shmdatasrc), "copy-buffers", TRUE, nullptr);
    if (!timestamping_done) {
      g_object_set(G_OBJECT(shmdatasrc), "do-timestamp", TRUE, nullptr);
      timestamping_done = true;
    }
  }

  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);
  g_object_set(G_OBJECT(avrec_bin_), "async-handling", TRUE, nullptr);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), avrec_bin_);

  // Disable all properties when recording.
  pmanage<&property::PBag::disable>(recpath_id_, Startable::disabledWhenStartedMsg);
  pmanage<&property::PBag::disable>(record_mode_id_, Startable::disabledWhenStartedMsg);
  for (auto& shmdata : connected_shmdata_) {
    pmanage<&property::PBag::disable>(shmdata->recfile_id_,
                                            Startable::disabledWhenStartedMsg);
    if (record_mode_.get_current() == AVRecorder::kRecordModeLabel)
      pmanage<&property::PBag::disable>(shmdata->label_id_,
                                              Startable::disabledWhenStartedMsg);
    for (auto& prop : shmdata->muxer_properties_id_)
      pmanage<&property::PBag::disable>(prop, Startable::disabledWhenStartedMsg);
  }

  gst_pipeline_->play(true);
  return true;
}

bool AVRecorder::stop() {
  if (gst_element_send_event(gst_pipeline_->get_pipeline(), gst_event_new_eos())) {
    std::unique_lock<std::mutex> lock(eos_m_);
    cv_eos_.wait_for(lock, std::chrono::seconds(5));
  }

  gst_pipeline_.reset();

  // Enable all properties when stopping.
  for (auto& shmdata : connected_shmdata_) {
    pmanage<&property::PBag::enable>(shmdata->recfile_id_);
    if (record_mode_.get_current() == AVRecorder::kRecordModeLabel)
      pmanage<&property::PBag::enable>(shmdata->label_id_);
    for (auto& prop : shmdata->muxer_properties_id_) pmanage<&property::PBag::enable>(prop);
  }

  // Put the value of all dynamically created properties in a structure for possible later saving.
  save_properties();

  return true;
}

bool AVRecorder::on_shmdata_connect(const std::string& shmpath, claw::sfid_t sfid) {
  auto connected_shmdata = std::make_unique<ConnectedShmdata>(this, shmpath, sfid);

  // Create a follower on the connecting shmdata, when receiving its caps, discover compatible
  // muxers and fetch all their gstreamer properties then create the appropriate switcher
  // properties.
  auto shm_follower = std::make_unique<shmdata::Follower>(
      this,
      shmpath,
      nullptr,
      [this, connected_shmdata = connected_shmdata.get(), shmpath](const std::string& str_caps) {
        auto manager = switcher::shmdata::caps::get_switcher_name(str_caps);
        auto id = std::to_string(switcher::shmdata::caps::get_quiddity_id(str_caps));
        auto category =
            stringutils::replace_char(switcher::shmdata::caps::get_category(str_caps), ' ', "_");

        auto name = std::string();
        if (!manager.empty()) name = manager + "_";
        if (!id.empty()) name = name + id + "_";
        name += category;

        connected_shmdata->shmdata_name_ = name;
        pmanage<&property::PBag::make_group>(
            name + "_group",
            "Muxer for " + name,
            std::string("Properties of the muxer for shmdata ") + name);

        connected_shmdata->caps_ = str_caps;
        std::vector<std::string> muxer_list;
        connected_shmdata->discover_compatible_muxers(muxer_list);

        auto recfile_prop_name = "recfile_" + name;
        connected_shmdata->recfile_id_ = pmanage<&property::PBag::make_parented_string>(
            recfile_prop_name,
            name + "_group",
            [connected_shmdata](const std::string& val) {
              connected_shmdata->recfile_ = val;
              return true;
            },
            [connected_shmdata]() { return connected_shmdata->recfile_; },
            "Recording file.",
            "Name of the file inside which to record this shmdata.",
            connected_shmdata->shmdata_name_);
        toggle_property_saving(recfile_prop_name);

        connected_shmdata->muxer_selection_ = property::Selection<>(std::move(muxer_list), 0);
        auto muxer_selection_prop_name = "muxers_" + connected_shmdata->shmdata_name_;
        connected_shmdata->muxer_selection_id_ =
            pmanage<&property::PBag::make_parented_selection<>>(
                muxer_selection_prop_name,
                name + "_group",
                [connected_shmdata](const quiddity::property::IndexOrName& val) {
                  connected_shmdata->muxer_selection_.select(val);
                  return connected_shmdata->update_gst_properties();
                },
                [connected_shmdata]() { return connected_shmdata->muxer_selection_.get(); },
                "Muxer choice",
                "Select a muxer to record shmdata " + connected_shmdata->shmdata_name_,
                connected_shmdata->muxer_selection_);
        toggle_property_saving(muxer_selection_prop_name);

        if (record_mode_.get_current() == AVRecorder::kRecordModeLabel)
          connected_shmdata->create_label_property();

        auto saved_properties = saved_properties_.find(name);
        if (saved_properties != saved_properties_.end()) {
          pmanage<&property::PBag::set_str>(connected_shmdata->muxer_selection_id_,
                                                  saved_properties->second["muxer"]);
          pmanage<&property::PBag::set_str>(connected_shmdata->recfile_id_,
                                                  saved_properties->second["recfile"]);
          pmanage<&property::PBag::set_str>(connected_shmdata->label_id_,
                                                  saved_properties->second["label"]);
          for (auto& prop : saved_properties->second) {
            if (prop.first == "muxer" || prop.first == "recfile" || prop.first == "label") continue;
            pmanage<&property::PBag::set_str_str>(prop.first, prop.second);
          }
        } else {
          pmanage<&property::PBag::set_to_current>(connected_shmdata->muxer_selection_id_);
        }
      },
      nullptr);

  connected_shmdata->shm_follower_ = std::move(shm_follower);
  connected_shmdata_.push_back(std::move(connected_shmdata));

  return true;
}

bool AVRecorder::on_shmdata_disconnect(claw::sfid_t sfid) {
  connected_shmdata_.erase(std::remove_if(
      connected_shmdata_.begin(),
      connected_shmdata_.end(),
      [&](const std::unique_ptr<ConnectedShmdata>& shmdata) { return sfid == shmdata->sfid_; }));
  return true;
}

GstBusSyncReply AVRecorder::bus_sync(GstMessage* msg) {
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
    std::unique_lock<std::mutex> lock(eos_m_);
    cv_eos_.notify_all();
  }
  return GST_BUS_PASS;
}

InfoTree::ptr AVRecorder::on_saving() {
  auto res = InfoTree::make();

  // We don't want to load a started recording scenario.
  pmanage<&property::PBag::set_str_str>("started", "false");

  // Put the value of all dynamically created properties in a structure for possible later saving.
  save_properties();

  // Keep the properties structure in the custom saving tree.
  for (auto& shmdata : saved_properties_) {
    auto avrec_key = std::string(".avrec_properties.") + shmdata.first;
    res->graft(avrec_key, InfoTree::make());
    for (auto& property : shmdata.second) {
      res->graft(avrec_key + "." + property.first, InfoTree::make(property.second));
    }
  }

  return res;
}

void AVRecorder::on_loading(InfoTree::ptr&& tree) {
  if (!tree || tree->empty()) return;

  auto saved_shmdata = tree->get_child_keys(".avrec_properties");
  for (auto& shmdata : saved_shmdata) {
    std::string avrec_key = ".avrec_properties." + shmdata;
    auto properties = tree->get_child_keys(avrec_key);
    for (auto& prop : properties) {
      std::string value = tree->branch_read_data<std::string>(avrec_key + "." + prop);
      saved_properties_[shmdata][prop] = value;
    }
  }
}

void AVRecorder::save_properties() {
  saved_properties_.clear();

  for (auto& shmdata : connected_shmdata_) {
    auto muxer = shmdata->muxers_.find(shmdata->muxer_selection_.get_current());
    // Only save the properties of the selected muxer.
    if (muxer == shmdata->muxers_.end()) continue;

    // These 3 are not gstreamer properties but are still dynamically generated.
    saved_properties_[shmdata->shmdata_name_]["muxer"] = shmdata->muxer_selection_.get_current();
    saved_properties_[shmdata->shmdata_name_]["recfile"] = shmdata->recfile_;
    saved_properties_[shmdata->shmdata_name_]["label"] = shmdata->label_;
    for (auto& prop : shmdata->muxer_properties_id_) {
      auto value = pmanage<&property::PBag::get_str>(prop);
      auto prop_name = pmanage<&property::PBag::get_name>(prop);
      saved_properties_[shmdata->shmdata_name_][prop_name] = value;
    }
  }
}

AVRecorder::ConnectedShmdata::ConnectedShmdata(AVRecorder* parent,
                                               const std::string& shmpath,
                                               claw::sfid_t sfid)
    : parent_(parent), shmpath_(shmpath), sfid_(sfid) {}

AVRecorder::ConnectedShmdata::~ConnectedShmdata() {
  parent_->pmanage<&property::PBag::remove>(muxer_selection_id_);
  parent_->pmanage<&property::PBag::remove>(recfile_id_);
  if (parent_->record_mode_.get_current() == AVRecorder::kRecordModeLabel)
    parent_->pmanage<&property::PBag::remove>(label_id_);
  parent_->pmanage<&property::PBag::remove>(
      parent_->pmanage<&property::PBag::get_id>(shmdata_name_ + "_group"));
  for (auto& muxer : muxers_) gst_object_unref(muxer.second.elem);
}

void AVRecorder::ConnectedShmdata::discover_compatible_muxers(
    std::vector<std::string>& muxer_list) {
  GstCaps* caps = gst_caps_from_string(caps_.c_str());
  On_scope_exit {
    if (nullptr != caps) gst_caps_unref(caps);
  };

  // Find all muxers in which we can sink caps and fetch their properties.
  for (auto& mux : AVRecorder::kGstMuxers) {
    GstElementFactory* current_factory = gst_element_factory_find(mux.c_str());
    if (current_factory && gst_element_factory_can_sink_all_caps(current_factory, caps)) {
      auto mux_name = GST_ELEMENT_NAME(current_factory);
      ElementProperties muxer;
      muxer.elem = gst_element_factory_make(mux_name, nullptr);
      if (!muxer.elem) continue;
      muxer_list.push_back(std::string(mux_name));
      GParamSpec** property_specs;
      unsigned int num_properties;
      property_specs =
          g_object_class_list_properties(G_OBJECT_GET_CLASS(muxer.elem), &num_properties);
      On_scope_exit { g_free(property_specs); };
      for (unsigned int i = 0; i < num_properties; ++i) {
        std::string prop_name = property_specs[i]->name;
        if (std::find(AVRecorder::kPropertiesBlackList.begin(),
                      AVRecorder::kPropertiesBlackList.end(),
                      prop_name) != AVRecorder::kPropertiesBlackList.end())
          continue;
        muxer.properties.push_back(prop_name);
      }

      muxers_[mux_name] = muxer;
    }
  }
}

bool AVRecorder::ConnectedShmdata::update_gst_properties() {
  // Remove all active dynamic properties.
  for (auto& prop_id : muxer_properties_id_) {
    parent_->pmanage<&property::PBag::remove>(prop_id);
  }
  muxer_properties_id_.clear();

  // Find the selected muxer object.
  auto muxer_name = muxer_selection_.get_current();
  auto selected_muxer = muxers_.find(muxer_selection_.get_current());
  if (selected_muxer == muxers_.end()) return false;

  // Create the properties of the selected muxer.
  for (auto& prop : selected_muxer->second.properties) {
    auto prop_name = prop + "_avrec_" + muxer_name + "_" + shmdata_name_;

    auto prop_id =
        static_cast<property::prop_id_t>(parent_->pmanage<&property::PBag::push_parented>(
            prop_name,
            shmdata_name_ + "_group",
            quiddity::property::to_prop(G_OBJECT(selected_muxer->second.elem), prop)));
    muxer_properties_id_.push_back(prop_id);
    parent_->toggle_property_saving(prop_name);
  }
  return true;
}

void AVRecorder::ConnectedShmdata::create_label_property() {
  auto label_prop_name = "label_" + shmdata_name_;
  label_id_ =
      parent_->pmanage<&property::PBag::make_parented_string>(label_prop_name,
                                                                    shmdata_name_ + "_group",
                                                                    [this](const std::string& val) {
                                                                      label_ = val;
                                                                      return true;
                                                                    },
                                                                    [this]() { return label_; },
                                                                    "File name suffix.",
                                                                    "",
                                                                    label_);
  parent_->toggle_property_saving(label_prop_name);
}

void AVRecorder::ConnectedShmdata::apply_gst_properties(GstElement* element) {
  for (auto& prop : muxer_properties_id_) {
    auto value = parent_->pmanage<&property::PBag::get_str>(prop);
    auto prop_name = parent_->pmanage<&property::PBag::get_name>(prop);
    auto gst_prop_name_pos = prop_name.find("_avrec_");
    if (gst_prop_name_pos == std::string::npos) return;

    // Find the gst property name to apply it to the element.
    gst_util_set_object_arg(
        G_OBJECT(element), std::string(prop_name, 0, gst_prop_name_pos).c_str(), value.c_str());
  }
}

}  // namespace quiddities
}  // namespace switcher
