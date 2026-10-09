#include "image_resources.h"
#include "logging.h"

wxBitmap *_backBitmap = 0;
wxBitmap *_backBitmap_long = 0;
wxBitmap *_backBitmap_notification = 0;
wxBitmap *_bmpTitle = 0;
wxBitmap *_bmpWindow = 0;
wxBitmap *_bmpNotificationLeopard = 0;

// Indexed by EDrinkKind, which starts at 1, so index 0 stays unused and the two enums line up.
static const wchar_t *const kDrinkFiles[] = {
    L"",
    L"tea_green",
    L"tea_white",
    L"tea_oolong",
    L"tea_black",
    L"tea_red",
    L"tea_puer",
    L"tea_herbal",
    L"tea_water",
};

wxBitmap *_bmpDrink[NUM_DRINKS + 1];

void LoadDrinkBitmaps() {
    for (int i = DRINK_GREEN; i <= NUM_DRINKS; i++) {
        if (_bmpDrink[i])
            continue;

        // Relative to the working directory, like every other asset here.
        _bmpDrink[i] = new wxBitmap(wxString::Format(L"Resources/%s.png", kDrinkFiles[i]),
                                    wxBITMAP_TYPE_PNG);
        if (!_bmpDrink[i]->IsOk()) {
            logging::msg(wxString::Format(L"vessel %s is missing or unreadable", kDrinkFiles[i]));
            delete _bmpDrink[i];
            _bmpDrink[i] = 0;
        }
    }
}

void FreeDrinkBitmaps() {
    for (int i = DRINK_GREEN; i <= NUM_DRINKS; i++) {
        delete _bmpDrink[i];
        _bmpDrink[i] = 0;
    }
}

void LoadEyeLeoResources() {
    _backBitmap = new wxBitmap("Resources/skin2.png", wxBITMAP_TYPE_PNG);
    _backBitmap_long = new wxBitmap("Resources/skin3.png", wxBITMAP_TYPE_PNG);
    _backBitmap_notification = new wxBitmap("Resources/skin4.png", wxBITMAP_TYPE_PNG);
    _bmpTitle = new wxBitmap("Resources/eyeleo_title.png", wxBITMAP_TYPE_PNG);
    _bmpWindow = new wxBitmap("Resources/minipause_window.png", wxBITMAP_TYPE_PNG);
    _bmpNotificationLeopard = new wxBitmap("Resources/notification_leopard.png", wxBITMAP_TYPE_PNG);
}