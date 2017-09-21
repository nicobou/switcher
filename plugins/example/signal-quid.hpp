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

#ifndef __SWITCHER_SIGNAL_QUID_H__
#define __SWITCHER_SIGNAL_QUID_H__

#include "switcher/quiddity.hpp"

namespace switcher {
class SignalQuid : public Quiddity {
 public:
  SignalQuid(QuiddityConfiguration&&);
  ~SignalQuid() = default;
  SignalQuid(const SignalQuid&) = delete;
  SignalQuid& operator=(const SignalQuid&) = delete;

  bool init() final;
  static gboolean my_signal_method(void*, void* user_data);

 private:
  SContainer::sig_id_t signal_id_;
};

SWITCHER_DECLARE_PLUGIN(SignalQuid);
}
#endif  // __SWITCHER_SIGNAL_QUID_H__
