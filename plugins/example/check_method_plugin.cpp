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
#include "switcher/method-trait.hpp"
#include "switcher/property-container.hpp"
#include "switcher/quiddity-basic-test.hpp"
#include "switcher/serialize-string.hpp"

int main() {
  {
    using namespace switcher;
    Switcher::ptr manager = Switcher::make_switcher("test_manager");
    manager->scan_directory_for_plugins("./");
    assert(QuiddityBasicTest::test_full(manager, "method"));
    assert(manager->create("method", "test") == "test");

    // testing "hello" method. Signature is string(string)
    using hello_meth_t = std::function<std::string(std::string)>;
    auto hello_id = manager->use_method<MPtr(&MContainer::get_id)>("test", "hello_");
    assert(hello_id != 0);
    auto res = manager->use_method<MPtr(&MContainer::invoke<hello_meth_t>)>(
        "test", hello_id, std::make_tuple(std::string("Nicolas")));
    assert("hello Nicolas and count is 0" == res);

    // using "count" method. Signature is void()
    using count_meth_t = std::function<void()>;
    auto count_id = manager->use_method<MPtr(&MContainer::get_id)>("test", "count_");
    assert(count_id != 0);
    manager->use_method<MPtr(&MContainer::invoke<count_meth_t>)>(
        "test", count_id, std::make_tuple());

    // testing count did its internal counting
    assert("hello Nicolas and count is 1" ==
           manager->use_method<MPtr(&MContainer::invoke<hello_meth_t>)>(
               "test", hello_id, std::make_tuple(std::string("Nicolas"))));

    // testing "many args" method: Signature bool(int, float, const std::string&, bool).
    // many return true only if arguments are <1,3.14,"is, but not ",false>
    using many_args_t = std::function<bool(int, float, const std::string&, bool)>;
    auto many_id = manager->use_method<MPtr(&MContainer::get_id)>("test", "many_args_");
    assert(many_id != 0);
    assert(true ==
           manager->use_method<MPtr(&MContainer::invoke<many_args_t>)>(
               "test", many_id, std::make_tuple(1, 3.14f, std::string("is, but not "), false)));

    // testing "many args" invokation from string, using tuple deserialization.
    // Note that tuple are serialized
    auto tuple_from_str = deserialize::apply<method_trait<many_args_t>::args_t>(
        std::string("1,3.14,") + serialize::esc_for_tuple("is, but not ") + std::string(",false"));
    assert(tuple_from_str.first);  // first is a boolean indicating the success of deserialization
    assert(true ==
           manager->use_method<MPtr(&MContainer::invoke<many_args_t>)>(
               "test", many_id, tuple_from_str.second));

    assert(manager->remove("test"));
  }  // end of scope is releasing the manager
  return 0;
}
