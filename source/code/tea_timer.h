#ifndef TEA_TIMER_H
#define TEA_TIMER_H

#include "image_resources.h"

#include <vector>

// How close a running steep has to be to its end before brewing is allowed to hold the rest of the app
// back: the short break, the hydration reminder and the long break all wait for it. A steep is a minute
// or two at most for every tea but the herbal one, and holding a break for the sake of a countdown that is
// nearly over costs the user nothing. Past this the brewing loses: the app exists to get people out of
// their eyes, and a reminder that can be pushed back eight minutes by starting a pot of chamomile is not
// doing its job.
static const long kTeaEventDeferCapSec = 150;

// The sound a finished steep makes: a file from the system's own media folder, with the event to fall
// back on if this machine does not ship it. Nothing is shipped with the app either way.
//
// The file rather than an alias, because "Windows Unlock" is a file and not an event: PlaySound's
// SND_ALIAS resolves only the names enumerated in playsoundapi.h, all of them System*, Device*, Message*
// and the like, and answers with FALSE and no error for anything else.
//
// More than one name because the system is not consistent about this one, and the difference is a space:
// some editions ship WindowsUnlock.wav and some "Windows Unlock.wav". Both are tried once and the log
// says which one it was, rather than the search happening again on every steep of every session. See
// EyeApp::PlaySteepSound.
static const wchar_t *const kSteepSoundFiles[] = {
    L"WindowsUnlock.wav",
    L"Windows Unlock.wav",
};
static const wchar_t *const kSteepSoundAlias = L"SystemNotify";

// One tea's steeping schedule. seconds are in seconds, and there is a hard ceiling of eight per tea
// because that is the longest shipped schedule (pu-erh); the parser will not read more.
struct TeaSchedule {
    EDrinkKind kind;
    double temperature; // shown in the caption only, it takes no part in the timing
    int seconds[8];
    int count;
};

// Where the schedules come from, and the brewing session that runs off them.
//
// Steeping is deliberately manual. No timer can know when the water has actually been poured off the
// leaves, so an automatic one would be wrong for anyone who looks away, and the fix for that is a
// button rather than a smarter guess. The countdown only runs out; it never moves on by itself.
class TeaTimer {
public:
    TeaTimer();

    // Reads tea.conf if it is there and readable. A missing or malformed file is not an error: the
    // built-in schedules stand, because a typo in a config file should not cost the user their teas.
    // A default-constructed object already holds those built-in schedules.
    //
    // templatePath is the copy the installer put next to the executable. When there is no config in the
    // user's profile yet, that shipped copy is copied over it, so the file the user edits is the one
    // that was shipped and the shipped values are what a fresh install actually runs on. Without it the
    // file would appear only after the first launch, and the installed copy would never be read at all.
    void LoadConfig(const wxString &path, const wxString &templatePath = wxEmptyString);

    // Writes the built-in schedules out in the config format, so the file exists to be found and
    // edited. Called when it is missing, which is the first run.
    void SaveDefaultConfig(const wxString &path) const;

    const TeaSchedule *ScheduleFor(EDrinkKind kind) const;

    // Starts brewing. Fails when there is no schedule, which is the case for water: it is a drink, not
    // something you steep.
    bool Start(EDrinkKind kind);

    // Advances the countdown by elapsedMs. Returns true when the current steep ran out, which is not
    // the same as the tea being finished.
    bool Advance(long elapsedMs);

    // Moves to the next steep. Refused while the current one is still running, because pouring early
    // would silently throw away time the user expected to be counted.
    bool Pour();

    bool IsBrewing() const {
        return _brewing;
    }
    bool IsAwaitingPour() const {
        return _brewing && _awaiting && !_finished;
    }
    bool IsFinished() const {
        return _finished;
    }
    EDrinkKind Kind() const {
        return _kind;
    }
    int Steep() const {
        return _steep;
    }
    int SteepCount() const {
        return _schedule ? _schedule->count : 0;
    }
    long MsLeft() const {
        return _msLeft;
    }

    // Seconds until the next pour, or 0 when there is nothing to pour. Drives the menu item's label.
    int NextPourSeconds() const;

    // How long the next steep will run for once poured, or 0 when there is none. Only meaningful while
    // awaiting a pour: that is the moment the number is worth having, because it says what pressing the
    // item is about to commit you to. Same idea as EBTeaSessionNextSeconds on mac.
    int UpcomingSteepSeconds() const;

    // Drops the session. The chosen tea is deliberately not remembered across a restart.
    void Stop();

private:
    static int KindFromName(const wxString &name);
    void ResetToDefaults();

    // Appends lines to an existing tea.conf. Used only for teas that a later version added, so that
    // the file the user edits names every tea that is actually in use.
    static bool AppendToConfigFile(const wxString &path, const wxString &lines);

    std::vector<TeaSchedule> _schedules;

    const TeaSchedule *_schedule; // into _schedules, never into a local
    EDrinkKind _kind;
    int _steep; // zero based
    long _msLeft;
    bool _brewing;
    bool _awaiting;
    bool _finished;
};

#endif
