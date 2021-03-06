// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTERNAL_PROTOCOL_DIALOG_H_
#define CHROME_BROWSER_CHROMEOS_EXTERNAL_PROTOCOL_DIALOG_H_

#include <string>

#include "base/strings/string16.h"
#include "base/time.h"
#include "ui/views/window/dialog_delegate.h"

class GURL;

namespace content {
class WebContents;
}

namespace views {
class MessageBoxView;
}

// An external protocol dialog for ChromeOS. Unlike other platforms,
// ChromeOS does not support launching external program, therefore,
// this dialog simply says it is not supported.
class ExternalProtocolDialog : public views::DialogDelegate {
 public:
  // RunExternalProtocolDialog calls this private constructor.
  ExternalProtocolDialog(content::WebContents* web_contents, const GURL& url);

  virtual ~ExternalProtocolDialog();

  // views::DialogDelegate Methods:
  virtual int GetDialogButtons() const OVERRIDE;
  virtual string16 GetDialogButtonLabel(ui::DialogButton button) const OVERRIDE;
  virtual string16 GetWindowTitle() const OVERRIDE;
  virtual void DeleteDelegate() OVERRIDE;
  virtual bool Accept() OVERRIDE;
  virtual views::View* GetContentsView() OVERRIDE;

  // views::WidgetDelegate Methods:
  virtual const views::Widget* GetWidget() const OVERRIDE;
  virtual views::Widget* GetWidget() OVERRIDE;

 private:
  // The message box view whose commands we handle.
  views::MessageBoxView* message_box_view_;

  // The time at which this dialog was created.
  base::TimeTicks creation_time_;

  // The scheme of the url.
  std::string scheme_;

  DISALLOW_COPY_AND_ASSIGN(ExternalProtocolDialog);
};

#endif  // CHROME_BROWSER_CHROMEOS_EXTERNAL_PROTOCOL_DIALOG_H_
