/* src/common/relaybridge.h - THE STEAM ROUTE'S DECISIONS. PURE: no Windows.h, no Steam, no ENet, no globals, no I/O, so the offline
   suite checks exactly what the plugin compiles.

   A game reaches the world server either directly (ENet over UDP to the address the player typed) or through Steam: the address
   steam:<64-bit id> dials that Steam id over Valve's relay, and the host's game (net/relay_bridge.cpp, the bridge) gives every Steam
   link its own connection to the world server on 127.0.0.1:<port> and copies the messages both ways unchanged. This file holds the
   steam:<id> address form, the route a dial takes, the connect budget of a Steam dial, when the host's bridge is open, and the
   bridge's limits: how long a joiner's arrival waits for its own world-server connection, how it slows down reading the world
   server while a joiner's Steam link is behind, when a joiner is too far behind to keep, and how long closing the whole bridge
   waits. */
#ifndef SW_RELAYBRIDGE_H
#define SW_RELAYBRIDGE_H

#include <string>
#include <cstddef>

namespace relaybridge {

/* ---- the steam:<id> address ---- */

/* The port a steam:<id> address carries. The Steam route dials no port, but every world-server target is <address>:<port> with a
   non-zero port (port 0 means "no world server"), so a steam:<id> typed without a port gets this one. */
const unsigned short kSteamRoutePort = 1;

/* "steam:" (any case) followed by 1-20 decimal digits making a non-zero 64-bit number. *id is written only on a yes. */
inline bool SteamAddressId(const std::string& a, unsigned long long* id)
{
    if (a.size() < 7 || a.size() > 26) return false;
    static const char kPrefix[] = "steam:";
    for (size_t i = 0; i < 6; ++i)
    {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != kPrefix[i]) return false;
    }
    unsigned long long v = 0;
    for (size_t i = 6; i < a.size(); ++i)
    {
        const char c = a[i];
        if (c < '0' || c > '9') return false;
        const unsigned long long d = (unsigned long long)(c - '0');
        if (v > (~0ULL - d) / 10ULL) return false;
        v = v * 10ULL + d;
    }
    if (v == 0) return false;
    if (id != 0) *id = v;
    return true;
}
inline bool IsSteamAddress(const std::string& a) { return SteamAddressId(a, 0); }

/* The dial form of an id: "steam:" and its decimal digits (lower case, no leading zero) - what the Steam transport's Join reads. */
inline std::string SteamAddressText(unsigned long long id)
{
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + (int)(id % 10ULL)); id /= 10ULL; } while (id != 0 && n < 23);
    std::string s("steam:");
    while (n > 0) s.push_back(tmp[--n]);
    return s;
}

/* A whole typed or file value that is steam:<id>: *addr = its dial form, *port = kSteamRoutePort. Written only on a yes. */
inline bool SplitSteamValue(const std::string& v, std::string* addr, unsigned short* port)
{
    unsigned long long id = 0;
    if (!SteamAddressId(v, &id)) return false;
    if (addr != 0) *addr = SteamAddressText(id);
    if (port != 0) *port = kSteamRoutePort;
    return true;
}

/* An address for a log line: text that starts "steam:" (any case) never shows what follows - a Steam id, a mistyped one, a port
   after it (logs travel in bug reports). Any other address is as given. */
inline std::string AddrForLog(const std::string& a)
{
    static const char kPrefix[] = "steam:";
    if (a.size() < 6) return a;
    for (size_t i = 0; i < 6; ++i)
    {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != kPrefix[i]) return a;
    }
    return std::string("steam:<id>");
}

/* An address and its port as the player sees them (the panel's lines, the JOIN box): a steam:<id> address shows no port - its
   kSteamRoutePort is a stand-in the player never typed; any other is <address>:<port>. */
inline std::string AddrPortForPlayer(const std::string& a, unsigned int port)
{
    if (IsSteamAddress(a)) return a;
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + (int)(port % 10u)); port /= 10u; } while (port != 0 && n < 11);
    std::string s(a);
    s.push_back(':');
    while (n > 0) s.push_back(tmp[--n]);
    return s;
}

/* ---- the route a world-server dial takes ---- */

enum Route { kRouteDirect = 0, kRouteSteam = 1, kRouteSteamIpTest = 2 };

/* viaIpTestPort non-zero (TEST ONLY, relayvia=ip:<port>): every world-server dial goes over the Steam transport's plain UDP to
   127.0.0.1:<port>, whatever the address - the one-PC stand-in for the relay. Otherwise a steam:<id> address takes Steam (Valve's
   relay only) and anything else the direct route. */
inline int RouteFor(const std::string& address, unsigned short viaIpTestPort)
{
    if (viaIpTestPort != 0) return kRouteSteamIpTest;
    return IsSteamAddress(address) ? kRouteSteam : kRouteDirect;
}

inline const char* RouteWord(int route)
{
    if (route == kRouteSteam) return "Steam (Valve's relay only)";
    if (route == kRouteSteamIpTest) return "Steam's plain UDP to 127.0.0.1 (relay test relayvia=ip)";
    return "direct (ENet/UDP)";
}
inline const char* RouteToken(int route)
{
    if (route == kRouteSteam) return "steam";
    if (route == kRouteSteamIpTest) return "steamIp";
    return "direct";
}

/* How long a dial may stay connecting before the re-dial gives up on it. Steam's own first-connect timeout is 10 s
   (net/steam_transport.cpp kTimeoutInitialMs), so a Steam dial is given 12 s: Steam ends a dial that is not coming with its own
   reason first, and that reason is what the route line reports. A direct dial keeps its budget; a longer one is kept for both. */
const unsigned int kSteamConnectBudgetMs = 12000;
inline unsigned int ConnectBudgetMs(int route, unsigned int directBudgetMs)
{
    if (route == kRouteDirect) return directBudgetMs;
    return directBudgetMs > kSteamConnectBudgetMs ? directBudgetMs : kSteamConnectBudgetMs;
}

/* ---- when the host's bridge is open ---- */

/* An address naming this PC: "localhost" (any case) or 127.<n>.<n>.<n>, each part 1-3 digits. */
inline bool IsLoopbackHost(const std::string& a)
{
    if (a.size() == 9)
    {
        static const char kLocal[] = "localhost";
        bool same = true;
        for (size_t i = 0; i < 9 && same; ++i)
        {
            char c = a[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != kLocal[i]) same = false;
        }
        if (same) return true;
    }
    if (a.size() < 9 || a.compare(0, 4, "127.") != 0) return false;
    int parts = 2, digits = 0;   /* "127" and the part being read */
    for (size_t i = 4; i < a.size(); ++i)
    {
        const char c = a[i];
        if (c == '.') { if (digits == 0) return false; ++parts; digits = 0; continue; }
        if (c < '0' || c > '9' || ++digits > 3) return false;
    }
    return parts == 4 && digits > 0;
}

enum SteamState { kSteamNotYet = 0, kSteamReady = 1, kSteamAbsent = 2 };
enum Want { kWantClosed = 0, kWantWait = 1, kWantOpen = 2, kWantNoSteam = 3, kWantBadPort = 4 };

/* The bridge is open while this game HOSTS (role host) a world server on this PC (its world-server address is a loopback address
   with a port) and the bridge is not switched off (relaybridge=off), once Steam has been looked up and is there. In the relay test
   (ipTestPort non-zero) the bridge's plain-UDP listener cannot share the world server's own port. */
inline int BridgeWant(bool roleHost, bool storeLoopback, unsigned short storePort, bool bridgeOff, unsigned short ipTestPort, int steam)
{
    if (!roleHost || !storeLoopback || storePort == 0 || bridgeOff) return kWantClosed;
    if (ipTestPort != 0 && ipTestPort == storePort) return kWantBadPort;
    if (steam == kSteamNotYet) return kWantWait;
    if (steam != kSteamReady) return kWantNoSteam;
    return kWantOpen;
}

/* A listener Steam would not open is tried again this often. */
const unsigned int kOpenRetryMs = 10000;

/* ---- one Steam link and its world-server connection ---- */

/* A joiner's arrival is held, not accepted, while its own connection to the world server is dialled; that connection has this long
   to come up (ENet's own minimum timeout; the world server is on this PC). Refused, dropped or late, the arrival is refused with
   steamlisten::kEndHostServerDown and the joiner's own re-dial tries again. The joiner's Steam dial waits for it inside Steam's
   first-connect timeout (net/steam_transport.cpp kTimeoutInitialMs, 10 s), which the listen socket's connections take too. */
const unsigned int kLocalConnectMs = 5000;
inline bool LocalConnectLate(unsigned int sinceDialMs) { return sinceDialMs >= kLocalConnectMs; }

/* BACK-PRESSURE toward a joiner. The world server on this PC hands the bridge messages at loopback speed; a joiner's Steam link
   carries 128 KB/s to 4 MB/s (relaysplit.h's send-rate step). While what waits on the joiner's Steam link (its own queue plus what
   Steam holds) is at kSlowAtBytes or more, the bridge reads that joiner's world-server connection only every kSlowPollMs instead of
   every frame, until it is down to kFastAtBytes. Reading less often acknowledges less often, so the world server's ENet sends less
   and keeps the rest itself (its own send bound); reading at least every kSlowPollMs keeps the connection well inside ENet's
   timeouts. kSlowAtBytes is the Steam send buffer of one link (steam_transport.cpp kSendBufferBytes). */
const long long    kSlowAtBytes = 4194304;   /* 4 MB */
const long long    kFastAtBytes = 1048576;   /* 1 MB */
const unsigned int kSlowPollMs  = 500;
inline bool SlowNext(bool slowNow, long long waitingBytes)
{
    return slowNow ? waitingBytes > kFastAtBytes : waitingBytes >= kSlowAtBytes;
}
inline bool ReadLocalNow(bool slow, unsigned int sinceLastReadMs) { return !slow || sinceLastReadMs >= kSlowPollMs; }

/* A joiner this far behind in either direction (what waits toward it on its Steam link, or what waits toward the world server on
   its connection) is closed: twice the largest message either side carries (relaysplit.h kRejoinMax, 32 MiB), so one largest
   message always fits and memory stays bounded. */
const long long kFarBehindBytes = 67108864;   /* 64 MB */
inline bool FarBehind(long long waitingBytes) { return waitingBytes > kFarBehindBytes; }

/* A JOINER'S WORLD-SERVER SIDE WHILE ITS STEAM LINK COMES UP. Accepting a held arrival starts the joiner's Steam link connecting,
   and Steam takes no message on it until it is up; the world server sends guaranteed messages to every connected game about once
   a second. So, each frame:
   - kJoinerDialling (not accepted yet): the world-server connection is read every frame (it is coming up); what it hands up waits.
   - kJoinerHold (accepted, its Steam link not up yet): the connection is read only every kSlowPollMs, as a slow link's is (the
     world server keeps the rest, and the connection stays inside ENet's timeouts); what it hands up waits.
   - kJoinerPass (accepted, its Steam link up): what waited goes to the joiner first, in order; then the connection is read as
     back-pressure says and what it hands up goes on at once.
   What waits is bounded by FarBehind on its bytes. */
enum JoinerStep { kJoinerDialling = 0, kJoinerHold = 1, kJoinerPass = 2 };
inline int JoinerSideStep(bool accepted, bool steamUp)
{
    if (!accepted) return kJoinerDialling;
    return steamUp ? kJoinerPass : kJoinerHold;
}

/* CLOSING THE WHOLE BRIDGE (hosting ends, the world server it bridges to changes, this game exits) has kCloseAllBudgetMs in all.
   A pair whose world-server connection is up is closed gracefully (ENet waits up to about kCloseAllGracefulMs for the world
   server's acknowledgement) only while that whole wait still fits: a graceful close starts only while at least kCloseAllGracefulMs
   of the budget remain. The rest are closed at once, so 32 joiners and a world server already gone do not hold this game's frame
   for 32 x 300 ms, and the whole close stays within about kCloseAllBudgetMs. */
const unsigned int kCloseAllGracefulMs = 300;
const unsigned int kCloseAllBudgetMs   = 600;
inline bool CloseGracefully(unsigned int msSinceCloseAllBegan) { return msSinceCloseAllBegan <= kCloseAllBudgetMs - kCloseAllGracefulMs; }

/* ---- the host's frame ---- */

/* A gap between two bridge ticks this long, while a Steam joiner is linked, is said in a line: nothing is relayed while this
   game's frame stands still (a world load or a save here), so every Steam joiner waits that long. */
const unsigned int kStallLineMs = 2000;
inline bool StallWorthALine(unsigned int gapMs, int linksUp) { return linksUp > 0 && gapMs >= kStallLineMs; }

/* The first 5 and every 100th of a repeated line. */
inline bool LogThis(long long n) { return n >= 1 && (n <= 5 || n % 100 == 0); }

}   /* namespace relaybridge */

#endif   /* SW_RELAYBRIDGE_H */
