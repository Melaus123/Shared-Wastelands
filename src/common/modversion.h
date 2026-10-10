/* src/common/modversion.h - THE MOD'S VERSION, in one place.
 * A version is major.minor.patch: the middle number goes up for a normal release, the last for a fix-only release, the first
 * reaches 1 when the mod leaves testing. It is shown in the first line of every game and world-server log, in every bug report and
 * on the title screen. The Steam Workshop upload tool takes a whole number: major x 10000 + minor x 100 + patch (0.3.0 -> 300), so
 * the number always goes up and reads back as the version. The game-to-game and world-server protocol numbers are separate: they say
 * which builds can play together. C++03 (VS2010 v100). */
#ifndef MODVERSION_H
#define MODVERSION_H

namespace modversion {

const int kMajor = 0;
const int kMinor = 3;
const int kPatch = 0;
const char* const kName = "0.3.0";   /* kMajor.kMinor.kPatch as text */

/* The Workshop upload tool's number for this version. */
inline int UploadNumber() { return kMajor * 10000 + kMinor * 100 + kPatch; }

}   // namespace modversion

#endif
