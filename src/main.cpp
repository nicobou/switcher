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

#include "switcher/ctrl-server.h"
//#include "switcher/webservices/control.nsmap"
#include "switcher/quiddity-manager.h"
#include <iostream>
#include <signal.h>



static gchar *server_name = NULL;
//static gchar **remaining_args = NULL;

static GOptionEntry entries[] =
  {
    { "server-name", 's', 0, G_OPTION_ARG_STRING, &server_name, "server name (default is \"default\")", NULL },
    { NULL }
};

 //clean up pipeline when ctrl-c
 void
 leave (int sig)
 {
   exit (sig);
 }


int
main (int argc,
      char *argv[])
{
     (void) signal (SIGINT, leave);

     //command line options
     GError *error = NULL;
     GOptionContext *context;
     context = g_option_context_new ("- switcher server");
     g_option_context_add_main_entries (context, entries, NULL);
     if (!g_option_context_parse (context, &argc, &argv, &error))
       {
	 g_print ("option parsing failed: %s\n", error->message);
	 exit (1);
       } 
  


     using namespace switcher;
     
     if (server_name == NULL)
       server_name = "default";

       QuiddityManager manager(server_name);  

     std::vector<std::string> available_object_list = manager.get_classes ();
     
    //list available object in factory
    // for (uint i=0; i < available_object_list.size (); i++)
    //   {
    //     std::cout<< "** available object: " << available_object_list[i] << std::endl; 
    //   }    
  
    // Create a runtime
    std::string runtime = manager.create ("runtime");

     //creating a SOAP webservice controling the manager
     //Quiddity::ptr baseserv = manager.create ("controlserver");
     //TODO make this available from the base manager interface 
     //(for instance "this" or better could be the string naming the manager)
     CtrlServer  *serv =  new CtrlServer(); //std::tr1::dynamic_pointer_cast<CtrlServer> (baseserv);
     serv->set_quiddity_manager (&manager);
     serv->start ();


    // // //create a videotest
    // std::string videotest = manager.create ("gconfvideosource");

    // // //attaching videotestsrc to the runtime
    // std::vector<std::string> ent_name;
    // ent_name.push_back (runtime);
    // manager.invoke_method (videotest,"set_runtime",ent_name);

     while (1)
       sleep (1);

  return 0;
}



