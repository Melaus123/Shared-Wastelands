/* src/common/authlock.h - TOO MANY WRONG PASSWORDS: the world server's per-player wait. PURE: no Windows.h, no clock of its own (every
   call takes `now` in seconds), so the offline suite checks exactly what the world server compiles.

   A player is keyed by its Steam id when the world server believed one (STEAM_ID), by 'host' for the host's own game (it proved
   HOST_TOKEN), else by the connection's address (none is ever logged). kWrongMax wrong passwords in a row lock that key for kLockSec
   from the last of them; while locked, a password is not even checked. A right password clears the count. The lock ends by itself;
   the count starts again from 0 after it. A count with no lock is forgotten kForgetSec after its last wrong password. */
#ifndef SW_AUTHLOCK_H
#define SW_AUTHLOCK_H

#include <string>
#include <map>

namespace authlock {

const int kWrongMax = 5;
const double kLockSec = 60.0;
const double kForgetSec = 600.0;   /* wrong passwords this old (from the last of them) no longer count */

struct Slot
{
    int wrong;
    double lockedUntil;   /* 0 = not locked */
    double lastWrong;     /* when the last wrong password came */
    Slot() : wrong(0), lockedUntil(0.0), lastWrong(0.0) {}
};

typedef std::map<std::string, Slot> Table;

/* true = locked now. A lock whose time is over is cleared here. */
inline bool IsLocked(Table* t, const std::string& key, double now)
{
    Table::iterator it = t->find(key);
    if (it == t->end() || it->second.lockedUntil <= 0.0) return false;
    if (now < it->second.lockedUntil) return true;
    t->erase(it);
    return false;
}

/* One wrong password; true = it locked the key (this was the kWrongMax-th in a row). */
inline bool NoteWrong(Table* t, const std::string& key, double now)
{
    Slot& s = (*t)[key];
    if (s.wrong > 0 && now - s.lastWrong >= kForgetSec) s.wrong = 0;   /* the earlier ones are too old to count */
    ++s.wrong;
    s.lastWrong = now;
    if (s.wrong >= kWrongMax) { s.lockedUntil = now + kLockSec; return true; }
    return false;
}

inline void NoteRight(Table* t, const std::string& key) { t->erase(key); }

/* Drops every key whose lock has run out, and every unlocked count kForgetSec after its last wrong password (the world server's
   1 Hz tick), so the table holds only live counts and locks. */
inline void Expire(Table* t, double now)
{
    for (Table::iterator it = t->begin(); it != t->end();)
    {
        const bool lockOver = it->second.lockedUntil > 0.0 && now >= it->second.lockedUntil;
        const bool countOld = it->second.lockedUntil <= 0.0 && now - it->second.lastWrong >= kForgetSec;
        if (lockOver || countOld) t->erase(it++);
        else ++it;
    }
}

}   /* namespace authlock */

#endif
