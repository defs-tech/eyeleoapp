#include "tea_timer.h"

#include "logging.h"

#include <wx/arrstr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/string.h>

// The shipped schedules, used when tea.conf names no usable tea. Green and white are short and stepped,
// oolong and black longer, pu-erh longest of all, and herbal is not tea at all but a dried blend that
// wants minutes rather than seconds. These are also what SaveDefaultConfig writes out on a first run.
static const TeaSchedule kDefaultSchedules[] = {
    {wxT("green"), wxT("Green tea"), wxT("Зелёный чай"), 80.0, {10, 15, 20, 25, 30, 35, 0, 0}, 6},
    {wxT("white"), wxT("White tea"), wxT("Белый чай"), 85.0, {10, 15, 20, 25, 30, 0, 0, 0}, 5},
    {wxT("oolong"), wxT("Oolong"), wxT("Улун"), 85.0, {15, 25, 35, 45, 60, 75, 0, 0}, 6},
    {wxT("black"), wxT("Black tea"), wxT("Чёрный чай"), 90.0, {20, 30, 40, 50, 60, 70, 0, 0}, 6},
    {wxT("red"), wxT("Red tea"), wxT("Красный чай"), 95.0, {7, 10, 15, 20, 25, 30, 0, 0}, 6},
    {wxT("puer"), wxT("Pu-erh"), wxT("Пуэр"), 99.0, {20, 30, 40, 50, 60, 70, 80, 90}, 8},
    {wxT("herbal"), wxT("Herbal tea"), wxT("Травяной чай"), 100.0, {300, 360, 420, 480, 0, 0, 0, 0}, 4},
};

static const int kNumDefaultSchedules =
    sizeof(kDefaultSchedules) / sizeof(kDefaultSchedules[0]);

static const TeaSchedule *BuiltInSchedule(int i) { return &kDefaultSchedules[i]; }

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

// One schedule as a config line, or empty if it cannot be written as one.
static wxString ScheduleLine(const TeaSchedule &schedule) {
    if (schedule.name.IsEmpty() || schedule.count <= 0)
        return wxEmptyString;

    wxString line = wxString::Format(wxT("%s, %.0f"), schedule.name, schedule.temperature);
    for (int k = 0; k < schedule.count; k++)
        line += wxString::Format(wxT(", %d"), schedule.seconds[k]);
    // The names go out with the schedule, so the template the installer ships is the same file format the
    // parser reads rather than a subset of it.
    if (!schedule.nameEn.IsEmpty())
        line += wxString::Format(wxT(", en=\"%s\""), schedule.nameEn);
    if (!schedule.nameRu.IsEmpty())
        line += wxString::Format(wxT(", ru=\"%s\""), schedule.nameRu);
    return line;
}

// The name is the whole identity of a tea: it keys its picture file and its menu entry, so it is held
// to a shape that cannot go wrong on either. Lowercase Latin, one word, no spaces, starting with a
// letter.
//
// Uppercase is refused rather than folded. A Windows volume is case-insensitive by default, so Tea.png
// and tea.png would be one file, and a name that differs only in case would quietly pick up another's
// picture. The same reason caps the length: the name is a filename, and a filename that can run to the
// end of the path is not one anybody wants to be handed by a text file.
static bool NameIsValid(const wxString &name) {
    if (name.IsEmpty() || name.length() > 24)
        return false;
    if (name[0] < wxT('a') || name[0] > wxT('z'))
        return false;
    for (int i = 1; i < (int)name.length(); i++) {
        wchar_t c = name[i];
        bool letter = (c >= wxT('a') && c <= wxT('z'));
        bool digit = (c >= wxT('0') && c <= wxT('9'));
        if (!letter && !digit && c != wxT('_'))
            return false;
    }
    return true;
}

// Splits a line into fields on commas, except for commas inside double quotes. The display names are
// written as en="..." and a name is allowed to contain a comma, as in en="Milk, with a dash". A single
// toggle over the quotes is enough: the values are user text, they do not nest, and an unterminated quote
// leaves the rest of the line as one field, which then fails to parse and is reported like any other
// mistake.
static void SplitFields(const wxString &line, wxArrayString *out) {
    out->Clear();
    wxString field;
    bool quoted = false;
    for (size_t i = 0; i < line.length(); i++) {
        wchar_t c = line[i];
        if (c == L'"') {
            quoted = !quoted;
            field += c;
        } else if (c == L',' && !quoted) {
            out->Add(field.Strip(wxString::both));
            field.Clear();
        } else {
            field += c;
        }
    }
    out->Add(field.Strip(wxString::both));
}

// Reads one en="..." or ru="..." field.
//
// Three answers, not two. A field that is not a name at all is left for the caller to try as a steep
// time; a name for a language this build has goes in its slot; a name for a language it does not have is
// recognised and dropped, because a tea written for another language is still a usable tea and refusing
// the whole line over it would throw away a working schedule.
enum ENameField { NAME_FIELD_NOT_A_NAME = 0, NAME_FIELD_TAKEN, NAME_FIELD_OTHER_LANGUAGE };

static ENameField ParseNameField(const wxString &text, wxString *en, wxString *ru) {
    size_t open = text.Find(wxT("=\""));
    if (open == wxNOT_FOUND || open < 2 || !text.EndsWith(wxT("\"")))
        return NAME_FIELD_NOT_A_NAME;

    wxString tag = text.Left(open).Lower();
    if (tag != wxT("en") && tag != wxT("ru"))
        return NAME_FIELD_OTHER_LANGUAGE;

    // Between the opening and the closing quote: skip the two characters of =", and the quote at the end.
    wxString value = text.Mid(open + 2, text.length() - open - 3);
    (tag == wxT("en") ? *en : *ru) = value;
    return NAME_FIELD_TAKEN;
}

// A refusal is only visible to whoever turned logging on, which is the point: a rejected line is a
// mistake in a file nobody is supposed to be editing by hand, and the day it matters is the day somebody
// is. It is not a message box over the top of an app that is otherwise working.
static void Reject(const wxString &reason) {
    logging::msg(wxString::Format(L"tea.conf: %s", reason));
}

void TeaTimer::ResetToDefaults() {
    _schedules.clear();
    for (int i = 0; i < kNumDefaultSchedules; i++)
        _schedules.push_back(*BuiltInSchedule(i));

    _schedule = 0;
    _name.Clear();
    _steep = 0;
    _msLeft = 0;
    _brewing = false;
    _awaiting = false;
    _finished = false;
}

TeaTimer::TeaTimer() {
    ResetToDefaults();
}

void TeaTimer::Parse(const wxString &text) {
    std::vector<TeaSchedule> parsed;
    wxArrayString seen;

    // One tea per line: name, temperature, then the steeps in seconds.
    //   puer, 99, 20, 30, 40, 50, 60, 70, 80, 90
    // A line is taken whole or refused whole. There is no half-read schedule, and a rejected line never
    // leaves a stray steep behind.
    wxString rest = text;
    int lineNo = 0;
    while (!rest.IsEmpty()) {
        wxString line = rest.BeforeFirst('\n');
        // AfterFirst hands back the whole string when the delimiter is not there any more, which would
        // leave rest unchanged and spin here for ever. That is not an odd file: it is a one-line tea.conf
        // without a trailing newline, which is exactly what a file written by hand usually is.
        wxString next = rest.AfterFirst('\n');
        rest = (next == rest) ? wxString() : next;
        lineNo++;

        line = line.Strip(wxString::both);
        if (line.IsEmpty() || line[0] == '#')
            continue;

        // Split on commas without dropping empty fields: an empty one is a mistake in the line, and
        // quietly stepping over it would shift every field after it. Quoted commas do not split.
        wxArrayString parts;
        SplitFields(line, &parts);

        const wxString where = wxString::Format(L"line %d", lineNo);
        const wxString name = parts[0];

        if (!NameIsValid(name)) {
            Reject(wxString::Format(L"%s: \"%s\" is not a usable name: one lowercase Latin word, letters, "
                                   L"then digits or underscores",
                                   where, name));
            continue;
        }
        if (seen.Index(name) != wxNOT_FOUND) {
            Reject(wxString::Format(L"%s: %s is already defined above; the first one stands", where, name));
            continue;
        }
        if ((int)parts.GetCount() < 3) {
            Reject(wxString::Format(L"%s: %s has too few fields, expected name, temperature, then one time "
                                   L"per steep",
                                   where, name));
            continue;
        }

        double temperature = 0.0;
        if (!parts[1].ToDouble(&temperature) || temperature <= 0.0 || temperature > 100.0) {
            Reject(wxString::Format(L"%s: %s temperature must be a number over 0 and up to 100", where,
                                   name));
            continue;
        }

        TeaSchedule schedule;
        schedule.name = name;
        schedule.temperature = temperature;
        schedule.count = 0;
        for (int s = 0; s < kMaxSteepsPerTea; s++)
            schedule.seconds[s] = 0;

        // Anything after the temperature is either a steep time in seconds or a display name written as
        // en="..." or ru="...". The two are told apart by shape, which is why the name is quoted: a bare
        // word could only be a mistyped time, and refusing it is more useful than guessing.
        bool malformed = false;
        for (size_t p = 2; p < parts.GetCount() && !malformed; p++) {
            ENameField kind = ParseNameField(parts[p], &schedule.nameEn, &schedule.nameRu);
            if (kind == NAME_FIELD_TAKEN)
                continue;
            if (kind == NAME_FIELD_OTHER_LANGUAGE) {
                // A tea written for a language this build does not carry. Not a reason to throw away the
                // schedule, and the reason it went unnoticed is said out loud under EYELEO_DEBUG.
                logging::msg(wxString::Format(L"%s: %s: \"%s\" names a language this build does not have, "
                                              L"the tea is shown under its name",
                                              where, name, parts[p]));
                continue;
            }

            long seconds = 0;
            if (parts[p].IsEmpty() || !parts[p].ToLong(&seconds) || seconds <= 0 ||
                seconds > kMaxSteepSeconds) {
                Reject(wxString::Format(L"%s: %s: \"%s\" is neither a steep time in whole seconds nor a "
                                       L"name written as en=\"...\" or ru=\"...\"",
                                       where, name, parts[p]));
                malformed = true;
            } else if (schedule.count >= kMaxSteepsPerTea) {
                Reject(wxString::Format(L"%s: %s: more than %d steeps", where, name, kMaxSteepsPerTea));
                malformed = true;
            } else {
                schedule.seconds[schedule.count++] = (int)seconds;
            }
        }
        if (malformed || schedule.count == 0)
            continue;

        seen.Add(name);
        parsed.push_back(schedule);
    }

    // A file that lists no teas is read as no file at all. Otherwise a single bad line would leave the
    // menu bare, which reads as breakage rather than as what it is: a file with a mistake in it, reported
    // in the log and left alone.
    if (parsed.empty()) {
        Reject(wxString::Format(L"no usable teas in the file, using the %d built-in schedules",
                                kNumDefaultSchedules));
        return;
    }

    // What is listed is what there is. Nothing is merged with the built-ins and nothing is added back
    // for the teas the file left out: the person editing it has said which teas they want.
    _schedules = parsed;
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
            logging::msg(L"tea.conf seeded from the copy shipped with the installer");
        } else {
            logging::msg(L"tea.conf not present, writing out the built-in schedules");
            SaveDefaultConfig(path);
            return;
        }
    }

    Parse(text);
}

void TeaTimer::SaveDefaultConfig(const wxString &path) const {
    // It lands beside settings.xml, whose directory exists by the time anything can be brewing, but
    // creating it keeps this usable on its own.
    wxFileName fileName(path);
    if (!wxFileName::DirExists(fileName.GetPath()))
        wxFileName::Mkdir(fileName.GetPath(), 0700, wxPATH_MKDIR_FULL);

    // Deliberately ASCII only. Reading is UTF-8, so a comment in Russian typed into this file works, but
    // a file the app writes itself must not depend on the machine's code page: wx 3.1.3 has no WriteAll
    // with a conversion, and the plain Write converts through the locale, which on a non-UTF-8 system
    // would mangle anything above ASCII. Nothing here needs a non-ASCII character.
    wxString text;
    text += wxT("# Steeping schedules, one tea per line:\n");
    text += wxT("#   name, temperature in C, then the seconds for each steep\n");
    text += wxT("#\n");
    text += wxT("# The name is the tea's whole identity. It is also the name of its picture file,\n");
    text += wxT("# Resources/tea_<name>.png, so a new tea needs a new picture to look right.\n");
    text += wxT("# Names are one lowercase Latin word: letters, then digits or underscores, up to 24.\n");
    text += wxT("# Between 1 and 8 steeps per tea, each a whole number of seconds from 1 to 3600.\n");
    text += wxT("# The menu follows this file in this order. Delete a line and that tea is gone.\n");
    text += wxT("# Change a line and restart EyeLeo. Anything unreadable falls back to the values here.\n");

    for (size_t i = 0; i < _schedules.size(); i++) {
        wxString line = ScheduleLine(_schedules[i]);
        if (!line.IsEmpty())
            text += line + wxT("\n");
    }

    wxFFile out(path, "wb");
    if (!out.IsOpened() || !out.Write(text))
        logging::msg(L"tea.conf could not be written; the built-in schedules stay in use");
}

int TeaTimer::IndexOfName(const wxString &name) const {
    for (size_t i = 0; i < _schedules.size(); i++)
        if (_schedules[i].name == name)
            return (int)i;
    return -1;
}

const TeaSchedule *TeaTimer::ScheduleFor(const wxString &name) const {
    int index = IndexOfName(name);
    return index < 0 ? 0 : &_schedules[index];
}

bool TeaTimer::Start(const wxString &name) {
    int index = IndexOfName(name);
    if (index < 0 || _schedules[index].count <= 0)
        return false;

    _schedule = &_schedules[index];
    _name = name;
    _steep = 0;
    _msLeft = (long)_schedule->seconds[0] * 1000;
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
    _name.Clear();
    _steep = 0;
    _msLeft = 0;
    _brewing = false;
    _awaiting = false;
    _finished = false;
}