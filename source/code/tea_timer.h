#ifndef TEA_TIMER_H
#define TEA_TIMER_H

#include "image_resources.h"

#include <wx/string.h>

#include <vector>

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
    void LoadConfig(const wxString &path);

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

    // Drops the session. The chosen tea is deliberately not remembered across a restart.
    void Stop();

private:
    static int KindFromName(const wxString &name);
    void ResetToDefaults();

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
