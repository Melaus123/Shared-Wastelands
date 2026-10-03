/* src/common/squadwriter.h - T-1 B1 (owner decisions 104 and 110, 2026-09-28): ONE GAME RUNS EACH NPC SQUAD, and it is the
 * game that runs the squad's leader. The pure decisions only: no engine memory, no Windows, no globals. The plugin
 * (handoff.cpp) and the offline suite (coop-test) compile the same functions. C++03 (VS2010 v100).
 *
 * WHY. Kenshi sleeps and wakes a squad WHOLE by the squad position, which is its leader's (Read, decompile 2026-09-28:
 * 0x4FE2C0 / 0x4FF930 store it at ActivePlatoon+0xB4; the stay-awake test 0x4FE140 resets ActivePlatoon+0xB0 to 4.0 s
 * while that position is inside a loaded or loading zone box, and at zero the squad sleeps). Deciding the running game
 * per member by the member's own position split a squad that straddled the two players' areas between the two games.
 *
 * XFER REASON (protocol 84). The XFER message's second u32, written 1 by every sender since protocol 25 ("forced"), is a
 * reason code: 0 not forced (never sent today), 1 forced (the owner is about to lose the squad - its old meaning, kept),
 * 2 follow-leader (members handed to the game that runs their squad's leader).
 */
#ifndef COOP_COMMON_SQUADWRITER_H
#define COOP_COMMON_SQUADWRITER_H

namespace coopsquad {
/* M7a A1 build 2 [a1b2-sw0]: the put-away / owed / hand-back / mark decisions are RETIRED (design 2.2) - their replacements are in
   liveowner.h (AdoptDecide, ReceiverRingPick, ReleaseStep, GiverSettleOnAck, SquadGivenEffective). */

enum { kXferReasonNone = 0, kXferReasonForced = 1, kXferReasonFollowLeader = 2 };

inline const char* XferReasonName(unsigned int r)
{
    return r == kXferReasonNone ? "none" : (r == kXferReasonForced ? "forced" : (r == kXferReasonFollowLeader ? "follow-leader" : "unknown"));
}

/* What the game that runs some members of a non-player squad does with them in its follow pass.
   leaderHasUid: the squad leader (ActivePlatoon+0xA0) carries a replicated uid here; leaderRunHere: this game runs that uid;
   inFlightOrBackoff: an XFER keyed by that leader is already waiting for its ACK, or the last follow for it was refused
   moments ago. "uid, else position": a leader with no uid leaves the squad to the position rule. */
enum { kFollowByPosition = 0, kFollowStay = 1, kFollowSend = 2, kFollowWait = 3 };
inline int FollowAction(int leaderHasUid, int leaderRunHere, int inFlightOrBackoff)
{
    if (leaderHasUid == 0) return kFollowByPosition;
    if (leaderRunHere != 0) return kFollowStay;       /* the members already follow this game; the position rule still applies to the squad */
    return inFlightOrBackoff != 0 ? kFollowWait : kFollowSend;
}

/* The receiver's test for ONE member of an XFER. When this game runs, or is taking in this very message, the squad's
   leader and the squad position is known, the SQUAD position decides for every member (the engine sleeps the squad by it);
   otherwise each member's own position decides, as before. 1 = take, 0 = refuse (the sender keeps it). */
inline int XferTakeMember(int leaderHereOrTaking, int squadPosKnown, int squadInRing, int memberInRing)
{
    if (leaderHereOrTaking != 0 && squadPosKnown != 0) return squadInRing != 0 ? 1 : 0;
    return memberInRing != 0 ? 1 : 0;
}

/* Owner 110: this game keeps a copy squad awake (refreshes its ActivePlatoon+0xB0 stay-awake timer) when EVERY member is a
   copy of a character the other game runs, at least one member stands on ground loaded here, and it is not a player's squad
   (a player's squad never sleeps and never changes hands). 1 = refresh. */
inline int KeepCopySquadAwake(int allMembersPeerCopies, int anyMemberOnLoadedGround, int isPlayerSquad)
{
    if (isPlayerSquad != 0) return 0;
    return (allMembersPeerCopies != 0 && anyMemberOnLoadedGround != 0) ? 1 : 0;
}

/* The squad position as read (ActivePlatoon+0xB4..+0xBC) is usable only when finite and not the untouched (0,0,0) of a
   squad the engine has not updated yet. By the bits (the plugin builds with /fp:fast). */
inline int SquadFloatFinite(float f)
{
    union { float f; unsigned int u; } b; b.f = f;
    return (b.u & 0x7F800000u) != 0x7F800000u ? 1 : 0;
}
inline int SquadPosUsable(float x, float y, float z)
{
    if (!SquadFloatFinite(x) || !SquadFloatFinite(y) || !SquadFloatFinite(z)) return 0;
    return (x != 0.0f || y != 0.0f || z != 0.0f) ? 1 : 0;
}

/* T-164 B4-3 (M4, owner 125-130): WHICH POSITION DECIDES WHO RUNS A SQUAD. A trader squad (the engine's getIsTrader) whose
   formal leader has a home building is decided by that building's position - the position its restock (ItRestockHolder) and
   its shop stock (the building's indoor pieces with an inventory) are decided by - so ONE game runs the keeper, holds its stock
   and restocks it. Every other squad: its own position when usable (T-1 B1), else none (the caller keeps the character's). */
enum { kDecideNone = 0, kDecideSquad = 1, kDecideHome = 2 };
inline int SquadDecisionChoice(int traderWithHomeRead, int squadPosUsable)
{
    if (traderWithHomeRead != 0) return kDecideHome;
    return squadPosUsable != 0 ? kDecideSquad : kDecideNone;
}

/* T-1 B1 restructure (re-check N1-N5 of ff655c5, 2026-09-28): ONE AGREED LEADER PER SQUAD, FROM ONE ANNOUNCED PAIR. Each engine
   picks its own squad leader (+0xA0; it re-picks members[0] on removal 0x794C00 / add 0x7922C0) and acting leader (+0xA8 when
   +0xA0 cannot lead), and the two games' engines can disagree (Confirmed T528). Each game ANNOUNCES the acting leader its own
   engine chooses (EngineLeaderChoice, computed once) for every squad it runs members of (MSG_SQUAD_LEAD, squadlead.h), and the
   other game uses that value AS IS - it never re-judges "standing" on its copies (N1: copy state may lag). So both games hold the
   SAME two inputs:
     mineLead - the acting leader this game announced (its engine's choice, any uid, 0 = none);
     peerLead - the acting leader the other game announced for its part of the squad (0 = none / not heard).
   Game A evaluates (a, b) and game B evaluates (b, a); the rule is symmetric, so both land on the same uid, and only the game
   that does NOT run it sends. RULE: a named leader wins over none; two different named leaders -> the LOWER uid. Why the lower
   uid: it is the one input both games hold verbatim (a uid names the same character on both games), it needs no clock, and it
   does not change while both announcements stand. SquadFollowLeader's engine-only fallback (a leader the other game runs that it
   did not name) is gone: the announced value already names the engine's leader when it is a character the other game runs. */
inline unsigned int AgreedSquadLeader(unsigned int mineLead, unsigned int peerLead)
{
    if (mineLead == 0) return peerLead;
    if (peerLead == 0) return mineLead;
    return mineLead < peerLead ? mineLead : peerLead;
}

/* T-1 B1 restructure (point 4): CROSSING follow transfers. While announcements lag, each game can send its members of one squad
   to the other at once (a swap). A game that receives a follow-leader XFER keyed incomingLeader (a leader it runs) while its own
   follow-leader XFER for the same squad, keyed myPendingLeader (a leader the other game runs), still waits for its ACK, takes the
   incoming one only when ITS leader is the lower uid: the transfer whose DESTINATION runs the lower leader proceeds. The other
   game evaluates the same test with the two keys swapped, so exactly one of the two is taken when the keys differ, and neither
   when they are equal (nothing moves; both retry after the follow backoff). 1 = take, 0 = refuse (the ACK names none). */
inline int CrossingTakeIncoming(unsigned int incomingLeader, unsigned int myPendingLeader)
{
    return (incomingLeader != 0 && incomingLeader < myPendingLeader) ? 1 : 0;
}

/* T-1 B1 fold (review M1): the leader the ENGINE acts on. 0x791FF0 (getSquadLeader - never called here: it WRITES +0xA8) skips
   a dead or bed/cage-held (+0x2F8) +0xA0 and writes the acting leader +0xA8; with no acting leader 0x4FE2C0 averages the
   members. Mirror: +0xA0 when present and standing; else +0xA8 when usable (a member, itself standing); else none (the
   position rule). */
enum { kEngineLeadA0 = 0, kEngineLeadA8 = 1, kEngineLeadNone = 2 };
inline int EngineLeaderChoice(int a0Present, int a0Out, int a8Usable)
{
    if (a0Present != 0 && a0Out == 0) return kEngineLeadA0;
    if (a0Present != 0 && a8Usable != 0) return kEngineLeadA8;
    return kEngineLeadNone;
}


/* M7a3f3 H1 [m7a3f3-sw0]: WHAT THE OTHER GAMES WERE TOLD ABOUT ONE OF OUR CHARACTERS - the three states of worldsync.cpp's g_announced
   (AnnouncedState). kAnnNoEntry: no entry - a character this game TOOK by hand-over (the take path writes none; the giver's game holds
   its copy) or a row from before the announce gate; kAnnWithheld: an EXPLICIT 0 - its SPAWN was withheld or its UNLOAD went, so the
   other games have no copy; kAnnAnnounced: an explicit 1. Only kAnnWithheld means "the other games were never told". The values 0 and 1
   keep the meaning the former 0/1 argument had. */
enum { kAnnNoEntry = -1, kAnnWithheld = 0, kAnnAnnounced = 1 };
inline int AnnouncedToOthers(int annState) { return annState != kAnnWithheld ? 1 : 0; }



/* M7a3f3 T-425 [m7a3f3-sw3]: A SQUAD THIS GAME HANDED OVER. After this game hands a squad to another game, its own world data still holds
   that squad; when its engine rebuilds it (a walk-back, a wake, an in-ring put-away's squad re-created) nothing matched the rebuilt
   people to the other game's copies (T735: two of each person for ~40 s). This game keeps the set of squads it handed over, keyed by
   (faction, the engine's own squad id - the id CONTEXT carries and that survives save / load).
   PrepareContextForSpawn: an id minted in MY block is my own squad's (inv7a3: refuse the supersede) - unless I handed that squad over:
   the announcement is then the running squad and my awake / sleeping copy is the stale one. 1 = refuse the supersede. */
inline int SupersedeOwnIdAfterHandover(int ownBlock, int handedOver)
{
    return (ownBlock != 0 && handedOver == 0) ? 1 : 0;
}
/* The wake hook: the wake of a squad I handed over is refused while another game holds its area (heldByOther, HeldByOtherTS at its file
   position: 1 held, 0 not, -1 no fresh map), another player is in this world (othersPresent: store.cpp PlayersPresent - the old link or
   the world server's roster) and it is a world faction's squad - the T-300 treatment (back on ice, thrown away on the next tick).
   1 = refuse. */
inline int HandedOverWakeRefuse(int handedOver, int heldByOther, int othersPresent, int worldFaction)
{
    return (handedOver != 0 && heldByOther == 1 && othersPresent != 0 && worldFaction != 0) ? 1 : 0;
}
/* THE STALE-SQUAD DROP QUEUE's first test (store.cpp T300DropTick). A drop of another game's file or of a squad this game handed over
   exists because another player runs those people live here, so it is cancelled while no other player is in this world
   (othersPresent 0). A context platoon's drop (its wake was refused whoever holds the area: it would rebuild another game's people from
   its own sleeping data) does not depend on who is present - it goes on to its own re-check either way. 1 = go on to the re-check. */
inline int StaleDropGoesOn(int isContextDrop, int othersPresent)
{
    return (isContextDrop != 0 || othersPresent != 0) ? 1 : 0;
}
/* THE STALE-SQUAD SWEEP (store.cpp T300SweepTick), armed when another player enters this world (the arrival record: the world server's
   roster or the old link). armed: a sweep is owed; worldReady: a running world this game may write to; othersPresent: another player is
   in this world now. The sweep waits for a world before it asks who is present, so an arrival seen while the world still loads is
   not lost. */
enum { kStaleSweepIdle = 0, kStaleSweepWait = 1, kStaleSweepDisarm = 2, kStaleSweepRun = 3 };
inline int StaleSweepStep(int armed, int worldReady, int othersPresent)
{
    if (armed == 0) return kStaleSweepIdle;
    if (worldReady == 0) return kStaleSweepWait;
    return othersPresent != 0 ? kStaleSweepRun : kStaleSweepDisarm;
}
/* The sweep: a rebuilt non-player person of a squad I handed over is never adopted while another game runs a live copy of that squad
   here, or while the area is not known to be mine alone (heldByOther 1, or -1 no fresh map) - it is noted for the orphan clean-up.
   A fresh map with no other holder and no live copy here: adopted, and the squad is mine again (unmarked). */
enum { kHoSweepAdopt = 0, kHoSweepSkip = 1, kHoSweepAdoptUnmark = 2 };
inline int HandedOverSweepAction(int handedOver, int peerCopyHere, int heldByOther)
{
    if (handedOver == 0) return kHoSweepAdopt;
    if (peerCopyHere != 0 || heldByOther != 0) return kHoSweepSkip;
    return kHoSweepAdoptUnmark;
}

/* PrepareContextForSpawn: the awake retire of this game's own platoon whose id is mine but whose squad I handed over (handedOverOnly)
   is refused when that platoon holds a person this game runs (mineHere > 0) or cannot be read (-1). 1 = refuse. */
inline int HandedOverRetireRefuse(int handedOverOnly, int mineHere)
{
    return (handedOverOnly != 0 && mineHere != 0) ? 1 : 0;
}

/* M7a2 [m7a2-sw1] (T803: 220 people in two owed records at once - the squad record the pass made keyed on its leader, and a record of
   one the put-away made once the leader's row was gone - both sent at the resume, each person taken twice, the second ACK counted
   handedOver noKey): ONE PERSON IS IN AT MOST ONE OWED RECORD AND IN AT MOST ONE HAND-OVER IN FLIGHT. A person about to go into an owed
   record (toFlight 0) or into an XFER about to be sent (toFlight 1) is looked up first: inOtherRecord = an owed record under another key
   holds it; inOtherFlight = an XFER in flight under another key names it.
   Into a record: held by another record -> SKIP (the first record keeps it: the squad record, made while the squad was live, keeps its
   members when the engine puts them away); named by a flight only -> PUT (the record holds it - its UNLOAD stays held - and the record's
   resume does not send it while that flight is out).
   Into an XFER: held by another record or named by another flight -> HOLD (the owed resume keeps it in its own record; the put-away
   flush owes it instead of sending it) until that record or flight settles it; else PUT. */
enum { kHoAddPut = 0, kHoAddSkip = 1, kHoAddHold = 2 };
inline int HandoverAddAction(int toFlight, int inOtherRecord, int inOtherFlight)
{
    if (toFlight == 0) return inOtherRecord != 0 ? kHoAddSkip : kHoAddPut;
    return (inOtherRecord != 0 || inOtherFlight != 0) ? kHoAddHold : kHoAddPut;
}
/* M7a2 fold 3 [m7a2h-sw0] (T807 GAP 1: noKey 0 -> 4 at A 779-821, 65 forced sends / 65 ACKs, none owed): THE PASS'S OWN SENDS - the
   per-second forced XFER and the follow-leader XFER - checked only their own key in flight, so a live person could be in two flights
   (the agreed leader, or the squad's key, changed between seconds): the receiver answered already-ours, this game released twice and
   the second release found no member note. Now each member is asked HandoverAddAction(1, 0, another XFER in flight names it): HOLD ->
   left out of this XFER until that flight settles. FLIGHTS ONLY (re-check 2): an owed record is not a flight - a release takes a
   person out of every record, and the resume drops a member live here (the pass decides) and holds back one a flight names - while a
   record waiting for a holder change could hold a live person back for a long time. kept = members left to send: none -> not sent. */
enum { kPassSendGo = 0, kPassSendSkipEmpty = 1 };
inline int PassSendAfterHold(int kept)
{
    return kept > 0 ? kPassSendGo : kPassSendSkipEmpty;
}
/* M7a2 [m7a2-cx0] (T803, 2026-10-02): A CONTEXT PLATOON IS NEVER FILLED OR WRITTEN FROM A STORE RECORD. A context platoon is the
   squad this game builds for another game's announced squad (spawn.cpp PrepareContextForSpawn); its people arrive by SPAWN, always.
   In T803 every LOAD-OVERRIDE (A 138/138, B 111/111) fired as the first puppet went into a new context platoon: the engine built a
   second, unregistered set of the people from the record beside the spawned puppets, and the orphan clean-up removed them.
   Load and wake: a newer record is not applied to a context platoon (skip). */
enum { kCtxRecApply = 0, kCtxRecSkip = 1 };
inline int ContextRecordApply(int isContext)
{
    return isContext != 0 ? kCtxRecSkip : kCtxRecApply;
}
/* Sleep (and the heartbeat's publish, the same table): a context platoon none of whose people this game runs is never written (they
   are another game's puppets); nor is a squad this game marks HANDED OVER whose KNOWN members include none of this game's (T803 A
   808.376: "SLEEP wrote chars=21 known=21 mine=0" published B's people). Not plain mine == 0: at some sleeps the uids are already
   gone and known is 0 - that is why rule E2 (a squad numbered in my block is mine) exists, and it still decides those.
   M7a2 fix-2 (manager): a context platoon holding a person this game runs (mine > 0 - e.g. after a take-back) is NOT refused - that
   would stop this game's own people being saved; it goes on to the existing rules (E2 decides, as before this patch) and is counted
   (kCtxSleepGoContextWithMine). Anything else goes on to the existing rules too (kCtxSleepGo). */
enum { kCtxSleepGo = 0, kCtxSleepRefuseContext = 1, kCtxSleepRefuseHandedOver = 2, kCtxSleepGoContextWithMine = 3 };
inline int ContextSleepWrite(int isContext, int isHandedOver, int known, int mine)
{
    if (isContext != 0) return mine > 0 ? kCtxSleepGoContextWithMine : kCtxSleepRefuseContext;
    if (isHandedOver != 0 && known > 0 && mine == 0) return kCtxSleepRefuseHandedOver;
    return kCtxSleepGo;
}
/* Wake: a context platoon is never woken by this game's engine, whoever holds the area - a wake rebuilds another game's people from the
   platoon's own sleeping data, as unregistered bodies the sweep would adopt beside that game's own. Refused (and dropped) unless its last
   sleep here held a person this game runs (lastSleepHeldMine - the sleep's kCtxSleepGoContextWithMine, e.g. a take-back): that one goes
   on to the existing rules. A refusal needs the squad id to queue the drop; with none the wake goes on and is counted (kCtxWakeNoId). */
enum { kCtxWakeGo = 0, kCtxWakeRefuse = 1, kCtxWakeNoId = 2 };
inline int ContextWake(int isContext, int lastSleepHeldMine, int haveId)
{
    if (isContext == 0 || lastSleepHeldMine != 0) return kCtxWakeGo;
    return haveId != 0 ? kCtxWakeRefuse : kCtxWakeNoId;
}

}   /* namespace coopsquad */

#endif
