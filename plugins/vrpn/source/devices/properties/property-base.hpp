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

#ifndef __SWITCHER_VRPN_SOURCE_DEVICE_PROPERTY_BASE_H__
#define __SWITCHER_VRPN_SOURCE_DEVICE_PROPERTY_BASE_H__

#include <string>
#include "switcher/quiddity/property/pbag.hpp"

namespace switcher {
using namespace quiddity;
namespace quiddities {
namespace vrpn {

class PropertyBase {
 public:
  /**
   * Deleted PropertyBase Default Constructor
   * @return
   */
  PropertyBase() = default;

  virtual ~PropertyBase() = default;
  /**
   * Switcher property id getter
   */
  property::prop_id_t getPropId() const;

  /**
   * Switcher property id setter
   */
  void setPropId(property::prop_id_t prop_id);

  /**
   * The templated property::Property class requires this one to have at least one virtual void
   * method. Unfortunately we don't need one so here it is...
   */
  virtual void noop() = 0;

 protected:
  /**
   * Switcher property::Property Id
   */
  property::prop_id_t prop_id_{};
};
}  // namespace vrpn
}  // namespace quiddities
}  // namespace switcher

#endif
