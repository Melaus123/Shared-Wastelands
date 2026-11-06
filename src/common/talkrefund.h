/* src/common/talkrefund.h - T-858 (ported from pull request 4, task 5): A PAID CONVERSATION NEVER TAKES THE MONEY AND GIVES NOTHING.
 *
 * In a forwarded conversation the NPC's game (A) runs the NPC's side of a line and the talker's game (B) the talker's side - the
 * TAKE_MONEY of a bribe or a paid service (ACT -> ACT_RESULT, speech.cpp). A holds the line's NPC-side part until B's ACT_RESULT.
 * If that answer is lost or late, A ends the conversation with nothing of the line run, while B has already taken the money.
 *
 * The fix, decided here and pure so the suite tests it:
 *   - B books every ACT it applied that took money: (peer, conversation, ACT seq) -> the amount taken (TalkRefundAmount: the purse's
 *     drop B measured itself, never more than the line's TAKE_MONEY total) and the money ledger it was taken from (base).
 *   - Every END A sends lists the ACT seqs whose NPC-side part that end leaves unrun (TalkMsg::unrun, game protocol 194).
 *     Every end of a forwarded conversation sends END while the link is up - also when the talker's game ended it first (its
 *     ANSWER CLOSED / BUSY / NO_CHAR / SHOW_FAILED: END TALKER_CLOSED, talkwire.h TalkEndForAnswer - T1914 lost 200 cats there).
 *   - B, on an END: refunds the listed seqs it booked, ONCE, and forgets every row of that conversation (an unlisted seq's NPC part
 *     ran). A repeated END, a repeated ACT_RESULT or a reconnect finds nothing left to refund.
 *   - A late ACT_RESULT runs nothing on A (the conversation is gone): the money comes back by the END's list, never both, never neither.
 *   - A row lives as long as its conversation: until that conversation's END, or until the link to that player goes (ForgetPeer -
 *     no END can come any more; nothing is refunded for a lost link, owner decision 1404). No clock: a conversation has no time limit.
 *     A's conversation and ACT numbers start again at 1 when A's game restarts, and a restart does NOT always end the link first:
 *     the world server holds PLAYER_GONE 10 s and cancels it when that player is admitted again, telling the other games nothing
 *     (store_main.cpp PlayerGoneHoldStart / PlayerGoneHoldReturned; no "admitted again" message reaches a game). So a row of the
 *     old launch can meet the new launch's numbers: Note OVERWRITES a row with the same (player, conversation, ACT) numbers - the
 *     newest payment wins - so it never blocks the new booking and the new END refunds the new amount, never the old one (review
 *     2026-11-03 LOW 1). An old row the new launch never re-numbers waits for that player's link end or the 64-row cap (counted).
 *   - Every bounded list here drops with a count on the status line (review LOW 2 / 3): the book's cap (evicted), an END's
 *     16-seq list (TalkUnrunAdd), A's dropped-line notes and B's waiting refunds (TalkPushCapped).
 * Money only: items handed to an NPC are not booked. C++03 (VS2010). */
#pragma once
#include <vector>

namespace cooptalk
{
const unsigned int kTalkMaxUnrun = 16;    /* END: unrun seqs carried (A holds at most 16 lines: speech.cpp kTkActHoldCap) */
const int kTalkTakeMoneyType = 8;         /* DialogActionEnum TAKE_MONEY (talkwire.h's per-action table) */
const size_t kTalkRefundDueCap = 64;      /* B: refunds waiting for the money to be movable (speech.cpp g_tkRefundDue); the oldest goes, counted */

/* Push x onto *v, bounded: at cap the oldest goes first and is counted in *lost. 1 = one went to make room. */
template <class T> inline int TalkPushCapped(std::vector<T>* v, const T& x, size_t cap, long long* lost)
{
    if (cap == 0) { ++*lost; return 1; }
    int went = 0;
    while (v->size() >= cap) { v->erase(v->begin()); ++*lost; went = 1; }
    v->push_back(x);
    return went;
}
/* An END's unrun list: seq goes in while it holds fewer than kTalkMaxUnrun; one more is counted in *overflow (that line is not
   refunded - never silently). */
inline void TalkUnrunAdd(std::vector<unsigned int>* out, unsigned int seq, long long* overflow)
{
    if (out->size() < kTalkMaxUnrun) out->push_back(seq); else ++*overflow;
}

/* The amount B may give back for one ACT it applied: 0 unless the ACT was APPLIED, both purse reads are known, the purse dropped and
   the line carries TAKE_MONEY; then the drop, capped at the line's TAKE_MONEY total (a GIVE_MONEY in the same line nets out). */
inline int TalkRefundAmount(int applied, int moneyBefore, int moneyAfter, const std::vector<int>& types, const std::vector<int>& values)
{
    if (!applied || moneyBefore < 0 || moneyAfter < 0 || moneyAfter >= moneyBefore) return 0;
    long long take = 0;
    for (size_t i = 0; i < types.size() && i < values.size(); ++i)
        if (types[i] == kTalkTakeMoneyType && values[i] > 0) take += values[i];
    if (take <= 0) return 0;
    const long long paid = (long long)moneyBefore - (long long)moneyAfter;
    const long long r = paid < take ? paid : take;
    return r > 2147483647LL ? 2147483647 : (int)r;
}

struct TalkPaid { unsigned int peer, convId, seq; int amount; long long base; };   /* base: the money ledger read it was taken from */

/* B's book of paid ACTs. Bounded (kMax rows; the oldest goes, counted in evicted). Peer identity is the caller's (Same(a, b): the
   same player). A row with the same numbers is overwritten (the newest payment wins; counted in replaced). */
struct TalkRefundBook
{
    static const size_t kMax = 64;
    std::vector<TalkPaid> rows;
    long long evicted, dropped, replaced;
    TalkRefundBook() : evicted(0), dropped(0), replaced(0) {}
    /* 1 = booked; 2 = booked OVER a row with the same numbers (an old launch's, or a repeated ACT's: still one row, the newest
       amount and ledger, moved to the newest place); 0 = nothing to book (amount <= 0) */
    template <class Same> int Note(unsigned int peer, unsigned int convId, unsigned int seq, int amount, long long base, Same same)
    {
        if (amount <= 0) return 0;
        int over = 0;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].convId == convId && rows[i].seq == seq && same(rows[i].peer, peer)) { rows.erase(rows.begin() + i); ++replaced; over = 1; break; }
        if (rows.size() >= kMax) { rows.erase(rows.begin()); ++evicted; }
        TalkPaid p; p.peer = peer; p.convId = convId; p.seq = seq; p.amount = amount; p.base = base;
        rows.push_back(p);
        return over ? 2 : 1;
    }
    /* An END for (peer, convId): *refund gets the booked rows the END lists as unrun; every row of that conversation is forgotten. */
    template <class Same> void OnEnd(unsigned int peer, unsigned int convId, const std::vector<unsigned int>& unrun,
                                     std::vector<TalkPaid>* refund, Same same)
    {
        for (size_t i = 0; i < rows.size();)
        {
            if (rows[i].convId != convId || !same(rows[i].peer, peer)) { ++i; continue; }
            for (size_t k = 0; k < unrun.size(); ++k)
                if (unrun[k] == rows[i].seq) { refund->push_back(rows[i]); break; }
            rows.erase(rows.begin() + i);
        }
    }
    /* The link to that player went (all: every link): its conversations can send no END any more - their rows go, nothing refunded. */
    template <class Same> void ForgetPeer(unsigned int peer, int all, Same same)
    {
        for (size_t i = 0; i < rows.size();)
            if (all || same(rows[i].peer, peer)) { rows.erase(rows.begin() + i); ++dropped; } else ++i;
    }
};
}
