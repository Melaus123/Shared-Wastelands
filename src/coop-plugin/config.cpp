// config.cpp - E38 / decisions 42 and 43. See config.h for what the role means.
//
// PARSING RULES, chosen so a hand-edited file can never do anything worse than leave this game single-player:
//  * at most 256 lines, each truncated to 512 characters - a file that is not a config file cannot make this
//    loop long or this process fat;
//  * `key = value`, `#` and `;` comments, keys lower-cased, surrounding whitespace trimmed;
//  * an unknown key is NAMED in the log and ignored (the notebook's own OPTIONS handler sets this precedent);
//  * a role that needs an address it did not get FALLS BACK TO SINGLE and says so loudly. Half a configuration
//    is worse than none: a client with no host address would sit declared-client with no link, and towngen
//    refuses every town-carrying creation in exactly that state (E18.5).
// Allocation is fine here - this is startPlugin and the command channel, never a detour.
#include "config.h"
#include "../common/names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "../common/datadir.h"   /* PP3: the data folder, the identity decision, player.cfg's text */
#include "../common/worlddir.h"   /* W2b: WorldFolderName / WorldOrDefault - a world switch is compared by folder name */
#include "../common/joinstage.h"   /* WorldDoorPick: the world server a game dials */
#include "../common/storelink.h"   /* TitleRedialDecide and the world-server link states (kSock*) */
#include "../common/areaclaim.h"   /* B13: coopstore::PlayerIdOk and kPlayerIdLen - the notebook refuses a malformed id, so the shape rule is read from ONE place */
#include "command_channel.h"   // PathNextToDll - the same directory the command channel already uses
#include "net/session.h"
#include "store.h"
#include "towngen.h"
#include "coop_log.h"
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <locale>
#include <string>
#include <cstdlib>

namespace {

// ANY THREAD. Written by ConfigLoad before a single hook exists, and by the command channel afterwards.
volatile long g_role = 0;   /* kRoleSingle */
bool g_fileSeen = false;
bool g_titleDone = false;
long long g_dials = 0;          /* a joining game's world-server dials at the title that went unanswered (bounded) */
/* The bound on those, which is what ends a JOIN with CAN'T JOIN. 12 dials of at most kTitleConnectBudgetMs each is about
   the minute the JOIN has always had before it gave up; a dial that fails at once (an address that does not resolve) is
   re-dialled once a second, so that case ends in about 12 s. */
const long long kDialCap = 12;
const DWORD kTitleRedialPaceMs    = 1000;   /* at most one dial a second */
const DWORD kTitleConnectBudgetMs = 5000;   /* a dial still connecting is left alone this long: ENet's own minimum timeout */
long long g_armGen = 0;         /* U2: how many times the arming below has actually run (0 = never) */
DWORD g_lastDialMs = 0;
std::string g_doorAddr; unsigned short g_doorPort = 0;   /* the world server the last arming rang ("" = none) - the title re-dial rings it again */
std::string g_hostAddr; unsigned short g_hostPort = 0;
std::string g_storeAddr; unsigned short g_storePort = 0;
std::string g_slot("s1");
std::string g_world;
std::string g_cfgSaveRoot, g_cfgStoreDir;   /* P8J-B2: saveroot= / storedir=; "" = absent */
std::string g_dataDir; int g_dataDirSource = 0;   /* PP3: the data folder, named at the first read */
std::string g_playerName;                   /* PP3: player.cfg's playername (the panel's Your name box) */
std::string g_lastAddr; unsigned short g_lastPort = 0;   /* PP3: player.cfg's host= - the last address/port used */
bool g_cfgRootsLatched = false;             /* P8J-B2: set by the first ConfigLoad; the two above never change after */
std::string g_playerId;                      /* B13: this install's stable name; empty until a settings file is read */
int g_profileTest = 0;                       /* prof1 TEST ONLY: profile= */
std::string g_slotProfile;                   /* prof1 fold (review-prof1 4): the picked profile's save folder - a re-arm keeps it over the file's slot= */
std::string g_slotFile;                      /* T-201 PP5: the settings' own slot= as last armed - what ConfigLeave puts back when the profile's folder goes */
int g_ownSaveCfg = 1;                        /* mmo1: ownsave= (T-251: default on) */
int g_joinViaWorld = 0;                      /* M11a S1 TEST ONLY: joinvia=world */
int g_routerPortCfg = 1;                     /* routerport= : 1 = the HOST press asks the router (the default) */
int g_modSalt = 0;                           /* settings5 S5 TEST ONLY: modfingerprint_salt= */
long long g_playerIdGenerated = 0, g_playerIdLoaded = 0;
long long g_unknownKeys = 0, g_badLines = 0, g_worldKeyMismatch = 0;
std::string g_remoteWorld;
std::string g_adoptedWorld;                 /* W3: the world this game follows in memory (ConfigAdoptWorld); empty = the file's */
long long g_worldAdoptedEarly = 0;          /* W3: adoptions made from the host's session WELCOME */

// RE_Kenshi's process-wide locale inserts digit-group separators into every number (F030).
std::string N(long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }

/* B13 - WHERE THE ID COMES FROM, AND WHY IT IS NOT THE WINDOWS CRYPTO. CryptGenRandom lives in
   advapi32, which this DLL does not link, and B13 does not add a library to a link line that is already
   the fussiest thing in this build (F031's /GL mandate, Ogre and MyGUI all sit on it). So the
   32 hex characters are built from the performance counter (which counts at the CPU's own rate and is
   different on every read), this process's id and the tick count, stirred by a 64-bit multiply-and-add
   step of the function's own (a fresh performance-counter read is mixed in every 8 characters); rand() is
   never called. That is not cryptographic and does not need to be: nothing is authorised by this id. It
   only has to be DIFFERENT on two installs that have never met, and it is written to a file ONCE - a second game on the
   same machine reads its own copy out of its own shared_wastelands.cfg rather than generating another.
   coopstore::PlayerIdOk is what says the result is well formed, and the offline suite sweeps that. */
std::string MakePlayerId()
{
    /* B13-b (review-b13, LOW): this no longer calls std::srand. Seeding the process-wide CRT generator from
       a plugin is a side effect on the WHOLE of Kenshi - anything else in the process that uses rand() would
       have its sequence replaced by ours - and nothing here needs rand(): the state below is its own. Nothing
       in this file calls rand() or srand(). */
    LARGE_INTEGER qpc; ::QueryPerformanceCounter(&qpc);
    unsigned long long mix = (unsigned long long)qpc.QuadPart;
    mix ^= ((unsigned long long)::GetCurrentProcessId() << 32) ^ (unsigned long long)::GetTickCount();
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(coopstore::kPlayerIdLen);
    for (size_t i = 0; i < coopstore::kPlayerIdLen; ++i)
    {
        if ((i & 7) == 0) { LARGE_INTEGER again; ::QueryPerformanceCounter(&again); mix ^= (unsigned long long)again.QuadPart * 0x9E3779B97F4A7C15ULL; }
        mix = mix * 6364136223846793005ULL + 1442695040888963407ULL;
        const unsigned nib = (unsigned)((mix >> 33) ^ (mix >> 51)) & 0xFu;
        out += kHex[nib];
    }
    return out;
}

/* B13-b (review-b13 M-2) - THE ID EXISTS EVEN WITH NO SETTINGS FILE. A game with no shared_wastelands.cfg
   returns out of ConfigReadFileInto before the generator ever ran, so its id stayed empty for the whole
   session: the HELLO carried nothing (the notebook refuses that), and the first thing the MULTIPLAYER panel
   saved wrote an empty playerid. The id is made in memory the first time anybody asks for it, whether or not
   there is a file to put it in; the writers below are what make it survive a restart. */
void EnsurePlayerId(const char* why)
{
    if (!g_playerId.empty()) return;
    g_playerId = MakePlayerId();
    ++g_playerIdGenerated;
    DebugLog(std::string("[CFG] B13: a player id was generated in memory (") + (why ? why : "?") + ") - '"
             + g_playerId + "'. The notebook names this game by it. It is written into player.cfg in the data"
             " folder (PP3) at once; until it is in that file, this game"
             " gets a new name - and therefore a new notebook slot and no restored areas - at every start."
             " Counted playerId[generated].");
}

/* PP3 - THE ONE ATOMIC WRITER: a temp file beside the target, then a rename over it (MOVEFILE_WRITE_THROUGH), so a reader
   sees the whole old file or the whole new one. The plugin writes player.cfg and session.cfg with it. It NEVER writes the
   DLL-side shared_wastelands.cfg any more: that file is the harness's TEST file (B13-b's in-place playerid edit is gone with it). */
bool WriteFileAtomic(const std::string& path, const std::string& body, std::string* err)
{
    char pidb[32]; sprintf(pidb, ".new.%lu", (unsigned long)::GetCurrentProcessId());
    const std::string tmpPath = path + pidb;   /* PP3b (review LOW): unique per process - two games never share one temp file */
    {
        HANDLE h = ::CreateFileA(tmpPath.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == INVALID_HANDLE_VALUE) { if (err) *err = "could not open " + tmpPath + " for writing"; return false; }
        DWORD wrote = 0;
        const BOOL wok = body.empty() ? TRUE : ::WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
        /* PP3b (review MED): the bytes reach the disk BEFORE the rename, so a power cut cannot leave a renamed-but-empty file. */
        const BOOL fok = ::FlushFileBuffers(h);
        ::CloseHandle(h);
        if (!wok || wrote != (DWORD)body.size() || !fok) { if (err) *err = "the write to " + tmpPath + " failed - the disk may be full"; ::DeleteFileA(tmpPath.c_str()); return false; }
    }
    if (!::MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD e = ::GetLastError();
        char nb[32]; sprintf(nb, "%lu", (unsigned long)e);
        if (err) *err = "could not replace " + path + " (Windows error " + nb + ")";
        ::DeleteFileA(tmpPath.c_str());
        return false;
    }
    return true;
}

/* PP3: every missing level of a folder, shortest first (CreateDirectoryA makes one level; an existing one is not an error). */
void EnsureDirTree(const std::string& dir)
{
    for (size_t i = 3; i <= dir.size(); ++i)
        if (i == dir.size() || dir[i] == '\\' || dir[i] == '/')
            ::CreateDirectoryA(dir.substr(0, i).c_str(), 0);
}

/* PP3: one settings file's text, bounded exactly as before (kCfgMaxReadBytes). false = no such file (or no path). */
bool ReadCfgTextAt(const std::string& path, std::string* text)
{
    text->clear();
    if (path.empty()) return false;
    std::ifstream f(path.c_str());
    if (!f) return false;
    std::vector<char> buf(4096);
    while (text->size() < coopcfg::kCfgMaxReadBytes && f)
    {
        f.read(&buf[0], (std::streamsize)buf.size());
        const std::streamsize got = f.gcount();
        if (got <= 0) break;
        text->append(&buf[0], (size_t)got);
    }
    if (text->size() > coopcfg::kCfgMaxReadBytes) text->erase(coopcfg::kCfgMaxReadBytes);
    return true;
}

std::string TestCfgPath() { return coop::PathNextToDll(swnames::kTestCfg); }   /* PP3: the DLL-side file is TEST-only now */
std::string DataFile(const char* name) { return g_dataDir.empty() ? std::string() : g_dataDir + "\\" + name; }

const char* RoleName(int r) { return r == coop::kRoleHost ? "host" : (r == coop::kRoleClient ? "client" : "single"); }
std::string AddrText(const std::string& a, unsigned short p) { return a.empty() || p == 0 ? std::string("none") : a + ":" + N((long long)p); }

/* P8i (U1): Trim, Lower and SplitAddr WERE HERE.  They are now CfgTrim / CfgLower / CfgSplitAddr in
   src/common/cfgtext.cpp, with their rules unchanged, and this file calls them.  They moved because the
   MULTIPLAYER panel has to refuse a bad <address>:<port> BEFORE it writes this file, and a second
   implementation of that rule inside ui.cpp is exactly the two-things-kept-in-step-by-hand shape 6a
   lesson 11 names.  The offline suite compiles that same translation unit and sweeps it, which it could
   never do while the rule lived in a file that includes Debug.h, session.h and store.h. */

/* The two role enums MUST hold the same three numbers - ConfigLoad assigns coopcfg's role straight into
   g_role.  A negative array size is a compile error, so they cannot drift apart unnoticed. */
typedef char CoopCfgRoleValuesAgree[
    (coopcfg::kCfgSingle == coop::kRoleSingle && coopcfg::kCfgHost == coop::kRoleHost
     && coopcfg::kCfgClient == coop::kRoleClient) ? 1 : -1];

void FallBackToSingle(const std::string& why)
{
    ::InterlockedExchange(&g_role, (long)coop::kRoleSingle);
    g_hostAddr.clear(); g_hostPort = 0; g_storeAddr.clear(); g_storePort = 0;
    ErrorLog("[CFG] shared_wastelands.cfg is incomplete: " + why + " - falling back to role=single. This game opens no link"
             " and behaves as unmodded Kenshi; half a configuration would leave it declared but unlinked, which is the"
             " state in which town generation refuses every town-carrying creation (E18.5).");
}

/* PP3 - THE DATA FOLDER, NAMED ONCE (the first read): datadir= from the TEST file wins (two instances on one PC share
   %LOCALAPPDATA% and would otherwise share one identity), else the default; see coopdata::DataDirChoose. */
void DataDirLatch(const std::string& overrideDir)
{
    g_dataDirSource = coopdata::DataDirChoose(overrideDir, getenv("LOCALAPPDATA"), getenv("USERPROFILE"), &g_dataDir);
    if (g_dataDir.empty())
        ErrorLog("[CFG] PP3: NO data folder - neither LOCALAPPDATA nor USERPROFILE is an absolute folder. This game's identity"
                 " lives in memory only this session (a new one at every start) and the panel's settings cannot be saved.");
    else
        DebugLog("[CFG] PP3: data folder '" + g_dataDir + "' (from " + coopdata::DataDirSourceName(g_dataDirSource)
                 + "): player.cfg = identity, session.cfg = the panel's settings, worlds\\ = the world server's data.");
}

bool g_identityNoWrite = false;   /* PP3b: player.cfg was present but unusable at start - it is NOT written this session */
int  g_identityKind = 0;          /* PP3d: coopdata::IdentitySessionOnlyKindOf at start; 0 again only by IdentityReplaceByChoice */

/* PP3b: a settings text's playerid when it is well formed (coopstore::PlayerIdOk), else "". */
std::string ValidIdIn(const std::string& text, coopcfg::CfgFields* out)
{
    coopcfg::CfgFields f;
    coopcfg::CfgParseText(text, &f, 0, 0, 0, 0);
    if (out) *out = f;
    return coopstore::PlayerIdOk(f.playerId) ? f.playerId : std::string();
}

/* PP3b: player.cfg's state (coopdata::PlayerCfgState). ABSENT only when Windows says there is no such file; a file that will
   not open is tried 5 times over ~1 s before it is called unreadable. */
int PlayerCfgProbe(const std::string& path, std::string* text)
{
    text->clear();
    if (path.empty()) return coopdata::kPcAbsent;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const DWORD a = ::GetFileAttributesA(path.c_str());
        if (a == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return coopdata::kPcAbsent;
        }
        else if ((a & FILE_ATTRIBUTE_DIRECTORY) == 0 && ReadCfgTextAt(path, text))
            return ValidIdIn(*text, 0).empty() ? coopdata::kPcInvalid : coopdata::kPcValid;
        if (attempt < 4) ::Sleep(250);
    }
    return coopdata::kPcUnreadable;
}

bool WritePlayerCfg(std::string* err)
{
    if (g_dataDir.empty()) { if (err) *err = "there is no data folder"; return false; }
    if (g_identityNoWrite) { if (err) *err = "player.cfg was present but unusable at start, so it is not written this session"; return false; }
    EnsureDirTree(g_dataDir);
    const std::string path = DataFile(coopdata::kPlayerCfg);
    /* PP3b (review 2026-09-27): every identity write first keeps the previous GOOD player.cfg as player.cfg.bak. */
    std::string prev;
    const std::string onDisk = ReadCfgTextAt(path, &prev) ? ValidIdIn(prev, 0) : std::string();
    /* PP3c (re-check 2026-09-27 LOW): a VALID id on disk that is not this game's is never written over - another game or a
       hand edit put it there since start, and replacing it would lose that identity. */
    if (!onDisk.empty() && onDisk != g_playerId)
    {
        ErrorLog("[CFG] !!! player.cfg NOT written: it holds a different valid id (playerid " + onDisk.substr(0, 8) + "...) than this"
                 " game's (playerid " + g_playerId.substr(0, 8) + "...) - it changed since Kenshi started. It is left exactly as it is;"
                 " restart Kenshi to use the id it holds.");
        if (err) *err = "player.cfg holds a different player id than this game's, so it was not written over";
        return false;
    }
    if (!onDisk.empty())
    {
        std::string berr;
        if (!WriteFileAtomic(path + ".bak", prev, &berr)) { if (err) *err = "player.cfg.bak could not be written first (" + berr + ")"; return false; }
    }
    return WriteFileAtomic(path, coopdata::PlayerCfgText(g_playerId, g_playerName, g_lastAddr, g_lastPort), err);
}

/* PP3 - THE IDENTITY, ONCE PER START (coopdata::IdentityDecide). player.cfg wins whenever it holds an id. Else a ONE-TIME
   COPY from the old shared_wastelands.cfg beside the DLL: written, read back and compared - the old file is never deleted or
   edited. Else a new id, written at once (a player id lost = their crews become a stranger's in every world). */
void IdentityLoad(const coopcfg::CfgFields& oldFields)
{
    const std::string path = DataFile(coopdata::kPlayerCfg);
    std::string text;
    coopcfg::CfgFields pf;
    /* PP3b (review 2026-09-27 HIGH): the FILE'S STATE first. Only an ABSENT player.cfg may lead to a new id. */
    const int state = PlayerCfgProbe(path, &text);
    if (state == coopdata::kPcValid || state == coopdata::kPcInvalid) ValidIdIn(text, &pf);
    std::string bakText;
    coopcfg::CfgFields bf;
    /* PP3c (re-check 2026-09-27 LOW): the .bak gets player.cfg's own ~1 s retry - a .bak held open for a moment is not "no .bak". */
    const int bakState = path.empty() ? coopdata::kPcAbsent : PlayerCfgProbe(path + ".bak", &bakText);
    const std::string bakId = bakState == coopdata::kPcValid ? ValidIdIn(bakText, &bf) : std::string();
    const std::string oldId = coopstore::PlayerIdOk(oldFields.playerId) ? oldFields.playerId : std::string();
    const coopdata::IdentityStartPlan plan = coopdata::IdentityPlanStart(state, state == coopdata::kPcValid ? pf.playerId : std::string(), bakState, bakId, oldId);
    const int d = plan.decision;
    g_identityKind = coopdata::IdentitySessionOnlyKindOf(state, bakState, plan);   /* PP3d; PP3e: a LOCKED .bak = kind 1, a legacy id restores */
    if (g_identityKind != 0)
        ErrorLog(std::string("[CFG] PP3d: this session's identity is ") + (g_identityKind == 1 ? "UNREADABLE" : "DAMAGED")
                 + " - the MULTIPLAYER panel refuses HOST and JOIN and shows the player a box instead"
                 + (g_identityKind == 2 ? " (its CONTINUE AS NEW PLAYER is the only way a new id is written)." : " (restart Kenshi once player.cfg can be read)."));
    bool asideFailed = false;
    std::string asideNote;
    if (plan.moveAside)
    {
        /* PP3c (re-check 2026-09-27 HIGH): readable but holding no valid id, AND a valid .bak restore is written in this same
           step - only then is it moved ASIDE (never deleted), so the rewrite below does not go over it. With no usable .bak it
           is NOT moved (session-only below): a moved file would read as ABSENT at the next start, which writes a NEW id. */
        SYSTEMTIME st; ::GetLocalTime(&st);
        char ts[48];
        sprintf(ts, ".bad-%04u%02u%02u-%02u%02u%02u", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        const std::string aside = path + ts;
        if (::MoveFileExA(path.c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH)) asideNote = " It was moved aside to " + aside + ".";
        else
        {
            asideFailed = true;
            char nb[32]; sprintf(nb, "%lu", (unsigned long)::GetLastError());
            asideNote = std::string(" It could NOT be moved aside (Windows error ") + nb + ") and is left exactly as it is.";
        }
    }
    const char* const whyBad = state == coopdata::kPcUnreadable ? "it exists but could not be read for ~1 s (another program may hold it)"
                             : "it holds no valid playerid (empty, damaged or hand-edited)";
    if (d == coopdata::kIdFromBak)
    {
        g_playerId = bakId; g_playerName = bf.playerName;
        if (coopdata::LastAddrRecordable(coopcfg::kCfgClient, bf.hostAddr)) { g_lastAddr = bf.hostAddr; g_lastPort = bf.hostPort; }
        ++g_playerIdLoaded;
        if (state != coopdata::kPcAbsent)
            ErrorLog(std::string("[CFG] !!! player.cfg is present but unusable - identity NOT replaced: ") + whyBad + "." + asideNote
                     + " player.cfg.bak holds this install's id (playerid " + g_playerId.substr(0, 8) + "...) and this session uses it.");
        if (!plan.write || asideFailed)
        {
            g_identityNoWrite = true;
            ErrorLog("[CFG] PP3b: player.cfg is NOT written this session (it is left as it is); restart Kenshi once it can be read.");
            return;
        }
        std::string werr;
        if (WritePlayerCfg(&werr))
            DebugLog("[CFG] data folder '" + g_dataDir + "': identity RESTORED from player.cfg.bak (playerid " + g_playerId.substr(0, 8) + "...) and player.cfg rewritten.");
        else
            ErrorLog("[CFG] data folder '" + g_dataDir + "': player.cfg could not be rewritten from player.cfg.bak (" + werr + "); this session uses the .bak's id and the next start tries again.");
        return;
    }
    if (d == coopdata::kIdSessionOnly)
    {
        g_identityNoWrite = true;
        g_playerName = pf.playerName;
        if (!oldId.empty()) { g_playerId = oldId; ++g_playerIdLoaded; }
        else EnsurePlayerId("player.cfg is present but unusable - a SESSION-ONLY id, never written");
        ErrorLog(std::string("[CFG] !!! player.cfg is present but unusable - identity NOT replaced: ") + whyBad + "." + asideNote
                 + (bakState == coopdata::kPcUnreadable ? " player.cfg.bak could not be read for ~1 s either (PP3e)." : " player.cfg.bak holds no valid id either.")
                 + " This session uses " + (oldId.empty() ? "a SESSION-ONLY id" : "the old shared_wastelands.cfg's id, in memory only")
                 + " (playerid " + g_playerId.substr(0, 8) + "...). player.cfg is left exactly where and as it is (not moved, not written),"
                 " so every start stays session-only until it is fixed. Restore player.cfg (or player.cfg.bak) and restart Kenshi.");
        return;
    }
    if (d == coopdata::kIdRead)
    {
        g_playerId = pf.playerId; g_playerName = pf.playerName; g_lastAddr = pf.hostAddr; g_lastPort = pf.hostPort;
        ++g_playerIdLoaded;
        std::string other;
        if (!oldFields.playerId.empty() && oldFields.playerId != g_playerId)
            other = " - the old shared_wastelands.cfg names another id (" + oldFields.playerId.substr(0, 8) + "...); player.cfg wins";
        DebugLog("[CFG] data folder '" + g_dataDir + "': identity read (playerid " + g_playerId.substr(0, 8) + "...)" + other);
        return;
    }
    if (d == coopdata::kIdMigrate)
    {
        g_playerId = oldFields.playerId;
        g_playerName = !pf.playerName.empty() ? pf.playerName : oldFields.playerName;
        if (coopdata::LastAddrRecordable(oldFields.role, oldFields.hostAddr)) { g_lastAddr = oldFields.hostAddr; g_lastPort = oldFields.hostPort; }   /* PP3b: only an address the player JOINED */
        ++g_playerIdLoaded;
        if (state == coopdata::kPcInvalid)
        {
            /* PP3e (review 2026-09-27 MED): a DAMAGED player.cfg with no usable .bak is RESTORED from the old shared_wastelands.cfg's valid
               id - moved aside above in this same step, exactly like the .bak restore. If it could not be moved it is not written over. */
            ErrorLog(std::string("[CFG] !!! player.cfg is present but unusable - identity NOT replaced: ") + whyBad + "." + asideNote
                     + " The old shared_wastelands.cfg holds a valid id (playerid " + g_playerId.substr(0, 8) + "...) and this session uses it.");
            if (asideFailed)
            {
                g_identityNoWrite = true;
                ErrorLog("[CFG] PP3e: player.cfg is NOT written this session (it is left as it is); restart Kenshi once it can be moved aside.");
                return;
            }
        }
        std::string werr;
        const bool wrote = WritePlayerCfg(&werr);
        bool verified = false;
        std::string back;
        if (wrote && ReadCfgTextAt(path, &back))
        {
            coopcfg::CfgFields bf;
            coopcfg::CfgParseText(back, &bf, 0, 0, 0, 0);
            verified = (bf.playerId == g_playerId);
        }
        if (verified)
            DebugLog("[CFG] data folder '" + g_dataDir + "': identity MIGRATED from the mod folder (playerid " + g_playerId.substr(0, 8)
                     + "...) - copied from " + TestCfgPath() + " into player.cfg and read back equal; the old file is left as it was.");
        else
            ErrorLog("[CFG] data folder '" + g_dataDir + "': the identity copy from the mod folder was NOT verified (playerid "
                     + g_playerId.substr(0, 8) + "...; " + (wrote ? std::string("player.cfg read back differently") : werr)
                     + "). This session uses the old id and the next start tries the copy again; the old file is left as it was.");
        return;
    }
    EnsurePlayerId("player.cfg is ABSENT, player.cfg.bak holds no id and the old shared_wastelands.cfg none");
    g_playerName = pf.playerName;
    std::string werr;
    if (WritePlayerCfg(&werr))
        DebugLog("[CFG] data folder '" + g_dataDir + "': NEW identity (playerid " + g_playerId.substr(0, 8) + "...) written to player.cfg.");
    else
        ErrorLog("[CFG] data folder '" + g_dataDir + "': a new identity could NOT be written (" + werr + "). This session uses it;"
                 " the next start makes another - a new notebook slot and no restored areas at every start until it can be written.");
}

}

namespace coop {

int         ConfigRole()     { return (int)::InterlockedCompareExchange(&g_role, 0, 0); }
bool        RoleIsSingle()   { return ConfigRole() == kRoleSingle; }
const char* ConfigRoleName() { return RoleName(ConfigRole()); }
bool        ConfigFileSeen() { return g_fileSeen; }
std::string    ConfigHostAddr()  { return g_hostAddr; }
unsigned short ConfigHostPort()  { return g_hostPort; }
std::string    ConfigStoreAddr() { return g_storeAddr; }
unsigned short ConfigStorePort() { return g_storePort; }
std::string    ConfigSlot()      { return g_slot; }
std::string    ConfigWorldKey()  { return g_world; }
std::string    ConfigSaveRoot()  { return g_cfgSaveRoot; }
std::string    ConfigStoreDir()  { return g_cfgStoreDir; }
std::string    ConfigDataDir()   { return g_dataDir; }   /* PP3 */
int            ConfigOwnSave()   { return g_ownSaveCfg; }   /* mmo1 */
int            ConfigRouterPort() { return g_routerPortCfg; }
int            ConfigModFingerprintSalt() { return g_modSalt; }
int            ConfigJoinViaWorld() { return g_joinViaWorld; }   /* M11a S1 */
int            ConfigProfileTest() { return g_profileTest; }
void           ConfigSetSlotForProfile(const std::string& slot) { g_slot = slot; g_slotProfile = slot; }
std::string    ConfigPlayerId()  { EnsurePlayerId("asked for before one existed"); return g_playerId; }

/* PP3d (owner-approved 2026-09-27) - 0 normal, 1 UNREADABLE, 2 DAMAGED. Set once by IdentityLoad. */
int IdentitySessionOnlyKind() { return g_identityKind; }

/* PP3d - CONTINUE AS NEW PLAYER, the ONLY path that replaces an identity (coopdata::IdentityMayReplace). An unusable
   player.cfg.bak is renamed to player.cfg.bak.bad-<time> FIRST, then the damaged player.cfg to player.cfg.bad-<time> (never
   deleted; if a rename fails nothing more is done and the identity stays DAMAGED). Then a new id is made and player.cfg and
   player.cfg.bak are written with it. 1 = replaced (the kind is 0 now); 0 = not (the log says why; HOST / JOIN stay refused).
   PP3e: 2 = RECOVERED at the click (a valid id found again in player.cfg or its .bak; nothing replaced, the kind is 0 now). */
int IdentityReplaceByChoice()
{
    if (!coopdata::IdentityMayReplace(g_identityKind, coopdata::kIdBoxContinue))
    {
        char kb[16]; sprintf(kb, "%d", g_identityKind);
        ErrorLog(std::string("[CFG] PP3d: CONTINUE AS NEW PLAYER refused - the identity is not DAMAGED (kind ") + kb + "); nothing replaced.");
        return 0;
    }
    if (g_dataDir.empty()) { ErrorLog("[CFG] PP3d: CONTINUE AS NEW PLAYER refused - there is no data folder; nothing replaced."); return 0; }
    const std::string path = DataFile(coopdata::kPlayerCfg);
    SYSTEMTIME st; ::GetLocalTime(&st);
    char ts[48];
    sprintf(ts, ".bad-%04u%02u%02u-%02u%02u%02u", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
    /* PP3e (review 2026-09-27 MED): BOTH FILES ARE READ AGAIN FIRST (coopdata::IdentityClickDecide). A file fixed - or a lock let
       go - since start is RECOVERED: its id is used, player.cfg written from it (its .bak kept), kind 0, and NOTHING is replaced. */
    {
        std::string cfgText, bakText;
        const int cfgState = PlayerCfgProbe(path, &cfgText);
        const int bakState = PlayerCfgProbe(path + ".bak", &bakText);
        const int act = coopdata::IdentityClickDecide(cfgState, bakState);
        if (act == coopdata::kClickRefuseUnreadable)
        {
            ErrorLog(std::string("[CFG] PP3e: CONTINUE AS NEW PLAYER refused - ") + (cfgState == coopdata::kPcUnreadable ? "player.cfg" : "player.cfg.bak")
                     + " could not be read for ~1 s (it may still hold the id); nothing replaced. HOST / JOIN stay refused.");
            return 0;
        }
        if (act == coopdata::kClickRecoverCfg || act == coopdata::kClickRecoverBak)
        {
            const bool fromCfg = act == coopdata::kClickRecoverCfg;
            coopcfg::CfgFields rf;
            g_playerId = ValidIdIn(fromCfg ? cfgText : bakText, &rf);
            if (!rf.playerName.empty()) g_playerName = rf.playerName;
            if (coopdata::LastAddrRecordable(coopcfg::kCfgClient, rf.hostAddr)) { g_lastAddr = rf.hostAddr; g_lastPort = rf.hostPort; }
            ++g_playerIdLoaded;
            std::string note;
            bool mayWrite = fromCfg || cfgState != coopdata::kPcUnreadable;
            if (!fromCfg && cfgState == coopdata::kPcInvalid)
            {
                const std::string aside = path + ts;
                if (::MoveFileExA(path.c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH)) note = " The damaged player.cfg was moved aside to " + aside + ".";
                else
                {
                    char nb[32]; sprintf(nb, "%lu", (unsigned long)::GetLastError());
                    note = std::string(" The damaged player.cfg could NOT be moved aside (Windows error ") + nb + ") and is left as it is.";
                    mayWrite = false;
                }
            }
            g_identityNoWrite = !mayWrite;
            std::string werr;
            if (!mayWrite) note += " player.cfg is not written this session.";
            else if (WritePlayerCfg(&werr)) note += " player.cfg written from it (its .bak kept).";
            else note += " player.cfg could not be written (" + werr + "); the next start tries again.";
            g_identityKind = 0;
            ErrorLog(std::string("[CFG] identity RECOVERED at the click (") + (fromCfg ? "player.cfg" : "player.cfg.bak") + ") - nothing replaced: playerid "
                     + g_playerId.substr(0, 8) + "...." + note + " HOST and JOIN work again.");
            return 2;
        }
    }
    std::string moved;
    const char* const suffix[2] = { ".bak", "" };
    for (int i = 0; i < 2; ++i)
    {
        const std::string p = path + suffix[i];
        if (::GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            /* PP3e (review 2026-09-27 LOW): only "no such file" is absent; any other error = present but unreadable, never written over. */
            const DWORD e = ::GetLastError();
            if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) continue;
            char nb[32]; sprintf(nb, "%lu", (unsigned long)e);
            ErrorLog("[CFG] PP3e: CONTINUE AS NEW PLAYER stopped - " + p + " could not be checked (Windows error " + nb + "), so it is not"
                     " treated as absent. Nothing was replaced" + (moved.empty() ? std::string() : " (already renamed:" + moved + ")")
                     + "; the identity stays DAMAGED and HOST / JOIN stay refused.");
            return 0;
        }
        const std::string aside = p + ts;
        if (!::MoveFileExA(p.c_str(), aside.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            char nb[32]; sprintf(nb, "%lu", (unsigned long)::GetLastError());
            ErrorLog("[CFG] PP3d: CONTINUE AS NEW PLAYER stopped - " + p + " could NOT be renamed aside (Windows error " + nb
                     + "). Nothing was replaced" + (moved.empty() ? std::string() : " (already renamed:" + moved + ")")
                     + "; the identity stays DAMAGED and HOST / JOIN stay refused.");
            return 0;
        }
        moved += " " + p + " -> " + aside;
    }
    const std::string before = g_playerId;
    g_playerId.clear();
    EnsurePlayerId("CONTINUE AS NEW PLAYER - the player chose a new identity");
    g_identityNoWrite = false;
    std::string werr, berr;
    if (!WritePlayerCfg(&werr))
    {
        g_identityNoWrite = true;
        ErrorLog("[CFG] PP3d: CONTINUE AS NEW PLAYER - player.cfg could NOT be written (" + werr + "). The identity stays DAMAGED"
                 " and HOST / JOIN stay refused; pressing it again tries again. Renamed aside:" + (moved.empty() ? std::string(" nothing") : moved) + ".");
        return 0;
    }
    const bool bakOk = WriteFileAtomic(path + ".bak", coopdata::PlayerCfgText(g_playerId, g_playerName, g_lastAddr, g_lastPort), &berr);
    g_identityKind = 0;
    ErrorLog("[CFG] identity REPLACED by the player's choice (CONTINUE AS NEW PLAYER) - new playerid " + g_playerId.substr(0, 8)
             + "... written to player.cfg" + (bakOk ? std::string(" and player.cfg.bak") : " (player.cfg.bak NOT written: " + berr + ")")
             + "; the session-only id " + (before.empty() ? std::string("(none)") : before.substr(0, 8) + "...") + " is dropped. Renamed aside:"
             + (moved.empty() ? std::string(" nothing (player.cfg was already gone)") : moved) + ". HOST and JOIN work again.");
    return 1;
}
long long      ConfigPlayerIdGenerated() { return g_playerIdGenerated; }
long long      ConfigPlayerIdLoaded()    { return g_playerIdLoaded; }

/* W3 (decisions 58/59; M4, M5) - THIS GAME FOLLOWS ANOTHER WORLD, IN MEMORY ONLY. g_world is what ConfigWorldKey hands the
   store HELLO, the record-index folder, the save's world key and the load gate, so setting it here is the whole adoption.
   shared_wastelands.cfg is NOT written (M5): the next start reads the file's world again, and a client follows the notebook again
   at its WELCOME. g_adoptedWorld is what a panel re-arm keeps for every co-op role (ConfigReadFileInto; W3-f).
   W3-f (review-w3 item 7): g_adoptedFromNotebook says the adoption was the notebook's own WELCOME, not the host's early hint. */
static bool g_adoptedFromNotebook = false;
bool ConfigAdoptWorld(const std::string& name, const char* why)
{
    const std::string before = g_world;
    /* W3-g (recheck-w3f item 4): THE SETTINGS FILE'S OWN WORLD IS CONFIRMED, NOT ADOPTED. A name whose folder is the folder of
       the world shared_wastelands.cfg names (no world= line = the default world) is that file's world: this game follows it and the
       adopted flag is cleared, so WorldKeyMarkSlot marks the settings' slot with it. Reached when this game followed another
       world first (the host's early hint) and the notebook then names the file's own. Returns false then, true = adopted. */
    const std::string fileWorld = ConfigFileWorld();
    if (coopworld::WorldFolderName(name) == coopworld::WorldFolderName(coopworld::WorldOrDefault(fileWorld, 0)))
    {
        g_world = name; g_adoptedWorld.clear(); g_adoptedFromNotebook = false;
        DebugLog("[CFG] W3-g: world '" + name + "' (" + std::string(why ? why : "?") + ") is the world shared_wastelands.cfg names ('"
                 + (fileWorld.empty() ? std::string("<none> - the default, coop") : fileWorld) + "') - confirmed, not adopted;"
                 " this game follows it and its save slot is marked with it.");
        return false;
    }
    g_world = name; g_adoptedWorld = name; g_adoptedFromNotebook = true;
    DebugLog("[CFG] W3: this game now follows world '" + name + "' (" + std::string(why ? why : "?") + "); shared_wastelands.cfg names '"
             + (before.empty() ? std::string("<none> - the default, coop") : before) + "' and is not changed - the adopted"
             " world lasts until Kenshi is closed.");
    return true;
}
std::string ConfigAdoptedWorld()      { return g_adoptedWorld; }
long long   ConfigWorldAdoptedEarly() { return g_worldAdoptedEarly; }

/* W3: THE HOST'S SESSION WELCOME IS AN EARLY HINT. A client whose world differs adopts the host's world here, so its store
   HELLO already names it; the notebook's own WELCOME still decides (M4 - StoreWelcomeWorld in store.cpp). An empty cfg
   world is the default world, coop. */
void ConfigNoteRemoteWorldKey(const std::string& key)
{
    g_remoteWorld = key;
    if (key.empty() || coopworld::WorldNamesCollide(coopworld::WorldOrDefault(g_world, 0), key)) return;
    ++g_worldKeyMismatch;
    /* W3-f (review-w3 item 7, M4): THE NOTEBOOK'S NAME WINS. A game that already follows the world the notebook's own WELCOME
       named keeps it when the host's session WELCOME names another - nothing is wrong on this side and no restart is asked. */
    if (g_adoptedFromNotebook && !g_adoptedWorld.empty())
    {
        DebugLog("[CFG] W3-f: the host's session welcome names world '" + key + "'; this game keeps world '" + g_world
                 + "', which the notebook's own WELCOME named - the notebook's name wins over the host's (M4). Nothing to do"
                 " here: the host follows the notebook's world too once its own notebook link is welcomed.");
        return;
    }
    const int d = (ConfigRole() == kRoleClient) ? coopworld::WelcomeWorldDecide(g_world, key, StoreIndexWorld()) : (int)coopworld::kWelcomeWorldNone;
    if (d == coopworld::kWelcomeWorldAdopt)
    {
        ++g_worldAdoptedEarly;
        ConfigAdoptWorld(key, "the host's session WELCOME, early - the notebook's WELCOME still decides");
        g_adoptedFromNotebook = false;   /* W3-f: an early hint, not the notebook's word */
        return;
    }
    const std::string mine = g_world.empty() ? std::string("<none> - the default, coop") : g_world;
    ErrorLog("[CFG] WORLD KEY MISMATCH: the host's session welcome names world '" + key + "', this game's world is '" + mine + "'. "
             + (d == coopworld::kWelcomeWorldRefuse
                ? "This game already opened world '" + StoreIndexWorld() + "'; restart Kenshi to join world '" + key + "'."
                : std::string("It is not adopted here (") + (ConfigRole() == kRoleClient ? "the name cannot be a world folder" : "this game is not a client")
                  + "); the notebook's WELCOME names the world this game follows."));
}

/* PP3 - THE SETTINGS TEXT: session.cfg (the panel's, in the data folder) then the DLL-side shared_wastelands.cfg (the harness's
   TEST keys), parsed as ONE text so a key the test file names wins and a key it leaves out does not (coopdata::
   LayeredSettingsText). Returns how many of the two files exist; nothing is committed, logged or counted. */
static int ConfigSettingsText(std::string* layered)
{
    std::string s, tx;
    const bool hs = ReadCfgTextAt(DataFile(coopdata::kSessionCfg), &s);
    const bool ht = ReadCfgTextAt(TestCfgPath(), &tx);
    if (layered) *layered = coopdata::ArmSettingsText(false, s, tx);   /* PP3b: the panel-press layering; an old panel-written test file counts as none */
    return (hs ? 1 : 0) + ((ht && coopdata::TestCfgKindOf(tx) != coopdata::kTestPanelOld) ? 1 : 0);
}

/* mp1 / W2-f: THE FIELDS, read and parsed exactly as ConfigReadFileInto does and committed NOWHERE. 0 = neither file exists. */
static int ConfigPeekFileFields(coopcfg::CfgFields* out)
{
    std::string text;
    if (ConfigSettingsText(&text) == 0) return 0;
    coopcfg::CfgParseText(text, out, 0, 0, 0, 0);
    return 1;
}
static int ConfigPeekFileWorld(std::string* worldOut, int* roleOut = 0)
{
    coopcfg::CfgFields fields;
    if (ConfigPeekFileFields(&fields) == 0) return 0;
    if (worldOut) *worldOut = fields.world;
    if (roleOut) *roleOut = fields.role;
    return 1;
}

/* W3-f (review-w3 item 4, M5): THE WORLD THE SETTINGS NAME, never an adopted one - the MULTIPLAYER panel fills its world
   box from this. "" = none, or no file. MAIN THREAD. */
std::string ConfigFileWorld() { std::string w; ConfigPeekFileWorld(&w); return w; }
/* PP3: the name lives in player.cfg (read at start, kept here); only a CHANGED name rewrites that file. */
std::string ConfigFilePlayerName() { return g_playerName; }
/* PP3b: what the panel's boxes start from - session.cfg's own fields, read live; it never arms anything at start. false = no
   session.cfg. MAIN THREAD. */
bool ConfigPanelPrefill(coopcfg::CfgFields* out)
{
    std::string s;
    if (!ReadCfgTextAt(DataFile(coopdata::kSessionCfg), &s)) return false;
    coopcfg::CfgParseText(s, out, 0, 0, 0, 0);
    return true;
}
/* PP3b: the last address this player JOINED (player.cfg's host=) as <address>:<port>; "" = none (never 0.0.0.0). */
std::string ConfigLastJoinAddr()
{
    if (g_lastAddr.empty() || g_lastPort == 0 || g_lastAddr == "0.0.0.0") return std::string();
    return g_lastAddr + ":" + N((long long)g_lastPort);
}
bool ConfigWritePlayerName(const std::string& name, std::string* err)
{
    if (g_playerName == name) return true;
    const std::string was = g_playerName;
    g_playerName = name;
    std::string werr;
    if (!WritePlayerCfg(&werr))
    {
        g_playerName = was;
        if (err) *err = "could not save your name: " + werr + ".";
        ErrorLog("[CFG] PP3: the player name could not be written to player.cfg (" + werr + ").");
        return false;
    }
    return true;
}

/* U2: THE READ, MADE CALLABLE TWICE.  This is ConfigLoad's whole body, unchanged in what it parses and
   what it logs; the only difference is that it now RETURNS whether the file named a complete role, and
   ConfigLoad and ConfigRearmFromFile are the two callers.  A second reader beside it would be the
   two-things-kept-in-step-by-hand shape 6a lesson 11 names, and the thing kept in step would be the
   meaning of the settings file. */
static int ConfigReadFileInto(std::string* whyOut)
{
    /* PP3: THE TEST FILE FIRST. The DLL-side shared_wastelands.cfg names the data folder (datadir=) and the bench folders, and its
       playerid is the one-time migration source; then session.cfg + the test file, layered, is what this game arms from. */
    const std::string testPath = TestCfgPath();
    std::string testText;
    const bool haveTest = ReadCfgTextAt(testPath, &testText);
    /* PP3b (review 2026-09-27 MED): an OLD panel-written shared_wastelands.cfg is ignored for everything except the one-time
       identity copy - its old keys would otherwise override every panel save (coopdata::TestCfgKindOf). */
    const bool testPanelOld = haveTest && coopdata::TestCfgKindOf(testText) == coopdata::kTestPanelOld;
    coopcfg::CfgFields tIdent;
    if (testPanelOld)
    {
        coopcfg::CfgParseText(testText, &tIdent, 0, 0, 0, 0);
        if (!g_cfgRootsLatched)
            DebugLog("[CFG] PP3b: " + testPath + " was written by an old MULTIPLAYER panel - it is IGNORED for everything except the"
                     " one-time identity copy (its playerid); the panel's settings live in session.cfg now.");
        testText.clear();
    }
    const bool haveTestEff = haveTest && !testPanelOld;
    coopcfg::CfgFields tf;
    std::vector<coopcfg::CfgNote> notes;
    int lines = 0;
    long long unknown = 0, badl = 0;
    coopcfg::CfgParseText(testText, &tf, &notes, &lines, &unknown, &badl);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const std::string msg = "[CFG] shared_wastelands.cfg (TEST file) line " + N((long long)notes[i].line) + ": " + notes[i].text;
        if (notes[i].bad) ErrorLog(msg); else DebugLog(msg);
    }
    g_unknownKeys += unknown;
    g_badLines    += badl;
    /* P8J-B2 (bench 2, F738) + PP3: the folder overrides and the data folder are taken at the FIRST read only. The store names
       its folders from them once; a later re-read (the panel's re-arm) that named others would split one game's files across
       two folders, so it is logged and waits for the next start. The identity is settled here, once. */
    if (!g_cfgRootsLatched)
    {
        g_cfgSaveRoot = tf.saveRoot; g_cfgStoreDir = tf.storeDir;
        if (!g_cfgSaveRoot.empty() || !g_cfgStoreDir.empty() || !tf.dataDir.empty())
            DebugLog("[CFG] P8j: folder overrides from " + testPath + ": saveroot='" + g_cfgSaveRoot + "' storedir='" + g_cfgStoreDir
                     + "' datadir='" + tf.dataDir + "' (empty = the default: the save folder is %LOCALAPPDATA%\\kenshi\\save, the data"
                       " folder is %LOCALAPPDATA%" + std::string(swnames::kDataSub) + " and the notebook's top folder is <data folder>\\worlds).");
        DataDirLatch(tf.dataDir);
        IdentityLoad(testPanelOld ? tIdent : tf);
    }
    else if (tf.saveRoot != g_cfgSaveRoot || tf.storeDir != g_cfgStoreDir)
        DebugLog("[CFG] P8j: " + testPath + " now names saveroot='" + tf.saveRoot + "' storedir='" + tf.storeDir
                 + "'; this game keeps saveroot='" + g_cfgSaveRoot + "' storedir='" + g_cfgStoreDir
                 + "' until Kenshi is restarted - its folders were named from them at start.");

    const std::string sessPath = DataFile(coopdata::kSessionCfg);
    std::string sessText;
    const bool sessOnDisk = ReadCfgTextAt(sessPath, &sessText);
    /* PP3b (manager decision 2026-09-27): session.cfg only PRE-FILLS the panel (last world, address, port; the name is
       player.cfg's). At the FIRST read it arms nothing - only the harness's test file can arm at start; a connection starts
       only from the panel, whose press re-reads session.cfg here (ConfigRearmFromFile). */
    const bool atStart = !g_cfgRootsLatched;
    if (atStart && sessOnDisk)
    {
        DebugLog("[CFG] session.cfg: panel pre-filled (no connection armed at start) - " + sessPath);
        sessText.clear();
    }
    const bool haveSess = sessOnDisk && !atStart;
    if (!haveSess && !haveTestEff)
    {
        /* DECISION 44: "no file" alone is NOT the single-player test any more - "no file AND no key" is. A save
           that has ever been part of a co-op world carries that world's key in multiplayer-world.key, and store.cpp's
           gate on SaveManager::loadGame refuses to load a keyed save with no notebook link. */
        DebugLog("[CFG] no settings: " + std::string(sessOnDisk ? "session.cfg: panel pre-filled, it arms nothing at start" : "no session.cfg in the data folder")
                 + " ('" + sessPath + "') and no shared_wastelands.cfg beside the DLL ("
                 + testPath + ") - role=single. No link of any kind is"
                 " opened, the store's load override and its notebook-people-pending refusal are disabled, the"
                 " record index is not read, and shop restocking runs the engine's own call: a lone game is plain"
                 " Kenshi (decision 43) - for every save that was never co-op. A save carrying multiplayer-world.key is"
                 " refused here (decision 44): no file AND no key is what single-player means.");
        if (whyOut) *whyOut = "there is no settings file at " + (sessPath.empty() ? testPath : sessPath) + " to read.";
        return 0;
    }
    g_fileSeen = true;
    const std::string path = haveSess ? (haveTestEff ? sessPath + " + TEST " + testPath : sessPath) : testPath;
    {
        std::vector<coopcfg::CfgNote> sn;
        int sl = 0;
        long long su = 0, sb = 0;
        coopcfg::CfgParseText(sessText, 0, &sn, &sl, &su, &sb);
        for (size_t i = 0; i < sn.size(); ++i)
        {
            const std::string msg = "[CFG] session.cfg line " + N((long long)sn[i].line) + ": " + sn[i].text;
            if (sn[i].bad) ErrorLog(msg); else DebugLog(msg);
        }
        g_unknownKeys += su;
        g_badLines    += sb;
        lines += sl;
    }
    coopcfg::CfgFields fields;
    coopcfg::CfgParseText(coopdata::ArmSettingsText(atStart, sessText, testText), &fields, 0, 0, 0, 0);   /* PP3b */
    g_hostAddr  = fields.hostAddr;  g_hostPort  = fields.hostPort;
    g_storeAddr = fields.storeAddr; g_storePort = fields.storePort;
    g_slotFile  = fields.slot;
    g_slot      = g_slotProfile.empty() ? fields.slot : g_slotProfile;   /* prof1 fold: a picked profile's folder survives a re-arm */
    g_world     = fields.world;
    /* W3 (decisions 58/59; M4): a game that ADOPTED the notebook's world keeps it over the file's when the file is re-read
       (a panel re-arm) - so the W2-f world-switch refusal in ConfigRearmFromFile compares the adopted world. W3-f (review-w3
       item 6): every co-op role, the host too. */
    {
        const std::string kept = coopworld::WorldForRole(fields.world, g_adoptedWorld);
        if (kept != fields.world)
        {
            g_world = kept;
            DebugLog("[CFG] W3: shared_wastelands.cfg names world '" + fields.world + "' and this game keeps world '" + kept
                     + "', which it adopted from the notebook's WELCOME - the notebook's name wins until Kenshi is closed.");
        }
    }
    if (fields.modSalt != 0 && g_modSalt == 0)
        DebugLog(std::string("[CFG] settings5 S5 TEST ONLY: modfingerprint_salt=1 - a made-up mod is added to this game's mod list,"
                             " so the notebook refuses it at join (a test of the refusal)"));
    if (fields.profileTest != 0 && fields.profileTest != g_profileTest)
    {
        char pb[160]; _snprintf(pb, 159, "[CFG] prof1 TEST ONLY: profile=%d - this game picks that profile in the world it joins, and asks the world to make it when it is missing", fields.profileTest); pb[159] = 0;
        DebugLog(pb);
    }
    g_profileTest = fields.profileTest;
    g_modSalt   = fields.modSalt;
    g_ownSaveCfg = fields.ownSave;   /* mmo1 */
    if (fields.routerPort != g_routerPortCfg)
        DebugLog(std::string("[CFG] routerport=") + (fields.routerPort ? "1 - the HOST press asks the home router to forward the world server's port"
                                                                      : "0 - the HOST press does not ask the home router"));
    g_routerPortCfg = fields.routerPort;
    if (fields.joinViaWorld != g_joinViaWorld && fields.joinViaWorld != 0)
        DebugLog("[CFG] joinvia= is not read: every joining game rings the world server alone");
    g_joinViaWorld = fields.joinViaWorld;   /* M11a S1 */
    const int wanted = fields.role;

    ::InterlockedExchange(&g_role, (long)wanted);
    if (atStart && haveTestEff && wanted != kRoleSingle)   /* T-202: only the harness's test file can do this */
        DebugLog("[CFG] a TEST settings file armed this game at start (role=" + std::string(RoleName(wanted)) + ") - a player's game never does this");
    DebugLog("[CFG] settings read from " + path + ": role=" + std::string(RoleName(wanted))
             + " host=" + AddrText(g_hostAddr, g_hostPort) + " store=" + AddrText(g_storeAddr, g_storePort)
             + " slot='" + g_slot + "' world='" + g_world + "' playerId='" + g_playerId + "' (" + N(lines)
             + " lines, " + N(g_unknownKeys) + " unknown keys, " + N(g_badLines) + " bad lines)");
    // A role that cannot reach anybody is not that role. `host` needs the port to bind, `client` the whole address.
    // P8i: the TEST is coopcfg::CfgIncompleteReason, the same one the panel refuses on, so the file's rule and the
    // panel's rule are one rule. The sentences below are unchanged.
    const int incomplete = coopcfg::CfgIncompleteReason(fields);
    if (incomplete == 1) FallBackToSingle("role=" + std::string(RoleName(wanted)) + " needs a `host=<address>:<port>` line and there is none");
    else if (incomplete == 2) FallBackToSingle("role=client needs the host's address in `host=<address>:<port>`");
    if (incomplete != 0 && whyOut)
        *whyOut = "the settings just written do not name everything that role needs, so this game stays"
                  " single-player. The [CFG] line in the plugin log says which part is missing.";
    return incomplete == 0 ? 1 : 0;
}

void ConfigLoad()
{
    ConfigReadFileInto(0);
    g_cfgRootsLatched = true;   /* P8J-B2: whatever the first read found (a missing file = both absent) is final */
}

/* W2b, made askable (T-201 N1): the panel asks it BEFORE writing session.cfg, ConfigRearmFromFile of the file just written.
   W3 / W3-f (item 6): the world compared is the ADOPTED one when there is one, for every co-op role (coopworld::WorldForRole). */
bool ConfigWorldSwitchRefused(const std::string& fileWorld, const char* why, std::string* err)
{
    const std::string indexWorld = StoreIndexWorld();
    if (indexWorld.empty()) return false;
    if (coopworld::WorldFolderName(coopworld::WorldOrDefault(coopworld::WorldForRole(fileWorld, g_adoptedWorld), 0)) == indexWorld) return false;
    const std::string wanted = fileWorld.empty() ? std::string(coopworld::kDefaultWorld) : fileWorld;
    StoreNoteWorldSwitchRefused();
    if (err) *err = "This game already opened world \"" + indexWorld + "\". Restart Kenshi to play world \"" + wanted + "\".";
    ErrorLog(std::string("[CFG] W2b re-arm by ") + (why ? why : "?") + " REFUSED: the settings name world '" + wanted
             + "' but this game already read world '" + indexWorld + "'s record index - a restart is needed to change world.");
    return true;
}

/* U2 - THE SECOND TRIGGER FOR THE ONE ARMING PATH.  See config.h for why this re-reads the file rather
   than taking arguments: the statics and the file cannot then disagree, which is F725 R4's hazard.
   MAIN THREAD (the title pump), called from ui.cpp inside the UI's own fault guard.  It opens NO socket
   itself - it clears the latch and the next ConfigTitleTick does the work, on the same pump, in the same
   order, through the same code F574 measured. */
bool ConfigRearmFromFile(const char* why, std::string* err)
{
    /* W2b (decision 58; design-worlds section 1) - ONE WORLD PER GAME START. Once the record index of a world is
       read, this process's records, mirror and queue belong to that world's folder; another world needs a fresh
       start. W2-f (review-w2 C): PARSE FIRST - the file's world is read into locals and compared BEFORE anything is
       committed, so a refused re-arm leaves every setting of this process exactly as it was (it used to write them
       all and put only the world name back). The panel shows *err to the player. */
    {
        std::string fileWorld;
        if (ConfigPeekFileWorld(&fileWorld) && ConfigWorldSwitchRefused(fileWorld, why, err)) return false;   /* T-201 N1: one test, shared with the panel */
    }
    const int before = ConfigRole();
    const int ok = ConfigReadFileInto(err);
    const int now = ConfigRole();

    /* ConfigReadFileInto has already written g_role directly, exactly as startup does, so calling
       ConfigSetRole here would compare equal and log nothing.  What ConfigSetRole does on the
       single -> other EDGE is still owed, and it is one call: the store names the folders of this game's world
       (W3-f: the record index is read, and the folders created, at the notebook's WELCOME - decision 43). */
    if (before == kRoleSingle && now != kRoleSingle) StoreRoleLeftSingle();

    g_titleDone = false;
    g_dials = 0;
    g_lastDialMs = 0;

    DebugLog(std::string("[CFG] U2 re-arm requested by ") + (why ? why : "?") + ": role="
             + RoleName(now) + " host=" + AddrText(g_hostAddr, g_hostPort)
             + " store=" + AddrText(g_storeAddr, g_storePort) + " slot='" + g_slot + "' world='" + g_world
             + "' - the settings file has been re-read and the title-screen arming latch is cleared, so the"
             " NEXT title tick opens the links through the same path a file-configured game uses. Nothing"
             " has been opened by this call itself.");

    if (ok == 0) return false;
    if (now == kRoleSingle)
    {
        if (err && err->empty()) *err = "the settings name role=single, which opens no link at all.";
        return false;
    }
    return true;
}

long long   ConfigArmGen()            { return g_armGen; }
long long   ConfigDialCount()         { return g_dials; }
long long   ConfigDialCap()           { return kDialCap; }
long long   ConfigWorldKeyMismatches(){ return g_worldKeyMismatch; }
std::string ConfigRemoteWorldKey()    { return g_remoteWorld; }

void ConfigTitleTick()
{
    const int role = ConfigRole();
    if (role == kRoleSingle) return;   /* role=single arms nothing, and says nothing every frame either */

    if (!g_titleDone)
    {
        g_titleDone = true;
        ++g_armGen;                 /* U2: the arming EVENT, which is what the panel tests before offering to arm again */
        DebugLog("[STORE] E38 link at title: role=" + std::string(RoleName(role)) + " host=" + AddrText(g_hostAddr, g_hostPort)
                 + " store=" + AddrText(g_storeAddr, g_storePort) + " slot='" + g_slot + "' world='" + g_world
                 + "' - this is the whole point of decision 42: both links come up here, at the front end, so the world"
                 " load can WAIT for the notebook's first map instead of answering its first town gate without one.");
        /* THE WORLD SERVER IS EVERY GAME'S ONE DOOR: no session host is opened and no session is dialled. A host rings the world server
           its own settings name (store= - the panel's HOST starts it on the PORT box's number); a joining game rings store= when the
           settings name one, else the address the player typed (host=). A JOIN is done at the world server's WELCOME (or lobby
           answer), and its load waits for the join gate (store.cpp StoreJoinGateRefuses). */
        const bool haveStore = !g_storeAddr.empty() && g_storePort != 0;
        const int door = coopjoin::WorldDoorPick(role == kRoleClient, haveStore, !g_hostAddr.empty() && g_hostPort != 0);
        g_doorAddr.clear(); g_doorPort = 0;
        if (door == coopjoin::kDoorStore) { g_doorAddr = g_storeAddr; g_doorPort = g_storePort; }
        else if (door == coopjoin::kDoorTyped) { g_doorAddr = g_hostAddr; g_doorPort = g_hostPort; }
        if (!g_doorAddr.empty()) SetStoreServer(g_doorAddr, g_doorPort);
        if (door == coopjoin::kDoorNone)
            ErrorLog("[CFG] world road: role=" + std::string(RoleName(role)) + " but shared_wastelands.cfg names no world server to ring - this game joins nothing.");
        else
            DebugLog("[CFG] world road: role=" + std::string(RoleName(role)) + " - this game rings the WORLD SERVER at "
                     + (door == coopjoin::kDoorStore ? AddrText(g_storeAddr, g_storePort) : AddrText(g_hostAddr, g_hostPort)) + " only.");
        g_lastDialMs = ::GetTickCount();
        return;
    }

    /* A JOINING GAME RINGS THE WORLD SERVER AGAIN UNTIL IT ANSWERS. store.cpp StoreRedialTick re-dials only while a world runs, so at
       the title this is the one retry: a JOIN pressed before the host's world server listens, or a connect that failed, is dialled
       again, at most once a second, until the world server answers (its link is up - welcomed, in its profile lobby or refusing in
       words) or kDialCap dials have gone unanswered - and then the JOIN press shows CAN'T JOIN (coopui::PressVerdictOf). It
       stops as well when the player cancels or a failure leaves (ConfigLeave: role single, this tick returns above) and when the
       world loads (this tick rides the title pump only). The decision is coopstore::TitleRedialDecide (pure, swept offline). */
    const DWORD now = ::GetTickCount();
    const int act = coopstore::TitleRedialDecide(role == kRoleClient ? 1 : 0, (!g_doorAddr.empty() && g_doorPort != 0) ? 1 : 0,
                                                 StoreLinkSockState(), g_dials, kDialCap, (unsigned int)(now - g_lastDialMs),
                                                 (unsigned int)kTitleRedialPaceMs, (unsigned int)kTitleConnectBudgetMs);
    if (act != coopstore::kTitleRedialAgain && act != coopstore::kTitleRedialLast) return;
    ++g_dials;
    if (act == coopstore::kTitleRedialLast)
    {
        ErrorLog("[CFG] world road: the world server at " + AddrText(g_doorAddr, g_doorPort) + " did not answer " + N(g_dials)
                 + " dials - no more are made; a JOIN press now shows CAN'T JOIN.");
        return;
    }
    g_lastDialMs = now;
    if (g_dials == 1)
        DebugLog("[CFG] world road: the world server at " + AddrText(g_doorAddr, g_doorPort) + " has not answered - dialling it again"
                 " at most once a second, up to " + N(kDialCap) + " unanswered dials (the rest are silent; the last one is said).");
    RedialClearAndDial(g_doorAddr, g_doorPort, 1);
}

/* T-201 N1 - LEAVE WITHOUT A RESTART (config.h). */
void ConfigLeave(const char* why)
{
    const int was = ConfigRole();
    const bool linked = net::SessionLinked();
    net::SessionLeave();                    /* BYE on a live link; "[net] session closed" when there was a session */
    SetStoreServer(std::string(), 0);       /* the store link closed and its re-dial target cleared */
    StoreLeftAtTitle();                     /* the last link's refusal and profile lobby go with it - and the profile's save folder (T-201 PP5) */
    g_slotProfile.clear(); g_slot = g_slotFile;   /* T-201 PP5: the picked profile's folder goes with the session; the next pick decides it again */
    ::InterlockedExchange(&g_role, (long)kRoleSingle);
    g_titleDone = false;
    g_dials = 0;
    g_lastDialMs = 0;
    DebugLog(std::string("[CFG] T-201 N1: LEFT (") + (why ? why : "?") + "): role " + RoleName(was) + " -> single, the session "
             + (linked ? "was linked and is closed (BYE sent)" : "is closed") + ", the store link is closed, the title-screen latch and the"
             " dial count are reset - the next HOST / JOIN press arms from nothing, no restart. A world server this game started is stopped by"
             " the HOST's leave (ui.cpp HostLeave), not here.");
}

void ConfigSetRole(int role, const char* why)
{
    const long was = ::InterlockedExchange(&g_role, (long)role);
    if (was == (long)role) return;
    DebugLog(std::string("[CFG] role ") + RoleName((int)was) + " -> " + RoleName(role) + " (" + (why ? why : "?")
             + ") - a command-channel verb overrides the file, which decision 43 says it may.");
    if (was == (long)kRoleSingle) StoreRoleLeftSingle();
}

// ------------------------------------------------------------------------------------------------
// P8i (U1) - THE WRITER.  See config.h for the contract; the one thing worth repeating here is that it
// updates NO global and arms NOTHING, so a settings save cannot change what this session is doing.
// ------------------------------------------------------------------------------------------------
std::string ConfigFilePath() { return DataFile(coopdata::kSessionCfg); }   /* PP3: session.cfg in the data folder */

bool ConfigWrite(const coopcfg::CfgFields& fields, std::string* err)
{
    const std::string finalPath = DataFile(coopdata::kSessionCfg);
    if (finalPath.empty())
    {
        if (err) *err = "there is no data folder to save the settings in (LOCALAPPDATA is not usable).";
        ErrorLog("[CFG] the MULTIPLAYER panel could not save: there is no data folder (PP3).");
        return false;
    }
    /* PP3: session.cfg carries the panel's own fields only. The identity (playerid, playername, the last address) is
       player.cfg's; the folder overrides are TEST keys that live only in the DLL-side shared_wastelands.cfg, which is never written. */
    const std::string body = coopdata::SessionCfgText(fields);   /* PP3b: one pure function, swept by the offline suite */
    EnsureDirTree(g_dataDir);
    std::string werr;
    if (!WriteFileAtomic(finalPath, body, &werr))
    {
        if (err) *err = werr + " - the settings were not saved.";
        ErrorLog("[CFG] the MULTIPLAYER panel could not write " + finalPath + " (" + werr + "); the old session.cfg is intact.");
        return false;
    }
    /* The identity file follows when the name or the address changed. A failure there is said; the settings above stand. */
    {
        bool change = false;
        if (!fields.playerName.empty() && fields.playerName != g_playerName && coopworld::DisplayNameOk(fields.playerName, 0))
        { g_playerName = fields.playerName; change = true; }
        if (fields.hostPort != 0 && coopdata::LastAddrRecordable(fields.role, fields.hostAddr)   /* PP3b: a host's 0.0.0.0 is not an address */
            && (fields.hostAddr != g_lastAddr || fields.hostPort != g_lastPort))
        { g_lastAddr = fields.hostAddr; g_lastPort = fields.hostPort; change = true; }
        std::string perr;
        if (change && !WritePlayerCfg(&perr))
            ErrorLog("[CFG] PP3: player.cfg could not be updated (" + perr + ") - the name / last address are kept for this session only.");
    }
    DebugLog("[CFG] session.cfg WRITTEN by the MULTIPLAYER panel: role="
             + std::string(RoleName(fields.role))
             + " host=" + AddrText(fields.hostAddr, fields.hostPort)
             + " store=" + AddrText(fields.storeAddr, fields.storePort)
             + " slot='" + fields.slot + "' world='" + fields.world + "' playerId='" + ConfigPlayerId()
             + "' -> " + finalPath
             + ". The press that wrote it re-reads it now (ConfigRearmFromFile) and the next title tick arms from it;"
             " at a start it only pre-fills the panel (PP3b).");
    return true;
}

}
