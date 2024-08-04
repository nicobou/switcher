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

#include <shmdata/console-logger.hpp>
#include "switcher/quiddity/basic-test.hpp"
#include "switcher/shmdata/follower.hpp"
#include "switcher/utils/serialize-string.hpp"

bool success = false;
std::atomic<bool> do_continue{true};
std::condition_variable cond_var{};
std::mutex mut{};

using namespace switcher;
using namespace quiddity;
using namespace claw;

void wait_until_success() {
  // wait 3 seconds
  unsigned int count = 3;
  while (do_continue.load()) {
    std::unique_lock<std::mutex> lock(mut);
    if (count == 0) {
      do_continue.store(false);
    } else {
      --count;
      cond_var.wait_for(lock, std::chrono::seconds(1), []() { return !do_continue.load(); });
    }
  }
}

void notify_success() {
  std::unique_lock<std::mutex> lock(mut);
  success = true;
  do_continue.store(false);
  cond_var.notify_one();
}

int main() {
  {
    using namespace switcher;
    Switcher::ptr manager = Switcher::make_switcher("ltcdifftest");



    // creating a jack server
    InfoTree::ptr server_config = InfoTree::make();
    server_config->vgraft("driver", "dummy");
    server_config->vgraft("realtime", false);
    auto jserv = manager->quids<&quiddity::Container::create>(
        "jackserver", "test_server", server_config.get());
    assert(jserv);
    assert(jserv.get()->prop<&property::PBag::set_str_str>("driver", "dummy"));
    assert(jserv.get()->prop<&property::PBag::set_str_str>("started", "true"));

    // Fringe case like CI cannot run this test successfully but we don't want it to fail.
    if (!manager->quids<&quiddity::Container::create>(
            "ltcsource", "ltctestsourcedummy", nullptr))
      return 0;

    if (!quiddity::test::full(manager, "ltcdiff")) return 1;

    auto ltctestsource1 =
        manager->quids<&quiddity::Container::create>("ltcsource", "ltctestsource1", nullptr)
            .get();
    if (!ltctestsource1) return 1;
    auto ltctestsource2 =
        manager->quids<&quiddity::Container::create>("ltcsource", "ltctestsource2", nullptr)
            .get();
    if (!ltctestsource2) return 1;
    auto ltcdiff =
        manager->quids<&quiddity::Container::create>("ltcdiff", "ltcdifftest", nullptr).get();
    if (!ltcdiff) return 1;

    // We set 30 frames of delay for this source.
    if (!ltctestsource1->prop<&property::PBag::set_str_str>("timeshift_forward", "30"))
      return 1;

    if (!ltctestsource1->prop<&property::PBag::set_str_str>("started", "true")) return 1;

    if (!ltctestsource2->prop<&property::PBag::set_str_str>("started", "true")) return 1;

    if (Ids::kInvalid ==
        ltcdiff->claw<&Claw::connect>(ltcdiff->claw<&Claw::get_sfid>("ltc1"),
                                            ltctestsource1->get_id(),
                                            ltctestsource1->claw<&Claw::get_swid>("ltc"))) {
      return 1;
    }
    if (Ids::kInvalid ==
        ltcdiff->claw<&Claw::connect>(ltcdiff->claw<&Claw::get_sfid>("ltc2"),
                                            ltctestsource2->get_id(),
                                            ltctestsource2->claw<&Claw::get_swid>("ltc"))) {
      return 1;
    }

    ::shmdata::ConsoleLogger logger;
    auto reader = std::make_unique<::shmdata::Reader>(
        ltcdiff->claw<&Claw::get_shmpath_from_writer_label>("ltc-diff"),
        [](void* data, size_t data_size) {
          if (data_size) {
            auto diff = *static_cast<double*>(data);
            // 1000ms in 30 frames, 50ms is between
            // one and two frames at 30 fps, we accept
            // 1 frame of error.
            if (diff - 1000 < 50) notify_success();
#ifdef SWITCHER_TEST_COVERAGE
            // we tolerate more difference when
            // testing coverage
            if (diff - 1000 < 500) notify_success();
#endif
          }
        },
        nullptr,
        nullptr,
        &logger);

    wait_until_success();

  }  // end of scope is releasing the manager
  return success ? 0 : 1;
}
