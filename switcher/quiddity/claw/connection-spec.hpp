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

#ifndef __SWITCHER_QUIDDITY_CLAW_CONNECTION_SPEC_H__
#define __SWITCHER_QUIDDITY_CLAW_CONNECTION_SPEC_H__

#include <map>
#include <string>

#include "../../infotree/information-tree.hpp"
#include "../../utils/bool-log.hpp"
#include "./types.hpp"

namespace switcher {
namespace quiddity {
namespace claw {

/**
 * ConnectionSpec class maintains connection specification.
 * It uses an InfoTree and guaranties the well formed structure
 * of the specifications.
 *
 */
class ConnectionSpec : public BoolLog {
 public:
  /**
   * ConnectionSpec constructor. Start with an
   * empty specification.
   *
   */
  ConnectionSpec();

  /**
   * ConnectionSpec constructor. Parse the spec and
   * construct the Infotree accordingly.
   *
   * \param spec the specification in JSON format.
   *
   */
  ConnectionSpec(const std::string& spec);

  std::string get_writer_name(swid_t sid) const;
  std::string get_follower_name(sfid_t sid) const;
  sfid_t get_sfid(const std::string& name) const;
  swid_t get_swid(const std::string& name) const;
  std::string get_writer_shmpath(swid_t id) const;
  std::vector<std::string> get_writer_names() const;
  std::vector<std::string> get_follower_names() const;

  // check is sfid is valid and compute the new sfid with a non-meta name
  bool is_allocated(sfid_t sfid) const;
  sfid_t get_actual_sfid(sfid_t sfid);
  bool release(sfid_t sfid);
  bool is_actual_writer(swid_t swid) const;

  InfoTree::ptr get_tree();

 private:
  Ids follower_id_generator_{};
  std::map<sfid_t, std::string> follower_ids_{};
  Ids writer_id_generator_{};
  std::map<swid_t, std::string> writer_ids_{};

  /**
   * Tree with structured specifications.
   *
   */
  InfoTree::ptr connection_spec_;

  /**
   * Validate the shmdata specification.
   *
   * \param Tree containing the shmdata specification.
   *
   * \return Success with errors in the BoolLog message if any.
   *
   */
  BoolLog check_shmdata_spec(const InfoTree* tree);
};

}  // namespace claw
}  // namespace quiddity
}  // namespace switcher
#endif