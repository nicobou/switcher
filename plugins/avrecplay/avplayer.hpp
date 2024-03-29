/*
 * This file is part of switcher-recplay.
 *
 * switcher-recplay is free software; you can redistribute it and/or
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

#ifndef SWITCHER_AVPLAYER_HPP
#define SWITCHER_AVPLAYER_HPP

#include "switcher/gst/pipeliner.hpp"
#include "switcher/quiddity/startable.hpp"
#include "switcher/shmdata/follower.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"
#include "switcher/shmdata/writer.hpp"
#include "switcher/utils/threaded-wrapper.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class AVPlayer : public Quiddity, public Startable {
 public:
  AVPlayer(quiddity::Config&&);
  bool start() final;
  bool stop() final;

 private:
  static const std::string kConnectionSpec;  //!< Shmdata specifications

  struct ShmFile {
    ShmFile(const std::string& shmpath, const std::string& filepath, const std::string& sink_name)
        : shmpath_(shmpath), filepath_(filepath), sink_name_(sink_name){};
    std::string shmpath_{};
    std::string filepath_{};
    std::string sink_name_{};
    GstElement* sink_element_{nullptr};
    std::unique_ptr<shmdata::GstTreeUpdater> shmsink_sub_{nullptr};
  };

  //! Shmdata methods
  GstBusSyncReply bus_async(GstMessage* msg);

  std::vector<std::unique_ptr<ShmFile>> files_list_{};
  GstElement* avplay_bin_{nullptr};  //!< Full recording pipeline
  std::unique_ptr<gst::Pipeliner> gst_pipeline_{nullptr};
  int64_t track_duration_{0};
  std::string playpath_{};
  int position_{0};
  property::prop_id_t position_id_{0};
  std::unique_ptr<PeriodicTask<>> position_task_{};
  bool pause_{false};
  bool seek_called_{false};
  std::mutex seek_mutex_{};
  std::unique_ptr<ThreadedWrapper<>> th_{std::make_unique<ThreadedWrapper<>>()};
};
}  // namespace quiddities
}  // namespace switcher

#endif
