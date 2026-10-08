#include "menu_tip.h"

#include "logging.h"

#include <wx/display.h>
#include <wx/stattext.h>

#include <wx/dc.h>

// Short enough to feel attached to the cursor. The menu is tracked on one modal loop, and this timer has to
// be running inside that loop for any of it to happen, so it is the loop's tick rate that decides how
// quickly a tip can react, not this interval alone.
static const int kTickMs = 120;

// Ticks the pointer has to stay on one item before the tip appears. Six teas are crossed in a moment, and a
// tip that appeared and vanished at every crossing would be noise rather than help.
static const int kTicksBeforeShow = 3;

// Offsets from the pointer, so the tip never sits under the thing that summoned it.
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

// The window procedure this one replaced, kept so that everything except the hit test is handled exactly as
// it was. One tip exists at a time, so one saved procedure is enough.
static WNDPROC g_prevWndProc = 0;

// The tip never takes the mouse.
//
// It sits a little below and to the right of the pointer, which puts it over the item below the one being
// read. Clicking that item has to reach the menu, so the hit test says the window is not there at all.
// WS_EX_TRANSPARENT was here first and did nothing of the kind: it changes painting order, and letting
// clicks pass through a top level window is WS_EX_LAYERED's job, which would mean a layered window.
static LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;

    if (g_prevWndProc)
        return ::CallWindowProc(g_prevWndProc, hwnd, msg, wParam, lParam);

    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

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

    // g_prevWndProc is deliberately left alone. Destroy() only queues the window for destruction, so ours is
    // still installed and still handling messages for a moment; clearing the procedure it delegates to now
    // would mean those messages went to DefWindowProc and bypassed wx entirely. It is overwritten at the
    // next install, before anything can read it.

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

    if (_shownCmdId == cmdId)
        return;

    // Only marked as shown if it really appeared. Marking it before the attempt, and not clearing it when
    // the attempt failed, meant that one refused attempt ended the tip for that item until the pointer left
    // and came back.
    if (Show(cmdId, _texts[cmdId]))
        _shownCmdId = cmdId;
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

bool MenuItemTip::Show(int cmdId, const wxString &text) {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return false;

    if (!_wnd) {
        _wnd = new wxWindow(NULL, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
        if (!_wnd)
            return false;

        HWND hwnd = _wnd->GetHandle();
        if (!hwnd) {
            // The object exists but nothing does on screen to put on it. Worth saying, because every
            // message after this point would otherwise talk about a window that does not exist.
            logging::msg(wxString::Format(L"menu hint: command %d, window has no native handle", cmdId));
            return false;
        }

        // Before the window is ever shown, because that is when WS_EX_NOACTIVATE has to be in place.
        // WS_EX_TOPMOST: a tip drawn behind the menu is a tip nobody sees.
        // WS_EX_NOACTIVATE: it must not take the focus the menu is holding, however it comes up.
        // WS_EX_TOOLWINDOW: stay out of the taskbar and out of Alt+Tab, where a menu tip does not belong.
        LONG ex = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        ::SetWindowLong(hwnd, GWL_EXSTYLE, ex);

        g_prevWndProc = reinterpret_cast<WNDPROC>(
            ::SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HintWndProc)));

        _label = new wxStaticText(_wnd, wxID_ANY, text);
        _label->SetBackgroundStyle(wxBG_STYLE_COLOUR);
        _label->SetBackgroundColour(kBg);
        _label->SetForegroundColour(kFg);
        _wnd->SetBackgroundColour(kBg);
    } else {
        _label->SetLabel(text);
    }

    // Measured off the label's own device context rather than asked of the control. GetBestSize() returns
    // whatever the control has measured so far, and the first time it is asked, before it has laid itself
    // out at all, it answers 0 by 16: no width at all, with a perfectly good height.
    wxSize textSize;
    {
        wxDC dc(_label);
        textSize = dc.GetTextExtent(text);
    }

    int width = textSize.GetWidth() + 2 * kPadding;
    int height = textSize.GetHeight() + 2 * kPadding;

    // Checked, because a number no tip could be is not worth putting on screen, and quietly creating one
    // again would put the same mystery back in the log.
    if (width < kMinHintSize || width > kMaxHintSize || height < kMinHintSize || height > kMaxHintSize) {
        logging::msg(wxString::Format(L"menu hint: command %d, text measures %dx%d, which is no hint; nothing shown",
                                       cmdId, textSize.GetWidth(), textSize.GetHeight()));
        return false;
    }

    _label->SetPosition(wxPoint(kPadding, kPadding));
    _label->SetSize(textSize);
    _wnd->SetSize(width, height);

    HWND hwnd = _wnd->GetHandle();

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

    // ShowWindow, and nothing else. This is the whole reason this hint has its own code rather than
    // wxWindow::Show(): wx sets m_isShown to true when a window is constructed, and wxWindowMSW::Show()
    // begins by calling wxWindowBase::Show(), which returns without touching the window when the flag
    // already says what was asked for. So the first Show() on a fresh window does nothing at all. The
    // underlying HWND is meanwhile created without WS_VISIBLE, so nobody is showing it. Asking the window
    // manager directly is the one thing here that cannot be argued with.
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    // Placed after being shown, and shown no more: the size asked for is then the size it has.
    ::SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

    // What the window manager thinks, not what wx believes. Everything earlier in this log was wx's own
    // opinion, and wx's opinion on this subject is worth nothing: it calls a window shown that was never
    // shown.
    RECT actual;
    ::GetWindowRect(hwnd, &actual);
    LONG styles = ::GetWindowLong(hwnd, GWL_EXSTYLE);

    if (!_logged) {
        _logged = true;
        logging::msg(wxString::Format(
            L"menu hint: command %d, asked %dx%d at %d,%d, visible=%d, actual %d,%d %dx%d, ex=0x%lx, text: %s",
            cmdId, width, height, x, y, (int)::IsWindowVisible(hwnd), actual.left, actual.top,
            actual.right - actual.left, actual.bottom - actual.top, (unsigned long)styles, text));
    }

    return ::IsWindowVisible(hwnd) != FALSE;
}

void MenuItemTip::Hide() {
    if (_wnd) {
        HWND hwnd = _wnd->GetHandle();
        // Straight past wx again, for the same reason as showing: wxWindowBase::Show(false) only changes
        // the flag and, if the flag already said false, does not even do that.
        if (hwnd)
            ::ShowWindow(hwnd, SW_HIDE);
    }

    _shownCmdId = 0;
    _hoverCmdId = 0;
    _ticksIdle = 0;
}