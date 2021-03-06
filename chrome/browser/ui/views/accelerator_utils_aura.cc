// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/accelerator_table.h"
#include "ui/base/accelerators/accelerator.h"

#if defined(USE_ASH)
#include "ash/accelerators/accelerator_table.h"
#endif  // USE_ASH

namespace chrome {

bool IsChromeAccelerator(const ui::Accelerator& accelerator, Profile* profile) {
#if defined(USE_ASH)
  for (size_t i = 0; i < ash::kAcceleratorDataLength; ++i) {
    const ash::AcceleratorData& accel_data = ash::kAcceleratorData[i];
    if (accel_data.keycode == accelerator.key_code() &&
        accel_data.modifiers == accelerator.modifiers()) {
      return true;
    }
  }
#endif

  std::vector<chrome::AcceleratorMapping> accelerators =
      chrome::GetAcceleratorList();
  for (std::vector<chrome::AcceleratorMapping>::const_iterator it =
       accelerators.begin(); it != accelerators.end(); ++it) {
    if (it->keycode == accelerator.key_code() &&
        it->modifiers == accelerator.modifiers())
      return true;
  }

  return false;
}

}  // namespace chrome
