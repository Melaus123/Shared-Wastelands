/* src/common/storemeta.h - THE NOTEBOOK'S INDEX LINE, ITS CHECKSUM AND ITS RECOVERY RULE, AND NOTHING ELSE
 * (P8b, design-save.md S1 / part 1.4).
 *
 * THE ONE RULE THIS FILE EXISTS TO ENFORCE, and it is clockmath.h's rule for the same stated reason: nothing in
 * here reads a global, calls the operating system or includes an Ogre, ENet or Windows header.
 * Everything is a pure function of its arguments. The same translation unit is compiled into the game plugin,
 * into the notebook (SharedWastelandsServer.exe) and into the offline test exe, so THREE PROGRAMS CANNOT HOLD THREE
 * DIFFERENT IDEAS OF ONE FILE FORMAT (6a lesson 11: remove the hand that keeps two copies in step).
 *
 * The file operations themselves - which file to rename onto which - are NOT here. They are the caller's, and
 * the decision that drives them (ChooseVersion) is pure, so every one of the four crash windows can be staged
 * as a set of facts in the offline suite with no disk and no game.
 *
 * STORE-FILE FORMAT 7 (B12, decision 52). One tab-separated line per record, in <id>.meta:
 *
 *   v7 <writtenAt> <owner> <x> <y> <z> <squadSid> <factionName> <worldId> <posAt> <town> <len> <crc32> <seq>
 *
 * v7 APPENDS ONE FIELD, `seq`: the NOTEBOOK'S OWN SEQUENCE NUMBER for this record, a u64 the notebook
 * increments once and stamps on every record it accepts, monotonic for the life of the folder and persisted
 * here because the index is the only thing that survives a restart of the notebook process. It exists for
 * decision 52's outage queue: a move refused while the notebook was unreachable is journaled and re-issued
 * when it returns, and the replay must LOSE to a record another writer changed meanwhile. `writtenAt` cannot
 * order those two writes - it is whole seconds of wall clock, stamped at write time, on two separate
 * machines - so a replayed record would always look newer and would silently win the conflict it must lose
 * (read-b12's top risk). seq is stamped by ONE process and therefore orders them. A v6 line and below carries
 * no seq: it parses exactly as before, reads back as seq 0, and is UPGRADED IN PLACE at the notebook's next
 * start with seq 0 - a sequence number cannot be invented for a record written before there were any, and 1
 * would be a guess that reads as fact. Field 14 is APPENDED, so every reader here still reads by field index.
 *
 * STORE-FILE FORMAT 6 (B10, design-e46-store 3.5). The same line without the last field:
 *
 *   v6 <writtenAt> <owner> <x> <y> <z> <squadSid> <factionName> <worldId> <posAt> <town> <len> <crc32>
 *
 * v6 HAS THE SAME FIELDS IN THE SAME ORDER AS v5 AND ONE FIELD MEANS SOMETHING ELSE. `owner` was the
 * SESSION ROLE of whichever game wrote the record - 1 the host, 2 the client - which cannot express a
 * third player and is wrong the moment the writer is the notebook`s authority peer rather than the host
 * (audit C13). It now carries the writer`s NOTEBOOK SLOT, 0..kMaxOwnerSlot (65,519 since M1-b), or kOwnerUnknown when the writer had
 * no slot. A v5 line is parsed exactly as before and UPGRADED IN PLACE at the notebook`s next start,
 * with owner set to kOwnerUnknown: a role cannot be mapped to a slot (which slot the host held is not
 * knowable from the file, and under decision 32 the host holds no particular slot), and writing 0 would
 * be a guess that reads as fact.
 *
 * `len` is the byte length of <id>.platoon, decimal; `crc32` is CRC-32 (IEEE, reflected, polynomial
 * 0xEDB88320) of those bytes as eight lowercase hex digits. len 0 / crc 00000000 means THIS RECORD HAS NO
 * PAYLOAD - a position-only note - and is not a failure. Fields 11 and 12 are APPENDED: v2/v3/v4 lines are
 * still parsed (they carry neither), because every reader here reads by field index and the only floor is 10.
 *
 * P8c (review-p8b C-1, H-2). THE UPGRADE OF A v2-v4 LINE IS A DECISION, AND IT LIVES HERE TOO.
 * It was a single `if (read succeeded)` in the caller, which made "the file is not there" and "the file is
 * there and I could not read it for a moment" the same answer - and wrote that answer back as a line saying
 * the record has no payload, permanently, over a squad that was intact on disk. UpgradeDecide takes the two
 * apart as an argument, so the case that needs a locked file to reproduce on disk is one literal here.
 *
 * P8d (review-p8c C-1, C-2, H-1). ONE FILE HAS EXACTLY THREE STATES AND THE DECISION IS TOLD ALL THREE,
 * FOR ALL FOUR OF A RECORD'S FILES. P8c took "absent" and "unreadable" apart for ONE file on ONE path;
 * the v5 validation path and the other three inputs to the upgrade still collapsed them, so a payload
 * that was merely LOCKED was promoted over and destroyed at the next start (review-p8c C-2) and a locked
 * <id>.meta.prev blessed a torn payload for good (H-1). FileState is named here, in the pure file,
 * because it is the DECISION that has to be able to refuse; the probe that produces it lives in the relay,
 * which is the only thing here allowed to touch a disk.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no >> template closer.
 */
#ifndef COOP_COMMON_STOREMETA_H
#define COOP_COMMON_STOREMETA_H

#include <cstddef>
#include <string>
#include "areaclaim.h"   /* M1-b (review-m1 M4): coopstore::kSlotLifetimeMax - kMaxOwnerSlot is DERIVED from it, so there is one number and not two */

namespace coopstore {

/* CRC-32, IEEE 802.3, reflected, polynomial 0xEDB88320, init 0xFFFFFFFF, final xor 0xFFFFFFFF - the same
   definition zip, png and zlib use, so a value here can be checked against any outside tool. Known vectors are
   asserted in the offline suite: "" = 0x00000000, "a" = 0xE8B7BE43, "123456789" = 0xCBF43926. */
unsigned int Crc32(const void* data, std::size_t n);

/* THE RECORD`S WRITER, AS IT TRAVELS AND AS IT IS STORED (B10, audit C13 / decision 32).
   kOwnerUnknown is a u32 on the wire and reads back as -1 through the `int owner` the record decoders
   hand out - that is deliberate and is what the tests assert against; it must not be "fixed". */
const unsigned int kOwnerUnknown = 0xFFFFFFFFu;
/* M1-b (review-m1 M4): the highest slot a writer can have is the highest number the notebook hands out -
   kSlotLifetimeMax - 1 = 65,519. It was 1023, so every player from slot 1024 up was published, and read back off
   the file, as an UNKNOWN writer - and an unknown writer translates everything, its own buildings included. */
const unsigned int kMaxOwnerSlot = (unsigned int)(kSlotLifetimeMax - 1);

/* THIS GAME`S OWN owner FIELD, FROM ITS SLOT. The one arithmetic behind StoreOwnerField(), lifted here
   so the offline suite can sweep it without a game: a slot below 0 (no WELCOME yet, or the link is
   down) and a slot outside the numbering are both kOwnerUnknown - we may not claim authorship of
   anything on a number the notebook did not give us. */
unsigned int OwnerFieldFromSlot(int slot);

/* THE FORMAT 5 -> 6 MIGRATION, AS ONE EXPRESSION, so the relay, the plugin and the suite cannot hold
   three ideas of it. A line below v6 holds a session ROLE in `owner` (1 the host, 2 the client) and a
   role cannot be mapped to a slot: which slot the host held is not knowable from the file, and under
   decision 32 the host holds no particular slot. 0 would be a guess that reads as fact. */
unsigned int OwnerAfterUpgrade(int lineVersion, unsigned int lineOwner);

/* THE owner FIELD AS IT IS READ OFF A LINE. Digits only, and either kOwnerUnknown exactly or a slot in
   0..kMaxOwnerSlot. Anything else is REFUSED - the line does not parse - rather than silently read as
   0, which is a real slot and would make some game the author of a record it never wrote. */
bool MetaOwnerFieldValid(const std::string& field, unsigned int* out);

/* WHETHER A LOADED ZONE`S BUILDING-OWNER STRINGS MUST BE SWAPPED, AND IT IS ONE QUESTION: "was this
   record written by another game?". `haveRecord` is 0 when this game holds no record for the zone at
   all. An UNKNOWN writer TRANSLATES, on either side of the comparison: the swap is symmetric, so a
   wrong swap shows up as wrong ownership, while a MISSED swap leaves the other player`s buildings
   reading as yours - which is worse. */
enum OwnerVerdict
{
    kOwnerNoRecord         = 0,   /* no record for this zone here - nothing is swapped */
    kOwnerTranslateUnknown = 1,   /* either side has no slot - translate, fail-safe */
    kOwnerTranslateOther   = 2,   /* a different slot wrote it - translate */
    kOwnerSkipOwn          = 3    /* my own slot wrote it - leave it alone */
};
int OwnerTranslateDecide(int haveRecord, unsigned int recordOwner, unsigned int myOwner);

/* THE PARSED LINE. `version` is 2, 3, 4, 5, 6 or 7 as found; `len` and `crc` are 0 for anything below 5, which is
   why an upgrade has to read the payload file to fill them in and cannot be done from the line alone.
   B12: `seq` is 0 for anything below 7 - the notebook stamped nothing before format 7 existed. */
struct MetaLine
{
    int          version;
    long long    writtenAt;
    long long    posAt;
    unsigned int owner;
    float        x, y, z;
    std::string  squadSid;
    std::string  factionName;
    std::string  worldId;
    std::string  town;
    long long    len;
    unsigned int crc;
    unsigned long long seq;   /* B12 (decision 52): the notebook's own sequence number; 0 below v7 */
    MetaLine();
};

/* Encode as v5 / as v6 / as v7. v5 and v6 differ ONLY in the tag and v7 appends `seq`, so they are one
   encoder with one argument and cannot drift apart (6a lesson 11); MetaEncodeV5 and MetaEncodeV6 stay
   because the offline suite has to be able to MAKE an older line to migrate. Returns false - and writes nothing - when any string field carries a TAB, a CR or an
   LF: the line is tab-separated and newline-terminated, so such a field would silently split into two fields
   or two lines and be read back as a different record. A refusal is a counted event at the call site, never a
   silent truncation. The trailing '\n' is included in *out. */
bool MetaEncodeV5(const MetaLine& m, std::string* out);
bool MetaEncodeV6(const MetaLine& m, std::string* out);
bool MetaEncodeV7(const MetaLine& m, std::string* out);   /* B12: the notebook writes THIS and nothing else */

/* Parse v2, v3, v4, v5, v6 or v7. Returns false for a tag this build does not know, for fewer than 10 fields,
   for a v5/v6 line with fewer than 13, for a v7 line with fewer than 14, for a v7 line whose `seq` field is
   not digits, for an owner field MetaOwnerFieldValid refuses, and for an empty world id. A trailing CR (a line written on one machine and read
   after a text-mode round trip) is stripped from the last field, from worldId and from town. */
bool MetaParse(const std::string& line, MetaLine* out);

/* P8d. WHAT ONE FILE IS, IN THREE ANSWERS, AND THE ONLY THREE THERE ARE. Every reader in the notebook
   produces one of these and nothing else, so no caller can invent a fourth meaning by collapsing two.
   The last one is the trap this enum exists to remove: a file of ZERO BYTES is READABLE with a length of
   0 - that is a fact about the file, not a failure to read it - while a file we could not open is not a
   file we know anything about, and may never decide anything. */
enum FileState
{
    kFileAbsent     = 0,   /* the folder says there is no such name. An ATTRIBUTE QUERY, which a
                              FILE_SHARE_NONE open does not defeat, and only the two not-found errors. */
    kFileUnreadable = 1,   /* THE NAME IS THERE AND WE DO NOT KNOW WHAT IT HOLDS: a sharing violation, a
                              permission, a device error, a directory of that name, a short read, or a
                              file above the reader's own size cap. NEVER AN INPUT TO A WRITE. */
    kFileReadable   = 2    /* the bytes are in hand, and their length may be 0. */
};

/* What a payload file on disk is, judged against what the line says it should be. */
enum PayloadVerdict
{
    kPayloadOk           = 0,   /* the file exists, its length matches and its CRC matches */
    kPayloadNoneExpected = 1,   /* the line claims no payload (len 0) - a position-only note, and correct */
    kPayloadMissing      = 2,   /* the line claims a payload and there is no file */
    kPayloadTorn         = 3,   /* the file is there and is the WRONG LENGTH - a half-finished write */
    kPayloadCrcBad       = 4,   /* the length matches and the bytes do not - a rename that completed onto
                                   different content, or a file damaged in place */
    kPayloadUnreadable   = 5    /* P8d (review-p8c C-2). THE LINE CLAIMS A PAYLOAD AND THE FILE IS THERE
                                   AND UNREADABLE. It is not missing, it is not torn, and it is not ruled
                                   out: nothing may be promoted over it and nothing may be written for
                                   this record until it can be read. Its own counter, because "we refused
                                   a damaged file" and "we could not look" ask for different repairs. */
};
/* `fileState` is one of FileState. A zero-length file is kFileReadable with fileLen 0 and therefore TORN
   against any line that claims bytes - which is what it is. */
int PayloadCheck(long long expectLen, unsigned int expectCrc,
                 int fileState, long long fileLen, unsigned int fileCrc);

/* THE FACTS ABOUT ONE RECORD'S FOUR FILES. The caller gathers them (it owns the disk); this header owns what
   they mean. A meta line below v5 must be presented as ABSENT - it carries no length and no checksum, so it
   cannot validate anything and must not be promoted over one that can.
   P8d: EACH OF THE FOUR CARRIES ITS OWN FileState, and the payload halves carry NOTHING ELSE - there is no
   separate "did we get its bytes" flag to be kept in step with the state by hand (6a lesson 11). A payload's
   length and checksum are meaningful only where its state is kFileReadable. `haveMeta` / `havePrevMeta` stay,
   because they say something the state cannot: the file was read AND its first line parsed as a known
   format naming this record. */
struct FolderFacts
{
    int          metaState;         int haveMeta;      MetaLine meta;        /* <id>.meta */
    int          prevMetaState;     int havePrevMeta;  MetaLine prevMeta;    /* <id>.meta.prev */
    int          payloadState;      long long payloadLen;     unsigned int payloadCrc;      /* <id>.platoon */
    int          prevPayloadState;  long long prevPayloadLen; unsigned int prevPayloadCrc;  /* <id>.platoon.prev */
    FolderFacts();
};

/* THE RULE, STATED ONCE (design-save 1.2(d)): prefer <id>.meta; if it is absent or its len/crc does not match
   <id>.platoon, try <id>.meta.prev against <id>.platoon and then against <id>.platoon.prev; the first pair
   that validates becomes current. A record whose current line parses but whose payload is bad and for which no
   previous version validates is still INDEXED, with no payload - its position is what the notebook has always
   served for a record with no file, and dropping it would lose the group entirely. */
enum ChoiceKind
{
    kChooseNone            = 0,   /* no usable meta line at all - the record is not indexed */
    kChooseCurrent         = 1,   /* <id>.meta is good */
    kChoosePrevMetaCurrent = 2,   /* <id>.meta.prev validates against the CURRENT <id>.platoon (window 2/4) */
    kChoosePrevPair        = 3,   /* <id>.meta.prev validates against <id>.platoon.prev (window 3) */
    kChooseCurrentRefused  = 4,   /* the current line parses, its payload is refused, nothing else validates */
    kChoosePrevRefused     = 5    /* P8c (review-p8b M-1): there is no <id>.meta at all and the previous line's
                                     payload does not validate either. The record is STILL INDEXED, on the
                                     previous line's position, because dropping it loses the group entirely -
                                     which is exactly what pass 1 already refuses to do with the same damage. */
};
/* P8l (review-p8d M-1). ONE ID PER `return` IN ChooseVersion, because THE COVERAGE LEG COUNTED KINDS.
   Two returns produce kChoosePrevRefused and two produce kChooseNone, so making one of them unreachable
   left the other one's hits standing and the leg passed - and arm two (kSiteCurrentPayloadUnreadable, the
   headline repair of P8d) could be DELETED ENTIRELY without changing a single one of the 625 answers,
   because the pass-2 guard catches the same folder from the other side with the same Choice. The sweep now
   requires every site below to be reached, so deleting any arm fails the suite. */
enum ChoiceSite
{
    kSiteMetaUnreadablePrevIndexed = 0,   /* <id>.meta unreadable, a previous line to index it on */
    kSiteMetaUnreadableNoPrev      = 1,   /* <id>.meta unreadable and nothing else to stand on */
    kSiteCurrentOk                 = 2,
    kSiteCurrentNoPayloadExpected  = 3,   /* the current line says len 0 - a position-only record */
    kSiteCurrentPayloadUnreadable  = 4,   /* ARM TWO: an unreadable payload is never promoted over */
    kSiteNoLineAtAll               = 5,
    kSitePrevMetaCurrentOk         = 6,
    kSitePrevMetaCurrentNoPayload  = 7,
    kSitePrevPairOk                = 8,
    kSiteCurrentRefused            = 9,
    kSitePrevRefused               = 10,
    kSiteNothingValidates          = 11,
    kChoiceSiteCount               = 12
};
struct Choice
{
    int kind;      /* one of ChoiceKind */
    int refusal;   /* THE PayloadVerdict THAT REJECTED THE PAIR THAT WAS TRIED. Until P8c this was set only
                      inside the current-line arm, so for a record found only as <id>.meta.prev - no
                      <id>.meta at all - refusedTorn / refusedCrc / refusedMissing could never move
                      (review-p8b H-1). It is now set on the previous-line arm as well, and it still names
                      the CURRENT pair's refusal whenever there was a current pair to refuse. */
    int hasFile;   /* 1 when the chosen pair has a payload the caller may serve, 0 when it is position-only */
    int site;      /* P8l: WHICH `return` produced this Choice - one of ChoiceSite. -1 means an arm was added
                      without tagging itself, which the suite treats as a failure rather than as a zero. */
    Choice();
};
Choice ChooseVersion(const FolderFacts& f);

enum UpgradeKind
{
    kUpgradeDefer        = 0,   /* leave the v2-v4 line EXACTLY AS IT IS and try again at the next start */
    kUpgradePromotePrev  = 1,   /* a validating v5 previous PAIR beats a line that can validate nothing */
    kUpgradeFromPayload  = 2,   /* rewrite as v5 with the length and checksum of the file as found */
    kUpgradePositionOnly = 3    /* there is no payload - rewrite as a position-only v5 line (len 0) */
};
struct UpgradeChoice
{
    int    kind;      /* one of UpgradeKind */
    Choice promote;   /* what ChooseVersion said; meaningful when kind is kUpgradePromotePrev */
    UpgradeChoice();
};
/* THE UPGRADE RULE, STATED ONCE (P8c for (1)-(4); P8d widens (1) and narrows (4)):
   (1) ANY OF THE FOUR FILES that is present and could not be read defers the whole upgrade. P8c applied
       this to <id>.platoon alone, and review-p8c H-1 measured the consequence: a locked <id>.meta.prev
       let a torn payload be checksummed and blessed as truth, permanently, because the next start then
       found a line that validated. The upgrade WRITES, and a write may not rest on a fact we do not have.
       A v2-v4 line left alone is parsed and upgraded at any later start; a line rewritten wrongly is a
       squad lost for good, so deferring is always the cheaper mistake.
   (2) otherwise the previous version is consulted exactly as it is for a v5 line, and a validating .prev
       PAIR is promoted rather than trusting a payload that a v2-v4 line cannot check;
   (3) a .prev line that validates against the CURRENT payload is NOT a promotion - that file is the one the
       line describes, so the newer v2-v4 line is upgraded from it and keeps its own position;
   (4) otherwise: the bytes as found when there are bytes, and a position-only line when there are not -
       which now includes a file that IS on disk with ZERO BYTES IN IT (review-p8c M-1: P8c wrote
       `len 0 / crc 00000000` for it while claiming in two documents that it never did). A zero-byte
       payload carries no squad, so the line that says so is the true one; the caller counts it apart. */
UpgradeChoice UpgradeDecide(const FolderFacts& f);

/* ============ review-p8l H-1 / H-2: THE TWO DECISIONS INSIDE A FAILED ROTATION ==================
   P8l made the rotation ALL OR NOTHING and told the caller, which is right for the case it measured
   and wrong for one it did not separate. The rename that fails is <id>.meta -> <id>.meta.prev, and
   there are two entirely different reasons it can fail:
     * THE SOURCE is shut. Something holds <id>.meta open. The same lock fails WriteMeta at the end of
       the same commit, so the write this rotation was protecting fails ANYWAY - and a payload half
       that had already rotated would strand the last committed squad with no line describing it
       (review-p8d C-2). REFUSE, exactly as P8l does.
     * THE DESTINATION is shut. <id>.meta is writable and <id>.meta.prev is the file held open. P8d
       COMMITTED here; P8l refuses, so a locked .prev turns away both a position update and a new
       squad and the old record is served from memory - worse than the deployed build in that one
       case (review-p8l H-1). Nothing may be rotated there either, but the WRITE GOES AHEAD: the pair
       already on disk is left mutually consistent and describes an older whole version, so a crash
       mid-write still recovers from it. What is lost is the FRESHNESS of the fallback - the same harm
       the payload arm already accepts on purpose.
   And when the PAYLOAD half will not move, exactly one of three things happened, which is H-2: the
   meta half was renamed back (rolledBack), the rename back also failed (rollbackFailed - the one
   state this cannot repair, and it MUST NOT also read as an ordinary rollback), or nothing had been
   rotated at all because there was no <id>.meta to move. P8l booked rolledBack for all three. */
enum RotateFailCause
{
    kRotCauseSourceLocked = 0,   /* <id>.meta itself could not be read */
    kRotCauseDestLocked   = 1,   /* <id>.meta is readable and <id>.meta.prev is the one that is shut */
    kRotCauseUnknown      = 2    /* neither file is unreadable - a permission, a device, a race */
};
int RotateMetaFailCause(int metaState, int prevMetaState);

enum RotateFailAction
{
    kRotActionRefuse          = 0,   /* nothing is rotated AND the write is abandoned */
    kRotActionProceedNoRotate = 1    /* nothing is rotated and the write GOES AHEAD */
};
int RotateMetaFailAction(int cause);

enum RotateBooking
{
    kRotBookNothing        = 0,   /* nothing had been rotated - there is nothing to roll back */
    kRotBookRolledBack     = 1,   /* the meta half was renamed back and the record is as it was */
    kRotBookRollbackFailed = 2    /* the rename back failed too. NEVER also booked as rolledBack. */
};
int RotateFailBooking(int metaMoved, int rollbackOk);

}   /* namespace coopstore */

#endif
