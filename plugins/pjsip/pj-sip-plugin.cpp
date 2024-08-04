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

#include "./pj-sip-plugin.hpp"
#include "switcher/utils/net-utils.hpp"
#include "switcher/utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_DECLARE_PLUGIN(SIPPlugin);
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(SIPPlugin,
                                     "sip",
                                     "SIP (Session Initiation Protocol)",
                                     "Manages user sessions",
                                     "LGPL",
                                     "Nicolas Bouillot");

SIPPlugin* SIPPlugin::this_ = nullptr;

std::atomic<unsigned short> SIPPlugin::sip_plugin_used_(0);

const std::string SIPPlugin::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "rtp%",
      "description": "rtp streams received",
      "can_do": ["application/x-rtp"]
    },
    {
      "label": "stream%",
      "description": "Stream received by ",
      "can_do": [ "all" ]
    }
  ]
}
)");

SIPPlugin::SIPPlugin(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}),
      port_id_(pmanage<&property::PBag::make_string>(
          "port",
          [this](const std::string& valstr) {
            if (valstr.empty()) return false;
            unsigned int val;
            if (!isdigit(valstr[0])) return false;
            val = atoi(valstr.c_str());
            if (sip_port_ == val && -1 != transport_id_) return true;
            if (val > 65535) return false;
            if (transport_id_ != -1) {
              sw_debug("SIP port changed for {}, closing existing SIP transport port {}",
                       std::to_string(val),
                       std::to_string(sip_port_));
              pjsua_transport_close(transport_id_, PJ_FALSE);
            }
            sip_port_ = val;
            return pjsip_->run<bool>([this]() { return start_sip_transport(); });
          },
          [this]() { return std::to_string(sip_port_); },
          "SIP Port",
          "SIP port used when registering",
          std::to_string(sip_port_))),
      default_dns_address_(netutils::get_system_dns()),
      dns_address_(default_dns_address_),
      dns_address_id_(pmanage<&property::PBag::make_string>(
          "dns_addr",
          [this](const std::string& requested_val) {
            auto val = requested_val;
            if (val.empty()) return false;
            if ("default" == requested_val) val = default_dns_address_;
            if (!netutils::is_valid_IP(val)) {
              sw_warning("not a valid IP address, expected x.y.z.a with x, y, z, a in [0:255].");
              return false;
            }

            // Try setting the new DNS address, if it doesn't work try the old one.
            auto old_dns_address = dns_address_;
            dns_address_ = val;
            if (!pjsip_->invoke<bool>(
                    [&](PJSIP* ctx) { return ctx->create_resolver(dns_address_); })) {
              dns_address_ = old_dns_address;
              sw_warning("could not set the DNS address (PJSIP), setting the old one {}.",
                         dns_address_);
              pjsip_->invoke<bool>([&](PJSIP* ctx) { return ctx->create_resolver(dns_address_); });
              return false;
            }

            return true;
          },
          [this]() { return dns_address_; },
          "DNS address",
          "IP address used for DNS",
          dns_address_)),
      decompress_streams_id_(pmanage<&property::PBag::make_bool>(
          "decompress",
          [this](const bool val) {
            decompress_streams_ = val;
            return true;
          },
          [this]() { return decompress_streams_; },
          "Decompress",
          "Decompress received streams",
          decompress_streams_)) {
  if (1 == sip_plugin_used_.fetch_or(1)) {
    sw_warning("an other sip quiddity is instancied, cannot init");
    is_valid_ = false;
    return;
  }
  i_m_the_one_ = true;
  this_ = this;

  apply_log_level_configuration();
  apply_port_configuration();

  pjsip_ = std::make_unique<ThreadedWrapper<PJSIP>>(
      // init
      [&]() {
        start_sip_transport();
        sip_calls_ = std::make_unique<PJCall>();
        white_list_ = std::make_unique<PJWhiteList>();
        sip_presence_ = std::make_unique<PJPresence>();
        stun_turn_ = std::make_unique<PJStunTurn>();
        return true;
      },
      // destruct
      [&]() {
        sip_calls_.reset(nullptr);
        sip_presence_.reset(nullptr);
        stun_turn_.reset(nullptr);
        white_list_.reset(nullptr);
        if (transport_id_ != -1) pjsua_transport_close(transport_id_, PJ_FALSE);
      });

  if (!pjsip_->invoke<bool>([](PJSIP* ctx) { return ctx->safe_bool_idiom(); })) {
    is_valid_ = false;
    return;
  }
  apply_configuration();

  quiddity_removal_cb_id_ = qcontainer_->register_removal_cb([this](auto quid_id) {
    std::lock_guard<std::mutex> lock(exposed_quiddities_mutex_);
    auto quiddity_name = qcontainer_->get_nickname(quid_id);
    for (auto& peer : exposed_quiddities_) {
      auto& exposed_quids = peer.second;
      auto it = std::find(exposed_quids.begin(), exposed_quids.end(), quiddity_name);
      if (it != exposed_quids.end()) {
        exposed_quids.erase(it);
      }
    }
  });

  this_->mmanage<&method::MBag::make_method<std::function<bool(std::string)>>>(
      "can-sink-caps",
      infotree::json::deserialize(
          R"(
                  {
                   "name" : "Can sink caps",
                   "description" : "Can SIP contacts connect with this caps",
                   "arguments" : [
                     {
                        "long name" : "String Caps",
                        "description" : "Caps as a string"
                     }
                   ]
                  }
              )"),
      [this](const std::string& caps) { return can_sink_caps(caps); });
}

SIPPlugin::~SIPPlugin() {
  if (!i_m_the_one_) return;

  qcontainer_->unregister_removal_cb(quiddity_removal_cb_id_);

  sip_calls_->finalize_calls();

  pjsip_->run([this]() {
    stun_turn_.reset(nullptr);
    sip_presence_.reset(nullptr);
  });

  this_ = nullptr;
  i_m_the_one_ = false;
  sip_plugin_used_ = 0;
}

void SIPPlugin::apply_log_level_configuration() {
  if (config<&InfoTree::branch_has_data>("log_level")) {
    int log_level = config<&InfoTree::branch_get_value>("log_level").copy_as<int>();
    if (log_level > -1) {
        log_level_ = log_level;
        sw_debug("sip has set log level from configuration");
    }
  }

  if (config<&InfoTree::branch_has_data>("console_log_level")) {
    int console_log_level = config<&InfoTree::branch_get_value>("console_log_level").copy_as<int>();
    if (console_log_level > -1) {
        console_log_level_ = console_log_level;
        sw_debug("sip has set console log level from configuration");
    }
  }

  if (config<&InfoTree::branch_has_data>("log_filename")) {
    auto log_filename = config<&InfoTree::branch_get_value>("log_filename");
    log_filename_.assign(log_filename.copy_as<std::string>());
    sw_debug("sip has set log filename from configuration");
  }
}

void SIPPlugin::apply_port_configuration() {
  // trying to set port if configuration found
  if (config<&InfoTree::branch_has_data>("port")) {
    sw_debug("SIP is trying to set port from configuration");
    auto port = config<&InfoTree::branch_get_value>("port");
    auto port_str = port.copy_as<std::string>();
    unsigned int val;
    if (!isdigit(port_str[0])) {
      sw_warning("requested local SIP port in config is not a string containing digits: {}",
                 port_str);
      return;
    }
    val = atoi(port_str.c_str());
    if (sip_port_ == val) return;
    if (val > 65535) {
      sw_warning("requested local SIP port is higher than 65535: {}", port_str);
      return;
    }
    sip_port_ = val;

    sw_debug("sip has set port from configuration");
  }
}

void SIPPlugin::apply_configuration() {
  // trying to set stun/turn from configuration
  std::string stun = config<&InfoTree::branch_get_value>("stun");
  std::string turn = config<&InfoTree::branch_get_value>("turn");
  std::string turn_user = config<&InfoTree::branch_get_value>("turn_user");
  std::string turn_pass = config<&InfoTree::branch_get_value>("turn_pass");
  if (!stun.empty()) {
    sw_debug("SIP is trying to set STUN/TURN from configuration");
    if (stun_turn_.get()->set_stun_turn(stun, turn, turn_user, turn_pass)) {
      sw_debug("sip has set STUN/TURN from configuration");
    } else {
      sw_warning("sip failed setting STUN/TURN from configuration");
    }
  }
  // trying to register if a user is given
  std::string user = config<&InfoTree::branch_get_value>("user");
  if (!user.empty()) {
    sw_debug("SIP is trying to register from configuration");
    std::string pass = config<&InfoTree::branch_get_value>("pass");
    pjsip_->run([&]() { sip_presence_->register_account(user, pass); });
    if (sip_presence_->registered_) {
      sw_debug("sip registered using configuration file");
    } else {
      sw_warning("sip failed registration from configuration");
    }
  }
}

bool SIPPlugin::start_sip_transport() {
  // Since pjsip 2.9 we do not call any more pjsua_transport_close when
  // transport_id_ != since it subsequent call to
  // pjsua_transport_create fails with the following error:
  // 09:38:52.161            pjsua_acc.c  ..Unable to generate suitable
  // Contact header for registration: Unsupported transport (PJSIP_EUNSUPTRANSPORT) [status=171060]
  // 09:38:52.161            pjsua_acc.c  ..Unable to create registration:
  // Unsupported transport (PJSIP_EUNSUPTRANSPORT) [status=171060]
  //
  // Note also pjsua_transport_close is not called between subsequent pjsua_transport_create
  // in pjsip-apps/src/pjsua/pjsua_app.c

  if (netutils::is_used(sip_port_)) {
    sw_warning("SIP port is not available ({})", std::to_string(sip_port_));
    return false;
  }
  pjsua_transport_config cfg;
  pjsua_transport_config_default(&cfg);
  cfg.port = sip_port_;
  pj_status_t status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_id_);
  if (status != PJ_SUCCESS) {
    sw_warning("Error creating transport");
    transport_id_ = -1;
    return false;
  }
  sw_debug("Successfully started SIP transport on port {}", std::to_string(cfg.port));
  return true;
}

std::string SIPPlugin::get_exposed_quiddity_name_from_shmpath(const std::string& shmpath) {
  {
    std::lock_guard<std::mutex> lock(exposed_quiddities_mutex_);
    for (auto const& [peer_uri, names] : exposed_quiddities_) {
      for (auto const& name : names) {
        if (shmpath == qcontainer_->get_quiddity(qcontainer_->get_id(name))
                           ->prop<&property::PBag::get_str_str>("shmdata-path")) {
          return name;
        }
      }
    }
    return "";
  }
}

void SIPPlugin::create_quiddity_stream(const std::string& peer_uri,
                                       const std::string& quid_nickname,
                                       const std::string& nickname) {
  {
    std::lock_guard<std::mutex> lock(exposed_quiddities_mutex_);
    auto& exposed_quids = exposed_quiddities_[peer_uri];
    if (std::find(exposed_quids.begin(), exposed_quids.end(), quid_nickname) != exposed_quids.end())
      return;
    exposed_quids.push_back(quid_nickname);
  }
  auto qrox = qcontainer_->create("extshmsrc", quid_nickname, nullptr);
  if (!qrox) {
    sw_warning("Failed to create external shmdata quiddity for pjsip incoming stream.");
    return;
  }
  if (!nickname.empty()) {
    qrox.get()->set_nickname(nickname);
  }
}

void SIPPlugin::expose_stream_to_quiddity(const std::string& quid_name,
                                          const std::string& shmpath) {
  qcontainer_->props<&property::PBag::set_str_str>(
      qcontainer_->get_id(quid_name), "shmdata-path", shmpath);
}

void SIPPlugin::remove_exposed_quiddity(const std::string& peer_uri,
                                        const std::string& quid_nickname) {
  {
    std::lock_guard<std::mutex> lock(exposed_quiddities_mutex_);
    auto& exposed_quids = exposed_quiddities_[peer_uri];
    auto it = std::find(exposed_quids.begin(), exposed_quids.end(), quid_nickname);
    if (it == exposed_quids.end()) return;
    exposed_quids.erase(it);
  }

  qcontainer_->remove(qcontainer_->get_id(quid_nickname));
}

void SIPPlugin::remove_exposed_quiddities(const std::string& peer_uri) {
  std::vector<std::string> quids_to_remove;
  {
    std::lock_guard<std::mutex> lock(exposed_quiddities_mutex_);
    quids_to_remove = exposed_quiddities_[peer_uri];
    exposed_quiddities_.erase(peer_uri);
  }
  for (auto& it : quids_to_remove) {
    auto id = qcontainer_->get_id(it);
    if (0 != id) qcontainer_->remove(id);
  }
}

InfoTree::ptr SIPPlugin::on_saving() {
  if (dns_address_ == default_dns_address_) dns_address_ = "default";
  return InfoTree::make();
}

void SIPPlugin::on_saved() {
  if (dns_address_ == "default") dns_address_ = default_dns_address_;
}

bool SIPPlugin::can_sink_caps(const std::string& str_caps) {
  return !stringutils::starts_with(str_caps, "video/x-raw");
}

}  // namespace quiddities
}  // namespace switcher
