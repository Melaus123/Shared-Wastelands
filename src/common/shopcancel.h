/* shopcancel.h - the holder's steps on a cancel and on a settle-only request of a home shop's trade, over a copy of the shop record:
   coopshop's decisions (shopwire.h) run on shoprec's entries (shoprec.h). Shared by the plugin and the offline suite; the world server
   does not include it. */
#pragma once
#include <string>
#include <vector>
#include "shoprec.h"
#include "shopwire.h"

namespace shoprec {
/* THE REQUESTS THIS GAME RAN AN UNDO FOR IN THIS WORLD ({asker, epoch, id}): a repeat after a lost mark marks again and never undoes
   twice. Across a save and a load the goods' own records say it (ShopUndoGoods). */
struct ShopReqKey { unsigned int asker, epoch, id; };
inline int ShopKeyHas(const std::vector<ShopReqKey>& v, unsigned int asker, unsigned int epoch, unsigned int id)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].asker == asker && v[i].epoch == epoch && v[i].id == id) return 1;
    return 0;
}
inline void ShopKeyAdd(std::vector<ShopReqKey>* v, unsigned int asker, unsigned int epoch, unsigned int id, size_t cap)
{
    if (ShopKeyHas(*v, asker, epoch, id) != 0) return;
    ShopReqKey k; k.asker = asker; k.epoch = epoch; k.id = id;
    v->push_back(k);
    if (v->size() > cap) v->erase(v->begin());
}
/* THE HOLDER'S STEP ON A CANCEL over its copy of the shop record. work / haveWork: this game's working copy (the step marks it when it
   marks); srv / haveSrv: the world server's last record; squad: the keeper squad's name; stockOk / exact / stale / known / appliedHere:
   as coopshop::CancelHolderDecide; undoneBefore: this game ran this request's undo already; keyOk: the cancel's home key names this
   record; askedN: the units the cancel names. An entry the stored record has further on (owed or undone) than the copy is taken from
   it first. step / answer: the decision; undo: run the goods and money undo now (the applied row kept here with no record - an owed
   entry's undo is the owed sweep's, ShopOwedDue); mark: the copy took a mark now (owed, undone or a marker); n / move: the entry's units and
   money. */
struct ShopCancelOut { int step, answer, undo, mark, n, move; ShopCancelOut() : step(0), answer(0), undo(0), mark(0), n(0), move(0) {} };
inline ShopCancelOut ShopCancelStep(ShopRec* work, int haveWork, int haveSrv, const ShopRec& srv, const std::string& squad, unsigned int asker,
                                    unsigned int epoch, unsigned int id, int npcBuy, int caravan, int stockOk, int exact, int stale, int known,
                                    int appliedHere, int undoneBefore, int keyOk, int askedN)
{
    ShopCancelOut o;
    const int at = (haveWork != 0) ? ShopSquadAt(*work, squad) : -1;
    const int srank = (haveSrv != 0) ? ShopRecRank(srv, squad, asker, epoch, id) : -1;
    int marker = 0, entry = kRsNone;
    if (at >= 0)
    {
        ShopReqDone* d = ShopReqFindMut(&work->squads[(size_t)at], asker, epoch, id);
        const ShopReqDone* sd = (srank >= 0) ? ShopReqFind(srv.squads[(size_t)ShopSquadAt(srv, squad)], asker, epoch, id) : 0;
        if (d != 0 && sd != 0 && srank > ShopRevRank(d->revoked) && (sd->n != 0 || sd->move != 0))
            { d->revoked = sd->revoked; d->undo = sd->undo; d->claimer = sd->claimer; }   /* the stored record has it further on: the copy takes that state (a stored marker never cancels an applied entry) */
        entry = ShopReqState(work->squads[(size_t)at], asker, epoch, id, &marker);
        if (d != 0) { o.n = d->n; o.move = d->move; }
    }
    const int stored = (srank >= 3) ? 2 : ((srank >= 1) ? 1 : 0);
    const int descOk = (appliedHere != 0 || (keyOk != 0 && o.n <= askedN)) ? 1 : 0;
    o.step = coopshop::CancelHolderDecide(npcBuy, caravan, stockOk, exact, at >= 0 ? 1 : 0, known, stale, entry, marker, stored, appliedHere, descOk, &o.answer);
    if (o.step == coopshop::kChUndoLocal)
    {
        if (undoneBefore != 0) { o.step = coopshop::kChAnswerNow; o.answer = coopshop::kCaUndone; }
        else { o.undo = 1; o.answer = coopshop::kCaUndone; }
        return o;
    }
    if (o.step == coopshop::kChMarkOwed) o.mark = ShopReqOwe(&work->squads[(size_t)at], asker, epoch, id);
    else if (o.step == coopshop::kChMarker)
    {
        o.mark = ShopReqMarker(&work->squads[(size_t)at], asker, epoch, id);
        o.answer = coopshop::kCaNotApplied;
    }
    return o;
}
/* THE HOLDER'S STEP ON A SETTLE-ONLY REQUEST over its copy (coopshop::SettleOnlyDecide): v the verdict; mark: the copy took a marker
   now (kSoMarker); n / move: the entry's units and money (kSoFromRecord answers with them). An entry cancelled with its undo owed reads
   as cancelled: refused once the owed mark is stored. */
struct ShopSettleOut { int v, mark, n, move; ShopSettleOut() : v(0), mark(0), n(0), move(0) {} };
inline ShopSettleOut ShopSettleOnlyStep(ShopRec* work, int haveWork, int haveSrv, const ShopRec& srv, const std::string& squad, unsigned int asker,
                                        unsigned int epoch, unsigned int id, int keeperFound, int exact, int stale, int known)
{
    ShopSettleOut o;
    const int at = (haveWork != 0) ? ShopSquadAt(*work, squad) : -1;
    const int srank = (haveSrv != 0) ? ShopRecRank(srv, squad, asker, epoch, id) : -1;
    int marker = 0, entry = kRsNone;
    if (at >= 0)
    {
        entry = ShopReqState(work->squads[(size_t)at], asker, epoch, id, &marker);
        const ShopReqDone* d = ShopReqFind(work->squads[(size_t)at], asker, epoch, id);
        if (d != 0) { o.n = d->n; o.move = d->move; }
    }
    int stored = 0;
    if (entry == kRsCancelRefused) entry = kRsApplied;   /* its cancel refused: the trade stands - answered from the entry */
    if (entry == kRsOwed || entry == kRsClaimed) { entry = kRsRevoked; stored = (srank >= 1) ? 1 : 0; }
    else if (entry == kRsRevoked) stored = (srank >= 3) ? 1 : 0;
    else if (entry == kRsApplied && srank >= 1)
    {   /* the stored record has it cancelled (a stored marker never cancels an applied entry; a refused cancel leaves it standing) */
        const ShopReqDone* sd = ShopReqFind(srv.squads[(size_t)ShopSquadAt(srv, squad)], asker, epoch, id);
        if ((sd->n != 0 || sd->move != 0) && sd->revoked != kRevRefused) { entry = kRsRevoked; stored = 1; }
    }
    o.v = coopshop::SettleOnlyDecide(keeperFound, exact, at >= 0 ? 1 : 0, known, stale, entry, stored);
    if (o.v == coopshop::kSoMarker) o.mark = ShopReqMarker(&work->squads[(size_t)at], asker, epoch, id);
    return o;
}
/* WHERE AN UNDO'S GOODS LIVE: each counter (box) of the shop. localHas: this game's own change to the counter carries the key and its
   write is not stored yet; haveRecord: the world server's record of the counter is kept here; recordHas: the record the world server
   STORED for it (an arrival, or this game's own write once its echo came) carries the key; applyDone: that record is put into the
   counter at this load (a counter whose record failed to go in is not decided); ownSeed: the record is only this game's own first
   write, sent less than kOwnSeedSettleMs ago (a refusal may still take it back); never: the counter cannot hold the undo in a record
   - it has no record id, or has stayed without a record or with its record refused at this load for kCounterGiveUpMs. */
struct ShopUndoPiece { int localHas, haveRecord, recordHas, applyDone, ownSeed, never; };
const unsigned int kOwnSeedSettleMs = 5000u;
const unsigned int kCounterGiveUpMs = 60000u;   /* six asks for a first write (one each 10 s); a write that can be made lands in seconds */
/* kUgIn: a stored record of a counter holds the undo - it is never applied again; kUgOut: every counter has a stored record put in at
   this load and none holds it - the goods lack it; kUgWait: this game's own change carrying it not stored yet, a counter with no
   record yet (its first write is asked), a record not put in yet, or only this game's own unsettled first write - not decided;
   kUgNever: a counter can never hold the undo in a record - the cancel is refused, the trade stands. */
const int kUgOut = 0, kUgIn = 1, kUgWait = 2, kUgNever = 3;
inline int ShopUndoGoods(const std::vector<ShopUndoPiece>& p)
{
    for (size_t i = 0; i < p.size(); ++i) if (p[i].haveRecord != 0 && p[i].recordHas != 0) return kUgIn;
    for (size_t i = 0; i < p.size(); ++i) if (p[i].localHas != 0) return kUgWait;
    for (size_t i = 0; i < p.size(); ++i) if (p[i].never != 0) return kUgNever;
    if (p.empty()) return kUgNever;
    for (size_t i = 0; i < p.size(); ++i) if (p[i].haveRecord == 0 || p[i].applyDone == 0 || p[i].ownSeed != 0) return kUgWait;
    return kUgOut;
}
/* AN UNDO THIS GAME APPLIED, UNTIL IT IS DURABLE: each counter the undo changed - stored: the world server stored its record carrying
   the key (the echo of this game's write, or an arrival); inFlight: not stored, and this game's change carrying the key is still in
   the counter (its write pending or in flight). keptOpen: the undo's units wait, kept, to be filed. kUgIn: every counter stored (none
   changed: nothing to wait for) - the undone mark and its money may go; kUgWait: a write still pending, or kept units; kUgOut: every
   change was lost before any record stored it (the counter's area left, the game quit, the write was dropped) - the undo is applied
   again, once, into goods that lack it; some stored and the rest lost: kUgIn, *partial = 1 (the lost part is counted, never applied
   twice into the stored part). */
struct ShopUndoTouch { int stored, inFlight; };
inline int ShopUndoDurable(const std::vector<ShopUndoTouch>& t, int keptOpen, int* partial)
{
    *partial = 0;
    if (keptOpen != 0) return kUgWait;
    int stored = 0, flight = 0;
    for (size_t i = 0; i < t.size(); ++i) { if (t[i].stored != 0) ++stored; else if (t[i].inFlight != 0) ++flight; }
    if (flight != 0) return kUgWait;
    if (stored == (int)t.size()) return kUgIn;
    if (stored == 0) return kUgOut;
    *partial = 1;
    return kUgIn;
}
/* THIS GAME'S NOTE THAT ITS CHANGE TO A COUNTER CARRIES AN UNDO (ShopUndoPiece::localHas, ShopUndoTouch::inFlight; items.cpp
   g_boxUndoLocal), after an event on that counter (had: the note before it). The note says the counter holds the undo through this
   game's own change not stored yet, so it lives exactly as long as that change is in the counter: kUnFiled - the undo's goods filed in
   the counter: noted; kUnSet - the counter set from a stored record (laid in, or found already that record): its contents are that
   record's now and only that record's undo list says what it holds - the note ends (kept, a newer record that wiped the goods would
   still read as holding the undo and the money would move with no goods back); kUnAreaLeft - the counter's area left this game: the
   counter keeps its goods while away and is set from a record again only if a newer one is stored, so the note stands (dropped, the
   goods still in the counter would read as lost and the undo be applied a second time); kUnWorldGone - the world torn down: the note
   ends with it. 1 = the note stands */
const int kUnFiled = 0, kUnSet = 1, kUnAreaLeft = 2, kUnWorldGone = 3;
inline int ShopUndoNoteStands(int had, int ev)
{
    if (ev == kUnFiled) return 1;
    if (ev == kUnSet || ev == kUnWorldGone) return 0;
    return (had != 0) ? 1 : 0;
}
/* HOW LONG ANOTHER GAME'S STORED CLAIM HAS STOOD UNFINISHED while this game is the exact holder (so that claimer is not: it cannot
   write the record, or is not in the world). otherClaim 0 (not the exact holder, or no other game's claim) ends the watch; a new
   claimer starts it again. The milliseconds watched. */
struct ShopClaimWatch { int have; unsigned int claimer, since; ShopClaimWatch() : have(0), claimer(0), since(0) {} };
inline unsigned int ShopClaimGoneMs(ShopClaimWatch* w, int otherClaim, unsigned int claimer, unsigned int now)
{
    if (otherClaim == 0) { w->have = 0; return 0; }
    if (w->have == 0 || w->claimer != claimer) { w->have = 1; w->claimer = claimer; w->since = now; return 0; }
    return now - w->since;
}
/* A CLAIM IS RE-CLAIMED once its claimer has been away this long: past the refusal hold (coopshop kRefusedHoldMs, 10 s) and the
   writer hand-over wait (coopbrec kBlvAfterRequestMs, 3 s), so every write the old claimer sent while it was the holder - its box
   records carrying the undo, its undone mark - has landed or been refused and reached this game; far inside a cancel's life (its
   asker resends each second and counts one older than 10 minutes). */
const unsigned int kReclaimMs = 30000u;
/* THE OWED SWEEP'S DECISION FOR ONE ENTRY. workRev / workClaimer: this game's copy; srvRev / srvClaimer: the world server's last record
   (-1 no entry); mySlot: this game's slot (-1 none); exact: this game holds the shop's area and the stock resolves here; canUndo: the
   undo's description is in the entry or the applied row is kept here; ranHere: the goods hold the undo durably (as goods kUgIn);
   goods: ShopUndoGoods / ShopUndoDurable (the shop's counters' stored records hold the undo, lack it, are not decided, or can never
   hold it); goneMs: ShopClaimGoneMs for another game's claim. kOwClaim: stored owed - write this game's claim (nothing applied);
   kOwApply: this game's claim is stored and the goods lack the undo - apply it; its undone mark waits until every counter it changed
   has its record stored (ShopUndoDurable); kOwMarkUndone: this game's claim is stored and the goods hold the undo durably - write
   undone with its money; kOwReclaim: another game's claim has stood unfinished kReclaimMs while this game is the exact holder - write
   this game's claim over it (applied only once stored, and only if the goods lack it); kOwRefuse: this game's claim is stored and a
   counter can never hold the undo in a record - write the cancel refused (the trade stands); kOwWait: a claim, a re-claim, an undone
   or refused mark in flight, another game's claim not yet away long, the goods not decided, or not the exact holder; kOwNone: nothing
   owed. Exactly one game applies an undo: the one whose claim is stored, and only into goods without it; the money moves only once
   the goods are stored undone. */
const int kOwNone = 0, kOwClaim = 1, kOwApply = 2, kOwMarkUndone = 3, kOwWait = 4, kOwReclaim = 5, kOwRefuse = 6;
inline int ShopOwedDue(int workRev, unsigned int workClaimer, int srvRev, unsigned int srvClaimer, int mySlot, int exact, int canUndo, int ranHere,
                       int goods = kUgOut, unsigned int goneMs = 0)
{
    if (srvRev == (int)kRevOwed)
    {
        if (exact == 0 || mySlot < 0 || canUndo == 0) return kOwWait;
        if (workRev == (int)kRevClaimed || workRev == (int)kRevUndone || workRev == (int)kRevRefused) return kOwWait;   /* a claim in flight, or another's claim rebased in */
        return kOwClaim;
    }
    if (srvRev != (int)kRevClaimed) return kOwNone;
    if (mySlot < 0) return kOwWait;
    if (srvClaimer != (unsigned int)mySlot)
    {
        if (exact == 0 || goneMs < kReclaimMs) return kOwWait;
        if (workRev != (int)kRevClaimed || workClaimer != srvClaimer) return kOwWait;   /* this game's re-claim (or another state) in flight */
        return (ranHere != 0 || goods == kUgIn || canUndo != 0) ? kOwReclaim : kOwWait;
    }
    if ((workRev == (int)kRevUndone || workRev == (int)kRevRefused) && workClaimer == (unsigned int)mySlot) return kOwWait;   /* the final mark in flight */
    if (exact == 0) return kOwWait;
    if (ranHere != 0 || goods == kUgIn) return kOwMarkUndone;
    if (goods == kUgNever) return kOwRefuse;
    if (goods != kUgOut) return kOwWait;
    return (canUndo != 0) ? kOwApply : kOwWait;
}
}   /* namespace shoprec */
