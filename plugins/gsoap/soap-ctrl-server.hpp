/*
 * This file is part of switcher-gsoap.
 *
 * switcher-gsoap is free software: you can redistribute it and/or modify
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

#ifndef __SWITCHER_SOAP_CTRL_SERVER_H__
#define __SWITCHER_SOAP_CTRL_SERVER_H__

#include <memory>
#include <mutex>
#include <thread>
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/switcher.hpp"
#include "webservices/soapcontrolService.h"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class SoapCtrlServer : public Quiddity {
 public:
  SoapCtrlServer(quiddity::Config&&);
  ~SoapCtrlServer();
  SoapCtrlServer(const SoapCtrlServer&) = delete;
  SoapCtrlServer& operator=(const SoapCtrlServer&) = delete;

  bool set_port(int port);
  bool start();
  Switcher* get_switcher();

 private:
  void server_thread();
  struct soap soap_ {};
  int port_{0};
  bool quit_server_thread_{false};
  controlService* service_{nullptr};
  SOAP_SOCKET socket_{-1};
  std::thread thread_{};
  std::mutex mutex_{};
  method::meth_id_t set_port_id_;
};

SWITCHER_DECLARE_PLUGIN(SoapCtrlServer);

}  // namespace quiddities
}  // namespace switcher
#endif
