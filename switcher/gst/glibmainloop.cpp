/*
 * This file is part of libswitcher.
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

#include "./glibmainloop.hpp"
#include <chrono>

namespace switcher {
namespace gst {

GlibMainLoop::GlibMainLoop()
    : main_context_(g_main_context_new()), mainloop_(g_main_loop_new(main_context_, FALSE)) {
  main_loop_.run_async([this]() { g_main_loop_run(mainloop_); });
}

GMainContext* GlibMainLoop::get_main_context() { return main_context_; }

GlibMainLoop::~GlibMainLoop() {
  while (!g_main_loop_is_running(mainloop_)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  g_main_loop_quit(mainloop_);
  main_loop_.run_async([this]() {
    g_main_loop_unref(mainloop_);
    g_main_context_unref(main_context_);
  });
}

}  // namespace gst
}  // namespace switcher
