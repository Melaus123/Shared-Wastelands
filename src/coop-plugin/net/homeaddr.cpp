/* THIS COMPUTER'S HOME NETWORK, read from Windows (homeaddr.h).
   Its own translation unit because iphlpapi.h wants winsock2.h ahead of windows.h and the Vista-level
   IP_ADAPTER_ADDRESSES (FirstGatewayAddress, GAA_FLAG_INCLUDE_GATEWAYS), while build.bat compiles the plugin at
   _WIN32_WINNT 0x0501.  Kenshi needs Windows 7 or later, so the Vista call is always there.  The DECISION is pure
   (coopui::PanelPickHomeNic, coopui::PanelVpnKind) and the offline suite sweeps it; this file only lists the adapters. */
#undef WINVER
#define WINVER 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

#include <string>
#include <vector>
#include "homeaddr.h"
#include "../../common/panelstatus.h"

namespace coop {
namespace net {

typedef char HomeAddrNicTypesAgree[(coopui::kNicEthernet == IF_TYPE_ETHERNET_CSMACD
                                    && coopui::kNicWifi == IF_TYPE_IEEE80211
                                    && coopui::kNicLoopback == IF_TYPE_SOFTWARE_LOOPBACK
                                    && coopui::kNicTunnel == IF_TYPE_TUNNEL
                                    && coopui::kNicPpp == IF_TYPE_PPP) ? 1 : -1];

namespace {

/* A Windows text as UTF-8, the buffer sized from the text itself (a long name in any language is kept whole). */
std::string Utf8(const wchar_t* w)
{
    if (w == 0) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    if (n <= 1) return std::string();
    std::vector<char> buf((size_t)n);
    if (::WideCharToMultiByte(CP_UTF8, 0, w, -1, &buf[0], n, 0, 0) <= 0) return std::string();
    return std::string(&buf[0]);
}

}   /* namespace */

void HomeNetwork(HomeNet* out)
{
    out->ip.clear();
    out->gateway.clear();
    out->name.clear();
    out->how.clear();
    out->vpnKind = coopui::kGameVpnNone;
    out->vpnIp.clear();

    /* The adapter Windows would send a packet to a public address through (8.8.8.8, in network byte order).  Nothing is
       sent: Windows only looks the route up. */
    DWORD bestIf = 0;
    const DWORD bestRc = ::GetBestInterface((IPAddr)htonl(0x08080808ul), &bestIf);

    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    std::vector<unsigned char> buf(16 * 1024);
    ULONG size = (ULONG)buf.size();
    ULONG rc = ::GetAdaptersAddresses(AF_INET, flags, 0, (PIP_ADAPTER_ADDRESSES)&buf[0], &size);
    for (int tries = 0; rc == ERROR_BUFFER_OVERFLOW && tries < 3; ++tries)
    {
        buf.resize((size_t)size + 1024);
        size = (ULONG)buf.size();
        rc = ::GetAdaptersAddresses(AF_INET, flags, 0, (PIP_ADAPTER_ADDRESSES)&buf[0], &size);
    }
    if (rc != NO_ERROR)
    {
        out->how = "GetAdaptersAddresses failed, Windows error " + coopui::PanelNum((long long)rc);
        return;
    }
    std::vector<coopui::PanelNic> nics;
    std::vector<std::string> names, gateways, vpnWords;
    std::vector<coopui::PanelVpnNic> vpnNics;   /* T-631: the same adapters for the game VPN row (description, up, IPv4) */
    for (PIP_ADAPTER_ADDRESSES a = (PIP_ADAPTER_ADDRESSES)&buf[0]; a != 0; a = a->Next)
    {
        coopui::PanelNic n;
        n.ifType     = (int)a->IfType;
        n.up         = (a->OperStatus == IfOperStatusUp) ? 1 : 0;
        n.hasGateway = (a->FirstGatewayAddress != 0) ? 1 : 0;
        n.best       = (bestRc == NO_ERROR && a->IfIndex == bestIf) ? 1 : 0;
        const std::string name = Utf8(a->FriendlyName);
        const std::string desc = Utf8(a->Description);
        const char* vpn = coopui::PanelVpnKind(name, desc);
        n.vpn        = (vpn != 0) ? 1 : 0;
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u != 0; u = u->Next)
        {
            const SOCKADDR* sa = u->Address.lpSockaddr;
            if (sa == 0 || sa->sa_family != AF_INET) continue;
            n.ipv4.push_back((unsigned long)ntohl(((const SOCKADDR_IN*)sa)->sin_addr.s_addr));
        }
        std::string gw;
        for (PIP_ADAPTER_GATEWAY_ADDRESS_LH g = a->FirstGatewayAddress; g != 0 && gw.empty(); g = g->Next)
        {
            const SOCKADDR* sa = g->Address.lpSockaddr;
            if (sa == 0 || sa->sa_family != AF_INET) continue;
            const unsigned long ga = (unsigned long)ntohl(((const SOCKADDR_IN*)sa)->sin_addr.s_addr);
            if (coopui::PanelIpv4Usable(ga) != 0) gw = coopui::PanelIpv4Text(ga);
        }
        coopui::PanelVpnNic vn;
        vn.desc = desc;
        vn.up   = n.up;
        vn.ipv4 = n.ipv4;
        vpnNics.push_back(vn);
        nics.push_back(n);
        names.push_back(name);
        gateways.push_back(gw);
        vpnWords.push_back(vpn != 0 ? std::string(vpn) : std::string());
    }
    int picked = -1, why = coopui::kPickNone;
    out->ip = coopui::PanelPickHomeNic(nics, &picked, &why);
    out->vpnKind = coopui::PanelGameVpnFind(vpnNics, &out->vpnIp);   /* T-631: the address stays out of `how` (it is logged) */
    out->how = coopui::PanelNum((long long)nics.size()) + " adapters listed; ";
    if (picked < 0) out->how += std::string("none picked - ") + coopui::PanelPickWhyText(why);
    else
    {
        const size_t p = (size_t)picked;
        out->name = names[p];
        out->gateway = gateways[p];
        out->how += "picked '" + names[p] + "' (type " + coopui::PanelNum((long long)nics[p].ifType)
                  + (gateways[p].empty() ? ", no router named" : ", has a router") + ") - " + coopui::PanelPickWhyText(why);
    }
    if (bestRc != NO_ERROR) out->how += "; Windows named no internet route (GetBestInterface error " + coopui::PanelNum((long long)bestRc) + ")";
    for (size_t i = 0; i < nics.size(); ++i)
        if (nics[i].vpn != 0 && nics[i].up != 0)
            out->how += "; skipped '" + names[i] + "' (" + vpnWords[i] + ", a VPN" + (nics[i].best != 0 ? ", Windows' internet route" : "") + ")";
}

std::string HomeNetworkAddress(std::string* how)
{
    HomeNet h;
    HomeNetwork(&h);
    if (how) *how = h.how;
    return h.ip;
}

}   /* namespace net */
}   /* namespace coop */
