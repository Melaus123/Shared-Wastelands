/* src/common/portraitdefer.h - THE DIALOGUE WINDOW'S PORTRAIT REFRESH, KEPT ON THE MAIN THREAD (T-804).

   WHAT IT IS FOR. The engine's dialogue code (Dialogue::sendEvent -> _chooseDialog and its condition checks) refreshes a
   portrait in the one dialogue window (DialogueWindow, args: the window, the character, a slot byte; slot 0 and slot 1 are
   the two portraits). That refresh renders the portrait with Ogre at once. When the conversation window does not pause the
   game, the AI worker thread reaches this code, and its render runs beside the main thread's frame render - Direct3D 11
   is not made for that and the game crashes. The engine's other window updates from this road test whether the AI worker
   is running and queue their work; the portrait refresh does not.

   THE RULE. The engine's refresh runs only on the main thread while the AI worker is not running; any other call is held
   here, one entry per slot (a newer request for a slot replaces the older one - only the latest portrait matters), and
   the main thread replays it at its safe point (the AI worker paused) if the window still shows the same conversation, is
   visible (the engine's callers refresh only a visible window) and the character still exists. A refresh run at once
   empties its slot's entry: the older held request must not paint over the newer portrait at the safe point (the engine
   refreshes slot 1 only when the conversation's target changes, so a wrong portrait would stay).

   Pure decisions and a plain table; no engine types, no allocation (the hold runs on the AI thread). */
#pragma once

namespace cooppd {

const int kSlots = 2;

/* The table entry a slot byte goes to: 0 for slot 0, 1 for any other (the engine's refresh tests the byte as zero or not). */
inline int SlotIndex(int slot) { return (slot != 0) ? 1 : 0; }

/* 1 = call the engine's refresh now; 0 = hold it. onMain: this is the main thread. workerBusy: the AI worker's own busy
   test, 1 running, 0 idle, -1 could not be read (no address row, no world) - then the main thread runs it as before. */
inline int CallNow(int onMain, int workerBusy) { return (onMain != 0 && workerBusy != 1) ? 1 : 0; }

/* Why a refresh was held (the counters and the capped log line name it). */
enum { kWhyOffThread = 1, kWhyWorkerBusy = 2 };

/* One held refresh. hand[5] = the character's own handle words (type, container, containerStamp, index, serial), read
   when it was held, so the replay finds the character again only if it still exists; hadWho = 0 when the engine passed
   no character (the replay drops it: the engine's refresh reads the character's name without testing it for none).
   dlgAtHold = the Dialogue the window showed when it was held (0 none). */
struct Held
{
    int used;
    int slot;
    int why;
    int hadWho;
    unsigned int hand[5];
    const void* dlgAtHold;
};

struct HeldTable { Held e[kSlots]; };

inline void Clear(HeldTable* t) { for (int i = 0; i < kSlots; ++i) { t->e[i].used = 0; } }

/* Stores h in its slot's entry. Returns 1 when it replaced a held refresh that was not yet replayed. */
inline int Put(HeldTable* t, const Held& h)
{
    Held& d = t->e[SlotIndex(h.slot)];
    const int replaced = d.used;
    d = h;
    d.used = 1;
    return replaced;
}

/* Empties the entry a slot byte goes to (a refresh for that slot ran at once). Returns 1 when it held a refresh. */
inline int Drop(HeldTable* t, int slot)
{
    Held& d = t->e[SlotIndex(slot)];
    const int had = d.used;
    d.used = 0;
    return had;
}

/* Takes entry i out of the table: 1 and *out filled when it held a refresh, 0 when it was empty. */
inline int Take(HeldTable* t, int i, Held* out)
{
    if (i < 0 || i >= kSlots || t->e[i].used == 0) return 0;
    *out = t->e[i];
    t->e[i].used = 0;
    return 1;
}

/* What the safe point does with a held refresh. */
enum { kApply = 0, kDropWindow = 1, kDropChar = 2, kDropHidden = 3 };

/* winNow: the dialogue window object now (0 none); winShown: the window's own visible test now (1 visible, 0 hidden, -1
   unreadable - anything but 1 drops, as the engine's callers refresh only a visible window); dlgNow: the Dialogue it shows
   now (0 none); dlgAtHold: the Dialogue it showed when the refresh was held (0 none - then any shown conversation takes it);
   hadWho / whoNow: the refresh named a character, and what its handle resolves to now (0 gone). The window is tested first:
   a refresh for a closed, hidden or other conversation's window is dropped whatever became of the character. A refresh
   with no character is dropped as kDropChar: the engine's refresh cannot run without one. */
inline int ReplayVerdict(const void* winNow, int winShown, const void* dlgNow, const void* dlgAtHold, int hadWho, const void* whoNow)
{
    if (winNow == 0 || dlgNow == 0) return kDropWindow;
    if (dlgAtHold != 0 && dlgAtHold != dlgNow) return kDropWindow;
    if (winShown != 1) return kDropHidden;
    if (hadWho == 0 || whoNow == 0) return kDropChar;
    return kApply;
}

}   /* namespace cooppd */
