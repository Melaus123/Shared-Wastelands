#pragma once
// WHERE A WORLD RECORD THIS GAME WRITES SAYS ITS GROUP IS. ONE header compiled into the plugin (store.cpp: the SLEEP, heartbeat and
// created-asleep writes) and the offline suite, so the two cannot hold different ideas of one rule.
//
// A record at x = 0 and z = 0 is the record store's "no position" (store.cpp's own test, x and z both 0): no area holds it, so an
// area wake never places the group, and as a pending note it holds its whole town's new people back. A group whose members are all
// gone from the world (a building's residents put away when the world is torn down) reads 0,0 from its own platoon, so a write takes
// the first known position in this order: a living member's; the platoon's own; the group's last record here; its home building's
// (the position its home key carries); its town's. With none of them the write keeps the platoon's own values, as before, counted.
namespace recordpos {

const int kSrcMember = 0, kSrcPlatoon = 1, kSrcLast = 2, kSrcHome = 3, kSrcTown = 4, kSrcNone = 5;
const int kSrcCount = 6;

// The record store's "no position": x and z both 0 (y is not asked).
inline int NoPos(float x, float z) { return (x == 0.0f && z == 0.0f) ? 1 : 0; }

// One source: have = it was read; its position.
struct Cand { int have; float x, y, z; };

// in[kSrcMember .. kSrcTown]. A living member's position wins whenever one was read (the writes have always taken it); each later
// source wins when it was read and is not "no position" and every earlier one is none. kSrcNone when nothing is known.
inline int Pick(const Cand* in)
{
    if (in == 0) return kSrcNone;
    if (in[kSrcMember].have != 0) return kSrcMember;
    for (int i = kSrcPlatoon; i <= kSrcTown; ++i)
        if (in[i].have != 0 && NoPos(in[i].x, in[i].z) == 0) return i;
    return kSrcNone;
}

// The words a log line uses for the sources past the platoon's own.
inline const char* SrcText(int src)
{
    if (src == kSrcLast) return "kept the last record's";
    if (src == kSrcHome) return "used its home building's";
    if (src == kSrcTown) return "used its town's";
    if (src == kSrcNone) return "none";
    return src == kSrcMember ? "a living member's" : "the platoon's own";
}

}   // namespace recordpos
