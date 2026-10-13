/* src/common/ownpark.h - A STOLEN MARK WAITING FOR ITS OWNER: the pure decisions.
 *
 * An item can arrive marked "stolen from" a character or a squad (named by uid, ownerwire.h) before that owner exists on this
 * game - a join's catch-up can bring a box's contents a moment before the owner's own spawn. Such a mark is PARKED instead of
 * dropped: the item is remembered by its own engine hand and the place it was put, and the mark goes on once the owner's uid is
 * registered here. These are the rules both the plugin and the offline suite use:
 *   ShouldPark - only a character or squad owner with a uid can turn up later; a faction-only or unknown owner cannot.
 *   Judge      - what the hand names now is the parked item only when it is found, is the same record, is still clean and still
 *                stands at the place it was parked at; anything else is dropped, never applied to another item.
 *   Expired    - a parked mark is given up kParkLimitMs after it was parked (the owner never came).
 *   RetryDue   - between registrations the list is tried every kParkRetryMs.
 * Pure: no engine memory, no Windows. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include "ownerwire.h"

namespace ownpark {

const int kParkCap = 256;               /* parked marks at once; one more is left clean, counted */
const int kNotedCap = 32;               /* uids registered between two ItemsTicks that are tried at once; more wait for the retry */
const unsigned int kParkLimitMs = 120000u;   /* 120 s: a join's catch-up brings the owners well inside it */
const unsigned int kParkRetryMs = 2000u;

inline bool ShouldPark(const coopmark::OwnerId& o)
{
    return o.uid != 0 && (o.kind == coopmark::kOwnCharacter || o.kind == coopmark::kOwnSquad);
}

/* where an item stood when it was parked: in an inventory or not, the section, the cell */
struct Place
{
    int inInv;
    std::string section;
    int x, y;
    Place() : inInv(0), x(-1), y(-1) {}
};

inline bool SamePlace(const Place& a, const Place& b)
{
    return a.inInv == b.inInv && a.section == b.section && a.x == b.x && a.y == b.y;
}

const int kFoundApply = 0;     /* the same item, still clean, where it was: the mark goes on */
const int kFoundGone = 1;      /* the hand names nothing now (destroyed, or moved so the hand no longer names it) */
const int kFoundChanged = 2;   /* another record, or it carries a mark already */
const int kFoundMoved = 3;     /* the same record, but not where it was parked */

inline int Judge(bool found, const std::string& parkedSid, const Place& parkedAt, const std::string& sidNow, bool cleanNow, const Place& atNow)
{
    if (!found) return kFoundGone;
    if (sidNow != parkedSid || !cleanNow) return kFoundChanged;
    if (!SamePlace(parkedAt, atNow)) return kFoundMoved;
    return kFoundApply;
}

/* millisecond tick counts; the subtraction is unsigned, so a counter that wrapped still measures right */
inline bool Expired(unsigned int now, unsigned int since) { return (unsigned int)(now - since) >= kParkLimitMs; }
inline bool RetryDue(unsigned int now, unsigned int last) { return (unsigned int)(now - last) >= kParkRetryMs; }

} // namespace ownpark
