/*
 * This file is part of switcher-plugin-example.
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

#ifndef __SWITCHER_SHMDATA_QUID_H__
#define __SWITCHER_SHMDATA_QUID_H__

#include "switcher/quiddity/quiddity.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;

/**
 * ShmdataQuid class.
 *
 * Example class that illustrates how to implement shmdata
 * connection in a Quiddity throught the use of the
 * claw::Config class.
 *
 */

class ShmdataQuid : public Quiddity {
 public:
  /**
   * ShmdataQuid constructor. It implements the expected
   * behaviour for being a Quiddity.
   *
   * \param conf Configuration given by Switcher
   */
  ShmdataQuid(quiddity::Config&& conf);

 private:
  /**
   * Connection specification for the ShmdataQuid
   * in string format, to be parsed by the Quidditity constructor.
   *
   */
  static const std::string kConnectionSpec;

  /**
   * Callback to be triggered by the Claw class when
   * the ShmdataQuid is invoked for a connection.
   *
   */
  bool on_connect(const std::string& shmpath, claw::sfid_t sfid);
  /**
   * Callback to be triggered by the Claw class when
   * the ShmdataQuid is invoked for a disconnection.
   *
   */
  bool on_disconnect(claw::sfid_t sfid);
};

SWITCHER_DECLARE_PLUGIN(ShmdataQuid);

}  // namespace quiddities
}  // namespace switcher
#endif
