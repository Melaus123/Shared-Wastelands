#pragma once
/* A building piece's placement input height. createBuilding puts a piece at the ground height under its (x,z) plus the
   y it is given, so the y a placement passes is a height ABOVE THE GROUND (the engine's own town placement passes 0).
   A standing piece's input height is its live y less the ground height at the same (x,z). Pure: no engine calls. */
namespace placeinput {

static const float kNoGround = -99.0f;   /* getTerrainHeight's "no terrain answered here" */

/* groundRead: 1 = ground holds a height, anything else = no height was read. 1 = *inY set; 0 = no usable ground
   height (*inY untouched). */
inline int InputYFromLive(float liveY, int groundRead, float ground, float* inY)
{
    if (groundRead != 1) return 0;
    if (ground == kNoGround) return 0;
    if (ground != ground || liveY != liveY) return 0;   /* NaN */
    *inY = liveY - ground;
    return 1;
}

}   /* namespace placeinput */
