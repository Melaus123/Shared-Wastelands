// money.cpp - see money.h. Every step waits for the one before it to be on disk (src/common/moneywire.h says the whole road):
//   SEND: the game's own take call (Ownerships::takeMoney, refused when short) -> the outbox row and the new balance in ONE pp.money
//   write -> MONEY_SEND once that write is on disk, and again on every new world-server link while the row is unacknowledged ->
//   SENT_OK drops the row (the next pp.money compare writes it); a final REFUSE gives the money back in one write with the row's
//   removal, a not-saved REFUSE keeps the row and sends it again after kRetryMs.
//   DELIVER: held in a table until StoreMoneyReady (no records restore waiting, the money record neither off nor held, the ledger
//   read) -> the money added and the applied number moved in ONE pp.money write -> MONEY_APPLIED once that write is on disk. A
//   delivery at or below the applied number is never added again; it sends APPLIED again (the world server still holds it).
// EDGES: what came on the world-server link (deliveries waiting, acknowledgements, final refusals) belongs to the WELCOME - the
//   profile - it came under: a new WELCOME, the link going down or a profile pick drops it (StoreMoneyEdge, moneyw::InboundSync);
//   the world server sends the inbox again at the next WELCOME and answers a resent transfer again. The ledger taken anew
//   (StoreMoneyBaseReads: another world load, store or profile) drops what this file kept about the last one's writes and sends.
// A WRITE THAT CANNOT BE MADE: the move is undone in the purse and the ledger together; when the purse cannot be undone, the
//   ledger keeps the move too and the write is owed (PayOwed, next tick) - money never leaves or enters the purse without its row.
// Logs: slots, transfer and inbox numbers and amounts only.
#include "money.h"
#include "store.h"
#include "chat.h"
#include "playerstab.h"
#include "coop_log.h"
#include "../common/moneywire.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ctime>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

const unsigned long kRetryMs = 30000;   /* a transfer the world server could not write is sent again this long after */

std::string N(long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
std::string U(unsigned long long v) { return moneyw::U64Text(v); }

moneyw::Inbound g_in;                                         /* what came on the link, under the edge it came under (EdgeSync) */
std::map<unsigned long long, moneyw::Deliver>& g_waiting = g_in.waiting;   /* deliveries not added yet, by inbox number */
std::set<unsigned long long>& g_acked = g_in.acked;                        /* SENT_OK transfers whose outbox rows are still to be dropped */
std::map<unsigned long long, unsigned>& g_refused = g_in.refused;          /* final REFUSE: transfer -> reason; its money is still to be given back */
long long g_baseSeen = -1;                                    /* the ledger read (StoreMoneyBaseReads) the tables below are about */
std::set<unsigned long long> g_owedOut;                       /* outbox rows whose pp.money write is owed (their money is already out of the purse) */
bool g_owedApplied = false;                                   /* the applied number moved with its write owed: MONEY_APPLIED waits for it */
bool g_owed = false;                                          /* a pp.money write is owed (PayOwed) */
std::map<unsigned long long, unsigned long long> g_outSeq;    /* an outbox row's transfer -> the pp.money write that holds it (none: on disk when read) */
std::map<unsigned long long, long> g_outSentOn;               /* an outbox row's transfer -> the link it was sent on */
std::map<unsigned long long, unsigned long> g_retryAt;        /* a not-saved transfer -> when it is sent again (GetTickCount) */
unsigned long long g_appliedSeq = 0;                          /* the pp.money write that holds the ledger's applied number (0: on disk when read) */
unsigned long long g_appliedSent = 0;                         /* the applied number last sent ... */
long g_appliedSentOn = -1;                                    /* ... and the link it went on (-1: send it again) */
std::string g_waitSaid;                                       /* the reason the last "wait" line gave */
bool g_roomSaid = false;                                      /* the "no room in the purse" line said for the waiting rows */
long long g_sent = 0, g_sendRefused = 0, g_sendsOut = 0, g_acks = 0, g_refunds = 0, g_delivers = 0, g_applied = 0, g_repeats = 0,
          g_appliedOut = 0, g_malformed = 0, g_notSaved = 0, g_writeFails = 0;

/* the player's name as the PLAYERS tab and the chat show it; a slot the world server could not name reads "A player" */
std::string NameOf(unsigned slot)
{
    std::string name, fac;
    if (slot != 0xFFFFFFFFu && coop::PlayersTabPlayerWords((int)slot, &name, &fac) && !name.empty()) return name;
    return "A player";
}
bool Work() { return !g_waiting.empty() || !g_acked.empty() || !g_refused.empty(); }

/* a new edge (a WELCOME, the link going down, a profile pick): what came before it is dropped - and with it the sends made before
   it, so every outbox row and the applied number go again after it */
void EdgeSync()
{
    const size_t w = g_waiting.size(), a = g_acked.size(), r = g_refused.size();
    if (!moneyw::InboundSync(&g_in, coop::StoreMoneyEdge())) return;
    g_outSentOn.clear(); g_retryAt.clear(); g_appliedSentOn = -1; g_roomSaid = false; g_waitSaid.clear();
    if (w + a + r > 0)
        DebugLog("[MONEY] a new world-server WELCOME, link or profile pick: " + N((long long)w) + " delivery(ies), " + N((long long)a) + " acknowledgement(s) and "
                 + N((long long)r) + " give-back(s) that came before it dropped - its WELCOME sends the inbox again and a resent transfer is answered again");
}

/* the ledger taken anew: the write numbers and sends kept for the last one's rows are dropped (a ledger taken from disk is on disk) */
void BaseSync()
{
    const long long r = coop::StoreMoneyBaseReads();
    if (r == g_baseSeen) return;
    g_baseSeen = r;
    g_outSeq.clear(); g_outSentOn.clear(); g_retryAt.clear(); g_owedOut.clear();
    g_appliedSeq = 0; g_appliedSent = 0; g_appliedSentOn = -1; g_owedApplied = false; g_owed = false;
}

/* the owed pp.money write (a move whose undo could not be made): made now; the rows and the applied number it holds wait for it */
void PayOwed()
{
    if (!g_owed) return;
    unsigned long long seq = 0;
    if (!coop::StoreMoneyWriteNow(&seq)) { if (++g_writeFails <= 5) DebugLog("[MONEY] the owed pp.money write could not be made - tried again next frame"); return; }
    for (std::set<unsigned long long>::const_iterator it = g_owedOut.begin(); it != g_owedOut.end(); ++it) g_outSeq[*it] = seq;
    if (g_owedApplied) g_appliedSeq = seq;
    DebugLog("[MONEY] the owed pp.money write made (seq " + U(seq) + ": " + N((long long)g_owedOut.size()) + " outbox row(s)"
             + std::string(g_owedApplied ? " and the applied number" : "") + ") - what it holds is sent once it is on disk");
    g_owedOut.clear(); g_owedApplied = false; g_owed = false;
}

void Acks(moneyw::Ledger* l)
{
    for (std::set<unsigned long long>::const_iterator it = g_acked.begin(); it != g_acked.end(); ++it)
    {
        moneyw::OutRow r;
        if (moneyw::LedgerDropOut(l, *it, &r))
        {
            ++g_acks;
            DebugLog("[MONEY] transfer " + U(*it) + " (" + N(r.amount) + " to slot " + N((long long)r.toSlot) + ") recorded by the world server - out of the outbox"
                     " (the next pp.money compare writes it; acks " + N(g_acks) + ")");
        }
        g_outSeq.erase(*it); g_outSentOn.erase(*it); g_retryAt.erase(*it); g_refused.erase(*it);
    }
    g_acked.clear();
}

void Refunds(moneyw::Ledger* l)
{
    while (!g_refused.empty())
    {
        const unsigned long long t = g_refused.begin()->first;
        const unsigned why = g_refused.begin()->second;
        const moneyw::Ledger before = *l;
        moneyw::OutRow r;
        if (!moneyw::LedgerDropOut(l, t, &r)) { g_refused.erase(g_refused.begin()); continue; }   /* not in the outbox: already given back or recorded */
        if (coop::StoreMoneyAdjust(r.amount) != 1) { *l = before; if (++g_writeFails <= 5) DebugLog("[MONEY] transfer " + U(t) + ": its money could not be put back in the purse yet - tried again next frame"); return; }
        unsigned long long seq = 0;
        const bool wrote = coop::StoreMoneyWriteNow(&seq);
        if (!wrote)
        {
            if (coop::StoreMoneyAdjust(-r.amount) == 1)
            {
                *l = before;
                if (++g_writeFails <= 5) DebugLog("[MONEY] transfer " + U(t) + ": the give-back could not be written to pp.money - nothing changed, tried again next frame");
                return;
            }
            g_owed = true;   /* the money could not be taken out again: the give-back stands with the row's removal, written by the next tick */
            if (++g_writeFails <= 5) DebugLog("[MONEY] transfer " + U(t) + ": the give-back could not be written to pp.money nor taken back out of the purse - it stands with the row's removal, the write owed");
        }
        g_refused.erase(g_refused.begin()); g_outSeq.erase(t); g_outSentOn.erase(t); g_retryAt.erase(t);
        ++g_refunds;
        const std::string name = NameOf(r.toSlot);
        coop::ChatMoneyLine((int)r.toSlot, name, moneyw::BackLine(name, r.amount));
        DebugLog("[MONEY] transfer " + U(t) + " REFUSED by the world server (" + moneyw::RefuseName(why) + "): " + N(r.amount) + " for slot "
                 + N((long long)r.toSlot) + " given back, " + (wrote ? "written with the row's removal (pp.money seq " + U(seq) + "; " : std::string("its write owed with the row's removal (")) + "given back " + N(g_refunds) + ")");
    }
}

void Apply(moneyw::Ledger* l)
{
    if (g_waiting.empty()) return;
    std::vector<moneyw::Deliver> w;
    for (std::map<unsigned long long, moneyw::Deliver>::const_iterator it = g_waiting.begin(); it != g_waiting.end(); ++it) w.push_back(it->second);
    int money = 0;
    if (coop::StoreMoneyRead(&money) != 1) return;
    long long sum = 0; unsigned long long upTo = 0; size_t repeats = 0;
    const size_t taken = moneyw::ApplyPlan(w, l->applied, money, &sum, &upTo, &repeats);
    if (repeats > 0)
    {
        g_repeats += (long long)repeats;
        for (size_t i = 0; i < w.size(); ++i) if (moneyw::InDecide(l->applied, w[i].inbox) == moneyw::kInRepeat) g_waiting.erase(w[i].inbox);
        g_appliedSentOn = -1;   /* the world server still holds rows this game has added: APPLIED goes again */
        DebugLog("[MONEY] " + N((long long)repeats) + " delivery(ies) at or below applied inbox " + U(l->applied) + " - already in the balance, not added again (APPLIED sent again)");
    }
    if (taken == 0)
    {
        if (!g_waiting.empty() && !g_roomSaid) { g_roomSaid = true; DebugLog("[MONEY] " + N((long long)g_waiting.size()) + " delivery(ies) wait: the purse (" + N(money) + ") has no room for the next one"); }
        return;
    }
    g_roomSaid = false;
    const moneyw::Ledger before = *l;
    if (coop::StoreMoneyAdjust((int)sum) != 1) return;
    l->applied = upTo;
    unsigned long long seq = 0;
    const bool wrote = coop::StoreMoneyWriteNow(&seq);
    if (!wrote)
    {
        if (coop::StoreMoneyAdjust(-(int)sum) == 1)
        {
            *l = before;
            if (++g_writeFails <= 5) DebugLog("[MONEY] " + N((long long)taken) + " delivery(ies) could not be written to pp.money - nothing added, tried again next frame");
            return;
        }
        g_owed = true; g_owedApplied = true;   /* the money could not be taken out again: it stands with the applied number, written by the next tick */
        if (++g_writeFails <= 5) DebugLog("[MONEY] " + N((long long)taken) + " delivery(ies) could not be written to pp.money nor taken back out of the purse - they stand with the applied number, the write owed");
    }
    else g_appliedSeq = seq;
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (w[i].inbox <= before.applied || w[i].inbox > upTo) continue;
        g_waiting.erase(w[i].inbox); ++g_applied;
        const std::string name = NameOf(w[i].fromSlot);
        coop::ChatMoneyLine(w[i].fromSlot == 0xFFFFFFFFu ? -1 : (int)w[i].fromSlot, name, moneyw::GotLine(name, w[i].amount));
        DebugLog("[MONEY] inbox " + U(w[i].inbox) + ": " + N(w[i].amount) + " from slot " + N((long long)w[i].fromSlot) + " added");
    }
    DebugLog("[MONEY] " + N((long long)taken) + " delivery(ies) added: " + N(money) + " -> " + N(money + sum) + ", applied inbox " + U(upTo)
             + (wrote ? " written with the balance (pp.money seq " + U(seq) + ")" : std::string(" moved with the balance, its write owed")) + "; MONEY_APPLIED waits for that write to be on disk (added " + N(g_applied) + ")");
}

void PumpOut(const moneyw::Ledger& l, long link)
{
    if (link < 0) return;
    const unsigned long now = ::GetTickCount();
    for (size_t i = 0; i < l.out.size(); ++i)
    {
        const moneyw::OutRow& r = l.out[i];
        if (g_refused.count(r.transfer) != 0) continue;
        if (g_owedOut.count(r.transfer) != 0) continue;   /* its pp.money write is owed */
        std::map<unsigned long long, long>::const_iterator s = g_outSentOn.find(r.transfer);
        const std::map<unsigned long long, unsigned long>::iterator again = g_retryAt.find(r.transfer);
        if (s != g_outSentOn.end() && s->second == link)
        {
            if (again == g_retryAt.end() || (long)(now - again->second) < 0) continue;   /* sent on this link, no retry due */
        }
        std::map<unsigned long long, unsigned long long>::const_iterator q = g_outSeq.find(r.transfer);
        if (!coop::StoreMoneyOnDisk(q == g_outSeq.end() ? 0 : q->second)) continue;
        moneyw::Send m; m.transfer = r.transfer; m.toSlot = r.toSlot; m.amount = r.amount;
        std::vector<char> b; moneyw::EncodeSend(&b, m);
        if (!coop::StoreSendMoney(moneyw::kMsgSend, b)) return;
        const bool before = s != g_outSentOn.end();
        g_outSentOn[r.transfer] = link;
        if (again != g_retryAt.end()) g_retryAt.erase(again);
        ++g_sendsOut;
        DebugLog("[MONEY] MONEY_SEND transfer " + U(r.transfer) + " (" + N(r.amount) + " to slot " + N((long long)r.toSlot) + ")"
                 + (before ? " again" : "") + " - its pp.money write is on disk (sends " + N(g_sendsOut) + ")");
    }
}

void PumpApplied(const moneyw::Ledger& l, long link)
{
    if (link < 0 || l.applied == 0 || g_owedApplied) return;   /* owed: the write holding the applied number is not made yet */
    if (g_appliedSentOn == link && g_appliedSent == l.applied) return;
    if (!coop::StoreMoneyOnDisk(g_appliedSeq)) return;
    std::vector<char> b; moneyw::EncodeU64(&b, l.applied);
    if (!coop::StoreSendMoney(moneyw::kMsgApplied, b)) return;
    g_appliedSent = l.applied; g_appliedSentOn = link; ++g_appliedOut;
    DebugLog("[MONEY] MONEY_APPLIED up to inbox " + U(l.applied) + " - written with the balance, on disk (sent " + N(g_appliedOut) + ")");
}

void TickBody()
{
    EdgeSync();
    std::string why;
    if (coop::StoreMoneyReady(&why) != 1)
    {
        if (Work() && why != g_waitSaid)
        {
            g_waitSaid = why;
            DebugLog("[MONEY] " + N((long long)g_waiting.size()) + " delivery(ies), " + N((long long)g_acked.size()) + " acknowledgement(s) and "
                     + N((long long)g_refused.size()) + " give-back(s) wait: " + why);
        }
        return;
    }
    g_waitSaid.clear();
    moneyw::Ledger* l = coop::StoreMoneyLedger();
    if (l == 0) return;
    BaseSync();
    PayOwed();
    const long link = coop::StoreMoneyLink();
    Acks(l);
    Refunds(l);
    Apply(l);
    PumpOut(*l, link);
    PumpApplied(*l, link);
}

}   /* namespace */

namespace coop {

void MoneyTick()
{
    try { TickBody(); } catch (...) { ErrorLog("[MONEY] an exception in the money tick - nothing more this frame"); }
}

void MoneyArrive(unsigned type, const std::vector<char>& payload)
{
    EdgeSync();   /* what arrives now is the current WELCOME's */
    const char* p = payload.empty() ? 0 : &payload[0];
    if (type == moneyw::kMsgSentOk)
    {
        unsigned long long t = 0;
        if (!moneyw::DecodeU64(p, payload.size(), &t)) { if (++g_malformed <= 5) DebugLog("[MONEY] a malformed MONEY_SENT_OK (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
        g_acked.insert(t);
        return;
    }
    if (type == moneyw::kMsgRefuse)
    {
        moneyw::RefuseMsg r;
        if (!moneyw::DecodeRefuse(p, payload.size(), &r)) { if (++g_malformed <= 5) DebugLog("[MONEY] a malformed MONEY_REFUSE (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
        if (!moneyw::RefuseFinal(r.why))
        {
            ++g_notSaved;
            g_retryAt[r.transfer] = (::GetTickCount() + kRetryMs) | 1u;
            DebugLog("[MONEY] transfer " + U(r.transfer) + " not recorded: the world server could not write it (not-saved) - kept, sent again in "
                     + N((long long)(kRetryMs / 1000)) + " s");
            return;
        }
        g_refused[r.transfer] = r.why;
        return;
    }
    if (type == moneyw::kMsgDeliver)
    {
        moneyw::Deliver d;
        if (!moneyw::DecodeDeliver(p, payload.size(), &d)) { if (++g_malformed <= 5) DebugLog("[MONEY] a malformed MONEY_DELIVER (" + N((long long)payload.size()) + " bytes) - ignored"); return; }
        ++g_delivers;
        if (g_waiting.count(d.inbox) != 0) return;
        g_waiting[d.inbox] = d;
        DebugLog("[MONEY] delivery inbox " + U(d.inbox) + ": " + N(d.amount) + " from slot " + N((long long)d.fromSlot) + " - added once this game's records allow ("
                 + N((long long)g_waiting.size()) + " waiting)");
    }
}

int MoneyHave(int* money) { return coop::StoreMoneyRead(money) == 1 ? 1 : 0; }

int MoneySend(int slot, const std::string& name, const std::string& typed, std::string* hint)
{
    EdgeSync();
    std::string why;
    int have = 0;
    const bool ready = slot >= 0 && coop::StoreMoneyReady(&why) == 1 && coop::StoreMoneyLink() >= 0 && coop::StoreMoneyRead(&have) == 1 && coop::StoreMoneyLedger() != 0;
    if (!ready)
    {
        ++g_sendRefused;
        *hint = moneyw::RefusedHint(1, moneyw::kAmtOk);
        DebugLog("[MONEY] SEND to slot " + N(slot) + " refused: " + (why.empty() ? std::string("the world server is not linked") : why) + " (refused " + N(g_sendRefused) + ")");
        return 0;
    }
    BaseSync();   /* the ledger StoreMoneyReady may just have taken anew: the write numbers below are about it */
    int amount = 0;
    const int a = moneyw::ParseAmount(typed, have, &amount);
    if (a != moneyw::kAmtOk)
    {
        ++g_sendRefused;
        *hint = moneyw::RefusedHint(0, a);
        DebugLog("[MONEY] SEND to slot " + N(slot) + " refused: the amount (" + N((long long)typed.size()) + " digit(s)) is not one this player can give - code " + N(a) + ", has " + N(have));
        return 0;
    }
    moneyw::Ledger* l = coop::StoreMoneyLedger();
    const moneyw::Ledger before = *l;
    moneyw::OutRow row;
    if (!moneyw::LedgerAddOut(l, (unsigned long long)::time(0), (unsigned)slot, amount, &row))
    {
        ++g_sendRefused;
        *hint = moneyw::RefusedHint(1, moneyw::kAmtOk);
        DebugLog("[MONEY] SEND to slot " + N(slot) + " refused: " + N((long long)l->out.size()) + " transfers already wait for the world server");
        return 0;
    }
    const int t = coop::StoreMoneyTake(amount);   /* the game's own take: refused when short, never a negative amount */
    if (t != 1)
    {
        *l = before; ++g_sendRefused;
        *hint = moneyw::RefusedHint(t == 0 ? 0 : 1, moneyw::kAmtTooMuch);
        DebugLog("[MONEY] SEND to slot " + N(slot) + " refused: the game's take of " + N(amount) + " " + (t == 0 ? "refused (short)" : "could not be made"));
        return 0;
    }
    int after = 0;
    coop::StoreMoneyRead(&after);
    unsigned long long seq = 0;
    const bool wrote = coop::StoreMoneyWriteNow(&seq);
    if (!wrote)
    {
        if (coop::StoreMoneyAdjust(amount) == 1)
        {
            *l = before; ++g_sendRefused;
            *hint = moneyw::RefusedHint(1, moneyw::kAmtOk);
            DebugLog("[MONEY] SEND to slot " + N(slot) + " refused: the balance and outbox row could not be written to pp.money - the " + N(amount) + " is back in the purse");
            return 0;
        }
        g_owedOut.insert(row.transfer); g_owed = true;   /* the money could not be put back: it left the purse, so its outbox row stays with it, written by the next tick */
    }
    else g_outSeq[row.transfer] = seq;
    ++g_sent;
    coop::ChatMoneyLine(slot, name, moneyw::SentLine(name, amount));
    DebugLog("[MONEY] transfer " + U(row.transfer) + ": " + N(amount) + " to slot " + N(slot) + " taken (" + N(have) + " -> " + N(after) + ")"
             + (after == have - amount ? std::string() : " - NOT the expected " + N(have - amount))
             + (wrote ? ", written with its outbox row (pp.money seq " + U(seq) + ")" : std::string(", its outbox row kept with it - the write could not be made nor the money put back, so the write is owed"))
             + "; MONEY_SEND waits for that write to be on disk (sent " + N(g_sent) + ")");
    return 1;
}

}   /* namespace coop */
