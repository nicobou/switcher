/*
 * This file is part of switcher-pjsip.
 *
 * switcher-pjsip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * switcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with switcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SWITCHER_PJSIP_PLUGIN_H__
#define __SWITCHER_PJSIP_PLUGIN_H__

#include <pjsua-lib/pjsua.h>
#include <atomic>
#include <memory>
#include "./pj-call.hpp"
#include "./pj-presence.hpp"
#include "./pj-sip.hpp"
#include "./pj-stun-turn.hpp"
#include "./pj-whitelist.hpp"
#include "switcher/quiddity/container.hpp"
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/utils/threaded-wrapper.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class SIPPlugin : public Quiddity {
  friend PJCall;
  friend PJPresence;
  friend PJSIP;
  friend PJStunTurn;
  friend PJWhiteList;
  friend PJICEStreamTrans;

 public:
  SIPPlugin(quiddity::Config&&);
  ~SIPPlugin();
  SIPPlugin(const SIPPlugin&) = delete;
  SIPPlugin& operator=(const SIPPlugin&) = delete;
  bool start();
  bool stop();

 private:
  static const std::string kConnectionSpec;  //!< Shmdata specifications
  pjsua_transport_id transport_id_{-1};
  unsigned int sip_port_{5060};
  property::prop_id_t port_id_;
  std::string default_dns_address_;
  std::string dns_address_;

  // log related variables
  int log_level_{2};
  int console_log_level_{2};
  std::string log_filename_;

  property::prop_id_t dns_address_id_{0};
  bool decompress_streams_{true};
  property::prop_id_t decompress_streams_id_;
  std::unique_ptr<PJCall> sip_calls_{nullptr};
  std::unique_ptr<PJPresence> sip_presence_{nullptr};
  std::unique_ptr<PJStunTurn> stun_turn_{nullptr};
  std::unique_ptr<PJWhiteList> white_list_{nullptr};
  std::unique_ptr<ThreadedWrapper<PJSIP>> pjsip_{};

  // singleton related members:
  bool i_m_the_one_{false};
  static std::atomic<unsigned short> sip_plugin_used_;
  static SIPPlugin* this_;

  // Expose incoming streams as quiddities feature
  std::map<std::string /*peer_uri*/, std::vector<std::string> /*quid_names_*/> exposed_quiddities_;
  std::mutex exposed_quiddities_mutex_{};
  unsigned int quiddity_removal_cb_id_{0};

  bool start_sip_transport();
  void apply_log_level_configuration();
  void apply_port_configuration();
  void apply_configuration();
  void create_quiddity_stream(const std::string& peer_uri, const std::string& quid_name, const std::string& nickname = "");
  void expose_stream_to_quiddity(const std::string& quid_name, const std::string& shmpath);
  void remove_exposed_quiddity(const std::string& peer_uri, const std::string& quid_name);
  void remove_exposed_quiddities(const std::string& peer_uri);
  std::string get_exposed_quiddity_name_from_shmpath(const std::string& shmpath);

  // on_saving and on_saved methods set DNS server to "default" when system (default) DNS is used.
  InfoTree::ptr on_saving() final;
  void on_saved() final;

  bool can_sink_caps(const std::string& str_caps);
};

}  // namespace quiddities
}  // namespace switcher
#endif
