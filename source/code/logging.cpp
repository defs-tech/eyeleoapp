#include "logging.h"

#include <wx/utils.h>
#include <stdarg.h>
#include <string.h>
#include <wx/filefn.h>
#ifdef WIN32
#include "shlobj.h"
#include <direct.h>
#include <shellapi.h>
#include <stdio.h>
#include <windows.h>
#endif

static FILE *logFile;

wxString GetSavePath() {
    static bool isInited = false;
    static wxString savePath;

#ifdef WIN32
    if (!isInited) {
        wchar_t appDataPath[MAX_PATH];

        // Getting a special path
        // CSIDL_COMMON_APPDATA -> 'C:\Documents and Settings\All Users\Application Data\'
        // CSIDL_APPDATA -> 'C:\Documents and Settings\username\Application Data\'
        // CSIDL_COMMON_DOCUMENTS -> 'C:\Documents and Settings\All Users\Documents\'
        if (SHGetSpecialFolderPathW(NULL, appDataPath, CSIDL_APPDATA, TRUE) == FALSE) {
            assert(!"SHGetSpecialFolderPath failed");
            return wxString(L"");
        }

        savePath.assign(appDataPath);
        savePath += L"\\EyeLeo\\";
        _wmkdir(savePath.c_str());

        isInited = true;
    }
#endif

    return savePath;
}

namespace {

// Whether anything is written at all. The same switch the macOS build uses, so the same habit works on
// both: set EYELEO_DEBUG=1 before launching and the log is written, leave it alone and nothing is.
//
// Off by default rather than on, because the log sits in the user's profile for the whole life of the
// install and is read by nobody unless something has gone wrong. The cost of that choice is that a
// problem nobody thought to trace leaves no trace, which is why it has to be written down in the README
// rather than left as a thing this file knows.
//
// Read once and cached: the environment cannot change under a running process, and this is reached from
// every state transition.
bool TraceOn() {
    static int cached = -1;
    if (cached < 0) {
        // wxGetEnv reports whether the variable exists and leaves the value alone when it does not, so
        // the "not set" case needs no handling of its own: an empty value is not "1" either way.
        wxString value;
        cached = wxGetEnv(L"EYELEO_DEBUG", &value) && value == L"1" ? 1 : 0;
    }
    return cached == 1;
}

} // namespace

namespace logging {
void Init() {
    // Nothing to do when the log is off, and in particular no file is created and no old one is deleted:
    // a run that is not being traced has no business touching the file it is not going to write.
    if (!TraceOn())
        return;

#ifdef WIN32
    _wfopen_s(&logFile, GetSavePath() + L"log.txt", L"r");
#else
    logFile = fopen("log.txt", "w");
#endif
    if (logFile) {
        fseek(logFile, 0, SEEK_END);
        long size = ftell(logFile);
        fseek(logFile, 0, SEEK_SET);

        fclose(logFile);
        logFile = 0;

        if (size > 1024 * 1024 * 3) // remove the log, if's larger than 3 megs
        {
            wxRemoveFile(GetSavePath() + L"log.txt");

            // Said out loud, because a log that suddenly turns up nearly empty looks exactly like logging
            // having been turned down or broken, and that is a bad afternoon to spend. The file really did
            // grow past three megabytes and really was deleted on purpose.
            msg(wxString::Format(L"logging: log.txt was %ld bytes, over the 3 MB limit, and has been removed", size));
        }
    }
}

void msg(wxString const &msg) {
    // Checked before the file is opened rather than after it is written, so a traced-off run costs one
    // comparison instead of an open, a write and a close on every single state transition.
    if (!TraceOn())
        return;

#ifdef WIN32
    _wfopen_s(&logFile, (GetSavePath() + L"log.txt").c_str(), L"a");
#else
    logFile = fopen("log.txt", "a");
#endif

    if (logFile) {
        // The count passed to fwrite has to be a count of bytes, and wxString::size() is a count of
        // characters. Every Cyrillic character is two bytes in UTF-8, so using it here cut the tail off
        // every line that contained any, which is most of them in a Russian log: "по 10, 15, 20, 25, 30"
        // came out as "по 10, 15, 20". It did not just look wrong, it made the log useless for working out
        // what the program had actually done. The length is taken from the converted string instead.
        wxCharBuffer utf8 = msg.mb_str(wxConvUTF8);
        // Checked as a pointer rather than as the buffer, because wxCharBuffer has no operator bool; it
        // only converts implicitly to const char *, which would make if(utf8) work without saying so.
        if (utf8.data())
            fwrite(utf8.data(), 1, strlen(utf8.data()), logFile);
        fwrite("\n", 1, 1, logFile);

        fclose(logFile);
    }
    logFile = 0;
}
} // namespace logging