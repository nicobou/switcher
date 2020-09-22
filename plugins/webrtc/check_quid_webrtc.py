#!/usr/bin/env python3

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2.1
# of the License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

import asyncio
import os
import pyquid
import sys
import time

from dataclasses import dataclass
from multiprocessing import Process

from signaling.simple_server import WebRTCSimpleServer
from util.generator import Generator


success = False


def on_frame_received(data, user_data):
    global success
    success = True


# create a switcher.
sw = pyquid.Switcher('Webrtc', debug=True)
assert 'Webrtc' == sw.name()

# creation of this configuration:

# DummySink_1                 DummySink_2
#   |                            |
#   |                            |
# webRTC_1 -----audioquid------webRTC_2
#         |                    |
#         ------videoquid-------

# create a videotest quiddity
vidqrox = sw.create(type='videotestsrc', name='vid')
assert None != vidqrox
vid = vidqrox.quid()

# create an audiotest quiddity
audioqrox = sw.create(type='audiotestsrc', name='audio')
assert None != audioqrox
audio = audioqrox.quid()

# create a webrtc quiddity that manages a webrtcclient
webqrox1 = sw.create(type='webrtc', name='webrtcclient1')
assert None != webqrox1
web1 = webqrox1.quid()

# create a second webrtc quiddity
webqrox2 = sw.create(type='webrtc', name='webrtcclient2')
assert None != webqrox2
web2 = webqrox2.quid()

# create dummysinks for each webrtcclients
dummyqrox1 = sw.create(type='dummysink', name='dummy1')
assert None != dummyqrox1
dummy1 = dummyqrox1.quid()

dummyqrox2 = sw.create(type='dummysink', name='dummy2')
assert None != dummyqrox2
dummy2 = dummyqrox2.quid()

# connect audio and video through their shmpaths to the webrtc quids
vidshmpath = vid.make_shmpath('video')
audioshmpath = audio.make_shmpath('audio')

assert web1.invoke('connect', [vidshmpath])
assert web1.invoke('connect', [audioshmpath])

assert web2.invoke('connect', [vidshmpath])
assert web2.invoke('connect', [audioshmpath])

# connect the dummysink to the webrtc quids
assert dummy1.invoke('connect-quid', ['webrtcclient1', 'webrtc'])
assert dummy2.invoke('connect-quid', ['webrtcclient2', 'webrtc'])

# subscribe to the 'frame-received' property of the dummysinks
assert dummy1.subscribe('frame-received', on_frame_received, vid)
assert dummy2.subscribe('frame-received', on_frame_received, vid)

# configure and start the webrtc communication

# 1. start the video and audio quiddities
assert vid.set('started', True)
assert audio.set('started', True)
time.sleep(1)

# 2. Name the room to join
assert web1.set('room', 'switcher-room')
assert web2.set('room', 'switcher-room')

# 3. start the webrtc quid
def launch_server():
    @dataclass
    class Options:
        addr: str = ''
        port: int = 8443
        keepalive_timeout: int = 30
        cert_restart: bool = False
        cert_path: str = os.path.dirname(__file__)
        disable_ssl: bool = False
        health: str = '/health'

    loop = asyncio.get_event_loop()
    server = WebRTCSimpleServer(loop, Options())
    while True:
        server.run()
        loop.run_forever()

# to lauch the server, we need to generate a certificate first
cert = Generator()
file_dir = os.path.dirname(os.path.realpath(__file__))
cert.generate_certificate(key_file=os.path.join(file_dir, 'key.pem'), cert_file=os.path.join(file_dir, 'cert.pem'))

# launch the server
server_process = Process(target=launch_server)
server_process.start()

assert web1.set('started', True)
assert web2.set('started', True)

# unsubcribe
dummy1.unsubscribe('frame-received')
dummy2.unsubscribe('frame-received')

# disconnect
vid.set('started', False)
audio.set('started', False)

web1.invoke('disconnect', [vidshmpath])
web1.invoke('disconnect', [audioshmpath])

web2.invoke('disconnect', [vidshmpath])
web2.invoke('disconnect', [audioshmpath])

dummy1.invoke('disconnect', ['webrtcclient1'])
dummy2.invoke('disconnect', ['webrtcclient2'])

# remove
sw.remove(vidqrox.id())
sw.remove(audioqrox.id())

sw.remove(webqrox1.id())
sw.remove(webqrox2.id())

sw.remove(dummyqrox1.id())
sw.remove(dummyqrox2.id())

server_process.terminate()

# exit
exit(success)