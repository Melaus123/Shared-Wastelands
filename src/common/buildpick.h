// buildpick.h - T-906: the same building / furniture choice on every game of one world.
// When an area is generated for the first time, createBuilding (1.0.65 0x57C1E0) asks the owner faction's building-swap pick
// (1.0.65 0x7F3BD0, 1.0.68 0x7F4770; Read from its decompile and disassembly, 2026-10-19) which record to make:
//   the faction holds a run of lists at Faction+0x278 (count) / +0x280 (first), 0xA0 bytes each. Each list has a hash set of the
//   records it replaces (bucket count +0x18, size +0x20, buckets +0x38; a node is {next, hash, record}) and a weighted map of the
//   choices (std::map<float running total, record>, head +0x48, size +0x50; the total weight at +0x90). The FIRST list whose set
//   holds the record wins. Its map's size decides: 0 -> null (the piece is not made), 1 -> that one choice (no draw), 2 or more ->
//   draw = RandomRange(0, total) and the first choice whose running total is above the draw. No list holds it -> the record itself.
// Only the draw is random, and it differs between games. This header replaces it with a draw from a hash of things every game of
// one world agrees on, and keeps everything else - the list found, the map walked - exactly as the engine does it.
// Pure (no includes, no Windows): build.cpp reads engine memory through these under its own __try; the offline suite builds small
// models of the same layout and runs the same functions.
#ifndef BUILDPICK_H
#define BUILDPICK_H
namespace buildpick {
typedef unsigned long long U64;

const unsigned kFacListCount = 0x278, kFacListFirst = 0x280, kListStride = 0xA0;
const unsigned kSetBuckets = 0x18, kSetSize = 0x20, kSetBucketArr = 0x38;   // the list's hash set of the records it replaces
const unsigned kNodeNext = 0x0, kNodeHash = 0x8, kNodeKey = 0x10;           // a hash-set node
const unsigned kMapHead = 0x48, kMapSize = 0x50, kListTotal = 0x90;          // the list's choices and their total weight
const unsigned kTreeLeft = 0x0, kTreeParent = 0x8, kTreeRight = 0x10;        // a std::map node (VS2010): head's parent is the root,
const unsigned kTreeKey = 0x18, kTreeValue = 0x20, kTreeIsNil = 0x29;        //   head's right the largest entry

// the engine's hash of a record pointer (0x7F3C05..0x7F3C51): k = (p >> 3) + p, then the shift-multiply steps
inline U64 EngineHash(U64 p)
{
    U64 x = (p >> 3) + p;
    x = (x << 21) + ~x;
    x = ((x >> 24) ^ x) * 0x109ull;
    x = ((x >> 14) ^ x) * 0x15ull;
    x = ((x >> 28) ^ x) * 0x80000001ull;
    return x;
}

inline U64 Rd64(const char* p, unsigned off) { return *(const U64*)(p + off); }
inline const char* RdPtr(const char* p, unsigned off) { return *(const char* const*)(p + off); }

// The first of the faction's lists whose set holds `record`, found exactly as the pick does (same order, same bucket walk, the same
// stop when a node of another bucket is reached). Returns its index and *list, or -1 when no list holds it. Reads raw memory: the
// caller guards it.
inline int FindList(const char* faction, U64 record, const char** list)
{
    const unsigned n = *(const unsigned*)(faction + kFacListCount);
    const char* l = RdPtr(faction, kFacListFirst);
    const U64 h = EngineHash(record);
    for (unsigned i = 0; i < n; ++i, l += kListStride)
    {
        const U64 mask = Rd64(l, kSetBuckets) - 1ull;
        const U64 b = mask & h;
        if (Rd64(l, kSetSize) == 0) continue;
        const char* slot = RdPtr(RdPtr(l, kSetBucketArr), (unsigned)(b * 8));
        const char* node = (slot != 0) ? RdPtr(slot, 0) : 0;
        for (; node != 0; node = RdPtr(node, kNodeNext))
        {
            const U64 nh = Rd64(node, kNodeHash);
            if (nh == h) { if (Rd64(node, kNodeKey) == record) { *list = l; return (int)i; } }
            else if ((mask & nh) != b) break;
        }
    }
    *list = 0;
    return -1;
}

// The map walk the pick makes after its draw (0x7F3D1D..0x7F3D4C): from the root, the lowest entry whose running total is above
// `draw`. When none is (a draw at or past the last key, where the engine reads the head's empty slot), the largest entry is taken
// instead, so a record is always a real choice, and *pastEnd is set to 1 (else 0) - review F4: the list builder 0x7FC580 is not
// read, so whether a key is the running total after its weight (then only rounding gets here) or before it is counted, not assumed.
inline const char* WalkMap(const char* list, float draw, int* pastEnd)
{
    const char* head = RdPtr(list, kMapHead);
    const char* found = head;
    const char* node = RdPtr(head, kTreeParent);
    while (node[kTreeIsNil] == 0)
    {
        if (*(const float*)(node + kTreeKey) > draw) { found = node; node = RdPtr(node, kTreeLeft); }
        else node = RdPtr(node, kTreeRight);
    }
    *pastEnd = (found == head) ? 1 : 0;
    if (found == head) found = RdPtr(head, kTreeRight);
    return RdPtr(found, kTreeValue);
}
// the map's keys in order (an in-order walk with its own stack), at most `cap` of them; returns how many were written, -1 when the
// tree is deeper than the stack (never for a balanced map of a few choices). For the pastEnd evidence line only.
inline int MapKeys(const char* list, float* out, int cap)
{
    const char* head = RdPtr(list, kMapHead);
    const char* stack[64];
    int sp = 0, n = 0;
    const char* node = RdPtr(head, kTreeParent);
    while (n < cap && (sp > 0 || node[kTreeIsNil] == 0))
    {
        if (node[kTreeIsNil] == 0) { if (sp >= 64) return -1; stack[sp++] = node; node = RdPtr(node, kTreeLeft); continue; }
        node = stack[--sp];
        out[n++] = *(const float*)(node + kTreeKey);
        node = RdPtr(node, kTreeRight);
    }
    return n;
}

// --- the stable draw ---------------------------------------------------------------------------------------------------------
// FNV-1a over the parts, each followed by a separator byte, then a final mix so near-equal inputs spread over the whole range.
inline unsigned Fnv(unsigned h, const char* s) { for (; *s != 0; ++s) { h ^= (unsigned char)*s; h *= 16777619u; } h ^= 0x1Fu; h *= 16777619u; return h; }
inline unsigned FnvInt(unsigned h, int v) { const unsigned u = (unsigned)v; for (int i = 0; i < 4; ++i) { h ^= (u >> (8 * i)) & 0xFFu; h *= 16777619u; } return h; }
inline unsigned Mix(unsigned h) { h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16; return h; }
// the world's key from its id (the world server's id of this world, the same on every game of it); 0 is kept for "no key"
inline unsigned WorldKey(const char* worldId) { const unsigned h = Mix(Fnv(2166136261u, worldId)); return h != 0 ? h : 1u; }
// a coordinate rounded to whole units (half away from zero), so the float bits of one spot need not agree to the last digit
inline int RoundPos(float v) { return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f); }
// host3 (review F3): a piece of furniture's position is local to its host building, so identical houses would all draw alike;
// its host's world position (rounded the same way) joins the key. 0 = no host: the key is exactly the one without it.
inline unsigned PickHash(unsigned worldKey, const char* recordId, const char* factionId, const int* pos3, int listIndex, const int* host3)
{
    unsigned h = FnvInt(2166136261u, (int)worldKey);
    h = Fnv(h, recordId);
    h = Fnv(h, factionId);
    for (int i = 0; i < 3; ++i) h = FnvInt(h, pos3[i]);
    h = FnvInt(h, listIndex);
    if (host3 != 0) for (int i = 0; i < 3; ++i) h = FnvInt(h, host3[i]);
    return Mix(h);
}
// the draw in [0, total): the hash as a fraction of 2^32, times the total
inline float DrawFromHash(unsigned h, float total)
{
    const double f = (double)h / 4294967296.0;
    float d = (float)(f * (double)total);
    if (!(d < total)) d = 0.0f;   // a float rounding at the very top: the first choice, never past the end
    return d;
}

// what the pick should do, from the list it found
enum { kPassNoList = 0, kPassEmpty = 1, kPassOne = 2, kPassBadTotal = 3, kSeeded = 4 };
// mapSize = the found list's map size, total = its total weight (-1 / anything when found == 0)
inline int Decide(int found, U64 mapSize, float total)
{
    if (found == 0) return kPassNoList;
    if (mapSize == 0) return kPassEmpty;
    if (mapSize == 1) return kPassOne;
    if (!(total > 0.0f) || !(total < 1e30f)) return kPassBadTotal;
    return kSeeded;
}
} // namespace buildpick
#endif
