/*
 * This file is part of switcher-myplugin.
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

#ifndef __SWITCHER_PJSIP_H__
#define __SWITCHER_PJSIP_H__

#include "switcher/quiddity.h"
#include "switcher/custom-property-helper.h"

#include <pjsua-lib/pjsua.h>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace switcher
{
  
  class PJSIP : public Quiddity
  {
  public:
    SWITCHER_DECLARE_QUIDDITY_PUBLIC_MEMBERS(PJSIP);
    PJSIP ();
    ~PJSIP ();
    PJSIP (const PJSIP &) = delete;
    PJSIP &operator= (const PJSIP &) = delete;
    bool init ();
    bool start ();
    bool stop ();
  private:
    CustomPropertyHelper::ptr custom_props_;
    unsigned sip_port_;
    GParamSpec *sip_port_spec_;
    pjsua_transport_id *transport_id_;
    pj_thread_desc thread_handler_desc_; 
    pj_thread_t	*pj_thread_ref_; 
    std::thread sip_thread_;
    std::mutex pj_init_mutex_;
    std::condition_variable pj_init_cond_;
    bool pj_sip_inited_;
    std::mutex work_mutex_;
    std::condition_variable work_cond_;
    std::mutex done_mutex_;
    std::condition_variable done_cond_;
    std::mutex registration_mutex_;
    std::condition_variable registration_cond_;
    bool continue_;
    std::function<void()> command_;
    pjsua_acc_id account_id_;
    void sip_init_shutdown_thread ();
    void sip_handling_thread ();
    static void on_buddy_state(pjsua_buddy_id buddy_id);
    bool pj_sip_init ();
    void exit_cmd ();
    void run_command_sync (std::function<void()> command);
    static void set_port (const gint value, void *user_data);
    static gint get_port (void *user_data);

    void register_account (const std::string &sip_user, 
			   const std::string &sip_domain, 
			   const std::string &sip_password);
    static gboolean register_account_wrapped (gchar *user, gchar *domain, gchar *password, void *user_data);
    static  void on_registration_state (pjsua_acc_id acc_id, pjsua_reg_info *info);
    void add_buddy (const std::string &sip_user);
    static pjmedia_transport *on_create_media_transport (pjsua_call_id call_id,
							 unsigned media_idx,
							 pjmedia_transport *base_tp,
							 unsigned flags);
    static void on_incoming_call (pjsua_acc_id acc_id, 
				  pjsua_call_id call_id,
				  pjsip_rx_data *rdata);

  };
  
  SWITCHER_DECLARE_PLUGIN(PJSIP);

}  // end of namespace

#endif // ifndef
