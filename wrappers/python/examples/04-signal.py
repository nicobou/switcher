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


def on_tree_grafted(key):
    # success
    exit(0)


sw = pyquid.Switcher("signals")

# create a quiddity
qroxvid = sw.create("videotestsrc", "vid")
assert None != qroxvid
vid = qroxvid.quid()

# check if on-tree-grafted is available with this quiddity
assert "on-tree-grafted" in pyquid.InfoTree(
    vid.get_info_tree_as_json(".signal")).get_key_values('id', False)

# subscribe to a signal
assert vid.subscribe("on-tree-grafted", on_tree_grafted)

vid.set("started", True)

# wait for the signal to arrive,
time.sleep(1)
# the test will fail if the signal is not triggered before
exit(1)