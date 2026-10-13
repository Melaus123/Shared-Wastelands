/* src/common/steamlisten.h - THE STEAM LISTENER'S DECISIONS. PURE: no Windows.h, no Steam, no globals, no I/O, so the offline suite
   checks exactly what the plugin compiles.

   A host's Steam listener (net/steam_transport.cpp, SteamListener) holds many links on one listen socket: each connection it accepts is
   its own link, up to a cap. This file holds the cap, the verdict on a new arrival, the end reason a refused one is closed with, the
   link table (which place holds which Steam connection under which link id), the relay-only route rule and the per-frame rule of
   Steam's callback pump. */
#ifndef SW_STEAMLISTEN_H
#define SW_STEAMLISTEN_H

namespace steamlisten {

/* Up to 32 Steam joiners on one host. */
const int kLinkCap = 32;

/* Our end reasons, in Steam's application range 1000-1999 (1001-1004 are the transport's own: malformed, over the size cap, a send
   Steam refused, busy). */
const int kEndListenerFull = 1005;     /* the listener already holds its cap of links */
const int kEndListenerNoRoom = 1006;   /* this game could not hold another Steam link (its link registry is full, or the accept failed) */
const int kEndNotRelayed = 1007;       /* a Steam P2P link came up on a route that is not Valve's relay */
const int kEndHostServerDown = 1008;   /* the host's bridge held the arrival while it dialled its world server, which did not answer */

/* The short text a connection closed with one of our listener's end reasons carries (Steam's end debug text); 0 for any other. */
inline const char* EndText(int reason)
{
    if (reason == kEndListenerFull) return "full: this host holds its most Steam links";
    if (reason == kEndListenerNoRoom) return "no room for another Steam link";
    if (reason == kEndNotRelayed) return "not relayed";
    if (reason == kEndHostServerDown) return "the host's world server is not answering";
    return 0;
}

enum { kArriveAccept = 0, kArriveFull = 1, kArriveNoRoom = 2, kArriveClosed = 3 };

/* The cap a listener runs with: 1..kLinkCap as asked; 0 or anything outside = kLinkCap. A test may force it low. */
inline int CapFrom(int asked) { return (asked >= 1 && asked <= kLinkCap) ? asked : kLinkCap; }

/* A new arrival on the listen socket: accepted while fewer than `cap` links are held and the listener is open. */
inline int ArrivalVerdict(int held, int cap, bool open)
{
    if (!open) return kArriveClosed;
    if (held < 0) held = 0;
    return held < CapFrom(cap) ? kArriveAccept : kArriveFull;
}

/* The end reason a refused arrival is closed with; 0 for an accepted one. */
inline int CloseReasonFor(int verdict)
{
    if (verdict == kArriveFull) return kEndListenerFull;
    if (verdict == kArriveNoRoom || verdict == kArriveClosed) return kEndListenerNoRoom;
    return 0;
}

/* A Steam link between players always goes through Valve's relay, so no player's internet address reaches
   another. A P2P link that came up without the relay is closed. A link on plain UDP to 127.0.0.1 or a socket pair inside one game
   (the relay test's) is not a link between players; the rule does not apply to it. */
inline bool RouteAllowed(bool p2p, bool relayed) { return !p2p || relayed; }

/* The relay-only option read back from a new P2P listen socket (ISteamNetworkingUtils::GetConfigValue of P2P_Transport_ICE_Enable at
   listen-socket scope): it holds only when the read succeeded (Valve's ESteamNetworkingGetConfigValueResult OK 1, or OKInherited 2),
   came back as an int32 (ESteamNetworkingConfigDataType 1) of 4 bytes, and is 0 (Disable). Anything else and the socket is closed. */
const int kGetConfigOk = 1;
const int kGetConfigOkInherited = 2;
inline bool IceOffConfirmed(int result, int dataType, int size, int value)
{
    return (result == kGetConfigOk || result == kGetConfigOkInherited) && dataType == 1 && size == 4 && value == 0;
}

/* A remote's Steam id is taken for the host's allow / block rules only when Steam says it checked it: the connection info's
   Unauthenticated flag (k_nSteamNetworkConnectionInfoFlags_Unauthenticated, 1) is clear. */
const int kInfoFlagUnauthenticated = 1;
inline bool IdAuthenticated(int infoFlags) { return (infoFlags & kInfoFlagUnauthenticated) == 0; }

/* THE LINK TABLE. kLinkCap places; a place is free when its id is 0. Link ids count up from 1 and are not reused while the listener
   lives, so a stale id a caller still holds does not reach a newer link. A Steam connection handle is never 0. */
struct LinkTable
{
    unsigned int id[kLinkCap];
    unsigned int handle[kLinkCap];
    unsigned int nextId;
    int          count;
};

inline void Reset(LinkTable* t)
{
    for (int i = 0; i < kLinkCap; ++i) { t->id[i] = 0; t->handle[i] = 0; }
    t->nextId = 1;
    t->count = 0;
}

/* The place holding link `id`, or -1. */
inline int SlotOfId(const LinkTable& t, unsigned int id)
{
    if (id == 0) return -1;
    for (int i = 0; i < kLinkCap; ++i)
        if (t.id[i] == id) return i;
    return -1;
}

/* The place holding Steam connection `handle`, or -1. */
inline int SlotOfHandle(const LinkTable& t, unsigned int handle)
{
    if (handle == 0) return -1;
    for (int i = 0; i < kLinkCap; ++i)
        if (t.id[i] != 0 && t.handle[i] == handle) return i;
    return -1;
}

/* Holds a connection: returns its place and writes its new link id; -1 = refused (handle 0, already held, or `cap` links held). */
inline int Add(LinkTable* t, unsigned int handle, int cap, unsigned int* idOut)
{
    if (handle == 0 || SlotOfHandle(*t, handle) >= 0 || t->count >= CapFrom(cap)) return -1;
    for (int i = 0; i < kLinkCap; ++i)
        if (t->id[i] == 0)
        {
            if (t->nextId == 0) t->nextId = 1;   /* 0 stays the free mark */
            t->id[i] = t->nextId++;
            t->handle[i] = handle;
            ++t->count;
            if (idOut != 0) *idOut = t->id[i];
            return i;
        }
    return -1;
}

/* Frees a place; false = out of range or already free. */
inline bool RemoveSlot(LinkTable* t, int slot)
{
    if (slot < 0 || slot >= kLinkCap || t->id[slot] == 0) return false;
    t->id[slot] = 0;
    t->handle[slot] = 0;
    --t->count;
    return true;
}

/* STEAM'S CALLBACK PUMP, once per frame: from the first Steam link or listener made, for the rest of the session - unless it faulted
   once (it is not called again), SteamAPI_RunCallbacks was not found, the relay probe's wait already called it this frame, or any Steam
   call in this game faulted (no further Steam call). */
inline bool PumpThisTick(bool inUse, bool pumpFaulted, bool haveFn, bool probePumped, bool steamFaulted)
{
    return inUse && !pumpFaulted && haveFn && !probePumped && !steamFaulted;
}

}   /* namespace steamlisten */

#endif   /* SW_STEAMLISTEN_H */
