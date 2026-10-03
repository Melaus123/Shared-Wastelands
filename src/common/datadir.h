/* datadir.h - PP3 (player-path-plan PP3; manager decisions 2026-09-27): THE PLAYER'S DATA FOLDER.
   Pure decisions only (no Windows, no files), so the plugin, SharedWastelandsServer.exe and the offline suite compile the SAME rules.

   THE LAYOUT (default %LOCALAPPDATA%\kenshi\Shared Wastelands\, beside Kenshi's own save folder):
     player.cfg   - this install's identity: playerid, playername, the last address/port. Written by the plugin.
     session.cfg  - what the MULTIPLAYER panel saves (role/host/store/slot/world). PP3b: it only PRE-FILLS the panel at the
                    next start and never arms a connection by itself; a connection starts only from the panel.
     worlds\      - the world server's data (one folder per world) - OUT of Kenshi's save folder, so LOAD GAME never lists it.
     players\     - the records stores: each multiplayer world + profile's own (T-251, mp-*) and TEST ones (ownsave <name>).
     backups\     - reserved (PP9); nothing creates it yet.
   The DLL-side shared_wastelands.cfg is now TEST-ONLY: the harness's keys (role, host, ..., saveroot, storedir, datadir) are read
   from it AFTER session.cfg, so a key it names wins; the plugin never writes it. Its playerid/playername are a one-time
   MIGRATION source only (IdentityDecide). */
#ifndef COOP_DATADIR_H
#define COOP_DATADIR_H

#include <string>
#include "cfgtext.h"   /* PP3b: SessionCfgText formats with coopcfg::CfgFormat */
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */

namespace coopdata {

const char* const kDataSub     = swnames::kDataSub;   /* appended to %LOCALAPPDATA% */
const char* const kPlayerCfg   = "player.cfg";
const char* const kSessionCfg  = "session.cfg";
const char* const kWorldsSub   = "worlds";
const char* const kRecordsSub  = "players";   /* owner 244 */

/* An ABSOLUTE folder: a drive letter, a colon, a separator and at least one more character, or a UNC path. */
inline bool AbsDirOk(const char* p)
{
    if (p == 0) return false;
    const std::string s(p);
    if (s.size() >= 4 && s[0] == '\\' && s[1] == '\\') return true;
    if (s.size() < 4) return false;
    const char d = s[0];
    if (!((d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z'))) return false;
    return s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}
inline std::string TrimSep(const std::string& s)
{
    std::string o(s);
    while (o.size() > 3 && (o[o.size() - 1] == '\\' || o[o.size() - 1] == '/')) o.erase(o.size() - 1);
    return o;
}

enum DataDirSource { kDataNone = 0, kDataOverride = 1, kDataLocalAppData = 2, kDataUserProfile = 3 };
inline const char* DataDirSourceName(int s)
{
    return s == kDataOverride ? "datadir= (TEST key)" : s == kDataLocalAppData ? "%LOCALAPPDATA%"
         : s == kDataUserProfile ? "%USERPROFILE%\\AppData\\Local (LOCALAPPDATA unusable)" : "none";
}

/* THE DATA FOLDER. overrideDir = shared_wastelands.cfg's datadir= (already accepted by coopcfg::CfgRootPathOk) or "".
   The override wins; else %LOCALAPPDATA%\kenshi\Shared Wastelands; else %USERPROFILE%\AppData\Local\kenshi\Shared Wastelands;
   else NONE (out = ""): the caller keeps the identity in memory only and says so - it never falls back to the mod folder. */
inline int DataDirChoose(const std::string& overrideDir, const char* localAppData, const char* userProfile, std::string* out)
{
    if (!overrideDir.empty()) { *out = TrimSep(overrideDir); return kDataOverride; }
    if (AbsDirOk(localAppData)) { *out = TrimSep(localAppData) + kDataSub; return kDataLocalAppData; }
    if (AbsDirOk(userProfile)) { *out = TrimSep(userProfile) + "\\AppData\\Local" + kDataSub; return kDataUserProfile; }
    out->clear();
    return kDataNone;
}

/* THE WORLD SERVER'S TOP FOLDER: <data folder>\worlds. "" when there is no data folder. */
inline std::string WorldsDir(const std::string& dataDir) { return dataDir.empty() ? std::string() : dataDir + "\\" + kWorldsSub; }
/* THE JOINED WORLDS' TOP FOLDER: <data folder>\joined - a world this computer joins but does not run keeps its queue and mirror
   there, apart from any world of the same name this computer runs. "" when there is no data folder. */
inline std::string JoinedDir(const std::string& dataDir) { return dataDir.empty() ? std::string() : dataDir + "\\" + swnames::kJoinedSub; }

/* THE IDENTITY DECISION, once per start. newId = player.cfg's playerid (already shape-checked, "" = none or no file);
   oldId = the DLL-side shared_wastelands.cfg's playerid ("" = none). player.cfg WINS whenever it holds an id; the old file is
   only copied from when player.cfg has none; neither = a new id. The old file is never deleted or edited. */
enum IdentitySource { kIdGenerate = 0, kIdMigrate = 1, kIdRead = 2, kIdFromBak = 3, kIdSessionOnly = 4 };
inline int IdentityDecide(const std::string& newId, const std::string& oldId)
{
    if (!newId.empty()) return kIdRead;
    if (!oldId.empty()) return kIdMigrate;
    return kIdGenerate;
}

/* PP3b (review 2026-09-27 HIGH) - WHAT player.cfg IS, before its id is looked at. ABSENT = Windows says there is no such file;
   UNREADABLE = it is there but would not open for ~1 s (another program holding it); INVALID = it reads but holds no valid id
   (empty, damaged, hand-edited). */
enum PlayerCfgState { kPcAbsent = 0, kPcValid = 1, kPcUnreadable = 2, kPcInvalid = 3 };

/* PP3b - THE IDENTITY DECISION WITH THE FILE'S STATE. A NEW id is made ONLY when player.cfg is truly ABSENT and neither
   player.cfg.bak nor the old shared_wastelands.cfg holds one. A PRESENT file that is unreadable or holds no valid id is NEVER
   replaced by a new id: its .bak is used when that holds one (kIdFromBak), else the id lives in this session only
   (kIdSessionOnly) and player.cfg is not written. newId / bakId / oldId: "" = none or not a valid id. */
inline int IdentityDecideState(int state, const std::string& newId, const std::string& bakId, const std::string& oldId)
{
    if (state == kPcValid && !newId.empty()) return kIdRead;
    if (state == kPcAbsent)
    {
        if (!bakId.empty()) return kIdFromBak;
        if (!oldId.empty()) return kIdMigrate;
        return kIdGenerate;
    }
    if (!bakId.empty()) return kIdFromBak;
    if (state == kPcInvalid && !oldId.empty()) return kIdMigrate;   /* PP3e: a valid legacy id RESTORES a damaged player.cfg */
    return kIdSessionOnly;
}

/* PP3b: may player.cfg be written after that decision? Never after kIdSessionOnly. After kIdFromBak only when the file was
   absent or readable-but-invalid (moved aside first) - a LOCKED file may still hold a good id, so it is left alone. */
inline bool IdentityMayWrite(int state, int decision)
{
    if (decision == kIdSessionOnly) return false;
    if (decision == kIdFromBak && state == kPcUnreadable) return false;
    return true;
}

/* PP3c (re-check 2026-09-27 HIGH) - THE START-UP FILE PLAN, pure so the offline suite can run two starts in a row.
   decision = IdentityDecideState; write = player.cfg is written this start (never for READ or SESSION-ONLY);
   moveAside = a readable-but-invalid player.cfg is renamed to player.cfg.bad-<time> - ONLY when a valid .bak restore is
   written in the same step. With no usable .bak the file is left exactly where and as it is, so the NEXT start sees the
   same PRESENT-but-bad file and is session-only again: a moved-aside file would read as ABSENT there and a NEW id would
   be written. sessionOnly = the id lives in memory only this start. */
struct IdentityStartPlan { int decision; bool write; bool moveAside; bool sessionOnly; };
/* PP3e (review 2026-09-27 MED): bakState = player.cfg.bak's own state. A .bak only LOCKED through its retry may still hold this
   install's id, so beside a damaged player.cfg the old shared_wastelands.cfg's id is NOT used over it (session-only, kind 1). A damaged
   player.cfg with no usable .bak and a VALID legacy id is RESTORED from it (kIdMigrate): moved aside and rewritten in the same
   step, exactly like the .bak restore. */
inline IdentityStartPlan IdentityPlanStart(int state, const std::string& newId, int bakState, const std::string& bakId, const std::string& oldId)
{
    IdentityStartPlan p;
    const bool bakLocked = bakState == kPcUnreadable && bakId.empty();
    p.decision = IdentityDecideState(state, newId, bakId, (state == kPcInvalid && bakLocked) ? std::string() : oldId);
    p.sessionOnly = p.decision == kIdSessionOnly;
    p.write = p.decision != kIdRead && IdentityMayWrite(state, p.decision);
    p.moveAside = state == kPcInvalid && (p.decision == kIdFromBak || p.decision == kIdMigrate) && p.write;
    return p;
}
inline IdentityStartPlan IdentityPlanStart(int state, const std::string& newId, const std::string& bakId, const std::string& oldId)
{
    return IdentityPlanStart(state, newId, bakId.empty() ? kPcAbsent : kPcValid, bakId, oldId);
}

/* PP3d (owner-approved 2026-09-27) - IS THIS START'S IDENTITY USABLE FOR MULTIPLAYER? Only a SESSION-ONLY start is not:
   UNREADABLE = player.cfg is present but would not open after the ~1 s retry; DAMAGED = it reads but holds no valid id. Both
   with no usable .bak (a .bak restore, even beside a locked file, carries this install's own id and is normal). */
enum IdentityUnusableKind { kIdKindNormal = 0, kIdKindUnreadable = 1, kIdKindDamaged = 2 };
/* PP3e (review 2026-09-27 MED): DAMAGED means NOTHING is recoverable - a .bak LOCKED through its retry is UNREADABLE (kind 1:
   restart, it may still hold the id), never DAMAGED. */
inline int IdentitySessionOnlyKindOf(int state, int bakState, const IdentityStartPlan& p)
{
    if (!p.sessionOnly) return kIdKindNormal;
    if (state == kPcUnreadable || bakState == kPcUnreadable) return kIdKindUnreadable;
    if (state == kPcInvalid) return kIdKindDamaged;
    return kIdKindNormal;
}
inline int IdentitySessionOnlyKindOf(int state, const IdentityStartPlan& p) { return IdentitySessionOnlyKindOf(state, kPcAbsent, p); }
/* PP3d: HOST and JOIN are refused while the kind is not normal - no world ever meets a session-only id. */
inline bool IdentityRefusesHostJoin(int kind) { return kind != kIdKindNormal; }
/* PP3d: the identity box's buttons (0 none pressed, 1 OK / CANCEL, 2 CONTINUE AS NEW PLAYER). The ONLY way an identity is
   replaced is the DAMAGED box's CONTINUE AS NEW PLAYER, pressed by the player. */
const int kIdBoxNone = 0, kIdBoxDismiss = 1, kIdBoxContinue = 2;
inline bool IdentityMayReplace(int kind, int button) { return kind == kIdKindDamaged && button == kIdBoxContinue; }
/* PP3e (review 2026-09-27 MED) - AT THE CONTINUE AS NEW PLAYER CLICK player.cfg and player.cfg.bak are READ AGAIN (each with the
   ~1 s retry). A valid id in either is RECOVERED (player.cfg first) and nothing is replaced; a file that still will not open is
   not "no id", so the click is refused; only when neither holds a valid id does the replacement run. */
enum IdentityClickAction { kClickReplace = 0, kClickRecoverCfg = 1, kClickRecoverBak = 2, kClickRefuseUnreadable = 3 };
inline int IdentityClickDecide(int cfgState, int bakState)
{
    if (cfgState == kPcValid) return kClickRecoverCfg;
    if (bakState == kPcValid) return kClickRecoverBak;
    if (cfgState == kPcUnreadable || bakState == kPcUnreadable) return kClickRefuseUnreadable;
    return kClickReplace;
}

/* PP3b (review LOW): the LAST ADDRESS player.cfg keeps is one the player JOINED - a host's own 0.0.0.0:<port> is not one. */
inline bool LastAddrRecordable(int role, const std::string& addr)
{
    return role == coopcfg::kCfgClient && !addr.empty() && addr != "0.0.0.0";
}

/* player.cfg's text. CRLF, only the lines that carry a value; host= is the LAST address/port the player used. Parsed back
   by coopcfg::CfgParseText (the same keys), so a name the parser would refuse is the caller's to drop first. */
inline std::string PlayerCfgText(const std::string& id, const std::string& name, const std::string& lastAddr, unsigned short lastPort)
{
    std::string o = "# player.cfg - this install's identity for " SW_NAME " (PP3). Do not copy it to another machine.\r\n";
    o += "playerid=" + id + "\r\n";
    if (!name.empty()) o += "playername=" + name + "\r\n";
    if (lastPort != 0)
    {
        char digits[8]; int n = 0; unsigned v = lastPort;
        while (v != 0 && n < 7) { digits[n++] = (char)('0' + v % 10); v /= 10; }
        std::string port; while (n > 0) port += digits[--n];
        o += "host=" + (lastAddr.empty() ? std::string("0.0.0.0") : lastAddr) + ":" + port + "\r\n";
    }
    return o;
}

/* THE SETTINGS THE GAME ARMS FROM: session.cfg first, then the TEST file, parsed as ONE text so the last line of a key wins -
   a key the harness writes into shared_wastelands.cfg overrides the panel's session.cfg; a key it leaves out does not. */
inline std::string LayeredSettingsText(const std::string& sessionText, const std::string& testText)
{
    return sessionText + "\n" + testText;
}

/* PP3b - WHO WROTE THE DLL-SIDE shared_wastelands.cfg, read from its first line (a UTF-8 mark and blank lines skipped):
     SW_TEST_CFG_HEADER " tools/write-cfg.ps1"  = the harness's file (kTestHarness): its keys take precedence;
     SW_TEST_CFG_HEADER followed by anything else = a panel-written file (kTestPanelOld): IGNORED for everything except the
                                                    one-time identity copy (its playerid), so its keys never override a panel save;
     any other first line (a hand-written test file) = kTestHarness. */
enum TestCfgKind { kTestNone = 0, kTestHarness = 1, kTestPanelOld = 2 };
inline int TestCfgKindOf(const std::string& text)
{
    size_t i = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) i = 3;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) ++i;
    if (i >= text.size()) return kTestNone;
    static const char kHarness[] = SW_TEST_CFG_HEADER " tools/write-cfg.ps1";
    static const char kWritten[] = SW_TEST_CFG_HEADER;
    if (text.compare(i, sizeof(kHarness) - 1, kHarness) == 0) return kTestHarness;
    if (text.compare(i, sizeof(kWritten) - 1, kWritten) == 0) return kTestPanelOld;
    return kTestHarness;
}

/* PP3b (manager decision 2026-09-27) - THE TEXT A GAME ARMS FROM. At START: the test file only - session.cfg only pre-fills
   the panel and never arms a connection. At a PANEL PRESS (the re-arm): session.cfg, then the test file's keys over it.
   An old panel-written test file counts as no test file in both. */
inline std::string ArmSettingsText(bool atStart, const std::string& sessionText, const std::string& testText)
{
    const std::string t = TestCfgKindOf(testText) == kTestPanelOld ? std::string() : testText;
    return atStart ? t : LayeredSettingsText(sessionText, t);
}

/* PP3b - session.cfg's text: the panel's own fields only. The identity (playerid, playername) is player.cfg's; the folder
   overrides and the TEST-only keys (modfingerprint_salt, profile) live only in the DLL-side test file. */
inline std::string SessionCfgText(const coopcfg::CfgFields& fields)
{
    coopcfg::CfgFields s = fields;
    s.playerId.clear(); s.playerName.clear(); s.saveRoot.clear(); s.storeDir.clear(); s.dataDir.clear();
    s.modSalt = 0; s.profileTest = 0;
    std::string body = coopcfg::CfgFormat(s);
    const std::string was = SW_TEST_CFG_HEADER;
    if (body.compare(0, was.size(), was) == 0) body.replace(0, was.size(), "# session.cfg - written by");
    return body;
}

}   /* namespace coopdata */

#endif   /* COOP_DATADIR_H */
