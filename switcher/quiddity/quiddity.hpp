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

#ifndef __SWITCHER_QUIDDITY_H__
#define __SWITCHER_QUIDDITY_H__

#include <memory>
#include <string>
#include <vector>

#include "../infotree/information-tree.hpp"
#include "../logger/logger.hpp"
#include "../utils/make-consultable.hpp"
#include "../utils/safe-bool-idiom.hpp"
#include "./claw/claw.hpp"
#include "./claw/config.hpp"
#include "./claw/types.hpp"
#include "./config.hpp"
#include "./doc.hpp"
#include "./documentation-registry.hpp"
#include "./method/mbag.hpp"
#include "./property/pbag.hpp"
#include "./quid-id-t.hpp"
#include "./signal/sbag.hpp"

namespace switcher {

namespace gst {
class AudioCodec;
class VideoCodec;
}  // namespace gst
namespace quiddities {
class ProtocolCurl;
class ProtocolOsc;
class ProtocolReader;
}  // namespace quiddities
namespace shmdata {
class Follower;
class GstTreeUpdater;
class ShmdataDecoder;
class Writer;
struct Stat;
}  // namespace shmdata
namespace quiddity {
class Container;
namespace bundle {
class Bundle;
}  // namespace bundle

/**
 * The Quiddity class.
 */
class Quiddity : public SafeBoolIdiom, public logger::Logger {
  friend class bundle::Bundle;  // access to props_ in order to forward properties
  friend class Startable;
  friend class claw::Claw;
  // FIXME do something for this (to many friend class in quiddity.hpp):
  friend class gst::AudioCodec;
  friend class gst::VideoCodec;
  friend class quiddities::ProtocolCurl;
  friend class quiddities::ProtocolOsc;
  friend class quiddities::ProtocolReader;
  friend class shmdata::Follower;
  friend class shmdata::GstTreeUpdater;
  friend class shmdata::ShmdataDecoder;
  friend class shmdata::Writer;
  friend struct shmdata::Stat;

 public:
  using ptr = std::shared_ptr<Quiddity>;
  explicit Quiddity(quiddity::Config&&, claw::Config conf = claw::Config());
  Quiddity() = delete;
  Quiddity(const Quiddity&) = delete;
  Quiddity& operator=(const Quiddity&) = delete;
  virtual ~Quiddity();

  // save/load quiddity state
  virtual InfoTree::ptr on_saving();
  virtual void on_saved();
  virtual void on_loading(InfoTree::ptr&& tree);
  virtual void on_loaded();
  bool prop_is_saved(const std::string& prop);

  // instance identification
  qid_t get_id() const;
  std::string get_kind() const;
  bool set_nickname(const std::string& nickname);
  std::string get_nickname() const;

  /**
   * Get a tree that is describing the quiddity.
   * @details It generates a tree that contains:
   *          - the id of the quiddity
   *          - its kind
   *          - its nickname
   * @return an info tree
   */
  InfoTree::ptr get_description();

  /**
   * Get a weak pointer to the current Quiddity, obtained from the Switcher instance.
   *
   * @return the weak pointer
   */
  std::weak_ptr<Quiddity> get_weak_ptr_to_this() const;

  // properties
  Make_consultable(Quiddity, property::PBag, &props_, prop);

  // methods
  Make_consultable(Quiddity, method::MBag, &meths_, meth);

  // signals
  Make_consultable(Quiddity, signal::SBag, &sigs_, sig);

  // information
  Make_consultable(Quiddity, InfoTree, information_tree_.get(), tree);

  // Shmdata connections
  Make_consultable(Quiddity, InfoTree, connection_spec_tree_.get(), conspec);
  Make_consultable(Quiddity, claw::Claw, &claw_, claw);

  // user data
  Make_delegate(Quiddity, InfoTree, structured_user_data_.get(), user_data);
  Selective_hook(user_data,
                 decltype(&InfoTree::graft),
                 &InfoTree::graft,
                 &Quiddity::user_data_graft_hook);
  Selective_hook(user_data,
                 decltype(&InfoTree::prune),
                 &InfoTree::prune,
                 &Quiddity::user_data_prune_hook);

  std::string get_manager_name();
  std::string get_quiddity_caps();

  /**
   * Force notification of on-user-data-grafted signal. Note the signal is already triggered when
   * the user_data tree is grafted with the user_data delegate.
   *
   * \param path the grafted path
   */
  void notify_user_data_grafted(const std::string& path);

  /**
   * Force notification of on-user-data-pruned signal. Note the signal is already triggered when
   * the user_data tree is pruned with the user_data delegate.
   *
   * \param path the pruned path
   */
  void notify_user_data_pruned(const std::string& path);

 private:
  // safe bool idiom implementation
  bool safe_bool_idiom() const { return is_valid_; }
  // user data hooks
  bool user_data_graft_hook(const std::string& path, InfoTree::ptr tree);
  InfoTree::ptr user_data_prune_hook(const std::string& path);

  // tree used by quiddity to communicate info to user,
  // read-only by user, read/write by quiddity
  InfoTree::ptr information_tree_;

  /**
   * \brief connection_spec_tree_ contains the shmdata branching
   * specification. It is intended to be feed and maintained by
   * the Claw class through the claw_ member.
   *
   */
  InfoTree::ptr connection_spec_tree_{};

  // writable tree for custom user data, should not be used by quiddity
  // (hooks are installed for signaling graft and prune)
  InfoTree::ptr structured_user_data_;

  // configuration tree. When manager is loaded with a config file,
  // the branch of the tree corresponding to the quiddity kind
  // is given to the quiddity. There is no constrain about how quiddity
  // should use this configuration
  InfoTree::ptr configuration_tree_;

  // signals
  signal::SBag sigs_;
  signal::sig_id_t on_method_added_id_;
  signal::sig_id_t on_method_removed_id_;
  signal::sig_id_t on_tree_grafted_id_;
  signal::sig_id_t on_tree_pruned_id_;
  signal::sig_id_t on_user_data_grafted_id_;
  signal::sig_id_t on_user_data_pruned_id_;
  signal::sig_id_t on_nicknamed_id_;
  signal::sig_id_t on_connection_spec_grafted_id_;
  signal::sig_id_t on_connection_spec_pruned_id_;

  // properties
  property::PBag props_;
  std::vector<std::string> properties_blacklist_{};

  // methods
  method::MBag meths_;

  // position weight FIXME should be outside this file ?
  int position_weight_counter_{0};

  // naming
  const qid_t id_;
  std::string nickname_;
  std::string kind_;

  // life management
  std::mutex self_destruct_mtx_{};


 protected:
  // information
  bool graft_tree(const std::string& path, InfoTree::ptr tree_to_graft, bool do_signal = true);
  void notify_tree_updated(const std::string& path);
  InfoTree::ptr prune_tree(const std::string& path, bool do_signal = true);
  InfoTree::ptr get_tree(const std::string& path);
  static InfoTree::ptr get_shm_information_template();

  // property
  Make_delegate(Quiddity, property::PBag, &props_, pmanage);

  // configuration
  Make_consultable(Quiddity, InfoTree, configuration_tree_.get(), config);

  // signal
  Make_delegate(Quiddity, signal::SBag, &sigs_, smanage);

  // methods
  Make_delegate(Quiddity, method::MBag, &meths_, mmanage);

  // connections with claw
  claw::Claw claw_;

  // life management
  void self_destruct();

  bool toggle_property_saving(const std::string&);

  // safe bool idiom implementation, default to valid
  // if you are writing a quiddity, you need to set this to false in order to invalidate the
  // creation of the Quiddity instance
  bool is_valid_{true};

  // access to the quiddity Container
  Container* qcontainer_;
};

}  // namespace quiddity
}  // namespace switcher

#define SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(                                                     \
    cpp_quiddity_kind, kind, name, description, license, author)                                  \
  bool cpp_quiddity_kind##_doc_registered = quiddity::DocumentationRegistry::get()->register_doc( \
      kind, quiddity::Doc(kind, name, description, license, author));                             \
  bool cpp_quiddity_kind##_kind_registered =                                                      \
      quiddity::DocumentationRegistry::get()->register_type_from_kind(                            \
          std::string(#cpp_quiddity_kind), kind);

#define SWITCHER_DECLARE_PLUGIN(cpp_quiddity_kind)                      \
  extern "C" quiddity::Quiddity* create(quiddity::Config&& conf) {      \
    return new cpp_quiddity_kind(std::forward<quiddity::Config>(conf)); \
  }                                                                     \
  extern "C" void destroy(Quiddity* quiddity) { delete quiddity; }      \
  extern "C" const char* get_quiddity_kind() {                          \
    static char type[64];                                               \
    strcpy(type,                                                        \
           quiddity::DocumentationRegistry::get()                       \
               ->get_type_from_kind(std::string(#cpp_quiddity_kind))    \
               .c_str());                                               \
    return static_cast<const char*>(type);                              \
  }

#endif
