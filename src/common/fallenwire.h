/* src/common/fallenwire.h - T-556: THE FALLEN LIST. A snapshot of one of this player's own characters, taken at its death,
 * and the decisions the TEST-ONLY bring-back makes. Pure: no engine memory, no Windows. The offline suite
 * (src/coop-test/test_main.cpp) checks the same code the plugin runs (src/coop-plugin/resurrect.cpp).
 *
 * THE SNAPSHOT (one Fallen) holds what the SPAWN road already carries to rebuild a lookalike on another game, plus the death:
 *   uid            the dead character's uid (the key: a second snapshot for the same uid replaces the first)
 *   templateName   its template GameData NAME (CreateAt looks it up as CHARACTER, else RECORD_ANIMAL)
 *   name           its shown name (cut to 64 bytes at a UTF-8 boundary, as MSG_NAME)
 *   race           the race record name the engine reports (the appearance object's race; "?" when unreadable)
 *   sid            a named character's GameData string id (the key uniques.txt keeps its state under); "" for any other
 *   unique         1 = a named unique (the engine's own "unique" property), 0 = not
 *   animal         1 = an animal (pet / pack animal), 0 = a person
 *   age            an animal's age 0..1 (what a SPAWN carries); 0 for a person
 *   statsHave      1 = stats[] holds the 44 values (statswire.h order, raw 4-byte patterns), 0 = they were unreadable
 *   look           the appearance record exactly as appearance_record.cpp SerialiseRecord writes it (opaque here)
 *   x, y, z        where it died (world units)
 *   diedUnix       when it died (wall clock, seconds since 1970, UTC)
 *   cause          how the engine's death call was reached (kCause*)
 *   handIndex, handSerial   the engine handle of the dead body (RootObjectBase::getHandle)
 *   place          the record name of the town nearest the death (the engine's TownList, NearestTown below); "" none read
 *
 * THE LIST is newest first and holds at most kFallenKeep (40, owner 510). It is a plain vector of Fallen, and each entry has a byte form
 * (EncodeFallen / DecodeFallen, a version byte then length-checked fields). The world server keeps each profile's list and a
 * game's list is its copy (below: the wire, the file, the book, the copy's rule), and a named character brought back is marked
 * so in the server's uniques.txt (below: the bring-back rule).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "statswire.h"
#include "slavewire.h"
#include "deferredmerge.h"

namespace swfallen {

const int kFallenKeep = 40;              /* the list's length (owner 510): a 41st death sends the oldest away */
const int kStatsCount = 44;              /* statswire.h kStatsCount */
/* compile-time: the snapshot's stats are exactly the statswire.h block (44 values, 176 bytes) */
typedef char StatsCountMatchesStatswire[(kStatsCount == coopstats::kStatsCount
                                         && (size_t)kStatsCount * 4 == coopstats::kStatsBlockBytes) ? 1 : -1];
const unsigned char kFallenVersion = 4;  /* the byte form's first byte: 4 carries the place, the in-game day and how the death came */
const unsigned char kFallenVersionNoDay = 3;     /* the form before the day (rows a world server kept then): read, day -1, how unknown */
const unsigned char kFallenVersionNoPlace = 2;   /* the form before the place: read, place "", day -1, how unknown */
const size_t kNameMax = 64;              /* namewire.h: a name is at most 64 bytes */
const size_t kTextMax = 255;             /* template and race names: one length byte */
const size_t kLookMax = 64u << 10;       /* an appearance record larger than 64 KiB is not kept (the ones read are ~1.7 KB); the bound keeps
                                            the largest TABLE under ENet's packet limit (kTableWorstBytes) */

/* How the engine's declareDead was reached (the caller's return address, medical.cpp DeathCallerCause). */
const int kCauseOther    = 0;   /* any other caller (a lever, a script, an unlisted engine path) */
const int kCauseMedical  = 1;   /* MedicalSystem's own update: wounds, blood loss, starvation */
const int kCauseInvCheck = 2;   /* Inventory::deathCheck */
const int kCauseUnequip  = 3;   /* Character::unequipItem */

inline const char* CauseWord(int cause)
{
    switch (cause)
    {
    case kCauseMedical:  return "medical";
    case kCauseInvCheck: return "inventory-death-check";
    case kCauseUnequip:  return "unequip";
    default:             return "other";
    }
}

/* How the death came, for the FALLEN tab's CAUSE column (read at the snapshot from the character's MedicalSystem). The engine's
   own medical death comes when hunger falls below 0, blood falls below its negative maximum, or a vital part fails (medical.cpp,
   0x7A5660's callers): below-zero hunger reads as starvation, below-zero blood as blood loss, and every other death - a failed
   part, a death outside the medical update, or a medical state that did not read - as injuries. The engine's one test
   (medicalUpdate 0x651570, build/decomp_651570.txt:136-138) asks blood below its negative maximum first, then hunger below 0;
   either kills. Hunger is checked first here because `blood` comes without its maximum: blood below 0 but above the maximum's
   negative does not kill, so a starving character who was bleeding would otherwise read as blood loss. */
const int kWhyUnknown    = 0;   /* a row kept before the snapshot carried it */
const int kWhyBloodLoss  = 1;
const int kWhyStarvation = 2;
const int kWhyInjuries   = 3;
inline int DeathWhy(int cause, int medRead, float blood, float hunger)
{
    if (cause != kCauseMedical || !medRead) return kWhyInjuries;
    if (hunger < 0.0f) return kWhyStarvation;
    if (blood < 0.0f) return kWhyBloodLoss;
    return kWhyInjuries;
}

struct Fallen
{
    unsigned int uid;
    std::string templateName, name, race, sid;
    int unique, animal;
    float age;
    int statsHave;
    unsigned int stats[kStatsCount];
    std::vector<char> look;
    float x, y, z;
    unsigned long long diedUnix;
    int cause;
    unsigned int handIndex, handSerial;
    std::string place;   /* the nearest town's record name at the death ("" none read) */
    int day;             /* the in-game day of the death, as the game's clock counts it (-1 not read) */
    int why;             /* how the death came (kWhy*) */
    Fallen() : uid(0), unique(0), animal(0), age(0.0f), statsHave(0), x(0.0f), y(0.0f), z(0.0f), diedUnix(0), cause(0),
               handIndex(0), handSerial(0), day(-1), why(kWhyUnknown) { std::memset(stats, 0, sizeof(stats)); }
};

/* ---- the byte form ---- */
inline void PutU8(std::vector<char>* b, unsigned int v) { b->push_back((char)(v & 0xFF)); }
inline void PutU32(std::vector<char>* b, unsigned int v) { for (int i = 0; i < 4; ++i) b->push_back((char)((v >> (8 * i)) & 0xFF)); }
inline void PutU64(std::vector<char>* b, unsigned long long v) { for (int i = 0; i < 8; ++i) b->push_back((char)((v >> (8 * i)) & 0xFF)); }
inline void PutF32(std::vector<char>* b, float f) { unsigned int u; std::memcpy(&u, &f, 4); PutU32(b, u); }
inline void PutText8(std::vector<char>* b, const std::string& s, size_t cap)
{
    const size_t n = s.size() < cap ? s.size() : cap;
    PutU8(b, (unsigned int)n);
    b->insert(b->end(), s.begin(), s.begin() + n);
}

struct Reader
{
    const char* p; size_t n; size_t at; bool bad;
    Reader(const char* d, size_t len) : p(d), n(len), at(0), bad(false) {}
    bool Need(size_t k) { if (bad || at > n || n - at < k) { bad = true; return false; } return true; }
    unsigned int U8() { if (!Need(1)) return 0; return (unsigned char)p[at++]; }
    unsigned int U32() { if (!Need(4)) return 0; unsigned int v = 0; for (int i = 0; i < 4; ++i) v |= (unsigned int)(unsigned char)p[at + i] << (8 * i); at += 4; return v; }
    unsigned long long U64() { if (!Need(8)) return 0; unsigned long long v = 0; for (int i = 0; i < 8; ++i) v |= (unsigned long long)(unsigned char)p[at + i] << (8 * i); at += 8; return v; }
    float F32() { const unsigned int u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    std::string Text8() { const size_t k = U8(); if (!Need(k)) return std::string(); std::string s(p + at, k); at += k; return s; }
};

/* The byte form of one snapshot. Names longer than their caps are cut (the plugin cuts the name before it gets here). */
inline void EncodeFallen(const Fallen& f, std::vector<char>* out)
{
    out->clear();
    PutU8(out, kFallenVersion);
    PutU32(out, f.uid);
    PutText8(out, f.templateName, kTextMax);
    PutText8(out, f.name, kNameMax);
    PutText8(out, f.race, kTextMax);
    PutText8(out, f.sid, kTextMax);
    PutU8(out, (unsigned int)((f.unique ? 1 : 0) | (f.animal ? 2 : 0) | (f.statsHave ? 4 : 0)));
    PutF32(out, f.age);
    for (int i = 0; i < kStatsCount; ++i) PutU32(out, f.statsHave ? f.stats[i] : 0u);
    PutF32(out, f.x); PutF32(out, f.y); PutF32(out, f.z);
    PutU64(out, f.diedUnix);
    PutU8(out, (unsigned int)(f.cause & 0xFF));
    PutU32(out, f.handIndex); PutU32(out, f.handSerial);
    PutText8(out, f.place, kTextMax);
    PutU32(out, (unsigned int)f.day);
    PutU8(out, (unsigned int)(f.why & 0xFF));
    const size_t lk = f.look.size() <= kLookMax ? f.look.size() : 0;   /* a record over kLookMax is not kept at all */
    PutU32(out, (unsigned int)lk);
    out->insert(out->end(), f.look.begin(), f.look.begin() + lk);
}

/* false = a version other than kFallenVersion, kFallenVersionNoDay or kFallenVersionNoPlace, a truncated block, trailing bytes,
   a template name that is empty, or a record over kLookMax. An earlier form reads with what it did not carry unset: place "",
   day -1, how kWhyUnknown. */
inline bool DecodeFallen(const char* p, size_t n, Fallen* f)
{
    Reader r(p, n);
    const unsigned int ver = r.U8();
    if (r.bad || (ver != kFallenVersion && ver != kFallenVersionNoDay && ver != kFallenVersionNoPlace)) return false;
    Fallen o;
    o.uid = r.U32();
    o.templateName = r.Text8();
    o.name = r.Text8();
    o.race = r.Text8();
    o.sid = r.Text8();
    const unsigned int flags = r.U8();
    o.unique = (flags & 1) ? 1 : 0; o.animal = (flags & 2) ? 1 : 0; o.statsHave = (flags & 4) ? 1 : 0;
    o.age = r.F32();
    for (int i = 0; i < kStatsCount; ++i) o.stats[i] = r.U32();
    o.x = r.F32(); o.y = r.F32(); o.z = r.F32();
    o.diedUnix = r.U64();
    o.cause = (int)r.U8();
    o.handIndex = r.U32(); o.handSerial = r.U32();
    if (ver != kFallenVersionNoPlace) o.place = r.Text8();
    if (ver == kFallenVersion) { o.day = (int)r.U32(); o.why = (int)r.U8(); }
    const unsigned int lk = r.U32();
    if (r.bad || lk > kLookMax || !r.Need(lk)) return false;
    o.look.assign(p + r.at, p + r.at + lk); r.at += lk;
    if (r.at != n || o.templateName.empty()) return false;
    *f = o;
    return true;
}

/* ---- the list ---- */
/* The new snapshot goes first; an older entry for the same uid is removed; the list is cut to kFallenKeep. The number of
   entries cut from the tail is returned (the replaced same-uid entry is not counted). */
inline int FallenPush(std::vector<Fallen>* list, const Fallen& f)
{
    for (size_t i = 0; i < list->size(); ++i)
        if ((*list)[i].uid == f.uid) { list->erase(list->begin() + (long)i); break; }
    list->insert(list->begin(), f);
    int cut = 0;
    while ((int)list->size() > kFallenKeep) { list->pop_back(); ++cut; }
    return cut;
}

/* Entry n leaves the list into *out. false = n is not on the list. */
inline bool FallenTake(std::vector<Fallen>* list, int n, Fallen* out)
{
    if (n < 0 || n >= (int)list->size()) return false;
    *out = (*list)[(size_t)n];
    list->erase(list->begin() + n);
    return true;
}

/* ---- one snapshot per uid per world ----
   A uid is snapshotted at its first death call only: a later declareDead on the same body (or on a uid already brought back)
   adds nothing. `seen` holds every uid snapshotted or refused in this world; true = first time (now recorded). */
inline bool FallenNoteOnce(std::set<unsigned int>* seen, unsigned int uid)
{
    return uid != 0 && seen->insert(uid).second;
}

/* ---- the squad the bring-back joins ----
   One row per character this game drives in its own player faction, in the order the game lists them: its squad (an opaque
   key, 0 = none) and whether it is alive. The squads are numbered 0, 1, 2 ... in the order their first LIVING member appears;
   a squad with no living member has no number. `want` = -1 asks for squad 0. The answer is the row of the first living member
   of the chosen squad (the character the new one is placed beside), or -1 when that squad does not exist. *squadCount gets how
   many numbered squads there are. */
struct SquadRow { unsigned long long squad; int alive; };
inline int PickSquadAnchor(const std::vector<SquadRow>& rows, int want, int* squadCount)
{
    std::vector<unsigned long long> seen;
    int answer = -1;
    const int target = want < 0 ? 0 : want;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (!rows[i].alive || rows[i].squad == 0) continue;
        bool known = false;
        for (size_t k = 0; k < seen.size(); ++k) if (seen[k] == rows[i].squad) { known = true; break; }
        if (known) continue;
        if ((int)seen.size() == target) answer = (int)i;
        seen.push_back(rows[i].squad);
    }
    if (squadCount) *squadCount = (int)seen.size();
    return answer;
}

/* The bring-back's place: kBesideUnits (world units, 10 a metre) along +x from the character it joins. */
const float kBesideUnits = 15.0f;

/* ---- the road: create alone, or create and then the engine's own recruit ----
   The two player-member fields recruit 0x7FED10 writes (build/answers-recruit1.md:70-73): movement(+0x640)+0x20 and
   (+0x1A0)+0xDC, read on the new character and on the living member it joins. Each read is -1 when unreadable.
   Recruit is called when a field the member has differs from the new character's, or when the member's read fails
   (then nothing says create alone is enough). An animal is never recruited: recruit is the engine's road for a hired
   PERSON (it resets a dialogue package and may open the character editor), so an animal takes create alone.
   1 = create + recruit, 0 = create alone. */
const int kRoadCreate = 0;
const int kRoadCreateRecruit = 1;
inline int RoadDecide(int animal, int newMove, int newAi, int refMove, int refAi)
{
    if (animal) return kRoadCreate;
    if (refMove < 0 || refAi < 0) return kRoadCreateRecruit;
    if (newMove != refMove || newAi != refAi) return kRoadCreateRecruit;
    return kRoadCreate;
}
inline const char* RoadWord(int road) { return road == kRoadCreateRecruit ? "create+recruit" : "create"; }

/* ---- where a bring-back may stand ----
   The new character is made beside a living character of this player's own that is FREE, with no enemy near it. The squadmate
   is the one the player names (the lever's `beside <uid>`) or the first living member of the chosen squad; either way these
   rules decide, and a refusal carries one reason - the first that holds, in this order:
     kBesideNoSuch       no character with that uid on this game
     kBesideNotMine      a character another game drives (another player's, or a copy of one)
     kBesideNotOwn       a character of another faction
     kBesideDead         dead
     kBesideUnreadable   a state read below failed (the rule fails closed)
     kBesideKnockedOut   unconscious (the engine's RootObjectBase::isUnconscious)
     kBesideCarried      being carried (Character _isBeingCarried, which getPickedUp sets)
     kBesideCaged        in a cage (Character inSomething 2; 1 is a bed, which is allowed)
     kBesideChained      in chains (Character's chained byte, setChainedMode's first write)
     kBesideEnslaved     a slave (StateBroadcastData slave state 1 IS_SLAVE or 2 ESCAPING_SLAVE; 3 EX_SLAVE is free)
     kBesideEnemyNear    a character able to fight (AbleToFight) within kEnemyNearUnits on the ground that the squadmate's own
                         Character::isEnemyOf calls an enemy (relation -30 or below)
   MateState: found / mine / ownFaction 1 or 0; dead, unconscious, carried, chained 1 yes, 0 no, -1 unreadable; inSomething and
   slaveState the engine's values, -1 unreadable (a character with no slave-state record reads slave state 0, free; a value
   outside 0..coopslave::kSlaveStateMax is unreadable). */
struct MateState
{
    int found, mine, ownFaction, dead, unconscious, carried, inSomething, chained, slaveState;
    MateState() : found(0), mine(0), ownFaction(0), dead(0), unconscious(0), carried(0), inSomething(0), chained(0), slaveState(0) {}
};
const int kBesideOk = 0;
const int kBesideNoSuch = 1;
const int kBesideNotMine = 2;
const int kBesideNotOwn = 3;
const int kBesideDead = 4;
const int kBesideUnreadable = 5;
const int kBesideKnockedOut = 6;
const int kBesideCarried = 7;
const int kBesideCaged = 8;
const int kBesideChained = 9;
const int kBesideEnslaved = 10;
const int kBesideEnemyNear = 11;
const int kBesideCodes = 12;
const float kEnemyNearUnits = 500.0f;   /* 50 m (10 units a metre): a Guess, to tune */

/* The squadmate's own verdict (everything but the enemy rule). */
inline int BesideVerdict(const MateState& m)
{
    if (!m.found) return kBesideNoSuch;
    if (m.mine != 1) return kBesideNotMine;
    if (m.ownFaction != 1) return kBesideNotOwn;
    if (m.dead == 1) return kBesideDead;
    if (m.dead < 0 || m.unconscious < 0 || m.carried < 0 || m.inSomething < 0 || m.chained < 0 || m.slaveState < 0
        || m.slaveState > coopslave::kSlaveStateMax) return kBesideUnreadable;
    if (m.unconscious) return kBesideKnockedOut;
    if (m.carried) return kBesideCarried;
    if (m.inSomething == 2) return kBesideCaged;
    if (m.chained) return kBesideChained;
    if (m.slaveState == 1 || m.slaveState == 2) return kBesideEnslaved;
    return kBesideOk;
}

/* An enemy counts only while it can fight: not dead, knocked out, carried, caged, chained or enslaved (the squadmate's reads, on
   the other character). A state that does not read leaves it able to fight (the rule errs toward refusing). 1 able, 0 not. */
inline int AbleToFight(const MateState& o)
{
    if (o.dead == 1 || o.unconscious == 1 || o.carried == 1 || o.inSomething == 2 || o.chained == 1) return 0;
    if (o.slaveState == 1 || o.slaveState == 2) return 0;
    return 1;
}
/* Why a character cannot fight: the first that holds of dead, ko, carried, caged, chained, enslaved; "" when it can. */
inline const char* UnableWhy(const MateState& o)
{
    if (o.dead == 1) return "dead";
    if (o.unconscious == 1) return "ko";
    if (o.carried == 1) return "carried";
    if (o.inSomething == 2) return "caged";
    if (o.chained == 1) return "chained";
    if (o.slaveState == 1 || o.slaveState == 2) return "enslaved";
    return "";
}

/* The enemy rule. One row per other character: its distance on the ground from the squadmate, whether the squadmate calls it
   an enemy (1 yes, 0 no, -1 unreadable) and whether it can fight (AbleToFight). Rows within radius are counted: enemies able to
   fight; enemies that cannot (logged, never a refusal); unreadable ones (logged, never a refusal). nearest = the nearest counted
   enemy's distance, -1 none; nearestRow its row, -1 none. */
struct NearRow { float dist; int enemy; int able; NearRow() : dist(-1.0f), enemy(0), able(1) {} };
struct EnemyCount { int within, unknown, unable, nearestRow; float nearest; EnemyCount() : within(0), unknown(0), unable(0), nearestRow(-1), nearest(-1.0f) {} };
inline EnemyCount CountEnemiesNear(const std::vector<NearRow>& rows, float radius)
{
    EnemyCount e;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const float d = rows[i].dist;
        if (!(d >= 0.0f) || d > radius) continue;   /* a NaN or negative distance is not near */
        if (rows[i].enemy < 0) { ++e.unknown; continue; }
        if (rows[i].enemy == 0) continue;
        if (rows[i].able == 0) { ++e.unable; continue; }
        ++e.within;
        if (e.nearestRow < 0 || d < e.nearest) { e.nearest = d; e.nearestRow = (int)i; }
    }
    return e;
}
/* Whether a near character is an enemy. charEnemy = the squadmate's own Character::isEnemyOf (1 / 0, -1 a fault). playerHostile
   = for another player's character only, the two players' standing as the name tag judges it (the worse of the two directions,
   relation -30 or below): 1 hostile, 0 not, -1 not another player's character or unread. Either saying enemy makes it one:
   isEnemyOf has exits before the relation test (squad, memory tags, "don't aggravate", temporary neutral) and answered "no" for
   another player's character standing 1 m away at -100 (T977, H070). 1 enemy, 0 not, -1 unread (isEnemyOf faulted and the
   standing does not say hostile). */
inline int EnemyVerdict(int charEnemy, int playerHostile)
{
    if (charEnemy == 1 || playerHostile == 1) return 1;
    return charEnemy < 0 ? -1 : 0;
}
/* Why a near row within the radius was NOT counted as an enemy: "not an enemy", "enemy state unreadable", "cannot fight"; ""
   when it was counted or is outside the radius. */
inline const char* NotCountedWhy(const NearRow& r, float radius)
{
    if (!(r.dist >= 0.0f) || r.dist > radius) return "";
    if (r.enemy < 0) return "enemy state unreadable";
    if (r.enemy == 0) return "not an enemy";
    if (r.able == 0) return "cannot fight";
    return "";
}

/* The whole verdict: the squadmate's own, then the enemy rule. */
inline int PlaceVerdict(const MateState& m, const EnemyCount& e)
{
    const int v = BesideVerdict(m);
    if (v != kBesideOk) return v;
    return e.within > 0 ? kBesideEnemyNear : kBesideOk;
}
/* A refusal's reason in words (log lines and the TEST lever's answer). */
inline const char* BesideWord(int code)
{
    switch (code)
    {
    case kBesideOk:         return "allowed";
    case kBesideNoSuch:     return "no character with that uid on this game";
    case kBesideNotMine:    return "not this player's character (another game drives it)";
    case kBesideNotOwn:     return "not of this player's faction";
    case kBesideDead:       return "the squadmate is dead";
    case kBesideUnreadable: return "the squadmate's state does not read";
    case kBesideKnockedOut: return "the squadmate is knocked out";
    case kBesideCarried:    return "the squadmate is being carried";
    case kBesideCaged:      return "the squadmate is in a cage";
    case kBesideChained:    return "the squadmate is in chains";
    case kBesideEnslaved:   return "the squadmate is enslaved";
    case kBesideEnemyNear:  return "an enemy is within 50 m of the squadmate";
    default:                return "?";
    }
}
/* The short tag of each code, in code order (the REPORT line's besideRefused[...] list). */
inline const char* BesideTag(int code)
{
    static const char* const t[kBesideCodes] = { "ok", "noSuch", "notMine", "notOwn", "dead", "unreadable", "ko", "carried", "caged",
                                                 "chained", "enslaved", "enemyNear" };
    return (code >= 0 && code < kBesideCodes) ? t[code] : "?";
}

/* ---- the place of a death: the nearest town ----
   The engine's TownList (every town; ruins and labs are towns too) as rows of (record name, distance on the ground from the
   death). The answer is the row of the nearest town with a name within kPlaceMaxUnits, -1 when there is none (the place is
   then empty; the words for a death far from any town wait for the owner). PlaceWords is how a log line and the TEST lever
   say it ("near <name>", the proposed words; never on a player screen yet). */
const float kPlaceMaxUnits = 30000.0f;   /* 3 km (10 units a metre): farther than this from every town, a death has no place */
struct TownDist { std::string name; float dist; };
inline int NearestTown(const std::vector<TownDist>& rows)
{
    int best = -1;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].name.empty() || !(rows[i].dist >= 0.0f) || rows[i].dist > kPlaceMaxUnits) continue;
        if (best < 0 || rows[i].dist < rows[(size_t)best].dist) best = (int)i;
    }
    return best;
}
inline std::string PlaceWords(const std::string& town) { return town.empty() ? std::string("unknown") : "near " + town; }

/* ======================== THE LIST ON THE WORLD SERVER (per profile) ========================
   The world server keeps each profile's fallen list (newest first, at most kFallenKeep, one row per character) in fallen.txt in
   the world's folder and is the only writer; a game's list is only its copy of the server's table. A game with no world server
   keeps nothing.

   THE LIST FOLLOWS THE PLAYER'S SAVE. A row is keyed by the dead character's uid and identified in a loaded world by its engine
   handle's serial (handSerial - the one character identity a save keeps, ownrec.h HandId). A bring-back is keyed by the same uid
   and identified by the NEW character's handle serial (newSerial):
   - TAKE moves the row to the profile's PENDING rows ("taken, not yet saved"). It becomes final when that game's own save
     finishes (SAVED), or when a world load finds the new character alive in it (SAVED as well). A world load that does not have
     the new character (the game quit or crashed without saving) puts the row back on the list (UNDO), and any listed row of the
     new character's own later death (its handle serial = the pending row's new serial) goes with it - one person, one row.
   - After a world load, a listed row whose character is ALIVE in the loaded world (same handle serial, same template or race,
     this player's faction) left the list for good (ALIVE): the save predates the death.
   A named character's BROUGHT BACK mark (below) follows its row: made at the TAKE, taken back at an UNDO.
   BACK ROWS: SAVED keeps the bring-back as a back row (BackRow: the new character's serial, the dead one's serial, name,
   template and race) - the record of which of this player's characters were brought back, for the price (resurrectfee.h).
   The death of a brought-back character (DiesAs) marks its pending row - and its back row - died (diedUid = the death's uid);
   SAVED carries the mark to the back row; an ALIVE of that death clears it (the loaded save predates it). A game counts A as
   its living characters a back row, pending row or unanswered TAKE names (CountBackAlive). Died rows beyond kBackDiedKeep go,
   oldest first; past kBackMax rows a died row goes first, and a living row only when none is left (BackTrim, logged).

   WIRE: FALLEN (61), both ways, little-endian: {u8 kind, then the kind's fields}.
     up   1 ADD   {u32 len, snapshot bytes (EncodeFallen)}   this game's character died: the row goes first in its list
          2 TAKE  {u32 uid, u32 newSerial, u64 takenUnix}     that row was brought back (when, by this game's clock - the clock
                                                              a snapshot's diedUnix uses): it becomes a PENDING row
          3 ASK   {}                                          this game's copy was cleared (a world torn down) while the link stayed
          4 ALIVE {u32 uid}                                   that row's character is alive in this game's loaded world: dropped
          5 SAVED {u32 n, n x u32 uid}                        those bring-backs are in a finished save (or in the loaded world): final
          6 UNDO  {u32 uid}                                   that bring-back is not in the loaded world: the row goes back on the list
     down 20 TABLE {u32 n, n x {u32 len, snapshot bytes}, u32 m, m x {u32 newSerial, u32 diedUid, u32 len, snapshot bytes},
                    u32 k, k x {u32 len, back row bytes}}
                                                              the profile's list, its pending rows and its back rows, after every up
                                                              message and at admission
   FILE (fallen.txt, rewritten whole through a temp file and a write-through rename, only when its text changes; a change that
   cannot be written is refused - not-saved): each profile's listed rows newest first, then its pending rows, then its back rows
   newest first -
     F <hex profile> <hex snapshot bytes>
     P <hex profile> <newSerial>[/<diedUid>/<takenUnix>] <hex snapshot bytes>   (the two only when either is not 0)
     B <hex profile> <hex back row bytes> */
const unsigned kMsgFallen = 61;
enum { kUpAdd = 1, kUpTake = 2, kUpAsk = 3, kUpAlive = 4, kUpSaved = 5, kUpUndo = 6 };
enum { kDnTable = 20 };
const size_t kSnapMax = kLookMax + 2048;   /* a snapshot's bytes: the look record plus the fixed fields and five texts (at most 5 x 256) */
const unsigned int kRetryMs = 10000;       /* an ADD or TAKE the world server has not acted on is sent again after this */

inline void PutBlob(std::vector<char>* b, const std::vector<char>& v) { PutU32(b, (unsigned int)v.size()); b->insert(b->end(), v.begin(), v.end()); }
inline bool GetBlob(Reader* r, std::vector<char>* out)
{
    const unsigned int n = r->U32();
    if (r->bad || n > kSnapMax || !r->Need(n)) { r->bad = true; return false; }
    out->assign(r->p + r->at, r->p + r->at + n); r->at += n;
    return true;
}

struct Up { int kind; unsigned int uid, newSerial; unsigned long long takenUnix; std::vector<unsigned int> uids; Fallen snap; Up() : kind(0), uid(0), newSerial(0), takenUnix(0) {} };
inline void EncodeAdd(std::vector<char>* b, const Fallen& f) { std::vector<char> s; EncodeFallen(f, &s); b->clear(); PutU8(b, kUpAdd); PutBlob(b, s); }
inline void EncodeTake(std::vector<char>* b, unsigned int uid, unsigned int newSerial, unsigned long long takenUnix = 0) { b->clear(); PutU8(b, kUpTake); PutU32(b, uid); PutU32(b, newSerial); PutU64(b, takenUnix); }
inline void EncodeAsk(std::vector<char>* b) { b->clear(); PutU8(b, kUpAsk); }
inline void EncodeAlive(std::vector<char>* b, unsigned int uid) { b->clear(); PutU8(b, kUpAlive); PutU32(b, uid); }
inline void EncodeUndo(std::vector<char>* b, unsigned int uid) { b->clear(); PutU8(b, kUpUndo); PutU32(b, uid); }
inline void EncodeSaved(std::vector<char>* b, const std::vector<unsigned int>& uids)
{
    b->clear(); PutU8(b, kUpSaved);
    const size_t k = uids.size() < (size_t)kFallenKeep ? uids.size() : (size_t)kFallenKeep;
    PutU32(b, (unsigned int)k);
    for (size_t i = 0; i < k; ++i) PutU32(b, uids[i]);
}
/* false = an unknown kind, a short or long message, an ADD whose snapshot does not decode, or a SAVED of more than kFallenKeep */
inline bool DecodeUp(const char* p, size_t n, Up* u)
{
    Reader r(p, n);
    Up o; o.kind = (int)r.U8();
    if (r.bad) return false;
    if (o.kind == kUpAdd)
    {
        std::vector<char> s;
        if (!GetBlob(&r, &s) || s.empty() || !DecodeFallen(&s[0], s.size(), &o.snap)) return false;
        o.uid = o.snap.uid;
    }
    else if (o.kind == kUpTake) { o.uid = r.U32(); o.newSerial = r.U32(); o.takenUnix = r.U64(); }
    else if (o.kind == kUpAlive || o.kind == kUpUndo) o.uid = r.U32();
    else if (o.kind == kUpSaved)
    {
        const unsigned int k = r.U32();
        if (r.bad || k > (unsigned int)kFallenKeep) return false;
        for (unsigned int i = 0; i < k; ++i) o.uids.push_back(r.U32());
    }
    else if (o.kind != kUpAsk) return false;
    if (r.bad || r.at != n) return false;
    *u = o;
    return true;
}

/* a pending row: taken (brought back), not yet in a finished save. diedUid: 0 = its brought-back character is not known dead;
   else the uid of that character's death row (DiesAs), kept even when that row is later cut from the list or taken.
   takenUnix: when it was brought back (the TAKE's time, the game's clock; 0 = not known). */
struct Pend { Fallen row; unsigned int newSerial, diedUid; unsigned long long takenUnix; Pend() : newSerial(0), diedUid(0), takenUnix(0) {} };
struct PendWire { unsigned int uid, newSerial, diedUid; Fallen row; PendWire() : uid(0), newSerial(0), diedUid(0) {} };
/* Is `death` the death of the character a bring-back made? By its handle serial when the bring-back's new serial was read; when
   it was not (newSerial 0), by the same non-empty name, template and race on a death other than the bring-back's own row
   (ownUid) that happened at or after the bring-back (diedUnix >= takenUnix) - the first such death added after its TAKE. */
inline bool DiesAs(const Fallen& death, unsigned int newSerial, unsigned int ownUid, const std::string& name, const std::string& tmpl, const std::string& race,
                   unsigned long long takenUnix)
{
    if (newSerial != 0) return death.handSerial == newSerial;
    return !name.empty() && death.uid != ownUid && death.diedUnix >= takenUnix && death.name == name && death.templateName == tmpl && death.race == race;
}
/* any death in `deaths` whose handle serial is `serial` (0 never) */
inline bool SerialDied(unsigned int serial, const std::vector<Fallen>& deaths)
{
    if (serial == 0) return false;
    for (size_t i = 0; i < deaths.size(); ++i) if (deaths[i].handSerial == serial) return true;
    return false;
}

/* ---- back rows: the bring-backs a finished save holds ----
   uid         the dead character's uid (its fallen row's key)
   newSerial   the brought-back character's handle serial (0 = unread at the bring-back)
   origSerial  the dead character's handle serial (its row's handSerial)
   diedUid     0 = not known dead; else the uid of the brought-back character's death row (DiesAs)
   takenUnix   when it was brought back (the TAKE's time, the game's clock; 0 = not known)
   name, templateName, race   the row's (the brought-back character is made with them) */
struct BackRow { unsigned int uid, newSerial, origSerial, diedUid; unsigned long long takenUnix; std::string name, templateName, race; BackRow() : uid(0), newSerial(0), origSerial(0), diedUid(0), takenUnix(0) {} };
const unsigned char kBackVersion = 2;   /* 2 carries takenUnix */
const int kBackDiedKeep = kFallenKeep;   /* died back rows a profile keeps (the newest) */
const int kBackMax = 1000;       /* back rows a profile has on the wire and in the file */
inline void EncodeBack(const BackRow& r, std::vector<char>* out)
{
    out->clear();
    PutU8(out, kBackVersion);
    PutU32(out, r.uid); PutU32(out, r.newSerial); PutU32(out, r.origSerial); PutU32(out, r.diedUid); PutU64(out, r.takenUnix);
    PutText8(out, r.name, kNameMax); PutText8(out, r.templateName, kTextMax); PutText8(out, r.race, kTextMax);
}
/* false = another version, a truncated block or trailing bytes */
inline bool DecodeBack(const char* p, size_t n, BackRow* r)
{
    Reader rd(p, n);
    if (rd.U8() != (unsigned int)kBackVersion || rd.bad) return false;
    BackRow o;
    o.uid = rd.U32(); o.newSerial = rd.U32(); o.origSerial = rd.U32(); o.diedUid = rd.U32(); o.takenUnix = rd.U64();
    o.name = rd.Text8(); o.templateName = rd.Text8(); o.race = rd.Text8();
    if (rd.bad || rd.at != n) return false;
    *r = o;
    return true;
}
inline BackRow BackOf(const Pend& p)
{
    BackRow r; r.uid = p.row.uid; r.newSerial = p.newSerial; r.origSerial = p.row.handSerial; r.diedUid = p.diedUid; r.takenUnix = p.takenUnix;
    r.name = p.row.name; r.templateName = p.row.templateName; r.race = p.row.race;
    return r;
}
/* Died rows beyond kBackDiedKeep go, oldest first (the vector is newest first). Then, while more than kBackMax rows remain, the
   oldest died row goes, and only when no died row is left the oldest LIVING row. The number of living rows dropped is returned
   (the world server logs it: those characters no longer count toward the price). */
inline int BackTrim(std::vector<BackRow>* v)
{
    int died = 0, livingDropped = 0;
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i].diedUid != 0) ++died;
    for (size_t i = v->size(); i > 0 && died > kBackDiedKeep; --i)
        if ((*v)[i - 1].diedUid != 0) { v->erase(v->begin() + (long)(i - 1)); --died; }
    while ((int)v->size() > kBackMax)
    {
        size_t at = v->size();
        for (size_t i = v->size(); i > 0; --i) if ((*v)[i - 1].diedUid != 0) { at = i - 1; break; }
        if (at == v->size()) { at = v->size() - 1; ++livingDropped; }
        v->erase(v->begin() + (long)at);
    }
    return livingDropped;
}
/* the row goes first; an older row of the same brought-back character (same non-zero new serial) is replaced. Returns BackTrim's
   living rows dropped. */
inline int BackPush(std::vector<BackRow>* v, const BackRow& r)
{
    for (size_t i = 0; i < v->size() && r.newSerial != 0; ++i)
        if ((*v)[i].newSerial == r.newSerial) { v->erase(v->begin() + (long)i); break; }
    v->insert(v->begin(), r);
    return BackTrim(v);
}

/* The largest TABLE: kFallenKeep listed and kFallenKeep pending snapshots at kSnapMax each, and kBackMax back rows at their largest
   (a version byte, four u32, a u64, three texts at their caps). It must stay under ENet's largest packet (enet.h
   ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE, 32 MiB, the limit both the world server's and the game's hosts use): 6,013,373 bytes. */
const size_t kBackRowMaxBytes = 1 + 16 + 8 + (1 + kNameMax) + 2 * (1 + kTextMax);
const size_t kTableWorstBytes = 1 + 4 + (size_t)kFallenKeep * (4 + kSnapMax) + 4 + (size_t)kFallenKeep * (12 + kSnapMax) + 4 + (size_t)kBackMax * (4 + kBackRowMaxBytes);
typedef char TableFitsEnetPacket[(kTableWorstBytes < (size_t)(32u << 20)) ? 1 : -1];
inline void EncodeTable(std::vector<char>* b, const std::vector<Fallen>& rows, const std::vector<Pend>& pend,
                        const std::vector<BackRow>& back = std::vector<BackRow>())
{
    b->clear(); PutU8(b, kDnTable);
    const size_t k = rows.size() < (size_t)kFallenKeep ? rows.size() : (size_t)kFallenKeep;
    PutU32(b, (unsigned int)k);
    for (size_t i = 0; i < k; ++i) { std::vector<char> s; EncodeFallen(rows[i], &s); PutBlob(b, s); }
    const size_t m = pend.size() < (size_t)kFallenKeep ? pend.size() : (size_t)kFallenKeep;
    PutU32(b, (unsigned int)m);
    for (size_t i = 0; i < m; ++i) { std::vector<char> sp; EncodeFallen(pend[i].row, &sp); PutU32(b, pend[i].newSerial); PutU32(b, pend[i].diedUid); PutBlob(b, sp); }
    const size_t k2 = back.size() < (size_t)kBackMax ? back.size() : (size_t)kBackMax;
    PutU32(b, (unsigned int)k2);
    for (size_t i = 0; i < k2; ++i) { std::vector<char> sb; EncodeBack(back[i], &sb); PutBlob(b, sb); }
}
/* false = not a TABLE, more than kFallenKeep rows or pending rows, more than kBackMax back rows, a row that does not decode, or
   trailing bytes. `back` may be 0 (the back rows are read and checked, not kept). */
inline bool DecodeTable(const char* p, size_t n, std::vector<Fallen>* rows, std::vector<PendWire>* pend, std::vector<BackRow>* back = 0)
{
    Reader r(p, n);
    if (r.U8() != (unsigned int)kDnTable || r.bad) return false;
    const unsigned int k = r.U32();
    if (r.bad || k > (unsigned int)kFallenKeep) return false;
    std::vector<Fallen> out;
    for (unsigned int i = 0; i < k; ++i)
    {
        std::vector<char> s; Fallen f;
        if (!GetBlob(&r, &s) || s.empty() || !DecodeFallen(&s[0], s.size(), &f)) return false;
        out.push_back(f);
    }
    const unsigned int m = r.U32();
    if (r.bad || m > (unsigned int)kFallenKeep) return false;
    std::vector<PendWire> pw;
    for (unsigned int i = 0; i < m; ++i)
    {
        PendWire w; w.newSerial = r.U32(); w.diedUid = r.U32();
        std::vector<char> sp;
        if (!GetBlob(&r, &sp) || sp.empty() || !DecodeFallen(&sp[0], sp.size(), &w.row)) return false;
        w.uid = w.row.uid;
        pw.push_back(w);
    }
    const unsigned int k2 = r.U32();
    if (r.bad || k2 > (unsigned int)kBackMax) return false;
    std::vector<BackRow> bk;
    for (unsigned int i = 0; i < k2; ++i)
    {
        std::vector<char> sb; BackRow br;
        if (!GetBlob(&r, &sb) || sb.empty() || !DecodeBack(&sb[0], sb.size(), &br)) return false;
        bk.push_back(br);
    }
    if (r.bad || r.at != n) return false;
    rows->swap(out); pend->swap(pw);
    if (back) back->swap(bk);
    return true;
}

/* ---- the server's book: profile -> list, profile -> pending rows, profile -> back rows ---- */
struct Book { std::map<std::string, std::vector<Fallen> > lists; std::map<std::string, std::vector<Pend> > pending; std::map<std::string, std::vector<BackRow> > back; };
/* the snapshot goes first in the profile's list (FallenPush: an older row of the same uid is replaced, the list is cut to kFallenKeep);
   a pending row of the same uid is replaced by it too (a new death of that uid is the newer truth); a pending row or back row
   whose brought-back character this death is (DiesAs; by name only the first such death) is marked died by it. The number of
   rows cut from the tail is returned. */
inline int BookAdd(Book* b, const std::string& prof, const Fallen& f)
{
    std::map<std::string, std::vector<Pend> >::iterator pi = b->pending.find(prof);
    if (pi != b->pending.end())
    {
        for (size_t i = 0; i < pi->second.size(); ++i) if (pi->second[i].row.uid == f.uid) { pi->second.erase(pi->second.begin() + (long)i); break; }
        if (pi->second.empty()) b->pending.erase(pi);
    }
    pi = b->pending.find(prof);
    if (pi != b->pending.end())
        for (size_t i = 0; i < pi->second.size(); ++i)
        {
            Pend& q = pi->second[i];
            if ((q.newSerial != 0 || q.diedUid == 0) && DiesAs(f, q.newSerial, q.row.uid, q.row.name, q.row.templateName, q.row.race, q.takenUnix)) q.diedUid = f.uid;
        }
    std::map<std::string, std::vector<BackRow> >::iterator bi = b->back.find(prof);
    if (bi != b->back.end())
    {
        for (size_t i = 0; i < bi->second.size(); ++i)
        {
            BackRow& q = bi->second[i];
            if ((q.newSerial != 0 || q.diedUid == 0) && DiesAs(f, q.newSerial, q.uid, q.name, q.templateName, q.race, q.takenUnix)) q.diedUid = f.uid;
        }
        BackTrim(&bi->second);
    }
    return FallenPush(&b->lists[prof], f);
}
inline bool ListTake(std::vector<Fallen>* list, unsigned int uid, Fallen* out)
{
    for (size_t i = 0; i < list->size(); ++i)
    {
        if ((*list)[i].uid != uid) continue;
        *out = (*list)[i];
        list->erase(list->begin() + (long)i);
        return true;
    }
    return false;
}
/* the listed row for `uid` leaves the list into *out (a list left empty goes). false = no such row. */
inline bool BookDrop(Book* b, const std::string& prof, unsigned int uid, Fallen* out)
{
    std::map<std::string, std::vector<Fallen> >::iterator it = b->lists.find(prof);
    if (it == b->lists.end() || !ListTake(&it->second, uid, out)) return false;
    if (it->second.empty()) b->lists.erase(it);
    return true;
}
/* TAKE: the listed row becomes a pending row (brought back at takenUnix). false = no such listed row. */
inline bool BookTake(Book* b, const std::string& prof, unsigned int uid, unsigned int newSerial, Fallen* out, unsigned long long takenUnix = 0)
{
    if (!BookDrop(b, prof, uid, out)) return false;
    Pend p; p.row = *out; p.newSerial = newSerial; p.takenUnix = takenUnix;
    b->pending[prof].push_back(p);
    return true;
}
/* SAVED: a pending row leaves for good. false = no such pending row. */
inline bool BookSaved(Book* b, const std::string& prof, unsigned int uid, Pend* out)
{
    std::map<std::string, std::vector<Pend> >::iterator it = b->pending.find(prof);
    if (it == b->pending.end()) return false;
    for (size_t i = 0; i < it->second.size(); ++i)
    {
        if (it->second[i].row.uid != uid) continue;
        *out = it->second[i];
        it->second.erase(it->second.begin() + (long)i);
        if (it->second.empty()) b->pending.erase(it);
        return true;
    }
    return false;
}
/* UNDO: a pending row goes back on the list in its place by time of death (newest first; the list is cut to kFallenKeep), and every
   listed row of the brought-back character's own death (handle serial = the pending row's new serial: a person the loaded world
   never had) leaves into *gone. false = no such pending row. *cut = rows cut from the tail. */
inline bool BookUndo(Book* b, const std::string& prof, unsigned int uid, Pend* out, int* cut, std::vector<Fallen>* gone = 0)
{
    *cut = 0;
    if (gone) gone->clear();
    if (!BookSaved(b, prof, uid, out)) return false;
    std::vector<Fallen>& list = b->lists[prof];
    for (size_t i = 0; i < list.size() && out->newSerial != 0; )
    {
        if (list[i].handSerial == out->newSerial) { if (gone) gone->push_back(list[i]); list.erase(list.begin() + (long)i); }
        else ++i;
    }
    size_t at = 0;
    while (at < list.size() && list[at].diedUnix >= out->row.diedUnix) ++at;
    list.insert(list.begin() + (long)at, out->row);
    while ((int)list.size() > kFallenKeep) { list.pop_back(); ++*cut; }
    return true;
}
/* SAVED, the world server's road: the pending row leaves (BookSaved) and becomes the profile's newest back row, marked died when
   its brought-back character's death is known anywhere: the pending row's own mark (set by that death's ADD, so it holds even
   after the death row was cut from the list or taken), else - by serial - a listed row, or a pending row (that death was itself
   brought back) with that handle serial. A bring-back whose new serial is 0 is marked only by the first death of the same name,
   template and race added after its TAKE and dated at or after it (the pending row's mark). *livingDropped (optional) = living back rows BackTrim dropped.
   false = no such pending row. */
inline bool BookSavedFinal(Book* b, const std::string& prof, unsigned int uid, Pend* out, int* livingDropped = 0)
{
    if (livingDropped) *livingDropped = 0;
    if (!BookSaved(b, prof, uid, out)) return false;
    BackRow r = BackOf(*out);
    if (r.diedUid == 0 && r.newSerial != 0)
    {
        std::map<std::string, std::vector<Fallen> >::const_iterator li = b->lists.find(prof);
        if (li != b->lists.end())
            for (size_t i = 0; i < li->second.size(); ++i) if (li->second[i].handSerial == r.newSerial) { r.diedUid = li->second[i].uid; break; }
        std::map<std::string, std::vector<Pend> >::const_iterator pi = b->pending.find(prof);
        if (r.diedUid == 0 && pi != b->pending.end())
            for (size_t i = 0; i < pi->second.size(); ++i) if (pi->second[i].row.handSerial == r.newSerial) { r.diedUid = pi->second[i].row.uid; break; }
    }
    const int dropped = BackPush(&b->back[prof], r);
    if (livingDropped) *livingDropped = dropped;
    return true;
}
/* ALIVE: the listed row leaves (BookDrop), and a back row or pending row marked died by that row is alive again (the loaded save
   predates the death). false = no such listed row. */
inline bool BookAlive(Book* b, const std::string& prof, unsigned int uid, Fallen* out)
{
    if (!BookDrop(b, prof, uid, out)) return false;
    std::map<std::string, std::vector<BackRow> >::iterator bi = b->back.find(prof);
    if (bi != b->back.end() && uid != 0)
        for (size_t i = 0; i < bi->second.size(); ++i) if (bi->second[i].diedUid == uid) bi->second[i].diedUid = 0;
    std::map<std::string, std::vector<Pend> >::iterator pi = b->pending.find(prof);
    if (pi != b->pending.end() && uid != 0)
        for (size_t i = 0; i < pi->second.size(); ++i) if (pi->second[i].diedUid == uid) pi->second[i].diedUid = 0;
    return true;
}
inline std::vector<BackRow> BookBack(const Book& b, const std::string& prof)
{
    std::map<std::string, std::vector<BackRow> >::const_iterator it = b.back.find(prof);
    return it == b.back.end() ? std::vector<BackRow>() : it->second;
}
inline size_t BookBackRows(const Book& b)
{
    size_t n = 0;
    for (std::map<std::string, std::vector<BackRow> >::const_iterator it = b.back.begin(); it != b.back.end(); ++it) n += it->second.size();
    return n;
}
inline std::vector<Fallen> BookList(const Book& b, const std::string& prof)
{
    std::map<std::string, std::vector<Fallen> >::const_iterator it = b.lists.find(prof);
    return it == b.lists.end() ? std::vector<Fallen>() : it->second;
}
inline std::vector<Pend> BookPending(const Book& b, const std::string& prof)
{
    std::map<std::string, std::vector<Pend> >::const_iterator it = b.pending.find(prof);
    return it == b.pending.end() ? std::vector<Pend>() : it->second;
}
/* a deleted profile's list, pending rows and back rows go; the number of rows dropped is returned */
inline size_t BookProfileDeleted(Book* b, const std::string& prof)
{
    size_t n = 0;
    std::map<std::string, std::vector<Fallen> >::iterator it = b->lists.find(prof);
    if (it != b->lists.end()) { n += it->second.size(); b->lists.erase(it); }
    std::map<std::string, std::vector<Pend> >::iterator pi = b->pending.find(prof);
    if (pi != b->pending.end()) { n += pi->second.size(); b->pending.erase(pi); }
    std::map<std::string, std::vector<BackRow> >::iterator bi = b->back.find(prof);
    if (bi != b->back.end()) { n += bi->second.size(); b->back.erase(bi); }
    return n;
}
inline size_t BookRows(const Book& b)
{
    size_t n = 0;
    for (std::map<std::string, std::vector<Fallen> >::const_iterator it = b.lists.begin(); it != b.lists.end(); ++it) n += it->second.size();
    return n;
}
inline size_t BookPendingRows(const Book& b)
{
    size_t n = 0;
    for (std::map<std::string, std::vector<Pend> >::const_iterator it = b.pending.begin(); it != b.pending.end(); ++it) n += it->second.size();
    return n;
}

inline std::string Hex(const char* p, size_t n)
{
    static const char d[] = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { const unsigned char c = (unsigned char)p[i]; s += d[c >> 4]; s += d[c & 15]; }
    return s;
}
inline int HexDigit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
inline bool Unhex(const std::string& h, std::vector<char>* out)
{
    if (h.size() % 2 != 0) return false;
    std::vector<char> v; v.reserve(h.size() / 2);
    for (size_t i = 0; i < h.size(); i += 2)
    {
        const int a = HexDigit(h[i]), c = HexDigit(h[i + 1]);
        if (a < 0 || c < 0) return false;
        v.push_back((char)((a << 4) | c));
    }
    out->swap(v);
    return true;
}
inline std::string HexRow(const Fallen& f) { std::vector<char> bytes; EncodeFallen(f, &bytes); return Hex(bytes.empty() ? "" : &bytes[0], bytes.size()); }
inline std::string BookFile(const Book& b)
{
    std::string s;
    for (std::map<std::string, std::vector<Fallen> >::const_iterator it = b.lists.begin(); it != b.lists.end(); ++it)
        for (size_t i = 0; i < it->second.size(); ++i)
            s += "F " + Hex(it->first.data(), it->first.size()) + " " + HexRow(it->second[i]) + "\n";
    for (std::map<std::string, std::vector<Pend> >::const_iterator it = b.pending.begin(); it != b.pending.end(); ++it)
        for (size_t i = 0; i < it->second.size(); ++i)
        {
            std::ostringstream ns; ns << it->second[i].newSerial;
            if (it->second[i].diedUid != 0 || it->second[i].takenUnix != 0) ns << "/" << it->second[i].diedUid << "/" << it->second[i].takenUnix;
            s += "P " + Hex(it->first.data(), it->first.size()) + " " + ns.str() + " " + HexRow(it->second[i].row) + "\n";
        }
    for (std::map<std::string, std::vector<BackRow> >::const_iterator it = b.back.begin(); it != b.back.end(); ++it)
        for (size_t i = 0; i < it->second.size(); ++i)
        {
            std::vector<char> by; EncodeBack(it->second[i], &by);
            s += "B " + Hex(it->first.data(), it->first.size()) + " " + Hex(&by[0], by.size()) + "\n";
        }
    return s;
}
enum { kLineOk = 0, kLineBad = 1, kLineDuplicate = 2, kLineOverKeep = 3, kLineOverBack = 4 };
/* one fallen.txt line onto the book, after the rows already read for that profile (the file holds each list newest first).
   false = refused (*why says which): not an F / P / B line, bad hex or number, a snapshot or back row that does not decode, a
   uid that profile already has (listed or pending), a back row of a new serial that profile already has, a row past the kFallenKeep a
   list (or the pending rows) keeps (kLineOverKeep), or a back row past kBackMax (kLineOverBack). */
inline bool BookParseLine(const std::string& raw, Book* b, int* why)
{
    std::string line = raw;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    *why = kLineBad;
    if (line.size() >= 2 && line[0] == 'B' && line[1] == ' ')
    {
        const size_t sp = line.find(' ', 2);
        if (sp == std::string::npos) return false;
        std::vector<char> prof, bytes; BackRow r;
        if (!Unhex(line.substr(2, sp - 2), &prof) || prof.empty() || !Unhex(line.substr(sp + 1), &bytes) || bytes.empty()
            || !DecodeBack(&bytes[0], bytes.size(), &r)) return false;
        std::vector<BackRow>& v = b->back[std::string(prof.begin(), prof.end())];
        bool ok = true;
        for (size_t i = 0; i < v.size() && r.newSerial != 0; ++i) if (v[i].newSerial == r.newSerial) { *why = kLineDuplicate; ok = false; break; }
        if (ok && (int)v.size() >= kBackMax) { *why = kLineOverBack; ok = false; }
        if (ok) { v.push_back(r); *why = kLineOk; }
        if (v.empty()) b->back.erase(std::string(prof.begin(), prof.end()));
        return ok;
    }
    if (line.size() < 2 || (line[0] != 'F' && line[0] != 'P') || line[1] != ' ') return false;
    const bool pend = line[0] == 'P';
    const size_t sp = line.find(' ', 2);
    if (sp == std::string::npos) return false;
    std::vector<char> prof, bytes; Fallen f;
    unsigned int newSerial = 0, diedUid = 0;
    unsigned long long takenUnix = 0;
    std::string rest = line.substr(sp + 1);
    if (pend)
    {
        const size_t sp2 = rest.find(' ');
        if (sp2 == std::string::npos || sp2 == 0) return false;
        std::vector<std::string> nums;   /* serial, or serial/diedUid/takenUnix - every part digits */
        const std::string field = rest.substr(0, sp2);
        size_t at = 0;
        for (;;) { const size_t sl = field.find('/', at); nums.push_back(field.substr(at, sl == std::string::npos ? std::string::npos : sl - at)); if (sl == std::string::npos) break; at = sl + 1; }
        if (nums.size() != 1 && nums.size() != 3) return false;
        for (size_t k = 0; k < nums.size(); ++k)
        {
            if (nums[k].empty() || nums[k].size() > 19) return false;   /* 19 digits never overflow 64 bits */
            for (size_t i = 0; i < nums[k].size(); ++i) if (nums[k][i] < '0' || nums[k][i] > '9') return false;
        }
        newSerial = (unsigned int)std::strtoul(nums[0].c_str(), 0, 10);
        if (nums.size() == 3) { diedUid = (unsigned int)std::strtoul(nums[1].c_str(), 0, 10); for (size_t i = 0; i < nums[2].size(); ++i) takenUnix = takenUnix * 10 + (unsigned long long)(nums[2][i] - '0'); }
        rest = rest.substr(sp2 + 1);
    }
    if (!Unhex(line.substr(2, sp - 2), &prof) || prof.empty() || !Unhex(rest, &bytes) || bytes.empty()
        || !DecodeFallen(&bytes[0], bytes.size(), &f)) return false;
    const std::string id(prof.begin(), prof.end());
    std::vector<Fallen>& list = b->lists[id];
    std::vector<Pend>& pl = b->pending[id];
    for (size_t i = 0; i < list.size(); ++i) if (list[i].uid == f.uid) { *why = kLineDuplicate; break; }
    for (size_t i = 0; i < pl.size(); ++i) if (pl[i].row.uid == f.uid) { *why = kLineDuplicate; break; }
    bool ok = *why != kLineDuplicate;
    if (ok && (int)(pend ? pl.size() : list.size()) >= kFallenKeep) { *why = kLineOverKeep; ok = false; }
    if (ok)
    {
        if (pend) { Pend p; p.row = f; p.newSerial = newSerial; p.diedUid = diedUid; p.takenUnix = takenUnix; pl.push_back(p); } else list.push_back(f);
        *why = kLineOk;
    }
    if (list.empty()) b->lists.erase(id);
    if (pl.empty()) b->pending.erase(id);
    return ok;
}

/* ---- a game's copy of the table ----
   A row this game has taken (brought back) stays out of its copy until a TABLE no longer lists it, so a TABLE the server sent
   before it handled the TAKE cannot offer the same row twice. A taken row is remembered with the link generation and the time
   its TAKE went out (gen -1 = not sent) and is kept across a world teardown until a TABLE drops it. A row this game reported
   ALIVE in its loaded world is held out the same way (kind kUpAlive). */
struct Taken { unsigned int uid, newSerial, rowSerial; int kind, gen, epoch; unsigned int sentMs; unsigned long long takenUnix; Taken() : uid(0), newSerial(0), rowSerial(0), kind(kUpTake), gen(-1), epoch(0), sentMs(0), takenUnix(0) {} };
inline void TableApply(const std::vector<Fallen>& table, std::vector<Taken>* taken, std::vector<Fallen>* list)
{
    std::vector<Fallen> out;
    std::vector<Taken> keep;
    for (size_t t = 0; t < taken->size(); ++t)
    {
        bool listed = false;
        for (size_t i = 0; i < table.size(); ++i) if (table[i].uid == (*taken)[t].uid) { listed = true; break; }
        if (listed) keep.push_back((*taken)[t]);   /* not listed: the server has acted on it - forgotten */
    }
    for (size_t i = 0; i < table.size(); ++i)
    {
        bool isTaken = false;
        for (size_t t = 0; t < keep.size(); ++t) if (keep[t].uid == table[i].uid) { isTaken = true; break; }
        if (!isTaken) out.push_back(table[i]);
    }
    taken->swap(keep);
    list->swap(out);
}
/* An ADD or TAKE the world server has not acted on is sent again: at once on a link it never went out on (sentGen != gen,
   -1 = never sent), and after kRetryMs on the same link (a refusal - not-saved - or a message the server never handled). */
inline bool RetryDue(int sentGen, unsigned int sentMs, int gen, unsigned int nowMs)
{
    return gen >= 0 && (sentGen != gen || (unsigned int)(nowMs - sentMs) >= kRetryMs);
}
const int kAddTries = 6;   /* an ADD the server never lists after this many sends is dropped, with a log line */

/* ---- after a world load: the list follows the save ----
   live = every LIVING character of this player's faction in the loaded world: its handle serial (0 never matches), name,
   template and race.
   ALIVE (on every list, and once after each load): a listed row whose character is among them - same handle serial and the same
   template or race - is alive in the save (the save predates the death). Never a NAMED character's row: a named character's life
   is the world server's unique state, which every game follows - while the server holds it dead the mod kills it in every world
   (worldstate.cpp), so its row must stay to bring it back; a save in which it is alive does not make it alive.
   PENDING (once per world load): a bring-back is in the loaded world (SAVED) when a living character has its NEW serial, or -
   because a character made during play is not proven to keep its handle serial through a save and a load - the same name,
   template and race while not being the row's own character (its original serial). Otherwise it is UNDOne - except a NAMED
   character's: that one is undone only when the row's own character is alive in the loaded world (the save predates the death,
   so it predates the bring-back too); with no such proof it stays pending (held) for the next load or save. */
struct LiveChar { unsigned int serial; std::string name, templateName, race; LiveChar() : serial(0) {} };
struct Reconciled { std::vector<unsigned int> alive, saved, undo, held; };
inline bool SameKind(const LiveChar& c, const Fallen& f) { return c.templateName == f.templateName || (!f.race.empty() && f.race != "?" && c.race == f.race); }
inline bool OriginalAlive(const Fallen& f, const std::vector<LiveChar>& live)
{
    if (f.handSerial == 0) return false;
    for (size_t k = 0; k < live.size(); ++k) if (live[k].serial == f.handSerial && SameKind(live[k], f)) return true;
    return false;
}
inline bool BroughtBackPresent(const PendWire& p, const std::vector<LiveChar>& live)
{
    for (size_t k = 0; k < live.size(); ++k)
    {
        if (p.newSerial != 0 && live[k].serial == p.newSerial) return true;
        if (live[k].serial != p.row.handSerial && !p.row.name.empty() && live[k].name == p.row.name && live[k].templateName == p.row.templateName
            && live[k].race == p.row.race) return true;
    }
    return false;
}
inline void Reconcile(const std::vector<Fallen>& listed, const std::vector<PendWire>& pend, const std::vector<LiveChar>& live,
                      bool checkPending, Reconciled* out)
{
    *out = Reconciled();
    for (size_t i = 0; i < listed.size(); ++i)
        if (!listed[i].unique && OriginalAlive(listed[i], live)) out->alive.push_back(listed[i].uid);
    if (!checkPending) return;
    for (size_t i = 0; i < pend.size(); ++i)
    {
        if (BroughtBackPresent(pend[i], live)) out->saved.push_back(pend[i].uid);
        else if (!pend[i].row.unique || OriginalAlive(pend[i].row, live)) out->undo.push_back(pend[i].uid);
        else out->held.push_back(pend[i].uid);
    }
}
/* A TAKE or ALIVE still unanswered when its world was torn down is judged again against the newly loaded world before it is sent
   again: a TAKE whose new character is not there is dropped (the pending rule settles that bring-back), an ALIVE whose character
   is not alive there is dropped. true = still true in this world, keep it. */
inline bool TakenStillTrue(const Taken& t, const std::vector<LiveChar>& live)
{
    const unsigned int want = t.kind == kUpAlive ? t.rowSerial : t.newSerial;
    if (want == 0) return false;
    for (size_t k = 0; k < live.size(); ++k) if (live[k].serial == want) return true;
    return false;
}

/* ---- A: how many of this player's brought-back characters are alive now (the price's count, resurrectfee.h) ----
   keys = every bring-back this game knows of: the back rows and pending rows of the last TABLE, and its own TAKEs the server has
   not answered yet (the same bring-back may appear twice - it is one character). live = every LIVING character of this player's
   faction this game drives. A living character counts once, when a key names it:
     by serial - its handle serial is the key's new serial (a non-zero one);
     by name   - only for a key no living character matched by serial, that may match so (byName: a pending row or back row
                 not known dead - a character made during play is not proven to keep its serial through a save and a load):
                 the same non-empty name, template and race, not the dead character's own serial (origSerial), and not a
                 character already counted. Each key matches at most one character this way.
   A dead character is not in `live`, so a brought-back character that died again does not count, and one alive in a loaded
   save that predates its death does. */
struct BackKey { unsigned int newSerial, origSerial; int byName; std::string name, templateName, race; BackKey() : newSerial(0), origSerial(0), byName(0) {} };
struct BackCount { int alive, bySerial, byName; BackCount() : alive(0), bySerial(0), byName(0) {} };
/* `counted` (when given) receives the index in `live` of each character counted, in the order counted. */
inline BackCount CountBackAlive(const std::vector<BackKey>& keys, const std::vector<LiveChar>& live, std::vector<size_t>* counted = 0)
{
    BackCount c;
    if (counted != 0) counted->clear();
    std::vector<char> used(live.size(), 0), hit(keys.size(), 0);
    for (size_t k = 0; k < keys.size(); ++k)
    {
        if (keys[k].newSerial == 0) continue;
        for (size_t i = 0; i < live.size(); ++i)
        {
            if (live[i].serial != keys[k].newSerial) continue;
            hit[k] = 1;
            if (!used[i]) { used[i] = 1; ++c.bySerial; if (counted != 0) counted->push_back(i); }
        }
    }
    for (size_t k = 0; k < keys.size(); ++k)
    {
        const BackKey& key = keys[k];
        if (hit[k] || !key.byName || key.name.empty()) continue;
        for (size_t i = 0; i < live.size(); ++i)
        {
            if (used[i] || live[i].serial == 0 || live[i].serial == key.origSerial) continue;
            if (live[i].name != key.name || live[i].templateName != key.templateName || live[i].race != key.race) continue;
            used[i] = 1; ++c.byName;
            if (counted != 0) counted->push_back(i);
            break;
        }
    }
    c.alive = c.bySerial + c.byName;
    return c;
}

/* ======================== NAMED CHARACTERS BROUGHT BACK (owner 493) ========================
   The world server's uniques.txt keeps one row per named character: its state (0 DEAD, 1 ALIVE, 2 IMPRISONED), whether a player
   was involved, and BACK - how many times it has been brought back. DEAD IS FINAL: no game may lift it. The one exception is a
   TAKE of that character's own fallen row: the row then reads ALIVE (playerInvolved cleared) and BACK grows by one. An UNDO of
   that TAKE (the bring-back never reached a save) makes it DEAD again and BACK shrinks by one. A brought-back character's later
   death is a game's DEAD like any other and is stored again (DEAD -> brought back -> DEAD ...).
   backNow (memory only, never written) counts the bring-backs made since this process started, so a uniques.txt that could not be
   read at start is merged without losing them: the merged row's BACK is the file's plus backNow, never below the file's.
   UNIQUE_STATE (36) from the world server carries {str sid, u32 state, u32 playerInvolved, u32 back}; a game's own UNIQUE_STATE
   carries the first three.
   FILE LINE: `v1<TAB>sid<TAB>state<TAB>playerInvolved` for a row never brought back (the shape every world already has), and
   `v2<TAB>sid<TAB>state<TAB>playerInvolved<TAB>back` once it has been. */
struct UniqueRow { int state; int playerInvolved; unsigned int back; int backNow; UniqueRow() : state(1), playerInvolved(0), back(0), backNow(0) {} };
enum { kUqStored = 0, kUqRefusedDead = 1 };
/* a game's UNIQUE_STATE onto the row (`have` = the row exists): a DEAD row refuses every state but DEAD; anything else is stored
   and BACK is kept */
inline int UniqueOnGameState(bool have, UniqueRow* row, int state, int playerInvolved)
{
    if (have && row->state == 0 && state != 0) return kUqRefusedDead;
    if (!have) *row = UniqueRow();
    row->state = state; row->playerInvolved = playerInvolved ? 1 : 0;
    return kUqStored;
}
/* the TAKE of a named character's fallen row: the row reads ALIVE, playerInvolved cleared, BACK + 1. The state it had is returned
   (-1 = there was no row: the world server never heard of the death). */
inline int UniqueOnBringBack(bool have, UniqueRow* row)
{
    const int was = have ? row->state : -1;
    if (!have) *row = UniqueRow();
    row->state = 1; row->playerInvolved = 0; ++row->back; ++row->backNow;
    return was;
}
/* the UNDO of that TAKE: DEAD again, BACK - 1 (never below 0) */
inline void UniqueOnUndo(UniqueRow* row)
{
    row->state = 0;
    if (row->back > 0) --row->back;
    if (row->backNow > 0) --row->backNow;
}
inline std::string UniqueFileLine(const std::string& sid, const UniqueRow& r)
{
    std::ostringstream o;
    if (r.back == 0) o << "v1\t" << sid << "\t" << r.state << "\t" << r.playerInvolved << "\n";
    else o << "v2\t" << sid << "\t" << r.state << "\t" << r.playerInvolved << "\t" << r.back << "\n";
    return o.str();
}
/* false = not a v1 / v2 line, an empty sid, or a state outside 0..2 */
inline bool UniqueParseFileLine(const std::string& raw, std::string* sid, UniqueRow* row)
{
    std::string line = raw;
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    std::vector<std::string> fld; size_t at = 0;
    while (at <= line.size()) { const size_t t = line.find('\t', at); if (t == std::string::npos) { fld.push_back(line.substr(at)); break; } fld.push_back(line.substr(at, t - at)); at = t + 1; }
    const bool v1 = fld.size() >= 4 && fld[0] == "v1", v2 = fld.size() >= 5 && fld[0] == "v2";
    if ((!v1 && !v2) || fld[1].empty()) return false;
    UniqueRow r;
    r.state = std::atoi(fld[2].c_str()); r.playerInvolved = std::atoi(fld[3].c_str()) != 0 ? 1 : 0;
    r.back = v2 ? (unsigned int)std::strtoul(fld[4].c_str(), 0, 10) : 0u;
    if (r.state < 0 || r.state > 2) return false;
    *sid = fld[1]; *row = r;
    return true;
}
/* uniques.txt read at last after a deferred load: `deferred` is the row as it stands in memory (every change since this process
   started, applied to an empty map). A row with bring-backs this session (backNow > 0) is the newest truth and wins even over a
   DEAD on the file, its BACK = the file's + backNow. Otherwise coopmerge::UniqueMergeDecide's DEAD-is-final rule, and BACK is
   never lowered below the file's. *merged = the row to keep (only meaningful for kMergeTakeDeferred / kMergeInsertDeferred, and
   for kMergeKeepLoaded it is the file's row). */
inline int UniqueMergeRows(bool haveLoaded, const UniqueRow& loaded, const UniqueRow& deferred, UniqueRow* merged)
{
    if (!haveLoaded) { *merged = deferred; merged->backNow = 0; return coopmerge::kMergeInsertDeferred; }
    if (deferred.backNow > 0)
    {
        *merged = deferred; merged->back = loaded.back + (unsigned int)deferred.backNow; merged->backNow = 0;
        return coopmerge::kMergeTakeDeferred;
    }
    const int d = coopmerge::UniqueMergeDecide(1, loaded.state, deferred.state);
    *merged = d == coopmerge::kMergeKeepLoaded ? loaded : deferred;
    merged->back = loaded.back > deferred.back ? loaded.back : deferred.back;
    merged->backNow = 0;
    return d;
}

/* ---- a game's side of the rule ----
   The engine's own setter never lifts a stored DEAD (decomp_34adc0 writes only an entry whose state is not 0), so an ALIVE or
   IMPRISONED from the world server for a character this game's map reads DEAD has no effect - unless the server says the
   character was brought back (back > 0). Then the state is written into the map entry directly, the way the engine's own
   periodic update writes ALIVE into an existing entry (decomp_5ce9e0). Not when the character is this game's own and the body
   it has here is dead: then this game's DEAD is the newer truth (it died again) and is re-asserted as before.
   ownDead: 1 = this game owns the loaded character carrying the record and it is dead; 0 = anything else. */
inline bool BringBackWrite(int localState, int incomingState, unsigned int back, int ownDead)
{
    return localState == 0 && incomingState != 0 && back > 0 && !ownDead;
}
/* A DEAD this game's map reads is published only when no LIVING character of this game carries the record: a DEAD written while
   a living own character carries it (the old body's squad unloading after a bring-back) is not that character's death.
   carrierAlive: 1 = a living character of this game carries the record; 0 = otherwise. */
inline bool DeadMayPublish(int state, int carrierAlive) { return state != 0 || !carrierAlive; }

} /* namespace swfallen */
