// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_process_platform_part_aurawin.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/process_util.h"
#include "base/win/windows_version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/metro_viewer/chrome_metro_viewer_process_host_aurawin.h"
#include "chrome/common/chrome_switches.h"

BrowserProcessPlatformPart::BrowserProcessPlatformPart() {
}

BrowserProcessPlatformPart::~BrowserProcessPlatformPart() {
}

void BrowserProcessPlatformPart::OnMetroViewerProcessTerminated() {
  metro_viewer_process_host_.reset(NULL);
}

void BrowserProcessPlatformPart::PlatformSpecificCommandLineProcessing(
    const CommandLine& command_line) {
  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      command_line.HasSwitch(switches::kViewerConnection) &&
      !metro_viewer_process_host_.get()) {
    // Tell the metro viewer process host to connect to the given IPC channel.
    metro_viewer_process_host_.reset(
        new ChromeMetroViewerProcessHost(
            command_line.GetSwitchValueASCII(switches::kViewerConnection)));
  }
}

void BrowserProcessPlatformPart::AttemptExit() {
  // On WinAura, the regular exit path is fine except on Win8+, where Ash might
  // be active in Metro and won't go away even if all browsers are closed. The
  // viewer process, whose host holds a reference to g_browser_process, needs to
  // be killed as well.
  BrowserProcessPlatformPartBase::AttemptExit();

  if (base::win::GetVersion() >= base::win::VERSION_WIN8 &&
      metro_viewer_process_host_) {
    base::ProcessId viewer_id =
        metro_viewer_process_host_->GetViewerProcessId();
    if (viewer_id == base::kNullProcessId)
      return;
    // The viewer doesn't hold any state so it is fine to kill it before it
    // cleanly exits. This will trigger MetroViewerProcessHost::OnChannelError()
    // which will cleanup references to g_browser_process.
    bool success = base::KillProcessById(viewer_id, 0, true);
    DCHECK(success);
  }
}
