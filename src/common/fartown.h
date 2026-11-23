#pragma once
// T-1469 (F1904, owner 1854): THE FAR TOWN MODEL LINE - a diagnostic that ships to players. Report: "the low-poly town models sometimes
// stay on when we reach the town". About 2 s after an area finishes loading, one [FARTOWN] log line for each town in that area: the
// town, the camera's map square, and whether the town's far low-detail model is shown.
//
// THE ENGINE (Read, capstone on 1.0.65; the same offsets found in 1.0.68 by byte pattern):
//   - ZoneManager::loadDistantTowns 0xA0A470 (build/decomp_a0a470.txt) runs when the camera's map square changes (:55), keeps that
//     square in ZoneManager +0x1681C8 / +0x1681CC (two ints, :56) and calls the show / hide 0x92CED0(town, show) per town.
//   - 0x92CED0: on show, with no far-model object yet, makes one (0x18 bytes, only when the town's record has 'distant mesh') and keeps
//     it at Town +0x4F8; then, with an object, calls 0xD8620(object, show).
//   - 0xD8620: returns at once when show equals the object's byte +0xC; else writes +0xC = show and calls Ogre
//     MovableObject::setVisible(show) on the object's +0x10 (the model; 0 = not made yet). So +0xC is "shown" as last asked.
//   - an area counts as loaded for this road when ZoneMap +0xB0 or +0xB1 is set (0xA08490).
// Pure (only <string> and <cstdio>): zones.cpp and the offline suite compile this one header. C++03. Reads only - nothing is written.
#include <string>
#include <cstdio>

namespace fartown {

const int kFramesAfterLoad = 120;   /* the line comes this many zone ticks (frames) after the area's loaded event - about 2 s at 60 fps */
const int kLineCap = 40;            /* lines per session; the REPORT's count goes on past it */
const int kQueueCap = 16;           /* areas waiting for their line at once; one more is dropped (counted in the next line) */

/* the far model's state as read: the object at Town +0x4F8 and its byte +0xC */
enum { kModelUnread = -1, kModelNone = 0, kModelHidden = 1, kModelShown = 2 };

/* 1 = the area's line is due on this tick */
inline int Due(long long nowTick, long long eventTick) { return (nowTick - eventTick >= kFramesAfterLoad) ? 1 : 0; }

/* 1 = one more line may be written this session */
inline int MayLog(long long linesWritten) { return (linesWritten < kLineCap) ? 1 : 0; }

/* the model state from the reads: objRead 0 = the Town +0x4F8 read faulted; obj 0 = no object (never shown); shownByte -1 = unread */
inline int ModelState(int objRead, int hasObj, int shownByte)
{
    if (objRead == 0) return kModelUnread;
    if (hasObj == 0) return kModelNone;
    if (shownByte < 0) return kModelUnread;
    return shownByte != 0 ? kModelShown : kModelHidden;
}

inline const char* ModelWord(int state)
{
    switch (state)
    {
    case kModelNone:   return "none";
    case kModelHidden: return "hidden";
    case kModelShown:  return "SHOWN";
    default:           return "unread";
    }
}

/* the report's symptom: the far model shown while the town's own area is loaded (1), not (0), unknown (-1) */
inline int Stuck(int state, int areaLoaded)
{
    if (state == kModelUnread || areaLoaded < 0) return -1;
    return (state == kModelShown && areaLoaded == 1) ? 1 : 0;
}

struct Fields
{
    int areaX, areaY;           /* the area whose load raised the check */
    long long frames;           /* zone ticks between the event and the line */
    double secs;                /* seconds between them */
    std::string name, sid;      /* the town's record name and stringID */
    int townX, townY;           /* the town centre's area */
    int state;                  /* ModelState */
    int meshMade;               /* the far-model object's +0x10 is set (1), not (0), unread (-1) */
    int townAreaLoaded;         /* the town centre's area: ZoneMap +0xB0 | +0xB1 (1 / 0, -1 unread) */
    int camX, camY;             /* ZoneManager +0x1681C8 / +0x1681CC: the camera square loadDistantTowns last worked from (-1 unread) */
    int playerX, playerY;       /* the watched character's area (zones.cpp g_playerSector) */
    long long lineNo;           /* this line's number this session (1-based) */
    long long dropped;          /* area checks dropped so far (the queue was full) */
};

/* A ' in a name is written as ` so the quoted name ends where it should */
inline std::string Quoted(const std::string& s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i) if (r[i] == '\'') r[i] = '`';
    return "'" + r + "'";
}

/* the line (without the log's own time stamp) */
inline std::string Line(const Fields& f)
{
    char a[200]; a[sizeof(a) - 1] = 0;
    _snprintf(a, sizeof(a) - 1, "[FARTOWN] T-1469 area %d,%d loaded %lld frames / %.2f s ago: town ", f.areaX, f.areaY, f.frames, f.secs);
    char b[400]; b[sizeof(b) - 1] = 0;
    _snprintf(b, sizeof(b) - 1,
              " townArea=%d,%d farModel=%s mesh=%d townAreaLoaded=%d cameraSquare=%d,%d playerArea=%d,%d stuck=%d line %lld/%d dropped=%lld",
              f.townX, f.townY, ModelWord(f.state), f.meshMade, f.townAreaLoaded, f.camX, f.camY, f.playerX, f.playerY,
              Stuck(f.state, f.townAreaLoaded), f.lineNo, kLineCap, f.dropped);
    return std::string(a) + Quoted(f.name) + " sid=" + Quoted(f.sid) + b;
}

}  // namespace fartown
