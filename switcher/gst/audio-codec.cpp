/*
 * This file is part of libswitcher.
 *
 * libswitcher is free software; you can redistribute it and/or
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

#include "./audio-codec.hpp"
#include "../infotree/json-serializer.hpp"
#include "../quiddity/property/gprop-to-prop.hpp"
#include "../quiddity/quiddity.hpp"
#include "../quiddity/startable.hpp"
#include "../utils/scope-exit.hpp"
#include "./utils.hpp"

namespace switcher {
namespace gst {
AudioCodec::AudioCodec(quiddity::Quiddity* quid)
    : quid_(quid),
      reset_id_(quid_->mmanage<&quiddity::method::MBag::make_method<std::function<bool()>>>(
          "reset",
          infotree::json::deserialize(
              R"(
                  {
                   "name" : "Reset codec configuration",
                   "description" : "Reset codec configuration to default",
                   "arguments" : []
                  }
              )"),
          [this]() { return reset_codec_configuration(); })),
      gst_pipeline_(std::make_unique<Pipeliner>(nullptr, nullptr)),
      codecs_(gst::utils::element_factory_list_to_pair_of_vectors(
                  GST_ELEMENT_FACTORY_TYPE_AUDIO_ENCODER, GST_RANK_SECONDARY, true, {"vorbisenc"}),
              0),
      codec_id_(install_codec()) {
  reset_codec_configuration();
  stop();
}

void AudioCodec::hide() {
  quid_->mmanage<&quiddity::method::MBag::disable>(
      reset_id_, quiddity::Startable::disabledWhenStartedMsg);
  quid_->pmanage<&quiddity::property::PBag::disable>(
      codec_id_, quiddity::Startable::disabledWhenStartedMsg);
  quid_->pmanage<&quiddity::property::PBag::disable>(
      group_codec_id_, quiddity::Startable::disabledWhenStartedMsg);
}

void AudioCodec::show() {
  quid_->mmanage<&quiddity::method::MBag::enable>(reset_id_);
  quid_->pmanage<&quiddity::property::PBag::enable>(codec_id_);
  quid_->pmanage<&quiddity::property::PBag::enable>(group_codec_id_);
}

void AudioCodec::make_bin() {
  if (0 != codecs_.get_current_index()) {
    gst_bin_add_many(GST_BIN(gst_pipeline_->get_pipeline()),
                     shmsrc_.get_raw(),
                     queue_codec_element_.get_raw(),
                     audio_convert_.get_raw(),
                     audio_resample_.get_raw(),
                     codec_element_.get_raw(),
                     shm_encoded_.get_raw(),
                     nullptr);
    gst_element_link_many(shmsrc_.get_raw(),
                          queue_codec_element_.get_raw(),
                          audio_convert_.get_raw(),
                          audio_resample_.get_raw(),
                          codec_element_.get_raw(),
                          shm_encoded_.get_raw(),
                          nullptr);
  }
}

bool AudioCodec::remake_codec_elements() {
  if (0 != codecs_.get_current_index()) {
    if (!UGstElem::renew(shmsrc_, {"socket-path"}) ||
        !UGstElem::renew(shm_encoded_, {"socket-path", "sync", "async", "extra-caps-properties"}) ||
        !UGstElem::renew(audio_convert_) || !UGstElem::renew(audio_resample_) ||
        !UGstElem::renew(queue_codec_element_) ||
        !UGstElem::renew(codec_element_, codec_properties_)) {
      return false;
    }
  }  // end no codec
  return true;
}

void AudioCodec::uninstall_codec_properties() {
  for (auto& it : codec_properties_)
    quid_->pmanage<&quiddity::property::PBag::remove>(
        quid_->pmanage<&quiddity::property::PBag::get_id>(it));
  codec_properties_.clear();
}

void AudioCodec::toggle_codec_properties(bool enable) {
  for (auto& it : codec_properties_) {
    if (enable)
      quid_->pmanage<&quiddity::property::PBag::enable>(
          quid_->pmanage<&quiddity::property::PBag::get_id>(it));
    else
      quid_->pmanage<&quiddity::property::PBag::disable>(
          quid_->pmanage<&quiddity::property::PBag::get_id>(it),
          quiddity::Startable::disabledWhenStartedMsg);
  }
}

void AudioCodec::make_codec_properties() {
  guint num_properties = 0;
  GParamSpec** props =
      g_object_class_list_properties(G_OBJECT_GET_CLASS(codec_element_.get_raw()), &num_properties);
  On_scope_exit { g_free(props); };
  group_codec_id_ = quid_->pmanage<&quiddity::property::PBag::make_group>(
      "codec_config", "Audio codec properties", "Configure the codec");
  for (guint i = 0; i < num_properties; i++) {
    auto param_name = g_param_spec_get_name(props[i]);
    if (param_black_list_.end() == param_black_list_.find(param_name)) {
      quid_->pmanage<&quiddity::property::PBag::push_parented>(
          param_name,
          "codec_config",
          quiddity::property::to_prop(G_OBJECT(codec_element_.get_raw()), param_name));
      codec_properties_.push_back(param_name);
    }
  }
}

bool AudioCodec::reset_codec_configuration() {
  {
    auto lock = quid_->pmanage<&quiddity::property::PBag::get_lock>(codec_id_);
    codecs_.select(quiddity::property::IndexOrName("Opus audio encoder"));
    quid_->pmanage<&quiddity::property::PBag::notify>(codec_id_);
  }
  return true;
}

bool AudioCodec::start(const std::string& shmpath, const std::string& shmpath_encoded) {
  hide();
  toggle_codec_properties(false);
  if (0 == codecs_.get_current_index()) return false;

  shmpath_to_encode_ = shmpath;
  if (shmpath_encoded.empty())
    shm_encoded_path_ = shmpath_to_encode_ + "-encoded";
  else
    shm_encoded_path_ = shmpath_encoded;

  auto extra_caps = quid_->get_quiddity_caps();
  g_object_set(G_OBJECT(shmsrc_.get_raw()), "socket-path", shmpath_to_encode_.c_str(), nullptr);
  g_object_set(G_OBJECT(shm_encoded_.get_raw()),
               "socket-path",
               shm_encoded_path_.c_str(),
               "sync",
               FALSE,
               "async",
               FALSE,
               "extra-caps-properties",
               extra_caps.c_str(),
               nullptr);

  shmsink_sub_ =
      std::make_unique<shmdata::GstTreeUpdater>(this->quid_,
                                                shm_encoded_.get_raw(),
                                                shm_encoded_path_,
                                                shmdata::GstTreeUpdater::Direction::writer);
  shmsrc_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
      this->quid_,
      shmsrc_.get_raw(),
      shmpath_to_encode_,
      shmdata::GstTreeUpdater::Direction::reader,
      [this](const std::string& caps) {
        if (!this->has_enough_channels(caps)) {
          // FIXME: To do in can_sink_caps of audioenc when destination caps are implemented.
          quid_->sw_warning("audio codec does not support the number of channels connected to it.");
        }
      });
  make_bin();
  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);
  if (copy_buffers_) g_object_set(G_OBJECT(shmsrc_.get_raw()), "copy-buffers", TRUE, nullptr);
  gst_pipeline_->play(true);
  return true;
}

bool AudioCodec::stop() {
  show();
  toggle_codec_properties(true);
  if (0 != codecs_.get_current_index()) {
    shmsink_sub_.reset();
    shmsrc_sub_.reset();
    remake_codec_elements();
    make_codec_properties();
    gst_pipeline_ = std::make_unique<Pipeliner>(nullptr, nullptr);
  }
  return true;
}

quiddity::property::prop_id_t AudioCodec::install_codec() {
  codecs_.select(quiddity::property::IndexOrName("Opus audio encoder"));
  return quid_->pmanage<&quiddity::property::PBag::make_selection<>>(
      "codec",
      [this](const quiddity::property::IndexOrName& val) {
        uninstall_codec_properties();
        codecs_.select(val);
        if (0 == val.index_) return true;
        codec_element_.mute(codecs_.get_attached().c_str());
        remake_codec_elements();
        make_codec_properties();
        return true;
      },
      [this]() { return codecs_.get(); },
      "Audio Codecs",
      "Selected audio codec for encoding",
      codecs_);
}

bool AudioCodec::has_enough_channels(const std::string& str_caps) {
  GstCaps* caps = gst_caps_from_string(str_caps.c_str());
  On_scope_exit {
    if (nullptr != caps) gst_caps_unref(caps);
  };

  GstStructure* s = gst_caps_get_structure(caps, 0);
  if (nullptr == s) {
    quid_->sw_warning("Cannot get structure from caps (audioenc)");
    return false;
  }

  gint channels = 0;
  if (!gst_structure_get_int(s, "channels", &channels)) {
    quid_->sw_warning("Cannot get channels number from shmdata description (audioenc)");
    return false;
  }

  std::pair<int, int> channels_range = gst::utils::get_gst_element_capability_as_range(
      gst_plugin_feature_get_name(gst_element_get_factory(codec_element_.get_raw())),
      "channels",
      GST_PAD_SINK);

  return channels <= channels_range.second;
}

}  // namespace gst
}  // namespace switcher
