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
 * t609_held_sender, t609_zone_kept_adopt, t1279_refused_save_fault, t1329_empty_record, t1329_read_decision). */
#ifndef ZONEREC_H_INCLUDED
#define ZONEREC_H_INCLUDED

#include <string>
#include <cstdio>
#include <cstddef>
#include <vector>

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

/* T-823 fold 2 (run T1424): A ZONE READ MADE WHILE THE LINK WAS UP BUT ITS RECORD LIST WAS NOT COMPLETE YET (the player's own
   area is read during the load, before the list is even asked for) is remembered as provisional and judged when the list
   completes. recordBlocks: a record of the area is held now (for a load from a record: one newer than the copy loaded);
   readGen / linkGen: the link generation at the read / now; readTeardowns / teardownsNow: the world teardown count at the read /
   now. Tested in this order; the first that fails is the answer. kZpMadeGood = the read gets the kept mark of this link. */
const int kZpMadeGood = 0;      /* every condition holds: marked as if read after the list was complete */
const int kZpRecordHeld = 1;    /* a record of the area is held (a newer one, for a load from a record): dropped */
const int kZpLinkChanged = 2;   /* the link generation changed since the read (or none was known): dropped */
const int kZpTornDown = 3;      /* a world teardown happened since the read: dropped */
inline int ZoneProvisionalJudge(int recordBlocks, long readGen, long linkGen, long long readTeardowns, long long teardownsNow)
{
    if (recordBlocks != 0) return kZpRecordHeld;
    if (readGen < 1 || readGen != linkGen) return kZpLinkChanged;
    if (readTeardowns != teardownsNow) return kZpTornDown;
    return kZpMadeGood;
}
inline const char* ZoneProvisionalWhy(int why)
{
    switch (why)
    {
    case kZpMadeGood: return "made good";
    case kZpRecordHeld: return "dropped: a record of the area is held";
    case kZpLinkChanged: return "dropped: the link changed since the read";
    case kZpTornDown: return "dropped: the world was torn down since the read";
    }
    return "dropped: unknown";
}
/* T-823 fold 3: ZoneProvisionalJudge's recordBlocks. fromRecord: the provisional read was a load from a record; recordHeld: a record
   of the area is held now; heldIsNewer: that record is newer than the copy loaded. An area-file read is blocked by any record held;
   a load from a record only by a newer one (its own record is expected to be there). */
inline int ZoneProvisionalBlocks(int fromRecord, int recordHeld, int heldIsNewer)
{
    if (fromRecord != 0) return (recordHeld != 0 && heldIsNewer != 0) ? 1 : 0;
    return (recordHeld != 0) ? 1 : 0;
}
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

/* T-1279 (owner decision 1580, 2026-11-09: "a save of your own area that gets refused will be logged as a fault rather than only
   counted"; owner rule 1556: a game is in charge of an area it has loaded). A zone save this game refused as a stale copy is a FAULT
   (an error line naming the area and why) when no other game holds the area: this game holds it, or nobody / an unknown game does -
   the area is loaded here, so it is this game's own. Refused while another game holds the area is the holder rule working (a debug
   line, as before). holderSlot: the area's holder on this game's AREAMAP (< 0 unknown, nobody or frozen); mySlot: this game's slot
   (-1 unknown). 1 = a fault. The counter (zoneStaleCopyRefused) counts both, as before. */
inline int ZoneRefusedSaveIsFault(int holderSlot, int mySlot)
{
    if (holderSlot < 0) return 1;
    return (mySlot >= 0 && holderSlot == mySlot) ? 1 : 0;
}
/* T-1279 fold (review finding 6): the FAULT line is said ONCE PER AREA PER LOAD (the counter keeps counting every refusal). saidBefore
   = this area's line was said before; saidLoad = the load number (the world teardown count) it was said in; loadNow = this one.
   1 = say it now. */
inline int ZoneFaultLineDue(int saidBefore, long long saidLoad, long long loadNow) { return (saidBefore == 0 || saidLoad != loadNow) ? 1 : 0; }

/* T-1329 (design C; owner decision 1598 a, 2026-11-12): AN EMPTY AREA RECORD. Kenshi's one-area save (saveLevelData 0x36DCA0) writes
   the area file only when the area holds something to save; otherwise it deletes the old file and sets ZoneMap+0x24 ("has a file") to
   0 (build/decomp_36dca0.txt:263-291). The game that saved then publishes an EMPTY area record: these exact bytes in place of an area
   file, stamped as any area record. The world server stores, judges and forwards it as any area record (zonecheck::Readable takes
   it), so it replaces an older copy and answers later askers; a game holding it gives the area read a path where no file is
   (ZoneReadServe kZrdNoFile), so Kenshi builds the base area. The bytes cannot be an area file Kenshi wrote (an area file is the
   duplicate report's layout, which these do not read as). */
const char kZoneEmptyMark[] = "SWAREA-EMPTY-1\n";
const size_t kZoneEmptyMarkLen = 15;   /* the bytes of kZoneEmptyMark, without its terminating zero */
inline void ZoneEmptyEncode(std::vector<char>* out) { out->assign(kZoneEmptyMark, kZoneEmptyMark + kZoneEmptyMarkLen); }
/* 1 = exactly the EMPTY mark */
inline int ZoneEmptyIsRaw(const unsigned char* b, size_t n)
{
    if (b == 0 || n != kZoneEmptyMarkLen) return 0;
    for (size_t i = 0; i < n; ++i) if (b[i] != (unsigned char)kZoneEmptyMark[i]) return 0;
    return 1;
}
inline int ZoneEmptyIs(const std::vector<char>& v) { return v.empty() ? 0 : ZoneEmptyIsRaw((const unsigned char*)&v[0], v.size()); }
/* THE AREA READ (the game's swap, store.cpp detour_sfsReadFile): gateOn - the swap may serve at all; haveRec - a record of the area
   is held; fileNamed - it names a mirror file; empty - that record is EMPTY. kZrdOwnFile: Kenshi reads its own file (as before);
   kZrdNoFile: the read is given a path where no file is (ZoneNoFilePath) - Kenshi's load fails and it builds the base area, and its
   next own save of the area deletes any stale local file; kZrdRecord: the record's file, by the swap's own further checks. */
const int kZrdOwnFile = 0, kZrdNoFile = 1, kZrdRecord = 2;
inline int ZoneReadServe(int gateOn, int haveRec, int fileNamed, int empty)
{
    if (gateOn == 0 || haveRec == 0 || fileNamed == 0) return kZrdOwnFile;
    return empty != 0 ? kZrdNoFile : kZrdRecord;
}
/* the path an EMPTY record's read is given: the mirror file's own name inside a folder that is never made, so no file is there and
   the name Kenshi gives the area is unchanged */
inline std::string ZoneNoFilePath(const std::string& mirrorFile)
{
    const size_t slash = mirrorFile.find_last_of("\\/");
    if (slash == std::string::npos) return std::string("no-file-empty-area\\") + mirrorFile;
    return mirrorFile.substr(0, slash + 1) + "no-file-empty-area\\" + mirrorFile.substr(slash + 1);
}
/* THE HOLDER'S WRITE (AREA_FRESH WRITE round): nothing was published, and Kenshi's save set ZoneMap+0x24 (hasFile: 1 = it wrote an
   area file, 0 = it saved nothing, -1 = not read). T-1329 fold 1 (F2): either answer from the save that published nothing is a FAULT
   (it ends the asker's wait) - a written file that sent nothing, or an EMPTY area whose record a gate refused (the stale-copy gate,
   the area's read still due); only "not read" keeps the reason as it was (notPublished). T-1329 fold 2 (re-check item 4): NOT when
   the area's read is still due here (readDue = ZoneMapContent+0x108, 1): this game has nothing newer than the world server's record
   (owner 1580 a), so the round answers notPublished and the asker keeps waiting for the record - its next ask's WRITE round, after
   this game's own read ran (its own wait is answered from the world server's records: it is the area's holder), writes a file or an
   EMPTY. readDue -1 = not read: judged as not due. */
inline int ZoneNotPublishedIsFault(int hasFile, int readDue) { return (hasFile == 1 || (hasFile == 0 && readDue != 1)) ? 1 : 0; }
/* T-1329 fold 1 (F1): ZoneMap+0x24 = 0 says the area's CONTAINER was empty when Kenshi saved it - an area whose read has not run yet
   (its load still due, or held by the mod's wait - AfSetRetryPod re-arms the same flag) saves empty too. readDue: the engine's own
   "this area's read is still due" byte, ZoneMapContent+0x108 (set when a load starts - decomp_a0d6a0 / a1a330; cleared by 0x9FF730
   just before it runs the read 0x9FEC00 - decomp_9ff730.txt:19-22; set again by the mod while it holds the read). 1 = an EMPTY area
   record may be published (Kenshi saved nothing AND this area's read ran here); 0 = not (a file was written, the byte was not read,
   or the read is still due - the empty container is the unread area, not an empty one). */
inline int ZoneEmptyMayPublish(int hasFile, int readDue) { return (hasFile == 0 && readDue == 0) ? 1 : 0; }
/* T-1329 fold 1 (F4): an EMPTY queued off the main thread (an unload on a worker) is stamped only when the queue drains, so it is
   dropped when a main-thread save of the same area came after it was queued (a newer state - it must not be overwritten). queuedSeq /
   lastMainSaveSeq: one counter both draw from (0 = no main-thread save of the area). 1 = drop it. */
inline int ZoneQueuedEmptyStale(long long queuedSeq, long long lastMainSaveSeq) { return lastMainSaveSeq > queuedSeq ? 1 : 0; }

}   /* namespace zonerec */

#endif
