/* src/common/editorstate.h - the TEST-ONLY read-only verb `editorstate` (hire.cpp): the character editor a hire with editing
   opens, read at the safe point. Pure parts: the argument and the per-recruit body test; and when the paused editor's recruits
   get the animation update call (hire.cpp EditorBodyStep - shipped behaviour, not a test lever). No engine types. */
#pragma once
#include <string>
#include <cstdlib>

namespace coopeditor {

const int kWatchMaxSeconds = 120;

/* "" -> 1 with *watchSecs 0 (one line at the next safe point); "watch <N>" (N 1..120, decimal) -> 1 with *watchSecs N (a line
   every second for N seconds); anything else 0. */
inline int EditorStateParse(const std::string& arg, int* watchSecs)
{
    *watchSecs = 0;
    size_t i = 0;
    while (i < arg.size() && arg[i] == ' ') ++i;
    if (i == arg.size()) return 1;
    if (arg.compare(i, 5, "watch") != 0) return 0;
    i += 5;
    if (i >= arg.size() || arg[i] != ' ') return 0;
    while (i < arg.size() && arg[i] == ' ') ++i;
    int n = 0, digits = 0;
    while (i < arg.size() && arg[i] >= '0' && arg[i] <= '9')
    {
        n = n * 10 + (arg[i] - '0');
        ++digits; ++i;
        if (n > kWatchMaxSeconds) return 0;
    }
    while (i < arg.size() && arg[i] == ' ') ++i;
    if (digits == 0 || i != arg.size() || n < 1) return 0;
    *watchSecs = n;
    return 1;
}

/* One recruit's body as the editor's update waits on it before it places the recruits and shows its widgets: the body model's
   entity is there (entityHere 1; its mesh is not read here), or no entity is there, none is pending (pendingHere 0) and the
   body's done byte is set (doneByte != 0). 1 ready, 0 still waited on. */
inline int EditorBodyReady(int entityHere, int pendingHere, int doneByte)
{
    if (entityHere != 0) return 1;
    return (pendingHere == 0 && doneByte != 0) ? 1 : 0;
}

/* Recruit bodies while the game is paused (hire.cpp EditorBodyStep): the most frames of animation-update calls one editor
   window gets before the one-time give-up line. */
const int kBodyStepGiveUpFrames = 600;

/* The editor window may have its recruits stepped this frame: the game is paused, the window was made in the hire mode (mode 1),
   it shows no recruit yet (it is still waiting on bodies) and it has had fewer than kBodyStepGiveUpFrames frames of calls.
   1 yes, 0 no. */
inline int EditorBodyStepDue(int paused, int modeOne, int onShowSet, int framesStepped)
{
    if (paused == 0 || modeOne == 0 || onShowSet != 0) return 0;
    return framesStepped < kBodyStepGiveUpFrames ? 1 : 0;
}

/* One recruit gets the call: the paused-frame update skips it (its faction's editor flag), it has a body object for the call to
   rebuild, and that body is not ready. 1 yes, 0 no. */
inline int EditorRecruitStepDue(int engineSkips, int bodyHere, int ready)
{
    return (engineSkips != 0 && bodyHere != 0 && ready == 0) ? 1 : 0;
}

/* The one-time give-up line: the window would still be stepped but for the frame limit, and a recruit still has no ready body.
   1 yes, 0 no. */
inline int EditorBodyGiveUpDue(int paused, int modeOne, int onShowSet, int framesStepped, int notReady)
{
    if (paused == 0 || modeOne == 0 || onShowSet != 0 || notReady <= 0) return 0;
    return framesStepped >= kBodyStepGiveUpFrames ? 1 : 0;
}

}   /* namespace coopeditor */
