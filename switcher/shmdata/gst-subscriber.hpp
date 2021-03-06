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

#ifndef __SWITCHER_GST_SHMDATA_TO_TREE_H__
#define __SWITCHER_GST_SHMDATA_TO_TREE_H__

#include <gst/gst.h>
#include <atomic>
#include <future>
#include <mutex>
#include "../utils/periodic-task.hpp"
#include "./stat.hpp"

namespace switcher {
namespace shmdata {
class GstSubscriber {
 public:
  using on_caps_cb_t = std::function<void(const std::string&)>;
  using on_stat_monitor_t = std::function<void(const Stat&)>;
  using on_delete_t = std::function<void()>;
  using on_connection_status_t = std::function<void(bool status)>;

  GstSubscriber(GstElement* element,
                on_caps_cb_t on_caps_cb,
                on_stat_monitor_t on_stat_monitor_cb,
                on_delete_t on_delete_cb = nullptr,
                on_connection_status_t on_connection_status_cb = nullptr,
                std::chrono::milliseconds update_interval = Stat::kDefaultUpdateInterval);

  GstSubscriber() = delete;
  GstSubscriber(const GstSubscriber&) = delete;
  GstSubscriber& operator=(const GstSubscriber&) = delete;
  ~GstSubscriber();

 private:
  static void on_caps_cb(GObject* gobject, GParamSpec* pspec, gpointer user_data);
  static void on_connection_status_cb(GObject* gobject, GParamSpec* pspec, gpointer user_data);
  void notify_caps();
  void notify_connection();
  void stat_monitor();

  std::mutex dtor_mutex_{};
  GstElement* element_;
  on_caps_cb_t on_caps_cb_;
  on_stat_monitor_t on_stat_monitor_cb_;
  on_delete_t on_delete_cb_;
  on_connection_status_t on_connection_status_cb_;
  PeriodicTask<> ptask_;
  gulong signal_handler_id_{0};
  gulong signal_connection_id_{0};
};

}  // namespace shmdata
}  // namespace switcher
#endif
