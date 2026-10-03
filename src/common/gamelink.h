/* gamelink.h - link1 (T416, 2026-09-26): the GAME link's ENet timeouts and a joiner's in-world re-dial of it, as pure
   decisions the offline suite hits. Header-only; no Windows, no ENet.

   T416: both games' world loads left ~5.35 s with no ENet service on either side. ENet drops a peer when
     elapsed since the oldest unacknowledged reliable command >= timeoutMaximum, OR
     (1 << (sendAttempts - 1)) >= timeoutLimit AND that elapsed >= timeoutMinimum
   (third_party/enet/protocol.c:1374-1379; defaults 32 / 5000 / 30000, enet.h). Over loopback the retransmit backoff
   reaches the limit in well under a second, so the 5 s minimum was the binding term and B's link died on B's first
   world frame. The minimum is raised to 30 s (over five times the longest overlap measured) and the maximum to 60 s;
   the limit stays ENet's 32. THE COST: a peer that vanishes WITHOUT a BYE (a crash, a pulled cable) is noticed after
   30-60 s instead of 5-30 s. A graceful leave sends BYE and an ENet disconnect and is seen at once.
   These apply to the SESSION (game) link only, set per connected peer on both host and joiner. M11a S3 (manager decision 6(a),
   2026-09-30): the WORLD-SERVER link takes the same three terms on both ends - the game's store link (store.cpp, SetPeerTimeouts
   before its Join, applied at its CONNECT) and coop-store.exe at each CONNECT (store_main.cpp). A dial that has not
   connected yet keeps ENet's defaults, so a dial at a host that is not listening still fails in ~5 s. */
#ifndef COOP_COMMON_GAMELINK_H
#define COOP_COMMON_GAMELINK_H

namespace coopgl {

const unsigned int kGameLinkTimeoutLimit     = 32;      /* ENet's own ENET_PEER_TIMEOUT_LIMIT */
const unsigned int kGameLinkTimeoutMinimumMs = 30000;
const unsigned int kGameLinkTimeoutMaximumMs = 60000;

/* THE RE-DIAL. A joiner whose game link is down while its world runs dials the host it joined again: the first
   attempt 2 s after the drop was seen, then 4, 8, 16, and every 30 s after that, each interval counted from the
   moment the previous dial stopped CONNECTING. It never dials at the title screen (config.cpp's E38 retry owns that)
   or during a load, never on the host, and never once the session is gone (`leave`, a protocol refusal, quit): all of
   those leave no transport behind. */
const unsigned int kGameRedialFirstMs = 2000;
const unsigned int kGameRedialCapMs   = 30000;

/* The wait before attempt (attemptsSoFar + 1) of one series: 2000, 4000, 8000, 16000, then 30000 for every later one. */
inline unsigned int GameRedialBackoffMs(unsigned int attemptsSoFar)
{
    unsigned int ms = kGameRedialFirstMs;
    for (unsigned int i = 0; i < attemptsSoFar && ms < kGameRedialCapMs; ++i) ms *= 2;
    return ms > kGameRedialCapMs ? kGameRedialCapMs : ms;
}

/* the game link's socket as the re-dial reads it */
const int kGlSockDown = 0, kGlSockConnecting = 1, kGlSockUp = 2;

enum GameRedialAction
{
    kGameRedialNotJoiner      = 0,   /* no session (left / refused / never joined), the host, or no host address: never dial */
    kGameRedialIdle           = 1,   /* the link is up - nothing to do */
    kGameRedialWaitConnecting = 2,   /* a dial is in flight - leave it alone; the next interval runs from its end */
    kGameRedialSkipNoWorld    = 3,   /* down, but no running world (title screen or a load) - never dial here */
    kGameRedialWait           = 4,   /* down in a running world, this attempt's interval still running */
    kGameRedialNow            = 5    /* down in a running world and the interval has elapsed: dial */
};

inline int GameRedialDecide(int haveSession, int isHost, int haveTarget, int sock, int worldRunning,
                            unsigned int sinceMs, unsigned int attemptsSoFar)
{
    if (!haveSession || isHost || !haveTarget) return kGameRedialNotJoiner;
    if (sock == kGlSockUp) return kGameRedialIdle;
    if (sock == kGlSockConnecting) return kGameRedialWaitConnecting;
    if (!worldRunning) return kGameRedialSkipNoWorld;
    return (sinceMs >= GameRedialBackoffMs(attemptsSoFar)) ? kGameRedialNow : kGameRedialWait;
}

/* mmo5 fold (review-mmo5 item 1): a joiner's latch for the host's SESSION_CLOSING. Raised when it arrives; cleared on a link
   UP (a new or re-made link carries a host that has announced nothing), when the session is left, and when it arrives where it
   cannot be acted on (no running world, or the host not seen in this world) - so it never fires later. */
const int kHcReceived = 1, kHcLinkUp = 2, kHcSessionLeft = 3, kHcNotActionable = 4;
inline int HostClosingFlagStep(int flag, int ev)
{
    if (ev == kHcReceived) return 1;
    if (ev == kHcLinkUp || ev == kHcSessionLeft || ev == kHcNotActionable) return 0;
    return flag;
}

/* M5b fold 1 (review 2026-09-30 item 1): THE SESSION GENERATION ONCE THIS POLL'S EDGES HAVE RUN. SessionTick applies the link
   edges BELOW its receive loop, so a HELLO / WELCOME dispatched inline in that loop reads the generation from BEFORE a link-up
   drained in the same Poll. This mirrors the edge block exactly: a moved connect counter while the link is recorded UP is a DOWN
   (+1) and leaves it recorded not-UP; then the transport UP while recorded not-UP is an UP (+1), or the transport not-UP while
   recorded UP is a DOWN (+1). A drop and a re-connect drained in one Poll therefore add 2; with no edge pending it is `gen`. */
inline long LinkGenAfterBatch(long gen, unsigned int connectGen, unsigned int connectGenSeen, int nowUp, int lastUp)
{
    if (connectGen != connectGenSeen && lastUp != 0) { ++gen; lastUp = 0; }
    if (nowUp != 0 && lastUp == 0) ++gen;
    else if (nowUp == 0 && lastUp != 0) ++gen;
    return gen;
}

}   /* namespace coopgl */

#endif
