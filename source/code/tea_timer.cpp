#include "tea_timer.h"

#include "logging.h"

#include <wx/arrstr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/string.h>

// The shipped schedules. Green and white are short and stepped, oolong and black longer, pu-erh longest
// of all, and herbal is not tea at all but a dried blend that wants minutes rather than seconds.
static const TeaSchedule kDefaultSchedules[] = {
    {DRINK_GREEN, 80.0, {10, 15, 20, 25, 30, 35, 0, 0}, 6},
    {DRINK_WHITE, 85.0, {10, 15, 20, 25, 30, 0, 0, 0}, 5},
    {DRINK_OOLONG, 85.0, {15, 25, 35, 45, 60, 75, 0, 0}, 6},
    {DRINK_BLACK, 90.0, {20, 30, 40, 50, 60, 70, 0, 0}, 6},
    {DRINK_RED, 95.0, {7, 10, 15, 20, 25, 30, 0, 0}, 6},
    {DRINK_PUER, 99.0, {20, 30, 40, 50, 60, 70, 80, 90}, 8},
    {DRINK_HERBAL, 100.0, {300, 360, 420, 480, 0, 0, 0, 0}, 4},
};

static const int kNumDefaultSchedules =
    sizeof(kDefaultSchedules) / sizeof(kDefaultSchedules[0]);

// Accepted in tea.conf in either language, because the person editing it is as likely to be reading
// this file as the app. Case and surrounding spaces do not matter.
static const wchar_t *const kKindNames[][4] = {
    {L"green", L"зеленый", L"зелёный", 0},
    {L"white", L"белый", 0, 0},
    {L"oolong", L"oolon", L"улун", 0},
    {L"black", L"чёрный", L"черный", 0},
    {L"red", L"красный", L"красн", 0},
    {L"puer", L"pu-er", L"пуэр", 0},
    {L"herbal", L"травяной", L"grass", 0},
};

int TeaTimer::KindFromName(const wxString &name) {
    wxString wanted = name.Strip(wxString::both);
    wanted.MakeLower();

    for (int i = 0; i < kNumDefaultSchedules; i++) {
        for (int n = 0; n < 4; n++) {
            const wchar_t *candidate = kKindNames[i][n];
            if (candidate && wanted == candidate)
                return kDefaultSchedules[i].kind;
        }
    }
    return DRINK_NONE;
}

void TeaTimer::ResetToDefaults() {
    _schedules.clear();
    for (int i = 0; i < kNumDefaultSchedules; i++)
        _schedules.push_back(kDefaultSchedules[i]);

    _schedule = 0;
    _kind = DRINK_NONE;
    _steep = 0;
    _msLeft = 0;
    _brewing = false;
    _awaiting = false;
    _finished = false;
}

TeaTimer::TeaTimer() {
    ResetToDefaults();
}

// Both helpers return false rather than throwing: for seeding a config that is a message, not a
// condition to recover from, and the caller falls back to the built-in schedules.
static bool ReadTextFile(const wxString &path, wxString *out) {
    wxFFile file(path, "rb");
    if (!file.IsOpened() || !file.ReadAll(out, wxConvUTF8))
        return false;
    return true;
}

static bool CopyTextFile(const wxString &from, const wxString &to) {
    wxString text;
    if (!ReadTextFile(from, &text))
        return false;

    wxFFile out(to, "wb");
    if (!out.IsOpened() || !out.Write(text))
        return false;
    return true;
}

// The name written back has to be one KindFromName accepts, so the English spelling is used. Empty for a
// kind with no schedule, water among them.
static wxString ScheduleName(EDrinkKind kind) {
    switch (kind) {
    case DRINK_GREEN:
        return wxT("green");
    case DRINK_WHITE:
        return wxT("white");
    case DRINK_OOLONG:
        return wxT("oolong");
    case DRINK_BLACK:
        return wxT("black");
    case DRINK_RED:
        return wxT("red");
    case DRINK_PUER:
        return wxT("puer");
    case DRINK_HERBAL:
        return wxT("herbal");
    default:
        return wxEmptyString;
    }
}

// One schedule as a config line, or empty if it cannot be written as one.
static wxString ScheduleLine(const TeaSchedule &schedule) {
    wxString name = ScheduleName(schedule.kind);
    if (name.IsEmpty() || schedule.count <= 0)
        return wxEmptyString;

    wxString line = wxString::Format(wxT("%s, %.0f"), name, schedule.temperature);
    for (int k = 0; k < schedule.count; k++)
        line += wxString::Format(wxT(", %d"), schedule.seconds[k]);
    return line;
}

bool TeaTimer::AppendToConfigFile(const wxString &path, const wxString &lines) {
    // The whole file is rewritten rather than opened for append, because the last line of a file a user
    // has been editing need not end in a newline and appending onto that one would run the new tea into
    // the old one. What was there is kept byte for byte; the new lines go after it.
    wxString text;
    if (!ReadTextFile(path, &text)) {
        logging::msg("tea.conf could not be re-read, the missing teas were not written out");
        return false;
    }

    if (!text.IsEmpty() && text[text.length() - 1] != '\n')
        text += wxT("\n");

    wxFFile out(path, "wb");
    if (!out.IsOpened() || !out.Write(text + lines)) {
        logging::msg("tea.conf could not be written; the teas added in this version are still in use");
        return false;
    }
    return true;
}

void TeaTimer::LoadConfig(const wxString &path, const wxString &templatePath) {
    // Back to the shipped schedules first, so a reload after an edit never leaves a half-applied file
    // behind.
    ResetToDefaults();

    // wxFFile does all of it: opening for reading doubles as the existence test, so wxFile is not
    // needed at all. Read as UTF-8, so a file written on any platform comes in the same way.
    wxString text;
    if (!ReadTextFile(path, &text)) {
        // First run on this machine. Prefer the copy the installer shipped over writing the built-ins:
        // the two should agree, and if they ever do not, the shipped file is the one the user can see.
        if (!templatePath.IsEmpty() && CopyTextFile(templatePath, path) && ReadTextFile(path, &text)) {
            logging::msg("tea.conf seeded from the copy shipped with the installer");
        } else {
            logging::msg("tea.conf not present, writing out the built-in schedules");
            SaveDefaultConfig(path);
            return;
        }
    }
    if (text.IsEmpty()) {
        logging::msg("tea.conf is empty or not valid UTF-8, using the built-in schedules");
        return;
    }

    // Which teas the file names, so that the ones it does not can be written out afterwards.
    bool found[NUM_DRINKS] = {false};

    // One tea per line: name, temperature, then the steeps in seconds.
    //   puer, 99, 20, 30, 40, 50, 60, 70, 80, 90
    wxString rest = text;
    while (!rest.IsEmpty()) {
        wxString line = rest.BeforeFirst('\n');
        rest = rest.AfterFirst('\n');

        line = line.Strip(wxString::both);
        if (line.IsEmpty() || line[0] == '#')
            continue;

        // name, temperature, then the steeps.
        wxArrayString parts;
        wxString fields = line;
        while (!fields.IsEmpty()) {
            wxString field = fields.BeforeFirst(',');
            fields = fields.AfterFirst(',');
            field = field.Strip(wxString::both);
            if (!field.IsEmpty())
                parts.Add(field);
        }
        if (parts.GetCount() < 3)
            continue;

        EDrinkKind kind = (EDrinkKind)KindFromName(parts[0]);
        // Water has no schedule, and an unknown name is a typo: skip the line and keep the default.
        if (kind == DRINK_NONE || kind == DRINK_WATER)
            continue;

        TeaSchedule schedule;
        schedule.kind = kind;
        schedule.count = 0;
        schedule.temperature = 0.0;
        for (int s = 0; s < 8; s++)
            schedule.seconds[s] = 0;

        // A non-positive steep would make the countdown sit at zero forever, so those are dropped.
        for (size_t p = 2; p < parts.GetCount() && schedule.count < 8; p++) {
            wxString value = parts[p].Strip(wxString::both);
            if (value.IsEmpty())
                continue;
            long seconds = 0;
            if (!value.ToLong(&seconds) || seconds <= 0)
                continue;
            schedule.seconds[schedule.count++] = (int)seconds;
        }

        if (schedule.count == 0)
            continue;

        double temperature = 0.0;
        if (parts[1].Strip(wxString::both).ToDouble(&temperature) && temperature > 0.0)
            schedule.temperature = temperature;
        else
            schedule.temperature = ScheduleFor(kind) ? ScheduleFor(kind)->temperature : 90.0;

        bool replaced = false;
        for (size_t s = 0; s < _schedules.size(); s++) {
            if (_schedules[s].kind == kind) {
                _schedules[s] = schedule;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            _schedules.push_back(schedule);

        if (kind > DRINK_NONE && kind < NUM_DRINKS)
            found[kind] = true;

        logging::msg(wxString::Format(L"tea.conf: schedule for kind %d read with %d steeps", kind,
                                       schedule.count));
    }

    // A tea added after this file was written is simply not in it. The built-in schedule covers it, so
    // nothing is broken and brewing it works: the log above simply does not mention it. What was broken
    // is the file the user is told to edit, which goes on not naming a tea that is in use, so an edit to
    // it would look like it should apply and would not. The missing lines are added in the same wording
    // the shipped copy uses, once, and only for teas that are actually missing.
    wxString missing;
    for (size_t i = 0; i < sizeof(kDefaultSchedules) / sizeof(kDefaultSchedules[0]); i++) {
        EDrinkKind kind = kDefaultSchedules[i].kind;
        if (kind > DRINK_NONE && kind < NUM_DRINKS && found[kind])
            continue;

        const TeaSchedule *inUse = ScheduleFor(kind);
        if (!inUse)
            continue;

        wxString line = ScheduleLine(*inUse);
        if (!line.IsEmpty())
            missing += line + wxT("\n");
    }

    if (!missing.IsEmpty() && AppendToConfigFile(path, missing))
        logging::msg("tea.conf: added the teas this version introduced");
}

void TeaTimer::SaveDefaultConfig(const wxString &path) const {
    // It lands beside settings.xml, whose directory exists by the time anything can be brewing, but
    // creating it keeps this usable on its own.
    wxFileName fileName(path);
    if (!wxFileName::DirExists(fileName.GetPath()))
        wxFileName::Mkdir(fileName.GetPath(), 0700, wxPATH_MKDIR_FULL);

    // Deliberately ASCII only. Reading is UTF-8, so Russian names typed into this file work, but a file
    // the app writes itself must not depend on the machine's code page: wx 3.1.3 has no WriteAll with a
    // conversion, and the plain Write converts through the locale, which on a non-UTF-8 system would
    // mangle anything above ASCII. Nothing here needs a non-ASCII character.
    wxString text;
    text += wxT("# Steeping schedules, one tea per line:\n");
    text += wxT("#   name, temperature in C, then the seconds for each steep\n");
    text += wxT("# Names are accepted in English or Russian, case does not matter.\n");
    text += wxT("# Water has no schedule: it is a drink, not something you steep.\n");
    text += wxT("# Change a line and restart EyeLeo. Anything unreadable falls back to the values here.\n");

    // The same line builder the append path uses, so a tea written out here and a tea appended later
    // come out spelled and formatted identically.
    for (size_t i = 0; i < _schedules.size(); i++) {
        wxString line = ScheduleLine(_schedules[i]);
        if (!line.IsEmpty())
            text += line + wxT("\n");
    }

    wxFFile out(path, "wb");
    if (!out.IsOpened() || !out.Write(text))
        logging::msg("tea.conf could not be written; the built-in schedules stay in use");
}

const TeaSchedule *TeaTimer::ScheduleFor(EDrinkKind kind) const {
    for (size_t i = 0; i < _schedules.size(); i++)
        if (_schedules[i].kind == kind)
            return &_schedules[i];
    return 0;
}

bool TeaTimer::Start(EDrinkKind kind) {
    const TeaSchedule *schedule = ScheduleFor(kind);
    if (!schedule || schedule->count <= 0)
        return false;

    _schedule = schedule;
    _kind = kind;
    _steep = 0;
    _msLeft = (long)schedule->seconds[0] * 1000;
    _brewing = true;
    _awaiting = false;
    _finished = false;
    return true;
}

bool TeaTimer::Advance(long elapsedMs) {
    if (!_brewing || _finished)
        return false;

    // Waiting for a pour is dead time: the countdown is already spent, so nothing moves.
    if (_awaiting) {
        _msLeft = 0;
        return false;
    }

    _msLeft -= elapsedMs;
    if (_msLeft > 0)
        return false;

    _msLeft = 0;
    if (_steep + 1 >= _schedule->count) {
        // The last steep ends the session on its own. There is no final "pour" to press, and that is
        // exactly what makes the "ready" notice appear.
        _finished = true;
        _awaiting = false;
    } else {
        _awaiting = true;
    }
    return true;
}

bool TeaTimer::Pour() {
    // Refused while a steep is still running, and that is what _awaiting false means: Advance sets it
    // once the countdown is spent and there is a next steep to start. The guard used to read the other
    // way round, which refused exactly the state Pour exists to serve, so the menu item was enabled,
    // clicked, and did nothing.
    if (!_brewing || _finished || !_awaiting)
        return false;
    if (_steep + 1 >= _schedule->count)
        return false;

    _steep++;
    _msLeft = (long)_schedule->seconds[_steep] * 1000;
    _awaiting = false;
    return true;
}

int TeaTimer::NextPourSeconds() const {
    // The time left in the steep that is running, which is the same as the time until the next pour.
    // The menu item is enabled only once the steep is over, so this counts down while it is greyed out
    // and the user can watch it approach.
    if (!_brewing || _finished || !_schedule || _awaiting)
        return 0;
    return (int)((_msLeft - 1) / 1000 + 1);
}

int TeaTimer::UpcomingSteepSeconds() const {
    if (!_brewing || _finished || !_awaiting || !_schedule)
        return 0;
    if (_steep + 1 >= _schedule->count)
        return 0;
    return _schedule->seconds[_steep + 1];
}

void TeaTimer::Stop() {
    _schedule = 0;
    _kind = DRINK_NONE;
    _steep = 0;
    _msLeft = 0;
    _brewing = false;
    _awaiting = false;
    _finished = false;
}
