/* THE MOD'S OWN ROUTER ASK - sending and receiving only (routerask.h).  Its own translation unit because winsock2.h must
   come ahead of windows.h.  Router thread only (upnp.cpp). */
#undef WINVER
#define WINVER 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

/* Older SDK headers may lack these WinHTTP option numbers (winhttp.h values). */
#ifndef WINHTTP_OPTION_DISABLE_FEATURE
#define WINHTTP_OPTION_DISABLE_FEATURE 63
#endif
#ifndef WINHTTP_DISABLE_REDIRECTS
#define WINHTTP_DISABLE_REDIRECTS 0x00000002
#endif
#ifndef WINHTTP_OPTION_AUTOLOGON_POLICY
#define WINHTTP_OPTION_AUTOLOGON_POLICY 77
#endif
#ifndef WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH
#define WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH 2
#endif
#ifndef WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT
#define WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT 7
#endif

#include <cstring>
#include <string>
#include <vector>
#include "routerask.h"
#include "../../common/routerwire.h"
#include "../../common/names.h"
#include "../coop_log.h"

namespace coop {
namespace net {
namespace {

/* Winsock for the length of one call (WSAStartup counts its users, so the game's own use is untouched). */
struct WsaHold
{
    bool ok;
    WsaHold() : ok(false)
    {
        WSADATA d;
        ok = (::WSAStartup(MAKEWORD(2, 2), &d) == 0);
    }
    ~WsaHold() { if (ok) ::WSACleanup(); }
};

std::string WinErr(const char* what, long code)
{
    return std::string(what) + ", Windows error " + swrouter::Num(code);
}

std::wstring Wide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, 0, 0);
    if (n <= 0) return std::wstring();
    std::vector<wchar_t> w((size_t)n);
    if (::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n) <= 0) return std::wstring();
    return std::wstring(&w[0]);
}

/* A UDP socket bound to `home` (host order), port chosen by Windows.  INVALID_SOCKET with *why on failure. */
SOCKET BoundUdp(unsigned long home, std::string* why)
{
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { *why = WinErr("socket", (long)::WSAGetLastError()); return INVALID_SOCKET; }
    sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(home);
    local.sin_port = 0;
    if (::bind(s, (const sockaddr*)&local, sizeof local) != 0)
    {
        *why = WinErr("bind to the home adapter's address", (long)::WSAGetLastError());
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* Waits at most `ms` for `s` to be readable: 1 readable, 0 not yet, -1 error. */
int Readable(SOCKET s, unsigned long ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv;
    tv.tv_sec = (long)(ms / 1000ul);
    tv.tv_usec = (long)((ms % 1000ul) * 1000ul);
    const int n = ::select(0, &rd, 0, 0, &tv);
    if (n < 0) return -1;
    return (n > 0) ? 1 : 0;
}

volatile LONG g_unsafeRefused = 0;   /* HTTP requests refused because Windows would not take a safety option */

/* No redirect is followed (a device could send the request on to any computer) and Windows' sign-in is never sent.  When
   Windows will not take either option the request is refused: false with *why (it carries the count so far); the first
   refusal is also logged on its own. */
bool SafeRequest(HINTERNET r, std::string* why)
{
    const char* failed = 0;
    long err = 0;
    DWORD noRedirect = WINHTTP_DISABLE_REDIRECTS;
    if (!::WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &noRedirect, sizeof noRedirect))
    {
        failed = "turn off redirects";
        err = (long)::GetLastError();
    }
    else
    {
        DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if (!::WinHttpSetOption(r, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof logon))
        {
            failed = "hold back its sign-in";
            err = (long)::GetLastError();
        }
    }
    if (failed == 0) return true;
    const LONG count = ::InterlockedIncrement(&g_unsafeRefused);
    *why = "refused: Windows would not " + std::string(failed) + " for the request, Windows error " + swrouter::Num(err)
         + " (" + swrouter::Num((long)count) + " refused so far)";
    if (count == 1)
        DebugLog("[UPNP] a request to the router was refused: Windows would not " + std::string(failed) + " for it, Windows error "
                 + swrouter::Num(err) + " - every such request is refused");
    return false;
}

/* Reads the body that follows the headers.  Its clock starts when the headers arrived; before every read the clock is
   checked and the wait for more is cut to the time left (swrouter::HttpBodyWaitMs); each read takes only what has
   already arrived (WinHttpQueryDataAvailable), and at most the buffer.  true = the whole body is in *answer (at most
   maxBytes); false = *why (still arriving at kHttpWholeMs, longer than maxBytes, or broken off). */
bool ReadBody(HINTERNET r, int maxBytes, std::string* answer, std::string* why)
{
    const std::string late = "an answer still arriving " + swrouter::Num((long)swrouter::kHttpWholeMs) + " ms after its headers";
    const DWORD t0 = ::GetTickCount();
    char buf[4096];
    for (;;)
    {
        DWORD wait = (DWORD)swrouter::HttpBodyWaitMs((unsigned long)(DWORD)(::GetTickCount() - t0));
        if (wait == 0) { *why = late; return false; }
        if (!::WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_TIMEOUT, &wait, sizeof wait))
        {
            *why = WinErr("the wait for the rest of the answer could not be cut to the time left", (long)::GetLastError());
            return false;
        }
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(r, &avail))
        {
            const long e = (long)::GetLastError();
            *why = (e == ERROR_WINHTTP_TIMEOUT) ? late : WinErr("the answer broke off", e);
            return false;
        }
        if (avail == 0) return true;   /* the whole answer is in */
        const DWORD want = (avail < (DWORD)sizeof buf) ? avail : (DWORD)sizeof buf;
        DWORD read = 0;
        if (!::WinHttpReadData(r, buf, want, &read)) { *why = WinErr("the answer broke off", (long)::GetLastError()); return false; }
        if (read == 0) return true;
        answer->append(buf, read);
        if ((int)answer->size() > maxBytes)
        {
            *why = "an answer longer than " + swrouter::Num((long)maxBytes) + " bytes";
            return false;
        }
    }
}

}   /* namespace */

int SsdpSearch(const std::string& homeIp, const std::string& gatewayIp, SsdpFound* f, std::string* why)
{
    *f = SsdpFound();
    unsigned long home = 0;
    if (!swrouter::Ipv4Read(homeIp, &home)) { *why = "no home-network address"; return -1; }
    WsaHold wsa;
    if (!wsa.ok) { *why = "Winsock could not start"; return -1; }
    SOCKET s = BoundUdp(home, why);
    if (s == INVALID_SOCKET) return -1;
    in_addr ifa;
    ifa.s_addr = htonl(home);
    ::setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifa, sizeof ifa);
    const int ttl = swrouter::kSsdpTtl;
    ::setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof ttl);
    sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(swrouter::kSsdpPort);
    to.sin_addr.s_addr = ::inet_addr(swrouter::kSsdpGroup);

    int sent = 0;
    long lastErr = 0;
    for (int i = 0; i < swrouter::kSsdpTargetCount; ++i)
    {
        const std::string m = swrouter::SsdpSearchText(swrouter::SsdpTarget(i));
        if (::sendto(s, m.data(), (int)m.size(), 0, (const sockaddr*)&to, sizeof to) == (int)m.size()) ++sent;
        else lastErr = (long)::WSAGetLastError();
    }
    if (sent == 0)
    {
        *why = WinErr("the search could not be sent (sendto)", lastErr);
        ::closesocket(s);
        return -1;
    }
    const DWORD t0 = ::GetTickCount();
    bool resent = false;
    int errors = 0;
    for (;;)
    {
        const unsigned long elapsed = (unsigned long)(DWORD)(::GetTickCount() - t0);
        if (elapsed >= swrouter::kSsdpWaitMs) break;
        if (!resent && f->location.empty() && elapsed >= swrouter::kSsdpResendMs)
        {
            for (int i = 0; i < swrouter::kSsdpTargetCount; ++i)
            {
                const std::string m = swrouter::SsdpSearchText(swrouter::SsdpTarget(i));
                ::sendto(s, m.data(), (int)m.size(), 0, (const sockaddr*)&to, sizeof to);
            }
            resent = true;
        }
        unsigned long wait = swrouter::kSsdpWaitMs - elapsed;
        if (!resent && f->location.empty() && elapsed < swrouter::kSsdpResendMs && swrouter::kSsdpResendMs - elapsed < wait)
            wait = swrouter::kSsdpResendMs - elapsed;   /* wake for the second send */
        const int r = Readable(s, wait);
        if (r < 0) { *why = WinErr("select", (long)::WSAGetLastError()); break; }
        if (r == 0) continue;
        char buf[2048];
        sockaddr_in from;
        int fromLen = sizeof from;
        const int got = ::recvfrom(s, buf, (int)sizeof buf, 0, (sockaddr*)&from, &fromLen);
        if (got <= 0)
        {
            if (++errors > 8) break;
            continue;
        }
        ++f->datagrams;
        const std::string sender = swrouter::Ipv4Text((unsigned long)ntohl(from.sin_addr.s_addr));
        std::string loc, st, w;
        if (!swrouter::HomeNetworkHost(sender, gatewayIp))
        {
            if (f->unreadable++ == 0) f->firstWhy = "an answer from a device outside the home network";
        }
        else if (!swrouter::SsdpAnswerRead(std::string(buf, (size_t)got), sender, &loc, &st, &w))
        {
            if (f->unreadable++ == 0) f->firstWhy = w;
        }
        else
        {
            const bool gw = !gatewayIp.empty() && sender == gatewayIp;
            if (f->location.empty() || (gw && !f->fromGateway))
            {
                f->location = loc;
                f->target = st;
                f->fromGateway = gw;
            }
            if (f->fromGateway || gatewayIp.empty()) break;   /* the home network's own router answered: stop waiting */
        }
        if (f->datagrams >= swrouter::kSsdpMaxAnswers) break;
    }
    f->ms = (unsigned long)(DWORD)(::GetTickCount() - t0);
    ::closesocket(s);
    return f->location.empty() ? 0 : 1;
}

int HttpAsk(const std::string& url, const std::string& gatewayIp, const char* verb, const std::string& soapAction, const std::string& body, int maxBytes,
            std::string* answer, std::string* why)
{
    answer->clear();
    std::string host, path;
    long port = 0;
    if (!swrouter::HttpUrlRead(url, &host, &port, &path)) { *why = "the URL is not plain http to an IPv4 address"; return 0; }
    if (!swrouter::HomeNetworkHost(host, gatewayIp)) { *why = "the URL names a device outside the home network"; return 0; }
    HINTERNET s = ::WinHttpOpen(swnames::kRouterAgentW, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s == 0) { *why = WinErr("WinHttpOpen", (long)::GetLastError()); return 0; }
    const int step = (int)swrouter::kHttpStepMs;
    ::WinHttpSetTimeouts(s, step, step, step, step);
    const std::wstring wHost = Wide(host), wPath = Wide(path), wVerb = Wide(verb);
    HINTERNET c = ::WinHttpConnect(s, wHost.c_str(), (INTERNET_PORT)port, 0);
    HINTERNET r = (c != 0) ? ::WinHttpOpenRequest(c, wVerb.c_str(), wPath.c_str(), NULL, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : 0;
    DWORD status = 0;
    if (r == 0) *why = WinErr("no connection could be made", (long)::GetLastError());
    else if (SafeRequest(r, why))
    {
        /* the headers must arrive within one step */
        DWORD headerWait = (DWORD)swrouter::kHttpStepMs;
        ::WinHttpSetOption(r, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &headerWait, sizeof headerWait);
        BOOL sentOk = FALSE;
        if (soapAction.empty())
            sentOk = ::WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        else
        {
            const std::wstring headers = L"Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: " + Wide(soapAction) + L"\r\n";
            sentOk = ::WinHttpSendRequest(r, headers.c_str(), (DWORD)-1L, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
        }
        if (!sentOk || !::WinHttpReceiveResponse(r, 0)) *why = WinErr("no answer", (long)::GetLastError());
        else
        {
            DWORD len = sizeof status;
            if (!::WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                       &status, &len, WINHTTP_NO_HEADER_INDEX))
                status = 0;
            if (!ReadBody(r, maxBytes, answer, why))
            {
                answer->clear();
                status = 0;
            }
            else if (status == 0) *why = "an answer with no readable HTTP status";
        }
    }
    if (r != 0) ::WinHttpCloseHandle(r);
    if (c != 0) ::WinHttpCloseHandle(c);
    ::WinHttpCloseHandle(s);
    return (int)status;
}

int UdpAskRouter(const std::string& homeIp, const std::string& gatewayIp, const unsigned char* request, int requestLen,
                 unsigned char* answer, int answerMax, int* answerLen, std::string* why)
{
    *answerLen = 0;
    unsigned long home = 0, gw = 0;
    if (!swrouter::Ipv4Read(homeIp, &home)) { *why = "no home-network address"; return kUdpCannot; }
    if (!swrouter::Ipv4Read(gatewayIp, &gw)) { *why = "the home adapter names no router address"; return kUdpCannot; }
    WsaHold wsa;
    if (!wsa.ok) { *why = "Winsock could not start"; return kUdpCannot; }
    SOCKET s = BoundUdp(home, why);
    if (s == INVALID_SOCKET) return kUdpCannot;
    /* Connected to the router's port: only its answers come back, and Windows reports an ICMP port-unreachable. */
    sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(swrouter::kPmpPort);
    to.sin_addr.s_addr = htonl(gw);
    if (::connect(s, (const sockaddr*)&to, sizeof to) != 0)
    {
        *why = WinErr("connect", (long)::WSAGetLastError());
        ::closesocket(s);
        return kUdpCannot;
    }
    int out = kUdpSilent;
    for (int attempt = 0; attempt < swrouter::kPmpSends && out == kUdpSilent; ++attempt)
    {
        if (::send(s, (const char*)request, requestLen, 0) != requestLen)
        {
            const long e = (long)::WSAGetLastError();
            if (e == WSAECONNRESET) { out = kUdpNobody; break; }
            *why = WinErr("send", e);
            out = kUdpCannot;
            break;
        }
        const DWORD t0 = ::GetTickCount();
        const unsigned long wait = swrouter::PmpWaitMs(attempt);
        for (;;)
        {
            const unsigned long elapsed = (unsigned long)(DWORD)(::GetTickCount() - t0);
            if (elapsed >= wait) break;
            const int r = Readable(s, wait - elapsed);
            if (r < 0) { *why = WinErr("select", (long)::WSAGetLastError()); out = kUdpCannot; break; }
            if (r == 0) continue;
            const int got = ::recv(s, (char*)answer, answerMax, 0);
            if (got > 0) { *answerLen = got; out = kUdpAnswered; break; }
            const long e = (long)::WSAGetLastError();
            if (e == WSAECONNRESET) { out = kUdpNobody; break; }
            if (e == WSAEMSGSIZE) continue;   /* a datagram longer than any answer: not one */
            *why = WinErr("recv", e);
            out = kUdpCannot;
            break;
        }
    }
    ::closesocket(s);
    return out;
}

}   /* namespace net */
}   /* namespace coop */
