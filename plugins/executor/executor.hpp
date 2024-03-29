/*
 * This file is part of switcher-executor.
 *
 * switcher-executor is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_EXECUTOR_H__
#define __SWITCHER_EXECUTOR_H__

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <wordexp.h>
#include <memory>
#include <regex>
#include <string>
#include <gst/gst.h>

#include "switcher/quiddity/quiddity.hpp"
#include "switcher/quiddity/startable.hpp"
#include "switcher/shmdata/follower.hpp"
#include "switcher/utils/periodic-task.hpp"
#include "switcher/utils/string-utils.hpp"

namespace switcher {
namespace quiddities {

using namespace quiddity;
class Executor : public Quiddity, public Startable {
 public:
  Executor(quiddity::Config&&);
  ~Executor();
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

 private:
  bool start() final;
  bool stop() final;
  bool on_shmdata_connect(const std::string& shmpath, claw::sfid_t sfid);
  bool on_shmdata_disconnect(claw::sfid_t sfid);
  pid_t spawn_child(char* program, char** arg_list);
  void clean_up_child_process();
  void monitor_process();
  bool read_outputs();
  bool graft_output(const std::string& type, const std::string& escaped_value);

  static const std::string kConnectionSpec;  //!< Shmdata specifications
  struct sigaction sigchld_action_;
  int child_return_code_{0};
  int cout_pipe_[2];
  int cerr_pipe_[2];
  bool user_stopped_{false};
  bool restart_{false};
  pid_t child_pid_{0};
  posix_spawnattr_t attr_;
  posix_spawn_file_actions_t act_;
  std::string shmpath_{};
  std::string shmpath_audio_{};
  std::string shmpath_video_{};
  std::unique_ptr<PeriodicTask<>> monitoring_task_{nullptr};
  std::unique_ptr<shmdata::Follower> follower_video_{nullptr};
  std::unique_ptr<shmdata::Follower> follower_audio_{nullptr};
  std::unique_ptr<shmdata::Follower> follower_{nullptr};

  std::string command_line_{};
  property::prop_id_t command_line_id_;

  bool autostart_{false};
  property::prop_id_t autostart_id_;
  bool restart_on_change_{false};
  property::prop_id_t restart_on_change_id_;
  bool periodic_{false};
  property::prop_id_t periodic_id_;

  std::string whitelist_caps_{};
  property::prop_id_t whitelist_caps_id_;
};
SWITCHER_DECLARE_PLUGIN(Executor);
}  // namespace quiddities
}  // namespace switcher
#endif
