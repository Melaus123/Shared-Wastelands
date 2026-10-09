#pragma once
/* This computer's home network, read from Windows: its address (LOCAL ADDRESS on HOSTING) and its router (where the
   router ask goes). */
#include <string>

namespace coop {
namespace net {

/* The home network's adapter (coopui::PanelPickHomeNic in src/common/panelstatus.h says how it is chosen). */
struct HomeNet
{
    std::string ip;        /* this computer's address on it, dotted ("" = none found) */
    std::string gateway;   /* its router's address, dotted ("" = the adapter names none) */
    std::string name;      /* the adapter's name as Windows shows it ("Wi-Fi", "Ethernet 2") */
    int vpnKind;           /* T-631: the game VPN found (coopui::kGameVpn* - Radmin VPN or Hamachi), 0 = neither */
    std::string vpnIp;     /* T-631: this computer's address on it, dotted ("" = none) - shown on HOSTING, never logged */
    std::string how;       /* for the log: how many adapters, which was picked and why, which were skipped as VPNs -
                              adapter names only, never an address (logs go into bug reports) */
    HomeNet() : vpnKind(0) {}
};

/* When the Hosting screen opens (main thread) and when the router is asked (the router thread, upnp.cpp) - never per
   tick; it keeps no state, so either thread may call it.  GetBestInterface towards a public address (no packet is sent)
   names the adapter Windows sends internet traffic through; GetAdaptersAddresses lists the adapters; the pick is pure.
   Nothing leaves this computer. */
void HomeNetwork(HomeNet* out);

/* HomeNetwork's address alone; *how (may be 0) gets its log text. */
std::string HomeNetworkAddress(std::string* how);

}   /* namespace net */
}   /* namespace coop */
