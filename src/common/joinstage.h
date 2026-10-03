/* joinstage.h - M11a S1 (T-197 piece #10; .modding/investigations/m11a-design-2026-09-30.md sections 2-3; world server
   protocol 61): A GAME JOINS THROUGH THE WORLD SERVER. The pure half, included by the world server (coop-store), the plugin
   and the offline suite - one file, so the two ends cannot drift:
     - the per-connection STAGE (CONNECTED -> LOBBY -> TITLE -> LOADING -> IN_WORLD) and its steps (StageStep);
     - LiveDestAllowed: only an IN_WORLD game receives LIVE messages (a game at the title is never a destination), and a WORLD-ROAD
       game sends them only once IN_WORLD (S1 fold, review M1: a session-road sender is not judged by its stage - its first RELSYNC
       goes out on its world's first frame and can beat its first AREAS);
     - JoinLoadAllowed: the joining game's load gate - WELCOME on this link, a world server that carries the whole live
       stream (kRelayCarriesLiveStream), the operator in the world;
     - LiveProtoDecide: the game-to-game protocol the STORE_HELLO carries, refused when it differs from the games already
       admitted (an empty world accepts the first) - manager decision 1(a);
     - LiveProtoJudge (S1 fold, review M2): THE HOST'S VERSION WINS - the operator is never refused for differing, and the others
       are judged against its number while it is admitted (else against the games in the world, else those at the title);
     - the wires: the STORE_HELLO's tail {u32 live protocol, u32 road, u32 len + name, f32 view distance} (after the profile
       u32), PLAYERS (54, down) {u32 n, n x {u32 slot, u32 stage, u32 operator, u32 len + name, f32 view distance}} and
       JOIN_STAGE (55, up) {u32 stage: LOADING or IN_WORLD}.
   OLD-ROAD COMPATIBILITY: a game that joined by the session link sends road 0 and counts as IN_WORLD from its first AREAS, so
   two-game runs are unchanged; a world-road game (road 1) sends JOIN_STAGE IN_WORLD on its first running tick after the load
   gate (manager decision 2(a)), and its AREAS before that are refused and counted.
   No engine, no ENet, no C++11 (VS2010). */
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace coopjoin {

/* The world server carries the whole live stream (catch-up, area routing, addressed messages, per-player leave), so a world-road game
   WELCOMED with the operator in the world may load. False would keep every world-road game at the title. */
const bool kRelayCarriesLiveStream = true;

/* THE ONE DOOR: every game reaches the others through the world server alone. The address it dials: the settings' store= when they
   name one, else - a joining game - the address the player typed (host=). kDoorNone: nothing to dial (a host with no world server). */
enum Door { kDoorNone = 0, kDoorStore = 1, kDoorTyped = 2 };
inline int WorldDoorPick(bool joining, bool haveStore, bool haveTyped)
{
    if (haveStore) return kDoorStore;
    if (joining && haveTyped) return kDoorTyped;
    return kDoorNone;
}

enum Stage { kStageConnected = 0, kStageLobby = 1, kStageTitle = 2, kStageLoading = 3, kStageInWorld = 4 };
enum StageEvent { kEvHelloLobby = 0, kEvWelcome = 1, kEvLoading = 2, kEvInWorld = 3, kEvAreas = 4 };
enum Road { kRoadSession = 0, kRoadWorld = 1 };

inline const char* StageName(int s)
{
    switch (s)
    {
        case kStageConnected: return "connected";
        case kStageLobby:     return "lobby";
        case kStageTitle:     return "title";
        case kStageLoading:   return "loading";
        case kStageInWorld:   return "in-world";
    }
    return "?";
}

/* The next stage, or -1 = refused (the stage does not move; the caller counts it). A stage never steps back here: leaving a
   world is the connection's end (PeerGone), not a step. */
inline int StageStep(int stage, int ev, int road)
{
    if (stage < kStageConnected || stage > kStageInWorld) return -1;
    switch (ev)
    {
        case kEvHelloLobby: return (stage == kStageConnected || stage == kStageLobby) ? (int)kStageLobby : -1;
        case kEvWelcome:    return (stage == kStageConnected || stage == kStageLobby) ? (int)kStageTitle : -1;
        case kEvLoading:    return (stage == kStageTitle || stage == kStageLoading) ? (int)kStageLoading : -1;
        case kEvInWorld:    return (stage == kStageTitle || stage == kStageLoading || stage == kStageInWorld) ? (int)kStageInWorld : -1;
        case kEvAreas:
            if (stage == kStageInWorld) return kStageInWorld;
            if (road == kRoadSession && (stage == kStageTitle || stage == kStageLoading)) return kStageInWorld;   /* the old road enters at its first AREAS */
            return -1;   /* a world-road game's AREAS before its IN_WORLD, or AREAS from a game not admitted */
    }
    return -1;
}

/* Only a game IN the world is a LIVE destination (and a LIVE sender): a game at the title is never fed the stream. */
inline bool LiveDestAllowed(int stage) { return stage == kStageInWorld; }

/* M11 C5 item 1 - A WORLD-ROAD GAME RE-WELCOMED MID-WORLD SAYS IN_WORLD AGAIN. Each new world-server link starts this game at TITLE
   there (the WELCOME), so the IN_WORLD sent on an earlier link is gone with it. IN_WORLD is owed on this link when this link's gate
   let the load through (unchanged), OR the running world was entered through the world road on an earlier link
   (worldJoinedEarlier = JoinedByWorldRoad) and this link has been WELCOMED. An old-road game is neither: it enters at its AREAS. */
inline bool InWorldOwedDecide(bool loadGatedThisLink, bool worldJoinedEarlier, bool welcomedThisLink)
{
    return loadGatedThisLink || (worldJoinedEarlier && welcomedThisLink);
}
/* M11 C5 fold 1 (owner 357 a, 2026-10-02) - NO LOADING A SAVE WHILE CONNECTED TO OTHER PLAYERS, by either road. Kenshi has no
   in-game way back to the title (a player leaves a world by quitting the game), so the F926 rule is extended: a load from inside a
   running world (titleScreenUp 0) is refused while this game is connected to another player - the session link up (F926,
   unchanged: kLoadConnRefusedSession) or the world server: its link up and WELCOMED (worldWelcomed) AND that link's PLAYERS roster
   shows another slot IN_WORLD (rosterOtherInWorld 1; 0 none, -1 no roster on this link: kLoadConnRefusedWorld). A game alone on
   the world server loads as before; a load at the title (1) or with the title unreadable (-1) is never refused here, as F926.
   Both load deciders (store.cpp: the save-request pump and the loadGame backstop) ask this, the same way. */
enum LoadConnVerdict { kLoadConnAllowed = 0, kLoadConnRefusedSession = 1, kLoadConnRefusedWorld = 2 };
inline int LoadWhileConnectedDecide(int titleScreenUp, bool sessionLinked, bool worldWelcomed, int rosterOtherInWorld)
{
    if (titleScreenUp != 0) return kLoadConnAllowed;
    if (sessionLinked) return kLoadConnRefusedSession;
    return (worldWelcomed && rosterOtherInWorld == 1) ? (int)kLoadConnRefusedWorld : (int)kLoadConnAllowed;
}

/* THE JOINING GAME'S LOAD GATE (design s2 step 5). */
enum JoinGate { kJoinLoadAllowed = 0, kJoinLoadNoWelcome = 1, kJoinLoadNoStream = 2, kJoinLoadNoOperator = 3 };
inline int JoinLoadAllowed(bool welcomed, bool relayCarriesStream, bool operatorInWorld)
{
    if (!welcomed) return kJoinLoadNoWelcome;
    if (!relayCarriesStream) return kJoinLoadNoStream;
    if (!operatorInWorld) return kJoinLoadNoOperator;   /* manager decision 3(a): no joining while the operator is not in the world */
    return kJoinLoadAllowed;
}
inline const char* JoinGateText(int v)
{
    switch (v)
    {
        case kJoinLoadAllowed:    return "JOIN gate: load allowed";
        case kJoinLoadNoWelcome:  return "JOIN gate: load REFUSED - the world server has not welcomed this game on this link";
        case kJoinLoadNoStream:   return "JOIN gate: load REFUSED - the world server does not carry the live stream yet";
        case kJoinLoadNoOperator: return "JOIN gate: load REFUSED - the world's operator is not in the world";
    }
    return "JOIN gate: ?";
}
/* A LOAD REFUSED ONLY BECAUSE THE OPERATOR IS NOT IN THE WORLD YET IS HELD, NOT DROPPED. Two games started together: the joiner is
   welcomed while the host is still loading its own world, so its load is refused for the operator alone - and nothing would ever ask
   again. The asked load is remembered and started when a PLAYERS roster shows the operator IN_WORLD (an event, never a timer).
   kind: kHoldPress (the JOIN press's automatic load), kHoldLoad (a load request at the title - the `load` command or the Load
   window), kHoldNone (not holdable: a NEW GAME or import request, or the WELCOME check that asks no load). Any other refusal reason
   is never held. */
enum JoinHoldKind { kHoldNone = 0, kHoldPress = 1, kHoldLoad = 2 };
inline bool JoinHoldOnRefusal(int gateVerdict, int kind)
{
    return kind != kHoldNone && gateVerdict == kJoinLoadNoOperator;
}
/* What a held load does when a roster arrives. cancelled: the player left or cancelled; otherStarted: another load is under way or the
   title is gone; sameLinkWelcomed: the world-server link the hold was made on is still up and welcomed (a drop or a re-dial ends the
   hold - the normal re-dial / CAN'T JOIN path applies); gateVerdictNow: the full JOIN gate read live at this moment. */
enum JoinHoldStep { kHoldKeep = 0, kHoldStart = 1, kHoldDrop = 2 };
inline int JoinHoldDecide(bool cancelled, bool otherStarted, bool sameLinkWelcomed, int gateVerdictNow)
{
    if (cancelled || otherStarted || !sameLinkWelcomed) return kHoldDrop;
    if (gateVerdictNow == kJoinLoadAllowed) return kHoldStart;
    if (gateVerdictNow == kJoinLoadNoOperator) return kHoldKeep;
    return kHoldDrop;   /* any other refusal: as without a hold */
}

/* THE LIVE (game-to-game) PROTOCOL, checked by the world server at HELLO. mine 0 = the HELLO carried none (unreadable tail). */
enum LiveProtoVerdict { kLiveProtoAdmit = 0, kLiveProtoRefuseDiffers = 1, kLiveProtoRefuseUnreadable = 2 };
inline int LiveProtoDecide(unsigned mine, const std::vector<unsigned>& admitted, unsigned* theirs)
{
    if (theirs) *theirs = admitted.empty() ? 0u : admitted[0];
    if (mine == 0) return kLiveProtoRefuseUnreadable;
    for (size_t i = 0; i < admitted.size(); ++i)
        if (admitted[i] != mine) { if (theirs) *theirs = admitted[i]; return kLiveProtoRefuseDiffers; }
    return kLiveProtoAdmit;
}
/* S1 fold (review M2) - THE HOST'S VERSION WINS. isOperator: this HELLO is the world's operator's. The operator is never refused for
   differing - its number is the one the others are judged by, so one out-of-date game idling at the title cannot lock the operator
   out of its own world. An unreadable number is still refused, operator or not: nothing could be judged against it. Anyone else is
   judged against operatorProto while the operator is admitted (non-zero), else against the admitted games in the world, else
   against the admitted games at the title (or loading); an empty world accepts the first. */
inline int LiveProtoJudge(unsigned mine, bool isOperator, unsigned operatorProto, const std::vector<unsigned>& inWorld,
                          const std::vector<unsigned>& notInWorld, unsigned* theirs)
{
    std::vector<unsigned> against;
    if (isOperator) {}
    else if (operatorProto != 0) against.push_back(operatorProto);
    else against = inWorld.empty() ? notInWorld : inWorld;
    return LiveProtoDecide(mine, against, theirs);
}

/* ---- the wires ---- */
const unsigned kNameMax = 64;       /* a player name on the wire; longer is cut on encode and refused on decode */
const unsigned kRosterMax = 1024;   /* rows in one PLAYERS message (the world server admits at most 256 at once) */

inline void JsPut32(std::vector<char>* b, unsigned v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline bool JsGet32(const char* p, size_t n, size_t* at, unsigned* v)
{
    if (p == 0 || *at > n || n - *at < 4) return false;
    std::memcpy(v, p + *at, 4); *at += 4; return true;
}
inline void JsPutStr(std::vector<char>* b, const std::string& s)
{
    const std::string c = s.size() > kNameMax ? s.substr(0, kNameMax) : s;
    JsPut32(b, (unsigned)c.size()); b->insert(b->end(), c.begin(), c.end());
}
inline bool JsGetStr(const char* p, size_t n, size_t* at, std::string* s)
{
    unsigned len = 0;
    if (!JsGet32(p, n, at, &len) || len > kNameMax || n - *at < len) return false;
    s->assign(p + *at, p + *at + len); *at += len; return true;
}
inline void JsPutF32(std::vector<char>* b, float f) { unsigned u = 0; std::memcpy(&u, &f, 4); JsPut32(b, u); }
inline bool JsGetF32(const char* p, size_t n, size_t* at, float* f) { unsigned u = 0; if (!JsGet32(p, n, at, &u)) return false; std::memcpy(f, &u, 4); return true; }

/* The STORE_HELLO's tail, after the profile u32. */
struct HelloTail
{
    unsigned liveProto;   /* the game-to-game protocol this build speaks (net/session.cpp kProtocolVersion) */
    unsigned road;        /* kRoadSession / kRoadWorld */
    std::string name;     /* the player's name (shared_wastelands.cfg playername=), "" = none */
    float viewDist;       /* this game's view distance, 0 = "I cannot tell you" */
    HelloTail() : liveProto(0), road(0), viewDist(0.0f) {}
};
inline void HelloTailEncode(const HelloTail& t, std::vector<char>* b)
{
    JsPut32(b, t.liveProto); JsPut32(b, t.road); JsPutStr(b, t.name); JsPutF32(b, t.viewDist);
}
inline bool HelloTailDecode(const char* p, size_t n, size_t at, HelloTail* t)
{
    HelloTail r;
    if (!JsGet32(p, n, &at, &r.liveProto) || !JsGet32(p, n, &at, &r.road) || !JsGetStr(p, n, &at, &r.name) || !JsGetF32(p, n, &at, &r.viewDist)) return false;
    if (r.road != (unsigned)kRoadSession && r.road != (unsigned)kRoadWorld) return false;
    *t = r;
    return true;
}

/* PLAYERS (54, down): every admitted game, sorted by slot. */
struct RosterRow
{
    unsigned slot, stage, op;
    std::string name;
    float viewDist;
    RosterRow() : slot(0), stage(0), op(0), viewDist(0.0f) {}
};
inline void RosterSortBySlot(std::vector<RosterRow>* rows)
{
    for (size_t i = 1; i < rows->size(); ++i)
        for (size_t j = i; j > 0 && (*rows)[j - 1].slot > (*rows)[j].slot; --j) { RosterRow t = (*rows)[j - 1]; (*rows)[j - 1] = (*rows)[j]; (*rows)[j] = t; }
}
inline bool RosterEncode(const std::vector<RosterRow>& rows, std::vector<char>* b)
{
    if (rows.size() > kRosterMax) return false;
    JsPut32(b, (unsigned)rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    { JsPut32(b, rows[i].slot); JsPut32(b, rows[i].stage); JsPut32(b, rows[i].op ? 1u : 0u); JsPutStr(b, rows[i].name); JsPutF32(b, rows[i].viewDist); }
    return true;
}
inline bool RosterDecode(const char* p, size_t n, std::vector<RosterRow>* out)
{
    size_t at = 0; unsigned cnt = 0;
    if (!JsGet32(p, n, &at, &cnt) || cnt > kRosterMax) return false;
    std::vector<RosterRow> rows;
    for (unsigned i = 0; i < cnt; ++i)
    {
        RosterRow r;
        if (!JsGet32(p, n, &at, &r.slot) || !JsGet32(p, n, &at, &r.stage) || !JsGet32(p, n, &at, &r.op) || !JsGetStr(p, n, &at, &r.name) || !JsGetF32(p, n, &at, &r.viewDist)) return false;
        if (r.stage > (unsigned)kStageInWorld || r.op > 1) return false;
        rows.push_back(r);
    }
    if (at != n) return false;   /* exact: a longer frame is another shape */
    *out = rows;
    return true;
}
inline bool RosterOperatorInWorld(const std::vector<RosterRow>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].op != 0 && rows[i].stage == (unsigned)kStageInWorld) return true;
    return false;
}
/* M11a S3 (T-197; manager decision 7(a), 2026-09-30) - THE OPERATOR ON THE ROSTER, FOR A WORLD-ROAD GAME'S HOST-LEFT.
   RosterOperatorSlot: the slot of the row flagged operator (-1 = the roster lists none). RosterListsSlot: that slot has a row.
   ClosingLiveAccepted (7(a) (i)): a SESSION_CLOSING that came through the world server counts only when its STAMPED origin (the
   world server writes it, never the sender) is the operator's slot this link's roster named; anyone else's, or any with no
   operator known, is refused. OperatorGoneDecide (7(a) (ii)): PLAYER_GONE (57) - sent by the world server only after its 10 s
   hold, so a blip it forgives never reaches here - names the operator's slot, and no roster since lists that slot again. */
inline int RosterOperatorSlot(const std::vector<RosterRow>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].op != 0) return (int)rows[i].slot;
    return -1;
}
inline bool RosterListsSlot(const std::vector<RosterRow>& rows, int slot)
{
    for (size_t i = 0; i < rows.size(); ++i) if (slot >= 0 && rows[i].slot == (unsigned)slot) return true;
    return false;
}
inline bool ClosingLiveAccepted(unsigned originSlot, int operatorSlot) { return operatorSlot >= 0 && originSlot == (unsigned)operatorSlot; }
inline bool OperatorGoneDecide(unsigned goneSlot, int operatorSlot, bool operatorListedAgain)
{
    return operatorSlot >= 0 && goneSlot == (unsigned)operatorSlot && !operatorListedAgain;
}
/* M11a S3 review fold (F1, 2026-09-30) - A WORLD-ROAD JOINER BY THE ROAD THIS GAME ACTUALLY JOINED THROUGH. S3 called any game a
   world-road joiner that was not a session joiner, the session host or the operator - so a two-player joiner right after its own
   `leave` (the session link gone, the world-server link kept) and a single-player game were watched for a host too. Now:
   JoinRoadStep - the join-road flag is SET when this game's JOIN completes through the world road (JOIN_STAGE IN_WORLD sent after
   a load the join gate let through: joinvia=world today, every JOIN after the flip) and CLEARED by the next world load or teardown
   and by the return to the title. JoinedByWorldRoad - the flag holds only while the session generation it was stamped with has
   not moved: this game's own leave, a new host or a new join all run SessionLeave, which moves it. WorldRoadJoinerDecide - joined
   through the world road AND not a session joiner, not the session host, not the world's operator. */
const int kJrInWorldSent = 1, kJrWorldLoad = 2, kJrTitle = 3;
inline int JoinRoadStep(int joined, int ev)
{
    if (ev == kJrInWorldSent) return 1;
    if (ev == kJrWorldLoad || ev == kJrTitle) return 0;
    return joined;
}
inline bool JoinedByWorldRoad(int joined, long sessionGenAt, long sessionGenNow) { return joined != 0 && sessionGenAt == sessionGenNow; }
inline bool WorldRoadJoinerDecide(bool joinedByWorldRoad, bool sessionJoiner, bool sessionHost, bool operatorHere, bool operatorIsMe)
{
    return joinedByWorldRoad && !sessionJoiner && !sessionHost && !operatorHere && !operatorIsMe;
}
/* A name as a log shows it: anything outside printable ASCII (and the quote) becomes '?'. */
inline std::string JsLogSafe(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) { const unsigned char c = (unsigned char)o[i]; if (c < 32 || c > 126 || c == 39) o[i] = '?'; }
    return o;
}
/* One line for a log: "3 player(s): slot 0 'A' in-world operator; slot 2 (no name) title (this game)". mySlot -1 = none. */
inline std::string RosterText(const std::vector<RosterRow>& rows, int mySlot)
{
    char b[48];
    std::sprintf(b, "%u player(s)", (unsigned)rows.size());
    std::string s(b);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        std::sprintf(b, "%s slot %u ", i == 0 ? ":" : ";", rows[i].slot);
        s += b;
        s += rows[i].name.empty() ? std::string("(no name)") : "'" + JsLogSafe(rows[i].name) + "'";
        s += std::string(" ") + StageName((int)rows[i].stage);
        if (rows[i].op != 0) s += " operator";
        if (mySlot >= 0 && rows[i].slot == (unsigned)mySlot) s += " (this game)";
    }
    return s;
}

/* JOIN_STAGE (55, up): {u32 stage} - LOADING or IN_WORLD only. */
inline bool JoinStageEncode(unsigned stage, std::vector<char>* b)
{
    if (stage != (unsigned)kStageLoading && stage != (unsigned)kStageInWorld) return false;
    JsPut32(b, stage);
    return true;
}
inline bool JoinStageDecode(const char* p, size_t n, unsigned* stage)
{
    size_t at = 0; unsigned s = 0;
    if (n != 4 || !JsGet32(p, n, &at, &s)) return false;
    if (s != (unsigned)kStageLoading && s != (unsigned)kStageInWorld) return false;
    *stage = s;
    return true;
}

}   /* namespace coopjoin */
