/* src/common/dlgwinguard.h - THE DIALOGUE WINDOW NEVER CALLS THROUGH A REMOVED CHARACTER (T-831).

   WHAT IT IS FOR. The one dialogue window (DialogueWindow) keeps two pointers while it shows a conversation: the Dialogue
   (window +0xD0) and one character of it (window +0xF0, the conversation's speaker 1 that show fills; its portrait refresh can
   set it too). Its per-frame update reads +0xF0 every frame while +0xD0 is set and calls that character's virtual (the money
   label). Only the window's hide(dlg) clears them, and hide runs from the conversation's end. In single player the window
   pauses the game, so nothing is removed while it shows; in a session the window does not pause the game, and a character of
   the shown conversation can be removed (another player's character copy removed when they walk away, an NPC streamed out) -
   the next frame calls through freed memory and the game crashes.

   THE RULE. At the main thread's safe point (the AI worker paused), while the window shows a conversation: the conversation's
   owner (the Dialogue's speaking character), its target (the Dialogue's target hand, resolved through the engine's handle
   registry) and the window's stored character must all still exist; if the target or the stored character is gone, the
   conversation ends through the engine's own endDialogue, which hides the window and clears both pointers. When the owner
   itself is gone the Dialogue may have been freed with it, so nothing of it is called: the window closes through its own
   hide(dlg) (address row DialogueWindowHide), which only compares dlg with the window's +0xD0 and never reads it. Without
   that row nothing is called and the window's stored character is cleared when it is gone, so the update never calls
   through it. Before this mod removes a character itself, a conversation the window shows that names it ends first.

   The stored character is never trusted: it counts as here only when it is the same address as the owner or the target the
   handle resolves to now, or the engine's own character lists still hold that address (address compares, no read of it).

   Pure decisions; no engine types. */
#pragma once

namespace winguard {

/* One part's liveness: 1 here, 0 gone, -1 nothing to check (no target named, no character stored). */

/* What is done with the shown conversation: kKeep nothing; kEnd the engine's endDialogue; kHide the window's own hide(dlg)
   (the owner is gone - the Dialogue is not touched); kOwnerGone the owner is gone and there is no hide row - nothing called. */
enum { kKeep = 0, kEnd = 1, kOwnerGone = 2, kHide = 3 };

/* The gone parts, for the counters and the log. */
enum { kGoneOwner = 1, kGoneTarget = 2, kGoneShown = 4 };

/* ownerLive: 1 = the Dialogue's owner is a live character whose Dialogue is still this one, anything else gone.
   targetLive / shownLive: 1 here, 0 gone, -1 nothing to check. hideRow: 1 = the window's hide has an address row. */
inline int Verdict(int ownerLive, int targetLive, int shownLive, int hideRow)
{
    if (ownerLive != 1) return (hideRow == 1) ? kHide : kOwnerGone;
    if (targetLive == 0 || shownLive == 0) return kEnd;
    return kKeep;
}

inline unsigned int GoneMask(int ownerLive, int targetLive, int shownLive)
{
    unsigned int m = 0;
    if (ownerLive != 1) m |= kGoneOwner;
    if (targetLive == 0) m |= kGoneTarget;
    if (shownLive == 0) m |= kGoneShown;
    return m;
}

/* The window's stored character: -1 none stored; 1 when it is the live owner's or the resolved target's address (each 0 when
   not live / not resolved), or the engine's character lists hold it (inLists 1); else 0 - gone, never to be called. */
inline int ShownLive(const void* shown, const void* ownerNow, const void* targetNow, int inLists)
{
    if (shown == 0) return -1;
    if (ownerNow != 0 && shown == ownerNow) return 1;
    if (targetNow != 0 && shown == targetNow) return 1;
    return (inLists == 1) ? 1 : 0;
}

/* 1 = the window's stored character is cleared by this mod: the conversation is left as it is (verdict not kEnd, or the end
   did not close the window) and that character is gone. */
inline int ClearShown(int windowStillShows, int shownLive) { return (windowStillShows != 0 && shownLive == 0) ? 1 : 0; }

/* Before this mod removes a character the shown conversation names: ownerHere 1 = its owner still exists now (the doomed
   owner too - it is removed only after this) and the Dialogue is still the owner's, so the engine's endDialogue runs;
   otherwise the window's own hide, or nothing without its row. */
inline int RemoveVerdict(int ownerHere, int hideRow)
{
    if (ownerHere == 1) return kEnd;
    return (hideRow == 1) ? kHide : kOwnerGone;
}

/* Before this mod removes doomed: 1 = the shown conversation names it (its owner, its target or the window's stored
   character) and must end first. Address compares only. */
inline int EndBeforeRemove(const void* doomed, const void* owner, const void* target, const void* shown)
{
    if (doomed == 0) return 0;
    return (doomed == owner || doomed == target || doomed == shown) ? 1 : 0;
}

}   /* namespace winguard */
