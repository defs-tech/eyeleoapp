#ifndef EXCERCISES_H
#define EXCERCISES_H
#include "wx/string.h"

enum EExercise
{
    EXERCISE_NONE = 0,
    EXERCISE_ROLL,
    EXERCISE_LOOK_VERT,
    EXERCISE_LOOK_HORZ,
    EXERCISE_CLOSE_TIGHTLY,
    EXERCISE_BLINK,
    EXERCISE_WINDOW,
    EXERCISE_STRETCH,

    // Added in the port, ids 8, 9 and 10. Nothing existing was removed or reworded: the new
    // exercises simply join the pool, and every id 1..7 keeps its original text and artwork.
    EXERCISE_BLINK_FULLY,
    EXERCISE_NECK_SHOULDERS,
    EXERCISE_PALMING,

    NUM_EXERCISES = EXERCISE_PALMING
};

// The personage bitmaps. The first seven are the original's, 156x98 each. The rest were generated for
// this port at 1x, because wxBitmap uses a PNG's pixel size as it stands: there is no halving step
// here to hide a 2x asset behind, and the panel places the portrait at a fixed point, so a 2x asset
// would simply cover the exercise text.
enum EPersonageFrame {
    PF_DEFAULT = 0,
    PF_LOOK_LEFT,
    PF_LOOK_RIGHT,
    PF_LOOK_UP,
    PF_LOOK_DOWN,
    PF_BLINK,
    PF_CLOSE_TIGHTLY,
    PF_SHOULDERS_UP,
    PF_SHOULDERS_DOWN,
    PF_STRETCH_UP_NEUTRAL,
    PF_STRETCH_UP,
    PF_STRETCH_UPPER,
    PF_EYES_OPEN,
    PF_EYES_CLOSED,
    // The full-figure frames for the long break, 155x340. Unreachable from ExcerciseAnim, which only
    // drives the short break, but they belong here so every bitmap is loaded and freed in one place.
    PF_STRETCH_NEUTRAL,
    PF_STRETCH_LEFT,
    PF_STRETCH_RIGHT,

    NUM_PERSONAGE_FRAMES = PF_STRETCH_RIGHT
};

class wxStaticBitmap;
class wxBitmap;

struct PersonageData {
    wxString _name;
    wxBitmap *_frame[NUM_PERSONAGE_FRAMES + 1];

    PersonageData(wxString const &name);
    ~PersonageData();

    // NULL when the file is missing or would not decode, so a caller can fall back to another frame
    // instead of handing an invalid bitmap to a control.
    wxBitmap *Frame(EPersonageFrame frame) const;
};

// One drawn frame an exercise moves through, and how long it is held. Update() is driven once per
// 100 ms and subtracts 100 per tick, so the hold values are the numbers the original assigned rather
// than clean millisecond counts; keeping them verbatim leaves the cadence of exercises 1..5 exactly as
// it was.
struct FrameStep {
    EPersonageFrame frame;
    int hold;
};

// The frames one exercise cycles through, in order. Returns the count, or 0 for an exercise that does
// not cycle: "look at the window" never animates artwork at all, and neither does a still frame.
//
// Both the frame the panel sizes its picture for and every frame shown afterwards come from this one
// table. The original listed the frames twice instead, once in the constructor and once in the update
// switch, and the two could disagree: an exercise with no constructor case fell through to the default
// head, so the panel sized itself for a 156x98 picture and then drew 138x110 frames into that box.
int FramesForExercise(int excercise, const FrameStep **outSteps);

extern PersonageData *g_Personage;

class ExcerciseAnim {
public:
    ExcerciseAnim(wxStaticBitmap *img, int excerciseNum);

    void Update();

private:
    // Falls back to the default head when a frame is missing, so a broken asset degrades to the old
    // behaviour instead of an empty control.
    void SetFrame(EPersonageFrame frame);

    wxStaticBitmap *_personageImg;
    int _excercise;
    int _time;
    int _state;
};

#endif