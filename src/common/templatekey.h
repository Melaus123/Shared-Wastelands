/* src/common/templatekey.h - HOW A CHARACTER'S TEMPLATE IS NAMED BETWEEN GAMES (T-922).
 *
 * A game-data entry's shown name (GameData +0x28) follows the game's language: a French game calls the template an English
 * game calls 'Mercenary Guild' 'Guilde des Mercenaires'. A SPAWN that named its template by the shown name found nothing on
 * a game in another language, and the copy was refused (report 1561427638684225647: 169 of 269 host characters). Every
 * entry also carries a stringID ("<number>-<mod file>", GameData +0x58), the same in every language.
 *
 * SENDER: a SPAWN carries BOTH texts (spawnage.h, protocol 172): the shown name in its old field, and the template's stringID
 * in a tail after the flags byte - "" for an entry whose stringID is empty, longer than kMaxSid bytes (the bound factionkey.h
 * and ownerwire.h use), holds a control byte or starts with '@'.
 *
 * RECEIVER: the stringID is tried first (the game's own by-id lookup, which creates nothing); the answer stands only when it
 * is a CHARACTER or RECORD_ANIMAL entry. Otherwise - no id sent, no entry has it, or an entry of another kind has it - the
 * shown name is looked up (CHARACTER, then RECORD_ANIMAL), as before. Neither = refused.
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once
#include <string>
#include "factionkey.h"

namespace templatekey {

const unsigned int kMaxSid = factionkey::kMaxSid;   /* 95: a stringID longer than this is not sent */

/* a stringID this layer sends and tries: 1..kMaxSid bytes, no control byte, not starting with '@' */
inline bool SidUsable(const std::string& s) { return factionkey::SidUsable(s); }

/* sender: the stringID a SPAWN carries - "" when it is not usable (the receiver then uses the shown name alone) */
inline std::string SidToSend(const std::string& sid) { return SidUsable(sid) ? sid : std::string(); }

/* sender: the template travels by its shown name alone (counted tmplSentByName, for a SPAWN sent) */
inline bool SentByName(const std::string& sid) { return !SidUsable(sid); }

/* receiver: is the stringID tried at all */
inline bool TryAsId(const std::string& sid) { return SidUsable(sid); }

/* receiver: an entry found by id is a template a character can be made from */
inline bool TypeAccepted(int type, int characterType, int animalType) { return type == characterType || type == animalType; }

const int kTkRefused = 0, kTkIdHit = 1, kTkNameFallback = 2;

/* receiver: what each step answered (filled by the plugin's lookup, read by the decisions below) */
struct Lookup
{
    bool tried, idFound, idTypeOk, nameTried, nameFound;
    int idType;   /* the type of the entry the id found (idFound), else -1 */
    Lookup() : tried(false), idFound(false), idTypeOk(false), nameTried(false), nameFound(false), idType(-1) {}
};

/* receiver: the name lookup runs only when the id lookup did not answer an accepted entry */
inline bool NameLookupNeeded(const Lookup& l) { return !(l.tried && l.idFound && l.idTypeOk); }

/* receiver: the verdict - by id, by shown name (the fallback), or refused */
inline int Verdict(const Lookup& l)
{
    if (!NameLookupNeeded(l)) return kTkIdHit;
    return (l.nameTried && l.nameFound) ? kTkNameFallback : kTkRefused;
}

inline std::string Num(int v)
{
    if (v == 0) return "0";
    const bool neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
    std::string s;
    while (u != 0) { s.insert(s.begin(), (char)('0' + (int)(u % 10u))); u /= 10u; }
    return neg ? "-" + s : s;
}

/* receiver: what was tried, id then name, and each result - for the refusal line */
inline std::string TriedWords(const std::string& sid, const std::string& name, const Lookup& l)
{
    std::string w = "id '" + sid + "': ";
    if (!l.tried) w += sid.empty() ? "none sent" : "not usable, not tried";
    else if (!l.idFound) w += "no entry";
    else if (!l.idTypeOk) w += "an entry of type " + Num(l.idType) + ", not a CHARACTER or RECORD_ANIMAL";
    else w += "found";
    w += "; then shown name '" + name + "': ";
    if (!NameLookupNeeded(l)) w += "not needed";
    else if (!l.nameTried) w += "none sent";
    else w += l.nameFound ? "found" : "no CHARACTER or RECORD_ANIMAL entry";
    return w;
}

}   /* namespace templatekey */
