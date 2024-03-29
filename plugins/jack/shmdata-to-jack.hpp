/*
 * This file is part of switcher-jack.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_SHMDATA_TO_JACK_H__
#define __SWITCHER_SHMDATA_TO_JACK_H__

#include <memory>
#include <mutex>
#include "./audio-ring-buffer.hpp"
#include "./drift-observer.hpp"
#include "./jack-client.hpp"
#include "switcher/gst/pipeliner.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class ShmdataToJack : public Quiddity {
 public:
  ShmdataToJack(quiddity::Config&&);
  ~ShmdataToJack() = default;
  ShmdataToJack(const ShmdataToJack&) = delete;
  ShmdataToJack& operator=(const ShmdataToJack&) = delete;

 private:
  static const std::string kConnectionSpec;  //!< Shmdata specifications
  unsigned int kMaxNumberOfChannels{128};
  bool is_constructed_{false};

  // internal use:
  std::string shmpath_{};
  GstElement* shmdatasrc_{nullptr};
  GstElement* audiobin_{nullptr};
  gulong handoff_handler_{0};
  unsigned short channels_{0};
  unsigned int debug_buffer_usage_{1000};
  std::mutex output_ports_mutex_{};
  std::mutex ports_to_connect_mutex_{};
  std::vector<AudioRingBuffer<jack_sample_t>> ring_buffers_{};  // one per channel

  // jack sample is the time unit, assuming gst pipeline has the same sample
  // rate:
  DriftObserver<jack_nframes_t> drift_observer_{};

  // properties
  std::string client_name_{};
  std::string server_name_{};
  property::prop_id_t client_name_id_{0};
  property::prop_id_t server_name_id_{0};
  bool auto_connect_{true};
  std::string connect_to_{"system:playback_%d"};
  property::prop_id_t connect_to_id_;
  unsigned int index_{1};
  property::prop_id_t index_id_{0};
  property::prop_id_t auto_connect_id_;
  bool connect_all_to_first_{false};
  property::prop_id_t connect_all_to_first_id_;
  bool connect_only_first_{false};
  property::prop_id_t connect_only_first_id_;
  bool do_format_conversion_{true};
  property::prop_id_t do_format_conversion_id_;
  bool do_rate_conversion_{true};
  property::prop_id_t do_rate_conversion_id_;

  // gst pipeline:
  std::unique_ptr<gst::Pipeliner> gst_pipeline_;

  // shmsubscriber (publishing to the information-tree):
  std::unique_ptr<shmdata::GstTreeUpdater> shm_sub_{nullptr};

  // jack client
  std::vector<std::string> ports_to_connect_{};
  std::mutex port_to_connect_in_jack_process_mutex_{};
  std::vector<std::pair<std::string, std::string>> port_to_connect_in_jack_process_{};
  std::unique_ptr<JackClient> jack_client_{nullptr};
  std::vector<JackPort> output_ports_{};

  bool start();
  bool stop();
  void update_ports_to_connect();
  void connect_ports();
  void disconnect_ports();
  void on_port(jack_port_t* port);
  bool on_shmdata_disconnect();
  bool on_shmdata_connect(const std::string& shmdata_socket_path);
  bool make_elements();
  void on_channel_update(unsigned int channels);
  void clean_output_ports();
  void on_xrun(uint num_of_missed_samples);
  static void on_handoff_cb(GstElement* object, GstBuffer* buf, GstPad* pad, gpointer user_data);
  static int jack_process(jack_nframes_t nframes, void* arg);
};

SWITCHER_DECLARE_PLUGIN(ShmdataToJack);
}  // namespace quiddities
}  // namespace switcher
#endif
