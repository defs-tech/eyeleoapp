#include "menu_tip.h"

#include "logging.h"

#include <wx/display.h>
#include <wx/stattext.h>

// Short enough to feel attached to the cursor. The menu is tracked on one modal loop, and this timer has to
// be running inside that loop for any of it to happen, so it is the loop's tick rate that decides how
// quickly a tip can react, not this interval alone.
static const int kTickMs = 120;

// Ticks the pointer has to stay on one item before the tip appears. Six teas are crossed in a moment, and a
// tip that appeared and vanished at every crossing would be noise rather than help.
static const int kTicksBeforeShow = 3;

// Offsets from the pointer, so the tip never sits under the thing that summoned it and never covers the item
// it is explaining.
static const int kOffsetX = 18;
static const int kOffsetY = 24;

// Space between the text and the edge of the tip.
static const int kPadding = 4;

// A tip is a box around one line of text. Anything outside this is not a box around a line of text, and
// showing it would put a window of an incomprehensible size on screen instead.
static const int kMinHintSize = 20;
static const int kMaxHintSize = 1200;

static const wxColour kBg(32, 33, 34);
static const wxColour kFg(255, 255, 255);

MenuItemTip::MenuItemTip()
    : _mainMenu(NULL)
    , _subMenu(NULL)
    , _wnd(NULL)
    , _label(NULL)
    , _hoverCmdId(0)
    , _shownCmdId(0)
    , _ticksIdle(0)
    , _logged(false) {
    // Any handler may own the timer: on MSW it ticks on a hidden window of its own, not on the owner.
    _timer.SetOwner(this);
    Bind(wxEVT_TIMER, &MenuItemTip::OnTick, this);
}

MenuItemTip::~MenuItemTip() {
    Stop();
}

void MenuItemTip::SetItemText(int cmdId, const wxString &text) {
    _texts[cmdId] = text;
}

void MenuItemTip::Start(HMENU mainMenu, HMENU subMenu) {
    Stop();

    _mainMenu = mainMenu;
    _subMenu = subMenu;

    if (!_mainMenu)
        return;

    _hoverCmdId = 0;
    _ticksIdle = 0;
    _logged = false;
    _timer.Start(kTickMs);
}

void MenuItemTip::Stop() {
    _timer.Stop();

    if (_wnd) {
        _wnd->Destroy();
        _wnd = NULL;
        _label = NULL; // owned by _wnd and gone with it
    }

    _mainMenu = NULL;
    _subMenu = NULL;
    _hoverCmdId = 0;
    _shownCmdId = 0;
}

void MenuItemTip::OnTick(wxTimerEvent &WXUNUSED(event)) {
    if (!_mainMenu)
        return;

    HMENU menu = NULL;
    int index = -1;
    int cmdId = 0;
    bool onTea = false;

    if (HoveredItem(menu, index)) {
        MENUITEMINFO item;
        ZeroMemory(&item, sizeof(item));
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID;
        if (::GetMenuItemInfo(menu, (UINT)index, TRUE, &item)) {
            cmdId = (int)item.wID;
            onTea = _texts.find(cmdId) != _texts.end();
        }
    }

    if (!onTea) {
        // Off the tea items altogether, or over one of the other menu's items. Away it goes at once, with
        // no delay, the same as the system would.
        Hide();
        return;
    }

    if (cmdId != _hoverCmdId) {
        // A new item under the pointer: count from zero again, so running along the list does not make the
        // tip flicker, and take away a tip that belonged to the item just left.
        if (_shownCmdId)
            Hide();
        _hoverCmdId = cmdId;
        _ticksIdle = 0;
        return;
    }

    if (++_ticksIdle < kTicksBeforeShow)
        return;

    if (_shownCmdId != cmdId) {
        _shownCmdId = cmdId;
        Show(cmdId, _texts[cmdId]);
    }
}

bool MenuItemTip::HoveredItem(HMENU &menuOut, int &indexOut) {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return false;

    // Three arguments, not the two the documentation used to show: hWnd, hMenu, point. hWnd is NULL here
    // because these are popup menus, which is what it is for.
    //
    // The submenu is tried first: it is drawn over the main menu, so where the two overlap the item
    // actually under the mouse is the submenu's. MenuItemFromPoint returns -1 when the point is outside the
    // menu it is given.
    HMENU menus[2] = {_subMenu, _mainMenu};
    for (int i = 0; i < 2; i++) {
        if (!menus[i])
            continue;
        int index = ::MenuItemFromPoint(NULL, menus[i], pt);
        if (index < 0)
            continue;

        menuOut = menus[i];
        indexOut = index;
        return true;
    }

    return false;
}

void MenuItemTip::Show(int cmdId, const wxString &text) {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return;

    if (!_wnd) {
        _wnd = new wxWindow(NULL, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
        if (!_wnd) {
            logging::msg("menu hint: window not created");
            return;
        }
        _label = new wxStaticText(_wnd, wxID_ANY, text);
        _label->SetBackgroundStyle(wxBG_STYLE_COLOUR);
        _label->SetBackgroundColour(kBg);
        _label->SetForegroundColour(kFg);
        _wnd->SetBackgroundColour(kBg);
    } else {
        _label->SetLabel(text);
    }

    // Sized here and not by asking a window to size itself. Both ways of asking produced a number rather
    // than a size: Fit() on a window with no sizer left the child out and came out 2 by 0, and a sizer
    // whose child carried a proportion of one, laid out before the window had any size at all, came out
    // 1232399200 wide. The label knows how wide its own text is, so it is asked directly and the window is
    // sized from that and nothing else.
    wxSize textSize = _label->GetBestSize();
    int width = textSize.GetWidth() + 2 * kPadding;
    int height = textSize.GetHeight() + 2 * kPadding;

    // Checked, because a number like the one above is not a window anybody can see, and quietly creating
    // it again would put the same mystery back in the log. Better to say it and show nothing.
    if (width < kMinHintSize || width > kMaxHintSize || height < kMinHintSize || height > kMaxHintSize) {
        logging::msg(wxString::Format(L"menu hint: label asked for %dx%d, which is not a tip; nothing shown",
                                       textSize.GetWidth(), textSize.GetHeight()));
        return;
    }

    _label->SetPosition(wxPoint(kPadding, kPadding));
    _label->SetSize(textSize);
    _wnd->SetSize(width, height);

    // Four extended styles, and each one is about not getting in the way. Read as Win32 constants rather
    // than the wxWS_EX_ names because wxWidgets 3.1.3 has no such names, and these have been in winuser.h
    // since well before any of this.
    HWND hwnd = _wnd->GetHandle();
    LONG ex = ::GetWindowLong(hwnd, GWL_EXSTYLE);
    // WS_EX_TOPMOST: a tip drawn behind the menu is a tip nobody sees.
    // WS_EX_TRANSPARENT: clicks pass straight through to the menu, which is still tracking the pointer.
    // WS_EX_NOACTIVATE: never take focus, which would take the menu's away with it.
    // WS_EX_TOOLWINDOW: stay out of the taskbar and out of Alt+Tab, where a menu tip does not belong.
    ex |= WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    ::SetWindowLong(hwnd, GWL_EXSTYLE, ex);

    int x = pt.x + kOffsetX;
    int y = pt.y + kOffsetY;

    // Held inside the work area of the display the pointer is on. Without that the tip goes wherever the
    // arithmetic puts it, which on a second monitor to the right or below is nowhere near the menu that
    // asked for it. In wxWidgets 3.1.3 the display is built by constructor rather than by a static Get(),
    // and GetFromPoint hands back an index or wxNOT_FOUND.
    int displayIndex = wxDisplay::GetFromPoint(wxPoint(pt.x, pt.y));
    if (displayIndex != wxNOT_FOUND) {
        wxDisplay display((unsigned int)displayIndex);
        if (display.IsOk()) {
            wxRect area = display.GetClientArea();
            if (x + width > area.GetRight())
                x = area.GetRight() - width;
            if (y + height > area.GetBottom())
                y = pt.y - height - kOffsetY;
            if (x < area.GetLeft())
                x = area.GetLeft();
            if (y < area.GetTop())
                y = area.GetTop();
        }
    }

    // Positioned and shown here rather than through wx, because a wx call that shows a window is the one
    // thing that would take the focus the menu is holding. SWP_NOACTIVATE says so explicitly.
    ::SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);

    if (!_logged) {
        _logged = true;
        // Three sizes and a position, separately. With one number for everything it was never possible to
        // tell which step produced the bad one, and that cost a build.
        logging::msg(wxString::Format(
            L"menu hint: command %d, label best %dx%d, window %dx%d, at %d,%d, visible=%d, text: %s", cmdId,
            textSize.GetWidth(), textSize.GetHeight(), width, height, x, y, (int)_wnd->IsShown(), text));
    }
}

void MenuItemTip::Hide() {
    if (_shownCmdId && _wnd)
        _wnd->Hide();

    _shownCmdId = 0;
    _hoverCmdId = 0;
    _ticksIdle = 0;
}