/*
 * This file is part of switcher-timecode.
 *
 * libswitcher is free software; you can redistribute it and/or
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

#undef NDEBUG  // get assert in release mode

#include <string>
#include "switcher/quiddity/basic-test.hpp"

int main() {
  bool success = true;

  {
    using namespace switcher;
    using namespace quiddity;

    Switcher::ptr manager = Switcher::make_switcher("test_manager");



    // creating a jack server
    InfoTree::ptr server_config = InfoTree::make();
    server_config->vgraft("driver", "dummy");
    server_config->vgraft("realtime", false);
    auto jserv = manager->quids<&quiddity::Container::create>(
        "jackserver", "test_server", server_config.get());
    assert(jserv);
    assert(jserv.get()->prop<&property::PBag::set_str_str>("driver", "dummy"));
    assert(jserv.get()->prop<&property::PBag::set_str_str>("started", "true"));

    if (!quiddity::test::full(manager, "ltctojack")) success = false;
  }  // end of scope is releasing the manager

  if (success)
    return 0;
  return 1;
}
