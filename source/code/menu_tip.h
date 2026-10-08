#ifndef MENU_TIP_H
#define MENU_TIP_H

#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <map>

#include <wx/msw/wrapwin.h>

// Hover tooltips for menu items.
//
// wxWidgets has no way to give a wxMenuItem one: there is no SetToolTip, no longHelp constructor argument
// and no tooltip handling in the MSW implementation at all. The help string it does accept is status bar
// text, which a tray icon menu has no place to show it in. Checked in the sources of 3.1.3, 3.2.6 and
// 3.3.3 alike, so this is not a thing to wait for a newer wxWidgets about, and the scheme of a tea had
// nowhere in the menu to go but the item's own label.
//
// The tooltip control Windows offers was tried first and is not used here. Registering a tool with it works
// exactly as it should: TTM_ADDTOOL succeeds, the control reports the tool back through
// TTM_GETTOOLCOUNT, the item rectangle is right and the pointer is relayed to it on every tick. It simply
// never draws anything, and the reason is not in anything this code controls. So the tip is drawn here
// instead. The item under the pointer is already found reliably through MenuItemFromPoint, and the rest is
// a label, which wx paints without asking anyone's permission.
//
// MenuItemFromPoint takes the menu handle itself rather than a window handle, so no menu window has to be
// hunted down or subclassed. Polling from a timer is heavier than subclassing the menu window would be,
// but nothing here is in a position to disturb the tracking of the menu itself, which is the part worth
// protecting.
class MenuItemTip : public wxEvtHandler {
public:
    MenuItemTip();
    ~MenuItemTip();

    // The text to show for a command, keyed by its menu id. Filled in while the menu is being built, which
    // happens before it is tracked.
    void SetItemText(int cmdId, const wxString &text);

    // Starts watching a menu that is about to be shown, and stops again once it has gone. mainMenu is the
    // menu itself and subMenu the one holding the items worth a tip, which may be a popup of its own rather
    // than part of mainMenu.
    void Start(HMENU mainMenu, HMENU subMenu);
    void Stop();

private:
    void OnTick(wxTimerEvent &event);
    bool HoveredItem(HMENU &menuOut, int &indexOut);
    void Show(int cmdId, const wxString &text);
    void Hide();

    std::map<int, wxString> _texts;
    wxTimer _timer;
    HMENU _mainMenu;
    HMENU _subMenu;

    // Both created on the first tip and destroyed when the menu closes, so nothing is ever left on screen
    // between two menus. _label is a child of _wnd; wx sizes _wnd around it with Fit.
    class wxWindow *_wnd;
    class wxStaticText *_label;

    int _hoverCmdId; // tea under the pointer, 0 for none
    int _shownCmdId; // tea the visible tip belongs to, 0 for none
    int _ticksIdle;  // ticks the pointer has spent on _hoverCmdId
    bool _logged;
};

#endif