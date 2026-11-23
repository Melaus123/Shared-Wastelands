/* T-801 TEST-ONLY lever `boxtest fillshop` (DEV build only) - the pure parts: its words and how the room it leaves is opened. No
 * engine types; the plugin (items.cpp BoxFillShopCommand) and the offline suite (src/coop-test/test_main.cpp) compile this.
 *
 * WHY IT EXISTS (F1884 / F1889): no lever filled a town shop's counters, so the sale road D5 was built for (a town shop whose
 * counters have no room: the keeper pays, the item is destroyed, as the base game does) never ran in a game. The lever fills EVERY
 * storage piece of the shop the sale would try (the holder's own stock, items.cpp ShopStockResolve) with one record until the
 * engine's own add refuses it, tops up that record's part stacks, then opens `room` units of room again.
 *
 * THE WORDS: `boxtest fillshop counter=<key text> [sid=<base data sid>] [room=<units>]`
 *   counter= (required) - part of a counter's position key, as keeperlist prints it (buytest keeper counter='s pin rule);
 *   sid=     - the filler record (made as kitgive makes an item); none = a copy of the first non-money item in the shop's stock;
 *   room=    - units of the filler record the shop can take afterwards (0 .. kFillRoomMax; default 0 = no room at all).
 *   Every word once; any other word is refused (nothing done).
 *
 * THE FILL (T2773 fold): each piece is filled with fillers of the LARGEST stack limit any of its sections gives the record (a
 * shop's storage sections can stack one record to tens of thousands - T2773: 1914 stacks to 4 in a counter and to ~48800 in three
 * storage pieces, where fillers of 4 only merged onto one big stack and the cap ran out); a filler a section takes only part of
 * is followed by another, and a piece is full only when a filler is refused whole (FillPieceStep).
 *
 * THE ROOM (FillOpenPlan): `room` units opened on the record's full stacks, each at its OWN section's limit - whole stacks whose
 * limit fits what is still to open (each frees the stack's cells - room for any item of the record), then the rest as units off
 * one more full stack whose limit is larger (room for units that stack onto it). */
#ifndef COOP_SHOPFILL_H
#define COOP_SHOPFILL_H

#include <string>

namespace shopfill {

const int kFillRoomMax = 100000;

/* 1 = the words parsed; 0 = refused (*why says which word). `rest` is everything after `fillshop`. */
inline int FillShopParse(const std::string& rest, std::string* pin, std::string* sid, int* room, std::string* why)
{
    *pin = ""; *sid = ""; *room = 0; *why = "";
    int havePin = 0, haveSid = 0, haveRoom = 0;
    size_t i = 0;
    while (i < rest.size())
    {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        if (i >= rest.size()) break;
        size_t j = i;
        while (j < rest.size() && rest[j] != ' ' && rest[j] != '\t') ++j;
        const std::string t = rest.substr(i, j - i);
        i = j;
        if (t.compare(0, 8, "counter=") == 0)
        {
            if (havePin != 0 || t.size() == 8) { *why = "counter= given twice or empty"; return 0; }
            *pin = t.substr(8); havePin = 1;
        }
        else if (t.compare(0, 4, "sid=") == 0)
        {
            if (haveSid != 0 || t.size() == 4) { *why = "sid= given twice or empty"; return 0; }
            *sid = t.substr(4); haveSid = 1;
        }
        else if (t.compare(0, 5, "room=") == 0)
        {
            if (haveRoom != 0 || t.size() == 5) { *why = "room= given twice or empty"; return 0; }
            long long v = 0;
            for (size_t k = 5; k < t.size(); ++k)
            {
                if (t[k] < '0' || t[k] > '9') { *why = "room= is not a whole number"; return 0; }
                v = v * 10 + (t[k] - '0');
                if (v > kFillRoomMax) { *why = "room= is too large"; return 0; }
            }
            *room = (int)v; haveRoom = 1;
        }
        else { *why = "unknown word '" + t + "'"; return 0; }
    }
    if (havePin == 0) { *why = "counter=<key text> is required"; return 0; }
    return 1;
}

/* After one filler offered to a piece: 1 = offer another (every unit placed, or a section took part of it - real room was
   used, so the piece may still have more); 0 = the piece is full for the record (nothing of the filler was placed); -1 = stop, a
   fault (the add faulted, or its quantity did not read). addResult is ItAddAnywhereCount's (1 all placed, 0 refused with *took
   placed before the refusal, -1 faulted, -2 quantity unread). */
inline int FillPieceStep(int addResult, int took)
{
    if (addResult == 1) return 1;
    if (addResult < 0) return -1;
    return (took > 0) ? 1 : 0;
}

/* How `room` units are opened on the record's stacks: limit[k] = stack k's limit in its own section (0 or less = unread), full[k] =
   1 when stack k holds its limit. act[k] (out): -1 = take the whole stack off, u > 0 = take u units off it, 0 = leave it. First
   whole full stacks in order while their limit is no more than what is still to open; then the rest off ONE more full stack whose
   limit is larger. Returns the units the plan opens (room when it can be done, less when not: no full stack fits). */
inline int FillOpenPlan(const int* limit, const int* full, int n, int room, int* act)
{
    for (int k = 0; k < n; ++k) act[k] = 0;
    if (room <= 0) return 0;
    int left = room;
    for (int k = 0; k < n && left > 0; ++k)
        if (full[k] != 0 && limit[k] > 0 && limit[k] <= left) { act[k] = -1; left -= limit[k]; }
    for (int k = 0; k < n && left > 0; ++k)
        if (full[k] != 0 && act[k] == 0 && limit[k] > left) { act[k] = left; left = 0; }
    return room - left;
}

}   // namespace shopfill

#endif
