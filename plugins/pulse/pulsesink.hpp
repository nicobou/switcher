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

#ifndef __SWITCHER_PULSE_SINK_H__
#define __SWITCHER_PULSE_SINK_H__

#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <condition_variable>
#include <mutex>
#include <vector>
#include "switcher/gst/glibmainloop.hpp"
#include "switcher/gst/pipeliner.hpp"
#include "switcher/gst/unique-gst-element.hpp"
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class PulseSink : public Quiddity {
 public:
  PulseSink(quiddity::Config&&);
  ~PulseSink();
  PulseSink(const PulseSink&) = delete;
  PulseSink& operator=(const PulseSink&) = delete;

 private:
  typedef struct {
    std::string name_{};
    std::string description_{};
    std::string state_{};
    std::string sample_format_{};
    std::string sample_rate_{};
    std::string channels_{};
    std::vector<std::pair<std::string /*port */, std::string /*description */>> ports_{};
    std::string active_port_{};
  } DeviceDescription;

  static const std::string kConnectionSpec;  //!< Shmdata specifications
  std::unique_ptr<gst::GlibMainLoop> mainloop_;
  // gst pipeline:
  std::unique_ptr<gst::Pipeliner> gst_pipeline_;
  // shmsubscriber (publishing to the information-tree):
  std::unique_ptr<shmdata::GstTreeUpdater> shm_sub_{nullptr};
  // internal use:
  std::string shmpath_{};
  gst::UGstElem shmsrc_{"shmdatasrc"};
  gst::UGstElem audioconvert_{"audioconvert"};
  gst::UGstElem pulsesink_{"pulsesink"};
  pa_glib_mainloop* pa_glib_mainloop_{nullptr};
  pa_mainloop_api* pa_mainloop_api_{nullptr};
  pa_context* pa_context_{nullptr};
  char* server_{nullptr};
  std::vector<DeviceDescription> devices_{};  // indexed by pulse_device_name
  std::mutex devices_mutex_{};
  std::condition_variable devices_cond_{};
  bool connected_to_pulse_{false};
  //  property:
  property::Selection<> devices_enum_{{"none"}, 0};
  property::prop_id_t devices_enum_id_{0};
  property::prop_id_t volume_id_{0};
  property::prop_id_t mute_id_{0};
  // quit
  std::mutex quit_mutex_{};
  std::condition_variable quit_cond_{};

  bool on_shmdata_connect(const std::string& shmpath);
  bool on_shmdata_disconnect();
  bool remake_elements();
  void make_device_description(pa_context* pulse_context);
  void update_output_device();
  static void pa_context_state_callback(pa_context* c, void* userdata);
  static void get_sink_info_callback(pa_context* c,
                                     const pa_sink_info* i,
                                     int is_last,
                                     void* userdata);
  static void on_pa_event_callback(pa_context* c,
                                   pa_subscription_event_type_t t,
                                   uint32_t idx,
                                   void* userdata);
  static gboolean async_get_pulse_devices(void* user_data);
  static gboolean quit_pulse(void* user_data);
};

SWITCHER_DECLARE_PLUGIN(PulseSink);
}  // namespace quiddities
}  // namespace switcher
#endif
