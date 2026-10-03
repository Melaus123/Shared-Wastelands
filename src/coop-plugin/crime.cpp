// crime.cpp - crime3: a character's current crime travels from its owner to its copies (see crime.h).
//
// OWNER (CrimeTick, main thread, ~4 Hz): for each owned character announced to the peer, read the crime fields
//   Character +0x148 int committingCrime (0 = none), +0x150 Faction* victim faction, +0x168..+0x178 the victim hand's five id
//   fields, +0x180 float expiry (cloud/ANSWERS.md crime2 1b, Read). The victim becomes a uid by matching those five fields
//   against each replicated character's own hand (+0x60..+0x70; the crimetest lever builds a hand from the same five);
//   the faction becomes a relations sid. coopcrime::CrimeShouldSend decides (a change, or the same crime set again).
// COPY (ApplyRemoteCrime on the session drain -> CrimeApplyDrain at the K2 safe point, worker paused): a crime is written with
//   BountyManager::notifyCrimeWitnessed 0x851F40 (this = copy+0xF0; faction, victim hand, int expiry, crime) - the call T275
//   showed the engine accepts (effective=1 x2); a cleared crime with setCrime 0x851DB0 (crime 0, no faction, the null hand),
//   the engine's own clear (_doActions CLEAR_BOUNTY, crime1 answer). Both are read back: "applied" means the copy's
//   +0x148 now holds the crime sent.
// A witness-side write on a copy (DA_CRIME_ALARM may set TRESPASSING on the offender, crime1 answer) is not sent back: only
// owned characters are sampled. A copy's own countdown may not run (its AI is gated); the owner's clear ends it.
//
// C++03 (VS2010 v100). Every __try lives in a function with no C++ object that has a destructor (C2712).

#include "crime.h"
#include "spawn.h"          /* FindSpawned, MirrorSlot, MirrorCapacity */
#include "speech.h"         /* crimetest bountyset: LeverNearestNamed */
#include "store.h"          /* EngineWritesBlocked */
#include "worldsync.h"      /* AnnouncedToPeerQuiet (H030) */
#include "addresses.h"      /* NotifyCrimeWitnessed / SetCrime come from the address table */
#include "relations.h"      /* RelationsWireSid / RelationsFactionFromWire */
#include "playerfaction.h"  /* IsPlayerFaction / IsPeerFaction */
#include "net/session.h"    /* IsUidMine, OwnedUidsSnapshot, SendCrime, UidOwnedByPeer, SessionLinkGen */
#include "../common/crimewire.h"
#include "../common/bountywire.h"   /* crime5 */
#include "coop_log.h"
#include "hooks.h"   /* pvp1: coop::AddHook (own MinHook) */
#include "game/Character.h"
#include "game/hand.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <string>
#include <vector>

namespace coop {
namespace {

unsigned long long kCrNotifyRva = 0; static AddrReg kCrNotifyRva_reg("NotifyCrimeWitnessed", &kCrNotifyRva);   /* Steam_1.0.65 0x851F40 */
unsigned long long kCrSetRva = 0;    static AddrReg kCrSetRva_reg("SetCrime", &kCrSetRva);                     /* Steam_1.0.65 0x851DB0 */
typedef void (*CrNotifyFn)(void* bountyManager, void* againstFaction, const hand& againstWho, int expirySeconds, int crime);
typedef bool (*CrSetFn)(void* bountyManager, int crime, void* againstFaction, const hand& againstWho);

const size_t kCrBountyMgr   = 0xF0;    /* Character -> its BountyManager (embedded) */
const size_t kCrCrime       = 0x148;   /* int CrimeEnum */
const size_t kCrFaction     = 0x150;   /* Faction* crimeAgainstFaction */
const size_t kCrHandFields  = 0x168;   /* the victim hand's five id fields (the hand object starts at +0x160) */
const size_t kCrExpiry      = 0x180;   /* float, game seconds */
const size_t kCrOwnHandFields = 0x60;  /* a character's own hand +0x58; its five id fields from +0x60 */
const int    kCrimeScanPerTick = 128;  /* owned characters sampled per CrimeTick */
const int    kCrimeMaxPerTick  = 16;   /* MSG_CRIME sent per CrimeTick */
const size_t kCrimeQueueCap    = 256;  /* distinct uids waiting for the safe point */
const int    kCrimeExpiryMax   = 600;  /* game seconds; a larger expiry is clamped */

/* owner, per owned uid: what was last SENT, and what the last SAMPLE saw (review-crime3 2 and 6: the re-set test compares
   with the last sampled expiry; the victim lookup is cached on the last sampled hand whether or not anything was sent) */
struct Last { coopcrime::CrimeState sent; float sampledExpiry; unsigned int hand5[5]; unsigned int victimUid; bool victimIsPlayer; };
std::map<unsigned int, Last> g_last;
long g_lastLinkGen = -1;                           /* review-crime3 4: a new link forgets what was sent (copies are re-made) */
std::vector<unsigned int> g_scan;
size_t g_scanPos = 0;
struct Pending { coopcrime::CrimeState s; unsigned int fromPeer; };
std::map<unsigned int, Pending> g_queue;           /* copy: latest crime per uid, for the safe point */
/* M7b slice 4 fold 1 (F5): a crime for a copy not made here yet - kept (the latest per uid) until the copy exists, a newer state
   for it arrives, or kCrimeHoldMs passes; at most kCrimeHoldCap uids. A load drops them (the owner's SPAWN brings the state again). */
struct Held { Pending p; DWORD ms; };
std::map<unsigned int, Held> g_held;
const size_t kCrimeHoldCap = 128;
const DWORD  kCrimeHoldMs  = 30000;
long long g_heldNew = 0, g_heldApplied = 0, g_heldSuperseded = 0, g_heldExpired = 0, g_heldFull = 0, g_heldDropped = 0;
long long g_withSpawnSent = 0, g_withSpawnFailed = 0;   /* fold 1 (F5): the owner's state after a SPAWN */
long long g_bOutUnsent = 0;                             /* fold 1 (F7), fold 2 (D4): a bounty message dropped for a lasting reason (BountyUnsentDrops) */

long long g_sent = 0, g_sendFailed = 0, g_playerVictimSamples = 0, g_sidTooLong = 0, g_tickBlocked = 0, g_victimUnresolved = 0,
          g_expiredSamples = 0;
long long g_recv = 0, g_malformed = 0, g_droppedBlocked = 0, g_notOwner = 0, g_queueDropped = 0;
long long g_applied = 0, g_clearApplied = 0, g_engineRefused = 0, g_clearRefused = 0, g_noCopy = 0, g_noFaction = 0,
          g_victimNone = 0, g_faulted = 0, g_applyBlocked = 0, g_noAddr = 0, g_clearForced = 0;
long long g_lines = 0;
const long long kCrimeLineCap = 40;

int CrReadOwner(const void* c, int* crime, void** faction, unsigned int* hand5, float* expiry)
{
    __try
    {
        const char* p = (const char*)c;
        *crime = *(const int*)(p + kCrCrime);
        *faction = *(void* const*)(p + kCrFaction);
        for (int i = 0; i < 5; ++i) hand5[i] = ((const unsigned int*)(p + kCrHandFields))[i];
        *expiry = *(const float*)(p + kCrExpiry);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrReadOwnHand(const void* c, unsigned int* hand5)
{
    __try { for (int i = 0; i < 5; ++i) hand5[i] = ((const unsigned int*)((const char*)c + kCrOwnHandFields))[i]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrReadCrime(const void* c, int* crime, float* expiry)
{
    __try { *crime = *(const int*)((const char*)c + kCrCrime); *expiry = *(const float*)((const char*)c + kCrExpiry); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* review-crime3 3: the fallback when setCrime(0) leaves the copy's crime set - plain stores at the safe point, the same
   two fields the engine itself counts down and clears. */
int CrWriteClear(void* c)
{
    __try { *(int*)((char*)c + kCrCrime) = 0; *(float*)((char*)c + kCrExpiry) = 0.0f; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrCallNotify(CrNotifyFn fn, void* c, void* f, const hand* h, int expiry, int crime)
{
    __try { fn((char*)c + kCrBountyMgr, f, *h, expiry, crime); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrCallSet(CrSetFn fn, void* c, const hand* h)
{
    __try { fn((char*)c + kCrBountyMgr, 0, 0, *h); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int CrFactionOf(::Character* c, ::Faction** out)
{
    *out = 0;
    __try { *out = c->getOwnerFactionDirect(); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* review-crime3 8: a raw float from copy memory is printed only when finite and small (by its bits - `v != v` is not safe
   under /fp:fast), so the log line cannot outgrow its buffer. */
float CrClampForLog(float v)
{
    unsigned int b = 0;
    std::memcpy(&b, &v, 4);
    if ((b & 0x7F800000u) == 0x7F800000u) return -1.0f;
    return (v > -99999.0f && v < 99999.0f) ? v : -1.0f;
}

bool SameHand(const unsigned int* a, const unsigned int* b)
{
    for (int i = 0; i < 5; ++i) if (a[i] != b[i]) return false;
    return true;
}
bool NullHand(const unsigned int* a)
{
    for (int i = 0; i < 5; ++i) if (a[i] != 0) return false;
    return true;
}

/* The replicated character whose own hand is `hand5`, 0 if none (a victim that is not replicated, or the null hand);
   *isPlayer says whether that character is of a player faction (mine or the peer's). */
unsigned int VictimUidOf(const unsigned int* hand5, bool* isPlayer)
{
    *isPlayer = false;
    if (NullHand(hand5)) return 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; unsigned int w[5];
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0) continue;
        if (!CrReadOwnHand(c, w)) continue;
        if (!SameHand(w, hand5)) continue;
        ::Faction* f = 0;
        if (CrFactionOf(c, &f) && f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f))) *isPlayer = true;
        return u;
    }
    return 0;
}

std::string S(unsigned long long v) { char b[32]; std::sprintf(b, "%llu", v); return std::string(b); }

void CrLog(const char* what, unsigned int uid, const coopcrime::CrimeState& s, const char* extra)
{
    if (g_lines >= kCrimeLineCap) return;
    ++g_lines;
    char b[360];
    std::sprintf(b, "[CRIME] %s uid=%u crime=%d expiry=%.1f victim=%u faction='%.96s' %s", what, uid, s.crime, s.expiry, s.victimUid,
                 s.factionSid.c_str(), extra ? extra : "");
    DebugLog(std::string(b));
}

/* T276: every MSG_CRIME for a PLAYER'S character landed on noCopy on both games - neither registry listed the other player's
   character under its owner's uid. This names what the registry does hold for the peer's player faction, so the next run
   shows which uid (if any) stands for it here. Up to 6 uids; `n` counts all. MAIN THREAD. */
std::string PeerFactionCopies()
{
    char b[160]; int n = 0; size_t at = 0;
    b[0] = 0;
    const int cap = MirrorCapacity();
    for (int i = 0; i < cap; ++i)
    {
        unsigned int u = 0; ::Character* c = 0; ::Faction* f = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0) continue;
        if (!CrFactionOf(c, &f) || f == 0 || !IsPeerFaction(f)) continue;
        ++n;
        if (n <= 6 && at < sizeof(b) - 24)
        {
            std::sprintf(b + at, "%s%u%s", at == 0 ? "" : ",", u, net::IsUidMine(u) ? "(mine)" : "");
            at = std::strlen(b);
        }
    }
    char out[200];
    std::sprintf(out, "peerFactionCopies=%d [%s]", n, b);
    return std::string(out);
}

void ForgetUnowned()
{
    std::map<unsigned int, Last>::iterator it = g_last.begin();
    while (it != g_last.end())
    {
        if (!net::IsUidMine(it->first)) g_last.erase(it++);
        else ++it;
    }
}


// ---- crime5: bounties (docs/design-crime.md 3 C; cloud/ANSWERS.md crime4 1-2, Read) ---------------------------------------
// The map is GameHashMap<Faction*, Bounty> at BountyManager +0 (Character+0xF0): bucket count +0x18, size +0x20,
// bucket array +0x38, first node = buckets[count], node->next +0; node +0x10 Faction* (the resolved bounty faction), +0x18 int
// amount, +0x1C uint crimes, +0x20 bool claimed, +0x28 double time. operator[] 0x5E7450 finds or inserts (it may rehash) and
// returns the pair (+0 Faction*, +8 amount, +0xC crimes, +0x10 claimed, +0x18 time); 0x860790 erases. BountyManager::update
// walks the map on the AI worker for every character, so EVERY read and write here runs at the K2 safe point.
unsigned long long kBmIndexRva = 0; static AddrReg kBmIndexRva_reg("BountyMapIndex", &kBmIndexRva);   /* Steam_1.0.65 0x5E7450 */
unsigned long long kBmEraseRva = 0; static AddrReg kBmEraseRva_reg("BountyMapErase", &kBmEraseRva);   /* Steam_1.0.65 0x860790 */
typedef void* (*BmIndexFn)(void* map, void* const* key);
typedef unsigned long long (*BmEraseFn)(void* map, void* const* key);

struct RawBounty { void* faction; int amount; unsigned int crimes; unsigned char claimed; double time; };
const int kRawBountyMax = 64;                  /* entries read (review-crime5 4: perceived-bounty lookups insert empty ones) */
const int kBountySamplePerFrame = 32;          /* replicated characters read per safe point */
const size_t kBountyOutCap = 64;               /* messages waiting for CrimeTick */
const int kBountySendPerTick = 16;
const DWORD kBountyResendMs = 30000;           /* every owned list is re-sent this often: a copy made after the owner's
                                                  last list learns it, and only then forwards additions */

struct OwnerSent { coopbounty::BountyList list; DWORD ms; long gen; };
std::map<unsigned int, OwnerSent> g_bSent;                  /* owner: last list sent per owned uid */
/* copy: the owner's last list applied here, and the copy it was applied to - a re-made copy (template bounties again, 0x62A780)
   starts over and forwards nothing until the owner's next list (review-crime5 1) */
struct Known { coopbounty::BountyList list; const void* obj; };
std::map<unsigned int, Known> g_bKnown;
long long g_bKnownReset = 0, g_bAddsGated = 0, g_bPreApplyAdds = 0;
long long g_bClearsSent = 0, g_bPreApplyClears = 0, g_bClearsApplied = 0, g_bClearsErased = 0, g_bClearsNoop = 0, g_bTestCleared = 0;   /* par20 */
volatile LONG g_bTestClearUid = 0;   /* par20 TEST-ONLY lever `crimetest bountyclear <uid>`: armed on the main thread, drained at the K2 safe point */
bool g_bWasBlocked = false;
struct BountyOut { unsigned int uid; unsigned char kind; coopbounty::BountyList list; };
std::vector<BountyOut> g_bOut;
struct BountyIn { unsigned int uid; unsigned char kind; coopbounty::BountyList list; };
std::vector<BountyIn> g_bIn;
int g_bCursor = 0;
long long g_bListsSent = 0, g_bAddsSent = 0, g_bListsApplied = 0, g_bAddsApplied = 0, g_bErased = 0, g_bReadFault = 0,
          g_bNoFaction = 0, g_bPlayerSkipped = 0, g_bNoCopy = 0, g_bNotOwner = 0, g_bQueueDropped = 0, g_bNoAddr = 0,
          g_bMalformed = 0, g_bWriteFault = 0, g_bLines = 0, g_bDroppedBlocked = 0;
const long long kBountyLineCap = 40;

/* -1: unreadable or implausible (never acted on); else the number of entries read (a map above kRawBountyMax is -1 too). */
int BmRead(const void* c, RawBounty* out)
{
    __try
    {
        const char* bm = (const char*)c + kCrBountyMgr;
        const size_t count = *(const size_t*)(bm + 0x18);
        const size_t size = *(const size_t*)(bm + 0x20);
        if (size == 0) return 0;
        if (size > (size_t)kRawBountyMax || count == 0 || count > ((size_t)1 << 20)) return -1;
        void* const* buckets = *(void* const* const*)(bm + 0x38);
        if (buckets == 0) return -1;
        const char* node = (const char*)buckets[count];
        int n = 0;
        while (node != 0 && (size_t)n < size)
        {
            out[n].faction = *(void* const*)(node + 0x10);
            out[n].amount = *(const int*)(node + 0x18);
            out[n].crimes = *(const unsigned int*)(node + 0x1C);
            out[n].claimed = *(const unsigned char*)(node + 0x20);
            out[n].time = *(const double*)(node + 0x28);
            ++n;
            node = *(const char* const*)node;
        }
        return ((size_t)n == size) ? n : -1;   /* a short walk is a map we do not understand: not acted on */
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* crime10 (F905; crime9 answer 1, Confirmed by disassembly): BountyManager::update erases an entry whose time is AHEAD of this
   game's clock (`clock - time < 0`, 5cb0f0:24). A time from the other game - its clock at assignment - was ahead by
   0.002 h in T284 and the entry vanished 6-20 frames later. So a written time never exceeds `now` (this game's clock at
   clock+0xA0), or, when the clock cannot be read, the time the engine stamped on the entry (operator[]'s value constructor
   stamps a new entry with the clock, 851100; an existing one keeps its own). Returns 0 fault, 1 written, 2 written with the
   time clamped. */
int BmWrite(BmIndexFn idx, void* c, void* faction, int amount, unsigned int crimes, unsigned char claimed, double time, double now)
{
    __try
    {
        void* key = faction;
        char* pr = (char*)idx((char*)c + kCrBountyMgr, &key);
        if (pr == 0 || *(void* const*)pr != faction) return 0;   /* review-crime5 6: the pair must start with our key */
        const double limit = (now >= 0.0) ? now : *(const double*)(pr + 0x18);
        int rc = 1;
        if (time > limit) { time = limit; rc = 2; }
        *(int*)(pr + 0x8) = amount;
        *(unsigned int*)(pr + 0xC) = crimes;
        *(unsigned char*)(pr + 0x10) = claimed ? 1 : 0;
        *(double*)(pr + 0x18) = time;
        return rc;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* BountyManager +0xA4 `_hadABountyAssignedForCurrentCrime` (crime4 answer 1: assignBountyForCrimes runs only while it is 0, then
   sets it). -1 unreadable. */
int BmAssignedFlag(const void* c)
{
    __try { return *(const unsigned char*)((const char*)c + kCrBountyMgr + 0xA4) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int BmSetAssignedFlag(void* c)
{
    __try { *(unsigned char*)((char*)c + kCrBountyMgr + 0xA4) = 1; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int BmErase(BmEraseFn er, void* c, void* faction)
{
    __try { void* key = faction; er((char*)c + kCrBountyMgr, &key); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* The raw entries as a wire list: an EMPTY entry (amount 0, no crimes - getPercievedBounty inserts one per looker's faction,
   review-crime5 4), a faction without a sid, or a PLAYER faction (decision 4, the user's) is left out; at most
   kBountyMaxEntries go on the wire. */
void BmToList(const RawBounty* raw, int n, coopbounty::BountyList* out)
{
    out->clear();
    for (int i = 0; i < n && out->size() < (size_t)coopbounty::kBountyMaxEntries; ++i)
    {
        ::Faction* f = (::Faction*)raw[i].faction;
        if (f == 0) continue;
        if (raw[i].amount == 0 && raw[i].crimes == 0) continue;
        if (IsPlayerFaction(f) || IsPeerFaction(f)) { ++g_bPlayerSkipped; continue; }
        coopbounty::BountyEntry e;
        e.sid = RelationsWireSid(f);
        if (e.sid.empty() || e.sid.size() > (size_t)coopbounty::kBountyMaxSid || e.sid[0] == '@') continue;
        e.amount = raw[i].amount < 0 ? 0 : (raw[i].amount > coopbounty::kBountyMaxAmount ? coopbounty::kBountyMaxAmount : raw[i].amount);
        e.crimes = raw[i].crimes;
        e.claimed = raw[i].claimed ? 1 : 0;
        e.time = coopbounty::BountyTimeOk(raw[i].time) ? raw[i].time : 0.0;
        out->push_back(e);
    }
}

void BountyLog(const std::string& line)
{
    if (g_bLines >= kBountyLineCap) return;
    ++g_bLines;
    DebugLog(line);
}
std::string BountyListText(const coopbounty::BountyList& l)
{
    std::string t;
    for (size_t i = 0; i < l.size() && i < 4; ++i)
    {
        char b[200];
        /* crime6: each entry's assignment time (world hours) - T280 saw an owner's entry vanish 0.1 s after it was added */
        std::sprintf(b, "%s%.60s=%d/0x%X%s@%.3f", i ? "," : "", l[i].sid.c_str(), l[i].amount, l[i].crimes, l[i].claimed ? "c" : "",
                     (l[i].time > -1e7 && l[i].time < 1e7) ? l[i].time : -1.0);
        t += b;
    }
    if (l.size() > 4) t += ",...";
    return t;
}

bool BountyEnqueue(unsigned int uid, unsigned char kind, const coopbounty::BountyList& list)
{
    if (g_bOut.size() >= kBountyOutCap) { ++g_bQueueDropped; return false; }
    BountyOut o; o.uid = uid; o.kind = kind; o.list = list;
    g_bOut.push_back(o);
    return true;
}

/* Forget per-uid state that no longer applies: an owner's record for a uid this game no longer drives, a copy's for one it
   now drives. MAIN THREAD (CrimeTick, when its scan restarts). */
void BountyForget()
{
    std::map<unsigned int, OwnerSent>::iterator s = g_bSent.begin();
    while (s != g_bSent.end()) { if (!net::IsUidMine(s->first)) g_bSent.erase(s++); else ++s; }
    std::map<unsigned int, Known>::iterator k = g_bKnown.begin();
    while (k != g_bKnown.end()) { if (net::IsUidMine(k->first)) g_bKnown.erase(k++); else ++k; }
}

/* MAIN THREAD, K2 safe point: read up to kBountySamplePerFrame replicated characters' maps. An owned, announced character's
   list goes to its copies when it changed, on a new link, or every kBountyResendMs while non-empty (or a player's); a copy's
   additions since the owner's last list go to the owner. */
void BountySample()
{
    const int cap = MirrorCapacity();
    if (cap <= 0) return;
    const long gen = StorePeerEpoch();   /* M11 C2: a player entering the world (or the old link) - every list goes again, as at a new link */
    const DWORD now = ::GetTickCount();
    RawBounty raw[kRawBountyMax];
    for (int k = 0; k < kBountySamplePerFrame; ++k)
    {
        const int i = g_bCursor++ % cap;
        if (g_bCursor >= cap) g_bCursor = 0;
        unsigned int u = 0; ::Character* c = 0;
        if (!MirrorSlot(i, &u, &c) || u == 0 || c == 0) continue;
        const bool mine = net::IsUidMine(u);
        std::map<unsigned int, Known>::iterator kn = g_bKnown.end();
        if (!mine)
        {
            kn = g_bKnown.find(u);
            if (kn == g_bKnown.end()) continue;   /* a copy forwards nothing until its owner's list arrived */
            if (kn->second.obj != (const void*)c) { g_bKnown.erase(kn); ++g_bKnownReset; continue; }   /* re-made: start over */
        }
        if (mine && !AnnouncedToPeerQuiet(u)) { g_bSent.erase(u); continue; }   /* withdrawn: a re-announce sends at once */
        const int n = BmRead(c, raw);
        if (n < 0) { ++g_bReadFault; continue; }
        coopbounty::BountyList cur;
        BmToList(raw, n, &cur);
        if (mine)
        {
            std::map<unsigned int, OwnerSent>::iterator it = g_bSent.find(u);
            /* review-crime5 2: re-sent every kBountyResendMs whatever it holds - an empty list lost to a copy that did not exist
               yet would otherwise leave that copy unable to forward its guards' bounties for good (6 bytes a character) */
            const bool due = it == g_bSent.end() || it->second.gen != gen || !coopbounty::BountyListsEqual(it->second.list, cur)
                             || (DWORD)(now - it->second.ms) >= kBountyResendMs;
            if (!due) continue;
            if (!BountyEnqueue(u, coopbounty::kBountyKindList, cur)) return;
            /* crime6: a PLAYER character's list that changed is logged with this game's clock (T280: 2500 vanished 0.1 s
               after the owner added it, once of twice) */
            ::Faction* pf = 0;
            if (it != g_bSent.end() && !coopbounty::BountyListsEqual(it->second.list, cur) && CrFactionOf(c, &pf) && pf != 0 && IsPlayerFaction(pf))
            {
                int cr = 0; float ce = 0.0f; CrReadCrime(c, &cr, &ce);
                char nb[120];
                std::sprintf(nb, " now=%.3f crime=%d expiry=%.1f", LocalWorldHours(), cr, CrClampForLog(ce));
                BountyLog("[BOUNTY] -> list uid=" + S(u) + " [" + BountyListText(cur) + "] was [" + BountyListText(it->second.list) + "]" + nb);
            }
            OwnerSent st; st.list = cur; st.ms = now; st.gen = gen;
            g_bSent[u] = st;
        }
        else
        {
            coopbounty::BountyList& known = kn->second.list;
            const coopbounty::BountyList add = coopbounty::BountyAdditions(known, cur);
            /* par20 (parity P20): what this game took OFF the copy since the owner's last list - a bounty claimed, paid off,
               pardoned or the character turned in here - goes to the owner too, or its next list would put it back */
            const coopbounty::BountyList clr = coopbounty::BountyRemovals(known, cur);
            if (add.empty() && clr.empty()) continue;
            if (g_bOut.size() + (add.empty() ? 0 : 1) + (clr.empty() ? 0 : 1) > kBountyOutCap) { ++g_bQueueDropped; return; }   /* both or neither: `known` moves once */
            if (!add.empty()) BountyEnqueue(u, coopbounty::kBountyKindAdd, add);
            if (!clr.empty()) BountyEnqueue(u, coopbounty::kBountyKindClear, clr);
            known = cur;   /* forwarded once; the owner's next list is the truth */
            if (!add.empty()) BountyLog("[BOUNTY] copy uid=" + S(u) + " a guard here added " + BountyListText(add) + " - sent to the owner");
            if (!clr.empty()) BountyLog("[BOUNTY] copy uid=" + S(u) + " taken off here " + BountyListText(clr) + " - sent to the owner (par20)");
        }
    }
}

long long g_bTimeClamped = 0;   /* crime10: written times pulled back to this game's clock */

/* MAIN THREAD, K2 safe point: the lists and additions that arrived. */
void BountyApply()
{
    if (g_bIn.empty()) return;
    if (kBmIndexRva == 0 || kBmEraseRva == 0) { g_bNoAddr += (long long)g_bIn.size(); g_bIn.clear(); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const BmIndexFn idx = (BmIndexFn)(base + (uintptr_t)kBmIndexRva);
    const BmEraseFn er = (BmEraseFn)(base + (uintptr_t)kBmEraseRva);
    std::vector<BountyIn> work;
    work.swap(g_bIn);
    const double nowHours = LocalWorldHours();   /* crime10: every written time is clamped to this */
    RawBounty raw[kRawBountyMax];
    for (size_t w = 0; w < work.size(); ++w)
    {
        const BountyIn& in = work[w];
        ::Character* c = FindSpawned(in.uid);
        if (c == 0) { ++g_bNoCopy; continue; }
        const int n = BmRead(c, raw);
        if (n < 0) { ++g_bReadFault; continue; }
        if (in.kind == coopbounty::kBountyKindList)
        {
            if (net::IsUidMine(in.uid)) { ++g_bNotOwner; continue; }   /* ownership moved while queued */
            /* review-crime5 3: a guard here may have added to this copy since its last sample - forward that first, or the
               owner's list below would overwrite it (the guard's "already assigned" flag stops it assigning again) */
            std::map<unsigned int, Known>::iterator kn = g_bKnown.find(in.uid);
            if (kn != g_bKnown.end() && kn->second.obj == (const void*)c)
            {
                coopbounty::BountyList cur;
                BmToList(raw, n, &cur);
                const coopbounty::BountyList add = coopbounty::BountyAdditions(kn->second.list, cur);
                if (!add.empty() && BountyEnqueue(in.uid, coopbounty::kBountyKindAdd, add)) ++g_bPreApplyAdds;
                const coopbounty::BountyList clr = coopbounty::BountyRemovals(kn->second.list, cur);   /* par20: a claim here too */
                if (!clr.empty() && BountyEnqueue(in.uid, coopbounty::kBountyKindClear, clr)) ++g_bPreApplyClears;
            }
            for (size_t e = 0; e < in.list.size(); ++e)
            {
                ::Faction* f = RelationsFactionFromWire(in.list[e].sid);
                if (f == 0) { ++g_bNoFaction; continue; }
                const int wr = BmWrite(idx, c, f, in.list[e].amount, in.list[e].crimes, in.list[e].claimed, in.list[e].time, nowHours);
                if (wr == 0) ++g_bWriteFault; else if (wr == 2) ++g_bTimeClamped;
            }
            /* a faction the owner no longer lists is erased here - never a player faction (not ours to decide) */
            for (int r = 0; r < n; ++r)
            {
                ::Faction* f = (::Faction*)raw[r].faction;
                if (f == 0 || IsPlayerFaction(f) || IsPeerFaction(f)) continue;
                if (raw[r].amount == 0 && raw[r].crimes == 0) continue;   /* an empty lookup entry: the engine drops it itself */
                const std::string sid = RelationsWireSid(f);
                if (sid.empty() || coopbounty::BountyFind(in.list, sid) != 0) continue;
                if (BmErase(er, c, f)) ++g_bErased; else ++g_bWriteFault;
            }
            Known kw; kw.obj = (const void*)c;
            {   /* par20: `known` holds only what really landed on the copy - an entry this game could not write (its faction
                   unresolved here) must never read as "taken off here" and clear the owner's bounty. Unreadable: as before. */
                RawBounty landedRaw[kRawBountyMax];
                const int m = BmRead(c, landedRaw);
                if (m < 0) kw.list = in.list;
                else
                {
                    coopbounty::BountyList landed;
                    BmToList(landedRaw, m, &landed);
                    for (size_t e = 0; e < in.list.size(); ++e)
                        if (coopbounty::BountyFind(landed, in.list[e].sid) != 0) kw.list.push_back(in.list[e]);
                }
            }
            g_bKnown[in.uid] = kw;
            ++g_bListsApplied;
            coopbounty::BountyList had;
            BmToList(raw, n, &had);
            if (!coopbounty::BountyListsEqual(had, in.list))   /* crime6: only a list that changes the copy is logged */
                BountyLog("[BOUNTY] <- list uid=" + S(in.uid) + " [" + BountyListText(in.list) + "] written onto the copy (had " + S(n) + ")");
        }
        else if (in.kind == coopbounty::kBountyKindClear)
        {
            /* par20 (parity P20): the other game took this off its copy (claimed, paid off, pardoned, turned in) - the same
               comes off our own character, so our next list (and every 30-s re-send) carries the cleared state */
            if (!net::IsUidMine(in.uid)) { ++g_bNotOwner; continue; }
            coopbounty::BountyList before;
            BmToList(raw, n, &before);
            int written = 0, erased = 0;
            for (size_t e = 0; e < in.list.size(); ++e)
            {
                ::Faction* f = RelationsFactionFromWire(in.list[e].sid);
                if (f == 0) { ++g_bNoFaction; continue; }
                const coopbounty::BountyEntry* own = coopbounty::BountyFind(before, in.list[e].sid);
                const coopbounty::BountyClearOutcome o = coopbounty::BountyClearApply(own, in.list[e]);
                if (o.action == coopbounty::kBountyClearNone) { ++g_bClearsNoop; continue; }
                if (o.action == coopbounty::kBountyClearErase)
                {
                    if (BmErase(er, c, f)) { ++g_bClearsErased; ++erased; } else ++g_bWriteFault;
                    continue;
                }
                const int wr = BmWrite(idx, c, f, o.entry.amount, o.entry.crimes, o.entry.claimed, o.entry.time, nowHours);
                if (wr != 0) { ++g_bClearsApplied; ++written; } else ++g_bWriteFault;
                if (wr == 2) ++g_bTimeClamped;
            }
            BountyLog("[BOUNTY] <- clear uid=" + S(in.uid) + " [" + BountyListText(in.list) + "] taken off our own character (had ["
                      + BountyListText(before) + "]): " + S(written) + " written, " + S(erased) + " erased - our next list carries it (par20)");
        }
        else
        {
            if (!net::IsUidMine(in.uid)) { ++g_bNotOwner; continue; }
            /* review-crime5 5: one crime earns one assignment, as in single player. The engine's own gate (assignBountyForCrimes
               852ff0:14) is "a crime is set AND +0xA4 is 0"; the same gate here: while this character's crime is set and a
               bounty was already assigned for it, a copy's addition adds only crime bits it lacks, not a second amount. With
               no crime set the gate is open (a later crime is a new one). */
            int curCrime = 0; float curExp = 0.0f;
            const bool crimeSet = CrReadCrime(c, &curCrime, &curExp) && curCrime != 0;
            const bool alreadyAssigned = crimeSet && BmAssignedFlag(c) == 1;
            for (size_t e = 0; e < in.list.size(); ++e)
            {
                const coopbounty::BountyEntry& a = in.list[e];
                ::Faction* f = RelationsFactionFromWire(a.sid);
                if (f == 0) { ++g_bNoFaction; continue; }
                int amount = 0; unsigned int crimes = 0; unsigned char claimed = 0; double time = a.time;
                for (int r = 0; r < n; ++r)
                    if (raw[r].faction == (void*)f)
                    {
                        amount = raw[r].amount; crimes = raw[r].crimes; claimed = raw[r].claimed;
                        if (coopbounty::BountyTimeOk(raw[r].time) && raw[r].time > time) time = raw[r].time;
                    }
                const bool newBits = (a.crimes & ~crimes) != 0;
                long long sum = (long long)amount + ((alreadyAssigned && !newBits) ? 0 : a.amount);
                if (alreadyAssigned && !newBits && a.amount > 0) ++g_bAddsGated;
                if (sum > coopbounty::kBountyMaxAmount) sum = coopbounty::kBountyMaxAmount;
                const int wr = BmWrite(idx, c, f, (int)sum, crimes | a.crimes, claimed, time, nowHours);
                if (wr != 0) ++g_bAddsApplied; else ++g_bWriteFault;
                if (wr == 2) ++g_bTimeClamped;
            }
            if (crimeSet) BmSetAssignedFlag(c);   /* as the engine does: only for a current crime */
            coopbounty::BountyList before;
            BmToList(raw, n, &before);
            char nb[120];
            std::sprintf(nb, " now=%.3f crime=%d", LocalWorldHours(), curCrime);
            BountyLog("[BOUNTY] <- add uid=" + S(in.uid) + " [" + BountyListText(in.list) + "] added to our own character (had ["
                      + BountyListText(before) + "])" + nb
                      + std::string(alreadyAssigned ? " (already assigned for this crime: amounts gated)" : "") + " (the owner's list follows)");
        }
    }
}


// ---- pvp1 (user decision 2026-09-24): no automatic reaction to one player's acts against another player -----------------
// "It should be the onus of the player to notice that the other player is doing these actions and choosing for themself how
// to best react. Being attacked would have the standard automatic response." Every crime the engine records goes through
// BountyManager::setCrime 0x851DB0 (notifyCrimeWitnessed calls it too; crime1/crime9 answers, Read): with no crime set,
// nobody's senses turn the act into a crime event, no bounty is assigned, no alarm is raised (assessCrimes reads
// committingCrime). So setCrime is refused when a PLAYER's character (this game's player faction, or the other player's,
// coop-peer) commits it against a PLAYER's faction. Combat's own response to being hit does not go through here.
// review-pvp1 2: notifyCrimeWitnessed 0x851F40 writes the offender squad's "recognised" timer (+0x110) BEFORE it calls
// setCrime, so it is refused at its own entry too, with the same test on its faction argument.
// review-pvp1 3 (open, not covered): the trespass path (isIntruder_Building 0x851480 -> task handlers -> EV_SHOO / 0x11 ->
// DA_CRIME_ALARM) can raise a town alarm and unfairAddToBounty before any crime is recorded; players are covered today only
// because copies run no AI and player characters do not shoo intruders (Inferred).
// NPC crimes against players, and players' crimes against NPCs, are untouched. ANY THREAD (task handlers call it on the
// AI worker): the test reads only faction pointers and flags.
typedef bool (*SetCrimeHookFn)(void* bountyManager, int crime, void* againstFaction, const void* againstWho);
SetCrimeHookFn orig_setCrime = 0;
volatile LONG64 g_pvpRefused = 0;
volatile LONG g_pvpHook = 0;   /* 0 not installed, 1 installed, -1 failed */
volatile LONG g_pvpNotifyHook = 0;
/* review-pvp1 4: the coop-peer faction a loaded save may carry before this session created or reused one (IsPeerFaction
   knows only the latter). Written on the main thread (CrimeTick), read by the detours on any thread as a plain pointer. */
void* volatile g_crimePeer = 0;
typedef void (*NotifyHookFn)(void* bountyManager, void* againstFaction, const void* againstWho, int expirySeconds, int crime);
NotifyHookFn orig_notify = 0;

int CrOffenderFactionPod(const void* bountyManager, void** faction)
{
    *faction = 0;
    __try
    {
        ::Character* c = *(::Character* const*)((const char*)bountyManager + 0x40);   /* setCrime's `this` holds its character at +0x40 (crime9 answer) */
        if (c == 0) return 0;
        *faction = (void*)c->getOwnerFactionDirect();
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool PlayerSide(void* f) { return f != 0 && (f == g_crimePeer || IsPeerFaction((::Faction*)f) || IsPlayerFaction((::Faction*)f)); }
bool PvpRefuse(void* bountyManager, int crime, void* againstFaction)
{
    if (crime == 0 || againstFaction == 0 || !PlayerSide(againstFaction)) return false;
    void* off = 0;
    return CrOffenderFactionPod(bountyManager, &off) && PlayerSide(off);
}

bool detour_setCrime(void* bountyManager, int crime, void* againstFaction, const void* againstWho)
{
    if (PvpRefuse(bountyManager, crime, againstFaction))
    {
        ::InterlockedIncrement64(&g_pvpRefused);
        return false;   /* player against player: the engine never records it (user decision pvp1) */
    }
    return orig_setCrime(bountyManager, crime, againstFaction, againstWho);
}
void detour_notify(void* bountyManager, void* againstFaction, const void* againstWho, int expirySeconds, int crime)
{
    if (PvpRefuse(bountyManager, crime, againstFaction)) { ::InterlockedIncrement64(&g_pvpRefused); return; }
    orig_notify(bountyManager, againstFaction, againstWho, expirySeconds, crime);
}

// sight1 (docs/design-arrest.md 3 "sight1"; F899; user decision 3 = full parity, recognition on sight included). A guard's
// senses recognise a wanted character only when the TARGET's faction is a player faction: inside 0x856030 the test is
//   0x85635D  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0    (rax = the target's faction, vt+0x58; +0x250 isPlayer)
//   0x856365  74 0E                     je 0x856375                 (not a player: the recognition roll is skipped)
//   0x856367  ...                       the player path (save1 answer 5: Confirmed by FnDis / capstone)
// A copy of another player's character belongs to that player's stand-in faction (coop-p<slot>, or an old save's coop-peer),
// which is not a player faction here, so it is never recognised by sight. review-sight1 1: only the 8-byte cmp is replaced
// (E9 rel32 + 3 NOPs) and the je stays where it is, so a branch elsewhere to 0x856365 still finds it. The stub leaves the flags
// the je reads: ZF=0 for a player faction (the original compare) or for a faction held in a sight cell, ZF=1 otherwise (the
// original compare re-done), then jumps back to the je. It touches no register and never dereferences a cell (RIP-relative
// compares on its own page). T-356: ONE CELL PER OTHER PLAYER'S FACTION (coopcrime::kSightCellCap, above the stand-in table's
// bound), was one cell for the one other game. Written once at start (InstallCrime, the preload stage); the cells are written
// by CrimeTick on the main thread (aligned 8-byte stores, each only when it changes) and read by the senses on the worker.
unsigned long long kCrSightRva = 0; static AddrReg kCrSightRva_reg("BountySightTargetTest", &kCrSightRva);   /* Steam_1.0.65 0x85635D - a patch site, not a function */
const unsigned char kSightOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x0E };
const unsigned int kSightBack  = 0x08;    /* 0x856365 - the je, left in place */
const size_t kSightCellsOff    = 0x400;   /* review-sight1 LOW: the cells are kept off the code's cache line; T-356: past the longer stub */
struct SightCells { volatile LONG64 hits; void* volatile peer[coopcrime::kSightCellCap]; };   /* T-356: hits at +0, cell k at +8+8k */
/* T-356: every stand-in the table can hold, plus an old save's coop-peer, has a cell; the stub ends before the cells; the cells fit the page */
typedef char kSightCellsCoverStandIns[(coopcrime::kSightCellCap >= kStandInTableCap + 1) ? 1 : -1];
typedef char kSightStubBeforeCells[(coopcrime::kSightStubLen <= (int)kSightCellsOff) ? 1 : -1];
typedef char kSightCellsInPage[((int)kSightCellsOff + 8 + 8 * coopcrime::kSightCellCap <= 0x1000) ? 1 : -1];
int g_sightHeld = 0, g_sightDropped = 0;   /* T-356: the cells in use at the last tick; other players' factions that found no cell */
SightCells* g_sightCells = 0;
volatile LONG g_sightState = 0;   /* 0 not tried, 1 patched, -1 refused / failed */
const char* g_sightWhy = "not tried";

// sight1 answers 1 (Inferred, the one open point): a looker of THIS game's player faction never passes the bounty gate because
// its faction has no law key (+0x38 myLawEnforcementFaction == 0 and +0x40 isALawEnforcementFaction == 0), so the patch cannot
// make the local player's own characters react to the other player's bounty. Logged once per world to confirm.
int SightLawKeyPod(const void* faction, void** law, int* isLaw)
{
    __try { *law = *(void* const*)((const char*)faction + 0x38); *isLaw = *((const unsigned char*)faction + 0x40); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool g_sightLawLogged = false;

int SightReadPod(const void* p, unsigned char* out, size_t n)
{
    __try { std::memcpy(out, p, n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// A page for the stub within a rel32 jump of the site (the image's own neighbourhood; searched downward in 64 KB steps).
void* SightAllocNear(uintptr_t site)
{
    const uintptr_t gran = 0x10000;
    const uintptr_t start = site & ~(gran - 1);
    for (uintptr_t i = 1; i < 0x7000; ++i)   /* up to 1.75 GB below */
    {
        if (start <= i * gran) break;
        void* p = ::VirtualAlloc((void*)(start - i * gran), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p != 0) return p;
    }
    return 0;
}

/* T-356: the stub's bytes (one compare per sight cell) are built by the pure coopcrime::SightStubBuild (crimewire.h, the layout
   drawn there), which the offline suite checks byte for byte. */

// 1 patched; 0 refused (the reason in g_sightWhy). MAIN THREAD, at start (the preload stage: no world, no AI worker).
int SightPatch(uintptr_t site)
{
    unsigned char now[10];
    if (!SightReadPod((const void*)site, now, 10)) { g_sightWhy = "the site could not be read"; return 0; }
    if (std::memcmp(now, kSightOrig, 10) != 0) { g_sightWhy = "the 10 bytes at the site are not the expected test (another mod?)"; return 0; }
    unsigned char* stub = (unsigned char*)SightAllocNear(site);
    if (stub == 0) { g_sightWhy = "no page for the stub within a jump of the site"; return 0; }
    const long long rel = (long long)((intptr_t)stub - (intptr_t)(site + 5));
    if (rel > 0x7FFFFFF0LL || rel < -0x7FFFFFF0LL) { ::VirtualFree(stub, 0, MEM_RELEASE); g_sightWhy = "the stub page is out of jump range"; return 0; }
    SightCells* cells = (SightCells*)(stub + kSightCellsOff);
    for (int k = 0; k < coopcrime::kSightCellCap; ++k) cells->peer[k] = 0;   /* T-356 */
    cells->hits = 0;
    if (coopcrime::SightStubBuild(stub, (unsigned long long)site, coopcrime::kSightCellCap, (int)kSightCellsOff) != coopcrime::kSightStubLen)
    { ::VirtualFree(stub, 0, MEM_RELEASE); g_sightWhy = "the stub came out at an unexpected length"; return 0; }
    ::FlushInstructionCache(::GetCurrentProcess(), stub, 0x1000);

    unsigned char patch[8];
    patch[0] = 0xE9;
    const int rel32 = (int)rel;
    std::memcpy(patch + 1, &rel32, 4);
    patch[5] = patch[6] = patch[7] = 0x90;
    DWORD oldProt = 0;
    if (!::VirtualProtect((void*)site, 8, PAGE_EXECUTE_READWRITE, &oldProt))
    { ::VirtualFree(stub, 0, MEM_RELEASE); g_sightWhy = "VirtualProtect refused"; return 0; }
    std::memcpy((void*)site, patch, 8);
    DWORD ignored = 0;
    ::VirtualProtect((void*)site, 8, oldProt, &ignored);
    ::FlushInstructionCache(::GetCurrentProcess(), (void*)site, 8);
    g_sightCells = cells;
    g_sightWhy = "patched";
    return 1;
}

} // namespace

void CrimeTick()
{
    if (EngineWritesBlocked())
    {
        ++g_tickBlocked; g_bOut.clear(); g_crimePeer = 0;   /* the world is being torn down or loaded: its factions are going */
        g_sightLawLogged = false;
        if (g_sightCells != 0)   /* sight1; T-356: every cell */
            for (int k = 0; k < coopcrime::kSightCellCap; ++k) if (g_sightCells->peer[k] != 0) g_sightCells->peer[k] = 0;
        g_sightHeld = 0;
        /* review-crime5 7: a load re-makes every character - both sides start over */
        if (!g_bWasBlocked) { g_bSent.clear(); g_bKnown.clear(); g_bWasBlocked = true; }
        return;
    }
    g_bWasBlocked = false;
    g_crimePeer = (void*)PeerFactionAny();   /* review-pvp1 4: MAIN THREAD, world loaded - a save's coop-peer counts before the first SPAWN */
    if (g_sightCells != 0)
    {   /* T-356: one sight cell per other player's faction - every stand-in in the table and the session peer's (or an old save's
           coop-peer); this game's own slot never. Each cell is written only when it changes. */
        int slots[coopcrime::kSightCellCap + 1];
        ::Faction* fl[coopcrime::kSightCellCap + 1];
        const void* facs[coopcrime::kSightCellCap + 1];
        int n = StandInList(slots, fl, coopcrime::kSightCellCap);
        if (g_crimePeer != 0) { fl[n] = (::Faction*)g_crimePeer; slots[n] = StandInSlotOf(fl[n]); ++n; }
        for (int i = 0; i < n; ++i) facs[i] = fl[i];
        const void* want[coopcrime::kSightCellCap];
        int dropped = 0;
        g_sightHeld = coopcrime::SightCellsFill(slots, facs, n, MySlotForWire(), want, coopcrime::kSightCellCap, &dropped);
        g_sightDropped = dropped;
        for (int k = 0; k < coopcrime::kSightCellCap; ++k)
            if (g_sightCells->peer[k] != want[k]) g_sightCells->peer[k] = (void*)want[k];
    }
    if (!g_sightLawLogged)   /* sight1 answers 1: confirm a player looker has no law key */
    {
        ::Faction* mine = LocalPlayerFaction();
        void* law = 0; int isLaw = -1;
        if (mine != 0 && SightLawKeyPod(mine, &law, &isLaw))
        {
            g_sightLawLogged = true;
            char b[200];
            std::sprintf(b, "[CRIME] sight1 looker check: this game's player faction +0x38 law=%p +0x40 isLaw=%d (%s)", law, isLaw,
                         (law == 0 && isLaw == 0) ? "no law key - its characters never pass the bounty gate" : "HAS A LAW KEY - its characters could react to the other player's bounty");
            DebugLog(std::string(b));
        }
    }   /* sight1: the factions the senses stub treats as a player are the sight cells above (T-356: one per other player) */
    /* crime5: the bounty messages the safe point queued */
    int bsent = 0;
    while (!g_bOut.empty() && bsent < kBountySendPerTick)
    {
        const BountyOut& o = g_bOut.front();
        bool mayDrop = false;
        if (!net::SendBounty(o.uid, o.kind, o.list, &mayDrop))
        {
            /* M7b slice 4 fold 2 (D4, ownerroute.h BountyUnsentDrops): kept, in order, for the next tick - no road at all, no road to
               that owner yet, or a send that failed (an addition / clear is an event, never re-sent). Dropped and counted only with a
               road open and a lasting reason - a LIST (it goes again within kBountyResendMs), or an addition / clear whose owner's
               game is out of the world roster or whose owner's slot is unknown with no session link - and the messages behind it go on */
            if (!mayDrop) break;
            ++g_bOutUnsent;
            g_bOut.erase(g_bOut.begin());
            ++bsent;
            continue;
        }
        if (o.kind == coopbounty::kBountyKindList) ++g_bListsSent; else if (o.kind == coopbounty::kBountyKindClear) ++g_bClearsSent; else ++g_bAddsSent;
        g_bOut.erase(g_bOut.begin());
        ++bsent;
    }
    const long gen = StorePeerEpoch();   /* M11 C2: moves at every arrival and every old-link down (two games: exactly as the link generation) */
    if (gen != g_lastLinkGen) { g_last.clear(); g_bKnown.clear(); if (g_lastLinkGen != -1) StoreArrivalNoteServed(kArrServeCrime); g_lastLinkGen = gen; }   /* review-crime3 4; crime5: copies are re-made */
    if (g_scanPos >= g_scan.size())
    {
        net::OwnedUidsSnapshot(&g_scan);
        g_scanPos = 0;
        ForgetUnowned();
        BountyForget();
        if (g_scan.empty()) return;
    }
    int scanned = 0, sent = 0;
    while (g_scanPos < g_scan.size() && scanned < kCrimeScanPerTick && sent < kCrimeMaxPerTick)
    {
        const unsigned int uid = g_scan[g_scanPos++];
        ++scanned;
        if (!net::IsUidMine(uid) || !AnnouncedToPeerQuiet(uid)) { g_last.erase(uid); continue; }   // H030; a re-announce sends afresh
        ::Character* c = FindSpawned(uid);
        if (c == 0) continue;
        int crime = 0; void* fac = 0; unsigned int hand5[5]; float expiry = 0.0f;
        if (!CrReadOwner(c, &crime, &fac, hand5, &expiry)) continue;
        std::map<unsigned int, Last>::iterator it = g_last.find(uid);
        if (it == g_last.end())
        {
            Last fresh; fresh.sampledExpiry = 0.0f; std::memset(fresh.hand5, 0, sizeof(fresh.hand5)); fresh.victimUid = 0; fresh.victimIsPlayer = false;
            it = g_last.insert(std::make_pair(uid, fresh)).first;
        }
        Last& l = it->second;
        const float exp = coopcrime::CrimeExpiryOk(expiry) ? expiry : 0.0f;
        coopcrime::CrimeState cur;
        /* anything but a known crime reads as none; so does a crime whose expiry has run out - the owner ends the copy's crime
           itself rather than trusting the engine to zero +0x148 (review-crime3 1; T275 saw it do so: crime 0 at expiry -0.1) */
        if (crime > 0 && crime <= coopcrime::kCrimeMaxEnum && exp > 0.0f)
        {
            ::Faction* f = (::Faction*)fac;
            if (!SameHand(l.hand5, hand5))
            {
                std::memcpy(l.hand5, hand5, sizeof(l.hand5));
                l.victimUid = VictimUidOf(hand5, &l.victimIsPlayer);
                if (l.victimUid == 0 && !NullHand(hand5)) ++g_victimUnresolved;
            }
            /* decision 4 (design-crime.md section 4) is the user's: a crime against a PLAYER - by its faction or by the
               character it names - is held back, and one already sent is cleared on the copy (review-crime3 5) */
            if ((f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f))) || l.victimIsPlayer) ++g_playerVictimSamples;
            else
            {
                cur.crime = crime;
                cur.expiry = exp;
                cur.victimUid = l.victimUid;
                cur.factionSid = (f != 0) ? RelationsWireSid(f) : std::string();
                if (cur.factionSid.size() > (size_t)coopcrime::kCrimeMaxSid) { ++g_sidTooLong; cur = coopcrime::CrimeState(); }
            }
        }
        else if (crime != 0 && l.sent.crime != 0) ++g_expiredSamples;   /* set but run out: the clear below ends it on the copy */
        const int due = coopcrime::CrimeShouldSend(l.sent, l.sampledExpiry, cur);
        l.sampledExpiry = cur.expiry;
        if (!due) continue;
        if (!net::SendCrime(uid, cur)) { ++g_sendFailed; --g_scanPos; return; }   // link down: retry this uid next tick
        l.sent = cur;
        ++g_sent; ++sent;
        CrLog("->", uid, cur, "");
    }
}

void CrimeSendWithSpawn(unsigned int uid)
{
    std::map<unsigned int, Last>::const_iterator it = g_last.find(uid);
    if (it == g_last.end() || it->second.sent.crime == 0) return;   /* nothing sent, or a clear: a new copy has no crime */
    coopcrime::CrimeState st = it->second.sent;
    if (it->second.sampledExpiry > 0.0f) st.expiry = it->second.sampledExpiry;   /* what is left of it now, not what was left then */
    if (net::SendCrimeAfterSpawn(uid, st)) ++g_withSpawnSent; else ++g_withSpawnFailed;
}

void ApplyRemoteCrime(unsigned int uid, const coopcrime::CrimeState& s, unsigned int fromPeer)
{
    ++g_recv;
    /* only the uid's owner reports its crime; UidOwnedByPeer does not count a handoff race as split-brain (review-crime3 7) */
    if (!net::UidOwnedByPeer(uid, fromPeer)) { ++g_notOwner; return; }
    std::map<unsigned int, Pending>::iterator it = g_queue.find(uid);
    if (it == g_queue.end() && g_queue.size() >= kCrimeQueueCap) { ++g_queueDropped; return; }
    Pending p; p.s = s; p.fromPeer = fromPeer;
    g_queue[uid] = p;
}

void CrimeNoteMalformed() { ++g_malformed; }
void CrimeNoteDroppedBlocked() { ++g_droppedBlocked; }

void ApplyRemoteBounty(unsigned int uid, unsigned char kind, const coopbounty::BountyList& list, unsigned int fromPeer)
{
    /* a list comes only from the uid's owner; an addition only for a uid THIS game drives */
    if (kind == coopbounty::kBountyKindList ? !net::UidOwnedByPeer(uid, fromPeer) : !net::IsUidMine(uid)) { ++g_bNotOwner; return; }
    if (g_bIn.size() >= kBountyOutCap) { ++g_bQueueDropped; return; }
    BountyIn in; in.uid = uid; in.kind = kind; in.list = list;
    g_bIn.push_back(in);
}
void BountyNoteMalformed() { ++g_bMalformed; }
void BountyNoteDroppedBlocked() { ++g_bDroppedBlocked; }

/* par20 TEST-ONLY lever (class b dev verb, not player behaviour): `crimetest bountyclear <uid>` erases every non-player bounty
   on character <uid> as THIS game sees it - on the non-owner's game, a copy-side clear standing in for a claim / pardon. */
std::string BountyTestClearArm(unsigned int uid)
{
    ::InterlockedExchange(&g_bTestClearUid, (LONG)uid);
    const std::string t = "ok crimetest bountyclear armed uid=" + S(uid)
                          + (net::IsUidMine(uid) ? " (this game OWNS it)" : " (a copy here: the clear must reach its owner)");
    DebugLog("[BOUNTY] " + t);
    return t;
}
/* MAIN THREAD, K2 safe point (CrimeApplyDrain), before BountyApply / BountySample. */
void BountyTestClearDrain()
{
    const unsigned int u = (unsigned int)::InterlockedExchange(&g_bTestClearUid, 0);
    if (u == 0) return;
    if (kBmEraseRva == 0) { DebugLog("[BOUNTY] crimetest bountyclear uid=" + S(u) + " - no erase address, nothing done"); return; }
    ::Character* c = FindSpawned(u);
    if (c == 0) { DebugLog("[BOUNTY] crimetest bountyclear uid=" + S(u) + " - no such character here, nothing done"); return; }
    RawBounty raw[kRawBountyMax];
    const int n = BmRead(c, raw);
    if (n < 0) { ++g_bReadFault; DebugLog("[BOUNTY] crimetest bountyclear uid=" + S(u) + " - map unreadable, nothing done"); return; }
    coopbounty::BountyList had;
    BmToList(raw, n, &had);
    const BmEraseFn er = (BmEraseFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBmEraseRva);
    int done = 0;
    for (int r = 0; r < n; ++r)
    {
        ::Faction* f = (::Faction*)raw[r].faction;
        if (f == 0 || IsPlayerFaction(f) || IsPeerFaction(f)) continue;
        if (raw[r].amount == 0 && raw[r].crimes == 0) continue;
        if (BmErase(er, c, f)) ++done; else ++g_bWriteFault;
    }
    g_bTestCleared += done;
    DebugLog("[BOUNTY] crimetest bountyclear uid=" + S(u) + " on " + std::string(net::IsUidMine(u) ? "the OWNER" : "a copy") + ": erased "
             + S(done) + " of [" + BountyListText(had) + "] - the owner's next lists must stay cleared (par20)");
}

/* TEST-ONLY lever (class b dev verb, not player behaviour; the carried hand-in fixture): `crimetest bountyset <personName>
   <lawNpcName> <amount>` on the person's OWNER. Person = the nearest own, living, non-player character with that name within
   kBountyTestSetNear of this game's first player character; law NPC = the nearest loaded character with that name. The bounty is
   written under the key the law NPC's side reads - GetBountyFaction 0x851140 (the person's BountyManager, the law NPC's faction) -
   with BmWrite, so a carried hand-in to that NPC has a bounty for PAY_BOUNTY to pay. Nothing is sent here: BountySample sends the
   changed list to the copies as it does for a bounty the engine assigned. */
static volatile LONG g_bTestSetPending = 0;   /* armed on the main thread, drained at the K2 safe point */
static std::string g_bTestSetPerson, g_bTestSetLaw;
static int g_bTestSetAmount = 0;
static const float kBountyTestSetNear = 300.0f;

std::string BountyTestSetArm(const std::string& personKey, const std::string& lawKey, int amount)
{
    g_bTestSetPerson = personKey;
    g_bTestSetLaw = lawKey;
    g_bTestSetAmount = amount;
    ::InterlockedExchange(&g_bTestSetPending, 1);
    const std::string t = "ok crimetest bountyset armed person='" + personKey + "' law='" + lawKey + "' amount=" + S(amount)
                          + " - runs at the next safe point; this game must own the person";
    DebugLog("[BOUNTY] " + t);
    return t;
}
/* MAIN THREAD, K2 safe point (CrimeApplyDrain), before BountyApply / BountySample. */
static void BountyTestSetDrain()
{
    if (::InterlockedExchange(&g_bTestSetPending, 0) == 0) return;
    const std::string head = "[BOUNTY] crimetest bountyset person='" + g_bTestSetPerson + "' law='" + g_bTestSetLaw + "'";
    if (kBmIndexRva == 0) { DebugLog(head + " - no BountyMapIndex address, nothing done"); return; }
    unsigned int pu = 0, lu = 0;
    float pd = -1.0f, ld = -1.0f;
    std::string pn, ln, why;
    ::Character* p = LeverNearestNamed(g_bTestSetPerson, kLeverPickOwnLiving, 0, kBountyTestSetNear, &pu, &pd, &pn, &why);
    if (p == 0) { DebugLog(head + " - person: " + why + ", nothing done"); return; }
    ::Character* law = LeverNearestNamed(g_bTestSetLaw, kLeverPickAny, 0, 1.0e30f, &lu, &ld, &ln, &why);
    if (law == 0) { DebugLog(head + " - law NPC: " + why + ", nothing done"); return; }
    ::Faction* lf = 0;
    if (!CrFactionOf(law, &lf) || lf == 0) { DebugLog(head + " - the law NPC's faction does not read, nothing done"); return; }
    void* key = 0;
    const int kr = BountyKeyForPod(p, lf, &key);
    if (kr != 1 || key == 0)
    {
        DebugLog(head + " - GetBountyFaction " + std::string(kr == 0 ? "is not in the address table" : "faulted or gave no faction") + ", nothing done");
        return;
    }
    const double now = LocalWorldHours();
    if (now < 0.0) { DebugLog(head + " - this game's world clock does not read, nothing done"); return; }
    const BmIndexFn idx = (BmIndexFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kBmIndexRva);
    if (BmWrite(idx, p, key, g_bTestSetAmount, 0, 0, now, now) == 0)
    {
        ++g_bWriteFault;
        DebugLog(head + " - the write faulted, nothing done");
        return;
    }
    RawBounty raw[kRawBountyMax];
    const int n = BmRead(p, raw);
    coopbounty::BountyList after;
    if (n >= 0) BmToList(raw, n, &after);
    DebugLog("[BOUNTY] crimetest bountyset uid=" + S(pu) + " '" + pn + "' key='" + RelationsWireSid((::Faction*)key) + "' amount="
             + S(g_bTestSetAmount) + " on the OWNER - now [" + (n >= 0 ? BountyListText(after) : std::string("unreadable"))
             + "] (law NPC uid=" + S(lu) + " '" + ln + "')");
}

/* M7b slice 4 fold 1 (F5), MAIN THREAD at the K2 safe point: a held crime whose copy now exists joins the queue; one with a newer
   state queued is superseded; one older than kCrimeHoldMs is dropped. Everything held is dropped while engine writes are blocked. */
static void CrimeHeldDrain()
{
    if (g_held.empty()) return;
    if (EngineWritesBlocked()) { g_heldDropped += (long long)g_held.size(); g_held.clear(); return; }
    const DWORD now = ::GetTickCount();
    for (std::map<unsigned int, Held>::iterator it = g_held.begin(); it != g_held.end(); )
    {
        if (g_queue.find(it->first) != g_queue.end()) { ++g_heldSuperseded; g_held.erase(it++); continue; }
        if (FindSpawned(it->first) != 0) { g_queue[it->first] = it->second.p; ++g_heldApplied; g_held.erase(it++); continue; }
        if ((DWORD)(now - it->second.ms) >= kCrimeHoldMs) { ++g_heldExpired; g_held.erase(it++); continue; }
        ++it;
    }
}

// MAIN THREAD, worker paused (combat.cpp detour_tsRagdollUpdates).
void CrimeApplyDrain()
{
    /* crime5: bounties - read and written only here, where the worker's BountyManager::update is paused */
    if (!EngineWritesBlocked()) { BountyTestClearDrain(); BountyTestSetDrain(); BountyApply(); BountySample(); }   /* the levers first */
    else if (!g_bIn.empty()) { g_bIn.clear(); }
    CrimeHeldDrain();   /* M7b slice 4 fold 1 (F5) */
    if (g_queue.empty()) return;
    if (EngineWritesBlocked()) { g_applyBlocked += (long long)g_queue.size(); g_queue.clear(); return; }
    if (kCrNotifyRva == 0 || kCrSetRva == 0) { g_noAddr += (long long)g_queue.size(); g_queue.clear(); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const CrNotifyFn notify = (CrNotifyFn)(base + (uintptr_t)kCrNotifyRva);
    const CrSetFn setCrime = (CrSetFn)(base + (uintptr_t)kCrSetRva);
    std::map<unsigned int, Pending> work;
    work.swap(g_queue);
    for (std::map<unsigned int, Pending>::const_iterator it = work.begin(); it != work.end(); ++it)
    {
        const unsigned int uid = it->first;
        const coopcrime::CrimeState& s = it->second.s;
        if (!net::UidOwnedByPeer(uid, it->second.fromPeer)) { ++g_notOwner; continue; }   // ownership moved while queued
        ::Character* c = FindSpawned(uid);
        if (c == 0)
        {
            ++g_noCopy;
            if (s.crime != 0)   /* M7b slice 4 fold 1 (F5): kept until the copy exists (CrimeHeldDrain) */
            {
                const bool had = g_held.find(uid) != g_held.end();
                if (had || g_held.size() < kCrimeHoldCap) { Held hd; hd.p = it->second; hd.ms = ::GetTickCount(); g_held[uid] = hd; if (!had) ++g_heldNew; }
                else ++g_heldFull;
            }
            if (g_lines < kCrimeLineCap)
            {
                ++g_lines;
                char nb[320];
                std::sprintf(nb, "[CRIME] <- uid=%u crime=%d NO COPY here: retiredHere=%d twin=%d %s", uid, s.crime,
                             KnownUidRetired(uid) ? 1 : 0, IsTwinUid(uid) ? 1 : 0, PeerFactionCopies().c_str());
                DebugLog(std::string(nb));
            }
            continue;
        }
        int before = -1, after = -1; float expBefore = -1.0f, expAfter = -1.0f;
        CrReadCrime(c, &before, &expBefore);
        char extra[256];
        if (s.crime == 0)
        {
            const hand nh(0, 0, (itemType)0, 0, 0);
            const int ok = CrCallSet(setCrime, c, &nh);
            CrReadCrime(c, &after, &expAfter);
            const int refused = (ok && after != 0) ? 1 : 0;
            int forced = 0;
            if (!ok) ++g_faulted;
            else if (after == 0) ++g_clearApplied;
            else
            {
                ++g_clearRefused;   /* the engine kept it: clear the two fields directly (review-crime3 3) */
                if (CrWriteClear(c)) { ++g_clearForced; forced = 1; CrReadCrime(c, &after, &expAfter); }
            }
            std::sprintf(extra, "CLEAR %s crime %d->%d refused=%d forced=%d (setCrime 0x851DB0)", ok ? "called" : "FAULTED", before, after,
                         refused, forced);
            CrLog("<-", uid, s, extra);
            continue;
        }
        ::Faction* f = s.factionSid.empty() ? 0 : RelationsFactionFromWire(s.factionSid);
        if (!s.factionSid.empty() && f == 0) ++g_noFaction;
        unsigned int w[5] = { 0, 0, 0, 0, 0 };
        ::Character* v = (s.victimUid != 0) ? FindSpawned(s.victimUid) : 0;
        if (v == 0 || !CrReadOwnHand(v, w)) { ++g_victimNone; std::memset(w, 0, sizeof(w)); }
        /* the lever's order (speech.cpp CrimeTestDrain): hand(index, serial, type, container, containerStamp) from +0x60.. */
        const hand h(w[3], w[4], (itemType)w[0], w[1], w[2]);
        int exp = (int)(s.expiry + 0.5f);
        if (exp < 1) exp = 1;
        if (exp > kCrimeExpiryMax) exp = kCrimeExpiryMax;
        const int ok = CrCallNotify(notify, c, (void*)f, &h, exp, s.crime);
        CrReadCrime(c, &after, &expAfter);
        if (!ok) ++g_faulted;
        else if (after == s.crime) ++g_applied;
        else ++g_engineRefused;
        std::sprintf(extra, "%s crime %d->%d expiry %.1f->%.1f factionResolved=%d victimHere=%d (notifyCrimeWitnessed 0x851F40)",
                     ok ? "called" : "FAULTED", before, after, CrClampForLog(expBefore), CrClampForLog(expAfter), f != 0 ? 1 : 0, v != 0 ? 1 : 0);
        CrLog("<-", uid, s, extra);
    }
}

void InstallCrime()
{
    if (g_pvpHook != 0) return;
    if (kCrSetRva == 0) { g_pvpHook = -1; ErrorLog("[CRIME] pvp1: SetCrime is not in the address table - player-vs-player crimes are NOT suppressed"); return; }
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    const coop::HookStatus st = coop::AddHook((void*)(base + (uintptr_t)kCrSetRva), (void*)&detour_setCrime, (void**)&orig_setCrime);
    g_pvpHook = (st == coop::SUCCESS) ? 1 : -1;
    if (kCrNotifyRva != 0)
    {
        const coop::HookStatus sn = coop::AddHook((void*)(base + (uintptr_t)kCrNotifyRva), (void*)&detour_notify, (void**)&orig_notify);
        g_pvpNotifyHook = (sn == coop::SUCCESS) ? 1 : -1;
    }
    else g_pvpNotifyHook = -1;
    DebugLog(std::string("[CRIME] pvp1 hooks setCrime 0x851DB0: ") + (g_pvpHook == 1 ? "installed" : "FAILED")
             + ", notifyCrimeWitnessed 0x851F40: " + (g_pvpNotifyHook == 1 ? "installed" : "FAILED")
             + " (a player's crime against a player's faction is never recorded)");
    /* sight1: the senses' player test also passes for every other player's faction (wanted on sight; T-356: one cell each) */
    if (g_sightState == 0)
    {
        if (kCrSightRva == 0) { g_sightState = -1; g_sightWhy = "BountySightTargetTest is not in the address table"; }
        else g_sightState = SightPatch(base + (uintptr_t)kCrSightRva) ? 1 : -1;
        if (g_sightState == 1) DebugLog("[CRIME] sight1 patched the senses' player test 0x85635D (the other players' characters are recognised as wanted on sight)");
        else ErrorLog(std::string("[CRIME] sight1 NOT patched: ") + g_sightWhy + " - the other players' characters are recognised only by the guards' AI search (F899)");
    }
}

void ReportCrime()
{
    char b[640];
    std::sprintf(b, "[CRIME] REPORT owner[sent,sendFailed,playerVictimSamples,sidTooLong,victimUnresolved,expiredSamples,tickBlocked]=%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " copy[recv,malformed,droppedBlocked,notOwner,queueDropped,applied,clearApplied,engineRefused,clearRefused,clearForced,noCopy,"
                 "noFaction,victimNone,faulted,applyBlocked,noAddr]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld tracked=%u lines=%lld",
                 g_sent, g_sendFailed, g_playerVictimSamples, g_sidTooLong, g_victimUnresolved, g_expiredSamples, g_tickBlocked,
                 g_recv, g_malformed, g_droppedBlocked, g_notOwner, g_queueDropped, g_applied, g_clearApplied, g_engineRefused, g_clearRefused,
                 g_clearForced, g_noCopy, g_noFaction, g_victimNone, g_faulted, g_applyBlocked, g_noAddr, (unsigned int)g_last.size(), g_lines);
    char pv[240];
    std::sprintf(pv, " pvp[refused]=%lld pvpHook=%ld pvpNotifyHook=%ld peerKnown=%d sight[state,peerRecognised]=%ld,%lld sightCells[held,dropped]=%d,%d",
                 (long long)g_pvpRefused, (long)g_pvpHook, (long)g_pvpNotifyHook, g_crimePeer != 0 ? 1 : 0, (long)g_sightState,
                 g_sightCells != 0 ? (long long)g_sightCells->hits : -1LL, g_sightHeld, g_sightDropped);   /* T-356: the cells */
    char hv[400];   /* M7b slice 4 fold 1 (F5, F7) */
    std::sprintf(hv, " held[new,applied,superseded,expired,full,dropped,now]=%lld,%lld,%lld,%lld,%lld,%lld,%u withSpawn[sent,failed]=%lld,%lld bountyUnsent=%lld",
                 g_heldNew, g_heldApplied, g_heldSuperseded, g_heldExpired, g_heldFull, g_heldDropped, (unsigned int)g_held.size(),
                 g_withSpawnSent, g_withSpawnFailed, g_bOutUnsent);
    DebugLog(std::string(b) + " " + PeerFactionCopies() + pv + hv);
    char bb[640];
    std::sprintf(bb, "[BOUNTY] REPORT sent[lists,adds,preApplyAdds]=%lld,%lld,%lld applied[lists,adds,addsGated,erased]=%lld,%lld,%lld,%lld"
                 " refused[notOwner,noCopy,noFaction,readFault,writeFault,malformed,queueDropped,noAddr,droppedBlocked]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " playerSkipped=%lld knownReset=%lld known=%u sentTracked=%u lines=%lld timeClamped=%lld"
                 " clears[sent,preApply,written,erased,noop,testCleared]=%lld,%lld,%lld,%lld,%lld,%lld",
                 g_bListsSent, g_bAddsSent, g_bPreApplyAdds, g_bListsApplied, g_bAddsApplied, g_bAddsGated, g_bErased, g_bNotOwner, g_bNoCopy,
                 g_bNoFaction, g_bReadFault, g_bWriteFault, g_bMalformed, g_bQueueDropped, g_bNoAddr, g_bDroppedBlocked, g_bPlayerSkipped,
                 g_bKnownReset, (unsigned int)g_bKnown.size(), (unsigned int)g_bSent.size(), g_bLines, g_bTimeClamped,
                 g_bClearsSent, g_bPreApplyClears, g_bClearsApplied, g_bClearsErased, g_bClearsNoop, g_bTestCleared);
    DebugLog(std::string(bb));
}

} // namespace coop
