/* src/common/adminwords.h - WHAT THE GAME SAYS ABOUT ADMIN LOGINS AND ADMIN COMMANDS, AND ITS ADMIN STANDING. Pure: no
   Windows.h, no clock of its own (every call takes the time in milliseconds), no engine; the game plugin (admin.cpp) and the
   offline suite compile it. C++03 (VS2010 v100).

   THE WORDS: every line goes to the chat of the player who typed the command only, grey (as "Ann joined the world.").
   Approved lines: build/mockups-t1158.md screen 4 (login, logout, refusal, lockout), build/mockups-t1159.md screen 12 (the
   command answers) and screen 11 ("Ben is not an admin."), mockups-t1158.md screen 3 ("At least 8 characters."), and mockups-t1159.md screen 19
   (the remaining login, command-answer and typing-help lines).

   THIS GAME'S STANDING (Standing): the world server decides who is an admin now (adminauth.h) and tells each game its own standing
   in ADMIN_STANDING - at the WELCOME and at each change it makes (a login taken, /logout, the idle end, the admin password set, the
   player taken off the admin list). The game keeps the last word of its link and no clock of its own: a new link is none until its
   first standing comes. */
#ifndef SW_ADMINWORDS_H
#define SW_ADMINWORDS_H

#include <string>
#include <cstdio>
#include "adminauth.h"
#include "admincmd.h"

namespace adminwords {

/* ---- the login's answers (adminauth RESULT codes) ---- */
inline unsigned WaitMinutes(unsigned waitSec) { return waitSec == 0 ? 1u : (waitSec + 59u) / 60u; }
inline std::string LockedLine(unsigned waitSec)
{
    const unsigned m = WaitMinutes(waitSec);
    char b[24]; std::sprintf(b, "%u", m);
    return std::string("Too many wrong admin passwords. Try again in ") + b + (m == 1 ? " minute." : " minutes.");
}
inline std::string ResultLine(unsigned code, unsigned waitSec)
{
    switch (code)
    {
    case adminauth::kResOk:          return "You're now an admin in this world.";
    case adminauth::kResNotAdmin:    return "You're not an admin in this world.";
    case adminauth::kResWrong:       return "Wrong admin password.";
    case adminauth::kResLocked:      return LockedLine(waitSec);
    case adminauth::kResLoggedOut:   return "You're no longer an admin.";
    case adminauth::kResNoPassword:  return "No admin password is set for this world yet.";
    case adminauth::kResNoChallenge: return "The login took too long. Type /login again.";
    case adminauth::kResBusy:        return "Too many logins at once. Wait a moment and try again.";
    case adminauth::kResOwnerNoNeed: return "You're the owner of this world, so you don't need to log in.";
    default:                         return std::string();
    }
}
/* a logged-in admin is one no longer, for any reason but its own /logout (the same words as the /logout's RESULT) */
inline std::string IdleEndLine() { return "You're no longer an admin."; }

/* ---- the command answers (admincmd ADMIN_REPLY) ---- */
inline std::string ShownName(const std::string& n) { return n.empty() ? std::string("That player") : n; }
/* the line for one reply; empty = no line (a /stop that worked: every player gets the DISCONNECTED box) */
inline std::string ReplyLine(const admincmd::Reply& r)
{
    const std::string n = ShownName(r.name);
    switch (r.code)
    {
    case admincmd::kRepOk:
        switch (r.verb)
        {
        case admincmd::kVerbKick:         return n + " was removed from the world.";
        case admincmd::kVerbBan:          return n + " is banned from this world.";
        case admincmd::kVerbUnban:        return n + " is no longer banned.";
        case admincmd::kVerbMakeAdmin:    return n + " is now an admin.";
        case admincmd::kVerbRemoveAdmin:  return n + " is no longer an admin.";
        case admincmd::kVerbSetPassword:  return "The admin password is set.";
        default:                          return std::string();
        }
    case admincmd::kRepNotAdmin:      return "Only an admin can do that.";
    case admincmd::kRepOwnerOnly:     return "Only the owner can do that.";
    case admincmd::kRepNoSuchPlayer:  return "No player by that name.";
    case admincmd::kRepNotBanned:     return "No player by that name.";
    case admincmd::kRepManyPlayers:   return "More than one player is called " + n + ". Nothing was done.";
    case admincmd::kRepNotYourself:   return "You can't do that to yourself.";
    case admincmd::kRepProtected:     return "Only the owner can do that to an admin.";
    case admincmd::kRepWriteFailed:
    case admincmd::kRepUnreadable:    return "The world server couldn't save the change, so nothing changed.";
    case admincmd::kRepAlready:
        if (r.verb == admincmd::kVerbBan) return n + " is banned from this world.";   /* already banned - and removed now */
        if (r.verb == admincmd::kVerbMakeAdmin) return n + " is already an admin.";
        return std::string();
    case admincmd::kRepPasswordShort: return "At least 8 characters.";
    case admincmd::kRepBadRequest:    return "The world server didn't understand that command.";
    case admincmd::kRepNotListed:     return n + " is not an admin.";
    case admincmd::kRepNoId:          return n + "'s game has no player ID the world can remember, so nothing changed.";
    default:                          return std::string();
    }
}

/* ---- the typed line's own answers (before anything is sent) ---- */
inline std::string UsageLine(int verb)
{
    switch (verb)
    {
    case admincmd::kVerbKick:  return "Type it like this: /kick <player name>";
    case admincmd::kVerbBan:   return "Type it like this: /ban <player name> (a reason may follow the name)";
    case admincmd::kVerbUnban: return "Type it like this: /unban <player name>";
    case admincmd::kVerbStop:  return "Type it like this: /stop";
    default:                   return std::string();
    }
}
/* admincmd::Parse's answer as a line; empty = kParseOk (nothing to say) */
inline std::string ParseLine(int parse, int verb)
{
    if (parse == admincmd::kParseOk || parse == admincmd::kParseNotCommand) return std::string();
    if (parse == admincmd::kParseUnknown) return "Unknown command. Type /help to see the commands.";
    return UsageLine(verb);
}
inline std::string HelpLine() { return "Commands: /login <password>, /logout, /kick <player name>, /ban <player name>, /unban <player name>, /stop"; }
inline std::string LoginNoPasswordLine() { return "Type it like this: /login <password>"; }
inline std::string NotLinkedLine() { return "You're not in a multiplayer world."; }
/* /logout's answer on this game for the standing it holds now; empty = there is a login to end, so the /logout goes up */
inline std::string LogoutLine(unsigned standing)
{
    if (standing == (unsigned)adminauth::kStandOwner) return "You're the owner of this world, so you're always an admin.";
    if (standing != (unsigned)adminauth::kStandAdmin) return "You're not logged in as an admin.";
    return std::string();
}

/* ---- the /login handshake ---- */
const unsigned kPendingMs = (unsigned)((adminauth::kChallengeSec + 5.0) * 1000.0);   /* a typed password is kept no longer than this for its CHALLENGE */
inline bool PendingStale(unsigned askMs, unsigned nowMs) { return (unsigned)(nowMs - askMs) >= kPendingMs; }

/* ---- this game's standing: the world server's last word on this link (adminauth ADMIN_STANDING) ---- */
struct Standing { int gen; unsigned standing; Standing() : gen(-1), standing((unsigned)adminauth::kStandNone) {} };
/* the standing on the link `linkGen`: the server's last word on that link, none before its first */
inline unsigned StandingNow(const Standing& s, int linkGen) { return (s.gen >= 0 && s.gen == linkGen) ? s.standing : (unsigned)adminauth::kStandNone; }
/* an admin now: the owner's own game, or a logged-in admin */
inline bool AdminBy(unsigned standing) { return standing == (unsigned)adminauth::kStandAdmin || standing == (unsigned)adminauth::kStandOwner; }
/* the server's `standing` for `why`, taken on the link `linkGen`. *line = IdleEndLine() when a logged-in admin is one no longer for any
   reason but its own /logout - by this standing, or by a link that ended before this game saw it end; else empty. True = this game's
   "admin now" changed. */
inline bool StandingTake(Standing* s, int linkGen, unsigned standing, unsigned why, std::string* line)
{
    line->clear();
    const bool endedUnseen = s->gen >= 0 && s->gen != linkGen && s->standing == (unsigned)adminauth::kStandAdmin;
    const unsigned was = StandingNow(*s, linkGen);
    s->gen = linkGen; s->standing = standing;
    if (endedUnseen || (was == (unsigned)adminauth::kStandAdmin && standing == (unsigned)adminauth::kStandNone && why != (unsigned)adminauth::kWhyLogout))
        *line = IdleEndLine();
    return AdminBy(was) != AdminBy(standing);
}
/* the link the standing came on ended (the world server ends a login with its connection): none now, *line as StandingTake.
   True = this game's "admin now" changed. */
inline bool StandingLinkEnd(Standing* s, std::string* line)
{
    line->clear();
    const bool was = s->gen >= 0 && AdminBy(s->standing);
    if (s->gen >= 0 && s->standing == (unsigned)adminauth::kStandAdmin) *line = IdleEndLine();
    s->gen = -1; s->standing = (unsigned)adminauth::kStandNone;
    return was;
}

/* ---- the PLAYERS tab's admin row, on the owner's own game (mockups-t1159.md screen 11): the picked player's line and its button ---- */
inline std::string AdminRowLine(const std::string& name, bool listed) { return ShownName(name) + (listed ? " is an admin." : " is not an admin."); }
inline const char* AdminRowButton(bool listed) { return listed ? "REMOVE ADMIN" : "MAKE ADMIN"; }
inline int AdminRowVerb(bool listed) { return listed ? (int)admincmd::kVerbRemoveAdmin : (int)admincmd::kVerbMakeAdmin; }

/* ---- the MULTIPLAYER window's ADMIN PASSWORD part, on the owner's own game (mockups-t1158.md screen 3) ---- */
inline const char* PasswordHead() { return "ADMIN PASSWORD"; }
inline const char* PasswordHint() { return "Admins type /login and this password in chat."; }
inline const char* PasswordShortLine() { return "At least 8 characters."; }
inline const char* PasswordShowWord() { return "SHOW"; }
/* the box's limit in characters: any this many characters fit the server's byte limit (a character is at most 4 bytes) */
const size_t kPasswordBoxChars = adminauth::kPasswordMaxBytes / 4;
enum { kPwKeep = 0, kPwShort = 1, kPwSend = 2, kPwBad = 3, kPwNotSent = 4 };   /* kPwNotSent: AdminSetPassword's answer when it was not sent */
/* the box's text as /login takes it: the spaces at both ends dropped (adminauth::LoginPassword), so the password set is the one typed */
inline void PasswordTrim(std::string* pw)
{
    size_t b = pw->size(); while (b > 0 && adminauth::Space((*pw)[b - 1])) --b;
    size_t a = 0; while (a < b && adminauth::Space((*pw)[a])) ++a;
    pw->resize(b);
    pw->erase(0, a);
}
/* DONE with `typed` in the box: kPwKeep = an empty box (the password is left as it is), kPwShort = under 8 characters (the line under
   the box says so and the window stays open), kPwSend = goes to the world server, kPwBad = a control byte or too many bytes (the
   box's own limits keep typing from making one) */
inline int PasswordDone(const std::string& typed)
{
    if (typed.empty()) return kPwKeep;
    if (adminauth::CharCount(typed) < adminauth::kPasswordMinChars) return kPwShort;
    return adminauth::PasswordLongEnough(typed) ? kPwSend : kPwBad;
}

}   /* namespace adminwords */

#endif
