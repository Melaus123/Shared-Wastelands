// identity.cpp - see identity.h for why this module exists (parity register P-2).

#include "identity.h"

#include "coop_log.h"
#include "game/Character.h"
#include "game/hand.h"
#include "game/Enums.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <sstream>
#include <locale>

namespace coop {

namespace {

// Local guards. Each module keeps its own six-line copy rather than exporting one across
// translation units - the same choice medical.cpp made, for the same reason.
bool PlausiblePtr(const void* p)
{
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000 || v >= 0x00007FFFFFFFFFFFull) return false;
    if (v & 0x7) return false;
    __try { volatile uintptr_t probe = *(uintptr_t*)v; (void)probe; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

bool PlausibleObject(const void* p)
{
    if (!PlausiblePtr(p)) return false;
    uintptr_t vtable = 0;
    __try { vtable = *(uintptr_t*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t base = (uintptr_t)::GetModuleHandleA(0);
    return vtable > base && vtable < base + 0x4000000;
}

std::string N(unsigned long long v)
{
    std::stringstream ss; ss.imbue(std::locale::classic()); ss << v; return ss.str();
}

} // namespace

bool ObjIdValid(const ObjId& id)
{
    // index 0 is the engine's own null handle. Serial is the anti-recycle field and a live
    // handle always carries one, so both being zero means "nothing was captured".
    return id.index != 0 || id.serial != 0;
}

bool CaptureObjId(const void* character, ObjId* out)
{
    if (out == 0) return false;
    out->index = out->serial = out->type = out->container = out->containerStamp = 0;
    if (!PlausibleObject(character)) return false;

    // getHandle() returns `this + 0x58` and falls back to a static object when called on
    // null (F099), so it cannot fault and cannot allocate - which is what makes it usable
    // from the combat detour's thread. The five reads that follow are plain aligned loads.
    const hand& h = ((::Character*)character)->getHandle();
    if (!PlausiblePtr(&h)) return false;

    __try
    {
        out->index           = h.index;
        out->serial          = h.serial;
        out->type            = (unsigned int)h.type;
        out->container       = h.container;
        out->containerStamp = h.containerStamp;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out->index = out->serial = out->type = out->container = out->containerStamp = 0;
        return false;
    }
    // TYPE CHECK AT CAPTURE. Without it a handle whose type is not CHARACTER is captured happily,
    // then refused by ResolveObjId (which does check), and the caller reports it as
    // "the character was destroyed" - a confident FALSE POSITIVE with no counter separating it from
    // a real destruction. Caught in review before any run relied on the number.
    //
    // Refusing here routes it to the caller's "no handle" bucket instead, which is honest: we have
    // no usable cross-process identity for this object.
    if (out->type != (unsigned int)RECORD_CHARACTER)
    {
        out->index = out->serial = out->type = out->container = out->containerStamp = 0;
        return false;
    }
    return ObjIdValid(*out);
}

::Character* ResolveObjId(const ObjId& id)
{
    if (!ObjIdValid(id)) return 0;
    // Only characters. A handle naming a building or an item must never be handed to a
    // function that will treat it as a Character - the type field is free, so check it.
    if (id.type != (unsigned int)RECORD_CHARACTER) return 0;

    // The engine's own resolver. Its serial comparison is what makes this safe: a recycled
    // index whose serial no longer matches returns null rather than the wrong object.
    hand h(id.index, id.serial, (itemType)id.type, id.container, id.containerStamp);
    ::Character* c = h.getCharacter();
    if (!PlausibleObject(c)) return 0;
    return c;
}

std::string ObjIdString(const ObjId& id)
{
    // Same field order as the engine's hand::toString(), so a line from here can be diffed
    // straight against a P009 line without anyone having to re-derive the layout.
    return N(id.index) + "-" + N(id.serial) + "-" + N(id.container) + "-"
         + N(id.containerStamp) + "-" + N(id.type);
}

} // namespace coop
