/* net/steam_transport.cpp - ITransport over Steam Networking Sockets: the second backend, beside ENet.

   WHAT USES IT NOW. Only the relay echo test (net/relay_test.cpp, TEST key relaytest=): two games on one PC over Steam sockets on
   127.0.0.1, or one game alone with a socket pair. No world link, no route and no screen uses it. CreateTransport(BACKEND_STEAM) makes
   one, so the `host steam <port>` verb opens a Steam P2P listener (virtual port 0) instead of refusing.

   STEAM IS REACHED ONLY THROUGH THE PROBE. net/steam_probe.cpp found steam_api64.dll with GetModuleHandle (never LoadLibrary) and
   holds the interface objects the installed Steam client handed back; SteamProbeSockets() / SteamProbeUtils() give them out while no
   Steam call in this game has faulted. GOG (no Steam DLL) or Steam closed: Host and Join refuse with a reason, nothing else happens.

   CALLS BY VTABLE SLOT. The interfaces are C++ objects served by steamclient64.dll; this file calls their virtual functions by slot,
   counted from the `virtual` lines of the Steamworks SDK public headers that declare exactly the versions the probe asks for:
   isteamnetworkingsockets.h (STEAMNETWORKINGSOCKETS_INTERFACE_VERSION "SteamNetworkingSockets012") and isteamnetworkingutils.h
   ("SteamNetworkingUtils004"), SDK 1.58 - SDK 1.53 and 1.54 declare the same functions in the same order. Neither class has a
   virtual destructor, an overloaded virtual or an #if inside it, so the slot is the declaration's position. Every struct passed or
   read is declared below in this file's own words with its size and offsets checked at compile time against the same headers
   (Windows packs them at 8; the address and identity structs at 1).
   SAFETY. Every call into Steam, and every read of memory Steam handed over, is in an __try frame in a function that holds no C++
   object; a fault is counted on the probe (SteamNoteFault), logged once, and this transport stops for good: it makes no further
   Steam call (no retry storm), and the probe hands no transport the interfaces again.

   THE WIRE. Every frame is the ENet frame (type u8 | payload length u32 | payload), split into Steam messages by
   src/common/relaysplit.h (one kind byte; pieces of 256 KB on the reliable lane only; rejoined up to 32 MiB). CH_RELIABLE ->
   k_nSteamNetworkingSend_Reliable (8), CH_UNRELIABLE -> Unreliable (0), both with NoNagle (1): a message leaves at once, as an
   ENet packet does at the next service.

   THE LOCAL SEND QUEUE. Steam refuses a send past its send buffer (k_EResultLimitExceeded). This link raises the buffer to 4 MB and starts the
   send rate at 512 KB/s (4 Mbit/s) when each socket is made, then steps it once a second from Steam's delivery quality and the data
   waiting (128 KB/s to 4 MB/s; Valve's library does not estimate bandwidth itself), and keeps every reliable message Steam has not yet taken in its own queue, in order,
   handing it on at every Poll and Flush: a refused send is retried, never dropped. An unreliable message Steam refuses is dropped and
   counted (the lane is lossy by definition).

   THE END OF A LINK. After the peer closes (or Steam finds a problem here) the messages Steam already received can still be read
   (Valve, steamnetworkingtypes.h): CheckConn reads them all before it frees the handle, so the peer's goodbye arrives. A graceful
   close here raises the connection's send buffer to hold this link's own queue as well (40 MB at most) and hands Steam all of it,
   so Steam's linger sends it. The UP line and the end line carry Steam's route facts (relayed or not, the data centres, the furthest
   state reached; for a link that never connected, Steam's detailed status and the relay network's status).

   CONNECTION EVENTS. Steam reports state changes to the function set when the socket is made (Callback_ConnectionStatusChanged, 201),
   dispatched from Steam's callback pump (SteamAPI_RunCallbacks - the probe calls it every frame from the first Steam transport or
   listener on) and
   from the sockets interface's own RunCallbacks, which Poll calls. Valve's header warns the handler may run on another thread, so the
   function only copies the event into its transport's small queue under a lock. Poll accepts an incoming connection from that queue;
   every edge of the transport's own connection (up, closed by the peer, a problem here) is read from GetConnectionInfo at every
   Poll, so a lost event loses no edge.

   ONE LINK PER TRANSPORT; MANY ON A LISTENER. A transport's own Host accepts one connection at a time; another arriving while one is
   held is closed as busy. The Steam listener (SteamListenerImpl, below the transport) holds many: each connection it accepts becomes
   its own transport (the same send queue, split and rejoin, send-rate step and end handling), up to its cap (steamlisten::kLinkCap,
   32); one arriving past the cap is closed at once with our end reason steamlisten::kEndListenerFull (1005). The remote's Steam id of
   each accepted link is kept for the host's allow / block rules - read from Steam only when Steam reports the connection
   authenticated (the Unauthenticated flag clear), never logged.

   RELAY ONLY. Every P2P socket (a listen socket on virtual port 0, a dial to steam:<id>) is made with
   P2P_Transport_ICE_Enable (104) = Disable (0) - Valve's header: "Do not do any ICE work at all or share any IP addresses with peer" -
   so the only P2P route left is Valve's relay network; a P2P link that reaches Steam's connected state without the relay (Steam's
   Relayed flag, 16, and no relay data centre) is closed with steamlisten::kEndNotRelayed (1007) before it counts as up. On a new P2P
   listen socket the option is read back from Steam (GetConfigValue at listen-socket scope); any value but 0 closes that socket and is
   counted (steamIceOff[read0,not0]). One route line per link says which rule it runs under. The
   relay test's plain-UDP sockets on 127.0.0.1 and its socket pair are not P2P: the rule does not apply to them.

   THE CALLBACK PUMP. Making a Steam transport or listener asks the probe to call SteamAPI_RunCallbacks every frame for the rest of
   the session (SteamPumpForSession); each Poll also calls the sockets interface's own RunCallbacks. */
#include <windows.h>

#include "steam_transport.h"
#include "steam_probe.h"
#include "../coop_log.h"
#include "../../common/relaysplit.h"
#include "../../common/steamlisten.h"
#include "../../common/typetally.h"   /* what each link carried, by message type - the [NET] TRAFFIC REPORT line */
#include "../config.h"   /* ConfigNetRateLog (TEST ONLY) */

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <sstream>
#include <locale>
#include <vector>

namespace coop {
namespace net {

namespace {

std::string N(long long v)   // RE_Kenshi's locale mangles raw numbers (F030)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

bool LogThis(long long n) { return n <= 5 || n % 100 == 0; }   /* the first 5 and every 100th */

/* Why a Steam close or call is not made: the game's exit has already shut Steam down (the probe was told), or a caught fault. */
const char* NoSteamWhy(const char* faulted)
{
    return SteamProbeShutDown() ? "its close is skipped - the game's exit has already shut Steam down, so it ends here without a Steam call" : faulted;
}

/* A Steam delivery quality (0..1) with three decimals in the classic locale, or "unknown". */
std::string QualityText(float q)
{
    if (!relaysplit::SendQualityKnown(q)) return "unknown";
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss.setf(std::ios::fixed);
    ss.precision(3);
    ss << q;
    return ss.str();
}

/* ---- Valve's declarations, in this file's own words: only what is called or read here ---- */

/* ISteamNetworkingSockets, "SteamNetworkingSockets012": the position of each function among the class's virtual declarations. */
const int kSlotCreateListenSocketIP        = 0;
const int kSlotConnectByIPAddress          = 1;
const int kSlotCreateListenSocketP2P       = 2;
const int kSlotConnectP2P                  = 3;
const int kSlotAcceptConnection            = 4;
const int kSlotCloseConnection             = 5;
const int kSlotCloseListenSocket           = 6;
const int kSlotSendMessageToConnection     = 11;
const int kSlotFlushMessagesOnConnection   = 13;
const int kSlotReceiveMessagesOnConnection = 14;
const int kSlotGetConnectionInfo           = 15;
const int kSlotGetConnectionRealTimeStatus = 16;
const int kSlotGetDetailedConnectionStatus = 17;
const int kSlotCreateSocketPair            = 19;
const int kSlotRunCallbacks                = 41;
/* ISteamNetworkingUtils, "SteamNetworkingUtils004" (the probe calls its slots 1 and 7). SetConfigValue and GetConfigValue are the 17th
   and 18th virtual declarations, after SetDebugOutputFunction 13, GetIPv4FakeIPType 14 and GetRealIdentityForFakeIP 15. */
const int kSlotSetConfigValue              = 16;
const int kSlotGetConfigValue              = 17;

/* ESteamNetworkingConfigScope / ESteamNetworkingConfigDataType / ESteamNetworkingConfigValue (steamnetworkingtypes.h). */
const int kCfgScopeGlobal = 1;
const int kCfgScopeListenSocket = 3;
const int kCfgScopeConnection = 4;
const int kCfgTypeInt32 = 1;
const int kCfgTypeFloat = 3;
const int kCfgTypePtr = 5;
const int kCfgFakeLossSend = 2;              /* [global float, 0-100] */
const int kCfgFakeLagSend = 4;               /* [global int32, ms] */
const int kCfgSendBufferSize = 9;            /* [connection int32] default 512 KB */
const int kCfgSendRateMin = 10;              /* [connection int32] bytes/s; default 256 KB/s */
const int kCfgSendRateMax = 11;
const int kCfgIpAllowWithoutAuth = 23;       /* [connection int32] a dev value: IP connections without strong auth are not failed */
const int kCfgTimeoutInitial = 24;           /* [connection int32] ms */
const int kCfgTimeoutConnected = 25;         /* [connection int32] ms */
const int kCfgCallbackStatusChanged = 201;   /* [connection FnSteamNetConnectionStatusChanged] */
const int kCfgP2PTransportIceEnable = 104;   /* [connection int32] what ICE candidates to share with the peer */
const int kIceEnableDisable = 0;             /* k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable: no ICE, no address shared */

/* Send flags; on a received message only the Reliable bit is valid. */
const int kSendUnreliableNoNagle = 1;   /* Unreliable 0 | NoNagle 1 */
const int kSendReliableNoNagle = 9;     /* Reliable 8 | NoNagle 1 */
const int kSendReliableBit = 8;

/* EResult (steamclientpublic.h). */
const int kResultOk = 1;
const int kResultLimitExceeded = 25;

/* ESteamNetworkingConnectionState. */
const int kStateNone = 0;
const int kStateConnecting = 1;
const int kStateFindingRoute = 2;
const int kStateConnected = 3;
const int kStateClosedByPeer = 4;
const int kStateProblemDetectedLocally = 5;

const int kIdentitySteamId = 16;   /* ESteamNetworkingIdentityType: 64-bit Steam id */
const int kIdentityIpAddress = 1;  /* ESteamNetworkingIdentityType: an IP address */

#pragma pack(push, 1)
struct SnIpAddr { unsigned char ip[16]; unsigned short port; };            /* SteamNetworkingIPAddr: IPv6 bytes, port in host order */
struct SnIdentity { int type; int cbSize; unsigned char body[128]; };     /* SteamNetworkingIdentity */
#pragma pack(pop)

struct SnConnInfo   /* SteamNetConnectionInfo_t */
{
    SnIdentity     identityRemote;
    long long      userData;
    unsigned int   listenSocket;
    SnIpAddr       addrRemote;
    unsigned short pad1;
    unsigned int   popRemote;
    unsigned int   popRelay;
    int            state;
    int            endReason;
    char           endDebug[128];
    char           description[128];
    int            flags;
    unsigned int   reserved[63];
};

struct SnStatusChanged   /* SteamNetConnectionStatusChangedCallback_t */
{
    unsigned int conn;
    SnConnInfo   info;
    int          oldState;
};

struct SnRealTimeStatus   /* SteamNetConnectionRealTimeStatus_t */
{
    int          state;
    int          ping;
    float        qualityLocal;
    float        qualityRemote;
    float        outPacketsPerSec;
    float        outBytesPerSec;
    float        inPacketsPerSec;
    float        inBytesPerSec;
    int          sendRateBytesPerSecond;
    int          pendingUnreliable;
    int          pendingReliable;
    int          sentUnackedReliable;
    long long    queueTimeUsec;
    unsigned int reserved[16];
};

struct SnMessage;
typedef void (*FnMessageFree)(SnMessage* m);
struct SnMessage   /* SteamNetworkingMessage_t; Release() is (*m_pfnRelease)(this) */
{
    void*          data;
    int            size;
    unsigned int   conn;
    SnIdentity     identityPeer;
    long long      connUserData;
    long long      timeReceivedUsec;
    long long      messageNumber;
    FnMessageFree  freeData;
    FnMessageFree  release;
    int            channel;
    int            flags;
    long long      userData;
    unsigned short lane;
    unsigned short pad;
};

struct SnConfigValue   /* SteamNetworkingConfigValue_t */
{
    int value;
    int dataType;
    union { int i32; long long i64; float f; const char* s; void* p; } v;
};

typedef char SnIpAddrSize[sizeof(SnIpAddr) == 18 ? 1 : -1];
typedef char SnIdentitySize[sizeof(SnIdentity) == 136 ? 1 : -1];
typedef char SnConnInfoSize[(sizeof(SnConnInfo) == 696 && offsetof(SnConnInfo, userData) == 136 && offsetof(SnConnInfo, listenSocket) == 144
    && offsetof(SnConnInfo, addrRemote) == 148 && offsetof(SnConnInfo, popRelay) == 172 && offsetof(SnConnInfo, state) == 176
    && offsetof(SnConnInfo, endReason) == 180 && offsetof(SnConnInfo, endDebug) == 184 && offsetof(SnConnInfo, flags) == 440) ? 1 : -1];
typedef char SnStatusChangedSize[(offsetof(SnStatusChanged, info) == 8 && offsetof(SnStatusChanged, oldState) == 704) ? 1 : -1];
typedef char SnRealTimeStatusSize[(sizeof(SnRealTimeStatus) == 120 && offsetof(SnRealTimeStatus, pendingUnreliable) == 36
    && offsetof(SnRealTimeStatus, queueTimeUsec) == 48) ? 1 : -1];
typedef char SnMessageSize[(sizeof(SnMessage) == 216 && offsetof(SnMessage, conn) == 12 && offsetof(SnMessage, connUserData) == 152
    && offsetof(SnMessage, release) == 184 && offsetof(SnMessage, flags) == 196) ? 1 : -1];
typedef char SnConfigValueSize[(sizeof(SnConfigValue) == 16 && offsetof(SnConfigValue, v) == 8) ? 1 : -1];
typedef char SnReadFieldsAt[(offsetof(SnConnInfo, popRemote) == 168 && offsetof(SnConnInfo, description) == 312
    && offsetof(SnConnInfo, flags) == 440 && offsetof(SnRealTimeStatus, inPacketsPerSec) == 24) ? 1 : -1];
typedef char SnRateFieldsAt[(offsetof(SnRealTimeStatus, qualityLocal) == 8 && offsetof(SnRealTimeStatus, qualityRemote) == 12
    && offsetof(SnRealTimeStatus, pendingReliable) == 40) ? 1 : -1];   /* the send-rate step reads these (SDK 1.58 order) */

/* The functions, `self` first (x64: one calling convention; a C++ reference parameter is a pointer). */
typedef unsigned int (*FnCreateListenSocketIP)(void* self, const SnIpAddr* local, int nOptions, const SnConfigValue* options);
typedef unsigned int (*FnConnectByIPAddress)(void* self, const SnIpAddr* addr, int nOptions, const SnConfigValue* options);
typedef unsigned int (*FnCreateListenSocketP2P)(void* self, int virtualPort, int nOptions, const SnConfigValue* options);
typedef unsigned int (*FnConnectP2P)(void* self, const SnIdentity* remote, int virtualPort, int nOptions, const SnConfigValue* options);
typedef int  (*FnAcceptConnection)(void* self, unsigned int conn);
typedef bool (*FnCloseConnection)(void* self, unsigned int conn, int reason, const char* debug, bool linger);
typedef bool (*FnCloseListenSocket)(void* self, unsigned int sock);
typedef int  (*FnSendMessageToConnection)(void* self, unsigned int conn, const void* data, unsigned int len, int flags, long long* outNumber);
typedef int  (*FnFlushMessagesOnConnection)(void* self, unsigned int conn);
typedef int  (*FnReceiveMessagesOnConnection)(void* self, unsigned int conn, SnMessage** out, int maxMessages);
typedef bool (*FnGetConnectionInfo)(void* self, unsigned int conn, SnConnInfo* info);
typedef int  (*FnGetConnectionRealTimeStatus)(void* self, unsigned int conn, SnRealTimeStatus* status, int nLanes, void* lanes);
typedef int  (*FnGetDetailedConnectionStatus)(void* self, unsigned int conn, char* buf, int cbBuf);   /* 0 ok, >0 truncated, -1 bad handle */
typedef bool (*FnCreateSocketPair)(void* self, unsigned int* out1, unsigned int* out2, bool loopback, const SnIdentity* id1, const SnIdentity* id2);
typedef void (*FnIfaceRunCallbacks)(void* self);
typedef bool (*FnSetConfigValue)(void* self, int value, int scope, intptr_t scopeObj, int dataType, const void* arg);
/* Returns ESteamNetworkingGetConfigValueResult; the data type comes back as ESteamNetworkingConfigDataType (both 32-bit enums);
   cbResult: in, the buffer's size; out, the bytes filled in or needed. */
typedef int  (*FnGetConfigValue)(void* self, int value, int scope, intptr_t scopeObj, int* outDataType, void* result, size_t* cbResult);

/* ---- this file's limits ---- */
const int kEventMax = 32;            /* connection events one transport holds between two Polls */
const int kLiveMax = 2 * steamlisten::kLinkCap + 16;   /* Steam transports alive at once: a full listener's links, the relay test's
                                                          dials to it, a joining link */
const int kListenerMax = 4;          /* Steam listeners alive at once (a host opens one) */
const int kListenEventMax = 128;     /* connection events one listener holds between two Polls */
const int kRecvBatch = 64;           /* messages taken from Steam per call */
const int kRecvBatchesPerPoll = 16;  /* one Poll takes at most 1,024 messages: the main thread's frame stays bounded */
const int kSendBufferBytes = 4194304;     /* 4 MB: Steam's 512 KB default holds two pieces of a 6 MB record */
const int kSendRateBytes = relaysplit::kSendRateStartBytes;   /* 512 KB/s (4 Mbit/s), min and max alike (Valve: set both the same): the
                                                                  rate each connection starts at; Steam's default is 256 KB/s */
const int kRateLogMax = 40;               /* send-rate changes logged per link; the rest are counted (steamLink rateChanges) */
const DWORD kRateStepMs = 1000;           /* the send rate is stepped once a second while the link is UP */
const int kLingerBufferCapBytes = 41943040;   /* 40 MB: a graceful close's send buffer at most (the link's own queue + the normal 4 MB) */
const int kTimeoutInitialMs = 10000;
const int kTimeoutConnectedMs = 10000;
const int kEndMalformed = 1001;      /* our close reasons, in Steam's application range 1000-1999 */
const int kEndOverCap = 1002;
const int kEndSendFailed = 1003;
const int kEndBusy = 1004;

/* One state change, copied out of Steam's callback struct (plain data: it is written under the lock, maybe on another thread). */
struct SnEvent
{
    unsigned int conn;
    unsigned int listenSocket;
    int          state;
    int          oldState;
};

struct LinkStats
{
    long long up, upMs, sent, recv, bytesOut, bytesIn, pieces, rejoined, queuedMax, refusedBig, endReason;
    long long limitHits, events, stray, dropped, unrelDropped, recvBad;
    long long rateNow, rateMin, rateMax, rateChanges;   /* send rates (bytes/s): the latest set, the lowest and highest run at, changes */
};
LinkStats g_stats;   /* every Steam transport of this game, summed (static storage: zero) */

struct ListenStats
{
    long long opened, heldMax, accepted, refusedFull, refusedOther, ended;
};
ListenStats g_listenStats;   /* every Steam listener of this game, summed (static storage: zero) */

struct IceStats
{
    long long read0, not0;   /* P2P listen sockets whose relay-only option read back as 0; ones whose did not (each closed) */
};
IceStats g_iceStats;   /* every P2P listen socket of this game (static storage: zero) */

/* A send rate a Steam link now runs at, into the REPORT's rateNow / rateMin / rateMax. */
void NoteRate(int rate)
{
    g_stats.rateNow = rate;
    if (g_stats.rateMin == 0 || rate < g_stats.rateMin) g_stats.rateMin = rate;
    if (rate > g_stats.rateMax) g_stats.rateMax = rate;
}

CRITICAL_SECTION g_cs;
bool g_csReady = false;
volatile LONG g_cbFaults = 0;   /* faults reading Steam's callback struct (the handler may be on another thread) */

class SteamTransport;
SteamTransport* g_live[kLiveMax];   /* the registry the callback routes by; written under g_cs */
class SteamListenerImpl;
SteamListenerImpl* g_listeners[kListenerMax];   /* the listeners, for the callback's routing of new arrivals; written under g_cs */

void OnStatusChanged(SnStatusChanged* cb);

void CopyText(char* dst, size_t cap, const char* src)
{
    size_t j = 0;
    for (size_t i = 0; src != 0 && i < cap && src[i] != 0 && j + 1 < cap; ++i)
        dst[j++] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
    dst[j] = 0;
}

/* Steam's multi-line detailed status as one log fragment: line breaks become " | ", tabs and carriage returns a space. */
std::string FlatText(const char* src, size_t cap)
{
    std::string s;
    for (size_t i = 0; i < cap && src[i] != 0; ++i)
    {
        const char c = src[i];
        if (c == '\n') s += " | ";
        else if (c == '\r' || c == '\t') s += ' ';
        else s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

/* The remote identity's TYPE only - steamid, ip, none, or its type number - never the id or the address: logs travel in bug
   reports. */
std::string IdentityText(const SnIdentity& id)
{
    if (id.type == kIdentitySteamId) return "steamid";
    if (id.type == kIdentityIpAddress) return "ip";
    if (id.type == 0) return "none";   /* k_ESteamNetworkingIdentityType_Invalid */
    return "type" + N(id.type);
}

void SetInt(SnConfigValue* c, int value, int x)
{
    c->value = value;
    c->dataType = kCfgTypeInt32;
    c->v.i64 = 0;
    c->v.i32 = x;
}

/* ::ffff:127.0.0.1 - SteamNetworkingIPAddr::SetIPv4's IPv4-mapped form: ten zero bytes, two 0xff bytes, the address in network order. */
SnIpAddr Loopback(unsigned short port)
{
    SnIpAddr a;
    std::memset(&a, 0, sizeof(a));
    a.ip[10] = 255; a.ip[11] = 255;
    a.ip[12] = 127; a.ip[15] = 1;
    a.port = port;
    return a;
}

/* "steam:<64-bit decimal id>" -> the id. */
bool ParseSteamId(const std::string& a, unsigned long long* out)
{
    if (a.size() <= 6 || a.size() > 26 || a.compare(0, 6, "steam:") != 0) return false;
    const unsigned long long kMax = ~0ULL;
    unsigned long long v = 0;
    for (size_t i = 6; i < a.size(); ++i)
    {
        if (a[i] < '0' || a[i] > '9') return false;
        const unsigned long long d = (unsigned long long)(a[i] - '0');
        if (v > (kMax - d) / 10) return false;
        v = v * 10 + d;
    }
    if (v == 0) return false;
    *out = v;
    return true;
}

/* The options every socket of ours is made with: the send buffer and rate, the timeouts, our status-change function; on a P2P socket
   ICE off (relay only); allow-without-auth only in the relay test's plain-UDP mode. A connection a listen socket accepts takes the
   listen socket's. `o` holds 8; at most 7 are written. */
int MakeOptions(SnConfigValue* o, unsigned int timeoutConnectedMs, bool allowNoAuth, bool p2p)
{
    int n = 0;
    SetInt(&o[n++], kCfgSendBufferSize, kSendBufferBytes);
    SetInt(&o[n++], kCfgSendRateMin, kSendRateBytes);
    SetInt(&o[n++], kCfgSendRateMax, kSendRateBytes);
    SetInt(&o[n++], kCfgTimeoutInitial, kTimeoutInitialMs);
    SetInt(&o[n++], kCfgTimeoutConnected, (int)timeoutConnectedMs);
    if (p2p) SetInt(&o[n++], kCfgP2PTransportIceEnable, kIceEnableDisable);
    else if (allowNoAuth) SetInt(&o[n++], kCfgIpAllowWithoutAuth, 1);
    o[n].value = kCfgCallbackStatusChanged;
    o[n].dataType = kCfgTypePtr;
    o[n].v.i64 = 0;
    o[n].v.p = (void*)&OnStatusChanged;
    ++n;
    return n;
}

/* ---- the only frames that call into Steam or read Steam's memory; no C++ objects below this line until the next marker ---- */

int SehListenIP(void* s, const SnIpAddr* a, int n, const SnConfigValue* o, unsigned int* out)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotCreateListenSocketIP] == 0) return 0;
            *out = ((FnCreateListenSocketIP)vt[kSlotCreateListenSocketIP])(s, a, n, o); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehConnectIP(void* s, const SnIpAddr* a, int n, const SnConfigValue* o, unsigned int* out)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotConnectByIPAddress] == 0) return 0;
            *out = ((FnConnectByIPAddress)vt[kSlotConnectByIPAddress])(s, a, n, o); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehListenP2P(void* s, int vport, int n, const SnConfigValue* o, unsigned int* out)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotCreateListenSocketP2P] == 0) return 0;
            *out = ((FnCreateListenSocketP2P)vt[kSlotCreateListenSocketP2P])(s, vport, n, o); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehConnectP2P(void* s, const SnIdentity* id, int vport, int n, const SnConfigValue* o, unsigned int* out)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotConnectP2P] == 0) return 0;
            *out = ((FnConnectP2P)vt[kSlotConnectP2P])(s, id, vport, n, o); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehAccept(void* s, unsigned int conn, int* res)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotAcceptConnection] == 0) return 0;
            *res = ((FnAcceptConnection)vt[kSlotAcceptConnection])(s, conn); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehCloseConn(void* s, unsigned int conn, int reason, const char* debug, bool linger)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotCloseConnection] == 0) return 0;
            ((FnCloseConnection)vt[kSlotCloseConnection])(s, conn, reason, debug, linger); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehCloseListen(void* s, unsigned int sock)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotCloseListenSocket] == 0) return 0;
            ((FnCloseListenSocket)vt[kSlotCloseListenSocket])(s, sock); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehSend(void* s, unsigned int conn, const void* data, unsigned int len, int flags, int* res)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotSendMessageToConnection] == 0) return 0;
            *res = ((FnSendMessageToConnection)vt[kSlotSendMessageToConnection])(s, conn, data, len, flags, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehFlush(void* s, unsigned int conn)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotFlushMessagesOnConnection] == 0) return 0;
            ((FnFlushMessagesOnConnection)vt[kSlotFlushMessagesOnConnection])(s, conn); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehReceive(void* s, unsigned int conn, SnMessage** out, int maxMessages, int* n)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotReceiveMessagesOnConnection] == 0) return 0;
            *n = ((FnReceiveMessagesOnConnection)vt[kSlotReceiveMessagesOnConnection])(s, conn, out, maxMessages); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehInfo(void* s, unsigned int conn, SnConnInfo* info, int* ok)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotGetConnectionInfo] == 0) return 0;
            *ok = ((FnGetConnectionInfo)vt[kSlotGetConnectionInfo])(s, conn, info) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehStatus(void* s, unsigned int conn, SnRealTimeStatus* st, int* res)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotGetConnectionRealTimeStatus] == 0) return 0;
            *res = ((FnGetConnectionRealTimeStatus)vt[kSlotGetConnectionRealTimeStatus])(s, conn, st, 0, 0); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehDetail(void* s, unsigned int conn, char* buf, int cbBuf, int* res)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotGetDetailedConnectionStatus] == 0) return 0;
            *res = ((FnGetDetailedConnectionStatus)vt[kSlotGetDetailedConnectionStatus])(s, conn, buf, cbBuf); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehPair(void* s, unsigned int* c1, unsigned int* c2, int* ok)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotCreateSocketPair] == 0) return 0;
            *ok = ((FnCreateSocketPair)vt[kSlotCreateSocketPair])(s, c1, c2, true, 0, 0) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehIfaceRunCallbacks(void* s)
{
    __try { void** vt = *(void***)s; if (vt == 0 || vt[kSlotRunCallbacks] == 0) return 0;
            ((FnIfaceRunCallbacks)vt[kSlotRunCallbacks])(s); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehSetConfig(void* u, int value, int scope, intptr_t obj, int dataType, const void* arg, int* ok)
{
    __try { void** vt = *(void***)u; if (vt == 0 || vt[kSlotSetConfigValue] == 0) return 0;
            *ok = ((FnSetConfigValue)vt[kSlotSetConfigValue])(u, value, scope, obj, dataType, arg) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehGetConfig(void* u, int value, int scope, intptr_t obj, int* dataType, void* res, size_t* cb, int* rc)
{
    __try { void** vt = *(void***)u; if (vt == 0 || vt[kSlotGetConfigValue] == 0) return 0;
            *rc = ((FnGetConfigValue)vt[kSlotGetConfigValue])(u, value, scope, obj, dataType, res, cb); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehMsgFields(const SnMessage* m, const char** data, int* size, int* flags)
{
    __try { *data = (const char*)m->data; *size = m->size; *flags = m->flags; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehMsgCopy(const char* src, char* dst, int n)
{
    __try { std::memcpy(dst, src, (size_t)n); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* SteamNetworkingMessage_t::Release() is an inline in Valve's header: (*m_pfnRelease)(this). */
int SehMsgRelease(SnMessage* m)
{
    __try { if (m->release == 0) return 0; m->release(m); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehReadEvent(const SnStatusChanged* cb, SnEvent* e)
{
    __try { e->conn = cb->conn; e->listenSocket = cb->info.listenSocket; e->state = cb->info.state; e->oldState = cb->oldState; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

/* ---- end of the __try frames ---- */

/* A new P2P listen socket was made with P2P_Transport_ICE_Enable = 0 among its options: Steam's own value at listen-socket scope on that
   handle is read back, so a Steam client that dropped the option cannot leave a listener that shares addresses. 1 = it reads an int32 0
   (relay only); 0 = anything else, or no utils interface to read it with (the caller closes the socket); -1 = a fault in the read (the
   caller stops). *line = what was read, for the log. Counted on g_iceStats. */
int IceOffReadBack(void* u, unsigned int h, std::string* line)
{
    if (u == 0)
    {
        *line = "no Steam utils interface to read it with";
        ++g_iceStats.not0;
        return 0;
    }
    int rc = 0, type = 0, value = -99;
    size_t cb = sizeof(value);
    if (!SehGetConfig(u, kCfgP2PTransportIceEnable, kCfgScopeListenSocket, (intptr_t)h, &type, &value, &cb, &rc)) return -1;
    *line = "P2P_Transport_ICE_Enable at listen-socket scope: result " + N(rc) + ", type " + N(type) + ", size " + N((long long)cb)
            + ", value " + N(value);
    const bool ok = steamlisten::IceOffConfirmed(rc, type, (int)cb, value);
    if (ok) ++g_iceStats.read0; else ++g_iceStats.not0;
    return ok ? 1 : 0;
}

void EnsureLock()
{
    if (!g_csReady)
    {
        ::InitializeCriticalSection(&g_cs);
        g_csReady = true;
    }
}

class SteamTransport : public ITransport
{
public:
    SteamTransport()
        : sockets_(0), listen_(0), conn_(0), state_(LINK_DOWN), hosting_(false), broken_(false), ipTest_(false), allowNoAuth_(false),
          sent_(0), recv_(0), bytesOut_(0), bytesIn_(0), connectGen_(0), nextPeerId_(1), peerId_(0), ping_(0),
          steamPending_(0), queueBytes_(0), dialMs_(0), upMs_(0), lastRecvMs_(0), relayed_(0), endReason_(0), maxState_(0), endedConn_(0),
          timeoutConnectedMs_(kTimeoutConnectedMs), nEvents_(0), registered_(false), rate_(kSendRateBytes), rateNextMs_(0), rateFrozen_(false),
          rateLogged_(0), p2p_(false), remoteKind_(0), remoteId64_(0), rateLogMs_(0), tallyLink_(nettally::kLinkSession)
    {
        endText_[0] = 0;
        EnsureLock();
        ::EnterCriticalSection(&g_cs);
        for (int i = 0; i < kLiveMax; ++i)
            if (g_live[i] == 0) { g_live[i] = this; registered_ = true; break; }
        ::LeaveCriticalSection(&g_cs);
        if (!registered_)
        {
            broken_ = true;
            stopWhy_ = "more than " + N(kLiveMax) + " Steam links alive in this game";
        }
        SteamPumpForSession();   /* Steam is in use: the probe calls SteamAPI_RunCallbacks every frame from now on */
    }

    ~SteamTransport()
    {
        Close(true);
        ::EnterCriticalSection(&g_cs);
        for (int i = 0; i < kLiveMax; ++i)
            if (g_live[i] == this) g_live[i] = 0;
        ::LeaveCriticalSection(&g_cs);
    }

    bool Host(unsigned short port, std::string* errOut)
    {
        if (!Ready(errOut)) { if (!broken_) state_ = LINK_FAILED; return false; }
        Close(true);
        if (broken_) { if (errOut) *errOut = stopWhy_; return false; }
        SnConfigValue o[8];
        const int n = Options(o, !ipTest_);
        unsigned int h = 0;
        int called = 0;
        if (ipTest_)
        {
            const SnIpAddr a = Loopback(port);
            called = SehListenIP(sockets_, &a, n, o, &h);
        }
        else
            called = SehListenP2P(sockets_, 0, n, o, &h);
        if (!called)
        {
            Fault(ipTest_ ? "CreateListenSocketIP" : "CreateListenSocketP2P");
            if (errOut) *errOut = stopWhy_;
            return false;
        }
        if (h == 0)
        {
            if (errOut) *errOut = "Steam refused to open the listen socket";
            state_ = LINK_FAILED;
            ErrorLog("[net] Steam link: Steam refused to open the listen socket" + std::string(ipTest_ ? " on 127.0.0.1:" + N(port) : " on P2P virtual port 0"));
            return false;
        }
        if (!ipTest_)
        {
            std::string read;
            const int r = IceOffReadBack(SteamProbeUtils(), h, &read);
            if (r < 0)
            {
                Fault("GetConfigValue");
                if (errOut) *errOut = stopWhy_;
                return false;
            }
            if (r == 0)
            {
                ErrorLog("[net] Steam link: the relay-only option did NOT read back as 0 on the new P2P listen socket (" + read
                         + ") - closing it: no P2P listener runs that could share an address");
                if (SteamProbeSockets() != 0 && !SehCloseListen(sockets_, h))   /* no Steam call after any fault */
                {
                    Fault("CloseListenSocket");
                    if (errOut) *errOut = stopWhy_;
                    return false;
                }
                if (errOut) *errOut = "Steam did not keep the relay-only option on the listen socket (" + read + ")";
                state_ = LINK_FAILED;
                return false;
            }
            DebugLog("[net] Steam link: relay-only option read back on the P2P listen socket - " + read);
        }
        listen_ = h;
        hosting_ = true;
        state_ = LINK_LISTENING;
        DebugLog(ipTest_ ? "[net] Steam link: listening on 127.0.0.1:" + N(port) + " over ordinary UDP (relay test; allow-without-auth "
                           + (allowNoAuth_ ? "on" : "off") + ")"
                         : std::string("[net] Steam link: listening on Steam P2P virtual port 0, relay only (ICE off: no address shared; Valve's relay is the only route)"));
        return true;
    }

    bool Join(const std::string& address, unsigned short port, std::string* errOut)
    {
        if (!Ready(errOut)) { if (!broken_) state_ = LINK_FAILED; return false; }
        unsigned long long id64 = 0;
        const bool p2p = ParseSteamId(address, &id64);
        if (!p2p && !(ipTest_ && address == "127.0.0.1"))
        {
            if (errOut) *errOut = "a Steam link dials steam:<64-bit id>, or 127.0.0.1 in the relay test only - not '" + relaysplit::HideIdNumbers(address) + "'";
            if (conn_ == 0 && listen_ == 0) state_ = LINK_FAILED;   /* a refused address leaves a live link or listener as it is */
            return false;
        }
        Close(true);
        if (broken_) { if (errOut) *errOut = stopWhy_; return false; }
        SnConfigValue o[8];
        const int n = Options(o, p2p);
        unsigned int h = 0;
        int called = 0;
        if (p2p)
        {
            SnIdentity id;
            std::memset(&id, 0, sizeof(id));
            id.type = kIdentitySteamId;
            id.cbSize = 8;
            std::memcpy(id.body, &id64, 8);
            called = SehConnectP2P(sockets_, &id, 0, n, o, &h);
        }
        else
        {
            const SnIpAddr a = Loopback(port);
            called = SehConnectIP(sockets_, &a, n, o, &h);
        }
        if (!called)
        {
            Fault(p2p ? "ConnectP2P" : "ConnectByIPAddress");
            if (errOut) *errOut = stopWhy_;
            return false;
        }
        if (h == 0)
        {
            if (errOut) *errOut = "Steam refused the connection attempt";
            state_ = LINK_FAILED;
            return false;
        }
        conn_ = h;
        hosting_ = false;
        p2p_ = p2p;
        peerId_ = 0;   /* the host is peer 0 everywhere */
        state_ = LINK_CONNECTING;
        dialMs_ = ::GetTickCount();
        endReason_ = 0;
        endText_[0] = 0;
        maxState_ = 0;
        DebugLog(p2p ? std::string("[net] Steam link: connecting to a Steam id on P2P virtual port 0")
                     : "[net] Steam link: connecting to 127.0.0.1:" + N(port) + " over ordinary UDP (relay test; allow-without-auth "
                       + (allowNoAuth_ ? "on" : "off") + ")");
        return true;
    }

    void Disconnect() { Close(true); }
    void Abort() { Close(false); }

    void Poll(std::vector<Message>* out)
    {
        if (broken_ || (listen_ == 0 && conn_ == 0)) return;
        if (SteamProbeSockets() == 0)
        {
            Stop(NoSteamWhy("another Steam call in this game faulted"));
            return;
        }
        if (g_cbFaults != 0) { Fault("reading a connection event"); return; }
        if (!SehIfaceRunCallbacks(sockets_)) { Fault("ISteamNetworkingSockets::RunCallbacks"); return; }
        /* The held connection first: if it ended this frame its last messages are read and it is freed, so a host's re-dial whose new
           incoming connection arrives in this same frame finds the listener free, not busy. A connection accepted now is checked after. */
        const unsigned int held = conn_;
        if (held != 0) CheckConn(out);
        if (broken_) return;
        TakeEvents();
        if (broken_ || conn_ == 0) return;
        if (conn_ != held) CheckConn(out);
        if (broken_ || conn_ == 0 || state_ != LINK_UP) return;
        Receive(out, false);
        if (broken_ || conn_ == 0 || state_ != LINK_UP) return;
        PumpQueue();
        if (broken_ || conn_ == 0 || state_ != LINK_UP) return;
        RefreshStatus();
    }

    bool Send(unsigned int /*peer*/, MsgType type, const char* data, size_t len, Channel ch)
    {
        if (broken_ || conn_ == 0 || state_ != LINK_UP) return false;
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return false; }   /* no Steam call after any fault */
        const bool reliable = (ch == CH_RELIABLE);
        const size_t frameBytes = 5 + len;
        const int verdict = relaysplit::SendVerdict(frameBytes, reliable);
        if (verdict != relaysplit::kSendOk)
        {
            ++g_stats.refusedBig;
            if (LogThis(g_stats.refusedBig))
                ErrorLog("[net] Steam link: REFUSED a frame of " + N((long long)frameBytes) + " bytes on the "
                         + std::string(reliable ? "guaranteed" : "unreliable") + " lane - "
                         + (verdict == relaysplit::kSendTooBig ? "over 32 MiB, ENet's cap too" : "an unreliable frame must fit one 256 KB piece")
                         + " (counted steamLink[refusedBig]; the first 5 and every 100th are logged)");
            return false;
        }
        std::vector<char> frame(frameBytes);
        frame[0] = (char)(unsigned char)type;
        const unsigned int len32 = (unsigned int)len;
        std::memcpy(&frame[1], &len32, 4);
        if (len != 0) std::memcpy(&frame[5], data, len);
        std::vector<std::vector<char> > msgs;
        if (!relaysplit::Split(&frame[0], frame.size(), reliable, &msgs)) return false;
        if (!reliable)
        {
            int res = 0;
            if (!SehSend(sockets_, conn_, &msgs[0][0], (unsigned int)msgs[0].size(), kSendUnreliableNoNagle, &res))
            {
                Fault("SendMessageToConnection");
                return false;
            }
            if (res != kResultOk)
            {
                ++g_stats.unrelDropped;   /* a full send buffer (or a closing link) drops an unreliable message; the lane is lossy */
                return false;
            }
            CountSent(frameBytes, msgs.size());   /* an unreliable frame counts as sent only once Steam took it */
            nettally::CountOut(tallyLink_, (unsigned int)type, frameBytes);
            return true;
        }
        CountSent(frameBytes, msgs.size());   /* a reliable frame counts when queued: it is retried, never dropped, while the link lives */
        nettally::CountOut(tallyLink_, (unsigned int)type, frameBytes);
        for (size_t i = 0; i < msgs.size(); ++i)
        {
            queueBytes_ += (long long)msgs[i].size();
            queue_.push_back(std::vector<char>());
            queue_.back().swap(msgs[i]);
        }
        PumpQueue();
        NoteQueued();
        return !broken_ && conn_ != 0;   /* a send Steam refused ends the link inside PumpQueue */
    }

    void Flush()
    {
        if (broken_ || conn_ == 0 || state_ != LINK_UP) return;
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return; }   /* no Steam call after any fault */
        PumpQueue();
        if (broken_ || conn_ == 0) return;
        if (!SehFlush(sockets_, conn_)) Fault("FlushMessagesOnConnection");
    }

    LinkState   State() const { return state_; }
    BackendId   Backend() const { return BACKEND_STEAM; }
    const char* BackendName() const { return "steam"; }
    void SetStoreLink() { if (tallyLink_ == nettally::kLinkSession) tallyLink_ = nettally::kLinkWorld; }   /* counted as the world link; nothing else changes on Steam */
    void SetTrafficLink(int link) { tallyLink_ = link; }   /* the [NET] TRAFFIC REPORT line this link is counted on - see transport.h */
    unsigned int PeerCount() const { return state_ == LINK_UP ? 1u : 0u; }
    bool CurrentPeerId(unsigned int* out) const
    {
        if (state_ != LINK_UP) return false;
        if (out != 0) *out = peerId_;
        return true;
    }
    unsigned int ConnectGen() const { return connectGen_; }
    /* Steam has one connected-link timeout (TimeoutConnected): the maximum term, for every connection made from now on. */
    void SetPeerTimeouts(unsigned int /*limit*/, unsigned int /*minimumMs*/, unsigned int maximumMs)
    {
        timeoutConnectedMs_ = maximumMs != 0 ? maximumMs : (unsigned int)kTimeoutConnectedMs;
    }
    unsigned int HostSilenceMs() const
    {
        return state_ == LINK_UP ? (unsigned int)(DWORD)(::GetTickCount() - lastRecvMs_) : 0u;
    }
    /* This link's own queue, plus what Steam reported at the last Poll as pending (both lanes) and sent-but-unacknowledged. Steam
       reports bytes, not messages: its share is counted in 256 KB pieces. */
    bool SendQueued(long long* packets, long long* bytes) const
    {
        if (state_ != LINK_UP) return false;
        const long long piece = (long long)relaysplit::kPieceBytes;
        if (packets != 0) *packets = (long long)queue_.size() + (steamPending_ + piece - 1) / piece;
        if (bytes != 0) *bytes = queueBytes_ + steamPending_;
        return true;
    }
    bool ByteCounts(unsigned long long* sent, unsigned long long* recv) const
    {
        if (sent != 0) *sent = bytesOut_;
        if (recv != 0) *recv = bytesIn_;
        return true;
    }
    unsigned int RoundTripMs() const { return ping_; }
    unsigned long long SentCount() const { return sent_; }
    unsigned long long RecvCount() const { return recv_; }

    /* ---- the relay test's controls (steam_transport.h) ---- */
    void SetIpTest(bool allowWithoutAuth) { ipTest_ = true; allowNoAuth_ = allowWithoutAuth; }

    bool PairWith(SteamTransport* other, std::string* errOut)
    {
        if (!Ready(errOut) || !other->Ready(errOut)) return false;
        Close(true);
        other->Close(true);
        if (broken_ || other->broken_) { if (errOut) *errOut = "a Steam link stopped"; return false; }
        unsigned int c1 = 0, c2 = 0;
        int ok = 0;
        if (!SehPair(sockets_, &c1, &c2, &ok))
        {
            Fault("CreateSocketPair");
            if (errOut) *errOut = stopWhy_;
            return false;
        }
        if (!ok || c1 == 0 || c2 == 0)
        {
            if (errOut) *errOut = "Steam refused CreateSocketPair";
            return false;
        }
        const int set1 = ConfigureConn(c1);
        const int set2 = ConfigureConn(c2);
        if (broken_) { if (errOut) *errOut = stopWhy_; return false; }
        Adopt(c1, false);
        other->Adopt(c2, true);
        DebugLog("[net] Steam link: socket pair made over network loopback, both ends in this game (send buffer / rate set on "
                 + N(set1 + set2) + " of 6 values)");
        return true;
    }

    void Facts(SteamLinkFacts* f) const
    {
        f->upMs = upMs_;
        f->relayed = relayed_;
        f->endReason = endReason_;
        f->endText = endText_;
        f->stopped = broken_ ? 1 : 0;
        f->stopWhy = stopWhy_;
    }

    /* The callback's routing tests and push; all run under g_cs. A connection's events go to the transport holding it first (a
       listener's accepted link is its own transport); an arrival goes to the owner of its listen socket. */
    bool OwnsConn(const SnEvent& e) const { return conn_ != 0 && e.conn == conn_; }
    bool OwnsListen(const SnEvent& e) const { return listen_ != 0 && e.listenSocket == listen_; }

    /* ---- for the Steam listener (SteamListenerImpl) ---- */
    bool Stopped() const { return broken_; }
    /* A connection the listener accepted on its own listen socket becomes this transport's link (its options came from that socket). */
    bool TakeAccepted(unsigned int h, bool p2p, std::string* errOut)
    {
        if (!Ready(errOut)) return false;
        Adopt(h, true);
        p2p_ = p2p;
        return true;
    }
    /* The remote's 64-bit Steam id as Steam reported it when the link came up, when it is one. Never logged. */
    bool RemoteSteamId(unsigned long long* out) const
    {
        if (remoteKind_ != kIdentitySteamId || remoteId64_ == 0) return false;
        if (out != 0) *out = remoteId64_;
        return true;
    }
    bool PushEvent(const SnEvent& e)
    {
        if (nEvents_ >= kEventMax) return false;
        events_[nEvents_++] = e;
        return true;
    }

private:
    bool Ready(std::string* errOut)
    {
        if (broken_)
        {
            if (errOut) *errOut = "this Steam link stopped: " + stopWhy_;
            return false;
        }
        sockets_ = SteamProbeSockets();
        if (sockets_ == 0)
        {
            if (errOut) *errOut = "Steam networking is not available in this game (see the [NET] steam: line)";
            return false;
        }
        return true;
    }

    int Options(SnConfigValue* o, bool p2p) const
    {
        return MakeOptions(o, timeoutConnectedMs_, ipTest_ && allowNoAuth_, p2p);
    }

    /* A socket pair takes no options: its two connections get the send buffer and rate through the utils interface. The number set. */
    int ConfigureConn(unsigned int c)
    {
        void* u = SteamProbeUtils();
        if (u == 0) return 0;
        const int values[3] = { kCfgSendBufferSize, kCfgSendRateMin, kCfgSendRateMax };
        const int amounts[3] = { kSendBufferBytes, kSendRateBytes, kSendRateBytes };
        int set = 0;
        for (int i = 0; i < 3; ++i)
        {
            int ok = 0;
            if (!SehSetConfig(u, values[i], kCfgScopeConnection, (intptr_t)c, kCfgTypeInt32, &amounts[i], &ok))
            {
                Fault("SetConfigValue");
                return set;
            }
            set += ok;
        }
        return set;
    }

    void Adopt(unsigned int c, bool asHost)
    {
        conn_ = c;
        hosting_ = asHost;
        peerId_ = asHost ? nextPeerId_++ : 0;
        p2p_ = false;   /* a socket pair; the listener marks its P2P links after */
        remoteKind_ = 0;
        remoteId64_ = 0;
        state_ = LINK_CONNECTING;
        dialMs_ = ::GetTickCount();
        endReason_ = 0;
        endText_[0] = 0;
        maxState_ = 0;
    }

    /* A fault in a Steam call: counted on the probe, logged, and this transport makes no further Steam call. */
    void Fault(const char* where)
    {
        SteamNoteFault();
        Stop(std::string("fault in ") + where);
        ErrorLog("[net] Steam link: FAULT in " + std::string(where) + " - this link stops and makes no further Steam call; every Steam"
                 " link in this game stops with it (the probe's fault count is no longer 0)");
    }
    void Stop(const std::string& why)
    {
        const bool wasUp = (state_ == LINK_UP);
        if (!broken_ && why.compare(0, 6, "fault ") != 0)
        {
            if (SteamProbeShutDown()) DebugLog("[net] Steam link: stops - " + why + " (no further Steam call from this link)");   /* the exit, not a fault */
            else ErrorLog("[net] Steam link: stops - " + why + " (no further Steam call from this link)");
        }
        broken_ = true;
        stopWhy_ = why;
        conn_ = 0;
        listen_ = 0;
        state_ = LINK_FAILED;
        if (wasUp) ++connectGen_;
        queue_.clear();
        queueBytes_ = 0;
        steamPending_ = 0;
        rejoin_.Reset();
    }

    /* The graceful close (linger: Steam still sends what it holds) or the abrupt one. A graceful close first raises this connection's
       send buffer to hold this link's own queue too (40 MB at most) and hands Steam all of it; what is still left over is dropped and
       logged. The abrupt close drops the queue. */
    void Close(bool linger)
    {
        if (!broken_ && sockets_ != 0 && (conn_ != 0 || listen_ != 0) && SteamProbeSockets() == 0)
        {
            Stop(NoSteamWhy("another Steam call in this game faulted"));   /* no Steam call after any fault */
            return;
        }
        if (!broken_ && sockets_ != 0)
        {
            bool lingerRaised = false;
            if (conn_ != 0 && state_ == LINK_UP && linger)
            {
                if (!queue_.empty()) lingerRaised = RaiseLingerBuffer();
                if (!broken_) PumpQueue();
            }
            if (!broken_ && conn_ != 0 && !queue_.empty())
                DebugLog("[net] Steam link: closing with " + N((long long)queue_.size()) + " message(s), " + N(queueBytes_)
                         + " bytes not yet taken by Steam - dropped" + (linger ? (lingerRaised ? " (Steam's send buffer, raised for the close, is full)" : " (Steam's send buffer could not be raised for the close)") : ""));
            if (!broken_ && conn_ != 0 && !SehCloseConn(sockets_, conn_, 0, linger ? "closed" : "aborted", linger)) { Fault("CloseConnection"); return; }
            if (!broken_ && listen_ != 0 && !SehCloseListen(sockets_, listen_)) { Fault("CloseListenSocket"); return; }
        }
        if (broken_) return;
        const bool wasUp = (state_ == LINK_UP);   /* read after PumpQueue: a send Steam refused there already ended the link and counted its edge */
        conn_ = 0;
        listen_ = 0;
        if (wasUp) ++connectGen_;
        if (state_ != LINK_DOWN) state_ = LINK_DOWN;
        hosting_ = false;
        queue_.clear();
        queueBytes_ = 0;
        steamPending_ = 0;
        rejoin_.Reset();
        if (g_csReady)
        {
            ::EnterCriticalSection(&g_cs);
            nEvents_ = 0;
            ::LeaveCriticalSection(&g_cs);
        }
    }

    /* Valve's SendBufferSize at connection scope, for a graceful close: what Steam holds (at most the normal buffer) plus this link's queue. */
    bool RaiseLingerBuffer()
    {
        void* u = SteamProbeUtils();
        if (u == 0) return false;
        const int bytes = (int)relaysplit::LingerBufferBytes(queueBytes_, kSendBufferBytes, kLingerBufferCapBytes);
        int ok = 0;
        if (!SehSetConfig(u, kCfgSendBufferSize, kCfgScopeConnection, (intptr_t)conn_, kCfgTypeInt32, &bytes, &ok)) { Fault("SetConfigValue"); return false; }
        DebugLog("[net] Steam link: closing gracefully with " + N(queueBytes_) + " bytes in this link's own queue - Steam's send buffer for it "
                 + (ok ? "raised to " + N(bytes) + " bytes" : "NOT raised (Steam refused " + N(bytes) + " bytes)"));
        return ok != 0;
    }

    void TakeEvents()
    {
        SnEvent ev[kEventMax];
        int n = 0;
        ::EnterCriticalSection(&g_cs);
        n = nEvents_;
        if (n > 0) std::memcpy(ev, events_, sizeof(SnEvent) * (size_t)n);
        nEvents_ = 0;
        ::LeaveCriticalSection(&g_cs);
        for (int i = 0; i < n && !broken_; ++i)
        {
            const SnEvent& e = ev[i];
            if (listen_ == 0 || e.listenSocket != listen_ || e.conn == 0 || e.conn == conn_ || e.conn == endedConn_)
                continue;   /* this link's own connection (CheckConn reads its state), or the one it already freed */
            if (e.state == kStateConnecting && e.oldState == kStateNone)
            {
                if (conn_ == 0) { Accept(e.conn); continue; }
                if (!SehCloseConn(sockets_, e.conn, kEndBusy, "busy: this listener holds one link", false)) { Fault("CloseConnection"); return; }
                DebugLog("[net] Steam link: a second incoming connection was closed as busy (this listener holds one link)");
            }
            else if (e.state == kStateClosedByPeer || e.state == kStateProblemDetectedLocally)
            {
                /* an incoming connection that ended before it was taken: Steam frees it only through CloseConnection */
                if (!SehCloseConn(sockets_, e.conn, 0, 0, false)) { Fault("CloseConnection"); return; }
            }
        }
    }

    void Accept(unsigned int h)
    {
        int res = 0;
        if (!SehAccept(sockets_, h, &res)) { Fault("AcceptConnection"); return; }
        if (res != kResultOk)
        {
            DebugLog("[net] Steam link: AcceptConnection refused an incoming connection (EResult " + N(res) + ") - it is dropped");
            if (!SehCloseConn(sockets_, h, 0, 0, false)) Fault("CloseConnection");
            return;
        }
        conn_ = h;
        p2p_ = !ipTest_;
        peerId_ = nextPeerId_++;
        dialMs_ = ::GetTickCount();
        endReason_ = 0;
        endText_[0] = 0;
        maxState_ = 0;
        DebugLog("[net] Steam link: incoming connection accepted (peer id " + N(peerId_) + ")");
    }

    void CheckConn(std::vector<Message>* out)
    {
        SnConnInfo info;
        std::memset(&info, 0, sizeof(info));
        int ok = 0;
        if (!SehInfo(sockets_, conn_, &info, &ok)) { Fault("GetConnectionInfo"); return; }
        const int st = ok ? info.state : kStateNone;
        if (st >= kStateConnecting && st <= kStateConnected && st > maxState_) maxState_ = st;   /* the furthest this connection got */
        if (st == kStateConnected)
        {
            if (state_ != LINK_UP)
            {
                /* The relay-only rule is judged before the link counts as up: a refused P2P link ends while still connecting (no UP
                   line, no UP count, wasUp=0 in a listener's ended event). */
                relayed_ = relaysplit::LinkRelayed(info.flags, info.popRelay) ? 1 : 0;
                if (!steamlisten::RouteAllowed(p2p_, relayed_ != 0))
                {
                    ErrorLog("[net] Steam link route: NOT relayed - a Steam P2P link must go through Valve's relay; closing before it counts"
                             " as up (end reason " + N(steamlisten::kEndNotRelayed) + ")");
                    Ended(steamlisten::kEndNotRelayed, "not relayed", "a route that is not Valve's relay", true);
                    return;
                }
                const DWORD now = ::GetTickCount();
                state_ = LINK_UP;
                ++connectGen_;
                upMs_ = (unsigned int)(DWORD)(now - dialMs_);
                lastRecvMs_ = now;
                rate_ = kSendRateBytes;   /* each connection starts at the rate its socket was made with; the first step is a second on */
                rateNextMs_ = now + kRateStepMs;
                rateFrozen_ = false;
                NoteRate(rate_);
                remoteKind_ = info.identityRemote.type;
                remoteId64_ = 0;
                if (info.identityRemote.type == kIdentitySteamId && info.identityRemote.cbSize == 8 && steamlisten::IdAuthenticated(info.flags))
                    std::memcpy(&remoteId64_, info.identityRemote.body, 8);   /* only an id Steam authenticated */
                ++g_stats.up;
                g_stats.upMs = upMs_;
                DebugLog("[net] Steam link UP in " + N(upMs_) + " ms (peer id " + N(peerId_) + ", relayed=" + N(relayed_) + ", send rate "
                         + N(kSendRateBytes) + " bytes/s min and max to start, stepped each second from Steam's delivery quality)");
                DebugLog("[net] Steam link facts at UP: " + Describe(info));
                DebugLog(p2p_ ? std::string("[net] Steam link route: relay-only (peer id ") + N(peerId_) + ") - direct paths are off for this link"
                                " (ICE disabled: no address shared with the peer), relayed=1 at UP"
                              : std::string("[net] Steam link route: plain UDP on 127.0.0.1 or a socket pair in this game (peer id ") + N(peerId_)
                                + ", relay test) - not a link between players; the relay-only rule does not apply");
            }
            return;
        }
        if (st == kStateConnecting || st == kStateFindingRoute) return;
        /* Valve (steamnetworkingtypes.h): after ClosedByPeer or ProblemDetectedLocally the messages already received can still be read.
           All of them are read now, before the handle is freed: the peer's last messages (its goodbye) arrive, so a graceful leave does
           not look like a crash. */
        if ((st == kStateClosedByPeer || st == kStateProblemDetectedLocally) && state_ == LINK_UP && out != 0)
        {
            const unsigned long long before = recv_;
            Receive(out, true);
            if (broken_ || conn_ == 0) return;   /* a fault, or a malformed message already closed the link */
            DebugLog("[net] Steam link: " + N((long long)(recv_ - before)) + " message(s) Steam still held were read before closing");
        }
        char text[128];
        CopyText(text, sizeof(text), ok ? relaysplit::HideIdNumbers(FlatText(info.endDebug, sizeof(info.endDebug))).c_str()
                                        : "the connection handle is no longer valid");
        Ended(ok ? info.endReason : -1, text,
              !ok ? "its handle is gone" : st == kStateClosedByPeer ? "closed by the peer"
                  : st == kStateProblemDetectedLocally ? "a problem was detected here" : "it left the connected states", true);
    }

    /* The link's connection is over: freed with CloseConnection (Valve: needed even after the peer closed), counted and logged. */
    void Ended(int reason, const char* text, const char* how, bool closeIt)
    {
        const bool wasUp = (state_ == LINK_UP);
        std::string facts;
        if (closeIt && !EndFacts(&facts)) return;   /* a fault: Stop ended the link */
        if (closeIt && !SehCloseConn(sockets_, conn_, reason >= 1000 && reason < 2000 ? reason : 0, text, false)) { Fault("CloseConnection"); return; }
        endedConn_ = conn_;
        conn_ = 0;
        if (wasUp) ++connectGen_;
        endReason_ = reason;
        CopyText(endText_, sizeof(endText_), text);
        g_stats.endReason = reason;
        state_ = (hosting_ && listen_ != 0) ? LINK_LISTENING : (wasUp ? LINK_DOWN : LINK_FAILED);
        queue_.clear();
        queueBytes_ = 0;
        steamPending_ = 0;
        rejoin_.Reset();
        DebugLog("[net] Steam link ended (" + std::string(how) + "): end reason " + N(reason) + " '" + endText_ + "'"
                 + (wasUp ? "" : " - it never came up"));
        if (closeIt) DebugLog("[net] Steam link facts at end: " + facts);
    }

    /* Re-read at the end: the route facts and the furthest state reached; for a link that never connected also Steam's detailed status
       and the relay network's status - the first questions when a player's link over the real relay fails. false = a fault. */
    bool EndFacts(std::string* line)
    {
        SnConnInfo info;
        std::memset(&info, 0, sizeof(info));
        int ok = 0;
        if (!SehInfo(sockets_, conn_, &info, &ok)) { Fault("GetConnectionInfo"); return false; }
        *line = ok ? Describe(info) : "connection info unavailable (the handle is gone), furthest state " + std::string(relaysplit::LinkStateWord(maxState_));
        if (maxState_ >= kStateConnected) return true;
        char buf[2048];
        buf[0] = 0;
        int res = -1;
        if (!SehDetail(sockets_, conn_, buf, (int)sizeof(buf), &res)) { Fault("GetDetailedConnectionStatus"); return false; }
        buf[sizeof(buf) - 1] = 0;
        *line += " | Steam's detailed status (" + N(res) + "): " + (res >= 0 ? relaysplit::HideIdNumbers(FlatText(buf, sizeof(buf))) : std::string("none - bad handle"));
        *line += " | " + SteamRelayStatusText();
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return false; }   /* the relay-status read faulted */
        return true;
    }

    /* What Steam says about the connection, in one line: route flags, relayed, both data centres, Steam's description of it (any Steam id
       in it hidden), the remote identity's type (never the id) and the furthest state reached. */
    std::string Describe(const SnConnInfo& info) const
    {
        char popRemote[16], popRelay[16], desc[128];
        relaysplit::PopText(info.popRemote, popRemote);
        relaysplit::PopText(info.popRelay, popRelay);
        CopyText(desc, sizeof(desc), info.description);
        return "flags=" + N(info.flags) + " relayed=" + N(relaysplit::LinkRelayed(info.flags, info.popRelay) ? 1 : 0) + " popRemote="
               + popRemote + " popRelay=" + popRelay + " description='" + relaysplit::HideIdNumbers(desc) + "' remote=" + IdentityText(info.identityRemote)
               + " furthest state=" + relaysplit::LinkStateWord(maxState_);
    }

    /* A received sequence the split rule refuses: the stream has a hole, so the link closes with our reason. */
    void CloseBad(int reason, const char* why)
    {
        ErrorLog("[net] Steam link: closing - received " + std::string(why) + " (end reason " + N(reason) + ")");
        Ended(reason, why, "a malformed message", true);
    }

    /* drainAll: read until Steam returns none (a link whose peer closed), past the per-Poll cap; otherwise at most 1,024 per Poll. */
    void Receive(std::vector<Message>* out, bool drainAll)
    {
        for (int batch = 0; drainAll || batch < kRecvBatchesPerPoll; ++batch)
        {
            SnMessage* msgs[kRecvBatch];
            int n = 0;
            if (!SehReceive(sockets_, conn_, msgs, kRecvBatch, &n)) { Fault("ReceiveMessagesOnConnection"); return; }
            if (n <= 0) return;   /* -1 = the handle is gone: CheckConn sees it at the next Poll */
            if (n > kRecvBatch) n = kRecvBatch;
            bool go = true;
            for (int i = 0; i < n; ++i)
            {
                if (go) go = TakeOne(msgs[i], out);
                if (broken_) return;   /* a fault: no further Steam call; the rest stay with Steam */
                if (!SehMsgRelease(msgs[i])) { Fault("SteamNetworkingMessage_t::m_pfnRelease"); return; }
            }
            if (!go || conn_ == 0 || state_ != LINK_UP || (!drainAll && n < kRecvBatch)) return;
        }
    }

    /* One received message: rejoined, checked like ENet's Decode, handed up. false = stop reading (a fault or the link closed). */
    bool TakeOne(SnMessage* m, std::vector<Message>* out)
    {
        const char* data = 0;
        int size = 0, flags = 0;
        if (!SehMsgFields(m, &data, &size, &flags)) { Fault("reading a received message"); return false; }
        if (size < 0 || (size > 0 && data == 0)) { CloseBad(kEndMalformed, "a message with no data"); return false; }
        std::vector<char> raw((size_t)size);
        if (size > 0 && !SehMsgCopy(data, &raw[0], size)) { Fault("copying a received message"); return false; }
        lastRecvMs_ = ::GetTickCount();
        std::vector<char> frame;
        size_t pieces = 0;
        const char* why = "";
        const int t = relaysplit::Take(&rejoin_, raw.empty() ? 0 : &raw[0], raw.size(), (flags & kSendReliableBit) != 0, &frame, &pieces, &why);
        if (t == relaysplit::kTakePiece) return true;
        if (t != relaysplit::kTakeFrame)
        {
            CloseBad(t == relaysplit::kTakeOver ? kEndOverCap : kEndMalformed, why);
            return false;
        }
        if (pieces > 1) ++g_stats.rejoined;
        unsigned int declared = 0;
        if (frame.size() >= 5) std::memcpy(&declared, &frame[1], 4);
        if (frame.size() < 5 || (size_t)declared != frame.size() - 5)
        {
            /* like ENet's Decode: a frame whose header disagrees with its size is dropped, the link stays */
            ++g_stats.recvBad;
            if (LogThis(g_stats.recvBad))
                ErrorLog("[net] Steam link: dropped a frame of " + N((long long)frame.size()) + " bytes whose 5-byte header does not match it"
                         " (counted steamLinkEv[recvBad]; the first 5 and every 100th are logged)");
            return true;
        }
        Message msg;
        msg.type = (MsgType)(unsigned char)frame[0];
        msg.peer = peerId_;
        msg.lane = (flags & kSendReliableBit) != 0 ? CH_RELIABLE : CH_UNRELIABLE;   /* a rejoined frame came on the reliable lane */
        if (declared != 0) msg.payload.assign(frame.begin() + 5, frame.end());
        out->push_back(msg);
        ++recv_; ++g_stats.recv;
        bytesIn_ += frame.size(); g_stats.bytesIn += (long long)frame.size();
        nettally::CountIn(tallyLink_, (unsigned int)(unsigned char)frame[0], frame.size());
        if (tallyLink_ != nettally::kLinkSession && (unsigned int)(unsigned char)frame[0] == nettally::kTypeLive)   /* 53 is a LIVE envelope only toward the world server */
            nettally::CountLiveIn(tallyLink_, tallyLink_ == nettally::kLinkWorld, &frame[0] + 5, frame.size() - 5);
        return true;
    }

    /* Reliable messages to Steam, in order, until Steam's send buffer is full (they wait here for the next Poll or Flush). */
    void PumpQueue()
    {
        while (!queue_.empty() && !broken_ && conn_ != 0)
        {
            const std::vector<char>& m = queue_.front();
            int res = 0;
            if (!SehSend(sockets_, conn_, &m[0], (unsigned int)m.size(), kSendReliableNoNagle, &res)) { Fault("SendMessageToConnection"); return; }
            if (res == kResultOk)
            {
                queueBytes_ -= (long long)m.size();
                queue_.pop_front();
                continue;
            }
            if (res == kResultLimitExceeded)
            {
                ++g_stats.limitHits;
                return;
            }
            char why[96];
            _snprintf(why, sizeof(why) - 1, "SendMessageToConnection returned EResult %d", res);
            why[sizeof(why) - 1] = 0;
            ErrorLog("[net] Steam link: closing - " + std::string(why));
            Ended(kEndSendFailed, why, "a send Steam refused", true);
            return;
        }
    }

    void RefreshStatus()
    {
        SnRealTimeStatus st;
        std::memset(&st, 0, sizeof(st));
        int res = 0;
        if (!SehStatus(sockets_, conn_, &st, &res)) { Fault("GetConnectionRealTimeStatus"); return; }
        if (res != kResultOk) return;
        ping_ = st.ping > 0 ? (unsigned int)st.ping : 0u;
        /* Steam's own incoming packet rate (keepalives included, averaged over Steam's recent window): any packet from the peer counts as
           heard from, as ENet counts any packet, so the host-silence clock does not run while the peer is alive but quiet. */
        if (st.inPacketsPerSec > 0.0f) lastRecvMs_ = ::GetTickCount();
        steamPending_ = (long long)st.pendingUnreliable + (long long)st.pendingReliable + (long long)st.sentUnackedReliable;
        NoteQueued();
        StepRate(st);
        RateLog(st);
    }

    /* TEST ONLY (netratelog=1; absent = off): once a second per link, what Steam reports for it beside this link's own rate and queue,
       so a bench sees real Steam numbers. Reads only. Integers only: q = quality x100 (-100 = Steam cannot say yet). */
    void RateLog(const SnRealTimeStatus& st)
    {
        if (ConfigNetRateLog() == 0) return;
        const DWORD now = ::GetTickCount();
        if (rateLogMs_ != 0 && now - rateLogMs_ < 1000u) return;
        rateLogMs_ = (now == 0) ? 1 : now;
        char b[384];
        _snprintf(b, sizeof(b) - 1, "[net] RATE link=%s peer=%u rate=%d steamRate=%d outBps=%d inBps=%d ping=%d qL=%d qR=%d pendRel=%d pendUnrel=%d"
                  " unacked=%d queueUs=%lld ownQ=%lld", nettally::LinkName(tallyLink_), peerId_, rate_, st.sendRateBytesPerSecond,
                  (int)st.outBytesPerSec, (int)st.inBytesPerSec, st.ping, (int)(st.qualityLocal * 100.0f), (int)(st.qualityRemote * 100.0f),
                  st.pendingReliable, st.pendingUnreliable, st.sentUnackedReliable, st.queueTimeUsec, queueBytes_);
        b[sizeof(b) - 1] = 0;
        DebugLog(b);
    }

    /* Owner decision 594: once a second while the link is UP the send rate follows Steam's live delivery quality and the data waiting
       (relaysplit::NextSendRate); a change is set on this connection for SendRateMin and SendRateMax alike. If Steam refuses it, the
       link keeps the last rate Steam accepted and stops adjusting (logged once). */
    void StepRate(const SnRealTimeStatus& st)
    {
        if (rateFrozen_ || broken_ || conn_ == 0 || state_ != LINK_UP) return;
        const DWORD now = ::GetTickCount();
        if ((int)(DWORD)(now - rateNextMs_) < 0) return;
        rateNextMs_ = now + kRateStepMs;
        long long waiting = (long long)st.pendingReliable + (long long)st.pendingUnreliable + queueBytes_;
        if (waiting < 0) waiting = 0;
        if (waiting > 4294967295LL) waiting = 4294967295LL;
        const int next = relaysplit::NextSendRate(rate_, st.qualityLocal, st.qualityRemote, (unsigned int)waiting);
        if (next == rate_) return;
        /* Raising sets the maximum first, lowering the minimum first: the minimum never stands above the maximum between the calls. */
        const bool raise = next > rate_;
        const int first = raise ? kCfgSendRateMax : kCfgSendRateMin;
        const int second = raise ? kCfgSendRateMin : kCfgSendRateMax;
        void* u = SteamProbeUtils();
        int ok1 = 0, ok2 = 0;
        if (u != 0 && !SehSetConfig(u, first, kCfgScopeConnection, (intptr_t)conn_, kCfgTypeInt32, &next, &ok1)) { Fault("SetConfigValue"); return; }
        if (u != 0 && ok1 && !SehSetConfig(u, second, kCfgScopeConnection, (intptr_t)conn_, kCfgTypeInt32, &next, &ok2)) { Fault("SetConfigValue"); return; }
        if (!ok1 || !ok2)
        {
            int back = 0;   /* the first value went in and the second did not: put the first back, so min and max stay the same */
            if (ok1 && !SehSetConfig(u, first, kCfgScopeConnection, (intptr_t)conn_, kCfgTypeInt32, &rate_, &back)) { Fault("SetConfigValue"); return; }
            rateFrozen_ = true;
            ErrorLog("[net] Steam link: Steam refused send rate " + N(next) + " bytes/s" + (u == 0 ? " (no utils interface)" : "")
                     + " - this link keeps " + N(rate_) + " bytes/s and stops adjusting it");
            return;
        }
        ++g_stats.rateChanges;
        ++rateLogged_;
        if (rateLogged_ <= kRateLogMax)
            DebugLog("[net] Steam link: send rate " + N(rate_) + " -> " + N(next) + " bytes/s (quality local " + QualityText(st.qualityLocal)
                     + " remote " + QualityText(st.qualityRemote) + ", waiting " + N(waiting) + " bytes; Steam's own rate now "
                     + N(st.sendRateBytesPerSecond) + ")"
                     + (rateLogged_ == kRateLogMax ? " - further changes on this link are counted only (steamLink rateChanges)" : ""));
        rate_ = next;
        NoteRate(rate_);
    }

    void CountSent(size_t frameBytes, size_t pieces)
    {
        g_stats.pieces += (long long)pieces;
        ++sent_; ++g_stats.sent;
        bytesOut_ += frameBytes; g_stats.bytesOut += (long long)frameBytes;
    }

    void NoteQueued()
    {
        const long long q = queueBytes_ + steamPending_;
        if (q > g_stats.queuedMax) g_stats.queuedMax = q;
    }

    void*        sockets_;
    unsigned int listen_;
    unsigned int conn_;
    LinkState    state_;
    bool         hosting_;
    bool         broken_;        /* a fault (or a stop) ended this transport: no further Steam call */
    std::string  stopWhy_;
    bool         ipTest_;        /* the relay test's IP mode (SteamTransportSetIpTest) */
    bool         allowNoAuth_;
    unsigned long long sent_, recv_, bytesOut_, bytesIn_;
    unsigned int connectGen_;
    unsigned int nextPeerId_;
    unsigned int peerId_;
    unsigned int ping_;
    long long    steamPending_;  /* bytes Steam holds for this link, as of the last Poll */
    long long    queueBytes_;    /* bytes in queue_ */
    std::deque<std::vector<char> > queue_;   /* reliable Steam messages Steam has not taken yet, in order */
    relaysplit::Rejoin rejoin_;
    DWORD        dialMs_;        /* GetTickCount at the dial, the accept or the pair */
    unsigned int upMs_;
    DWORD        lastRecvMs_;
    int          relayed_;
    int          endReason_;
    int          maxState_;      /* the furthest ESteamNetworkingConnectionState this connection reached (1-3), for the logs */
    unsigned int endedConn_;     /* the connection this link freed last: its late events are not a new arrival */
    char         endText_[128];
    unsigned int timeoutConnectedMs_;
    SnEvent      events_[kEventMax];   /* written by the callback under g_cs */
    int          nEvents_;
    bool         registered_;
    int          rate_;          /* the send rate (bytes/s) Steam last accepted for this link's connection, min and max alike */
    DWORD        rateNextMs_;    /* GetTickCount of the next send-rate step */
    bool         rateFrozen_;    /* Steam refused a rate: this connection keeps rate_ and is not adjusted again */
    long long    rateLogged_;    /* send-rate changes of this link so far (the first kRateLogMax are logged) */
    bool         p2p_;           /* this link's connection is Steam P2P (relay only), not the relay test's plain UDP or socket pair */
    int          remoteKind_;    /* the remote identity's type at UP (kIdentitySteamId, kIdentityIpAddress, 0 none) */
    unsigned long long remoteId64_;   /* the remote's Steam id at UP when it is one: for the host's allow / block rules, never logged */
    DWORD        rateLogMs_;     /* TEST ONLY netratelog: GetTickCount of this link's last [net] RATE line; 0 = none yet */
    int          tallyLink_;     /* nettally::kLink*: the [NET] TRAFFIC REPORT line this link is counted on (SetStoreLink, SetTrafficLink) */
};

/* ---- the Steam listener: many links on one listen socket ---- */

class SteamListenerImpl : public SteamListener
{
public:
    SteamListenerImpl()
        : sockets_(0), listen_(0), ipTest_(false), broken_(false), cap_(steamlisten::kLinkCap), nEvents_(0), registered_(false),
          refusedLogged_(0), linkTimeoutMs_((unsigned int)kTimeoutConnectedMs), hold_(false)
    {
        steamlisten::Reset(&table_);
        for (int i = 0; i < steamlisten::kLinkCap; ++i) links_[i] = 0;
        EnsureLock();
        ::EnterCriticalSection(&g_cs);
        for (int i = 0; i < kListenerMax; ++i)
            if (g_listeners[i] == 0) { g_listeners[i] = this; registered_ = true; break; }
        ::LeaveCriticalSection(&g_cs);
        if (!registered_)
        {
            broken_ = true;
            stopWhy_ = "more than " + N(kListenerMax) + " Steam listeners alive in this game";
        }
        SteamPumpForSession();   /* Steam is in use: the probe calls SteamAPI_RunCallbacks every frame from now on */
    }

    ~SteamListenerImpl()
    {
        Close();
        ::EnterCriticalSection(&g_cs);
        for (int i = 0; i < kListenerMax; ++i)
            if (g_listeners[i] == this) g_listeners[i] = 0;
        ::LeaveCriticalSection(&g_cs);
    }

    bool Open(unsigned short ipTestPort, std::string* errOut)
    {
        if (broken_) { if (errOut) *errOut = "this Steam listener stopped: " + stopWhy_; return false; }
        Close();
        if (broken_) { if (errOut) *errOut = stopWhy_; return false; }
        sockets_ = SteamProbeSockets();
        if (sockets_ == 0)
        {
            if (errOut) *errOut = "Steam networking is not available in this game (see the [NET] steam: line)";
            return false;
        }
        ipTest_ = (ipTestPort != 0);
        SnConfigValue o[8];
        const int n = MakeOptions(o, linkTimeoutMs_, ipTest_, !ipTest_);   /* a connection the listen socket accepts takes these */
        unsigned int h = 0;
        int called = 0;
        if (ipTest_)
        {
            const SnIpAddr a = Loopback(ipTestPort);
            called = SehListenIP(sockets_, &a, n, o, &h);
        }
        else
            called = SehListenP2P(sockets_, 0, n, o, &h);
        if (!called)
        {
            Fault(ipTest_ ? "CreateListenSocketIP" : "CreateListenSocketP2P");
            if (errOut) *errOut = stopWhy_;
            return false;
        }
        if (h == 0)
        {
            if (errOut) *errOut = "Steam refused to open the listen socket";
            ErrorLog(ipTest_ ? "[net] Steam listener: Steam refused to open the listen socket on 127.0.0.1:" + N(ipTestPort)
                             : std::string("[net] Steam listener: Steam refused to open the listen socket on P2P virtual port 0 (relay only)"));
            return false;
        }
        if (!ipTest_)
        {
            std::string read;
            const int r = IceOffReadBack(SteamProbeUtils(), h, &read);
            if (r < 0)
            {
                Fault("GetConfigValue");
                if (errOut) *errOut = stopWhy_;
                return false;
            }
            if (r == 0)
            {
                ErrorLog("[net] Steam listener: the relay-only option did NOT read back as 0 on the new P2P listen socket (" + read
                         + ") - closing it: no P2P listener runs that could share an address");
                if (SteamProbeSockets() != 0 && !SehCloseListen(sockets_, h))   /* no Steam call after any fault */
                {
                    Fault("CloseListenSocket");
                    if (errOut) *errOut = stopWhy_;
                    return false;
                }
                if (errOut) *errOut = "Steam did not keep the relay-only option on the listen socket (" + read + ")";
                return false;
            }
            DebugLog("[net] Steam listener: relay-only option read back on the P2P listen socket - " + read);
        }
        listen_ = h;
        ++g_listenStats.opened;
        DebugLog(ipTest_ ? "[net] Steam listener: listening on 127.0.0.1:" + N(ipTestPort) + " over ordinary UDP (relay test; allow-without-auth on),"
                           " up to " + N(cap_) + " links"
                         : "[net] Steam listener: listening on Steam P2P virtual port 0, relay only (ICE off: no address shared), up to "
                           + N(cap_) + " links");
        return true;
    }

    void Close()
    {
        int closed = 0;
        for (int s = 0; s < steamlisten::kLinkCap; ++s)
        {
            if (links_[s] != 0) { DropSlot(s); ++closed; }   /* each link's own close: graceful */
            else if (table_.id[s] != 0) { CloseHeldAt(s, steamlisten::kEndListenerNoRoom); ++closed; }   /* a held arrival: refused */
        }
        if (listen_ != 0 && !broken_ && sockets_ != 0)
        {
            if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return; }   /* no Steam call after any fault */
            if (!SehCloseListen(sockets_, listen_)) { Fault("CloseListenSocket"); return; }
            DebugLog("[net] Steam listener: closed (" + N(closed) + " link(s) closed with it)");
        }
        listen_ = 0;
        ClearEvents();
    }

    bool IsOpen() const { return !broken_ && listen_ != 0; }
    void SetCap(int cap) { cap_ = steamlisten::CapFrom(cap); }
    void SetLinkTimeoutMs(unsigned int ms) { linkTimeoutMs_ = ms != 0 ? ms : (unsigned int)kTimeoutConnectedMs; }
    void SetHoldArrivals(bool on) { hold_ = on; }

    bool AcceptHeld(unsigned int link)
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (broken_ || listen_ == 0 || s < 0 || links_[s] != 0) return false;
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return false; }   /* no Steam call after any fault */
        const unsigned int h = table_.handle[s];
        SteamTransport* t = new SteamTransport();
        t->SetTrafficLink(nettally::kLinkBridge);   /* a listener's links are the bridge's */
        if (t->Stopped())   /* the link registry is full */
        {
            delete t;
            ++g_listenStats.refusedOther;
            ErrorLog("[net] Steam listener: held link " + N(link) + " could not be taken (this game's Steam link registry is full) - closed with"
                     " end reason " + N(steamlisten::kEndListenerNoRoom));
            CloseHeldAt(s, steamlisten::kEndListenerNoRoom);
            return false;
        }
        int res = 0;
        if (!SehAccept(sockets_, h, &res)) { delete t; Fault("AcceptConnection"); return false; }
        std::string err;
        if (res != kResultOk || !t->TakeAccepted(h, !ipTest_, &err))
        {
            delete t;
            ++g_listenStats.refusedOther;
            ErrorLog("[net] Steam listener: held link " + N(link) + " could not be accepted (" + (res != kResultOk ? "AcceptConnection EResult " + N(res) : err)
                     + ") - closed with end reason " + N(steamlisten::kEndListenerNoRoom));
            CloseHeldAt(s, steamlisten::kEndListenerNoRoom);
            return false;
        }
        links_[s] = t;
        ++g_listenStats.accepted;
        DebugLog("[net] Steam listener: held link " + N(link) + " accepted - holds " + N(table_.count) + " of " + N(cap_));
        return true;
    }

    bool RefuseHeld(unsigned int link, int reason)
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (s < 0 || links_[s] != 0) return false;
        ++g_listenStats.refusedOther;
        CloseHeldAt(s, reason);
        return true;
    }
    int  Cap() const { return cap_; }
    int  LinkCount() const { return table_.count; }

    void Poll(std::vector<SteamListenEvent>* events, std::vector<Message>* in)
    {
        if (broken_ || listen_ == 0) return;
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return; }
        if (g_cbFaults != 0) { Fault("reading a connection event"); return; }
        if (!SehIfaceRunCallbacks(sockets_)) { Fault("ISteamNetworkingSockets::RunCallbacks"); return; }
        /* The held links first: one that ended this frame frees its place before this frame's arrivals are judged. */
        for (int s = 0; s < steamlisten::kLinkCap && !broken_; ++s)
        {
            SteamTransport* t = links_[s];
            if (t == 0) continue;
            std::vector<Message> got;
            t->Poll(&got);
            const unsigned int id = table_.id[s];
            for (size_t k = 0; in != 0 && k < got.size(); ++k)
            {
                in->push_back(Message());
                in->back().type = got[k].type;
                in->back().peer = id;
                in->back().lane = got[k].lane;
                in->back().payload.swap(got[k].payload);
            }
            if (t->Stopped() || SteamProbeSockets() == 0) { Stop(NoSteamWhy("a Steam call in this game faulted")); return; }
            const LinkState st = t->State();
            if (st == LINK_DOWN || st == LINK_FAILED) EndSlot(s, events);
        }
        if (broken_) return;
        TakeArrivals(events);
    }

    bool Send(unsigned int link, MsgType type, const char* data, size_t len, Channel ch)
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (broken_ || s < 0 || links_[s] == 0) return false;
        return links_[s]->Send(0, type, data, len, ch);
    }

    void Flush()
    {
        for (int s = 0; s < steamlisten::kLinkCap && !broken_; ++s)
            if (links_[s] != 0) links_[s]->Flush();
    }

    bool CloseLink(unsigned int link, bool linger)
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (s < 0) return false;
        if (links_[s] == 0) return RefuseHeld(link, steamlisten::kEndListenerNoRoom);   /* a held arrival: refused */
        if (!linger) links_[s]->Abort();
        DropSlot(s);
        ++g_listenStats.ended;
        DebugLog("[net] Steam listener: link " + N(link) + " closed here (" + (linger ? "graceful" : "abrupt") + ") - holds "
                 + N(table_.count) + " of " + N(cap_));
        return true;
    }

    const ITransport* Link(unsigned int link) const
    {
        const int s = steamlisten::SlotOfId(table_, link);
        return s < 0 ? 0 : links_[s];
    }

    bool LinkFacts(unsigned int link, SteamLinkFacts* out) const
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (s < 0 || links_[s] == 0 || out == 0) return false;
        links_[s]->Facts(out);
        return true;
    }

    bool LinkSteamId(unsigned int link, unsigned long long* id64) const
    {
        const int s = steamlisten::SlotOfId(table_, link);
        if (s < 0 || links_[s] == 0 || ipTest_) return false;   /* plain UDP in the relay test: an id Steam did not check */
        return links_[s]->RemoteSteamId(id64);
    }

    bool Stopped(std::string* why) const
    {
        if (broken_ && why != 0) *why = stopWhy_;
        return broken_;
    }

    /* The callback's routing test and push; both run under g_cs. */
    bool OwnsListen(const SnEvent& e) const { return listen_ != 0 && e.listenSocket == listen_; }
    bool PushEvent(const SnEvent& e)
    {
        if (nEvents_ >= kListenEventMax) return false;
        events_[nEvents_++] = e;
        return true;
    }

private:
    void DropSlot(int s)
    {
        SteamTransport* t = links_[s];
        links_[s] = 0;
        steamlisten::RemoveSlot(&table_, s);
        delete t;   /* the transport's own close: graceful, Steam still sends what it holds */
    }

    /* A held arrival closed here with `reason`; its place is free. No Steam call after a fault (Steam closes it with the socket). */
    void CloseHeldAt(int s, int reason)
    {
        const unsigned int h = table_.handle[s];
        steamlisten::RemoveSlot(&table_, s);
        if (broken_ || sockets_ == 0 || h == 0) return;
        if (SteamProbeSockets() == 0) { Stop(NoSteamWhy("another Steam call in this game faulted")); return; }
        const char* text = steamlisten::EndText(reason);
        if (!SehCloseConn(sockets_, h, reason, text != 0 ? text : "closed by the host", false)) Fault("CloseConnection");
    }

    /* A held arrival the joiner's side ended: it has no transport to read its state, so its end is read here. Steam frees it only
       through CloseConnection. */
    void HeldGone(int s, unsigned int h, std::vector<SteamListenEvent>* events)
    {
        const unsigned int id = table_.id[s];
        steamlisten::RemoveSlot(&table_, s);
        if (!SehCloseConn(sockets_, h, 0, 0, false)) { Fault("CloseConnection"); return; }
        ++g_listenStats.ended;
        if (events != 0)
        {
            SteamListenEvent e;
            e.kind = SL_ENDED;
            e.link = id;
            e.wasUp = 0;
            events->push_back(e);
        }
        DebugLog("[net] Steam listener: held link " + N(id) + " ended before it was accepted (the joiner's side ended it) - holds "
                 + N(table_.count) + " of " + N(cap_));
    }

    void EndSlot(int s, std::vector<SteamListenEvent>* events)
    {
        SteamLinkFacts f;
        links_[s]->Facts(&f);
        const unsigned int id = table_.id[s];
        const bool wasUp = links_[s]->ConnectGen() != 0;   /* every UP edge counts one */
        DropSlot(s);
        ++g_listenStats.ended;
        if (events != 0)
        {
            SteamListenEvent e;
            e.kind = SL_ENDED;
            e.link = id;
            e.endReason = f.endReason;
            e.wasUp = wasUp ? 1 : 0;
            events->push_back(e);
        }
        DebugLog("[net] Steam listener: link " + N(id) + " ended (end reason " + N(f.endReason) + " '" + f.endText + "'"
                 + (wasUp ? "" : ", it never came up") + ") - holds " + N(table_.count) + " of " + N(cap_));
    }

    void TakeArrivals(std::vector<SteamListenEvent>* events)
    {
        SnEvent ev[kListenEventMax];
        int n = 0;
        ::EnterCriticalSection(&g_cs);
        n = nEvents_;
        if (n > 0) std::memcpy(ev, events_, sizeof(SnEvent) * (size_t)n);
        nEvents_ = 0;
        ::LeaveCriticalSection(&g_cs);
        for (int i = 0; i < n && !broken_; ++i)
        {
            const SnEvent& e = ev[i];
            if (e.listenSocket != listen_ || e.conn == 0) continue;   /* not this socket's */
            const int placed = steamlisten::SlotOfHandle(table_, e.conn);
            if (placed >= 0)
            {
                /* an accepted link reads its own state; a held arrival has no transport yet, so its end is read here */
                if (links_[placed] == 0 && (e.state == kStateClosedByPeer || e.state == kStateProblemDetectedLocally))
                    HeldGone(placed, e.conn, events);
                continue;
            }
            if (e.state == kStateConnecting && e.oldState == kStateNone)
                Arrival(e.conn, events);
            else if (e.state == kStateClosedByPeer || e.state == kStateProblemDetectedLocally)
            {
                /* an arrival that ended before it was taken: Steam frees it only through CloseConnection */
                if (!SehCloseConn(sockets_, e.conn, 0, 0, false)) { Fault("CloseConnection"); return; }
            }
        }
    }

    void Arrival(unsigned int h, std::vector<SteamListenEvent>* events)
    {
        int verdict = steamlisten::ArrivalVerdict(table_.count, cap_, listen_ != 0);
        if (verdict == steamlisten::kArriveAccept && hold_) { Hold(h, events); return; }
        SteamTransport* t = 0;
        if (verdict == steamlisten::kArriveAccept)
        {
            t = new SteamTransport();
            t->SetTrafficLink(nettally::kLinkBridge);   /* a listener's links are the bridge's */
            if (t->Stopped()) { delete t; t = 0; verdict = steamlisten::kArriveNoRoom; }   /* the link registry is full */
        }
        if (t == 0) { Refuse(h, verdict, events); return; }
        int res = 0;
        if (!SehAccept(sockets_, h, &res)) { Fault("AcceptConnection"); delete t; return; }
        if (res != kResultOk)
        {
            delete t;
            ++g_listenStats.refusedOther;
            DebugLog("[net] Steam listener: AcceptConnection refused an incoming connection (EResult " + N(res) + ") - it is dropped");
            if (!SehCloseConn(sockets_, h, 0, 0, false)) Fault("CloseConnection");
            return;
        }
        unsigned int id = 0;
        const int s = steamlisten::Add(&table_, h, cap_, &id);
        std::string err;
        if (s < 0 || !t->TakeAccepted(h, !ipTest_, &err))
        {
            delete t;
            if (s >= 0) steamlisten::RemoveSlot(&table_, s);
            ++g_listenStats.refusedOther;
            ErrorLog("[net] Steam listener: an accepted connection could not be held (" + (s < 0 ? std::string("no place in the link table") : err)
                     + ") - closed with end reason " + N(steamlisten::kEndListenerNoRoom));
            if (!SehCloseConn(sockets_, h, steamlisten::kEndListenerNoRoom, "no room for another Steam link", false)) Fault("CloseConnection");
            return;
        }
        links_[s] = t;
        ++g_listenStats.accepted;
        if (table_.count > g_listenStats.heldMax) g_listenStats.heldMax = table_.count;
        if (events != 0)
        {
            SteamListenEvent e;
            e.kind = SL_ACCEPTED;
            e.link = id;
            events->push_back(e);
        }
        DebugLog("[net] Steam listener: link " + N(id) + " accepted - holds " + N(table_.count) + " of " + N(cap_));
    }

    /* SetHoldArrivals on: the arrival takes a place and a link id, and waits for AcceptHeld / RefuseHeld (or the joiner's giving up). */
    void Hold(unsigned int h, std::vector<SteamListenEvent>* events)
    {
        unsigned int id = 0;
        const int s = steamlisten::Add(&table_, h, cap_, &id);
        if (s < 0) { Refuse(h, steamlisten::kArriveNoRoom, events); return; }
        links_[s] = 0;
        if (table_.count > g_listenStats.heldMax) g_listenStats.heldMax = table_.count;
        if (events != 0)
        {
            SteamListenEvent e;
            e.kind = SL_ARRIVED;
            e.link = id;
            events->push_back(e);
        }
    }

    void Refuse(unsigned int h, int verdict, std::vector<SteamListenEvent>* events)
    {
        const int reason = steamlisten::CloseReasonFor(verdict);
        const bool full = (verdict == steamlisten::kArriveFull);
        if (!SehCloseConn(sockets_, h, reason, full ? "full: this host holds its most Steam links" : "no room for another Steam link", false))
        {
            Fault("CloseConnection");
            return;
        }
        if (full) ++g_listenStats.refusedFull; else ++g_listenStats.refusedOther;
        ++refusedLogged_;
        if (LogThis(refusedLogged_))
            DebugLog("[net] Steam listener: an incoming connection was closed with end reason " + N(reason) + " ("
                     + (full ? "full - it holds " + N(table_.count) + " of " + N(cap_) : std::string("no room for another Steam link in this game"))
                     + "; the first 5 and every 100th are logged)");
        if (events != 0)
        {
            SteamListenEvent e;
            e.kind = SL_REFUSED;
            e.endReason = reason;
            events->push_back(e);
        }
    }

    void ClearEvents()
    {
        if (!g_csReady) return;
        ::EnterCriticalSection(&g_cs);
        nEvents_ = 0;
        ::LeaveCriticalSection(&g_cs);
    }

    /* A fault in a Steam call: counted on the probe, logged, and the listener and its links make no further Steam call. */
    void Fault(const char* where)
    {
        SteamNoteFault();
        ErrorLog("[net] Steam listener: FAULT in " + std::string(where) + " - the listener and its links stop and make no further Steam"
                 " call; every Steam link in this game stops with it (the probe's fault count is no longer 0)");
        Stop(std::string("fault in ") + where);
    }
    void Stop(const std::string& why)
    {
        if (!broken_ && why.compare(0, 6, "fault ") != 0)
        {
            if (SteamProbeShutDown()) DebugLog("[net] Steam listener: stops - " + why + " (no further Steam call from it)");   /* the exit, not a fault */
            else ErrorLog("[net] Steam listener: stops - " + why + " (no further Steam call from it)");
        }
        broken_ = true;
        stopWhy_ = why;
        listen_ = 0;
        for (int s = 0; s < steamlisten::kLinkCap; ++s)
        {
            if (links_[s] != 0) DropSlot(s);   /* after a fault each link's close makes no Steam call */
            else if (table_.id[s] != 0) steamlisten::RemoveSlot(&table_, s);   /* a held arrival: no Steam call either */
        }
        ClearEvents();
    }

    void*        sockets_;
    unsigned int listen_;
    bool         ipTest_;        /* the relay test's plain-UDP listener on 127.0.0.1 */
    bool         broken_;        /* a fault ended this listener: no further Steam call */
    std::string  stopWhy_;
    int          cap_;
    steamlisten::LinkTable table_;
    SteamTransport* links_[steamlisten::kLinkCap];   /* index = the table's place */
    SnEvent      events_[kListenEventMax];   /* written by the callback under g_cs */
    int          nEvents_;
    bool         registered_;
    long long    refusedLogged_;
    unsigned int linkTimeoutMs_;   /* Steam's TimeoutConnected for the links this listener accepts (SetLinkTimeoutMs) */
    bool         hold_;            /* SetHoldArrivals: an arrival within the cap waits for AcceptHeld / RefuseHeld */
};

/* Links every listener holds now, for the REPORT (main thread). */
long long HeldNow()
{
    if (!g_csReady) return 0;
    long long n = 0;
    ::EnterCriticalSection(&g_cs);
    for (int i = 0; i < kListenerMax; ++i)
        if (g_listeners[i] != 0) n += g_listeners[i]->LinkCount();
    ::LeaveCriticalSection(&g_cs);
    return n;
}

/* Steam's status-change function: copies the event to the transport that owns the connection or the listen socket. */
void OnStatusChanged(SnStatusChanged* cb)
{
    SnEvent e;
    if (cb == 0 || !SehReadEvent(cb, &e))
    {
        ::InterlockedIncrement(&g_cbFaults);
        return;
    }
    if (!g_csReady) return;
    ::EnterCriticalSection(&g_cs);
    ++g_stats.events;
    /* The connection's holder first (a listener's accepted link is its own transport), then a listener by its listen socket (an
       arrival), then a transport's own one-link listen socket. */
    SteamTransport* to = 0;
    SteamListenerImpl* lis = 0;
    for (int i = 0; i < kLiveMax && to == 0; ++i)
        if (g_live[i] != 0 && g_live[i]->OwnsConn(e)) to = g_live[i];
    for (int i = 0; i < kListenerMax && to == 0 && lis == 0; ++i)
        if (g_listeners[i] != 0 && g_listeners[i]->OwnsListen(e)) lis = g_listeners[i];
    for (int i = 0; i < kLiveMax && to == 0 && lis == 0; ++i)
        if (g_live[i] != 0 && g_live[i]->OwnsListen(e)) to = g_live[i];
    if (to == 0 && lis == 0) ++g_stats.stray;
    else if (to != 0 ? !to->PushEvent(e) : !lis->PushEvent(e)) ++g_stats.dropped;
    ::LeaveCriticalSection(&g_cs);
}

SteamTransport* Find(ITransport* t)
{
    if (t == 0 || !g_csReady) return 0;
    SteamTransport* found = 0;
    ::EnterCriticalSection(&g_cs);
    for (int i = 0; i < kLiveMax && found == 0; ++i)
        if (g_live[i] != 0 && static_cast<ITransport*>(g_live[i]) == t) found = g_live[i];
    ::LeaveCriticalSection(&g_cs);
    return found;
}

}   /* anonymous namespace */

ITransport* NewSteamTransport()
{
    return new SteamTransport();
}

SteamListener* NewSteamListener()
{
    return new SteamListenerImpl();
}

bool SteamTransportSetIpTest(ITransport* t, bool allowWithoutAuth)
{
    SteamTransport* s = Find(t);
    if (s == 0) return false;
    s->SetIpTest(allowWithoutAuth);
    return true;
}

bool SteamTransportMakePair(ITransport* a, ITransport* b, std::string* errOut)
{
    SteamTransport* x = Find(a);
    SteamTransport* y = Find(b);
    if (x == 0 || y == 0 || x == y)
    {
        if (errOut) *errOut = "a socket pair needs two live Steam links";
        return false;
    }
    return x->PairWith(y, errOut);
}

bool SteamSetFakeLagLoss(int lagMs, int lossPct, std::string* errOut)
{
    void* u = SteamProbeUtils();
    if (u == 0)
    {
        if (errOut) *errOut = "Steam networking is not available in this game";
        return false;
    }
    const int lag = lagMs;
    const float loss = (float)lossPct;
    int ok1 = 0, ok2 = 0;
    if (!SehSetConfig(u, kCfgFakeLagSend, kCfgScopeGlobal, 0, kCfgTypeInt32, &lag, &ok1)
        || !SehSetConfig(u, kCfgFakeLossSend, kCfgScopeGlobal, 0, kCfgTypeFloat, &loss, &ok2))
    {
        SteamNoteFault();
        if (errOut) *errOut = "fault in SetConfigValue";
        ErrorLog("[net] Steam link: FAULT in SetConfigValue (fake lag / loss) - every Steam link in this game stops");
        return false;
    }
    if (!ok1 || !ok2)
    {
        if (errOut) *errOut = "Steam refused the fake lag or loss value";
        return false;
    }
    return true;
}

bool SteamTransportFacts(ITransport* t, SteamLinkFacts* out)
{
    SteamTransport* s = Find(t);
    if (s == 0 || out == 0) return false;
    s->Facts(out);
    return true;
}

std::string SteamLinkReportToken()
{
    const LinkStats& s = g_stats;
    const ListenStats& l = g_listenStats;
    return "steamLink[up,ms,sent,recv,bytesOut,bytesIn,pieces,rejoined,queuedMax,refusedBig,endReason,rateNow,rateMin,rateMax,rateChanges]=" + N(s.up) + "," + N(s.upMs) + ","
           + N(s.sent) + "," + N(s.recv) + "," + N(s.bytesOut) + "," + N(s.bytesIn) + "," + N(s.pieces) + "," + N(s.rejoined) + ","
           + N(s.queuedMax) + "," + N(s.refusedBig) + "," + N(s.endReason) + "," + N(s.rateNow) + "," + N(s.rateMin) + ","
           + N(s.rateMax) + "," + N(s.rateChanges)
           + " steamLinkEv[events,stray,dropped,limitHits,unrelDropped,recvBad,cbFaults]=" + N(s.events) + "," + N(s.stray) + ","
           + N(s.dropped) + "," + N(s.limitHits) + "," + N(s.unrelDropped) + "," + N(s.recvBad) + "," + N((long long)g_cbFaults)
           + " steamListen[opened,heldNow,heldMax,accepted,refusedFull,refusedOther,ended]=" + N(l.opened) + "," + N(HeldNow()) + ","
           + N(l.heldMax) + "," + N(l.accepted) + "," + N(l.refusedFull) + "," + N(l.refusedOther) + "," + N(l.ended)
           + " steamIceOff[read0,not0]=" + N(g_iceStats.read0) + "," + N(g_iceStats.not0);
}

long long SteamIceOffRead0()
{
    return g_iceStats.read0;
}

}   /* namespace net */
}   /* namespace coop */
