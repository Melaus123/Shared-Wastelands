/* squadbirth.h - a squad's birth persistence and what follows from it: whether the game keeps the squad when its area unloads,
   whether its world record goes when the game discards it, and whether a world record made before the flag existed is retired.

   THE ENGINE (Read): RootObjectFactory::createRandomUnloadedSquad 0x57EA30 writes its byte argument into Platoon+0x115 (the kept
   byte, decomp_57ea30.txt:61). A town's residents are born kept (1); its roaming squads are born not kept (0) and the engine
   destroys them when their area unloads, while their camp makes new ones. Platoon::_NV_serialiseEverything 0x372DB0 saves the
   byte as the state record's bool "persistent" and loadStateData 0x7EC550 reads it back, so a squad from a save load carries its
   own kept byte.

   Pure functions only: the plugin, the world server and the offline suite compile this same header. */
#ifndef SQUADBIRTH_H
#define SQUADBIRTH_H

#include <string>
#include <vector>

namespace sqbirth {

const int kBirthUnknown = -1;   /* no birth flag: an older record or message, or a squad never asked */
const int kBirthDiscard = 0;    /* born not kept: the game discards it when its area unloads */
const int kBirthKeep    = 1;    /* born kept: the game puts it to sleep when its area unloads */

/* ---- the RECORD flags byte (factionkey.h kRecFacCode is bit 0) ---- */
const unsigned int kRecBirthKnown   = 2u;   /* bit 1: the squad's birth flag is known */
const unsigned int kRecBornDiscard  = 4u;   /* bit 2: born not kept (meaningful only with bit 1) */
const unsigned int kRecBirthMask    = kRecBirthKnown | kRecBornDiscard;

inline unsigned int FlagsOfBirth(int birth)
{
    return birth == kBirthKeep ? kRecBirthKnown : (birth == kBirthDiscard ? kRecBirthMask : 0u);
}
inline int BirthOfFlags(unsigned int flags)
{
    if ((flags & kRecBirthKnown) == 0) return kBirthUnknown;
    return (flags & kRecBornDiscard) != 0 ? kBirthDiscard : kBirthKeep;
}
/* a write of a record keeps the birth bits held for it unless the arriving write knows them; every other bit is the arriving write's */
inline unsigned int FlagsMerge(unsigned int stored, unsigned int arriving)
{
    const unsigned int birth = (arriving & kRecBirthKnown) != 0 ? (arriving & kRecBirthMask) : (stored & kRecBirthMask);
    return (arriving & ~kRecBirthMask) | birth;
}
/* a birth flag that arrives replaces the one held only when it is known */
inline int BirthMerge(int stored, int arriving) { return arriving >= 0 ? arriving : stored; }
/* the birth flag a keep question uses, in this order: one noted at the squad's creation, copy or record (noted); its bound record's
   known flag (recordBirth - one a game published from a creation); one read off the squad's own kept byte (liveRead, *fromLive = 1).
   -1 none. The record comes before the squad's byte because the engine clears a resident's byte at an unload and a save keeps it. */
inline int BirthChoose(int noted, int recordBirth, int liveRead, int* fromLive)
{
    if (fromLive != 0) *fromLive = 0;
    if (noted >= 0) return noted;
    if (recordBirth >= 0) return recordBirth;
    if (liveRead >= 0) { if (fromLive != 0) *fromLive = 1; return liveRead; }
    return kBirthUnknown;
}
/* the birth flag a game publishes for a squad (its record, the index line, CONTEXT): only one noted at its creation, copy or record is
   definite; one read off its own kept byte goes as unknown - in an older world that byte may be this mod's own forcing, or the
   engine's clearing of a resident's byte at an earlier unload */
inline int BirthToPublish(int birth, int fromLive) { return fromLive != 0 ? kBirthUnknown : birth; }

/* ---- the index line's optional field 16 (storemeta.h) ---- */
inline std::string BirthField(int birth) { return birth == kBirthKeep ? std::string("p") : (birth == kBirthDiscard ? std::string("d") : std::string()); }
inline int BirthOfField(const std::string& f) { return f == "p" ? kBirthKeep : (f == "d" ? kBirthDiscard : kBirthUnknown); }
/* the game's own index reader (store.cpp ParseMetaLine) splits a line on tabs: field 16 of a v7 line, its line end stripped; -1 when
   the line has none or is not v7 */
inline int BirthOfLineFields(const std::vector<std::string>& f)
{
    if (f.size() < 16 || f[0] != "v7") return kBirthUnknown;
    std::string s = f[15];
    while (!s.empty() && (s[s.size() - 1] == '\r' || s[s.size() - 1] == '\n')) s.erase(s.size() - 1);
    return BirthOfField(s);
}

/* ---- CONTEXT's trailing word (the copy message) ---- */
const unsigned int kWireBirthUnknown = 0xFFFFFFFFu;
inline unsigned int WireOfBirth(int birth) { return birth == kBirthKeep ? 1u : (birth == kBirthDiscard ? 0u : kWireBirthUnknown); }
inline int BirthOfWire(unsigned int w) { return w == 1u ? kBirthKeep : (w == 0u ? kBirthDiscard : kBirthUnknown); }

/* ---- the keep question (Platoon::isPersistentSquad, asked of a squad bound to a world record) ----
   living: living members (0 = none, -2 = a squad read whole with no awake members - emptied by hiring or merging, -1 = not read - a
   fault, an implausible member list or more than 512 members); birth: the squad's birth
   flag (BirthChoose); fromLive: 1 when the flag was read off the squad's own kept byte rather than noted at its creation, copy or
   record or carried by its record; hasHome: the squad has a home building (its record's home key or its live one - a town's
   resident); playerHeld: the squad's faction is a player's or a stand-in's, or it holds such a character (a member, or one a member
   carries); otherHeld: another game holds the squad's area now; templateRoaming (T-999 fold 2b): the squad's template is listed in its
   bound record's town's "roaming squads" (1), not listed (0), not readable (-1) - asked only of a kept flag read off the squad's own
   byte with no home: that byte is not proof of a kept birth (this mod writes 1 on a copy or a record-made squad whose flag is unknown),
   so such a squad goes the protected road only when its template is a roaming one, and is kept otherwise. */
enum { kKeepDeadRelease = 0, kKeepEngine = 1, kKeepBorn = 2, kKeepUnknown = 3, kKeepPlayerHeld = 4, kKeepOtherHolder = 5, kKeepHome = 6, kKeepNotRead = 7 };
inline int KeepDecide(int living, int birth, int fromLive, int hasHome, int playerHeld, int otherHeld, int templateRoaming)
{
    if (living == 0 || living == -2) return kKeepDeadRelease;   /* nobody living, or no members at all: the game's own answer */
    if (living < 0) return kKeepNotRead;   /* T-999 fold 2: the member list not read - kept, the answer from before the birth flag (the dead road is taken on a read only) */
    if (hasHome == 1) return kKeepHome;   /* a town's resident: kept whatever its byte says (the engine clears a knocked-out resident's) */
    if (birth == kBirthKeep && fromLive == 0) return kKeepBorn;
    if (birth != kBirthKeep && birth != kBirthDiscard) return kKeepUnknown;   /* no flag readable: kept, as a recorded squad always was */
    if (birth == kBirthKeep && templateRoaming != 1) return kKeepUnknown;   /* fold 2b: a kept byte read off a squad not shown to be a roaming one: kept */
    /* born not kept, or a kept byte read off a roaming-listed squad with no home (the byte may be this mod's own write): kept only
       while protected, else the game decides - and the engine's own test keeps a campaign's squad */
    if (playerHeld == 1) return kKeepPlayerHeld;
    if (otherHeld == 1) return kKeepOtherHolder;
    return kKeepEngine;
}
/* 1 = the mod answers 'keep'; 0 = the game's own answer is returned */
inline int KeepAnswers(int verdict) { return verdict >= kKeepBorn ? 1 : 0; }
/* the kept byte a squad made here from another game's message or from a world record is given: its birth flag; unknown = kept */
inline int MadeKeptByte(int birth) { return birth == kBirthDiscard ? 0 : 1; }

/* ---- the delete a destroy of a squad sends ----
   bound: the squad is bound to a world record here; diedOut: the death watch marked it wiped out; discarding: the last keep
   answer for it was the game's own while it had living members; livingAtDestroy: its living members as the destroy fired (> 0 = the
   unload road - a squad emptied by hiring or merging has none (-2), and its destroy stays the plain delete; -1, its members not read,
   stays on the unload road: a squad marked discarding had living members at its last keep question); idShared: another squad here
   is bound to the same id, whose record the discard of this one must not delete (nothing is sent). */
enum { kDelNone = 0, kDelPlain = 1, kDelDiscard = 2 };
/* fold 3: 1 = the keep question's 'no living members' answer erases the squad's discard mark - everyone dead (0), or an empty squad
   (-2) the engine's own unload removals did not empty (hiring, merging). An unload-emptied one keeps the mark for its destroy: at an
   area unload the squad walk asks with the members there, removes them all ('wandered out of zone'), and the faction update asks again
   with the squad empty before it destroys it. */
inline int DiscardMarkErased(int living, int emptiedByUnload) { return (living == 0 || (living == -2 && emptiedByUnload == 0)) ? 1 : 0; }
/* 1 = a destroy with this living count is on the unload road (living members, or not read; fold 3: or none (-2) when the engine's own
   unload removals emptied it - emptiedByUnload) */
inline int LivingUnloadRoad(int livingAtDestroy, int emptiedByUnload) { return (livingAtDestroy > 0 || livingAtDestroy == -1 || (livingAtDestroy == -2 && emptiedByUnload != 0)) ? 1 : 0; }
inline int DestroyDelete(int bound, int diedOut, int discarding, int livingAtDestroy, int emptiedByUnload, int idShared)
{
    if (bound == 0) return kDelNone;
    if (diedOut == 0 && discarding != 0 && LivingUnloadRoad(livingAtDestroy, emptiedByUnload) != 0) return idShared != 0 ? kDelNone : kDelDiscard;
    return kDelPlain;
}

/* ---- the world server's judgement of a discard delete ----
   holderId: the player the area map names as the holder of the record's area ("" = nobody); senderId: the sending game's player;
   otherLoaded: how many games other than the sender have the area loaded now (their last area report within the grace). A discard
   delete waits while ANOTHER game holds or has the area loaded - that game still shows the squad, and the sender's own area update
   can arrive after its delete; the game that drops the area last sends the delete that lands. A plain delete is never judged here. */
enum { kSrvApply = 0, kSrvRefuseOtherHolds = 1, kSrvRefuseOtherLoaded = 2 };
inline int ServerDiscardJudge(int isDiscard, const std::string& holderId, const std::string& senderId, int otherLoaded)
{
    if (isDiscard == 0) return kSrvApply;
    if (!holderId.empty() && holderId != senderId) return kSrvRefuseOtherHolds;
    if (otherLoaded > 0) return kSrvRefuseOtherLoaded;
    return kSrvApply;
}

/* ---- a refused discard delete, held on the world server and judged again (T-999 fold 2) ----
   The world server holds a refused discard delete (record id -> its stamp, sender and area) and judges it again when the area's
   holder changes, when a game's report drops the area and when a game's link closes. recordHere: the record is still held;
   recordStamp: its last write's stamp; pendingStamp: the held delete's stamp. Dropped when the record is gone (deleted another way)
   or was written again after the delete (newer stamp - the squad lives on somewhere); else ServerDiscardJudge decides. */
const size_t kPendingDiscardCap = 1024;
enum { kPdKeep = 0, kPdApply = 1, kPdDrop = 2 };
/* fold 2c: senderReturned (PendingDiscardSenderReturned) - the sending game is back in the area after the server saw it leave: it
   remade the squad from the record, which must stay (dropped) */
inline int PendingDiscardStep(int recordHere, long long recordStamp, long long pendingStamp, const std::string& holderId, const std::string& senderId, int otherLoaded, int senderReturned)
{
    if (recordHere == 0) return kPdDrop;
    if (recordStamp > pendingStamp) return kPdDrop;
    if (senderReturned != 0) return kPdDrop;
    return ServerDiscardJudge(1, holderId, senderId, otherLoaded) == kSrvApply ? kPdApply : kPdKeep;
}
/* 1 = the sender is back: the server saw it leave the area (senderLeft: a report of its slot without the area, or its link closed)
   and it now holds the area or its latest report lists it (senderThere). Before a leave is seen its own listing is ignored - its area
   update can land after its delete. */
inline int PendingDiscardSenderReturned(int senderLeft, int senderThere) { return (senderLeft != 0 && senderThere != 0) ? 1 : 0; }
/* 1 = a write of the record with writeStamp drops the held discard delete (it is newer than the delete) */
inline int PendingDiscardDropOnWrite(long long pendingStamp, long long writeStamp) { return writeStamp > pendingStamp ? 1 : 0; }
/* 1 = a refused discard delete is held: one already held for the id is replaced; a new one only below the cap */
inline int PendingDiscardAdmit(size_t held, size_t cap, int alreadyHeld) { return (alreadyHeld != 0 || held < cap) ? 1 : 0; }

/* ---- a world record from before the birth flag, at its placing: retired instead of made again ----
   ALL must hold: no birth flag; no home building key; its faction is not a player's or a stand-in's; its squad template is listed
   in its town's "roaming squads" (1; 0 not listed, -1 not readable); the saved squad names no player's or stand-in's faction
   (0; 1 it does, -1 not readable). */
inline int RetireJudge(int birth, int homeEmpty, int playersFaction, int templateRoaming, int holdsPlayer)
{
    return (birth == kBirthUnknown && homeEmpty != 0 && playersFaction == 0 && templateRoaming == 1 && holdsPlayer == 0) ? 1 : 0;
}

/* does `bytes` contain `needle` (a byte search; an empty needle is never found) */
inline int BytesHold(const char* bytes, size_t n, const std::string& needle)
{
    if (bytes == 0 || needle.empty() || n < needle.size()) return 0;
    for (size_t i = 0; i + needle.size() <= n; ++i)
    {
        size_t j = 0;
        while (j < needle.size() && bytes[i + j] == needle[j]) ++j;
        if (j == needle.size()) return 1;
    }
    return 0;
}

}   /* namespace sqbirth */

#endif
