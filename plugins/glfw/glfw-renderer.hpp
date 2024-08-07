/*
 * This file is part of switcher-glfwin.
 *
 * switcher-glfwin is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_GLFW_RENDERER_H__
#define __SWITCHER_GLFW_RENDERER_H__

#include <future>
#include "./glfwvideo.hpp"

namespace switcher {

class GLFWRenderer {
 public:
  GLFWRenderer();
  ~GLFWRenderer();
  GLFWRenderer(const GLFWRenderer&) = delete;
  GLFWRenderer& operator=(const GLFWRenderer&) = delete;

  void subscribe_to_render_loop(GLFWVideo* window);
  void unsubscribe_from_render_loop(GLFWVideo* window);
  void add_rendering_task(GLFWVideo* window, std::function<bool()> task);
  GLFWVideo* get_current_window() const { return current_window_; }

 private:
  using rendering_tasks_t = std::map<GLFWVideo*, std::vector<std::function<bool()>>>;

  void render_loop();
  rendering_tasks_t pop_rendering_tasks();

  rendering_tasks_t rendering_tasks_;
  std::mutex rendering_task_mutex_{};
  std::atomic<bool> running_{true};
  GLFWVideo* current_window_{nullptr};
  std::future<void> gl_loop_;
};

class RendererSingleton {
  friend class GLFWRenderer;
  friend class GLFWVideo;

 private:
  static GLFWRenderer* get();
  GLFWRenderer& operator=(const GLFWRenderer&) = delete;
  static std::mutex creation_window_mutex_;
  static std::unique_ptr<GLFWRenderer> s_instance_;
  static std::mutex creation_mutex_;
};

}  // namespace switcher

#endif
