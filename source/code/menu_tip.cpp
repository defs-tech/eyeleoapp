#include "menu_tip.h"

#include "logging.h"

#include <string>

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

// A tip is a box around one line of text. Anything outside this is not a box around a line of text.
static const int kMinHintSize = 20;
static const int kMaxHintSize = 1200;

// A light tip rather than a dark one. Near-black read as a black box dropped on a light menu, and the
// tip is read rather than looked at: light ground, dark letters, the way the system's own is.
static const COLORREF kBgColour = RGB(240, 240, 240);
static const COLORREF kFgColour = RGB(0, 0, 0);

// Eight point, a size down from the menu font. The stock GUI font was never bold, it was simply as large
// as the menu's, and a tip that matches the menu in weight competes with it rather than sitting under it.
static const int kFontPointSize = 8;

static const wchar_t *const kHintClassName = L"EyeLeoTeaHint";

// Made once, when the first tip window is created, and destroyed with the last one. One window exists at
// a time, so one brush and one font do.
static HBRUSH g_hintBrush = 0;
static HFONT g_hintFont = 0;

// Four messages, and everything else belongs to Windows.
//
// WM_PAINT draws the whole window, which is why WM_ERASEBKGND refuses to: painting every pixel here means
// there is nothing left to erase and nothing to flicker between.
// WM_NCHITTEST says the window is not there at all as far as the mouse is concerned. The tip sits a little
// below and to the right of the pointer, which puts it over the item below the one being read, and clicking
// that item has to reach the menu.
// WM_MOUSEACTIVATE hands the click back, so a tip can never take the menu's mouse.
static LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        if (dc) {
            if (!g_hintBrush)
                g_hintBrush = ::CreateSolidBrush(kBgColour);

            RECT rc;
            ::GetClientRect(hwnd, &rc);
            if (g_hintBrush)
                ::FillRect(dc, &rc, g_hintBrush);

            // The text is read back off the window rather than kept in a variable alongside it, so there is
            // one copy of it and no chance of drawing something other than what was set.
            int length = ::GetWindowTextLengthW(hwnd);
            std::wstring text;
            if (length > 0) {
                text.resize((size_t)length + 1);
                int got = ::GetWindowTextW(hwnd, &text[0], (int)text.size());
                text.resize(got > 0 ? (size_t)got : 0);
            }

            if (!text.empty()) {
                // The font is taken straight from the variable that made it, not asked of the window with
                // WM_GETFONT. A window of an ordinary class gets no answer to that from the default
                // procedure, and a null font here meant the text went out in the system's default face
                // while the width had been measured in ours: the tip came out larger than the menu's and
                // its ends were cut off, because the window had been sized for the smaller one. Using the
                // same handle that measured it makes the two the same by construction.
                HGDIOBJ oldFont = g_hintFont ? (HGDIOBJ)::SelectObject(dc, g_hintFont) : 0;
                ::SetBkMode(dc, TRANSPARENT);
                ::SetTextColor(dc, kFgColour);
                ::DrawTextW(dc, text.c_str(), (int)text.size(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (oldFont)
                    ::SelectObject(dc, oldFont);
            }
        }
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCDESTROY:
        // Both are ours rather than the system's, so both have to be released, and both here rather than
        // in the caller: Destroy() is deferred, and a frame could still be painted after it returned.
        if (g_hintBrush) {
            ::DeleteObject(g_hintBrush);
            g_hintBrush = 0;
        }
        if (g_hintFont) {
            ::DeleteObject(g_hintFont);
            g_hintFont = 0;
        }
        break;

    default:
        break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Registered once for the life of the process. Only zero really means trouble, because it is only ever
// asked for once, so the "class already exists" case cannot come up and needs no allowance.
static bool EnsureHintClass() {
    static bool tried = false;
    static bool registered = false;

    if (tried)
        return registered;
    tried = true;

    WNDCLASSEX wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HintWndProc;
    wc.hInstance = ::GetModuleHandle(NULL);
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = 0; // every pixel is painted in WM_PAINT
    wc.lpszClassName = kHintClassName;

    registered = ::RegisterClassExW(&wc) != 0;
    if (!registered)
        logging::msg(wxString::Format(L"menu hint: window class not registered, error %d", (int)::GetLastError()));

    return registered;
}

MenuItemTip::MenuItemTip()
    : _mainMenu(NULL)
    , _subMenu(NULL)
    , _hwnd(NULL)
    , _font(NULL)
    , _fontHeight(0)
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

    // Here, and not lazily during the menu: Start() runs before the menu is tracked, and creating a window
    // inside that modal loop is the one thing that failed before. One window per menu, made while the
    // pointer is still on the tray icon, and reused for the whole time the menu is up.
    if (EnsureHintClass()) {
        _hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kHintClassName, L"", WS_POPUP,
                                  0, 0, 1, 1, NULL, NULL, ::GetModuleHandle(NULL), NULL);
        if (_hwnd) {
            // Made rather than borrowed: the stock GUI font has no size of its own to ask for, and the
            // tip is meant to be quieter than the menu. The point size is converted against the window's
            // own DPI, so it stays 8pt on a scaled display instead of shrinking with the system metrics.
            HDC dc = ::GetDC(_hwnd);
            int dpiY = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
            if (dc)
                ::ReleaseDC(_hwnd, dc);
            if (dpiY <= 0)
                dpiY = 96;
            // Negative height, because in GDI that asks for the height of the characters themselves; a
            // positive one asks for the height of the cell they sit in, and the text comes out larger.
            // The division rounds rather than truncating, so 8pt at 96dpi is -11 and not -10.
            _fontHeight = -(kFontPointSize * dpiY + 36) / 72;
            _font = ::CreateFontW(_fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hintFont = _font;
            ::SendMessage(_hwnd, WM_SETFONT, (WPARAM)_font, TRUE);
        }
    }

    // The one number that settles whether this approach is any different from the last: a real window, made
    // before the menu opened.
    logging::msg(wxString::Format(L"menu hint: window created before the menu, handle %p, error %d", (void *)_hwnd,
                                   (int)::GetLastError()));

    _hoverCmdId = 0;
    _ticksIdle = 0;
    _logged = false;
    _timer.Start(kTickMs);
}

void MenuItemTip::Stop() {
    _timer.Stop();

    if (_hwnd) {
        ::DestroyWindow(_hwnd);
        _hwnd = NULL;
    }
    _font = NULL;
    _fontHeight = 0;

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

    if (!_hwnd) {
        // Can only happen if window creation failed back in Start(), and then the log already said so.
        return false;
    }

    const wxChar *chars = text.wc_str();
    std::wstring wtext(chars, (size_t)text.length());

    // Measured under the very font the window draws with, so the two can never disagree about how wide the
    // text is. Asked of GDI rather than of a control, which is what produced a width of zero last time.
    // GetTextExtentPoint32 hands back a single packed SIZE, not the two integers its older sibling
    // GetTextExtentPoint hands back.
    SIZE measured = {0, 0};
    HDC dc = ::GetDC(_hwnd);
    if (dc) {
        HGDIOBJ oldFont = _font ? (HGDIOBJ)::SelectObject(dc, _font) : 0;
        ::GetTextExtentPoint32W(dc, wtext.c_str(), (int)wtext.size(), &measured);
        if (oldFont)
            ::SelectObject(dc, oldFont);
        ::ReleaseDC(_hwnd, dc);
    }
    int textWidth = (int)measured.cx;
    int textHeight = (int)measured.cy;

    if (textWidth <= 0 || textHeight <= 0) {
        logging::msg(wxString::Format(L"menu hint: command %d, GDI measured %dx%d, which is no hint; nothing shown",
                                       cmdId, textWidth, textHeight));
        return false;
    }

    // A couple of pixels more than the text needs, because a tip whose last two letters are shaved off
    // looks broken and the slack costs nothing that can be seen.
    int width = textWidth + 2 * kPadding + 4;
    int height = textHeight + 2 * kPadding;

    if (width < kMinHintSize || width > kMaxHintSize || height < kMinHintSize || height > kMaxHintSize) {
        logging::msg(wxString::Format(L"menu hint: command %d, tip would be %dx%d, which is no tip; nothing shown",
                                       cmdId, width, height));
        return false;
    }

    // Set before showing, so the first paint of the window has its text in it already.
    ::SetWindowTextW(_hwnd, wtext.c_str());

    int x = pt.x + kOffsetX;
    int y = pt.y + kOffsetY;

    // Held inside the work area of the display the pointer is on. Without that the tip goes wherever the
    // arithmetic puts it, which on a second monitor to the right or below is nowhere near the menu that
    // asked for it.
    HMONITOR monitor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (monitor) {
        MONITORINFO info;
        ::ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        if (::GetMonitorInfoW(monitor, &info)) {
            RECT area = info.rcWork;
            if (x + width > area.right)
                x = area.right - width;
            if (y + height > area.bottom)
                y = pt.y - height - kOffsetY;
            if (x < area.left)
                x = area.left;
            if (y < area.top)
                y = area.top;
        }
    }

    // Shown without activating, because the menu holds the focus and this is not going to take it. Then
    // placed, with the size that was measured for this exact text.
    ::ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
    ::SetWindowPos(_hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);

    // What the window manager thinks, since that is the only opinion here worth anything. The first
    // wxWindow that stood in for this reported itself as not shown on every occasion, which was its own
    // answer and not Windows'.
    RECT actual;
    ::ZeroMemory(&actual, sizeof(actual));
    ::GetWindowRect(_hwnd, &actual);

    if (!_logged) {
        _logged = true;
        logging::msg(wxString::Format(
            L"menu hint: command %d, text %dx%d, asked %dx%d at %d,%d, font %d, visible=%d, actual %d,%d %dx%d, text: %s",
            cmdId, textWidth, textHeight, width, height, x, y, _fontHeight, (int)::IsWindowVisible(_hwnd),
            actual.left, actual.top, actual.right - actual.left, actual.bottom - actual.top, text));
    }

    return ::IsWindowVisible(_hwnd) != FALSE;
}

void MenuItemTip::Hide() {
    if (_hwnd)
        ::ShowWindow(_hwnd, SW_HIDE);

    _shownCmdId = 0;
    _hoverCmdId = 0;
    _ticksIdle = 0;
}