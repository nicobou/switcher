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

#ifndef __SWITCHER_PLUGIN_H__
#define __SWITCHER_PLUGIN_H__

#include <gmodule.h>
#include <memory>
#include <string>

#include "../utils/bool-log.hpp"
#include "./config.hpp"

namespace switcher {
namespace quiddity {
class Quiddity;

// the types of the class factories for quiddity plugins
typedef Quiddity* create_t(quiddity::Config&&);
typedef void destroy_t(Quiddity*);
typedef const char* get_type_t();

class PluginLoader {
 public:
  typedef std::shared_ptr<PluginLoader> ptr;
  PluginLoader();
  ~PluginLoader();
  PluginLoader(const PluginLoader&) = delete;
  PluginLoader& operator=(const PluginLoader&) = delete;

  BoolLog load(const std::string& filename);
  BoolLog close();
  std::string get_class_name() const;

  create_t* create_{nullptr};
  destroy_t* destroy_{nullptr};
  get_type_t* get_type_{nullptr};

 private:
  GModule* module_{nullptr};
  std::string class_name_{};
};
}  // namespace quiddity
}  // namespace switcher

#endif
