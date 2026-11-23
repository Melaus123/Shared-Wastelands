/* src/common/copybody.h - THE COPY BODY-REBUILD RULE: a replicated copy's body is never rebuilt while the engine is using it.
 *
 * ONE RULE, ownedmirror.h's: nothing in here reads engine memory, reads a global, calls the operating system or
 * includes a header, so the offline suite drives it directly.
 *
 * WHY. A body rebuild (AppearanceHuman::createBody / AppearanceAnimal::createBody - the only appearance step that
 * replaces the body entity) destroys the old body's scene node and calls set-body with no body, which deletes the
 * get-up blend and clears the animation object's body fields. Two engine states make that fatal:
 *  - INSIDE THE ENGINE'S RAGDOLL PASS (ThreadSafeRagdollUpdates, main thread), for both classes: the get-up blend's last
 *    update calls the animation update, which calls AppearanceBase::update, which runs a queued createBody right there -
 *    set-body deletes the blend that is still running and the blend then reads the old body's destroyed node.
 *  - RAGDOLL (AnimationClass +0x2E8 set): the animal createBody destroys the node before set-body, whose ragdoll kill
 *    then reads that node, so an animal lying in ragdoll always waits. The human createBody ends the ragdoll through
 *    set-body (decomp_5b8910.txt:42-44 -> the ragdoll switch-off 0x5D0320) before it destroys anything, so a human lying
 *    in ragdoll is rebuilt between frames like a standing one. It has to be: a human copy with a queued rebuild does not
 *    read as built until that rebuild runs, and the carry that would lift a knocked-out copy out of ragdoll waits for
 *    built - a hold for ragdoll alone would never end. BUT the switch-off does its work only while the animation
 *    object's has-body byte (AnimationClass +0x88) is set (decomp_5d0320.txt:47); with it clear the ragdoll stays live
 *    and the human rebuild would destroy the node under it (the animal crash). So a human lying in ragdoll rebuilds only
 *    while that byte reads set (ragdollOffReady) and otherwise waits like an animal. The switch-off's other condition,
 *    the character's vtable +0xF8 (decomp_5d0320.txt:35-36), is not decompiled and is NOT checked here.
 * The guard returns without calling createBody: the pending byte (+0x143) stays set and the engine calls again on its
 * next ordinary update, so a held rebuild is retried every update and is never lost - a human's runs at the first
 * ordinary update outside the pass (in ragdoll, once its has-body byte also reads set), an animal's once it is also out of
 * ragdoll. A rebuild between frames is safe:
 * set-body cleans the blend up and the pass then skips the character.
 *
 * A HOLD OUTLIVES AN OWNERSHIP CHANGE. An animal copy held lying in ragdoll that becomes this game's own (an area hand-over) is still
 * lying in ragdoll with the rebuild the mod queued while it was a copy, so its hold goes on (heldBefore); so does a rebuild the
 * mod queued that has not run yet, hold or no hold (the caller's mark). A rebuild the engine
 * queues on a body this game already owned, with no hold carried over, is left to the engine as in a game without the mod.
 *
 * INPUTS (0 / nonzero): onMain = the call is on the main thread; human = the appearance is AppearanceHuman's (0 = an
 * animal, or a class not known: the stricter rule); replicated = a character the mod replicates (it has a
 * uid), whoever owns it; owned = this game owns that uid now; heldBefore = this uid's rebuild is already being held (its
 * hold began while it was a copy, or the mod queued it and it has not run yet, or the caller is the mod's own look change); entity = it has a body (app +0xD8); ragdoll = the ragdoll object exists (it holds an animal; a human only while
 * ragdollOffReady is 0); ragdollOffReady = the animation object's has-body byte (+0x88) is set, so set-body's ragdoll
 * switch-off can end the ragdoll (only read for a character in ragdoll; an animal ignores it); inPass = the main
 * thread is inside the engine's ragdoll pass now; fault = a read faulted. Off the main thread it never waits; on the main
 * thread a fault waits.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef COOP_COMMON_COPYBODY_H
#define COOP_COMMON_COPYBODY_H

namespace coopbody {

enum { kCopyBodyRun = 0, kCopyBodyWait = 1 };

/* heldBefore: this uid's rebuild is already being held (its hold began while it was a copy, or the caller is the mod's own look
   change). A body this game owns waits only then - the rebuild the mod queued while it was a copy outlives an ownership change;
   a rebuild the engine queues on its own character is left to the engine, as in a game without the mod. */
inline int CopyBodyDecide(int onMain, int human, int replicated, int owned, int heldBefore, int entity, int ragdoll,
                          int ragdollOffReady, int inPass, int fault)
{
    if (onMain == 0) return kCopyBodyRun;
    if (owned != 0 && heldBefore == 0) return kCopyBodyRun;          /* this game's own, no hold carried over */
    if (fault != 0) return kCopyBodyWait;
    if (replicated == 0 || entity == 0) return kCopyBodyRun;         /* not replicated, or no old body: nothing is destroyed */
    if (inPass != 0) return kCopyBodyWait;                           /* the get-up blend may be running: both classes */
    if (ragdoll == 0) return kCopyBodyRun;
    if (human == 0) return kCopyBodyWait;                            /* the animal rebuild reads the node it destroyed */
    return (ragdollOffReady != 0) ? kCopyBodyRun : kCopyBodyWait;    /* a human's set-body ends the ragdoll only with +0x88 set */
}

/* ---- T-1449 attempt 2 (design D1, F1829) - A FRESH COPY IS DRAWN FOR THE FIRST TIME ONLY ONCE IT WEARS ITS OWNER'S LOOK AND KIT ----
 * WHY. The joining game makes another player's NPC copy with the game's create call and no save state, so the game puts a random kit
 * in its inventory; the first body build (a later frame) attaches that kit's 3-D models and so makes their looks (textures by look
 * name); ~1.5 s later the owner's CLOTHING removes them while their textures load, and the loader drops a look whose only wearer is
 * gone - every item with that look shows black (F1823 / F1826 / F1827).
 * THE RULE. The copy's FIRST body build (createBody with no body yet) waits while the copy is marked (a fresh copy from a SPAWN) and
 * the mark is not released. The mark is released on an EVENT only: the owner's kit went on before the build (the look first), the
 * owner said it has no look / no kit to send for that uid (an APPEARANCE carrying the 'NOLK' flag below), a look or kit apply ended
 * without applying, the early road was refused for any reason but 'a body already', or the owner left or changed. The mark goes with the copy (removed, area unload, world change). A copy this game owns
 * builds at once (its owner's kit will never come: a kit for an owned uid is dropped). Never a timer.
 * Before the build only the record writes plus setAppearanceData (look; updateAppearance would return at once with no body,
 * decomp_532a40.txt:31-33) and inventory-only changes (kit) run (FirstPreBuildOk). The book of marks and events: firsthold.h. */
enum { kFirstNone = 0, kFirstWait = 1, kFirstRunReleased = 2, kFirstRunOwned = 3, kFirstHadBody = 4 };

/* onMain: the createBody call is on the main thread. marked: the uid carries a first-build mark. released: that mark was released.
   owned: this game owns the uid now. entity: the appearance already has a body (+0xD8). fault: the guard's read faulted.
   kFirstNone = not a first-build question (the ordinary copy rule decides, CopyBodyDecide). */
inline int FirstBuildDecide(int onMain, int marked, int released, int owned, int entity, int fault)
{
    if (onMain == 0 || marked == 0) return kFirstNone;   /* off the main thread nothing waits; no mark: the ordinary rule */
    if (fault != 0) return kFirstNone;                   /* the ordinary rule (a fault waits there) */
    if (entity != 0) return kFirstHadBody;               /* a body already: not a first build - the mark goes */
    if (owned != 0) return kFirstRunOwned;               /* this game's own now: no kit will come */
    if (released != 0) return kFirstRunReleased;
    return kFirstWait;
}

/* May a look / kit go on a marked copy BEFORE its first build? marked and not released, no body (+0xD8 and +0xE0 clear) and none
   being made (+0xE8 clear). 0 = the ordinary road (it waits for the body to be built). */
inline int FirstPreBuildOk(int marked, int released, int entity, int entity2, int creating)
{
    return (marked != 0 && released == 0 && entity == 0 && entity2 == 0 && creating == 0) ? 1 : 0;
}

/* The owner's "no look / no kit" for a uid: an APPEARANCE whose payload is uid u32 | 'NOLK' u32 | what u8 (little-endian; 9 bytes).
   A real record never starts with this count (0x4B4C4F4E bool keys). what: 1 = no look and no kit will come on this road,
   2 = the look was sent, the kit will not come. Game-to-game protocol 245. */
const unsigned int kNoLookMagic = 0x4B4C4F4Eu;   /* 'N' 'O' 'L' 'K' as read little-endian */
const unsigned long kNoLookLen = 9;
enum { kNoLookNone = 1, kNoLookNoKit = 2 };
inline void NoLookWrite(unsigned int uid, int what, unsigned char* out)
{
    unsigned int i;
    for (i = 0; i < 4; ++i) out[i] = (unsigned char)((uid >> (8 * i)) & 0xFF);
    for (i = 0; i < 4; ++i) out[4 + i] = (unsigned char)((kNoLookMagic >> (8 * i)) & 0xFF);
    out[8] = (unsigned char)what;
}
/* 1 = p is a NOLK APPEARANCE (uid and what set); 0 = it is not (an ordinary record, or a bad what). */
inline int NoLookParse(const unsigned char* p, unsigned long n, unsigned int* uid, int* what)
{
    if (p == 0 || n != kNoLookLen) return 0;
    unsigned int u = 0, m = 0, i;
    for (i = 0; i < 4; ++i) u |= ((unsigned int)p[i]) << (8 * i);
    for (i = 0; i < 4; ++i) m |= ((unsigned int)p[4 + i]) << (8 * i);
    if (m != kNoLookMagic) return 0;
    if (p[8] != (unsigned char)kNoLookNone && p[8] != (unsigned char)kNoLookNoKit) return 0;
    if (uid != 0) *uid = u;
    if (what != 0) *what = (int)p[8];
    return 1;
}

}   /* namespace coopbody */

#endif
