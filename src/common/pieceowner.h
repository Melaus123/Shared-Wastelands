#pragma once
/* src/common/pieceowner.h - T-1443 fold 2 (owner decision 1804 a): AN AREA RECORD WITH NO KNOWN WRITER TAKES EACH PLAYER BUILDING'S
 * OWNER FROM THAT BUILDING'S OWN PIECE RECORD.
 *
 * An area record whose owner field is unknown (0xFFFFFFFF) names its writer's own buildings under the shared player-faction id
 * ("204-gamedata.base"), and nothing in the file says whose they are. Each such building is also one world-server piece record
 * (piecerec.h), whose header names the piece's owner player. The mapping is by POSITION KEY, the key both games and the world server
 * already give one piece: the area file's building list entry (its template sid and position, the state record's 'world Y pos') makes
 * the key (poskey::KeyOfPos, exactly as zonekeep::ExtractKeptPieces does), the key makes the piece record's id
 * (piecerec::MakePieceRecId), and the building list entry's state record sid is the id the owner translation walks
 * (store.cpp TranslateZoneOwners) - so each building is translated by its own owner, several players per file allowed.
 *
 * The piece records of an area are all here once that area is DONE on this link (the world server's AREA_DONE for it, or the whole
 * feed's - store.h StoreAreaDoneXY): before that the read waits (areafresh kSwapPiecesNotReady, held - the AREA_DONE event ends it);
 * after it, a building with no piece record cannot be resolved (-1: loaded from the shared copy as its file names it, a FAULT line).
 *
 * Pure: no engine memory, no Windows. C++03. The offline suite (t1443_pieces) runs it. */
#include <string>
#include <vector>
#include <map>
#include <utility>
#include "zonekeep.h"
#include "piecerec.h"

namespace coopowner {

/* one player building of an area file: the state record the translation walks, and its piece record id ("" = no key could be made) */
struct PlayerPieceRef { std::string stateSid; std::string pieceId; std::string key; };

/* every building of the file's building list whose state record names `playerSid` as its owner ('owner faction ID') */
inline void PlayerPieceRefsOf(const zonekeep::Container& c, const std::string& playerSid, std::vector<PlayerPieceRef>* out)
{
    if (out == 0 || playerSid.empty()) return;
    std::map<std::string, int> bySid;
    for (size_t i = 0; i < c.recs.size(); ++i) if (bySid.find(c.recs[i].sid) == bySid.end()) bySid[c.recs[i].sid] = (int)i;
    std::map<std::string, int>::const_iterator bl = bySid.find(zonekeep::kBuildingList);
    if (bl == bySid.end()) return;
    const zonekeep::Rec& list = c.recs[(size_t)bl->second];
    std::map<int, int> seen;
    for (size_t i = 0; i < list.insts.size(); ++i)
    {
        const zonekeep::Inst& in = list.insts[i];
        const int s = zonekeep::InstRecOfType(c, bySid, in, zonekeep::kTypeState);
        if (s < 0 || seen.find(s) != seen.end()) continue;
        seen[s] = 1;
        const zonekeep::Rec& r = c.recs[(size_t)s];
        if (zonekeep::RecStr(r, "owner faction ID") != playerSid) continue;
        const float y = zonekeep::RecHasFloat(r, "world Y pos") ? zonekeep::RecFloat(r, "world Y pos", 0.0f) : in.pos[1];
        char key[poskey::kKeyCap];
        const int kr = poskey::KeyOfPos(in.target.c_str(), in.pos[0], y, in.pos[2], key, poskey::kKeyCap);
        PlayerPieceRef p; p.stateSid = r.sid;
        if (kr == poskey::kKeyOk || kr == poskey::kKeyHashed) { p.key = key; p.pieceId = piecerec::MakePieceRecId(p.key); }
        out->push_back(p);
    }
}
/* the owner player a piece record's bytes name, or -1 (not a readable piece record) */
inline int PieceOwnerOfBytes(const char* p, size_t n)
{
    piecerec::PieceHeader h;
    if (p == 0 || n == 0 || piecerec::DecodePieceHeader(p, n, &h) != piecerec::kPrOk) return -1;
    return (h.ownerSlot >= 0 && h.ownerSlot <= coopslot::kSlotMax) ? h.ownerSlot : -1;
}
/* THE RESOLUTION. areaDone = every sector the refs' piece ids name (and the area's own) is DONE on this link. 0 = wait (nothing
   filled); 1 = resolved: byState[stateSid] = (owner slot or -1 for no piece record, the key or the state sid for the line), slots =
   each owner other than mySlot once (their stand-ins are needed), *unresolved = the buildings with no owner. */
typedef std::map<std::string, std::pair<int, std::string> > PieceOwnerMap;
/* T-1443 fold 3: final = the read cannot wait (a world load's read, or one the hold did not keep): it resolves with the records here now,
   and what is missing stays unresolved (loaded from the shared copy, a FAULT) - never this game's own copy */
inline int ResolvePieceOwners(const std::vector<PlayerPieceRef>& refs, const std::map<std::string, int>& ownerByPieceId, int areaDone, int final, int mySlot,
                              PieceOwnerMap* byState, std::vector<int>* slots, int* unresolved)
{
    if (areaDone == 0 && final == 0) return 0;
    int un = 0;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        int o = -1;
        if (!refs[i].pieceId.empty())
        {
            std::map<std::string, int>::const_iterator f = ownerByPieceId.find(refs[i].pieceId);
            if (f != ownerByPieceId.end()) o = f->second;
        }
        if (o < 0) ++un;
        else if (o != mySlot && slots != 0) coopslot::AddSlotOnce(slots, o);
        if (byState != 0) (*byState)[refs[i].stateSid] = std::make_pair(o, refs[i].key.empty() ? refs[i].stateSid : refs[i].key);
    }
    if (unresolved != 0) *unresolved = un;
    return 1;
}
/* what the translation writes for one building whose file owner is the shared id and whose writer is unknown: 1 = `*to` (a stand-in
   id), 2 = it is this game's own (left as it is), 0 = no owner known, 3 = its owner (*ownerOut) has no stand-in here (haveStandIn
   lists those that do) - 0 and 3 load from the shared copy as the file names it, each with a FAULT line */
inline int PieceOwnerSwap(const PieceOwnerMap& byState, const std::string& stateSid, int mySlot, const std::vector<int>& haveStandIn, std::string* to, int* ownerOut)
{
    PieceOwnerMap::const_iterator f = byState.find(stateSid);
    if (ownerOut != 0) *ownerOut = -1;
    if (f == byState.end() || f->second.first < 0) return 0;
    if (ownerOut != 0) *ownerOut = f->second.first;
    if (f->second.first == mySlot) return 2;
    bool has = false;
    for (size_t i = 0; i < haveStandIn.size(); ++i) if (haveStandIn[i] == f->second.first) has = true;
    if (!has) return 3;
    if (to != 0) *to = coopslot::StandInId(f->second.first);
    return 1;
}
/* the owner a piece record held here gives: -1 when none is held or it is a removal mark (a deleted piece names no owner) */
inline int PieceOwnerOfHeld(int heldOwner, int removed) { return (heldOwner >= 0 && heldOwner <= coopslot::kSlotMax && removed == 0) ? heldOwner : -1; }

}   /* namespace coopowner */
