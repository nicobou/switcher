/*
 * This file is part of switcher-plugin-example.
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

#include <cassert>
#include <vector>

#include "switcher/quiddity/basic-test.hpp"

int main() {
  {
    using namespace switcher;
    using namespace quiddity;
    using namespace claw;
    using namespace property;

    Switcher::ptr sw = Switcher::make_switcher("test_manager");
    assert(quiddity::test::full(sw, "dyn-reader-quid"));

    auto reader = sw->quids<&Container::create>("dyn-reader-quid", "reader", nullptr).get();
    assert(reader);
    // register to new connection spec in the tree
    int num_added_received = 0;
    assert(0 !=
           reader->sig<&signal::SBag::subscribe_by_name>(
               "on-connection-spec-added", [&](const InfoTree::ptr& tree) {
                 assert(stringutils::starts_with(tree->read_data().as<std::string>(), "follower."));
                 ++num_added_received;
               }));
    int num_removed_received = 0;
    assert(0 !=
           reader->sig<&signal::SBag::subscribe_by_name>(
               "on-connection-spec-removed", [&](const InfoTree::ptr& tree) {
                 assert(stringutils::starts_with(tree->read_data().as<std::string>(), "follower."));
                 ++num_removed_received;
               }));

    auto writer = sw->quids<&Container::create>("connection-quid", "writer", nullptr).get();
    assert(writer);

    // check connection of the writer "texture" with the reader "video%"
    {
      // get information from the claw + check of consitency among class and Quiddity Tree
      auto wtexture_id = writer->claw<&Claw::get_swid>("texture");
      assert(Ids::kInvalid != wtexture_id);
      auto rtexture_id = reader->claw<&Claw::get_sfid>("video%");
      assert(Ids::kInvalid != rtexture_id);

      // check only one follower is specified
      assert(1 == reader->claw<&Claw::get_follower_labels>().size());
      // connection of both texture shmdata
      auto res_id = reader->claw<&Claw::connect>(rtexture_id, writer->get_id(), wtexture_id);
      assert(0 != res_id);
      // since follower texture IS meta, we expect to obtain a different id
      assert(res_id != rtexture_id);
      // check a follower has been created
      assert(2 == reader->claw<&Claw::get_follower_labels>().size());
      // check we had a notification for addition into the connection spec
      assert(1 == num_added_received);
      // and then disconnect
      assert(reader->claw<&Claw::disconnect>(res_id));
      // check we received a notification for spec removal
      assert(1 == num_removed_received);
      // check the created follower has been removed
      assert(1 == reader->claw<&Claw::get_follower_labels>().size());
    }

  }  // end of scope is releasing the switcher
  return 0;
}
