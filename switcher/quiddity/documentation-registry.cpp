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

#include "./documentation-registry.hpp"

namespace switcher {
namespace quiddity {

std::unique_ptr<DocumentationRegistry> DocumentationRegistry::instance_{nullptr};

bool DocumentationRegistry::register_doc(const std::string& quiddity_type,
                                         const quiddity::Doc& doc) {
  auto res = doc_registry_.try_emplace(quiddity_type, doc);
  return res.second;
}

bool DocumentationRegistry::register_type_from_kind(const std::string& kind,
                                                    const std::string& quiddity_type) {
  type_from_kind_registry_.emplace(std::make_pair<>(kind, quiddity_type));
  return true;
}

std::string DocumentationRegistry::get_type_from_kind(const std::string& kind) const {
  auto quiddity_type = type_from_kind_registry_.find(kind);
  if (quiddity_type != type_from_kind_registry_.end()) return quiddity_type->second;
  return std::string();
}

const std::map<std::string, quiddity::Doc>& DocumentationRegistry::get_docs() const {
  return doc_registry_;
}

}  // namespace quiddity
}  // namespace switcher
