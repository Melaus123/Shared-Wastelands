/* src/common/talkrefund.h - contributor task 5 (0.4.0 brief): A PAID CONVERSATION NEVER TAKES THE MONEY AND GIVES NOTHING.
 *
 * In a forwarded conversation the NPC's game (A) runs the NPC's side of a line and the talker's game (B) the talker's side - the
 * TAKE_MONEY of a bribe or a paid service (ACT -> ACT_RESULT, speech.cpp). A holds the line's NPC-side part until B's ACT_RESULT.
 * If that answer is lost or late, A ends the conversation with nothing of the line run, while B has already taken the money.
 *
 * The fix, decided here and pure so the suite tests it:
 *   - B books every ACT it applied that took money: (peer, conversation, ACT seq) -> the amount taken (TalkRefundAmount: the purse's
 *     drop B measured itself, never more than the line's TAKE_MONEY total).
 *   - Every END A sends lists the ACT seqs whose NPC-side part that end leaves unrun (TalkMsg::unrun, game protocol 188).
 *   - B, on an END: refunds the listed seqs it booked, ONCE, and forgets every row of that conversation (an unlisted seq's NPC part
 *     ran). A repeated END, a repeated ACT_RESULT or a reconnect finds nothing left to refund.
 *   - A late ACT_RESULT runs nothing on A (the conversation is gone): the money comes back by the END's list, never both, never neither.
 * Money only: items handed to an NPC are not booked (see the PR's design note). C++03 (VS2010). */
#pragma once
#include <vector>

namespace cooptalk
{
const unsigned int kTalkMaxUnrun = 16;    /* END: unrun seqs carried (A holds at most 16 lines: speech.cpp kTkActHoldCap) */
const int kTalkTakeMoneyType = 8;         /* DialogActionEnum TAKE_MONEY (talkwire.h's per-action table) */

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

struct TalkPaid { unsigned int peer, convId, seq; int amount; unsigned long tick; };

/* B's book of paid ACTs. Bounded (kMax rows; the oldest goes, counted); a row older than kMaxAgeMs goes too (no END ever came: a lost
   link - nothing is refunded for it, the PR's design note). Peer identity is the caller's (Same(a, b): the same player). */
struct TalkRefundBook
{
    static const size_t kMax = 64;
    static const unsigned long kMaxAgeMs = 600000;   /* 10 minutes */
    std::vector<TalkPaid> rows;
    long long evicted, expired;
    TalkRefundBook() : evicted(0), expired(0) {}
    /* 1 = booked; 0 = nothing to book (amount <= 0) or that ACT is booked already (a repeated ACT never books twice) */
    template <class Same> int Note(unsigned int peer, unsigned int convId, unsigned int seq, int amount, unsigned long tick, Same same)
    {
        if (amount <= 0) return 0;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].convId == convId && rows[i].seq == seq && same(rows[i].peer, peer)) return 0;
        if (rows.size() >= kMax) { rows.erase(rows.begin()); ++evicted; }
        TalkPaid p; p.peer = peer; p.convId = convId; p.seq = seq; p.amount = amount; p.tick = tick;
        rows.push_back(p);
        return 1;
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
    void Expire(unsigned long now)
    {
        for (size_t i = 0; i < rows.size();)
            if ((unsigned long)(now - rows[i].tick) >= kMaxAgeMs) { rows.erase(rows.begin() + i); ++expired; } else ++i;
    }
};
}
