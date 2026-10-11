#include "image_resources.h"
#include "logging.h"

#include <wx/ffile.h>

#include <cstring>
#include <map>

wxBitmap *_backBitmap = 0;
wxBitmap *_backBitmap_long = 0;
wxBitmap *_backBitmap_notification = 0;
wxBitmap *_bmpTitle = 0;
wxBitmap *_bmpWindow = 0;
wxBitmap *_bmpNotificationLeopard = 0;

namespace {

// The PNG header, read straight from the file: the eight-byte signature, then the IHDR chunk carrying
// the width, the height, the bit depth and the colour type. wxBitmap will load a JPEG that has been
// renamed to .png and says nothing about the colour type, so the one property that matters here can only
// be answered from the bytes themselves.
struct PngHeader {
    unsigned long width;
    unsigned long height;
    unsigned char bitDepth;
    unsigned char colourType;
};

const unsigned char kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

bool ReadPngHeader(const wxString &path, PngHeader *out) {
    wxFFile file(path, "rb");
    if (!file.IsOpened())
        return false;

    unsigned char buf[26];
    if (file.Read(buf, sizeof(buf)) != sizeof(buf))
        return false;
    if (memcmp(buf, kPngSignature, sizeof(kPngSignature)) != 0)
        return false;
    if (memcmp(buf + 12, "IHDR", 4) != 0)
        return false;

    out->width = ((unsigned long)buf[16] << 24) | ((unsigned long)buf[17] << 16) |
                 ((unsigned long)buf[18] << 8) | (unsigned long)buf[19];
    out->height = ((unsigned long)buf[20] << 24) | ((unsigned long)buf[21] << 16) |
                  ((unsigned long)buf[22] << 8) | (unsigned long)buf[23];
    out->bitDepth = buf[24];
    out->colourType = buf[25];
    return true;
}

std::map<wxString, wxBitmap *> gDrinkBitmaps;

} // namespace

wxBitmap *DrinkBitmap(const wxString &name) {
    if (name.IsEmpty())
        return 0;

    std::map<wxString, wxBitmap *>::iterator it = gDrinkBitmaps.find(name);
    if (it != gDrinkBitmaps.end())
        return it->second ? it->second : 0;

    const wxString file = wxString::Format(L"Resources/tea_%s.png", name);

    PngHeader header;
    if (!ReadPngHeader(file, &header)) {
        logging::msg(wxString::Format(L"vessel %s is not a PNG", file));
        gDrinkBitmaps[name] = 0; // remembered as absent, so a missing file is not looked for again
        return 0;
    }
    if (header.bitDepth != 8 || header.colourType != 6) {
        logging::msg(wxString::Format(L"vessel %s is %d-bit colour type %d, want 8-bit RGBA (6) with a real "
                                      L"alpha channel",
                                      file, (int)header.bitDepth, (int)header.colourType));
        gDrinkBitmaps[name] = 0;
        return 0;
    }
    if (header.width > 71 || header.height > 44) {
        logging::msg(wxString::Format(L"vessel %s is %lux%lu, larger than the 71x44 slot", file, header.width,
                                      header.height));
        gDrinkBitmaps[name] = 0;
        return 0;
    }

    // Relative to the working directory, like every other asset here.
    wxBitmap *bitmap = new wxBitmap(file, wxBITMAP_TYPE_PNG);
    if (!bitmap->IsOk()) {
        logging::msg(wxString::Format(L"vessel %s did not decode", file));
        delete bitmap;
        gDrinkBitmaps[name] = 0;
        return 0;
    }

    gDrinkBitmaps[name] = bitmap;
    return bitmap;
}

void FreeDrinkBitmaps() {
    for (std::map<wxString, wxBitmap *>::iterator it = gDrinkBitmaps.begin(); it != gDrinkBitmaps.end(); ++it)
        delete it->second;
    gDrinkBitmaps.clear();
}

void LoadEyeLeoResources() {
    _backBitmap = new wxBitmap("Resources/skin2.png", wxBITMAP_TYPE_PNG);
    _backBitmap_long = new wxBitmap("Resources/skin3.png", wxBITMAP_TYPE_PNG);
    _backBitmap_notification = new wxBitmap("Resources/skin4.png", wxBITMAP_TYPE_PNG);
    _bmpTitle = new wxBitmap("Resources/eyeleo_title.png", wxBITMAP_TYPE_PNG);
    _bmpWindow = new wxBitmap("Resources/minipause_window.png", wxBITMAP_TYPE_PNG);
    _bmpNotificationLeopard = new wxBitmap("Resources/notification_leopard.png", wxBITMAP_TYPE_PNG);
}