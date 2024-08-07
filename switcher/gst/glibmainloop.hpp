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

#ifndef __SWITCHER_GLIB_MAINLOOP_H__
#define __SWITCHER_GLIB_MAINLOOP_H__

#include <glib.h>
#include <memory>
#include <mutex>
#include <thread>
#include "../utils/threaded-wrapper.hpp"

namespace switcher {
namespace gst {

class GlibMainLoop {
 public:
  typedef std::shared_ptr<GlibMainLoop> ptr;
  GlibMainLoop();
  ~GlibMainLoop();
  GlibMainLoop(const GlibMainLoop&) = delete;
  GlibMainLoop& operator=(const GlibMainLoop&) = delete;

  GMainContext* get_main_context();
  bool invoke_in_main_loop(std::function<void()> fun);
  
 private:
  GMainContext* main_context_{nullptr};
  GMainLoop* mainloop_{nullptr};
  ThreadedWrapper<> main_loop_{};
  std::mutex funs_mutex_{};
  std::vector<std::function<void()>> funs_{};

  static int invoke_in_main_loop_cb(void* self);
};
}  // namespace gst
}  // namespace switcher

#endif
