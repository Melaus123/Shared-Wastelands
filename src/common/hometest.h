// hometest.h - T-1334 (owner 1616 a, run T2331): the TEST-ONLY `storetest home` lever's pure part - its argument check
// (store.cpp StoreHomeTestCommand). DEV build only. The forms, each naming buildings by their position keys:
//   storetest home show <buildingKey>              read: the group the building's residents hand names, that group's live home key
//                                                  and this game's record home entry (g_homeOf) for its world id
//   storetest home drop <buildingKey>              that group loses its home the engine's way: Ownerships::removeOwnedObject 0x7EB550
//                                                  with the building's handle (it blanks the home when it names the building)
//   storetest home move <buildingKey> <toKey>      that group is moved to the other building the engine's way: Ownerships::
//                                                  setHomeBuilding 0x7EBE40 through its hooked entry, as the engine calls it
// PURE: no engine, no plugin types.
#pragma once
#include <sstream>
#include <string>
namespace swhometest {
enum { kBad = 0, kShow = 1, kDrop = 2, kMove = 3 };
const size_t kKeyMax = 120;   // a building key longer than this is refused (the plugin's key buffers are 128)
inline int KeyOk(const std::string& k) { return (!k.empty() && k.size() <= kKeyMax && k.find('@') != std::string::npos) ? 1 : 0; }
// The words after `storetest home`. kShow / kDrop: exactly one key; kMove: exactly two different keys. kBad otherwise.
inline int Parse(const std::string& args, std::string* key, std::string* to)
{
    std::istringstream is(args);
    std::string op, a, b, extra;
    if (!(is >> op >> a)) return kBad;
    if (op == "move") { if (!(is >> b)) return kBad; }
    if (is >> extra) return kBad;
    if (KeyOk(a) == 0) return kBad;
    int r = kBad;
    if (op == "show") r = kShow;
    else if (op == "drop") r = kDrop;
    else if (op == "move") { if (KeyOk(b) == 0 || b == a) return kBad; r = kMove; }
    else return kBad;
    *key = a;
    if (r == kMove) *to = b;
    return r;
}
inline const char* Usage() { return "error storetest home: usage storetest home show|drop <buildingKey> | storetest home move <buildingKey> <toBuildingKey>"; }
}
