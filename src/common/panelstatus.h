/* src/common/panelstatus.h - U2: WHAT THE MULTIPLAYER PANEL SAYS ABOUT THE LINK, AS A PURE DECISION.
 *
 * U1's status area held ONE fixed paragraph, so there was nothing to get wrong and nothing to test.
 * U2's status area is the only place a player can see whether hosting started, whether the other game
 * answered, whether the world's notebook is up and which player number this game holds - and every one
 * of those sentences is a CHOICE over live readings.  design-ui-panel 3.3 writes that choice as four
 * ordered states.  It lives here rather than in ui.cpp for the reason 6a lesson 11 gives: a rule kept in
 * step by hand between the shipped code and its test drifts, so there is one implementation and the
 * offline suite sweeps it with the same compiler that builds the plugin.
 *
 * ONE CORRECTION OF RECORD AGAINST design-ui-panel 3.3.  Its state 2 is
 * "!SessionLinked() and role is client -> waiting for the host", which leaves a HOST that has started
 * listening and has nobody connected falling through to state 3 ("connected to the other game").
 * net::SessionLinked() is `g_transport != 0 && State() == LINK_UP` and enet_transport.cpp raises LINK_UP
 * only on an ENET_EVENT_TYPE_CONNECT (Read, enet_transport.cpp:168), so a host alone reads NOT linked.
 * The design's four states therefore tell a host who is waiting for a friend that it is connected to
 * them.  There are FIVE states here, and the extra one is the host's own wait.
 * The link these states read is now the WORLD SERVER's (PanelLinkFacts::worldLinked), every game's one door - there is no
 * session link any more - so a host reads "waiting" only until its own world server's link is up.
 *
 * NO LOCALE, ANYWHERE.  RE_Kenshi imbues a process-wide locale that inserts digit-group separators into
 * numbers (F030), and the panel prints a port and an attempt count.  PanelNum below is hand-rolled
 * decimal conversion: no stream, no printf, no locale to imbue and none to forget.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.  Header-only in the manner of panelfit.h.
 */
#pragma once

#include <string>
#include <vector>   /* mp4: the Hosting screen's adapters and players */
#include <map>      /* mp5: the Game options screen reads the world's options.txt rows */
#include <utility>
#include <cstdio>
#include "gpoptions.h"   /* mp5: the notebook's own gp.* / gt.* range check */
#include "diskformat.h"  /* the newer-format refusal words */

namespace coopui {

/* The three roles.  These are the same three numbers as coop::RoleId and coopcfg::kCfgSingle/Host/Client;
   config.cpp already carries a compile-time assertion that those two agree, and ui.cpp carries one for
   these.  A pure header may not include either, which is exactly why the assertion is at the call site. */
const int kPanelRoleSingle = 0;
const int kPanelRoleHost   = 1;
const int kPanelRoleClient = 2;

/* THE FIVE STATES, in the order they are tested.  The first that matches wins. */
enum PanelLinkState
{
    kPanelNotConnected    = 0,   /* role=single: this game opens no link at all */
    kPanelHostWaiting     = 1,   /* hosting, nobody has connected yet */
    kPanelWaitingHost     = 2,   /* joining, the host has not answered yet */
    kPanelWaitingNotebook = 3,   /* the other game is connected, the world's notebook is not */
    kPanelConnected       = 4    /* both links up and the notebook has welcomed us */
};

/* THE NOTEBOOK PROCESS's own states (design-ui-panel 2.2: CreateProcess returning TRUE proves only that a
   process was created, so the positive proof is the WELCOME and the negative proof is the exit code). */
enum PanelNotebookState
{
    kNbNotOurs      = 0,   /* we did not start one - the player pointed us at one already running */
    kNbStarting     = 1,   /* started, still running, has not welcomed us yet */
    kNbRunning      = 2,   /* started, still running, and it has answered */
    kNbStoppedEarly = 3,   /* the process we started has exited */
    kNbNoExe        = 4,   /* SharedWastelandsServer.exe is not in the mod folder beside the plugin */
    kNbSpawnFailed  = 5    /* CreateProcess itself refused */
};

/* ------------------------------------------------------------------------------------------------
   NUMBER TO TEXT, WITH NO LOCALE INVOLVED AT ALL (F030).
   ------------------------------------------------------------------------------------------------ */
inline std::string PanelNum(long long v)
{
    char b[24];
    int i = 24;
    bool neg = (v < 0);
    /* -(-9223372036854775808) overflows a signed long long, so the negation is done in unsigned. */
    unsigned long long u = neg ? ((unsigned long long)(-(v + 1)) + 1ull) : (unsigned long long)v;
    if (u == 0ull) { b[--i] = '0'; }
    while (u != 0ull) { b[--i] = (char)('0' + (int)(u % 10ull)); u /= 10ull; }
    if (neg) b[--i] = '-';
    return std::string(b + i, (size_t)(24 - i));
}

/* An <address>:<port> for display.  Empty address or port 0 is "none" - the same shape config.cpp's own
   AddrText uses, so the panel and the log name an unset link the same way. */
inline std::string PanelAddrText(const std::string& addr, int port)
{
    if (addr.empty() || port == 0) return std::string("none");
    return addr + ":" + PanelNum((long long)port);
}

/* ------------------------------------------------------------------------------------------------
   THE FACTS THE PANEL READS LIVE, AS A POD.  Every field is read at the moment the line is built and
   never cached (design-ui-panel 3.1): StoreWelcomedThisLink() "can still answer 1 for a link that has
   since dropped", so notebookUp is the AND of the welcome and the live link, computed by the caller.
   ------------------------------------------------------------------------------------------------ */
struct PanelLinkFacts
{
    int       role;              /* kPanelRoleSingle / Host / Client */
    int       worldLinked;       /* the world-server link is up right now, welcomed or not (StoreLinkIsUp()) - every game's one door */
    int       peers;             /* net::SessionPeerCount() */
    int       notebookUp;        /* StoreWelcomedThisLink() && StoreLinkIsUp() */
    long long dials;             /* ConfigDialCount() - the re-dials config.cpp has already made */
    long long dialCap;           /* ConfigDialCap() - read rather than repeated */
    int       mySlot;            /* StoreMySlot(), -1 = the notebook has not numbered us */
    long long worldKeyMismatch;  /* ConfigWorldKeyMismatches() */
    int       hostStartFailed;   /* a host's listen port did not bind - nothing in the game sets it now (a busy port stops the world
                                    server, which says so in PanelNotebookText's kNbStoppedEarly line), so its words are not shown */

    PanelLinkFacts()
        : role(kPanelRoleSingle), worldLinked(0), peers(0), notebookUp(0),
          dials(0), dialCap(60), mySlot(-1), worldKeyMismatch(0), hostStartFailed(0)
    {
    }
};

inline int PanelLinkStateOf(const PanelLinkFacts& f)
{
    if (f.role != kPanelRoleHost && f.role != kPanelRoleClient) return kPanelNotConnected;
    /* The world server is the one door: "waiting" until its link is up, "joining" while it is up and has not welcomed this game
       (the handshake, or its profile lobby), "connected" once it has. */
    if (f.worldLinked == 0)
        return (f.role == kPanelRoleHost) ? kPanelHostWaiting : kPanelWaitingHost;
    if (f.notebookUp == 0) return kPanelWaitingNotebook;
    return kPanelConnected;
}

/* mp1 (design-mpmenu1 section 7): a join whose tries have ALL gone unanswered. One test, used by the sentence below and by
   the Join screen's Try again button, so the words and the button cannot disagree. */
inline int PanelJoinGaveUp(const PanelLinkFacts& f)
{
    return (PanelLinkStateOf(f) == kPanelWaitingHost && f.dialCap > 0 && f.dials >= f.dialCap) ? 1 : 0;
}

/* For the [P005] verdict token: a state a run can grep for without parsing a sentence. */
inline const char* PanelLinkStateName(int state)
{
    switch (state)
    {
        case kPanelNotConnected:    return "notConnected";
        case kPanelHostWaiting:     return "hostWaiting";
        case kPanelWaitingHost:     return "waitingHost";
        case kPanelWaitingNotebook: return "waitingNotebook";
        case kPanelConnected:       return "connected";
        default:                    return "??";
    }
}

/* T-201 N1 (owner-approved wording, 2026-09-29) - THE PRESS: HOST / JOIN connect when pressed, and CANCEL or a failure
   disconnects with no restart. `mode` is 0 HOST, 1 JOIN. World and profile names are always in "double" quotes. */
const unsigned kPressHostStartMs = 60000u;   /* HOST: the world must answer within this after the press */
const unsigned kPressJoinWorldMs = 60000u;   /* JOIN: the host's world must answer within this after the host did */
inline std::string PressQuoted(const std::string& name) { return "\"" + name + "\""; }
inline std::string PressIntroText(int mode)
{
    return mode == 0 ? std::string("Select a world and press HOST.")
                     : std::string("Enter the host's address (for example 81.2.3.4:7777) and press JOIN.");
}
inline std::string PressStatusText(int mode, const std::string& world, const std::string& addr)
{
    return mode == 0 ? "Starting " + PressQuoted(world) + "..." : "Connecting to " + addr + "...";
}
inline const char* PressBoxTitle(int mode) { return mode == 0 ? "CAN'T HOST" : "CAN'T JOIN"; }
inline std::string PressHostPortText(int port)
{
    return "Couldn't host on port " + PanelNum((long long)port)
         + ". It may be in use or blocked by a firewall. Choose a different port and try again.";
}
const char* const kPressHostLateText     = "The world didn't start in time. Try again.";
const char* const kPressJoinNoAnswerText = "The host isn't responding. Check the address and make sure the host is hosting, then try again.";
const char* const kPressJoinLateText     = "The host's world didn't respond in time. Try again.";

/* THE SENTENCE.  `addr`/`port` are what this game is set up to reach: the LISTEN port for a host, the
   host's address for a client.  `timeMode` is TimeModeName() and may be null. */
inline std::string PanelStatusText(const PanelLinkFacts& f, const std::string& addr, int port,
                                   const char* timeMode)
{
    const int state = PanelLinkStateOf(f);
    std::string s;

    if (state == kPanelNotConnected)
    {
        return s;   /* T-201 N1: no idle line - nothing is online until HOST or JOIN is pressed */
    }

    if (state == kPanelHostWaiting)
    {
        if (f.hostStartFailed != 0)
        {
            s = PressHostPortText(port);   /* T-201 N1: the CAN'T HOST box's words */
            return s;
        }
        s = "Hosting on port " + PanelNum((long long)port) + ". Waiting for players...";
    }
    else if (state == kPanelWaitingHost)
    {
        /* mp1 (design-mpmenu1 section 7): the Join screen's words. words1: "the host is hosting" matches the host's own lines
           ("Hosting on port N" above, PanelHostingText's "Hosting.") - change them together. */
        if (PanelJoinGaveUp(f) != 0)
        {
            s = kPressJoinNoAnswerText;   /* T-201 N1: the CAN'T JOIN box's words; TRY AGAIN is gone */
            return s;
        }
        s = "Connecting to " + PanelAddrText(addr, port) + "...";   /* T-201 N1: no attempt count */
    }
    else if (state == kPanelWaitingNotebook)
    {
        s = "Connected. Joining world...";
    }
    else
    {
        if (f.mySlot < 0)
            s = "Connected. Joining world...";
        else
            s = "Connected. You are player " + PanelNum((long long)(f.mySlot + 1)) + ".";
        /* U2-c (review-u2 L-1) - THE PEER COUNT REACHES A SENTENCE.  PanelLinkFacts::peers was filled
           from net::SessionPeerCount() on every status build and then read by nothing, which is a silent
           zero: a reading nobody can see is indistinguishable from a reading that is broken.  It is
           either used or removed, and it is worth using - it is the only place the player can see that
           the count of games matches what they expect. */
        if (f.peers == 1)
            s += " 1 other player connected.";
        else if (f.peers > 1)
            s += " " + PanelNum((long long)f.peers) + " other players connected.";
        /* mp1: the time mode is no longer said here - design-mpmenu1 section 1 lists it as jargon, and one of its values
           names the notebook. The `timemode` verb and the log still say it. */
        (void)timeMode;
    }

    if (f.worldKeyMismatch > 0)
        s += "\n\nWorld mismatch: you and the host have different worlds selected. One of you must change the world"
             " name.";

    return s;
}

/* The notebook process's own line, kept apart from the link's so a host reads two independent facts
   rather than one sentence that has to be true of both. */
inline std::string PanelNotebookText(int state, int port, long exitCode)
{
    switch (state)
    {
        case kNbStarting:
            return "Starting the world on port " + PanelNum((long long)port) + "...";
        case kNbRunning:
            return std::string();   /* words1 (wording audit 9): "running" is nothing the player can act on - no line */
        case kNbStoppedEarly:
            if (exitCode == swformat::kServerExitFormatRefused) return swformat::FormatHostRefusedText();   /* its folder was written by a newer build */
            return "The world couldn't start (error " + PanelNum((long long)exitCode)
                 + "). Close other programs that may be using port " + PanelNum((long long)port) + ", or restart your computer.";   /* words1b (owner 2026-09-27) */
        case kNbNoExe:
            return "A multiplayer file is missing. Reinstall the mod.";
        case kNbSpawnFailed:
            return "The world couldn't start. Try again, or reinstall the mod.";
        default:
            return std::string();
    }
}

/* mp1 (design-mpmenu1 section 7) - WHY THE SHARED WORLD SAID NO, in the Join screen's words. `reason` is
   coopstore::StoreRefuseReason (src/common/storelink.h: 0 versions, 1 no player id, 2 already in, 3 full, 4 lifetime
   full, 5 mods) - a pure header may not include it, so the offline suite ties the numbers. -1 = not refused. The mods
   sentence is store.cpp's own (it names what differs) and is passed in; empty falls back to the design's words. */
inline std::string PanelRefusalText(int reason, const std::string& modsText, int hostMode = 0)   /* T-201 N1b: hostMode 1 = under CAN'T HOST */
{
    switch (reason)
    {
        case -1: return std::string();
        case 1:  return hostMode != 0 ? std::string("Couldn't host: your player ID couldn't be sent. Try again.")   /* T-201 N1b (owner 165) */
                                      : std::string("Couldn't join: your player ID couldn't be sent. Restart Kenshi and try again.");
        case 2:  return "You're already in this world from another Kenshi window. Close it first.";
        case 3:  return "This world is full. Try again when someone leaves.";
        case 4:  return "This world has reached its player limit.";
        case 6:  return "You're already playing in this world from another Kenshi window. Close it first.";   /* prof1: kRefuseSamePerson */
        case 100: return !modsText.empty() ? modsText
                       : hostMode != 0 ? std::string("Couldn't host: no profile is available. Try again.")   /* T-201 N1b (owner 165) */
                       : std::string("Couldn't join: no profile is available.");   /* prof1 fold: this game's own profile dead end; store.cpp's sentence */
        case 5:  return !modsText.empty() ? modsText
                      : std::string("Mod mismatch: use the same mods as the host, in the same order, then restart"
                                    " Kenshi and join again.");
        default: return "Version mismatch: this multiplayer mod is a different version from the one running the world. Every"
                         " player needs the same version.";   /* words1b: host-neutral - the host's own game can see it too */
    }
}

/* T-201 N1 - THE PRESS IN PROGRESS, AS A PURE DECISION: still waiting, done, or failed with the box's words. */
enum { kPressWait = 0, kPressDone = 1, kPressFailed = 2 };
struct PressFacts
{
    int       mode;             /* 0 HOST, 1 JOIN */
    int       armed;            /* the title tick has opened the links since the press (ConfigArmGen moved) */
    int       hostStartFailed;  /* a host's listen port did not bind - nothing in the game sets it now (see PanelLinkFacts) */
    int       nbState;          /* the world server process we started (kNb*) */
    long      nbExit;           /* its exit code when it stopped */
    int       nbPort;           /* its port */
    int       port;             /* HOST: the game port */
    int       worldLinked;      /* the world-server link is up right now, welcomed or not (StoreLinkIsUp()) */
    int       notebookUp;       /* the world has welcomed this game on a live link */
    long long dials, dialCap;   /* JOIN: re-dials made and the cap */
    int       refusal;          /* StoreRefusedReason(), -1 none */
    unsigned  elapsedMs;        /* since the press */
    unsigned  linkedMs;         /* JOIN: since the world-server link first came up after the press, 0 = not yet */
    int       wrongWorld;       /* a WELCOME was refused since the last leave (StoreWrongWorldRefusal): a kWrongWorld* kind, 0 = none */
    std::string openedWorld, theirWorld;   /* ... the world this game already opened, and the one the WELCOME named */
    long      lobbyGen;         /* T552: the store link generation the world's profile LOBBY list arrived on (StoreLobbyAnswerGen), -1 none */
    long      linkGen;          /* T552: the store link's current generation (StoreLinkGen) - a lobby list counts only when the two agree */
    PressFacts() : mode(0), armed(0), hostStartFailed(0), nbState(kNbNotOurs), nbExit(0), nbPort(27016), port(7777),
                   worldLinked(0), notebookUp(0), dials(0), dialCap(60), refusal(-1), elapsedMs(0), linkedMs(0), wrongWorld(0),
                   lobbyGen(-1), linkGen(0) {}
};
/* T-201 N1 fold - config.cpp ConfigWorldSwitchRefused's sentence, word for word (already approved). */
inline std::string PressWrongWorldText(const std::string& opened, const std::string& wanted)
{
    return "This game already opened world \"" + opened + "\". Restart Kenshi to play world \"" + wanted + "\".";
}
/* T-490 (owner, approved) - the world this game opened was deleted and made again under its name: the new one needs a restart. */
inline std::string PressRemadeWorldText(const std::string& world)
{
    return "This game already opened an earlier world named \"" + world + "\". Restart Kenshi to play the new one.";
}
/* Why a WELCOME was refused (StoreWrongWorldRefusal): another world's name; the same name remade; this computer's copy of the world
   written by a newer build. */
enum WrongWorldKind { kWrongWorldNone = 0, kWrongWorldName = 1, kWrongWorldRemade = 2, kWrongWorldNewerFormat = 3 };
inline std::string PressWrongWorldBox(int kind, int mode, const std::string& opened, const std::string& theirs)
{
    if (kind == kWrongWorldRemade) return PressRemadeWorldText(theirs);
    if (kind == kWrongWorldNewerFormat) return mode == 0 ? swformat::FormatHostRefusedText() : swformat::FormatJoinRefusedText();
    return PressWrongWorldText(opened, theirs);
}
/* T-201 N1 fold (finding 1b) - A HOST PRESS ON THE WORLD THIS GAME ALREADY HOSTS, WITH THE SAME PORT, only shows HOSTING: no leave,
   no restart. hostedFolder empty = nothing hosted. The port field (trimmed by the caller) must read the hosted port's digits. */
inline bool PressSameHosted(const std::string& hostedFolder, const std::string& pickedFolder, const std::string& portField, int hostedPort)
{
    if (hostedFolder.empty() || hostedFolder != pickedFolder || hostedPort <= 0) return false;
    return portField == PanelNum((long long)hostedPort);
}
inline int PressVerdictOf(const PressFacts& f, const std::string& modsText, std::string* box)
{
    std::string b;
    /* T552: THE WORLD'S PROFILE LOBBY IS AN ANSWER. A game that has picked no profile is put in the lobby (the PROFILES list) and
       gets no WELCOME until it picks - and the pick comes after the press. So the lobby list on THIS link counts as the world
       having answered, exactly as a WELCOME does; a list from an older link generation never counts. */
    const int lobbyAnswered = (f.armed != 0 && f.lobbyGen >= 1 && f.lobbyGen == f.linkGen) ? 1 : 0;
    if (f.refusal >= 0) b = PanelRefusalText(f.refusal, modsText, f.mode == 0 ? 1 : 0);   /* T-201 N1b: a HOST press's refusal says host */
    else if (f.armed != 0 && f.wrongWorld != 0) b = PressWrongWorldBox(f.wrongWorld, f.mode, f.openedWorld, f.theirWorld);   /* T-201 N1 fold: at once, not after 60 s */
    else if (f.mode == 0)
    {
        if (f.nbState == kNbNoExe || f.nbState == kNbSpawnFailed || f.nbState == kNbStoppedEarly)
            b = PanelNotebookText(f.nbState, f.nbPort, f.nbExit);
        else if (f.armed != 0 && f.hostStartFailed != 0) b = PressHostPortText(f.port);
        else if (f.armed != 0 && (f.notebookUp != 0 || lobbyAnswered != 0)) return kPressDone;   /* T552: WELCOME or lobby */
        else if (f.elapsedMs >= kPressHostStartMs) b = kPressHostLateText;
    }
    else
    {
        /* JOIN goes through the world server alone: done at its WELCOME or its profile lobby on a live link (T552). CAN'T JOIN when
           neither came and every allowed dial went unanswered (config.cpp's title re-dial counts them); the late box when its link
           is up and neither came within kPressJoinWorldMs of the link first coming up. */
        if (f.armed != 0 && f.worldLinked != 0 && (f.notebookUp != 0 || lobbyAnswered != 0)) return kPressDone;
        if (f.armed != 0 && f.notebookUp == 0 && lobbyAnswered == 0 && f.dialCap > 0 && f.dials >= f.dialCap) b = kPressJoinNoAnswerText;
        else if (f.worldLinked != 0 && f.notebookUp == 0 && lobbyAnswered == 0 && f.linkedMs >= kPressJoinWorldMs) b = kPressJoinLateText;
    }
    if (b.empty()) return kPressWait;
    if (box) *box = b;
    return kPressFailed;
}

/* THE GIVE-UP SENTENCE.  words1 (owner wording audit 2026-09-27): it no longer shows the cap - a count of tries is
   nothing the player can act on; the log line beside the call prints it.  History, review-p8i-b's LOW: the shipped message
   hardcoded "5" beside a `kPanelFailStreakCap` that a later edit could move, so the number on screen and
   the number in the code were kept in step by hand. */
inline std::string PanelGiveUpMessage(long cap)
{
    (void)cap;
    return "The multiplayer menu couldn't open. Press MULTIPLAYER to try again.";
}

/* mp3: WHEN A WORLD WAS LAST PLAYED, in words for the Host a game list.  lastUnix 0 = unknown; fresh = the folder holds
   nothing but its world.txt (made with New world, never hosted).  A clock that runs behind reads as "just now".
   ui2b: the words sit under the list's LAST PLAYED head, so they no longer repeat it ("3 days ago", "unknown"), as the
   profiles list's PanelProfilePlayedText reads.  Each wording covers ONE unbroken stretch of time (the offline suite sweeps
   it): the head sorts by a map from these words to a time (ui.cpp OnCoopWorldListLess). */
inline std::string PanelPlayedText(long long lastUnix, long long nowUnix, int fresh)
{
    if (fresh != 0) return "not played yet";
    if (lastUnix <= 0) return "unknown";
    long long d = nowUnix - lastUnix;
    if (d < 0) d = 0;
    const long long kDay = 86400;
    if (d < 120)        return "just now";
    if (d < 3600)       return PanelNum(d / 60) + " minutes ago";
    if (d < 7200)       return "an hour ago";
    if (d < kDay)       return PanelNum(d / 3600) + " hours ago";
    if (d < 2 * kDay)   return "yesterday";
    if (d < 14 * kDay)  return PanelNum(d / kDay) + " days ago";
    if (d < 60 * kDay)  return PanelNum(d / (7 * kDay)) + " weeks ago";
    if (d < 730 * kDay) return PanelNum(d / (30 * kDay)) + " months ago";
    return PanelNum(d / (365 * kDay)) + " years ago";
}

/* prof3 (design-mpmenu1 section 8) - THE YOUR PROFILES SCREEN, as pure decisions the offline suite sweeps.
   WHEN A PROFILE WAS LAST PLAYED, in words.  lastUnix is unix seconds (the world's helper stamps NowUnix() on each pick,
   coop-store/store_main.cpp), 0 = never played.  Whole 24-hour days since now, not calendar days: "today" is "within the
   last 24 hours".  A clock that runs behind reads as "today". */
inline std::string PanelProfilePlayedText(long long lastUnix, long long nowUnix)
{
    if (lastUnix <= 0) return "never";
    long long d = nowUnix - lastUnix;
    if (d < 0) d = 0;
    const long long days = d / 86400;
    if (days == 0)  return "today";
    if (days == 1)  return "yesterday";
    if (days < 60)  return PanelNum(days) + " days ago";
    if (days < 730) return PanelNum(days / 30) + " months ago";
    return PanelNum(days / 365) + " years ago";
}
/* One row of the list: "> " before the picked one, then the name, the faction and when it was last played. */
inline std::string PanelProfileRowText(int selected, const std::string& name, const std::string& faction, long long lastUnix, long long nowUnix)
{
    return std::string(selected ? "> " : "") + name + "   " + faction + "   " + PanelProfilePlayedText(lastUnix, nowUnix);
}
/* NEW PROFILE is allowed below the host's cap (cap 0 = no answer yet, never "at the cap"); at or over it the button is
   greyed and the screen says the design's line.  A cap lowered below what a player has deletes nothing (design-profiles1). */
inline int PanelProfileNewAllowed(unsigned have, unsigned cap) { return (cap == 0 || have < cap) ? 1 : 0; }
inline std::string PanelProfileCapLine(unsigned have, unsigned cap)
{
    if (PanelProfileNewAllowed(have, cap)) return std::string();
    return "Profile limit reached (" + PanelNum((long long)have) + " of " + PanelNum((long long)cap)
         + "). Delete a profile to create a new one.";
}

/* ------------------------------------------------------------------------------------------------
   mp4 (design-mpmenu1 section 6) - THE HOSTING SCREEN, as pure decisions the offline suite sweeps.
   ------------------------------------------------------------------------------------------------ */

/* Windows' adapter types (ipifcons.h IF_TYPE_ETHERNET_CSMACD / IF_TYPE_IEEE80211); net/homeaddr.cpp asserts both. */
const int kNicEthernet = 6;
const int kNicWifi     = 71;

/* One network adapter as Windows lists it (GetAdaptersAddresses order). ipv4 holds a.b.c.d as (a<<24)|(b<<16)|(c<<8)|d. */
struct PanelNic
{
    int ifType, up, hasGateway;
    std::vector<unsigned long> ipv4;
    PanelNic() : ifType(0), up(0), hasGateway(0) {}
};

/* An address a friend on the same home network could reach: not nothing, not loopback (127.x), not "this network"
   (0.x), not multicast or reserved (224 and up), and not 169.254.x (Windows gives itself that when no router answered). */
inline int PanelIpv4Usable(unsigned long a)
{
    const unsigned long top = (a >> 24) & 0xFFul;
    if (top == 0 || top == 127 || top >= 224) return 0;
    if (((a >> 16) & 0xFFFFul) == 0xA9FEul) return 0;
    return 1;
}

inline std::string PanelIpv4Text(unsigned long a)
{
    return PanelNum((long long)((a >> 24) & 0xFFul)) + "." + PanelNum((long long)((a >> 16) & 0xFFul)) + "."
         + PanelNum((long long)((a >> 8) & 0xFFul)) + "." + PanelNum((long long)(a & 0xFFul));
}

/* THE PICK: the first usable IPv4 address of the first adapter that is UP, is Ethernet or Wi-Fi, and HAS A ROUTER (a
   default gateway) - the router is what tells the home network's adapter apart from a virtual one (Hyper-V, VirtualBox,
   a VPN's switch), which is Ethernet too but has none.  If no such adapter has one, the first up Ethernet / Wi-Fi
   adapter's usable address.  "" = none.  *pickedNic = the adapter's index, -1 = none. */
inline std::string PanelPickHomeAddr(const std::vector<PanelNic>& nics, int* pickedNic)
{
    if (pickedNic) *pickedNic = -1;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t i = 0; i < nics.size(); ++i)
        {
            const PanelNic& n = nics[i];
            if (n.up == 0 || (n.ifType != kNicEthernet && n.ifType != kNicWifi)) continue;
            if (pass == 0 && n.hasGateway == 0) continue;
            for (size_t k = 0; k < n.ipv4.size(); ++k)
            {
                if (PanelIpv4Usable(n.ipv4[k]) == 0) continue;
                if (pickedNic) *pickedNic = (int)i;
                return PanelIpv4Text(n.ipv4[k]);
            }
        }
    }
    return std::string();
}

/* What Copy puts on the clipboard: "192.168.1.20:7777", "" with no address. */
inline std::string PanelHostCopyText(const std::string& ip, int port)
{
    if (ip.empty()) return std::string();
    return ip + ":" + PanelNum((long long)port);
}

inline std::string PanelHomeAddrLine(const std::string& ip, int port)
{
    if (ip.empty()) return "Couldn't find your local IP address.";
    return PanelHostCopyText(ip, port);   /* ui5: the value only - LOCAL ADDRESS is its own label, level with it (no colon) */
}

/* THE INTERNET ADDRESS ROW on HOSTING (owner 439 / 452 / 454), under LOCAL ADDRESS, hidden until SHOW.  Where the address
   comes from: the home router first, asked when the window opens (upnp.cpp UpnpAddrAsk - nothing outside the home is
   contacted then); when the router gives none, the address-lookup websites (addrlookup.h, in their order), asked only when
   the player presses SHOW or COPY (upnp.cpp UpnpLookupAsk).  The port is the game port LOCAL ADDRESS shows (not secret).
   Kenshi's fonts carry no bullet glyph (data/gui/fonts/kenshi_fonts.xml: codes 32-126 and 8127-8217, U+2022 is 8226), so
   the mask is asterisks.
   The states: ASKING the router is being asked; FOUND an address (the router's or a website's); MISSING the router gave
   none and no website has been asked yet; LOOKING the websites are being asked; FAILED no website answered - SHOW and COPY
   stay greyed until the window is opened again. */
enum PanelNetAddrState { kNetAddrAsking = 0, kNetAddrFound = 1, kNetAddrMissing = 2, kNetAddrLooking = 3, kNetAddrFailed = 4 };
/* A SHOW or COPY press waiting for an address (none = nothing waits). */
enum PanelNetPress { kNetPressNone = 0, kNetPressShow = 1, kNetPressCopy = 2 };
inline std::string PanelNetAddrMask(int port)
{
    return "***.***.***.***:" + PanelNum((long long)port);
}
/* An address is in hand: what SHOW shows and COPY copies. */
inline int PanelNetAddrHave(int state, const std::string& ip) { return (state == kNetAddrFound && !ip.empty()) ? 1 : 0; }
/* The row's value.  An address in hand: the mask, or ip:port once SHOW is pressed.  While the router is asked: the mask
   (the window opens hidden either way), or "Finding..." once a press waits on the answer.  The router gave none: the mask
   until a press.  The websites are asked: "Finding...".  None answered: "Couldn't find...". */
inline std::string PanelNetAddrLine(int state, const std::string& ip, int port, int shown, int pending)
{
    if (state == kNetAddrFound) return ip.empty() ? std::string("Couldn't find your internet address.")
                                                  : (shown != 0 ? PanelHostCopyText(ip, port) : PanelNetAddrMask(port));
    if (state == kNetAddrLooking || (state == kNetAddrAsking && pending != kNetPressNone)) return "Finding your internet address...";
    if (state == kNetAddrAsking || state == kNetAddrMissing) return PanelNetAddrMask(port);
    return "Couldn't find your internet address.";
}
/* What one event does to the row: the state after it, the press still waiting, and what the caller does now - ask the
   websites (startLookup), show the address (showNow), flip SHOW / HIDE (toggleShown), copy the address (copyNow). */
struct PanelNetStep { int state; int pending; int startLookup; int showNow; int toggleShown; int copyNow; };
inline PanelNetStep PanelNetStepNone(int state, int pending)
{
    PanelNetStep s;
    s.state = state; s.pending = pending; s.startLookup = 0; s.showNow = 0; s.toggleShown = 0; s.copyNow = 0;
    return s;
}
/* An address arrived for the presses that waited on it: SHOW shows it, COPY copies it without showing it. */
inline PanelNetStep PanelNetFoundFor(int pending)
{
    PanelNetStep s = PanelNetStepNone(kNetAddrFound, kNetPressNone);
    if (pending == kNetPressShow) s.showNow = 1;
    if (pending == kNetPressCopy) s.copyNow = 1;
    return s;
}
/* SHOW or COPY pressed (press = kNetPressShow / kNetPressCopy).  With an address: SHOW flips SHOW / HIDE, COPY copies.
   While the router is asked: the press waits for its answer.  The router gave none: the websites are asked and the press
   waits.  While they are asked: the newest press waits.  None answered: nothing (the buttons are greyed). */
inline PanelNetStep PanelNetAddrPress(int state, int pending, int press)
{
    if (press != kNetPressShow && press != kNetPressCopy) return PanelNetStepNone(state, pending);
    if (state == kNetAddrFound)
    {
        PanelNetStep s = PanelNetStepNone(state, kNetPressNone);
        if (press == kNetPressShow) s.toggleShown = 1; else s.copyNow = 1;
        return s;
    }
    if (state == kNetAddrAsking || state == kNetAddrLooking) return PanelNetStepNone(state, press);
    if (state == kNetAddrMissing)
    {
        PanelNetStep s = PanelNetStepNone(kNetAddrLooking, press);
        s.startLookup = 1;
        return s;
    }
    return PanelNetStepNone(state, pending);
}
/* The router answered (found 1 = an address).  Found: the waiting press is done.  None: with a press waiting the websites
   are asked; without one the row waits for a press.  An answer in any other state is an old one and changes nothing. */
inline PanelNetStep PanelNetRouterAnswer(int state, int pending, int found)
{
    if (state != kNetAddrAsking) return PanelNetStepNone(state, pending);
    if (found != 0) return PanelNetFoundFor(pending);
    if (pending == kNetPressNone) return PanelNetStepNone(kNetAddrMissing, kNetPressNone);
    PanelNetStep s = PanelNetStepNone(kNetAddrLooking, pending);
    s.startLookup = 1;
    return s;
}
/* The websites answered (found 1 = one gave a usable address).  None: FAILED, until the window opens again. */
inline PanelNetStep PanelNetLookupAnswer(int state, int pending, int found)
{
    if (state != kNetAddrLooking) return PanelNetStepNone(state, pending);
    if (found != 0) return PanelNetFoundFor(pending);
    return PanelNetStepNone(kNetAddrFailed, kNetPressNone);
}
/* The row's value as shown: wrapped at word breaks to `perLine` characters (ui.cpp: the value width over kPanelTextCharW, the
   panel's own fallback character width), the same rule as NoticeLinesFor; a word longer than a line is never split. */
inline std::string PanelWrapText(const std::string& text, int perLine)
{
    if (perLine < 10) perLine = 10;
    std::string out;
    int col = 0;
    size_t i = 0;
    while (i < text.size())
    {
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        const int wl = (int)(j - i);
        if (col > 0 && col + 1 + wl > perLine) { out += "\n"; col = 0; }
        else if (col > 0) { out += " "; ++col; }
        out += text.substr(i, j - i);
        col += wl;
        i = j;
        while (i < text.size() && text[i] == ' ') ++i;
    }
    return out;
}
/* The row's height in half-rows for its wrapped value: one row for one line, two rows otherwise. */
inline int PanelNetAddrHalves(const std::string& wrapped) { return wrapped.find('\n') == std::string::npos ? 2 : 4; }
/* SHOW / COPY on the internet row are greyed only once no website answered (until the window opens again). */
inline int PanelNetAddrButtonsOn(int state) { return state != kNetAddrFailed ? 1 : 0; }
/* HIDE only while the address is on screen. */
inline std::string PanelNetAddrShowCaption(int state, const std::string& ip, int shown) { return (PanelNetAddrHave(state, ip) != 0 && shown != 0) ? "HIDE" : "SHOW"; }
/* The line under the two address rows. */
inline std::string PanelAddrNoteText()
{
    return "Players in your home use the local address. Everyone else uses the internet address.";
}
/* What a COPY press says (the HOSTING status area).  netRow 0 = LOCAL ADDRESS, 1 = INTERNET ADDRESS; text = what was put on
   (or failed to reach) the clipboard.  A failed internet copy never prints the address - it may be hidden on screen. */
inline std::string PanelCopyNote(int netRow, int copied, const std::string& text)
{
    if (copied != 0) return "Address copied. Players paste it on the JOIN GAME screen.";
    if (text.empty()) return "There is no address to copy.";
    if (netRow != 0) return "Couldn't copy. Press SHOW to see the address.";
    return "Couldn't copy. Your address is " + text + ".";
}

/* ui5 (panel-mockups.md) - ONE TITLE: the window's caption names the screen (no repeated first row).  hostMode 1 = this
   game hosts (the PROFILES title then names the world); hostWorld the hosted world, optWorld the one GAME OPTIONS edits. */
inline std::string PanelScreenTitle(int screen, int profDlg, int hostMode, const std::string& hostWorld, const std::string& optWorld)
{
    if (screen == 7)
    {
        if (profDlg == 1) return "NEW PROFILE";
        if (profDlg == 2) return "DELETE PROFILE?";
        return (hostMode != 0 && !hostWorld.empty()) ? "PROFILES: " + hostWorld : std::string("PROFILES");   /* NEW WORDING: was "PROFILES - " */
    }
    if (screen == 6) return "GAME OPTIONS: " + optWorld;
    if (screen == 5) return "HOSTING: " + hostWorld;
    if (screen == 4) return "DELETE WORLD?";
    if (screen == 3) return "NEW WORLD";
    if (screen == 2) return "JOIN GAME";
    if (screen == 1) return "HOST GAME";
    return "MULTIPLAYER";
}

/* ui5b (review LOW) - A TITLE LONGER THAN ITS WINDOW is cut with "..." - measured against an estimate of the caption font,
   kPanelTitleCharW px a capital, with the close button and the frame's ends kept clear.  winW <= 0 = not measured yet. */
const int kPanelTitleCharW = 9;
inline std::string PanelTitleFit(const std::string& title, int winW)
{
    if (winW <= 0) return title;
    const int fits = (winW - 64) / kPanelTitleCharW;
    if (fits < 4 || (int)title.size() <= fits) return title;
    return title.substr(0, (size_t)(fits - 3)) + "...";
}

/* ui5d (T485: the router help said "forward UDP ports 0 and 27016") - THE GAME PORT THE HOSTING SCREEN NAMES, never 0.
   livePort: the port this game hosts on (ui.cpp passes ConfigHostPort() only while it is set up to host - a joiner's is
   the FRIEND's port, and a game set up for neither has 0); fieldPort: the HOST GAME PORT box's value (0 = not a valid
   port); else kPanelDefaultGamePort - the value that box starts with (ui.cpp PanelDefaults) and that cfgtext.cpp
   CfgPortFieldOk's refusal names as the default. */
const int kPanelDefaultGamePort = 7777;
inline int PanelHostingPort(int livePort, int fieldPort)
{
    if (livePort > 0 && livePort <= 65535) return livePort;
    if (fieldPort > 0 && fieldPort <= 65535) return fieldPort;
    return kPanelDefaultGamePort;
}

/* The router help: the one port to forward by hand when the router does not open it itself. */
inline std::string PanelRouterHelpText(int port)   /* the world server's one port: every game joins on it */
{
    return "To play over the internet, forward UDP port " + PanelNum((long long)port)
         + " on your router. If Windows Firewall asks, allow access.";
}

/* The first line of the Hosting screen's status area.  isHost: this game is set up to host; nbState: this computer's
   shared-world program (kNb*). */
inline std::string PanelHostingText(int isHost, int hostStartFailed, int nbState, int port)
{
    if (isHost == 0) return "Not hosting. Press BACK, select a world and press HOST.";
    if (hostStartFailed != 0)
        return "Port " + PanelNum((long long)port) + " is in use. Press BACK, change the port and press HOST.";
    if (nbState == kNbStoppedEarly || nbState == kNbNoExe || nbState == kNbSpawnFailed)
        return "The world couldn't start. Try again.";
    return "Hosting.";   /* words1b (owner 2026-09-27): the PLAYERS box says "Waiting for players..."; the joiner's give-up line says "the host is hosting" - change together */
}

/* ui1 (ui-polish-audit 3.3): the title-screen error box's title, chosen from its sentence - CAPITALS like Kenshi's own boxes.
   words1 (owner wording audit 2026-09-27): "Couldn't join" (PanelRefusalText 1 / 100) also reads CAN'T JOIN; the mods
   title is MOD MISMATCH, matched by ModRefusalText's "Mod mismatch:" start or any sentence naming "mods".
   T-246 (owner 200): the warning for a joiner let in with other mods (coopmods::ModWarnText) is MODS DON'T MATCH, keyed on its
   two starts ahead of that catch-all. */
/* PP3d (owner-approved 2026-09-27, exact text) - THE IDENTITY BOXES, shown on the title screen instead of HOST / JOIN while the
   player's identity is unusable (coopdata::IdentitySessionOnlyKindOf: 1 UNREADABLE, 2 DAMAGED). */
const char* const kIdBox1Title    = "CAN'T START MULTIPLAYER";
const char* const kIdBox1Text     = "Kenshi couldn't read your multiplayer data. Another program (such as antivirus or a backup tool)"
                                    " may be using it. Restart Kenshi and try again.";
const char* const kIdBox1Ok       = "OK";
const char* const kIdBox2Title    = "MULTIPLAYER DATA DAMAGED";
const char* const kIdBox2Text     = "Your multiplayer data is damaged and couldn't be recovered. You can continue as a new player,"
                                    " but worlds you've played before won't recognise your characters.";
const char* const kIdBox2Cancel   = "CANCEL";
const char* const kIdBox2Continue = "CONTINUE AS NEW PLAYER";
inline const char* IdBoxTitle(int kind) { return kind == 2 ? kIdBox2Title : kIdBox1Title; }
inline const char* IdBoxText(int kind)  { return kind == 2 ? kIdBox2Text : kIdBox1Text; }
inline int IdBoxButtons(int kind)       { return kind == 2 ? 2 : 1; }
/* The box's first button: OK (kind 1) or CANCEL (kind 2) - both only close it. The second (kind 2 only) is CONTINUE AS NEW PLAYER. */
inline const char* IdBoxDismissCaption(int kind) { return kind == 2 ? kIdBox2Cancel : kIdBox1Ok; }

/* T-201 N1b (owner 166): the name-taken box (NameTakenText) - its OK returns to JOIN GAME with the PLAYER NAME box selected. */
inline bool NoticeIsNameTaken(const std::string& text)
{ return text.compare(0, 10, "The name \"") == 0 && text.find("\" is already taken in this world.") != std::string::npos; }
inline std::string NoticeTitle(const std::string& text)
{
    if (text.compare(0, 18, "You could not load") == 0) return "CAN'T LOAD";
    if (text.compare(0, 18, "You could not join") == 0) return "CAN'T JOIN";
    if (text.compare(0, 13, "Couldn't join") == 0) return "CAN'T JOIN";
    /* ui5 (panel-mockups.md section 10): two texts that stop a join but carry neither start take CAN'T JOIN by name - the
       name-taken box (PP6b builds its check) and this game's own unreadable mod list (modlist.h ModUnreadableText). */
    if (NoticeIsNameTaken(text)) return "CAN'T JOIN";   /* T-201 N1: double quotes; N1b: one test */   /* T-201 N1: double quotes */
    if (text.compare(0, 42, "Your game could not read its own mod list,") == 0) return "CAN'T JOIN";
    /* ui5b: the two profile-save texts that stop a load (kSaveNotHereText, kSaveNoBackupText below) take CAN'T LOAD by name. */
    { const std::string gone("This profile's save couldn't be found on this computer."); if (text.compare(0, gone.size(), gone) == 0) return "CAN'T LOAD"; }   /* T-201 PP6' (owner 176 B) */
    /* T-201 PP6' (owner 176 A, C, D): the one press's boxes (LoadFail names the title too). */
    if (text.compare(0, 24, "Couldn't load your game.") == 0) return "CAN'T LOAD";
    if (text.compare(0, 51, "The connection to the world was lost while loading.") == 0) return "CAN'T LOAD";
    if (text.compare(0, 14, "Couldn't host:") == 0) return "CAN'T HOST";
    if (text.compare(0, 29, "Couldn't delete that profile.") == 0) return "CAN'T DELETE";   /* T-220 (owner 180) */
    if (text.compare(0, 54, "This profile's save is damaged and there is no backup.") == 0) return "CAN'T LOAD";
    /* T-201 PP5 fold (H1): a load refused while a profile's folder is decided (coopprof::kLoadAutoText) is a CAN'T LOAD too. */
    { const std::string autoLoad("In multiplayer your game loads automatically."); if (text.compare(0, autoLoad.size(), autoLoad) == 0) return "CAN'T LOAD"; }
    std::string low(text);
    for (size_t i = 0; i < low.size(); ++i) if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] - 'A' + 'a');
    { const std::string w1("Your mods don't match the host's"), w2("Your mods are the same as the host's");   /* T-246 (owner 200): coopmods::ModWarnText */
      if (text.compare(0, w1.size(), w1) == 0 || text.compare(0, w2.size(), w2) == 0) return "MODS DON'T MATCH"; }
    if (low.compare(0, 12, "mod mismatch") == 0 || low.find("mods") != std::string::npos) return "MOD MISMATCH";   /* the refusal's words (T-247 path) */
    return "MULTIPLAYER";
}

/* ui5 - THE ERROR / IDENTITY BOX AS TALL AS ITS TEXT (panel-mockups.md sections 10-11).  ui7: the box is built at this
   ESTIMATE (a line kNoticeLineH high, a character kNoticeCharW wide) and then re-fitted to its MEASURED text (ui.cpp
   BoxFitToText: MyGUI's wrapped text size and line height, NoticeClientHFor / NoticeLayoutFor below); the estimate stays
   only as the fallback when MyGUI reports 0 (counted).  NoticeLinesFor wraps at spaces as the word-wrapped box does. */
const int kNoticeCharW = 9;   /* ui5b: was 8 - the fallback's character width (ui7: the real text is measured) */
const int kNoticeLineH = 18;   /* ui6: was 20 - the panel's own 18 px line (panelfit.h kPanelTextLinePx); ui7: the fallback's line only */
inline int NoticeLinesFor(const std::string& text, int perLine)
{
    if (perLine < 10) perLine = 10;
    int lines = 1, col = 0;
    size_t i = 0;
    while (i < text.size())
    {
        if (text[i] == '\n') { ++lines; col = 0; ++i; continue; }
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\n') ++j;
        const int wl = (int)(j - i);
        if (col > 0 && col + 1 + wl > perLine) { ++lines; col = 0; }
        col += (col > 0 ? 1 : 0) + wl;
        while (col > perLine) { ++lines; col -= perLine; }
        i = j;
        while (i < text.size() && text[i] == ' ') ++i;
    }
    return lines;
}
/* ui6 (panel-mockups.md sections 10-11; T491 at 1920x1080 showed the buttons straight under the text) - THE BOX'S INSIDE,
   one rule for the error box and both identity boxes: the text kNoticePadPx from the top and as tall as its text (ui7:
   measured; the estimated lines only as the fallback), ONE BLANK LINE (ui7: one MEASURED line height; kNoticeLineH in the fallback), the button row (kNoticeBtnH, a fixed height - ui5's H / 5 in 26..40 grew with the box),
   kNoticePadPx under it.  The frame is Kenshi_WindowCX's 45 (read in panelfit.h; Kenshi_WindowC's own was not read -
   Inferred alike). */
const int kNoticeFramePx = 45;
const int kNoticePadPx   = 6;
const int kNoticeBtnH    = 30;
const int kNoticeSparePx = 9;    /* review-ui6 #2: the 9 px wrap guard of the FALLBACK estimate (ui7: a measured box needs none) */
struct NoticeLayout { int textY, textH, btnY, btnH; };
/* For a client area `clientH` tall (the Window's client, not the whole box). A box cut short by a small screen shrinks its
   text area first and keeps the blank line until only one line of text is left (review-ui6 #3: the comment said the reverse). */
/* ui7: `blankH` = the blank line's height - the MEASURED line height (ui.cpp BoxFitToText); 0 or less = kNoticeLineH. */
inline NoticeLayout NoticeLayoutFor(int clientH, int blankH)
{
    if (blankH <= 0) blankH = kNoticeLineH;
    NoticeLayout lay;
    lay.btnH  = kNoticeBtnH;
    lay.textY = kNoticePadPx;
    lay.btnY  = clientH - kNoticePadPx - kNoticeBtnH;
    if (lay.btnY < lay.textY) lay.btnY = lay.textY;
    lay.textH = lay.btnY - blankH - lay.textY;
    if (lay.textH < blankH) lay.textH = blankH;
    return lay;
}
inline NoticeLayout NoticeLayoutIn(int clientH) { return NoticeLayoutFor(clientH, kNoticeLineH); }   /* the fallback's blank line */
/* ui7 - THE CLIENT HEIGHT FOR A MEASURED TEXT: padding, the text area (`textH` = MyGUI's wrapped text height plus the edit
   box's skin inset; at least one line), ONE BLANK LINE (`lineH`, measured), the button row, padding.  No wrap guard: nothing
   here is estimated.  The builders add the window's own frame (measured: the box's height minus its client's). */
inline int NoticeClientHFor(int textH, int lineH)
{
    if (lineH <= 0) lineH = kNoticeLineH;
    if (textH < lineH) textH = lineH;
    return kNoticePadPx + textH + lineH + kNoticeBtnH + kNoticePadPx;
}
/* The whole box for a window `boxW` wide: frame, padding, the text's lines, one blank line, the button row, padding.
   ui7: the box is BUILT at this estimate and re-fitted to its measured text (it stays so only when the measurement fails).
   ui6: no spare LINE any more (ui5b kept one, and the text sat centred in it); review-ui6 #2: a 9 px wrap guard instead. */
inline int NoticeBoxH(const std::string& text, int boxW)
{
    const int lines = NoticeLinesFor(text, (boxW - 6 - 16) / kNoticeCharW);
    return kNoticeFramePx + kNoticePadPx + lines * kNoticeLineH + kNoticeSparePx + kNoticeLineH + kNoticeBtnH + kNoticePadPx;
}

/* ui6 (decision 42) - THE FOUR STRIPS THAT BLOCK THE TITLE MENU AROUND A BOX.  A parent pw x ph and the box's rectangle
   (l t w h, clamped into the parent); out[4*i .. 4*i+3] = left, top, width, height of the top, bottom, left and right
   strip.  With the box's rectangle they tile the parent exactly once, and none covers any pixel of the box (the offline
   suite sweeps both), so no pick order can hand a strip a click meant for the box.  A strip may be empty (width or height
   0); the return value is how many are not. */
inline int BoxBlockStrips(int pw, int ph, int l, int t, int w, int h, int* out)
{
    if (pw < 0) pw = 0;
    if (ph < 0) ph = 0;
    int x0 = l, y0 = t, x1 = l + w, y1 = t + h;
    if (x0 < 0) x0 = 0;
    if (x0 > pw) x0 = pw;
    if (y0 < 0) y0 = 0;
    if (y0 > ph) y0 = ph;
    if (x1 < x0) x1 = x0;
    if (x1 > pw) x1 = pw;
    if (y1 < y0) y1 = y0;
    if (y1 > ph) y1 = ph;
    out[0]  = 0;  out[1]  = 0;  out[2]  = pw;      out[3]  = y0;        /* top: the full width above the box */
    out[4]  = 0;  out[5]  = y1; out[6]  = pw;      out[7]  = ph - y1;   /* bottom: the full width below it */
    out[8]  = 0;  out[9]  = y0; out[10] = x0;      out[11] = y1 - y0;   /* left of it, its own height */
    out[12] = x1; out[13] = y0; out[14] = pw - x1; out[15] = y1 - y0;   /* right of it, its own height */
    int n = 0;
    for (int i = 0; i < 4; ++i) if (out[4 * i + 2] > 0 && out[4 * i + 3] > 0) ++n;
    return n;
}

/* ui5 - APPROVED TEXTS FOR THE EFFORTS THAT BUILD THEIR MECHANICS (panel-mockups.md, owner-approved 2026-09-27).  Kept here
   so those efforts use these exact words; NOTHING in this effort shows them yet. */
/* PP6b - the name-taken box (title CAN'T JOIN by NoticeTitle). */
inline std::string NameTakenText(const std::string& name)
{ return "The name \"" + name + "\" is already taken in this world. Change PLAYER NAME and try again."; }   /* T-201 N1: double quotes */
/* PP6 - after PLAY on PROFILES: a played profile loads, a never-played one opens Kenshi's NEW GAME. */
inline std::string ProfLoadingText(const std::string& name)     { return "Loading \"" + name + "\"..."; }   /* T-201 N1: double quotes */
inline std::string ProfOpeningNewGameText(const std::string& name) { return "Opening NEW GAME for \"" + name + "\"..."; }
/* T-201 PP6' (approved 2026-09-29): a profile made on PROFILES - PLAY (or the HOST press) loads it by itself now, so the line
   names nothing more to press. */
inline std::string ProfCreatedAutoText(const std::string& name) { return "Profile \"" + name + "\" created."; }
/* T-201 PP6' - THE PRESS AFTER THE WORLD'S ANSWER, AS PURE STEPS (ui.cpp LoadTick). ready = the world has admitted the picked
   profile and its folder is decided; autoVerdict = coopprof::AutoLoadDecide (0 load, 1 NEW GAME, 2 missing), -1 before ready;
   mode 1 HOST, 2 JOIN; waitedMs since the world answered (HOST) or PLAY (JOIN). */
enum { kLoadStepWait = 0, kLoadStepShow = 1, kLoadStepLate = 2, kLoadStepMissing = 3 };
inline int LoadAdmitStep(int ready, int autoVerdict, unsigned waitedMs, int mode)
{
    if (ready == 0) return waitedMs >= (mode == 1 ? kPressHostStartMs : kPressJoinWorldMs) ? kLoadStepLate : kLoadStepWait;
    return autoVerdict == 2 ? kLoadStepMissing : kLoadStepShow;
}
/* The line while the panel closes: Loading "<profile>"... for a load, Opening NEW GAME for "<profile>"... for a new game. */
inline std::string LoadLineOf(int autoVerdict, const std::string& profile)
{
    return autoVerdict == 0 ? ProfLoadingText(profile) : ProfOpeningNewGameText(profile);
}
/* CANCEL (BACK's place) works until the load or NEW GAME can go: stage 1 (the world admits the profile) and stage 2 (the line up, waiting
   for the engine to take a request - nothing is posted yet); greyed from stage 3 (the panel closes and the post follows). T-220. */
inline int LoadCancelAllowed(int stage) { return stage < 3 ? 1 : 0; }
/* PROFILES' main button (owner 143 / 159): SELECT when HOST GAME's CHANGE opened it, PLAY otherwise. */
inline const char* ProfMainButtonText(int selectMode) { return selectMode != 0 ? "SELECT" : "PLAY"; }
/* T-201 PP6' (owner 134 + 141 + 143): HOST GAME's PROFILE row - the profile the HOST press plays; "<name> (new)" while the world
   has none of this player's yet (made at the press); empty with no world picked. */
inline std::string HostProfileValue(const std::string& name, int isNew, int worldPicked)
{
    if (worldPicked == 0) return std::string();
    return isNew != 0 ? name + " (new)" : name;
}
/* T-201 PP6' fold (owner 176, approved 2026-09-29) - THE ONE PRESS'S BOXES. Each has one OK that returns to HOST GAME or JOIN GAME
   (ui.cpp LoadFail, which also leaves the world). A: CAN'T LOAD - the load could not be posted / the panel did not close / NEW GAME's
   button was not found. B: CAN'T LOAD - the profile's save is not on this computer (played before and no folder, or a folder with no
   quick.save). C: CAN'T HOST - the host's pick could not be sent. D: CAN'T LOAD - the link or the pick changed while loading. */
const char* const kLoadFailText     = "Couldn't load your game. Try again.";
/* T-220 (owner 180, approved 2026-09-29): HOST GAME -> CHANGE's DELETE could not edit the world's file - CAN'T DELETE, one OK. */
const char* const kProfDeleteFailText  = "Couldn't delete that profile. Try again.";
const char* const kProfDeleteFailTitle = "CAN'T DELETE";
const char* const kSaveNotHereText  = "This profile's save couldn't be found on this computer. Copy your save folder from the other computer,"
                                      " or start a new profile.";
const char* const kHostPickFailText = "Couldn't host: your profile couldn't be picked. Try again.";
const char* const kLoadLinkLostText = "The connection to the world was lost while loading. Try again.";
/* T-201 PP6' fold (item 6) - THE LOAD'S POST, AS A PURE STEP (ui.cpp LoadTick stage 2 and LoadTailAct). ready: 1 = the engine takes a
   request now (SaveManager+0xA0 reads 0); 0 = a request is pending (+0xA0 != 0 - readable, and the engine clears it when that request
   is done); -1 = never on this title (no SaveManager, anySavesExist() false, or the post was tried and nothing was posted). A pending
   request is waited on - its own state, read every title frame - up to kLoadPostWaitMs (the 60 s an accepted save is given to finish,
   store.cpp WorldKeySaveTickRow). waitedMs = since the Loading line showed (stage 2) or the panel was told to close (stage 3). */
const unsigned kLoadPostWaitMs = 60000;
enum { kPostWait = 0, kPostGo = 1, kPostFail = 2 };
inline int LoadPostStep(int ready, unsigned waitedMs)
{
    if (ready > 0) return kPostGo;
    if (ready < 0) return kPostFail;
    return waitedMs >= kLoadPostWaitMs ? kPostFail : kPostWait;
}
/* T-201 PP6' fold (item 4): JOIN's PLAY, refused by the world while the one press waits for it to admit the pick (stage 1) - the wait
   ends at once with the world's words (was: 60 s, the pick gone). HOST's refusals end in LoadHostPickStep the same way. */
inline int LoadPickRefused(int loadStage, int loadMode, int verdict) { return (loadStage == 1 && loadMode == 2 && verdict != 0) ? 1 : 0; }
/* PP9 - the SAVE DAMAGED box (whenWords: the LAST PLAYED words, "3 hours ago"); the no-backup CAN'T LOAD text. */
const char* const kSaveDamagedTitle  = "SAVE DAMAGED";
const char* const kSaveDamagedCancel = "CANCEL";
const char* const kSaveDamagedLoad   = "LOAD BACKUP";
inline std::string SaveDamagedText(const std::string& whenWords)
{
    return "This profile's save is damaged and can't be loaded. Load the backup from " + whenWords
           + " instead? Progress the world has kept since then is added back after loading.";
}
const char* const kSaveNoBackupText = "This profile's save is damaged and there is no backup. Start a new profile.";
/* ui4 - the PLAYERS window (Escape menu). */
const char* const kPlayersTitle       = "PLAYERS";
const char* const kPlayersColPlayer   = "PLAYER";
const char* const kPlayersColFaction  = "FACTION";
const char* const kPlayersColYours    = "YOUR STANCE";
const char* const kPlayersColTheirs   = "THEIR STANCE";
const char* const kPlayersHelp1       = "Your stance is how your characters treat theirs.";
const char* const kPlayersHelp2       = "Each player chooses their own stance.";
const char* const kPlayersClose       = "CLOSE";
const char* const kPlayersNone        = "No other players are connected.";
const char* const kStanceNeutral      = "NEUTRAL";
const char* const kStanceAlly         = "ALLY";
const char* const kStanceEnemy        = "ENEMY";
/* The message line when another player changes theirs ("an" before ALLY / ENEMY is Inferred in the mock-ups - owner to check). */
inline std::string StanceChangedText(const std::string& player, const std::string& stance)
{
    const std::string art = (stance == kStanceAlly || stance == kStanceEnemy) ? std::string("an ") : std::string();
    return player + " now treats you as " + art + stance + ".";
}

/* The Players list: this player "(host)", then each connected player by the name their game sent.  words1 (wording
   audit 12): no state word - being listed means connected. */
inline std::string PanelPlayersText(const std::string& me, const std::vector<std::string>& friends)
{
    std::string s = "PLAYERS\n    " + (me.empty() ? std::string("Host") : me + " (host)");
    for (size_t i = 0; i < friends.size(); ++i)
        s += "\n    " + (friends[i].empty() ? std::string("Unnamed player") : friends[i]);
    if (friends.empty()) s += "\n    Waiting for players...";
    return s;
}

/* mp5 (design-mpmenu1 section 5) - [C] GAME OPTIONS: the rows, their steps and their words.  Every value a row can
   produce is one the notebook's own checks accept (gpoptions.h GpValueOk / GtValueOk, recruitmult.h, and the
   basepolicy / timemode word sets) - the offline suite walks every step of every row to prove it.
   STEP LIMITS: the design's proposed ranges, 0.25x-4x (difficulty) and 0.25x-2x (world).  Kenshi builds its own
   sliders in code (no range in its layout files) and their limits were NOT decoded - Guess that they may differ.
   Attacks on your base 0-5 with 5 = off, limb loss 0-3: shown as numbers, the engine's own labels not read.
   gp.ae, gp.dh, gt.civ (no visible effect, design-settings1) and researchmode (hidden until L2c') are not rows. */
enum { kOptTabDifficulty = 0, kOptTabWorld = 1, kOptTabCoop = 2, kOptTabCount = 3 };
enum { kOptKindDifficulty = 0, kOptKindWorld = 1, kOptKindTick = 2, kOptKindPick = 3 };
const int kOptCount = 19;   /* ui5: + PROFILES PER PLAYER */
const int kOptRowsShown = 9;   /* the most rows one tab has (Difficulty) */
struct OptDef { int tab; const char* key; const char* label; int kind; const char* dflt; };
inline const OptDef* OptDefs()
{
    static const OptDef d[kOptCount] = {
        { kOptTabDifficulty, "gp.gdm", "DAMAGE", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.cod", "CHANCE OF DEATH", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.ht", "HUNGER SPEED", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.bs", "BUILDING SPEED", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.rs", "RESEARCH SPEED", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.ps", "CRAFTING SPEED", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.nnm", "ANIMAL NESTS", kOptKindDifficulty, "1" },
        { kOptTabDifficulty, "gp.bl", "BANDITS LOOT DOWNED CHARACTERS", kOptKindTick, "1" },
        { kOptTabDifficulty, "gp.ep", "EASY PROSPECTING", kOptKindTick, "0" },
        { kOptTabWorld, "gt.pop", "POPULATION", kOptKindWorld, "1" },
        { kOptTabWorld, "gt.squad", "SQUAD SIZE", kOptKindWorld, "1" },
        { kOptTabWorld, "gt.raidsize", "RAID SIZE", kOptKindWorld, "1" },
        { kOptTabWorld, "gt.raidfreq", "RAID FREQUENCY", kOptKindWorld, "1" },
        { kOptTabWorld, "gt.attacks", "BASE ATTACKS", kOptKindPick, "" },   /* no fixed default: the host's own Options value */
        { kOptTabWorld, "gt.limbloss", "LIMB LOSS", kOptKindPick, "" },
        { kOptTabCoop, "recruitmult", "EXTRA RECRUITS IN BARS", kOptKindPick, "auto" },
        { kOptTabCoop, "basepolicy", "BASE ACCESS", kOptKindPick, "shared" },
        { kOptTabCoop, "timemode", "GAME SPEED CONTROL", kOptKindPick, "fixed" },
        /* ui5 (approved 2026-09-27): the host option prof1 reads (profiles.h kCapMin..kCapMax, default kCapDefault); the
           world server accepts it from the host (store_main.cpp OptionValueOk "profilecap"). */
        { kOptTabCoop, "profilecap", "PROFILES PER PLAYER", kOptKindPick, "3" } };
    return d;
}
struct OptChoice { const char* key; const char* wire; const char* shown; };
const int kOptChoiceCount = 36;   /* ui5: + profilecap 1..16 */
inline const OptChoice* OptChoices()
{
    static const OptChoice c[kOptChoiceCount] = {
        { "gt.attacks", "0", "0" }, { "gt.attacks", "1", "1" }, { "gt.attacks", "2", "2" }, { "gt.attacks", "3", "3" },
        { "gt.attacks", "4", "4" }, { "gt.attacks", "5", "5 (Off)" },
        { "gt.limbloss", "0", "0" }, { "gt.limbloss", "1", "1" }, { "gt.limbloss", "2", "2" }, { "gt.limbloss", "3", "3" },
        { "recruitmult", "auto", "Auto" }, { "recruitmult", "1", "1" }, { "recruitmult", "2", "2" }, { "recruitmult", "3", "3" },
        { "recruitmult", "4", "4" },
        { "basepolicy", "shared", "Everyone" }, { "basepolicy", "owner", "Owner only" }, { "basepolicy", "locked", "Locked" },
        { "timemode", "fixed", "Host" }, { "timemode", "consensus", "Vote" },
        { "profilecap", "1", "1" }, { "profilecap", "2", "2" }, { "profilecap", "3", "3" }, { "profilecap", "4", "4" },
        { "profilecap", "5", "5" }, { "profilecap", "6", "6" }, { "profilecap", "7", "7" }, { "profilecap", "8", "8" },
        { "profilecap", "9", "9" }, { "profilecap", "10", "10" }, { "profilecap", "11", "11" }, { "profilecap", "12", "12" },
        { "profilecap", "13", "13" }, { "profilecap", "14", "14" }, { "profilecap", "15", "15" }, { "profilecap", "16", "16" } };
    return c;
}
inline const double* OptLadder(int kind, int* n)
{
    static const double diff[10]  = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0 };
    static const double world[8]  = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0 };
    if (kind == kOptKindDifficulty) { *n = 10; return diff; }
    if (kind == kOptKindWorld) { *n = 8; return world; }
    *n = 0;
    return 0;
}
/* The option shown on `row` of `tab`, or -1 for an empty row. */
inline int OptIndexOf(int tab, int row)
{
    int k = 0;
    for (int i = 0; i < kOptCount; ++i)
    {
        if (OptDefs()[i].tab != tab) continue;
        if (k == row) return i;
        ++k;
    }
    return -1;
}
/* The notebook's float text: the same %.9g of a float that settings.cpp writes, so the texts compare equal. */
inline std::string OptFloatText(double d)
{
    char b[48];
    _snprintf(b, sizeof b - 1, "%.9g", (double)(float)d);
    b[sizeof b - 1] = 0;
    return std::string(b);
}
/* The value as the notebook stores it, or "" when row i cannot hold it. */
inline std::string OptCanon(int i, const std::string& v)
{
    if (i < 0 || i >= kOptCount) return std::string();
    const OptDef& d = OptDefs()[i];
    if (d.kind == kOptKindTick) return (v == "0" || v == "1") ? v : std::string();
    if (d.kind == kOptKindPick)
    {
        for (int c = 0; c < kOptChoiceCount; ++c)
            if (std::string(OptChoices()[c].key) == d.key && v == OptChoices()[c].wire) return v;
        return std::string();
    }
    double x = 0.0;
    if (!coopgp::GpKindValueOk(coopgp::kGpFloat, v, &x)) return std::string();
    return OptFloatText(x);
}
/* One step of row i: dir -1 down, +1 up (a tick box flips).  At an end the same value comes back.  A value between
   two steps (a world recorded elsewhere) moves to the next step in that direction. */
inline std::string OptStep(int i, const std::string& cur, int dir)
{
    if (i < 0 || i >= kOptCount) return cur;
    const OptDef& d = OptDefs()[i];
    if (d.kind == kOptKindTick) return cur == "1" ? std::string("0") : std::string("1");
    if (d.kind == kOptKindPick)
    {
        std::vector<std::string> w;
        for (int c = 0; c < kOptChoiceCount; ++c) if (std::string(OptChoices()[c].key) == d.key) w.push_back(OptChoices()[c].wire);
        int at = -1;
        for (int k = 0; k < (int)w.size(); ++k) if (w[(size_t)k] == cur) at = k;
        if (at < 0) return w.empty() ? cur : w[0];
        const int to = at + (dir > 0 ? 1 : -1);
        return (to < 0 || to >= (int)w.size()) ? cur : w[(size_t)to];
    }
    int n = 0;
    const double* l = OptLadder(d.kind, &n);
    double x = 0.0;
    if (!coopgp::GpParseNumber(cur, &x)) return OptFloatText(1.0);
    if (dir > 0) { for (int k = 0; k < n; ++k) if (l[k] > x + 1e-6) return OptFloatText(l[k]); }
    else         { for (int k = n - 1; k >= 0; --k) if (l[k] < x - 1e-6) return OptFloatText(l[k]); }
    return cur;
}
inline bool OptCanStep(int i, const std::string& cur, int dir) { return OptStep(i, cur, dir) != cur; }
/* What row i shows for its value: "1.0x", "0.25x", a choice's words, or "Unknown".  A tick box shows nothing. */
inline std::string OptShown(int i, const std::string& cur)
{
    if (i < 0 || i >= kOptCount) return std::string();
    const OptDef& d = OptDefs()[i];
    if (d.kind == kOptKindTick) return std::string();
    if (cur.empty()) return "Unknown";
    if (d.kind == kOptKindPick)
    {
        for (int c = 0; c < kOptChoiceCount; ++c)
            if (std::string(OptChoices()[c].key) == d.key && cur == OptChoices()[c].wire) return OptChoices()[c].shown;
        return cur;
    }
    double x = 0.0;
    if (!coopgp::GpParseNumber(cur, &x) || x < 0.0) return cur;
    const long long hundredths = (long long)(x * 100.0 + 0.5);
    const long long frac = hundredths % 100;
    std::string s = PanelNum(hundredths / 100) + ".";
    if (frac == 0) s += "0";
    else if (frac % 10 == 0) s += PanelNum(frac / 10);
    else s += (frac < 10 ? std::string("0") : std::string()) + PanelNum(frac);
    return s + "x";
}
/* The notebook's options.txt (`v1<TAB>key<TAB>value` per line, store_main.cpp OptionsParseInto) as a map. */
inline void OptParseFile(const std::string& text, std::map<std::string, std::string>* out)
{
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos || line.substr(0, t1) != "v1") continue;
        const size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos || t2 == t1 + 1) continue;
        std::string val = line.substr(t2 + 1);
        const size_t t3 = val.find('\t');
        if (t3 != std::string::npos) val = val.substr(0, t3);
        (*out)[line.substr(t1 + 1, t2 - t1 - 1)] = val;
    }
}
/* The rows whose value differs from what the screen opened with, as (key, notebook text).  Only these are handed on:
   a row the host did not change stays whatever the world (or, for a new world, the host's save) already has. */
inline std::vector<std::pair<std::string, std::string> > OptChanged(const std::string* now, const std::string* was)
{
    std::vector<std::pair<std::string, std::string> > out;
    for (int i = 0; i < kOptCount; ++i)
    {
        const std::string c = OptCanon(i, now[i]);
        if (!c.empty() && c != OptCanon(i, was[i])) out.push_back(std::make_pair(std::string(OptDefs()[i].key), c));
    }
    return out;
}

}   /* namespace coopui */
