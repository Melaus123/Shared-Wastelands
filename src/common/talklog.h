/* src/common/talklog.h - THE CONVERSATION WINDOW LOG'S LINES, CAP AND STEP SORTING (owner decision 1410 c, T-978).

   WHAT IT IS FOR. A player may report "my character answered an NPC by itself". The log then has to show, for THIS game's own
   conversation window (it shows only conversations of this game's own player's characters), which of these happened:
     - the game's own next-line step said the next line with no reply clicked since the line before (by=engine), while the
       window offered replies as the step began (offered=n) - the game moved on without the player's choice; the step may run on
       the window's own conversation object (obj=window) or on this game's own character's object in that conversation
       (obj=partner: the character's own line is said there);
     - a reply was clicked in this window (CLICK src=window, then the next line by=click);
     - the window is a copy of a conversation another game runs and the click went back there (src=mirror), or the mod made the
       step itself to copy the other game's conversation (src=mod / by=mod / mod=1);
     - the window closed early while the game was running (CLOSE open=<seconds> paused=0).
   Each line carries the pause byte (paused=1 the game is paused, 0 running, -1 unreadable) and the thread (main / off).

   THE LINES (speech.cpp writes them; at most kCap a launch, then CapLine once):
     [TALKLOG] OPEN #<n> by=<setInDialog|nextLine|click> npc='<name>' talker='<name>|?' talkerMine=<1|0|-1> mod=<0|1> paused= thread=
     [TALKLOG] NEXTLINE #<n> at=<s since open> obj=<window|partner> by=<engine|click|mod> after=<s since the line, click or open
               before> offered=<replies the window offered as the step began> text='<the line on the stepped object>' replies=<n
               now> ended=<0|1> window=<1|0|-1> paused= thread=
     [TALKLOG] CLICK #<n> at=<s> index=<i> replies=<n> reply='<its text>' src=<window|mirror|mod> paused= thread=
     [TALKLOG] END #<n> at=<s> fin=<0|1> mod=<0|1> paused= thread=
     [TALKLOG] CLOSE #<n> by=<setInDialog(false)|replaced|gone> open=<s> nextLines=<n> partnerLines=<n> otherNextLines=<n> clicks=<n>
               ends=<n> paused= thread=       gone = the window no longer shows that conversation object (freed or swapped
               without a close): the record is dropped at the next step or hook that sees it, so a reused address logs nothing
     [TALKLOG] cap reached - no more conversation window lines this launch (<kCap>)

   Pure: no engine types. C++03. */
#pragma once

#include <string>
#include <cstdio>

namespace talklog {

const long kCap = 200;   /* lines a launch, then one cap line */

/* k = this line's number this launch, counted before it is written (1 = the first): 0 drop it, 1 write it, 2 write it and then
   the cap line (the last one allowed). */
inline int CapStep(long k)
{
    if (k < 1 || k > kCap) return 0;
    return (k == kCap) ? 2 : 1;
}

/* Seconds, one decimal, from two millisecond tick counts (32-bit, wraps like GetTickCount). */
inline std::string Sec(unsigned int nowMs, unsigned int thenMs)
{
    char b[32];
    std::sprintf(b, "%.1f", (double)(unsigned int)(nowMs - thenMs) / 1000.0);
    return std::string(b);
}

/* A game string on one log line: line breaks and tabs become spaces, a quote becomes a backtick (the line's own quotes stay
   unambiguous), at most maxLen characters. */
inline std::string Clean(const std::string& s, size_t maxLen)
{
    std::string r = s.substr(0, maxLen);
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r[i] == '\r' || r[i] == '\n' || r[i] == '\t') r[i] = ' ';
        else if (r[i] == '\'') r[i] = '`';
    }
    return r;
}

inline std::string Num(long v) { char b[24]; std::sprintf(b, "%ld", v); return std::string(b); }

/* " paused=<p> thread=<main|off>" - every line's tail */
inline std::string Tail(int paused, int mainThread)
{
    return " paused=" + Num(paused < 0 ? -1 : (paused != 0 ? 1 : 0)) + " thread=" + (mainThread ? "main" : "off");
}

/* NEXTLINE's by=: the mod's own step on a copied conversation, a reply clicked since the line before, else the game's own step */
inline const char* SaidBy(int byMod, int clickedSince)
{
    return byMod ? "mod" : (clickedSince ? "click" : "engine");
}

/* CLICK's src=: the mod's own call on a copied conversation, a click in a window that copies another game's conversation (it
   goes back to that game), else this game's own window (the player's click or key) */
inline const char* ClickSrc(int byMod, int mirror)
{
    return byMod ? "mod" : (mirror ? "mirror" : "window");
}

/* What a next-line step is to the log. dlg: the stepped conversation object; shown: what the window shows now (0 none, (void*)1
   the window's address is not known); rec: the open record's object (0 none); partner: 1 when dlg's owner is this game's own
   character in shown's conversation (the caller asks only for kStepOther, then passes 1 to get kStepPartner).
     kStepNone    nothing to log (no record and not the window's object, or the window cannot be read)
     kStepWindow  the window's own object (its record, opened now when there is none)
     kStepPartner this game's own character's object in the window's conversation
     kStepOther   another conversation while the window is up (counted, not printed)
     kStepGone    the record's object is no longer what the window shows: close the record (by=gone), log nothing else */
enum { kStepNone = 0, kStepWindow = 1, kStepPartner = 2, kStepOther = 3, kStepGone = 4 };
inline int StepKind(const void* dlg, const void* shown, const void* rec, int partner)
{
    if (dlg == 0 || shown == (const void*)1) return kStepNone;
    if (shown != 0 && dlg == shown) return kStepWindow;
    if (rec == 0) return kStepNone;
    if (rec != shown) return kStepGone;
    return partner ? kStepPartner : kStepOther;
}

inline const char* ObjWord(int partner) { return partner ? "partner" : "window"; }

/* talker: "" = not known (prints ?); talkerMine -1 when not known */
inline std::string OpenLine(long n, const char* by, const std::string& npc, const std::string& talker, int talkerMine, int byMod,
                            int paused, int mainThread)
{
    return "[TALKLOG] OPEN #" + Num(n) + " by=" + by + " npc='" + Clean(npc, 60) + "' talker='"
         + (talker.empty() ? std::string("?") : Clean(talker, 60)) + "' talkerMine=" + Num(talkerMine < 0 ? -1 : (talkerMine ? 1 : 0))
         + " mod=" + Num(byMod ? 1 : 0) + Tail(paused, mainThread);
}

inline std::string NextLineLine(long n, const std::string& at, int partner, int byMod, int clickedSince, const std::string& after, long offered,
                                const std::string& text, int replies, int ended, int window, int paused, int mainThread)
{
    return "[TALKLOG] NEXTLINE #" + Num(n) + " at=" + at + " obj=" + ObjWord(partner) + " by=" + SaidBy(byMod, clickedSince) + " after=" + after + " offered="
         + Num(offered) + " text='" + Clean(text, 80) + "' replies=" + Num(replies) + " ended=" + Num(ended ? 1 : 0) + " window="
         + Num(window < 0 ? -1 : (window ? 1 : 0)) + Tail(paused, mainThread);
}

inline std::string ClickLine(long n, const std::string& at, int index, int replies, const std::string& reply, int byMod, int mirror,
                             int paused, int mainThread)
{
    return "[TALKLOG] CLICK #" + Num(n) + " at=" + at + " index=" + Num(index) + " replies=" + Num(replies) + " reply='"
         + Clean(reply, 60) + "' src=" + ClickSrc(byMod, mirror) + Tail(paused, mainThread);
}

inline std::string EndLine(long n, const std::string& at, int fin, int byMod, int paused, int mainThread)
{
    return "[TALKLOG] END #" + Num(n) + " at=" + at + " fin=" + Num(fin ? 1 : 0) + " mod=" + Num(byMod ? 1 : 0) + Tail(paused, mainThread);
}

inline std::string CloseLine(long n, const char* by, const std::string& open, long nextLines, long partnerLines, long otherNextLines,
                             long clicks, long ends, int paused, int mainThread)
{
    return "[TALKLOG] CLOSE #" + Num(n) + " by=" + by + " open=" + open + " nextLines=" + Num(nextLines) + " partnerLines="
         + Num(partnerLines) + " otherNextLines=" + Num(otherNextLines) + " clicks=" + Num(clicks)
         + " ends=" + Num(ends) + Tail(paused, mainThread);
}

inline std::string CapLine()
{
    return "[TALKLOG] cap reached - no more conversation window lines this launch (" + Num(kCap) + ")";
}

}   /* namespace talklog */
