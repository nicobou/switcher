/*
 * This file is part of switcher-osc.
 *
 * switcher-osc is free software; you can redistribute it and/or
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

#include "./osc-to-shmdata.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(OscToShmdata,
                                     "OSCsrc",
                                     "OSC Receiver",
                                     "Receive OSC messages and write to shmdata",
                                     "LGPL",
                                     "Nicolas Bouillot");

const std::string OscToShmdata::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "osc",
      "description": "OSC stream",
      "can_do": ["application/x-libloserialized-osc"]
    }
  ]
}
)");

OscToShmdata::OscToShmdata(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}),
      Startable(this),
      port_id_(pmanage<&property::PBag::make_int>(
          "port",
          [this](const int& val) {
            port_ = val;
            return true;
          },
          [this]() { return port_; },
          "Port",
          "OSC port to listen to",
          port_,
          1,
          65536)) {}

OscToShmdata::~OscToShmdata() { stop(); }

bool OscToShmdata::start() {
  // creating a shmdata
  shm_ = std::make_unique<shmdata::Writer>(
      this, claw_.get_shmpath_from_writer_label("osc"), 1, "application/x-libloserialized-osc");
  if (!shm_.get()) {
    sw_warning("OscToShmdata failed to start");
    shm_.reset(nullptr);
    return false;
  }

  osc_thread_ = lo_server_thread_new(std::to_string(port_).c_str(), osc_error);
  if (nullptr == osc_thread_) return false;
  /* add method that will match any path and args */
  lo_server_thread_add_method(osc_thread_, nullptr, nullptr, osc_handler, this);
  lo_server_thread_start(osc_thread_);
  return true;
}

bool OscToShmdata::stop() {
  shm_.reset(nullptr);
  if (nullptr != osc_thread_) {
    lo_server_thread_free(osc_thread_);
    osc_thread_ = nullptr;
    return true;
  }
  return false;
}

/* catch any osc incoming messages. */
int OscToShmdata::osc_handler(const char* path,
                              const char* /*types*/,
                              lo_arg** /*argv*/,
                              int /*argc*/,
                              lo_message m,
                              void* user_data) {
  OscToShmdata* context = static_cast<OscToShmdata*>(user_data);
  lo_timetag timetag = lo_message_get_timestamp(m);
  if (0 != timetag.sec) {
    // FIXME handle internal timetag
    // note: this is not implemented in osc-send
  }
  size_t size = 0;
  void* buftmp = lo_message_serialise(m, path, nullptr, &size);
  if (context->shm_->writer<&::shmdata::Writer::alloc_size>() < size) {
    context->shm_.reset(nullptr);
    context->shm_.reset(new shmdata::Writer(context,
                                            context->claw_.get_shmpath_from_writer_label("osc"),
                                            size,
                                            "application/x-libloserialized-osc"));
  }
  context->shm_->writer<&::shmdata::Writer::copy_to_shm>(buftmp, size);
  context->shm_->bytes_written(size);
  free(buftmp);
  return 0;
}

void OscToShmdata::osc_error(int /*num*/, const char* /*msg*/, const char* /*path*/) {}

}  // namespace quiddities
}  // namespace switcher
