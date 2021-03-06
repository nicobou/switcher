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

#ifndef __SWITCHER_RTPSESSION_H__
#define __SWITCHER_RTPSESSION_H__

#include <memory>
#include "../utils/safe-bool-idiom.hpp"
#include "./pipeliner.hpp"
#include "./rtp-receiver.hpp"
#include "./rtp-sender.hpp"

namespace switcher {
namespace gst {
class RTPSession : public SafeBoolIdiom {
  friend RTPSender;
  friend RTPReceiver;

 public:
  RTPSession();
  ~RTPSession() = default;
  RTPSession(const RTPSession&) = delete;
  RTPSession(RTPSession&&) = delete;
  RTPSession& operator=(const RTPSession&) = delete;

 private:
  std::unique_ptr<Pipeliner> gst_pipeline_;
  GstElement* rtpsession_;

  // safe bool idiom
  bool safe_bool_idiom() const final { return nullptr != rtpsession_ && gst_pipeline_; }
};

}  // namespace gst
}  // namespace switcher
#endif
