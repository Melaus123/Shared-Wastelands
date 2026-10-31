# Shared Wastelands roadmap

This is the work slated after the dedicated server. Each item says what it is, why it's wanted, how far along it is, and where it touches the
code, so a contributor (or an AI assistant reading this repo) can pick one up. **Status words:**
- **slated:** agreed, not designed;
- **designed:** the shape is decided, no code;
- **in progress:** code exists, not finished;
- **done:** merged and checked in game.

General rules for every item:
- **Co-op first.** Every game in a world must reach the same answer. Prefer pure rules each game evaluates locally on the same data, or a
  world-server message; never a decision only one game can see.
- **Address rows, not hard-coded addresses.** A new engine function gets a row in `src/coop-plugin/addresses/<fingerprint>.txt` for every
  supported build. `tools/check_tables.py` requires the same names in every table. A row that isn't proven on a build is optional
  (`kAddrUseOptional`): a miss turns the feature off and leaves the mod running.
- **Pure logic in `src/common/*.h`, tested in `src/coop-test`.** Tests are C++03 (VS2010), and new tests go in `test_main.cpp` with a
  `kTests` row.

---

## 1. Kenshi API: per-race rules
- **What:** `kenshi_api.cfg` beside the DLL lists rules per race, by string id or name.
- **Why:** game data can exclude items from a race only one record at a time, and can't express "this race never holds a weapon" or "XP in
  this skill is +40%". A robot race mod needs exactly that.
- **Status:**
  - rules engine and parser: done (`src/common/kapirules.h`, tests in the suite);
  - hooks (`src/coop-plugin/kapi.cpp`): `block_slot`, `no_weapon_in_hand` and `skill_xp` are written and installed when a race uses
    them; checking them in game is in progress. `stat_cap` and `anim_speed` are parsed and logged, not enforced yet.
- **Rule types:**

| Rule | What it does | Where it hooks |
|---|---|---|
| `block_slot <slot>[|<slot>]` | blocks equipment slots: weapon, armour, pants, shirt, boots, helmet, backpack, belt | **done in code:** `InventorySection::isLimitedSlotCompatible`, the check every add path calls (drag, loot, buy, auto-equip, load), found through `InventorySection_fitsAt`'s first call; a refused item goes to the bag |
| `no_weapon_in_hand` | no weapon in hand | **done in code, for now** by keeping weapons out of the hip and back sections through the same check |
| `skill_xp <skill> <percent>` | scales XP in a skill | **done in code:** the skill XP sink and the task XP adder (jobs, first aid), rows `Kapi_SkillXpGain` / `Kapi_SkillXpTask`. The engine drops any single gain over 20, so a boost stops at 20 |
| `stat_cap <skill> <max>` | caps a skill | slated: a cap on the stat read, `CharStatsGetStat(CharStats*, stat 0..38, bool raw)`. This caps what the game reads; the stored level still rises. (The XP sink can't take a second hook.) |
| `anim_speed <multiplier>` | changes animation speed | slated: `AnimationClass::update(dt)`, scaling the frame time; watch for foot sliding |

- **Follow-up: a draw-to-hand hook**, a second, belt-and-braces check that no weapon gets drawn. Its candidate function takes a
  `std::string` **by value**, so a detour must match the engine's exact parameter layout and must never build or free that string itself.
- **Fallback found:** the engine's own race item check (class `RaceLimiter`, the first virtual method: `(item GameData*, owner)`, which
  reads the race's "races exclude" list) is behind the "Can't wear that item." message. If the slot check misses a path, hook that.
- **Co-op:** the rules are local data. Today every game must carry the same `kenshi_api.cfg`; the Sync API (2) removes that requirement.

## 2. Sync API: make API-based mods co-op compatible
- **What:** three slices.
  1. **Server-owned rules:** the world server holds the world's rules file and sends it on join, so every game uses the server's copy and
     not its own.
  2. **Synced named values:** a key/value store scoped per character, squad, faction or world. The owner is authoritative (whoever owns
     the character or squad writes; the world server decides world values), values are persisted with the world, and both rate and size
     are capped.
  3. **Versioned C ABI:** a small exported C interface (a version number plus function table) so a separate plugin mod can read rules, read
     and write synced values, and register hook callbacks without linking against this code.
- **Why:** any mod built on the API is then co-op compatible with no networking of its own.
- **Status:** designed.
- **Touches:** a new world-server message range (protocol bump), `store_main.cpp` (persistence, caps), `src/common/` (wire format and
  limits, with tests), and plugin-side export.

## 3. Server-side Steam ticket verification
- **The gap:**
  - In **listen (hosted) mode**, joins over Steam networking are already Steam-authenticated. `src/coop-plugin/net/steam_transport.cpp` keeps a
    remote's Steam id only when `steamlisten::IdAuthenticated(info.flags)` says Steam authenticated it.
  - A **dedicated server** has no Steam session of its own. It takes the Steam id a game REPORTS in its HELLO (`AdminIdOf` and the HELLO
    admission in `src/coop-store/store_main.cpp`), so admin rights in `dedicated.txt`, `bans.txt` and the `access.txt` allow-list rest on a
    self-reported id.
- **What we know:** Kenshi's own `steam_api64.dll` (interfaces SteamUser019 / SteamGameServer012) exports `GetAuthSessionTicket`,
  `BeginAuthSession` and `LogOnAnonymous`. So the game can already mint a session ticket for the server to check.
- **Preferred route: an anonymous Steam game server inside the world server.**
  - `SteamGameServer_Init` then `LogOnAnonymous`;
  - each joining game sends its ticket (from `GetAuthSessionTicket`);
  - the server calls `BeginAuthSession(ticket, steamId)` and admits only on a valid ValidateAuthTicketResponse, keeping the id as "checked";
  - this needs Valve's free `steamclient` redistributable beside the server.
  - **Unproven:** whether it runs under Wine on Linux, and whether anonymous game-server logon works for Kenshi's app id (233860). That's
    the first thing to test.
- **Fallback route: the Steam Web API.** `ISteamUserAuth/AuthenticateUserTicket` with the server owner's Web API key. Valve's docs are
  unclear on whether an ordinary user key may call it, so check before relying on it.
- **Touches:**
  - a new game-to-world-server message carrying the ticket, and a **protocol bump**;
  - HELLO admission in `store_main.cpp` (hold the join until the ticket answers, with a timeout);
  - the "checked" flag in `src/common/authproof.h` / `accessrule.h`;
  - tests in `src/coop-test`.
- **Status:** slated (researched; not in this PR).
- **Until then:**
  - Steam-id bans on a dedicated server are **advisory**: they stop honest clients, not a spoofer.
  - Admin commands need two factors: the Steam id on the admin list AND `/login` with the server's admin password (challenge-response, the
    password is never sent; set with `--set-admin-password`), with a lockout after wrong tries, an idle end, and an admin security log.
  - Run a server with admins on a LAN or a VPN.

## 4. More admin tools (dedicated-server admins)
- **Already built:** `/kick` `/ban` `/unban` `/heal` `/medic` `/restore` `/revive` `/god` `/bring` `/goto` `/tp` `/where` `/here`
  `/sethome` `/home` `/jail` `/additem` `/save`.
- **Slated:**

| Tool | What |
|---|---|
| `/unstick` | nudge a stuck character to the nearest walkable ground |
| `/give` and `/take` | cats or items between players; move a recruit to another player (today: dismiss, then re-hire) |
| World controls | weather; a server announcement |

- **Touches:** `src/common/admincmd.h` (verbs, parser, tests), `store_main.cpp` (`OnAdminReq`), `src/coop-plugin/admin.cpp` (the owner
  game carries the work out).
- **Every action is checked server-side** against the admin list and written to the server log.

## 5. Rules mods can use (Kenshi API, later)
## 5. More Kenshi API features
- **Combat rules:** damage multipliers by attacker/target race, gender or faction; "will not target" rules; knockback and stagger scaling
  for heavy units.
- **Interaction entries:** custom right-click entries on buildings and characters (a real "Bank access" entry instead of borrowing "use"),
  and dialogue actions that call plugin features.
- **Data pop-ups:** simple data-defined amount and confirm boxes.
- **Status:** slated.

## 6. World ideas
| Idea | What | Status |
|---|---|---|
| **Hungry NPC factions** | the engine runs hunger only for player characters. A rule `faction <id> hunger on|off [rate]` would run the hunger tick for listed factions, with AI that eats, raids for food, or weakens. In co-op, each NPC's owner game runs its hunger, as it already owns that NPC's health. | slated. First step: find the hunger update and the gate that skips non-player characters. |
| **A recruitable character per player** | hiring a unique recruitable gives the recruiter their own copy; the original stays, still recruitable, so every player can recruit "their" copy. Today it's first come, first served, with a recruit locked to its recruiter. | slated |
| **Towns open to public building** | a player town flagged public lets players of other factions build inside its claim zone. It uses the same placement check that lets teammates build today, gated on the town's public flag, which is already synced. | slated |