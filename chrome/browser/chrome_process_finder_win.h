// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_PROCESS_FINDER_WIN_H_
#define CHROME_BROWSER_CHROME_PROCESS_FINDER_WIN_H_

#include <windows.h>

namespace base {
class FilePath;
}

namespace chrome {

enum NotifyChromeResult {
  NOTIFY_SUCCESS,
  NOTIFY_FAILED,
  NOTIFY_WINDOW_HUNG,
};

// Finds an already running Chrome window if it exists.
HWND FindRunningChromeWindow(const base::FilePath& user_data_dir);

// Attempts to send the current command line to an already running instance of
// Chrome via a WM_COPYDATA message.
// Returns true if a running Chrome is found and successfully notified.
NotifyChromeResult AttemptToNotifyRunningChrome(HWND remote_window);

}  // namespace chrome

#endif  // CHROME_BROWSER_CHROME_PROCESS_FINDER_WIN_H_
