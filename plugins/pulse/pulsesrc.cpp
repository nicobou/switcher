/*
 * Copyright (C) 2012-2013 Nicolas Bouillot (http://www.nicolasbouillot.net)
 *
 * This file is part of switcher-pulse. 
 *
 * Partially from pactl.c Copyright 2004-2006 Lennart Poettering 
 *
 * switcher-pulse is free software; you can redistribute it and/or
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

#include "pulsesrc.h"
#include "switcher/gst-utils.h"

namespace switcher
{
  SWITCHER_MAKE_QUIDDITY_DOCUMENTATION(PulseSrc,
				       "Audio Device Source (Pulse)",
				       "audio source", 
				       "Inspecting Devices And Getting Audio From Inputs",
				       "LGPL",
				       "pulsesrc",
				       "Nicolas Bouillot");
    
  bool
  PulseSrc::init ()
  {
    if (!make_elements ())
      return false;
    //set the name before registering properties
    set_name (gst_element_get_name (pulsesrc_));

    pa_context_ = NULL;
    server_ = NULL;

    pa_glib_mainloop_ = pa_glib_mainloop_new(get_g_main_context ());
    
    pa_mainloop_api_ = pa_glib_mainloop_get_api(pa_glib_mainloop_);
    
    if (!(pa_context_ = pa_context_new(pa_mainloop_api_, NULL))) {
      g_debug ("PulseSrc:: pa_context_new() failed.");
      return false;
    }
    
    pa_context_set_state_callback(pa_context_, pa_context_state_callback, this);
    
    if (pa_context_connect(pa_context_, server_, (pa_context_flags_t)0, NULL) < 0) {
      g_debug ("pa_context_connect() failed: %s", pa_strerror(pa_context_errno(pa_context_)));
      return false;
    }
    
    
    capture_devices_description_ = NULL;

    publish_method ("Capture",
		    "capture", 
		    "start capturing from default device", 
		    "success or fail",
		    Method::make_arg_description ("none",
						  NULL),
		    (Method::method_ptr) &capture_wrapped, 
		    G_TYPE_BOOLEAN,
		    Method::make_arg_type_description (G_TYPE_NONE, 
						       NULL),
		    this);
    


    publish_method ("Capture Device",
		    "capture_device", 
		    "start capturing from selected device", 
		    "success or fail",
		    Method::make_arg_description ("Pulse Audio Device Name",
						  "pulse_name_device",
						  "Pulse Audio Device Name",
						  NULL),
		    (Method::method_ptr) &capture_device_wrapped, 
		    G_TYPE_BOOLEAN,
		    Method::make_arg_type_description (G_TYPE_STRING, 
						       NULL),
		    this);


    custom_props_.reset (new CustomPropertyHelper ());
    capture_devices_description_spec_ = custom_props_->make_string_property ("devices-json", 
									     "Description of capture devices (json formated)",
									     "",
									     (GParamFlags) G_PARAM_READABLE,
									     NULL,
									     PulseSrc::get_capture_devices_json,
									     this);
    
    register_property_by_pspec (custom_props_->get_gobject (), 
				capture_devices_description_spec_, 
				"devices-json",
				"Capture Devices");
    return true;
  }

  PulseSrc::~PulseSrc ()
  {
     pa_context_disconnect (pa_context_);
     //pa_mainloop_api_->quit (pa_mainloop_api_, 0);
     pa_glib_mainloop_free(pa_glib_mainloop_);

    if (capture_devices_description_ != NULL)
      g_free (capture_devices_description_);
  }


  bool
  PulseSrc::make_elements ()
  {
    if (!GstUtils::make_element ("pulsesrc",&pulsesrc_))
      return false;
    if (!GstUtils::make_element ("capsfilter",&capsfilter_))
      return false;
    if (!GstUtils::make_element ("bin",&pulsesrc_bin_))
      return false;

    unregister_property ("volume");
    unregister_property ("mute");
    register_property (G_OBJECT (pulsesrc_),"volume","volume", "Volume");
    register_property (G_OBJECT (pulsesrc_),"mute","mute", "Mute");

    g_object_set (G_OBJECT (pulsesrc_), "client", get_nick_name ().c_str (), NULL);
    
    gst_bin_add_many (GST_BIN (pulsesrc_bin_),
		      pulsesrc_,
		      capsfilter_,
		      NULL);

    gst_element_link (pulsesrc_, capsfilter_);

    GstPad *src_pad = gst_element_get_static_pad (capsfilter_, "src");
    GstPad *ghost_srcpad = gst_ghost_pad_new (NULL, src_pad);
    gst_pad_set_active(ghost_srcpad,TRUE);
    gst_element_add_pad (pulsesrc_bin_, ghost_srcpad); 
    gst_object_unref (src_pad);

    return true;
  }

  void 
  PulseSrc::pa_context_state_callback(pa_context *pulse_context, void *user_data) {
    
    PulseSrc *context = static_cast <PulseSrc *> (user_data);
    
    switch (pa_context_get_state(pulse_context)) {
    case PA_CONTEXT_CONNECTING:
      //g_print ("PA_CONTEXT_CONNECTING\n");
      break;
    case PA_CONTEXT_AUTHORIZING:
      //g_print ("PA_CONTEXT_AUTHORIZING\n");
      break;
    case PA_CONTEXT_SETTING_NAME:
      //g_print ("PA_CONTEXT_SETTING_NAME\n");
      break;
    case PA_CONTEXT_READY: 
      //g_print ("PA_CONTEXT_READY\n");
      context->make_device_description (pulse_context);
      // pa_operation_unref(pa_context_get_source_info_list(pulse_context,
      // 							 get_source_info_callback, 
      // 							 NULL));
      
      pa_context_set_subscribe_callback (pulse_context,
					 on_pa_event_callback,
					 NULL); 	
      
      pa_operation_unref(pa_context_subscribe (pulse_context,
					       (pa_subscription_mask_t) (PA_SUBSCRIPTION_MASK_SINK|
									 PA_SUBSCRIPTION_MASK_SOURCE|
									 PA_SUBSCRIPTION_MASK_SINK_INPUT|
									 PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT|
									 PA_SUBSCRIPTION_MASK_MODULE|
									 PA_SUBSCRIPTION_MASK_CLIENT|
									 PA_SUBSCRIPTION_MASK_SAMPLE_CACHE|
									 PA_SUBSCRIPTION_MASK_SERVER|
									 PA_SUBSCRIPTION_MASK_CARD),
					       NULL, //pa_context_success_cb_t cb,
					       NULL) //void *userdata);
			 );
      
      break;
    case PA_CONTEXT_TERMINATED:
      g_debug ("PulseSrc: PA_CONTEXT_TERMINATED\n");
      pa_context_unref(context->pa_context_);
      context->pa_context_ = NULL;
      break;
    case PA_CONTEXT_FAILED:
      //g_print ("PA_CONTEXT_FAILED\n");
      break;
    default:
      g_debug ("PulseSrc Context error: %s\n",pa_strerror(pa_context_errno(pulse_context)));
    }
  }

  void 
  PulseSrc::make_json_description ()
  {
    if (capture_devices_description_ != NULL)
      g_free (capture_devices_description_);
    
    JSONBuilder::ptr builder (new JSONBuilder ());
    builder->reset();
    builder->begin_object ();
    builder->set_member_name ("capture devices");
    builder->begin_array ();

    for (auto &it: capture_devices_)
      {
	builder->begin_object ();
	builder->add_string_member ("long name", it.second.description_.c_str());
	builder->add_string_member ("name", it.second.name_.c_str());
	builder->add_string_member ("state", it.second.state_.c_str());
	builder->add_string_member ("sample format", it.second.sample_format_.c_str());
	builder->add_string_member ("sample rate", it.second.sample_rate_.c_str());
	builder->add_string_member ("channels", it.second.channels_.c_str());
	builder->add_string_member ("active port", it.second.active_port_.c_str());
	builder->end_object ();
      }
    
    builder->end_array ();
    builder->end_object ();
    capture_devices_description_ = g_strdup (builder->get_string (true).c_str ());
    //g_print ("%s\n",capture_devices_description_);
    GObjectWrapper::notify_property_changed (gobject_->get_gobject (), capture_devices_description_spec_);
  }
  
  void 
  PulseSrc::get_source_info_callback(pa_context *pulse_context, 
				     const pa_source_info *i, 
				     int is_last, 
				     void *user_data) 
  {
    PulseSrc *context = static_cast <PulseSrc *> (user_data);
    if (is_last < 0) {
      g_debug ("Failed to get source information: %s", pa_strerror(pa_context_errno(pulse_context)));
      return;
    }
    
    if (is_last) {
      pa_operation *operation = pa_context_drain(pulse_context, NULL, NULL);
      if (operation)
        pa_operation_unref(operation);
      
      context->make_json_description ();
      return;
    }
    
    DeviceDescription description;
    switch (i->state) {
    case PA_SOURCE_INIT:
      description.state_ = "INIT";
      //g_print ("state: INIT \n");
      break;
    case PA_SOURCE_UNLINKED:
      description.state_ = "UNLINKED";
      //g_print ("state: UNLINKED \n");
      break;
    case PA_SOURCE_INVALID_STATE:
      description.state_ = "n/a";      
      //g_print ("state: n/a \n");
      break;
    case PA_SOURCE_RUNNING:
      description.state_ = "RUNNING";      
      //g_print ("state: RUNNING \n");
      break;
    case PA_SOURCE_IDLE:
      description.state_ = "IDLE";      
      //g_print ("state: IDLE \n");
      break;
    case PA_SOURCE_SUSPENDED:
      description.state_ = "SUSPENDED";      
      //g_print ("state: SUSPENDED \n");
      break;
    }

    description.name_ = i->name;
    if (i->description == NULL)
      description.description_ = "";
    else
      description.description_ = i->description;
    
    description.sample_format_ = pa_sample_format_to_string (i->sample_spec.format);
    gchar *rate = g_strdup_printf ("%u", i->sample_spec.rate);
    description.sample_rate_ = rate;
    g_free (rate);
    gchar *channels = g_strdup_printf ("%u", i->sample_spec.channels);
    description.channels_ = channels;
    g_free (channels);
    
    // g_print ("Name: %s\n"
    // 	     "Description: %s\n"
    // 	     " format: %s\n"
    // 	     " rate: %u\n"
    // 	     " channels: %u\n",
    // 	     //"Channel Map: %s\n",
    // 	     i->name,
    // 	     i->description,//warning this can be NULL
    // 	     pa_sample_format_to_string (i->sample_spec.format),
    // 	     i->sample_spec.rate,
    // 	     i->sample_spec.channels//,
    // 	     //pa_channel_map_snprint(cm, sizeof(cm), &i->channel_map)
    // 	     );

    if (i->ports) {
        pa_source_port_info **p;
        //printf("\tPorts:\n");
        for (p = i->ports; *p; p++)
	  {
	    //printf("\t\t%s: %s (priority. %u)\n", (*p)->name, (*p)->description, (*p)->priority);
	    description.ports_.push_back (std::make_pair ((*p)->name, (*p)->description));
	  }
    }

    if (i->active_port)
      {
	//printf("\tActive Port: %s\n", i->active_port->name);
	description.active_port_ = i->active_port->description;
      }
    else
      description.active_port_ = "n/a";

    context->capture_devices_[description.name_] = description;

    // if (i->formats) {
    //   uint8_t j;
    //   printf("\tFormats:\n");
    //   for (j = 0; j < i->n_formats; j++)
    // 	printf("\t\t%s\n", pa_format_info_snprint(f, sizeof(f), i->formats[j]));
    // }
  }

  void 
  PulseSrc::make_device_description (pa_context *pulse_context)
  {
    capture_devices_.clear ();
    pa_operation_unref(pa_context_get_source_info_list(pulse_context, get_source_info_callback, this));
  }


  void 
    PulseSrc::on_pa_event_callback (pa_context *pulse_context, 
				    pa_subscription_event_type_t pulse_event_type,
				    uint32_t index, 
				    void *user_data)
  {
    PulseSrc *context = static_cast<PulseSrc *> (user_data);

    if ((pulse_event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE) 
      {
	if ((pulse_event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) 
	  {
	    context->make_device_description (pulse_context);
	    return;
	  }
      }
    
    if ((pulse_event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE) 
      {
	if ((pulse_event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) 
	  {
	    context->make_device_description (pulse_context);
	    return;
	  }
      }
  }
  
  gchar *
  PulseSrc::get_capture_devices_json (void *user_data)
  {
    PulseSrc *context = static_cast<PulseSrc *> (user_data);
    if (context->capture_devices_description_ == NULL)
      context->capture_devices_description_ = g_strdup ("{ \"capture devices\" : [] }");

    return context->capture_devices_description_;
  }

  gboolean 
  PulseSrc::capture_wrapped (gpointer device_file_path, 
			    gpointer user_data)
  {
    PulseSrc *context = static_cast<PulseSrc *>(user_data);
    
    if (context->capture_device ("default"))
      return TRUE;
    else
      return FALSE;
  }

    gboolean 
    PulseSrc::capture_device_wrapped (gpointer pulse_device_name,
				      gpointer user_data)
  {
    PulseSrc *context = static_cast<PulseSrc *>(user_data);
    
    if (context->capture_device ((const char *)pulse_device_name))
      return TRUE;
    else
      return FALSE;
  }

  bool 
  PulseSrc::capture_device (const char *pulse_device_name)
  {
    make_elements ();

    if (g_strcmp0 (pulse_device_name, "default") != 0)
      if (capture_devices_.find (pulse_device_name) != capture_devices_.end ())	
     	g_object_set (G_OBJECT (pulsesrc_), "device", pulse_device_name, NULL);
      else
     	{
     	  g_warning ("PulseSrc: device %s has not been detected by pulse audio, cannot use", pulse_device_name);
     	  return false;
     	}

    set_raw_audio_element (pulsesrc_bin_);
    return true;
  }

}//end of PulseSrc class
  
  
  