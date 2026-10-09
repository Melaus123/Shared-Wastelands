#ifndef SW_COMMON_LOSTCOPY_H
#define SW_COMMON_LOSTCOPY_H
/* A LOST COPY AND THE ASK TO SEND IT AGAIN (game-to-game MSG_RESEND). Pure: no engine, no network - the offline suite tests every
   decision here.

   A copy is LOST when this game's engine put away its copy of another game's character (the area the copy stood in went to sleep
   here; the copy's temporary group is not saved, so the copy is gone) while the owner still runs the character, or when the owner
   streams a character this game holds no copy of. The owner's announce mark still says "sent", so nothing on the owner's side would
   send it again. This game keeps a row per lost uid (who owns it, where it last stood) and, while that spot is in an area loaded
   here, ASKS the owner to send the character again. The owner sends its full state (CONTEXT, SPAWN, looks, STATE, COMBATMODE,
   INTENT) to this game alone and ANSWERS with a verdict per uid. A uid whose SPAWN this game's own table refused is never booked
   (the owner was told by NOT_SHOWN; a re-send would be refused the same way).

   The asks are limited by state, not by a clock. A VISIT is an unbroken stretch in which the row's spot reads as loaded here and
   stays in one sector (the owner moving the character into another sector starts a new visit); the row lives for the whole
   visit, also while the copy is back (HERE). An ask goes out only while no copy is here (the same test that writes the BACK
   line), never while an earlier ask of the same visit awaits its answer, and inside a visit only from a row
   that is WAITING: an answer of SENT, a refusal (a hand-over in flight, not announced, not the owner's) and a copy that came back
   in this visit all wait for the next visit, because a copy the engine puts away again on the same loaded ground would be put away
   again at once, and the owner's own paths (the hand-over's settle re-send, the announce pass, OWNER_MOVED) send it then. The
   SPAWN that makes the copy marks the row HERE at once (LostArrived), before any look. At most kLostAsksPerVisit asks a visit, an
   ask whose send found no road counted like a sent one, once per road outage. A spot that is not loaded here is never asked for:
   a copy made in a sleeping area would be put away again at once.

   WIRE: kind u8 (kRsAsk / kRsAnswer) | n u8 (1..kResendMax) | n x (uid u32 | verdict u8)      2 + 5n bytes, little-endian.
   In an ASK every verdict is kRvAsk (0). */
#include <vector>
#include <cstddef>

namespace lostcopy {

enum { kRsAsk = 1, kRsAnswer = 2 };
const unsigned int kResendMax = 32;
enum { kRvAsk = 0, kRvSent = 1, kRvHeld = 2, kRvNotAnnounced = 3, kRvNotMine = 4, kRvNoBody = 5, kRvFailed = 6, kRvNoRoad = 7, kRvMax = 7 };
enum { kRsDecodeOk = 0, kRsDecodeShort = 1, kRsDecodeBadKind = 2, kRsDecodeBadCount = 3, kRsDecodeBadUid = 4, kRsDecodeBadVerdict = 5 };

inline const char* ResendVerdictName(int v)
{
    switch (v)
    {
    case kRvAsk:          return "asked";
    case kRvSent:         return "sent again";
    case kRvHeld:         return "held (a hand-over is in flight; sent when it settles)";
    case kRvNotAnnounced: return "not announced by its owner (its announce pass decides)";
    case kRvNotMine:      return "not the answering game's character";
    case kRvNoBody:       return "no readable body on its owner";
    case kRvFailed:       return "its send failed";
    case kRvNoRoad:       return "no road to the asking game";
    default:              return "?";
    }
}

/* False (nothing written) for a bad kind, 0 or more than kResendMax entries, sizes that differ, a uid 0, or a verdict that does not
   fit the kind (an ASK carries kRvAsk only; an ANSWER kRvSent..kRvMax). */
inline bool EncodeResend(std::vector<unsigned char>* out, int kind, const std::vector<unsigned int>& uids, const std::vector<int>& verdicts)
{
    if (out == 0 || (kind != kRsAsk && kind != kRsAnswer) || uids.empty() || uids.size() > kResendMax || uids.size() != verdicts.size()) return false;
    for (size_t i = 0; i < uids.size(); ++i)
    {
        if (uids[i] == 0) return false;
        if (kind == kRsAsk ? verdicts[i] != kRvAsk : (verdicts[i] < kRvSent || verdicts[i] > kRvMax)) return false;
    }
    out->clear();
    out->push_back((unsigned char)kind);
    out->push_back((unsigned char)uids.size());
    for (size_t i = 0; i < uids.size(); ++i)
    {
        for (int b = 0; b < 4; ++b) out->push_back((unsigned char)((uids[i] >> (8 * b)) & 0xFFu));
        out->push_back((unsigned char)verdicts[i]);
    }
    return true;
}
/* Nothing is written on a refusal. Bytes past the n entries are ignored. */
inline int DecodeResend(const unsigned char* p, size_t size, int* kind, std::vector<unsigned int>* uids, std::vector<int>* verdicts)
{
    if (p == 0 || kind == 0 || uids == 0 || verdicts == 0 || size < 2) return kRsDecodeShort;
    const int k = (int)p[0];
    const size_t n = (size_t)p[1];
    if (k != kRsAsk && k != kRsAnswer) return kRsDecodeBadKind;
    if (n == 0 || n > kResendMax) return kRsDecodeBadCount;
    if (size < 2 + 5 * n) return kRsDecodeShort;
    std::vector<unsigned int> u; std::vector<int> v;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char* e = p + 2 + 5 * i;
        unsigned int x = 0;
        for (int b = 0; b < 4; ++b) x |= ((unsigned int)e[b]) << (8 * b);
        if (x == 0) return kRsDecodeBadUid;
        const int vv = (int)e[4];
        if (k == kRsAsk ? vv != kRvAsk : (vv < kRvSent || vv > kRvMax)) return kRsDecodeBadVerdict;
        u.push_back(x); v.push_back(vv);
    }
    *kind = k; uids->swap(u); verdicts->swap(v);
    return kRsDecodeOk;
}

/* THE OWNER: what an ask for one uid gets. SENT means "send its full state to the asker now"; HELD means the hand-over's settle
   sends it; every other verdict sends nothing. */
inline int ResendOwnerVerdict(int uidMine, int handoverPending, int announced, int readsOk)
{
    if (!uidMine) return kRvNotMine;
    if (handoverPending) return kRvHeld;
    if (!announced) return kRvNotAnnounced;
    if (!readsOk) return kRvNoBody;
    return kRvSent;
}

/* THE ASKER: may this uid be booked at all? Never this game's own, never one with no owner on record, never one whose SPAWN this
   game's table refused (a re-send would be refused again). */
inline int LostNoteAllowed(int uidMine, int ownerKnown, int refusedHere)
{
    return (!uidMine && ownerKnown && !refusedHere) ? 1 : 0;
}

/* THE ASKER'S BOOK. */
enum { kLcWaiting = 0, kLcAsked = 1, kLcAnswered = 2, kLcRefused = 3, kLcHere = 4 };
const int kLostAsksPerVisit = 3;
const size_t kLostBookMax = 256;
struct LostRow
{
    unsigned int uid, ownerKey;
    float x, y, z;
    int hasPos;
    int state;            /* kLc* */
    int asksThisVisit;    /* asks made (sent or not) since the spot last became loaded here */
    int loadedPrev;       /* the spot read as loaded here at the last look */
    int asksTotal;        /* asks sent over the row's life */
    int noRoad;           /* the last ask found no road: further no-road asks in the same outage are not counted again */
    int hereMoves;        /* MOVEs seen for the copy since it came back (HERE) */
    int backThisVisit;    /* the copy came back during this visit: a loss in the same visit is not asked for */
};
struct LostBook
{
    std::vector<LostRow> rows;
    long long full;   /* rows dropped (the oldest) to make room */
    LostBook() : full(0) {}
};
/* The sector of one world coordinate: floor((147456 + c) / 4608). */
inline int LostSectorOf(float c)
{
    const float v = (147456.0f + c) / 4608.0f;
    int s = (int)v;
    if ((float)s > v) --s;
    return s;
}
inline int LostFind(const LostBook& b, unsigned int uid)
{
    for (size_t i = 0; i < b.rows.size(); ++i) if (b.rows[i].uid == uid) return (int)i;
    return -1;
}
/* Note (or refresh) a lost uid: its owner and, when known, where it stands now. A new row starts WAITING with no visit; a refreshed
   row keeps its visit and its count (a copy lost again on the same loaded ground spends the same budget) unless its new spot is in
   another sector (the next loaded look starts a new visit), and a HERE row is lost again (WAITING); a row whose owner changed
   starts again (the new owner was never asked). Past kLostBookMax the oldest row goes
   (counted in `full`). Answers 1 for a new row, 0 for a refreshed one, -1 for uid 0. */
inline int LostNote(LostBook* b, unsigned int uid, unsigned int ownerKey, int hasPos, float x, float y, float z)
{
    if (b == 0 || uid == 0) return -1;
    const int i = LostFind(*b, uid);
    if (i >= 0)
    {
        LostRow& r = b->rows[(size_t)i];
        if (r.ownerKey != ownerKey) { r.ownerKey = ownerKey; r.state = kLcWaiting; r.asksThisVisit = 0; r.loadedPrev = 0; r.backThisVisit = 0; }
        if (r.state == kLcHere) r.state = kLcWaiting;
        if (hasPos)
        {
            if (r.hasPos && (LostSectorOf(r.x) != LostSectorOf(x) || LostSectorOf(r.z) != LostSectorOf(z))) r.loadedPrev = 0;
            r.x = x; r.y = y; r.z = z; r.hasPos = 1;
        }
        return 0;
    }
    if (b->rows.size() >= kLostBookMax) { b->rows.erase(b->rows.begin()); ++b->full; }
    LostRow r;
    r.uid = uid; r.ownerKey = ownerKey; r.x = hasPos ? x : 0.0f; r.y = hasPos ? y : 0.0f; r.z = hasPos ? z : 0.0f;
    r.hasPos = hasPos ? 1 : 0; r.state = kLcWaiting; r.asksThisVisit = 0; r.loadedPrev = 0; r.asksTotal = 0; r.noRoad = 0; r.hereMoves = 0;
    r.backThisVisit = 0;
    b->rows.push_back(r);
    return 1;
}

/* One look at a row: DROP it, KEEP it while the copy is here, WAIT, or ASK. `haveCopy`: the copy exists here now; `ownerOk`: its
   owner is on record and is not this game; `loadedHere`: the row's spot reads as loaded here now (a row with no spot is never
   loaded). A copy that is here keeps its row until the visit ends (HERE); the row goes when the copy is here and the visit is
   over, or when the owner record is gone. With no copy here on loaded ground: a new visit asks; an ask that awaits its answer
   waits for the rest of its visit; inside a visit only a WAITING row whose copy has not come back in this visit asks, within the budget. The
   caller then applies LostLookApply to a row it did not drop. */
enum { kLaWait = 0, kLaAsk = 1, kLaDrop = 2, kLaHere = 3 };
inline int LostLookDecide(int haveCopy, int ownerOk, int loadedHere, const LostRow& r)
{
    const int loaded = (loadedHere && r.hasPos) ? 1 : 0;
    if (!ownerOk) return kLaDrop;
    if (haveCopy) return (loaded && r.loadedPrev) ? kLaHere : kLaDrop;
    if (!loaded) return kLaWait;
    if (!r.loadedPrev) return kLaAsk;                                    /* a new visit: every earlier answer is history (an answer lost on the way is covered here) */
    if (r.state == kLcAsked) return kLaWait;                             /* the answer is owed */
    if (r.state != kLcWaiting || r.backThisVisit) return kLaWait;        /* answered, refused, or back in this visit: the next visit */
    return r.asksThisVisit < kLostAsksPerVisit ? kLaAsk : kLaWait;
}
/* The row after a look that did not drop it. A sent ASK counts against the visit (ASKED). An ASK that found no road leaves the row
   WAITING for the next look and counts once per road outage: the first no-road ask of an outage is counted, the ones after it are
   not, until an ask goes out again. A copy that comes back (HERE) starts counting its MOVEs from 0 and marks the visit. A new visit
   starts the count and the mark again; an ask that awaits its answer stays ASKED. */
inline void LostLookApply(LostRow* r, int loadedHere, int action, int sent)
{
    if (r == 0) return;
    const int loaded = (loadedHere && r->hasPos) ? 1 : 0;
    if (action == kLaHere) { if (r->state != kLcHere) r->hereMoves = 0; r->state = kLcHere; r->backThisVisit = 1; r->loadedPrev = loaded; return; }
    if (loaded && !r->loadedPrev) { r->asksThisVisit = 0; r->backThisVisit = 0; if (r->state != kLcAsked) r->state = kLcWaiting; }
    r->loadedPrev = loaded;
    if (action == kLaAsk)
    {
        if (sent) { ++r->asksThisVisit; r->state = kLcAsked; ++r->asksTotal; r->noRoad = 0; }
        else if (!r->noRoad) { ++r->asksThisVisit; r->noRoad = 1; }
    }
}
/* Did this no-road ask start an outage (the first of it - the one worth a line)? Read BEFORE LostLookApply. */
inline int LostNoRoadFirst(const LostRow& r) { return r.noRoad ? 0 : 1; }
/* A copy that has stayed back is trusted again: after kLostHereMovesReset MOVEs of its owner seen while it is HERE (the owner's
   own stream, not a clock), the visit's count and its came-back mark start again, so a real loss much later in the same stay is
   asked for. Answers 1 when the count was reset. */
const int kLostHereMovesReset = 600;
inline int LostHereMoveSeen(LostRow* r)
{
    if (r == 0 || r->state != kLcHere) return 0;
    if (++r->hereMoves < kLostHereMovesReset) return 0;
    r->hereMoves = 0; r->asksThisVisit = 0; r->noRoad = 0; r->backThisVisit = 0;
    return 1;
}
/* An answer counts only from the game the row names as the owner. */
inline int LostAnswerFromOwner(unsigned int rowOwnerKey, unsigned int senderKey)
{
    return (rowOwnerKey != 0 && rowOwnerKey == senderKey) ? 1 : 0;
}
/* The owner's answer for one row (only when LostAnswerFromOwner said so). Only a row that awaits it (ASKED) takes it; a row whose
   copy is already back (HERE) or that was booked again keeps its state. */
inline void LostAnswerApply(LostRow* r, int verdict)
{
    if (r == 0 || r->state != kLcAsked) return;
    r->state = (verdict == kRvSent) ? kLcAnswered : kLcRefused;
}
/* A SPAWN made the copy of a booked uid (or found it live): the row is HERE at once and the visit is marked, so no look after it
   asks while the copy is here or after it is put away again in the same visit. Answers 1 when the row was not HERE (the copy is
   BACK - the line is written for every booked uid that comes back, asked for or not), 0 otherwise. */
inline int LostArrived(LostRow* r)
{
    if (r == 0 || r->state == kLcHere) return 0;
    r->state = kLcHere; r->hereMoves = 0; r->backThisVisit = 1;
    return 1;
}
/* The per-uid log budget: every line for one uid is written for its first kLostLogPerUid events, then every 100th. */
const long long kLostLogPerUid = 12;
inline int LostUidLogThis(long long n)
{
    return (n >= 1 && (n <= kLostLogPerUid || (n % 100) == 0)) ? 1 : 0;
}

/* THE COPY'S TABLE: which refusals of a SPAWN would repeat on a re-send. Only those keep a uid from being booked: a template not in
   this game's data, a faction name that is not a player faction and did not resolve (a player faction, "@...", may appear later),
   the TEST-ONLY table cap. Everything else (no world yet, no reference character, the table really full, the engine's create
   answering nothing) passes, and the uid is booked and asked for within the visit's budget. */
enum { kCrNone = 0, kCrNoTemplate = 1, kCrFactionUnknown = 2, kCrTableTestCap = 3 };
inline int CreateRefusalLasting(int why) { return (why == kCrNoTemplate || why == kCrFactionUnknown || why == kCrTableTestCap) ? 1 : 0; }
inline int WireFactionMayAppear(const char* name) { return (name != 0 && name[0] == '@') ? 1 : 0; }

/* THE COPY'S TABLE: a SPAWN for a uid whose old row this game still keeps. The old row is STALE - it gives way, so the SPAWN makes
   a copy - only when all hold: the SPAWN answers this game's own RESEND ask for the uid, the uid is another game's, the row is
   retired (FindSpawned no longer answers it), its handle does not resolve to its object now, its object is not in the engine's
   list of running characters (the second witness), and the SPAWN's spot is loaded here (the engine's own "zone loaded" byte; an
   area awake here would have brought a merely streamed-out copy back). Otherwise the SPAWN repeats a copy this game has, or will
   restore, and is ignored. */
inline int StaleRowGivesWay(int askedForIt, int uidMine, int rowRetired, int handleResolves, int inRunningList, int spawnSpotLoaded)
{
    return (askedForIt && !uidMine && rowRetired && !handleResolves && !inRunningList && spawnSpotLoaded) ? 1 : 0;
}

/* THE RETURN CHECK. While this game was off the world-server link, an owner's withdrawal of a character (its UNLOAD when the
   character's area went away from this game) reached nobody, and the roster check on return answers only "I run it": the copy stays
   standing where the owner left it. At the first welcomed link after a drop (ReturnEdge: a new link generation after an earlier one)
   every copy here of a world-road player's character is booked. Each look (once a second): a copy its owner has streamed to this game
   since the return is KEPT (the owner still announces it here); a silent copy is ASKED of its owner (MSG_RESEND, as a lost copy is);
   the answer decides - NOT ANNOUNCED withdraws it (the owner runs it but has not told this game: the withdrawal was missed), SENT keeps
   it (the owner sent its state again; the stale-copy rule places or retires it), NOT MINE keeps it (another game runs it; the roster's
   MOVED word re-keys it). Any other answer, or none within kReturnAnswerLooks looks with the road up, asks again; after
   kReturnAsksMax sent asks (or kReturnUnsentMax asks that found no road - those are not counted as asks) the row ends and the copy is
   KEPT (logged): a copy that may be live is never taken on silence. The one exception at that end: an owner absent from this link's
   world roster for longer than the world server's hold plus a margin (ReturnGiveUpOwnerGone) left while this game was away - its
   PLAYER_GONE went only to the games connected then - and the caller handles that player's leave here as PLAYER_GONE would, without
   taking any NPC here (the games connected then made the take). A copy no longer here, or
   one whose owner record changed or went, ends its row. */
enum { kRtWaiting = 0, kRtAsked = 1, kRtAnswered = 2 };
enum { kRtaWait = 0, kRtaAsk = 1, kRtaDrop = 2, kRtaKeepHeard = 3, kRtaKeepAnswered = 4, kRtaWithdraw = 5, kRtaGiveUp = 6 };
const int kReturnAsksMax = 3;
const int kReturnAnswerLooks = 5;
const int kReturnUnsentMax = 10;
const size_t kReturnBookMax = 1024;
struct ReturnRow { unsigned int uid, ownerKey; int state, asks, looksWaiting, verdict, unsent; };
inline bool ReturnEdge(long prevGen, long nowGen) { return prevGen != 0 && nowGen != 0 && nowGen != prevGen; }
inline int ReturnLookDecide(int haveCopy, int ownerSame, int heardSinceReturn, int roadUp, const ReturnRow& r)
{
    if (!haveCopy || !ownerSame) return kRtaDrop;
    if (heardSinceReturn) return kRtaKeepHeard;
    if (r.state == kRtAnswered) return r.verdict == kRvNotAnnounced ? kRtaWithdraw : kRtaKeepAnswered;
    if (!roadUp) return kRtaWait;
    if (r.state == kRtAsked && r.looksWaiting < kReturnAnswerLooks) return kRtaWait;
    return (r.asks < kReturnAsksMax && r.unsent < kReturnUnsentMax) ? kRtaAsk : kRtaGiveUp;
}
/* A sent ask counts and awaits its answer; an ask whose send found no road is not an ask - it is counted apart (kReturnUnsentMax
   bounds those) and the row asks again at the next look. A look while an answer is owed counts toward its wait only with the road up. */
inline void ReturnLookApply(ReturnRow* r, int action, int sent, int roadUp)
{
    if (r == 0) return;
    if (action == kRtaAsk) { if (sent) { ++r->asks; r->state = kRtAsked; } else { ++r->unsent; r->state = kRtWaiting; } r->looksWaiting = 0; return; }
    if (action == kRtaWait && r->state == kRtAsked && roadUp) ++r->looksWaiting;
}
/* The owner's answer (only from the owner on record - LostAnswerFromOwner): only a row that awaits it takes it. A deciding answer is
   kept; any other asks again. */
inline void ReturnAnswerApply(ReturnRow* r, int verdict)
{
    if (r == 0 || r->state != kRtAsked) return;
    if (verdict == kRvNotAnnounced || verdict == kRvSent || verdict == kRvNotMine) { r->state = kRtAnswered; r->verdict = verdict; }
    else r->state = kRtWaiting;
}
/* At the end of a row's asks: has its owner left while this game was away? ownerInWorld: StoreRosterSlotInWorld of the owner's slot
   (1 in the world, 0 not, -1 no roster of this link); absentSec: how long this link's roster has read it absent, without a break. Only
   an owner absent for at least holdSec has left by the world server's own rule (the caller passes the world server's hold plus
   peergone.h kReturnAwayMarginSec). */
inline int ReturnGiveUpOwnerGone(int ownerInWorld, double absentSec, double holdSec)
{
    return (ownerInWorld == 0 && absentSec >= holdSec) ? 1 : 0;
}
/* THE RETURN CHECK'S OWN PEOPLE: does a person this game runs go to another game? answerLive: that game answers it runs the person
   now (LIVE); dualRunYields: the dual-run rule (liveowner.h DualRunResolve) gives it to that game - the higher generation. Only a LIVE
   answer gives a person up: a NOT-LIVE answer (that game took it and put it away) or a MOVED one (that game records another owner)
   never takes a person this game runs - this game runs it, and the roster's own check settles the record. */
inline int ReturnOwnYield(int answerLive, int dualRunYields)
{
    return (answerLive != 0 && dualRunYields != 0) ? 1 : 0;
}

}   /* namespace lostcopy */

#endif
