/*
 * This file is part of switcher-nvenc.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
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

#include "./nvenc-plugin.hpp"

#include <gst/gst.h>

#include "cuda/cuda-context.hpp"
#include "switcher/quiddity/container.hpp"
#include "switcher/utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(NVencPlugin,
                                     "nvenc",
                                     "Hardware video encoder (NVenc)",
                                     "CUDA-based video encoder",
                                     "LGPL",
                                     "Nicolas Bouillot");
const std::string NVencPlugin::kConnectionSpec(R"(
{
"follower":
  [
    {
      "label": "video",
      "description": "Video stream ro encode",
      "can_do": ["video/x-raw"]
    }
  ],
"writer":
  [
    {
      "label": "video-encoded",
      "description": "Encoded video stream",
      "can_do": ["video/x-h264", "video/x-h265"]
    }
  ]
}
)");

NVencPlugin::NVencPlugin(quiddity::Config&& conf)
    : Quiddity(
          std::forward<quiddity::Config>(conf),
          {kConnectionSpec,
           [this](const std::string& shmpath, claw::sfid_t) { return on_shmdata_connect(shmpath); },
           [this](claw::sfid_t) { return on_shmdata_disconnect(); }}),
      default_preset_id_(pmanage<&property::PBag::make_bool>(
          "bitrate_from_preset",
          [this](bool value) {
            bitrate_from_preset_ = value;
            if (bitrate_from_preset_) {
              pmanage<&property::PBag::disable>(
                  bitrate_id_, "Cannot set bitrate if using default preset configuration.");
            } else {
              pmanage<&property::PBag::enable>(bitrate_id_);
            }
            return true;
          },
          [this]() { return bitrate_from_preset_; },
          "Default preset configuration",
          "Use default preset configuration.",
          bitrate_from_preset_)),
      bitrate_id_(pmanage<&property::PBag::make_unsigned_int>(
          "bitrate",
          [this](const uint32_t& value) {
            bitrate_ = value;
            return true;
          },
          [this]() { return bitrate_; },
          "Desired bitrate",
          "Value of the desired average bitrate.",
          bitrate_,
          1000000,
          20000000)),
      es_(std::make_unique<ThreadedWrapper<NVencES>>(this, 0)) {
  auto devices = CudaContext::get_devices();
  std::vector<std::string> names;
  for (auto& it : devices) {
    devices_nv_ids_.push_back(it.first);
    names.push_back(std::string("GPU #") + std::to_string(it.first) + " " + it.second);
  }
  if (names.empty()) {
    sw_info("ERROR:Could not find any NVENC-enabled GPU.");
    is_valid_ = false;
    return;
  }
  sw_info(CudaContext::get_support_description());
  devices_ = property::Selection<>(std::move(names), 0);
  update_device();
  devices_id_ = pmanage<&property::PBag::make_selection<>>(
      "gpu",
      [this](const quiddity::property::IndexOrName& val) {
        if (devices_.get_current_index() == val.index_) return true;
        devices_.select(val);
        update_device();
        return true;
      },
      [this]() { return devices_.get(); },
      "Encoder GPU",
      "Selection of the GPU used for encoding",
      devices_);
  pmanage<&property::PBag::set_to_current>(default_preset_id_);

  if (!es_) {
    is_valid_ = false;
    return;
  }
  is_valid_ = es_.get()->invoke<bool>([](NVencES* ctx) { return ctx->safe_bool_idiom(); });
}

void NVencPlugin::update_device() {
  es_.reset();
  es_ = std::make_unique<ThreadedWrapper<NVencES>>(this,
                                                   devices_nv_ids_[devices_.get_current_index()]);
  if (!es_.get()->invoke<bool>([](NVencES* ctx) { return ctx->safe_bool_idiom(); })) {
    sw_info(
        "ERROR: nvenc failed to create encoding session "
        "(the total number of simultaneous sessions "
        "may be reached)");
    sw_warning(
        "nvenc failed to create encoding session "
        "(the total number of simultaneous sessions "
        "may be reached)");
    es_.reset();  // this makes init method failing
    return;
  }
  update_codec();
}

void NVencPlugin::update_codec() {
  codecs_guids_ = es_->invoke<decltype(codecs_guids_)>(
      [](NVencES* ctx) { return ctx->get_supported_codecs(); });
  std::vector<std::string> names;
  for (auto& it : codecs_guids_) names.push_back(it.first);
  codecs_ = property::Selection<>(std::move(names), 0);
  auto set = [this](const quiddity::property::IndexOrName& val) {
    if (codecs_.get_current_index() != val.index_) {
      codecs_.select(val);
      update_preset();
      update_profile();
      update_max_width_height();
      update_input_formats();
    }
    return true;
  };
  auto get = [this]() { return codecs_.get_current_index(); };
  if (0 == codecs_id_)
    codecs_id_ = pmanage<&property::PBag::make_selection<>>(
        "codec", set, get, "Codec", "Video codec", codecs_);
  else
    pmanage<&property::PBag::replace>(
        codecs_id_,
        std::make_unique<property::Property<property::Selection<>, property::Selection<>::index_t>>(
            set, get, "Codec", "Video codec", codecs_, codecs_.size() - 1));
  update_preset();
  update_profile();
  update_max_width_height();
  update_input_formats();
}

void NVencPlugin::update_preset() {
  auto cur_codec = codecs_.get_current();
  auto guid_iter = std::find_if(
      codecs_guids_.begin(), codecs_guids_.end(), [&](const std::pair<std::string, GUID>& codec) {
        return codec.first == cur_codec;
      });
  presets_guids_ = es_->invoke<decltype(presets_guids_)>(
      [&](NVencES* ctx) { return ctx->get_presets(guid_iter->second); });
  std::vector<std::string> names;
  size_t index_low_lantency_default = 0;
  size_t current_index = 0;
  for (auto& it : presets_guids_) {
    names.push_back(it.first);
    // FIXME (the following does not work with SIP and sometimes with decoder)
    // if (it.first == "Low Latency default") index_low_lantency_default = current_index;
    ++current_index;
  }
  presets_ = property::Selection<>(std::move(names), index_low_lantency_default);
  auto set = [this](const quiddity::property::IndexOrName& val) {
    if (presets_.get_current_index() != val.index_) presets_.select(val);
    return true;
  };
  auto get = [this]() { return presets_.get(); };
  if (0 == presets_id_)
    presets_id_ = pmanage<&property::PBag::make_selection<>>(
        "preset", set, get, "Preset", "Encoding preset", presets_);
  else
    pmanage<&property::PBag::replace>(
        presets_id_,
        std::make_unique<
            property::Property<property::Selection<>, quiddity::property::IndexOrName>>(
            set, get, "Preset", "Encoding preset", presets_, presets_.size() - 1));
}

void NVencPlugin::update_profile() {
  auto cur_codec = codecs_.get_current();
  auto guid_iter = std::find_if(
      codecs_guids_.begin(), codecs_guids_.end(), [&](const std::pair<std::string, GUID>& codec) {
        return codec.first == cur_codec;
      });
  profiles_guids_ = es_->invoke<decltype(profiles_guids_)>(
      [&](NVencES* ctx) { return ctx->get_profiles(guid_iter->second); });
  std::vector<std::string> names;
  for (auto& it : profiles_guids_) names.push_back(it.first);
  profiles_ = property::Selection<>(std::move(names), 0);
  auto set = [this](const quiddity::property::IndexOrName& val) {
    if (profiles_.get_current_index() != val.index_) profiles_.select(val);
    return true;
  };
  auto get = [this]() { return profiles_.get(); };
  if (0 == profiles_id_)
    profiles_id_ = pmanage<&property::PBag::make_selection<>>(
        "profile", set, get, "Profile", "Encoding profile", profiles_);
  else
    pmanage<&property::PBag::replace>(
        profiles_id_,
        std::make_unique<
            property::Property<property::Selection<>, quiddity::property::IndexOrName>>(
            set, get, "Profile", "Encoding profile", profiles_, profiles_.size() - 1));
}

void NVencPlugin::update_max_width_height() {
  auto cur_codec = codecs_.get_current();
  auto guid_iter = std::find_if(
      codecs_guids_.begin(), codecs_guids_.end(), [&](const std::pair<std::string, GUID>& codec) {
        return codec.first == cur_codec;
      });
  auto mwh = es_->invoke<std::pair<int, int>>(
      [&](NVencES* ctx) { return ctx->get_max_width_height(guid_iter->second); });
  max_width_ = mwh.first;
  max_height_ = mwh.second;
  auto getwidth = [this]() { return this->max_width_; };
  if (0 == max_width_id_)
    max_width_id_ = pmanage<&property::PBag::make_int>("maxwidth",
                                                             nullptr,
                                                             getwidth,
                                                             "Max width",
                                                             "Max video source width",
                                                             max_width_,
                                                             max_width_,
                                                             max_width_);
  else
    pmanage<&property::PBag::notify>(max_width_id_);

  auto getheight = [this]() { return max_height_; };
  if (0 == max_height_id_)
    max_height_id_ = pmanage<&property::PBag::make_int>("maxheight",
                                                              nullptr,
                                                              getheight,
                                                              "Max height",
                                                              "Max video source height",
                                                              max_height_,
                                                              max_height_,
                                                              max_height_);
  else
    pmanage<&property::PBag::notify>(max_height_id_);
}

void NVencPlugin::update_input_formats() {
  auto cur_codec = codecs_.get_current();
  auto guid_iter = std::find_if(
      codecs_guids_.begin(), codecs_guids_.end(), [&](const std::pair<std::string, GUID>& codec) {
        return codec.first == cur_codec;
      });
  video_formats_.clear();
  video_formats_ = es_->invoke<decltype(video_formats_)>(
      [&](NVencES* ctx) { return ctx->get_input_formats(guid_iter->second); });
  for (auto& it : video_formats_) {
    std::string format;
    if ("NV12" == it.first)
      format = "NV12";
    else if ("YV12" == it.first)
      format = "YV12";
    else if ("IYUV" == it.first)
      format = "I420";
    else if ("YUV444" == it.first)
      format = "Y444";
    else if ("ARGB" == it.first)
      format = "ARGB";
    else if ("AYUV" == it.first)
      format = "AYUV";
    else
      sw_warning("format not supported by NVencPlugin : {}", it.first);

    if (!format.empty()) it.first = std::string("video/x-raw, format=(string)") + format;
  }
}

bool NVencPlugin::on_shmdata_disconnect() {
  shm_.reset(nullptr);
  es_.reset();
  es_ = std::make_unique<ThreadedWrapper<NVencES>>(this,
                                                   devices_nv_ids_[devices_.get_current_index()]);
  shmw_.reset(nullptr);

  pmanage<&property::PBag::enable>(devices_id_);
  pmanage<&property::PBag::enable>(presets_id_);
  pmanage<&property::PBag::enable>(profiles_id_);
  pmanage<&property::PBag::enable>(codecs_id_);
  pmanage<&property::PBag::enable>(max_width_id_);
  pmanage<&property::PBag::enable>(max_height_id_);
  pmanage<&property::PBag::enable>(default_preset_id_);
  pmanage<&property::PBag::set_to_current>(default_preset_id_);

  return true;
}

bool NVencPlugin::on_shmdata_connect(const std::string& shmpath) {
  // Needed to avoid concurrency with old shmdata follower.
  shm_.reset(nullptr);
  shm_.reset(new shmdata::Follower(
      this,
      shmpath,
      [this](void* data, size_t size) { this->on_shmreader_data(data, size); },
      [this](const std::string& data_descr) { this->on_shmreader_server_connected(data_descr); }));

  pmanage<&property::PBag::disable>(devices_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(presets_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(profiles_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(codecs_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(max_width_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(max_height_id_, property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(default_preset_id_,
                                          property::PBag::disabledWhenConnectedMsg);
  pmanage<&property::PBag::disable>(bitrate_id_, property::PBag::disabledWhenConnectedMsg);

  return true;
}

void NVencPlugin::on_shmreader_data(void* data, size_t size) {
  if (!es_.get()->invoke<bool>(
          [&](NVencES* ctx) { return ctx->copy_to_next_input_buffer(data, size); })) {
    sw_warning("error copying data to nvenc");
    return;
  }
  es_.get()->invoke_async([](NVencES* ctx) { ctx->encode_current_input(); });
  es_.get()->invoke_async([&](NVencES* ctx) {
    ctx->process_encoded_frame([&](void* data, uint32_t enc_size) {
      shmw_->writer<&::shmdata::Writer::copy_to_shm>(data, enc_size);
      shmw_->bytes_written(enc_size);
    });
  });
}

void NVencPlugin::on_shmreader_server_connected(const std::string& data_descr) {
  GstCaps* caps = gst_caps_from_string(data_descr.c_str());
  On_scope_exit {
    if (nullptr != caps) gst_caps_unref(caps);
  };
  GstStructure* s = gst_caps_get_structure(caps, 0);
  if (nullptr == s) {
    sw_warning("cannot get structure from caps (nvenc)");
    return;
  }
  gint width = 0, height = 0;
  if (!gst_structure_get_int(s, "width", &width) || !gst_structure_get_int(s, "height", &height)) {
    sw_warning("cannot get width/height from shmdata description (nvenc)");
    return;
  }
  gint frameNum = 0, frameDen = 0;
  if (!gst_structure_get_fraction(s, "framerate", &frameNum, &frameDen)) {
    sw_warning("cannot get framerate from shmdata description (nvenc)");
    return;
  }
  const char* format = gst_structure_get_string(s, "format");
  if (nullptr == format) {
    sw_warning("cannot get video format from shmdata description (nvenc)");
    return;
  }
  auto format_str = std::string(format);
  auto buf_format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
  if (format_str == "NV12")
    buf_format = NV_ENC_BUFFER_FORMAT_NV12;
  else if (format_str == "YV12")
    buf_format = NV_ENC_BUFFER_FORMAT_YV12;
  else if (format_str == "I420")
    buf_format = NV_ENC_BUFFER_FORMAT_IYUV;
  else if (format_str == "Y444")
    buf_format = NV_ENC_BUFFER_FORMAT_YUV444;
  else if (format_str == "ARGB")
    buf_format = NV_ENC_BUFFER_FORMAT_ARGB;
  else if (format_str == "AYUV")
    buf_format = NV_ENC_BUFFER_FORMAT_AYUV;
  else {
    sw_warning("video format {} not supported by switcher nvenc plugin", format_str);
    return;
  }

  auto cur_codec = codecs_.get_current();
  auto guid_iter = std::find_if(
      codecs_guids_.begin(), codecs_guids_.end(), [&](const std::pair<std::string, GUID>& codec) {
        return codec.first == cur_codec;
      });
  auto cur_preset = presets_.get_current();
  auto preset_iter = std::find_if(
      presets_guids_.begin(),
      presets_guids_.end(),
      [&](const std::pair<std::string, GUID>& preset) { return preset.first == cur_preset; });

  auto cur_profile = profiles_.get_current();
  auto profiles_iter = std::find_if(
      profiles_guids_.begin(),
      profiles_guids_.end(),
      [&](const std::pair<std::string, GUID>& profile) { return profile.first == cur_profile; });

  es_->invoke_async([&](NVencES* ctx) {
    ctx->initialize_encoder(guid_iter->second,
                            preset_iter->second,
                            profiles_iter->second,
                            bitrate_from_preset_ ? 0 : bitrate_,
                            width,
                            height,
                            frameNum,
                            frameDen,
                            buf_format);
  });
  shmw_.reset();

  std::string codec;
  if (cur_codec == "HEVC")
    codec = "x-h265";
  else
    codec = "x-h264";

  shmw_ = std::make_unique<shmdata::Writer>(
      this,
      claw_.get_shmpath_from_writer_label("video-encoded"),
      1,
      std::string(
          "video/" + codec +
          ", stream-format=(string)byte-stream, "
          "alignment=(string)au, profile=(string)baseline" +
          ", width=(int)" + std::to_string(width) + ", height=(int)" + std::to_string(height) +
          ", pixel-aspect-ratio=(fraction)1/1, framerate=(fraction)" + std::to_string(frameNum) +
          "/" + std::to_string(frameDen) + ", switcher-name=(string)" + get_manager_name() +
          ", quiddity-id=(int)" + std::to_string(qcontainer_->get_id(get_nickname()))));
}

}  // namespace quiddities
}  // namespace switcher
