#pragma once
#include "wx/bitmap.h"
#include "wx/string.h"

extern wxBitmap *_backBitmap;
extern wxBitmap *_backBitmap_long;
extern wxBitmap *_backBitmap_notification;
extern wxBitmap *_bmpWindow;
extern wxBitmap *_bmpTitle;

extern wxBitmap *_bmpNotificationLeopard;

// The vessel for a drink, looked up by name: the file is tea_<name>.png and the name is the one from
// tea.conf, so a tea added to that file gets a vessel without anything being recompiled. Water has one
// the same way, under the name "water", and is not a tea: it has no schedule and no line in that file.
//
// Held in a small cache rather than in an array, because the teas are no longer a fixed list.
//
// Returns 0 when the file is missing or does not meet the standard below, and says which in the log.
// The standard: PNG, 8 bits a sample, colour type 6 so there is a real alpha channel, and no larger than
// 71x44 px, which is the corner of the bubble at the 1x this build runs at. Each of those has a failure
// that shows up on screen as a black box or a picture drawn over the text.
wxBitmap *DrinkBitmap(const wxString &name);

// Drops the cached vessels. Called when the reminder closes; they are small but there is no reason to
// hold them between drinks either.
void FreeDrinkBitmaps();

void LoadEyeLeoResources();