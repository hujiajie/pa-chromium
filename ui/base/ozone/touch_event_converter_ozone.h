// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_OZONE_TOUCH_EVENT_CONVERTER_OZONE_H_
#define UI_BASE_OZONE_TOUCH_EVENT_CONVERTER_OZONE_H_

#include <bitset>

#include "base/compiler_specific.h"
#include "ui/base/events/event_constants.h"
#include "ui/base/ozone/event_converter_ozone.h"
#include "ui/base/ui_export.h"

namespace ui {

class TouchEvent;

class UI_EXPORT TouchEventConverterOzone : public EventConverterOzone {
 public:
  enum {
    MAX_FINGERS = 11
  };
  TouchEventConverterOzone(int fd, int id);
  virtual ~TouchEventConverterOzone();

 private:
  friend class MockTouchEventConverterOzone;

  // Unsafe part of initialization.
  void Init();

  // Overidden from base::MessagePumpLibevent::Watcher.
  virtual void OnFileCanReadWithoutBlocking(int fd) OVERRIDE;
  virtual void OnFileCanWriteWithoutBlocking(int fd) OVERRIDE;

  // Pressure values.
  int pressure_min_;
  int pressure_max_;  // Used to normalize pressure values.

  // Touch scaling.
  float x_scale_;
  float y_scale_;

  // Touch point currently being updated from the /dev/input/event* stream.
  int current_slot_;

  // File descriptor for the /dev/input/event* instance.
  int fd_;

  // Number corresponding to * in the source evdev device: /dev/input/event*
  int id_;

  // Bit field tracking which in-progress touch points have been modified
  // without a syn event.
  std::bitset<MAX_FINGERS> altered_slots_;

  struct InProgressEvents {
    int x_;
    int y_;
    int id_;  // Device reported "unique" touch point id; -1 means not active
    int finger_;  // "Finger" id starting from 0; -1 means not active

    EventType type_;
    int major_;
    float pressure_;
  };

  // In-progress touch points.
  InProgressEvents events_[MAX_FINGERS];

  DISALLOW_COPY_AND_ASSIGN(TouchEventConverterOzone);
};

}  // namespace ui

#endif  // UI_BASE_OZONE_TOUCH_EVENT_CONVERTER_OZONE_H_
