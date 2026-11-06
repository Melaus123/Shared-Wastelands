// lookalikelist.h - THE WORLD'S LOOK-ALIKE LIST (T-1096, owner 1234 e). Shared by the plugin, the world server and the offline suite.
//
// A named (unique) character imported from single player plays as a look-alike copy: a CHARACTER record made at run time from the
// original's template under the id spimport::CopySid ('<original id>~i<slot>'). The record lives only in the running game, so every
// game of the world must make it again before any character built from it. The world server keeps the list of the world's copy ids
// (lookalike_list.txt in the world's folder) and hands it to every game inside the WELCOME's opening push, ahead of the CLOCK the
// world-load gate waits for, so a game has the whole list before its world load reads a squad file. The importing game sends each new
// copy id up the moment it makes the copy; the server keeps it and sends it on to every other game, which makes the copy then.
//
// LOOKALIKE (90), world-server protocol 130:
//   up   ADD  {u32 op = 2, u32 count, count x (u32 len, bytes)} - copy ids this game made (1..kMaxIds)
//   down LIST {u32 op = 1, u32 count, ...} - the world's whole list (0..kMaxIds); further ids of a long list follow as ADD
//   down ADD  - ids another game made
// Every id is a copy id (spimport::IsCopySid) of at most spimport::kCopySidMax bytes; a message with any other is malformed whole.
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include "spimport.h"

namespace lalist {

const unsigned kMsgLookalike = 90;
const unsigned kProtocol = 145;          /* the world-server protocol that carries LOOKALIKE */

const unsigned kOpList = 1, kOpAdd = 2;
const unsigned kMaxIds = 256;            /* ids in one message; a longer list goes as one LIST and then ADD messages */
const char* const kListFile = "lookalike_list.txt";   /* the world server's file, in the world's folder */

inline bool IdOk(const std::string& id) { return !id.empty() && id.size() <= (size_t)spimport::kCopySidMax && spimport::IsCopySid(id); }
inline void PutU32(std::vector<char>* b, unsigned v) { char c[4]; std::memcpy(c, &v, 4); b->insert(b->end(), c, c + 4); }

inline bool Encode(std::vector<char>* out, unsigned op, const std::vector<std::string>& ids)
{
    if (out == 0 || (op != kOpList && op != kOpAdd) || ids.size() > kMaxIds || (op == kOpAdd && ids.empty())) return false;
    std::vector<char> b;
    PutU32(&b, op); PutU32(&b, (unsigned)ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (!IdOk(ids[i])) return false;
        PutU32(&b, (unsigned)ids[i].size()); b.insert(b.end(), ids[i].begin(), ids[i].end());
    }
    out->swap(b);
    return true;
}
// 1 = decoded (every id a copy id, nothing trailing), 0 = malformed (nothing is taken from it)
inline int Decode(const char* p, size_t n, unsigned* op, std::vector<std::string>* ids)
{
    if (op == 0 || ids == 0) return 0;
    *op = 0; ids->clear();
    if (p == 0 || n < 8) return 0;
    unsigned o = 0, count = 0; std::memcpy(&o, p, 4); std::memcpy(&count, p + 4, 4);
    if ((o != kOpList && o != kOpAdd) || count > kMaxIds || (o == kOpAdd && count == 0)) return 0;
    size_t at = 8;
    std::vector<std::string> v;
    for (unsigned i = 0; i < count; ++i)
    {
        if (n - at < 4) return 0;
        unsigned len = 0; std::memcpy(&len, p + at, 4); at += 4;
        if (len == 0 || (size_t)len > (size_t)spimport::kCopySidMax || n - at < (size_t)len) return 0;
        const std::string id(p + at, len); at += len;
        if (!IdOk(id)) return 0;
        v.push_back(id);
    }
    if (at != n) return 0;
    *op = o; ids->swap(v);
    return 1;
}

// the server's file: one line each, '<copy id><TAB><original id>'
inline std::string Line(const std::string& copySid) { return copySid + "\t" + spimport::OrigOfCopySid(copySid) + "\n"; }
// the copy ids a file names: a line's first field when it is a copy id and its second field (if any) is that copy's original; each once
inline std::vector<std::string> ParseFile(const std::string& text)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at); if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at); at = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const size_t t = line.find('\t');
        const std::string c = line.substr(0, t);
        if (!IdOk(c)) continue;
        if (t != std::string::npos && line.substr(t + 1) != spimport::OrigOfCopySid(c)) continue;
        bool dup = false;
        for (size_t i = 0; i < out.size() && !dup; ++i) if (out[i] == c) dup = true;
        if (!dup) out.push_back(c);
    }
    return out;
}

inline bool Has(const std::vector<std::string>& v, const std::string& s) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return true; return false; }

// THE GAME'S BOOK: the list of ONE world (the WELCOME's world id), and the ids this game made that the world server has not been sent
struct Book { std::string world; std::vector<std::string> ids; std::vector<std::string> unsent; };
// the link's world: a book of another world (its ids and its unsent ids) is dropped. 1 = dropped.
inline int BookWorld(Book* b, const std::string& world)
{
    if (b == 0 || b->world == world) return 0;
    b->world = world; b->ids.clear(); b->unsent.clear();
    return 1;
}
// the world server's whole list replaces the book's; this game's unsent ids stay in it and are returned, to be sent now
inline std::vector<std::string> BookList(Book* b, const std::vector<std::string>& in)
{
    std::vector<std::string> keep;
    if (b == 0) return keep;
    std::vector<std::string> v;
    for (size_t i = 0; i < in.size(); ++i) if (IdOk(in[i]) && !Has(v, in[i])) v.push_back(in[i]);
    for (size_t i = 0; i < b->unsent.size(); ++i) { if (!Has(v, b->unsent[i])) v.push_back(b->unsent[i]); keep.push_back(b->unsent[i]); }
    b->ids.swap(v);
    return keep;
}
// one id into the book (sent = the world server has it or it came from there). 1 = new to the book, 0 = already there, -1 = not a copy id
inline int BookAdd(Book* b, const std::string& id, bool sent)
{
    if (b == 0 || !IdOk(id)) return -1;
    if (!sent && !Has(b->unsent, id)) b->unsent.push_back(id);
    if (Has(b->ids, id)) return 0;
    b->ids.push_back(id);
    return 1;
}
inline void BookSent(Book* b, const std::string& id)
{
    if (b == 0) return;
    for (size_t i = 0; i < b->unsent.size(); ++i) if (b->unsent[i] == id) { b->unsent.erase(b->unsent.begin() + i); return; }
}

// THE PRE-MAKE at the top of a world load: the ids to make now - the book's, when it is the world the link is to (none with no link world)
inline std::vector<std::string> PreMakeIds(const Book& b, const std::string& linkWorld)
{
    std::vector<std::string> out;
    if (linkWorld.empty() || b.world != linkWorld) return out;
    for (size_t i = 0; i < b.ids.size(); ++i) if (IdOk(b.ids[i]) && !Has(out, b.ids[i])) out.push_back(b.ids[i]);
    return out;
}

// THE BACKUP at a squad member's build (the engine's 0x36F570): a member whose template record is missing and whose template id is a
// copy id -> on the main thread the copy is made and the build goes on with it; off the main thread (the record table has no lock) the
// member is left out (the engine itself would swap in its placeholder record 136-gamedata.quack, decomp_36f570.txt:40-52, and
// build it on that - not this character). Anything else -> built as it is.
enum BuildStep { kBuildAsIs = 0, kBuildMake = 1, kBuildLeaveOut = 2 };
inline int BuildStepFor(bool haveRecord, bool copyId, bool onMain)
{
    if (haveRecord || !copyId) return kBuildAsIs;
    return onMain ? kBuildMake : kBuildLeaveOut;
}

// THE MEMBERS LEFT OUT of their build (kBuildLeaveOut): each copy id with the squad its member belongs to (0 = not readable), until a
// build of that copy succeeds. Fixed size and no allocation: it is filled under a spin lock on any thread. Emptied at every world load
// (a member or a squad address of the last world is not this one's). An id that does not fit, or one more than kLeftCap, is an
// overflow: its member is not known, so it counts as one until the list is emptied.
const int kLeftCap = 64, kLeftIdMax = 96;
struct LeftList { char id[kLeftCap][kLeftIdMax]; const void* squad[kLeftCap]; int said[kLeftCap]; int n; int over; };
inline void LeftClear(LeftList* l) { if (l != 0) { l->n = 0; l->over = 0; } }
// 1 = added, 0 = already there (a squad not known before is taken now), -1 = an overflow (counted in over)
inline int LeftAdd(LeftList* l, const char* id, const void* squad)
{
    if (l == 0 || id == 0) return -1;
    const size_t len = std::strlen(id);
    if (len == 0 || len >= (size_t)kLeftIdMax) { ++l->over; return -1; }
    for (int i = 0; i < l->n; ++i)
        if (std::strcmp(l->id[i], id) == 0) { if (l->squad[i] == 0) l->squad[i] = squad; return 0; }
    if (l->n >= kLeftCap) { ++l->over; return -1; }
    std::memcpy(l->id[l->n], id, len + 1); l->squad[l->n] = squad; l->said[l->n] = 0; ++l->n;
    return 1;
}
// 1 = the id was on the list and is taken off (its member is built now)
inline int LeftBuilt(LeftList* l, const char* id)
{
    if (l == 0 || id == 0) return 0;
    for (int i = 0; i < l->n; ++i)
    {
        if (std::strcmp(l->id[i], id) != 0) continue;
        --l->n;
        if (i != l->n) { std::memcpy(l->id[i], l->id[l->n], kLeftIdMax); l->squad[i] = l->squad[l->n]; l->said[i] = l->said[l->n]; }
        return 1;
    }
    return 0;
}
// members left out and not built again (any squad), an overflow as one
inline int LeftCount(const LeftList& l) { return l.n + (l.over > 0 ? 1 : 0); }
// THE IMPORT'S SHARE (ImportHeld's leftOut): the members of the squads `imported` says the import read in, a member whose squad could
// not be read, and an overflow as one - never a member of another game's squad
template <class IsImported> inline int LeftInImport(const LeftList& l, const IsImported& imported)
{
    int k = l.over > 0 ? 1 : 0;
    for (int i = 0; i < l.n; ++i) if (l.squad[i] == 0 || imported(l.squad[i])) ++k;
    return k;
}

// THE IMPORT'S SAVE HOLD, on the fact: held while a character of the imported squads is still on a named template (namedLeft > 0; -1 =
// the squads could not be read), a copy member of the imported squads is left out and not yet built again (leftOut > 0, LeftInImport), or a copy id this game made has not
// been sent to the world server (unsent > 0 - the world must have the id before any record naming it). Decided again each tick.
inline bool ImportHeld(int namedLeft, int leftOut, int unsent) { return namedLeft != 0 || leftOut > 0 || unsent > 0; }

}  // namespace lalist
