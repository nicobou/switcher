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

/**
 * @file  information-tree.h
 *
 * @brief tree data structure for storing, formating and serializing informations
 *
 * The information tree is largely inspired from the boost' property tree.
 * It provides a data structure that stores an arbitrarily deeply nested
 * tree of values, indexed at each level by some key. Each node of the
 * tree stores its own value, plus an ordered list of its subnodes and their keys.
 * The tree allows easy access to any of its nodes by means of a path,
 * which is a concatenation of multiple keys.
 *
 */

#ifndef __SWITCHER_INFORMATION_TREE_H__
#define __SWITCHER_INFORMATION_TREE_H__

#include <string>
#include <algorithm>
#include <list>
#include <memory>
#include <type_traits>
#include <mutex>
#include "./any.hpp"

namespace switcher {
namespace data {
class Tree {
 public:
  typedef std::shared_ptr<Tree> ptr;
  typedef std::shared_ptr<const Tree> ptrc;
  typedef std::pair<std::string, Tree::ptr> child_type;
  typedef std::list<child_type> childs_t;
  typedef std::function <void(const std::string & name,
                              const Tree::ptrc tree,
                              bool is_array_element)> OnNodeFunction;
  typedef std::pair <Tree::childs_t,
                     Tree::childs_t::iterator> GetNodeReturn;
  Tree() {}
  explicit Tree(const Any &data);

  //const methods
  bool is_leaf() const;
  bool is_array() const;
  bool has_data() const;
  const Any read_data () const;
  bool is_leaf(const std::string & path) const;
  bool is_array(const std::string & path) const;
  bool has_data(const std::string & path) const;
  const Any read_data (const std::string & path) const;
  // get child keys - returning a newly allocated container
  template<template<class T, class = std::allocator<T>>
           class Container = std::list>
      Container<std::string>
      get_child_keys(const std::string path) const {
    Container<std::string> res;
    std::unique_lock<std::mutex> lock(mutex_);
    auto found = get_node(path);
    if (!found.first.empty()) {
      res.resize(found.second->second->childrens_.size());
      std::transform(found.second->second->childrens_.cbegin(),
                     found.second->second->childrens_.cend(),
                     res.begin(),
                     [](const child_type & child) {
                       return child.first;
                     });
    }
    return res;
  }
  // get child key in place, use with std::insert_iterator
  template<typename Iter>
  void get_child_keys(const std::string path, Iter pos) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto found = get_node(path);
    if (!found.first.empty()) {
      std::transform(found.second->second->childrens_.begin(),
                     found.second->second->childrens_.end(),
                     pos,
                     [](const child_type & child) {
                       return child.first;
                     });
    }
  }

  //static version of const methods, for being used with invoke_info_tree
  static void preorder_tree_walk(Tree::ptrc tree,
                                 Tree::OnNodeFunction on_visiting_node,
                                 Tree::OnNodeFunction on_node_visited);
  static bool is_leaf(Tree::ptrc tree);
  static bool is_array(Tree::ptrc tree);
  static bool has_data(Tree::ptrc tree);
  static const Any read_data (Tree::ptrc tree);
  static bool is_leaf(Tree::ptrc tree, const std::string & path);
  static bool is_array(Tree::ptrc tree, const std::string & path);
  static bool has_data(Tree::ptrc tree, const std::string & path);
  static const Any read_data (Tree::ptrc tree, const std::string & path);
  // get child keys - returning a newly allocated container
  template<template<class T, class = std::allocator<T>>
           class Container = std::list>
      static Container<std::string> get_child_keys(Tree::ptrc tree, const std::string path) {
    return tree->get_child_keys<Container>(path);
  }
  // get child key in place, use with std::insert_iterator
  template<typename Iter>
  static void get_child_keys(Tree::ptrc tree, const std::string path, Iter pos) {
    tree->get_child_keys<Iter> (path, pos);
  }
  
  //Tree modifications:
  Any get_data();
  void set_data(const Any & data);
  void set_data(const char *data);
  void set_data(std::nullptr_t ptr);
  Any get_data(const std::string & path);
  bool set_data(const std::string & path, const Any & data);
  bool set_data(const std::string & path, const char *data);
  bool set_data(const std::string & path, std::nullptr_t ptr);
  // graft will create the path and graft the tree,
  // or remove old one and replace will the new tree
  bool graft(const std::string & path, Tree::ptr);
  // return empty tree if nothing can be pruned
  Tree::ptr prune(const std::string & path);
  // get but not remove
  Tree::ptr get(const std::string & path);
  // return false if the path does not exist
  // when a path is tagged as an array, keys might be discarded
  // by some serializers, such as JSON
  bool tag_as_array(const std::string & path, bool is_array);

 private:
  Any data_ {};
  bool is_array_ {false};
  mutable childs_t childrens_ {};
  mutable std::mutex mutex_ {};
  childs_t::iterator get_child_iterator(const std::string & key) const;
  static bool graft_next(std::istringstream &path, Tree *tree,
                         Tree::ptr leaf);
  GetNodeReturn get_node(const std::string &path) const;
  bool get_next(std::istringstream &path,
                childs_t &parent_list_result,
                childs_t::iterator &result_iterator) const;
};

// constructor
Tree::ptr make_tree();
template<typename ValueType> Tree::ptr make_tree(ValueType data) {
  return std::make_shared<Tree> (data);
}
Tree::ptr make_tree(const char *data);      // Tree will store a std::string
}  // namespace data
}  // namespace switcher
#endif