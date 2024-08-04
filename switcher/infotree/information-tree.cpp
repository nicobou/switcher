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

#include "./information-tree.hpp"
#include <algorithm>
#include <iostream>
#include "../utils/scope-exit.hpp"
#include "../utils/string-utils.hpp"
#include "./json-serializer.hpp"

namespace switcher {

InfoTree::ptr InfoTree::make() {
  std::shared_ptr<InfoTree> tree;  // can't use make_shared because ctor is private
  tree.reset(new InfoTree());
  tree->me_ = tree;
  return tree;
}

InfoTree::ptr InfoTree::merge(InfoTree::ptrc first, InfoTree::ptrc second) {
  auto res = InfoTree::copy(first);
  std::list<std::string> path;

  if (second)
    preorder_tree_walk(second,
                       [&](const std::string& name, InfoTree::ptrc tree, bool is_array_element) {
                         std::string key;
                         for (auto& it : path) {
                           key += "." + it;
                         }
                         if (is_array_element) res->tag_as_array(key, true);
                         path.push_back(name);
                         key += "." + name;
                         auto value = tree->read_data();
                         if (value.not_null()) res->graft(key, make(value));
                         return true;
                       },
                       [&](const std::string&, InfoTree::ptrc, bool) {
                         path.pop_back();
                         return true;
                       });
  return res;
}

InfoTree::ptr InfoTree::copy(InfoTree::ptrc tree) {
  auto res = InfoTree::make();
  std::list<std::string> path;

  if (tree) {
    auto root_value = tree->get_value();
    if (root_value.not_null()) {
      res->set_value(root_value);
    }
    preorder_tree_walk(tree,
                       [&](const std::string& name, InfoTree::ptrc tree, bool is_array_element) {
                         std::string key{"."};
                         for (auto& it : path) {
                           key += it + ".";
                         }
                         if (is_array_element) {
                           res->tag_as_array(key, true);
                         }
                         path.push_back(name);
                         key += "." + name;
                         auto value = tree->read_data();
                         if (value.not_null())
                           res->graft(key, make(value));
                         else
                           res->graft(key, make());
                         return true;
                       },
                       [&](const std::string&, InfoTree::ptrc, bool) {
                         path.pop_back();
                         return true;
                       });
  }
  return res;
}

std::list<Any> InfoTree::collect_values(InfoTree::ptrc tree,
                                        collect_predicate_t predicate,
                                        bool continue_search_in_siblings) {
  std::list<Any> res;
  preorder_tree_walk(tree,
                     [&](const std::string& name, InfoTree::ptrc node, bool) {
                       if (predicate(name, node)) {
                         res.push_back(Any(node->read_data()));
                         return continue_search_in_siblings;
                       }
                       return true;
                     },
                     [&](const std::string&, InfoTree::ptrc, bool) { return true; });
  return res;
}

InfoTree::ptr InfoTree::make(const char* data) { return make(std::string(data)); }

InfoTree::ptr InfoTree::make_null() {
  std::shared_ptr<InfoTree> tree;
  return tree;
}

void InfoTree::preorder_tree_walk(InfoTree::ptrc tree,
                                  InfoTree::OnNodeFunction on_visiting_node,
                                  InfoTree::OnNodeFunction on_node_visited) {
  std::unique_lock<std::recursive_mutex> lock(tree->mutex_);
  for (auto& it : tree->children_) {
    if (!on_visiting_node(it.first, it.second.get(), tree->is_array_) && it.second.get()) break;
    preorder_tree_walk(it.second.get(), on_visiting_node, on_node_visited);
    on_node_visited(it.first, it.second.get(), tree->is_array_);
    }
}

InfoTree::InfoTree(const Any& data) : data_(data) {}

InfoTree::InfoTree(Any&& data) : data_(data) {}

bool InfoTree::empty() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return children_.empty() && data_.is_null();
}

bool InfoTree::is_leaf() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return children_.empty();
}

bool InfoTree::is_array() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return is_array_;
}

bool InfoTree::has_data() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return !data_.is_null();
}

Any InfoTree::get_value() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return data_;
}

const Any& InfoTree::read_data() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return data_;
}

void InfoTree::set_value(const Any& data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  data_ = data;
}

void InfoTree::set_value(const char* data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  data_ = std::string(data);
}

void InfoTree::set_value(std::nullptr_t ptr) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  data_ = ptr;
}

bool InfoTree::branch_is_leaf(const std::string& path) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return children_.empty();
  auto found = get_node(path);
  if (nullptr != found.first) return (*found.first)[found.second].second->children_.empty();
  return false;
}

bool InfoTree::branch_is_array(const std::string& path) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return children_.empty();
  auto found = get_node(path);
  if (nullptr != found.first) return (*found.first)[found.second].second->is_array_;
  return false;
}

bool InfoTree::branch_has_data(const std::string& path) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return data_.not_null();
  auto found = get_node(path);
  if (nullptr != found.first) return (*found.first)[found.second].second->data_.not_null();
  return false;
}

Any InfoTree::branch_get_value(const std::string& path) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return data_;
  auto found = get_node(path);
  if (nullptr != found.first) return (*found.first)[found.second].second->data_;
  Any res;
  return res;
}

InfoTree::ptr InfoTree::get_copy() const { return copy(this); }

InfoTree::ptr InfoTree::branch_get_copy(const std::string& path) const {
  if (path_is_root(path)) return copy(this);
  auto found = get_node(path);
  if (nullptr == found.first) return nullptr;
  return copy((*found.first)[found.second].second.get());
}

bool InfoTree::branch_set_value(const std::string& path, const Any& data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return data_ = data;
  auto found = get_node(path);
  if (nullptr != found.first) {
    (*found.first)[found.second].second->data_ = data;
    return true;
  }
  return false;
}

bool InfoTree::branch_set_value(const std::string& path, const char* data) {
  return branch_set_value(path, std::string(data));
}

bool InfoTree::branch_set_value(const std::string& path, std::nullptr_t ptr) {
  return branch_set_value(path, Any(ptr));
}

std::pair<bool, InfoTree::children_t::size_type> InfoTree::get_child_index(
    const std::string& key) const {
  auto found =
      std::find_if(children_.begin(), children_.end(), [key](const InfoTree::child_type& s) {
        return (0 == s.first.compare(key));
      });
  return std::make_pair(children_.end() != found,
                        children_.end() != found ? found - children_.begin() : 0);
}

InfoTree::ptr InfoTree::prune(const std::string& path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto found = get_node(path);
  if (nullptr != found.first) {
    On_scope_exit { found.first->erase(found.first->begin() + found.second); };
    return (*found.first)[found.second].second;
  }
  return InfoTree::make_null();
}

InfoTree::ptr InfoTree::get_tree(const std::string& path) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return me_.lock();
  auto found = get_node(path);
  if (nullptr != found.first) return (*found.first)[found.second].second;
  // not found
  return make();
}

InfoTree::GetNodeReturn InfoTree::get_node(const std::string& path) const {
  std::istringstream iss(path);
  return get_next(iss, nullptr, 0);
}

InfoTree::GetNodeReturn InfoTree::get_next(std::istringstream& path,
                                           InfoTree::children_t* parent_vector,
                                           children_t::size_type index) const {
  std::string child_key;
  if (!std::getline(path, child_key, '.')) return std::make_pair(parent_vector, index);
  if (child_key.empty()) return get_next(path, parent_vector, index);

  auto child_index = get_child_index(child_key);
  if (!child_index.first) return std::make_pair(nullptr, 0);
  return children_[child_index.second].second->get_next(path, &children_, child_index.second);
}

bool InfoTree::graft(const std::string& where, InfoTree::ptr tree) {
  if (!tree) return false;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::istringstream iss(where);
  return !graft_next(iss, this, tree);
}

bool InfoTree::graft_next(std::istringstream& path, InfoTree* tree, InfoTree::ptr leaf) {
  std::string child;
  if (!std::getline(path, child, '.')) return true;
  if (child.empty())  // in case of two or more consecutive dots
    return graft_next(path, tree, leaf);
  auto index = tree->get_child_index(child);
  if (index.first) {
    if (graft_next(path, tree->children_[index.second].second.get(), leaf))
      // graft on already existing child
      tree->children_[index.second].second =
          leaf;  // replacing the previously empy tree with the one to graft
  } else {
    InfoTree::ptr child_node = make();
    tree->children_.emplace_back(child, child_node);
    if (graft_next(path, child_node.get(), leaf))  // graft on already existing child
    {
      // replacing empty tree for replacement by leaf
      tree->children_.pop_back();
      tree->children_.emplace_back(child, leaf);
    }
  }
  return false;
}

bool InfoTree::tag_as_array(const std::string& path, bool is_array) {
  InfoTree::ptr tree = InfoTree::get_tree(path);
  if (!(bool)tree) return false;
  tree->is_array_ = is_array;
  return true;
}

bool InfoTree::make_array(bool is_array) {
  is_array_ = is_array;
  return true;
}

std::string InfoTree::escape_dots(const std::string& str) {
  return stringutils::replace_char(str, '.', "__DOT__");
}

std::string InfoTree::unescape_dots(const std::string& str) {
  return stringutils::replace_string(str, "__DOT__", ".");
}

std::vector<std::string> InfoTree::get_child_keys(const std::string& path) const {
  std::vector<std::string> res;
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // if root is asked
  if (path_is_root(path)) {
    res.resize(children_.size());
    std::transform(children_.cbegin(), children_.cend(), res.begin(), [](const child_type& child) {
      return child.first;
    });
    return res;
  }
  // else looking into childrens
  auto found = get_node(path);
  if (nullptr != found.first) {
    res.resize((*found.first)[found.second].second->children_.size());
    std::transform((*found.first)[found.second].second->children_.cbegin(),
                   (*found.first)[found.second].second->children_.cend(),
                   res.begin(),
                   [](const child_type& child) { return child.first; });
  }
  return res;
}

std::list<std::string> InfoTree::copy_leaf_values(const std::string& path) const {
  std::list<std::string> res;
  InfoTree::ptr tree;
  {  // finding the node
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (path_is_root(path))
      tree = me_.lock();
    else {
      auto found = get_node(path);
      if (nullptr == found.first) return res;
      tree = (*found.first)[found.second].second;
    }
  }
  preorder_tree_walk(tree.get(),
                     [&res](std::string /*key*/, InfoTree::ptrc node, bool /*is_array_element*/) {
                       if (node->is_leaf()) res.push_back(Any::to_string(node->read_data()));
                       return true;
                     },
                     [](std::string, InfoTree::ptrc, bool) { return true; });
  return res;
}

InfoTree::ptrc InfoTree::get_subtree(InfoTree::ptrc tree, const std::string& path) {
  std::lock_guard<std::recursive_mutex> lock(tree->mutex_);
  auto found = tree->get_node(path);
  if (nullptr == found.first) return nullptr;
  return (*found.first)[found.second].second.get();
}

std::string InfoTree::serialize_json(const std::string& path) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (path_is_root(path)) return infotree::json::serialize(me_.lock().get(), true);
  auto found = get_node(path);
  if (nullptr == found.first) return "null";
  auto tree = (*found.first)[found.second].second;
  if (!tree) return "null";
  return infotree::json::serialize(tree.get(), true);
}

bool InfoTree::path_is_root(const std::string& path) { return (path == ".") || (path == ".."); }

bool InfoTree::for_each_in_array(const std::string& path, std::function<void(InfoTree*)> fun) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // finding the parent node of the array
  auto children = children_;
  if (path_is_root(path)) {
    if (!is_array_) return false;
  } else {
    auto found = get_node(path);
    if (!found.first) return false;
    auto& tree = (*found.first)[found.second].second;
    if (!tree->is_array_) return false;
    children = tree->children_;
  }

  // apply function to array elements
  for (auto& child : children) {
    fun(child.second.get());
  }
  return true;
}

bool InfoTree::cfor_each_in_array(const std::string& path,
                                  std::function<void(const InfoTree*)> fun) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // finding the parent node of the array
  auto children = children_;
  if (path_is_root(path)) {
    if (!is_array_) return false;
  } else {
    auto found = get_node(path);
    if (!found.first) return false;
    auto& tree = (*found.first)[found.second].second;
    if (!tree->is_array_) return false;
    children = tree->children_;
  }

  // apply function to array elements
  for (auto& child : children) {
    fun(child.second.get());
  }
  return true;
}

std::string InfoTree::json() const { return serialize_json("."); }
}  // namespace switcher
