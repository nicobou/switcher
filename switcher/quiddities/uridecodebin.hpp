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

#ifndef __SWITCHER_URIDECODEBIN_H__
#define __SWITCHER_URIDECODEBIN_H__

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "../gst/pipeliner.hpp"
#include "../quiddity/quiddity.hpp"
#include "../shmdata/gst-tree-updater.hpp"
#include "../utils/counter-map.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class Uridecodebin : public Quiddity {
 public:
  Uridecodebin(quiddity::Config&&);
  ~Uridecodebin();

 private:
  static const std::string kConnectionSpec;  //!< Shmdata specifications

  std::mutex pipe_mtx_{};
  std::map<GstElement*, gulong> decodebin_handles_{};
  std::vector<gulong> sig_handles_{};
  gst::Pipe::on_msg_async_cb_t on_msg_async_cb_{nullptr};
  gst::Pipe::on_msg_sync_cb_t on_msg_sync_cb_{nullptr};
  gst::Pipeliner::on_error_cb_t on_error_cb_{nullptr};
  bool error_{false};

  GstElement* uridecodebin_{nullptr};
  GstCaps* rtpgstcaps_{nullptr};
  bool discard_next_uncomplete_buffer_{false};
  bool loop_{false};  //!< custom properties
  bool playing_{true};
  std::string uri_{};
  CounterMap counter_{};
  std::vector<std::unique_ptr<shmdata::GstTreeUpdater>> shm_subs_{};
  std::unique_ptr<gst::Pipeliner> gst_pipeline_;

  void init_uridecodebin();
  void destroy_uridecodebin();
  void bus_async(GstMessage* msg);
  bool to_shmdata();

  static std::string get_pad_name(GstPad* pad);
  static bool pad_is_image(const std::string& padname);
  static void decodebin_pad_added_cb(GstElement* object, GstPad* pad, gpointer user_data);
  static void uridecodebin_pad_added_cb(GstElement* object, GstPad* pad, gpointer user_data);
  static gboolean to_shmdata_wrapped(gpointer uri, gpointer user_data);
  static gboolean event_probe_cb(GstPad* pad, GstEvent* event, gpointer data);
  static gboolean process_eos(gpointer user_data);
  static void source_setup_cb(GstBin* /*bin*/, GstElement* /*source*/, gpointer /*user_data*/);
  static void unknown_type_cb(GstElement* bin, GstPad* pad, GstCaps* caps, gpointer user_data);
  static int autoplug_continue_cb(GstElement* bin, GstPad* pad, GstCaps* caps, gpointer user_data);
  static int autoplug_select_cb(
      GstElement* bin, GstPad* pad, GstCaps* caps, GstElementFactory* factory, gpointer user_data);
  static void on_handoff_cb(GstElement*, GstBuffer*, GstPad*, gpointer);
  static void release_buf(void*);
  void pad_to_shmdata_writer(GstElement* bin, GstPad* pad);
};

}  // namespace quiddities
}  // namespace switcher
#endif
