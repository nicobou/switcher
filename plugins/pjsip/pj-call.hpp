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

#ifndef PLUGINS_PJSIP_PJ_CALL_H_
#define PLUGINS_PJSIP_PJ_CALL_H_

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <pjsua-lib/pjsua.h>

#include "./pj-codec.hpp"
#include "./pj-ice-stream-trans.hpp"
#include "./pj-media-endpt.hpp"
#include "./pj-sip.hpp"
#include "switcher/gst/pipeliner.hpp"
#include "switcher/gst/rtp-session.hpp"
#include "switcher/gst/shmdata-to-cb.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"
#include "switcher/shmdata/writer.hpp"
#include "switcher/switcher.hpp"

namespace switcher {
namespace quiddities {
class PJCall {
 public:
  PJCall();
  ~PJCall() = default;
  PJCall(const PJCall&) = delete;
  PJCall& operator=(const PJCall&) = delete;
  void finalize_calls();

  enum class SendRecvStatus { DISCONNECTED, CONNECTING, CALLING, RECEIVING };
  static const std::map<SendRecvStatus, std::string> SendRecvStatusMap;

 private:
  /* Media stream created when the call is active. */
  using media_t = struct media_stream {
    std::string shm_path_to_send{};
    gst::RTPSender::id_t cb_id{0};
    pj_sockaddr def_addr;
  };

  // This is a call structure that is created when the application starts
  // and only destroyed when the application quits.
  using call_t = struct call {
    pjsip_inv_session* inv{nullptr};
    // as receiver
    std::vector<std::unique_ptr<shmdata::Writer>> rtp_writers_{};
    std::unique_ptr<PJICEStreamTrans> ice_trans_{};
    std::unique_ptr<gst::RTPSession> recv_rtp_session_{};
    std::mutex shm_subs_mtx_{};
    std::vector<std::unique_ptr<shmdata::GstTreeUpdater>> shm_subs_{};
    std::vector<std::unique_ptr<gst::RTPReceiver>> rtp_receivers_{};
    // as sender
    std::unique_ptr<PJICEStreamTrans> ice_trans_send_{};
    // media
    std::vector<media_t> media{};
    std::string peer_uri{};
  };

 private:
  static pjsip_module mod_siprtp_;
  pj_str_t local_addr_{nullptr, 0};
  bool is_calling_{false};
  bool is_hanging_up_{false};
  std::map<std::string, std::string> local_ips_{};
  std::mutex call_m_{};

  // We need separate mutexes for the special case of a contact calling itself.
  std::mutex finalize_incoming_calls_m_{};
  std::mutex finalize_outgoing_calls_m_{};

  bool can_create_calls_{true};
  std::condition_variable call_cv_{};
  bool call_action_done_{false};
  // internal rtp
  gst::RTPSession rtp_session_{};
  std::map<std::string, unsigned> reader_ref_count_{};
  std::vector<std::unique_ptr<call_t>> incoming_call_{};
  std::vector<std::unique_ptr<call_t>> outgoing_call_{};
  std::map<std::string, std::unique_ptr<gst::RTPSender>> readers_{};

  // sip functions
  static pj_bool_t on_rx_request(pjsip_rx_data* rdata);
  static void call_on_state_changed(pjsip_inv_session* inv, pjsip_event* e);
  static void call_on_forked(pjsip_inv_session* inv, pjsip_event* e);
  static void call_on_media_update(pjsip_inv_session* inv, pj_status_t status);
  static void call_on_rx_offer(pjsip_inv_session* inv, const pjmedia_sdp_session* offer);
  static void process_incoming_call(pjsip_rx_data* rdata);
  static pj_status_t create_sdp_answer(pj_pool_t* pool,
                                       struct call* call,
                                       const std::vector<pjmedia_sdp_media*>& media_to_receive,
                                       pjmedia_sdp_session** p_sdp);
  static pj_status_t parse_SDP_from_incoming_request(pjsip_rx_data* rdata,
                                                     pjmedia_sdp_session* offer);
  bool is_call_valid(const std::string& contact_uri);
  pjsip_dialog* create_sip_dialog(const pj_str_t local_uri, const pj_str_t remote_uri);
  bool send_invite_request(pjsip_dialog* dlg, call_t* call, const pjmedia_sdp_session* sdp);
  bool make_call(std::string contact_uri);
  bool create_outgoing_sdp(pjsip_dialog* dlg, call_t* call, pjmedia_sdp_session** res);
  quiddity::Quiddity::ptr retrieve_rtp_manager();
  bool send_to(const std::string& sip_url);
  void make_hang_up(pjsip_inv_session* inv);
  bool hang_up(const std::string& sip_url);
  bool hang_up_all_calls();
  bool attach_shmdata_to_contact(const std::string& shmpath,
                                 const std::string& contact_uri,
                                 bool attach);
  void make_attach_shmdata_to_contact(const std::string& shmpath,
                                      const std::string& contact_uri,
                                      bool attach);
  static void on_inv_state_disconnected(struct call* call,
                                        pjsip_inv_session* inv,
                                        pjsua_buddy_id id);
  static void on_inv_state_confirmed(struct call* call, pjsip_inv_session* inv, pjsua_buddy_id id);
  static void on_inv_state_early(struct call* call, pjsip_inv_session* inv, pjsua_buddy_id id);
  static void on_inv_state_connecting(struct call* call, pjsip_inv_session* inv, pjsua_buddy_id id);
  static bool release_incoming_call(call_t* call, pjsua_buddy_id id);
  static bool release_outgoing_call(call_t* call, pjsua_buddy_id id);
  static void print_sdp(const pjmedia_sdp_session* local_sdp);
  static bool negotiate_ice(PJICEStreamTrans* ice_trans,
                            const pjmedia_sdp_session* remote_sdp,
                            pj_pool_t* dlg_pool);
};

}  // namespace quiddities
}  // namespace switcher
#endif
