/*
 * This file is part of switcher python wrapper.
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

#ifndef __SWITCHER_PYSWITCH_H__
#define __SWITCHER_PYSWITCH_H__

#include <Python.h>  // according to python doc, this *must* be the first include
#include "switcher/switcher.hpp"

using namespace switcher;

class pySwitch {
 public:
  using pySwitchObject = struct {
    PyObject_HEAD PyObject* name{nullptr};
    Switcher::ptr switcher{};
  };

  static PyMethodDef pySwitch_methods[];
  static PyTypeObject pyType;

 private:
  static void Switcher_dealloc(pySwitchObject* self);
  static PyObject* Switcher_new(PyTypeObject* type, PyObject* args, PyObject* kwds);
  static int Switcher_init(pySwitchObject* self, PyObject* args, PyObject* kwds);
  static PyObject* get_name(pySwitchObject* self);
  static PyObject* create(pySwitchObject* self, PyObject* args, PyObject* kwds);
};
#endif
