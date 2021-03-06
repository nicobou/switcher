/**
 * This file is part of switcher-vrpn.
 *
 * switcher-vrpn is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_VRPN_CLIENT_CONNECTION_H__
#define __SWITCHER_VRPN_CLIENT_CONNECTION_H__

#include <string>
#include "./vrpn-connection.hpp"

namespace switcher {
namespace quiddities {
namespace vrpn {

/**
 * Wrapper/Helper to be able to instanciate a client vrpn connection normally.
 */
class VRPNClientConnection : public VRPNConnection {
 public:
  VRPNClientConnection(const std::string& hostname)
      : VRPNConnection(vrpn_get_connection_by_name(hostname.c_str())){};
};
}  // namespace vrpn
}  // namespace quiddities
}  // namespace switcher

#endif
