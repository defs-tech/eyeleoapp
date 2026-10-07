#include "menu_tip.h"

#include "logging.h"

#include <commctrl.h>

// Short enough to feel attached to the cursor. The menu is tracked on one modal loop, and this timer has
// to be running inside that loop for any of it to happen, so it is the loop's tick rate that decides
// how often a tip can move, not this interval alone.
static const int kTickMs = 120;

// A fixed tool id is all that is needed: one tooltip, re-pointed at whichever item is under the mouse.
static const UINT_PTR kToolId = 1;

// Windows takes a tooltip down again after its initial delay if the mouse has not moved, so a tip on an
// item the user has settled on would fade out from under a stationary cursor. Re-sending it every couple
// of seconds keeps it up, which is what a tooltip on a menu item is expected to do.
static const int kRefreshTicks = 20;

MenuItemTip::MenuItemTip()
    : _mainMenu(NULL)
    , _subMenu(NULL)
    , _tipWnd(NULL)
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

    // Created here rather than in the constructor because it should exist only while a menu is up, and
    // a tooltip that outlives its menu is the kind of thing that stays on screen looking like a bug.
    _tipWnd = ::CreateWindowEx(0, TOOLTIPS_CLASS, L"", TTS_ALWAYSONTOP, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL,
                               NULL, ::GetModuleHandle(NULL), NULL);
    if (!_tipWnd) {
        // No tooltip means no tip, which is a small loss. Failing to get one must not take the menu with
        // it, so this is logged and the tick still runs, just with nothing to move.
        logging::msg(wxString::Format(L"menu tooltip: tooltip window not created, error %d", ::GetLastError()));
        return;
    }

    _shownCmdId = 0;
    _logged = false;
    _timer.Start(kTickMs);
}

void MenuItemTip::Stop() {
    _timer.Stop();
    Hide();
    _mainMenu = NULL;
    _subMenu = NULL;
    if (_tipWnd) {
        ::DestroyWindow(_tipWnd);
        _tipWnd = NULL;
    }
}

void MenuItemTip::OnTick(wxTimerEvent &WXUNUSED(event)) {
    if (!_tipWnd || !_mainMenu)
        return;

    int cmdId = HoveredCommandId();

    std::map<int, wxString>::const_iterator it = _texts.find(cmdId);
    if (it == _texts.end()) {
        Hide();
        return;
    }

    if (cmdId == _shownCmdId) {
        if (++_ticksIdle >= kRefreshTicks) {
            _ticksIdle = 0;
            Show(cmdId, it->second);
        }
        return;
    }

    _ticksIdle = 0;
    Show(cmdId, it->second);
}

int MenuItemTip::HoveredCommandId() {
    POINT pt;
    if (!::GetCursorPos(&pt))
        return 0;

    // The submenu is tried first: it is drawn over the main menu, so where the two overlap the item
    // actually under the mouse is the submenu's. MenuItemFromPoint wants screen coordinates, and
    // returns -1 when the point is outside the menu it is given.
    HMENU menus[2] = {_subMenu, _mainMenu};
    for (int i = 0; i < 2; i++) {
        if (!menus[i])
            continue;
        int index = ::MenuItemFromPoint((HWND)menus[i], pt);
        if (index < 0)
            continue;

        MENUITEMINFO item;
        ZeroMemory(&item, sizeof(item));
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID;
        if (::GetMenuItemInfo(menus[i], (UINT)index, TRUE, &item))
            return (int)item.wID;
    }

    return 0;
}

void MenuItemTip::Show(int cmdId, const wxString &text) {
    if (!_tipWnd)
        return;

    TOOLINFO tip;
    ZeroMemory(&tip, sizeof(tip));
    tip.cbSize = sizeof(tip);
    // No hwnd and no window of our own: this tip belongs to no window, it just sits at the mouse, which
    // is what an item of a menu wants. TTF_CENTERMOUSE puts it there without any positioning to do.
    tip.uFlags = TTF_CENTERMOUSE;
    tip.hInst = ::GetModuleHandle(NULL);
    tip.uId = kToolId;
    tip.lpszText = (LPCTSTR)text.wc_str();

    ::SendMessage(_tipWnd, TTM_SETTOOLINFO, 0, (LPARAM)&tip);
    ::SendMessage(_tipWnd, TTM_UPDATETIP, 0, (LPARAM)&tip);

    _shownCmdId = cmdId;

    // One line, once, so that a log says whether this reached the screen at all. Everything about it is
    // Win32 behaviour that cannot be checked from the build machine, and the first thing worth knowing
    // is whether the timer runs inside the menu's modal loop on this Windows at all.
    if (!_logged) {
        _logged = true;
        logging::msg(wxString::Format(L"menu tooltip: first tip shown for command %d: %s", cmdId, text));
    }
}

void MenuItemTip::Hide() {
    if (_tipWnd && _shownCmdId)
        ::SendMessage(_tipWnd, TTM_HIDETIP, 0, 0);
    _shownCmdId = 0;
    _ticksIdle = 0;
}
