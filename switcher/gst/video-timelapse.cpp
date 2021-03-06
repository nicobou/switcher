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

#include "./video-timelapse.hpp"
#include <iostream>
#include "../quiddity/property/gprop-to-prop.hpp"
#include "../quiddity/quiddity.hpp"
#include "../utils/scope-exit.hpp"
#include "./utils.hpp"

namespace switcher {
namespace gst {
VideoTimelapse::VideoTimelapse(const VideoTimelapseConfig& config,
                               quiddity::Quiddity* quid,
                               on_new_file_t on_new_file)
    : config_(config),
      on_new_file_(on_new_file),
      gst_pipeline_(std::make_unique<Pipeliner>(
          [this](GstMessage* msg) {
            if (msg->type != GST_MESSAGE_ELEMENT) return;
            const GstStructure* s = gst_message_get_structure(msg);
            auto name = std::string(gst_structure_get_name(s));
            if (name != "GstMultiFileSink") return;
            if (on_new_file_) on_new_file_(gst_structure_get_string(s, "filename"));
          },
          nullptr)) {
  GError* error = nullptr;

  auto nb_threads_gst_element = gst::utils::get_nthreads_property_value();
  std::string nthreads_property{};
  if (nb_threads_gst_element > 0) {
    nthreads_property += "n-threads=" + std::to_string(nb_threads_gst_element);
  }

  std::string width_height;
  if (0 != config_.width_ && 0 != config_.height_)
    width_height = std::string(" ! videoscale ") + nthreads_property + " ! video/x-raw, " +
                   " width=" + std::to_string(config_.width_) +
                   ", height=" + std::to_string(config_.height_) + ", pixel-aspect-ratio=1/1";
  else if (0 != config_.width_ && 0 == config_.height_)
    width_height = std::string(" ! videoscale ") + nthreads_property + " ! video/x-raw, " +
                   " width=" + std::to_string(config_.width_) + ", pixel-aspect-ratio=1/1";
  else if (0 == config_.width_ && 0 != config_.height_)
    width_height = std::string(" ! videoscale ") + nthreads_property + " ! video/x-raw, " +
                   " height=" + std::to_string(config_.height_) + ", pixel-aspect-ratio=1/1";

  // FIXME: pixel-aspect-ratio is fixed here because videorate absolutely needs it and it is not
  // possible to know the caps at the moment we create this pipeline. Also a shmdata connected to
  // this quiddity must
  // provide a framerate. Ideally we would detect that we have these informations and put default
  // ones if we don't.

  std::string description(
      std::string("shmdatasrc socket-path=") + config_.orig_shmpath_ +
      " copy-buffers=true do-timestamp=true ! queue ! video/x-raw, " +
      "pixel-aspect-ratio=1/1 ! videorate ! video/x-raw, framerate=" +
      std::to_string(config_.framerate_num_) + "/" + std::to_string(config_.framerate_denom_) +
      width_height + " ! videoconvert " + nthreads_property +
      " ! jpegenc quality=" + std::to_string(config_.jpg_quality_) +
      " ! multifilesink post-messages=true " + +" max-files=" + std::to_string(config_.max_files_) +
      " location=\"" + config_.image_path_ + "\"");

  GstElement* bin = gst_parse_bin_from_description(description.c_str(), TRUE, &error);
  if (error != nullptr) {
#ifdef DEBUG
    std::cerr << error->message << '\n';
#endif
    g_error_free(error);
  }
  GstElement* shmdatasrc =
      gst::utils::get_first_element_from_factory_name(GST_BIN(bin), "shmdatasrc");
  shmsrc_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
      quid, shmdatasrc, config_.orig_shmpath_, shmdata::GstTreeUpdater::Direction::reader);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), bin);
  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);
  gst_pipeline_->play(true);
  is_valid_ = true;
}

}  // namespace gst
}  // namespace switcher
