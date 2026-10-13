/* net/steam_friends.h - Steam friends find and join a host: this game's rich presence while it hosts, JOIN GAME's friends list, and
   the join requests from Steam (its "Join Game" / an accepted invite, while the game runs or on the command line it was started with).
   MAIN THREAD only (the title pump and the main-loop pump). The decisions are src/common/steamfriends.h. */
#ifndef COOP_NET_STEAM_FRIENDS_H
#define COOP_NET_STEAM_FRIENDS_H

#include <string>
#include <vector>
#include "../../common/steamfriends.h"

namespace coop {

/* Once per frame, after the Steam callback pump (SteamProbeTick): looks up ISteamFriends once Steam is found, keeps this game's rich
   presence equal to its hosting state, reads the launch command line once, and turns Steam's join requests into a pending join. */
void SteamFriendsTick();

/* The friends list as JOIN GAME shows it, read now (one log line, the count only): the rows, and what shows (steamfriends::kShow*). */
int  SteamFriendsShows(std::vector<steamfriends::FriendRow>* rows);
long SteamFriendsGen();      /* moves when the list should be read again: Steam said a friend's presence changed, REFRESH, the lever */
void SteamFriendsRefresh();  /* REFRESH: Steam is asked again for each friend in a game; the list is read again now */
/* One friend's row read from Steam now (the live read at JOIN). false = that friend hosts nothing this game can read now. */
bool SteamFriendRowNow(unsigned long long id, steamfriends::FriendRow* out);
/* A join request waiting for the title screen: taken once. */
bool SteamJoinTake(unsigned long long* id, std::string* name, int* from);

/* INVITE FRIENDS (over HOSTING). Ready: Steam runs, this player's own id is known and Steam's two invite calls were found - or the
   TEST-ONLY sample rows are on. Rows: the friends online now (steamfriends::InviteListed), in the list's order (no log line here).
   Send: a Steam game invite carrying this game's connect string (the one its rich presence sets) to the row's friend; 1 sent,
   2 a sample row (not sent), 0 failed - each with one [NET] line naming the list's row number `row`, never an id or a name. */
int  SteamInviteReady();
void SteamInviteRows(std::vector<steamfriends::InviteRow>* rows);
int  SteamInviteSend(const steamfriends::InviteRow& r, int row);

/* TEST-ONLY levers. `steamfriends fake <n> | off`; `steamjoin <connect string> | steam:<id>` runs the join-request handler with that connect string. The
   answer is the command channel's status word. */
std::string SteamFriendsLever(const std::string& args);
std::string SteamJoinLever(const std::string& args);

}   /* namespace coop */

#endif
