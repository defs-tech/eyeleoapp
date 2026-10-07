#include "menu_tip.h"

#include "logging.h"

#include <commctrl.h>

// Short enough to feel attached to the cursor. The menu is tracked on one modal loop, and this timer has
// to be running inside that loop for any of it to happen, so it is the loop's tick rate that decides how
// often a tip can move, not this interval alone.
static const int kTickMs = 120;

// One tip per menu: a fixed tool id identifies it, and its rectangle is moved to whichever item the mouse
// is on.
static const UINT_PTR kToolId = 1;

MenuItemTip::MenuItemTip()
    : _mainMenu(NULL)
    , _subMenu(NULL)
    , _tipWnd(NULL)
    , _ownerWnd(NULL)
    , _shownCmdId(0)
    , _toolAdded(false)
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

    // Created here rather than in the constructor because it should exist only while a menu is up, and
    // a tooltip that outlives its menu is the kind of thing that stays on screen looking like a bug.
    //
    // WS_EX_TOPMOST, because TTS_ALWAYSONTOP is not in the current SDK any more, along with
    // TTF_CENTERMOUSE, TTM_UPDATETIP and TTM_HIDETIP. Without the window being topmost the tip is drawn
    // underneath the menu it is explaining, which is the same as not being drawn.
    _tipWnd = ::CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, L"", 0, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL,
                               NULL, ::GetModuleHandle(NULL), NULL);
    if (!_tipWnd) {
        // No tooltip means no tip, which is a small loss. Failing to get one must not take the menu with
        // it, so this is logged and the tick still runs, just with nothing to point.
        logging::msg(wxString::Format(L"menu tooltip: tooltip window not created, error %d", ::GetLastError()));
        return;
    }

    // The tool needs a window to belong to, and there is none here to belong to. NULL is taken by some
    // builds of the control and quietly produces a tool that never shows on others, so a message-only
    // window is created purely to own it: it takes no input, paints nothing and is never on screen. The
    // predefined STATIC class is used rather than registering one, which would need a window procedure
    // that exists only to be never called.
    _ownerWnd = ::CreateWindowEx(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandle(NULL), NULL);

    // The default initial delay is half a second, which is about right for a tip that follows the mouse
    // across a menu rather than for one that belongs to a window the pointer came to rest on.
    ::SendMessage(_tipWnd, TTM_SETDELAYTIME, TTDT_INITIAL, 250);

    _shownCmdId = 0;
    _toolAdded = false;
    _logged = false;
    _timer.Start(kTickMs);
}

void MenuItemTip::Stop() {
    _timer.Stop();
    DetachTool();
    _mainMenu = NULL;
    _subMenu = NULL;
    if (_tipWnd) {
        ::DestroyWindow(_tipWnd);
        _tipWnd = NULL;
    }
    if (_ownerWnd) {
        ::DestroyWindow(_ownerWnd);
        _ownerWnd = NULL;
    }
}

void MenuItemTip::OnTick(wxTimerEvent &WXUNUSED(event)) {
    if (!_tipWnd || !_mainMenu)
        return;

    UpdateTool();

    // Always, on every path: this is what tells the control where the mouse is, and it needs to hear about
    // the mouse leaving an item just as much as about it arriving on one.
    RelayMouse();
}

void MenuItemTip::UpdateTool() {
    HMENU menu = NULL;
    int index = -1;

    if (!HoveredItem(menu, index)) {
        // Off every item worth a tip, which includes being over the items of the other menu entirely.
        DetachTool();
        return;
    }

    MENUITEMINFO item;
    ZeroMemory(&item, sizeof(item));
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID;
    if (!::GetMenuItemInfo(menu, (UINT)index, TRUE, &item)) {
        DetachTool();
        return;
    }

    int cmdId = (int)item.wID;
    std::map<int, wxString>::const_iterator it = _texts.find(cmdId);
    if (it == _texts.end()) {
        // Over the menu, but not over a tea: a parent item, the separator, or anything else.
        DetachTool();
        return;
    }

    if (cmdId == _shownCmdId && _toolAdded)
        return;

    AttachTool(menu, index, cmdId, it->second);
}

void MenuItemTip::RelayMouse() {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return;

    // The piece that was missing, and the reason a correctly registered tool still never appeared.
    //
    // A tooltip control does not watch the mouse by itself. The window the tooltip belongs to relays
    // WM_MOUSEMOVE down to it, and the control hit-tests those positions against the rectangles of its
    // tools to decide whether there is anything to show. There is no such window here, so nothing was
    // being relayed, and a tool registered perfectly well was never hit-tested and so never displayed.
    // wxWidgets does the same relay out of wxWindow for its own tooltips.
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    msg.message = WM_MOUSEMOVE;
    msg.hwnd = _tipWnd;
    // Since Windows 7 the control wants the calling thread's extra message information in wParam when the
    // relayed message is a mouse move.
    msg.wParam = (WPARAM)::GetMessageExtraInfo();
    msg.lParam = MAKELPARAM(pt.x, pt.y);
    ::SendMessage(_tipWnd, TTM_RELAYEVENT, 0, (LPARAM)&msg);
}

bool MenuItemTip::HoveredItem(HMENU &menuOut, int &indexOut) {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return false;

    // Three arguments, not the two the documentation used to show: hWnd, hMenu, point. hWnd is NULL here
    // because these are popup menus, which is what it is for.
    //
    // The submenu is tried first: it is drawn over the main menu, so where the two overlap the item
    // actually under the mouse is the submenu's. MenuItemFromPoint returns -1 when the point is outside
    // the menu it is given.
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

void MenuItemTip::AttachTool(HMENU menu, int index, int cmdId, const wxString &text) {
    if (!_tipWnd)
        return;

    RECT itemRect;
    if (!::GetMenuItemRect(NULL, menu, (UINT)index, &itemRect)) {
        // Without a rectangle there is nothing for the control to show itself against.
        DetachTool();
        return;
    }

    TOOLINFO tip;
    ZeroMemory(&tip, sizeof(tip));
    tip.cbSize = sizeof(tip);
    // Left deliberately: the module handle is only read for a caption callback, and the SDK spells that
    // field hinst in one version of the structure and hInst in another, so the same line cannot be
    // written for both. wxWidgets leaves it alone for the same reason.
    //
    // The text is cast away from const because that version of the structure dropped the const from this
    // field while the other kept it, and wxChar is wchar_t here.
    tip.lpszText = const_cast<wxChar *>(text.wc_str());
    tip.rect = itemRect;
    // The window made for this and nothing else, and the fixed id under it. TTF_CENTERTIP puts the tip on
    // the item's rectangle rather than under the mouse, which is what stops it landing on top of the text
    // it is explaining.
    tip.hwnd = _ownerWnd;
    tip.uId = kToolId;
    tip.uFlags = TTF_CENTERTIP;

    LRESULT added = _toolAdded ? ::SendMessage(_tipWnd, TTM_SETTOOLINFO, 0, (LPARAM)&tip)
                               : ::SendMessage(_tipWnd, TTM_ADDTOOL, 0, (LPARAM)&tip);

    _toolAdded = true;
    _shownCmdId = cmdId;

    // Once, and with the rectangle in it, because the rectangle is the thing most likely to be wrong on
    // the machine this runs on and it cannot be seen from the build. The log already proved the timer
    // runs inside the menu's modal loop and that the item under the mouse is found correctly, so what is
    // left to find out is what the control was given to work with.
    if (!_logged) {
        _logged = true;
        logging::msg(wxString::Format(
            L"menu tooltip: command %d, rect %d,%d %dx%d, add=%lld, text: %s", cmdId, itemRect.left, itemRect.top,
            itemRect.right - itemRect.left, itemRect.bottom - itemRect.top, (long long)added, text));
    }
}

void MenuItemTip::DetachTool() {
    if (!_tipWnd || !_toolAdded)
        return;

    TOOLINFO tip;
    ZeroMemory(&tip, sizeof(tip));
    tip.cbSize = sizeof(tip);
    tip.hwnd = NULL;
    tip.uId = kToolId;

    // There is no TTM_HIDETIP to call any more, and there is no need for one: the tool goes away, and
    // with it the tip.
    ::SendMessage(_tipWnd, TTM_DELTOOL, 0, (LPARAM)&tip);

    _toolAdded = false;
    _shownCmdId = 0;
}
