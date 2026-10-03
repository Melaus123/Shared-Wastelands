/* src/common/addrlookup.h - THE HOST'S INTERNET ADDRESS FROM ADDRESS-LOOKUP WEBSITES (owner 452).
 *
 * When the home router gives no internet address, the HOSTING window asks these websites - only when the player presses SHOW
 * or COPY - in this order, the first usable answer winning: api.ipify.org (HTTPS), then checkip.amazonaws.com, then
 * ipv4.icanhazip.com (both plain HTTP; the IPv4-only name of icanhazip, whose plain name may answer with an IPv6 address,
 * which is refused). Each answers a plain GET with the asker's address as text. upnp.cpp asks them on a short-lived worker
 * thread of its own (WinHTTP, never the router thread), each step limited to kLookupStepMs, no further website once
 * kLookupDeadlineMs have passed since the press; this file holds the order, the limits and the reading of an answer. The
 * address is never logged; only which website answered, or why none did.
 *
 * Pure: no Windows call. Swept by src/coop-test/test_main.cpp. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include "upnpplan.h"   /* coopupnp::RouterAddressPublic - the one dotted-IPv4 reading, shared with the router's answer */

namespace coopaddr {

struct LookupSite
{
    const char*    name;    /* for the log: which website answered */
    const wchar_t* host;
    const wchar_t* path;
    int            https;   /* 1 = HTTPS on 443, 0 = HTTP on 80 */
};

const int kLookupSiteCount = 3;
inline const LookupSite& LookupSiteAt(int i)
{
    static const LookupSite t[kLookupSiteCount] = {
        { "api.ipify.org",         L"api.ipify.org",         L"/", 1 },
        { "checkip.amazonaws.com", L"checkip.amazonaws.com", L"/", 0 },
        { "ipv4.icanhazip.com",    L"ipv4.icanhazip.com",    L"/", 0 },
    };
    return t[(i >= 0 && i < kLookupSiteCount) ? i : 0];
}

/* Each website's name lookup, connect, send and receive are each given this long (WinHttpSetTimeouts). */
const int kLookupStepMs = 3000;
/* The whole lookup's limit: no further website is asked once this long has passed since the press that started it (a website
   already being asked finishes its own steps). */
const unsigned long kLookupDeadlineMs = 12000;
/* Milliseconds from `startMs` to `nowMs` on the 32-bit millisecond clock (GetTickCount), across its wrap. */
inline unsigned long LookupElapsedMs(unsigned long startMs, unsigned long nowMs) { return (nowMs - startMs) & 0xFFFFFFFFUL; }
/* 1 = the next website may be asked `elapsedMs` after the press; 0 = the deadline has passed. */
inline int LookupMayAskNext(unsigned long elapsedMs) { return elapsedMs < kLookupDeadlineMs ? 1 : 0; }
/* The most of an answer read: an IPv4 address and its line end fit with room to spare; anything longer is not one. */
const int kLookupMaxBody = 64;

/* An answer's body -> the address. Spaces, tabs and line ends around it are dropped; what is left must be a dotted IPv4
   address (four numbers 0-255, nothing else) that is an internet one - a private, shared, loopback or reserved address is
   no use to a player elsewhere. true: *ip = the address rebuilt from its four numbers. false: *why says why in words,
   never the address. */
inline bool LookupAnswer(const std::string& body, std::string* ip, std::string* why)
{
    size_t a = 0, b = body.size();
    while (a < b && (body[a] == ' ' || body[a] == '\t' || body[a] == '\r' || body[a] == '\n')) ++a;
    while (b > a && (body[b - 1] == ' ' || body[b - 1] == '\t' || body[b - 1] == '\r' || body[b - 1] == '\n')) --b;
    const std::string t = body.substr(a, b - a);
    if (t.empty()) { if (why) *why = "an empty answer"; return false; }
    if (t.size() > 15) { if (why) *why = "not an IPv4 address"; return false; }
    std::string kind, clean;
    const bool pub = coopupnp::RouterAddressPublic(t, &kind, &clean);
    if (kind == "unreadable") { if (why) *why = "not an IPv4 address"; return false; }
    if (!pub) { if (why) *why = "a " + kind + " address, not an internet one"; return false; }
    if (ip) *ip = clean;
    return true;
}

}   /* namespace coopaddr */
