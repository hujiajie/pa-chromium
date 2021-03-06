// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/message_center/cocoa/notification_controller.h"

#include "base/mac/foundation_util.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "grit/ui_resources.h"
#include "skia/ext/skia_utils_mac.h"
#import "ui/base/cocoa/hover_image_button.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/text/text_elider.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/notification.h"

@interface MCNotificationButtonCell : NSButtonCell {
  BOOL hovered_;
}
@end

@implementation MCNotificationButtonCell
- (void)drawBezelWithFrame:(NSRect)frame inView:(NSView*)controlView {
  // Else mouseEntered: and mouseExited: won't be called and hovered_ won't be
  // valid.
  DCHECK([self showsBorderOnlyWhileMouseInside]);

  if (!hovered_)
    return;
  [gfx::SkColorToCalibratedNSColor(
      message_center::kHoveredButtonBackgroundColor) set];
  NSRectFill(frame);
}

- (void)drawImage:(NSImage*)image
        withFrame:(NSRect)frame
           inView:(NSView*)controlView {
  if (!image)
    return;
  NSRect rect = NSMakeRect(message_center::kButtonHorizontalPadding,
                           message_center::kButtonIconTopPadding,
                           message_center::kNotificationButtonIconSize,
                           message_center::kNotificationButtonIconSize);
  [image drawInRect:rect
            fromRect:NSZeroRect
           operation:NSCompositeSourceOver
            fraction:1.0
      respectFlipped:YES
               hints:nil];
}

- (NSRect)drawTitle:(NSAttributedString*)title
          withFrame:(NSRect)frame
             inView:(NSView*)controlView {
  CGFloat offsetX = message_center::kButtonHorizontalPadding;
  if ([base::mac::ObjCCastStrict<NSButton>(controlView) image]) {
    offsetX += message_center::kNotificationButtonIconSize +
               message_center::kButtonIconToTitlePadding;
  }
  frame.origin.x = offsetX;
  frame.size.width -= offsetX;

  NSDictionary* attributes = @{
    NSFontAttributeName :
        [title attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL],
    NSForegroundColorAttributeName :
        gfx::SkColorToCalibratedNSColor(message_center::kRegularTextColor),
  };
  [[title string] drawWithRect:frame
                       options:(NSStringDrawingUsesLineFragmentOrigin |
                                NSStringDrawingTruncatesLastVisibleLine)
                    attributes:attributes];
  return frame;
}

- (void)mouseEntered:(NSEvent*)event {
  hovered_ = YES;

  // Else the cell won't be repainted on hover.
  [super mouseEntered:event];
}

- (void)mouseExited:(NSEvent*)event {
  hovered_ = NO;
  [super mouseExited:event];
}
@end

@interface MCNotificationView : NSBox {
 @private
  MCNotificationController* controller_;
}

- (id)initWithController:(MCNotificationController*)controller
                   frame:(NSRect)frame;
@end

@implementation MCNotificationView
- (id)initWithController:(MCNotificationController*)controller
                   frame:(NSRect)frame {
  if ((self = [super initWithFrame:frame]))
    controller_ = controller;
  return self;
}

- (void)mouseDown:(NSEvent*)event {
  if ([event type] != NSLeftMouseDown) {
    [super mouseDown:event];
    return;
  }
  [controller_ notificationClicked];
}
@end

@interface MCNotificationController (Private)
// Returns a string with item's title in title color and item's message in
// message color.
+ (NSAttributedString*)
    attributedStringForItem:(const message_center::NotificationItem&)item
                       font:(NSFont*)font;

// Configures a NSBox to be borderless, titleless, and otherwise appearance-
// free.
- (void)configureCustomBox:(NSBox*)box;

// Initializes the icon_ ivar and returns the view to insert into the hierarchy.
- (NSView*)createImageView;

// Initializes the closeButton_ ivar with the configured button.
- (void)configureCloseButtonInFrame:(NSRect)rootFrame;

// Initializes title_ in the given frame.
- (void)configureTitleInFrame:(NSRect)rootFrame;

// Initializes message_ in the given frame.
- (void)configureBodyInFrame:(NSRect)rootFrame;

// Creates a NSTextField that the caller owns configured as a label in a
// notification.
- (NSTextField*)newLabelWithFrame:(NSRect)frame;

// Gets the rectangle in which notification content should be placed. This
// rectangle is to the right of the icon and left of the control buttons.
// This depends on the icon_ and closeButton_ being initialized.
- (NSRect)currentContentRect;

// Returns the wrapped text that could fit within the given text field with not
// more than the given number of lines. The Ellipsis could be added at the end
// of the last line if it is too long.
- (string16)wrapText:(const string16&)text
            forField:(NSTextField*)field
    maxNumberOfLines:(size_t)lines;
@end

@implementation MCNotificationController

- (id)initWithNotification:(const message_center::Notification*)notification
    messageCenter:(message_center::MessageCenter*)messageCenter {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    notification_ = notification;
    notificationID_ = notification_->id();
    messageCenter_ = messageCenter;
  }
  return self;
}

- (void)loadView {
  // Create the root view of the notification.
  NSRect rootFrame = NSMakeRect(0, 0,
      message_center::kNotificationPreferredImageSize,
      message_center::kNotificationIconSize);
  scoped_nsobject<MCNotificationView> rootView(
      [[MCNotificationView alloc] initWithController:self
                                               frame:rootFrame]);
  [self configureCustomBox:rootView];
  [rootView setFillColor:gfx::SkColorToCalibratedNSColor(
      message_center::kNotificationBackgroundColor)];
  [self setView:rootView];

  [rootView addSubview:[self createImageView]];

  // Create the close button.
  [self configureCloseButtonInFrame:rootFrame];
  [rootView addSubview:closeButton_];

  // Create the title.
  [self configureTitleInFrame:rootFrame];
  [rootView addSubview:title_];

  // Create the message body.
  [self configureBodyInFrame:rootFrame];
  [rootView addSubview:message_];

  // Populate the data.
  [self updateNotification:notification_];
}

- (NSRect)updateNotification:(const message_center::Notification*)notification {
  DCHECK_EQ(notification->id(), notificationID_);
  notification_ = notification;

  NSRect rootFrame = NSMakeRect(0, 0,
      message_center::kNotificationPreferredImageSize,
      message_center::kNotificationIconSize);

  // Update the icon.
  [icon_ setImage:notification_->icon().AsNSImage()];

  // The message_center:: constants are relative to capHeight at the top and
  // relative to the baseline at the bottom, but NSTextField uses the full line
  // height for its height.
  CGFloat titleTopGap =
      roundf([[title_ font] ascender] - [[title_ font] capHeight]);
  CGFloat titleBottomGap = roundf(fabs([[title_ font] descender]));
  CGFloat titlePadding = message_center::kTextTopPadding - titleTopGap;

  CGFloat messageTopGap =
      roundf([[message_ font] ascender] - [[message_ font] capHeight]);
  CGFloat messagePadding =
      message_center::kTextTopPadding - titleBottomGap - messageTopGap;

  // Set the title and recalculate the frame.
  [title_ setStringValue:base::SysUTF16ToNSString(
      [self wrapText:notification_->title()
            forField:title_
       maxNumberOfLines:message_center::kTitleLineLimit])];
  [title_ sizeToFit];
  NSRect titleFrame = [title_ frame];
  titleFrame.origin.y = NSMaxY(rootFrame) - titlePadding - NSHeight(titleFrame);

  // Set the message and recalculate the frame.
  [message_ setStringValue:base::SysUTF16ToNSString(
      [self wrapText:notification_->message()
            forField:title_
       maxNumberOfLines:message_center::kMessageExpandedLineLimit])];
  [message_ sizeToFit];
  NSRect messageFrame = [message_ frame];
  messageFrame.origin.y =
      NSMinY(titleFrame) - messagePadding - NSHeight(messageFrame);
  messageFrame.size.height = NSHeight([message_ frame]);

  // Create the list item views (up to a maximum).
  [listItemView_ removeFromSuperview];
  const std::vector<message_center::NotificationItem>& items =
      notification->items();
  NSRect listFrame = NSZeroRect;
  if (items.size() > 0) {
    listFrame = [self currentContentRect];
    listFrame.origin.y = 0;
    listFrame.size.height = 0;
    listItemView_.reset([[NSView alloc] initWithFrame:listFrame]);
    CGFloat y = 0;

    NSFont* font = [NSFont systemFontOfSize:message_center::kMessageFontSize];
    CGFloat lineHeight = roundf(NSHeight([font boundingRectForFont]));

    const int kNumNotifications =
        std::min(items.size(), message_center::kNotificationMaximumItems);
    for (int i = kNumNotifications - 1; i >= 0; --i) {
      NSTextField* field = [self newLabelWithFrame:
          NSMakeRect(0, y, NSWidth(listFrame), lineHeight)];
      [[field cell] setUsesSingleLineMode:YES];
      [field setAttributedStringValue:
          [MCNotificationController attributedStringForItem:items[i]
                                                       font:font]];
      [listItemView_ addSubview:field];
      y += lineHeight;
    }
    // TODO(thakis): The spacing is not completely right.
    CGFloat listTopPadding =
        message_center::kTextTopPadding - messageTopGap;
    listFrame.size.height = y;
    listFrame.origin.y =
        NSMinY(messageFrame) - listTopPadding - NSHeight(listFrame);
    [listItemView_ setFrame:listFrame];
    [[self view] addSubview:listItemView_];
  }

  // If the bottom-most element so far is out of the rootView's bounds, resize
  // the view.
  CGFloat minY = NSMinY(messageFrame);
  if (listItemView_ && NSMinY(listFrame) < minY)
    minY = NSMinY(listFrame);
  if (minY < messagePadding) {
    CGFloat delta = messagePadding - minY;
    rootFrame.size.height += delta;
    titleFrame.origin.y += delta;
    messageFrame.origin.y += delta;
    listFrame.origin.y += delta;
  }

  // Add the bottom container view.
  NSRect frame = rootFrame;
  frame.size.height = 0;
  [bottomView_ removeFromSuperview];
  bottomView_.reset([[NSView alloc] initWithFrame:frame]);
  CGFloat y = 0;

  // Create action buttons if appropriate, bottom-up.
  std::vector<message_center::ButtonInfo> buttons = notification->buttons();
  for (int i = buttons.size() - 1; i >= 0; --i) {
    message_center::ButtonInfo buttonInfo = buttons[i];
    NSRect buttonFrame = frame;
    buttonFrame.origin = NSMakePoint(0, y);
    buttonFrame.size.height = message_center::kButtonHeight;
    scoped_nsobject<NSButton> button(
        [[NSButton alloc] initWithFrame:buttonFrame]);
    scoped_nsobject<MCNotificationButtonCell> cell(
        [[MCNotificationButtonCell alloc]
            initTextCell:base::SysUTF16ToNSString(buttonInfo.title)]);
    [cell setShowsBorderOnlyWhileMouseInside:YES];
    [button setCell:cell];
    [button setImage:buttonInfo.icon.AsNSImage()];
    [button setBezelStyle:NSSmallSquareBezelStyle];
    [button setImagePosition:NSImageLeft];
    [button setTag:i];
    [button setTarget:self];
    [button setAction:@selector(buttonClicked:)];
    y += NSHeight(buttonFrame);
    frame.size.height += NSHeight(buttonFrame);
    [bottomView_ addSubview:button];

    NSRect separatorFrame = frame;
    separatorFrame.origin = NSMakePoint(0, y);
    separatorFrame.size.height = 1;
    scoped_nsobject<NSBox> separator(
        [[NSBox alloc] initWithFrame:separatorFrame]);
    [self configureCustomBox:separator];
    [separator setFillColor:gfx::SkColorToCalibratedNSColor(
        message_center::kButtonSeparatorColor)];
    y += NSHeight(separatorFrame);
    frame.size.height += NSHeight(separatorFrame);
    [bottomView_ addSubview:separator];
  }

  // Create the image view if appropriate.
  if (!notification->image().IsEmpty()) {
    NSImage* image = notification->image().AsNSImage();
    NSRect imageFrame = frame;
    imageFrame.origin = NSMakePoint(0, y);
    imageFrame.size = NSSizeFromCGSize(message_center::GetImageSizeForWidth(
        NSWidth(frame), notification->image().Size()).ToCGSize());
    scoped_nsobject<NSImageView> imageView(
        [[NSImageView alloc] initWithFrame:imageFrame]);
    [imageView setImage:image];
    [imageView setImageScaling:NSImageScaleProportionallyUpOrDown];
    y += NSHeight(imageFrame);
    frame.size.height += NSHeight(imageFrame);
    [bottomView_ addSubview:imageView];
  }

  [bottomView_ setFrame:frame];
  [[self view] addSubview:bottomView_];

  rootFrame.size.height += NSHeight(frame);
  titleFrame.origin.y += NSHeight(frame);
  messageFrame.origin.y += NSHeight(frame);
  listFrame.origin.y += NSHeight(frame);

  [[self view] setFrame:rootFrame];
  [title_ setFrame:titleFrame];
  [message_ setFrame:messageFrame];
  [listItemView_ setFrame:listFrame];

  return rootFrame;
}

- (void)close:(id)sender {
  [closeButton_ setTarget:nil];
  messageCenter_->RemoveNotification([self notificationID], /*by_user=*/true);
}

- (void)buttonClicked:(id)button {
  messageCenter_->ClickOnNotificationButton([self notificationID],
                                            [button tag]);
}

- (const message_center::Notification*)notification {
  return notification_;
}

- (const std::string&)notificationID {
  return notificationID_;
}

- (void)notificationClicked {
  messageCenter_->ClickOnNotification([self notificationID]);
}

// Private /////////////////////////////////////////////////////////////////////

+ (NSAttributedString*)
    attributedStringForItem:(const message_center::NotificationItem&)item
                       font:(NSFont*)font {
  NSString* text = base::SysUTF16ToNSString(
      item.title + base::UTF8ToUTF16(" ") + item.message);
  NSMutableAttributedString* formattedText =
      [[[NSMutableAttributedString alloc] initWithString:text] autorelease];

  scoped_nsobject<NSMutableParagraphStyle> paragraphStyle(
      [[NSParagraphStyle defaultParagraphStyle] mutableCopy]);
  [paragraphStyle setLineBreakMode:NSLineBreakByTruncatingTail];
  NSDictionary* sharedAttribs = @{
    NSFontAttributeName : font,
    NSParagraphStyleAttributeName : paragraphStyle,
  };
  const NSRange range = NSMakeRange(0, [formattedText length] - 1);
  [formattedText addAttributes:sharedAttribs range:range];

  NSDictionary* titleAttribs = @{
    NSForegroundColorAttributeName :
        gfx::SkColorToCalibratedNSColor(message_center::kRegularTextColor),
  };
  const NSRange titleRange = NSMakeRange(0, item.title.size());
  [formattedText addAttributes:titleAttribs range:titleRange];

  NSDictionary* messageAttribs = @{
    NSForegroundColorAttributeName :
        gfx::SkColorToCalibratedNSColor(message_center::kDimTextColor),
  };
  const NSRange messageRange =
      NSMakeRange(item.title.size() + 1, item.message.size());
  [formattedText addAttributes:messageAttribs range:messageRange];

  return formattedText;
}

- (void)configureCustomBox:(NSBox*)box {
  [box setBoxType:NSBoxCustom];
  [box setBorderType:NSNoBorder];
  [box setTitlePosition:NSNoTitle];
  [box setContentViewMargins:NSZeroSize];
}

- (NSView*)createImageView {
  // Create another box that shows a background color when the icon is not
  // big enough to fill the space.
  NSRect imageFrame = NSMakeRect(0, 0,
       message_center::kNotificationIconSize,
       message_center::kNotificationIconSize);
  scoped_nsobject<NSBox> imageBox([[NSBox alloc] initWithFrame:imageFrame]);
  [self configureCustomBox:imageBox];
  [imageBox setFillColor:gfx::SkColorToCalibratedNSColor(
      message_center::kLegacyIconBackgroundColor)];
  [imageBox setAutoresizingMask:NSViewMinYMargin];

  // Inside the image box put the actual icon view.
  icon_.reset([[NSImageView alloc] initWithFrame:imageFrame]);
  [imageBox setContentView:icon_];

  return imageBox.autorelease();
}

- (void)configureCloseButtonInFrame:(NSRect)rootFrame {
  closeButton_.reset([[HoverImageButton alloc] initWithFrame:NSMakeRect(
      NSMaxX(rootFrame) - message_center::kControlButtonSize,
      NSMaxY(rootFrame) - message_center::kControlButtonSize,
      message_center::kControlButtonSize,
      message_center::kControlButtonSize)]);
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  [closeButton_ setDefaultImage:
      rb.GetNativeImageNamed(IDR_NOTIFICATION_CLOSE).ToNSImage()];
  [closeButton_ setHoverImage:
      rb.GetNativeImageNamed(IDR_NOTIFICATION_CLOSE_HOVER).ToNSImage()];
  [closeButton_ setPressedImage:
      rb.GetNativeImageNamed(IDR_NOTIFICATION_CLOSE_PRESSED).ToNSImage()];
  [[closeButton_ cell] setHighlightsBy:NSOnState];
  [closeButton_ setTrackingEnabled:YES];
  [closeButton_ setBordered:NO];
  [closeButton_ setAutoresizingMask:NSViewMinYMargin];
  [closeButton_ setTarget:self];
  [closeButton_ setAction:@selector(close:)];
}

- (void)configureTitleInFrame:(NSRect)rootFrame {
  NSRect frame = [self currentContentRect];
  frame.size.height = 0;
  title_.reset([self newLabelWithFrame:frame]);
  [title_ setAutoresizingMask:NSViewMinYMargin];
  [title_ setTextColor:gfx::SkColorToCalibratedNSColor(
      message_center::kRegularTextColor)];
  [title_ setFont:[NSFont messageFontOfSize:message_center::kTitleFontSize]];
}

- (void)configureBodyInFrame:(NSRect)rootFrame {
  NSRect frame = [self currentContentRect];
  frame.size.height = 0;
  message_.reset([self newLabelWithFrame:frame]);
  [message_ setAutoresizingMask:NSViewMinYMargin];
  [message_ setTextColor:gfx::SkColorToCalibratedNSColor(
      message_center::kDimTextColor)];
  [message_ setFont:
      [NSFont messageFontOfSize:message_center::kMessageFontSize]];
}

- (NSTextField*)newLabelWithFrame:(NSRect)frame {
  NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
  [label setDrawsBackground:NO];
  [label setBezeled:NO];
  [label setEditable:NO];
  [label setSelectable:NO];
  return label;
}

- (NSRect)currentContentRect {
  DCHECK(icon_);
  DCHECK(closeButton_);

  NSRect iconFrame, contentFrame;
  NSDivideRect([[self view] bounds], &iconFrame, &contentFrame,
      NSWidth([icon_ frame]) + message_center::kIconToTextPadding,
      NSMinXEdge);
  contentFrame.size.width -= NSWidth([closeButton_ frame]);
  return contentFrame;
}

- (string16)wrapText:(const string16&)text
            forField:(NSTextField*)field
    maxNumberOfLines:(size_t)lines {
  gfx::Font font([field font]);
  int width = NSWidth([self currentContentRect]);
  int height = (lines + 1) * font.GetHeight();

  std::vector<string16> wrapped;
  ui::ElideRectangleText(text, font, width, height,
                         ui::WRAP_LONG_WORDS, &wrapped);

  if (wrapped.size() > lines) {
    // Add an ellipsis to the last line. If this ellipsis makes the last line
    // too wide, that line will be further elided by the ui::ElideText below.
    string16 last = wrapped[lines - 1] + UTF8ToUTF16(ui::kEllipsis);
    if (font.GetStringWidth(last) > width)
      last = ui::ElideText(last, font, width, ui::ELIDE_AT_END);
    wrapped.resize(lines - 1);
    wrapped.push_back(last);
  }

  return JoinString(wrapped, '\n');
}

@end
