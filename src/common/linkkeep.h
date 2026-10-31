/* src/common/linkkeep.h - THE WORLD-SERVER LINK KEPT MOVING WHILE THE GAME SAVES.
 *
 * A game services its link to the world server only from its frame (store.cpp PumpLink). While the engine saves - the whole
 * game save (saveGame) or one area's write (saveLevelData) - the frame stands still, and an area file handed to ENet inside
 * that call sits in ENet: ENet keeps at most one window of guaranteed data in flight and sends more only when the world
 * server's acknowledgements are read, which needs a service call. So for the length of the save the link moves nothing.
 *
 * While such a call runs on the main thread, a helper thread owned by the link's ENet transport services ENet alone
 * (enet_host_service with no event: it sends, receives and acknowledges; what it receives stays inside ENet for the next
 * Poll) under the lock every other ENet call on that link takes. No message is handled and no game state is touched.
 *
 * This header is the pure part: the save calls nest (saveGame calls saveLevelData), so a WINDOW opens at the outermost
 * entry and closes at its matching exit; what each window was (kept / held back / no keeper / helper not started) and the
 * totals for the REPORT.
 * The plugin (store.cpp LinkKeepEnter / LinkKeepExit) and the offline suite compile this same code. No Windows, no ENet,
 * no threads. No C++11. */
#ifndef COOP_COMMON_LINKKEEP_H
#define COOP_COMMON_LINKKEEP_H

namespace linkkeep {

/* What one window was.
   kOff      - no world-server link was up: nothing to keep, not counted.
   kKept     - the helper serviced the link for the whole window.
   kHeld     - the link was up but PumpLink's back-pressure held it (the arrival queue near full while engine writes are
               blocked): PumpLink does not poll it then, so neither does the helper - acknowledging more would let the world
               server send more into a queue that is already full.
   kNoKeeper - the link was up but its backend has no helper (Steam: Valve's library sends and acknowledges on its own
               thread).
   kNoHelper - the link was up and its backend has a helper (ENet), but the helper could not be started (its event or
               thread could not be made). */
enum { kOff = 0, kKept = 1, kHeld = 2, kNoKeeper = 3, kNoHelper = 4 };

/* What the helper did in one window (filled by the transport, under its lock). */
struct Pass
{
    long long services;       /* enet_host_service calls */
    long long sentBytes;      /* bytes ENet put on the wire during them */
    long long ackedPackets;   /* packets ENet let go during them: guaranteed ones acknowledged, unguaranteed ones sent */
    long long lockWaitMaxUs;  /* the longest the main thread waited for the link's lock (the helper's waits are not timed) */
    Pass() : services(0), sentBytes(0), ackedPackets(0), lockWaitMaxUs(0) {}
};

/* The open window: how deep the save calls are nested and what the outermost entry decided. */
struct Window
{
    int depth;
    int kind;
    Window() : depth(0), kind(kOff) {}
};

/* An entry into a save call. true = the outermost one: the caller decides the window's kind and sets it (SetKind). */
inline bool Enter(Window* w)
{
    if (w->depth++ > 0) return false;
    w->kind = kOff;
    return true;
}
inline void SetKind(Window* w, int kind) { w->kind = kind; }

/* An exit from a save call. Returns the window's kind when this exit closes the outermost call, -1 for an inner exit
   or an exit with no open window. */
inline int Exit(Window* w)
{
    if (w->depth <= 0) { w->depth = 0; return -1; }
    if (--w->depth > 0) return -1;
    const int k = w->kind;
    w->kind = kOff;
    return k;
}

/* The kind the outermost entry gives a window, before asking the transport: linkUp = the world-server link's transport
   is up; held = PumpLink's own hold condition is true right now. A kKept answer becomes kNoKeeper when the transport
   says it cannot keep itself serviced. */
inline int Decide(int linkUp, int held)
{
    if (!linkUp) return kOff;
    return held ? kHeld : kKept;
}

/* The kind a kKept window takes when the transport refused to keep itself serviced: hasHelper = its backend has a helper
   (then it could not be started: kNoHelper), else it has none (kNoKeeper). */
inline int Refused(int hasHelper) { return hasHelper ? kNoHelper : kNoKeeper; }

/* The REPORT's linkKeep[...] numbers, since the plugin loaded. */
struct Totals
{
    long long stalls;         /* windows with the link up (kept + held + noKeeper + noHelper) */
    long long kept, held, noKeeper, noHelper;
    long long services, maxStallMs, sentBytes, ackedPackets, lockWaitMaxUs;
    Totals() : stalls(0), kept(0), held(0), noKeeper(0), noHelper(0), services(0), maxStallMs(0), sentBytes(0), ackedPackets(0), lockWaitMaxUs(0) {}
};

/* One closed window into the totals. false = a kOff (or unknown) window: nothing counted. */
inline bool Add(Totals* t, int kind, long long stallMs, const Pass& p)
{
    if (kind != kKept && kind != kHeld && kind != kNoKeeper && kind != kNoHelper) return false;
    ++t->stalls;
    if (stallMs > t->maxStallMs) t->maxStallMs = stallMs;
    if (kind == kHeld) { ++t->held; return true; }
    if (kind == kNoKeeper) { ++t->noKeeper; return true; }
    if (kind == kNoHelper) { ++t->noHelper; return true; }
    ++t->kept;
    t->services += p.services;
    t->sentBytes += p.sentBytes;
    t->ackedPackets += p.ackedPackets;
    if (p.lockWaitMaxUs > t->lockWaitMaxUs) t->lockWaitMaxUs = p.lockWaitMaxUs;
    return true;
}

/* The window's log line is written for the first 5 windows and every 100th (n = Totals::stalls after Add). */
inline bool LogThis(long long n) { return n >= 1 && (n <= 5 || (n % 100) == 0); }

}   /* namespace linkkeep */

#endif
