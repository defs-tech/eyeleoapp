#include "menu_tip.h"

#include "logging.h"

#include <commctrl.h>

// Short enough to feel attached to the cursor. The menu is tracked on one modal loop, and this timer has
// to be running inside that loop for any of it to happen, so it is the loop's tick rate that decides how
// often a tip can move, not this interval alone.
static const int kTickMs = 120;

// One tip per menu: a fixed tool id identifies it, and its rectangle is moved to whichever item the mouse
// is on. That is how the control works now, so there is nothing to update a second tip for.
static const UINT_PTR kToolId = 1;

MenuItemTip::MenuItemTip()
    : _mainMenu(NULL)
    , _subMenu(NULL)
    , _tipWnd(NULL)
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
    // No TTS_ALWAYSONTOP: it is not in the current SDK any more, along with TTF_CENTERMOUSE, TTM_UPDATETIP
    // and TTM_HIDETIP. The control was reworked around tools with a rectangle, and a tip is now added
    // with TTM_ADDTOOL and moves by having its rectangle changed, showing and hiding itself as the mouse
    // enters and leaves that rectangle. Everything below is written to that API.
    _tipWnd = ::CreateWindowEx(0, TOOLTIPS_CLASS, L"", 0, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL,
                               ::GetModuleHandle(NULL), NULL);
    if (!_tipWnd) {
        // No tooltip means no tip, which is a small loss. Failing to get one must not take the menu with
        // it, so this is logged and the tick still runs, just with nothing to point.
        logging::msg(wxString::Format(L"menu tooltip: tooltip window not created, error %d", ::GetLastError()));
        return;
    }

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
}

void MenuItemTip::OnTick(wxTimerEvent &WXUNUSED(event)) {
    if (!_tipWnd || !_mainMenu)
        return;

    HMENU menu = NULL;
    int index = -1;

    if (!HoveredItem(menu, index)) {
        // Off every item worth a tip, which includes being over the items of the other menu entirely.
        if (_toolAdded)
            DetachTool();
        return;
    }

    MENUITEMINFO item;
    ZeroMemory(&item, sizeof(item));
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID;
    if (!::GetMenuItemInfo(menu, (UINT)index, TRUE, &item))
        return;

    int cmdId = (int)item.wID;
    std::map<int, wxString>::const_iterator it = _texts.find(cmdId);
    if (it == _texts.end()) {
        if (_toolAdded)
            DetachTool();
        return;
    }

    if (cmdId == _shownCmdId && _toolAdded)
        return;

    AttachTool(menu, index, cmdId, it->second);
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
    // No window of our own: the tool is identified by the NULL hwnd and the fixed id, which is a pair
    // like any other, and TTF_CENTERTIP puts the tip on the item's rectangle rather than under the mouse,
    // which is what stops it landing on top of the text it is explaining.
    tip.uId = kToolId;
    tip.uFlags = TTF_CENTERTIP;

    if (_toolAdded)
        ::SendMessage(_tipWnd, TTM_SETTOOLINFO, 0, (LPARAM)&tip);
    else
        ::SendMessage(_tipWnd, TTM_ADDTOOL, 0, (LPARAM)&tip);

    _toolAdded = true;
    _shownCmdId = cmdId;

    // One line, once, so that a log says whether this reached the screen at all. Everything about it is
    // Win32 behaviour that cannot be checked from the build machine, and the first thing worth knowing is
    // whether the timer runs inside the menu's modal loop on this Windows at all.
    if (!_logged) {
        _logged = true;
        logging::msg(wxString::Format(L"menu tooltip: first tip shown for command %d: %s", cmdId, text));
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
