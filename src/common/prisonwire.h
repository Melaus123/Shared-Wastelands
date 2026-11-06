/* src/common/prisonwire.h - arrest2 (docs/design-arrest.md 3 "arrest2"): A GUARD ON ONE GAME CAGED THE OTHER GAME'S CHARACTER.
 *
 * A guard's task puts a body in a cage on the guard's game (Task_PutInSomething 0x358610 -> setPrisonMode 0x3305C0). When
 * that body is a COPY there, the character it stands for is driven by the other game, which must cage its own character
 * in its own copy of the same cage. The guard's game sends:
 *
 *   uid u32 | kind u8 | len u8 (0..kPrisonMaxKey) + cage key | len u8 (0..kPrisonMaxKey) + outer key
 *   | kPrisonIn only (P11 f3, protocol 101): u8 sentence - the ARRESTING game's verdict (0 none, 1 sentence, 2 unknown: the
 *   owner reads its own bounty)
 *
 * kind kPrisonIn (arrest2, guard's game -> owner): the cage key is required. arrest3 (session 57): kPrisonRelease (guard's
 * game -> owner: this engine let its copy out - a guard's release), kPrisonRefused (owner -> guard's game: the owner could
 * not cage its character, so the guard's game takes its copy out again); both carry no keys (empty). par6 fold (session 80):
 * kPrisonDeath (copy's game -> owner: a player's one-shot kill of the copy, refused there by the copy death gate - the owner
 * kills its own character if it is alive); its cage key names the engine caller ("deathCheck" / "unequip"), no outer key.
 *
 * kind kPrisonBedIn (rescuer's game -> owner): the rescuer's game put its knocked-out COPY of `uid` in a bed (Task_PutInSomething
 * 0x358610, bed branch: setBedMode 0x32E2B0, then the bed spot and a teleport 0x5C9BF0); the owner puts its own character in its
 * own copy of the same bed. The cage key field carries the BED's key (required), the outer key its building's, and after the
 * outer key come three floats (x, y, z: the copy's position after the put-down, 12 bytes, each finite). kind kPrisonBedRefused
 * (owner -> rescuer's game): the owner could not do it, so the rescuer's game takes its copy out of the bed. Its cage key names
 * the refused bed (empty: not known), no outer key, then u8 permanent: 1 when the bed itself (full, not a bed) or the
 * character's death refused it - the rescuer does not ask about that bed again; 0 for a refusal that may pass (still carried,
 * bed not found yet, awake), after which the next put-down asks again.
 *
 * kinds kPrisonShackleLock / kPrisonCageLock (P42, protocol 137): the lock of a prisoner's restraint - the shackles it wears
 * (LockedArmour +0x2F0) or the cage it sits in (UseableStuff +0x438), both a DoorLock. The prisoner's OWNER decides both. From the
 * owner (to every other game) it is the owner's word: the copy's restraint takes those bits. From a copy's game (to the owner) it
 * is a request: the copy's restraint was opened there (a rescuer picked it); the owner opens its own only when its own is closed.
 * The cage key field carries the shackles' inventory section ("boots" / "armour") or the cage's POSE key (required), the outer
 * key nothing (shackles) or the cage's building key; after the outer key: u8 flags (kLockFlagLocked | kLockFlagBroken, no other
 * bit) and i32 lock level (0..kLockLevelMax).
 *
 * kind kPrisonSlaveAsk (copy's game -> owner): this game's engine changed the slave state of its COPY of `uid`
 * (StateBroadcastData::setSlaveState 0x5A3EB0, refused on the copy). No keys; after the outer key: u8 from (the copy's state when
 * the change was made) and u8 want (the state the engine set), each 0..3 (SlaveStateEnum) and different. The owner applies it to its
 * own character through the same setter only when the sender runs the character's area (slavewire.h SlaveAskOwnerDecide).
 *
 * kind kPrisonFreed (copy's game -> owner): this game's engine raised the unlock event 0xD (EventTriggerEnum 13, the event the
 * release task and the pick task send) on its COPY of the prisoner `uid`, with this game's own character `freerUid` as the freer.
 * The owner raises the same event on its own prisoner with its copy of that character as the freer, so the owner's game picks the
 * prisoner's line from the prisoner's real state. kind kPrisonJoinAsk (owner -> the freer's game): the owner's prisoner `uid`
 * reached its join line (act 18) with this game's copy of the freer `freerUid` as the one it joins; the owner did not run the line
 * and asks the freer's game to hire the prisoner through the hire handshake (MSG_HIRE REQ to the owner). kind kPrisonJoinNo (the
 * freer's game -> owner): that game did not ask (u8 reason, kJoinNo*). All three carry no keys (empty); after the outer key: u32
 * freerUid (not 0, not `uid`); for kPrisonFreed one more byte, the locks the freer's game read open on its copy as it sent the
 * message (kFreedLock* bits: 1 shackles, 2 cage); for kPrisonJoinNo one more byte, the reason (1..kJoinNoMax).
 *
 * A kPrisonRelease whose cage key is kPrisonBailKey ("@bail") is a bail paid on the sender's game
 * (CharacterTrading_PrisonerBailout's confirm, 1.0.68 0x6AE170): the owner releases its own character as that confirm does - every
 * bounty cleared, no pardon, slave state 0, the walk-out order (PrisonReleaseModeOf below). A game that does not know the marker
 * looks it up as a faction, finds none and releases with the cage's faction.
 *
 * The keys are the POSE bed keys (spawn.cpp PoseBedKeyFromHand): the cage's own P7n building key and, when the cage is
 * interior furniture, the key of the building whose layout made it (BED1). Pure: no engine memory, no Windows; the offline
 * suite hits the same bytes. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace cooprison {

const unsigned char kPrisonIn = 1;       /* the sender's guard caged its copy of `uid` */
const unsigned char kPrisonRelease = 2;  /* arrest3: the sender's engine released its copy of `uid` from its cage */
const unsigned char kPrisonRefused = 3;  /* arrest3: the owner could not cage `uid`; the guard's game frees its copy */
const unsigned char kPrisonDeath = 4;    /* par6 fold (review-par6 #1, session 80): a DEATH REQUEST, copy's game -> owner - a player's
                                            one-shot kill of the copy (Inventory::deathCheck 0x75DCA0 / Character::unequipItem
                                            0x5DB880) was refused by the copy death gate; the owner kills its own character through
                                            Character::declareDead only if it is alive. The cage key carries the caller's name. */
const unsigned char kPrisonBedIn = 5;      /* the sender's game put its knocked-out copy of `uid` in a bed; the owner follows */
const unsigned char kPrisonBedRefused = 6; /* the owner could not put `uid` in that bed; the sender takes its copy out */
const unsigned char kPrisonShackleLock = 7; /* P42: the lock of the shackles `uid` wears - the owner's word, or a copy's game's request */
const unsigned char kPrisonCageLock = 8;    /* P42: the lock of the cage `uid` sits in - the owner's word, or a copy's game's request */
const unsigned char kPrisonSlaveAsk = 9;    /* a slave-state change this game's engine made on its copy of `uid`, to the owner */
const unsigned char kPrisonFreed = 10;     /* a copy's unlock event 0xD with this game's character as the freer, to the prisoner's owner */
const unsigned char kPrisonJoinAsk = 11;   /* the owner's prisoner joins the freer: the freer's game is asked to hire it */
const unsigned char kPrisonJoinNo = 12;    /* the freer's game did not ask for the hire (reason kJoinNo*) */
const int kPrisonSlaveStateMax = 3;         /* SlaveStateEnum: 0 NOT_SLAVE, 1 IS_SLAVE, 2 ESCAPING_SLAVE, 3 EX_SLAVE (slavewire.h) */
const unsigned int  kPrisonMaxKey = 48;  /* as coopstate::kPoseStrMax */

const int kPrisonDecodeOk       = 0;
const int kPrisonDecodeTooShort = 1;
const int kPrisonDecodeBadKind  = 2;
const int kPrisonDecodeBadKey   = 3;   /* kPrisonIn / kPrisonBedIn with an empty cage key, or a key longer than kPrisonMaxKey */
const int kPrisonDecodeBadPos   = 5;   /* kPrisonBedIn with a position that is not a finite number */
const int kPrisonDecodeBadFlag  = 6;   /* kPrisonBedRefused with a permanent byte other than 0 or 1 */
const int kPrisonDecodeBadLock  = 7;   /* kPrisonShackleLock / kPrisonCageLock with an unknown flag bit or a level outside 0..100 */
const int kPrisonDecodeBadSlave = 8;   /* kPrisonSlaveAsk with a state outside 0..3, or from == want */
const int kPrisonDecodeBadFreer = 9;   /* kPrisonFreed / kPrisonJoinAsk / kPrisonJoinNo with a freer uid of 0 or equal to the prisoner's */
const int kPrisonDecodeBadReason = 10; /* kPrisonJoinNo with a reason outside 1..kJoinNoMax */
const int kPrisonDecodeBadLocks = 11;  /* kPrisonFreed with a lock bit other than kFreedLockShackles / kFreedLockCage */
/* kPrisonFreed's lock byte: the restraints the freer's game read open on its copy as it sent the message */
const unsigned char kFreedLockShackles = 1, kFreedLockCage = 2, kFreedLocksAll = 3;
/* kPrisonJoinNo reasons (the freer's game: JoinAskDecide below, or the hire road's own refusal to ask) */
const unsigned char kJoinNoNone = 0;
const unsigned char kJoinNoFreerNotOurs = 1;   /* the freer is not a character this game runs */
const unsigned char kJoinNoNoCopy = 2;         /* this game has no copy of the prisoner */
const unsigned char kJoinNoPrisonerOurs = 3;   /* this game runs the prisoner itself */
const unsigned char kJoinNoNotAsked = 4;       /* the hire road refused to ask (a hire already pending, no link, another player's person) */
const unsigned char kJoinNoBlocked = 5;        /* the world was loading or tearing down */
const unsigned char kJoinNoMax = 5;
inline bool PrisonCarriesFreer(unsigned char k) { return k == kPrisonFreed || k == kPrisonJoinAsk || k == kPrisonJoinNo; }
inline bool PrisonFreerOk(unsigned int uid, unsigned int freerUid) { return freerUid != 0 && freerUid != uid; }
inline bool PrisonSlaveAskOk(unsigned char from, unsigned char want)
{
    return from <= kPrisonSlaveStateMax && want <= kPrisonSlaveStateMax && from != want;
}

/* P42: a DoorLock as it travels - +0x20 locked, +0x21 broken, +0x00 lockLevel (build/read-locks.md 2.1, Confirmed there) */
const unsigned char kLockFlagLocked = 1;
const unsigned char kLockFlagBroken = 2;
const int kLockLevelMax = 100;   /* the engine clamps every lock level to 0..100 */
inline bool LockBitsOk(unsigned char flags, int level) { return (flags & ~(kLockFlagLocked | kLockFlagBroken)) == 0 && level >= 0 && level <= kLockLevelMax; }
/* the engine's "this lock stops nobody" - Task_PickLock's success test (decomp_359100: not locked, broken, or level < 1) */
inline bool LockOpen(unsigned char flags, int level) { return (flags & kLockFlagLocked) == 0 || (flags & kLockFlagBroken) != 0 || level < 1; }
inline bool PrisonIsLockKind(unsigned char k) { return k == 7 || k == 8; }

struct PrisonMsg
{
    unsigned int uid;
    unsigned char kind;
    std::string cageKey;
    std::string outerKey;
    unsigned char sentence;   /* P11 f3, kPrisonIn only: kPrisonSentence* */
    float pos[3];             /* kPrisonBedIn only: the copy's position after the put-down */
    unsigned char permanent;  /* kPrisonBedRefused only: 1 the bed or a death refused it (not asked again), 0 it may pass */
    unsigned char lockFlags;  /* kPrisonShackleLock / kPrisonCageLock only: kLockFlag* */
    int lockLevel;            /* kPrisonShackleLock / kPrisonCageLock only: 0..kLockLevelMax */
    unsigned char slaveFrom;  /* kPrisonSlaveAsk only: the copy's slave state when its engine changed it */
    unsigned char slaveWant;  /* kPrisonSlaveAsk only: the slave state that engine set */
    unsigned int freerUid;    /* kPrisonFreed / kPrisonJoinAsk / kPrisonJoinNo only: the freeing player's character */
    unsigned char joinNo;     /* kPrisonJoinNo only: kJoinNo* (1..kJoinNoMax) */
    unsigned char freedLocks; /* kPrisonFreed only: kFreedLock* bits */
    PrisonMsg() : uid(0), kind(0), sentence(2), permanent(0), lockFlags(0), lockLevel(0), slaveFrom(0), slaveWant(0), freerUid(0), joinNo(0),
                  freedLocks(0)
    { pos[0] = 0.0f; pos[1] = 0.0f; pos[2] = 0.0f; }
};

/* P11 f3 (review M6): Task_PutInSomething 0x358610 starts a sentence only when GetActualBounty 0x8531B0(victim +0xF0, the cage's
   faction) > 0, and it reads that on the ARRESTING game (decomp_358610.txt:147-148 - Read). kPrisonIn carries that game's verdict;
   the owner follows it. */
const unsigned char kPrisonSentenceNo = 0, kPrisonSentenceYes = 1, kPrisonSentenceUnknown = 2;
const int kPrisonDecodeBadSentence = 4;   /* kPrisonIn with a sentence byte above kPrisonSentenceUnknown */
inline bool PrisonKindOk(unsigned char k)
{
    return k == kPrisonIn || k == kPrisonRelease || k == kPrisonRefused || k == kPrisonDeath || k == kPrisonBedIn || k == kPrisonBedRefused
        || k == kPrisonShackleLock || k == kPrisonCageLock || k == kPrisonSlaveAsk || PrisonCarriesFreer(k);
}
/* a float whose exponent bits are not all set (not an infinity, not a NaN) - read by its bits, whatever the floating-point mode */
inline bool PrisonPosFinite(float f) { unsigned int u; std::memcpy(&u, &f, 4); return (u & 0x7F800000u) != 0x7F800000u; }
inline bool PrisonKeyRequired(unsigned char k) { return k == kPrisonIn || k == kPrisonBedIn || PrisonIsLockKind(k); }

/* false (nothing appended) for a bad kind, an empty (kPrisonIn) or too long cage key, or a too long outer key. A kPrisonRelease
   carries the releaser's faction in the cage key (review-arrest3 M2): its stringID, "@player", or empty when unknown. */

inline bool EncodePrison(std::vector<char>* b, const PrisonMsg& m)
{
    if (b == 0 || !PrisonKindOk(m.kind) || (PrisonKeyRequired(m.kind) && m.cageKey.empty())
        || m.cageKey.size() > kPrisonMaxKey || m.outerKey.size() > kPrisonMaxKey || (m.kind == kPrisonIn && m.sentence > kPrisonSentenceUnknown))
        return false;
    if (m.kind == kPrisonBedIn && !(PrisonPosFinite(m.pos[0]) && PrisonPosFinite(m.pos[1]) && PrisonPosFinite(m.pos[2]))) return false;
    if (m.kind == kPrisonBedRefused && m.permanent > 1) return false;
    if (PrisonIsLockKind(m.kind) && !LockBitsOk(m.lockFlags, m.lockLevel)) return false;
    if (m.kind == kPrisonSlaveAsk && !PrisonSlaveAskOk(m.slaveFrom, m.slaveWant)) return false;
    if (PrisonCarriesFreer(m.kind) && !PrisonFreerOk(m.uid, m.freerUid)) return false;
    if (m.kind == kPrisonJoinNo && (m.joinNo < 1 || m.joinNo > kJoinNoMax)) return false;
    if (m.kind == kPrisonFreed && (m.freedLocks & ~kFreedLocksAll) != 0) return false;
    const size_t at = b->size();
    b->resize(at + 4 + 1 + 1 + m.cageKey.size() + 1 + m.outerKey.size() + (m.kind == kPrisonIn ? 1 : 0) + (m.kind == kPrisonBedIn ? 12 : 0)
              + (m.kind == kPrisonBedRefused ? 1 : 0) + (PrisonIsLockKind(m.kind) ? 5 : 0) + (m.kind == kPrisonSlaveAsk ? 2 : 0)
              + (PrisonCarriesFreer(m.kind) ? 4 : 0) + ((m.kind == kPrisonJoinNo || m.kind == kPrisonFreed) ? 1 : 0));
    char* p = &(*b)[at];
    std::memcpy(p, &m.uid, 4); p += 4;
    *p++ = (char)m.kind;
    *p++ = (char)m.cageKey.size();
    std::memcpy(p, m.cageKey.data(), m.cageKey.size()); p += m.cageKey.size();
    *p++ = (char)m.outerKey.size();
    if (!m.outerKey.empty()) std::memcpy(p, m.outerKey.data(), m.outerKey.size());
    if (m.kind == kPrisonIn) p[m.outerKey.size()] = (char)m.sentence;   /* P11 f3 */
    if (m.kind == kPrisonBedIn) std::memcpy(p + m.outerKey.size(), m.pos, 12);   /* x, y, z after the outer key */
    if (m.kind == kPrisonBedRefused) p[m.outerKey.size()] = (char)m.permanent;
    if (PrisonIsLockKind(m.kind)) { p[m.outerKey.size()] = (char)m.lockFlags; std::memcpy(p + m.outerKey.size() + 1, &m.lockLevel, 4); }
    if (m.kind == kPrisonSlaveAsk) { p[m.outerKey.size()] = (char)m.slaveFrom; p[m.outerKey.size() + 1] = (char)m.slaveWant; }
    if (PrisonCarriesFreer(m.kind)) std::memcpy(p + m.outerKey.size(), &m.freerUid, 4);
    if (m.kind == kPrisonJoinNo) p[m.outerKey.size() + 4] = (char)m.joinNo;
    if (m.kind == kPrisonFreed) p[m.outerKey.size() + 4] = (char)m.freedLocks;
    return true;
}

/* The kind byte of a MSG_PRISON payload read without decoding the rest: true for a death request (kPrisonDeath). */
inline bool PrisonPayloadIsDeath(const char* p, size_t size)
{
    return p != 0 && size >= 5 && (unsigned char)p[4] == kPrisonDeath;
}

/* Every length test is `size - off < n` with off already <= size. */
inline int DecodePrison(const char* p, size_t size, PrisonMsg* out)
{
    if (p == 0 || size < 7) return kPrisonDecodeTooShort;
    PrisonMsg m;
    std::memcpy(&m.uid, p, 4);
    m.kind = (unsigned char)p[4];
    if (!PrisonKindOk(m.kind)) return kPrisonDecodeBadKind;
    size_t off = 5;
    const unsigned int kl = (unsigned char)p[off]; off += 1;
    if ((kl == 0 && PrisonKeyRequired(m.kind)) || kl > kPrisonMaxKey) return kPrisonDecodeBadKey;
    if (size - off < (size_t)kl + 1) return kPrisonDecodeTooShort;
    m.cageKey.assign(p + off, kl); off += kl;
    const unsigned int ol = (unsigned char)p[off]; off += 1;
    if (ol > kPrisonMaxKey) return kPrisonDecodeBadKey;
    if (size - off < (size_t)ol) return kPrisonDecodeTooShort;
    m.outerKey.assign(p + off, ol);
    off += ol;
    if (m.kind == kPrisonIn)   /* P11 f3: the arresting game's sentence verdict */
    {
        if (size - off < 1) return kPrisonDecodeTooShort;
        m.sentence = (unsigned char)p[off];
        if (m.sentence > kPrisonSentenceUnknown) return kPrisonDecodeBadSentence;
    }
    if (m.kind == kPrisonBedIn)   /* the copy's position after the put-down */
    {
        if (size - off < 12) return kPrisonDecodeTooShort;
        std::memcpy(m.pos, p + off, 12);
        if (!(PrisonPosFinite(m.pos[0]) && PrisonPosFinite(m.pos[1]) && PrisonPosFinite(m.pos[2]))) return kPrisonDecodeBadPos;
    }
    if (m.kind == kPrisonBedRefused)   /* whether the refusal is for good */
    {
        if (size - off < 1) return kPrisonDecodeTooShort;
        m.permanent = (unsigned char)p[off];
        if (m.permanent > 1) return kPrisonDecodeBadFlag;
    }
    if (PrisonIsLockKind(m.kind))   /* P42: the lock's flags and level */
    {
        if (size - off < 5) return kPrisonDecodeTooShort;
        m.lockFlags = (unsigned char)p[off];
        std::memcpy(&m.lockLevel, p + off + 1, 4);
        if (!LockBitsOk(m.lockFlags, m.lockLevel)) return kPrisonDecodeBadLock;
    }
    if (m.kind == kPrisonSlaveAsk)   /* the copy's state and the state its engine set */
    {
        if (size - off < 2) return kPrisonDecodeTooShort;
        m.slaveFrom = (unsigned char)p[off];
        m.slaveWant = (unsigned char)p[off + 1];
        if (!PrisonSlaveAskOk(m.slaveFrom, m.slaveWant)) return kPrisonDecodeBadSlave;
    }
    if (PrisonCarriesFreer(m.kind))   /* the freeing player's character, and a refusal's reason */
    {
        if (size - off < 4) return kPrisonDecodeTooShort;
        std::memcpy(&m.freerUid, p + off, 4);
        if (!PrisonFreerOk(m.uid, m.freerUid)) return kPrisonDecodeBadFreer;
        if (m.kind == kPrisonJoinNo)
        {
            if (size - off < 5) return kPrisonDecodeTooShort;
            m.joinNo = (unsigned char)p[off + 4];
            if (m.joinNo < 1 || m.joinNo > kJoinNoMax) return kPrisonDecodeBadReason;
        }
        if (m.kind == kPrisonFreed)   /* the locks the freer's game read open on its copy */
        {
            if (size - off < 5) return kPrisonDecodeTooShort;
            m.freedLocks = (unsigned char)p[off + 4];
            if ((m.freedLocks & ~kFreedLocksAll) != 0) return kPrisonDecodeBadLocks;
        }
    }
    if (out) *out = m;
    return kPrisonDecodeOk;
}

/* ---- a bail paid on one game for another game's character, as pure decisions (spawn.cpp bail block, PrisonTick) ---- */

/* The marker a kPrisonRelease carries in its cage key for a bail (see the header comment). */
const char* const kPrisonBailKey = "@bail";

/* How the owner releases its character on a kPrisonRelease, from the cage key as sent and whether a named faction was found here:
   2 "@player" (a player freed it: no bounty cleared), 3 "@bail" (every bounty cleared, no pardon, slave state 0 - the engine's bail
   confirm), 1 a stringID that names a faction here, 0 anything else (the cage's faction). */
const int kReleaseModeCage = 0, kReleaseModeNamed = 1, kReleaseModePlayer = 2, kReleaseModeBail = 3;
inline bool PrisonReleaseKeyIsMarker(const std::string& key) { return key == "@player" || key == kPrisonBailKey; }
inline int PrisonReleaseModeOf(const std::string& key, bool namedFound)
{
    if (key == "@player") return kReleaseModePlayer;
    if (key == kPrisonBailKey) return kReleaseModeBail;
    return (!key.empty() && namedFound) ? kReleaseModeNamed : kReleaseModeCage;
}
inline bool PrisonReleaseClearsEveryBounty(int mode) { return mode == kReleaseModeBail; }   /* clearBounty(manager, no faction), as the bail */
inline bool PrisonReleasePardons(int mode) { return mode != kReleaseModeBail; }              /* the bail writes no pardon */
inline bool PrisonReleaseFreesSlave(int mode) { return mode == kReleaseModeBail; }           /* the bail sets slave state 0 */

/* The payer's game, one prisoner the bail confirm just freed: told to its owner only when it is a replicated character this game
   does not own (uid 0 = not replicated: the engine's own business; this game's own character was freed by this engine). */
inline bool BailTellsOwner(unsigned int uid, bool mine) { return uid != 0 && !mine; }

/* The owner: a "@bail" release is taken from the game that runs the character's area (where the prison and its keeper are run),
   or from any game when the owner runs that area itself (the payer paid on its own copy there), as this game's area map says;
   -1 (not known) refuses. */
inline bool PrisonBailFromRunner(int holderSlot, int senderSlot, int mySlot)
{
    return holderSlot >= 0 && senderSlot >= 0 && (holderSlot == senderSlot || holderSlot == mySlot);
}

/* T-478 step 3 review fold (manager 2026-11-03; three-player bail is T-680): the payer's game, one prisoner its bail window could
   list - does paying for it free it? This game's own character or one that is not replicated: yes (this engine frees it). Another
   game's character: only when its owner will take this game's "@bail" by the owner's own rule above, judged from this game's area
   map (holderSlot = the slot that runs the prisoner's area, payerSlot = this game, ownerSlot = the character's owner; -1 unknown
   = no). A row that fails would take the money and free nobody. */
inline bool BailRowFreeable(unsigned int uid, bool mine, int holderSlot, int payerSlot, int ownerSlot)
{
    if (!BailTellsOwner(uid, mine)) return true;
    return ownerSlot >= 0 && PrisonBailFromRunner(holderSlot, payerSlot, ownerSlot);
}
/* The bail window's rows cannot be filtered before the charge (the engine builds the list inside its own dialogue case and charges
   every ticked row in the confirm, 6ae960:64-66 / 136-137; the window's constructor 0x6AC080 is not in the address table). So the
   payer's game refuses the WHOLE window when a prisoner it could list fails BailRowFreeable: this far (x, z) from the police NPC
   counts as the town's prisons (a Guess: 500 m, 10 units a metre; too far only refuses more windows, never charges). */
const float kBailRowNearUnits = 5000.0f;
inline bool BailWindowOk(int leftOut) { return leftOut == 0; }

/* The payer's game keeps a bail it told an owner about until the owner's word shows the character out of its cage: the release is
   sent again every kBailResendMs (the owner drops one it cannot apply yet - not caged there, its queue full, writes blocked), and
   the copy's release hold is renewed with each send. The copy gone (or now ours) ends it; after kBailTriesMax sends with the owner
   still saying caged it ends too, logged once, and the copy follows its owner's word again. */
const unsigned int kBailResendMs = 3000;
const int kBailTriesMax = 20;
const int kBailDone = 0, kBailGone = 1, kBailWait = 2, kBailResend = 3, kBailGiveUp = 4;
/* A bail paid before the owner's word shows the cage (the owner's own caging still queued) is done only once the owner's word has
   shown the character caged and then out: until then it is sent again on the same timer (the owner drops it while not caged). */
inline int BailPendingStep(bool copyHere, bool ownerSaysCaged, bool ownerSeenCaged, unsigned int msSinceSend, int tries)
{
    if (!copyHere) return kBailGone;
    if (!ownerSaysCaged && ownerSeenCaged) return kBailDone;
    if (msSinceSend < kBailResendMs) return kBailWait;
    return tries >= kBailTriesMax ? kBailGiveUp : kBailResend;
}

/* ---- P42: who decides a prisoner's restraint lock, as pure decisions (spawn.cpp RestraintWatch / RestraintSafePointDrain) ---- */

/* The prisoner's owner's game: is its word for one restraint due? First sight, a change of the bits or of the key, or the resend
   period over (a game that joined later, or a copy spawned again, hears it again). */
inline bool RestraintWordDue(bool sent, bool sameKey, unsigned char sentFlags, int sentLevel, unsigned char flags, int level,
                             unsigned int msSinceSend, unsigned int resendMs)
{
    if (!sent || !sameKey) return true;
    if (sentFlags != flags || sentLevel != level) return true;
    return msSinceSend >= resendMs;
}

/* A copy's game, one restraint of a copy against its owner's word. `seenEqual`: a read on this game found the copy's lock equal to
   the CURRENT word (a new word, a new restraint or a first sight start it false). A copy whose lock differs from the word takes the
   word (kRestraintRewrite) - on first sight and after every new word too, open or not: a copy is caged here by setPrisonMode with
   no lock write, so an open read then is no one's pick. Only a lock SEEN equal to a closed word and now open was opened here (a
   rescuer picked or broke it): the owner is asked once (kRestraintAsk); while that ask is fresh the copy is left open; once it is
   stale with no new word the copy takes the word again. No word yet, or the same bits: nothing. */
/* A copy found NOT restrained (out of its cage, shackles off - or a put-back's brief out moment): it keeps the owner's word (the
   key check judges another cage or other shackles) and forgets what it saw here. That moment opens a rescue window only for a
   cage that stood open here while the word for it was open too. */
inline bool RestraintLeftOpenRescue(bool isCage, bool haveWord, bool openHere, unsigned char wordFlags, int wordLevel)
{
    return isCage && haveWord && openHere && LockOpen(wordFlags, wordLevel);
}
const int kRestraintKeep = 0, kRestraintAsk = 1, kRestraintRewrite = 2;
const int kRestraintAskNone = 0, kRestraintAskFresh = 1, kRestraintAskStale = 2;
inline int RestraintCopyDecide(bool haveWord, unsigned char wordFlags, int wordLevel, unsigned char flags, int level, bool seenEqual,
                               int askState)
{
    if (!haveWord) return kRestraintKeep;
    if (flags == wordFlags && level == wordLevel) return kRestraintKeep;
    if (seenEqual && LockOpen(flags, level) && !LockOpen(wordFlags, wordLevel))
    {
        if (askState == kRestraintAskNone) return kRestraintAsk;
        if (askState == kRestraintAskFresh) return kRestraintKeep;
    }
    return kRestraintRewrite;
}

/* The owner's game, a copy's game's request: its own restraint is opened only when the request OPENS a lock that is closed here.
   A request to close, or about a lock already open here, changes nothing (the owner's word answers it). */
inline bool RestraintOwnerAccepts(unsigned char ownFlags, int ownLevel, unsigned char reqFlags, int reqLevel)
{
    return !LockOpen(ownFlags, ownLevel) && LockOpen(reqFlags, reqLevel);
}
/* ...and how: the owner keeps its own lock level; a broken request breaks its lock, any other opening clears its locked byte. */
inline unsigned char RestraintOwnerOpened(unsigned char ownFlags, unsigned char reqFlags)
{
    if (reqFlags & kLockFlagBroken) return (unsigned char)(ownFlags | kLockFlagBroken);
    return (unsigned char)(ownFlags & ~kLockFlagLocked);
}

/* A copy left a cage it was held in on its owner's word, not by this game's own cage-out and with no fresh pardon of a guard here:
   it is a rescue - told to the owner as a player's release - when the cage's lock stood open here and the owner's word for that
   cage was open too, and no jailer of this game is releasing it: either this game made no arrest of it, or the pardon's second
   look (arrest3) has run out. Otherwise the arrest3 rules hold (a jailer's release, or put back). */
inline bool CageRescueRelease(bool sawCaged, bool oursOut, bool guardFresh, bool guardArrest, bool pardonLookOver, bool openHere,
                              bool wordOpen)
{
    return sawCaged && !oursOut && !guardFresh && (!guardArrest || pardonLookOver) && openHere && wordOpen;
}

/* P42 TEST-ONLY lever `locktest` (spawn.cpp LockTestCommand): its argument shapes - `read <uid|near>`, `open|close <uid|near>
   shackles|cage`, `escape <uid|near>`. Returns the mode (kLockTestRead .. kLockTestEscape), 0 = not a shape the lever takes.
   *nearForm 1 = the target word is "near" (the lever picks the character), else *uid = the typed uid (decimal digits, not 0);
   *kind = kPrisonShackleLock / kPrisonCageLock for open|close, 0 otherwise. Words after the shape are ignored, as before. */
const int kLockTestRead = 1, kLockTestOpen = 2, kLockTestClose = 3, kLockTestEscape = 4;
inline int LockTestArgs(const std::string& arg, unsigned int* uid, int* nearForm, unsigned char* kind)
{
    *uid = 0; *nearForm = 0; *kind = 0;
    std::string t[3];
    int n = 0;
    size_t i = 0;
    while (n < 3)
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        const size_t b = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t') ++i;
        t[n++] = arg.substr(b, i - b);
    }
    if (n < 2) return 0;
    int mode = 0;
    if (t[0] == "read") mode = kLockTestRead;
    else if (t[0] == "open") mode = kLockTestOpen;
    else if (t[0] == "close") mode = kLockTestClose;
    else if (t[0] == "escape") mode = kLockTestEscape;
    else return 0;
    if (t[1] == "near") *nearForm = 1;
    else
    {
        if (t[1].size() > 10) return 0;
        unsigned long long v = 0;
        for (size_t k = 0; k < t[1].size(); ++k)
        {
            const char ch = t[1][k];
            if (ch < '0' || ch > '9') return 0;
            v = v * 10 + (unsigned long long)(ch - '0');
        }
        if (v == 0 || v > 0xFFFFFFFFull) return 0;
        *uid = (unsigned int)v;
    }
    if (mode == kLockTestOpen || mode == kLockTestClose)
    {
        if (n < 3) { *uid = 0; *nearForm = 0; return 0; }
        if (t[2] == "shackles") *kind = kPrisonShackleLock;
        else if (t[2] == "cage") *kind = kPrisonCageLock;
        else { *uid = 0; *nearForm = 0; return 0; }
    }
    return mode;
}

/* P42 TEST-ONLY lever `locktest pick|release <pickerUid|me> <prisonerUid|near>` (spawn.cpp LockTestCommand): the picker - this
   game's own character, a typed uid or `me` - is given the engine's own order on the caged character, PICK_LOCK 76 (a player's
   click on a caged character; the engine turns it into a pick of the cage) or RELEASE_PRISONER 110. Returns kLockTestPick /
   kLockTestRelease, 0 = not this shape. *pickerMe 1 = "me", else *picker = the typed uid; *prisonerNear 1 = "near", else
   *prisoner = the typed uid (decimal digits, not 0, at most 32 bits). Words after the shape are ignored. */
const int kLockTestPick = 5, kLockTestRelease = 6;
const int kLockTaskPick = 76, kLockTaskRelease = 110;   /* TaskType PICK_LOCK 0x4C, RELEASE_PRISONER 0x6E */
inline int LockTestPickTask(int mode) { return mode == kLockTestPick ? kLockTaskPick : (mode == kLockTestRelease ? kLockTaskRelease : 0); }
inline bool LockTestUidWord(const std::string& w, unsigned int* out)
{
    *out = 0;
    if (w.empty() || w.size() > 10) return false;
    unsigned long long v = 0;
    for (size_t k = 0; k < w.size(); ++k)
    {
        const char ch = w[k];
        if (ch < '0' || ch > '9') return false;
        v = v * 10 + (unsigned long long)(ch - '0');
    }
    if (v == 0 || v > 0xFFFFFFFFull) return false;
    *out = (unsigned int)v;
    return true;
}
inline int LockTestPickArgs(const std::string& arg, unsigned int* picker, int* pickerMe, unsigned int* prisoner, int* prisonerNear)
{
    *picker = 0; *pickerMe = 0; *prisoner = 0; *prisonerNear = 0;
    std::string t[3];
    int n = 0;
    size_t i = 0;
    while (n < 3)
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        const size_t b = i;
        while (i < arg.size() && arg[i] != ' ' && arg[i] != '\t') ++i;
        t[n++] = arg.substr(b, i - b);
    }
    if (n < 3) return 0;
    int mode = 0;
    if (t[0] == "pick") mode = kLockTestPick;
    else if (t[0] == "release") mode = kLockTestRelease;
    else return 0;
    unsigned int pu = 0, qu = 0;
    const int pm = (t[1] == "me") ? 1 : 0;
    const int qn = (t[2] == "near") ? 1 : ((t[2] == "nearopen") ? 2 : 0);   /* nearopen: the nearest whose shackles read open here */
    if (!pm && !LockTestUidWord(t[1], &pu)) return 0;
    if (!qn && !LockTestUidWord(t[2], &qu)) return 0;
    *picker = pu; *pickerMe = pm; *prisoner = qu; *prisonerNear = qn;
    return mode;
}

/* ---- T-980: a prisoner another player's character frees, as pure decisions (speech.cpp FreedNoteEvent / FreedSendDrain /
   FreedRaiseEvent, spawn.cpp FreedInDrain, hire.cpp the join hand-off) ---- */

const int kPrisonEvUnlock = 0xD;   /* EventTriggerEnum 13: the unlock event the release task 0x357040 and the pick task 0x359100 send */
const int kPrisonActJoin = 18;     /* DialogActionEnum DA_JOIN_SQUAD_FAST: the freed prisoner's join line */

/* A plain record handed from an engine thread to the main thread: the event (or act) number, the prisoner's uid and the freer's;
   for a held join line also its actions (up to kPrisonNoteActs type:value pairs, and how many the line has in all), for the log. */
const int kPrisonNoteActs = 16;
/* For an unlock event on a copy also the copy's cage as the event found it (speech.cpp FreedNoteEvent): cagedAtEvent 1 in a cage,
   0 not, -1 unreadable, and the five id fields of the hand that names the cage (the character's +0x300 hand), so the main thread
   reads the lock of that cage after the release task has taken the copy out of it. Other notes leave both unset. */
const int kPrisonNoteHandBytes = 20;
struct PrisonNote { int ev; unsigned int uid; unsigned int freerUid; int nActs; int actTotal; int actType[kPrisonNoteActs]; int actValue[kPrisonNoteActs];
                    int cagedAtEvent; unsigned char cageHand[kPrisonNoteHandBytes]; };
/* A fixed ring of PrisonNote. Not thread-safe by itself: the plugin holds one lock around every push and take. A full ring keeps
   what it holds and counts the new record in `lost`. */
const int kPrisonNoteCap = 32;
struct PrisonNoteRing
{
    PrisonNote at[kPrisonNoteCap];
    int head, n;
    long long lost;
    PrisonNoteRing() : head(0), n(0), lost(0) {}
};
inline bool PrisonNotePush(PrisonNoteRing* r, const PrisonNote& x)
{
    if (r->n >= kPrisonNoteCap) { ++r->lost; return false; }
    r->at[(r->head + r->n) % kPrisonNoteCap] = x;
    ++r->n;
    return true;
}
/* Takes up to cap records, oldest first; returns how many. */
inline int PrisonNoteTake(PrisonNoteRing* r, PrisonNote* out, int cap)
{
    int k = 0;
    while (r->n > 0 && k < cap)
    {
        out[k++] = r->at[r->head];
        r->head = (r->head + 1) % kPrisonNoteCap;
        --r->n;
    }
    return k;
}

/* The freer's game, any thread: an unlock event raised on a COPY (the speaker has a uid this game does not run) with this game's own
   character as the freer goes to the prisoner's owner. Every other event, an NPC freer, or this game's own prisoner: nothing. */
inline bool FreedForward(int ev, unsigned int prisonerUid, bool prisonerMine, unsigned int freerUid, bool freerMine)
{
    return ev == kPrisonEvUnlock && prisonerUid != 0 && !prisonerMine && freerMine && PrisonFreerOk(prisonerUid, freerUid);
}

/* The freer's game, main thread: the copy's cage as the unlock event found it. The release task 0x357040 opens the cage's lock,
   raises the event, and then takes the prisoner out of the cage, so when the freed message is sent the copy no longer reads as
   caged and the watch's own check finds no cage. The event's reading decides instead: cagedAtEvent 1 in a cage, 0 not, -1
   unreadable; lockRead 1 the lock of the cage the event's hand names was read (lockOpen its state), 0 that cage has no lock, -1 the
   cage or its lock could not be read. */
const int kFreedCageNone = 0, kFreedCageLocked = 1, kFreedCageOpen = 2, kFreedCageUnread = 3;
inline int FreedCageAtEvent(int cagedAtEvent, int lockRead, bool lockOpen)
{
    if (cagedAtEvent == 0) return kFreedCageNone;
    if (cagedAtEvent < 0 || lockRead < 0) return kFreedCageUnread;
    if (lockRead == 0) return kFreedCageNone;
    return lockOpen ? kFreedCageOpen : kFreedCageLocked;
}
inline const char* FreedCageName(int cageAtEvent)
{
    return cageAtEvent == kFreedCageOpen ? "open" : cageAtEvent == kFreedCageLocked ? "locked" : cageAtEvent == kFreedCageUnread ? "unread" : "none";
}
/* ...and the freed message's lock byte. openNow: the locks the watch's own check read open as the message goes. A cage open at the
   event adds the cage bit, and its open request goes to the owner ahead of the message (*sendCageOpen) unless that check read the
   cage open itself (the copy still caged: the watch handles that cage as before). A cage locked at the event (a pick of the
   shackles only leaves it locked on both games, and the owner's line for a still-caged prisoner is then the right one), no cage,
   or one not read: the check's own byte, and no request from here. */
inline unsigned char FreedLocksAtEvent(unsigned char openNow, int cageAtEvent, bool* sendCageOpen)
{
    *sendCageOpen = cageAtEvent == kFreedCageOpen && (openNow & kFreedLockCage) == 0;
    return (cageAtEvent == kFreedCageOpen) ? (unsigned char)(openNow | kFreedLockCage) : openNow;
}

/* The owner, at the safe point, one kPrisonFreed: raise the event on its own prisoner at once, or refuse (each refusal counted).
   Just before this message the freer's game sends, on the same road, the lock-open requests its lock watch makes for its copy's
   shackles and cage, so the owner applies (or refuses) each of them before it applies this message. freedLocks: the message's
   lock byte (the locks that game read open on its copy as it sent it); lockedHere: the same bits for the locks that read locked
   here now. A lock in both - open there, still locked here - means its request was refused or lost: the raise would pick the
   line for a locked prisoner, so it is not raised (an end of the exchange, not a wait). */
const int kFreedRaise = 0, kFreedNotOurs = 2, kFreedGone = 3, kFreedNoFreer = 4, kFreedFreerNotSender = 5, kFreedLockShut = 6;
inline int FreedOwnerDecide(bool prisonerMine, bool prisonerHere, bool freerHere, bool freerMine, bool freerFromSender,
                            unsigned char freedLocks, unsigned char lockedHere)
{
    if (!prisonerMine) return kFreedNotOurs;
    if (!prisonerHere) return kFreedGone;
    if (!freerHere || freerMine) return kFreedNoFreer;
    if (!freerFromSender) return kFreedFreerNotSender;
    if ((freedLocks & lockedHere & kFreedLocksAll) != 0) return kFreedLockShut;
    return kFreedRaise;
}

/* The owner, any thread, a line about to run its actions: a line carrying the join act (18) whose speaker is this game's own
   character and whose target is ANOTHER player's character (its copy here) is held - not run - and the freer's game is asked to
   hire the prisoner. Run here, the act would put the prisoner in that player's stand-in faction on this game only (the stand-in has
   no player controller, so the engine takes its NPC branch). A line that also takes money (act 8) is left to the engine: the ask
   carries no price. tgtPlayerChar / spkPlayerChar: 1 a player character, 0 an NPC, -1 not classified - the speaker must be an NPC
   (one of this game's own squad speaking a join line to another player's character is not a prisoner joining). canEndChat: the
   engine's endDialogue is in the address table - without it a held line would leave the chat open, so the line is not held. */
inline bool JoinHandOffHold(bool hasJoin, bool hasMoney, unsigned int speakerUid, bool speakerMine, unsigned int targetUid, bool targetMine,
                            int tgtPlayerChar, int spkPlayerChar, bool canEndChat)
{
    return hasJoin && !hasMoney && speakerUid != 0 && speakerMine && targetUid != 0 && targetUid != speakerUid && !targetMine
        && tgtPlayerChar == 1 && spkPlayerChar == 0 && canEndChat;
}

/* The freer's game, at the safe point, one kPrisonJoinAsk: kJoinNoNone = ask for the hire, else the reason sent back. */
inline unsigned char JoinAskDecide(bool blocked, bool freerMine, bool prisonerHere, bool prisonerMine)
{
    if (blocked) return kJoinNoBlocked;
    if (!freerMine) return kJoinNoFreerNotOurs;
    if (prisonerMine) return kJoinNoPrisonerOurs;
    if (!prisonerHere) return kJoinNoNoCopy;
    return kJoinNoNone;
}

/* The owner keeps one asked hand-off per prisoner until the freer's game's hire request for it arrives, its kPrisonJoinNo arrives,
   that game leaves, or kJoinAskWaitMs pass with neither (then it is logged as lost and dropped; the prisoner joins nobody). */
const unsigned int kJoinAskWaitMs = 15000;
inline bool JoinAskLost(unsigned int msSinceSend) { return msSinceSend >= kJoinAskWaitMs; }

} // namespace cooprison
