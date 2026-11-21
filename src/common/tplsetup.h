/* T-1416 fix attempt 1 (H-T1416-squadplan, design (a), multiplayer item 13 - a copy made through the game's own routine):
   whether a squad this game makes for ANOTHER game's characters gets the engine's template setup (1.0.65 0x2715E0, 1.0.68 0x271620,
   address row PlatoonPlanTemplateSetup).

   The engine runs that setup on every squad it makes from a template (createRandomUnloadedSquad 0x57EA30, createRandomSquad 0x582F80,
   the captured-character road 0x48EFA0, the save load 0x272BE0): it fills the squad plan's package list (plan = Platoon +0xF8; the
   list's element count at plan +0x28) from the template's 'AI packages' with their priorities and 'Leads to' links, and the plan's
   per-tick chooser then picks the active package (plan +0x48). A copy squad made by Faction::spawnEmptySquad + ConfigurePlatoonPod
   never had it (F1503 / F1536), so its package list stayed empty and its members fought back on goal 32 alone (F1535).

   Arguments, exactly as the engine's own callers pass them (Read, 1.0.68 disassembly at each call): rcx = the squad plan
   (Platoon +0xF8), rdx = the squad template's GameData (Platoon +0x108 at 0x57F6AC; plan +0x100 else Platoon +0x108 at 0x58598C;
   the GameData given to spawnEmptySquad at 0x48FBFB). The setup reads no other plan field than its own list (Read, 1.0.65 body).

   Pure decisions only - no engine type, no game call. Compiled into the plugin (spawn.cpp) and the offline suite. */
#ifndef SW_TPLSETUP_H
#define SW_TPLSETUP_H

namespace sqtpl {

const unsigned int kPlanOffInPlatoon = 0xF8;    /* Platoon +0xF8 the squad plan (decomp_57ea30 plVar2[0x1f]; the callers above) */
const unsigned int kTemplateInPlatoon = 0x108;  /* Platoon +0x108 the squad template GameData (decomp_57ea30 plVar2[0x21]) */
const unsigned int kPlanListCount = 0x28;       /* plan +0x28 the package list's element count (setup 0x271C68, lookup 0x27ED28) */
const unsigned int kPlanActivePkg = 0x48;      /* plan +0x48 the active package, written only by set-active 0x272160 (F1503); read for the P323 line */
const long long kListCountSane = 4096;          /* a count above this is not a list count: the read is treated as unread */

enum Verdict
{
    kRun = 1,               /* call the setup now */
    kSkipReused = 0,        /* an existing copy squad was reused: it got its list when it was made */
    kSkipNoRow = -1,        /* the address row is not bound on this game build */
    kSkipNoTemplate = -2,   /* the template's GameData did not resolve from the squad message's template sid */
    kSkipNoPlan = -3,       /* the plan or its list count could not be read */
    kSkipHasList = -4       /* the list already holds packages: never built twice */
};

/* newlyMade: this road made the squad just now (1) or reused one (0); rowBound: the setup's address is known; templateResolved: the
   GameData passed to spawnEmptySquad is the one the message's sid names and is a live object; listCount: the plan's list count, -1
   when the plan or the count could not be read. */
inline int Decide(int newlyMade, int rowBound, int templateResolved, long long listCount)
{
    if (newlyMade == 0) return kSkipReused;
    if (rowBound == 0) return kSkipNoRow;
    if (templateResolved == 0) return kSkipNoTemplate;
    if (listCount < 0 || listCount > kListCountSane) return kSkipNoPlan;
    if (listCount != 0) return kSkipHasList;
    return kRun;
}

inline const char* VerdictWord(int v)
{
    switch (v)
    {
    case kRun: return "ran";
    case kSkipReused: return "reused";
    case kSkipNoRow: return "no-row";
    case kSkipNoTemplate: return "no-template";
    case kSkipNoPlan: return "no-plan";
    case kSkipHasList: return "has-list";
    default: return "?";
    }
}

}   /* namespace sqtpl */

#endif
