/* src/common/zonerec.h - T-609 part 2 (H1, W): two rules for a ZONE RECORD, id zone.<X>.<Y> - one area's level data (its buildings
 * and loose items) as the world server keeps it.
 *
 * H1, THE STAMP (the game's side). The world server refuses a record whose stamp (writtenAt, whole seconds) is not newer than the
 * one it holds (store_main.cpp OnRecordFrom, `>=`), so a zone saved twice in one second used to lose its second save. A zone record
 * is stamped max(now, last + 1), `last` being the newer of the stamp this game last sent for that zone and the stamp of the record
 * it holds for it. The server's refusal of a truly older stamp is unchanged.
 *
 * W, THE WRITER (the world server's side). A zone record is taken only from the game that holds its area on the server's own area
 * map (AreaRow.ownerId - the same map GROUND and BOX records are judged against), compared by PLAYER ID rather than by slot, so a
 * holder that reconnected in another slot is still the holder before its first area report. An area nobody holds is judged by the
 * stamp rule alone (the games' own writer ladder decides it). A record held behind another write of its id (M14) is judged on the
 * sender's player id AS IT ARRIVED (kept with the held message, fold 1 F3), so a save from a game that left meanwhile is still
 * judged; only a sender with no player id at all is taken unchecked. An area row whose player slots.txt does not know (restored
 * from areas.txt, owner slot -1) holds nothing, exactly as for GROUND and BOX records (fold 1 F1, AreaRowHolderId).
 * A refused record is logged and dropped; nothing is sent back (no new message, no protocol change).
 *
 * F2, THE GAME'S SIDE OF A ZONE RECORD THAT ARRIVES (fold 1). A game whose own save was refused (it raced an area hand-over) holds
 * a record stamped newer than the new holder's next copies; the plain not-newer rule kept it, so the two games differed. The game
 * now takes a copy whose owner field (the writer's slot, which the world server stores and relays unchanged) is the area's holder
 * on the game's own AREAMAP even when it is not newer - but only over a record held here from ANOTHER writer, and never its own
 * slot's copy (ZoneRecvJudge). Anything else keeps the not-newer rule.
 *
 * A, A KEPT-IN-STEP COPY (owner decision 911 a). A game that becomes an area's holder may save its copy of that area when the copy
 * was loaded on one link - while no record of the area existed, or from a record - and stayed loaded the whole time another
 * game held it and wrote newer records; a copy loaded from the game's own old save while a record existed stays refused
 * (F480), and a record the game wrote itself never counts. ZoneKeptAdoptJudge names the first condition that fails, or adopt.
 *
 * PURE C++03 (VS2010 v100), header-only: no engine, no OS header. Swept by the offline suite (t609_zone_stamp, t609_zone_holder, t609_area_holder_rule, t609_zone_recv_holder,
 * t609_held_sender, t609_zone_kept_adopt). */
#ifndef ZONEREC_H_INCLUDED
#define ZONEREC_H_INCLUDED

#include <string>
#include <cstdio>

namespace zonerec {

/* H1: the stamp to send a zone record with. now: time(0); lastSent: the stamp this game last sent for the zone (0 = none);
   held: the stamp of the record held here for it (0 = none). *bumped = 1 when the answer is not `now`. */
inline long long ZoneStampNext(long long now, long long lastSent, long long held, int* bumped)
{
    const long long last = (lastSent > held) ? lastSent : held;
    long long s = now;
    if (last > 0 && last >= s) s = last + 1;
    if (bumped != 0) *bumped = (s != now) ? 1 : 0;
    return s;
}

/* W: 1 = `id` is a zone record id (zone.<X>.<Y>, the form the game's ZoneIdOfPath makes) and *sx / *sy are its area; 0 = not one. */
inline int ZoneRecIdParse(const std::string& id, int* sx, int* sy)
{
    if (id.size() < 8 || id.compare(0, 5, "zone.") != 0) return 0;
    int x = 0, y = 0;
    if (std::sscanf(id.c_str() + 5, "%d.%d", &x, &y) != 2) return 0;
    if (sx != 0) *sx = x;
    if (sy != 0) *sy = y;
    return 1;
}

const int kZrAccept = 0;               /* the sender holds the area */
const int kZrAcceptNoHolder = 1;       /* nobody holds the area: the stamp rule decides */
const int kZrAcceptSenderUnknown = 2;  /* the sender has no player id (neither as it arrived nor now): taken, never a holder's save lost */
const int kZrRefuseNotHolder = 3;      /* another game holds the area */

/* W: holderId: the area map's holder (empty = nobody, AreaRowHolderId); senderId: the sending game's player id as the message arrived
   (kept with a held message; empty = not known). */
inline int ZoneJudgeWrite(const std::string& holderId, const std::string& senderId)
{
    if (holderId.empty()) return kZrAcceptNoHolder;
    if (senderId.empty()) return kZrAcceptSenderUnknown;
    return (senderId == holderId) ? kZrAccept : kZrRefuseNotHolder;
}
inline int ZoneJudgeTakes(int why) { return (why != kZrRefuseNotHolder) ? 1 : 0; }
/* fold 1 (F3): the sender ZoneJudgeWrite is given - its id as the message arrived (kept with a held message) wins; its connection's
   id now is the fallback for a message that came with none. */
inline std::string ZoneSenderId(const std::string& idAsArrived, const std::string& idNow) { return !idAsArrived.empty() ? idAsArrived : idNow; }
inline const char* ZoneJudgeWhy(int why)
{
    return why == kZrAccept ? "the sending game holds the area"
         : why == kZrAcceptNoHolder ? "nobody holds the area"
         : why == kZrAcceptSenderUnknown ? "the sending game has no player id - taken, so a holder's last save is not lost"
         : why == kZrRefuseNotHolder ? "the sending game does not hold the area"
         : "unknown";
}

/* fold 1 (F1): WHO HOLDS AN AREA - the world server's one rule for ZONE, GROUND and BOX records. A row holds its area only when it
   names a player (ownerId) AND that player has a slot number (ownerSlot >= 0). A row restored from areas.txt whose player slots.txt
   does not know (store_main.cpp LoadAreas: owner -1) is sent to the games as "nobody holds it", and is judged so. 1 = held. */
inline int AreaRowHeld(const std::string& ownerId, int ownerSlot) { return (!ownerId.empty() && ownerSlot >= 0) ? 1 : 0; }
/* the holder's player id for ZoneJudgeWrite: empty = nobody */
inline std::string AreaRowHolderId(const std::string& ownerId, int ownerSlot) { return AreaRowHeld(ownerId, ownerSlot) != 0 ? ownerId : std::string(); }

/* fold 1 (F2), THE GAME: a zone record that arrived. haveHeld / heldStamp / heldOwner: the record held here for the zone (its owner
   field, -1 unknown); stamp / ownerSlot: the arriving copy's; holderSlot: the area's holder on this game's AREAMAP (< 0 unknown,
   nobody or frozen); mySlot: this game's slot (-1 unknown). */
const int kZvTakeNewer = 0;        /* newer than the record held here, or none held: the rule as before */
const int kZvTakeHolder = 1;       /* not newer, but the area holder's copy over a record from another writer: the holder's copy wins */
const int kZvRefuseNotNewer = 2;   /* not newer, and not the holder's copy over another writer's */
inline int ZoneRecvJudge(int haveHeld, long long heldStamp, int heldOwner, long long stamp, int ownerSlot, int holderSlot, int mySlot)
{
    if (haveHeld == 0 || stamp > heldStamp) return kZvTakeNewer;
    if (ownerSlot >= 0 && holderSlot >= 0 && ownerSlot == holderSlot && ownerSlot != mySlot && heldOwner != ownerSlot) return kZvTakeHolder;
    return kZvRefuseNotNewer;
}
inline int ZoneRecvTakes(int why) { return (why != kZvRefuseNotNewer) ? 1 : 0; }

/* A, THE GAME: may a zone copy that was NOT loaded from the record now held be saved over it? hasMark: the copy carries the
   kept mark (loaded on this link from a record, or with no record once the record list was known); markGen / linkGen: the
   link generation the mark was taken on / the generation now; holderSlot: the
   area's holder on this game's AREAMAP (< 0 unknown, nobody or frozen); mySlot: this game's slot (-1 unknown); recordSlot: the slot
   that wrote the record now held (-1 unknown); loadedSince: when this game's own bit last appeared in the area's cell of its map
   (0 = not set now); sinceLimit: the latest loadedSince that still counts as "stayed loaded" (same clock as loadedSince). The
   conditions are tested in this order and the first that fails is the answer. */
const int kZaAdopt = 0;           /* every condition holds: the copy is adopted as loaded from the record now held */
const int kZaNoMark = 1;          /* no kept mark: not loaded on this link from a record, nor with no record once the record list was known */
const int kZaLinkDropped = 2;     /* the link dropped (a new generation) since the mark */
const int kZaNotHolder = 3;       /* this game does not hold the area now (or its slot or the holder is unknown) */
const int kZaOwnRecord = 4;       /* the record now held was written by this game's own slot, or its writer is unknown */
const int kZaNotLoadedSince = 5;  /* the area is not loaded here on the map, or its bit appeared after sinceLimit (or sinceLimit is 0: no time to judge by) */
inline int ZoneKeptAdoptJudge(int hasMark, long markGen, long linkGen, int holderSlot, int mySlot, int recordSlot, double loadedSince, double sinceLimit)
{
    if (hasMark == 0) return kZaNoMark;
    if (markGen != linkGen) return kZaLinkDropped;
    if (mySlot < 0 || holderSlot < 0 || holderSlot != mySlot) return kZaNotHolder;
    if (recordSlot < 0 || recordSlot == mySlot) return kZaOwnRecord;
    if (!(loadedSince > 0.0) || loadedSince > sinceLimit) return kZaNotLoadedSince;
    return kZaAdopt;
}
inline const char* ZoneKeptAdoptWhy(int why)
{
    switch (why)
    {
    case kZaAdopt: return "adopted";
    case kZaNoMark: return "not loaded on this link from a record, nor while no record existed";
    case kZaLinkDropped: return "the link dropped since it was loaded";
    case kZaNotHolder: return "this game does not hold the area";
    case kZaOwnRecord: return "the record is this game's own (or its writer is unknown)";
    case kZaNotLoadedSince: return "the area was not loaded here on the map from before the record landed until now";
    }
    return "unknown";
}

}   /* namespace zonerec */

#endif
