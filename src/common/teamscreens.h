/* src/common/teamscreens.h - THE FACTION SCREENS: what the PLAYERS tab's bottom line shows for a selection, the faction boxes
 * (invitation, leave, remove, disband, removed while away) and the message line a membership event gives.
 *
 * Every word is the approved page's (build/pages/player-factions.html, decisions 473-481) or an approved decision's (515, 516).
 * A line or a box that would need a word neither has is "" here and is not shown: a player whose name this game does not know
 * (never a made-up name), a refused request's reason other than an invitation's, and the invited player's own line when they
 * declined.
 *
 * THE BOTTOM LINE, by this player's place in a faction (none / founder / member) and the selected row:
 *   - a teammate selected: "<NAME> IS A MEMBER OF YOUR FACTION"; the founder gets DISBAND FACTION left and REMOVE FROM
 *     FACTION right (REMOVE only when this game knows that member's name - its box names the member), a member LEAVE FACTION
 *     right; no stance buttons (towards a teammate the stance is held at ally).
 *   - anyone else, this player not a member: "YOUR STANCE TOWARDS <FACTION>", the three stance buttons, and INVITE TO FACTION
 *     right while that player is online and in no faction of players; in its place, when they cannot be invited, the words why
 *     (owner 516 c): "<Name> is offline." (first, as the world server refuses it first), else "<Name> is in another faction.".
 *   - anyone else, this player a member: "STANCE TOWARDS <FACTION>: <STANCE> (SET BY <FOUNDER>)" (owner 476: the founder sets
 *     it) and LEAVE FACTION right; no stance buttons.
 *   - nothing selected: no line; a member keeps LEAVE FACTION right (it is on a member's line whatever is selected).
 *
 * Pure: no engine memory, no MyGUI, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include "playerstab.h"
#include "teamwire.h"
#include "teamresearch.h"   /* the shared research (swteam::ResearchSharedLine and the routes a teammate's tech takes) */

namespace teamscreen {

enum Role { kRoleNone = 0, kRoleFounder = 1, kRoleMember = 2 };

/* the bottom line's action buttons */
enum Action { kActNone = 0, kActInvite = 1, kActRemove = 2, kActDisband = 3, kActLeave = 4, kActLast = 4 };
inline const char* ActionCaption(int a)
{
    static const char* const c[kActLast + 1] = { "", "INVITE TO FACTION", "REMOVE FROM FACTION", "DISBAND FACTION", "LEAVE FACTION" };
    return (a >= 0 && a <= kActLast) ? c[a] : "";
}
/* the TEST-ONLY lever's word for each (playerstab action <word>) */
inline const char* ActionWord(int a)
{
    static const char* const w[kActLast + 1] = { "", "invite", "remove", "disband", "leave" };
    return (a >= 0 && a <= kActLast) ? w[a] : "";
}
inline int ActionOfWord(const std::string& w)
{
    for (int a = 1; a <= kActLast; ++a) if (w == ActionWord(a)) return a;
    return kActNone;
}

/* a teammate's YOU and THEM cells: that player's place in the faction */
inline std::string RoleCell(bool founder) { return founder ? "Founder" : "Member"; }

/* the selected teammate's line; "" when this game does not know the name */
inline std::string MemberLine(const std::string& name)
{
    const std::string n = nametag::Trimmed(name);
    if (n.empty()) return std::string();
    return playerstab::Upper(playerstab::EscapeHash(n)) + " IS A MEMBER OF YOUR FACTION";
}
/* a member's line towards a player outside the faction: the founder's stance (its level as the stance buttons name it); the
   "(SET BY <FOUNDER>)" part only when this game knows the founder's name */
inline std::string FoundersStanceLine(const std::string& faction, int level, const std::string& founder)
{
    const int k = (level >= nametag::kFriendly && level <= nametag::kHostile) ? level : nametag::kNeutral;
    std::string s = "STANCE TOWARDS " + playerstab::Upper(playerstab::EscapeHash(nametag::Trimmed(faction))) + ": " + playerstab::ButtonCaption(k);
    const std::string f = nametag::Trimmed(founder);
    if (!f.empty()) s += " (SET BY " + playerstab::Upper(playerstab::EscapeHash(f)) + ")";
    return s;
}

/* what the bottom line is built from: this player's role, and the selected row */
struct Sel
{
    int myRole;            /* kRole* */
    bool mate;             /* the selected player shares this player's faction */
    bool inTeam;           /* the selected player is in a faction of players that is not this player's */
    bool online;
    int you;               /* this player's own level towards them (nametag levels) */
    std::string name;      /* the selected player's name as this game knows it ("" = not known) */
    std::string faction;   /* the selected player's faction as the table shows it */
    std::string founder;   /* this player's founder's name ("" = not known, or no faction) */
    Sel() : myRole(kRoleNone), mate(false), inTeam(false), online(false), you(nametag::kNeutral) {}
};
enum LineKind { kLineSelect = 0, kLineStance = 1, kLineMember = 2, kLineFounders = 3 };
struct View
{
    int lineKind;
    std::string line;      /* "" = no line (a name this game does not know) */
    bool stance;           /* the three stance buttons show */
    int left, right;       /* kAct* (kActNone = no button) */
    std::string note;      /* the words in the right-hand button's place when there is no button there ("" = none) */
    View() : lineKind(kLineSelect), stance(false), left(kActNone), right(kActNone) {}
};
/* why the selected player cannot be invited, in INVITE TO FACTION's place (owner 516 c); "" when they can, or the name is not known */
inline std::string NotInvitableNote(bool online, bool inTeam, const std::string& name)
{
    const std::string n = playerstab::EscapeHash(nametag::Trimmed(name));
    if (n.empty()) return std::string();
    if (!online) return n + " is offline.";
    if (inTeam) return n + " is in another faction.";
    return std::string();
}
inline View ViewFor(bool selected, const Sel& s)
{
    View v;
    if (s.myRole == kRoleMember) v.right = kActLeave;
    if (!selected) { v.lineKind = kLineSelect; v.line = playerstab::kSelectLine; return v; }
    if (s.mate && s.myRole != kRoleNone)
    {
        v.lineKind = kLineMember;
        v.line = MemberLine(s.name);
        if (s.myRole == kRoleFounder) { v.left = kActDisband; v.right = nametag::Trimmed(s.name).empty() ? kActNone : kActRemove; }
        return v;
    }
    if (s.myRole == kRoleMember)
    {
        v.lineKind = kLineFounders;
        v.line = FoundersStanceLine(s.faction, s.you, s.founder);
        return v;
    }
    v.lineKind = kLineStance;
    v.line = playerstab::StanceLine(s.faction);
    v.stance = true;
    if (s.online && !s.inTeam) v.right = kActInvite;
    else v.note = NotInvitableNote(s.online, s.inTeam, s.name);
    return v;
}
/* the box an action asks first with (INVITE asks nothing: the invitation itself is the question) */
enum Box { kBoxNone = 0, kBoxHostile = 1, kBoxInvite = 2, kBoxLeave = 3, kBoxRemove = 4, kBoxDisband = 5, kBoxAway = 6, kBoxAwayDisband = 7, kBoxLast = 7 };
inline int BoxOfAction(int a) { return a == kActRemove ? kBoxRemove : a == kActDisband ? kBoxDisband : a == kActLeave ? kBoxLeave : kBoxNone; }

/* THE BOXES: title, words, buttons (back / cancel left, the action right; the two while-away boxes have OK alone, right) */
inline const char* BoxTitle(int k)
{
    static const char* const t[kBoxLast + 1] = { "", playerstab::kBoxTitle, "FACTION INVITATION", "LEAVE FACTION", "REMOVE FROM FACTION", "DISBAND FACTION", "FACTION", "FACTION" };
    return (k >= 0 && k <= kBoxLast) ? t[k] : "";
}
inline const char* BoxLeft(int k)
{
    static const char* const t[kBoxLast + 1] = { "", playerstab::kBoxCancel, "DECLINE", "CANCEL", "CANCEL", "CANCEL", "", "" };
    return (k >= 0 && k <= kBoxLast) ? t[k] : "";
}
inline const char* BoxRight(int k)
{
    static const char* const t[kBoxLast + 1] = { "", playerstab::kBoxConfirm, "ACCEPT", "LEAVE", "REMOVE", "DISBAND", "OK", "OK" };
    return (k >= 0 && k <= kBoxLast) ? t[k] : "";
}
/* the box's words: `team` the faction's name, `name` the player named (the inviter, the member removed; the SET HOSTILE box's
   faction). "" when a name the words need is not known - the box is not shown. */
inline std::string BoxText(int k, const std::string& team, const std::string& name)
{
    const std::string t = playerstab::EscapeHash(nametag::Trimmed(team)), n = playerstab::EscapeHash(nametag::Trimmed(name));
    switch (k)
    {
    case kBoxHostile: return n.empty() ? std::string() : playerstab::BoxText(name);
    case kBoxInvite:
        if (n.empty() || t.empty()) return std::string();
        return n + " invites you to join " + t + ". Members share research and standing with every faction, and can enter each other's bases. Your characters and money stay yours.";
    case kBoxLeave:
        if (t.empty()) return std::string();
        return "Leave " + t + "? You keep your research. Your standing with every faction goes back to what it was before you joined.";
    case kBoxRemove:
        if (n.empty() || t.empty()) return std::string();
        return "Remove " + n + " from " + t + "? " + n + " keeps their research. Their standing with every faction goes back to what it was before joining.";
    case kBoxDisband:
        if (t.empty()) return std::string();
        return "Disband " + t + "? Every member leaves the faction and keeps their research.";
    case kBoxAway:
        if (t.empty()) return std::string();
        return "You were removed from " + t + " while you were away. You kept your research; your standing with every faction is back to what it was before you joined.";
    case kBoxAwayDisband:
        if (t.empty()) return std::string();
        return t + " was disbanded while you were away. You kept your research; your standing with every faction is back to what it was before you joined.";
    default: return std::string();
    }
}
/* the FACTION box a RESTORE row made while this player was away is owed once its world is loaded (the row's pre-join standing put
   back): removed -> kBoxAway, disbanded -> kBoxAwayDisband (owner 516 d); none for a row made while connected, a row with no
   pre-join standing (a founder who made the team: nothing was put back), or a leave */
inline int AwayBoxOf(bool away, int why, bool haveSnapshot)
{
    if (!away || !haveSnapshot) return kBoxNone;
    return why == swteam::kWhyRemoved ? kBoxAway : why == swteam::kWhyDisbanded ? kBoxAwayDisband : kBoxNone;
}
inline bool OkOnlyBox(int k) { return k == kBoxAway || k == kBoxAwayDisband; }

/* THE INVITER'S LINE when the world server refuses an invitation (owner 516 a): the refusal's reason with the invited player's
   name (`who`; "" = not known); a member who is not the founder gets the founder line, which needs no name (owner 522); any
   other reason, or a reason whose name is not known, gives the general line */
const char* const kInviteNotSent = "The invitation could not be sent. Try again in a moment.";
const char* const kInviteNotFounder = "Only the founder can invite players.";
inline std::string InviteRefusedLine(int result, const std::string& who)
{
    if (result == swteam::kOk) return std::string();
    if (result == swteam::kRefNotFounder) return kInviteNotFounder;
    const std::string w = playerstab::EscapeHash(nametag::Trimmed(who));
    if (!w.empty())
    {
        if (result == swteam::kRefInvitePending) return w + " already has an invitation.";
        if (result == swteam::kRefInTeam) return w + " is in another faction.";
        if (result == swteam::kRefNotOnline) return w + " is offline.";
    }
    return kInviteNotSent;
}
/* THE MESSAGE LINE a membership NOTICE gives on this game (teamwire.h events; `who` = the notice's subject player's name, `actor`
   = its actor's, "" when not known; iAmSubject = this game's player is that subject). "" = no line: a refusal other than an
   invitation's, an event no approved words cover on this side, or a name not known. A refused invitation names the invited
   player as its subject (InviteRefusedLine); an invitation that ended unanswered (its time ran out, or it was withdrawn - its
   faction ended, its inviter left) tells the invited player whose it was (owner 516 b). */
inline std::string NoticeLine(int event, int result, bool iAmSubject, const std::string& who, const std::string& team, const std::string& actor = std::string())
{
    if (result != swteam::kOk) return event == swteam::kEvInvite ? InviteRefusedLine(result, iAmSubject ? std::string() : who) : std::string();
    const std::string w = playerstab::EscapeHash(nametag::Trimmed(who)), t = playerstab::EscapeHash(nametag::Trimmed(team));
    const std::string a = playerstab::EscapeHash(nametag::Trimmed(actor));
    switch (event)
    {
    case swteam::kEvInvite:   return (iAmSubject || w.empty()) ? std::string() : "Invitation sent to " + w + ".";
    case swteam::kEvDeclined: return (iAmSubject || w.empty()) ? std::string() : w + " declined your invitation.";
    case swteam::kEvExpired:  return iAmSubject ? (a.empty() ? std::string() : "The invitation from " + a + " has ended.") : (w.empty() ? std::string() : w + " did not answer.");
    case swteam::kEvInviteGone: return (iAmSubject && !a.empty()) ? "The invitation from " + a + " has ended." : std::string();
    case swteam::kEvJoined:   return t.empty() ? std::string() : iAmSubject ? "You joined " + t + "." : (w.empty() ? std::string() : w + " joined " + t + ".");
    case swteam::kEvLeft:     return t.empty() ? std::string() : iAmSubject ? "You left " + t + "." : (w.empty() ? std::string() : w + " left " + t + ".");
    case swteam::kEvRemoved:  return t.empty() ? std::string() : iAmSubject ? "You were removed from " + t + "." : (w.empty() ? std::string() : w + " was removed from " + t + ".");
    case swteam::kEvDisbanded: return t.empty() ? std::string() : t + " was disbanded.";
    default: return std::string();
    }
}
/* A TEAMMATE'S FINISHED TECH: the mod's line names the teammate, every time, whichever road the tech took here (owner 680,
   superseding that part of 515 a): "Research complete: <tech> (<teammate>)." - escaped as every shown name is. The engine's
   setResearched also shows its own "Research complete: {1}" toast; Research::load shows none. "" when the tech's or the
   teammate's name is not known. */
inline std::string ResearchCompleteShown(const std::string& tech, const std::string& member)
{
    const std::string t = nametag::Trimmed(tech), m = nametag::Trimmed(member);
    if (t.empty() || m.empty()) return std::string();
    return "Research complete: " + playerstab::EscapeHash(t) + " (" + playerstab::EscapeHash(m) + ").";
}

/* the invitation box goes up while an invitation waits unanswered, no box of this module is up, no other box that blocks the
   game is up (otherUp: the mod's other boxes, the game's own quit box - it waits and opens after) and the inviter's name is known
   (it is shown again after anything took it down while the invitation still waits) */
inline bool InviteBoxDue(bool waiting, bool boxUp, bool otherUp, bool nameKnown) { return waiting && !boxUp && !otherUp && nameKnown; }
/* a NOTICE refusing this game's own ACCEPT in a way that keeps the invitation waiting (no standing yet, not saved): the box comes
   back so the player can answer again */
inline bool InviteBackAfterRefusal(int event, int result, bool myAnswer)
{
    return myAnswer && event == swteam::kEvAnswer && result != swteam::kOk && !swteam::AnswerRefusalEndsInvite(result);
}

/* THE MESSAGE LINES held while no world is ready to show them (a world loading or tearing down): kept in order, each shown once
   when it is ready; at most kHeldLines (the oldest dropped first, counted) */
const size_t kHeldLines = 32;
struct HeldLines
{
    std::vector<std::string> q;
    std::vector<unsigned char> snd;   /* T-546: per line of q, 1 = it plays the research-complete sound when it is shown */
    unsigned dropped;
    HeldLines() : dropped(0) {}
};
inline void HoldLine(HeldLines* h, const std::string& line, bool sound = false)
{
    if (line.empty()) return;
    h->snd.resize(h->q.size(), 0);
    if (h->q.size() >= kHeldLines) { h->q.erase(h->q.begin()); h->snd.erase(h->snd.begin()); ++h->dropped; }
    h->q.push_back(line); h->snd.push_back(sound ? 1 : 0);
}
/* the lines to show now, oldest first, taken out (none while not ready); *sounds = each one's sound mark, in the same order */
inline std::vector<std::string> TakeHeld(HeldLines* h, bool ready, std::vector<unsigned char>* sounds = 0)
{
    std::vector<std::string> out;
    std::vector<unsigned char> s;
    if (ready) { h->snd.resize(h->q.size(), 0); out.swap(h->q); s.swap(h->snd); }
    if (sounds != 0) sounds->swap(s);
    return out;
}
/* lines taken and not shown (the game could not show them yet): put back in front, in their order, with their sound marks */
inline void PutBack(HeldLines* h, const std::vector<std::string>& lines, size_t from, const std::vector<unsigned char>* sounds = 0)
{
    if (from >= lines.size()) return;
    std::vector<std::string> q(lines.begin() + (std::ptrdiff_t)from, lines.end());
    std::vector<unsigned char> s;
    for (size_t i = from; i < lines.size(); ++i) s.push_back(sounds != 0 && i < sounds->size() ? (*sounds)[i] : (unsigned char)0);
    h->snd.resize(h->q.size(), 0);
    q.insert(q.end(), h->q.begin(), h->q.end());
    s.insert(s.end(), h->snd.begin(), h->snd.end());
    while (q.size() > kHeldLines) { q.erase(q.begin()); s.erase(s.begin()); ++h->dropped; }
    h->q.swap(q); h->snd.swap(s);
}
/* line i of a taken batch plays its sound: it was marked and the game showed it (StoreShowPlayerLine 1) - never for a line not
   shown, held again or without words, so once per line shown */
inline bool HeldLineSounds(const std::vector<unsigned char>& sounds, size_t i, int shown)
{
    return shown == 1 && i < sounds.size() && sounds[i] != 0;
}

/* a player's side towards this player that moved while the two shared a faction, or within kMateQuietMs of the two joining or
   parting: the team's own pin, unpin and restore writes - shown to the player as no notice (only the faction lines are) */
const unsigned long kMateQuietMs = 10000;
inline bool MateQuiet(bool matesNow, unsigned long changedMs, unsigned long nowMs)
{
    return matesNow || (changedMs != 0 && nowMs - changedMs < kMateQuietMs);
}
/* the PLAYERS table waits this long after this player's teammates changed before it is rebuilt, so the stance cells show the
   values the restore settles on, not the pin's for a moment */
const unsigned long kMatesSettleMs = 750;
inline bool MatesSettling(unsigned long changedMs, unsigned long nowMs) { return changedMs != 0 && nowMs - changedMs < kMatesSettleMs; }
/* the invitation box on screen is taken down when no invitation waits, or a newer one replaced it */
inline bool InviteBoxEnds(bool waiting, unsigned serial, unsigned boxSerial) { return !waiting || serial != boxSerial; }

}   /* namespace teamscreen */
