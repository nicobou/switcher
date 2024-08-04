/*
 * This file is part of switcher-v4l2.
 *
 * switcher-v4l2 is free software; you can redistribute it and/or
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

#include <cassert>
#include <vector>
#include "switcher/quiddity/basic-test.hpp"

int main() {
  {
    using namespace switcher;
    Switcher::ptr manager = Switcher::make_switcher("test_manager");


    assert(quiddity::test::full(manager, "v4l2src"));
    if (!manager->quids<&quiddity::Container::create>("v4l2src", "test", nullptr))
      return 0;  // no camera in this computer
    assert(manager->quids<&quiddity::Container::remove>(
        manager->quids<&quiddity::Container::get_id>("test")));
  }  // end of scope is releasing the manager
  return 0;
}
