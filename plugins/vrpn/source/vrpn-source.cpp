/**
 * This file is part of switcher-vrpn.
 *
 * switcher-vrpn is free software; you can redistribute it and/or
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

#include "./vrpn-source.hpp"

namespace switcher {
namespace quiddities {
namespace vrpn {

SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(
    VRPNSource,
    "vrpnsrc",
    "VRPN Source Client",
    "Plugin to connect to a VRPN server and share its controls through shmdata and/or properties.",
    "LGPL",
    "François Ubald Brien");

const std::string VRPNSource::kConnectionSpec(R"(
{
"writer":
  [
    {
      "label": "vrpn",
      "description": "VRPN stream",
      "can_do": ["application/vrpn"]
    }
  ]
}
)");

const unsigned int VRPNSource::vrpnLoopInterval{16};

VRPNSource::~VRPNSource() { destroyed_ = true; }

VRPNSource::VRPNSource(quiddity::Config&& conf)
    : Quiddity(std::forward<quiddity::Config>(conf), {kConnectionSpec}), Startable(this) {
  // Create the hostname property
  host_id_ = pmanage<&property::PBag::make_string>("host",
                                                         [this](const std::string& val) {
                                                           host_ = val;
                                                           return true;
                                                         },
                                                         [this]() { return host_; },
                                                         "Host",
                                                         "VRPN server hostname or IP address.",
                                                         host_);

  // Create the port property
  port_id_ = pmanage<&property::PBag::make_int>("port",
                                                      [this](int val) {
                                                        port_ = val;
                                                        return true;
                                                      },
                                                      [this]() { return port_; },
                                                      "Port",
                                                      "Port that the VRPN client will connect to.",
                                                      port_,
                                                      1,
                                                      65536);

  // Create the advanced configuration group
  pmanage<&property::PBag::make_group>(
      "advanced", "Advanced configuration", "Advanced configuration");

  // Add debug option to advanced configuration group
  pmanage<&property::PBag::make_parented_bool>("debug",
                                                     "advanced",
                                                     [this](bool val) {
                                                       debug_ = val;
                                                       return true;
                                                     },
                                                     [this]() { return debug_; },
                                                     "Debug",
                                                     "Debug values to console",
                                                     debug_);
}

/**
 * @thread switcher
 */
InfoTree::ptr VRPNSource::on_saving() {
  InfoTree::ptr devicesTree = InfoTree::make();

  {
    // Sync with vrpn thread before accessing connection/devices
    std::lock_guard<std::mutex> _(vrpnMutex_);
    unsigned int index = 0;
    for (auto& device : devices_) {
      devicesTree->graft(std::to_string(index), device.second->getTree());
      ++index;
    }
    devicesTree->make_array(true);
  }

  InfoTree::ptr customData = InfoTree::make();
  customData->graft(".devices", devicesTree);
  return customData;
}

/**
 * @thread switcher
 */
void VRPNSource::on_loading(InfoTree::ptr&& tree) {
  if (tree->empty()) {
    return;
  }

  // Sync with vrpn thread before accessing connection/devices
  std::lock_guard<std::mutex> _(vrpnMutex_);

  InfoTree::ptr devicesTree = tree->get_tree(".devices");
  if (devicesTree->is_array()) {
    std::list<std::string> keys = devicesTree->get_child_keys(".");
    for (auto key : keys) {
      InfoTree::ptr deviceTree = devicesTree->get_tree(key);
      std::string type = deviceTree->branch_get_value("type");
      std::string name = deviceTree->branch_get_value("name");
      std::string uri = deviceTree->branch_get_value("uri");
      std::string id = type + "/" + uri;

      if (type == "vrpn_Analog") {
        Any numChannelsAny = deviceTree->branch_get_value("numChannels");
        createAnalogDevice(
            id, name, uri, numChannelsAny.is_null() ? 0 : numChannelsAny.copy_as<int>());

      } else if (type == "vrpn_Button") {
        Any numButtonsAny = deviceTree->branch_get_value("numButtons");
        createButtonDevice(
            id, name, uri, numButtonsAny.is_null() ? 0 : numButtonsAny.copy_as<int>());

      } else if (type == "vrpn_Tracker") {
        createTrackerDevice(id, name, uri);
      }
    }
  }
}

/**
 * @thread switcher
 */
bool VRPNSource::start() {
  if (host_.empty()) {
    sw_error("Host is required.");
    return false;
  }

  // Sync with vrpn thread before accessing connection/devices
  std::lock_guard<std::mutex> _(vrpnMutex_);

  // Initialize the shmdata writer
  shmDataWriter_ = std::make_unique<shmdata::Writer>(
      this, claw_.get_shmpath_from_writer_label("vrpn"), 1, "application/vrpn");
  if (!shmDataWriter_.get()) {
    sw_error("VRPN source failed to initialize its shmdata");
    shmDataWriter_.reset(nullptr);
    return false;
  }

  // Create the connection and check its "okay-ness"
  connection_ = std::make_unique<VRPNClientConnection>(host_ + ":" + std::to_string(port_));
  if (!connection_->raw()->doing_okay()) {
    sw_error("VRPN source connection is not doing okay (whatever that means).");
    return false;
  }

  // Disable some properties while we're on
  pmanage<&property::PBag::disable>(host_id_, Startable::disabledWhenStartedMsg);
  pmanage<&property::PBag::disable>(port_id_, Startable::disabledWhenStartedMsg);

  for (auto& device : devices_) {
    device.second->start(connection_.get());
  }

  // Register a callback handler for *all* messages going through the connection
  connection_->raw()->register_handler(vrpn_ANY_TYPE, handleMessage, this, vrpn_ANY_SENDER);
  loopTask_ = std::make_unique<PeriodicTask<>>([this]() { this->loop(); },
                                               std::chrono::milliseconds(vrpnLoopInterval));

  sw_debug("Started VRPN source connection");

  return true;
}

/**
 * @thread switcher
 */
bool VRPNSource::stop() {
  sw_debug("Stopping VRPN source connection");

  loopTask_.reset(nullptr);

  // Sync with vrpn thread before accessing connection/devices
  std::lock_guard<std::mutex> _(vrpnMutex_);
  for (auto& device : devices_) {
    device.second->stop();
  }

  // Reset the connection, unregistering the handler before, just in case...
  connection_->raw()->unregister_handler(vrpn_ANY_TYPE, handleMessage, this, vrpn_ANY_SENDER);
  connection_.reset(nullptr);

  // Reset shmDataWriter
  shmDataWriter_.reset(nullptr);

  // Re-enable properties
  pmanage<&property::PBag::enable>(host_id_);

  return true;
}

/**
 * @thread vrpn
 */
void VRPNSource::loop() {
  std::lock_guard<std::mutex> _(vrpnMutex_);

#ifdef DEBUG
  // Whatever that means
  if (!connection_->raw()->doing_okay()) {
    sw_debug("VRPN source connection is not doing okay.");
  }

  if (!connection_->raw()->connected()) {
    sw_debug("VRPN source disconnected.");
  }
#endif

  connection_->raw()->mainloop();
  for (auto& device : devices_) {
    device.second->loop();
  }
}

/**
 * @thread switcher + vrpn
 * @unsafe
 */
void VRPNSource::createAnalogDevice(const std::string& id,
                                    const std::string& name,
                                    const std::string& uri,
                                    int numChannels) {
  // Analog device creation
  std::string groupName = name + "-analog";

  // Create a group to hold the channel properties
  pmanage<&property::PBag::make_group>(
      groupName, "\"" + name + "\" Device Analog Channels", "Device Analog Channels Properties");

  // Create the device
  std::unique_ptr<AnalogSourceDevice> device = std::make_unique<AnalogSourceDevice>(
      name,
      uri,
      numChannels,
      [this, groupName](Property<double>* property,
                        std::string p_id,
                        std::string p_name,
                        std::string p_description,
                        double p_value,
                        double p_min,
                        double p_max) {
        return pmanage<&property::PBag::make_parented_double>(
            p_id,
            groupName,
            nullptr,
            [this, property]() {
              // Called from switcher's thread OR vrpnPropertyNotify_'s thread unfortunately
              // That's the reason we need an std::mutex
              std::lock_guard<std::mutex> _(vrpnMutex_);
              return property->value();
            },
            p_name,
            p_description,
            p_value,
            p_min,
            p_max);
      },
      [this](property::prop_id_t propId) {
        // ThreadWrapped notify, because it is called from the vrpn thread
        vrpnPropertyNotify_->run_async(
            [this, propId]() { pmanage<&property::PBag::notify>(propId); });
      });

  if (is_started()) {
    device->start(connection_.get());
  }

  devices_.emplace(id, std::move(device));
}

/**
 * @thread switcher + vrpn
 * @unsafe
 */
void VRPNSource::createButtonDevice(const std::string& id,
                                    const std::string& name,
                                    const std::string& uri,
                                    int numButtons) {
  // Button device creation
  std::string groupName = name + "-button";

  // Create a group to hold the button properties
  pmanage<&property::PBag::make_group>(
      groupName, "\"" + name + "\" Device Buttons", "Device Buttons Properties");

  // Create the device
  std::unique_ptr<ButtonSourceDevice> device = std::make_unique<ButtonSourceDevice>(
      name,
      uri,
      numButtons,
      [this, groupName](Property<bool>* property,
                        std::string p_id,
                        std::string p_name,
                        std::string p_description,
                        bool p_value) {
        return pmanage<&property::PBag::make_parented_bool>(
            p_id,
            groupName,
            nullptr,
            [this, property]() {
              // Called from switcher's thread OR
              // vrpnPropertyNotify_'s thread unfortunately
              // That's the reason we need an std::mutex
              std::lock_guard<std::mutex> _(vrpnMutex_);
              return property->value();
            },
            p_name,
            p_description,
            p_value);
      },
      [this](property::prop_id_t propId) {
        // ThreadWrapped notify, because it is called from the
        // vrpn thread
        vrpnPropertyNotify_->run_async(
            [this, propId]() { pmanage<&property::PBag::notify>(propId); });
      });

  if (is_started()) {
    device->start(connection_.get());
  }

  devices_.emplace(id, std::move(device));
}

/**
 * @thread switcher + vrpn
 * @unsafe
 */
void VRPNSource::createTrackerDevice(const std::string& id,
                                     const std::string& name,
                                     const std::string& uri) {
  // Tracker device creation
  std::string groupName = name + "-tracker";

  // Create a group to hold the button properties
  pmanage<&property::PBag::make_group>(
      groupName, "\"" + name + "\" Device Trackers", "Device Trackers Properties");

  // Create the device
  std::unique_ptr<TrackerSourceDevice> device =
      std::make_unique<TrackerSourceDevice>(name, uri, [this](property::prop_id_t propId) {
        pmanage<&property::PBag::notify>(propId);
      });

  if (is_started()) {
    device->start(connection_.get());
  }

  devices_.emplace(id, std::move(device));
}

/**
 * Guarded by the loop
 * @thread vrpn
 * @unsafe
 */
void VRPNSource::watchDevice(const std::string& senderName, const std::string& typeName) {
  // typeName is in the form of "vrpn_Device Message Type", we only want the vrpn_Device part here
  std::string deviceType = typeName.substr(0, typeName.find_first_of(' '));
  std::string connectionString = senderName + "@" + host_ + ":" + std::to_string(port_);
  // Make up an Id for us to map devices to
  std::string id = deviceType + "/" + connectionString;

  auto search = devices_.find(id);
  if (search == devices_.end()) {
    if (deviceType == "vrpn_Analog") {
      createAnalogDevice(id, senderName, connectionString);
    } else if (deviceType == "vrpn_Button") {
      createButtonDevice(id, senderName, connectionString);
    } else if (deviceType == "vrpn_Tracker") {
      createTrackerDevice(id, senderName, connectionString);
    }
  }
}

/**
 * Guarded by the loop
 * @thread vrpn
 * @unsafe
 */
int VRPNSource::handleMessage(void* userData, vrpn_HANDLERPARAM p) {
  auto* context = static_cast<VRPNSource*>(userData);

  // FIXME: This stops us from trying to handle connection drop messages on destruction
  if (context->destroyed_) {
    return 0;
  }

  std::string senderName = context->connection_->raw()->sender_name(p.sender);
  std::string typeName = context->connection_->raw()->message_type_name(p.type);

  context->watchDevice(senderName, typeName);

  std::vector<unsigned char> buffer((size_t)(
      sizeof(uint32_t) + senderName.length() + sizeof(uint32_t) + typeName.length() +
      sizeof(p.msg_time.tv_sec) + sizeof(p.msg_time.tv_usec) + sizeof(uint32_t) + p.payload_len));

  // BUFFER
  auto buffer_ptr = buffer.data();

  // SENDER
  *((uint32_t*)buffer_ptr) = htonl((uint32_t)senderName.length());
  buffer_ptr += sizeof(uint32_t);
  memcpy(buffer_ptr, senderName.data(), senderName.length());
  buffer_ptr += senderName.length();

  // TYPE
  *((uint32_t*)buffer_ptr) = htonl((uint32_t)typeName.length());
  buffer_ptr += sizeof(uint32_t);
  memcpy(buffer_ptr, typeName.data(), typeName.length());
  buffer_ptr += typeName.length();

  // TIME
  *((uint32_t*)buffer_ptr) = htonl((uint32_t)p.msg_time.tv_sec);
  buffer_ptr += sizeof(p.msg_time.tv_sec);
  *((uint32_t*)buffer_ptr) = htonl((uint32_t)p.msg_time.tv_usec);
  buffer_ptr += sizeof(p.msg_time.tv_usec);

  // PAYLOAD
  *((uint32_t*)buffer_ptr) = htonl((uint32_t)p.payload_len);
  buffer_ptr += sizeof(p.payload_len);
  memcpy(buffer_ptr, p.buffer, (uint32_t)p.payload_len);

  // DEBUG
  if (context->debug_) {
    context->sw_debug("VRPNSource >>> Sender: {} Type: {} Length: {} Payload: {}",
                      senderName,
                      typeName,
                      std::to_string(buffer.size()),
                      std::to_string(p.payload_len));

    std::stringstream ss;
    ss << "VRPNSource >>> ";
    for (auto const& value : buffer) {
      ss << std::hex << std::setfill('0') << std::setw(2) << (int)value << " ";
    }
    context->sw_debug(ss.str());
  }

  // WRITE TO SHMDATA
  context->shmDataWriter_->writer<&::shmdata::Writer::copy_to_shm>(buffer.data(),
                                                                         buffer.size());
  context->shmDataWriter_->bytes_written(buffer.size());

  return 0;
}

}  // namespace vrpn
}  // namespace quiddities
}  // namespace switcher
