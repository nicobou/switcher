/*
 * This file is part of switcher.
 *
 * switcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * switcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with switcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "./key-val-serializer.hpp"
#include <iostream>
#include <iterator>
#include <string>

namespace switcher {
namespace infotree {
namespace keyval {

typedef struct {
  std::list<std::string> path_{};
  std::string result_{};
} BasicSerializerData;

std::string path_to_string(std::list<std::string> path) {
  std::stringstream result;
  std::copy(path.begin(), path.end(), std::ostream_iterator<std::string>(result, "."));
  return result.str();
}

bool on_visiting_node(std::string key, const InfoTree::ptrc node, bool, BasicSerializerData* data) {
  data->path_.push_back(key);
  auto value = node->read_data();
  if (value.not_null())
    data->result_.append("." + infotree::keyval::path_to_string(data->path_) + " " +
                         Any::to_string(value) + "\n");
  return true;  // always true since we want preorder_tree_walk continue to visit sibling(s)
}

bool on_node_visited(std::string, const InfoTree::ptrc, bool, BasicSerializerData* data) {
  data->path_.pop_back();
  return true;  // always true since we want preorder_tree_walk continue to visit sibling(s)
}

std::string serialize(InfoTree::ptrc tree) {
  BasicSerializerData data;
  InfoTree::preorder_tree_walk(tree,
                               std::bind(infotree::keyval::on_visiting_node,
                                         std::placeholders::_1,
                                         std::placeholders::_2,
                                         std::placeholders::_3,
                                         &data),
                               std::bind(infotree::keyval::on_node_visited,
                                         std::placeholders::_1,
                                         std::placeholders::_2,
                                         std::placeholders::_3,
                                         &data));
  return data.result_;
}

InfoTree::ptr deserialize(const std::string& serialized) {
  InfoTree::ptr tree = InfoTree::make();
  std::istringstream ss(serialized);
  std::string line;
  while (std::getline(ss, line)) {
    std::istringstream line_ss(line);
    std::string absolute_key;
    while (std::getline(line_ss, absolute_key, ' ') && absolute_key.empty()) {
    }
    std::string value;
    while (std::getline(line_ss, value, ' ') && value.empty()) {
    }
    // getting the rest of the value, if it contains space in it
    std::string val_cont;
    std::getline(line_ss, val_cont);
    if (!val_cont.empty()) value += ' ' + val_cont;
    if (!absolute_key.empty() && !value.empty()) tree->graft(absolute_key, InfoTree::make(value));
  }
  return tree;
}

}  // namespace keyval
}  // namespace infotree
}  // namespace switcher
