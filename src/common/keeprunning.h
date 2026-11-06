/* keeprunning.h - "KEEP THIS WORLD RUNNING": the world server's pure decisions for a world that outlives its host's game
   (SharedWastelandsServer.exe --keep-running), and the plain words its window shows. Pure: no I/O, no globals.

   Without --keep-running every answer here is "as before": the host's game closing is forwarded, --parent is obeyed and the
   window shows the whole technical log. */
#ifndef SW_KEEPRUNNING_H
#define SW_KEEPRUNNING_H

#include <cstddef>
#include <string>
#include <vector>
#include "upnpplan.h"   /* the router notes RouterLine words */

namespace keeprun {

const char* const kStopFile = "stop.request";            /* in the world folder: its arrival stops the world (the game's STOP WORLD, a service, Linux) */
const char* const kStopFileAside = "stop.request.taken"; /* where an acted-on or stale stop file is renamed to (never deleted) */
const int kExitRulesUnreadable = 36;                     /* the exit code of a kept world whose rules.txt could not be read */

/* START (owner 1508 b, 2026-11-05, in place of owner 1274): a kept world with a world password (the `world password =` line of its
   rules file, not empty) serves as before; one without serves OPEN - anyone who knows its address can join while its host is away -
   and its window says so once (OpenLine). passwordSet = rules.txt was read and its world password is not empty. */
enum StartVerdict { kStartServe = 0, kStartOpen = 1, kStartUnreadable = 2 };   /* kStartUnreadable: worldpw::KeptStart */
inline int StartDecide(bool keepRunning, bool passwordSet)
{
    return keepRunning && !passwordSet ? (int)kStartOpen : (int)kStartServe;
}

/* WORLD_STOPPING (91, down only; world-server protocol 131): a kept world server tells every admitted game it is stopping (the stop
   file, stop typed in its window, the window closed, a session end), at its final save, before its write queue drains and the
   process ends. Payload {u32 reason}: 1 = the world was stopped (the only reason; any other value, or another length, does not
   decode). A game in a running world shows DISCONNECTED / "The world was stopped." (ownrec.h kHlWorldStopped). A hosted world sends
   none: its games keep the host-left road. */
const unsigned int kMsgWorldStopping = 91u;
const unsigned int kStopReasonStopped = 1u;
inline bool WorldStoppingTells(bool keepRunning) { return keepRunning; }
inline void WorldStoppingEncode(unsigned int reason, std::vector<char>* b)
{
    b->clear();
    for (int i = 0; i < 4; ++i) b->push_back((char)((reason >> (8 * i)) & 0xFFu));
}
inline bool WorldStoppingDecode(const char* p, size_t n, unsigned int* reason)
{
    if (p == 0 || n != 4) return false;
    unsigned int v = 0;
    for (int i = 0; i < 4; ++i) v |= ((unsigned int)(unsigned char)p[i]) << (8 * i);
    if (v != kStopReasonStopped) return false;
    *reason = v;
    return true;
}

/* --parent closes this server with the game that started it; a kept world must not close with it, so it is ignored */
inline bool ParentObeyed(bool keepRunning, bool parentGiven) { return parentGiven && !keepRunning; }

/* A GAME'S SESSION_CLOSING (its game is exiting). Kept world: that game leaves NOW (its process ends too soon for the link's own
   close to be confirmed) and the closing is not forwarded - nobody else's world closes with it. Hosted world: forwarded as
   before, so the joiners take the host-left road. admitted = the sender holds a slot. */
enum ClosingVerdict { kClosingForward = 0, kClosingLeave = 1 };
inline int ClosingDecide(bool keepRunning, bool admitted, bool isClosing)
{
    return keepRunning && admitted && isClosing ? (int)kClosingLeave : (int)kClosingForward;
}

/* THE WORLD'S PAUSE: the clock moves only while at least one admitted game is in the world (a game still loading does not
   count). The other conditions are the clock's own: its hours are known, the step is sane, and the players' speed is not 0. */
inline bool ClockAdvances(bool hoursKnown, double dt, float speed, int inWorld)
{
    return hoursKnown && dt > 0.0 && dt < 60.0 && speed > 0.0f && inWorld > 0;
}

/* how the number of games in the world moved: the first one in, the last one out, or neither */
enum InWorldMove { kInWorldSame = 0, kInWorldFirstIn = 1, kInWorldEmptied = 2 };
inline int InWorldMoveOf(int before, int after)
{
    if (before <= 0 && after > 0) return kInWorldFirstIn;
    if (before > 0 && after <= 0) return kInWorldEmptied;
    return kInWorldSame;
}

/* A STEWARD MOVE TO A GAME ALREADY IN THE WORLD: that game walked its world long ago and sends no end-of-walk (SEED_END), so
   whatever waits for the steward's walk is released now. A move to a game still loading leaves the wait to its own walk. */
inline bool WalkDoneAtMove(const std::string& before, const std::string& after, bool afterInWorld)
{
    return !after.empty() && after != before && afterInWorld;
}

/* TYPED IN THE WINDOW: "stop" (any case, spaces and the line end around it allowed) stops the world */
inline bool IsStopWord(const std::string& typed)
{
    size_t a = 0, b = typed.size();
    while (a < b && (typed[a] == ' ' || typed[a] == '\t' || typed[a] == '\r' || typed[a] == '\n')) ++a;
    while (b > a && (typed[b - 1] == ' ' || typed[b - 1] == '\t' || typed[b - 1] == '\r' || typed[b - 1] == '\n')) --b;
    if (b - a != 4) return false;
    const char* w = "stop";
    for (size_t i = 0; i < 4; ++i) { char c = typed[a + i]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if (c != w[i]) return false; }
    return true;
}

/* THE WINDOW'S PLAIN LINES (owner 1285 a: the window shows only these; the technical log stays in its file) */
inline std::string NumberWord(unsigned v)
{
    char buf[16]; int n = 0;
    do { buf[n++] = (char)('0' + v % 10u); v /= 10u; } while (v != 0u && n < 15);
    std::string s;
    while (n > 0) s += buf[--n];
    return s;
}
/* the player's shown name; a game that gave none is "Player <slot>" */
inline std::string ShownName(const std::string& name, int slot)
{
    if (!name.empty()) return name;
    return slot >= 0 ? "Player " + NumberWord((unsigned)slot) : std::string("A player");
}
inline std::string TitleLine(const std::string& world) { return "Shared Wastelands world server - " + world; }
inline std::string HeaderLine() { return "Shared Wastelands world server"; }
inline std::string WorldLine(const std::string& world) { return "World: " + world; }
inline std::string PortLine(unsigned port) { return "Port: " + NumberWord(port); }
/* owner 1514 a (1): hasPassword = the world has a world password (one from rules.txt or a carried 0.4.0 lock, owner 1513 a) */
inline std::string RunningLine(bool hasPassword)
{
    return hasPassword ? "The world is running. Players join by address with the world's password." : "The world is running. Players join by address.";
}
inline std::string HowToStopLine() { return "To stop the world, close this window, or type stop and press Enter."; }
inline std::string JoinedLine(const std::string& shown) { return shown + " joined the world."; }
inline std::string LeftLine(const std::string& shown) { return shown + " left the world. The world keeps running."; }
inline std::string PausedLine() { return "Nobody is in the world. The world is paused."; }
inline std::string SavingLine() { return "Saving the world..."; }
inline std::string StoppedLine() { return "The world has stopped."; }
inline std::string TypeStopLine() { return "Type stop and press Enter to stop the world."; }
/* owner 1508 b / 1509: a world that starts with no world password says so once - a kept world in its window, any other in its log */
inline std::string OpenLine() { return "This world has no password: anyone who knows its address can join."; }
/* owner 1374 (2) / 1510 a: a world kept running whose world password line is emptied (or whose rules.txt is deleted) keeps its
   password until it is started again (words: owner 1514 a (2)) */
inline std::string KeepsPasswordLine()
{
    return "The world password line was emptied. The world keeps its password until it is started again; started with the line empty, it will have no password.";
}
/* A REFUSED START KEEPS ITS WINDOW (T-1254, owner 1512 a): after a kept world's refusal lines and the rules file's full path, the window
   says this and waits for Enter - only when it can be seen and is this process's own (visibleWindow: the console has a window that
   is shown; ownConsole: no other process shares it; consoleInput: stdin is that console). A world server started hidden, or by a
   test harness with no window, never waits. */
inline std::string PressEnterLine() { return "Press Enter to close this window."; }
inline bool RefusedWindowHolds(bool visibleWindow, bool ownConsole, bool consoleInput) { return visibleWindow && ownConsole && consoleInput; }
/* T-1255 C (review #2): a world server started minimized or hidden never waits for Enter - the game starts a kept world so
   (STARTUPINFO STARTF_USESHOWWINDOW, SW_SHOWMINNOACTIVE 7) and reads its exit code, and the test tools start it with Start-Process
   -WindowStyle Minimized (SW_SHOWMINIMIZED 2). useShowWindow: STARTF_USESHOWWINDOW set; showWindow: wShowWindow - SW_HIDE 0,
   SW_SHOWMINIMIZED 2, SW_MINIMIZE 6, SW_SHOWMINNOACTIVE 7 or SW_FORCEMINIMIZE 11 is "minimized or hidden". */
inline bool StartedMinimized(bool useShowWindow, unsigned showWindow)
{
    return useShowWindow && (showWindow == 0u || showWindow == 2u || showWindow == 6u || showWindow == 7u || showWindow == 11u);
}

/* THE ROUTER LINES: a world kept running asks the home router itself to let players from the internet in on its port (UPnP,
   src/coop-plugin/upnp.cpp), and closes that opening when it stops. One line for each note (src/common/upnpplan.h kNote*);
   "" for a note it does not know. */
inline std::string RouterLine(int note, unsigned port)
{
    const std::string p = "port " + NumberWord(port) + " (UDP)";
    switch (note)
    {
    case coopupnp::kNoteMapped:     return "The router is now open on " + p + ": players can join from the internet.";
    case coopupnp::kNoteAlready:    return "The router was already open on " + p + ": players can join from the internet.";
    case coopupnp::kNoteFailed:     return "The router did not open " + p + ". Players from the internet may not get in - open that port on the router by hand.";
    case coopupnp::kNoteNoRouter:   return "No router answered, so " + p + " was not opened. Players from the internet may not get in - open that port on the router by hand.";
    case coopupnp::kNoteRemoved:    return "The router's opening on " + p + " is closed.";
    case coopupnp::kNoteGone:       return "The router's opening on " + p + " had already closed.";
    case coopupnp::kNoteNotRemoved: return "The router did not close " + p + ". It closes by itself within an hour, or the next time this world stops.";
    case coopupnp::kNoteLeft:       return "The router's opening on " + p + " was not made by this world server, so it is left open.";
    case coopupnp::kNoteNotRemovedNoLimit:
                                    return "The router did not close " + p + ". Close it on the router by hand, or it closes the next time this world stops.";
    default:                        return std::string();
    }
}

}   /* namespace keeprun */

#endif
