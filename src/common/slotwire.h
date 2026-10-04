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
 * faction's id; any other "coop-p<k>" is the same stand-in on both games and stays (and needs a stand-in here before the
 * area is read - ZoneFileNamedSlots). "coop-peer" is the protocol-67 name (one stand-in for "the other player"): it is
 * this game's only while the world has had at most two players (LegacyPeerIsMineByWorld), else it is kept as it is.
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
/* Another player's faction as this world's save carries it (record id "coop-p<n>") travels as "@slot:<n>", the name a stand-in
   met this session travels under, so a standing towards that player reaches that player's own game (where "@slot:<n>" is its
   player faction) and every other game (its stand-in or carried faction for n). `mySlot` is this game's slot: "coop-p<mySlot>"
   stays as it is (it is not another player). Anything else is returned unchanged. */
inline std::string StandInIdAsSlotWire(const std::string& sid, int mySlot)
{
    const int n = StandInSlotOfId(sid);
    if (n < 0 || n == mySlot) return sid;
    return std::string(kSlotWirePrefix) + SlotNum(n);
}
/* the key of one side of a relation pair: the slot form without a name, a carried "coop-p<n>" keyed as "@slot:<n>" - so the
   record id and either wire form of one player give one key */
inline std::string RelationSideKey(const std::string& sid, int mySlot) { return SlotWireKey(StandInIdAsSlotWire(sid, mySlot)); }
/* THE PLAYER-NUMBER LOOKUP, pure half (the plugin half: playerfaction.cpp OwnerFactionForSlot). A building's owner id names a
   player number or nobody, as the game of `viewerSlot` wrote it: the shared player-faction id = that game's own player,
   "coop-p<n>" = player n, "coop-peer" = protocol 67's "the other player", anything else (a town, nofac) = nobody's. */
const int kOwnerWorld = -1;      /* not a player's */
const int kOwnerLegacy = -2;     /* protocol 67's "coop-peer" */
const int kOwnerNoViewer = -3;   /* the shared id, on a game whose number is not known */
inline int OwnerSlotOfId(const std::string& owner, const std::string& sharedSid, int viewerSlot)
{
    if (owner.empty()) return kOwnerWorld;
    if (!sharedSid.empty() && owner == sharedSid) return (viewerSlot >= 0 && viewerSlot <= kSlotMax) ? viewerSlot : kOwnerNoViewer;
    if (IsLegacyPeerId(owner)) return kOwnerLegacy;
    const int s = StandInSlotOfId(owner);
    return s >= 0 ? s : kOwnerWorld;
}
/* ... and back: the owner id that names player `slot` on the game of `viewerSlot` - the shared id for that game's own player, else
   player slot's stand-in; "" for no player, or a viewer whose own number is not known (its player cannot be told from the others) */
inline std::string OwnerIdForSlot(int slot, const std::string& sharedSid, int viewerSlot)
{
    if (slot < 0 || slot > kSlotMax || viewerSlot < 0) return std::string();
    return slot == viewerSlot ? sharedSid : StandInId(slot);
}
/* the N-party zone owner swap for ONE building state: `owner` as the record holds it, `mineSid` this game's player faction id,
   `mySlot` / `writerSlot` the reader's and the record writer's notebook slots, `legacyIsMine` the protocol-67 rule
   (LegacyPeerIsMineByWorld). The owner is read as a player number the way the WRITER named it and written back the way THIS game
   names that player; any other player's coop-p<k> is the same stand-in on both games and stays. *out = the id to write. */
enum { kSwapNoWriter = -1, kSwapNone = 0, kSwapToStandIn = 1, kSwapToMine = 2, kSwapLegacyToMine = 3, kSwapLegacyInactive = 4 };
inline int ZoneOwnerSwapN(const std::string& owner, const std::string& mineSid, int mySlot, int writerSlot, bool legacyIsMine, std::string* out)
{
    if (owner.empty() || mineSid.empty()) return kSwapNone;
    const int s = OwnerSlotOfId(owner, mineSid, writerSlot);   /* whose it is, as the writer wrote it */
    if (s == kOwnerNoViewer || (owner == mineSid && writerSlot == mySlot)) return kSwapNoWriter;
    if (s == kOwnerLegacy)
    {
        if (writerSlot < 0 || writerSlot == mySlot) return kSwapNone;   /* in my own old record `coop-peer` is the other player */
        if (!legacyIsMine) return kSwapLegacyInactive;   /* more than two players: never handed to me */
        if (out) *out = mineSid;
        return kSwapLegacyToMine;
    }
    if (s < 0) return kSwapNone;
    const std::string to = (mySlot >= 0) ? OwnerIdForSlot(s, mineSid, mySlot) : StandInId(s);   /* the same player, as THIS game names it */
    if (to.empty() || to == owner) return kSwapNone;
    if (out) *out = to;
    return (s == mySlot) ? kSwapToMine : kSwapToStandIn;
}
/* the swap in a world of at most two players: a protocol-67 coop-peer in another game's record is mine */
inline int ZoneOwnerSwap(const std::string& owner, const std::string& mineSid, int mySlot, int writerSlot, std::string* out)
{
    return ZoneOwnerSwapN(owner, mineSid, mySlot, writerSlot, true, out);
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
   records' owner fields) that is a real slot and not `mySlot`, once each, in the order met, until `out` holds `cap`. None while
   this game's own slot is unknown - its own records could not be told from the others'. */
inline void AreaWriterStandInSlots(const std::vector<int>& writers, int mySlot, size_t cap, std::vector<int>* out)
{
    if (out == 0 || mySlot < 0 || mySlot > kSlotMax) return;
    for (size_t i = 0; i < writers.size() && out->size() < cap; ++i)
        if (writers[i] != mySlot) AddSlotOnce(out, writers[i]);
}
/* the players to make a placeholder stand-in for, over the writers AND every player the records name as a building's owner (`named`,
   ZoneFileNamedSlots): each other player once, writers first, skipping those listed in `out` already and those that have a stand-in
   here (`haveStandIn`, which never count). `room` = how many placeholders may still be made: at most that many are added to `out`.
   Returns how many players without a stand-in the room left out. */
inline int AreaStandInSlotsCapped(const std::vector<int>& writers, const std::vector<int>& named, int mySlot, const std::vector<int>& haveStandIn,
                                  size_t room, std::vector<int>* out)
{
    if (out == 0 || mySlot < 0 || mySlot > kSlotMax) return 0;
    std::vector<int> all;
    for (size_t i = 0; i < writers.size(); ++i) if (writers[i] != mySlot) AddSlotOnce(&all, writers[i]);
    for (size_t i = 0; i < named.size(); ++i) if (named[i] != mySlot) AddSlotOnce(&all, named[i]);
    int left = 0;
    size_t added = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        bool skip = false;
        for (size_t j = 0; j < out->size(); ++j) if ((*out)[j] == all[i]) skip = true;
        for (size_t j = 0; j < haveStandIn.size(); ++j) if (haveStandIn[j] == all[i]) skip = true;
        if (skip) continue;
        if (added < room) { out->push_back(all[i]); ++added; } else ++left;
    }
    return left;
}
/* the player numbers an area record FILE names as owners - every engine string (int32 little-endian length + bytes) that reads
   exactly "coop-p<n>". Raw text that is not such a string is never read; nothing outside [p, p+n) is read. Returns the matches
   (repeats included); `out` gets each number once, in the order met. *legacyPeer (may be null) is set true when such a string reads
   exactly "coop-peer" (protocol 67's owner), and left as it is otherwise. */
inline int ZoneFileNamedSlots(const unsigned char* p, size_t n, std::vector<int>* out, bool* legacyPeer = 0)
{
    const size_t pl = 6;   /* "coop-p" */
    int hits = 0;
    if (p == 0 || n < 4 + pl + 1) return 0;
    for (size_t i = 4; i + pl < n; ++i)
    {
        size_t j = 0;
        while (j < pl && p[i + j] == (unsigned char)kStandInPrefix[j]) ++j;
        if (j < pl) continue;
        const unsigned int len = (unsigned int)p[i - 4] | ((unsigned int)p[i - 3] << 8) | ((unsigned int)p[i - 2] << 16) | ((unsigned int)p[i - 1] << 24);
        if (len < pl + 1 || len > pl + 4 || (size_t)len > n - i) continue;
        const std::string id((const char*)p + i, (size_t)len);
        if (legacyPeer != 0 && IsLegacyPeerId(id)) *legacyPeer = true;
        const int k = StandInSlotOfId(id);
        if (k < 0) continue;
        ++hits;
        AddSlotOnce(out, k);
    }
    return hits;
}
/* the players a record by `writerSlot` needs a stand-in for on the game of `mySlot`: the writer (its own buildings carry the shared
   id) and every number the file names, except me */
inline void SlotsNeedingStandIn(const std::vector<int>& named, int mySlot, int writerSlot, std::vector<int>* out)
{
    if (writerSlot != mySlot) AddSlotOnce(out, writerSlot);
    for (size_t i = 0; i < named.size(); ++i) if (named[i] != mySlot) AddSlotOnce(out, named[i]);
}
/* the area read's owner gate: a record another game wrote is loaded with its owners translated only when ZoneSwapReady holds AND no
   player its file names lacks a stand-in here (`namedMissing`; -1 = the file could not be read). A missing one would leave that
   building with the engine's fallback owner, and the next save of the area would lose its owner for every game. */
inline bool ZoneOwnersReady(int mySlot, int writerSlot, bool writerStandInExists, int namedMissing)
{
    return ZoneSwapReady(mySlot, writerSlot, writerStandInExists) && namedMissing == 0;
}
/* a protocol-67 "coop-peer" owner in a record another game wrote is this game's only while the world has never had more than two
   players. `worldPlayers` is the WORLD SERVER's count - every number its slots.txt holds, carried on the PLAYERS roster
   (joinstage.h RosterDecode) - so every game answers the same; -1 (no roster on this link) cannot tell, and the owner is kept as it
   is, never guessed from this game's own evidence (two games could disagree). */
inline bool LegacyPeerIsMineByWorld(int worldPlayers) { return worldPlayers >= 0 && worldPlayers <= 2; }
/* the area read's gate for a protocol-67 "coop-peer" owner in a record ANOTHER game wrote (`namesLegacy`: its file names one,
   ZoneFileNamedSlots). The read goes ahead (true) only when the world server's count is known - with -1 the owner would be judged on
   a guess, so the read is refused and the engine loads its own copy - and, when that count keeps the owner as it is (more than two
   players: ZoneOwnerSwapN's kSwapLegacyInactive), a "coop-peer" faction exists here to own those buildings (`legacyFactionExists`),
   else they would take the engine's fallback owner. A record by no known writer or by this game is not this gate's. */
inline bool ZoneLegacyPeerReady(bool namesLegacy, int mySlot, int writerSlot, int worldPlayers, bool legacyFactionExists)
{
    if (!namesLegacy || writerSlot < 0 || writerSlot == mySlot) return true;
    if (worldPlayers < 0) return false;
    return LegacyPeerIsMineByWorld(worldPlayers) || legacyFactionExists;
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
/* whether a faction name is held here: a faction other than the one asking CURRENTLY carries it. Kenshi's record index
   (GameDataContainer::findRecordByName) can name a record that no longer carries the name - renameRecord (1.0.65 0x6BF820) erases
   the key held at container+0x150, never the record's old name, so every old name stays indexed - and the FACTION tab writes only
   the faction's own name (Faction+0x1A8, decomp_491940), never its record's. So: a live faction (not the one asking) whose name is
   `name` holds it (liveHit); else the indexed record (not the one asking) holds it only while its record name is `name` AND, when a
   live faction is made from it, that faction's name is `name` too. */
inline bool FactionNameHeld(const std::string& name, bool liveHit, bool indexHit, const std::string& indexRecordName,
                            bool indexHasLive, const std::string& indexLiveName)
{
    if (liveHit) return true;
    if (!indexHit || indexRecordName != name) return false;
    return !indexHasLive || indexLiveName == name;
}
/* a stand-in carrying a clash name (its name is not the one its player's faction carries) asks for that name again whenever a
   faction name changes here - the name may be free now (StandInClashName then gives it back). */
inline bool StandInAsksAgain(const std::string& current, const std::string& asked)
{
    return !asked.empty() && current != asked;
}
/* The world gave this game's player faction a name that a faction RECORD of this save carries (sid = that record's id). A stand-in's
   record is stale - the world's names are unique, so that player no longer holds the name here - and moves aside by the clash rule
   ("<name> (peer)", else "<name> (peer) (p<slot>)"); peerTaken: a record carries "<name> (peer)". False = the record is not a
   per-slot stand-in's (a Kenshi faction, or an old save's single stand-in) and keeps its name. */
inline bool StandInAsideName(const std::string& sid, const std::string& name, bool peerTaken, std::string* out)
{
    const int slot = StandInSlotOfId(sid);
    if (slot < 0) return false;
    *out = StandInClashName(name, slot, true, peerTaken);
    return true;
}

}   /* namespace coopslot */
