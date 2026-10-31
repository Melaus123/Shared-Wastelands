// admin.cpp - THE ADMIN'S TOYCHEST v2 (admin.h; the rules and the wire: src/common/admincmd.h).
//
// THE ADMIN'S END. A chat line starting with '/' never goes to the chat: admincmd::Parse reads it, and ADMIN_REQ goes to the world
// server, which alone decides whether this player is an admin. /bring and /sethome carry this game's lead character's position (for
// /bring 5 m beside it), so the target's game never has to find this player's characters. The answer (ADMIN_REPLY) is shown as grey
// chat lines; a NOTICE (ADMIN_REPLY req 0, a /jail) is shown to every player. /help, /jail list and /here are answered here: /here
// (any player) is this game's own lead character's position, in /where's words plus the height, the nearest town and the line to
// paste into `playerteleport abs`.
//
// THE OWNER'S END. ADMIN_DO names one of this game's OWN characters (this player's faction, driven here): by name, or "" for this
// player's lead character (the watched one, else a squad leader, else the first alive). The work runs in AdminTick, on the main
// thread, only while EngineWritesBlocked() is false and a world runs:
//   FIND    - how many own characters (or, for /revive, fallen rows) carry the name; x = 1 when the lead character is one of them.
//   WHERE   - the character's position and sector.
//   HEAL    - the three tiers, medical.cpp HealOwnedTier: MEDIC (the v1 heal), HEAL (+ blood to the engine's maximum, limb wear 0,
//             hunger full), RESTORE (the limbs first: clothing.cpp LimbRegrowRequest at the K2 safe point, then HEAL). The engine's
//             own medical tick stands a knocked-out character up, and the answer waits for that (up to admincmd::kStandWaitMs).
//   REVIVE  - the FALLEN tab's own road (resurrect.cpp ResurrectBringBackFor / ResurrectBringBackHere), as if this player pressed
//             BRING BACK: beside the selected free squadmate (else the first), or where the camera looks when none is alive; the
//             host's resurrection options, fee and character limit apply.
//   MOVE    - handoff.cpp AdminMoveCharacter to the ABSOLUTE position carried, on the ground. A downed character's ragdoll would
//             snap it back, so a KO'd one is healed first and moved once it stands (admincmd::MovePlan); a dead one is refused.
//             With kFlagSquad (/home, /jail) the character's whole squad goes, laid out in rows (admincmd::SquadSpot); a knocked-out or
//             downed member stays where it is and the answer says so, and nobody is healed first.
//   GOD     - the toggle of this character's god mode (admincmd::GodToggle over the table the world server last sent): ADMIN_GOD to
//             the server, which keeps the table and sends it to every game.
//   SAVE    - this profile's own slot saved now (store.cpp StoreAdminSaveBegin: the leave save's road, nothing continues after).
// GOD MODE ON EVERY GAME: the world server's table (GOD_TABLE) is kept here and, every frame, each row's character on this game (its own
// or a copy) is written into a fixed pointer table the AI-thread detours read with plain compares (AdminGodAnyThread): peace.cpp's
// isEnemyOf / isAllyOf (untargetable), medical.cpp's hit and knockout gates, clothing.cpp's amputation gate. This game's OWN god
// characters are also kept at the HEAL tier four times a second, their STATE pushed when a value moved (at most twice a second).
#include "admin.h"
#include "store.h"           // StoreSendAdmin, StoreMySlot, EngineWritesBlocked, StoreAdminSaveBegin / Result
#include "chat.h"            // ChatNoticeLine
#include "spawn.h"           // FindSpawned, FindSpawnedUid, MirrorSlot, MirrorCapacity, NameOf
#include "handoff.h"         // AdminMoveCharacter, ReadSquadOf, SquadView
#include "medical.h"         // HealOwnedTier, OwnedUnconscious, StatePush
#include "clothing.h"        // LimbRegrowRequest / Result / Forget
#include "items.h"           // ItemsGiveMaterial - /additem through the engine's own item factory and inventory add
#include "resurrect.h"       // ResurrectTabRead, ResurrectFreeMates, ResurrectBringBackFor / Here, ResurrectCameraRoadOffered
#include "replicate.h"       // IsDownedCharacter
#include "playerfaction.h"   // IsPlayerFaction
#include "ai_spike.h"        // GetTarget
#include "zones.h"           // SectorOf, SectorString, TownDistances
#include "soak.h"            // GameplayRunning
#include "net/session.h"     // IsUidMine
#include "coop_log.h"
#include "game/Character.h"
#include "../common/admincmd.h"
#include "../common/adminauth.h"   // admin two-factor: /login's challenge and proof (the password never leaves this game)
#include "../common/fallentab.h"   // swtab::RefusalWords
#include <ogre/OgreVector3.h>
#include <Windows.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coop {
namespace {

/* ---- god mode: the AI-thread table (pointer compares only, never followed) ---- */
void* volatile g_godPtrs[admincmd::kGodMax];
volatile LONG g_godN = 0;

std::string N(long long v) { std::ostringstream o; o << v; return o.str(); }

struct Own { unsigned int uid; ::Character* c; std::string name; int alive; };

/* this player's own characters: driven here, of this game's player faction */
void OwnChars(std::vector<Own>* out)
{
    out->clear();
    for (int i = 0; i < MirrorCapacity(); ++i)
    {
        unsigned int uid = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &uid, &c) || c == 0 || uid == 0 || !net::IsUidMine(uid)) continue;
        if (!IsPlayerFaction(c->getOwnerFactionDirect())) continue;
        Own o; o.uid = uid; o.c = c; o.alive = c->hasDied() ? 0 : 1;
        NameOf(c, &o.name);
        out->push_back(o);
    }
}
/* this player's lead character: the watched one (the character the zones and the camera follow), else the first living one that
   leads its squad, else the first living one; -1 none */
int LeadIndex(const std::vector<Own>& own)
{
    ::Character* t = GetTarget();
    for (size_t i = 0; i < own.size(); ++i) if (own[i].c == t && own[i].alive) return (int)i;
    for (size_t i = 0; i < own.size(); ++i)
    {
        if (!own[i].alive) continue;
        SquadView sv;
        if (ReadSquadOf(own[i].c, &sv) == 1 && sv.leader == own[i].c) return (int)i;
    }
    for (size_t i = 0; i < own.size(); ++i) if (own[i].alive) return (int)i;
    return -1;
}
/* the characters a job names: "" = the lead; else every own character of that name (living first) */
std::vector<int> Named(const std::vector<Own>& own, const std::string& target)
{
    std::vector<int> out;
    if (target.empty()) { const int k = LeadIndex(own); if (k >= 0) out.push_back(k); return out; }
    for (int pass = 1; pass >= 0; --pass)
        for (size_t i = 0; i < own.size(); ++i) if (own[i].alive == pass && admincmd::NamesMatch(own[i].name, target)) out.push_back((int)i);
    return out;
}
bool ReadPosOf(::Character* c, float* x, float* y, float* z)
{
    __try { Ogre::Vector3 p = c->worldPosition(); *x = p.x; *y = p.y; *z = p.z; return p.x == p.x && p.z == p.z; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string Who(const Own& o) { return o.name.empty() ? "uid " + N(o.uid) : o.name; }
/* the nearest town within admincmd::kHereTownNear of x,z: its name (empty = none near) */
std::string NearTown(float x, float z, float* dist)
{
    std::vector<std::pair<std::string, float> > towns; unsigned faults = 0;
    *dist = 0.0f;
    if (!TownDistances(x, z, &towns, &faults)) return std::string();
    int best = -1;
    for (size_t i = 0; i < towns.size(); ++i) if (!towns[i].first.empty() && towns[i].second == towns[i].second && (best < 0 || towns[i].second < towns[(size_t)best].second)) best = (int)i;
    if (best < 0 || towns[(size_t)best].second > admincmd::kHereTownNear) return std::string();
    *dist = towns[(size_t)best].second;
    return towns[(size_t)best].first;
}

/* ---- the admin's end ---- */
unsigned g_nextReq = 1;
/* admin two-factor: the typed /login password, held only until the challenge comes (adminauth::kChallengeSec), then wiped */
std::string g_loginPw;
DWORD g_loginAt = 0;
long long g_loginsAsked = 0, g_loginProofs = 0, g_loginStale = 0;
void Wipe(std::string* s) { if (!s->empty()) SecureZeroMemory(&(*s)[0], s->size()); s->clear(); }
std::set<unsigned> g_myReqs;                 /* requests sent and not answered */
std::vector<admincmd::Reply> g_replies;      /* arrived, shown at the next tick */
long long g_typed = 0, g_sent = 0, g_shown = 0, g_strayReplies = 0, g_notices = 0;

void Say(const std::string& line) { ChatNoticeLine(-1, std::string(), line); }

/* ---- god mode: the world server's table, and this game's own god characters kept whole ---- */
std::vector<admincmd::GodRow> g_godRows;     /* MAIN THREAD: the last GOD_TABLE */
DWORD g_godProtectAt = 0;
std::map<unsigned int, DWORD> g_godPushedAt;
long long g_godTables = 0, g_godProtects = 0, g_godPushes = 0;

void GodPointersRefresh()
{
    LONG n = 0;
    for (size_t i = 0; i < g_godRows.size() && n < (LONG)admincmd::kGodMax; ++i)
    {
        ::Character* c = FindSpawned(g_godRows[i].uid);
        if (c != 0) g_godPtrs[n++] = (void*)c;
    }
    for (LONG k = n; k < g_godN && k < (LONG)admincmd::kGodMax; ++k) g_godPtrs[k] = 0;
    ::InterlockedExchange(&g_godN, n);
}
void GodProtectTick()
{
    int dedHere = 0, haveClock = 0, clockMode = 0; unsigned dedMode = 0; float speed = 1.0f;
    StoreFooterTimeFacts(&dedHere, &dedMode, &haveClock, &speed, &clockMode);   /* dedHere: DEDINFO arrived on this link (a dedicated world) */
    if (!admincmd::GodProtectRuns(StoreLinkIsUp(), dedHere != 0, g_godRows.size())) return;   /* a no-op outside a linked dedicated world */
    const DWORD now = ::GetTickCount();
    if (now - g_godProtectAt < 250) return;
    g_godProtectAt = now;
    for (size_t i = 0; i < g_godRows.size(); ++i)
    {
        const unsigned int uid = g_godRows[i].uid;
        if (!net::IsUidMine(uid)) continue;
        int parts = 0, wasDown = 0, changed = 0;
        if (HealOwnedTier(uid, admincmd::kTierHeal, false, &parts, &wasDown, &changed) != 1) continue;
        ++g_godProtects;
        if (!changed) continue;
        std::map<unsigned int, DWORD>::iterator p = g_godPushedAt.find(uid);
        if (p != g_godPushedAt.end() && now - p->second < 500) continue;
        g_godPushedAt[uid] = now;
        if (StatePush(uid)) ++g_godPushes;
    }
}

/* ---- the owner's end ---- */
enum { kStNew = 0, kStHealWait = 1, kStMoveWait = 2, kStRegrowWait = 3, kStSaveWait = 4, kStAddItemWait = 5 };
struct Job
{
    admincmd::Do d; int stage; DWORD since; std::vector<unsigned int> uids; std::vector<std::string> names; int dead; std::string words;
    int regrown, robotKept; long long saveSeen;
    Job() : stage(kStNew), since(0), dead(0), regrown(0), robotKept(0), saveSeen(0) {}
};
std::vector<Job> g_jobs;
long long g_jobsIn = 0, g_jobsDone = 0, g_jobsBad = 0;
/* /additem: one give at a time, made at the K2 safe point (AdminSafePointDrain) - the job waits for it */
struct AddItemWant { unsigned job; unsigned int uid; int alias, count, state, added; AddItemWant() : job(0), uid(0), alias(-1), count(0), state(0), added(0) {} };   /* state 0 none, 1 asked, 2 done, -1 refused */
AddItemWant g_addItem;

void Answer(const Job& j, unsigned code, unsigned count, float x, float y, float z, const std::string& text)
{
    admincmd::Done d; d.job = j.d.job; d.code = code; d.count = count; d.x = x; d.y = y; d.z = z; d.text = text;
    std::vector<char> b; admincmd::EncodeDone(d, &b);
    const bool sent = StoreSendAdmin(admincmd::kMsgAdminDone, b);
    ++g_jobsDone;
    DebugLog("[ADMIN] job " + N(j.d.job) + " " + admincmd::OpName((int)j.d.op) + " answered " + admincmd::CodeName(code) + " count=" + N(count)
             + (text.empty() ? std::string() : " '" + text + "'") + (sent ? "" : " - NOT sent (no link)"));
}
/* every uid of the job standing (alive, not down) */
bool AllStanding(const std::vector<unsigned int>& uids)
{
    for (size_t i = 0; i < uids.size(); ++i)
    {
        ::Character* c = FindSpawned(uids[i]);
        if (c == 0 || c->hasDied()) continue;
        if (OwnedUnconscious(uids[i]) == 1 || IsDownedCharacter(c)) return false;
    }
    return true;
}
std::string HealWords(const Job& j, bool standing)
{
    const int tier = admincmd::TierOfOp(j.d.op);
    const std::string who = j.names.size() == 1 ? j.names[0] : N((long long)j.names.size()) + " characters";
    std::string t = admincmd::TierWords(tier, who, j.regrown, j.robotKept, standing);
    if (tier == admincmd::kTierRestore && !j.words.empty()) t += " (" + j.words + ")";
    if (j.dead > 0) t += " " + N(j.dead) + " dead one(s) left: use /revive.";
    return t;
}
/* the heal itself, every uid of the job, at its tier; then finished (answered) or a stand-up wait */
bool HealNow(Job* j)
{
    const int tier = admincmd::TierOfOp(j->d.op);
    std::vector<unsigned int> healed; std::vector<std::string> names; int bad = 0;
    for (size_t k = 0; k < j->uids.size(); ++k)
    {
        int parts = 0, wasDown = 0, changed = 0;
        const int h = HealOwnedTier(j->uids[k], tier, true, &parts, &wasDown, &changed);
        if (h == 1) { healed.push_back(j->uids[k]); names.push_back(j->names[k]); }
        else if (h == -2) ++j->dead; else ++bad;
    }
    j->uids = healed; j->names = names;
    if (j->uids.empty())
    {
        const std::string who = j->d.target.empty() ? "This player's lead character" : j->d.target;
        if (j->dead > 0) Answer(*j, admincmd::kDead, 0, 0, 0, 0, who + " is dead. Use /revive " + (j->d.target.empty() ? std::string("<name>") : j->d.target) + " instead.");
        else Answer(*j, admincmd::kFailed, 0, 0, 0, 0, who + " could not be healed (its health could not be read).");
        return true;
    }
    if (AllStanding(j->uids)) { Answer(*j, admincmd::kOk, (unsigned)j->uids.size(), 0, 0, 0, HealWords(*j, true)); return true; }
    j->stage = kStHealWait; j->since = ::GetTickCount();
    DebugLog("[ADMIN] job " + N(j->d.job) + " healed " + N((long long)j->uids.size()) + " - waiting up to " + N((long long)admincmd::kStandWaitMs) + " ms for them to stand");
    return false;
}
/* the move itself (the character up): 1 done (finished), 0 not yet (it went down: wait), -1 finished with a refusal */
int MoveNow(Job* j, unsigned int uid)
{
    ::Character* c = FindSpawned(uid);
    std::vector<Own> own; OwnChars(&own);
    const int lead = LeadIndex(own);
    if (c == 0) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "The character is no longer on its owner's game."); return -1; }
    std::string why, name; NameOf(c, &name); if (name.empty()) name = "uid " + N(uid);
    const bool focus = lead >= 0 && own[(size_t)lead].c == c;
    const int r = AdminMoveCharacter(c, j->d.x, j->d.z, (j->d.flags & admincmd::kFlagHaveY) != 0, j->d.y, focus, &why);
    if (r == 2) return 0;
    if (r == 0) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, name + " was not moved: " + why + "."); return -1; }
    Answer(*j, admincmd::kOk, 1, j->d.x, j->d.y, j->d.z, (j->words.empty() ? std::string() : j->words + " ") + "Moved " + name + " to "
           + admincmd::Num(j->d.x) + ", " + admincmd::Num(j->d.z) + " (sector " + SectorString(SectorOf(j->d.x, j->d.z)) + ").");
    return 1;
}
/* a squad move (/home, /jail): the named character's squad, the members this game drives and alive; a downed one stays */
void SquadMove(Job* j, const Own& o)
{
    std::vector<Own> own; OwnChars(&own);
    const int lead = LeadIndex(own);
    std::vector< ::Character*> members;
    SquadView sv;
    if (ReadSquadOf(o.c, &sv) == 1)
        for (int i = 0; i < sv.stored && i < kSquadViewCap; ++i) if (sv.members[i] != 0) members.push_back(sv.members[i]);
    if (members.empty()) members.push_back(o.c);
    /* the named character first, so it lands on the spot itself */
    for (size_t i = 1; i < members.size(); ++i) if (members[i] == o.c) { members[i] = members[0]; members[0] = o.c; break; }
    std::vector<std::string> moved, down; int dead = 0, k = 0; std::string failed;
    for (size_t i = 0; i < members.size(); ++i)
    {
        ::Character* c = members[i];
        const unsigned int uid = FindSpawnedUid(c);
        if (uid == 0 || !net::IsUidMine(uid)) continue;
        std::string name; NameOf(c, &name); if (name.empty()) name = "uid " + N(uid);
        if (c->hasDied()) { ++dead; continue; }
        if (OwnedUnconscious(uid) == 1 || IsDownedCharacter(c)) { down.push_back(name); continue; }
        float x = 0, z = 0; admincmd::SquadSpot(j->d.x, j->d.z, k, &x, &z);
        std::string why;
        const bool focus = lead >= 0 && own[(size_t)lead].c == c;
        const int r = AdminMoveCharacter(c, x, z, (j->d.flags & admincmd::kFlagHaveY) != 0, j->d.y, focus, &why);
        if (r == 1) { moved.push_back(name); ++k; }
        else if (r == 2) down.push_back(name);
        else if (failed.empty()) failed = name + " was not moved: " + why + ".";
    }
    const std::string where = admincmd::Num(j->d.x) + ", " + admincmd::Num(j->d.z) + " (sector " + SectorString(SectorOf(j->d.x, j->d.z)) + ")";
    const std::string text = admincmd::SquadMoveWords(moved, down, dead, where) + (failed.empty() ? std::string() : " " + failed);
    Answer(*j, moved.empty() ? (down.empty() ? admincmd::kFailed : admincmd::kStillDown) : admincmd::kOk, (unsigned)moved.size(), j->d.x, j->d.y, j->d.z, text);
}
/* a new job: true = finished (answered), false = it waits (a stand-up, the limbs, the save) */
bool Start(Job* j)
{
    std::vector<Own> own; OwnChars(&own);
    const admincmd::Do& d = j->d;
    const std::string who = d.target.empty() ? "This player's lead character" : d.target;
    if (d.op == admincmd::kOpFind)
    {
        unsigned n = 0; float leadNamed = 0.0f;
        if (d.flags & admincmd::kFlagFallen)
        {
            FallenTabRead r; ResurrectTabRead(&r);
            for (size_t i = 0; i < r.rows.size(); ++i) if (admincmd::NamesMatch(r.rows[i].name, d.target)) ++n;
        }
        else
        {
            for (size_t i = 0; i < own.size(); ++i) if (admincmd::NamesMatch(own[i].name, d.target)) ++n;
            const int k = LeadIndex(own);
            if (k >= 0 && admincmd::NamesMatch(own[(size_t)k].name, d.target)) leadNamed = 1.0f;   /* the lead is one of them: a player of that name is the same target */
        }
        Answer(*j, admincmd::kOk, n, leadNamed, 0, 0, std::string());
        return true;
    }
    if (d.op == admincmd::kOpAddItem)
    {
        const int alias = admincmd::AddItemAliasOf(d.flags), count = admincmd::AddItemCountOf(d.flags);
        const int k = LeadIndex(own);
        if (alias < 0 || count == 0) { Answer(*j, admincmd::kRefused, 0, 0, 0, 0, admincmd::Usage(admincmd::kVerbAddItem)); return true; }
        if (k < 0) { Answer(*j, admincmd::kNoSuch, 0, 0, 0, 0, "You have no living character to give it to."); return true; }
        if (g_addItem.state == 1) { Answer(*j, admincmd::kNotReady, 0, 0, 0, 0, "Another /additem is still being made: try again in a moment."); return true; }
        g_addItem = AddItemWant(); g_addItem.job = d.job; g_addItem.uid = own[(size_t)k].uid; g_addItem.alias = alias; g_addItem.count = count; g_addItem.state = 1;
        j->names.push_back(Who(own[(size_t)k]));
        j->stage = kStAddItemWait; j->since = ::GetTickCount();
        return false;
    }
    if (d.op == admincmd::kOpSave)
    {
        std::string why;
        if (StoreAdminSaveBegin(&j->saveSeen, &why) != 1) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "not saved: " + why); return true; }
        j->stage = kStSaveWait; j->since = ::GetTickCount();
        DebugLog("[ADMIN] job " + N(d.job) + " /save: this profile's own slot is being saved");
        return false;
    }
    if (d.op == admincmd::kOpRevive)
    {
        FallenTabRead r; ResurrectTabRead(&r);
        int row = -1;
        for (size_t i = 0; i < r.rows.size() && row < 0; ++i) if (d.target.empty() || admincmd::NamesMatch(r.rows[i].name, d.target)) row = (int)i;   /* newest first */
        if (row < 0) { Answer(*j, admincmd::kNoSuch, 0, 0, 0, 0, d.target.empty() ? "This player has no fallen character to bring back." : "No fallen character called " + d.target + " on this player's game."); return true; }
        const swtab::Row& fr = r.rows[(size_t)row];
        BringBackOut out;
        std::string mate;
        if (ResurrectCameraRoadOffered() == 1) out = ResurrectBringBackHere(fr.uid, -1);
        else
        {
            std::vector<FallenMate> mates; ResurrectFreeMates(&mates);
            int pick = -1;
            for (size_t i = 0; i < mates.size() && pick < 0; ++i) if (mates[i].selected) pick = (int)i;
            if (pick < 0 && !mates.empty()) pick = 0;
            if (pick < 0) { out.refusal = swtab::kRefNoMate; out.name = fr.name; }
            else { mate = mates[(size_t)pick].name; out = ResurrectBringBackFor(fr.uid, mates[(size_t)pick].uid, -1); }
        }
        const std::string nm = out.name.empty() ? fr.name : out.name, mt = out.mate.empty() ? mate : out.mate;
        if (out.ok)
        {
            std::string t = "Brought " + nm + " back " + (mt.empty() ? std::string("where the camera is looking") : "beside " + mt);
            if (out.paid > 0) t += " (the owner paid c." + N(out.paid) + ")";
            Answer(*j, admincmd::kOk, 1, 0, 0, 0, t + ".");
        }
        else
        {
            const int code = (out.refusal > 0 && out.refusal < swtab::kRefCodes) ? out.refusal : swtab::kRefFailed;
            Answer(*j, admincmd::kRefused, 0, 0, 0, 0, nm + " was not brought back: " + swtab::RefusalWords(code, nm, mt, out.price, out.money));
        }
        return true;
    }
    const bool heal = admincmd::TierOfOp(d.op) != 0;
    std::vector<int> picked;
    if (heal && (d.flags & admincmd::kFlagAll)) { for (size_t i = 0; i < own.size(); ++i) if (own[i].alive) picked.push_back((int)i); }
    else picked = Named(own, d.target);
    if (picked.empty()) { Answer(*j, admincmd::kNoSuch, 0, 0, 0, 0, d.target.empty() ? std::string("This player has no living character.") : "No character called " + d.target + " on this player's game."); return true; }
    if (d.op == admincmd::kOpWhere)
    {
        const Own& o = own[(size_t)picked[0]];
        float x = 0, y = 0, z = 0;
        if (!ReadPosOf(o.c, &x, &y, &z)) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, Who(o) + "'s position could not be read."); return true; }
        Answer(*j, admincmd::kOk, 1, x, y, z, admincmd::WhereWords(Who(o), x, z, SectorString(SectorOf(x, z))));
        return true;
    }
    if (d.op == admincmd::kOpGod)
    {
        if (picked.size() > 1) { Answer(*j, admincmd::kRefused, 0, 0, 0, 0, N((long long)picked.size()) + " characters on this player's game are called " + d.target + ": god mode not changed."); return true; }
        const Own& o = own[(size_t)picked[0]];
        const bool on = admincmd::GodToggle(g_godRows, o.uid);
        if (admincmd::GodPlanOf(o.alive, !on) == admincmd::kGodRefuseDead) { Answer(*j, admincmd::kDead, 0, 0, 0, 0, Who(o) + " is dead. Use /revive " + Who(o) + " first."); return true; }   /* a dead god row can still be switched off */
        admincmd::GodSet g; g.job = d.job; g.uid = o.uid; g.on = on ? 1u : 0u; g.name = admincmd::CleanName(Who(o));
        std::vector<char> b;
        if (!admincmd::EncodeGodSet(g, &b) || !StoreSendAdmin(admincmd::kMsgAdminGod, b)) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "Not connected to the world server: god mode not changed."); return true; }
        if (on && o.alive) { int parts = 0, wasDown = 0, changed = 0; HealOwnedTier(o.uid, admincmd::kTierHeal, true, &parts, &wasDown, &changed); }
        DebugLog("[GOD] uid " + N(o.uid) + " '" + Who(o) + "' god mode " + (on ? "ON" : "OFF") + " asked of the world server (job " + N(d.job) + ")");
        Answer(*j, admincmd::kOk, on ? 1u : 0u, 0, 0, 0, admincmd::GodWords(Who(o), on));
        return true;
    }
    if (heal)
    {
        for (size_t k = 0; k < picked.size(); ++k) { const Own& o = own[(size_t)picked[k]]; if (o.alive) { j->uids.push_back(o.uid); j->names.push_back(Who(o)); } else ++j->dead; }
        if (j->uids.empty()) { Answer(*j, admincmd::kDead, 0, 0, 0, 0, who + " is dead. Use /revive " + (d.target.empty() ? std::string("<name>") : d.target) + " instead."); return true; }
        if (d.op == admincmd::kOpRestore)
        {
            for (size_t k = 0; k < j->uids.size(); ++k) LimbRegrowRequest(j->uids[k]);
            j->stage = kStRegrowWait; j->since = ::GetTickCount();
            return false;
        }
        return HealNow(j);
    }
    if (d.op == admincmd::kOpMove)
    {
        if (picked.size() > 1) { Answer(*j, admincmd::kRefused, 0, 0, 0, 0, N((long long)picked.size()) + " characters on this player's game are called " + d.target + ": not moved."); return true; }
        const Own& o = own[(size_t)picked[0]];
        if ((d.flags & admincmd::kFlagSquad) != 0)
        {
            if (!o.alive) { Answer(*j, admincmd::kDead, 0, 0, 0, 0, Who(o) + " is dead: nobody was moved."); return true; }
            SquadMove(j, o);
            return true;
        }
        const int plan = admincmd::MovePlan(o.alive ? 0 : 1, IsDownedCharacter(o.c) ? 1 : 0);
        if (plan == admincmd::kMoveRefuseDead) { Answer(*j, admincmd::kDead, 0, 0, 0, 0, Who(o) + " is dead. Use /revive " + Who(o) + " first."); return true; }
        if (plan == admincmd::kMoveHealFirst)
        {
            int parts = 0, wasDown = 0;
            if (HealOwnedFull(o.uid, &parts, &wasDown) != 1) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, Who(o) + " is down and could not be healed, so it was not moved."); return true; }
            j->uids.push_back(o.uid); j->stage = kStMoveWait; j->since = ::GetTickCount();
            j->words = Who(o) + " was down: healed first.";
            DebugLog("[ADMIN] job " + N(d.job) + " move: uid " + N(o.uid) + " is down - healed, the move waits for it to stand");
            return false;
        }
        const int r = MoveNow(j, o.uid);
        if (r == 0) { int p = 0, w = 0; HealOwnedFull(o.uid, &p, &w); j->uids.push_back(o.uid); j->stage = kStMoveWait; j->since = ::GetTickCount(); j->words = Who(o) + " was down: healed first."; return false; }
        return true;
    }
    ++g_jobsBad;
    Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "That job is not known on this game.");
    return true;
}
/* a waiting job, every frame: true = finished */
bool Step(Job* j)
{
    const DWORD age = ::GetTickCount() - j->since;
    if (j->stage == kStAddItemWait)
    {
        if (g_addItem.job != j->d.job) { Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "The item could not be made."); return true; }
        if (g_addItem.state == 1 && age < admincmd::kRegrowWaitMs) return false;
        const AddItemWant w = g_addItem;
        g_addItem = AddItemWant();
        const std::string who = j->names.empty() ? std::string("your lead character") : j->names[0];
        if (w.state == 1) Answer(*j, admincmd::kFailed, 0, 0, 0, 0, "The engine's safe point did not come in time: nothing was added.");
        else if (w.added <= 0) Answer(*j, admincmd::kFailed, 0, 0, 0, 0, std::string("Nothing was added: ") + (w.state == -1 ? "the engine's inventory add faulted." : "the item could not be made here, or the inventory is full."));
        else Answer(*j, admincmd::kOk, (unsigned)w.added, 0, 0, 0, admincmd::AddItemWords(w.added, w.count, w.alias, who));
        return true;
    }
    if (j->stage == kStSaveWait)
    {
        std::string why;
        const int r = StoreAdminSaveResult(j->saveSeen, &why);
        if (r < 0 && age < (DWORD)((admincmd::kSaveWaitSec - 3.0) * 1000.0)) return false;
        if (r == 1) Answer(*j, admincmd::kOk, 1, 0, 0, 0, "saved");
        else Answer(*j, admincmd::kFailed, 0, 0, 0, 0, r < 0 ? std::string("its save did not finish in time") : "its save ended " + why);
        return true;
    }
    if (j->stage == kStRegrowWait)
    {
        bool all = true; std::string notes;
        for (size_t k = 0; k < j->uids.size(); ++k)
        {
            int regrown = 0, robotKept = 0; std::string why;
            const int r = LimbRegrowResult(j->uids[k], &regrown, &robotKept, &why);
            if (r < 0) { all = false; continue; }
            j->regrown += regrown; j->robotKept += robotKept;
            if (r == 0 && notes.find(why) == std::string::npos) notes += (notes.empty() ? "" : "; ") + why;
        }
        if (!all && age < admincmd::kRegrowWaitMs) return false;
        if (!all) { for (size_t k = 0; k < j->uids.size(); ++k) LimbRegrowForget(j->uids[k]); notes += (notes.empty() ? "" : "; ") + std::string("the limbs' safe point did not come in time"); }
        j->words = notes;
        return HealNow(j);
    }
    const bool up = AllStanding(j->uids);
    const bool late = age >= admincmd::kStandWaitMs;
    if (j->stage == kStHealWait)
    {
        if (!up && !late) return false;
        Answer(*j, admincmd::kOk, (unsigned)j->uids.size(), 0, 0, 0, HealWords(*j, up));
        return true;
    }
    if (!up && !late) return false;
    if (!up)
    {
        Answer(*j, admincmd::kStillDown, 0, 0, 0, 0, j->words + " Still down after " + N((long long)(admincmd::kStandWaitMs / 1000)) + " s, so it was not moved: try again in a moment.");
        return true;
    }
    const int r = MoveNow(j, j->uids.empty() ? 0u : j->uids[0]);
    if (r != 0) return true;
    if (!late) return false;   /* it went down again between the check and the move */
    Answer(*j, admincmd::kStillDown, 0, 0, 0, 0, j->words + " Still down, so it was not moved: try again in a moment.");
    return true;
}

}   // namespace

bool AdminGodAnyThread(const void* character)
{
    if (character == 0) return false;
    const LONG n = g_godN;
    for (LONG i = 0; i < n && i < (LONG)admincmd::kGodMax; ++i) if (g_godPtrs[i] == character) return true;
    return false;
}
int AdminGodCountAnyThread() { return (int)g_godN; }

void AdminTyped(const std::string& line)
{
    ++g_typed;
    admincmd::Cmd cmd; std::string err;
    if (!admincmd::Parse(line, &cmd, &err)) { Say(err); DebugLog("[ADMIN] typed command refused here: " + err); return; }
    if (cmd.verb == admincmd::kVerbHelp) { Say(admincmd::HelpText()); return; }
    if (cmd.verb == admincmd::kVerbJail && (cmd.flags & admincmd::kFlagJailList)) { Say(admincmd::JailListWords()); return; }
    std::vector<Own> own;
    if (cmd.verb == admincmd::kVerbHere || cmd.verb == admincmd::kVerbBring || cmd.verb == admincmd::kVerbSetHome) OwnChars(&own);
    const int lead = (cmd.verb == admincmd::kVerbHere || cmd.verb == admincmd::kVerbBring || cmd.verb == admincmd::kVerbSetHome) ? LeadIndex(own) : -1;
    float lx = 0, ly = 0, lz = 0;
    const bool leadPos = lead >= 0 && ReadPosOf(own[(size_t)lead].c, &lx, &ly, &lz);
    if (cmd.verb == admincmd::kVerbHere)   /* any player: this game's own answer, never sent */
    {
        if (!leadPos) { Say("Your lead character's position could not be read."); return; }
        float dist = 0.0f;
        const std::string town = NearTown(lx, lz, &dist);
        Say(admincmd::HereWords(Who(own[(size_t)lead]), lx, ly, lz, SectorString(SectorOf(lx, lz)), town, dist));
        DebugLog("[ADMIN] /here: " + admincmd::Num(lx) + " " + admincmd::Num(lz) + " " + admincmd::Num(ly));
        return;
    }
    admincmd::Req r; r.req = g_nextReq++; if (g_nextReq == 0) g_nextReq = 1;
    r.verb = (unsigned)cmd.verb; r.flags = cmd.flags & (admincmd::kFlagAll | admincmd::kFlagPickPlayer | admincmd::kFlagPickChar); r.target = cmd.target; r.extra = cmd.extra;
    if (cmd.verb == admincmd::kVerbJail) r.flags |= admincmd::JailSpotFlags(cmd.spot);
    if (cmd.verb == admincmd::kVerbTp) { r.x = cmd.x; r.z = cmd.z; }
    if (cmd.verb == admincmd::kVerbBring || cmd.verb == admincmd::kVerbSetHome)
    {
        if (!leadPos) { Say(std::string("Your lead character's position could not be read, so /") + admincmd::VerbName(cmd.verb) + " has nothing to go by."); return; }
        if (cmd.verb == admincmd::kVerbBring) { admincmd::BringSpot(lx, lz, &r.x, &r.z); r.y = ly; }
        else
        {
            r.x = lx; r.y = ly; r.z = lz;
            float dist = 0.0f; const std::string town = NearTown(lx, lz, &dist);
            r.target = admincmd::CleanName(town.empty() ? "sector " + SectorString(SectorOf(lx, lz)) : "near " + town);   /* the home's area words */
        }
    }
    std::vector<char> b;
    if (!admincmd::EncodeReq(r, &b) || !StoreSendAdmin(admincmd::kMsgAdminReq, b)) { Say("Not connected to the world server: the command was not sent."); return; }
    ++g_sent;
    g_myReqs.insert(r.req);
    DebugLog("[ADMIN] sent /" + std::string(admincmd::VerbName(cmd.verb)) + ((cmd.flags & admincmd::kFlagAll) ? " all" : "") + " '" + admincmd::TargetText(cmd.pick, cmd.target, cmd.extra) + "' req " + N(r.req)
             + (cmd.verb == admincmd::kVerbBring || cmd.verb == admincmd::kVerbTp || cmd.verb == admincmd::kVerbSetHome ? " at " + admincmd::Num(r.x) + ", " + admincmd::Num(r.z) : std::string())
             + (cmd.verb == admincmd::kVerbJail && cmd.spot >= 0 ? std::string(" spot ") + admincmd::kJailSpots[cmd.spot].key : std::string()));
}

void AdminLoginTyped(std::string* line)
{
    if (adminauth::IsLogoutLine(*line))
    {
        Wipe(line);
        std::vector<char> b; adminauth::EncodeKind(adminauth::kKindLogout, &b);
        if (!StoreSendAdmin(adminauth::kMsgAdminLogin, b)) Say("Not connected to the world server.");
        Wipe(&g_loginPw);
        DebugLog("[ADMIN] /logout sent");
        return;
    }
    std::string pw = adminauth::LoginPassword(*line);
    Wipe(line);
    if (pw.empty()) { Say("Type /login followed by the server's admin password. The password is never sent: only a one-time proof made from it."); return; }
    Wipe(&g_loginPw);
    g_loginPw.swap(pw);
    Wipe(&pw);
    std::vector<char> b; adminauth::EncodeKind(adminauth::kKindAsk, &b);
    if (!StoreSendAdmin(adminauth::kMsgAdminLogin, b)) { Wipe(&g_loginPw); Say("Not connected to the world server: not logged in."); return; }
    g_loginAt = ::GetTickCount() | 1u;
    ++g_loginsAsked;
    DebugLog("[ADMIN] /login: a challenge asked (the password stays in this game; asked " + N(g_loginsAsked) + ")");
}

void AdminArrive(unsigned type, const std::vector<char>& payload)
{
    const char* p = payload.empty() ? 0 : &payload[0];
    if (type == adminauth::kMsgAdminLogin)
    {
        const int kind = adminauth::KindOf(p, payload.size());
        if (kind == adminauth::kKindResult)
        {
            unsigned code = 0;
            if (!adminauth::DecodeResult(p, payload.size(), &code)) { DebugLog("[ADMIN] malformed ADMIN_LOGIN result - ignored"); return; }
            if (code != (unsigned)adminauth::kResLoggedOut) Wipe(&g_loginPw);
            admincmd::Reply r; r.req = 0; r.ok = code == (unsigned)adminauth::kResOk ? 1u : 0u; r.text = adminauth::ResultWords(code);
            g_replies.push_back(r);   /* shown by AdminTick, as any answer */
            DebugLog("[ADMIN] /login result " + N((long long)code));
            return;
        }
        if (kind != adminauth::kKindChallenge) { DebugLog("[ADMIN] ADMIN_LOGIN of kind " + N((long long)kind) + " - ignored"); return; }
        unsigned char nonce[adminauth::kNonceBytes], salt[adminauth::kSaltBytes], key[adminauth::kKeyBytes], proof[adminauth::kProofBytes];
        unsigned iters = 0, session = 0;
        if (g_loginPw.empty()) { DebugLog("[ADMIN] a /login challenge with no /login waiting - ignored"); return; }
        if (!adminauth::DecodeChallenge(p, payload.size(), nonce, salt, &iters, &session)) { Wipe(&g_loginPw); Say("The world server's /login challenge was not usable - not logged in."); return; }
        adminauth::DeriveKey(g_loginPw, salt, iters, key);   /* about a tenth of a second, once per /login */
        Wipe(&g_loginPw);
        adminauth::MakeProof(key, nonce, session, proof);
        SecureZeroMemory(key, sizeof(key));
        std::vector<char> b; adminauth::EncodeProof(proof, &b);
        SecureZeroMemory(proof, sizeof(proof));
        const bool sent = StoreSendAdmin(adminauth::kMsgAdminLogin, b);
        if (!b.empty()) SecureZeroMemory(&b[0], b.size());
        if (!sent) { Say("Not connected to the world server: not logged in."); return; }
        ++g_loginProofs;
        DebugLog("[ADMIN] /login: the proof sent (" + N((long long)iters) + " rounds; proofs " + N(g_loginProofs) + ")");
        return;
    }
    if (type == admincmd::kMsgAdminReply)
    {
        admincmd::Reply r;
        if (!admincmd::DecodeReply(p, payload.size(), &r)) { DebugLog("[ADMIN] malformed ADMIN_REPLY - ignored"); return; }
        if (r.req == 0) { ++g_notices; g_replies.push_back(r); return; }   /* a notice to every player (a /jail) */
        if (g_myReqs.erase(r.req) == 0) { ++g_strayReplies; DebugLog("[ADMIN] ADMIN_REPLY for req " + N(r.req) + ", which this game did not send - ignored"); return; }
        g_replies.push_back(r);
        return;
    }
    if (type == admincmd::kMsgGodTable)
    {
        std::vector<admincmd::GodRow> rows;
        if (!admincmd::DecodeGodTable(p, payload.size(), &rows)) { DebugLog("[GOD] malformed GOD_TABLE - ignored"); return; }
        ++g_godTables;
        std::string names;
        for (size_t i = 0; i < rows.size(); ++i) names += (i ? ", " : "") + admincmd::LogSafe(rows[i].name, 64) + " (uid " + N(rows[i].uid) + (net::IsUidMine(rows[i].uid) ? ", ours" : "") + ")";
        DebugLog("[GOD] the world server's god table: " + N((long long)rows.size()) + " character(s)" + (names.empty() ? std::string() : ": " + names));
        g_godRows.swap(rows);
        for (std::map<unsigned int, DWORD>::iterator it = g_godPushedAt.begin(); it != g_godPushedAt.end(); )
            if (admincmd::GodToggle(g_godRows, it->first)) g_godPushedAt.erase(it++); else ++it;
        GodPointersRefresh();
        return;
    }
    if (type == admincmd::kMsgAdminDo)
    {
        Job j;
        if (!admincmd::DecodeDo(p, payload.size(), &j.d)) { ++g_jobsBad; DebugLog("[ADMIN] malformed ADMIN_DO - ignored"); return; }
        ++g_jobsIn;
        DebugLog("[ADMIN] job " + N(j.d.job) + " " + admincmd::OpName((int)j.d.op) + " '" + admincmd::LogSafe(j.d.target, 160) + "' from the world server");
        g_jobs.push_back(j);
    }
}

void AdminTick()
{
    if (!g_loginPw.empty() && g_loginAt != 0 && ::GetTickCount() - g_loginAt > (DWORD)(adminauth::kChallengeSec * 1000.0))
    {   /* no challenge came: the typed password goes */
        Wipe(&g_loginPw); g_loginAt = 0; ++g_loginStale;
        Say("No answer from the world server to /login - not logged in. Try again.");
    }
    for (size_t i = 0; i < g_replies.size(); ++i)
    {
        const std::vector<std::string> lines = admincmd::Lines(g_replies[i].text);
        for (size_t k = 0; k < lines.size(); ++k) { Say(lines[k]); ++g_shown; }
        DebugLog("[ADMIN] " + (g_replies[i].req == 0 ? std::string("notice") : "reply to req " + N(g_replies[i].req) + " (" + (g_replies[i].ok ? "done" : "not done") + ")") + ": " + admincmd::LogSafe(g_replies[i].text));
    }
    g_replies.clear();
    if (!g_godRows.empty() || g_godN != 0) GodPointersRefresh();   /* a copy made or remade since: its new pointer */
    if (!GameplayRunning() || EngineWritesBlocked()) return;   /* the world server times a job out; nothing here goes stale */
    GodProtectTick();   /* a no-op outside a linked dedicated world (admincmd::GodProtectRuns) */
    if (g_jobs.empty()) return;
    std::vector<Job> keep;
    for (size_t i = 0; i < g_jobs.size(); ++i)
    {
        Job& j = g_jobs[i];
        const bool finished = j.stage == kStNew ? Start(&j) : Step(&j);
        if (!finished) keep.push_back(j);
    }
    g_jobs.swap(keep);
}

void AdminSafePointDrain()
{
    if (g_addItem.state != 1 || EngineWritesBlocked()) return;
    ::Character* c = FindSpawned(g_addItem.uid);
    if (c == 0 || !net::IsUidMine(g_addItem.uid) || g_addItem.alias < 0) { g_addItem.state = -1; return; }
    int units = 0;
    const int r = ItemsGiveMaterial((void*)c, admincmd::kBookAliases[g_addItem.alias].sid, g_addItem.count, &units);
    g_addItem.added = units;
    g_addItem.state = r < 0 ? -1 : 2;   /* -1: the engine's add faulted (items.cpp says what was left) */
    DebugLog("[ADMIN] /additem job " + N(g_addItem.job) + ": " + N(g_addItem.count) + " x " + admincmd::kBookAliases[g_addItem.alias].sid + " to uid " + N(g_addItem.uid)
             + " -> give=" + N(r) + " units placed=" + N(units));
}

void AdminForget(const char* why, bool linkLost)
{
    const size_t jobs = g_jobs.size(), rows = g_godRows.size();
    g_jobs.clear();
    if (linkLost) Wipe(&g_loginPw);   /* a /login waiting for its challenge goes with the link (the server ends the login with it too) */
    if (g_addItem.state == 1) g_addItem.state = -1;   /* an /additem waiting for the safe point is dropped (its job went with g_jobs) */
    g_addItem = AddItemWant();
    if (linkLost)
    {
        g_godRows.clear();          /* the god table is the world server's: a new link (another world) starts with none */
        g_godPushedAt.clear();
        g_replies.clear();
        g_myReqs.clear();
    }
    ::InterlockedExchange(&g_godN, 0);   /* the AI threads' table emptied at once - the count first, then the slots (no lookup while the */
    for (LONG k = 0; k < (LONG)admincmd::kGodMax; ++k) g_godPtrs[k] = 0;   /* world goes); AdminTick refills it from the rows kept */
    if (jobs != 0 || (linkLost && rows != 0))
        DebugLog("[ADMIN] forgotten - " + std::string(why) + ": " + N((long long)jobs) + " job(s)" + (linkLost ? ", " + N((long long)rows) + " god row(s)" : std::string()));
}

std::string AdminGodTokens()
{
    return " godTable[rows,here,tables,protects,pushes]=" + N((long long)g_godRows.size()) + "," + N((long long)g_godN) + "," + N(g_godTables) + "," + N(g_godProtects) + "," + N(g_godPushes);
}

}   // namespace coop
