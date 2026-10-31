# Running a Shared Wastelands dedicated server

So you want a world that stays up when nobody's hosting. That's what this is. The server is one exe, `SharedWastelandsServer.exe`, and it's headless: no Kenshi running on the box, no window, no GPU needed. It just holds the world, and players come and go whenever.

The first player who joins does the host chores (weather, saves, sleeping squads, options). We call them the steward. When they leave, the next player in picks it up. Nobody has to babysit anything.

## What you need

- A box that stays on. Old laptop, a Pi-class thing won't cut it (x86, it's a Windows exe), any cheap PC is fine. RAM and CPU barely matter, it's mostly a filing cabinet.
- UDP port 7777 open on that box (or whatever port you pick).
- Every player on the same Shared Wastelands build and the same mod list. The server checks the mod list when you join and tells you if yours is off.

## Windows (the easy way)

1. Grab `SharedWastelandsServer.exe` (from the release, or build it, see below).
2. Make a folder for it, say `C:\swserver\`, and a `worlds` folder inside it.
3. Run it:

       SharedWastelandsServer.exe --dedicated --port 7777 --world myworld --root C:\swserver\worlds

4. First run makes `worlds\myworld\dedicated.txt` for you. Edit that (see the settings below), it re-reads it within 10 seconds, no restart needed.
5. Players join from the in-game multiplayer panel with your box's IP and the port.

To stop it cleanly, make an empty file called `stop.request` in the world folder. It saves and exits. Don't just kill it if you can help it.

## Linux

Yep, it runs on Linux. It's still the Windows exe, it just runs under Wine, and honestly it's been rock solid like that. No native Linux build (yet), so don't go looking for one.

### Building it on Linux

You only need this if you're building from source. If you've got the exe already, skip to running it.

The short version: the VS2010 x64 compiler runs fine under Wine if you don't let its installer anywhere near it.

1. Get the Windows SDK 7.1 iso (`GRMSDKX_EN_DVD.iso`, about 570 MB, from Microsoft). Don't run the installer under Wine, it falls over. Loop mount the iso and pull the msi's apart with `msiextract` (from msitools).
2. Get Boost 1.60 headers somewhere (headers only, you don't build Boost).
3. Set up:

       export WINEPREFIX=~/sw-wine WINEDEBUG=-all
       export COOP_BOOST='Z:\path\to\boost_1_60_0'

4. Build the server:

       cd src/coop-store && wine cmd /c build.bat

   The plugin is the same deal in `src/coop-plugin`, and that one also runs the whole offline test suite.

Stuff that'll bite you (fix it in your local copy of the .bat files, don't commit it, it's all Wine nonsense):

- Wine's cmd gets `call something || exit /b 1` wrong and bails even when the call worked. Drop the `|| exit /b 1` after the calls.
- At the end of `tools/vc2010-env.bat`, `goto :eof` leaves a stale errorlevel behind. Make it `exit /b 0`.
- Add `/DBOOST_ALL_NO_LIB /DBOOST_ERROR_CODE_HEADER_ONLY` to the cl line, or Ogre's headers try to auto link Boost libs you don't have.
- The server won't link with `server_version.res` under Wine (cvtres throws LNK1123). Link it without the .res. The exe just has no version info, it runs fine.
- Run the python gates (`check_tables`, `scan_hardcoded_rva`) from the repo root with plain `python3` on the Linux side instead of inside the bat. Python under Wine isn't worth the headache.
- The source files are CRLF. If you edit on Linux keep them CRLF or your diffs turn into soup.

Plugin build plus tests is about 5 or 6 minutes, the server about 2.

### Running it on Linux

1. Install Wine (whatever your distro ships is fine, 64-bit).
2. Make a home for it:

       mkdir -p ~/swserver/bin ~/swserver/worlds
       cp SharedWastelandsServer.exe ~/swserver/bin/

3. Give it its own Wine prefix so it's not mixed up with anything else, then run it once by hand to make sure it starts:

       export WINEPREFIX=~/swserver/wineprefix WINEDEBUG=-all
       cd ~/swserver/bin
       wine SharedWastelandsServer.exe --dedicated --port 7777 --world myworld --root 'Z:\home\YOU\swserver\worlds'

   Note the `--root` is a Windows style path through Wine's `Z:` drive. Swap `YOU` for your username. When you see `listening` in `~/swserver/worlds/myworld/shared-wastelands-server.log`, you're good. Ctrl+C out (or do the `stop.request` thing).

4. Open the port (ufw example):

       sudo ufw allow 7777/udp

### Keep it running (systemd)

You want it to come back after a reboot and not die when you log out. Make a user service, `~/.config/systemd/user/swserver.service`:

    [Unit]
    Description=Shared Wastelands dedicated server
    After=network-online.target

    [Service]
    Environment=WINEPREFIX=%h/swserver/wineprefix
    Environment=WINEDEBUG=-all
    WorkingDirectory=%h/swserver/bin
    ExecStartPre=/bin/rm -f %h/swserver/worlds/myworld/stop.request
    ExecStart=/usr/bin/wine %h/swserver/bin/SharedWastelandsServer.exe --dedicated --port 7777 --world myworld --root Z:\\home\\YOU\\swserver\\worlds
    ExecStop=/bin/sh -c "touch %h/swserver/worlds/myworld/stop.request; for i in $$(seq 1 50); do kill -0 $MAINPID 2>/dev/null || exit 0; sleep 1; done"
    Restart=on-failure
    RestartSec=10

    [Install]
    WantedBy=default.target

Then:

    systemctl --user daemon-reload
    systemctl --user enable --now swserver
    sudo loginctl enable-linger $USER

That last one matters. Without linger your user services die when you log out, and you'll be wondering why the server keeps vanishing.

The `ExecStop` line is the clean shutdown: it drops a `stop.request`, gives the server up to 50 seconds to save and exit, and only then lets systemd kill it.

Check on it with:

    systemctl --user status swserver
    tail -f ~/swserver/worlds/myworld/shared-wastelands-server.log

### Backups

The whole world is just the folder. Copy it and you've got a backup. A nightly copy with a timer is plenty, and keep a couple weeks of them. Copy it while the server's running if you want, but for a backup you really care about (like before updating), stop it first.

### Updating

1. Tell everyone to get out.
2. `systemctl --user stop swserver`
3. Back up the world folder.
4. Swap in the new exe (keep the old one renamed next to it, in case).
5. `systemctl --user start swserver` and check the log says `listening`.
6. Everybody updates their game to the matching build. A new server can refuse old games, and it'll say so.

## The settings (dedicated.txt)

It lives in the world folder and gets re-read within 10 seconds. Lines starting with `#` are comments.

    timemode=vote
    startmode=freeplay
    autosave=10
    admin=76561198000000000

- `timemode` is who controls game speed:
  - `vote`: everyone has to pick the same speed (pause counts), otherwise it's 1x
  - `host`: the steward's speed
  - `locked`: always 1x
  - `admin`: only admins
- `startmode`:
  - `freeplay`: normal starts and character creation
  - `kenshi`: everyone gets a random Drifter class and skips the start screen
- `autosave` is minutes between background saves while someone's in the world (0 turns it off).
- `admin=` is a SteamID64, one per line. `admin-loud=` is the same but you get a star in the players tab. Being on this list is only half of it: admins also need the admin password (see "The admin password" below). With no admin password set, nobody can use admin commands.

### Read this before you open the port: the Steam id is self-reported

A dedicated server has no Steam check. Every game tells the server its own Steam id, and the server believes it. Steam ids are public (they're on everyone's profile page), so:

- **Anyone who can reach the port and knows an admin's Steam id gets past the admin list.** That's why admin commands also need the admin password (below): the id alone gets them nothing but a line in the admin security log.
- **The admin star gives the id away.** `admin-loud=` puts a star next to that player in everyone's players tab, which tells a stranger exactly whose id to borrow. Use plain `admin=` if you don't want that.
- **Bans can be dodged and can be framed.** A banned player can send a different id (or none) and walk back in. And a player who sends somebody else's id gets *that* person banned when an admin bans them by name.

So: **keep a dedicated server on a LAN or a VPN (WireGuard, Tailscale, ZeroTier) until the server can check Steam tickets itself.** Don't forward the port to the whole internet. Set a world password too (below). The UDP 7777 note under "When stuff goes wrong" is for people you've let onto your network, not for strangers.

### Kicking and banning

These are admin commands, typed in chat:

- `/kick <name>` disconnects that player. They can come straight back.
- `/ban <name> [reason]` disconnects them and writes their Steam id to `bans.txt`. Since the id came from their own game, the line is marked `[unverified-id]` and the server tells you so. Have a look at `bans.txt` afterwards.
- `/ban <SteamID64> [reason]` bans an id you type yourself (kicks whoever is on it now).
- `/unban <name or SteamID64>` takes the line back out.

A banned id is refused the moment its game says hello, before it gets a slot or sees any of the world, and anything else it sends while it disconnects is ignored.

`bans.txt` lives in the world folder, one ban per line: `<SteamID64> <username> <reason>`. Lines starting with `#` are comments. You can edit it by hand; delete a line to let that person back in. The server reads it at every join, so no restart.

### The admin password (two-factor for admin commands)

Admin commands need two things, every time:

1. the player's Steam id is on the `admin=` / `admin-loud=` list in dedicated.txt, **and**
2. the player typed `/login <admin password>` in chat since they connected.

There's one admin password for the whole server. Set it on the server's console, with the same `--world` and `--root` it runs with:

    SharedWastelandsServer.exe --world myworld --root C:\swserver\worlds --set-admin-password

Same rules as `--set-password`: it isn't shown while you type, 511 characters or more is refused, an empty line removes it. **No admin password = admin commands are switched off for everyone**, and the server log says how to set one.

In game, an admin types `/login hunter2` (with the real password) in the chat box. **The password never leaves your PC.** The game catches `/login` before anything is sent, on any world, linked or not, so it never goes out as chat. It asks the server for a one-time challenge, works out a proof from the password, sends only that proof, and wipes the password. Someone sniffing the network sees nothing they can reuse, and the server never sees or stores the password itself.

- `/logout` ends it. It also ends when you disconnect, and after 30 minutes without an admin command.
- Five wrong passwords in a row lock that connection out of `/login` for a minute.
- To anyone who isn't logged in, an admin command just says "for server admins only", same as for any other player.
- The password shows in the chat box while you type it, like any chat text. Mind who's watching your screen or stream.
- `admin-auth.txt` in the world folder holds the salted key. Like `auth.txt`, **treat it like the password itself** and keep it to the server's account.

### Admin security log

Every day the server writes `admin-security-YYYYMMDD.log` (UTC dates) in the world folder, next to `auth.txt`. It's for you, the owner, to check later whether anyone tried to pass as an admin or guess the password. Players never see it, and it never holds a password.

One line per event, tab-separated: UTC time, event, `addr=` (IP:port), `steam=` (the id the game claimed), `name=` (the player's name, cleaned up and cut short), `cmd=` (the command's name only, never what came after it), `result=`.

| Event | Means |
|---|---|
| `login-ok` | someone on the admin list gave the right admin password |
| `logout` | a login ended: `result=logout`, `disconnected` or `idle-timeout` |
| `login-failed` | wrong password (`wrong-password`), too many `/login` tries, or a challenge that ran out |
| `lockout` | five wrong passwords in a row: that address can't `/login` for a minute |
| `login-while-locked` | a `/login` during a lockout |
| `login-id-not-on-admin-list` | a `/login` from a Steam id that isn't an admin |
| `login-no-admin-password-set` | a `/login` while the server has no admin password |
| `cmd-without-login` | an admin command from an admin's Steam id that never logged in. **Someone using an admin's id without the password looks exactly like this.** |
| `cmd-id-not-on-admin-list` | an admin command from a player who isn't an admin at all |
| `cmd-no-admin-password-set` | an admin command while no admin password is set |
| `suppressed` | the summary when a connection leaves: how many of its events weren't written |
| `day-file-full` | the day's file hit its 16 MB cap; nothing more is written that day |

To keep a flood from filling the disk, each connection gets 20 lines at once and then one every 5 seconds. When lines get held back, the next one written says `suppressed-before=N`, and a `suppressed` line sums up the rest when that connection leaves. The 90 newest day files are kept. To review: open the file, or search it, e.g. `findstr cmd-without-login admin-security-*.log` (Windows) or `grep -h cmd-without-login admin-security-*.log` (Linux). Lots of `login-failed` or `cmd-without-login` from one address means someone is guessing.

### The world password (auth.txt)

On a dedicated server you set it from the server's console, with the same `--world` and `--root` you start it with:

    SharedWastelandsServer.exe --world myworld --root C:\swserver\worlds --set-password

It asks you to type the password (it isn't shown while you type) and press Enter. An empty line removes the password. 511 characters or more is refused. It's never taken on the command line, so it won't end up in your shell history or a process list. A running server picks the change up at the next join.

Only a salted key goes into `auth.txt`, not the password itself, but **treat `auth.txt` like the password**: anyone who can read it can get in. Keep the world folder readable only by the account the server runs as, and don't copy `auth.txt` around.

## When stuff goes wrong

- **"Mods don't match"**: the world remembers the mod list of whoever joined first. If you change mods, everyone has to change them the same way. To start the list over, move `mods.txt` out of the world folder and have the person with the right setup join first.
- **Someone gets refused with a protocol number**: their game and the server are different builds. Update the one that's behind.
- **Joining a brand new world takes forever**: that's normal the first time. The first player's game builds the world and everyone else downloads it piece by piece. Keep that first game unpaused and running, and it moves way faster.
- **The server's up but nobody can connect**: it's almost always the port. Check UDP 7777 is open on the box and on the router if people are coming from outside.
- **Steam updated the mod and now I can't join**: Steam can quietly swap the mod files back to a different build. Reinstall the matching build and you're back.
