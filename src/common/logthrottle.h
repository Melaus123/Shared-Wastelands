#pragma once
/* THE WORLD SERVER'S LOG AND DISK BOUNDS (security audit 2026-10-31, finding 2: any player could grow the log and the world folder
   without limit). Pure: no disk, no clock (the caller passes `now` in seconds), no engine; the world server (store_main.cpp) and the
   offline suite compile it. C++03 (VS2010 v100).

   COUNTED LINES: a line any game can trigger with no limit is logged the first five times and then every hundredth (CountedLine) -
     the rule the server's research-work lines already use.
   A PER-CONNECTION LINE BUDGET (LineTake): a refusal line naming one connection (a BANK request, an ADMIN_* message the server
     ignores) takes from that connection's budget - kLineBurst at once, then one every 1 / kLinePerSec seconds. A line over the
     budget is not written; the next one that is says how many were held back.
   BANK WRITES (kBankBurst / kBankPerSec): a deposit or withdraw that would rewrite teams.txt takes from its connection's bank
     allowance; one over it is answered "not saved" - nothing changed, nothing remembered - and the game sends it again by itself.
   THE RECORDER (spawnlog-YYYYMMDD.tsv): each game's lines take from its own allowance (kRecBurst / kRecPerSec); the lines waiting
     for the next append are capped at kRecBufMax bytes; and one day file never grows past kSpawnlogDayBytesMax (DayFileRoom). The
     recorder is lossy telemetry - a line over any of these is dropped and counted. */
#include "storegate.h"   /* storegate::Allowance / AllowanceTake: the per-connection allowance the entry gate already uses */

namespace logthrottle {

/* the first five, then every hundredth (n counts from 1) */
inline bool CountedLine(long long n) { return n >= 1 && (n <= 5 || n % 100 == 0); }

const double kLineBurst = 20.0, kLinePerSec = 0.2;   /* 20 lines at once, then one every 5 s, per connection */
struct LineBudget { storegate::Allowance a; long long held; LineBudget() : held(0) {} };
/* true = write this line; *heldBefore = the lines of this budget not written since the last one that was (then counted afresh) */
inline bool LineTake(LineBudget* b, double now, long long* heldBefore)
{
    *heldBefore = 0;
    if (!storegate::AllowanceTake(&b->a, now, kLineBurst, kLinePerSec)) { ++b->held; return false; }
    *heldBefore = b->held;
    b->held = 0;
    return true;
}

const double kBankBurst = 20.0, kBankPerSec = 0.5;   /* 20 deposits / withdraws at once, then one every 2 s, per connection */

const double kRecBurst = 4000.0, kRecPerSec = 100.0;   /* recorder lines per game: a whole area's spawns at a load, then 100 a second */
const size_t kRecBufMax = 4u * 1024u * 1024u;          /* bytes waiting for the next append */
const long long kSpawnlogDayBytesMax = 64LL * 1024 * 1024;   /* one day file's most */

enum { kDayAppend = 0, kDayFull = 1 };
/* may a batch of `add` bytes go into a day file that holds `have` bytes, under `cap`? Whole batches only: one that does not fit is
   dropped (kDayFull), so a day file never ends in half a line. A size not known (negative) is full. */
inline int DayFileRoom(long long have, long long add, long long cap)
{
    if (have < 0 || add < 0 || cap < 0) return kDayFull;
    return add <= cap - have ? kDayAppend : kDayFull;
}

}   /* namespace logthrottle */
