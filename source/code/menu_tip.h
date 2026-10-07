#ifndef MENU_TIP_H
#define MENU_TIP_H

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <map>

#include <wx/msw/wrapwin.h>

// Hover tooltips for menu items.
//
// wxWidgets has no way to give a wxMenuItem one: there is no SetToolTip, no longHelp constructor
// argument, and no tooltip handling in the MSW implementation at all. The help string it does accept is
// status bar text, which a tray icon menu has no place to show it in. Checked in the sources of 3.1.3,
// 3.2.6 and 3.3.3 alike, so this is not a thing to wait for a newer wxWidgets about, and the scheme of a
// tea therefore has nowhere in the menu to go but the item's own label, which is what it used to be
// written into and what made that menu unreadable.
//
// MenuItemFromPoint takes the menu handle itself rather than a window handle, so no menu window has to
// be hunted down or subclassed. While the menu is tracked a timer asks Windows which item is under the
// cursor and moves a tooltip onto it. Polling is heavier than subclassing the menu window would be, but
// nothing here is in a position to corrupt the tracking of the menu itself, which is the part worth
// protecting.
class MenuItemTip : public wxEvtHandler {
public:
    MenuItemTip();
    ~MenuItemTip();

    // The text to show for a command, keyed by its menu id. Filled in while the menu is being built,
    // which happens before it is tracked.
    void SetItemText(int cmdId, const wxString &text);

    // Starts watching a menu that is about to be shown, and stops again once it has gone. mainMenu is
    // the menu itself and subMenu the one holding the items worth a tip, which may be a popup of its
    // own rather than part of mainMenu.
    void Start(HMENU mainMenu, HMENU subMenu);
    void Stop();

private:
    void OnTick(wxTimerEvent &event);
    int HoveredCommandId();
    void Show(int cmdId, const wxString &text);
    void Hide();

    std::map<int, wxString> _texts;
    wxTimer _timer;
    HMENU _mainMenu;
    HMENU _subMenu;
    HWND _tipWnd;
    int _shownCmdId;
    int _ticksIdle;
    bool _logged;
};

#endif
