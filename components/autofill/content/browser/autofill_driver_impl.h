// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_BROWSER_AUTOFILL_DRIVER_IMPL_H_
#define COMPONENTS_AUTOFILL_CONTENT_BROWSER_AUTOFILL_DRIVER_IMPL_H_

#include <string>

#include "base/supports_user_data.h"
#include "components/autofill/browser/autofill_driver.h"
#include "components/autofill/browser/autofill_manager.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class WebContents;
}

namespace IPC {
class Message;
}

namespace autofill {

class AutofillContext;
class AutofillExternalDelegate;
class AutofillManagerDelegate;

// Class that drives autofill flow in the browser process based on
// communication from the renderer and from the external world. There is one
// instance per WebContents.
class AutofillDriverImpl : public AutofillDriver,
                           public content::WebContentsObserver,
                           public base::SupportsUserData::Data {
 public:
  static void CreateForWebContentsAndDelegate(
      content::WebContents* contents,
      autofill::AutofillManagerDelegate* delegate,
      const std::string& app_locale,
      AutofillManager::AutofillDownloadManagerState enable_download_manager,
      bool enable_native_ui);
  static AutofillDriverImpl* FromWebContents(content::WebContents* contents);

  // AutofillDriver:
  virtual content::WebContents* GetWebContents() OVERRIDE;

  AutofillManager* autofill_manager() { return &autofill_manager_; }

 private:
  AutofillDriverImpl(
      content::WebContents* web_contents,
      autofill::AutofillManagerDelegate* delegate,
      const std::string& app_locale,
      AutofillManager::AutofillDownloadManagerState enable_download_manager,
      bool enable_native_ui);
  virtual ~AutofillDriverImpl();

  // content::WebContentsObserver:
  virtual void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) OVERRIDE;
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;

  // AutofillManager instance via which this object drives the shared Autofill
  // code.
  AutofillManager autofill_manager_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_BROWSER_AUTOFILL_DRIVER_IMPL_H_
