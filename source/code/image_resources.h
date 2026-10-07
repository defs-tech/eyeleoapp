#pragma once
#include "wx/bitmap.h"

extern wxBitmap *_backBitmap;
extern wxBitmap *_backBitmap_long;
extern wxBitmap *_backBitmap_notification;
extern wxBitmap *_bmpWindow;
extern wxBitmap *_bmpTitle;

extern wxBitmap *_bmpNotificationLeopard;

// What is being drunk. Water is the hydration reminder and the six teas share the tea timer, so it is
// one list rather than two features: the reminder shows a cup, and the cup tells you which.
enum EDrinkKind {
    DRINK_NONE = 0,
    DRINK_GREEN,
    DRINK_WHITE,
    DRINK_OOLONG,
    DRINK_BLACK,
    DRINK_PUER,
    DRINK_HERBAL,
    DRINK_WATER,

    NUM_DRINKS = DRINK_WATER
};

// The vessel for each kind, indexed by EDrinkKind so the enum and the array cannot drift apart.
// A null entry means the file is missing and the reminder falls back to the leopard, which is what the
// hydration bubble showed before cups existed.
extern wxBitmap *_bmpDrink[NUM_DRINKS + 1];

// Loaded on demand rather than by LoadEyeLeoResources, because eight bitmaps that most sessions never
// show are not worth holding for the life of the process.
void LoadDrinkBitmaps();
void FreeDrinkBitmaps();

void LoadEyeLeoResources();
