/* T-1168 (owner 1425 a, 2026-11-03) - THE TEST-ONLY `netthrottle off|on` SWITCH: ENet's own packet throttle on every live link of
 * one process held at its full value (off), or ENet's default terms back (on). Run T1900's one slow live wait (338 ms) had the mod's
 * own queue empty and ENet's throttle at 0 on that link; a run switches the throttle off for half its rounds to see whether the slow
 * waits go with it. Pure: the words, the terms, the request file's name and text, the log line - the plugin, the world server and
 * the offline suite compile this same header.
 *   - The game: its command file verb `netthrottle off|on` (command_channel.cpp) applies it to this game's own ENet links.
 *   - The world server: netthrottle.request in the world folder (one word, off or on), written by the hosting game's same verb or by
 *     a run's harness; the folder watch takes it and renames it netthrottle.request.taken (store_main.cpp NetThrottleNoticeCheck).
 * WHY THESE TERMS (third_party/enet/peer.c enet_peer_throttle, lines 61-90): at each acknowledged round trip ENet raises the
 * throttle by the acceleration (up to its limit) when the round trip did not grow, and lowers it by the deceleration (to 0 at the
 * least) when it grew past the last one plus twice its variance. The throttle also sizes the RELIABLE send window
 * (protocol.c line 1470: window x throttle / 32, never under one MTU) - at 0 one packet per round trip leaves. Off = acceleration 32
 * (the throttle's full scale: any round trip that did not grow puts it straight back at its limit) and deceleration 0 (a grown
 * round trip takes nothing off), with the throttle itself set to its limit at the switch - the limit is 32 here, as neither end
 * sets a bandwidth (enet_host_create(..., 0, 0); host.c lines 421-439 then give every peer the full scale). On = ENet's defaults
 * (peer.c lines 410-412 set them at each peer reset).
 * T-1168 design (c), 2026-11-03 - THE SHIPPED TERMS ARE THE OFF TERMS (run T1922: with ENet's default terms a catch-up made live
 * updates wait ~2 s; held at full there were none). Every live ENet link, on the game and on the world server, gets ShippedTerms()
 * and its throttle at its limit once its connection completes (the CONNECT event - never before: ENet's connect check,
 * protocol.c 960-962, drops a link whose terms changed while connecting). The TEST-ONLY switch stays as a run's control:
 * `netthrottle on` puts ENet's defaults back, `netthrottle off` returns to the shipped terms.
 * enet_peer_throttle_configure also tells the far end, which takes the same three terms for its side of that link (protocol.c
 * lines 808-818) but not the throttle value itself - so each process is switched by its own road.
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once
#include <string>

namespace netthrottle {

enum Mode { kBad = 0, kOff = 1, kOn = 2 };

/* the world server's request file, in the world folder; taken = renamed aside, never deleted */
const char* const kRequestFile = "netthrottle.request";
const char* const kRequestFileAside = "netthrottle.request.taken";
const unsigned int kRequestMax = 64;   /* a longer file is not one this switch wrote */

/* ENet's own figures (third_party/enet/include/enet/enet.h lines 222-226), copied so this header needs no ENet include; the plugin
   and the world server each check at compile time that they agree with the ENet they build */
const unsigned int kScale = 32;          /* ENET_PEER_PACKET_THROTTLE_SCALE: the throttle's full value (no unreliable packet dropped, the whole reliable window) */
const unsigned int kDefInterval = 5000;  /* ENET_PEER_PACKET_THROTTLE_INTERVAL (ms): how long ENet measures the lowest round trip over */
const unsigned int kDefAccel = 2;        /* ENET_PEER_PACKET_THROTTLE_ACCELERATION */
const unsigned int kDefDecel = 2;        /* ENET_PEER_PACKET_THROTTLE_DECELERATION */

struct Terms { unsigned int interval, accel, decel; };

/* the three enet_peer_throttle_configure terms for a mode (kBad answers the defaults, so a caller can never apply nonsense) */
inline Terms TermsFor(int mode)
{
    Terms t;
    t.interval = kDefInterval;
    t.accel = mode == kOff ? kScale : kDefAccel;
    t.decel = mode == kOff ? 0u : kDefDecel;
    return t;
}
/* T-1168 (c): the mode every link starts in - the held-full terms (interval 5000, acceleration 32, deceleration 0) */
const int kShippedMode = kOff;
inline Terms ShippedTerms() { return TermsFor(kShippedMode); }

/* one word, spaces / tabs / line ends around it ignored, any case: "off" or "on"; anything else kBad */
inline int Parse(const std::string& text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r' || text[a] == '\n')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r' || text[b - 1] == '\n')) --b;
    std::string w;
    for (size_t i = a; i < b; ++i) { const char c = text[i]; w += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
    if (w == "off") return kOff;
    if (w == "on") return kOn;
    return kBad;
}
inline const char* Word(int mode) { return mode == kOff ? "off" : (mode == kOn ? "on" : "bad"); }

/* the request file's text, and its reading (a longer file is refused whole) */
inline std::string RequestText(int mode) { return std::string(Word(mode)) + "\r\n"; }
inline int RequestParse(const std::string& text) { return text.size() > kRequestMax ? (int)kBad : Parse(text); }

inline std::string Num(unsigned long v)   /* digits by hand: the game's locale mangles stream numbers (F030) */
{
    std::string d;
    do { d.insert(d.begin(), (char)('0' + v % 10)); v /= 10; } while (v != 0);
    return d;
}
/* THE ONE LOG LINE per switch (newLink: a link that connected while off and was given the off terms):
   "<tag> netthrottle off (TEST-ONLY): ENet throttle terms interval=5000 acceleration=32 deceleration=0, throttle set to its limit - 2 live links" */
inline std::string Line(const std::string& tag, int mode, int links, bool newLink)
{
    const Terms t = TermsFor(mode);
    std::string s = tag + " netthrottle " + Word(mode) + " (TEST-ONLY): ENet throttle terms interval=" + Num(t.interval) + " acceleration=" + Num(t.accel)
                    + " deceleration=" + Num(t.decel) + (mode == kOff ? ", throttle set to its limit (full scale " + Num(kScale) + ")" : ", throttle value left to ENet (ENet's defaults)");
    if (newLink) return s + " - a link that connected while off";
    return s + " - " + Num(links < 0 ? 0ul : (unsigned long)links) + (links == 1 ? " live link" : " live links");
}
/* T-1168 (c): THE ONE LOG LINE per link at its connection, the shipped terms applied (no switch used):
   "<tag> netthrottle: ENet throttle held at full on this link - terms interval=5000 acceleration=32 deceleration=0, throttle at its limit (full scale 32)" */
inline std::string ShippedLine(const std::string& tag)
{
    const Terms t = ShippedTerms();
    return tag + " netthrottle: ENet throttle held at full on this link - terms interval=" + Num(t.interval) + " acceleration=" + Num(t.accel)
           + " deceleration=" + Num(t.decel) + ", throttle at its limit (full scale " + Num(kScale) + ")";
}
/* T-1168 (c): a game held at ENet's defaults by `netthrottle on` whose link adopted other terms from the far end (the world server
   sets the shipped terms at its own connect, and ENet hands them to this end, protocol.c 808-818): the defaults are put back, once
   per adoption - so the control holds for a link that connects during an `on` round too */
inline bool OnNeedsReapply(int mode, unsigned int accel, unsigned int decel) { return mode == kOn && (accel != kDefAccel || decel != kDefDecel); }
inline std::string ReapplyLine(const std::string& tag, unsigned int accel, unsigned int decel)
{
    return tag + " netthrottle on (TEST-ONLY): this link had adopted the far end's terms acceleration=" + Num(accel) + " deceleration=" + Num(decel)
           + " - ENet's defaults put back";
}

/* THE WORLD SERVER FOLLOWS A GAME'S SWITCH (a run's world server works on a copy of the world folder, so the request file may never
   reach it): ENet hands a game's enet_peer_throttle_configure to the server's end of that link (protocol.c 808-818), which adopts the
   three terms but not the throttle value. Terms of exactly acceleration 32 and deceleration 0 are the shipped / off terms (T-1168 (c):
   every link gets them at its connect; every peer reset sets 2 / 2, peer.c 410-412) - so on those terms the server puts the link's
   throttle at its limit. KEPT with the shipped terms: a link set to them at connect starts at its limit and, with deceleration 0,
   never falls, so this never fires there; it is still needed after a game's `netthrottle on` then `off` - the throttle may have
   fallen under ENet's defaults, and ENet itself raises it again only at an acknowledgement whose round trip did not grow (peer.c
   69-76), which a catch-up can postpone - so `off` would not equal the shipped state for that round without it. */
inline bool IsOffTerms(unsigned int accel, unsigned int decel) { return accel == kScale && decel == 0u; }
inline bool FollowRaise(unsigned int accel, unsigned int decel, unsigned int throttle, unsigned int limit) { return IsOffTerms(accel, decel) && throttle < limit; }
inline std::string FollowLine(int slot, unsigned int from, unsigned int to)
{
    return "[STORE] netthrottle: link slot" + (slot < 0 ? std::string("-") + Num((unsigned long)(-(long)slot)) : Num((unsigned long)slot)) + " follows the game's off terms - throttle "
           + Num(from) + " -> " + Num(to) + " (TEST-ONLY)";
}

}   /* namespace netthrottle */
