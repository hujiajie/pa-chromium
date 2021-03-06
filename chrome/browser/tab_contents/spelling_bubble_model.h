// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_SPELLING_BUBBLE_MODEL_H_
#define CHROME_BROWSER_TAB_CONTENTS_SPELLING_BUBBLE_MODEL_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "chrome/browser/ui/confirm_bubble_model.h"

class Profile;

namespace content {
class WebContents;
}

// A class that implements a bubble menu shown when we confirm a user allows
// integrating the spelling service of Google to Chrome.
class SpellingBubbleModel : public ConfirmBubbleModel {
 public:
  SpellingBubbleModel(Profile* profile,
                      content::WebContents* web_contents,
                      bool include_autocorrect);
  virtual ~SpellingBubbleModel();

  // ConfirmBubbleModel implementation.
  virtual string16 GetTitle() const OVERRIDE;
  virtual string16 GetMessageText() const OVERRIDE;
  virtual gfx::Image* GetIcon() const OVERRIDE;
  virtual string16 GetButtonLabel(BubbleButton button) const OVERRIDE;
  virtual void Accept() OVERRIDE;
  virtual void Cancel() OVERRIDE;
  virtual string16 GetLinkText() const OVERRIDE;
  virtual void LinkClicked() OVERRIDE;

 private:
  // Set the profile preferences to enable or disable the feature.
  void SetPref(bool enabled);

  Profile* profile_;
  content::WebContents* web_contents_;
  bool include_autocorrect_;

  DISALLOW_COPY_AND_ASSIGN(SpellingBubbleModel);
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_SPELLING_BUBBLE_MODEL_H_
