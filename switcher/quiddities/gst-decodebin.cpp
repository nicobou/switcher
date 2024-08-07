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

#include "./gst-decodebin.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(GstDecodebin,
                                     "decoder",
                                     "Decoder",
                                     "Generic shmdata decoder",
                                     "LGPL",
                                     "Nicolas Bouillot");

const std::string GstDecodebin::kConnectionSpec(R"(
{
"follower":
  [
    {
      "label": "to-decode",
      "description": "Stream to be decoded",
      "can_do": [ "all" ]
    }
  ],
"writer":
  [
    {
      "label": "custom%",
      "description": "Produced random buffers",
      "can_do": [ "video/x-raw", "audio/x-raw", "text/x-raw" ]
    }
  ]
}
)");

GstDecodebin::GstDecodebin(quiddity::Config&& conf)
    : Quiddity(
          std::forward<quiddity::Config>(conf),
          {kConnectionSpec,
           [this](const std::string& shmpath, claw::sfid_t) { return on_shmdata_connect(shmpath); },
           [this](claw::sfid_t sfid) { return on_shmdata_disconnect(); }}),
      gst_pipeline_(std::make_unique<gst::Pipeliner>(nullptr, nullptr)),
      shmsrc_("shmdatasrc") {
  if (!shmsrc_) {
    is_valid_ = false;
    return;
  }
}

bool GstDecodebin::on_shmdata_disconnect() {
  if (shmpath_to_decode_.empty()) {
    return false;
  }
  cur_caps_.clear();
  shmw_sub_.reset();
  shmr_sub_.reset();
  gst_pipeline_->play(false);
  gst_pipeline_ = nullptr;
  counter_.reset_counter_map();
  shmpath_to_decode_.clear();
  shmpath_decoded_.clear();

  return true;
}

void GstDecodebin::configure_shmdatasink(GstElement* element,
                                         const std::string& media_type,
                                         const std::string& media_label) {
  auto count = counter_.get_count(media_label + media_type);
  std::string media_name = media_type;
  if (count != 0) media_name.append("-" + std::to_string(count));
  if (media_label.empty()) {
    shmpath_decoded_ = claw_.get_writer_shmpath(
        claw_.add_writer_to_meta(claw_.get_swid("custom%"), {media_name, media_name}));
  } else {
    shmpath_decoded_ = claw_.get_writer_shmpath(claw_.add_writer_to_meta(
        claw_.get_swid("custom%"), {media_label + "-" + media_name, media_label}));
  }

  auto extra_caps = get_quiddity_caps();
  g_object_set(G_OBJECT(element), "socket-path", shmpath_decoded_.c_str(), "extra-caps-properties", extra_caps.c_str(), nullptr);
  shmw_sub_ = std::make_unique<shmdata::Follower>(this,
                                                  shmpath_decoded_,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr,
                                                  shmdata::Stat::kDefaultUpdateInterval,
                                                  shmdata::Follower::Direction::writer,
                                                  true);
}

bool GstDecodebin::on_shmdata_connect(const std::string& shmpath) {
  if (shmpath.empty()) {
    sw_error("shmpath must not be empty");
    return false;
  }
  if (shmpath == shmpath_decoded_) {
    sw_error("decoder cannot connect to itself");
    return false;
  }
  shmpath_to_decode_ = shmpath;
  shmr_sub_.reset();
  shmw_sub_.reset();

  if (!create_pipeline()) {
    return false;
  }
  shmr_sub_ = std::make_unique<shmdata::Follower>(
      this,
      shmpath_to_decode_,
      nullptr,
      [this](const std::string& caps) {
        if (!cur_caps_.empty() && cur_caps_ != caps) {
          cur_caps_ = caps;
          sw_debug(
              "decoder restarting shmdata connection "
              "because of updated caps ({})",
              cur_caps_);
          async_this_.run_async([this]() { on_shmdata_connect(shmpath_to_decode_); });

          return;
        }
        cur_caps_ = caps;
      },
      nullptr,
      shmdata::Stat::kDefaultUpdateInterval,
      shmdata::Follower::Direction::reader,
      true);

  return true;
}

bool GstDecodebin::create_pipeline() {
  if (gst_pipeline_ != nullptr) {
    gst_pipeline_->play(false);
  }
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr);
  if (!gst::UGstElem::renew(shmsrc_)) return false;

  // creating shmdata reader
  g_object_set(G_OBJECT(shmsrc_.get_raw()),
               "copy-buffers",
               TRUE,
               "socket-path",
               shmpath_to_decode_.c_str(),
               nullptr);

  // creating decodebin
  auto decodebin = std::make_unique<gst::DecodebinToShmdata>(
      gst_pipeline_.get(),
      [this](GstElement* el, const std::string& media_type, const std::string& media_label) {
        configure_shmdatasink(el, media_type, media_label);
      },
      [this]() { sw_warning("discarding uncomplete custom frame due to a network loss"); },
      nullptr,
      true /*decompress*/);

  // adding to pipeline
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), shmsrc_.get_raw());
  if (!decodebin->invoke_with_return<gboolean>([this](GstElement* el) {
        return gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), el);
      })) {
  }

  // get pads and link
  GstPad* pad = gst_element_get_static_pad(shmsrc_.get_raw(), "src");
  On_scope_exit { gst_object_unref(GST_OBJECT(pad)); };
  GstPad* sinkpad = decodebin->invoke_with_return<GstPad*>(
      [](GstElement* el) { return gst_element_get_static_pad(el, "sink"); });
  On_scope_exit { gst_object_unref(GST_OBJECT(sinkpad)); };
  if (!gst::utils::check_pad_link_return(gst_pad_link(pad, sinkpad))) return false;

  // set media label if relevant
  auto caps = gst_pad_get_allowed_caps(pad);
  if (0 < gst_caps_get_size(caps)) {
    On_scope_exit { gst_caps_unref(caps); };
    auto structure = gst_caps_get_structure(caps, 0);
    auto media_label = gst_structure_get_string(structure, "media-label");
    if (media_label) decodebin->set_media_label(stringutils::base64_decode(media_label));
  }

  // save the decodebin
  decoder_ = std::move(decodebin);

  if (!static_cast<bool>(gst_pipeline_.get())) {
    return false;
  }
  gst_pipeline_->play(true);
  return true;
}

}  // namespace quiddities
}  // namespace switcher
