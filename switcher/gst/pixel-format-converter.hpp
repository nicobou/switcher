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

#ifndef __SWITCHER_GST_PIXEL_FORMAT_CONVERTER_H__
#define __SWITCHER_GST_PIXEL_FORMAT_CONVERTER_H__

#include <memory>
#include "../utils/safe-bool-idiom.hpp"
#include "./pipeliner.hpp"
#include "./unique-gst-element.hpp"

namespace switcher {
namespace gst {
class PixelFormatConverter : public SafeBoolIdiom {
 public:
  using uptr = std::unique_ptr<PixelFormatConverter>;
  PixelFormatConverter(const std::string& shmpath_to_convert,
                          const std::string& shmpath_converted,
                          const std::string& format_name,
                          const std::string& extra_caps_properties);
  PixelFormatConverter() = delete;
  ~PixelFormatConverter() = default;
  PixelFormatConverter(const PixelFormatConverter&) = delete;
  PixelFormatConverter& operator=(const PixelFormatConverter&) = delete;

  static bool can_sink_caps(const std::string& caps);

  // for external shm subscribers...
  GstElement* get_shmsink() { return shm_converted_.get_raw(); }
  GstElement* get_shmsrc() { return shmsrc_.get_raw(); }

 private:
  UGstElem shmsrc_{"shmdatasrc"};
  UGstElem queue_codec_element_{"queue"};
  UGstElem color_space_codec_element_{"videoconvert"};
  UGstElem capsfilter_{"capsfilter"};
  UGstElem shm_converted_{"shmdatasink"};

  // safe bool idiom:
  bool is_valid_{false};
  std::unique_ptr<Pipeliner> gst_pipeline_;

  std::string get_caps_str(const std::string& format_name) const;
  bool safe_bool_idiom() const final { return is_valid_; }
};

}  // namespace gst
}  // namespace switcher
#endif
