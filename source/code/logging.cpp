#include "logging.h"
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

namespace logging {
void Init() {
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
        }
    }
}

void msg(wxString const &msg) {
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