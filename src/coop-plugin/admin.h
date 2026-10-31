// admin.h - THE ADMIN'S TOYCHEST v2 (--dedicated only): /medic /heal /restore /revive /god /bring /goto /tp /where /jail /sethome /home
// /save typed in the chat, and /here for every player. The rules, the wire and the road: src/common/admincmd.h. The world server judges who may (its admins) and logs every use; this file is both ends on
// the game: the admin's request, and the owner's game doing the work on its own characters.
#pragma once

#include <string>
#include <vector>

namespace coop {

// A line typed in the chat window that starts with '/' (chat.cpp SendFromBox): parsed, and sent to the world server as ADMIN_REQ
// (for /bring with this game's lead character's position). A typo, /help, or no link is answered at once with a grey line.
// MAIN THREAD.
void AdminTyped(const std::string& line);

// ADMIN TWO-FACTOR (src/common/adminauth.h): "/login <password>" or "/logout" typed in the chat window, taken by chat.cpp SendFromBox
// before any chat or command road, on every world. The password stays in this game: it is held only until the world server's
// challenge arrives (or kChallengeSec), then turned into a one-time proof and wiped. *line is wiped here. MAIN THREAD.
void AdminLoginTyped(std::string* line);

// ADMIN_DO (a job for this game's own characters), ADMIN_REPLY (the answer to this admin's command; req 0 = a notice to every player)
// or GOD_TABLE (every god character in the world) from the world server (store.cpp's drain): noted only; AdminTick acts. MAIN THREAD.
void AdminArrive(unsigned type, const std::vector<char>& payload);

// GOD MODE. ANY THREAD (the AI worker's detours - peace.cpp, medical.cpp, clothing.cpp): this character (own or a copy) is in the
// world server's god table - a pointer compare against a fixed table AdminTick rewrites every frame, never followed. The count is
// one load, for a cheap first test.
bool AdminGodAnyThread(const void* character);
int AdminGodCountAnyThread();
std::string AdminGodTokens();   // " godTable[...]": the table's size here and the owner's keep-whole counters

// The K2 safe point (combat.cpp, the AI worker paused): an /additem's books made and added there. MAIN THREAD.
void AdminSafePointDrain();

// Every frame of the in-world pump (coop.cpp, beside MoneyTick): the replies shown, the jobs done while the engine takes writes
// (a healed character's stand-up waited for), each answered with ADMIN_DONE. MAIN THREAD.
void AdminTick();

// the toychest's state dropped - a world teardown (linkLost false: the jobs and an /additem in flight; the world
// server times them out) or the world-server link going down (linkLost true: the god table, the replies and the requests too, so a
// god row never outlives its world). MAIN THREAD (store.cpp, beside TeamForget).
void AdminForget(const char* why, bool linkLost);

}   // namespace coop
