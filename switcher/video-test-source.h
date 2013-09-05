/*
 * Copyright (C) 2012-2013 Nicolas Bouillot (http://www.nicolasbouillot.net)
 *
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


#ifndef __SWITCHER_VIDEO_TEST_SOURCE_H__
#define __SWITCHER_VIDEO_TEST_SOURCE_H__

#include "video-source.h"
#include "startable-quiddity.h"
#include <memory>

namespace switcher
{

  class VideoTestSource : public VideoSource, public StartableQuiddity
  {
  public:
    SWITCHER_DECLARE_QUIDDITY_PUBLIC_MEMBERS(VideoTestSource);
    ~VideoTestSource();

    bool start ();
    bool stop ();

  private:
    GstElement *videotestsrc_;
    bool make_videotestsrc ();
  };

}  // end of namespace

#endif // ifndef
