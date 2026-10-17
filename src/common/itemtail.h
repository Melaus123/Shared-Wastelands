/* src/common/itemtail.h - WHICH OPTIONAL BLOCK COMES NEXT IN AN ITEM_REQUEST / ITEM_CONFIRM TAIL.
 *
 * After the trade block (and the optional shop block) both messages carry optional blocks in a fixed order:
 *   ITEM_REQUEST: [BAG1 rows] [OWN1] [GEM1] [BAGP]          ITEM_CONFIRM: [BAG1 rows] [OWN1] [GEM1]
 * Any of them may be absent, so the reader decides from the marker at `at` which step reads the bytes there. A GEM1 block may
 * follow the trade block directly (a gear item that is not a pack and has no owner), so neither the BAG1 step nor the OWN1 step
 * may take a GEM1 marker for its own: read as backpack rows it is 'malformed' and the whole message is refused.
 * Pure functions of the bytes; session.cpp's two readers and the offline suite call the same ones. C++03 (VS2010 v100).
 */
#pragma once

#include <cstddef>
#include "bagwire.h"    /* BagPathAt (BAGP); it brings ownerwire.h (OwnerBlockAt) and gearwire.h (GearBlockAt) */

namespace itemtail {

/* ITEM_REQUEST: the bytes at `at` are read as BAG1 rows - there are bytes, and they are not an OWN1, GEM1 or BAGP block. */
inline bool ReqBagNext(const char* p, size_t n, size_t at)
{
    return at < n && !coopmark::OwnerBlockAt(p, n, at) && !gearwire::GearBlockAt(p, n, at) && !coopbag::BagPathAt(p, n, at);
}
/* ITEM_REQUEST: the bytes at `at` are read as an OWN1 block - there are bytes, and they are not a BAGP or GEM1 block. */
inline bool ReqOwnerNext(const char* p, size_t n, size_t at)
{
    return at < n && !coopbag::BagPathAt(p, n, at) && !gearwire::GearBlockAt(p, n, at);
}
/* ITEM_CONFIRM: the bytes at `at` are read as BAG1 rows - there are bytes, and they are not an OWN1 or GEM1 block. */
inline bool CfBagNext(const char* p, size_t n, size_t at)
{
    return at < n && !coopmark::OwnerBlockAt(p, n, at) && !gearwire::GearBlockAt(p, n, at);
}
/* ITEM_CONFIRM: the bytes at `at` are read as an OWN1 block - there are bytes, and they are not a GEM1 block. */
inline bool CfOwnerNext(const char* p, size_t n, size_t at)
{
    return at < n && !gearwire::GearBlockAt(p, n, at);
}

}   /* namespace itemtail */
