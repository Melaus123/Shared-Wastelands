/* upnp.cpp - T-53: WHILE THIS GAME HOSTS, THE HOME ROUTER IS ASKED TO FORWARD THE WORLD SERVER'S ONE UDP PORT.
 *
 * THE HOME NETWORK is the adapter Windows sends internet traffic through, never a VPN adapter (net/homeaddr.cpp,
 * coopui::PanelPickHomeNic); the router ask goes out from its address and to its router.
 *
 * FOUR ROUTES, tried in src/common/upnpplan.h's order until one settles the port (forwarded, already forwarded here, or
 * forwarded to another computer and left alone):
 *   igd      the mod's own UPnP IGD ask: an SSDP search from the home adapter's address, the router's device description,
 *            GetSpecificPortMappingEntry (what is there already), then AddPortMapping with a kAskLeaseSeconds lease - or
 *            lease 0 when the router answers fault 725 (it only takes permanent entries);
 *   pcp      a PCP MAP request to the router's UDP port 5351, kAskLeaseSeconds asked, the router's granted lifetime kept;
 *   natpmp   the same as NAT-PMP, only when the router answered PCP in NAT-PMP's format;
 *   windows  Windows' own UPnP interface (NATUPnP in hnetcfg.dll: IUPnPNAT, IStaticPortMappingCollection, COM), the last
 *            fallback; it takes no lease.
 * The texts, bytes and readings of every route are src/common/routerwire.h; the sending and receiving net/routerask.cpp;
 * the decisions src/common/upnpplan.h - all swept by the offline suite.
 *
 * A LEASED ENTRY IS RENEWED WHILE HOSTING LASTS: the router thread waits on its wake event and the world server's process
 * with a timeout that falls due at half the lease (upnpplan.h RenewAfterMs); then the route that holds the entry asks
 * again, for kAskLeaseSeconds.  A LEASED entry a crashed game leaves behind therefore disappears from the router on its
 * own when its lease runs out; a permanent one (a router that answered fault 725, or the windows route) stays until the
 * next host on that port takes it over.  A renewal that finds the port forwarded elsewhere marks it no longer ours.
 *
 * THE GAME NEVER WAITS FOR THE ROUTER. Finding the router takes seconds, and a router may never answer at all, so every
 * router call runs on ONE background thread made here (UpnpWorker). The main thread only records what hosting needs
 * (UpnpHostStart) and wakes it; the lock below is never held across a router call. The only wait on the router anywhere
 * is the game's exit (UpnpQuitWait), bounded by coopupnp::kQuitWaitMs, after the game window is gone.
 *
 * WHEN HOSTING ENDS. (1) The world server stops: the worker watches its process handle (a duplicate of the panel's, so
 * the panel may close its own) and removes the entry, by the route that holds it, when the process ends. (2) The game
 * quits: the world server only closes itself after this game is gone (--parent), so the exit path (store.cpp
 * detour_gameWorldDtor) calls UpnpQuitBegin / UpnpQuitWait and the entry is removed before the process ends.
 *
 * NOTHING ABOUT THE FORWARDING IS SHOWN TO THE PLAYER (owner decision 382): [UPNP] log lines and the REPORT's upnp[...]
 * fields only.  The same thread also answers the HOSTING window's first INTERNET ADDRESS ask: the address the router itself
 * reports (igd GetExternalIPAddress, then the NAT-PMP address request, then NATUPnP's port-forwarding entries; asked when the
 * window opens - UpnpAddrAsk / UpnpAddrResult).
 * THE LOOKUP WEBSITES (owner 452) - when the router gives none and the player presses SHOW or COPY, the address-lookup
 * websites of src/common/addrlookup.h, in their order, over WinHTTP (UpnpLookupAsk / UpnpLookupResult) - are asked on a
 * short-lived worker thread of their own (LookupWorker), never on the router thread, so a slow website can never hold up
 * the router's entry being removed when hosting ends or the game quits.  One lookup at a time; an ask while one runs joins
 * it.  Nothing waits for that worker, the game's exit included: it is left running and the process's end stops it.  It
 * owns its WinHTTP handles (opened and closed inside LookupOne), its thread handle is closed as soon as it starts, and it
 * writes its answer only into the fixed buffers below, under their own lock - plain arrays and a lock that is never
 * deleted, nothing that is destroyed while the process lives - so a HOSTING window closed (or opened again) before it
 * answers leaves nothing it could touch: the window reads the answer only by its ask number, and a reopened window has
 * forgotten that number.
 * No address is ever written to the log (streamer safety: logs go into bug reports) - not this computer's, not the
 * router's, not the internet one; only adapter names, which route answered, or why none did.
 * NO CRASH WITHOUT A ROUTER: no router, a router with UPnP / PCP off, discovery switched off, Windows' UPnP missing - all
 * are answers (failed / noRouter), checked call by call; hosting goes on exactly as before.
 * routerport=0 in shared_wastelands.cfg: the router is never asked (UpnpHostOff), counted as `off`, not as noRouter -
 * noRouter means the routes ran and nothing answered, off means nothing was asked.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <natupnp.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <locale>

#include "upnp.h"
#include "coop_log.h"
#include "net/homeaddr.h"
#include "net/routerask.h"
#include "../common/upnpplan.h"
#include "../common/routerwire.h"
#include "../common/addrlookup.h"
#include "../common/names.h"

namespace coop {
namespace {

/* Made by the first UpnpHostStart, on the main thread, before the worker exists. */
CRITICAL_SECTION g_upLock;
bool             g_upLockMade   = false;
bool             g_upStartFailed = false;   /* the worker could not be made: no further attempt this process */
volatile LONG    g_upReady      = 0;        /* 1 once the lock, the wake event and the worker exist */
volatile LONG    g_upStopped    = 0;        /* 1 once the worker has ended (quitting, or it could not wait): no ask is answered after */
HANDLE           g_upWake       = 0;        /* auto-reset: the main thread changed what hosting needs */
HANDLE           g_upThread     = 0;

/* Under g_upLock. */
HANDLE              g_upServer   = 0;       /* our duplicate of the world server's process handle (0 = none watched) */
std::vector<HANDLE> g_upRetired;            /* earlier duplicates: the worker closes them, so none is closed while it may wait on it */
coopupnp::State     g_upState;
bool                g_upQuitting = false;
/* The INTERNET ADDRESS ask: the newest ask's number and port, and the answer to the newest ask the worker finished. */
long                g_upAddrWant = 0, g_upAddrDone = 0;
long                g_upAddrPort = 0;
int                 g_upAddrOutcome = 0;   /* 1 found, 2 not found */
std::string         g_upAddrIp, g_upAddrWhy;

/* THE LOOKUP WORKER's ask and answer, under g_lkLock (made by the first UpnpLookupAsk, on the main thread, before any worker
   exists; never deleted).  Fixed buffers, not strings: nothing here is destroyed while a worker may still write it. */
CRITICAL_SECTION g_lkLock;
bool             g_lkLockMade = false;     /* MAIN THREAD only */
long             g_lkWant = 0;             /* the newest ask's number */
long             g_lkRunning = 0;          /* the ask a worker is answering now (0 = no worker) */
DWORD            g_lkStartMs = 0;          /* GetTickCount at the press that started it */
long             g_lkDone = 0;             /* the ask the last finished worker answered */
int              g_lkOutcome = 0;          /* 1 found, 2 not found */
char             g_lkIp[16], g_lkSource[40], g_lkWhy[640];

/* The REPORT's upnp[...] fields. */
volatile LONG64 g_upAsked = 0, g_upMapped = 0, g_upAlready = 0, g_upFailed = 0, g_upRemoved = 0, g_upNoRouter = 0, g_upOff = 0;
volatile LONG64 g_upByRoute[coopupnp::kRouteCount] = { 0, 0, 0, 0, 0 };   /* asks each route settled */
volatile LONG64 g_upRenewed = 0, g_upRenewFailed = 0;
volatile LONG   g_upLastRoute = 0, g_upLastLease = 0;   /* the route that settled the newest ask, and its lease in seconds */

std::string Num(long long v)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << v;
    return o.str();
}

std::string Hr(HRESULT hr)
{
    char b[16];
    _snprintf(b, sizeof b - 1, "0x%08lX", (unsigned long)hr);
    b[sizeof b - 1] = 0;
    return std::string(b);
}

std::string Ms(DWORD since)
{
    return Num((long long)(DWORD)(::GetTickCount() - since)) + " ms";
}

std::string Narrow(BSTR s)
{
    if (s == 0) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, 0, 0, 0, 0);
    if (n <= 1) return std::string();
    std::vector<char> buf((size_t)n);
    if (::WideCharToMultiByte(CP_UTF8, 0, s, -1, &buf[0], n, 0, 0) <= 0) return std::string();
    return std::string(&buf[0]);
}

/* 0 when the text could not be converted or allocated - every caller checks. */
BSTR MakeBstr(const std::string& s)
{
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, 0, 0);
    if (n <= 0) return 0;
    std::vector<wchar_t> w((size_t)n);
    if (::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n) <= 0) return 0;
    return ::SysAllocString(&w[0]);
}

void CountOutcome(int out)
{
    if (out == coopupnp::kOutMapped)        ::InterlockedIncrement64(&g_upMapped);
    else if (out == coopupnp::kOutAlready)  ::InterlockedIncrement64(&g_upAlready);
    else if (out == coopupnp::kOutNoRouter) ::InterlockedIncrement64(&g_upNoRouter);
    else                                    ::InterlockedIncrement64(&g_upFailed);
}

/* The router as the routes found it. Kept by the worker from the attempt to the renewals and the removal, so neither
   has to search for the router again (the removal at the game's exit runs inside a bounded wait). Worker thread only. */
struct Router
{
    /* the home network when the entry was asked for (net/homeaddr.cpp) */
    std::string homeIp, gateway;
    /* igd: the port-forwarding service's control URL and type ("" = not found) */
    std::string igdControl, igdService;
    /* pcp: the nonce the router echoes; the same one renews and removes the entry (MakeNonce) */
    unsigned char nonce[12];
    bool nonceMade;
    /* what the hosting ask learned, for the INTERNET ADDRESS ask: it ran while this hosting lasts, and the router answered
       on port 5351 (both forgotten when hosting ends - EndHosting) */
    bool mapAsked, pmpSpeaks;
    /* windows */
    IUPnPNAT* nat;
    IStaticPortMappingCollection* col;
    Router() : nonceMade(false), mapAsked(false), pmpSpeaks(false), nat(0), col(0) { memset(nonce, 0, sizeof nonce); }
    void Drop()
    {
        if (col != 0) { col->Release(); col = 0; }
        if (nat != 0) { nat->Release(); nat = 0; }
    }
    /* Hosting ended: Windows' objects are released and what the hosting ask learned is forgotten, so a later address
       ask searches for the router afresh. */
    void EndHosting()
    {
        Drop();
        mapAsked = false;
        pmpSpeaks = false;
        igdControl.clear();
        igdService.clear();
    }
};

/* ------------------------------------------------------------------------------------------------------------------ */
/* igd                                                                                                                 */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Finds the router's port-forwarding service (kept in r).  1 = found; 0 = nothing answered the search; -1 = the search
   could not be sent; -2 = a router answered but its description gave no usable service.  *why says which. */
int IgdFind(Router* r, std::string* why)
{
    if (!r->igdControl.empty()) return 1;
    net::SsdpFound f;
    std::string w;
    const int got = net::SsdpSearch(r->homeIp, r->gateway, &f, &w);
    if (got < 0) { *why = "the search could not be sent: " + w; return -1; }
    if (got == 0)
    {
        *why = "nothing answered the search in " + Num((long long)f.ms) + " ms (" + Num((long long)f.datagrams) + " datagrams"
             + (f.unreadable > 0 ? ", " + Num((long long)f.unreadable) + " refused: " + f.firstWhy : std::string()) + ")";
        return 0;
    }
    /* Only the home adapter's own router counts as a router that refused: another device on the home network that
       answered but offers no usable port-forwarding service is no router at all (noRouter, not failed). */
    const std::string who = f.fromGateway ? "the home adapter's own router" : "another device on the home network";
    const int notThere = f.fromGateway ? -2 : 0;
    std::string xml, hw;
    const int status = net::HttpAsk(f.location, r->gateway, "GET", std::string(), std::string(), swrouter::kMaxDescBytes, &xml, &hw);
    if (status != 200)
    {
        *why = who + " answered the search in " + Num((long long)f.ms) + " ms but its description could not be read ("
             + (status == 0 ? hw : "HTTP " + Num((long long)status)) + ")";
        return notThere;
    }
    std::string ctl, svc, dw;
    if (!swrouter::IgdControlRead(xml, f.location, &ctl, &svc, &dw))
    {
        *why = who + " answered the search but " + dw;
        return notThere;
    }
    r->igdControl = ctl;
    r->igdService = svc;
    DebugLog("[UPNP] igd: " + who + " answered the search in " + Num((long long)f.ms) + " ms; its port-forwarding service is " + swrouter::ServiceShort(svc));
    return 1;
}

/* One SOAP call to the found service.  1 = done (*answer), 0 = the router refused (*fault), -1 = no answer or one that
   cannot be read.  *why names the call and what came back. */
int IgdCall(const Router& r, const std::string& action, const std::string& body, std::string* answer, int* fault, std::string* why)
{
    *fault = -1;
    std::string ans, w;
    const int status = net::HttpAsk(r.igdControl, r.gateway, "POST", swrouter::SoapActionHeader(r.igdService, action), body,
                                    swrouter::kMaxSoapBytes, &ans, &w);
    if (status == 0) { *why = action + ": no answer (" + w + ")"; return -1; }
    std::string ft;
    const int k = swrouter::SoapAnswerRead(status, ans, action, fault, &ft);
    if (k == swrouter::kSoapOk) { *answer = ans; return 1; }
    if (k == swrouter::kSoapFault)
    {
        *why = action + ": fault " + Num((long long)*fault) + " " + swrouter::FaultWord(*fault);
        return 0;
    }
    *why = action + ": an answer that cannot be read (HTTP " + Num((long long)status) + ")";
    return -1;
}

/* A description the router supplied, for the log: whether it is ours, and a safe short form (no address, no line breaks). */
std::string DescForLog(const std::string& d)
{
    return std::string(coopupnp::IsOurDescription(d) ? "ours" : "not ours") + ": '" + swrouter::LogSafeText(d, 48) + "'";
}

/* AddPortMapping for `port` to this computer, asking `*lease` seconds; a 725 to a time-limited ask is asked again as
   permanent (*lease becomes 0).  kRrMapped, kRrElsewhere (718) or kRrRefused; *detail says what happened. */
int IgdAdd(Router* r, long port, long* lease, std::string* detail)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        std::string ans, w;
        int fault = -1;
        const int a = IgdCall(*r, "AddPortMapping",
                              swrouter::SoapAddPortMapping(r->igdService, port, coopupnp::kProtocol, r->homeIp, coopupnp::Description(port), *lease),
                              &ans, &fault, &w);
        if (a == 1) return coopupnp::kRrMapped;
        if (a == 0)
        {
            const int next = swrouter::AddFaultNext(fault, *lease);
            if (next == swrouter::kAddRetryPermanent)
            {
                DebugLog("[UPNP] igd: " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port) + ": " + w
                         + " - the router only takes permanent entries; asked again with lease 0");
                *lease = 0;
                continue;
            }
            if (next == swrouter::kAddHeldElsewhere)
            {
                *detail = "the router forwards that port to another computer (" + w + ") - left as it is";
                return coopupnp::kRrElsewhere;
            }
        }
        *detail = "the router refused the entry (" + w + ")";
        return coopupnp::kRrRefused;
    }
    *detail = "the router refused the entry";
    return coopupnp::kRrRefused;
}

int IgdMap(Router* r, long port, bool* removeAtEnd, long* lease, std::string* detail)
{
    std::string why;
    const int f = IgdFind(r, &why);
    if (f != 1)
    {
        *detail = why;
        return (f == 0) ? coopupnp::kRrNoAnswer : (f == -1 ? coopupnp::kRrUnavailable : coopupnp::kRrRefused);
    }
    /* What the router forwards on the port already.  714 (no such entry) is the usual "nothing"; some routers answer
       another fault for a missing entry, so any refusal reads as nothing there - the add's own 718 still keeps another
       computer's entry safe. */
    std::string ans, w;
    int fault = -1;
    const int g = IgdCall(*r, "GetSpecificPortMappingEntry", swrouter::SoapGetSpecificEntry(r->igdService, port, coopupnp::kProtocol),
                          &ans, &fault, &w);
    if (g < 0)
    {
        *detail = w;
        r->igdControl.clear();
        return coopupnp::kRrRefused;
    }
    const bool found = (g == 1);
    std::string exClient, exDesc;
    long exPort = 0;
    if (found)
    {
        exClient = swrouter::XmlValue(ans, "NewInternalClient");
        if (!swrouter::ReadNum(swrouter::XmlValue(ans, "NewInternalPort"), &exPort)) exPort = 0;
        exDesc = swrouter::XmlValue(ans, "NewPortMappingDescription");
    }
    const int act = coopupnp::ActionFor(coopupnp::ClassifyExisting(found, exClient, exPort, r->homeIp, port));
    if (act == coopupnp::kActLeave)
    {
        *detail = "the router already forwards that port to another computer or port (" + DescForLog(exDesc) + ") - left as it is";
        return coopupnp::kRrElsewhere;
    }
    if (act == coopupnp::kActKeep && !coopupnp::IsOurDescription(exDesc))
    {
        *removeAtEnd = false;
        *lease = 0;
        *detail = "the router already forwards it here (" + DescForLog(exDesc) + ") - kept and left on the router when hosting ends";
        return coopupnp::kRrAlready;
    }
    *lease = coopupnp::kAskLeaseSeconds;
    const int rr = IgdAdd(r, port, lease, detail);
    if (rr != coopupnp::kRrMapped) return rr;
    *removeAtEnd = true;
    if (act == coopupnp::kActKeep)
    {
        *detail = "the router already forwards it here with our description (an earlier entry of ours) - taken over, lease "
                + Num((long long)*lease) + " s, removed when hosting ends";
        return coopupnp::kRrAlready;
    }
    *detail = "entry '" + coopupnp::Description(port) + "' added, lease " + Num((long long)*lease) + " s"
            + (*lease == 0 ? " (permanent: removed when hosting ends)" : "");
    return coopupnp::kRrMapped;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* pcp / natpmp                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------------ */

/* PCP's nonce for `port` (swrouter::PcpNonceFrom): the same on every session of this computer for this port, so a game
   started again after a crash renews or removes the entry the earlier session made. */
void MakeNonce(Router* r, long port)
{
    if (r->nonceMade) return;
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = (DWORD)sizeof name;
    if (!::GetComputerNameA(name, &len)) len = 0;
    name[len < sizeof name ? len : sizeof name - 1] = 0;
    swrouter::PcpNonceFrom(std::string(name) + "|" + r->homeIp + "|" + coopupnp::PortText(port), r->nonce);
    r->nonceMade = true;
}

/* One PCP MAP ask for `port`, `lifetime` seconds (0 removes the entry).  *granted = the router's lifetime.
   Returns kRrMapped (accepted), kRrElsewhere, kRrRefused, kRrNoAnswer, kRrUnavailable or kRrOtherVersion. */
int PcpAsk(Router* r, long port, unsigned long lifetime, unsigned long* granted, std::string* detail)
{
    *granted = 0;
    unsigned long home = 0;
    if (!swrouter::Ipv4Read(r->homeIp, &home)) { *detail = "no home-network address"; return coopupnp::kRrUnavailable; }
    MakeNonce(r, port);
    unsigned char req[swrouter::kPcpMapSize];
    swrouter::PcpMapRequest(req, home, r->nonce, port, port, lifetime);
    unsigned char ans[1100];
    int len = 0;
    std::string w;
    const int u = net::UdpAskRouter(r->homeIp, r->gateway, req, swrouter::kPcpMapSize, ans, (int)sizeof ans, &len, &w);
    if (u == net::kUdpSilent) { *detail = "no answer on the router's port " + Num((long long)swrouter::kPmpPort); return coopupnp::kRrNoAnswer; }
    if (u == net::kUdpNobody) { *detail = "nothing listens on the router's port " + Num((long long)swrouter::kPmpPort); return coopupnp::kRrNoAnswer; }
    if (u != net::kUdpAnswered) { *detail = w; return coopupnp::kRrUnavailable; }
    if (swrouter::PcpAnswerIsPmp(ans, len)) { *detail = "the router answered in NAT-PMP's format"; return coopupnp::kRrOtherVersion; }
    unsigned long result = 0, life = 0, extIp = 0;
    long extPort = 0;
    if (!swrouter::PcpMapAnswerRead(ans, len, r->nonce, &result, &life, &extPort, &extIp))
    {
        *detail = "an answer that cannot be read (" + Num((long long)len) + " bytes)";
        return coopupnp::kRrRefused;
    }
    if (result != 0)
    {
        *detail = "result " + Num((long long)result) + " " + swrouter::PcpResultWord(result);
        return coopupnp::kRrRefused;
    }
    if (lifetime != 0 && extPort != port)
    {
        /* the router gave another external port: the asked one is taken - the offer is handed back at once */
        unsigned char del[swrouter::kPcpMapSize];
        swrouter::PcpMapRequest(del, home, r->nonce, port, extPort, 0);
        int dl = 0;
        std::string dw;
        net::UdpAskRouter(r->homeIp, r->gateway, del, swrouter::kPcpMapSize, ans, (int)sizeof ans, &dl, &dw);
        *detail = "the router offered external port " + Num((long long)extPort) + " instead - the asked port is taken; the offer was handed back";
        return coopupnp::kRrElsewhere;
    }
    *granted = life;
    *detail = (lifetime == 0) ? std::string("removed") : "accepted, lifetime " + Num((long long)life) + " s";
    return coopupnp::kRrMapped;
}

/* The same in NAT-PMP's format (a removal sends external port 0 and lifetime 0, RFC 6886 3.4). */
int PmpAsk(Router* r, long port, unsigned long lifetime, unsigned long* granted, std::string* detail)
{
    *granted = 0;
    unsigned char req[swrouter::kPmpMapRequestSize];
    swrouter::PmpMapRequest(req, port, lifetime == 0 ? 0 : port, lifetime);
    unsigned char ans[1100];
    int len = 0;
    std::string w;
    const int u = net::UdpAskRouter(r->homeIp, r->gateway, req, swrouter::kPmpMapRequestSize, ans, (int)sizeof ans, &len, &w);
    if (u == net::kUdpSilent) { *detail = "no answer on the router's port " + Num((long long)swrouter::kPmpPort); return coopupnp::kRrNoAnswer; }
    if (u == net::kUdpNobody) { *detail = "nothing listens on the router's port " + Num((long long)swrouter::kPmpPort); return coopupnp::kRrNoAnswer; }
    if (u != net::kUdpAnswered) { *detail = w; return coopupnp::kRrUnavailable; }
    unsigned long result = 0, life = 0;
    long inPort = 0, extPort = 0;
    if (!swrouter::PmpMapAnswerRead(ans, len, &result, &inPort, &extPort, &life))
    {
        *detail = "an answer that cannot be read (" + Num((long long)len) + " bytes)";
        return coopupnp::kRrRefused;
    }
    if (result != 0)
    {
        *detail = "result " + Num((long long)result) + " " + swrouter::PmpResultWord(result);
        return coopupnp::kRrRefused;
    }
    if (lifetime != 0 && extPort != port)
    {
        unsigned char del[swrouter::kPmpMapRequestSize];
        swrouter::PmpMapRequest(del, port, 0, 0);
        int dl = 0;
        std::string dw;
        net::UdpAskRouter(r->homeIp, r->gateway, del, swrouter::kPmpMapRequestSize, ans, (int)sizeof ans, &dl, &dw);
        *detail = "the router offered external port " + Num((long long)extPort) + " instead - the asked port is taken; the offer was handed back";
        return coopupnp::kRrElsewhere;
    }
    *granted = life;
    *detail = (lifetime == 0) ? std::string("removed") : "accepted, lifetime " + Num((long long)life) + " s";
    return coopupnp::kRrMapped;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* windows (NATUPnP)                                                                                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

/* kOutNone = the router answered (r->col is set); otherwise noRouter / failed and *why says which call said so. */
int FindRouter(Router* r, bool comReady, bool homeAddrFound, std::string* why)
{
    if (r->col != 0) return coopupnp::kOutNone;
    r->Drop();
    HRESULT hrCreate = E_FAIL;
    HRESULT hrCol = E_FAIL;
    if (homeAddrFound && comReady)
    {
        hrCreate = ::CoCreateInstance(__uuidof(UPnPNAT), 0, CLSCTX_ALL, __uuidof(IUPnPNAT), (void**)&r->nat);
        if (SUCCEEDED(hrCreate) && r->nat != 0) hrCol = r->nat->get_StaticPortMappingCollection(&r->col);
    }
    const bool created = SUCCEEDED(hrCreate) && r->nat != 0;
    const int out = coopupnp::FindRouterOutcome(homeAddrFound, comReady, created, (long)hrCol, r->col != 0);
    if (out == coopupnp::kOutNone) return out;
    if (!homeAddrFound)  *why = "this computer has no home-network address to forward to";
    else if (!comReady)  *why = "COM is not available on the router thread";
    else if (!created)   *why = "Windows' UPnP interface could not be created (CoCreateInstance UPnPNAT, HRESULT " + Hr(hrCreate) + ")";
    else if (FAILED(hrCol)) *why = "Windows could not search for a router (get_StaticPortMappingCollection, HRESULT " + Hr(hrCol) + ")";
    else                 *why = "no UPnP router answered Windows' search (no router, or UPnP is switched off on it)";
    r->Drop();
    return out;
}

int WindowsMap(Router* r, bool comReady, long port, bool* removeAtEnd, std::string* detail)
{
    const std::string& client = r->homeIp;
    const std::string desc = coopupnp::Description(port);
    const int found = FindRouter(r, comReady, !client.empty(), detail);
    if (found == coopupnp::kOutNoRouter) return coopupnp::kRrNoAnswer;
    if (found != coopupnp::kOutNone) return coopupnp::kRrUnavailable;
    int rr = coopupnp::kRrRefused;
    BSTR proto   = MakeBstr(coopupnp::kProtocol);
    BSTR bClient = MakeBstr(client);
    BSTR bDesc   = MakeBstr(desc);
    if (proto == 0 || bClient == 0 || bDesc == 0) *detail = "the request's text could not be made (SysAllocString)";
    else
    {
        IStaticPortMapping* ex = 0;
        const HRESULT hrItem = r->col->get_Item(port, proto, &ex);
        const bool exists = SUCCEEDED(hrItem) && ex != 0;
        std::string exClient, exDesc;
        long exPort = 0;
        if (exists)
        {
            BSTR b = 0;
            if (SUCCEEDED(ex->get_InternalClient(&b))) exClient = Narrow(b);
            ::SysFreeString(b);
            b = 0;
            long ip = 0;
            if (SUCCEEDED(ex->get_InternalPort(&ip))) exPort = ip;
            if (SUCCEEDED(ex->get_Description(&b))) exDesc = Narrow(b);
            ::SysFreeString(b);
        }
        if (ex != 0) ex->Release();
        const int act = coopupnp::ActionFor(coopupnp::ClassifyExisting(exists, exClient, exPort, client, port));
        if (act == coopupnp::kActKeep)
        {
            rr = coopupnp::kRrAlready;
            *removeAtEnd = coopupnp::RemoveAtEnd(act, exDesc);
            *detail = "the router already forwards it here (" + DescForLog(exDesc) + ") - "
                    + (*removeAtEnd ? std::string("an earlier entry of ours, taken over and removed when hosting ends")
                                    : std::string("not ours, kept and left on the router when hosting ends"));
        }
        else if (act == coopupnp::kActLeave)
        {
            rr = coopupnp::kRrElsewhere;
            *detail = "the router already forwards that port to another computer or port (" + DescForLog(exDesc) + ") - left as it is";
        }
        else
        {
            IStaticPortMapping* added = 0;
            const HRESULT hrAdd = r->col->Add(port, proto, port, bClient, VARIANT_TRUE, bDesc, &added);
            if (added != 0) added->Release();
            if (SUCCEEDED(hrAdd))
            {
                rr = coopupnp::kRrMapped;
                *removeAtEnd = true;
                *detail = "entry '" + desc + "' added, lease " + Num((long long)coopupnp::kWindowsLeaseSeconds) + " (kept until removed)";
            }
            else
                *detail = "the router refused the entry (Add, HRESULT " + Hr(hrAdd) + "; no entry was there before, get_Item HRESULT " + Hr(hrItem) + ")";
        }
    }
    ::SysFreeString(proto);
    ::SysFreeString(bClient);
    ::SysFreeString(bDesc);
    return rr;
}

/* Remove our entry by NATUPnP. A collection kept from the attempt may be stale (the router restarted, this computer's
   address changed): one fresh search, then one more try. */
bool WindowsUnmap(Router* r, bool comReady, long port, std::string* why)
{
    BSTR proto = MakeBstr(coopupnp::kProtocol);
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 2 && proto != 0; ++attempt)
    {
        if (r->col == 0 && FindRouter(r, comReady, !r->homeIp.empty(), why) != coopupnp::kOutNone) break;
        hr = r->col->Remove(port, proto);
        if (SUCCEEDED(hr)) break;
        *why = "Remove, HRESULT " + Hr(hr);
        r->Drop();
    }
    if (proto == 0) *why = "the request's text could not be made (SysAllocString)";
    ::SysFreeString(proto);
    return SUCCEEDED(hr);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* The ask, the renewal and the removal, route by route.                                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

/* One attempt for `port`: the routes in upnpplan.h's order until one settles it.  Returns the outcome; *removeAtEnd,
   *route (the route that settled it) and *lease (its granted seconds, 0 = permanent). */
int MapPort(Router* r, bool comReady, long port, bool* removeAtEnd, int* route, long* lease)
{
    ::InterlockedIncrement64(&g_upAsked);
    *removeAtEnd = false;
    *route = coopupnp::kRouteNone;
    *lease = 0;
    const DWORD t0 = ::GetTickCount();
    const std::string what = std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port);
    net::HomeNet h;
    net::HomeNetwork(&h);
    r->Drop();
    r->homeIp = h.ip;
    r->gateway = h.gateway;
    r->igdControl.clear();
    r->igdService.clear();
    r->nonceMade = false;
    r->pmpSpeaks = false;
    r->mapAsked = true;
    DebugLog("[UPNP] asking the router to forward " + what + " to this computer - home network: " + h.how);

    int out = coopupnp::kOutNoRouter;
    if (h.ip.empty())
        DebugLog("[UPNP] " + what + ": no route tried - this computer has no home-network address to forward to");
    for (int rt = h.ip.empty() ? coopupnp::kRouteNone : coopupnp::FirstRoute(); rt != coopupnp::kRouteNone; )
    {
        const DWORD t1 = ::GetTickCount();
        std::string detail;
        bool rm = false;
        long granted = 0;
        int rr = coopupnp::kRrUnavailable;
        if (rt == coopupnp::kRouteIgd) rr = IgdMap(r, port, &rm, &granted, &detail);
        else if (rt == coopupnp::kRoutePcp || rt == coopupnp::kRoutePmp)
        {
            unsigned long life = 0;
            if (r->gateway.empty()) detail = "the home adapter names no router address";
            else rr = (rt == coopupnp::kRoutePcp) ? PcpAsk(r, port, (unsigned long)coopupnp::kAskLeaseSeconds, &life, &detail)
                                                  : PmpAsk(r, port, (unsigned long)coopupnp::kAskLeaseSeconds, &life, &detail);
            if (rr == coopupnp::kRrMapped) { rm = true; granted = coopupnp::ClampLease((long)(life > 0x7FFFFFFFul ? 0x7FFFFFFFul : life)); }
            if (rr != coopupnp::kRrNoAnswer && rr != coopupnp::kRrUnavailable) r->pmpSpeaks = true;
        }
        else rr = WindowsMap(r, comReady, port, &rm, &detail);
        DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(rt)) + ": " + what + ": " + coopupnp::RouteResultWord(rr) + " - "
                 + detail + " (" + Ms(t1) + ")");
        out = coopupnp::FoldOutcome(out, rr);
        if (coopupnp::RouteSettles(rr))
        {
            *route = rt;
            *removeAtEnd = rm;
            *lease = granted;
            ::InterlockedIncrement64(&g_upByRoute[rt]);
            break;
        }
        rt = coopupnp::NextRoute(rt, rr);
    }
    if (*route != coopupnp::kRouteWindows) r->Drop();
    CountOutcome(out);
    ::InterlockedExchange(&g_upLastRoute, (LONG)*route);
    ::InterlockedExchange(&g_upLastLease, (LONG)*lease);
    DebugLog("[UPNP] " + what + ": " + coopupnp::OutcomeWord(out)
             + (*route != coopupnp::kRouteNone ? " by " + std::string(coopupnp::RouteWord(*route)) + ", lease " + Num((long long)*lease) + " s"
                                               : std::string(" - no route settled the port"))
             + " (" + Ms(t0) + ")");
    return out;
}

/* Asks the route that holds `port` again, for kAskLeaseSeconds, so its lease does not run out.  1 = accepted (*granted =
   the new lease), 0 = not accepted (asked again sooner), -1 = the port is forwarded elsewhere now - no longer ours. */
int RenewPort(Router* r, int route, long port, long lease, long* granted)
{
    const DWORD t0 = ::GetTickCount();
    const std::string what = std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port);
    std::string detail;
    bool ok = false, lost = false;
    *granted = lease;
    if (route == coopupnp::kRouteIgd)
    {
        long asked = coopupnp::kAskLeaseSeconds;
        std::string why;
        if (IgdFind(r, &why) != 1) detail = why;
        else
        {
            int rr = IgdAdd(r, port, &asked, &detail);
            if (rr != coopupnp::kRrMapped && rr != coopupnp::kRrElsewhere)
            {
                /* the control URL may have changed (the router restarted): one fresh search, one more ask */
                r->igdControl.clear();
                if (IgdFind(r, &why) == 1) { asked = coopupnp::kAskLeaseSeconds; rr = IgdAdd(r, port, &asked, &detail); }
                else detail += "; " + why;
            }
            if (rr == coopupnp::kRrElsewhere)
            {
                /* 718: the entry on the port is looked up first - one that is still this computer's and ours is kept
                   (upnpplan.h RenewConflictStillOurs), with the lease the router says is left on it */
                std::string ans, w;
                int fault = -1;
                const int g = IgdCall(*r, "GetSpecificPortMappingEntry", swrouter::SoapGetSpecificEntry(r->igdService, port, coopupnp::kProtocol),
                                      &ans, &fault, &w);
                long exPort = 0, left = 0;
                if (g == 1 && !swrouter::ReadNum(swrouter::XmlValue(ans, "NewInternalPort"), &exPort)) exPort = 0;
                const std::string exDesc = (g == 1) ? swrouter::XmlValue(ans, "NewPortMappingDescription") : std::string();
                if (coopupnp::RenewConflictStillOurs(g == 1, (g == 1) ? swrouter::XmlValue(ans, "NewInternalClient") : std::string(),
                                                     exPort, exDesc, r->homeIp, port))
                {
                    asked = (swrouter::ReadNum(swrouter::XmlValue(ans, "NewLeaseDuration"), &left) && left > 0) ? left : lease;
                    rr = coopupnp::kRrMapped;
                    DebugLog("[UPNP] igd: " + what + ": the router answered 718 to the renewal, but its entry on the port is still"
                             " this computer's and ours - kept");
                }
                else if (g == 1) detail += "; the entry on that port: " + DescForLog(exDesc);
                else detail += "; the entry on that port could not be looked up (" + w + ")";
            }
            ok = (rr == coopupnp::kRrMapped);
            lost = (rr == coopupnp::kRrElsewhere);
            if (ok) *granted = asked;
        }
    }
    else if (route == coopupnp::kRoutePcp || route == coopupnp::kRoutePmp)
    {
        unsigned long life = 0;
        const int rr = (route == coopupnp::kRoutePcp) ? PcpAsk(r, port, (unsigned long)coopupnp::kAskLeaseSeconds, &life, &detail)
                                                      : PmpAsk(r, port, (unsigned long)coopupnp::kAskLeaseSeconds, &life, &detail);
        ok = (rr == coopupnp::kRrMapped);
        lost = (rr == coopupnp::kRrElsewhere);
        if (ok) *granted = (long)(life > 0x7FFFFFFFul ? 0x7FFFFFFFul : life);
    }
    else detail = "this route holds no lease";
    *granted = coopupnp::ClampLease(*granted);
    if (ok)
    {
        ::InterlockedIncrement64(&g_upRenewed);
        ::InterlockedExchange(&g_upLastLease, (LONG)*granted);
        DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(route)) + ": " + what + ": renewed, lease " + Num((long long)*granted)
                 + " s (" + Ms(t0) + ")");
        return 1;
    }
    ::InterlockedIncrement64(&g_upRenewFailed);
    if (lost)
    {
        DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(route)) + ": " + what + ": NOT renewed - " + detail + " (" + Ms(t0)
                 + "); the port is no longer ours - not renewed again and not removed when hosting ends");
        return -1;
    }
    DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(route)) + ": " + what + ": NOT renewed - " + detail + " (" + Ms(t0)
             + "); asked again in " + Num((long long)(coopupnp::RenewAfterMs(lease, false) / 1000ul)) + " s");
    return 0;
}

/* Removes our entry for `port` by the route that holds it.  Whatever the answer, it is not tried again: a leased entry
   ends on its own when its lease runs out, and every entry carries our description, so the next host on that port
   takes it over. */
void UnmapPort(Router* r, bool comReady, long port, int route)
{
    const DWORD t0 = ::GetTickCount();
    const std::string what = std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port);
    std::string why;
    bool ok = false;
    if (route == coopupnp::kRouteIgd)
    {
        if (IgdFind(r, &why) == 1)
        {
            /* what the router forwards on the port now: deleted only while it is still ours (upnpplan.h DeleteAction);
               a fault other than 714 leaves it unknown, and the delete is sent anyway (upnpplan.h DeleteCheckNext) */
            std::string ans;
            int fault = -1;
            const int g = IgdCall(*r, "GetSpecificPortMappingEntry", swrouter::SoapGetSpecificEntry(r->igdService, port, coopupnp::kProtocol),
                                  &ans, &fault, &why);
            const int next = coopupnp::DeleteCheckNext(g, fault);
            if (next == coopupnp::kDelCheckGone)
            {
                DebugLog("[UPNP] igd: " + what + ": already gone from the router (its lease ran out, or the router restarted)");
                return;
            }
            if (next == coopupnp::kDelCheckJudge)
            {
                long exPort = 0;
                if (!swrouter::ReadNum(swrouter::XmlValue(ans, "NewInternalPort"), &exPort)) exPort = 0;
                const std::string exDesc = swrouter::XmlValue(ans, "NewPortMappingDescription");
                if (coopupnp::DeleteAction(true, swrouter::XmlValue(ans, "NewInternalClient"), exPort, exDesc, r->homeIp, port) != coopupnp::kDelRemove)
                {
                    DebugLog("[UPNP] igd: " + what + ": the router's entry on that port is no longer ours (" + DescForLog(exDesc)
                             + ") - left as it is");
                    return;
                }
            }
            if (next == coopupnp::kDelCheckUnchecked)
                DebugLog("[UPNP] igd: " + what + ": could not check the entry first (" + why + "); asked to remove it");
            if (next != coopupnp::kDelCheckNoAnswer)
            {
                const int d = IgdCall(*r, "DeletePortMapping", swrouter::SoapDeletePortMapping(r->igdService, port, coopupnp::kProtocol),
                                      &ans, &fault, &why);
                ok = (d == 1);
                if (d == 0 && fault == 714) { ok = true; why.clear(); }   /* gone between the two calls */
            }
        }
    }
    else if (route == coopupnp::kRoutePcp || route == coopupnp::kRoutePmp)
    {
        unsigned long life = 0;
        const int rr = (route == coopupnp::kRoutePcp) ? PcpAsk(r, port, 0, &life, &why) : PmpAsk(r, port, 0, &life, &why);
        ok = (rr == coopupnp::kRrMapped);
    }
    else ok = WindowsUnmap(r, comReady, port, &why);
    if (ok)
    {
        ::InterlockedIncrement64(&g_upRemoved);
        DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(route)) + ": " + what + ": removed from the router (" + Ms(t0) + ")");
    }
    else
    {
        ::InterlockedIncrement64(&g_upFailed);
        DebugLog("[UPNP] " + std::string(coopupnp::RouteWord(route)) + ": " + what + ": NOT removed - " + why + " (" + Ms(t0)
                 + "); a leased entry ends with its lease, and the entry carries our description, so the next host on this port takes it over");
    }
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* The INTERNET ADDRESS the router reports.                                                                            */
/* ------------------------------------------------------------------------------------------------------------------ */

/* A candidate address: true when it is a public one (*ip = its clean text); otherwise *why says what kind it was. */
bool TakePublic(const std::string& got, std::string* ip, std::string* why)
{
    std::string kind, clean;
    if (!coopupnp::RouterAddressPublic(got, &kind, &clean))
    {
        *why = "the router reports a " + kind + " address, not an internet one (another router or the provider's shared address is in front of it)";
        return false;
    }
    *ip = clean;
    return true;
}

/* NATUPnP: the address on `port`'s entry (ours, when hosting forwarded it), else on the first entry the router lists. */
bool WindowsExternalAddress(Router* kept, bool comReady, long port, std::string* ip, std::string* why)
{
    Router own;
    own.homeIp = kept->homeIp;
    Router* r = (kept->col != 0) ? kept : &own;
    if (r == &own && FindRouter(&own, comReady, !own.homeIp.empty(), why) != coopupnp::kOutNone) return false;
    std::string got;
    BSTR proto = MakeBstr(coopupnp::kProtocol);
    if (proto != 0 && coopupnp::PortOk(port))
    {
        IStaticPortMapping* m = 0;
        if (SUCCEEDED(r->col->get_Item(port, proto, &m)) && m != 0)
        {
            BSTR b = 0;
            if (SUCCEEDED(m->get_ExternalIPAddress(&b))) got = Narrow(b);
            ::SysFreeString(b);
        }
        if (m != 0) m->Release();
    }
    ::SysFreeString(proto);
    long listed = 0;
    if (got.empty())
    {
        IUnknown* unk = 0;
        IEnumVARIANT* en = 0;
        if (SUCCEEDED(r->col->get__NewEnum(&unk)) && unk != 0
         && SUCCEEDED(unk->QueryInterface(IID_IEnumVARIANT, (void**)&en)) && en != 0)
        {
            VARIANT v;
            ::VariantInit(&v);
            ULONG fetched = 0;
            while (got.empty() && listed < 256 && en->Next(1, &v, &fetched) == S_OK && fetched == 1)
            {
                ++listed;
                if (v.vt == VT_DISPATCH && v.pdispVal != 0)
                {
                    IStaticPortMapping* m = 0;
                    if (SUCCEEDED(v.pdispVal->QueryInterface(__uuidof(IStaticPortMapping), (void**)&m)) && m != 0)
                    {
                        BSTR b = 0;
                        if (SUCCEEDED(m->get_ExternalIPAddress(&b))) got = Narrow(b);
                        ::SysFreeString(b);
                        m->Release();
                    }
                }
                ::VariantClear(&v);
            }
        }
        if (en != 0) en->Release();
        if (unk != 0) unk->Release();
    }
    own.Drop();
    if (got.empty())
    {
        *why = (listed == 0) ? "the router lists no port-forwarding entry to read it from"
                             : "the router's port-forwarding entries carry no internet address";
        return false;
    }
    return TakePublic(got, ip, why);
}

/* Worker thread.  The router's own idea of the internet address, by igd (GetExternalIPAddress), then NAT-PMP's address
   request, then NATUPnP.  1 = found (*ip a public address), 2 = not found (*why names each route and why it gave none -
   never an address). */
int AskExternalAddress(Router* kept, bool comReady, long port, std::string* ip, std::string* why)
{
    /* WHAT THE HOSTING ASK LEARNED IS REUSED - no fresh search: the router thread always runs the hosting ask before an
       address ask (NextStep comes first), so each route is asked here only when the hosting ask found the router on it,
       and with none found the answer is "none" at once.  Only when no hosting ask ran is the full chain tried. */
    if (kept->mapAsked)
    {
        std::string wIgd = "no UPnP router answered when hosting asked", wPmp = "the router did not answer on port 5351 when hosting asked",
                    wWin = "Windows found no router when hosting asked";
        if (!kept->igdControl.empty())
        {
            std::string ans;
            int fault = -1;
            if (IgdCall(*kept, "GetExternalIPAddress", swrouter::SoapGetExternalAddress(kept->igdService), &ans, &fault, &wIgd) == 1)
            {
                const std::string got = swrouter::XmlValue(ans, "NewExternalIPAddress");
                if (got.empty()) wIgd = "the router gave no address";
                else if (TakePublic(got, ip, &wIgd)) return 1;
            }
        }
        if (kept->pmpSpeaks && !kept->gateway.empty())
        {
            unsigned char req[swrouter::kPmpAddrRequestSize];
            swrouter::PmpAddrRequest(req);
            unsigned char ans[1100];
            int len = 0;
            const int u = net::UdpAskRouter(kept->homeIp, kept->gateway, req, swrouter::kPmpAddrRequestSize, ans, (int)sizeof ans, &len, &wPmp);
            unsigned long result = 0, addr = 0;
            if (u == net::kUdpSilent)      wPmp = "no answer on the router's port " + Num((long long)swrouter::kPmpPort);
            else if (u == net::kUdpNobody) wPmp = "nothing listens on the router's port " + Num((long long)swrouter::kPmpPort);
            else if (u == net::kUdpAnswered)
            {
                if (!swrouter::PmpAddrAnswerRead(ans, len, &result, &addr)) wPmp = "an answer in another format";
                else if (result != 0) wPmp = "result " + Num((long long)result) + " " + swrouter::PmpResultWord(result);
                else if (TakePublic(swrouter::Ipv4Text(addr), ip, &wPmp)) return 1;
            }
        }
        if (kept->col != 0 && WindowsExternalAddress(kept, comReady, port, ip, &wWin)) return 1;
        *why = "igd: " + wIgd + "; natpmp: " + wPmp + "; windows: " + wWin;
        return 2;
    }
    /* no hosting ask ran: the home network as it is now */
    Router probe;
    Router* r = kept;
    if (kept->igdControl.empty() || kept->homeIp.empty())
    {
        net::HomeNet h;
        net::HomeNetwork(&h);
        probe.homeIp = h.ip;
        probe.gateway = h.gateway;
        r = &probe;
    }
    std::string wIgd, wPmp, wWin;
    if (r->homeIp.empty()) wIgd = "no home-network address";
    else
    {
        const int f = IgdFind(r, &wIgd);
        if (f == 1)
        {
            std::string ans;
            int fault = -1;
            if (IgdCall(*r, "GetExternalIPAddress", swrouter::SoapGetExternalAddress(r->igdService), &ans, &fault, &wIgd) == 1)
            {
                const std::string got = swrouter::XmlValue(ans, "NewExternalIPAddress");
                if (got.empty()) wIgd = "the router gave no address";
                else if (TakePublic(got, ip, &wIgd)) return 1;
            }
        }
    }
    if (r->gateway.empty()) wPmp = "the home adapter names no router address";
    else
    {
        unsigned char req[swrouter::kPmpAddrRequestSize];
        swrouter::PmpAddrRequest(req);
        unsigned char ans[1100];
        int len = 0;
        const int u = net::UdpAskRouter(r->homeIp, r->gateway, req, swrouter::kPmpAddrRequestSize, ans, (int)sizeof ans, &len, &wPmp);
        unsigned long result = 0, addr = 0;
        if (u == net::kUdpSilent)      wPmp = "no answer on the router's port " + Num((long long)swrouter::kPmpPort);
        else if (u == net::kUdpNobody) wPmp = "nothing listens on the router's port " + Num((long long)swrouter::kPmpPort);
        else if (u == net::kUdpAnswered)
        {
            if (!swrouter::PmpAddrAnswerRead(ans, len, &result, &addr)) wPmp = "an answer in another format";
            else if (result != 0) wPmp = "result " + Num((long long)result) + " " + swrouter::PmpResultWord(result);
            else if (TakePublic(swrouter::Ipv4Text(addr), ip, &wPmp)) return 1;
        }
    }
    Router winKept;
    winKept.homeIp = r->homeIp;
    Router* w = (kept->col != 0) ? kept : &winKept;
    if (WindowsExternalAddress(w, comReady, port, ip, &wWin)) return 1;
    winKept.Drop();
    *why = "igd: " + wIgd + "; natpmp: " + wPmp + "; windows: " + wWin;
    return 2;
}

/* LOOKUP WORKER. One website, one plain GET: 1 = a usable address (*ip), 0 = none (*why in words, never an address). */
int LookupOne(const coopaddr::LookupSite& site, std::string* ip, std::string* why)
{
    HINTERNET s = ::WinHttpOpen(swnames::kAddrLookupAgentW, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s == 0) { *why = "WinHttpOpen failed, Windows error " + Num((long long)::GetLastError()); return 0; }
    ::WinHttpSetTimeouts(s, coopaddr::kLookupStepMs, coopaddr::kLookupStepMs, coopaddr::kLookupStepMs, coopaddr::kLookupStepMs);
    HINTERNET c = ::WinHttpConnect(s, site.host, site.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    HINTERNET r = (c != 0) ? ::WinHttpOpenRequest(c, L"GET", site.path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  site.https ? WINHTTP_FLAG_SECURE : 0) : 0;
    int got = 0;
    DWORD status = 0;
    std::string body;
    if (r == 0) *why = "no connection could be made, Windows error " + Num((long long)::GetLastError());
    else if (!::WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !::WinHttpReceiveResponse(r, 0))
        *why = "no answer, Windows error " + Num((long long)::GetLastError());
    else
    {
        DWORD len = sizeof status;
        if (!::WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX))
            status = 0;
        char buf[coopaddr::kLookupMaxBody + 1];
        DWORD read = 0;
        while ((int)body.size() <= coopaddr::kLookupMaxBody && ::WinHttpReadData(r, buf, (DWORD)coopaddr::kLookupMaxBody, &read) && read > 0)
            body.append(buf, read);
        if (status != 200) *why = "it answered HTTP " + Num((long long)status);
        else if ((int)body.size() > coopaddr::kLookupMaxBody) *why = "its answer is too long to be an address";
        else
        {
            std::string w;
            if (coopaddr::LookupAnswer(body, ip, &w)) got = 1;
            else *why = "its answer is " + w;
        }
    }
    if (r != 0) ::WinHttpCloseHandle(r);
    if (c != 0) ::WinHttpCloseHandle(c);
    ::WinHttpCloseHandle(s);
    return got;
}

/* LOOKUP WORKER. The websites in their order; the first usable answer wins; no further website once the deadline since the
   press (`startMs`) has passed. 1 = found (*ip, *source = the website's name), 2 = none (*why names each website and why it
   gave none, or that it was not asked). */
int LookupFromSites(DWORD startMs, std::string* ip, std::string* source, std::string* why)
{
    why->clear();
    for (int i = 0; i < coopaddr::kLookupSiteCount; ++i)
    {
        const coopaddr::LookupSite& site = coopaddr::LookupSiteAt(i);
        std::string w;
        const unsigned long ms = coopaddr::LookupElapsedMs(startMs, ::GetTickCount());
        if (coopaddr::LookupMayAskNext(ms) == 0) w = "not asked (" + Num((long long)ms) + " ms had passed since the press)";
        else if (LookupOne(site, ip, &w) != 0) { *source = site.name; return 1; }
        *why += (why->empty() ? std::string() : std::string("; ")) + site.name + ": " + w;
    }
    return 2;
}

DWORD WINAPI UpnpWorker(LPVOID)
{
    const HRESULT hrCom = ::CoInitializeEx(0, COINIT_MULTITHREADED);
    const bool comReady = SUCCEEDED(hrCom);   /* S_OK, or S_FALSE when this thread's apartment already existed */
    if (!comReady)
        ErrorLog("[UPNP] CoInitializeEx (multithreaded) failed on the router thread, HRESULT " + Hr(hrCom)
                 + " - Windows' UPnP route is not available; the mod's own routes still ask");
    Router router;
    /* THE RENEWAL TIMER: armed after an entry with a lease is held (RenewAfterMs), disarmed (renewAfter 0) while none is. */
    DWORD renewArmedAt = 0;
    unsigned long renewAfter = 0;
    for (;;)
    {
        coopupnp::State s;
        HANDLE server = 0;
        bool quitting = false;
        std::vector<HANDLE> retired;
        ::EnterCriticalSection(&g_upLock);
        s = g_upState;
        server = g_upServer;
        quitting = g_upQuitting;
        retired.swap(g_upRetired);
        ::LeaveCriticalSection(&g_upLock);
        for (size_t i = 0; i < retired.size(); ++i) ::CloseHandle(retired[i]);
        /* hosting ended with no port held (its ask settled nothing, or the entry is already removed or forgotten):
           what the hosting ask learned is forgotten too */
        if (s.want == 0 && s.held == 0 && router.mapAsked) router.EndHosting();

        const int step = coopupnp::NextStep(s);
        if (step == coopupnp::kStepMap)
        {
            bool removeAtEnd = false;
            int route = coopupnp::kRouteNone;
            long lease = 0;
            const int out = MapPort(&router, comReady, s.want, &removeAtEnd, &route, &lease);
            ::EnterCriticalSection(&g_upLock);
            coopupnp::AfterMap(&g_upState, s.want, out, removeAtEnd, route, lease);
            const long heldLease = (g_upState.held != 0) ? g_upState.lease : 0;
            ::LeaveCriticalSection(&g_upLock);
            renewArmedAt = ::GetTickCount();
            renewAfter = coopupnp::RenewAfterMs(heldLease, true);
            continue;
        }
        if (step == coopupnp::kStepRenew)
        {
            long granted = s.lease;
            const int renewed = RenewPort(&router, s.route, s.held, s.lease, &granted);
            const bool ok = (renewed > 0);
            ::EnterCriticalSection(&g_upLock);
            if (renewed < 0) coopupnp::AfterRenewLost(&g_upState);
            else coopupnp::AfterRenew(&g_upState, ok, granted);
            const long heldLease = (g_upState.held != 0) ? g_upState.lease : 0;
            ::LeaveCriticalSection(&g_upLock);
            renewArmedAt = ::GetTickCount();
            renewAfter = coopupnp::RenewAfterMs(heldLease, ok);
            continue;
        }
        if (step == coopupnp::kStepUnmap || step == coopupnp::kStepForget)
        {
            if (step == coopupnp::kStepUnmap) UnmapPort(&router, comReady, s.held, s.route);
            else DebugLog("[UPNP] " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(s.held)
                          + ": hosting ended - the router's entry was not made by this mod, so it is left there");
            router.EndHosting();
            ::EnterCriticalSection(&g_upLock);
            coopupnp::AfterUnmap(&g_upState);
            ::LeaveCriticalSection(&g_upLock);
            renewAfter = 0;
            continue;
        }
        if (quitting) break;

        long addrAsk = 0, addrPort = 0;
        ::EnterCriticalSection(&g_upLock);
        if (g_upAddrWant != g_upAddrDone) { addrAsk = g_upAddrWant; addrPort = g_upAddrPort; }
        ::LeaveCriticalSection(&g_upLock);
        if (addrAsk != 0)
        {
            std::string ip, why;
            const int got = AskExternalAddress(&router, comReady, addrPort, &ip, &why);
            ::EnterCriticalSection(&g_upLock);
            g_upAddrDone = addrAsk;
            g_upAddrOutcome = got;
            g_upAddrIp = ip;
            g_upAddrWhy = why;
            ::LeaveCriticalSection(&g_upLock);
            continue;
        }

        /* Wait for the main thread, the world server's end, or the lease's renewal time - whichever comes first. */
        const DWORD timeout = (renewAfter == 0) ? INFINITE
                            : (DWORD)coopupnp::RenewWaitMs((unsigned long)renewArmedAt, renewAfter, (unsigned long)::GetTickCount());
        HANDLE waits[2] = { g_upWake, server };
        const DWORD count = (server != 0) ? 2 : 1;
        const DWORD w = ::WaitForMultipleObjects(count, waits, FALSE, timeout);
        if (w == WAIT_TIMEOUT)
        {
            renewAfter = 0;
            ::EnterCriticalSection(&g_upLock);
            if (g_upState.held != 0 && g_upState.lease > 0) g_upState.renewDue = true;
            ::LeaveCriticalSection(&g_upLock);
        }
        else if (count == 2 && w == WAIT_OBJECT_0 + 1)
        {
            DWORD code = 0;
            const BOOL gotCode = ::GetExitCodeProcess(server, &code);
            bool ended = false;
            ::EnterCriticalSection(&g_upLock);
            if (g_upServer == server)
            {
                g_upServer = 0;
                coopupnp::HostEnd(&g_upState);
                ended = true;
            }
            ::LeaveCriticalSection(&g_upLock);
            if (ended)
            {
                ::CloseHandle(server);
                DebugLog("[UPNP] the world server stopped (exit code " + (gotCode ? Num((long long)code) : std::string("unreadable"))
                         + ") - hosting ended");
            }
        }
        else if (w == WAIT_FAILED)
        {
            const DWORD e = ::GetLastError();
            if (server == 0)
            {
                ErrorLog("[UPNP] the router thread cannot wait for work, Windows error " + Num((long long)e) + " - it stops; the router is not asked again this game");
                break;
            }
            ErrorLog("[UPNP] the world server's process cannot be waited on, Windows error " + Num((long long)e)
                     + " - it is no longer watched; the router's entry is removed at the game's exit instead");
            ::EnterCriticalSection(&g_upLock);
            if (g_upServer == server) { g_upRetired.push_back(server); g_upServer = 0; }
            ::LeaveCriticalSection(&g_upLock);
        }
    }
    router.Drop();
    if (comReady) ::CoUninitialize();
    ::InterlockedExchange(&g_upStopped, 1);
    DebugLog("[UPNP] the router thread stopped");
    return 0;
}

/* `s` into a fixed buffer of `size`, cut short if it is longer, always ended. */
void CopyCut(char* to, size_t size, const std::string& s)
{
    const size_t n = s.size() < size - 1 ? s.size() : size - 1;
    if (n != 0) memcpy(to, s.data(), n);
    to[n] = 0;
}

/* THE LOOKUP WORKER: one ask (its number is the thread's argument), the websites in order, the answer into g_lk*, then the
   thread ends.  Nobody waits for it. */
DWORD WINAPI LookupWorker(LPVOID arg)
{
    const long ask = (long)(LONG_PTR)arg;
    ::EnterCriticalSection(&g_lkLock);
    const DWORD startMs = g_lkStartMs;
    ::LeaveCriticalSection(&g_lkLock);
    std::string ip, source, why;
    const int got = LookupFromSites(startMs, &ip, &source, &why);
    ::EnterCriticalSection(&g_lkLock);
    g_lkDone = ask;
    g_lkOutcome = got;
    CopyCut(g_lkIp, sizeof g_lkIp, got == 1 ? ip : std::string());
    CopyCut(g_lkSource, sizeof g_lkSource, source);
    CopyCut(g_lkWhy, sizeof g_lkWhy, why);
    if (g_lkRunning == ask) g_lkRunning = 0;
    ::LeaveCriticalSection(&g_lkLock);
    return 0;
}

/* MAIN THREAD. The lock, the wake event and the worker, made once. */
bool EnsureWorker()
{
    if (::InterlockedCompareExchange(&g_upReady, 0, 0) != 0) return true;
    if (g_upStartFailed) return false;
    if (!g_upLockMade)
    {
        ::InitializeCriticalSection(&g_upLock);
        g_upLockMade = true;
    }
    g_upWake = ::CreateEventA(0, FALSE, FALSE, 0);
    if (g_upWake == 0)
    {
        g_upStartFailed = true;
        ::InterlockedIncrement64(&g_upFailed);
        ErrorLog("[UPNP] CreateEvent failed, Windows error " + Num((long long)::GetLastError()) + " - the router is not asked this game");
        return false;
    }
    g_upThread = ::CreateThread(0, 0, &UpnpWorker, 0, 0, 0);
    if (g_upThread == 0)
    {
        g_upStartFailed = true;
        ::InterlockedIncrement64(&g_upFailed);
        ErrorLog("[UPNP] the router thread could not be started, Windows error " + Num((long long)::GetLastError()) + " - the router is not asked this game");
        ::CloseHandle(g_upWake);
        g_upWake = 0;
        return false;
    }
    ::InterlockedExchange(&g_upReady, 1);
    return true;
}

}   /* namespace */

void UpnpHostStart(unsigned short port, void* serverProcess)
{
    if (!coopupnp::PortOk((long)port)) return;
    if (!EnsureWorker()) return;
    HANDLE dup = 0;
    if (serverProcess == 0
     || ::DuplicateHandle(::GetCurrentProcess(), (HANDLE)serverProcess, ::GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0)
    {
        dup = 0;
        ErrorLog("[UPNP] the world server's process cannot be watched (DuplicateHandle, Windows error " + Num((long long)::GetLastError())
                 + ") - the router's entry is removed at the game's exit instead of when the world server stops");
    }
    bool taken = false;
    ::EnterCriticalSection(&g_upLock);
    if (!g_upQuitting)
    {
        if (g_upServer != 0) g_upRetired.push_back(g_upServer);
        g_upServer = dup;
        coopupnp::HostStart(&g_upState, (long)port);
        taken = true;
    }
    ::LeaveCriticalSection(&g_upLock);
    if (!taken)
    {
        if (dup != 0) ::CloseHandle(dup);
        return;
    }
    ::SetEvent(g_upWake);
    DebugLog("[UPNP] hosting on " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText((long)port)
             + " - the router is asked on the router thread; the game does not wait for it and the player is shown nothing");
}

void UpnpHostOff(unsigned short port)
{
    ::InterlockedIncrement64(&g_upOff);
    DebugLog("[UPNP] off by the settings file (routerport=0) - the router is not asked to forward " + std::string(coopupnp::kProtocol)
             + " " + coopupnp::PortText((long)port) + "; hosting goes on without it");
}

void UpnpQuitBegin()
{
    if (::InterlockedCompareExchange(&g_upReady, 0, 0) == 0) return;
    ::EnterCriticalSection(&g_upLock);
    g_upQuitting = true;
    coopupnp::HostEnd(&g_upState);
    ::LeaveCriticalSection(&g_upLock);
    ::SetEvent(g_upWake);
}

void UpnpQuitWait()
{
    if (::InterlockedCompareExchange(&g_upReady, 0, 0) == 0) return;
    const DWORD t0 = ::GetTickCount();
    const DWORD w = ::WaitForSingleObject(g_upThread, coopupnp::kQuitWaitMs);
    const std::string ms = Num((long long)(DWORD)(::GetTickCount() - t0));
    if (w == WAIT_OBJECT_0)
        DebugLog("[UPNP] quit: the router thread finished in " + ms + " ms");
    else
        DebugLog("[UPNP] quit: the router has not finished after " + ms + " ms - the game ends anyway; an entry left on the"
                 " router carries our description, so the next host on that port takes it over");
}

long UpnpAddrAsk(unsigned short port)
{
    if (!EnsureWorker()) return 0;
    long id = 0;
    ::EnterCriticalSection(&g_upLock);
    id = ++g_upAddrWant;
    g_upAddrPort = (long)port;
    ::LeaveCriticalSection(&g_upLock);
    ::SetEvent(g_upWake);
    return id;
}

int UpnpAddrResult(long askId, std::string* ip, std::string* why)
{
    if (askId <= 0 || ::InterlockedCompareExchange(&g_upReady, 0, 0) == 0) return 0;
    int out = 0;
    ::EnterCriticalSection(&g_upLock);
    if (g_upAddrDone == askId)
    {
        out = g_upAddrOutcome;
        *ip = g_upAddrIp;
        *why = g_upAddrWhy;
    }
    ::LeaveCriticalSection(&g_upLock);
    if (out == 0 && ::InterlockedCompareExchange(&g_upStopped, 0, 0) != 0)
    {
        out = 2;
        *why = "the router thread has stopped";
    }
    return out;
}

long UpnpLookupAsk()
{
    if (!g_lkLockMade)
    {
        ::InitializeCriticalSection(&g_lkLock);
        g_lkLockMade = true;
    }
    long id = 0;
    bool start = false;
    ::EnterCriticalSection(&g_lkLock);
    if (g_lkRunning != 0) id = g_lkRunning;   /* one lookup at a time: this ask joins the one running */
    else
    {
        id = ++g_lkWant;
        g_lkRunning = id;
        g_lkStartMs = ::GetTickCount();
        start = true;
    }
    ::LeaveCriticalSection(&g_lkLock);
    if (!start) return id;
    HANDLE t = ::CreateThread(0, 0, &LookupWorker, (LPVOID)(LONG_PTR)id, 0, 0);
    if (t == 0)
    {
        const DWORD e = ::GetLastError();
        ::EnterCriticalSection(&g_lkLock);
        if (g_lkRunning == id) g_lkRunning = 0;
        ::LeaveCriticalSection(&g_lkLock);
        ErrorLog("[UPNP] the address-lookup worker could not be started, Windows error " + Num((long long)e) + " - no website is asked for this press");
        return 0;
    }
    ::CloseHandle(t);   /* nobody waits for it: it ends on its own, or with the process */
    return id;
}

int UpnpLookupResult(long askId, std::string* ip, std::string* source, std::string* why)
{
    if (askId <= 0 || !g_lkLockMade) return 0;
    int out = 0;
    ::EnterCriticalSection(&g_lkLock);
    if (g_lkDone == askId)
    {
        out = g_lkOutcome;
        *ip = g_lkIp;
        *source = g_lkSource;
        *why = g_lkWhy;
    }
    ::LeaveCriticalSection(&g_lkLock);
    return out;
}

std::string UpnpReportToken()
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "upnp[asked,mapped,already,failed,removed,noRouter,off]="
      << (long long)g_upAsked << "," << (long long)g_upMapped << "," << (long long)g_upAlready << ","
      << (long long)g_upFailed << "," << (long long)g_upRemoved << "," << (long long)g_upNoRouter << "," << (long long)g_upOff
      << " upnpRoute[igd,pcp,natpmp,windows,renewed,renewFailed]="
      << (long long)g_upByRoute[coopupnp::kRouteIgd] << "," << (long long)g_upByRoute[coopupnp::kRoutePcp] << ","
      << (long long)g_upByRoute[coopupnp::kRoutePmp] << "," << (long long)g_upByRoute[coopupnp::kRouteWindows] << ","
      << (long long)g_upRenewed << "," << (long long)g_upRenewFailed
      << " upnpLast=" << coopupnp::RouteWord((int)g_upLastRoute) << "/" << (long)g_upLastLease;
    return o.str();
}

}   /* namespace coop */
