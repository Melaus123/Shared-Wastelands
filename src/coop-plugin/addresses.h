/* addresses.h - P8h. THE GATE: is this the executable our numbers were read from?
 *
 * Every RVA this plugin uses was read out of ONE binary - the RE_Kenshi-patched Kenshi 1.0.65 x64 Steam
 * `kenshi_x64.exe`, MD5 df4a5a7e... (`.modding/01-environment.md`). On any other executable those numbers
 * point at whatever happens to be there: F020's rule, and the reason peace.cpp and store.cpp already refuse
 * to hook a function whose first bytes are not the ones they were written against.
 *
 * THE MOD IS GOING TO BE PUBLIC and Steam and GOG executables differ, as does every patch. So the plugin now
 * FINGERPRINTS the executable at start, loads `addresses/<fingerprint>.txt` from beside the DLL, checks the
 * bytes at EVERY address that table names, and installs only if all of them match. No table, or one
 * mismatch, and NOTHING is installed: one plain line in the log, one caption on the title screen, and the
 * game runs unmodded - exactly what role=single already means (config.h), reached by a different road.
 * REFUSE RATHER THAN CRASH (user, approved 2026-09-04).
 * rk1 - THE ONE DIFFERENCE THAT IS ACCEPTED: a table-road code row that another mod has HOOKED (its first instruction
 * replaced by a 5-byte E9 jump, every later byte the table names still matching) when every registration of that row
 * is kAddrUseCallOnly - this mod only calls it, as the game itself does through the other mod's jump. Counted as
 * addrHookedAccepted, one [ADDR] line each, never in addrMismatch (src/common/addrhooked.h decides). Any other
 * difference, any row this mod hooks or patches, and every row on the signature road still refuse.
 *
 * WHAT THIS IS NOT. It is not a tamper check and it is not a guarantee that an address is the right FUNCTION -
 * only that the bytes there are the bytes this build was read against, which is F038's distinction and is all
 * a byte comparison can honestly claim. Addresses in WRITABLE data have no constant bytes at all and are
 * counted, unverified, as `addrVarEntries`; see build/prep-p8h.md for that gap at its true width.
 *
 * ORDER. AddrInit() runs FIRST in startPlugin, before ConfigLoad and before any hook, because everything
 * downstream depends on its answer. It reads a file and compares bytes; it calls no engine function.
 *
 * C++03 (VS2010 v100).
 */
#pragma once

#include <string>

class GameWorld;       /* the game's global-namespace classes (game/GameWorld.h, game/OptionsHolder.h) */
class GameOptions;

namespace coop {

/* THE REGISTRY. Every table-backed address constant in the plugin declares itself with one of these beside
   its definition; the constructor runs at DLL load, long before AddrInit, and does nothing but record the
   name and where to write the answer. A registry, not a sweep - the plugin cannot forget an entry, and a
   name that is not in the table is a REFUSAL rather than a zero nobody notices.

   The array behind it is a zero-initialised POD with a plain int counter, so it is correct no matter what
   order these constructors run in (both are zero before any constructor runs).

   rk1: THE USE. kAddrUseStrict (the default) = this mod hooks or patches the address, or nobody has said - its bytes
   must match exactly. kAddrUseCallOnly = this mod only CALLS (or reads) the address, never hooks or writes it - the
   gate then also accepts another mod's 5-byte jump at its start (src/common/addrhooked.h). Give kAddrUseCallOnly only
   when EVERY use of the bound constant is a plain call through it; a name registered twice is call-only only when
   every registration says so. */
enum { kAddrUseStrict = 0, kAddrUseCallOnly = 1 };
struct AddrReg
{
    AddrReg(const char* name, unsigned long long* slot, int use = kAddrUseStrict);
};

/* Fingerprint the executable, load its table, verify every entry, fill every registered slot.
   Returns 1 if EVERYTHING matched (a call-only row hooked by another mod counts as matched - rk1, above) and the
   plugin may install; 0 otherwise. Logs one summary line either way, plus one [ADDR] line per accepted hooked row.
   owner 97a: with NO table for this executable, or one that is not usable, it then also looks for
   TitleScreen::update by a byte pattern (src/common/sigscan.h) and fills ONLY the TitleScreen_update row
   with a unique match, so the refusal button can still be shown - one more [ADDR] line says which.
   Safe to call twice: the second call returns the first call's answer and logs nothing. */
int AddrInit();

/* The gate. ANY THREAD (a plain aligned int written once before any hook exists). */
int AddrOk();

/* The table's answer for one name, as an RVA, or 0 if it is not in the table. Startup use only - the
   registered constants above are what the detours read. */
unsigned long long Rva(const char* name);

/* review S3 (2026-10-17): THE HOOK GUARD. `target` is an absolute address about to be hooked (coop::AddHook,
   hooks.cpp). Returns the name of the registered row bound there when EVERY registration bound to that address is
   kAddrUseCallOnly - the gate may have accepted another mod's 5-byte jump at it, so this mod must not hook it; AddHook
   refuses. 0 when the hook is allowed: no registration is bound there (unbound, or an address no row names), or at
   least one is strict. ANY THREAD after AddrInit (reads slots written once, before any hook). */
const char* AddrHookRefusedAt(unsigned long long target);

/* One token for the periodic title-pump line, and the counters this build is judged by. */
std::string AddrReportToken();

/* The refusal's short reason, for log lines - "" when the gate did not refuse. ui98 (owner 98 A): no longer shown to the
   player - the title button reads MULTIPLAYER, greyed, with the hover note (ui.cpp UiRefusedTipTick). */
const char* AddrRefusalCaption();

/* What AddrInit worked out, for the version line: the executable's fingerprint, and the name (its !version)
   of the address table it read. Either is "" when AddrInit has not run or refused before computing it
   (e.g. the executable could not be read, or there is no table for this fingerprint). Startup use only. */
std::string AddrExeFingerprint();
std::string AddrTableName();

/* The plugin's own addresses for the game's functions and globals.
   AddrAbs: the address in the running image of a table row a file has bound with AddrReg - image base + the
   row's RVA - or 0 when that row is unbound: AddrInit has not run, or found no table / no usable table (then
   every row is 0 except TitleScreen_update, which the byte pattern may have filled - owner 97a). A REFUSAL DOES
   NOT MEAN 0: when a table loaded and refused on its bytes (or a name it lacks), every row it carries IS filled,
   so AddrOk() is 0 while AddrAbs returns a real address - only the refusal path's title hook may use one then.
   Every caller guards 0.
   GameWorldPtr / OptionsPtr: the game's static GameWorld and GameOptions (rows GameWorldGlobal and
   OptionsHolderGlobal); 0 when the row is
   unbound, including before AddrInit. ANY THREAD after AddrInit (the slots are written once, before any hook). */
unsigned long long AddrAbs(unsigned long long rva);

/* stage 7/9: the end of the RUNNING executable's .text section, as an RVA, read from its own PE headers (never a number
   typed for one game version) - for "is this a code address inside the exe" checks. 0 if no .text section is found, which
   callers treat as "not inside". ANY THREAD (computed once; a race computes the same value). */
unsigned long long AddrTextEnd();

/* gamecalls.cpp (mig3 C3): builds, ONCE, the empty handle RootObjectBase::getHandle answers for an unfilled row.
   AddrInit calls it first - on the plugin's start thread, before any hook exists. Nothing else may call it.
   gog1 fold 2: returns 1, or 0 when the game's Hand_ctor faulted - caught by its own __try/__except (gamecalls.cpp,
   BuildEmptyHandGuarded), and AddrInit then refuses with every slot cleared. */
int GcBuildEmptyHand();
::GameWorld* GameWorldPtr();
::GameOptions* OptionsPtr();

}
