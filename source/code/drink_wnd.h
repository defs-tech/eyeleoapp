#ifndef DRINK_WND_H
#define DRINK_WND_H

#include "image_resources.h"
#include "task_mgr.h"
#include "wx/timer.h"
#include "wx/wx.h"

class wxBitmap;

// How long a drink reminder stays up on its own. The hydration one is long enough to notice without
// becoming something to dismiss, and the tea timer uses a much shorter one for the "ready" notice.
static const long kDrinkReminderSec = 15;
static const long kTeaReadyNoticeSec = 5;

// The hydration and tea reminder. Same 209x71 bubble, same bottom right corner and same countdown as
// the long break countdown, with a cup in place of the portrait and a caption above the countdown.
//
// It is advisory, unlike a break overlay: it takes no input at all, so it never captures the keyboard
// and never blocks a click aimed at whatever is behind it. A right click hides it only when the user
// has allowed notifications to be closed by hand, which is the same rule the countdown follows.
class DrinkReminderWindow : public wxFrame, public ITask {
    enum EState {
        STATE_SHOWING,
        STATE_ACTIVE,
        STATE_HIDING,
        STATE_DONE
    };

public:
    // showForMs of 0 means the window stays until it is hidden.
    DrinkReminderWindow(int drinkKind, const wxString &caption, long showForMs);
    virtual ~DrinkReminderWindow();

    // False when it refused to come up because another reminder already holds the slot. The caller has
    // to delete the object then and must not keep the pointer: a window that was never initialised has
    // no task registered and no way to close, so a stored pointer to it would block every later
    // reminder for the rest of the session.
    bool Init(int displayInd);

    // The countdown the bubble shows, which is not the same thing as how long it stays up: the tea
    // timer counts down to the next pour while the bubble itself stays until the pour happens.
    void SetTimeLabel(long msLeft);

    // Stops the number from being shown at all, for the "ready" notice, which has a caption and no
    // countdown of its own to show.
    void HideCountdown();

    // Arms or cancels the self-dismiss. <= 0 means stay until something hides it.
    void SetAutoDismiss(long ms);

    // Replaces the caption in place. The tea timer changes its caption on every steep, and rebuilding
    // the window would drop it under whatever the redraw pushed up.
    void SetCaption(const wxString &caption);

    virtual bool Hide();

    static bool HasInstance() {
        return sInstance != 0;
    }

private:
    void ExecuteTask(float f, long time_went);
    void UpdateTimeLabel();
    void OnClose(wxCloseEvent &event);
    void OnMouseTap(wxMouseEvent &);

    static DrinkReminderWindow *sInstance;

    EState _state;
    int _drinkKind;
    wxString _caption;

    // Two different clocks, which is the point of the split: _autoDismissMs is how long the bubble
    // stays once it is up, and _shownMsLeft is what the number on screen counts. The tea timer needs
    // a bubble that stays put while its countdown runs down to the next pour.
    long _autoDismissMs; // 0 means stay until something hides it
    long _activeMsLeft;  // only counts while the bubble is up and dismissing
    bool _hideCountdown; // the notice asked for no number: the dismissal clock is not one
    long _shownMsLeft;   // what the countdown on screen is counting
    float _alpha;
    bool _preventClosing;

    wxStaticText *_captionText;
    wxStaticText *_timeText;

    DECLARE_EVENT_TABLE()
};

#endif
