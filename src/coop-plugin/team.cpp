// team.cpp - see team.h. [TEAM] lines.
#include "team.h"
#include "store.h"          /* StoreSendTeam, StoreMySlot, StoreRosterNameOf, StoreLinkGen, StoreWelcomedThisLink, StoreNotebookDir, EngineWritesBlocked */
#include "relations.h"      /* RelationsOwnRecord: this game's faction standing with every NPC faction, read for the ACCEPT */
#include "playerfaction.h"  /* LocalPlayerFaction */
#include "soak.h"           /* GameplayRunning */
#include "tags.h"           /* TagsCaptionsDirty: a team change recolours and renames the name tags */
#include "policy.h"         /* PolicyTeamAccessProbe: the `team access` lever */
#include "playerstab.h"     /* PlayersTabRemovedWhileAway: the FACTION box at the next join */
#include "coop_log.h"
#include "game/Faction.h"
#include "../common/teamwire.h"
#include "../common/teameffect.h"   /* what membership does: the pin, the tag's colour and line 2, base access, a notice's names */
#include "../common/ownrec.h"   /* the pre-join standing's NPC rows are the pp.faction record's encoding */
#include "../common/teamstanding.h"   /* step 5: the pre-join snapshot, the join set, the founder's stance, the restore */
#include "../common/teamresearch.h"   /* step 6: the shared research - what is new here, the echo rule, the lines */
#include "../common/teamscreens.h"    /* the faction screens' words: the message lines */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>   /* GetTickCount */
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coop {
namespace {
std::vector<swteam::WireTeam> g_table;
int g_tableGen = -1;                 // StoreLinkGen() of the link that sent g_table (-1 = none: never sent, or cleared)
int g_askedGen = -1;                 // StoreLinkGen() of the link an ASK went on since the copy was cleared (-1 = none)
std::string g_tableSaid;
struct WaitingInvite { int on; unsigned fromSlot; std::string teamName; unsigned long at; unsigned serial; bool answered; WaitingInvite() : on(0), fromSlot(0), at(0), serial(0), answered(false) {} };
WaitingInvite g_invite;
unsigned g_inviteSerial = 0;         // the serial the last invitation received was given
long long g_linesShown = 0, g_linesNotShown = 0, g_linesNoWords = 0, g_linesHeld = 0;
teamscreen::HeldLines g_heldLines;   // the faction lines waiting for a world that is ready to show them, in order
bool g_heldSaid = false;             // "the game cannot show a line yet" said for the lines held now
/* when each slot last became or stopped being this game's player's teammate (GetTickCount, 0 = never), read on any thread
   (TeamMateQuietAnyThread); and when any did (the PLAYERS table's settle wait, main thread) */
volatile LONG g_mateChangedMs[swteam::kSlotIndexCap];
unsigned long g_matesChangedMs = 0;
swteam::Inbox g_inbox;               // RESTORE rows held until this game's world is loaded, and the row numbers answered
long long g_in = 0, g_malformed = 0, g_restores = 0, g_restoresAway = 0, g_restoresAgain = 0, g_restoresDone = 0, g_sent = 0, g_sendFailed = 0, g_asks = 0, g_cleared = 0;
/* the team NUMBER each slot is in on this game's copy of the table (0 = none), published by the main thread at every table
   change for the readers on any thread (base access in policy.cpp's hooks): aligned 32-bit stores and loads, no lock */
volatile LONG g_teamOfSlot[swteam::kSlotIndexCap];
/* THE PIN: the teammates this game holds its own side towards at ally (ascending), and when it last looked at those sides */
std::vector<unsigned> g_pinned;
long long g_pinEpoch = -1;                 // RelationsPlayerPairEpoch() at the last look
unsigned long g_pinLookedAt = 0;           // GetTickCount() of the last look
bool g_pinLookOwed = false;
const unsigned long kPinLookEveryMs = 2000;   // a look even when no standing with a player faction moved
const size_t kPinFailCausesKept = 64;   // (teammate, cause) pairs remembered as said; past that every failure is counted only
std::map<std::string, long long> g_pinFailSaid;   // "<slot>|<cause>" -> failures of that cause towards that teammate (cause: RelationsPinAlly's code)
/* the box stub's cells (policy.cpp PolicyTeammateFactions): this game's teammates' factions, looked up again on a table change and
   every kCellsEveryMs (a stand-in met after the table arrived) */
bool g_cellsOwed = true;
unsigned long g_cellsAt = 0;
const unsigned long kCellsEveryMs = 1000;
long long g_pinStarts = 0, g_pinEnds = 0, g_pinWrites = 0, g_pinFails = 0, g_pinLooks = 0, g_pinDropped = 0;
std::map<unsigned, std::string> g_names;   // the roster's name for each slot seen this session: a player who has left is still named
/* THE SHARED STANDING (src/common/teamstanding.h; the record: teamwire.h TeamRec): the last record the world server sent, whether
   it is owed onto this world's faction, the seed sent on this link, this game's own changes gathered every kDeltaEveryMs, and the
   held RESTORE rows not written yet (tried again after swteam::RestoreRetryMs) */
swteam::TeamRec g_rec;
unsigned g_recTeam = 0;                    // the team number the record is for (0 = none held)
bool g_recOwed = false;                    // the record is to be written onto this world's faction
unsigned g_recWrittenGen = 0;
int g_seedLink = -1; unsigned g_seedTeam = 0;   // the link generation and team a SEED went on
unsigned long g_deltaAt = 0, g_restoreTriedAt = 0;
long long g_records = 0, g_recordWrites = 0, g_recordWriteFails = 0, g_seeds = 0, g_deltaSends = 0, g_deltaSendFails = 0, g_stances = 0,
          g_sides = 0, g_restoreFails = 0;
std::map<unsigned, long long> g_restoreFailsOf;   // row number -> failed tries (the wait doubles; said at the first and at the cap)
std::map<unsigned, unsigned long> g_restoreNextTry;   // row number -> GetTickCount() of its next try
std::map<unsigned, swteam::RestoreProgress> g_restoreProgress;   // row number -> what of it is written (kept across a link loss; a new world clears it)
bool g_seedBase = false;                   // the founder's SEED is the base of its own changes until its first record is written
std::map<unsigned, unsigned> g_stanceAskedGen;   // slot -> the record generation a filling STANCE was sent against
/* a departure from a team that stays (three players or more): this player's teammates on the last table, and the ones who have
   left since the last record - the founder's fill sends no stance towards them until the record the world server sends after the
   departure's RESTORE rows has arrived (swteam::StancesMissing's `waiting`) */
std::vector<unsigned> g_lastMates;
std::set<unsigned> g_fillWait;
std::string g_lastMatesWorld;              // the world (StoreNotebookDir) g_lastMates and g_fillWait belong to
/* a newer own change wins over a departure's side (swteam::OwnSideStands): the own-change number (RelationsSideNoNow) when the
   pin towards each departed player ended here, and when each held RESTORE row first arrived */
std::map<unsigned, unsigned long long> g_pinEndNo;
std::map<unsigned, unsigned long long> g_rowArrivedNo;
long long g_sidesKeptOwn = 0;
unsigned long g_recWriteTriedAt = 0;
/* 477: the side towards a new teammate as the FIRST pin write found it, owed to the world server until sent; and the teammates
   whose first pin write has been seen while pinned */
std::map<unsigned, std::pair<float, unsigned> > g_sideOwed;
std::set<unsigned> g_sideSeen;
long long g_sidesNothing = 0;
/* THE SHARED RESEARCH (src/common/teamresearch.h; the wire: teamwire.h RESEARCH): the whole finished list the world server
   accepted for this team on this link since the world was loaded, and the one sent and not answered yet; the team's whole
   research and the teammates' techs received and not applied yet (applied once this game's world is ready); the techs this
   game's own player finished and not sent yet, and those sent and not answered yet; the echo filter's sets */
unsigned g_resWholeTeam = 0;               // the team whose world server accepted this game's whole list since the world was loaded (0 = none)
int g_resWholeLink = -1;                   // the link that answer came on
unsigned g_resFlightTeam = 0;              // the whole list sent and not answered yet: its team (0 = none), link, time and resends
int g_resFlightLink = -1;
unsigned long g_resFlightAt = 0;
int g_resFlightResends = 0;
std::vector<std::string> g_resFlightList;  // the techs it carried (this game's own finishes among them are covered once it is accepted)
const unsigned long kResRetryMs = 2000;    // a load refused, or a finished list that could not be read, is tried again after this
unsigned long g_resLoadTriedAt = 0;        // the load's own timer
unsigned long g_resReadTriedAt = 0;        // the finished list's read timer
bool g_resWholeOwed = false;               // a whole research received, to be loaded
unsigned g_resWholeMsgTeam = 0;
std::vector<std::string> g_resWhole;
struct ResIncoming { std::string sid; unsigned by, team; ResIncoming() : by(0xFFFFFFFFu), team(0) {} };
std::vector<ResIncoming> g_resIncoming;    // teammates' techs received, applied in order once the world is ready
std::vector<std::string> g_resPending;     // this game's own finishes, not sent yet
std::vector<std::string> g_resOwnFlight;   // this game's own finishes sent and not answered yet: their time and resends
unsigned long g_resOwnAt = 0;
int g_resOwnResends = 0;
std::set<std::string> g_resNotOwn;         // techs that came from the team (its whole research, teammates' techs): never sent as this player's own
std::set<std::string> g_resHeld;           // techs the world server has said the team holds
long long g_resIn = 0, g_resWholeSends = 0, g_resWholeResends = 0, g_resWholeAnswers = 0, g_resOwnSends = 0, g_resOwnResendCount = 0, g_resOwnAnswers = 0,
          g_resRefused = 0, g_resNotSent = 0, g_resEncodeFails = 0, g_resLoads = 0, g_resLoadFails = 0, g_resLoadedTechs = 0,
          g_resAppliedBySet = 0, g_resAppliedByLoad = 0, g_resApplyFails = 0, g_resAlready = 0, g_resNotMine = 0, g_resOffDropped = 0,
          g_resDroppedSaid = 0, g_resReadFails = 0;
bool g_resOffSaid = false;

std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }
std::string Value(int read, float v) { if (read != 1) return std::string("unreadable here now"); std::ostringstream o; o << v; return o.str(); }
/* a player's faction name as this game shows it: mine, that player's stand-in met this session, or the coop-p<n> faction this
   world's save carries; "" when none is here or no world is loaded. MAIN THREAD. */
std::string FactionWordsOf(int slot)
{
    if (slot < 0 || EngineWritesBlocked()) return std::string();
    if (slot == StoreMySlot()) { ::Faction* m = LocalPlayerFaction(); return m != 0 ? std::string(m->getName()) : std::string(); }
    ::Faction* f = StandInForSlot(slot);
    if (f == 0) f = StandInRecordFaction(slot);
    if (f == 0) return std::string();
    const std::string shown = StandInDisplayName(f);
    return shown.empty() ? std::string(f->getName()) : shown;
}
/* a player named in a line: the roster's name, else the one it gave this slot earlier this session, else that player's faction
   name, else "s<n>" (swteam::PlayerWords). MAIN THREAD. */
std::string NameOf(unsigned slot)
{
    if (slot == 0xFFFFFFFFu) return "?";
    std::string n;
    if (StoreRosterNameOf((int)slot, &n) == 1 && !n.empty()) g_names[slot] = n; else n.clear();
    std::map<unsigned, std::string>::const_iterator it = g_names.find(slot);
    return swteam::PlayerWords(n, it != g_names.end() ? it->second : std::string(), FactionWordsOf((int)slot), slot);
}
/* a player's name as the faction screens say it: the roster's name, else the one it gave earlier this session; "" otherwise */
std::string ScreenNameOf(unsigned slot)
{
    if (slot == 0xFFFFFFFFu) return std::string();
    std::string n;
    if (StoreRosterNameOf((int)slot, &n) == 1 && !swteam::Clean(n).empty()) { g_names[slot] = n; return n; }
    std::map<unsigned, std::string>::const_iterator it = g_names.find(slot);
    return it != g_names.end() ? it->second : std::string();
}
/* the held faction lines shown, oldest first, once a world is ready (loaded, not tearing down); a line the game cannot show yet
   (no message bar) is held again with the ones after it, for the next tick */
void FlushLines()
{
    const std::vector<std::string> lines = teamscreen::TakeHeld(&g_heldLines, GameplayRunning() && !EngineWritesBlocked());
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const int r = StoreShowPlayerLine(lines[i]);
        if (r == 0)
        {
            teamscreen::PutBack(&g_heldLines, lines, i);
            if (!g_heldSaid) { g_heldSaid = true; DebugLog("[TEAM] message line '" + lines[i] + "' held: the game has no message bar yet (" + N((long long)g_heldLines.q.size()) + " held)"); }
            return;
        }
        g_heldSaid = false;
        if (r == 1) ++g_linesShown; else ++g_linesNotShown;
        DebugLog("[TEAM] message line '" + lines[i] + "' " + (r == 1 ? std::string("shown") : "NOT shown (" + N((long long)r) + ")")
                 + " (shown " + N(g_linesShown) + ", not shown " + N(g_linesNotShown) + ", held " + N(g_linesHeld) + ", dropped " + N((long long)g_heldLines.dropped) + ")");
    }
}
/* one line on the game's message line (StoreShowPlayerLine, as the PLAYERS and FALLEN tabs' lines), behind any line still held;
   held while a world loads or tears down and shown once it is ready; "" = the page has no words for it here - logged only */
void ShowLine(const std::string& line, const std::string& what)
{
    if (line.empty()) { ++g_linesNoWords; DebugLog("[TEAM] message line for " + what + ": none (no words for it on this side, or a name this game does not know)"); return; }
    teamscreen::HoldLine(&g_heldLines, line);
    if (!GameplayRunning() || EngineWritesBlocked())
    {
        ++g_linesHeld;
        DebugLog("[TEAM] message line '" + line + "' for " + what + " held: no world is ready to show it (" + N((long long)g_heldLines.q.size()) + " held)");
        return;
    }
    DebugLog("[TEAM] message line for " + what + ": '" + line + "'");
    FlushLines();
}
/* every slot on the table named now, so a player who later leaves the world is still named by the roster's name */
void RememberNames()
{
    for (size_t t = 0; t < g_table.size(); ++t)
    {
        NameOf(g_table[t].founderSlot);
        for (size_t m = 0; m < g_table[t].memberSlots.size(); ++m) NameOf(g_table[t].memberSlots[m]);
    }
}
/* the any-thread index from the table this game holds now (an empty table when it holds none) */
void PublishIndex()
{
    const std::vector<unsigned> idx = swteam::TeamIndex(g_tableGen < 0 ? std::vector<swteam::WireTeam>() : g_table);
    const int me = StoreMySlot();
    const unsigned mineWas = (me >= 0 && (unsigned)me < swteam::kSlotIndexCap) ? (unsigned)g_teamOfSlot[me] : 0u;
    const unsigned mineNow = (me >= 0 && (unsigned)me < swteam::kSlotIndexCap) ? idx[me] : 0u;
    const unsigned long now = ::GetTickCount() | 1u;   /* never 0: 0 = never changed */
    for (unsigned s = 0; s < swteam::kSlotIndexCap; ++s)
    {
        const bool was = (int)s != me && mineWas != 0 && (unsigned)g_teamOfSlot[s] == mineWas;
        const bool is = (int)s != me && mineNow != 0 && idx[s] == mineNow;
        if (was != is) { g_mateChangedMs[s] = (LONG)now; g_matesChangedMs = now; }
        if ((unsigned)g_teamOfSlot[s] != idx[s]) g_teamOfSlot[s] = (LONG)idx[s];
    }
}
std::string MineText()
{
    const int me = StoreMySlot();
    if (me < 0) return "me=s? (no slot)";
    if (g_tableGen < 0) return "me=s" + N(me) + " (no table)";
    int founder = 0;
    const int t = swteam::WireTeamOf(g_table, (unsigned)me, &founder);
    if (t < 0) return "me=s" + N(me) + " in no team";
    return "me=s" + N(me) + " " + (founder ? "FOUNDER" : "MEMBER") + " of team " + N((long long)g_table[t].no) + " '" + swteam::Clean(g_table[t].name) + "'";
}
/* the sentence a player would read for a notice (build/read-t545-t546.md B7.8); the screens come in a later step */
std::string NoticeLine(const swteam::Down& d)
{
    const int me = StoreMySlot();
    const bool iAmSubject = me >= 0 && d.subjectSlot == (unsigned)me;
    const std::string team = d.teamName, who = NameOf(d.subjectSlot);
    if (d.result != swteam::kOk) return std::string("Refused (") + swteam::EventName((int)d.event) + "): " + swteam::ResultName((int)d.result) + ".";
    switch (d.event)
    {
    case swteam::kEvInvite: return "Invitation sent to " + who + ".";
    case swteam::kEvJoined: return iAmSubject ? "You joined " + team + "." : who + " joined " + team + ".";
    case swteam::kEvDeclined: return iAmSubject ? "You declined the invitation to " + team + "." : who + " declined your invitation.";
    case swteam::kEvExpired: return iAmSubject ? "The invitation to " + team + " ended unanswered." : who + " did not answer.";
    case swteam::kEvLeft: return iAmSubject ? "You left " + team + "." : who + " left " + team + ".";
    case swteam::kEvRemoved: return iAmSubject ? "You were removed from " + team + "." : who + " was removed from " + team + ".";
    case swteam::kEvDisbanded: return team + " was disbanded.";
    case swteam::kEvInviteGone: return "The invitation to " + team + " ended: a player left the world.";
    default: return "?";
    }
}
bool Send(const std::vector<char>& b)
{
    if (StoreSendTeam(b)) { ++g_sent; return true; }
    ++g_sendFailed;
    return false;
}
void SendDone(unsigned no)
{
    std::vector<char> b; swteam::EncodeRestoreDone(&b, no);
    if (!Send(b)) DebugLog("[TEAM] RESTORE_DONE #" + N((long long)no) + " NOT sent (no link) - the world server keeps the row and sends it again at the next join, where it is answered again");
}
/* this game's world can take the team's standing writes: loaded, not loading or tearing down, and the load's own-record restore
   (store.cpp) already run - so the save's record is not laid over them */
bool WorldReady() { return !EngineWritesBlocked() && GameplayRunning() && !StoreOwnRestorePending(); }
/* 477: a held row written back - the NPC standing ONCE (a retry never writes it again: this player's own changes after it stand),
   then every side it carries (the snapshot's own, and the sides the world server kept from before the pin towards players the
   snapshot lacks) except towards the players this game shares a team with now, each side once. A side towards a player whose
   faction this game does not hold has nothing to write; a side this player changed itself after this game learned of the
   departure is left as it is (swteam::OwnSideStands). true = done (the row is answered); false = not now (*said says why; the row stays held, only what is
   still owed is tried again). A snapshot of a version this build does not know is held; only broken bytes, which can never be
   written, are answered (with an error line) so the world server stops keeping them. */
bool ApplyRestore(const swteam::Down& d, std::string* said)
{
    swteam::PreJoin pj;
    if (!d.snap.empty())
    {
        const int k = swteam::DecodePreJoin(d.snap, &pj);
        if (k == swteam::kPjVersion) { *said = "its pre-join standing is of a version this build does not know - held for a build that does"; return false; }
        if (k == swteam::kPjMalformed)
        {
            *said = "its pre-join standing (" + N((long long)d.snap.size()) + " bytes) is broken - NOTHING written back";
            ErrorLog("[TEAM] restore #" + N((long long)d.no) + ": " + *said);
            return true;
        }
    }
    swteam::RestoreProgress& pg = g_restoreProgress[d.no];
    std::string why, detail;
    if (!pg.npcDone && !pj.npc.rows.empty())
    {
        if (!RelationsTeamSet(pj.npc, &why, &detail)) { *said = "the NPC standings were not written (" + why + ")"; return false; }
    }
    const bool npcNow = !pg.npcDone && !pj.npc.rows.empty();
    pg.npcDone = true;
    const int me = StoreMySlot();
    std::vector<unsigned> skipped;
    const std::vector<swteam::PlayerSide> sides = swteam::RestoreSides(swteam::MergeSides(pj.players, d.sides), (unsigned)me, swteam::TeammatesOf(g_table, (unsigned)me), &skipped);
    std::string set, same, nothing, retry, own;
    const std::map<unsigned, unsigned long long>::const_iterator arr = g_rowArrivedNo.find(d.no);
    const unsigned long long arrivedNo = arr != g_rowArrivedNo.end() ? arr->second : RelationsSideNoNow();
    for (size_t i = 0; i < sides.size(); ++i)
    {
        if (swteam::SideDone(pg, sides[i].slot)) continue;
        const std::string tag = "s" + N((long long)sides[i].slot);
        const std::map<unsigned, unsigned long long>::const_iterator pe = g_pinEndNo.find(sides[i].slot);
        if (swteam::OwnSideStands(RelationsSideOwnNo((int)sides[i].slot), pe != g_pinEndNo.end(), pe != g_pinEndNo.end() ? pe->second : 0, arrivedNo))
        { own += (own.empty() ? "" : ",") + tag; pg.sidesDone.push_back(sides[i].slot); ++g_sidesKeptOwn; continue; }
        float now = 0.0f; unsigned fl = 0;
        if (RelationsMineTowardsFull((int)sides[i].slot, &now, &fl) == 1 && now == sides[i].rel && (sides[i].flags < 0 || (unsigned)sides[i].flags == fl))
        { same += (same.empty() ? "" : ",") + tag; pg.sidesDone.push_back(sides[i].slot); continue; }
        float before = 0.0f; std::string w;
        const int o = swteam::SideOutcome(RelationsSetMineTowards((int)sides[i].slot, sides[i].rel, sides[i].flags, false, &before, &w));
        if (o == swteam::kSideRetry) { retry += (retry.empty() ? "" : ",") + tag + " (" + w + ")"; continue; }
        pg.sidesDone.push_back(sides[i].slot);
        if (o == swteam::kSideNothing) { ++g_sidesNothing; nothing += (nothing.empty() ? "" : ",") + tag; }
        else set += (set.empty() ? "" : ",") + tag + " " + Value(1, before) + "->" + Value(1, sides[i].rel);
    }
    std::string sk;
    for (size_t i = 0; i < skipped.size(); ++i) sk += (i ? ",s" : "s") + N((long long)skipped[i]);
    *said = std::string("NPC standings ") + (npcNow ? "written back (" + N((long long)pj.npc.rows.size()) + " factions;" + detail + ")" : pj.npc.rows.empty() ? std::string("none carried") : std::string("written back at an earlier try"))
          + "; this game's side towards other players: set " + (set.empty() ? std::string("none") : set) + ", already so " + (same.empty() ? std::string("none") : same)
          + ", no faction of that player here (nothing to write) " + (nothing.empty() ? std::string("none") : nothing) + ", teammates now (held at ally) " + (sk.empty() ? std::string("none") : sk)
          + ", changed by this player since the departure (its own change stands) " + (own.empty() ? std::string("none") : own);
    if (!retry.empty()) { *said += "; NOT written yet: " + retry; return false; }
    return true;
}
/* every held row written back and answered once this game's world is ready and it holds a table from this link (the table that
   left this game out of the team came first, so the pins have ended); a row not written stays held, tried again after a wait
   that doubles up to swteam::kRestoreRetryCapMs, said at its first failure and when the wait reaches the cap */
void ReleaseHeld()
{
    if (g_inbox.held.empty() || !WorldReady()) return;
    if (!swteam::RestoreDue(true, g_tableGen >= 0 && g_tableGen == StoreLinkGen())) return;
    const unsigned long now = ::GetTickCount();
    if (g_restoreTriedAt != 0 && now - g_restoreTriedAt < 250) return;
    g_restoreTriedAt = now;
    const std::vector<swteam::Down> rows = g_inbox.held;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const swteam::Down& d = rows[i];
        long long& fails = g_restoreFailsOf[d.no];
        if (fails > 0 && (long)(now - g_restoreNextTry[d.no]) < 0) continue;
        std::string said;
        if (!ApplyRestore(d, &said))
        {
            ++fails; ++g_restoreFails;
            const unsigned long wait = swteam::RestoreRetryMs(fails);
            g_restoreNextTry[d.no] = now + wait;
            if (fails == 1 || (wait == swteam::kRestoreRetryCapMs && swteam::RestoreRetryMs(fails - 1) != wait))
                DebugLog("[TEAM] restore #" + N((long long)d.no) + " not written yet: " + said + " - kept, tried again in " + N((long long)(wait / 1000)) + " s"
                         + (wait == swteam::kRestoreRetryCapMs ? " and every " + N((long long)(wait / 1000)) + " s after" : std::string()) + "; RESTORE_DONE waits (fails " + N(fails) + ")");
            continue;
        }
        g_restoreFailsOf.erase(d.no); g_restoreNextTry.erase(d.no); g_restoreProgress.erase(d.no); g_rowArrivedNo.erase(d.no);
        swteam::InboxAnswerOne(&g_inbox, d.no);
        ++g_restoresDone;
        DebugLog("[TEAM] restore #" + N((long long)d.no) + " written with the world loaded: " + swteam::WhyName((int)d.why) + " from '" + swteam::Clean(d.teamName) + "'"
                 + (d.away ? " while away" : "") + " - " + said + "; RESTORE_DONE sent (answered " + N(g_restoresDone) + ")");
        if (d.away && d.why == swteam::kWhyRemoved && !d.snap.empty())
        {
            DebugLog("[TEAM] removal applied at join: the FACTION box is owed (removed from '" + swteam::Clean(d.teamName) + "' while away)");
            PlayersTabRemovedWhileAway(d.teamName);
        }
        SendDone(d.no);
    }
}
std::string TeamWordsOf(unsigned slot)
{
    const int t = swteam::WireTeamOf(g_table, slot, 0);
    return t < 0 ? std::string("no team") : "team " + N((long long)g_table[t].no) + " '" + swteam::Clean(g_table[t].name) + "'";
}
/* THE PIN (members are allies), with this game's world loaded: every teammate on the table has this game's own side towards
   them held at ally (+100) through the relate road - setRelation, forwarded, so the other games and the name tags follow. A
   side is looked at when the pin starts, when any standing with a player faction moved (RelationsPlayerPairEpoch), and every
   kPinLookEveryMs. A pin ENDS when that player is no longer a teammate (left, removed, disbanded, or this game left): the side
   is left where it stands; the RESTORE row that departure makes carries the standing to go back to. */
void PinTick()
{
    const int me = StoreMySlot();
    if (me < 0 || g_tableGen < 0) return;
    const swteam::PinPlan plan = swteam::PlanPins(g_pinned, swteam::TeammatesOf(g_table, (unsigned)me));
    for (size_t i = 0; i < plan.start.size(); ++i)
    {
        ++g_pinStarts; g_pinLookOwed = true;
        float v = 0.0f; const int r = RelationsMineTowards((int)plan.start[i], &v);
        DebugLog("[TEAM] pin started: s" + N((long long)plan.start[i]) + " '" + swteam::Clean(NameOf(plan.start[i])) + "' shares " + TeamWordsOf(plan.start[i])
                 + " with this game - this game's side towards them is held at ally (+100) while both are in it (it stands at " + Value(r, v) + "; pins started " + N(g_pinStarts) + ")");
    }
    for (size_t i = 0; i < plan.end.size(); ++i)
    {
        ++g_pinEnds; g_sideSeen.erase(plan.end[i]); g_sideOwed.erase(plan.end[i]);
        float v = 0.0f; const int r = RelationsMineTowards((int)plan.end[i], &v);
        DebugLog("[TEAM] pin ended: s" + N((long long)plan.end[i]) + " '" + swteam::Clean(NameOf(plan.end[i])) + "' no longer shares a team with this game - this game's side towards them is left at "
                 + Value(r, v) + "; the standing from before joining comes back with the departure's RESTORE row (pins ended " + N(g_pinEnds) + ")");
    }
    if (!plan.start.empty() || !plan.end.empty()) { g_pinned = plan.keep; g_pinned.insert(g_pinned.end(), plan.start.begin(), plan.start.end()); std::sort(g_pinned.begin(), g_pinned.end()); }
    if (g_pinned.empty()) return;
    const long long ep = RelationsPlayerPairEpoch();
    const unsigned long now = ::GetTickCount();
    if (!g_pinLookOwed && ep == g_pinEpoch && now - g_pinLookedAt < kPinLookEveryMs) return;
    g_pinLookOwed = false; g_pinEpoch = ep; g_pinLookedAt = now; ++g_pinLooks;
    for (size_t i = 0; i < g_pinned.size(); ++i)
    {
        float before = 0.0f; std::string why;
        float fv = 0.0f; unsigned flagsBefore = 0;
        if (RelationsMineTowardsFull((int)g_pinned[i], &fv, &flagsBefore) != 1) flagsBefore = 0;
        const int w = RelationsPinAlly((int)g_pinned[i], &before, &why);
        if (swteam::SideReportDue(w, g_sideSeen.count(g_pinned[i]) != 0))
        {   /* 477: the side this first pin write moved - owed to the world server until sent (it keeps the first report) */
            g_sideSeen.insert(g_pinned[i]);
            g_sideOwed[g_pinned[i]] = std::make_pair(before, flagsBefore);
        }
        if (w == 0) g_sideSeen.insert(g_pinned[i]);   /* already at the pin (allies before, or this side was pinned before a reload): nothing to report */
        if (w == 1)
        {
            ++g_pinWrites;
            DebugLog("[TEAM] pin: this game's side towards teammate s" + N((long long)g_pinned[i]) + " '" + swteam::Clean(NameOf(g_pinned[i])) + "' stood at " + Value(1, before)
                     + " - set to ally (+100) through the relate road (pin writes " + N(g_pinWrites) + ")");
        }
        else if (w < 0)
        {
            /* said once per teammate and cause; the same failure again is counted (pin fails, and per pair on `team show`) */
            ++g_pinFails;
            const std::string key = N((long long)g_pinned[i]) + "|" + (w == -1 ? "unreadable" : "relate-refused");
            std::map<std::string, long long>::iterator it = g_pinFailSaid.find(key);
            if (it != g_pinFailSaid.end()) ++it->second;
            else if (g_pinFailSaid.size() < kPinFailCausesKept)
            {
                g_pinFailSaid[key] = 1;
                DebugLog("[TEAM] pin: this game's side towards teammate s" + N((long long)g_pinned[i]) + " not held at this look (" + why + ") - looked at again within "
                         + N((long long)(kPinLookEveryMs / 1000)) + " s; this cause towards this teammate is said once (pin fails " + N(g_pinFails) + ")");
            }
        }
    }
}
/* the sides owed to the world server (the first pin write's), sent while there is a link */
void SendOwedSides()
{
    if (g_sideOwed.empty() || StoreWelcomedThisLink() == 0) return;
    std::map<unsigned, std::pair<float, unsigned> >::iterator it = g_sideOwed.begin();
    while (it != g_sideOwed.end())
    {
        std::vector<char> sb; swteam::EncodeSide(&sb, it->first, it->second.first, it->second.second);
        if (!Send(sb)) return;
        ++g_sides;
        DebugLog("[TEAM] side towards s" + N((long long)it->first) + " before the pin sent: " + Value(1, it->second.first) + " flags " + N((long long)it->second.second)
                 + " (the first pin write moved it; sides " + N(g_sides) + ")");
        g_sideOwed.erase(it++);
    }
}
/* THE TEAM'S RECORD on this game (teamwire.h TeamRec, owner 476 / 477). In a team, with this game's world ready:
   - SEED: the founder's game gives a team that has no record its own standing (NPC factions both ways, its sides towards every
     player outside the team), once per link;
   - WRITE: the record is written onto this game's faction - at a new record and after every world load - as the team's write;
   - GATHER: every kDeltaEveryMs this game's own changes since the record was written: NPC differences go up as a DELTA (put
     back for the next gather when the send fails); a side towards a player outside the team moved on this game (the founder's
     choice, or any member's own engine - owner 512) goes up as the team's STANCE. */
void SendDeltasNow()
{
    std::vector<swteam::Delta> npc; std::vector<int> sides;
    RelationsTeamCollect(&npc, &sides);
    const int me = StoreMySlot();
    const int role = (me < 0 || g_tableGen < 0) ? -1 : swteam::RoleOf(g_table, (unsigned)me);
    if (!npc.empty())
    {
        std::vector<char> b;
        if (swteam::EncodeDeltas(&b, npc) && Send(b))
        {
            ++g_deltaSends;
            if (g_deltaSends <= 30)
            {
                std::string w;
                for (size_t i = 0; i < npc.size() && i < 4; ++i) w += (i ? ", " : "") + npc[i].sid + ((npc[i].dir & 1u) ? " it->mine " : " mine->it ") + Value(1, npc[i].dRel) + " (now " + Value(1, npc[i].rel) + ")";
                DebugLog("[TEAM] record: this game's own change(s) sent as a DELTA - " + N((long long)npc.size()) + ": " + w + (npc.size() > 4 ? ", ..." : "") + " (deltas sent " + N(g_deltaSends) + ")");
            }
        }
        else { ++g_deltaSendFails; RelationsTeamUncollect(npc); if (g_deltaSendFails <= 3) DebugLog("[TEAM] record: a DELTA was NOT sent (no link) - kept for the next gather"); }
    }
    const std::vector<unsigned> mates = (me < 0 || g_tableGen < 0) ? std::vector<unsigned>() : swteam::TeammatesOf(g_table, (unsigned)me);
    for (size_t i = 0; i < sides.size(); ++i)
    {
        const unsigned s = (unsigned)sides[i];
        if ((int)s == me || std::find(mates.begin(), mates.end(), s) != mates.end()) continue;
        if (!swteam::SideMoveIsTeamStance(role)) continue;
        float v = 0.0f; unsigned fl = 0;
        if (RelationsMineTowardsFull((int)s, &v, &fl) != 1) continue;
        std::vector<char> b; swteam::EncodeStance(&b, s, v, fl);
        if (Send(b)) { ++g_stances; DebugLog("[TEAM] record: this game's side towards s" + N((long long)s) + " '" + swteam::Clean(NameOf(s)) + "' sent as the team's stance ("
                                             + (role == 1 ? "the founder" : "a member's own engine") + "): " + Value(1, v) + " flags " + N((long long)fl) + " (stances " + N(g_stances) + ")"); }
    }
    /* the founder's side towards a player the record holds no stance for (that faction appeared after the SEED): sent as a STANCE,
       once against each record generation - not towards a player who has just left the team, nor one a held RESTORE row still
       puts this game's side back towards */
    if (role == 1 && g_rec.seeded && g_recTeam != 0)
    {
        std::vector<swteam::PlayerSide> ps; RelationsPlayerSides(&ps);
        std::vector<unsigned> wait = swteam::HeldSideSlots(g_inbox.held);
        wait.insert(wait.end(), g_fillWait.begin(), g_fillWait.end());
        const std::vector<unsigned> missing = swteam::StancesMissing(ps, g_rec.stances, mates, (unsigned)me, wait);
        for (size_t i = 0; i < missing.size(); ++i)
        {
            std::map<unsigned, unsigned>::const_iterator a = g_stanceAskedGen.find(missing[i]);
            if (a != g_stanceAskedGen.end() && a->second == g_rec.gen) continue;
            for (size_t k = 0; k < ps.size(); ++k)
                if (ps[k].slot == missing[i])
                {
                    std::vector<char> b; swteam::EncodeStance(&b, ps[k].slot, ps[k].rel, (unsigned)(ps[k].flags < 0 ? 0 : ps[k].flags));
                    if (Send(b)) { g_stanceAskedGen[missing[i]] = g_rec.gen; ++g_stances; DebugLog("[TEAM] record: the founder's stance towards s" + N((long long)ps[k].slot) + " (a faction the record had none for) sent: " + Value(1, ps[k].rel)); }
                }
        }
    }
}
void RecordTick()
{
    const int me = StoreMySlot();
    if (me < 0 || g_tableGen < 0) return;
    int founder = 0;
    const int t = swteam::WireTeamOf(g_table, (unsigned)me, &founder);
    if (t < 0)
    {
        if (RelationsTeamBaseValid()) { RelationsTeamForgetBase(); DebugLog("[TEAM] record: this game is in no team now - it no longer follows a team's record"); }
        return;
    }
    if (!WorldReady()) return;
    const unsigned teamNo = g_table[t].no;
    const bool held = g_recTeam == teamNo && g_rec.seeded;
    const bool linked = StoreWelcomedThisLink() != 0;
    if (linked && swteam::SeedDue(founder ? 1 : 0, held, g_seedLink == StoreLinkGen() && g_seedTeam == teamNo))
    {
        g_seedLink = StoreLinkGen(); g_seedTeam = teamNo;
        coopown::FactionRec fr; std::string why;
        if (RelationsNpcRecordOf(-1, &fr, &why))
        {
            swteam::TeamRec r;
            for (size_t i = 0; i < fr.rows.size(); ++i)
            {
                swteam::RecRow x; x.sid = fr.rows[i].sid; x.rel = fr.rows[i].rel; x.trust = fr.rows[i].trust; x.trustNeg = fr.rows[i].trustNeg;
                x.relBack = fr.rows[i].relBack; x.trustBack = fr.rows[i].trustBack; x.trustNegBack = fr.rows[i].trustNegBack;
                x.flags = (unsigned)(fr.rows[i].flags & 3); x.flagsBack = (unsigned)(fr.rows[i].flagsBack & 3); x.have = swteam::kHaveFwd | swteam::kHaveBack;
                r.rows.push_back(x);
            }
            std::vector<swteam::PlayerSide> ps; RelationsPlayerSides(&ps);
            const std::vector<unsigned> mates = swteam::TeammatesOf(g_table, (unsigned)me);
            for (size_t i = 0; i < ps.size(); ++i)
            {
                if (std::find(mates.begin(), mates.end(), ps[i].slot) != mates.end()) continue;
                swteam::RecStance x; x.slot = ps[i].slot; x.rel = ps[i].rel; x.flags = (unsigned)(ps[i].flags < 0 ? 0 : ps[i].flags); r.stances.push_back(x);
            }
            std::vector<char> b;
            if (swteam::EncodeSeed(&b, r) && Send(b)) { ++g_seeds; RelationsTeamBaseFromRec(r); g_seedBase = true; DebugLog("[TEAM] record: the founder's standing sent as team " + N((long long)teamNo) + "'s SEED - " + N((long long)r.rows.size()) + " NPC factions, " + N((long long)r.stances.size()) + " stances (seeds " + N(g_seeds) + ")"); }
            else DebugLog("[TEAM] record: the SEED was NOT sent (too large or no link) - asked again on the next link");
        }
        else DebugLog("[TEAM] record: the founder's standing could not be read for the SEED (" + why + ") - asked again on the next link");
    }
    const unsigned long now = ::GetTickCount();
    if (swteam::RecordWriteDue(g_recOwed, true, held, g_rec.seeded != 0) && swteam::RecordWriteSafe(linked, RelationsTeamBaseValid())
        && (g_recWriteTriedAt == 0 || now - g_recWriteTriedAt >= swteam::kDeltaEveryMs))
    {
        if (RelationsTeamBaseValid()) SendDeltasNow();   /* this game's own changes since its base go up before the record is written over them */
        std::string detail;
        if (RelationsTeamApplyRecord(g_rec, TeamMatesOfMine(), &detail))
        {
            g_recOwed = false; ++g_recordWrites; g_seedBase = false; g_recWriteTriedAt = 0;
            if (g_recWrittenGen != g_rec.gen || g_recordWrites <= 3) DebugLog("[TEAM] record: team " + N((long long)teamNo) + "'s record written onto this game's faction as the team's write - " + detail + " (writes " + N(g_recordWrites) + ")");
            g_recWrittenGen = g_rec.gen;
        }
        else { ++g_recordWriteFails; g_recWriteTriedAt = now; if (g_recordWriteFails <= 3) DebugLog("[TEAM] record: NOT wholly written (" + detail + ") - the record stays owed, written again within a second"); }
    }
    SendOwedSides();
    if (swteam::GatherDue(RelationsTeamBaseValid(), linked, held || (g_seedBase && g_seedTeam == teamNo)) && now - g_deltaAt >= swteam::kDeltaEveryMs) { g_deltaAt = now; SendDeltasNow(); }
}
/* a line logged at most 5 times, then every 50th */
bool ResSay(long long n) { return n <= 5 || n % 50 == 0; }
/* the shared research, every frame: this game's own finishes taken from the hook; with the player in a team and the world ready,
   a held whole research loaded once, held teammates' techs applied once each (by the road the queue allows at that moment), the
   whole finished list sent once per world load, link and team until the world server accepts it, and this player's own finishes
   sent (the echo rule) until it accepts them - a list refused or not answered is sent again after a growing wait */
void ResearchTick()
{
    std::vector<std::string> own; long long dropped = 0;
    StoreResearchTakeOwnFinished(&own, &dropped);
    if (dropped != g_resDroppedSaid) { g_resDroppedSaid = dropped; DebugLog("[TEAM] research: " + N(dropped) + " finished tech(s) dropped past the hook's list cap - the whole list sent at the next load carries them"); }
    for (size_t i = 0; i < own.size(); ++i)
    {
        g_resPending.push_back(own[i]);
        DebugLog("[TEAM] research: this game finished " + own[i] + " (" + StoreResearchNameOf(own[i]) + ")");
    }
    const int me = StoreMySlot();
    int founder = 0;
    const int t = (me < 0 || g_tableGen < 0) ? -1 : swteam::WireTeamOf(g_table, (unsigned)me, &founder);
    const unsigned myTeam = t < 0 ? 0u : g_table[t].no;
    if (myTeam == 0)   /* in no team: the whole list sent when the player enters one (the same team again included) carries them */
    {
        g_resPending.clear(); g_resOwnFlight.clear(); g_resOwnResends = 0;
        g_resWholeTeam = 0; g_resWholeLink = -1; g_resFlightTeam = 0; g_resFlightLink = -1; g_resFlightAt = 0; g_resFlightResends = 0; g_resFlightList.clear();
        return;
    }
    if (!WorldReady()) return;
    const unsigned long now = ::GetTickCount();
    if (StoreResearchOff() && (g_resWholeOwed || !g_resIncoming.empty()))
    {
        g_resOffDropped += (long long)g_resIncoming.size() + (g_resWholeOwed ? 1 : 0);
        g_resWholeOwed = false; g_resIncoming.clear();
        if (!g_resOffSaid) { g_resOffSaid = true; DebugLog("[TEAM] research: loads and applies are OFF for this process (an engine fault - see the [OWNSAVE] line) - the team's research received is not applied here"); }
    }
    if (g_resWholeOwed && (g_resLoadTriedAt == 0 || now - g_resLoadTriedAt >= kResRetryMs))
    {
        if (!swteam::ResearchForMyTeam(myTeam, g_resWholeMsgTeam))
        {
            g_resWholeOwed = false; ++g_resNotMine;
            DebugLog("[TEAM] research: the whole research of team " + N((long long)g_resWholeMsgTeam) + " dropped - this game's player is in team " + N((long long)myTeam));
        }
        else
        {
            int added = 0; std::string detail;
            const std::string w = StoreResearchLoadUnion(g_resWhole, &added, &detail);
            if (!w.empty())
            {
                ++g_resLoadFails; g_resLoadTriedAt = now;
                if (w == "fault" || w == "off") g_resWholeOwed = false;   /* the research category is off for this process */
                if (ResSay(g_resLoadFails)) DebugLog("[TEAM] research: team " + N((long long)myTeam) + "'s research NOT loaded (" + w + ")" + (g_resWholeOwed ? " - tried again in " + N((long long)(kResRetryMs / 1000)) + " s" : std::string(" - not tried again")) + " (loadFails " + N(g_resLoadFails) + ")");
            }
            else
            {
                g_resWholeOwed = false; g_resLoadTriedAt = 0; ++g_resLoads; g_resLoadedTechs += added;
                for (size_t i = 0; i < g_resWhole.size(); ++i) g_resNotOwn.insert(g_resWhole[i]);
                if (added > 0)
                {
                    DebugLog("[TEAM] research: team " + N((long long)myTeam) + "'s research loaded ONCE through Research::load, own queue kept - " + detail + ": \"" + swteam::ResearchSharedLine(added) + "\" (loads " + N(g_resLoads) + ")");
                    ShowLine(swteam::ResearchSharedLine(added), "the team's research loaded");
                }
                else DebugLog("[TEAM] research: team " + N((long long)myTeam) + "'s research holds nothing new for this game - " + detail + " - nothing loaded (loads " + N(g_resLoads) + ")");
            }
        }
    }
    if (!g_resIncoming.empty() && !StoreResearchOff())   /* off after a fault: the research object is not read again */
    {
        std::vector<std::string> mine; long long q = 0; std::string why;
        if (!StoreResearchFinished(&mine, &q, &why))
        {
            ++g_resReadFails;
            if (g_resReadFails <= 3) DebugLog("[TEAM] research: this game's research could not be read (" + why + ") - the teammates' techs wait");
        }
        else
        {
            std::vector<ResIncoming> in; in.swap(g_resIncoming);
            for (size_t i = 0; i < in.size(); ++i)
            {
                if (!swteam::ResearchForMyTeam(myTeam, in[i].team)) { ++g_resNotMine; continue; }
                const std::vector<std::string> one(1, in[i].sid);
                std::set<std::string> known; known.insert(in[i].sid);   /* StoreResearchApply looks the record up */
                if (swteam::ResearchNewToMe(one, mine, known).empty())
                {
                    ++g_resAlready; g_resNotOwn.insert(in[i].sid);
                    DebugLog("[TEAM] research: " + in[i].sid + " from s" + N((long long)in[i].by) + " is already finished here - nothing applied (already " + N(g_resAlready) + ")");
                    continue;
                }
                std::string name, w; int route = swteam::kResViaSetResearched;
                const int k = StoreResearchApply(in[i].sid, false, &name, &w, &route);
                if (k == 1)
                {
                    g_resNotOwn.insert(in[i].sid); mine.push_back(in[i].sid);
                    if (route == swteam::kResViaLoad) ++g_resAppliedByLoad; else ++g_resAppliedBySet;
                    DebugLog("[TEAM] research: " + in[i].sid + " from s" + N((long long)in[i].by) + " applied " + (route == swteam::kResViaLoad
                             ? std::string("through Research::load (queued behind this game's front tech; taken out of the queue, the rest kept)") : std::string("through setResearched"))
                             + ": \"" + swteam::ResearchCompleteLine(name, NameOf(in[i].by)) + "\" (appliedBySet " + N(g_resAppliedBySet) + ", appliedByLoad " + N(g_resAppliedByLoad) + ")");
                    ShowLine(teamscreen::ResearchCompleteShown(name, ScreenNameOf(in[i].by)), "a teammate's tech applied");
                }
                else
                {
                    ++g_resApplyFails;
                    DebugLog("[TEAM] research: " + in[i].sid + " from s" + N((long long)in[i].by) + " NOT applied (" + w + ") - the whole research at the next load carries it (applyFails " + N(g_resApplyFails) + ")");
                }
            }
        }
    }
    if (StoreWelcomedThisLink() == 0) return;
    const int link = StoreLinkGen();
    if (!StoreResearchOff() && swteam::ResearchWholeDue(myTeam, g_resWholeTeam, g_resWholeLink, link))
    {
        const bool flying = g_resFlightTeam == myTeam && g_resFlightLink == link;
        if (flying && now - g_resFlightAt < swteam::ResearchResendWaitMs(g_resFlightResends)) return;   /* waiting for the answer */
        if (g_resReadTriedAt != 0 && now - g_resReadTriedAt < kResRetryMs) return;
        std::vector<std::string> mine; long long q = 0; std::string why;
        if (!StoreResearchFinished(&mine, &q, &why))
        {
            g_resReadTriedAt = now; ++g_resReadFails;
            if (g_resReadFails <= 3) DebugLog("[TEAM] research: this game's finished list could not be read (" + why + ") - tried again in " + N((long long)(kResRetryMs / 1000)) + " s");
            return;
        }
        g_resReadTriedAt = 0;
        if (flying) ++g_resFlightResends; else g_resFlightResends = 0;
        g_resFlightTeam = myTeam; g_resFlightLink = link; g_resFlightAt = now;   /* sent or not, the next try waits its turn */
        std::vector<char> b;
        if (!swteam::EncodeResearchUp(&b, true, mine))
        {
            ++g_resEncodeFails;
            if (ResSay(g_resEncodeFails)) DebugLog("[TEAM] research: this game's whole finished list (" + N((long long)mine.size()) + " techs) cannot be encoded (more than " + N((long long)swteam::kMaxTechs) + " techs or a stringID too long) - tried again in " + N((long long)(swteam::ResearchResendWaitMs(g_resFlightResends) / 1000)) + " s (encodeFails " + N(g_resEncodeFails) + ")");
            return;
        }
        if (!Send(b))
        {
            ++g_resNotSent;
            if (ResSay(g_resNotSent)) DebugLog("[TEAM] research: the whole finished list was NOT sent (no link) - tried again in " + N((long long)(swteam::ResearchResendWaitMs(g_resFlightResends) / 1000)) + " s (notSent " + N(g_resNotSent) + ")");
            return;
        }
        g_resFlightList = mine; ++g_resWholeSends; if (flying) ++g_resWholeResends;
        DebugLog("[TEAM] research: this game's whole finished list " + std::string(flying ? "sent AGAIN" : "sent") + " for team " + N((long long)myTeam) + " - " + N((long long)mine.size()) + " tech(s), queue " + N(q)
                 + " - kept until the world server accepts it (whole sends " + N(g_resWholeSends) + ", resends " + N(g_resWholeResends) + ")");
        return;
    }
    if (!g_resOwnFlight.empty())
    {
        if (now - g_resOwnAt < swteam::ResearchResendWaitMs(g_resOwnResends)) return;   /* waiting for the answer */
        const std::vector<std::string> more = swteam::ResearchOwnToSend(g_resPending, g_resNotOwn, g_resHeld);
        std::vector<std::string> up = g_resOwnFlight;
        for (size_t i = 0; i < more.size(); ++i) if (std::find(up.begin(), up.end(), more[i]) == up.end()) up.push_back(more[i]);
        g_resPending.clear(); g_resOwnFlight = up; g_resOwnAt = now; ++g_resOwnResends;
        std::vector<char> b;
        if (!swteam::EncodeResearchUp(&b, false, up))
        {
            ++g_resEncodeFails;
            if (ResSay(g_resEncodeFails)) DebugLog("[TEAM] research: this player's " + N((long long)up.size()) + " own finish(es) cannot be encoded (more than " + N((long long)swteam::kMaxTechs) + " or a stringID too long) - dropped; the whole list at the next load carries them (encodeFails " + N(g_resEncodeFails) + ")");
            g_resOwnFlight.clear(); g_resOwnResends = 0;
            return;
        }
        if (!Send(b)) { ++g_resNotSent; if (ResSay(g_resNotSent)) DebugLog("[TEAM] research: this player's own finishes were NOT sent again (no link) (notSent " + N(g_resNotSent) + ")"); return; }
        ++g_resOwnResendCount;
        DebugLog("[TEAM] research: this player's " + N((long long)up.size()) + " own finish(es) sent AGAIN to team " + N((long long)myTeam) + " - not accepted yet (own resends " + N(g_resOwnResendCount) + ", next wait " + N((long long)(swteam::ResearchResendWaitMs(g_resOwnResends) / 1000)) + " s)");
        return;
    }
    if (g_resPending.empty()) return;
    const std::vector<std::string> up = swteam::ResearchOwnToSend(g_resPending, g_resNotOwn, g_resHeld);
    if (up.empty()) { g_resPending.clear(); return; }
    std::vector<char> b;
    if (!swteam::EncodeResearchUp(&b, false, up))
    {
        ++g_resEncodeFails;
        if (ResSay(g_resEncodeFails)) DebugLog("[TEAM] research: this player's " + N((long long)up.size()) + " own finish(es) cannot be encoded (more than " + N((long long)swteam::kMaxTechs) + " or a stringID too long) - dropped; the whole list at the next load carries them (encodeFails " + N(g_resEncodeFails) + ")");
        g_resPending.clear();
        return;
    }
    if (!Send(b)) return;   /* kept in g_resPending: tried again next frame */
    g_resPending.clear(); g_resOwnFlight = up; g_resOwnAt = now; g_resOwnResends = 0; ++g_resOwnSends;
    for (size_t i = 0; i < up.size(); ++i) DebugLog("[TEAM] research: " + up[i] + " (" + StoreResearchNameOf(up[i]) + "), finished by this game's player, sent to team " + N((long long)myTeam) + " - kept until the world server accepts it (own sends " + N(g_resOwnSends) + ")");
}
/* the counters: sends / resends / answers of the whole list and of own finishes; refused = NOTICE research-whole / -own for this
   game; loadedTechs = techs the whole-research loads added; appliedBySet / appliedByLoad = teammates' techs applied one by one,
   through setResearched or (queued behind the front) Research::load; notOwn = techs that came from the team (never sent as this
   player's own: the whole research's techs and the teammates' techs, applied here or already finished) */
std::string ResearchText()
{
    return "research: wholeAcceptedTeam=" + N((long long)g_resWholeTeam) + " wholeInFlight=" + N(g_resFlightTeam != 0 ? 1 : 0) + " held=" + N((long long)g_resHeld.size())
           + " notOwn=" + N((long long)g_resNotOwn.size()) + " pending=" + N((long long)g_resPending.size()) + " ownInFlight=" + N((long long)g_resOwnFlight.size())
           + " in=" + N(g_resIn) + " wholeSends=" + N(g_resWholeSends) + " wholeResends=" + N(g_resWholeResends) + " wholeAnswers=" + N(g_resWholeAnswers)
           + " ownSends=" + N(g_resOwnSends) + " ownResends=" + N(g_resOwnResendCount) + " ownAnswers=" + N(g_resOwnAnswers) + " refused=" + N(g_resRefused)
           + " notSent=" + N(g_resNotSent) + " encodeFails=" + N(g_resEncodeFails) + " loads=" + N(g_resLoads) + " loadedTechs=" + N(g_resLoadedTechs)
           + " loadFails=" + N(g_resLoadFails) + " appliedBySet=" + N(g_resAppliedBySet) + " appliedByLoad=" + N(g_resAppliedByLoad) + " applyFails=" + N(g_resApplyFails)
           + " already=" + N(g_resAlready) + " notMine=" + N(g_resNotMine) + " off=" + N(StoreResearchOff() ? 1 : 0) + " offDropped=" + N(g_resOffDropped);
}
/* the box stub's cells: every teammate's faction here (a stand-in met this session, else the coop-p<n> faction the save carries) */
void CellsTick()
{
    const unsigned long now = ::GetTickCount();
    if (!g_cellsOwed && now - g_cellsAt < kCellsEveryMs) return;
    g_cellsOwed = false; g_cellsAt = now;
    void* facs[16]; int n = 0;
    const int me = MySlotForWire();   /* the slot the ownership hook judges by (policy.cpp), so the hook and the cells agree */
    if (me >= 0 && g_tableGen >= 0)
    {
        const std::vector<unsigned> mates = swteam::TeammatesOf(g_table, (unsigned)me);
        for (size_t i = 0; i < mates.size() && n < 16; ++i)
        {
            ::Faction* f = StandInForSlot((int)mates[i]);
            if (f == 0) f = StandInRecordFaction((int)mates[i]);
            if (f != 0) facs[n++] = (void*)f;
        }
    }
    PolicyTeammateFactions(facs, n);
}
std::string PinsText()
{
    std::string s = g_pinned.empty() ? std::string("none") : std::string();
    for (size_t i = 0; i < g_pinned.size(); ++i) s += (i ? ",s" : "s") + N((long long)g_pinned[i]);
    std::string fails;
    for (std::map<std::string, long long>::const_iterator it = g_pinFailSaid.begin(); it != g_pinFailSaid.end(); ++it)
        fails += (fails.empty() ? "; fails by teammate|cause: s" : ", s") + it->first + " x" + N(it->second);
    return s + " (started " + N(g_pinStarts) + " ended " + N(g_pinEnds) + " writes " + N(g_pinWrites) + " fails " + N(g_pinFails) + " looks " + N(g_pinLooks) + " dropped " + N(g_pinDropped) + fails + ")";
}
}   // namespace

void TeamArrive(const std::vector<char>& payload)
{
    swteam::Down d;
    if (!swteam::DecodeDown(payload.empty() ? 0 : &payload[0], payload.size(), &d))
    {
        ++g_malformed;
        if (g_malformed <= 5) ErrorLog("[TEAM] a TEAM message of " + N((long long)payload.size()) + " bytes did not decode - ignored (malformed " + N(g_malformed) + ")");
        return;
    }
    ++g_in;
    const int me = StoreMySlot();
    if (d.kind == swteam::kDnTable)
    {
        g_table = d.table; g_tableGen = StoreLinkGen();
        {   /* teammates who left (left, removed, or the team ended): the founder's fill waits for the next record; the marks of this
               game's side towards them not gathered yet (the pin's own writes) are dropped, so no gather sends the pin's value as the
               team's stance; the own-change number now is kept (an own change after it wins over the departure's side). The last
               table's teammates are another world's when the world changed: nobody has left this one. */
            const std::string world = StoreNotebookDir();
            if (world != g_lastMatesWorld) { g_lastMatesWorld = world; g_lastMates.clear(); g_fillWait.clear(); g_pinEndNo.clear(); }
            const std::vector<unsigned> mates = me >= 0 ? swteam::TeammatesOf(g_table, (unsigned)me) : std::vector<unsigned>();
            const std::vector<unsigned> gone = swteam::DepartedMates(g_lastMates, mates);
            std::string gw;
            for (size_t i = 0; i < gone.size(); ++i) { g_fillWait.insert(gone[i]); g_pinEndNo[gone[i]] = RelationsSideNoNow(); gw += (i ? ",s" : "s") + N((long long)gone[i]); }
            for (size_t i = 0; i < mates.size(); ++i) g_pinEndNo.erase(mates[i]);
            if (!gone.empty())
            {
                const int dropped = RelationsTeamDropSideMarks(gone);
                DebugLog("[TEAM] teammate(s) " + gw + " left this game's team - no stance towards them is filled from this game until the next record; "
                         + N((long long)dropped) + " unsent mark(s) of this game's side towards them dropped");
            }
            g_lastMates = mates;
        }
        PublishIndex();
        RememberNames();
        const std::string t = swteam::TableText(g_table);
        g_cellsOwed = true;
        if (t != g_tableSaid) { g_tableSaid = t; TagsCaptionsDirty(); DebugLog("[TEAM] table from the world server: " + t + " | " + MineText()); }
    }
    else if (d.kind == swteam::kDnInvited)
    {
        if (d.secondsLeft == 0) { g_invite = WaitingInvite(); DebugLog("[TEAM] invitation from " + NameOf(d.fromSlot) + " withdrawn"); return; }
        g_invite.on = 1; g_invite.fromSlot = d.fromSlot; g_invite.teamName = d.teamName; g_invite.at = ::GetTickCount(); g_invite.serial = ++g_inviteSerial; g_invite.answered = false;
        DebugLog("[TEAM] invitation from s" + N((long long)d.fromSlot) + " to join '" + swteam::Clean(d.teamName) + "' for " + N((long long)d.secondsLeft)
                 + " s (invitation " + N((long long)g_invite.serial) + "; its FACTION INVITATION box shows while it waits)");
    }
    else if (d.kind == swteam::kDnNotice)
    {
        /* this game's waiting invitation ends when a notice is about this game's own invitation (joined / declined / expired /
           gone, with this game the subject), or refuses this game's OWN answer (actor = this game) in a way that ends it */
        const bool aboutMyInvite = me >= 0 && d.subjectSlot == (unsigned)me
                                   && (d.event == swteam::kEvJoined || d.event == swteam::kEvDeclined || d.event == swteam::kEvExpired || d.event == swteam::kEvInviteGone);
        const bool myAnswerRefused = me >= 0 && d.event == swteam::kEvAnswer && d.actorSlot == (unsigned)me && swteam::AnswerRefusalEndsInvite((int)d.result);
        if (aboutMyInvite || myAnswerRefused) g_invite = WaitingInvite();
        if (g_invite.on && g_invite.answered && teamscreen::InviteBackAfterRefusal((int)d.event, (int)d.result, me >= 0 && d.actorSlot == (unsigned)me))
        {
            g_invite.answered = false;
            DebugLog("[TEAM] this game's ACCEPT was refused (" + std::string(swteam::ResultName((int)d.result)) + ") and invitation " + N((long long)g_invite.serial) + " still waits - its box comes back");
        }
        /* this game's research list refused: kept, and sent again after its growing wait, counted from now */
        if (me >= 0 && d.actorSlot == (unsigned)me && d.result != swteam::kOk && (d.event == swteam::kEvResearchWhole || d.event == swteam::kEvResearchOwn))
        {
            ++g_resRefused;
            const bool whole = d.event == swteam::kEvResearchWhole;
            if (whole) g_resFlightAt = ::GetTickCount(); else g_resOwnAt = ::GetTickCount();
            DebugLog("[TEAM] research: the world server refused this game's " + std::string(whole ? "whole finished list" : "own finishes") + " (" + swteam::ResultName((int)d.result) + ") - kept, sent again in "
                     + N((long long)(swteam::ResearchResendWaitMs(whole ? g_resFlightResends : g_resOwnResends) / 1000)) + " s (refused " + N(g_resRefused) + ")");
        }
        DebugLog("[TEAM] notice " + std::string(swteam::EventName((int)d.event)) + " result=" + swteam::ResultName((int)d.result) + " actor=s"
                 + (d.actorSlot == 0xFFFFFFFFu ? std::string("?") : N((long long)d.actorSlot)) + " subject=s" + (d.subjectSlot == 0xFFFFFFFFu ? std::string("?") : N((long long)d.subjectSlot))
                 + ": \"" + NoticeLine(d) + "\"");
        ShowLine(teamscreen::NoticeLine((int)d.event, (int)d.result, me >= 0 && d.subjectSlot == (unsigned)me, ScreenNameOf(d.subjectSlot), d.teamName),
                 std::string("the notice ") + swteam::EventName((int)d.event) + " " + swteam::ResultName((int)d.result));
    }
    else if (d.kind == swteam::kDnRecord)
    {
        /* this game's own changes gathered against the record it last wrote go up first, then the new record is owed */
        if (RelationsTeamBaseValid() && StoreWelcomedThisLink() != 0) SendDeltasNow();
        const bool news = d.recTeam != g_recTeam || d.rec.gen != g_rec.gen;
        if (!g_fillWait.empty())
        {
            std::string gw;
            for (std::set<unsigned>::const_iterator it = g_fillWait.begin(); it != g_fillWait.end(); ++it) gw += (gw.empty() ? "s" : ",s") + N((long long)*it);
            DebugLog("[TEAM] record of team " + N((long long)d.recTeam) + " gen " + N((long long)d.rec.gen) + " after the departure of " + gw + " - " + N((long long)d.rec.stances.size())
                     + " stance(s); the founder's fill no longer waits for them");
            g_fillWait.clear();
        }
        g_rec = d.rec; g_recTeam = d.recTeam; g_recOwed = true; ++g_records;
        if (news && (g_records <= 40 || g_records % 50 == 0))
            DebugLog("[TEAM] record of team " + N((long long)d.recTeam) + " gen " + N((long long)d.rec.gen) + " from the world server: " + N((long long)d.rec.rows.size()) + " NPC factions, "
                     + N((long long)d.rec.stances.size()) + " stances" + (d.rec.seeded ? std::string() : std::string(" (not seeded)")) + " - owed onto this game's faction (records " + N(g_records) + ")");
    }
    else if (d.kind == swteam::kDnResearch)
    {
        ++g_resIn;
        for (size_t i = 0; i < d.techs.size(); ++i) g_resHeld.insert(d.techs[i]);
        const int kind = swteam::ResearchDownKind(d.resWhole != 0, d.resBy, me);
        if (kind == swteam::kResDnWholeAnswer && g_resFlightTeam != 0 && d.resTeam == g_resFlightTeam)
        {
            /* this game's whole list accepted: it went once for this team and link; its own finishes it carried are covered */
            g_resWholeTeam = g_resFlightTeam; g_resWholeLink = g_resFlightLink; ++g_resWholeAnswers;
            g_resPending = swteam::ResearchMinus(g_resPending, g_resFlightList);
            g_resOwnFlight = swteam::ResearchMinus(g_resOwnFlight, g_resFlightList);
            g_resFlightTeam = 0; g_resFlightLink = -1; g_resFlightResends = 0; g_resFlightList.clear();
            DebugLog("[TEAM] research: the world server accepted this game's whole finished list for team " + N((long long)d.resTeam) + " (whole answers " + N(g_resWholeAnswers) + ")");
        }
        if (kind == swteam::kResDnOwnAnswer)
        {
            g_resOwnFlight = swteam::ResearchMinus(g_resOwnFlight, d.techs); ++g_resOwnAnswers;
            if (g_resOwnFlight.empty()) g_resOwnResends = 0;
            DebugLog("[TEAM] research: the world server accepted " + N((long long)d.techs.size()) + " own finish(es) of this player for team " + N((long long)d.resTeam) + " - " + N((long long)g_resOwnFlight.size()) + " still unanswered (own answers " + N(g_resOwnAnswers) + ")");
        }
        else if (d.resWhole)
        {
            g_resWhole = d.techs; g_resWholeMsgTeam = d.resTeam; g_resWholeOwed = true; g_resLoadTriedAt = 0;
            DebugLog("[TEAM] research: team " + N((long long)d.resTeam) + "'s whole research from the world server (" + (kind == swteam::kResDnWholeAnswer ? std::string("the answer to this game's list") : std::string("the team's grew")) + ") - "
                     + N((long long)d.techs.size()) + " tech(s); loaded once this game's world is ready");
        }
        else
            for (size_t i = 0; i < d.techs.size(); ++i)
            {
                if (g_resIncoming.size() >= swteam::kMaxTechs) { DebugLog("[TEAM] research: a teammate's tech dropped past the held cap - the whole research at the next load carries it"); break; }
                ResIncoming x; x.sid = d.techs[i]; x.by = d.resBy; x.team = d.resTeam; g_resIncoming.push_back(x);
                DebugLog("[TEAM] research: " + d.techs[i] + " finished by s" + N((long long)d.resBy) + " (" + NameOf(d.resBy) + ") in team " + N((long long)d.resTeam) + " received");
            }
    }
    else if (d.kind == swteam::kDnRestore)
    {
        ++g_restores; if (d.away) ++g_restoresAway;
        const std::string worldWas = g_inbox.world;
        const int k = swteam::InboxArrive(&g_inbox, StoreNotebookDir(), d);
        if (g_inbox.world != worldWas) { g_restoreProgress.clear(); g_restoreFailsOf.clear(); g_restoreNextTry.clear(); g_rowArrivedNo.clear(); }   /* row numbers are per world */
        if (k == swteam::kInHeld && g_rowArrivedNo.find(d.no) == g_rowArrivedNo.end()) g_rowArrivedNo[d.no] = RelationsSideNoNow();
        if (k == swteam::kInAgainDone)
        {
            ++g_restoresAgain;
            DebugLog("[TEAM] restore #" + N((long long)d.no) + " received again - already answered here: RESTORE_DONE sent again, nothing applied twice");
            SendDone(d.no);
            return;
        }
        if (k == swteam::kInAgainHeld) { ++g_restoresAgain; DebugLog("[TEAM] restore #" + N((long long)d.no) + " received again - already held until the world is loaded"); return; }
        DebugLog("[TEAM] restore #" + N((long long)d.no) + " received " + (EngineWritesBlocked() ? "with no world loaded - held until this game's world is loaded" : "in play")
                 + ": " + swteam::WhyName((int)d.why) + " from '" + swteam::Clean(d.teamName) + "'" + (d.away ? " while away" : "") + " (restores " + N(g_restores)
                 + ", while away " + N(g_restoresAway) + ")");
        ReleaseHeld();
    }
}

void TeamTick()
{
    /* a world torn down or loading: the box stub's cells are emptied, so no faction of the next world can match an old teammate's
       freed address; they are filled again at the first tick after the load */
    if (EngineWritesBlocked()) { PolicyTeammateFactions(0, 0); g_cellsOwed = true; return; }
    FlushLines();
    ReleaseHeld();
    PinTick();
    RecordTick();
    ResearchTick();
    CellsTick();
    const int gen = StoreLinkGen();
    if (g_tableGen == gen || g_askedGen == gen || StoreWelcomedThisLink() == 0) return;
    g_askedGen = gen;   /* once per link and per clearing, sent or not: a new link's WELCOME brings the table anyway */
    std::vector<char> b; swteam::EncodeAsk(&b);
    ++g_asks;
    DebugLog(std::string("[TEAM] no table from this link since this game's copy was cleared - ") + (Send(b) ? "asked the world server for it" : "the ASK was NOT sent (no link)"));
}

void TeamForget(const char* why, bool linkLost)
{
    if (linkLost) g_heldLines = teamscreen::HeldLines();   /* a lost link: lines held for this world are not shown in the next */
    const size_t held = linkLost ? g_inbox.held.size() : 0;
    const bool inv = linkLost && g_invite.on != 0;
    if (g_tableGen >= 0 || held != 0 || inv)
    {
        ++g_cleared;
        DebugLog(std::string("[TEAM] this game's copy of the table cleared (") + why + ")"
                 + (held ? "; " + N((long long)held) + " held restore row(s) dropped - the world server sends them again at the next join" : std::string())
                 + (inv ? "; the waiting invitation ended with the connection" : std::string()));
    }
    if (!g_pinned.empty())
    {
        g_pinDropped += (long long)g_pinned.size();
        DebugLog("[TEAM] " + N((long long)g_pinned.size()) + " pin(s) dropped with the table copy - standing unchanged; they start again from the next table");
    }
    const bool hadTable = g_tableGen >= 0;
    g_table.clear(); g_tableGen = -1; g_askedGen = -1; g_tableSaid.clear();
    g_pinned.clear(); g_pinLookOwed = false;
    g_pinFailSaid.clear();
    PolicyTeammateFactions(0, 0); g_cellsOwed = true;   /* the box stub's cells empty: the factions they named may be freed */
    PublishIndex();
    if (hadTable) TagsCaptionsDirty();
    if (linkLost) { g_inbox.held.clear(); g_invite = WaitingInvite(); }
    g_recOwed = true;   /* the next world takes the record again, after its own load restore */
    g_restoreTriedAt = 0; g_recWriteTriedAt = 0; g_seedBase = false; g_sideSeen.clear();
    if (linkLost) { g_seedLink = -1; g_restoreFailsOf.clear(); g_restoreNextTry.clear(); }   /* a row sent again on the next link is tried at once; what of it was written stays written */
    /* the shared research starts again from the next table: the whole list goes up once more after the next load (or link), and
       what was received for the old world or link is answered by that list's reply */
    g_resWholeTeam = 0; g_resWholeLink = -1; g_resFlightTeam = 0; g_resFlightLink = -1; g_resFlightAt = 0; g_resFlightResends = 0; g_resFlightList.clear();
    g_resLoadTriedAt = 0; g_resReadTriedAt = 0; g_resWholeOwed = false; g_resWhole.clear(); g_resIncoming.clear();
    g_resPending.clear(); g_resOwnFlight.clear(); g_resOwnAt = 0; g_resOwnResends = 0; g_resNotOwn.clear(); g_resHeld.clear();
}

bool TeamStanceIsFounders(int slot)
{
    const int me = StoreMySlot();
    if (me < 0 || slot < 0 || g_tableGen < 0) return false;
    const std::vector<unsigned> mates = swteam::TeammatesOf(g_table, (unsigned)me);
    return swteam::StanceIsFounders(swteam::RoleOf(g_table, (unsigned)me), slot == me || std::find(mates.begin(), mates.end(), (unsigned)slot) != mates.end());
}
std::vector<int> TeamFanOutTargets(int slot)
{
    std::vector<int> out;
    const int me = StoreMySlot();
    if (me < 0 || slot < 0 || g_tableGen < 0) return out;
    const std::vector<unsigned> v = swteam::FanOutTargets(g_table, (unsigned)me, (unsigned)slot);
    for (size_t i = 0; i < v.size(); ++i) out.push_back((int)v[i]);
    return out;
}
std::vector<int> TeamMatesOfMine()
{
    std::vector<int> out;
    const int me = StoreMySlot();
    if (me < 0 || g_tableGen < 0) return out;
    const std::vector<unsigned> m = swteam::TeammatesOf(g_table, (unsigned)me);
    for (size_t i = 0; i < m.size(); ++i) out.push_back((int)m[i]);
    return out;
}
unsigned TeamNoOfSlotAnyThread(int slot)
{
    return (slot >= 0 && (unsigned)slot < swteam::kSlotIndexCap) ? (unsigned)g_teamOfSlot[slot] : 0u;
}
bool TeamSameAnyThread(int a, int b)
{
    if (a < 0 || b < 0 || a == b) return false;
    const unsigned ta = TeamNoOfSlotAnyThread(a);
    return ta != 0 && ta == TeamNoOfSlotAnyThread(b);
}
void TeamTagFor(int slot, const std::string& own, std::string* line2, bool* teammate)
{
    *line2 = own; *teammate = false;
    if (slot < 0 || g_tableGen < 0) return;
    const int t = swteam::WireTeamOf(g_table, (unsigned)slot, 0);
    if (t < 0) return;
    const int me = StoreMySlot();
    *teammate = me >= 0 && swteam::SameTeam(g_table, (unsigned)me, (unsigned)slot);
    *line2 = swteam::TagFactionLine(own, true, FactionWordsOf((int)g_table[t].founderSlot), g_table[t].name);
}

std::string TeamCommand(const std::string& args)
{
    std::istringstream is(args);
    std::string sub; is >> sub;
    std::vector<char> b;
    if (sub == "show")
    {
        const std::string tableFrom = g_tableGen < 0 ? std::string("none") : g_tableGen == StoreLinkGen() ? std::string("this link") : std::string("an earlier link");
        std::string inv = "none";
        if (g_invite.on) inv = "from s" + N((long long)g_invite.fromSlot) + " to '" + swteam::Clean(g_invite.teamName) + "' " + N((long long)((::GetTickCount() - g_invite.at) / 1000)) + " s ago";
        if (g_invite.on) inv += " (invitation " + N((long long)g_invite.serial) + (g_invite.answered ? ", answered)" : ", waiting)");
        DebugLog("[TEAM] show: " + MineText() + " | table: " + swteam::TableText(g_table) + " (from " + tableFrom + ") | invitation: " + inv
                 + " | lines shown=" + N(g_linesShown) + " notShown=" + N(g_linesNotShown) + " noWords=" + N(g_linesNoWords)
                 + " | in=" + N(g_in) + " malformed=" + N(g_malformed) + " sent=" + N(g_sent) + " sendFailed=" + N(g_sendFailed) + " restores=" + N(g_restores)
                 + " restoresAway=" + N(g_restoresAway) + " restoresAgain=" + N(g_restoresAgain) + " answered=" + N(g_restoresDone) + " held=" + N((long long)g_inbox.held.size())
                 + " asks=" + N(g_asks) + " cleared=" + N(g_cleared) + " | pins: " + PinsText()
                 + " | record: team=" + N((long long)g_recTeam) + " gen=" + N((long long)g_rec.gen) + " seeded=" + N((long long)g_rec.seeded) + " rows=" + N((long long)g_rec.rows.size())
                 + " stances=" + N((long long)g_rec.stances.size()) + " owed=" + N(g_recOwed ? 1 : 0) + " records=" + N(g_records) + " writes=" + N(g_recordWrites) + " writeFails=" + N(g_recordWriteFails)
                 + " seeds=" + N(g_seeds) + " deltaSends=" + N(g_deltaSends) + " deltaSendFails=" + N(g_deltaSendFails) + " stances=" + N(g_stances)
                 + " sides=" + N(g_sides) + " sidesOwed=" + N((long long)g_sideOwed.size()) + " sidesNothingToWrite=" + N(g_sidesNothing) + " sidesKeptOwn=" + N(g_sidesKeptOwn) + " restoreFails=" + N(g_restoreFails)
                 + " " + RelationsTeamText() + " | " + ResearchText());
        return "ok team show";
    }
    if (sub == "access")
    {
        long long slot = -1; is >> slot;
        std::string cls; is >> cls;
        if (slot < 0 || slot > 4095) return "error team access usage: team access <slot> [class] (the nearest loaded building of that player's faction, its class name containing [class])";
        return PolicyTeamAccessProbe((int)slot, cls);
    }
    if (sub == "click")
    {
        long long slot = -1; is >> slot;
        std::string cls; is >> cls;
        if (slot < 0 || slot > 4095) return "error team click usage: team click <slot> [class] (a real click on the nearest loaded building of that player's faction)";
        return PolicyTeamClick((int)slot, cls);
    }
    if (sub == "orders")
    {
        long long slot = -1; is >> slot;
        if (slot < 0 || slot > 4095) return "error team orders usage: team orders <slot> (the engine's order decisions on this game's own character and on that player's)";
        return PolicyTeamOrders((int)slot);
    }
    if (sub == "research")
    {
        std::string what, sid; is >> what >> sid;
        if (what == "finish")
        {
            if (sid.empty()) return "error team research usage: team research finish <techStringID>";
            if (!GameplayRunning() || EngineWritesBlocked()) return "error team research finish: no world running";
            std::string name, why; int route = swteam::kResViaSetResearched;
            const int k = StoreResearchApply(sid, true, &name, &why, &route);
            DebugLog("[TEAM] research finish (TEST): " + sid + " (" + name + ") through the hooked setResearched - " + (k == 1 ? std::string("applied") : "NOT applied (" + why + ")"));
            return k == 1 ? "ok team research finish " + sid : "error team research finish: " + why;
        }
        if (what == "queue")
        {
            if (sid.empty()) return "error team research usage: team research queue <techStringID>";
            if (!GameplayRunning() || EngineWritesBlocked()) return "error team research queue: no world running";
            std::string name, detail;
            const std::string w = StoreResearchQueueTech(sid, &name, &detail);
            std::vector<std::pair<std::string, float> > q; std::string qt, why;
            if (!StoreResearchQueue(&q, &qt, &why)) qt = "unreadable (" + why + ")";
            DebugLog("[TEAM] research queue (TEST): " + sid + " (" + name + ") through the engine's Research::addToQueue - " + (w.empty() ? std::string("queued") : "NOT queued (" + w + ")")
                     + (detail.empty() ? std::string() : " " + detail) + " | queue now: " + qt);
            return w.empty() ? "ok team research queue " + sid : "error team research queue: " + w;
        }
        if (what == "show")
        {
            std::vector<std::string> mine; long long q = 0; std::string why;
            if (!StoreResearchFinished(&mine, &q, &why)) { DebugLog("[TEAM] research show: unreadable (" + why + ")"); return "error team research show: " + why; }
            std::vector<std::pair<std::string, float> > queue; std::string qt, qwhy;
            if (!StoreResearchQueue(&queue, &qt, &qwhy)) qt = "unreadable (" + qwhy + ")";
            std::string one;
            if (!sid.empty())
            {
                int at = -1;
                for (size_t i = 0; i < queue.size(); ++i) if (queue[i].first == sid) { at = (int)i; break; }
                one = " | " + sid + " (" + StoreResearchNameOf(sid) + ") finished=" + (std::find(mine.begin(), mine.end(), sid) != mine.end() ? "yes" : "no")
                      + " queued=" + (at < 0 ? std::string("no") : "at " + N((long long)at));
            }
            DebugLog("[TEAM] research show: finished=" + N((long long)mine.size()) + " queue=" + N(q) + " [" + qt + "]" + one + " researchLevel=" + N((long long)StoreResearchLevel())
                     + " | " + ResearchText());
            return "ok team research show finished=" + N((long long)mine.size());
        }
        return "error team research usage: team research finish <techStringID> | queue <techStringID> | show [<techStringID>]";
    }
    if (sub == "invite" || sub == "remove")
    {
        long long slot = -1; is >> slot;
        if (slot < 0 || slot > 4095) return "error team " + sub + " usage: team " + sub + " <slot>";
        return TeamRequestSend(sub, (int)slot);
    }
    if (sub == "accept" || sub == "decline" || sub == "leave" || sub == "disband") return TeamRequestSend(sub, -1);
    return "error team usage: team invite <slot> | accept | decline | leave | remove <slot> | disband | show | access <slot> [class] | click <slot> [class] | research finish <techStringID> | research queue <techStringID> | research show [<techStringID>]";
}

std::string TeamRequestSend(const std::string& sub, int slot)
{
    std::vector<char> b;
    if (StoreWelcomedThisLink() == 0) return "error team " + sub + ": no world-server link";
    if (sub == "invite" || sub == "remove")
    {
        if (slot < 0 || slot > 4095) return "error team " + sub + " usage: team " + sub + " <slot>";
        if (sub == "remove") swteam::EncodeRemove(&b, (unsigned)slot);
        else
        {
            ::Faction* mine = LocalPlayerFaction();
            if (!GameplayRunning() || mine == 0) return "error team invite: no world running (the faction name is read from this game's own faction)";
            swteam::EncodeInvite(&b, (unsigned)slot, mine->getName());
        }
    }
    else if (sub == "accept")
    {
        if (!GameplayRunning()) return "error team accept: no world running (the standing at the moment of joining is read from this game's world)";
        swteam::PreJoin pj; std::string why;
        if (!RelationsNpcRecordOf(-1, &pj.npc, &why)) return "error team accept: this game's standing could not be read (" + why + ")";
        RelationsPlayerSides(&pj.players);
        std::vector<char> snap; swteam::EncodePreJoin(pj, &snap);
        if (!swteam::EncodeAnswer(&b, true, (unsigned)pj.npc.rows.size(), snap)) return "error team accept: the standing is too large to send";
        std::string ps;
        for (size_t i = 0; i < pj.players.size(); ++i) ps += (i ? ", s" : "s") + N((long long)pj.players[i].slot) + "=" + Value(1, pj.players[i].rel);
        DebugLog("[TEAM] accept: pre-join standing read - " + N((long long)pj.npc.rows.size()) + " NPC factions and this game's side towards " + N((long long)pj.players.size())
                 + " other players' factions (" + (ps.empty() ? std::string("none") : ps) + "), " + N((long long)snap.size()) + " bytes, sent with the ACCEPT");
    }
    else if (sub == "decline") swteam::EncodeAnswer(&b, false, 0, std::vector<char>());
    else if (sub == "leave") swteam::EncodeLeave(&b);
    else if (sub == "disband") swteam::EncodeDisband(&b);
    else return "error team request: " + sub + " is not a request";
    if (!Send(b)) return "error team " + sub + ": not sent (link down)";
    if ((sub == "accept" || sub == "decline") && g_invite.on) g_invite.answered = true;   /* its box is not shown again; the notice ends it */
    DebugLog("[TEAM] " + sub + " sent to the world server" + (sub == "invite" || sub == "remove" ? " (slot " + N((long long)slot) + ")" : std::string()));
    return "ok team " + sub + " sent";
}

int TeamMyRole(std::string* teamName, int* founderSlot)
{
    if (teamName != 0) teamName->clear();
    if (founderSlot != 0) *founderSlot = -1;
    const int me = StoreMySlot();
    if (me < 0 || g_tableGen < 0) return 0;
    int founder = 0;
    const int t = swteam::WireTeamOf(g_table, (unsigned)me, &founder);
    if (t < 0) return 0;
    if (teamName != 0) *teamName = g_table[t].name;
    if (founderSlot != 0) *founderSlot = (int)g_table[t].founderSlot;
    return founder ? 1 : 2;
}
bool TeamIsFounder(int slot)
{
    if (slot < 0 || g_tableGen < 0) return false;
    int founder = 0;
    return swteam::WireTeamOf(g_table, (unsigned)slot, &founder) >= 0 && founder != 0;
}
std::string TeamPlayerName(int slot) { return slot < 0 ? std::string() : ScreenNameOf((unsigned)slot); }
bool TeamTableKnown() { return g_tableGen >= 0 && g_tableGen == StoreLinkGen(); }
bool TeamMatesSettling() { return teamscreen::MatesSettling(g_matesChangedMs, ::GetTickCount()); }
bool TeamMateQuietAnyThread(int me, int slot)
{
    if (slot < 0 || (unsigned)slot >= swteam::kSlotIndexCap) return false;
    return teamscreen::MateQuiet(TeamSameAnyThread(me, slot), (unsigned long)g_mateChangedMs[slot], ::GetTickCount());
}
bool TeamInvitationWaiting(int* fromSlot, std::string* teamName, unsigned* serial)
{
    if (!g_invite.on || g_invite.answered) return false;
    if (fromSlot != 0) *fromSlot = (int)g_invite.fromSlot;
    if (teamName != 0) *teamName = g_invite.teamName;
    if (serial != 0) *serial = g_invite.serial;
    return true;
}
}   // namespace coop
