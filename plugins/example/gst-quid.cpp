/*
 * This file is part of switcher-plugin-example.
 *
 * switcher-plugin-example is free software; you can redistribute it and/or
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

#include "gst-quid.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(GstQuid,
                                     "gst",
                                     "Gst Quid",
                                     "Plugin implementing a simple Gst pipeline",
                                     "LGPL",
                                     "Hantz-Carly F. Vius");

GstQuid::GstQuid(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf)), quiddity::Startable(this) {
  LOGGER_DEBUG(this->logger, "GstQuid::GstQuid");
}

bool GstQuid::remake_elements() {
  LOGGER_DEBUG(this->logger, "GstQuid::remake_elements");
  if (!(gst::UGstElem::renew(src_) && gst::UGstElem::renew(sink_))) {
    LOGGER_ERROR(this->logger, "GstQuid::remake_elements: Could not renew GStreamer elements");
    return false;
  }

  return true;
}

bool GstQuid::stop() {
  if (!pipeline_) {
    LOGGER_DEBUG(this->logger, "GstQuid::stop_pipeline: Pipeline not initialized. Nothing to do.");
    return true;
  }

  pipeline_->play(false);
  LOGGER_DEBUG(this->logger, "GstQuid::stop_pipeline: Pipeline stopped.");

  return true;
}

bool GstQuid::start() {
  if (pipeline_) {
    LOGGER_DEBUG(this->logger, "GstQuid::create_pipeline: Pipeline already created. Stopping");
    pipeline_->play(false);
  }

  pipeline_ = std::make_unique<gst::Pipeliner>(nullptr, nullptr);
  if (!remake_elements()) {
    LOGGER_ERROR(this->logger, "GstQuid::create_pipeline: Could not remake GStreamer elements");
    return false;
  }

  LOGGER_DEBUG(this->logger, "GstQuid::create_pipeline: Pipeline elements made");

  gst_bin_add_many(GST_BIN(pipeline_->get_pipeline()), src_.get_raw(), sink_.get_raw(), nullptr);

  gst_element_link(src_.get_raw(), sink_.get_raw());

  if (!static_cast<bool>(pipeline_.get())) {
    LOGGER_ERROR(this->logger, "GstQuid::create_pipeline: Pipeline not found");
    return false;
  }

  pipeline_->play(true);

  LOGGER_DEBUG(this->logger, "GstQuid::create_pipeline: Pipeline started");
  return true;
}

}  // namespace quiddities
}  // namespace switcher
