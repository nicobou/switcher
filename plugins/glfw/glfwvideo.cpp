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

#include "./glfwvideo.hpp"
#include <sys/stat.h>
#include "./glfw-renderer.hpp"
#include "switcher/quiddity/property/gprop-to-prop.hpp"
#include "switcher/utils/file-utils.hpp"
#include "switcher/utils/scope-exit.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "./external/stb_image.h"

namespace switcher {
SWITCHER_DECLARE_PLUGIN(GLFWVideo);
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(GLFWVideo,
                                     "glfwin",
                                     "OpenGL Video Display (configurable)",
                                     "Video window with fullscreen",
                                     "LGPL",
                                     "Jérémie Soria");

const std::string GLFWVideo::kBackgroundColorDisabled =
    "Cannot modify color when background image mode is selected.";
const std::string GLFWVideo::kBackgroundImageDisabled =
    "Cannot modify background image when color mode is selected.";
const std::string GLFWVideo::kFullscreenDisabled =
    "This property is disabled while in fullscreen mode.";
const std::string GLFWVideo::kBackgroundTypeImage = "Image";
const std::string GLFWVideo::kBackgroundTypeColor = "Color";
const std::string GLFWVideo::kOverlayDisabledMessage =
    "This property cannot be modified because the text overlay is disabled.";

std::atomic<int> GLFWVideo::instance_counter_(0);

const std::string GLFWVideo::kConnectionSpec(R"(
{
"follower":
  [
    {
      "label": "video",
      "description": "Video stream to display",
      "can_do": ["video/x-raw"]
    }
  ],
"writer":
  [
    {
      "label": "keyb",
      "description": "Keyboard events",
      "can_do": ["application/x-keyboard-events"]
    },
    {
      "label": "mouse",
      "description": "Mouse events",
      "can_do": ["application/x-mouse-events"]
    }
  ]
}
)");

GLFWVideo::GLFWVideo(quiddity::Config&& conf)
    : Quiddity(
          std::forward<quiddity::Config>(conf),
          {kConnectionSpec,
           [this](const std::string& shmpath, claw::sfid_t) { return on_shmdata_connect(shmpath); },
           [this](claw::sfid_t) { return on_shmdata_disconnect(); }}),
      gst_pipeline_(std::make_unique<gst::Pipeliner>(nullptr, nullptr)),
      background_config_id_(pmanage<&property::PBag::make_group>(
          "background_config",
          "Background configuration",
          "Select if you want a color or an image background when no video is playing.")),
      background_type_id_(pmanage<&property::PBag::make_parented_selection<>>(
          "background_type",
          "background_config",
          [this](const quiddity::property::IndexOrName& val) {
            background_type_.select(val);
            if (background_type_.get_current() == kBackgroundTypeImage) {
              pmanage<&property::PBag::disable>(color_id_, kBackgroundColorDisabled);
              pmanage<&property::PBag::enable>(background_image_id_);
              draw_image_ = true;
            } else {
              pmanage<&property::PBag::disable>(background_image_id_,
                                                      kBackgroundImageDisabled);
              pmanage<&property::PBag::enable>(color_id_);
              draw_image_ = false;
            }
            return true;
          },
          [this]() { return background_type_.get(); },
          "Background type",
          "Use a color or image background. Default: color.",
          background_type_)),
      color_(0, 0, 0, 0xFF),
      color_id_(pmanage<&property::PBag::make_parented_color>(
          "color",
          "background_config",
          [this](const property::Color& val) {
            color_ = val;
            add_rendering_task([this]() {
              set_color();
              return true;
            });
            return true;
          },
          [this]() { return color_; },
          "Background color",
          "Color of the background when no video is displayed.",
          color_)),
      background_image_id_(pmanage<&property::PBag::make_parented_string>(
          "background_image",
          "background_config",
          [this](const std::string& val) {
            background_image_ = val;
            if (background_image_.empty()) return true;
            add_rendering_task([this]() {
              int w = 0, h = 0, n = 0;
              auto data = stbi_load(background_image_.c_str(), &w, &h, &n, 0);
              On_scope_exit { stbi_image_free(data); };

              if (!data) {
                sw_warning("Failed to load image at path {}.", background_image_);
                return true;
              }

              if (n < 3) {
                sw_warning("Source background image is neither RGB nor RGBA (glfw).");
                return true;
              }

              size_t data_size = w * h * n;
              image_width_ = w;
              image_height_ = h;
              image_components_ = n;
              image_data_.resize(data_size);
              std::copy(data, data + data_size, image_data_.data());
              if (!draw_video_) setup_background_texture();
              return true;
            });
            return true;
          },
          [this]() { return background_image_; },
          "Background image file",
          "Path to the image to use as background when no video is played.",
          std::string())),
      overlay_id_(pmanage<&property::PBag::make_group>(
          "overlay_config", "Overlay configuration", "Toggle and configure the text overlay.")),
      show_overlay_id_(pmanage<&property::PBag::make_parented_bool>(
          "show_overlay",
          "overlay_config",
          [this](bool val) {
            show_overlay_ = val;
            if (show_overlay_) {
              pmanage<&property::PBag::enable>(gui_configuration_->text_id_);
              pmanage<&property::PBag::enable>(gui_configuration_->alignment_id_);
              pmanage<&property::PBag::enable>(gui_configuration_->font_id_);
              pmanage<&property::PBag::enable>(gui_configuration_->use_custom_font_id_);
              if (gui_configuration_->use_custom_font_)
                pmanage<&property::PBag::enable>(gui_configuration_->custom_font_id_);
              else
                pmanage<&property::PBag::enable>(gui_configuration_->font_size_id_);
              pmanage<&property::PBag::enable>(gui_configuration_->color_id_);
            } else {
              pmanage<&property::PBag::disable>(gui_configuration_->text_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->alignment_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->font_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->use_custom_font_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->custom_font_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->font_size_id_,
                                                      kOverlayDisabledMessage);
              pmanage<&property::PBag::disable>(gui_configuration_->color_id_,
                                                      kOverlayDisabledMessage);
            }
            return true;
          },
          [this]() { return show_overlay_; },
          "Show overlay",
          "Show the overlay label on top of the video",
          show_overlay_)),
      geometry_task_(std::make_unique<PeriodicTask<>>(
          [this]() {
            std::lock_guard<std::mutex> lock(configuration_mutex_);
            if (!window_moved_ || !gui_configuration_) return;
            add_rendering_task([this]() {
              pmanage<&property::PBag::notify>(position_x_id_);
              pmanage<&property::PBag::notify>(position_y_id_);
              pmanage<&property::PBag::notify>(width_id_);
              pmanage<&property::PBag::notify>(height_id_);
              ImGui::SetCurrentContext(gui_configuration_->context_->ctx);
              ImGuiIO& io = ImGui::GetIO();
              io.DisplaySize.x = static_cast<float>(width_);
              io.DisplaySize.y = static_cast<float>(height_);

              set_viewport();
              window_moved_ = false;
              return true;
            });
          },
          std::chrono::milliseconds(500))),
      title_(get_nickname()),
      title_id_(pmanage<&property::PBag::make_string>(
          "title",
          [this](const std::string& val) {
            title_ = val;
            glfwSetWindowTitle(window_, title_.c_str());
            return true;
          },
          [this]() { return title_; },
          "Window Title",
          "Window Title",
          title_)),
      xevents_to_shmdata_id_(pmanage<&property::PBag::make_bool>(
          "xevents",
          [this](bool val) {
            xevents_to_shmdata_ = val;
            if (xevents_to_shmdata_) {
              keyb_shm_ =
                  std::make_unique<shmdata::Writer>(this,
                                                    claw_.get_shmpath_from_writer_label("keyb"),
                                                    sizeof(KeybEvent),
                                                    "application/x-keyboard-events");
              if (!keyb_shm_.get()) {
                sw_warning("GLFW keyboard event shmdata writer failed");
                keyb_shm_.reset(nullptr);
              }

              mouse_shm_ =
                  std::make_unique<shmdata::Writer>(this,
                                                    claw_.get_shmpath_from_writer_label("mouse"),
                                                    sizeof(MouseEvent),
                                                    "application/x-mouse-events");
              if (!mouse_shm_.get()) {
                sw_warning("GLFW mouse event shmdata writer failed");
                mouse_shm_.reset(nullptr);
              }
            } else {
              mouse_shm_.reset(nullptr);
              keyb_shm_.reset(nullptr);
            }
            return true;
          },
          [this]() { return xevents_to_shmdata_; },
          "Keyboard/Mouse Events",
          "Capture Keyboard/Mouse Events",
          xevents_to_shmdata_)),
      fullscreen_id_(pmanage<&property::PBag::make_bool>(
          "fullscreen",
          [this](bool val) {
            if (val == fullscreen_) return true;
            fullscreen_ = val;
            add_rendering_task([this, val]() {
              std::lock_guard<std::mutex> lock(configuration_mutex_);
              if (val) {
                minimized_width_ = width_;
                minimized_height_ = height_;
                minimized_position_x_ = position_x_;
                minimized_position_y_ = position_y_;
                pmanage<&property::PBag::disable>(width_id_, kFullscreenDisabled);
                pmanage<&property::PBag::disable>(height_id_, kFullscreenDisabled);
                pmanage<&property::PBag::disable>(position_x_id_, kFullscreenDisabled);
                pmanage<&property::PBag::disable>(position_y_id_, kFullscreenDisabled);
                pmanage<&property::PBag::disable>(decorated_id_, kFullscreenDisabled);

                MonitorConfig monitor = get_monitor_config();
                // Positioning is handled by glfwSetWindowMonitor; variables are set just so that
                // they are in sync with the actual screen position.
                // Width/height variables are automatically updated by glfwSetWindowMonitor.
                position_x_ = monitor.position_x;
                position_y_ = monitor.position_y;
                glfwSetWindowMonitor(window_,
                                     monitor.monitor,
                                     GLFW_DONT_CARE,
                                     GLFW_DONT_CARE,
                                     monitor.width,
                                     monitor.height,
                                     GLFW_DONT_CARE);
              } else {
                pmanage<&property::PBag::enable>(width_id_);
                pmanage<&property::PBag::enable>(height_id_);
                pmanage<&property::PBag::enable>(position_x_id_);
                pmanage<&property::PBag::enable>(position_y_id_);
                pmanage<&property::PBag::enable>(decorated_id_);
                position_x_ = minimized_position_x_;
                position_y_ = minimized_position_y_;
                glfwSetWindowMonitor(window_,
                                     nullptr,
                                     minimized_position_x_,
                                     minimized_position_y_,
                                     minimized_width_,
                                     minimized_height_,
                                     GLFW_DONT_CARE);
              }
              return true;
            });
            return true;
          },
          [this]() { return fullscreen_; },
          "Window Fullscreen",
          "Toggle fullscreen on the window in its current monitor",
          false)),
      decorated_id_(pmanage<&property::PBag::make_bool>(
          "decorated",
          [this](bool val) {
            if (val == decorated_) return true;
            decorated_ = val;
            add_rendering_task([this]() {
              swap_window();
              return false;  // Always start over the rendering tasks unstacking.
            });
            return true;
          },
          [this]() { return decorated_; },
          "Window Decoration",
          "Show/Hide Window Decoration",
          true)),
      always_on_top_id_(pmanage<&property::PBag::make_bool>(
          "always_on_top",
          [this](bool val) {
            if (val == always_on_top_) return true;
            always_on_top_ = val;
            add_rendering_task([this]() {
              swap_window();
              return false;  // Always start over the rendering tasks unstacking.
            });
            return true;
          },
          [this]() { return always_on_top_; },
          "Always On Top",
          "Toggle Window Always On Top",
          true)),
      rotation_id_(pmanage<&property::PBag::make_selection<>>(
          "rotation",
          [this](const quiddity::property::IndexOrName& val) {
            rotation_.select(val);
            add_rendering_task([this]() {
              set_viewport();
              set_rotation_shader();
              return true;
            });
            return true;
          },
          [this]() { return rotation_.get(); },
          "Rotation modes",
          "Possible rotation modes of the video.",
          rotation_)),
      flip_id_(pmanage<&property::PBag::make_selection<>>(
          "flip",
          [this](const quiddity::property::IndexOrName& val) {
            flip_.select(val);
            add_rendering_task([this]() {
              set_flip_shader();
              return true;
            });
            return true;
          },
          [this]() { return flip_.get(); },
          "Flip modes",
          "Possible flip modes of the video.",
          flip_)),
      vsync_id_(pmanage<&property::PBag::make_selection<int>>(
          "vsync",
          [this](const quiddity::property::IndexOrName& val) {
            vsync_.select(val);
            add_rendering_task([this]() {
              glfwSwapInterval(vsync_.get_attached());
              return true;
            });
            return true;
          },
          [this]() { return vsync_.get(); },
          "Vertical synchronization type",
          "Select the vertical synchronization mode (Hard/Soft/None).",
          vsync_)) {
  if (getenv("DISPLAY") == nullptr) {
    if (-1 == setenv("DISPLAY", ":0", false)) {
      sw_warning("BUG: Failed to set a display!");
      is_valid_ = false;
      return;
    }
  }

  if (!instance_counter_ && !glfwInit()) {
    sw_warning("BUG: Failed to initialize glfw library!");
    is_valid_ = false;
    return;
  }

  discover_monitor_properties();

  if (!monitors_config_.size()) {
    sw_warning("BUG: Failed to discover monitors.");
    is_valid_ = false;
    return;
  }
  // win_aspect_ratio_toggle_id_ =
  //     pmanage<&property::PBag::make_bool>("win_aspect_ratio_toggle",
  //                                           [this](const bool val) {
  //                                             win_aspect_ratio_toggle_ = val;
  //                                             minimized_width_ = width_;
  //                                             minimized_height_ = height_;
  //                                             if (val)
  //                                               add_rendering_task(
  //                                                   [ this, width = width_, height = height_ ]()
  //                                                   {
  //                                                     glfwSetWindowAspectRatio(
  //                                                         window_, width, height);
  //                                                     return true;
  //                                                   });
  //                                             else
  //                                               add_rendering_task([this]() {
  //                                                 glfwSetWindowAspectRatio(
  //                                                     window_, GLFW_DONT_CARE, GLFW_DONT_CARE);
  //                                                 return true;
  //                                               });

  //                                             return true;
  //                                           },
  //                                           [this]() { return win_aspect_ratio_toggle_; },
  //                                           "Locked window aspect ratio",
  //                                           "Enable/Disable",
  //                                           win_aspect_ratio_toggle_);

  width_id_ = pmanage<&property::PBag::make_int>(
      "width",
      [this](const int& val) {
        if (val == width_) return true;
        add_rendering_task([this, val]() {
          width_ = val;
          set_size();
          std::lock_guard<std::mutex> lock(configuration_mutex_);
          ImGui::SetCurrentContext(gui_configuration_->context_->ctx);
          ImGuiIO& io = ImGui::GetIO();
          io.DisplaySize.x = static_cast<float>(width_);
          return true;
        });
        return true;
      },
      [this]() { return width_; },
      "Window Width",
      "Set Window Width",
      800,
      1,
      max_width_);
  height_id_ = pmanage<&property::PBag::make_int>(
      "height",
      [this](const int& val) {
        if (val == height_) return true;
        add_rendering_task([this, val]() {
          height_ = val;
          set_size();
          std::lock_guard<std::mutex> lock(configuration_mutex_);
          ImGui::SetCurrentContext(gui_configuration_->context_->ctx);
          ImGuiIO& io = ImGui::GetIO();
          io.DisplaySize.y = static_cast<float>(height_);
          return true;
        });
        return true;
      },
      [this]() { return height_; },
      "Window Height",
      "Set Window Height",
      600,
      1,
      max_height_);
  position_x_id_ = pmanage<&property::PBag::make_int>("position_x",
                                                            [this](const int& val) {
                                                              if (val == position_x_) return true;
                                                              add_rendering_task([this, val]() {
                                                                position_x_ = val;
                                                                set_position();
                                                                return true;
                                                              });
                                                              return true;
                                                            },
                                                            [this]() { return position_x_; },
                                                            "Window Position X",
                                                            "Set Window Horizontal Position",
                                                            0,
                                                            0,
                                                            max_width_);
  position_y_id_ = pmanage<&property::PBag::make_int>("position_y",
                                                            [this](const int& val) {
                                                              if (val == position_y_) return true;
                                                              add_rendering_task([this, val]() {
                                                                position_y_ = val;
                                                                set_position();
                                                                return true;
                                                              });
                                                              return true;
                                                            },
                                                            [this]() { return position_y_; },
                                                            "Window Position Y",
                                                            "Set Window Vertical Position",
                                                            0,
                                                            0,
                                                            max_height_);

  pmanage<&property::PBag::make_bool>(
      "keyb_interaction",
      [this](const bool val) {
        keyb_interaction_ = val;
        return true;
      },
      [this]() { return keyb_interaction_; },
      "Keyboard Shortcuts",
      "Enable/Disable keybord shortcuts",
      keyb_interaction_);

  if (!remake_elements()) {
    is_valid_ = false;
    return;
  }

  std::lock_guard<std::mutex> lock(RendererSingleton::creation_window_mutex_);

  glfwWindowHint(GLFW_VISIBLE, false);
  glfwWindowHint(GLFW_RESIZABLE, true);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#if OSX
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  window_ = create_window();
  if (!window_) {
    is_valid_ = false;
    return;
  }

  glfwMakeContextCurrent(window_);
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    sw_warning("BUG: glad could not load openGL functionalities.");
    is_valid_ = false;
    return;
  }

  sw_debug("OpenGL Version {}.{} loaded",
           std::to_string(GLVersion.major),
           std::to_string(GLVersion.minor));

  if (!setup_shaders()) {
    is_valid_ = false;
    return;
  }
  glGenTextures(1, &drawing_texture_);
  setup_vertex_array();

  pmanage<&property::PBag::set_to_current>(color_id_);
  pmanage<&property::PBag::set_to_current>(background_type_id_);

  {
    std::lock_guard<std::mutex> lock(configuration_mutex_);
    gui_configuration_ = std::make_unique<GUIConfiguration>(this);
    if (gui_configuration_->initialized_)
      gui_configuration_->init_properties();
    else
      sw_warning(
          "Overlay is not useable because something wrong happened during gui configuration");
  }

  glfwMakeContextCurrent(nullptr);

  ++instance_counter_;
  RendererSingleton::get()->subscribe_to_render_loop(this);

  // We need the quiddity config to be loaded so we cannot do it in the constructor.
  if (is_valid_) {
    load_icon();
    setup_icon();
  }
}

GLFWVideo::~GLFWVideo() {
  // Blocking call until the window is unsubscribed from the render loop.
  ongoing_destruction_ = true;
  if (is_valid_) RendererSingleton::get()->unsubscribe_from_render_loop(this);

  {
    std::lock_guard<std::mutex> lock(RendererSingleton::creation_window_mutex_);
    destroy_gl_elements();
    if (window_) glfwDestroyWindow(window_);
  }
  // Needs to be called because glfwTerminate releases OpenGL function pointers.
  gui_configuration_.reset();

  --instance_counter_;
  if (!instance_counter_) {
    glfwTerminate();
  }
}

void GLFWVideo::destroy_gl_elements() {
  if (!window_) return;

  glfwMakeContextCurrent(window_);
  if (drawing_texture_) glDeleteTextures(1, &drawing_texture_);
  if (shader_program_) glDeleteProgram(shader_program_);
  if (fragment_shader_) glDeleteShader(fragment_shader_);
  if (vertex_shader_) glDeleteShader(vertex_shader_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  glfwMakeContextCurrent(nullptr);
}

GLFWwindow* GLFWVideo::create_window(GLFWwindow* old) {
  glfwWindowHint(GLFW_DECORATED, decorated_);
  glfwWindowHint(GLFW_FLOATING, always_on_top_);
  glfwWindowHint(GLFW_AUTO_ICONIFY, GL_FALSE);

  auto window = glfwCreateWindow(width_, height_, title_.c_str(), nullptr, old);
  if (!window) {
    sw_warning("Could not create glfw window, probably an OpenGL version mismatch.");
    return nullptr;
  }

  set_events_cb(window);

  //  Always create in (0, 0).
  glfwSetWindowPos(window, 0, 0);
  glfwShowWindow(window);

  glfwMakeContextCurrent(window);

  glfwSwapInterval(1);

  glfwMakeContextCurrent(nullptr);

  return window;
}

void GLFWVideo::swap_window() {
  glfwMakeContextCurrent(nullptr);

  auto new_window = create_window(window_);
  if (!new_window) return;

  glfwDestroyWindow(window_);
  window_ = new_window;

  setup_icon();

  glfwMakeContextCurrent(window_);
  setup_vertex_array();
  gui_configuration_->destroy_imgui();
  gui_configuration_->init_imgui();
  glUseProgram(shader_program_);

  // Needed after the re-creation of the context.
  set_geometry();
}

inline void GLFWVideo::add_rendering_task(std::function<bool()> task) {
  if (!ongoing_destruction_) RendererSingleton::get()->add_rendering_task(this, task);
}

bool GLFWVideo::setup_shaders() {
  const char* vertex_source = R"(
  #version 330

  smooth out vec2 UV;

  uniform int geometry = 0;
  uniform int rotation;
  uniform int flip;

  const vec2 vertices[4] = vec2[]
  (
    vec2(-1.0, 1.0),
    vec2(1.0, 1.0),
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0)
  );

  const vec2 uvs[4] = vec2[]
  (
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0)
  );

  void main()
  {
    UV = uvs[gl_VertexID];

    if (geometry == 1) {
      if (flip == 1)
        UV = vec2(1.f - UV.s, UV.t);
      else if (flip == 2)
        UV = vec2(UV.s, 1.f - UV.t);
      else if (flip == 3)
        UV = vec2(1.f - UV.s, 1.f - UV.t);

      if (rotation == 1)
        UV = vec2(UV.t, 1.f - UV.s);
      else if (rotation == 2)
        UV = vec2(1.f - UV.t, UV.s);
      else if (rotation == 3)
        UV = vec2(1.f - UV.s, 1.f - UV.t);
    }

    gl_Position = vec4(vertices[gl_VertexID], 0.f, 1.f);
  }
 )";

  const char* fragment_source = R"(
  #version 330

  smooth in vec2 UV;

  out vec4 out_color;

  uniform sampler2D tex;

  void main()
  {
    out_color = texture(tex, UV);
  }
 )";

  GLint status;
  shader_program_ = glCreateProgram();

  vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader_, 1, &vertex_source, nullptr);
  glCompileShader(vertex_shader_);
  glGetShaderiv(vertex_shader_, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetProgramiv(vertex_shader_, GL_INFO_LOG_LENGTH, &length);
    std::string buffer;
    glGetShaderInfoLog(vertex_shader_, length, nullptr, const_cast<char*>(buffer.data()));
    sw_warning("Failed to compile vertex shader (glfwin): {}.", buffer);
    return false;
  }

  fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader_, 1, &fragment_source, nullptr);
  glCompileShader(fragment_shader_);
  glGetShaderiv(fragment_shader_, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetProgramiv(fragment_shader_, GL_INFO_LOG_LENGTH, &length);
    std::string buffer;
    glGetShaderInfoLog(fragment_shader_, length, nullptr, const_cast<char*>(buffer.data()));
    sw_warning("Failed to compile fragment shader (glfwin): {}.", buffer);
    return false;
  }

  glAttachShader(shader_program_, vertex_shader_);
  glAttachShader(shader_program_, fragment_shader_);
  glBindFragDataLocation(shader_program_, 0, "out_color");
  glLinkProgram(shader_program_);
  glUseProgram(shader_program_);

  return true;
}

bool GLFWVideo::setup_vertex_array() {
  if (vao_) glDeleteVertexArrays(1, &vao_);
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  return true;
}

void GLFWVideo::setup_video_texture() {
  glBindTexture(GL_TEXTURE_2D, drawing_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               vid_width_,
               vid_height_,
               0,
               GL_RGBA,
               GL_UNSIGNED_INT_8_8_8_8_REV,
               0);
  glBindTexture(GL_TEXTURE_2D, 0);
  set_viewport();
}

void GLFWVideo::setup_background_texture() {
  glBindTexture(GL_TEXTURE_2D, drawing_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               image_width_,
               image_height_,
               0,
               image_components_ == 4 ? GL_RGBA : GL_RGB,
               image_components_ == 4 ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_BYTE,
               image_data_.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  set_viewport();
}

void GLFWVideo::load_icon() {
  icon_.pixels = nullptr;
  if (!config<&InfoTree::branch_has_data>("icon")) {
    sw_warning("Icons configuration is missing (glfw).");
    return;
  }
  auto icon_config = config<&InfoTree::branch_get_value>("icon").copy_as<std::string>();

  int w = 0, h = 0, n = 0;
  auto icon_content = stbi_load(icon_config.c_str(), &w, &h, &n, 4);
  if (n != 4) return;  // Only accept 32-bit formats for the icon.
  On_scope_exit { stbi_image_free(icon_content); };
  if (!icon_content) return;
  int data_size = w * h * n;
  icon_data_.resize(data_size);
  std::copy(icon_content, icon_content + data_size, icon_data_.data());
  icon_.width = w;
  icon_.height = h;
  icon_.pixels = icon_data_.data();
}

void GLFWVideo::setup_icon() {
  if (!icon_.pixels) return;
  glfwSetWindowIcon(window_, 1, &icon_);
}

void GLFWVideo::set_events_cb(GLFWwindow* window) {
  glfwSetWindowUserPointer(window, this);  // Will be used by events callbacks.

  glfwSetWindowCloseCallback(window, close_cb);
  glfwSetWindowPosCallback(window, move_cb);
  glfwSetWindowSizeCallback(window, resize_cb);
  glfwSetWindowFocusCallback(window, focus_cb);
  glfwSetKeyCallback(window, key_cb);
  glfwSetCursorPosCallback(window, mouse_cb);
  glfwSetCursorEnterCallback(window, enter_cb);
}

void GLFWVideo::close_cb(GLFWwindow* window) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));
  quiddity->meth<&method::MBag::invoke_str>(
      quiddity->meth<&method::MBag::get_id>("disconnect-all"), "");
  quiddity->self_destruct();
}

void GLFWVideo::move_cb(GLFWwindow* window, int pos_x, int pos_y) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));
  quiddity->add_rendering_task([quiddity, pos_x, pos_y]() {
    quiddity->position_x_ = pos_x;
    quiddity->position_y_ = pos_y;
    quiddity->window_moved_ = true;
    return true;
  });
}

void GLFWVideo::resize_cb(GLFWwindow* window, int width, int height) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));
  quiddity->add_rendering_task([quiddity, width, height]() {
    quiddity->width_ = width;
    quiddity->height_ = height;
    quiddity->window_moved_ = true;
    return true;
  });
}

void GLFWVideo::focus_cb(GLFWwindow* window, int focused) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));
  if (focused)
    quiddity->graft_tree(".focused.", InfoTree::make("true"));
  else
    quiddity->graft_tree(".focused.", InfoTree::make("false"));
}

void GLFWVideo::key_cb(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));

  if (quiddity->keyb_shm_.get()) {
    guint32 val = key;
    auto keybevent = KeybEvent(val, action);
    quiddity->keyb_shm_->writer<&::shmdata::Writer::copy_to_shm>(&keybevent,
                                                                       sizeof(KeybEvent));
    quiddity->keyb_shm_->bytes_written(sizeof(KeybEvent));
  }

  if (quiddity->keyb_interaction_) {
    if (action == GLFW_RELEASE) return;  // Only process the key presses.
    switch (key) {
      case GLFW_KEY_F:
      case GLFW_KEY_ESCAPE:
        quiddity->pmanage<&property::PBag::set<bool>>(
            quiddity->fullscreen_id_,
            !quiddity->fullscreen_);  // toggle fullscreen
        break;
      case GLFW_KEY_D:
        quiddity->pmanage<&property::PBag::set<bool>>(
            quiddity->decorated_id_,
            !quiddity->decorated_);  // toggle decoration
        break;
      case GLFW_KEY_T:
        quiddity->pmanage<&property::PBag::set<bool>>(
            quiddity->always_on_top_id_,
            !quiddity->always_on_top_);  // toggle always on top status
        break;
      default:
        break;
    }
  }
}

void GLFWVideo::mouse_cb(GLFWwindow* window, double xpos, double ypos) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));

  if (!quiddity->xevents_to_shmdata_) return;
  if (!quiddity->mouse_shm_.get()) return;
  if (!quiddity->cursor_inside_) return;

  auto mouse_event = MouseEvent(xpos, ypos, 1);
  quiddity->mouse_shm_->writer<&::shmdata::Writer::copy_to_shm>(&mouse_event,
                                                                      sizeof(MouseEvent));
  quiddity->mouse_shm_->bytes_written(sizeof(MouseEvent));
}

void GLFWVideo::enter_cb(GLFWwindow* window, int entered) {
  auto quiddity = static_cast<GLFWVideo*>(glfwGetWindowUserPointer(window));
  quiddity->cursor_inside_ = entered;
}

void GLFWVideo::set_viewport(bool clear) {
  On_scope_exit {
    if (clear) glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  };

  // Resets the viewport if no video is playing.
  if (!draw_video_) {
    glViewport(0, 0, width_, height_);
    return;
  }

  float window_ratio = static_cast<float>(width_) / static_cast<float>(height_);
  float image_ratio = static_cast<float>(vid_width_) / static_cast<float>(vid_height_);

  // Inverts the ratio if rotated 90-degrees.
  if (rotation_.get_current_index() == 1 || rotation_.get_current_index() == 2)
    image_ratio = 1.f / image_ratio;

  float padding_x = 0.f;
  float padding_y = 0.f;

  if (image_ratio > window_ratio) {
    padding_y = (1.f - window_ratio / image_ratio) * height_ / 2.f;
    glViewport(padding_x, padding_y, width_, height_ * (window_ratio / image_ratio));
  } else {
    padding_x = (1.f - image_ratio / window_ratio) * width_ / 2.f;
    glViewport(padding_x, padding_y, width_ * image_ratio / window_ratio, height_);
  }
}

inline void GLFWVideo::set_color() {
  glClearColor(static_cast<GLfloat>(color_.red()) / 255.0f,
               static_cast<GLfloat>(color_.green()) / 255.0f,
               static_cast<GLfloat>(color_.blue()) / 255.0f,
               static_cast<GLfloat>(color_.alpha()) / 255.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

inline void GLFWVideo::set_rotation_shader() {
  glUniform1i(glGetUniformLocation(shader_program_, "rotation"), rotation_.get_current_index());
}

inline void GLFWVideo::set_flip_shader() {
  glUniform1i(glGetUniformLocation(shader_program_, "flip"), flip_.get_current_index());
}

inline void GLFWVideo::enable_geometry() {
  glUniform1i(glGetUniformLocation(shader_program_, "geometry"), 1);
}

inline void GLFWVideo::disable_geometry() {
  glUniform1i(glGetUniformLocation(shader_program_, "geometry"), 0);
}

inline void GLFWVideo::set_position() { glfwSetWindowPos(window_, position_x_, position_y_); }

inline void GLFWVideo::set_size() { glfwSetWindowSize(window_, width_, height_); }

inline void GLFWVideo::set_geometry() {
  set_position();
  set_color();
  set_viewport();
  set_rotation_shader();
  set_flip_shader();
}

void GLFWVideo::discover_monitor_properties() {
  int count;
  monitors_config_.clear();
  auto all_monitors = glfwGetMonitors(&count);
  if (!all_monitors) return;

  max_width_ = 0;
  max_height_ = 0;
  for (int i = 0; i < count; ++i) {
    auto mode = glfwGetVideoMode(all_monitors[i]);
    if (!mode) continue;
    MonitorConfig config;
    config.monitor = all_monitors[i];
    config.width = mode->width;
    config.height = mode->height;
    glfwGetMonitorPos(all_monitors[i], &config.position_x, &config.position_y);
    max_width_ = std::max(config.width + config.position_x, max_width_);
    max_height_ = std::max(config.height + config.position_y, max_height_);
    monitors_config_.push_back(config);
  }
}

const GLFWVideo::MonitorConfig GLFWVideo::get_monitor_config() const {
  auto config = std::find_if(
      monitors_config_.cbegin(), monitors_config_.cend(), [this](const MonitorConfig& conf) {
        // Check if the current window is within the bounds of a monitor.
        if (conf.position_x <= position_x_ && (conf.position_x + conf.width) > position_x_ &&
            conf.position_y <= position_y_ && (conf.position_y + conf.height) > position_y_) {
          return true;
        }
        return false;
      });
  if (config == monitors_config_.cend()) return MonitorConfig();
  return *config;
}

bool GLFWVideo::on_shmdata_connect(const std::string& shmpath) {
  on_shmdata_disconnect();
  shmpath_ = shmpath;
  g_object_set(G_OBJECT(shmsrc_.get_raw()), "socket-path", shmpath_.c_str(), nullptr);
  g_object_set(G_OBJECT(shmsrc_.get_raw()), "copy-buffers", TRUE, nullptr);
  shm_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
      this,
      shmsrc_.get_raw(),
      shmpath_,
      shmdata::GstTreeUpdater::Direction::reader,
      [this](const std::string& caps) {
        GstCaps* gstcaps = gst_caps_from_string(caps.c_str());
        On_scope_exit {
          if (gstcaps) gst_caps_unref(gstcaps);
        };
        GstStructure* caps_struct = gst_caps_get_structure(gstcaps, 0);
        const GValue* width_val = gst_structure_get_value(caps_struct, "width");
        const GValue* height_val = gst_structure_get_value(caps_struct, "height");
        vid_width_ = g_value_get_int(width_val);
        vid_height_ = g_value_get_int(height_val);

        auto frame_size = vid_width_ * vid_height_ * 4;  // Only RGBA is supported.
        video_frames_.resize(frame_size);

        add_rendering_task([this]() {
          draw_video_ = true;
          setup_video_texture();
          enable_geometry();
          return true;
        });
      },
      [this]() {
        add_rendering_task([this]() {
          draw_video_ = false;
          setup_background_texture();
          disable_geometry();
          return true;
        });
      });

  shm_follower_ = std::make_unique<shmdata::Follower>(
      this,
      shmpath_,
      nullptr,
      [this](const std::string& shmtype) {
        if (!cur_caps_.empty() && cur_caps_ != shmtype) {
          cur_caps_ = shmtype;
          sw_debug(
              "glfwin restarting shmdata connection "
              "because of an updated caps ({})",
              cur_caps_);
          async_this_.run_async([this]() { on_shmdata_connect(shmpath_); });

          return;
        }
        cur_caps_ = shmtype;
        add_rendering_task([this]() {
          draw_video_ = true;
          setup_video_texture();
          enable_geometry();
          return true;
        });
      },
      [this]() {
        add_rendering_task([this]() {
          draw_video_ = false;
          setup_background_texture();
          disable_geometry();
          return true;
        });
      });

  // Fakesink setup
  g_object_set(G_OBJECT(fakesink_.get_raw()),
               "silent",
               TRUE,
               "signal-handoffs",
               TRUE,
               "sync",
               FALSE,
               nullptr);
  g_signal_connect(G_OBJECT(fakesink_.get_raw()), "handoff", G_CALLBACK(on_handoff_cb), this);

  // Pipeline setup
  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);

  // Capsfilter setup
  GstCaps* usercaps = gst_caps_from_string("video/x-raw,format=RGBA");
  g_object_set(G_OBJECT(capsfilter_.get_raw()), "caps", usercaps, nullptr);

  auto nthreads_videoconvert = gst::utils::get_nthreads_property_value();
  if (nthreads_videoconvert > 0) {
    g_object_set(G_OBJECT(videoconvert_.get_raw()), "n-threads", nthreads_videoconvert, nullptr);
  }

  gst_caps_unref(usercaps);

  // Pipeline assembly
  gst_bin_add_many(GST_BIN(gst_pipeline_->get_pipeline()),
                   shmsrc_.get_raw(),
                   queue_.get_raw(),
                   videoconvert_.get_raw(),
                   capsfilter_.get_raw(),
                   gamma_.get_raw(),
                   videobalance_.get_raw(),
                   fakesink_.get_raw(),
                   nullptr);
  gst_element_link_many(shmsrc_.get_raw(),
                        queue_.get_raw(),
                        videoconvert_.get_raw(),
                        capsfilter_.get_raw(),
                        gamma_.get_raw(),
                        videobalance_.get_raw(),
                        fakesink_.get_raw(),
                        nullptr);

  install_gst_properties();
  gst_pipeline_->play(true);

  return true;
}

bool GLFWVideo::on_shmdata_disconnect() {
  cur_caps_.clear();
  shm_sub_.reset();
  shm_follower_.reset();
  On_scope_exit { gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr); };

  return remake_elements();
}

bool GLFWVideo::remake_elements() {
  remove_gst_properties();
  if (!gst::UGstElem::renew(shmsrc_) || !gst::UGstElem::renew(queue_) ||
      !gst::UGstElem::renew(videoconvert_) || !gst::UGstElem::renew(capsfilter_) ||
      !gst::UGstElem::renew(gamma_, {"gamma"}) ||
      !gst::UGstElem::renew(videobalance_, {"contrast", "brightness", "hue", "saturation"}) ||
      !gst::UGstElem::renew(fakesink_)) {
    sw_error("glfwin could not renew GStreamer elements");
    return false;
  }
  install_gst_properties();
  return true;
}

void GLFWVideo::install_gst_properties() {
  pmanage<&property::PBag::push>(
      "gamma", quiddity::property::to_prop(G_OBJECT(gamma_.get_raw()), "gamma"));
  pmanage<&property::PBag::push>(
      "contrast", quiddity::property::to_prop(G_OBJECT(videobalance_.get_raw()), "contrast"));
  pmanage<&property::PBag::push>(
      "brightness", quiddity::property::to_prop(G_OBJECT(videobalance_.get_raw()), "brightness"));
  pmanage<&property::PBag::push>(
      "hue", quiddity::property::to_prop(G_OBJECT(videobalance_.get_raw()), "hue"));
  pmanage<&property::PBag::push>(
      "saturation", quiddity::property::to_prop(G_OBJECT(videobalance_.get_raw()), "saturation"));
}

void GLFWVideo::remove_gst_properties() {
  pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>("gamma"));
  pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>("contrast"));
  pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>("brightness"));
  pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>("hue"));
  pmanage<&property::PBag::remove>(pmanage<&property::PBag::get_id>("saturation"));
}

inline void GLFWVideo::on_handoff_cb(GstElement* /*object*/,
                                     GstBuffer* buf,
                                     GstPad* /*pad*/,
                                     gpointer user_data) {
  GLFWVideo* context = static_cast<GLFWVideo*>(user_data);

  GstMapInfo map;

  if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
    context->sw_warning("gst_buffer_map failed: canceling video buffer access");
    return;
  }
  On_scope_exit { gst_buffer_unmap(buf, &map); };

  context->video_frames_.write(map.data, map.data + map.size);
}

GLFWVideo::GUIConfiguration::GUIConfiguration(GLFWVideo* window)
    : parent_window_(window), color_(255, 255, 255, 255), context_(std::make_unique<GUIContext>()) {
  ImGui::SetCurrentContext(context_->ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts = &font_atlas_;
  init_imgui();
}

GLFWVideo::GUIConfiguration::~GUIConfiguration() {
  ImGui::SetCurrentContext(context_->ctx);
  destroy_imgui();
}

void GLFWVideo::GUIConfiguration::init_imgui() {
  const char* vertex_source{R"(
          #version 330 core

          uniform mat4 ProjMtx;
          in vec2 Position;
          in vec2 UV;
          in vec4 Color;
          out vec2 Frag_UV;
          out vec4 Frag_Color;

          void main()
          {
              Frag_UV = UV;
              Frag_Color = Color;
              gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
          }
      )"};

  const char* fragment_source{R"(
          #version 330 core

          uniform sampler2D Texture;
          in vec2 Frag_UV;
          in vec4 Frag_Color;
          out vec4 Out_Color;

          void main()
          {
              Out_Color = Frag_Color * texture(Texture, Frag_UV.st);
          }
      )"};

  gui_shader_program_ = glCreateProgram();
  gui_vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
  gui_fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);

  glShaderSource(gui_vertex_shader_, 1, &vertex_source, 0);
  glShaderSource(gui_fragment_shader_, 1, &fragment_source, 0);
  glCompileShader(gui_vertex_shader_);
  glCompileShader(gui_fragment_shader_);
  glAttachShader(gui_shader_program_, gui_vertex_shader_);
  glAttachShader(gui_shader_program_, gui_fragment_shader_);
  glLinkProgram(gui_shader_program_);

  GLint status;
  glGetShaderiv(gui_vertex_shader_, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetProgramiv(gui_vertex_shader_, GL_INFO_LOG_LENGTH, &length);
    std::string buffer;
    glGetShaderInfoLog(gui_vertex_shader_, length, nullptr, const_cast<char*>(buffer.data()));
    parent_window_->sw_warning("Failed to compile GUI vertex shader (glfwin): {}.", buffer);
    return;
  }

  glGetShaderiv(gui_fragment_shader_, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint length;
    glGetProgramiv(gui_fragment_shader_, GL_INFO_LOG_LENGTH, &length);
    std::string buffer;
    glGetShaderInfoLog(gui_fragment_shader_, length, nullptr, const_cast<char*>(buffer.data()));
    parent_window_->sw_warning("Failed to compile GUI fragment shader (glfwin): {}.", buffer);
    return;
  }

  glGetProgramiv(gui_shader_program_, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length;
    glGetProgramiv(gui_shader_program_, GL_INFO_LOG_LENGTH, &length);
    std::string buffer;
    glGetProgramInfoLog(gui_shader_program_, length, nullptr, const_cast<char*>(buffer.data()));
    parent_window_->sw_warning("Failed to link GUI shader program (glfwin): {}.", buffer);
    return;
  }

  gui_texture_location_ = glGetUniformLocation(gui_shader_program_, "Texture");
  gui_proj_matrix_location_ = glGetUniformLocation(gui_shader_program_, "ProjMtx");
  gui_position_location_ = glGetAttribLocation(gui_shader_program_, "Position");
  gui_uv_location_ = glGetAttribLocation(gui_shader_program_, "UV");
  gui_color_location_ = glGetAttribLocation(gui_shader_program_, "Color");

  glGenBuffers(1, &gui_vbo_);
  glGenBuffers(1, &gui_elements_);
  glBindBuffer(GL_ARRAY_BUFFER, gui_vbo_);
  glBufferData(GL_ARRAY_BUFFER, gui_vbo_max_size_, nullptr, GL_DYNAMIC_DRAW);

  glGenVertexArrays(1, &gui_vao_);
  glBindVertexArray(gui_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, gui_vbo_);
  glEnableVertexAttribArray(gui_position_location_);
  glEnableVertexAttribArray(gui_uv_location_);
  glEnableVertexAttribArray(gui_color_location_);

  glVertexAttribPointer(gui_position_location_,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        sizeof(ImDrawVert),
                        (GLvoid*)(size_t) & (((ImDrawVert*)0)->pos));
  glVertexAttribPointer(gui_uv_location_,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        sizeof(ImDrawVert),
                        (GLvoid*)(size_t) & (((ImDrawVert*)0)->uv));
  glVertexAttribPointer(gui_color_location_,
                        4,
                        GL_UNSIGNED_BYTE,
                        GL_TRUE,
                        sizeof(ImDrawVert),
                        (GLvoid*)(size_t) & (((ImDrawVert*)0)->col));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  ImGui::SetCurrentContext(context_->ctx);

  // Initialize ImGui
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize.x = static_cast<float>(parent_window_->width_);
  io.DisplaySize.y = static_cast<float>(parent_window_->height_);
  io.DeltaTime = 1.f / 30.f;

  io.RenderDrawListsFn = render_overlay;

  fonts_list_ = get_fonts();
  if (fonts_list_.empty()) {
    parent_window_->sw_warning("Could not find fonts installed for overlay (glfw).");
    return;
  }

  // We only generate for one font at a time, by default the first one.
  if (use_custom_font_) {
    if (!generate_font_texture(custom_font_)) return;
  } else {
    if (!generate_font_texture(std::string(DATADIR) + "fonts/" +
                               fonts_list_.at(fonts_.get_current_index())))
      return;
  }

  // Remove inner padding to easily calculate text size for alignment.
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = ImVec2(0.f, 0.f);

  initialized_ = true;
}

void GLFWVideo::GUIConfiguration::destroy_imgui() {
  glDeleteTextures(1, &gui_font_texture_id_);
  glDeleteProgram(gui_shader_program_);
  glDeleteBuffers(1, &gui_vbo_);
  glDeleteBuffers(1, &gui_elements_);
  glDeleteVertexArrays(1, &gui_vao_);
}

void GLFWVideo::GUIConfiguration::init_properties() {
  auto set_font = [this](const quiddity::property::IndexOrName& val) {
    std::lock_guard<std::mutex> lock(parent_window_->configuration_mutex_);
    fonts_.select(val);
    parent_window_->add_rendering_task([this, val]() {
      generate_font_texture(std::string(DATADIR) + "fonts/" + fonts_list_.at(val.index_));
      return true;
    });
    return true;
  };
  auto get_font = [this]() { return fonts_.get(); };

  text_id_ = parent_window_->pmanage<&property::PBag::make_parented_string>(
      "overlay_text",
      "overlay_config",
      [this](const std::string& val) {
        std::lock_guard<std::mutex> lock(parent_window_->configuration_mutex_);
        text_ = val;
        return true;
      },
      [this]() { return text_; },
      "Overlay text",
      "Overlay text content",
      text_);
  alignment_id_ =
      parent_window_->pmanage<&property::PBag::make_parented_selection<unsigned int>>(
          "overlay_alignment",
          "overlay_config",
          [this](const quiddity::property::IndexOrName& val) {
            std::lock_guard<std::mutex> lock(parent_window_->configuration_mutex_);
            alignment_.select(val);
            return true;
          },
          [this]() { return alignment_.get(); },
          "Text alignment",
          "Alignment of the overlay text",
          alignment_);
  use_custom_font_id_ = parent_window_->pmanage<&property::PBag::make_parented_bool>(
      "overlay_use_config",
      "overlay_config",
      [this](bool val) {
        use_custom_font_ = val;
        if (val) {
          parent_window_->pmanage<&property::PBag::disable>(
              font_id_,
              "This property is unavailable because "
              "the custom font is currently selected.");
          parent_window_->pmanage<&property::PBag::enable>(custom_font_id_);
          if (stringutils::starts_with(custom_font_, "/") &&
              stringutils::ends_with(custom_font_, ".ttf")) {
            parent_window_->pmanage<&property::PBag::set_to_current>(custom_font_id_);
          }
        } else {
          parent_window_->pmanage<&property::PBag::disable>(
              custom_font_id_,
              "This property is disabled because the"
              " custom font option is turned off.");
          parent_window_->pmanage<&property::PBag::enable>(font_id_);
          parent_window_->pmanage<&property::PBag::set_to_current>(font_id_);
        }
        return true;
      },
      [this]() { return use_custom_font_; },
      "Toggle use of custom font",
      "Toggle use of custom font",
      false);
  custom_font_id_ = parent_window_->pmanage<&property::PBag::make_parented_string>(
      "overlay_additional_font",
      "overlay_config",
      [this](const std::string& val) {
        std::lock_guard<std::mutex> lock(parent_window_->configuration_mutex_);
        if (val.empty()) {
          parent_window_->sw_info(
              "No value was defined for overlay_additional_font or overlay_config properties.");
          return true;
        } else if (!stringutils::ends_with(val, ".ttf")) {
          parent_window_->sw_info(
              "Cannot set {} as custom font, only truetype fonts are supported (.ttf extension).",
              val);
          return false;
        } else if (!stringutils::starts_with(val, "/")) {
          parent_window_->sw_info("Only absolute paths are supported for the custom font.");
          return false;
        }
        custom_font_ = val;
        parent_window_->add_rendering_task([this]() {
          generate_font_texture(custom_font_);
          return true;
        });
        return true;
      },
      [this]() { return custom_font_; },
      "Custom text font",
      "Custom text font full path",
      custom_font_);
  fonts_ = property::Selection<>(std::move(fonts_list_), 0);
  font_id_ = parent_window_->pmanage<&property::PBag::make_parented_selection<>>(
      "overlay_fonts",
      "overlay_config",
      set_font,
      get_font,
      "Text font",
      "Font of the overlay text",
      fonts_);
  color_id_ = parent_window_->pmanage<&property::PBag::make_parented_color>(
      "overlay_color",
      "overlay_config",
      [this](const property::Color& val) {
        std::lock_guard<std::mutex> lock(parent_window_->configuration_mutex_);
        color_ = val;
        return true;
      },
      [this]() { return color_; },
      "Overlay color",
      "Overlay text color",
      color_);
  font_size_id_ = parent_window_->pmanage<&property::PBag::make_parented_unsigned_int>(
      "overlay_font_size",
      "overlay_config",
      [this](const unsigned int& val) {
        font_size_ = val;
        if (use_custom_font_)
          parent_window_->pmanage<&property::PBag::set_to_current>(custom_font_id_);
        else
          parent_window_->pmanage<&property::PBag::set_to_current>(font_id_);
        return true;
      },
      [this]() { return font_size_; },
      "Overlay font size",
      "Overlay text font size",
      font_size_,
      12,
      64);
}

std::vector<std::string> GLFWVideo::GUIConfiguration::get_fonts() {
  auto fonts =
      fileutils::get_files_from_directory(std::string(DATADIR) + "fonts", "", ".ttf", true);
  for (auto& font : fonts) {
    font = stringutils::replace_string(font, std::string(DATADIR) + "fonts/", "");
  }
  return fonts;
}

bool GLFWVideo::GUIConfiguration::generate_font_texture(std::string font) {
  struct stat filestat;
  if (stat(font.c_str(), &filestat) < 0) {
    parent_window_->sw_warning("Could not find font file {} (glfwin)", font);
    return false;
  }

  ImGui::SetCurrentContext(context_->ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  io.Fonts->AddFontFromFileTTF(font.c_str(), font_size_);
  io.Fonts->Build();

  unsigned char* pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

  glDeleteTextures(1, &gui_font_texture_id_);
  glGenTextures(1, &gui_font_texture_id_);
  glBindTexture(GL_TEXTURE_2D, gui_font_texture_id_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  io.Fonts->TexID = (void*)(intptr_t)gui_font_texture_id_;

  return true;
}

void GLFWVideo::GUIConfiguration::show() {
  ImVec2 text_position;
  ImGuiStyle& style = ImGui::GetStyle();

  style.WindowRounding = 0.0;
  style.ItemSpacing = ImVec2(0.0, text_size_.y / 2);

  auto alignment = alignment_.get_attached();
  if (alignment & Alignment::RIGHT_ALIGNED) text_position.x = parent_window_->width_ - text_size_.x;
  if (alignment & Alignment::BOTTOM_ALIGNED)
    text_position.y = parent_window_->height_ - text_size_.y;
  if (alignment & Alignment::HORIZONTAL_CENTER)
    text_position.x = (parent_window_->width_ - text_size_.x) / 2.f;
  if (alignment & Alignment::VERTICAL_CENTER)
    text_position.y = (parent_window_->height_ - text_size_.y) / 2.f;

  ImGui::SetNextWindowPos(text_position);
  ImGui::Begin(parent_window_->get_nickname().c_str(),
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);

  ImGui::TextColored(ImVec4(color_.red() / 255.f,
                            color_.green() / 255.f,
                            color_.blue() / 255.f,
                            color_.alpha() / 255.f),
                     text_.c_str(),
                     "");
  text_size_ = ImGui::GetItemRectSize();
  ImGui::End();
}

void GLFWVideo::GUIConfiguration::render_overlay(ImDrawData* draw_data) {
  if (!draw_data->CmdListsCount) return;

  auto current = RendererSingleton::get()->get_current_window();
  if (!current) return;
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);

  const float width = ImGui::GetIO().DisplaySize.x;
  const float height = ImGui::GetIO().DisplaySize.y;
  const float orthoProjection[4][4] = {
      {2.0f / width, 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / -height, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };

  GLint program;
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  glViewport(0, 0, current->width_, current->height_);
  glUseProgram(current->gui_configuration_->gui_shader_program_);
  glUniform1i(current->gui_configuration_->gui_texture_location_, 0);
  glUniformMatrix4fv(current->gui_configuration_->gui_proj_matrix_location_,
                     1,
                     GL_FALSE,
                     (GLfloat*)orthoProjection);
  glBindVertexArray(current->gui_configuration_->gui_vao_);
  for (int n = 0; n < draw_data->CmdListsCount; ++n) {
    const ImDrawList* cmd_list = draw_data->CmdLists[n];
    const ImDrawIdx* idx_buffer_offset = 0;
    unsigned int needed_vtx_size = cmd_list->VtxBuffer.size() * sizeof(ImDrawVert);
    glBindBuffer(GL_ARRAY_BUFFER, current->gui_configuration_->gui_vbo_);

    if (current->gui_configuration_->gui_vbo_max_size_ <= needed_vtx_size) {
      current->gui_configuration_->gui_vbo_max_size_ = needed_vtx_size + 2000 * sizeof(ImDrawVert);
      glBufferData(GL_ARRAY_BUFFER,
                   (GLsizeiptr)current->gui_configuration_->gui_vbo_max_size_,
                   nullptr,
                   GL_STREAM_DRAW);
    }
    unsigned char* vtx_data = (unsigned char*)glMapBufferRange(
        GL_ARRAY_BUFFER, 0, needed_vtx_size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

    if (!vtx_data) continue;
    memcpy(vtx_data, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof(ImDrawVert));
    glUnmapBuffer(GL_ARRAY_BUFFER);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, current->gui_configuration_->gui_elements_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx),
                 (GLvoid*)&cmd_list->IdxBuffer.front(),
                 GL_STREAM_DRAW);
    for (const ImDrawCmd* pcmd = cmd_list->CmdBuffer.begin(); pcmd != cmd_list->CmdBuffer.end();
         ++pcmd) {
      if (pcmd->UserCallback) {
        pcmd->UserCallback(cmd_list, pcmd);
      } else {
        glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
        glScissor((int)pcmd->ClipRect.x,
                  (int)(height - pcmd->ClipRect.w),
                  (int)(pcmd->ClipRect.z - pcmd->ClipRect.x),
                  (int)(pcmd->ClipRect.w - pcmd->ClipRect.y));
        glDrawElements(GL_TRIANGLES,
                       (GLsizei)pcmd->ElemCount,
                       sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                       idx_buffer_offset);
      }
      idx_buffer_offset += pcmd->ElemCount;
    }
  }

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glUseProgram(program);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

}  // namespace switcher
