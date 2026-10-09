/* src/common/routerwire.h - THE MOD'S OWN ROUTER ASK: every text and byte it sends to the home router, and the reading of
 * every answer, as pure functions the offline suite checks against sample answers.
 *
 * Three ways of asking the home router to forward the world server's UDP port (src/common/upnpplan.h orders them;
 * src/coop-plugin/upnp.cpp runs them; src/coop-plugin/net/routerask.cpp only sends and receives):
 *   UPnP IGD (UPnP Device Architecture 1.1; InternetGatewayDevice:1 / :2)
 *     - SSDP: an M-SEARCH datagram to the multicast address 239.255.255.250:1900, sent from the home adapter's own
 *       address; a router answers "HTTP/1.1 200 OK" with a LOCATION header naming its device description.
 *     - The device description (XML, fetched over HTTP) names the WANIPConnection / WANPPPConnection service and its
 *       control URL.
 *     - SOAP over HTTP POST to that control URL: GetSpecificPortMappingEntry (what the router already forwards on the
 *       port), AddPortMapping (with a time-limited lease), DeletePortMapping, GetExternalIPAddress.  A refusal is an
 *       HTTP 500 carrying a UPnPError errorCode (718 ConflictInMappingEntry, 725 OnlyPermanentLeasesSupported, ...).
 *   PCP (RFC 6887): one 60-byte MAP request to the router's UDP port 5351; the answer carries a result code, the granted
 *     lifetime and the external port.  A lifetime of 0 removes the entry.
 *   NAT-PMP (RFC 6886): the older protocol on the same port 5351: a 12-byte mapping request, a 16-byte answer; a 2-byte
 *     request asks the router's internet address.
 *
 * STRICT AND SMALL: an answer that is not exactly what the protocol says is refused, never guessed at.  Only a device on
 * the home network is listened to (HomeNetworkHost: a private or shared-range address, or the adapter's own router); a
 * LOCATION or a control URL must be plain http:// to a dotted IPv4 address and must name the device that answered, and
 * the HTTP requests follow no redirect (net/routerask.cpp) - no answer can send the mod's requests to another computer.
 *
 * PURE: no Windows.h, no sockets, no logging, no globals.  C++03 (VS2010 v100).
 */
#ifndef SW_COMMON_ROUTERWIRE_H
#define SW_COMMON_ROUTERWIRE_H

#include <string>
#include <vector>

namespace swrouter {

/* ---------------------------------------------------------------------------------------------------------------- */
/* Small text helpers.                                                                                              */
/* ---------------------------------------------------------------------------------------------------------------- */

inline std::string Lower(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}

inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

inline std::string Num(long v)
{
    if (v == 0) return "0";
    const bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1ul : (unsigned long)v;
    char buf[24];
    int n = 0;
    while (u > 0 && n < 23) { buf[n++] = (char)('0' + (int)(u % 10ul)); u /= 10ul; }
    std::string s(neg ? "-" : "");
    while (n > 0) s += buf[--n];
    return s;
}

/* A whole decimal number of 1..10 digits, nothing else (spaces around it allowed).  false otherwise. */
inline bool ReadNum(const std::string& text, long* out)
{
    const std::string t = Trim(text);
    if (t.empty() || t.size() > 10) return false;
    unsigned long v = 0;
    for (size_t i = 0; i < t.size(); ++i)
    {
        if (t[i] < '0' || t[i] > '9') return false;
        v = v * 10ul + (unsigned long)(t[i] - '0');
    }
    if (v > 2147483647ul) return false;
    *out = (long)v;
    return true;
}

/* Dotted IPv4 "a.b.c.d", each part 0..255 with 1..3 digits; *out = (a<<24)|(b<<16)|(c<<8)|d. */
inline bool Ipv4Read(const std::string& text, unsigned long* out)
{
    unsigned long v = 0, part = 0;
    int parts = 0, digits = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const char c = (i < text.size()) ? text[i] : '.';
        if (c >= '0' && c <= '9')
        {
            if (++digits > 3) return false;
            part = part * 10ul + (unsigned long)(c - '0');
        }
        else if (c == '.')
        {
            if (digits == 0 || part > 255ul || parts >= 4) return false;
            v = (v << 8) | part;
            ++parts;
            part = 0;
            digits = 0;
        }
        else return false;
    }
    if (parts != 4) return false;
    *out = v & 0xFFFFFFFFul;
    return true;
}

inline std::string Ipv4Text(unsigned long a)
{
    return Num((long)((a >> 24) & 0xFFul)) + "." + Num((long)((a >> 16) & 0xFFul)) + "." + Num((long)((a >> 8) & 0xFFul))
         + "." + Num((long)(a & 0xFFul));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* SSDP: finding the router.                                                                                         */
/* ---------------------------------------------------------------------------------------------------------------- */

const char* const    kSsdpGroup = "239.255.255.250";
const unsigned short kSsdpPort  = 1900;
/* MX: the most seconds a device may wait before it answers a search (UDA 1.1 section 1.3.2). */
const int            kSsdpMx    = 2;
/* THE SEARCH'S WAIT IS A CAP, NOT A READINESS GUESS.  A device that answers does so within MX seconds of the search (UDA
   1.1), so after MX + 1 seconds no answer is still coming.  The router thread stops waiting the moment the home network's
   own router (the adapter's gateway) answers; it waits out the cap only when that router never answers, and then tries
   the first other device on the home network that answered as an internet gateway. */
const unsigned int   kSsdpWaitMs   = (unsigned int)(kSsdpMx + 1) * 1000u;
/* The search goes out a second time this long after the first when nothing has answered yet: multicast UDP may be lost. */
const unsigned int   kSsdpResendMs = 1000u;
/* At most this many answers are read in one search (one per device and search target is usual). */
const int            kSsdpMaxAnswers = 32;
/* Multicast hops: UDA 1.1 asks for 2, enough to reach the router from this computer and no further. */
const int            kSsdpTtl = 2;

/* What the search asks for: the router device (both versions) and the two services that forward ports. */
const int kSsdpTargetCount = 4;
inline const char* SsdpTarget(int i)
{
    switch (i)
    {
    case 0:  return "urn:schemas-upnp-org:device:InternetGatewayDevice:1";
    case 1:  return "urn:schemas-upnp-org:device:InternetGatewayDevice:2";
    case 2:  return "urn:schemas-upnp-org:service:WANIPConnection:1";
    default: return "urn:schemas-upnp-org:service:WANPPPConnection:1";
    }
}

/* Is `addr` (dotted) a device on the home network?  A private address (10/8, 172.16/12, 192.168/16), the providers'
   shared range (100.64/10, a router behind carrier-grade NAT hands those out at home too), or the home adapter's own
   router `gateway` (dotted, may be "").  Anything else - a public address, loopback, multicast, unreadable - is not. */
inline bool HomeNetworkHost(const std::string& addr, const std::string& gateway)
{
    unsigned long a = 0;
    if (!Ipv4Read(addr, &a)) return false;
    if (!gateway.empty() && addr == gateway) return true;
    const unsigned long top = (a >> 24) & 0xFFul, second = (a >> 16) & 0xFFul;
    if (top == 10) return true;
    if (top == 172 && second >= 16 && second <= 31) return true;
    if (top == 192 && second == 168) return true;
    if (top == 100 && second >= 64 && second <= 127) return true;
    return false;
}

/* The M-SEARCH datagram for one target. */
inline std::string SsdpSearchText(const std::string& target)
{
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: " + Num((long)kSsdpMx) + "\r\n"
           "ST: " + target + "\r\n"
           "\r\n";
}

/* One header's value from an HTTP-shaped text (the first line is the status line and is skipped); the name is compared
   without case.  false when the header is absent. */
inline bool HeaderValue(const std::string& text, const std::string& name, std::string* value)
{
    const std::string want = Lower(name);
    size_t pos = text.find('\n');
    while (pos != std::string::npos && pos + 1 < text.size())
    {
        const size_t start = pos + 1;
        size_t end = text.find('\n', start);
        const std::string line = text.substr(start, (end == std::string::npos ? text.size() : end) - start);
        const size_t colon = line.find(':');
        if (colon != std::string::npos && Lower(Trim(line.substr(0, colon))) == want)
        {
            *value = Trim(line.substr(colon + 1));
            return true;
        }
        if (Trim(line).empty()) return false;   /* the end of the headers */
        pos = end;
    }
    return false;
}

/* A plain http:// URL to a dotted IPv4 address: *host the address text, *port (80 when not given), *path ("/" when
   none).  https, a name instead of an address, user info, IPv6 and a port outside 1..65535 are all refused. */
inline bool HttpUrlRead(const std::string& url, std::string* host, long* port, std::string* path)
{
    const std::string u = Trim(url);
    if (u.size() < 8 || Lower(u.substr(0, 7)) != "http://") return false;
    const size_t hostStart = 7;
    size_t hostEnd = u.find('/', hostStart);
    if (hostEnd == std::string::npos) hostEnd = u.size();
    const std::string authority = u.substr(hostStart, hostEnd - hostStart);
    if (authority.empty() || authority.find('@') != std::string::npos || authority.find('[') != std::string::npos) return false;
    std::string h = authority;
    long p = 80;
    const size_t colon = authority.find(':');
    if (colon != std::string::npos)
    {
        h = authority.substr(0, colon);
        if (!ReadNum(authority.substr(colon + 1), &p) || p < 1 || p > 65535) return false;
        if (authority.substr(colon + 1).find(' ') != std::string::npos) return false;
    }
    unsigned long a = 0;
    if (!Ipv4Read(h, &a)) return false;
    *host = Ipv4Text(a);
    *port = p;
    *path = (hostEnd < u.size()) ? u.substr(hostEnd) : std::string("/");
    if (path->find(' ') != std::string::npos || path->find('\r') != std::string::npos || path->find('\n') != std::string::npos) return false;
    return true;
}

/* AN ANSWER TO THE SEARCH.  text: the datagram; sender: the dotted address it came from.  A router's answer is
   "HTTP/1.1 200 OK" with a LOCATION header naming an http:// URL ON THE ANSWERING DEVICE ITSELF.  *location / *target
   (may be 0) are filled when true; *why (may be 0) says what is wrong when false. */
inline bool SsdpAnswerRead(const std::string& text, const std::string& sender, std::string* location, std::string* target, std::string* why)
{
    std::string w;
    std::string loc, st, host, path;
    long port = 0;
    const size_t eol = text.find('\n');
    const std::string first = Trim(text.substr(0, eol == std::string::npos ? text.size() : eol));
    if (first.size() < 12 || first.substr(0, 7) != "HTTP/1." || first.substr(8, 4) != " 200" || (first.size() > 12 && first[12] != ' '))
        w = "not a 200 answer";
    else if (!HeaderValue(text, "LOCATION", &loc) || loc.empty()) w = "no LOCATION";
    else if (!HttpUrlRead(loc, &host, &port, &path)) w = "a LOCATION that is not plain http to an IPv4 address";
    else if (host != sender) w = "a LOCATION on another computer than the one that answered";
    if (!w.empty())
    {
        if (why != 0) *why = w;
        return false;
    }
    HeaderValue(text, "ST", &st);
    if (location != 0) *location = Trim(loc);
    if (target != 0) *target = st;
    return true;
}

/* HTTP to the router (the description and the SOAP calls).  THE STEP TIMEOUT IS A CAP, NOT A READINESS GUESS: a router
   on the home network answers within a fraction of a second; one that has not connected, taken the request or answered
   within kHttpStepMs for a step is not answering, and the route ends there.  The answer is read as soon as it arrives.
   THE WHOLE REQUEST IS CAPPED TOO: the headers must arrive within kHttpStepMs of the request (WinHTTP's response-header
   timeout), and the body is read only until kHttpWholeMs after the headers arrived: the clock is checked before every
   read, each read takes only what has already arrived, and each wait for more is cut to the time left (HttpBodyWaitMs)
   - a device that trickles its answer a byte at a time is cut off there.  One request therefore holds the router thread
   for at most connect + send + headers (one step each) + kHttpWholeMs, about 15 s, however the device behaves.
   An answer longer than the limit is refused unread (a device description is a few kilobytes, a SOAP answer less). */
const unsigned int kHttpStepMs   = 3000u;
const unsigned int kHttpWholeMs  = 6000u;

/* How long the next wait for more of an HTTP answer's body may last, `spentMs` after its headers arrived: the time left
   of kHttpWholeMs, never more than kHttpStepMs.  0 = the body's time is up (the answer is refused as late). */
inline unsigned long HttpBodyWaitMs(unsigned long spentMs)
{
    if (spentMs >= kHttpWholeMs) return 0ul;
    const unsigned long left = (unsigned long)kHttpWholeMs - spentMs;
    return (left < (unsigned long)kHttpStepMs) ? left : (unsigned long)kHttpStepMs;
}
const int          kMaxDescBytes = 65536;
const int          kMaxSoapBytes = 16384;

/* ---------------------------------------------------------------------------------------------------------------- */
/* XML: a small, strict element reader for the device description and SOAP answers.                                   */
/* ---------------------------------------------------------------------------------------------------------------- */

inline std::string XmlUnescape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&') { o += s[i]; continue; }
        const size_t semi = s.find(';', i);
        const std::string ent = (semi == std::string::npos) ? std::string() : s.substr(i, semi - i + 1);
        if (ent == "&amp;")       o += '&';
        else if (ent == "&lt;")   o += '<';
        else if (ent == "&gt;")   o += '>';
        else if (ent == "&quot;") o += '"';
        else if (ent == "&apos;") o += '\'';
        else { o += s[i]; continue; }
        i = semi;
    }
    return o;
}

inline std::string XmlEscape(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '&')      o += "&amp;";
        else if (s[i] == '<') o += "&lt;";
        else if (s[i] == '>') o += "&gt;";
        else if (s[i] == '"') o += "&quot;";
        else                  o += s[i];
    }
    return o;
}

/* The next element named `local` (any namespace prefix, e.g. <u:AddPortMappingResponse> for "AddPortMappingResponse")
   at or after `from`: *inner = its raw content between the tags ("" for <x/>), *after = the position just past its end
   tag.  false when there is none, or its end tag is missing. */
inline bool XmlNext(const std::string& doc, const std::string& local, size_t from, std::string* inner, size_t* after)
{
    size_t pos = from;
    while (pos < doc.size())
    {
        const size_t lt = doc.find('<', pos);
        if (lt == std::string::npos || lt + 1 >= doc.size()) return false;
        if (doc[lt + 1] == '/' || doc[lt + 1] == '?' || doc[lt + 1] == '!') { pos = lt + 1; continue; }
        size_t nameEnd = lt + 1;
        while (nameEnd < doc.size() && doc[nameEnd] != '>' && doc[nameEnd] != '/' && doc[nameEnd] != ' '
               && doc[nameEnd] != '\t' && doc[nameEnd] != '\r' && doc[nameEnd] != '\n') ++nameEnd;
        std::string name = doc.substr(lt + 1, nameEnd - lt - 1);
        const size_t colon = name.find(':');
        if (colon != std::string::npos) name = name.substr(colon + 1);
        const size_t gt = doc.find('>', lt);
        if (gt == std::string::npos) return false;
        if (name != local) { pos = gt + 1; continue; }
        if (doc[gt - 1] == '/')
        {
            if (inner != 0) inner->clear();
            if (after != 0) *after = gt + 1;
            return true;
        }
        /* the matching end tag: </local> or </prefix:local> */
        size_t scan = gt + 1;
        while (scan < doc.size())
        {
            const size_t end = doc.find("</", scan);
            if (end == std::string::npos) return false;
            const size_t endGt = doc.find('>', end);
            if (endGt == std::string::npos) return false;
            std::string endName = Trim(doc.substr(end + 2, endGt - end - 2));
            const size_t c2 = endName.find(':');
            if (c2 != std::string::npos) endName = endName.substr(c2 + 1);
            if (endName == local)
            {
                if (inner != 0) *inner = doc.substr(gt + 1, end - gt - 1);
                if (after != 0) *after = endGt + 1;
                return true;
            }
            scan = endGt + 1;
        }
        return false;
    }
    return false;
}

/* The first element `local`'s text, unescaped and trimmed; "" when absent. */
inline std::string XmlValue(const std::string& doc, const std::string& local)
{
    std::string inner;
    size_t after = 0;
    if (!XmlNext(doc, local, 0, &inner, &after)) return std::string();
    return Trim(XmlUnescape(inner));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* The device description: which service forwards ports, and where its control URL is.                             */
/* ---------------------------------------------------------------------------------------------------------------- */

/* The services that forward ports, best first: IGD2's WANIPConnection:2, then WANIPConnection:1, then WANPPPConnection:1
   (a router that dials the provider itself).  -1 = not one of them. */
inline int ServiceRank(const std::string& serviceType)
{
    const std::string t = Trim(serviceType);
    if (t == "urn:schemas-upnp-org:service:WANIPConnection:2")  return 0;
    if (t == "urn:schemas-upnp-org:service:WANIPConnection:1")  return 1;
    if (t == "urn:schemas-upnp-org:service:WANPPPConnection:1") return 2;
    return -1;
}

/* A URL named in the description, made absolute.  An absolute http:// URL stays as it is; any other is a path on the
   description's own device (base: URLBase when the description gives one, else the description's own URL).  A path
   without a leading "/" is taken from the device's root, as routers write it. */
inline bool UrlResolve(const std::string& base, const std::string& ref, std::string* out)
{
    const std::string r = Trim(ref);
    if (r.empty()) return false;
    if (r.size() >= 7 && Lower(r.substr(0, 7)) == "http://") { *out = r; return true; }
    if (r.find("://") != std::string::npos) return false;
    std::string host, path;
    long port = 0;
    if (!HttpUrlRead(base, &host, &port, &path)) return false;
    *out = "http://" + host + ":" + Num(port) + (r[0] == '/' ? r : "/" + r);
    return true;
}

/* THE DEVICE DESCRIPTION.  descUrl: where it was fetched from (the search answer's LOCATION).  true = a port-forwarding
   service was found: *controlUrl (absolute, on the same device as descUrl) and *serviceType.  *why (may be 0) says what
   was missing when false. */
inline bool IgdControlRead(const std::string& xml, const std::string& descUrl, std::string* controlUrl, std::string* serviceType, std::string* why)
{
    std::string base = XmlValue(xml, "URLBase");
    std::string h0, p0, h1, p1;
    long port0 = 0, port1 = 0;
    if (base.empty() || !HttpUrlRead(base, &h0, &port0, &p0)) base = descUrl;
    if (!HttpUrlRead(descUrl, &h1, &port1, &p1))
    {
        if (why != 0) *why = "the description's own URL is not plain http to an IPv4 address";
        return false;
    }
    int bestRank = 99;
    std::string bestType, bestUrl;
    bool sawService = false, badUrl = false;
    size_t pos = 0;
    std::string block;
    size_t after = 0;
    while (XmlNext(xml, "service", pos, &block, &after))
    {
        pos = after;
        sawService = true;
        const std::string type = XmlValue(block, "serviceType");
        const int rank = ServiceRank(type);
        if (rank < 0 || rank >= bestRank) continue;
        std::string url, h, p;
        long port = 0;
        if (!UrlResolve(base, XmlValue(block, "controlURL"), &url) || !HttpUrlRead(url, &h, &port, &p) || h != h1)
        {
            badUrl = true;
            continue;
        }
        bestRank = rank;
        bestType = Trim(type);
        bestUrl = url;
    }
    if (bestRank == 99)
    {
        if (why != 0)
            *why = !sawService ? "the description lists no service"
                 : badUrl      ? "the port-forwarding service's control URL is unreadable or on another computer"
                               : "the description lists no port-forwarding service (WANIPConnection / WANPPPConnection)";
        return false;
    }
    *controlUrl = bestUrl;
    *serviceType = bestType;
    return true;
}

/* A short name for a service type, for the log ("WANIPConnection:1"). */
inline std::string ServiceShort(const std::string& serviceType)
{
    const std::string pre = "urn:schemas-upnp-org:service:";
    return (serviceType.compare(0, pre.size(), pre) == 0) ? serviceType.substr(pre.size()) : serviceType;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* SOAP: the port-forwarding calls.                                                                                  */
/* ---------------------------------------------------------------------------------------------------------------- */

/* The SOAPAction header's value: "<serviceType>#<action>" in quotes. */
inline std::string SoapActionHeader(const std::string& serviceType, const std::string& action)
{
    return "\"" + serviceType + "#" + action + "\"";
}

/* The SOAP body: `n` arguments, names[i] = values[i] (values escaped here), in the order given - routers read them in
   the order the service description lists them. */
inline std::string SoapBody(const std::string& serviceType, const std::string& action, const char* const* names,
                            const std::string* values, int n)
{
    std::string b = "<?xml version=\"1.0\"?>\r\n"
                    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                    "<s:Body><u:" + action + " xmlns:u=\"" + serviceType + "\">";
    for (int i = 0; i < n; ++i) b += std::string("<") + names[i] + ">" + XmlEscape(values[i]) + "</" + names[i] + ">";
    b += "</u:" + action + "></s:Body></s:Envelope>\r\n";
    return b;
}

/* AddPortMapping: forward external `port` (`proto`) to `client`:`port`, enabled, described, for `leaseSeconds` (0 =
   until removed).  NewRemoteHost empty = from anyone. */
inline std::string SoapAddPortMapping(const std::string& serviceType, long port, const std::string& proto, const std::string& client,
                                      const std::string& description, long leaseSeconds)
{
    static const char* const names[8] = { "NewRemoteHost", "NewExternalPort", "NewProtocol", "NewInternalPort",
                                          "NewInternalClient", "NewEnabled", "NewPortMappingDescription", "NewLeaseDuration" };
    const std::string values[8] = { std::string(), Num(port), proto, Num(port), client, "1", description, Num(leaseSeconds) };
    return SoapBody(serviceType, "AddPortMapping", names, values, 8);
}

inline std::string SoapDeletePortMapping(const std::string& serviceType, long port, const std::string& proto)
{
    static const char* const names[3] = { "NewRemoteHost", "NewExternalPort", "NewProtocol" };
    const std::string values[3] = { std::string(), Num(port), proto };
    return SoapBody(serviceType, "DeletePortMapping", names, values, 3);
}

inline std::string SoapGetSpecificEntry(const std::string& serviceType, long port, const std::string& proto)
{
    static const char* const names[3] = { "NewRemoteHost", "NewExternalPort", "NewProtocol" };
    const std::string values[3] = { std::string(), Num(port), proto };
    return SoapBody(serviceType, "GetSpecificPortMappingEntry", names, values, 3);
}

inline std::string SoapGetExternalAddress(const std::string& serviceType)
{
    return SoapBody(serviceType, "GetExternalIPAddress", 0, 0, 0);
}

/* What a SOAP call came to. */
enum { kSoapOk = 0, kSoapFault = 1, kSoapUnreadable = 2 };

/* THE ANSWER TO A SOAP CALL.  httpStatus: the HTTP status (0 = no answer).  A success is HTTP 200 carrying
   <actionResponse>; a refusal is HTTP 500 carrying a UPnPError <errorCode> (*fault, with its <errorDescription> in
   *faultText); anything else is unreadable (*fault -1). */
inline int SoapAnswerRead(int httpStatus, const std::string& body, const std::string& action, int* fault, std::string* faultText)
{
    *fault = -1;
    if (faultText != 0) faultText->clear();
    std::string inner;
    size_t after = 0;
    if (httpStatus == 200 && XmlNext(body, action + "Response", 0, &inner, &after)) return kSoapOk;
    if (httpStatus == 500)
    {
        long code = 0;
        if (ReadNum(XmlValue(body, "errorCode"), &code))
        {
            *fault = (int)code;
            if (faultText != 0) *faultText = XmlValue(body, "errorDescription");
            return kSoapFault;
        }
    }
    return kSoapUnreadable;
}

/* The IGD error codes this mod meets, in words for the log. */
inline const char* FaultWord(int code)
{
    switch (code)
    {
    case 401: return "InvalidAction";
    case 402: return "InvalidArgs";
    case 501: return "ActionFailed";
    case 606: return "ActionNotAuthorized";
    case 714: return "NoSuchEntryInArray";
    case 715: return "WildCardNotPermittedInSrcIP";
    case 716: return "WildCardNotPermittedInExtPort";
    case 718: return "ConflictInMappingEntry";
    case 724: return "SamePortValuesRequired";
    case 725: return "OnlyPermanentLeasesSupported";
    case 726: return "RemoteHostOnlySupportsWildcard";
    case 727: return "ExternalPortOnlySupportsWildcard";
    case 728: return "NoPortMapsAvailable";
    case 729: return "ConflictWithOtherMechanisms";
    case 732: return "WildCardNotPermittedInIntPort";
    default:  return "other";
    }
}

/* What the router thread does after AddPortMapping was refused with `fault`, having asked for `leaseAsked` seconds:
   725 to a time-limited ask -> ask again for a permanent entry (lease 0); 718 -> the port is forwarded to another
   computer (left as it is, never overwritten); anything else -> this route is refused (the next route is tried). */
enum { kAddRefused = 0, kAddRetryPermanent = 1, kAddHeldElsewhere = 2 };
inline int AddFaultNext(int fault, long leaseAsked)
{
    if (fault == 725 && leaseAsked > 0) return kAddRetryPermanent;
    if (fault == 718) return kAddHeldElsewhere;
    return kAddRefused;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* NAT-PMP (RFC 6886) and PCP (RFC 6887): UDP to the router's port 5351.                                             */
/* ---------------------------------------------------------------------------------------------------------------- */

const unsigned short kPmpPort = 5351;
/* THE RETRY SCHEDULE IS A CAP, NOT A READINESS GUESS.  (RFC 6887 8.1.1 starts PCP's retries at 3 s because its client is a
   long-lived background service retrying for minutes; this ask is one bounded attempt at the home network's own router,
   which answers in milliseconds, so it uses RFC 6886's schedule for the same port - four datagrams in all.)  A request goes out again after 250 ms, then 500, 1000 and 2000 ms
   without an answer (RFC 6886 3.1's doubling from 250 ms, stopped after four sends instead of nine): a router on the home
   network answers within milliseconds, so the router thread moves on to the next route after 3.75 s of silence.  The
   first answer ends the wait at once, and "nothing listens on that port" (an ICMP port-unreachable) ends it at once too. */
const int kPmpSends = 4;
inline unsigned int PmpWaitMs(int send) { return 250u << (unsigned int)(send < 0 ? 0 : (send > 6 ? 6 : send)); }

inline void Put16(unsigned char* p, unsigned long v) { p[0] = (unsigned char)((v >> 8) & 0xFF); p[1] = (unsigned char)(v & 0xFF); }
inline void Put32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFF); p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);  p[3] = (unsigned char)(v & 0xFF);
}
inline unsigned long Get16(const unsigned char* p) { return ((unsigned long)p[0] << 8) | (unsigned long)p[1]; }
inline unsigned long Get32(const unsigned char* p)
{
    return (((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | (unsigned long)p[3]) & 0xFFFFFFFFul;
}

/* NAT-PMP: the address request (2 bytes) and the UDP mapping request (12 bytes).  A mapping request with lifetime 0 and
   external port 0 removes the entry (RFC 6886 3.4). */
const int kPmpAddrRequestSize = 2;
const int kPmpMapRequestSize  = 12;
inline void PmpAddrRequest(unsigned char out[2]) { out[0] = 0; out[1] = 0; }
inline void PmpMapRequest(unsigned char out[12], long internalPort, long externalPort, unsigned long lifetime)
{
    out[0] = 0;   /* version 0 */
    out[1] = 1;   /* opcode 1: map UDP */
    out[2] = 0; out[3] = 0;
    Put16(out + 4, (unsigned long)internalPort);
    Put16(out + 6, (unsigned long)externalPort);
    Put32(out + 8, lifetime);
}

/* NAT-PMP's result codes. */
inline const char* PmpResultWord(unsigned long r)
{
    switch (r)
    {
    case 0:  return "success";
    case 1:  return "unsupported version";
    case 2:  return "not authorized (port forwarding is switched off on the router, or another request holds the entry)";
    case 3:  return "network failure (the router has no internet address yet)";
    case 4:  return "out of resources";
    case 5:  return "unsupported opcode";
    default: return "unknown result";
    }
}

/* The answer to the address request: 12 bytes, version 0, opcode 128.  true = readable (*result; *ip when result 0). */
inline bool PmpAddrAnswerRead(const unsigned char* b, int len, unsigned long* result, unsigned long* ip)
{
    if (b == 0 || len < 12 || b[0] != 0 || b[1] != 128) return false;
    *result = Get16(b + 2);
    *ip = Get32(b + 8);
    return true;
}

/* The answer to a UDP mapping request: 16 bytes, version 0, opcode 129.  true = readable (*result, and when result 0 the
   internal port it is for, the external port and the lifetime the router granted). */
inline bool PmpMapAnswerRead(const unsigned char* b, int len, unsigned long* result, long* internalPort, long* externalPort, unsigned long* lifetime)
{
    if (b == 0 || len < 16 || b[0] != 0 || b[1] != 129) return false;
    *result = Get16(b + 2);
    *internalPort = (long)Get16(b + 8);
    *externalPort = (long)Get16(b + 10);
    *lifetime = Get32(b + 12);
    return true;
}

/* PCP: the MAP request (60 bytes: the 24-byte common header, then the 36-byte MAP body).  client: this computer's home
   address (the router checks it against the request's source).  nonce: 12 bytes the router echoes; the same nonce renews
   or (lifetime 0) removes the entry.  Protocol 17 = UDP.  The suggested external address is left empty (any). */
const int kPcpMapSize = 60;
inline void PcpMapRequest(unsigned char out[60], unsigned long client, const unsigned char nonce[12], long internalPort,
                          long externalPort, unsigned long lifetime)
{
    for (int i = 0; i < 60; ++i) out[i] = 0;
    out[0] = 2;   /* version 2 */
    out[1] = 1;   /* R = 0 (request), opcode 1 = MAP */
    Put32(out + 4, lifetime);
    out[18] = 0xFF; out[19] = 0xFF;   /* client address as IPv4-mapped IPv6: ::ffff:a.b.c.d */
    Put32(out + 20, client);
    for (int i = 0; i < 12; ++i) out[24 + i] = nonce[i];
    out[36] = 17;
    Put16(out + 40, (unsigned long)internalPort);
    Put16(out + 42, (unsigned long)externalPort);
    out[54] = 0xFF; out[55] = 0xFF;   /* suggested external address ::ffff:0.0.0.0 (any) */
}

/* PCP's result codes (RFC 6887 7.4). */
inline const char* PcpResultWord(unsigned long r)
{
    switch (r)
    {
    case 0:  return "success";
    case 1:  return "unsupported version";
    case 2:  return "not authorized (port forwarding is switched off on the router, or another request holds the entry)";
    case 3:  return "malformed request";
    case 4:  return "unsupported opcode";
    case 5:  return "unsupported option";
    case 6:  return "malformed option";
    case 7:  return "network failure";
    case 8:  return "no resources";
    case 9:  return "unsupported protocol";
    case 10: return "user over quota";
    case 11: return "cannot provide that external port";
    case 12: return "address mismatch (the router sees another address for this computer)";
    case 13: return "excessive remote peers";
    default: return "unknown result";
    }
}

/* PCP'S NONCE, made from `seed` (this computer's name, its home address and the port): the same computer asking for the same
   port always sends the same nonce, so a game started again after a crash renews or removes the entry its earlier
   session made (a router refuses a different nonce for an existing entry with NOT_AUTHORIZED, RFC 6887 11.5).  Two
   64-bit FNV-1a hashes with different starting values give the 12 bytes.  It is not a secret: PCP's nonce guards
   against senders off the home network, which cannot reach the router's port 5351 at all. */
inline void PcpNonceFrom(const std::string& seed, unsigned char out[12])
{
    unsigned long long h1 = 14695981039346656037ULL, h2 = 1099511628211ULL ^ 0x5357524F55544552ULL;
    for (size_t i = 0; i < seed.size(); ++i)
    {
        h1 = (h1 ^ (unsigned char)seed[i]) * 1099511628211ULL;
        h2 = (h2 ^ (unsigned char)seed[i]) * 1099511628211ULL;
    }
    for (int i = 0; i < 8; ++i) out[i] = (unsigned char)((h1 >> (56 - 8 * i)) & 0xFF);
    for (int i = 0; i < 4; ++i) out[8 + i] = (unsigned char)((h2 >> (56 - 8 * i)) & 0xFF);
}

/* A character of an address as LogSafeText reads one: a hex digit, a dot or a colon. */
inline bool LogSafeAddrChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '.' || c == ':';
}

/* A TEXT A ROUTER SUPPLIED, MADE SAFE FOR THE LOG (an entry's description may name another device and its address, carry
   line breaks or be kilobytes long): control characters become spaces; any run of hex digits, dots and colons with two
   or more colons (an IPv6 address, "::" forms and IPv4-mapped ones included, with a trailing %zone) becomes "<address>";
   inside any other such run, each run of digits and dots with two or more dots (an IPv4 address, or anything that looks
   like one) becomes "<address>"; and the result is cut to `maxLen` bytes with "..." after it, never inside a multi-byte
   UTF-8 character (the cut moves back to that character's start). */
inline std::string LogSafeText(const std::string& s, size_t maxLen)
{
    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        const char c = s[i];
        if (LogSafeAddrChar(c))
        {
            size_t j = i;
            int colons = 0;
            while (j < s.size() && LogSafeAddrChar(s[j])) { if (s[j] == ':') ++colons; ++j; }
            if (colons >= 2)
            {
                out += "<address>";
                if (j < s.size() && s[j] == '%')
                {
                    ++j;
                    while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || (s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z')
                                            || s[j] == '_' || s[j] == '-' || s[j] == '.')) ++j;
                }
                i = j;
                continue;
            }
            size_t k = i;
            while (k < j)
            {
                if ((s[k] >= '0' && s[k] <= '9') || s[k] == '.')
                {
                    size_t m = k;
                    int dots = 0;
                    while (m < j && ((s[m] >= '0' && s[m] <= '9') || s[m] == '.')) { if (s[m] == '.') ++dots; ++m; }
                    out += (dots >= 2) ? std::string("<address>") : s.substr(k, m - k);
                    k = m;
                }
                else out += s[k++];
            }
            i = j;
            continue;
        }
        out += ((unsigned char)c < 32 || c == 127) ? ' ' : c;
        ++i;
    }
    if (out.size() > maxLen)
    {
        size_t n = maxLen;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80) --n;   /* out[n] continues a character: cut before its start */
        out = out.substr(0, n) + "...";
    }
    return out;
}

/* An answer on port 5351 to a PCP request that is NAT-PMP's instead: version 0 (a router that speaks only NAT-PMP answers
   "unsupported version" in its own format, RFC 6887 9). */
inline bool PcpAnswerIsPmp(const unsigned char* b, int len) { return b != 0 && len >= 2 && b[0] == 0; }

/* The answer to a PCP MAP request: version 2, R = 1 with opcode MAP (0x81), at least 60 bytes, OUR nonce, UDP.  true =
   readable (*result, *lifetime; when result 0 also *externalPort and *externalIp - an IPv4-mapped address, 0 when the
   router gave another kind). */
inline bool PcpMapAnswerRead(const unsigned char* b, int len, const unsigned char nonce[12], unsigned long* result,
                             unsigned long* lifetime, long* externalPort, unsigned long* externalIp)
{
    if (b == 0 || len < 60 || b[0] != 2 || b[1] != 0x81) return false;
    for (int i = 0; i < 12; ++i) if (b[24 + i] != nonce[i]) return false;
    if (b[36] != 17) return false;
    *result = (unsigned long)b[3];
    *lifetime = Get32(b + 4);
    *externalPort = (long)Get16(b + 42);
    bool mapped = true;
    for (int i = 44; i < 54; ++i) if (b[i] != 0) mapped = false;
    if (b[54] != 0xFF || b[55] != 0xFF) mapped = false;
    *externalIp = mapped ? Get32(b + 56) : 0ul;
    return true;
}

}   /* namespace swrouter */

#endif
