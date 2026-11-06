// admin.cpp - THE ADMIN LOGIN AND THE ADMIN COMMANDS, THIS GAME'S END (see admin.h).
//
// THE LOGIN. "/login <password>" typed in the chat never leaves this game as chat or as text: the game asks the world server for a
// challenge (ASK, numbered); the CHALLENGE brings a fresh nonce, the admin password's salt and iteration count, this connection's
// number and the ask's number - only the challenge for this game's latest ask is answered, so a second /login replaces the first;
// the game sends back only the proof (adminauth::MakeProof, bound to the nonce, the connection number, the world id this link's
// WELCOME named and "SW-ADMIN-2"). The typed password is held in one string until the CHALLENGE comes, or for
// adminwords::kPendingMs at most, then wiped (SecureZeroMemory), as are the key and the proof. The RESULT is said in words.
//
// THE OWNER (owner 1369 b): the same /login road with the owner password. REMEMBERED LOGINS (owner 1380 / 1379): a typed /login the
// world server takes as the owner or an admin is remembered for that world at the place this game dialled (store.cpp StoreLoginPasswordRemember); the host's game
// knows the owner password HOST found or wrote in the world's rules.txt. Either is shown at the world's door (store.cpp
// StoreAuthAnswer), and the world server logs this game in at its admission with the same RESULT a /login gets. When the door
// carried no login proof - or kAutoRetryMs after one that was not taken (a world kept running reads a newly written password line
// within seconds) - this game logs in with it by itself once (AdminTick); nothing is typed and nothing is said but the RESULT's
// words; a try the world server did not take is logged only, and a remembered one it answers as a wrong password is forgotten.
//
// THIS GAME'S STANDING (adminwords::Standing): whether this game is an admin now is the world server's word alone - ADMIN_STANDING
// right behind the WELCOME (none) and at each change it makes (a login taken - admin, or owner - /logout, the idle end, a password
// changed, this player taken off the admin list). A new link is none until its first
// standing. A logged-in admin that is one no longer, for any reason but its own /logout, is told "You're no longer an admin." once
// (the /logout's RESULT says those words itself).
//
// THE TOOLBOX (admintool.h). /where goes up as an ADMIN_REQ like any admin command. The world server hands this game a JOB (ADMIN_DO)
// when this game owns its target: it is queued here and done in AdminTick on the main thread while gameplay runs and the engine
// takes writes; its answer goes up as ADMIN_DONE. A finished job's ADMIN_DONE is kept (admintool::Memo) and sent again for a repeat
// of that job without acting again; a job still queued is not queued twice; a new link forgets both. /here (every player) is this
// game's own answer and never reaches the world server.
// A job that changes a character (/medic, /heal, /additem) waits for the K2 safe point (AdminSafePointDrain, the AI worker paused);
// it changes only this game's own characters, and the other games get the change through the existing records: the character's
// STATE (MSG_STATE 14, StatePush) for health, the item's ITEM_MOVE (37, the InventorySection::_addItem hook) and the squad's record
// for an item. A character search (kOpFind) counts this game's own player characters of a name. /additem's item is looked up here
// by name before anything goes up (its record id travels); one /additem at a time (adminwords::AddItemWait).
// THE MOVES (admintool.h): /bring, /tp, /goto, /home and /jail move only this game's own characters (kOpMove, kOpSquadMove) at the K2
// safe point through the engine's own teleport (handoff.cpp AdminMoveCharacter); the other games' copies follow through the
// existing move records. A dead character is not moved; a knocked-out one is not moved while it is down. /bring's and /tp's move
// (kDoHealFirst) heals a knocked-out character first, as /heal does, and waits for the engine's own medical tick to stand it up
// (g_standWait: tried again at each safe point, at most admintool::kStandWaitMs, then answered still down); /goto, /home and /jail
// heal nobody first. kOpPos answers where one of this game's characters is (for
// /bring, /goto and /sethome). A moved character's player is told "An admin moved Ruk." (owner 1396) unless the admin plays on this
// same game; a /jail's squad is told by the jail notice every player sees instead. /jail list and a spot that is not one are
// answered here.
// /GOD (kOpGod, mockups-t1205.md section 12): one of this game's own characters gets god mode on or off in this game's memory
// (medical.cpp's god table: no damage, knockouts, hunger or lost limbs here, and enemies ignore it on every game through its STATE,
// pushed at once); a dead character is never made a god. The god mode ends when this game leaves the world. The player is told
// "An admin turned god mode on for Ruk." (owner 1396) unless the admin plays on this same game.
// /REVIVE (kOpRevive): this player's fallen character of that name comes back on the FALLEN tab's own road (resurrect.cpp
// ResurrectAdminRevive), on the main tick as the tab runs it, beside a free living character of this player - free and whatever the
// world's resurrection option, as only the world server sends this op and only for a logged-in admin's command (owner 1394, 1398).
// The player is told "An admin brought Ruk back, free." (owner 1396) unless the admin plays on this same game.
// LEAD CHARACTER - the one the player is controlling: the first character of the player's selection when it is this game's own
// player character, else the character this game watches as its player (GetTarget) when it is.
//
// The world server alone decides who may do what; nothing here gates a command on this game's standing. MAIN THREAD throughout
// (the chat, the store drain and the in-world / title pumps).

#include "admin.h"
#include "store.h"
#include "chat.h"
#include "kapi.h"
#include "coop_log.h"
#include "spawn.h"          // FirstSelectedCharacter, LiveCharacter, FindSpawnedUid, SafeReadPosition, NameOf
#include "ai_spike.h"       // GetTarget
#include "playerfaction.h"  // IsPlayerFaction
#include "net/session.h"    // IsUidMine
#include "zones.h"          // TownDistances
#include "soak.h"           // GameplayRunning
#include "medical.h"        // AdminHealOwned
#include "items.h"          // ItemsNamed, ItemsAdminMake
#include "handoff.h"        // AdminMoveCharacter, ReadSquadOf
#include "replicate.h"      // IsDownedCharacter: the knocked-out test the moves use (LeadCharacter puts a knocked-out one last)
#include "resurrect.h"      // ResurrectAdminRevive - /revive on the FALLEN tab's own road
#include "addresses.h"      // GameWorldPtr
#include "game/GameWorld.h"
#include "game/Character.h"
#include "../common/adminauth.h"
#include "../common/admincmd.h"
#include "../common/admintool.h"
#include "../common/adminwords.h"
#include "../common/fallenwire.h"   // NearestTown: the town a place is near (kPlaceMaxUnits)
#include "../common/townnear.h"     // Nearest: the nearest town, however far

#include <Windows.h>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string N(long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
void Wipe(std::string* s) { if (!s->empty()) ::SecureZeroMemory(&(*s)[0], s->size()); s->clear(); }
void WipeBytes(std::vector<char>* b) { if (!b->empty()) ::SecureZeroMemory(&(*b)[0], b->size()); b->clear(); }

adminwords::Standing g_stand;                  /* the world server's last word on this game's admin standing, and the link it came on */
std::string g_pendingPw;                       /* the typed admin password, until its CHALLENGE (never logged) */
int g_pendingGen = -1;                         /* the link it was asked on */
unsigned g_pendingAt = 0;                      /* GetTickCount at the ASK */
unsigned g_askNo = 0;                          /* the number of this game's latest ASK (0 = none yet): only its CHALLENGE is answered */
unsigned g_nextReq = 0;                        /* ADMIN_REQ numbers (0 is the server's "answering no request") */
int g_pendingAuto = 0;                         /* the login waiting is this game's own remembered login (nothing typed): only its success is said */
std::string g_typedLogin;                      /* a typed /login's password, kept until its RESULT so a taken one is remembered (owner 1380); never logged */
int g_typedLoginGen = -1;                      /* the link it was typed on */
unsigned g_typedFrom = 0;                      /* this game's standing when it was typed: a login that changes nothing is not remembered */
int g_autoGen = -1;                            /* the link the owner login was last sent on by itself */
int g_autoTries = 0;                           /* its tries on that link (at most 2) */
unsigned g_autoAt = 0;                         /* GetTickCount at its last try */
const unsigned kAutoRetryMs = adminwords::kAutoRetryMs;
adminwords::LogoutHold g_logoutHold;           /* owner 1412 b: this player's own /logout - no login by itself for the rest of this visit */
long g_holdLoggedGen = -1;                     /* the world-load generation the hold's "not logged in by itself" was last logged in */
long long g_malformed = 0, g_unasked = 0, g_olderChallenges = 0;
struct PendingDo { admintool::Do d; int gen; unsigned at; };   /* at: GetTickCount when it was queued (admintool::DoStale) */
std::vector<PendingDo> g_dos;                  /* the toolbox jobs this game was handed and has not done yet */
struct StandWait { PendingDo pd; unsigned since; };   /* a healed-first move (kDoHealFirst) and when its character was healed (GetTickCount) */
std::vector<StandWait> g_standWait;            /* the healed-first moves waiting for their character to stand (AdminSafePointDrain) */
admintool::Memo g_memo;                        /* the ADMIN_DONE of each job finished on this link */
const size_t kDosMax = 16;                     /* jobs queued at once; one past it is answered kDoneFailed at once */
long long g_dosDone = 0, g_dosRepeat = 0, g_dosFull = 0, g_hereNoLead = 0, g_dosStale = 0;
adminwords::AddItemWait g_addWait;             /* the /additem this game asked and has no answer to yet */

bool Linked() { return coop::StoreRelayLinked() && coop::StoreWelcomedThisLink() != 0; }
/* every admin line shown in this game's chat; the line is logged too (T-1205: a run quotes the approved words; no admin line holds a
   password) */
void Say(const std::string& line)
{
    if (line.empty()) return;
    DebugLog("[ADMIN] shown in the chat: " + line);
    coop::ChatNoticeLine(-1, std::string(), line);
}

std::string StandingName(unsigned s)
{
    if (s == (unsigned)adminauth::kStandOwner) return "the world's owner (logged in with the owner password)";
    if (s == (unsigned)adminauth::kStandAdmin) return "a logged-in admin";
    return "not an admin";
}
std::string WhyName(unsigned w)
{
    switch (w)
    {
    case adminauth::kWhyWelcome:         return "its WELCOME";
    case adminauth::kWhyLogin:           return "a login";
    case adminauth::kWhyLogout:          return "its /logout";
    case adminauth::kWhyIdle:            return "no admin act for " + N((long long)adminauth::kIdleSec) + " s";
    case adminauth::kWhyPasswordChanged: return "the admin password was set";
    case adminauth::kWhyRemoved:         return "its player is no longer on the admin list, or the link names another player";
    default:                             return "reason " + N((long long)w);
    }
}

void PendingWipe(const char* why)
{
    if (g_pendingPw.empty()) return;
    Wipe(&g_pendingPw);
    DebugLog(std::string("[ADMIN] a typed admin password was wiped unused (") + why + ")");
}
bool SendLogin(int kind)
{
    std::vector<char> b; adminauth::EncodeKind(kind, &b);
    return coop::StoreSendAdmin(adminauth::kMsgAdminLogin, b);
}

void LoginTyped(std::string* pw, bool autoOwner)
{
    if (pw->empty()) { Say(adminwords::LoginNoPasswordLine()); return; }
    if (!Linked()) { Say(adminwords::NotLinkedLine()); return; }
    PendingWipe("a new /login");
    g_pendingAuto = autoOwner ? 1 : 0;
    g_askNo = adminauth::NextAskNumber(g_askNo);   /* a CHALLENGE for any earlier ask is ignored from now on */
    std::vector<char> ab; adminauth::EncodeAsk(g_askNo, &ab);
    if (!coop::StoreSendAdmin(adminauth::kMsgAdminLogin, ab)) { Say(adminwords::NotLinkedLine()); return; }
    Wipe(&g_typedLogin);
    if (!autoOwner) { g_typedLogin = *pw; g_typedLoginGen = coop::StoreLinkGen(); g_typedFrom = adminwords::StandingNow(g_stand, g_typedLoginGen); }
    g_pendingPw.swap(*pw);   /* the caller's copy is now empty; this one is wiped at the CHALLENGE or in AdminTick */
    g_pendingGen = coop::StoreLinkGen(); g_pendingAt = ::GetTickCount();
    DebugLog(std::string("[ADMIN] ") + (autoOwner ? "this game's remembered login (by itself)" : "/login") + ": a challenge asked of the world server (the password stays on this game until it comes, then only the proof goes up)");
}
void LogoutTyped()
{
    /* owner 1412 b: the player's /logout holds for the rest of this visit - set as it is TYPED, before anything goes up, so no login
       by itself can slip in between (the standing none can come ahead of the RESULT), and also when it is not sent (a login by itself
       waiting, or the link down): the player asked to be logged out either way. Lifted when this game leaves the world. */
    adminwords::LogoutHoldSet(&g_logoutHold, coop::StoreWorldGenNow());
    PendingWipe("/logout");
    if (!Linked()) { Say(adminwords::NotLinkedLine()); return; }
    const unsigned standing = adminwords::StandingNow(g_stand, coop::StoreLinkGen());
    const std::string line = adminwords::LogoutLine(standing);
    if (!line.empty()) { DebugLog("[ADMIN] /logout not sent - this game is " + StandingName(standing)); Say(line); return; }
    if (!SendLogin(adminauth::kKindLogout)) { Say(adminwords::NotLinkedLine()); return; }
    DebugLog("[ADMIN] /logout sent - this game's standing changes when the world server says so");
}

void OnChallenge(const char* p, size_t n)
{
    const int gen = coop::StoreLinkGen();
    if (g_pendingPw.empty() || g_pendingGen != gen)
    {
        ++g_unasked;
        if (g_unasked <= 5) DebugLog("[ADMIN] a CHALLENGE came with no /login waiting on this link - ignored (" + N(g_unasked) + ")");
        return;
    }
    unsigned char nonce[adminauth::kNonceBytes], salt[adminauth::kSaltBytes];
    unsigned iters = 0, session = 0, askNo = 0;
    if (!adminauth::DecodeChallenge(p, n, nonce, salt, &iters, &session, &askNo))
    {
        PendingWipe("its CHALLENGE did not decode");
        ++g_malformed;
        ErrorLog("[ADMIN] the world server's CHALLENGE (" + N((long long)n) + " bytes, iterations " + N((long long)iters) + ") did not decode or names an iteration count outside "
                 + N((long long)adminauth::kIterMin) + "-" + N((long long)adminauth::kIterMax) + " - no proof sent");
        Say(adminwords::ResultLine(adminauth::kResNoChallenge, 0));
        return;
    }
    if (!adminauth::ChallengeIsLatest(askNo, g_askNo))
    {   /* the answer to an earlier /login, overtaken by a newer one: the newer one's CHALLENGE comes next */
        ++g_olderChallenges;
        if (g_olderChallenges <= 5) DebugLog("[ADMIN] a CHALLENGE for an earlier /login came after a newer one was asked - ignored, the newer one's is awaited (" + N(g_olderChallenges) + ")");
        return;
    }
    unsigned char key[adminauth::kKeyBytes], proof[adminauth::kProofBytes];
    adminauth::DeriveKey(g_pendingPw, salt, iters, key);   /* the key's iterations run here, on the main thread, once per login */
    Wipe(&g_pendingPw);
    adminauth::MakeProof(key, nonce, session, coop::StoreLinkWorldId(), proof);
    ::SecureZeroMemory(key, sizeof(key));
    std::vector<char> b; adminauth::EncodeProof(proof, &b);
    ::SecureZeroMemory(proof, sizeof(proof));
    const bool sent = coop::StoreSendAdmin(adminauth::kMsgAdminLogin, b);
    WipeBytes(&b);
    if (!sent) { Say(adminwords::NotLinkedLine()); return; }
    DebugLog("[ADMIN] /login: the proof sent (" + N((long long)iters) + " key iterations)");
}

void OnResult(const char* p, size_t n)
{
    unsigned code = 0, wait = 0;
    if (!adminauth::DecodeResult(p, n, &code, &wait)) { ++g_malformed; ErrorLog("[ADMIN] a RESULT of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    PendingWipe("a RESULT came");
    const bool autoOwner = g_pendingAuto != 0;
    g_pendingAuto = 0;
    {   /* REMEMBERED (owner 1380 / 1379): a typed login the world server took, that made this game the owner or an admin it was not */
        if (!autoOwner && adminwords::LoginRemembers(code, g_typedFrom) && !g_typedLogin.empty() && g_typedLoginGen == coop::StoreLinkGen()) coop::StoreLoginPasswordRemember(coop::StoreLinkWorldId(), g_typedLogin);
        Wipe(&g_typedLogin);
    }
    DebugLog("[ADMIN] " + std::string(autoOwner ? "this game's remembered login" : "login") + " RESULT code " + N((long long)code) + (code == adminauth::kResLocked ? " (wait " + N((long long)wait) + " s)" : std::string()));
    if (autoOwner && code != (unsigned)adminauth::kResOwner && code != (unsigned)adminauth::kResOk)
    {   /* a wrong password is forgotten, so it is not shown at every connect and counted wrong each time (the host's own is rules.txt's) */
        const bool forgot = adminwords::LoginForgets(code) && coop::StoreLoginPasswordForget(coop::StoreLinkWorldId());
        ErrorLog("[ADMIN] the world server did not take the login password this game knows for this world (RESULT code " + N((long long)code) + ")"
                 + (forgot ? std::string(" - forgotten; this game is not logged in by itself on this link")
                    : g_autoTries < 2 ? " - tried once more in " + N((long long)(kAutoRetryMs / 1000)) + " s" : std::string(" - this game is not logged in by itself on this link")));
        return;
    }
    Say(adminwords::ResultLine(code, wait));
}

void OnStanding(const char* p, size_t n)
{
    unsigned standing = 0, why = 0;
    if (!adminauth::DecodeStanding(p, n, &standing, &why)) { ++g_malformed; ErrorLog("[ADMIN] an ADMIN_STANDING of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    std::string line;
    const bool changed = adminwords::StandingTake(&g_stand, coop::StoreLinkGen(), standing, why, &line);
    DebugLog("[ADMIN] the world server says this game is " + StandingName(standing) + " (" + WhyName(why) + ")"
             + (changed ? (adminwords::AdminBy(standing) ? " - it sets the world's rules now" : " - it no longer sets the world's rules") : std::string()));
    Say(line);
}

/* ---- the toolbox ---- */
::Faction* FactionPod(::Character* c)
{
    __try { return c->getOwnerFactionDirect(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* this game's own player character: in this game's character index, owned here, of this game's player faction */
bool OwnPlayerCharacter(::Character* c)
{
    if (c == 0) return false;
    const unsigned int u = coop::FindSpawnedUid(c);
    return u != 0 && coop::net::IsUidMine(u) && coop::IsPlayerFaction(FactionPod(c));
}
/* 1 dead, 0 alive, -1 the read faulted */
int DeadPod(::Character* c)
{
    __try { return c->hasDied() ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* this game's own player characters (all of the player's squads) on the engine's character-update list, and their names. A dead
   character leaves that list (findings: "dead characters leave the engine's character-update list"), so the dead are not here:
   this game's fallen list holds them (FallenRows) */
void OwnPlayerCharacters(std::vector< ::Character*>* out, std::vector<std::string>* names)
{
    out->clear(); names->clear();
    if (coop::GameWorldPtr() == 0) return;
    const GameHashSet< ::Character*>::type& all = coop::GameWorldPtr()->activeCharacters();
    if (all.size() > 20000) return;
    for (GameHashSet< ::Character*>::type::const_iterator it = all.begin(); it != all.end(); ++it)
    {
        ::Character* c = coop::LiveCharacter(*it);
        std::string name;
        if (!OwnPlayerCharacter(c) || !coop::NameOf(c, &name)) continue;
        out->push_back(c); names->push_back(name);
    }
}
/* this game's fallen-list rows (the FALLEN tab's read): their names and uids, newest first */
void FallenRows(std::vector<std::string>* names, std::vector<unsigned>* uids)
{
    names->clear(); uids->clear();
    coop::FallenTabRead t;
    coop::ResurrectTabRead(&t);
    for (size_t i = 0; i < t.rows.size(); ++i) { names->push_back(t.rows[i].name); uids->push_back(t.rows[i].uid); }
}
/* this game's fallen-list rows called `target` ("" = every row) whose character is not one of cs (a dead one still on the update
   list is counted there); with a target, the newest such row's name and uid in *name, *uid (each may be 0) */
int FallenNotIn(const std::vector< ::Character*>& cs, const std::string& target, std::string* name, unsigned* uid)
{
    std::vector<std::string> fn; std::vector<unsigned> fu, live;
    FallenRows(&fn, &fu);
    for (size_t i = 0; i < cs.size(); ++i) live.push_back(coop::FindSpawnedUid(cs[i]));
    const int n = admintool::FallenNotLive(fn, fu, live, target);
    if (name != 0) name->clear();
    if (uid != 0) *uid = 0;
    if (n > 0 && !target.empty())
        for (size_t i = 0; i < fn.size(); ++i)
            if (admintool::FallenNotLive(std::vector<std::string>(1, fn[i]), std::vector<unsigned>(1, fu[i]), live, target) == 1)
            { if (name != 0) *name = fn[i]; if (uid != 0) *uid = fu[i]; break; }
    return n;
}
/* the player's lead character: the first living, NOT knocked-out one of the player's selection, else the game's target if not
   knocked out, else the first such own character; only when none is up, the first living (knocked-out) one, the same order
   (admintool::LeadPick). A dead one is skipped: after /revive the old body stayed selected (run T1899). A knocked-out one comes
   last: after /jail moved the squad, the knocked-out squadmate stayed and /where answered for it (run T1902). Knocked out =
   coop::IsDownedCharacter, the test AdminMoveCharacter refuses a move with (MoveSquad's "knocked out stayed") */
::Character* LeadCharacter()
{
    std::vector< ::Character*> cand;
    void* sel[64];
    const int ns = coop::SelectedCharacters(sel, 64);
    for (int i = 0; i < ns && i < 64; ++i) { ::Character* c = coop::LiveCharacter((::Character*)sel[i]); if (OwnPlayerCharacter(c)) cand.push_back(c); }
    ::Character* t = coop::LiveCharacter(coop::GetTarget());
    if (OwnPlayerCharacter(t)) cand.push_back(t);
    std::vector< ::Character*> own; std::vector<std::string> names;
    OwnPlayerCharacters(&own, &names);
    cand.insert(cand.end(), own.begin(), own.end());
    std::vector<int> dead, down;
    for (size_t i = 0; i < cand.size(); ++i)
    {
        dead.push_back(DeadPod(cand[i]));
        down.push_back(dead.back() == 0 && coop::IsDownedCharacter(cand[i]) ? 1 : 0);
    }
    const int k = admintool::LeadPick(dead, down);
    return k >= 0 ? cand[(size_t)k] : 0;
}
/* the lead character's name and position; false = no lead character, or its position or name did not read */
bool LeadAt(std::string* name, Ogre::Vector3* pos)
{
    ::Character* c = LeadCharacter();
    return c != 0 && coop::SafeReadPosition(c, pos) && coop::NameOf(c, name) && !name->empty();
}
void SendDone(const admintool::Done& d, int gen)
{
    std::vector<char> b;
    if (!admintool::EncodeDone(d, &b)) { ErrorLog("[ADMIN] job " + N((long long)d.job) + "'s ADMIN_DONE would not encode - not sent (the world server ends it as lapsed)"); return; }
    admintool::MemoKeep(&g_memo, gen, d.job, b);
    if (!coop::StoreSendAdmin(admintool::kMsgAdminDone, b)) DebugLog("[ADMIN] job " + N((long long)d.job) + "'s ADMIN_DONE not sent - the link is down");
}
/* a job that changes a character: done at the K2 safe point */
bool SafePointOp(unsigned op)
{
    return admintool::HealOp(op) || op == (unsigned)admintool::kOpAddItem || admintool::MoveOp(op);
}
/* the job's character: target "" = the lead character, else the one own player character of that name. 1 found (*c, *name), 0 none
   (no lead character), -1 not exactly one of that name, 2 dead: no living one of that name but a row of this game's fallen list
   (*name = its name, *fallenUid = its uid, *c = 0) - its job answers kDoneDead, never "no such name" (run T1899) */
int JobCharacter(const std::string& target, ::Character** c, std::string* name, unsigned* fallenUid = 0)
{
    *c = 0; name->clear();
    if (fallenUid != 0) *fallenUid = 0;
    if (target.empty())
    {
        *c = LeadCharacter();
        return (*c != 0 && coop::NameOf(*c, name) && !name->empty()) ? 1 : 0;
    }
    std::vector< ::Character*> cs; std::vector<std::string> names;
    OwnPlayerCharacters(&cs, &names);
    int hits = 0;
    for (size_t i = 0; i < cs.size(); ++i) if (admintool::SameName(names[i], target)) { ++hits; *c = cs[i]; *name = names[i]; }
    std::string fname; unsigned fuid = 0;
    const int fallen = hits == 0 ? FallenNotIn(cs, target, &fname, &fuid) : 0;
    switch (admintool::NamedPickOf(hits, fallen))
    {
    case admintool::kNamedOne: return 1;
    case admintool::kNamedDead: *c = 0; *name = fname; if (fallenUid != 0) *fallenUid = fuid; return 2;
    default: *c = 0; name->clear(); return -1;
    }
}
/* the town within kPlaceMaxUnits of (x, z), as /where says it ("" none) */
std::string TownNear(float x, float z)
{
    std::vector<std::pair<std::string, float> > raw; unsigned faults = 0;
    if (!coop::TownDistances(x, z, &raw, &faults)) return std::string();
    std::vector<swfallen::TownDist> rows;
    for (size_t i = 0; i < raw.size(); ++i) { swfallen::TownDist t; t.name = raw[i].first; t.dist = raw[i].second; rows.push_back(t); }
    const int k = swfallen::NearestTown(rows);
    return k >= 0 ? admincmd::CleanText(rows[(size_t)k].name, admintool::kNameBytes) : std::string();
}
/* one job, done now (on the main tick: /where, the character search): its ADMIN_DONE */
admintool::Done DoJob(const admintool::Do& d)
{
    admintool::Done r; r.job = d.job;
    if (d.op == (unsigned)admintool::kOpFind)
    {
        std::vector< ::Character*> cs; std::vector<std::string> names;
        OwnPlayerCharacters(&cs, &names);
        int live = 0;
        for (size_t i = 0; i < names.size(); ++i) if (admintool::SameName(names[i], d.target)) ++live;
        r.num = admintool::NamedCount(live, live == 0 ? FallenNotIn(cs, d.target, 0, 0) : 0);   /* a dead one counts: its job says dead */
        r.code = admintool::kDoneOk;
        return r;
    }
    if (d.op == (unsigned)admintool::kOpPos)
    {   /* where one of this game's characters is: /bring (the admin's lead), /goto (the target), /sethome (the admin's lead) */
        ::Character* c = 0; std::string name; Ogre::Vector3 pos;
        const int f = JobCharacter(d.target, &c, &name);
        if (f == 0) { r.code = admintool::kDoneNoLead; return r; }
        if (f < 0) { r.code = admintool::kDoneNotFound; return r; }
        if (f == 2) { r.code = admintool::kDoneDead; r.name = admincmd::CleanText(name, admintool::kNameBytes); return r; }   /* /goto a dead one */
        if (!coop::SafeReadPosition(c, &pos) || !admintool::CoordOk(pos.x) || !admintool::CoordOk(pos.y) || !admintool::CoordOk(pos.z)) { r.code = admintool::kDoneFailed; return r; }
        r.code = admintool::kDoneOk; r.x = pos.x; r.y = pos.y; r.z = pos.z;
        r.name = admincmd::CleanText(name, admintool::kNameBytes);
        r.place = TownNear(pos.x, pos.z);
        return r;
    }
    if (d.op == (unsigned)admintool::kOpGod)
    {   /* /god: this game's own character's god mode, in this game's memory (medical.cpp); its STATE tells the other games */
        ::Character* c = 0; std::string name;
        unsigned fallenUid = 0;
        const int f = JobCharacter(d.target, &c, &name, &fallenUid);
        if (f == 0) { r.code = admintool::kDoneNoLead; return r; }
        if (f < 0) { r.code = admintool::kDoneNotFound; return r; }
        r.name = admincmd::CleanText(name, admintool::kNameBytes);
        r.num = d.num != 0 ? 1u : 0u;
        if (f == 2)
        {   /* dead, on the fallen list only: never made a god; a god mode left on its uid may be switched off (this game's memory) */
            const int step = admintool::GodNext(d.num != 0, fallenUid != 0 && coop::GodOn(fallenUid), true);
            if (step == admintool::kGodAlready) { r.code = admintool::kDoneAlready; return r; }
            if (step == admintool::kGodDead) { r.code = admintool::kDoneDead; return r; }
            if (fallenUid == 0 || coop::GodSet(fallenUid, false) < 0) { r.code = admintool::kDoneFailed; return r; }
            DebugLog("[GOD] '" + name + "' (uid " + N((long long)fallenUid) + ", fallen) god mode OFF - this game's memory");
            r.code = admintool::kDoneOk;
            return r;
        }
        const unsigned int uid = coop::FindSpawnedUid(c);
        const int dead = DeadPod(c);
        if (uid == 0 || dead < 0) { r.code = admintool::kDoneFailed; return r; }
        const bool on = d.num != 0;
        const int step = admintool::GodNext(on, coop::GodOn(uid), dead > 0);
        if (step == admintool::kGodAlready) { r.code = admintool::kDoneAlready; return r; }
        if (step == admintool::kGodDead) { r.code = admintool::kDoneDead; return r; }
        if (coop::GodSet(uid, on) < 0) { r.code = admintool::kDoneFailed; return r; }
        const bool pushed = coop::StatePush(uid);   /* the other games learn it now, not at the next change */
        DebugLog("[GOD] '" + name + "' (uid " + N((long long)uid) + ") god mode " + (on ? "ON" : "OFF") + " - this game's memory; STATE " + (pushed ? "sent" : "not sent (link down)"));
        r.code = admintool::kDoneOk;
        if ((d.flags & admintool::kDoAskerHere) == 0) Say(adminwords::GodByAdminLine(name, on));   /* the target player is told (owner 1396) */
        return r;
    }
    if (d.op == (unsigned)admintool::kOpRevive)
    {   /* /revive: this player's fallen character comes back on the FALLEN tab's own road (on the main tick, as the tab runs it) -
           free and whatever the resurrection option, as this is an admin's job (owner 1394, 1398) */
        coop::BringBackOut out;
        if (!coop::ResurrectAdminRevive(d.target, &out)) { r.code = admintool::kDoneNotFound; r.name = admincmd::CleanText(d.target, admintool::kNameBytes); return r; }
        r.code = adminwords::ReviveDoneCode(out.ok, out.refusal);
        r.name = admincmd::CleanText(out.name, admintool::kNameBytes);
        if (out.ok)
        {
            r.names.push_back(admincmd::CleanText(out.mate, admintool::kNameBytes));
            if ((d.flags & admintool::kDoAskerHere) == 0) Say(adminwords::RevivedByAdminLine(out.name));   /* the target player is told (owner 1396) */
        }
        return r;
    }
    if (d.op != (unsigned)admintool::kOpWhere) { r.code = admintool::kDoneUnknownOp; return r; }
    std::string name; Ogre::Vector3 pos;
    if (!LeadAt(&name, &pos)) { r.code = admintool::kDoneNoLead; return r; }
    if (!admintool::CoordOk(pos.x) || !admintool::CoordOk(pos.z)) { r.code = admintool::kDoneFailed; return r; }
    r.code = admintool::kDoneOk; r.x = pos.x; r.z = pos.z;
    r.place = TownNear(pos.x, pos.z);
    return r;
}
void OnDo(const char* p, size_t n)
{
    admintool::Do d;
    if (!admintool::DecodeDo(p, n, &d)) { ++g_malformed; ErrorLog("[ADMIN] an ADMIN_DO of " + N((long long)n) + " bytes did not decode - ignored (the world server ends it as lapsed)"); return; }
    const int gen = coop::StoreLinkGen();
    std::vector<char> old;
    if (admintool::MemoFind(g_memo, gen, d.job, &old))
    {   /* a repeat of a finished job: the same answer, nothing done again */
        ++g_dosRepeat;
        DebugLog("[ADMIN] job " + N((long long)d.job) + " came again - its answer sent again, nothing done again");
        coop::StoreSendAdmin(admintool::kMsgAdminDone, old);
        return;
    }
    for (size_t i = 0; i < g_dos.size(); ++i)
        if (g_dos[i].gen == gen && g_dos[i].d.job == d.job) { ++g_dosRepeat; DebugLog("[ADMIN] job " + N((long long)d.job) + " came again while queued - kept once"); return; }
    for (size_t i = 0; i < g_standWait.size(); ++i)
        if (g_standWait[i].pd.gen == gen && g_standWait[i].pd.d.job == d.job) { ++g_dosRepeat; DebugLog("[ADMIN] job " + N((long long)d.job) + " came again while its character is healed first - kept once"); return; }
    if (g_dos.size() >= kDosMax)
    {
        ++g_dosFull;
        ErrorLog("[ADMIN] job " + N((long long)d.job) + " came with " + N((long long)g_dos.size()) + " jobs queued - answered failed (" + N(g_dosFull) + ")");
        admintool::Done r; r.job = d.job; r.code = admintool::kDoneFailed;
        SendDone(r, gen);
        return;
    }
    PendingDo pd; pd.d = d; pd.gen = gen; pd.at = ::GetTickCount();
    g_dos.push_back(pd);
    DebugLog("[ADMIN] job " + N((long long)d.job) + " (op " + N((long long)d.op) + ") queued - done on the next frame that runs gameplay");
}
/* a queued job past the world server's job limit (admintool::DoStale): its admin was told it lapsed, so it is dropped, never done
   late (T-1205 review fold 6) - true = dropped (logged, counted) */
bool DoDroppedStale(const PendingDo& pd, unsigned now)
{
    if (!admintool::DoStale(pd.at, now)) return false;
    ++g_dosStale;
    ErrorLog("[ADMIN] job " + N((long long)pd.d.job) + " (op " + N((long long)pd.d.op) + ") dropped - queued " + N((long long)(unsigned)(now - pd.at))
             + " ms ago, past the world server's job limit (it told its admin the job lapsed) (dosStale=" + N(g_dosStale) + ")");
    return true;
}
/* the queued jobs: another link's are dropped (the world server ends them as lapsed), and any past the job limit; this link's are
   done while gameplay runs and the engine takes writes */
void DosTick()
{
    if (g_dos.empty()) return;
    const int gen = coop::StoreLinkGen();
    const unsigned now = ::GetTickCount();
    std::vector<PendingDo> keep;
    size_t gone = 0;
    for (size_t i = 0; i < g_dos.size(); ++i)
    {
        if (g_dos[i].gen != gen || !Linked()) { ++gone; continue; }
        if (!DoDroppedStale(g_dos[i], now)) keep.push_back(g_dos[i]);
    }
    if (gone != 0) DebugLog("[ADMIN] " + N((long long)gone) + " queued job(s) dropped - the link they came on ended");
    g_dos.swap(keep);
    if (g_dos.empty() || !coop::GameplayRunning() || coop::EngineWritesBlocked()) return;
    std::vector<PendingDo> todo, safe;
    for (size_t i = 0; i < g_dos.size(); ++i) (SafePointOp(g_dos[i].d.op) ? safe : todo).push_back(g_dos[i]);
    g_dos.swap(safe);   /* those wait for the K2 safe point */
    for (size_t i = 0; i < todo.size(); ++i)
    {
        const admintool::Done r = DoJob(todo[i].d);
        ++g_dosDone;
        DebugLog("[ADMIN] job " + N((long long)r.job) + " done: code " + N((long long)r.code)
                 + (r.code == (unsigned)admintool::kDoneOk ? " at " + adminwords::Coord(r.x) + ", " + adminwords::Coord(r.z) + (r.place.empty() ? std::string(" (no town near)") : " near '" + r.place + "'") : std::string()));
        SendDone(r, todo[i].gen);
    }
}
/* /medic, /heal or /restore on one of this game's characters: *r's code (and for /restore its num = the lost limbs grown back, num2 =
   the robotic limbs left on), the healed count (*healed) or the dead one (*dead) */
void HealOne(::Character* c, const std::string& name, const admintool::Do& d, admintool::Done* r, unsigned* healed, unsigned* dead)
{
    int out = 0, grown = 0, robot = 0;
    const unsigned int uid = coop::FindSpawnedUid(c);
    const int level = d.op == (unsigned)admintool::kOpRestore ? coop::kAdminRestore : d.op == (unsigned)admintool::kOpHeal ? coop::kAdminHeal : coop::kAdminMedic;
    const int h = uid != 0 ? coop::AdminHealOwned(uid, level, &out, &grown, &robot) : -1;
    if (h == -2) { ++*dead; r->code = admintool::kDoneDead; return; }
    if (h != 1) { r->code = admintool::kDoneFailed; return; }
    ++*healed;
    r->num = (unsigned)grown; r->num2 = (unsigned)robot;
    r->code = out != 0 ? admintool::kDoneOut : admintool::kDoneOk;
    if ((d.flags & admintool::kDoAskerHere) == 0) Say(adminwords::HealedByAdminLine(name));   /* the target player is told (owner 1396) */
}
/* /bring, /tp, /goto: one of this game's characters to d's position (beside it with kDoNear). waitedMs: how long this job has waited
   since its knocked-out character was healed first (-1 = not healed). *wait = true: no answer yet - the job waits in g_standWait for
   the character to stand (admintool::MoveNextOf decides from the game's own dead and down reads) */
admintool::Done MoveOne(const admintool::Do& d, long waitedMs, bool* wait)
{
    *wait = false;
    admintool::Done r; r.job = d.job;
    ::Character* c = 0; std::string name;
    const int f = JobCharacter(d.target, &c, &name);
    if (f == 0) { r.code = admintool::kDoneNoLead; return r; }
    if (f < 0) { r.code = admintool::kDoneNotFound; return r; }
    r.name = admincmd::CleanText(name, admintool::kNameBytes);
    if (f == 2) { r.code = admintool::kDoneDead; return r; }   /* dead, on the fallen list only */
    const bool told = (d.flags & admintool::kDoAskerHere) == 0;   /* the target player is told (owner 1396) */
    const int dead = DeadPod(c);
    float x = d.x, z = d.z, y = 0.0f;
    if ((d.flags & admintool::kDoNear) != 0) admintool::NearSpot(d.x, d.z, &x, &z);
    const int m = dead == 0 ? coop::AdminMoveCharacter(c, x, z, (d.flags & admintool::kDoHaveY) != 0, d.y, c == LeadCharacter(), &y) : 0;
    switch (admintool::MoveNextOf(dead, m, (d.flags & admintool::kDoHealFirst) != 0, waitedMs))
    {
    case admintool::kMoveDone: break;
    case admintool::kMoveDead: r.code = admintool::kDoneDead; return r;
    case admintool::kMoveDown:
        r.code = admintool::kDoneDown;
        if (waitedMs >= 0)
        {   /* healed, not moved: its player is told of the heal */
            DebugLog("[ADMIN] job " + N((long long)d.job) + ": '" + name + "' was healed first and is still down after " + N((long long)waitedMs) + " ms - not moved");
            if (told) Say(adminwords::HealedByAdminLine(name));
        }
        return r;
    case admintool::kMoveHealThenWait:
    {   /* /heal's own call, here at the K2 safe point; the engine's medical tick stands it up when its knockout ends */
        int out = 0, grown = 0, robot = 0;
        const unsigned int uid = coop::FindSpawnedUid(c);
        const int h = uid != 0 ? coop::AdminHealOwned(uid, coop::kAdminHeal, &out, &grown, &robot) : -1;
        if (h == -2) { r.code = admintool::kDoneDead; return r; }
        if (h != 1) { r.code = admintool::kDoneFailed; return r; }
        DebugLog("[ADMIN] job " + N((long long)d.job) + ": '" + name + "' is knocked out - healed first (still out " + N((long long)out) + "), moved once it stands");
        *wait = true;
        return r;
    }
    case admintool::kMoveWait: *wait = true; return r;
    default: r.code = admintool::kDoneFailed; return r;
    }
    r.code = admintool::kDoneOk; r.x = x; r.y = y; r.z = z;
    r.num = waitedMs >= 0 ? 1u : 0u;   /* healed first */
    if (told) Say(adminwords::MovedByAdminLine(name));
    return r;
}
/* /home, /jail: the lead character's squad - this game's own living characters of it, the lead first - laid out at d's position;
   the knocked-out and the dead stay */
admintool::Done MoveSquad(const admintool::Do& d)
{
    admintool::Done r; r.job = d.job;
    ::Character* lead = 0; std::string leadName;
    if (JobCharacter(std::string(), &lead, &leadName) != 1) { r.code = admintool::kDoneNoLead; return r; }
    std::vector< ::Character*> members(1, lead);
    coop::SquadView sv;
    if (coop::ReadSquadOf(lead, &sv) == 1)
        for (int i = 0; i < sv.stored && i < coop::kSquadViewCap; ++i) if (sv.members[i] != 0 && sv.members[i] != lead) members.push_back(sv.members[i]);
    std::vector<std::string> moved, down; unsigned dead = 0, failed = 0; int k = 0;
    const ::Character* focusOn = lead;
    for (size_t i = 0; i < members.size(); ++i)
    {
        ::Character* c = coop::LiveCharacter(members[i]);
        std::string name;
        if (!OwnPlayerCharacter(c) || !coop::NameOf(c, &name)) continue;   /* only this game's own player characters */
        name = admincmd::CleanText(name, admintool::kNameBytes);
        const int dd = DeadPod(c);
        if (dd > 0) { ++dead; continue; }
        if (dd < 0) { ++failed; continue; }
        float x = 0.0f, z = 0.0f, y = 0.0f;
        admintool::SquadSpot(d.x, d.z, k, &x, &z);
        const int m = coop::AdminMoveCharacter(c, x, z, (d.flags & admintool::kDoHaveY) != 0, d.y, c == focusOn, &y);
        if (m == 1) { moved.push_back(name); ++k; }
        else if (m == 2) down.push_back(name);
        else ++failed;
    }
    admintool::SquadNames(moved, down, &r);
    r.num2 = dead;
    if (!moved.empty()) { r.code = admintool::kDoneOk; r.x = d.x; r.y = d.y; r.z = d.z; r.name = moved[0]; }
    else if (!down.empty()) { r.code = admintool::kDoneDown; r.name = down[0]; }
    else r.code = admintool::kDoneFailed;
    DebugLog("[ADMIN] squad move: " + N((long long)moved.size()) + " moved, " + N((long long)down.size()) + " knocked out stayed, " + N((long long)dead) + " dead stayed, "
             + N((long long)failed) + " not moved (a read or the teleport failed)");
    return r;
}
/* one job that changes a character, done now at the K2 safe point: its ADMIN_DONE */
admintool::Done DoSafeJob(const admintool::Do& d)
{
    admintool::Done r; r.job = d.job;
    if (d.op == (unsigned)admintool::kOpMove) { bool wait = false; return MoveOne(d, -1, &wait); }   /* AdminSafePointDrain calls MoveOne itself */
    if (d.op == (unsigned)admintool::kOpSquadMove) return MoveSquad(d);
    if (d.op == (unsigned)admintool::kOpAddItem)
    {   /* the admin's own lead character; what does not fit is not made (ItemsAdminMake destroys the rest it made) */
        ::Character* lead = LeadCharacter();
        std::string who;
        if (lead == 0 || !coop::NameOf(lead, &who)) { r.code = admintool::kDoneNoLead; return r; }
        r.name = admincmd::CleanText(who, admintool::kNameBytes);
        int units = 0; std::string why;
        const int given = coop::ItemsAdminMake(lead, d.extra, (int)d.num, &units, &why);
        if (given < 0)
        {   /* -1: the engine's add faulted (items.cpp left it alone); -2: not made */
            ErrorLog("[ADMIN] /additem " + d.extra + " not made: " + why);
            r.code = admintool::kDoneFailed; return r;
        }
        r.code = admintool::kDoneOk;
        r.num = (unsigned)(units > 0 ? units : 0);
        return r;
    }
    unsigned healed = 0, dead = 0;
    if ((d.flags & admintool::kDoAll) != 0)
    {   /* every character of the player's squad */
        std::vector< ::Character*> cs; std::vector<std::string> names;
        OwnPlayerCharacters(&cs, &names);
        if (cs.empty()) { r.code = admintool::kDoneNoLead; return r; }
        admintool::Done one;
        for (size_t i = 0; i < cs.size(); ++i) HealOne(cs[i], names[i], d, &one, &healed, &dead);
        dead += (unsigned)FallenNotIn(cs, std::string(), 0, 0);   /* the dead are off the update list: the fallen list counts them */
        r.code = (healed > 0 || dead > 0) ? admintool::kDoneOk : admintool::kDoneFailed;
        r.num = healed; r.num2 = dead;
        return r;
    }
    ::Character* c = 0; std::string name;
    const int f = JobCharacter(d.target, &c, &name);   /* the lead, or the one own player character of that name (the search counted it) */
    if (f == 0) { r.code = admintool::kDoneNoLead; return r; }
    if (f < 0) { r.code = admintool::kDoneNotFound; return r; }
    r.name = admincmd::CleanText(name, admintool::kNameBytes);
    if (f == 2) { r.code = admintool::kDoneDead; return r; }   /* dead, on the fallen list only: "Ruk is dead. Use /revive Ruk instead." */
    HealOne(c, name, d, &r, &healed, &dead);
    return r;
}
/* /medic, /heal or /restore typed */
void HealTyped(const std::string& typed, int verb)
{
    std::string target, extra; unsigned flags = 0; std::vector<std::string> lines;
    if (!adminwords::HealTyped(typed, verb, &target, &flags, &extra, &lines))
    {
        DebugLog(std::string("[ADMIN] ") + admincmd::VerbName(verb) + " typed wrong - answered on this game");
        for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]);
        return;
    }
    const int sent = coop::AdminRequest((unsigned)verb, target, flags, &extra);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::HealUsageLine(verb));
}
/* /god <name> on|off typed */
void GodTyped(const std::string& typed)
{
    std::string target, extra; unsigned flags = 0; std::vector<std::string> lines;
    if (!adminwords::GodTyped(typed, &target, &flags, &extra, &lines))
    {
        DebugLog("[ADMIN] /god typed wrong - answered on this game");
        for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]);
        return;
    }
    const int sent = coop::AdminRequest((unsigned)admincmd::kVerbGod, target, flags, &extra);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::GodUsageLine());
}
/* /additem typed: the item found here by name, then its record id and count go up */
void AddItemTyped(const std::string& typed)
{
    std::string item; int count = 1; std::vector<std::string> lines;
    if (!adminwords::AddItemTyped(typed, &item, &count, &lines)) { for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]); return; }
    if (!Linked()) { Say(adminwords::NotLinkedLine()); return; }
    if (adminwords::AddItemBusy(g_addWait, coop::StoreLinkGen())) { Say(adminwords::StillMakingLine()); return; }
    if (coop::GameplayRunning() && LeadCharacter() == 0) { DebugLog("[ADMIN] /additem: no character selected - said so"); Say(adminwords::NoCharacterLine()); return; }
    std::vector<std::pair<std::string, std::string> > raw;
    if (!coop::ItemsNamed(item, &raw)) DebugLog("[ADMIN] /additem: the game's item records could not be read");
    std::vector<admintool::ItemRow> rows;
    for (size_t i = 0; i < raw.size(); ++i) { admintool::ItemRow w; w.name = raw[i].first; w.sid = raw[i].second; rows.push_back(w); }
    size_t pick = 0; std::vector<std::string> names;
    const int found = admintool::ItemPick(item, rows, &pick, &names);
    if (found == admintool::kItemNone) { Say(adminwords::NoItemLine(item)); return; }
    if (found == admintool::kItemMany) { Say(adminwords::ManyItemsLine(item, names)); return; }
    std::string countText = N((long long)count);
    const int sent = coop::AdminRequest((unsigned)admincmd::kVerbAddItem, rows[pick].sid, 0u, &countText);
    if (sent == 0) { Say(adminwords::NotLinkedLine()); return; }
    if (sent < 0) { Say(adminwords::AddItemUsageLine()); return; }
    g_addWait.req = g_nextReq; g_addWait.gen = coop::StoreLinkGen(); g_addWait.asked = (unsigned)count; g_addWait.item = rows[pick].name;
    DebugLog("[ADMIN] /additem " + N((long long)count) + " x '" + rows[pick].name + "' (" + rows[pick].sid + ") asked (request " + N((long long)g_addWait.req) + ")");
}
/* /bring, /tp, /goto typed */
void MoveTyped(const std::string& typed, int verb)
{
    std::string target, extra; unsigned flags = 0; float x = 0.0f, z = 0.0f; std::vector<std::string> lines;
    if (!adminwords::MoveTyped(typed, verb, &target, &flags, &extra, &x, &z, &lines))
    {
        DebugLog(std::string("[ADMIN] ") + admincmd::VerbName(verb) + " typed wrong - answered on this game");
        for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]);
        return;
    }
    const int sent = coop::AdminRequestAt((unsigned)verb, target, flags, &extra, x, z);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::MoveUsageLine(verb));
}
/* /sethome, /home typed: nothing after the word */
void BareTyped(const std::string& typed, int verb)
{
    if (!adminwords::BareTyped(typed)) { Say(adminwords::MoveUsageLine(verb)); return; }
    const int sent = coop::AdminRequest((unsigned)verb, std::string(), 0u, 0);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::MoveUsageLine(verb));
}
/* /jail typed: "list" and a spot that is not one are answered here */
void JailTyped(const std::string& typed)
{
    std::string target, spot; unsigned flags = 0; std::vector<std::string> lines;
    const int j = adminwords::JailTyped(typed, &target, &flags, &spot, &lines);
    if (j == adminwords::kJailList) { Say(adminwords::JailListLine()); return; }
    if (j != adminwords::kJailSend) { for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]); return; }
    const int sent = coop::AdminRequest((unsigned)admincmd::kVerbJail, target, flags, &spot);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::MoveUsageLine(admincmd::kVerbJail));
}
/* /revive typed */
void ReviveTyped(const std::string& typed)
{
    std::string target, extra; unsigned flags = 0; std::vector<std::string> lines;
    if (!adminwords::ReviveTyped(typed, &target, &flags, &extra, &lines))
    {
        DebugLog("[ADMIN] /revive typed wrong - answered on this game");
        for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]);
        return;
    }
    const int sent = coop::AdminRequest((unsigned)admincmd::kVerbRevive, target, flags, &extra);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::ReviveUsageLine());
}
/* /here: this game's own answer from its lead character, never sent */
void HereTyped()
{
    std::string name; Ogre::Vector3 pos;
    std::vector<std::pair<std::string, float> > rows; unsigned faults = 0;
    int k = -1;
    if (coop::GameplayRunning() && LeadCharacter() == 0)
    {
        ++g_hereNoLead;
        DebugLog("[ADMIN] /here: no character selected - said so (" + N(g_hereNoLead) + ")");
        Say(adminwords::NoCharacterLine());
        return;
    }
    if (coop::GameplayRunning() && LeadAt(&name, &pos) && coop::TownDistances(pos.x, pos.z, &rows, &faults)) k = swtownnear::Nearest(rows);
    if (k < 0)
    {
        ++g_hereNoLead;
        ErrorLog("[ADMIN] /here: no lead character, its position or name did not read, or no town was read - nothing said (" + N(g_hereNoLead) + ")");
        return;
    }
    DebugLog("[ADMIN] /here answered on this game");
    Say(adminwords::HereLine(name, pos.x, pos.y, pos.z, rows[(size_t)k].first, rows[(size_t)k].second));
}
void WhereTyped(const std::string& typed)
{
    admincmd::Req q; std::vector<std::string> lines;
    if (!adminwords::WhereTyped(typed, &q.target, &q.flags, &lines))
    {
        DebugLog("[ADMIN] /where typed wrong - answered on this game");
        for (size_t i = 0; i < lines.size(); ++i) Say(lines[i]);
        return;
    }
    const int sent = coop::AdminRequest((unsigned)admincmd::kVerbWhere, q.target, q.flags, 0);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::WhereUsageLine());
}

void OnReply(const char* p, size_t n)
{
    admincmd::Reply r;
    if (!admincmd::DecodeReply(p, n, &r)) { ++g_malformed; ErrorLog("[ADMIN] an ADMIN_REPLY of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    DebugLog("[ADMIN] answer to " + std::string(admincmd::VerbName((int)r.verb)) + " (request " + N((long long)r.req) + "): code " + N((long long)r.code));
    if (adminwords::AddItemEnds(g_addWait, coop::StoreLinkGen(), r))
    {
        const adminwords::AddItemWait w = g_addWait;
        g_addWait = adminwords::AddItemWait();
        if (r.code == (unsigned)admincmd::kRepOk) { Say(adminwords::AddItemLine(r.num, w.asked, w.item, r.name)); return; }
    }
    Say(adminwords::ReplyLine(r));
}

}   /* namespace */

namespace coop {

void AdminTyped(std::string* typed)
{
    const int route = adminauth::SlashRoute(*typed);
    if (route == adminauth::kRouteLogin)
    {
        std::string pw = adminauth::LoginPassword(*typed);
        Wipe(typed);
        LoginTyped(&pw, false);
        Wipe(&pw);
        return;
    }
    if (route == adminauth::kRouteLogout) { LogoutTyped(); return; }
    const std::string word = adminauth::FirstWord(*typed);
    if (word == "/help") { const std::vector<std::string> h = adminwords::HelpLines(); for (size_t i = 0; i < h.size(); ++i) Say(h[i]); return; }
    if (word == "/where") { WhereTyped(*typed); return; }
    if (word == "/medic") { HealTyped(*typed, admincmd::kVerbMedic); return; }
    if (word == "/heal") { HealTyped(*typed, admincmd::kVerbHeal); return; }
    if (word == "/restore") { HealTyped(*typed, admincmd::kVerbRestore); return; }
    if (word == "/additem") { AddItemTyped(*typed); return; }
    if (word == "/bring") { MoveTyped(*typed, admincmd::kVerbBring); return; }
    if (word == "/tp") { MoveTyped(*typed, admincmd::kVerbTp); return; }
    if (word == "/goto") { MoveTyped(*typed, admincmd::kVerbGoto); return; }
    if (word == "/sethome") { BareTyped(*typed, admincmd::kVerbSetHome); return; }
    if (word == "/home") { BareTyped(*typed, admincmd::kVerbHome); return; }
    if (word == "/jail") { JailTyped(*typed); return; }
    if (word == "/revive") { ReviveTyped(*typed); return; }
    if (word == "/god") { GodTyped(*typed); return; }
    if (word == "/here")
    {
        if (!adminwords::HereTyped(*typed)) { Say(adminwords::HereUsageLine()); return; }
        HereTyped();
        return;
    }
    admincmd::Cmd c;
    const int pr = admincmd::Parse(*typed, &c);
    if (pr != admincmd::kParseOk)
    {
        DebugLog("[ADMIN] a slash line answered on this game (" + std::string(pr == admincmd::kParseUnknown ? "not a command" : "a command typed wrong") + ")");
        Say(adminwords::ParseLine(pr, c.verb));
        return;
    }
    const int sent = AdminRequest((unsigned)c.verb, c.target, 0u, &c.extra);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::UsageLine(c.verb));
}

int AdminRequest(unsigned verb, const std::string& target, unsigned flags, std::string* extra)
{
    return AdminRequestAt(verb, target, flags, extra, 0.0f, 0.0f);
}

int AdminRequestAt(unsigned verb, const std::string& target, unsigned flags, std::string* extra, float x, float z)
{
    admincmd::Req q;
    q.req = ++g_nextReq; if (q.req == 0) q.req = ++g_nextReq;
    q.verb = verb; q.flags = flags; q.target = target; q.x = x; q.z = z;
    if (extra != 0) q.extra = *extra;
    std::vector<char> b;
    const bool encoded = admincmd::EncodeReq(q, &b);
    Wipe(&q.extra);
    if (extra != 0) Wipe(extra);
    if (!encoded) { WipeBytes(&b); return -1; }
    if (!Linked()) { WipeBytes(&b); return 0; }
    const bool sent = StoreSendAdmin(admincmd::kMsgAdminReq, b);
    WipeBytes(&b);
    if (!sent) return 0;
    DebugLog("[ADMIN] " + std::string(admincmd::VerbName((int)verb)) + " sent to the world server (request " + N((long long)q.req) + ")");
    return 1;
}

void AdminArrive(unsigned type, const std::vector<char>& payload)
{
    const char* p = payload.empty() ? 0 : &payload[0];
    const size_t n = payload.size();
    if (type == admincmd::kMsgAdminReply) { OnReply(p, n); return; }
    if (type == adminauth::kMsgAdminStanding) { OnStanding(p, n); return; }
    if (type == admintool::kMsgAdminDo) { OnDo(p, n); return; }
    if (type != adminauth::kMsgAdminLogin) return;
    const int kind = adminauth::KindOf(p, n);
    if (kind == adminauth::kKindChallenge) OnChallenge(p, n);
    else if (kind == adminauth::kKindResult) OnResult(p, n);
    else { ++g_malformed; if (g_malformed <= 5) ErrorLog("[ADMIN] an ADMIN_LOGIN of kind " + N((long long)kind) + " came down - ignored"); }
}

void AdminTick()
{
    const unsigned now = ::GetTickCount();
    const int gen = StoreLinkGen();
    if (!g_pendingPw.empty())
    {
        if (g_pendingGen != gen || !Linked()) PendingWipe("the link to the world server changed");
        else if (adminwords::PendingStale(g_pendingAt, now)) { PendingWipe("its challenge did not come in time"); if (g_pendingAuto == 0) Say(adminwords::ResultLine(adminauth::kResNoChallenge, 0)); g_pendingAuto = 0; }
    }
    if (g_typedLoginGen != gen && !g_typedLogin.empty()) Wipe(&g_typedLogin);
    if (g_addWait.req != 0 && !adminwords::AddItemBusy(g_addWait, gen)) g_addWait = adminwords::AddItemWait();   /* its link ended */
    DosTick();
    /* THIS GAME'S REMEMBERED LOGIN BY ITSELF: at once after each WELCOME when the door carried no login proof, else kAutoRetryMs after
       it (the door's own login comes with the WELCOME); once more kAutoRetryMs later if it was not taken. Not while this game is an
       admin or the owner already. Not after this player's own /logout until this game leaves the world (owner 1412 b). */
    if (Linked() && adminwords::StandingNow(g_stand, gen) == (unsigned)adminauth::kStandNone && g_pendingPw.empty())
    {
        if (g_autoGen != gen) { g_autoGen = gen; g_autoTries = 0; g_autoAt = now; }
        const long worldGen = StoreWorldGenNow();
        if (adminwords::LogoutHolds(g_logoutHold, worldGen) && g_holdLoggedGen != worldGen)
        {
            g_holdLoggedGen = worldGen;
            DebugLog("[ADMIN] this game's remembered login is not tried by itself - this player typed /logout in this world (it is tried again after the player leaves the world; a typed /login works)");
        }
        if (adminwords::AutoLoginDue(g_autoTries, StoreDoorLoginSentThisLink() != 0, (unsigned)(now - g_autoAt), g_logoutHold, worldGen))
        {
            std::string pw;
            if (StoreLoginPasswordForLink(&pw)) { ++g_autoTries; g_autoAt = now; LoginTyped(&pw, true); }
            Wipe(&pw);
        }
    }
    if (g_stand.gen >= 0 && (g_stand.gen != gen || !Linked()))
    {
        std::string line;
        (void)adminwords::StandingLinkEnd(&g_stand, &line);
        DebugLog("[ADMIN] the link to the world server changed or went down - this game is not an admin until the world server's next standing");
        Say(line);
    }
}

void AdminSafePointDrain()
{
    if ((g_dos.empty() && g_standWait.empty()) || !GameplayRunning() || EngineWritesBlocked()) return;
    const int gen = StoreLinkGen();
    const unsigned now = ::GetTickCount();
    if (!g_standWait.empty())
    {   /* the healed-first moves: each tried again until its character stands, at most admintool::kStandWaitMs; another link's are
           dropped (the world server ends them as lapsed) */
        std::vector<StandWait> waits; waits.swap(g_standWait);
        for (size_t i = 0; i < waits.size(); ++i)
        {
            if (waits[i].pd.gen != gen || !Linked()) { DebugLog("[ADMIN] job " + N((long long)waits[i].pd.d.job) + " dropped while its character was healed first - the link it came on ended"); continue; }
            if (DoDroppedStale(waits[i].pd, now))
            {   /* healed first and never moved: its player is still told of the heal (owner 1396) */
                ::Character* c = 0; std::string name;
                if ((waits[i].pd.d.flags & admintool::kDoAskerHere) == 0 && JobCharacter(waits[i].pd.d.target, &c, &name) > 0) Say(adminwords::HealedByAdminLine(name));
                continue;
            }
            bool wait = false;
            const admintool::Done r = MoveOne(waits[i].pd.d, (long)(unsigned)(now - waits[i].since), &wait);
            if (wait) { g_standWait.push_back(waits[i]); continue; }
            ++g_dosDone;
            DebugLog("[ADMIN] job " + N((long long)r.job) + " (a healed-first move) done at the safe point: code " + N((long long)r.code));
            SendDone(r, waits[i].pd.gen);
        }
    }
    std::vector<PendingDo> keep, todo;
    for (size_t i = 0; i < g_dos.size(); ++i) (SafePointOp(g_dos[i].d.op) && g_dos[i].gen == gen && Linked() ? todo : keep).push_back(g_dos[i]);
    if (todo.empty()) return;
    g_dos.swap(keep);
    for (size_t i = 0; i < todo.size(); ++i)
    {
        if (DoDroppedStale(todo[i], now)) continue;
        if (todo[i].d.op == (unsigned)admintool::kOpMove)
        {
            bool wait = false;
            const admintool::Done r = MoveOne(todo[i].d, -1, &wait);
            if (wait) { StandWait w; w.pd = todo[i]; w.since = now; g_standWait.push_back(w); continue; }
            ++g_dosDone;
            DebugLog("[ADMIN] job " + N((long long)r.job) + " (a move) done at the safe point: code " + N((long long)r.code));
            SendDone(r, todo[i].gen);
            continue;
        }
        const admintool::Done r = DoSafeJob(todo[i].d);
        ++g_dosDone;
        DebugLog("[ADMIN] job " + N((long long)r.job) + " (op " + N((long long)todo[i].d.op) + ") done at the safe point: code " + N((long long)r.code)
                 + " num " + N((long long)r.num) + "/" + N((long long)r.num2));
        SendDone(r, todo[i].gen);
    }
}

bool AdminNow()
{
    return Linked() && adminwords::AdminBy(adminwords::StandingNow(g_stand, StoreLinkGen()));
}

bool AdminLogoutHolds()
{
    return adminwords::LogoutHolds(g_logoutHold, StoreWorldGenNow());
}

bool AdminOwnerNow()
{
    return Linked() && adminwords::StandingNow(g_stand, StoreLinkGen()) == (unsigned)adminauth::kStandOwner;
}

int AdminSetPassword(std::string* pw, const char* from)
{
    adminwords::PasswordTrim(pw);
    const int done = adminwords::PasswordDone(*pw);
    if (done != adminwords::kPwSend)
    {
        Wipe(pw);
        DebugLog(std::string("[ADMIN] the admin password not sent from ") + from + " ("
                 + (done == adminwords::kPwKeep ? "the box was empty - left as it is" : "a control character or too long") + ")");
        return done;
    }
    const int sent = AdminRequest((unsigned)admincmd::kVerbSetPassword, std::string(), 0u, pw);   /* wipes *pw */
    Wipe(pw);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    DebugLog(std::string("[ADMIN] the admin password ") + (sent == 1 ? "sent" : sent == 0 ? "not sent (not linked)" : "not sent (it would not encode)") + " from " + from);
    return sent == 1 ? done : adminwords::kPwNotSent;
}

int AdminListChange(int slot, bool make, const char* from)
{
    const int sent = AdminRequest((unsigned)adminwords::AdminRowVerb(!make), N((long long)slot), admincmd::kFlagSlot, 0);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    DebugLog(std::string("[ADMIN] ") + adminwords::AdminRowButton(!make) + " for slot " + N((long long)slot) + " from " + from + (sent == 1 ? " - sent" : " - not sent"));
    return sent;
}

std::string AdminTestCommand(const std::string& args)
{
    admincmd::TestAsk a;
    const std::string shown = admincmd::Trim(args);   /* fixed words only: the password is never among them */
    if (!admincmd::TestParse(args, &a))
    {
        DebugLog("[ADMIN] TEST-ONLY admintest REFUSED - not a form it knows (" + N((long long)shown.size()) + " characters, not logged: they may hold a password)");
        return "error admintest usage: setpw sample | makeadmin <slot> | removeadmin <slot> | type login right|login wrong|logout|kick <slot>|ban <slot>|ban nobody|unban <slot>|stop | typeline <words: name:<uid> player:<slot> q:<text> hash:<text> filled in>";
    }
    if (a.kind == admincmd::kTestSetPw)
    {
        std::string pw(admincmd::kTestPassword);
        const int done = AdminSetPassword(&pw, "the TEST-ONLY admintest lever");
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - the sample password through the MULTIPLAYER window's DONE road");
        return done == adminwords::kPwSend ? "ok admintest " + shown : "error admintest " + shown + " not-sent";
    }
    if (a.kind == admincmd::kTestMakeAdmin || a.kind == admincmd::kTestRemoveAdmin)
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - the PLAYERS tab's admin row road");
        const int sent = AdminListChange(a.slot, a.kind == admincmd::kTestMakeAdmin, "the TEST-ONLY admintest lever");
        return sent == 1 ? "ok admintest " + shown : "error admintest " + shown + " not-sent";
    }
    if (a.kind == admincmd::kTestTypeLine)
    {   /* T-1205: any line, the name words filled in here; then the same call the chat box's Enter makes for a '/' line */
        std::vector<std::string> names(a.words.size());
        for (size_t i = 0; i < a.words.size(); ++i)
        {
            const admincmd::LineWord& lw = a.words[i];
            if (lw.kind == admincmd::kLineWordChar)
            {
                ::Character* c = coop::LiveCharacter(coop::FindSpawned(lw.num));
                if (c == 0 || !coop::NameOf(c, &names[i]) || names[i].empty())
                {
                    DebugLog("[ADMIN] TEST-ONLY admintest typeline REFUSED - no loaded character with a name for name:" + N((long long)lw.num));
                    return "error admintest typeline no-name name:" + N((long long)lw.num);
                }
            }
            else if (lw.kind == admincmd::kLineWordPlayer && StoreRosterNameOf((int)lw.num, &names[i]) != 1)
            {
                DebugLog("[ADMIN] TEST-ONLY admintest typeline REFUSED - slot " + N((long long)lw.num) + " has no name on this link's roster");
                return "error admintest typeline no-name player:" + N((long long)lw.num);
            }
        }
        std::string line = admincmd::TestLineJoin(a.words, names);
        if (line.empty())
        {
            DebugLog("[ADMIN] TEST-ONLY admintest typeline REFUSED - a name cannot be typed (a quote mark in it)");
            return "error admintest typeline no-line";
        }
        const bool login = adminauth::SlashRoute(line) == adminauth::kRouteLogin;
        DebugLog("[ADMIN] TEST-ONLY admintest typeline - typed into the chat-command road: "
                 + (login ? std::string("a /login line (its password not logged)") : "'" + line + "'"));
        AdminTyped(&line);
        Wipe(&line);
        return "ok admintest typeline";
    }
    std::string name;
    if (admincmd::TestNeedsName(a) && StoreRosterNameOf(a.slot, &name) != 1)
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " REFUSED - slot " + N((long long)a.slot) + " has no name on this link's roster");
        return "error admintest " + shown + " no-name";
    }
    std::string typed = admincmd::TestTypedLine(a, name);
    if (typed.empty())
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " REFUSED - the slot's name cannot be typed (a quote mark in it)");
        return "error admintest " + shown + " no-line";
    }
    DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - typed into the chat-command road");
    AdminTyped(&typed);
    Wipe(&typed);
    return "ok admintest " + shown;
}

}
