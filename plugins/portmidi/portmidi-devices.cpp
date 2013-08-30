/*
 * Copyright (C) 2012-2013 Nicolas Bouillot (http://www.nicolasbouillot.net)
 *
 * This file is part of switcher-portmidi.
 *
 * switcher-myplugin is free software; you can redistribute it and/or
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

#include "portmidi-devices.h"
#include "switcher/json-builder.h"
#include <glib/gprintf.h>

namespace switcher
{

  PortMidi::PortMidiScheduler *PortMidi::scheduler_ = NULL;
  guint PortMidi::num_of_streams_ = 0;

  PortMidi::PortMidi ()
  {
    if (scheduler_ == NULL)
      scheduler_ = new PortMidiScheduler();

    devices_description_ = NULL;
    make_devices_description (this);
    update_device_enum ();
  }
  
  PortMidi::~PortMidi ()
  {

    for (auto &it: input_streams_)
      close_input_device (it.first);
    
    for (auto &it: output_streams_)
      close_output_device (it.first);
    
    //FIXME
    // if (num_of_streams_ == 0)
    //   delete scheduler_;
  }
  
  int 
  PortMidi::get_default_output_device_id ()
  {
    return Pm_GetDefaultOutputDeviceID();
  }
  
  int 
  PortMidi::get_default_input_device_id ()
  {
    return Pm_GetDefaultInputDeviceID();
  }
  
  bool
  PortMidi::open_input_device (int id, on_pm_event_method method, void *user_data)
  {
    if (input_streams_.find(id) != input_streams_.end())
      {
	g_debug ("input device (id %d), already opened, cannot open", id);
	return false;
      }
    PmStream *stream = scheduler_->add_input_stream (id, method, user_data);
   
    if (stream == NULL)
      return false;
    num_of_streams_++;
    input_streams_[id] = stream;
    g_message ("Midi input device opened (id %d)", id);
    return true;
  }

  bool
  PortMidi::open_output_device (int id)
  {
    if (output_streams_.find(id) != output_streams_.end())
      {
	g_debug ("output device (id %d), already openned, cannot open", id);
	return false;
      }
    PmStream *stream = scheduler_->add_output_stream (id);
    if (stream == NULL)
      return false;      

    num_of_streams_++;
    output_streams_[id] = stream;
    g_message ("Midi output device opened (id %d)", id);
    return true;
  }
  
  bool
  PortMidi::close_input_device (int id)
  {
    std::map<guint, PmStream *>::iterator it = input_streams_.find(id);
    if (it == input_streams_.end())
      return false;
    
    if (scheduler_->remove_input_stream (it->second))
      num_of_streams_--;
    input_streams_.erase (id);
    
    g_message ("Midi input device closed (id %d)", id);
    return true;
  }

 
  bool
  PortMidi::close_output_device (int id)
  {
    std::map<guint, PmStream *>::iterator it = output_streams_.find(id);
    if (it == output_streams_.end())
      return false;
    
    if (scheduler_->remove_output_stream (it->second))
      num_of_streams_--;
    output_streams_.erase (id);
    g_message ("Midi input device closed (id %d)", id);
    return true;
  }

  gchar * 
  PortMidi::make_devices_description (void *user_data)
  {
    PortMidi *context = static_cast<PortMidi *> (user_data);
        
    if (context->devices_description_ != NULL)
      g_free (context->devices_description_);
    JSONBuilder::ptr builder (new JSONBuilder ());
    builder->reset();
    builder->begin_object ();
    builder->set_member_name ("devices");
    builder->begin_array ();

      /* list device information */
      int i;
      for (i = 0; i < Pm_CountDevices(); i++) {
	const PmDeviceInfo *listinfo = Pm_GetDeviceInfo(i);
	builder->begin_object ();
	builder->add_string_member ("long name", listinfo->name);
	builder->add_string_member ("interface", listinfo->interf);
	gchar *id = g_strdup_printf ("%d",i);
	builder->add_string_member ("id", id);
	g_free (id);
	if (listinfo->input) 
	  builder->add_string_member ("type", "input");
	else 
	  builder->add_string_member ("type","output");
	builder->end_object ();
      }

    builder->end_array ();
    builder->end_object ();
    context->devices_description_ = g_strdup (builder->get_string (true).c_str ());

    return context->devices_description_;
  }



  //#################################### SCHEDULER
    PortMidi::PortMidiScheduler::PortMidiScheduler () :  
      app_sysex_in_progress_ (false),
      thru_sysex_in_progress_ (false)
    {
      portmidi_initialized_ = false;
      /* always start the timer before you start midi */
      Pt_Start(1, &process_midi, this); /* start a timer with 1 millisecond accuracy */
      /* the timer will call our function, process_midi() every millisecond */
      Pm_Initialize();
      portmidi_initialized_ = true;
    }

    
    PortMidi::PortMidiScheduler::~PortMidiScheduler ()
    {
      portmidi_initialized_ = false;
      Pt_Stop(); /* stop the timer */
      Pm_Terminate();
    }

    PmStream *
    PortMidi::PortMidiScheduler::add_input_stream (int id, on_pm_event_method method, void *user_data)
    {

      PmStream *midi_in;
      if (pmNoError != Pm_OpenInput(&midi_in, 
				    id, 
				    NULL /* driver info */,
				    0 /* use default input size */,
				    NULL,
				    NULL /* time info */))
	return NULL;
      /* Note: if you set a filter here, then this will filter what goes
	 to the MIDI THRU port. You may not want to do this.
      */
      Pm_SetFilter(midi_in, PM_FILT_ACTIVE | PM_FILT_CLOCK);

      input_callbacks_[midi_in] = std::make_pair (method, user_data);      
     
      return midi_in;
    }

    PmStream *
    PortMidi::PortMidiScheduler::add_output_stream (int id)
    {
      PmStream *midi_out;
      if (pmNoError != Pm_OpenOutput(&midi_out, 
				     id, 
				     NULL /* driver info */,
				     0 /* use default input size */,
				     NULL,
				     NULL, /* time info */
				     0))
	return NULL;

      output_queues_[midi_out] = new std::queue<PmEvent>();      

      return midi_out;
    }

    // PmEvent 
    // PortMidi::PortMidiScheduler::poll (PmStream *stream)
    // {
    //   PmEvent message = input_queues_[stream]->front();
    //   input_queues_[stream]->pop();
    //   return message;
    // }

    // bool 
    // PortMidi::PortMidiScheduler::is_queue_empty (PmStream *stream)
    // {
    //   return input_queues_[stream]->empty();
    // }

    bool
    PortMidi::PortMidiScheduler::remove_input_stream (PmStream *stream)
    {
      Pm_Close (stream);
      input_callbacks_.erase(stream);
      return true;
    }

    bool
    PortMidi::PortMidiScheduler::remove_output_stream (PmStream *stream)
    {
      Pm_Close (stream);
      output_queues_.erase(stream);
      return true;
    }

    bool 
    PortMidi::PortMidiScheduler::push_message (PmStream *stream, 
					       unsigned char status, 
					       unsigned char data1, 
					       unsigned char data2)
    {
      PmEvent message_to_push;
      message_to_push.message = Pm_Message(status,data1,data2);
      message_to_push.timestamp=0; //use current time
      
      output_queues_[stream]->push (message_to_push);
      return true;
    }

    /* timer interrupt for processing midi data.
       Incoming data is delivered to main program via in_queue.
       Outgoing data from main program is delivered via out_queue.
       Incoming data from midi_in is copied with low latency to  midi_out.
       Sysex messages from either source block messages from the other.
    */
    void 
    PortMidi::PortMidiScheduler::process_midi (PtTimestamp timestamp, void *user_data)
    {

      PortMidiScheduler *context = static_cast<PortMidiScheduler *>(user_data);
    
      PmError result;
      PmEvent buffer; /* just one message at a time */

      if (!context->portmidi_initialized_) 
	  return;
      
      for(auto &itr: context->input_callbacks_)
	{
	  /* see if there is any midi input to process */
	  if (!context->app_sysex_in_progress_) {
	    do {
	      result = Pm_Poll(itr.first);
	      if (result) {
		int status;
		PmError rslt = (PmError)Pm_Read(itr.first, &buffer, 1);
		if (rslt == pmBufferOverflow) 
		  continue;
	
		/* the data might be the end of a sysex message that
		   has timed out, in which case we must ignore it.
		   It's a continuation of a sysex message if status
		   is actually a data byte (high-order bit is zero). */
		status = Pm_MessageStatus(buffer.message);
		if (((status & 0x80) == 0) && !context->thru_sysex_in_progress_) {
		  continue; /* ignore this data */
		}
		
		
		// g_print ("midi input msg from %u %u %u \n",
		// 	 Pm_MessageStatus(buffer.message),
		// 	 Pm_MessageData1(buffer.message),
		// 	 Pm_MessageData2(buffer.message));

		//invoking the callback
		itr.second.first (&buffer, itr.second.second);

		/* sysex processing */
		if (status == MIDI_SYSEX) context->thru_sysex_in_progress_ = true;
		else if ((status & 0xF8) != 0xF8) {
		  /* not MIDI_SYSEX and not real-time, so */
		  context->thru_sysex_in_progress_ = false;
		}
		if (context->thru_sysex_in_progress_ && /* look for EOX */
		    (((buffer.message & 0xFF) == MIDI_EOX) ||
		     (((buffer.message >> 8) & 0xFF) == MIDI_EOX) ||
		     (((buffer.message >> 16) & 0xFF) == MIDI_EOX) ||
		     (((buffer.message >> 24) & 0xFF) == MIDI_EOX))) {
		  context->thru_sysex_in_progress_ = false;
		}
	      }
	    } while (result);
	  }
	}//end of "for input_streams_"


      for(auto &itr: context->output_queues_)
	{
	  /* see if there is application midi data to process */
	  while (!itr.second->empty()) {
	    /* see if it is time to output the next message */
	    PmEvent *next = &(itr.second->front());//(PmEvent *) Pm_QueuePeek(out_queue);
	    //assert(next); /* must be non-null because queue is not empty */
	    /* time to send a message, first make sure it's not blocked */
	    int status = Pm_MessageStatus(next->message);
	    if ((status & 0xF8) == 0xF8) {
	      ; /* real-time messages are not blocked */
	    } else if (context->thru_sysex_in_progress_) {
                /* maybe sysex has timed out (output becomes unblocked) */
            	  context->thru_sysex_in_progress_ = false;
	    }
	    
	    g_print ("midi output msg: %u %u %u\n",
		     Pm_MessageStatus(next->message),
		     Pm_MessageData1(next->message),
		     Pm_MessageData2(next->message));
	    
	    Pm_Write(itr.first, next, 1);
	    
	    itr.second->pop();
	    
	    /* inspect message to update app_sysex_in_progress */
	    if (status == MIDI_SYSEX) context->app_sysex_in_progress_ = true;
	    else if ((status & 0xF8) != 0xF8) {
	      /* not MIDI_SYSEX and not real-time, so */
	      context->app_sysex_in_progress_ = false;
	    }
	    if (context->app_sysex_in_progress_ && /* look for EOX */
		(((buffer.message & 0xFF) == MIDI_EOX) ||
		 (((buffer.message >> 8) & 0xFF) == MIDI_EOX) ||
		 (((buffer.message >> 16) & 0xFF) == MIDI_EOX) ||
		 (((buffer.message >> 24) & 0xFF) == MIDI_EOX))) {
	      context->app_sysex_in_progress_ = false;
	    }
	  }
	}
    }

  gchar *
  PortMidi::get_devices_description_json (gpointer user_data)
  {
    PortMidi *context = static_cast <PortMidi *> (user_data);
    return context->devices_description_;
  }

  void 
  PortMidi::update_device_enum ()
  {
    int i;
    int input_i = 0;
    int output_i = 0;
    for (i = 0; i < Pm_CountDevices(); i++) {
      const PmDeviceInfo *listinfo = Pm_GetDeviceInfo(i);

      if (listinfo->input)
	{
	  input_devices_enum_ [input_i].value = i;
	  //FIXME free
	  input_devices_enum_ [input_i].value_nick = g_strdup_printf ("%s (%s)",
								      listinfo->name,
								      listinfo->interf);
	  input_devices_enum_ [input_i].value_name = input_devices_enum_ [input_i].value_nick;
	  input_i ++;
	}
      else
	{
	  output_devices_enum_ [output_i].value = i;
	  //FIXME free
	  output_devices_enum_ [output_i].value_nick = g_strdup (listinfo->name);
	  output_devices_enum_ [output_i].value_name = g_strdup_printf ("%d",i);
	  output_i ++;
	}
    }
    input_devices_enum_[input_i].value = 0;
    input_devices_enum_[input_i].value_name = NULL;
    input_devices_enum_[input_i].value_nick = NULL;
    output_devices_enum_[output_i].value = 0;
    output_devices_enum_[output_i].value_name = NULL;
    output_devices_enum_[output_i].value_nick = NULL;
  }

}
