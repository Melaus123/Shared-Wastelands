/* src/common/copysquadid.h - THE ID A COPY SQUAD IS SAVED UNDER.
 *
 * ONE RULE, the one standinpurge.h keeps: nothing in here reads engine memory, reads a global or calls the operating
 * system, and it includes only <string>.  A pure function of its arguments, so the offline suite drives every branch.
 *
 * WHY.  The engine names a new squad "<its faction's name>_<n>" and keeps that id at Platoon+0x78 and in the squad's
 * state record ("platoon stringID"; Platoon::loadStateData 0x7EC550, the new-squad arm).  <n> is the faction's OWN
 * counter (Faction+0x260; 0x7F29E0 hands it out and adds one) and the only id the engine skips is one whose
 * platoon/<id>.platoon the save's file index already lists - another live squad holding the id is never asked about.
 * A save keeps ONE file per id, whatever the faction.  A squad made for a copy of another player's characters is made
 * under that player's stand-in faction, which carries the other player's faction name - in a non-English game the
 * translated default name this game's own faction had when it made its first squad.  Both counters start at 0, both
 * squads get "<name>_0", and the save writes the other player's characters into this player's squad file.  So a copy
 * squad whose engine id another squad holds takes a fresh id the engine's own naming can never produce.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no lambdas.
 */
#ifndef COPYSQUADID_H_INCLUDED
#define COPYSQUADID_H_INCLUDED

#include <string>

namespace copysq {

/* who holds an id: no squad, a squad of this game's own player faction, or any other squad (any faction) */
const int kHeldNone = 0, kHeldOwn = 1, kHeldOther = 2;
/* a copy squad just made: keep the engine's id, give it a fresh one, or no answer (its id or the squads were not read) */
const int kIdKeep = 0, kIdRenew = 1, kIdUnread = 2;
/* fresh candidates tried before giving up (each one another squad would have to hold) */
const unsigned kFreshTries = 64;

inline int IdDecide(int unread, int held)
{
    if (unread != 0) return kIdUnread;
    return (held != kHeldNone) ? kIdRenew : kIdKeep;
}

/* 1 = the engine's own naming can produce this id: the text after its last '_' is one or more decimal digits */
inline int EngineCanMint(const std::string& id)
{
    const std::string::size_type u = id.rfind('_');
    if (u == std::string::npos || u + 1 >= id.size()) return 0;
    for (std::string::size_type i = u + 1; i < id.size(); ++i) if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

/* 1 = a character Windows refuses in a file name (the id becomes the file name platoon/<id>.platoon) */
inline int FileNameBad(char c)
{
    const unsigned char u = (unsigned char)c;
    return (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') ? 1 : 0;
}

/* The text every fresh id of this engine id begins with: the engine's id, any character a file name refuses replaced by
   '-', then "-c". */
inline std::string FreshPrefix(const std::string& engineId)
{
    std::string o;
    o.reserve(engineId.size() + 14);
    for (std::string::size_type i = 0; i < engineId.size(); ++i) o += FileNameBad(engineId[i]) ? '-' : engineId[i];
    o += "-c";
    return o;
}

/* The k-th fresh id (k from 1): the engine's id, "-c", k - any character a file name refuses replaced by '-'.  The text
   after its last '_' holds the 'c', so EngineCanMint answers 0 and no faction's counter ever hands it out; it ends in a
   digit, never in the space or dot Windows drops from the end of a file name. */
inline std::string FreshCandidate(const std::string& engineId, unsigned k)
{
    std::string o = FreshPrefix(engineId);
    char t[16]; int n = 0; unsigned v = k;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v != 0 && n < 15);
    while (n > 0) o += t[--n];
    return o;
}

/* The first fresh candidate (k = 1, 2, ...) that held() answers kHeldNone for; "" when all kFreshTries are held.
   *tries = the candidates asked about.  HeldFn: int operator()(const std::string&) const, answering kHeld*. */
template <class HeldFn>
inline std::string FreshId(const std::string& engineId, const HeldFn& held, unsigned* tries)
{
    *tries = 0;
    for (unsigned k = 1; k <= kFreshTries; ++k)
    {
        const std::string c = FreshCandidate(engineId, k);
        ++*tries;
        if (held(c) == kHeldNone) return c;
    }
    return std::string();
}

/* 1 = an id the check of engineId can ask about: engineId itself, or freshPrefix (FreshPrefix(engineId)) followed by at
   least one character.  The walk over every squad keeps only these, so a world of thousands of squads does not fill a
   table with ids no answer needs. */
inline int IdConcerns(const std::string& id, const std::string& engineId, const std::string& freshPrefix)
{
    if (engineId.empty()) return 0;
    if (id == engineId) return 1;
    return (id.size() > freshPrefix.size() && id.compare(0, freshPrefix.size(), freshPrefix) == 0) ? 1 : 0;
}

/* The squad file a "content file" value names: the text after its last '/' or '\', less a trailing ".platoon".  The
   save writes "platoon/" + the file-name part of the awake half's own copy of this value into the state record. */
inline std::string ContentFileStem(const std::string& v)
{
    const std::string::size_type s = v.find_last_of("/\\");
    std::string b = (s == std::string::npos) ? v : v.substr(s + 1);
    const std::string ext(".platoon");
    if (b.size() > ext.size() && b.compare(b.size() - ext.size(), ext.size(), ext) == 0) b.erase(b.size() - ext.size());
    return b;
}

/* 1 = v names engineId's squad file; *out = v naming fresh's instead (its folder and its ending kept).  0 = v is empty
   or names another file; *out is left alone. */
inline int ContentFileRenamed(const std::string& v, const std::string& engineId, const std::string& fresh, std::string* out)
{
    if (v.empty() || engineId.empty() || fresh.empty() || ContentFileStem(v) != engineId) return 0;
    const std::string::size_type s = v.find_last_of("/\\");
    const std::string folder = (s == std::string::npos) ? std::string() : v.substr(0, s + 1);
    const std::string b = (s == std::string::npos) ? v : v.substr(s + 1);
    *out = folder + fresh + b.substr(engineId.size());
    return 1;
}

inline const char* HeldText(int held) { return held == kHeldOwn ? "own" : (held == kHeldOther ? "other" : "no"); }

} // namespace copysq

#endif
