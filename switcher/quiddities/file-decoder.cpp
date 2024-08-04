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

#include "./file-decoder.hpp"
#include <filesystem>
#include "../gst/utils.hpp"
#include "../utils/scope-exit.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(FileDecoder,
                                     "filesrc",
                                     "file Player",
                                     "File decoding to one or more shmdata",
                                     "LGPL",
                                     "Nicolas Bouillot");

const std::string FileDecoder::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "custom%",
      "description": "Produced random buffers",
      "can_do": [ "video/x-raw", "audio/x-raw", "text/x-raw" ]
    }
  ]
}
)");

FileDecoder::FileDecoder(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}),
      location_id_(pmanage<&property::PBag::make_string>(
          "location",
          [this](const std::string& val) {
            location_ = val;
            return load_file(location_);
          },
          [this]() { return location_; },
          "File location",
          "Location of the file to decode",
          "")),
      play_id_(pmanage<&property::PBag::make_bool>(
          "play",
          [this](const bool val) {
            play_ = val;
            if (gst_pipeline_) {
              gst_pipeline_->play(play_);
            }
            return true;
          },
          [this]() { return play_; },
          "Play",
          "Play/pause the player",
          play_)),
      cur_pos_id_(0),
      loop_id_(pmanage<&property::PBag::make_bool>(
          "loop",
          [this](const bool val) {
            loop_ = val;
            if (gst_pipeline_) gst_pipeline_->loop(loop_);
            return true;
          },
          [this]() { return loop_; },
          "Looping",
          "Loop media",
          loop_)),
      speed_id_(pmanage<&property::PBag::make_double>(
          "rate",
          [this](const double& val) {
            // just pause if rate is set to 0
            if (0 == val) {
              if (gst_pipeline_) {
                {
                  auto lock = pmanage<&property::PBag::get_lock>(play_id_);
                  play_ = false;
                  gst_pipeline_->play(false);
                }
                pmanage<&property::PBag::notify>(play_id_);
              }
              return true;
            }
            // applying rate
            if (gst_pipeline_) {
              if (!gst_pipeline_->speed(val)) return false;
              speed_ = val;
              return true;
            }
            return true;
          },
          [this]() { return speed_; },
          "Palyback rate",
          "A rate of 1.0 means normal playback rate, 2.0 means double speed. "
          "Negatives values means backwards playback.",
          speed_,
          0,
          10.0)) {}

bool FileDecoder::load_file(const std::string& path) {
  {
    if (!std::filesystem::exists(std::filesystem::path(path))) {
      sw_warning("file {} could not be loaded (does not exist)", path);
      return false;
    }
  }
  // cleaning previous
  shm_subs_.clear();
  media_loaded_ = false;
  counter_.reset_counter_map();
  if (0 != cur_pos_id_) pmanage<&property::PBag::remove>(cur_pos_id_);
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, [this, path](GstMessage* message) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_DURATION) {
      gint64 duration = GST_CLOCK_TIME_NONE;
      if (gst_element_query_duration(
              GST_ELEMENT(GST_MESSAGE_SRC(message)), GST_FORMAT_TIME, &duration)) {
        cur_pos_id_ = pmanage<&property::PBag::make_unsigned_int>(
            "pos",
            [this](const unsigned int& val) {
              cur_pos_ = val;
              if (gst_pipeline_) {
                if (play_) return gst_pipeline_->seek(cur_pos_);
                if (!gst_pipeline_->seek_key_frame(cur_pos_)) return false;
                gst_pipeline_->play(true);
                gst::utils::wait_state_changed(gst_pipeline_->get_pipeline());
                gst_pipeline_->play(false);
              }
              return true;
            },
            [this]() {
              if (gst_pipeline_) {
                gint64 position = GST_CLOCK_TIME_NONE;
                if (gst_element_query_position(
                        gst_pipeline_->get_pipeline(), GST_FORMAT_TIME, &position)) {
                  cur_pos_ = GST_TIME_AS_MSECONDS(position);
                }
              }
              return cur_pos_;
            },
            "Current position (ms)",
            "Current position in the file",
            0,
            0,
            GST_TIME_AS_MSECONDS(duration));
      } else
        sw_warning("issue querying file duration {}", path);
    }
    return GST_BUS_PASS;
  });
  gst_pipeline_->loop(loop_);
  decodebin_.reset();

  // creating new decoder
  filesrc_ = gst_element_factory_make("filesrc", nullptr);
  g_object_set(G_OBJECT(filesrc_), "location", path.c_str(), nullptr);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), filesrc_);
  decodebin_ = std::make_unique<gst::DecodebinToShmdata>(
      gst_pipeline_.get(),
      [this](GstElement* el, const std::string& media_type, const std::string& media_label) {
        configure_shmdatasink(el, media_type, media_label);
      },
      [this]() { sw_warning("discarding uncomplete custom frame due to a network loss"); },
      [this]() {
        sw_debug("file-src: finished loading media from file");
        std::unique_lock<std::mutex> lock(media_loaded_mutex_);
        media_loaded_ = true;
        media_loaded_cond_.notify_one();
      },
      true);
  if (!decodebin_->invoke_with_return<gboolean>([this](GstElement* el) {
        return gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), el);
      })) {
    sw_warning("decodebin cannot be added to pipeline");
  }

  if (!decodebin_->invoke_with_return<bool>(
          [&](GstElement* el) { return gst_element_link(filesrc_, el) ? true : false; })) {
    sw_warning("file-decoder: cannot link filesrc with decodebin");
  }

  {
    std::unique_lock<std::mutex> lock(media_loaded_mutex_);
    gst_element_set_state(gst_pipeline_->get_pipeline(), GST_STATE_PAUSED);
    gst::utils::wait_state_changed(gst_pipeline_->get_pipeline());
    if (!media_loaded_cond_.wait_for(
            lock, std::chrono::milliseconds(2000), [this] { return media_loaded_; })) {
      sw_warning("file-src: media loading timed out");
    }
  }
  if (play_) gst_pipeline_->play(true);

  position_task_ = std::make_unique<PeriodicTask<>>(
      [this]() {
        if (0 == cur_pos_id_) return;
        gint64 position = GST_CLOCK_TIME_NONE;
        if (gst_element_query_position(gst_pipeline_->get_pipeline(), GST_FORMAT_TIME, &position)) {
          if (cur_pos_ != GST_TIME_AS_MSECONDS(position)) {
            pmanage<&property::PBag::notify>(cur_pos_id_);
          }
        }
      },
      std::chrono::milliseconds(500));
  return true;
}

void FileDecoder::configure_shmdatasink(GstElement* element,
                                        const std::string& media_type,
                                        const std::string& media_label) {
  auto count = counter_.get_count(media_label + media_type);
  std::string media_name = media_type;
  if (count != 0) media_name.append("-" + std::to_string(count));
  std::string shmpath;
  if (media_label.empty()) {
    shmpath = claw_.get_writer_shmpath(
        claw_.add_writer_to_meta(claw_.get_swid("custom%"), {media_name, media_name}));
  } else {
    shmpath = claw_.get_writer_shmpath(claw_.add_writer_to_meta(
        claw_.get_swid("custom%"), {media_label + "-" + media_name, media_label}));
  }

  g_object_set(G_OBJECT(element), "socket-path", shmpath.c_str(), nullptr);
  auto extra_caps = get_quiddity_caps();
  g_object_set(G_OBJECT(element), "extra-caps-properties", extra_caps.c_str(), nullptr);
  shm_subs_.emplace_back(std::make_unique<shmdata::GstTreeUpdater>(
      this, element, shmpath, shmdata::GstTreeUpdater::Direction::writer));
}

}  // namespace quiddities
}  // namespace switcher
