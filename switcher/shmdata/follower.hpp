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

#ifndef __SWITCHER_SHMDATA_FOLLOWER_H__
#define __SWITCHER_SHMDATA_FOLLOWER_H__

#include <functional>
#include <memory>
#include <shmdata/follower.hpp>
#include <shmdata/reader.hpp>
#include <string>

#include "../utils/periodic-task.hpp"
#include "./stat.hpp"
#include "./switcher-logger.hpp"

namespace switcher {
namespace shmdata {

class Follower {
 public:
  enum class Direction { writer, reader };
  Follower(quiddity::Quiddity* quid,
           const std::string& path,
           ::shmdata::Reader::onData cb,
           ::shmdata::Reader::onServerConnected osc = nullptr,
           ::shmdata::Reader::onServerDisconnected osd = nullptr,
           std::chrono::milliseconds update_interval = Stat::kDefaultUpdateInterval,
           Direction dir = Direction::reader,
           bool get_shmdata_on_connect = false);
  ~Follower();
  Follower(const Follower&) = delete;
  Follower& operator=(const Follower&) = delete;

 private:
  quiddity::Quiddity* quid_;
  // shmdata stats
  Stat shm_stat_{};
  std::mutex bytes_mutex_{};
  // shmdata follower related members:
  SwitcherLogger shmlogger_;
  std::string data_type_{};
  ::shmdata::Reader::onData od_;
  ::shmdata::Reader::onServerConnected osc_;
  ::shmdata::Reader::onServerDisconnected osd_;
  std::string tree_path_;
  Direction dir_;
  bool get_shmdata_on_connect_{false};
  std::unique_ptr<::shmdata::Follower> follower_;
  std::unique_ptr<PeriodicTask<>> task_;

  void on_data(void* data, size_t data_size);
  void on_server_connected(const std::string& data_type);
  void on_server_disconnected();
  void update_quid_stats();
  void initialize_tree(const std::string& tree_path);
};

}  // namespace shmdata
}  // namespace switcher
#endif
