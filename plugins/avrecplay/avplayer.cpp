/*
 * This file is part of switcher-avrecplay.
 *
 * switcher-avrecplay is free software; you can redistribute it and/or
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

#include "avplayer.hpp"

#include <filesystem>

#include "switcher/utils/scope-exit.hpp"

namespace fs = std::filesystem;

namespace switcher {
namespace quiddities {
SWITCHER_DECLARE_PLUGIN(AVPlayer);
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(AVPlayer,
                                     "avplayer",
                                     "Audio/video shmdata player",
                                     "Replays and controls a recorded shmdata audio/video file",
                                     "LGPL",
                                     "Jérémie Soria");

const std::string AVPlayer::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "stream%",
      "description": "Streams from file",
      "can_do": [ "all" ]
    }
  ]
}
)");

AVPlayer::AVPlayer(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf)),
      Startable(this),
      gst_pipeline_(std::make_unique<gst::Pipeliner>(nullptr, nullptr)) {
  pmanage<&property::PBag::make_string>(
      "playpath",
      [this](const std::string& val) {
        if (val.empty()) {
          sw_warning("Empty folder provided for shmdata recorder.");
          return false;
        }

        if (!fs::is_directory(fs::status(val))) {
          sw_warning("The specified folder does not exist (avplayer).");
          return false;
        }

        playpath_ = val;
        return true;
      },
      [this]() { return playpath_; },
      "Source folder",
      "Location of the folder from which the player will read recorded shmdata files",
      playpath_);

  pmanage<&property::PBag::make_bool>(
      "paused",
      [this](const bool val) {
        pause_ = val;
        gst_pipeline_->play(pause_);
        return true;
      },
      []() { return false; },
      "Paused",
      "Toggle paused status of the stream",
      pause_);
}

bool AVPlayer::start() {
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(
      [this](GstMessage* msg) { return this->bus_async(msg); },
      /*[this](GstMessage* msg) { return this->bus_sync(msg); }*/ nullptr);
  std::vector<std::string> playlist;
  if (!playpath_.empty()) {
    for (auto& dir : fs::directory_iterator(playpath_)) {
      playlist.push_back(dir.path());
    }
  }

  // Create a pipeline to read all the files and write in a shmdatasink.
  std::string description;
  int i = 0;  // File index of written shmdata
  for (auto& file : playlist) {
    if (file == "." || file == "..") continue;
    auto swid = claw_.add_writer_to_meta(claw_.get_swid("stream%"), {"", file});
    auto to_play = std::make_unique<ShmFile>(
        claw_.get_writer_shmpath(swid), playpath_ + "/" + file, "shmsink_" + file);

    auto extra_caps = get_quiddity_caps();
    description += std::string("filesrc") + (i == 0 ? " do-timestamp=true " : " ") + "location=" +
                   to_play->filepath_ + " ! decodebin ! shmdatasink name=" + to_play->sink_name_ +
                   " socket-path=" + to_play->shmpath_ + " extra-caps-properties='" + extra_caps + "' ";
    files_list_.push_back(std::move(to_play));
    ++i;
  }

  GError* error = nullptr;
  avplay_bin_ = gst_parse_bin_from_description(description.c_str(), FALSE, &error);
  bool success = true;
  On_scope_exit {
    if (!success) gst_object_unref(avplay_bin_);
  };

  if (error) {
    sw_warning("Could not create shmdata player: {}", std::string(error->message));
    g_error_free(error);
    return false;
  }

  for (auto& file : files_list_) {
    file->sink_element_ = gst_bin_get_by_name(GST_BIN(avplay_bin_), file->sink_name_.c_str());
    if (!file->sink_element_) continue;

    file->shmsink_sub_ = std::make_unique<shmdata::GstTreeUpdater>(
        this, file->sink_element_, file->shmpath_, shmdata::GstTreeUpdater::Direction::writer);
  }

  g_object_set(G_OBJECT(gst_pipeline_->get_pipeline()), "async-handling", TRUE, nullptr);
  g_object_set(G_OBJECT(avplay_bin_), "async-handling", TRUE, nullptr);
  gst_bin_add(GST_BIN(gst_pipeline_->get_pipeline()), avplay_bin_);
  gst_pipeline_->play(true);
  return true;
}
bool AVPlayer::stop() {
  position_task_.reset();
  gst_pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr);
  pmanage<&property::PBag::remove>(position_id_);
  position_id_ = 0;
  track_duration_ = 0;
  pause_ = false;
  files_list_.clear();
  for (const auto& swid : claw_.get_swids()) {
    claw_.remove_writer_from_meta(swid);
  }
  return true;
}

GstBusSyncReply AVPlayer::bus_async(GstMessage* msg) {
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS: {
      th_->run_async([this]() { pmanage<&property::PBag::set_str_str>("started", "false"); });
      break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
      if (position_id_ == 0) {
        gst_element_query_duration(
            gst_pipeline_->get_pipeline(), GST_FORMAT_TIME, &track_duration_);

        if (track_duration_ != 0) {
          position_id_ = pmanage<&property::PBag::make_int>(
              "track_position",
              [this](const int& pos) {
                std::lock_guard<std::mutex> lock(seek_mutex_);
                position_ = pos;
                seek_called_ = true;
                gst_pipeline_->play(false);
                return true;
              },
              [this]() { return position_; },
              "Track position",
              "Current position of the track",
              0,
              0,
              track_duration_ / GST_SECOND);
          position_task_ = std::make_unique<PeriodicTask<>>(
              [this]() {
                gint64 position;
                gst_element_query_position(
                    gst_pipeline_->get_pipeline(), GST_FORMAT_TIME, &position);
                position_ = static_cast<int>(position / GST_SECOND);
                pmanage<&property::PBag::notify>(position_id_);

              },
              std::chrono::milliseconds(500));
        }
      }
      std::lock_guard<std::mutex> lock(seek_mutex_);
      if (seek_called_) {
        seek_called_ = false;
        gst_element_seek_simple(gst_pipeline_->get_pipeline(),
                                GST_FORMAT_TIME,
                                GST_SEEK_FLAG_FLUSH,
                                position_ * GST_SECOND);
        gst_pipeline_->play(true);
      }
      break;
    }
    default:
      break;
  }
  return GST_BUS_PASS;
}

}  // namespace quiddities
}  // namespace switcher
