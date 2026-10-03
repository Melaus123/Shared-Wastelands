#pragma once
/* mp4 (docs/design-mpmenu1.md section 6): this computer's home-network address, read from Windows. */
#include <string>

namespace coop {
namespace net {

/* When the Hosting screen opens (main thread) and when the router is asked to forward the world server's port (the
   router thread, upnp.cpp) - never per tick; it keeps no state, so either thread may call it.  GetAdaptersAddresses, then coopui::PanelPickHomeAddr
   (src/common/panelstatus.h says how the adapter is chosen).  "" = none found.  *how says which adapter, or why none,
   for the log.  Nothing leaves this computer: no outside service is asked. */
std::string HomeNetworkAddress(std::string* how);

}   /* namespace net */
}   /* namespace coop */
