/* src/common/uniqslot.h - the rules that keep a named (unique) character from existing twice in one world, or coming
 * back from the dead through a group made from a record. Pure: no engine memory, no Windows. The offline suite
 * (src/coop-test/test_main.cpp) checks the same code the plugin runs (src/coop-plugin/worldstate.cpp).
 *
 * THE ENGINE'S TABLE (read from the 1.0.65 exe): one entry per named character, keyed by its template record. Entry +0x8 is
 * the "made" slot (the template, once a copy of it has been made or used in this game), +0x30 the state (0 dead, 1 alive,
 * 2 imprisoned). The engine's own check before it makes a named character (0x591720, asked by the character maker 0x582C50
 * and by the war-party builder 0x9C4170) says "may make" when there is no entry or the slot is empty; it never reads the
 * state. The plugin never writes this table for a character that is alive or imprisoned in another game: it answers that
 * check itself, from a set it keeps in memory only (nothing of it is saved).
 *
 * FIRST SIGHT OF AN ALIVE NAMED CHARACTER (FirstSightAlive). The drain meets a record it has no shadow row for, and the
 *    table reads ALIVE. It is published only when the world server's opening data for this world has been applied here
 *    (before that, "the world server does not know it" cannot be answered, so the record is asked again on its next pass),
 *    the world server has named no state for that id, and the character carrying it here is this game's own and alive.
 *    It is also asked again while no character here carries it, while the carrier's owner cannot be established yet (a
 *    character the mod has not registered), and while the carrier's life cannot be read - the first look can come before
 *    the character is registered as this game's own, and a record learned silently then would never be published.
 *    A carrier that is another game's, or this game's own already-known or dead one, is learned silently. (An IMPRISONED or DEAD first sight goes through the drain's general first-sight rule.)
 * THE ELSEWHERE SET (ElsewhereMember). A template is in it when the world server's opening data has been applied, its
 *    shadow row says ALIVE or IMPRISONED, that row is a state received from another game and applied here (never one this
 *    game learned, published or changed itself - a respawn that erased the entry here must be able to make the character
 *    again), and no character in this game carries it (alive or dead, whoever owns it). The engine's "may make" check
 *    answers "may not" for a template in the set, so this game's own makers do not make a second copy of a character that
 *    lives in another game.
 * THE DEAD SET (DeadSetMember). A template is in it when its shadow row says DEAD, it is not a character brought back
 *    from the dead, and no living character this game owns carries it.
 * A MEMBER OF A GROUP MADE FROM A RECORD (MemberVerdict). The engine makes each member of a group loaded from its records
 *    (ActivePlatoon vt+0x38) without the "may make" check. A member is skipped when its template is in the dead set, the
 *    template is flagged unique, and the member is not a dead body in its record. When the load is told it is the group's
 *    first time (its third argument non-zero) the engine makes the member fresh from its template with no saved state, so
 *    there is no body: the member is skipped then too. A body still loads (it carries its loot); an unreadable answer
 *    anywhere makes the member.
 * TWO LIVING BODIES OF ONE NAMED CHARACTER (DupDecide). The body held by the game that runs its area stays; a game removes
 *    only an unprotected body it owns in an area it runs; a player's squad member, a carried or caged body and a prisoner are
 *    never removed.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

namespace swuniq {

/* ---- first sight of an ALIVE named character ---- */
const int kFirstSightLearn = 0;     // learn it silently: a shadow row, nothing sent
const int kFirstSightDefer = 1;     // no shadow row: the record is asked again on its next pass
const int kFirstSightPublish = 2;   // send ALIVE, then the shadow row
/* snapshotApplied: the world server's opening data for this world has been applied here. inFeed: the world server has
   named a state for this id. ownKind: kFsOwnNone no character here carries the record, kFsOwnUnknown one does but its
   owner cannot be established yet, kFsOwnMine this game's own, kFsOwnOther another game's (a copy or a twin).
   carrierAlive: 1 living, 0 dead, -1 unreadable. */
const int kFsOwnNone = 0, kFsOwnUnknown = 1, kFsOwnMine = 2, kFsOwnOther = 3;
inline int FirstSightAlive(int snapshotApplied, int inFeed, int ownKind, int carrierAlive)
{
    if (snapshotApplied == 0) return kFirstSightDefer;
    if (ownKind == kFsOwnOther) return kFirstSightLearn;
    if (ownKind != kFsOwnMine) return kFirstSightDefer;
    if (inFeed != 0) return kFirstSightLearn;
    if (carrierAlive < 0) return kFirstSightDefer;
    return carrierAlive == 1 ? kFirstSightPublish : kFirstSightLearn;
}

/* ---- the elsewhere set: 1 in it, 0 not ----
   snapshotApplied: as above. state: the shadow row's state (0 dead, 1 alive, 2 imprisoned). remote: 1 the row's latest
   write is a state received from another game and applied here, 0 this game's own. carried: 1 a character in this game
   carries the template (alive or dead, whoever owns it), 0 none. */
inline int ElsewhereMember(int snapshotApplied, int state, int remote, int carried)
{
    if (snapshotApplied == 0) return 0;
    if (state != 1 && state != 2) return 0;
    if (remote != 1) return 0;
    if (carried != 0) return 0;
    return 1;
}

/* ---- the dead set: 1 in it, 0 not ----
   state: the shadow row's state. confirmed: 1 the DEAD is the world's word - a state received from another game (or the
   world server) and applied here, or one this game published as the character's owner; 0 a reading of this game's own
   table nobody confirmed (the engine also writes DEAD when it puts some groups away - a living character must never be
   skipped on such a reading). broughtBack: 1 the template is a character brought back from the dead (by this game, or
   the world server says so). livingMine: 1 a living character this game owns carries the template. */
inline int DeadSetMember(int state, int confirmed, int broughtBack, int livingMine)
{
    if (state != 0) return 0;
    if (confirmed != 1) return 0;
    if (broughtBack != 0) return 0;
    if (livingMine != 0) return 0;
    return 1;
}

/* ---- a member of a group made from a record ---- */
const int kMemberMake = 0;
const int kMemberSkip = 1;
/* inDeadSet: 1 the member's template is in the dead set, 0 not, -1 the set could not be asked.
   unique: the template's own "unique" flag - 1 unique, 0 not, -1 the read faulted.
   firstTime: the load's third argument - non-zero, the member is made fresh from its template and its record is not read.
   memberDead: the member's own record - 1 a dead body, 0 alive (no medical record or no "dead" field reads alive, as the
   engine's own load reads it), -1 the read faulted. Ignored when firstTime is non-zero. */
inline int MemberVerdict(int inDeadSet, int unique, int firstTime, int memberDead)
{
    if (inDeadSet != 1) return kMemberMake;
    if (unique != 1) return kMemberMake;
    if (firstTime != 0) return kMemberSkip;
    if (memberDead != 0) return kMemberMake;
    return kMemberSkip;
}

/* ---- two or more LIVING bodies of one named character in this game (DupDecide) ----
   The game that runs a body's area is the only game that removes it: this game removes a body only when it owns it AND runs
   its area, never another game's copy, never a protected body, and nothing while any answer is missing. A body this game owns
   in an area another game runs is never removed here: it waits (after a hand-over the game that runs the area owns it and
   judges it, its own protections included). Which body is kept: a protected one when there is one; else the one held by the
   game that runs its area; with two or more held by this game, the one whose squad the engine's table names, else one in a
   squad the engine made, else the lower uid. A protected body is a member of any player's faction (this game's own, or another
   player's stand-in faction) or the watched player, a carried body, one in a bed or a cage, and every body of a character whose
   table state is imprisoned.
   kProtectedIsKept: 1 a protected body is the kept one - the other bodies this game owns and may remove go, and two protected
   bodies remove nothing; 0 any protected body removes nothing. */
const int kProtectedIsKept = 1;
struct DupBody
{
    unsigned int uid;
    int mine;          // 1 this game owns the body's uid
    int owner;         // the slot of the game that owns the body (this game's own slot when mine), -1 unknown
    int runner;        // the slot of the game that runs the body's area, -1 no fresh area map
    int prot;          // 1 protected (see above), 0 not
    int tableHolds;    // 1 the engine's table names this body's squad as the character's, 0 not, -1 unread
    int engineSquad;   // 1 the body is in a squad the engine made, 0 in a stand-in squad or none
};
const int kDupNone = 0;            // fewer than two bodies
const int kDupRemove = 1;          // at least one body this game owns, in an area it runs, is marked for removal
const int kDupProtectedKept = 2;   // protection decided and nothing is removed
const int kDupCrossGame = 3;       // bodies held in areas run by different games: nothing removed (not built)
const int kDupWait = 4;            // an owner or an area runner unknown, no body held by its area's runner, or an extra body of
                                   // this game's own stands in an area another game runs
const int kDupOthersOwn = 5;       // a body is kept and nothing this game owns is to go (the game running the area decides)
const int kKeepNone = 0, kKeepRunner = 1, kKeepProtected = 2, kKeepTable = 3, kKeepEngineSquad = 4, kKeepLowerUid = 5;

/* b[0..n): the living bodies. keep: the index kept (-1 none). keepWhy: kKeep*. remove[0..n): 1 this game removes that body.
   protectedIsKept: kProtectedIsKept, passed so both rules are testable. */
inline int DupDecide(const DupBody* b, int n, int mySlot, int protectedIsKept, int* keep, int* keepWhy, int* remove)
{
    *keep = -1; *keepWhy = kKeepNone;
    for (int i = 0; i < n; ++i) remove[i] = 0;
    if (n < 2) return kDupNone;
    if (mySlot < 0) return kDupWait;
    for (int i = 0; i < n; ++i) if (b[i].owner < 0 || b[i].runner < 0) return kDupWait;   /* no fresh area map: nothing is decided */
    int prot = 0, protAt = -1;
    for (int i = 0; i < n; ++i) if (b[i].prot != 0) { ++prot; if (protAt < 0) protAt = i; }
    int kept = -1, why = kKeepNone;
    if (prot > 0)
    {
        if (protectedIsKept == 0 || prot >= 2) { *keep = protAt; *keepWhy = kKeepProtected; return kDupProtectedKept; }
        /* a protected body kept in place of this game's own must be this game's own: another game's copy may read protected here
           only (a carry not yet mirrored to its owner), and that game may be keeping this game's body for the same reason */
        if (b[protAt].mine == 0) { *keep = protAt; *keepWhy = kKeepProtected; return kDupWait; }
        kept = protAt; why = kKeepProtected;
    }
    else
    {
        int held = 0, heldOwner = -1, crossGame = 0;
        for (int i = 0; i < n; ++i)
        {
            if (b[i].owner != b[i].runner) continue;
            if (held != 0 && b[i].owner != heldOwner) crossGame = 1;
            ++held; heldOwner = b[i].owner;
        }
        if (held == 0) return kDupWait;
        if (crossGame != 0) return kDupCrossGame;
        if (held == 1)
        {
            for (int i = 0; i < n; ++i) if (b[i].owner == b[i].runner) kept = i;
            why = kKeepRunner;
        }
        else
        {
            if (heldOwner != mySlot) return kDupOthersOwn;   /* that game keeps one of its own and removes the rest */
            for (int i = 0; i < n; ++i)
            {
                if (b[i].owner != b[i].runner) continue;
                if (kept < 0) { kept = i; continue; }
                const DupBody& k = b[kept];
                const DupBody& c = b[i];
                if ((c.tableHolds == 1) != (k.tableHolds == 1)) { if (c.tableHolds == 1) kept = i; continue; }
                if ((c.engineSquad == 1) != (k.engineSquad == 1)) { if (c.engineSquad == 1) kept = i; continue; }
                if (c.uid < k.uid) kept = i;
            }
            /* why the kept one won over the others */
            why = kKeepLowerUid;
            for (int i = 0; i < n; ++i)
            {
                if (i == kept || b[i].owner != b[i].runner) continue;
                if (b[kept].tableHolds == 1 && b[i].tableHolds != 1) { why = kKeepTable; break; }
                if (b[kept].tableHolds == b[i].tableHolds && b[kept].engineSquad == 1 && b[i].engineSquad != 1 && why == kKeepLowerUid) why = kKeepEngineSquad;
            }
        }
    }
    *keep = kept; *keepWhy = why;
    int removed = 0, ownElsewhere = 0;
    for (int i = 0; i < n; ++i)
    {
        if (i == kept || b[i].mine == 0 || b[i].prot != 0) continue;
        if (b[i].runner == mySlot) { remove[i] = 1; ++removed; }
        else ownElsewhere = 1;   /* this game's own extra body in an area another game runs: not removed here */
    }
    if (removed != 0) return kDupRemove;
    return ownElsewhere != 0 ? kDupWait : kDupOthersOwn;
}

/* the words for a log line */
inline const char* DupKeepWhyText(int why)
{
    switch (why)
    {
    case kKeepRunner: return "held by the game that runs its area";
    case kKeepProtected: return "protected";
    case kKeepTable: return "the engine's table names its squad";
    case kKeepEngineSquad: return "in a squad the engine made";
    case kKeepLowerUid: return "the lower uid";
    default: return "none";
    }
}
inline const char* DupResultText(int r)
{
    switch (r)
    {
    case kDupRemove: return "remove";
    case kDupProtectedKept: return "protected kept, nothing removed";
    case kDupCrossGame: return "held in areas run by different games, nothing removed";
    case kDupWait: return "waiting (an owner or an area's runner unknown, no body held by its area's runner, or this game's own extra body stands in an area another game runs)";
    case kDupOthersOwn: return "nothing of this game's own to remove";
    default: return "one body";
    }
}

}   /* namespace swuniq */
