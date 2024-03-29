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

#ifndef __SWITCHER_GST_DECODER_H__
#define __SWITCHER_GST_DECODER_H__

#include <memory>
#include "../gst/decodebin-to-shmdata.hpp"
#include "../gst/pipeliner.hpp"
#include "../gst/unique-gst-element.hpp"
#include "../quiddity/quiddity.hpp"
#include "../shmdata/follower.hpp"
#include "../utils/scope-exit.hpp"
#include "../utils/threaded-wrapper.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class GstDecodebin : public Quiddity {
 public:
  GstDecodebin(quiddity::Config&&);
  ~GstDecodebin() = default;
  GstDecodebin(const GstDecodebin&) = delete;
  GstDecodebin& operator=(const GstDecodebin&) = delete;

 private:
  CounterMap counter_{};
  std::unique_ptr<gst::Pipeliner> gst_pipeline_;
  gst::UGstElem shmsrc_;
  // registering connect/disconnect/can_sink_caps:
  std::string shmpath_to_decode_{};
  std::string shmpath_decoded_{};
  std::string cur_caps_{};
  std::unique_ptr<gst::DecodebinToShmdata> decoder_{nullptr};
  std::unique_ptr<shmdata::Follower> shmw_sub_{};
  std::unique_ptr<shmdata::Follower> shmr_sub_{};
  ThreadedWrapper<> async_this_{};
  static const std::string kConnectionSpec;  //!< Shmdata specifications

  bool on_shmdata_disconnect();
  bool on_shmdata_connect(const std::string& shmdata_socket_path);
  void configure_shmdatasink(GstElement* element,
                             const std::string& media_type,
                             const std::string& media_label);
  bool create_pipeline();
};

}  // namespace quiddities
}  // namespace switcher
#endif
