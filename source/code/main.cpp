#include "main.h"
#include "activity_monitor.h"
#include "beforepause_wnd.h"
#include "bigpause_wnd.h"
#include "debug_wnd.h"
#include "drink_wnd.h"
#include "excercises.h"
#include "image_resources.h"
#include "language_set.h"
#include "logging.h"
#include "menu_tip.h"
#include "minipause_wnd.h"
#include "notification_wnd.h"
#include "oscapabilities.h"
#include "pugixml.hpp"
#include "settings.h"
#include "settings_wnd.h"
#include "timeloc.h"
#include "waiting_wnd.h"
#include <algorithm>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/taskbar.h>
#include <wx/tipwin.h>

#ifdef WIN32
#include "shlobj.h"
#include <VersionHelpers.h>
#include <Wtsapi32.h>
#include <direct.h>
#include <shellapi.h>
#include <winver.h>
#endif

static EyeApp *g_eyeApp = nullptr;

IMPLEMENT_APP(EyeApp);

///////////////////////////////////////////////////////////////////////////////////////
EyeApp *getApp() {
    return g_eyeApp;
}

///////////////////////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(EyeApp, wxApp)
EVT_QUERY_END_SESSION(EyeApp::OnQueryEndSession)
EVT_END_SESSION(EyeApp::OnEndSession)
EVT_COMMAND(wxID_ANY, EXECUTE_TASK_EVENT, EyeApp::OnTaskEvent)
END_EVENT_TABLE()

EyeApp::EyeApp()
    : _settingsWnd(nullptr)
    , _inactivityTime(0)
    , _timeLeftToBigPause(0)
    , _timeLeftToMiniPause(0)
    , _timeToWaterReminder(0)
    , _bigPauseInterval(0)
    , _relaxingTimeLeft(0)
    , _fullscreenBlockDuration(0)
    , _timeUntilWaitingWnd(0)
    , _postponeCount(0)
    , _warningInterval(0)
    , _debugWindow(nullptr)
    , _firstLaunch(true)
    , _seenSettingsWindow(false)
    , _fastMode(false)
    , _userLongBreakCount(0)
    , _userEarlySkipCount(0)
    , _userLateSkipCount(0)
    , _userRefuseCount(0)
    , _userPostponeCount(0)
    , _userAutoBreakCount(0)
    , _userShortBreakCount(0)
    , _lastBigPauseTimeLeft(0)
    , _lastMiniPauseTimeLeft(0)
    , _lastDuration(0)
    , _currentState(0)
    , _nextState(0)
    , _finished(false)
    , _lastShutdown()
    , _notificationWnd(nullptr)
    , _waterReminderWnd(nullptr)
    , _teaReminderWnd(nullptr)
    , _showedLongBreakCountdown(false) {
    g_eyeApp = this;
}

void EyeApp::ReadConfig() {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file("config.xml");

    if (result.status != pugi::status_ok) {
        wxFileName execPath = wxStandardPaths::Get().GetExecutablePath();
        execPath.SetFullName("config.xml");

        logging::msg("Failed to load config.xml from cwd, trying executable path: " + execPath.GetFullPath());

        result = doc.load_file((const wchar_t *)execPath.GetFullPath().c_str());
    }

    if (result.status == pugi::status_ok) {
        pugi::xml_node node = doc.child(L"config");
        if (!node.empty()) {
            _lang.assign(node.attribute(L"language").value());
            _version.assign(node.attribute(L"version").value());
            _website.assign(node.attribute(L"website").value());
        }
        logging::msg(wxString::Format("Config read, lang=%s, version=%s, website=%s", _lang, _version, _website));
    } else {
        logging::msg("Failed to read config.xml");
        wxMessageBox(_("Could't load config.xml file."), _("EyeLeo"));
    }
}

bool EyeApp::OnInit() {
    if (!IsOnlyInstance())
        return false;

    logging::Init();

    srand((unsigned)time(0));

    _lang = L"en";
    _version = L"(?)";
    _website = L"eyeleo.com";
    ReadConfig();

    if (!LoadLanguagePack(_lang)) {
        if (!LoadLanguagePack(L"en")) {
            wxMessageBox(_("Could't load nor '") + _lang + _("', nor 'en' language pack."), _("EyeLeo"));
            return false;
        } else {
            wxMessageBox(_("Could't load '") + _lang + _("' language pack. Fell back to 'en' pack."), _("EyeLeo"));
        }
    }

    wxInitAllImageHandlers();

    LoadEyeLeoResources();

    _taskBarIcon = new EyeTaskBarIcon();

    g_TaskMgr = new TaskManager();

    wxThreadError err = g_TaskMgr->Run();
    if (err != wxTHREAD_NO_ERROR) {
        wxMessageBox(_("Can't start timer thread!"));
        return false;
    }

    //_fastMode = true;

    ResetSettings();

    if (!LoadSettings())
        ResetSettings();
    else
        CheckSettings();

    if (_firstLaunch) {
        ChangeState(STATE_FIRST_LAUNCH, 1000);
    } else {
        if (_lastShutdown.IsValid()) {
            wxDateTime now = wxDateTime::Now();

            wxTimeSpan lastShutdownP = now.Subtract(_lastShutdown);
            long secondsWent = lastShutdownP.GetSeconds().ToLong();

            if (secondsWent > 0 && secondsWent <= 30 * 60 /*<= 30 mins */) {
                wxLongLong_t p = lastShutdownP.GetMilliseconds().GetValue();
                long lastBigPauseTimeLeft = _lastBigPauseTimeLeft - p;
                long lastMiniPauseTimeLeft = _lastMiniPauseTimeLeft - p;

                if (lastBigPauseTimeLeft < -1000 * 60 * 3) {
                    RestartBigPauseInterval();
                    RestartMiniPauseInterval();
                } else {
                    if (lastBigPauseTimeLeft < 1000 * 60) // not less than a minute
                        lastBigPauseTimeLeft = 1000 * 60;

                    SetBigPauseTime(lastBigPauseTimeLeft);

                    if (lastMiniPauseTimeLeft < 1000 * 60)
                        lastMiniPauseTimeLeft = 1000 * 60;

                    SetMiniPauseTime(lastMiniPauseTimeLeft);
                }

                UpdateTaskbarText();
            } else {
                // normal case, restart everything
                ApplySettings();
            }
        } else {
            ApplySettings();
        }

        // Water is not resumed from the previous session, by design, so it always starts a full
        // interval in. Done here rather than in ApplySettings() because the branch above sets the two
        // break timers by hand and never calls that.
        RestartWaterInterval();
    }

    PrepareActivityMonitor();
    InstallActivityMonitor();

    // The config the user edits lives in their profile, beside settings.xml. The copy the installer put
    // next to the executable is only a template: on the first run it is copied over, so a fresh install
    // runs exactly the values that shipped, and the user never has to guess where the file went.
    // Read from the executable rather than the working directory, unlike the assets, because this one
    // has to be found even if the app was started by a shortcut with an empty working directory.
    wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
    _tea.LoadConfig(GetSavePath() + L"tea.conf", exeDir.GetPath() + wxT("\\") + wxT("tea.conf"));

    g_Personage = new PersonageData(L"leopard");

#ifndef RELEASE
    //_debugWindow = new DebugWindow();
    //_debugWindow->Show(true);
#endif

    int big_pause_seconds = _timeLeftToBigPause / 1000;
    wxString text = wxString::Format(langPack->Get("tb_notification_first_launch"), getTimeStr(big_pause_seconds, SECONDS, _lang));
    _taskBarIcon->ShowBalloonToolip(text);

    return true;
}

bool EyeApp::IsOnlyInstance() const {
#ifdef NDEBUG
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "EyeLeo_mutex");
#else
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "EyeLeo_mutex_d");
#endif
    if (GetLastError()) {
        if (hMutex)
            ReleaseMutex(hMutex);
        return false;
    }
    return true;
}

wxString EyeApp::GetSavePath() const {
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

void EyeApp::RestartBigPauseInterval() {
    logging::msg("RestartBigPauseInterval");

    _postponeCount = 0;
    _inactivityTime = 0;
    _showedLongBreakCountdown = false;

    if (_enableBigPause)
        _timeLeftToBigPause = _bigPauseInterval * 1000 * 60;
    else
        _timeLeftToBigPause = 0;

    ChangeState(STATE_IDLE, 1000);
}

void EyeApp::SetBigPauseTime(long ms) {
    logging::msg(wxString::Format(L"SetBigPauseTime to %d", ms));

    _postponeCount = 0;
    _inactivityTime = 0;

    if (ms < 1000 * 91) // not less than 1.5mins
    {
        ms = 1000 * 91;
    } else if (ms > 1000 * 60 * _bigPauseInterval) // not more current duration setting (bug check)
    {
        logging::msg(wxString::Format(L"Error! SetBigPauseTime"));
        ms = 1000 * 60 * _bigPauseInterval;
    }

    if (_enableBigPause)
        _timeLeftToBigPause = ms;
    else
        _timeLeftToBigPause = 0;

    ChangeState(STATE_IDLE, 1000);
}

void EyeApp::RestartMiniPauseInterval() {
    logging::msg("RestartMiniPauseInterval");

    if (_enableMiniPause)
        _timeLeftToMiniPause = _miniPauseInterval * 1000 * 60;
    else
        _timeLeftToMiniPause = 0;
}

void EyeApp::SetMiniPauseTime(long ms) {
    logging::msg(wxString::Format(L"SetMiniPauseTime to %d", ms));

    if (ms < 1000 * 91)
        ms = 1000 * 91;

    if (_enableMiniPause)
        _timeLeftToMiniPause = ms;
    else
        _timeLeftToMiniPause = 0;

    UpdateDebugWindow();
}

void EyeApp::OnUserActivity() {
    if (GetNextState() == STATE_SUSPENDED)
        return;

    _inactivityTime = 0;

    if (GetNextState() == STATE_AUTO_RELAX) {
        logging::msg("OnUserActivity ended auto-relax");
        ApplySettings();

        int big_pause_seconds = _timeLeftToBigPause / 1000;
        wxString text = wxString::Format(langPack->Get("tb_notification_auto_relax_ended"), getTimeStr(big_pause_seconds, SECONDS, _lang));
        _taskBarIcon->ShowBalloonToolip(text, 1000 * 8);
    }
}

// Check if full screen app is running, returns display number or -1 as 'display'
bool EyeApp::IsFullscreenAppRunning(int *display, HWND *fullscreenWndHandle) const {
    refillResolutionParams();

    HWND hWnd = GetForegroundWindow();
    if (!hWnd) {
        // logging::msg("IsFullscreenAppRunning failed 0");
        return false;
    }

    HWND hDesktop = GetDesktopWindow();
    HWND hShell = GetShellWindow();
    if (hWnd == hDesktop || hWnd == hShell) {
        logging::msg("IsFullscreenAppRunning failed 1");
        return false;
    }

    if (!IsWindowVisible(hWnd) || IsIconic(hWnd)) {
        logging::msg("IsFullscreenAppRunning failed 2");
        return false;
    }

    RECT wndArea;
    if (!GetWindowRect(hWnd, &wndArea)) {
        logging::msg("IsFullscreenAppRunning failed 3");
        return false;
    }

    // logging::msg(wxString::Format("wndArea (%d, %d), (%d, %d)", wndArea.left, wndArea.right, wndArea.top, wndArea.bottom));

    for (int d = 0; d < osCaps.numDisplays; ++d) {
        wxRect displayArea = osCaps.displays[d].geometry;

        // logging::msg(wxString::Format("%d) displayArea (%d, %d, %d, %d)", d, displayArea.x, displayArea.y, displayArea.width, displayArea.height));

        if (wndArea.left == displayArea.x && wndArea.top == displayArea.y && wndArea.right - wndArea.left == displayArea.width &&
            wndArea.bottom - wndArea.top == displayArea.height) {
            if (display)
                *display = d;
            if (fullscreenWndHandle)
                *fullscreenWndHandle = hWnd;
            // logging::msg(wxString::Format("Fullscreen app is at disp %d", d));
            return true;
        }
    }
    // logging::msg("No fullscreen app running");
    return false;
}

void EyeApp::UpdateTaskbarText() {
    if (GetNextState() == STATE_AUTO_RELAX) {
        _taskBarIcon->UpdateTooltip(L"EyeLeo error! Please contact author and tell him you saw this...");
        return;
    }

    if (GetNextState() == STATE_SUSPENDED) {
        int secs = _inactivityTime / 1000;
        wxString text = wxString::Format(langPack->Get("tb_popup_paused"), getTimeStr(secs, SECONDS, _lang));
        _taskBarIcon->UpdateTooltip(text);
        return;
    }

    if (_enableBigPause) {
        int secs = _timeLeftToBigPause / 1000;
        wxString text = wxString::Format(langPack->Get("tb_popup_active_1"), getTimeStr(secs, SECONDS, _lang));
        _taskBarIcon->UpdateTooltip(text);
    } else {
        _taskBarIcon->UpdateTooltip(langPack->Get("tb_popup_default"));
    }
}

void EyeApp::ChangeState(int nextState, int duration) {
    _lastDuration = duration;
    _nextState = nextState;

    g_TaskMgr->AddTask("EyeApp", duration);
}

void EyeApp::RepeatState() {
    ChangeState(_currentState, _lastDuration);
}

void EyeApp::Stop() {
    if (g_TaskMgr) {
        // g_TaskMgr->StopTasks();
        g_TaskMgr->Delete();
        g_TaskMgr = 0;
    }
}

void EyeApp::OnTaskEvent(wxCommandEvent &c) {
    TaskPayload *pl = static_cast<TaskPayload *>(c.GetClientData());
    if (!pl)
        return;

    wxMilliClock_t now = ::wxGetLocalTimeMillis();
    wxMilliClock_t time_went = now - pl->start_time;
    float f = float(time_went.ToDouble() / pl->duration.ToDouble());

    if (pl->check != 12345) {
        logging::msg(wxString::Format("Detected wrong event! pl=%x", pl));
        assert(false);
    }

    TaskPtr task = getWindow(pl->address);
    if (task)
        task->ExecuteTask(f, time_went.ToLong());

    delete pl;
}

bool EyeApp::CheckInactivity() {
    POINT p;
    BOOL res = GetCursorPos(&p);
    if (!res)
        return false;

    if (abs(_cursorPos.x - p.x) > 1 || abs(_cursorPos.y - p.y) > 1) {
        _cursorPos = p;
        return false;
    }

    return true;
}

ITask *EyeApp::getWindow(const wxString &address) {
    if (address == "EyeApp")
        return this;

    for (std::vector<BigPauseWindow *>::iterator wnd = _bigPauseWnds.begin(); wnd != _bigPauseWnds.end(); ++wnd) {
        if ((*wnd)->GetName() == address)
            return *wnd;
    }

    for (std::vector<MiniPauseWindow *>::iterator wnd = _miniPauseWnds.begin(); wnd != _miniPauseWnds.end(); ++wnd) {
        if ((*wnd)->GetName() == address)
            return *wnd;
    }

    for (std::vector<WaitingFullscreenWindow *>::iterator wnd = _waitWnds.begin(); wnd != _waitWnds.end(); ++wnd) {
        if ((*wnd)->GetName() == address)
            return *wnd;
    }

    if (_notificationWnd && _notificationWnd->GetName() == address)
        return _notificationWnd;

    for (std::vector<BeforePauseWindow *>::iterator wnd = _beforePauseWnds.begin(); wnd != _beforePauseWnds.end(); ++wnd) {
        if ((*wnd)->GetName() == address)
            return *wnd;
    }

    // Every reminder has to be reachable here by name, or its task never runs. The task manager resolves
    // names through this function, and a name it cannot resolve means ExecuteTask is never called at
    // all: the window stays at zero alpha, so it never fades in, never counts down, and never closes.
    // Worse, its destructor never runs either, so the class's single-instance flag stays set and every
    // later reminder is dropped without a word. That is exactly how the hydration and tea bubbles failed
    // to appear at all.
    DrinkReminderWindow *reminders[2] = {_waterReminderWnd, _teaReminderWnd};
    for (int i = 0; i < 2; i++)
        if (reminders[i] && reminders[i]->GetName() == address)
            return reminders[i];

    return nullptr;
}

void EyeApp::StartTea(EDrinkKind kind) {
    logging::msg(wxString::Format(L"StartTea kind=%d", kind));

    // Every one of these used to return in silence, which made a refusal look exactly like a failure
    // somewhere else. The reason belongs in the log, because the symptom is identical every time.
    if (_notificationWnd || _waterReminderWnd || _teaReminderWnd) {
        logging::msg("StartTea refused: another reminder is already on screen");
        return;
    }
    if (_bigPauseWnds.size() || _miniPauseWnds.size()) {
        logging::msg("StartTea refused: a break is on screen");
        return;
    }

    if (!_tea.Start(kind)) {
        logging::msg(wxString::Format(L"StartTea refused: no schedule for kind %d", kind));
        return;
    }

    TickTea(0);
}

void EyeApp::PourNextSteep() {
    if (!_tea.Pour()) {
        // Either nothing is brewing or the current steep is still running, and those look the same from
        // the menu, which is why the item is disabled unless a steep is over.
        logging::msg("Pour refused: nothing to pour yet");
        return;
    }

    TickTea(0);
}

void EyeApp::TickTea(long elapsedMs) {
    if (!_tea.IsBrewing())
        return;

    // The session is over: the ready notice has been put up, or has come and gone, and there is nothing
    // left to count. Without this the tail of TickTea ran on every tick with a finished session, and since
    // IsAwaitingPour() is false for a finished one it asked for the last steep's caption again and again.
    // ShowTeaReminder writes that onto the bubble that is already up, so the "ready" notice was replaced
    // by the last steep within one tick and the bubble, having nothing left to dismiss it, stayed for good.
    // It held the single reminder slot, and IsTeaMenuEnabled() refuses while the slot is held, so the tea
    // part of the menu stayed blocked too.
    if (_tea.IsFinished())
        return;

    if (elapsedMs > 0 && _tea.Advance(elapsedMs)) {
        if (_tea.IsFinished()) {
            // The last steep ended on its own. The bubble showing its countdown is still up and is what
            // the notice belongs in: there is one bubble by design and a second cannot be created while
            // the first holds the slot, so the notice is written onto it rather than replacing it.
            logging::msg(L"tea: last steep done, putting up the ready notice");
            if (_teaReminderWnd) {
                _teaReminderWnd->HideCountdown();
                _teaReminderWnd->SetCaption(TeaReadyCaption());
                _teaReminderWnd->SetTimeLabel(0);
                _teaReminderWnd->SetAutoDismiss(kTeaReadyNoticeSec * 1000);
            } else {
                ShowTeaReminder(TeaReadyCaption(), 0, kTeaReadyNoticeSec * 1000);
            }
            return;
        }

        // A steep ran out. Showing zero seconds is pointless, so the bubble goes away and the pour
        // happens from the menu.
        CloseTeaReminder();
        return;
    }

    // A steep is running: keep the countdown to it on screen, in place, since this is called every tick.
    if (!_tea.IsAwaitingPour())
        ShowTeaReminder(TeaSteepCaption(), _tea.MsLeft(), 0);
}

wxString EyeApp::TeaReadyCaption() {
    // Not "the tea is ready": by the time this is reached the last steep has been poured and nothing
    // more is coming out of the leaves, so the brewing cycle is over rather than anything ready. The
    // tea's name left the text along with the claim, which is what let the notice fit in three lines,
    // one word each.
    return langPack->Get("tea_caption_ready");
}

wxString EyeApp::TeaSteepCaption() {
    // No tea name here, same as on mac: the cup in the bubble is a different shape for every tea, so
    // the name would only repeat what the picture already says. The name does appear in the "ready"
    // notice, where the string is short.
    //
    // The temperature goes on a second line rather than trailing the sentence. mac gets that for free
    // because its caption field is tall enough for two lines and the line simply wraps, but whether a
    // line wraps is a property of the font metrics of the machine it is drawn on, and a wrapped second
    // line still gets cut off at the bottom of the field. A line break in the string is certain, and the
    // wording ends up the same as what mac's wrapping produces.
    const TeaSchedule *schedule = _tea.ScheduleFor(_tea.Kind());
    double temperature = schedule ? schedule->temperature : 0.0;
    return wxString::Format(langPack->Get("tea_caption_steep"), _tea.Steep() + 1, _tea.SteepCount(), temperature);
}

wxString EyeApp::TeaName(EDrinkKind kind) {
    switch (kind) {
    case DRINK_GREEN:
        return langPack->Get(L"tea_name_green");
    case DRINK_WHITE:
        return langPack->Get(L"tea_name_white");
    case DRINK_OOLONG:
        return langPack->Get(L"tea_name_oolong");
    case DRINK_BLACK:
        return langPack->Get(L"tea_name_black");
    case DRINK_RED:
        return langPack->Get(L"tea_name_red");
    case DRINK_PUER:
        return langPack->Get(L"tea_name_puer");
    case DRINK_HERBAL:
        return langPack->Get(L"tea_name_herbal");
    case DRINK_WATER:
        return langPack->Get(L"tea_name_water");
    default:
        return langPack->Get(L"tea_name_generic");
    }
}

void EyeApp::ShowTeaReminder(const wxString &caption, long msLeft, long autoDismissMs) {
    if (_teaReminderWnd) {
        // Update in place. Recreating the window on every steep would flash, and while the old one is
        // still fading out it still owns the singleton, so the new one could not even be created.
        _teaReminderWnd->SetCaption(caption);
        _teaReminderWnd->SetTimeLabel(msLeft);
        if (autoDismissMs > 0)
            _teaReminderWnd->SetAutoDismiss(autoDismissMs);
        return;
    }

    // Something else already owns the single reminder slot, most likely the hydration bubble. There is
    // one bubble by design, so refusing is right, but it must not be silent: a tea that silently does
    // nothing is indistinguishable from a broken one.
    if (DrinkReminderWindow::HasInstance()) {
        logging::msg("tea reminder not shown: another reminder already holds the slot");
        return;
    }

    DrinkReminderWindow *wnd = new DrinkReminderWindow(_tea.Kind(), caption, 0);
    if (!wnd->Init(0)) {
        delete wnd;
        return;
    }
    _teaReminderWnd = wnd;
    _teaReminderWnd->SetTimeLabel(msLeft);
    if (autoDismissMs > 0)
        _teaReminderWnd->SetAutoDismiss(autoDismissMs);
}

void EyeApp::CloseTeaReminder() {
    // The pointer has to stay alive while the window fades out: the fade is driven by ExecuteTask, which
    // resolves the window by name through getWindow, and that lookup goes through this very pointer.
    // Clearing it here would strand the window on screen forever. OnDrinkReminderWindowClosed drops it
    // from the destructor, once the window is really gone.
    if (_teaReminderWnd)
        _teaReminderWnd->Hide();
}

bool EyeApp::IsTeaMenuEnabled() const {
    // Nothing may be started while a break overlay owns the screen: the reminder would land underneath
    // it and nothing would be left of it once the break ended.
    if (_notificationWnd || _waterReminderWnd || _teaReminderWnd)
        return false;
    if (_bigPauseWnds.size() || _miniPauseWnds.size())
        return false;
    return true;
}

void EyeApp::ExecuteTask(float, long time_went) {
    // Ahead of everything else, so a steep keeps running through a break.
    TickTea(time_went);
    _currentState = _nextState;
    _nextState = 0;

    if (_settingInactivityTracking && _currentState != STATE_SUSPENDED && _currentState != STATE_AUTO_RELAX) {
        if (CheckInactivity()) {
            _inactivityTime += time_went * (_fastMode ? 1 : 1);
        } else {
            _inactivityTime = 0;
        }
    }

    switch (_currentState) {
    case STATE_SUSPENDED: {
        RepeatState();

        // для этого состояния исп-тся _inactivityTime с обратным отсчетом (хотя в обычном режиме он накапливается)
        int multiplier = _fastMode ? 2 : 1;
        _inactivityTime -= time_went * multiplier;
        if (_inactivityTime <= 0) {
            _taskBarIcon->ShowBalloonToolip(langPack->Get("tb_notification_start_after_pause"));
            RestartBigPauseInterval();
            RestartMiniPauseInterval();

            SaveSettings();
        }

        UpdateTaskbarText();
    } break;

    case STATE_FIRST_LAUNCH: {
        wxString text = wxString::Format(langPack->Get("tb_notification_first_launch"), getTimeStr(_bigPauseInterval, MINUTES, _lang));
        _taskBarIcon->ShowBalloonToolip(text);

        _firstLaunch = false;

        SaveSettings();
        ApplySettings();
    } break;

    case STATE_IDLE:
        // Water counts as a reason to stay in idle. Without this, turning both breaks off would also
        // silently stop the hydration reminder, with nothing on screen to say so.
        if (_enableBigPause || _enableMiniPause || _enableWaterReminder) {
            RepeatState();

            UpdateTaskbarText();

            CheckSettings();

            if (_settingInactivityTracking) {
                if (_inactivityTime >= 8 * 60 * 1000) // 8 mins
                {
                    AutoRelax();
                    break;
                }
            }

            // Deliberately after the auto-relax check and outside every multiplier: a reminder is not
            // a break, so it should not arrive eight times faster because the break timers do, and it
            // should not pile up against someone who has left the desk. Auto-relax gets here only
            // while the machine is still in use, which is the only case worth reminding about.
            if (_enableWaterReminder && _timeToWaterReminder > 0) {
                _timeToWaterReminder -= time_went;

                if (_timeToWaterReminder <= 0) {
                    _timeToWaterReminder = 0;
                    if (!_enableBigPause && !_enableMiniPause) {
                        // Nobody is going to interrupt, so do not put a bubble over the screen.
                        RestartWaterInterval();
                    } else {
                        ShowWaterReminder();
                        RestartWaterInterval();
                    }
                }
            }

            if (_enableBigPause && _timeLeftToBigPause > 0) {
                int multiplier = _fastMode ? 8 : 1;
                _timeLeftToBigPause -= time_went * multiplier;

                if (_warningInterval > 0.0f) {
                    if (_timeLeftToBigPause <= _warningInterval * 60 * 1000 && _timeLeftToBigPause > eyeleo::settings::timeForLongBreakConfirmation * 1000) {
                        if (!NotificationWindow::hasAnyInstance() && !_showedLongBreakCountdown) {
                            // open the countdown window, but not over a fullscreen app
                            int fullscreenDisplay = -1;
                            bool isFullscreen = IsFullscreenAppRunning(&fullscreenDisplay);

                            for (int displayInd = 0; displayInd < osCaps.numDisplays; ++displayInd) {
                                if (isFullscreen && fullscreenDisplay == displayInd)
                                    continue;

                                _timeLeftToBigPause = (long)(_warningInterval * 60 * 1000);

                                NotificationWindow *wnd = new NotificationWindow();
                                wnd->Init();
                                wnd->SetTime(_timeLeftToBigPause);
                                wnd->Show(true);
                                _notificationWnd = wnd;

                                logging::msg(wxString("Countdown window opened"));

                                _showedLongBreakCountdown = true;
                                //_fastMode = false;
                                break;
                            }

                            if (!_showedLongBreakCountdown)
                                logging::msg("Couldn't show a countdown because of fullscreen app");
                        }
                    }
                }

                if (_timeLeftToBigPause <= eyeleo::settings::timeForLongBreakConfirmation * 1000) {
                    ChangeState(STATE_START_BIG_PAUSE, 100);
                    break;
                }
            }
            if (_enableMiniPause) {
                if (_timeLeftToMiniPause > 0) {
                    int multiplier = _fastMode ? 2 : 1;
                    _timeLeftToMiniPause -= time_went * multiplier;
                }
                if (!NotificationWindow::hasAnyInstance() && !_showedLongBreakCountdown) // don't show mini-pause if big pause is about to start
                {
                    if (_timeLeftToMiniPause <= 0) {
                        StartMiniPause();
                        SaveSettings();
                    }
                }
            }
        } else {
            ChangeState(STATE_SUSPENDED, 300);
        }
        break;

    case STATE_WAITING_SCREEN: {
        // Note that we might be in this state not only because of fullscreen application, but also because of locked OS
        logging::msg(wxString::Format("State: Waiting screen: _fullscreenBlockDuration=%d", _fullscreenBlockDuration));
        RepeatState();

        int multiplier = _fastMode ? 1 : 1;
        _fullscreenBlockDuration += time_went * multiplier;
        _timeUntilWaitingWnd += time_went * multiplier;

        if (_timeUntilWaitingWnd >= 1000 * 60 * 1 && _fullscreenBlockDuration < 1000 * 60 * 3) // should appear 2 times with 1 min interval after 1 min of wait
        {
            ShowWaitingWnd();
        }

        if (_fullscreenBlockDuration >= 1000 * 60 * 5) // after 5 mins
        {
            logging::msg("Restarting big pause after 5 mins waiting");

            RestartBigPauseInterval(); // cancel current big pause
            SaveSettings();
        } else {
            bool fullscreenBlock = IsFullscreenAppRunning();
            if (fullscreenBlock) {
                ChangeState(STATE_WAITING_SCREEN, 3000);
            } else {
                logging::msg("Big pause no longer blocked, starting it...");
                CloseWaitingWnd();
                ChangeState(STATE_START_BIG_PAUSE, 3000);
            }
        }
    } break;

    case STATE_START_BIG_PAUSE: {
        logging::msg(wxString("State: Start big pause"));
        if (_enableStrictMode) {
            HWND hwnd;
            bool fullscreenBlock = IsFullscreenAppRunning(0, &hwnd);
            if (fullscreenBlock && hwnd) {
                ShowWindow(hwnd, SW_FORCEMINIMIZE);
                ChangeState(STATE_START_BIG_PAUSE, 2000);
            } else {
                StartBigPause();
            }
        } else {
            bool fullscreenBlock = IsFullscreenAppRunning();
            if (!fullscreenBlock) {
                AskForBigPause();
            } else {
                logging::msg(wxString("Couldn't start big pause because of fullscreen block"));

                ShowWaitingWnd();
                _fullscreenBlockDuration = 0;
                _timeUntilWaitingWnd = 0;
                ChangeState(STATE_WAITING_SCREEN, 1000);
            }
        }
    } break;

    case STATE_AUTO_RELAX:
        RepeatState();
        logging::msg(wxString("State: Auto relax"));
        _inactivityTime += time_went;

        if (!CheckInactivity()) {
            OnUserActivity();
        }
        break;

    case STATE_RELAXING: // in 'long pause'
    {
        //logging::msg(wxString::Format("State: Relaxing: _relaxingTimeLeft=%d", _relaxingTimeLeft));
        int multiplier = _fastMode ? 1 : 1;
        _relaxingTimeLeft -= time_went * multiplier;

        if (_relaxingTimeLeft < 0) {
#ifdef WIN32
            if (_enableSounds)
                ::PlaySound(L"SystemExclamation", NULL, SND_ALIAS | SND_ASYNC);
#endif
            StopBigPause();
            SaveSettings();
        } else {
            RepeatState();
        }
    } break;

    case STATE_DESTROY: {
        Exit();
    } break;
    }

    UpdateDebugWindow();
}

void EyeApp::UpdateDebugWindow() {
    if (!_debugWindow)
        return;
    _debugWindow->_timeLeftToBigPause->SetLabel(wxString::Format(L"%d", _timeLeftToBigPause));
    _debugWindow->_timeLeftToMiniPause->SetLabel(wxString::Format(L"%d", _timeLeftToMiniPause));
    _debugWindow->_inactivityTime->SetLabel(wxString::Format(L"%d", _inactivityTime));
    _debugWindow->_relaxingTimeLeft->SetLabel(wxString::Format(L"%d", _relaxingTimeLeft));
}

void EyeApp::AskForBigPause() {
    logging::msg("AskForBigPause()");

    BeforePauseWindow *wnd = new BeforePauseWindow(0, _postponeCount);
    wnd->Init();
    wnd->Show(true);

    _beforePauseWnds.push_back(wnd);
}

void EyeApp::CloseBeforePauseWnds() {
    for (std::vector<BeforePauseWindow *>::iterator wnd = _beforePauseWnds.begin(); wnd != _beforePauseWnds.end(); ++wnd) {
        (*wnd)->Hide();
    }
}

void EyeApp::PostponeBigPause() {
    _showedLongBreakCountdown = false;
    _userPostponeCount++;
    _postponeCount++;
    _inactivityTime = 0;
    _timeLeftToBigPause = 3000 * 60; // 3 mins
    ChangeState(STATE_IDLE, 1000);

    if (_timeLeftToMiniPause <= 3000) { // 3 seconds
        _timeLeftToMiniPause = 3000;    // postpone upcoming mini-pause just a little bit
    }

    UpdateTaskbarText();
}

void EyeApp::RefuseBigPause() {
    _userRefuseCount++;
    RestartBigPauseInterval();

    SaveSettings();
}

void EyeApp::AutoRelax() {
    logging::msg("AutoRelax");

    UninstallActivityMonitor();
    InstallActivityMonitor();

    _inactivityTime = 0;
    _timeLeftToBigPause = 0;
    _timeLeftToMiniPause = 0;
    _userAutoBreakCount++;
    ChangeState(STATE_AUTO_RELAX, 500);
    UpdateTaskbarText();
}

void EyeApp::StartBigPause(bool demo) {
    if (!_bigPauseWnds.empty()) {
        // we should only Get here from Settings Wnd
        return;
    }

    logging::msg("StartBigPause");

    _showedLongBreakCountdown = false;

    if (_notificationWnd) {
        _notificationWnd->Hide();
        _notificationWnd = nullptr;
    }

    bool shortDemoPause = demo && _enableStrictMode;
    static const int ShortDemoPauseDurationSec = 5;

    bool fullscreenBlock = IsFullscreenAppRunning();
    if (!fullscreenBlock) {
        _userLongBreakCount++;

        StopMiniPause();

        assert(_bigPauseWnds.empty());
        for (int displayInd = 0; displayInd < osCaps.numDisplays; ++displayInd) {
            BigPauseWindow *wnd = new BigPauseWindow(displayInd);
            logging::msg(wxString::Format("_bigPauseDuration = %d", _bigPauseDuration * 60));
            wnd->Init();

            int seconds = !shortDemoPause ? _bigPauseDuration * 60 : ShortDemoPauseDurationSec;

            wnd->SetBreakDuration(seconds);
            wnd->Show(true);
            _bigPauseWnds.push_back(wnd);
        }
        _bigPauseWnds[0]->SetFocus();

        _relaxingTimeLeft = !shortDemoPause ? _bigPauseDuration * 1000 * 60 : ShortDemoPauseDurationSec * 1000;

        ChangeState(STATE_RELAXING, 300);
    } else {
        logging::msg("fullscreen block, show wait wnd");

        // ShowWaitingWnd();
        _fullscreenBlockDuration = 0;
        _timeUntilWaitingWnd = 0;
        ChangeState(STATE_WAITING_SCREEN, 1000);
    }
}

void EyeApp::ShowWaitingWnd() {
    if (!_waitWnds.empty()) {
        logging::msg(wxString::Format("ShowWaitingWnd() failed, _waitWnds not empty"));
        return;
    }

    logging::msg(wxString::Format("ShowWaitingWnd()"));

    int fullscreenDisplay = -1;
    IsFullscreenAppRunning(&fullscreenDisplay);

    for (int displayInd = 0; displayInd < osCaps.numDisplays; ++displayInd) {
        if (fullscreenDisplay == displayInd)
            continue;

        logging::msg(wxString::Format("ShowWaitingWnd shows wnd at disp %d", displayInd));

        WaitingFullscreenWindow *wnd = new WaitingFullscreenWindow();
        wnd->Init(displayInd);
        wnd->Show();

        _waitWnds.push_back(wnd);

        _timeUntilWaitingWnd = 0;
    }
}

void EyeApp::CloseWaitingWnd() {
    logging::msg(wxString::Format("CloseWaitingWnd"));

    if (!_waitWnds.empty()) {
        for (std::vector<WaitingFullscreenWindow *>::iterator it = _waitWnds.begin(); it != _waitWnds.end(); ++it) {
            WaitingFullscreenWindow *wnd = (*it);
            wnd->Hide();
        }
        //_waitWnds.resize(0);
    }
}

void EyeApp::OnCloseWaitingWnd(WaitingFullscreenWindow *ptr) {
    std::vector<WaitingFullscreenWindow *>::iterator it = std::find(_waitWnds.begin(), _waitWnds.end(), ptr);
    if (it != _waitWnds.end()) {
        _waitWnds.erase(it);
    } else {
        logging::msg(wxString::Format("OnCloseWaitingWnd failed to find wnd"));
    }
}

void EyeApp::OnCloseBeforePauseWnd(BeforePauseWindow *ptr) {
    std::vector<BeforePauseWindow *>::iterator it = std::find(_beforePauseWnds.begin(), _beforePauseWnds.end(), ptr);
    if (it != _beforePauseWnds.end()) {
        _beforePauseWnds.erase(it);
    }
}

void EyeApp::CloseBigPauseWnds() {
    if (!_bigPauseWnds.empty()) {
        for (std::vector<BigPauseWindow *>::iterator it = _bigPauseWnds.begin(); it != _bigPauseWnds.end(); ++it) {
            BigPauseWindow *wnd = (*it);
            wnd->Hide();
        }
    }
}

void EyeApp::StopBigPause() {
    logging::msg(wxString::Format("EyeApp::StopBigPause, bigPauseWnds.size=%d", _bigPauseWnds.size()));

    CloseBigPauseWnds();

    RestartBigPauseInterval();
    RestartMiniPauseInterval();

    SaveSettings();

    logging::msg(wxString::Format("EyeApp::StopBigPause end"));
}

void EyeApp::OnSkipBigPauseClicked() {
    long fullPeriod = _bigPauseDuration * 1000 * 60;
    long earlyThreshold = long(float(fullPeriod) * 0.35f);

    if (_relaxingTimeLeft > earlyThreshold)
        _userEarlySkipCount++;
    else
        _userLateSkipCount++;

    StopBigPause();
}

void EyeApp::StartMiniPause() {
    logging::msg("StartMiniPause");

    int fullscreenDisplay = -1;
    IsFullscreenAppRunning(&fullscreenDisplay);

    if (_miniPauseWnds.empty()) {
        _userShortBreakCount++;

        for (int displayInd = 0; displayInd < osCaps.numDisplays; ++displayInd) {
            if (fullscreenDisplay == displayInd)
                continue;

            MiniPauseWindow *wnd = new MiniPauseWindow(displayInd, _userShortBreakCount);
            wnd->Init(_miniPauseFullscreenEnabled);
            wnd->Show(true);

            _miniPauseWnds.push_back(wnd);
        }
    } else {
        logging::msg("(!) _miniPauseWnds is not empty");
    }

    RestartMiniPauseInterval();
}

void EyeApp::StopMiniPause() {
    if (!_miniPauseWnds.empty()) {
        for (std::vector<MiniPauseWindow *>::iterator it = _miniPauseWnds.begin(); it != _miniPauseWnds.end(); ++it) {
            MiniPauseWindow *wnd = (*it);
            wnd->Hide();
        }
    }

    RestartMiniPauseInterval();
}

void EyeApp::OnMiniPauseWindowClosed(MiniPauseWindow *ptr) {
    std::vector<MiniPauseWindow *>::iterator it = std::find(_miniPauseWnds.begin(), _miniPauseWnds.end(), ptr);
    assert(it != _miniPauseWnds.end());
    if (it != _miniPauseWnds.end())
        _miniPauseWnds.erase(it);
}

void EyeApp::OnBigPauseWindowClosed(BigPauseWindow *ptr) {
    std::vector<BigPauseWindow *>::iterator it = std::find(_bigPauseWnds.begin(), _bigPauseWnds.end(), ptr);
    assert(it != _bigPauseWnds.end());
    if (it != _bigPauseWnds.end())
        _bigPauseWnds.erase(it);

    it = std::find(_bigPauseWnds.begin(), _bigPauseWnds.end(), ptr);
    assert(it == _bigPauseWnds.end());
}

void EyeApp::OnNotificationWindowClosed() {
    _notificationWnd = nullptr;
}

void EyeApp::OnDrinkReminderWindowClosed(DrinkReminderWindow *wnd) {
    // Whichever of the two reminders it was: a window that closed itself after its countdown must not
    // leave a pointer to it behind.
    if (_waterReminderWnd == wnd)
        _waterReminderWnd = nullptr;
    if (_teaReminderWnd == wnd)
        _teaReminderWnd = nullptr;
}

void EyeApp::OnDebugWindowClosed() {
    _debugWindow = nullptr;
}

void EyeApp::OpenSettings() {
    if (_settingsWnd) {
        ::SetForegroundWindow(_settingsWnd->GetHWND());
        return;
    }

    SettingsWindow *wnd = new SettingsWindow(langPack->Get("settings_title"));
    wnd->Show(true);
    _settingsWnd = wnd;

    _seenSettingsWindow = true;
}

bool EyeApp::LoadSettings() {
    logging::msg("LoadSettings");

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file((GetSavePath() + L"settings.xml").wchar_str());

    if (result.status != pugi::status_ok)
        return false;

    pugi::xml_node nodeSettings = doc.child(L"settings");
    if (nodeSettings.empty())
        return false;

    for (pugi::xml_node node = nodeSettings.first_child(); node; node = node.next_sibling()) {
        const wchar_t *name = node.name();
        if (wcscmp(name, L"statistics") == 0) {
            _firstLaunch = node.attribute(L"first_launch").as_bool();
            _seenSettingsWindow = node.attribute(L"seen_settings").as_bool();
            _userLongBreakCount = node.attribute(L"long_break_count").as_uint();
            _userEarlySkipCount = node.attribute(L"early_skip_count").as_uint();
            _userLateSkipCount = node.attribute(L"late_skip_count").as_uint();
            _userRefuseCount = node.attribute(L"refuse_count").as_uint();
            _userPostponeCount = node.attribute(L"postpone_count").as_uint();
            _userAutoBreakCount = node.attribute(L"auto_break_count").as_uint();
            _userShortBreakCount = node.attribute(L"short_break_count").as_uint();
            _lastShutdown.ParseISOCombined(node.attribute(L"last_shutdown").value());
            _lastBigPauseTimeLeft = node.attribute(L"last_big_pause_time_left").as_uint();
            _lastMiniPauseTimeLeft = node.attribute(L"last_mini_pause_time_left").as_uint();
        } else if (wcscmp(name, L"big_pause") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _enableBigPause = enabled;

            int interval = node.attribute(L"interval").as_int();
            _bigPauseInterval = interval;

            int duration = node.attribute(L"duration").as_int();
            _bigPauseDuration = duration;
        } else if (wcscmp(name, L"mini_pause") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _enableMiniPause = enabled;

            int interval = node.attribute(L"interval").as_int();
            if (interval < 5) // correction for old version
                interval = 5;
            _miniPauseInterval = interval;

            int duration = node.attribute(L"duration").as_int();
            if (duration == 0)
                duration = 8;
            _miniPauseDuration = duration;
        } else if (wcscmp(name, L"warning") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _enableWarning = enabled;

            float interval = node.attribute(L"interval").as_float();
            _warningInterval = interval;
        } else if (wcscmp(name, L"sounds") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _enableSounds = enabled;
        } else if (wcscmp(name, L"strict_mode") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _enableStrictMode = enabled;
        } else if (wcscmp(name, L"window_nearby") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _settingWindowNearby = enabled;
        } else if (wcscmp(name, L"can_close_notifications") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _settingCanCloseNotifications = enabled;
        } else if (wcscmp(name, L"inactivity_tracking") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _settingInactivityTracking = enabled;
        } else if (wcscmp(name, L"show_notifications") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _showNotificationsEnabled = enabled;
        } else if (wcscmp(name, L"mini_pause_fullscreen_mode") == 0) {
            bool enabled = node.attribute(L"enabled").as_bool();
            _miniPauseFullscreenEnabled = enabled;
        } else if (wcscmp(name, L"water") == 0) {
            // Absent in a settings file from before this existed, in which case the defaults stand.
            _enableWaterReminder = node.attribute(L"enabled").as_bool(_enableWaterReminder);
            _waterInterval = node.attribute(L"interval").as_int(_waterInterval);
            _waterVolume = node.attribute(L"volume").as_int(_waterVolume);
        }
    }
    return true;
}

// Snaps a value onto the list of allowed choices. A settings file written by another build, or edited
// by hand, can hold a value that is not on the list; rounding to the nearest entry keeps the setting
// usable instead of silently invalid.
static int SnapToList(int value, const int *list, int count) {
    int best = list[0];
    long bestDiff = labs((long)value - (long)best);
    for (int i = 1; i < count; i++) {
        long diff = labs((long)value - (long)list[i]);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = list[i];
        }
    }
    return best;
}

void EyeApp::CheckSettings() {
    if (_bigPauseInterval > 120) {
        logging::msg("CheckSettings: bigPauseInterval corrected");
        assert(false);

        _bigPauseInterval = 120; // in minutes
    }

    if (_bigPauseDuration > 30) {
        logging::msg("CheckSettings: bigPauseDuration corrected");
        assert(false);

        _bigPauseDuration = 30;
    }

    if (_warningInterval > 3.0f) {
        logging::msg("CheckSettings: warningInterval corrected");
        assert(false);

        _warningInterval = 3.0f;
    }

    if (_miniPauseInterval > 30) {
        logging::msg("CheckSettings: miniPauseInterval corrected");
        assert(false);

        _miniPauseInterval = 30;
    }

    if (_miniPauseDuration > 20) {
        logging::msg("CheckSettings: miniPauseDuration corrected");
        assert(false);

        _miniPauseDuration = 20;
    }

    {
        int numIntervals = sizeof(kWaterIntervalsMin) / sizeof(kWaterIntervalsMin[0]);
        int snapped = SnapToList(_waterInterval, kWaterIntervalsMin, numIntervals);
        if (snapped != _waterInterval) {
            logging::msg(wxString::Format(L"CheckSettings: waterInterval %d -> %d", _waterInterval,
                                           snapped));
            _waterInterval = snapped;
        }
        int numVolumes = sizeof(kWaterVolumesMl) / sizeof(kWaterVolumesMl[0]);
        snapped = SnapToList(_waterVolume, kWaterVolumesMl, numVolumes);
        if (snapped != _waterVolume) {
            logging::msg(wxString::Format(L"CheckSettings: waterVolume %d -> %d", _waterVolume, snapped));
            _waterVolume = snapped;
        }
    }

    if (_timeLeftToBigPause > 1000 * 60 * _bigPauseInterval) {
        logging::msg("CheckSettings: _timeLeftToBigPause corrected");
        assert(false);

        _timeLeftToBigPause = 1000 * 60 * _bigPauseInterval;
    }

    if (_timeLeftToMiniPause > 1000 * 60 * _miniPauseInterval) {
        logging::msg("CheckSettings: _timeLeftToMiniPause corrected");
        assert(false);

        _timeLeftToMiniPause = 1000 * 60 * _miniPauseInterval;
    }

    // long _relaxingTimeLeft;
    // long _fullscreenBlockDuration;
    // long _inactivityTime;
}

void EyeApp::SaveSettings() {
    _lastBigPauseTimeLeft = _timeLeftToBigPause;
    _lastMiniPauseTimeLeft = _timeLeftToMiniPause;

    ///
    logging::msg("SaveSettings");

    pugi::xml_document doc;
    pugi::xml_node node = doc.append_child(pugi::node_element);
    node.set_name(L"settings");

    pugi::xml_node nodeStatistics = node.append_child(pugi::node_element);
    nodeStatistics.set_name(L"statistics");
    nodeStatistics.append_attribute(L"first_launch") = _firstLaunch;
    nodeStatistics.append_attribute(L"seen_settings") = _seenSettingsWindow;
    nodeStatistics.append_attribute(L"long_break_count") = _userLongBreakCount;
    nodeStatistics.append_attribute(L"early_skip_count") = _userEarlySkipCount;
    nodeStatistics.append_attribute(L"late_skip_count") = _userLateSkipCount;
    nodeStatistics.append_attribute(L"refuse_count") = _userRefuseCount;
    nodeStatistics.append_attribute(L"postpone_count") = _userPostponeCount;
    nodeStatistics.append_attribute(L"auto_break_count") = _userAutoBreakCount;
    nodeStatistics.append_attribute(L"short_break_count") = _userShortBreakCount;
    if (_lastShutdown.IsValid())
        nodeStatistics.append_attribute(L"last_shutdown") = _lastShutdown.FormatISOCombined().c_str();

    assert(_lastBigPauseTimeLeft < INT_MAX);
    nodeStatistics.append_attribute(L"last_big_pause_time_left") = (int)(_lastBigPauseTimeLeft);

    assert(_lastMiniPauseTimeLeft < INT_MAX);
    nodeStatistics.append_attribute(L"last_mini_pause_time_left") = (int)(_lastMiniPauseTimeLeft);

    pugi::xml_node nodeBigPause = node.append_child(pugi::node_element);
    nodeBigPause.set_name(L"big_pause");
    nodeBigPause.append_attribute(L"enabled") = GetBigPauseEnabled();
    nodeBigPause.append_attribute(L"interval") = GetBigPauseInterval();
    nodeBigPause.append_attribute(L"duration") = GetBigPauseDuration();

    pugi::xml_node nodeMiniPause = node.append_child(pugi::node_element);
    nodeMiniPause.set_name(L"mini_pause");
    nodeMiniPause.append_attribute(L"enabled") = GetMiniPauseEnabled();
    nodeMiniPause.append_attribute(L"interval") = GetMiniPauseInterval();
    nodeMiniPause.append_attribute(L"duration") = GetMiniPauseDuration();

    pugi::xml_node nodeWarning = node.append_child(pugi::node_element);
    nodeWarning.set_name(L"warning");
    nodeWarning.append_attribute(L"enabled") = GetWarningEnabled();
    nodeWarning.append_attribute(L"interval") = GetWarningInterval();

    pugi::xml_node nodeSounds = node.append_child(pugi::node_element);
    nodeSounds.set_name(L"sounds");
    nodeSounds.append_attribute(L"enabled") = GetSoundsEnabled();

    pugi::xml_node nodeStrictMode = node.append_child(pugi::node_element);
    nodeStrictMode.set_name(L"strict_mode");
    nodeStrictMode.append_attribute(L"enabled") = GetStrictModeEnabled();

    pugi::xml_node nodeWindowNearby = node.append_child(pugi::node_element);
    nodeWindowNearby.set_name(L"window_nearby");
    nodeWindowNearby.append_attribute(L"enabled") = GetWindowNearbySetting();

    pugi::xml_node nodeInactivityTracking = node.append_child(pugi::node_element);
    nodeInactivityTracking.set_name(L"inactivity_tracking");
    nodeInactivityTracking.append_attribute(L"enabled") = GetInactivityTrackingEnabled();

    pugi::xml_node nodeCanCloseNotifications = node.append_child(pugi::node_element);
    nodeCanCloseNotifications.set_name(L"can_close_notifications");
    nodeCanCloseNotifications.append_attribute(L"enabled") = GetCanCloseNotificationsSetting();

    pugi::xml_node nodeShowNotifications = node.append_child(pugi::node_element);
    nodeShowNotifications.set_name(L"show_notifications");
    nodeShowNotifications.append_attribute(L"enabled") = GetShowNotificationsEnabled();

    pugi::xml_node nodeMiniPauseFullscreenMode = node.append_child(pugi::node_element);
    nodeMiniPauseFullscreenMode.set_name(L"mini_pause_fullscreen_mode");
    nodeMiniPauseFullscreenMode.append_attribute(L"enabled") = GetMiniPauseFullscreenEnabled();

    pugi::xml_node nodeWater = node.append_child(pugi::node_element);
    nodeWater.set_name(L"water");
    nodeWater.append_attribute(L"enabled") = GetWaterReminderEnabled();
    nodeWater.append_attribute(L"interval") = GetWaterInterval();
    nodeWater.append_attribute(L"volume") = GetWaterVolume();

    doc.save_file((GetSavePath() + L"settings.xml").wchar_str(), L"\t");
}

void EyeApp::RestartWaterInterval() {
    // Deliberately not persisted: the first reminder of a session always comes a full interval in, so
    // restarting the app cannot be used to skip a drink.
    _timeToWaterReminder = (long)_waterInterval * 60 * 1000;
}

void EyeApp::ShowWaterReminder() {
    if (DrinkReminderWindow::HasInstance()) {
        logging::msg("water reminder not shown: another reminder already holds the slot");
        return;
    }

    int fullscreenDisplay = -1;
    bool isFullscreen = IsFullscreenAppRunning(&fullscreenDisplay);

    for (int displayInd = 0; displayInd < osCaps.numDisplays; ++displayInd) {
        if (isFullscreen && fullscreenDisplay == displayInd)
            continue;

        DrinkReminderWindow *wnd = new DrinkReminderWindow(DRINK_WATER, langPack->Get(L"water_reminder_label"),
                                                          kDrinkReminderSec * 1000);
        if (!wnd->Init(displayInd)) {
            delete wnd;
            return;
        }
        _waterReminderWnd = wnd;
        break;
    }

    if (!_waterReminderWnd)
        logging::msg("water reminder not shown: every display is fullscreen");
}

void EyeApp::CloseWaterReminder() {
    // Same reasoning as CloseTeaReminder: the pointer outlives the fade on purpose.
    if (_waterReminderWnd)
        _waterReminderWnd->Hide();
}

void EyeApp::ResetSettings() {
    _enableBigPause = true;
    _bigPauseInterval = 50;
    _bigPauseDuration = 5;
    _enableMiniPause = true;
    _miniPauseInterval = 10;
    _miniPauseDuration = 8;
    _enableWarning = true;
    _warningInterval = 0.5f;
    _enableSounds = true;
    _enableStrictMode = false;
    _settingWindowNearby = true;
    _settingCanCloseNotifications = false;
    _firstLaunch = true;
    _seenSettingsWindow = false;
    _settingInactivityTracking = true;
    _showNotificationsEnabled = true;
    _miniPauseFullscreenEnabled = false;
    _enableWaterReminder = true;
    _waterInterval = kWaterIntervalDefaultMin;
    _waterVolume = kWaterVolumeDefaultMl;
}

void EyeApp::ApplySettings() {
    logging::msg("ApplySettings");

    RestartBigPauseInterval();
    RestartMiniPauseInterval();
    RestartWaterInterval();
    UpdateTaskbarText();
}

void EyeApp::OnSettingsClosed() {
    logging::msg("OnSettingsClosed");

    _settingsWnd = 0;

    if (_enableBigPause) {
        int newInterval = _bigPauseInterval * 1000 * 60;
        if (newInterval < _timeLeftToBigPause)
            _timeLeftToBigPause = newInterval;

        if (_timeLeftToBigPause == 0)
            RestartBigPauseInterval();
    } else {
        if (_timeLeftToBigPause > 0) {
            _timeLeftToBigPause = 0;
            UpdateTaskbarText();
        }
    }

    if (_enableMiniPause) {
        int newInterval = _miniPauseInterval * 1000 * 60;
        if (newInterval < _timeLeftToMiniPause)
            _timeLeftToMiniPause = newInterval;

        if (_timeLeftToMiniPause == 0) {
            RestartMiniPauseInterval();
            if (GetNextState() == STATE_SUSPENDED)
                ChangeState(STATE_IDLE, 1000);
        }
    } else {
        if (_timeLeftToMiniPause > 0)
            _timeLeftToMiniPause = 0;
    }

    logging::msg(wxString::Format("    _timeLeftToMiniPause = %d, _timeLeftToBigPause = %d", _timeLeftToMiniPause, _timeLeftToBigPause));
}

void EyeApp::TogglePausedMode(int minutes) {
    if (GetNextState() == STATE_IDLE || GetNextState() == STATE_NONE || GetNextState() == STATE_WAITING_SCREEN) {
        // pause for an hour
        _inactivityTime = minutes * 1000 * 60; // in ms
        ChangeState(STATE_SUSPENDED, 500);
        UpdateTaskbarText();

        if (_notificationWnd)
            _notificationWnd->Hide();
        CloseWaitingWnd();
        CloseBeforePauseWnds();
        CloseBigPauseWnds();
    } else if (GetNextState() == STATE_SUSPENDED) {
        RestartBigPauseInterval();
        RestartMiniPauseInterval();
        UpdateTaskbarText();

        SaveSettings();
    }
}

bool EyeApp::isPausedMode() const {
    return GetNextState() == STATE_SUSPENDED;
}

void EyeApp::TakeLongBreakNow() {
    int state = GetNextState();

    if (state == STATE_IDLE || state == STATE_AUTO_RELAX || state == STATE_RELAXING) {
        StartBigPause();
    }

    CloseWaitingWnd();
}

void EyeApp::Exit() {
    if (!_miniPauseWnds.empty()) {
        for (std::vector<MiniPauseWindow *>::iterator it = _miniPauseWnds.begin(); it != _miniPauseWnds.end(); ++it) {
            MiniPauseWindow *wnd = (*it);
            wnd->Destroy();
        }
        _miniPauseWnds.resize(0);

        // signal to call Exit afterwards
        ChangeState(STATE_DESTROY, 50); // still unstable
    } else if (!_bigPauseWnds.empty()) {
        for (std::vector<BigPauseWindow *>::iterator it = _bigPauseWnds.begin(); it != _bigPauseWnds.end(); ++it) {
            BigPauseWindow *wnd = (*it);
            wnd->Destroy();
        }
        _bigPauseWnds.resize(0);
    } else {
        wxApp::Exit();
    }
}

int EyeApp::OnExit() {
    logging::msg("OnExit");

    static int destroyCount = 0;
    if (destroyCount)
        return 0;
    destroyCount++;

    _lastShutdown = wxDateTime::Now();
    SaveSettings();

    _finished = true;

    DeletePendingEvents();

    UninstallActivityMonitor();
    _taskBarIcon->RemoveIcon();

    Stop();

    // The reminder is an ordinary frame with a task of its own, so it has to go before the task
    // manager stops; leaving it up would keep it registered and tick against a dead manager.
    CloseWaterReminder();
    CloseTeaReminder();
    _tea.Stop();

    DeleteLanguagePack();
    delete g_Personage;

    int res = wxApp::OnExit();
    logging::msg("done OnExit");
    return res;
}

void EyeApp::OnQueryEndSession(wxCloseEvent &evt) {
    logging::msg("OnQueryEndSession");

    DeletePendingEvents();
    getApp()->Exit();

    wxApp::OnQueryEndSession(evt);

    logging::msg("done OnQueryEndSession");
}

void EyeApp::OnEndSession(wxCloseEvent &evt) {
    logging::msg("OnEndSession");

    UninstallActivityMonitor();
    _taskBarIcon->RemoveIcon();

    Stop();

    CloseWaterReminder();
    CloseTeaReminder();

    wxApp::OnEndSession(evt);

    logging::msg("done OnEndSession");
}

void EyeApp::OnSessionUnlock() {
    logging::msg("EyeApp::OnSessionUnlock");

    _taskBarIcon->ShowBalloonToolip(langPack->Get("tb_notification_start_after_pause"));

    RestartBigPauseInterval();
    RestartMiniPauseInterval();
}

///////////////////////////////////////////////////////////////////////////////////////

EyeTaskBarIcon::EyeTaskBarIcon()
    : wxTaskBarIcon()
    , _menu(0)
    , _teaSubMenu(0) {
    _icon = new wxIcon(L"Resources/icon.ico", wxBITMAP_TYPE_ICO, 16, 16);
    if (!_icon->IsOk()) // case for larger fonts
    {
        delete _icon;
        _icon = new wxIcon(L"Resources/icon.ico", wxBITMAP_TYPE_ICO);
    }

    _iconGray = new wxIcon(L"Resources/icongray.ico", wxBITMAP_TYPE_ICO, 16, 16);
    if (!_iconGray->IsOk()) // case for larger fonts
    {
        delete _iconGray;
        _iconGray = new wxIcon(L"Resources/icongray.ico", wxBITMAP_TYPE_ICO);
    }

    _iconSettings = new wxIcon(L"Resources/settings.ico", wxBITMAP_TYPE_ICO, 16, 16);
    _iconPause = new wxIcon(L"Resources/pause.ico", wxBITMAP_TYPE_ICO, 16, 16);
    _iconResume = new wxIcon(L"Resources/resume.ico", wxBITMAP_TYPE_ICO, 16, 16);

    if (!SetIcon(*_icon, langPack->Get("tb_popup_default")))
        wxMessageBox(wxT("Could not set icon."));

    Connect(wxEVT_TASKBAR_LEFT_DOWN, wxMouseEventHandler(EyeTaskBarIcon::OnLeftButtonDown));
    Connect(ID_TASKBAR_MENU_QUIT, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnQuit));
    Connect(ID_TASKBAR_MENU_SETTINGS, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnSettings));
    Connect(ID_TASKBAR_MENU_PAUSE_RESUME_MONITORING, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnPauseResumeMonitoring));
    Connect(ID_TASKBAR_MENU_PAUSE_RESUME_MONITORING_2, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnPauseResumeMonitoring2));
    Connect(ID_TASKBAR_MENU_TAKE_LONG_BREAK_NOW, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnTakeLongBreakNow));
    Connect(ID_TASKBAR_MENU_POUR, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(EyeTaskBarIcon::OnPourNextSteep));
    for (int i = 1; i <= 7; i++)
        Connect((int)(ID_TASKBAR_MENU_TEA_BASE + i), wxEVT_COMMAND_MENU_SELECTED,
                wxCommandEventHandler(EyeTaskBarIcon::OnStartTea));
}

void EyeTaskBarIcon::UpdateTooltip(wxString const &text) {
    if (!getApp()->isPausedMode())
        SetIcon(*_icon, text);
    else
        SetIcon(*_iconGray, text);
}

void EyeTaskBarIcon::OnLeftButtonDown(wxMouseEvent /*wxTaskBarIconEvent*/ &WXUNUSED(event)) {
    wxMenu *menu = CreatePopupMenu();
    if (menu) {
        PopupMenu(menu);
        delete menu;
    }
}

void EyeTaskBarIcon::ShowBalloonToolip(wxString const &text, unsigned msec) {
    if (getApp()->GetShowNotificationsEnabled()) {
        ShowBalloon(langPack->Get("tb_popup_default"), text, msec, wxICON_INFORMATION);
    }
}

wxMenu *EyeTaskBarIcon::CreatePopupMenu() {
    // don't allow to open context menu while being in 'long break' in strict mode, because
    // a user may close the application from the context menu
    if (getApp()->GetStrictModeEnabled() && getApp()->GetNextState() == STATE_RELAXING)
        return nullptr;

    _menu = new wxMenu();

    wxMenuItem *item = new wxMenuItem(_menu, ID_TASKBAR_MENU_SETTINGS, langPack->Get("tb_menu_settings"), langPack->Get("tb_menu_settings_tip"));
    item->SetBitmap(*_iconSettings);
    _menu->Append(item);

    if (getApp()->GetNextState() == STATE_SUSPENDED) {
        item = new wxMenuItem(
            _menu, ID_TASKBAR_MENU_PAUSE_RESUME_MONITORING, langPack->Get("tb_menu_resume_monitoring"), langPack->Get("tb_menu_resume_monitoring_tip"));
        item->SetBitmap(*_iconResume);
        _menu->Append(item);
    } else {
        wxMenu *subMenu = new wxMenu();

        item = _menu->AppendSubMenu(subMenu, langPack->Get("tb_menu_pause_monitoring"));
        // item->SetBitmap(*_iconPause);

        wxMenuItem *subitem = new wxMenuItem(
            _menu, ID_TASKBAR_MENU_PAUSE_RESUME_MONITORING, langPack->Get("tb_menu_pause_monitoring_1"), langPack->Get("tb_menu_pause_monitoring_tip"));
        subitem->SetBitmap(*_iconPause);
        subMenu->Append(subitem);

        subitem = new wxMenuItem(
            _menu, ID_TASKBAR_MENU_PAUSE_RESUME_MONITORING_2, langPack->Get("tb_menu_pause_monitoring_2"), langPack->Get("tb_menu_pause_monitoring_tip"));
        subitem->SetBitmap(*_iconPause);
        subMenu->Append(subitem);

        item = new wxMenuItem(_menu, ID_TASKBAR_MENU_TAKE_LONG_BREAK_NOW, langPack->Get("tb_menu_take_break_now"));
        // item->SetBitmap(*_iconSettings);
        _menu->Append(item);
    }

    // The tea section. Water is not here: it has its own switch in the settings and counts working
    // time, while these are steeps the user drives by hand.
    EyeApp *app = getApp();
    // Cleared first on purpose: the menu it used to point at was deleted when that one closed, and the
    // branch below is not taken every time, so a stale handle could otherwise survive into PopupMenu.
    _teaSubMenu = 0;
    if (app->IsTeaMenuEnabled()) {
        wxString pourLabel = langPack->Get("tb_menu_pour");
        // Two different numbers, and only one of them can be showing at a time. While a steep runs, what
        // is worth knowing is how long is left of it. Once it has run out, nothing is left to count and
        // the number that is worth having is how long the next one will run for, because that is what
        // pressing the item commits you to. Same rule as EBTeaSessionNextSeconds on mac.
        int pourSeconds = app->GetTea().NextPourSeconds();
        if (pourSeconds <= 0)
            pourSeconds = app->GetTea().UpcomingSteepSeconds();
        if (pourSeconds > 0)
            pourLabel = wxString::Format(langPack->Get("tb_menu_pour_fmt"), pourSeconds);

        item = new wxMenuItem(_menu, ID_TASKBAR_MENU_POUR, pourLabel);
        item->Enable(app->GetTea().IsAwaitingPour());
        _menu->Append(item);

        wxMenu *teaMenu = new wxMenu();
        _teaSubMenu = teaMenu;
        item = _menu->AppendSubMenu(teaMenu, langPack->Get("tb_menu_new_tea"));

        static const EDrinkKind kTeas[] = {DRINK_GREEN, DRINK_WHITE, DRINK_OOLONG, DRINK_BLACK,
                                           DRINK_RED,   DRINK_PUER,  DRINK_HERBAL};
        for (int i = 0; i < (int)(sizeof(kTeas) / sizeof(kTeas[0])); i++) {
            EDrinkKind kind = kTeas[i];

            // Just the name. The scheme cannot go here and stay readable, and it cannot go into a
            // tooltip wxWidgets does not have, so PopupMenu below puts it on screen on hover instead.
            wxMenuItem *teaItem = new wxMenuItem(teaMenu, (int)(ID_TASKBAR_MENU_TEA_BASE + 1 + i),
                                                 EyeApp::TeaName(kind));
            // wxITEM_CHECK, and not wxITEM_RADIO, because a radio group cannot express "no tea brewing
            // yet", which is a state this menu has. It is also the only kind that draws a tickmark at
            // all: Check() returns immediately on a plain item, at wxCHECK_RET(IsCheckable()), and
            // Append only passes MF_CHECKED on while IsCheck() holds, so without this the tickmark
            // never appears at all.
            teaItem->SetKind(wxITEM_CHECK);
            teaItem->Check(app->GetTea().IsBrewing() && app->GetTea().Kind() == kind);
            teaMenu->Append(teaItem);
        }
        _menu->AppendSeparator();
    }

    item = new wxMenuItem(_menu, ID_TASKBAR_MENU_QUIT, langPack->Get("tb_menu_quit"), langPack->Get("tb_menu_quit_tip"));
    _menu->Append(item);

    return _menu;
}

bool EyeTaskBarIcon::PopupMenu(wxMenu *menu) {
    // Local to this call on purpose: a tip exists exactly as long as the menu is on the screen, which is
    // the only time it has anything to point at, and its destructor is what takes the tooltip window
    // down again. Tracking is also what the timer inside it needs, because that timer runs on the modal
    // loop the tracking holds: there is nothing to see before this call and nothing after it.
    MenuItemTip tip;

    if (menu && _teaSubMenu) {
        EyeApp *app = getApp();
        static const EDrinkKind kTeas[] = {DRINK_GREEN, DRINK_WHITE, DRINK_OOLONG, DRINK_BLACK,
                                           DRINK_RED,   DRINK_PUER,  DRINK_HERBAL};
        for (int i = 0; i < (int)(sizeof(kTeas) / sizeof(kTeas[0])); i++) {
            const TeaSchedule *schedule = app->GetTea().ScheduleFor(kTeas[i]);
            if (!schedule)
                continue;

            wxString list;
            for (int s = 0; s < schedule->count; s++) {
                if (s)
                    list += L", ";
                list += wxString::Format(L"%d", schedule->seconds[s]);
            }
            // No tea name in the text: the tip only appears while the pointer is on that very menu
            // item, which already carries the name. Repeating it there costs width for nothing.
            tip.SetItemText((int)(ID_TASKBAR_MENU_TEA_BASE + 1 + i),
                            wxString::Format(langPack->Get("tea_scheme_fmt"), schedule->count, list,
                                             schedule->temperature));
        }

        tip.Start(menu->GetHMenu(), _teaSubMenu->GetHMenu());
    }

    bool rval = wxTaskBarIcon::PopupMenu(menu);

    _teaSubMenu = 0;

    return rval;
}

void EyeTaskBarIcon::OnPauseResumeMonitoring(wxCommandEvent &) {
    getApp()->TogglePausedMode(60);
}

void EyeTaskBarIcon::OnPauseResumeMonitoring2(wxCommandEvent &) {
    getApp()->TogglePausedMode(180);
}

void EyeTaskBarIcon::OnTakeLongBreakNow(wxCommandEvent &) {
    getApp()->TakeLongBreakNow();
}

void EyeTaskBarIcon::OnPourNextSteep(wxCommandEvent &) {
    // No menu refresh here on purpose. The tray menu is built from scratch on every right click, since
    // CreatePopupMenu allocates a fresh one each time it is called, so the pour countdown and the tea
    // check mark are never stale by the time the user looks at them.
    getApp()->PourNextSteep();
}

void EyeTaskBarIcon::OnStartTea(wxCommandEvent &event) {
    // One handler for all seven: the id is the kind's position in the menu's list, and the list order is
    // fixed, so there is nothing to look up.
    static const EDrinkKind kTeas[] = {DRINK_GREEN, DRINK_WHITE, DRINK_OOLONG, DRINK_BLACK,
                                       DRINK_RED,   DRINK_PUER,  DRINK_HERBAL};
    int index = event.GetId() - (int)ID_TASKBAR_MENU_TEA_BASE - 1;
    if (index < 0 || index >= (int)(sizeof(kTeas) / sizeof(kTeas[0])))
        return;

    getApp()->StartTea(kTeas[index]);
}

void EyeTaskBarIcon::OnQuit(wxCommandEvent &) {
    getApp()->Exit();
}

void EyeTaskBarIcon::OnSettings(wxCommandEvent &) {
    getApp()->OpenSettings();
}
