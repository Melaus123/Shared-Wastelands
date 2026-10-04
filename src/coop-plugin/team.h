// team.h - T-546: this game's copy of the world server's player-faction membership table (src/common/teamwire.h).
// The world server is the only writer; this game keeps the last table it was sent, its own waiting invitation, and answers each
// RESTORE row it receives once its world is loaded.
// What membership does here (step 4, src/common/teameffect.h): this game's own side towards every teammate is held at ally (the
// pin, through relations.cpp's relate road); the name tags show a teammate in team blue and every player in a team with the
// team's name (tags.cpp, through TeamTagFor); a teammate's buildings open to this game's player whatever the base policy, and
// this game's to them (policy.cpp, through the any-thread team index).
// The shared standing (step 5, src/common/teamstanding.h): the team's standing is ONE record the world server keeps (teamwire.h
// TeamRec); the founder's game seeds it, this game writes its own faction to it at every new record and after every world load,
// and sends its own changes (NPC differences; the founder's stance towards other players) up. The ACCEPT carries this game's
// pre-join standing; its side towards a new teammate as it stood before the pin goes up as a SIDE; a RESTORE row puts both
// sides back before it is answered.
// The shared research (step 6, src/common/teamresearch.h): this game sends its whole finished list once per world load and link
// while its player is in a team, loads the team's whole research it gets back ONCE through the engine's Research::load (its own
// queue kept), applies each tech a teammate finishes through the engine's setResearched, and sends the techs its own player
// finishes (never one applied from a teammate). Leaving or removal takes nothing away.
#pragma once
#include <string>
#include <vector>
namespace coop {
// MAIN THREAD (store.cpp's drain): one TEAM message from the world server - a table write, log lines, and RESTORE_DONE sent back.
void TeamArrive(const std::vector<char>& payload);
// MAIN THREAD, every frame (command_channel.cpp): with this game's world loaded, every held RESTORE row is answered, and the
// table is asked for once when this link has not sent one since the copy was cleared.
void TeamTick();
// MAIN THREAD: the copy of the table cleared - a world teardown (leaving to the title, or another load: linkLost false) or the
// world-server link going down (linkLost true: the held RESTORE rows and the waiting invitation go too; the world server sends
// the rows again at the next join).
void TeamForget(const char* why, bool linkLost);
// TEST-ONLY `team invite <slot> | accept | decline | leave | remove <slot> | disband | show | access <slot> [class] | click <slot>
// [class] | orders <slot> | research finish <techStringID> | research queue <techStringID> | research show [<techStringID>]` (access: the nearest loaded building of that player's
// faction met through its own getters and the hooks - policy.cpp PolicyTeamAccessProbe; click: a real click on it through the
// engine's PlayerInterface::buildingSelected - PolicyTeamClick; research finish: the engine's own setResearched through the hook,
// as this player finishing the tech (refused for a tech queued behind the front); research queue: the engine's own
// Research::addToQueue, as the research screen's "add"; research show: read only - this game's finished count, its queue in
// order with each entry's progress, one tech by stringID); orders: this game's own first character and that player's first
// character through the engine's isEnemy, its interaction menu (the list made and shown) and its right-click handler as a hover -
// policy.cpp PolicyTeamOrders.
// MAIN THREAD. Returns the status.
std::string TeamCommand(const std::string& args);
// ANY THREAD: the team number `slot` is in on this game's copy of the table (0 = none, or no table here). Plain loads of an index
// the main thread rewrites at every table change.
unsigned TeamNoOfSlotAnyThread(int slot);
// ANY THREAD: two different players (slots) share a team on this game's copy of the table.
bool TeamSameAnyThread(int a, int b);
// MAIN THREAD: the slots that share this game's player's team on its copy of the table (none without a table or a team).
std::vector<int> TeamMatesOfMine();
// MAIN THREAD (476): this game's player is a member (not the founder) and `slot` is outside its team - its stance is the founder's.
bool TeamStanceIsFounders(int slot);
// MAIN THREAD (476, either side): the other members of `slot`'s team when this game's player is not in it (else none).
std::vector<int> TeamFanOutTargets(int slot);
// THE PLAYER'S REQUESTS (the faction screens - playerstab.cpp - and the TEST-ONLY lever send the same message): sub = invite
// <slot> | accept | decline | leave | remove <slot> | disband. invite names this game's own faction; accept carries this game's
// standing at that moment; accept / decline answer the waiting invitation (its box is not shown again). MAIN THREAD. Returns
// "ok team <sub> sent" or "error ...".
std::string TeamRequestSend(const std::string& sub, int slot);
// MAIN THREAD: this game's player's place on its copy of the table - 0 in no faction of players (or no table), 1 the founder,
// 2 a member; *teamName the faction's name and *founderSlot its founder (when in one).
int TeamMyRole(std::string* teamName, int* founderSlot);
// MAIN THREAD: `slot` founded the faction of players it is in.
bool TeamIsFounder(int slot);
// MAIN THREAD: a player's name as the faction screens say it - the roster's name now, else the one it gave earlier this
// session; "" when this game knows neither (no made-up name).
std::string TeamPlayerName(int slot);
// MAIN THREAD: the invitation waiting for this game's player and not answered yet - who sent it, the faction's name, and its
// serial (a new number for each invitation received). false = none.
bool TeamInvitationWaiting(int* fromSlot, std::string* teamName, unsigned* serial);
// MAIN THREAD: this game holds the membership table the current link sent (false while the link is down or before its table) -
// only then is what the screens read of the table current.
bool TeamTableKnown();
// MAIN THREAD: this player's teammates changed within teamscreen::kMatesSettleMs - the PLAYERS table waits for the settled values.
bool TeamMatesSettling();
// ANY THREAD: player `slot`'s side towards player `me` moved by the team's own write (the two share a faction, or joined or
// parted within teamscreen::kMateQuietMs) - no notice is shown for it.
bool TeamMateQuietAnyThread(int me, int slot);
// MAIN THREAD: player `slot`'s name tag - *line2 = its faction line (the team's name - the founder's faction name as this game
// shows it - for a player in a team, else `own`), *teammate = that player shares this game's player's team (team blue).
void TeamTagFor(int slot, const std::string& own, std::string* line2, bool* teammate);
}
