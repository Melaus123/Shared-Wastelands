#pragma once
// THE WORLD SERVER'S ENTRY RULES - who may talk to it, how big a message may be, how often, and how long a record's removed
// files are kept. The pure half: compiled into the world server (src/coop-store/store_main.cpp) and the offline suite.
//
// WHO: a connection is NEW (connected, no HELLO yet), in the LOBBY (its HELLO named a person and no profile, or a profile it
// may not play) or JOINED (a HELLO admitted it: it holds a slot and has been sent the WELCOME). AdmitDecide answers for one
// message: HELLO is taken at every stage; PROFILES from the lobby or a joined game; everything else from a joined game only.
// The game sends nothing else before its WELCOME (every send but HELLO and PROFILES waits for the link to be welcomed), so
// a message refused here is one no game of ours sends.
// HOW BIG: the per-field bounds of the messages the server parses by hand (the ones with their own decoders check their own).
// A key (a record id, a named character's id, a faction key) with a control byte refuses its message; a text field that is
// only shown or stored (a squad template, a faction's name, a town, a save folder, a world name) has its control bytes
// replaced with '?' (CleanText) and the message is kept. A deleted-groups list is used up to kBitsBytesMax bytes and the
// rest is set aside (BitsBytesUsed): both sides ignore group numbers from 1,048,576 up, so those bytes are never read.
// HOW OFTEN: a per-connection allowance for the two messages that cost the server real work and reach it before a game has
// joined - HELLO (an admitted HELLO reads the whole folder for the opening push) and PROFILES (NEW and DELETE rewrite
// profiles.txt). Generous: a full allowance of kHelloBurst / kProfilesBurst, refilled at kHelloPerSec / kProfilesPerSec.
// HOW LONG: a record's files leave the folder by moving into <world>\recycle\ named
// "<unix seconds>_<process id>_<n>_<file name>"; RecyclePrune keeps them kRecycleKeepSec and at most kRecycleKeepBytes, oldest
// first out (the server prunes at start and once an hour).
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace storegate {

enum { kStageNew = 0, kStageLobby = 1, kStageJoined = 2 };
enum { kAdmit = 0, kRefuseNotJoined = 1 };
const unsigned kMsgHello = 29, kMsgProfiles = 45;   // the server's MSG_STORE_HELLO and MSG_PROFILES (checked by the offline suite)

inline int StageOf(int joined, int inLobby) { return joined ? kStageJoined : (inLobby ? kStageLobby : kStageNew); }

inline int AdmitDecide(unsigned type, int stage)
{
    if (type == kMsgHello) return kAdmit;
    if (type == kMsgProfiles) return stage == kStageNew ? kRefuseNotJoined : kAdmit;
    return stage == kStageJoined ? kAdmit : kRefuseNotJoined;
}

// ---- field bounds ----
const size_t kIdMax = 512;               // a record id, a faction name used as a key
const size_t kUniqueIdMax = 4096;        // a named character's id - the game's own bound for one
const size_t kTextMax = 1024;            // a save folder, a world name, a town, a squad template
const unsigned kBitsBytesMax = 131072;   // DELETED_BITS: group numbers stop below 1,048,576 (SplitGroupId) - 1,048,576 / 8 bytes
const unsigned kAreasMax = 4096;         // AREAS: the world is 64 x 64 sectors

// no control byte (below 0x20, or 0x7F) and at most `max` bytes; empty allowed
inline bool TextOk(const std::string& s, size_t max)
{
    if (s.size() > max) return false;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
inline bool IdOk(const std::string& s) { return !s.empty() && TextOk(s, kIdMax); }
// every control byte replaced with '?'; returns how many were
inline int CleanText(std::string* s)
{
    int n = 0;
    for (size_t i = 0; i < s->size(); ++i) { const unsigned char c = (unsigned char)(*s)[i]; if (c < 0x20 || c == 0x7F) { (*s)[i] = '?'; ++n; } }
    return n;
}

// a float that is a real number (not infinite, not NaN), read from its bits so no compiler setting changes the answer
inline bool FiniteF32(float v) { unsigned u = 0; memcpy(&u, &v, 4); return (u & 0x7F800000u) != 0x7F800000u; }

inline bool DeletedBitsFactionOk(const std::string& faction) { return IdOk(faction); }
inline unsigned BitsBytesUsed(unsigned bytes) { return bytes < kBitsBytesMax ? bytes : kBitsBytesMax; }
inline bool UniqueIdOk(const std::string& sid) { return !sid.empty() && TextOk(sid, kUniqueIdMax); }
inline bool RecordGoneOk(const std::string& worldId) { return IdOk(worldId); }
inline bool AreasCountOk(unsigned n) { return n <= kAreasMax; }
inline bool HelloTextOk(const std::string& saveFolder, const std::string& world) { return TextOk(saveFolder, kTextMax) && TextOk(world, kTextMax); }
inline bool RecordOk(const std::string& worldId, const std::string& squadSid, const std::string& faction, const std::string& town, float x, float y, float z)
{
    return IdOk(worldId) && TextOk(squadSid, kTextMax) && TextOk(faction, kTextMax) && TextOk(town, kTextMax)
        && FiniteF32(x) && FiniteF32(y) && FiniteF32(z);
}

// ---- per-connection allowance ----
const double kHelloBurst = 20.0, kHelloPerSec = 0.5;        // 20 at once, then one every 2 s
const double kProfilesBurst = 40.0, kProfilesPerSec = 2.0;   // 40 at once, then two a second

struct Allowance { double left; double at; int started; Allowance() : left(0.0), at(0.0), started(0) {} };

// true = the message may be acted on (one unit taken); false = over the allowance. `now` in seconds.
inline bool AllowanceTake(Allowance* a, double now, double burst, double perSec)
{
    if (!a->started) { a->started = 1; a->left = burst; a->at = now; }
    if (now > a->at) { a->left += (now - a->at) * perSec; if (a->left > burst) a->left = burst; a->at = now; }
    if (a->left < 1.0) return false;
    a->left -= 1.0;
    return true;
}
inline bool RateLimited(unsigned type) { return type == kMsgHello || type == kMsgProfiles; }

// ---- the recycle folder ----
const long long kRecycleKeepSec = 14LL * 24 * 3600;
const long long kRecycleKeepBytes = 512LL * 1024 * 1024;

inline std::string RecycleName(long long unixSec, unsigned long pid, unsigned long long n, const std::string& fileName)
{
    char b[80]; sprintf(b, "%lld_%lu_%llu_", unixSec, pid, n);
    return std::string(b) + fileName;
}
// the moved-at seconds in a recycle file name; -1 = not a name RecycleName made
inline long long RecycleStampOf(const std::string& name)
{
    size_t i = 0; long long v = 0;
    while (i < name.size() && i < 18 && name[i] >= '0' && name[i] <= '9') { v = v * 10 + (name[i] - '0'); ++i; }
    if (i == 0 || i >= name.size() || name[i] != '_') return -1;
    for (int group = 0; group < 2; ++group)   // the process id, then the number
    {
        const size_t from = ++i;
        while (i < name.size() && name[i] >= '0' && name[i] <= '9') ++i;
        if (i == from || i >= name.size() || name[i] != '_') return -1;
    }
    if (i + 1 >= name.size()) return -1;
    return v;
}
struct RecycleEntry { long long stamp; long long bytes; };
struct RecycleOlder
{
    const std::vector<RecycleEntry>* in;
    explicit RecycleOlder(const std::vector<RecycleEntry>& v) : in(&v) {}
    bool operator()(size_t a, size_t b) const { return (*in)[a].stamp < (*in)[b].stamp; }
};
// which entries go (indexes into `in`): every one older than keepSec, then the oldest until the rest fit in keepBytes.
// An entry whose name was not ours (stamp -1) is never chosen and does not count.
inline std::vector<size_t> RecyclePrune(const std::vector<RecycleEntry>& in, long long now, long long keepSec, long long keepBytes)
{
    std::vector<size_t> order, out;
    long long total = 0;
    for (size_t i = 0; i < in.size(); ++i) if (in[i].stamp >= 0) { order.push_back(i); total += in[i].bytes; }
    std::stable_sort(order.begin(), order.end(), RecycleOlder(in));   // oldest first; equal stamps keep their order
    for (size_t k = 0; k < order.size(); ++k)
    {
        const RecycleEntry& e = in[order[k]];
        if (now - e.stamp > keepSec || total > keepBytes) { out.push_back(order[k]); total -= e.bytes; }
    }
    return out;
}

}   // namespace storegate
