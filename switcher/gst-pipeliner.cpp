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
 * The GstPipeliner class
 */

// #include <gst/interfaces/xoverlay.h>
#include "./gst-pipeliner.hpp"
#include <algorithm>
#include "./g-source-wrapper.hpp"
#include "./gst-utils.hpp"
#include "./invocation-spec.hpp"
#include "./quiddity-container.hpp"
#include "./quiddity.hpp"
#include "./scope-exit.hpp"

namespace switcher {
GstPipeliner::GstPipeliner(GstPipe::on_msg_async_cb_t on_msg_async_cb,
                           GstPipe::on_msg_sync_cb_t on_msg_sync_cb)
    : GstPipeliner(on_msg_async_cb, on_msg_sync_cb, nullptr) {}

GstPipeliner::GstPipeliner(GstPipe::on_msg_async_cb_t on_msg_async_cb,
                           GstPipe::on_msg_sync_cb_t on_msg_sync_cb,
                           on_error_cb_t on_error_cb)
    : on_msg_async_cb_(on_msg_async_cb),
      on_msg_sync_cb_(on_msg_sync_cb),
      on_error_cb_(on_error_cb),
      main_loop_(std::make_unique<GlibMainLoop>()),
      gst_pipeline_(std::make_unique<GstPipe>(
          main_loop_->get_main_context(), &GstPipeliner::bus_sync_handler, this
          // [this](GstMessage *msg){
          //   if(this->on_msg_async_cb_)
          //     this->on_msg_async_cb_(msg);
          // },
          // [this](GstMessage *msg){
          //   if (GST_BUS_DROP == this->on_gst_error(msg))
          //     return GST_BUS_DROP;
          //   else {
          //     if (this->on_msg_sync_cb_)
          //       return this->on_msg_sync_cb_(msg);
          //     else
          //       return GST_BUS_PASS;
          //   }
          // }
          )) {
  if (!gst_pipeline_) {
    return;
  }

  GstUtils::g_idle_add_full_with_context(
      main_loop_->get_main_context(), G_PRIORITY_DEFAULT_IDLE, push_thread_context, this, nullptr);
}

GstPipeliner::~GstPipeliner() {
  std::unique_lock<std::mutex> lock(watch_mutex_);
  while (!watch_added_) cond_watch_.wait_for(lock, std::chrono::milliseconds(200));
}

gboolean GstPipeliner::push_thread_context(gpointer user_data) {
  auto context = static_cast<GstPipeliner*>(user_data);
  g_main_context_push_thread_default(context->main_loop_->get_main_context());

  std::unique_lock<std::mutex> lock(context->watch_mutex_);
  gst_bus_add_watch(
      gst_pipeline_get_bus(GST_PIPELINE(context->get_pipeline())), bus_watch, user_data);
  context->watch_added_ = true;
  context->cond_watch_.notify_one();

  return FALSE;
}

void GstPipeliner::play(gboolean play) {
  if (play) {
    gst_pipeline_->play(true);
  } else {
    gst_pipeline_->play(false);
  }
}

bool GstPipeliner::seek(gdouble position_in_ms) { return gst_pipeline_->seek(position_in_ms); }

// gboolean GstPipeliner::run_remove_quid(gpointer user_data) {
//   GstPipeliner *context = static_cast<GstPipeliner *>(user_data);
//   QuiddityContainer::ptr manager = context->qcontainer_.lock();
//   if (manager) {
//     // copying in case of self destruction
//     std::list<std::string> tmp = context->quids_to_remove_;
//     context->quids_to_remove_.clear();
//     for (auto &it: tmp)
//       manager->remove(it);
//   }
//   return FALSE;
// }

GstElement* GstPipeliner::get_pipeline() { return gst_pipeline_->get_pipeline(); }

// void
// GstPipeliner::print_one_tag(const GstTagList */*list*/,
//                             const gchar */*tag*/,
//                             gpointer /*user_data*/) {
//   // int i, num;

//   // num = gst_tag_list_get_tag_size (list, tag);
//   // for (i = 0; i < num; ++i) {
//   //   const GValue *val;

//   //   /* Note: when looking for specific tags, use the g_tag_list_get_xyz()
//   API,
//   //    * we only use the GValue approach here because it is more generic */
//   //   val = gst_tag_list_get_value_index (list, tag, i);
//   //   if (G_VALUE_HOLDS_STRING (val)) {
//   // g_print ("\t%20s : %s\n", tag, g_value_get_string (val));
//   //   } else if (G_VALUE_HOLDS_UINT (val)) {
//   // g_print ("\t%20s : %u\n", tag, g_value_get_uint (val));
//   //   } else if (G_VALUE_HOLDS_DOUBLE (val)) {
//   // g_print ("\t%20s : %g\n", tag, g_value_get_double (val));
//   //   } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
//   // g_print ("\t%20s : %s\n", tag,
//   //  (g_value_get_boolean (val)) ? "true" : "false");
//   //   } else if (GST_VALUE_HOLDS_BUFFER (val)) {
//   // g_print ("\t%20s : buffer of size %u\n", tag,
//   //  GST_BUFFER_SIZE (gst_value_get_buffer (val)));
//   //   } else if (GST_VALUE_HOLDS_DATE (val)) {
//   // g_print ("\t%20s : date (year=%u,...)\n", tag,
//   //  g_date_get_year (gst_value_get_date (val)));
//   //   } else {
//   // g_print ("\t%20s : tag of type '%s'\n", tag, G_VALUE_TYPE_NAME (val));
//   //   }
//   // }
// }

GstBusSyncReply GstPipeliner::on_gst_error(GstMessage* msg) {
  if (GST_MESSAGE_TYPE(msg) != GST_MESSAGE_ERROR) return GST_BUS_PASS;
  gchar* debug = nullptr;
  GError* error = nullptr;
  gst_message_parse_error(msg, &error, &debug);
  g_free(debug);
  On_scope_exit {
    if (error) g_error_free(error);
  };
  // on-error-gsource
  GSourceWrapper* gsrc =
      static_cast<GSourceWrapper*>(g_object_get_data(G_OBJECT(msg->src), "on-error-gsource"));
  if (nullptr != gsrc) {
    // removing command in order to get it invoked once
    g_object_set_data(G_OBJECT(msg->src), "on-error-gsource", nullptr);
    gsrc->attach(main_loop_->get_main_context());
  }
  if (on_error_cb_) on_error_cb_(GST_MESSAGE_SRC(msg), error);
  return GST_BUS_DROP;
}

gboolean GstPipeliner::bus_watch(GstBus* /*bus*/, GstMessage* message, gpointer user_data) {
  auto context = static_cast<GstPipeliner*>(user_data);
  if (context && context->on_msg_async_cb_) context->on_msg_async_cb_(message);
  return TRUE;
}

GstBusSyncReply GstPipeliner::bus_sync_handler(GstBus* /*bus*/,
                                               GstMessage* msg,
                                               gpointer user_data) {
  GstPipeliner* context = static_cast<GstPipeliner*>(user_data);
  auto res = GST_BUS_PASS;
  if (GST_BUS_DROP == context->on_gst_error(msg))
    return GST_BUS_DROP;
  else {
    if (context->on_msg_sync_cb_) {
      if (GST_BUS_DROP == context->on_msg_sync_cb_(msg)) return GST_BUS_DROP;
    }
  }

  // if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_QOS) {
  //   GstFormat format;
  //   guint64 processed;
  //   guint64 dropped;
  //   gst_message_parse_qos_stats(msg, &format, &processed, &dropped);
  //   // g_print ("QOS from %s, format %d, processed %lu dropped %lu\n",
  //   //  G_OBJECT_TYPE_NAME(G_OBJECT (msg->src)),
  //   //  format,
  //   //  processed,
  //   //  dropped);
  // }

  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    gchar* debug = nullptr;
    GError* error = nullptr;
    gst_message_parse_error(msg, &error, &debug);
    g_free(debug);
    g_error_free(error);
    res = GST_BUS_DROP;
  }

  // if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
  //   if (context->on_eos_cb_)
  //     context->on_eos_cb_();
  //   return GST_BUS_DROP;
  // }

  // FIXME:
  // const GstStructure *gstruct = gst_message_get_structure (msg);
  // if (nullptr != gstruct) {
  //   if (gst_structure_has_name(gstruct, "prepare-xwindow-id")) {
  //     guintptr *window_handle =
  //         (guintptr *) g_object_get_data(G_OBJECT(msg->src),
  //                                        "window-handle");
  //     if (window_handle != nullptr) {
  //       gst_x_overlay_set_window_handle(GST_X_OVERLAY(msg->src),
  //                                       *window_handle);
  //     }
  //   }
  // }

  // if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_TAG) {
  //   // GstTagList *tags = nullptr;
  //   // gst_message_parse_tag (msg, &tags);
  //   // g_print ("Got tags from element %s:\n", GST_OBJECT_NAME (msg->src));
  //   // gst_tag_list_foreach (tags, print_one_tag, nullptr);
  //   // g_print ("\n");
  //   // gst_tag_list_free (tags);
  // }

  return res;
}

}  // namespace switcher
