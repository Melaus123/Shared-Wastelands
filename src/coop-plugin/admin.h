// admin.h - THE ADMIN LOGIN AND THE ADMIN COMMANDS, THIS GAME'S END. The pure rules are src/common/adminauth.h (the login, its
// proof, the 103 wire, this game's admin standing on the 101 wire), src/common/admincmd.h (the typed commands, the 97 / 100 wire)
// and src/common/adminwords.h (the lines this game says, and its standing as the world server last gave it). The world server
// (src/coop-store/store_main.cpp OnAdminLogin, OnAdminReq) decides who is an admin now; this file asks it and says what it
// answered, in the chat of this player only.
#pragma once

#include <string>
#include <vector>

namespace coop {

// The chat's typing line was sent with a line starting with '/' (chat.cpp SendFromBox): taken here, never sent as chat. /login,
// /logout, /help and the admin commands; any other slash line is answered with words. A /login line's password is kept only until
// its proof is made (or its challenge is late) and wiped; *typed is wiped here when it is a /login line. MAIN THREAD.
void AdminTyped(std::string* typed);

// ADMIN_LOGIN (103: CHALLENGE, RESULT), ADMIN_STANDING (101) and ADMIN_REPLY (100) from the world server (store.cpp's drain).
// MAIN THREAD.
void AdminArrive(unsigned type, const std::vector<char>& payload);

// Every in-world and title frame (coop.cpp): a typed password whose challenge never came is wiped; a link that ended ends this
// game's admin standing (the world server ends a login with its connection). MAIN THREAD.
void AdminTick();

// THIS GAME IS AN ADMIN NOW (decision 1294 a): the world server's last ADMIN_STANDING on this link says owner (its connection
// proved this hosting's secret, HOST_TOKEN) or a logged-in admin. A new link is not an admin until its first standing. Only such a
// game sets the world's rules (settings.cpp RulesAuthority). MAIN THREAD.
bool AdminNow();

// One admin command up, for a button (the PLAYERS tab's MAKE / REMOVE ADMIN, the MULTIPLAYER window's admin password):
// verb = admincmd::Verb, flags = admincmd::kFlagSlot when target is a slot number. The answer comes as this player's chat line.
// `extra` (the new password for kVerbSetPassword) is wiped here once sent. 1 = sent, 0 = not linked, -1 = the request would not
// encode (a text too long). MAIN THREAD.
int AdminRequest(unsigned verb, const std::string& target, unsigned flags, std::string* extra);

// THIS GAME IS THE OWNER'S OWN GAME NOW: the world server's last ADMIN_STANDING on this link says owner. Only then are the MULTIPLAYER
// window's ADMIN PASSWORD part and the PLAYERS tab's admin row shown. MAIN THREAD.
bool AdminOwnerNow();

// The MULTIPLAYER window's DONE with the ADMIN PASSWORD box's text (and the TEST-ONLY lever's sample, the same road): *pw is wiped
// here in every case and never logged. adminwords::kPwKeep (empty: nothing sent), kPwShort (under 8 characters: nothing sent, the
// window says so), kPwBad (nothing sent), kPwSend (sent; not linked: said in the chat). MAIN THREAD.
int AdminSetPassword(std::string* pw, const char* from);

// The PLAYERS tab's MAKE ADMIN (make) / REMOVE ADMIN for the player in `slot`, sent at once; the world server's answer is said in the
// chat (not linked: said here). 1 = sent. MAIN THREAD.
int AdminListChange(int slot, bool make, const char* from);

// TEST-ONLY `admintest` (command_channel.cpp; the forms are admincmd.h's TestParse): "ok admintest .." or "error admintest ..". MAIN THREAD.
std::string AdminTestCommand(const std::string& args);

}
