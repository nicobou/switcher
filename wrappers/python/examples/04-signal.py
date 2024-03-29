#!/usr/bin/env python3

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation; either version 2.1
# of the License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

import sys
import pyquid
import time


success = False
success2 = False


def on_nicknamed(data, user_data):
    global success2
    assert user_data == my_user_data2
    print(data.get())
    assert data.get() == 'vid2'
    # success
    success2 = True


def on_tree_grafted(data, user_data):
    global success
    assert user_data == my_user_data
    assert not data.empty()
    # success
    success = True


sw = pyquid.Switcher("signals", debug=True)

# create a quiddity
vid = sw.create("videotestsrc", "vid")

my_user_data = vid
my_user_data2 = str('on-nicknamed')

# check if on-tree-grafted is available with this quiddity
assert "on-tree-grafted" in pyquid.InfoTree(
    vid.get_info_tree_as_json(".signal")).get_key_values('id', False)

# subscribe to a signal
assert vid.subscribe("on-tree-grafted", on_tree_grafted, my_user_data)
assert vid.subscribe("on-nicknamed", on_nicknamed, my_user_data2)
vid.set_nickname('vid2')
vid.set("started", True)

# wait for the signal to arrive,
time.sleep(0.5)

assert vid.unsubscribe("on-tree-grafted")

vid.set("started", False)

if (not success or not success2):
    exit(1)
exit(0)
