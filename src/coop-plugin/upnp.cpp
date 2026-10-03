/* upnp.cpp - T-53: WHILE THIS GAME HOSTS, THE HOME ROUTER IS ASKED TO FORWARD THE WORLD SERVER'S ONE UDP PORT.
 *
 * Windows' own UPnP interface does the asking (NATUPnP in hnetcfg.dll: IUPnPNAT, IStaticPortMappingCollection, COM); no
 * library of ours talks to the router. The decisions - when to ask, what the entry says, what an entry already there
 * means, when it is removed - are src/common/upnpplan.h, swept by the offline suite.
 *
 * THE GAME NEVER WAITS FOR THE ROUTER. Finding the router takes seconds, and a router may never answer at all, so every
 * router call runs on ONE background thread made here (UpnpWorker). The main thread only records what hosting needs
 * (UpnpHostStart) and wakes it; the lock below is never held across a router call. The only wait on the router anywhere
 * is the game's exit (UpnpQuitWait), bounded by coopupnp::kQuitWaitMs, after the game window is gone.
 *
 * WHEN HOSTING ENDS. (1) The world server stops: the worker watches its process handle (a duplicate of the panel's, so
 * the panel may close its own) and removes the entry when the process ends. (2) The game quits: the world server only
 * closes itself after this game is gone (--parent), so the exit path (store.cpp detour_gameWorldDtor) calls
 * UpnpQuitBegin / UpnpQuitWait and the entry is removed before the process ends.
 *
 * NOTHING ABOUT THE FORWARDING IS SHOWN TO THE PLAYER (owner decision 382): [UPNP] log lines and the REPORT's upnp[...]
 * field only.  The same thread also answers the HOSTING window's first INTERNET ADDRESS ask: the address the router itself
 * reports, read from a port-forwarding entry (UpnpAddrAsk / UpnpAddrResult, asked when the window opens).
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
 * The address is never written to the log (streamer safety: logs go into bug reports); only which source answered, or why
 * none did.
 * NO CRASH WITHOUT UPnP: Windows' UPnP missing, discovery switched off, no router, or a router with UPnP off are all
 * answers (failed / noRouter), checked HRESULT by HRESULT; hosting goes on exactly as before.
 * routerport=0 in shared_wastelands.cfg: the router is never asked (UpnpHostOff), counted as `off`, not as noRouter -
 * noRouter means a search ran and nobody answered, off means no search was made.
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
#include "../common/upnpplan.h"
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

/* The REPORT's upnp[...] field. */
volatile LONG64 g_upAsked = 0, g_upMapped = 0, g_upAlready = 0, g_upFailed = 0, g_upRemoved = 0, g_upNoRouter = 0, g_upOff = 0;

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

/* The router as Windows found it. Kept by the worker from the attempt to the removal, so the removal at the game's exit
   does not have to search for the router again inside its bounded wait. Worker thread only. */
struct Router
{
    IUPnPNAT* nat;
    IStaticPortMappingCollection* col;
    Router() : nat(0), col(0) {}
    void Drop()
    {
        if (col != 0) { col->Release(); col = 0; }
        if (nat != 0) { nat->Release(); nat = 0; }
    }
};

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

/* One attempt for `port`. Returns the outcome; *removeAtEnd = coopupnp::RemoveAtEnd. */
int MapPort(Router* r, bool comReady, long port, bool* removeAtEnd)
{
    ::InterlockedIncrement64(&g_upAsked);
    *removeAtEnd = false;
    const DWORD t0 = ::GetTickCount();
    std::string how;
    const std::string client = net::HomeNetworkAddress(&how);
    const std::string desc = coopupnp::Description(port);
    DebugLog("[UPNP] asking the router to forward " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port) + " to "
             + (client.empty() ? std::string("(no home-network address)") : client + ":" + coopupnp::PortText(port))
             + " - " + how);

    std::string detail;
    int out = FindRouter(r, comReady, !client.empty(), &detail);
    if (out == coopupnp::kOutNone)
    {
        BSTR proto   = MakeBstr(coopupnp::kProtocol);
        BSTR bClient = MakeBstr(client);
        BSTR bDesc   = MakeBstr(desc);
        if (proto == 0 || bClient == 0 || bDesc == 0)
        {
            out = coopupnp::kOutFailed;
            detail = "the request's text could not be made (SysAllocString)";
        }
        else
        {
            IStaticPortMapping* ex = 0;
            const HRESULT hrItem = r->col->get_Item(port, proto, &ex);
            const bool found = SUCCEEDED(hrItem) && ex != 0;
            std::string exClient, exDesc;
            long exPort = 0;
            if (found)
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
            const int act = coopupnp::ActionFor(coopupnp::ClassifyExisting(found, exClient, exPort, client, port));
            if (act == coopupnp::kActKeep)
            {
                out = coopupnp::kOutAlready;
                *removeAtEnd = coopupnp::RemoveAtEnd(act, exDesc);
                detail = "the router already forwards it here ('" + exDesc + "') - "
                       + (*removeAtEnd ? std::string("an earlier entry of ours, taken over and removed when hosting ends")
                                       : std::string("not ours, kept and left on the router when hosting ends"));
            }
            else if (act == coopupnp::kActLeave)
            {
                out = coopupnp::kOutFailed;
                detail = "the router already forwards that port to " + (exClient.empty() ? std::string("(not said)") : exClient)
                       + ":" + coopupnp::PortText(exPort) + " ('" + exDesc + "') - left as it is";
            }
            else
            {
                IStaticPortMapping* added = 0;
                const HRESULT hrAdd = r->col->Add(port, proto, port, bClient, VARIANT_TRUE, bDesc, &added);
                if (added != 0) added->Release();
                if (SUCCEEDED(hrAdd))
                {
                    out = coopupnp::kOutMapped;
                    *removeAtEnd = true;
                    detail = "entry '" + desc + "' added, lease " + Num(coopupnp::kLeaseSeconds) + " (kept until removed)";
                }
                else
                {
                    out = coopupnp::kOutFailed;
                    detail = "the router refused the entry (Add, HRESULT " + Hr(hrAdd) + "; no entry was there before, get_Item HRESULT " + Hr(hrItem) + ")";
                }
            }
        }
        ::SysFreeString(proto);
        ::SysFreeString(bClient);
        ::SysFreeString(bDesc);
    }
    CountOutcome(out);
    DebugLog("[UPNP] " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port) + ": " + coopupnp::OutcomeWord(out)
             + " - " + detail + " (" + Num((long long)(DWORD)(::GetTickCount() - t0)) + " ms)");
    if (out != coopupnp::kOutMapped && out != coopupnp::kOutAlready) r->Drop();
    return out;
}

/* Remove our entry for `port`. A collection kept from the attempt may be stale (the router restarted, this computer's
   address changed): one fresh search, then one more try. Whatever the answer, it is not tried again. */
void UnmapPort(Router* r, bool comReady, long port)
{
    const DWORD t0 = ::GetTickCount();
    BSTR proto = MakeBstr(coopupnp::kProtocol);
    HRESULT hr = E_FAIL;
    std::string why;
    for (int attempt = 0; attempt < 2 && proto != 0; ++attempt)
    {
        if (r->col == 0)
        {
            std::string how;
            const std::string client = net::HomeNetworkAddress(&how);
            if (FindRouter(r, comReady, !client.empty(), &why) != coopupnp::kOutNone) break;
        }
        hr = r->col->Remove(port, proto);
        if (SUCCEEDED(hr)) break;
        why = "Remove, HRESULT " + Hr(hr);
        r->Drop();
    }
    if (proto == 0) why = "the request's text could not be made (SysAllocString)";
    ::SysFreeString(proto);
    const std::string ms = Num((long long)(DWORD)(::GetTickCount() - t0));
    if (SUCCEEDED(hr))
    {
        ::InterlockedIncrement64(&g_upRemoved);
        DebugLog("[UPNP] " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port) + ": removed from the router (" + ms + " ms)");
    }
    else
    {
        ::InterlockedIncrement64(&g_upFailed);
        DebugLog("[UPNP] " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(port) + ": NOT removed - " + why
                 + " (" + ms + " ms); the entry carries our description, so the next host on this port takes it over");
    }
}

/* Worker thread.  The address the router reports for `port`'s entry (ours, when hosting forwarded it), else the first
   entry in the router's list that carries one.  1 = found (*ip a public address), 2 = not found (*why). */
int AskExternalAddress(Router* kept, bool comReady, long port, std::string* ip, std::string* why)
{
    Router own;
    Router* r = (kept->col != 0) ? kept : &own;
    if (r == &own)
    {
        std::string how;
        const std::string client = net::HomeNetworkAddress(&how);
        if (FindRouter(&own, comReady, !client.empty(), why) != coopupnp::kOutNone) return 2;
    }
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
        return 2;
    }
    std::string kind, clean;
    if (!coopupnp::RouterAddressPublic(got, &kind, &clean))
    {
        *why = "the router reports a " + kind + " address, not an internet one (another router or the provider's shared address is in front of it)";
        return 2;
    }
    *ip = clean;
    return 1;
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
                 + " - the router will not be asked; hosting goes on without it");
    Router router;
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

        const int step = coopupnp::NextStep(s);
        if (step == coopupnp::kStepMap)
        {
            bool removeAtEnd = false;
            const int out = MapPort(&router, comReady, s.want, &removeAtEnd);
            ::EnterCriticalSection(&g_upLock);
            coopupnp::AfterMap(&g_upState, s.want, out, removeAtEnd);
            ::LeaveCriticalSection(&g_upLock);
            continue;
        }
        if (step == coopupnp::kStepUnmap || step == coopupnp::kStepForget)
        {
            if (step == coopupnp::kStepUnmap) UnmapPort(&router, comReady, s.held);
            else DebugLog("[UPNP] " + std::string(coopupnp::kProtocol) + " " + coopupnp::PortText(s.held)
                          + ": hosting ended - the router's entry was not made by this mod, so it is left there");
            ::EnterCriticalSection(&g_upLock);
            coopupnp::AfterUnmap(&g_upState);
            ::LeaveCriticalSection(&g_upLock);
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

        HANDLE waits[2] = { g_upWake, server };
        const DWORD count = (server != 0) ? 2 : 1;
        const DWORD w = ::WaitForMultipleObjects(count, waits, FALSE, INFINITE);
        if (count == 2 && w == WAIT_OBJECT_0 + 1)
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
      << (long long)g_upFailed << "," << (long long)g_upRemoved << "," << (long long)g_upNoRouter << "," << (long long)g_upOff;
    return o.str();
}

}   /* namespace coop */
