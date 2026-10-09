// May the mod call into a Character pointer it stored earlier? Pure (no includes): spawn.cpp's LiveCharacter and the offline
// suite include this one header.
// A stored pointer is only as good as the moment it was stored: the engine frees characters when their zone unloads or they
// are destroyed, and the memory is reused. A call such as Character::worldPosition makes a virtual call through the object at
// +0x448, so on a freed pointer it can run another object's code (a record's destructor), and the engine later frees that
// record a second time. LiveCharacter asks, in the same frame as the call:
//   plausible     - the pointer is readable and starts with a vtable inside the game exe;
//   retired       - the mod's registry has withdrawn it (P034 / the destroy hook);
//   worldUp       - a GameWorld exists (the engine's handle registry needs one);
//   gotHandle     - the object's own engine handle (+0x58) reads as a CHARACTER handle;
//   storedSame    - that handle's SERIAL is the one the mod's registry stored when it registered this address (true when
//                   the registry stored none): a different character built at a reused address gets a new serial and fails
//                   it. The index is not compared - it can change for the same character (a move between squads); a live
//                   answer with the same serial and another index re-notes the stored handle (Renote);
//   doomed        - the engine accepted a destroy of this address with this same handle (it waits on the kill list);
//   onMain        - the handle registry is asked only on the main thread; off it (the engine's combat / hit / medical
//                   detours) a pointer that passed every check above is used without the registry's answer (kUnasked);
//   resolvedSelf  - the engine's registry resolves that handle to this very address;
//   resolvedOther - it resolves to a DIFFERENT live character (the bytes here name another object);
//   inUpdateList  - the address is in the engine's active-character set (pointer compare, never a dereference); the second
//                   witness P034 also uses, because a live character's handle can fail to resolve (P034 falseGone).
#ifndef LIVECHAR_H
#define LIVECHAR_H
namespace livechar {
enum Verdict
{
    kLive = 0,         // the engine's registry or update list names this address: usable
    kUnasked = 1,      // off the main thread, every check that can run there passed: usable
    kNoObject = 2,     // not a readable object with a game vtable
    kRetired = 3,      // withdrawn by the mod's registry
    kNoWorld = 4,      // no GameWorld to ask
    kNoHandle = 5,     // no CHARACTER handle in the object
    kDoomed = 6,       // destroy accepted, waiting on the kill list
    kGone = 7,         // the handle resolves to nothing and the engine does not update this address
    kOtherObject = 8,  // the handle resolves to another character
    kNotStored = 9,    // the handle differs from the one the registry stored for this address: another character lives here
    kVerdicts = 10
};
inline bool Usable(int v) { return v == kLive || v == kUnasked; }
// The stored handle against the one read now: the serial decides; a registry row that stored no handle (0, 0) does not.
inline bool SameStored(unsigned int storedIndex, unsigned int storedSerial, unsigned int nowIndex, unsigned int nowSerial)
{
    (void)nowIndex;
    if (storedIndex == 0 && storedSerial == 0) return true;
    return storedSerial == nowSerial;
}
// After a live answer: the stored handle is brought up to date when its serial matches and its index moved.
inline bool Renote(unsigned int storedIndex, unsigned int storedSerial, unsigned int nowIndex, unsigned int nowSerial)
{
    if (storedIndex == 0 && storedSerial == 0) return false;
    return storedSerial == nowSerial && storedIndex != nowIndex;
}
inline int Judge(bool plausible, bool retired, bool worldUp, bool gotHandle, bool storedSame, bool doomed, bool onMain,
                 bool resolvedSelf, bool resolvedOther, bool inUpdateList)
{
    if (!plausible) return kNoObject;
    if (retired) return kRetired;
    if (!worldUp) return kNoWorld;
    if (!gotHandle) return kNoHandle;
    if (!storedSame) return kNotStored;
    if (doomed) return kDoomed;
    if (!onMain) return kUnasked;
    if (resolvedSelf) return kLive;
    if (inUpdateList) return kLive;
    if (resolvedOther) return kOtherObject;
    return kGone;
}
// A noted destroy ends on the EVENT, not on a clock: the main thread's per-frame sweep drops an entry once its handle no
// longer resolves to its address AND the address is not in the engine's update list. From then on LiveCharacter's own
// registry questions refuse that address anyway, so the entry has nothing left to add; while either still names it, it stays.
inline bool DoomEnds(bool resolvesToSelf, bool inUpdateList)
{
    return !resolvesToSelf && !inUpdateList;
}
// The names the REPORT line prints, in Verdict order.
inline const char* VerdictName(int v)
{
    static const char* const k[kVerdicts] = { "live", "unasked", "noObject", "retired", "noWorld", "noHandle", "doomed", "gone", "otherObject", "notStored" };
    return (v >= 0 && v < kVerdicts) ? k[v] : "?";
}
}
#endif
