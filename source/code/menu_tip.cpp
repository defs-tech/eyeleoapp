#include "menu_tip.h"

#include "logging.h"

#include <commctrl.h>

#include <cstddef>

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
    // Spelled out from wxWidgets' own creation of this control in src/msw/tooltip.cpp, because every part
    // of it matters and the obvious spelling is not it.
    _tipWnd = ::CreateWindowEx(0, TOOLTIPS_CLASS, NULL, TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, ::GetModuleHandle(NULL), NULL);
    if (!_tipWnd) {
        // No tooltip means no tip, which is a small loss. Failing to get one must not take the menu with
        // it, so this is logged and the tick still runs, just with nothing to point.
        logging::msg(wxString::Format(L"menu tooltip: tooltip window not created, error %d", ::GetLastError()));
        return;
    }

    // Topmost by SetWindowPos rather than by an extended style, which is what wx does and works: the tip
    // is otherwise drawn underneath the very menu it is explaining, which amounts to not being drawn.
    //
    // TTS_ALWAYSTIP is why this was ever invisible. It tells the control to show tips regardless of
    // whether the window they belong to is the active one, and the only such window here is the
    // message-only one below, which never becomes active. In the API this was written against the same
    // flag was TTS_ALWAYSONTOP, and the current SDK has taken that one out.
    ::SetWindowPos(_tipWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

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
    // The owner, because this stands in for the message the owner window would have received, which is
    // what wx relays when it forwards one out of wxWindow.
    msg.hwnd = _ownerWnd;
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
    // The V1 size, which is the offset of lpszText plus the field itself and what wxWidgets passes as
    // TTTOOLINFO_V1_SIZE. The two fields the SDK has added since, lParam and lpReserved, are not used
    // here, and claiming them puts a version number on the structure that nothing here fills in.
    tip.cbSize = (UINT)(offsetof(TOOLINFO, lpszText) + sizeof(tip.lpszText));
    // Left deliberately: the module handle is only read for a caption callback, and the SDK spells that
    // field hinst in one version of the structure and hInst in another, so the same line cannot be
    // written for both. wxWidgets leaves it alone for the same reason.
    //
    // The text is cast away from const because that version of the structure dropped the const from this
    // field while the other kept it, and wxChar is wchar_t here.
    tip.lpszText = const_cast<wxChar *>(text.wc_str());
    tip.rect = itemRect;
    // The window made for this and nothing else, and the fixed id under it.
    tip.hwnd = _ownerWnd;
    tip.uId = kToolId;
    // TTF_TRANSPARENT, which wxWidgets sets on every tooltip it creates. It stops a tip being dismissed
    // when its window is reported as having lost focus and then immediately reappearing, which is what
    // happens with a window that is never active in the first place. TTF_CENTERTIP is deliberately not
    // set: with it the tip is centred on the item's rectangle, and without it the tip goes below that
    // rectangle, which is the usual place for one and does not sit on top of the menu.
    tip.uFlags = TTF_TRANSPARENT;

    LRESULT added;
    if (_toolAdded) {
        // Changed through TTM_UPDATETIPTEXT, which is what wx uses, and blanked first: setting the text of
        // a tip that is already up otherwise repaints whatever lies underneath it, which wx tracked as
        // issue #10520.
        tip.lpszText = const_cast<wxChar *>(wxT(""));
        ::SendMessage(_tipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&tip);
        tip.lpszText = const_cast<wxChar *>(text.wc_str());
        added = ::SendMessage(_tipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&tip);
    } else {
        added = ::SendMessage(_tipWnd, TTM_ADDTOOL, 0, (LPARAM)&tip);
    }

    _toolAdded = true;
    _shownCmdId = cmdId;

    // Once, and with everything the control was actually handed, because none of this can be seen from the
    // build machine. What earlier logs settled: the timer does run inside the menu's modal loop, and the
    // item under the mouse is found correctly. What was left unknown is what reaches the control, and the
    // count of tools it holds afterwards says whether it took what it was given.
    if (!_logged) {
        _logged = true;
        LRESULT tools = ::SendMessage(_tipWnd, TTM_GETTOOLCOUNT, 0, 0);
        logging::msg(wxString::Format(
            L"menu tooltip: command %d, rect %d,%d %dx%d, added=%lld, tools=%lld, cbSize=%u, text: %s", cmdId,
            itemRect.left, itemRect.top, itemRect.right - itemRect.left, itemRect.bottom - itemRect.top,
            (long long)added, (long long)tools, (unsigned)tip.cbSize, text));
    }
}

void MenuItemTip::DetachTool() {
    if (!_tipWnd || !_toolAdded)
        return;

    TOOLINFO tip;
    ZeroMemory(&tip, sizeof(tip));
    tip.cbSize = (UINT)(offsetof(TOOLINFO, lpszText) + sizeof(tip.lpszText));
    tip.hwnd = _ownerWnd;
    tip.uId = kToolId;

    // There is no TTM_HIDETIP to call any more, and there is no need for one: the tool goes away, and
    // with it the tip.
    ::SendMessage(_tipWnd, TTM_DELTOOL, 0, (LPARAM)&tip);

    _toolAdded = false;
    _shownCmdId = 0;
}
