/*
 * This file is part of switcher-pjsip.
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

#include <unistd.h>  // usleep
#include <assert.h>

#include <vector>
#include <string>
#include <list>

#include "switcher/quiddity-manager.hpp"
#include "switcher/quiddity-basic-test.hpp"
#include "switcher/information-tree.hpp"

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

int main() {
  bool success = true;
  {
    const std::string manager_name("siptest");
    const std::string sip_name("test");
    const std::string audio_name("a");
    const std::string video_name("v");
    std::list<std::string> buddies =
        { "sip:1002@10.10.30.179",
          "sip:1003@10.10.30.179"};

    switcher::QuiddityManager::ptr manager =
        switcher::QuiddityManager::make_manager(manager_name);

#ifdef HAVE_CONFIG_H
    gchar *usr_plugin_dir = g_strdup_printf("./%s", LT_OBJDIR);
    manager->scan_directory_for_plugins(usr_plugin_dir);
    g_free(usr_plugin_dir);
#else
    return 1;
#endif

    // testing uncompressed data transmission
    assert(0 ==
           manager->create("audiotestsrc", audio_name).compare(audio_name));
    assert(manager->set_property(audio_name, "started", "true"));

    assert(0 ==
           manager->create("videotestsrc", video_name).compare(video_name));
    assert(manager->set_property(video_name, "started", "true"));

    // SIP
    assert(0 == manager->create("sip", sip_name).compare(sip_name));
    assert(manager->set_property(sip_name, "port", "5070"));
    assert(manager->set_property(sip_name, "port", "5080"));

    assert(manager->invoke_va(sip_name,
                              "register",
                              nullptr,
                              "1004",  // user
                              "10.10.30.179",  // domain
                              "1234",  // password
                              nullptr));

    for (auto &it : buddies) {
      assert(manager->invoke_va(sip_name,
                                "add_buddy",
                                nullptr,
                                it.c_str(),
                                nullptr));

      assert(manager->invoke_va(sip_name,
                                "name_buddy",
                                nullptr,
                                std::string(it+"-nickname").c_str(),
                                it.c_str(),
                                nullptr));
      
      assert(manager->invoke_va(sip_name,
                                "attach_shmdata_to_contact",
                                nullptr,
                                std::string(
                                    "/tmp/switcher_" +
                                    manager_name + "_" +
                                    audio_name + "_audio").c_str(),
                                it.c_str(),
                                "true",
                                nullptr));
      assert(manager->invoke_va(sip_name,
                                "attach_shmdata_to_contact",
                                nullptr,
                                std::string(
                                    "/tmp/switcher_" +
                                    manager_name + "_" +
                                    video_name + "_video").c_str(),
                                it.c_str(),
                                "true",
                                nullptr));
    }
    
    {// get added buddies from  
      auto get_buddy_ids = [&] (switcher::data::Tree::ptrc tree) {
        return switcher::data::Tree::get_child_keys<std::list>(tree, "buddy.");
      };
      using buddy_ids_t = std::list<std::string>;
      buddy_ids_t buddy_ids = manager->
          invoke_info_tree<buddy_ids_t>(sip_name,
                                        get_buddy_ids);

      std::list <std::string> buds_from_tree;
      for (auto &it : buddy_ids) {
        buds_from_tree.push_back(manager->invoke_info_tree<std::string>(
            sip_name,
            [&](switcher::data::Tree::ptrc tree){
              return 
		switcher::data::Tree::read_data(tree, 
						"buddy." + it + ".uri").copy_as<std::string>();
            }));
      }
      assert(std::equal(buddies.begin(), buddies.end(),
                         buds_from_tree.begin()));
    }

    usleep(200000);

    for (auto &it : buddies)
      assert(manager->invoke_va(sip_name,
                                "call",
                                nullptr,
                                it.c_str(),
                                nullptr));
    
    // usleep(8000000);
    assert(manager->set_property(sip_name, "status", "Away"));
    // usleep(8000000);
    assert(manager->set_property(sip_name, "status-note", "coucou"));
    // usleep(8000000);
    assert(manager->set_property(sip_name, "status", "BRB"));
    // usleep(2000000);
    
    for (auto &it : buddies)
      assert(manager->invoke_va(sip_name,
                                "hang-up",
                                nullptr,
                                it.c_str(),
                                nullptr));
    
    assert(manager->invoke_va(sip_name,
                              "save_buddies",
                              nullptr,
                              "buddies.list",
                              nullptr));
// usleep(8000000);
    
    assert(manager->remove(sip_name));

    // usleep(8000000);
  }  // end of scope is releasing the manager
  
  if (success)
    return 0;
  else
    return 1;
}
