/* src/common/adminwords.h - WHAT THE GAME SAYS ABOUT ADMIN LOGINS AND ADMIN COMMANDS, AND ITS ADMIN STANDING. Pure: no
   Windows.h, no clock of its own (every call takes the time in milliseconds), no engine; the game plugin (admin.cpp) and the
   offline suite compile it. C++03 (VS2010 v100).

   THE WORDS: every line goes to the chat of the player who typed the command only, grey (as "Ann joined the world.").
   Approved lines: build/mockups-t1158.md screen 4 (login, logout, refusal, lockout), build/mockups-t1159.md screen 12 (the
   command answers) and screen 11 ("Ben is not an admin."), and mockups-t1159.md screen 19
   (the remaining login, command-answer and typing-help lines); build/mockups-t1205.md sections 2, 3, 4 (/medic, /heal and /restore), 6 to 9
   (/bring, /tp, /goto, /sethome and /home), 10 (/jail), 11 (/additem), 5 (/revive) and 13 (the admin toolbox's /where, /here and its shared
   replies). The jail notice (kRepJailNotice) goes to every player in the world. Section 14 (decision 1400, approved): every count
   reads naturally, one / many (Count) - "1 character" / "5 characters", "1 is dead" / "2 are dead", "1 lost limb grew back" /
   "2 lost limbs grew back", "1 robot limb was left on" / "2 robot limbs were left on", "1 dead character stayed." / "2 dead
   characters stayed.".

   THIS GAME'S STANDING (Standing): the world server decides who is an admin now (adminauth.h) and tells each game its own standing
   in ADMIN_STANDING - at the WELCOME and at each change it makes (a login taken, /logout, the idle end, the admin password set, the
   player taken off the admin list). The game keeps the last word of its link and no clock of its own: a new link is none until its
   first standing comes. */
#ifndef SW_ADMINWORDS_H
#define SW_ADMINWORDS_H

#include <string>
#include <vector>
#include <cstdio>
#include "adminauth.h"
#include "admincmd.h"
#include "admintool.h"
#include "fallentab.h"   /* PlaceCell: "near Squin", "near the Hub" - the FALLEN tab's words for the town near a place */

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
    case adminauth::kResNoPassword:  return "No admin password is set for this world yet. The owner writes it after \"admin password =\" in the world's rules.txt.";   /* owner 1512 a */
    case adminauth::kResNoChallenge: return "The login took too long. Type /login again.";
    case adminauth::kResBusy:        return "Too many logins at once. Wait a moment and try again.";
    case adminauth::kResOwner:       return "You are logged in as the world's owner.";
    default:                         return std::string();
    }
}
/* a logged-in admin is one no longer, for any reason but its own /logout (the same words as the /logout's RESULT) */
inline std::string IdleEndLine() { return "You're no longer an admin."; }

/* ---- the command answers (admincmd ADMIN_REPLY) ---- */
inline std::string ShownName(const std::string& n) { return n.empty() ? std::string("That player") : n; }
/* a map position number as the chat shows it: whole units */
inline std::string Coord(float f) { char b[32]; std::sprintf(b, "%lld", admintool::Whole(f)); return b; }
/* /where's line: "Ana is at -12450, 3210, near Squin." - or "in the open." with no town near */
inline std::string WhereLine(const std::string& who, float x, float z, const std::string& town)
{
    return who + " is at " + Coord(x) + ", " + Coord(z) + ", " + (town.empty() ? std::string("in the open") : swtab::PlaceCell(town)) + ".";
}
/* a name as an admin types it in a hint: quoted when it has a space ("Old Tam"), or an @ or # (which would otherwise split the name
   or pick player / character - admintool::ParseName reads a quoted part whole) */
inline std::string TypedName(const std::string& n) { return n.find_first_of(" @#") == std::string::npos ? n : "\"" + n + "\""; }
inline std::string Num(unsigned v) { char b[16]; std::sprintf(b, "%u", v); return b; }
/* "Ana's and Ben's" - the players whose characters share a name, one per player */
inline std::string OwnersText(const std::vector<std::string>& owners)
{
    std::string o;
    for (size_t i = 0; i < owners.size(); ++i)
    {
        if (i > 0) o += (i + 1 == owners.size()) ? " and " : ", ";
        o += ShownName(owners[i]) + "'s";
    }
    return o;
}
/* "Tam", "Tam and Lee", "Tam, Lee and Bo" */
inline std::string NameList(const std::vector<std::string>& names, size_t from, size_t to)
{
    std::string o;
    for (size_t i = from; i < to && i < names.size(); ++i)
    {
        if (i > from) o += (i + 1 == to || i + 1 == names.size()) ? " and " : ", ";
        o += ShownName(names[i]);
    }
    return o;
}
/* the jail spot's place words ("the Ashlands") for its word ("ash"); "" = no such spot */
inline std::string JailPlace(const std::string& key)
{
    const int s = admintool::JailSpotOf(key);
    return s < 0 ? std::string() : std::string(admintool::kJailSpots[s].place);
}
/* a count and its words, one / many as the counts read naturally (mockups-t1205.md section 14, decision 1400): "1 character",
   "5 characters" */
inline std::string Count(unsigned n, const char* one, const char* many) { return Num(n) + " " + (n == 1 ? one : many); }
/* /home's words for the dead who stayed (none: nothing) */
inline std::string DeadStayedText(unsigned dead)
{
    return dead == 0 ? std::string() : " " + Count(dead, "dead character stayed.", "dead characters stayed.");
}
/* a squad move's line: /home ("Tam, Lee and Bo went home.") or /jail ("Ana's squad was sent to the Ashlands."), then one sentence
   per character who stayed knocked out ("Bo is knocked out and stayed."); /home also the dead who stayed */
inline std::string SquadMovedLine(const admincmd::Reply& r, bool home)
{
    const size_t moved = r.num < r.others.size() ? (size_t)r.num : r.others.size();
    std::string o = home ? NameList(r.others, 0, moved) + " went home."
                         : ShownName(r.name) + "'s squad was sent to " + JailPlace(r.place) + ".";
    for (size_t i = moved; i < r.others.size(); ++i) o += " " + ShownName(r.others[i]) + " is knocked out and stayed.";
    if (home) o += DeadStayedText(r.num2);
    return o;
}
/* /restore's line (mockups-t1205.md section 4): the lost limbs grown back, then the robotic limbs left on (never replaced). With no
   limb grown back it is /heal's own approved line, since nothing more was done. The counts read naturally (section 14, decision
   1400): "1 lost limb grew back" / "2 lost limbs grew back", "1 robot limb was left on" / "2 robot limbs were left on". */
inline std::string RestoredText(const std::string& n, unsigned grown, unsigned robot)
{
    std::string t = grown > 0 ? n + " is fully healed, and " + Count(grown, "lost limb grew back.", "lost limbs grew back.")
                              : n + " is fully healed: wounds, blood, crippled limbs and hunger.";
    if (robot > 0) t += " " + Count(robot, "robot limb was left on.", "robot limbs were left on.");
    return t;
}
/* /here and /additem when this game has no character selected (mockups-t1205.md section 14) */
inline std::string NoCharacterLine() { return "You have no character selected."; }
/* the line for one reply; empty = no line (a /stop that worked: every player gets the DISCONNECTED box; an /additem that went in:
   the game's own AddItemLine, which knows the item and the count it asked) */
inline std::string ReplyLine(const admincmd::Reply& r)
{
    const std::string n = ShownName(r.name);
    const std::string verb = admincmd::VerbName((int)r.verb);
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
        case admincmd::kVerbMedic:        return n + "'s wounds are closed and the bleeding has stopped.";
        case admincmd::kVerbHeal:         return n + " is fully healed: wounds, blood, crippled limbs and hunger.";
        case admincmd::kVerbRestore:      return RestoredText(n, r.num, r.num2);
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
    case admincmd::kRepPasswordBad:   /* a password the box cannot make (no minimum length, owner 1368) */
    case admincmd::kRepBadRequest:    return "The world server didn't understand that command.";
    case admincmd::kRepNotListed:     return n + " is not an admin.";
    case admincmd::kRepNoId:          return n + "'s game has no player ID the world can remember, so nothing changed.";
    case admincmd::kRepWhereAt:       return WhereLine(n, r.x, r.z, r.place);
    case admincmd::kRepWhereNoAnswer: return n + "'s game didn't answer, so where " + n + " is isn't known.";
    case admincmd::kRepNoSuchName:    return "No player or character by that name.";
    case admincmd::kRepNotInWorld:    return n + " is not in the world right now.";
    case admincmd::kRepLapsed:        return "No answer came in time, so this may or may not have happened. Check with /where before you try again.";
    case admincmd::kRepFailedOnGame:  return "That didn't work on " + n + "'s game, so nothing changed.";
    case admincmd::kRepPlayerAndChar:
        return n + " is a player and also one of " + ShownName(r.others.empty() ? std::string() : r.others[0]) + "'s characters. Type " + verb + " @"
               + TypedName(n) + " for the player, or " + verb + " #" + TypedName(n) + " for the character.";
    case admincmd::kRepManyChars:
        return "More than one character is called " + n + " (" + OwnersText(r.others) + "). Type " + verb + " #" + TypedName(n) + "@"
               + TypedName(ShownName(r.others.empty() ? std::string() : r.others[0])) + " to pick one.";
    case admincmd::kRepManyInSquad:
        return ShownName(r.others.empty() ? std::string() : r.others[0]) + " has more than one character called " + n
               + ". Select the one you mean and use the player's name instead.";
    case admincmd::kRepWhereNoChar:   return n + " has no character in the world right now.";
    case admincmd::kRepNoCharSelected: return NoCharacterLine();
    case admincmd::kRepAskerNotInWorld: return "You have no character in the world right now.";   /* section 15 (owner 1406) */
    case admincmd::kRepWhereNobody:   return "No players are in the world right now.";             /* section 15 (owner 1406) */
    case admincmd::kRepDead:          return n + " is dead. Use /revive " + n + " instead.";
    case admincmd::kRepHealedOut:     return n + " is healed but still knocked out. " + n + " will get up when the knockout ends.";
    case admincmd::kRepSquadHealed:
        return n + "'s squad is healed (" + Count(r.num, "character).", "characters).") + (r.num2 > 0 ? " " + Count(r.num2, "is dead: use /revive.", "are dead: use /revive.") : std::string());
    case admincmd::kRepBrought:       return n + " was brought to " + ShownName(r.others.empty() ? std::string() : r.others[0]) + ".";
    case admincmd::kRepGodOn:
        return "God mode is on for " + n + ": enemies ignore " + n + ", and " + n + " takes no damage, knockouts, hunger or lost limbs.";
    case admincmd::kRepGodOff:        return "God mode is off for " + n + ".";
    case admincmd::kRepGodAlready:    return n + " already has god mode on.";
    case admincmd::kRepGodNotOn:      return n + " doesn't have god mode on.";
    case admincmd::kRepGodDead:       return n + " is dead. Use /revive " + n + " first.";
    case admincmd::kRepBroughtHealed:
        return n + " was knocked out, so " + n + " was healed first and brought to " + ShownName(r.others.empty() ? std::string() : r.others[0]) + ".";
    case admincmd::kRepMovedTo:       return n + " was moved to " + Coord(r.x) + ", " + Coord(r.z) + ".";
    case admincmd::kRepWent:          return n + " went to " + ShownName(r.others.empty() ? std::string() : r.others[0]) + ".";
    case admincmd::kRepHomeSet:
        if (r.place.empty()) return "Your home is set at " + Coord(r.x) + ", " + Coord(r.z) + ".";
        return "Your home is set " + swtab::PlaceCell(r.place) + " (at " + Coord(r.x) + ", " + Coord(r.z) + ").";
    case admincmd::kRepNoHome:        return "You have no home yet. Use /sethome first.";
    case admincmd::kRepWentHome:      return SquadMovedLine(r, true);
    case admincmd::kRepJailed:        return SquadMovedLine(r, false);
    case admincmd::kRepJailNotice:    return n + " has been jailed in " + JailPlace(r.place) + ".";
    case admincmd::kRepStillDown:     return n + " is still knocked out, so " + n + " was not moved. Try again in a moment.";
    case admincmd::kRepRevived:       return n + " is back, beside " + ShownName(r.others.empty() ? std::string() : r.others[0]) + ". Nothing was paid.";
    case admincmd::kRepNoFallen:      return "No fallen character by that name.";
    case admincmd::kRepReviveNoMate:
        return n + " has no living character for " + ShownName(r.others.empty() ? std::string() : r.others[0]) + " to come back beside. Nothing was paid.";
    case admincmd::kRepSquadFull:     return n + "'s squad is already full. Nothing was paid.";
    case admincmd::kRepNotRevived:    return n + " was not brought back and nothing was paid.";
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
/* ---- the toolbox's typed lines (mockups-t1205.md sections 2, 3 and 13) ---- */
inline std::string WhereUsageLine() { return "Type it like this: /where <player name>, or /where for everyone"; }
inline std::string HereUsageLine() { return "Type it like this: /here"; }
/* a typed name's mistake (admintool::ParseName); empty = none to say. The command's usage line follows it. */
inline std::string NameMistakeLine(int parse)
{
    if (parse == admintool::kNameNoClose) return "The name's closing quote is missing.";
    if (parse == admintool::kNameTooLong) return "That name is too long (32 characters at most).";
    return std::string();
}
/* the words after a typed line's command word, trimmed */
inline std::string AfterWord(const std::string& typed)
{
    const std::string t = admincmd::Trim(typed);
    size_t a = 0; while (a < t.size() && !admincmd::IsSpace(t[a])) ++a;
    return admincmd::Trim(t.substr(a));
}
/* "/where" or "/where <player name>" (plain or @name): true = goes up as kVerbWhere with *target ("" = every player in the world) and
   *flags (admincmd::kFlagPickPlayer for @name); false = nothing goes up and *lines are this game's answer - a name's mistake and the
   usage line, or the usage line alone (no name after an @, a #character, or words after the name) */
inline bool WhereTyped(const std::string& typed, std::string* target, unsigned* flags, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; lines->clear();
    const std::string rest = AfterWord(typed);
    if (rest.empty()) return true;
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(rest, &n, &after);
    if (r == admintool::kNameOk && n.pick != admintool::kPickChar && after.empty())
    {
        *target = n.name;
        if (n.pick == admintool::kPickPlayer) *flags = admincmd::kFlagPickPlayer;
        return true;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(WhereUsageLine());
    return false;
}
/* "/here" alone; false = words after it (the usage line is the answer) */
inline bool HereTyped(const std::string& typed) { return AfterWord(typed).empty(); }
/* /here's line: "Tam is at -12450, 3210, height 1290, near Squin (850 away)." - the nearest town, however far */
inline std::string HereLine(const std::string& who, float x, float y, float z, const std::string& town, float dist)
{
    return who + " is at " + Coord(x) + ", " + Coord(z) + ", height " + Coord(y) + ", " + swtab::PlaceCell(town) + " (" + Coord(dist) + " away).";
}
/* ---- /medic, /heal and /restore (mockups-t1205.md section 4) ---- */
inline std::string HealUsageLine(int verb)
{
    if (verb == admincmd::kVerbMedic) return "Type it like this: /medic <name>, or /medic all <player name>";
    if (verb == admincmd::kVerbRestore) return "Type it like this: /restore <name>, or /restore all <player name>";
    return "Type it like this: /heal <name>, or /heal all <player name>";
}
/* the line the TARGET player's game shows for each character an admin healed (owner 1396) */
inline std::string HealedByAdminLine(const std::string& who) { return "An admin healed " + who + "."; }
/* "/medic <name>", "/medic all <player name>" (and /heal): true = goes up with *target, *flags (admincmd::kFlagPickPlayer for @name,
   kFlagPickChar for #name with *extra = the player of #name@player, kFlagAll for the "all" form) ; false = nothing goes up and
   *lines are this game's answer - a name's mistake and the usage line, or the usage line alone */
inline bool HealTyped(const std::string& typed, int verb, std::string* target, unsigned* flags, std::string* extra, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; extra->clear(); lines->clear();
    std::string rest = AfterWord(typed);
    bool all = false;
    {   /* "all" then a name: the whole squad of that player */
        size_t a = 0; while (a < rest.size() && !admincmd::IsSpace(rest[a])) ++a;
        if (admincmd::LowerAscii(rest.substr(0, a)) == "all" && !admincmd::Trim(rest.substr(a)).empty()) { all = true; rest = admincmd::Trim(rest.substr(a)); }
    }
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(rest, &n, &after);
    if (r == admintool::kNameOk && after.empty() && !(all && n.pick == admintool::kPickChar))
    {
        *target = n.name;
        if (all) *flags |= admincmd::kFlagAll;
        if (n.pick == admintool::kPickPlayer) *flags |= admincmd::kFlagPickPlayer;
        if (n.pick == admintool::kPickChar) { *flags |= admincmd::kFlagPickChar; *extra = n.player; }
        return true;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(HealUsageLine(verb));
    return false;
}
/* ---- /bring, /tp, /goto, /sethome, /home (mockups-t1205.md sections 6 to 9) and /jail (section 10) ---- */
inline std::string MoveUsageLine(int verb)
{
    switch (verb)
    {
    case admincmd::kVerbBring:   return "Type it like this: /bring <name>";
    case admincmd::kVerbTp:      return "Type it like this: /tp <name> <x> <z> (two numbers, as /where shows them)";
    case admincmd::kVerbGoto:    return "Type it like this: /goto <name>";
    case admincmd::kVerbSetHome: return "Type it like this: /sethome";
    case admincmd::kVerbHome:    return "Type it like this: /home";
    case admincmd::kVerbJail:    return "Type it like this: /jail <player name>, or /jail <player name> <spot>. /jail list shows the spots.";
    default:                     return std::string();
    }
}
/* /god (mockups-t1205.md section 12) */
inline std::string GodUsageLine() { return "Type it like this: /god <name> on, or /god <name> off"; }
/* the line the TARGET player's game shows when an admin turned god mode on or off for one of its characters (owner 1396) */
inline std::string GodByAdminLine(const std::string& who, bool on) { return std::string("An admin turned god mode ") + (on ? "on" : "off") + " for " + who + "."; }
/* "/god <name> on" or "/god <name> off" (any case): true = goes up with *target, *flags (kFlagGodOn for on; kFlagPickPlayer for
   @name, kFlagPickChar for #name with *extra = the player of #name@player); false = nothing goes up and *lines are this game's
   answer - a name's mistake and the usage line, or the usage line alone */
inline bool GodTyped(const std::string& typed, std::string* target, unsigned* flags, std::string* extra, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; extra->clear(); lines->clear();
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(AfterWord(typed), &n, &after);
    const std::string word = admincmd::LowerAscii(admincmd::Trim(after));
    if (r == admintool::kNameOk && (word == "on" || word == "off"))
    {
        *target = n.name;
        if (word == "on") *flags |= admincmd::kFlagGodOn;
        if (n.pick == admintool::kPickPlayer) *flags |= admincmd::kFlagPickPlayer;
        if (n.pick == admintool::kPickChar) { *flags |= admincmd::kFlagPickChar; *extra = n.player; }
        return true;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(GodUsageLine());
    return false;
}
/* the line the TARGET player's game shows for each of its characters an admin moved (owner 1396) */
inline std::string MovedByAdminLine(const std::string& who) { return "An admin moved " + who + "."; }
inline std::string JailListLine()
{
    std::string o = "Jail spots: ";
    for (int i = 0; i < admintool::kJailSpotCount; ++i)
        o += std::string(i > 0 ? ", " : "") + admintool::kJailSpots[i].key + " (" + admintool::kJailSpots[i].place + ")";
    return o + ". With no spot, one is picked at random.";
}
inline std::string NoSpotLine(const std::string& typed) { return "No jail spot called \"" + typed + "\". Type /jail list to see them."; }
/* a map number as typed for /tp: an optional '-', digits, an optional '.' and digits (a ',' after it is allowed, as /where shows) */
inline bool TpNumber(std::string w, float* out)
{
    if (!w.empty() && w[w.size() - 1] == ',') w.erase(w.size() - 1);
    return admintool::HomeNumber(w, out);
}
/* "/bring <name>", "/goto <name>" or "/tp <name> <x> <z>": true = goes up with *target, *flags (kFlagPickPlayer for @name,
   kFlagPickChar for #name with *extra = the player of #name@player) and for /tp *x, *z; false = nothing goes up and *lines are this
   game's answer - a name's mistake and the usage line, or the usage line alone */
inline bool MoveTyped(const std::string& typed, int verb, std::string* target, unsigned* flags, std::string* extra, float* x, float* z, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; extra->clear(); lines->clear(); *x = 0.0f; *z = 0.0f;
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(AfterWord(typed), &n, &after);
    bool ok = r == admintool::kNameOk;
    if (ok && verb == admincmd::kVerbTp)
    {
        size_t a = 0; while (a < after.size() && !admincmd::IsSpace(after[a])) ++a;
        const std::string w1 = after.substr(0, a), w2 = admincmd::Trim(after.substr(a));
        ok = TpNumber(w1, x) && w2.find(' ') == std::string::npos && TpNumber(w2, z) && w2[w2.size() - 1] != ',';
    }
    else if (ok) ok = after.empty();
    if (ok)
    {
        *target = n.name;
        if (n.pick == admintool::kPickPlayer) *flags |= admincmd::kFlagPickPlayer;
        if (n.pick == admintool::kPickChar) { *flags |= admincmd::kFlagPickChar; *extra = n.player; }
        return true;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(MoveUsageLine(verb));
    return false;
}
enum { kJailSend = 0, kJailList = 1, kJailAnswered = 2 };
/* "/jail <player name> [spot]" or "/jail list": kJailSend = goes up with *target, *flags (kFlagPickPlayer for @name) and *spot (the
   spot's word, "" = at random); kJailList = this game shows JailListLine; kJailAnswered = *lines are this game's answer (a name's
   mistake and the usage line, the usage line alone, or a spot that is not one) */
inline int JailTyped(const std::string& typed, std::string* target, unsigned* flags, std::string* spot, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; spot->clear(); lines->clear();
    const std::string rest = AfterWord(typed);
    if (admincmd::LowerAscii(rest) == "list") return kJailList;
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(rest, &n, &after);
    if (r == admintool::kNameOk && n.pick != admintool::kPickChar && after.find(' ') == std::string::npos)
    {
        if (!after.empty() && admintool::JailSpotOf(after) < 0) { lines->push_back(NoSpotLine(after)); return kJailAnswered; }
        *target = n.name; *spot = admincmd::LowerAscii(after);
        if (n.pick == admintool::kPickPlayer) *flags = admincmd::kFlagPickPlayer;
        return kJailSend;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(MoveUsageLine(admincmd::kVerbJail));
    return kJailAnswered;
}
/* "/sethome" or "/home" alone; false = words after it (the usage line is the answer) */
inline bool BareTyped(const std::string& typed) { return AfterWord(typed).empty(); }

/* ---- /additem (mockups-t1205.md section 11) ---- */
/* ---- /revive (mockups-t1205.md section 5) ---- */
inline std::string ReviveUsageLine() { return "Type it like this: /revive <fallen character's name>"; }
/* the line the TARGET player's game shows when an admin brought one of its characters back (owner 1396) */
inline std::string RevivedByAdminLine(const std::string& who) { return "An admin brought " + who + " back, free."; }
/* "/revive <name>" (plain, #name or #name@player - a fallen character is never a player, so no @name): true = goes up with *target
   and *flags (admincmd::kFlagPickChar for #name, *extra = the player of #name@player); false = nothing goes up and *lines are this
   game's answer - a name's mistake and the usage line, or the usage line alone */
inline bool ReviveTyped(const std::string& typed, std::string* target, unsigned* flags, std::string* extra, std::vector<std::string>* lines)
{
    target->clear(); *flags = 0; extra->clear(); lines->clear();
    admintool::Name n; std::string after;
    const int r = admintool::ParseName(AfterWord(typed), &n, &after);
    if (r == admintool::kNameOk && after.empty() && n.pick != admintool::kPickPlayer)
    {
        *target = n.name;
        if (n.pick == admintool::kPickChar) { *flags = admincmd::kFlagPickChar; *extra = n.player; }
        return true;
    }
    const std::string mistake = NameMistakeLine(r);
    if (!mistake.empty()) lines->push_back(mistake);
    lines->push_back(ReviveUsageLine());
    return false;
}
/* the owning game's answer (admintool kDone*) for its bring-back: done, or the FALLEN tab's refusal (swtab::kRef*) - no free
   living character to come back beside, at the game's character limit (the squad is full), anything else */
inline unsigned ReviveDoneCode(int ok, int refusal)
{
    if (ok) return (unsigned)admintool::kDoneOk;
    if (refusal == swtab::kRefNoMate) return (unsigned)admintool::kDoneNoMate;
    if (refusal == swtab::kRefAtLimit) return (unsigned)admintool::kDoneFull;
    return (unsigned)admintool::kDoneFailed;
}

inline std::string AddItemUsageLine() { return "Type it like this: /additem <item name> [count] (1 to 99, to your own lead character)"; }
inline std::string NoItemLine(const std::string& typed) { return "No item called \"" + typed + "\"."; }
inline std::string ManyItemsLine(const std::string& typed, const std::vector<std::string>& names)
{
    std::string o = "More than one item is called \"" + typed + "\": ";
    for (size_t i = 0; i < names.size(); ++i) o += (i > 0 ? ", " : "") + names[i];
    return o + ". Type the full name.";
}
inline std::string CountLine() { return "The count is a number from 1 to 99."; }
inline std::string StillMakingLine() { return "Another /additem is still being made. Try again in a moment."; }
/* the admin's own answer: `added` of the `asked` went in to `who` */
inline std::string AddItemLine(unsigned added, unsigned asked, const std::string& item, const std::string& who)
{
    if (added == 0) return "Nothing was added: " + ShownName(who) + "'s inventory is full.";
    return "Added " + Num(added) + " " + item + " to " + ShownName(who) + "." + (asked > added ? " " + Num(asked - added) + " did not fit." : std::string());
}
/* a word that is a count as typed: digits, perhaps after a '-' */
inline bool CountWord(const std::string& w)
{
    size_t i = (!w.empty() && w[0] == '-') ? 1 : 0;
    if (i >= w.size()) return false;
    for (; i < w.size(); ++i) if (w[i] < '0' || w[i] > '9') return false;
    return true;
}
/* "/additem <item name> [count]": true = look the item up by *item, *count of it (1 when none is typed); false = *lines are this
   game's answer (the usage line; the count's line; a name's mistake and the usage line) */
inline bool AddItemTyped(const std::string& typed, std::string* item, int* count, std::vector<std::string>* lines)
{
    item->clear(); *count = 1; lines->clear();
    std::string rest = AfterWord(typed);
    size_t sp = rest.size(); while (sp > 0 && !admincmd::IsSpace(rest[sp - 1])) --sp;
    if (sp > 0 && CountWord(rest.substr(sp)))
    {
        if (!admintool::CountOk(rest.substr(sp), count)) { lines->push_back(CountLine()); return false; }
        rest = admincmd::Trim(rest.substr(0, sp));
    }
    if (!rest.empty() && rest[0] == '"')
    {
        if (rest.size() < 2 || rest[rest.size() - 1] != '"') { lines->push_back(NameMistakeLine(admintool::kNameNoClose)); lines->push_back(AddItemUsageLine()); return false; }
        rest = rest.substr(1, rest.size() - 2);
    }
    rest = admincmd::CleanText(rest, 1024);
    if (rest.empty()) { lines->push_back(AddItemUsageLine()); return false; }
    if (admintool::Chars(rest) > admintool::kNameChars) { lines->push_back(NameMistakeLine(admintool::kNameTooLong)); lines->push_back(AddItemUsageLine()); return false; }
    *item = rest;
    return true;
}
/* ONE /additem AT A TIME on this game: from its ADMIN_REQ until its ADMIN_REPLY on that link (the world server answers each request,
   by the lapse at the latest), or that link's end */
struct AddItemWait { unsigned req; int gen; unsigned asked; std::string item; AddItemWait() : req(0), gen(-1), asked(0) {} };
inline bool AddItemBusy(const AddItemWait& w, int linkGen) { return w.req != 0 && w.gen == linkGen; }
/* a reply on the link `linkGen`: true = it ends the /additem waiting (its request), whose words this game says itself */
inline bool AddItemEnds(const AddItemWait& w, int linkGen, const admincmd::Reply& r)
{
    return AddItemBusy(w, linkGen) && r.verb == (unsigned)admincmd::kVerbAddItem && r.req == w.req;
}
inline std::string HelpLine() { return "Commands: /login <password>, /logout, /kick <player name>, /ban <player name>, /unban <player name>, /stop"; }
/* T-1205: /help's lines, in order (mockups-t1205.md section 1, to everyone as today): today's line first, then the admin toolbox and
   how a name is typed. The chat window wraps a long line into rows (chat.cpp Wrap); the 200-character limit is the typed line's only */
inline std::vector<std::string> HelpLines()
{
    std::vector<std::string> v;
    v.push_back(HelpLine());
    v.push_back("Admin tools: /where [player], /here, /medic <name>, /heal <name>, /restore <name>, /revive <name>, /bring <name>, /tp <name> <x> <z>, /goto <name>, /sethome, /home, /jail <name> [spot], /additem <item> [count], /god <name> on|off");
    v.push_back("A name can be a player's (that means their lead character) or a character's. Put a name with spaces in quotes: \"Old Tam\".");
    return v;
}
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
/* an admin now: the owner (logged in with the owner password), or a logged-in admin */
inline bool AdminBy(unsigned standing) { return standing == (unsigned)adminauth::kStandAdmin || standing == (unsigned)adminauth::kStandOwner; }
/* the server's `standing` for `why`, taken on the link `linkGen`. *line = IdleEndLine() when a logged-in admin is one no longer for any
   reason but its own /logout - by this standing, or by a link that ended before this game saw it end; else empty. True = this game's
   "admin now" changed. */
/* a remembered login the world server answered as a wrong password is forgotten (a changed password would else be shown at every
   connect and counted wrong each time); a locked, busy or not-listed answer, or one with no password set, keeps it */
inline bool LoginForgets(unsigned code) { return code == (unsigned)adminauth::kResWrong; }
/* REMEMBERED LOGINS (owner 1380 / 1379): a typed /login is remembered for its world when the world server took it (RESULT owner or
   ok) and it made this game something it was not when typed - the owner, or an admin from nothing. An admin password typed by the
   owner (answered as the owner) is never remembered as the owner's. */
inline bool LoginRemembers(unsigned code, unsigned standingWhenTyped)
{
    if (code == (unsigned)adminauth::kResOwner) return standingWhenTyped != (unsigned)adminauth::kStandOwner;
    if (code == (unsigned)adminauth::kResOk) return standingWhenTyped == (unsigned)adminauth::kStandNone;
    return false;
}
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
/* ---- this game's remembered login by itself (admin.cpp AdminTick), and the player's own /logout (owner 1412 b) ----
   While this game's standing is none and no login waits, its remembered login is tried by itself: at once after the WELCOME when
   the door carried no login proof, else kAutoRetryMs after the last try; at most kAutoTries times on one link.
   A /logout this player typed HOLDS FOR THE VISIT: from the moment it is typed until this game leaves the world it was typed in
   (back to the title, another world or save loaded - the world-load generation, store.cpp StoreWorldLoadBegan, moves), the remembered
   login does not log this game in by itself. A link drop and reconnect inside the same world is the same visit (the link generation
   is not part of the hold). The password is kept; a typed /login works as always. */
const unsigned kAutoRetryMs = 5000;
const int kAutoTries = 2;
struct LogoutHold { bool set; long worldGen; LogoutHold() : set(false), worldGen(0) {} };
inline void LogoutHoldSet(LogoutHold* h, long worldGenNow) { h->set = true; h->worldGen = worldGenNow; }
/* the player's own /logout still holds in the world-load generation `worldGenNow` */
inline bool LogoutHolds(const LogoutHold& h, long worldGenNow) { return h.set && h.worldGen == worldGenNow; }
/* whether the remembered login is tried by itself now (the caller has checked: linked, standing none, no login waiting) */
inline bool AutoLoginDue(int triesThisLink, bool doorLoginSentThisLink, unsigned msSinceLastTry, const LogoutHold& h, long worldGenNow)
{
    if (LogoutHolds(h, worldGenNow) || triesThisLink >= kAutoTries) return false;
    return (triesThisLink == 0 && !doorLoginSentThisLink) || msSinceLastTry >= kAutoRetryMs;
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
inline const char* PasswordShowWord() { return "SHOW"; }
/* the box's limit in characters: any this many characters fit the server's byte limit (a character is at most 4 bytes) */
const size_t kPasswordBoxChars = adminauth::kPasswordMaxBytes / 4;
enum { kPwKeep = 0, kPwSend = 2, kPwBad = 3, kPwNotSent = 4 };   /* kPwNotSent: AdminSetPassword's answer when it was not sent */
/* the box's text as /login takes it: the spaces at both ends dropped (adminauth::LoginPassword), so the password set is the one typed */
inline void PasswordTrim(std::string* pw)
{
    size_t b = pw->size(); while (b > 0 && adminauth::Space((*pw)[b - 1])) --b;
    size_t a = 0; while (a < b && adminauth::Space((*pw)[a])) ++a;
    pw->resize(b);
    pw->erase(0, a);
}
/* DONE with `typed` in the box: kPwKeep = an empty box (the password is left as it is), kPwSend = goes to the world server, kPwBad =
   a control byte or too many bytes (the box's own limits keep typing from making one). No minimum length (owner 1368). */
inline int PasswordDone(const std::string& typed)
{
    if (typed.empty()) return kPwKeep;
    return adminauth::PasswordOk(typed) ? kPwSend : kPwBad;
}

}   /* namespace adminwords */

#endif
