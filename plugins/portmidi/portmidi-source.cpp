/*
 * This file is part of switcher-portmidi.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
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

#include "./portmidi-source.hpp"
#include <time.h>
#include <switcher/quiddity/container.hpp>
#include <switcher/switcher.hpp>
#include <switcher/utils/scope-exit.hpp>
#include "switcher/infotree/json-serializer.hpp"

namespace switcher {
namespace quiddities {
SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(PortMidiSource,
                                     "midisrc",
                                     "Midi (PortMidi)",
                                     "Midi to shmdata and properties",
                                     "LGPL",
                                     "Nicolas Bouillot");

const std::string PortMidiSource::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "midi",
      "description": "Midi Stream",
      "can_do": ["audio/midi"]
    }
]
}
)");

PortMidiSource::PortMidiSource(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}), Startable(this) {
  if (input_devices_enum_.empty()) {
    sw_info("No MIDI capture device detected.");
    is_valid_ = false;
    return;
  }
  devices_id_ = pmanage<&property::PBag::make_selection<>>(
      "device",
      [this](const quiddity::property::IndexOrName& val) {
        input_devices_enum_.select(val);
        return true;
      },
      [this]() { return input_devices_enum_.get(); },
      "Capture device",
      "MIDI capture devices to use",
      input_devices_enum_);
  last_midi_value_id_ =
      pmanage<&property::PBag::make_int>("last-midi-value",
                                               nullptr,
                                               [this]() { return last_data2_; },
                                               "Last MIDI value",
                                               "Last MIDI value seen on capture device",
                                               0,
                                               0,
                                               127);
  pmanage<&property::PBag::disable>(last_midi_value_id_, disabledWhenStopedMsg);

  auto next_midi_event_to_property_id_ =
      mmanage<&method::MBag::make_method<std::function<bool(std::string)>>>(
          "next_midi_event_to_property",
          infotree::json::deserialize(
              R"(
                  {
                   "name" : "Next MIDI Event To property::Property",
                   "description" : "Wait for a MIDI event and make a property for this channel",
                   "arguments" : [
                     {
                        "long name" : "Property Long Name",
                        "description" : "string"
                     }
                   ]
                  }
              )"),
          [this](const std::string& prop_name) {
            return next_midi_event_to_property_method(prop_name);
          });
  mmanage<&method::MBag::disable>(next_midi_event_to_property_id_,
                                        Startable::disabledWhenStopedMsg);

  auto remove_midi_property_id_ =
      mmanage<&method::MBag::make_method<std::function<bool(std::string)>>>(
          "remove_midi_property",
          infotree::json::deserialize(
              R"(
                  {
                   "name" : "Remove Midi property::Property",
                   "description" : "remove a property made with Make property::Property",
                   "arguments" : [
                     {
                        "long name" : "Property Long Name",
                        "description" : "string"
                     }
                   ]
                  }
              )"),
          [this](const std::string& prop_name) { return remove_property_method(prop_name); });
  mmanage<&method::MBag::disable>(remove_midi_property_id_, Startable::disabledWhenStopedMsg);

  using map_midi_to_prop_t = std::function<bool(std::string, int, int)>;
  auto map_midi_to_property_id_ = mmanage<&method::MBag::make_method<map_midi_to_prop_t>>(
      "map_midi_to_property",
      infotree::json::deserialize(
          R"(
                  {
                   "name" : "Map midi channel to property",
                   "description" :  "creates a property from a midi channel",
                   "arguments" : [
                     {
                        "long name" : "Property Long Name",
                        "description" : "string"
                     },
                     {
                        "long name" : "Midi Status",
                        "description" : "int"
                     },
                     {
                        "long name" : "Midi data1",
                        "description" : "int"
                     }
                   ]
                  }
              )"),
      [this](const std::string& prop_name, int status, int data) {
        return make_property(prop_name, status, data);
      });
  mmanage<&method::MBag::disable>(map_midi_to_property_id_, Startable::disabledWhenStopedMsg);
}

bool PortMidiSource::start() {
  shm_ = std::make_unique<shmdata::Writer>(
      this, claw_.get_shmpath_from_writer_label("midi"), sizeof(PmEvent), "audio/midi");
  if (!shm_.get()) {
    sw_info("Midi failed to start");
    shm_.reset(nullptr);
    return false;
  }
  pmanage<&property::PBag::disable>(devices_id_, disabledWhenStartedMsg);
  open_input_device(std::stoi(input_devices_enum_.get_attached()), on_pm_event, this);
  pmanage<&property::PBag::enable>(last_midi_value_id_);
  mmanage<&method::MBag::enable>(next_midi_event_to_property_id_);
  mmanage<&method::MBag::enable>(remove_midi_property_id_);
  mmanage<&method::MBag::enable>(map_midi_to_property_id_);
  return true;
}

bool PortMidiSource::stop() {
  close_input_device(std::stoi(input_devices_enum_.get_attached()));
  pmanage<&property::PBag::disable>(last_midi_value_id_, disabledWhenStopedMsg);
  mmanage<&method::MBag::disable>(next_midi_event_to_property_id_,
                                        Startable::disabledWhenStopedMsg);
  mmanage<&method::MBag::disable>(remove_midi_property_id_, Startable::disabledWhenStopedMsg);
  mmanage<&method::MBag::disable>(map_midi_to_property_id_, Startable::disabledWhenStopedMsg);
  pmanage<&property::PBag::enable>(devices_id_);
  shm_.reset(nullptr);
  return true;
}

void PortMidiSource::on_pm_event(PmEvent* event, void* user_data) {
  PortMidiSource* context = static_cast<PortMidiSource*>(user_data);

  PmEvent* tmp_event = (PmEvent*)g_malloc(sizeof(PmEvent));
  tmp_event->message = event->message;
  tmp_event->timestamp = event->timestamp;
  context->shm_->writer<&::shmdata::Writer::copy_to_shm>(tmp_event, sizeof(PmEvent));
  context->shm_->bytes_written(sizeof(PmEvent));
  g_free(tmp_event);

  unsigned int status = Pm_MessageStatus(event->message);
  unsigned int data1 = Pm_MessageData1(event->message);
  unsigned int data2 = Pm_MessageData2(event->message);

  context->last_status_ = static_cast<int>(status);
  context->last_data1_ = static_cast<int>(data1);

  {
    auto lock = context->pmanage<&property::PBag::get_lock>(context->last_midi_value_id_);
    context->last_data2_ = (int)data2;
  }
  context->pmanage<&property::PBag::notify>(context->last_midi_value_id_);
  // updating property if needed
  if (context->midi_channels_.find(std::make_pair(status, data1)) !=
      context->midi_channels_.end()) {
    std::string prop_long_name = context->midi_channels_[std::make_pair(status, data1)];
    context->midi_values_[prop_long_name] = data2;
    context->pmanage<&property::PBag::notify>(context->prop_ids_[prop_long_name]);
  }

  // making property if needed
  if (context->make_property_for_next_midi_event_) {
    context->make_property(
        context->next_property_name_, context->last_status_, context->last_data1_);
    context->make_property_for_next_midi_event_ = FALSE;
  }
}

bool PortMidiSource::next_midi_event_to_property_method(const std::string& long_name) {
  make_property_for_next_midi_event_ = TRUE;
  next_property_name_ = long_name;
  return true;
}

bool PortMidiSource::remove_property_method(const std::string& long_name) {
  if (midi_property_contexts_.find(long_name) == midi_property_contexts_.end()) {
    sw_debug("property {} not found for removing", std::string(long_name));
    return false;
  }

  std::pair<unsigned int, unsigned int> midi_channel;
  for (auto& it : midi_channels_) {
    if (it.second == long_name) {
      midi_channel = it.first;
      break;
    }
  }

  gchar* prop_name = g_strdup_printf("%u-%u", midi_channel.first, midi_channel.second);
  pmanage<&property::PBag::remove>(prop_ids_[long_name]);
  unused_props_specs_[prop_name] = prop_ids_[long_name];
  prop_ids_.erase(long_name);
  midi_channels_.erase(midi_channel);
  midi_values_.erase(long_name);
  g_free(prop_name);
  return true;
}

bool PortMidiSource::make_property(const std::string& property_long_name,
                                   int last_status,
                                   int last_data1) {
  if (midi_channels_.find(std::make_pair(last_status, last_data1)) != midi_channels_.end()) {
    sw_info("Midi Channels {} {} is already a property (is currently named {})",
            std::to_string(last_status),
            std::to_string(last_data1),
            midi_channels_.find(std::make_pair(last_status, last_data1))->second);
    return false;
  }
  midi_channels_[std::make_pair(last_status, last_data1)] = property_long_name;
  std::string prop_name(std::to_string(last_status) + "-" + std::to_string(last_data1));
  midi_values_[property_long_name] = last_data2_;
  if (unused_props_specs_.find(prop_name) == unused_props_specs_.end()) {
    MidiPropertyContext midi_property_context;
    midi_property_context.port_midi_source_ = this;
    midi_property_context.property_long_name_ = property_long_name;
    midi_property_contexts_[property_long_name] = midi_property_context;
    prop_ids_[property_long_name] = pmanage<&property::PBag::make_int>(
        prop_name,
        nullptr,
        [this, property_long_name]() { return midi_values_[property_long_name]; },
        property_long_name,
        property_long_name,
        0,
        0,
        127);
  } else {
    prop_ids_[property_long_name] = unused_props_specs_[prop_name];
    unused_props_specs_.erase(prop_name);
  }
  return true;
}

}  // namespace quiddities
}  // namespace switcher
