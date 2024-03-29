/*
 * This file is part of switcher-portmidi.
 *
 * switcher-portmidi is free software; you can redistribute it and/or
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

#ifndef __SWITCHER_PORTMIDI_SOURCE_H__
#define __SWITCHER_PORTMIDI_SOURCE_H__

#include "./portmidi-devices.hpp"
#include "switcher/quiddity/quiddity.hpp"
#include "switcher/quiddity/startable.hpp"
#include "switcher/shmdata/writer.hpp"

namespace switcher {
namespace quiddities {
using namespace quiddity;
class PortMidiSource : public Quiddity, public Startable, public PortMidi {
 public:
  PortMidiSource(quiddity::Config&&);
  ~PortMidiSource() = default;
  PortMidiSource(const PortMidiSource&) = delete;
  PortMidiSource& operator=(const PortMidiSource&) = delete;

 private:
  typedef struct {
    PortMidiSource* port_midi_source_{nullptr};
    std::string property_long_name_{};
  } MidiPropertyContext;

  static const std::string kConnectionSpec;  //!< Shmdata specifications
  std::unique_ptr<shmdata::Writer> shm_{nullptr};
  int last_status_{-1};
  int last_data1_{-1};
  int last_data2_{-1};
  // properties
  property::prop_id_t devices_id_{0};
  // last midi value property
  property::prop_id_t last_midi_value_id_{0};
  // method ids
  method::meth_id_t next_midi_event_to_property_id_{0};
  method::meth_id_t remove_midi_property_id_{0};
  method::meth_id_t map_midi_to_property_id_{0};
  // other members
  bool make_property_for_next_midi_event_{false};
  std::string next_property_name_{};
  std::map<std::string, property::prop_id_t> prop_ids_{};
  // this is persistent to the quiddity:
  std::map<std::string, MidiPropertyContext> midi_property_contexts_{};
  std::map<std::pair<unsigned int, unsigned int>, std::string> midi_channels_{};
  std::map<std::string, unsigned int> midi_values_{};
  // using property name instead of long name:
  std::map<std::string, property::prop_id_t> unused_props_specs_{};

  bool start() final;
  bool stop() final;

  bool make_property(const std::string& property_long_name, int last_status, int last_data);
  static int get_midi_value(void* user_data);
  // midi properties
  bool next_midi_event_to_property_method(const std::string& long_name);
  bool remove_property_method(const std::string& long_name);
  static int get_midi_property_value(void* user_data);
  static void on_pm_event(PmEvent* event, void* user_data);
};

SWITCHER_DECLARE_PLUGIN(PortMidiSource);

}  // namespace quiddities
}  // namespace switcher
#endif
