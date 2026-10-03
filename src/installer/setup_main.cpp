/* src/installer/setup_main.cpp - SharedWastelandsSetup.exe (installer1, stage 6; owner decisions 114/115/117/119/120).
 *
 * A small window program, shipped in the mod's folder next to SharedWastelandsLoader.dll.  In the Kenshi folder it
 * changes two things and nothing else:
 *   1. copies SharedWastelandsLoader.dll from its own folder into the Kenshi folder (an older copy is overwritten);
 *   2. in Plugins_x64.cfg: saves Plugins_x64.cfg.multiplayer-backup (only if none exists), then adds the line
 *      Plugin=SharedWastelandsLoader after the last Plugin= line, once (src/common/installtext.h, offline-tested).
 * Uninstall takes our line out (only ours), moves the loader DLL to the Recycle Bin and leaves the backup.
 * Order matters: install copies the DLL BEFORE it adds the line, uninstall removes the line BEFORE the DLL -
 * a Plugin= line naming a missing DLL stops the game starting, so no failure in between can leave one.
 *
 * Every word a player sees is the approved mock-up's (decision 119), and nothing else is drawn.
 *
 * TEST-ONLY command line (never shows a window, never touches any folder but the one given):
 *   /test-install <kenshi folder>     exit 0 done, 2 not a Kenshi folder, 3 Kenshi running, 4 could not change files
 *   /test-uninstall <kenshi folder>   same codes
 *   /test-status <kenshi folder>      exit 0 installed, 1 not installed
 *   /test-install-skiprun, /test-uninstall-skiprun <folder>   the same without the "Kenshi is running" check
 *
 * VS2010 v100, C++03.  Static CRT (/MT) so it needs nothing installed; only Windows system DLLs are imported.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <string>
#include <vector>

#include "installtext.h"
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

namespace {

/* ---- THE APPROVED WORDS (decision 119) - the only text this program draws ---- */
const wchar_t* const kTitle        = swnames::kSetupTitleW;
const wchar_t* const kMsgInstall   = SW_NAME_W L" needs to add one small file to your Kenshi folder so the game can start it.";
const wchar_t* const kLabelFolder  = L"Kenshi folder:";
const wchar_t* const kMsgInstalled = SW_NAME_W L" is installed.";
const wchar_t* const kMsgAlready   = SW_NAME_W L" is already installed.";
const wchar_t* const kMsgRemoved   = SW_NAME_W L" has been removed from your Kenshi folder. Your saves are not affected.";
const wchar_t* const kMsgNotFound  = L"Kenshi wasn't found in this folder. Choose the folder that contains kenshi_x64.exe.";
const wchar_t* const kMsgRunning   = L"Kenshi is running. Close Kenshi, then try again.";
const wchar_t* const kMsgNoWrite   = L"Setup couldn't change files in the Kenshi folder. Right-click Setup and choose Run as administrator.";
const wchar_t* const kBtnCancel    = L"Cancel";
const wchar_t* const kBtnInstall   = L"Install";
const wchar_t* const kBtnFinish    = L"Finish";
const wchar_t* const kBtnUninstall = L"Uninstall";
const wchar_t* const kBtnClose     = L"Close";
const wchar_t* const kBtnBrowse    = L"Browse...";
const wchar_t* const kBtnTryAgain  = L"Try Again";

const wchar_t* const kGameExe   = L"kenshi_x64.exe";
const wchar_t* const kLoaderDll = swnames::kLoaderDllW;
const wchar_t* const kCfgName   = L"Plugins_x64.cfg";
const wchar_t* const kCfgBackup = L"Plugins_x64.cfg.multiplayer-backup";
const wchar_t* const kCfgNew    = L"Plugins_x64.cfg.multiplayer-new";

enum Result { kOk = 0, kNotKenshi = 2, kRunning = 3, kWriteFail = 4 };

/* ---------------------------------- small file helpers ---------------------------------- */
std::wstring Join(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty()) return name;
    const wchar_t last = dir[dir.size() - 1];
    return (last == L'\\' || last == L'/') ? dir + name : dir + L"\\" + name;
}

std::wstring Parent(const std::wstring& path)
{
    std::wstring p = path;
    while (!p.empty() && (p[p.size() - 1] == L'\\' || p[p.size() - 1] == L'/')) p.erase(p.size() - 1);
    const size_t at = p.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : p.substr(0, at);
}

bool FileExists(const std::wstring& path)
{
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool HasGame(const std::wstring& folder) { return !folder.empty() && FileExists(Join(folder, kGameExe)); }

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), 0, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

/* The whole file as bytes; false when it cannot be opened or is over 4 MB (Plugins_x64.cfg is a few hundred bytes). */
bool ReadBytes(const std::wstring& path, std::string* out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) != 0 && size.QuadPart <= 4 * 1024 * 1024;
    out->clear();
    if (ok && size.QuadPart > 0)
    {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, &(*out)[0], (DWORD)size.QuadPart, &got, 0) != 0 && got == (DWORD)size.QuadPart;
    }
    CloseHandle(h);
    return ok;
}

/* Write <folder>\Plugins_x64.cfg.multiplayer-new, then move it over the cfg in one step, so a failed write
   never leaves the game's file half-written. */
bool WriteCfg(const std::wstring& folder, const std::string& bytes)
{
    const std::wstring tmp = Join(folder, kCfgNew), cfg = Join(folder, kCfgName);
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = bytes.empty() || (WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, 0) != 0 && put == (DWORD)bytes.size());
    ok = FlushFileBuffers(h) != 0 && ok;
    CloseHandle(h);
    if (ok) ok = MoveFileExW(tmp.c_str(), cfg.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(tmp.c_str());   /* our own temporary file only */
    return ok;
}

bool KenshiRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &pe); more && !found; more = Process32NextW(snap, &pe))
        if (lstrcmpiW(pe.szExeFile, kGameExe) == 0) found = true;
    CloseHandle(snap);
    return found;
}

/* Already installed = our line in Plugins_x64.cfg AND the loader file in the folder. */
bool IsInstalled(const std::wstring& folder)
{
    if (!HasGame(folder) || !FileExists(Join(folder, kLoaderDll))) return false;
    std::string text;
    return ReadBytes(Join(folder, kCfgName), &text) && coopinst::PcfgHasOurLine(text);
}

/* ----------------------------------- install / uninstall ----------------------------------- */
Result Install(const std::wstring& folder, const std::wstring& selfDir, bool checkRunning)
{
    if (!HasGame(folder)) return kNotKenshi;
    if (checkRunning && KenshiRunning()) return kRunning;
    const std::wstring src = Join(selfDir, kLoaderDll), dst = Join(folder, kLoaderDll), cfg = Join(folder, kCfgName);
    if (!FileExists(src)) return kWriteFail;   /* packaging defect: the loader must ship next to Setup */
    std::string text;
    if (!ReadBytes(cfg, &text)) return kWriteFail;
    const std::wstring backup = Join(folder, kCfgBackup);
    if (!FileExists(backup) && !CopyFileW(cfg.c_str(), backup.c_str(), TRUE)) return kWriteFail;
    if (lstrcmpiW(src.c_str(), dst.c_str()) != 0 && !CopyFileW(src.c_str(), dst.c_str(), FALSE)) return kWriteFail;
    bool removedOld = false, changed = false;
    const std::string after = coopinst::PcfgAdd(coopinst::PcfgRemoveOld(text, &removedOld), &changed);
    if ((removedOld || changed) && !WriteCfg(folder, after)) return kWriteFail;
    return kOk;
}

bool RecycleFile(const std::wstring& path)
{
    std::vector<wchar_t> from(path.begin(), path.end());
    from.push_back(L'\0');
    from.push_back(L'\0');   /* SHFileOperation wants a double-NUL-terminated list */
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = &from[0];
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&op);
    return rc == 0 && !op.fAnyOperationsAborted && !FileExists(path);
}

Result Uninstall(const std::wstring& folder, bool checkRunning)
{
    if (!HasGame(folder)) return kNotKenshi;
    if (checkRunning && KenshiRunning()) return kRunning;
    const std::wstring cfg = Join(folder, kCfgName), dll = Join(folder, kLoaderDll);
    if (FileExists(cfg))
    {
        std::string text;
        if (!ReadBytes(cfg, &text)) return kWriteFail;
        bool changed = false;
        const std::string after = coopinst::PcfgRemove(text, &changed);
        if (changed && !WriteCfg(folder, after)) return kWriteFail;
    }
    if (FileExists(dll) && !RecycleFile(dll)) return kWriteFail;
    return kOk;
}

/* ------------------------------------ finding the folder ------------------------------------ */
std::wstring SteamPath()
{
    HKEY key = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[MAX_PATH * 2];
    DWORD type = 0, bytes = sizeof(buf) - sizeof(wchar_t);
    std::wstring out;
    if (RegQueryValueExW(key, L"SteamPath", 0, &type, (LPBYTE)buf, &bytes) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
    {
        buf[bytes / sizeof(wchar_t)] = L'\0';
        out = buf;
        for (size_t i = 0; i < out.size(); ++i) if (out[i] == L'/') out[i] = L'\\';
    }
    RegCloseKey(key);
    return out;
}

/* 1. two levels up from Setup's own folder (<Kenshi>\mods\<name>\); 2. the Workshop layout
   (...\steamapps\workshop\content\233860\<id>\ -> ...\steamapps\common\Kenshi); 3. Steam's registry path and
   every library in its libraryfolders.vdf, each + steamapps\common\Kenshi.  "" when none holds kenshi_x64.exe. */
std::wstring FindKenshi(const std::wstring& selfDir)
{
    const std::wstring up2 = Parent(Parent(selfDir));
    if (HasGame(up2)) return up2;
    const std::wstring ws = FromUtf8(coopinst::WorkshopGameFolder(ToUtf8(selfDir)));
    if (HasGame(ws)) return ws;
    const std::wstring steam = SteamPath();
    if (steam.empty()) return std::wstring();
    std::vector<std::wstring> libs(1, steam);
    std::string vdf;
    if (ReadBytes(Join(steam, L"steamapps\\libraryfolders.vdf"), &vdf))
    {
        const std::vector<std::string> more = coopinst::VdfLibraryPaths(vdf);
        for (size_t i = 0; i < more.size(); ++i) libs.push_back(FromUtf8(more[i]));
    }
    for (size_t i = 0; i < libs.size(); ++i)
    {
        const std::wstring cand = Join(libs[i], L"steamapps\\common\\Kenshi");
        if (HasGame(cand)) return cand;
    }
    return std::wstring();
}

std::wstring SelfDir()
{
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(0, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return std::wstring();
    return Parent(std::wstring(buf, n));
}

/* --------------------------------------------- the window --------------------------------------------- */
enum Screen { kScrInstall, kScrInstalled, kScrAlready, kScrRemoved, kScrNotFound, kScrRunning, kScrNoWrite };
enum Pending { kDoInstall, kDoUninstall };
enum { IDC_MSG = 1001, IDC_LABEL = 1002, IDC_PATH = 1003, IDC_BROWSE = 1004, IDC_LEFT = 1005 };

struct State
{
    std::wstring selfDir, folder;
    Screen screen;
    Pending pending;
    bool placed;
};
State g;

int CALLBACK BrowseInit(HWND wnd, UINT msg, LPARAM, LPARAM data)
{
    if (msg == BFFM_INITIALIZED && data) SendMessageW(wnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

bool PickFolder(HWND owner, std::wstring* out)
{
    wchar_t name[MAX_PATH];
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.pszDisplayName = name;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    bi.lpfn = BrowseInit;
    bi.lParam = (LPARAM)(g.folder.empty() ? 0 : g.folder.c_str());
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH];
    const bool ok = SHGetPathFromIDListW(pidl, path) != 0;
    CoTaskMemFree(pidl);
    if (ok) *out = path;
    return ok;
}

void Place(HWND dlg, int id, int x, int y, int w, int h, bool show)
{
    HWND c = GetDlgItem(dlg, id);
    SetWindowPos(c, 0, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

int Dlu(HWND dlg, int x, bool horizontal)
{
    RECT r = { 0, 0, horizontal ? x : 0, horizontal ? 0 : x };
    MapDialogRect(dlg, &r);
    return horizontal ? r.right : r.bottom;
}

void Show(HWND dlg, Screen s)
{
    g.screen = s;
    const wchar_t* msg = kMsgInstall;
    const wchar_t* left = 0;        /* the secondary action, left of the main one; 0 = none */
    const wchar_t* main = kBtnFinish;
    switch (s)
    {
    case kScrInstall:   msg = kMsgInstall;   left = kBtnCancel;    main = kBtnInstall;  break;
    case kScrInstalled: msg = kMsgInstalled;                       main = kBtnFinish;   break;
    case kScrAlready:   msg = kMsgAlready;   left = kBtnUninstall; main = kBtnClose;    break;
    case kScrRemoved:   msg = kMsgRemoved;                         main = kBtnFinish;   break;
    case kScrNotFound:  msg = kMsgNotFound;  left = kBtnCancel;    main = kBtnBrowse;   break;
    case kScrRunning:   msg = kMsgRunning;   left = kBtnCancel;    main = kBtnTryAgain; break;
    case kScrNoWrite:   msg = kMsgNoWrite;                         main = kBtnClose;    break;
    }
    SetDlgItemTextW(dlg, IDC_MSG, msg);
    SetDlgItemTextW(dlg, IDC_LEFT, left ? left : L"");
    SetDlgItemTextW(dlg, IDOK, main);
    SetDlgItemTextW(dlg, IDC_PATH, g.folder.c_str());

    /* layout, in dialog units: 7 margin, 4 between buttons, buttons 50 x 14 (the Windows dialog standard) */
    const int m = Dlu(dlg, 7, true), mv = Dlu(dlg, 7, false), W = Dlu(dlg, 250, true);
    const int bw = Dlu(dlg, 50, true), bh = Dlu(dlg, 14, false), gap = Dlu(dlg, 4, true);
    HDC dc = GetDC(dlg);
    HGDIOBJ old = SelectObject(dc, (HGDIOBJ)SendMessageW(dlg, WM_GETFONT, 0, 0));
    RECT tr = { 0, 0, W, 0 };
    DrawTextW(dc, msg, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(dlg, dc);
    int y = mv;
    Place(dlg, IDC_MSG, m, y, W, tr.bottom, true);
    y += tr.bottom;
    const bool folderRow = (s == kScrInstall);
    if (folderRow)
    {
        y += Dlu(dlg, 7, false);
        Place(dlg, IDC_LABEL, m, y, W, Dlu(dlg, 8, false), true);
        y += Dlu(dlg, 11, false);
        Place(dlg, IDC_PATH, m, y, W - bw - gap, bh, true);
        Place(dlg, IDC_BROWSE, m + W - bw, y, bw, bh, true);
        y += bh;
    }
    else
    {
        ShowWindow(GetDlgItem(dlg, IDC_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_PATH), SW_HIDE);
        ShowWindow(GetDlgItem(dlg, IDC_BROWSE), SW_HIDE);
    }
    y += Dlu(dlg, 11, false);
    Place(dlg, IDOK, m + W - bw, y, bw, bh, true);
    Place(dlg, IDC_LEFT, m + W - bw - gap - bw, y, bw, bh, left != 0);
    y += bh + mv;

    RECT wr = { 0, 0, W + 2 * m, y };
    AdjustWindowRectEx(&wr, (DWORD)GetWindowLongW(dlg, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(dlg, GWL_EXSTYLE));
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    if (!g.placed)
    {
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(dlg, 0, work.left + (work.right - work.left - ww) / 2, work.top + (work.bottom - work.top - wh) / 2, ww, wh, SWP_NOZORDER);
        g.placed = true;
    }
    else SetWindowPos(dlg, 0, 0, 0, ww, wh, SWP_NOZORDER | SWP_NOMOVE);

    SendMessageW(dlg, DM_SETDEFID, IDOK, 0);
    SendMessageW(dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(dlg, IDOK), TRUE);
    InvalidateRect(dlg, 0, TRUE);
}

/* The folder the player chose: not Kenshi -> E; installed there -> C; else -> A. */
void UseFolder(HWND dlg, const std::wstring& f)
{
    g.folder = f;
    if (!HasGame(f)) Show(dlg, kScrNotFound);
    else Show(dlg, IsInstalled(f) ? kScrAlready : kScrInstall);
}

void Act(HWND dlg, Pending what)
{
    g.pending = what;
    HCURSOR prev = SetCursor(LoadCursor(0, IDC_WAIT));
    const Result r = (what == kDoInstall) ? Install(g.folder, g.selfDir, true) : Uninstall(g.folder, true);
    SetCursor(prev);
    if (r == kOk) Show(dlg, what == kDoInstall ? kScrInstalled : kScrRemoved);
    else if (r == kNotKenshi) Show(dlg, kScrNotFound);
    else if (r == kRunning) Show(dlg, kScrRunning);
    else Show(dlg, kScrNoWrite);
}

HWND MakeControl(HWND dlg, const wchar_t* cls, DWORD style, DWORD exStyle, int id)
{
    HWND c = CreateWindowExW(exStyle, cls, L"", WS_CHILD | style, 0, 0, 10, 10, dlg, (HMENU)(INT_PTR)id, GetModuleHandleW(0), 0);
    SendMessageW(c, WM_SETFONT, SendMessageW(dlg, WM_GETFONT, 0, 0), FALSE);
    return c;
}

INT_PTR CALLBACK DlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        /* the title-bar / taskbar icon: icon group 1 from setup_icon.res (make_icon.py), linked into this exe - the
           same resource Explorer shows as the file's icon.  A missing one only leaves the default icon. */
        HINSTANCE self = GetModuleHandleW(0);
        HANDLE iconBig = LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HANDLE iconSmall = LoadImageW(self, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (iconBig) SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)iconBig);
        if (iconSmall) SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
        MakeControl(dlg, L"STATIC", SS_LEFT | SS_NOPREFIX, 0, IDC_MSG);
        HWND label = MakeControl(dlg, L"STATIC", SS_LEFT | SS_NOPREFIX, 0, IDC_LABEL);
        SetWindowTextW(label, kLabelFolder);
        MakeControl(dlg, L"EDIT", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_PATH);
        HWND browse = MakeControl(dlg, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BROWSE);
        SetWindowTextW(browse, kBtnBrowse);
        MakeControl(dlg, L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_LEFT);
        MakeControl(dlg, L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDOK);
        g.folder = FindKenshi(g.selfDir);
        Show(dlg, (!g.folder.empty() && IsInstalled(g.folder)) ? kScrAlready : kScrInstall);
        return FALSE;   /* Show() placed the focus */
    }
    case WM_COMMAND:
    {
        const int id = LOWORD(wp);
        if (id == IDCANCEL) { EndDialog(dlg, 0); return TRUE; }   /* Esc, the title bar's close box */
        if (id == IDC_BROWSE)
        {
            std::wstring f;
            if (PickFolder(dlg, &f)) UseFolder(dlg, f);
            return TRUE;
        }
        if (id == IDC_LEFT)
        {
            if (g.screen == kScrAlready) Act(dlg, kDoUninstall);
            else EndDialog(dlg, 0);   /* Cancel */
            return TRUE;
        }
        if (id == IDOK)
        {
            switch (g.screen)
            {
            case kScrInstall:  Act(dlg, kDoInstall); break;
            case kScrRunning:  Act(dlg, g.pending); break;
            case kScrNotFound: { std::wstring f; if (PickFolder(dlg, &f)) UseFolder(dlg, f); } break;
            default:           EndDialog(dlg, 0); break;   /* Finish / Close */
            }
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(dlg, 0);
        return TRUE;
    }
    return FALSE;
}

/* The dialog template, built in memory (the toolchain has no resource compiler): a fixed-size, captioned,
   non-resizable modal frame in Segoe UI 9 - the Windows 7+ dialog font and size.  Controls are made in WM_INITDIALOG. */
std::vector<WORD> DialogTemplate()
{
    std::vector<WORD> t;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
    t.push_back(LOWORD(style)); t.push_back(HIWORD(style));
    t.push_back(0); t.push_back(0);                 /* extended style */
    t.push_back(0);                                 /* no template controls */
    t.push_back(0); t.push_back(0); t.push_back(200); t.push_back(80);
    t.push_back(0);                                 /* no menu */
    t.push_back(0);                                 /* the standard dialog class */
    for (const wchar_t* p = kTitle; *p; ++p) t.push_back((WORD)*p);
    t.push_back(0);
    t.push_back(9);                                 /* point size */
    for (const wchar_t* p = L"Segoe UI"; *p; ++p) t.push_back((WORD)*p);
    t.push_back(0);
    return t;
}

/* Themed buttons with no manifest resource (no resource compiler): activate shell32.dll's own Common Controls 6
   manifest (resource 124) around the window.  A failure only leaves the classic look. */
HANDLE ThemeContext(ULONG_PTR* cookie)
{
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return INVALID_HANDLE_VALUE;
    const std::wstring shell32 = Join(sys, L"shell32.dll");
    ACTCTXW ac;
    ZeroMemory(&ac, sizeof(ac));
    ac.cbSize = sizeof(ac);
    ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    ac.lpSource = shell32.c_str();
    ac.lpResourceName = MAKEINTRESOURCEW(124);
    HANDLE h = CreateActCtxW(&ac);
    if (h != INVALID_HANDLE_VALUE && !ActivateActCtx(h, cookie)) { ReleaseActCtx(h); h = INVALID_HANDLE_VALUE; }
    return h;
}

int TestMode(const std::wstring& verb, const std::wstring& folder, const std::wstring& selfDir)
{
    if (verb == L"/test-install") return (int)Install(folder, selfDir, true);
    if (verb == L"/test-uninstall") return (int)Uninstall(folder, true);
    /* the same two with the "Kenshi is running" check left out, so a scratch folder can be tested while a game runs elsewhere */
    if (verb == L"/test-install-skiprun") return (int)Install(folder, selfDir, false);
    if (verb == L"/test-uninstall-skiprun") return (int)Uninstall(folder, false);
    if (verb == L"/test-status") return IsInstalled(folder) ? 0 : 1;
    return 64;
}

}   /* anonymous namespace */

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    g.selfDir = SelfDir();
    g.screen = kScrInstall;
    g.pending = kDoInstall;
    g.placed = false;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc == 3 && argv[1][0] == L'/')
    {
        const int rc = TestMode(argv[1], argv[2], g.selfDir);
        LocalFree(argv);
        return rc;
    }
    if (argv) LocalFree(argv);

    SetProcessDPIAware();
    ULONG_PTR cookie = 0;
    HANDLE act = ThemeContext(&cookie);
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    OleInitialize(0);   /* the folder picker's new-style dialog needs OLE */
    const std::vector<WORD> tmpl = DialogTemplate();
    DialogBoxIndirectParamW(inst, (LPCDLGTEMPLATEW)&tmpl[0], 0, DlgProc, 0);
    OleUninitialize();
    if (act != INVALID_HANDLE_VALUE) { DeactivateActCtx(0, cookie); ReleaseActCtx(act); }
    return 0;
}
