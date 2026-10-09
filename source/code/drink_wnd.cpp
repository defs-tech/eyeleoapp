#include "drink_wnd.h"

#include "language_set.h"
#include "logging.h"
#include "main.h"
#include "oscapabilities.h"
#include "wx/button.h"

#include <assert.h>

BEGIN_EVENT_TABLE(DrinkReminderWindow, wxFrame)
EVT_CLOSE(DrinkReminderWindow::OnClose)
EVT_RIGHT_UP(DrinkReminderWindow::OnMouseTap)
END_EVENT_TABLE()

////////////////////////////////////////////////////////////////////////

DrinkReminderWindow *DrinkReminderWindow::sInstance = 0;

DrinkReminderWindow::DrinkReminderWindow(int drinkKind, const wxString &caption, long autoDismissMs)
    : wxFrame(NULL, -1, L"", wxDefaultPosition, wxDefaultSize,
              wxFRAME_TOOL_WINDOW | wxFRAME_SHAPED | wxNO_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP)
    , _state(STATE_SHOWING)
    , _drinkKind(drinkKind)
    , _caption(caption)
    , _autoDismissMs(autoDismissMs)
    , _hideCountdown(false)
    , _activeMsLeft(0)
    , _shownMsLeft(0)
    , _alpha(0.0f)
    , _preventClosing(true)
    , _captionText(0)
    , _timeText(0) {
    SetName("DrinkReminderWindow");
}

bool DrinkReminderWindow::Init(int displayInd) {
    if (DrinkReminderWindow::sInstance) {
        // Not an assert: those are removed in Release, and this is exactly the place where a stray
        // instance would otherwise be overwritten without a word. The flag would then point at a dead
        // window, its own destructor would not clear it, and every later reminder would be dropped
        // silently for the rest of the session. Better to say so and show nothing this once.
        logging::msg("a drink reminder is already up; discarding this one");
        return false;
    }
    DrinkReminderWindow::sInstance = this;

    refillResolutionParams();

    // The same skin the long break countdown uses, so the two read as the same kind of thing.
    wxRegion region(*_backBitmap_notification, *wxWHITE);
    SetShape(region);

    SetBackgroundColour(wxColour(0, 0, 0));
    SetTransparent(0);

    // Bottom right of the work area. osCaps.clientArea already excludes the taskbar, which matters:
    // placed against the full screen the bottom 60 of these 71 points sit under the taskbar and only
    // the top sliver is visible.
    SetSize(_backBitmap_notification->GetSize());
    assert(displayInd < osCaps.numDisplays);
    wxRect displayRect = osCaps.displays[displayInd].clientArea;
    SetPosition(wxPoint(displayRect.GetRight() - GetSize().GetX(),
                        displayRect.GetBottom() - GetSize().GetY()));

    // The bubble is 209x71 and the cup slot ends at x=77, so the text column runs from 82 to 203: a
    // right margin of 6 matching the cup's left one. The caption is given room for two lines rather
    // than one, because whether "Steep 1 of 6, 80°C" fits on a single line depends on the font
    // metrics of the machine it is drawn on, and a second line inside the box is harmless where a
    // clipped second line is not. wxALIGN_CENTER centres vertically as well as horizontally, so the
    // caption stays put whether it takes one line or two.
    _captionText = new wxStaticText(this, wxID_ANY, _caption, wxDefaultPosition, wxSize(121, 32),
                                    wxALIGN_CENTER);
    _captionText->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false));
    _captionText->SetForegroundColour(wxColour(255, 255, 255, 255));
    // Transparent rather than a colour of its own: the bubble behind is black, and a painted
    // rectangle showed up as a grey block sitting on it.
    _captionText->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    _captionText->SetPosition(wxPoint(82, 6));

    _timeText = new wxStaticText(this, wxID_ANY, L"", wxDefaultPosition, wxSize(121, 22), wxALIGN_CENTER);
    _timeText->SetFont(wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false));
    _timeText->SetForegroundColour(wxColour(255, 200, 70, 255));
    _timeText->SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    _timeText->SetPosition(wxPoint(82, 40));

    // The cup. Loaded here rather than at startup so a session that never triggers a reminder never
    // pays for eight bitmaps, and freed again when the window goes.
    LoadDrinkBitmaps();
    wxBitmap *cup = 0;
    if (_drinkKind >= DRINK_GREEN && _drinkKind <= NUM_DRINKS)
        cup = _bmpDrink[_drinkKind];
    if (cup) {
        wxStaticBitmap *logo = new wxStaticBitmap(this, wxID_ANY, *cup);
        // Centred in the 71pt tall bubble and to the left of the text column, which starts at x=85.
        logo->SetPosition(wxPoint(6 + (71 - cup->GetWidth()) / 2, (71 - cup->GetHeight()) / 2));
    }

    _state = DrinkReminderWindow::STATE_SHOWING;
    g_TaskMgr->AddTask(GetName(), 20);
    _alpha = 0.0f;

    Show(true);
    return true;
}

DrinkReminderWindow::~DrinkReminderWindow() {
    if (!getApp()->isFinished())
        g_TaskMgr->RemoveTasks(GetName());

    // The bitmaps are shared, so they are only released when the last reminder goes.
    if (DrinkReminderWindow::sInstance == this) {
        DrinkReminderWindow::sInstance = 0;
        FreeDrinkBitmaps();
        // Without this the app keeps a pointer to a window that no longer exists and reads it the
        // next time the menu is built.
        getApp()->OnDrinkReminderWindowClosed(this);
    }
}

void DrinkReminderWindow::SetTimeLabel(long msLeft) {
    // A countdown asked for again undoes the notice that asked for none. One window serves every
    // reminder of a session, and without this a steep arriving after the cycle notice would keep the
    // tall caption box and, worse, keep the number hidden for the rest of the session.
    if (msLeft > 0 && _hideCountdown) {
        _hideCountdown = false;
        ApplyCaptionLayout(false);
    }
    _shownMsLeft = msLeft;
    UpdateTimeLabel();
}

void DrinkReminderWindow::SetAutoDismiss(long ms) {
    _autoDismissMs = ms;
    // The deadline starts when the bubble finishes fading in, so a caller that arms it during the fade
    // still gets the full time. Forcing it to take effect at once here would have been the alternative,
    // and it is wrong: the tea timer arms the dismissal the moment it creates the window, which is
    // exactly while the window is still fading in.
    _activeMsLeft = (ms > 0 && _state == STATE_ACTIVE) ? ms : 0;
}

void DrinkReminderWindow::SetCaption(const wxString &caption) {
    _caption = caption;
    if (_captionText && _captionText->GetLabel() != caption)
        _captionText->SetLabel(caption);
}

void DrinkReminderWindow::ExecuteTask(float f, long time_went) {
    switch (_state) {
    case STATE_SHOWING: {
        _alpha += 11.0f * f;
        if (_alpha >= 210.0f) {
            _alpha = 210.0f;
            _state = STATE_ACTIVE;
            _activeMsLeft = _autoDismissMs;
            UpdateTimeLabel();
            g_TaskMgr->AddTask(GetName(), 100);
        } else {
            g_TaskMgr->AddTask(GetName(), 20);
        }
        SetTransparent((int)_alpha);
        break;
    }

    case STATE_ACTIVE: {
        // The shown countdown runs whether or not the bubble itself is going to close.
        _shownMsLeft -= time_went;
        UpdateTimeLabel();

        if (_activeMsLeft > 0) {
            _activeMsLeft -= time_went;
            if (_activeMsLeft <= 0) {
                _activeMsLeft = 0;
                _state = STATE_HIDING;
                g_TaskMgr->AddTask(GetName(), 20);
            } else {
                g_TaskMgr->AddTask(GetName(), 100);
            }
        } else {
            // No self-dismiss: stay up until something hides it.
            g_TaskMgr->AddTask(GetName(), 200);
        }
        break;
    }

    case STATE_HIDING: {
        _alpha -= 15.0f * f;
        if (_alpha <= 0.0f) {
            _alpha = 0.0f;
            _state = STATE_DONE;
            _preventClosing = false;
            SetTransparent(0);
            Close();
        } else {
            SetTransparent((int)_alpha);
            g_TaskMgr->AddTask(GetName(), 20);
        }
        break;
    }

    case STATE_DONE:
        break;
    }
}

void DrinkReminderWindow::ApplyCaptionLayout(bool fullHeight) {
    if (!_captionText)
        return;

    // The bubble is 209x71 and the cup slot ends at x=77, so the text column runs from 82 to 203. The
    // tall box leaves 6 points of margin above and below, the same as the right-hand one.
    if (fullHeight)
        _captionText->SetSize(wxSize(121, 59));
    else
        _captionText->SetSize(wxSize(121, 32));
    _captionText->SetPosition(wxPoint(82, 6));
    _captionText->Refresh();
}

void DrinkReminderWindow::HideCountdown() {
    // The "ready" notice has a caption and nothing to count. It dismisses itself on a timer, and without
    // this the dismissal timer would be what the number showed: a 5, 4, 3 counting down to the bubble
    // going away, which reads as a countdown of something nobody asked about. mac shows no number there.
    _hideCountdown = true;
    // With no number under it, the caption has the bubble to itself, and it is handed the whole column
    // rather than the top half of it. The text would fit in the half either way; what changes is where
    // it sits. Left in the top half it would stand above an empty 22 points, which reads as a number
    // that failed to appear, and centred in the full column it stands in the middle of the bubble.
    ApplyCaptionLayout(true);
    UpdateTimeLabel();
}

void DrinkReminderWindow::UpdateTimeLabel() {
    if (!_timeText)
        return;

    if (_hideCountdown) {
        if (_timeText->GetLabel() != wxEmptyString)
            _timeText->SetLabel(wxEmptyString);
        return;
    }

    // Two possible countdowns, and the tea timer sets its own explicitly. When it does not, what the
    // user is waiting for is this bubble closing, so that is what the number shows: the hydration
    // reminder never sets a countdown of its own and used to end up with a blank where the number goes.
    long shown = _shownMsLeft > 0 ? _shownMsLeft : _activeMsLeft;
    if (shown <= 0) {
        // Nothing left to count: the tea is ready, or the bubble is just a caption.
        if (_timeText->GetLabel() != wxEmptyString)
            _timeText->SetLabel(wxEmptyString);
        return;
    }
    int secsLeft = (int)((shown - 1) / 1000 + 1);
    wxString newStr = wxString::Format(L"%d", secsLeft);
    if (_timeText->GetLabel() != newStr)
        _timeText->SetLabel(newStr);
}

bool DrinkReminderWindow::Hide() {
    if (_state == STATE_HIDING)
        return false;

    _state = STATE_HIDING;
    _activeMsLeft = 0;
    g_TaskMgr->AddTask(GetName(), 20);
    return true;
}

void DrinkReminderWindow::OnClose(wxCloseEvent &event) {
    if (!_preventClosing) {
        event.Skip(true);
    }
}

void DrinkReminderWindow::OnMouseTap(wxMouseEvent &) {
    if (getApp()->GetCanCloseNotificationsSetting())
        Hide();
}
