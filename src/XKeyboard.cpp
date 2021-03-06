/*
 * Implementation of a class to get keyboard layout information and change layouts
 *
 * Copyright (C) 2008 by Jay Bromley <jbromley@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 */

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>

#include <iostream>
#include <string>
#include <sstream>

#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>

#include "XKeyboard.h"
#include "XKbSwitch.hpp"

namespace kb {

XKeyboard::XKeyboard()
  : _display(0), _deviceId(XkbUseCoreKbd), _kbdDescPtr(0)
{
}

void XKeyboard::open_display()
{

  XkbIgnoreExtension(False);

  char* displayName = strdup(""); // allocates memory for string!
  int eventCode;
  int errorReturn;
  int major = XkbMajorVersion;
  int minor = XkbMinorVersion;
  int reasonReturn;

  _display = XkbOpenDisplay(displayName, &eventCode, &errorReturn, &major,
      &minor, &reasonReturn);
  free(displayName);
  switch (reasonReturn) {
    case XkbOD_Success:           break;
    case XkbOD_BadLibraryVersion: THROW_MSG("Bad XKB library version.");
    case XkbOD_ConnectionRefused: THROW_MSG("Connection to X server refused.");
    case XkbOD_BadServerVersion:  THROW_MSG("Bad X11 server version.");
    case XkbOD_NonXkbServer:      THROW_MSG("XKB not present.");
    default:                      THROW_MSG("XKB refused to open the display with reason '" << reasonReturn << "'.");
  }

  _kbdDescPtr = XkbAllocKeyboard();
  if (_kbdDescPtr == NULL) {
    THROW_MSG("Failed to get keyboard description.");
  }

  _kbdDescPtr->dpy = _display;
  if (_deviceId != XkbUseCoreKbd) {
    _kbdDescPtr->device_spec = _deviceId;
  }
}

XKeyboard::~XKeyboard()
{
  if(_kbdDescPtr!=NULL)
    XkbFreeKeyboard(_kbdDescPtr, 0, True);

  if (_display!=NULL) {
    XCloseDisplay(_display);
  }
}

// XkbRF_VarDefsRec contains heap-allocated C strings, but doesn't provide a
// direct cleanup method. This wrapper privides a workaround.
// See also https://gitlab.freedesktop.org/xorg/lib/libxkbfile/issues/6
struct XkbRF_VarDefsRec_wrapper {

  XkbRF_VarDefsRec _it;

  XkbRF_VarDefsRec_wrapper() {
    std::memset(&_it,0,sizeof(_it));
  }

  ~XkbRF_VarDefsRec_wrapper() {
    if(_it.model) std::free(_it.model);
    if(_it.layout) std::free(_it.layout);
    if(_it.variant) std::free(_it.variant);
    if(_it.options) std::free(_it.options);
  }
};


void XKeyboard::build_layout(string_vector& out)
{
  XkbRF_VarDefsRec_wrapper vdr;
  char* tmp = NULL;
  Bool bret;

  bret = XkbRF_GetNamesProp(_display, &tmp, &vdr._it);
  free(tmp);  // return memory allocated by XkbRF_GetNamesProp
  CHECK_MSG(bret==True, "Failed to get keyboard properties");

  std::istringstream layout(vdr._it.layout ? vdr._it.layout : "us");
  std::istringstream variant(vdr._it.variant ? vdr._it.variant : "");

  while(true) {
    string l,v;

    getline(layout, l, ',');
    getline(variant, v, ',');
    if(!layout && !variant)
      break;

    if(v!="") {
      v = "(" + v + ")";
    }
    if(l!="") {
      out.push_back(l+v);
    }
  }
}

void XKeyboard::wait_event()
{
  CHECK(_display != 0);

  Bool bret = XkbSelectEventDetails(_display, XkbUseCoreKbd,
      XkbStateNotify, XkbAllStateComponentsMask, XkbGroupStateMask);
  CHECK_MSG(bret==True, "XkbSelectEventDetails failed");

  XEvent event;
  int iret = XNextEvent(_display, &event);
  CHECK_MSG(iret==0, "XNextEvent failed with " << iret);
}

void XKeyboard::set_group(int groupNum)
{
  Bool result = XkbLockGroup(_display, _deviceId, groupNum);
  CHECK(result == True);
  XFlush(_display);
}

int XKeyboard::get_group() const
{
  XkbStateRec xkbState;
  XkbGetState(_display, _deviceId, &xkbState);
  return static_cast<int>(xkbState.group);
}

// returns true if symbol is ok
bool filter(const string_vector& nonsyms, const std::string& symbol)
{
  if(symbol.empty())
    return false;

  // Filter out all prohibited words
  string_vector::const_iterator r = find(nonsyms.begin(), nonsyms.end(), symbol);
  if(r != nonsyms.end())
    return false;

  // Filter out all numbers groups started with number
  if(isdigit(symbol[0]))
    return false;

  return true;
}

}

