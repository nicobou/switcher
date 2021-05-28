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

#include "switcher/infotree/information-tree.hpp"
#include "switcher/infotree/json-serializer.hpp"
#include "switcher/quiddity/basic-test.hpp"

int main() {
  {
    using namespace switcher;
    using namespace quiddity;
    using namespace claw;

    Switcher::ptr swit = Switcher::make_switcher("test_manager");
    assert(quiddity::test::full(swit, "connection-quid"));

    auto reader = swit->quids<MPtr(&Container::create)>("connection-quid", "reader", nullptr).get();
    assert(reader);

    auto writer = swit->quids<MPtr(&Container::create)>("connection-quid", "writer", nullptr).get();
    assert(writer);

    // get the writer connecction information from the Quiddity conspec tree
    std::map<std::string, swid_t> swids;
    {
      auto conspec_tree = writer->conspec<MPtr(&InfoTree::get_copy)>();
      assert(conspec_tree);
      assert(!conspec_tree->empty());
      conspec_tree->cfor_each_in_array("writer", [&](const InfoTree* tree) {
        swids.emplace(tree->branch_get_value("name"), tree->branch_get_value("swid"));
      });
      assert(!swids.empty());
    }

    // get the reader connecction information from the Quiddity conspec tree
    std::map<std::string, sfid_t> sfids;
    {
      auto conspec_tree = reader->conspec<MPtr(&InfoTree::get_copy)>();
      assert(conspec_tree);
      assert(!conspec_tree->empty());
      conspec_tree->cfor_each_in_array("follower", [&](const InfoTree* tree) {
        sfids.emplace(tree->branch_get_value("name"), tree->branch_get_value("sfid"));
      });
      assert(!sfids.empty());
    }

    // check connection of the writer "texture" with the reader "texture"
    {
      // get information from the claw + check of consitency among class and Quiddity Tree
      auto wtexture_id = writer->claw<MPtr(&Claw::get_swid)>("texture");
      assert(Ids::kInvalid != wtexture_id);
      assert(wtexture_id == swids["texture"]);
      auto rtexture_id = reader->claw<MPtr(&Claw::get_sfid)>("texture");
      assert(Ids::kInvalid != rtexture_id);
      assert(rtexture_id == sfids["texture"]);

      // connection of both texture shmdata
      auto res_id = reader->claw<MPtr(&Claw::connect)>(rtexture_id, writer->get_id(), wtexture_id);
      // since follower texture is not meta, we expect to obtain the same id
      assert(res_id == rtexture_id);
      // and then disconnect
      assert(reader->claw<MPtr(&Claw::disconnect)>(res_id));
    }
  }  // end of scope is releasing the switcher
  return 0;
}