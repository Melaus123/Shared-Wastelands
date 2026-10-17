/* src/common/savesquadforeign.h - WHOSE CHARACTERS A SAVE'S SQUAD FILE HOLDS, AND WHEN THIS GAME'S OWN RECORD IS LAID OVER IT.
 *
 * Pure: nothing in here reads engine memory, reads a global or calls the operating system. The offline suite
 * (src/coop-test/test_main.cpp, t710_*) drives every branch.
 *
 * WHY.  A save keeps ONE platoon/<id>.platoon per squad id, whatever the faction.  A copy squad of another player's
 * characters made before the copy-squad id check (copysquadid.h) could hold this game's own squad id, and the save then
 * wrote the other player's characters into this game's own squad file.  The restore's roster check (ownrec.h
 * RosterResolve) refuses this game's own record of that squad (the file's characters would be in no squad after the load:
 * rosterConflict), and every later save carries a newer mark, so the record is never a candidate again.
 *
 * THE TEST IS POSITIVE.  Every character record (type 0x24) in a squad file carries the string field "owner faction ID",
 * the record id of the faction the character belongs to (read on a save's squad files: '51707-rebirth.mod' for a world
 * faction's character).  A character is ANOTHER PLAYER'S only when that id names a player's stand-in faction: "coop-p<n>"
 * for a slot other than this game's, or protocol 67's single "coop-peer".  A character this game has no record of is never
 * counted as another player's (records are written only by the 30 s heartbeat or an item change and only for awake player
 * squads, so a recruit can have none).  A file counts as another player's only when it holds at least one character and
 * EVERY character in it is another player's.
 *
 * THE FENCE.  This game's own record of a squad - only one the existing rule takes (newer than the save: an older record
 * would bring back people who died, were dismissed or left after it) - is laid over the save's file of that squad id in
 * place of the file's characters only when (a) the file counts as another player's, (b) every squad file of the save
 * parsed (an unparsed file may hold anyone), (c) the record holds at least one character, every one with a hand, and
 * (d) none of the record's characters is in any OTHER squad file of the save.  Then nothing of this game's own squad is
 * left in the save, and the roster check runs on the record's own characters (each must be in exactly one squad after the
 * load); refused there, the squad loads the file, and the roster counts the file's characters for it (ownrec.h
 * RosterSquad.fenced).  Otherwise the existing rule stands: the record is checked against the file's characters.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no lambdas.
 */
#ifndef SAVESQUADFOREIGN_H_INCLUDED
#define SAVESQUADFOREIGN_H_INCLUDED

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdio>
#include "ownrec.h"     /* GdcCur readers, HandId - the same reads GdcScan makes */
#include "slotwire.h"   /* the stand-in record ids */

namespace coopforeign {

/* One squad file's characters, in the order GdcScan lists them (chars is GdcScan's chars), and each one's "owner faction
   ID" ("" when the record has none). */
struct OwnerScan
{
    std::vector<std::string> chars, owners;
};
inline bool ScanCharOwners(const char* data, size_t n, const std::string& src, OwnerScan* out)
{
    using namespace coopown;
    out->chars.clear(); out->owners.clear();
    GdcCur c; c.p = (const unsigned char*)data; c.n = data ? n : 0; c.at = 0; c.bad = false;
    GdcI32(&c); GdcI32(&c);
    const int recs = GdcCount(&c, 40);
    if (c.bad) return false;
    static const int kWidth[7] = { 1, 4, 4, 12, 16, 0, 0 };   /* boolFields, floatFields, intFields, vectorFields, rotationFields, stringFields, fileFields */
    for (int r = 0; r < recs; ++r)
    {
        GdcI32(&c);
        const int type = GdcI32(&c);
        GdcI32(&c);
        GdcStr(&c, 0); GdcStr(&c, 0);
        GdcI32(&c);
        int haveS = 0, haveT = 0, hs = 0, ht = 0;
        std::string owner;
        for (int m = 0; m < 7; ++m)
        {
            const int cnt = GdcCount(&c, 4);
            for (int e = 0; e < cnt && !c.bad; ++e)
            {
                std::string k;
                GdcStr(&c, &k);
                if (kWidth[m] == 0)
                {
                    if (m == 5 && k == "owner faction ID") GdcStr(&c, &owner); else GdcStr(&c, 0);
                    continue;
                }
                if (m == 2 && k == "handleS") { hs = GdcI32(&c); haveS = 1; }
                else if (m == 2 && k == "handleTYPE") { ht = GdcI32(&c); haveT = 1; }
                else GdcSkip(&c, (size_t)kWidth[m]);
            }
            if (c.bad) return false;
        }
        const int lists = GdcCount(&c, 8);
        for (int l = 0; l < lists && !c.bad; ++l)
        {
            GdcStr(&c, 0);
            const int ne = GdcCount(&c, 16);
            for (int e = 0; e < ne && !c.bad; ++e) { GdcStr(&c, 0); GdcSkip(&c, 12); }
        }
        const int inst = GdcCount(&c, 40);
        for (int i = 0; i < inst && !c.bad; ++i)
        {
            GdcStr(&c, 0); GdcStr(&c, 0); GdcSkip(&c, 12); GdcSkip(&c, 16);
            const int ns = GdcCount(&c, 4);
            for (int s = 0; s < ns && !c.bad; ++s) GdcStr(&c, 0);
        }
        if (c.bad) return false;
        if (type != 0x24) continue;
        if (haveS && haveT) out->chars.push_back(HandId(hs, ht));
        else
        {
            char b[24]; std::sprintf(b, "#%d", r);
            out->chars.push_back("?" + src + b);
        }
        out->owners.push_back(owner);
    }
    return !c.bad;
}

/* 1 = an "owner faction ID" that names another player's stand-in: "coop-p<n>" with n not this game's slot (mySlot -1 =
   not known: any n; no stand-in is ever made for this game's own slot), or "coop-peer". 0 = anything else, "" included. */
inline int OwnerIsOtherPlayer(const std::string& owner, int mySlot)
{
    if (coopslot::IsLegacyPeerId(owner)) return 1;
    const int k = coopslot::StandInSlotOfId(owner);
    return (k >= 0 && k != mySlot) ? 1 : 0;
}

/* the file's verdict: no character / none of them another player's / some / every one (the file counts as another
   player's only for kVerdictAll).  *foreignN = the characters that are another player's. */
const int kVerdictNoChars = 0, kVerdictNone = 1, kVerdictSome = 2, kVerdictAll = 3;
inline int ForeignVerdict(const std::vector<std::string>& owners, int mySlot, int* foreignN)
{
    int f = 0;
    for (std::vector<std::string>::size_type i = 0; i < owners.size(); ++i) f += OwnerIsOtherPlayer(owners[i], mySlot);
    if (foreignN) *foreignN = f;
    if (owners.empty()) return kVerdictNoChars;
    if (f == 0) return kVerdictNone;
    return f == (int)owners.size() ? kVerdictAll : kVerdictSome;
}
inline const char* VerdictName(int v)
{
    return v == kVerdictAll ? "another player's" : v == kVerdictSome ? "partly another player's" : v == kVerdictNone ? "no other player's" : "no character";
}

/* every squad file of the save: character -> how many files hold it (each file counted once per character) */
inline void AddFileChars(std::map<std::string, int>* holding, const std::vector<std::string>& chars)
{
    std::set<std::string> once(chars.begin(), chars.end());
    for (std::set<std::string>::const_iterator it = once.begin(); it != once.end(); ++it) ++(*holding)[*it];
}
/* how many of the record's characters a squad file other than the one whose characters are selfChars holds */
inline int CharsElsewhere(const std::vector<std::string>& rec, const std::map<std::string, int>& holding, const std::vector<std::string>& selfChars)
{
    int n = 0;
    for (std::vector<std::string>::size_type i = 0; i < rec.size(); ++i)
    {
        std::map<std::string, int>::const_iterator h = holding.find(rec[i]);
        const int files = h == holding.end() ? 0 : h->second;
        int inSelf = 0;
        for (std::vector<std::string>::size_type k = 0; k < selfChars.size() && inSelf == 0; ++k) if (selfChars[k] == rec[i]) inSelf = 1;
        if (files - inSelf > 0) ++n;
    }
    return n;
}
/* 1 = a character id with no hand ('?<src>#<record>', GdcScan's form) */
inline int Unhanded(const std::string& id) { return (!id.empty() && id[0] == '?') ? 1 : 0; }

/* THE FENCE: the record is laid over the file (kFenceApply) or the existing rule stands (the reason).  unparsedFiles: the
   save's squad files that did not parse - any one of them may hold a record character, so the fence fails. */
const int kFenceApply = 0, kFenceNotForeign = 1, kFenceRecordEmpty = 2, kFenceUnhanded = 3, kFenceElsewhere = 4, kFenceUnparsed = 5;
inline int FenceDecide(int verdict, const std::vector<std::string>& rec, int recElsewhere, long long unparsedFiles)
{
    if (verdict != kVerdictAll) return kFenceNotForeign;
    if (unparsedFiles > 0) return kFenceUnparsed;
    if (rec.empty()) return kFenceRecordEmpty;
    for (std::vector<std::string>::size_type i = 0; i < rec.size(); ++i) if (Unhanded(rec[i])) return kFenceUnhanded;
    if (recElsewhere > 0) return kFenceElsewhere;
    return kFenceApply;
}
inline const char* FenceName(int f)
{
    return f == kFenceApply ? "apply" : f == kFenceNotForeign ? "the file is not another player's" : f == kFenceRecordEmpty ? "the record holds nobody"
         : f == kFenceUnhanded ? "a character of the record has no hand" : f == kFenceElsewhere ? "a character of the record is in another squad file"
         : f == kFenceUnparsed ? "a squad file of the save did not parse (it may hold anyone)" : "?";
}

/* What kind of squad loaded a watched squad file (the claim's log line): this game's own player faction, a player's stand-in
   this game knows (the stand-in table), a stand-in by its record id only (coop-p<n> / coop-peer, not in the table yet), any
   other faction, or a faction that could not be read. */
const int kKindOwn = 0, kKindPeer = 1, kKindStandIn = 2, kKindWorld = 3, kKindUnread = 4;
inline int SquadKind(int readOk, int playerFaction, int inStandInTable, int standInRecord)
{
    if (readOk == 0) return kKindUnread;
    if (playerFaction != 0) return kKindOwn;
    if (inStandInTable != 0) return kKindPeer;
    if (standInRecord != 0) return kKindStandIn;
    return kKindWorld;
}
inline const char* KindName(int k)
{
    return k == kKindOwn ? "own" : k == kKindPeer ? "peer" : k == kKindStandIn ? "stand-in" : k == kKindWorld ? "world" : "unread";
}
/* 1 = another player's squad: it never takes this game's own record of a squad id, and is renamed when it holds this game's id */
inline int KindIsOtherPlayer(int k) { return (k == kKindPeer || k == kKindStandIn) ? 1 : 0; }

} // namespace coopforeign

#endif
