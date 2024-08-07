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

#include "./uridecodebin.hpp"
#include "../gst/utils.hpp"
#include "../utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(Uridecodebin,
                                     "urisrc",
                                     "URI/URL Player",
                                     "URI decoding to one or more shmdata",
                                     "LGPL",
                                     "Nicolas Bouillot");

const std::string Uridecodebin::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "custom%",
      "description": "Decoded streams",
      "can_do": [ "all" ]
    }
  ]
}
)");

void Uridecodebin::bus_async(GstMessage* msg) {
  if (GST_MESSAGE_TYPE(msg) != GST_MESSAGE_EOS) return;
  if (loop_) gst_pipeline_->seek(0);
}

Uridecodebin::Uridecodebin(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}),
      on_msg_async_cb_([this](GstMessage* msg) { this->bus_async(msg); }),
      on_msg_sync_cb_(nullptr),
      on_error_cb_([this](GstObject*, GError*) { this->error_ = true; }),
      gst_pipeline_(
          std::make_unique<gst::Pipeliner>(on_msg_async_cb_, on_msg_sync_cb_, on_error_cb_)) {
  if (!gst::utils::make_element("uridecodebin", &uridecodebin_)) {
    is_valid_ = false;
    return;
  }

  pmanage<&property::PBag::make_string>(
      "uri",
      [this](const std::string& val) {
        // first reset all existing writers from dynamic specs
        for (const auto& swid : claw_.get_swids()) {
          claw_.remove_writer_from_meta(swid);
        }
        uri_ = val;
        return to_shmdata();
      },
      [this]() { return uri_; },
      "URI",
      "URI To Be Redirected Into Shmdata(s)",
      "");

  pmanage<&property::PBag::make_bool>(
      "loop",
      [this](const bool val) {
        loop_ = val;
        return true;
      },
      [this]() { return loop_; },
      "Looping",
      "Loop media",
      loop_);
}

Uridecodebin::~Uridecodebin() { destroy_uridecodebin(); }

void Uridecodebin::init_uridecodebin() {
  if (!gst::utils::make_element("uridecodebin", &uridecodebin_)) {
    sw_warning("cannot create uridecodebin");
    return;
  }

  // discard_next_uncomplete_buffer_ = false;
  rtpgstcaps_ = gst_caps_from_string("application/x-rtp, media=(string)application");
  sig_handles_.emplace_back(g_signal_connect(G_OBJECT(uridecodebin_),
                                             "pad-added",
                                             (GCallback)Uridecodebin::uridecodebin_pad_added_cb,
                                             (gpointer)this));
  sig_handles_.emplace_back(g_signal_connect(G_OBJECT(uridecodebin_),
                                             "unknown-type",
                                             (GCallback)Uridecodebin::unknown_type_cb,
                                             (gpointer)this));
  sig_handles_.emplace_back(g_signal_connect(G_OBJECT(uridecodebin_),
                                             "autoplug-continue",
                                             (GCallback)Uridecodebin::autoplug_continue_cb,
                                             (gpointer)this));
  sig_handles_.emplace_back(g_signal_connect(G_OBJECT(uridecodebin_),
                                             "autoplug-select",
                                             (GCallback)Uridecodebin::autoplug_select_cb,
                                             (gpointer)this));
  sig_handles_.emplace_back(g_signal_connect(G_OBJECT(uridecodebin_),
                                             "source-setup",
                                             (GCallback)Uridecodebin::source_setup_cb,
                                             (gpointer)this));
  if (sig_handles_.end() != std::find_if(sig_handles_.begin(),
                                         sig_handles_.end(),
                                         [](const gulong& handle) { return handle <= 0; })) {
    sw_warning("subscription to a g_signal failed in uridecodeing quiddity");
  }
  g_object_set(
      G_OBJECT(uridecodebin_), "expose-all-streams", TRUE, "async-handling", TRUE, nullptr);
}

void Uridecodebin::destroy_uridecodebin() {
  std::lock_guard<std::mutex> lock(pipe_mtx_);
  if (uridecodebin_) {
    for (const auto& it : sig_handles_) {
      g_signal_handler_disconnect(uridecodebin_, it);
    }
  }
  sig_handles_.clear();
  for (const auto& it : decodebin_handles_) {
    g_signal_handler_disconnect(it.first, it.second);
  }
  decodebin_handles_.clear();
  gst_pipeline_.reset();
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(on_msg_async_cb_, on_msg_sync_cb_, on_error_cb_);
  shm_subs_.clear();
}

void Uridecodebin::source_setup_cb(GstBin* /*bin*/, GstElement* /*source*/, gpointer user_data) {
  Uridecodebin* context = static_cast<Uridecodebin*>(user_data);
  if (context->error_) context->gst_pipeline_->play(false);
}

void Uridecodebin::unknown_type_cb(GstElement* bin,
                                   GstPad* pad,
                                   GstCaps* caps,
                                   gpointer user_data) {
  Uridecodebin* context = static_cast<Uridecodebin*>(user_data);
  context->sw_warning("Uridecodebin unknown type: {} ({})",
                      std::string(gst_caps_to_string(caps)),
                      std::string(gst_element_get_name(bin)));
  std::lock_guard<std::mutex> lock(context->pipe_mtx_);
  context->pad_to_shmdata_writer(context->gst_pipeline_->get_pipeline(), pad);
}

gboolean sink_factory_filter(GstPluginFeature* feature, gpointer data) {
  GstCaps* caps = (GstCaps*)data;
  if (!GST_IS_ELEMENT_FACTORY(feature)) return FALSE;
  const GList* static_pads =
      gst_element_factory_get_static_pad_templates(GST_ELEMENT_FACTORY(feature));
  int not_any_number = 0;
  for (GList* item = (GList*)static_pads; item; item = item->next) {
    GstStaticPadTemplate* padTemplate = (GstStaticPadTemplate*)item->data;
    GstPadTemplate* pad = gst_static_pad_template_get(padTemplate);
    GstCaps* padCaps = gst_pad_template_get_caps(pad);
    if (!gst_caps_is_any(padCaps)) not_any_number++;
  }
  if (not_any_number == 0) return FALSE;
  if (!gst_element_factory_list_is_type(GST_ELEMENT_FACTORY(feature),
                                        GST_ELEMENT_FACTORY_TYPE_DECODABLE))
    return FALSE;
  if (!gst_element_factory_can_sink_all_caps(GST_ELEMENT_FACTORY(feature), caps)) return FALSE;
  return TRUE;
}

int Uridecodebin::autoplug_continue_cb(GstElement* /*bin */,
                                       GstPad* pad,
                                       GstCaps* caps,
                                       gpointer /*user_data*/) {
  GList* list = gst_registry_feature_filter(
      gst_registry_get(), (GstPluginFeatureFilter)sink_factory_filter, FALSE, caps);
  int length = g_list_length(list);
  gst_plugin_feature_list_free(list);

  if (length == 0 || pad_is_image(get_pad_name(pad))) return 0;

  return 1;
}

int Uridecodebin::autoplug_select_cb(GstElement* /*bin */,
                                     GstPad* /*pad */,
                                     GstCaps* caps,
                                     GstElementFactory* factory,
                                     gpointer user_data) {
  auto context = static_cast<Uridecodebin*>(user_data);
  context->sw_debug("uridecodebin autoplug select {}, (factory {})",
                    std::string(gst_caps_to_string(caps)),
                    std::string(GST_OBJECT_NAME(factory)));
  if (g_strcmp0(GST_OBJECT_NAME(factory), "rtpgstdepay") == 0) return 1;  // expose
  return 0;                                                               // try
}

void Uridecodebin::release_buf(void* user_data) {
  GstBuffer* buf = static_cast<GstBuffer*>(user_data);
  gst_buffer_unref(buf);
}

std::string Uridecodebin::get_pad_name(GstPad* pad) {
  std::string padname;
  {
    GstCaps* padcaps = gst_pad_get_current_caps(pad);
    if (nullptr == padcaps) return padname;
    On_scope_exit { gst_caps_unref(padcaps); };
    gchar* padcapsstr = gst_caps_to_string(padcaps);
    On_scope_exit { g_free(padcapsstr); };
    if (0 == g_strcmp0("ANY", padcapsstr)) {
      padname = "custom";
    } else {
      padname = gst_structure_get_name(gst_caps_get_structure(padcaps, 0));
    }
  }
  return padname;
}

bool Uridecodebin::pad_is_image(const std::string& padname) {
  return stringutils::starts_with(padname, "image");
}

void Uridecodebin::decodebin_pad_added_cb(GstElement* object, GstPad* pad, gpointer user_data) {
  Uridecodebin* context = static_cast<Uridecodebin*>(user_data);
  std::lock_guard<std::mutex> lock(context->pipe_mtx_);
  GstBin* bin = static_cast<GstBin*>(g_object_get_data(G_OBJECT(object), "bin"));
  GstElement* shmdatasink =
      static_cast<GstElement*>(g_object_get_data(G_OBJECT(object), "shmdatasink"));

  GstElement* imagefreeze = nullptr;
  gst::utils::make_element("imagefreeze", &imagefreeze);
  gst_bin_add_many(GST_BIN(bin), imagefreeze, shmdatasink, nullptr);

  GstPad* sinkpad = gst_element_get_static_pad(imagefreeze, "sink");
  On_scope_exit { gst_object_unref(sinkpad); };
  gst_pad_link(pad, sinkpad);
  gst_element_link(imagefreeze, shmdatasink);
  gst::utils::sync_state_with_parent(imagefreeze);
  gst::utils::sync_state_with_parent(shmdatasink);
}

void Uridecodebin::pad_to_shmdata_writer(GstElement* bin, GstPad* pad) {
  // detecting type of media
  bool stream_is_image = false;
  std::string padname = get_pad_name(pad);
  gchar** padname_split = g_strsplit_set(padname.c_str(), "/", -1);
  On_scope_exit { g_strfreev(padname_split); };
  stream_is_image = pad_is_image(padname);

  sw_debug("uridecodebin new pad name is {}", padname);
  GstElement* shmdatasink = nullptr;
  gst::utils::make_element("shmdatasink", &shmdatasink);

  if (stream_is_image) {
    GstElement* decodebin = nullptr;
    gst::utils::make_element("decodebin", &decodebin);
    decodebin_handles_.emplace(decodebin,
                               g_signal_connect(G_OBJECT(decodebin),
                                                "pad-added",
                                                (GCallback)Uridecodebin::decodebin_pad_added_cb,
                                                (gpointer)this));
    g_object_set_data(G_OBJECT(decodebin), "decodebin", decodebin);
    g_object_set_data(G_OBJECT(decodebin), "shmdatasink", shmdatasink);
    g_object_set_data(G_OBJECT(decodebin), "bin", bin);
    gst_bin_add(GST_BIN(bin), decodebin);
    gst::utils::sync_state_with_parent(decodebin);
    GstPad* sinkpad = gst_element_get_static_pad(decodebin, "sink");
    On_scope_exit { gst_object_unref(sinkpad); };
    if (GST_PAD_LINK_OK != gst_pad_link(pad, sinkpad))
      sw_warning("pad link failed from uridecodebin to decodebin (fixed image in uridecodebin)");
  } else {
    gst_bin_add(GST_BIN(bin), shmdatasink);
    GstPad* sinkpad = gst_element_get_static_pad(shmdatasink, "sink");
    On_scope_exit { gst_object_unref(sinkpad); };
    if (GST_PAD_LINK_OK != gst_pad_link(pad, sinkpad))
      sw_warning("pad link failed from uridecodebin to shmdatasink (uridecodebin)");
  }

  // counting
  auto name_splited = padname_split[0] ? std::string(padname_split[0]) : std::string();
  auto count = counter_.get_count(name_splited);
  std::string media_name = std::string(padname_split[0]) + "-" + std::to_string(count);
  // find caps in order  to register new claw
  GstCaps* pad_caps = gst_pad_get_current_caps(pad);
  On_scope_exit { gst_caps_unref(pad_caps); };
  char* pad_caps_str = gst_caps_to_string(pad_caps);
  On_scope_exit { free(pad_caps_str); };
  sw_debug("uridecodebin: new media {}, caps is {}", media_name, std::string(pad_caps_str));
  auto swid = claw_.add_writer_to_meta(claw_.get_swid("custom%"),
                                       {media_name, media_name, {std::string(pad_caps_str)}});
  std::string shmpath = claw_.get_writer_shmpath(swid);
  sw_debug("uridecodebin: shmpath is {}", shmpath);
  g_object_set(G_OBJECT(shmdatasink), "socket-path", shmpath.c_str(), nullptr);
  auto extra_caps = get_quiddity_caps();
  g_object_set(G_OBJECT(shmdatasink), "extra-caps-properties", extra_caps.c_str(), nullptr);

  shm_subs_.emplace_back(std::make_unique<shmdata::GstTreeUpdater>(
      this, shmdatasink, shmpath, shmdata::GstTreeUpdater::Direction::writer));
  if (!stream_is_image) gst::utils::sync_state_with_parent(shmdatasink);
}

void Uridecodebin::uridecodebin_pad_added_cb(GstElement* object, GstPad* pad, gpointer user_data) {
  Uridecodebin* context = static_cast<Uridecodebin*>(user_data);
  std::lock_guard<std::mutex> lock(context->pipe_mtx_);
  GstCaps* newcaps = gst_pad_get_current_caps(pad);
  On_scope_exit { gst_caps_unref(newcaps); };
  if (gst_caps_can_intersect(context->rtpgstcaps_, newcaps)) {
    // asking rtpbin to send an event when a packet is lost (do-lost property)
    gst::utils::set_element_property_in_bin(object, "gstrtpbin", "do-lost", TRUE);
    context->sw_debug("custom rtp stream found");
    GstElement* rtpgstdepay = nullptr;
    gst::utils::make_element("rtpgstdepay", &rtpgstdepay);

    // adding a probe for discarding uncomplete packets
    GstPad* depaysrcpad = gst_element_get_static_pad(rtpgstdepay, "src");
    On_scope_exit { gst_object_unref(depaysrcpad); };
    gst_bin_add(GST_BIN(context->gst_pipeline_->get_pipeline()), rtpgstdepay);
    GstPad* sinkpad = gst_element_get_static_pad(rtpgstdepay, "sink");
    On_scope_exit { gst_object_unref(sinkpad); };
    gst::utils::check_pad_link_return(gst_pad_link(pad, sinkpad));
    GstPad* srcpad = gst_element_get_static_pad(rtpgstdepay, "src");
    gst::utils::sync_state_with_parent(rtpgstdepay);
    On_scope_exit { gst_object_unref(srcpad); };
    gst_element_get_state(rtpgstdepay, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    context->pad_to_shmdata_writer(context->gst_pipeline_->get_pipeline(), srcpad);
  } else {
    context->pad_to_shmdata_writer(context->gst_pipeline_->get_pipeline(), pad);
  }
}

bool Uridecodebin::to_shmdata() {
  if (uri_.empty()) {
    sw_warning("no uri to decode");
    return false;
  }
  if (!gst_uri_is_valid(uri_.c_str())) {
    sw_warning("uri {} is invalid (uridecodebin)", uri_);
    return false;
  }
  destroy_uridecodebin();
  init_uridecodebin();
  counter_.reset_counter_map();
  sw_debug("to_shmdata set uri {}", uri_);
  g_object_set(G_OBJECT(uridecodebin_), "uri", uri_.c_str(), nullptr);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), uridecodebin_);
  gst_pipeline_->play(true);
  return true;
}

}  // namespace quiddities
}  // namespace switcher
