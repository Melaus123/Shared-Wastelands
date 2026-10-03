#pragma once
/* P105g (row P105, owner 344 a, approved S2-65, 2026-10-01): A GROUND PICKUP BY A GAME THAT DOES NOT HOLD THE AREA SHOWS AT ONCE.
   The industry-standard optimistic action (the T-164 non-shop road's take rule, showmove.h, applied to the ground):
     PICKUP   the engine's own pickup runs at once on the picker's game - the item is in the inventory and gone from that game's
              ground - and the TAKE goes to the holder afterwards (the wire is unchanged: the holder's answer is the same; "already
              shown" is a mark on the requester's own row only).
     YES      PLACED ok=1 and nothing is built (it is already there). A partial pickup (the inventory was full): the holder can only
              grant the whole stack (GroundServeRequest), so the TAKE names the stack as it lay and the units still on this game's
              ground become this game's own drop on the yes (announced, or PUT and protected) - the GRANTED-IN-PART road.
     GONE     (reason 2: another game took it first) - the units that went in are taken back out of the picker: the pickup never
              happened. Units used or eaten before the answer are counted as a shortfall.
     NOT HOLDER / BUSY / no answer / the link down - asked again (the SAME id; the holder answers a repeat of a TAKE it already
              granted from the item it keeps), never removed silently. Other refusals (unreadable) are asked again up to kReaskMax
              times, then the item stays with the picker, counted.
   Header-only, no engine types; shared by items.cpp and the offline suite (coop-test). */

#include "groundkey.h"

namespace coopgshow {

/* The units that went into the picker. og: after the engine's body, 1 the item is still on the ground, 0 it is not, < 0 unreadable
   (the object left the ground or was merged into a stack and destroyed) - the whole stack. q0 the stack as it lay, q1 after. */
inline int PickTook(int og, int q0, int q1)
{
    if (q0 < 1) q0 = 1;
    if (og != 1) return q0;
    if (q1 >= 0 && q1 < q0) return q0 - q1;
    return 0;   /* nothing went in: nothing to ask */
}

/* The main thread, right after the pickup. mineNow: this game holds the area after all (the snapshot did not know the sector);
   unconfHere / unconfWaiting: the item is this game's own drop the holder never confirmed (a PUT for it is in flight).
   p105g-f1-2: putEverSent - a PUT for that drop ever went out (GrUnconf.serial != 0, not localOnly). Only a drop no PUT ever left
   for is surely the only copy (the pickup stands, nothing asked); a PUT whose answer never came may have been applied by the
   holder, so the TAKE is asked then (the row keeps that it was our own drop: a "no such item" then still stands). */
const int kAkSend = 0, kAkHeldHere = 1, kAkOwnDrop = 2;
inline int AskAction(int mineNow, int unconfHere, int unconfWaiting, int putEverSent)
{
    if (mineNow != 0) return kAkHeldHere;
    if (unconfHere != 0 && unconfWaiting == 0 && putEverSent == 0) return kAkOwnDrop;   /* the only copy is ours: nothing to ask */
    return kAkSend;
}

/* The holder's answer to a shown TAKE. */
const int kSgGranted = 1, kSgRemove = 2, kSgOwnDrop = 3, kSgReask = 4, kSgHeldHere = 5, kSgKeepStop = 6;
const int kReaskMax = 12;   /* an unreadable / other refusal: asked this many times, then the item stays with the picker (counted) */
inline int SettleAction(int ok, int reason, int unconfHere, int unconfWaiting, int mineNow, int tries)
{
    if (ok != 0) return kSgGranted;
    if (reason == coopground::kGroundReasonNotFound)
    {
        if (unconfHere != 0) return (unconfWaiting != 0) ? kSgReask : kSgOwnDrop;   /* our own drop: its PUT may still land */
        return kSgRemove;
    }
    if (reason == coopground::kGroundReasonNotHolder) return (mineNow != 0) ? kSgHeldHere : kSgReask;
    if (reason == coopground::kGroundReasonBusy) return kSgReask;   /* the holder's own pickup of it is running: gone or yes next time */
    return (tries >= kReaskMax) ? kSgKeepStop : kSgReask;
}

/* The re-ask delay after `tries` failed asks: 1 s doubling to 30 s. */
inline unsigned int ReaskDelayMs(int tries)
{
    unsigned int d = 1000u;
    for (int i = 1; i < tries && d < 30000u; ++i) d *= 2u;
    return (d < 30000u) ? d : 30000u;
}

/* A shown row at a tick. inFlight: a TAKE is out (re-sent with the same id after timeoutMs without an answer); otherwise it waits for
   its re-ask time (beforeReaskAt 1 = not yet). */
const int kRdWait = 0, kRdSend = 1;
inline int RowDue(int inFlight, unsigned int sinceSentMs, unsigned int timeoutMs, int beforeReaskAt)
{
    if (inFlight != 0) return (sinceSentMs > timeoutMs) ? kRdSend : kRdWait;
    return (beforeReaskAt != 0) ? kRdWait : kRdSend;
}

/* On a yes: the holder's stack less what went in here is what still lies on this game's ground. */
inline int GrantRest(int holderQty, int took)
{
    const int r = holderQty - took;
    return (r > 0) ? r : 0;
}

/* On a GONE: the units that could not be taken back (used, eaten, or moved away before the answer). */
inline int Shortfall(int want, int removed)
{
    return (removed >= want) ? 0 : want - removed;
}

/* p105g-f1-5c: which refusals count toward kReaskMax. "Not holder", "busy" and our own drop's "no such item" while its PUT may still
   land are asked again without a cap; a failed send (the link) is no refusal at all (GrRow.tries only paces the re-asks). */
inline int CapCounts(int reason)
{
    return (reason == coopground::kGroundReasonNotFound || reason == coopground::kGroundReasonNotHolder
            || reason == coopground::kGroundReasonBusy) ? 0 : 1;
}

/* p105g-f1-3: the pickup detour reserves the ASK's slot in the main thread's queue BEFORE the engine's pickup runs (queued +
   reserved < cap); no room = the pickup is not run (the task ends unpicked, counted ringFull). The pickup never runs without its ASK. */
inline int RingRoom(int queued, int reserved, int cap)
{
    return (queued >= 0 && reserved >= 0 && queued + reserved < cap) ? 1 : 0;
}

/* p105g-f1-1: the holder, a repeat of a TAKE it granted, answered again from the kept row: the row's PLACED wait (10 s,
   coopesc::ExpireAction) starts again at that answer - the requester's PLACED answers THIS confirm. Not answered: unchanged. */
inline unsigned int RepeatSentAt(unsigned int sentAt, unsigned int now, int answered)
{
    return (answered != 0) ? now : sentAt;
}
/* p105g-f1-5a: does a kept ground row answer a repeat of its id? Asked by the peer it was granted to: yes (that pass runs first,
   over every row). By another peer: only when the request names the row's key AND a link edge happened since the grant (relinked:
   the host numbers a reconnecting requester afresh). */
inline int RepeatMatch(int samePeer, int keySame, int relinked)
{
    if (samePeer != 0) return 1;
    return (keySame != 0 && relinked != 0) ? 1 : 0;
}

/* p105g-f1-4: which stack a GONE takes back out first (lower first; -1 never). An equipped slot is never touched; a stack of the
   row's quality before one that is not; the character's own sections before a worn pack's contents. */
inline int TakeBackRank(int equipped, int inPack, int qMatch)
{
    if (equipped != 0) return -1;
    return ((qMatch != 0) ? 0 : 2) + ((inPack != 0) ? 1 : 0);
}

/* p105g-f1-5b: a copy still lying here under the row's key is removed only when the detour could not read the item after the
   pickup (afterUnread: it may still lie here although it went in), or on a GONE of a partial pickup (the rest is stale here too:
   the holder has none). Otherwise an item at that key is another item and stays. */
inline int LeftoverRemove(int granted, int afterUnread, int partial)
{
    if (afterUnread != 0) return 1;
    return (granted == 0 && partial != 0) ? 1 : 0;
}

/* p105g-f2-1 (row T-435): the holder marks a kept ground row escrow-eligible (ItOwnerPend.cfSent) when an ok CONFIRM for it went
   out - the grant, or an answered repeat (p105g-f1-1). From then 10 s without a PLACED holds it in escrow (coopesc::ExpireAction),
   never back on the ground: the picker already shows the item. A send that failed leaves the mark as it was (an earlier ok CONFIRM
   may have arrived); a row never marked still goes back on the ground at 10 s - the picker never heard yes and asks again. */
inline int KeptCfSent(int prev, int ok, int sent)
{
    return (ok != 0 && sent != 0) ? 1 : prev;
}

}   /* namespace coopgshow */
