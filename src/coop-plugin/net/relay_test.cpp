/* net/relay_test.cpp - THE STEAM LINK'S ECHO TEST, driven by the TEST key relaytest= in shared_wastelands.cfg. Absent = nothing here
   runs: RelayTestTick returns at its first line. Ticked once per frame from SteamProbeTick (main thread; title and main loop).

   MODES
     listen:<port>          opens a Steam IP listen socket on 127.0.0.1:<port> and echoes every frame back on the lane it came on;
                            when the link that came up ends, it logs what it echoed and stops.
     dial:127.0.0.1:<port>  dials that listener over Steam sockets (ordinary UDP on loopback, no Steam relay), then runs the sweep.
                            A failed attempt is re-dialled at most once a second, at most kMaxAttempts times (the exit: UP, or the
                            attempts are spent). Odd attempts dial WITHOUT Steam's IP_AllowWithoutAuth, even ones WITH it, so one
                            run shows whether the same Steam user's second game is let in either way (each failure's end reason is
                            logged).
     pair                   one game alone: two Steam links joined by CreateSocketPair over network loopback; one end echoes, the
                            other runs the sweep.
     many:<port>:<cap>:<dials>
                            one game alone: a Steam listener (SteamListener, a host's many-link side) on 127.0.0.1:<port> with its cap
                            forced to <cap>, and <dials> Steam links from this same game dialling it. Each link that comes up sends one
                            frame of two pieces and checks its echo; the dials past the cap must end with end reason 1005 (full). Then
                            one echoed link is closed from its end and, once the listener has freed its place, one refused dial dials
                            again and must be accepted and echo. Each stage is bounded by 60 s; the exit is every dial settled. First a
                            Steam P2P listener (virtual port 0, relay only) is opened, its relay-only option read back from Steam (it
                            must read 0; PASS needs it), and closed.
   relaylag=<ms> / relayloss=<percent> set Steam's GLOBAL fake lag / loss on this game's sends before the first socket is made.

   THE SWEEP (dialing end). First a frame one byte over 32 MiB must be refused before anything is sent. Then, one at a time, a reliable
   frame of each size in kSizes is sent and its echo compared byte for byte (the payload sizes are chosen so the FRAMES are 255 KB, 256
   KB and 256 KB + 1 around the piece size, the two real record sizes 2,635,601 and 6,013,373, and exactly 32 MiB); meanwhile 1,000
   small unreliable frames go out (two per frame) and their echoes are checked by content (the lane may lose, reorder or repeat them).
   Last, a small reliable end marker; its echo ends the sweep. A link that drops ends the test as failed - Steam's own connected
   timeout is the bound on every wait; there is no timer here.

   LOG. "[NET] relay test: ..." lines: the start, each dial attempt's end, "<mode> link UP in N ms (relayed=R, auth=...)", one line per
   size "echo <payload> bytes (frame F) ok in N ms (pieces P)", and one "done" line. RelayTestWord() goes on the REPORT line. */
#include <windows.h>

#include "steam_transport.h"
#include "steam_probe.h"
#include "../config.h"
#include "../coop_log.h"
#include "../../common/relaysplit.h"
#include "../../common/steamlisten.h"

#include <cstring>
#include <sstream>
#include <locale>
#include <vector>

namespace coop {
namespace net {

namespace {

std::string N(long long v)   // RE_Kenshi's locale mangles raw numbers (F030)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

enum { RT_UNDECIDED = 0, RT_OFF, RT_WAIT, RT_LISTENING, RT_DIALING, RT_RUNNING, RT_DONE, RT_FAILED, RT_MANY };

const MsgType kTypeReliable = (MsgType)250;     /* test frames: this link carries nothing else */
const MsgType kTypeUnreliable = (MsgType)251;
const unsigned int kSizes[] = { 0, 1, 261115, 262139, 262140, 2635601, 6013373, 33554427 };
const int kSizeCount = (int)(sizeof(kSizes) / sizeof(kSizes[0]));
const unsigned int kOverCapPayload = 33554428;  /* frame = 32 MiB + 1 */
const unsigned int kUnreliableCount = 1000;
const unsigned int kUnreliableBytes = 64;
const unsigned int kUnreliableTag = 100000;     /* tag of unreliable frame k = kUnreliableTag + k */
const unsigned int kMarkerTag = 999;
const int kMaxAttempts = 60;
const DWORD kRedialGapMs = 1000;

int         g_rt = RT_UNDECIDED;
const char* g_word = "off";
int         g_mode = 0;
unsigned short g_port = 0;
ITransport* g_main = 0;   /* listen: the listener; dial: the dialer; pair: the dialing end */
ITransport* g_echo = 0;   /* pair: the echoing end */
DWORD       g_t0 = 0;     /* GetTickCount when the test began */
unsigned int g_gen = 0;   /* ConnectGen at UP */
bool        g_upSeen = false;
/* dialing */
int         g_attempts = 0;
bool        g_dialLive = false;
DWORD       g_dialEndMs = 0;
/* the sweep */
int         g_next = 0;           /* index into kSizes; kSizeCount = the end marker */
bool        g_waiting = false;
DWORD       g_sentAt = 0;
std::vector<char> g_expect;
int         g_ok = 0, g_wrong = 0;
int         g_overCapRefused = -1;
unsigned int g_uSent = 0, g_uEcho = 0, g_uBad = 0;
/* echoing */
long long   g_echoFrames = 0, g_echoBytes = 0, g_echoRefused = 0;
/* many: one listener with a forced cap, and dials to it from this same game */
const int kManyDialsMax = coopcfg::kRelayManyDialsMax;
const unsigned int kManyFrameBytes = 300000;   /* a frame of two pieces: each link's own rejoin runs */
const DWORD kManyStageBoundMs = 60000;          /* a stage that has not settled in 60 s ends the test */
enum { MD_IDLE = 0, MD_DIALING, MD_UP, MD_ECHOED, MD_REFUSED, MD_FAILED, MD_CLOSED };
struct ManyDial { ITransport* t; int phase; DWORD sentAt; };
ManyDial    g_dial[kManyDialsMax];
std::vector<char> g_dialExpect[kManyDialsMax];
SteamListener* g_listener = 0;
int         g_manyCap = 0, g_manyDials = 0, g_manyStage = 0;
DWORD       g_stageMs = 0;
int         g_manyHeldMax = 0, g_manyOverCap = 0;
long long   g_manyIceRead0 = 0;   /* the P2P listener's relay-only option read back from Steam as 0: 1 = yes */
long long   g_lsAccepted = 0, g_lsRefused = 0, g_lsRefusedOther = 0, g_lsEnded = 0, g_lsEchoed = 0, g_endedBeforeClose = 0;
int         g_round1Echoed = 0, g_round1Refused = 0, g_round1Failed = 0;
int         g_closedDial = -1, g_redial = -1;

const char* ModeName(int m)
{
    return m == coopcfg::kRelayTestListen ? "listen" : m == coopcfg::kRelayTestDial ? "dial" : m == coopcfg::kRelayTestPair ? "pair"
         : m == coopcfg::kRelayTestMany ? "many" : "none";
}

void Fill(std::vector<char>* v, size_t n, unsigned int tag)
{
    v->resize(n);
    for (size_t i = 0; i < n; ++i) (*v)[i] = (char)((i * 131u + tag * 7u + (i >> 11)) & 255u);
    if (n >= 4) std::memcpy(&(*v)[0], &tag, 4);
}

void Drop()
{
    delete g_main; g_main = 0;
    delete g_echo; g_echo = 0;
    for (int i = 0; i < kManyDialsMax; ++i) { delete g_dial[i].t; g_dial[i].t = 0; }
    delete g_listener; g_listener = 0;
}

void FailTest(const std::string& why)
{
    DebugLog("[NET] relay test: FAILED - " + why);
    g_word = "failed";
    g_rt = RT_FAILED;
    Drop();
}

std::string EndText(ITransport* t)
{
    SteamLinkFacts f;
    if (!SteamTransportFacts(t, &f)) return "(no facts)";
    std::string s = "end reason " + N(f.endReason) + " '" + f.endText + "'";
    if (f.stopped) s += ", stopped: " + f.stopWhy;
    return s;
}

void UpLine(ITransport* t, const char* auth)
{
    SteamLinkFacts f;
    SteamTransportFacts(t, &f);
    DebugLog("[NET] relay test: " + std::string(ModeName(g_mode)) + " link UP in " + N(f.upMs) + " ms (relayed=" + N(f.relayed) + ", auth="
             + auth + (g_mode == coopcfg::kRelayTestDial ? ", attempt " + N(g_attempts) : std::string()) + ")");
}

void EchoAll(ITransport* t, const std::vector<Message>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
    {
        const Message& m = in[i];
        const Channel ch = (m.type == kTypeUnreliable) ? CH_UNRELIABLE : CH_RELIABLE;
        if (t->Send(m.peer, m.type, m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), ch))
        {
            ++g_echoFrames;
            g_echoBytes += (long long)m.payload.size();
        }
        else
            ++g_echoRefused;
    }
}

void Dial()
{
    ++g_attempts;
    const bool allow = (g_attempts % 2) == 0;
    SteamTransportSetIpTest(g_main, allow);
    std::string err;
    g_dialLive = g_main->Join("127.0.0.1", g_port, &err);
    if (!g_dialLive)
    {
        SteamLinkFacts f;
        if (SteamTransportFacts(g_main, &f) && f.stopped) { FailTest("dial attempt " + N(g_attempts) + ": " + err); return; }
        DebugLog("[NET] relay test: dial attempt " + N(g_attempts) + " (allow-without-auth " + (allow ? "on" : "off") + ") refused at once: " + err);
        g_dialEndMs = ::GetTickCount();
    }
}

/* ---- many ---- */

void ManyDialOne(int i)
{
    ManyDial& d = g_dial[i];
    if (d.t == 0)
    {
        d.t = NewSteamTransport();
        SteamTransportSetIpTest(d.t, true);
    }
    std::string err;
    if (!d.t->Join("127.0.0.1", g_port, &err))
    {
        d.phase = MD_FAILED;
        DebugLog("[NET] relay test: many - dial " + N(i + 1) + " refused at once: " + err);
        return;
    }
    d.phase = MD_DIALING;
}

void ManyDialTick(int i)
{
    ManyDial& d = g_dial[i];
    if (d.t == 0 || (d.phase != MD_DIALING && d.phase != MD_UP && d.phase != MD_ECHOED)) return;
    std::vector<Message> in;
    d.t->Poll(&in);
    const LinkState s = d.t->State();
    if (d.phase == MD_DIALING)
    {
        if (s == LINK_UP)
        {
            SteamLinkFacts f;
            SteamTransportFacts(d.t, &f);
            Fill(&g_dialExpect[i], kManyFrameBytes, 500u + (unsigned int)i);
            if (!d.t->Send(0, kTypeReliable, &g_dialExpect[i][0], g_dialExpect[i].size(), CH_RELIABLE))
            {
                d.phase = MD_FAILED;
                DebugLog("[NET] relay test: many - dial " + N(i + 1) + " came up but refused to send its frame - WRONG");
                return;
            }
            d.phase = MD_UP;
            d.sentAt = ::GetTickCount();
            DebugLog("[NET] relay test: many - dial " + N(i + 1) + " UP in " + N(f.upMs) + " ms; its frame of " + N(kManyFrameBytes) + " bytes is sent");
            return;
        }
        if (s == LINK_FAILED || s == LINK_DOWN)
        {
            SteamLinkFacts f;
            SteamTransportFacts(d.t, &f);
            d.phase = (f.endReason == steamlisten::kEndListenerFull) ? MD_REFUSED : MD_FAILED;
            DebugLog("[NET] relay test: many - dial " + N(i + 1) + " ended before it came up: " + EndText(d.t)
                     + (d.phase == MD_REFUSED ? " - refused as full (expected past the cap)" : " - FAILED"));
        }
        return;
    }
    for (size_t k = 0; k < in.size(); ++k)
    {
        const Message& m = in[k];
        if (d.phase == MD_UP && m.type == kTypeReliable && m.payload == g_dialExpect[i])
        {
            d.phase = MD_ECHOED;
            DebugLog("[NET] relay test: many - dial " + N(i + 1) + " echo ok in " + N((long long)(DWORD)(::GetTickCount() - d.sentAt))
                     + " ms (pieces " + N((long long)relaysplit::PieceCount((size_t)kManyFrameBytes + 5)) + ")");
            std::vector<char>().swap(g_dialExpect[i]);
            continue;
        }
        d.phase = MD_FAILED;
        DebugLog("[NET] relay test: many - dial " + N(i + 1) + " got a frame it did not wait for (" + N((long long)m.payload.size()) + " bytes) - WRONG");
        return;
    }
    if (s != LINK_UP)
    {
        d.phase = MD_FAILED;
        DebugLog("[NET] relay test: many - dial " + N(i + 1) + " went down: " + EndText(d.t) + " - FAILED");
    }
}

void ManyFinish()
{
    const int expectUp = g_manyDials < g_manyCap ? g_manyDials : g_manyCap;
    const int expectRefused = g_manyDials - expectUp;
    const bool redialWanted = expectRefused > 0 && expectUp > 0;
    const bool redialOk = !redialWanted || (g_redial >= 0 && g_dial[g_redial].phase == MD_ECHOED);
    const bool passed = g_round1Echoed == expectUp && g_round1Refused == expectRefused && g_round1Failed == 0 && g_manyOverCap == 0
                        && g_manyHeldMax == expectUp && g_lsRefused == (long long)expectRefused && g_lsRefusedOther == 0
                        && g_lsAccepted == (long long)(expectUp + (redialWanted ? 1 : 0)) && redialOk
                        && g_manyIceRead0 == 1;
    DebugLog("[NET] relay test: many done - cap " + N(g_manyCap) + ", dials " + N(g_manyDials) + ": round 1 echoed " + N(g_round1Echoed) + " of "
             + N(expectUp) + ", refused as full " + N(g_round1Refused) + " of " + N(expectRefused) + ", failed " + N(g_round1Failed)
             + "; the listener accepted " + N(g_lsAccepted) + ", refused " + N(g_lsRefused) + " as full and " + N(g_lsRefusedOther)
             + " otherwise, saw " + N(g_lsEnded) + " link end(s), held at most " + N(g_manyHeldMax) + " (over its cap " + N(g_manyOverCap)
             + " time(s)); the re-dial into the freed place " + (redialWanted ? (redialOk ? "was accepted and echoed" : "was NOT accepted and echoed") : "was not needed")
             + "; the relay-only option read back as 0 on the P2P listener: " + (g_manyIceRead0 == 1 ? "yes" : "NO")
             + ", " + N((long long)(DWORD)(::GetTickCount() - g_t0)) + " ms in all - " + (passed ? "PASSED" : "FAILED")
             + "; closing every link and the listener");
    g_word = passed ? "passed" : "failed";
    g_rt = RT_DONE;
    for (int i = 0; i < kManyDialsMax; ++i)
        if (g_dial[i].t != 0) g_dial[i].t->Disconnect();
    Drop();
}

void ManyStage()
{
    const bool overBound = (DWORD)(::GetTickCount() - g_stageMs) >= kManyStageBoundMs;
    if (g_manyStage == 1)
    {
        int echoed = 0, refused = 0, failed = 0;
        for (int i = 0; i < g_manyDials; ++i)
        {
            const int p = g_dial[i].phase;
            if (p == MD_ECHOED) ++echoed;
            else if (p == MD_REFUSED) ++refused;
            else if (p == MD_FAILED) ++failed;
        }
        const int settled = echoed + refused + failed;
        if (settled < g_manyDials && !overBound) return;
        g_round1Echoed = echoed;
        g_round1Refused = refused;
        g_round1Failed = failed + (g_manyDials - settled);
        DebugLog("[NET] relay test: many - round 1 " + std::string(settled < g_manyDials ? "did NOT settle in 60 s" : "settled") + ": "
                 + N(echoed) + " link(s) up and echoed, " + N(refused) + " refused as full, " + N(g_round1Failed) + " failed; the listener held at most "
                 + N(g_manyHeldMax) + " of its cap " + N(g_manyCap));
        g_closedDial = -1;
        g_redial = -1;
        for (int i = 0; i < g_manyDials; ++i)
        {
            if (g_dial[i].phase == MD_ECHOED && g_closedDial < 0) g_closedDial = i;
            if (g_dial[i].phase == MD_REFUSED && g_redial < 0) g_redial = i;
        }
        if (g_closedDial < 0 || g_redial < 0 || settled < g_manyDials) { ManyFinish(); return; }
        g_endedBeforeClose = g_lsEnded;
        g_dial[g_closedDial].t->Disconnect();
        g_dial[g_closedDial].phase = MD_CLOSED;
        DebugLog("[NET] relay test: many - dial " + N(g_closedDial + 1) + " closed from its end; once the listener frees its place, dial "
                 + N(g_redial + 1) + " (refused before) dials again");
        g_manyStage = 2;
        g_stageMs = ::GetTickCount();
        return;
    }
    if (g_manyStage == 2)
    {
        if (g_lsEnded > g_endedBeforeClose)
        {
            ManyDialOne(g_redial);
            g_manyStage = 3;
            g_stageMs = ::GetTickCount();
            return;
        }
        if (overBound)
        {
            DebugLog("[NET] relay test: many - the listener did not see the closed link end in 60 s - WRONG");
            ManyFinish();
        }
        return;
    }
    const int p = g_dial[g_redial].phase;
    if (p == MD_ECHOED || p == MD_REFUSED || p == MD_FAILED || overBound) ManyFinish();
}

void TickMany()
{
    std::vector<SteamListenEvent> ev;
    std::vector<Message> in;
    g_listener->Poll(&ev, &in);
    std::string why;
    if (g_listener->Stopped(&why)) { FailTest("the listener stopped: " + why); return; }
    for (size_t k = 0; k < ev.size(); ++k)
    {
        if (ev[k].kind == SL_ACCEPTED) ++g_lsAccepted;
        else if (ev[k].kind == SL_REFUSED) { if (ev[k].endReason == steamlisten::kEndListenerFull) ++g_lsRefused; else ++g_lsRefusedOther; }
        else if (ev[k].kind == SL_ENDED) ++g_lsEnded;
    }
    const int held = g_listener->LinkCount();
    if (held > g_manyHeldMax) g_manyHeldMax = held;
    if (held > g_listener->Cap()) ++g_manyOverCap;
    for (size_t k = 0; k < in.size(); ++k)   /* the listener's side echoes every frame back on its link and lane */
    {
        const Message& m = in[k];
        const Channel ch = (m.type == kTypeUnreliable) ? CH_UNRELIABLE : CH_RELIABLE;
        if (g_listener->Send(m.peer, m.type, m.payload.empty() ? 0 : &m.payload[0], m.payload.size(), ch)) ++g_lsEchoed;
    }
    for (int i = 0; i < g_manyDials; ++i) ManyDialTick(i);
    if (g_rt != RT_MANY) return;
    ManyStage();
}

void BeginMany()
{
    std::string err;
    /* Steam takes the relay-only option on a P2P listen socket and it reads back as 0: opened and closed at once (nobody can reach it
       in this test). */
    const long long read0Before = SteamIceOffRead0();
    SteamListener* p2p = NewSteamListener();
    const bool p2pOk = p2p->Open(0, &err);
    delete p2p;
    if (!p2pOk) { FailTest("a Steam P2P listener with the relay-only option: " + err); return; }
    g_manyIceRead0 = SteamIceOffRead0() - read0Before;
    if (g_manyIceRead0 != 1) { FailTest("a Steam P2P listener opened but its relay-only option was not read back as 0"); return; }
    DebugLog("[NET] relay test: many - a Steam P2P listener (virtual port 0) opened with the relay-only option, the option read back from"
             " Steam as 0, and closed - Steam took it");
    g_manyCap = ConfigRelayTestCap();
    g_manyDials = ConfigRelayTestDials();
    if (g_manyDials > kManyDialsMax) g_manyDials = kManyDialsMax;
    g_listener = NewSteamListener();
    g_listener->SetCap(g_manyCap);
    g_manyCap = g_listener->Cap();
    if (!g_listener->Open(g_port, &err)) { FailTest("listener on 127.0.0.1:" + N(g_port) + ": " + err); return; }
    DebugLog("[NET] relay test: many - a Steam listener on 127.0.0.1:" + N(g_port) + " with its cap forced to " + N(g_manyCap) + " (a host's is "
             + N(steamlisten::kLinkCap) + "), and " + N(g_manyDials) + " dials to it from this same game; each link that comes up echoes a frame of "
             + N(kManyFrameBytes) + " bytes");
    for (int i = 0; i < g_manyDials; ++i) ManyDialOne(i);
    g_manyStage = 1;
    g_stageMs = ::GetTickCount();
    g_word = "running";
    g_rt = RT_MANY;
}

void Begin()
{
    g_t0 = ::GetTickCount();
    const int lag = ConfigRelayLagMs(), loss = ConfigRelayLossPct();
    if (lag != 0 || loss != 0)
    {
        std::string err;
        if (!SteamSetFakeLagLoss(lag, loss, &err)) { FailTest("fake lag / loss: " + err); return; }
        DebugLog("[NET] relay test: fake lag " + N(lag) + " ms and loss " + N(loss) + " % on every Steam packet this game sends (global)");
    }
    std::string err;
    if (g_mode == coopcfg::kRelayTestListen)
    {
        g_main = NewSteamTransport();
        SteamTransportSetIpTest(g_main, true);
        if (!g_main->Host(g_port, &err)) { FailTest("listen on 127.0.0.1:" + N(g_port) + ": " + err); return; }
        DebugLog("[NET] relay test: listening on 127.0.0.1:" + N(g_port) + " (Steam sockets over ordinary UDP, allow-without-auth on) - every frame is echoed");
        g_word = "listening";
        g_rt = RT_LISTENING;
        return;
    }
    if (g_mode == coopcfg::kRelayTestDial)
    {
        g_main = NewSteamTransport();
        g_word = "dialing";
        g_rt = RT_DIALING;
        Dial();
        return;
    }
    if (g_mode == coopcfg::kRelayTestMany)
    {
        BeginMany();
        return;
    }
    g_main = NewSteamTransport();
    g_echo = NewSteamTransport();
    if (!SteamTransportMakePair(g_main, g_echo, &err)) { FailTest("socket pair: " + err); return; }
    DebugLog("[NET] relay test: socket pair made in this game - one end echoes, the other runs the sweep");
    g_word = "dialing";
    g_rt = RT_DIALING;
}

void Finish()
{
    const bool passed = g_ok == kSizeCount && g_wrong == 0 && g_overCapRefused == 1 && g_uBad == 0;
    DebugLog("[NET] relay test: done - sizes ok " + N(g_ok) + " of " + N(kSizeCount) + " (wrong " + N(g_wrong) + "), the frame over 32 MiB "
             + (g_overCapRefused == 1 ? "refused before sending" : "NOT refused") + ", unreliable echoed " + N(g_uEcho) + " of " + N(g_uSent)
             + " sent (bad " + N(g_uBad) + "), " + N((long long)(DWORD)(::GetTickCount() - g_t0)) + " ms in all - " + (passed ? "PASSED" : "FAILED")
             + "; closing the link");
    g_word = passed ? "passed" : "failed";
    g_rt = RT_DONE;
    if (g_main != 0) g_main->Disconnect();
    if (g_echo != 0) g_echo->Disconnect();
    Drop();
}

void CheckEchoes(const std::vector<Message>& in)
{
    for (size_t i = 0; i < in.size(); ++i)
    {
        const Message& m = in[i];
        if (m.type == kTypeUnreliable)
        {
            unsigned int tag = 0;
            if (m.payload.size() == kUnreliableBytes) std::memcpy(&tag, &m.payload[0], 4);
            std::vector<char> want;
            if (tag >= kUnreliableTag && tag < kUnreliableTag + kUnreliableCount) Fill(&want, kUnreliableBytes, tag);
            if (!want.empty() && want == m.payload) ++g_uEcho; else ++g_uBad;
            continue;
        }
        const DWORD ms = (DWORD)(::GetTickCount() - g_sentAt);
        if (m.type != kTypeReliable || !g_waiting)
        {
            ++g_wrong;
            DebugLog("[NET] relay test: an echo nobody waited for (type " + N((long long)m.type) + ", " + N((long long)m.payload.size()) + " bytes) - WRONG");
            continue;
        }
        g_waiting = false;
        if (g_next >= kSizeCount)
        {
            if (m.payload == g_expect) { Finish(); return; }
            ++g_wrong;
            DebugLog("[NET] relay test: the end marker came back changed - WRONG");
            Finish();
            return;
        }
        const size_t frame = (size_t)kSizes[g_next] + 5;
        if (m.payload == g_expect)
        {
            ++g_ok;
            DebugLog("[NET] relay test: echo " + N((long long)kSizes[g_next]) + " bytes (frame " + N((long long)frame) + ") ok in " + N((long long)ms)
                     + " ms (pieces " + N((long long)relaysplit::PieceCount(frame)) + ")");
        }
        else
        {
            size_t at = 0;
            while (at < m.payload.size() && at < g_expect.size() && m.payload[at] == g_expect[at]) ++at;
            ++g_wrong;
            DebugLog("[NET] relay test: echo " + N((long long)kSizes[g_next]) + " bytes WRONG - " + N((long long)m.payload.size())
                     + " bytes came back, the first difference at byte " + N((long long)at) + " (" + N((long long)ms) + " ms)");
        }
        std::vector<char>().swap(g_expect);
        ++g_next;
    }
}

void SendNext()
{
    if (g_overCapRefused < 0)
    {
        std::vector<char> big(kOverCapPayload);
        g_overCapRefused = g_main->Send(0, kTypeReliable, &big[0], big.size(), CH_RELIABLE) ? 0 : 1;
        DebugLog(std::string("[NET] relay test: a frame one byte over 32 MiB was ")
                 + (g_overCapRefused == 1 ? "refused before sending - ok" : "ACCEPTED - WRONG"));
    }
    if (!g_waiting && g_next < kSizeCount)
    {
        Fill(&g_expect, kSizes[g_next], (unsigned int)g_next + 1);
        if (!g_main->Send(0, kTypeReliable, g_expect.empty() ? 0 : &g_expect[0], g_expect.size(), CH_RELIABLE))
        {
            ++g_wrong;
            DebugLog("[NET] relay test: echo " + N((long long)kSizes[g_next]) + " bytes - the link refused to send it - WRONG");
            std::vector<char>().swap(g_expect);
            ++g_next;
            return;
        }
        g_waiting = true;
        g_sentAt = ::GetTickCount();
    }
    for (int k = 0; k < 2 && g_uSent < kUnreliableCount; ++k)
    {
        std::vector<char> u;
        Fill(&u, kUnreliableBytes, kUnreliableTag + g_uSent);
        if (!g_main->Send(0, kTypeUnreliable, &u[0], u.size(), CH_UNRELIABLE)) break;   /* Steam's buffer is full: tried again next frame */
        ++g_uSent;
    }
    if (!g_waiting && g_next >= kSizeCount && g_uSent >= kUnreliableCount)
    {
        Fill(&g_expect, 16, kMarkerTag);
        if (!g_main->Send(0, kTypeReliable, &g_expect[0], g_expect.size(), CH_RELIABLE)) { ++g_wrong; Finish(); return; }
        g_waiting = true;
        g_sentAt = ::GetTickCount();
    }
}

void TickListening()
{
    std::vector<Message> in;
    g_main->Poll(&in);
    EchoAll(g_main, in);
    SteamLinkFacts f;
    SteamTransportFacts(g_main, &f);
    if (f.stopped) { FailTest("the listener stopped: " + f.stopWhy); return; }
    if (!g_upSeen && g_main->State() == LINK_UP)
    {
        g_upSeen = true;
        g_gen = g_main->ConnectGen();
        UpLine(g_main, "allow-without-auth");
        return;
    }
    if (g_upSeen && g_main->ConnectGen() != g_gen)
    {
        DebugLog("[NET] relay test: listener - the link ended (" + EndText(g_main) + "); echoed " + N(g_echoFrames) + " frame(s), "
                 + N(g_echoBytes) + " payload bytes, refused " + N(g_echoRefused));
        g_word = "listened";
        g_rt = RT_DONE;
        g_main->Disconnect();
        Drop();
    }
}

void TickDialing()
{
    if (g_mode == coopcfg::kRelayTestPair)
    {
        std::vector<Message> in;
        g_echo->Poll(&in);
        g_main->Poll(&in);
        if (g_main->State() == LINK_UP && g_echo->State() == LINK_UP)
        {
            g_gen = g_main->ConnectGen();
            UpLine(g_main, "pair");
            g_word = "running";
            g_rt = RT_RUNNING;
            return;
        }
        if (g_main->State() == LINK_FAILED || g_echo->State() == LINK_FAILED || g_main->State() == LINK_DOWN || g_echo->State() == LINK_DOWN)
            FailTest("the socket pair did not come up: " + EndText(g_main) + " / " + EndText(g_echo));
        return;
    }
    if (!g_dialLive)
    {
        if (g_attempts >= kMaxAttempts) { FailTest("no link after " + N(g_attempts) + " dial attempts"); return; }
        if ((DWORD)(::GetTickCount() - g_dialEndMs) >= kRedialGapMs) Dial();
        return;
    }
    std::vector<Message> in;
    g_main->Poll(&in);
    const LinkState s = g_main->State();
    if (s == LINK_UP)
    {
        g_gen = g_main->ConnectGen();
        UpLine(g_main, (g_attempts % 2) == 0 ? "allow-without-auth" : "normal");
        g_word = "running";
        g_rt = RT_RUNNING;
        return;
    }
    if (s == LINK_FAILED || s == LINK_DOWN)
    {
        SteamLinkFacts f;
        SteamTransportFacts(g_main, &f);
        if (f.stopped) { FailTest("the dialer stopped: " + f.stopWhy); return; }
        DebugLog("[NET] relay test: dial attempt " + N(g_attempts) + " (allow-without-auth " + ((g_attempts % 2) == 0 ? "on" : "off")
                 + ") ended: " + EndText(g_main));
        g_dialLive = false;
        g_dialEndMs = ::GetTickCount();
    }
}

void TickRunning()
{
    std::vector<Message> in;
    if (g_echo != 0)
    {
        g_echo->Poll(&in);
        EchoAll(g_echo, in);
        in.clear();
    }
    g_main->Poll(&in);
    CheckEchoes(in);
    if (g_rt != RT_RUNNING) return;
    if (g_main->State() != LINK_UP || g_main->ConnectGen() != g_gen || (g_echo != 0 && g_echo->State() != LINK_UP))
    {
        FailTest("the link went down during the sweep after " + N(g_next) + " of " + N(kSizeCount) + " sizes (" + EndText(g_main) + ")");
        return;
    }
    SendNext();
}

}   /* anonymous namespace */

void RelayTestTick()
{
    if (g_rt == RT_OFF || g_rt == RT_DONE || g_rt == RT_FAILED)
        return;
    if (g_rt == RT_UNDECIDED)
    {
        g_mode = ConfigRelayTestMode();
        g_port = ConfigRelayTestPort();
        if (g_mode == 0) { g_rt = RT_OFF; return; }
        g_rt = RT_WAIT;
        g_word = "waiting";
        DebugLog("[NET] relay test: TEST ONLY relaytest=" + std::string(ModeName(g_mode))
                 + (g_port != 0 ? " port " + N(g_port) : std::string()) + " - starts once Steam has been looked up");
    }
    if (g_rt == RT_WAIT)
    {
        if (!SteamProbeStarted()) return;
        if (SteamProbeSockets() == 0) { FailTest("Steam networking is not available in this game (see the [NET] steam: line)"); return; }
        Begin();
        return;
    }
    if (g_rt == RT_LISTENING) { TickListening(); return; }
    if (g_rt == RT_DIALING) { TickDialing(); return; }
    if (g_rt == RT_MANY) { TickMany(); return; }
    if (g_rt == RT_RUNNING) TickRunning();
}

const char* RelayTestWord()
{
    return g_word;
}

}   /* namespace net */
}   /* namespace coop */
