#pragma once
// AN EMPTY TOWN THAT NOTHING ACCOUNTS FOR IS NAMED IN ONE LOG LINE - log only, nothing is repaired.
// The pure half, ONE header compiled into the plugin (towngen.cpp EmptyTownCheck) and the offline suite. At a town's check-up the
// game that runs the town gathers the facts below. Decide says whether the town is not this game's to judge yet (a silent
// reason), has living people, has work in flight that could still bring them, has something on record that accounts for it
// being empty - or nothing does. ShouldSay keeps the line to once per town per world load, again only when the checked answers
// change, re-armed when the town is seen with people or accounted for, and kLineCap lines per world. Line builds the line.
#include <string>
#include <cstdio>

namespace emptytown {

const long long kLineCap = 64;   // [EMPTYTOWN] lines per world

struct Facts
{
    // the gates - the same ones the empty-town refill uses
    int held;            // 1 = this game holds the town's area
    int frozen;          // 1 = the area is frozen (the area map's frozen answer)
    int live;            // 1 = the town's zone is live here and the town is in loaded range
    int settled;         // 1 = held for the settle time
    int pushDone;        // 1 = the world server's opening push and this world's record snapshot are in
    int playerTown;      // 1 = the town's owner is a player's faction or a stand-in (or could not be read)
    int readOk;          // 1 = both town lists, their groups and the character list were read
    // living people
    int listLiving;      // living members of the groups on the town's residents list
    int homeLiving;      // living characters whose group's home town is this town, of no player and no stand-in
    int patrolLiving;    // living members of the groups on the town's patrols list (shown; patrols are not residents)
    // work in flight that could still bring people
    int owedRows;        // owed-population rows for the town (the world server's table, either kind)
    int aside;           // this game's own set-aside work for the town (building residents, its first bar roll)
    int barPend;         // 1 = the town's bar list is set aside, waiting
    int noteHeld;        // 1 = a notebook note holds the town's creation back
    int rebuildQueued;   // the town's groups queued, or watched, for the engine's group rebuild
    // what accounts for an empty town
    int recLiving, recPlacedHere, recWaitingHere, recElsewhere, recUnplaceable, recCreateFailed, recDeleted;   // the town's world records
    int listed;          // groups on both town lists
    int gone;            // list entries whose group no longer resolves
    int deadMembers;     // dead members of the listed groups (a sleeping group: its shortfall from its original size)
    int townDead;        // 1 = the town's own dead flag
    int goneThisLaunch;  // groups of the town this game deleted since it started
    int copies;          // living characters whose home town is this town, in a player's or a stand-in's faction
    int loss;            // 1 = the world server has a recorded loss for the town
    int refilling;       // 1 = this game is refilling the town building by building
    Facts() : held(0), frozen(0), live(0), settled(0), pushDone(0), playerTown(0), readOk(0), listLiving(0), homeLiving(0), patrolLiving(0),
              owedRows(0), aside(0), barPend(0), noteHeld(0), rebuildQueued(0), recLiving(0), recPlacedHere(0), recWaitingHere(0), recElsewhere(0),
              recUnplaceable(0), recCreateFailed(0), recDeleted(0), listed(0), gone(0), deadMembers(0), townDead(0), goneThisLaunch(0), copies(0),
              loss(0), refilling(0) {}
};

enum { kSilentFrozen = 0, kSilentNotHeld, kSilentNotLive, kSettling, kSilentPush, kSilentPlayer, kSilentUnread,
       kNotEmpty, kInFlight, kAccRecords, kAccDeaths, kAccCopies, kAccLoss, kAccRefilling, kUnexplained, kVerdictN };
const int kGatesPass = -1;
inline const char* VerdictName(int v)
{
    static const char* const k[kVerdictN] = { "frozen", "notHeld", "notLive", "settling", "pushNotDone", "playerTown", "unread",
                                               "notEmpty", "inFlight", "records", "deaths", "copies", "loss", "refilling", "unexplained" };
    return (v >= 0 && v < kVerdictN) ? k[v] : "?";
}
inline bool IsSilent(int v) { return v >= kSilentFrozen && v <= kSilentUnread && v != kSettling; }
inline bool IsAccounted(int v) { return v >= kAccRecords && v <= kAccRefilling; }

enum { kInOwedRow = 1, kInAside = 2, kInBarPend = 4, kInNoteHeld = 8, kInRebuild = 16 };
inline int InFlightBits(const Facts& f)
{
    return (f.owedRows > 0 ? kInOwedRow : 0) | (f.aside > 0 ? kInAside : 0) | (f.barPend > 0 ? kInBarPend : 0)
         | (f.noteHeld > 0 ? kInNoteHeld : 0) | (f.rebuildQueued > 0 ? kInRebuild : 0);
}
// The gates alone - the caller reads the town only when they pass (kGatesPass).
inline int GateVerdict(const Facts& f)
{
    if (f.frozen != 0) return kSilentFrozen;
    if (f.held == 0) return kSilentNotHeld;
    if (f.live == 0) return kSilentNotLive;
    if (f.settled == 0) return kSettling;
    if (f.pushDone == 0) return kSilentPush;
    if (f.playerTown != 0) return kSilentPlayer;
    return kGatesPass;
}
// Records that exist but cannot be placed, or whose creation failed, are living records: they account for the town (manager decision).
inline int Decide(const Facts& f)
{
    const int g = GateVerdict(f);
    if (g != kGatesPass) return g;
    if (f.readOk == 0) return kSilentUnread;
    if (f.listLiving > 0 || f.homeLiving > 0) return kNotEmpty;
    if (InFlightBits(f) != 0) return kInFlight;
    if (f.recLiving > 0) return kAccRecords;
    if (f.recDeleted > 0 || f.gone > 0 || f.deadMembers > 0 || f.townDead != 0 || f.goneThisLaunch > 0) return kAccDeaths;
    if (f.copies > 0) return kAccCopies;
    if (f.loss != 0) return kAccLoss;
    if (f.refilling != 0) return kAccRefilling;
    return kUnexplained;
}

// Every checked answer, as one comparable string (the held time is not in it: it changes every look).
inline std::string Sig(const Facts& f)
{
    char b[512];
    _snprintf(b, sizeof(b) - 1, "%d,%d,%d,%d,%d,%d,%d|%d,%d,%d|%d,%d,%d,%d,%d|%d,%d,%d,%d,%d,%d,%d|%d,%d,%d,%d,%d,%d,%d,%d",
              f.held, f.frozen, f.live, f.settled, f.pushDone, f.playerTown, f.readOk, f.listLiving, f.homeLiving, f.patrolLiving,
              f.owedRows, f.aside, f.barPend, f.noteHeld, f.rebuildQueued, f.recLiving, f.recPlacedHere, f.recWaitingHere, f.recElsewhere,
              f.recUnplaceable, f.recCreateFailed, f.recDeleted, f.listed, f.gone, f.deadMembers, f.townDead, f.goneThisLaunch, f.copies,
              f.loss, f.refilling);
    b[sizeof(b) - 1] = 0;
    return std::string(b);
}

// prevSig = what was stored for the town ("" = armed), sig = Sig of this look, linesUsed = lines written in this world.
// *nextSig = what to store: a town with people or accounted for is re-armed (""); an unexplained town stores this look's answers;
// a silent, settling or in-flight look says nothing new and keeps what was stored.
enum { kSayNo = 0, kSaySay, kSayDropped };
inline int ShouldSay(const std::string& prevSig, const std::string& sig, int verdict, long long linesUsed, std::string* nextSig)
{
    if (verdict == kNotEmpty || IsAccounted(verdict)) { if (nextSig != 0) nextSig->clear(); return kSayNo; }
    if (verdict != kUnexplained) { if (nextSig != 0) *nextSig = prevSig; return kSayNo; }
    if (nextSig != 0) *nextSig = sig;
    if (!prevSig.empty() && prevSig == sig) return kSayNo;
    if (linesUsed >= kLineCap) return kSayDropped;
    return kSaySay;
}

// One line: no quote and no control character from a name survives into it.
inline std::string CleanText(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) { if (o[i] == '\'') o[i] = '`'; else if ((unsigned char)o[i] < 0x20) o[i] = ' '; }
    return o;
}
// the records counts (records living, placedHere, elsewhere, unplaceable, createFailed, waitingHere, deleted) cover only the
// notes and records that NAME this town - a record carrying no town name is not counted, so a town whose people were written before
// records carried towns reads as having none. goneLaunch counts this game's death-road deletes only.
inline std::string Line(const std::string& name, const std::string& sid, int sx, int sy, long long heldS, unsigned modStamp, unsigned sp, unsigned ws, const Facts& f)
{
    char head[160];
    _snprintf(head, sizeof(head) - 1, " area %d,%d held %llds mod=%08X sp=%u ws=%u", sx, sy, heldS, modStamp, sp, ws);
    head[sizeof(head) - 1] = 0;
    char body[640];
    _snprintf(body, sizeof(body) - 1,
              " | living list=%d home=%d patrols=%d | groups listed=%d gone=%d dead=%d townDead=%d"
              " | records living=%d placedHere=%d elsewhere=%d unplaceable=%d createFailed=%d | loss=%d refilling=%d"
              " | owed rows=%d aside=%d barPend=%d | copies=%d | also noteHeld=%d rebuild=%d waitingHere=%d deleted=%d goneLaunch=%d",
              f.listLiving, f.homeLiving, f.patrolLiving, f.listed, f.gone, f.deadMembers, f.townDead,
              f.recLiving, f.recPlacedHere, f.recElsewhere, f.recUnplaceable, f.recCreateFailed, f.loss, f.refilling,
              f.owedRows, f.aside, f.barPend, f.copies, f.noteHeld, f.rebuildQueued, f.recWaitingHere, f.recDeleted, f.goneThisLaunch);
    body[sizeof(body) - 1] = 0;
    return "[EMPTYTOWN] unexplained empty town '" + CleanText(name) + "' (" + CleanText(sid) + ")" + head + body;
}

}   // namespace emptytown
