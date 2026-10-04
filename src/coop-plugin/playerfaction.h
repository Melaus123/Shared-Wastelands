// playerfaction.h - P3 piece 1 (decision 23): each player is their own faction.
// The sender's own PLAYER faction never travels by name (both games' player factions are called "Nameless" by default, so
// a name lookup on the receiver landed the peer's characters in the RECEIVER's player faction - the overlapping player
// cards decision 23 removes). It travels as "@player:<name>" and resolves here to the PEER faction: a runtime faction
// (id "coop-peer") this game creates on first use, named after the peer's own faction name.
#pragma once
#include <string>
class Faction;
namespace coop {
std::string WireFactionName(::Faction* f);                 // what a SPAWN carries for f (sender side)
::Faction* ResolveWireFaction(const std::string& wire);    // receiver side: "@player:<name>" -> the peer faction (created), else by name
bool IsPlayerFaction(::Faction* f);                        // this game's own player faction (Faction+0x250 PlayerInterface* != 0)
bool IsPeerFaction(::Faction* f);
::Faction* LocalPlayerFaction();
bool PeerFactionExists();
::Faction* PeerFactionAny();   /* pvp1 (review-pvp1 4): MAIN THREAD - the peer faction created here, else a `coop-peer` a loaded save carries, else 0 */   /* Z1-b: MAIN THREAD - the peer faction created here, or a `coop-peer` the loaded world already has */
::Faction* PeerFaction();                                   // the peer faction created here, 0 if none yet                            // this game's own player faction (0 before a game is loaded)                          // the peer faction created here (0 if none yet)
void ReportPlayerFaction();
void PlayerFactionTick();        // MAIN THREAD: my faction's name vs the last seen - a rename is the standings event
void ResetPlayerFactionState();   // on a game (re)load: the engine freed the factions
/* stand1 (docs/design-profiles1.md s2 Required 1-4): ONE stand-in per player SLOT, record id coop-p<slot>, named after that
   player's faction. IsPeerFaction is now "ANY player's stand-in"; PeerFaction / PeerFactionAny / PeerFactionDisplayName are
   "the one other game on the session link" (its slot learned from its wire names). */
int  MySlotForWire();                         // ANY THREAD: my notebook slot (StoreMySlot, else the last one this process was given); -1 none
int  LinkPeerSlot();                          // the slot the one other game on the session link announced in its wire names; -1 unknown
void NoteLinkPeerSlotAnnounced(int slot);     // M5b, MAIN THREAD: the session peer said its own slot (MSG_PEER_SLOT) - the only way LinkPeerSlot is learnt now
void PlayerFactionOnLinkDown();               // M5b fold 1, MAIN THREAD: the session link's DOWN edge (SessionOnLinkDown / leaving a live link) - LinkPeerSlot is forgotten
int  WireSenderSlot();                        // M5b, MAIN THREAD: the slot of the game whose names are being resolved - the relayed stamp, else the session peer's; -1 unknown
void SetWireRelayedSender(int slot);          // M5a fold 1, MAIN THREAD: the wire names being resolved came THROUGH THE NOTEBOOK from this slot (-1 = the session road)
int  StandInRecordSlot(::Faction* f);         // inv4 fold, ANY THREAD: the slot a faction's record id "coop-p<n>" names (table or not); -2 coop-peer, -1 other, -3 unreadable
int  StandInSlotOf(::Faction* f);             // ANY THREAD: a stand-in's slot (one created or reused here); -1 for anything else
::Faction* StandInForSlot(int slot);          // MAIN THREAD: that slot's stand-in (created or reused here), 0 if none yet
const int kStandInTableCap = 32;              // T-356: the stand-in table's bound (one per player slot this world has seen)
int  StandInList(int* slots, ::Faction** out, int cap);   // T-356, MAIN THREAD: every stand-in in the table (slot, faction), at most cap; the count
bool LegacyPeerFactionExists();              // MAIN THREAD: the loaded world carries a protocol-67 "coop-peer" faction (asked by id)
bool StandInExistsForSlot(int slot);          // MAIN THREAD: ... or a coop-p<slot> record the loaded world already carries
int  MakeAreaWriterStandIn(int slot, std::string* detail);   // MAIN THREAD, engine writes allowed: a placeholder stand-in "Player <slot>" for a player not seen here yet; 1 made, 0 one exists, -1 not made (*detail why)
::Faction* OwnerFactionForSlot(int slot, bool makePlaceholder);   // MAIN THREAD: my number -> my player faction; another -> its stand-in; makePlaceholder: a save's coop-p<slot> enters the table, else a placeholder is made; 0 none
int  StandInIsPlaceholder(int slot);            // MAIN THREAD: 1 that player's stand-in is a placeholder its player has not named yet, 0 named, -1 not in the table
::Faction* StandInRecordFaction(int slot);      // MAIN THREAD, read-only: the faction a loaded world carries under coop-p<slot> (table or not), 0 none
std::string StandInDisplayName(::Faction* f); // MAIN THREAD: a stand-in's name as this game shows it ("" for anything else)
int  StandInAnyAnyThread();                   // P26s6 fold 1, ANY THREAD: 1 = at least one stand-in (coop-p<n>) is in the table (32 pointer reads)
bool IsStandInFaction(::Faction* f);          // stand1 fold (2b), ANY THREAD: IsPeerFaction, or the protocol-67 `coop-peer` faction this world carries (pointer compare)
void NoteHeldForSlot();                        // stand1 fold (1d), ANY THREAD: a co-op send waited because this game has no slot yet (released by PlayerFactionTick)
::Faction* PeerFactionForTest(const std::string& name);
std::string PeerFactionDisplayName();   /* tags1, MAIN THREAD: the peer faction's name as this game shows it ("" when there is none) */
void PlayerFactionWorldAnswer(int verdict, unsigned num, const std::string& asked, const std::string& name);   // T-368, MAIN THREAD: the world gave profile num's faction another name (coopprof::kFacBack / kFacMoved / kFacDefault) - applied (and acknowledged) by PlayerFactionTick
void PlayerFactionWorldAnswerForget();   // T-368, MAIN THREAD: a new pick or a leave drops an answer not applied yet
void InstallFactionNameHooks();   // T-368: the FACTION tab's name box refuses a name another profile of this world holds (Kenshi's own box)
bool RenameMyFaction(const std::string& name);              // 'renamemyfaction <name>': Faction::setName + renameRecord on this game's player faction   // the 'peerfaction <name>' verb: create/rename it without a peer (T159)
}
