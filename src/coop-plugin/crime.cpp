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
// COPY-SIDE WRITES (MSG_CRIME kind 1): a crime this game's engine records on a copy - its own witnesses saw the copy act (a guard
// hit, a trespass its senses caught, DA_CRIME_ALARM) - goes to the character's owner, which records it on its own character;
// the owner's next state then reaches every game. A copy's own countdown may not run (its AI is gated); the owner's clear ends
// it, except while a crime recorded here has not yet come back in the owner's state.
//
// C++03 (VS2010 v100). Every __try lives in a function with no C++ object that has a destructor (C2712).

#include "crime.h"
#include "spawn.h"          /* FindSpawned, MirrorSlot, MirrorCapacity */
#include "speech.h"         /* crimetest bountyset: LeverNearestNamed */
#include "store.h"          /* EngineWritesBlocked */
#include <cstddef>          /* offsetof - the looker refusal counter's place */
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
#include <intrin.h>   /* T-1131: _ReturnAddress - the roll's own call sites */
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <string>
#include <vector>

namespace coop {
/* T-1293: the thread inside ApplyRemoteHit's played hit right now (0 none) - combat.cpp sets it around that one engine call (the
   main thread in practice); the setCrime detour compares it with its own thread, so another thread's crime is never taken for it */
volatile DWORD g_replayHitThread = 0;
DWORD CrimeReplayHitEnter() { const DWORD prev = g_replayHitThread; g_replayHitThread = ::GetCurrentThreadId(); return prev; }
void CrimeReplayHitLeave(DWORD prev) { g_replayHitThread = prev; }
namespace {

unsigned long long kCrNotifyRva = 0; static AddrReg kCrNotifyRva_reg("NotifyCrimeWitnessed", &kCrNotifyRva);   /* Steam_1.0.65 0x851F40 */
unsigned long long kCrSetRva = 0;    static AddrReg kCrSetRva_reg("SetCrime", &kCrSetRva);                     /* Steam_1.0.65 0x851DB0 */
unsigned long long kCrTimeSetRva = 0; static AddrReg kCrTimeSetRva_reg("SetCrimeTimeLeft", &kCrTimeSetRva);   /* Steam_1.0.65 0x851070 (1.0.68 0x851F40) */
typedef void (*CrNotifyFn)(void* bountyManager, void* againstFaction, const hand& againstWho, int expirySeconds, int crime);
typedef bool (*CrSetFn)(void* bountyManager, int crime, void* againstFaction, const hand& againstWho);

const size_t kCrBountyMgr   = 0xF0;    /* Character -> its BountyManager (embedded) */
const size_t kCrCrime       = 0x148;   /* int CrimeEnum */
const size_t kCrFaction     = 0x150;   /* Faction* crimeAgainstFaction */
const size_t kCrHandFields  = 0x168;   /* the victim hand's five id fields (the hand object starts at +0x160) */
const size_t kCrExpiry      = 0x180;   /* float, game seconds */
const size_t kCrOwnHandFields = 0x60;  /* a character's own hand +0x58; its five id fields from +0x60 */
const size_t kCrHandIdsInHand = 0x8;   /* T-1131 fold 2: in a hand OBJECT (as notifyCrimeWitnessed gets it) the five id fields start at +8 (0x168 - 0x160, 0x60 - 0x58) */
const int    kCrimeScanPerTick = 128;  /* owned characters sampled per CrimeTick */
const int    kCrimeMaxPerTick  = 16;   /* MSG_CRIME sent per CrimeTick */
const size_t kCrimeQueueCap    = 256;  /* distinct uids waiting for the safe point */
const int    kCrimeExpiryMax   = 600;  /* game seconds; a larger expiry is clamped */

/* owner, per owned uid: what was last SENT, and what the last SAMPLE saw (review-crime3 2 and 6: the re-set test compares
   with the last sampled expiry; the victim lookup is cached on the last sampled hand whether or not anything was sent) */
struct Last { coopcrime::CrimeState sent; float sampledExpiry; unsigned int hand5[5]; unsigned int victimUid; bool victimIsPlayer;
              int origin; bool evSent; bool evSawNoTime; int evCrime; unsigned int evHand5[5]; void* evFac; bool failedSend; };
/* fix attempt 2 review S1: failedSend - a send of this record failed (either road) and none has gone since: the backstop's retry of it
   is 'retryAfterFailedSend', not 'missed' */
/* T-1442 step 5, the backstop's why (crimewire.h CrimeBackstopWhy): origin - made after a link change or not; evSent - the event
   drain sent this record's state since the sampler's last look; evSawNoTime + evCrime / evHand5 / evFac - the drain read a crime with
   no time left since then (what it read) */
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

/* Where a copy-side crime (kind 1) was seen: the engine's setCrime write, the owner's state refused because the copy held
   another crime, a guard's bounty added to the copy here. */
const int kCapSrcSet = 0, kCapSrcRefused = 1, kCapSrcBounty = 2, kCapSrcReplay = 3;   /* T-1293: kCapSrcReplay - recorded inside a replayed hit, never sent */
void CopyNoteLive(::Character* c, unsigned int uid, int src);   /* defined with the copy-side writes below */

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
            if (!add.empty()) CopyNoteLive(c, u, kCapSrcBounty);   /* the crime the copy holds now goes to the owner as a copy-side write when the owner's state lacks it */
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
/* T-1442 step 5 fix attempt 2 (F1466): BountyManager's direct time-left setter 0x851070 (this, float seconds in XMM1 - the
   disassembly: movaps xmm6, xmm1 ... movss [rbx+0x90], xmm6; the decompile's 'undefined4' is that float). It writes +0x90 (the
   character's +0x180) unless the character's vt+0x30 test holds, with no setCrime: the reaction 0x853E40 and the dialogue
   action 0x21 re-raise a thief's time left through it. ANY THREAD. */
typedef void (*TimeSetHookFn)(void* bountyManager, float seconds);
TimeSetHookFn orig_timeSet = 0;
volatile LONG g_timeSetHook = 0;   /* 0 not installed, 1 installed, -1 failed / no row */

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

/* COPY-SIDE WRITES (kind 1). Every crime the engine records goes through the two detours below; each one recorded on a
   REPLICATED character is noted here (its uid by address compare, the crime) unless this game is writing the owner's state onto
   that copy right now. CrimeTick decides on the main thread whether it was a copy and sends it to the owner. ANY THREAD. */
struct CopyCap { unsigned int uid; int crime; int src; };   /* src: kCapSrc* */
const size_t kCopyCapMax = 256;
CopyCap g_copyCap[kCopyCapMax];
size_t g_copyCapN = 0;
CRITICAL_SECTION g_copyCapLock;
volatile LONG g_copyCapLockInit = 0;
volatile LONG64 g_copyCaptured = 0, g_copyCapOverflow = 0;
volatile LONG64 g_copyCrimeInReplaySkipped = 0;   /* T-1293: crimes recorded on a copy inside a replayed hit, not captured */
long long g_copyLiveNote[2] = { 0, 0 };              /* copy crimes noted held (main thread): the owner's state refused for one, a guard's bounty added */
volatile LONG64 g_trespassSet[4] = { 0, 0, 0, 0 };   /* setCrime TRESPASSING: calls, recorded, on another player's character, recorded on one */
volatile DWORD g_crApplyingThread = 0;   /* the thread writing the owner's state onto a copy right now (CrimeApplyDrain); 0 none */

int CrBmCharPod(const void* bountyManager, const void** c)
{
    *c = 0;
    __try { *c = *(const void* const*)((const char*)bountyManager + 0x40); return 1; }   /* the BountyManager's character */
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The crime the BountyManager holds now (+0x58, setCrime's own write). setCrime records 9, not the 5 it was given, when the
   victim hand resolves to a character the 0x7956A0 test passes for (decomp_851db0); the capture notes what was recorded. */
int CrBmCrimePod(const void* bountyManager, int* crime)
{
    *crime = 0;
    __try { *crime = *(const int*)((const char*)bountyManager + 0x58); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CrRecordedCrime(const void* bountyManager)
{
    int c = 0;
    return CrBmCrimePod(bountyManager, &c) ? c : 0;
}
void CopyCapNote(void* bountyManager, int crime)
{
    if (crime == 0 || g_copyCapLockInit == 0) return;
    if (g_crApplyingThread != 0 && g_crApplyingThread == ::GetCurrentThreadId()) return;   /* the owner's state, from the wire */
    const void* c = 0;
    if (!CrBmCharPod(bountyManager, &c) || c == 0) return;
    const unsigned int uid = FindSpawnedUid(c);
    if (uid == 0) return;
    /* T-1293: inside ApplyRemoteHit's played hit a crime on a copy is the replay's side effect - noted for the drain's line only */
    const bool inReplay = g_replayHitThread != 0 && g_replayHitThread == ::GetCurrentThreadId();
    const bool owned = inReplay && net::IsUidMineAnyThread(uid);   /* only read inside a replay: outside it the decision does not use it */
    const int d = coopcrime::CopyCaptureDecide(owned, inReplay, crime);
    if (d == coopcrime::kCopyCapNone) return;
    if (d == coopcrime::kCopyCapReplay) ::InterlockedIncrement64(&g_copyCrimeInReplaySkipped);
    ::EnterCriticalSection(&g_copyCapLock);
    if (g_copyCapN < kCopyCapMax)
    {
        g_copyCap[g_copyCapN].uid = uid; g_copyCap[g_copyCapN].crime = crime; g_copyCap[g_copyCapN].src = d == coopcrime::kCopyCapReplay ? kCapSrcReplay : kCapSrcSet; ++g_copyCapN;
        if (d == coopcrime::kCopyCapNote) ::InterlockedIncrement64(&g_copyCaptured);
    }
    else ::InterlockedIncrement64(&g_copyCapOverflow);
    ::LeaveCriticalSection(&g_copyCapLock);
}

/* ANY THREAD. Every TRESPASSING write the engine asks for, and whether it recorded it - on any character, and on another player's
   character (its faction is a stand-in) - so the REPORT shows whether the senses' trespass write reaches this entry at all. */
void TrespassSetNote(void* bountyManager, bool recorded)
{
    ::InterlockedIncrement64(&g_trespassSet[0]);
    if (recorded) ::InterlockedIncrement64(&g_trespassSet[1]);
    void* f = 0;
    if (!CrOffenderFactionPod(bountyManager, &f) || f == 0 || !(f == g_crimePeer || IsPeerFaction((::Faction*)f))) return;
    ::InterlockedIncrement64(&g_trespassSet[2]);
    if (recorded) ::InterlockedIncrement64(&g_trespassSet[3]);
}

/* T-1442 step 5 (F1453 design A): every setCrime the engine makes notes its character here, after the original returns (ANY THREAD -
   crime.cpp:636; the copy-capture table's locking: a critical section made in InstallCrime before the hook). No lookup in the
   detour: the main thread's CrimeEventDrainTick finds the uid next frame and decides as the sampler does (CrimeDecideSend). Not
   noted: this game writing the owner's state onto a copy (CrimeApplyDrain's thread) - that is a copy, never sent from here.
   The note is made BEFORE setCrime's caller writes the time left (notifyCrimeWitnessed writes it after, decomp_851f40:22): for a
   main-thread caller the drain reads it finished; for an AI-worker caller it may not be - the sampler corrects that (lateExpiry). */
coopcrime::CrimeEvTable g_crEv;   /* zero-initialised static: empty */
CRITICAL_SECTION g_crEvLock;
volatile LONG g_crEvLockInit = 0;
/* fix attempt 2 (F1466): who noted - setCrime's detour, notifyCrimeWitnessed's after its original, the time-left setter's */
enum { kCrNoteSetCrime = 0, kCrNoteNotifyAfter = 1, kCrNoteTimeSet = 2 };
long long g_crEvNotedBy[3] = { 0, 0, 0 };   /* LOCK HELD: notes that reached the table (a slot taken or already waiting), by kCrNote* */
/* a time-left set's old and new time, for the drain's one line per character per crime; one record per noted character */
struct CrTimeSetRec { const void* who; float oldT; float newT; };
const int kCrTimeSetRecs = coopcrime::kCrimeEvSlots;
CrTimeSetRec g_crTsRec[kCrTimeSetRecs];
int g_crTsN = 0;                 /* LOCK HELD */
long long g_crTsLost = 0;        /* LOCK HELD: a record that found the list full (its note still went) */
int CrimeEvNoteAs(void* bountyManager, int by, float oldT, float newT)
{
    if (g_crEvLockInit == 0) return coopcrime::kCrimeEvNone;
    if (g_crApplyingThread != 0 && g_crApplyingThread == ::GetCurrentThreadId()) return coopcrime::kCrimeEvNone;   /* the owner's state, from the wire */
    const void* c = 0;
    if (!CrBmCharPod(bountyManager, &c) || c == 0) return coopcrime::kCrimeEvNone;
    ::EnterCriticalSection(&g_crEvLock);
    const int r = coopcrime::CrimeEvAdd(&g_crEv, c);
    if (r == coopcrime::kCrimeEvNoted || r == coopcrime::kCrimeEvAlready)
    {
        ++g_crEvNotedBy[by];
        if (by == kCrNoteTimeSet)
        {
            int i = 0;
            while (i < g_crTsN && g_crTsRec[i].who != c) ++i;
            if (i < g_crTsN) g_crTsRec[i].newT = newT;   /* already waiting: keep the first old time, the latest new one */
            else if (g_crTsN < kCrTimeSetRecs) { g_crTsRec[g_crTsN].who = c; g_crTsRec[g_crTsN].oldT = oldT; g_crTsRec[g_crTsN].newT = newT; ++g_crTsN; }
            else ++g_crTsLost;
        }
    }
    ::LeaveCriticalSection(&g_crEvLock);
    return r;
}
void CrimeEvNote(void* bountyManager) { CrimeEvNoteAs(bountyManager, kCrNoteSetCrime, 0.0f, 0.0f); }
/* the BountyManager's time left (+0x90, the character's +0x180) */
int CrBmTimePod(const void* bountyManager, float* t)
{
    *t = 0.0f;
    __try { *t = *(const float*)((const char*)bountyManager + 0x90); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* T-1442 step 5 fix attempt 2: the time-left setter notes its character after the original returns, as setCrime's detour does */
void detour_timeSet(void* bountyManager, float seconds)
{
    float oldT = 0.0f, newT = 0.0f;
    CrBmTimePod(bountyManager, &oldT);
    orig_timeSet(bountyManager, seconds);
    CrBmTimePod(bountyManager, &newT);
    CrimeEvNoteAs(bountyManager, kCrNoteTimeSet, oldT, newT);
}

DWORD g_caughtInNotifyTid = 0;   /* T-1131 fold 2: the main thread inside the caught roll's notifyCrimeWitnessed (0 none) */
int g_caughtRecorded = 0;         /* ... and the crime its setCrime recorded there (0 = none / refused) */
bool detour_setCrime(void* bountyManager, int crime, void* againstFaction, const void* againstWho)
{
    if (PvpRefuse(bountyManager, crime, againstFaction))
    {
        ::InterlockedIncrement64(&g_pvpRefused);
        return false;   /* player against player: the engine never records it (user decision pvp1) */
    }
    const bool r = orig_setCrime(bountyManager, crime, againstFaction, againstWho);
    if (crime == 11) TrespassSetNote(bountyManager, r);   /* TRESPASSING */
    if (r) CopyCapNote(bountyManager, CrRecordedCrime(bountyManager));   /* what the engine recorded (5 may become 9) */
    if (g_caughtInNotifyTid != 0 && g_caughtInNotifyTid == ::GetCurrentThreadId()) g_caughtRecorded = r ? CrRecordedCrime(bountyManager) : 0;   /* T-1131 fold 2 */
    CrimeEvNote(bountyManager);   /* T-1442 step 5: after the original, recorded or not - the drain reads what the engine left */
    return r;
}
uintptr_t g_caughtNotifyAt = 0;   /* T-1131: absolute return address of the caught roll's notifyCrimeWitnessed call (0 = no row) */
bool CaughtBeforeNotify();   /* T-1131, below: true = an event 8 is held for this record (main thread) */
void CaughtAfterNotify(void* fac, const void* who, int expirySeconds, int recorded);
void detour_notify(void* bountyManager, void* againstFaction, const void* againstWho, int expirySeconds, int crime)
{
    const bool atRoll = g_caughtNotifyAt != 0 && (uintptr_t)_ReturnAddress() == g_caughtNotifyAt && CaughtBeforeNotify();   /* T-1131 */
    if (PvpRefuse(bountyManager, crime, againstFaction))
    {
        ::InterlockedIncrement64(&g_pvpRefused);
        if (atRoll) CaughtAfterNotify(againstFaction, againstWho, expirySeconds, 0);   /* T-1131 fold 2: nothing recorded */
        return;
    }
    if (atRoll) { g_caughtRecorded = 0; g_caughtInNotifyTid = ::GetCurrentThreadId(); }
    orig_notify(bountyManager, againstFaction, againstWho, expirySeconds, crime);   /* its own setCrime call (0x851DB0) goes through detour_setCrime, which notes what was recorded */
    CrimeEvNoteAs(bountyManager, kCrNoteNotifyAfter, 0.0f, 0.0f);   /* T-1442 step 5 fix attempt 2: again, now its time left is written (decomp_851f40:22) */
    if (atRoll) { g_caughtInNotifyTid = 0; CaughtAfterNotify(againstFaction, againstWho, expirySeconds, g_caughtRecorded); }   /* T-1131 fold 2: after the record */
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
struct SightCells { volatile LONG64 hits; void* volatile peer[coopcrime::kSightCellCap]; volatile LONG64 lookerRefused; };   /* hits at +0, cell k at +8+8k; the trespass stub's looker refusals after the last cell */
/* T-356: every stand-in the table can hold, plus an old save's coop-peer, has a cell; the stub ends before the cells; the cells fit the page */
typedef char kSightCellsCoverStandIns[(coopcrime::kSightCellCap >= kStandInTableCap + 1) ? 1 : -1];
typedef char kSightStubBeforeCells[(coopcrime::kSightStubLen <= (int)kSightCellsOff) ? 1 : -1];
typedef char kSightCellsInPage[((int)kSightCellsOff + 8 + 8 * coopcrime::kSightCellCap + 8 <= 0x1000) ? 1 : -1];
typedef char kTrespassStubBeforeCells[(coopcrime::kTrespassStubLen <= (int)kSightCellsOff) ? 1 : -1];
typedef char kLookerRefusedAfterCells[(sizeof(void*) != 8 || offsetof(SightCells, lookerRefused) == 8 + 8 * coopcrime::kSightCellCap) ? 1 : -1];
int g_sightHeld = 0, g_sightDropped = 0;   /* T-356: the cells in use at the last tick; other players' factions that found no cell */
SightCells* g_sightCells = 0;
volatile LONG g_sightState = 0;   /* 0 not tried, 1 patched, -1 refused / failed */
const char* g_sightWhy = "not tried";

// The trespass test (P56). In the same senses function a TARGET reaches the building-intruder check (isIntruder_Building 0x851480
// -> setCrime TRESPASSING on the target with the looker's faction and hand, then event 0x11 on the looker) only when its faction is
// a player faction:
//   0x858D13  48 83 B8 50 02 00 00 00   cmp qword [rax+0x250], 0    (rax = the target's faction, vt+0x58)
//   0x858D1B  74 51                     je 0x858D6E                 (not a player: the intruder check is skipped)
// A copy of another player's character is in that player's stand-in faction, so this game's NPCs never caught it trespassing. The
// sight1 stub (SightStubBuild) on its own page, its cells holding the same factions as sight1's, makes such a copy a player-faction
// target for this test only; the TRESPASSING the engine then writes on the copy goes to its owner as a copy-side write (kind 1).
// The test sits outside the senses' 'the looker is not a player' gate (decomp_8587b0: the gate at 0x858B16.., the test after it),
// and past setCrime the engine raises 0x684CA0(looker, target, 0x11) and 0x672F90(looker, target, 1) whatever setCrime returned
// - so a looker of THIS game's player faction would react to another player's copy as an intruder, which a player's own
// characters never do in single player (pvp1 refuses only the crime). The trespass stub (coopcrime::TrespassStubBuild) passes a
// cell hit only when the looker ([rbp+0x148] at the site) is not of this game's player faction; its refusals are counted.
unsigned long long kCrTrespassRva = 0; static AddrReg kCrTrespassRva_reg("BountyTrespassTargetTest", &kCrTrespassRva);   /* Steam_1.0.65 0x858D13 - a patch site, not a function */
const unsigned char kTrespassOrig[10] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x51 };
SightCells* g_trespassCells = 0;
volatile LONG g_trespassState = 0;   /* 0 not tried, 1 patched, -1 refused / failed */
const char* g_trespassWhy = "not tried";

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

// 1 patched; 0 refused (the reason in *why). MAIN THREAD, at start (the preload stage: no world, no AI worker).
int SightPatch(uintptr_t site, const unsigned char* orig, SightCells** out, const char** why, bool lookerGate)
{
    unsigned char now[10];
    if (!SightReadPod((const void*)site, now, 10)) { *why = "the site could not be read"; return 0; }
    if (std::memcmp(now, orig, 10) != 0) { *why = "the 10 bytes at the site are not the expected test (another mod?)"; return 0; }
    unsigned char* stub = (unsigned char*)SightAllocNear(site);
    if (stub == 0) { *why = "no page for the stub within a jump of the site"; return 0; }
    const long long rel = (long long)((intptr_t)stub - (intptr_t)(site + 5));
    if (rel > 0x7FFFFFF0LL || rel < -0x7FFFFFF0LL) { ::VirtualFree(stub, 0, MEM_RELEASE); *why = "the stub page is out of jump range"; return 0; }
    SightCells* cells = (SightCells*)(stub + kSightCellsOff);
    for (int k = 0; k < coopcrime::kSightCellCap; ++k) cells->peer[k] = 0;   /* T-356 */
    cells->hits = 0;
    cells->lookerRefused = 0;
    const int built = lookerGate ? coopcrime::TrespassStubBuild(stub, (unsigned long long)site, coopcrime::kSightCellCap, (int)kSightCellsOff)
                                 : coopcrime::SightStubBuild(stub, (unsigned long long)site, coopcrime::kSightCellCap, (int)kSightCellsOff);
    if (built != (lookerGate ? coopcrime::kTrespassStubLen : coopcrime::kSightStubLen))
    { ::VirtualFree(stub, 0, MEM_RELEASE); *why = "the stub came out at an unexpected length"; return 0; }
    ::FlushInstructionCache(::GetCurrentProcess(), stub, 0x1000);

    unsigned char patch[8];
    patch[0] = 0xE9;
    const int rel32 = (int)rel;
    std::memcpy(patch + 1, &rel32, 4);
    patch[5] = patch[6] = patch[7] = 0x90;
    DWORD oldProt = 0;
    if (!::VirtualProtect((void*)site, 8, PAGE_EXECUTE_READWRITE, &oldProt))
    { ::VirtualFree(stub, 0, MEM_RELEASE); *why = "VirtualProtect refused"; return 0; }
    std::memcpy((void*)site, patch, 8);
    DWORD ignored = 0;
    ::VirtualProtect((void*)site, 8, oldProt, &ignored);
    ::FlushInstructionCache(::GetCurrentProcess(), (void*)site, 8);
    *out = cells;
    *why = "patched";
    return 1;
}

/* COPY-SIDE WRITES (kind 1), MAIN THREAD. g_copyMark: copies holding a crime recorded here and sent to the owner, not yet in
   its state (coopcrime::CopyClearKeep); g_ownerKnown: the crime of the owner's last state applied to each copy; g_copyOut: kind-1
   sends waiting for a road (latest per uid). */
struct CopyMark { int crime; DWORD ms; DWORD runOutMs; };
std::map<unsigned int, CopyMark> g_copyMark;
std::map<unsigned int, int> g_ownerKnown;
struct OwnerAt { float expiry; DWORD ms; };
std::map<unsigned int, OwnerAt> g_ownerKnownAt;   /* the owner's last state's expiry for each copy, and when it was applied here */
long long g_copyReSent = 0;                        /* kind 1 sent again: re-recorded here while the owner's was near its end */
std::map<unsigned int, coopcrime::CrimeState> g_copyOut;
struct K1In { unsigned int uid; coopcrime::CrimeState s; unsigned int fromPeer; };
std::vector<K1In> g_k1In;                      /* owner: kind-1 arrivals for the K2 safe point */
const size_t kK1InCap = 64;
const float kCopyDefaultExpiry = 20.0f;        /* the expiry the engine sets (crimewire.h), when the copy's own does not read */
long long g_copyQueued = 0, g_copySent = 0, g_copyNotRecorded = 0, g_copyAlready = 0, g_copyPlayerVictim = 0, g_copyGone = 0,
          g_copyDropped = 0, g_copySendRetry = 0, g_copySidTooLong = 0;
std::map<unsigned int, int> g_copyReplayHeld;   /* T-1293: the crime a replayed hit recorded on each copy, while the copy holds it */
long long g_copyReplayHeldSkipped = 0, g_copyReplayLines = 0;
const long long kCopyReplayLineCap = 20;
/* T-1293: one line per copy crime not sent because a replayed hit recorded it (shared cap 20) */
void CopyReplayLog(unsigned int uid, int crime, unsigned int victim, const char* why)
{
    if (g_copyReplayLines >= kCopyReplayLineCap) return;
    ++g_copyReplayLines;
    char b[320];
    std::sprintf(b, "[CRIME] copy crime NOT sent uid=%u crime=%d victim=%u - %s (#%lld/%lld)", uid, crime, victim, why, g_copyReplayLines, kCopyReplayLineCap);
    DebugLog(std::string(b));
}
long long g_ownerK1Recv = 0, g_ownerK1Applied = 0, g_ownerK1Refused = 0, g_ownerK1NotMine = 0, g_ownerK1PlayerVictim = 0,
          g_ownerK1Bad = 0, g_ownerK1Engine = 0, g_ownerK1NoChar = 0, g_ownerK1Overflow = 0, g_ownerK1Dropped = 0, g_ownerK1NoFaction = 0;
long long g_clearsKeptLocal = 0, g_localCrimeEnded = 0;
long long g_k1Lines = 0;
const long long kK1LineCap = 40;
volatile LONG64 g_witnessEv[4] = { 0, 0, 0, 0 }, g_witnessEvRepl[4] = { 0, 0, 0, 0 };   /* 0x29, 0x2A, 8, 0x11 */

void K1Log(const char* what, unsigned int uid, const coopcrime::CrimeState& s, const char* extra)
{
    if (g_k1Lines >= kK1LineCap) return;
    ++g_k1Lines;
    char b[400];
    std::sprintf(b, "[CRIME] %s uid=%u crime=%d expiry=%.1f victim=%u faction='%.96s' %s", what, uid, s.crime, s.expiry, s.victimUid,
                 s.factionSid.c_str(), extra ? extra : "");
    DebugLog(std::string(b));
}

bool MarkRunOut(const CopyMark& m, DWORD now) { return (DWORD)(now - m.ms) >= m.runOutMs; }

void CopyForget()
{
    g_copyMark.clear(); g_ownerKnown.clear(); g_ownerKnownAt.clear(); g_copyOut.clear(); g_k1In.clear();
    g_copyReplayHeld.clear();   /* T-1293 */
    if (g_copyCapLockInit != 0) { ::EnterCriticalSection(&g_copyCapLock); g_copyCapN = 0; ::LeaveCriticalSection(&g_copyCapLock); }
}

/* MAIN THREAD. The crime a copy holds now, read at a moment this game's engine acted on that copy (the owner's state refused for
   it, a guard's bounty added to it here), joins the writes the setCrime detour noted; CopyWriteDrain decides on it as on those
   (the owner already holds it or was already sent it: nothing goes). */
void CopyNoteLive(::Character* c, unsigned int uid, int src)
{
    int crime = 0; float e = 0.0f;
    if (c == 0 || uid == 0 || g_copyCapLockInit == 0 || !CrReadCrime(c, &crime, &e) || crime == 0) return;
    ::EnterCriticalSection(&g_copyCapLock);
    if (g_copyCapN < kCopyCapMax)
    {
        g_copyCap[g_copyCapN].uid = uid; g_copyCap[g_copyCapN].crime = crime; g_copyCap[g_copyCapN].src = src; ++g_copyCapN;
        ++g_copyLiveNote[src == kCapSrcBounty ? 1 : 0];
    }
    else ::InterlockedIncrement64(&g_copyCapOverflow);
    ::LeaveCriticalSection(&g_copyCapLock);
}

/* One line per copy-side crime the drain decides on (bounded): where it was seen, what the owner's state holds, the verdict. */
long long g_capLines = 0;
const long long kCapLineCap = 30;
void CapLog(unsigned int uid, int crime, int src, int known, int pending, float ownerLeft, int toSend)
{
    if (g_capLines >= kCapLineCap) return;
    ++g_capLines;
    const char* from = src == kCapSrcRefused ? "the owner's state refused for it" : (src == kCapSrcBounty ? "a guard's bounty added here" : "setCrime");
    char b[320];
    std::sprintf(b, "[CRIME] copy-capture uid=%u crime=%d seen=%s ownerKnown=%d pending=%d ownerLeft=%.1f -> %s", uid, crime, from,
                 known, pending, CrClampForLog(ownerLeft), toSend ? "send" : "already (the owner holds it or was sent it)");
    DebugLog(std::string(b));
}

/* MAIN THREAD (CrimeTick). The writes the detours noted: a copy's crime, read live now, that the owner neither holds nor has been
   told of is queued for its owner (and marked); then the queue is sent - kept while there is no road, dropped when the owner is
   unknown or the crime would have run out. */
void CopyWriteDrain()
{
    CopyCap local[kCopyCapMax];
    size_t n = 0;
    if (g_copyCapLockInit != 0)
    {
        ::EnterCriticalSection(&g_copyCapLock);
        n = g_copyCapN;
        if (n > 0) std::memcpy(local, g_copyCap, n * sizeof(CopyCap));
        g_copyCapN = 0;
        ::LeaveCriticalSection(&g_copyCapLock);
    }
    const DWORD now = ::GetTickCount();
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned int uid = local[i].uid;
        if (net::IsUidMine(uid)) continue;   /* this game's own character: its owner sampler sends it */
        ::Character* c = FindSpawned(uid);
        if (c == 0) { ++g_copyGone; continue; }
        int crime = 0; void* fac = 0; unsigned int hand5[5]; float expiry = 0.0f;
        if (!CrReadOwner(c, &crime, &fac, hand5, &expiry)) continue;
        if (local[i].src == kCapSrcReplay)
        {   /* T-1293: recorded inside a replayed hit - never sent; remembered while the copy holds it (the live-read roads below) */
            if (crime == local[i].crime) g_copyReplayHeld[uid] = crime;
            bool vPlayer = false;
            CopyReplayLog(uid, local[i].crime, VictimUidOf(hand5, &vPlayer), "recorded inside a replayed hit (another game's decided blow): the replay's side effect, not this character's act");
            continue;
        }
        if (crime != local[i].crime) { ++g_copyNotRecorded; continue; }   /* refused by the engine, or replaced since: the live crime wins */
        int pending = 0;
        std::map<unsigned int, CopyMark>::iterator mk = g_copyMark.find(uid);
        if (mk != g_copyMark.end())
        {
            if (!MarkRunOut(mk->second, now)) pending = mk->second.crime;   /* one that ran out is ended below */
        }
        std::map<unsigned int, int>::const_iterator ok = g_ownerKnown.find(uid);
        const int known = ok != g_ownerKnown.end() ? ok->second : 0;
        {   /* T-1293: a live read (owner's state refused, guard's bounty added) of the crime a replayed hit recorded here is not sent */
            std::map<unsigned int, int>::iterator rh = g_copyReplayHeld.find(uid);
            const int rdec = coopcrime::CopyReplayHeldDecide(rh != g_copyReplayHeld.end() ? rh->second : 0, crime, known, local[i].src != kCapSrcSet);
            if (rdec == coopcrime::kReplayHeldEnd) g_copyReplayHeld.erase(rh);
            else if (rdec == coopcrime::kReplayHeldSkip)
            {
                ++g_copyReplayHeldSkipped;
                bool vPlayer = false;
                CopyReplayLog(uid, crime, VictimUidOf(hand5, &vPlayer), local[i].src == kCapSrcBounty ? "a guard's bounty added here, but the crime the copy holds was recorded inside a replayed hit"
                              : "the owner's state refused for it, but the crime the copy holds was recorded inside a replayed hit");
                continue;
            }
        }
        float ownerLeft = 1.0e9f;   /* no owner state: nothing is running out */
        std::map<unsigned int, OwnerAt>::const_iterator oa = g_ownerKnownAt.find(uid);
        if (oa != g_ownerKnownAt.end()) ownerLeft = oa->second.expiry - (float)(DWORD)(now - oa->second.ms) / 1000.0f;
        const int toSend = coopcrime::CopyWriteShouldSend(crime, known, pending, ownerLeft);
        CapLog(uid, crime, local[i].src, known, pending, ownerLeft, toSend);
        if (!toSend) { ++g_copyAlready; continue; }
        if (crime == known) ++g_copyReSent;
        ::Faction* f = (::Faction*)fac;
        bool victimIsPlayer = false;
        const unsigned int victim = VictimUidOf(hand5, &victimIsPlayer);
        if ((f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f))) || victimIsPlayer) { ++g_copyPlayerVictim; continue; }   /* decision 4 */
        coopcrime::CrimeState s;
        s.crime = crime;
        s.expiry = (coopcrime::CrimeExpiryOk(expiry) && expiry > 0.0f) ? expiry : kCopyDefaultExpiry;
        if (s.expiry > (float)kCrimeExpiryMax) s.expiry = (float)kCrimeExpiryMax;
        s.victimUid = victim;
        s.factionSid = (f != 0) ? RelationsWireSid(f) : std::string();
        if (s.factionSid.size() > (size_t)coopcrime::kCrimeMaxSid) { ++g_copySidTooLong; continue; }
        g_copyOut[uid] = s;
        ++g_copyQueued;
        CopyMark m; m.crime = crime; m.ms = now; m.runOutMs = (DWORD)(s.expiry * 1000.0f);
        g_copyMark[uid] = m;
    }
    /* a mark that ran out ends. If the copy still holds that crime and the owner's last state here is no crime, the crime recorded
       here never came back (the owner refused it, or it was lost): the owner's clear is applied now - a clear kept while the mark
       was live, or none ever coming because the owner's state never changed (a copy's own countdown may not run) */
    for (std::map<unsigned int, CopyMark>::iterator mk = g_copyMark.begin(); mk != g_copyMark.end(); )
    {
        if (!MarkRunOut(mk->second, now)) { ++mk; continue; }
        const unsigned int uid = mk->first;
        std::map<unsigned int, int>::const_iterator k = g_ownerKnown.find(uid);
        ::Character* c = FindSpawned(uid);
        int live = 0; float le = 0.0f; unsigned int ownerPeer = 0;
        if ((k == g_ownerKnown.end() || k->second == 0) && c != 0 && CrReadCrime(c, &live, &le) && live == mk->second.crime
            && g_queue.find(uid) == g_queue.end() && g_queue.size() < kCrimeQueueCap && net::UidOwnerPeer(uid, &ownerPeer))
        {
            Pending p; p.s = coopcrime::CrimeState(); p.fromPeer = ownerPeer;
            g_queue[uid] = p;
            ++g_localCrimeEnded;
            K1Log("copy-write", uid, p.s, "ran out unconfirmed: the owner's clear applied to the copy");
        }
        g_copyMark.erase(mk++);
    }
    int sent = 0;
    for (std::map<unsigned int, coopcrime::CrimeState>::iterator it = g_copyOut.begin(); it != g_copyOut.end() && sent < kCrimeMaxPerTick; )
    {
        std::map<unsigned int, CopyMark>::iterator mk = g_copyMark.find(it->first);
        if (mk == g_copyMark.end() || mk->second.crime != it->second.crime || MarkRunOut(mk->second, now))
        { ++g_copyDropped; g_copyOut.erase(it++); continue; }   /* its crime would have run out (or was replaced) before a road came */
        const int r = net::SendCrimeToOwner(it->first, it->second);
        if (r == 0) { ++g_copySendRetry; ++it; continue; }   /* no road now: kept for the next tick */
        if (r > 0) { ++g_copySent; ++sent; K1Log("copy-write ->", it->first, it->second, "recorded here on the copy - sent to its owner (kind 1)"); }
        else { ++g_copyDropped; g_copyMark.erase(it->first); }   /* no owner on record */
        g_copyOut.erase(it++);
    }
}

/* MAIN THREAD, K2 safe point (CrimeApplyDrain). Kind 1 on the owner: the crime another game's witnesses recorded on its copy is
   recorded on this game's own character through notifyCrimeWitnessed (the hooked entry: pvp1 applies); the owner sampler's next
   state carries it to every game. */
void OwnerCopyWriteDrain()
{
    if (g_k1In.empty()) return;
    std::vector<K1In> work;
    work.swap(g_k1In);
    if (EngineWritesBlocked()) { g_ownerK1Dropped += (long long)work.size(); return; }
    if (kCrNotifyRva == 0) { g_noAddr += (long long)work.size(); return; }
    const CrNotifyFn notify = (CrNotifyFn)((uintptr_t)::GetModuleHandleA(0) + (uintptr_t)kCrNotifyRva);
    for (size_t i = 0; i < work.size(); ++i)
    {
        const K1In& in = work[i];
        const coopcrime::CrimeState& s = in.s;
        const bool mine = net::IsUidMine(in.uid);
        ::Character* c = mine ? FindSpawned(in.uid) : 0;
        if (mine && c == 0) { ++g_ownerK1NoChar; continue; }
        ::Faction* f = s.factionSid.empty() ? 0 : RelationsFactionFromWire(s.factionSid);
        if (!s.factionSid.empty() && f == 0) ++g_ownerK1NoFaction;
        const bool facPlayer = f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f));
        unsigned int w[5] = { 0, 0, 0, 0, 0 };
        bool victimIsPlayer = false;
        ::Character* v = (s.victimUid != 0) ? FindSpawned(s.victimUid) : 0;
        if (v != 0)
        {
            ::Faction* vf = 0;
            if (CrFactionOf(v, &vf) && vf != 0 && (IsPlayerFaction(vf) || IsPeerFaction(vf))) victimIsPlayer = true;
            if (!CrReadOwnHand(v, w)) std::memset(w, 0, sizeof(w));
        }
        char extra[200];
        const int d = coopcrime::OwnerCopyWriteDecide(mine, s.crime, facPlayer, victimIsPlayer);
        if (d != coopcrime::kOwnerWriteApply)
        {
            ++g_ownerK1Refused;
            if (d == coopcrime::kOwnerWriteNotMine) ++g_ownerK1NotMine;
            else if (d == coopcrime::kOwnerWritePlayerVictim) ++g_ownerK1PlayerVictim;
            else ++g_ownerK1Bad;
            std::sprintf(extra, "from=%u REFUSED (%s)", in.fromPeer, d == coopcrime::kOwnerWriteNotMine ? "not this game's character"
                         : (d == coopcrime::kOwnerWritePlayerVictim ? "a crime against a player: decision 4" : "not a crime"));
            K1Log("copy-write <-", in.uid, s, extra);
            continue;
        }
        const hand h(w[3], w[4], (itemType)w[0], w[1], w[2]);
        int exp = (int)(s.expiry + 0.5f);
        if (exp < 1) exp = 1;
        if (exp > kCrimeExpiryMax) exp = kCrimeExpiryMax;
        int before = -1, after = -1; float eb = -1.0f, ea = -1.0f;
        CrReadCrime(c, &before, &eb);
        const int ok = CrCallNotify(notify, c, (void*)f, &h, exp, s.crime);
        CrReadCrime(c, &after, &ea);
        if (ok && after == s.crime) ++g_ownerK1Applied;
        else { ++g_ownerK1Refused; ++g_ownerK1Engine; }
        std::sprintf(extra, "from=%u %s crime %d->%d factionResolved=%d victimHere=%d - recorded on our own character (notifyCrimeWitnessed)",
                     in.fromPeer, !ok ? "FAULTED" : (after == s.crime ? "applied" : "REFUSED by the engine or pvp1"), before, after,
                     f != 0 ? 1 : 0, v != 0 ? 1 : 0);
        K1Log("copy-write <-", in.uid, s, extra);
    }
}

/* MAIN THREAD, K2 safe point: the owner's CLEAR for a copy. true = kept: a crime recorded here is still waiting for the owner's
   state (coopcrime::CopyClearKeep). A mark that ran out, or whose crime the copy no longer holds, ends here. */
bool CopyClearKept(unsigned int uid, int liveCrime, const coopcrime::CrimeState& s)
{
    std::map<unsigned int, CopyMark>::iterator mk = g_copyMark.find(uid);
    if (mk == g_copyMark.end()) return false;
    const bool runOut = MarkRunOut(mk->second, ::GetTickCount());
    if (coopcrime::CopyClearKeep(true, mk->second.crime, runOut, liveCrime))
    {
        ++g_clearsKeptLocal;
        char extra[160];
        std::sprintf(extra, "CLEAR KEPT: crime %d recorded here is not in the owner's state yet (sent as kind 1)", liveCrime);
        K1Log("<-", uid, s, extra);
        return true;
    }
    g_copyMark.erase(mk);
    return false;
}

/* T-1131 DESIGN A: A LOST CAUGHT ROLL'S WATCHER REACTION IS RAISED ON THE GAME THAT RUNS THE WATCHER.
   A take with thievery checks rolls the thief's caught roll Character vt+0x298 0x793C70 on the THIEF's game (decomp_793c70.txt).
   Lost, it calls 0x684BD0(watcher's Dialogue +0x280, thief, 8, 0) - the watcher's 'caught' event (call site 0x793D4B, row
   TheftCaughtEv8Ret) - then notifyCrimeWitnessed(thief, owner's faction, owner's hand, 30, 3) and 0x672F90(watcher, thief, 4), the
   watcher's memory note (call site 0x793D88, row TheftCaughtMemoryRet); its other branch (watcher +0x2F8 == 1 and a coin flip) calls
   0x684BD0(.., thief, 0x2F, 0) only (call site 0x793CFB, row TheftCaughtEv2FRet). When the watcher is ANOTHER game's character, that
   reaction ran on this game's copy (its line dropped at say(), its AI gated) and the real watcher never reacted (T2220, Confirmed).
   Now, on the thief's game, at those two calls from inside the roll (told apart by their return addresses, so no hook on the roll
   itself), on the main thread, for a copy watcher and this game's thief with a road open to the watcher's owner: the copy's event
   and its memory note are NOT run; after the roll's crime write (the memory-note call) the thief's crime and the event go to the
   watcher's owner as MSG_CRIME kind 3 (crimewire.h). With no road (or off the main thread) the copy's reaction runs here as before
   and a line says why; a send that fails after the event was held back runs the held copy reaction then. On the watcher's owner:
   refused with a line when it does not run the watcher now (no retry); else the thief's crime is queued as a kind-0 state first and,
   at the K2 safe point after that write, the same engine entries are called on its own watcher: 0x684BD0(dlg, thief's copy, ev, 0)
   and, for event 8, 0x672F90(watcher, thief's copy, 4), after the roll's own state stop of the watcher (vt+0x368 / +0x370).
   Review folds 1-2: event 8 is sent at the roll's notifyCrimeWitnessed call (row TheftCaughtNotifyRet, through pvp1's hook) with the
   crime the engine recorded there (sent AFTER the record); with no road there the copy's event runs first, in the roll's order. */
unsigned long long kCaughtAlertRva = 0; static AddrReg kCaughtAlertRva_reg("StealthKoFailAlert", &kCaughtAlertRva);   /* Steam_1.0.65 0x684BD0: (Dialogue*, who, event, flag) */
unsigned long long kCaughtMemRva = 0;   static AddrReg kCaughtMemRva_reg("PlatoonMemoryNote", &kCaughtMemRva);       /* Steam_1.0.65 0x672F90: (watcher, about, tag) */
unsigned long long kCaughtEv8Ret = 0;   static AddrReg kCaughtEv8Ret_reg("TheftCaughtEv8Ret", &kCaughtEv8Ret);       /* Steam_1.0.65 0x793D50 */
unsigned long long kCaughtEv2FRet = 0;  static AddrReg kCaughtEv2FRet_reg("TheftCaughtEv2FRet", &kCaughtEv2FRet);    /* Steam_1.0.65 0x793D00 */
unsigned long long kCaughtMemRet = 0;   static AddrReg kCaughtMemRet_reg("TheftCaughtMemoryRet", &kCaughtMemRet);    /* Steam_1.0.65 0x793D8D */
unsigned long long kCaughtNotifyRet = 0; static AddrReg kCaughtNotifyRet_reg("TheftCaughtNotifyRet", &kCaughtNotifyRet); /* Steam_1.0.65 0x793D7C: the roll's notifyCrimeWitnessed call returns here */
typedef unsigned long long (*CaughtAlertFn)(void* dlg, void* who, unsigned int ev, char flag);
typedef void (*CaughtMemFn)(void* watcher, void* about, int tag);
CaughtAlertFn orig_caughtAlert = 0;
CaughtMemFn orig_caughtMem = 0;
uintptr_t g_caughtEv8At = 0, g_caughtEv2FAt = 0, g_caughtMemAt = 0;   /* absolute return addresses of the roll's three calls (0 = no row) */
long g_caughtHook = 0;                                                /* 1 both hooks in, -1 not */
const size_t kCaughtDlgOfChar = 0x280;   /* Character -> its Dialogue (the roll's own read, 0x793D3A) */
const size_t kCaughtDlgMe = 0x150;       /* Dialogue::me (0x684BD0's own read, 0x684BE7) */
struct CaughtPend { int active; int sent; void* watcherAt; void* thiefAt; unsigned int watcherUid, thiefUid; int ev; char flag; };
/* MAIN THREAD: an event 8 held back at the roll's event call. Held by uid; the two addresses are kept only to check, after a fresh
   FindSpawned of each uid, that both are still the same characters (they are never read through). sent = 1: kind 3 went out at the
   roll's crime record and the memory-note call is to be skipped. */
CaughtPend g_caughtPend = { 0, 0, 0, 0, 0u, 0u, 0, 0 };
struct CaughtIn { unsigned int thiefUid, watcherUid, fromPeer; int ev; };
std::vector<CaughtIn> g_caughtIn;                          /* MAIN THREAD: received kind 3, raised at the K2 safe point */
const size_t kCaughtInCap = 64;
long long g_caughtFwd[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };     /* thief's game: heldBack, sent, sendFailed (copy reaction run), noRoute, memMissed, crimeNone, gone, droppedLoading */
volatile LONG64 g_caughtOffMain = 0;
long long g_caughtOwn[7] = { 0, 0, 0, 0, 0, 0, 0 };        /* watcher's owner: recv, refused, raised, engineRefused, faulted, overflow, notOwnerNow */

void* CaughtReadPtrPod(const void* p, size_t off)
{
    if (p == 0) return 0;
    __try { return *(void* const*)((const char*)p + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CaughtHandPod(const void* h, unsigned int* hand5)   /* h = a hand object; its id fields from +kCrHandIdsInHand */
{
    __try { for (int i = 0; i < 5; ++i) hand5[i] = ((const unsigned int*)((const char*)h + kCrHandIdsInHand))[i]; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* 1 called (*r = its answer), 0 faulted. No C++ object here (C2712). */
int CaughtCallAlertPod(CaughtAlertFn fn, void* dlg, void* who, unsigned int ev, char flag, unsigned long long* r)
{
    __try { *r = fn(dlg, who, ev, flag); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int CaughtCallMemPod(CaughtMemFn fn, void* watcher, void* about, int tag)
{
    __try { fn(watcher, about, tag); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* M2 (review fold 1): the roll's own 'stop what you are doing' step on its watcher, before event 8 (decomp_793c70 :35-43, the code at
   0x793D05-0x793D34): state = vt+0x368(watcher); the first answer 3, else a second read's answer 1 -> vt+0x370(watcher, 0). *state = the first
   answer (-1 unread). 1 done, 0 faulted. */
int CaughtStopPod(void* w, int* state, int* stopped)
{
    *state = -1; *stopped = 0;
    __try
    {
        void** vt = *(void***)w;
        typedef int (*StateFn)(void*);
        typedef void (*StopFn)(void*, int);
        const int st = ((StateFn)vt[0x368 / 8])(w);
        *state = st;
        /* decomp_793c70.txt :35-42: stop when the first answer is 3, else when a second read answers 1 */
        const bool stop = (st == 3) || (((StateFn)vt[0x368 / 8])(w) == 1);
        if (stop) { ((StopFn)vt[0x370 / 8])(w, 0); *stopped = 1; }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool CaughtOnMain()
{
    const unsigned long m = StoreMainThreadId();
    return m != 0 && ::GetCurrentThreadId() == (DWORD)m;
}
/* MAIN THREAD: the held event's two characters looked up again by uid; 1 = both still here at the same addresses, *dlg the watcher's
   Dialogue (its me = the watcher). */
int CaughtResolve(const CaughtPend& p, void** w, void** t, void** dlg)
{
    *w = 0; *t = 0; *dlg = 0;
    void* const cw = FindSpawned(p.watcherUid);
    void* const ct = FindSpawned(p.thiefUid);
    if (cw == 0 || ct == 0 || cw != p.watcherAt || ct != p.thiefAt) return 0;
    void* const d = CaughtReadPtrPod(cw, kCaughtDlgOfChar);
    if (d == 0 || CaughtReadPtrPod(d, kCaughtDlgMe) != cw) return 0;
    *w = cw; *t = ct; *dlg = d;
    return 1;
}
/* the thief's crime as a kind-3 state: crime 1..max, not against a player (decision 4), sid sendable; else crime 0 */
coopcrime::CrimeState CaughtState(int crime, float expiry, void* fac, const unsigned int* hand5)
{
    coopcrime::CrimeState s;
    if (crime <= 0 || crime > coopcrime::kCrimeMaxEnum) return s;
    ::Faction* f = (::Faction*)fac;
    bool victimIsPlayer = false;
    const unsigned int victim = VictimUidOf(hand5, &victimIsPlayer);
    const std::string sid = (f != 0) ? RelationsWireSid(f) : std::string();
    if ((f != 0 && (IsPlayerFaction(f) || IsPeerFaction(f))) || victimIsPlayer || sid.size() > (size_t)coopcrime::kCrimeMaxSid) return s;
    s.crime = crime;
    s.expiry = (coopcrime::CrimeExpiryOk(expiry) && expiry > 0.0f) ? expiry : kCopyDefaultExpiry;
    if (s.expiry > (float)kCrimeExpiryMax) s.expiry = (float)kCrimeExpiryMax;
    s.victimUid = victim;
    s.factionSid = sid;
    return s;
}
void CaughtSentLog(const CaughtPend& p, const coopcrime::CrimeState& s, const char* at)
{
    char b[400];
    std::sprintf(b, "[CRIME] caught -> watcher uid=%u thief uid=%u ev=0x%X crime=%d expiry=%.1f victim=%u sent to the watcher's owner (kind 3, %s; the copy's own event%s not run here)",
                 p.watcherUid, p.thiefUid, (unsigned int)p.ev, s.crime, CrClampForLog(s.expiry), s.victimUid, at,
                 p.ev == coopcrime::kCaughtEvCaught ? " and memory note" : "");
    DebugLog(std::string(b));
}
/* MAIN THREAD: the copy's held reaction run here after all (a send that failed): its event, and for 8 its memory note when runMem */
void CaughtRunLocal(const CaughtPend& p, void* w, void* t, void* dlg, bool runMem, int sendRc, const char* at)
{
    unsigned long long er = 0;
    const int evOk = (orig_caughtAlert != 0) ? CaughtCallAlertPod(orig_caughtAlert, dlg, t, (unsigned int)p.ev, p.flag, &er) : 0;
    int memOk = -1;
    if (runMem && p.ev == coopcrime::kCaughtEvCaught && orig_caughtMem != 0) memOk = CaughtCallMemPod(orig_caughtMem, w, t, 4);
    char b[400];
    std::sprintf(b, "[CRIME] caught -> watcher uid=%u thief uid=%u ev=0x%X NOT sent (%s, %s) - the copy's own reaction runs here as before: event %s memoryNote=%d",
                 p.watcherUid, p.thiefUid, (unsigned int)p.ev, sendRc == 0 ? "no road now" : "never sendable", at, evOk ? "raised" : "FAULTED", memOk);
    ErrorLog(std::string(b));
}
void CaughtGoneLog(const CaughtPend& p, const char* why)
{
    char b[300];
    std::sprintf(b, "[CRIME] caught -> watcher uid=%u thief uid=%u ev=0x%X DROPPED: %s - neither game raises this catch",
                 p.watcherUid, p.thiefUid, (unsigned int)p.ev, why);
    DebugLog(std::string(b));
}

/* MAIN THREAD: send the held event with the thief's crime as it stands now (the 0x2F branch at its event call; the backstops). Not
   sent: the copy's held reaction runs here (L4: after the crime record when the roll had already written it). */
void CaughtSendHeld(const CaughtPend& p, bool runMem, const char* at)
{
    void* w = 0; void* t = 0; void* dlg = 0;
    if (!CaughtResolve(p, &w, &t, &dlg)) { ++g_caughtFwd[6]; CaughtGoneLog(p, "the watcher or the thief is no longer the character the roll named"); return; }
    int crime = 0; void* fac = 0; unsigned int hand5[5] = { 0, 0, 0, 0, 0 }; float expiry = 0.0f;
    if (!CrReadOwner(t, &crime, &fac, hand5, &expiry)) crime = 0;
    const coopcrime::CrimeState s = CaughtState(crime, expiry, fac, hand5);
    if (s.crime == 0) ++g_caughtFwd[5];
    coopcrime::CrimeCaught c;
    c.watcherUid = p.watcherUid; c.ev = p.ev;
    const int r = net::SendCrimeCaught(p.thiefUid, s, c);
    if (r > 0) { ++g_caughtFwd[1]; CaughtSentLog(p, s, at); return; }
    ++g_caughtFwd[2];
    CaughtRunLocal(p, w, t, dlg, runMem, r, at);
}

/* MAIN THREAD, inside the roll: notifyCrimeWitnessed called from the roll (0x793D77, row TheftCaughtNotifyRet) while an event 8 is
   held. BEFORE the record (fold 2): with no road to the watcher's owner now, the copy's event runs here at once - the roll's own order
   (event, crime, memory note) - and the hold ends. true = still held: the forward follows the record. */
bool CaughtBeforeNotify()
{
    if (g_caughtPend.active == 0 || g_caughtPend.sent != 0 || !CaughtOnMain()) return false;
    if (net::CaughtRouteOpen(g_caughtPend.watcherUid)) return true;
    const CaughtPend p = g_caughtPend;
    g_caughtPend.active = 0;
    void* w = 0; void* t = 0; void* dlg = 0;
    if (!CaughtResolve(p, &w, &t, &dlg)) { ++g_caughtFwd[6]; CaughtGoneLog(p, "the watcher or the thief is no longer the character the roll named"); return false; }
    ++g_caughtFwd[2];
    CaughtRunLocal(p, w, t, dlg, false, 0, "before the roll's crime record, single player's order");
    return false;
}

/* MAIN THREAD, inside the roll, AFTER notifyCrimeWitnessed (fold 2): kind 3 carries what the engine actually recorded - the crime
   setCrime recorded inside that call (`recorded`; 0 = notify, setCrime or pvp1 refused it: then no crime, with one line), with the
   call's own expiry, faction and victim hand. Sent: the memory-note call is skipped (sent = 1). Not sent (the road failed inside
   this one call): the copy's event runs now, after the record (rare), and the memory note then runs as the engine's. */
void CaughtAfterNotify(void* fac, const void* who, int expirySeconds, int recorded)
{
    if (g_caughtPend.active == 0 || g_caughtPend.sent != 0 || !CaughtOnMain()) return;
    const int crime = recorded;
    const CaughtPend p = g_caughtPend;
    void* w = 0; void* t = 0; void* dlg = 0;
    if (!CaughtResolve(p, &w, &t, &dlg))
    {
        g_caughtPend.active = 0; ++g_caughtFwd[6];
        CaughtGoneLog(p, "the watcher or the thief is no longer the character the roll named");
        return;
    }
    unsigned int hand5[5] = { 0, 0, 0, 0, 0 };
    if (who == 0 || !CaughtHandPod(who, hand5)) std::memset(hand5, 0, sizeof(hand5));
    const coopcrime::CrimeState s = CaughtState(crime, (float)expirySeconds, fac, hand5);
    if (s.crime == 0) ++g_caughtFwd[5];
    if (crime == 0)
    {
        char nb[300];
        std::sprintf(nb, "[CRIME] caught -> watcher uid=%u thief uid=%u ev=0x%X: the engine did not record the thief's crime (notifyCrimeWitnessed / setCrime / pvp1 refused it) - kind 3 carries no crime",
                     p.watcherUid, p.thiefUid, (unsigned int)p.ev);
        DebugLog(std::string(nb));
    }
    coopcrime::CrimeCaught c;
    c.watcherUid = p.watcherUid; c.ev = p.ev;
    const int r = net::SendCrimeCaught(p.thiefUid, s, c);
    if (r > 0) { ++g_caughtFwd[1]; g_caughtPend.sent = 1; CaughtSentLog(p, s, "at the roll's crime record"); return; }
    ++g_caughtFwd[2];
    g_caughtPend.active = 0;
    CaughtRunLocal(p, w, t, dlg, false, r, "after the roll's crime record: the send failed inside the call");
}

/* MAIN THREAD or ANY THREAD: the roll's own call of the watcher's event (the detour below saw its return address).
   L5 (review fold 1, kept): the let-off branch 0x2F is chosen inside the roll from THIS game's copy of the watcher (its +0x2F8 and
   this game's coin flip); the forward carries that choice as it is. */
unsigned long long CaughtAtRoll(void* dlg, void* who, unsigned int ev, char flag)
{
    void* const watcher = CaughtReadPtrPod(dlg, kCaughtDlgMe);
    const unsigned int wu = (watcher != 0) ? FindSpawnedUid(watcher) : 0u;
    const unsigned int tu = (who != 0) ? FindSpawnedUid(who) : 0u;
    const bool onMain = CaughtOnMain();
    /* L2: on the main thread the exact owner table; off it the lock-free mirror (only to pass the event on untouched) */
    const bool wCopy = wu != 0 && !(onMain ? net::IsUidMine(wu) : net::IsUidMineAnyThread(wu));
    const bool tMine = tu != 0 && (onMain ? net::IsUidMine(tu) : net::IsUidMineAnyThread(tu));
    const bool hooksReady = (int)ev == coopcrime::kCaughtEvLetOff || (orig_notify != 0 && g_caughtNotifyAt != 0);   /* event 8 is sent at the crime record */
    const bool route = onMain && wCopy && tMine && hooksReady && net::CaughtRouteOpen(wu);
    const int d = coopcrime::CaughtForwardDecide((int)ev, wCopy, tMine, onMain, route);
    if (d == coopcrime::kCaughtPass) return orig_caughtAlert(dlg, who, ev, flag);
    if (d == coopcrime::kCaughtLocalOffMain) { ::InterlockedIncrement64(&g_caughtOffMain); return orig_caughtAlert(dlg, who, ev, flag); }
    if (g_caughtPend.active != 0)   /* a held event 8 the crime-record / memory-note calls never ended (not expected: straight-line code) */
    {
        const CaughtPend old = g_caughtPend;
        g_caughtPend.active = 0;
        ++g_caughtFwd[4];
        if (old.sent == 0) CaughtSendHeld(old, false, "the next roll came first");
    }
    if (d == coopcrime::kCaughtLocalNoRoute)
    {
        ++g_caughtFwd[3];
        char b[320];
        std::sprintf(b, "[CRIME] caught -> watcher uid=%u thief uid=%u ev=0x%X NOT forwarded: %s - the copy's own reaction runs here (as before)",
                     wu, tu, ev, hooksReady ? "no road to the watcher's owner" : "the crime-record hook is not installed");
        DebugLog(std::string(b));
        return orig_caughtAlert(dlg, who, ev, flag);
    }
    ++g_caughtFwd[0];
    CaughtPend p;
    p.active = 1; p.sent = 0; p.watcherAt = watcher; p.thiefAt = who; p.watcherUid = wu; p.thiefUid = tu; p.ev = (int)ev; p.flag = flag;
    if ((int)ev == coopcrime::kCaughtEvCaught) { g_caughtPend = p; return 0; }   /* sent after the roll's crime record (CaughtAfterNotify) */
    CaughtSendHeld(p, false, "at the event (0x2F: no crime record follows)");
    return 0;
}

unsigned long long detour_caughtAlert(void* dlg, void* who, unsigned int ev, char flag)
{
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (ra == 0 || (ra != g_caughtEv8At && ra != g_caughtEv2FAt)) return orig_caughtAlert(dlg, who, ev, flag);
    return CaughtAtRoll(dlg, who, ev, flag);
}

void detour_caughtMem(void* watcher, void* about, int tag)
{
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    if (ra == 0 || ra != g_caughtMemAt || g_caughtPend.active == 0 || !CaughtOnMain()) { orig_caughtMem(watcher, about, tag); return; }
    if (g_caughtPend.watcherAt != watcher || g_caughtPend.thiefAt != about) { orig_caughtMem(watcher, about, tag); return; }
    const CaughtPend p = g_caughtPend;
    g_caughtPend.active = 0;
    if (p.sent != 0) return;   /* sent at the crime record: the copy's memory note is not written here */
    ++g_caughtFwd[4];          /* the crime-record call was not seen: send now (the crime is recorded by now) */
    CaughtSendHeld(p, true, "at the memory-note call (the crime-record call was not seen)");
}

void CaughtInstall()
{
    if (g_caughtHook != 0) return;
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    if (kCaughtAlertRva == 0 || kCaughtMemRva == 0 || kCaughtEv8Ret == 0 || kCaughtEv2FRet == 0 || kCaughtMemRet == 0 || kCaughtNotifyRet == 0)
    {
        g_caughtHook = -1;
        ErrorLog("[CRIME] T-1131 caught forward NOT installed: a row is missing (StealthKoFailAlert / PlatoonMemoryNote / TheftCaught*Ret) - a caught roll's copy watcher reacts here, as before");
        return;
    }
    g_caughtEv8At = base + (uintptr_t)kCaughtEv8Ret; g_caughtEv2FAt = base + (uintptr_t)kCaughtEv2FRet; g_caughtMemAt = base + (uintptr_t)kCaughtMemRet;
    g_caughtNotifyAt = base + (uintptr_t)kCaughtNotifyRet;   /* read by detour_notify (pvp1's hook, installed in InstallCrime) */
    const coop::HookStatus a = coop::AddHook((void*)(base + (uintptr_t)kCaughtAlertRva), (void*)&detour_caughtAlert, (void**)&orig_caughtAlert);
    if (a != coop::SUCCESS) { orig_caughtAlert = 0; g_caughtEv8At = g_caughtEv2FAt = g_caughtMemAt = 0; g_caughtNotifyAt = 0; g_caughtHook = -1; }
    else
    {
        const coop::HookStatus m = coop::AddHook((void*)(base + (uintptr_t)kCaughtMemRva), (void*)&detour_caughtMem, (void**)&orig_caughtMem);
        if (m != coop::SUCCESS) { orig_caughtMem = 0; g_caughtMemAt = 0; }   /* L1: event 8 is still sent at the crime record; the copy's memory note then runs here */
        g_caughtHook = 1;
    }
    DebugLog(std::string("[CRIME] T-1131 caught forward hooks: event entry 0x684BD0 ") + (g_caughtHook == 1 ? "installed" : "FAILED")
             + ", memory note 0x672F90 " + (orig_caughtMem != 0 ? "installed" : "FAILED (the copy's memory note runs here)")
             + " (event 8 is sent at the roll's crime record, through pvp1's notifyCrimeWitnessed hook; a lost caught roll whose watcher is another game's character is raised on that game)");
}

/* MAIN THREAD (CrimeTick, after its teardown check): a held event 8 still open outside a roll. sent = the memory-note call was not
   seen (nothing left to do); else it is sent now, its characters looked up again by uid. While the world loads or tears down
   (blocked) it is dropped with a line. */
void CaughtPendFlush(bool blocked)
{
    if (g_caughtPend.active == 0) return;
    const CaughtPend p = g_caughtPend;
    g_caughtPend.active = 0;
    if (p.sent != 0) return;   /* already sent at the crime record: nothing is dropped, nothing missed */
    if (blocked) { ++g_caughtFwd[7]; CaughtGoneLog(p, "the world is loading or tearing down"); return; }
    ++g_caughtFwd[4];
    CaughtSendHeld(p, false, "the roll's crime-record and memory-note calls were not seen");
}

/* MAIN THREAD, K2 safe point (CrimeApplyDrain), after the queued crimes were written: each received kind 3 checked again on this
   game's own tables (the second owner check) and raised on our own watcher through the engine's own entries (the hooked ones: called
   from here they pass). For event 8 the roll's own 'stop what you are doing' step runs first (M2). */
void CaughtRaiseDrain()
{
    if (g_caughtIn.empty()) return;
    std::vector<CaughtIn> work;
    work.swap(g_caughtIn);
    const uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    for (size_t i = 0; i < work.size(); ++i)
    {
        const CaughtIn& in = work[i];
        char b[460];
        ::Character* w = net::IsUidMine(in.watcherUid) ? FindSpawned(in.watcherUid) : 0;
        ::Character* t = FindSpawned(in.thiefUid);
        int d = coopcrime::CaughtOwnerDecide(net::IsUidMine(in.watcherUid), w != 0, net::UidOwnedByPeer(in.thiefUid, in.fromPeer), t != 0, in.ev);
        const char* why = coopcrime::CaughtOwnerWhy(d);
        if (d == coopcrime::kCaughtNotOwner) { ++g_caughtOwn[6]; why = "the watcher changed owner in flight - neither game raises this catch"; }
        void* dlg = 0;
        if (d == coopcrime::kCaughtRaise && EngineWritesBlocked()) { d = -1; why = "the world is loading or tearing down"; }
        if (d == coopcrime::kCaughtRaise && kCaughtAlertRva == 0) { d = -1; why = "the event entry is not in the address table"; }
        if (d == coopcrime::kCaughtRaise)
        {
            dlg = CaughtReadPtrPod(w, kCaughtDlgOfChar);
            if (dlg == 0 || CaughtReadPtrPod(dlg, kCaughtDlgMe) != (void*)w) { d = -1; why = "the watcher has no Dialogue"; }
        }
        if (d != coopcrime::kCaughtRaise)
        {
            ++g_caughtOwn[1];
            std::sprintf(b, "[CRIME] caught <- watcher uid=%u thief uid=%u ev=0x%X from=%u REFUSED at the raise: %s (no retry)",
                         in.watcherUid, in.thiefUid, (unsigned int)in.ev, in.fromPeer, why);
            DebugLog(std::string(b));
            continue;
        }
        int state = -1, stopped = 0, stopOk = -1;
        if (in.ev == coopcrime::kCaughtEvCaught) stopOk = CaughtStopPod(w, &state, &stopped);   /* M2: the roll's own stop of its watcher */
        unsigned long long r = 0;
        const int ok = CaughtCallAlertPod((CaughtAlertFn)(base + (uintptr_t)kCaughtAlertRva), dlg, t, (unsigned int)in.ev, 0, &r);
        int memOk = -1;
        if (ok && in.ev == coopcrime::kCaughtEvCaught && kCaughtMemRva != 0) memOk = CaughtCallMemPod((CaughtMemFn)(base + (uintptr_t)kCaughtMemRva), w, t, 4);
        int crime = -1; float ce = -1.0f;
        CrReadCrime(t, &crime, &ce);
        if (!ok) ++g_caughtOwn[4];
        else if ((r & 0xFF) != 0) ++g_caughtOwn[2];
        else ++g_caughtOwn[3];
        std::sprintf(b, "[CRIME] caught <- watcher uid=%u thief uid=%u ev=0x%X from=%u event 0x%X %s answer=%d stop[ok,state,stopped]=%d,%d,%d memoryNote=%d thiefCrimeNow=%d/%.1f (our watcher, the engine's own entries)",
                     in.watcherUid, in.thiefUid, (unsigned int)in.ev, in.fromPeer, (unsigned int)in.ev,
                     !ok ? "FAULTED" : ((r & 0xFF) != 0 ? "raised" : "raised - REFUSED by the engine"), (int)(r & 0xFF), stopOk, state, stopped,
                     memOk, crime, CrClampForLog(ce));
        if (ok) DebugLog(std::string(b)); else ErrorLog(std::string(b));
    }
}

std::string CaughtReportText()
{
    char b[400];
    std::sprintf(b, " caughtForward[hook,heldBack,sent,sendFailed,noRoute,memMissed,crimeNone,offMain,gone,droppedLoading]=%ld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " caughtOwner[recv,refused,raised,engineRefused,faulted,overflow,notOwnerNow]=%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_caughtHook, g_caughtFwd[0], g_caughtFwd[1], g_caughtFwd[2], g_caughtFwd[3], g_caughtFwd[4], g_caughtFwd[5], (long long)g_caughtOffMain,
                 g_caughtFwd[6], g_caughtFwd[7], g_caughtOwn[0], g_caughtOwn[1], g_caughtOwn[2], g_caughtOwn[3], g_caughtOwn[4], g_caughtOwn[5], g_caughtOwn[6]);
    return std::string(b);
}

} // namespace

// PROBE-START: P158
/* PROBE P158 (T-921): the crime state and bounty list of `c` as text, by the POD reads BountySample already makes (MAIN THREAD).
   "<crime>/<expiry>[<list>]", '?' for a part that faulted. Reads only; nothing is sent or written. */
std::string P158CrimeText(const void* c)
{
    if (c == 0) return "-";
    char b[64];
    int cr = 0; float ce = 0.0f;
    if (CrReadCrime(c, &cr, &ce)) std::sprintf(b, "%d/%.1f", cr, CrClampForLog(ce));
    else std::sprintf(b, "?");
    RawBounty raw[kRawBountyMax];
    const int n = BmRead(c, raw);
    if (n < 0) return std::string(b) + "[?]";
    coopbounty::BountyList l;
    BmToList(raw, n, &l);
    return std::string(b) + "[" + BountyListText(l) + "]";
}
// PROBE-END: P158

/* T-1442 step 5 (F1453 design A): ONE per-character decision for both roads - the event drain (the first main-thread frame after
   setCrime; the time left may still be unwritten for an AI-worker caller, see CrimeEvNote) and the
   sampler (every 15 frames, the backstop). MAIN THREAD. Reads the fields live and updates the same last-sent record (g_last: sent,
   sampledExpiry), so whichever road sends first, the other then finds nothing new. Every rule is the sampler's own: not mine or not
   announced forgets the record (H030); a crime against a PLAYER is held back (decision 4) and one already sent is cleared; the 1 s
   re-set rule (CrimeShouldSend). A failed send leaves the record, so the next look sends it again. */
enum { kCrFromEvent = 0, kCrFromSampler = 1 };
/* kCrOutFailed: a send of a new crime / victim / faction that failed - the record keeps the old state, so the next look sends it;
   kCrOutFailedReSet: a failed send of a re-set only (the time back up) - the sampled time has moved; the next look sends it while
   the time left is still over 1 s above the last SENT one (CrimeShouldSend, fix attempt 2), else a link reset or the next change */
enum { kCrOutUnchanged = 0, kCrOutSent = 1, kCrOutGone = 2, kCrOutNotMine = 3, kCrOutFailed = 4, kCrOutFailedReSet = 5 };
long long g_crEvOut[6] = { 0, 0, 0, 0, 0, 0 };   /* event drain, by kCrOut* */
long long g_crEvDropped = 0;                  /* notes dropped while the world loads or tears down */
long long g_backstop[coopcrime::kCrimeBackstopKinds] = { 0, 0, 0, 0, 0, 0, 0, 0 };   /* sampler sends, by coopcrime::kCrimeBackstop* */
int g_linkResetPasses = 0;   /* > 0: a record made now is a link reset's (set at a reset; down one at each sampler pass start) */
long long g_crEvLines = 0, g_backstopLines = 0;
const long long kCrEvLineCap = 60, kBackstopLineCap = 60;
long long g_crTsLines = 0;                    /* fix attempt 2: 'time-left set .. noted' lines (one per character per crime) */
const long long kCrTsLineCap = 60;
std::map<unsigned int, int> g_crTsLogged;     /* uid -> the crime its time-left line was printed for */

static void CrimeLinkGenCheck()
{
    const long gen = StorePeerEpoch();   /* M11 C2: moves at every arrival and every old-link down (two games: exactly as the link generation) */
    if (gen != g_lastLinkGen) { g_last.clear(); g_bKnown.clear(); CopyForget(); if (g_lastLinkGen != -1) { StoreArrivalNoteServed(kArrServeCrime); g_linkResetPasses = 2; } g_lastLinkGen = gen; }   /* review-crime3 4; crime5: copies are re-made; T-1442 step 5: the rest of this pass and the next are 'linkReset' */
}

static int CrimeDecideSend(unsigned int uid, int from, coopcrime::CrimeState* sentState)
{
    if (!net::IsUidMine(uid) || !AnnouncedToPeerQuiet(uid)) { g_last.erase(uid); return kCrOutNotMine; }   // H030; a re-announce sends afresh
    ::Character* c = FindSpawned(uid);
    if (c == 0) return kCrOutGone;
    int crime = 0; void* fac = 0; unsigned int hand5[5]; float expiry = 0.0f;
    if (!CrReadOwner(c, &crime, &fac, hand5, &expiry)) return kCrOutGone;
    std::map<unsigned int, Last>::iterator it = g_last.find(uid);
    bool created = false;
    if (it == g_last.end())
    {
        Last fresh; fresh.sampledExpiry = 0.0f; std::memset(fresh.hand5, 0, sizeof(fresh.hand5)); fresh.victimUid = 0; fresh.victimIsPlayer = false;
        fresh.origin = g_linkResetPasses > 0 ? coopcrime::kCrimeOriginLinkReset : coopcrime::kCrimeOriginFresh;
        fresh.failedSend = false; fresh.evSent = false; fresh.evSawNoTime = false; fresh.evCrime = 0; std::memset(fresh.evHand5, 0, sizeof(fresh.evHand5)); fresh.evFac = 0;
        it = g_last.insert(std::make_pair(uid, fresh)).first;
        created = true;   /* this look is the record's first: only its send can be 'fresh' / 'linkReset' */
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
    const bool liveRunOut = (crime == 0 || exp <= 0.0f);   /* the live fields hold no crime, or its time has run out */
    const bool onlyTime = cur.crime != 0 && cur.crime == l.sent.crime && cur.victimUid == l.sent.victimUid && cur.factionSid == l.sent.factionSid;
    coopcrime::CrimeBackstopIn bi;
    bi.sentCrime = l.sent.crime; bi.curCrime = cur.crime; bi.liveRunOut = liveRunOut; bi.onlyTime = onlyTime; bi.firstLook = created;
    bi.origin = l.origin; bi.pendingNote = false; bi.evSent = l.evSent; bi.retryAfterFailed = l.failedSend;
    bi.evSawSameNoTime = l.evSawNoTime && crime != 0 && crime == l.evCrime && fac == l.evFac && SameHand(l.evHand5, hand5);
    if (from == kCrFromEvent)
    {   /* what the drain read, for the sampler's why: a crime with no time left yet (its caller writes the time after setCrime) */
        l.evSawNoTime = crime > 0 && crime <= coopcrime::kCrimeMaxEnum && liveRunOut;
        if (l.evSawNoTime) { l.evCrime = crime; l.evFac = fac; std::memcpy(l.evHand5, hand5, sizeof(l.evHand5)); }
    }
    else { l.evSent = false; l.evSawNoTime = false; }   /* the sampler's look closes the drain's window */
    if (sentState != 0) *sentState = cur;
    if (!coopcrime::CrimeDecide(l.sent, &l.sampledExpiry, cur)) return kCrOutUnchanged;
    if (!net::SendCrime(uid, cur)) { ++g_sendFailed; l.failedSend = true; return onlyTime ? kCrOutFailedReSet : kCrOutFailed; }
    l.sent = cur;
    l.failedSend = false;   /* bi above keeps what held before this send */
    if (from == kCrFromEvent) l.evSent = true;
    ++g_sent;
    if (from == kCrFromSampler)
    {
        if (g_crEvLockInit != 0)   /* noted and not yet drained: the drain would have sent it at its next frame */
        {
            ::EnterCriticalSection(&g_crEvLock);
            bi.pendingNote = coopcrime::CrimeEvHas(&g_crEv, c);
            ::LeaveCriticalSection(&g_crEvLock);
        }
        const int why = coopcrime::CrimeBackstopWhy(bi);
        ++g_backstop[why];
        if (++g_backstopLines <= kBackstopLineCap)
        {
            char b[200];
            std::sprintf(b, "[CRIME] backstop -> uid=%u crime=%d expiry=%.1f (%s) (line %lld of %lld)", uid, cur.crime, cur.expiry,
                         coopcrime::CrimeBackstopWord(why), g_backstopLines, kBackstopLineCap);
            DebugLog(std::string(b));
        }
    }
    return kCrOutSent;
}

void CrimeEventDrainTick()
{
    if (g_crEv.n == 0 || g_crEvLockInit == 0) return;   /* the every-frame fast path: one load */
    const bool blocked = EngineWritesBlocked();
    const void* got[kCrimeMaxPerTick];
    bool tsHas[kCrimeMaxPerTick]; float tsOld[kCrimeMaxPerTick], tsNew[kCrimeMaxPerTick];
    int n = 0;
    ::EnterCriticalSection(&g_crEvLock);
    if (blocked) { g_crEvDropped += (long long)coopcrime::CrimeEvReset(&g_crEv); g_crTsN = 0; }   /* the world is going: its characters with it */
    else n = coopcrime::CrimeEvTake(&g_crEv, got, kCrimeMaxPerTick);   /* at most the sampler's per-pass send cap; the rest wait a frame */
    for (int j = 0; j < n; ++j)   /* fix attempt 2: a taken character's time-left record leaves the list with its note */
    {
        tsHas[j] = false; tsOld[j] = 0.0f; tsNew[j] = 0.0f;
        for (int i = 0; i < g_crTsN; ++i)
            if (g_crTsRec[i].who == got[j]) { tsHas[j] = true; tsOld[j] = g_crTsRec[i].oldT; tsNew[j] = g_crTsRec[i].newT; g_crTsRec[i] = g_crTsRec[g_crTsN - 1]; --g_crTsN; break; }
    }
    ::LeaveCriticalSection(&g_crEvLock);
    if (n == 0) return;
    CrimeLinkGenCheck();   /* review-crime3 4: a new link forgets what was sent - before this decides, as in the sampler */
    for (int j = 0; j < n; ++j)
    {
        const unsigned int uid = FindSpawnedUid(got[j]);
        if (uid == 0) { ++g_crEvOut[kCrOutGone]; continue; }   /* not a character this game registers (or gone since the note) */
        if (tsHas[j])
        {   /* fix attempt 2: the first time-left set per character per crime gets a line (the event line follows when it is sent) */
            int lc = 0; float le = 0.0f;
            if (!CrReadCrime(got[j], &lc, &le)) lc = -1;
            std::map<unsigned int, int>::iterator tl = g_crTsLogged.find(uid);
            if ((tl == g_crTsLogged.end() || tl->second != lc) && g_crTsLines < kCrTsLineCap)
            {
                g_crTsLogged[uid] = lc;
                ++g_crTsLines;
                char b[200];
                std::sprintf(b, "[CRIME] time-left set uid=%u %.1f -> %.1f noted crime=%d mine=%d (line %lld of %lld)", uid, CrClampForLog(tsOld[j]),
                             CrClampForLog(tsNew[j]), lc, net::IsUidMine(uid) ? 1 : 0, g_crTsLines, kCrTsLineCap);
                DebugLog(std::string(b));
            }
        }
        coopcrime::CrimeState cur;
        const int out = CrimeDecideSend(uid, kCrFromEvent, &cur);
        ++g_crEvOut[out];
        if (out != kCrOutSent && out != kCrOutFailed && out != kCrOutFailedReSet) continue;   /* gone / notMine / unchanged: counted only */
        if (++g_crEvLines <= kCrEvLineCap)
        {
            char b[240];
            std::sprintf(b, "[CRIME] event -> uid=%u crime=%d expiry=%.1f %s (line %lld of %lld)", uid, cur.crime, cur.expiry,
                         out == kCrOutSent ? "sent"
                             : (out == kCrOutFailed ? "send FAILED (the record keeps the old state: the next look sends it)"
                                                    : "send FAILED (a re-set only: the next look sends it while the time is over 1 s above the last sent)"),
                         g_crEvLines, kCrEvLineCap);
            DebugLog(std::string(b));
        }
    }
}

void CrimeTick()
{
    if (EngineWritesBlocked())
    {
        CaughtPendFlush(true);   /* T-1131 (review fold 1 M1): a held event 8 is dropped with a line while the world loads or tears down */
        ++g_tickBlocked; g_bOut.clear(); g_crimePeer = 0;   /* the world is being torn down or loaded: its factions are going */
        g_sightLawLogged = false;
        if (g_sightCells != 0)   /* sight1; T-356: every cell */
            for (int k = 0; k < coopcrime::kSightCellCap; ++k) if (g_sightCells->peer[k] != 0) g_sightCells->peer[k] = 0;
        g_sightHeld = 0;
        if (g_trespassCells != 0)   /* the trespass test's cells */
            for (int k = 0; k < coopcrime::kSightCellCap; ++k) if (g_trespassCells->peer[k] != 0) g_trespassCells->peer[k] = 0;
        CopyForget();   /* kind 1: the world is going - its copies with it */
        if (g_crEvLockInit != 0) { ::EnterCriticalSection(&g_crEvLock); g_crEvDropped += (long long)coopcrime::CrimeEvReset(&g_crEv); g_crTsN = 0; ::LeaveCriticalSection(&g_crEvLock); }   /* T-1442 step 5 */
        g_crTsLogged.clear();   /* fix attempt 2: a load re-makes every character */
        /* review-crime5 7: a load re-makes every character - both sides start over */
        if (!g_bWasBlocked) { g_bSent.clear(); g_bKnown.clear(); g_bWasBlocked = true; }
        return;
    }
    CaughtPendFlush(false);   /* T-1131: a held event 8 still open outside a roll (after the teardown check) */
    g_bWasBlocked = false;
    g_crimePeer = (void*)PeerFactionAny();   /* review-pvp1 4: MAIN THREAD, world loaded - a save's coop-peer counts before the first SPAWN */
    if (g_sightCells != 0 || g_trespassCells != 0)
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
        {
            if (g_sightCells != 0 && g_sightCells->peer[k] != want[k]) g_sightCells->peer[k] = (void*)want[k];
            if (g_trespassCells != 0 && g_trespassCells->peer[k] != want[k]) g_trespassCells->peer[k] = (void*)want[k];   /* the trespass test: the same factions */
        }
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
    CrimeLinkGenCheck();   /* review-crime3 4: a new link forgets what was sent - shared with the event drain (T-1442 step 5) */
    CopyWriteDrain();   /* kind 1: crimes this game's witnesses recorded on copies, to their owners */
    if (g_scanPos >= g_scan.size())
    {
        net::OwnedUidsSnapshot(&g_scan);
        g_scanPos = 0;
        if (g_linkResetPasses > 0) --g_linkResetPasses;   /* T-1442 step 5: a link reset's records - the pass it struck in and the next */
        ForgetUnowned();
        BountyForget();
        if (g_scan.empty()) return;
    }
    /* T-1442 step 5: the sampler, unchanged in cadence, is the BACKSTOP - a crime set through setCrime goes out at the first
       main-thread frame after it (CrimeEventDrainTick); each send this makes is counted by why (crimewire.h CrimeBackstopWhy) */
    int scanned = 0, sent = 0;
    while (g_scanPos < g_scan.size() && scanned < kCrimeScanPerTick && sent < kCrimeMaxPerTick)
    {
        const unsigned int uid = g_scan[g_scanPos++];
        ++scanned;
        const int out = CrimeDecideSend(uid, kCrFromSampler, 0);
        if (out == kCrOutFailed || out == kCrOutFailedReSet) { --g_scanPos; return; }   // link down: retry this uid next tick
        if (out == kCrOutSent) ++sent;
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

void ApplyRemoteCopyWrite(unsigned int uid, const coopcrime::CrimeState& s, unsigned int fromPeer)
{
    ++g_ownerK1Recv;
    /* only for a character THIS game drives; any game holding a copy may send it */
    if (!net::IsUidMine(uid)) { ++g_ownerK1Refused; ++g_ownerK1NotMine; return; }
    if (g_k1In.size() >= kK1InCap) { ++g_ownerK1Overflow; return; }
    K1In in; in.uid = uid; in.s = s; in.fromPeer = fromPeer;
    g_k1In.push_back(in);
}

/* T-1131 kind 3 on the watcher's owner (MAIN THREAD, the session drain): refused with a line when this game does not run the watcher
   now (no retry); else the thief's crime is queued as the sender's kind-0 state (the owner test there), and the raise waits for the
   K2 safe point after that write (CaughtRaiseDrain). */
void ApplyRemoteCaught(unsigned int thiefUid, const coopcrime::CrimeState& s, const coopcrime::CrimeCaught& c, unsigned int fromPeer)
{
    ++g_caughtOwn[0];
    const bool wMine = net::IsUidMine(c.watcherUid);
    const int d = coopcrime::CaughtOwnerDecide(wMine, wMine && FindSpawned(c.watcherUid) != 0, net::UidOwnedByPeer(thiefUid, fromPeer),
                                               FindSpawned(thiefUid) != 0, c.ev);
    char b[400];
    if (d == coopcrime::kCaughtRaise && g_caughtIn.size() >= kCaughtInCap) ++g_caughtOwn[5];
    if (d == coopcrime::kCaughtNotOwner) ++g_caughtOwn[6];   /* L3 (review fold 1): the watcher changed owner in flight - logged, no retry */
    if (d != coopcrime::kCaughtRaise || g_caughtIn.size() >= kCaughtInCap)
    {
        ++g_caughtOwn[1];
        std::sprintf(b, "[CRIME] caught <- watcher uid=%u thief uid=%u ev=0x%X from=%u REFUSED: %s (no retry)", c.watcherUid, thiefUid,
                     (unsigned int)c.ev, fromPeer, d == coopcrime::kCaughtNotOwner ? "this game does not run the watcher now (it changed owner in flight?) - neither game raises this catch"
                     : (d != coopcrime::kCaughtRaise ? coopcrime::CaughtOwnerWhy(d) : "the queue is full"));
        DebugLog(std::string(b));
        return;
    }
    std::sprintf(b, "[CRIME] caught <- watcher uid=%u thief uid=%u ev=0x%X from=%u crime=%d expiry=%.1f received: the thief's crime first, the event at the safe point",
                 c.watcherUid, thiefUid, (unsigned int)c.ev, fromPeer, s.crime, CrClampForLog(s.expiry));
    DebugLog(std::string(b));
    if (s.crime != 0) ApplyRemoteCrime(thiefUid, s, fromPeer);
    CaughtIn in; in.thiefUid = thiefUid; in.watcherUid = c.watcherUid; in.fromPeer = fromPeer; in.ev = c.ev;
    g_caughtIn.push_back(in);
}

void CrimeNoteWitnessEvent(int what, const void* who)
{
    int i = -1;
    if (what == 0x29) i = 0; else if (what == 0x2A) i = 1; else if (what == 8) i = 2; else if (what == 0x11) i = 3;
    if (i < 0) return;
    ::InterlockedIncrement64(&g_witnessEv[i]);
    /* `who` is the character the event is about (the offender); counted when it is another player's character's copy */
    ::Faction* of = 0;
    if (who != 0 && CrFactionOf((::Character*)who, &of) && of != 0 && ((void*)of == g_crimePeer || IsPeerFaction(of)))
        ::InterlockedIncrement64(&g_witnessEvRepl[i]);
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
static void CrimeApplyDrainBody()
{
    /* crime5: bounties - read and written only here, where the worker's BountyManager::update is paused */
    if (!EngineWritesBlocked()) { BountyTestClearDrain(); BountyTestSetDrain(); BountyApply(); BountySample(); }   /* the levers first */
    else if (!g_bIn.empty()) { g_bIn.clear(); }
    CrimeHeldDrain();   /* M7b slice 4 fold 1 (F5) */
    OwnerCopyWriteDrain();     /* kind 1: crimes other games' witnesses recorded on their copies of this game's characters */
    RelationsEvApplyDrain();   /* kind 2: standing events against this game's player raised on another game */
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
        std::map<unsigned int, int>::const_iterator pk = g_ownerKnown.find(uid);
        const int prevKnown = pk != g_ownerKnown.end() ? pk->second : 0;   /* the owner's previous state for this copy */
        g_ownerKnown[uid] = s.crime;   /* kind 1: what the owner holds for this copy */
        { OwnerAt at; at.expiry = s.expiry; at.ms = ::GetTickCount(); g_ownerKnownAt[uid] = at; }   /* and how long it has left */
        if (s.crime == 0)
        {
            if (CopyClearKept(uid, before, s)) continue;   /* a crime recorded here, not yet in the owner's state */
            const hand nh(0, 0, (itemType)0, 0, 0);
            g_crApplyingThread = ::GetCurrentThreadId();
            const int ok = CrCallSet(setCrime, c, &nh);
            g_crApplyingThread = 0;
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
        {   /* kind 1: the owner's state now holds a crime recorded here - its mark ends */
            std::map<unsigned int, CopyMark>::iterator mk = g_copyMark.find(uid);
            if (mk != g_copyMark.end() && coopcrime::CopyMarkMet(mk->second.crime, s.crime)) g_copyMark.erase(mk);
        }
        g_crApplyingThread = ::GetCurrentThreadId();
        const int ok = CrCallNotify(notify, c, (void*)f, &h, exp, s.crime);
        g_crApplyingThread = 0;
        CrReadCrime(c, &after, &expAfter);
        if (!ok) ++g_faulted;
        else if (after == s.crime) ++g_applied;
        else
        {
            ++g_engineRefused;
            /* the engine kept the crime the copy held: one recorded here, not in the owner's previous state, goes to the owner */
            if (coopcrime::CopyHeldNotOwners(before, after, s.crime, prevKnown)) CopyNoteLive(c, uid, kCapSrcRefused);
        }
        std::sprintf(extra, "%s crime %d->%d expiry %.1f->%.1f factionResolved=%d victimHere=%d (notifyCrimeWitnessed 0x851F40)",
                     ok ? "called" : "FAULTED", before, after, CrClampForLog(expBefore), CrClampForLog(expAfter), f != 0 ? 1 : 0, v != 0 ? 1 : 0);
        CrLog("<-", uid, s, extra);
    }
}

// MAIN THREAD, worker paused: the queued crimes are written first, then (T-1131) each received caught event is raised on our watcher.
void CrimeApplyDrain()
{
    CrimeApplyDrainBody();
    if (EngineWritesBlocked() && !g_caughtIn.empty()) { g_caughtOwn[1] += (long long)g_caughtIn.size(); g_caughtIn.clear(); DebugLog("[CRIME] caught <- dropped: the world is loading or tearing down (no retry)"); }
    CaughtRaiseDrain();
}

void InstallCrime()
{
    if (g_copyCapLockInit == 0) { ::InitializeCriticalSection(&g_copyCapLock); g_copyCapLockInit = 1; }   /* before the detours note a write */
    if (g_crEvLockInit == 0) { ::InitializeCriticalSection(&g_crEvLock); g_crEvLockInit = 1; }   /* T-1442 step 5: before the setCrime detour notes */
    CaughtInstall();   /* T-1131: before the pvp1 hooks' early return */
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
    if (kCrTimeSetRva != 0)   /* T-1442 step 5 fix attempt 2 (F1466): the time-left setter notes its character after the original */
    {
        const coop::HookStatus sts = coop::AddHook((void*)(base + (uintptr_t)kCrTimeSetRva), (void*)&detour_timeSet, (void**)&orig_timeSet);
        g_timeSetHook = (sts == coop::SUCCESS) ? 1 : -1;
    }
    else g_timeSetHook = -1;
    DebugLog(std::string("[CRIME] pvp1 hooks setCrime 0x851DB0: ") + (g_pvpHook == 1 ? "installed" : "FAILED")
             + ", notifyCrimeWitnessed 0x851F40: " + (g_pvpNotifyHook == 1 ? "installed" : "FAILED")
             + " (a player's crime against a player's faction is never recorded)"
             + "; time-left setter 0x851070 (T-1442 step 5 note): " + (g_timeSetHook == 1 ? "installed" : (kCrTimeSetRva == 0 ? "NOT IN THE TABLE" : "FAILED")));
    /* sight1: the senses' player test also passes for every other player's faction (wanted on sight; T-356: one cell each) */
    if (g_sightState == 0)
    {
        if (kCrSightRva == 0) { g_sightState = -1; g_sightWhy = "BountySightTargetTest is not in the address table"; }
        else g_sightState = SightPatch(base + (uintptr_t)kCrSightRva, kSightOrig, &g_sightCells, &g_sightWhy, false) ? 1 : -1;
        if (g_sightState == 1) DebugLog("[CRIME] sight1 patched the senses' player test 0x85635D (the other players' characters are recognised as wanted on sight)");
        else ErrorLog(std::string("[CRIME] sight1 NOT patched: ") + g_sightWhy + " - the other players' characters are recognised only by the guards' AI search (F899)");
    }
    /* the trespass test also passes for every other player's faction (a copy of their character can be caught trespassing) */
    if (g_trespassState == 0)
    {
        if (kCrTrespassRva == 0) { g_trespassState = -1; g_trespassWhy = "BountyTrespassTargetTest is not in the address table"; }
        else g_trespassState = SightPatch(base + (uintptr_t)kCrTrespassRva, kTrespassOrig, &g_trespassCells, &g_trespassWhy, true) ? 1 : -1;
        if (g_trespassState == 1) DebugLog("[CRIME] trespass test patched 0x858D13 (the other players' characters can be caught trespassing here, by lookers not of this game's player faction)");
        else ErrorLog(std::string("[CRIME] trespass test NOT patched: ") + g_trespassWhy + " - the other players' characters are never caught trespassing on this game");
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
    char cw[1600];
    std::sprintf(cw, " copySideWrites[queued,sent,applied,refused]=%lld,%lld,%lld,%lld"
                 " copySide[captured,capOverflow,notRecorded,already,playerVictim,gone,dropped,sendRetry,sidTooLong,marks,waiting]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%u,%u"
                 " ownerApplied[kind1,kind2]=%lld,%lld ownerKind1[recv,notMine,playerVictim,bad,engine,noChar,noFaction,overflow,dropped]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                 " clearsKeptLocal=%lld localCrimeEnded=%lld reSent=%lld trespassSite[state,hits,lookerRefused]=%ld,%lld,%lld witnessEvents[0x29,0x2A,8,0x11]=%lld,%lld,%lld,%lld"
                 " witnessEventsAboutReplicated[0x29,0x2A,8,0x11]=%lld,%lld,%lld,%lld"
                 " trespassSetCrime[calls,recorded,onOtherPlayer,recordedOnOtherPlayer]=%lld,%lld,%lld,%lld copyHeldNotes[ownerRefused,bountyAdded]=%lld,%lld",
                 g_copyQueued, g_copySent, g_ownerK1Applied, g_ownerK1Refused,
                 (long long)g_copyCaptured, (long long)g_copyCapOverflow, g_copyNotRecorded, g_copyAlready, g_copyPlayerVictim, g_copyGone,
                 g_copyDropped, g_copySendRetry, g_copySidTooLong, (unsigned int)g_copyMark.size(), (unsigned int)g_copyOut.size(),
                 g_ownerK1Applied, RelationsEvAppliedCount(), g_ownerK1Recv, g_ownerK1NotMine, g_ownerK1PlayerVictim, g_ownerK1Bad,
                 g_ownerK1Engine, g_ownerK1NoChar, g_ownerK1NoFaction, g_ownerK1Overflow, g_ownerK1Dropped,
                 g_clearsKeptLocal, g_localCrimeEnded, g_copyReSent, (long)g_trespassState, g_trespassCells != 0 ? (long long)g_trespassCells->hits : -1LL,
                 g_trespassCells != 0 ? (long long)g_trespassCells->lookerRefused : -1LL,
                 (long long)g_witnessEv[0], (long long)g_witnessEv[1], (long long)g_witnessEv[2], (long long)g_witnessEv[3],
                 (long long)g_witnessEvRepl[0], (long long)g_witnessEvRepl[1], (long long)g_witnessEvRepl[2], (long long)g_witnessEvRepl[3],
                 (long long)g_trespassSet[0], (long long)g_trespassSet[1], (long long)g_trespassSet[2], (long long)g_trespassSet[3],
                 g_copyLiveNote[0], g_copyLiveNote[1]);
    char rp[160];   /* T-1293 */
    std::sprintf(rp, " copyCrimeInReplaySkipped=%lld copyReplayHeld[skipped,now]=%lld,%u", (long long)g_copyCrimeInReplaySkipped,
                 g_copyReplayHeldSkipped, (unsigned int)g_copyReplayHeld.size());
    char ev[768];   /* T-1442 step 5; fix attempt 2 (F1466): notes by who (the setter's and notify's after-notes), lost time-left records, the setter's hook */
    std::sprintf(ev, " crimeEvent[noted,sent,unchanged,gone,notMine,overflow,already,failed,failedReSet,dropped,notedSetCrime,notedNotifyAfter,notedTimeSet,timeSetLost,timeSetHook]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%ld"
                 " backstop[expiry,heldBack,pendingNote,lateExpiry,linkReset,fresh,missed,retryAfterFailedSend]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                 g_crEv.noted, g_crEvOut[kCrOutSent], g_crEvOut[kCrOutUnchanged], g_crEvOut[kCrOutGone], g_crEvOut[kCrOutNotMine], g_crEv.overflow,
                 g_crEv.already, g_crEvOut[kCrOutFailed], g_crEvOut[kCrOutFailedReSet], g_crEvDropped,
                 g_crEvNotedBy[kCrNoteSetCrime], g_crEvNotedBy[kCrNoteNotifyAfter], g_crEvNotedBy[kCrNoteTimeSet], g_crTsLost, (long)g_timeSetHook,
                 g_backstop[coopcrime::kCrimeBackstopExpiry], g_backstop[coopcrime::kCrimeBackstopHeldBack], g_backstop[coopcrime::kCrimeBackstopPendingNote],
                 g_backstop[coopcrime::kCrimeBackstopLateExpiry], g_backstop[coopcrime::kCrimeBackstopLinkReset],
                 g_backstop[coopcrime::kCrimeBackstopFresh], g_backstop[coopcrime::kCrimeBackstopMissed],
                 g_backstop[coopcrime::kCrimeBackstopRetryAfterFailedSend]);
    DebugLog(std::string(b) + ev + " " + PeerFactionCopies() + pv + hv + cw + rp + RelationsEvText() + CaughtReportText());
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
