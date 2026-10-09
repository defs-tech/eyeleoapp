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
// Two earlier attempts are not used here. Windows' own tooltip control takes the tool, reports it back and
// never draws anything, for reasons that are not in anything this code controls. And a wxWindow standing in
// for the tip came back from GetHandle() with no native window at all, which crashed the app the first time
// that half-built object was used again.
//
// So the window is neither of those: a plain Win32 window, created once before the menu opens, measuring
// and drawing its own text with GDI, and asked which item the pointer is over through MenuItemFromPoint,
// which takes a menu handle rather than a window handle and so needs no menu window found or subclassed.
// What is left in contact with wxWidgets is only the timer and the string.
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
    bool Show(int cmdId, const wxString &text);
    void Hide();

    std::map<int, wxString> _texts;
    wxTimer _timer;
    HMENU _mainMenu;
    HMENU _subMenu;

    // Created in Start(), before the menu is tracked, and destroyed in Stop(). Never created while a menu
    // is up, which is the one circumstance under which window creation had failed.
    HWND _hwnd;
    HFONT _font;

    int _hoverCmdId; // tea under the pointer, 0 for none
    int _shownCmdId; // tea the visible tip belongs to, 0 for none
    int _ticksIdle;  // ticks the pointer has spent on _hoverCmdId
    bool _logged;
};

#endif