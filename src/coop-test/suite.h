/* src/coop-test/suite.h - WHAT THE OFFLINE SUITE'S FILES SHARE (owner decision 1204).
 *
 * The suite was one file, test_main.cpp. Its older, settled tests now sit in suite_part1.cpp .. suite_part7.cpp so that
 * cl /MP8 compiles the files side by side (build.bat). The run order is still kTests in test_main.cpp, and new tests
 * still go in test_main.cpp. Every file opens namespace suite; this header carries the includes, the types
 * more than one file uses, a declaration of every helper one file defines and another calls, and a declaration
 * of every test a suite_part file defines (the table in test_main.cpp names them).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#ifndef SW_SUITE_H
#define SW_SUITE_H

#include <io.h>   /* T-802 fold: _findfirst over the real squad files */
#include "clockmath.h"
#include "loadlatch.h"
#include "deferredmerge.h"
#include "storemeta.h"
#include "sigscan.h"     /* owner 97a: the byte-pattern scanner that finds the title screen with no address table */
#include "loadermarker.h"   /* mig6 (owner 118): the marker file SharedWastelandsLoader.dll finds the mod by */
#include "modsenabled.h"   /* owner 123/124: is a mod switched on in Kenshi's mod list */
#include "ogreplugin.h"    /* the loader's hand-built Ogre plugin object: its name string's layout */
#include "addrtable.h"   /* P8h: the address table format and the executable fingerprint */
#include "addrhooked.h"  /* rk1: a call-only row another mod has hooked - accepted or refused */
#include "sigtable.h"    /* T-63 stage 7: every address row by its byte pattern when no table matches */
#include "modlist.h"   /* settings5 S5: the mod list, its diff and its words */
#include "storelink.h"   /* B10-b (review-b10 M-3 / M-7): the store link's two pure decisions */
#include "queuejournal.h"   /* B12 (decision 52): the outage queue's conflict rule and its journal line */
#include "writerladder.h"
#include "cfgtext.h"   /* P8i: shared_wastelands.cfg's text layer - the SAME translation unit the plugin and the MULTIPLAYER panel compile */
#include "steamprobe.h"   /* T-290 S0: the relay probe's words, settle rule, log line and version reader */
#include "relaysplit.h"   /* the Steam link's split and rejoin - the same header the plugin compiles */
#include "steamlisten.h"  /* the Steam listener's cap, arrival verdict, link table, relay-only rule and pump rule - the plugin's header */
#include "relaybridge.h"  /* T-290 S2: the steam:<id> route and the host's bridge decisions - the plugin's header */
#include "steamfriends.h"  /* T-290 S4a: Steam friends find and join a host - the plugin's header */
#include "authproof.h"   /* the world's password: SHA-256, HMAC, PBKDF2, the proof and auth.txt */
#include "accessrule.h"   /* access.txt and the admission decision */
#include "authlock.h"   /* the wrong-password wait */
#include "upnpplan.h"     /* T-53: the router port opening's decisions - when to ask, the entry's text, what is there, when it goes */
#include "routerwire.h"   /* T-597: the mod's own router ask - SSDP, the device description, SOAP, PCP / NAT-PMP */
#include "nametag.h"      /* the name tag over another player's characters - its two lines and its colour */
#include "playerstab.h"   /* T-545: the PLAYERS tab - its rows, words, tab numbers and the other game's lines */
#include "joblever.h"     /* P17 / P43: the jobtest, cagetest and `boxdigest ... players` argument shapes */
#include "panelfit.h"  /* P8i-b: the panel's size refusal and its give-up rule, as pure arithmetic */
#include "../common/hbmath.h"   /* E47-S2: the world-group heartbeat's cadence arithmetic - the same header the plugin compiles */
#include "../common/squadevent.h"   /* T-1084: a world squad's events mark its record due on the heartbeat's early road - the same header the plugin compiles */
#include "panelstatus.h" /* U2: every sentence the MULTIPLAYER panel's status area can say */
#include "doorsync.h"  /* P8n: the door road's last-actor rule, terminal-state test and churn give-up */
#include "retirewithdraw.h"   /* P8m (H-ghost): the pending-withdraw set as pure transitions */
#include "uidtable.h"         /* mirror1 (crash T487): the uid table's decisions */
#include "uidslots.h"         /* the per-copy word tables' slot bookkeeping (medical.cpp: hunger, knocked out, dead) */
#include "uidlayout.h"        /* M4 (owner 203): a uid is (the making game's slot << 22) | counter */
#include "uidblock.h"         /* M4 fold: the notebook's uid counter blocks */
#include "liverelay.h"        /* M6 (T-197 piece 6): the AREA route, the delivery sets and CATCHUP */
#include "liveowner.h"        /* M7a A1 build 1 [a1b1-t0]: generations, receipts, ROSTER, the road to one game */
#include "preload.h"         /* T-346 slice 1: the records before the load (world-server protocol 67, RECORD_FEED kinds 5-8) */
#include "recordfeed.h"       /* T-313: the record feed (world-server protocol 63, RECORD_FEED 58) */
#include "pumpcarry.h"         /* the world-server link's carry and its MOVE collapse */
#include "liveenvelope.h"     /* M5a (T-197 piece 4): the LIVE envelope, its routes and the relayed sender id */
#include "ownerroute.h"       /* M7b slice 1 (T-197): a request to its target's owner, an answer back to its asker */
#include "peergone.h"         /* M8 (T-197 piece 8): PLAYER_GONE and the per-player removal rule */
#include "../common/p113reuse.h"   /* PROBE P113 (reuse): same object or a new one at the address */
#include "../common/livechar.h"    /* may the mod call into a stored Character pointer */
#include "../common/p124stop.h"   /* PROBE P124: a non-player copy's stop - the report reading and the overshoot measure */
#include "../common/loadedzones.h"   /* the loaded set from the engine's active-zone list */
#include "../common/recordpos.h"   /* T-694: which known position a record this game writes takes */
#include "../common/modversion.h"   /* the mod's version */
#include "../common/lostcopy.h"   /* MSG_RESEND's bytes, the owner's verdict, the lost-copy book, the stale-row rule */
#include "../common/stalecopy.h"  /* a repeat SPAWN over a stale copy; a copy whose owner is in an area not loaded here */
#include "../common/chatwire.h"   /* in-game text chat - the message bytes, the words, Tab, search, the kept lines, chat.cfg */
#include "../common/dupsquad.h"   /* the duplicate report over one squad record's engine bytes */
#include "joinstage.h"        /* M11a S1 (T-197 piece #10): the join stage, the join gate, PLAYERS and JOIN_STAGE */
#include "presence.h"         /* M11 C1 (T-197, to-do M11): is another player in this world; the area tie-break with no host */
#include "../common/poselayer.h"   /* T-307: the layer table's held state as a pose - the pick and the settle rule */
#include "arrivals.h"         /* M11 C2 (T-197, row M11): which other player has just entered the world */
#include <ctime>
#include "areaclaim.h"   /* B13: what the notebook remembers across a restart - the area-claim rule, the slot rule and the three file lines */
#include "diskformat.h"   /* owner 429: the folder format number */
#include "datadir.h"    /* PP3: the player's data folder */
#include "worlddir.h"   /* W1w (decisions 58, 59): world names, world folders, display names, the migration plan */
#include "areadrop.h"   /* W1-b (T240; review 2026-09-22 H2/H3): the effective area map - an absent reporter keeps its areas */
#include "spawnage.h"   /* P1 (animal age): the MSG_SPAWN payload - SendSpawn and OnSpawn call these, the suite round-trips them */
#include "gpoptions.h"   /* settings2 S2: the gp.* / gt.* keys and ranges */
#include "recruitmult.h"   /* recruit3: the recruitmult host option */
#include "weatherwire.h"   /* P7s (F553): the MSG_WEATHER payload keyed by the region stringID - the plugin and the notebook call these */
#include "saywire.h"      /* P3 (read-parity3 GAP 3): the MSG_SAY payload - SendSay and OnSay call these */
#include "crimewire.h"    /* crime3: a character's current crime, sent by its owner */
#include "prisonwire.h"
#include "treatwire.h"   /* heal1 */
#include "cageask.h"     /* a guard's caging asked of the owner until it is answered */
#include "namewire.h"    /* names1 */
#include "insidewire.h"  /* P25 fold 2: MSG_INSIDE */
#include "profiles.h"    /* prof1 */
#include "uidrive.h"     /* pp1: the TEST-ONLY UI driver verbs' name table */
#include "restoreguard.h"   /* restore1a */
#include "slavewire.h"   /* slave1 */
#include "capturewire.h"   /* P11 */
#include "shotwire.h"   /* P104 fix: MSG_SHOT */
#include "meleewire.h"   /* MSG_MELEE_DECIDED and which game decides a melee blow */
#include "swinggate.h"   /* the swing credit of a copy of another player's character */
#include "shotgate.h"   /* T-330: the shot credit, which game decides a bolt's hit, MSG_SHOT_FIRED */
#include "handwire.h"   /* T-330: MSG_HAND and the copy's draw / put-away decision */
#include "fightlog.h"   /* the [FIGHT] lines: name cleaning, the event ring and its cap */
#include "hitwire.h"   /* fight parity: the decided blow on HIT and the attack move on SWING */
#include "blockpick.h"   /* T-1101 step 5: MSG_BLOCK_PICK and the defender's owner's arrived picks */
#include "effectwire.h"   /* T-327: MSG_EFFECT */
#include "orphanhold.h"   /* T-306 (owner decision 226; fold 1): when an orphaned zone tick sends its player sector as unknown */
#include "gamelink.h"   /* link1 (T416): the game link's ENet timeouts and the joiner's re-dial */
#include "talkwire.h"    /* P26 stages 1-3: the MSG_TALK payload */
#include "talkcursor.h"  /* T-737: the talk cursor's decisions */
#include "hirewire.h"    /* recruit1 */
#include "paritywire.h"  /* par1 */
#include "snapdigest.h"  /* snap1 (user decision 2026-09-26) */
#include "researchwire.h"  /* loot2b: RESEARCH_BOX and research_boxes.txt lines */
#include "townrebuild.h"   /* towns2: the game's marker rule, the 5-day timer, the version wait, the sight test and the group verdict */
#include "townpending.h"   /* T-580: which notebook notes hold a town's creation back (decision 34) */
#include "townreoffer.h"   /* T-392 (owner 335 a / 337 a): the town gates' set-aside causes and the re-offer decision */
#include "layouthold.h"   /* T-966: whether the residents re-run holds the interior's plan load */
#include "barwire.h"  /* refill1: TOWN_BAR, town_bars.txt lines and the refill decisions */
#include "owedpop.h"   /* T-581: the owed town populations - the row, its wire, its file line and both sides' decisions */
#include "townrefill.h"
#include "worldkeep.h"   /* T-612: the slots.txt shrink check, the world export manifest and the alias table */
#include "worldreset.h"  /* --reset-world: which entry of a world folder is kept, moved or set aside */
#include "storegate.h"   /* the world server's entry rules: who may talk, field bounds, the per-connection allowance, the recycle folder */   /* each town's recorded losses, RECORD_GONE's town, the record tally and the one-refill decision */
#include "emptytown.h"   /* T-636: the unexplained-empty-town verdict, the once-per-town rule and the line */
#include "worldrelwire.h"  /* par24: WORLD_REL and world_relations.txt lines */
#include "lootroute.h"    /* loot2c part B: the materialise / give routes' pure decisions */
#include "boxowner.h"     /* inv4: who writes a player building's storage (the owner rung) */
#include "takeone.h"     /* par1 (parity P1): the AI take-one road */
#include "invnet.h"      /* inv2a: the inventory safety net's pure diff and baseline follow */
#include "looptick.h"   /* M15 (T-197): the world server's once-a-second jobs on a fixed schedule, message handling budgeted per pass */
#include "sendbound.h"    /* M16 (T-197): what waits to be sent - direct until full, latest-value keeps the newest, the rest never dropped */
#include "sendbundle.h"   /* M13: one packet per pass per destination - lanes, the size budget, the bundle wire */
#include "writequeue.h"   /* M14 (T-197): the world server's write queue - order per file, one writer, failures lose nothing after them */
#include "buildwire.h"   /* build1-b */
#include "placeinput.h"  /* a placement input height = live y less the ground height */
#include "recname.h"     /* a record's data file name: the world server's folder and a game's mirror */
#include "mirrorkeep.h"  /* T-985 C1: the kept mirror folder and the kept record table */
#include "areafile.h"    /* T-985: this game's area file - where its people are, kept beside the record table */
#include "farmwire.h"    /* par16 */
#include "powerwire.h"   /* P47 */
#include "bountywire.h"   /* crime5: a character's bounties between its owner and its copies */
#include "garmentdetail.h" /* quality1: per-item details after the CLOTHING list */
#include "bagwire.h" /* inv3a: a handed-over backpack's contents on ITEM_REQUEST / ITEM_CONFIRM */
#include "kitcell.h" /* kit2 (owner 102): each kit item's cell on MSG_CLOTHING, and the copy's placement rule */
#include "itemstatwire.h" /* items9: ITEM_MOVE op 4 CHARGES, op 0's [BAG1][OWN1] tail, snapshot detail stats, itemtest set */
#include "statswire.h"    /* S1 (read-stats): the 44-value stats block - SPAWN's tail and MSG_STATS */
#include "statewire.h"     /* K1 (read-carry): the MSG_STATE payload - SendState and OnState call these */
#include "limbwire.h"      /* LIMBS: the limb block of MSG_STATE and the copy's plan per limb */
#include "squadtest.h"     /* statstest: the lever's argument shape and the stat levels */
#include "bedmatch.h"      /* BED1 (T263/T264): which interior piece a bed key names - PoseBedViaLayout calls this */
#include "bedhandoff.h"    /* the owner enters a bed from the carry, as the engine does */
#include "ownedmirror.h"  /* O1 (recheck-c2b): the owned-uid mirror IsUidMineAnyThread reads from worker threads */
#include "ragdollrest.h"  /* R3 (read-ragdoll): the pending rest moves the 0x7D38D0 detour takes */
#include "installtext.h"  /* installer1 (stage 6): Setup's Plugins_x64.cfg edit, the Steam library list, the Workshop rule */
#include "runboost.h"   /* T-189 runboost (H051): running / boost / decide / ceiling */
#include <cmath>        /* T-189: std::sqrt, std::fabs */
#include "getupcrawl.h"   /* T-178 crawl1 (H050): owner up / copy ready / MOVE drives a crawler */
#include "copyjudge.h"   /* T-573: the copy judge held while the simulation is stopped; a given-up copy re-armed */
#include "sneakwire.h"    /* the owner's stealth mode in STATE bit 7, and what the copy's game does with it */
#include "slotwire.h"      /* stand1: the per-slot stand-in names and the N-party zone owner swap */
#include "copykeep.h"     /* copy1: a copy keeps its blood across the stats push; a refused kit item goes to its own section */
#include "pathlead.h"     /* D3 (read-pathend): the path drive's lead / near-end / too-close / landed arithmetic */
#include "followplay.h"   /* the follow drive: the stamped recording, the replayed target, never past N, the stop rules */
#include "floorplace.h"   /* T-424: a copy on another floor than its stopped owner is placed at the owner's height */
#include "stateptr.h"     /* T-296 fix 2 (H055): a platoon's state record (+0x40) lives outside its own container */
#include "groundkey.h"    /* inv5 phase 0 (P090): the ground key - compose, parse, near miss, bag match */
#include "ctxkey.h"       /* the context map's squad-id key: the id and the faction the squad was built under */
#include "givelate.h"     /* give1: the late-give removal, the cancel refunds, the confirmdelay parse */
#include "itemescrow.h"   /* inv7c: the escrow state machine */
#include "groundshow.h"   /* p105g-test: P105g - the shown ground pickup */
#include "groundname.h"   /* ground items' shared names */
#include "groundrec.h"    /* T-697: an area's ground items as one world-server record, and the server's rule */
#include "groundsync.h"   /* T-697 step 2: what a game does with an area's ground record */
#include "boxrec.h"       /* T-823: one box's contents as one world-server record, and the server's rule */
#include "piecerec.h"     /* one built piece as one world-server record, and the server's rule */
#include "zonerec.h"      /* T-609 part 2: a zone record's stamp (H1) and the world server's holder rule (W) */
#include "walkdrop.h"     /* T-609 part 2 (H4): a dismantled building leaves the memoised building list */
#include "holdwire.h"     /* P105 build 2: HOLD / LAND - the trailers, the absorb rule, where a LAND goes, the answers */
#include "ownpick.h"      /* P105: an own pickup out of a container this game writes - its removal published at once */
#include "rcgive.h"       /* T-1079: a give made without the cursor - the drain's pairing by record and units */
#include "showmove.h"     /* T-164 non-shop road: the show-at-once decisions */
#include "ownerwire.h"    /* inv6: the stolen mark as an identity - the OWN1 block, the journal b2 fields, the D5-2 choice */
#include "gearwire.h"     /* an item's uniform and crafter - the GEM1 block, the GEML tail, the journal b3 fields */
#include "itemtail.h"     /* which optional block comes next in an ITEM_REQUEST / ITEM_CONFIRM tail */
#include "standinpurge.h" /* inv7a: the stand-in purge at load - one character's steps */
#include "copysquadid.h"  /* a copy squad never saves under an id another squad holds */
#include "savesquadforeign.h"   /* T-710: whose characters a save squad file holds; the fence for this game's own record */
#include "ownrec.h"       /* mmo1: the own-squad record writer's names, index line, write decisions and pacing */
#include "handrec.h"      /* T-889 step 2: the hand-over record pp.handover, its decisions and MSG_ITEM_HANDOVER */
#include "permdelete.h"   /* the permanent delete's path-inside check and which saves go */
#include "copybody.h"     /* crash2: the animal copy body-rebuild skip rule */
#include "kolook.h"       /* a copy met while its owner says knocked out is dressed before its knockdown */
#include "humantest.h"    /* T-293: the human class rule */
#include "orphanpurge.h"  /* orphan1: the leaf gate's unlinked arm and the orphan cleanup's steps */
#include "dirlevels.h"    /* tickwait: EnsureDir's folder levels */
#include "arearec.h" /* T-1084 step 3: the area-recorded marks (R5), the unrecorded-wake refusal (R6), the authority's build arm */
#include "squadwriter.h" /* T-1 B1: one writer per NPC squad - follow-leader, the receiver's squad-position take, copy-squad keep-awake */
#include "squadwriterreg.h" /* T-1084 step 5: the world server's per-squad writer register */
#include "townverwire.h" /* TOWN_VER: one town version per placed town, the revision rule, the key, owner decision 260 */
#include "squadlead.h"   /* T-1 B1 restructure: MSG_SQUAD_LEAD wire and the book of the other game's announcements */
#include "removalreason.h"   /* M7a3: the engine's removal reason - an unload is never sent as a death */
/* T-1 B3 restructure (protocol 89): keeperwire.h retired - the squad's money is tested through squadlead.h */
#include "tradecharge.h"   /* T-164 B4-1: the real charge of a player trade; the keeper lever's affordable pick */
#include "shopstock.h"
#include "keeperaim.h"   /* lev343-tests (owner 343 b): where `buytest keeper` aims */
#include "townprices.h"   /* T-619: the host's town price tables, the joiner's choice, the holder's price-leg test */
#include "shopwire.h"   /* T-164 B4-4 intercept: the gate, amount/price, reservation/escrow, holder verdicts, the shop blocks */   /* T-164 B4-4: shop recognition by the trader's home pieces, the piece by item record, the keeper's pot */
#include "shopcancel.h"   /* the holder's steps on a home shop trade's cancel and settle-only request, over a copy of the record */
#include "wanderrestock.h"   /* T-1 B2 (owner 109): who restocks a wandering trader */
#include "reqserial.h"   /* T-336: the request counter's random base per launch */
#include "celldrift.h"   /* T-172 tinc: the copy's cell-drift repair and the leftover test */
#include "logbudget.h"   /* T-879: the release build's quiet-logging gate and the drift test */
#include "linkkeep.h"   /* T-1118 (a): the save window that keeps the world-server link serviced - nesting, kinds, totals, log cap */
#include "linekeep.h"   /* T-910: the bug report's keeper-lines.txt - the first and the newest lines of one kind */
#include "logrotate.h"   /* T-417: the names of the mod log's kept copies of earlier launches */
#include "pausemenu.h"   /* T-514: where the pause menu's rows go */
#include "addrlookup.h"  /* T-524: the internet address from the lookup websites */
#include "bugreport.h"   /* T-461: REPORT A BUG - words, scrub, compressor, budget, zip, the nearby-log messages */
#include "portraitdefer.h"   /* T-804: the dialogue window's portrait refresh, held off the AI thread */
#include "dlgwinguard.h"     /* T-831: the dialogue window never calls through a removed character */
#include "twinsleep.h"       /* T-761: a save-born non-player twin no game drives here - put to sleep or taken */
#include "editorstate.h"     /* the editorstate verb's argument and the editor's per-recruit body test */
#include "teamwire.h"   /* T-546 step 3: the player-faction membership table and TEAM (60) */
#include "teameffect.h"   /* T-546 step 4: what membership does - the pin, the tag's colour and line 2, base access, a notice's names */
#include "teamstanding.h"   /* T-546 step 5: the shared standing */
#include "teamresearch.h"   /* T-546 step 6: the shared research */
#include "teamscreens.h"   /* T-546 step 7: the faction screens - the bottom line, the boxes, the message lines */
#include "teamorders.h"   /* T-546 step 7: the orders a teammate's character is not offered */
#include "teamally.h"   /* T-546 (owner 512): a teammate's faction answers ally / not-enemy as one's own */
#include "uniqslot.h"     /* first sight of an alive named character, the elsewhere set, the dead set, the record-member filter */
#include "fallenwire.h"   /* T-556: the fallen list - snapshot byte form, list trimming, squad choice, road */
#include "moneywire.h"   /* T-810: money between players - the wire, the pp.money record, the ledgers */
#include "resurrectfee.h"   /* T-556: the resurrection fee and its host options */
#include "fallentab.h"   /* T-556: the FALLEN tab - its words and states */
#include "relside.h"   /* T-563: the entry a faction-relations changer wrote */
#include "titleart.h"   /* T-513: the mod's title art - the cover crop and the note's place on it */
#include "factionkey.h"   /* an NPC faction travels by its stringID */
#include "templatekey.h"   /* T-922: a character's template travels by its stringID */
#include "u8path.h"   /* UTF-8 <-> UTF-16 for the wide file calls */
#include "corpsedecay.h"   /* T-653: a copy's dead body is held short of the rot limit; its owner decides */
#include "spawnpace.h"   /* T-599: copies made under a frame budget, in arrival order */
#include "fontguard.h"   /* T-730: the font guard - MyGUI's getFontName body, the jumps to it, the empty name */
#include "zonekeep.h"    /* RESET WORLD's kept pieces: the area record container, the players' pieces, the kept list (and poskey.h) */
#include "keptwire.h"    /* KEPT: the kept pieces laid back - the wire, the row state and both sides' decisions */
#include "townkeep.h"    /* TOWNS: the players' outposts carried into a reset world */
#include "baseshow.h"    /* T-811: SHOW - the teammates' outposts on the map */
#include "carrycheck.h"  /* carryread / carryset: RESET WORLD's carry as counts and exact sums */
#include "squadorigin.h"  /* a squad's origin slot: its number's block, or for a save squad the game that runs it now */
#include "numberblock.h"  /* T-642: where a faction's squad counter stands in this game's number block */

#include <cstdio>
#include <limits>   /* par24 */
#include <algorithm>   /* par24 */
#include <io.h>        /* _findfirst: t_address_tables_count_matches */
#include <cctype>      /* std::isalnum: t_address_bindings_fit */
#include <cstring>
#include <cstddef>   /* offsetof */
#include <string>
#include <vector>
/* the headers test_main.cpp included further down, among its tests */
#include "aoi.h"   /* T-985 step 2A: the ring math, the classification, the per-game ledger and the area index */
#include "ownpark.h"   /* a stolen mark parked until its owner is on this game */
#include "townwake.h"   /* T-936: a newly founded outpost woken at the next safe point */
#include "buildnear.h"   /* T-923: the engine's "Can't build too close to another town." decision (the canbuild lever) */
#include "buildpick.h"   /* T-906: the building-swap pick's list walk and the world-stable draw */
#include "typetally.h"   /* the per-link message-type tally of the [NET] TRAFFIC REPORT line */
#include "squadbirth.h"   /* T-999: a squad's birth flag - the keep question, the discard delete, the world server's judgement, the retire rule */
#include "shoprec.h"   /* one shop building's restock state as one world-server record, and the restock gate's rules */
#include "setupstock.h"   /* a shop's set-up restock and a keeper's unknown money when the counters hold the world server's stock */
#include "windowbound.h"   /* T-986 (drift bound): the native combat window's distance bound */
#include "townmove.h"   /* T-924 fault 2: a teammate's outpost copy moved to the sent centre */
#include "squadsplit.h"   /* T-948 b: a squad of copies is not split; a copy is not moved to a new squad */
#include "sameidpair.h"   /* T-1097: one live platoon per world squad id - the same-id copy yield, its twin step and the pair guard */
#include "tplnobody.h"   /* T-1090: a building whose resident template makes nobody */
#include "thingaccess.h"   /* T-942: own / teammate / friend / stranger (owner 1187 a, 1188, 1189 a) */
#include "itemnotice.h"    /* T-942 / owner 1191: the item notice's grouping and wording */
#include "npcsense.h"   /* T-943: the NPC senses' and memory's player tests and the teammate list */

using namespace coopclock;

namespace suite {

/* test_main.cpp: the runner's counters */
extern int g_tests;
extern int g_failed;
extern const char* g_name;
extern int g_thisFailed;
extern int g_skipped;
extern int g_thisSkipped;

/* test_main.cpp */
void Fail(const std::string& what);
void Skip(const std::string& why);
std::string D(double v);
std::string I(int v);

using namespace coopwriter;

/* suite_part1.cpp */
const char* KindName(int k);
const char* VerdictName(int v);
void t_panel_layout_is_centred_and_no_label_keeps_the_old_55_per_cent_share();

struct TestRow { const char* name; void (*fn)(); };

/* suite_part2.cpp */

/* copy3: a fake section grid for coopkeep::KitFirstFreeCell - taken = any occupied cell under the footprint; records
   the furthest right/bottom edge ever asked, so a probe outside the grid is caught */
struct Copy3Grid { int iw, ih, maxRight, maxBottom, asked; unsigned char cell[8][8]; };
int Copy3Taken(void* ctx, int x, int y);
void Copy3Init(Copy3Grid* g, int iw, int ih);
owedpop::Row T581Row(unsigned kind, const char* key, const char* sid);

/* suite_part3.cpp */
coopprof::SaveDirSeen Pp5Dir(const char* name, const char* world, const std::string& prof, unsigned gen, unsigned long long t);
void GearTabFields(const std::string& s, std::vector<std::string>* f);
void Mmo2PutKeyI(coopown::PodBuf* b, const char* k, int v);
void T802Rec(std::vector<char>* v, int type, const std::string& sid, int handS, int charCount, const std::vector<std::string>& refs,
             const std::vector<std::string>& targets, const std::vector<std::vector<std::string> >& states, const char* handPrefix = 0, int handTo = 0,
             const int* money = 0);
std::vector<char> T802Squad(bool withA, bool withB, int charCount, bool shareItem, bool listA, int variant = 0);

/* suite_part4.cpp */
std::string T693Key(float x, float z, float y, coopground::GroundKeyParts* parts);
struct T313Sink
{
    std::map<std::string, std::string>* dst;
    size_t operator()(const std::string& k, const std::string& v) { (*dst)[k] = v; return k.size() + v.size(); }
};
std::string T313ReadFile(const char* path);
/* ---- A RECORD A GAME ALREADY HOLDS CROSSES THE WIRE AS ITS HEADER ONLY (src/common/recordfeed.h HELD, FeedHeldCovers): the HELD
   wire, the rule against the game's own apply on every small combination of stamps, and a join whose world changed during the load ---- */
struct T985Row
{
    long long file, pos; unsigned int owner;
    T985Row() : file(0), pos(0), owner(0) {}
    T985Row(long long f, long long p, unsigned int o) : file(f), pos(p), owner(o) {}
    bool operator==(const T985Row& o) const { return file == o.file && pos == o.pos && owner == o.owner; }
};
struct T985Srv
{
    int hasFile; long long file, pos; unsigned int owner; size_t payload;
    T985Srv() : hasFile(0), file(0), pos(0), owner(0), payload(0) {}
    T985Srv(int h, long long f, long long p, unsigned int o, size_t n) : hasFile(h), file(f), pos(p), owner(o), payload(n) {}
};
std::string T985Key(int i);
void T985ApplyFull(std::map<std::string, T985Row>* t, const std::string& k, long long f, unsigned int o);
void T985ApplyPos(std::map<std::string, T985Row>* t, const std::string& k, long long p);
const size_t kT985Header = 120;
/* the world server's page sender: SendRecordTo (the full record, then a later position), or the header only when the game holds it */
struct T985Wire
{
    std::map<std::string, T985Row>* game;
    const std::map<std::string, coopfeed::HeldRow>* held;
    unsigned int full, header; size_t fullBytes, headerBytes;
    T985Wire(std::map<std::string, T985Row>* g, const std::map<std::string, coopfeed::HeldRow>* h) : game(g), held(h), full(0), header(0), fullBytes(0), headerBytes(0) {}
    size_t operator()(const std::string& k, const T985Srv& r)
    {
        if (held != 0)
        {
            const std::map<std::string, coopfeed::HeldRow>::const_iterator h = held->find(k);
            if (h != held->end() && coopfeed::FeedHeldCoversKey(k, h->second.fileStamp, h->second.posAt, h->second.owner, r.hasFile, r.file, r.pos, r.owner) == 1)
            {
                T985ApplyPos(game, k, coopfeed::FeedHeldHeaderStamp(r.hasFile, r.file, r.pos));
                ++header; headerBytes += kT985Header; return kT985Header;
            }
        }
        if (r.hasFile && r.file > 0) { T985ApplyFull(game, k, r.file, r.owner); if (r.pos > r.file) T985ApplyPos(game, k, r.pos); }
        else T985ApplyPos(game, k, r.pos);
        ++full; fullBytes += r.payload + kT985Header; return r.payload + kT985Header;
    }
};
std::map<std::string, coopfeed::HeldRow> T985HeldAtServer(const std::map<std::string, T985Row>& game, const std::map<std::string, T985Srv>& srv);
void T985Snapshot(const std::map<std::string, T985Srv>& srv, T985Wire* w);
void T985TableAsWritten(const std::map<std::string, T985Srv>& srv, std::map<std::string, T985Row>* g);

/* suite_part5.cpp */
cooffect::EffectMsg T327Req();
coopprof::SaveDirSeen T490Dir(const char* name, const char* world, const std::string& prof, const std::string& id, long long savedUnix);
swfallen::Fallen T556Snap(unsigned int uid, const char* name);

/* suite_part6.cpp */
keptwire::Row T36b2Row(unsigned row);
std::string T36b2Pieces(const std::vector<keptwire::Row>& rows);

/* suite_part7.cpp */
coopgrec::GroundRow T697Row(const char* name, const char* sid, unsigned int qty, float x);
bool T697RowSame(const coopgrec::GroundRow& a, const coopgrec::GroundRow& b);

/* test_main.cpp */
cooppar::ParityItem T823BoxItem(const char* sid, int x, int y, int qty);
std::vector<char> T1045Rec(const std::string& key, unsigned int gen, int writer, int owner, unsigned int flags, unsigned int placement, float progress);

/* suite_part7.cpp */
int T1047cSawWrites(int bands);
int T1047cSawFull(int bands, int* fullOk);
int T1047cFlapWrites(unsigned int stepMs, unsigned int ms, unsigned int holdMs);
size_t T1080BodyEnd(const std::string& src, size_t at);
size_t T1080DefAt(const std::string& src, const char* head);
int T1080BodyHas(const std::string& src, const char* head, const char* needle);
townkeep::TownRow T1047fRow(int owner, float x, float z, unsigned id, const char* key);

/* the tests the suite_part files define, in each file's order */

/* suite_part1.cpp */
void t_wl_r1_refuses_when_not_loaded_here();
void t_wl_r2_sole_loaded_writes();
void t_wl_r3_tie_break_by_slot();
void t_wl_peer_unknown_is_never_sole_loaded();
void t_wl_exactly_one_writer_over_consistent_views();
void t_wl_r4_refuses_past_the_grace_from_every_input();
void t_wl_no_notebook_slot_answers_no_answer();
void t_wl_every_rung_is_reachable();
void t_clock_decide_never_writes_below_current();
void t_clock_decide_engine_held_writes_nothing();
void t_clock_decide_join_jump_is_bounded_and_two_way();
void t_clock_decide_forward_bound_refuses();
void t_clock_decide_cap_is_a_fraction_of_the_measured_advance();
void t_relay_resting_speed_is_the_last_real_one_not_zero();
void t_relay_consensus_is_the_minimum_over_games_that_voted();
void t_clock_wire_round_trip_and_truncation();
void t_clock_speed_mod_change_is_a_vote();
void t_clock_shared_hours_interpolates_and_refuses_a_stale_reading();
void t_clock_decide_dead_band_and_join_jump_precedence();
void t_clock_forward_set_never_below_current();
void t_clock_dialogue_pause_suppressed_in_a_session_only();
void t_clock_relink_no_jump();
void t_clock_pull_converges();
void t_worldgen_bump_once_per_load();
void t_clock_pull_seed_bound_refuses_in_and_out_of_the_window();
void t_crc32_known_vectors();
void t_storemeta_v5_round_trip();
void t_storemeta_v4_upgrades_to_v5();
void t_store_refuses_a_truncated_payload();
void t_store_refuses_a_crc_mismatch();
void t_store_promotes_the_previous_version();
void t_store_upgrade_defers_an_unreadable_payload();
void t_store_upgrade_promotes_a_prev_pair_over_a_torn_payload();
void t_store_pass2_keeps_the_position_instead_of_dropping_the_record();
void t_store_an_empty_or_damaged_meta_line_is_refused_not_guessed();
void t_store_every_folder_shape_keeps_every_committed_version();
void t_store_an_unreadable_current_payload_is_never_promoted_over();
void t_uniques_merge_after_a_deferred_load_refuses_a_revival();
void t_clock_merge_after_a_deferred_load_never_moves_backward();
void t_p97_leaf_nomap_refuses_every_slot();
void t_p97_announce_nomap_refuses_every_slot();
void t_cfg_write_round_trips_through_the_parser();
void t_cfg_addr_rule();
void t_cfg_panel_field_validators();
void t_cfg_sanitise_typed();
void t_cfg_incomplete_reason();
void t_pp3_datadir_choose();
void t_pp3_identity_migration_table();
void t_pp3b_identity_state_table();
void t_pp3c_bad_player_cfg_two_starts();
void t_pp3d_identity_kind_and_box();
void t_pp3e_case2_only_when_nothing_recoverable();
void t_pp3b_session_cfg_strips_identity();
void t_pp3b_old_panel_cfg_and_prefill();
void t_pp3b_store_root_default();
void t_pp3_player_cfg_round_trip();
void t_pp3_cfg_datadir_key();
void t_pp3_test_keys_layered();
void t_cfg_roots_absent_is_unchanged();
void t_cfg_roots_parse_refuse_and_round_trip();
void t_cfg_parser_bounds_and_notes();
void t_panel_fit_is_decided_from_the_window_rectangle_alone();
void t_panel_refusal_advances_the_streak_and_gives_up_at_the_cap();
void t_kit3_owned_mirror_exact_only_while_nothing_is_missing();
void t_o1_owned_mirror_matches_the_set_through_tombstones_and_full();
void t_retire_withdraw_a_retired_uid_is_owed_an_unload();
void t_retire_withdraw_link_down_defers_and_never_consumes();
void t_retire_withdraw_restore_cancels_and_a_same_tick_pair_sends_nothing();
void t_retire_withdraw_an_unannounced_uid_is_resolved_without_a_send();
void t_b10_v5_line_upgrades_to_v6_with_an_unknown_owner();
void t_b10_v2_v3_v4_chain_to_v6_keeps_the_payload_fields();
void t_b10_v6_round_trips_every_owner_value();
void t_b10_a_bad_owner_field_is_refused_not_zeroed();
void t_b10_translate_predicate_and_every_one_of_its_four_exits();
void t_b10_owner_field_from_slot();
void t_b10_rotation_tells_a_locked_source_from_a_locked_destination();
void t_b10_a_failed_rollback_never_also_reads_as_a_clean_one();
void t_b10_the_uniques_retry_republishes_the_whole_merged_map();
void t_b10_c11_the_zone_file_writer_follows_the_ladder();
void t_b10b_an_unknown_owner_round_trips_through_the_index_line();
void t_b10b_the_repush_stays_owed_while_the_link_is_down();
void t_b10b_a_store_protocol_mismatch_is_refused_in_words();
void t_panel_one_guard_owns_both_size_minimums();
void t_panel_numbers_carry_no_locale_separators();
void t_panel_status_picks_one_of_five_states_and_a_host_alone_is_not_connected();
void t_panel_notebook_and_give_up_sentences_name_their_numbers();
void t_d3_path_lead_near_end_extend_and_landed();
void t_follow_playback_target_at_delay();
void t_follow_bounded_guess_and_limit();
void t_follow_keep_walk_from_owner_speed();
void t_follow_never_past_and_pace();
void t_follow_stop_detect_and_order();
void t_follow_npc_copies();
void t_follow_replay_idle();
void t_follow_stop_trailing_report();
void t_follow_behind_schedule();
void t_follow_stop_left_without_moving_report();
void t_follow_goal_resend();
void t_follow_movestop_on_live_road();
void t_join1_notebook_addr_for_joiner();
void t_addr1_notebook_dial_from_welcome();
void t_t632_create_pos_agrees();
void t_t296_state_wake_decide();
void t_addr_pattern_refusal_clears_every_slot();
void t_rk1_addr_hooked_call_only_verdict();
void t_rk1_addr_uses_all_call_only();
void t_addr_signatures_resolve_every_kind_and_refuse_anything_less();
void t_addr_fingerprint_is_the_four_numbers_and_a_bad_header_writes_nothing();
void t_addr_table_parser_reads_every_kind_and_names_what_it_refuses();
void t_addr_a_corrupted_table_refuses_and_never_passes_by_default();
void t_door_a_holder_word_plays_the_swing_and_the_rest_snap();
void t_door_a_played_swing_is_ours_until_it_lands();
void t_door_the_lock_word_is_applied_by_the_engine_lock_calls();
void t_door_every_lock_word_is_reachable();
void t_door_a_lock_no_detour_saw_is_a_local_write();
void t_door_a_live_row_lock_no_detour_saw_is_queued();
void t_door_last_actor_is_reported_not_reverted();
void t_door_only_terminal_states_are_compared();
void t_door_give_up_counts_corrections_that_were_undone();
void t_door_drain_drop_and_re_registration_booking();
void t_door_churn_re_arms_after_thirty_seconds_of_agreement();
void t_door_origin_byte_is_parsed_and_an_unknown_one_is_ignored();
void t_door_a_lost_report_retries_and_is_abandoned_at_the_bound();
void t_p113_reuse_decide();
void t_t920_notstored_judge();
void t_livechar_judge();
void t_p124_stop_report_reading();
void t_p124_stop_measure();
void t_door_an_abandoned_report_stays_abandoned_until_the_door_agrees();
void t_door_a_holder_word_is_refused_only_for_a_settled_own_door();
void t_door_a_key_with_no_row_resolves_through_the_parent_building();
void t_b12_the_conflict_rule_takes_every_branch();
void t_b12_the_writtenat_rule_would_have_let_the_replay_win();
void t_b12_a_journal_line_round_trips();
void t_b12_a_corrupt_journal_line_is_dropped_not_guessed();
void t591_home_key_rides_the_v7_line();
void t805b_player_house_and_cleared_home();
void t755_copy_home_owed_and_load_override_keep();
void t_b12_a_v6_index_line_upgrades_to_v7_with_seq_zero();
void t_b12b_a_seq_echo_applies_to_a_known_key_and_is_counted_for_an_unknown_one();
void t_b12a_the_door_drain_is_unchanged_by_the_outage_queue();
void t_b12c_a_zero_sequence_carries_no_information();
void t_b12c_an_entry_behind_this_games_own_replay_is_not_a_foreign_conflict();
void t_b12c_the_own_ahead_mark_travels_in_the_journal_line();
void t_b12c_only_state_families_supersede_and_the_cap_is_bounded();
void t_t962_store_sock_could_arm_only_while_a_welcome_can_come();
void t_b12e_a_down_link_with_a_server_configured_is_re_dialled();
void t_b13_areaclaim_takes_every_arm_including_the_grace_boundary();
void t_t592_frozen_area();
void t_restored_claim_released_when_its_holder_loads();
void t_b13_the_three_file_lines_round_trip_and_a_corrupt_line_is_dropped();
void t_b13_a_known_id_gets_its_old_slot_even_when_a_new_id_dialled_first();
void t_b13_the_playerid_survives_a_cfg_round_trip_and_a_v41_game_is_refused();
void t_b13c_a_silent_old_connection_on_the_same_id_is_evicted_and_a_live_one_refuses();
void t_link1_the_game_link_outlasts_a_load_and_a_joiner_redials_with_backoff();
void t_m1_the_notebooks_named_limits_are_refused_in_words();
void t_b13b_a_refusal_names_its_reason_and_only_the_protocol_one_mentions_deploying();
void t_w1b_an_absent_reporter_keeps_its_areas();
void t_w1b_a_present_reporter_is_taken_exactly();
void t_p1_spawn_round_trips_the_age_and_an_old_payload_reads_0();
void t_p1_a_bad_age_is_clamped_and_a_truncated_spawn_is_refused();
void t_k1_state_round_trips_with_and_without_carrying();
void t_r3_state_round_trips_with_and_without_rest();
void t_pose_state_round_trips_with_and_without_actpose();
void t_bed1_state_outer_key_round_trips_and_50_payload_reads_empty();
void t_par5_state_hunger_round_trips_absent_and_cut_reads_not_carried();
void t_limbs_state_block_round_trips_and_malformed_reads_absent();
void t_limbs_plan_limb_per_owner_and_copy_state();
void t_limbs_quality_refit_robot_class_and_limbtest_args();
void t_statstest_args_and_levels();
void t_par6b_medical_update_death_call_by_return_address();
void t_bed1_pick_piece_key_exact_near_tiebreak_and_refusals();
void t_bed1_pick_refuses_a_lone_piece_a_floor_away();
void t_r3_rest_table_post_take_withdraw();
void t_s1_stats_block_round_trips_and_refuses_truncated_and_nonfinite();
void t_s1_send_rule_and_spawn_stats_tail();
void t_quality1_garment_details_block();
void t_bag23_path_block();
void t_p94_parity_serve_trusted();
void t_t481_parity_ground_tail();
void t_t697_listing_left();
void t_t697_catchup_world_add();
void t_t481_ground_catch_up_plan();
void t_t481_ground_catch_up_rejoin();
void t_p109_parity_pull();

/* suite_part2.cpp */
void t_bag23_parity_box_packs();
void t_kit2_kit_cells_block();
void t_bag23_kit_bags_block();
void t_inv3a_bag_rows();
void t_inv3a_fold_decisions();
void t_t600_bail_and_slave_ask();
void t_arrest2_prison_wire();
void t_p42_restraint_lock();
void t_bed_prison_wire();
void t_heal1_treat_wire();
void t_t1b1_keep_awake();
void t_t1b1_follow_action();
void t_t1b1_xfer_take();
void t_m7a3_removal_reason_class();
void t_m7a3_removal_is_unload();
void t_m7a2_handover_once();
void t_m7a2_context_platoon_record();
void t_t574_block_squad_other_game();
void t_m7a3f3_announced_three_state();
void t_m7a3f3_handed_over_supersede();
void t_m7a3f3_handed_over_wake();
void t_m7a3f3_handed_over_sweep();
void t_m7a3f4_retire_refused_when_mine();
void t_t1b1_reason_and_pos();
void t_t1b1_agreed_leader();
void t_t1b2_wander_restock_decide();
void t_t1b2f_wander_split_deadlines();
void t_t1b3r_squadlead_cats_wire();
void t_t1b3r_squadcats_book_and_copy_write();
void t_t1b3r_handover_adoption_order();
void t_t1b3r_inflight_hold();
void t_t1b3f_m1_no_copy_write_into_own_pot();
void t_t1b3f_m2_undo_decided_at_send();
void t_t1b3f_m2_announced_net_of_inflight();
void t_t1b3f_m2_takeover_held();
void t_t1b3f_m3_final_announce_before_release();
void t_t1b3f_l1_undo_exact_price();
void t_t1b3f_l3_resends_gated();
void t_t1b3f_l4_stale_cats_record_cleared();
void t_t1b1_squadlead_wire();
void t_t1b1_squadlead_book();
void t_t1b1_announced_pair();
void t_t1b1_crossing();
void t_t1b1_engine_leader();
void t_sig97a_scan();
void t_address_tables_count_matches();
void t_recruit3_mult();
void t_settings2_gp_values();
void t_prof1_profiles();
void t_names2a_faction_row();
void t_h068_default_faction_numbers();
void t_h069_freed_names_are_free();
void t_t368_unique_names();
void t_t368_format_owed_reply();
void t_t368_names_all_typed();
void t_names1_name_wire();
void t_recruit1_hire_wire();
void t_melee_decided_wire();
void t_melee_decided_decide();
void t_melee_decided_paced_and_counted();
void t_p104_shot_wire();
void t_t311_shot_reactions();
void t_slave1_slave_wire();
void t_p11f6_slave_gate();
void t_p11f5_chain_wire();
void t_p11_capture_wire();
void t_stand1_slot_wire();
void t_p106_owner_lookup();
void t_p106_zone_names_every_player();
void t_p106_fold1();
void t_p106_world_players_wire();
void t_p106_owner_two_bytes();
void t_area_writer_standins();
void t_copy1_keep_decisions();
void t_copy3_animal_route();
void t_t959_area_flag();
void t_t1076_ring1_party();
void t_t1077_area_assign_list();
void t_t1077_player_sector_wire();
void t_t1077_player_sector_list();
void t_t1077_onareas_rows();
void t_t1077_party_halo_list();
void t_t1077_receipt_and_roster_near();
void t_t1076_mid_load_defer();
void t_t392_defer_cause();
void t_t392_reoffer_decide();
void t_t581_wire();
void t_t581_table();
void t_t581_file();
void t_t581_load_cause();
void t_t581_commit();
void t_t581_two_games();
void t_t589_settle();
void t_t581_after_rerun();
void t_t581_fold();
void t_t438_residents_aside();
void t_towns2_rebuild_decisions();
void t_refill1_bar_decisions();
void t_loot2b_research_wire();
void t_loot2c_takes_wire();
void t_snap1_digest();
void t_par1_parity_wire();
void t_p15_destroyed_state();
void t_t903_help_harvest();
void t_build1b_place_wire();
void t_build1c_state_wire();
void t_build1h_place_owner_slot();
void t_build1h_house_owner();
void t_build1e_furniture_wire();
void t_build1f_roster_request_wire();
void t_t988_piececheck();
void t_t274_zonescan();
void t_t720_world_node_mine_state();
void t_help1_fold5();
void t_help1_fold4();
void t_help1_fold3();
void t_help1_wire();
void t_help1_fold();
void t_help1_decisions();
void t_p87_reconcile();
void t_p87f3_owner_rule();
void t_p87rf1_copy_marks();
void t_p87_copy_list_ceiling();
void t_p87root_copy_list();
void t_p14_tombstone_kill();
void t_p14f1_ledger();
void t_p14f2_ledger();
void t_p14f3_ledger();
void t_p14f4_folds();
void t_p14f5_holder();
void t_p87f1_tombstones();
void t_par13_owed_remove();
void t_house2_task_gate();
void t_house2_late_place();
void t_house2_owed_merge();
void t_house2_place_nonce_wire();
void t_build1d_remove_wire();
void t_crime5_bounty_wire_and_additions();
void t_par20_bounty_clear_reaches_the_owner();
void t_t356_sight_cells_one_per_other_player();
void t_t356_sight_stub_golden_bytes();
void t_t968_foliage_cells_stay_put();
void t_crime3_wire_round_trips_and_send_decision();
void t_crime_kinds_wire();
void t_crime_copy_write_decisions();
void t_crime_fold2_copy_held();
void t_crime_fold1_trespass_load_death();
void t_crime_relev_dedupe();
void t_p3_say_round_trips_and_refuses_hostile_lengths();
void t_p3b_send_event_gate_blocks_a_copys_own_chats();
void t_p26s0_talk_target_kind_splits_mine_partner_npc();
void t_p26s0_talktest_parse();
void t_p25_name_levers_parse();
void t_p26s6_talksight_parse();
void t_p26s1_talkwire_round_trip_caps_and_refusals();
void t_p26s5_talkwire_act_round_trip_caps_and_side_table();
void t_t430_carried_person_shares_and_hand_over();
void t_p26s1f1_talkwire_every_truncation_is_trunc();
void t_t737_talk_cursor_decisions_wire_and_rate();
void t_t737f1_talk_cursor_ask_click_and_fresh_hover();
void t_p25_talkwire_request_and_new_fields();
void t_p25_intercept_request_order_and_local_windows();
void t_p25f2_click_event_and_not_started_why();
void t_t719_copy_own_end_action_every_branch();
void t_t726_talk_order_log_due_outcome_and_nth();
void t_p25_talktest_nearcopy_parse();
void t_t430_lever_shapes_parse();
void t_p26s1_talkprompt_parse();
void t_p3b_say_classify_splits_pass_through_and_checks_replays();
void t_p3b_say_marks_match_exact_text_once();
void t_p7s_weather_round_trips_by_region_name();
void t_p7s_weather_refuses_hostile_and_old_payloads();
void t_w1w_world_names_refuse_what_windows_refuses();
void t_w1w_display_names_and_the_playername_cfg_key();
void t_w1w_the_world_table_is_sorted_and_skips_what_is_not_a_world();
void t_t1038_editor_reset_population();

/* suite_part3.cpp */
void t_pp5_profile_save_folder();
void t_pp6_one_press_load();
void t_mp3_played_words();
void t_prof3_profiles_screen_words();
void t_mp5_game_options_rows();
void t_t556f_fee_rows();
void t_mp4_hosting_address_pick_and_players();
void t_w1w_the_migration_plan_moves_each_file_once_and_reverses();
void t_w1w_an_interrupted_migration_resumes_from_where_it_stopped();
void t_w2f_world_file_names_are_reserved_and_a_finished_migration_is_not_resumed();
void t_w2a_the_migration_journal_round_trips_and_refuses_what_it_never_writes();
void t_w2a_unmigrate_steps_never_overwrite_and_replay_is_resumable();
void t_w3_the_welcome_round_trips_with_and_without_the_world();
void t_w3_a_game_confirms_adopts_or_refuses_the_notebooks_world();
void t_t318_plugin_rows();
void t_settings5_mod_check();
void t_t246_mods_warn_and_admit();
void t_t246_fold_notice();
void t_settings5_fold_operator_only_and_recheck();
void t_loot2d_retract_reasons();
void t_mirror1_index_release_keeps_every_live_entry_findable();
void t_mirror1_adopt_drive_despawn_reclaim_decisions();
void t_t758_new_body_stale_row();
void t_mirror1_fold_row_refusal_sweep();
void t_mirror1_rejoin_fits_after_leave();
void t_t925_sweep_one_road();
void t_t354_fill_to_bound_then_refuse();
void t_t354_release_reuse_at_bound();
void t_t354_not_shown_to_owner();
void t_par1_takeone_route();
void t_t608_npc_buy_gate();
void t_t608_npc_buy_road();
void t_inv2b_direct_writes();
void t_inv2a_net_diff();
void t_inv4_owner_writer();
void t_inv4_one_writer_property();
void t_loot2c_route_decisions();
void t_inv5_ground_key_p1();
void t_t497_ground_tell_except_requester();
void t_t497_ground_recent_keys();
void t_p2p3p4_ground_route();
void t_inv5_ground_key();
void t_carry_sums();
void t_t832_item_tail();
void t_inv6_owner_wire();
void t_t853_give_lapse_revoke_and_late_yes_square();
void t_t889_give_row_by_player_presence_and_retry_hold();
void t_t1087_give_done_and_close_merge();
void t_give1_late_give_and_cancel_refunds();
void t_inv6p2_owner_tails();
void t_ground_player_mark();
void t_mmo1c_podbytes();
void t_mmo5_host_left();
void t_mmo5_fold();
void t_mmo4_leave_save();
void t_t251p2_created_save();
void t_mmo8a_build_record();
void t_ui5_panel_screens();
void t_n1_press();
void t_m11flip_world_door_press_and_redial();
void t_reset_world_panel();
void t_t810_money_fold();
void t_t810_money_fold2();
void t_t810_money_fold3();
void t_t810_money_fold4();
void t_mmo3_records();
void t_mmo3_fold();
void t_mmo2_overlay_roster();
void t_t802_moved_after_save();
void t_t1062_settle_per_person();
void t_t802_fold2_strip_rules();
void t_t802_strip_last_member();
void t_t823_squad_head();
void t_t823_restore_roster();
void t_t802_real_squad_files();
void t_mmo1c_compare_due();
void t_mmo1_ownrec();
void t_tickwait_dir_levels();
void t_orphan1_orphan_purge();
void t_inv7a_standin_purge();
void t_inv7a2_purge_triggers_and_id_carry();
void t_t707_copy_squad_id();
void t_inv7a3_own_id_supersede_and_woken_recheck();
void t_inv7e2_reloaded_copy_retire();
void t_restore1b1_ownhigh_checkpoints();
void t_restore1b1_fold();
void t_restore1b1_fold2();
void t_restore1a_world_guard();
void t_crash2_copy_body_decide();
void t_t500_ko_look_gate();
void t_t293_human_class_by_vtable();
void t_p5t_area_assign_ring1_lowest();
void t_area2_fold_sweep_request_and_unconfirmed_lease();
void t_mmo6_ledger();
void t_t251_overlay_window();
void t_hb_cursor_quota_takes_elapsed_ms_not_ticks();
void t_hb_ring_cycle_never_exceeds_the_period();
void t_hb_due_on_turn_floor_and_giant_rule();
void t_hb_budget_defers_and_never_drops();
void t_hb_budget_carries_its_overspend();
void t_hb_budget_item_move_is_not_held_by_the_debt();
void t_hb_wake_loaded_skip_is_the_first_soon_visit_only();
void t_hb_ring_at_fifty_squads_delivers_a_thirty_second_period();
void t_inv7c_escrow();
void t_hb_fold_oversized_record_admitted_into_an_empty_second();
void t_hb_fold_giant_does_not_block_the_ring();
void t_sqev_event_marks_the_record_due();
void t_sqev_burst_of_events_is_one_send_per_window();
void t_sqev_member_set_changes();
void t_sqev_non_writer_event_sends_nothing();
void t_sqev_money_and_home_rules();
void t_sqev_marked_groups_go_ahead_of_owed_ones();
void t_sqev_restock_names_the_squad_or_its_active_part();
void t_sqev_wake_window_and_note_verdict();
void t_sqev_window_closes_on_people_and_ends_after_drain();
void t_sqev_ring_reserves_before_it_fills();
void t_sqev_ring_bounds_money_notes();
void t_sqev_off_thread_event_is_queued_not_applied();
void t_inv7c_fold();
void t_inv7c_fold2();
void t_inv7d_owner_arrive();
void t_items9_stat_wire();
void t_par24_world_relations_table();
void t_par24_fold_standins_echo_operator_seed();
void t_t616_node_writer_mine_op();

/* suite_part4.cpp */
void t_t609_build_farm_kinds_apart();
void t_t904_wall_hit();
void t_t1044_power_network();
void t_p47_power();
void t_p47_look_queue();
void t_p47_offmain_verdict();
void t_p47_completion_switch();
void t_par16_farm();
void t_runboost_owner_runs();
void t_runboost_boost_speed();
void t_runboost_decide();
void t_runboost_ceiling();
void t_runboost_ease();
void t_crawl1_owner_up();
void t_crawl1_copy_ready();
void t_crawl1_move_drives();
void t_crawl1_state_bit();
void t_sneak_state_bit();
void t_sneak_copy_action();
void t_crawl1_goal_name();
void t_installer1_pcfg_add_remove();
void t_installer1_steam_paths();
void t_mig6_marker_parse();
void t_mig6_modscfg_lists();
void t_t522_modscfg_after_ok();
void t_t522_ogre_plugin_name_layout();
void t_t522_ogre_object_locator();
void t_m15_fixed_schedule_keeps_its_grid();
void t_m15_pass_budget_ends_at_budget_or_due();
void t_m15_flood_cannot_starve_the_jobs();
void t_m14_fifo_order_and_failure();
void t_m14_keygate_holds_in_arrival_order();
void t_m14_held_position_merge();
void t_m14_held_merge_per_record_id();
void t_lev343_keeper_aim();
void t_t1389_keeper_form();
void t_b44_piece_recognition();
void t_b44i_gate();
void t_b5_caravan_holder();
void t_b5f1_caravan_unadopted();
void t_b5f2_caravan_leader();
void t1007_caravan_one_road();
void t_b44i_precheck();
void t_t164p2_drag();
void t_b44i_amount_price();
void t_b44i_dedupe();
void t_b44i_fit();
void t_b44i_late();
void t_b44i_record();
void t_b44i_settle();
void t_t447_partial_sale();
void t_b44i_holder();
void t_b44i_wire();
void t_b44_piece_by_record();
void t_m4_old_prefix_collides_new_layout_does_not();
void t_m4_uidmake_never_collides_across_slots();
void t_m4_mint_refusals_are_counted_and_never_mint_zero();
void t_m4f_restarted_game_never_repeats_a_uid();
void t_m4f_slot_1023_and_the_last_block();
void t_m4f2_blocks_bound_to_their_link();
void t_m4f2_refused_grant_keeps_the_ask_pacing();
void t_m4f2_sweep_counts_once_per_stretch();
void t_m4f_minter_asks_at_three_quarters_and_refuses_stale_grants();
void t_m4f_spawn_for_own_uid_refused();
void t_m4s_seat_pick_lowest_free_with_space();
void t_m4s_freed_seat_continues_above_its_high_water();
void t_b44_keeper_pot_choice();
void t_t1b4r_charge_sign_and_exact_undo();
void t_t1b4r_charge_ticket_window();
void t_t1b4r_charge_is_trade();
void t_t1b4r_charge_claim_own_buyer();
void t_t1b42_refund_pick();
void t_t215_keeper_follows_refund();
void t_t215_keeper_class_at_move();
void t_t215_refund_runner_keeper_undone();
void t_t215_refund_local_only_keeper_undone();
void t_t215_refund_copy_keeper_left();
void t_t215_refund_keeper_unknown_paid();
void t_t215_announced_prediction_measured();
void t_t215_first_ticket_claimable();
void t_t1b42_keep_unmeasured();
void t_t1b42_claim_ticket0();
void t_t1b42_gate_names();
void t_t1b4r_keeper_pick_cheapest_affordable();
void t_t1b4r_cats_parse();
void t_m16_direct_until_full();
void t_m16_latest_keeps_newest_per_subject();
void t_m16_latest_keeps_reliability();
void t_m16_unreliable_passes_the_line();
void t_m16_must_deliver_never_dropped();
void t_m16_classify();
void t_m16_link_gone_and_edges();
void t_m16_drain_before_disconnect();
void t_m13_budget_is_enet_fragment_limit();
void t_m13_round_trip_in_order();
void t_m13_single_goes_as_itself();
void t_m13_split_at_budget();
void t_m13_large_goes_alone_after_open();
void t_m13_lanes_never_mix();
void t_m13_handshake_alone();
void t_m13_decode_refuses_whole();
void t_m13_saved_bytes();
void t_m13_clear_counts_open();
void t_m14_deferred_events_in_order();
void t_filename_non_ascii_distinct();
void t_filename_earlier_carry_decide();
void t_filename_earlier_carry_folder();
void t_filename_earlier_carry_guards();
void t_filename_long_ids_bounded();
void t_m14_gate_key_is_the_file_name();
void t_m14_loop_and_writer_per_file_order();
void t_t164_show_road_decisions();
void t_t338_ai_take_one_shows();
void t_t969_take_access_gate();
void t_t969_gate_decide();
void t_t338_f2_holder_split_absorbed();
void t_t338_f3_absorb_moves_to_its_take();
void t_shop_refusenext_answers_the_named_verdict();
void t_t290_avail_words();
void t_t290_relay_settle_rule();
void t_t290_relay_line();
void t_t290_file_version_and_debug_text();
void t_t306_orphan_hold();
void t_m5a_live_up_round_trip();
void t_m5a_live_down_round_trip();
void t_m5a_live_length_mismatch_refused();
void t_m5a_live_world_excludes_the_origin_and_the_lobby();
void t_m5a_live_slot_route();
void t_m5a_live_area_is_refused_until_m6();
void t_m5a_live_lobby_connection_has_no_slot();
void t_m5a_relay_peer_is_never_a_transport_id();
void t_p25f2_insidewire_roundtrip();
void t_p25f2_inside_verdict();
void t_t693_name_freeze_at_publish();
void t_t693_lookup_by_name_twins();
void t_t693_near_refuses_other_name();
void t_t693_rebind_after_address_change();
void t_t693_table_overflow();
void t_t693_reason_by_gone_memory();
void t_t693_dead_name_never_rebinds();
void t_t693_bare_key_reaches_named();
void t_t693_address_swap();
void t_t693_orphan_expiry();
void t_t693_name_for_check();
void t_t693_kept_id_match();
void t_t693_nearest_pair_first();
void t_t693_more_than_16_twins();
void t_t693_id_kind_wire();
void t_t693_catchup_pairs_named();
void t_t693_reason5_never_removes();
void t_p105g_pick_took();
void t_p105g_ask_action();
void t_p105g_settle_action();
void t_p105g_reask_and_due();
void t_p105b2_trailers();
void t_p105b2_absorb();
void t_p105b2_serve_step();
void t_t159_box_to_box();
void t_p105b2_land_plan();
void t_p105b2_answer_action();
void t_p105b2_how_at_drop();
void t_p105b2f1_hold_row_done();
void t_p105b2f1_late_answer();
void t_p105b2f1_scenario_land_overtakes_hold();
void t_p105b2f1_room_for_hold();
void t_p105b2f2_ground_drop_next();
void t_p105b2f2_answer_final();
void t_p105b2f2_hold_pre_step();
void t_p105b2f2_scenario_land_back_overtakes_hold();
void t_p105b2_scenario_reask_once();
void t_p105b3_publish_at_pickup();
void t_p105b3_deferred_remove();
void t_p105b3_back_add_after_give();
void t_p105b3_split_count_publish();
void t_p105b3_scenario_carries();
void t_p105b3_split_drop_road();
void t_p105b3_merged_give_split();
void t_t1079_units_moved();
void t_t1079_applies();
void t_t1079_give_whole_stack();
void t_t1079_give_part_stack();
void t_t1079_give_onto_stack();
void t_t1079_give_spread();
void t_t1079_take_whole_stack();
void t_t1079_take_part_and_onto();
void t_t1079_take_merged_last_unit();
void t_t1079_take_merged_shift_click();
void t_t1079_drag_merged_take();
void t_t1079_give_merged_source_stays();
void t_t1079_mismatch_back();
void t_t1079_not_this_road();
void t_t1079_back_conservation();
void t_t1079_guards();
void t_t1079_net_split_take();
void t_t1079_net_merge_give();
void t_t1079_net_not_paired();
void t_t1079_net_grouping();
void t_t1079_net_job_role();
void t_t1079_net_refusal_units();
void t_p105b3_scenario_split_give();
void t_m7bs4f1_talk_same_player();
void t_m7bs4f1_door_gen_by_player();
void t_m7bs4f1_side_road_three_games();
void t_p105g_rest_and_shortfall();
void t_p105g_f1_repeat_clock();
void t_p105g_f1_ring_room();
void t_p105g_f1_own_drop_and_cap();
void t_p105g_f1_take_back_rank();
void t_p105g_f2_ground_escrow();
void t_p25f2_inside_send_due();
void t_p25f2_inside_on_live_road();
void t_m7a_announce_decide_reannounces_gained_reporter();
void t_m7b_addr_route_owner_slot();
void t_m7b_addr_groups_and_whitelist();
void t_m7b2_item_addr_decide();
void t_m7b2f1_asked_set();
void t_m7b2f1_items_wait_for_world();
void t_m11flip_world_door();
void t_m11flip_name_slave_hire_roads();
void t_m7b4_side_roads();
void t_m7b4f2_build_addr();
void t_m7b2_item_confirm_wrong_sender();
void t_m7b2_item_move_not_owner();
void t_m7b_fold1_live_slot_not_in_world();
void t_m7b_fold1_prison_refuses_earlier_asker();
void t_m7b_fold1_no_owner_counts_once();
void t_m7a_char_stream_one_road();
void t_m7a_char_stream_types_and_routes();
void t_m7a_char_area_target_names_the_sector_left();
void t_m7a_relayed_stream_owner_gate();
void t_m7a_relayed_move_is_a_level();
void t_m7af_char_road_follows_the_peer_proof();
void t_m7af_relpeer_ok_learnt_for_two_in_world_games();
void t_m7af_reconnect_sweep_withdraws_what_was_not_resent();
void t_m7a2_sweep_only_what_the_owner_was_asked();
void t_m7a2_sweep_keeps_a_handover_in_flight();
void t_m7a2_owner_moved_rule();
void t_m7a2_world_road_ask_held_until_in_world();
void t_m7a2_move_held_until_its_state();
void t_move_hold_expiry_books_at_latest_spot();
void t_lost_book_takes_a_held_uid();
void t_spawn_not_kept_is_booked();
void t_one_road_per_uid();
void t_m7a2_ask_timeout_and_dedup();
void t_m7a2f_owner_moved_owed_and_announced();
void t_m7a2f3_owner_moved_road_gate();
void t_m7a2f_claims_for_not_asked_and_expired_copies();
void t_m7a2f_claim_conflict();
void t_m7a2f_expired_marks_withdrawn_and_not_asked_restored();
void t_m7a2f_ask_list_reset_per_link();
void t_m7a2f_missed_stamp_one_off_only();
void t_m7a2f2_silent_owner_keeps_its_copies();
void t_m7a2_missed_stamp();
void t_m7af_relayed_spawn_owner_gate();
void t_m5b_relayed_owner_slot_accepted_other_refused();
void t_m5b_record_before_slot_known();
void t_p25f1_request_only_to_session_peer();
void t_m5b_player_keys_never_alias();
void t_m5b_relation_pair_owner_slot();
void t_m5bf1_link_gen_after_batch();
void t_m5a_live_sender_is_the_notebooks_stamp();
void t_m6_area_route_right_games_only();
void t_m6_area_margin_and_crossing();
void t_m6_area_target_wire();
void t_m6_area_dilate_edges();
void t_m6_area_slots_past_32();
void t_m6_catchup_newly_and_intersect();
void t_m6_catchup_wire();
void t_m8_peer_gone_select_only_that_slot();
void t_m8_two_roads_remove_each_uid_once();
void t_m8_player_gone_wire();
void t_m8f1_take_pending();
void t_m8f1_mask_bits();
void t_m9_seat40_survives();
void t_m9_rekey_3_to_9();
void t_m9_wire_slot40000();
void t_m9_seat_assign_257();
void t_m9f1_row_varlen();
void t_m9f1_pending_unlisted_dropped();
void t_m9f1_pending_expires();
void t_m9f1_restart_sequence();
void t_m8f1_skip_session_peer();
void t_m11s3_closing_live_origin();
void t_m11s3_operator_gone();
void t_m11s3_source_priority();
void t_m11s3_road_local_never_dispatched();
void t_m11s3f1_world_road_joiner();
void t_m11s3f1_host_seen_road_switch();
void t_m11s3f2_closing_live_episode();
void t_m8f1_hold_rule();
void t_m6_road_local_inner_types();
void t_m6f1_catchup_rejoin_window();
void t_m5af1_one_delivery_per_destination();
void t_m5af1_answer_goes_back_the_road_it_came();
void t_m5af1_world_except_and_world_target();
void t_m5af2_peer_relay_book();
void t_m5af2_relsync_payload();
void t_m11c1_presence_decide();
void t_owner_in_world_eaten_copy();
void t_m11c1_tiebreak_three_games();
void t_m11c2_arrival_diff();
void t_m11c2_link_merge();
void t_t456a_counterpart_edges();
void t_m11c2_two_player_equivalence();
void t_m11c2f1_relink_after_outage();
void t_m11c2f1_fresh_record_beyond_link();
void t_build_sender_slot_now();
void t_others_in_world_beyond_link();
void t_ctx_key_faction();
void t_player_gone_takes_row();
void t_hire_done_to_every_game();
void t_stale_drop_and_sweep();
void t_reach_any_and_one();
void t_round_target_for_arrival();
void t_door_unsent_reoffer();
void t_m11c2f1_rebind_and_holds();
void t_m11a_stage_steps();
void t_m11a_join_load_allowed_table();
void t_m11a_live_dest_title_never();
void t_m11a_roster_round_trip();
void t_m11a_live_proto_decide();
void t_m11a_hello_tail_and_join_stage_wire();
void t_m11c5_in_world_resend();
void t_m11c5_presence_world_road_outage();
void t_m11c5f1_load_refused_connected();
void t_join_hold_operator_only();
void t_join_hold_started_by_roster();
void t_join_hold_dropped();
void t_t313_feed_wire_roundtrip();
void t_t313_feed_wire_malformed();
void t_t313_feed_forward_truth_table();
void t_t313_feed_ask_decision();
void t_t313_feed_paging_converges();
void t_t313_feed_end_marker_arms_once();
void t_address_bindings_fit();
void t_t985_feed_held_once();

/* suite_part5.cpp */
void t_t985_held_ground_box_full();
void t_t985s2a_ring_math();
void t_t985s2a_classify();
void t_t985s2a_ledger_index();
void t_t985s2a_first_areas();
void t_t985s2b_area_done_wire();
void t_t985s2b_area_keys();
void t_t985s2b_area_done_stale();
void t_t985s2b_area_settled_other_pending();
void t_t985s2b_marker_then_area_done();
void t_t985s2b_all_equals_old();
void t_t985s2b_sibling_clear();
void t_t985s2b_server_sends_area_done();
void t_preload_aoi_ring();
void t_preload_aoi_choose();
void t_t346_wire_roundtrip();
void t_t346_want_rule();
void t_t346_index_walk_pages();
void t_t346_flow_fetches_exactly_the_newer();
void t_t346_wait_exit_rule();
void t_t346f1_fetch_cut_by_bytes();
void t_t346f1_position_only_not_current();
void t_t313f1_gone_reaches_unsubscribed();
void t_t313f1_seq_rises_within_a_link();
void t_t313f1_begin_and_off_gates();
void t_t336_launches_start_apart();
void t_t336_repeat_same_launch_still_recognised();
void t_t172_footprints_overlap();
void t_t172_row_visibility();
void t_t172_restore_blanked();
void t_t172_null_stale();
void t_t172_review_decisions();
void t_t172_owner_add_over_stale();
void t_t336_serial_take_skips_zero();
void t_t313f1_page_caps();
void t_t327_effect_wire();
void t_t327_detour_cases();
void t_t327_owner_validation();
void t_t327_accumulate_flush();
void t_t327_stop_rules();
void t_t327_routing();
void t_t329_ko_wire();
void t_t329_ko_decisions();
void t_houses_b2();
void t_p18f1_fold();
void t_p18f2_rebuild_hold();
void t_t492_rebuild_restore();
void t_t805_remake_choose();
void t_p18_owner_change_rule();
void t_pp7_list_hides_only_stamped_on_kenshi_lists();
void t_pp7_continue_choice();
void t_t650_area_receiver_order();
void t_t650_area_receiver_hold();
void t_t650_engine_keeps_at();
void t_t650_area_receiver_keeps();
void t_t650_announce_hold();
void t_t650_retire_withdraw_not_mine();
void t_a1b2_adopt_decide();
void t_a1b2_ring_pick();
void t_a1b2_release_step();
void t_a1b2_giver_settle();
void t_a1b2_release_codecs();
void t_a1b2_scenarios();
void t_a1b2f2_fold2();
void t_a1b2f1_fold1();
void t_a1b2f3_fold3();
void t_a1b1_world_first_route();
void t_a1b1_fold1();
void t_a1b1_roster_hash();
void t_a1b1_roster_check_due();
void t_a1b1_roster_answer_for();
void t_a1b1_roster_apply();
void t_a1b1_dual_run_and_leader();
void t_a1b1_receipts();
void t_a1b1_orphan_copy();
void t_a1b1_codecs();
void t_a1b1_scenario_t810_lost_unloads();
void t_a1b1_scenario_welcome_pending();
void t_a1b1_scenario_restarted_owner_gen0();
void t_a1b1_scenario_wipe_and_orphans();
void t_a1b1_scenario_conflict_bounded();
void t_t417_log_copy_names();
void t_t879_log_budget();
void t_t879_log_part_names();
void t_t879_log_ceiling();
void t_t879_drift_disagrees();
void t_t910_first_lines();
void t_t910_line_keep();
void t_t879_budget_cut_first();
void t_t461_approved_words();
void t_t461_description_rule();
void t_t461_ip_scrub();
void t_t461_id_scrub();
void t_t461_deflate_round_trip();
void t_t461_crc_join();
void t_t461_segments();
void t_t461_pack_log_and_cap();
void t_t461_budget_order();
void t_t835_budget_floor_zero_same();
void t_t835_budget_field_case();
void t_t835_budget_floor_shares();
void t_t838_budget_server_log();
void t_t835_budget_left_out();
void t_t461_zip_and_upload();
void t_t461_send_outcome();
void t_t461_crash_file_rule();
void t_t651_crash_launch();
void t_t651_earlier_log_order();
void t_t461_nearby_messages();
void t_t461_answer_manners();
void t_t53_upnp_text_and_start();
void t_t53_routerport_cfg_key();
void t_t53_upnp_find_and_existing();
void t_t510_internet_address();
void t_t346_place_input_height();
void t_t346_record_file_names();
void t_t53_upnp_worker_steps();
void t_t597_home_adapter_pick();
void t_t631_game_vpn_find();
void t_t597_ssdp_answers();
void t_t597_device_description();
void t_t597_soap();
void t_t597_pcp_natpmp();
void t_t597_routes_and_lease();
void t_t597_fold_safety();
void t_t490_world_id_text();
void t_t490_welcome_id_round_trip();
void t_t490_identity_decide_sweep();
void t_t490_key_field8();
void t_t490_scan_two_same_name_worlds();
void t_t490_store_names_by_id();
void t_t714_permanent_delete();
void t_t490_welcome_remade_refused();
void t_t490_joined_dir_choice();
void t_t429_format_decide();
void t_t490_t429_words();
void t_t490_fold_decisions();
void t_t490_queue_merge_older_first();
void t_t490_lobby_folder_format();
void t_t414_world_leftovers();
void t_nametag_level_of_relation();
void t_nametag_worse_of_two();
void t_nametag_colours();
void t_nametag_outline();
void t_playerstab_tab_place();
void t_playerstab_rows();
void t_playerstab_bottom_and_press();
void t_playerstab_words();
void t_playerstab_their_notices();
void t_playerstab_notice_book();
void t_playerstab_notice_putback();
void t_playerstab_ally_flag();
void t_slotwire_carried_faction();
void t_playerstab_lever_parse();
void t_t546h_bottom_line();
void t_t546h_boxes();
void t_t546h_message_lines();
void t_t546l_invite_refused_lines();
void t_t546l_invitation_ended();
void t_t546l_not_invitable_note();
void t_t546l_away_disband_box();
void t_t546l_research_lines();
void t_t546l_level_book();
void t_t546l_level_game();
void t_t546h_fold_rules();
void t_t546s_research_sound();
void t_t546h_rows_and_lever();
void t_nametag_caption();
void t_uidslots_fill_refuses_then_forget_frees_a_slot();
void t_uidslots_remove_keeps_colliding_run_findable();
void t_uidslots_churn_never_fills_and_never_reads_a_forgotten_value();
void t_uidslots_ownership_change_never_reuses_a_stale_value();
void t_titleart_cover_crop();
void t_titleart_rows_and_note();
void t_titleart_menu_column();
void t_cursor_boxarg();
void t_pausemenu_kenshi_layout_outside_multiplayer();
void t_pausemenu_closed_up_in_multiplayer();
void t_pausemenu_order_and_reopen_never_move_twice();
void t_pausemenu_refuses_rows_out_of_order();
void t_pausemenu_hosting_row();
void t_addrlookup_sites_and_answers();
void t_netaddr_presses_and_answers();
void t_t556_fallen_round_trip();
void t_t556_fallen_list_keeps_forty();
void t_t556_fallen_squad_choice();
void t_t556_fallen_road();
void t_t556_fallen_size_edges();
void t_t556_fallen_v2_row();
void t_t556_place_rules();
void t_t556_fallen_wire();
void t_t556_fallen_book_file();
void t_t556_fallen_table_apply();
void t_t556_fallen_reconcile();
void t_t556_unique_bring_back();
void t_t556_fallen_protocol_74();
void t_t556_fee_price();
void t_t556_fee_options();
void t_t556_back_rows();
void t_t556_count_alive();
void t_t556_table_worst_fits();
void t_t556_tab_rows();
void t_t556_tab_price_lines();
void t_t556_tab_box_and_lines();
void t_t556_tab_states();
void t_t556_tab_fold1();
void t_t556_count_alive_names();
void t_t556_died_marks();
void t_t546_team_wire_round_trip();
void t_t546_team_three_members();
void t_t546_team_refusals();
void t_t546_team_offline_removal();
void t_t546_team_expiry_and_disband();
void t_t546_team_accept_refusals();
void t_t546_team_size_limits();
void t_t546_team_owed_limit();
void t_t546_team_founder_ends();
void t_t546_team_load_edges();
void t_t546_team_profile_deleted();
void t_t546h_profile_deleted_last();
void t_t546h_decline_wait();
void t_t546_team_inbox();
void t_t546c_prejoin_snapshot();
void t_t546c_record_seed_delta();
void t_t546c_record_stance_side();
void t_t546c_departure_both_sides();
void t_t546i_departure_stance();
void t_t546c_record_wire_and_file();
void t_t546c_game_rules();
void t_t546c_fold1();
void t_t546i_fold1();
void t_t546b_teammates_and_index();
void t_t546b_pin_start_end();
void t_t546b_tag_look();
void t_t546b_member_access();
void t_t546b_notice_names();
void t_t546b_box_stub();
void t_t546b_players_tab_teammate();
void t_t546d_union_decide();
void t_t546d_wire_and_file();
void t_t546d_game_rules();
void t_t546d_fold1();

/* suite_part6.cpp */
void t_t546c_fold3();
void t_t546c_npc_entry_roads();
void t_t563_written_pair();
void t_t563_forward_roads();
void t_t573_simulation_held();
void t_t573_hold_shift();
void t_t573_shift_clock();
void t_t573_window_counts_running_frames();
void t_t573_hold_shift_sec();
void t_t573_shift_since();
void t_t573_rearm_schedule();
void t_t573_rearm_bound();
void t_t986_giveup_lift_rule();
void t_t986_giveup_lift_moving_owner();
void t_t986g_floor_judge_due();
void t_t986g2_floor_place_copy();
void t_t986s_place_hold();
void t_t986_giveup_lift_standing_bound();
void t_t580_note_role();
void t_t580_holds();
void t_t580_pending_combinations();
void t_t580_rerun_keep();
void t_t580_ask_area();
void t_t580_kept_bound();
void t_t1099_creation_ask();
void t_t1099_park_stays();
void t_t580_town_answer();
void t_t580_log_due();
void t_t587_placeable();
void t_t587_note_role_placer();
void t_t587_town_people();
void t_t587_place_now();
void t_t587_counter_floor();
void t_t593_faction_key();
void t_t593_faction_lookup();
void t_t593_faction_rekey();
void t_t594_zone_sector();
void t_t594_normalise_same();
void t_t594_sources();
void t_t595_repeat_spawn();
void t_t595_far_unloaded();
void t_t595_stale_keep();
void t_t595_fresh_sample();
void t_t596_first_sighting();
void t_t596_key_sector();
void t_t596_sector_serve();
void t_t596_gen_forget();
void t_t596_giveup_rearm();
void t_t596_held_evict();
void t_t596_reload_sequence();
void t_t596_report_decision();
void t_t596_catchup_serve();
void t_t596_word_refind();
void t_t596_serve_norow();
void t_t596_word_line_budget();
void t_t596_held_victim();
void t_t593_position_key();
void t_t586_resend_wire();
void t_t586_owner_verdict();
void t_t586_lost_book();
void t_t586_lost_look();
void t_lost_spot_kept();
void t_t834_lost_again_waits();
void t_t735_lost_backoff();
void t_t735_lost_keep_edge();
void t_t735_lost_book_room();
void t_t586_lost_arrive();
void t_t586_stale_row();
void t_t595_ko_pose_left_to_engine();
void t_t601_held_ko_timer();
void t_t619_town_table_wire();
void t_t619_town_table_choice();
void t_t619_price_source();
void t_t619_road();
void t_t619_kept();
void t_t619_hook_uses_book();
void t_t619_price_legs();
void t_t619_shop_req_legs();
void t_t632_npc_squad_writer();
void t_t587_sleep_pos_publish();
void t_steamlink_split_sizes();
void t_steamlink_split_interleave();
void t_steamlink_split_malformed();
void t_steamlink_log_facts();
void t_steamlink_linger_buffer();
void t_steamlink_send_rate();
void t_steamlink_hide_ids();
void t_steamlink_relaytest_cfg();
void t_steamfriends_connect_roundtrip();
void t_steamfriends_presence_keys();
void t_steamfriends_list_line();
void t_t290s4b_invite();
void t_steamlink_listen_verdict();
void t_steamlink_listen_table();
void t_steamlink_relaytest_many_cfg();
void t_t290s2_steam_address();
void t_t290s2_route();
void t_t290s2_bridge_want();
void t_t290s2_backpressure();
void t_t290s2_bridge_fold();
void t_t290s2_joiner_wait();
void t_t290s2_cfg_keys();
void t_t602_utf8_utf16();
void t_t602_squad_key_unicode();
void t_t602_utf8_boundaries();
void t_t602_long_cyrillic_path();
void t_t636_empty_town();
void t_t612_world_keep();
void t_t612_world_keep_moves();
void t_t612_welcome_person_round_trip();
void t_t612_welcome_person_decide();
void t_t36_reset_world();
void t_t36_reset_split();
void t_t36_reset_carry();
void t_t36_byid();
void t_t36_import_gate_redecide_delete();
void t_t676_wake_load_road();
void t_t605_store_gate();
void t_t653_copy_decay_pin();
void t_uniq_first_sight_alive();
void t_uniq_elsewhere_set();
void t_uniq_dead_set();
void t_uniq_dup_decide();
void t_uniq_member_verdict();
void t_t625_chat_wire();
void t_t625_chat_words();
void t_t625_chat_tab();
void t_t625_chat_search();
void t_t625_chat_ring();
void t_t625_chat_fade();
void t_t625_chat_cfg();
void t_t625_chat_layout();
void t_t815_chat_sounds();
void t_rk1_hook_guard_pins();
void t_t815_chat_sound_pins();
void t_t625_chat_plan();
void t_t625_chat_utf8();
void t_t625_chat_accept();
void t_t625_chat_pins();
void t_t606_gone_take_over();
void t_t606_session_end_rows();
void t_t657_return_check();
void t_t668_unload_route();
void t_t607_cage_ask_step();
void t_t607_prison_in_keeps_first_at();
void t_t724_bed_own_step();
void t_t607_treat_ask_step();
void t_t607_treat_confirmed_by();
void t_t702_treat_part_decide();
void t_t702_treat_added_after_send();
void t_t702_treat_source();
void t_t677_talk_purse();
void t_t677_purse_after_act();
void t_t676_dup_report();
void t_t599_pace_decide();
void t_t599_pace_burst();
void t_t599_pace_held();
void t_t599_pace_subjects_encoders();
void t_t599_pace_sender_scope();
void t_t599_pace_handover_first();
void t_t694_record_pos_order();
void t_t599_pace_order_through_held();
void t_t700_mod_version();
void t_t599_carry_collapse_in_place();
void t_t599_carry_pops_forgotten();
void t_t599_carry_collapse_after_pop();
void t_t599_pump_stop_rule();
void t_t599_pumplink_no_front_insert();
void t_t290s3a_sha_hmac_pbkdf2();
void t_t290s3a_access_decide();
void t_t290s3a_access_parse();
void t_t290s3a_lockout();
void t_t290s3a_steam_id_trust();
void t_t290s3a_refusal_words();
void t_t290s3b_password_screen();
void t_t290s3a_lock_forget();
void t_t290s3a_export_auth();
void t_t424_floor_place_gap();
void t_t424_floor_strikes();
void t_t424_floor_nan();
void t_t730_font_guard();
void t_t36_joiner_carry();
void t_t36b1_position_key();
void t_t779_fold_keys();
void t_t36b1_area_extract();
void t_t36b1_kept_list_round_trip();
void t_t36b2_wire();
void t_t36b2_decisions();
void t_t36gaps_fold4();
void t_t36gaps_fold3();
void t_t36b2_state_file();
void t_t36b2_record_holds();
void t_t36b2b_local_frame();
void t_t36b2b_game_steps();
void t_t36b2_merge();
void t_t36b2_settle_and_items();
void t_t36b2_merge_parts();
void t_t36b2_host_missing();
void t_t36_echo_match();
void t_t36g_packs_doors_extract();
void t_t36g_list_format2();
void t_t36o_outposts();
void t_ownpark_decisions();
void t_bagnest_moves();
void t_bagnest_wire();
void t_t36o_outposts_fold();
void t_t36o_outposts_keys();
void t_t36r_outposts_near();
void t_t36r_outposts_fold();
void t_t36g_state_broken();
void t_t36g3_own_state();
void t_t36_merge_logs();
void t_t36g_prod_bar_none();
void t_bagnest_push_partner();

/* suite_part7.cpp */
void t_key_height_into_offset();
void t_t609u_rekey_act();
void t_t609u_rekey_queue();
void t_t936_wake_queue();
void t_t936_wake_verdict();
void t_t799_money_known();
void t_t799_keeper_repair();
void t_t801_sell_no_room();
void t_t799_money_load_rule();
void t_t799_record_money();
void t_t799_holder_restock();
void t_t799_money_list();
void t_t805_dismantle_gate();
void t_t798_has_reasons();
void t_t798_keyless_live();
void t_t798_note_holds();
void t_t798_repair();
void t_t798_town_gate();
void t_t798_group_fault();
void t_t798_owed_files();
void t_t1090_reopen_decide();
void t_t804_portrait_hold();
void t809_lever_window_pick();
void t809_editorstate();
void t_t831_window_guard();
void t_t761_twin_sleep();
void t642_deleted_mark_floor();
void t642q_writer_lift_and_new_faction_block();
void t642_other_block_counter_moved();
void t642_renamed_faction_keeps_floor();
void t642_npc_name_clash();
void t850_squad_origin();
void t850_load_age_lift();
void t876_hb_writer();
void t_t871_limbtest_level();
void t_t871_limb_level_wire();
void t_t1045f1_piece_fold();
void t_t1045f3_writer_gate();
void t_t1045f4_writer_events();
void t_t1045f5_refused_inputs();
void t_t1045b_piece_apply();
void t_t1047_piece_live();
void t_t1047b_live_misses();
void t_t1045b2_piece_rules();
void t_t1045b3_piece_seen();
void t_t1045b4_piece_seen_area();
void t_t1045b5_piece_seen_load();
void t_t1045b6_piece_own_make();
void t_t1045f2_piece_answers();
void t_t889_handrec_round_trip();
void t_t889_holds_after_load();
void t_t889_verdict();
void t_t889_freeze();
void t_t889_writer_hold();
void t_t889_actions();
void t_t889_file_cap();
void t_t889f1_holder_state();
void t_t889f1_settle_retry();
void t_t889f1_parks();
void t_t889f2_match();
void t_t889f2_remove_wait();
void t_t889f2_ground_holder();
void t_t889f2_char_route();
void t_t889f2_parked_consumed();
void t_t889f3_char_id();
void t_t889f3_placed();
void t_t889f3_ground_order();
void t_t889f3_search_before_rebuild();
void t_t889f4_late_take_line();
void t_t889f4_holder_look();
void t_t889f4_receiver_wide();
void t_t889f4_own_square_first();
void t_t889f5_durable();
void t_t889f5_closed_road();
void t_t889f5b_taker_closed();
void t710_scan_owners();
void t710_foreign_verdict();
void t710_fence();
void t710_fence_roster();
void t710_squad_kind();
void t_t609_zone_stamp();
void t_t609_zone_holder();
void t_t609_walk_drop();
void t_t609_area_holder_rule();
void t_t609_zone_recv_holder();
void t_t609_held_sender();
void t_t609_zone_kept_adopt();
void t_t609_gone_guard();
void t_t923_build_near_town();
void t_t923_town_pick_takes_mate();
void t_t923_place_town_tail();
void t_t906_pick_walk_and_draw();
void t_t906_pick_find_list_and_pass();
void t_t985_traffic_tally();
void t_t999_keep_decide();
void t_t999_fold1();
void t_t999_fold2();
void t_t966_plan_hold();
void t_t999_fold3();
void t_t1080_purge_emptied_destroy();
void t_t1080_fold1();
void t_t1080_fold3();
void t_t1080_fold4();
void t_t1080_fold5();
void t_t1084_copy_gate();
void t_t999_server_and_retire();
void t_t999_birth_wire();
void t_t772_shop_record();
void t_t772_shop_gate();
void t_t1098_shop_park();
void t_t772_shop_after_run();
void t_t772_shop_money();
void t_t772c_holder_cancel();
void t_t772c_goods_undo();
void t_t772c_durable_undo();
void t_t1018_setup_restock();
void t_t1018_keeper_unknown_money();
void t_t986_window_drift_bound();
void t_t986f1_close_kind();
void t_t937_kept_town_choice();
void t_t985c1_mirror_name();
void t_t985c1_table_round_trip();
void t_t985c1_table_refused();
void t_t985c1_row_check();
void t_t985c1f1_load_order();
void t_t985c1f1_stamp_pairing();
void t_t985c1f1_unnamed_drop();
void t_t985c1f1_name_clash();
void t_t985c1f1_reset_kept_mirror();
void t_t985c1f1_server_behind();
void t_t985c1f2_snapshot_whole();
void t_t985c2bf6_feed_complete();
void t_t985c1f2_seq_judge();
void t_t985c1f2_list_unclashed();
void t_t985c1f2_kept_beside();
void t_t985c1f3_refuse_named();
void t_t985_area_file_round_trip();
void t_t985_area_file_refused();
void t_t985_area_file_missing();
void t_t922_template_sid_sender();
void t_t922_template_lookup_order();
void t_t926f2_camera_road_words();
void t_t926f2_spot_order();
void t_t926f2_spot_refusals();
void t_t926f2_view_and_limit();
void t_t922_spawn_sid_roundtrip();
void t_t924f_town_move_decide();
void t_t924f_fold1_refound_offmap_height();
void t_t924f_fold4_pending_link();
void t_t924f_fold2_refound_limits();
void t_t752_squad_held_by_member();
void t_t752_member_rows();
void t_t948b_squad_split_decide();
void t_t948d_caged_duplicate();
void t_t948d_save_split_squad();
void t_t1047f_outpost_id_mint();
void t_t1047f_towns_file_ids();
void t_t1047f_link_apply_order();
void t_t1047f_hidden_rule();
void t_t1047f1_id_never_reused();
void t_t1047f1_link_owner_checked();
void t_t1047f1_own_piece_team_only();
void t_t1047f1_discovery_per_save();
void t_t1047f1_relink_power_event();
void t_t1047f1_link_mark_again();
void t_t1047f2_own_waits_for_team();
void t_t1047f2_own_check_events_only();
void t_t1047f2_failed_copy_once();
void t_t1047f2_failed_load_disarmed();
void t_t1047f3_town_event();
void t_t1047f3_gate_and_tick();
void t_t1047f3_load_and_save_wiring();
void t_t940_anchor_fixed();
void t_t940_zone_verdict();
void t_t940_cursor_stub();
void t_t940f1_team_wait_and_unread();
void t_t940_cursor_act();
void t_t940_npc_behind();
void t_t1101_fight_name();
void t_t1101_fight_cap();
void t_t1101_swing_credit();
void t_t1101_swing_credit_hold();
void t1097_same_id_copy();
void t1097f_never_both_put_away();
void t1097f_bartender_and_late();
void t1097f_pair_partner_gone();
void t1097g_reported_case_record_yields_to_running();
void t1097g_same_kind_one_or_neither();
void t1097g_running_never_yields();
void t1097g_pair_guard_holds_asleep();
void t_townver_revision_rule();
void t_townver_key_and_gates();
void t_townver_ignore_standings();
void t_t1118_link_keep_window();
void t_t942_thing_classes();
void t_t942_thing_table();
void t_t942_thing_sites();
void t_t942_item_notice();
void t_t943_npc_sites();
void t_t943_npc_stub();
void t_t943_npc_list();

}   /* namespace suite */

#endif
