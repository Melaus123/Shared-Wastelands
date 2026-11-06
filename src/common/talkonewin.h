/* src/common/talkonewin.h - ONE CONVERSATION IN THE WINDOW AT A TIME (T-978, owner decision 1445 a: B2 and B1; D and A at the end).

   WHAT IT IS FOR. This game has one conversation window. In single player it pauses the world, so while it shows a
   conversation no other NPC can start one with the player. In a session the world keeps running (T-288), and run T1930
   (Confirmed, evidence/T1930/log-A.txt:6521-6525) showed a second NPC's talk aimed at the player writing into the open
   window - its text, then clearing the reply buttons, then its own buttons - and taking the window over: the player saw
   the conversation answer by itself or close at once.

   B2 - THE SECOND START IS REFUSED. While the window shows a live conversation (or the mirrored one), a start of another
   conversation toward this game's own player character - an NPC's talk, or the player's own Talk order to a second NPC -
   is refused with the engine's own 'not started' before anything is written. A start toward anyone else (the other
   player's character - marked, it never opens this window - or an NPC) is not this rule's business. The refused talk is
   QUEUED, not lost: the newest refused (NPC, player character) pair is kept with both replicated ids, whether this game
   ran the NPC, and the walk-over's parked line (below); once the window is empty it is sent again through the engine's
   own talk event (event 1), if both characters are still here (re-found by id), the NPC is still run by the same game,
   it is not in a conversation, and the window emptied less than 30 s ago (review 2026-11-03, manager for the owner: the
   refused talk happens after your conversation, as in solo; the 30 s count from the window closing, not from the
   refusal). The engine's own checks (distance, a line to say) still apply. In single player a guard who wanted to talk
   does so after the conversation, since the world was paused - this is the nearest thing in a running world.
   THE PARKED LINE. A walk-over's talk starts from a line parked on the NPC's Dialogue (+0xC8, written by the line's
   actions); sendEvent's event 1 starts with it and wipes it after the start, refused or not (0x684221-0x684238, Read). The
   line is kept with the queued talk and put back on the Dialogue before the re-send, so the same line (a guard's bag check)
   is said; a line the engine chose fresh is chosen fresh again.
   TWO STARTS IN ONE FRAME. Two starts on the AI thread can both pass B2 (the window opens only when the engine replays
   setInDialog on the main thread). The later one's window open is refused there (RefuseOpen), it ends at the next safe point
   the engine's own way, and its talk is queued like a refused start.

   B1 - THE WINDOW IGNORES OTHER CONVERSATIONS' WRITES. A conversation that was already running before the window opened
   is not caught by B2. Its three writers into the window (its text, its reply buttons, the reply-list clear) are skipped
   on the main thread while the window shows another conversation. A write while the window shows nothing passes (that
   is how a window opens). Off the main thread the engine's writers queue themselves and come back through the same
   entry on the main thread, so the rule is applied there.

   Pure decisions; no engine types. */
#pragma once

namespace talkonewin {

/* TkWindowDlg's answer: the Dialogue the window shows; 0 none; (void*)1 the window's address is not in the table. */
inline int ShownReal(const void* shown) { return (shown != 0 && shown != (const void*)1) ? 1 : 0; }

/* B2: 1 = refuse this start of dlg's conversation. shownBusy = the shown conversation is live (TkDlgBusy); shownMirrored =
   the window shows the mirrored conversation (the other game runs it, so its own Dialogue here need not look busy);
   towardMine = the start is toward this game's own player character (TkOrderIsMine) - only such a start opens this window. */
inline int RefuseStart(const void* dlg, const void* shown, int shownBusy, int shownMirrored, int towardMine)
{
    if (dlg == 0 || !towardMine) return 0;
    if (!ShownReal(shown) || shown == dlg) return 0;
    return (shownBusy || shownMirrored) ? 1 : 0;
}

/* Two starts in one frame: 1 = refuse setInDialog(dlg, on) - B2's rule, on the main thread (where the engine replays the
   window's open), never for the mirrored conversation's own Dialogue (B's road answers BUSY before it starts). */
inline int RefuseOpen(const void* dlg, const void* shown, int shownBusy, int shownMirrored, int towardMine, int onMain, int dlgMirrored)
{
    if (!onMain || dlgMirrored) return 0;
    return RefuseStart(dlg, shown, shownBusy, shownMirrored, towardMine);
}

/* B1: 1 = skip this write into the window by dlg's conversation. onMain = called on the main thread (off it the engine
   queues the write and replays it through the same entry on the main thread, where this rule runs). */
inline int SkipWrite(const void* dlg, const void* shown, int onMain)
{
    if (!onMain) return 0;
    return (ShownReal(shown) && dlg != shown) ? 1 : 0;
}

/* B2's queue: one slot, the newest refused talk wins. npc / who = the addresses at the refusal; npcUid / whoUid = their
   replicated ids (0 = none: the address is all there is); npcMine = this game ran the NPC then; line = the parked line
   to put back (0 = none: the engine chose its line fresh); ms = when it was refused (GetTickCount; never 0 - stored 1 for a
   tick of 0; it names the entry); emptyMs = when the window was first seen empty since (0 = not yet; EmptySince). */
struct Queued { const void* npc; const void* who; const void* line; unsigned int npcUid, whoUid; int npcMine; unsigned int ms, emptyMs; int set; };
const unsigned int kQueueMaxAgeMs = 30000;
/* 1 = this refusal is queued: the start had a line to say (without one the engine would not have started it) and the
   talker is this game's own player character. */
inline int QueueWorthy(int hadLine, int talkerMine) { return (hadLine && talkerMine) ? 1 : 0; }
/* keeps the newest refused pair; 1 = it replaced an older one */
inline int QueueKeep(Queued* q, const void* npc, const void* who, const void* line, unsigned int npcUid, unsigned int whoUid, int npcMine,
                     unsigned int ms)
{
    if (q == 0) return 0;
    const int replaced = q->set ? 1 : 0;
    q->npc = npc; q->who = who; q->line = line; q->npcUid = npcUid; q->whoUid = whoUid; q->npcMine = npcMine ? 1 : 0;
    q->ms = (ms == 0) ? 1u : ms; q->emptyMs = 0; q->set = 1;
    return replaced;
}
/* The parked line kept with a refused start: `line` only when it IS the Dialogue's parked line (+0xC8) as the start runs -
   the walk-over road; a line the engine chose fresh (parked 0, or another line) is not kept. */
inline const void* KeptLine(const void* line, const void* parked) { return (line != 0 && line == parked) ? line : 0; }
/* At the re-send: what to do with the kept line, given the Dialogue's parked line now. kLinePutBack = write it back (nothing
   parked); kLineNewer = the engine parked another line since - it is left (the engine's newer choice wins); kLineThere = it is
   still parked; kLineNone = nothing was kept. */
const int kLineNone = 0, kLinePutBack = 1, kLineNewer = 2, kLineThere = 3;
inline int ParkedLineStep(const void* kept, const void* parkedNow)
{
    if (kept == 0) return kLineNone;
    if (parkedNow == 0) return kLinePutBack;
    return (parkedNow == kept) ? kLineThere : kLineNewer;
}
/* The window-empty clock: when the window was first seen empty since the refusal, or since it last showed a conversation
   (the 30 s start again each time it empties). 0 = not seen empty. */
inline unsigned int EmptySince(unsigned int emptyMs, int windowEmpty, unsigned int nowMs)
{
    if (!windowEmpty) return 0;
    if (emptyMs != 0) return emptyMs;
    return (nowMs == 0) ? 1u : nowMs;
}
/* 1 = the NPC is run by the same game as at the refusal (uid 0: no replicated id - nothing can have changed hands). */
inline int OwnerSame(unsigned int npcUid, int mineThen, int mineNow) { return (npcUid == 0 || (mineThen ? 1 : 0) == (mineNow ? 1 : 0)) ? 1 : 0; }

/* What the main thread does with the queued talk at a safe point. emptyMs = the slot's clock after EmptySince; windowEmpty =
   the window shows nothing (TkWindowDlg 0); npcHere / talkerHere = the character, re-found by its id, still exists, the NPC
   alive and conscious; npcBusy = the NPC is in a conversation (its own, or one this game marked); ownerSame = OwnerSame.
   The 30 s count only while the window is empty: a conversation of any length waits. Wrap-safe. */
const int kResendWait = 0, kResendNow = 1, kResendDropOld = 2, kResendDropGone = 3, kResendDropOwner = 4;
inline int ResendStep(int queued, unsigned int nowMs, unsigned int emptyMs, int windowEmpty, int npcHere, int talkerHere, int npcBusy,
                      int ownerSame)
{
    if (!queued) return kResendWait;
    if (!npcHere || !talkerHere) return kResendDropGone;
    if (!ownerSame) return kResendDropOwner;
    if (!windowEmpty || emptyMs == 0) return kResendWait;
    if ((unsigned int)(nowMs - emptyMs) >= kQueueMaxAgeMs) return kResendDropOld;
    if (npcBusy) return kResendWait;
    return kResendNow;
}

/* B1's portrait refresh (review 2026-11-03): 1 = skip a refresh of the window's portrait - the window shows a conversation and
   the refreshed character is neither its NPC, nor its target, nor one of its speakers (another conversation's refresh). who 0
   (no character) passes: it names no conversation. Decided on the main thread, as B1. Re-check 2026-11-03: a third character
   speaking a line in the SHOWN conversation (an interjector - a squad-mate or bystander) passes: the line's speaker setter
   0x6709A0 writes it into the shown Dialogue's speaker hand (+0x1C8 / +0x1E8 / +0x208 for speaker types 3 / 4 / 5) before it
   refreshes slot 0, and the target setter 0x670EA0 writes the target hand (+0x158) before it refreshes slot 1 (Read,
   disassembly of both). */
inline int SkipPortrait(const void* shown, const void* who, int whoIsNpc, int whoIsTarget, int whoSpeaks)
{
    if (!ShownReal(shown) || who == 0) return 0;
    return (whoIsNpc || whoIsTarget || whoSpeaks) ? 0 : 1;
}

/* D - A CLICK THAT BEGAN BEFORE THE WINDOW OPENED IS IGNORED (owner 1445 a). The window's per-frame step (0x726FF0 / 1.0.68
   0x727B20) picks the hovered reply on the frame the game's 'left button released this frame' byte is set (0x2132419 / 1.0.68
   0x2133469; its only writer 0x82AAF0 sets it when the left button is up now and was down the frame before - Read), and the engine
   never asks when that press began. On the first step that shows a NEW conversation (a takeover too) the rule arms when the left
   button reads down (the game's own left-down byte 0x2132413 / 1.0.68 0x2133463, which 0x82AAF0 reads to make the released
   byte - Read) - or its release lands on that very step (the press began before it). While armed, a reply click made BY THE
   STEP is dropped; the first release after arming disarms it (the window stays open: the player clicks again). A release the game
   never saw (the button let go while the game had no input) cannot keep it armed: kHeldUpSteps steps with the button up and no
   release disarm it. Solo too (one road whatever the player count). */
struct HeldClick { const void* last; int armed; int upSteps; };
const int kHeldUpSteps = 3;
/* before the step. shown = the window's conversation (0 none); forceNew = the window log opened a new record since (the same
   Dialogue shown again is a new conversation); leftDown = the button reads down now; releasedNow = the game's released byte.
   1 = armed by this step. */
inline int HeldBegin(HeldClick* h, const void* shown, int forceNew, int leftDown, int releasedNow)
{
    if (h == 0) return 0;
    if (shown == 0) { h->last = 0; h->armed = 0; h->upSteps = 0; return 0; }
    if (shown == h->last && !forceNew) return 0;
    h->last = shown; h->upSteps = 0;
    h->armed = (leftDown || releasedNow) ? 1 : 0;
    return h->armed;
}
/* replyClicked: 1 = drop this click (armed, and the window step made it) */
inline int HeldDrop(const HeldClick* h, int fromStep) { return (h != 0 && h->armed && fromStep) ? 1 : 0; }
/* after the step: 1 = disarmed now (the release was seen, or kHeldUpSteps steps up with no release); 2 = disarmed by the up steps */
inline int HeldEnd(HeldClick* h, int releasedNow, int leftDown)
{
    if (h == 0 || !h->armed) return 0;
    if (releasedNow) { h->armed = 0; h->upSteps = 0; return 1; }
    if (leftDown) { h->upSteps = 0; return 0; }
    if (++h->upSteps >= kHeldUpSteps) { h->armed = 0; h->upSteps = 0; return 2; }
    return 0;
}

/* A - THE NPC BEING TALKED TO STANDS STILL IN ITS DECISIONS (owner 1445 a). In single player the world pauses while the window
   is open; in a session it runs, so the NPC's AI kept deciding (walking off, starting jobs) mid-conversation. While the window
   shows a conversation of this game's own NPC, the mod's own 'decisions off' switch (coop::SuppressCharacter, the copy gate) holds
   it; its tasks and orders are not touched. One slot. Released on every road the conversation ends by, never for a character
   that was already held when the window opened (that hold belongs to someone else).
   FORWARDED (review 2026-11-03): this game's own NPC talking to the OTHER player's character never shows this window (its
   conversation is forwarded and shown on the other game), so the game that runs the NPC holds it the same way while the
   forwarded conversation is open (FreezeWhy with windowShowsDlg = 'forwarded and open'); a few slots, one per conversation.
   Released when the conversation leaves the forwarded table (its END, the other game's CLOSED, the mark's release, a restart)
   and on the end roads that apply to it (FwdRoad); a back-stop at the safe point releases a slot whose conversation is gone. */
const int kFrzFreeze = 0, kFrzNotLinked = 1, kFrzOffMain = 2, kFrzNotShown = 3, kFrzNoNpc = 4, kFrzNotMine = 5, kFrzPlayer = 6,
          kFrzAlreadyHeld = 7, kFrzAlreadyFrozen = 8, kFrzWhys = 9;
inline int FreezeWhy(int linked, int onMain, int windowShowsDlg, int npcPresent, int npcMine, int npcPlayerChar, int npcAlreadyHeld,
                     int slotHoldsDlg)
{
    if (!linked) return kFrzNotLinked;
    if (!onMain) return kFrzOffMain;
    if (!windowShowsDlg) return kFrzNotShown;
    if (slotHoldsDlg) return kFrzAlreadyFrozen;
    if (!npcPresent) return kFrzNoNpc;
    if (!npcMine) return kFrzNotMine;
    if (npcPlayerChar) return kFrzPlayer;
    if (npcAlreadyHeld) return kFrzAlreadyHeld;
    return kFrzFreeze;
}

struct Frozen { const void* dlg; const void* npc; unsigned int uid; int set; };
/* the roads a frozen conversation ends by */
const int kRelSetInDialogOff = 0, kRelEndDialogue = 1, kRelReplaced = 2, kRelDestroyed = 3, kRelLinkDropped = 4, kRelTeardown = 5,
          kRelWindowMoved = 6, kRelFwdEnded = 7, kRelRoads = 8;
/* 1 = this road ends the frozen slot's conversation. dlg = the Dialogue the road names (setInDialog(false), endDialogue, the
   replaced window record); obj = the destroyed object. */
inline int ReleaseMatches(const Frozen* f, int road, const void* dlg, const void* obj)
{
    if (f == 0 || !f->set) return 0;
    if (road == kRelSetInDialogOff || road == kRelEndDialogue || road == kRelReplaced) return (dlg != 0 && dlg == f->dlg) ? 1 : 0;
    if (road == kRelDestroyed) return (obj != 0 && obj == f->npc) ? 1 : 0;
    if (road == kRelFwdEnded) return (dlg != 0 && dlg == f->dlg) ? 1 : 0;
    return (road == kRelLinkDropped || road == kRelTeardown || road == kRelWindowMoved) ? 1 : 0;
}
/* 1 = this road can end a FORWARDED conversation's hold: never the window's own roads (setInDialog(false) of a marked Dialogue is
   swallowed; the window replaced or moved on is another conversation's business). */
inline int FwdRoad(int road)
{
    return (road == kRelEndDialogue || road == kRelDestroyed || road == kRelLinkDropped || road == kRelTeardown || road == kRelFwdEnded) ? 1 : 0;
}
/* what a matching road does: kActRelease = the hold goes (by the character's address; the object is not touched); kActForget =
   only the slot is emptied: a world teardown has already freed every hold and an address may be reused by the next world; a
   character no longer this game's own (handed over: it is now a copy, and its hold is the copy gate's) keeps it - releasing it
   would make a free-running copy (F152). */
const int kActNone = 0, kActRelease = 1, kActForget = 2;
inline int ReleaseAction(int road, int stillMine)
{
    if (road == kRelTeardown) return kActForget;
    if (road == kRelDestroyed) return kActRelease;
    return stillMine ? kActRelease : kActForget;
}

/* BUBBLE - A SPEECH BUBBLE SAID BEFORE THE WINDOW OPENED DOES NOT END THE CONVERSATION (T-978 attempt 2, run T1949). The engine's
   per-frame Dialogue step counts two bubble timers down while the Dialogue has a speech bubble and the game is not paused; when both
   run out it ends the Dialogue's conversation (or advances its line). A player conversation's lines go to the window, never to a
   bubble, and its start does not remove a bubble already there: an NPC that said a line to someone else just before keeps that
   bubble, and its expiry ended the player's window a few seconds later. In single player the window's own pause froze the timer;
   in a session the pause is skipped (T-288), so the mod holds the first timer instead, while this game's window shows a live
   conversation that has a bubble (any conversation event: the single-player pause froze an NPC-started window's timer too), or, on
   the NPC's game, a forwarded PLAYER conversation (event 1) is open with one: at the value it had when the hold began, or
   kBubbleHoldFloor, whichever is larger. The floor is the engine's own shortest bubble (say: 2.0 + 0.12 x the text's length), far
   above one frame's step. The hold stops the moment the conversation is no longer shown, ended or bubble-less; the end deletes the
   bubble, so nothing is put back. */
const float kBubbleHoldFloor = 2.0f;
struct BubbleHold { const void* dlg; float hold; int set; int wrote; };
/* the value held: the timer as the hold began, at least the floor (an unreadable / not-a-number timer holds the floor) */
inline float BubbleHoldValue(float atStart) { return (atStart > kBubbleHoldFloor) ? atStart : kBubbleHoldFloor; }
/* 1 = the hold applies to this Dialogue now: in a session (for the window: linked, or this window's opening pause was skipped -
   a link dropped mid-window keeps it), on the main thread, shown (the window's Dialogue, or an open forwarded conversation), a
   player conversation (event 1) only when needEvent1 (the forwarded ones; the window's own hold takes any event), not ended (the
   ended byte reads 0; -1 = unreadable) and a bubble present */
inline int BubbleHoldOn(int inSession, int onMain, int shown, int needEvent1, int convEvent, int endedByte, int hasBubble)
{
    return (inSession && onMain && shown && (!needEvent1 || convEvent == 1) && endedByte == 0 && hasBubble) ? 1 : 0;
}
/* one safe point for one slot: on = BubbleHoldOn for dlg; timerNow = the first timer read now. Returns 1 = write *out into the
   timer (it is below the held value); a new Dialogue in the slot starts a new hold; off empties the slot (the hold stops). */
inline int BubbleHoldStep(BubbleHold* h, const void* dlg, int on, float timerNow, float* out)
{
    if (h == 0 || out == 0) return 0;
    if (!on || dlg == 0) { h->dlg = 0; h->hold = 0.0f; h->set = 0; h->wrote = 0; return 0; }
    if (!h->set || h->dlg != dlg) { h->dlg = dlg; h->hold = BubbleHoldValue(timerNow); h->set = 1; h->wrote = 0; }
    if (timerNow >= h->hold) return 0;   /* also false for a not-a-number timer: it is written */
    *out = h->hold;
    return 1;
}

}   /* namespace talkonewin */
