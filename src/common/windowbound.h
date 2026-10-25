/* src/common/windowbound.h - T-986 (drift bound): A COPY'S NATIVE COMBAT WINDOW IS BOUNDED BY DISTANCE.
 *
 * On the friend's game a copy in a native combat window is moved by that game's own AI, not by the drive, and until T-986 the
 * window ended only on the owner's combat-mode OFF edge. In run T1425 the owners stood in attack mode (no OFF edge was sent) while
 * the copies ran to their target and stayed 5,000-6,700 units from their owners until the game quit. The drive now closes the
 * window, through the OFF edge's own steps, once the copy is farther from its owner's reported spot than the far snap's distance
 * (replicate.cpp kLostDist). Only a fresh ON from the owner opens a window again.
 *
 * PURE C++03 (VS2010 v100), header-only. Swept by the offline suite (t986_window_drift_bound). */
#ifndef WINDOWBOUND_H_INCLUDED
#define WINDOWBOUND_H_INCLUDED

namespace winbound {

/* true = close the window: the copy is in one and its horizontal distance from its owner's reported spot is past `bound`. A
   distance that is not a number or is infinite never closes (an unread position is no evidence); nor does a bound of 0 or below. */
inline bool DriftClosesWindow(bool inWindow, float dist, float bound)
{
    if (!inWindow) return false;
    if (!(bound > 0.0f)) return false;
    if (!(dist - dist == 0.0f)) return false;   /* NaN or infinite */
    return dist > bound;
}

/* T-986 fold 1: the two roads that close a window and what each undoes. Both gate the AI, wait out its pass and clear the
   attack order. Only the owner's OFF edge leaves combat mode and ends the copy's current action. A drift close does neither: the
   owner is still fighting (its swings play on a copy only in combat mode), and ending the action from the main thread is the
   F408 race the switch-off close refuses - combat mode and the action end with the owner's OFF edge. */
enum CloseKind { kCloseOwnerOff = 0, kCloseDrift = 1 };
inline bool CloseLeavesCombat(int kind) { return kind == kCloseOwnerOff; }
inline bool CloseEndsAction(int kind) { return kind == kCloseOwnerOff; }
inline const char* CloseReason(int kind) { return kind == kCloseDrift ? "drift" : (kind == kCloseOwnerOff ? "authority_off" : "unknown"); }

}   /* namespace winbound */

#endif
