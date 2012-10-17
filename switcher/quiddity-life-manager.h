/*
 * Copyright (C) 2012 Nicolas Bouillot (http://www.nicolasbouillot.net)
 *
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

/**
 * The QuiddityLifeManager class wraps abstract factory for creating instace by name (birth) 
 * It wraps StringMap for managing instances (subsistence). 
 * Object destruction is managed through the use of std::tr1::shared_ptr
 */

#ifndef __SWITCHER_QUIDDITY_LIFE_MANAGER_H__
#define __SWITCHER_QUIDDITY_LIFE_MANAGER_H__


#include <tr1/memory>
#include "switcher/abstract-factory.h" 
#include "switcher/string-map.h"
#include "switcher/quiddity.h" 

namespace switcher
{
  class Quiddity;

  class QuiddityLifeManager //FIXME rename that into QuiddityManager_Impl 
  //or find a way to provide a shared/weak ptr for "this" (and name it QuiddityManager) 
  {
  public:
    typedef std::tr1::shared_ptr< QuiddityLifeManager > ptr;
    
    QuiddityLifeManager();//will get name "default"
    QuiddityLifeManager(std::string);
    
    //info
    std::string get_name ();
    std::vector<std::string> get_classes ();
    bool class_exists (std::string class_name);
    std::vector<std::string> get_instances ();
    bool exists (std::string quiddity_name);
    
    //creation
    std::string create (std::string quiddity_class, 
			QuiddityLifeManager::ptr life_manager);
    std::string create (std::string quiddity_class, 
			std::string nick_name, 
			QuiddityLifeManager::ptr life_manager);
 
    //subsistence
    std::tr1::shared_ptr<Quiddity> get_quiddity (std::string quiddity_name);
    
    //release base quiddity (destructed with the shared pointer)
    bool remove (std::string quiddity_name);

    //properties
    std::string get_properties_description (std::string quiddity_name); //json formated
    std::string get_property_description (std::string quiddity_name, std::string property_name); //json formated
    bool set_property (std::string quiddity_name,
		       std::string property_name,
		       std::string property_value);
     
    std::string get_property (std::string quiddity_name, 
			      std::string property_name);
     
    //methods 
    std::string get_methods_description (std::string quiddity_name); //json formated
    std::string get_method_description (std::string quiddity_name, std::string method_name); //json formated
    bool invoke (std::string quiddity_name, 
		 std::string method_name,
		 std::vector<std::string> args);  


  private:
    std::string name_;
    void register_classes ();
    AbstractFactory< Quiddity, std::string, std::string, QuiddityLifeManager::ptr > abstract_factory_;
    StringMap< std::tr1::shared_ptr<Quiddity> > quiddities_;
    StringMap< std::string> quiddities_nick_names_;
  };

} // end of namespace



#endif // ifndef
