/*
 * This file is part of switcher-pulse.
 *
 * switcher-pulse is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_PULSE_SRC_H__
#define __SWITCHER_PULSE_SRC_H__

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <condition_variable>
#include <mutex>
#include "switcher/gst/glibmainloop.hpp"
#include "switcher/gst/pipeliner.hpp"
#include "switcher/gst/unique-gst-element.hpp"
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/quiddity/startable.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class PulseSrc : public Quiddity, public Startable {
 public:
  PulseSrc(quiddity::Config&&);
  ~PulseSrc();

 private:
  typedef struct {
    std::string name_{};
    std::string description_{};
    std::string state_{};
    std::string sample_format_{};
    std::string sample_rate_{};
    std::string channels_{};
    std::vector<std::pair<std::string /*port*/, std::string /*description*/>> ports_{};
    std::string active_port_{};
    std::string bus_path_{};
  } DeviceDescription;

  static const std::string kConnectionSpec;  //!< Shmdata specifications
  std::unique_ptr<gst::GlibMainLoop> mainloop_;
  std::string shmpath_{};
  gst::UGstElem pulsesrc_{"pulsesrc"};
  gst::UGstElem shmsink_{"shmdatasink"};
  std::unique_ptr<gst::Pipeliner> gst_pipeline_;
  std::unique_ptr<shmdata::GstTreeUpdater> shm_sub_{nullptr};
  // pulse devices
  bool connected_to_pulse_{false};
  std::mutex devices_mutex_{};
  std::condition_variable devices_cond_{};
  // property:
  // device enum members
  property::Selection<> devices_{{"none"}, 0};
  property::prop_id_t devices_id_{0};
  property::prop_id_t volume_id_{0};
  property::prop_id_t mute_id_{0};
  property::Selection<> save_device_enum_{{"port", "device"}, 0};
  bool is_loading_{false};

  // pulse_audio
  pa_glib_mainloop* pa_glib_mainloop_{nullptr};
  pa_mainloop_api* pa_mainloop_api_{nullptr};
  pa_context* pa_context_{nullptr};
  char* server_{nullptr};
  std::vector<DeviceDescription> capture_devices_{};
  // quit
  std::mutex quit_mutex_{};
  std::condition_variable quit_cond_{};

  bool start() final;
  bool stop() final;
  InfoTree::ptr on_saving() final;
  void on_loading(InfoTree::ptr&& tree) final;
  void on_loaded() final;

  bool remake_elements();
  static gboolean async_get_pulse_devices(void* user_data);
  void update_capture_device();
  void make_device_description(pa_context* pulse_context);
  // device enum and select
  static void pa_context_state_callback(pa_context* c, void* userdata);
  static void get_source_info_callback(pa_context* c,
                                       const pa_source_info* i,
                                       int is_last,
                                       void* userdata);
  static void on_pa_event_callback(pa_context* c,
                                   pa_subscription_event_type_t t,
                                   uint32_t idx,
                                   void* userdata);
  static gboolean quit_pulse(void* user_data);
};

SWITCHER_DECLARE_PLUGIN(PulseSrc);
}  // namespace quiddities
}  // namespace switcher
#endif
