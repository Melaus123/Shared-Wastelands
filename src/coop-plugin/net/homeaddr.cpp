/* mp4 (docs/design-mpmenu1.md section 6) - THIS COMPUTER'S HOME-NETWORK ADDRESS, read from Windows.
   Its own translation unit because iphlpapi.h wants winsock2.h ahead of windows.h and the Vista-level
   IP_ADAPTER_ADDRESSES (FirstGatewayAddress, GAA_FLAG_INCLUDE_GATEWAYS), while build.bat compiles the plugin at
   _WIN32_WINNT 0x0501.  Kenshi needs Windows 7 or later, so the Vista call is always there.  The DECISION is pure
   (coopui::PanelPickHomeAddr) and the offline suite sweeps it; this file only lists the adapters. */
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
                                    && coopui::kNicWifi == IF_TYPE_IEEE80211) ? 1 : -1];

std::string HomeNetworkAddress(std::string* how)
{
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
        if (how) *how = "GetAdaptersAddresses failed, Windows error " + coopui::PanelNum((long long)rc);
        return std::string();
    }
    std::vector<coopui::PanelNic> nics;
    std::vector<std::string> names;
    for (PIP_ADAPTER_ADDRESSES a = (PIP_ADAPTER_ADDRESSES)&buf[0]; a != 0; a = a->Next)
    {
        coopui::PanelNic n;
        n.ifType     = (int)a->IfType;
        n.up         = (a->OperStatus == IfOperStatusUp) ? 1 : 0;
        n.hasGateway = (a->FirstGatewayAddress != 0) ? 1 : 0;
        for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u != 0; u = u->Next)
        {
            const SOCKADDR* sa = u->Address.lpSockaddr;
            if (sa == 0 || sa->sa_family != AF_INET) continue;
            n.ipv4.push_back((unsigned long)ntohl(((const SOCKADDR_IN*)sa)->sin_addr.s_addr));
        }
        nics.push_back(n);
        char nm[128];
        nm[0] = 0;
        if (a->FriendlyName != 0) ::WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, nm, (int)sizeof nm, 0, 0);
        nm[sizeof nm - 1] = 0;
        names.push_back(std::string(nm));
    }
    int picked = -1;
    const std::string ip = coopui::PanelPickHomeAddr(nics, &picked);
    if (how)
    {
        *how = coopui::PanelNum((long long)nics.size()) + " adapters listed; ";
        if (picked < 0) *how += "none is up, Ethernet or Wi-Fi, with a usable IPv4 address";
        else *how += "picked '" + names[(size_t)picked] + "' (type " + coopui::PanelNum((long long)nics[(size_t)picked].ifType)
                   + (nics[(size_t)picked].hasGateway ? ", has a router)" : ", no router - no up adapter had one)");
    }
    return ip;
}

}   /* namespace net */
}   /* namespace coop */
