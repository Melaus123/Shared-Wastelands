/* src/common/steamfriends.h - STEAM FRIENDS FIND AND JOIN A HOST: THE DECISIONS. PURE: no Windows.h, no Steam, no globals, no I/O, so
   the offline suite checks exactly what the plugin compiles.

   A hosting game sets Steam rich presence keys (what a friend's game reads from Steam about this player): "connect" (what Steam's own
   friends-list "Join Game" hands to the friend's game, on its command line or in its join-request callback), the world's name, the
   player count, whether a password is set, and the game-to-game protocol number. JOIN GAME lists the friends whose keys say they host;
   a row whose protocol differs from this game's says "(different version)" and cannot be joined. This file holds the connect string's
   build and parse, the keys for a host state, a friend's row from its keys, the cells and lines the screen shows, the placeholder line's
   choice, and the TEST-ONLY sample rows. */
#ifndef SW_STEAMFRIENDS_H
#define SW_STEAMFRIENDS_H

#include <string>
#include <vector>
#include <utility>
#include <cstddef>

#include "relaybridge.h"   /* the steam:<id> address form: SteamAddressText / SteamAddressId */

namespace steamfriends {

/* ---- the connect string: "+mpjoin steam:<id>" - Steam passes it on the game's command line when it starts the game from a Join or an
   invite, and in the join-request callback while the game runs ---- */
const char* const kConnectVerb = "+mpjoin";
inline std::string ConnectString(unsigned long long hostId)
{
    if (hostId == 0) return std::string();
    return std::string(kConnectVerb) + " " + relaybridge::SteamAddressText(hostId);
}
/* The verb anywhere in `s` (a whole command line or the callback's text), as its own word, then spaces or tabs, then a steam:<id> word
   ending at a space, a tab, a quote or the end. *hostId is written only on a yes. */
inline bool ParseConnect(const std::string& s, unsigned long long* hostId)
{
    const std::string verb(kConnectVerb);
    size_t from = 0;
    while (from < s.size())
    {
        const size_t at = s.find(verb, from);
        if (at == std::string::npos) return false;
        from = at + 1;
        if (at > 0 && s[at - 1] != ' ' && s[at - 1] != '\t' && s[at - 1] != '"') continue;
        size_t i = at + verb.size();
        if (i >= s.size() || (s[i] != ' ' && s[i] != '\t')) continue;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '"') ++j;
        unsigned long long id = 0;
        if (!relaybridge::SteamAddressId(s.substr(i, j - i), &id)) continue;
        if (hostId != 0) *hostId = id;
        return true;
    }
    return false;
}

/* A connect string that names `friendId`'s own game: a friend hosts only its own game, so a request or a row whose connect names any
   other id (or none) is not that friend's game. */
inline bool ConnectNamesFriend(unsigned long long friendId, const std::string& connect)
{
    unsigned long long id = 0;
    return friendId != 0 && ParseConnect(connect, &id) && id == friendId;
}

/* ---- the host's rich presence keys ---- */
const char* const kKeyConnect  = "connect";
const char* const kKeyWorld    = "mp_world";
const char* const kKeyPlayers  = "mp_players";
const char* const kKeyPassword = "mp_password";
const char* const kKeyProtocol = "mp_protocol";
const size_t kValueMax = 255;   /* Steam's longest rich presence value */

inline std::string Num(unsigned long long v)
{
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + (int)(v % 10ULL)); v /= 10ULL; } while (v != 0 && n < 23);
    std::string s;
    while (n > 0) s.push_back(tmp[--n]);
    return s;
}
/* A whole decimal value of 1-9 digits; -1 = anything else. */
inline long long ReadNum(const std::string& s)
{
    if (s.empty() || s.size() > 9) return -1;
    long long v = 0;
    for (size_t i = 0; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + (s[i] - '0'); }
    return v;
}

struct HostState
{
    int hosting;               /* 1 = this game hosts a world now */
    unsigned long long selfId; /* this player's own Steam id (0 = unknown: no connect key, so no key at all) */
    std::string world;         /* the world's name as the HOSTING screen shows it */
    int players;               /* players in the world, the host included (< 1 = unknown, said as 1) */
    int password;              /* 1 = the world needs a password */
    unsigned protocol;         /* the game-to-game protocol */
    HostState() : hosting(0), selfId(0), players(0), password(0), protocol(0) {}
};
typedef std::vector<std::pair<std::string, std::string> > KeyList;
/* The keys a host state sets, in a fixed order; none (= clear them all) when not hosting or the own id is unknown. */
inline KeyList PresenceKeys(const HostState& h)
{
    KeyList k;
    if (h.hosting == 0 || h.selfId == 0) return k;
    std::string w = h.world.size() > kValueMax ? h.world.substr(0, kValueMax) : h.world;
    k.push_back(std::make_pair(std::string(kKeyConnect), ConnectString(h.selfId)));
    k.push_back(std::make_pair(std::string(kKeyWorld), w));
    k.push_back(std::make_pair(std::string(kKeyPlayers), Num((unsigned long long)(h.players < 1 ? 1 : h.players))));
    k.push_back(std::make_pair(std::string(kKeyPassword), std::string(h.password != 0 ? "1" : "0")));
    k.push_back(std::make_pair(std::string(kKeyProtocol), Num((unsigned long long)h.protocol)));
    return k;
}
/* The keys for a log line: every key and value except connect's id (the key is named, its value is not). */
inline std::string KeysForLog(const KeyList& k)
{
    if (k.empty()) return "none (cleared)";
    std::string s;
    for (size_t i = 0; i < k.size(); ++i)
    {
        if (!s.empty()) s += " ";
        s += k[i].first + "=" + (k[i].first == kKeyConnect ? std::string("<set>") : "'" + k[i].second + "'");
    }
    return s;
}

/* ---- a friend's row ---- */
struct FriendRow
{
    unsigned long long id;   /* the friend's Steam id - what JOIN dials (never logged, never shown) */
    std::string name;        /* the friend's Steam name */
    std::string world;
    int players;
    int password;            /* 1 = Yes */
    int sameVersion;         /* 1 = the friend's protocol is this game's */
    FriendRow() : id(0), players(0), password(0), sameVersion(0) {}
};
/* A friend's row from its keys: yes only when connect names this friend's own id (a friend hosts only its own game) and the protocol
   key is a number. A missing or bad player count reads as 1. */
inline bool RowFromKeys(unsigned long long friendId, const std::string& name, const std::string& connect, const std::string& world,
                        const std::string& players, const std::string& password, const std::string& protocol, unsigned ourProtocol,
                        FriendRow* out)
{
    if (!ConnectNamesFriend(friendId, connect)) return false;
    const long long proto = ReadNum(protocol);
    if (proto < 0) return false;
    if (out == 0) return true;
    out->id = friendId;
    out->name = name;
    out->world = world;
    const long long p = ReadNum(players);
    out->players = p < 1 ? 1 : (p > 999 ? 999 : (int)p);
    out->password = password == "1" ? 1 : 0;
    out->sameVersion = (unsigned long long)proto == (unsigned long long)ourProtocol ? 1 : 0;
    return true;
}

/* ---- what the screen shows ---- */
/* MyGUI reads '#' as the start of a colour tag: a '#' from a friend's name or world is shown as "##" (one '#' on screen). */
inline std::string ScreenText(const std::string& plain)
{
    std::string out;
    for (size_t i = 0; i < plain.size(); ++i) { if (plain[i] == '#') out += '#'; out += plain[i]; }
    return out;
}
inline std::string WorldCell(const FriendRow& r) { return ScreenText(r.world) + (r.sameVersion != 0 ? "" : " (different version)"); }
inline std::string PlayersCell(const FriendRow& r) { return Num((unsigned long long)(r.players < 1 ? 1 : r.players)); }
inline const char* PasswordCell(const FriendRow& r) { return r.password != 0 ? "Yes" : "No"; }
/* The joining line while a friend's game is joined: these words only, whether or not the friend's name is known (never a name, never an
   id); a typed address keeps its own line (`typedLine`, panelstatus.h PressStatusText). `friendJoin` 1 = the join dials a friend's game. */
const char* const kFriendConnectingText = "Connecting...";
inline std::string PressLineText(int friendJoin, const std::string& typedLine)
{
    return friendJoin != 0 ? std::string(kFriendConnectingText) : typedLine;
}
/* CAN'T JOIN when a Join, an invite or a launch request arrives while this game hosts or is in a world (hosting is being in a world).
   "" = the friend's name is not known. */
inline std::string InWorldRefuseText(const std::string& name)
{
    if (name.empty()) return "You can't join that game while you are in a world.";
    return "You can't join " + ScreenText(name) + "'s game while you are in a world.";
}
/* The IP ADDRESS box is pre-filled only with an address the player can read and type: never a steam:<id> (a friend's game is picked from
   the list, and a Steam id is never shown). */
inline bool JoinBoxPrefillOk(const std::string& addr) { return !addr.empty() && !relaybridge::IsSteamAddress(addr); }

/* The line in place of the list: 0 = the list itself (rows to show). */
enum { kShowList = 0, kShowNoSteamApi = 1, kShowSteamNotRunning = 2, kShowNobodyHosting = 3 };
/* `steamApi` 1 = this copy of Kenshi has Steam's DLL; `steamRunning` 1 = signed in to Steam with no Steam call faulted; `rows` the
   rows found. */
inline int ListShows(int steamApi, int steamRunning, size_t rows)
{
    if (steamApi == 0) return kShowNoSteamApi;
    if (steamRunning == 0) return kShowSteamNotRunning;
    return rows == 0 ? kShowNobodyHosting : kShowList;
}
inline const char* ListLine(int shows)
{
    switch (shows)
    {
        case kShowNoSteamApi:      return "Friends' games are shown only in the Steam version of Kenshi.";
        case kShowSteamNotRunning: return "Steam isn't running, so friends' games can't be shown.";
        case kShowNobodyHosting:   return "No Steam friends are hosting right now.";
        default:                   return "";
    }
}

/* ---- where a join request came from (the log's word) ---- */
enum { kFromList = 0, kFromCallback = 1, kFromLaunch = 2, kFromLever = 3 };
inline const char* SourceWord(int from)
{
    return from == kFromCallback ? "callback" : from == kFromLaunch ? "launch" : from == kFromLever ? "lever" : "list";
}

/* ---- TEST-ONLY: `steamfriends fake <n>` - n fixed sample rows (1-8), the first with a password, the second a different version, so
   the screen can be shown on a bench with one Steam account. The ids are made up (a dial to one finds no host). ---- */
const int kFakeMax = 8;
inline std::vector<FriendRow> FakeRows(int n)
{
    static const char* const kNames[kFakeMax]  = { "Scott", "Alex", "Jordan", "Riley", "Casey", "Morgan", "Taylor", "Jamie" };
    static const char* const kWorlds[kFakeMax] = { "Dustlands", "Hub Run", "The Swamp", "Shek Kingdom", "Black Desert", "Okran's Pride",
                                                   "Vain", "Border Zone" };
    static const int kPlayers[kFakeMax] = { 3, 1, 2, 5, 1, 4, 2, 1 };
    std::vector<FriendRow> v;
    if (n > kFakeMax) n = kFakeMax;
    for (int i = 0; i < n; ++i)
    {
        FriendRow r;
        r.id = 76561190000000001ULL + (unsigned long long)i;
        r.name = kNames[i];
        r.world = kWorlds[i];
        r.players = kPlayers[i];
        r.password = (i == 0) ? 1 : 0;
        r.sameVersion = (i == 1) ? 0 : 1;
        v.push_back(r);
    }
    return v;
}
/* `steamfriends fake <n> | off`: *n = 1-8 for fake, 0 for off; false = anything else. */
inline bool FakeArgs(const std::string& args, int* n)
{
    std::string a = args;
    while (!a.empty() && (a[0] == ' ' || a[0] == '\t')) a.erase(0, 1);
    while (!a.empty() && (a[a.size() - 1] == ' ' || a[a.size() - 1] == '\t' || a[a.size() - 1] == '\r')) a.erase(a.size() - 1);
    if (a == "off") { if (n != 0) *n = 0; return true; }
    if (a.compare(0, 5, "fake ") != 0) return false;
    std::string c = a.substr(5);
    while (!c.empty() && c[0] == ' ') c.erase(0, 1);
    const long long v = ReadNum(c);
    if (v < 1 || v > kFakeMax) return false;
    if (n != 0) *n = (int)v;
    return true;
}

/* ---- INVITE FRIENDS (over HOSTING): the friends who are online, what each is doing, and the invite's lines ---- */
const unsigned kKenshiAppId = 233860;   /* Kenshi's Steam app id: a friend in this game reads "Playing Kenshi" */
/* Steam's EPersonaState: 0 offline, 1 online, 2 busy, 3 away, 4 snooze, 5 looking to trade, 6 looking to play, 7 invisible. */
enum { kPersonaOffline = 0, kPersonaAway = 3, kPersonaSnooze = 4, kPersonaInvisible = 7 };
/* Listed while Steam says the friend is online in any way; offline friends (and invisible ones, which read as offline) are not. */
inline bool InviteListed(int persona) { return persona > kPersonaOffline && persona < kPersonaInvisible; }
enum { kInvitePlaying = 0, kInviteOnline = 1, kInviteAway = 2 };
/* A friend's STATUS: `appId` = the app id of the game the friend plays now (0 = none). */
inline int InviteStatusOf(int persona, unsigned appId)
{
    if (appId == kKenshiAppId) return kInvitePlaying;
    return (persona == kPersonaAway || persona == kPersonaSnooze) ? kInviteAway : kInviteOnline;
}
inline const char* InviteStatusText(int status)
{
    return status == kInvitePlaying ? "Playing Kenshi" : status == kInviteAway ? "Away" : "Online";
}
struct InviteRow
{
    unsigned long long id;   /* the friend's Steam id - where the invite goes (never logged, never shown) */
    std::string name;        /* the friend's Steam name */
    int status;              /* kInvite* */
    int sample;              /* 1 = a TEST-ONLY sample row: an invite on it is not sent */
    InviteRow() : id(0), status(kInviteOnline), sample(0) {}
};
/* The list's order: playing Kenshi, then online, then away; Steam's own order within each. */
inline std::vector<InviteRow> InviteOrdered(const std::vector<InviteRow>& in)
{
    std::vector<InviteRow> out;
    for (int s = kInvitePlaying; s <= kInviteAway; ++s)
        for (size_t i = 0; i < in.size(); ++i) if (in[i].status == s) out.push_back(in[i]);
    return out;
}
const char* const kSteamRowOffText  = "Steam isn't running, so friends can only join by address.";
const char* const kInviteNoneText   = "None of your Steam friends are online.";
const char* const kInviteFailedText = "Couldn't send the invite.";
inline std::string InviteSentText(const std::string& name) { return "Invite sent to " + ScreenText(name) + "."; }
/* HOSTING's status line while every joiner place on the Steam route is taken (`full` 1, `cap` the places): the hosting line
   "Hosting." becomes the full line; any other line (not hosting, the world couldn't start) stays as it is. */
inline std::string HostingSteamFullText(const std::string& line, int full, int cap)
{
    if (full == 0 || cap < 1 || line != "Hosting.") return line;
    return "Hosting. " + Num((unsigned long long)cap) + " players have joined through Steam; more can join by address.";
}
/* The steamjoin lever's argument: a whole connect string (`+mpjoin steam:<id>`, as Steam hands it) or the steam:<id> word alone (a
   request line cannot carry '+'). *connect = the connect string the join-request handler reads; false = neither form. */
inline bool LeverConnect(const std::string& args, std::string* connect)
{
    if (ParseConnect(args, 0)) { if (connect != 0) *connect = args; return true; }
    std::string a = args;
    while (!a.empty() && (a[0] == ' ' || a[0] == '\t')) a.erase(0, 1);
    while (!a.empty() && (a[a.size() - 1] == ' ' || a[a.size() - 1] == '\t' || a[a.size() - 1] == '\r')) a.erase(a.size() - 1);
    unsigned long long id = 0;
    if (!relaybridge::SteamAddressId(a, &id) || id == 0) return false;
    if (connect != 0) *connect = ConnectString(id);
    return true;
}
/* TEST-ONLY: `steamfriends fake <n>` gives INVITE FRIENDS n sample rows too - made-up names and ids that are no real account. */
inline std::vector<InviteRow> InviteFakeRows(int n)
{
    static const char* const kNames[kFakeMax] = { "Sam", "Alex", "Jordan", "Riley", "Casey", "Morgan", "Taylor", "Jamie" };
    std::vector<InviteRow> v;
    if (n > kFakeMax) n = kFakeMax;
    for (int i = 0; i < n; ++i)
    {
        InviteRow r;
        r.id = 76561190000000101ULL + (unsigned long long)i;
        r.name = kNames[i];
        r.status = i % 3;   /* Playing Kenshi, Online, Away, then again */
        r.sample = 1;
        v.push_back(r);
    }
    return v;
}

}   /* namespace steamfriends */

#endif
