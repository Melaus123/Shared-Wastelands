/* src/common/firsthold.h - T-1449 attempt 2 (design D1, F1829): THE FIRST-BUILD HOLD'S BOOK - which fresh copies wait for their owner's
 * look and kit before their first body build, and every event that ends that wait.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or keeps a clock - the
 * caller passes "now" (only to measure waits; nothing here is released by time). The plugin (appearance.cpp) keeps one Book on the
 * main thread and logs what these functions return; the offline suite drives the same functions.
 *
 * THE EVENTS. Begin (a copy made from a SPAWN, no body yet); BuildStep (each createBody call: held while marked and not released;
 * runs when released, owned, or a body is already there); PreBuild (may a look / kit go on before the build - a refusal other than
 * 'it already has a body' RELEASES the hold, because the ordinary road waits for a built body and the build is held: a copy would
 * otherwise stay invisible); SexRaceRoad (attempt 3: a look that changes the copy's sex or race goes on before the build too, unless an
 * item record already holds a model); LookPre / SexRaceBefore / LookSkipped / KitPre (the owner's look and kit went on before the build -
 * the kit releases);
 * NoKit (no kit will come: released once the look is done or no look waits); Release (no look, the owner left, the owner changed, a
 * refused record); Forget / ForgetAll (the copy or the world is gone).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_FIRSTHOLD_H
#define COOP_COMMON_FIRSTHOLD_H

#include <cstdio>
#include <map>
#include <string>
#include "copybody.h"

namespace firsthold {

enum
{
    kWhyNone = 0, kWhyKit = 1, kWhyNoLook = 2, kWhyNoKit = 3, kWhyLookFailed = 4, kWhyKitFailed = 5, kWhyKitDropped = 6,
    kWhyOwnerGone = 7, kWhyOwnerChanged = 8, kWhyBuildUnderway = 9, kWhyReadFault = 10, kWhyKitBusy = 11, kWhyCount = 12
};

inline const char* WhyText(int w)
{
    switch (w)
    {
    case kWhyKit: return "its owner's kit went on";
    case kWhyNoLook: return "the owner said no look / no kit";
    case kWhyNoKit: return "no kit will come (its look is done)";
    case kWhyLookFailed: return "its owner's look did not apply";
    case kWhyKitFailed: return "its owner's kit did not apply";
    case kWhyKitDropped: return "its owner's kit was dropped (the owner moved a worn item)";
    case kWhyOwnerGone: return "its owner left";
    case kWhyOwnerChanged: return "its owner changed (a hand-over) - the new owner's look is not waited for";
    case kWhyBuildUnderway: return "a body build is already under way (+0xE0 / +0xE8 set) - the early look / kit cannot go on";
    case kWhyReadFault: return "its appearance could not be read - the early look / kit cannot go on";
    case kWhyKitBusy: return "something on this game still holds one of its items (a window / the cursor / a request) - its kit goes on after the build";
    default: return "?";
    }
}

struct Entry
{
    unsigned long since, releasedAt;
    int why;
    long calls;
    bool released, lookPre, lookSkipped, kitPre, noKit, sexRacePre;
};

struct Book
{
    std::map<unsigned int, Entry> m;
    long begun, overwrote, heldUids, heldCalls, runOwned, hadBody, builtKit, builtOther;
    long droppedGone, droppedReleased, droppedWorld, lookPre, lookSkipped, kitPre;
    long sexRacePre, builtSexRacePre;   /* attempt 3: sex / race looks put on before the build; first builds that ran with one on */
    long rel[kWhyCount];
    long long waitSum;
    unsigned long waitMax;
    Book() : begun(0), overwrote(0), heldUids(0), heldCalls(0), runOwned(0), hadBody(0), builtKit(0), builtOther(0),
             droppedGone(0), droppedReleased(0), droppedWorld(0), lookPre(0), lookSkipped(0), kitPre(0),
             sexRacePre(0), builtSexRacePre(0), waitSum(0), waitMax(0)
    { for (int i = 0; i < kWhyCount; ++i) rel[i] = 0; }
    long Released() const { long n = 0; for (int i = 1; i < kWhyCount; ++i) n += rel[i]; return n; }
};

/* A copy made from a SPAWN with no body yet. 1 = a mark of this uid was still there (its old copy was not forgotten) - replaced by
   the new copy's fresh mark (the old one's look / kit belonged to the old body); the caller logs it. */
inline int Begin(Book* b, unsigned int uid, unsigned long now)
{
    Entry e;
    e.since = now; e.releasedAt = 0; e.why = kWhyNone; e.calls = 0;
    e.released = false; e.lookPre = false; e.lookSkipped = false; e.kitPre = false; e.noKit = false; e.sexRacePre = false;
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    const int over = (it != b->m.end()) ? 1 : 0;
    if (over != 0) ++b->overwrote;
    b->m[uid] = e;
    ++b->begun;
    return over;
}

inline bool Has(const Book& b, unsigned int uid) { return b.m.find(uid) != b.m.end(); }
inline bool HeldNow(const Book& b, unsigned int uid)
{
    std::map<unsigned int, Entry>::const_iterator it = b.m.find(uid);
    return it != b.m.end() && !it->second.released;
}
/* the look is done for the hold: it went on before the build, or it was skipped (attempt 3: only when an item record already held a
   model or the records could not be read - SexRaceRoad's kSexRaceAfter; it goes on after the build) */
inline bool LookDone(const Book& b, unsigned int uid)
{
    std::map<unsigned int, Entry>::const_iterator it = b.m.find(uid);
    return it != b.m.end() && (it->second.lookPre || it->second.lookSkipped);
}

/* true = released now (msOut: the wait since Begin). A released or unknown uid: false. */
inline bool Release(Book* b, unsigned int uid, int why, unsigned long now, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return false;
    it->second.released = true;
    it->second.why = why;
    it->second.releasedAt = now;
    const unsigned long ms = now - it->second.since;
    if (why > 0 && why < kWhyCount) ++b->rel[why];
    b->waitSum += (long long)ms;
    if (ms > b->waitMax) b->waitMax = ms;
    if (msOut != 0) *msOut = ms;
    return true;
}

/* No kit will come on this road. lookPending: a look for uid waits in the queue and is not done yet. Released now when the look is
   done or none waits; else the look's apply releases it (LookPre). */
inline bool NoKit(Book* b, unsigned int uid, int why, bool lookPending, unsigned long now, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return false;
    it->second.noKit = true;
    if (it->second.lookPre || it->second.lookSkipped || !lookPending) return Release(b, uid, why, now, msOut);
    return false;
}

/* The owner's look went on before the build (ok) or failed. true = released now (whyOut: why). */
inline bool LookPre(Book* b, unsigned int uid, bool ok, unsigned long now, int* whyOut, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return false;
    if (!ok) { if (whyOut != 0) *whyOut = kWhyLookFailed; return Release(b, uid, kWhyLookFailed, now, msOut); }
    if (!it->second.lookPre) ++b->lookPre;
    it->second.lookPre = true;
    if (it->second.noKit) { if (whyOut != 0) *whyOut = kWhyNoKit; return Release(b, uid, kWhyNoKit, now, msOut); }
    return false;
}

/* The early look is skipped (attempt 3: SexRaceRoad said kSexRaceAfter - a sex / race change while an item record already holds a model
   or cloth part, or the records could not be read): the look goes on after the build; the kit may still go on before it. true = released
   now (no kit was coming). */
inline bool LookSkipped(Book* b, unsigned int uid, unsigned long now, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return false;
    if (!it->second.lookSkipped) ++b->lookSkipped;
    it->second.lookSkipped = true;
    if (it->second.noKit) return Release(b, uid, kWhyNoKit, now, msOut);
    return false;
}

/* T-1449 attempt 3 (F1864 + its check) - A LOOK THAT CHANGES THE COPY'S SEX OR RACE, BEFORE ITS FIRST BUILD.
   The look apply's sex step (decomp_5378d0.txt:79-81 -> appearance vt+0x28 = 0x52D530 -> 0x52D0F0) calls 0x5314B0 on every attached
   item record, which destroys the record's model (+0x80) and cloth part (+0xF0) (decomp_5314b0.txt:41, 82-83, 145-149): after the
   first build that drops the models the build has just made, and a look still loading loses its only wearer and stays black (T2758).
   Before the first build the step needs no body (Read): its other reads are the attach-point table (0x52CDC0, data and the record's
   bone name / offset), the item's slot +0x340 (empty for Item / Weapon / Armour, decomp_d22e0.txt); a record's model is made only by
   0x536C00 and its cloth part only by 0x535490, both reached from the attach step 0x537BA0 that returns at once with no body (+0xD8,
   decomp_537ba0.txt:47-49; decomp_535490.txt:86-87) - and from 0x537200 (the show-helmet step, 0x5EB2C0). The race step (character
   vt+0x3B8 = 0x5C8940) sets the race and rebuilds the anatomy table (decomp_5c8940.txt, decomp_64d280.txt), no items; the rebuild mark
   +0x143 it leaves is cleared by the first build itself (decomp_539440.txt:122).
   So the decision is made from the record list itself at the moment of the apply: change = the look changes sex or race (1), not (0),
   unread (-1, judged as a change); recordsRead = the attached records walked (-1 unread); withModel / withCloth = records holding a model
   / a cloth part. kSexRaceNo = no change (the ordinary early look); kSexRaceBefore = put on before the build; kSexRaceAfter = a record
   already holds a model or cloth part, or the walk failed: the step would destroy something - the look goes on after the build
   (LookSkipped, attempt 2's road; counted, expected 0). */
enum { kSexRaceNo = 0, kSexRaceBefore = 1, kSexRaceAfter = 2 };
inline int SexRaceRoad(int change, int recordsRead, int withModel, int withCloth)
{
    if (change == 0) return kSexRaceNo;
    if (recordsRead < 0 || withModel != 0 || withCloth != 0) return kSexRaceAfter;
    return kSexRaceBefore;
}
/* fold 1 (review MINOR 1): the after road is decided ONCE - a copy whose look was skipped (LookSkipped) keeps it for after the build;
   a later tick (the kit still to come) never re-decides it to before. */
inline bool WasSkipped(const Book& b, unsigned int uid)
{
    std::map<unsigned int, Entry>::const_iterator it = b.m.find(uid);
    return it != b.m.end() && it->second.lookSkipped;
}
/* A sex / race look went on before the build (kSexRaceBefore and the apply took): marked on the copy (its first build counts it). */
inline void SexRaceBefore(Book* b, unsigned int uid)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.sexRacePre) return;
    it->second.sexRacePre = true;
    ++b->sexRacePre;
}

/* The owner's kit went on before the build (ok) or failed: either way released. */
inline bool KitPre(Book* b, unsigned int uid, bool ok, unsigned long now, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return false;
    if (ok) { it->second.kitPre = true; ++b->kitPre; }
    return Release(b, uid, ok ? kWhyKit : kWhyKitFailed, now, msOut);
}

/* PreBuild: may a look / kit for uid go on before its first build? readOk: the appearance was read; e / e2 / creating: +0xD8 / +0xE0 /
   +0xE8 set (+0xE8 and +0xE0 are set only inside createBody itself, decomp_539440.txt:123 / 297 - a build that the hold did not stop).
   kPreApply = go on now; kPreHadBody = a body already (the mark goes; the ordinary road applies it); kPreReleased = refused for another
   reason - the hold is RELEASED (whyOut) so the build runs and the ordinary road applies it after; kPreNone = not held. */
enum { kPreNone = 0, kPreApply = 1, kPreHadBody = 2, kPreReleased = 3 };
inline int PreBuild(Book* b, unsigned int uid, int readOk, int e, int e2, int creating, unsigned long now, int* whyOut, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end() || it->second.released) return kPreNone;
    if (readOk == 0) { if (whyOut != 0) *whyOut = kWhyReadFault; Release(b, uid, kWhyReadFault, now, msOut); return kPreReleased; }
    if (coopbody::FirstPreBuildOk(1, 0, e, e2, creating) != 0) return kPreApply;
    if (e != 0) { ++b->hadBody; b->m.erase(it); return kPreHadBody; }
    if (whyOut != 0) *whyOut = kWhyBuildUnderway;
    Release(b, uid, kWhyBuildUnderway, now, msOut);
    return kPreReleased;
}

/* One createBody call (main thread). Returns coopbody::kFirst*; on a run or a body the mark goes (copied to *gone first).
   *firstHeld = 1 on the first held call of this uid. */
inline int BuildStep(Book* b, unsigned int uid, int owned, int entity, int fault, Entry* gone, int* firstHeld)
{
    if (firstHeld != 0) *firstHeld = 0;
    std::map<unsigned int, Entry>::iterator it = (uid != 0) ? b->m.find(uid) : b->m.end();
    if (it == b->m.end()) return coopbody::kFirstNone;
    const int d = coopbody::FirstBuildDecide(1, 1, it->second.released ? 1 : 0, owned, entity, fault);
    if (d == coopbody::kFirstWait)
    {
        ++b->heldCalls;
        if (it->second.calls++ == 0) { ++b->heldUids; if (firstHeld != 0) *firstHeld = 1; }
        return d;
    }
    if (d == coopbody::kFirstNone) return d;
    if (gone != 0) *gone = it->second;
    if (d == coopbody::kFirstRunReleased) { if (it->second.kitPre) ++b->builtKit; else ++b->builtOther; }
    else if (d == coopbody::kFirstRunOwned) ++b->runOwned;
    else ++b->hadBody;
    if ((d == coopbody::kFirstRunReleased || d == coopbody::kFirstRunOwned) && it->second.sexRacePre) ++b->builtSexRacePre;   /* attempt 3 */
    b->m.erase(it);
    return d;
}

/* The copy is gone. 0 = no mark, 1 = dropped while held (unreleased), 2 = dropped after its release (not built yet). */
inline int Forget(Book* b, unsigned int uid, unsigned long now, unsigned long* msOut)
{
    std::map<unsigned int, Entry>::iterator it = b->m.find(uid);
    if (it == b->m.end()) return 0;
    const int r = it->second.released ? 2 : 1;
    if (r == 1) ++b->droppedGone; else ++b->droppedReleased;
    if (msOut != 0) *msOut = now - it->second.since;
    b->m.erase(it);
    return r;
}
inline void ForgetAll(Book* b)
{
    for (std::map<unsigned int, Entry>::const_iterator it = b->m.begin(); it != b->m.end(); ++it)
        if (!it->second.released) ++b->droppedWorld;
    b->m.clear();
}

/* What is open now, for the REPORT (visibility only - nothing is released by it). */
struct OpenNow { long held, heldLookDone, releasedNotBuilt; unsigned long oldestHeldMs; };
inline OpenNow Open(const Book& b, unsigned long now)
{
    OpenNow o; o.held = 0; o.heldLookDone = 0; o.releasedNotBuilt = 0; o.oldestHeldMs = 0;
    for (std::map<unsigned int, Entry>::const_iterator it = b.m.begin(); it != b.m.end(); ++it)
    {
        if (it->second.released) { ++o.releasedNotBuilt; continue; }
        ++o.held;
        if (it->second.lookPre || it->second.lookSkipped) ++o.heldLookDone;
        const unsigned long ms = now - it->second.since;
        if (ms > o.oldestHeldMs) o.oldestHeldMs = ms;
    }
    return o;
}

/* T-1449 attempt 2 diagnostic (F1851, log only): a held copy's FIRST BUILD - when it ran and whether its owner's look was skipped
   before it for a sex / race change (LookSkipped) - kept until the copy goes, for the look applied after that build ([FIRSTBUILD]
   look after first build) and the copy's body rebuilds ([ITEMLOOK] REBUILD). Nothing here changes what the hold does. */
const unsigned long kAfterYoungMs = 2000;   /* an after-build look apply this soon after the first build: counted under2s */
const long kAfterLineCap = 40;
struct Built { unsigned long at; int sexRace; int lookTold; Built() : at(0), sexRace(0), lookTold(0) {} };
typedef std::map<unsigned int, Built> BuiltBook;
struct AfterCounts { long applies, sexRace, under2s, set143, lines; AfterCounts() : applies(0), sexRace(0), under2s(0), set143(0), lines(0) {} };

/* the first build of a held copy ran now (BuildStep's kFirstRunReleased / kFirstRunOwned); sexRace = its look was skipped before it */
inline void NoteBuilt(BuiltBook* b, unsigned int uid, unsigned long now, int sexRace)
{
    if (uid == 0) return;
    Built x;
    x.at = now; x.sexRace = sexRace != 0 ? 1 : 0;
    (*b)[uid] = x;
}
/* ms since the copy's first build and its sexRace; -1 / -1 when it has none (not a held copy, or gone) */
inline void SinceBuilt(const BuiltBook& b, unsigned int uid, unsigned long now, long long* ms, int* sexRace)
{
    *ms = -1; *sexRace = -1;
    std::map<unsigned int, Built>::const_iterator it = b.find(uid);
    if (it == b.end()) return;
    *ms = (long long)(unsigned long)(now - it->second.at);
    *sexRace = it->second.sexRace;
}
/* a look applied after the build (the ordinary road): 1 = the FIRST such apply of a held copy (counted; ms / sexRace filled), else 0.
   set143: 1 the apply set the rebuild byte, 0 not, 2 it was set already, -1 unread */
inline int AfterBuildLook(BuiltBook* b, AfterCounts* c, unsigned int uid, unsigned long now, int set143, long long* ms, int* sexRace)
{
    *ms = -1; *sexRace = -1;
    std::map<unsigned int, Built>::iterator it = b->find(uid);
    if (it == b->end() || it->second.lookTold != 0) return 0;
    it->second.lookTold = 1;
    *ms = (long long)(unsigned long)(now - it->second.at);
    *sexRace = it->second.sexRace;
    ++c->applies;
    if (*sexRace == 1) ++c->sexRace;
    if (*ms < (long long)kAfterYoungMs) ++c->under2s;
    if (set143 == 1) ++c->set143;
    return 1;
}
inline std::string FhNum(long long v) { char t[32]; std::sprintf(t, "%lld", v); return t; }
/* [FIRSTBUILD] uid=N look after first build: msSinceFirstBuild=X sexRaceChange=1/0 set143=1/0/already/?; 40 lines at most */
inline std::string AfterBuildLookLine(unsigned int uid, long long ms, int sexRace, int set143)
{
    const char* s143 = set143 == 1 ? "1" : set143 == 0 ? "0" : set143 == 2 ? "already" : "?";
    return "[FIRSTBUILD] uid=" + FhNum((long long)uid) + " look after first build: msSinceFirstBuild=" + FhNum(ms) + " sexRaceChange="
         + FhNum((long long)sexRace) + " set143=" + s143 + "; " + FhNum((long long)kAfterLineCap) + " lines at most";
}
/* attempt 3: " sexRace[before,builtWithLook,after]=" - sex / race looks put on before the first build, first builds that ran with one on,
   and those left for after the build (LookSkipped, kSexRaceAfter) */
inline std::string SexRaceToken(const Book& b)
{
    return " sexRace[before,builtWithLook,after]=" + FhNum((long long)b.sexRacePre) + "," + FhNum((long long)b.builtSexRacePre) + ","
         + FhNum((long long)b.lookSkipped);
}
/* fold 1 (review MINOR 2): a look applied on a copy WITH a body that changes its sex and / or race (the mid-life road: the sex step
   drops every attached item model, F1864). sexFlip / raceFlip 1 / 0; msSinceFirstBuild -1 = not a held copy's first build on record;
   models = attached records holding a model before the apply (-1 unread). */
const long kMidLifeLineCap = 40;
inline std::string MidLifeLine(unsigned int uid, int sexFlip, int raceFlip, long long msSinceFirstBuild, int models, int firstLook)
{
    const char* what = (sexFlip != 0 && raceFlip != 0) ? "sex and race" : (sexFlip != 0 ? "sex" : "race");
    return "[FIRSTBUILD] uid=" + FhNum((long long)uid) + " look on a BUILT copy changes its " + what + " (mid-life road): msSinceFirstBuild="
         + (msSinceFirstBuild < 0 ? std::string("?") : FhNum(msSinceFirstBuild)) + " attachedModels=" + (models < 0 ? std::string("?") : FhNum((long long)models))
         + " firstLook=" + FhNum((long long)(firstLook != 0 ? 1 : 0)) + (sexFlip != 0 ? " - its sex step drops every attached item model" : " - a full rebuild is marked")
         + "; " + FhNum((long long)kMidLifeLineCap) + " lines at most";
}
inline std::string AfterToken(const AfterCounts& c)
{
    return " lookAfterFirstBuild[applies,sexRace,under2s,set143,lines]=" + FhNum((long long)c.applies) + "," + FhNum((long long)c.sexRace) + ","
         + FhNum((long long)c.under2s) + "," + FhNum((long long)c.set143) + "," + FhNum((long long)c.lines);
}

}   /* namespace firsthold */

#endif
