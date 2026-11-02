/* T-890 (2026-10-31) - THE TEST-ONLY `camturn` LEVER'S PURE PART: its arguments, and the camera rig's facing as a quaternion.
 * No engine types; the plugin (command_channel.cpp `camturn`, handoff.cpp CameraTurnPod / CameraReadPod) and the offline suite
 * (src/coop-test/test_main.cpp t890_camturn) compile this.
 *
 * WHY IT EXISTS: camlook / camfollow move only WHERE the camera looks (PlayerInterface::focusCamera sets the rig's position).
 * The rig keeps its facing, and its default sits south of its target looking north (+z), so a building stands between the
 * camera and its north side. T-890 needs a picture of a north door.
 *
 * THE ENGINE'S CAMERA RIG (Read, our disassembly of Steam 1.0.65):
 *   camera object = *(PlayerInterface + 0x30) (focusCamera 0x7F1F10 passes it on); +0x58 the target node, +0x70 the camera
 *   node (a child of the target node), +0x68 the Ogre camera. Its constructor (around 0x6B0575) puts the camera node at
 *   local (0, 0, -150) and gives the target node Quaternion(30 deg, UNIT_X); the player's rotate (0x6B0660) turns the target
 *   node with SceneNode::yaw in world space and Node::pitch in local space - so the target node's orientation is
 *   Qyaw(about Y) * Qpitch(about X), and the camera node's local z is minus the distance.
 *   The setter: PlayerInterface 0x7F1F20 (the function right after focusCamera) (this, const Quaternion& rot, float zoom) ->
 *   0x6AF0C0: target node setOrientation(rot), camera node setPosition(0, 0, zoom). The save loader (0x7AD374) calls the same
 *   with the saved "rot" and "zoom". zoom is the camera node's local z: NEGATIVE, minus the distance.
 *
 * YAW / PITCH HERE: yaw in degrees about the world up axis, 0 = the camera faces +z (it sits on the -z side of its target),
 * 90 = faces +x, 180 = faces -z (sits north of the target, looking south). pitch in degrees, positive = looking down; the
 * engine's default is 30. dist = how far the camera sits from its target (world units; the engine's default 150).
 * Quaternions are Ogre's order: w, x, y, z.
 *
 * ARGUMENTS: `camturn show` | `camturn [<yaw>] [yaw <deg>] [pitch <deg>] [dist <d>]` - at least one value; a value not given
 * keeps the camera's current one. Refused: a value given twice, an unknown word, a word with no number, a number that is not
 * finite, pitch outside kPitchMin..kPitchMax, dist outside kDistMin..kDistMax (both ranges are our own choice, not the
 * engine's limits: Guess at what is safe to look at).
 */
#ifndef COOP_CAMTURN_H
#define COOP_CAMTURN_H

#include <cmath>
#include <cstdlib>
#include <string>
#include <sstream>

namespace camturn {

static const float kPitchMin = 1.0f;
static const float kPitchMax = 89.0f;
static const float kDistMin = 10.0f;
static const float kDistMax = 3000.0f;
static const double kPi = 3.14159265358979323846;

struct Args
{
    int show;
    int haveYaw, havePitch, haveDist;
    float yaw, pitch, dist;
};

/* a whole token as a finite number (no trailing characters); 1 = read */
inline int ReadNum(const std::string& t, float* out)
{
    if (t.empty()) return 0;
    const char* s = t.c_str();
    char* end = 0;
    const double v = std::strtod(s, &end);
    if (end == s || *end != 0) return 0;
    if (!(v == v) || v > 1.0e7 || v < -1.0e7) return 0;
    *out = (float)v;
    return 1;
}

/* yaw folded into [0, 360) */
inline float NormYaw(float deg)
{
    double d = std::fmod((double)deg, 360.0);
    if (d < 0.0) d += 360.0;
    if (d >= 360.0) d -= 360.0;
    return (float)d;
}

/* rest = the text after the verb. 1 = good (out filled), 0 = refused */
inline int Parse(const std::string& rest, Args* out)
{
    Args a; a.show = 0; a.haveYaw = 0; a.havePitch = 0; a.haveDist = 0; a.yaw = 0.0f; a.pitch = 0.0f; a.dist = 0.0f;
    std::istringstream is(rest);
    std::string t;
    int n = 0;
    while (is >> t)
    {
        ++n;
        if (t == "show") { if (n != 1) return 0; a.show = 1; continue; }
        if (a.show) return 0;   /* show takes nothing after it */
        float v = 0.0f;
        if (t == "yaw" || t == "pitch" || t == "dist")
        {
            std::string num;
            if (!(is >> num) || !ReadNum(num, &v)) return 0;
            if (t == "yaw") { if (a.haveYaw) return 0; a.haveYaw = 1; a.yaw = v; }
            else if (t == "pitch") { if (a.havePitch) return 0; a.havePitch = 1; a.pitch = v; }
            else { if (a.haveDist) return 0; a.haveDist = 1; a.dist = v; }
        }
        else if (n == 1 && ReadNum(t, &v)) { a.haveYaw = 1; a.yaw = v; }   /* a bare first number is the yaw */
        else return 0;
    }
    if (!a.show && !a.haveYaw && !a.havePitch && !a.haveDist) return 0;
    if (a.havePitch && (a.pitch < kPitchMin || a.pitch > kPitchMax)) return 0;
    if (a.haveDist && (a.dist < kDistMin || a.dist > kDistMax)) return 0;
    if (a.haveYaw) a.yaw = NormYaw(a.yaw);
    *out = a;
    return 1;
}

/* q (w, x, y, z) = Qyaw(about Y) * Qpitch(about X): the target node's orientation for that facing */
inline void QuatFromYawPitch(float yawDeg, float pitchDeg, float q[4])
{
    const double hy = (double)yawDeg * kPi / 360.0, hp = (double)pitchDeg * kPi / 360.0;
    const double cy = std::cos(hy), sy = std::sin(hy), cp = std::cos(hp), sp = std::sin(hp);
    q[0] = (float)(cy * cp);
    q[1] = (float)(cy * sp);
    q[2] = (float)(sy * cp);
    q[3] = (float)(-sy * sp);
}

/* the facing a target-node orientation gives: its local +z axis (the way the camera faces) as yaw and pitch, in degrees.
   0 = q is not a usable rotation (length far from 1) */
inline int YawPitchFromQuat(const float q[4], float* yawDeg, float* pitchDeg)
{
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double len2 = w * w + x * x + y * y + z * z;
    if (!(len2 > 0.9 && len2 < 1.1)) return 0;
    const double fx = 2.0 * (x * z + w * y), fy = 2.0 * (y * z - w * x), fz = 1.0 - 2.0 * (x * x + y * y);
    double s = -fy; if (s > 1.0) s = 1.0; if (s < -1.0) s = -1.0;
    *pitchDeg = (float)(std::asin(s) * 180.0 / kPi);
    *yawDeg = NormYaw((float)(std::atan2(fx, fz) * 180.0 / kPi));
    return 1;
}

}   /* namespace camturn */

#endif
