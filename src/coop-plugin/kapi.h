#pragma once
/* kapi.h - per-race rules (block_slot, no_weapon_in_hand, skill_xp; stat_cap and anim_speed read and logged only), plugin side.
   Ported from the third public pull request (author: MassDivide). The rule engine and the option form: src/common/kapirules.h.

   ONE ROAD WHATEVER THE PLAYER COUNT. The rules every game applies come from the world's option map (race.* rows). T-1209
   (owner 1414 d): the world server reads them from the kenshi_api.cfg in the world's own folder and sends them to every game;
   no game sends race rows (the world server refuses them) and a login changes nothing. A multiplayer game's own file is not
   used (HOST hands it to the world server for a NEW world's folder that has none - decision 1417 a, ui.cpp). Solo play (role single, no link) applies
   its own file's rows through the same rows-to-rules step, at every world load.

   The three hooks are installed at start whatever the rules say, and act only for a race the active rules name. */

#include <string>
#include "../common/kapirules.h"

namespace coop
{
    void InstallKapi();          /* startPluginOn: reads kenshi_api.cfg, installs the hooks, one [KAPI] line each */

    /* store.cpp's OPTIONS handler, MAIN THREAD: Begin at a map's start, one Option call per race.* row, End with whether the map
       was whole and the world's own (not a refusal reply). End applies the world's rows. */
    void KapiOptionsMapBegin();
    void KapiWorldOption(const std::string& key, const std::string& value);
    void KapiOptionsMapEnd(bool complete);

    /* store.cpp StoreWorldLoadBegan, MAIN THREAD: solo play applies its own file's rows; in a multiplayer role a set taken
       from this game's own file is dropped (the world's map brings the rules), and so is a world set not taken on the
   welcomed link this load runs on. */
    void KapiWorldLoadBegan();

    void ReportKapi();           /* the [KAPI] REPORT line (the `report` fan-out) */
}
