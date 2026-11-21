/* src/common/playermods.h - T-938 + T-247 (owner decisions 1679, 1681, 1686, 1692, 2026-11-15; world-server protocol 158):
 * PLAYER_MODS, THE WORLD SERVER TELLING EVERY GAME WHO HAS WHICH MODS, AND WHAT THE WORLD'S LIST IS.
 *
 * Down only (world server -> game), message 104. Two kinds:
 *   kKindWorld  - the world's list as the world server holds it (mods.txt): whether there is one, who set it and when, the
 *                 DIFFERENT MODS value, and the list itself. Sent to a game at its admission and to every game when the list is
 *                 set (USE MY MODS FOR THIS WORLD) or found changed by hand at a greeting; flags say which, and whether this
 *                 game is an admin to be told the world has no list (the approved page, version 3 section 3).
 *   kKindPlayer - one player's verdict against the world's list: slot, name, verdict, the DIFFERENT MODS value, that player's
 *                 counts and (when it differs) the detail line. Sent to every game for a newcomer, to a newcomer for every game
 *                 already in, and to every game for every game after a list change. Every game logs it (section 8), so a bug
 *                 report from anyone shows who has which mods.
 * Nothing here disconnects anyone (1681).
 *
 * Pure: no global, no operating-system call, no engine header. Compiled into the plugin, the world server and the offline test exe.
 * C++03 (VS2010 v100).
 */
#ifndef SW_PLAYERMODS_H
#define SW_PLAYERMODS_H

#include "modlist.h"
#include <string>
#include <vector>
#include <cstdio>

namespace playermods {

const unsigned int kMsgPlayerMods = 104u;   /* down only */
const unsigned int kProtocol = 173u;        /* the world-server protocol that carries PLAYER_MODS */
const size_t kNameMax = 128, kByMax = 128, kAtMax = 16, kDetailMax = 4096;

enum Kind { kKindWorld = 1, kKindPlayer = 2 };
/* kKindWorld flags */
const unsigned kFlagChanged = 1u;     /* the list was just set (by an admin) or found changed by hand: every game is told once */
const unsigned kFlagTellAdmin = 2u;   /* the world has no list and this game is an admin: told once (section 3) */
const unsigned kFlagByHand = 4u;      /* the change was a hand edit (or delete) of mods.txt, seen at a greeting */
const unsigned kFlagYouSetIt = 8u;    /* this game's own USE MY MODS set it ("set by you") */
const unsigned kFlagsKnown = kFlagChanged | kFlagTellAdmin | kFlagByHand | kFlagYouSetIt;

/* kKindPlayer verdicts */
enum Verdict { kVNotChecked = 0, kVMatch = 1, kVDiffers = 2, kVUnreadable = 3, kVAdminLetIn = 4 };

struct World
{
    unsigned hasList, flags, policy;
    std::string by, at;
    coopmods::ModList list;   /* when hasList */
    World() : hasList(0), flags(0), policy(0) {}
};
struct Player
{
    unsigned slot, verdict, policy, nActive, nPlugins;
    std::string name, detail;
    Player() : slot(0), verdict(0), policy(0), nActive(0), nPlugins(0) {}
};

inline void PutStr(std::vector<char>* o, const std::string& s, size_t cap) { coopmods::ModPutU32(o, (unsigned)(s.size() > cap ? cap : s.size())); o->insert(o->end(), s.begin(), s.begin() + (s.size() > cap ? cap : s.size())); }
inline bool GetStr(const std::vector<char>& p, size_t* at, size_t cap, std::string* s)
{
    unsigned n = 0;
    if (!coopmods::ModGetU32(p, at, &n) || n > cap || p.size() - *at < (size_t)n) return false;
    s->assign(p.begin() + *at, p.begin() + *at + n); *at += n;
    return true;
}

/* {u32 kind=1, u32 hasList, u32 flags, u32 policy, str by, str at, [ModList when hasList]} */
inline void EncodeWorld(const World& w, std::vector<char>* out)
{
    out->clear();
    coopmods::ModPutU32(out, kKindWorld); coopmods::ModPutU32(out, w.hasList ? 1u : 0u); coopmods::ModPutU32(out, w.flags & kFlagsKnown);
    coopmods::ModPutU32(out, w.policy ? 1u : 0u);
    PutStr(out, w.by, kByMax); PutStr(out, w.at, kAtMax);
    if (w.hasList) coopmods::ModListEncode(w.list, out);
}
/* {u32 kind=2, u32 slot, str name, u32 verdict, u32 policy, u32 nActive, u32 nPlugins, str detail} */
inline void EncodePlayer(const Player& p, std::vector<char>* out)
{
    out->clear();
    coopmods::ModPutU32(out, kKindPlayer); coopmods::ModPutU32(out, p.slot); PutStr(out, p.name, kNameMax);
    coopmods::ModPutU32(out, p.verdict); coopmods::ModPutU32(out, p.policy ? 1u : 0u);
    coopmods::ModPutU32(out, p.nActive); coopmods::ModPutU32(out, p.nPlugins); PutStr(out, p.detail, kDetailMax);
}
/* the kind of a payload, or 0 for none */
inline unsigned KindOf(const std::vector<char>& p) { size_t at = 0; unsigned k = 0; return coopmods::ModGetU32(p, &at, &k) ? k : 0u; }
inline bool DecodeWorld(const std::vector<char>& p, World* w)
{
    *w = World();
    size_t at = 0; unsigned k = 0;
    if (!coopmods::ModGetU32(p, &at, &k) || k != kKindWorld) return false;
    if (!coopmods::ModGetU32(p, &at, &w->hasList) || !coopmods::ModGetU32(p, &at, &w->flags) || !coopmods::ModGetU32(p, &at, &w->policy)) return false;
    if (w->hasList > 1 || (w->flags & ~kFlagsKnown) != 0 || w->policy > 1) return false;
    if (!GetStr(p, &at, kByMax, &w->by) || !GetStr(p, &at, kAtMax, &w->at)) return false;
    if (w->hasList && !coopmods::ModListDecode(p, &at, &w->list)) return false;
    return at == p.size();
}
inline bool DecodePlayer(const std::vector<char>& p, Player* r)
{
    *r = Player();
    size_t at = 0; unsigned k = 0;
    if (!coopmods::ModGetU32(p, &at, &k) || k != kKindPlayer) return false;
    if (!coopmods::ModGetU32(p, &at, &r->slot) || !GetStr(p, &at, kNameMax, &r->name) || !coopmods::ModGetU32(p, &at, &r->verdict)
        || !coopmods::ModGetU32(p, &at, &r->policy) || !coopmods::ModGetU32(p, &at, &r->nActive) || !coopmods::ModGetU32(p, &at, &r->nPlugins)
        || !GetStr(p, &at, kDetailMax, &r->detail)) return false;
    if (r->verdict > (unsigned)kVAdminLetIn || r->policy > 1) return false;
    return at == p.size();
}

/* A connected game's verdict from the HELLO's (coopmods::ModsHelloVerdict) or a re-check's (coopmods::ModsRecheckVerdict). */
inline unsigned VerdictOfHello(int v)
{
    if (v == coopmods::kModsHelloMatch) return kVMatch;
    if (v == coopmods::kModsHelloAdmitDiffers) return kVDiffers;
    if (v == coopmods::kModsHelloAdmitUnreadable) return kVUnreadable;
    if (v == coopmods::kModsHelloAdmitAdmin) return kVAdminLetIn;
    return kVNotChecked;
}
inline unsigned VerdictOfRecheck(int r)
{
    if (r == coopmods::kMrMatch) return kVMatch;
    if (r == coopmods::kMrDiffers) return kVDiffers;
    if (r == coopmods::kMrUnreadable) return kVUnreadable;
    return kVNotChecked;
}

inline std::string Num(unsigned v) { char b[16]; std::sprintf(b, "%u", v); return b; }
inline std::string Counts(unsigned a, unsigned p) { return Num(a) + " mods, " + Num(p) + " DLLs"; }
/* THE LOG LINES EVERY GAME AND THE WORLD SERVER WRITE (the approved page, version 3 section 8). */
inline std::vector<std::string> PlayerLines(const Player& r)
{
    std::vector<std::string> o;
    const std::string who = "[MODS] player '" + r.name + "' (slot " + Num(r.slot) + "): ";
    const std::string counts = "(" + Counts(r.nActive, r.nPlugins) + ")";
    if (r.verdict == kVMatch) o.push_back(who + "mods match the world's list " + counts);
    else if (r.verdict == kVDiffers)
    {
        o.push_back(who + "mods differ from the world's list " + counts + ". " + r.detail);
        o.push_back(who + "let in (DIFFERENT MODS: " + coopmods::ModsPolicyWord((int)r.policy) + ")");
    }
    else if (r.verdict == kVAdminLetIn)
    {
        o.push_back(who + "mods differ from the world's list " + counts + ". " + r.detail);
        o.push_back(who + "mods differ - let in as an admin (DIFFERENT MODS: " + coopmods::ModsPolicyWord((int)r.policy) + ")");
    }
    else if (r.verdict == kVUnreadable) o.push_back(who + "its game could not read its own mod list - not compared (DIFFERENT MODS: " + coopmods::ModsPolicyWord((int)r.policy) + ")");
    else o.push_back(who + "mods not checked - the world has no mod list yet");
    return o;
}
inline std::string NoListLine() { return "[MODS] the world has no mod list yet - nobody's mods are checked; admins are told"; }
inline std::string SetLine(const std::string& by, const coopmods::ModList& m, int byHand)
{
    const bool none = m.active.empty() && m.plugins.empty();
    return std::string("[MODS] the world's mod list was ") + (byHand ? "changed by hand in mods.txt" : "set by '" + by + "'") + " ("
         + Counts((unsigned)m.active.size(), (unsigned)m.plugins.size()) + (none ? " - a list of no mods" : "") + "); connected players stay";
}
inline std::string RemovedLine() { return "[MODS] the world's mod list was removed by hand (mods.txt deleted) - nobody's mods are checked; connected players stay"; }

/* THE RECEIVING GAME'S DEDUPLICATION: a player row already logged with the same words is not logged again (a rejoin, a repeat). */
inline bool RowIsNew(std::vector<std::pair<unsigned, std::string> >* seen, const Player& r)
{
    std::string key = Num(r.verdict) + "|" + Num(r.policy) + "|" + Num(r.nActive) + "|" + Num(r.nPlugins) + "|" + r.name + "|" + r.detail;
    for (size_t i = 0; i < seen->size(); ++i)
        if ((*seen)[i].first == r.slot) { if ((*seen)[i].second == key) return false; (*seen)[i].second = key; return true; }
    seen->push_back(std::make_pair(r.slot, key));
    return true;
}

/* WHAT A GAME SHOWS ITS PLAYER FOR A kKindWorld (the notices of sections 3 and 7). "" = nothing. A list changed: matching = the
   one-line notice; differing = the let-in notice with the new differences (plus the Rejected sentence). No list, admin = section 3. */
inline std::string NoticeOfWorld(const World& w, const coopmods::ModList& mine)
{
    if (!w.hasList) return (w.flags & kFlagTellAdmin) ? coopmods::ModNoListAdminText() : std::string();
    if (!(w.flags & kFlagChanged) || (w.flags & kFlagYouSetIt)) return std::string();
    if (mine.known && coopmods::ModListSame(w.list, mine)) return coopmods::ModChangedMatchText((w.flags & kFlagByHand) ? std::string() : w.by);
    return coopmods::ModChangedDiffersText(coopmods::ModListDiff(w.list, mine), mine.known, (int)w.policy);
}

/* T-247 (owner 1703, 2026-11-16): THE MODS REFUSE AFTER ITS FIRST THREE WORDS {proto, gameProto, reason} - {u32 login note
   (coopstore kModsLogin*), u32 the login lockout's wait in s (kModsLoginLocked, else 0), u32 flags, the world's list (ModListEncode)}.
   kRefuseListed: the joiner's own person id is on the world's admin list (admins.txt) and an admin password is set - only then does its
   MODS DON'T MATCH box offer JOIN AS ADMIN beside OK. It says the joiner's own standing only, never another player's. */
const unsigned kRefuseListed = 1u;
const unsigned kRefuseFlagsKnown = kRefuseListed;
const size_t kRefuseTailAt = 12;   /* after {proto, gameProto, reason} */
struct ModsRefuseTail
{
    unsigned loginNote, lockWait, flags;
    int haveWorld;              /* the world's list came with it */
    coopmods::ModList world;
    ModsRefuseTail() : loginNote(0), lockWait(0), flags(0), haveWorld(0) {}
};
inline void ModsRefuseTailEncode(const ModsRefuseTail& t, std::vector<char>* out)
{
    coopmods::ModPutU32(out, t.loginNote); coopmods::ModPutU32(out, t.lockWait); coopmods::ModPutU32(out, t.flags & kRefuseFlagsKnown);
    coopmods::ModListEncode(t.world, out);
}
/* false = the three words are not all there (*t then says nothing: no note, no flag); a missing or malformed list leaves haveWorld 0 */
inline bool ModsRefuseTailDecode(const std::vector<char>& p, ModsRefuseTail* t)
{
    *t = ModsRefuseTail();
    size_t at = kRefuseTailAt;
    ModsRefuseTail r;
    if (!coopmods::ModGetU32(p, &at, &r.loginNote) || !coopmods::ModGetU32(p, &at, &r.lockWait) || !coopmods::ModGetU32(p, &at, &r.flags)) return false;
    r.flags &= kRefuseFlagsKnown;
    r.haveWorld = coopmods::ModListDecode(p, &at, &r.world) ? 1 : 0;
    *t = r;
    return true;
}
/* the MODS DON'T MATCH box's buttons: OK always; JOIN AS ADMIN only for a joiner the world server says is listed (owner 1703) */
const unsigned kBoxOk = 1u, kBoxJoinAsAdmin = 2u;
inline unsigned ModsRefuseBoxButtons(unsigned flags) { return kBoxOk | ((flags & kRefuseListed) ? kBoxJoinAsAdmin : 0u); }

}   /* namespace playermods */

#endif
