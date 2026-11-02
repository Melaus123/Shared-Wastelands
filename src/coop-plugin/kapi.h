#pragma once
/* kapi.h - per-race rules (block_slot, no_weapon_in_hand, skill_xp; stat_cap and anim_speed read and logged only), plugin side.
   Ported from the third public pull request (author: MassDivide). The rule engine and the option form: src/common/kapirules.h.

   ONE ROAD WHATEVER THE PLAYER COUNT. The rules every game applies come from the world's option map (race.* rows): the
   session authority's game reads kenshi_api.cfg beside the DLL and sends its rows whenever the world's differ (compared at
   every complete options map and at the world server's AUTH, bounded - T-1157 review fold); every game, the authority
   included, applies the rows the map carries back. A joiner's own file is not used. Solo play (role single, no link) applies
   its own file's rows through the same rows-to-rules step, at every world load.

   The three hooks are installed at start whatever the rules say, and act only for a race the active rules name. */

#include <string>
#include "../common/kapirules.h"

namespace coop
{
    void InstallKapi();          /* startPluginOn: reads kenshi_api.cfg, installs the hooks, one [KAPI] line each */

    /* store.cpp's OPTIONS handler, MAIN THREAD: Begin at a map's start, one Option call per race.* row, End with whether the map
       was whole and the world's own (not a refusal reply). End applies the world's rows and, on the authority, sends this
       game's file's rows when the world's differ (kapi::AskBound bounds the resends on one link). */
    void KapiOptionsMapBegin();
    void KapiWorldOption(const std::string& key, const std::string& value);
    void KapiOptionsMapEnd(bool complete);
    void KapiAuthorityChanged(const char* why = "the world server's AUTH");   /* store.cpp's AUTH handler and admin.cpp's login edges, MAIN THREAD: the rule-setter may have moved on this link */

    /* store.cpp StoreWorldLoadBegan, MAIN THREAD: solo play applies its own file's rows; in a multiplayer role a set taken
       from this game's own file is dropped (the world's map brings the rules), and so is a world set not taken on the
   welcomed link this load runs on. */
    void KapiWorldLoadBegan();

    void ReportKapi();           /* the [KAPI] REPORT line (the `report` fan-out) */
}
