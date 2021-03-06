#!/usr/bin/env python3

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2.1
# of the License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.


# This test creates two jack servers that will communicate audio through a shmdata.
#
# This test also illustrates the use of quiddity configuration (see creation of Jack clients).

import sys
import pyquid
import time
import assert_exit_1

# create a switcher.
sw = pyquid.Switcher('pyquid', debug=True)

# first dummy jack server
src_serv = sw.create('jackserver', config=pyquid.InfoTree(
    '{ "driver" : "dummy", "name": "swcapture" }')).quid()
src_serv.set('started', True)

# second dummy jack server
sink_serv = sw.create('jackserver', config=pyquid.InfoTree(
    '{ "driver" : "dummy", "name": "swcard" }')).quid()
sink_serv.set('started', True)

# create a jack-to-shmdata in first jack server.
swcapture = sw.create('jacksrc', 'capture', pyquid.InfoTree('{ "server_name" : "swcapture" }'))
swcapture.quid().set('started', True)

# create a shmdata-to-jack in the second sever
swcard = sw.create('jacksink', 'display', pyquid.InfoTree('{ "server_name" : "swcard" }'))
swcard.quid().invoke('connect-quid', ['capture', 'audio'])

# let it go! but just for a sec.
time.sleep(1)

sw.remove(swcapture.id())
sw.remove(swcard.id())
exit(0)
