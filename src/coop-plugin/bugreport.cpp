// bugreport.cpp - REPORT A BUG (T-461; owner decisions 364-378; the approved mock-up build/pages/bug-report-mockup.html).
//
// WHAT THE PLAYER SEES. A small REPORT A BUG button in the title screen's bottom-left corner (369, Kenshi's message-box button
// style), and REPORT A BUG on its own row of Kenshi's pause menu between EXIT GAME and RESUME, set apart by the menu's own gap
// and lettered in the menu buttons' own highlight colour (378 d). Either opens the REPORT A BUG window: Kenshi's framed window
// with a close X, centred, and modal (nothing behind it takes a click). WHAT HAPPENED?, the typed box (a grey hint while it is
// empty; Enter is a new line; at most 1000 characters, 371), the counter "n / 1000", the line "Describe what happened to send a
// report." while the box is empty (SEND is greyed then, 370), CANCEL left and SEND right. SEND greys the box and shows
// "Preparing your report..." then "Sending your report... n%"; CANCEL, X or ESC stop it and give the window back with the text.
// A send that works replaces the window with REPORT SENT (OK); one that fails puts CAN'T SEND REPORT over the window (BACK /
// TRY AGAIN). The text stays until Kenshi closes (375) unless it was sent. Nothing is written to disk (373), there is no report
// number (374) and no limit on reports (376). In the pause menu ESC acts as CANCEL and the menu stays open: the engine's ESC
// closes the menu first, and the tick opens it again through the engine's own show (ui.cpp UiPauseMenuReshow).
//
// WHAT IS SENT (365-367, 372): the description; this launch's and the previous launch's mod logs (src/common/logrotate.h);
// Kenshi's crash file when the previous launch crashed (coopbug::CrashFromLastLaunch); and each nearby player's CURRENT log,
// asked for by LOG_ASK through the world server (route AREA - the games that have this player's sector in their delivery area)
// and answered in LOG_PARTs by slot, without telling that player (377). Every log has its IP addresses replaced and every
// player identity code cut to its first 8 characters before it is compressed (coopbug::ScrubIps) - an answering game scrubs its
// own. Everything goes into one zip under coopbug::kPackLimit,
// the oldest part of the previous launch's log cut first (coopbug::PlanBudget), with report.txt saying what is in it and what
// was cut. The zip is posted (multipart/form-data, WinHTTP, HTTPS) to coopbug::kRelayUrl.
//
// THREADS. MyGUI, the world-server sends and the engine are touched on the main thread only (BugReportTick and the hooks that
// call in). Reading the logs, compressing, packing and the HTTP post run on a worker thread per report, and an answering game
// compresses its log on a worker thread too. They meet only in the state below that g_cs guards. A report's worker carries its
// generation; CANCEL moves the generation on, so a worker that finishes late changes nothing, and its stop flag ends the
// compression and the upload early. Click handlers only write an interlocked action, as in ui.cpp.
//
// THE ONE RULE OF ui.cpp HOLDS HERE: no widget pointer is kept across frames - every widget is looked up by name on the frame
// that uses it. Every name starts "BugReport" and holds no '_' (RE_Kenshi matches Kenshi's layout widgets after the first '_').
// Every MyGUI call runs behind an SEH frame with no C++ object in it (C2712); a memory fault, or the third C++ throw, takes the
// feature down for the rest of the process after removing whatever of it was on screen.

#include "coop_log.h"
#include <mygui/MyGUI_Gui.h>
#include <mygui/MyGUI_Widget.h>
#include <mygui/MyGUI_Button.h>
#include <mygui/MyGUI_TextBox.h>
#include <mygui/MyGUI_EditBox.h>
#include <mygui/MyGUI_Window.h>
#include <mygui/MyGUI_InputManager.h>
#include <mygui/MyGUI_RenderManager.h>
#include <mygui/MyGUI_KeyCode.h>
#include <mygui/MyGUI_ResourceManager.h>
#include <mygui/MyGUI_XmlDocument.h>
#include <mygui/MyGUI_IFont.h>
#include "bugreport.h"
#include "ui.h"                            /* UiFindLayoutSuffix, UiPauseMenuVisible, UiPauseMenuReshow */
#include "titleart.h"                      /* TitleArtNoteBand - the note's place on the mod's title art */
#include "store.h"                         /* StoreSendLive, StoreLiveReady, StoreLiveSendQueued, StoreRosterOtherInWorldTS */
#include "zones.h"                         /* MyPlayerSector */
#include "config.h"                        /* ConfigFilePlayerName */
#include "addresses.h"                     /* AddrTableName, AddrExeFingerprint */
#include "soak.h"                          /* GameplayRunning */
#include "net/session.h"                   /* SessionProtocolVersion */
#include "../common/bugreport.h"
#include "../common/titleart.h"          /* PlaceTitleNote */
#include "../common/pausemenu.h"   /* T-514: REPORT A BUG's button name, shared with ui.cpp's arranging */
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "../common/liverelay.h"           /* kInnerLogAsk / kInnerLogPart, AreaKey, AreaTarget */
#include "../common/liveenvelope.h"        /* kRouteArea, kRouteSlot */
#include "../common/panelstatus.h"         /* NoticeBoxH / NoticeLayoutIn - the message-box layout the other boxes use */
#include "../common/storemeta.h"           /* coopstore::Crc32 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>
#include <process.h>
#pragma comment(lib, "winhttp.lib")
#ifndef WINHTTP_OPTION_SECURE_PROTOCOLS
#define WINHTTP_OPTION_SECURE_PROTOCOLS 84
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 0x00000080
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 0x00000200
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstring>

namespace coop {
namespace {

/* ---- widget names ---- */
const char* const kTitleBtn  = "BugReportTitleButton";
const char* const kTitleNoteP = "BugReportTitleNote";
const char* const kTitleNoteH = "BugReportTitleNoteHead";
const char* const kTitleNoteB = "BugReportTitleNoteBody";
const char* const kAbout1    = "BugReportAbout1";
const char* const kAbout2    = "BugReportAbout2";
const char* const kPauseBtn  = pausemenu::kBugButtonName;
const char* const kWin       = "BugReportWindow";
const char* const kLabel     = "BugReportLabel";
const char* const kText      = "BugReportText";
const char* const kTextHint  = "BugReportTextHint";
const char* const kCounter   = "BugReportCounter";
const char* const kStatus    = "BugReportStatus";
const char* const kCancelBtn = "BugReportCancel";
const char* const kSendBtn   = "BugReportSend";
const char* const kSentBox   = "BugReportSentBox";
const char* const kSentText  = "BugReportSentText";
const char* const kSentOk    = "BugReportSentOk";
const char* const kFailBox   = "BugReportFailBox";
const char* const kFailText  = "BugReportFailText";
const char* const kFailBack  = "BugReportFailBack";
const char* const kFailRetry = "BugReportFailRetry";

/* ---- colours ---- */
/* Kenshi_MessageBox.layout's MessageText colour, which every box of the mod uses for its text and its Kenshi_Button2 captions */
MyGUI::Colour Lit() { return MyGUI::Colour(0.871f, 0.845f, 0.810f, 1.0f); }
/* the dim caption ui.cpp gives a greyed Kenshi_Button2 and a hint */
MyGUI::Colour Dim() { return MyGUI::Colour(0.50f, 0.48f, 0.46f, 1.0f); }
/* Kenshi_Button1Skin's "highlighted" text colour (data/gui/skins/kenshi_skins.xml) - the colour the menu's own buttons take under
   the mouse - so REPORT A BUG stands out in a colour the menu already uses (378 d) */
MyGUI::Colour Highlight() { return MyGUI::Colour(0.505882f, 0.4f, 0.207843f, 1.0f); }

/* ---- what the player pressed: written by the click and key handlers (any thread), taken by the tick ---- */
enum { kActNone = 0, kActOpenTitle = 1, kActOpenPause = 2, kActCancel = 3, kActSend = 4, kActOk = 5, kActBack = 6, kActRetry = 7 };
volatile LONG g_act = kActNone;
volatile LONG g_esc = 0;      /* an ESC meant for this feature's window or box */
volatile LONG g_uiUp = 0;     /* the window or a box is up: the title's ESC gate answers for it */
volatile LONG g_off = 0;      /* the feature took itself down after a fault */

void OnOpenTitle(MyGUI::Widget*) { ::InterlockedExchange(&g_act, kActOpenTitle); }
void OnOpenPause(MyGUI::Widget*) { ::InterlockedExchange(&g_act, kActOpenPause); }
void OnCancel(MyGUI::Widget*)    { ::InterlockedExchange(&g_act, kActCancel); }
void OnSend(MyGUI::Widget*)      { ::InterlockedExchange(&g_act, kActSend); }
void OnOk(MyGUI::Widget*)        { ::InterlockedExchange(&g_act, kActOk); }
void OnBack(MyGUI::Widget*)      { ::InterlockedExchange(&g_act, kActBack); }
void OnRetry(MyGUI::Widget*)     { ::InterlockedExchange(&g_act, kActRetry); }
void OnKey(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char) { if (key == MyGUI::KeyCode::Escape) ::InterlockedExchange(&g_esc, 1); }
void OnWinButton(MyGUI::Window*, const std::string& button) { if (button == "close") ::InterlockedExchange(&g_act, kActCancel); }

/* ---- the screen's state: MAIN THREAD ---- */
enum { kStClosed = 0, kStEdit = 1, kStBusy = 2, kStFailed = 3, kStSent = 4 };
int         g_st = kStClosed;
int         g_fromPause = 0;      /* opened from the pause menu: the menu is kept open under the window */
std::string g_caption;            /* the typed text as the box holds it (MyGUI's tags kept), kept after CANCEL (375) */
long        g_gen = 0;            /* the report whose worker the screen waits on; 0 = none */
long        g_genNext = 0;
int         g_failKind = 0, g_failCode = 0;
int         g_winWired = 0, g_sentWired = 0, g_failWired = 0, g_titleWired = 0, g_pauseWired = 0;
int         g_buildFails = 0;
DWORD       g_titleLookMs = 0;
int         g_titleW = 0, g_titleH = 0;   /* the title art's size the title button and note were placed for */
int         g_titleLogged = 0, g_pauseLogged = 0;
size_t      g_shownLen = (size_t)-1;
std::string g_shownStatus;
int         g_shownBusy = -1, g_shownSendOk = -1;

/* ---- state shared with the workers: g_cs ---- */
CRITICAL_SECTION g_cs;
int g_csReady = 0;   /* set on the main thread before any worker exists */
void CsInit() { if (!g_csReady) { ::InitializeCriticalSection(&g_cs); g_csReady = 1; coopstore::Crc32("a", 1); /* its table is built here, before any worker reads it */ } }
struct Lock { Lock() { ::EnterCriticalSection(&g_cs); } ~Lock() { ::LeaveCriticalSection(&g_cs); } };

struct NearbyLog { unsigned int slot; std::string name; coopbug::LogPack pack; };
enum { kPhPreparing = 0, kPhSending = 1, kPhDone = 2 };
struct Shared
{
    long gen;                         /* the report whose results count; a worker of another generation writes nothing */
    int phase, pct, ok, failKind, failCode;
    int nearbyFinal;                  /* the main thread has handed over every nearby answer it will get */
    std::vector<NearbyLog> nearby;
    std::string nearbyNote;
    std::vector<unsigned char> body;  /* the last packed upload, kept in memory for TRY AGAIN */
    std::string boundary;
    Shared() : gen(0), phase(0), pct(0), ok(0), failKind(0), failCode(0), nearbyFinal(0) {}
};
Shared g_sh;
/* a report's stop flag, by generation: CANCEL raises it, the worker reads it between segments and between upload chunks */
volatile long g_stop[64];
volatile long* StopOf(long gen) { return &g_stop[(unsigned long)gen % 64u]; }

/* ---- the nearby players' answers to this game's ask: MAIN THREAD ---- */
struct Collect
{
    int active;
    long gen;
    unsigned int askId;
    DWORD askAt;
    int sx, sy;
    std::map<unsigned int, coopbug::PartsJoin> joins;   /* by the answering game's slot */
    Collect() : active(0), gen(0), askId(0), askAt(0), sx(-1), sy(-1) {}
};
Collect g_col;
unsigned int g_askNext = 0;

/* ---- this game's answer to another game's ask ---- */
struct Answer
{
    int state;              /* 0 idle, 1 the worker is compressing, 2 sending parts (MAIN THREAD) */
    unsigned int askId, slot, next, parts;
    DWORD askAtMs;          /* when the ask arrived (coopbug::AnswerExpired) */
    DWORD lastSentMs;
    std::vector<char> bundle;
    int ready;              /* g_cs: the worker has filled `bundle` */
    Answer() : state(0), askId(0), slot(0), next(0), parts(0), askAtMs(0), lastSentMs(0), ready(0) {}
};
Answer g_ans;
std::map<unsigned int, DWORD> g_ansBySlot;   /* MAIN THREAD: when each asking slot's last answer began (coopbug::AskerMayBeAnswered) */
long long g_ansRefused = 0;                  /* asks refused since this game started */
long long g_ansRefusedUnlogged = 0;          /* ... of which no log line has spoken yet */
int g_ansRefusalLogged = 0;
DWORD g_ansRefusalLogAt = 0;

std::string Num(unsigned long long v) { char b[32]; _snprintf(b, 31, "%llu", v); b[31] = 0; return b; }
std::string NumI(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return b; }
int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
long long FileTimeSec(const FILETIME& ft) { return (long long)((((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull); }

/* =========================================================================================================================
   FILES (worker threads)
   ========================================================================================================================= */
/* The newest `window` bytes of a file as it is now (a log being written is read up to its size at the moment it is opened),
   starting after a line end when the start was cut. */
bool ReadTail(const std::wstring& path, unsigned long long window, std::string* out, unsigned long long* total,
              unsigned long long* dropped, long long* lastWriteSec)
{
    out->clear(); *total = 0; *dropped = 0; if (lastWriteSec) *lastWriteSec = 0;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; sz.QuadPart = 0;
    FILETIME wt; std::memset(&wt, 0, sizeof(wt));
    if (!::GetFileSizeEx(h, &sz)) { ::CloseHandle(h); return false; }
    if (lastWriteSec && ::GetFileTime(h, NULL, NULL, &wt)) *lastWriteSec = FileTimeSec(wt);
    const unsigned long long size = (unsigned long long)sz.QuadPart;
    const unsigned long long start = size > window ? size - window : 0;
    LARGE_INTEGER at; at.QuadPart = (LONGLONG)start;
    if (start > 0 && !::SetFilePointerEx(h, at, NULL, FILE_BEGIN)) { ::CloseHandle(h); return false; }
    out->resize((size_t)(size - start));
    size_t got = 0;
    while (got < out->size())
    {
        DWORD want = (DWORD)((out->size() - got) > (4u << 20) ? (4u << 20) : (out->size() - got)), n = 0;
        if (!::ReadFile(h, &(*out)[got], want, &n, NULL) || n == 0) break;
        got += n;
    }
    ::CloseHandle(h);
    out->resize(got);
    *total = size;
    *dropped = start;
    if (start > 0)
    {
        const size_t off = coopbug::WindowStart(out->data(), out->size());
        out->erase(0, off);
        *dropped += off;
    }
    return true;
}
std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
/* The game's folder (where kenshi_x64.exe is, and where Kenshi writes its crash file), with a trailing backslash. */
std::wstring GameFolder()
{
    wchar_t p[2048];
    const DWORD n = ::GetModuleFileNameW(NULL, p, 2048);
    if (n == 0 || n >= 2048) return std::wstring();
    std::wstring s(p, n);
    const std::wstring::size_type k = s.find_last_of(L"\\/");
    return k == std::wstring::npos ? std::wstring() : s.substr(0, k + 1);
}
/* Kenshi's newest crash file in the game folder (a .zip before a .dmp), if the previous launch ended in it. */
bool FindCrash(const std::wstring& folder, long long prevLogEnd, std::string* name, std::vector<unsigned char>* bytes)
{
    if (folder.empty()) return false;
    FILETIME ct, et, kt, ut;
    if (!::GetProcessTimes(::GetCurrentProcess(), &ct, &et, &kt, &ut)) return false;
    const long long launch = FileTimeSec(ct);
    WIN32_FIND_DATAW fd;
    HANDLE f = ::FindFirstFileW((folder + L"crashDump*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::wstring best; int bestKind = 0; long long bestAt = 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const int kind = coopbug::CrashFileKind(Narrow(fd.cFileName));
        const long long t = FileTimeSec(fd.ftLastWriteTime);
        const unsigned long long size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        /* Kenshi's own file is about 0.3-0.6 MB; anything far bigger is not it (367 a: no Windows full dumps) */
        if (kind == 0 || size > (4ull << 20) || !coopbug::CrashFromLastLaunch(t, prevLogEnd, launch)) continue;
        if (kind > bestKind || (kind == bestKind && t > bestAt)) { best = fd.cFileName; bestKind = kind; bestAt = t; }
    } while (::FindNextFileW(f, &fd));
    ::FindClose(f);
    if (bestKind == 0) return false;
    std::string raw; unsigned long long total = 0, dropped = 0;
    if (!ReadTail(folder + best, 4ull << 20, &raw, &total, &dropped, 0) || dropped != 0) return false;
    *name = Narrow(best);
    bytes->assign(raw.begin(), raw.end());
    return true;
}

/* =========================================================================================================================
   THE UPLOAD (worker thread)
   ========================================================================================================================= */
void SetPhase(long gen, int phase, int pct) { Lock l; if (g_sh.gen == gen) { g_sh.phase = phase; g_sh.pct = pct; } }
void Finish(long gen, int ok, int kind, int code)
{
    Lock l;
    if (g_sh.gen != gen) return;
    g_sh.phase = kPhDone; g_sh.ok = ok; g_sh.failKind = kind; g_sh.failCode = code;
}
/* 1 sent; 0 failed (*kind, *code as the box says them); -1 stopped by CANCEL. */
int Post(long gen, const std::string& boundary, const std::vector<unsigned char>& body, int* kind, int* code)
{
    *kind = coopbug::kFailOther; *code = 0;
    const std::string url = coopbug::kRelayUrl;
    if (url.empty()) { *code = coopbug::kErrNoRelay; return 0; }
    /* HTTPS only: the report carries the players' logs and their words */
    const std::wstring wurl = Widen(url);
    URL_COMPONENTS uc; std::memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1; uc.dwHostNameLength = (DWORD)-1; uc.dwUrlPathLength = (DWORD)-1; uc.dwExtraInfoLength = (DWORD)-1;
    if (!::WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc) || uc.lpszHostName == 0 || uc.dwHostNameLength == 0
        || uc.nScheme != INTERNET_SCHEME_HTTPS)
    { *code = coopbug::kErrBadRelay; return 0; }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path = uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : std::wstring();
    if (uc.lpszExtraInfo) path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    int winErr = 0, status = 0, stopped = 0;
    HINTERNET s = ::WinHttpOpen(swnames::kBugReportAgentW, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = 0, r = 0;
    if (s == 0) winErr = (int)::GetLastError();
    if (s != 0)
    {
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        ::WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));   /* refused on a Windows without TLS 1.2: its default stays */
        ::WinHttpSetTimeouts(s, 15000, 15000, 30000, 30000);
        c = ::WinHttpConnect(s, host.c_str(), uc.nPort, 0);
        if (c == 0) winErr = (int)::GetLastError();
    }
    if (c != 0)
    {
        r = ::WinHttpOpenRequest(c, L"POST", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE);
        if (r == 0) winErr = (int)::GetLastError();
    }
    if (r != 0)
    {
        const std::wstring head = L"Content-Type: multipart/form-data; boundary=" + Widen(boundary);
        if (!::WinHttpSendRequest(r, head.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, (DWORD)body.size(), 0)) winErr = (int)::GetLastError();
        size_t sent = 0;
        while (winErr == 0 && sent < body.size())
        {
            if (*StopOf(gen) != 0) { stopped = 1; break; }
            const DWORD chunk = (DWORD)((body.size() - sent) > 65536u ? 65536u : (body.size() - sent));
            DWORD wrote = 0;
            if (!::WinHttpWriteData(r, &body[sent], chunk, &wrote)) { winErr = (int)::GetLastError(); break; }
            sent += wrote;
            SetPhase(gen, kPhSending, (int)((unsigned long long)sent * 100ull / (unsigned long long)body.size()));
        }
        if (winErr == 0 && !stopped && !::WinHttpReceiveResponse(r, NULL)) winErr = (int)::GetLastError();
        if (winErr == 0 && !stopped)
        {
            DWORD st = 0, sz = sizeof(st);
            if (::WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz, WINHTTP_NO_HEADER_INDEX))
                status = (int)st;
            else winErr = (int)::GetLastError();
            char buf[4096];
            DWORD avail = 0, total = 0;
            while (winErr == 0 && total < 65536 && ::WinHttpQueryDataAvailable(r, &avail) && avail != 0)
            {
                DWORD n = 0;
                if (!::WinHttpReadData(r, buf, avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail, &n) || n == 0) break;
                total += n;
            }
        }
    }
    if (r != 0) ::WinHttpCloseHandle(r);
    if (c != 0) ::WinHttpCloseHandle(c);
    if (s != 0) ::WinHttpCloseHandle(s);
    if (stopped) return -1;
    if (coopbug::SendSucceeded(winErr, status)) return 1;
    *kind = coopbug::FailKindOf(winErr, status);
    *code = coopbug::FailCodeOf(winErr, status);
    return 0;
}

/* =========================================================================================================================
   THE REPORT (worker thread)
   ========================================================================================================================= */
struct Job
{
    long gen;
    int sendOnly;           /* TRY AGAIN: post the upload kept from the last attempt */
    std::string desc;       /* the player's words, as typed (MyGUI's tags removed) */
    std::string info;       /* one line: game, fingerprint, protocol, where */
    std::string where;
    std::wstring log0, log1, gameFolder;
};
struct ZipPart
{
    std::string name;
    int order;
    const coopbug::LogPack* pack;           /* a log, or */
    const std::vector<unsigned char>* raw;  /* a file stored as it is (the crash file) */
    std::string what;                       /* report.txt's words for it */
};
std::string UtcNow()
{
    SYSTEMTIME t; ::GetSystemTime(&t);
    char b[64]; _snprintf(b, 63, "%04d-%02d-%02d %02d:%02d:%02d UTC", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); b[63] = 0;
    return b;
}
std::string LogLine(const std::string& name, const std::string& what, const coopbug::LogPack& p, unsigned long long cut)
{
    std::string s = name + ": " + what + ", " + Num(p.rawTotal) + " bytes, " + Num(p.ipsScrubbed) + " IP addresses removed";
    const unsigned long long dropped = p.rawDropped + cut;
    if (dropped != 0) s += "; its oldest " + Num(dropped) + " bytes are not included (kept under " + Num(coopbug::kPackLimit) + " bytes)";
    return s + "\r\n";
}
void RunJob(Job& j)
{
    std::vector<unsigned char> body;
    std::string boundary;
    if (j.sendOnly)
    {
        Lock l;
        body = g_sh.body;
        boundary = g_sh.boundary;
    }
    else
    {
        volatile long* stop = StopOf(j.gen);
        coopbug::LogPack cur, prev;
        std::string raw;
        long long prevEnd = 0;
        const bool haveCur = ReadTail(j.log0, coopbug::kRawLogWindow, &raw, &cur.rawTotal, &cur.rawDropped, 0);
        if (haveCur && !coopbug::PackLog(raw, &cur, stop)) return;
        const bool havePrev = ReadTail(j.log1, coopbug::kRawLogWindow, &raw, &prev.rawTotal, &prev.rawDropped, &prevEnd);
        if (havePrev && !coopbug::PackLog(raw, &prev, stop)) return;
        raw.clear();
        std::string crashName; std::vector<unsigned char> crash;
        const bool haveCrash = FindCrash(j.gameFolder, havePrev ? prevEnd : 0, &crashName, &crash);
        /* the nearby players' logs: the main thread hands them over when its wait is over (coopbug::NearbyWaitOver) */
        std::vector<NearbyLog> nearby;
        std::string nearbyNote;
        for (;;)
        {
            if (*stop != 0) return;
            {
                Lock l;
                if (g_sh.gen != j.gen) return;
                if (g_sh.nearbyFinal) { nearby.swap(g_sh.nearby); nearbyNote = g_sh.nearbyNote; break; }
            }
            ::Sleep(50);
        }
        /* the budget (372) */
        std::vector<ZipPart> parts;
        if (haveCur) { ZipPart p = { swnames::kLog, coopbug::kOrderThisLog, &cur, 0, "this launch's mod log" }; parts.push_back(p); }
        if (havePrev) { ZipPart p = { swnames::kLogPrev, coopbug::kOrderPrevLog, &prev, 0, "the previous launch's mod log" }; parts.push_back(p); }
        if (haveCrash) { ZipPart p = { crashName, coopbug::kOrderCrash, 0, &crash, "Kenshi's crash file from the previous launch" }; parts.push_back(p); }
        for (size_t k = 0; k < nearby.size(); ++k)
        {
            ZipPart p = { "nearby/slot" + Num(nearby[k].slot) + "-" + coopbug::SafeName(nearby[k].name) + "/" + swnames::kLog,
                          coopbug::kOrderNearby, &nearby[k].pack, 0, nearby[k].name + "'s current mod log (a nearby player, slot " + Num(nearby[k].slot) + ")" };
            parts.push_back(p);
        }
        std::vector<coopbug::BudgetPart> bp(parts.size() + 1);
        for (size_t k = 0; k < parts.size(); ++k)
        {
            bp[k].order = parts[k].order;
            bp[k].fixed = coopbug::ZipEntryOverhead(parts[k].name);
            if (parts[k].pack != 0)
            {
                bp[k].fixed += sizeof(coopbug::kFinalBlock);
                for (size_t s = 0; s < parts[k].pack->segs.size(); ++s) bp[k].segBytes.push_back(parts[k].pack->segs[s].bytes.size());
            }
            else { bp[k].fixed += parts[k].raw->size(); bp[k].droppable = true; }
        }
        /* report.txt and the zip's end record, never cut: a generous reserve, as report.txt is written after the cuts */
        bp[parts.size()].fixed = coopbug::ZipEntryOverhead("report.txt") + 16384u + j.desc.size() + coopbug::kZipEndBytes;
        const coopbug::BudgetCut cut = coopbug::PlanBudget(bp, coopbug::kPackLimit);
        /* report.txt */
        std::string m;
        m += std::string(swnames::kBugReportTitle) + "\r\n";
        m += "Made: " + UtcNow() + "\r\n";
        m += j.info + "\r\n";
        m += "Where: " + j.where + "\r\n\r\n";
        m += "What happened (the player's words):\r\n" + j.desc + "\r\n\r\n";
        m += "Files in this report:\r\n";
        std::vector<coopbug::ZipEntry> es;
        es.push_back(coopbug::ZipEntry());   /* report.txt goes first; it is filled in below */
        for (size_t k = 0; k < parts.size(); ++k)
        {
            if (cut.dropped[k] != 0) { m += parts[k].name + ": " + parts[k].what + " - left out to keep the report under " + Num(coopbug::kPackLimit) + " bytes\r\n"; continue; }
            coopbug::ZipEntry e;
            e.name = parts[k].name;
            if (parts[k].pack != 0)
            {
                const size_t first = cut.dropSegs[k];
                unsigned long long rawLen = 0, cutRaw = 0;
                for (size_t s = 0; s < first; ++s) cutRaw += parts[k].pack->segs[s].rawLen;
                coopbug::JoinPack(*parts[k].pack, first, &e.data, &rawLen, &e.crc);
                e.method = 8;
                e.rawLen = (unsigned int)rawLen;
                m += LogLine(parts[k].name, parts[k].what, *parts[k].pack, cutRaw);
            }
            else
            {
                e.method = 0;
                e.data = *parts[k].raw;
                e.rawLen = (unsigned int)e.data.size();
                e.crc = coopstore::Crc32(e.data.empty() ? 0 : &e.data[0], e.data.size());
                m += parts[k].name + ": " + parts[k].what + ", " + Num(e.rawLen) + " bytes\r\n";
            }
            es.push_back(e);
        }
        if (!haveCur) m += std::string(swnames::kLog) + ": this launch's mod log could not be read\r\n";
        if (!havePrev) m += std::string(swnames::kLogPrev) + ": there is no previous launch's mod log\r\n";
        if (!haveCrash) m += "No Kenshi crash file from the previous launch.\r\n";
        m += "\r\nNearby players: " + nearbyNote + "\r\n";
        es[0] = coopbug::StoredEntry("report.txt", m);
        SYSTEMTIME lt; ::GetLocalTime(&lt);
        const std::vector<unsigned char> zip = coopbug::BuildZip(es, coopbug::DosTime(lt.wHour, lt.wMinute, lt.wSecond), coopbug::DosDate(lt.wYear, lt.wMonth, lt.wDay));
        char seed[64]; _snprintf(seed, 63, "----SharedWastelandsReport%08lX%08lX", (unsigned long)::GetTickCount(), (unsigned long)j.gen); seed[63] = 0;
        boundary = coopbug::PickBoundary(seed, j.desc, j.info, zip);
        body = coopbug::BuildMultipart(boundary, j.desc, j.info, zip);
        DebugLog("[BUG] report packed: zip " + Num(zip.size()) + " bytes (limit " + Num(coopbug::kPackLimit) + "), " + Num(es.size()) + " files, "
                 + (cut.fits ? "within the limit" : "STILL OVER the limit after every cut") + "; nearby: " + nearbyNote);
        Lock l;
        if (g_sh.gen != j.gen) return;
        g_sh.body = body;   /* kept in memory only (373), for TRY AGAIN */
        g_sh.boundary = boundary;
    }
    if (body.empty()) { Finish(j.gen, 0, coopbug::kFailOther, 0); return; }
    SetPhase(j.gen, kPhSending, 0);
    int kind = 0, code = 0;
    const int r = Post(j.gen, boundary, body, &kind, &code);
    if (r < 0) return;
    Finish(j.gen, r == 1 ? 1 : 0, kind, code);
    DebugLog(r == 1 ? std::string("[BUG] report sent (") + Num(body.size()) + " bytes)"
                    : "[BUG] report NOT sent: " + coopbug::FailText(kind, code) + " (kind " + NumI(kind) + ", code " + NumI(code) + ")");
}
unsigned __stdcall JobMain(void* arg)
{
    Job* j = (Job*)arg;
    try { RunJob(*j); }
    catch (...) { Finish(j->gen, 0, coopbug::kFailOther, 0); DebugLog("[BUG] the report's worker stopped on a C++ exception"); }
    delete j;
    return 0;
}

/* =========================================================================================================================
   THIS GAME'S ANSWER TO ANOTHER GAME'S ASK (worker thread, then the main thread sends it)
   ========================================================================================================================= */
struct AnswerJob { std::string name; std::wstring log0; unsigned int cap; };
unsigned __stdcall AnswerMain(void* arg)
{
    AnswerJob* a = (AnswerJob*)arg;
    std::vector<char> bundle;
    try
    {
        coopbug::LogPack p;
        std::string raw;
        if (ReadTail(a->log0, coopbug::kRawLogWindow, &raw, &p.rawTotal, &p.rawDropped, 0))
            coopbug::PackLogNewest(raw, &p, a->cap > 4096u ? a->cap - 4096u : a->cap);   /* room for the bundle's own header */
        bundle = coopbug::BundleEncode(a->name, p);
    }
    catch (...) { bundle.clear(); }
    {
        Lock l;
        g_ans.bundle.swap(bundle);
        g_ans.ready = 1;
    }
    delete a;
    return 0;
}
/* An ask this game will not answer: counted, and logged at most once per coopbug::kRefusalLogGapMs with the count since the
   previous line, so a stream of asks cannot flood the log. */
void AnswerRefused(unsigned int slot, const char* why)
{
    ++g_ansRefused; ++g_ansRefusedUnlogged;
    const DWORD now = ::GetTickCount();
    if (!coopbug::RefusalLogDue(g_ansRefusalLogged != 0, (unsigned int)(now - g_ansRefusalLogAt))) return;
    DebugLog("[BUG] a nearby log ask from slot " + Num(slot) + " not answered: " + why + " (" + NumI(g_ansRefusedUnlogged)
             + " refused since the last such line, " + NumI(g_ansRefused) + " in all; this line is written at most every "
             + Num(coopbug::kRefusalLogGapMs / 1000u) + " s)");
    g_ansRefusalLogged = 1; g_ansRefusalLogAt = now; g_ansRefusedUnlogged = 0;
}
void AnswerStart(unsigned int askId, unsigned int slot, unsigned int maxBytes)
{
    if (g_ans.state != 0) { AnswerRefused(slot, "an answer is already being made"); return; }
    const DWORD now = ::GetTickCount();
    const std::map<unsigned int, DWORD>::const_iterator last = g_ansBySlot.find(slot);
    if (!coopbug::AskerMayBeAnswered(last != g_ansBySlot.end(), last != g_ansBySlot.end() ? (unsigned int)(now - last->second) : 0u))
    { AnswerRefused(slot, "that slot was answered less than a minute ago"); return; }
    AnswerJob* a = new AnswerJob;
    a->name = ConfigFilePlayerName();
    a->log0 = LogFilePath(0);
    a->cap = maxBytes < coopbug::kNearbyLogMax ? maxBytes : coopbug::kNearbyLogMax;
    { Lock l; g_ans.ready = 0; g_ans.bundle.clear(); }
    g_ans.askId = askId; g_ans.slot = slot; g_ans.next = 0; g_ans.parts = 0; g_ans.askAtMs = now;
    g_ans.state = 1;
    const uintptr_t t = _beginthreadex(NULL, 0, &AnswerMain, a, 0, NULL);
    if (t == 0) { delete a; g_ans.state = 0; ErrorLog("[BUG] a nearby log ask from slot " + Num(slot) + " not answered: no worker thread"); return; }
    ::CloseHandle((HANDLE)t);
    g_ansBySlot[slot] = now;
    DebugLog("[BUG] slot " + Num(slot) + " asked for this game's current log for a bug report (ask " + Num(askId) + ") - it is being sent; the player is not told");
}
/* MAIN THREAD: the next part only when coopbug::AnswerPartDue says the world-server link is nearly idle, so the answer never
   crowds the play that uses that link; an answer the asker has stopped waiting for ends (coopbug::AnswerExpired). */
void AnswerTick()
{
    if (g_ans.state == 1)
    {
        Lock l;
        if (!g_ans.ready) return;
        g_ans.state = 2;
        g_ans.parts = coopbug::PartCountFor((unsigned int)g_ans.bundle.size());
        g_ans.lastSentMs = ::GetTickCount() - coopbug::kPartGapMs;
    }
    if (g_ans.state != 2) return;
    const DWORD now = ::GetTickCount();
    if (coopbug::AnswerExpired((unsigned int)(now - g_ans.askAtMs)))
    {
        DebugLog("[BUG] the answer to slot " + Num(g_ans.slot) + " stopped at part " + Num(g_ans.next) + " of " + Num(g_ans.parts)
                 + ": the asking game stopped waiting " + Num(coopbug::kNearbyTotalMs / 1000u) + " s after it asked");
        g_ans.state = 0; g_ans.bundle.clear();
        return;
    }
    long long queuedPackets = 0, queuedBytes = 0;
    const bool queueKnown = StoreLiveSendQueued(&queuedPackets, &queuedBytes);
    if (!coopbug::AnswerPartDue((unsigned int)(now - g_ans.lastSentMs), queueKnown, queuedBytes)) return;
    g_ans.lastSentMs = now;
    if (g_ans.next < g_ans.parts)
    {
        std::vector<char> part;
        coopbug::LogPartEncode(&part, g_ans.askId, g_ans.bundle, g_ans.next);
        if (!StoreSendLive(cooplive::kRouteSlot, g_ans.slot, cooplive::kInnerLogPart, part, true))
        {
            DebugLog("[BUG] the answer to slot " + Num(g_ans.slot) + " stopped at part " + Num(g_ans.next) + " of " + Num(g_ans.parts) + ": the world server road is down");
            g_ans.state = 0; g_ans.bundle.clear();
            return;
        }
        ++g_ans.next;
    }
    if (g_ans.next >= g_ans.parts)
    {
        DebugLog("[BUG] the answer to slot " + Num(g_ans.slot) + " is sent: " + Num(g_ans.bundle.size()) + " bytes in " + Num(g_ans.parts) + " parts");
        g_ans.state = 0; g_ans.bundle.clear();
    }
}

/* =========================================================================================================================
   THIS GAME'S ASK (MAIN THREAD)
   ========================================================================================================================= */
/* 1 = asked: the nearby players' answers are awaited; 0 = nobody to ask (the main thread hands over an empty list at once). */
int AskNearby(long gen, std::string* note)
{
    g_col = Collect();
    if (!GameplayRunning()) { *note = "none asked (the report was made at the title screen)"; return 0; }
    if (!StoreLiveReady() || StoreRosterOtherInWorldTS() != 1) { *note = "none asked (no other player was in this world)"; return 0; }
    const Sector s = MyPlayerSector();
    const int key = cooplive::AreaKey(s.x, s.y);
    if (key < 0) { *note = "none asked (this game has no player sector)"; return 0; }
    if (++g_askNext == 0) ++g_askNext;
    std::vector<char> in;
    coopbug::LogAskEncode(&in, g_askNext, coopbug::kNearbyLogMax);
    if (!StoreSendLive(cooplive::kRouteArea, cooplive::AreaTarget(key, -1), cooplive::kInnerLogAsk, in, true))
    { *note = "none asked (the world server road is down)"; return 0; }
    g_col.active = 1; g_col.gen = gen; g_col.askId = g_askNext; g_col.askAt = ::GetTickCount(); g_col.sx = s.x; g_col.sy = s.y;
    DebugLog("[BUG] nearby players asked for their current logs (ask " + Num(g_askNext) + ", sector " + NumI(s.x) + "," + NumI(s.y) + ")");
    return 1;
}
void CollectTick()
{
    if (!g_col.active) return;
    if (g_col.gen != g_gen) { g_col = Collect(); return; }   /* CANCEL ended the report it was for */
    int started = 0, finished = 0;
    for (std::map<unsigned int, coopbug::PartsJoin>::const_iterator it = g_col.joins.begin(); it != g_col.joins.end(); ++it)
    { ++started; if (it->second.Complete()) ++finished; }
    if (!coopbug::NearbyWaitOver((unsigned int)(::GetTickCount() - g_col.askAt), started, finished)) return;
    std::vector<NearbyLog> logs;
    std::string note = "asked (sector " + NumI(g_col.sx) + "," + NumI(g_col.sy) + ")";
    std::string answered, partial;
    for (std::map<unsigned int, coopbug::PartsJoin>::const_iterator it = g_col.joins.begin(); it != g_col.joins.end(); ++it)
    {
        NearbyLog n;
        n.slot = it->first;
        if (it->second.Complete() && coopbug::BundleDecode(it->second.data, &n.name, &n.pack))
        { logs.push_back(n); answered += (answered.empty() ? "" : ", ") + std::string("slot ") + Num(n.slot) + " (" + n.name + ")"; }
        else partial += (partial.empty() ? "" : ", ") + std::string("slot ") + Num(it->first);
    }
    note += answered.empty() ? "; none answered within " + Num(coopbug::kNearbyFirstMs / 1000u) + " s" : "; answered: " + answered;
    if (!partial.empty()) note += "; left out (its log did not all arrive or could not be read): " + partial;
    DebugLog("[BUG] nearby logs: " + note);
    {
        Lock l;
        if (g_sh.gen == g_col.gen) { g_sh.nearby.swap(logs); g_sh.nearbyNote = note; g_sh.nearbyFinal = 1; }
    }
    g_col = Collect();
}

/* =========================================================================================================================
   STARTING AND STOPPING A REPORT (MAIN THREAD)
   ========================================================================================================================= */
std::string WhereNow()
{
    if (!GameplayRunning()) return "the title screen";
    return StoreLiveReady() ? "a multiplayer world" : "a single-player world";
}
void StartJob(const std::string& desc, int sendOnly)
{
    const long gen = ++g_genNext;
    *StopOf(gen) = 0;
    g_gen = gen;
    std::string note;
    const int asked = sendOnly ? 0 : AskNearby(gen, &note);
    {
        Lock l;
        g_sh.gen = gen; g_sh.phase = sendOnly ? kPhSending : kPhPreparing; g_sh.pct = 0; g_sh.ok = 0; g_sh.failKind = 0; g_sh.failCode = 0;
        g_sh.nearby.clear(); g_sh.nearbyNote = note; g_sh.nearbyFinal = asked ? 0 : 1;
        if (!sendOnly) { g_sh.body.clear(); g_sh.boundary.clear(); }
    }
    Job* j = new Job;
    j->gen = gen; j->sendOnly = sendOnly; j->desc = desc;
    const std::string table = AddrTableName(), fp = AddrExeFingerprint();
    j->where = WhereNow();
    j->info = "Game: " + (table.empty() ? std::string("unknown") : table) + " (fingerprint " + (fp.empty() ? std::string("unknown") : fp)
              + "); game-to-game protocol " + Num(net::SessionProtocolVersion()) + "; made in " + j->where;
    j->log0 = LogFilePath(0); j->log1 = LogFilePath(1); j->gameFolder = GameFolder();
    const uintptr_t t = _beginthreadex(NULL, 0, &JobMain, j, 0, NULL);
    if (t == 0)
    {
        const int err = (int)::GetLastError();
        delete j;
        Finish(gen, 0, coopbug::kFailOther, err);
        ErrorLog("[BUG] the report's worker thread could not start (Windows error " + NumI(err) + ")");
        return;
    }
    ::CloseHandle((HANDLE)t);
    DebugLog(std::string("[BUG] report ") + NumI(gen) + (sendOnly ? " sent again (TRY AGAIN)" : " started") + " from " + WhereNow());
}
void CancelJob(const char* why)
{
    if (g_gen == 0) return;
    *StopOf(g_gen) = 1;
    { Lock l; g_sh.gen = 0; }
    DebugLog(std::string("[BUG] report ") + NumI(g_gen) + " cancelled (" + why + ")");
    g_gen = 0;
    g_col = Collect();
}

/* =========================================================================================================================
   WIDGETS (MAIN THREAD, inside the SEH frame below)
   ========================================================================================================================= */
MyGUI::Widget* Find(MyGUI::Gui* gui, const char* name) { return gui->findWidgetT(std::string(name), false); }
MyGUI::Widget* Mk(MyGUI::Widget* parent, const char* type, const char* skin, int l, int t, int w, int h, const char* name)
{
    if (parent == 0) return 0;
    return parent->createWidgetT(std::string(type), std::string(skin), MyGUI::IntCoord(l, t, w > 1 ? w : 1, h > 1 ? h : 1),
                                 MyGUI::Align::Default, std::string(name));
}
/* A root widget on the "Info" layer (Kenshi's message box's, above the menus), or "Popup" when Info is unknown. */
MyGUI::Widget* MkRoot(MyGUI::Gui* gui, const char* skin, const MyGUI::IntCoord& c, const char* name)
{
    static const char* const kLayers[2] = { "Info", "Popup" };
    for (int k = 0; k < 2; ++k)
    {
        MyGUI::Widget* w = gui->createWidgetT(std::string("Window"), std::string(skin), c, MyGUI::Align::Default, std::string(kLayers[k]), std::string(name));
        if (w == 0) return 0;
        if (w->getLayer() != 0) return w;
        gui->destroyWidget(w);
    }
    return 0;
}
void ViewSize(int* vw, int* vh)
{
    *vw = 1280; *vh = 720;
    MyGUI::RenderManager* rm = MyGUI::RenderManager::getInstancePtr();
    if (rm != 0) { const MyGUI::IntSize& s = rm->getViewSize(); if (s.width > 0 && s.height > 0) { *vw = s.width; *vh = s.height; } }
}
template <class T> T* As(MyGUI::Widget* w) { return w != 0 ? w->castType<T>(false) : 0; }
void DropRoot(MyGUI::Gui* gui, const char* name)
{
    MyGUI::Widget* w = Find(gui, name);
    if (w == 0) return;
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->removeWidgetModal(w); im->resetKeyFocusWidget(); }
    gui->destroyWidget(w);
}
void StyleText(MyGUI::Widget* w, const char* caption, MyGUI::Align align, const MyGUI::Colour& colour)
{
    MyGUI::TextBox* t = As<MyGUI::TextBox>(w);
    if (t == 0) return;
    t->setCaption(MyGUI::UString(caption));
    t->setTextAlign(align);
    t->setTextColour(colour);
}

/* THE REPORT WINDOW (mock-up section 3). Built at a row height read from the window size (Kenshi's own text scales with it);
   the window is exactly as tall as its rows. false = not built (nothing left on screen). */
bool BuildWindow(MyGUI::Gui* gui)
{
    int vw = 0, vh = 0;
    ViewSize(&vw, &vh);
    if (vw < 560 || vh < 400) return false;
    const int rowH = Clamp(vh / 27, 26, 44);
    const int W = Clamp(vw * 34 / 100, 520, vw - 20);
    MyGUI::Widget* raw = MkRoot(gui, "Kenshi_WindowCX", MyGUI::IntCoord((vw - W) / 2, vh / 4, W, 400), kWin);
    MyGUI::Window* win = As<MyGUI::Window>(raw);
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) { if (raw != 0) gui->destroyWidget(raw); return false; }
    win->setMovable(false);
    win->setCaption(MyGUI::UString(coopbug::kButtonCaption));
    win->eventWindowButtonPressed += MyGUI::newDelegate(OnWinButton);
    win->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    win->setNeedKeyFocus(true);
    /* the rows: the two about lines, WHAT HAPPENED? with the counter, the box, then CANCEL - the status line - SEND */
    const int frameH = raw->getHeight() - cl->getHeight();
    const int pad = Clamp(W * 3 / 100, 10, 24);
    const int innerW = cl->getWidth() - 2 * pad;
    int y = pad;
    const int aboutLineH = rowH * 3 / 4;
    const int about1Y = y;  y += aboutLineH;
    const int about2Y = y;  y += aboutLineH + rowH / 4;
    const int labelY = y;   y += rowH;   /* WHAT HAPPENED? on the left, the character counter on the right of the same row */
    const int editY = y;    const int editH = rowH * 3;   y += editH + rowH / 4;
    const int countY = labelY;   const int countH = rowH;
    const int btnY = y;     const int btnH = rowH * 6 / 5;   y += btnH + pad;
    const int H = y + frameH;
    raw->setCoord(MyGUI::IntCoord((vw - W) / 2, (vh - H) / 2, W, H));
    const int btnW = Clamp(innerW / 4, 120, innerW / 2 - 8);
    const int statusX = pad + btnW + pad / 2;   /* between CANCEL and SEND, as tall as they are */
    const int statusW = innerW - 2 * (btnW + pad / 2);
    MyGUI::Widget* about1 = Mk(cl, "TextBox", "Kenshi_TextboxStandardText", pad, about1Y, innerW, aboutLineH, kAbout1);
    MyGUI::Widget* about2 = Mk(cl, "TextBox", "Kenshi_TextboxStandardText", pad, about2Y, innerW, aboutLineH, kAbout2);
    MyGUI::Widget* label  = Mk(cl, "TextBox", "Kenshi_TextboxPaintedText", pad, labelY, innerW, rowH, kLabel);
    MyGUI::Widget* text   = Mk(cl, "EditBox", "Kenshi_EditBox", pad, editY, innerW, editH, kText);
    MyGUI::Widget* hint   = Mk(cl, "TextBox", "Kenshi_TextboxStandardText", pad + 8, editY + 4, innerW - 16, rowH, kTextHint);
    MyGUI::Widget* count  = Mk(cl, "TextBox", "Kenshi_TextboxStandardText", pad, countY, innerW, countH, kCounter);
    MyGUI::Widget* status = Mk(cl, "TextBox", "Kenshi_TextboxStandardText", statusX, btnY, statusW, btnH, kStatus);
    MyGUI::Widget* cancel = Mk(cl, "Button", "Kenshi_Button1", pad, btnY, btnW, btnH, kCancelBtn);
    MyGUI::Widget* send   = Mk(cl, "Button", "Kenshi_Button1", pad + innerW - btnW, btnY, btnW, btnH, kSendBtn);
    MyGUI::EditBox* e = As<MyGUI::EditBox>(text);
    MyGUI::Button* cb = As<MyGUI::Button>(cancel);
    MyGUI::Button* sb = As<MyGUI::Button>(send);
    if (As<MyGUI::TextBox>(about1) == 0 || As<MyGUI::TextBox>(about2) == 0
        || As<MyGUI::TextBox>(label) == 0 || e == 0 || As<MyGUI::TextBox>(hint) == 0 || As<MyGUI::TextBox>(count) == 0
        || As<MyGUI::TextBox>(status) == 0 || cb == 0 || sb == 0)
    { gui->destroyWidget(raw); return false; }
    {   /* Kenshi's painted caption text in its own colour, as the panel's PORT and PLAYER NAME labels */
        MyGUI::TextBox* lt = As<MyGUI::TextBox>(label);
        lt->setCaption(MyGUI::UString(coopbug::kLabelText));
        lt->setTextAlign(MyGUI::Align::Left | MyGUI::Align::VCenter);
    }
    StyleText(about1, coopbug::kAboutLine1, MyGUI::Align::Left | MyGUI::Align::VCenter, Lit());
    StyleText(about2, coopbug::kAboutLine2, MyGUI::Align::Left | MyGUI::Align::VCenter, Lit());
    StyleText(hint, coopbug::kBoxHintText, MyGUI::Align::Left | MyGUI::Align::Top, Dim());
    hint->setNeedMouseFocus(false);   /* a click on the hint reaches the box under it */
    StyleText(count, "", MyGUI::Align::Right | MyGUI::Align::VCenter, Lit());
    StyleText(status, "", MyGUI::Align::Center, Lit());
    {   /* Kenshi_EditBox's skin holds its typing area one line tall and centred in the box (Client, align HStretch VCenter, 5 px
           below the top of its 32 px skin); stretched over the whole box at that inset, typed text starts top-left and runs down */
        MyGUI::Widget* ec = e->getClientWidget();
        const int inTop = 5;
        if (ec != 0)
        {
            const int inX = ec->getLeft();
            ec->setCoord(MyGUI::IntCoord(inX, inTop, innerW - 2 * inX, editH - 2 * inTop));
            ec->setAlign(MyGUI::Align::Stretch);
        }
    }
    e->setEditMultiLine(true);        /* Enter is a new line; it never sends */
    e->setEditWordWrap(true);
    e->setMaxTextLength(coopbug::kDescMaxChars);
    e->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    e->setCaption(MyGUI::UString(g_caption.c_str()));
    e->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    cb->setCaption(MyGUI::UString(coopbug::kCancelCaption));
    sb->setCaption(MyGUI::UString(coopbug::kSendCaption));
    cb->eventMouseButtonClick += MyGUI::newDelegate(OnCancel);
    sb->eventMouseButtonClick += MyGUI::newDelegate(OnSend);
    cb->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    sb->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->addWidgetModal(raw); im->setKeyFocusWidget(text); }
    g_shownLen = (size_t)-1; g_shownStatus = "-"; g_shownBusy = -1; g_shownSendOk = -1;
    g_winWired = 1;   /* the last step: a window found without it is rebuilt */
    const MyGUI::IntCoord c = raw->getAbsoluteCoord();
    DebugLog("[UI] rect bugreport x=" + NumI(c.left) + " y=" + NumI(c.top) + " w=" + NumI(c.width) + " h=" + NumI(c.height));
    return true;
}

/* A message box (mock-up sections 5 and 6): Kenshi_MessageBox.layout's look as the mod's other boxes build it - Kenshi_WindowC
   (a title bar, no X), the sentence centred in the message box's colour, Kenshi_Button2 buttons lit by hand, one OK bottom-right
   or the back button left and the action right. Centred on the screen, over the report window. */
typedef void (*ClickFn)(MyGUI::Widget*);
bool BuildBox(MyGUI::Gui* gui, const char* boxName, const char* title, const std::string& text, const char* textName,
              const char* b0Name, const char* b0Caption, ClickFn b0Click, const char* b1Name, const char* b1Caption, ClickFn b1Click)
{
    int vw = 0, vh = 0;
    ViewSize(&vw, &vh);
    const int w = Clamp(vw * 30 / 100, 440, vw - 20);
    const int h = Clamp(coopui::NoticeBoxH(text, w), 150, vh - 20);
    MyGUI::Widget* raw = MkRoot(gui, "Kenshi_WindowC", MyGUI::IntCoord((vw - w) / 2, (vh - h) / 2, w, h), boxName);
    MyGUI::Window* win = As<MyGUI::Window>(raw);
    MyGUI::Widget* cl = win != 0 ? win->getClientWidget() : 0;
    if (cl == 0) { if (raw != 0) gui->destroyWidget(raw); return false; }
    win->setMovable(false);
    win->setCaption(MyGUI::UString(title));
    win->setNeedKeyFocus(true);
    win->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    const int W = cl->getWidth(), H = cl->getHeight();
    const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);
    const int btnW = Clamp(W / 4, 120, W / 2 - 12);
    MyGUI::EditBox* e = As<MyGUI::EditBox>(Mk(cl, "EditBox", "Kenshi_WordWrapEmpty", 8, lay.textY, W - 16, lay.textH, textName));
    MyGUI::Button* b0 = As<MyGUI::Button>(Mk(cl, "Button", "Kenshi_Button2", b1Name != 0 ? 8 : W - btnW - 8, lay.btnY, btnW, lay.btnH, b0Name));
    MyGUI::Button* b1 = b1Name != 0 ? As<MyGUI::Button>(Mk(cl, "Button", "Kenshi_Button2", W - btnW - 8, lay.btnY, btnW, lay.btnH, b1Name)) : 0;
    if (e == 0 || b0 == 0 || (b1Name != 0 && b1 == 0)) { gui->destroyWidget(raw); return false; }
    e->setEditStatic(true); e->setEditReadOnly(true); e->setEditMultiLine(true); e->setEditWordWrap(true);
    e->setTextAlign(MyGUI::Align::Center);
    e->setTextColour(Lit());
    e->setCaption(MyGUI::UString(text.c_str()));
    b0->setCaption(MyGUI::UString(b0Caption));
    b0->setTextColour(Lit());   /* Kenshi_Button2's own caption colour is dim - lit by hand, as ui.cpp's boxes */
    b0->eventMouseButtonClick += MyGUI::newDelegate(b0Click);
    b0->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    if (b1 != 0)
    {
        b1->setCaption(MyGUI::UString(b1Caption));
        b1->setTextColour(Lit());
        if (b1Click != 0) b1->eventMouseButtonClick += MyGUI::newDelegate(b1Click);
        b1->eventKeyButtonPressed += MyGUI::newDelegate(OnKey);
    }
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im != 0) { im->addWidgetModal(raw); im->setKeyFocusWidget(raw); }
    const MyGUI::IntCoord c = raw->getAbsoluteCoord();
    DebugLog(std::string("[UI] rect ") + boxName + " x=" + NumI(c.left) + " y=" + NumI(c.top) + " w=" + NumI(c.width) + " h=" + NumI(c.height));
    return true;
}

/* The status line between the buttons: one line when it fits the space, else the same words on two lines, broken at the space
   nearest the middle (two lines of the standard face fit the buttons' height). */
void SetCaptionFitted(MyGUI::TextBox* t, const std::string& line)
{
    t->setCaption(MyGUI::UString(line.c_str()));
    if (t->getTextSize().width <= t->getWidth()) return;
    const size_t mid = line.size() / 2;
    size_t best = std::string::npos;
    for (size_t k = 0; k < line.size(); ++k)
    {
        if (line[k] != ' ') continue;
        const size_t dk = k > mid ? k - mid : mid - k;
        const size_t db = best == std::string::npos ? (size_t)-1 : (best > mid ? best - mid : mid - best);
        if (dk < db) best = k;
    }
    if (best == std::string::npos) return;
    t->setCaption(MyGUI::UString((line.substr(0, best) + "\n" + line.substr(best + 1)).c_str()));
}

/* The window's live parts follow the state and the box's text: the counter, the hint in the box, the hint / status line, which
   controls are greyed. Read from the box itself every frame; written only when something changed. */
void SyncWindow(MyGUI::Gui* gui)
{
    MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kText));
    if (e == 0) return;
    const int busy = (g_st == kStBusy) ? 1 : 0;
    const size_t len = e->getTextLength();
    if (!busy && g_st == kStEdit)
    {
        const char* p = e->getCaption().asUTF8_c_str();
        g_caption = p != 0 ? p : "";
    }
    const int sendOk = (!busy && coopbug::DescriptionSendable(coopbug::CaptionToPlain(g_caption))) ? 1 : 0;
    std::string status;
    if (busy)
    {
        int phase = kPhPreparing, pct = 0;
        { Lock l; if (g_sh.gen == g_gen) { phase = g_sh.phase; pct = g_sh.pct; } }
        status = phase == kPhPreparing ? std::string(coopbug::kPreparingLine) : coopbug::SendingLine(pct);
    }
    else if (!sendOk) status = coopbug::kNeedTextLine;   /* the line says why SEND is greyed: nothing but spaces yet (370) */
    if (len != g_shownLen)
    {
        g_shownLen = len;
        MyGUI::TextBox* c = As<MyGUI::TextBox>(Find(gui, kCounter));
        if (c != 0) c->setCaption(MyGUI::UString(coopbug::CounterText((unsigned int)len).c_str()));
    }
    MyGUI::Widget* hint = Find(gui, kTextHint);
    if (hint != 0) hint->setVisible(len == 0 && !busy);
    if (status != g_shownStatus)
    {
        g_shownStatus = status;
        MyGUI::TextBox* s = As<MyGUI::TextBox>(Find(gui, kStatus));
        if (s != 0) SetCaptionFitted(s, status);
    }
    if (busy != g_shownBusy)
    {
        g_shownBusy = busy;
        e->setEditReadOnly(busy != 0);
        e->setEnabled(busy == 0);   /* greyed while sending; only CANCEL works */
        e->setAlpha(busy ? 0.6f : 1.0f);
        MyGUI::Widget* l = Find(gui, kLabel);
        if (l != 0) l->setAlpha(busy ? 0.5f : 1.0f);
    }
    if (sendOk != g_shownSendOk)
    {
        g_shownSendOk = sendOk;
        MyGUI::Widget* s = Find(gui, kSendBtn);
        if (s != 0) s->setEnabled(sendOk != 0);
    }
}

/* MyGUI's key focus is kept on what is on top - the box's window, or the typed box - so ESC is heard and typing goes there. */
void KeepFocus(MyGUI::Gui* gui)
{
    MyGUI::InputManager* im = MyGUI::InputManager::getInstancePtr();
    if (im == 0) return;
    const char* topName = g_st == kStSent ? kSentBox : (g_st == kStFailed ? kFailBox : kWin);
    MyGUI::Widget* top = Find(gui, topName);
    if (top == 0) return;
    MyGUI::Widget* f = im->getKeyFocusWidget();
    for (int d = 0; f != 0 && d < 40; ++d, f = f->getParent()) if (f == top) return;
    MyGUI::Widget* want = (g_st == kStEdit) ? Find(gui, kText) : top;
    im->setKeyFocusWidget(want != 0 ? want : top);
}

/* THE NOTE'S OWN FONTS (owner 437): the game's Exo 2 and Sentencia files at sizes Kenshi's font list does not carry - the lines
   at 52 (about 22 px a line, twice the note's first 11 px; 40 measured 17 px, 60 measured 26) and the heading at 56 (Kenshi pairs its Sentencia headings with Exo 2
   text at 1.0 to 1.17 times the size). Made once, through MyGUI's own resource loader, from the definition kenshi_fonts.xml
   gives its fonts; a font of that name already made is reused. A font that cannot be made gives way to the game's largest of
   the same face. */
const char* const kNoteBodyFont = "SharedWastelands_NoteBody";
const char* const kNoteHeadFont = "SharedWastelands_NoteHead";
const int kNoteBodySize = 52;
const int kNoteHeadSize = 56;
int g_noteFontsDone = 0;
std::string g_noteBodyUse = "Kenshi_FloaterFont_Large";
std::string g_noteHeadUse = "Kenshi_PaintedTextFont_Large";

std::string NoteFontXml(const char* name, const char* source, int size)
{
    return std::string("<Resource type=\"ResourceTrueTypeFont\" name=\"") + name + "\">"
           "<Property key=\"Source\" value=\"" + source + "\"/>"
           "<Property key=\"Size\" value=\"" + NumI(size) + "\"/>"
           "<Property key=\"Hinting\" value=\"use_native\"/>"
           "<Property key=\"Resolution\" value=\"50\"/>"
           "<Property key=\"Antialias\" value=\"false\"/>"
           "<Property key=\"TabWidth\" value=\"4\"/>"
           "<Property key=\"OffsetHeight\" value=\"0\"/>"
           "<Property key=\"SubstituteCode\" value=\"0\"/>"
           "<Property key=\"Distance\" value=\"2\"/>"
           "<Codes><Code range=\"32 126\"/><Code range=\"8127 8217\"/></Codes>"
           "</Resource>";
}

/* true when the resource of that name is a font with its glyph texture drawn (its face file was found and read) */
bool NoteFontUsable(MyGUI::ResourceManager* rm, const char* name)
{
    if (rm == 0 || !rm->isExist(name)) return false;
    MyGUI::IResource* r = rm->getByName(name, false);
    MyGUI::IFont* f = r != 0 ? r->castType<MyGUI::IFont>(false) : 0;
    return f != 0 && f->getTextureFont() != 0 && f->getDefaultHeight() > 0;
}

/* The two fonts are parsed from XML text by MyGUI's own reader (xml::Document::open on a string stream - the plugin and the
   game's MyGUI both use the VS2010 runtime, msvcp100/msvcr100) and handed to ResourceManager::loadFromXmlNode, as the game's
   font file is. */
void NoteFontsMake()
{
    if (g_noteFontsDone) return;
    g_noteFontsDone = 1;
    MyGUI::ResourceManager* rm = MyGUI::ResourceManager::getInstancePtr();
    if (rm != 0 && (!rm->isExist(kNoteBodyFont) || !rm->isExist(kNoteHeadFont)))
    {
        std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MyGUI type=\"Resource\" version=\"1.1\">";
        if (!rm->isExist(kNoteBodyFont)) xml += NoteFontXml(kNoteBodyFont, "Exo2-SemiBold.ttf", kNoteBodySize);
        if (!rm->isExist(kNoteHeadFont)) xml += NoteFontXml(kNoteHeadFont, "Sentencia_1.ttf", kNoteHeadSize);
        xml += "</MyGUI>";
        try
        {
            std::istringstream in(xml);
            MyGUI::xml::Document doc;
            if (doc.open(in) && doc.getRoot() != 0) rm->loadFromXmlNode(doc.getRoot(), "", MyGUI::Version(1, 1, 0));
        }
        catch (...) {}
    }
    const bool bodyOk = NoteFontUsable(rm, kNoteBodyFont);
    const bool headOk = NoteFontUsable(rm, kNoteHeadFont);
    if (bodyOk) g_noteBodyUse = kNoteBodyFont;
    if (headOk) g_noteHeadUse = kNoteHeadFont;
    DebugLog(std::string("[UI] note fonts: body '") + g_noteBodyUse + "' " + (bodyOk ? "ok" : "fallback")
             + " head '" + g_noteHeadUse + "' " + (headOk ? "ok" : "fallback"));
}

/* The note's heading and lines in these fonts, measured at full size: the panel's size and the two boxes' rows. */
void NoteFonts(MyGUI::TextBox* ht, MyGUI::TextBox* bt, const std::string& headFont, const std::string& bodyFont, int pw, int ph,
               int pad, MyGUI::IntSize* hs, MyGUI::IntSize* bs, int* w, int* h)
{
    ht->setFontName(headFont);
    bt->setFontName(bodyFont);
    ht->setSize(pw, ph);
    bt->setSize(pw, ph);
    *hs = ht->getTextSize();
    *bs = bt->getTextSize();
    *w = (hs->width > bs->width ? hs->width : bs->width) + 2 * pad;
    *h = pad + hs->height + pad / 2 + bs->height + pad;
}

/* THE TITLE SCREEN'S NOTE (owner 425): a painted heading and four plain lines saying the mod is experimental and where bugs are
   reported. Children of the title art, built and taken down with the title button. Placed by swtitle::PlaceTitleNote: on the
   mod's own art (titleart.cpp, T-513) in the open band under its painted title and above its figures, beside the menu column;
   on the game's art, the right side a little below the middle. Never over the menu column or the REPORT A BUG button (`btn`):
   when the note does not fit in the mod's fonts it is made again in the game's (Kenshi_PaintedTextFont_Large heading,
   Kenshi_FloaterFont_Large lines), and when it does not fit in those either there is no note. */
void TitleNoteBuild(MyGUI::Widget* art, int pw, int ph, int margin, const MyGUI::IntCoord& btn)
{
    NoteFontsMake();
    MyGUI::Widget* panel = Mk(art, "Widget", "Kenshi_FloatingPanelSkin", 0, 0, 10, 10, kTitleNoteP);
    if (panel == 0) return;
    panel->setNeedMouseFocus(false);
    MyGUI::Widget* head = Mk(panel, "TextBox", "Kenshi_TextboxStandardText_Large", 0, 0, 10, 10, kTitleNoteH);
    MyGUI::Widget* body = Mk(panel, "TextBox", "Kenshi_TextboxStandardText_Large", 0, 0, 10, 10, kTitleNoteB);
    MyGUI::TextBox* ht = As<MyGUI::TextBox>(head);
    MyGUI::TextBox* bt = As<MyGUI::TextBox>(body);
    if (ht == 0 || bt == 0) return;
    ht->setCaption(MyGUI::UString(coopbug::kTitleNoteHead));
    bt->setCaption(MyGUI::UString(coopbug::kTitleNoteBody));
    ht->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    bt->setTextAlign(MyGUI::Align::Left | MyGUI::Align::Top);
    ht->setTextColour(MyGUI::Colour(1.0f, 1.0f, 1.0f, 1.0f));
    bt->setTextColour(Lit());
    ht->setTextShadow(true);
    bt->setTextShadow(true);
    ht->setTextShadowColour(MyGUI::Colour(0.0f, 0.0f, 0.0f, 1.0f));
    bt->setTextShadowColour(MyGUI::Colour(0.0f, 0.0f, 0.0f, 1.0f));
    head->setNeedMouseFocus(false);
    body->setNeedMouseFocus(false);
    const int pad = Clamp(ph * 15 / 1000, 10, 24);
    MyGUI::Widget* ver = UiFindLayoutSuffix(art, "VersionText");
    const int verTop = ver != 0 ? ver->getAbsoluteTop() - art->getAbsoluteTop() : ph * 9778 / 10000;
    /* the menu column's right edge: the layout's ExitButton (every column button shares its left and width) */
    MyGUI::Widget* exitBtn = UiFindLayoutSuffix(art, "ExitButton");
    const int colRight = exitBtn != 0 ? exitBtn->getRight() : (int)((long long)pw * 416667 / 1000000);
    int bandTop = 0, bandBottom = 0;
    const int modArt = TitleArtNoteBand(art, &bandTop, &bandBottom);
    const swtitle::Rect br = { btn.left, btn.top, btn.width, btn.height };
    MyGUI::IntSize hs, bs;
    int w = 0, h = 0;
    /* Sentencia heading, Exo 2 lines (the mod's fonts, or the game's when they could not be made) */
    NoteFonts(ht, bt, g_noteHeadUse, g_noteBodyUse, pw, ph, pad, &hs, &bs, &w, &h);
    swtitle::NoteFit f = swtitle::PlaceTitleNote(modArt, pw, ph, w, h, margin, colRight, br, bandTop, bandBottom, verTop);
    const char* fonts = "the note's fonts";
    if (!f.ok)
    {
        NoteFonts(ht, bt, "Kenshi_PaintedTextFont_Large", "Kenshi_FloaterFont_Large", pw, ph, pad, &hs, &bs, &w, &h);
        f = swtitle::PlaceTitleNote(modArt, pw, ph, w, h, margin, colRight, br, bandTop, bandBottom, verTop);
        fonts = "the game's smaller fonts";
    }
    const std::string where = (modArt ? "the mod's art, band " + NumI(bandTop) + "-" + NumI(bandBottom) : std::string("the game's art"))
                              + ", column right " + NumI(colRight) + ", version text top " + NumI(verTop) + ", art " + NumI(pw) + "x" + NumI(ph);
    if (!f.ok)
    {
        MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
        if (gui != 0) gui->destroyWidget(panel);
        DebugLog("[UI] title note not shown: no place clear of the menu column and REPORT A BUG even in the game's smaller fonts ("
                 + NumI(w) + "x" + NumI(h) + " on " + where + ")");
        return;
    }
    panel->setCoord(MyGUI::IntCoord(f.x, f.y, w, h));
    head->setCoord(MyGUI::IntCoord(pad, pad, w - 2 * pad, hs.height));
    body->setCoord(MyGUI::IntCoord(pad, pad + hs.height + pad / 2, w - 2 * pad, bs.height));
    DebugLog("[UI] title note at x=" + NumI(f.x) + " y=" + NumI(f.y) + " w=" + NumI(w) + " h=" + NumI(h)
             + " (heading " + NumI(hs.width) + "x" + NumI(hs.height) + ", lines " + NumI(bs.width) + "x" + NumI(bs.height)
             + ", in " + fonts + ", on " + where + ")");
}

/* The note is hidden while Kenshi's CREDITS panel (the main menu layout's CreditsPanel, a child of the title art) is up, and shown
   again when it closes. */
void NoteCreditsSync(MyGUI::Gui* gui, MyGUI::Widget* art)
{
    MyGUI::Widget* np = Find(gui, kTitleNoteP);
    if (np == 0 || art == 0) return;
    MyGUI::Widget* credits = UiFindLayoutSuffix(art, "CreditsPanel");
    const bool want = !(credits != 0 && credits->getVisible());
    if (np->getVisible() == want) return;
    np->setVisible(want);
    DebugLog(want ? "[UI] title note shown again: the CREDITS panel closed" : "[UI] title note hidden: the CREDITS panel is open");
}

/* THE TITLE SCREEN'S BUTTON (369): bottom-left, Kenshi's message-box button (Kenshi_Button2, its caption lit by hand), a child of
   the title art so it goes with the title screen. Looked for every frame by name; built (at most every 250 ms) when missing, and
   built again with the note when the title art's size is no longer the size they were placed for (a new window size). */
void TitleButtonTick(MyGUI::Gui* gui)
{
    MyGUI::Widget* b = Find(gui, kTitleBtn);
    int resized = 0;
    if (b != 0 && g_titleWired != 0)
    {
        MyGUI::Widget* art = b->getParent();
        if (art != 0 && (art->getWidth() != g_titleW || art->getHeight() != g_titleH))
        {
            DebugLog("[UI] title art " + NumI(g_titleW) + "x" + NumI(g_titleH) + " -> " + NumI(art->getWidth()) + "x" + NumI(art->getHeight())
                     + ": REPORT A BUG and the note are placed again");
            resized = 1;
        }
        else { NoteCreditsSync(gui, art); return; }
    }
    if (b != 0)
    {
        gui->destroyWidget(b); b = 0;
        MyGUI::Widget* np = Find(gui, kTitleNoteP); if (np != 0) gui->destroyWidget(np);
    }
    g_titleWired = 0;
    const DWORD now = ::GetTickCount();
    if (!resized && (DWORD)(now - g_titleLookMs) < 250) return;
    g_titleLookMs = now;
    MyGUI::Widget* exitBtn = 0;
    MyGUI::EnumeratorWidgetPtr roots = gui->getEnumerator();
    while (exitBtn == 0 && roots.next()) exitBtn = UiFindLayoutSuffix(roots.current(), "ExitButton");
    MyGUI::Widget* art = exitBtn != 0 ? exitBtn->getParent() : 0;
    if (art == 0) return;
    const int pw = art->getWidth(), ph = art->getHeight();
    if (pw < 200 || ph < 200) return;
    /* Kenshi_MessageBox.layout's button is 0.0477 of the screen wide and 0.0335 tall; the margin matches the version text's
       distance from the bottom edge on the other side */
    const int h = Clamp(ph * 335 / 10000, 26, 60);
    int w = Clamp(pw * 477 / 10000, 130, pw / 3);
    const int margin = Clamp(ph * 185 / 10000, 8, 40);
    MyGUI::Button* btn = As<MyGUI::Button>(Mk(art, "Button", "Kenshi_Button2", margin, ph - h - margin, w, h, kTitleBtn));
    if (btn == 0) return;
    btn->setCaption(MyGUI::UString(coopbug::kButtonCaption));
    btn->setTextColour(Lit());
    const MyGUI::IntSize ts = btn->getTextSize();
    if (ts.width + 32 > w) { w = ts.width + 32; btn->setSize(w, h); }
    btn->eventMouseButtonClick += MyGUI::newDelegate(OnOpenTitle);
    g_titleWired = 1;
    g_titleW = pw;
    g_titleH = ph;
    TitleNoteBuild(art, pw, ph, margin, btn->getCoord());
    NoteCreditsSync(gui, art);
    if (!g_titleLogged)
    {
        g_titleLogged = 1;
        DebugLog("[UI] REPORT A BUG on the title screen at x=" + NumI(margin) + " y=" + NumI(ph - h - margin) + " w=" + NumI(w) + " h=" + NumI(h)
                 + " (title art " + NumI(pw) + "x" + NumI(ph) + ")");
    }
}

/* What ESC means now: CANCEL in the window (it stops a send in progress), BACK on CAN'T SEND REPORT, OK on REPORT SENT. */
int EscAction()
{
    if (g_st == kStEdit || g_st == kStBusy) return kActCancel;
    if (g_st == kStFailed) return kActBack;
    if (g_st == kStSent) return kActOk;
    return kActNone;
}
void Apply(MyGUI::Gui* gui, int act, int atTitle)
{
    switch (act)
    {
        case kActOpenTitle:
        case kActOpenPause:
            if (g_st != kStClosed) return;
            g_st = kStEdit;
            g_fromPause = (act == kActOpenPause && !atTitle) ? 1 : 0;
            g_buildFails = 0;
            DebugLog(std::string("[BUG] the REPORT A BUG window opened from ") + (g_fromPause ? "the pause menu" : (atTitle ? "the title screen" : "a world")));
            return;
        case kActCancel:
            if (g_st == kStBusy) { CancelJob("CANCEL"); g_st = kStEdit; return; }
            if (g_st == kStEdit) { g_st = kStClosed; DebugLog("[BUG] the REPORT A BUG window closed (CANCEL); its text is kept"); }
            return;
        case kActSend:
        {
            if (g_st != kStEdit) return;
            MyGUI::EditBox* e = As<MyGUI::EditBox>(Find(gui, kText));
            if (e != 0) { const char* p = e->getCaption().asUTF8_c_str(); g_caption = p != 0 ? p : ""; }
            const std::string plain = coopbug::CaptionToPlain(g_caption);
            if (!coopbug::DescriptionSendable(plain)) return;   /* read again at the press: SEND is greyed then anyway (370) */
            StartJob(plain, 0);
            g_st = kStBusy;
            return;
        }
        case kActOk:
            if (g_st != kStSent) return;
            g_st = kStClosed;
            return;
        case kActBack:
            if (g_st != kStFailed) return;
            { Lock l; g_sh.body.clear(); g_sh.boundary.clear(); }
            g_st = kStEdit;
            return;
        case kActRetry:
            if (g_st != kStFailed) return;
            StartJob(coopbug::CaptionToPlain(g_caption), 1);
            g_st = kStBusy;
            return;
        default: return;
    }
}
/* A report's worker has finished: REPORT SENT, or CAN'T SEND REPORT with its reason. */
void PollJob()
{
    if (g_st != kStBusy || g_gen == 0) return;
    int done = 0, ok = 0, kind = 0, code = 0;
    { Lock l; if (g_sh.gen == g_gen && g_sh.phase == kPhDone) { done = 1; ok = g_sh.ok; kind = g_sh.failKind; code = g_sh.failCode; } }
    if (!done) return;
    g_gen = 0;
    if (ok) { g_st = kStSent; g_caption.clear(); { Lock l; g_sh.body.clear(); g_sh.boundary.clear(); } return; }   /* a sent report's text is not kept */
    g_failKind = kind; g_failCode = code;
    g_st = kStFailed;
}

/* The widgets follow the state: the window for EDIT / BUSY / FAILED, CAN'T SEND REPORT over it for FAILED, REPORT SENT alone. */
void SyncWidgets(MyGUI::Gui* gui)
{
    const int wantWin = (g_st == kStEdit || g_st == kStBusy || g_st == kStFailed) ? 1 : 0;
    MyGUI::Widget* win = Find(gui, kWin);
    if (win != 0 && (!wantWin || !g_winWired)) { DropRoot(gui, kWin); win = 0; g_winWired = 0; }
    if (wantWin && win == 0 && g_st != kStFailed)
    {
        if (!BuildWindow(gui))
        {
            ++g_buildFails;
            ErrorLog("[UI] the REPORT A BUG window could not be built (try " + NumI(g_buildFails) + ")" + (g_buildFails >= 3 ? " - it is closed" : ""));
            if (g_buildFails >= 3) { if (g_st == kStBusy) CancelJob("the window could not be built"); g_st = kStClosed; }
        }
    }
    MyGUI::Widget* fail = Find(gui, kFailBox);
    if (fail != 0 && (g_st != kStFailed || !g_failWired)) { DropRoot(gui, kFailBox); fail = 0; g_failWired = 0; }
    if (g_st == kStFailed && fail == 0)
    {
        if (BuildBox(gui, kFailBox, coopbug::kFailTitle, coopbug::FailText(g_failKind, g_failCode), kFailText,
                     kFailBack, coopbug::kBackCaption, &OnBack, kFailRetry, coopbug::kTryAgainCaption, &OnRetry))
            g_failWired = 1;
        else { ErrorLog("[UI] the CAN'T SEND REPORT box could not be built - back to the window"); g_st = kStEdit; }
    }
    MyGUI::Widget* sent = Find(gui, kSentBox);
    if (sent != 0 && (g_st != kStSent || !g_sentWired)) { DropRoot(gui, kSentBox); sent = 0; g_sentWired = 0; }
    if (g_st == kStSent && sent == 0)
    {
        if (BuildBox(gui, kSentBox, coopbug::kSentTitle, coopbug::kSentText, kSentText, kSentOk, coopbug::kOkCaption, &OnOk, 0, 0, 0))
            g_sentWired = 1;
        else { ErrorLog("[UI] the REPORT SENT box could not be built"); g_st = kStClosed; }
    }
    if (g_st == kStEdit || g_st == kStBusy || g_st == kStFailed) SyncWindow(gui);
    if (g_st != kStClosed) KeepFocus(gui);
}

__declspec(noinline) void UiInner(int atTitle)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    if (atTitle) TitleButtonTick(gui);
    int act = (int)::InterlockedExchange(&g_act, kActNone);
    int esc = (int)::InterlockedExchange(&g_esc, 0);
    /* In a world, over the pause menu: the engine's own ESC closes the menu (MyGUI hears the key first, then the engine's toggle
       runs); found closed while this feature is up, that ESC was meant for it, and the menu is opened again below. */
    int reopenMenu = 0;
    if (!atTitle && g_fromPause && g_st != kStClosed && UiPauseMenuVisible() == 0) { esc = 1; reopenMenu = 1; }
    if (esc != 0 && act == kActNone) act = EscAction();
    if (act != kActNone) Apply(gui, act, atTitle);
    PollJob();
    SyncWidgets(gui);
    if (reopenMenu)
    {
        const int r = UiPauseMenuReshow();
        if (r != 1) ErrorLog("[UI] the pause menu could not be opened again after ESC over REPORT A BUG (" + NumI(r) + ")");
    }
    if (g_st == kStClosed) g_fromPause = 0;
    ::InterlockedExchange(&g_uiUp, g_st != kStClosed ? 1 : 0);
}
volatile LONG g_lastCode = 0;
long long g_softStrikes = 0;
int Filter(unsigned long code) { ::InterlockedExchange(&g_lastCode, (LONG)code); return EXCEPTION_EXECUTE_HANDLER; }
int IsMemoryFault(unsigned long c) { return (c == 0xC0000005ul || c == 0xC0000006ul || c == 0xC00000FDul || c == 0x80000001ul || c == 0x80000002ul) ? 1 : 0; }
// The SEH frame. No local object with a destructor may appear here, and none does.
int UiGuarded(int atTitle)
{
    __try { UiInner(atTitle); }
    __except (Filter(GetExceptionCode())) { return 1; }
    return 0;
}
/* After the latch: whatever of this feature is on screen goes, so no modal window of ours can be left holding the game. */
__declspec(noinline) void TakeDownInner()
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    if (gui == 0) return;
    DropRoot(gui, kFailBox);
    DropRoot(gui, kSentBox);
    DropRoot(gui, kWin);
    MyGUI::Widget* t = Find(gui, kTitleBtn);
    if (t != 0) gui->destroyWidget(t);
    MyGUI::Widget* np = Find(gui, kTitleNoteP);
    if (np != 0) gui->destroyWidget(np);
}
int TakeDownGuarded()
{
    __try { TakeDownInner(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    return 0;
}

/* The pause menu's row (378 d). `panel` is the engine's MainMenuPopup; its widget is panel+8, the layout's MainMenuPopupPanel under
   it. REPORT A BUG is made once per menu, EXIT GAME's size, Kenshi_Button1 like its neighbours, lettered in Highlight(); where it and
   every other row stand is ui.cpp UiPauseMenuArrange's (pausemenu.h Place: its own row set apart by Kenshi's gap below EXIT GAME,
   RESUME one row and gap below it, the panel grown around its centre), called here once the button exists. A menu that already
   holds the button is left to the arranging the menu's show does on every open. */
__declspec(noinline) void PauseRowInner(void* panel)
{
    MyGUI::Gui* gui = MyGUI::Gui::getInstancePtr();
    MyGUI::Widget* root = *(MyGUI::Widget**)((char*)panel + 8);
    if (gui == 0 || root == 0) return;
    MyGUI::Widget* pp = UiFindLayoutSuffix(root, "MainMenuPopupPanel");
    MyGUI::Widget* mine = Find(gui, kPauseBtn);
    if (mine != 0 && mine->getParent() == pp && g_pauseWired) return;
    if (mine != 0) { gui->destroyWidget(mine); g_pauseWired = 0; }
    MyGUI::Widget* ex = pp != 0 ? UiFindLayoutSuffix(pp, "ExitGameButton") : 0;
    if (ex == 0)
    {
        if (!g_pauseLogged) { g_pauseLogged = 1; ErrorLog("[UI] REPORT A BUG was not added to the pause menu: EXIT GAME was not found under MainMenuPopupPanel"); }
        return;
    }
    const MyGUI::IntCoord e = ex->getCoord();
    MyGUI::Button* b = As<MyGUI::Button>(Mk(pp, "Button", "Kenshi_Button1", e.left, e.top, e.width, e.height, kPauseBtn));
    if (b == 0) return;
    b->setCaption(MyGUI::UString(coopbug::kButtonCaption));
    b->setTextColour(Highlight());
    b->eventMouseButtonClick += MyGUI::newDelegate(OnOpenPause);
    g_pauseWired = 1;
    const int r = UiPauseMenuArrange(panel);
    if (!g_pauseLogged)
    {
        g_pauseLogged = 1;
        DebugLog("[UI] REPORT A BUG added to the pause menu below EXIT GAME; the menu arranged (" + NumI(r) + ")");
    }
}
int PauseRowGuarded(void* panel)
{
    __try { PauseRowInner(panel); }
    __except (Filter(GetExceptionCode())) { return 1; }
    return 0;
}
void Latch(const char* where)
{
    const unsigned long code = (unsigned long)(LONG)g_lastCode;
    const int mem = IsMemoryFault(code);
    if (!mem) ++g_softStrikes;
    const int off = (mem || g_softStrikes >= 3) ? 1 : 0;
    char b[300];
    _snprintf(b, 299, "[UI] REPORT A BUG: a MyGUI call faulted in %s (code 0x%08lX, %s) - %s", where, code,
              mem ? "a memory fault" : "a C++ throw or other", off ? "the feature is OFF for the rest of this process" : "retried on the next frame");
    b[299] = 0;
    ErrorLog(b);
    if (!off) return;
    ::InterlockedExchange(&g_off, 1);
    ::InterlockedExchange(&g_uiUp, 0);
    if (g_st == kStBusy) CancelJob("the feature was turned off after a fault");
    g_st = kStClosed;
    TakeDownGuarded();
}

}   // namespace

void BugReportTick(int atTitle)
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    CsInit();
    try { CollectTick(); AnswerTick(); }
    catch (...) { DebugLog("[BUG] a C++ exception in the nearby-log step; nothing more done this frame"); }
    if (UiGuarded(atTitle) != 0) Latch(atTitle ? "the title screen" : "a world");
}

void BugReportPauseMenuShown(void* panel)
{
    if (panel == 0 || ::InterlockedCompareExchange(&g_off, 0, 0) != 0) return;
    if (PauseRowGuarded(panel) != 0) Latch("the pause menu");
}

int BugReportTakesEscape()
{
    if (::InterlockedCompareExchange(&g_off, 0, 0) != 0) return 0;
    if (::InterlockedCompareExchange(&g_uiUp, 0, 0) == 0) return 0;
    ::InterlockedExchange(&g_esc, 1);
    return 1;
}

void BugReportLiveIn(unsigned int innerType, unsigned int originSlot, const char* data, size_t len)
{
    CsInit();
    try
    {
        if (innerType == cooplive::kInnerLogAsk)
        {
            unsigned int askId = 0, maxBytes = 0;
            if (!coopbug::LogAskDecode(data, len, &askId, &maxBytes)) { DebugLog("[BUG] a malformed nearby log ask from slot " + Num(originSlot) + " - dropped"); return; }
            AnswerStart(askId, originSlot, maxBytes);
            return;
        }
        if (innerType == cooplive::kInnerLogPart)
        {
            coopbug::LogPart p;
            if (!coopbug::LogPartDecode(data, len, &p)) { DebugLog("[BUG] a malformed nearby log part from slot " + Num(originSlot) + " - dropped"); return; }
            if (!g_col.active || p.askId != g_col.askId) return;   /* an answer to an ask that is over */
            if (!g_col.joins[originSlot].Add(p)) DebugLog("[BUG] a nearby log part from slot " + Num(originSlot) + " disagrees with its first part - dropped");
        }
    }
    catch (...) { DebugLog("[BUG] a C++ exception reading a nearby log message; it is dropped"); }
}

}   // namespace coop
