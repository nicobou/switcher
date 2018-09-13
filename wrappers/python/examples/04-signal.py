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
sys.path.insert(0, '/usr/local/lib/python3/dist-packages')
import pyquid
import time
import assert_exit_1

# "my_user_data" will be passed to the subscribe method
my_user_data = ["my", "user", "data"]


def on_tree_grafted(key, user_data):
    assert user_data == my_user_data
    # success
    exit(0)


sw = pyquid.Switcher("signals", debug=True)
print('1')
# create a quiddity
qroxvid = sw.create("videotestsrc", "vid")
assert None != qroxvid
vid = qroxvid.quid()

print('2')
# check if on-tree-grafted is available with this quiddity
assert "on-tree-grafted" in pyquid.InfoTree(
    vid.get_info_tree_as_json(".signal")).get_key_values('id', False)
print('3')

# subscribe to a signal
assert vid.subscribe("on-tree-grafted", on_tree_grafted, my_user_data)
print('4')

vid.set("started", True)
print('5')

# wait for the signal to arrive,
time.sleep(1)
print('6')

# the test will fail if the signal is not triggered before
exit(1)
