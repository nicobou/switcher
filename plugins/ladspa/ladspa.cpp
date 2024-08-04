/*
 * This file is part of switcher-ladspa.
 *
 * switcher-ladspa is free software; you can redistribute it and/or
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

#include "./ladspa.hpp"
#include "switcher/quiddity/property/gprop-to-prop.hpp"
#include "switcher/utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_DECLARE_PLUGIN(LADSPA);
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(LADSPA,
                                     "ladspa",
                                     "LADSPA plugin",
                                     "Choice of LADSPA plugins",
                                     "LGPL",
                                     "Jérémie Soria");

const std::vector<std::string> LADSPA::KPropertiesBlackList = {"name", "parent"};

const std::string LADSPA::kConnectionSpec(R"(
{
"follower":
  [
    {
      "label": "audio",
      "description": "Audio stream",
      "can_do": ["audio/x-raw"]
    }
  ],
"writer":
  [
    {
      "label": "audio",
      "description": "Audio stream with effect",
      "can_do": ["audio/x-raw"]
    }
  ]
}
)");

LADSPA::LADSPA(quiddity::Config&& conf)
    : Quiddity(
          std::forward<quiddity::Config>(conf),
          {kConnectionSpec,
           [this](const std::string& shmpath, claw::sfid_t) { return on_shmdata_connect(shmpath); },
           [this](claw::sfid_t) { return on_shmdata_disconnect(); }}),
      gst_pipeline_(std::make_unique<gst::Pipeliner>(nullptr, nullptr)),
      plugins_list_(get_ladspa_plugins()),
      plugins_(property::Selection<>(
          std::move(plugins_list_.first), std::move(plugins_list_.second), 0)) {
  if (plugins_list_.first.empty()) return;

  perchannel_group_id_ = pmanage<&property::PBag::make_group>(
      "perchannel_group",
      "Per-channel settings",
      "If multiple channels are input they will all be configured separately in this group.");

  global_settings_id_ =
      pmanage<&property::PBag::make_bool>("global_settings",
                                                [this](bool val) {
                                                  global_settings_ = val;
                                                  return true;
                                                },
                                                [this]() { return global_settings_; },
                                                "Settings for all channels",
                                                "Toggle the global settings for all channels",
                                                true);

  plugins_id_ = pmanage<&property::PBag::make_selection<>>(
      "plugins",
      [this](const quiddity::property::IndexOrName& val) {
        plugins_.select(val);

        sw_debug("ladspa plugin selected: {}", plugins_.get_current());
        // We don't want to reset the properties if they were loaded from a save file
        if (reset_saved_properties_)
          saved_properties_.clear();
        else
          reset_saved_properties_ = true;

        if (!create_gst_pipeline()) return false;
        // Here we just create the pipeline to get the properties, since we don't put the bin in a
        // gst::Pipeline it
        // will leak if we don't unref.
        gst_object_unref(ladspa_bin_);
        first_connect_ = true;

        return true;
      },
      [this]() { return plugins_.get(); },
      "LADSPA plugin",
      "Select a LADSPA plugin among all the ones installed on the system.",
      plugins_);

  pmanage<&property::PBag::set_to_current>(plugins_id_);
  pmanage<&property::PBag::set_to_current>(global_settings_id_);
}

LADSPA::PluginList LADSPA::get_ladspa_plugins() {
  PluginList plugins_list;
  auto plugins = gst::utils::element_factory_list_to_pair_of_vectors(
      GST_ELEMENT_FACTORY_TYPE_MEDIA_AUDIO, GST_RANK_NONE, false);
  size_t index = 0;
  for (auto& plugin : plugins.second) {
    if (stringutils::starts_with(plugin, "ladspa")) {
      auto value = gst::utils::get_gst_element_capability(plugin, "channels", GST_PAD_SINK);
      if (value && 1 != g_value_get_int(value)) {
        sw_warning("ladspa plugin {} not supported because it requires more than one input channel",
                   plugins.first.at(index));
      } else {
        plugins_list.first.push_back(plugins.first.at(index));
        plugins_list.second.push_back(plugin);
      }
    }
    ++index;
  }

  return plugins_list;
}

void LADSPA::mirror_gst_properties() {
  if (ladspa_elements_.empty()) return;
  // Unsubscribe old global properties
  for (auto& subscriber : prop_subscribers_) {
    pmanage<&property::PBag::unsubscribe>(subscriber.first, subscriber.second);
  }
  prop_subscribers_.clear();

  // We arbitrarily take the first element as reference
  auto first_element = ladspa_elements_.front();
  if (!first_element) {
    sw_warning("Cannot find a valid ladspa element to mirror properties");
    return;
  }
  std::string first_element_name = gst_element_get_name(first_element);
  for (auto& property : properties_) {
    auto prop_id = static_cast<property::prop_id_t>(pmanage<&property::PBag::push>(
        property, quiddity::property::to_prop(G_OBJECT(first_element), property)));

    // When the global property changes we propagate the change on all the elements
    auto rid =
        pmanage<&property::PBag::subscribe>(prop_id, [this, property, first_element_name]() {
          if (!global_settings_) return;
          auto property_value =
              pmanage<&property::PBag::get_str_str>(property + "_" + first_element_name);
          for (auto& element : ladspa_elements_) {
            pmanage<&property::PBag::set_str_str>(
                property + "_" + gst_element_get_name(element), property_value);
          }
        });
    // We start by propagating once for each property.
    pmanage<&property::PBag::set_to_current>(prop_id);
    prop_subscribers_.push_back(std::make_pair<>(prop_id, rid));
  }
}

bool LADSPA::create_gst_pipeline() {
  GError* error = nullptr;
  std::string target_format = g_value_get_string(
      gst::utils::get_gst_element_capability(plugins_.get_attached(), "format", GST_PAD_SINK));

  auto target_channels = g_value_get_int(
      gst::utils::get_gst_element_capability(plugins_.get_attached(), "channels", GST_PAD_SINK));

  std::string description =
      std::string("shmdatasrc name=shmsrc ! audioconvert ! audio/x-raw,format=") + target_format;

  auto extra_caps = get_quiddity_caps();

  if (target_channels > 1) {
    if (channels_number_ != target_channels) {
      sw_warning("LADSPA element {} only accepts {} channels, we cannot fit {} channels in it.",
                 plugins_.get_attached(),
                 std::to_string(target_channels),
                 std::to_string(channels_number_));
      return false;
    }
    description += " ! " + plugins_.get_attached() + " name=ladspa ";
    auto it = saved_properties_.find("ladspa");
    if (it != saved_properties_.end()) {
      for (auto& property : it->second) {
        description += property.first + "=" + property.second + " ";
      }
    }
    description += "! queue ! shmdatasink name=shmsink sync=false extra-caps-properties='" + extra_caps + "'";
  } else {
    description += " ! deinterleave name=d interleave name=i ! shmdatasink name=shmsink sync=false extra-caps-properties='" + extra_caps + "'";

    for (int channel = 0; channel < channels_number_; ++channel) {
      auto channel_str = std::to_string(channel);
      std::string element_name = std::string("ladspa_") + channel_str;
      description +=
          " d.src_" + channel_str + " ! " + plugins_.get_attached() + " name=" + element_name;
      // We do this in case the pipeline was reset, otherwise the properties values will be reset.
      auto it = saved_properties_.find(element_name);
      if (it != saved_properties_.end()) {
        for (auto& property : it->second) {
          description += std::string(" ") + property.first + "=" + property.second + " ";
        }
      }
      description += " ! queue ! i.sink_" + channel_str;
    }
  }

  auto new_ladspa_bin = gst_parse_bin_from_description(description.c_str(), FALSE, &error);
  bool success = true;
  On_scope_exit {
    if (!success) gst_object_unref(new_ladspa_bin);
  };

  if (error) {
    sw_warning("Could not create ladspa plugin {}: {}",
               plugins_.get_attached(),
               std::string(error->message));
    g_error_free(error);
    return false;
  }

  auto shmdatasrc = gst_bin_get_by_name(GST_BIN(new_ladspa_bin), "shmsrc");
  auto shmdatasink = gst_bin_get_by_name(GST_BIN(new_ladspa_bin), "shmsink");

  if (!shmdatasrc || !shmdatasink) return false;

  // We do not leak GstElement pointers here since they are in a RAII gstreamer pipeline
  ladspa_elements_.clear();
  std::vector<GstElement*> ladspa_elements;

  int elements_number = channels_number_;
  if (target_channels > 1) elements_number = 1;
  for (int i = 0; i < elements_number; ++i) {
    std::string element_name = std::string("ladspa_") + std::to_string(i);
    auto ladspa_element = gst_bin_get_by_name(GST_BIN(new_ladspa_bin), element_name.c_str());
    if (!ladspa_element) {
      success = false;
      return false;
    }
    ladspa_elements.push_back(ladspa_element);
  }

  for (auto& element : ladspa_elements) ladspa_elements_.push_back(element);
  shmdatasrc_ = shmdatasrc;
  shmdatasink_ = shmdatasink;
  ladspa_bin_ = new_ladspa_bin;

  get_gst_properties();
  mirror_gst_properties();

  return true;
}

void LADSPA::get_gst_properties() {
  GParamSpec** property_specs;
  unsigned int num_properties;

  if (ladspa_elements_.empty()) return;

  // All LADSPA instances will be of the same plugin so we only keep one vector of properties.
  property_specs =
      g_object_class_list_properties(G_OBJECT_GET_CLASS(ladspa_elements_.back()), &num_properties);
  On_scope_exit { g_free(property_specs); };

  // Remove all the gstreamer properties
  for (auto& element : ladspa_elements_) {
    std::string element_name = gst_element_get_name(element);

    // Remove the group too
    pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>(element_name));

    for (auto& property : properties_) {
      pmanage<&property::PBag::remove>(
          pmanage<&property::PBag::get_id>(property + "_" + element_name));
    }
  }

  // We unsubscribe the global properties before clearing the list.
  for (auto& property : properties_) {
    pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>(property));
  }
  properties_.clear();

  for (unsigned int i = 0; i < num_properties; ++i) {
    std::string property_name = property_specs[i]->name;

    if (std::find(LADSPA::KPropertiesBlackList.begin(),
                  LADSPA::KPropertiesBlackList.end(),
                  property_name) != LADSPA::KPropertiesBlackList.end())
      continue;

    properties_.push_back(property_name);
  }

  for (auto& element : ladspa_elements_) {
    std::string element_name = gst_element_get_name(element);
    pmanage<&property::PBag::make_parented_group>(
        element_name,
        "perchannel_group",
        element_name,
        std::string("Setting of ladspa instance ") + element_name);
    for (auto& property : properties_) {
      pmanage<&property::PBag::push_parented>(
          property + "_" + element_name,
          element_name,
          quiddity::property::to_prop(G_OBJECT(element), property));
    }
  }
}

bool LADSPA::on_shmdata_connect(const std::string& shmpath) {
  shmpath_ = shmpath;
  shmpath_transformed_ = claw_.get_shmpath_from_writer_label("audio");

  // We get the values before resetting the pipeline before the first connection.
  // After that this will be done during the disconnection event.
  // This is a bit far-fetched but it is necessary due to the fact that we have to create the ladspa
  // element to fetch its properties and we also recreate the pipeline at each connection since it
  // is reset on disconnection. Without this we would lose the property changes between the
  // creation/selection of the plugin and its first connection.
  if (first_connect_) {
    save_properties();
    first_connect_ = false;
  }
  pmanage<&property::PBag::disable>(plugins_id_, property::PBag::disabledWhenConnectedMsg);

  create_and_play_gst_pipeline();

  return true;
}

void LADSPA::create_and_play_gst_pipeline() {
  shmsrc_sub_.reset();
  shmsink_sub_.reset();
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr);

  if (!create_gst_pipeline()) return;

  shmsrc_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
      this,
      shmdatasrc_,
      shmpath_,
      shmdata::GstTreeUpdater::Direction::reader,
      [this](const std::string& str_caps) {
        GstCaps* caps = gst_caps_from_string(str_caps.c_str());
        On_scope_exit {
          if (nullptr != caps) gst_caps_unref(caps);
        };
        GstStructure* s = gst_caps_get_structure(caps, 0);
        if (nullptr == s) {
          sw_warning("cannot get structure from caps (ladspa)");
          return;
        }

        gint channels_number = 0;

        if (!gst_structure_get_int(s, "channels", &channels_number)) {
          sw_warning("cannot get channels number from shmdata description (ladspa)");
          return;
        }

        std::lock_guard<std::mutex> lock(channels_change_mutex_);
        if (channels_number != channels_number_) {
          channels_number_ = channels_number;
          channels_change_th_->run_async([this]() {
            std::lock_guard<std::mutex> lock(channels_change_mutex_);
            create_and_play_gst_pipeline();
          });
        }
      });

  shmsink_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
      this, shmdatasink_, shmpath_transformed_, shmdata::GstTreeUpdater::Direction::writer);

  auto extra_caps = get_quiddity_caps();
  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);
  g_object_set(G_OBJECT(ladspa_bin_), "async-handling", TRUE, nullptr);
  g_object_set(G_OBJECT(shmdatasrc_), "socket-path", shmpath_.c_str(), nullptr);
  g_object_set(G_OBJECT(shmdatasink_), "socket-path", shmpath_transformed_.c_str(), "extra-caps-properties", extra_caps.c_str(), nullptr);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), ladspa_bin_);

  gst_pipeline_->play(true);
}

bool LADSPA::on_shmdata_disconnect() {
  pmanage<&property::PBag::enable>(plugins_id_);
  shmsrc_sub_.reset();
  shmsink_sub_.reset();

  // Save the properties values if we reconnect the same plugin.
  save_properties();

  gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr);
  return true;
}

InfoTree::ptr LADSPA::on_saving() {
  auto res = InfoTree::make();

  // We need to refresh the saved properties.
  save_properties();
  for (auto& element : saved_properties_) {
    auto ladspa_key = std::string(".ladspa_properties.") + element.first;
    res->graft(ladspa_key, InfoTree::make());
    for (auto& property : element.second) {
      res->graft(ladspa_key + "." + property.first, InfoTree::make(property.second));
    }
  }
  res->graft(".channels_number", InfoTree::make(channels_number_));
  return res;
}

void LADSPA::on_loading(InfoTree::ptr&& tree) {
  if (!tree || tree->empty()) return;

  auto elements = tree->get_child_keys(".ladspa_properties");

  for (auto& element : elements) {
    auto ladspa_key = std::string(".ladspa_properties.") + element;
    auto properties = tree->get_child_keys(ladspa_key);
    for (auto& property : properties) {
      std::string property_value = tree->branch_read_data<std::string>(ladspa_key + "." + property);
      saved_properties_[element][property] = property_value;
    }
  }

  channels_number_ = tree->branch_read_data<int>(".channels_number");

  reset_saved_properties_ = false;
  pmanage<&property::PBag::set_to_current>(plugins_id_);
}

void LADSPA::save_properties() {
  // switching locale to C in order to get float serialized correctly
  auto cur_loc = std::locale("");
  std::locale::global(std::locale::classic());
  // restore previous locale when done
  On_scope_exit { std::locale::global(cur_loc); };

  for (auto& element : ladspa_elements_) {
    std::string element_name = gst_element_get_name(element);
    for (auto& prop_name : properties_) {
      saved_properties_[element_name][prop_name] =
          pmanage<&property::PBag::get_str_str>(prop_name + "_" + element_name);
    }
  }
}

}  // namespace quiddities
}  // namespace switcher
