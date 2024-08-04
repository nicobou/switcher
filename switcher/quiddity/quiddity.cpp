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
 * The Quiddity class
 */

#include "./quiddity.hpp"

#include <sys/un.h>

#include <algorithm>
#include <regex>

#include "../shmdata/stat.hpp"
#include "../switcher.hpp"
#include "./container.hpp"
#include "./quid-id-t.hpp"

namespace switcher {
namespace quiddity {

Quiddity::Quiddity(quiddity::Config&& conf, claw::Config claw_conf)
    : Logger(*conf.qc_->get_switcher()),
      information_tree_(InfoTree::make()),
      structured_user_data_(InfoTree::make()),
      configuration_tree_(conf.tree_config_ ? InfoTree::copy(conf.tree_config_) : InfoTree::make()),
      sigs_(
          information_tree_,
          [this](const std::string& key) {
            smanage<&signal::SBag::notify>(on_tree_grafted_id_, InfoTree::make(key));
          },
          [this](const std::string& key) {
            smanage<&signal::SBag::notify>(on_tree_pruned_id_, InfoTree::make(key));
          }),
      on_method_added_id_(
          smanage<&signal::SBag::make>("on-method-added", "A method has been installed")),
      on_method_removed_id_(
          smanage<&signal::SBag::make>("on-method-removed", "A method has been uninstalled")),
      on_tree_grafted_id_(smanage<&signal::SBag::make>("on-tree-grafted", "On Tree Grafted")),
      on_tree_pruned_id_(smanage<&signal::SBag::make>(
          "on-tree-pruned", "A tree has been pruned from the quiddity tree")),
      on_user_data_grafted_id_(smanage<&signal::SBag::make>(
          "on-user-data-grafted", "A tree has been grafted to the quiddity's user data tree")),
      on_user_data_pruned_id_(smanage<&signal::SBag::make>(
          "on-user-data-pruned", "A branch has been pruned from the quiddity's user data tree")),
      on_nicknamed_id_(smanage<&signal::SBag::make>(
          "on-nicknamed", "A nickname has been given to the quiddity")),
      on_connection_spec_grafted_id_(smanage<&signal::SBag::make>(
          "on-connection-spec-added",
          "New specification has been added to the connection specification")),
      on_connection_spec_pruned_id_(smanage<&signal::SBag::make>(
          "on-connection-spec-removed",
          "Specification has been removed from the connection specification")),
      props_(
          information_tree_,
          [this](const std::string& key) {
            smanage<&signal::SBag::notify>(on_tree_grafted_id_, InfoTree::make(key));
          },
          [this](const std::string& key) {
            smanage<&signal::SBag::notify>(on_tree_pruned_id_, InfoTree::make(key));
          }),
      meths_(
          this,
          information_tree_,
          [this](const std::string& key) {  // on_tree_grafted_cb
            smanage<&signal::SBag::notify>(on_tree_grafted_id_, InfoTree::make(key));
          },
          [this](const std::string& key) {  // on_tree_pruned_cb
            smanage<&signal::SBag::notify>(on_tree_pruned_id_, InfoTree::make(key));
          },
          [this](const std::string& method_name) {  // on_method_created
            smanage<&signal::SBag::notify>(on_method_added_id_, InfoTree::make(method_name));
          },
          [this](const std::string& method_name) {  // on_method_removed
            smanage<&signal::SBag::notify>(on_method_removed_id_,
                                                 InfoTree::make(method_name));
          },
          [this](const std::string& method_name) {  // on_method_enabled
            smanage<&signal::SBag::notify>(on_method_added_id_, InfoTree::make(method_name));
          },
          [this](const std::string& method_name) {  // on_method_disabled
            smanage<&signal::SBag::notify>(on_method_removed_id_,
                                                 InfoTree::make(method_name));
          }),
      id_(conf.id_),
      nickname_(conf.nickname_),
      kind_(conf.kind_),
      claw_(this,
            claw::ConnectionSpec(claw_conf.spec),
            claw_conf.on_connect_cb,
            claw_conf.on_disconnect_cb),
      qcontainer_(conf.qc_) {
  configuration_tree_->graft(".", InfoTree::make());
  information_tree_->graft(".kind", InfoTree::make(conf.kind_));
}

Quiddity::~Quiddity() {
  std::lock_guard<std::mutex> lock(self_destruct_mtx_);
}

qid_t Quiddity::get_id() const { return id_; }

std::string Quiddity::get_kind() const { return kind_; }

std::string Quiddity::get_manager_name() { return qcontainer_->get_switcher()->name_; }

std::string Quiddity::get_quiddity_caps() {
  auto caps_str = qcontainer_->get_switcher()->get_switcher_caps();
  caps_str = caps_str + ",quiddity-id=(int)" + std::to_string(id_);
  return caps_str;
}

bool Quiddity::graft_tree(const std::string& path, InfoTree::ptr tree, bool do_signal) {
  if (!information_tree_->graft(path, tree)) return false;
  if (do_signal) {
    smanage<&signal::SBag::notify>(on_tree_grafted_id_, InfoTree::make(path));
  }
  return true;
}

void Quiddity::notify_tree_updated(const std::string& path) {
  smanage<&signal::SBag::notify>(on_tree_grafted_id_, InfoTree::make(path));
}

InfoTree::ptr Quiddity::get_tree(const std::string& path) {
  return information_tree_->get_tree(path);
}

InfoTree::ptr Quiddity::prune_tree(const std::string& path, bool do_signal) {
  InfoTree::ptr result = information_tree_->prune(path);
  if (result) {
    if (do_signal) {
      smanage<&signal::SBag::notify>(on_tree_pruned_id_, InfoTree::make(path));
    }
  }
  return result;
}

bool Quiddity::user_data_graft_hook(const std::string& path, InfoTree::ptr tree) {
  if (!structured_user_data_->graft(path, std::forward<InfoTree::ptr>(tree))) return false;
  smanage<&signal::SBag::notify>(on_user_data_grafted_id_, InfoTree::make(path));
  return true;
}

InfoTree::ptr Quiddity::user_data_prune_hook(const std::string& path) {
  auto res = structured_user_data_->prune(path);
  if (res) {
    smanage<&signal::SBag::notify>(on_user_data_pruned_id_, InfoTree::make(path));
  }
  return res;
}

void Quiddity::notify_user_data_grafted(const std::string& path) {
  smanage<&signal::SBag::notify>(on_user_data_grafted_id_, InfoTree::make(path));
}

void Quiddity::notify_user_data_pruned(const std::string& path) {
  smanage<&signal::SBag::notify>(on_user_data_pruned_id_, InfoTree::make(path));
}

void Quiddity::self_destruct() {
  std::unique_lock<std::mutex> lock(self_destruct_mtx_);
  auto thread = std::thread([ this, th_lock = std::move(lock) ]() mutable {
    th_lock.unlock();
    auto res = qcontainer_->get_switcher()->quids<&quiddity::Container::remove>(id_);
    if (!res) sw_warning("{} did not self destruct ({})", get_nickname(), res.msg());
  });
  thread.detach();
}

InfoTree::ptr Quiddity::on_saving() { return InfoTree::make(); };

void Quiddity::on_saved(){};

void Quiddity::on_loading(InfoTree::ptr&&){};

void Quiddity::on_loaded(){};

bool Quiddity::prop_is_saved(const std::string& prop) {
  return std::find(properties_blacklist_.begin(), properties_blacklist_.end(), prop) ==
         properties_blacklist_.end();
}

bool Quiddity::toggle_property_saving(const std::string& prop) {
  auto prop_bl = std::find(properties_blacklist_.begin(), properties_blacklist_.end(), prop);
  if (prop_bl == properties_blacklist_.end()) {
    properties_blacklist_.push_back(prop);
    return false;
  } else {
    properties_blacklist_.erase(prop_bl);
    return true;
  }
}

bool Quiddity::set_nickname(const std::string& nickname) {
  nickname_ = nickname;
  smanage<&signal::SBag::notify>(on_nicknamed_id_, InfoTree::make(nickname));
  return true;
}

std::string Quiddity::get_nickname() const { return nickname_; }

InfoTree::ptr Quiddity::get_description() {
  auto tree = InfoTree::make();
  tree->graft(".id", InfoTree::make(id_));
  tree->graft(".kind", InfoTree::make(kind_));
  tree->graft(".nickname", InfoTree::make(nickname_));
  return tree;
}

InfoTree::ptr Quiddity::get_shm_information_template() {
  InfoTree::ptr tree = InfoTree::make();
  tree->graft(".caps", InfoTree::make());
  tree->graft(".category", InfoTree::make());
  shmdata::Stat().update_tree(tree, ".");
  return tree;
}

std::weak_ptr<Quiddity> Quiddity::get_weak_ptr_to_this() const {
  return qcontainer_->get_quiddity(id_);
}

}  // namespace quiddity
}  // namespace switcher
