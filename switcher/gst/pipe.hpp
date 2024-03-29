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

/**
 * RAII GStreamer pipeline
 */

#ifndef __SWITCHER_GST_PIPELINE_H__
#define __SWITCHER_GST_PIPELINE_H__

#include <gst/gst.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include "../utils/bool-log.hpp"

namespace switcher {
namespace gst {

class Pipe {
 public:
  using on_msg_async_cb_t = std::function<void(GstMessage*)>;
  using on_msg_sync_cb_t = std::function<GstBusSyncReply(GstMessage*)>;
  Pipe(GMainContext* context,
          GstBusSyncReply (*bus_sync_cb)(GstBus* /*bus*/, GstMessage* msg, gpointer user_data),
          gpointer user_data);
  ~Pipe();
  Pipe() = delete;
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
  bool play(bool play);
  BoolLog seek(gdouble position);
  BoolLog seek_key_frame(gdouble position);
  BoolLog speed(gdouble speed);
  GstElement* get_pipeline();

 private:
  typedef struct {  // GstBus is a specific context:
    GSource source;
    GstBus* bus;
    gboolean inited;
  } GstBusSource;

  GMainContext* gmaincontext_{nullptr};
  GstElement* pipeline_{nullptr};
  GSourceFuncs source_funcs_;
  gdouble speed_{1.0};
  GSource* source_{nullptr};
  gint64 length_{0};
  std::mutex end_{};
  std::condition_variable end_cond_{};
  bool playing_asked_{false};
  std::condition_variable play_cond_{};
  void query_position_and_length();
  static gboolean gst_pipeline_delete(gpointer user_data);
  static gboolean source_prepare(GSource* source, gint* timeout);
  static gboolean source_check(GSource* source);
  static gboolean source_dispatch(GSource* source, GSourceFunc callback, gpointer user_data);
  static void source_finalize(GSource* source);
  static gboolean gst_play(gpointer user_data);
};

}  // namespace gst
}  // namespace switcher
#endif
