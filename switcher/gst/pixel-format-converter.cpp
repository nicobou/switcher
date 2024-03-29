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

#include "./pixel-format-converter.hpp"
#include "../utils/scope-exit.hpp"

namespace switcher {
namespace gst {
PixelFormatConverter::PixelFormatConverter(const std::string& shmpath_to_convert,
                                                 const std::string& shmpath_converted,
                                                 const std::string& format_name,
                                                 const std::string& extra_caps_properties)
    : gst_pipeline_(std::make_unique<Pipeliner>(nullptr, nullptr)) {
  if (shmpath_converted.empty() || shmpath_to_convert.empty()) {
    return;
  }
  if (shmpath_to_convert == shmpath_converted) {
    return;
  }
  GstCaps* caps = gst_caps_from_string(get_caps_str(format_name).c_str());
  g_object_set(G_OBJECT(capsfilter_.get_raw()), "caps", caps, nullptr);
  gst_caps_unref(caps);
  g_object_set(G_OBJECT(shmsrc_.get_raw()), "socket-path", shmpath_to_convert.c_str(), nullptr);

  auto nthreads_videoconvert = gst::utils::get_nthreads_property_value();
  if (nthreads_videoconvert > 0) {
    g_object_set(G_OBJECT(color_space_codec_element_.get_raw()),
                 "n-threads",
                 nthreads_videoconvert,
                 nullptr);
  }

  g_object_set(G_OBJECT(shm_converted_.get_raw()),
               "socket-path",
               shmpath_converted.c_str(),
               "sync",
               false,
               "extra-caps-properties",
               extra_caps_properties.c_str(),
               nullptr);
  gst_bin_add_many(GST_BIN(gst_pipeline_->get_pipeline()),
                   shmsrc_.get_raw(),
                   queue_codec_element_.get_raw(),
                   color_space_codec_element_.get_raw(),
                   capsfilter_.get_raw(),
                   shm_converted_.get_raw(),
                   nullptr);
  gst_element_link_many(shmsrc_.get_raw(),
                        queue_codec_element_.get_raw(),
                        color_space_codec_element_.get_raw(),
                        capsfilter_.get_raw(),
                        shm_converted_.get_raw(),
                        nullptr);
  gst_pipeline_->play(true);
  is_valid_ = true;
}

std::string PixelFormatConverter::get_caps_str(const std::string& format_name) const {
  return std::string("video/x-raw, format=(string)") + format_name;
}

bool PixelFormatConverter::can_sink_caps(const std::string& caps) {
  return gst::utils::can_sink_caps("videoconvert", caps);
}

}  // namespace gst
}  // namespace switcher
