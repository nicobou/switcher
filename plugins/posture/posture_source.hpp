/*
 * This file is part of posture.
 *
 * posture is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_POSTURE_SRC_H__
#define __SWITCHER_POSTURE_SRC_H__

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "./posture.hpp"
#include "switcher/quiddity.hpp"
#include "switcher/segment.hpp"
#include "switcher/startable-quiddity.hpp"
#include "switcher/custom-property-helper.hpp"


namespace switcher {
class PostureSrc:public Quiddity, public Segment, public StartableQuiddity {
 public:
  SWITCHER_DECLARE_QUIDDITY_PUBLIC_MEMBERS(PostureSrc);
  PostureSrc();
  ~PostureSrc();
  PostureSrc(const PostureSrc &) = delete;
  PostureSrc & operator=(const PostureSrc &) = delete;

  bool start();
  bool stop();

 private:
  CustomPropertyHelper::ptr custom_props_;
  std::string calibration_path_ {"default.kvc"};
  std::string devices_path_ {"devices.kvc"};
  unsigned int device_index_ {0};
  bool capture_ir_ {false};
  bool compress_cloud_ {false};
  GParamSpec *calibration_path_prop_ {nullptr};
  GParamSpec *devices_path_prop_ {nullptr};
  GParamSpec *device_index_prop_ {nullptr};
  GParamSpec *capture_ir_prop_ {nullptr};
  GParamSpec *compress_cloud_prop_ {nullptr};

  int capture_mode_ {0};
  GParamSpec *capture_mode_prop_ {nullptr};
  GEnumValue capture_modes_enum_[16];

  std::shared_ptr<posture::ZCamera> zcamera_ {nullptr};

  ShmdataAnyWriter::ptr cloud_writer_ {nullptr};
  ShmdataAnyWriter::ptr depth_writer_ {nullptr};
  ShmdataAnyWriter::ptr rgb_writer_ {nullptr};
  ShmdataAnyWriter::ptr ir_writer_ {nullptr};

  std::mutex shmwriters_queue_mutex_ {};
  std::deque<std::shared_ptr<std::vector<unsigned char>>> shmwriters_queue_ {};

  bool cloud_compressed_ {false};
  int depth_width_ {0}, depth_height_ {0};
  int rgb_width_ {0}, rgb_height_ {0};
  int ir_width_ {0}, ir_height_ {0};

  bool init() final;

  static const gchar *get_calibration_path(void *user_data);
  static void set_calibration_path(const gchar *name, void *user_data);
  static const gchar *get_devices_path(void *user_data);
  static void set_devices_path(const gchar *name, void *user_data);
  static int get_device_index(void *user_data);
  static void set_device_index(const int index, void *user_data);
  static int get_capture_ir(void *user_data);
  static void set_capture_ir(const int ir, void *user_data);
  static int get_compress_cloud(void *user_data);
  static void set_compress_cloud(const int compress, void *user_data);
  static int get_capture_mode(void *user_data);
  static void set_capture_mode(const int mode, void *user_data);

  static void cb_frame_cloud(void *context,
                             const std::vector<char>&data);
  static void cb_frame_depth(void *context,
                             const std::vector<unsigned char>&data,
                             int width, int height);
  static void cb_frame_rgb(void *context,
                           const std::vector<unsigned char>&data,
                           int width, int height);
  static void cb_frame_ir(void *context,
                          const std::vector<unsigned char>&data,
                          int width, int height);
  static void free_sent_buffer(void* data);
  void check_buffers();

  std::shared_ptr<posture::Display> display_ {nullptr};
};

SWITCHER_DECLARE_PLUGIN(PostureSrc);
}  // namespace switcher

#endif
