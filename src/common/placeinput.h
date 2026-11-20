#pragma once
#include <string>
#include "boxrec.h"   /* BoxKeyParse: a record's key read for its height (KeyHeightOf) */
/* A building piece's placement input height. createBuilding's createPhysical 0x55FF50 puts a free-standing piece at the
   ENGINE'S ground under its (x,z) plus the y it is given, so the y a placement passes is a height above THAT ground (the
   engine's own town placement passes 0). The engine's ground is 0x9B2850 (row BuildingPlaceGround): the larger of
   getTerrainHeightFast 0x9B2420's height and 100.0 - not the terrain height itself (T-1372: a piece on terrain below 100
   was measured from the terrain and remade 100 - terrain higher each time). A standing piece's input height is its live y
   less the engine's ground at the same (x,z). Pure: no engine calls. */
namespace placeinput {

static const float kNoGround = -99.0f;   /* getTerrainHeightFast's "no terrain answered here" */
static const float kEngineGroundFloor = 100.0f;   /* 0x9B2850's floor (exe 1.0.65 0x1682250 / 1.0.68 0x1683250) - the tests' model only */

/* 0x9B2850's own rule, as its code runs it (comiss floor, terrain; ja -> floor): the floor when it is above the terrain
   height, else the terrain height (a NaN height passes through). The plugin calls the engine for it; this is the tests'
   statement of what that call answers. */
inline float EngineGroundOf(float terrain, float floor)
{
    return (floor > terrain) ? floor : terrain;
}

/* terrainRead: 1 = terrain holds getTerrainHeightFast's answer (anything else = not read). engineRead: 1 = engineGround holds
   0x9B2850's answer at the same (x,z). 1 = *inY set to liveY - engineGround; 0 = no input (*inY untouched): the terrain did
   not answer (-99 - 0x9B2850 would add 100 now, the real ground later), a call failed, or a value is NaN. */
inline int InputYFromLive(float liveY, int terrainRead, float terrain, int engineRead, float engineGround, float* inY)
{
    if (terrainRead != 1 || engineRead != 1) return 0;
    if (terrain == kNoGround) return 0;
    if (terrain != terrain || engineGround != engineGround || liveY != liveY) return 0;   /* NaN */
    *inY = liveY - engineGround;
    return 1;
}

/* T-1372 decisions 1691 + 1694: AN OLD PLACEMENT RECORD, CONVERTED ONLY WHEN IT WAS MEASURED THE OLD WAY. Builds before the fix stored
   SOME free-standing inputs as the live y less the TERRAIN height (getTerrainHeightFast 0x9B2420); createPhysical adds the engine's
   ground (0x9B2850 = the larger of the terrain and 100), so on terrain below 100 such an input lands (engineGround - terrain) too high.
   Other old inputs are the game's own (createBuilding's input: live y less the engine's ground) and must not be touched. Which one a
   record holds is read from its own key: the key's last number is the piece's live height x10 when the key was made (keyY). The old
   way gives ~ keyY - terrain, the game's own ~ keyY - engineGround; the input nearer the old way's is converted (inY - (engineGround -
   terrain)), any other kept. Both candidates within the key's precision (kKeyPrecision) of each other: it does not matter - kept.
   Every PLACE this build writes carries a mark ('measured the engine's way': buildwire.h kBuildGroundTag); a record judged here
   (converted or kept) is marked. *mark is the record's mark (0 / 1). hostEmpty 0 = furniture (host-local, no ground is added).
   Returns one of the kOld* values below. */
static const float kKeyPrecision = 0.1f;   /* a position key's tenths */
static const int kOldNothing = 0;      /* already marked (never a second judgement), or furniture (now marked) */
static const int kOldConverted = 1;    /* measured the old way: *inY converted, *mark set */
static const int kOldKept = 2;         /* the game's own input: *inY kept, *mark set */
static const int kOldTie = 3;          /* the two candidates within kKeyPrecision: *inY kept, *mark set */
static const int kOldNoKeyY = 4;       /* the key holds no readable height: *inY and *mark untouched - placed as it is, unconverted */
static const int kOldNoGround = -1;    /* no ground answered (or a NaN): *inY and *mark untouched - not placed now */
inline float AbsF(float v) { return v < 0.0f ? -v : v; }
inline int ConvertOldInput(int hostEmpty, unsigned char* mark, float* inY, int keyRead, float keyY, int terrainRead, float terrain, int engineRead, float engineGround)
{
    if (*mark != 0) return kOldNothing;
    if (hostEmpty == 0) { *mark = 1; return kOldNothing; }
    if (keyRead != 1 || keyY != keyY) return kOldNoKeyY;
    if (terrainRead != 1 || engineRead != 1 || terrain == kNoGround) return kOldNoGround;
    if (terrain != terrain || engineGround != engineGround || *inY != *inY) return kOldNoGround;   /* NaN */
    const float oldWay = keyY - terrain, gameOwn = keyY - engineGround;
    *mark = 1;
    if (AbsF(oldWay - gameOwn) < kKeyPrecision) return kOldTie;
    if (AbsF(*inY - oldWay) < AbsF(*inY - gameOwn)) { *inY = *inY - (engineGround - terrain); return kOldConverted; }
    return kOldKept;
}
/* a position key's height (sid@sx,sy@x10,z10,y10 - boxrec.h BoxKeyParse): 1 = *y set to y10 / 10; 0 = not a key, or no height in it */
inline int KeyHeightOf(const std::string& key, float* y)
{
    coopbrec::BoxKeyParts k;
    if (coopbrec::BoxKeyParse(key, &k) == 0 || k.haveY == 0) return 0;
    *y = (float)k.y10 / 10.0f;
    return 1;
}

}   /* namespace placeinput */
