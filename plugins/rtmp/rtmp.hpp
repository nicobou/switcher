/*
 * This file is part of switcher-rtmp.
 *
 * switcher-rtmp is free software; you can redistribute it and/or
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

#include "switcher/gst/pipeliner.hpp"
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/shmdata/connector.hpp"
#include "switcher/shmdata/gst-tree-updater.hpp"

namespace switcher {
namespace quiddities {
/**
 * RTMP class,
 * RTMP quiddity compatible with multiple streaming applications (Youtube, Twitch,...)
 */
using namespace quiddity;
class RTMP : public Quiddity {
 public:

  //! Constructor
  RTMP(quiddity::Config&&);

  //! Destructor
  ~RTMP() = default;

  //! Mandatory quiddity base class method

 private:
  enum class ShmType { AUDIO = 0, VIDEO = 1 };

  // Shmdata methods
  bool on_shmdata_connect(const std::string& shmpath, ShmType type);
  bool on_shmdata_disconnect(ShmType type);
  bool can_sink_caps(std::string str_caps);

  // Gstreamer pipeline creation
  bool create_gst_pipeline();

  std::string audio_shmpath_{};  //!< Path of the audio input shmdata
  std::string video_shmpath_{};  //!< Path of the video input shmdata
  shmdata::Connector
      shmcntr_;  //!< Shmdata connector of uncompressed audio/video into the quiddity.
  std::unique_ptr<shmdata::GstTreeUpdater> shmaudio_sub_{nullptr};  //!< Subscriber to audio shmdata
  std::unique_ptr<shmdata::GstTreeUpdater> shmvideo_sub_{nullptr};  //!< Subscriber to video shmdata

  std::unique_ptr<gst::Pipeliner> gst_pipeline_{nullptr};  //!< Gstreamer pipeline

  property::prop_id_t stream_app_url_id_{0};    //!< Stream URL property id
  std::string stream_app_url_{};                //!< RTMP url of the streaming application
  property::prop_id_t stream_key_id_{0};        //!< Stream key property id
  std::string stream_key_{};                    //!< Stream key, found on the streaming application
};

}  // namespace quiddities
}  // namespace switcher
