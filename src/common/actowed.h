/* src/common/actowed.h - AN ACT ON AN AWAY PLAYER'S CHARACTER IS OWED UNTIL THE PLAYER IS BACK (T-860 / T-859, OWNER DECISION 1551 a).
 *
 * A game that puts another game's character (its copy here) in a bed or a cage, lets it out of a cage, opens its restraint, or ends
 * the carry of its own body by another game's carrier sends one request to that character's OWNER. When the owner's game is gone
 * (crashed, or relaunching - a game cannot load while another player is in the world) the request reaches nobody, and the
 * relaunched game rebuilds its world with NEW uids, so a request kept for the old uid names nobody afterwards.
 *
 * The acting game keeps each such act as OWED, keyed by the owner's seat, the character's LASTING handle (the owner engine's hand
 * serial + type, which its save keeps: ownrec.h "the engine keeps no other character uid") and the act's FAMILY:
 *   - PLACE (where the character is held: caged, let out, put in a bed, picked up out of it, a carry ended) - ONE row per character:
 *     the newest act of the family replaces the older one, so an older opposite act is never sent over a newer one (fold 1, HIGH-1);
 *     a pick-up here (no message of its own) ends an owed BED IN only - a release or a caging stays owed (fold 2, NEW-1);
 *   - each restraint lock (shackles, cage) - its own row.
 * The table is capped (an act over the cap is dropped, counted and logged). Nothing here runs on a clock - every step is an event:
 *   - each owner STATE of the copy the act is bound to is counted and judged (Step);
 *   - the owner's REFUSED answer for the act ends it at once; a "may pass" answer to a re-send asks again at the next STATE, at most
 *     kMayPassMax times, then the act ends (Answer, fold 2 NEW-2);
 *   - the owner's new copy (its SPAWN; its CONTEXT names the same handle) re-binds the act ONLY when the owner's world was loaded
 *     after the world the act's copy came from (it came back - fold 2 NEW-4: an act made after the load, before the new copy, is
 *     carried over too); a copy re-made while the owner stayed (a region reload) ends the act (Resolve, fold 1 MED-2);
 *   - this game's world teardown or link loss clears the table.
 * Kills are not owed (owner 1551: a kill made while the owner is away stays dropped; a relaunch can bring back up to ~30 s).
 *
 * Pure: no engine memory, no globals, no Windows; the offline suite drives it. C++03 (VS2010 v100).
 */
#pragma once

#include <map>
#include <utility>
#include <vector>

namespace actowed {

const int kKindCageIn      = 1;   /* the copy was caged here (PRISON IN) */
const int kKindCageOut     = 2;   /* the copy was let out of its cage here (PRISON RELEASE) */
const int kKindBedIn       = 3;   /* the copy was put in a bed here (PRISON BED IN) */
const int kKindShackleLock = 4;   /* the copy's shackles were opened here (PRISON SHACKLE LOCK request) */
const int kKindCarryEnd    = 5;   /* this game's own body's carry by another game's carrier ended here (CARRY_BREAK; keyed by the carrier) */
const int kKindCageLock    = 6;   /* the lock of the cage the copy sits in was opened here (PRISON CAGE LOCK request) */
const int kKindMax         = 6;

const int kFamPlace       = 1;    /* cage in / release / bed in / pick-up from a bed / carry end: one row per character */
const int kFamShackleLock = 2;
const int kFamCageLock    = 3;
inline int FamilyOf(int kind)
{
    return kind == kKindShackleLock ? kFamShackleLock : kind == kKindCageLock ? kFamCageLock : (kind >= 1 && kind <= kKindMax) ? kFamPlace : 0;
}

const int kCap        = 256;   /* owed acts kept at once; one over the cap is dropped and counted (capDropped) */
const int kStatesSame = 10;    /* owner STATEs on the copy the act was made on without the act: the owner was there and decided */
const int kStatesNew  = 3;     /* owner STATEs on the owner's NEW copy before a missing act is judged missing / a re-send unanswered */
const int kMayPassMax = 2;     /* "may pass" answers to a re-send that are asked again; the next one ends the act */
const unsigned int kSameLoadMs = 5000;   /* two load moments this close are one world load (each is read off a CONTEXT's age; the link's delay moves it) */

const int kShowsNo      = 0;   /* the owner's state lacks the act */
const int kShowsYes     = 1;   /* the owner's state shows the act */
const int kShowsUnknown = -1;  /* the owner's state does not say (no pose, no lock word yet) */

const int kStepWait    = 0;
const int kStepSettled = 1;    /* the act is in force on the owner: nothing to do */
const int kStepResend  = 2;    /* send the act (again) to the new copy's uid */
const int kStepEnded   = 3;    /* the owner's word stands (a later change, its decision, its refusal, or nothing to judge by) */

inline const char* KindName(int k)
{
    return k == kKindCageIn ? "cageIn" : k == kKindCageOut ? "cageOut" : k == kKindBedIn ? "bedIn" : k == kKindShackleLock ? "shackleLock"
         : k == kKindCarryEnd ? "carryEnd" : k == kKindCageLock ? "cageLock" : "?";
}
inline const char* StepName(int s)
{
    return s == kStepSettled ? "settled" : s == kStepResend ? "resent" : s == kStepEnded ? "ended" : "wait";
}

struct Key
{
    int slot;              /* the owner's seat */
    unsigned int serial;   /* the owner engine's hand serial of the character (the carrier for kKindCarryEnd) */
    unsigned int type;     /* its hand type */
    int fam;               /* kFam* */
    Key() : slot(-1), serial(0), type(0), fam(0) {}
    Key(int s, unsigned int se, unsigned int t, int f) : slot(s), serial(se), type(t), fam(f) {}
    bool operator<(const Key& o) const
    {
        if (slot != o.slot) return slot < o.slot;
        if (serial != o.serial) return serial < o.serial;
        if (type != o.type) return type < o.type;
        return fam < o.fam;
    }
};
inline bool KeyUsable(const Key& k) { return k.slot >= 0 && k.serial != 0 && k.fam >= kFamPlace && k.fam <= kFamCageLock; }

struct Counts
{
    long long noted, resolved, settled, resent, ended, capDropped, cleared;
    Counts() : noted(0), resolved(0), settled(0), resent(0), ended(0), capDropped(0), cleared(0) {}
};

/* P: what the plugin needs to send the act again (its request, the body of a carry) */
template <class P>
struct Entry
{
    int kind;              /* kKind* - the newest act of the family */
    unsigned int oldUid;   /* the copy the act was made on */
    unsigned int newUid;   /* the owner's new copy of the same character after its return, 0 = not arrived */
    unsigned int notedAt;  /* this game's tick count when the act was made */
    unsigned int copyLoadAt;  /* the owner's world-load moment the copy the act was made on came from (notedAt when not known) */
    int states;            /* owner STATEs counted on the bound copy since the act, the re-bind or the last send */
    int sends;             /* sends to the new copy */
    bool reask;            /* the owner answered a send "may pass": ask again at its next STATE */
    int mayPass;           /* "may pass" answers to sends so far */
    P payload;
    Entry() : kind(0), oldUid(0), newUid(0), notedAt(0), copyLoadAt(0), states(0), sends(0), reask(false), mayPass(0), payload() {}
};
template <class P> inline unsigned int BoundUid(const Entry<P>& e) { return e.newUid != 0 ? e.newUid : e.oldUid; }

template <class P>
struct Table
{
    std::map<Key, Entry<P> > rows;
    Counts n;
};

/* `a` happened after `b` on one wrapping millisecond clock */
inline bool After(unsigned int a, unsigned int b) { return (int)(a - b) > 0; }

/* An act of `kind` made on the copy `uid` at `at`; copyLoadKnown / copyLoadAt: the owner's world-load moment that copy came from
   (its CONTEXT's load age), else the act's own time stands in. The newest act of a key's family replaces the older one (an opposite act
   included); the same act repeated on the OLD copy after the owner's new copy arrived (an old copy's own re-ask) refreshes the
   request and keeps the new copy's binding. 1 kept, 0 refused (no usable key or kind, or the table is full - counted). */
template <class P>
inline int Note(Table<P>* t, const Key& k, int kind, unsigned int uid, unsigned int at, bool copyLoadKnown, unsigned int copyLoadAt, const P& p)
{
    if (t == 0 || uid == 0 || !KeyUsable(k) || FamilyOf(kind) != k.fam) return 0;
    typename std::map<Key, Entry<P> >::iterator it = t->rows.find(k);
    if (it != t->rows.end())
    {
        Entry<P>& e = it->second;
        if (e.newUid != 0 && uid == e.oldUid && kind == e.kind) { e.payload = p; return 1; }
        e.kind = kind; e.oldUid = uid; e.newUid = 0; e.notedAt = at; e.copyLoadAt = copyLoadKnown ? copyLoadAt : at;
        e.states = 0; e.sends = 0; e.reask = false; e.mayPass = 0; e.payload = p;
        ++t->n.noted;
        return 1;
    }
    if ((int)t->rows.size() >= kCap) { ++t->n.capDropped; return 0; }
    Entry<P> e; e.kind = kind; e.oldUid = uid; e.notedAt = at; e.copyLoadAt = copyLoadKnown ? copyLoadAt : at; e.payload = p;
    t->rows.insert(std::make_pair(k, e));
    ++t->n.noted;
    return 1;
}

/* The act of key `k` is done with step `s` (not kStepWait): counted and removed. */
template <class P>
inline void Finish(Table<P>* t, const Key& k, int s)
{
    if (t == 0) return;
    typename std::map<Key, Entry<P> >::iterator it = t->rows.find(k);
    if (it == t->rows.end()) return;
    if (s == kStepSettled) ++t->n.settled; else if (s == kStepEnded) ++t->n.ended; else return;
    t->rows.erase(it);
}

/* A pick-up of the character by this game's carrier (it sends nothing of its own): an owed BED IN of that row ends - a release or a
   caging stays owed (fold 2, NEW-1: a rescuer that lets a prisoner out and then carries it off still owes the release). 1 when one did. */
template <class P>
inline int SupersedeBedIn(Table<P>* t, const Key& k)
{
    if (t == 0) return 0;
    typename std::map<Key, Entry<P> >::const_iterator it = t->rows.find(k);
    if (it == t->rows.end() || it->second.kind != kKindBedIn) return 0;
    Finish(t, k, kStepEnded);
    return 1;
}

/* The owner's copy `newUid` of the character (seat, serial, type) arrived. ownerLoadKnown / ownerLoadAt: the moment the owner's
   current world was loaded, on this game's clock. An act whose copy came from an OLDER world load (the owner came back since) is bound
   to the new copy - an act made after the load but on the old copy too (fold 2, NEW-4); an act on a copy of the same world load (a
   region reload re-made the copy while the owner stayed, or the load moment is not known) ends: it was applied or refused there. Ended rows are copied to `ended` (for the line) and counted. Returns how many were bound. */
template <class P>
inline int Resolve(Table<P>* t, int slot, unsigned int serial, unsigned int type, unsigned int newUid, bool ownerLoadKnown,
                   unsigned int ownerLoadAt, std::vector<std::pair<Key, Entry<P> > >* ended)
{
    if (t == 0 || newUid == 0 || serial == 0) return 0;
    int n = 0;
    std::vector<Key> gone;
    for (typename std::map<Key, Entry<P> >::iterator it = t->rows.begin(); it != t->rows.end(); ++it)
    {
        if (it->first.slot != slot || it->first.serial != serial || it->first.type != type) continue;
        Entry<P>& e = it->second;
        if (newUid == BoundUid(e)) continue;   /* a repeat SPAWN of the copy it is bound to */
        if (ownerLoadKnown && After(ownerLoadAt, e.copyLoadAt + kSameLoadMs))
        {
            e.newUid = newUid; e.states = 0; e.sends = 0; e.reask = false;
            ++t->n.resolved; ++n;
        }
        else
        {
            if (ended) ended->push_back(std::make_pair(it->first, e));
            gone.push_back(it->first);
        }
    }
    for (size_t i = 0; i < gone.size(); ++i) Finish(t, gone[i], kStepEnded);
    return n;
}

/* One owner STATE judged for an act. onNew: the act is bound to the owner's new copy. statesAfter: the owner STATEs counted on the
   bound copy since the act / re-bind / last send, this one included. sends / reask: the entry's. */
inline int Step(bool onNew, int shows, bool laterChange, int statesAfter, int sends, bool reask)
{
    if (shows == kShowsYes) return kStepSettled;
    if (laterChange) return kStepEnded;
    if (sends > 0)
    {
        if (reask) return kStepResend;                                  /* the owner said "may pass": asked again now */
        return statesAfter < kStatesNew ? kStepWait : kStepEnded;      /* no answer and not shown: its word stands */
    }
    if (statesAfter < (onNew ? kStatesNew : kStatesSame)) return kStepWait;
    if (!onNew) return kStepEnded;
    return shows == kShowsNo ? kStepResend : kStepEnded;
}
/* After a send: the count starts again and a "may pass" is spent. */
template <class P> inline void MarkSent(Table<P>* t, Entry<P>* e) { if (e == 0) return; ++e->sends; e->states = 0; e->reask = false; if (t) ++t->n.resent; }

/* The owner answered the act with a refusal. final: it will not do it (REFUSED, BED REFUSED permanent) - the act ends now; may
   pass (BED REFUSED for now: awake, still carried, the bed not found yet) to a re-send: asked again at the owner's next STATE, at most
   kMayPassMax times - the next one ends it (fold 2, NEW-2: no endless asking, no bed-in landing hours later); to the first send:
   its own asker handles it. */
template <class P> inline int Answer(Entry<P>* e, bool final)
{
    if (e == 0) return kStepWait;
    if (final) return kStepEnded;
    if (e->sends == 0) return kStepWait;
    if (++e->mayPass > kMayPassMax) return kStepEnded;
    e->reask = true;
    return kStepWait;
}

/* World teardown or this game's link loss: every owed act is forgotten (counted). */
template <class P>
inline void Clear(Table<P>* t)
{
    if (t == 0) return;
    t->n.cleared += (long long)t->rows.size();
    t->rows.clear();
}

}   /* namespace actowed */
