/* src/common/charparity.h - T-1463 (owner 1856 'yes', 1857; F1910 design C): THE CHARACTER CHECK. Items passed between two games
 * for a CHARACTER or a BODY can end up different on each game after an exchange that ends without a clear answer, and nothing
 * re-checked them (boxes have box parity). This is box parity's road for a character:
 *
 *   ASK     MSG_PARITY_REQ with the PRQK kind kParityAskChar (paritywire.h): one row, key "C|<uid>" (CharKey), the asking game's
 *           digest of ITS COPY's inventory (KitDigest) and its item count; the REQ's sx carries the scope (kScopeWhole / kScopeWorn),
 *           sy 0. Sent to the game that runs the character (its recorded owner).
 *   ANSWER  the running game reads its live character with the same scope: EQUAL -> a PARITY_BOX status EQUAL; different -> it
 *           re-sends its FULL KIT on the existing kit road (MSG_CLOTHING) and answers status CONTENTS with no items; a game that
 *           does not run it NOT HOLDER; no such character NOT FOUND; unreadable UNREADABLE (CharAnswer).
 *
 * THE DIGEST covers what the owner's kit carries (clothing.cpp CaptureGarments): every section (main pack, belt, worn slots,
 * weapons, packs) and each top-level pack's contents. Box parity's ORDER-FREE sum of cooppar::ParityItemHash terms over
 * (section, cell, base sid, quantity, quality to 0.01); a pack's inner rows are hashed under "<outer section>@x,y/<inner>^<parent>".
 * A DEAD copy is checked on its worn slots only (kScopeWorn, owner 1857: a dead body's main pack and belt are NOT in this build -
 * a dead copy's kit apply rebuilds only its worn slots, clothing.cpp ApplyWornGarments).
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same functions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstdio>
#include <string>
#include <vector>
#include "paritywire.h"
#include "bagwire.h"

namespace coopcharpar {

const int kScopeWhole = 0;   /* every section the kit carries, and the packs' contents */
const int kScopeWorn  = 1;   /* a dead copy: shirt, legs, armour, head, boots only (the caller marks the rows it keeps) */

/* "C|<uid>" - never a box key (box keys never start with "C|"). */
inline std::string CharKey(unsigned int uid)
{
    char b[24];
    std::sprintf(b, "C|%u", uid);
    return std::string(b);
}
/* 1 = `key` is a character key naming a non-zero uid (written to *uid); 0 = any other key (a box's). */
inline int CharKeyUid(const std::string& key, unsigned int* uid)
{
    if (key.size() < 3 || key.size() > 12 || key[0] != 'C' || key[1] != '|') return 0;
    unsigned long long v = 0;
    for (size_t i = 2; i < key.size(); ++i)
    {
        if (key[i] < '0' || key[i] > '9') return 0;
        v = v * 10u + (unsigned long long)(key[i] - '0');
        if (v > 0xFFFFFFFFull) return 0;
    }
    if (v == 0) return 0;
    if (uid != 0) *uid = (unsigned int)v;
    return 1;
}

/* One kit item as the digest sees it. keep 0 = left out (a dead copy's non-worn sections). */
struct KitRow
{
    std::string section; unsigned int x, y; std::string baseSid; unsigned int qty; float quality; int keep;
    KitRow() : x(0), y(0), qty(1), quality(0.0f), keep(1) {}
};
/* The digest of a kit: the sum of one ParityItemHash per kept item and per row inside a kept pack (bags[k].kitIndex names its pack
   row; a bag naming no row, or a left-out row, is skipped). *count = the items summed. The same contents give the same number
   whatever order they were read in. */
inline unsigned int KitDigest(const std::vector<KitRow>& rows, const std::vector<coopbag::KitBag>& bags, unsigned int* count)
{
    unsigned int sum = 0, n = 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].keep == 0) continue;
        sum += cooppar::ParityItemHash(rows[i].section, (int)rows[i].x, (int)rows[i].y, rows[i].baseSid, (int)rows[i].qty, rows[i].quality);
        ++n;
    }
    for (size_t k = 0; k < bags.size(); ++k)
    {
        const size_t at = (size_t)bags[k].kitIndex;
        if (at >= rows.size() || rows[at].keep == 0) continue;
        char cell[40];
        std::sprintf(cell, "@%u,%u/", rows[at].x, rows[at].y);
        const std::string outer = rows[at].section + cell;
        for (size_t r = 0; r < bags[k].rows.size(); ++r)
        {
            const coopbag::BagRow& b = bags[k].rows[r];
            char par[16];
            std::sprintf(par, "^%u", b.parent);
            sum += cooppar::ParityItemHash(outer + b.section + par, (int)b.x, (int)b.y, b.baseSid, (int)b.qty, b.quality);
            ++n;
        }
    }
    if (count != 0) *count = n;
    return sum;
}

/* THE RUNNING GAME'S ANSWER (box parity's statuses). found: the uid names a character here; runHere: this game runs it;
   readable: its kit was read. Equal digest and count -> EQUAL; else CONTENTS (= its full kit is re-sent). */
inline int CharAnswer(int found, int runHere, int readable, unsigned int dMine, unsigned int nMine, unsigned int dAsked, unsigned int nAsked)
{
    if (found == 0) return cooppar::kParityStatusNotFound;
    if (runHere == 0) return cooppar::kParityStatusNotHolder;
    if (readable == 0) return cooppar::kParityStatusUnreadable;
    return (dMine == dAsked && nMine == nAsked) ? cooppar::kParityStatusEqual : cooppar::kParityStatusContents;
}

/* WHEN A TAKE / GIVE WITH A CHARACTER ASKS (T-1463 (2)): the other window was a character (not a box) of another game, and the
   exchange ended without a clear answer - it lapsed (no answer in time), the holder's yes came but its split never did (noSplit),
   or a full character kept the escrow copy in no inventory (keptNoRoom). A clear yes or no asks nothing. */
inline int AskAfterSettle(int isBox, unsigned int uid, int runHere, int lapsed, int keptNoRoom, int noSplit)
{
    if (isBox != 0 || uid == 0 || runHere != 0) return 0;
    return (lapsed != 0 || keptNoRoom != 0 || noSplit != 0) ? 1 : 0;
}

/* A KIT THAT CANNOT GO ON NOW (T-1463 (3)): while this game still holds one of the copy's items (KitApplyBusy != 0) the kit that
   came is DROPPED - applied later it could undo newer single-item changes - and the character is asked again once nothing holds
   it (the runner then answers equal or sends its kit as it is then). Box parity defers an answer the same way. */
const int kKitApply   = 0;
const int kKitDropAsk = 1;
inline int HeldKitAction(int busy) { return (busy != 0) ? kKitDropAsk : kKitApply; }

/* AN OWED ASK'S WAIT (T-1463 fold; events only, never a timer): ready = send at the next tick; waitLink = the road refused the send
   (it goes again when a player enters the world, ItCharRelink); waitOwner = no game is recorded as running the character, or the
   game asked answered NOT HOLDER / NOT FOUND (it goes again when this game's owner table records or changes the uid's owner -
   CharParityOwnerChanged, also at a SPAWN of it - or when a player enters the world). */
const int kOwedReady     = 0;
const int kOwedWaitLink  = 1;
const int kOwedWaitOwner = 2;
/* AN OWED ASK'S STEP each tick: -1 forget it (no copy here any more, or this game runs the character now); 0 wait (this game still
   holds one of its items, or it waits for a link-up / an owner change); 1 send it now. */
inline int OwedAskStep(int copyHere, int runHere, int busy, int wait)
{
    if (copyHere == 0 || runHere != 0) return -1;
    if (busy != 0 || wait != kOwedReady) return 0;
    return 1;
}
/* THE ASKER'S STEP AFTER AN ANSWER: -1 done (EQUAL, CONTENTS - the kit is on its way - or UNREADABLE: the runner could not read its
   own kit, asking again would not read it either); kOwedWaitOwner = NOT HOLDER / NOT FOUND: owed again, sent when the owner changes. */
inline int OwedAfterAnswer(int status)
{
    if (status == cooppar::kParityStatusNotHolder || status == cooppar::kParityStatusNotFound) return kOwedWaitOwner;
    return -1;
}
/* AN OWNER RECORDED OR CHANGED for the uid: an ask waiting for the owner (or for a link) is ready again; a ready one stays ready. */
inline int OwedOnOwnerChange(int wait) { (void)wait; return kOwedReady; }

}   /* namespace coopcharpar */
