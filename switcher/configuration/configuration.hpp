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

#ifndef __SWITCHER_CONFIGURATION_H__
#define __SWITCHER_CONFIGURATION_H__

#include <filesystem>
#include <sstream>
#include <vector>

#include "../infotree/information-tree.hpp"
#include "../utils/bool-log.hpp"

namespace fs = std::filesystem;

namespace switcher {

class Configuration {
 public:
  /**
   * Construct a Configuration. Callbacks are triggered by the constructor, the from_file and
   * from_tree methods.
   *
   * @param on_reloaded function to call when a new configuration has been loaded
   *
   **/
  Configuration(std::function<void()> on_reloaded);

  /**
   * Load a new configuration from a file. If required configurations are missing,
   * they will be set with default values.
   *
   * \param file_path Path to the configuration file.
   *
   * \return A BoolLog set to True if new configuration has been loaded, false with the 
   * explanation message otherwise.
   */
  BoolLog from_file(const std::string& file_path);

  /**
   * Load a new configuration from an InfoTree. If required configurations are missing,
   * they will be set with default values.
   *
   * \param tree Tree containing a configuration.
   *
   * \return True if new configuration has been loaded, false otherwise.
   */
  bool from_tree(const InfoTree* tree);

  /**
   * Get the Information tree with the configuration.
   **/
  const InfoTree::ptr get();

  /**
   * Retrieves a vector of configured path(s) for the `extraConfig` key.
   *
   * Available configuration keys:
   *
   * "extraConfig": "/path/to/extra_config_file.json"
   *
   *  or
   *
   * "extraConfig": [
   *   "/path/to/extra_config_1.json",
   *   "/path/to/extra_config_2.json",
   * ]
   *
   * @return A vector of any extra configuration paths
   */
  std::vector<fs::path> list_extra_configs();

  /**
   * Gets the default path of the global configuration file
   * @return The path of the global configuration file
   */
  static fs::path get_default_global_path();

  /**
   * Reads the content of an extra configuration file
   *
   * @param name The name of the extra configuration file
   *
   * @return The content of an extra configuration file in the BoolLog
   * message if success, or the error message in case of error
   */
  BoolLog get_extra_config(const std::string& name);

  /**
   * Get a value in the configuration
   *
   * @param branch_path The path of the value in the configuration
   *
   * @return the value
   */
  Any get_value(const std::string& branch_path) const;

  /**
   * Set a value in the configuration
   *
   * @param key The configuration key
   * @param value The value to pair with the key
   *
   * @return A boolean asserting how the pairing went
   */
  template <typename T>
  bool set_value(const std::string& key, T value) {
    return configuration_->vgraft(key, value);
  };

 private:
  void set_defaults();

  InfoTree::ptr configuration_{};
  std::function<void()> on_reloaded_;
  static const int kMaxConfigurationFileSize;
};
}  // namespace switcher

#endif
