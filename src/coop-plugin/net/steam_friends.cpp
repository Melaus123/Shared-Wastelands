/* net/steam_friends.cpp - STEAM FRIENDS FIND AND JOIN A HOST (the decisions are src/common/steamfriends.h).

   HOW STEAM IS REACHED. As the probe does (net/steam_probe.cpp): the game's own steam_api64.dll, found with GetModuleHandle - never
   LoadLibrary, so a GOG copy (no Steam DLL) does nothing here - and its exports by GetProcAddress. The interface objects come from the
   probe's ISteamClient (SteamProbeInterface) by the versions the game's DLL was built with, SteamFriends015 and SteamUser019 (the names
   embedded in that DLL), because the DLL's flat exports (SteamAPI_ISteamFriends_*, SteamAPI_ISteamUser_GetSteamID) call through the
   layout of those versions. Flat calls take the interface as their first argument; a Steam id goes in and comes out as a 64-bit number.

   CALLBACKS. Steam tells a game about a friend's changed rich presence (FriendRichPresenceUpdate_t, 336) and about a "Join Game" or an
   accepted invite while the game runs (GameRichPresenceJoinRequested_t, 337) only through SteamAPI_RunCallbacks, which the probe pumps
   (SteamPumpForSession is asked as soon as the two are registered here). Each is registered with SteamAPI_RegisterCallback as an object
   laid out like Steam's CCallbackBase: a table of three functions - the call-result run, the run, the size (the order MSVC gives Steam's
   two overloaded Run functions, the second one first) - then a flags byte and the callback number, which Steam fills. Each run only
   copies what Steam handed over; the tick below acts on it.

   SAFETY. Every Steam call is inside __try in a function that holds no C++ object (C2712); a fault is reported to the probe
   (SteamNoteFault), which ends every Steam use in this game. Nothing logged carries a Steam id or a friend's name. */
#include <windows.h>

#include "steam_friends.h"
#include "steam_probe.h"
#include "session.h"            /* SessionProtocolVersion: the presence's protocol key */
#include "../coop_log.h"
#include "../ui.h"              /* UiHostingNow, UiJoinRefuseShow / UiJoinRefuseTick */
#include "../store.h"           /* StoreRosterNames, StoreHostHasPassword */
#include "../soak.h"            /* GameplayRunning: a join request in a world is refused */

#include <cstring>
#include <sstream>
#include <locale>

namespace coop {

namespace {

std::string N(long long v)   // RE_Kenshi's locale mangles raw numbers (F030)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << v;
    return ss.str();
}

const char* const kFriendsVersion = "SteamFriends015";
const char* const kUserVersion    = "SteamUser019";
const int kFriendFlagImmediate = 0x04;   /* EFriendFlags k_EFriendFlagImmediate: the player's own friends */
const int kFriendsReadCap = 2000;        /* the most friends one read walks */
const int kCbPresenceUpdate = 336;       /* FriendRichPresenceUpdate_t: CSteamID friend, AppId_t app */
const int kCbJoinRequested  = 337;       /* GameRichPresenceJoinRequested_t: CSteamID friend, char connect[256] */

typedef bool               (*FnSetRP)(intptr_t self, const char* key, const char* value);
typedef void               (*FnClearRP)(intptr_t self);
typedef int                (*FnFriendCount)(intptr_t self, int flags);
typedef unsigned long long (*FnFriendByIndex)(intptr_t self, int index, int flags);
typedef const char*        (*FnPersonaName)(intptr_t self, unsigned long long id);
typedef bool               (*FnGamePlayed)(intptr_t self, unsigned long long id, void* info);
typedef const char*        (*FnFriendRP)(intptr_t self, unsigned long long id, const char* key);
typedef void               (*FnRequestRP)(intptr_t self, unsigned long long id);
typedef unsigned long long (*FnGetSteamID)(intptr_t self);
typedef void               (*FnRegister)(void* callback, int id);
typedef int                (*FnPersonaState)(intptr_t self, unsigned long long id);
typedef bool               (*FnInvite)(intptr_t self, unsigned long long id, const char* connect);

struct Api
{
    FnSetRP setRP; FnClearRP clearRP; FnFriendCount count; FnFriendByIndex byIndex; FnPersonaName persona; FnGamePlayed played;
    FnFriendRP friendRP; FnRequestRP requestRP; FnGetSteamID selfId; FnRegister reg;
    FnPersonaState state; FnInvite invite;   /* INVITE FRIENDS only: looked up apart, so a missing one turns off INVITE FRIENDS alone */
};

/* Steam's CCallbackBase as MSVC lays it out (steam_api.h): the call-result Run, the Run, GetCallbackSizeBytes; a flags byte; the id. */
class CbBase
{
public:
    CbBase() : m_flags(0), m_id(0) {}
    virtual void RunCallResult(void* param, bool ioFailure, unsigned long long call) { (void)ioFailure; (void)call; Run(param); }
    virtual void Run(void* param) = 0;
    virtual int  GetCallbackSizeBytes() = 0;
    unsigned char m_flags;
    int           m_id;
};

volatile LONG g_presenceNews = 0;    /* a 336 arrived since the last tick */
volatile LONG g_joinNews = 0;        /* a 337 arrived: g_joinConnect holds its text */
char          g_joinConnect[257];
unsigned long long g_joinFriend = 0;   /* the friend that 337 names (its first 8 bytes) */

class CbPresence : public CbBase
{
public:
    virtual void Run(void* param) { (void)param; ::InterlockedExchange(&g_presenceNews, 1); }
    virtual int  GetCallbackSizeBytes() { return 16; }
};
class CbJoin : public CbBase
{
public:
    virtual void Run(void* param)
    {
        if (param == 0) return;
        std::memcpy(&g_joinFriend, param, 8);   /* the friend's 8-byte id */
        std::memcpy(g_joinConnect, (const char*)param + 8, 256);   /* after it, the connect text */
        g_joinConnect[256] = 0;
        ::InterlockedExchange(&g_joinNews, 1);
    }
    virtual int  GetCallbackSizeBytes() { return 264; }
};
CbPresence g_cbPresence;
CbJoin     g_cbJoin;

Api       g_api;
int       g_state = 0;          /* 0 not set up yet, 1 set up (g_friends live), 2 never (no DLL, not signed in, missing export, fault) */
void*     g_friends = 0;        /* ISteamFriends (SteamFriends015) */
unsigned long long g_self = 0;  /* this player's own Steam id (never logged) */
long      g_gen = 0;
int       g_launchAsked = 0;    /* the command line was read */
int       g_fake = 0;           /* TEST-ONLY: sample rows shown (0 = Steam's) */
steamfriends::KeyList g_keysSet;   /* the presence keys Steam has from this game */
int       g_keysAny = 0;        /* 1 = keys were set (a clear is owed when hosting stops) */
long long g_presenceFaults = 0;
/* the join request waiting for the title screen */
int                g_pendFrom = -1;
unsigned long long g_pendId = 0;
std::string        g_pendName;

/* ---- the only frames that call into Steam; no C++ objects below this line until the next marker ---- */

int SehSetRP(FnSetRP fn, void* self, const char* key, const char* value)
{
    __try { fn((intptr_t)self, key, value); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SehClearRP(FnClearRP fn, void* self)
{
    __try { fn((intptr_t)self); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SehSelfId(FnGetSteamID fn, void* user, unsigned long long* out)
{
    __try { *out = fn((intptr_t)user); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
int SehRegister(FnRegister fn, void* cb, int id)
{
    __try { fn(cb, id); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SehCount(FnFriendCount fn, void* self, int* out)
{
    __try { *out = fn((intptr_t)self, kFriendFlagImmediate); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
int SehByIndex(FnFriendByIndex fn, void* self, int i, unsigned long long* out)
{
    __try { *out = fn((intptr_t)self, i, kFriendFlagImmediate); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
/* 1 = in a game (FriendGameInfo_t is 24 bytes; 64 are given), 0 = not, -1 = a fault. */
int SehPlayed(FnGamePlayed fn, void* self, unsigned long long id)
{
    __try { unsigned char info[64]; return fn((intptr_t)self, id, info) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
/* A Steam string copied into out[cap] (NUL-ended, "" for none). */
int SehFriendRP(FnFriendRP fn, void* self, unsigned long long id, const char* key, char* out, int cap)
{
    __try
    {
        out[0] = 0;
        const char* s = fn((intptr_t)self, id, key);
        if (s == 0) return 1;
        int i = 0;
        for (; i < cap - 1 && s[i] != 0; ++i) out[i] = s[i];
        out[i] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
int SehPersona(FnPersonaName fn, void* self, unsigned long long id, char* out, int cap)
{
    __try
    {
        out[0] = 0;
        const char* s = fn((intptr_t)self, id);
        if (s == 0) return 1;
        int i = 0;
        for (; i < cap - 1 && s[i] != 0; ++i) out[i] = s[i];
        out[i] = 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return 0; }
}
int SehRequestRP(FnRequestRP fn, void* self, unsigned long long id)
{
    __try { fn((intptr_t)self, id); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int SehPersonaState(FnPersonaState fn, void* self, unsigned long long id, int* out)
{
    __try { *out = fn((intptr_t)self, id); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
/* The app id of the game a friend plays now: FriendGameInfo_t starts with the 64-bit game id, whose low 24 bits are the app id
   (0 = not in a game). 1 = read, 0 = a fault. */
int SehPlayedApp(FnGamePlayed fn, void* self, unsigned long long id, unsigned* app)
{
    __try
    {
        unsigned long long info[8];
        info[0] = 0;
        *app = fn((intptr_t)self, id, info) ? (unsigned)(info[0] & 0xFFFFFFULL) : 0u;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *app = 0; return 0; }
}
int SehInvite(FnInvite fn, void* self, unsigned long long id, const char* connect, int* ok)
{
    __try { *ok = fn((intptr_t)self, id, connect) ? 1 : 0; return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ok = 0; return 0; }
}

/* ---- end of the __try frames ---- */

void Fault(const char* where)
{
    SteamNoteFault();
    g_state = 2;
    g_friends = 0;
    ++g_gen;   /* the list says Steam isn't running from now on */
    DebugLog(std::string("[NET] steam friends: a Steam call faulted (") + where + ") - friends' games, presence and join requests stop for this session");
}

bool Live() { return g_state == 1 && g_friends != 0 && SteamProbeRunning(); }

void SetUp()
{
    if (!SteamProbeStarted()) return;
    if (SteamProbeHasDll() != 1 || !SteamProbeRunning()) { g_state = 2; ++g_gen; return; }   /* the list's placeholder says which */
    HMODULE mod = ::GetModuleHandleW(L"steam_api64.dll");   /* never LoadLibrary */
    if (mod == 0) { g_state = 2; ++g_gen; return; }
    g_api.setRP     = (FnSetRP)::GetProcAddress(mod, "SteamAPI_ISteamFriends_SetRichPresence");
    g_api.clearRP   = (FnClearRP)::GetProcAddress(mod, "SteamAPI_ISteamFriends_ClearRichPresence");
    g_api.count     = (FnFriendCount)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendCount");
    g_api.byIndex   = (FnFriendByIndex)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendByIndex");
    g_api.persona   = (FnPersonaName)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    g_api.played    = (FnGamePlayed)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    g_api.friendRP  = (FnFriendRP)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendRichPresence");
    g_api.requestRP = (FnRequestRP)::GetProcAddress(mod, "SteamAPI_ISteamFriends_RequestFriendRichPresence");
    g_api.selfId    = (FnGetSteamID)::GetProcAddress(mod, "SteamAPI_ISteamUser_GetSteamID");
    g_api.reg       = (FnRegister)::GetProcAddress(mod, "SteamAPI_RegisterCallback");
    const char* missing = g_api.setRP == 0 ? "SteamAPI_ISteamFriends_SetRichPresence"
                        : g_api.clearRP == 0 ? "SteamAPI_ISteamFriends_ClearRichPresence"
                        : g_api.count == 0 ? "SteamAPI_ISteamFriends_GetFriendCount"
                        : g_api.byIndex == 0 ? "SteamAPI_ISteamFriends_GetFriendByIndex"
                        : g_api.persona == 0 ? "SteamAPI_ISteamFriends_GetFriendPersonaName"
                        : g_api.played == 0 ? "SteamAPI_ISteamFriends_GetFriendGamePlayed"
                        : g_api.friendRP == 0 ? "SteamAPI_ISteamFriends_GetFriendRichPresence"
                        : g_api.requestRP == 0 ? "SteamAPI_ISteamFriends_RequestFriendRichPresence"
                        : g_api.selfId == 0 ? "SteamAPI_ISteamUser_GetSteamID"
                        : g_api.reg == 0 ? "SteamAPI_RegisterCallback" : 0;
    if (missing != 0)
    {
        g_state = 2; ++g_gen;
        DebugLog(std::string("[NET] steam friends: export ") + missing + " not found - no friends' games, presence or join requests");
        return;
    }
    g_api.state  = (FnPersonaState)::GetProcAddress(mod, "SteamAPI_ISteamFriends_GetFriendPersonaState");
    g_api.invite = (FnInvite)::GetProcAddress(mod, "SteamAPI_ISteamFriends_InviteUserToGame");
    if (g_api.state == 0 || g_api.invite == 0)
        DebugLog(std::string("[NET] steam friends: export ") + (g_api.state == 0 ? "SteamAPI_ISteamFriends_GetFriendPersonaState" : "SteamAPI_ISteamFriends_InviteUserToGame")
                 + " not found - INVITE FRIENDS is off (friends' games, presence and join requests are not affected)");
    void* friends = SteamProbeInterface(kFriendsVersion);
    void* user = SteamProbeInterface(kUserVersion);
    if (friends == 0 || user == 0)
    {
        g_state = 2; ++g_gen;
        DebugLog(std::string("[NET] steam friends: Steam handed back ") + kFriendsVersion + "=" + (friends != 0 ? "yes" : "no") + " "
                 + kUserVersion + "=" + (user != 0 ? "yes" : "no") + " - no friends' games, presence or join requests");
        return;
    }
    unsigned long long self = 0;
    if (!SehSelfId(g_api.selfId, user, &self)) { Fault("GetSteamID"); return; }
    if (!SehRegister(g_api.reg, &g_cbPresence, kCbPresenceUpdate)) { Fault("RegisterCallback 336"); return; }
    if (!SehRegister(g_api.reg, &g_cbJoin, kCbJoinRequested)) { Fault("RegisterCallback 337"); return; }
    g_friends = friends;
    g_self = self;
    g_state = 1;
    ++g_gen;
    SteamPumpForSession();   /* the two callbacks arrive only through SteamAPI_RunCallbacks */
    DebugLog(std::string("[NET] steam friends: ") + kFriendsVersion + " and " + kUserVersion + " found; rich-presence updates (336) and"
             " join requests (337) registered; own id " + (self != 0 ? "read" : "NOT read (no presence will be set)"));
}

/* THE HOST'S PRESENCE: the keys are re-derived from the live hosting state each tick and Steam is told only when one changed - a join or a
   leave (the world server's roster), hosting starting or stopping, the world server going away. */
void PresenceTick()
{
    steamfriends::HostState h;
    std::string world;
    h.hosting = UiHostingNow(&world);
    h.selfId = g_self;
    h.world = world;
    {   /* the players on the world server's roster now: the others and this host */
        std::vector<std::string> others;
        const int n = StoreRosterNames(&others);
        h.players = n >= 0 ? n + 1 : 1;
    }
    h.password = StoreHostHasPassword();
    h.protocol = net::SessionProtocolVersion();
    const steamfriends::KeyList want = steamfriends::PresenceKeys(h);
    if (want == g_keysSet) return;
    if (want.empty())
    {
        if (g_keysAny != 0 && !SehClearRP(g_api.clearRP, g_friends)) { Fault("ClearRichPresence"); return; }
        g_keysSet.clear();
        g_keysAny = 0;
        DebugLog("[NET] steam presence: cleared (not hosting)");
        return;
    }
    for (size_t i = 0; i < want.size(); ++i)
    {
        bool same = false;
        for (size_t j = 0; j < g_keysSet.size(); ++j) if (g_keysSet[j] == want[i]) same = true;
        if (same) continue;
        if (!SehSetRP(g_api.setRP, g_friends, want[i].first.c_str(), want[i].second.c_str())) { Fault("SetRichPresence"); return; }
    }
    g_keysSet = want;
    g_keysAny = 1;
    DebugLog("[NET] steam presence: set " + steamfriends::KeysForLog(want));
}

/* One friend's keys read now into a row; false = not in a game, not hosting this game, or a key unreadable. */
bool ReadRow(unsigned long long id, steamfriends::FriendRow* out)
{
    const int played = SehPlayed(g_api.played, g_friends, id);
    if (played < 0) { Fault("GetFriendGamePlayed"); return false; }
    if (played == 0) return false;
    char connect[257], world[257], players[257], password[257], protocol[257], name[129];
    if (!SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyConnect, connect, sizeof(connect))) { Fault("GetFriendRichPresence"); return false; }
    if (connect[0] == 0) return false;
    if (!SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyWorld, world, sizeof(world))
        || !SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyPlayers, players, sizeof(players))
        || !SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyPassword, password, sizeof(password))
        || !SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyProtocol, protocol, sizeof(protocol)))
    { Fault("GetFriendRichPresence"); return false; }
    if (!SehPersona(g_api.persona, g_friends, id, name, sizeof(name))) { Fault("GetFriendPersonaName"); return false; }
    return steamfriends::RowFromKeys(id, name, connect, world, players, password, protocol, net::SessionProtocolVersion(), out);
}

/* The friend a request names, by name (empty when Steam does not know it yet). */
std::string NameOf(unsigned long long id)
{
    if (!Live() || id == 0) return std::string();
    char name[129];
    if (!SehPersona(g_api.persona, g_friends, id, name, sizeof(name))) { Fault("GetFriendPersonaName"); return std::string(); }
    return std::string(name);
}

/* A friend's connect key read from Steam now ("" = not readable or not set). */
std::string LiveConnect(unsigned long long id)
{
    if (!Live() || id == 0) return std::string();
    char connect[257];
    if (!SehFriendRP(g_api.friendRP, g_friends, id, steamfriends::kKeyConnect, connect, sizeof(connect))) { Fault("GetFriendRichPresence"); return std::string(); }
    return std::string(connect);
}

/* THE ONE JOIN-REQUEST HANDLER: Steam's 337, the launch command line and the steamjoin lever all come here with a connect string. In a
   world: CAN'T JOIN. Otherwise the request waits for the title screen (ui.cpp takes it), a newer one replacing an older one. */
bool HandleJoin(const std::string& connect, int from, unsigned long long named)   /* named: the friend 337 names (0 = launch / lever) */
{
    unsigned long long id = 0;
    if (!steamfriends::ParseConnect(connect, &id))
    {
        DebugLog(std::string("[NET] steam join request (source ") + steamfriends::SourceWord(from) + ") ignored: not this mod's connect string");
        return false;
    }
    if (from == steamfriends::kFromCallback && !steamfriends::ConnectNamesFriend(named, connect))
    {
        DebugLog("[NET] steam join request (source callback) ignored: its connect string names another player than the friend Steam named");
        return false;
    }
    const std::string name = NameOf(id);
    if (GameplayRunning())
    {
        const int r = UiJoinRefuseShow(steamfriends::InWorldRefuseText(name).c_str());
        DebugLog(std::string("[NET] steam join request (source ") + steamfriends::SourceWord(from) + ") while in a world - CAN'T JOIN shown"
                 " (quit Kenshi, then accept the invite again; rc=" + N(r) + ")");
        return true;
    }
    g_pendFrom = from;
    g_pendId = id;
    g_pendName = name;
    DebugLog(std::string("[NET] steam join request (source ") + steamfriends::SourceWord(from) + ") - the join starts at the title screen"
             " as a pick of that friend's game; the friend's name " + (name.empty() ? "is not known yet" : "is known"));
    return true;
}

}   /* anonymous namespace */

void SteamFriendsTick()
{
    if (g_state == 0) SetUp();
    UiJoinRefuseTick();
    if (g_launchAsked == 0 && g_state != 0)
    {   /* once Steam is known: a game Steam started from a Join or an invite carries the connect string on its command line */
        g_launchAsked = 1;
        const char* cmd = ::GetCommandLineA();
        const std::string line = cmd != 0 ? std::string(cmd) : std::string();
        if (steamfriends::ParseConnect(line, 0)) HandleJoin(line, steamfriends::kFromLaunch, 0);
        else DebugLog("[NET] steam join: the command line carries no join request");
    }
    if (g_pendFrom >= 0 && GameplayRunning())   /* the request waited through a press or a load and a world is now up: CAN'T JOIN */
    {
        const std::string name = g_pendName.empty() ? NameOf(g_pendId) : g_pendName;
        const int r = UiJoinRefuseShow(steamfriends::InWorldRefuseText(name).c_str());
        DebugLog(std::string("[NET] steam join request (source ") + steamfriends::SourceWord(g_pendFrom) + ") waited and a world loaded - CAN'T JOIN shown"
                 " (quit Kenshi, then accept the invite again; rc=" + N(r) + ")");
        g_pendFrom = -1; g_pendId = 0; g_pendName.clear();
    }
    if (!Live()) return;
    if (::InterlockedExchange(&g_presenceNews, 0) != 0) ++g_gen;
    if (::InterlockedExchange(&g_joinNews, 0) != 0)
    {
        const std::string connect(g_joinConnect);
        HandleJoin(connect, steamfriends::kFromCallback, g_joinFriend);
    }
    PresenceTick();
}

int SteamFriendsShows(std::vector<steamfriends::FriendRow>* rows)
{
    rows->clear();
    if (g_fake > 0)
    {
        *rows = steamfriends::FakeRows(g_fake);
        DebugLog("[NET] steam friends list: " + N((long long)rows->size()) + " hosting (TEST-ONLY sample rows)");
        return steamfriends::kShowList;
    }
    const int running = Live() ? 1 : 0;
    if (running != 0)
    {
        int count = 0;
        if (!SehCount(g_api.count, g_friends, &count)) Fault("GetFriendCount");
        if (count > kFriendsReadCap) count = kFriendsReadCap;
        for (int i = 0; i < count && Live(); ++i)
        {
            unsigned long long id = 0;
            if (!SehByIndex(g_api.byIndex, g_friends, i, &id)) { Fault("GetFriendByIndex"); break; }
            steamfriends::FriendRow r;
            if (id != 0 && ReadRow(id, &r)) rows->push_back(r);
        }
    }
    const int shows = steamfriends::ListShows(SteamProbeHasDll() == 0 ? 0 : 1, Live() ? 1 : 0, rows->size());
    DebugLog("[NET] steam friends list: " + N((long long)rows->size()) + " hosting"
             + (shows == steamfriends::kShowList ? std::string() : " (placeholder " + N(shows) + ")"));
    return shows;
}

long SteamFriendsGen() { return g_gen; }

void SteamFriendsRefresh()
{
    ++g_gen;
    if (!Live()) return;
    int count = 0, asked = 0;
    if (!SehCount(g_api.count, g_friends, &count)) { Fault("GetFriendCount"); return; }
    if (count > kFriendsReadCap) count = kFriendsReadCap;
    for (int i = 0; i < count && Live(); ++i)
    {
        unsigned long long id = 0;
        if (!SehByIndex(g_api.byIndex, g_friends, i, &id)) { Fault("GetFriendByIndex"); return; }
        const int played = SehPlayed(g_api.played, g_friends, id);
        if (played < 0) { Fault("GetFriendGamePlayed"); return; }
        if (played == 0) continue;
        if (!SehRequestRP(g_api.requestRP, g_friends, id)) { Fault("RequestFriendRichPresence"); return; }
        ++asked;
    }
    DebugLog("[NET] steam friends: REFRESH - Steam asked again for " + N(asked) + " friend(s) in a game");
}

bool SteamFriendRowNow(unsigned long long id, steamfriends::FriendRow* out)
{
    if (g_fake > 0)
    {
        const std::vector<steamfriends::FriendRow> v = steamfriends::FakeRows(g_fake);
        for (size_t i = 0; i < v.size(); ++i) if (v[i].id == id) { if (out != 0) *out = v[i]; return true; }
        return false;
    }
    if (!Live() || id == 0) return false;
    steamfriends::FriendRow r;
    if (!ReadRow(id, &r)) return false;
    if (out != 0) *out = r;
    return true;
}

bool SteamJoinTake(unsigned long long* id, std::string* name, int* from)
{
    if (g_pendFrom < 0) return false;
    if (g_pendFrom == steamfriends::kFromCallback || g_pendFrom == steamfriends::kFromLaunch)
    {   /* before the dial: the friend's presence, read now, must still name that friend's own game */
        if (!steamfriends::ConnectNamesFriend(g_pendId, LiveConnect(g_pendId)))
        {
            DebugLog(std::string("[NET] steam join request (source ") + steamfriends::SourceWord(g_pendFrom) + ") dropped: that friend's presence"
                     " read now does not name the friend's own game (not hosting now, or Steam has not answered); nothing dialled");
            g_pendFrom = -1; g_pendId = 0; g_pendName.clear();
            return false;
        }
    }
    if (id != 0) *id = g_pendId;
    if (name != 0) *name = g_pendName.empty() ? NameOf(g_pendId) : g_pendName;   /* Steam may know the name by now */
    if (from != 0) *from = g_pendFrom;
    g_pendFrom = -1; g_pendId = 0; g_pendName.clear();
    return true;
}

int SteamInviteReady()
{
    if (g_fake > 0) return 1;
    return (Live() && g_self != 0 && g_api.state != 0 && g_api.invite != 0) ? 1 : 0;
}

void SteamInviteRows(std::vector<steamfriends::InviteRow>* rows)
{
    rows->clear();
    if (g_fake > 0) { *rows = steamfriends::InviteFakeRows(g_fake); return; }
    if (!SteamInviteReady()) return;
    int count = 0;
    if (!SehCount(g_api.count, g_friends, &count)) { Fault("GetFriendCount"); return; }
    if (count > kFriendsReadCap) count = kFriendsReadCap;
    std::vector<steamfriends::InviteRow> found;
    for (int i = 0; i < count && Live(); ++i)
    {
        unsigned long long id = 0;
        if (!SehByIndex(g_api.byIndex, g_friends, i, &id)) { Fault("GetFriendByIndex"); return; }
        if (id == 0) continue;
        int persona = 0;
        if (!SehPersonaState(g_api.state, g_friends, id, &persona)) { Fault("GetFriendPersonaState"); return; }
        if (!steamfriends::InviteListed(persona)) continue;
        unsigned app = 0;
        if (!SehPlayedApp(g_api.played, g_friends, id, &app)) { Fault("GetFriendGamePlayed"); return; }
        char name[129];
        if (!SehPersona(g_api.persona, g_friends, id, name, sizeof(name))) { Fault("GetFriendPersonaName"); return; }
        steamfriends::InviteRow r;
        r.id = id;
        r.name = name;
        r.status = steamfriends::InviteStatusOf(persona, app);
        found.push_back(r);
    }
    *rows = steamfriends::InviteOrdered(found);
}

int SteamInviteSend(const steamfriends::InviteRow& r, int row)
{
    if (r.sample != 0)
    {
        DebugLog("[NET] steam invite: TEST row " + N(row) + " - not sent (sample row)");
        return 2;
    }
    const char* why = 0;
    int ok = 0;
    if (!Live()) why = "Steam is not running";
    else if (g_self == 0) why = "this player's Steam id was not read";
    else if (g_api.invite == 0) why = "Steam's invite call was not found";
    else if (r.id == 0) why = "no friend on that row";
    else
    {
        const std::string connect = steamfriends::ConnectString(g_self);   /* the connect string the host's rich presence sets */
        if (!SehInvite(g_api.invite, g_friends, r.id, connect.c_str(), &ok)) { Fault("InviteUserToGame"); why = "a Steam call faulted"; }
        else if (ok == 0) why = "Steam refused it";
    }
    if (why != 0)
    {
        DebugLog("[NET] steam invite: FAILED (" + std::string(why) + ")");
        return 0;
    }
    DebugLog("[NET] steam invite: sent to a friend (row " + N(row) + ")");
    return 1;
}

std::string SteamFriendsLever(const std::string& args)
{
    int n = 0;
    if (!steamfriends::FakeArgs(args, &n))
    {
        DebugLog("[cmd] usage: steamfriends fake <1-8> | off");
        return "error steamfriends usage";
    }
    g_fake = n;
    ++g_gen;
    DebugLog("[NET] TEST-ONLY steamfriends: " + (n > 0 ? "JOIN GAME's friends list and INVITE FRIENDS show " + N(n) + " sample row(s)" : std::string("Steam's own list again")));
    return n > 0 ? "ok steamfriends fake " + N(n) : std::string("ok steamfriends off");
}

std::string SteamJoinLever(const std::string& args)
{
    std::string connect;
    if (!steamfriends::LeverConnect(args, &connect))
    {
        DebugLog("[cmd] usage: steamjoin [" + std::string(steamfriends::kConnectVerb) + "] steam:<id>");
        return "error steamjoin usage";
    }
    DebugLog("[NET] TEST-ONLY steamjoin: the join-request handler runs as for Steam's callback 337");
    HandleJoin(connect, steamfriends::kFromLever, 0);
    return "ok steamjoin";
}

}   /* namespace coop */
