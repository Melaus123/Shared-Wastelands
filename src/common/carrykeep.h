/* src/common/carrykeep.h - T-1411 fix attempt 2, fold 2 (F1274, F1286): A CARRIED CHARACTER IS KEPT UPDATED AS IF ON THIS GAME'S
 * CAMERA FOR THE WHOLE CARRY - EVERY CARRY, DECIDED FROM THE ENGINE'S OWN 'BEING CARRIED' BYTE.
 *
 * The engine's carry step (the carried body's vt+0x268, 1.0.65 0x5CCF90) moves the body's scene node with its carrier's hand only
 * while the body's 'in this game's camera view' byte Character +0x1A9 is set (decomp_5ccf90:78). The AI worker's view pass
 * (0x786E00 -> 0x5C94D0) rewrites that byte every pass: 1 for a character the engine's keep-updated test says to keep fully
 * updated (0x5C6F30, decomp_5c94d0:38-40: its faction's +0x250, or the character's own keep-updated byte Character +0x250), else
 * 0 when it is far from or outside this game's camera. A carry this game shows off its own camera - because another game's
 * player is the one near it - left the body's node, its animation spot (anim+0x98) and its rag-doll behind, and the put-down
 * landed it there (F1258 / F1259 / F1273 / F1274).
 *
 * The mod hooks that keep-updated test (row CharacterKeepUpdatedTest: 1.0.65 0x5C6F30, 1.0.68 0x5C79C0): it calls the engine's
 * own test and answers 'keep updated' also when the character's own being-carried byte Character +0x3D4 is set - the byte the
 * engine's pick-up getPickedUp (0x5CE300:68) sets and its drop (0x5CD750:31), its put-down reset (0x5CC110:20) and the
 * constructor (0x621960:146) clear. So every carried character - another game's copy or this game's own, near another player or
 * not - is kept updated for exactly as long as the engine itself says it is carried: no list, no start or end roads.
 * Nothing writes +0x1A9 or +0x250 (the attempt-2 keep list, which wrote +0x250 twice a frame, is removed).
 *
 * The hook runs on the AI worker thread for every character the view pass walks: no allocation, no lock, no log line; the byte is
 * read under the crash guard. The [K1] carry keep lines are written on the main thread at the mod's carry log points.
 *
 * Pure: no engine memory, no globals, no Windows; the offline suite drives it. C++03 (VS2010 v100).
 */
#pragma once

namespace carrykeep {

/* The hook's answer: keep the character updated when the engine's own test says so, or when the character is being carried
   (Character +0x3D4 non-zero; 0 also when the byte could not be read). */
inline bool KeepUpdated(bool engineKeeps, int beingCarriedByte)
{
    return engineKeeps || beingCarriedByte != 0;
}

/* Did the hook change the engine's answer (the count on the REPORT)? Only a 'no' turned into 'keep'. */
inline bool HookTurnedToKeep(bool engineKeeps, int beingCarriedByte)
{
    return !engineKeeps && KeepUpdated(engineKeeps, beingCarriedByte);
}

/* The hook's install state, for the start line and the [K1] carry keep lines. */
const int kHookNotTried = 0;
const int kHookOn       = 1;
const int kHookFailed   = -1;   /* AddHook failed */
const int kHookNoRow    = -2;   /* no address row for this game build */

inline const char* HookText(int state)
{
    switch (state)
    {
    case kHookOn:     return "hook on: the keep-updated test answers keep while the being-carried byte is set";
    case kHookFailed: return "HOOK NOT INSTALLED (AddHook failed): a carry off this game's camera is not kept updated";
    case kHookNoRow:  return "HOOK NOT INSTALLED (no CharacterKeepUpdatedTest row): a carry off this game's camera is not kept updated";
    default:          return "hook not tried yet";
    }
}

}   /* namespace carrykeep */
