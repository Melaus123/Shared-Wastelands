/* src/common/slotwire.h - stand1 (docs/design-profiles1.md s2 "Required" 1-3, build step 3): the names of the per-slot
 * stand-in factions, as pure text.
 *
 * Every other player's characters live, on this game, in ONE stand-in faction PER NOTEBOOK SLOT (B13: a player's slot is
 * stable, slots.txt), record id "coop-p<slot>". A player faction travels by slot:
 *   "@slot:<n>:<name>"  - the sender's OWN player faction (n = the sender's slot; the name lets the receiver follow a rename)
 *   "@slot:<n>"         - the stand-in for slot n on the sender's game (one side of a relation pair)
 *   "@slot:?:<name>"    - the sender had no slot yet (the receiver may only read it as the one other game on its link)
 * Slots are absolute, so both games key a player pair the same way (relations.cpp g_ownerValues).
 * The zone owner swap (store.cpp TranslateZoneOwners) is N-party: a record written by slot N has its writer's own
 * buildings under the shared player-faction id ("204-gamedata.base") -> "coop-p<N>" here, and "coop-p<me>" -> my own
 * faction's id; any other "coop-p<k>" is the same stand-in on both games and stays. "coop-peer" is the protocol-67 name
 * (one stand-in for "the other player"): a record that still carries it was written in a two-party world, where the
 * writer's other player was this game.
 *
 * Pure: no engine memory, no Windows. The offline suite (src/coop-test/test_main.cpp, stand1_slot_wire) runs it.
 */
#pragma once
#include <string>
#include <vector>

namespace coopslot {

const int kSlotMax = 1023;                        /* the notebook's slot range 0..1023 (storemeta.h's RECORD owner field) */
const char* const kStandInPrefix = "coop-p";
const char* const kLegacyPeerId = "coop-peer";    /* protocol 67's single stand-in */
const char* const kSlotWirePrefix = "@slot:";

inline std::string SlotNum(int n)
{
    if (n < 0) return std::string("?");
    char b[16]; int i = 15; b[i] = 0;
    do { b[--i] = (char)('0' + n % 10); n /= 10; } while (n > 0 && i > 0);
    return std::string(b + i);
}
/* digits only - no sign, no leading zero except "0" itself - within 0..kSlotMax; -1 otherwise */
inline int ParseSlotNum(const std::string& s)
{
    if (s.empty() || s.size() > 4) return -1;
    if (s.size() > 1 && s[0] == '0') return -1;
    int v = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + (s[i] - '0'); }
    return v <= kSlotMax ? v : -1;
}
inline std::string StandInId(int slot) { return std::string(kStandInPrefix) + SlotNum(slot); }
/* the slot a "coop-p<n>" record id names, or -1 - including the legacy "coop-peer" ('e' is not a digit) */
inline int StandInSlotOfId(const std::string& sid)
{
    const std::string p(kStandInPrefix);
    if (sid.size() <= p.size() || sid.compare(0, p.size(), p) != 0) return -1;
    return ParseSlotNum(sid.substr(p.size()));
}
inline bool IsLegacyPeerId(const std::string& sid) { return sid == kLegacyPeerId; }
inline std::string SlotWire(int slot, const std::string& name)
{
    std::string w = std::string(kSlotWirePrefix) + SlotNum(slot);
    if (!name.empty()) w += ":" + name;
    return w;
}
inline bool IsSlotWire(const std::string& w) { return w.size() >= 6 && w.compare(0, 6, kSlotWirePrefix) == 0; }
/* "@slot:<n>" or "@slot:<n>:<name>" (n may be "?", read as -1). The name is everything after the second colon and may itself
   hold colons. False for anything else, a malformed number included. Any out-pointer may be null. */
inline bool ParseSlotWire(const std::string& w, int* slot, std::string* name, bool* hasName)
{
    if (!IsSlotWire(w)) return false;
    const size_t at = 6;
    const size_t colon = w.find(':', at);
    const std::string num = colon == std::string::npos ? w.substr(at) : w.substr(at, colon - at);
    int n = -1;
    if (num != "?") { n = ParseSlotNum(num); if (n < 0) return false; }
    if (slot) *slot = n;
    if (name) *name = colon == std::string::npos ? std::string() : w.substr(colon + 1);
    if (hasName) *hasName = colon != std::string::npos;
    return true;
}
/* M5b (carried from M5a) - WHOSE STANDING A PAIR IS, by the relations rule (relations.cpp Owned): the OWNER side decides first,
   then the other side. The slot of the first side that is a player's wire name; -1 when that side is unnumbered ("@slot:?") or
   neither side is a player's (a world pair). */
inline int PairOwnerSlot(const std::string& ownerSid, const std::string& otherSid)
{
    int n = -1;
    if (ParseSlotWire(ownerSid, &n, 0, 0)) return n;
    if (ParseSlotWire(otherSid, &n, 0, 0)) return n;
    return -1;
}
/* the KEY form of a wire name: "@slot:<n>" without the name (a rename must not make a new key); anything else unchanged */
inline std::string SlotWireKey(const std::string& w)
{
    int n = -1;
    if (!ParseSlotWire(w, &n, 0, 0)) return w;
    return std::string(kSlotWirePrefix) + SlotNum(n);
}
/* the N-party zone owner swap for ONE building state: `owner` as the record holds it, `mineSid` this game's player
   faction id, `mySlot` / `writerSlot` the reader's and the record writer's notebook slots. *out = the id to write. */
enum { kSwapNoWriter = -1, kSwapNone = 0, kSwapToStandIn = 1, kSwapToMine = 2, kSwapLegacyToMine = 3 };
inline int ZoneOwnerSwap(const std::string& owner, const std::string& mineSid, int mySlot, int writerSlot, std::string* out)
{
    if (owner.empty() || mineSid.empty()) return kSwapNone;
    if (owner == mineSid)
    {
        if (writerSlot < 0 || writerSlot > kSlotMax || writerSlot == mySlot) return kSwapNoWriter;
        if (out) *out = StandInId(writerSlot);
        return kSwapToStandIn;
    }
    if (mySlot >= 0 && owner == StandInId(mySlot)) { if (out) *out = mineSid; return kSwapToMine; }
    /* stand1 fold (review-stand1 4c): only a record ANOTHER game wrote - in my own old record `coop-peer` is the other player */
    if (IsLegacyPeerId(owner) && writerSlot >= 0 && writerSlot != mySlot) { if (out) *out = mineSid; return kSwapLegacyToMine; }
    return kSwapNone;
}

/* stand1 fold (review-stand1 4b): may a record by `writerSlot` be translated now? MY slot must be known (else coop-p<me> -> mine
   cannot be done and my buildings would keep an owner id this world may not have), the writer's too, and the writer's
   stand-in must exist unless the writer is me. Not ready = the swap is refused and the engine loads its own copy. */
inline bool ZoneSwapReady(int mySlot, int writerSlot, bool writerStandInExists)
{
    return mySlot >= 0 && writerSlot >= 0 && (writerSlot == mySlot || writerStandInExists);
}
/* stand1 fold (review-stand1 1d): a wire name with no slot ("@slot:?...") - such a send is HELD until this game has one */
inline bool WireLacksSlot(const std::string& w) { int n = 0; return ParseSlotWire(w, &n, 0, 0) && n < 0; }

/* the name of a stand-in made before its player has been seen here; that player's own name replaces it (ResolveStandIn) */
inline std::string PlaceholderName(int slot) { return std::string("Player ") + SlotNum(slot); }
inline void AddSlotOnce(std::vector<int>* v, int s)
{
    if (v == 0 || s < 0 || s > kSlotMax) return;
    for (size_t i = 0; i < v->size(); ++i) if ((*v)[i] == s) return;
    v->push_back(s);
}
/* the players whose stand-in must exist before this game loads the area records it holds: each writer slot (`writers`, the
   records' owner fields) that is a real slot and not `mySlot`, once each, in the order met, at most `cap` of them. None while
   this game's own slot is unknown - its own records could not be told from the others'. */
inline void AreaWriterStandInSlots(const std::vector<int>& writers, int mySlot, size_t cap, std::vector<int>* out)
{
    if (out == 0 || mySlot < 0 || mySlot > kSlotMax) return;
    for (size_t i = 0; i < writers.size() && out->size() < cap; ++i)
        if (writers[i] != mySlot) AddSlotOnce(out, writers[i]);
}
/* the name a stand-in takes when another faction RECORD already carries the one wanted - one rule for a new stand-in and a
   rename: the name, else "<name> (peer)", else "<name> (peer) (p<slot>)". wantTaken / peerTaken: a record other than this
   stand-in's own carries `want` / `want + " (peer)"`. */
inline std::string StandInClashName(const std::string& want, int slot, bool wantTaken, bool peerTaken)
{
    if (!wantTaken) return want;
    if (!peerTaken) return want + " (peer)";
    return want + " (peer) (p" + SlotNum(slot) + ")";
}

}   /* namespace coopslot */
