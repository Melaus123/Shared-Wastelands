#pragma once
/* THE MOD'S OWN ROUTER ASK - only the sending and receiving: an SSDP search, an HTTP request to the router, a UDP
   datagram to the router's port 5351.  What is sent and how answers are read is src/common/routerwire.h; what is asked
   in which order is src/common/upnpplan.h and upnp.cpp.  Router thread only (upnp.cpp).  Each call ends within the caps
   routerwire.h names: the search within kSsdpWaitMs, a UDP ask within its four-send schedule, an HTTP request within
   connect + send + headers (one step each) + kHttpWholeMs for the body (about 15 s at most, however slowly a device
   answers).  Plain Winsock and WinHTTP. */
#include <string>

namespace coop {
namespace net {

/* What one SSDP search found. */
struct SsdpFound
{
    std::string location;   /* the chosen answer's LOCATION (the router's device description), "" = none */
    std::string target;     /* its ST */
    bool fromGateway;       /* it came from the home adapter's own router */
    int datagrams;          /* datagrams received */
    int unreadable;         /* of those, answers SsdpAnswerRead refused */
    std::string firstWhy;   /* why the first refused one was refused */
    unsigned long ms;       /* how long the search took */
    SsdpFound() : fromGateway(false), datagrams(0), unreadable(0), ms(0) {}
};

/* THE SEARCH, sent from homeIp (the home adapter's address) to 239.255.255.250:1900 for each routerwire.h target; the
   answers are read as they arrive and the wait ends the moment the router at gatewayIp answers (or, with no gateway
   known, at the first readable answer); otherwise at swrouter::kSsdpWaitMs.  Only a device on the home network
   (swrouter::HomeNetworkHost) is listened to.  1 = an answer (*f), 0 = none within the cap, -1 = the search could not
   be sent (*why). */
int SsdpSearch(const std::string& homeIp, const std::string& gatewayIp, SsdpFound* f, std::string* why);

/* ONE HTTP REQUEST to the router: verb "GET" (soapAction and body empty) or "POST" (a SOAP call: soapAction is the
   SOAPAction header's value, body the envelope).  No proxy (the router is on the home network), no redirect followed,
   never Windows' sign-in sent, and only to a device on the home network (swrouter::HomeNetworkHost; gatewayIp is the
   home adapter's router, also accepted).  Returns the HTTP status
   (*answer = the body, at most maxBytes), or 0 when there was no answer, the URL is not plain http to an IPv4 address,
   Windows would not take the no-redirect or no-sign-in option (the request is then refused unsent), the body was still
   arriving kHttpWholeMs after the headers or broke off, or the answer was longer than maxBytes (*why). */
int HttpAsk(const std::string& url, const std::string& gatewayIp, const char* verb, const std::string& soapAction, const std::string& body, int maxBytes,
            std::string* answer, std::string* why);

/* ONE UDP ASK to the router's port 5351 (PCP / NAT-PMP), from homeIp: sent up to swrouter::kPmpSends times on the
   swrouter::PmpWaitMs schedule until an answer arrives. */
enum { kUdpAnswered = 1, kUdpSilent = 0, kUdpNobody = -1, kUdpCannot = -2 };
/* kUdpAnswered: *answerLen bytes in answer.  kUdpSilent: no answer within the schedule.  kUdpNobody: Windows reported
   that nothing listens there (ICMP port unreachable).  kUdpCannot: no socket, or an address unreadable (*why). */
int UdpAskRouter(const std::string& homeIp, const std::string& gatewayIp, const unsigned char* request, int requestLen,
                 unsigned char* answer, int answerMax, int* answerLen, std::string* why);

}   /* namespace net */
}   /* namespace coop */
