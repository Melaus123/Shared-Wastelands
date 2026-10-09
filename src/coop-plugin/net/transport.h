// transport.h - the ONE abstraction the replication layer talks to.
//
// Ratified design point (user amendment 2026-08-05): there is NO privileged "default"
// backend. The PLAYER chooses when hosting or joining. ENet (direct/LAN/IP) ships first
// because it is testable on one machine; the Steam backend is added later as an EQUAL
// citizen implementing this same interface. Nothing above this header may know or care
// which backend is live - if a call site needs to ask, that is a design bug.
#pragma once

#include <string>
#include <vector>

namespace coop {
namespace net {

// Message types (spec section 3). Wire value is explicit and MUST NOT be renumbered -
// a version handshake protects mismatched builds, but stable numbers keep logs readable.
enum MsgType
{
    MSG_HELLO  = 1,   // handshake: protocol version + build id + role
    MSG_WELCOME = 2,  // host's reply: accepted, assigned peer id
    MSG_PING   = 3,   // liveness/RTT probe
    MSG_PONG   = 4,
    MSG_SPAWN  = 10,  // M1
    MSG_TASK   = 11,  // M2
    MSG_MOVE   = 12,  // M2 (unreliable)
    MSG_HIT    = 13,  // M3
    MSG_STATE  = 14,  // M4
    MSG_APPEARANCE = 15,  // H010b - the authority's appearance RECORD (F156/F160)
    MSG_CLOTHING   = 16,  // the authority's worn items (F173)
    MSG_COMBATMODE = 17,  // P-15/F220: the authority ENTERED or LEFT combat, and against whom
    // F322 - the authority's engine DESTROYED this character. Until P038 hooked
    // `GameWorld::destroy` there was no moment at which this could be said: retirement only ever
    // stopped US dereferencing a dead pointer, and the peer kept its copy forever, so the two
    // worlds diverged by attrition (F290). The destroy event runs on the MAIN thread (F321), which
    // is what makes it safe to send from.
    MSG_DESPAWN    = 18,
    // F348 - the authority's character STARTED A SWING. Combat mode (17) says a fight is on;
    // this says a blow is being thrown, which the engine authorizes by three field writes with
    // no AI decision anywhere in the path. Sent on the transition INTO `combatState == 0`, which
    // is the moment the swing is confirmed to have begun rather than merely intended.
    MSG_SWING      = 19,
    MSG_BYE    = 20,
    // H019 / M-C - the authority's CURRENT GOAL for a uid it owns: TaskType, subject uid, location,
    // priority. Sent on change and refreshed every 5 s. The peer stores it per puppet; the
    // `handoff auto <n>` command hands it to the copy as an ORDER and scores the first free pass.
    MSG_INTENT     = 21,
    // M-B / P064 - the authority's CONTEXT for a replicated character: squad template stringID, SquadType,
    // home town stringID, home building handle (raw) + its position, squad member type. Sent once after SPAWN.
    MSG_CONTEXT    = 22,
    // M-A - this instance's LOADED SECTOR SET (client -> host, 1 Hz): count(u32) | (x,y)(i32 pairs)
    MSG_ZONES      = 23,
    // M-D - the host's sector->owner map (host -> client, 1 Hz): count(u32) | (x,y,owner)(i32 triples)
    MSG_SECTORMAP  = 24,
    // M-D - a SQUAD handoff: leader(u32) | forced(u32) | count(u32) | per uid: uid(u32) x,y,z(3f) fx,fz(2f) intentType(i32) intentSubject(u32) ix,iy,iz(3f)
    MSG_XFER       = 25,
    // M-D - the new owner took the squad: leader(u32) | count(u32)
    MSG_XFER_ACK   = 26,
    // M-A step 2 - the peer no longer holds this character's sector: retire your copy (not a death; the owner
    // re-announces with SPAWN when the sector is loaded again). uid(u32)
    MSG_UNLOAD     = 27,
    // P1 persistence (store.h): a squad's sleeping record - worldId, squadSid, factionName (strings), x,y,z (f32),
    // writtenAt (i64), owner (u32), bytes (u32 length + the engine's own container file). Reliable, any direction.
    MSG_RECORD     = 28,
    // P2 item 1 (SharedWastelandsServer.exe): game -> store HELLO (slot string, protocol u32); store -> game WELCOME (protocol,
    // authority, recordCount - u32 each) then every RECORD; store -> game AUTH (authority u32) when it changes.
    MSG_STORE_HELLO   = 29,
    MSG_STORE_WELCOME = 30,
    MSG_STORE_AUTH    = 31,
    // P3 piece 2 (relations.h): one faction standing - ownerSid, otherSid (strings), relation, trust, trustNeg (f32),
    // flags (u32: 1 ally, 2 atWar), reason (u32: 0 change, 1 snapshot). Reliable, either direction (the owner sends).
    MSG_RELATION      = 32,
    // review-p3s M4: "my world was rebuilt - send me your owned standings again" (no payload). Reliable.
    MSG_RELSYNC       = 33,
    // P4e (decision 26): a group is gone (wiped out / thrown away by the engine): worldId + stamp. Reliable.
    MSG_RECORD_GONE   = 34,
    // decision 28 (F508): the host has written its whole sleeping population into the notebook - the client may clean now. Reliable.
    MSG_WORLD_LISTED  = 35,
    // decision 30: one faction's bitmap of deleted group numbers (faction name + bytes). Reliable.
    MSG_DELETED_BITS  = 36,
    // E22a / decision 38 (approved 2026-09-04): ONE ITEM MOVE on a character this sender OWNS. Live between the
    // two games - NOT the relay - because it is the player's action as it happens, not a stored note.
    // uid(u32) | op(u8: 0 add, 1 remove, 2 quantity, 3 split) | section(string) | x(i32) | y(i32) | quantity(i32)
    // and, for ADD only, the engine's own 0x2A record fields: base data sid, company sid, material sid,
    // color sid (strings) | quality, charges (f32) | item function, level, unique (i32).
    // `quantity` MEANS A DIFFERENT THING PER OP - the table is in items.h and it is not decoration.
    MSG_ITEM_MOVE     = 37,
    // E22b / decision 38 part (2): a player here touched an inventory this game does NOT own (looting a ghost's
    // body, giving to or taking from the other player's character). The local move has been UNDONE and this
    // asks the owner to make it for real. The owner's own hooks then ring and the ordinary MSG_ITEM_MOVE
    // carries the result back, so this message never writes an inventory by itself.
    // id(u32) | dir(u8: 0 take from the owner, 1 give to the owner) | ownerUid(u32) | ownerSection(string)
    // | ownerX(i32) | ownerY(i32) | quantity(i32) | takerUid(u32) | takerSection(string) | takerX(i32) | takerY(i32)
    // and, for dir 1 GIVE only, the engine's own 0x2A record fields (the owner has to BUILD the item):
    // base data sid, company sid, material sid, color sid (strings) | quality, charges (f32)
    // | item function, level, unique (i32).
    MSG_ITEM_REQUEST  = 38,
    // E22b - the owner's answer to one MSG_ITEM_REQUEST. `ok` 0 means REFUSED and the requester changes nothing
    // (decision 38's own rule: refuse and let the player retry, never guess at a nearby slot).
    // id(u32) | ok(u8) | quantity(i32) and, when ok is 1, the item's 0x2A record fields in the same order as
    // MSG_ITEM_REQUEST's - the TAKE direction needs them, because only the owner could read the real item.
    MSG_ITEM_CONFIRM  = 39,
    // E22b-2 (P6b). THE TAKER SAYS WHETHER IT LANDED. On a TAKE the owner has already removed the real item
    // and answered ok=1; until this arrives it KEEPS THE OBJECT ALIVE rather than destroying it, so a taker
    // that cannot place it does not cost the item. ok=0 - or no answer within 10 s - and the owner puts it
    // back on its own character, which publishes the ordinary MSG_ITEM_MOVE and restores the taker's ghost.
    //   id(u32) | ok(u8)
    MSG_ITEM_PLACED   = 40,
    // E22b-2 (P6b). THE GIVER COULD NOT LET GO. On a GIVE the owner has already BUILT the item on its own
    // character and published it; if the requester's own copy could not then be removed (it moved during the
    // round trip) the two games would both hold one. This asks the owner to undo its half - it removes and
    // destroys what it built at the recorded slot, which publishes as its own move.
    //   id(u32)
    MSG_ITEM_REVOKE   = 41,
    // E45 / decision 40 (P8e): ONE DOOR'S OPEN/CLOSED STATE, published by the game that HOLDS its
    // patch of map and applied by every other game through DoorStuff::setDoorState 0x298FC0.  Shared
    // as STATE and not as cause, exactly as the weather is: the behaviour that opens and closes doors
    // (NPC task actions, physics setup, town population) runs independently on each game and cannot be
    // made to agree - F632/F633.  OPENING and CLOSING NEVER TRAVEL: they are animation and the
    // receiver derives them.
    //   key(u32 len + bytes: the P7n position key, a gate's carrying the "gate" suffix)
    //   | state(u8: 0 CLOSED, 1 OPEN) | locked(u8) | gen(u32, the publisher's monotonic counter)
    // `locked` is CARRIED AND NOT APPLIED in this build - E37 owns the lock (read-doors 5.4).
    MSG_DOOR_STATE    = 42,
    // P3 (read-parity3 GAP 3, session protocol 46): an NPC speech line, said by the game that AUTHORS the
    // character and shown on its copy through Dialogue::say(text, 0).  RELIABLE.
    //   uid(u32) | len(u16, 0..512) | text bytes      (src/common/saywire.h)
    MSG_SAY           = 43,
    // S1 (read-stats, session protocol 49): a character's 44 saved stat values, sent by the game that DRIVES
    // it when a value's whole number changes or moves by 0.05 or more, and again every ~30 s.  RELIABLE.
    //   uid(u32) | 176-byte block      (src/common/statswire.h)
    MSG_STATS         = 44,
    // crime3 (T275 / F894, session protocol 52): a character's current crime, sent by the game that DRIVES it when it
    // changes; the other game writes it onto its copy so its own witnesses react.  RELIABLE.
    //   uid(u32) | crime(i32) | expiry(f32) | victimUid(u32) | len(u8, 0..96) | faction sid      (src/common/crimewire.h)
    MSG_CRIME         = 45,
    // crime5 (session protocol 53): a character's bounties - kind 0 the owner's whole list (owner -> copies), kind 1 what a
    // guard added to a copy (copy's game -> owner).  RELIABLE.
    //   uid(u32) | kind(u8) | n(u8, 0..16) | n x { len(u8) + faction sid | amount(i32) | crimes(u32) | claimed(u8) | time(f64) }
    //   (src/common/bountywire.h)
    MSG_BOUNTY        = 46,
    // arrest1 (docs/design-arrest.md 3, session protocol 55): the game that DRIVES a body says its carry by the other game's
    // carrier has ended there (the engine let go - waking alone no longer ends it, owner 333 a - or it was awake when the carry was asked), so the carrier's own
    // game puts it down too.  RELIABLE.
    //   body uid(u32) | carrier uid(u32)
    MSG_CARRY_BREAK   = 47,
    // arrest2 (docs/design-arrest.md 3, session protocol 56): a guard on the sender's game caged its COPY of a character the
    // receiver drives; the receiver cages its own character in its own copy of that cage.  RELIABLE.
    // arrest3 (57): kind 2 RELEASE (the sender's engine let its copy out; the owner releases its own character), kind 3 REFUSED
    // (the owner could not cage its character; the guard's game frees its copy) - both with empty keys.
    // par6 fold (80): kind 4 DEATH (copy's game -> owner: a player's one-shot kill of the copy was refused by the copy death
    // gate; the owner kills its own character if alive) - the cage key names the engine caller, no outer key.
    //   uid(u32) | kind(u8) | len(u8) + cage key | len(u8) + outer key      (src/common/prisonwire.h)
    MSG_PRISON        = 48,
    // heal1 (session protocol 58): a medic on the sender's game treated its COPY of a character the receiver drives; the
    // receiver raises its own character's bandageLevel / splint per part to these values (never lowers them).  RELIABLE.
    //   uid(u32) | n(u8) | n x (f32 bandageLevel, f32 splintLevel)      (src/common/treatwire.h)
    MSG_TREAT         = 49,
    // build1-b (docs/design-build1.md 2-3, session protocol 59): a construction the sender's OWN player faction placed; the
    // receiver creates a copy owned by coop-peer at its K2 safe point.  RELIABLE.
    //   kind(u8 1 PLACE) | len(u8) + key | len(u8) + sid | 3 x f32 pos | 4 x f32 rot | u8 complete | f32 progress
    //   | f32 needed | len(u8) + host key | u8 host form | i32 floor | u8 outside | u16 owner number [| u32 nonce]
    //   (src/common/buildwire.h)
    // build1-c (protocol 60): kind(u8 2 STATE) | len(u8) + key | f32 progress | f32 needed | u8 complete | u8 n (<= 16)
    //   | n x f32 delivered materials - the builder's whole construction state, written onto the copy at the K2 safe point.
    MSG_BUILD         = 50,
    // names1 (session protocol 64): the owner's character name, sent after every SPAWN it sends and when the owner renames
    // one of its characters (Character::setName 0x5CB840 hook); the receiver sets it on its copy.  RELIABLE.
    //   uid(u32) | len(u8 <= 64) | len bytes, UTF-8 as the engine stores them      (src/common/namewire.h)
    MSG_NAME          = 51,
    // slave1 (session protocol 65): the owner's SlaveStateEnum for one character, sent after every SPAWN it sends and when
    // the owner's engine changes it (setSlaveState 0x5A3EB0 hook; periodicUpdate 0x5A44C0 direct writes); the receiver
    // sets it on its copy at the K2 safe point and refuses the copy's own changes.  RELIABLE.
    //   uid(u32) | state(u8 0..3) | ownerUid(u32; P11 protocol 101: 0 none, 0xFFFFFFFF not a replicated character)
    //   (src/common/slavewire.h)
    MSG_SLAVE         = 52,
    // par1 (session protocol 66; docs/design-loot2.md rev 2, 0.4): box parity, PULL. RELIABLE, on channel 0 with ITEM_MOVE.
    // REQ: a game that does not hold a sector asks its holder about every box there, once per zone arrival:
    //   i32 sx | i32 sy | u16 n | n x (u8 len + P7n key, u32 digest, u32 itemCount)      (src/common/paritywire.h)
    // BOX: the holder's answer, one box per message: u8 status | u8 len + key | u32 digest | u16 n | n x item (the
    //   ITEM_MOVE add fields plus section / x / y / quantity); the asker swaps its box for the items.
    MSG_PARITY_REQ    = 53,
    MSG_PARITY_BOX    = 54,
    // recruit1 (session protocol 67; docs/design-recruit1.md 4): hiring a recruit the other game runs. RELIABLE.
    //   u8 kind 1 REQ / 2 OK / 3 NO / 4 DONE | u32 reqId | u32 uid | u32 hirerUid | i32 price | u8 joinType | u8 reason | u8 taken
    //   | u8 joined | u8 len + faction name      (src/common/hirewire.h)
    MSG_HIRE          = 55,
    // mmo5 (session protocol 77; e47-mmo-design.md 6): the HOST is leaving on purpose (the `leave` verb, or its exit), sent
    // just before it closes the link. No payload. RELIABLE. A joiner pauses, saves and shows the host-left window.
    MSG_SESSION_CLOSING = 56,
    // T-1 B1 restructure (session protocol 85): each game announces, for every NPC squad it runs members of, the acting leader its
    // engine chooses, the formal leader (+0xA0) and the members it runs. RELIABLE.
    //   u32 squadKey | u32 acting | u32 formal | u32 seq | i32 cats | u8 hasCats | u16 n | n x u32 member uid   (src/common/squadlead.h)
    //   T-1 B3 restructure (protocol 89): cats = the squad's money, from the game that runs its formal leader (hasCats 1).
    MSG_SQUAD_LEAD    = 57,
    // 58 was MSG_KEEPER (protocol 88) - retired by protocol 89: the squad's money rides MSG_SQUAD_LEAD.
    // P26 stages 1-3 (session protocol 98; .modding/investigations/p26-npc-dialogue.md Q3): an NPC's conversation with the OTHER
    // player's character. RELIABLE. u8 kind 1 PROMPT (NPC owner -> target owner) / 2 ANSWER (back) / 3 END (NPC owner -> target
    // owner) | u32 convId | u32 npcUid | u32 targetUid | the kind's fields   (src/common/talkwire.h)
    MSG_TALK          = 59,
    // P11 (session protocol 101; p11-design.md 2b): a slaver on the captor's game processed the other game's character (a
    // copy there). CAPTURE (captor -> the victim's owner): what the captor's engine wanted to do to the copy, caught and not
    // done; CAPTURE_DONE (owner -> captor): what the owner did, with the stripped items for the slaver. RELIABLE.
    //   (src/common/capturewire.h)
    MSG_CAPTURE       = 60,
    MSG_CAPTURE_DONE  = 61,
    // P11 f3 (session protocol 101): CAPTURE_PLACED (captor -> the victim's owner): which of the DONE's taken rows landed in the
    // captor's slaver. The owner holds the taken objects until it (or a link loss) and then destroys those / puts the rest back.
    // RELIABLE. (src/common/capturewire.h)
    MSG_CAPTURE_PLACED = 62,
    // P104 fix (session protocol 104): SHOT (the shooter's owner -> the victim's owner): a crossbow bolt our character fired
    // struck the other game's character (a COPY here, whose engine damage C2-b skips). The owner plays the hit through
    // MedicalSystem::addWound. RELIABLE. victim u32 | shooter u32 | the bolt's rec 4 x f32 | flags u8   (src/common/shotwire.h)
    MSG_SHOT          = 63,
    // M5b (session protocol 107): PEER_SLOT (each game -> its session peer): this game's permanent notebook slot, once the
    // session is ready and again whenever it changes. The receiver keys its owner records by it. RELIABLE, INLINE. u32 slot
    MSG_PEER_SLOT     = 64,
    // T-327 (session protocol 111; effect-on-copy-design-2026-09-30.md): EFFECT - an effect on another game's character is a
    // request to its owner. REQUEST (the actor's owner -> the target's owner, routed by the target's owner slot: LIVE route SLOT
    // when the notebook road is up, else the session link) / ANSWER (back on the road the request came by). RELIABLE.
    //   dir u8 | kind u8 | reqId u32 | target u32 | actor u32 | kind fields   (src/common/effectwire.h; kind 1 EAT only so far)
    MSG_EFFECT        = 65,
    // P25 fold 2 (session protocol 115; T776 FAIL): INSIDE (the owner -> every game holding a copy, the character stream's road:
    // AREA on the notebook, the session link while it is down): whether the owner's OWN player-faction character is in a
    // building and which. Sent on change and refreshed every 5 s (as INTENT). The receiver keeps that building's inside loaded
    // on this word only (replicate.cpp InteriorKeepTick). RELIABLE.
    //   uid u32 | flags u8 (bit0 inside) | keyLen u8 | key bytes (the P7n position key)   (src/common/insidewire.h)
    MSG_INSIDE        = 66,
    // 67 (MSG_HANDBACK, M7a3 fold 7) is RETIRED by M7a A1 build 2 (protocol 125) and is never reused.
    // M7a A1 build 2 (session protocol 125) [a1b2-tr0]: RELEASE (giver -> ONE candidate, LIVE SLOT, src/common/liveowner.h 1.3): this
    // game's engine put its own people away - offered for adoption; flag REVOKE (bit1) tells a late adopter the winner.
    // u32 id | u32 key | u16 sector | u8 flags | u8 n | u16 winner | u16 0 | n x {XferMember 48 bytes, u32 gen}. RELIABLE.
    MSG_RELEASE       = 68,
    // RELEASE_ACK (candidate -> giver, the XFER_ACK's road): u32 id | u16 nAdopted | u16 nDropped | u16 nDeferred | the uid lists. RELIABLE.
    MSG_RELEASE_ACK   = 69,
    // M7a A1 build 1 (session protocol 118) [a1b1-tr0]: ROSTER (src/common/liveowner.h, one type, three kinds, per SECTOR): HASH
    // (owner -> WORLD every 5 s and at a notebook WELCOME: u8 kind | u8 0 | u16 chunk | u32 rosterSeq | u16 n | u16 lastChunk |
    // n x {u16 sector, u16 count, u64 hash}), CHECK (copy holder -> owner, LIVE SLOT: the copies it holds as that owner's, {uid, gen})
    // and ANSWER (owner -> asker, LIVE SLOT: only the rows that differ - LIVE / MOVED / PENDING / NOT_LIVE). Chunked under 65,536 bytes.
    MSG_ROSTER        = 70,
    // M7a A1 build 1 [a1b1-tr1]: RECEIPT (receiver of an UNLOAD / DESPAWN that listed its slot -> the sender, LIVE SLOT, one per
    // SessionTick per sender): u16 n | u16 0 | n x u32 seq.
    MSG_RECEIPT       = 71,
    // MOVESTOP (owner -> every game holding a copy, the character stream's road, AREA): a character the owner streams (its
    // player faction or an NPC) STOPPED here - the engine's moving flag reads 0 and the body has stood still two frames. Stamped with the owner's clock,
    // like MOVE, so the copy orders it against MOVE by owner time. uid u32 | x, y, z f32 | stamp u32 (ms) = 20 bytes. RELIABLE.
    MSG_MOVESTOP      = 72,
    // T-354 (session protocol 130; 66 / 115 on its branch, renumbered at the merge of main 22ca5c91 - 66 is INSIDE there): NOT_SHOWN
    // (the copy's game -> the character's owner, on the road the SPAWN came by): this game's character table refused the copy, so the
    // character is not shown here. The owner counts it; nothing else changes.
    // RELIABLE.  uid u32 | reason u8 | the sender's refusal count u32      (src/common/uidtable.h H)
    MSG_NOT_SHOWN     = 73,
    // RESEND (a copy's game <-> the character's owner). ASK: this game lost its copy of the owner's
    // character (or never got one) while the character stands in an area loaded here - send it again. ANSWER: the owner's verdict per
    // uid; for SENT the full state went to the asking game alone first. The ask goes by the owner's road (effectwire.h
    // EffectRequestRoad), the answer back on the road the ask came by.
    // RELIABLE.  kind u8 | n u8 | n x (uid u32 | verdict u8)      (src/common/lostcopy.h)
    MSG_RESEND        = 74,
    // T-619: TOWN_PRICES (the world's ONE price source - townprice::PriceSource: the lowest IN_WORLD roster slot, or the session host
    // with no roster - to every other game, each by one road: the session link to its session peer, the world server to the rest):
    // one town's per-item local trade multipliers as the source's engine rolled them, once per (arrival epoch, world load) of the
    // source (a game whose own source answer names the sender only after a copy arrived keeps that copy and files it then).
    // Every other game answers Town::getLocalTradePriceMult from the source's tables,
    // so every game quotes one price at one counter. RELIABLE.
    //   'TPR1' | u32 the source's world generation | u8 len + town stringID | u16 n | n x {u8 len + item stringID, f32}   (src/common/townprices.h)
    MSG_TOWN_PRICES   = 75
};

enum Channel
{
    CH_RELIABLE   = 0,
    CH_UNRELIABLE = 1
};

// One decoded inbound message, owned by the caller after Poll().
struct Message
{
    MsgType           type;
    unsigned int      peer;      // sender's peer id (host is always 0)
    std::vector<char> payload;   // message body, no header
};

// Connection state, reported to the user rather than inferred.
enum LinkState
{
    LINK_DOWN = 0,
    LINK_LISTENING,
    LINK_CONNECTING,
    LINK_UP,
    LINK_FAILED
};

// Backend identity - for display and diagnostics ONLY. No behaviour may branch on it.
enum BackendId
{
    BACKEND_NONE = 0,
    BACKEND_DIRECT,   // ENet over UDP: LAN / direct IP
    BACKEND_STEAM     // post-PoC, equal citizen
};

class ITransport
{
public:
    virtual ~ITransport() {}

    // Host: begin accepting one peer. Join: connect to an address.
    // Both are non-blocking; watch State() / poll for MSG_WELCOME.
    virtual bool Host(unsigned short port, std::string* errOut) = 0;
    virtual bool Join(const std::string& address, unsigned short port, std::string* errOut) = 0;

    virtual void Disconnect() = 0;

    /* B12-f (review-b12e H-2) - TEAR DOWN WITHOUT WAITING FOR A HANDSHAKE THAT CANNOT COME.
       Disconnect() is the GRACEFUL close: it flushes, sends a disconnect and then services the socket for up
       to ~300 ms on the MAIN THREAD waiting for the peer's acknowledgement (F069's fix, and right for a leave
       a peer is there to hear). On a socket that is merely CONNECTING - the notebook re-dial's ordinary case -
       enet_peer_disconnect takes its not-yet-connected branch and resets the peer with no DISCONNECT event to
       wait for, so that whole ~300 ms is spent for nothing, once per re-dial attempt, in the frame loop.
       Abort() is the abrupt close for exactly that case: reset the peer, drop the host, no service wait.
       DEFAULTED rather than pure, like PeerCount() and ConnectGen() above, so a backend that has nothing
       cheaper than Disconnect() keeps compiling and keeps behaving. */
    virtual void Abort() { Disconnect(); }

    // Called from the MAIN-THREAD pump only (spec section 5: all game-state mutation is
    // main-thread; for the PoC the transport is serviced there too - a dedicated network
    // thread is a later optimization behind this same interface, not a redesign).
    virtual void Poll(std::vector<Message>* out) = 0;

    virtual bool Send(unsigned int peer, MsgType type, const char* data, size_t len, Channel ch) = 0;

    // Push anything queued onto the wire now. Only needed when a message must leave before
    // the next Poll() - notably a BYE sent immediately before disconnecting (F069).
    virtual void Flush() = 0;

    virtual LinkState  State() const = 0;
    virtual BackendId  Backend() const = 0;
    virtual const char* BackendName() const = 0;

    /* P7f (review-p6z H-1): THE BACKEND'S OWN CONNECTED-PEER COUNT, not our cached LinkState mirror. NOT pure -
       a backend that cannot answer says 0 rather than failing to compile, because this is a diagnostic and no
       behaviour branches on it. Note what it does NOT buy: like `state_`, ENet's counter only moves inside
       enet_host_service, so a caller that is not servicing the socket cannot see a drop with either number. */
    virtual unsigned int PeerCount() const { return 0; }
    /* T-355 fold 2: the transport id of the ONE peer on the session link while the link is up (out), false otherwise - the id
       Message::peer carries for its messages. Main thread (the poll's). A backend that cannot answer says false. */
    virtual bool CurrentPeerId(unsigned int* /*out*/) const { return false; }

    /* P7v (review-p7h M-5, design-noworld-queue 5.2) - A MONOTONIC CONNECT/DISCONNECT COUNTER, so a caller
       can see an EDGE that State() cannot show it. ENet's Poll can drain a DISCONNECT and a CONNECT in ONE
       call, leaving `state_` at LINK_UP with no edge visible: that is the drop-then-reconnect-inside-the-gate
       case, which a state comparison cannot see and a counter cannot miss. It is what lets the session pump
       run the link edges - so a link raised inside a load gate SENDS ITS HELLO instead of burning the whole
       8 s waiting for a WELCOME nothing asked for (review-p7h H-4, review-p7q H-3(b)).
       PROCESS-LOCAL: nothing about this goes on the wire. NOT pure - a backend that cannot answer returns 0
       rather than failing to compile, for the same reason PeerCount() does. */
    virtual unsigned int ConnectGen() const { return 0; }

    /* link1 (T416): ENet peer timeouts for every peer this transport CONNECTS from now on (0 = ENet's own default for
       that term). The session sets them on its game link; the notebook link never calls this and keeps the defaults.
       A backend without the notion ignores it. */
    virtual void SetPeerTimeouts(unsigned int /*limit*/, unsigned int /*minimumMs*/, unsigned int /*maximumMs*/) {}

    /* addr1 - THE ADDRESS THIS GAME'S SESSION LINK ACTUALLY REACHED, as the backend resolved it (a joining game's
       one peer, the host). Dotted text, or empty when the backend cannot say or there is no peer. Used by OnWelcome
       to replace a loopback notebook address with the host's real one. PROCESS-LOCAL: nothing goes on the wire. */
    virtual std::string PeerHostIp() const { return std::string(); }

    /* ui1 (owner 2026-09-27, connection trouble step 2): ms since this transport last received ANYTHING from its one peer (a
       joining game's host) on the backend's own clock - ENet: enet_time_get() - peer->lastReceiveTime, which ENet refreshes on
       every command the host sends, its acknowledgements included (protocol.c:913), so a live link keeps it fresh.
       0 when no peer is up or the backend cannot say. Like state_, it only moves while the socket is serviced (Poll). */
    virtual unsigned int HostSilenceMs() const { return 0; }
    /* M16 (T-197): what waits to be sent on this link (src/common/sendbound.h) as one REPORT token; empty for a backend without the bound. */
    virtual std::string SendBoundToken() { return std::string(); }
    /* M16 fold 2: this link is the game's link to the world server - type 53 is a LIVE envelope on it (on the session link
       it is MSG_PARITY_REQ and must-deliver). A no-op for a backend without the bound. */
    virtual void SetStoreLink() {}
    /* M13: bundling on this link on (true) or off - the messages handed over during one pass leave as one packet per lane
       (src/common/sendbundle.h). The store layer turns it on at a WELCOME of the same world-server protocol; the link turns
       it off itself at every connect and disconnect. A no-op for a backend without it, and for the session link. */
    virtual void SetBundling(bool /*on*/) {}
    /* M13: what bundling did on this link as one REPORT token (" bundle[...]"); empty for a link that never bundles. */
    virtual std::string BundleToken() { return std::string(); }

    /* What this link still holds undelivered: messages handed to it and not yet sent (those waiting in M16's bound included),
       and guaranteed ones sent but not yet acknowledged - `packets` pieces (a large message travels in several) of `bytes` in all. A sender of bulk data that can
       wait reads it to send only while the link is nearly idle. false = no peer is up or the backend cannot say. */
    virtual bool SendQueued(long long* /*packets*/, long long* /*bytes*/) const { return false; }

    // Diagnostics for the netstat command (round-trip ms, counts).
    virtual unsigned int RoundTripMs() const = 0;
    virtual unsigned long long SentCount() const = 0;
    virtual unsigned long long RecvCount() const = 0;
};

// Factory: the player's choice, made explicit. No default argument, deliberately.
ITransport* CreateTransport(BackendId which);

const char* MsgTypeName(MsgType t);
const char* LinkStateName(LinkState s);

} // namespace net
} // namespace coop
