#include "excercises.h"
#include "image_resources.h"
#include "logging.h"
#include "wx/bitmap.h"
#include "wx/statbmp.h"

PersonageData *g_Personage = 0;

// Indexed by EPersonageFrame, so the order here and the enum cannot drift apart without the compiler
// noticing the initializer being one entry short.
static const wchar_t *const kFrameFiles[] = {
    L"default",
    L"look_left",
    L"look_right",
    L"look_up",
    L"look_down",
    L"blink",
    L"close_tightly",
    L"shoulders_up",
    L"shoulders_down",
    L"stretch_up_neutral",
    L"stretch_up",
    L"stretch_upper",
    L"eyes_open",
    L"eyes_closed",
    L"stretch_neutral",
    L"stretch_left",
    L"stretch_right",
};

// The frames each exercise cycles through, in order. The original showed the default head first and
// only entered its loop on the first tick, so it listed that same frame twice for the blinking
// exercises; the cycle starts directly on the first move instead. Roll, look and blink keep the
// original's cadence exactly.
static const FrameStep kLookHorz[] = {{PF_LOOK_RIGHT, 1200}, {PF_LOOK_LEFT, 1200}};
static const FrameStep kLookVert[] = {{PF_LOOK_DOWN, 1200}, {PF_LOOK_UP, 1200}};
static const FrameStep kRoll[] = {
    {PF_LOOK_LEFT, 1200},
    {PF_LOOK_UP, 1200},
    {PF_LOOK_RIGHT, 1200},
    {PF_LOOK_DOWN, 1200},
};
static const FrameStep kBlink[] = {{PF_DEFAULT, 200}, {PF_BLINK, 200}};
static const FrameStep kCloseTightly[] = {{PF_DEFAULT, 2000}, {PF_CLOSE_TIGHTLY, 2000}};
// Same cadence as the plain blink, but the closed-eyes frame is held longer than the open one: the
// point is a complete, unhurried blink, not a fast one.
static const FrameStep kBlinkFully[] = {{PF_DEFAULT, 200}, {PF_CLOSE_TIGHTLY, 500}};
// Two drawn frames rather than a transform. Moving one baked picture reads as a sliding image rather
// than as shoulders; swapping between two aligned poses reads as the movement itself.
static const FrameStep kShoulders[] = {{PF_SHOULDERS_UP, 350}, {PF_SHOULDERS_DOWN, 350}};
// Arms down, arms up, then a backward bend, then back to standing. The original never animated this
// exercise at all, so the three frames and this cadence are both new.
static const FrameStep kStretch[] = {
    {PF_STRETCH_UP_NEUTRAL, 500},
    {PF_STRETCH_UP, 500},
    {PF_STRETCH_UPPER, 500},
};
// Eyes open, then covered by both paws. Held longer than it takes to show, because the point is the
// darkness and the warmth of the paws, not a blink.
static const FrameStep kPalming[] = {{PF_EYES_OPEN, 700}, {PF_EYES_CLOSED, 500}};

int FramesForExercise(int excercise, const FrameStep **outSteps) {
    const FrameStep *steps = 0;
    int count = 0;

    switch (excercise) {
    case EXERCISE_LOOK_HORZ:
        steps = kLookHorz;
        count = sizeof(kLookHorz) / sizeof(kLookHorz[0]);
        break;
    case EXERCISE_LOOK_VERT:
        steps = kLookVert;
        count = sizeof(kLookVert) / sizeof(kLookVert[0]);
        break;
    case EXERCISE_ROLL:
        steps = kRoll;
        count = sizeof(kRoll) / sizeof(kRoll[0]);
        break;
    case EXERCISE_BLINK:
        steps = kBlink;
        count = sizeof(kBlink) / sizeof(kBlink[0]);
        break;
    case EXERCISE_CLOSE_TIGHTLY:
        steps = kCloseTightly;
        count = sizeof(kCloseTightly) / sizeof(kCloseTightly[0]);
        break;
    case EXERCISE_BLINK_FULLY:
        steps = kBlinkFully;
        count = sizeof(kBlinkFully) / sizeof(kBlinkFully[0]);
        break;
    case EXERCISE_NECK_SHOULDERS:
        steps = kShoulders;
        count = sizeof(kShoulders) / sizeof(kShoulders[0]);
        break;
    case EXERCISE_STRETCH:
        steps = kStretch;
        count = sizeof(kStretch) / sizeof(kStretch[0]);
        break;
    case EXERCISE_PALMING:
        steps = kPalming;
        count = sizeof(kPalming) / sizeof(kPalming[0]);
        break;
    case EXERCISE_NONE:
    case EXERCISE_WINDOW:
    default:
        // Looking out of the window never animates, which the original achieved by returning before
        // the timer even ran down.
        break;
    }

    if (outSteps)
        *outSteps = steps;
    return count;
}

PersonageData::PersonageData(wxString const &name)
    : _name(name) {
    for (int i = 0; i <= NUM_PERSONAGE_FRAMES; i++)
        _frame[i] = 0;

    wxString path = wxString::Format(L"Personages/%s/%s_", name, name);

    for (int i = 0; i <= NUM_PERSONAGE_FRAMES; i++) {
        // Every one of these is relative to the working directory, which is also why the app has to
        // be started from bin. A frame that is missing is reported and left null rather than being
        // handed to a control, which would draw nothing and say nothing about why.
        _frame[i] = new wxBitmap(path + kFrameFiles[i] + L".png", wxBITMAP_TYPE_PNG);
        if (!_frame[i]->IsOk()) {
            logging::msg(wxString::Format(L"personage frame %s is missing or unreadable", kFrameFiles[i]));
            delete _frame[i];
            _frame[i] = 0;
        }
    }
}

PersonageData::~PersonageData() {
    for (int i = 0; i <= NUM_PERSONAGE_FRAMES; i++)
        delete _frame[i];
}

wxBitmap *PersonageData::Frame(EPersonageFrame frame) const {
    if (frame < 0 || frame > NUM_PERSONAGE_FRAMES)
        return 0;
    return _frame[frame];
}

ExcerciseAnim::ExcerciseAnim(wxStaticBitmap *img, int excerciseNum)
    : _personageImg(img)
    , _excercise(excerciseNum)
    , _state(0) {
    // The window is the one exercise whose artwork is not a personage frame.
    if (_excercise == EXERCISE_WINDOW) {
        _personageImg->SetBitmap(*_bmpWindow);
    } else {
        const FrameStep *steps = 0;
        int count = FramesForExercise(_excercise, &steps);
        SetFrame(count > 0 ? steps[0].frame : PF_DEFAULT);
    }

    _time = 400;
}

void ExcerciseAnim::SetFrame(EPersonageFrame frame) {
    wxBitmap *bmp = g_Personage->Frame(frame);
    if (!bmp)
        bmp = g_Personage->Frame(PF_DEFAULT);
    if (bmp)
        _personageImg->SetBitmap(*bmp);
}

void ExcerciseAnim::Update() {
    const FrameStep *steps = 0;
    int count = FramesForExercise(_excercise, &steps);
    if (count == 0)
        return;

    _time -= 100;

    if (_time <= 0) {
        _state = (_state + 1) % count;
        SetFrame(steps[_state].frame);
        _time = steps[_state].hold;
    }
}