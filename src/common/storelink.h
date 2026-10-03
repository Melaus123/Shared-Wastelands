/* src/common/storelink.h - THE STORE LINK'S TWO PURE DECISIONS, AND NOTHING ELSE (B10-b, review-b10 M-3 / M-7).
 *
 * Same charter as clockmath.h and storemeta.h: nothing in here reads a global, calls the operating system, or
 * includes an Ogre, ENet or Windows header. Every function is a pure function of its arguments, and
 * the same header is compiled into the game plugin, into the notebook (SharedWastelandsServer.exe) and into the offline
 * test exe - so the two sides of one handshake cannot hold two ideas of when it is refused.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_STORELINK_H
#define COOP_COMMON_STORELINK_H

#include <string>
#include <cstdio>
#include <cstring>
#include <vector>
#include "modlist.h"   /* T-246 (store protocol 57): the world's mod list rides in a kModsDiffers WELCOME */

namespace coopstore {

/* ---- THE PUSH THIS GAME OWES THE NOTEBOOK, AND WHETHER THIS TICK MAY RUN IT (review-b10 M-3) ----
   The arm this replaces read
       if (!g_repushPending) return;  g_repushPending = 0;  if (!LinkUp()) return;
   - it DISARMED FIRST and tested the link second, so a push armed while the link was down was thrown away
   silently and never retried: a drop path with no counter, which is the one shape this project keeps paying
   for. The rule now is: while the link is down the push STAYS PENDING (so the next tick with a link runs it)
   and the deferral is COUNTED; the arm is cleared only when the push actually runs. */
enum RepushAction
{
    kRepushIdle          = 0,   /* nothing is armed - do nothing, and the arm stays clear */
    kRepushDeferLinkDown = 1,   /* armed, but the notebook link is down - the arm STAYS SET, counted */
    kRepushRun           = 2    /* armed and the link is up - run the push, and the arm is cleared */
};

/* `pendingAfter` is what the caller must store back into its own armed flag. It is written on EVERY path,
   so a caller cannot forget one; the caller may pass 0 if it does not want it. */
inline int RepushDecide(int pending, int linkUp, int* pendingAfter)
{
    if (!pending)   { if (pendingAfter) *pendingAfter = 0; return kRepushIdle; }
    if (!linkUp)    { if (pendingAfter) *pendingAfter = 1; return kRepushDeferLinkDown; }
    if (pendingAfter) *pendingAfter = 0;
    return kRepushRun;
}

/* ---- THE STORE HANDSHAKE, ON BOTH SIDES OF IT (review-b10 M-7) ----
   The relay used to LOG a protocol mismatch and then WELCOME the game and push it every record anyway, while
   the game's own receive path was not gated on the mismatch at all - so a mismatched pair still exchanged
   records in both directions and the "refusal" was a sentence in a log. One rule, one text, both sides:
   equal protocols accept, anything else refuses, and the refusal says which two numbers disagreed. */
enum StoreHandshakeVerdict
{
    kHandshakeAccept = 0,
    kHandshakeRefuse = 1
};

inline int StoreHandshakeDecide(unsigned int storeProtocol, unsigned int gameProtocol)
{
    return (storeProtocol == gameProtocol) ? kHandshakeAccept : kHandshakeRefuse;
}

/* THE REASON, IN THE WORDS BOTH PROGRAMS PRINT. Numbers only - no file name, no peer name - so the same
   sentence can be asserted offline and read in either log. */
inline std::string StoreHandshakeRefusalText(unsigned int storeProtocol, unsigned int gameProtocol)
{
    char a[24], b[24];
    std::sprintf(a, "%u", storeProtocol);
    std::sprintf(b, "%u", gameProtocol);
    return std::string("store protocol ") + a + ", game speaks " + b;
}

/* ---- WHY A GAME WAS REFUSED, IN ONE SET OF WORDS (B13-b, review-b13 M-1) ----
   B13 added two refusals - a HELLO with no usable player id, and a second game carrying an id that is
   already in the world - and sent BOTH of them down the protocol-mismatch message, whose payload is two
   protocol numbers. The game therefore printed "the plugin and SharedWastelandsServer.exe must be deployed together"
   about a copied settings file: a wrong diagnosis, which sends the player to rebuild the mod when what they
   have to do is delete one line of a file. The REFUSE payload carries the reason as a third word (store
   protocol 42 is already the shipping-together number, so this costs no further bump), and the sentence for
   each reason lives HERE, once, so the notebook's log and the game's log say the same thing.
   A reason this build does not know reads as kRefuseProtocol - the oldest meaning of the message, and the
   one a notebook older than this header can only have meant. */
enum StoreRefuseReason
{
    kRefuseProtocol    = 0,   /* the two ends speak different store protocols */
    kRefuseNoPlayerId  = 1,   /* a v42 HELLO whose player id field was missing or malformed */
    kRefuseDuplicateId = 2,   /* that player id is ALREADY connected on another peer - a copied settings file */
    kRefuseFull        = 3,   /* M1 (protocol 43): the notebook already has its limit of games connected; words 4-5 = connected, limit */
    kRefuseLifetimeFull = 4,  /* M1 (protocol 43): this id is new and the world has admitted its lifetime limit of players; word 4 = the limit */
    kRefuseMods        = 5,  /* settings5 S5 (protocol 46): this game's mod list is not the world's (src/common/modlist.h); words 4-5 are 0 and the world's list follows them. T-246: sent for a differing list only under coopmods::kModsMismatchRefuse (T-247); under the warn default the game is admitted with kModsDiffers */
    kRefuseSamePerson  = 6,  /* prof1 (protocol 48): this PERSON already plays in this world (any of their profiles) on a connection that is live right now; words 4-5 are 0 */
    kRefuseLiveProtocol = 7   /* M11a S1 (protocol 61; manager decision 1(a)): this game's GAME-TO-GAME protocol (the STORE_HELLO's tail, src/common/joinstage.h) differs from the games already in this world; words 4-5 = theirs, this game's. A game that does not know 7 reads it as the protocol refusal, which names the same fix (the same mod version) */
};

/* A count with thousands commas, so the sentence reads "65,520" as the limit is named everywhere else. */
inline std::string StoreRefuseCount(unsigned int v)
{
    char b[24]; std::sprintf(b, "%u", v);
    std::string d(b), out;
    for (size_t i = 0; i < d.size(); ++i) { if (i > 0 && (d.size() - i) % 3 == 0) out += ','; out += d[i]; }
    return out;
}

/* M1: n1 / n2 are the REFUSE payload's fourth and fifth words - (connected, limit) for kRefuseFull, (limit, -)
   for kRefuseLifetimeFull - and are ignored by the other reasons. A 0 limit (a short payload) reads as the
   built-in figure. */
inline std::string StoreRefuseReasonText(unsigned int reason, unsigned int storeProtocol, unsigned int gameProtocol,
                                         unsigned int n1 = 0, unsigned int n2 = 0)
{
    if (reason == (unsigned int)kRefuseFull)
    {
        const unsigned int lim = n2 ? n2 : 256u;
        return "the world is full: " + StoreRefuseCount(n1 ? n1 : lim) + " of " + StoreRefuseCount(lim)
               + " players are connected; try again later.";
    }
    if (reason == (unsigned int)kRefuseLifetimeFull)
        return "this world has admitted its limit of " + StoreRefuseCount(n1 ? n1 : 65520u)
               + " different players, and this player id is not one of them; no new player can join it.";
    if (reason == (unsigned int)kRefuseMods)   /* settings5 S5 */
        return std::string("this game's mods do not match the world's mod list - every player needs exactly the host's mods, in"
                           " the same load order (the refusal carries the world's list, so the game can name what differs).");
    if (reason == (unsigned int)kRefuseNoPlayerId)
        return std::string("this game's HELLO carried no usable player id, and the notebook names every slot"
                           " number, every area claim and the operator by that id. The game writes one into"
                           " shared_wastelands.cfg on first use; a settings file it cannot write is the usual cause.");
    if (reason == (unsigned int)kRefuseLiveProtocol && n2 == 0u)   /* M11a S1 fold (review L6): coopjoin::kLiveProtoRefuseUnreadable - the HELLO's tail was unreadable, so there is no number to name. Log only (both ends): the game's panel shows PanelRefusalText's version sentence for reason 7 */
        return std::string("this game's HELLO carried no readable game-to-game protocol, so it cannot be matched to the one the games already in this world speak")
               + (n1 ? " (" + StoreRefuseCount(n1) + ")" : std::string()) + " - every player needs the same version of the mod.";
    if (reason == (unsigned int)kRefuseLiveProtocol)   /* M11a S1 */
        return "this game's multiplayer mod speaks game-to-game protocol " + StoreRefuseCount(n2) + " and the games already in this world speak "
               + StoreRefuseCount(n1) + " - every player needs the same version of the mod.";
    if (reason == (unsigned int)kRefuseSamePerson)   /* prof1 */
        return std::string("you are already playing in this world from another game that is running right now - one person plays"
                           " one profile at a time. Close the other game first. (If that other game is not yours, two installs"
                           " share one settings file: delete the playerid line from one of the two shared_wastelands.cfg files.)");
    if (reason == (unsigned int)kRefuseDuplicateId)
        return std::string("this player id is already in the world on a connection that is live right now - a copied"
                           " settings file; each install needs its own. Delete the playerid line from one of the two"
                           " shared_wastelands.cfg files and that game will write itself a new one at its next start. (A"
                           " connection this same player left behind is silent, and the notebook drops it for the"
                           " new one instead of refusing - B13-c.)");
    return StoreHandshakeRefusalText(storeProtocol, gameProtocol);
}

/* ---- IS THE NOTEBOOK LINK TO BE RE-DIALLED ON THIS TICK (B12-e / B12-f, T238) ----
   T238 (2026-09-19, Confirmed): the notebook process was killed mid-run and restarted 150 s later. The game
   logged the link going down and NEVER re-linked, because the store link was dialled in exactly two places and
   neither can happen in a running world - the title-screen arming tick (which latches) and the `store server`
   verb. Decision 52's outage queue is "written when the notebook returns", so 21 journalled entries stayed
   pending for the rest of the run: there was no link for them to replay on.

   PRE-PATCH EQUIVALENT: there was no decision at all. The old answer to EVERY input - including a down link
   with a notebook address configured, a session role and a running world - was "do nothing", which is what
   kRedialIdle means here. The offline row for that input is the one that fails on the old behaviour.

   THE EXIT IS THE LINK STATE, NOT A COUNT (design principle 1). The socket state is re-read and re-asked on
   every tick; `msSinceLast`/`intervalMs` only PACE the attempts. There is no attempt cap and no one-shot: a
   notebook away for an hour is dialled every 5 s for that hour, and the cost of that is one connect attempt
   per interval.

   B12-f (review-b12e H-3) - WHY THE INPUT IS A FOUR-WAY STATE AND NOT A BOOLEAN. `State() != LINK_UP` counts
   a socket that is still CONNECTING as down, and the 5 s pace is SHORTER THAN ENET'S OWN CONNECT TIMEOUT
   (ENET_PEER_TIMEOUT_MINIMUM is 5000 ms, and the limit doubles off a 500 ms round-trip estimate), so a slow
   connect was destroyed at the moment it would first have been judged - the identical hazard this project
   already recorded for the session link (05-findings.md:14276). A connecting socket is therefore LEFT ALONE
   for connectBudgetMs and only then given up on. And a socket that is UP but has never been WELCOMED is a
   third case again: the far end accepted the TCP-like handshake and then said nothing, which no amount of
   waiting fixes, so after welcomeBudgetMs it is aborted and re-dialled. A protocol REFUSAL is not an outage
   at all - the notebook is answering, in words, and saying no - so it reads as idle and is never re-dialled.

   `sinceDialMs` is measured from the last dial (the moment a Join returned), so it covers the connect and the
   handshake that follows it with one stamp. The skip arms are ordered most-permanent-first so each counts its
   own cause, and they are asked BEFORE the connecting arms: a link that is mid-connect at the title screen,
   or in a game that has left the session, is skipped and left standing rather than torn down. */
enum StoreSocketState
{
    kSockDownOrFailed  = 0,   /* LINK_DOWN, LINK_FAILED, or no transport object at all */
    kSockConnecting    = 1,   /* LINK_CONNECTING - ENet has not yet had an answer, and may still get one */
    kSockUpNotWelcomed = 2,   /* LINK_UP, HELLO sent, no WELCOME back yet - the handshake is in flight or stuck */
    kSockUpWelcomed    = 3,   /* LINK_UP and WELCOMED - this is the exit condition */
    kSockUpRefused     = 4    /* LINK_UP and the notebook REFUSED this game's protocol - answered, not absent */
};

enum StoreRedialAction
{
    kRedialIdle             = 0,   /* welcomed, or refused in words - nothing to do, and the outage has ended */
    kRedialNow              = 1,   /* down, configured, in a session, in a world, and the interval has elapsed */
    kRedialWait             = 2,   /* as kRedialNow except that this interval is still running */
    kRedialSkipNoServer     = 3,   /* down, but no notebook address+port is configured - there is nothing to dial */
    kRedialSkipSingle       = 4,   /* down, but role=single: this game owes no notebook anything */
    kRedialSkipNoWorld      = 5,   /* down, but there is no running world - the title screen's own dial owns that */
    kRedialWaitConnecting   = 6,   /* CONNECTING, or UP and not yet welcomed, inside its budget - LEAVE IT ALONE */
    kRedialGiveUpConnect    = 7,   /* CONNECTING past connectBudgetMs - abort it, then the interval runs again */
    kRedialStalledHandshake = 8    /* UP past welcomeBudgetMs with no WELCOME and no refusal - abort and re-dial */
};

inline int StoreRedialDecide(int sockState, int hasServer, int roleIsSession, int worldRunning,
                             unsigned int msSinceLast, unsigned int intervalMs,
                             unsigned int sinceDialMs, unsigned int connectBudgetMs,
                             unsigned int welcomeBudgetMs)
{
    if (sockState == kSockUpWelcomed || sockState == kSockUpRefused) return kRedialIdle;
    if (!hasServer)     return kRedialSkipNoServer;
    if (!roleIsSession) return kRedialSkipSingle;
    if (!worldRunning)  return kRedialSkipNoWorld;
    if (sockState == kSockConnecting)
        return (sinceDialMs < connectBudgetMs) ? kRedialWaitConnecting : kRedialGiveUpConnect;
    if (sockState == kSockUpNotWelcomed)
        return (sinceDialMs < welcomeBudgetMs) ? kRedialWaitConnecting : kRedialStalledHandshake;
    return (msSinceLast < intervalMs) ? kRedialWait : kRedialNow;
}

/* THE TITLE SCREEN'S RE-DIAL OF THE WORLD SERVER - a joining game, before any world runs (config.cpp ConfigTitleTick).
   StoreRedialDecide above skips every re-dial while no world runs, so at the title this is the only retry: a JOIN pressed before
   the host's world server listens, or a connect that failed, is dialled again here. Nothing to do once the world server has
   answered at all (UP: welcomed, in its profile lobby, mid-handshake, or refused in words) or once `dials` unanswered dials
   have reached `dialCap`. A dial still CONNECTING is left alone for connectBudgetMs (ENet keeps re-sending its CONNECT, so a
   world server that starts listening inside that time still answers it); after that, or once the link reads down, the dial
   counts as unanswered - at most one per paceMs after the last dial. kTitleRedialAgain: count it and dial again;
   kTitleRedialLast: count it - that was the last allowed dial, and nothing more is dialled. */
enum TitleRedialAction { kTitleRedialNone = 0, kTitleRedialWait = 1, kTitleRedialAgain = 2, kTitleRedialLast = 3 };
inline int TitleRedialDecide(int isClient, int hasDoor, int sockState, long long dials, long long dialCap,
                             unsigned int sinceDialMs, unsigned int paceMs, unsigned int connectBudgetMs)
{
    if (!isClient || !hasDoor || dialCap <= 0 || dials >= dialCap) return kTitleRedialNone;
    if (sockState != kSockDownOrFailed && sockState != kSockConnecting) return kTitleRedialNone;   /* the world server answered */
    if (sinceDialMs < paceMs) return kTitleRedialWait;
    if (sockState == kSockConnecting && sinceDialMs < connectBudgetMs) return kTitleRedialWait;
    return (dials + 1 >= dialCap) ? kTitleRedialLast : kTitleRedialAgain;
}

/* ---- W3 (decisions 58, 59; store protocol 45): THE STORE WELCOME, WRITTEN AND READ IN ONE PLACE ----
   {u32 protocol, u32 authority, u32 recordCount, u32 slot, u32 worldLen + worldLen bytes}. The slot arrived in 31, the
   world name in 45 - the notebook's world, which the game follows (coopworld::WelcomeWorldDecide). Decode tolerates the
   older shapes: 12 bytes (no slot) and 16 bytes (no world, hasWorld false). A world field whose length is above
   kStoreWelcomeWorldMax or runs past the payload is not read (worldMalformed) - the rest of the WELCOME still is.
   False only for a payload too short to be a WELCOME at all. */
const unsigned int kStoreWelcomeWorldMax = 256;
/* settings5 S5 (store protocol 46): after the world, {u32 modsVerdict, u32 world's active mod count, u32 its plugin DLL count}
   - what the notebook concluded about THIS game's mod list. Only written after a world; absent = a 45 WELCOME. */
/* T-246 (store protocol 57, owner 199 b): kModsDiffers = let in although the lists differ - THE WORLD'S LIST (coopmods::ModListEncode)
   follows the three words, before the world guard's tail, so the game can name what differs. */
/* T-490 (store protocol 69): right after the world name, {u32 idLen, idLen bytes, u32 flags} - the world's id (coopworld::WorldIdOk;
   idLen 0 = none) and flags bit 0 = the id was given to a world made before ids (upgraded). Written and read on every WELCOME of
   protocol kStoreWelcomeIdSince or later that carries a world; an id field longer than kStoreWelcomeWorldIdMax or running past the
   payload is not read (worldIdMalformed) and neither is anything after it. */
const unsigned int kStoreWelcomeIdSince     = 69;
const unsigned int kStoreWelcomeWorldIdMax  = 40;
const unsigned int kStoreWelcomeIdUpgraded  = 1u;
enum StoreModsVerdict { kModsNotChecked = 0, kModsMatch = 1, kModsRecorded = 2, kModsDiffers = 3 };
struct StoreWelcome
{
    unsigned int protocol, authority, recordCount, slot;
    bool hasSlot, hasWorld, worldMalformed;
    std::string world;
    bool hasWorldId, worldIdMalformed, worldIdUpgraded; std::string worldId;   /* T-490 (store protocol 69) */
    bool hasMods; unsigned int modsVerdict, modsActive, modsPlugins;   /* settings5 S5 */
    bool hasModsWorld; coopmods::ModList modsWorld;   /* T-246 (store protocol 57): the world's list - written and read only when modsVerdict is kModsDiffers */
    bool hasWorldGen; std::vector<char> worldTail;   /* restore1a (store protocol 52): the world guard's tail after the mods - restoreguard::WorldMsg bytes (src/common/restoreguard.h) */
    StoreWelcome() : protocol(0), authority(0), recordCount(0), slot(0), hasSlot(false), hasWorld(false), worldMalformed(false),
                     hasWorldId(false), worldIdMalformed(false), worldIdUpgraded(false), hasMods(false), modsVerdict(0), modsActive(0), modsPlugins(0), hasModsWorld(false), hasWorldGen(false) {}   /* T-246: hasModsWorld */
};
inline void StoreWelcomePutU32(std::vector<char>* out, unsigned int v)
{
    char b[4]; std::memcpy(b, &v, 4); out->insert(out->end(), b, b + 4);
}
inline void StoreWelcomeEncode(const StoreWelcome& w, std::vector<char>* out)
{
    out->clear();
    StoreWelcomePutU32(out, w.protocol); StoreWelcomePutU32(out, w.authority);
    StoreWelcomePutU32(out, w.recordCount); StoreWelcomePutU32(out, w.slot);
    if (!w.hasWorld) return;
    const size_t n = w.world.size() > kStoreWelcomeWorldMax ? (size_t)kStoreWelcomeWorldMax : w.world.size();
    StoreWelcomePutU32(out, (unsigned int)n);
    out->insert(out->end(), w.world.begin(), w.world.begin() + n);
    if (w.protocol >= kStoreWelcomeIdSince)   /* T-490: the world id field */
    {
        const std::string id = w.hasWorldId ? w.worldId.substr(0, kStoreWelcomeWorldIdMax) : std::string();
        StoreWelcomePutU32(out, (unsigned int)id.size());
        out->insert(out->end(), id.begin(), id.end());
        StoreWelcomePutU32(out, w.worldIdUpgraded ? kStoreWelcomeIdUpgraded : 0u);
    }
    if (!w.hasMods) return;   /* settings5 S5 */
    StoreWelcomePutU32(out, w.modsVerdict); StoreWelcomePutU32(out, w.modsActive); StoreWelcomePutU32(out, w.modsPlugins);
    if (w.modsVerdict == (unsigned int)kModsDiffers) coopmods::ModListEncode(w.modsWorld, out);   /* T-246 (store protocol 57): the world's list, before the tail */
    if (!w.hasWorldGen) return;   /* restore1a (store protocol 52): the world guard's tail - restoreguard::EncodeWorldMsg bytes */
    out->insert(out->end(), w.worldTail.begin(), w.worldTail.end());
}
inline bool StoreWelcomeDecode(const std::vector<char>& p, StoreWelcome* w)
{
    *w = StoreWelcome();
    if (p.size() < 12) return false;
    std::memcpy(&w->protocol, &p[0], 4); std::memcpy(&w->authority, &p[4], 4); std::memcpy(&w->recordCount, &p[8], 4);
    if (p.size() >= 16) { std::memcpy(&w->slot, &p[12], 4); w->hasSlot = true; }
    if (p.size() >= 20)
    {
        unsigned int n = 0; std::memcpy(&n, &p[16], 4);
        if (n > kStoreWelcomeWorldMax || p.size() < 20 + (size_t)n) { w->worldMalformed = true; return true; }
        w->world.assign(p.begin() + 20, p.begin() + 20 + n);
        w->hasWorld = true;
        size_t mo = 20 + (size_t)n;   /* settings5 S5: the mods verdict, when the notebook sent one */
        if (w->protocol >= kStoreWelcomeIdSince)   /* T-490: the world id field */
        {
            unsigned int idn = 0, flags = 0;
            if (p.size() < mo + 4) { w->worldIdMalformed = true; return true; }
            std::memcpy(&idn, &p[mo], 4);
            if (idn > kStoreWelcomeWorldIdMax || p.size() < mo + 8 + (size_t)idn) { w->worldIdMalformed = true; return true; }
            w->worldId.assign(p.begin() + mo + 4, p.begin() + mo + 4 + idn);
            std::memcpy(&flags, &p[mo + 4 + idn], 4);
            w->hasWorldId = idn > 0; w->worldIdUpgraded = (flags & kStoreWelcomeIdUpgraded) != 0;
            mo += 8 + (size_t)idn;
        }
        if (p.size() >= mo + 12)
        { std::memcpy(&w->modsVerdict, &p[mo], 4); std::memcpy(&w->modsActive, &p[mo + 4], 4); std::memcpy(&w->modsPlugins, &p[mo + 8], 4); w->hasMods = true; }
        size_t to = mo + 12;   /* T-246 (store protocol 57): where the tail starts - after the world's list when the verdict is kModsDiffers */
        bool listOk = true;
        if (w->hasMods && w->modsVerdict == (unsigned int)kModsDiffers)
        { size_t k = to; if (coopmods::ModListDecode(p, &k, &w->modsWorld)) { w->hasModsWorld = true; to = k; } else listOk = false; }
        if (w->hasMods && listOk && p.size() > to)   /* restore1a: the world guard's tail (restoreguard::DecodeWorldMsg reads it); absent = a 51 WELCOME. T-246: a torn list reads no tail */
        { w->worldTail.assign(p.begin() + to, p.end()); w->hasWorldGen = true; }
    }
    else if (p.size() > 16) w->worldMalformed = true;
    return true;
}

/* ---- THE NOTEBOOK ADDRESS A JOINER DIALS (join1, 2026-09-24) ----
   The Multiplayer panel's Host button writes `store=127.0.0.1:27016` when the notebook runs on the host's own computer
   (ui.cpp), and the host's session WELCOME passes that address on unchanged (net/session.cpp). On a friend's computer
   127.0.0.1 is the FRIEND's machine, so the joiner would dial itself and wait at the load gate. A welcome that names a
   loopback or unspecified address means "the host's own machine": the joiner uses the address it dialled to reach the host
   instead. A joiner that itself dialled a loopback host (both games on one computer, as every harness run) keeps the named
   address, so nothing changes there. */
inline bool IsLoopbackOrAnyAddr(const std::string& a)
{
    return a == "localhost" || a == "::1" || a == "0.0.0.0" || a.compare(0, 4, "127.") == 0;
}
/* The address to dial for the notebook the host named. *swapped = 1 when the host's dialled address replaced a loopback. */
inline std::string NotebookAddrForJoiner(const std::string& named, const std::string& dialledHost, int* swapped)
{
    if (swapped) *swapped = 0;
    if (IsLoopbackOrAnyAddr(named) && !dialledHost.empty() && !IsLoopbackOrAnyAddr(dialledHost))
    {
        if (swapped) *swapped = 1;
        return dialledHost;
    }
    return named;
}

/* addr1 (2026-09-26): WHICH HOST ADDRESS REPLACES A LOOPBACK NOTEBOOK ADDRESS, and why. `peerIp` is the address the
   session link actually reached (the ENet peer, already resolved); `dialled` is the address the join was dialled at
   (the panel's typed address or a `join` verb's); `cfgHost` is shared_wastelands.cfg's host= (the older join1 source, which a
   `join` verb does not update). The first non-empty, specified one wins. Loopback reached at loopback stays loopback,
   so two games on one computer are unchanged. *reason gets a short plain explanation for the [STORE] log line. */
inline std::string NotebookDialFromWelcome(const std::string& named, const std::string& peerIp, const std::string& dialled,
                                           const std::string& cfgHost, int* swapped, std::string* reason)
{
    std::string host, source;
    if (!peerIp.empty() && peerIp != "0.0.0.0") { host = peerIp; source = "the session link's peer address"; }
    else if (!dialled.empty()) { host = dialled; source = "the address this game dialled"; }
    else if (!cfgHost.empty()) { host = cfgHost; source = "shared_wastelands.cfg host="; }
    int sw = 0;
    const std::string used = NotebookAddrForJoiner(named, host, &sw);
    if (swapped) *swapped = sw;
    if (reason)
    {
        if (!IsLoopbackOrAnyAddr(named)) *reason = "a real address - used as the host gave it";
        else if (sw) *reason = "loopback on the host's computer - the host was reached at " + host + " (" + source + ")";
        else if (host.empty()) *reason = "loopback, and no host address is known to use instead - kept";
        else *reason = "loopback, and the host was reached at loopback " + host + " - both games on this computer";
    }
    return used;
}

}   /* namespace coopstore */

#endif
