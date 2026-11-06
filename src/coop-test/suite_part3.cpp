/* src/coop-test/suite_part3.cpp - THE OFFLINE SUITE, part 3 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"

namespace suite {

/* T-201 PP5 (owner 167, 168, 17): the profile's one save folder - its name, the key-scan choice, the redirect and the one-time rename. */
coopprof::SaveDirSeen Pp5Dir(const char* name, const char* world, const std::string& prof, unsigned gen, unsigned long long t)
{
    coopprof::SaveDirSeen d; d.name = name;
    if (world != 0) { d.keyed = true; d.world = world; d.profile = prof; d.hasGen = gen != 0; d.gen = gen; d.saveTime = t; }
    return d;
}
void t_pp5_profile_save_folder()
{
    using namespace coopprof;
    const std::string P(32, 'a');
    const std::string P1 = ProfileId(P, 1), P2 = ProfileId(P, 2);
    const std::vector<SaveDirSeen> none;
    /* the name rule: one convention for every profile (owner 167), both parts made folder-safe the same way */
    if (ProfileSaveFolderDecide("World1", "Scott", none, "world1", P1, 1) != "World1 - Multiplayer - Scott") Fail("the first profile's folder carries its profile name");
    if (ProfileSaveFolderDecide("coop", "Profile 2", none, "coop", P2, 2) != "coop - Multiplayer - Profile 2") Fail("every profile uses the same convention");
    if (SaveNamePart("a/b\\c:d*e?f\"g<h>i|j~k") != "a_b_c_d_e_f_g_h_i_j_k") Fail("every character Windows refuses in a name (and ~) becomes _");
    if (SaveNamePart("  Scott.  ") != "Scott" || SaveNamePart("A.B") != "A.B") Fail("spaces and dots go from the ends only");
    if (SaveNamePart("...") != "_" || SaveNamePart("") != "_" || SaveNamePart("   ") != "_") Fail("a part with nothing left is _");
    if (SaveNamePart(std::string("a\tb")) != "a_b" || SaveNamePart("caf\xE9") != "caf_") Fail("control and non-ASCII characters become _");
    if (SaveFolderBaseName("my:world", "a/b") != "my_world - Multiplayer - a_b") Fail("the world and the profile are made safe the same way");
    /* suffixes: only while the name is taken by something that is not this (world, profile) */
    std::vector<SaveDirSeen> t;
    t.push_back(Pp5Dir("coop - Multiplayer - Scott", 0, "", 0, 0));
    if (ProfileSaveFolderDecide("coop", "Scott", t, "coop", P1, 1) != "coop - Multiplayer - Scott 2") Fail("a folder with no key file takes the name: 2");
    t.push_back(Pp5Dir("COOP - MULTIPLAYER - SCOTT 2", "other", P1, 3, 1));
    if (ProfileSaveFolderDecide("coop", "Scott", t, "coop", P1, 1) != "coop - Multiplayer - Scott 3") Fail("another world's folder in another case takes the name: 3");
    t.push_back(Pp5Dir("coop - Multiplayer - Scott 3", "coop", P2, 1, 1));
    if (ProfileSaveFolderDecide("coop", "Scott", t, "coop", P1, 1) != "coop - Multiplayer - Scott 4") Fail("another profile of the same world takes the name: 4");
    std::vector<SaveDirSeen> mine; mine.push_back(Pp5Dir("coop - Multiplayer - Scott", "COOP", P1, 2, 5));
    if (ProfileSaveFolderDecide("coop", "Scott", mine, "coop", P1, 1) != "coop - Multiplayer - Scott") Fail("this world and profile's own folder does not take its name");
    const std::string w48(coopcfg::kCfgWorldFieldMax, 'w'), p24(kRowFactionMax, 'p');
    const std::string longest = ProfileSaveFolderDecide(w48, p24, none, w48, P1, 1);
    if (longest != w48 + kSaveFolderMiddle + p24 || longest.size() != 89) Fail("a 48-character world and a 24-character profile keep their whole names (89 characters)");
    /* the key scan: none / one / two of this profile / another world's folder */
    int owned = -1;
    if (SaveFolderScanChoose(none, "coop", P1, 1, &owned) != -1 || owned != 0) Fail("no folder: none found");
    std::vector<SaveDirSeen> s;
    s.push_back(Pp5Dir("s1", "other", P1, 9, 9));
    s.push_back(Pp5Dir("coop_coop_2", "coop", P2, 4, 1));
    s.push_back(Pp5Dir("plain", 0, "", 0, 0));
    if (SaveFolderScanChoose(s, "coop", P1, 1, &owned) != -1 || owned != 0) Fail("another world's and another profile's folders are never this profile's");
    s.push_back(Pp5Dir("coop_coop_1", "Coop", P1, 3, 1));
    if (SaveFolderScanChoose(s, "coop", P1, 1, &owned) != 3 || owned != 1) Fail("one folder: found by its key file whatever its name (world case ignored)");
    s.push_back(Pp5Dir("my own name", "coop", P1, 5, 0));
    if (SaveFolderScanChoose(s, "coop", P1, 1, &owned) != 3 || owned != 2) Fail("two folders of one profile: the newer quick.save wins over a higher save number (M1)");
    s.push_back(Pp5Dir("copy", "coop", P1, 2, 7));
    if (SaveFolderScanChoose(s, "coop", P1, 1, &owned) != 5 || owned != 3) Fail("the newest quick.save wins, whatever its save number");
    s.push_back(Pp5Dir("copy 2", "coop", P1, 4, 7));
    if (SaveFolderScanChoose(s, "coop", P1, 1, &owned) != 6 || owned != 4) Fail("equal quick.save times: the higher save number wins");
    std::vector<SaveDirSeen> k1; k1.push_back(Pp5Dir("old", "coop", "", 0, 0));
    if (SaveFolderScanChoose(k1, "coop", P1, 1, &owned) != 0 || SaveFolderScanChoose(k1, "coop", P2, 2, &owned) != -1) Fail("a key file naming no profile is profile 1's only");
    /* the redirect */
    if (SaveRedirectDecide(true, true, "coop - Multiplayer - Scott")) Fail("single player: never redirected");
    if (SaveRedirectDecide(false, false, "coop - Multiplayer - Scott") || SaveRedirectDecide(false, true, "")) Fail("linked with no profile (or no folder yet): never");
    if (!SaveRedirectDecide(false, true, "coop - Multiplayer - Scott")) Fail("linked with a picked profile: always");
    /* T-201 PP5 fold (H1): the load rule while a profile's folder is decided */
    const std::string F = "coop - Multiplayer - Scott";
    if (ProfileLoadDecide(false, F, kProfReqLoad, F, true) != kProfLoadAllow) Fail("the profile folder itself loads");
    if (ProfileLoadDecide(false, F, kProfReqLoad, "COOP - multiplayer - scott", true) != kProfLoadAllow) Fail("the profile folder in another case loads");
    if (ProfileLoadDecide(false, F, kProfReqLoad, "C:/saves/" + F + "/", true) != kProfLoadAllow) Fail("the profile folder given as a path loads");
    if (ProfileLoadDecide(false, F, kProfReqNewGame, "start", false) != kProfLoadAllow) Fail("NEW GAME for a profile that has never saved");
    if (ProfileLoadDecide(false, F, kProfReqNewGame, "start", true) != kProfLoadRefuse) Fail("NEW GAME for a profile that has saved is refused");
    if (ProfileLoadDecide(false, F, kProfReqLoad, "s1", true) != kProfLoadRefuse || ProfileLoadDecide(false, F, kProfReqLoad, "s1", false) != kProfLoadRefuse)
        Fail("another save (single player, an older copy) is refused while a folder is decided");
    if (ProfileLoadDecide(false, F, kProfReqLoad, F + " 2", true) != kProfLoadRefuse) Fail("a twin of the profile folder is refused");
    if (ProfileLoadDecide(false, F, kProfReqImport, F, true) != kProfLoadRefuse) Fail("an import is refused while a folder is decided");
    if (ProfileLoadDecide(true, F, kProfReqLoad, "s1", true) != kProfLoadAllow || ProfileLoadDecide(true, "", kProfReqNewGame, "x", true) != kProfLoadAllow)
        Fail("single player: unchanged");
    if (ProfileLoadDecide(false, "", kProfReqLoad, "s1", true) != kProfLoadAllow || ProfileLoadDecide(false, "", kProfReqImport, "s1", true) != kProfLoadAllow)
        Fail("no folder decided: unchanged");
    if (std::string(kLoadAutoText) != "In multiplayer your game loads automatically.") Fail("approved refusal wording");
    if (coopui::NoticeTitle(kLoadAutoText) != "CAN'T LOAD") Fail("the refusal's box is CAN'T LOAD");
    if (SaveNameLastPart("C:\\a\\b\\") != "b" || SaveNameLastPart("b") != "b" || SaveNameLastPart("") != "") Fail("a save name's last part");
    /* the redirect with an open save (H1 second guard) */
    if (SaveRedirectOpenDecide(true, "", F, "autosave") != kRedirectDo) Fail("no save open (a new game): redirected");
    if (SaveRedirectOpenDecide(true, F, F, "quicksave") != kRedirectDo || SaveRedirectOpenDecide(true, "D:/x/" + F, F, "a") != kRedirectDo)
        Fail("the profile folder open: redirected");
    if (SaveRedirectOpenDecide(true, "s1", F, "autosave") != kRedirectOwnName) Fail("another save open: saved under its own name, never onto the profile folder");
    if (SaveRedirectOpenDecide(true, "s1", F, F) != kRedirectRefuse || SaveRedirectOpenDecide(false, "", F, F) != kRedirectRefuse)
        Fail("another (or an unreadable) open save asked into the profile folder: refused");
    if (SaveRedirectOpenDecide(false, "", F, "autosave") != kRedirectOwnName) Fail("open save unreadable: never redirected");
    /* PP6 fold-2 F2: the engine's own CONTINUE / LOAD took the decided save */
    if (!OpenSaveIsDecidedFolder(true, F, F) || !OpenSaveIsDecidedFolder(true, "D:/x/" + F + "/", F)) Fail("F2: the decided folder open: the press is done");
    if (OpenSaveIsDecidedFolder(true, "s1", F) || OpenSaveIsDecidedFolder(true, "", F) || OpenSaveIsDecidedFolder(false, F, F) || OpenSaveIsDecidedFolder(true, F, ""))
        Fail("F2: another, no, or an unreadable open save (or no folder): the press failed");
    /* T-239 (owner 191 A / 192): hand saving / loading / new game blocked only with the title down in a multiplayer world */
    if (!HandSaveBlockedDecide(0, true)) Fail("T-239: title down, multiplayer world: blocked");
    if (HandSaveBlockedDecide(1, true) || HandSaveBlockedDecide(-1, true)) Fail("T-239: title up or unreadable: not blocked (the mod's own loads)");
    if (HandSaveBlockedDecide(0, false) || HandSaveBlockedDecide(1, false)) Fail("T-239: single player: never blocked");
    /* the one-time rename (owner 168) */
    std::vector<SaveDirSeen> r; r.push_back(Pp5Dir("coop_coop_1", "coop", P1, 1, 1));
    if (SaveFolderRenameDecide("coop_coop_1", "coop - Multiplayer - Scott", r) != kRenameDo) Fail("target free: renamed");
    r.push_back(Pp5Dir("coop - multiplayer - scott", "coop", P2, 1, 1));
    if (SaveFolderRenameDecide("coop_coop_1", "coop - Multiplayer - Scott", r) != kRenameKeepTaken) Fail("target taken by another folder: the old name is kept");
    if (SaveFolderRenameDecide("coop - multiplayer - scott", "coop - Multiplayer - Scott", none) != kRenameNone) Fail("already the rule's name (any case): nothing to do");
    if (SaveFolderRenameDecide("coop - Multiplayer - Scott 2", "coop - Multiplayer - Scott", r) != kRenameNone) Fail("a suffixed name the rule made stays");
    if (SaveFolderRenameDecide("coop - Multiplayer - Scott 02", "coop - Multiplayer - Scott", none) != kRenameDo
        || SaveFolderRenameDecide("coop - Multiplayer - Scott 2x", "coop - Multiplayer - Scott", none) != kRenameDo) Fail("a name the rule never makes is renamed");
}

/* T-201 PP6': the one press - the automatic load's decision, the host's profile pick, PROFILES' main button, the load steps. */
void t_pp6_one_press_load()
{
    using namespace coopprof;
    const std::string PP(32, 'a'), PQ(32, 'b');   /* person ids as ConfigPlayerId makes them (RowParse checks PlayerIdOk) */
    const std::string P1 = ProfileId(PP, 1), P2 = ProfileId(PP, 2);
    const std::string F = "coop - Multiplayer - Scott";
    /* AutoLoadDecide: host / joiner x never played / played x folder (with / without quick.save) / no folder / foreign folder */
    for (int role = 0; role < 2; ++role)   /* host and joiner decide alike: the role is not an input */
        for (int never = 0; never < 2; ++never)
            for (int place = 0; place < 4; ++place)   /* 0 folder + quick.save, 1 folder without quick.save, 2 no folder, 3 foreign folder only */
            {
                std::vector<SaveDirSeen> dirs;
                if (place == 0 || place == 1) dirs.push_back(Pp5Dir(F.c_str(), "coop", P1, 3, place == 0 ? 5 : 0));
                if (place == 3) { dirs.push_back(Pp5Dir(F.c_str(), "other", P1, 3, 5)); dirs.push_back(Pp5Dir("coop - Multiplayer - Anna", "coop", P2, 1, 5)); }
                int owned = 0;
                const int at = SaveFolderScanChoose(dirs, "coop", P1, 1, &owned);
                const bool found = at >= 0;
                const bool quick = found && dirs[(size_t)at].saveTime != 0;
                const int v = AutoLoadDecide(found, quick, never != 0);
                const int want = (place == 0) ? kAutoLoadLoad : (never != 0 ? kAutoLoadNewGame : kAutoLoadMissing);
                const std::string c = std::string(role ? "joiner" : "host") + (never ? " never played" : " played") + " place " + (char)('0' + place);
                if (v != want) Fail("pp6: AutoLoadDecide " + c + " -> " + AutoLoadVerdictName(v));
                if (place == 3 && found) Fail("pp6: a foreign folder under the rule's name is never this profile's");
                /* the PP5 gate lets exactly these through (no T243: a load is decided only with quick.save; NEW GAME only without it) */
                if (v == kAutoLoadLoad && (ProfileLoadDecide(false, F, kProfReqLoad, F, quick) != kProfLoadAllow || !quick)) Fail("pp6: the gate refuses a decided load " + c);
                if (v == kAutoLoadNewGame && ProfileLoadDecide(false, F, kProfReqNewGame, "start", quick) != kProfLoadAllow) Fail("pp6: the gate refuses a decided NEW GAME " + c);
            }
    if (std::string(AutoLoadVerdictName(kAutoLoadLoad)) != "load" || std::string(AutoLoadVerdictName(kAutoLoadNewGame)) != "newgame"
        || std::string(AutoLoadVerdictName(kAutoLoadMissing)) != "missing") Fail("pp6: the verdict names the proving run greps");
    /* the host's profile: last used / none -> made from PLAYER NAME / the name taken / CHANGE's choices */
    Row a; a.person = PP; a.num = 1; a.slot = 1; a.lastPlayed = 100; a.name = "Scott"; a.faction = "Scott";
    Row b = a; b.num = 2; b.lastPlayed = 200; b.name = "Builder";
    Row gone = a; gone.num = 3; gone.active = 0; gone.name = "Old";
    Row other = a; other.person = PQ; other.num = 1; other.name = "Theirs";
    const std::string file = RowFormat(a) + "\r\n" + RowFormat(b) + "\r\n" + RowFormat(gone) + "\r\n" + RowFormat(other) + "\r\nnot a row\r\n";
    const std::vector<Row> mine = PersonActiveRows(file, PP);
    if (mine.size() != 2 || mine[0].num != 1 || mine[1].num != 2) Fail("pp6: PersonActiveRows keeps only this person's active rows");
    if (!PersonActiveRows("", PP).empty() || !PersonActiveRows(file, "nobody").empty()) Fail("pp6: no file / no rows of mine");
    HostProfileChoice c = HostProfileChoose(mine, 0, "", "Scott");
    if (c.isNew != 0 || c.num != 2 || c.name != "Builder") Fail("pp6: HOST plays the profile used last");
    c = HostProfileChoose(std::vector<Row>(), 0, "", "Scott");
    if (c.isNew == 0 || c.name != "Scott") Fail("pp6: none of mine -> one made from PLAYER NAME");
    c = HostProfileChoose(std::vector<Row>(), 0, "", "");
    if (c.isNew == 0 || c.name != "Profile") Fail("pp6: no usable PLAYER NAME -> 'Profile'");
    c = HostProfileChoose(PersonActiveRows(RowFormat(gone) + "\n", PP), 0, "", "Old");
    if (c.isNew == 0 || c.name != "Old") Fail("pp6: a DELETED profile's name is free again");
    c = HostProfileChoose(mine, 1, "", "Scott");
    if (c.isNew != 0 || c.num != 1) Fail("pp6: SELECT's choice wins over the last used");
    c = HostProfileChoose(mine, 9, "", "Scott");
    if (c.isNew != 0 || c.num != 2) Fail("pp6: a chosen profile gone from the world -> the last used");
    c = HostProfileChoose(mine, 0, "Scott", "X");
    if (c.isNew != 0 || c.num != 1 || c.name != "Scott") Fail("pp6: a NEW PROFILE name already taken plays that profile");
    c = HostProfileChoose(mine, 0, "scott", "X");   /* T-368: the world's NewDecide name rule - "scott" is the name "Scott" */
    if (c.isNew != 0 || c.num != 1) Fail("T-368: CHANGE's NEW PROFILE name is compared as the world's NewDecide does (case ignored)");
    c = HostProfileChoose(mine, 0, "Trader", "X");
    if (c.isNew == 0 || c.name != "Trader") Fail("pp6: CHANGE's NEW PROFILE name is made at the press");
    if (coopui::HostProfileValue("Scott", 1, 1) != "Scott (new)" || coopui::HostProfileValue("Scott", 0, 1) != "Scott"
        || !coopui::HostProfileValue("Scott", 0, 0).empty()) Fail("pp6: HOST GAME's PROFILE row value");
    /* PROFILES' main button */
    if (std::string(coopui::ProfMainButtonText(1)) != "SELECT" || std::string(coopui::ProfMainButtonText(0)) != "PLAY") Fail("pp6: SELECT from CHANGE, PLAY on join");
    {
        size_t n = 0; const coopui::UiDriveName* t = coopui::UiDriveTable(&n); int sel = 0, chg = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (std::string(t[i].name) == "select" && std::string(t[i].w1) == "CoopProfPlayBtn") sel = 1;
            if (std::string(t[i].name) == "changeprofile" && std::string(t[i].w1) == "CoopHostProfChangeBtn") chg = 1;
        }
        if (!sel || !chg) Fail("pp6: the driver's select / changeprofile verbs");
    }
    /* the press through the load: world answered (PressVerdictOf done) -> admitted -> line -> panel closes -> act */
    {
        coopui::PressFacts p; p.mode = 0; p.armed = 1; p.nbState = coopui::kNbRunning; p.lobbyGen = 2; p.linkGen = 2;
        if (coopui::PressVerdictOf(p, "", 0) != coopui::kPressDone) Fail("pp6: HOST's press is done at the world's lobby list (the load starts there)");
    }
    for (int mode = 1; mode <= 2; ++mode)
    {
        if (coopui::LoadAdmitStep(0, -1, 0, mode) != coopui::kLoadStepWait || coopui::LoadAdmitStep(0, -1, 59999, mode) != coopui::kLoadStepWait)
            Fail("pp6: waiting for the world to admit the profile");
        if (coopui::LoadAdmitStep(0, -1, 60000, mode) != coopui::kLoadStepLate) Fail("pp6: the admit wait is bounded (60 s)");
        if (coopui::LoadAdmitStep(1, kAutoLoadLoad, 0, mode) != coopui::kLoadStepShow || coopui::LoadAdmitStep(1, kAutoLoadNewGame, 70000, mode) != coopui::kLoadStepShow)
            Fail("pp6: admitted -> the line shows (load / NEW GAME)");
        if (coopui::LoadAdmitStep(1, kAutoLoadMissing, 0, mode) != coopui::kLoadStepMissing) Fail("pp6: admitted, save not here -> CAN'T LOAD");
    }
    if (coopui::LoadLineOf(kAutoLoadLoad, "Scott") != "Loading \"Scott\"..." || coopui::LoadLineOf(kAutoLoadNewGame, "Scott") != "Opening NEW GAME for \"Scott\"...")
        Fail("pp6: the approved Loading / Opening NEW GAME lines");
    if (!coopui::LoadCancelAllowed(0) || !coopui::LoadCancelAllowed(1) || !coopui::LoadCancelAllowed(2) || coopui::LoadCancelAllowed(3))
        Fail("T-220: CANCEL works while the line waits for the engine (stage 2) and greys once the post can go (stage 3)");
    if (coopui::NoticeTitle(coopui::kProfDeleteFailText) != "CAN'T DELETE" || std::string(coopui::kProfDeleteFailTitle) != "CAN'T DELETE"
        || std::string(coopui::kProfDeleteFailText) != "Couldn't delete that profile.")
        Fail("T-220 (owner 180): the CAN'T DELETE box's approved words");
    if (coopui::NoticeTitle(coopui::kSaveNotHereText) != "CAN'T LOAD"
        || std::string(coopui::kSaveNotHereText) != "This profile's save couldn't be found on this computer.")
        Fail("pp6: the approved CAN'T LOAD box");
    if (coopui::ProfCreatedAutoText("Scott") != "Profile \"Scott\" created.") Fail("pp6: the approved created line");
    /* T-201 PP6' fold (owner 176, approved 2026-09-29): the one press's boxes A-D, word for word, and their titles */
    if (std::string(coopui::kLoadFailText) != "Couldn't load your game."
        || std::string(coopui::kHostPickFailText) != "Couldn't host: your profile couldn't be picked."
        || std::string(coopui::kLoadLinkLostText) != "The connection to the world was lost while loading.")
        Fail("pp6 fold: the approved A / C / D texts");
    if (coopui::NoticeTitle(coopui::kLoadFailText) != "CAN'T LOAD" || coopui::NoticeTitle(coopui::kLoadLinkLostText) != "CAN'T LOAD"
        || coopui::NoticeTitle(coopui::kHostPickFailText) != "CAN'T HOST" || coopui::NoticeTitle(coopui::kSaveNotHereText) != "CAN'T LOAD")
        Fail("pp6 fold: the A-D box titles");
    /* owner 470: a press prepares; the role is cancelled when the title is back with no world on its way */
    {
        const int phases[4] = { coopui::kPrepArmed, coopui::kPrepWindowWait, coopui::kPrepWindowSeen, coopui::kPrepPosted };
        for (int code = -1; code <= 4; ++code)
            for (int sh = 0; sh <= 1; ++sh)
            {
                coopui::PrepFacts n; n.phase = coopui::kPrepNone; n.requestCode = code; n.windowShown = sh;
                if (coopui::PrepStep(n) != coopui::kPrepKeep) Fail("owner 470: no prepared press - nothing is touched (a test-armed game included)");
            }
        for (int k = 0; k < 4; ++k)
        {
            coopui::PrepFacts f; f.phase = phases[k];
            coopui::PrepFacts g = f; g.roleSingle = 1; g.windowShown = 1;
            if (coopui::PrepStep(g) != coopui::kPrepForget) Fail("owner 470: the role is single already - the phase is forgotten, nothing left twice");
            g = f; g.panelWanted = 1;
            if (coopui::PrepStep(g) != coopui::kPrepKeep) Fail("owner 470: the panel is open - its own CANCEL / X / Escape decide");
            g = f; g.pressBusy = 1;
            if (coopui::PrepStep(g) != coopui::kPrepKeep) Fail("owner 470: the press or its load steps are running");
            for (int code = 2; code <= 4; ++code)
            {
                g = f; g.requestCode = code;
                const bool own = phases[k] == coopui::kPrepPosted
                              || (code == 4 && (phases[k] == coopui::kPrepWindowWait || phases[k] == coopui::kPrepWindowSeen));
                if (coopui::PrepStep(g) != (own ? coopui::kPrepKeep : coopui::kPrepCancelForeign))
                    Fail("owner 470: the press's own request is kept; Kenshi's own CONTINUE / LOAD / IMPORT (a request it did not post) leaves the role first");
            }
            g = f; g.reshowPending = 1;
            if (coopui::PrepStep(g) != coopui::kPrepKeep) Fail("owner 470: a refused NEW GAME's window is coming back - kept");
        }
        coopui::PrepFacts f;
        f.phase = coopui::kPrepWindowWait;
        if (coopui::PrepStep(f) != coopui::kPrepKeep) Fail("owner 470: the opened NEW GAME window not up yet - kept");
        f.windowShown = 1;
        if (coopui::PrepStep(f) != coopui::kPrepSeen) Fail("owner 470: the opened NEW GAME window is up - seen");
        f.requestCode = -1; f.windowShown = 0;
        if (coopui::PrepStep(f) != coopui::kPrepKeep) Fail("owner 470: the window not up yet, the request unreadable - kept");
        f.requestCode = 0; f.waitedMs = coopui::kPrepWindowWaitMs - 1;
        if (coopui::PrepStep(f) != coopui::kPrepKeep) Fail("owner 470: the opened window not up yet, within its wait - kept");
        f.waitedMs = coopui::kPrepWindowWaitMs;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: the opened window never came up - cancelled like a back-out");
        f.windowShown = 1;
        if (coopui::PrepStep(f) != coopui::kPrepSeen) Fail("owner 470: the opened window up late - seen, not cancelled");
        f.windowShown = 0; f.requestCode = -1;
        if (coopui::PrepStep(f) != coopui::kPrepUnread) Fail("owner 470: the window never came up, the request unreadable - kept and said");
        if (std::string(coopui::PrepCancelWhy(coopui::kPrepWindowWait)).empty() || std::string(coopui::PrepForeignWhy(2)) == coopui::PrepForeignWhy(3))
            Fail("owner 470: the window-never-came-up and foreign-request reasons");
        f = coopui::PrepFacts(); f.phase = coopui::kPrepWindowSeen; f.windowShown = 1;
        if (coopui::PrepStep(f) != coopui::kPrepKeep) Fail("owner 470: the NEW GAME window is up - kept");
        f.windowShown = 0;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: the NEW GAME window closed with no world on its way - cancelled");
        f.requestCode = 1;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: a save request is no world start - cancelled");
        f.requestCode = -1;
        if (coopui::PrepStep(f) != coopui::kPrepUnread) Fail("owner 470: the request unreadable with the window closed - kept and said");
        f = coopui::PrepFacts(); f.phase = coopui::kPrepArmed;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: the panel closed with the role armed and no world on its way - cancelled");
        f.windowShown = 1;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: a NEW GAME window this press did not open keeps nothing");
        f = coopui::PrepFacts(); f.phase = coopui::kPrepPosted;
        if (coopui::PrepStep(f) != coopui::kPrepCancel) Fail("owner 470: the posted load was refused (the request cleared) - cancelled");
        const std::string w1 = coopui::PrepCancelWhy(coopui::kPrepWindowSeen), w2 = coopui::PrepCancelWhy(coopui::kPrepPosted), w3 = coopui::PrepCancelWhy(coopui::kPrepArmed);
        if (w1.empty() || w2.empty() || w3.empty() || w1 == w2 || w2 == w3 || w1 == w3) Fail("owner 470: each cancel says its own reason");
        /* the left HOST's world server: done once it has ended; ended outright only past the server's own drain limit; never waited on */
        if (coopui::NbStopStep(1, 0, 0, 0) != coopui::kNbStopDone || coopui::NbStopStep(1, 1, coopui::kNbStopGraceMs + 5, coopui::kNbStopEndWaitMs + 5) != coopui::kNbStopDone)
            Fail("owner 470 fold: a world server that has ended is done, whatever else holds");
        if (coopui::NbStopStep(0, 0, 0, 0) != coopui::kNbStopWait || coopui::NbStopStep(0, 0, coopui::kNbStopGraceMs - 1, 0) != coopui::kNbStopWait)
            Fail("owner 470 fold: within the grace - asked again next tick (a server in start-up included)");
        if (coopui::NbStopStep(0, 0, coopui::kNbStopGraceMs, 0) != coopui::kNbStopEnd) Fail("owner 470 fold: not ended within the grace - ended");
        if (coopui::NbStopStep(0, 1, coopui::kNbStopGraceMs + 9999, coopui::kNbStopEndWaitMs - 1) != coopui::kNbStopWait) Fail("owner 470 fold: ended once - its exit is waited for, not ended twice");
        if (coopui::NbStopStep(0, 1, coopui::kNbStopGraceMs + 9999, coopui::kNbStopEndWaitMs) != coopui::kNbStopGiveUp) Fail("owner 470 fold: the exit never seen after the end - the stop is given up, not held for ever");
        if (coopui::kNbStopGraceMs < 60000u + 1000u) Fail("owner 470 fold: the grace covers the world server's own 60 s drain limit");
    }
    /* item 6: the post - ready goes at once, never fails at once, a pending request is waited on (its state) up to the bound */
    if (coopui::LoadPostStep(1, 0) != coopui::kPostGo || coopui::LoadPostStep(1, 999999) != coopui::kPostGo) Fail("pp6 fold: a load the engine takes now is posted");
    if (coopui::LoadPostStep(-1, 0) != coopui::kPostFail) Fail("pp6 fold: anySavesExist() false fails at once (box A), no retry");
    if (coopui::LoadPostStep(0, 0) != coopui::kPostWait || coopui::LoadPostStep(0, coopui::kLoadPostWaitMs - 1) != coopui::kPostWait)
        Fail("pp6 fold: a pending request (SaveManager+0xA0 != 0) is waited on");
    if (coopui::LoadPostStep(0, coopui::kLoadPostWaitMs) != coopui::kPostFail) Fail("pp6 fold: the wait on a pending request is bounded");
    /* item 4: JOIN's PLAY refused while the world admits it ends the wait at once; HOST's own path, other stages and a yes do not */
    if (coopui::LoadPickRefused(1, 2, 3) != 1) Fail("pp6 fold: JOIN's refused pick ends the admit wait at once");
    if (coopui::LoadPickRefused(1, 2, 0) != 0 || coopui::LoadPickRefused(1, 1, 3) != 0 || coopui::LoadPickRefused(0, 2, 3) != 0 || coopui::LoadPickRefused(2, 2, 3) != 0)
        Fail("pp6 fold: only a refusal during JOIN's admit wait ends it");
    /* item 2 (owner 175): PLAYED = the first FINISHED save, never the admit */
    {
        coopprof::Row r; r.person = PP; r.num = 2; r.slot = 5; r.lastPlayed = 300; r.name = "Fresh"; r.faction = "Fresh";   /* admitted: slot bound, last used set */
        coopprof::Row back;
        if (!coopprof::RowParse(coopprof::RowFormat(r), &back) || back.played != 0 || back.slot != 5) Fail("pp6 fold: an admitted, never-saved profile stays unplayed in profiles.txt");
        r.played = 1;
        if (!coopprof::RowParse(coopprof::RowFormat(r) + "\r", &back) || back.played != 1 || back.faction != "Fresh") Fail("pp6 fold: a played profile reads back played");
        if (!coopprof::RowParse("prof1\t" + PP + "\t1\t4\t0\t9\tactive\tOld\tOld", &back) || back.played != 1) Fail("pp6 fold: a 9-field line with a slot (before the fold) is played");
        if (!coopprof::RowParse("prof1\t" + PP + "\t1\t-1\t0\t0\tactive\tNew\tNew", &back) || back.played != 0) Fail("pp6 fold: a 9-field line with no slot is not played");
        if (coopprof::RowParse("prof1\t" + PP + "\t1\t4\t0\t9\tactive\tOld\tOld\tmaybe", &back)) Fail("pp6 fold: field 10 is played|unplayed only");
        std::vector<coopprof::Row> rows; r.played = 0; rows.push_back(r);
        const std::string key = coopprof::SlotKey(PP, 2);
        int at = -1;
        if (coopprof::SavedDecide(rows, key, 2, &at) != coopprof::kSavedSet || at != 0) Fail("pp6 fold: the profile's own SAVED marks it");
        if (coopprof::SavedDecide(rows, coopprof::SlotKey(PP, 1), 2, &at) != coopprof::kSavedRefusedSender || coopprof::SavedDecide(rows, "", 2, &at) != coopprof::kSavedRefusedSender)
            Fail("pp6 fold: only the connection admitted under that profile may mark it");
        if (coopprof::SavedDecide(rows, coopprof::SlotKey(PP, 7), 7, &at) != coopprof::kSavedRefusedUnknown) Fail("pp6 fold: SAVED for no such profile");
        rows[0].played = 1;
        if (coopprof::SavedDecide(rows, key, 2, &at) != coopprof::kSavedSame) Fail("pp6 fold: a second SAVED changes nothing");
        std::vector<char> w; int kind = 0; unsigned num = 0; std::string nm;
        coopprof::EncodeRequest(&w, coopprof::kReqSaved, 2, std::string());
        if (!coopprof::DecodeRequest(w, &kind, &num, &nm) || kind != coopprof::kReqSaved || num != 2 || !nm.empty()) Fail("pp6 fold: PROFILES kind 4 SAVED round-trips");
        coopprof::Answer an; an.rows.push_back(rows[0]); rows[0].played = 0; an.rows.push_back(rows[0]); an.rows[1].num = 3;
        std::vector<char> aw; coopprof::EncodeAnswer(&aw, an); coopprof::Answer got;
        if (!coopprof::DecodeAnswer(aw, &got) || got.rows.size() != 2 || got.rows[0].played != 1 || got.rows[1].played != 0) Fail("pp6 fold: the answer's rows carry played");
        /* the verdicts the fold covers: quit NEW GAME before saving, CANCEL after the admit, a failed load, a crash before the first save
           all leave the row admitted (slot bound) and unplayed -> NEW GAME, never CAN'T LOAD */
        if (coopprof::AutoLoadDecide(false, false, got.rows[1].played == 0) != coopprof::kAutoLoadNewGame) Fail("pp6 fold: admitted, never saved -> NEW GAME");
        if (coopprof::AutoLoadDecide(true, false, got.rows[1].played == 0) != coopprof::kAutoLoadNewGame) Fail("pp6 fold: a folder with no quick.save, never saved -> NEW GAME");
        if (coopprof::AutoLoadDecide(false, false, got.rows[0].played == 0) != coopprof::kAutoLoadMissing) Fail("pp6 fold: played, no save here -> CAN'T LOAD");
    }
    /* item 9 (owner 178a): the one delete and the notebook's file shape, as the host's CHANGE applies them */
    {
        coopprof::Row x; x.person = PP; x.num = 1; x.slot = 1; x.name = "A"; x.faction = "A"; x.played = 1;
        coopprof::Row y = x; y.num = 2; y.slot = -1; y.name = "B"; y.faction = "B"; y.played = 0;
        coopprof::Row z = x; z.person = PQ; z.name = "Z"; z.faction = "Z";
        const std::string file = coopprof::RowFormat(x) + "\r\n" + coopprof::RowFormat(y) + "\r\n" + coopprof::RowFormat(x) + "\r\njunk\r\n" + coopprof::RowFormat(z) + "\r\n";
        int dropped = 0;
        std::vector<coopprof::Row> rows = coopprof::RowsParseFile(file, &dropped);
        if (rows.size() != 3 || dropped != 2) Fail("pp6 fold: profiles.txt read as the notebook reads it (a repeat and junk dropped)");
        if (coopprof::DeleteApply(&rows, PP, 2, 0) != coopprof::kOk || rows[1].active != 0) Fail("pp6 fold: CHANGE's DELETE marks the row deleted");
        if (coopprof::DeleteApply(&rows, PP, 2, 0) != coopprof::kRefusedUnknown) Fail("pp6 fold: a deleted profile can't be deleted again");
        if (coopprof::DeleteApply(&rows, PP, 1, 1) != coopprof::kRefusedInUse || rows[0].active != 1) Fail("pp6 fold: the notebook's DELETE of the profile in play is refused");
        /* T-220 PP6 fold 2: the world lock, SAVED only for the profile's own finished save, SAVED owed until the world shows it; T-222. */
        if (coopprof::WorldLockDecide(true, 0) != coopprof::kLockTaken || coopprof::WorldLockDecide(false, 32) != coopprof::kLockHeld
            || coopprof::WorldLockDecide(false, 33) != coopprof::kLockHeld || coopprof::WorldLockDecide(false, 5) != coopprof::kLockFailed
            || coopprof::WorldLockDecide(false, 3) != coopprof::kLockFailed || std::string(coopprof::kWorldLockFile) != "server.lock")
            Fail("T-220: the world lock - taken only when opened; a sharing / lock violation is a running helper; any other error is no edit either");
        {   /* owner decision 183: --parent <pid> - the helper closes when the Kenshi that started it is gone */
            unsigned long pid = 7;
            if (coopprof::ParentPidParse("1234", &pid) != 1 || pid != 1234ul || coopprof::ParentPidParse("4294967295", &pid) != 1 || pid != 4294967295ul)
                Fail("decision 183: a whole pid is taken");
            pid = 7;
            if (coopprof::ParentPidParse("0", &pid) != 0 || coopprof::ParentPidParse("", &pid) != 0 || coopprof::ParentPidParse(0, &pid) != 0
                || coopprof::ParentPidParse("12x", &pid) != 0 || coopprof::ParentPidParse("-5", &pid) != 0 || coopprof::ParentPidParse("4294967296", &pid) != 0 || pid != 7ul)
                Fail("decision 183: 0, empty, not a number or too big - ignored, pid untouched");
            if (coopprof::ParentWatchDecide(true, 0) != coopprof::kParentWatch || coopprof::ParentWatchDecide(false, 87) != coopprof::kParentGone
                || coopprof::ParentWatchDecide(false, 5) != coopprof::kParentNotWatched)
                Fail("decision 183: opened -> watched; no such process -> close now; any other error -> not watched, runs as before");
        }
        if (coopprof::FolderBaseName("C:\\s\\save\\Scott\\") != "Scott" || coopprof::FolderBaseName("Scott") != "Scott" || coopprof::FolderBaseName("C:/s/Scott") != "Scott")
            Fail("T-220: the saved folder's own name");
        if (coopprof::SavedTellDecide(2, "Scott", "C:\\save\\scott", true) != 1) Fail("T-220: the profile's own folder with quick.save -> SAVED (case ignored, as Windows)");
        if (coopprof::SavedTellDecide(2, "Scott", "C:\\save\\autosave0", true) != 0) Fail("T-220: a save the redirect skipped (its own name) -> no SAVED");
        if (coopprof::SavedTellDecide(2, "Scott", "C:\\save\\Scott", false) != 0) Fail("T-220: the folder with no quick.save -> no SAVED");
        if (coopprof::SavedTellDecide(0, "Scott", "C:\\save\\Scott", true) != 0 || coopprof::SavedTellDecide(2, "", "C:\\save\\Scott", true) != 0)
            Fail("T-220: no pick, or no decided folder -> no SAVED");
        if (coopprof::SavedTellDecide(2, "Scott", "C:\\save\\Scott2", true) != 0) Fail("T-220: a folder that only starts with the name is another folder");
        if (coopprof::SavedSendDecide(1, -1, 4) != 1 || coopprof::SavedSendDecide(1, 4, 4) != 0 || coopprof::SavedSendDecide(1, 4, 5) != 1 || coopprof::SavedSendDecide(0, -1, 4) != 0)
            Fail("T-220: SAVED is told once per link while owed, again on the next link, never when not owed");
        if (coopprof::SavedOwedAtWelcome(true, true, true) != 1 || coopprof::SavedOwedAtWelcome(false, true, true) != 0
            || coopprof::SavedOwedAtWelcome(true, false, true) != 0 || coopprof::SavedOwedAtWelcome(true, true, false) != 0)
            Fail("T-220: an untold save from an earlier session is owed at the WELCOME only for a never-played row whose FOUND folder holds quick.save");
        {
            std::vector<coopprof::Row> list(2); list[0].num = 1; list[0].played = 1; list[1].num = 2; list[1].played = 0;
            if (coopprof::SavedOwedAfterList(1, list, 2) != 1 || coopprof::SavedOwedAfterList(1, list, 1) != 0 || coopprof::SavedOwedAfterList(0, list, 2) != 0
                || coopprof::SavedOwedAfterList(1, std::vector<coopprof::Row>(), 2) != 1)
                Fail("T-220: SAVED stays owed until the world's list shows that very row played");
        }
        {
            const std::string known = "prof1\t" + PP + "\t1\t-1\t0\t0\tactive\tNew\tNew\tplayed";
            if (coopprof::RowLineNewer(known) || coopprof::RowLineNewer("prof1\t" + PP + "\t1\t-1\t0\t0\tactive\tNew\tNew")
                || !coopprof::RowLineNewer(known + "\tsomething") || coopprof::RowLineNewer("junk\ta\tb\tc\td\te\tf\tg\th\ti\tj\tk"))
                Fail("T-222: a prof1 line with more than 10 fields is a newer build's");
            if (coopprof::RowsNewerLines(known + "\n" + known + "\tx\r\n\n") != 1 || coopprof::RowsNewerLines(known + "\n") != 0)
                Fail("T-222: a file holding a newer build's line is counted (read-only to this build)");
        }
        std::vector<coopprof::Row> again = coopprof::RowsParseFile(coopprof::RowsFormatFile(rows), 0);
        if (again.size() != 3 || again[1].active != 0 || again[2].person != PQ || coopprof::PersonActiveRows(coopprof::RowsFormatFile(rows), PP).size() != 1)
            Fail("pp6 fold: the edited file keeps every other row and person");
        if (coopprof::CapFromOption("") != coopprof::kCapDefault || coopprof::CapFromOption("2") != 2) Fail("pp6 fold: CHANGE's limit is the world's profilecap");
    }
    if (coopui::PressStatusText(0, "My World", "") != "Starting \"My World\"...") Fail("pp6: the approved Starting line");
}

/* mp3 (design-mpmenu1 section 4): the Host a game screen's "last played" words (its automatic save folder retired by T-201 PP5). */
void t_mp3_played_words()
{
    const long long now = 1800000000;
    const long long day = 86400;
    struct PlayedCase { long long last; int fresh; const char* want; };
    const PlayedCase cs[] = {
        { now, 1, "not played yet" },
        { 0, 0, "unknown" },                    /* ui2b: the cells no longer repeat the LAST PLAYED head */
        { now - 30, 0, "just now" },
        { now + 500, 0, "just now" },
        { now - 600, 0, "10 minutes ago" },
        { now - 3700, 0, "an hour ago" },
        { now - 5 * 3600, 0, "5 hours ago" },
        { now - 90000, 0, "yesterday" },
        { now - 3 * day, 0, "3 days ago" },
        { now - 21 * day, 0, "3 weeks ago" },
        { now - 90 * day, 0, "3 months ago" },
        { now - 800 * day, 0, "2 years ago" },
    };
    for (size_t i = 0; i < sizeof(cs) / sizeof(cs[0]); ++i)
    {
        const std::string got = coopui::PanelPlayedText(cs[i].last, now, cs[i].fresh);
        if (got != cs[i].want) Fail("case " + I((int)i) + ": wanted '" + cs[i].want + "', said '" + got + "'");
    }
    /* ui2b: both LAST PLAYED heads sort by a map from the words a row shows to a time (ui.cpp UiListLess), which is only
       right if one wording covers ONE unbroken stretch of time - a wording that came back after another would sort wrong. */
    for (int which = 0; which < 2; ++which)
    {
        std::vector<std::string> seen;
        for (long long d = 0; d < 3000 * day; d += (d < 7200 ? 30 : 3600))
        {
            const std::string w = which == 0 ? coopui::PanelPlayedText(now - d, now, 0) : coopui::PanelProfilePlayedText(now - d, now);
            if (!seen.empty() && seen.back() == w) continue;
            for (size_t j = 0; j < seen.size(); ++j)
                if (seen[j] == w) Fail(std::string(which == 0 ? "worlds" : "profiles") + " LAST PLAYED words come back after others: '" + w + "'");
            seen.push_back(w);
        }
    }
}

/* prof3 (design-mpmenu1 section 8): the Your profiles screen's words - last played, one row, the cap line. */
void t_prof3_profiles_screen_words()
{
    const long long now = 1800000000;
    const long long day = 86400;
    struct PlayedCase { long long last; const char* want; };
    const PlayedCase cs[] = {
        { 0, "never" },
        { now - 60, "today" },
        { now + 900, "today" },
        { now - day + 1, "today" },
        { now - day, "yesterday" },
        { now - 3 * day, "3 days ago" },
        { now - 12 * day, "12 days ago" },
        { now - 90 * day, "3 months ago" },
        { now - 800 * day, "2 years ago" },
    };
    for (size_t i = 0; i < sizeof(cs) / sizeof(cs[0]); ++i)
    {
        const std::string got = coopui::PanelProfilePlayedText(cs[i].last, now);
        if (got != cs[i].want) Fail("played case " + I((int)i) + ": wanted '" + cs[i].want + "', said '" + got + "'");
    }
    if (coopui::PanelProfileRowText(1, "Wanderers", "The Wanderers", now - day, now) != "> Wanderers   The Wanderers   yesterday") Fail("the picked row");
    if (coopui::PanelProfileRowText(0, "Hub traders", "Hub Co.", 0, now) != "Hub traders   Hub Co.   never") Fail("a row not picked");
    if (!coopui::PanelProfileCapLine(2, 3).empty() || !coopui::PanelProfileNewAllowed(2, 3)) Fail("below the cap: no line, New profile allowed");
    if (coopui::PanelProfileCapLine(3, 3) != "Profile limit reached (3 of 3)."
        || coopui::PanelProfileNewAllowed(3, 3)) Fail("at the cap: the design's line, New profile greyed");
    if (coopui::PanelProfileCapLine(4, 3).empty() || coopui::PanelProfileNewAllowed(4, 3)) Fail("over a lowered cap");
    if (!coopui::PanelProfileCapLine(0, 0).empty() || !coopui::PanelProfileNewAllowed(0, 0)) Fail("no answer yet (cap 0)");
}

/* mp5 (design-mpmenu1 section 5): the Game options rows - every step of every row is a value the notebook accepts,
   the ends are the design's ranges, and the words and the options.txt reader say what they should. */
bool Mp5NotebookOk(const std::string& k, const std::string& v)
{
    if (k.compare(0, 3, "gp.") == 0) return coopgp::GpValueOk(k, v, 0);
    if (k.compare(0, 3, "gt.") == 0) return coopgp::GtValueOk(k, v);
    if (k == "recruitmult") return coopr::RecruitMultValueOk(v);
    if (k == "basepolicy") return v == "shared" || v == "owner" || v == "locked";
    if (k == "timemode") return coopclock::TimeModeOfWire(v.c_str()) >= 0;
    if (k == "profilecap") return coopprof::CapValueOk(v) != 0;   /* ui5: store_main.cpp OptionValueOk's own check */
    if (swfee::IsOptionKey(k)) return swfee::OptionValueOk(k, v);   /* T-556: the world server's own check */
    if (spimport::IsOptionKey(k)) return spimport::OptionValueOk(k, v);   /* T-1096: the world server's own check */
    return false;
}
void t_mp5_game_options_rows()
{
    int perTab[coopui::kOptTabCount] = { 0, 0, 0 };
    for (int i = 0; i < coopui::kOptCount; ++i)
    {
        const coopui::OptDef& d = coopui::OptDefs()[i];
        if (coopui::OptIndexOf(d.tab, perTab[d.tab]) != i) Fail(std::string("row order: ") + d.key);
        ++perTab[d.tab];
        if (d.dflt[0] != 0 && coopui::OptCanon(i, d.dflt).empty()) Fail(std::string("default is not a legal value: ") + d.key);
        std::string v = d.dflt[0] != 0 ? coopui::OptCanon(i, d.dflt) : coopui::OptStep(i, "", 1);
        int guard = 0;
        const bool fixedRow = coopui::OptIsTick(i) || d.kind == coopui::kOptKindAmount;   /* a tick box or a typing box: no steps */
        while (!fixedRow && coopui::OptCanStep(i, v, -1) && ++guard < 50) v = coopui::OptStep(i, v, -1);
        const std::string low = v;
        int values = 1;
        if (!Mp5NotebookOk(d.key, v)) Fail(std::string("the notebook refuses ") + d.key + "=" + v);
        while (!fixedRow && coopui::OptCanStep(i, v, 1) && ++guard < 100)
        {
            v = coopui::OptStep(i, v, 1);
            ++values;
            if (!Mp5NotebookOk(d.key, v)) Fail(std::string("the notebook refuses ") + d.key + "=" + v);
            if (coopui::OptShown(i, v).empty()) Fail(std::string("no words for ") + d.key + "=" + v);
        }
        const std::string high = v;
        std::string wantLow, wantHigh; int wantN = 0;
        const std::string k = d.key;
        /* the number rows: each steps inside Kenshi's own limits, both ends reached */
        if (k == "gp.gdm" || k == "gp.cod" || k == "gp.nnm") { wantLow = "0.5"; wantHigh = "4"; wantN = 9; }
        else if (k == "gp.ht") { wantLow = "0.25"; wantHigh = "8"; wantN = 12; }
        else if (k == "gp.bs" || k == "gp.rs" || k == "gp.ps") { wantLow = "0.5"; wantHigh = "2"; wantN = 6; }
        else if (k == "gt.pop") { wantLow = "0.5"; wantHigh = "4"; wantN = 10; }
        else if (k == "gt.squad") { wantLow = "0.5"; wantHigh = "3"; wantN = 9; }
        else if (k == "gt.raidsize") { wantLow = "0.25"; wantHigh = "4"; wantN = 11; }
        else if (k == "gt.raidfreq") { wantLow = "0.25"; wantHigh = "3"; wantN = 10; }
        else if (fixedRow) { wantLow = d.dflt; wantHigh = d.dflt; wantN = 1; }
        else if (k == "resurrectgrowth") { wantLow = "steady"; wantHigh = "steep"; wantN = 2; }   /* T-556 */
        else if (k == "gt.attacks") { wantLow = "0"; wantHigh = "5"; wantN = 6; }
        else if (k == "gt.limbloss") { wantLow = "0"; wantHigh = "2"; wantN = 3; }
        else if (k == "recruitmult") { wantLow = "auto"; wantHigh = "4"; wantN = 5; }
        else if (k == "basepolicy") { wantLow = "shared"; wantHigh = "locked"; wantN = 3; }
        else if (k == "timemode") { wantLow = "fixed"; wantHigh = "locked"; wantN = 4; }
        else if (k == "profilecap") { wantLow = "1"; wantHigh = "16"; wantN = 16; }   /* ui5 */
        else if (k == "spimport") { wantLow = "all"; wantHigh = "off"; wantN = 3; }   /* T-1096 */
        else if (k == "spimportmoney" || k == "spimportresearch") { wantLow = "yes"; wantHigh = "no"; wantN = 2; }   /* T-1096 */
        if (low != wantLow || high != wantHigh || values != wantN)
            Fail(k + ": steps " + low + ".." + high + " (" + I(values) + ") - wanted " + wantLow + ".." + wantHigh + " (" + I(wantN) + ")");
        if (d.kind == coopui::kOptKindTick && (coopui::OptStep(i, "0", 1) != "1" || coopui::OptStep(i, "1", 1) != "0")) Fail(k + ": a tick box flips");
    }
    if (perTab[0] != 9 || perTab[1] != 6 || perTab[2] != 10) Fail("rows per tab: " + I(perTab[0]) + "/" + I(perTab[1]) + "/" + I(perTab[2]));
    for (int t = 0; t < coopui::kOptTabCount; ++t) if (perTab[t] > coopui::kOptRowsShown || coopui::OptIndexOf(t, perTab[t]) != -1) Fail("a tab has more rows than the screen");
    const int gdm = coopui::OptIndexOf(coopui::kOptTabDifficulty, 0);
    if (coopui::OptShown(gdm, "1") != "1.0x" || coopui::OptShown(gdm, "0.25") != "0.25x" || coopui::OptShown(gdm, "1.5") != "1.5x"
        || coopui::OptShown(gdm, "4") != "4.0x" || coopui::OptShown(gdm, "") != "Unknown") Fail("the multiplier words: " + coopui::OptShown(gdm, "1"));
    if (coopui::OptStep(gdm, "1.1", 1) != "1.25" || coopui::OptStep(gdm, "1.1", -1) != "1" || coopui::OptStep(gdm, "10", -1) != "4"
        || coopui::OptStep(gdm, "4", 1) != "4") Fail("a value between steps moves to the next step; the ends hold");
    if (coopui::OptCanon(gdm, "0") != "" || coopui::OptCanon(gdm, "1.50") != "1.5") Fail("canonical text");
    const int att = coopui::OptIndexOf(coopui::kOptTabWorld, 4), pol = coopui::OptIndexOf(coopui::kOptTabCoop, 1);
    if (coopui::OptShown(att, "5") != "Never" || coopui::OptShown(att, "0") != "Bombardment" || coopui::OptShown(pol, "owner") != "Owner only" || coopui::OptCanon(pol, "open") != "")
        Fail("the choice words");
    std::map<std::string, std::string> m;
    coopui::OptParseFile("v1\tgp.gdm\t2\r\nbad line\nv2\tgp.cod\t3\nv1\tbasepolicy\towner\textra\nv1\t\tx\n", &m);
    if (m.size() != 2 || m["gp.gdm"] != "2" || m["basepolicy"] != "owner") Fail("options.txt reader: " + I((int)m.size()) + " rows");
    std::string was[coopui::kOptCount], now[coopui::kOptCount];
    was[gdm] = "1"; now[gdm] = "2"; was[pol] = "shared"; now[pol] = "shared"; now[att] = "";
    const std::vector<std::pair<std::string, std::string> > ch = coopui::OptChanged(now, was);
    if (ch.size() != 1 || ch[0].first != "gp.gdm" || ch[0].second != "2") Fail("only the changed rows are handed on: " + I((int)ch.size()));
}

/* GAME OPTIONS' resurrection rows - the MULTIPLAYER tab's rows 5 to 7, the fee box's rule, the fee rows' hover note word
   for word, the greying, which rows have hover notes, the hover notes' hit test, DEFAULTS and the hand-over. */
void t_t556f_fee_rows()
{
    const int on = coopui::OptIndexOfKey("resurrect"), fee = coopui::OptIndexOfKey("resurrectfee"), gr = coopui::OptIndexOfKey("resurrectgrowth");
    if (on < 0 || fee < 0 || gr < 0) { Fail("the three rows exist"); return; }
    if (coopui::OptIndexOf(coopui::kOptTabCoop, 4) != on || coopui::OptIndexOf(coopui::kOptTabCoop, 5) != fee || coopui::OptIndexOf(coopui::kOptTabCoop, 6) != gr
        || coopui::OptRowOf(coopui::kOptTabCoop, fee) != 5 || coopui::OptTabRows(coopui::kOptTabCoop) != 10)   /* T-1096: + the three import rows after them */
        Fail("RESURRECTION, RESURRECTION FEE, FEE GROWTH are the MULTIPLAYER tab's rows 5, 6, 7");
    if (std::string(coopui::OptDefs()[on].label) != "RESURRECTION" || std::string(coopui::OptDefs()[fee].label) != "RESURRECTION FEE"
        || std::string(coopui::OptDefs()[gr].label) != "FEE GROWTH") Fail("the approved labels");
    if (!coopui::OptIsTick(on) || coopui::OptIsTick(fee) || coopui::OptIsTick(gr) || coopui::OptDefs()[fee].kind != coopui::kOptKindAmount)
        Fail("a tick box, a typing box, a < > row");
    /* the tick */
    if (coopui::OptStep(on, "off", 1) != "on" || coopui::OptStep(on, "on", -1) != "off" || !coopui::OptTickOn(on, "on") || coopui::OptTickOn(on, "off")
        || coopui::OptShown(on, "on") != "" || coopui::OptCanon(on, "1") != "" || coopui::OptCanon(on, "on") != "on") Fail("the RESURRECTION tick");
    /* FEE GROWTH: Steady < > Steep */
    if (coopui::OptShown(gr, "steady") != "Steady" || coopui::OptShown(gr, "steep") != "Steep" || coopui::OptStep(gr, "steep", -1) != "steady"
        || coopui::OptStep(gr, "steady", 1) != "steep" || coopui::OptCanStep(gr, "steep", 1) || coopui::OptCanStep(gr, "steady", -1)) Fail("FEE GROWTH steps");
    /* the box: digits only, at most 6; an empty box stands for the value the screen opened with */
    if (coopui::OptFeeBoxFilter("12a3") != "123" || coopui::OptFeeBoxFilter("1,000") != "1000" || coopui::OptFeeBoxFilter("-5") != "5"
        || coopui::OptFeeBoxFilter("9999999") != "999999" || coopui::OptFeeBoxFilter("c.") != "" || coopui::OptFeeBoxFilter(" 25 ") != "25")
        Fail("the box keeps digits only, six at most");
    if (coopui::OptFeeCanon("0050") != "50" || coopui::OptFeeCanon("000") != "0" || coopui::OptFeeCanon("") != "" || coopui::OptFeeCanon("1000000") != ""
        || coopui::OptFeeCanon("12a") != "" || coopui::OptFeeCanon("999999") != "999999") Fail("the amount's text");
    if (coopui::OptFeeBoxValue("", "2500") != "2500" || coopui::OptFeeBoxValue("0", "2500") != "0" || coopui::OptFeeBoxValue("040", "2500") != "40")
        Fail("an empty box goes back to the opened value");
    if (coopui::OptCanon(fee, "1000") != "1000" || coopui::OptCanon(fee, "1000000") != "" || coopui::OptStep(fee, "1000", 1) != "1000" || coopui::OptShown(fee, "750") != "750")
        Fail("the fee row's value");
    if (!swfee::OptionValueOk("resurrectfee", coopui::OptCanon(fee, "999999")) || !swfee::OptionValueOk("resurrectfee", coopui::OptCanon(fee, "0")))
        Fail("every amount the box can hold, the world server accepts");
    /* Kenshi's money style */
    if (coopui::OptMoneyText(0) != "c.0" || coopui::OptMoneyText(999) != "c.999" || coopui::OptMoneyText(1000) != "c.1,000"
        || coopui::OptMoneyText(16000) != "c.16,000" || coopui::OptMoneyText(999999) != "c.999,999" || coopui::OptMoneyText(15999984000000LL) != "c.15,999,984,000,000")
        Fail("money text: " + coopui::OptMoneyText(1000));
    /* RESURRECTION FEE's hover note, word for word (T-1198: the approved page, version 2) */
    if (coopui::OptFeeNoteText("on", "1000", "steep") != "Price: free, then c.1,000, c.4,000, c.9,000 ...\nRises with brought-back characters still alive.")
        Fail("the Steep note: " + coopui::OptFeeNoteText("on", "1000", "steep"));
    if (coopui::OptFeeNoteText("on", "1000", "steady") != "Price: free, then c.1,000, c.2,000, c.3,000 ...\nRises with brought-back characters still alive.")
        Fail("the Steady note: " + coopui::OptFeeNoteText("on", "1000", "steady"));
    if (coopui::OptFeeNoteText("on", "0", "steep") != "Bringing back a fallen character is free." || coopui::OptFeeNoteText("on", "000", "steady") != "Bringing back a fallen character is free.")
        Fail("the amount-0 note");
    if (coopui::OptFeeNoteText("off", "1000", "steep") != "" || coopui::OptFeeNoteText("", "1000", "steep") != "") Fail("no note while RESURRECTION is not ticked");
    if (coopui::OptFeeNoteText("on", "2500", "steep") != "Price: free, then c.2,500, c.10,000, c.22,500 ...\nRises with brought-back characters still alive."
        || coopui::OptFeeNoteText("on", "999999", "steep") != "Price: free, then c.999,999, c.3,999,996, c.8,999,991 ...\nRises with brought-back characters still alive.")
        Fail("the typed amount is filled in: " + coopui::OptFeeNoteText("on", "999999", "steep"));
    /* greying */
    std::string v[coopui::kOptCount];
    for (int i = 0; i < coopui::kOptCount; ++i) v[i] = coopui::OptCanon(i, coopui::OptDefs()[i].dflt);
    if (v[on] != "off" || v[fee] != "1000" || v[gr] != "steep") Fail("DEFAULTS: not ticked, 1000, Steep");
    if (!coopui::OptRowLit(on, v) || coopui::OptRowLit(fee, v) || coopui::OptRowLit(gr, v)) Fail("off: the fee box and FEE GROWTH grey");
    for (int i = 0; i < coopui::kOptCount; ++i) if (i != fee && i != gr && !coopui::OptRowLit(i, v)) Fail(std::string("a row that does not depend on RESURRECTION greyed: ") + coopui::OptDefs()[i].key);
    v[on] = "on";
    if (!coopui::OptRowLit(fee, v) || !coopui::OptRowLit(gr, v)) Fail("on: both lit");
    v[fee] = "0";
    if (!coopui::OptRowLit(fee, v) || coopui::OptRowLit(gr, v)) Fail("amount 0: FEE GROWTH greys, the box stays lit");
    /* the hover notes (T-1198: every row has one; the words themselves are pinned by t1198_option_hover_notes): RESURRECTION
       FEE free at 0, its prices otherwise; FEE GROWTH greyed at 0 and so without one; none on a fee row while RESURRECTION is
       not ticked */
    {
        std::string h[coopui::kOptCount];
        for (int i = 0; i < coopui::kOptCount; ++i) h[i] = v[i];   /* RESURRECTION ticked, amount 0, Steep */
        const int nests = coopui::OptIndexOfKey("gp.nnm");
        if (nests < 0 || coopui::OptDefs()[nests].tab != coopui::kOptTabDifficulty) Fail("ANIMAL NESTS is a Difficulty row");
        for (int i = 0; i < coopui::kOptCount; ++i) if (!coopui::OptHasHoverNote(i)) Fail(std::string("a row without a hover note: ") + coopui::OptDefs()[i].key);
        if (coopui::OptHoverText(fee, h) != "Bringing back a fallen character is free." || coopui::OptHoverText(gr, h) != "")
            Fail("amount 0: the fee box notes that it is free, the greyed FEE GROWTH has no note");
        h[fee] = "1000"; h[gr] = "steady";
        if (coopui::OptHoverText(fee, h) != "Price: free, then c.1,000, c.2,000, c.3,000 ...\nRises with brought-back characters still alive."
            || coopui::OptHoverText(gr, h) != "The price rises evenly with each one currently reanimated.")
            Fail("an amount: the fee box notes the prices, FEE GROWTH its own: " + coopui::OptHoverText(fee, h));
        const std::string nestsNote = coopui::OptHoverText(nests, h);
        h[on] = "off";
        if (coopui::OptHoverText(fee, h) != "" || coopui::OptHoverText(gr, h) != "") Fail("no fee note while RESURRECTION is not ticked");
        if (nestsNote.empty() || coopui::OptHoverText(nests, h) != nestsNote) Fail("the NESTS AND CAMPS note does not depend on RESURRECTION");
        if (coopui::OptHoverText(-1, h) != "" || coopui::OptHoverText(coopui::kOptCount, h) != "" || coopui::OptHasHoverNote(-1) || coopui::OptHasHoverNote(coopui::kOptCount))
            Fail("no note off the list");
    }
    /* the hover notes' hit test: the left and top edges in, the right and bottom edges out, an empty rectangle holds nothing */
    if (!coopui::PanelPointIn(10, 20, 10, 20, 5, 4) || !coopui::PanelPointIn(14, 23, 10, 20, 5, 4) || coopui::PanelPointIn(15, 22, 10, 20, 5, 4)
        || coopui::PanelPointIn(12, 24, 10, 20, 5, 4) || coopui::PanelPointIn(9, 22, 10, 20, 5, 4) || coopui::PanelPointIn(12, 19, 10, 20, 5, 4)
        || coopui::PanelPointIn(10, 20, 10, 20, 0, 4) || coopui::PanelPointIn(10, 20, 10, 20, 5, 0))
        Fail("the pointer-in-rectangle test");
    /* the hand-over: only the changed rows, in the world server's words */
    std::string was[coopui::kOptCount], now[coopui::kOptCount];
    for (int i = 0; i < coopui::kOptCount; ++i) { was[i] = coopui::OptCanon(i, coopui::OptDefs()[i].dflt); now[i] = was[i]; }
    now[on] = "on"; now[fee] = "2500"; now[gr] = "steady";
    const std::vector<std::pair<std::string, std::string> > ch = coopui::OptChanged(now, was);
    if (ch.size() != 3 || ch[0].first != "resurrect" || ch[0].second != "on" || ch[1].first != "resurrectfee" || ch[1].second != "2500"
        || ch[2].first != "resurrectgrowth" || ch[2].second != "steady") Fail("the three rows are handed on as the world server's words: " + I((int)ch.size()));
    for (size_t k = 0; k < ch.size(); ++k) if (!swfee::OptionValueOk(ch[k].first, ch[k].second)) Fail("the world server refuses " + ch[k].first);
    /* the test driver reaches the tick box and the fee box */
    const coopui::UiDriveName* t = coopui::UiDriveFind("opttick", coopui::kDriveButton);
    const coopui::UiDriveName* te = coopui::UiDriveFind("opttick" + std::string(1, (char)('a' + coopui::OptRowOf(coopui::kOptTabCoop, on))), coopui::kDriveButton);
    const coopui::UiDriveName* fb = coopui::UiDriveFind("fee", coopui::kDriveEdit);
    if (t != 0 || te == 0 || std::string(te->w1) != "CoopOptTick4" || fb == 0 || std::string(fb->w1) != "SWOptFeeEdit") Fail("the driver's opttick / fee names");
}

/* mp4 (design-mpmenu1 section 6): the Hosting screen's address pick, its words and the players list. */
void t_mp4_hosting_address_pick_and_players()
{
    std::vector<coopui::PanelNic> nics;
    int picked = 7;
    if (coopui::PanelPickHomeAddr(nics, &picked) != "" || picked != -1) Fail("no adapters: no address, picked -1");
    coopui::PanelNic lo;     lo.ifType = 24; lo.up = 1; lo.hasGateway = 1; lo.ipv4.push_back(0x7F000001ul);
    coopui::PanelNic tunnel; tunnel.ifType = 131; tunnel.up = 1; tunnel.hasGateway = 1; tunnel.ipv4.push_back(0x0A000001ul);
    coopui::PanelNic virt;   virt.ifType = coopui::kNicEthernet; virt.up = 1; virt.ipv4.push_back(0xAC1F7001ul);   /* 172.31.112.1, no router */
    coopui::PanelNic down;   down.ifType = coopui::kNicEthernet; down.up = 0; down.hasGateway = 1; down.ipv4.push_back(0xC0A80105ul);
    coopui::PanelNic apipa;  apipa.ifType = coopui::kNicWifi; apipa.up = 1; apipa.hasGateway = 1; apipa.ipv4.push_back(0xA9FE0102ul);
    coopui::PanelNic wifi;   wifi.ifType = coopui::kNicWifi; wifi.up = 1; wifi.hasGateway = 1;
    wifi.ipv4.push_back(0xA9FE0909ul); wifi.ipv4.push_back(0xC0A80114ul);   /* 169.254.9.9 skipped, then 192.168.1.20 */
    nics.push_back(lo); nics.push_back(tunnel); nics.push_back(virt); nics.push_back(down); nics.push_back(apipa); nics.push_back(wifi);
    std::string got = coopui::PanelPickHomeAddr(nics, &picked);
    if (got != "192.168.1.20" || picked != 5) Fail("the up Wi-Fi adapter with a router wins over loopback, a tunnel, a virtual switch, a down cable and 169.254 (" + got + ")");
    nics.pop_back();
    got = coopui::PanelPickHomeAddr(nics, &picked);
    if (got != "172.31.112.1" || picked != 2) Fail("with no router anywhere usable, the first up Ethernet / Wi-Fi address (" + got + ")");
    nics.pop_back(); nics.pop_back(); nics.pop_back();
    if (coopui::PanelPickHomeAddr(nics, &picked) != "" || picked != -1) Fail("loopback and a tunnel alone give no address");
    if (coopui::PanelIpv4Text(0x0A000001ul) != "10.0.0.1" || coopui::PanelIpv4Text(0xFFFFFFFFul) != "255.255.255.255") Fail("dotted text");
    if (coopui::PanelIpv4Usable(0xE0000001ul) || coopui::PanelIpv4Usable(0x00000000ul) || !coopui::PanelIpv4Usable(0x0A000001ul)) Fail("usable ranges");

    if (coopui::PanelHostCopyText("192.168.1.20", 7777) != "192.168.1.20:7777" || coopui::PanelHostCopyText("", 7777) != "") Fail("copy text");
    if (coopui::PanelHomeAddrLine("192.168.1.20", 7777).find("192.168.1.20:7777") == std::string::npos) Fail("the address line shows address:port");
    if (coopui::PanelHostingText(1, coopui::kNbRunning) != "Hosting.") Fail("open");
    if (coopui::PanelHostingText(1, coopui::kNbStarting) != "Hosting.") Fail("open while the world starts");
    if (coopui::PanelHostingText(1, coopui::kNbStoppedEarly).find("The world couldn't start") != 0
     || coopui::PanelHostingText(1, coopui::kNbNoExe).find("The world couldn't start") != 0
     || coopui::PanelHostingText(1, coopui::kNbSpawnFailed).find("The world couldn't start") != 0) Fail("helper failed");
    if (coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, 7) != "The world couldn't start (error 7). Port 27016 may be in use.")
        Fail("words1b: the world's start failure");
    if (coopui::PanelHostingText(0, coopui::kNbNotOurs) == "Hosting.") Fail("not a host is not open");

    std::vector<std::string> fr;
    std::string pl = coopui::PanelPlayersText("Scott", fr);
    if (pl.find("Scott (host)") == std::string::npos || pl.find("Waiting for players...") == std::string::npos) Fail("alone: " + pl);
    fr.push_back("Sam");
    pl = coopui::PanelPlayersText("Scott", fr);
    if (pl.find("\n    Sam") == std::string::npos || pl.find("connected") != std::string::npos || pl.find("Waiting") != std::string::npos) Fail("Sam joined: " + pl);
    fr[0] = "";
    pl = coopui::PanelPlayersText("", fr);
    if (pl.find("Unnamed player") == std::string::npos || pl.find("Host\n") == std::string::npos) Fail("no names: " + pl);

    const std::string all = coopcfg::CfgLower(pl + coopui::PanelHomeAddrLine("", 1)
        + coopui::PanelNetAddrLine(coopui::kNetAddrLooking, "", 1, 0, 0) + coopui::PanelNetAddrLine(coopui::kNetAddrFailed, "", 1, 0, 0)
        + coopui::PanelCopyNote(1, 0, "1.2.3.4:1") + coopui::PanelCopyNote(0, 0, "1.2.3.4:1") + coopui::PanelCopyNote(1, 1, "1.2.3.4:1")
        + coopui::PanelHostingText(0, 0) + coopui::PanelHostingText(1, 0) + coopui::PanelHostingText(1, coopui::kNbNoExe));
    const char* banned[] = { "notebook", "store", "peer", "slot", "cfg", "uid", "friend", "co-op", "coop" };   /* words1: + friend, co-op; words1b: + coop */
    for (size_t i = 0; i < sizeof banned / sizeof banned[0]; ++i)
        if (all.find(banned[i]) != std::string::npos) Fail(std::string("the Hosting screen says '") + banned[i] + "'");
}

namespace {
/* The folder, as lower-cased relative paths. Applying refuses a move whose source is missing or whose target exists. */
bool W1wApply(std::set<std::string>* fs, const std::vector<coopworld::Move>& moves)
{
    for (size_t i = 0; i < moves.size(); ++i)
    {
        const std::string from = coopcfg::CfgLower(moves[i].from), to = coopcfg::CfgLower(moves[i].to);
        if (fs->find(from) == fs->end() || fs->find(to) != fs->end()) return false;
        fs->erase(from); fs->insert(to);
    }
    return true;
}
std::vector<coopworld::DirEntry> W1wRootListing(const std::set<std::string>& fs, const std::vector<std::string>& dirs)
{
    std::vector<coopworld::DirEntry> r;
    for (std::set<std::string>::const_iterator it = fs.begin(); it != fs.end(); ++it)
        if (it->find('\\') == std::string::npos) r.push_back(coopworld::DirEntry(*it, false));
    for (size_t i = 0; i < dirs.size(); ++i) r.push_back(coopworld::DirEntry(dirs[i], true));
    return r;
}
}

/* owner decision 249 a: the default world is "New World"; the migration mechanics below are about a world named "coop" */
static const char* const kW1wOldWorld = "coop";
void t_w1w_the_migration_plan_moves_each_file_once_and_reverses()
{
    const char* const files[] = { "slots.txt", "areas.txt", "owner.txt", "pendingpos.txt", "options.txt", "clock.txt",
                                  "uniques.txt", "coop-store.log", "a1.platoon", "a1.meta", "a1.platoon.prev", "a1.meta.prev",
                                  "a1.platoon.refused", "z1.zone", "deleted.Holy_Nation.bits", "queue-coop.txt",
                                  "queue-other.txt", "notes.txt", "a1.platoon.1234.tmp", "worlds.migrated.txt" };
    std::set<std::string> fs;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) fs.insert(coopcfg::CfgLower(files[i]));
    const std::set<std::string> original = fs;
    std::vector<std::string> dirs; dirs.push_back("mirror-4242"); dirs.push_back("northlands");
    std::vector<coopworld::DirEntry> root = W1wRootListing(fs, dirs);
    root.push_back(coopworld::DirEntry("SLOTS.TXT", false));   /* the same name twice, any case, is one file */
    if (!coopworld::NeedsMigration(root, kW1wOldWorld)) Fail("loose world files are the old layout");

    const coopworld::MigrationPlan p = coopworld::PlanMigration(root, kW1wOldWorld);
    if (p.moves.size() != 16) Fail("16 world files move, read " + I((int)p.moves.size()));
    std::set<std::string> targets;
    for (size_t i = 0; i < p.moves.size(); ++i)
    {
        if (!targets.insert(coopcfg::CfgLower(p.moves[i].to)).second) Fail("two moves share the target " + p.moves[i].to);
        if (p.moves[i].to.compare(0, 5, "coop\\") != 0) Fail("every move lands in coop\\ (" + p.moves[i].to + ")");
    }
    if (targets.find("coop\\queue.txt") == targets.end()) Fail("queue-coop.txt becomes coop\\queue.txt");
    /* Unknown files, other worlds' queues, leftovers and dead mirrors stay where they are, and are named. */
    if (p.leftInPlace.size() != 4) Fail("4 names are left in place, read " + I((int)p.leftInPlace.size()));
    const char* const stay[] = { "queue-other.txt", "notes.txt", "a1.platoon.1234.tmp", "mirror-4242\\" };
    for (size_t i = 0; i < 4; ++i)
        if (std::find(p.leftInPlace.begin(), p.leftInPlace.end(), std::string(stay[i])) == p.leftInPlace.end())
            Fail(std::string(stay[i]) + " must be left in place and listed");

    if (!W1wApply(&fs, p.moves)) { Fail("the plan moved a file that was not there, or onto one that was"); return; }
    dirs.push_back("coop");
    const std::vector<coopworld::DirEntry> after = W1wRootListing(fs, dirs);
    if (coopworld::NeedsMigration(after, kW1wOldWorld)) Fail("after the plan, the old layout is gone");
    const coopworld::MigrationPlan again = coopworld::PlanMigration(after, kW1wOldWorld);
    if (!again.moves.empty()) Fail("a second plan must move nothing (read " + I((int)again.moves.size()) + ")");
    if (again.leftInPlace.size() != p.leftInPlace.size()) Fail("a second plan lists the same names left in place");
    if (fs.find("notes.txt") == fs.end() || fs.find("worlds.migrated.txt") == fs.end()) Fail("unknown files and the journal stay put");

    if (!W1wApply(&fs, coopworld::ReverseMoves(p.moves))) { Fail("the reverse plan could not be applied"); return; }
    if (fs != original) Fail("reverse(plan(x)) must restore x exactly");

    /* Another world's migration takes its own queue and leaves coop's. */
    const coopworld::MigrationPlan q = coopworld::PlanMigration(root, "Other");
    bool tookOther = false, leftCoopQueue = false;
    for (size_t i = 0; i < q.moves.size(); ++i) if (q.moves[i].from == "queue-other.txt" && q.moves[i].to == "other\\queue.txt") tookOther = true;
    for (size_t i = 0; i < q.leftInPlace.size(); ++i) if (q.leftInPlace[i] == "queue-coop.txt") leftCoopQueue = true;
    if (!tookOther || !leftCoopQueue) Fail("a migration into 'Other' takes queue-other.txt and leaves queue-coop.txt");
    if (p.worldFellBack || q.worldFellBack) Fail("a valid world is not reported as a fallback");

    /* W1w-b: an invalid cfg world plans into coop and says so - never into '..' or 'coop.'. */
    const char* const invalid[] = { "..", "coop." };
    for (size_t i = 0; i < 2; ++i)
    {
        const coopworld::MigrationPlan r = coopworld::PlanMigration(root, invalid[i]);
        if (!r.worldFellBack) Fail(std::string("world '") + invalid[i] + "' must be reported as a fallback");
        /* owner decision 249 a: the default world is "New World" - an invalid name plans exactly the default world's moves, into its folder */
        const coopworld::MigrationPlan def = coopworld::PlanMigration(root, coopworld::kDefaultWorld);
        const std::string defDir = coopworld::WorldFolderName(coopworld::kDefaultWorld) + "\\";
        if (r.moves.size() != def.moves.size() || def.worldFellBack) Fail(std::string("world '") + invalid[i] + "' must plan exactly the default world's moves");
        for (size_t j = 0; j < r.moves.size(); ++j)
            if (r.moves[j].to.compare(0, defDir.size(), defDir) != 0) Fail(std::string("world '") + invalid[i] + "' moved a file to " + r.moves[j].to);
    }
}

/* W1w-b: a migration interrupted after HALF its moves (a slot table moved, say, but not the .meta files) is still the
   old layout, and a second plan makes exactly the other half - nothing twice, nothing missed. */
void t_w1w_an_interrupted_migration_resumes_from_where_it_stopped()
{
    const char* const files[] = { "slots.txt", "areas.txt", "owner.txt", "options.txt", "clock.txt", "coop-store.log",
                                  "a1.platoon", "a1.meta", "b2.platoon", "b2.meta", "queue-coop.txt", "notes.txt" };
    std::set<std::string> fs;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) fs.insert(files[i]);
    std::vector<std::string> dirs; dirs.push_back("mirror-7");
    const coopworld::MigrationPlan p = coopworld::PlanMigration(W1wRootListing(fs, dirs), kW1wOldWorld);
    if (p.moves.size() != 11) { Fail("11 world files move, read " + I((int)p.moves.size())); return; }

    /* Each half in turn: the first half, and (on a fresh copy) the second half - so the loose slots.txt and the
       loose *.meta are each absent in one of the two runs. */
    for (int half = 0; half < 2; ++half)
    {
        std::set<std::string> part = fs;
        std::vector<coopworld::Move> done, rest;
        for (size_t i = 0; i < p.moves.size(); ++i)
            ((i < p.moves.size() / 2) == (half == 0) ? done : rest).push_back(p.moves[i]);
        if (!W1wApply(&part, done)) { Fail("the first part of the plan could not be applied"); return; }
        std::vector<std::string> d2 = dirs; d2.push_back("coop");
        const std::vector<coopworld::DirEntry> mid = W1wRootListing(part, d2);
        if (!coopworld::NeedsMigration(mid, kW1wOldWorld)) Fail("a half-done migration still needs migrating (half " + I(half) + ")");
        const coopworld::MigrationPlan again = coopworld::PlanMigration(mid, kW1wOldWorld);
        if (again.moves.size() != rest.size()) Fail("the second plan must be exactly the moves not yet made (half " + I(half) + ")");
        for (size_t i = 0; i < again.moves.size(); ++i)
            for (size_t j = 0; j < done.size(); ++j)
                if (coopcfg::CfgLower(again.moves[i].from) == coopcfg::CfgLower(done[j].from)) Fail("a file was planned twice: " + done[j].from);
        if (!W1wApply(&part, again.moves)) { Fail("the rest of the plan could not be applied"); return; }
        const std::vector<coopworld::DirEntry> end = W1wRootListing(part, d2);
        if (coopworld::NeedsMigration(end, kW1wOldWorld)) Fail("after the rest, nothing is left to migrate (half " + I(half) + ")");
        std::set<std::string> whole = fs;
        W1wApply(&whole, p.moves);
        if (part != whole) Fail("half then the rest must end where the whole plan ends (half " + I(half) + ")");
    }
    /* A loose options.txt alone (no slots.txt, no .meta) is still a world file left behind. */
    std::vector<coopworld::DirEntry> lone; lone.push_back(coopworld::DirEntry("options.txt", false)); lone.push_back(coopworld::DirEntry("coop", true));
    if (!coopworld::NeedsMigration(lone, kW1wOldWorld)) Fail("one loose world file is still the old layout");
}

/* W2-f (review-w2 G, H): every loose world file the migration moves by exact name, and --unmigrate's log, is refused
   as a world name (any case) - one list with WorldFileKnown; and a start resumes a migration only while the journal
   names moves into the folder AND the folder has no migrated.done. */
void t_w2f_world_file_names_are_reserved_and_a_finished_migration_is_not_resumed()
{
    const char* const names[] = { "coop-store.log", "slots.txt", "areas.txt", "owner.txt", "pendingpos.txt", "options.txt",
                                  "clock.txt", "uniques.txt", "worlds.unmigrate.log", "Slots.TXT", "COOP-STORE.LOG", "Worlds.Unmigrate.Log" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        std::string why;
        if (coopworld::WorldNameOk(names[i], &why)) Fail(std::string("'") + names[i] + "' must be refused as a world name");
        else if (why.empty()) Fail(std::string("the refusal of '") + names[i] + "' must say why");
    }
    for (size_t i = 0; i < coopworld::kWorldFileExactCount; ++i)
        if (!coopworld::WorldFileKnown(coopworld::kWorldFileExact[i]) || !coopworld::WorldNameIsStoreReserved(coopworld::kWorldFileExact[i]))
            Fail(std::string("the shared list and its two readers disagree on ") + coopworld::kWorldFileExact[i]);
    if (coopworld::WorldFileKnown(coopworld::kUnmigrateLog)) Fail("worlds.unmigrate.log is not a world file - it stays loose");
    if (!coopworld::WorldNameOk("slots", 0) || !coopworld::WorldNameOk("north", 0)) Fail("near-misses stay valid world names");

    const coopworld::JournalRead j = coopworld::MigrationJournalParse("slots.txt\tcoop\\slots.txt\n");
    if (!coopworld::MigrationResuming(j, "coop", false)) Fail("journal moves into coop\\ and no marker = resuming");
    if (coopworld::MigrationResuming(j, "coop", true)) Fail("a marker = finished, not resuming");
    if (coopworld::MigrationResuming(j, "north", false)) Fail("no journal move into north\\ = not resuming");
    if (coopworld::MigrationResuming(coopworld::MigrationJournalParse(""), "coop", false)) Fail("an empty journal = not resuming");
}

/* W2a: THE MIGRATION JOURNAL. What SharedWastelandsServer.exe writes before each move is what --unmigrate reads back: every line
   of a real plan round-trips; a line of any other shape (a path that climbs out, a drive, a second separator, no tab)
   is counted bad and never becomes a move; a last line cut off mid-write is dropped and said. */
void t_w2a_the_migration_journal_round_trips_and_refuses_what_it_never_writes()
{
    const char* const files[] = { "slots.txt", "a1.meta", "a1.platoon", "coop-store.log", "queue-North.txt", "deleted.Holy_Nation.bits" };
    std::vector<coopworld::DirEntry> root;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) root.push_back(coopworld::DirEntry(files[i], false));
    const coopworld::MigrationPlan p = coopworld::PlanMigration(root, "North");
    if (p.moves.size() != 6) { Fail("6 moves planned, read " + I((int)p.moves.size())); return; }
    std::string text;
    for (size_t i = 0; i < p.moves.size(); ++i) text += coopworld::MigrationJournalLine(p.moves[i]);
    coopworld::JournalRead j = coopworld::MigrationJournalParse(text);
    if (j.moves.size() != p.moves.size() || j.badLines != 0 || j.cutOffTail) Fail("a written journal reads back whole");
    for (size_t i = 0; i < j.moves.size() && i < p.moves.size(); ++i)
        if (j.moves[i].from != p.moves[i].from || j.moves[i].to != p.moves[i].to) Fail("journal line " + I((int)i) + " changed on the way back");
    if (!coopworld::JournalHasMovesInto(j, "north") || !coopworld::JournalHasMovesInto(j, "NORTH")) Fail("the journal names moves into north\\ (any case)");
    if (coopworld::JournalHasMovesInto(j, "coop") || coopworld::JournalHasMovesInto(j, "nort")) Fail("the journal names no move into coop\\ or nort\\");

    /* CRLF, blank lines and a cut-off tail */
    j = coopworld::MigrationJournalParse("slots.txt\tcoop\\slots.txt\r\n\n\r\na1.meta\tcoop\\a1.meta\na1.plat");
    if (j.moves.size() != 2 || j.badLines != 0 || !j.cutOffTail) Fail("CRLF and blank lines are read, a tail with no LF is dropped and said");

    const char* const bad[] = { "slots.txt coop\\slots.txt", "slots.txt\tslots.txt", "..\tcoop\\x", "a\\b\tcoop\\b",
                                "a\tcoop\\..", "a\t..\\a", "a\tcoop\\sub\\a", "a\tC:\\a", "C:a\tcoop\\a", "a\tcoop/a",
                                "\tcoop\\a", "a\t\\a", "a\tcoop\\", "a\tcoop\\a\tb" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        const coopworld::JournalRead r = coopworld::MigrationJournalParse(std::string(bad[i]) + "\n");
        if (r.badLines != 1 || !r.moves.empty()) Fail(std::string("a journal line of the wrong shape must be refused: ") + bad[i]);
    }
    if (coopworld::MigrationJournalUndoneName(1700000000LL) != "worlds.migrated.txt.undone.1700000000") Fail("the undone journal's name");
}

/* W2a: ONE REVERSE STEP never overwrites, and the journal replayed backwards restores the old layout even when it holds
   a line whose move Windows refused (the line is written first) and when --unmigrate is run twice. */
void t_w2a_unmigrate_steps_never_overwrite_and_replay_is_resumable()
{
    if (coopworld::UnmigrateStepDecide(true, false) != coopworld::kUnmigrateMove) Fail("moved file there, loose name free: move back");
    if (coopworld::UnmigrateStepDecide(false, true) != coopworld::kUnmigrateAlreadyBack) Fail("moved file gone, loose there: already back");
    if (coopworld::UnmigrateStepDecide(false, false) != coopworld::kUnmigrateMissing) Fail("neither there: missing");
    if (coopworld::UnmigrateStepDecide(true, true) != coopworld::kUnmigrateConflict) Fail("both there: conflict, never overwrite");

    /* The journal of a migration whose third move (slots.txt, onto a coop\slots.txt already there) was refused. */
    std::set<std::string> fs;
    fs.insert("coop\\a1.meta"); fs.insert("coop\\a1.platoon"); fs.insert("slots.txt"); fs.insert("coop\\slots.txt");
    const std::string text = "a1.meta\tcoop\\a1.meta\na1.platoon\tcoop\\a1.platoon\nslots.txt\tcoop\\slots.txt\n";
    const std::vector<coopworld::Move> back = coopworld::ReverseMoves(coopworld::MigrationJournalParse(text).moves);
    for (int pass = 0; pass < 2; ++pass)
    {
        int moved = 0, conflicts = 0;
        for (size_t i = 0; i < back.size(); ++i)
        {
            const coopworld::UnmigrateStep st = coopworld::UnmigrateStepDecide(fs.count(back[i].from) != 0, fs.count(back[i].to) != 0);
            if (st == coopworld::kUnmigrateConflict) { ++conflicts; break; }
            if (st == coopworld::kUnmigrateMove) { fs.erase(back[i].from); fs.insert(back[i].to); ++moved; }
        }
        if (pass == 0 && (conflicts != 1 || moved != 0)) Fail("the refused move's line meets both files and stops before moving anything");
        if (pass == 0) fs.erase("slots.txt");   /* the player removes the stray loose copy and runs --unmigrate again */
        if (pass == 1 && (conflicts != 0 || moved != 3)) Fail("the second --unmigrate moves all three back, read " + I(moved));
    }
    std::set<std::string> want; want.insert("a1.meta"); want.insert("a1.platoon"); want.insert("slots.txt");
    if (fs != want) Fail("after --unmigrate the files are loose again and coop\\ is empty");
}

/* ================= W3 (decisions 58, 59; store protocol 45): THE WELCOME NAMES THE WORLD, AND A GAME FOLLOWS IT ================= */
void t_w3_the_welcome_round_trips_with_and_without_the_world()
{
    coopstore::StoreWelcome a; a.protocol = 45; a.authority = 1; a.recordCount = 7; a.slot = 3; a.hasWorld = true; a.world = "NorthLands";
    std::vector<char> b; coopstore::StoreWelcomeEncode(a, &b);
    if (b.size() != 16 + 4 + 10) Fail("a 45 WELCOME is 16 bytes, the length and the name (" + I((int)b.size()) + ")");
    coopstore::StoreWelcome r;
    if (!coopstore::StoreWelcomeDecode(b, &r) || r.protocol != 45 || r.authority != 1 || r.recordCount != 7 || !r.hasSlot || r.slot != 3
        || !r.hasWorld || r.world != "NorthLands" || r.worldMalformed)
        Fail("a 45 WELCOME reads back every field and the world as sent");
    coopstore::StoreWelcome o = a; o.hasWorld = false; coopstore::StoreWelcomeEncode(o, &b);
    if (b.size() != 16) Fail("a WELCOME without the world is the 44 shape, 16 bytes");
    if (!coopstore::StoreWelcomeDecode(b, &r) || !r.hasSlot || r.slot != 3 || r.hasWorld || !r.world.empty() || r.worldMalformed)
        Fail("a 44 WELCOME reads with no world and is not malformed");
    b.resize(12);
    if (!coopstore::StoreWelcomeDecode(b, &r) || r.hasSlot || r.hasWorld || r.recordCount != 7) Fail("a 12-byte WELCOME reads with no slot and no world");
    b.resize(11);
    if (coopstore::StoreWelcomeDecode(b, &r)) Fail("11 bytes is not a WELCOME");
    a.world = ""; coopstore::StoreWelcomeEncode(a, &b);
    if (b.size() != 20 || !coopstore::StoreWelcomeDecode(b, &r) || !r.hasWorld || !r.world.empty()) Fail("an empty name travels as length 0");
    a.world = "NorthLands"; coopstore::StoreWelcomeEncode(a, &b); b.resize(b.size() - 3);
    if (!coopstore::StoreWelcomeDecode(b, &r) || r.hasWorld || !r.worldMalformed || r.recordCount != 7) Fail("a truncated name is not read and the rest still is");
    coopstore::StoreWelcomeEncode(o, &b); coopstore::StoreWelcomePutU32(&b, 0xFFFFFFFFu);
    if (!coopstore::StoreWelcomeDecode(b, &r) || r.hasWorld || !r.worldMalformed) Fail("a hostile length is refused");
    coopstore::StoreWelcomeEncode(o, &b); b.push_back('x');
    if (!coopstore::StoreWelcomeDecode(b, &r) || r.hasWorld || !r.worldMalformed) Fail("a stray byte after the slot is malformed");
    a.world = std::string(coopstore::kStoreWelcomeWorldMax + 40, 'a'); coopstore::StoreWelcomeEncode(a, &b);
    if (!coopstore::StoreWelcomeDecode(b, &r) || !r.hasWorld || r.world.size() != coopstore::kStoreWelcomeWorldMax) Fail("an over-long name is cut to the limit on the way out");
}
void t_w3_a_game_confirms_adopts_or_refuses_the_notebooks_world()
{
    /* confirm: the same folder name, case ignored; an empty or invalid cfg world is the default */
    if (coopworld::WelcomeWorldDecide("coop", "coop", "") != coopworld::kWelcomeWorldConfirm) Fail("coop and coop confirm");
    if (coopworld::WelcomeWorldDecide("NorthLands", "northlands", "northlands") != coopworld::kWelcomeWorldConfirm) Fail("case is one world");
    /* owner decision 249 a: the default world is "New World" */
    if (coopworld::WelcomeWorldDecide("", "New World", "") != coopworld::kWelcomeWorldConfirm) Fail("no cfg world is New World");
    if (coopworld::WelcomeWorldDecide("CON", "new WORLD", "new world") != coopworld::kWelcomeWorldConfirm) Fail("an invalid cfg world is New World");
    /* adopt: another world and no index read, or the index already the notebook's */
    if (coopworld::WelcomeWorldDecide("coop", "NorthLands", "") != coopworld::kWelcomeWorldAdopt) Fail("a joiner adopts the notebook's world");
    if (coopworld::WelcomeWorldDecide("", "NorthLands", "") != coopworld::kWelcomeWorldAdopt) Fail("a joiner with no cfg world adopts");
    if (coopworld::WelcomeWorldDecide("coop", "NorthLands", "northlands") != coopworld::kWelcomeWorldAdopt) Fail("an index of the notebook's world adopts");
    /* refuse: an index of another world is already read */
    if (coopworld::WelcomeWorldDecide("coop", "NorthLands", "coop") != coopworld::kWelcomeWorldRefuse) Fail("a game that opened coop refuses NorthLands");
    if (coopworld::WelcomeWorldDecide("NorthLands", "coop", "northlands") != coopworld::kWelcomeWorldRefuse) Fail("a game that opened northlands refuses coop");
    /* none: no usable name changes nothing, index or not */
    if (coopworld::WelcomeWorldDecide("coop", "", "") != coopworld::kWelcomeWorldNone) Fail("no name is none");
    if (coopworld::WelcomeWorldDecide("coop", "", "coop") != coopworld::kWelcomeWorldNone) Fail("no name never refuses");
    if (coopworld::WelcomeWorldDecide("coop", "CON", "") != coopworld::kWelcomeWorldNone) Fail("a device name is not a world");
    if (coopworld::WelcomeWorldDecide("coop", "a~1", "coop") != coopworld::kWelcomeWorldNone) Fail("a short-name spelling is not a world");
    /* W3-f (review-w3 item 6, M4): the re-read keeps an adopted world for EVERY co-op role - the host follows too */
    if (coopworld::WorldForRole("coop", "NorthLands") != "NorthLands") Fail("a game keeps its adopted world, whatever its role");
    if (coopworld::WorldForRole("coop", "") != "coop") Fail("a game that adopted nothing takes the file's world");
    if (coopworld::WorldForRole("", "") != "") Fail("no file world and nothing adopted stays empty (the default world)");
}

/* ================= T-318: WHICH RE_Kenshi.json ROWS ARE PLUGIN DLLs OF THIS GAME ================= */
void t_t318_plugin_rows()
{
    /* the T710 bench: vanilla 1.0.68 started by SharedWastelandsLoader, RE_Kenshi.dll renamed .off - our own test DLL is not a mod difference */
    if (coopmods::PluginRowDecide(0, "Shared Wastelands", "KenshiCoopHello.dll") != coopmods::kPluginRowOwnMod) Fail("T-318: our test DLL is left out without RE_Kenshi");
    if (coopmods::PluginRowDecide(1, "Shared Wastelands", "KenshiCoopHello.dll") != coopmods::kPluginRowOwnMod) Fail("T-318: our test DLL is left out with RE_Kenshi too");
    if (coopmods::PluginRowDecide(1, "SHARED wastelands", "sub\\KenshiCoopHello.dll") != coopmods::kPluginRowOwnMod) Fail("T-318: our mod's name matches in any case");
    if (coopmods::PluginRowDecide(1, "SomeMod", "sub/SharedWastelands.dll") != coopmods::kPluginRowOwnMod) Fail("T-318: our DLL named by another mod's file is ours");
    /* another mod's rows: counted only while RE_Kenshi loads plugins (today's rule kept then) */
    if (coopmods::PluginRowDecide(0, "SomeMod", "Other.dll") != coopmods::kPluginRowNoReKenshi) Fail("T-318: a leftover RE_Kenshi.json is no plugin without RE_Kenshi");
    if (coopmods::PluginRowDecide(1, "SomeMod", "Other.dll") != coopmods::kPluginRowCounted) Fail("T-318: another mod's DLL counts while RE_Kenshi runs");
    if (coopmods::PluginRowDecide(1, "Shared Wastelands extras", "x.dll") != coopmods::kPluginRowCounted) Fail("T-318: a mod merely named like ours is not ours");
    if (std::string(coopmods::PluginRowWhy(coopmods::kPluginRowOwnMod)) == coopmods::PluginRowWhy(coopmods::kPluginRowNoReKenshi)) Fail("T-318: the two reasons read differently");
    /* RE_Kenshi's presence: its module in this process AND no --norekenshi */
    if (coopmods::ReKenshiLoadsPlugins(1, 0) != 1) Fail("T-318: module loaded, no flag - RE_Kenshi loads plugins");
    if (coopmods::ReKenshiLoadsPlugins(1, 1) != 0) Fail("T-318: --norekenshi - RE_Kenshi loads nothing");
    if (coopmods::ReKenshiLoadsPlugins(0, 0) != 0) Fail("T-318: no module - RE_Kenshi loads nothing");
    const char* f = "--norekenshi";
    if (!coopmods::CmdLineHasArg("\"C:\\Games\\Kenshi\\kenshi_x64.exe\" --norekenshi", f)) Fail("T-318: the flag after a quoted exe");
    if (!coopmods::CmdLineHasArg("kenshi_x64.exe  \t--norekenshi", f)) Fail("T-318: the flag after spaces and a tab");
    if (!coopmods::CmdLineHasArg("kenshi_x64.exe \"--norekenshi\"", f)) Fail("T-318: a quoted flag");
    if (coopmods::CmdLineHasArg("kenshi_x64.exe", f)) Fail("T-318: no arguments, no flag");
    if (coopmods::CmdLineHasArg("", f)) Fail("T-318: an empty command line, no flag");
    if (coopmods::CmdLineHasArg("--norekenshi", f)) Fail("T-318: argument 0 is the exe, never the flag");
    if (coopmods::CmdLineHasArg("\"C:\\x --norekenshi\\kenshi_x64.exe\"", f)) Fail("T-318: the flag inside the quoted exe path is not an argument");
    if (coopmods::CmdLineHasArg("kenshi_x64.exe --norekenshiX", f)) Fail("T-318: a longer word is not the flag");
    if (coopmods::CmdLineHasArg("kenshi_x64.exe --NoReKenshi", f)) Fail("T-318: RE_Kenshi's own compare is exact");
    if (coopmods::CmdLineHasArg("kenshi_x64.exe \"--norekenshi", f) != true) Fail("T-318: an unclosed quote still ends the last argument");
}

/* ================= settings5 S5: THE MOD LIST AT JOIN (store protocol 46) ================= */
void t_settings5_mod_check()
{
    coopmods::ModList w; w.known = 1; w.hash = "3bfe99a1ccfc1334";
    w.active.push_back("Alpha|3|0000aaaa"); w.active.push_back("Beta|1|0000bbbb"); w.active.push_back("Gamma|2|0000cccc");
    w.plugins.push_back("helper.dll|1234abcd");
    std::vector<char> b; b.push_back('x');   /* the list is APPENDED - it must read back from its own offset */
    coopmods::ModListEncode(w, &b);
    size_t at = 1; coopmods::ModList r;
    if (!coopmods::ModListDecode(b, &at, &r) || at != b.size() || !coopmods::ModListSame(w, r) || r.known != 1 || r.hash != w.hash)
        Fail("a mod list reads back row for row from where it was appended");
    std::vector<char> cut(b.begin(), b.end() - 3); at = 1;
    if (coopmods::ModListDecode(cut, &at, &r) || at != 1 || r.known != 0) Fail("a torn mod list is refused, unmoved and unknown");
    at = 0; std::vector<char> none;
    if (coopmods::ModListDecode(none, &at, &r)) Fail("an absent mod list (a 45 HELLO) does not decode");
    /* the salt knob's row: an EXTRA mod, named */
    coopmods::ModList s = w; s.active.push_back(coopmods::kSaltRow);
    if (coopmods::ModListSame(w, s)) Fail("the salt row makes the lists differ");
    coopmods::ModDiff d = coopmods::ModListDiff(w, s);
    if (d.extra.size() != 1 || d.extra[0] != "coop-test-salt" || !d.missing.empty() || !d.changed.empty() || d.orderDiffers)
        Fail("the salted game has exactly one extra mod, coop-test-salt");
    std::string txt = coopmods::ModRefusalText(d);
    if (txt.find("Mod mismatch: extra coop-test-salt.") != 0) Fail("the salted sentence reads '" + txt + "'");
    /* missing, extra, a different version, and the order */
    coopmods::ModList m; m.known = 1;
    m.active.push_back("Alpha|4|0000aaaa"); m.active.push_back("Delta|1|0000dddd"); m.active.push_back("Gamma|2|0000cccc");
    d = coopmods::ModListDiff(w, m);
    if (d.missing.size() != 2 || d.missing[0] != "Beta" || d.missing[1] != "helper.dll" || d.extra.size() != 1 || d.extra[0] != "Delta"
        || d.changed.size() != 1 || d.changed[0] != "Alpha")
        Fail("missing Beta and helper.dll, extra Delta, a different Alpha");
    txt = coopmods::ModRefusalText(d);
    if (txt.find("missing Beta, helper.dll; extra Delta; different version of Alpha.") == std::string::npos) Fail("the mixed sentence reads '" + txt + "'");
    if (coopmods::ModDetailText(d).find("missing (2): Beta, helper.dll | extra (1): Delta | different version or file (1): Alpha") == std::string::npos)
        Fail("the detail line counts and names every kind");
    /* words1 (wording audit 10): the player's detail - one line per kind that differs, no counts */
    if (coopmods::ModDetailPlayerText(d).find("Missing: Beta, helper.dll\nExtra: Delta\nDifferent version: Alpha") != 0
     || coopmods::ModDetailPlayerText(d).find("(") != std::string::npos || coopmods::ModDetailPlayerText(d).find("same") != std::string::npos)
        Fail("words1: the player's detail lines read '" + coopmods::ModDetailPlayerText(d) + "'");
    coopmods::ModList o = w; std::swap(o.active[0], o.active[2]);
    d = coopmods::ModListDiff(w, o);
    if (!d.orderDiffers || !d.missing.empty() || !d.extra.empty() || coopmods::ModRefusalText(d).find("different load order") == std::string::npos)
        Fail("the same mods in another order are refused as an order difference");
    if (coopmods::ModDetailPlayerText(d) != "Load order is different.") Fail("words1: an order-only detail reads '" + coopmods::ModDetailPlayerText(d) + "'");
    if (!coopmods::ModListDiff(w, w).missing.empty() || coopmods::ModListDiff(w, w).orderDiffers) Fail("a list against itself has no difference");
    /* more than the headline's three names: the rest are counted */
    coopmods::ModList many; many.known = 1; for (int i = 0; i < 5; ++i) many.active.push_back(std::string(1, (char)('p' + i)) + "|1|0");
    coopmods::ModList empty; empty.known = 1;
    if (coopmods::ModRefusalText(coopmods::ModListDiff(many, empty)).find("missing p, q, r and 2 more") == std::string::npos) Fail("five missing names say three and 2 more");
    /* mods.txt round trip; known=0 or no file is no record */
    if (!coopmods::ModListFromText(coopmods::ModListToText(w), &r) || !coopmods::ModListSame(w, r) || r.hash != w.hash) Fail("mods.txt reads back as written");
    std::string crlfText = coopmods::ModListToText(w); for (size_t i = 0; i < crlfText.size(); ++i) if (crlfText[i] == '\n') { crlfText.insert(i, "\r"); ++i; }
    if (!coopmods::ModListFromText(crlfText, &r) || !coopmods::ModListSame(w, r)) Fail("a CRLF mods.txt reads back as written");
    coopmods::ModList unk = w; unk.known = 0;
    if (coopmods::ModListFromText(coopmods::ModListToText(unk), &r) || coopmods::ModListFromText("", &r)) Fail("known=0 or an empty file is no record");
    /* the refusal reason and the WELCOME verdict */
    if (coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseMods, 46, 46).find("mods do not match") == std::string::npos) Fail("kRefuseMods has its own words");
    coopstore::StoreWelcome a; a.protocol = 46; a.authority = 0; a.recordCount = 2; a.slot = 1; a.hasWorld = true; a.world = "W";
    a.hasMods = true; a.modsVerdict = coopstore::kModsMatch; a.modsActive = 3; a.modsPlugins = 1;
    std::vector<char> wb; coopstore::StoreWelcomeEncode(a, &wb);
    coopstore::StoreWelcome wr2;
    if (wb.size() != 16 + 4 + 1 + 12 || !coopstore::StoreWelcomeDecode(wb, &wr2) || !wr2.hasMods || wr2.modsVerdict != 1 || wr2.modsActive != 3
        || wr2.modsPlugins != 1 || wr2.world != "W" || wr2.worldMalformed)
        Fail("a 46 WELCOME carries the mods verdict after the world");
    a.hasMods = false; coopstore::StoreWelcomeEncode(a, &wb);
    if (!coopstore::StoreWelcomeDecode(wb, &wr2) || wr2.hasMods || wr2.world != "W") Fail("a 45-shaped WELCOME reads with no verdict");
    /* the test knob */
    coopcfg::CfgFields f; std::vector<coopcfg::CfgNote> notes; int lines = 0; long long unk2 = 0, bad = 0;
    coopcfg::CfgParseText("modfingerprint_salt=1\n", &f, &notes, &lines, &unk2, &bad);
    if (f.modSalt != 1 || unk2 != 0 || bad != 0) Fail("modfingerprint_salt=1 is read, not an unknown key");
    coopcfg::CfgFields g;
    if (g.modSalt != 0) Fail("the salt is off by default");
}

/* ================= T-246 (owner 199 b / 200): a differing game is warned and let in; the refusal kept behind a constant ================= */
void t_t246_mods_warn_and_admit()
{
    using namespace coopmods;
    const std::string tail(" You can still play, but some things may look or work differently for you than for the host.");
    /* the verdict path: the warn default admits, the refusal constant still refuses */
    if (kModsMismatchPolicyNow != kModsMismatchWarn) Fail("T-246: the policy constant's default is warn (owner 199 b)");
    if (ModsHelloDecidePolicy(1, 1, 0, 0, kModsMismatchWarn) != kModsHelloAdmitDiffers) Fail("T-246: under warn a differing list is admitted");
    if (ModsHelloDecidePolicy(1, 1, 0, 0, kModsMismatchRefuse) != kModsHelloRefuseDiffers) Fail("T-246: under refuse a differing list is still refused (T-247)");
    if (ModsHelloDecidePolicy(1, 1, 0, 0, kModsMismatchPolicyNow) != kModsHelloAdmitDiffers) Fail("T-246: the shipped constant admits");
    if (ModsHelloDecidePolicy(1, 1, 0, 1, kModsMismatchWarn) != kModsHelloMatch || ModsHelloDecidePolicy(1, 1, 1, 0, kModsMismatchWarn) != kModsHelloRecord
     || ModsHelloDecidePolicy(1, 0, 0, 0, kModsMismatchWarn) != kModsHelloProvisional || ModsHelloDecidePolicy(0, 0, 0, 0, kModsMismatchWarn) != kModsHelloAdmitUnchecked)
        Fail("T-246: match, record, provisional and unchecked are unchanged under warn");
    if (ModsHelloDecidePolicy(0, 1, 0, 0, kModsMismatchWarn) != kModsHelloAdmitUnreadable || ModsHelloDecidePolicy(0, 1, 1, 0, kModsMismatchWarn) != kModsHelloOperatorUnreadable)
        Fail("T-246 fold (owner 201) / fold 2: under warn an unreadable own list is let in when the world has one - a joiner warned, the operator silently");
    if (ModsHelloDecidePolicy(0, 1, 0, 0, kModsMismatchRefuse) != kModsHelloRefuseUnreadable) Fail("T-246 fold: under refuse an unreadable own list is still refused (T-247)");
    ModList op; op.known = 1; op.active.push_back("Alpha|1|0000aaaa");
    ModList other = op; other.active.push_back(kSaltRow);
    if (ModsRecheckRefusesPolicy(op, other, kModsMismatchWarn) != 0) Fail("T-246: under warn the re-check keeps a differing game");
    if (ModsRecheckRefusesPolicy(op, other, kModsMismatchRefuse) != 1) Fail("T-246: under refuse the re-check still refuses it");
    /* the notice text: missing + extra */
    ModList w; w.known = 1;
    w.active.push_back("Alpha|3|0000aaaa"); w.active.push_back("Beta|1|0000bbbb"); w.active.push_back("Gamma|2|0000cccc");
    ModList m; m.known = 1;
    m.active.push_back("Alpha|3|0000aaaa"); m.active.push_back("Delta|1|0000dddd"); m.active.push_back("Gamma|2|0000cccc");
    std::string t = ModWarnText(ModListDiff(w, m));
    if (t != "Your mods don't match the host's: missing Beta; extra Delta." + tail) Fail("T-246: missing+extra reads '" + t + "'");
    /* changed */
    ModList c = w; c.active[0] = "Alpha|4|0000aaaa";
    t = ModWarnText(ModListDiff(w, c));
    if (t != "Your mods don't match the host's." + tail) Fail("T-246 fold (owner 201): a version difference reads the plain sentence, got '" + t + "'");
    /* all three kinds, in the approved order */
    ModList x = m; x.active[0] = "Alpha|4|0000aaaa";
    t = ModWarnText(ModListDiff(w, x));
    if (t != "Your mods don't match the host's." + tail) Fail("T-246 fold (owner 201): with a version difference among others the notice is plain, got '" + t + "'");
    /* order only */
    ModList o = w; std::swap(o.active[0], o.active[2]);
    t = ModWarnText(ModListDiff(w, o));
    if (t != "Your mods are the same as the host's but in a different order." + tail) Fail("T-246: order-only reads '" + t + "'");
    /* the 'and N more' cut */
    ModList many; many.known = 1; for (int i = 0; i < 5; ++i) many.active.push_back(std::string(1, (char)('p' + i)) + "|1|0");
    ModList empty; empty.known = 1;
    t = ModWarnText(ModListDiff(many, empty));
    if (t != "Your mods don't match the host's: missing p, q, r and 2 more." + tail) Fail("T-246: five missing read '" + t + "'");
    /* the titles: the warning is MODS DON'T MATCH, the refusal's words (T-247 path) untouched and still MOD MISMATCH */
    if (coopui::NoticeTitle(ModWarnText(ModListDiff(w, m))) != "MODS DON'T MATCH" || coopui::NoticeTitle(ModWarnText(ModListDiff(w, o))) != "MODS DON'T MATCH"
     || std::string(kModsWarnTitle) != "MODS DON'T MATCH")
        Fail("T-246: the notice is titled MODS DON'T MATCH");
    if (ModRefusalText(ModListDiff(w, m)).find("Mod mismatch: missing Beta; extra Delta.") != 0 || coopui::NoticeTitle(ModRefusalText(ModListDiff(w, m))) != "MOD MISMATCH")
        Fail("T-246: the refusal's words and title are untouched");
    /* the WELCOME: kModsDiffers carries the world's list, and the world guard's tail still reads after it */
    coopstore::StoreWelcome a; a.protocol = 57; a.recordCount = 2; a.slot = 1; a.hasWorld = true; a.world = "W";
    a.hasMods = true; a.modsVerdict = coopstore::kModsDiffers; a.modsActive = 3; a.modsPlugins = 0; a.modsWorld = w;
    a.hasWorldGen = true; a.worldTail.push_back('T'); a.worldTail.push_back('Z');
    std::vector<char> wb; coopstore::StoreWelcomeEncode(a, &wb);
    coopstore::StoreWelcome r;
    if (!coopstore::StoreWelcomeDecode(wb, &r) || !r.hasMods || r.modsVerdict != 3 || !r.hasModsWorld || !ModListSame(r.modsWorld, w)
        || !r.hasWorldGen || r.worldTail.size() != 2 || r.worldTail[0] != 'T' || r.worldTail[1] != 'Z')
        Fail("T-246: a kModsDiffers WELCOME carries the world's list before the tail");
    a.modsVerdict = coopstore::kModsMatch; coopstore::StoreWelcomeEncode(a, &wb);
    if (!coopstore::StoreWelcomeDecode(wb, &r) || r.hasModsWorld || !r.hasWorldGen || r.worldTail.size() != 2) Fail("T-246: a match WELCOME carries no list");
    a.modsVerdict = coopstore::kModsDiffers; a.hasWorldGen = false; a.worldTail.clear(); coopstore::StoreWelcomeEncode(a, &wb);
    wb.resize(wb.size() - 2);
    if (!coopstore::StoreWelcomeDecode(wb, &r) || r.hasModsWorld || r.hasWorldGen) Fail("T-246: a torn list is not read and no tail is guessed");
}

/* ================= T-246 fold (owner 201; review 2026-09-29): approved words only, the notice's life as pure steps ================= */
void t_t246_fold_notice()
{
    using namespace coopmods;
    const std::string plain("Your mods don't match the host's. You can still play, but some things may look or work differently for you than for the host.");
    /* 1: a version difference (or nothing nameable) reads the plain sentence; missing/extra are still named; the log keeps versions */
    if (ModWarnPlainText() != plain || coopui::NoticeTitle(ModWarnPlainText()) != "MODS DON'T MATCH") Fail("fold 1: the plain sentence is owner 201's, titled MODS DON'T MATCH");
    ModList w; w.known = 1; w.active.push_back("Alpha|3|0000aaaa"); w.active.push_back("Beta|1|0000bbbb");
    ModList v = w; v.active[1] = "Beta|2|0000bbbb";
    std::string t = ModWarnText(ModListDiff(w, v));
    if (t != plain || t.find("version") != std::string::npos) Fail("fold 1: a version difference reads plain, got '" + t + "'");
    if (ModDetailText(ModListDiff(w, v)).find("Beta") == std::string::npos) Fail("fold 1: the log's detail still names the version difference");
    if (ModWarnText(ModDiff()) != plain) Fail("fold 1: nothing nameable reads plain");
    ModList mi = w; mi.active.pop_back();
    if (ModWarnText(ModListDiff(w, mi)) != "Your mods don't match the host's: missing Beta. You can still play, but some things may look or work differently for you than for the host.")
        Fail("fold 1: missing only is still named");
    /* 2: an unreadable own list is admitted under warn and shown the plain notice */
    ModList unread;
    if (ModWarnNoticeText(0, 1, ModListDiff(w, unread)) != plain) Fail("fold 2: an unreadable own list gets the plain notice");
    if (ModWarnNoticeText(1, 0, ModDiff()) != plain) Fail("fold 2: no world list to compare with gets the plain notice");
    if (ModWarnNoticeText(1, 1, ModListDiff(w, mi)) != ModWarnText(ModListDiff(w, mi))) Fail("fold 2: a readable list names what it can");
    if (ModsHelloDecidePolicy(0, 1, 0, 0, kModsMismatchPolicyNow) != kModsHelloAdmitUnreadable) Fail("fold 2: the shipped constant admits an unreadable list");
    /* T-1158 step 7b (owner 1369 b): the saved list decides; none saved = the first game's list is saved; the owner's list replaces it */
    if (ModsHelloDecideWorld(1, 0, 0, 0, kModsMismatchWarn) != kModsHelloRecord) Fail("7b: with no list saved the first game's list is not saved");
    if (ModsHelloDecideWorld(0, 0, 0, 0, kModsMismatchWarn) != kModsHelloAdmitUnchecked) Fail("7b: an unreadable list with none saved is not let in unchecked");
    if (ModsHelloDecideWorld(1, 1, 0, 0, kModsMismatchWarn) != kModsHelloAdmitDiffers) Fail("7b: a differing non-owner game is not judged against the saved list");
    if (ModsHelloDecideWorld(1, 1, 0, 1, kModsMismatchWarn) != kModsHelloMatch || ModsHelloDecideWorld(1, 1, 1, 1, kModsMismatchWarn) != kModsHelloMatch) Fail("7b: a match is a match");
    if (ModsHelloDecideWorld(1, 1, 1, 0, kModsMismatchWarn) != kModsHelloRecord) Fail("7b: the owner's differing list does not replace the saved one");
    if (ModsHelloDecideWorld(1, 1, 0, 0, kModsMismatchRefuse) != kModsHelloRefuseDiffers) Fail("7b: the refuse policy is untouched");
    if (ModsOwnerLoginWrites(1, 1, 0) != 1 || ModsOwnerLoginWrites(1, 0, 0) != 1) Fail("7b: an owner login does not write its differing list");
    if (ModsOwnerLoginWrites(1, 1, 1) != 0 || ModsOwnerLoginWrites(0, 1, 0) != 0) Fail("7b: an owner login rewrote the same list, or wrote an unreadable one");
    /* 7: the admit line and count only once admitted */
    if (ModsCountAdmittedDiffers(kModsHelloAdmitDiffers, 0) != 0 || ModsCountAdmittedDiffers(kModsHelloAdmitUnreadable, 0) != 0) Fail("fold 7: not counted before admission");
    if (ModsCountAdmittedDiffers(kModsHelloAdmitDiffers, 1) != 1 || ModsCountAdmittedDiffers(kModsHelloAdmitUnreadable, 1) != 1) Fail("fold 7: counted once admitted");
    if (ModsCountAdmittedDiffers(kModsHelloMatch, 1) != 0 || ModsCountAdmittedDiffers(kModsHelloProvisional, 1) != 0 || ModsCountAdmittedDiffers(-1, 1) != 0)
        Fail("fold 7: only a differing game is counted");
    /* 3: never on screen with the host-left window */
    ModsNoticeState s;
    if (ModsNoticeOnWelcome(&s, 1, "A") != 1 || s.pending != "A") Fail("fold 3: queued");
    if (ModsNoticeStep(s, 0, 1, 1, 1) != kMnNothing) Fail("fold 3: not raised while the host-left window is up");
    if (ModsNoticeStep(s, 0, 1, 0, 0) != kMnNothing || ModsNoticeStep(s, 0, 0, 0, 1) != kMnNothing) Fail("fold 3: not raised before the world is up");
    if (ModsNoticeStep(s, 0, 1, 0, 1) != kMnShow) Fail("fold 3: raised once the host-left window is gone");
    if (ModsNoticeShown(&s, 1, 100) != 0 || !s.up || !s.pending.empty() || s.showing != "A") Fail("fold 3: shown");
    if (ModsNoticeStep(s, 0, 1, 1, 1) != kMnYield) Fail("fold 3: an up notice yields to the host-left window");
    ModsNoticeClosed(&s, 1);
    if (s.up || s.pending != "A") Fail("fold 3: a yielded notice is held, not dropped");
    if (ModsNoticeStep(s, 0, 1, 1, 1) != kMnNothing || ModsNoticeStep(s, 0, 1, 0, 1) != kMnShow) Fail("fold 3: held until the host-left window is gone");
    ModsNoticeShown(&s, 1, 200);
    if (ModsNoticeStep(s, 1, 1, 1, 1) != kMnClose || ModsNoticeStep(s, 0, 0, 0, 1) != kMnClose) Fail("fold 3: OK or the world going closes it");
    ModsNoticeClosed(&s, 0);
    if (s.up || !s.pending.empty()) Fail("fold 3: OK drops it");
    if (ModsNoticeOnWelcome(&s, 1, "A") != 0) Fail("fold 3: once per stay");
    /* 5: the retry budget is 10 s from the first failure (across the tick's wrap) and starts fresh each stay */
    ModsNoticeNewStay(&s);
    if (ModsNoticeOnWelcome(&s, 1, "B") != 1) Fail("fold 5: a new stay tells again");
    unsigned int t0 = 0xFFFFF000u;
    if (ModsNoticeShown(&s, 0, t0) != 1 || ModsNoticeShown(&s, 0, t0 + 9999u) != 1) Fail("fold 5: retried inside 10 s, across the wrap");
    if (ModsNoticeShown(&s, 0, t0 + 10000u) != 2 || !s.pending.empty() || s.failing != 0) Fail("fold 5: given up at 10 s");
    ModsNoticeNewStay(&s);
    if (ModsNoticeOnWelcome(&s, 1, "B") != 1) Fail("fold 5: the next stay queues it again");
    if (ModsNoticeShown(&s, 0, 5000u) != 1 || s.failFirstAt != 5000u || ModsNoticeShown(&s, 0, 14999u) != 1) Fail("fold 5: the budget starts fresh each stay");
    /* 6: a later match / recorded / not-checked WELCOME drops a pending notice; one on screen stays until its OK */
    if (ModsNoticeOnWelcome(&s, 0, std::string()) != 2 || !s.pending.empty() || s.failing != 0) Fail("fold 6: a later match drops a pending notice");
    if (ModsNoticeOnWelcome(&s, 0, std::string()) != 0) Fail("fold 6: nothing pending, nothing dropped");
    ModsNoticeNewStay(&s);
    ModsNoticeOnWelcome(&s, 1, "C"); ModsNoticeShown(&s, 1, 1u);
    if (ModsNoticeOnWelcome(&s, 0, std::string()) != 0 || !s.up) Fail("fold 6: one on screen stays until its OK");

    /* ---- T-246 fold 2 ---- */
    /* item 1: the operator (host) is never given a verdict that shows the notice */
    for (int lk = 0; lk < 2; ++lk) for (int hr = 0; hr < 2; ++hr) for (int sm = 0; sm < 2; ++sm)
    {
        const int v = ModsHelloDecideWorld(lk, hr, 1, (hr && lk) ? sm : 0, kModsMismatchWarn);
        if (v == kModsHelloAdmitDiffers || v == kModsHelloAdmitUnreadable) Fail("fold 2 item 1: the owner is never warned");
    }
    if (ModsHelloDecideWorld(0, 1, 1, 0, kModsMismatchPolicyNow) != kModsHelloOperatorUnreadable) Fail("fold 2 item 1: the owner's unreadable list is let in silently");
    if (ModsCountAdmittedDiffers(kModsHelloOperatorUnreadable, 1) != 0) Fail("fold 2 item 1: the operator is not counted as let in with differing mods");
    if (ModsHelloDecideWorld(0, 1, 0, 0, kModsMismatchWarn) != kModsHelloAdmitUnreadable) Fail("fold 2 item 1: a joiner's unreadable list still gets the notice");
    if (ModsHelloDecidePolicy(0, 1, 1, 0, kModsMismatchRefuse) != kModsHelloRefuseUnreadable) Fail("fold 2 item 1: the refuse policy is untouched (T-247)");
    /* item 2: the re-check of a provisional game at the first operator, written or not */
    {
        ModList wl; wl.known = 1; wl.active.push_back("Alpha|1|0000aaaa");
        ModList same = wl, diff = wl; diff.active.push_back("Beta|1|0000bbbb");
        ModList unk;
        if (ModsProvisionalRecheck(0, ModList(), diff, kModsMismatchWarn) != kMprNone) Fail("fold 2 item 2: no world list, nothing to check");
        if (ModsProvisionalRecheck(1, wl, unk, kModsMismatchWarn) != kMprNone) Fail("fold 2 item 2: an unreadable list is not judged");
        if (ModsProvisionalRecheck(1, wl, same, kModsMismatchWarn) != kMprMatch) Fail("fold 2 item 2: a matching provisional game matches");
        if (ModsProvisionalRecheck(1, wl, diff, kModsMismatchPolicyNow) != kMprAdmitDiffers) Fail("fold 2 item 2: a differing provisional game is let in with differing mods (warn)");
        if (ModsProvisionalRecheck(1, wl, diff, kModsMismatchRefuse) != kMprRefused) Fail("fold 2 item 2: under refuse it is refused (T-247)");
    }
    /* item 3: the told-once mark is set when shown, not when queued */
    ModsNoticeState q;
    if (ModsNoticeOnWelcome(&q, 1, "D") != 1 || !q.said.empty()) Fail("fold 2 item 3: queuing does not mark it said");
    if (ModsNoticeOnWelcome(&q, 1, "D") != 0) Fail("fold 2 item 3: an identical notice already waiting is not queued twice");
    ModsNoticeOnWelcome(&q, 0, std::string());
    if (ModsNoticeOnWelcome(&q, 1, "D") != 1) Fail("fold 2 item 3: a notice dropped by a match is queued again by a later identical WELCOME");
    ModsNoticeShown(&q, 0, 0u);
    if (ModsNoticeShown(&q, 0, 10000u) != 2) Fail("fold 2 item 3: given up");
    if (ModsNoticeOnWelcome(&q, 1, "D") != 1) Fail("fold 2 item 3: a notice given up is queued again by a later identical WELCOME");
    if (ModsNoticeShown(&q, 1, 20000u) != 0 || q.said != "D") Fail("fold 2 item 3: shown - now it is said");
    if (ModsNoticeOnWelcome(&q, 1, "D") != 0) Fail("fold 2 item 3: once shown, not again this stay");
    /* item 6: a re-show after a hold is not a second "once" */
    ModsNoticeClosed(&q, 1);
    if (ModsNoticeShown(&q, 1, 20100u) != 3 || !q.up) Fail("fold 2 item 6: the re-show after a hold is reported as a re-show");
    /* item 5: the retry budget is paused while held behind the host-left window (across the tick's wrap) */
    ModsNoticeNewStay(&q);
    ModsNoticeOnWelcome(&q, 1, "E");
    q.up = 0; q.showing.clear();
    const unsigned int h0 = 0xFFFFF000u;
    if (ModsNoticeShown(&q, 0, h0) != 1) Fail("fold 2 item 5: first failure");
    ModsNoticeHold(&q, 1, h0 + 5000u);
    ModsNoticeHold(&q, 1, h0 + 30000u);
    if (!q.holding) Fail("fold 2 item 5: held behind the host-left window");
    ModsNoticeHold(&q, 0, h0 + 60000u);
    if (q.holding || ModsNoticeShown(&q, 0, h0 + 64999u) != 1) Fail("fold 2 item 5: the 55 s held are not spent from the budget");
    if (ModsNoticeShown(&q, 0, h0 + 65000u) != 2) Fail("fold 2 item 5: given up after 10 s of actual trying");
}

/* ================= settings5 fold (review-settings5 1a/1b/4) ================= */
void t_settings5_fold_operator_only_and_recheck()
{
    using namespace coopmods;
    /* 1a: only the operator writes; a non-operator with no record is PROVISIONAL, never recorded */
    if (ModsHelloDecide(1, 0, 1, 0) != kModsHelloRecord)      Fail("the operator with no record writes the world's list");
    if (ModsHelloDecide(1, 1, 1, 0) != kModsHelloRecord)      Fail("the operator whose mods changed replaces the list");
    if (ModsHelloDecide(1, 0, 0, 0) != kModsHelloProvisional) Fail("a non-operator into a world with no list is provisional, not recorded");
    if (ModsHelloDecide(1, 1, 0, 1) != kModsHelloMatch || ModsHelloDecide(1, 1, 1, 1) != kModsHelloMatch) Fail("a matching list is a match");
    if (ModsHelloDecide(1, 1, 0, 0) != kModsHelloRefuseDiffers) Fail("a non-operator that differs is refused");
    /* 4: an unreadable own list - refused with a record, admitted without */
    if (ModsHelloDecide(0, 1, 0, 0) != kModsHelloRefuseUnreadable || ModsHelloDecide(0, 1, 1, 0) != kModsHelloRefuseUnreadable) Fail("an unreadable list is refused when the world has one");
    if (ModsHelloDecide(0, 0, 0, 0) != kModsHelloAdmitUnchecked || ModsHelloDecide(0, 0, 1, 0) != kModsHelloAdmitUnchecked) Fail("an unreadable list is admitted when the world has none");
    if (ModUnreadableText() != "Your game could not read its own mod list, so it can't be checked against the host's.") Fail("the unreadable sentence");
    /* 1b: provisional admit, then the operator's list is written: the re-check refuses a differing game, keeps a matching one */
    ModList op; op.known = 1; op.active.push_back("Alpha|1|0000aaaa");
    ModList same = op, other = op; other.active.push_back(kSaltRow);
    ModList unread; unread.known = 0;
    if (ModsRecheckRefuses(op, same) != 0) Fail("a provisional game with the operator's list stays");
    if (ModsRecheckRefuses(op, other) != 1) Fail("a provisional game whose list differs is refused at the re-check");
    if (ModsRecheckRefuses(op, unread) != 0) Fail("an unreadable list is judged at its next greeting, not at the re-check");
    /* 4: placeholder rows and unreadable files never match - not even each other */
    ModList p1; p1.known = 1; p1.active.push_back("(unreadable mod 0)|?|?");
    ModList p2 = p1;
    if (ModListSame(p1, p2)) Fail("two placeholder rows are not the same mod");
    ModList c1; c1.known = 1; c1.active.push_back("Beta|2|-1"); ModList c2 = c1;
    if (ModListSame(c1, c2)) Fail("two unreadable files (crc -1) are not the same file");
    ModList pl; pl.known = 1; pl.plugins.push_back("x.dll|-1"); ModList pl2 = pl;
    if (ModListSame(pl, pl2)) Fail("two unreadable plugin DLLs are not the same DLL");
    ModDiff d = ModListDiff(c1, c2);
    if (d.changed.size() != 1 || d.changed[0] != "Beta") Fail("an unreadable file is named as a different version");
    if (ModsHelloDecide(1, 1, 0, ModListSame(c1, c2) ? 1 : 0) != kModsHelloRefuseDiffers) Fail("a world list with an unreadable file admits nobody else");
    if (!ModListSame(op, same)) Fail("readable identical lists still match");
    /* nit: the salt knob's off words */
    const char* offs[5] = { "modfingerprint_salt=0\n", "modfingerprint_salt=false\n", "modfingerprint_salt=OFF\n", "modfingerprint_salt=no\n", "modfingerprint_salt=\n" };
    for (int i = 0; i < 5; ++i)
    {
        coopcfg::CfgFields f; std::vector<coopcfg::CfgNote> notes; int lines = 0; long long unk = 0, bad = 0;
        coopcfg::CfgParseText(offs[i], &f, &notes, &lines, &unk, &bad);
        if (f.modSalt != 0) Fail(std::string("the salt is off for ") + offs[i]);
    }
    coopcfg::CfgFields on; std::vector<coopcfg::CfgNote> n2; int l2 = 0; long long u2 = 0, b2 = 0;
    coopcfg::CfgParseText("modfingerprint_salt=true\n", &on, &n2, &l2, &u2, &b2);
    if (on.modSalt != 1) Fail("the salt is on for true");
}

/* loot2d (T379): a research copy's retract takes it out by its object - a copy its cell does not hold (two copies at one cell)
   is still retracted; only a take-out that handed nothing back or faulted leaves it in the box, and every outcome has a name. */
void t_loot2d_retract_reasons()
{
    if (coopres::LootRetractWhyAfterTakeOut(1, 0) != coopres::kLootRetractOkByObject || coopres::LootRetractResult(coopres::kLootRetractOkByObject) != 1)
        Fail("T379: a copy whose cell holds another item or nothing must still be retracted (taken out by its object)");
    if (coopres::LootRetractWhyAfterTakeOut(1, 1) != coopres::kLootRetractOk || coopres::LootRetractResult(coopres::kLootRetractOk) != 1)
        Fail("a copy its cell holds must be retracted");
    if (coopres::LootRetractWhyAfterTakeOut(0, 1) != coopres::kLootRetractRefused || coopres::LootRetractWhyAfterTakeOut(0, 0) != coopres::kLootRetractRefused
        || coopres::LootRetractWhyAfterTakeOut(-1, 1) != coopres::kLootRetractFault || coopres::LootRetractWhyAfterTakeOut(-1, 0) != coopres::kLootRetractFault)
        Fail("the take-out's own answer, never the cell, must decide refused / fault");
    if (coopres::LootRetractResult(coopres::kLootRetractRefused) != -1 || coopres::LootRetractResult(coopres::kLootRetractFault) != -1)
        Fail("a copy the take-out left in its box must read as stuck (-1)");
    if (coopres::LootRetractResult(coopres::kLootRetractNever) != 0 || coopres::LootRetractResult(coopres::kLootRetractNotLive) != 0
        || coopres::LootRetractResult(coopres::kLootRetractNoBox) != 0 || coopres::LootRetractResult(coopres::kLootRetractNotInBox) != 0
        || coopres::LootRetractResult(coopres::kLootRetractClaimLost) != 0)
        Fail("a copy that is not live or not in its box is nothing to take out (0)");
    for (int w = coopres::kLootRetractNever; w <= coopres::kLootRetractFault; ++w)
        if (std::string(coopres::LootRetractWhyName(w)) == "?") Fail("every retract outcome must have a name for the tripwire");
    if (std::string(coopres::LootRetractWhyName(99)) != "?") Fail("an unknown outcome must read as ?");
}

/* loot2c part B: the touched-book routes - the outcome of one give/materialise attempt and of its confirmation, which ADDs are
   a first-touch landing on a friend's character, and the tag ring (watch list / request ids). */
/* inv2a: the inventory safety net's pure part - baseline vs current -> ops, order independence, and a normal publish (or the
   net's own send) folded into the baseline so nothing is caught twice. */
coopinv::Slot InvSlot(const char* sec, int x, int y, const char* sid, int qty, float ch)
{
    coopinv::Slot s; s.section = sec; s.x = x; s.y = y; s.sid = sid; s.qty = qty; s.charges = ch;
    return s;
}
std::string InvOpsText(const std::vector<coopinv::Op>& ops)
{
    std::string t;
    char b[160];
    for (size_t i = 0; i < ops.size(); ++i)
    {
        std::sprintf(b, "%d:%s:%d,%d:%s:%d>%d:%d>%d:%d;", ops[i].kind, ops[i].section.c_str(), ops[i].x, ops[i].y, ops[i].sid.c_str(),
                     ops[i].qtyFrom, ops[i].qtyTo, coopinv::Charges100(ops[i].chFrom), coopinv::Charges100(ops[i].chTo), ops[i].chargesChanged);
        t += b;
    }
    return t;
}
/* par1 (parity P1): an AI take-one from a box - own box / the other game's box / refused / confirmed */
/* mirror1 (crash T487) - THE UID TABLE'S DECISIONS (src/common/uidtable.h).
   T487 game B: the 512-row mirror filled after one leave + rejoin with 2 players, because no row was ever freed;
   69 refused characters were adopted and driven anyway, and a DESPAWN for one of them found nothing. */
struct M1Keys
{
    const void* const* k;
    const void* operator()(int i) const { return k[i]; }
};
const void* M1Obj(int n) { return (const void*)(size_t)(0x10000 + 0x10 * n); }

/* [1] the index with release: every live entry stays findable and every released one is gone, through 20,000 random
   inserts/releases on a 64-slot table with deliberately colliding homes; an insert never fails while live <= 24.
   [2] the pre-mirror1 table (never released) could not have held that churn: more distinct objects passed through
   than it has slots. */
void t_mirror1_index_release_keeps_every_live_entry_findable()
{
    const int kSlots = 64, mask = 63, kIds = 200, kLiveMax = 24;
    const void* keys[kSlots];
    int live[kIds];
    int everInserted[kIds];
    for (int i = 0; i < kSlots; ++i) keys[i] = 0;
    for (int i = 0; i < kIds; ++i) { live[i] = 0; everInserted[i] = 0; }
    M1Keys K; K.k = keys;
    unsigned int rng = 12345u;
    int nLive = 0, trimmed = 0;
    for (int step = 0; step < 20000; ++step)
    {
        rng = rng * 1103515245u + 12345u;
        const int n = (int)((rng >> 8) % kIds);
        const void* obj = M1Obj(n);
        const int home = (n * 7) & mask;
        if (!live[n])
        {
            if (nLive >= kLiveMax) continue;
            int ex = -2;
            const int at = coopuid::IxInsertAt(K, mask, home, obj, &ex);
            if (at < 0) { Fail("[1] insert refused at step " + I(step) + " with " + I(nLive) + " live of 64 slots"); return; }
            if (ex >= 0) { Fail("[1] a released object was still found by insert at step " + I(step)); return; }
            keys[at] = obj; live[n] = 1; everInserted[n] = 1; ++nLive;
        }
        else
        {
            int s = coopuid::IxFind(K, mask, home, obj);
            if (s < 0) { Fail("[1] a live object was not found for release at step " + I(step)); return; }
            keys[s] = coopuid::Tomb();
            while (coopuid::IxTrimmable(K, mask, s)) { keys[s & mask] = 0; ++trimmed; s = (s - 1) & mask; }
            live[n] = 0; --nLive;
        }
        for (int j = 0; j < kIds; ++j)
        {
            const int f = coopuid::IxFind(K, mask, (j * 7) & mask, M1Obj(j));
            if (live[j] && f < 0) { Fail("[1] live object " + I(j) + " NOT findable at step " + I(step)); return; }
            if (!live[j] && f >= 0) { Fail("[1] released object " + I(j) + " still findable at step " + I(step)); return; }
        }
    }
    if (trimmed == 0) Fail("[1] no tombstone was ever trimmed - the trim rule is not exercised");
    int distinct = 0;
    for (int i = 0; i < kIds; ++i) distinct += everInserted[i];
    if (distinct <= kSlots)
        Fail("[2] only " + I(distinct) + " distinct objects passed through - the churn does not exceed the table, so the"
             " test cannot show that a never-released table would have filled");
}

/* [3] A, B and C as decisions, each against the T487 shape. */
void t_mirror1_adopt_drive_despawn_reclaim_decisions()
{
    /* A: T487's puppet had no row (registered uid 0). */
    if (coopuid::AdoptDecision(0u, 16777504u) != coopuid::kAdoptRefusedUnregistered) Fail("[3] an unregistered character was not refused");
    if (coopuid::AdoptDecision(16777505u, 16777504u) != coopuid::kAdoptRefusedOtherUid) Fail("[3] a row of another uid was not refused");
    if (coopuid::AdoptDecision(16777504u, 16777504u) != coopuid::kAdoptYes) Fail("[3] a registered character was refused");
    if (coopuid::DriveDecision(0u, 16777504u) != 0) Fail("[3] a puppet with no row would be driven - T487's crash");
    if (coopuid::DriveDecision(16777505u, 16777504u) != 0) Fail("[3] a puppet whose row was rebound would be driven");
    if (coopuid::DriveDecision(16777504u, 16777504u) != 1) Fail("[3] a registered puppet would not be driven");

    /* B: T487's DESPAWN - no live copy, the map and the puppet list still held it. Pre-mirror1: unknown. */
    if (coopuid::DespawnRouteLegacy(0, 1, 1) != coopuid::kRouteUnknown) Fail("[3] the legacy route is mis-stated");
    if (coopuid::DespawnRoute(0, 1, 1) != coopuid::kRouteDropStale) Fail("[3] T487's despawn does not drop the stale puppet");
    if (coopuid::DespawnRoute(0, 0, 1) != coopuid::kRouteDropStale) Fail("[3] a puppet with no map entry is not dropped");
    if (coopuid::DespawnRoute(1, 1, 1) != coopuid::kRouteRemove) Fail("[3] a live copy is not removed");
    if (coopuid::DespawnRoute(0, 0, 0) != coopuid::kRouteUnknown) Fail("[3] an unknown uid is not 'unknown'");

    /* C: only a finished identity is reclaimed; a streamed-out one keeps its row. */
    if (coopuid::ReclaimRow(1, 1, 1) != 1) Fail("[3] a destroyed row is not reclaimed");
    if (coopuid::ReclaimRow(1, 1, 0) != 0) Fail("[3] a streamed-out (dead, not destroyed) row would be reclaimed");
    if (coopuid::ReclaimRow(1, 0, 0) != 0) Fail("[3] a live row would be reclaimed");
    if (coopuid::ReclaimRow(0, 1, 1) != 0) Fail("[3] an empty row would be reclaimed");
}

/* T-758 - a body the factory has JUST built at an address a row of another uid still holds (uidtable.h D2, NewBodyRowAction):
   that row is stale and is retired and rebound; an existing body (adoption) is still refused; a destroyed row is rebound as before. */
void t_t758_new_body_stale_row()
{
    const unsigned int oldU = 8388610u, newU = 8388609u;
    if (coopuid::NewBodyRowAction(newU, oldU, 0, 0, 1) != coopuid::kRowRetireOtherAndRebind) Fail("[T-758] a just-built body at another uid's live row is still refused");
    if (coopuid::NewBodyRowAction(newU, oldU, 1, 0, 1) != coopuid::kRowRetireOtherAndRebind) Fail("[T-758] a just-built body at another uid's retired row is still refused");
    if (coopuid::NewBodyRowAction(newU, oldU, 0, 0, 0) != coopuid::kRowRefuseOtherUid) Fail("[T-758] an existing body (adoption) at another uid's live row is no longer refused");
    if (coopuid::NewBodyRowAction(newU, oldU, 1, 0, 0) != coopuid::kRowRefuseOtherUid) Fail("[T-758] an existing body (adoption) at another uid's retired row is no longer refused");
    if (coopuid::NewBodyRowAction(newU, oldU, 1, 1, 1) != coopuid::kRowRebind) Fail("[T-758] a destroyed row is not rebound for a just-built body");
    if (coopuid::NewBodyRowAction(newU, oldU, 1, 1, 0) != coopuid::kRowRebind) Fail("[T-758] a destroyed row is not rebound for an existing body");
    if (coopuid::NewBodyRowAction(newU, newU, 0, 0, 1) != coopuid::kRowAlready) Fail("[T-758] its own uid's live row is not 'already registered'");
    if (coopuid::NewBodyRowAction(newU, newU, 1, 0, 1) != coopuid::kRowUnretire) Fail("[T-758] its own uid's retired row is not un-retired");
    if (coopuid::NewBodyRowAction(0u, oldU, 1, 1, 1) != coopuid::kRowRefuseRebind) Fail("[T-758] uid 0 on a destroyed row is no longer refused (F328)");
    if (coopuid::ExistingRowAction(newU, oldU, 0, 0) != coopuid::kRowRefuseOtherUid) Fail("[T-758] the plain existing-row rule changed");
}

/* [5] mirror1 fold (review-mirror1 #3 #6 #12): the existing-row rule, the refusal log limit, and the sweep's step after a
   refusal - which never switches adoption off. */
void t_mirror1_fold_row_refusal_sweep()
{
    const unsigned int a = 16777504u, b = 16777505u;
    if (coopuid::ExistingRowAction(b, a, 0, 0) != coopuid::kRowRefuseOtherUid) Fail("[5] a live row of another uid answered 'registered' (#6)");
    if (coopuid::ExistingRowAction(b, a, 1, 0) != coopuid::kRowRefuseOtherUid) Fail("[5] a retired row of another uid would be un-retired under the old uid");
    if (coopuid::ExistingRowAction(a, a, 0, 0) != coopuid::kRowAlready) Fail("[5] the same uid on a live row is not 'already registered'");
    if (coopuid::ExistingRowAction(a, a, 1, 0) != coopuid::kRowUnretire) Fail("[5] the same uid's retired row is not un-retired");
    if (coopuid::ExistingRowAction(0u, a, 1, 0) != coopuid::kRowUnretire) Fail("[5] the came-back path (uid 0) does not un-retire");
    if (coopuid::ExistingRowAction(0u, a, 0, 0) != coopuid::kRowAlready) Fail("[5] uid 0 on a live row is not 'already registered'");
    if (coopuid::ExistingRowAction(0u, a, 1, 1) != coopuid::kRowRefuseRebind) Fail("[5] a destroyed row would keep its old uid (F328)");
    if (coopuid::ExistingRowAction(b, a, 1, 1) != coopuid::kRowRebind) Fail("[5] a destroyed row is not rebound to the new uid (F328)");

    int logged = 0;
    for (long long n = 1; n <= 1000; ++n) logged += coopuid::LogRefusal(n);
    if (logged != 15) Fail("[5] the refusal log limit is not 'the first 5, then every 100th' (15 lines in 1000)");
    if (coopuid::LogRefusal(0) != 0) Fail("[5] a zero count would log");

    if (coopuid::SweepRefusalAction(0) != coopuid::kSweepEndTick) Fail("[5] a full table does not end the tick");
    if (coopuid::SweepRefusalAction(1) != coopuid::kSweepSkipCharacter) Fail("[5] a refusal with a free row does not skip just that character");

    /* The T487 shape on a 4-row table: full -> one refusal (the tick ends, the sweep stays on) -> a row is released -> the
       next tick adopts. Before the fold the refusal switched the sweep off and the freed row was never used. */
    const int kRows = 4;
    int occ = kRows, adopted = 0, refused = 0, endedTicks = 0;
    for (int tick = 0; tick < 3; ++tick)
    {
        if (tick == 1) --occ;                                   /* a destroyed character's row is reclaimed */
        if (occ < kRows) { ++occ; ++adopted; continue; }
        ++refused;
        if (coopuid::SweepRefusalAction(occ < kRows ? 1 : 0) == coopuid::kSweepEndTick) ++endedTicks;
    }
    if (adopted != 1 || refused != 2 || endedTicks != 2) Fail("[5] the sweep did not resume once a row was released");
}

/* [4] THE LEAVE / REJOIN SHAPE on an 8-row table: session 1 fills it, `leave` removes every copy (despawn applied
   -> row released), the rejoin's 8 copies all fit. Pre-mirror1 (rows never released) the rejoin fits none. */
void t_mirror1_rejoin_fits_after_leave()
{
    const int kRows = 8;
    int occ[kRows], occLegacy[kRows];
    for (int i = 0; i < kRows; ++i) { occ[i] = 0; occLegacy[i] = 0; }
    for (int n = 0; n < kRows; ++n) { occ[n] = 1; occLegacy[n] = 1; }            /* session 1 */
    for (int n = 0; n < kRows; ++n)                                              /* leave: every copy removed */
        if (coopuid::DespawnRoute(1, 1, 1) == coopuid::kRouteRemove) occ[n] = 0; /* released after the destroy */
    int fit = 0, fitLegacy = 0;
    for (int n = 0; n < kRows; ++n)                                              /* rejoin */
    {
        for (int i = 0; i < kRows; ++i) if (!occ[i]) { occ[i] = 1; ++fit; break; }
        for (int i = 0; i < kRows; ++i) if (!occLegacy[i]) { occLegacy[i] = 1; ++fitLegacy; break; }
    }
    if (fit != kRows) Fail("[4] the rejoin fit " + I(fit) + " of " + I(kRows) + " copies after a leave");
    if (fitLegacy != 0) Fail("[4] the never-released table fit " + I(fitLegacy) + " - it must fit none (T487)");
}

/* T-925 - THE WORLD SWEEP TAKES ONE ROAD, ALONE OR WITH ANOTHER PLAYER, OWN CHARACTERS FIRST (src/common/uidtable.h I).  A model of
   worldsync.cpp's sweep: 6 adoptions a sweep tick, each candidate this game's own player faction or a world character, with its
   distance.  Whether another player is in this world is not an input of any sweep decision, so a run alone and a run with company
   adopt the same characters in the same order. */
void t_t925_sweep_one_road()
{
    if (coopuid::SweepPick(1, 1) != coopuid::kSweepPickOwn) Fail("[t925] an own candidate does not go before a world one");
    if (coopuid::SweepPick(1, 0) != coopuid::kSweepPickOwn) Fail("[t925] a lone own candidate is not picked");
    if (coopuid::SweepPick(0, 1) != coopuid::kSweepPickOther) Fail("[t925] a lone world candidate is not picked");
    if (coopuid::SweepPick(0, 0) != coopuid::kSweepPickNone) Fail("[t925] no candidate picked something");
    if (coopuid::SweepLoadLineDue(3, 0) != 1) Fail("[t925] the per-load line is not due");
    if (coopuid::SweepLoadLineDue(3, 1) != 0) Fail("[t925] the per-load line is due twice in one world load");
    if (coopuid::SweepLoadLineDue(0, 0) != 0) Fail("[t925] the per-load line is due with nothing adopted");

    /* T1408's shape: 8 own characters and 127 world characters, every world character NEARER.  Run 0: the host alone the whole
       time; run 1: another player here from the start. */
    const int kOwn = 8, kOther = 127, kAll = kOwn + kOther, kPerTick = 6;
    std::vector<double> dist(kAll, 0.0);
    for (int i = 0; i < kAll; ++i) dist[i] = (i < kOwn) ? 1000.0 + i : 1.0 + i;
    std::vector<int> order[2];
    int ownDoneTick[2] = { -1, -1 }, logLines[2] = { 0, 0 };
    long long ownLoad[2] = { 0, 0 }, worldLoad[2] = { 0, 0 };
    for (int run = 0; run < 2; ++run)
    {
        std::vector<int> adopted(kAll, 0);
        int logged = 0;
        for (int tick = 0; tick < 40; ++tick)
        {
            for (int pass = 0; pass < kPerTick; ++pass)
            {
                int bOwn = -1, bOther = -1;
                for (int i = 0; i < kAll; ++i)
                {
                    if (adopted[i]) continue;
                    if (i < kOwn) { if (bOwn < 0 || dist[i] < dist[bOwn]) bOwn = i; }
                    else if (bOther < 0 || dist[i] < dist[bOther]) bOther = i;
                }
                const int pick = coopuid::SweepPick(bOwn >= 0 ? 1 : 0, bOther >= 0 ? 1 : 0);
                if (pick == coopuid::kSweepPickNone)
                {
                    if (coopuid::SweepLoadLineDue(ownLoad[run] + worldLoad[run], logged) == 1) { logged = 1; ++logLines[run]; }
                    break;
                }
                const int b = (pick == coopuid::kSweepPickOwn) ? bOwn : bOther;
                adopted[b] = 1;
                order[run].push_back(b);
                if (b < kOwn) ++ownLoad[run]; else ++worldLoad[run];
            }
            if (ownDoneTick[run] < 0)
            {
                int left = 0;
                for (int i = 0; i < kOwn; ++i) left += adopted[i] ? 0 : 1;
                if (left == 0) ownDoneTick[run] = tick;
            }
        }
    }
    if (worldLoad[0] != kOther) Fail("[t925] alone, " + I((int)worldLoad[0]) + " of " + I(kOther) + " world characters adopted (expected all)");
    if (order[0] != order[1]) Fail("[t925] the sweep adopted in a different order alone than with another player");
    for (int run = 0; run < 2; ++run)
    {
        if (ownDoneTick[run] != 1) Fail("[t925] run " + I(run) + ": the 8 own characters took " + I(ownDoneTick[run] + 1) + " sweep ticks (expected 2: own first, 6 a tick)");
        if (ownLoad[run] != kOwn || worldLoad[run] != kOther) Fail("[t925] run " + I(run) + ": " + I((int)ownLoad[run]) + " own and " + I((int)worldLoad[run]) + " world adopted (expected 8 and 127)");
        if (logLines[run] != 1) Fail("[t925] run " + I(run) + ": " + I(logLines[run]) + " per-load lines (expected 1)");
    }
}

/* T-354 - THE TABLE SIZED FOR 256 PLAYERS, FILLED AT ITS REAL SIZE (src/common/uidtable.h G/H).  A model of spawn.cpp's
   MirrorAdd / MirrorRelease: the flat array (first free row) and the index (coopuid::IxHome / IxInsertAt / IxFind / IxTrimmable),
   with heap-like addresses 0x40 apart. */
struct T354Table
{
    std::vector<const void*> rows, keys;
    int used;
    T354Table() : rows(coopuid::kMirrorRows, (const void*)0), keys(coopuid::kMirrorIndexSlots, (const void*)0), used(0) {}
    struct K { const std::vector<const void*>* k; const void* operator()(int i) const { return (*k)[i]; } };
    /* 1 registered, 0 refused (no free row or the cap), -1 the index refused (must never happen) */
    int Add(const void* obj, int testCap)
    {
        int freeSlot = -1;
        for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == 0) { freeSlot = i; break; }
        if (freeSlot >= 0 && coopuid::MirrorCapReached(used, testCap)) freeSlot = -1;
        if (freeSlot < 0) return 0;
        K kk; kk.k = &keys;
        int ex = -1;
        const int mask = coopuid::kMirrorIndexSlots - 1;
        const int at = coopuid::IxInsertAt(kk, mask, coopuid::IxHome(obj, mask), obj, &ex);
        if (at < 0 || ex >= 0) return -1;
        keys[at] = obj; rows[freeSlot] = obj; ++used;
        return 1;
    }
    bool Release(const void* obj)
    {
        int r = -1;
        for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == obj) { r = i; break; }
        if (r < 0) return false;
        rows[r] = 0; --used;
        K kk; kk.k = &keys;
        const int mask = coopuid::kMirrorIndexSlots - 1;
        int s = coopuid::IxFind(kk, mask, coopuid::IxHome(obj, mask), obj);
        if (s < 0) return false;
        keys[s] = coopuid::Tomb();
        while (coopuid::IxTrimmable(kk, mask, s)) { keys[s & mask] = 0; s = (s - 1) & mask; }
        return true;
    }
    bool Findable(const void* obj) const
    {
        K kk; kk.k = &keys;
        const int mask = coopuid::kMirrorIndexSlots - 1;
        return coopuid::IxFind(kk, mask, coopuid::IxHome(obj, mask), obj) >= 0;
    }
};
const void* T354Obj(int n) { return (const void*)(size_t)(0x20000000ull + 0x40ull * (unsigned long long)n); }

/* [1] the bound: 4096 rows for 256 players, the index 4 x and a power of two; [2] fill to the bound - every row registers, every
   one is findable, the next is refused (the refusal path, not a half-registration); [3] the TEST-ONLY cap refuses while the table
   has room, and off restores it. */
void t_t354_fill_to_bound_then_refuse()
{
    if (coopuid::kMirrorRows != 4096) Fail("[1] the mirror is not 4096 rows");
    if (coopuid::kMirrorIndexSlots != 4 * coopuid::kMirrorRows) Fail("[1] the index is not 4 x the rows");
    if ((coopuid::kMirrorIndexSlots & (coopuid::kMirrorIndexSlots - 1)) != 0) Fail("[1] the index size is not a power of two");
    if (coopuid::kMirrorRows < 256 * 10) Fail("[1] 256 players at 10 characters each do not fit");
    T354Table t;
    for (int n = 0; n < coopuid::kMirrorRows; ++n)
    {
        const int r = t.Add(T354Obj(n), 0);
        if (r != 1) { Fail("[2] row " + I(n) + " of " + I(coopuid::kMirrorRows) + " was not registered (" + I(r) + ")"); return; }
    }
    for (int n = 0; n < coopuid::kMirrorRows; ++n) if (!t.Findable(T354Obj(n))) { Fail("[2] a registered row is not findable"); return; }
    if (t.Add(T354Obj(coopuid::kMirrorRows), 0) != 0) Fail("[2] the row past the bound was not refused");
    if (t.Findable(T354Obj(coopuid::kMirrorRows))) Fail("[2] a refused character was half-registered (indexed with no row)");
    T354Table c;
    for (int n = 0; n < 3; ++n) if (c.Add(T354Obj(n), 3) != 1) Fail("[3] a row under the test cap was refused");
    if (c.Add(T354Obj(3), 3) != 0) Fail("[3] the test cap did not refuse the 4th row");
    if (c.Add(T354Obj(3), 0) != 1) Fail("[3] cap off did not take the row");
    if (coopuid::MirrorCapReached(5, 0) != 0 || coopuid::MirrorCapReached(2, 3) != 0 || coopuid::MirrorCapReached(3, 3) != 1)
        Fail("[3] MirrorCapReached is not 'cap > 0 and used >= cap'");
}

/* [4] release and tombstone reuse AT THE BOUND: from a full table, release 1,500 rows in a scattered order (tombstones, trims),
   every survivor stays findable and every released one is gone; 1,500 new characters then fit (rows and tombstones reused), the
   next is refused again; 20 rounds of that churn never make the index refuse (the T487 shape, at 256-player size). */
void t_t354_release_reuse_at_bound()
{
    T354Table t;
    for (int n = 0; n < coopuid::kMirrorRows; ++n) if (t.Add(T354Obj(n), 0) != 1) { Fail("[4] the fill failed"); return; }
    std::vector<int> liveIds;
    for (int n = 0; n < coopuid::kMirrorRows; ++n) liveIds.push_back(n);
    int next = coopuid::kMirrorRows;
    unsigned int rng = 354u;
    for (int round = 0; round < 20; ++round)
    {
        std::vector<int> released;
        for (int k = 0; k < 1500; ++k)
        {
            rng = rng * 1103515245u + 12345u;
            const int pick = (int)((rng >> 8) % (unsigned int)liveIds.size());
            const int id = liveIds[pick];
            liveIds[pick] = liveIds.back(); liveIds.pop_back();
            if (!t.Release(T354Obj(id))) { Fail("[4] a release failed in round " + I(round)); return; }
            released.push_back(id);
        }
        for (size_t i = 0; i < released.size(); ++i) if (t.Findable(T354Obj(released[i]))) { Fail("[4] a released character is still findable"); return; }
        for (size_t i = 0; i < liveIds.size(); ++i) if (!t.Findable(T354Obj(liveIds[i]))) { Fail("[4] a survivor is no longer findable in round " + I(round)); return; }
        for (int k = 0; k < 1500; ++k)
        {
            const int r = t.Add(T354Obj(next), 0);
            if (r != 1) { Fail("[4] a new character did not fit after the release (" + I(r) + ") in round " + I(round)); return; }
            liveIds.push_back(next++);
        }
        if (t.used != coopuid::kMirrorRows || t.Add(T354Obj(next), 0) != 0) { Fail("[4] the refilled table is not full at the bound"); return; }
    }
}

/* [5] the refusal path's message: NOT_SHOWN bytes round-trip, malformed ones are refused, the copy's game sends only for a table
   refusal, the owner counts only its own character. */
void t_t354_not_shown_to_owner()
{
    unsigned char b[16];
    if (coopuid::EncodeNotShown(b, (unsigned int)sizeof(b), 0x01000123u, coopuid::kNsNoRow, 77u) != coopuid::kNotShownSize) Fail("[5] encode refused a good message");
    unsigned int uid = 0, n = 0; int reason = 0;
    if (coopuid::DecodeNotShown(b, coopuid::kNotShownSize, &uid, &reason, &n) != coopuid::kNsDecodeOk || uid != 0x01000123u || reason != coopuid::kNsNoRow || n != 77u)
        Fail("[5] the round trip lost a field");
    if (b[0] != 0x23 || b[3] != 0x01 || b[4] != 1 || b[5] != 77) Fail("[5] the bytes are not little-endian uid | reason | count");
    if (coopuid::DecodeNotShown(b, coopuid::kNotShownSize - 1, &uid, &reason, &n) != coopuid::kNsDecodeShort) Fail("[5] a short message decoded");
    if (coopuid::EncodeNotShown(b, (unsigned int)sizeof(b), 0u, coopuid::kNsNoRow, 1u) != 0) Fail("[5] uid 0 encoded");
    if (coopuid::EncodeNotShown(b, (unsigned int)sizeof(b), 5u, 0, 1u) != 0 || coopuid::EncodeNotShown(b, (unsigned int)sizeof(b), 5u, 5, 1u) != 0) Fail("[5] a reason outside 1..4 encoded");
    if (coopuid::EncodeNotShown(b, 8u, 5u, 1, 1u) != 0) Fail("[5] a short buffer was written");
    unsigned char bad[9] = { 0, 0, 0, 0, 1, 0, 0, 0, 0 };
    if (coopuid::DecodeNotShown(bad, 9u, &uid, &reason, &n) != coopuid::kNsDecodeBadUid) Fail("[5] uid 0 decoded");
    unsigned char badR[9] = { 5, 0, 0, 0, 9, 0, 0, 0, 0 };
    if (coopuid::DecodeNotShown(badR, 9u, &uid, &reason, &n) != coopuid::kNsDecodeBadReason) Fail("[5] reason 9 decoded");
    if (coopuid::SpawnRefusedByTable(4, 5) != 1) Fail("[5] a table refusal did not send");
    if (coopuid::SpawnRefusedByTable(5, 5) != 0) Fail("[5] a SPAWN that failed for another reason (or succeeded) would send");
    if (coopuid::NotShownOwnerAction(1) != coopuid::kNsOwnerCounted) Fail("[5] the owner did not count its own character");
    if (coopuid::NotShownOwnerAction(0) != coopuid::kNsOwnerNotMine) Fail("[5] a uid the owner does not run was counted as its own");
    for (int r = coopuid::kNsNoRow; r <= coopuid::kNsReasonMax; ++r)
        if (coopuid::NotShownReasonName(r)[0] == '?') Fail("[5] a reason has no name");
}

void t_par1_takeone_route()
{
    using namespace cooptake1;
    if (Route(1, 0, 1, 0) != kT1Publish) Fail("own box, unit in our pack: published as before");
    if (Route(1, 0, 0, 0) != kT1Publish) Fail("own box, unit eaten: published as before");
    if (Route(0, 1, 1, 0) != kT1Transfer) Fail("other game's box, unit in our pack: undone and asked of the writer");
    if (Route(0, 1, 0, 0) != kT1Consume) Fail("other game's box, unit used where it was taken: the writer is asked to remove it");
    if (Route(0, 1, 1, 1) != kT1Consume) Fail("other game's box, unit landed and moved on: a consume, never a transfer");
    if (Route(0, 0, 1, 0) != kT1Drop) Fail("no writer named: not this road");
    if (Route(0, 0, 0, 0) != kT1Drop) Fail("no writer named, unit used: not this road");
    if (OnConfirm(1, 0) != kT1CfPlace) Fail("confirmed transfer: the taker gets its unit");
    if (OnConfirm(0, 0) != kT1CfNothing) Fail("refused transfer: the taker gets nothing, the unit stays in the box");
    if (OnConfirm(1, 1) != kT1CfAnswerOk) Fail("confirmed consume: nothing placed, the writer is answered ok");
    if (OnConfirm(0, 1) != kT1CfNothing) Fail("refused consume: nothing changes here");
    if (LateConsume(1) != 1) Fail("late ok consume: answered ok");
    if (LateConsume(0) != 0) Fail("late refused consume: nothing");
    if (UndoQty(4) != 5) Fail("undo: the lowered stack goes back up by one");
    if (UndoQty(1) != 2) Fail("undo: a 2-stack lowered to 1 goes back to 2");
    if (UndoQty(0) != -1) Fail("undo: a stack of none is not this write");
    /* review-par1 fold F6: two takes from one 5-stack in one drain leave it at 3; each undo raises the CURRENT count by one */
    {
        int q = 3;
        q = UndoQty(q); q = UndoQty(q);
        if (q != 5) Fail("undo: two consumes from one stack in one drain give both units back (relative, never absolute)");
    }
    /* review-par1 fold F1: a transfer whose undo failed goes the consume way */
    if (AfterTransfer(1) != kT1Transfer) Fail("transfer undone: counted as a transfer");
    if (AfterTransfer(0) != kT1Consume) Fail("transfer undo failed: the unit stays in our pack and the writer is asked to remove one");
    /* the ledger for a 5-stack: units in the world after the round trip = box (both games) + what the taker holds */
    {
        const int q0 = 5;
        const int takerT = (OnConfirm(1, 0) == kT1CfPlace) ? 1 : 0;   /* confirmed transfer: box 4 on both games, taker 1 */
        if ((q0 - 1) + takerT != q0) Fail("confirmed transfer: no unit made or lost");
        const int takerR = (OnConfirm(0, 0) == kT1CfPlace) ? 1 : 0;   /* refused transfer: box 5 on both games, taker 0 */
        if (q0 + takerR != q0) Fail("refused transfer: no unit made");
        const int undone = UndoQty(q0 - 1);                             /* confirmed consume: ours back to 5, the writer's take -> 4 */
        const int placed = (OnConfirm(1, 1) == kT1CfPlace) ? 1 : 0;
        if (undone != q0 || (undone - 1) + 1 + placed != q0) Fail("confirmed consume: box one short on both games, nothing placed");
        /* review-par1 fold F1: the undo failed, the unit stayed in our pack (1); as a confirmed consume the writer's box -> 4 */
        const int kept = (AfterTransfer(0) == kT1Consume && OnConfirm(1, 1) == kT1CfAnswerOk) ? 1 : 0;
        if ((q0 - 1) + kept != q0) Fail("transfer fallen back to consume: no unit made or lost");
    }
}
/* T-608 / T-534 (F1042): the NPC purchase gate and the drop re-ask limiter - every branch */
void t_t608_npc_buy_gate()
{
    using namespace coopinv;
    /* the seller's class */
    if (NpcBuyBoxClass(0, 0, 0, 0) != kBuyBoxNone || NpcBuyBoxClass(0, 1, 1, 1) != kBuyBoxNone) Fail("buy gate: a seller that is not a keyed box is none, whatever else is said");
    if (NpcBuyBoxClass(1, 1, 0, 0) != kBuyBoxMine || NpcBuyBoxClass(1, 1, 1, 1) != kBuyBoxMine) Fail("buy gate: a box this game writes is mine");
    if (NpcBuyBoxClass(1, 0, 0, 0) != kBuyBoxNotMine || NpcBuyBoxClass(1, 0, 0, 1) != kBuyBoxNotMine) Fail("buy gate: a definite not-mine answer is not mine (an unread owner without the off-main wait changes nothing)");
    if (NpcBuyBoxClass(1, 0, 1, 1) != kBuyBoxUnread) Fail("buy gate: the off-main wait on a never-read owner is unread");
    if (NpcBuyBoxClass(1, 0, 1, 0) != kBuyBoxPlayerWait) Fail("buy gate: the off-main wait on a read (player-owned) owner is the player wait");
    /* the gate */
    for (int c = 0; c <= 1; ++c)
        for (int b = kBuyBoxNone; b <= kBuyBoxPeerOwner; ++b)
            if (NpcBuyGate(1, c, b) != kBuyRun) Fail("buy gate: the mod's own apply always runs");
    for (int b = kBuyBoxNone; b <= kBuyBoxPeerOwner; ++b)
        if (NpcBuyGate(0, 1, b) != kBuyRefuseCopy) Fail("buy gate: a copy's purchase is refused on every seller");
    if (NpcBuyGate(0, 0, kBuyBoxNone) != kBuyRun) Fail("buy gate: a seller the mod cannot name keeps the engine's purchase");
    if (NpcBuyGate(0, 0, kBuyBoxMine) != kBuyRun) Fail("buy gate: this game's own NPC buys from a box this game writes");
    if (NpcBuyGate(0, 0, kBuyBoxNotMine) != kBuyRefuseNotWriter) Fail("buy gate: a box this game does not write refuses the purchase");
    if (NpcBuyGate(0, 0, kBuyBoxUnread) != kBuyRefuseLearn) Fail("buy gate: a never-read owner refuses and asks the main thread to read it");
    if (NpcBuyGate(0, 0, kBuyBoxPlayerWait) != kBuyRunPlayerBox) Fail("buy gate: a player-owned box off the main thread runs and the drain decides");
    if (NpcBuyGate(0, 0, 99) != kBuyRefuseNotWriter) Fail("buy gate: an unknown class is refused (no writer named)");
    /* what the detour refuses */
    if (NpcBuyRefused(kBuyRun) != 0 || NpcBuyRefused(kBuyRunPlayerBox) != 0) Fail("buy gate: the run answers run the purchase");
    if (NpcBuyRefused(kBuyRefuseCopy) != 1 || NpcBuyRefused(kBuyRefuseNotWriter) != 1 || NpcBuyRefused(kBuyRefuseLearn) != 1) Fail("buy gate: every refusal returns 0 without the purchase");
    /* the refusal lines: the first ten, then every 100th */
    if (RefusalSay(0, 10, 100) != 0 || RefusalSay(-3, 10, 100) != 0) Fail("refusal say: no refusal, no line");
    if (RefusalSay(1, 10, 100) != 1 || RefusalSay(10, 10, 100) != 1) Fail("refusal say: the first ten are said");
    if (RefusalSay(11, 10, 100) != 0 || RefusalSay(99, 10, 100) != 0 || RefusalSay(101, 10, 100) != 0) Fail("refusal say: between the marks, quiet");
    if (RefusalSay(100, 10, 100) != 1 || RefusalSay(300, 10, 100) != 1) Fail("refusal say: every 100th is said");
    if (RefusalSay(50, 10, 0) != 0 || RefusalSay(5, 10, 0) != 1) Fail("refusal say: no period - only the first ten");
    /* the per-box re-ask limiter ages out by time alone, across the tick counter's wrap */
    if (ReaskDue(0, 0u, 123u, 5000u) != 1) Fail("re-ask: a box never asked is due");
    if (ReaskDue(1, 1000u, 5999u, 5000u) != 0) Fail("re-ask: within 5 s of the last ask, not due");
    if (ReaskDue(1, 1000u, 6000u, 5000u) != 1 || ReaskDue(1, 1000u, 900000u, 5000u) != 1) Fail("re-ask: 5 s on, due again (the stamp ages out)");
    if (ReaskDue(1, 0xFFFFF000u, 0x00000100u, 5000u) != 0) Fail("re-ask: across the wrap, 4.35 s on, not due");
    if (ReaskDue(1, 0xFFFFF000u, 0x00001000u, 5000u) != 1) Fail("re-ask: across the wrap, 8.2 s on, due");
}
/* T-608 (owner decision 971 b): an NPC purchase from another player's counter - the gate's new class, stock kind 2 on the wire,
   the owner's verdict, the answer table, one purchase in flight per NPC, the lapse, one count per visit */
void t_t608_npc_buy_road()
{
    using namespace coopinv;
    /* the class and the gate */
    if (NpcBuyBoxClass(1, 0, 1, 0, 1) != kBuyBoxPeerOwner || NpcBuyBoxClass(1, 0, 0, 0, 1) != kBuyBoxPeerOwner) Fail("npc road: another player's counter its game writes is the peer class");
    if (NpcBuyBoxClass(1, 1, 0, 0, 1) != kBuyBoxMine) Fail("npc road: a counter this game writes stays mine");
    if (NpcBuyBoxClass(0, 0, 0, 0, 1) != kBuyBoxNone) Fail("npc road: an unkeyed seller is none");
    if (NpcBuyBoxClass(1, 0, 1, 0) != kBuyBoxPlayerWait) Fail("npc road: the old four-argument class is unchanged");
    if (NpcBuyGate(0, 0, kBuyBoxPeerOwner) != kBuySendOwner) Fail("npc road: the peer class sends");
    if (NpcBuyGate(0, 1, kBuyBoxPeerOwner) != kBuyRefuseCopy) Fail("npc road: a copy's purchase is refused, not sent");
    if (NpcBuyRefused(kBuySendOwner) != 1) Fail("npc road: a sent purchase is refused locally (the buying game applies nothing on its own)");
    /* the main-thread send check */
    if (NpcBuySendDecide(1, 1, 1, 1, 1) != kNbSend) Fail("npc road: owner's game writes it -> sent");
    if (NpcBuySendDecide(0, 1, 1, 1, 1) != kNbBuyerGone) Fail("npc road: the NPC gone -> not sent");
    if (NpcBuySendDecide(1, 0, 1, 1, 1) != kNbNoCounter) Fail("npc road: no counter -> not sent");
    if (NpcBuySendDecide(1, 1, 0, 1, 1) != kNbNoLink) Fail("npc road: no link (owner offline) -> refused");
    if (NpcBuySendDecide(1, 1, 1, 0, 1) != kNbNotOwnerGame) Fail("npc road: area not loaded on the owner's game -> refused, never a local sale");
    if (NpcBuySendDecide(1, 1, 1, 1, 0) != kNbNotOwnerGame) Fail("npc road: no other player owner resolved -> refused");
    /* one in flight per NPC */
    {
        int a = 1, b2 = 2, c = 3;
        const void* busy[3] = { 0, &a, 0 };
        if (NpcBuyAdmit(busy, 3, &a) != kNbAdmitSame) Fail("npc road: a second purchase of an NPC in flight is refused quietly");
        if (NpcBuyAdmit(busy, 3, &b2) != 0) Fail("npc road: another NPC takes the first free slot");
        const void* full[2] = { &a, &b2 };
        if (NpcBuyAdmit(full, 2, &c) != kNbAdmitFull) Fail("npc road: no free slot -> full");
        if (NpcBuyAdmit(full, 2, &b2) != kNbAdmitSame) Fail("npc road: a full table still names the same NPC first");
        if (NpcBuyAdmit(busy, 3, 0) != 0) Fail("npc road: a null buyer never matches a free slot as 'same'");
    }
    /* one count per visit */
    if (NpcBuyNewVisit(0, 1, 0u, 10u, 20000u) != 1 || NpcBuyNewVisit(1, 0, 0u, 10u, 20000u) != 1) Fail("visit: another buyer or another box is a new visit");
    if (NpcBuyNewVisit(1, 1, 1000u, 5000u, 20000u) != 0) Fail("visit: the same buyer at the same box within 20 s is the same visit");
    if (NpcBuyNewVisit(1, 1, 1000u, 21000u, 20000u) != 1) Fail("visit: 20 s on, a new visit");
    if (NpcBuyNewVisit(1, 1, 0xFFFFF000u, 0x00000100u, 20000u) != 0) Fail("visit: across the tick wrap, still the same visit");
    using namespace coopshop;
    /* the wire: stock kind 2 round-trips; its shape is checked both ways */
    {
        ShopReq r; r.has = 1; r.keeperUid = 0; r.homeKey = "3384-TwoStorey.mod@21,31@-505887,-12252,10734"; r.sid = "1230-gamedata.base";
        r.n = 1; r.unitPrice = 0; r.entry = kEntryOne; r.epoch = 77; r.stock = kStockNpcBuy;
        std::vector<char> w;
        if (!EncodeReq(&w, r)) Fail("npc wire: an NPC purchase did not encode");
        else
        {
            size_t at = 0; ShopReq d;
            if (!ReqTagAt(&w[0], w.size(), 0) || !DecodeReq(&w[0], w.size(), &at, &d) || at != w.size() || d.stock != kStockNpcBuy || d.homeKey != r.homeKey
                || d.keeperUid != 0 || d.n != 1 || d.entry != kEntryOne || d.unitPrice != 0 || d.epoch != 77u || d.sid != r.sid) Fail("npc wire: an NPC purchase did not round-trip");
            ShopReq h = r; h.stock = kStockHome; h.n = 2; std::vector<char> hb;   /* the same block with two units and kind 2 written in */
            if (EncodeReq(&hb, h) && hb.size() == w.size())
            {
                hb[hb.size() - 1] = (char)kStockNpcBuy;
                size_t at2 = 0; ShopReq d2;
                if (DecodeReq(&hb[0], hb.size(), &at2, &d2)) Fail("npc wire: a decoded NPC purchase of two units");
            }
            else Fail("npc wire: the comparison block did not build");
        }
        ShopReq k = r; k.homeKey.clear(); std::vector<char> kb;
        if (EncodeReq(&kb, k)) Fail("npc wire: an NPC purchase with no counter key encoded");
        ShopReq two = r; two.n = 2; std::vector<char> tb;
        if (EncodeReq(&tb, two)) Fail("npc wire: an NPC purchase of two units encoded");
        ShopReq kp = r; kp.keeperUid = 9; std::vector<char> kpb;
        if (EncodeReq(&kpb, kp)) Fail("npc wire: an NPC purchase naming a keeper encoded");
        ShopReq dr = r; dr.entry = kEntryDrag; std::vector<char> drb;
        if (EncodeReq(&drb, dr)) Fail("npc wire: an NPC purchase with a drag entry encoded");
        if (StockKeyOk(kStockNpcBuy, "B:1,2:3") != 1 || StockKeyOk(kStockNpcBuy, "") != 0 || StockKeyOk(3, "B:1,2:3") != 0) Fail("npc wire: the stock-key rule");
        if (NpcBuyShapeOk(kStockHome, 5, 7, kEntryAll) != 1) Fail("npc wire: the shape rule leaves the other kinds alone");
    }
    /* the owner's verdict: nothing moves before ok */
    if (NpcBuyHolder(1, 1, 1, 0, 1, 1) != kVOk) Fail("npc owner: the whole check -> ok");
    if (NpcBuyHolder(0, 1, 1, 0, 1, 1) != kVMismatch) Fail("npc owner: a bad block first");
    if (NpcBuyHolder(1, 0, 1, 0, 1, 1) != kVNoHome) Fail("npc owner: no counter here");
    if (NpcBuyHolder(1, 1, 0, 0, 1, 1) != kVNotHolder) Fail("npc owner: not this game's to sell (the owner not loaded there, or not ours)");
    if (NpcBuyHolder(1, 1, 1, kVSoldOut, 1, 1) != kVSoldOut) Fail("npc owner: the refusenext lever's verdict");
    if (NpcBuyHolder(1, 1, 1, 0, 0, 1) != kVBusy) Fail("npc owner: a full record table refuses before anything moves");
    if (NpcBuyHolder(1, 1, 1, 0, 1, 0) != kVSoldOut) Fail("npc owner: the item is no longer on the counter -> sold out");
    /* the buying game's answer table */
    if (NpcBuyAnswer(1, 1, kVOk, 1) != (kNbAck | kNbGive | kNbPay)) Fail("npc answer: ok, NPC here -> give, pay, ack");
    if (NpcBuyAnswer(1, 1, kVOk, 0) != kNbAck) Fail("npc answer: ok, NPC gone -> ack only (the accepted residual)");
    if (NpcBuyAnswer(0, 1, kVSoldOut, 1) != kNbReask || NpcBuyAnswer(0, 1, kVNotHolder, 1) != kNbReask) Fail("npc answer: a refusal moves nothing and re-asks");
    if (NpcBuyAnswer(1, 0, kVOk, 1) != kNbReask) Fail("npc answer: an ok with no shop block is not a sale");
    if (NpcBuyAnswer(1, 1, kVFault, 1) != kNbReask) Fail("npc answer: ok with a non-ok verdict is not a sale");
    /* the lapse */
    if (NpcBuyAge(1000u, 5000u, 0, 10000u, 300000u) != kNbKeep) Fail("npc lapse: 4 s, kept");
    if (NpcBuyAge(1000u, 11000u, 0, 10000u, 300000u) != kNbLapse) Fail("npc lapse: 10 s, lapsed (the NPC is freed)");
    if (NpcBuyAge(1000u, 11000u, 1, 10000u, 300000u) != kNbKeep) Fail("npc lapse: lapsed once, kept for a late ok");
    if (NpcBuyAge(1000u, 301000u, 1, 10000u, 300000u) != kNbForget) Fail("npc lapse: 5 minutes, forgotten");
    if (NpcBuyAge(0xFFFFF000u, 0x00001000u, 0, 10000u, 300000u) != kNbKeep) Fail("npc lapse: across the wrap, 8 s, kept");
    /* fold 2 (review H1): one unit is taken and priced - a stack of one leaves whole, a bigger stack gives one */
    if (NpcBuyTakeCount(1) != -1 || NpcBuyTakeCount(0) != -1 || NpcBuyTakeCount(2) != 1 || NpcBuyTakeCount(40) != 1) Fail("npc fold2: one unit is taken, never the stack");
    /* fold 2 (review M1): the budget */
    if (NpcBuyAfford(120, 120) != kVOk || NpcBuyAfford(0, 0) != kVOk) Fail("npc fold2: a price within the budget sells");
    if (NpcBuyAfford(121, 120) != kVBuyerShort || NpcBuyAfford(5, -1) != kVBuyerShort || NpcBuyAfford(-1, 50) != kVBuyerShort) Fail("npc fold2: above the budget (or unread) is short");
    if (NpcBuyAnswer(0, 1, kVBuyerShort, 1) != kNbReask) Fail("npc fold2: a short answer moves nothing");
    if (coopinv::NpcBuySendDecide(1, 1, 1, 1, 1, 0, 1) != coopinv::kNbNoBudget) Fail("npc fold2: the NPC's money unread -> not sent");
    if (coopinv::NpcBuySendDecide(1, 1, 1, 1, 1, 1, 0) != coopinv::kNbNoRoom) Fail("npc fold2: no room for one unit -> not sent");
    if (coopinv::NpcBuySendDecide(1, 1, 1, 1, 1, 1, -1) != coopinv::kNbNotOnCounter) Fail("npc fold2: the item gone from our copy -> not sent");
    if (coopinv::NpcBuySendDecide(1, 1, 1, 0, 1, 0, 0) != coopinv::kNbNotOwnerGame) Fail("npc fold2: the owner check comes first");
    if (coopinv::NpcBuySendDecide(1, 1, 1, 1, 1, 1, 1) != coopinv::kNbSend) Fail("npc fold2: all checks pass -> sent");
    /* fold 2 (review L2): the owner's payment */
    if (NpcBuyOwnerPaid(1, 0) != kNbOwnPaid) Fail("npc fold2: paid -> the sale stands");
    if (NpcBuyOwnerPaid(-1, 1) != kNbOwnRefuse || NpcBuyOwnerPaid(0, 1) != kNbOwnRefuse) Fail("npc fold2: not paid, unit back -> refused");
    if (NpcBuyOwnerPaid(-1, 0) != kNbOwnFree) Fail("npc fold2: not paid, unit not back -> price 0, never a full price");
    /* fold 2 (review M2 b): the ack of a forgotten OK */
    if (NpcBuyOrphanAnswer(1, 1, 1, kVOk) != kNbAck) Fail("npc fold2: an OK after the row was dropped is acknowledged");
    if (NpcBuyOrphanAnswer(1, 0, 1, kVSoldOut) != 0) Fail("npc fold2: a refusal after the row was dropped needs nothing");
    if (NpcBuyOrphanAnswer(0, 1, 1, kVOk) != 0) Fail("npc fold2: an id that was never ours is not acked here");
}
void t_inv2b_direct_writes()
{
    if (coopinv::TakeOneLowered(5, 4) != 1) Fail("takeOneItemOnly: a 5-stack lowered to 4 must be published");
    if (coopinv::TakeOneLowered(2, 1) != 1) Fail("takeOneItemOnly: a 2-stack lowered to 1 must be published");
    if (coopinv::TakeOneLowered(1, 0) != 0) Fail("takeOneItemOnly: a stack of one leaves through removeItem (hook B) - not a direct write");
    if (coopinv::TakeOneLowered(5, 5) != 0) Fail("takeOneItemOnly: an unchanged stack must not be published");
    if (coopinv::TakeOneLowered(5, 3) != 0) Fail("takeOneItemOnly: a drop of two is not this write - never guessed at");
    /* inv2c: Inventory::buyItem's seller-side direct write */
    if (coopinv::BuyLowered(5, 4, 1) != 1) Fail("buyItem: a 5-stack lowered to 4 with a clone handed over must be published");
    if (coopinv::BuyLowered(2, 1, 1) != 1) Fail("buyItem: a 2-stack lowered to 1 with a clone handed over must be published");
    if (coopinv::BuyLowered(5, 5, 0) != 0) Fail("buyItem: a refused buy (unit put back, nothing returned) must not be published");
    if (coopinv::BuyLowered(5, 5, 1) != 0) Fail("buyItem: an unchanged stack must not be published");
    if (coopinv::BuyLowered(1, 0, 0) != 0) Fail("buyItem: a stack of one leaves whole through removeItem (hook B) - not a direct write");
    if (coopinv::BuyLowered(5, 4, 0) != 0) Fail("buyItem: no clone came back - not the stack path, never guessed at");
    if (coopinv::BuyLowered(5, 3, 1) != 0) Fail("buyItem: a drop of two is not this write - never guessed at");
    int after = -7;
    const int q1[] = { 3 };
    if (coopinv::ConsumePartial(q1, 1, 2, &after) != 0 || after != 1) Fail("research: need 2 from a 3-stack lowers stack 0 to 1");
    const int q2[] = { 2, 5 };
    after = -7;
    if (coopinv::ConsumePartial(q2, 2, 4, &after) != 1 || after != 3) Fail("research: need 4 over 2,5 - the 2 goes whole, the 5 is lowered to 3");
    const int q3[] = { 2, 3 };
    if (coopinv::ConsumePartial(q3, 2, 5, &after) != -1) Fail("research: need 5 over 2,3 - both whole, nothing direct");
    if (coopinv::ConsumePartial(q3, 1, 2, &after) != -1) Fail("research: need equal to the stack - whole, watched");
    const int q4[] = { 2, 4, 1 };
    if (coopinv::ConsumePartial(q4, 3, 3, &after) != -2) Fail("research: a stack fetched after the lowered one disagrees with the engine loop");
    const int q5[] = { 2, 7 };
    if (coopinv::ConsumePartial(q5, 2, 2, &after) != -1) Fail("research: need met exactly, a trailing untouched stack - nothing lowered");
    if (coopinv::ConsumePartial(q1, 0, 2, &after) != -1) Fail("research: nothing fetched - nothing lowered");
    if (coopinv::ConsumePartial(q1, 1, 0, &after) != -1) Fail("research: need 0 - nothing lowered");
}
void t_inv2a_net_diff()
{
    std::vector<coopinv::Slot> base, cur;
    std::vector<coopinv::Op> ops;
    {   /* a hand-back answered with contents: this game's moves since the ask go on top of the holder's rows */
        std::vector<coopinv::Slot> h, b, n;
        std::vector<coopinv::MergeRow> out;
        h.push_back(InvSlot("main", 0, 0, "Dried Meat", 5, 1.0f));
        h.push_back(InvSlot("main", 1, 0, "First Aid Kit", 1, 40.0f));
        b.push_back(InvSlot("main", 0, 0, "Dried Meat", 5, 1.0f));
        /* no difference: the holder's rows as they are */
        n = b;
        if (coopinv::HandbackMerge(h, b, n, &out) != 0 || out.size() != 2 || out[0].from != 0 || out[0].qty != 5 || out[1].qty != 1)
            Fail("merge: no local difference must lay the holder's rows unchanged");
        /* add: the holder's rows are laid first, then a pack put in during the wait */
        n = b; n.push_back(InvSlot("main", 2, 0, "Pack", 1, 1.0f));
        if (coopinv::HandbackMerge(h, b, n, &out) != 1 || out.size() != 3 || out[0].from != 0 || out[0].qty != 5 || out[1].from != 0
            || out[1].qty != 1 || out[2].from != 1 || n[out[2].index].sid != "Pack" || out[2].qty != 1)
            Fail("merge: the holder's rows are laid first and an item added during the wait after them");
        /* add to a stack: the holder's stack of the record grows */
        n.clear(); n.push_back(InvSlot("main", 0, 0, "Dried Meat", 8, 1.0f));
        if (coopinv::HandbackMerge(h, b, n, &out) != 1 || out.size() != 2 || out[0].from != 0 || out[0].qty != 8)
            Fail("merge: a stack grown during the wait must grow the holder's stack");
        /* remove: meat taken out during the wait leaves the holder's meat */
        n.clear();
        if (coopinv::HandbackMerge(h, b, n, &out) != 1 || out.size() != 1 || out[0].from != 0 || out[0].index != 1 || out[0].qty != 1)
            Fail("merge: an item taken during the wait must be taken from the holder's rows");
        /* remove, not present: the holder has no meat - nothing goes below zero, the rest stands */
        std::vector<coopinv::Slot> h2(1, InvSlot("main", 1, 0, "First Aid Kit", 1, 40.0f));
        if (coopinv::HandbackMerge(h2, b, n, &out) != 1 || out.size() != 1 || out[0].from != 0 || out[0].qty != 1)
            Fail("merge: taking a record the holder has not must take nothing");
        /* partial remove: 2 of 5 taken during the wait */
        n.clear(); n.push_back(InvSlot("main", 0, 0, "Dried Meat", 3, 1.0f));
        if (coopinv::HandbackMerge(h, b, n, &out) != 1 || out.size() != 2 || out[0].qty != 3 || out[1].qty != 1)
            Fail("merge: a part taken during the wait must lower the holder's stack");
        /* the merged set laid out before anything is removed: the holder's rows at their cells, then this game's at their own
           cell or the first free one (y outer, x inner) */
        {
            std::vector<coopinv::LaySec> secs(1);
            secs[0].name = "main"; secs[0].w = 4; secs[0].h = 2;
            std::vector<coopinv::LayItem> hl(1), ol(1), none;
            hl[0].section = "main"; hl[0].x = 0; hl[0].y = 0; hl[0].w = 2; hl[0].h = 2;
            ol[0].section = "main"; ol[0].x = 2; ol[0].y = 0; ol[0].w = 1; ol[0].h = 1;
            if (coopinv::HandbackLayout(secs, hl, &ol) != 1 || ol[0].x != 2 || ol[0].y != 0) Fail("layout: a free own cell must be kept");
            ol[0].x = 1; ol[0].y = 1;
            if (coopinv::HandbackLayout(secs, hl, &ol) != 1 || ol[0].x != 2 || ol[0].y != 0)
                Fail("layout: an own cell under a holder row must move to the first free cell");
            ol[0].x = 0; ol[0].y = 0; ol[0].w = 2; ol[0].h = 2;
            if (coopinv::HandbackLayout(secs, hl, &ol) != 1 || ol[0].x != 2 || ol[0].y != 0) Fail("layout: a 2x2 must find the free 2x2");
            ol.push_back(ol[0]);
            if (coopinv::HandbackLayout(secs, hl, &ol) != 0) Fail("layout: a set that does not fit must be refused whole");
            std::vector<coopinv::LayItem> hl2(2, hl[0]);
            if (coopinv::HandbackLayout(secs, hl2, &none) != 0) Fail("layout: overlapping holder rows must be refused");
            std::vector<coopinv::LayItem> other(1, hl[0]);
            other[0].section = "weapon";
            if (coopinv::HandbackLayout(secs, other, &none) != 0) Fail("layout: a section the box has not must be refused");
            std::vector<coopinv::LayItem> off(1, hl[0]);
            off[0].x = 3;
            if (coopinv::HandbackLayout(secs, off, &none) != 0) Fail("layout: a row off its grid must be refused");
        }
        /* a move made before the baseline was saved, taken back out of it */
        {
            std::vector<coopinv::Slot> bl;
            bl.push_back(InvSlot("main", 0, 0, "Dried Meat", 8, 1.0f));
            bl.push_back(InvSlot("main", 2, 0, "Pack", 1, 1.0f));
            if (coopinv::UnapplyOp(&bl, coopinv::kOpAdd, "main", 2, 0, "Pack", 1, 0) != 1 || bl.size() != 1) Fail("unapply: an add leaves the baseline");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpAdd, "main", 0, 0, "Katana", 1, 0) != 0) Fail("unapply: an add of another record stays");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpRemove, "main", 3, 0, "First Aid Kit", 2, 0) != 1 || bl.size() != 2 || bl[1].qty != 2
                || bl[1].sid != "First Aid Kit")
                Fail("unapply: a removed stack comes back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpRemove, "main", 0, 0, "Katana", 1, 0) != 0) Fail("unapply: a remove onto a taken cell cannot come back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpRemove, "main", 5, 0, "", 1, 0) != 0) Fail("unapply: a remove with no record cannot come back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpQty, "main", 0, 0, "Dried Meat", 8, 5) != 1 || bl[0].qty != 5) Fail("unapply: a count change goes back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpQty, "main", 0, 0, "Dried Meat", 5, 0) != 0) Fail("unapply: a count change with no count before cannot go back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpSplit, "main", 0, 0, "Dried Meat", 2, 0) != 1 || bl[0].qty != 7) Fail("unapply: a split's units go back");
            if (coopinv::UnapplyOp(&bl, coopinv::kOpSplit, "main", 9, 0, "Dried Meat", 2, 0) != 0) Fail("unapply: a split at an empty cell cannot go back");
        }
    }
    base.push_back(InvSlot("main", 0, 0, "Dried Meat", 5, 1.0f));
    base.push_back(InvSlot("main", 1, 0, "First Aid Kit", 1, 40.0f));
    base.push_back(InvSlot("weapon", 0, 0, "Katana", 1, 1.0f));
    /* identical -> nothing */
    cur = base;
    coopinv::Diff(base, cur, &ops);
    if (!ops.empty()) Fail("identical contents must produce no op");
    /* takeOneItemOnly: the source stack lowered by a raw write -> one absolute quantity op */
    cur[0].qty = 4;
    coopinv::Diff(base, cur, &ops);
    if (ops.size() != 1 || ops[0].kind != coopinv::kOpQty || ops[0].qtyFrom != 5 || ops[0].qtyTo != 4 || ops[0].chargesChanged != 0
        || ops[0].section != "main" || ops[0].x != 0 || ops[0].y != 0 || ops[0].sid != "Dried Meat")
        Fail("a lowered stack must be one op 2 5->4 at its slot: " + InvOpsText(ops));
    /* kit charges drained, nothing else -> uncarried charges op */
    cur = base; cur[1].charges = 12.5f;
    coopinv::Diff(base, cur, &ops);
    if (ops.size() != 1 || ops[0].kind != coopinv::kOpCharges || coopinv::Charges100(ops[0].chTo) != 1250)
        Fail("a charges-only change must be one kOpCharges: " + InvOpsText(ops));
    /* quantity AND charges -> the qty op carries the charges flag */
    cur = base; cur[0].qty = 3; cur[0].charges = 0.5f;
    coopinv::Diff(base, cur, &ops);
    if (ops.size() != 1 || ops[0].kind != coopinv::kOpQty || ops[0].chargesChanged != 1) Fail("qty+charges must be one op 2 flagged: " + InvOpsText(ops));
    /* tiny float noise under 0.01 is not a change */
    cur = base; cur[1].charges = 40.001f;
    coopinv::Diff(base, cur, &ops);
    if (!ops.empty()) Fail("charges noise under 0.01 must not be a catch");
    /* whole stack gone, new item appeared, a slot changed kind: removes first, then adds */
    cur = base;
    cur.erase(cur.begin());                                       /* Dried Meat gone */
    cur.push_back(InvSlot("main", 2, 0, "Bread", 2, 1.0f));      /* new */
    cur[1].sid = "Wakizashi";                                     /* weapon 0,0 changed kind */
    coopinv::Diff(base, cur, &ops);
    if (InvOpsText(ops) != "1:main:0,0:Dried Meat:5>0:100>100:0;1:weapon:0,0:Katana:1>0:100>100:0;0:main:2,0:Bread:0>2:100>100:0;0:weapon:0,0:Wakizashi:0>1:100>100:0;")
        Fail("remove/add order wrong: " + InvOpsText(ops));
    /* ORDER INDEPENDENCE: the same sets in other orders give the same ops */
    {
        const std::string want = InvOpsText(ops);
        std::vector<coopinv::Slot> b2(base.rbegin(), base.rend()), c2(cur.rbegin(), cur.rend());
        coopinv::Diff(b2, c2, &ops);
        if (InvOpsText(ops) != want) Fail("reversed inputs changed the ops: " + InvOpsText(ops));
        std::swap(c2[0], c2[2]);
        coopinv::Diff(b2, c2, &ops);
        if (InvOpsText(ops) != want) Fail("shuffled inputs changed the ops: " + InvOpsText(ops));
    }
    /* A NORMAL PUBLISH FOLLOWED -> NO CATCH: a split of 2 off the meat (op 3), the split landing at 3,0 (op 0), a merge into
       the kit's slot (op 2 absolute) and the katana dropped (op 1) - the baseline follows each op and then equals the live state */
    {
        std::vector<coopinv::Slot> b = base, c = base;
        c[0].qty = 3; c.push_back(InvSlot("main", 3, 0, "Dried Meat", 2, 1.0f));
        c[1].qty = 2;
        c.erase(c.begin() + 2);
        if (coopinv::ApplyOp(&b, coopinv::kOpSplit, "main", 0, 0, "", 2, 0.0f) != 1) Fail("split must follow");
        if (coopinv::ApplyOp(&b, coopinv::kOpAdd, "main", 3, 0, "Dried Meat", 2, 1.0f) != 1) Fail("add must follow");
        if (coopinv::ApplyOp(&b, coopinv::kOpQty, "main", 1, 0, "", 2, 0.0f) != 1) Fail("qty must follow");
        if (coopinv::ApplyOp(&b, coopinv::kOpRemove, "weapon", 0, 0, "", 1, 0.0f) != 1) Fail("remove must follow");
        coopinv::Diff(b, c, &ops);
        if (!ops.empty()) Fail("a followed normal publish must leave nothing to catch: " + InvOpsText(ops));
        if (coopinv::ApplyOp(&b, coopinv::kOpRemove, "weapon", 5, 5, "", 1, 0.0f) != 0) Fail("a remove of an unknown slot must report no follow");
        if (coopinv::ApplyOp(&b, coopinv::kOpSplit, "main", 3, 0, "", 2, 0.0f) != 1 || coopinv::FindSlot(b, "main", 3, 0) != -1)
            Fail("a split of the whole stack must empty the slot");
    }
    /* THE NET'S OWN CATCHES FOLDED IN -> the next visit catches nothing (never twice) */
    {
        std::vector<coopinv::Slot> b = base, c = base;
        c[0].qty = 4; c[1].charges = 3.0f; c.erase(c.begin() + 2); c.push_back(InvSlot("main", 4, 1, "Bread", 1, 1.0f));
        coopinv::Diff(b, c, &ops);
        if (ops.size() != 4) Fail("four differences expected: " + InvOpsText(ops));
        for (size_t i = 0; i < ops.size(); ++i) coopinv::ApplyCatch(&b, ops[i]);
        coopinv::Diff(b, c, &ops);
        if (!ops.empty()) Fail("caught differences folded into the baseline must not be caught again: " + InvOpsText(ops));
    }
    {
        volatile float z = 0.0f;
        const float nan = z / z;
        if (coopinv::Charges100(nan) != 0) Fail("NaN charges must read as 0");
    }
    if (coopinv::Charges100(1.0e9f) != 0 || coopinv::Charges100(-0.125f) != -13 || coopinv::Charges100(0.125f) != 13) Fail("Charges100 rounding");
}
void t_inv4_owner_writer()
{
    using namespace coopown;
    int why = -9, slot = -9, via = -9;
    /* the faction classes */
    if (OwnerClassOf(1, 0, -1, 1, 1) != kOwnMe) Fail("this game's player faction must read mine");
    if (OwnerClassOf(0, 1, -1, 0, 1) != kOwnPeer) Fail("a stand-in in the table must read another player's");
    if (OwnerClassOf(0, 0, 3, 0, 1) != kOwnPeer) Fail("a coop-p3 record with an empty stand-in table must read another player's");
    if (OwnerClassOf(0, 0, -2, 0, 1) != kOwnPeer) Fail("a legacy coop-peer record must read another player's");
    if (OwnerClassOf(0, 0, -1, 0, 1) != kOwnNone) Fail("an NPC faction must read none");
    if (OwnerClassOf(0, 0, -1, 1, 1) != kOwnShared) Fail("a player faction that is not mine must read shared");
    if (OwnerClassOf(0, 0, -3, 0, 1) != kOwnUnknown) Fail("an unreadable record must read unknown");
    /* mine: only with the notebook map showing my slot loaded - never from a local read. The rung has no player-count input:
       a game alone in the world gets exactly these answers, the ones a game with company gets. */
    if (OwnerRung(kOwnMe, 1, 1, &why) != kRungMine || why != kWhyMine) Fail("my box, the map shows me: mine (alone too - a lone host writes its own box in its own area)");
    if (OwnerRung(kOwnMe, 1, 0, &why) != kRungArea || why != kWhyAbsent) Fail("my box, the map does not show me yet: the area rule");
    if (OwnerRung(kOwnMe, 1, -1, &why) != kRungDefer || why != kWhyStale) Fail("my box, no fresh map: nobody writes (alone too - it holds back until the world server is back)");
    if (OwnerRung(kOwnMe, -1, 1, &why) != kRungDefer || why != kWhyUnresolved) Fail("my box, my slot unknown: nobody writes");
    /* another player online and holding (by the map) -> that game writes */
    if (OwnerRung(kOwnPeer, 3, 1, &why) != kRungHeld || why != kWhyHeld) Fail("owner loaded in the map: held");
    /* offline / far away, positively (a fresh map) -> the area rule */
    if (OwnerRung(kOwnPeer, 3, 0, &why) != kRungArea || why != kWhyAbsent) Fail("owner not loaded in a fresh map: area rule (a lone host holding the area writes an absent friend's box)");
    /* unresolved -> nobody writes */
    if (OwnerRung(kOwnPeer, -1, 1, &why) != kRungDefer || why != kWhyUnresolved) Fail("owner slot unknown: nobody writes");
    if (OwnerRung(kOwnPeer, 3, -1, &why) != kRungDefer || why != kWhyStale) Fail("no fresh map: nobody writes");
    if (OwnerRung(kOwnUnknown, 3, 1, &why) != kRungDefer || why != kWhyUnresolved) Fail("unknown owner: nobody writes");
    /* shared faction, nobody's, unreadable building -> the area rule */
    if (OwnerRung(kOwnShared, -1, 1, &why) != kRungArea || why != kWhyShared) Fail("shared faction: area rule");
    if (OwnerRung(kOwnNone, -1, 1, &why) != kRungArea || why != kWhyNone) Fail("nobody's box: area rule");
    if (OwnerRung(kOwnUnreadable, -1, 1, &why) != kRungArea || why != kWhyUnreadable) Fail("unreadable building: area rule");
    /* decision 2: inside another player's house -> the house owner, whoever placed it */
    if (PickOwner(1, kOwnPeer, 3, kOwnMe, -1, &slot, &via) != kOwnPeer || slot != 3 || via != 1) Fail("my chest in slot 3's house must be slot 3's");
    if (PickOwner(1, kOwnMe, 1, kOwnPeer, 3, &slot, &via) != kOwnMe || slot != 1 || via != 1) Fail("slot 3's chest in my house must be mine");
    if (OwnerRung(PickOwner(1, kOwnPeer, 3, kOwnMe, -1, &slot, &via), slot, 1, &why) != kRungHeld) Fail("inside the loaded owner's house: that game writes");
    if (PickOwner(1, kOwnUnknown, -1, kOwnMe, 1, &slot, &via) != kOwnUnknown) Fail("inside a house whose owner cannot be read: unknown");
    /* a standalone building, or one inside a building no player owns -> its own owner */
    if (PickOwner(0, kOwnNone, -1, kOwnMe, 1, &slot, &via) != kOwnMe || via != 0) Fail("standalone: its own owner");
    if (PickOwner(1, kOwnNone, -1, kOwnPeer, 2, &slot, &via) != kOwnPeer || slot != 2 || via != 0) Fail("inside an NPC building: its own owner");
    if (PickOwner(1, kOwnShared, -1, kOwnMe, 1, &slot, &via) != kOwnShared) Fail("inside a shared-faction house: shared (area rule)");
    if (WriterIs(kRungArea, 2) != 2 || WriterIs(kRungMine, 2) != kRungMine || WriterIs(kRungDefer, 1) != kRungDefer) Fail("WriterIs");
    /* inv4b 6: a record naming this game's own slot is mine, not another player's */
    if (OwnerClassOf(0, 0, 2, 0, 1, 2) != kOwnMe) Fail("a coop-p<my slot> record must read mine");
    if (OwnerClassOf(0, 0, 3, 0, 1, 2) != kOwnPeer) Fail("a coop-p<other slot> record must still read another player's");
    if (OwnerClassOf(0, 0, 2, 0, 1, -1) != kOwnPeer) Fail("with my slot unknown a coop-p record stays another player's");
    /* inv4b 1: a hand-back starts only on a real arrival, and not after a flap on the same building this game already wrote */
    if (HandbackStart(0.0, 1, -1, 0) != kHbNone || HandbackStart(0.0, 1, 1, 1) != kHbNone) Fail("a cleared stamp must not start a hand-back");
    if (HandbackStart(12.5, -1, -1, 0) != kHbNone) Fail("my slot unknown must not start a hand-back");
    if (HandbackStart(12.5, 1, -1, 0) != kHbAsk) Fail("a first real arrival must ask");
    if (HandbackStart(12.5, 1, 1, 1) != kHbSkip) Fail("a new stamp on the same building after a done hand-back must be skipped");
    if (HandbackStart(12.5, 1, 1, 0) != kHbAsk) Fail("a new stamp on a reloaded building must ask");
    if (HandbackStart(12.5, 1, 0, 1) != kHbAsk || HandbackStart(12.5, 1, 2, 1) != kHbAsk) Fail("a new stamp while a hand-back is unfinished must ask");
    /* a box this game placed itself while the area's stamp was the one it has now, with no hand-back record yet, is not asked about */
    if (HandbackMadeHereSkip(kHbAsk, 0, 1, 12.5, 12.5, 0) != 1) Fail("a first arrival on a box this game made itself must skip the hand-back");
    if (HandbackMadeHereSkip(kHbAsk, 0, 0, 12.5, 12.5, 0) != 0) Fail("a first arrival on a box this game did not make must still ask");
    if (HandbackMadeHereSkip(kHbAsk, 1, 1, 12.5, 12.5, 0) != 0) Fail("a box with a hand-back record follows that record");
    if (HandbackMadeHereSkip(kHbSkip, 0, 1, 12.5, 12.5, 0) != 0 || HandbackMadeHereSkip(kHbNone, 0, 1, 12.5, 12.5, 0) != 0)
        Fail("only an arrival that would ask is skipped");
    if (HandbackMadeHereSkip(kHbAsk, 0, 1, 12.5, 30.0, 0) != 0) Fail("a box placed before the area was left and loaded again must be asked about");
    if (HandbackMadeHereSkip(kHbAsk, 0, 1, 0.0, 0.0, 0) != 0) Fail("a placement with no stamp must be asked about");
    if (HandbackMadeHereSkip(kHbAsk, 0, 1, 12.5, 12.5, 1) != 0) Fail("a full hand-back table must not skip (nothing could record it)");
    /* a box this game last wrote by the area rule at the stamp it has now, in an area it holds now, with no hand-back record yet */
    if (HandbackAreaWroteSkip(kHbAsk, 0, 1, 12.5, 12.5, 1, 0) != 1) Fail("an arrival on a box this game wrote by the area rule, area held since and now, must skip");
    if (HandbackAreaWroteSkip(kHbAsk, 0, 0, 12.5, 12.5, 1, 0) != 0) Fail("a box this game did not write by the area rule must still ask");
    if (HandbackAreaWroteSkip(kHbAsk, 0, 1, 12.5, 30.0, 1, 0) != 0) Fail("a box written before the area was left and loaded again must be asked about");
    if (HandbackAreaWroteSkip(kHbAsk, 0, 1, 0.0, 0.0, 1, 0) != 0) Fail("a write with no stamp must be asked about");
    if (HandbackAreaWroteSkip(kHbAsk, 0, 1, 12.5, 12.5, 0, 0) != 0) Fail("an area another game holds now must be asked about");
    if (HandbackAreaWroteSkip(kHbAsk, 1, 1, 12.5, 12.5, 1, 0) != 0) Fail("a box with a hand-back record follows that record");
    if (HandbackAreaWroteSkip(kHbSkip, 0, 1, 12.5, 12.5, 1, 0) != 0 || HandbackAreaWroteSkip(kHbNone, 0, 1, 12.5, 12.5, 1, 0) != 0)
        Fail("only an arrival that would ask is skipped (area rule)");
    if (HandbackAreaWroteSkip(kHbAsk, 0, 1, 12.5, 12.5, 1, 1) != 0) Fail("a full hand-back table must not skip an area-rule box");
    /* a move rung before the baseline was saved is in it; one rung at or after the save is not; tickets wrap */
    if (HandbackMoveBeforeHold(5, 10) != 1 || HandbackMoveBeforeHold(10, 10) != 0 || HandbackMoveBeforeHold(11, 10) != 0)
        Fail("HandbackMoveBeforeHold");
    if (HandbackMoveBeforeHold(-1, 0) != 1 || HandbackMoveBeforeHold(0, -1) != 0) Fail("HandbackMoveBeforeHold must compare across the wrap");
    /* a merged swap: an item not at its own cell stops it before anything leaves; a remove that fails stops it, and what left goes back */
    if (HandbackSwapRemoveOutcome(0, 0) != kHbSwapNotRun || HandbackSwapRemoveOutcome(0, 2) != kHbSwapNotRun)
        Fail("a merged swap with an item not at its own cell must remove nothing");
    if (HandbackSwapRemoveOutcome(1, 0) != kHbSwapGo) Fail("a merged swap whose removes all worked must lay the set");
    if (HandbackSwapRemoveOutcome(1, 1) != kHbSwapPutBack || HandbackSwapRemoveOutcome(1, 3) != kHbSwapPutBack)
        Fail("a merged swap with a failed remove must lay nothing of the answer and put back what left");
    /* after a mismatch a plain swap is asked once more, a merged swap never */
    if (HandbackReaskAfterMismatch(0, 0) != 1 || HandbackReaskAfterMismatch(0, 1) != 0) Fail("a plain swap's mismatch is asked once more, once");
    if (HandbackReaskAfterMismatch(1, 0) != 0 || HandbackReaskAfterMismatch(1, 1) != 0) Fail("a merged swap's mismatch must not be asked again");
    /* a pull lets an answer over this game's box only while the box owes no rows */
    if (HandbackPullStands(0, 1) != 1 || HandbackPullStands(0, 0) != 0) Fail("a live pull of a box that owes nothing lets its answer in");
    if (HandbackPullStands(1, 1) != 0 || HandbackPullStands(1, 0) != 0) Fail("a box that owes rows must not take a pull's answer");
    /* a hand-back waits while asked (0) or while its contents are on the way (2); a done one (1) does not */
    if (HandbackWaiting(0) != 1 || HandbackWaiting(2) != 1 || HandbackWaiting(1) != 0) Fail("HandbackWaiting");
    /* inv4b 2: only a game that really wrote the box by the area rule, and still holds the area, hands it back */
    if (HandbackServes(1, 1, 1) != 1) Fail("the area writer must serve the owner's hand-back");
    if (HandbackServes(1, 0, 1) != 0) Fail("a game that never wrote the box must answer not holder");
    if (HandbackServes(1, 1, 0) != 0) Fail("a game that no longer holds the area must answer not holder");
    if (HandbackServes(0, 1, 1) != 0) Fail("only a box the owner now writes is handed back");
    /* inv4b 3: a box whose owner cannot be read here */
    /* no player-count input: a game alone in the world waits on exactly the boxes a game with company waits on */
    if (UnreadRung(kOwnUnreadable) != kRungDefer) Fail("a never-read box must wait");
    if (UnreadRung(kOwnMe) != kRungDefer || UnreadRung(kOwnPeer) != kRungDefer || UnreadRung(kOwnUnknown) != kRungDefer)
        Fail("a player box that cannot be read must wait (alone too)");
    if (UnreadRung(kOwnNone) != kRungArea || UnreadRung(kOwnShared) != kRungArea) Fail("a box known not player-owned keeps the area rule");
    /* inv4b 5 */
    if (HandbackEvictable(1) != 1 || HandbackEvictable(0) != 0 || HandbackEvictable(2) != 0) Fail("only done hand-backs may be evicted");
    /* inv4b 7: an answer is counted once; contents on the way are never released by a later answer */
    int counted = 0;
    if (HandbackAnswer(0, 1, &counted) != 2 || counted != 1) Fail("first contents answer: wait for the swap, counted");
    if (HandbackAnswer(2, 1, &counted) != 2 || counted != 0) Fail("a second contents answer while waiting must not count");
    if (HandbackAnswer(2, 0, &counted) != 2 || counted != 0) Fail("a later not-holder answer must not release waiting contents");
    if (HandbackAnswer(0, 0, &counted) != 1 || counted != 1) Fail("a not-holder answer: done, no swap");
    if (HandbackAnswer(1, 1, &counted) != 1 || counted != 0) Fail("a done hand-back stays done");
}
/* review-inv4 fold 6: THE TWO-GAME PROPERTY. Game A owns the box (slot 1), game B is slot 2, the link is up on both. Both games
   receive the same notebook map (truth: the owner loaded or not; the area holder nobody / A / B); each game's own view may be
   stale (the notebook down there), A may not know its own slot, B may know the owner by the table, by the record only, or not at
   all. For every combination AT MOST ONE game answers mine, and with everything resolved and the owner loaded, the owner writes. */
void t_inv4_one_writer_property()
{
    using namespace coopown;
    int cases = 0, bothMine = 0, ownerWrote = 0, areaWrote = 0;
    for (int truth = 0; truth < 2; ++truth)
    for (int holder = 0; holder < 3; ++holder)
    for (int aFresh = 0; aFresh < 2; ++aFresh)
    for (int bFresh = 0; bFresh < 2; ++bFresh)
    for (int aSlotKnown = 0; aSlotKnown < 2; ++aSlotKnown)
    for (int bKnows = 0; bKnows < 3; ++bKnows)   /* 0 record unreadable and not in the table, 1 by the table, 2 by the record only */
    for (int aRead = 0; aRead < 3; ++aRead)      /* inv4b 3: 0 the rung reads the owner, 1 cannot (never read), 2 cannot (last read mine) */
    for (int bRead = 0; bRead < 3; ++bRead)      /* ... on B: 2 = last read as B's class */
    {
        ++cases;
        int why = 0;
        const int aRung = (aRead == 0) ? OwnerRung(kOwnMe, aSlotKnown != 0 ? 1 : -1, aFresh != 0 ? truth : -1, &why)
                                       : UnreadRung(aRead == 1 ? kOwnUnreadable : kOwnMe);
        const int bCls = OwnerClassOf(0, bKnows == 1 ? 1 : 0, bKnows == 2 ? 1 : (bKnows == 0 ? -3 : -1), 0, 1);
        const int bRung = (bRead == 0) ? OwnerRung(bCls, bKnows != 0 ? 1 : -1, bFresh != 0 ? truth : -1, &why)
                                       : UnreadRung(bRead == 1 ? kOwnUnreadable : bCls);
        /* the area rule from the same map (3 = unowned); a stale view never reaches it for a player-owned box */
        const int aArea = (aFresh != 0) ? (holder == 1 ? kRungMine : holder == 2 ? kRungHeld : 3) : kRungDefer;
        const int bArea = (bFresh != 0) ? (holder == 2 ? kRungMine : holder == 1 ? kRungHeld : 3) : kRungDefer;
        const int a = WriterIs(aRung, aArea), b = WriterIs(bRung, bArea);
        if (a == kRungMine && b == kRungMine) ++bothMine;
        if (truth == 1 && aRead == 0 && bRead == 0 && aFresh != 0 && bFresh != 0 && aSlotKnown != 0 && bKnows != 0 && !(a == kRungMine && b == kRungHeld))
            Fail("owner loaded and resolved on both games: A must write and B must leave it to A");
        if ((a == kRungMine || b == kRungMine) && truth == 1 && aRung == kRungMine) ++ownerWrote;
        if ((a == kRungMine || b == kRungMine) && truth == 0) ++areaWrote;
    }
    if (cases != 1296) Fail("the sweep must cover 1296 combinations");
    if (bothMine != 0) Fail("two games answered mine for one player-owned box");
    if (ownerWrote == 0 || areaWrote == 0) Fail("vacuous sweep: the owner branch or the area branch never wrote");
}
void t_loot2c_route_decisions()
{
    if (coopres::LootRouteOutcome(0, 0) != coopres::kLootRouteFailed) Fail("nothing moved and nothing asked must be a failure");
    if (coopres::LootRouteOutcome(1, 0) != coopres::kLootRouteOwnPack) Fail("moved to the pack but not asked must leave the book in the own pack");
    if (coopres::LootRouteOutcome(1, 1) != coopres::kLootRouteAsked) Fail("a sent request must read as asked");
    if (coopres::LootConfirmOutcome(1, 0) != coopres::kLootRouteOwnPack || coopres::LootConfirmOutcome(1, 1) != coopres::kLootRouteOwnPack)
        Fail("a refusal must leave the book in the own pack");
    if (coopres::LootConfirmOutcome(0, 1) != coopres::kLootRouteDone) Fail("a confirmed request whose pack copy was removed must be done");
    if (coopres::LootConfirmOutcome(0, 0) != coopres::kLootRouteFailed) Fail("a confirmation that could not remove the pack copy must be a failure");
    if (coopres::LootLandingIsGive(0, 1, 0, 1) != 1) Fail("a watched ADD on a friend's character must be a give");
    if (coopres::LootLandingIsGive(1, 1, 0, 1) != 0 || coopres::LootLandingIsGive(0, 0, 0, 1) != 0
        || coopres::LootLandingIsGive(0, 1, 1, 1) != 0 || coopres::LootLandingIsGive(0, 1, 0, 0) != 0)
        Fail("only a watched op-0 ADD on a character this game does not own may be a give");
    coopres::LootRing r;
    coopres::LootRingClear(&r);
    coopres::LootRingPut(&r, 0, 1);
    if (coopres::LootRingHas(&r, 0) != 0 || coopres::LootRingTake(&r, 0) != 0) Fail("tag 0 must never be stored");
    coopres::LootRingPut(&r, 77, 2);
    if (coopres::LootRingHas(&r, 77) != 1 || coopres::LootRingTake(&r, 77) != 2 || coopres::LootRingHas(&r, 77) != 0 || coopres::LootRingTake(&r, 77) != 0)
        Fail("a tag must be found once and taken once");
    coopres::LootRingPut(&r, 5, 1);
    coopres::LootRingPut(&r, 5, 2);
    if (coopres::LootRingTake(&r, 5) != 2 || coopres::LootRingHas(&r, 5) != 0) Fail("a re-put tag must keep one entry with the newest value");
    for (int i = 0; i <= coopres::kLootRingCap; ++i) coopres::LootRingPut(&r, (unsigned long long)(1000 + i), 1);
    if (coopres::LootRingHas(&r, 1000) != 0 || coopres::LootRingHas(&r, 1001) != 1 || coopres::LootRingHas(&r, (unsigned long long)(1000 + coopres::kLootRingCap)) != 1)
        Fail("the ring must overwrite its oldest entry and keep the rest");
    /* fold (review-loot2c-restructure 3a/3b): no room is its own outcome, and the watch list's decisions */
    if (coopres::kLootRouteNoRoom == coopres::kLootRouteFailed || coopres::kLootRouteNoRoom == coopres::kLootRouteAsked
        || coopres::kLootRouteNoRoom == coopres::kLootRouteOwnPack || coopres::kLootRouteNoRoom == coopres::kLootRouteDone)
        Fail("no room must be its own outcome (the book waits; it is never a failure that loses it)");
    if (coopres::LootWatchAction(1, 0, 0, coopres::kWatchAtOtherBox) != coopres::kWatchLeave) Fail("a watched book on the cursor must be left alone until it lands");
    if (coopres::LootWatchAction(0, 1, 0, coopres::kWatchAtOtherBox) != coopres::kWatchLeave) Fail("a watched book with a request out must wait for its answer");
    if (coopres::LootWatchAction(0, 0, 1, coopres::kWatchAtNowhere) != coopres::kWatchSettle) Fail("a watched book the other games know where it is must settle");
    if (coopres::LootWatchAction(0, 0, 0, coopres::kWatchAtOwnChar) != coopres::kWatchSettle) Fail("a watched book in an own character's pack must settle");
    if (coopres::LootWatchAction(0, 0, 0, coopres::kWatchAtHeldBox) != coopres::kWatchPublish) Fail("a watched book in a held box must be published by this game");
    if (coopres::LootWatchAction(0, 0, 0, coopres::kWatchAtOtherBox) != coopres::kWatchRouteHolder) Fail("a watched book dropped into a box another game holds must be routed through the holder");
    if (coopres::LootWatchAction(0, 0, 0, coopres::kWatchAtFriend) != coopres::kWatchRouteGive) Fail("a watched book on a friend's character must be a give");
    if (coopres::LootWatchAction(0, 0, 0, coopres::kWatchAtNowhere) != coopres::kWatchWait) Fail("a watched book nowhere known (the ground) must wait, watched");
    if (coopres::LootWatchAbsorb(1, 0, 0) != 1 || coopres::LootWatchAbsorb(2, 0, 0) != 1) Fail("a remove / quantity change of a book the other games never had there must be absorbed");
    if (coopres::LootWatchAbsorb(0, 0, 0) != 0 || coopres::LootWatchAbsorb(1, 1, 0) != 0 || coopres::LootWatchAbsorb(1, 0, 1) != 0)
        Fail("a landing, a published book and a book with a request out must go on as ordinary moves");
}

/* inv5 phase 1: review-inv5p0 LOW - the key's number reader refuses (never wraps) a number outside a 32-bit long; the position
   a receiver rebuilds from a key's tenths names the same key (no floats ride on the wire); the long-sid hash fallback round-trips. */
void t_inv5_ground_key_p1()
{
    using namespace coopground;
    GroundKeyParts p;
    std::memset(&p, 0, sizeof p);
    if (GroundKeyParse("G|a@1,2@2147483647,0,0", &p) != 1 || p.x10 != 2147483647L) Fail("the largest 32-bit tenths must parse");
    if (GroundKeyParse("G|a@1,2@-2147483648,0,0", &p) != 1 || p.x10 != (-2147483647L - 1)) Fail("the smallest 32-bit tenths must parse");
    if (GroundKeyParse("G|a@1,2@2147483648,0,0", &p) != 0) Fail("a tenths number past 32 bits must be refused, not wrapped");
    if (GroundKeyParse("G|a@1,2@-2147483649,0,0", &p) != 0) Fail("a negative tenths number past 32 bits must be refused");
    if (GroundKeyParse("G|a@1,2@99999999999,0,0", &p) != 0) Fail("an 11-digit number must be refused");
    if (GroundKeyParse("G|a@4294967297,2@0,0,0", &p) != 0) Fail("a sector number past 32 bits must be refused");
    if (GroundKeyParse("G|a@1,2@0,0,4294967296", &p) != 0) Fail("a height past 32 bits must be refused");
    const float xs[10] = { 11190.25f, -13888.75f, 0.05f, -0.05f, 1520.5f, 123456.7f, -98765.4f, 0.0f, 250000.3f, -250000.9f };
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 10; ++j)
        {
            char k1[kGroundKeyCap], k2[kGroundKeyCap];
            GroundKeyParts a, b;
            float pos[3];
            if (GroundKeyCompose(k1, kGroundKeyCap, "2292-gamedata.base", 1, 2, xs[i], xs[j], xs[(i + j) % 10], &a) != 1) Fail("a plain key did not compose");
            GroundKeyPos(a, pos);
            if (GroundKeyCompose(k2, kGroundKeyCap, "2292-gamedata.base", 1, 2, pos[0], pos[1], pos[2], &b) != 1 || std::strcmp(k1, k2) != 0)
                Fail(std::string("the position rebuilt from a key named another key: ") + k1 + " vs " + k2);
        }
    char longSid[200];
    for (int i = 0; i < 150; ++i) longSid[i] = (char)('a' + (i % 26));
    longSid[150] = 0;
    char k3[kGroundKeyCap];
    GroundKeyParts h;
    std::memset(&h, 0, sizeof h);
    if (GroundKeyCompose(k3, kGroundKeyCap, longSid, 1, 2, 1.0f, 2.0f, 3.0f, &h) != 2 || h.sid[0] != '#') Fail("a 150-byte sid must compose hashed");
    if (GroundKeyParse(k3, &p) != 1 || !GroundKeySame(p, h)) Fail("a hashed key must parse back to the same key");
    if (kGroundReasonTag != 0x31444E47u || kGroundReasonNotHolder != 1 || kGroundReasonNotFound != 2) Fail("the GND1 reason block constants moved");
    if (kGroundReasonBusy != 3 || kGroundReasonUnreadable != 4) Fail("the inv5p1 fold reasons (3 busy, 4 unreadable) moved");
}

/* T-497: the area holder's ground word goes to every game but the one whose TAKE / PUT it served - the road per item road and
   requester key, and who the world server then delivers it to (A holds, B asked, C watches) */
void t_t497_ground_tell_except_requester()
{
    using namespace cooplive;
    const unsigned int bRelayed = RelayPeerId(1);
    ExceptPlan e = ItemExceptRoad(bRelayed, 1, kItemRoadLive, true);
    if (e.road != kAddrLive || e.route != (unsigned int)kRouteWorldExcept || e.target != 1u || e.why != kExceptWhyOk) Fail("a relayed requester on the world road: WORLD_EXCEPT its slot");
    e = ItemExceptRoad(bRelayed, 2, kItemRoadLive, true);
    if (e.road != kAddrLive || e.target != 1u) Fail("a relayed requester is excepted by its own slot, not the session peer's");
    e = ItemExceptRoad(77u, 2, kItemRoadLive, true);
    if (e.road != kAddrLive || e.route != (unsigned int)kRouteWorldExcept || e.target != 2u) Fail("a raw requester (the session peer) on the world road: WORLD_EXCEPT the session peer's slot");
    e = ItemExceptRoad(77u, -1, kItemRoadLive, true);
    if (e.road != kAddrNone || e.why != kExceptWhyNoSlot) Fail("a requester whose slot is not known must not be sent WORLD (it would get its own GONE)");
    e = ItemExceptRoad(bRelayed, 1, kItemRoadLive, false);
    if (e.road != kAddrNone || e.why != kExceptWhyNoRoad) Fail("the world road not ready: not sent");
    e = ItemExceptRoad(bRelayed, 1, kItemRoadNone, true);
    if (e.road != kAddrNone || e.why != kExceptWhyNoRoad) Fail("no item road: not sent");
    e = ItemExceptRoad(77u, 2, kItemRoadSession, true);
    if (e.road != kAddrNone || e.why != kExceptWhyOnlyThem) Fail("the session road with the session peer as the requester: nobody else to tell");
    e = ItemExceptRoad(RelayPeerId(2), 2, kItemRoadSession, true);
    if (e.road != kAddrNone || e.why != kExceptWhyOnlyThem) Fail("the session road, the requester relayed with the session peer's slot: nobody else to tell");
    e = ItemExceptRoad(RelayPeerId(5), 2, kItemRoadSession, true);
    if (e.road != kAddrSession || e.why != kExceptWhyOk) Fail("the session road, a third game asked: the session peer is told");
    if (ItemExceptRoad(RelayPeerId(70000u & 0xFFFFu), -1, kItemRoadLive, true).road != kAddrLive) Fail("a u16 slot is a slot");
    /* delivery: A (slot 0) holds, B (slot 1) took, C (slot 2) and D (slot 3) watch - C and D get it, B and A do not */
    std::vector<int> c; c.push_back(0); c.push_back(1); c.push_back(2); c.push_back(3);
    std::vector<size_t> d;
    e = ItemExceptRoad(RelayPeerId(1), -1, kItemRoadLive, true);
    if (LiveRouteDecide(e.route, e.target, 0, c, &d) != kLiveFwd || d.size() != 2 || d[0] != 2 || d[1] != 3) Fail("the GONE must reach every game but the holder and the taker");
    std::vector<int> two; two.push_back(0); two.push_back(1);
    if (LiveRouteDecide(e.route, e.target, 0, two, &d) != kLiveFwd || !d.empty()) Fail("two games: the taker is the only other game - nobody is told");
    /* the undo's ADD: only after a GONE, only once the item is back, only while this game holds the area */
    using namespace coopground;
    if (GroundUndoTell(1, 1, 1) != 1) Fail("an undone take told GONE, put back, still held: ADD to the others");
    if (GroundUndoTell(0, 1, 1) != 0) Fail("never told GONE: the others still show it - an ADD would make a second one");
    if (GroundUndoTell(1, 0, 1) != 0) Fail("not put back: nothing to announce");
    if (GroundUndoTell(1, 1, 0) != 0) Fail("no longer the holder: not this game's ground to announce");
    /* a relayed requester while the session peer's slot is not known: the session peer may be the requester - not sent */
    e = ItemExceptRoad(RelayPeerId(5), -1, kItemRoadSession, true);
    if (e.road != kAddrNone || e.why != kExceptWhyNoSlot) Fail("session road, relayed requester, session peer's slot unknown: must not be sent");
    /* told = really sent: nothing went out when the requester is the only other game */
    const ExceptPlan only = ItemExceptRoad(77u, 2, kItemRoadSession, true);
    if (ExceptToldBy(only, false) != 0 || ExceptToldBy(only, true) != 0) Fail("OnlyThem sends nothing - nobody was told GONE");
    const ExceptPlan live = ItemExceptRoad(RelayPeerId(1), -1, kItemRoadLive, true);
    if (ExceptToldBy(live, true) != 1 || ExceptToldBy(live, false) != 0) Fail("told only when the GONE really went out");
    /* the undo's ADD follows the GONE's stored road, route and excepted slot - and is held while that road is down */
    if (ExceptReplayWhy(kAddrLive, true, false) != kExceptWhyOk) Fail("a stored world-road plan with the world road up: sent");
    if (ExceptReplayWhy(kAddrLive, false, true) != kExceptWhyRoadChanged) Fail("a stored world-road plan with only the session link up: road changed, not sent");
    if (ExceptReplayWhy(kAddrSession, false, true) != kExceptWhyOk) Fail("a stored session plan with the session link up: sent");
    if (ExceptReplayWhy(kAddrSession, true, false) != kExceptWhyRoadChanged) Fail("a stored session plan with the session link down: road changed");
    if (ExceptReplayWhy(kAddrNone, true, true) != kExceptWhyNoRoad) Fail("a plan that never had a road: not sent");
    if (LiveRouteDecide(live.route, live.target, 0, c, &d) != kLiveFwd || d.size() != 2 || d[0] != 2 || d[1] != 3)
        Fail("the stored plan must reach the same games as the GONE did (not the taker)");
}

/* T-497: a receiver's short memory of ground keys it added / removed - a repeat ADD keeps the copy it has, a repeat GONE never
   removes a neighbour; the window, the renewal, the forget on the opposite event and the overwrite past the cap */
void t_t497_ground_recent_keys()
{
    using namespace coopground;
    static GroundRecentKeys r;
    GroundRecentClear(&r);
    const char* k = "G|42313-changes_otto.mod@43,11@538271,-945911,10525";
    if (GroundRecentHas(r, k, 1000) != 0) Fail("an empty memory has nothing");
    GroundRecentNote(&r, k, 1000);
    if (GroundRecentHas(r, k, 1000 + kGroundRecentMs - 1) != 1) Fail("a key noted is remembered inside the window");
    if (GroundRecentHas(r, k, 1000 + kGroundRecentMs) != 0) Fail("a key is forgotten when the window has passed");
    if (GroundRecentHas(r, "G|42313-changes_otto.mod@43,11@538271,-945911,10511", 1001) != 0) Fail("only the EXACT key counts");
    GroundRecentNote(&r, k, 0xFFFFFF00u);
    if (GroundRecentHas(r, k, 0x00000100u) != 1) Fail("the tick wrap is harmless");
    GroundRecentForget(&r, k);
    if (GroundRecentHas(r, k, 0x00000100u) != 0) Fail("a forgotten key is gone");
    GroundRecentClear(&r);
    char kk[64];
    for (int i = 0; i < kGroundRecentCap + 5; ++i)
    {
        std::sprintf(kk, "G|2292-gamedata.base@43,11@%d,0,0", i);
        GroundRecentNote(&r, kk, 5000u + (unsigned int)i);
    }
    if (r.overwritten != 5) Fail("past the cap the oldest live entries are overwritten and counted");
    std::sprintf(kk, "G|2292-gamedata.base@43,11@%d,0,0", 0);
    if (GroundRecentHas(r, kk, 5200u) != 0) Fail("the oldest entry is the one overwritten");
    std::sprintf(kk, "G|2292-gamedata.base@43,11@%d,0,0", kGroundRecentCap + 4);
    if (GroundRecentHas(r, kk, 5200u) != 1) Fail("the newest entry is kept");
    if (GroundRecentDecide(0, 1, 1, 0) != kGroundRecentKeepAdd) Fail("a repeat ADD whose item is still here: keep it, no second copy");
    if (GroundRecentDecide(0, 1, 0, 0) != kGroundRecentApply) Fail("a repeat ADD whose item is gone: build it");
    if (GroundRecentDecide(0, 0, 1, 0) != kGroundRecentApply) Fail("an ADD not seen recently: the ordinary adopt / build");
    if (GroundRecentDecide(1, 0, 0, 1) != kGroundRecentIgnoreGone) Fail("a repeat GONE: ignored, never a near match");
    if (GroundRecentDecide(1, 1, 1, 0) != kGroundRecentApply) Fail("a GONE not seen recently: applied");
}

/* inv5 phase 0 (P090): the ground key (design section 2) - its text, its round trip, the hash fallback, the refusals, the
   near-miss rule (0.5 units, sector not compared) and the bag match (quantity, then quality, then any). */
/* P2/P3/P4 (inv5 phase 2): the one drop road's decision for every engine road onto the ground */
/* T-931: a box window's drop is spilled before it leaves the inventory - it waits for the removal, is noted once, and is given up
   when the removal never comes */
void t_t931_spill_wait()
{
    using namespace coopground;
    if (GroundSpillWaitsRemoval(1, 1) != 1) Fail("a spilled item physical in the world but still flagged in an inventory must wait for its removal");
    if (GroundSpillWaitsRemoval(1, 0) != 0) Fail("an item already out of the inventory is noted at once (a destroyed box's contents)");
    if (GroundSpillWaitsRemoval(0, 1) != 0 || GroundSpillWaitsRemoval(0, 0) != 0) Fail("an item not physical in the world never waits");
    GroundSpillWait w = { 0, 0 };
    if (GroundSpillWaitTake(&w, 0x1000) != 0) Fail("nothing waiting: a removal must not note anything");
    if (GroundSpillWaitClear(&w) != 0) Fail("nothing waiting: nothing to give up");
    GroundSpillWaitPut(&w, 0x1000);
    if (w.set != 1) Fail("a spill must be remembered");
    if (GroundSpillWaitTake(&w, 0x2000) != 0 || w.set != 1) Fail("another item's removal must leave the waiting drop waiting");
    if (GroundSpillWaitTake(&w, 0) != 0 || w.set != 1) Fail("a null removal must leave the waiting drop waiting");
    if (GroundSpillWaitTake(&w, 0x1000) != 1 || w.set != 0) Fail("the waiting item's removal must note it and empty the slot");
    if (GroundSpillWaitTake(&w, 0x1000) != 0) Fail("a drop must never be noted twice");
    GroundSpillWaitPut(&w, 0x3000);
    if (GroundSpillWaitClear(&w) != 1 || w.set != 0) Fail("a drop whose removal never came must be given up once");
    if (GroundSpillWaitTake(&w, 0x3000) != 0) Fail("a given-up drop must not be noted by a later removal");
    GroundSpillWaitPut(&w, 0);
    if (w.set != 0) Fail("a null item must not wait");
}
void t_p2p3p4_ground_route()
{
    using namespace coopground;
    if (GroundNewItemRoute(1, 1, 0, 0, 0) != kGroundRouteAdd) Fail("the holder must announce its own drop (GROUND ADD)");
    if (GroundNewItemRoute(1, 0, 0, 0, 0) != kGroundRouteAdd) Fail("the holder must announce a copy character's drop too (GROUND ADD)");
    if (GroundNewItemRoute(0, 1, 0, 0, 0) != kGroundRoutePut) Fail("a non-holder's own drop / spill / refund / overflow must be PUT to the holder");
    if (GroundNewItemRoute(0, 0, 0, 0, 0) != kGroundRouteIgnore) Fail("a non-holder must leave a copy's drop alone (its owner's game drops it for real)");
    if (GroundNewItemRoute(1, 0, 1, 0, 0) != kGroundRouteRemoveLocal || GroundNewItemRoute(0, 0, 1, 0, 0) != kGroundRouteRemoveLocal
        || GroundNewItemRoute(1, 1, 1, 0, 0) != kGroundRouteRemoveLocal || GroundNewItemRoute(0, 1, 1, 0, 0) != kGroundRouteRemoveLocal)
        Fail("ANY copy's spill (during the mod's own apply or not) must be removed here on either game, never announced or PUT");
    if (GroundCharDropFrom(0, 1) != kGroundFromPlayer) Fail("a person's drop out of its own inventory is a player drop");
    if (GroundCharDropFrom(1, 1) != kGroundFromAnimal) Fail("an animal's drop out of its own inventory is an animal drop");
    if (GroundCharDropFrom(0, 0) != kGroundFromOverflow || GroundCharDropFrom(1, 0) != kGroundFromOverflow)
        Fail("an item dropped that was not in the dropper's inventory is the full-inventory overflow");
    if (GroundFromBuilding(kGroundFromSpill) != 1 || GroundFromBuilding(kGroundFromRefund) != 1) Fail("spills and refunds are building sources");
    if (GroundFromBuilding(kGroundFromPlayer) != 0 || GroundFromBuilding(kGroundFromAnimal) != 0 || GroundFromBuilding(kGroundFromOverflow) != 0)
        Fail("player, animal and overflow drops are character sources");
    if (GroundBuildingSource(0, 1, 0) != kGroundSrcOwn || GroundBuildingSource(0, 1, 1) != kGroundSrcOwn)
        Fail("this game's own piece stays own even while the mod applies help to it (a real refund / spill)");
    if (GroundBuildingSource(1, 0, 0) != kGroundSrcCopy || GroundBuildingSource(1, 1, 0) != kGroundSrcCopy) Fail("a copy outside the mod's own apply is a copy");
    if (GroundBuildingSource(1, 0, 1) != kGroundSrcCopyApplying) Fail("a copy inside the mod's own apply (a REMOVE) is removed-on-spill");
    /* ground5 fold 1 (F2): a piece neither a copy nor this game's own (an NPC's box, a town building, no owner) is a world source */
    if (GroundBuildingSource(0, 0, 0) != kGroundSrcWorld || GroundBuildingSource(0, 0, 1) != kGroundSrcWorld)
        Fail("an NPC / world building must be a world source, not this game's own");
    if (GroundNewItemRoute(1, 0, 0, 1, 0) != kGroundRouteAdd) Fail("the holder must announce its own spill of an NPC / world building");
    /* ground5 fold 2 (the re-check of fold 1, HIGH a/b): a non-holder removes a world spill ONLY when the holder spills it too */
    if (GroundNewItemRoute(0, 0, 0, 1, 1) != kGroundRouteRemoveLocal) Fail("a non-holder must remove a world spill the holder also makes (the holder's wins)");
    if (GroundNewItemRoute(0, 0, 0, 1, 0) != kGroundRoutePut) Fail("a world spill / refund only this game made must be PUT with no taker, never removed (a loss)");
    /* ground5 fold 1 (F3): ANY copy's spill is removed here; (F5) an items apply marks only a copy's spill */
    if (GroundSrcRemovedHere(kGroundSrcCopy) != 1 || GroundSrcRemovedHere(kGroundSrcCopyApplying) != 1) Fail("a copy's spill must be removed here, in the mod's apply or not");
    if (GroundSrcRemovedHere(kGroundSrcOwn) != 0 || GroundSrcRemovedHere(kGroundSrcWorld) != 0) Fail("an own or NPC building's spill is not a copy's");
    if (GroundSpillSource(kGroundSrcCopy, 1) != kGroundSrcCopyApplying || GroundSpillSource(kGroundSrcCopy, 0) != kGroundSrcCopy)
        Fail("an items apply marks a copy's spill as the mod's own");
    if (GroundSpillSource(kGroundSrcOwn, 1) != kGroundSrcOwn || GroundSpillSource(kGroundSrcWorld, 1) != kGroundSrcWorld)
        Fail("an items apply must NOT mark this game's own box (or an NPC's) as a copy's spill");
    /* the detour's whole decision for this game's own box spilled on ground another game holds: PUT (with no taker) */
    {
        const int src = GroundSpillSource(GroundBuildingSource(0, 1, 0), 1);
        if (GroundNewItemRoute(0, src == kGroundSrcOwn ? 1 : 0, GroundSrcRemovedHere(src), src == kGroundSrcWorld ? 1 : 0, 0) != kGroundRoutePut)
            Fail("this game's own box spilled during an items apply on ground another game holds must be PUT");
    }
    /* ground5 fold 1 (F1): a ground PUT naming no taker skips the taker check; a TAKE, or a PUT naming a taker, keeps it */
    if (GroundPutNamesNoTaker(1, 0) != 1) Fail("a ground PUT with no taker (a building's spill / refund) must go to the area test");
    if (GroundPutNamesNoTaker(1, 77) != 0 || GroundPutNamesNoTaker(0, 0) != 0 || GroundPutNamesNoTaker(0, 77) != 0)
        Fail("a ground request naming a taker, or a TAKE, must keep the taker check");
    /* ground5 fold 2 (the re-check of fold 1, HIGH a/b): the area verdict with the world source - removed ONLY on ground another game
       holds, from the spill road, while the engine takes the piece apart; unowned / no answer / a drag / a refund are PUT, mine is ADD */
    {
        const int areas[4] = { kGroundAreaMine, kGroundAreaHeld, kGroundAreaUnowned, kGroundAreaNoAnswer };
        for (int a = 0; a < 4; ++a)
            for (int fs = 0; fs < 2; ++fs)
                for (int ap = 0; ap < 2; ++ap)
                {
                    const int r = GroundDropRoute(areas[a], 0, 0, 1, fs, ap);
                    const int want = (areas[a] == kGroundAreaMine) ? kGroundRouteAdd
                                   : ((areas[a] == kGroundAreaHeld && fs != 0 && ap != 0) ? kGroundRouteRemoveLocal : kGroundRoutePut);
                    if (r != want) Fail("a world spill / refund must follow the verdict: held + spill road + taken apart = removed, mine = ADD, anything else = PUT");
                }
        if (GroundDropRoute(kGroundAreaUnowned, 0, 0, 1, 1, 1) != kGroundRoutePut) Fail("a world piece taken apart on ground nobody holds must be PUT, never removed (nobody announces it)");
        if (GroundDropRoute(kGroundAreaNoAnswer, 0, 0, 1, 1, 1) != kGroundRoutePut) Fail("a world piece taken apart with no area answer must be PUT, never removed");
        if (GroundDropRoute(kGroundAreaHeld, 0, 1, 0, 1, 0) != kGroundRouteRemoveLocal || GroundDropRoute(kGroundAreaUnowned, 0, 1, 0, 0, 0) != kGroundRouteRemoveLocal)
            Fail("a copy's spill is removed here whatever the verdict");
        if (GroundDropRoute(kGroundAreaHeld, 1, 0, 0, 0, 0) != kGroundRoutePut || GroundDropRoute(kGroundAreaUnowned, 1, 0, 0, 1, 1) != kGroundRoutePut)
            Fail("this game's own drop / spill / refund on ground it does not hold is PUT");
        if (GroundDropRoute(kGroundAreaHeld, 0, 0, 0, 0, 0) != kGroundRouteIgnore) Fail("a copy character's drop on ground another game holds is left alone");
    }
    /* ground5 fold 2 (T629): the drop road's address match compares x and z only - a refund placed at its piece's height landed lower */
    {
        GroundKeyParts at, landed, other;
        char k[kGroundKeyCap];
        if (GroundKeyCompose(k, kGroundKeyCap, "580-gamedata.base", 20, 32, -50941.3f, 3065.5f, 2939.0f, &at) != 1
            || GroundKeyCompose(k, kGroundKeyCap, "580-gamedata.base", 20, 32, -50941.3f, 1532.9f, 2939.0f, &landed) != 1
            || GroundKeyCompose(k, kGroundKeyCap, "581-gamedata.base", 20, 32, -50941.3f, 1532.9f, 2939.0f, &other) != 1)
            Fail("the T629 refund's ground keys must compose");
        if (GroundKeyDist2XZ(at, landed) != 0) Fail("a fall changes only the height - the horizontal distance must be 0");
        if (GroundKeyDist2(at, landed) <= 50LL * 50LL) Fail("(control) the T629 fall is far past the old 5-unit 3-axis window");
        if (GroundKeyDist2XZ(at, other) != -1) Fail("another sid is never the same item");
    }
}

void t_inv5_ground_key()
{
    using namespace coopground;
    char k[kGroundKeyCap];
    GroundKeyParts p, q;
    std::memset(&p, 0, sizeof p); std::memset(&q, 0, sizeof q);
    if (GroundKeyCompose(k, kGroundKeyCap, "2292-gamedata.base", 33, 29, 11190.25f, 1520.5f, -13888.75f, &p) != 1) Fail("a plain key did not compose");
    if (std::strcmp(k, "G|2292-gamedata.base@33,29@111903,-138888,15205") != 0) Fail(std::string("key text is ") + k);
    if (GroundKeyParse(k, &q) != 1) Fail("a composed key did not parse");
    if (!GroundKeySame(p, q)) Fail("parse(compose(x)) is not the same key");
    if (q.sx != 33 || q.sy != 29 || q.x10 != 111903 || q.z10 != -138888 || q.y10 != 15205) Fail("parsed pieces differ");
    /* the same floats compose the same bytes (the property both games rely on) */
    char k2[kGroundKeyCap];
    GroundKeyCompose(k2, kGroundKeyCap, "2292-gamedata.base", 33, 29, 11190.25f, 1520.5f, -13888.75f, 0);
    if (std::strcmp(k, k2) != 0) Fail("the same inputs composed two different keys");
    /* refusals */
    volatile float big = 1.0e30f;   /* squared at run time: an infinity the compiler does not fold */
    if (GroundKeyCompose(k, kGroundKeyCap, "", 1, 1, 1.0f, 1.0f, 1.0f, 0) != 0) Fail("an empty sid composed");
    if (GroundKeyCompose(k, kGroundKeyCap, "a", 1, 1, big * big, 1.0f, 1.0f, 0) != 0) Fail("an infinite position composed");
    if (GroundKeyCompose(k, kGroundKeyCap, "a", 1, 1, 2.0e7f, 1.0f, 1.0f, 0) != 0) Fail("an implausible position composed");
    if (GroundKeyCompose(k, 8, "abc", 1, 1, 1.0f, 1.0f, 1.0f, 0) != 0 || k[0] != 0) Fail("a key that cannot fit composed or left text");
    /* the hash fallback for a sid too long for the buffer */
    std::string longSid(130, 'x');
    char kh[kGroundKeyCap];
    char tiny[4];
    if (GroundKeyCompose(kh, kGroundKeyCap, longSid.c_str(), 2, 3, 10.0f, 0.0f, 20.0f, &p) != 2) Fail("a too-long sid was not hashed");
    if (std::strncmp(kh, "G|#", 3) != 0) Fail(std::string("the hashed key does not start G|# : ") + kh);
    if (GroundKeyParse(kh, &q) != 1 || !GroundKeySame(p, q)) Fail("a hashed key did not round-trip");
    std::string midSid(100, 'm');   /* fits the key buffer but not the parts: hashed too, so it still parses back */
    if (GroundKeyCompose(tiny, 0, "", 0, 0, 0, 0, 0, 0) != 0) Fail("cap 0 composed");
    if (GroundKeyCompose(kh, kGroundKeyCap, midSid.c_str(), 2, 3, 10.0f, 0.0f, 20.0f, &p) != 2 || GroundKeyParse(kh, &q) != 1) Fail("a 100-char sid did not hash and parse back");
    GroundKeyCompose(kh, kGroundKeyCap, longSid.c_str(), 2, 3, 10.0f, 0.0f, 20.0f, &p);
    std::string otherLong(130, 'y');
    char kh2[kGroundKeyCap];
    GroundKeyCompose(kh2, kGroundKeyCap, otherLong.c_str(), 2, 3, 10.0f, 0.0f, 20.0f, 0);
    if (std::strcmp(kh, kh2) == 0) Fail("two different long sids hashed to one key");
    /* malformed keys */
    if (GroundKeyParse("X|a@1,2@3,4,5", &q) != 0) Fail("a key without the G| prefix parsed");
    if (GroundKeyParse("G|@1,2@3,4,5", &q) != 0) Fail("a key with an empty sid parsed");
    if (GroundKeyParse("G|a@1,2@3,4", &q) != 0) Fail("a key with two coordinates parsed");
    if (GroundKeyParse("G|a@1,2@3,4,5x", &q) != 0) Fail("a key with trailing text parsed");
    if (GroundKeyParse("G|a@1,2@-3,-4,-5", &q) != 1 || q.x10 != -3 || q.z10 != -4 || q.y10 != -5) Fail("negative components did not parse");
    /* rounding: half away from zero, and -0.04 is 0 */
    int ok = 0;
    if (GroundTenths(0.25f, &ok) != 3 || !ok) Fail("0.25 did not round to 3 tenths");
    if (GroundTenths(-0.25f, &ok) != -3) Fail("-0.25 did not round to -3 tenths");
    if (GroundTenths(-0.04f, &ok) != 0) Fail("-0.04 did not round to 0 tenths");
    /* near miss: same sid within 0.5 units, the sector NOT compared; an identical key is not a near miss */
    GroundKeyParts a, b;
    std::memset(&a, 0, sizeof a); std::memset(&b, 0, sizeof b);
    GroundKeyCompose(k, kGroundKeyCap, "s", 10, 10, 100.0f, 0.0f, 100.0f, &a);
    GroundKeyCompose(k, kGroundKeyCap, "s", 10, 10, 100.25f, 0.0f, 100.25f, &b);
    if (GroundKeyNear(a, b) != 1) Fail("0.35 units apart is not a near miss");
    GroundKeyCompose(k, kGroundKeyCap, "s", 10, 10, 100.5f, 0.0f, 100.5f, &b);
    if (GroundKeyNear(a, b) != 0) Fail("0.7 units apart counted as a near miss");
    GroundKeyCompose(k, kGroundKeyCap, "s", 11, 10, 100.25f, 0.0f, 100.0f, &b);
    if (GroundKeyNear(a, b) != 1) Fail("a near item across a sector line is not a near miss");
    GroundKeyCompose(k, kGroundKeyCap, "t", 10, 10, 100.0f, 0.0f, 100.0f, &b);
    if (GroundKeyNear(a, b) != 0 || GroundKeyDist2(a, b) != -1) Fail("a different sid counted as near");
    if (GroundKeyNear(a, a) != 0) Fail("an identical key counted as a near miss");
    /* bag match under one key */
    const int qty[3] = { 3, 1, 1 };
    const int q100[3] = { 50, 40, 60 };
    if (GroundBagPick(qty, q100, 0, 3, 1, 60) != 2) Fail("bag: quantity+quality did not win");
    if (GroundBagPick(qty, q100, 0, 3, 1, 99) != 1) Fail("bag: quantity did not come before quality");
    if (GroundBagPick(qty, q100, 0, 3, 7, 40) != 1) Fail("bag: quality did not come before any");
    if (GroundBagPick(qty, q100, 0, 3, 7, 99) != 0) Fail("bag: 'any' is not the first");
    const int taken[3] = { 1, 0, 0 };
    if (GroundBagPick(qty, q100, taken, 3, 3, 50) != 1) Fail("bag: a taken candidate was matched");
    if (GroundBagPick(qty, q100, 0, 0, 3, 50) != -1) Fail("bag: an empty bag matched");
    if (GroundQ100(0.25f) != 25 || GroundQ100(1.0f) != 100) Fail("quality hundredths");
}

/* give1: a late ok answer to a GIVE removes the giver's copy; cancelled sales / purchases settle the purse back; the TEST lever's
   parse; the pack digest sees quality, charges, function, level and unique. */
/* an item's uniform and crafter on the wire (src/common/gearwire.h) - the GEM1 block, the GEML list tail, their places in
   the ADD tail, the parity box, bag rows, the kit's details and capture rows, and the journal's b3 fields. */
void GearTabFields(const std::string& s, std::vector<std::string>* f)
{
    f->clear();
    std::string::size_type at = 0;
    for (;;)
    {
        const std::string::size_type t = s.find('\t', at);
        if (t == std::string::npos) { f->push_back(s.substr(at)); return; }
        f->push_back(s.substr(at, t - at)); at = t + 1;
    }
}
/* carrycheck.h: the sums carryread prints before and after RESET WORLD - order-free and exact - and both levers' argument shapes */
void t_carry_sums()
{
    using namespace swcarry;
    std::vector<std::string> v;
    if (CarrySum(v) != 0x811C9DC5u) Fail("carry: the empty set's sum is the FNV-1a basis");
    v.push_back("a");
    if (CarrySum(v) != 0x2524C6D2u) Fail("carry: FNV-1a 32 of 'a' and a line break");
    v.push_back("b");
    if (CarrySum(v) != 0x7A256DEEu) Fail("carry: two lines");
    std::vector<std::string> r; r.push_back("b"); r.push_back("a");
    if (CarrySum(r) != CarrySum(v)) Fail("carry: the order of the lines does not move the sum");
    const std::vector<std::string> one(1, "ab");
    if (CarrySum(one) != 0x31480D40u || CarrySum(one) == CarrySum(v)) Fail("carry: the line break is part of the sum");
    v.push_back("b");
    if (CarrySum(v) != 0x9CA6B62Au) Fail("carry: a line held twice counts twice");
    if (CarryHex(0xABCDu) != "0000abcd" || CarryHex(0x811C9DC5u) != "811c9dc5") Fail("carry: the sum as eight hex digits");
    if (CarryFloatBits(100.0f) != 0x42C80000u || CarryFloatBits(-20.0f) != 0xC1A00000u) Fail("carry: a float's bits");
    if (CarryFloatBits(-0.0f) == CarryFloatBits(0.0f)) Fail("carry: -0 and 0 sum apart");
    if (CarryFloatText(0.5f) != "0.5" || CarryFloatText(-20.0f) != "-20") Fail("carry: the readable float");
    if (CarryMapLine("123-gamedata.base", 1, 0) != "123-gamedata.base d1 e0") Fail("carry: the map line");
    CarryFac f; f.sid = "17-x.mod"; f.rel = 100.0f; f.relBack = -20.0f; f.trust = 0.5f; f.flags = 1; f.flagsBack = 2;
    if (CarryFacSumLine(f) != "17-x.mod 42c80000 3f000000 00000000 f1 | c1a00000 00000000 00000000 f2") Fail("carry: the standings sum line");
    if (CarryFacText(f) != "17-x.mod mine->it rel=100 trust=0.5 trustNeg=0 flags=1 | it->mine rel=-20 trust=0 trustNeg=0 flags=2")
        Fail("carry: the standings row text");
    CarryFac z; z.sid = "q";
    if (CarryFacNonZero(z) != 0 || CarryFacNonZero(f) != 1) Fail("carry: a row with nothing set is not printed on its own");
    z.trustNegBack = 0.25f;
    if (CarryFacNonZero(z) != 1) Fail("carry: any one value set prints the row");
    CarryFac g = f; g.trust = 0.50000006f;   /* the next float above 0.5 */
    const std::vector<std::string> a1(1, CarryFacSumLine(f)), a2(1, CarryFacSumLine(g));
    if (CarrySum(a1) == CarrySum(a2)) Fail("carry: two standings one float step apart sum apart");
    if (CarryList(r) != "[a, b]" || CarryList(std::vector<std::string>()) != "[]") Fail("carry: a short set is listed, sorted");
    if (CarryList(std::vector<std::string>(13, "x")) != "(13, not listed)") Fail("carry: a long set is counted, not listed");
    if (CarryList(std::vector<std::string>(12, "x")).substr(0, 5) != "[x, x") Fail("carry: twelve lines are still listed");

    std::string tag = "keep"; int full = 7;
    if (CarryReadParse("", &tag, &full) != 1 || tag != "-" || full != 0) Fail("carryread: no argument");
    if (CarryReadParse(" before ", &tag, &full) != 1 || tag != "before" || full != 0) Fail("carryread before");
    if (CarryReadParse("full after-1", &tag, &full) != 1 || tag != "after-1" || full != 1) Fail("carryread full after-1");
    if (CarryReadParse("after full", &tag, &full) != 1 || tag != "after" || full != 1) Fail("carryread after full");
    tag = "keep"; full = 7;
    const char* const badRead[] = { "a b", "full full", "bad!tag", "123456789012345678901234567890123" };
    for (size_t k = 0; k < sizeof(badRead) / sizeof(badRead[0]); ++k)
        if (CarryReadParse(badRead[k], &tag, &full) != 0) Fail(std::string("carryread must refuse '") + badRead[k] + "'");
    if (tag != "keep" || full != 7) Fail("carryread: a refused shape leaves the outputs untouched");
    if (CarryReadParse("12345678901234567890123456789012", &tag, &full) != 1) Fail("carryread: a 32-character tag");

    std::string t = "keep"; int ex = 7;
    if (CarrySetParse("research any", &t, &ex) != kCarrySetResearch || t != "any" || ex != 0) Fail("carryset research any");
    if (CarrySetParse("research 1234-gamedata.base", &t, &ex) != kCarrySetResearch || t != "1234-gamedata.base") Fail("carryset research <sid>");
    if (CarrySetParse("map any explored", &t, &ex) != kCarrySetMap || t != "any" || ex != 1) Fail("carryset map any explored");
    if (CarrySetParse("map  The   Hub ", &t, &ex) != kCarrySetMap || t != "The Hub" || ex != 0) Fail("carryset map <name with blanks>");
    if (CarrySetParse("standing Holy Nation", &t, &ex) != kCarrySetStanding || t != "Holy Nation" || ex != 0) Fail("carryset standing <name>");
    t = "keep"; ex = 7;
    const char* const badSet[] = { "", "research", "research a b", "map", "standing", "relation x", "Research any" };
    for (size_t k = 0; k < sizeof(badSet) / sizeof(badSet[0]); ++k)
        if (CarrySetParse(badSet[k], &t, &ex) != kCarrySetBad) Fail(std::string("carryset must refuse '") + badSet[k] + "'");
    if (t != "keep" || ex != 7) Fail("carryset: a refused shape leaves the outputs untouched");
}
void t_t832_item_tail()
{
    /* a gear item that is not a pack and has no owner: the trade block, then a GEM1 block straight after it */
    gearwire::GearMarks g; g.uniformSid = "@slot:1:Paladins";
    std::vector<char> t;
    t.push_back((char)0);              /* trade kind 0 */
    gearwire::GearPutU32(&t, 0u);      /* price 0 */
    const size_t gemAt = t.size();
    if (gearwire::EncodeGear(&t, g, 0) != 1) { Fail("t832: the GEM1 block must encode"); return; }
    const char* p = &t[0];
    const size_t n = t.size();
    if (itemtail::ReqBagNext(p, n, gemAt)) Fail("t832: a request's GEM1 after the trade block must not be read as backpack rows");
    if (itemtail::CfBagNext(p, n, gemAt)) Fail("t832: a confirm's GEM1 after the trade block must not be read as backpack rows");
    if (itemtail::ReqOwnerNext(p, n, gemAt)) Fail("t832: a request's GEM1 after the trade block must not be read as an owner block");
    if (itemtail::CfOwnerNext(p, n, gemAt)) Fail("t832: a confirm's GEM1 after the trade block must not be read as an owner block");
    gearwire::GearMarks back; size_t end = 0;
    if (!gearwire::GearBlockAt(p, n, gemAt) || gearwire::DecodeGear(p, n, gemAt, &back, &end) != gearwire::kGearOk || end != n
        || !gearwire::GearEqual(back, g)) Fail("t832: the GEM1 block after the trade block must read whole, to the end of the message");
    std::vector<coopbag::BagRow> rows; size_t bagEnd = 0;
    if (coopbag::DecodeBagRows(p, n, gemAt, &rows, &bagEnd) == coopbag::kBagOk) Fail("t832: GEM1 bytes are not backpack rows (the malformed refusal)");
    /* at the end of the message no step reads anything */
    if (itemtail::ReqBagNext(p, n, n) || itemtail::CfBagNext(p, n, n) || itemtail::ReqOwnerNext(p, n, n) || itemtail::CfOwnerNext(p, n, n))
        Fail("t832: no block is read past the end of the message");
    /* a real BAG1 block after the trade block is still read as backpack rows by both readers */
    std::vector<coopbag::BagRow> one(1);
    one[0].section = "backpack_content"; one[0].baseSid = "684-gamedata.base"; one[0].qty = 1;
    std::vector<char> b(t.begin(), t.begin() + (std::ptrdiff_t)gemAt);
    if (!coopbag::EncodeBagRows(&b, one)) { Fail("t832: the BAG1 block must encode"); return; }
    if (!itemtail::ReqBagNext(&b[0], b.size(), gemAt) || !itemtail::CfBagNext(&b[0], b.size(), gemAt))
        Fail("t832: a BAG1 block after the trade block is read as backpack rows");
    if (coopbag::DecodeBagRows(&b[0], b.size(), gemAt, &rows, &bagEnd) != coopbag::kBagOk || bagEnd != b.size() || rows.size() != 1)
        Fail("t832: the BAG1 block after the trade block reads whole");
}

/* inv6 phase 1: the stolen mark's codec (src/common/ownerwire.h) - the OWN1 block, its place after BAG1, the journal's b2 fields,
   the D5-2 choice, and the gated tombstone / phase-2 digest term. */
void t_inv6_owner_wire()
{
    using namespace coopmark;
    std::vector<char> b;
    OwnerId clean;
    if (!EncodeOwner(&b, clean) || !b.empty()) Fail("a clean owner must encode as NOTHING (protocol 70's bytes for a clean item)");
    if (DecodeOwner(0, 0, 0, 0, 0) != kOwnAbsent) Fail("no bytes is absent");

    const char* sids[3] = { "@slot:1:Wanderers", "HolyNation", "" };
    for (int k = 1; k <= 3; ++k)
    {
        OwnerId o; o.kind = (unsigned char)k; o.uid = 1000u + (unsigned int)k; o.factionSid = sids[k - 1];
        std::vector<char> e;
        if (!EncodeOwner(&e, o)) { Fail("a valid owner must encode"); continue; }
        OwnerId back; size_t end = 0;
        if (DecodeOwner(&e[0], e.size(), 0, &back, &end) != kOwnOk || end != e.size() || !OwnerEqual(back, o)) Fail("owner round trip");
        for (size_t cut = 4; cut < e.size(); ++cut)
            if (DecodeOwner(&e[0], cut, 0, &back, &end) != kOwnBad) { Fail("a cut owner block must be malformed, never read short"); break; }
        for (size_t cut = 1; cut < 4; ++cut)
            if (DecodeOwner(&e[0], cut, 0, &back, &end) != kOwnAbsent) { Fail("fewer than four bytes is not an owner block"); break; }
    }
    {
        OwnerId o; o.kind = 4; o.uid = 1; o.factionSid = "x";
        std::vector<char> e;
        if (EncodeOwner(&e, o) || !e.empty()) Fail("kind 4 must not encode");
        o.kind = 1; o.factionSid = "a\tb";
        if (EncodeOwner(&e, o) || !e.empty()) Fail("a sid with a tab must not encode (it rides a journal line)");
        o.factionSid = std::string(96, 'a');
        if (EncodeOwner(&e, o) || !e.empty()) Fail("a sid over 95 bytes must not encode");
        o.factionSid = std::string(95, 'a');
        if (!EncodeOwner(&e, o)) Fail("a 95-byte sid must encode");
        OwnerId z; z.kind = 0; z.uid = 5;
        if (OwnerValid(z)) Fail("kind 0 with a uid is not valid");
    }
    {
        /* hand-built blocks the encoder would never write */
        std::vector<char> e;
        OwnPutU32(&e, kOwnMarker); e.push_back((char)0); OwnPutU32(&e, 0); OwnPutU32(&e, 0);
        OwnerId back; size_t end = 0;
        if (DecodeOwner(&e[0], e.size(), 0, &back, &end) != kOwnBad) Fail("a block of kind 0 is malformed (clean items send no block)");
        e[4] = (char)7;
        if (DecodeOwner(&e[0], e.size(), 0, &back, &end) != kOwnBad) Fail("a block of kind 7 is malformed");
        std::vector<char> f;
        OwnPutU32(&f, kOwnMarker); f.push_back((char)1); OwnPutU32(&f, 9); OwnPutU32(&f, 200); f.resize(f.size() + 200, 'a');
        if (DecodeOwner(&f[0], f.size(), 0, &back, &end) != kOwnBad) Fail("a sid length over the cap is malformed");
    }
    {
        /* the request / confirm tail: BAG1 then OWN1, read in that order at the offsets session.cpp uses */
        std::vector<char> m;
        m.push_back('x'); m.push_back('y');   /* the message's fixed part */
        std::vector<coopbag::BagRow> rows(1);
        rows[0].section = "backpack_content"; rows[0].baseSid = "123-gamedata.base";
        if (!coopbag::EncodeBagRows(&m, rows)) Fail("bag rows");
        OwnerId o; o.kind = kOwnSquad; o.uid = 77; o.factionSid = "@slot:0";
        if (!EncodeOwner(&m, o)) Fail("owner after bag");
        size_t at = 2, bagEnd = 0;
        if (OwnerBlockAt(&m[0], m.size(), at)) Fail("a BAG1 block is not an owner block");
        std::vector<coopbag::BagRow> got;
        if (coopbag::DecodeBagRows(&m[0], m.size(), at, &got, &bagEnd) != coopbag::kBagOk || got.size() != 1) Fail("bag decode before the owner");
        OwnerId back; size_t end = 0;
        if (!OwnerBlockAt(&m[0], m.size(), bagEnd) || DecodeOwner(&m[0], m.size(), bagEnd, &back, &end) != kOwnOk || end != m.size() || !OwnerEqual(back, o))
            Fail("the owner block after BAG1");
        /* an owner block with no bag: the decoder must see it FIRST, not hand it to the bag reader */
        std::vector<char> n; n.push_back('x'); n.push_back('y');
        EncodeOwner(&n, o);
        if (!OwnerBlockAt(&n[0], n.size(), 2)) Fail("an owner block standing alone is recognised by its marker");
        if (coopbag::DecodeBagRows(&n[0], n.size(), 2, &got, &bagEnd) == coopbag::kBagOk) Fail("the bag reader must not accept an owner block");
    }
    {
        unsigned int h[5] = { 11u, 0u, 0u, 5u, 6u };
        if (RawHandIsOwner(h)) Fail("RECORD_NONE is no owner");
        unsigned int z[5] = { 34u, 1u, 2u, 0u, 0u };
        if (RawHandIsOwner(z)) Fail("an empty ticket is no owner");
        unsigned int q[5] = { 34u, 1u, 2u, 5u, 0u };
        if (!RawHandIsOwner(q)) Fail("a platoon hand with a ticket is an owner");
        if (OwnerKindOfHandType(1) != kOwnCharacter || OwnerKindOfHandType(34) != kOwnSquad || OwnerKindOfHandType(0) != kOwnOther
            || OwnerKindOfHandType(11) != kOwnNone) Fail("hand type -> kind");
    }
    {
        OwnerId o; o.kind = 2; o.uid = 4000000000u; o.factionSid = "@slot:2:Cats and Dogs";
        const std::string t = OwnerJournalTail(o);
        const size_t t1 = t.find('\t'), t2 = t.find('\t', t1 + 1);
        OwnerId back;
        if (t1 == std::string::npos || t2 == std::string::npos
            || !OwnerFromJournal(t.substr(0, t1), t.substr(t1 + 1, t2 - t1 - 1), t.substr(t2 + 1), &back) || !OwnerEqual(back, o))
            Fail("journal b2 fields round trip");
        if (OwnerFromJournal("4", "1", "x", &back)) Fail("journal kind 4 refused");
        if (OwnerFromJournal("0", "0", "", &back)) Fail("journal kind 0 refused (b2 is written only with an owner)");
        if (OwnerFromJournal("1", "12a", "x", &back)) Fail("journal uid not a number refused");
        if (OwnerFromJournal("1", "4294967296", "x", &back)) Fail("journal uid over 32 bits refused");
    }
    {
        OwnerId none, wire; wire.kind = 1; wire.uid = 3;
        if (OwnerTakerChoice(wire, true, true) != kTakerOwnWire) Fail("the confirm's owner wins over the local mark");
        if (OwnerTakerChoice(none, false, true) != kTakerOwnLocal) Fail("no owner on the confirm: the local D5-2 mark");
        if (OwnerTakerChoice(none, false, false) != kTakerOwnNone) Fail("nothing to apply");
        if (OwnerTakerChoice(wire, false, true) != kTakerOwnLocal) Fail("review-inv6 LOW: an unresolvable confirm owner yields to the local mark");
        if (OwnerTakerChoice(wire, false, false) != kTakerOwnWire) Fail("an unresolvable confirm owner with no local mark still goes to the apply");
    }
    {   /* review-inv6p2 MEDIUM: when a created pack takes its own mark; LOW: unknown-owner rows */
        OwnerId none, wire; wire.kind = 1; wire.uid = 3;
        if (OwnerPackMarkAfterFill(true, wire, false) != 0) Fail("a wire-carried pack mark goes on before the fill");
        if (OwnerPackMarkAfterFill(true, wire, true) != 0) Fail("a wire pack mark with a local one beside it still goes on before the fill");
        if (OwnerPackMarkAfterFill(true, none, true) != 1) Fail("the local D5-2 pack mark goes on after the fill");
        if (OwnerPackMarkAfterFill(false, none, true) != 0) Fail("no rows: the local mark goes on at create");
        if (OwnerPackMarkAfterFill(true, none, false) != 0) Fail("no mark at all: nothing after the fill");
        OwnerId unk; unk.kind = 1;
        OwnerId fac; fac.kind = 3; fac.factionSid = "HolyNation";
        std::vector<OwnerId> tail; tail.push_back(wire); tail.push_back(unk); tail.push_back(none); tail.push_back(fac);
        if (!OwnerUnknown(unk) || OwnerUnknown(wire) || OwnerUnknown(none) || OwnerUnknown(fac)) Fail("owner-unknown shape");
        if (OwnerTailUnknown(tail) != 1 || OwnerTailMarked(tail) != 3) Fail("a row tail: 3 marked, 1 of them owner unknown");
    }
    {
        OwnerId a; a.kind = 2; a.uid = 0; a.factionSid = "HolyNation";
        OwnerId c = a;
        OwnerId d = a; d.uid = 1;
        unsigned int ia = 0, sa = 0, ic = 0, sc = 0, id = 0, sd = 0;
        OwnerTombstone(a, &ia, &sa); OwnerTombstone(c, &ic, &sc); OwnerTombstone(d, &id, &sd);
        if (ia != ic || sa != sc) Fail("equal owners give equal tombstones (so their stacks still merge)");
        if (ia == id && sa == sd) Fail("different owners give different tombstones");
        if ((ia & 0x80000000u) == 0 || sa == 0) Fail("a tombstone index has its top bit set and a non-zero serial");
        if (!OwnerDigestTerm(OwnerId()).empty()) Fail("a clean item adds nothing to a box digest (phase 2)");
        if (OwnerDigestTerm(a) != "|o2:HolyNation") Fail("digest term");
    }
}
/* T-853: a give whose confirmation never lands - silence means 'cancelled' on both games. */
void t_t853_give_lapse_revoke_and_late_yes_square()
{
    using namespace coopgive;
    /* the owner's stand window outlives the giver's lapse - by a whole lapse - so the giver's REVOKE at its lapse lands first */
    if (!GiveStandOutlivesLapse(kOwnerGiveStandMs, kGiverLapseMs)) Fail("T-853: the owner's GIVE must stand only after twice the giver's lapse");
    if (GiveStandOutlivesLapse(kGiverLapseMs, kGiverLapseMs)) Fail("T-853: (control) a stand window equal to the lapse is too short");
    if (kGiverLapseMs != 10000u) Fail("T-853: kGiverLapseMs must match items.cpp kPendTimeoutMs (10 s)");
    if (kOwnerGiveStandMs != 2u * kGiverLapseMs) Fail("T-853: the owner's stand window is defined as twice the giver's lapse");
    if (OwnerPendWindowMs(1, 10000u, kOwnerGiveStandMs) != kOwnerGiveStandMs) Fail("T-853: a GIVE row uses the stand window");
    if (OwnerPendWindowMs(0, 10000u, kOwnerGiveStandMs) != 10000u) Fail("T-853: a TAKE row keeps its own 10 s");
    {   /* the owner's GIVE row, aged by the giver's lapse plus a little: still waiting (the REVOKE finds it), then stands */
        using coopesc::ExpireAction;
        const unsigned int Q = coopesc::kEscrowQuietMs;
        const unsigned int w = OwnerPendWindowMs(1, 10000u, kOwnerGiveStandMs);
        if (ExpireAction(1, 1, 0, kGiverLapseMs + 500u, 0, w, Q) != coopesc::kExpWait) Fail("T-853: a GIVE row still lives just after the giver's lapse");
        if (ExpireAction(1, 1, 0, kOwnerGiveStandMs, 0, w, Q) != coopesc::kExpStand) Fail("T-853: a GIVE nobody revoked stands at the stand window");
        const unsigned int t = OwnerPendWindowMs(0, 10000u, kOwnerGiveStandMs);
        if (ExpireAction(0, 1, 0, 10000u, 0, t, Q) != coopesc::kExpEscrow) Fail("T-853: a TAKE's timing is unchanged (escrow at 10 s)");
    }
    /* which lapsed requests revoke at once */
    if (LapseRevoke(1, 0, 0, 0, 1) != 1) Fail("T-853: a lapsed plain GIVE revokes at once");
    if (LapseRevoke(1, 2, 1, 0, 1) != 1) Fail("T-853: a lapsed SALE whose refund moved revokes (the owner puts the shopkeeper's cats back)");
    if (LapseRevoke(1, 2, 0, 0, 1) != 0) Fail("T-853: a lapsed SALE whose refund moved nothing keeps the late-yes road");
    if (LapseRevoke(0, 0, 0, 0, 1) != 0 || LapseRevoke(0, 1, 1, 0, 1) != 0) Fail("T-853: a TAKE never revokes at its lapse (the PLACED road)");
    if (LapseRevoke(1, 0, 0, 1, 1) != 0) Fail("T-853: a take-one consume never revokes at its lapse");
    /* an item that did not come back anywhere real (backLost, parked for our box, a faulted put-back) is never revoked */
    if (LapseRevoke(1, 0, 0, 0, 0) != 0) Fail("T-853: a lapsed GIVE whose item is not back somewhere real must not revoke");
    if (LapseRevoke(1, 2, 1, 0, 0) != 0) Fail("T-853: a lapsed SALE whose item is not back somewhere real must not revoke");
    /* a late ok after the revoke re-sends it (the owner's row exists now); a late no, or an id never revoked, does not */
    if (LateYesReRevoke(1, 1) != 1) Fail("T-853: a late ok for a revoked GIVE re-sends the REVOKE");
    if (LateYesReRevoke(1, 0) != 0) Fail("T-853: a late no for a revoked GIVE needs nothing (the owner built nothing)");
    if (LateYesReRevoke(0, 1) != 0) Fail("T-853: (control) an id not revoked at its lapse takes its own road");
    /* the late yes's removal by sid and square */
    if (!LateGiveTrySquare(-1, false, true)) Fail("T-853: a different object at the cell asks the square test");
    if (LateGiveTrySquare(0, false, true) || LateGiveTrySquare(1, false, true)) Fail("T-853: an empty cell / a removed object does not ask the square test");
    if (!LateGiveTrySquare(0, true, false)) Fail("T-853: a pack whose object test failed asks the square test");
    if (!LateGiveSquareMatch(true, 1, 1, false, true, true, true)) Fail("T-853: the same sid and quantity at the cell is our copy after the put-back");
    if (!LateGiveSquareMatch(true, 10, 10, false, false, true, true)) Fail("T-853: the digest is asked only of a pack");
    if (LateGiveSquareMatch(false, 1, 1, false, true, true, true)) Fail("T-853: a different sid at the cell is not ours");
    if (LateGiveSquareMatch(true, 30, 10, false, true, true, true)) Fail("T-853: a merge into the cell since (a different count) is not ours to take");
    if (LateGiveSquareMatch(true, -1, 1, false, true, true, true) || LateGiveSquareMatch(true, 0, 0, false, true, true, true)) Fail("T-853: an unreadable or unknown quantity is not guessed at");
    if (LateGiveSquareMatch(true, 1, 1, true, false, true, true)) Fail("T-853: a pack whose rows changed is not removed");
    if (!LateGiveSquareMatch(true, 1, 1, true, true, true, true)) Fail("T-853: a pack still holding the rows that were sent is ours");
    if (LateGiveSquareMatch(true, 1, 1, false, true, false, true)) Fail("T-853: another quality at the cell is not ours");
    if (LateGiveSquareMatch(true, 1, 1, false, true, true, false)) Fail("T-853: another level at the cell is not ours");
    if (LateGiveOutcome(false, true, LateGiveSquareMatch(true, 1, 1, false, true, true, true) ? 1 : -1) != kGiveRemoved) Fail("T-853: a square match ends removed");
    if (!LateGiveQualitySame(0.75f, 0.75f) || !LateGiveQualitySame(0.75f, 0.7504f)) Fail("T-853: the same quality (within a thousandth) matches");
    if (LateGiveQualitySame(0.75f, 0.76f) || LateGiveQualitySame(0.76f, 0.75f)) Fail("T-853: another quality does not match");
    {
        volatile float z = 0.0f;
        const float nan = z / z;
        if (LateGiveQualitySame(nan, 0.75f) || LateGiveQualitySame(0.75f, nan) || LateGiveQualitySame(nan, nan)) Fail("T-853: a quality that is not a number never matches");
    }
    /* the owner's keeper half on a REVOKE: undone exactly once */
    if (RevokeKeeperAction(0, 1, 0) != kRkNone || RevokeKeeperAction(0, 0, 1) != kRkNone) Fail("T-853: no cats moved at the grant - nothing to settle");
    if (RevokeKeeperAction(1, 1, 0) != kRkUndo) Fail("T-853: the item removed and this game runs the keeper - undone here");
    if (RevokeKeeperAction(1, 1, 1) != kRkRunner) Fail("T-853: the keeper is a copy here - its runner undoes the real pot, never a second payback here");
    if (RevokeKeeperAction(1, 0, 0) != kRkKept || RevokeKeeperAction(1, 0, 1) != kRkKept) Fail("T-853: the item was not removed - the payment stands with it");
    /* a REVOKE that could not be sent is kept, re-sent, and given up on - bounded */
    if (RevokeRetryAction(1, 0, kRevokeKeepMs) != kRvDone || RevokeRetryAction(1, kRevokeKeepMs + 5, kRevokeKeepMs) != kRvDone) Fail("T-853: a REVOKE handed to the link leaves the list");
    if (RevokeRetryAction(0, kRevokeKeepMs - 1, kRevokeKeepMs) != kRvKeep) Fail("T-853: an unsent REVOKE is kept inside its window");
    if (RevokeRetryAction(0, kRevokeKeepMs, kRevokeKeepMs) != kRvGiveUp) Fail("T-853: an unsent REVOKE is dropped at the end of its window");
    if (kRevokeKeepMs < kOwnerGiveStandMs || kRevokeRetryMs >= kGiverLapseMs || kRevokeListCap <= 0) Fail("T-853: the re-send covers the owner's stand window, tries more than once in it, and has room");
}
/* T-889: a give between players is never left doubled by a reconnect or a long drop. */
void t_t889_give_row_by_player_presence_and_retry_hold()
{
    using namespace coopgive;
    const unsigned int C = coopesc::kQuietDtCapMs;
    /* the owner's GIVE row a REVOKE names: by request id, then the connection id, then the player */
    if (GiveRowMatch(1, 7u, 7u, 1, 1) != kGmExact || GiveRowMatch(1, 7u, 7u, 1, 0) != kGmExact) Fail("T-889: the same connection id and request id is the row");
    if (GiveRowMatch(1, 7u, 7u, 0, 1) != kGmPlayer) Fail("T-889: a reconnected giver (new connection id, same player) finds its row");
    if (GiveRowMatch(1, 7u, 7u, 0, 0) != kGmNone) Fail("T-889: another player's REVOKE with the same request id is not this row");
    if (GiveRowMatch(1, 7u, 8u, 1, 1) != kGmNone) Fail("T-889: two gives from one player stay separate (another request id)");
    if (GiveRowMatch(0, 7u, 7u, 1, 1) != kGmNone) Fail("T-889: a TAKE row never answers a REVOKE");
    if (GiveRowMatch(1, 0u, 0u, 1, 1) != kGmNone) Fail("T-889: request id 0 names no row");
    {   /* T-608: the player match is GiveSamePlayer's - the exact player key (SamePlayer), plus the raw current session peer while
           the link's slot is unknown; never any two raw keys */
        const unsigned int a = cooplive::RelayPeerId(2u), b = cooplive::RelayPeerId(3u);
        if (GiveRowMatch(1, 7u, 7u, 0, GiveSamePlayer(a, a, -1, 0)) != kGmPlayer) Fail("T-889: the same slot key is the same player");
        if (GiveRowMatch(1, 7u, 7u, 0, GiveSamePlayer(a, b, -1, 0)) != kGmNone) Fail("T-889: another slot is another player");
        if (GiveSamePlayer(a, b, 2, 1) != 0) Fail("T-608: another slot is another player, whatever the session flag says");
        /* link slot known: a raw row of the session link and the same player's relayed sender are one player, both ways */
        if (GiveSamePlayer(5u, a, 2, 0) != 1) Fail("T-608: a row asked on the session link answers that player's REVOKE through the notebook");
        if (GiveSamePlayer(a, 9u, 2, 0) != 1) Fail("T-608: a row asked through the notebook answers that player's REVOKE on the session link");
        if (GiveSamePlayer(5u, 9u, 2, 0) != 1) Fail("T-608: with the link's slot known two raw ids of that link are its one player");
        if (GiveSamePlayer(5u, b, 2, 0) != 0) Fail("T-608: a raw row of slot 2's link does not answer slot 3");
        /* link slot unknown (no notebook, or a reconnect before PEER_SLOT): raw against raw only for the CURRENT session peer */
        if (GiveSamePlayer(5u, 5u, -1, 0) != 1) Fail("T-608: no slot - the same raw connection id is the same player");
        if (GiveRowMatch(1, 7u, 7u, 0, GiveSamePlayer(5u, 9u, -1, 1)) != kGmPlayer) Fail("T-608 fold: a reconnected giver (the current session peer, new raw id, no slot) finds its row");
        if (GiveRowMatch(1, 7u, 7u, 0, GiveSamePlayer(5u, 9u, -1, 0)) != kGmNone) Fail("T-608 fold: a raw sender that is not the current session peer is not the row's player");
        if (GiveSamePlayer(a, 9u, -1, 1) != 0) Fail("T-608 fold: the raw session peer with no link slot does not take a slot-keyed row");
        if (GiveSamePlayer(5u, a, -1, 1) != 0) Fail("T-608 fold: a relayed sender never takes a raw row while the link's slot is unknown");
        if (GiveSamePlayer(5u, 9u, 2, 1) != 1) Fail("T-608 fold: with the link's slot known the raw ids fold to it (the flag is not needed)");
        /* the flag as the plugin computes it (coopesc::RawIsSessionPeerUnslotted), end to end */
        if (GiveSamePlayer(5u, 9u, -1, coopesc::RawIsSessionPeerUnslotted(9u, 1, 9u, -1)) != 1) Fail("T-608 fold: the current session peer's new id, flag from the session state");
        if (GiveSamePlayer(5u, 9u, -1, coopesc::RawIsSessionPeerUnslotted(9u, 1, 11u, -1)) != 0) Fail("T-608 fold: a raw id that is not the current session peer, flag from the session state");
        if (GiveSamePlayer(5u, 9u, -1, coopesc::RawIsSessionPeerUnslotted(9u, 0, 9u, -1)) != 0) Fail("T-608 fold: no session peer up - nothing is the session peer");
    }
    /* the owner's stand clock: a GIVE's age is its giver's time in the world; a TAKE keeps wall time */
    if (OwnerPendAgeMs(1, 90000u, 1234u) != 1234u) Fail("T-889: a GIVE row is aged by its giver's time in the world");
    if (OwnerPendAgeMs(0, 90000u, 1234u) != 90000u) Fail("T-889: a TAKE row keeps the wall time since the grant");
    {
        using coopesc::ExpireAction;
        const unsigned int Q = coopesc::kEscrowQuietMs;
        const unsigned int w = OwnerPendWindowMs(1, 10000u, kOwnerGiveStandMs);
        unsigned int here = 0, wall = 0;
        for (int t = 0; t < 5; ++t) { here = coopesc::QuietStep(here, 1000u, 1, C); wall += 1000u; }   /* 5 s with the giver here */
        for (int t = 0; t < 120; ++t) { here = coopesc::QuietStep(here, 1000u, 0, C); wall += 1000u; }   /* a 2-minute drop */
        if (here != 5000u) Fail("T-889: the stand clock is held while the giver is away");
        if (ExpireAction(1, 0, 0, OwnerPendAgeMs(1, wall, here), 0, w, Q) != coopesc::kExpWait) Fail("T-889: a GIVE row whose giver dropped does not stand on wall time");
        if (ExpireAction(1, 0, 0, OwnerPendAgeMs(1, wall, 0u), 0, w, Q) != coopesc::kExpWait) Fail("T-889: a GIVE row is not aged before its giver is seen");
        here = coopesc::QuietStep(here, 900000u, 1, C);   /* a long frame (a load) counts as the cap */
        if (here != 5000u + C) Fail("T-889: a hitch never ages the stand clock by more than the cap");
        while (here < kOwnerGiveStandMs) here = coopesc::QuietStep(here, 1000u, 1, C);
        if (ExpireAction(1, 0, 0, OwnerPendAgeMs(1, wall, here), 0, w, Q) != coopesc::kExpStand) Fail("T-889: a GIVE nobody revoked stands after the giver's time in the world");
        if (ExpireAction(0, 1, 0, OwnerPendAgeMs(0, 10000u, 0u), 0, OwnerPendWindowMs(0, 10000u, kOwnerGiveStandMs), Q) != coopesc::kExpEscrow) Fail("T-889: a TAKE's timing is unchanged");
    }
    /* the hold is said once */
    if (AwayHoldFirst(0, 0) != 1) Fail("T-889: the first hold is said");
    if (AwayHoldFirst(0, 1) != 0) Fail("T-889: a hold already said is not said again");
    if (AwayHoldFirst(1, 0) != 0 || AwayHoldFirst(1, 1) != 0) Fail("T-889: no hold while the other game is here");
    /* the giver's kept REVOKE: its clock does not run while the game it goes to is unreachable */
    {
        unsigned int age = 0;
        for (int t = 0; t < 600; ++t) age = coopesc::QuietStep(age, 1000u, 0, C);   /* 10 minutes with the link down */
        if (age != 0u || RevokeRetryAction(0, age, kRevokeKeepMs) != kRvKeep) Fail("T-889: a kept REVOKE is never given up while the link is down");
        while (age < kRevokeKeepMs - 1000u) age = coopesc::QuietStep(age, 1000u, 1, C);
        if (RevokeRetryAction(0, age, kRevokeKeepMs) != kRvKeep) Fail("T-889: a kept REVOKE is kept inside its reachable window");
        age = coopesc::QuietStep(age, 1000u, 1, C);
        if (RevokeRetryAction(0, age, kRevokeKeepMs) != kRvGiveUp) Fail("T-889: a kept REVOKE is dropped after its window of reachable time");
        if (RevokeRetryAction(1, age, kRevokeKeepMs) != kRvDone) Fail("T-889: a REVOKE handed to the link leaves the list");
    }
}
/* The giver's 'give done' ends the owner's GIVE row at once; the built stack merges onto a matching stack only after its row ended. */
void t_t1087_give_done_and_close_merge()
{
    using namespace coopgive;
    /* the value byte: a done is neither of a TAKE's two answers */
    if (kPlacedGiveDone == 0 || kPlacedGiveDone == 1 || kPlacedGiveDone > 255) Fail("t1087: the done value is not a TAKE's answer and fits the byte");
    /* a TAKE's answers are never a done, whatever row they name */
    if (PlacedDoneAction(1, 1, 1, 7u, 7u, 1, 1) != kDnNotDone || PlacedDoneAction(0, 1, 1, 7u, 7u, 1, 1) != kDnNotDone
        || PlacedDoneAction(1, 1, 0, 7u, 7u, 1, 1) != kDnNotDone) Fail("t1087: PLACED 0 / 1 stay a TAKE's answer");
    /* a done closes the GIVE row of its own request from its own giver */
    if (PlacedDoneAction(kPlacedGiveDone, 1, 1, 7u, 7u, 1, 1) != kDnClose) Fail("t1087: a done from the request's connection closes its GIVE row");
    if (PlacedDoneAction(kPlacedGiveDone, 1, 1, 7u, 7u, 0, 1) != kDnClose) Fail("t1087: a done from the same player (reconnected) closes its GIVE row");
    /* every other done ends with nothing changed: no row (it stood, was revoked, a second copy), another player, another request, a TAKE row */
    if (PlacedDoneAction(kPlacedGiveDone, 0, -1, 0u, 7u, 0, 0) != kDnNoRow) Fail("t1087: a done after its row ended (late, duplicate, after a revoke) changes nothing");
    if (PlacedDoneAction(kPlacedGiveDone, 1, 1, 7u, 7u, 0, 0) != kDnNoRow) Fail("t1087: another player's done with the same request id is not this row");
    if (PlacedDoneAction(kPlacedGiveDone, 1, 1, 7u, 8u, 1, 1) != kDnNoRow) Fail("t1087: a done for another request is not this row");
    if (PlacedDoneAction(kPlacedGiveDone, 1, 0, 7u, 7u, 1, 1) != kDnNoRow) Fail("t1087: a done never closes a TAKE row");
    if (PlacedDoneAction(kPlacedGiveDone, 1, 1, 0u, 0u, 1, 1) != kDnNoRow) Fail("t1087: request id 0 names no row");
    /* a lost done: the stand window stays the ending, and it still outlives the giver's lapse */
    if (!GiveStandOutlivesLapse(kOwnerGiveStandMs, kGiverLapseMs)) Fail("t1087: the stand window (the ending of a lost done) outlives the giver's lapse");

    /* the merge: all checks hold */
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 5, 5, 1, 10) != kMgMerge) Fail("t1087: a closed box row's untouched built stack merges onto a matching stack with room");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 5, 5, 1, 5) != kMgMerge) Fail("t1087: room for exactly the built units is enough");
    /* never while the row is open - a REVOKE removes exactly the built object - whatever else holds */
    if (GiveMergeAction(0, 1, 1, 1, 1, 1, 1, 5, 5, 1, 10) != kMgRowOpen) Fail("t1087: an open row's built stack never merges");
    if (GiveMergeAction(0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0) != kMgRowOpen) Fail("t1087: an open row is the first answer");
    /* each failing check leaves it unmerged, with its own reason */
    if (GiveMergeAction(1, 0, 1, 1, 1, 1, 1, 5, 5, 1, 10) != kMgNotBox) Fail("t1087: a character's give is not merged");
    if (GiveMergeAction(1, 1, 0, 1, 1, 1, 1, 5, 5, 1, 10) != kMgNotWriter) Fail("t1087: a box this game does not write is not merged");
    if (GiveMergeAction(1, 1, 1, 0, 1, 1, 1, 5, 5, 1, 10) != kMgLineOpen) Fail("t1087: a give whose hand-over line stands is not merged (its square must keep the stack)");
    if (GiveMergeAction(1, 1, 1, 1, 0, 1, 1, 5, 5, 1, 10) != kMgNoSection) Fail("t1087: an unresolved section is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 0, 1, 5, 5, 1, 10) != kMgBuiltGone) Fail("t1087: a built object no longer in the section is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 0, 5, 5, 1, 10) != kMgUnread) Fail("t1087: an unreadable built object is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 8, 5, 1, 10) != kMgQtyChanged) Fail("t1087: units the player stacked onto the built object are not merged with it");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 3, 5, 1, 10) != kMgQtyChanged) Fail("t1087: a built object that lost units is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, -1, 5, 1, 10) != kMgQtyChanged) Fail("t1087: an unread quantity is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 10) != kMgQtyChanged) Fail("t1087: a row with no built quantity is not merged");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 5, 5, 0, 10) != kMgNoStack) Fail("t1087: no matching stack - nothing to merge onto");
    if (GiveMergeAction(1, 1, 1, 1, 1, 1, 1, 5, 5, 1, 4) != kMgNoRoom) Fail("t1087: a stack without room for every built unit is not merged onto");
    if (kMgReasons != 11 || kMgMerge != 0 || kMgNoRoom != kMgReasons - 1) Fail("t1087: the merge reasons fill the report's slots 0-10");
    /* a fault inside the merge: before the removal nothing changed; after it the stack is never left out silently */
    if (MergeFaultEnd(0, 5, -1) != kMfNothing) Fail("t1087: a fault before the removal changed nothing");
    if (MergeFaultEnd(1, 0, -1) != kMfMerged) Fail("t1087: a fault after the stack went into its target - it merged (the empty object is destroyed)");
    if (MergeFaultEnd(1, 5, 1) != kMfPutBack) Fail("t1087: a fault after the removal puts the stack back in the box");
    if (MergeFaultEnd(1, 5, 0) != kMfLost || MergeFaultEnd(1, 5, -1) != kMfLost) Fail("t1087: a stack the box would not take back is lost (counted)");
    if (MergeFaultEnd(1, -1, -1) != kMfLost) Fail("t1087: an unreadable stack after the removal is counted lost, never guessed placed");
}
void t_give1_late_give_and_cancel_refunds()
{
    if (coopgive::LateConfirmAction(1, 0, 1, 0) != coopgive::kLateGiveRemove) Fail("a late ok answer to a GIVE must remove the giver's copy");
    if (coopgive::LateConfirmAction(0, 0, 1, 0) != coopgive::kLateNone) Fail("a late refusal of a give must change nothing");
    if (coopgive::LateConfirmAction(1, 0, 0, 0) != coopgive::kLateNone) Fail("a late ok answer to a plain TAKE must change nothing here");
    if (coopgive::LateConfirmAction(1, 2, 1, 1) != coopgive::kLateSell) Fail("a late ok refunded SALE keeps its P7q removal");
    if (coopgive::LateConfirmAction(1, 2, 1, 0) != coopgive::kLateGiveRemove) Fail("a late ok sale whose refund did not move removes the giver's copy like a give (review-give1 M2)");
    if (coopgive::LateConfirmAction(1, 1, 0, 1) != coopgive::kLateBuyNote) Fail("a late ok refunded PURCHASE stays a note");
    if (coopgive::LateConfirmAction(1, 1, 0, 0) != coopgive::kLateNone) Fail("a late ok purchase with no refund does nothing");
    if (coopgive::LateConfirmAction(0, 2, 1, 1) != coopgive::kLateNone) Fail("a late refusal of a sale does nothing");

    if (coopgive::LateGiveOutcome(false, true, 1) != coopgive::kGiveRemoved) Fail("an item still there must be removed");
    if (coopgive::LateGiveOutcome(true, true, 1) != coopgive::kGiveRemoved) Fail("an unchanged pack must be removed");
    if (coopgive::LateGiveOutcome(true, false, 0) != coopgive::kGiveChanged) Fail("a changed pack must count changed");
    if (coopgive::LateGiveOutcome(true, false, 1) != coopgive::kGiveChanged) Fail("a changed pack is changed whatever the removal said");
    if (coopgive::LateGiveOutcome(false, true, 0) != coopgive::kGiveGone) Fail("an empty slot must count gone");
    if (coopgive::LateGiveOutcome(false, true, -1) != coopgive::kGiveChanged) Fail("a different object at the slot must count changed");

    if (coopgive::CancelRefundOwed(1, 2) != coopgive::kCancelSale) Fail("a sale cancelled at confirm must take the credit back");
    if (coopgive::CancelRefundOwed(0, 1) != coopgive::kCancelBuy) Fail("a purchase that could not be placed must give the cats back");
    if (coopgive::CancelRefundOwed(1, 0) != coopgive::kCancelNone || coopgive::CancelRefundOwed(0, 0) != coopgive::kCancelNone)
        Fail("a plain give / take moves no money");
    if (coopgive::CancelRefundOwed(0, 2) != coopgive::kCancelNone || coopgive::CancelRefundOwed(1, 1) != coopgive::kCancelNone)
        Fail("a direction that does not match its trade must move no money");

    int v = -1;
    if (!coopgive::ConfirmDelayParse("0", &v) || v != 0) Fail("0 must turn the lever off");
    if (!coopgive::ConfirmDelayParse("15", &v) || v != 15) Fail("15 must be accepted");
    if (!coopgive::ConfirmDelayParse("30", &v) || v != 30) Fail("30 is the maximum and must be accepted");
    if (coopgive::ConfirmDelayParse("31", &v) || coopgive::ConfirmDelayParse("", &v) || coopgive::ConfirmDelayParse("-1", &v)
        || coopgive::ConfirmDelayParse("1x", &v) || coopgive::ConfirmDelayParse("0015", &v) || coopgive::ConfirmDelayParse(0, &v))
        Fail("anything but 0..30 in whole seconds must be refused");

    coopbag::BagRow r;
    r.section = "backpack_content"; r.x = 1; r.y = 2; r.qty = 3;
    r.baseSid = "2292-gamedata.base"; r.companySid = "c"; r.materialSid = "m"; r.colorSid = "k";
    r.quality = 40.0f; r.charges = 1.0f; r.functionKind = 2; r.level = 3; r.unique = 0;
    const std::vector<coopbag::BagRow> a(1, r);
    const unsigned int d = coopbag::BagRowsDigest(a);
    if (coopbag::BagRowsDigest(a) != d) Fail("the same rows must digest the same");
    std::vector<coopbag::BagRow> b = a; b[0].quality = 41.0f;
    if (coopbag::BagRowsDigest(b) == d) Fail("the digest must see a changed quality");
    b = a; b[0].charges = 2.0f;
    if (coopbag::BagRowsDigest(b) == d) Fail("the digest must see changed charges");
    b = a; b[0].functionKind = 5;
    if (coopbag::BagRowsDigest(b) == d) Fail("the digest must see a changed function");
    b = a; b[0].level = 4;
    if (coopbag::BagRowsDigest(b) == d) Fail("the digest must see a changed level");
    b = a; b[0].unique = 1;
    if (coopbag::BagRowsDigest(b) == d) Fail("the digest must see a changed unique flag");
}

/* inv6 phase 2: the OWNL owner tail after the PARITY_BOX items, the BAG1 rows and the QLT1 entries - zero bytes when nothing is
   marked, a round trip by index, a cut / out-of-range tail refused, and the digests blind to the marks. */
void t_inv6p2_owner_tails()
{
    coopmark::OwnerId mk; mk.kind = 1; mk.uid = 77; mk.factionSid = "Nobles";
    coopmark::OwnerId sq; sq.kind = 2; sq.uid = 0; sq.factionSid = "@slot:1:Wanderers";
    {
        std::vector<coopmark::OwnerId> none(3), some(3);
        std::vector<char> b;
        if (coopmark::EncodeOwnerTail(&b, none, 0) != 0 || !b.empty()) Fail("a list with no mark must add zero bytes");
        some[0] = mk; some[2] = sq;
        if (coopmark::EncodeOwnerTail(&b, some, 0) != 2 || b.empty()) { Fail("two marks must encode"); return; }
        std::vector<coopmark::OwnerId> back; size_t end = 0;
        if (coopmark::DecodeOwnerTail(&b[0], b.size(), 0, 3, &back, &end) != coopmark::kOwnOk || end != b.size() || back.size() != 3
            || !coopmark::OwnerEqual(back[0], mk) || back[1].kind != 0 || !coopmark::OwnerEqual(back[2], sq)) Fail("tail round trip by index");
        if (coopmark::DecodeOwnerTail(&b[0], b.size(), 0, 2, &back, &end) != coopmark::kOwnBad) Fail("an index past the list must be refused");
        for (size_t cut = 1; cut < b.size(); ++cut)
            if (coopmark::DecodeOwnerTail(&b[0], cut, 0, 3, &back, &end) == coopmark::kOwnOk) { Fail("a cut tail must never decode"); break; }
        std::vector<char> dup(b);   /* the second entry's index rewritten to 0: not strictly rising */
        const size_t second = 8 + 4 + 1 + 4 + 4 + mk.factionSid.size();
        std::memset(&dup[second], 0, 4);
        if (coopmark::DecodeOwnerTail(&dup[0], dup.size(), 0, 3, &back, &end) != coopmark::kOwnBad) Fail("a repeated index must be refused");
        const char other[4] = { 'O', 'W', 'N', '1' };
        if (coopmark::DecodeOwnerTail(other, 4, 0, 3, &back, &end) != coopmark::kOwnAbsent) Fail("an OWN1 marker is not a tail");
        coopmark::OwnerId bad; bad.kind = 1; bad.factionSid = std::string(200, 'x');
        std::vector<coopmark::OwnerId> inv(1, bad); std::vector<char> bi; unsigned int invalid = 0;
        if (coopmark::EncodeOwnerTail(&bi, inv, &invalid) != 0 || !bi.empty() || invalid != 1) Fail("an invalid identity travels clean and is counted");
    }
    {
        cooppar::ParityBox m; m.status = cooppar::kParityStatusContents; m.key = "box|1";
        cooppar::ParityItem a; a.section = "main"; a.baseSid = "2292-gamedata.base"; a.quantity = 3; a.quality = 0.5f;
        cooppar::ParityItem c = a; c.x = 1;
        m.items.push_back(a); m.items.push_back(c);
        m.digest = cooppar::ParityDigestOf(m.items);
        std::vector<char> clean; cooppar::EncodeParityBox(&clean, m);
        m.items[1].owner = mk;
        if (cooppar::ParityDigestOf(m.items) != m.digest) Fail("the box digest must not see the mark");
        std::vector<char> marked; cooppar::EncodeParityBox(&marked, m);
        if (clean.empty() || marked.size() <= clean.size() || std::memcmp(&marked[0], &clean[0], clean.size()) != 0) { Fail("the marked box is the clean bytes plus a tail"); return; }
        cooppar::ParityBox back;
        if (cooppar::DecodeParityBox(&clean[0], clean.size(), &back) != cooppar::kParityDecodeOk || back.items.size() != 2 || back.items[1].owner.kind != 0) Fail("a clean box decodes clean");
        if (cooppar::DecodeParityBox(&marked[0], marked.size(), &back) != cooppar::kParityDecodeOk || back.items.size() != 2 || back.items[0].owner.kind != 0
            || !coopmark::OwnerEqual(back.items[1].owner, mk)) Fail("the box tail round trip");
        if (cooppar::DecodeParityBox(&marked[0], marked.size() - 1, &back) != cooppar::kParityDecodeBadOwner) Fail("a cut box tail is refused as a bad owner tail");
        if (cooppar::ParityMarkedCount(m.items) != 1) Fail("parity marked count");
    }
    {
        std::vector<coopbag::BagRow> rows(2);
        rows[0].section = "backpack_content"; rows[0].baseSid = "2292-gamedata.base"; rows[0].qty = 2;
        rows[1] = rows[0]; rows[1].x = 1;
        std::vector<char> clean; coopbag::EncodeBagRows(&clean, rows);
        const unsigned int d0 = coopbag::BagRowsDigest(rows);
        rows[0].owner = sq;
        if (coopbag::BagRowsDigest(rows) != d0) Fail("the bag digest must not see the mark");
        std::vector<char> marked; coopbag::EncodeBagRows(&marked, rows);
        if (clean.empty() || marked.size() <= clean.size()) { Fail("a marked row adds a tail"); return; }
        std::vector<char> withPack(marked); coopmark::EncodeOwner(&withPack, mk);   /* the pack's own OWN1 follows the rows' tail */
        std::vector<coopbag::BagRow> back; size_t end = 0;
        if (coopbag::DecodeBagRows(&withPack[0], withPack.size(), 0, &back, &end) != coopbag::kBagOk || end != marked.size() || back.size() != 2
            || !coopmark::OwnerEqual(back[0].owner, sq) || back[1].owner.kind != 0) Fail("bag tail round trip, stopping at the pack's OWN1");
        coopmark::OwnerId packOwner; size_t pe = 0;
        if (coopmark::DecodeOwner(&withPack[0], withPack.size(), end, &packOwner, &pe) != coopmark::kOwnOk || !coopmark::OwnerEqual(packOwner, mk))
            Fail("the pack's own OWN1 after the rows' tail");
        if (coopbag::DecodeBagRows(&clean[0], clean.size(), 0, &back, &end) != coopbag::kBagOk || end != clean.size() || back[0].owner.kind != 0) Fail("clean bag rows");
        if (coopbag::DecodeBagRows(&marked[0], marked.size() - 1, 0, &back, &end) != coopbag::kBagBadOwner) Fail("a cut bag tail is refused");
        if (coopbag::BagRowsMarked(rows) != 1) Fail("bag marked count");
    }
    {
        std::vector<coopgarment::GarmentDetail> d(2);
        d[0].quality = 0.8f;
        std::vector<char> clean; coopgarment::EncodeGarmentDetails(&clean, d);
        d[1].owner = mk;
        std::vector<char> marked; coopgarment::EncodeGarmentDetails(&marked, d);
        if (clean.empty() || marked.size() <= clean.size() || std::memcmp(&marked[0], &clean[0], clean.size()) != 0) { Fail("the marked kit is the clean bytes plus a tail"); return; }
        std::vector<coopgarment::GarmentDetail> back;
        if (coopgarment::DecodeGarmentDetails(&marked[0], marked.size(), 0, 2, &back) != coopgarment::kDetailOk || back.size() != 2
            || back[0].owner.kind != 0 || !coopmark::OwnerEqual(back[1].owner, mk)) Fail("kit tail round trip");
        back.clear();
        if (coopgarment::DecodeGarmentDetails(&marked[0], marked.size() - 1, 0, 2, &back) != coopgarment::kDetailBadOwner || back.size() != 2
            || back[1].owner.kind != 0 || back[0].quality != 0.8f) Fail("a cut kit tail keeps the details and drops the marks");
        if (coopgarment::DecodeGarmentDetails(&clean[0], clean.size(), 0, 2, &back) != coopgarment::kDetailOk || back[1].owner.kind != 0) Fail("a clean kit");
        if (coopgarment::GarmentMarked(d) != 1) Fail("kit marked count");
    }
}
/* owner decision 396: which owner marks a GROUND item leaves without (src/common/ownerwire.h OwnerNamesPlayer) - a player's own
   faction and any stand-in ("@slot:..."), as a character, squad or other owner; never an NPC faction, an unknown owner or no owner. */
void t_ground_player_mark()
{
    using namespace coopmark;
    OwnerId none;
    if (OwnerNamesPlayer(none)) Fail("no owner names no player");
    OwnerId mine; mine.kind = kOwnCharacter; mine.uid = 4194305u; mine.factionSid = "@slot:1:Wanderers";
    if (!OwnerNamesPlayer(mine)) Fail("the dropper's own player faction names a player");
    OwnerId stand; stand.kind = kOwnCharacter; stand.uid = 8388609u; stand.factionSid = "@slot:2";
    if (!OwnerNamesPlayer(stand)) Fail("a stand-in (another player) names a player");
    OwnerId noSlot; noSlot.kind = kOwnSquad; noSlot.factionSid = "@slot:?:Wanderers";
    if (!OwnerNamesPlayer(noSlot)) Fail("a player faction with no slot yet names a player");
    OwnerId other; other.kind = kOwnOther; other.factionSid = "@slot:0";
    if (!OwnerNamesPlayer(other)) Fail("a player's non-character owner names a player");
    OwnerId npc; npc.kind = kOwnCharacter; npc.uid = 77; npc.factionSid = "Nobles";
    if (OwnerNamesPlayer(npc)) Fail("an NPC character keeps its mark");
    OwnerId npcSquad; npcSquad.kind = kOwnSquad; npcSquad.uid = 78; npcSquad.factionSid = "204-gamedata.base";
    if (OwnerNamesPlayer(npcSquad)) Fail("an NPC faction's stringID keeps its mark");
    OwnerId unknown; unknown.kind = kOwnCharacter;
    if (OwnerNamesPlayer(unknown)) Fail("an owner the sender did not find keeps its mark");
    OwnerId lookalike; lookalike.kind = kOwnCharacter; lookalike.factionSid = "slot:1";
    if (OwnerNamesPlayer(lookalike)) Fail("only the @slot: prefix names a player");
    OwnerId kindless; kindless.factionSid = "@slot:1";
    if (OwnerNamesPlayer(kindless)) Fail("kind 0 is no owner whatever the text");
}

/* inv7a: the stand-in purge at load. Every step of one character, and the once-per-load / hold rules. */
/* mmo1c: GameDataContainer::save 0x6BD220's byte layout written to memory (src/common/ownrec.h PodBuf helpers - the
   SAME code the plugin's writer calls). Ints, strings, the overflow count, and a synthetic one-record container encoded
   with the helpers against its bytes spelled out by hand. */
void t_mmo1c_podbytes()
{
    using namespace coopown;
    char buf[512];
    PodBuf b;
    PodInit(&b, buf, sizeof(buf));
    PodPutI32(&b, 0x01020304); PodPutI32(&b, -1);
    const unsigned char e1[8] = { 4, 3, 2, 1, 0xFF, 0xFF, 0xFF, 0xFF };
    if (b.len != 8 || std::memcmp(buf, e1, 8) != 0) Fail("mmo1c: an int is 4 bytes little-endian");
    PodInit(&b, buf, sizeof(buf));
    PodPutStr(&b, "abc", 3); PodPutStr(&b, "", 0);
    const unsigned char e2[11] = { 3, 0, 0, 0, 'a', 'b', 'c', 0, 0, 0, 0 };
    if (b.len != 11 || std::memcmp(buf, e2, 11) != 0) Fail("mmo1c: a string is its 4-byte length then its bytes, no terminator");

    /* a too-small buffer: the exact size is still counted, nothing is written past its end */
    char small[8]; std::memset(small, 0x55, sizeof(small));
    PodInit(&b, small, 5);
    PodPutI32(&b, 7); PodPutStr(&b, "xyz", 3);
    if (b.len != 11 || !PodOverflow(&b) || small[0] != 7 || small[4] != 0x55 || small[5] != 0x55 || small[7] != 0x55)
        Fail("mmo1c: an overflowing buffer counts the exact size and writes nothing past its end");
    PodInit(&b, small, 4); PodPutI32(&b, 9);
    if (b.len != 4 || PodOverflow(&b)) Fail("mmo1c: an exact fit is not an overflow");

    /* a synthetic container: one record, every section in 0x6BD220's order */
    PodInit(&b, buf, sizeof(buf));
    PodPutGdcHeader(&b, 7, 1);
    PodPutI32(&b, 2); PodPutI32(&b, 30); PodPutI32(&b, 5);            /* idCounter, type 0x1E, id */
    PodPutStr(&b, "sq", 2); PodPutStr(&b, "5-x", 3); PodPutI32(&b, 0); /* name, stringID, 0 */
    const char bt = 1; const float fv = 1.0f; const int iv = 3;
    PodPutI32(&b, 1); PodPutKeyRaw(&b, "b", 1, &bt, 1);                /* boolFields */
    PodPutI32(&b, 1); PodPutKeyRaw(&b, "f", 1, &fv, 4);                /* floatFields */
    PodPutI32(&b, 1); PodPutKeyRaw(&b, "char count", 10, &iv, 4);      /* intFields */
    PodPutI32(&b, 0);                                                  /* vectorFields */
    PodPutI32(&b, 0);                                                  /* rotationFields */
    PodPutI32(&b, 1); PodPutKeyStr(&b, "k", 1, "v", 1);                /* stringFields */
    PodPutI32(&b, 0);                                                  /* fileFields */
    const int tri[3] = { 1, 2, 3 };
    PodPutI32(&b, 1); PodPutStr(&b, "L", 1); PodPutI32(&b, 1); PodPutStr(&b, "r", 1); PodPutBytes(&b, tri, 12);   /* references */
    PodPutI32(&b, 0);                                                  /* instances */
    const unsigned char e3[146] = {
        15,0,0,0, 7,0,0,0, 1,0,0,0,
        2,0,0,0, 30,0,0,0, 5,0,0,0,
        2,0,0,0, 's','q', 3,0,0,0, '5','-','x', 0,0,0,0,
        1,0,0,0, 1,0,0,0, 'b', 1,
        1,0,0,0, 1,0,0,0, 'f', 0x00,0x00,0x80,0x3F,
        1,0,0,0, 10,0,0,0, 'c','h','a','r',' ','c','o','u','n','t', 3,0,0,0,
        0,0,0,0,
        0,0,0,0,
        1,0,0,0, 1,0,0,0, 'k', 1,0,0,0, 'v',
        0,0,0,0,
        1,0,0,0, 1,0,0,0, 'L', 1,0,0,0, 1,0,0,0, 'r', 1,0,0,0, 2,0,0,0, 3,0,0,0,
        0,0,0,0 };
    if (b.len != sizeof(e3) || PodOverflow(&b)) Fail("mmo1c: the synthetic container is " + std::string(b.len == sizeof(e3) ? "" : "NOT ") + "146 bytes");
    else
    {
        const long long at = FirstDiff(buf, b.len, (const char*)e3, sizeof(e3));
        if (at >= 0) Fail("mmo1c: the synthetic container's bytes differ from the hand-written layout");
    }

    /* FirstDiff, the compare-mode verdict */
    if (FirstDiff("abc", 3, "abc", 3) != -1 || FirstDiff("abc", 3, "abd", 3) != 2 || FirstDiff("ab", 2, "abc", 3) != 2
        || FirstDiff("abc", 3, "ab", 2) != 2 || FirstDiff("", 0, "", 0) != -1 || FirstDiff("x", 1, "y", 1) != 0)
        Fail("mmo1c: FirstDiff names the first differing offset, the shorter length for a prefix, -1 for equal");
}

/* mmo2 + fold (review-mmo2): the mark, the store identity, the overlay rule (mark / quick.save time / none), a .platoon
   file's characters and items, the container-name check and the duplicate-aware roster check (src/common/ownrec.h). */
void Mmo2PutKeyI(coopown::PodBuf* b, const char* k, int v) { coopown::PodPutKeyRaw(b, k, std::strlen(k), &v, 4); }
/* one record: hand (serial/type/index/container) when hasHand; an extra "item handle*" hand when itemSerial != 0 */
void Mmo2PutRecord(coopown::PodBuf* b, int type, bool hasHand, int serial, int htype, int index, int cont, int itemSerial, bool withExtras)
{
    using namespace coopown;
    PodPutI32(b, 1); PodPutI32(b, type); PodPutI32(b, 3);
    PodPutStr(b, "n", 1); PodPutStr(b, "12-x.base", 9); PodPutI32(b, 0);
    const char one = 1; const int four = 4; char twelve[12] = { 0 }; char sixteen[16] = { 0 };
    PodPutI32(b, withExtras ? 1 : 0); if (withExtras) PodPutKeyRaw(b, "stealth", 7, &one, 1);
    PodPutI32(b, withExtras ? 1 : 0); if (withExtras) PodPutKeyRaw(b, "hunger", 6, &four, 4);
    PodPutI32(b, (hasHand ? 5 : 0) + (itemSerial ? 2 : 0) + 1);
    if (hasHand)
    {
        Mmo2PutKeyI(b, "handleTYPE", htype); Mmo2PutKeyI(b, "handleC", cont); Mmo2PutKeyI(b, "handleCS", cont + 1);
        Mmo2PutKeyI(b, "handleI", index); Mmo2PutKeyI(b, "handleS", serial);
    }
    if (itemSerial) { Mmo2PutKeyI(b, "item handleS", itemSerial); Mmo2PutKeyI(b, "item handleTYPE", 3); }
    PodPutKeyRaw(b, "char count", 10, &four, 4);
    PodPutI32(b, withExtras ? 1 : 0); if (withExtras) PodPutKeyRaw(b, "pos", 3, twelve, 12);
    PodPutI32(b, withExtras ? 1 : 0); if (withExtras) PodPutKeyRaw(b, "rot", 3, sixteen, 16);
    PodPutI32(b, withExtras ? 1 : 0); if (withExtras) PodPutKeyStr(b, "town", 4, "Hub", 3);
    PodPutI32(b, 0);
    PodPutI32(b, withExtras ? 1 : 0);
    if (withExtras) { PodPutStr(b, "items", 5); PodPutI32(b, 1); PodPutStr(b, "55-x.base", 9); PodPutBytes(b, twelve, 12); }
    PodPutI32(b, withExtras ? 1 : 0);
    if (withExtras) { PodPutStr(b, "i1", 2); PodPutStr(b, "i2", 2); PodPutBytes(b, twelve, 12); PodPutBytes(b, sixteen, 16); PodPutI32(b, 1); PodPutStr(b, "s", 1); }
}
/* mmo4 + fold (review-mmo4): the leave save's gate and slot (the harness start-up slot is never a target), the timed-autosave
   skip by the timer's name, the drain to the leave's own records, No cancelling a held exit, the continuation tied to its
   session link and world, and the phases */
/* mmo5: when the host leaves - joiner only, which sentence, the lost rule (link1's backoff), the window text, the fallback's
   timing, and the mmo4 LOW same-sender rule */
void t_mmo5_host_left()
{
    using namespace coopown;
    if (HostLeftDecide(true, true, true, false, true, false, false, 0) != kHlAnnounced) Fail("mmo5: SESSION_CLOSING on a joiner in a running world -> announced");
    if (HostLeftDecide(true, true, true, false, true, true, false, 0) != kHlAnnounced) Fail("mmo5: announced even before the link drops");
    if (HostLeftDecide(false, true, true, false, true, false, false, 9) != kHlNone) Fail("mmo5: never on the host");
    if (HostLeftDecide(true, false, true, false, true, false, false, 9) != kHlNone) Fail("mmo5: no running world -> nothing");
    if (HostLeftDecide(true, true, false, false, true, false, false, 9) != kHlNone) Fail("mmo5: the host never seen in this world -> nothing");
    if (HostLeftDecide(true, true, true, true, true, false, false, 9) != kHlNone) Fail("mmo5: this game is itself stopping -> nothing");
    /* ui1 (owner 2026-09-27): the link declared dead is the CONNECTION LOST edge at once - re-dials are logged only */
    if (HostLeftDecide(true, true, true, false, false, false, false, 0) != kHlLost) Fail("ui1: the link DOWN -> lost at once");
    if (HostLeftDecide(true, true, true, false, false, false, true, 9) != kHlLost) Fail("ui1: link1 re-dialling -> still lost");
    if (HostLeftDecide(true, true, true, false, false, true, false, 9) != kHlNone) Fail("mmo5: the link is up -> nothing");
    /* ui1 (ui-polish-audit 3.1; owner rulings 2026-09-27): the DISCONNECTED dialog's texts */
    if (std::string(HostLeftTitle(kHlAnnounced)) != "DISCONNECTED" || std::string(HostLeftTitle(kHlLost)) != "CONNECTION LOST") Fail("ui1: the dialog's two titles");
    if (std::string(HostLeftSentence(kHlAnnounced)) != "The host has ended the game."
        || std::string(HostLeftSentence(kHlLost)) != "Reconnecting to host..." || std::string(HostLeftSentence(kHlNone)) != "")
        Fail("ui1: the two sentences");
    if (HostLeftButtonEnabled(kHlSaving, false) || !HostLeftButtonEnabled(kHlSaved, false) || !HostLeftButtonEnabled(kHlSaveNone, false)
        || !HostLeftButtonEnabled(kHlSaveGaveUp, false) || HostLeftButtonEnabled(kHlSaved, true))
        Fail("ui1: EXIT GAME greyed while the save runs or the exit is under way, enabled once it settles - whatever the outcome");
    {
        std::string all = HostLeftSentence(kHlAnnounced) + std::string(HostLeftSentence(kHlLost)) + HostLeftBackLine() + HostLeftTitle(kHlAnnounced)
            + HostLeftTitle(kHlLost) + HostQuietLine();
        const int kinds[2] = { kHlAnnounced, kHlLost };
        const int states[4] = { kHlSaving, kHlSaved, kHlSaveGaveUp, kHlSaveNone };
        for (int k = 0; k < 2; ++k)
            for (int s = 0; s < 4; ++s) all += HostLeftFallbackLine(kinds[k], states[s], false, false) + HostLeftFallbackLine(kinds[k], states[s], true, false);
        const char* never[] = { "records", "saved", "not be saved", "did not finish", "progress", "[co-op]", "notebook" };
        for (size_t i = 0; i < sizeof never / sizeof never[0]; ++i)
            if (all.find(never[i]) != std::string::npos) Fail(std::string("ui1: a host-left text says '") + never[i] + "'");
    }
    if (HostLeftFallbackLine(kHlAnnounced, kHlSaved, false, false) != HostLeftFallbackLine(kHlAnnounced, kHlSaveGaveUp, false, false)
        || HostLeftFallbackLine(kHlAnnounced, kHlSaved, false, false) != HostLeftFallbackLine(kHlAnnounced, kHlSaveNone, false, false))
        Fail("ui1: the fallback line is the same whether the save finished, gave up or never ran");
    if (HostLeftFallbackLine(kHlAnnounced, kHlSaved, false, true) != "The host has ended the game.") Fail("ui1: no 'Kenshi will close' in a harness-driven game (it does not close)");
    if (HostLeftFallbackLine(kHlAnnounced, kHlSaved, false, false) != "The host has ended the game. Kenshi will close in 20 seconds.") Fail("ui1: the fallback's closing notice");
    if (HostLeftFallbackLine(kHlLost, kHlSaved, false, false).find(" close") != std::string::npos) Fail("ui1: a LOST host's fallback never says the game closes");
    if (HostLeftAutoExitDue(true, true, 999999, false, false)) Fail("ui1: a LOST host is re-dialled, never auto-exited");
    if (HostLeftFallbackLine(kHlLost, kHlSaveNone, false, false) != "Connection lost. Reconnecting to host...")
        Fail("ui1 review: a LOST host's fallback line says how to leave");
    if (HostLeftFallbackLine(kHlAnnounced, kHlSaved, false, false).find("Escape") != std::string::npos) Fail("ui1 review: DISCONNECTED's fallback closes on its own");
    if (!HostLeftLineRepeatDue(true, true, false, kHlLineRepeatMs) || HostLeftLineRepeatDue(true, true, false, kHlLineRepeatMs - 1)
        || HostLeftLineRepeatDue(false, true, false, 999999) || HostLeftLineRepeatDue(true, false, false, 999999) || HostLeftLineRepeatDue(true, true, true, 999999))
        Fail("ui1 review: a LOST host's fallback line repeats every 30 s - never while exiting, never on the dialog path, never for DISCONNECTED");
    if (std::string(HostLeftBackLine()) != "Reconnected to the host.") Fail("ui1: the came-back line");
    if (!HostLeftStartsSave(kHlAnnounced) || HostLeftStartsSave(kHlLost) || HostLeftStartsSave(kHlNone))
        Fail("ui1 follow-up: only DISCONNECTED saves at once; CONNECTION LOST starts no leave save (EXIT GAME saves on the way out)");
    if (!HostLeftButtonEnabled(kHlSaveNone, false)) Fail("ui1 follow-up: CONNECTION LOST's EXIT GAME is enabled from the start");
    /* ui1 (owner 2026-09-27, "connection trouble in three steps"): which step, from silence, the link and SESSION_CLOSING */
    if (std::string(HostQuietLine()) != "Connection problem - waiting for the host...") Fail("ui1: the step-2 notice's words");
    if (ConnTroubleStep(true, true, true, false, false, true, 0) != kCtNothing) Fail("ui1: the host heard just now -> nothing");
    if (ConnTroubleStep(true, true, true, false, false, true, kHostQuietMs - 1) != kCtNothing) Fail("ui1: under 10 s of silence -> nothing");
    if (ConnTroubleStep(true, true, true, false, false, true, kHostQuietMs) != kCtQuietNotice) Fail("ui1: 10 s of silence on a live link -> the small notice");
    if (ConnTroubleStep(true, true, true, false, false, false, 0) != kCtConnectionLost || ConnTroubleStep(true, true, true, false, false, false, 99999) != kCtConnectionLost)
        Fail("ui1: the link declared dead -> CONNECTION LOST, whatever the silence count");
    if (ConnTroubleStep(true, true, true, false, true, false, 99999) != kCtDisconnected || ConnTroubleStep(true, true, true, false, true, true, 0) != kCtDisconnected)
        Fail("ui1: SESSION_CLOSING -> DISCONNECTED, whatever the link");
    if (ConnTroubleStep(false, true, true, false, false, true, 99999) != kCtNothing || ConnTroubleStep(false, true, true, false, false, false, 0) != kCtNothing)
        Fail("ui1: the host never shows any of these");
    if (ConnTroubleStep(true, true, false, false, false, true, 99999) != kCtNothing || ConnTroubleStep(true, false, true, false, false, true, 99999) != kCtNothing
        || ConnTroubleStep(true, true, true, true, false, true, 99999) != kCtNothing)
        Fail("ui1: host never seen / no running world / this game stopping -> nothing");
    if (!HostLeftAutoExitDue(true, true, kHlAutoExitMs, false) || HostLeftAutoExitDue(true, true, kHlAutoExitMs - 1, false))
        Fail("mmo5: the fallback exits 20 s after the save settled, not before");
    if (HostLeftAutoExitDue(true, false, 999999, false)) Fail("mmo5: never while the save runs");
    if (HostLeftAutoExitDue(false, true, 999999, false)) Fail("mmo5: the window path never exits on its own");
    if (HostLeftAutoExitDue(true, true, 999999, true)) Fail("mmo5: never in a harness-driven game");
    int a = 0, b = 0;
    if (!LeaveCancelIgnored(true, &a, &a) || LeaveCancelIgnored(true, &a, &b) || LeaveCancelIgnored(false, 0, 0))
        Fail("mmo5: only the sender that raised the held quit is ignored");
}
/* mmo5 fold: the SESSION_CLOSING latch (cleared on link-up), acted on only where actionable, the came-back undo, the sender-0
   hold ignoring cancels, the stalled exit */
void t_mmo5_fold()
{
    using namespace coopown;
    if (coopgl::HostClosingFlagStep(0, coopgl::kHcReceived) != 1) Fail("mmo5 fold: SESSION_CLOSING raises the latch");
    if (coopgl::HostClosingFlagStep(1, coopgl::kHcLinkUp) != 0) Fail("mmo5 fold: a link UP clears the latch");
    if (coopgl::HostClosingFlagStep(1, coopgl::kHcSessionLeft) != 0 || coopgl::HostClosingFlagStep(1, coopgl::kHcNotActionable) != 0)
        Fail("mmo5 fold: leaving, or an unactionable arrival, clears it");
    if (coopgl::HostClosingFlagStep(1, 0) != 1 || coopgl::HostClosingFlagStep(0, 0) != 0) Fail("mmo5 fold: nothing else moves it");
    if (!HostLeftClosingActionable(true, true) || HostLeftClosingActionable(false, true) || HostLeftClosingActionable(true, false))
        Fail("mmo5 fold: acted on only in a running world where the host was seen");
    if (!HostLeftCameBack(kHlLost, false, true)) Fail("mmo5 fold: a lost host whose link is up again is back");
    if (HostLeftCameBack(kHlAnnounced, false, true)) Fail("mmo5 fold: an announced leave is final");
    if (HostLeftCameBack(kHlLost, true, true)) Fail("mmo5 fold: never once Exit was clicked");
    if (HostLeftCameBack(kHlLost, false, false) || HostLeftCameBack(kHlNone, false, true)) Fail("mmo5 fold: link down / no edge -> not back");
    int a = 0, b = 0;
    if (!LeaveCancelIgnored(true, 0, &a) || !LeaveCancelIgnored(true, 0, 0)) Fail("mmo5 fold: a sender-0 hold (the button) ignores every cancel");
    if (!LeaveCancelIgnored(true, &a, &a) || LeaveCancelIgnored(true, &a, &b) || LeaveCancelIgnored(false, 0, &a))
        Fail("mmo5 fold: a dialog's hold still takes another sender's No");
    if (!HostLeftExitStalled(true, true, false, kHlExitStallMs) || HostLeftExitStalled(true, true, false, kHlExitStallMs - 1))
        Fail("mmo5 fold: the button comes back 10 s after the save went idle, not before");
    if (HostLeftExitStalled(true, true, true, 999999) || HostLeftExitStalled(true, false, false, 999999) || HostLeftExitStalled(false, true, false, 999999))
        Fail("mmo5 fold: never while quitting, while the save runs, or when not exiting");
}
void t_mmo4_leave_save()
{
    using namespace coopown;
    /* T-251: the player path - a multiplayer world whose profile save folder is decided saves into that folder; no name rule */
    const std::string pf = "Holdout - Multiplayer - Beep";
    if (!LeaveSaveWanted(true, pf, true, false)) Fail("T-251: a multiplayer world, the profile's save folder, a running world -> leave save");
    if (!LeaveSaveWanted(true, "cooptest-mmo4", true, false) || !LeaveSaveWanted(true, "s2", true, false))
        Fail("T-251: any decided folder is saved - no cooptest-* name rule");
    if (LeaveSaveWanted(true, "", true, false)) Fail("T-251: no profile save folder -> no leave save");
    if (LeaveSaveWanted(false, pf, true, false) || LeaveSaveWanted(false, "", true, false)) Fail("T-251: single player -> never a leave save");
    if (LeaveSaveWanted(true, pf, false, false)) Fail("mmo4: no world loaded -> no leave save");
    if (LeaveSaveWanted(true, pf, true, true)) Fail("mmo4: already quitting -> no leave save");
    if (LeaveSaveTarget(pf) != pf || !LeaveSaveTarget("").empty())
        Fail("T-251: the target is the profile's save folder itself");
    /* fold item 3: the timer's name */
    if (!AutosaveSkip(true, "autosave0", true, true, false) || !AutosaveSkip(true, "autosave2", true, true, false))
        Fail("mmo4: the timed autosave (autosave<n>) is skipped in a co-op world with the records on");
    if (AutosaveSkip(true, "quicksave", true, true, false) || AutosaveSkip(true, "", true, true, false) || AutosaveSkip(true, "s2", true, true, false)
        || AutosaveSkip(true, "autosav", true, true, false) || AutosaveSkip(true, "Autosave0", true, true, false))
        Fail("mmo4 fold: a save not named autosave... (F5's quicksave, 0x47B530's caller) is never skipped");
    if (AutosaveSkip(false, "autosave0", true, true, false) || AutosaveSkip(true, "autosave0", false, true, false)
        || AutosaveSkip(true, "autosave0", true, false, false) || AutosaveSkip(true, "autosave0", true, true, true))
        Fail("mmo4: the autosave runs as today unless all hold; our own request is never skipped");
    /* fold item 7: the drain */
    if (!LeaveDrainDone(0, 10) || !LeaveDrainDone(11, 10) || LeaveDrainDone(10, 10) || LeaveDrainDone(3, 10))
        Fail("mmo4 fold: the drain waits for the records up to the begin's seq only");
    /* fold item 4: No cancels a held exit, the leave bit stays */
    if (LeaveCancelQuit(kLsContQuit) != 0 || LeaveCancelQuit(kLsContQuit | kLsContLeave) != kLsContLeave || LeaveCancelQuit(kLsContLeave) != kLsContLeave)
        Fail("mmo4 fold: a No clears only the pending quit");
    /* fold item 1: the continuation */
    int dl = -1, dq = -1;
    LeaveContinue(kLsContLeave, kLsWhyCompleted, 4, 4, &dl, &dq);
    if (dl != 1 || dq != 0) Fail("mmo4 fold: same session link -> leave");
    LeaveContinue(kLsContLeave, kLsWhyCompleted, 4, 6, &dl, &dq);
    if (dl != 0 || dq != 0) Fail("mmo4 fold: the session link changed -> no SessionLeave");
    LeaveContinue(kLsContLeave, kLsWhyTimeout, 4, 4, &dl, &dq);
    if (dl != 1) Fail("mmo4: a give-up still leaves the session it was asked about");
    LeaveContinue(kLsContQuit, kLsWhyCompleted, 4, 9, &dl, &dq);
    if (dl != 0 || dq != 1) Fail("mmo4: the quit resumes after the save (the link does not matter to it)");
    LeaveContinue(kLsContQuit, kLsWhyNotAccepted, 4, 4, &dl, &dq);
    if (dq != 1) Fail("mmo4: a give-up still resumes the quit");
    LeaveContinue(kLsContQuit | kLsContLeave, kLsWhyWorldChanged, 4, 4, &dl, &dq);
    if (dq != 0 || dl != 1) Fail("mmo4 fold: world-changed -> no resumed quit");
    LeaveContinue(kLsContQuit, kLsWhyStopping, 4, 4, &dl, &dq);
    if (dq != 0) Fail("mmo4 fold: already stopping -> the confirm path is not called again");
    LeaveContinue(0, kLsWhyCompleted, 4, 4, &dl, &dq);
    if (dl != 0 || dq != 0) Fail("mmo4: nothing asked -> nothing continues");
    /* the phases */
    int why = -1;
    if (LeaveSaveStep(kLsDrain, 0, 1, 0, 0, true, false, &why) != kLsRequest || why != kLsWhyNone) Fail("mmo4: drained -> request");
    if (LeaveSaveStep(kLsDrain, kLsDrainMs - 1, 0, 0, 0, true, false, &why) != kLsDrain) Fail("mmo4: still draining");
    if (LeaveSaveStep(kLsDrain, kLsDrainMs, 0, 0, 0, true, false, &why) != kLsRequest || why != kLsWhyDrainSlow) Fail("mmo4: a slow drain still requests");
    if (LeaveSaveStep(kLsRequest, 0, 0, 1, 0, true, false, &why) != kLsWait || why != kLsWhyNone) Fail("mmo4: accepted -> wait");
    if (LeaveSaveStep(kLsRequest, kLsRequestMs - 1, 0, 0, 0, true, false, &why) != kLsRequest) Fail("mmo4: not accepted yet -> retry");
    if (LeaveSaveStep(kLsRequest, kLsRequestMs, 0, 0, 0, true, false, &why) != kLsFinished || why != kLsWhyNotAccepted) Fail("mmo4: never accepted -> give up");
    if (LeaveSaveStep(kLsRequest, 0, 0, -2, 0, true, false, &why) != kLsFinished || why != kLsWhyRefused
        || LeaveSaveStep(kLsRequest, 0, 0, -1, 0, true, false, &why) != kLsFinished) Fail("mmo4: a refused request gives up at once");
    if (LeaveSaveStep(kLsWait, 5, 0, 0, 1, true, false, &why) != kLsFinished || why != kLsWhyCompleted) Fail("mmo4: the completion edge");
    if (LeaveSaveStep(kLsWait, 5, 0, 0, 2, true, false, &why) != kLsFinished || why != kLsWhyFailed) Fail("mmo4: finished with no quick.save");
    if (LeaveSaveStep(kLsWait, 5, 0, 0, 3, true, false, &why) != kLsFinished || why != kLsWhyStale) Fail("mmo4 fold: an older quick.save is not a completion");
    if (LeaveSaveStep(kLsWait, 5, 0, 0, 0, true, false, &why) != kLsWait || LeaveSaveStep(kLsWait, 5, 0, 0, -1, true, false, &why) != kLsWait) Fail("mmo4: still saving");
    if (LeaveSaveStep(kLsWait, kLsWaitMs, 0, 0, 0, true, false, &why) != kLsFinished || why != kLsWhyTimeout) Fail("mmo4: not finished in time -> give up");
    /* fold items 1 and 2 in the step */
    if (LeaveSaveStep(kLsDrain, 0, 1, 0, 0, false, false, &why) != kLsFinished || why != kLsWhyWorldChanged
        || LeaveSaveStep(kLsWait, 0, 0, 0, 1, false, false, &why) != kLsFinished || why != kLsWhyWorldChanged)
        Fail("mmo4 fold: a load/reset since the begin -> world-changed");
    if (LeaveSaveStep(kLsDrain, 0, 1, 0, 0, true, true, &why) != kLsFinished || why != kLsWhyStopping
        || LeaveSaveStep(kLsRequest, 0, 0, 1, 0, true, true, &why) != kLsFinished || why != kLsWhyStopping)
        Fail("mmo4 fold: quitting / teardown in drain or request -> give up without requesting");
    if (LeaveSaveStep(kLsWait, 5, 0, 0, 1, true, true, &why) != kLsFinished || why != kLsWhyCompleted)
        Fail("mmo4 fold: a save already requested is still watched to its edge while stopping");
    if (LeaveSaveStep(kLsIdle, 999999, 1, 1, 1, false, true, &why) != kLsIdle || LeaveSaveStep(kLsFinished, 0, 1, 1, 1, true, false, &why) != kLsFinished)
        Fail("mmo4: idle and finished do not move");
    if (std::string(LeaveSaveWhyName(kLsWhyCompleted)) != "completed" || std::string(LeaveSaveWhyName(kLsWhyWorldChanged)) != "world-changed"
        || std::string(LeaveSaveWhyName(99)) != "none") Fail("mmo4: reason names");
}
/* T-251 part 2: the save right after a new character is made - the editor-close edge, the armed trigger that no gate consumes,
   the re-arm on a give-up, the leave save not wanted while the editor is open, and the created checkpoint tag */
void t_t251p2_created_save()
{
    using namespace coopown;
    const std::string pf = "Holdout - Multiplayer - Beep";
    if (LeaveSaveWanted(true, pf, true, false, true)) Fail("T-251 part 2: no leave / Exit save while the character editor is open");
    if (!LeaveSaveWanted(true, pf, true, false, false) || !LeaveSaveWanted(true, pf, true, false))
        Fail("T-251 part 2: the editor closed (or not asked) -> the leave save as before");
    {
        CreatedSave s;
        if (CreatedEdge(&s, 0ULL, 0, false, 3) != kCrEdgeNone || s.pending != 0 || s.open != 0) Fail("T-251 part 2: no editor, no edge");
        if (CreatedEdge(&s, 0x1000ULL, 0, true, 3) != kCrEdgeNone || s.open != 1 || s.worldAtOpen != 3) Fail("T-251 part 2: the new-game editor opens");
        if (CreatedEdge(&s, 0x1000ULL, 0, true, 3) != kCrEdgeNone || s.pending != 0) Fail("T-251 part 2: still open - nothing yet");
        if (CreatedEdge(&s, 0ULL, 0, false, 3) != kCrEdgeArm || s.pending != 1 || s.worldArmed != 3 || s.open != 0)
            Fail("T-251 part 2: the new-game editor closes in the same world with the AI on -> ARMED");
        if (CreatedEdge(&s, 0ULL, 0, false, 3) != kCrEdgeNone) Fail("T-251 part 2: one close is one edge");
    }
    {
        CreatedSave s; CreatedEdge(&s, 0x2000ULL, 1, true, 5);
        if (CreatedEdge(&s, 0ULL, 0, false, 5) != kCrEdgeInGame || s.pending != 0) Fail("T-251 part 2: an in-game editor (mode 1) never arms");
    }
    {
        CreatedSave s; CreatedEdge(&s, 0x1000ULL, 0, true, 5); CreatedEdge(&s, 0x1000ULL, 0, true, 6);
        if (CreatedEdge(&s, 0ULL, 0, false, 6) != kCrEdgeWorldChanged || s.pending != 0) Fail("T-251 part 2: a load between opening and close never arms");
        CreatedSave n; CreatedEdge(&n, 0x1000ULL, 0, true, 5); CreatedEdge(&n, 0x3000ULL, 0, true, 6);
        if (CreatedEdge(&n, 0ULL, 0, false, 6) != kCrEdgeArm || n.worldArmed != 6) Fail("T-251 part 2: another window is a new opening");
    }
    {
        CreatedSave s; CreatedEdge(&s, 0x1000ULL, 0, true, 5);
        if (CreatedEdge(&s, 0ULL, 0, true, 5) != kCrEdgeAiOff || s.pending != 0) Fail("T-251 part 2: the AI still off -> not armed");
    }
    {
        CreatedSave s; CreatedEdge(&s, 0x1000ULL, 0, true, 7); CreatedEdge(&s, 0ULL, 0, false, 7);
        for (int i = 0; i < 3; ++i)
            if (CreatedPendingStep(s, 7, false, false) != kCrWait || s.pending != 1) Fail("T-251 part 2: not wanted yet -> waits, stays armed");
        if (CreatedPendingStep(s, 7, false, true) != kCrStartNow) Fail("T-251 part 2: wanted -> the save starts");
        if (CreatedPendingStep(s, 8, false, true) != kCrDropWorld) Fail("T-251 part 2: a world change drops it");
        if (CreatedPendingStep(s, 7, true, true) != kCrDropStopping) Fail("T-251 part 2: the game stopping drops it");
    }
    {
        CreatedSave s; s.running = 1;
        if (CreatedSaveEnded(&s, kLsWhyCompleted) != kCrOutDone || s.running != 0 || s.pending != 0) Fail("T-251 part 2: completed -> done");
        s.running = 1; s.waitLogged = 1;
        if (CreatedSaveEnded(&s, kLsWhyNotAccepted) != kCrOutRearmed || s.pending != 1 || s.waitLogged != 0 || s.running != 0)
            Fail("T-251 part 2: another save held the save slot past 30 s -> RE-ARMED");
        CreatedSave d; d.running = 1;
        if (CreatedSaveEnded(&d, kLsWhyWorldChanged) != kCrOutDropped || CreatedSaveEnded(&d, kLsWhyStopping) != kCrOutDropped || d.pending != 0)
            Fail("T-251 part 2: world-changed / stopping -> dropped");
        if (CreatedSaveEnded(&d, kLsWhyRefused) != kCrOutGaveUp || CreatedSaveEnded(&d, kLsWhyTimeout) != kCrOutGaveUp
            || CreatedSaveEnded(&d, kLsWhyFailed) != kCrOutGaveUp || CreatedSaveEnded(&d, kLsWhyStale) != kCrOutGaveUp || d.pending != 0)
            Fail("T-251 part 2: other give-ups end it without a re-arm");
    }
    {   /* T-251 review fold L1: the third not-accepted give-up ENDS the trigger */
        CreatedSave s; CreatedEdge(&s, 0x1000ULL, 0, 1, 9); CreatedEdge(&s, 0ULL, 0, 0, 9);
        s.running = 1;
        if (CreatedSaveEnded(&s, kLsWhyNotAccepted) != kCrOutRearmed || s.pending != 1) Fail("T-251 fold L1: the first not-accepted re-arms");
        s.pending = 0; s.running = 1;
        if (CreatedSaveEnded(&s, kLsWhyNotAccepted) != kCrOutRearmed || s.pending != 1) Fail("T-251 fold L1: the second not-accepted re-arms");
        s.pending = 0; s.running = 1;
        if (CreatedSaveEnded(&s, kLsWhyNotAccepted) != kCrOutTriesSpent || s.pending != 0 || s.running != 0)
            Fail("T-251 fold L1: the third re-arm ENDS the trigger (3 tries without a save)");
        CreatedEdge(&s, 0x1000ULL, 0, 1, 9); CreatedEdge(&s, 0ULL, 0, 0, 9);
        if (s.tries != 0 || s.pending != 1) Fail("T-251 fold L1: a new arm starts the count again");
        s.pending = 0; s.running = 1;
        if (CreatedSaveEnded(&s, kLsWhyNotAccepted) != kCrOutRearmed) Fail("T-251 fold L1: after a new arm the first not-accepted re-arms");
    }
    {   /* T-251 review fold L2: an unknown AI-off byte (-1) at the new-game close is NOT YET */
        CreatedSave s; CreatedEdge(&s, 0x1000ULL, 0, 1, 5);
        if (CreatedEdge(&s, 0ULL, 0, -1, 5) != kCrEdgeNotYet || s.open != 1 || s.pending != 0) Fail("T-251 fold L2: AI-off unknown -> not yet, the close kept");
        if (CreatedEdge(&s, 0ULL, 0, -1, 5) != kCrEdgeNotYet) Fail("T-251 fold L2: still unknown -> still not yet");
        if (CreatedEdge(&s, 0ULL, 0, 0, 5) != kCrEdgeArm || s.pending != 1 || s.open != 0) Fail("T-251 fold L2: read next frame, AI on -> ARMED");
        CreatedSave a; CreatedEdge(&a, 0x1000ULL, 0, 1, 5); CreatedEdge(&a, 0ULL, 0, -1, 5);
        if (CreatedEdge(&a, 0ULL, 0, 1, 5) != kCrEdgeAiOff || a.pending != 0) Fail("T-251 fold L2: read next frame, AI still off -> not armed");
        CreatedSave g; CreatedEdge(&g, 0x2000ULL, 1, 1, 5);
        if (CreatedEdge(&g, 0ULL, 0, -1, 5) != kCrEdgeInGame) Fail("T-251 fold L2: an in-game editor is decided without the AI byte");
        CreatedSave w; CreatedEdge(&w, 0x1000ULL, 0, 1, 5);
        if (CreatedEdge(&w, 0ULL, 0, -1, 6) != kCrEdgeWorldChanged) Fail("T-251 fold L2: a world change is decided without the AI byte");
    }
    {
        restoreguard::CheckpointTag ct;
        if (restoreguard::CheckpointTagCreated(90ULL, 17ULL) != "c-s90-o17") Fail("T-251 part 2: the created tag spelling");
        if (!restoreguard::ParseCheckpointTag("c-s90-o17", &ct) || ct.kind != restoreguard::kCpCreated || ct.seq != 90ULL || ct.own != 17ULL)
            Fail("T-251 part 2: a created tag parses");
        if (restoreguard::ParseCheckpointTag("c-s090-o17", &ct) || restoreguard::ParseCheckpointTag("c-s90-o17.part", &ct))
            Fail("T-251 part 2: one spelling only; a .part folder is never a tag");
        std::vector<std::string> nm;
        nm.push_back(restoreguard::CheckpointTagLeave(10ULL, 1ULL)); nm.push_back(restoreguard::CheckpointTagLeave(20ULL, 2ULL));
        nm.push_back(restoreguard::CheckpointTagCreated(5ULL, 1ULL)); nm.push_back(restoreguard::CheckpointTagCreated(6ULL, 3ULL));
        const std::vector<std::string> out = restoreguard::PruneSelect(nm, 1);
        if (out.size() != 2 || std::find(out.begin(), out.end(), std::string("l-s10-o1")) == out.end()
            || std::find(out.begin(), out.end(), std::string("c-s5-o1")) == out.end())
            Fail("T-251 part 2: created tags are pruned in their own group");
    }
}
/* mmo8a: pp.build - round trip (sorted, bytes exact), a bad record refused whole, a retired (removed) row dropped, a merged
   update keeping the rows the registry no longer knows, a live row replacing the file's */
void t_mmo8a_build_record()
{
    using namespace coopown;
    coopbuild::BuildMsg m;
    m.kind = coopbuild::kBuildPlace; m.key = "43,11:1459:120,30,-40"; m.sid = "1459-gamedata.base";
    m.pos[0] = 1.0f; m.pos[1] = 2.0f; m.pos[2] = 3.0f; m.rot[0] = 1.0f; m.rot[1] = 0.0f; m.rot[2] = 0.0f; m.rot[3] = 0.0f;
    m.progress = 0.27f; m.needed = 1.0f; m.nonce = 0x1234u; m.ownerSlot = coopbuild::kBuildOwnerSender;
    coopbuild::BuildMsg st;
    st.kind = coopbuild::kBuildState; st.key = m.key; st.progress = 0.27f; st.needed = 1.0f; st.nMats = 0;
    std::vector<char> pb, sb;
    if (!coopbuild::EncodeBuild(&pb, m) || !coopbuild::EncodeBuild(&sb, st)) { Fail("mmo8a: the PLACE / STATE encode"); return; }
    BuildRow a; a.key = m.key; a.place.assign(pb.begin(), pb.end()); a.state.assign(sb.begin(), sb.end());
    BuildRow z; z.key = "10,10:first"; z.place = "p"; z.state = "s";
    std::vector<BuildRow> rows; rows.push_back(a); rows.push_back(z);
    std::vector<char> b; EncodeBuildRec(rows, &b);
    std::vector<BuildRow> back;
    if (!DecodeBuildRec(b, &back) || back.size() != 2 || back[0].key != z.key || back[1].key != a.key || back[1].place != a.place || back[1].state != a.state)
        Fail("mmo8a: round trip (sorted by key, bytes exact)");
    else
    {
        coopbuild::BuildMsg m2;
        if (coopbuild::DecodeBuild(back[1].place.data(), back[1].place.size(), &m2) != coopbuild::kBuildDecodeOk || m2.key != m.key || m2.nonce != m.nonce
            || m2.progress != m.progress || m2.kind != coopbuild::kBuildPlace)
            Fail("mmo8a: the row's PLACE decodes back to the piece");
    }
    { std::vector<char> c(b.begin(), b.end() - 1); if (DecodeBuildRec(c, &back) || !back.empty()) Fail("mmo8a: a cut record is refused whole"); }
    { std::vector<char> c(b); c.push_back(0); if (DecodeBuildRec(c, &back)) Fail("mmo8a: trailing bytes are refused"); }
    { std::vector<BuildRow> d; d.push_back(z); d.push_back(z); std::vector<char> c; EncodeBuildRec(d, &c); if (DecodeBuildRec(c, &back)) Fail("mmo8a: a key twice is refused"); }
    { std::vector<BuildRow> d; BuildRow e = z; e.place.clear(); d.push_back(e); std::vector<char> c; EncodeBuildRec(d, &c); if (DecodeBuildRec(c, &back)) Fail("mmo8a: an empty PLACE is refused"); }
    { std::vector<char> c; EncodeMoney(5, &c); if (DecodeBuildRec(c, &back)) Fail("mmo8a: another record is not pp.build"); }
    { std::vector<char> c; EncodeBuildRec(std::vector<BuildRow>(), &c); if (!DecodeBuildRec(c, &back) || !back.empty()) Fail("mmo8a: an empty record round trips"); }
    if (BuildRecKey() != "pp.build" || IsSquadKey(BuildRecKey())) Fail("mmo8a: the key is pp.build and is not a squad record");
    /* the merge */
    BuildRow u; u.key = "20,20:unloaded"; u.place = "pu"; u.state = "su";   /* in the file, not in the registry (its zone never seen) */
    BuildRow gone; gone.key = "30,30:gone"; gone.place = "pg"; gone.state = "sg";   /* removed here */
    std::vector<BuildRow> file; file.push_back(u); file.push_back(gone); file.push_back(a);
    BuildRow a2 = a; a2.state = "newer";
    BuildRow n; n.key = "40,40:new"; n.place = "pn"; n.state = "sn";
    std::vector<BuildRow> live; live.push_back(a2); live.push_back(n);
    std::set<std::string> retired; retired.insert(gone.key); retired.insert("99,99:never-in-the-file");
    std::vector<BuildRow> out; int dropped = -1;
    MergeBuildRows(file, live, retired, &out, &dropped);
    if (out.size() != 3 || out[0].key != u.key || out[1].key != n.key || out[2].key != a.key || out[2].state != "newer" || dropped != 1)
        Fail("mmo8a: the merge keeps the unknown row, drops the removed one, adds the new one and takes the live state");
    std::set<std::string> both; both.insert(a.key);
    MergeBuildRows(file, live, both, &out, &dropped);
    if (out.size() != 4 || dropped != 0) Fail("mmo8a: a key both live and retired is live (a new piece where a removed one stood)");
    MergeBuildRows(file, std::vector<BuildRow>(), std::set<std::string>(), &out, &dropped);
    if (out.size() != 3 || dropped != 0) Fail("mmo8a: an empty registry keeps every file row");
}
/* mmo8a2 (review-mmo8a H1, M1, M2): the world generation scopes pp.build's rows, a restore settles over 3 consecutive safe points,
   and a retirement owed to the record outlives the registry's 60 s sweep until the writer takes a record that drops it */
/* ui5 (panel-mockups.md, owner-approved 2026-09-27): every panel screen and dialog is sized to its own rows - at the two
   client sizes the harness shoots (1280x720, 1920x1080) no row overflows the screen's inner height, no screen outgrows the
   rectangle the size guard accepted, each window is exactly its rows + margin + frame and centred, the bottom button row is
   last, and every label shares its box's vertical centre; plus the one-title captions, the explicit CAN'T JOIN titles, the
   error box's height estimate and the approved texts kept for later efforts. */
void t_ui5_panel_screens()
{
    static const int kRes[2][2] = { { 1280, 720 }, { 1920, 1080 } };
    for (int k = 0; k < 2; ++k)
    {
        const int pw = kRes[k][0], ph = kRes[k][1];
        const std::string at = I(pw) + "x" + I(ph);
        const coopui::PanelGeom g = coopui::PanelGeomIn(pw, ph);
        const int innerMaxH = g.maxRect.h - coopui::kWindowCXChromeH - 2 * g.padY;
        const int gap = coopui::PanelGapOf(g.rowH);
        if (!coopui::PanelWindowAllowed(g.maxRect.w, g.maxRect.h)) Fail("ui5: the largest panel is refused at " + at);
        if (g.rowH < coopui::kPanelMinRowH) Fail("ui5: a row of " + I(g.rowH) + " px at " + at);
        if (2 * coopui::PanelBottomBtnW(g.innerW) + gap > g.innerW) Fail("ui5: BACK and the action overlap at " + at);
        for (int lay = 0; lay < coopui::kLayCount; ++lay)
        {
            const std::string where = "layout " + I(lay) + " at " + at;
            const int innerH = coopui::PanelLayoutInnerH(lay, g.rowH);
            if (coopui::PanelLayoutHalves(lay) > coopui::kPanelMaxHalves) Fail("ui5: more than thirteen rows on " + where);
            if (innerH > innerMaxH) Fail("ui5: " + where + " is " + I(innerH) + " px, over the inner " + I(innerMaxH));
            const coopui::PanelRect r = coopui::PanelScreenRect(g, ph, lay, coopui::kWindowCXChromeH);
            if (r.h != innerH + 2 * g.padY + coopui::kWindowCXChromeH) Fail("ui5: the window is not sized to its rows on " + where);
            if (r.left < 0 || r.top < 0 || r.left + r.w > pw || r.top + r.h > ph) Fail("ui5: the window leaves the screen on " + where);
            if (r.top != (ph - r.h) / 2 || r.w != g.maxRect.w) Fail("ui5: the window is not centred at the panel's width on " + where);
            int n = 0;
            const coopui::PanelSlot* s = coopui::PanelRowsOf(lay, &n);
            if (n < 1 || s[n - 1].slot != coopui::kSlotButtons) Fail("ui5: the bottom button row is not last on " + where);
            int end = 0;
            for (int i = 0; i < n; ++i)
            {
                if (s[i].slot == coopui::kSlotGap) continue;
                const coopui::PanelBand b = coopui::PanelSlotBand(lay, s[i].slot, g.rowH);
                if (b.top < end || b.h < coopui::kPanelMinRowH || b.top + b.h > innerH)
                    Fail("ui5: slot " + I(s[i].slot) + " (" + I(b.top) + "+" + I(b.h) + ") overflows or overlaps on " + where);
                end = b.top + b.h;
                const int sl = s[i].slot;
                if (sl == coopui::kSlotNameRow || sl == coopui::kSlotPortRow || sl == coopui::kSlotAddrRow || sl == coopui::kSlotDlgName
                    || sl == coopui::kSlotHomeAddr || sl == coopui::kSlotPwRow)   /* T-290: PASSWORD, SHOW / HIDE at the right */
                {
                    const int reserve = (sl == coopui::kSlotAddrRow || sl == coopui::kSlotHomeAddr || sl == coopui::kSlotPwRow) ? coopui::PanelSideBtnW(g.innerW) + gap : 0;
                    const coopui::PanelPairRect p = coopui::PanelLabelBoxIn(g.innerW, gap, b, sl == coopui::kSlotPortRow ? coopui::PanelPortBoxW(g.innerW) : 0, reserve);
                    /* the label and the box: the same band, so the same vertical centre; side by side; inside the row */
                    if (2 * p.top + p.h != 2 * b.top + b.h || p.h != b.h) Fail("ui5: a label is not level with its box on " + where);
                    if (p.labelX + p.labelW >= p.boxX || p.boxW < coopui::kPanelMinFieldW || p.boxX + p.boxW + reserve > g.innerW)
                        Fail("ui5: a label / box row does not fit across on " + where);
                }
            }
            if (end != innerH) Fail("ui5: the last row does not end at the inner height on " + where);
        }
        if (coopui::PanelLayoutInnerH(coopui::kLayNewWorld, g.rowH) >= coopui::PanelLayoutInnerH(coopui::kLayHost, g.rowH)
            || coopui::PanelLayoutInnerH(coopui::kLayLanding, g.rowH) >= coopui::PanelLayoutInnerH(coopui::kLayOptions, g.rowH))
            Fail("ui5: the dialogs / MULTIPLAYER are not shorter than the long screens at " + at);
    }
    if (coopui::PanelLayoutOf(0, 0) != coopui::kLayLanding || coopui::PanelLayoutOf(3, 0) != coopui::kLayNewWorld
        || coopui::PanelLayoutOf(4, 0) != coopui::kLayDeleteWorld || coopui::PanelLayoutOf(7, 0) != coopui::kLayProfiles
        || coopui::PanelLayoutOf(7, 1) != coopui::kLayNewProfile || coopui::PanelLayoutOf(7, 2) != coopui::kLayDeleteProfile
        || coopui::PanelLayoutOf(0, 0, 1) != coopui::kLayLanding || coopui::PanelLayoutOf(1, 0, 1) != coopui::kLayHostHint   /* T-201 N1b */
        || coopui::PanelLayoutOf(1, 0, 0) != coopui::kLayHost || coopui::PanelLayoutOf(2, 0, 1) != coopui::kLayJoinHint
        || coopui::PanelLayoutOf(2, 0, 0) != coopui::kLayJoin || coopui::PanelLayoutOf(8, 0) != coopui::kLayPassword)   /* T-290 */
        Fail("ui5: the screen -> layout map");
    /* one title, the window's caption */
    if (coopui::PanelScreenTitle(0, 0, 0, "", "") != "MULTIPLAYER" || coopui::PanelScreenTitle(1, 0, 1, "", "") != "HOST GAME"
        || coopui::PanelScreenTitle(2, 0, 0, "", "") != "JOIN GAME" || coopui::PanelScreenTitle(3, 0, 1, "", "") != "NEW WORLD"
        || coopui::PanelScreenTitle(4, 0, 1, "", "") != "DELETE WORLD?" || coopui::PanelScreenTitle(5, 0, 1, "World-1", "") != "HOSTING: World-1"
        || coopui::PanelScreenTitle(6, 0, 1, "", "World-1") != "GAME OPTIONS: World-1"
        || coopui::PanelScreenTitle(7, 0, 1, "World-1", "") != "PROFILES: World-1" || coopui::PanelScreenTitle(7, 0, 0, "World-1", "") != "PROFILES"
        || coopui::PanelScreenTitle(7, 1, 1, "World-1", "") != "NEW PROFILE" || coopui::PanelScreenTitle(7, 2, 1, "World-1", "") != "DELETE PROFILE?"
        || coopui::PanelScreenTitle(8, 0, 0, "", "") != "PASSWORD REQUIRED")   /* T-290 */
        Fail("ui5: a screen title");
    if (coopui::PanelHomeAddrLine("192.168.1.20", 7777) != "192.168.1.20:7777"
        || coopui::PanelHomeAddrLine("", 7777) != "Couldn't find your local IP address.")
        Fail("ui5: the LOCAL ADDRESS value: " + coopui::PanelHomeAddrLine("192.168.1.20", 7777));
    /* the error box: explicit CAN'T JOIN titles, the rest unchanged */
    if (coopui::NoticeTitle(coopui::NameTakenText("Anna")) != "CAN'T JOIN"
        || coopui::NoticeTitle("Your game could not read its own mod list, so it can't be checked against the host's.") != "CAN'T JOIN"
        || coopui::NoticeTitle("The multiplayer menu couldn't open.") != "MULTIPLAYER"
        || coopui::NoticeTitle("Profile deleted. Its save couldn't be moved to the Recycle Bin and is still at C:\\x.") != "MULTIPLAYER"
        || coopui::NoticeTitle(coopui::kSaveNotHereText) != "CAN'T LOAD" || coopui::NoticeTitle(coopui::kSaveNoBackupText) != "CAN'T LOAD"
        || coopui::NoticeTitle(coopui::SaveDamagedText("3 hours ago")) != "MULTIPLAYER"
        || coopui::NoticeTitle("You could not load that save. x") != "CAN'T LOAD")
        Fail("ui5: an error box title");
    if (coopui::NoticeLinesFor("OK", 40) != 1 || coopui::NoticeLinesFor("a\nb", 40) != 2
        || coopui::NoticeLinesFor("aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd", 21) != 2
        || coopui::NoticeBoxH(std::string(400, 'x'), 400) <= coopui::NoticeBoxH("OK", 400))
        Fail("ui5: the error box height estimate");
    /* ui6 (panel-mockups.md sections 10-11) - EACH BOX IS ITS TEXT, ONE BLANK LINE, ITS BUTTONS (was ui5b's spare line): at every
       width the boxes open at, the client of the box NoticeBoxH sizes lays out (coopui::NoticeLayoutIn, what ui.cpp uses) as the
       estimate's lines x 18 px, exactly one 18 px line, the button row and the padding - for the long MOD MISMATCH text too. */
    {
        std::vector<std::string> bt;
        bt.push_back(coopui::IdBoxText(1));
        bt.push_back(coopui::IdBoxText(2));
        bt.push_back("Mod mismatch: the host uses mods you don't have, or in a different order. Missing: Dark UI, Recruit Prisoners,"
                     " Reactive World, Better Than Nothing Animations, Tougher Bandits, More Hair. Extra: Faster Walking, Compact"
                     " Inventory. Use the same mods as the host, in the same order, then restart Kenshi and join again.");
        bt.push_back(coopui::kSaveNotHereText);
        const int bw[] = { 380, 460, 540, 685, 760 };
        for (size_t t = 0; t < bt.size(); ++t)
            for (int k = 0; k < 5; ++k)
            {
                const int nh = coopui::NoticeBoxH(bt[t], bw[k]);
                const int H = nh - coopui::kNoticeFramePx;
                const coopui::NoticeLayout lay = coopui::NoticeLayoutIn(H);
                const int lines = coopui::NoticeLinesFor(bt[t], (bw[k] - 22) / coopui::kNoticeCharW);
                if (lay.textY != coopui::kNoticePadPx || lay.textH != lines * coopui::kNoticeLineH + coopui::kNoticeSparePx   /* review-ui6 #2: the 9 px wrap guard */
                    || lay.btnY - (lay.textY + lay.textH) != coopui::kNoticeLineH
                    || lay.btnH != coopui::kNoticeBtnH || lay.btnY + lay.btnH + coopui::kNoticePadPx != H)
                    Fail("ui6: a box is not text + one blank line + buttons at width " + I(bw[k]) + " (text " + I((int)t) + ")");
            }
        if (coopui::NoticeBoxH("OK", 380) < 120) Fail("ui6: a one-line box is below the 120 px the builders refuse under");
        /* ui7 - THE MEASURED BOX (ui.cpp BoxFitToText; the measurement itself is MyGUI's and cannot run offline): for every
           measured text height and line height, the client NoticeClientHFor sizes lays out (NoticeLayoutFor) as the text,
           exactly ONE measured line blank, the button row and the padding - and NoticeLayoutIn is NoticeLayoutFor at 18 px. */
        for (int lh = 8; lh <= 48; ++lh)
            for (int th = 0; th <= 600; th += 7)
            {
                const int H = coopui::NoticeClientHFor(th, lh);
                const coopui::NoticeLayout ml = coopui::NoticeLayoutFor(H, lh);
                const int want = th < lh ? lh : th;
                if (ml.textY != coopui::kNoticePadPx || ml.textH != want || ml.btnY - (ml.textY + ml.textH) != lh
                    || ml.btnH != coopui::kNoticeBtnH || ml.btnY + ml.btnH + coopui::kNoticePadPx != H)
                    Fail("ui7: a measured box is not text + one measured blank line + buttons (lineH " + I(lh) + ", textH " + I(th) + ")");
            }
        for (int H = 20; H <= 400; H += 13)
        {
            const coopui::NoticeLayout a = coopui::NoticeLayoutIn(H), b = coopui::NoticeLayoutFor(H, coopui::kNoticeLineH);
            const coopui::NoticeLayout z = coopui::NoticeLayoutFor(H, 0);
            if (a.textY != b.textY || a.textH != b.textH || a.btnY != b.btnY || a.btnH != b.btnH
                || z.textH != b.textH || z.btnY != b.btnY)
                Fail("ui7: NoticeLayoutIn is not NoticeLayoutFor at kNoticeLineH (client " + I(H) + ")");
        }
        if (coopui::NoticeClientHFor(0, 0) != coopui::NoticeClientHFor(coopui::kNoticeLineH, coopui::kNoticeLineH))
            Fail("ui7: an unmeasured line height is not the fallback's 18 px line");
        if (coopui::kNoticeLineH != coopui::kPanelTextLinePx) Fail("ui6: the box's line is not the panel's 18 px line");
        if (coopui::kNoticeCharW < 9) Fail("ui5b: the box's character estimate is below 9 px");
        {
            const coopui::NoticeLayout sq = coopui::NoticeLayoutIn(40);   /* cut short by a tiny screen: one line of text kept */
            if (sq.textY != coopui::kNoticePadPx || sq.textH < coopui::kNoticeLineH || sq.btnY < sq.textY)
                Fail("ui6: the squeezed box layout");
        }
    }
    /* ui6 (decision 42) - THE STRIPS THAT BLOCK THE TITLE MENU AROUND A BOX: every pixel of the title art is covered exactly once
       by the strips and the box together (so no strip is under the box, whatever MyGUI's pick order, and no gap lets a click
       through to the menu), for boxes inside, on the edges, past the edges, empty and off the screen. */
    {
        const int par[2][2] = { { 40, 30 }, { 33, 17 } };
        const int hole[9][4] = { { 10, 5, 12, 8 }, { 0, 0, 40, 30 }, { -5, -5, 20, 20 }, { 30, 20, 30, 30 }, { 0, 3, 40, 4 },
                                 { 7, 0, 3, 30 }, { 12, 9, 0, 0 }, { 50, 50, 5, 5 }, { 1, 1, 1, 1 } };
        int bad = 0;
        for (int p = 0; p < 2 && bad == 0; ++p)
            for (int h = 0; h < 9 && bad == 0; ++h)
            {
                const int pw = par[p][0], ph = par[p][1];
                const int l = hole[h][0], t = hole[h][1], w = hole[h][2], hh = hole[h][3];
                int r[16];
                const int n = coopui::BoxBlockStrips(pw, ph, l, t, w, hh, r);
                int nonEmpty = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (r[4 * i] < 0 || r[4 * i + 1] < 0 || r[4 * i + 2] < 0 || r[4 * i + 3] < 0
                        || r[4 * i] + r[4 * i + 2] > pw || r[4 * i + 1] + r[4 * i + 3] > ph) bad = 1;
                    if (r[4 * i + 2] > 0 && r[4 * i + 3] > 0) ++nonEmpty;
                }
                if (n != nonEmpty) bad = 1;
                for (int y = 0; y < ph && bad == 0; ++y)
                    for (int x = 0; x < pw && bad == 0; ++x)
                    {
                        int cov = (x >= l && x < l + w && y >= t && y < t + hh) ? 1 : 0;
                        for (int i = 0; i < 4; ++i)
                            if (x >= r[4 * i] && x < r[4 * i] + r[4 * i + 2] && y >= r[4 * i + 1] && y < r[4 * i + 1] + r[4 * i + 3]) ++cov;
                        if (cov != 1) bad = 1;
                    }
                if (bad != 0) Fail("ui6: the box-block strips do not tile the title art around box " + I(h) + " in parent " + I(p));
            }
    }
    /* ui5c: the areas below are CEILINGS now - on screen each is as tall as its live text (the ui5c block after this one).
       ui5b - NO DEAD SPACE: each status / text area at 1280x720 is the most lines its screen shows (the longest sentences
       that can be up together, wrapped at kPanelTextCharW) plus one half-row - HOSTING's one row more (review LOW) - and no
       more; MULTIPLAYER's buttons are the bottom row's height, HOST GAME straight under PLAYER NAME. */
    {
        const coopui::PanelGeom g = coopui::PanelGeomIn(1280, 720);
        const int per = g.innerW / coopui::kPanelTextCharW;
        std::vector<int> lay, slot, extra;
        std::vector<std::string> txt;
        /* JOIN GAME's status: its ceiling keeps room for six lines at the smallest row height (the ui5c check below), more than its
           longest status now needs at 1280x720; the area itself follows its text, so the spare is not on screen */
        lay.push_back(coopui::kLayJoin); slot.push_back(coopui::kSlotStatus); extra.push_back(3);
        txt.push_back(std::string("Connection failed: the host isn't responding. Check the address and that the host is hosting, then press"
                        " TRY AGAIN.\n\n") + coopui::PanelRefusalText(0, ""));
        /* T-201 N1b, T-290: HOST GAME says only its own lines (T-201 N1 - failures are boxes): a press's line with the name-not-saved line -
           four lines at 1280x720 */
        lay.push_back(coopui::kLayHost); slot.push_back(coopui::kSlotStatus); extra.push_back(0);
        txt.push_back(coopui::PressStatusText(0, "World 1", "") + "\n\nYour name couldn't be saved. You'll need to enter it again next time.");
        /* HOSTING's status at its most: the hosting line and the copy note */
        lay.push_back(coopui::kLayHosting); slot.push_back(coopui::kSlotStatus); extra.push_back(2);
        txt.push_back(coopui::PanelHostingText(1, 0)
                      + "\n\n" + coopui::PanelCopyNote(1, 1, "255.255.255.255:65535"));
        lay.push_back(coopui::kLayHosting); slot.push_back(coopui::kSlotPlayers); extra.push_back(0);
        txt.push_back(coopui::PanelPlayersText("Scott", std::vector<std::string>(1, std::string("Anna"))));
        for (size_t t = 0; t < txt.size(); ++t)
        {
            int n = 0, halves = 0;
            const coopui::PanelSlot* rows = coopui::PanelRowsOf(lay[t], &n);
            for (int i = 0; i < n; ++i) if (rows[i].slot == slot[t]) halves = rows[i].halves;
            const int lines = coopui::NoticeLinesFor(txt[t], per);
            if (halves < lines + 1 || halves > lines + 1 + extra[t])
                Fail("ui5b: layout " + I(lay[t]) + " slot " + I(slot[t]) + " holds " + I(halves) + " half-rows for " + I(lines) + " line(s)");
        }
        /* T-201 N1b (owner 166): MULTIPLAYER is HOST GAME, JOIN GAME and BACK only, one height, no name row */
        {
            const coopui::PanelBand hb = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotHostBtn, g.rowH);
            const coopui::PanelBand jb = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotJoinBtn, g.rowH);
            const coopui::PanelBand bb = coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotButtons, g.rowH);
            if (hb.h != bb.h || jb.h != bb.h) Fail("ui5b: MULTIPLAYER's buttons are not one height");
            if (hb.top != 0) Fail("N1b: MULTIPLAYER does not start with HOST GAME");
            if (coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotNameRow, g.rowH).h != 0
                || coopui::PanelSlotBand(coopui::kLayLanding, coopui::kSlotNameHint, g.rowH).h != 0) Fail("N1b: PLAYER NAME is still on MULTIPLAYER");
        }
        /* T-201 N1b (owner 166): PLAYER NAME the top row of HOST GAME and JOIN GAME, a full row; the hint's row only while it has a
           sentence, straight under the box, and every hint sentence fits it at the box's width */
        {
            const int la[] = { coopui::kLayHost, coopui::kLayHostHint, coopui::kLayJoin, coopui::kLayJoinHint };
            const char* const hints[] = { "No player name.", "Name too long (24 characters max).",
                                          "Your name cannot start or end with a space.", "The name has a character that can't be used.",
                                          "Your name can't contain '#'." };
            for (int k = 0; k < 4; ++k)
            {
                const coopui::PanelBand nb = coopui::PanelSlotBand(la[k], coopui::kSlotNameRow, g.rowH);
                const coopui::PanelBand bb = coopui::PanelSlotBand(la[k], coopui::kSlotButtons, g.rowH);
                const coopui::PanelBand hb = coopui::PanelSlotBand(la[k], coopui::kSlotNameHint, g.rowH);
                if (nb.top != 0 || nb.h != bb.h) Fail("N1b: PLAYER NAME is not the full top row on layout " + I(la[k]));
                if ((k % 2) == 0 && hb.h != 0) Fail("N1b: an empty hint row on layout " + I(la[k]));
                if ((k % 2) == 1)
                {
                    if (hb.top != nb.top + nb.h) Fail("N1b: the hint is not straight under the box on layout " + I(la[k]));
                    const coopui::PanelPairRect p = coopui::PanelLabelBoxIn(g.innerW, coopui::PanelGapOf(g.rowH), nb, 0, 0);
                    const int perBox = p.boxW / coopui::kPanelTextCharW;
                    int n = 0, halves = 0;
                    const coopui::PanelSlot* rows = coopui::PanelRowsOf(la[k], &n);
                    for (int i = 0; i < n; ++i) if (rows[i].slot == coopui::kSlotNameHint) halves = rows[i].halves;
                    for (size_t h = 0; h < sizeof(hints) / sizeof(hints[0]); ++h)
                        if (coopui::NoticeLinesFor(hints[h], perBox) > halves)
                            Fail("N1b: the hint '" + std::string(hints[h]) + "' needs " + I(coopui::NoticeLinesFor(hints[h], perBox)) + " lines in " + I(halves));
                }
            }
            std::string why;
            if (coopworld::DisplayNameOk("#x", &why) || why != "Your name can't contain '#'.") Fail("N1b: the swept '{c}' hint is not DisplayNameOk's: '" + why + "'");
        }
        if (coopui::PanelSlotBand(coopui::kLayDeleteWorld, coopui::kSlotDlgText, g.rowH).h < 5 * g.rowH) Fail("ui5b: DELETE WORLD?'s text area");
        /* a long world name is cut with "...", a short title is left alone */
        const std::string longT = coopui::PanelTitleFit("HOSTING: " + std::string(80, 'W'), g.maxRect.w);
        if (coopui::PanelTitleFit("HOST GAME", g.maxRect.w) != "HOST GAME" || coopui::PanelTitleFit("HOST GAME", 0) != "HOST GAME"
            || (int)longT.size() > (g.maxRect.w - 64) / coopui::kPanelTitleCharW || longT.compare(longT.size() - 3, 3, "...") != 0
            || longT.compare(0, 9, "HOSTING: ") != 0)
            Fail("ui5b: a long title is not cut to its window: " + longT);
    }
    /* ui5c - NO DEAD SPACE, LIVE (user 2026-09-27: every screen is sized to its content).  The status / dialog text area is as
       tall as its live text (liveHalves), never taller than its row list's ceiling; the bottom row (on a name dialog, the name
       row) is one gap band under the text's last line; the lists keep their size; the window is its rows, centred. */
    {
        const coopui::PanelGeom g = coopui::PanelGeomIn(1280, 720);
        const int lays[] = { coopui::kLayHost, coopui::kLayJoin, coopui::kLayHosting, coopui::kLayProfiles, coopui::kLayNewWorld,
                             coopui::kLayDeleteWorld, coopui::kLayNewProfile, coopui::kLayDeleteProfile };
        for (int k = 0; k < (int)(sizeof(lays) / sizeof(lays[0])); ++k)
        {
            const int lay = lays[k];
            const int ts = coopui::PanelSlotBand(lay, coopui::kSlotStatus, 2).h > 0 ? coopui::kSlotStatus : coopui::kSlotDlgText;
            const int cap = coopui::PanelSlotBand(lay, ts, 2).h;   /* the list's value, in half-rows */
            const int next = (lay == coopui::kLayNewWorld || lay == coopui::kLayNewProfile) ? coopui::kSlotDlgName : coopui::kSlotButtons;
            if (cap <= 0) Fail("ui5c: layout " + I(lay) + " has no text area");
            for (int live = 0; live <= cap + 2; ++live)
            {
                const int want = live < cap ? live : cap;
                const coopui::PanelBand tb = coopui::PanelSlotBand(lay, ts, g.rowH, live);
                const coopui::PanelBand nb = coopui::PanelSlotBand(lay, next, g.rowH, live);
                if (coopui::PanelLayoutHalves(lay, live) != coopui::PanelLayoutHalves(lay) - cap + want)
                    Fail("ui5c: layout " + I(lay) + " is not as tall as its text at " + I(live) + " half-rows");
                if (nb.top - (tb.top + tb.h) > g.rowH / 2 + 1)
                    Fail("ui5c: layout " + I(lay) + " leaves an empty band under its text at " + I(live) + " half-rows");
                if (coopui::PanelSlotBand(lay, coopui::kSlotList, g.rowH, live).h != coopui::PanelSlotBand(lay, coopui::kSlotList, g.rowH).h)
                    Fail("ui5c: layout " + I(lay) + "'s list changed size with its text");
                const coopui::PanelRect r = coopui::PanelScreenRect(g, 720, lay, coopui::kWindowCXChromeH, live);
                if (r.h != coopui::PanelLayoutInnerH(lay, g.rowH, live) + 2 * g.padY + coopui::kWindowCXChromeH || r.top != (720 - r.h) / 2)
                    Fail("ui5c: layout " + I(lay) + "'s window is not its rows, centred, at " + I(live) + " half-rows");
            }
        }
        if (coopui::PanelLayoutHalves(coopui::kLayHost, -1) != coopui::PanelLayoutHalves(coopui::kLayHost))
            Fail("ui5c: liveHalves -1 is not the list's value");
    }
    /* ui5c fold (F4), ui5d pixel rule (manager 2026-09-28) - THE LAST LINE IS NEVER CLIPPED AND ONLY THE SPARE IS KEPT.  A
       text's half-rows are ceil((lines * kPanelTextLinePx + kPanelTextSparePx) / half-row); no text, no rows.  Swept over
       every row height from the smallest the panel accepts to 60 and one to twelve lines: never clipped (the area's px >=
       lines * kPanelTextLinePx) and less than one half-row + kPanelTextSparePx blank under the text.  24 is the comfort row
       height below ~620 px parents (the pinned 2 -> 3 and 5 -> 6 there are the accepted cost: no clipping wins). */
    {
        if (coopui::kPanelTextSparePx != 9 || coopui::kPanelTextLinePx != 18) Fail("ui5d: the text spare / line pitch moved");
        if (coopui::PanelTextHalves(0, 24) != 0 || coopui::PanelTextHalves(-1, 24) != 0) Fail("ui5c fold: no text is not zero half-rows");
        if (coopui::PanelTextHalves(1, 24) != 3 || coopui::PanelTextHalves(3, 24) != 6 || coopui::PanelTextHalves(3, 40) != 4
            || coopui::PanelTextHalves(3, 34) != 4 || coopui::PanelTextHalves(3, 52) != 3 || coopui::PanelTextHalves(2, 0) != 3)
            Fail("ui5d: PanelTextHalves' pinned values");
        for (int rowH = 14; rowH <= 60; ++rowH)
            for (int lines = 1; lines <= 12; ++lines)
            {
                const int h = coopui::PanelTextHalves(lines, rowH);
                if ((h * rowH) / 2 < lines * coopui::kPanelTextLinePx) Fail("ui5d: " + I(lines) + " lines clip at rowH " + I(rowH));
                if (h * rowH - 2 * lines * coopui::kPanelTextLinePx >= rowH + 2 * coopui::kPanelTextSparePx)   /* in half-pixels */
                    Fail("ui5d: " + I(lines) + " lines leave a half-row + spare or more blank at rowH " + I(rowH));
            }
        /* ui7 - THE MEASURED HEIGHT (ui.cpp PanelTextAreaHalves; the measurement itself is MyGUI's and cannot run offline):
           PanelTextHalvesPx never clips a measured text, leaves less than one half-row + kPanelTextSparePx under it, is 0 for
           no text and -1 (the list's value) with no row height; the fallback PanelTextHalves is it at lines x kPanelTextLinePx. */
        if (coopui::PanelTextHalvesPx(0, 24) != 0 || coopui::PanelTextHalvesPx(-5, 24) != 0 || coopui::PanelTextHalvesPx(40, 0) != -1
            || coopui::PanelTextHalvesPx(18, 24) != 3 || coopui::PanelTextHalvesPx(27, 36) != 2)
            Fail("ui7: PanelTextHalvesPx' pinned values");
        for (int rowH = 14; rowH <= 60; ++rowH)
        {
            for (int px = 1; px <= 400; ++px)
            {
                const int h = coopui::PanelTextHalvesPx(px, rowH);
                if (h * rowH < 2 * px) Fail("ui7: a measured " + I(px) + " px text clips at rowH " + I(rowH));
                if (h * rowH - 2 * px >= rowH + 2 * coopui::kPanelTextSparePx)
                    Fail("ui7: a measured " + I(px) + " px text leaves a half-row + spare or more blank at rowH " + I(rowH));
            }
            for (int lines = 1; lines <= 12; ++lines)
                if (coopui::PanelTextHalves(lines, rowH) != coopui::PanelTextHalvesPx(lines * coopui::kPanelTextLinePx, rowH))
                    Fail("ui7: the fallback is not the measured rule at " + I(lines) + " x 18 px (rowH " + I(rowH) + ")");
        }
        for (int lines = 1; lines <= 6; ++lines)   /* JOIN's status area at rowH 24 holds its lines, under its 9-half-row ceiling */
            if (coopui::PanelSlotBand(coopui::kLayJoin, coopui::kSlotStatus, 24, coopui::PanelTextHalves(lines, 24)).h < lines * coopui::kPanelTextLinePx)
                Fail("ui5c fold: JOIN's status band clips " + I(lines) + " lines at rowH 24");
    }
    /* ui5d (T485 at 1280x720) - HOSTING'S PLAYERS AREA FOLLOWS ITS TEXT.  Each text area is as tall as PanelLive says,
       never taller than its list value; the band under each starts one gap band after its end; the other screens ignore both
       values; an int PanelLive is the text area's alone; the real texts fit their live bands; the port is never 0. */
    {
        const coopui::PanelLive none;
        const coopui::PanelLive five(5);
        if (none.text != -1 || none.steam != -1 || none.players != -1) Fail("ui5d: PanelLive's default is not the list values");
        if (five.text != 5 || five.steam != -1 || five.players != -1) Fail("ui5d: an int PanelLive is not the text area's alone");
        const int capP = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotPlayers, 2).h;
        if (capP != 4) Fail("ui5d: HOSTING's PLAYERS ceiling moved");
        /* HOSTING has no gap rows (each text area's spare is the gap): STEAM (two lines, INVITE FRIENDS at the right), PLAYERS,
           LOCAL ADDRESS, INTERNET ADDRESS, the status area - 22 half-rows at its ceilings, 16 at T485's texts at 1280x720, 15 at
           T486's at 1920x1080. */
        {
            int n = 0, gaps = 0;
            const coopui::PanelSlot* s = coopui::PanelRowsOf(coopui::kLayHosting, &n);
            for (int i = 0; i < n; ++i) if (s[i].slot == coopui::kSlotGap) ++gaps;
            if (gaps != 0 || coopui::PanelLayoutHalves(coopui::kLayHosting) != 22) Fail("ui5d: HOSTING's row list (no gap rows, 22 half-rows)");
            const coopui::PanelGeom g7 = coopui::PanelGeomIn(1280, 720);
            const int per7 = g7.innerW / coopui::kPanelTextCharW;
            coopui::PanelLive t485(coopui::PanelTextHalves(coopui::NoticeLinesFor(coopui::PanelHostingText(0, 0), per7), g7.rowH));
            t485.players = coopui::PanelTextHalves(coopui::NoticeLinesFor(coopui::PanelPlayersText("PlayerB", std::vector<std::string>()), per7), g7.rowH);
            t485.net = 2;   /* T-510: an address on the INTERNET ADDRESS row (one line) */
            if (t485.text != 2 || t485.players != 4 || coopui::PanelLayoutHalves(coopui::kLayHosting, t485) != 16)
                Fail("ui5d: HOSTING at T485's texts at 1280x720 is " + I(coopui::PanelLayoutHalves(coopui::kLayHosting, t485)) + " half-rows, not 16 (text "
                     + I(t485.text) + ", players " + I(t485.players) + ")");
            const coopui::PanelGeom g10 = coopui::PanelGeomIn(1920, 1080);
            const int per10 = g10.innerW / coopui::kPanelTextCharW;
            coopui::PanelLive t486(coopui::PanelTextHalves(coopui::NoticeLinesFor(coopui::PanelHostingText(0, 0), per10), g10.rowH));
            t486.players = coopui::PanelTextHalves(coopui::NoticeLinesFor(coopui::PanelPlayersText("PlayerB", std::vector<std::string>()), per10), g10.rowH);
            t486.net = 2;
            if (t486.text != 2 || t486.players != 3 || coopui::PanelLayoutHalves(coopui::kLayHosting, t486) != 15)
                Fail("ui5d: HOSTING at T486's texts at 1920x1080 is " + I(coopui::PanelLayoutHalves(coopui::kLayHosting, t486)) + " half-rows, not 15 (text "
                     + I(t486.text) + ", players " + I(t486.players) + ")");
        }
        const int parents[][2] = { { 1280, 720 }, { 1920, 1080 }, { 1024, 768 }, { 800, 600 } };
        for (int q = 0; q < (int)(sizeof(parents) / sizeof(parents[0])); ++q)
        {
            const coopui::PanelGeom g = coopui::PanelGeomIn(parents[q][0], parents[q][1]);
            for (int p = 0; p <= capP + 2; ++p)
            {
                coopui::PanelLive v(3);
                v.players = p;
                const int wantP = p < capP ? p : capP;
                if (coopui::PanelLayoutHalves(coopui::kLayHosting, v) != coopui::PanelLayoutHalves(coopui::kLayHosting, 3) - capP + wantP)
                    Fail("ui5d: HOSTING is not as tall as its PLAYERS text at players " + I(p));
                const coopui::PanelBand tb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotSteamRow, g.rowH, v);
                const coopui::PanelBand pb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotPlayers, g.rowH, v);
                const coopui::PanelBand hb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, g.rowH, v);
                const coopui::PanelBand nb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, g.rowH, v);
                const coopui::PanelBand sb = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotStatus, g.rowH, v);
                if (pb.top != tb.top + tb.h || hb.top != pb.top + pb.h || nb.top != hb.top + hb.h || sb.top != nb.top + nb.h)   /* each area's spare is the gap */
                    Fail("ui5d: a gap row in HOSTING at players " + I(p));
                if (hb.h != coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, g.rowH).h)
                    Fail("ui5d: LOCAL ADDRESS changed size with the PLAYERS text");
                const coopui::PanelRect r = coopui::PanelScreenRect(g, parents[q][1], coopui::kLayHosting, coopui::kWindowCXChromeH, v);
                if (r.h != coopui::PanelLayoutInnerH(coopui::kLayHosting, g.rowH, v) + 2 * g.padY + coopui::kWindowCXChromeH)
                    Fail("ui5d: HOSTING's window is not its rows at players " + I(p));
            }
            /* the real texts: the PLAYERS list alone and with one friend */
            std::vector<std::string> nobody, one;
            one.push_back("Anna");
            const std::string texts[2] = { coopui::PanelPlayersText("PlayerB", nobody), coopui::PanelPlayersText("PlayerB", one) };
            for (int k = 0; k < 2; ++k)
            {
                const int lines = coopui::NoticeLinesFor(texts[k], g.innerW / coopui::kPanelTextCharW);
                coopui::PanelLive v(3);
                v.players = coopui::PanelTextHalves(lines, g.rowH);
                const coopui::PanelBand b = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotPlayers, g.rowH, v);
                if (b.h < lines * coopui::kPanelTextLinePx)
                    Fail("ui5d: HOSTING's PLAYERS text " + I(k) + " clips at " + I(parents[q][0]) + "x" + I(parents[q][1]));
            }
        }
        coopui::PanelLive both;
        both.steam = 2;
        both.players = 0;
        for (int lay = 0; lay < coopui::kLayCount; ++lay)
            if (lay != coopui::kLayHosting && lay != coopui::kLayHostingVpn && coopui::PanelLayoutHalves(lay, both) != coopui::PanelLayoutHalves(lay))
                Fail("ui5d: layout " + I(lay) + " read HOSTING's live heights");
        /* the port: the running host's, else the PORT box's, else 7777 - never 0 */
        if (coopui::kPanelDefaultGamePort != 7777) Fail("ui5d: the default game port moved (ui.cpp PanelDefaults says 7777)");
        if (coopui::PanelHostingPort(0, 0) != 7777 || coopui::PanelHostingPort(0, 8000) != 8000 || coopui::PanelHostingPort(9000, 8000) != 9000
            || coopui::PanelHostingPort(-1, 70000) != 7777 || coopui::PanelHostingPort(70000, 0) != 7777)
            Fail("ui5d: PanelHostingPort's pinned values");
        for (int lp = -2; lp <= 2; ++lp)
            for (int fp = -2; fp <= 2; ++fp)
                if (coopui::PanelHostingPort(lp, fp) <= 0) Fail("ui5d: a HOSTING port of 0 or less at " + I(lp) + ", " + I(fp));
        if (coopui::PanelHomeAddrLine("192.168.1.20", coopui::PanelHostingPort(0, 0)) != "192.168.1.20:7777")
            Fail("ui5d: LOCAL ADDRESS with no host port");
    }
    /* ui5b: the PP6 lines (T-201 PP6': the created line shows now, approved 2026-09-29) */
    if (coopui::ProfCreatedAutoText("Scott") != "Profile \"Scott\" created.")   /* T-201 PP6' fold: kLoadRefusedAutoText removed - nothing showed it */
        Fail("ui5b: the PP6 texts");
    /* the approved texts later efforts show, exactly */
    if (coopui::NameTakenText("Anna") != "The name \"Anna\" is already taken in this world."
        || coopui::ProfLoadingText("Scott") != "Loading \"Scott\"..." || coopui::ProfOpeningNewGameText("Scott") != "Opening NEW GAME for \"Scott\"..."
        || coopui::SaveDamagedText("3 hours ago") != "This profile's save is damaged and can't be loaded. Load the backup from 3 hours ago instead?"
                                                     " Progress the world has kept since then is added back after loading."
        || coopui::StanceChangedText("Anna", coopui::kStanceAlly) != "Anna now treats you as an ALLY."
        || coopui::StanceChangedText("Anna", coopui::kStanceNeutral) != "Anna now treats you as NEUTRAL.")
        Fail("ui5: an approved text for a later effort");
    /* GAME OPTIONS: PROFILES PER PLAYER is the MULTIPLAYER tab's fourth row, 1-16, default 3 */
    const int pc = coopui::OptIndexOf(coopui::kOptTabCoop, 3);
    if (pc < 0 || std::string(coopui::OptDefs()[pc].key) != "profilecap" || std::string(coopui::OptDefs()[pc].label) != "PROFILES PER PLAYER"
        || coopui::OptCanon(pc, coopui::OptDefs()[pc].dflt) != "3" || coopui::OptShown(pc, "16") != "16" || !coopui::OptCanon(pc, "17").empty())
        Fail("ui5: the PROFILES PER PLAYER row");
}
/* pp1 (player-path-plan 1a): the UI driver's name table - every friendly name the harness writes maps to the widget ui.cpp's
   UiNames builds (asserted literally, so a rename on either side fails here), every widget name keeps the Coop / no '_' rule,
   and the argument shapes refuse what a player could not do. */
/* T-201 N1 - THE PRESS: press -> waiting, done, failure -> the box's exact words; cancel -> the screen's opening line. */
static int N1Verdict(const coopui::PressFacts& f, const std::string& mods, std::string* box)
{
    box->clear();
    return coopui::PressVerdictOf(f, mods, box);
}
void t_n1_press()
{
    std::vector<std::string> shown;
    std::string box;
    if (coopui::PressStatusText(0, "Kenshi-1", "") != "Starting \"Kenshi-1\"...") Fail("N1: the HOST status line");
    if (coopui::PressStatusText(1, "", "81.2.3.4:7777") != "Connecting to 81.2.3.4:7777...") Fail("N1: the JOIN status line");
    if (std::string(coopui::PressBoxTitle(0)) != "CAN'T HOST" || std::string(coopui::PressBoxTitle(1)) != "CAN'T JOIN") Fail("N1: the box titles");
    shown.push_back(coopui::PressStatusText(0, "w", "")); shown.push_back(coopui::PressStatusText(1, "", "1.2.3.4:7777"));

    /* HOST */
    coopui::PressFacts h;
    h.mode = 0;
    if (N1Verdict(h, "", &box) != coopui::kPressWait) Fail("N1: a fresh HOST press is not waiting");
    h.notebookUp = 1;
    if (N1Verdict(h, "", &box) != coopui::kPressWait) Fail("N1: a welcome before the title tick armed counted for this press");
    h.armed = 1;
    if (N1Verdict(h, "", &box) != coopui::kPressDone) Fail("N1: an armed host the world answered is not done");
    h.notebookUp = 0; h.elapsedMs = coopui::kPressHostStartMs - 1;
    if (N1Verdict(h, "", &box) != coopui::kPressWait) Fail("N1: a host inside its start time is not waiting");
    h.elapsedMs = coopui::kPressHostStartMs;
    if (N1Verdict(h, "", &box) != coopui::kPressFailed || box != "The world didn't start in time.") Fail("N1: the host's late box: '" + box + "'");
    shown.push_back(box);
    h.elapsedMs = 0;
    h.nbState = coopui::kNbNoExe;
    if (N1Verdict(h, "", &box) != coopui::kPressFailed || box != "A multiplayer file is missing.") Fail("N1: the missing-file box: '" + box + "'");
    shown.push_back(box);
    h.nbState = coopui::kNbSpawnFailed;
    if (N1Verdict(h, "", &box) != coopui::kPressFailed || box != "The world couldn't start.") Fail("N1: the spawn box: '" + box + "'");
    shown.push_back(box);
    h.nbState = coopui::kNbStoppedEarly; h.nbExit = 7; h.nbPort = 27016;
    if (N1Verdict(h, "", &box) != coopui::kPressFailed
     || box != "The world couldn't start (error 7). Port 27016 may be in use.")
        Fail("N1: the stopped box: '" + box + "'");
    shown.push_back(box);
    h.nbState = coopui::kNbStarting; h.armed = 1; h.notebookUp = 1;
    if (N1Verdict(h, "", &box) != coopui::kPressDone) Fail("N1: a starting world server that answered is not done");
    h.refusal = 3;
    if (N1Verdict(h, "", &box) != coopui::kPressFailed || box != "This world is full.") Fail("N1: a refused host");

    /* JOIN */
    coopui::PressFacts j;
    j.mode = 1;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("N1: a fresh JOIN press is not waiting");
    j.armed = 1; j.dials = 59;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("N1: a join on try 59 of 60 gave up");
    j.dials = 60;
    if (N1Verdict(j, "", &box) != coopui::kPressFailed
     || box != "The host isn't responding.")
        Fail("N1: the no-answer box: '" + box + "'");
    shown.push_back(box);
    j.dialCap = 0;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("N1: a cap of 0 gave up");
    j.dialCap = 60; j.dials = 3; j.worldLinked = 1; j.linkedMs = coopui::kPressJoinWorldMs - 1;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("N1: a linked join inside its world time is not waiting");
    j.linkedMs = coopui::kPressJoinWorldMs;
    if (N1Verdict(j, "", &box) != coopui::kPressFailed || box != "The host's world didn't respond in time.") Fail("N1: the world-late box: '" + box + "'");
    shown.push_back(box);
    j.linkedMs = 10; j.notebookUp = 1;
    if (N1Verdict(j, "", &box) != coopui::kPressDone) Fail("N1: a linked, welcomed join is not done");
    j.armed = 0;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("N1: a join done before its own arming");
    j.armed = 1;
    {
        static const struct { int r; const char* text; } kRef[] = {
            { 3, "This world is full." },
            { 4, "This world has reached its player limit." },
            { 2, "You're already in this world from another Kenshi window." },
            { 6, "You're already playing in this world from another Kenshi window." },
            { 1, "Couldn't join: your player ID couldn't be sent." },
            { 100, "Couldn't join: no profile is available." },
            { 0, "Version mismatch: this multiplayer mod is a different version from the one running the world." } };
        for (size_t i = 0; i < sizeof(kRef) / sizeof(kRef[0]); ++i)
        {
            j.refusal = kRef[i].r;
            if (N1Verdict(j, "", &box) != coopui::kPressFailed || box != kRef[i].text) Fail("N1: refusal " + std::string(kRef[i].text) + " -> '" + box + "'");
            shown.push_back(box);
        }
        j.refusal = 5;
        if (N1Verdict(j, "Mod mismatch: X", &box) != coopui::kPressFailed || box != "Mod mismatch: X") Fail("N1: the mods refusal is not store.cpp's sentence");
        /* T-201 N1b (owner 165): under CAN'T HOST the host's own refusals say host; JOIN keeps its own */
        coopui::PressFacts hf = j;
        hf.mode = 0;
        hf.refusal = 1;
        if (N1Verdict(hf, "", &box) != coopui::kPressFailed || box != "Couldn't host: your player ID couldn't be sent.") Fail("N1b: host refusal 1 -> '" + box + "'");
        hf.refusal = 100;
        if (N1Verdict(hf, "", &box) != coopui::kPressFailed || box != "Couldn't host: no profile is available.") Fail("N1b: host refusal 100 -> '" + box + "'");
        if (coopui::PanelRefusalText(100, "M", 1) != "M" || coopui::PanelRefusalText(1, "", 0) != "Couldn't join: your player ID couldn't be sent."
            || coopui::PanelRefusalText(3, "", 1) != coopui::PanelRefusalText(3, "", 0)) Fail("N1b: PanelRefusalText's host / join texts");
        /* T-201 N1b (owner 166): the name-taken box is known by its text (its OK returns to JOIN GAME, the name box selected) */
        if (!coopui::NoticeIsNameTaken(coopui::NameTakenText("Scott")) || coopui::NoticeIsNameTaken("Couldn't join: x")
            || coopui::NoticeTitle(coopui::NameTakenText("Scott")) != "CAN'T JOIN") Fail("N1b: NoticeIsNameTaken");
    }
    {
        /* T-201 N1 fold: a WELCOME refused as another world fails the JOIN at once, in config.cpp's sentence, under CAN'T JOIN */
        coopui::PressFacts w;
        w.mode = 1; w.armed = 1; w.worldLinked = 1; w.linkedMs = 10;
        w.wrongWorld = 1; w.openedWorld = "Alpha"; w.theirWorld = "Beta";
        if (N1Verdict(w, "", &box) != coopui::kPressFailed || box != "This game already opened world \"Alpha\". Another world needs a restart.")
            Fail("N1 fold: the wrong-world join: '" + box + "'");
        w.armed = 0;
        if (N1Verdict(w, "", &box) != coopui::kPressWait) Fail("N1 fold: a wrong-world fact before this press armed failed it");
        /* done needs no profiles list: linked and welcomed is done; the list, if one comes, follows through UiProfilesPoll */
        w.armed = 1; w.wrongWorld = 0; w.notebookUp = 1;
        if (N1Verdict(w, "", &box) != coopui::kPressDone || !box.empty()) Fail("N1 fold: a join done without a profiles list is not done");
        if (!coopui::PressSameHosted("Alpha", "Alpha", "7777", 7777)) Fail("N1 fold: HOST on the hosted world, same port, does not just show HOSTING");
        if (coopui::PressSameHosted("Alpha", "Alpha", "7778", 7777) || coopui::PressSameHosted("Alpha", "Beta", "7777", 7777)
         || coopui::PressSameHosted("", "", "7777", 7777) || coopui::PressSameHosted("Alpha", "Alpha", "", 7777))
            Fail("N1 fold: another world, another port or nothing hosted counted as the hosted world");
    }
    {
        /* T552: the world's profile LOBBY answer on the current link counts as the world answering - HOST and JOIN; a lobby
           list from an older link generation does not; every failure still wins over it */
        coopui::PressFacts l;
        l.mode = 0; l.armed = 1; l.nbState = coopui::kNbStarting; l.linkGen = 2; l.lobbyGen = 2;
        if (N1Verdict(l, "", &box) != coopui::kPressDone || !box.empty()) Fail("T552: a host the world answered with its profile lobby is not done");
        l.lobbyGen = 1;
        if (N1Verdict(l, "", &box) != coopui::kPressWait) Fail("T552: a host's lobby list from an older link generation counted");
        l.lobbyGen = -1; l.linkGen = 0;
        if (N1Verdict(l, "", &box) != coopui::kPressWait) Fail("T552: no lobby list (generation -1 / 0) counted");
        l.lobbyGen = 2; l.linkGen = 2; l.armed = 0;
        if (N1Verdict(l, "", &box) != coopui::kPressWait) Fail("T552: a lobby list before this press armed counted");
        l.armed = 1; l.lobbyGen = 1; l.elapsedMs = coopui::kPressHostStartMs;
        if (N1Verdict(l, "", &box) != coopui::kPressFailed || box != "The world didn't start in time.") Fail("T552: a stale lobby list stopped the host's late box");
        l.lobbyGen = 2; l.elapsedMs = 0; l.nbState = coopui::kNbStoppedEarly;
        if (N1Verdict(l, "", &box) != coopui::kPressFailed) Fail("T552: a lobby answer hid a stopped world server");
        l.nbState = coopui::kNbStarting; l.refusal = 100;
        if (N1Verdict(l, "", &box) != coopui::kPressFailed || box != "Couldn't host: no profile is available.") Fail("T552: a lobby answer hid a profile dead end");   /* T-201 N1b (owner 165): a HOST press */
        coopui::PressFacts m;
        m.mode = 1; m.armed = 1; m.worldLinked = 1; m.linkedMs = 10; m.linkGen = 3; m.lobbyGen = 3;
        if (N1Verdict(m, "", &box) != coopui::kPressDone || !box.empty()) Fail("T552: a linked joiner the host's world answered with its lobby is not done");
        m.lobbyGen = 2;
        if (N1Verdict(m, "", &box) != coopui::kPressWait) Fail("T552: a joiner's lobby list from an older link generation counted");
        m.lobbyGen = 3; m.worldLinked = 0; m.linkedMs = 0;
        if (N1Verdict(m, "", &box) != coopui::kPressWait) Fail("T552: a lobby answer without the host's game linked counted");
        m.worldLinked = 1; m.linkedMs = coopui::kPressJoinWorldMs; m.lobbyGen = 2;
        if (N1Verdict(m, "", &box) != coopui::kPressFailed || box != "The host's world didn't respond in time.") Fail("T552: a stale lobby list stopped the world-late box");
        m.lobbyGen = 3; m.refusal = 3;
        if (N1Verdict(m, "", &box) != coopui::kPressFailed) Fail("T552: a lobby answer hid a refusal");
    }
    if (coopui::NameTakenText("Anna") != "The name \"Anna\" is already taken in this world.") Fail("N1: name taken");
    shown.push_back(coopui::NameTakenText("Anna"));
    {
        coopui::PanelLinkFacts f;
        if (!coopui::PanelStatusText(f, "", 0, 0).empty()) Fail("N1: the idle line \"Not connected.\" is back");
    }
    /* no internal words in anything the player reads */
    static const char* const kBad[] = { "notebook", "relay", "session.cfg", "co-op", " slot", " link", " arm", " store", "TRY AGAIN" };
    for (size_t i = 0; i < shown.size(); ++i)
        for (size_t k = 0; k < sizeof(kBad) / sizeof(kBad[0]); ++k)
        {
            if (shown[i].find(kBad[k]) != std::string::npos) Fail("N1: '" + std::string(kBad[k]) + "' in player text: " + shown[i]);
        }
}
/* M11 flip fold - THE WORLD SERVER IS THE ONE DOOR, IN THE PANEL'S DECISIONS: the title re-dial of a joining game (bounded, never
   while the world server has answered), the CAN'T JOIN / late boxes keyed to the world server's WELCOME or lobby, and the status
   line's states keyed to the world-server link. */
void t_m11flip_world_door_press_and_redial()
{
    /* coopstore::TitleRedialDecide(isClient, hasDoor, sock, dials, cap, sinceDialMs, paceMs, connectBudgetMs) */
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 0, 12, 1000u, 1000u, 5000u) != coopstore::kTitleRedialAgain) Fail("flip: a down link 1 s after the dial was not dialled again");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 0, 12, 999u, 1000u, 5000u) != coopstore::kTitleRedialWait) Fail("flip: re-dialled faster than once a second");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockConnecting, 0, 12, 4999u, 1000u, 5000u) != coopstore::kTitleRedialWait) Fail("flip: a connect inside its budget was torn down");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockConnecting, 0, 12, 5000u, 1000u, 5000u) != coopstore::kTitleRedialAgain) Fail("flip: a connect past its budget was not dialled again");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockUpNotWelcomed, 0, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: an up link (lobby / handshake) was re-dialled");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockUpWelcomed, 0, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: a welcomed link was re-dialled");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockUpRefused, 0, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: a refusal in words was re-dialled");
    if (coopstore::TitleRedialDecide(0, 1, coopstore::kSockDownOrFailed, 0, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: a host / single game was re-dialled at the title");
    if (coopstore::TitleRedialDecide(1, 0, coopstore::kSockDownOrFailed, 0, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: a game with no world server to ring dialled");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 10, 12, 1000u, 1000u, 5000u) != coopstore::kTitleRedialAgain) Fail("flip: dial 11 of 12 was not made");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 11, 12, 1000u, 1000u, 5000u) != coopstore::kTitleRedialLast) Fail("flip: the 12th unanswered dial was not the last");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 12, 12, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: dialled past the cap");
    if (coopstore::TitleRedialDecide(1, 1, coopstore::kSockDownOrFailed, 0, 0, 99999u, 1000u, 5000u) != coopstore::kTitleRedialNone) Fail("flip: a cap of 0 dialled");
    {
        /* THE BOUND IS REACHED: a joiner whose world server never answers counts up to the cap and then stops - the loop the
           title tick runs, with the clock moved one pace per step. */
        long long dials = 0;
        int last = 0, steps = 0;
        while (steps < 100)
        {
            const int a = coopstore::TitleRedialDecide(1, 1, coopstore::kSockConnecting, dials, 12, 5000u, 1000u, 5000u);
            if (a == coopstore::kTitleRedialNone) break;
            if (a == coopstore::kTitleRedialAgain || a == coopstore::kTitleRedialLast) { ++dials; if (a == coopstore::kTitleRedialLast) ++last; }
            ++steps;
        }
        if (dials != 12 || last != 1 || steps != 12) Fail("flip: an unanswered JOIN did not stop at exactly 12 dials");
    }

    std::string box;
    coopui::PressFacts j;
    j.mode = 1; j.armed = 1; j.dialCap = 12;
    j.dials = 11;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("flip: CAN'T JOIN before every dial went unanswered");
    j.dials = 12;
    if (N1Verdict(j, "", &box) != coopui::kPressFailed || box != coopui::kPressJoinNoAnswerText) Fail("flip: no CAN'T JOIN at the cap with no answer: '" + box + "'");
    j.linkGen = 4; j.lobbyGen = 4;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("flip: CAN'T JOIN although the world's profile lobby answered (link down now)");
    j.worldLinked = 1;
    if (N1Verdict(j, "", &box) != coopui::kPressDone) Fail("flip: a lobby answer on a live world link is not done");
    j.lobbyGen = -1; j.linkGen = 4; j.notebookUp = 1;
    if (N1Verdict(j, "", &box) != coopui::kPressDone) Fail("flip: a WELCOME on a live world link is not done, whatever the dial count");
    j.notebookUp = 0; j.dials = 0; j.linkedMs = coopui::kPressJoinWorldMs - 1;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("flip: the late box before kPressJoinWorldMs of the link being up");
    j.linkedMs = coopui::kPressJoinWorldMs;
    if (N1Verdict(j, "", &box) != coopui::kPressFailed || box != coopui::kPressJoinLateText) Fail("flip: no late box with the link up and no answer: '" + box + "'");
    j.worldLinked = 0;
    if (N1Verdict(j, "", &box) != coopui::kPressWait) Fail("flip: the late box with the world link down (the re-dial is still running)");

    coopui::PanelLinkFacts f;
    f.role = coopui::kPanelRoleClient; f.dialCap = 12;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelWaitingHost) Fail("flip: a joiner with no world link was not waitingHost");
    f.dials = 12;
    if (coopui::PanelJoinGaveUp(f) != 1) Fail("flip: a joiner at the cap with no world link did not read as given up");
    f.worldLinked = 1;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelWaitingNotebook || coopui::PanelJoinGaveUp(f) != 0) Fail("flip: an up, unwelcomed world link was not waitingNotebook");
    f.notebookUp = 1;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelConnected) Fail("flip: a welcomed world link was not connected");
    f.role = coopui::kPanelRoleHost;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelConnected) Fail("flip: a host its own world server welcomed was not connected");
    f.worldLinked = 0; f.notebookUp = 0;
    if (coopui::PanelLinkStateOf(f) != coopui::kPanelHostWaiting) Fail("flip: a host with no world link was not hostWaiting");
}
/* RESET WORLD on HOST GAME: the dialog's words in their two pieces and its tick box's label, the button's lit rule, the world
   buttons' rule while a reset runs, the dialog's title and layout (it fits at 1280x720 and 1920x1080 with a 48-character world name
   and the tick-box row), the status lines (a crash code too), the four world buttons' width, and the driver names that press the
   button, the dialog's two and its tick box. */
void t_reset_world_panel()
{
    const std::string dlg = coopui::ResetWorldDlgHead("World 1");
    const std::string want =
        "Reset \"World 1\"?\n"
        "\n"
        "The world goes back to how it was at the start, for every player.\n"
        "\n"
        "Starts over:\n"
        "  Towns, outposts and the people living in them\n"
        "  Named characters who died, and who rules each town\n"
        "  How the factions feel about each other\n"
        "  The day and time\n"
        "  Ruins, loot and everything left on the ground\n"
        "  Buildings and what is stored in them, unless kept below\n"
        "\n"
        "Kept for every player:\n"
        "  Characters, with their skills, health and equipment\n"
        "  Money and research\n"
        "  What each player's map shows\n"
        "  How each player's faction stands with others";
    if (dlg != want) Fail("reset: the RESET WORLD? dialog's words above the tick box are not the approved ones:\n" + dlg);
    if (std::string(coopui::kResetKeepLabel) != "Keep players' buildings, furniture and stored items") Fail("reset: the tick box's label");
    const std::string tail = coopui::ResetWorldDlgTail(worldreset::ResetOldWorldName("World 1", 1));
    if (tail != "Players who aren't here now are moved into the reset world the next time they join. The old world is kept as"
                " \"World 1 - before reset 1\".")
        Fail("reset: the RESET WORLD? dialog's last paragraph is not the approved one:\n" + tail);
    if (coopui::ResetWorldBusyText("World 1") != "Resetting \"World 1\"...") Fail("reset: the line while the reset runs");
    if (coopui::ResetWorldOutcomeText("World 1", 0) != "\"World 1\" was reset.") Fail("reset: the finished line");
    if (coopui::ResetWorldOutcomeText("World 1", swformat::kServerExitWorldLocked) != "Couldn't reset \"World 1\" - its files are in use.")
        Fail("reset: the line for a world whose lock a world server holds");
    {
        /* 3221225477 = 0xC0000005 and 4294967295 = 0xFFFFFFFF: Windows exit codes past 2^31, carried as they are */
        const long long other[] = { -1, 1, 2, 5, 6, 7, 33, 34, worldreset::kServerExitResetHalfway, 3221225477LL, 4294967295LL };
        for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); ++i)
            if (coopui::ResetWorldOutcomeText("World 1", other[i]) != "Couldn't reset \"World 1\".")
                Fail("reset: exit code " + coopui::PanelNum(other[i]) + " does not give the plain failure line");
    }
    /* lit only for a picked world, while this game neither hosts nor has opened a world, the pick is not in use and no reset runs */
    for (int m = 0; m < 32; ++m)
    {
        const int picked = m & 1, hosting = (m >> 1) & 1, opened = (m >> 2) & 1, inUse = (m >> 3) & 1, running = (m >> 4) & 1;
        const int wantLit = (picked && !hosting && !opened && !inUse && !running) ? 1 : 0;
        if (coopui::ResetWorldLit(picked, hosting, opened, inUse, running) != wantLit) Fail("reset: the lit rule at case " + I(m));
    }
    /* HOST, NEW WORLD, GAME OPTIONS and DELETE WORLD: greyed while a reset runs whatever their own rule says, else their own rule */
    for (int m = 0; m < 4; ++m)
    {
        const int own = m & 1, running = (m >> 1) & 1;
        if (coopui::WorldButtonLit(own, running) != ((own != 0 && running == 0) ? 1 : 0)) Fail("reset: the world buttons' rule at case " + I(m));
    }
    if (coopui::WorldButtonLit(7, 0) != 1 || coopui::WorldButtonLit(1, 5) != 0) Fail("reset: the world buttons' rule reads any non-zero as yes");
    if (coopui::PanelScreenTitle(4, coopui::kWorldDlgReset, 1, "", "") != "RESET WORLD?" || coopui::PanelScreenTitle(4, 0, 1, "", "") != "DELETE WORLD?")
        Fail("reset: the world dialog's titles");
    if (coopui::PanelLayoutOf(4, coopui::kWorldDlgReset) != coopui::kLayResetWorld || coopui::PanelLayoutOf(4, 0) != coopui::kLayDeleteWorld)
        Fail("reset: the world dialog's layouts");
    if (coopui::PanelLayoutHalves(coopui::kLayResetWorld) > coopui::kPanelMaxHalves) Fail("reset: RESET WORLD? is taller than the panel's rows");
    /* at the panel's width, the dialog's two pieces at the fallback line pitch fit their areas with the tick-box row (a whole row)
       between them, the tick box's label fits one line beside it, and the window stays on the screen, at both sizes, for a short
       world name and two 48-character ones (the name's limit; the old world's name is cut to 48 too).  At 1280x720 the fallback's
       17 lines above the tick box fill its 18 half-rows exactly, so the areas are held to the lines themselves, not the lines with
       the 9 px spare under them */
    const int sizes[][2] = { { 1280, 720 }, { 1920, 1080 } };
    std::string spaced;
    for (int i = 0; i < 15; ++i) spaced += "Wo ";
    spaced += "Wor";
    const std::string names[] = { "World 1", std::string(48, 'W'), spaced };
    for (int k = 0; k < 2; ++k)
    {
        const int pw = sizes[k][0], ph = sizes[k][1];
        const std::string at = I(pw) + "x" + I(ph);
        const coopui::PanelGeom g = coopui::PanelGeomIn(pw, ph);
        if (coopui::PanelScreenRect(g, ph, coopui::kLayResetWorld, coopui::kWindowCXChromeH).w != g.maxRect.w)
            Fail("reset: RESET WORLD? is not at the panel's width at " + at);
        const int perLine = g.innerW / coopui::kPanelTextCharW;
        const int capHead = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotDlgText, 2).h;   /* the list's values, in half-rows */
        const int capTail = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotDlgTail, 2).h;
        for (size_t t = 0; t < sizeof(names) / sizeof(names[0]); ++t)
        {
            const std::string what = " (a " + I((int)names[t].size()) + "-character name) at " + at;
            const std::string oldName = worldreset::ResetOldWorldName(names[t], 1);
            if (oldName.empty() || oldName.size() > 48) Fail("reset: the old world's name for" + what);
            const int headLines = coopui::NoticeLinesFor(coopui::ResetWorldDlgHead(names[t]), perLine);
            const int tailLines = coopui::NoticeLinesFor(coopui::ResetWorldDlgTail(oldName), perLine);
            coopui::PanelLive live(coopui::PanelTextHalvesPx(headLines * coopui::kPanelTextLinePx, g.rowH));
            live.tail = coopui::PanelTextHalvesPx(tailLines * coopui::kPanelTextLinePx, g.rowH);
            if (headLines * coopui::kPanelTextLinePx > (capHead * g.rowH) / 2)
                Fail("reset: RESET WORLD?'s text above the tick box is " + I(headLines) + " lines, over its " + I(capHead) + " half-rows" + what);
            if (tailLines * coopui::kPanelTextLinePx > (capTail * g.rowH) / 2)
                Fail("reset: RESET WORLD?'s last paragraph is " + I(tailLines) + " lines, over its " + I(capTail) + " half-rows" + what);
            const coopui::PanelRect r = coopui::PanelScreenRect(g, ph, coopui::kLayResetWorld, coopui::kWindowCXChromeH, live);
            if (r.top < 0 || r.top + r.h > ph || r.left < 0 || r.left + r.w > pw) Fail("reset: RESET WORLD? leaves the screen" + what);
            if (r.h != coopui::PanelLayoutInnerH(coopui::kLayResetWorld, g.rowH, live) + 2 * g.padY + coopui::kWindowCXChromeH)
                Fail("reset: RESET WORLD?'s rows are taller than its window" + what);
            const coopui::PanelBand tb = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotDlgText, g.rowH, live);
            const coopui::PanelBand kb = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotDlgTick, g.rowH, live);
            const coopui::PanelBand lb = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotDlgTail, g.rowH, live);
            const coopui::PanelBand bb = coopui::PanelSlotBand(coopui::kLayResetWorld, coopui::kSlotButtons, g.rowH, live);
            if (tb.h < headLines * coopui::kPanelTextLinePx || kb.top < tb.top + tb.h) Fail("reset: RESET WORLD?'s text runs into its tick box" + what);
            if (kb.h < g.rowH || lb.top < kb.top + kb.h) Fail("reset: RESET WORLD?'s tick-box row is not a whole row above the last paragraph" + what);
            if (lb.h < tailLines * coopui::kPanelTextLinePx || bb.top < lb.top + lb.h) Fail("reset: RESET WORLD?'s last paragraph runs into CANCEL / RESET WORLD" + what);
            if (coopui::NoticeLinesFor(coopui::kResetKeepLabel, (g.innerW - kb.h - coopui::PanelGapOf(g.rowH)) / coopui::kPanelTextCharW) != 1)
                Fail("reset: the tick box's label does not fit one line beside it" + what);
        }
        /* HOST GAME's four world buttons: each wide enough for its caption (GAME OPTIONS, the longest, at the text estimate) */
        const int gap = coopui::PanelGapOf(g.rowH);
        const int qw = coopui::PanelQuarterBtnW(g.innerW, gap);
        if (4 * qw + 3 * gap > g.innerW) Fail("reset: the four world buttons overflow the panel at " + at);
        if (qw < 12 * coopui::kPanelTextCharW + 2 * coopui::kPanelAddrBtnPadPx) Fail("reset: a world button is narrower than GAME OPTIONS at " + at);
    }
    /* the driver: resetworld presses the button; resetcancel / resetok press the dialog's two only while they read CANCEL / RESET WORLD */
    const coopui::UiDriveName* rb = coopui::UiDriveFind("resetworld", coopui::kDriveButton);
    const coopui::UiDriveName* rc = coopui::UiDriveFind("resetcancel", coopui::kDriveButton);
    const coopui::UiDriveName* ro = coopui::UiDriveFind("resetok", coopui::kDriveButton);
    if (rb == 0 || std::string(rb->w1) != "SWResetWorldBtn" || rb->caption != 0) Fail("reset: uiclick resetworld");
    if (rc == 0 || std::string(rc->w1) != "CoopDlgCancelBtn" || rc->caption == 0 || std::string(rc->caption) != "CANCEL") Fail("reset: uiclick resetcancel");
    if (ro == 0 || std::string(ro->w1) != "CoopDlgOkBtn" || ro->caption == 0 || std::string(ro->caption) != "RESET WORLD") Fail("reset: uiclick resetok");
    const coopui::UiDriveName* rk = coopui::UiDriveFind("resetkeep", coopui::kDriveButton);
    if (rk == 0 || std::string(rk->w1) != "SWResetKeepTick" || rk->caption != 0) Fail("reset: uiclick resetkeep");
}
/* T-810 fold: the link edges, the load's ledger source (the reconcile rule), the save's ledger file, the hint's place in the GIVE
   MONEY box */
void t_t810_money_fold()
{
    namespace mw = moneyw;
    {   /* an edge drops what came before it; the same edge keeps it */
        mw::Inbound in;
        if (!mw::InboundSync(&in, 3)) Fail("T-810 fold: the first edge is news");
        mw::Deliver d; d.inbox = 9; d.amount = 5;
        in.waiting[9] = d; in.acked.insert(4); in.refused[6] = 1;
        if (mw::InboundSync(&in, 3) || in.waiting.size() != 1 || in.acked.size() != 1 || in.refused.size() != 1) Fail("T-810 fold: the same edge keeps what came under it");
        if (!mw::InboundSync(&in, 4) || !in.waiting.empty() || !in.acked.empty() || !in.refused.empty() || in.edge != 4) Fail("T-810 fold: a new edge drops what came before it");
    }
    {   /* the ledger comes from where the balance came from */
        if (mw::LedgerFrom(true, true) != mw::kFromRecord || mw::LedgerFrom(true, false) != mw::kFromRecord) Fail("T-810 fold: a restored balance takes its record's ledger");
        if (mw::LedgerFrom(false, true) != mw::kFromSave) Fail("T-810 fold: the save's balance takes the save's ledger");
        if (mw::LedgerFrom(false, false) != mw::kFromIndex) Fail("T-810 fold: no save ledger - the newest record's");
        /* the receiver: the save's balance holds inbox 7's money and the save's ledger says applied 7 - 7 again is a repeat */
        std::vector<mw::Deliver> w; mw::Deliver d; d.inbox = 7; d.amount = 100; w.push_back(d); d.inbox = 8; d.amount = 50; w.push_back(d);
        long long sum = 0; unsigned long long upTo = 0; size_t rep = 0;
        if (mw::ApplyPlan(w, 7, 1000, &sum, &upTo, &rep) != 1 || sum != 50 || upTo != 8 || rep != 1) Fail("T-810 fold: the save's applied number makes its own deliveries repeats");
    }
    {   /* the save's ledger file */
        mw::Ledger l; l.nextTransfer = (5ULL << 16) + 2; l.applied = 77;
        mw::OutRow r; r.transfer = 5ULL << 16; r.toSlot = 2; r.amount = 300; l.out.push_back(r);
        r.transfer += 1; r.toSlot = 0xFFFFFFFEu; r.amount = 2147483647; l.out.push_back(r);
        const std::string t = mw::SaveLedgerText("pp_store", "world key", "person.2", l);
        std::string s, wk, pr; mw::Ledger b;
        if (t.empty() || !mw::SaveLedgerParse(t, &s, &wk, &pr, &b) || s != "pp_store" || wk != "world key" || pr != "person.2" || b.nextTransfer != l.nextTransfer
            || b.applied != 77 || b.out.size() != 2 || b.out[0].transfer != (5ULL << 16) || b.out[0].amount != 300 || b.out[1].toSlot != 0xFFFFFFFEu || b.out[1].amount != 2147483647)
            Fail("T-810 fold: the save's ledger round trip");
        std::string crlf;
        for (size_t i = 0; i < t.size(); ++i) { if (t[i] == '\n') crlf += '\r'; crlf += t[i]; }
        if (!mw::SaveLedgerParse(crlf, &s, &wk, &pr, &b) || b.out.size() != 2) Fail("T-810 fold: the save's ledger read with CRLF line ends");
        if (!mw::SaveLedgerText("a\tb", "w", "p", l).empty() || !mw::SaveLedgerText("s", "w\n", "p", l).empty()) Fail("T-810 fold: a name with a control byte is never written");
        if (mw::SaveLedgerParse(t.substr(0, t.size() - 1), &s, &wk, &pr, &b)) Fail("T-810 fold: a cut ledger file is refused");
        if (mw::SaveLedgerParse("SWLEDGER 2\n" + t.substr(t.find('\n') + 1), &s, &wk, &pr, &b)) Fail("T-810 fold: another version is refused");
        std::string bad = t; bad.replace(bad.find("\t300\n"), 5, "\t0\n");
        if (mw::SaveLedgerParse(bad, &s, &wk, &pr, &b)) Fail("T-810 fold: an outbox row with no money is refused");
        mw::Ledger big;
        for (size_t i = 0; i <= mw::kOutMax; ++i) { r.transfer = i + 1; r.amount = 1; big.out.push_back(r); }
        if (!mw::SaveLedgerText("s", "w", "p", big).empty()) Fail("T-810 fold: more rows than an outbox holds are never written");
        mw::Ledger none;
        if (!mw::SaveLedgerParse(mw::SaveLedgerText("", "", "", none), &s, &wk, &pr, &b) || !b.out.empty() || b.applied != 0 || !s.empty())
            Fail("T-810 fold: an empty ledger round trip");
    }
    {   /* the hint holds its widest line inside the box at every width */
        const char* const said[4] = { mw::DigitsHint(), mw::RefusedHint(1, mw::kAmtOk), mw::RefusedHint(0, mw::kAmtTooMuch), mw::RefusedHint(0, mw::kAmtEmpty) };
        for (int k = 0; k < 4; ++k)
        {
            bool in = false;
            for (int i = 0; mw::HintWords(i) != 0; ++i) if (std::string(mw::HintWords(i)) == said[k]) in = true;
            if (!in) Fail(std::string("T-810 fold: the hint line '") + said[k] + "' is not measured");
        }
        if (std::string(mw::RefusedHint(0, mw::kAmtTooMuch)) != "You don't have that much.") Fail("T-810 fold: the longest hint's words");
        const int lh = 18, rowY = 40, textLeft = 4;
        for (int W = 150; W <= 1200; W += 7)
            for (int need = 60; need <= 320; need += 13)
            {
                int ew = W / 5; if (ew < 70) ew = 70; if (ew > 120) ew = 120;
                const int editRight = textLeft + 66 + 8 + ew;
                const mw::HintSpot h = mw::PlaceHint(W, textLeft, editRight, rowY, lh, need);
                if (h.x < textLeft || h.x + h.w > W - 8 || h.h != lh) Fail("T-810 fold: the hint stays inside the box's margin");
                if (!h.below && (h.x < editRight + 8 || h.y != rowY || h.w < need || h.grow != 0)) Fail("T-810 fold: a hint right of the amount box holds its widest line");
                if (h.below && (editRight + 8 + need <= W - 8 || h.y < rowY + lh + 6 || h.grow != lh + 6 || h.x != textLeft)) Fail("T-810 fold: the hint goes under the amount box only when it does not fit beside it");
                if (need <= W - 8 - textLeft && h.w < need) Fail("T-810 fold: the hint's own row holds its widest line");
            }
    }
}
/* T-810 re-check fold: the save's ledger is never missing and never another write's (the pick at the request, the "none" file, the
   load's pairing), the file a load reads (written before the old one goes; an earlier save's is not read), the money hold lasting as
   long as the engine's save, the hint fitting inside the GIVE MONEY box */
void t_t810_money_fold2()
{
    namespace mw = moneyw;
    {   /* the "none" file round trip; neither kind of file reads as the other */
        for (int w = mw::kNoneHeld; w <= mw::kNoneUnwritable; ++w)
        {
            const std::string t = mw::SaveLedgerNoneText("pp_store", "world key", "person.2", w);
            std::string s, wk, pr; int why = 0; mw::Ledger l;
            if (t.empty() || !mw::SaveLedgerNoneParse(t, &s, &wk, &pr, &why) || s != "pp_store" || wk != "world key" || pr != "person.2" || why != w)
                Fail("T-810 fold 2: the none file round trip");
            if (mw::SaveLedgerParse(t, &s, &wk, &pr, &l)) Fail("T-810 fold 2: a none file is never read as a ledger");
            std::string crlf;
            for (size_t i = 0; i < t.size(); ++i) { if (t[i] == '\n') crlf += '\r'; crlf += t[i]; }
            if (!mw::SaveLedgerNoneParse(crlf, &s, &wk, &pr, &why) || why != w) Fail("T-810 fold 2: the none file read with CRLF line ends");
            if (mw::SaveLedgerNoneParse(t.substr(0, t.size() - 1), &s, &wk, &pr, &why)) Fail("T-810 fold 2: a cut none file is refused");
        }
        mw::Ledger l; l.applied = 3;
        std::string s, wk, pr; int why = 0;
        if (mw::SaveLedgerNoneParse(mw::SaveLedgerText("a", "b", "c", l), &s, &wk, &pr, &why)) Fail("T-810 fold 2: a ledger is never read as a none file");
        if (!mw::SaveLedgerNoneText("a", "b", "c", mw::kNoneNo).empty()) Fail("T-810 fold 2: no reason, no none file");
        if (!mw::SaveLedgerNoneParse(mw::SaveLedgerNoneText("a\tb", "w", "p", mw::kNoneHeld), &s, &wk, &pr, &why) || !s.empty() || !wk.empty() || !pr.empty())
            Fail("T-810 fold 2: unwritable names leave the none file naming no store");
        if (mw::SaveLedgerNoneParse(std::string(mw::kLedgerNoneHeader) + "\ns\ta\nw\tb\np\tc\nn\tsomething\n", &s, &wk, &pr, &why)) Fail("T-810 fold 2: an unknown reason is refused");
    }
    {   /* THE PAIRING RULE: every request state, and what a later load of that save pairs its balance with */
        int n = 0;
        for (int bits = 0; bits < 64; ++bits)
            for (int wn = mw::kNoneNo; wn <= mw::kNoneUnwritable; ++wn)
                for (int base = -1; base <= 1; ++base)
                {
                    const bool loaded = (bits & 1) != 0, rsp = (bits & 2) != 0, restored = (bits & 4) != 0, sm = (bits & 8) != 0, held = (bits & 16) != 0, landed = (bits & 32) != 0;
                    int why = -1;
                    const int k = mw::SaveLedgerPick(loaded, rsp, restored, sm, wn, held, base, landed, &why);
                    const bool ask = mw::SaveLedgerAskBase(loaded, rsp, restored, sm, wn, held);
                    ++n;
                    if (k == mw::kPickWait && landed) Fail("T-810 fold 2: a landed save is always decided");
                    if ((k == mw::kPickNone) != (why != mw::kNoneNo)) Fail("T-810 fold 2: a none pick carries its reason, any other none");
                    if (loaded && k != mw::kPickLive) Fail("T-810 fold 2: a loaded ledger is the save's");
                    if (!ask)
                    {   /* the store is not asked: its answer changes nothing */
                        for (int b2 = -1; b2 <= 1; ++b2)
                        {
                            int w2 = -1;
                            if (mw::SaveLedgerPick(loaded, rsp, restored, sm, wn, held, b2, landed, &w2) != k || w2 != why) Fail("T-810 fold 2: the store's answer is read only when it decides");
                        }
                    }
                    if (!loaded && k == mw::kPickLive && !(ask && base == 1)) Fail("T-810 fold 2: an unread ledger is live only once the store read it");
                    if (!loaded && !rsp && restored && k != mw::kPickRecord) Fail("T-810 fold 2: a balance laid from a record takes that record's ledger");
                    if (!loaded && !rsp && !restored && !held && sm && k != mw::kPickSave) Fail("T-810 fold 2: the save's balance takes the loaded save's ledger");
                    if (!loaded && !rsp && !restored && !held && !sm && (wn == mw::kNoneHeld || wn == mw::kNoneUndecided || wn == mw::kNoneUnwritable) && (k != mw::kPickNone || why != wn))
                        Fail("T-810 fold 2: a world loaded from a none save keeps its reason");
                    if (!loaded && !rsp && !restored && held && (k != mw::kPickNone || why != mw::kNoneHeld))   /* T-810 fold 3 (F1): not even the loaded save's own */
                        Fail("T-810 fold 2: a held money record has no ledger that pairs");
                    if (!loaded && rsp && k != (landed ? mw::kPickNone : mw::kPickWait)) Fail("T-810 fold 2: nothing is taken while this load's restore has not run");
                    if (k == mw::kPickNone)
                    {   /* the later load of that save: its balance is never paired with another write's ledger */
                        const int from = mw::LedgerFrom(false, false, why);
                        if (why == mw::kNoneUnreadable ? from != mw::kFromIndex : from != mw::kFromNone) Fail("T-810 fold 2: a none save pairs with nothing (unreadable: as its world would)");
                        if (mw::LedgerFrom(true, false, why) != mw::kFromRecord) Fail("T-810 fold 2: a none save whose balance a record lays takes that record's ledger");
                    }
                }
        if (n != 64 * 5 * 3) Fail("T-810 fold 2: the pick sweep");
        if (mw::LedgerFrom(false, false) != mw::kFromIndex || mw::LedgerFrom(false, false, mw::kNoneNo) != mw::kFromIndex) Fail("T-810 fold 2: no ledger file - the newest record's, as before");
        if (mw::LedgerFrom(false, true, mw::kNoneHeld) != mw::kFromSave) Fail("T-810 fold 2: a ledger read beats a reason");
        /* conservation, told as a story: the save's balance 1000 holds inbox 7; the newest pp.money (made later in a held world) says applied 9
           with 300 more. Paired with that record the save would never add inbox 8 and 9 again (300 destroyed); kFromNone pairs nothing */
        std::vector<mw::Deliver> w; mw::Deliver d; d.inbox = 8; d.amount = 100; w.push_back(d); d.inbox = 9; d.amount = 200; w.push_back(d);
        long long sum = 0; unsigned long long upTo = 0; size_t rep = 0;
        if (mw::ApplyPlan(w, 9, 1000, &sum, &upTo, &rep) != 0 || sum != 0 || rep != 2) Fail("T-810 fold 2: the mismatched pair would drop both deliveries (the case kFromNone prevents)");
        if (mw::ApplyPlan(w, 7, 1000, &sum, &upTo, &rep) != 2 || sum != 300) Fail("T-810 fold 2: the save's own pair adds both");
    }
    {   /* WHICH FILE A LOAD READS: written before the old one goes (the copy in the records store at the request), never an earlier save's */
        const unsigned long long sec = 10000000ULL, T0 = 133000000000000000ULL;
        const unsigned long long req = T0, quick = T0 + 20 * sec, landed = quick + 1;
        if (mw::SaveLedgerChoose(landed, req, quick) != mw::kUseFolder) Fail("T-810 fold 2: the ledger beside the save, written after it landed");
        if (mw::SaveLedgerChoose(quick, req, quick) != mw::kUseFolder) Fail("T-810 fold 2: written in the same tick as quick.save is the save's");
        if (mw::SaveLedgerChoose(0, req, quick) != mw::kUsePending) Fail("T-810 fold 2: the game ended before the ledger landed - the copy written at the request");
        if (mw::SaveLedgerChoose(T0 - 3600 * sec, 0, quick) != mw::kUseNone) Fail("T-810 fold 2: an earlier save's ledger left beside a newer save is not read");
        if (mw::SaveLedgerChoose(T0 - 3600 * sec, req, quick) != mw::kUsePending) Fail("T-810 fold 2: a left-behind ledger gives way to this save's copy");
        if (mw::SaveLedgerChoose(0, quick + 1, quick) != mw::kUseNone) Fail("T-810 fold 2: a copy made after the save is a later request's");
        if (mw::SaveLedgerChoose(0, quick - mw::kPendingFitFt - 1, quick) != mw::kUseNone) Fail("T-810 fold 2: a copy older than any save's run is not this save's");
        if (mw::SaveLedgerChoose(0, quick - mw::kPendingFitFt, quick) != mw::kUsePending) Fail("T-810 fold 2: the copy's window holds its edge");
        if (mw::SaveLedgerChoose(T0, 0, 0) != mw::kUseFolder || mw::SaveLedgerChoose(0, req, 0) != mw::kUseNone) Fail("T-810 fold 2: no quick.save time - the file beside the save, as before");
        if (mw::SaveLedgerChoose(0, 0, quick) != mw::kUseNone) Fail("T-810 fold 2: no file at all");
        /* every moment of one save's life has a ledger a load would read: before the request the older save's own (its own quick.save older),
           from the request the copy, from the landing the file beside the save */
        const unsigned long long oldQuick = T0 - 600 * sec, oldLedger = oldQuick + 2;
        if (mw::SaveLedgerChoose(oldLedger, 0, oldQuick) != mw::kUseFolder) Fail("T-810 fold 2: the older save, untouched");
        if (mw::SaveLedgerChoose(0, req, quick) != mw::kUsePending) Fail("T-810 fold 2: the new save written, its ledger not yet");
        if (mw::SaveLedgerChoose(landed, 0, quick) != mw::kUseFolder) Fail("T-810 fold 2: landed, the copy removed");
    }
    {   /* THE MONEY HOLD LASTS AS LONG AS THE ENGINE'S SAVE */
        const unsigned long back = 120000UL;
        const unsigned long times[5] = { 0UL, 119999UL, 120000UL, 600000UL, 3600000UL };
        for (int i = 0; i < 5; ++i)
        {
            const unsigned long w = times[i];
            if (coopown::MarkWaitDecide(0, 1, 0, w, back) != coopown::kMarkWait) Fail("T-810 fold 2: a request the engine has not taken yet holds, however long");
            if (coopown::MarkWaitDecide(1, 0, 1, w, back) != coopown::kMarkWait) Fail("T-810 fold 2: the save thread still writing holds, however long");
            if (coopown::MarkWaitDecide(1, 2, 1, w, back) != coopown::kMarkWait) Fail("T-810 fold 2: a later request over a running save still holds");
            if (coopown::MarkWaitDecide(1, 0, 0, w, back) != coopown::kMarkLanded) Fail("T-810 fold 2: request taken and thread done - landed");
            if (coopown::MarkWaitDecide(0, 0, 0, w, back) != coopown::kMarkLanded) Fail("T-810 fold 2: the request seen back at 0 this frame - landed");
            if (coopown::MarkWaitDecide(1, 0, -1, w, back) != coopown::kMarkLanded) Fail("T-810 fold 2: an unreadable busy flag after the request cleared lands, as before");
            if (coopown::MarkWaitDecide(0, -1, 1, w, back) != (w < back ? coopown::kMarkWait : coopown::kMarkGiveUp)) Fail("T-810 fold 2: only an unreadable request gives up, at the backstop");
            if (coopown::MarkWaitDecide(1, -1, 1, w, back) != coopown::kMarkWait) Fail("T-810 fold 2: a request seen taken, its thread still writing, holds");
        }
    }
    {   /* THE HINT FITS INSIDE THE BOX: beside the amount box, on its own row, or the box wider - within the room the area leaves */
        const int lh = 18, rowY = 40, textLeft = 4;
        long long widened = 0, below = 0, beside = 0, tight = 0;
        for (int W = 200; W <= 900; W += 23)
            for (int need = 60; need <= 320; need += 29)
                for (int roomH = 0; roomH <= 40; roomH += 8)
                    for (int roomW = 0; roomW <= 300; roomW += 37)
                    {
                        int ew = W / 5; if (ew < 70) ew = 70; if (ew > 120) ew = 120;
                        const int editRight = textLeft + 66 + 8 + ew;
                        const mw::HintFit f = mw::PlaceHintFit(W, textLeft, editRight, rowY, lh, need, roomH, roomW);
                        const int BW = W + f.widen;
                        if (f.widen < 0 || f.widen > roomW) Fail("T-810 fold 2: the box widens only inside its room");
                        if (f.spot.x < textLeft || f.spot.x + f.spot.w > BW - 8 || f.spot.h != lh) Fail("T-810 fold 2: the hint stays inside the box's margin");
                        const mw::HintSpot alone = mw::PlaceHint(W, textLeft, editRight, rowY, lh, need);
                        if (!alone.below && (f.widen != 0 || f.spot.below)) Fail("T-810 fold 2: a hint that fits beside the amount box stays there");
                        if (f.fits)
                        {
                            if (f.spot.w < need) Fail("T-810 fold 2: a fitting hint holds its widest line");
                            if (f.spot.below && f.spot.grow > roomH) Fail("T-810 fold 2: a fitting hint's row is inside the room");
                            if (!f.spot.below && f.spot.grow != 0) Fail("T-810 fold 2: beside the amount box needs no row");
                            if (f.widen > 0) ++widened; else if (f.spot.below) ++below; else ++beside;
                        }
                        else
                        {   /* only when the area truly cannot hold it: even the widest box leaves the row too tall or too narrow */
                            ++tight;
                            const mw::HintSpot most = mw::PlaceHint(W + roomW, textLeft, editRight, rowY, lh, need);
                            if (!most.below || (most.grow <= roomH && need <= most.w) || f.widen != roomW) Fail("T-810 fold 2: 'does not fit' only when nothing fits");
                        }
                        if (alone.below && alone.grow <= roomH && need <= alone.w && (f.widen != 0 || !f.spot.below)) Fail("T-810 fold 2: the hint's own row comes before a wider box (owner 878)");
                    }
        if (widened == 0 || below == 0 || beside == 0 || tight == 0) Fail("T-810 fold 2: the hint sweep reaches every placement");
    }
}
/* T-810 fold 3: F1 the held money record (no base while held, the hold ends only when the record is laid, the save's balance never
   written meanwhile) and F2 a "none" save's load (its money record laid whatever its age; the next save takes a real ledger) */
void t_t810_money_fold3()
{
    namespace mw = moneyw;
    {   /* F1: WHILE HELD THERE IS NO BASE - never the save's ledger, never the newest record's, never a record's */
        for (int bits = 0; bits < 4; ++bits)
            for (int wn = mw::kNoneNo; wn <= mw::kNoneUnwritable; ++wn)
            {
                const bool restored = (bits & 1) != 0, sm = (bits & 2) != 0;
                if (mw::LedgerFrom(restored, sm, wn, true) != mw::kFromHeld) Fail("T-810 fold 3: a held money record gives no ledger base");
                if (mw::LedgerFrom(restored, sm, wn, false) == mw::kFromHeld) Fail("T-810 fold 3: no hold, no held answer");
                if (mw::LedgerFrom(restored, sm, wn) != mw::LedgerFrom(restored, sm, wn, false)) Fail("T-810 fold 3: the hold argument defaults to none");
                for (int base = -1; base <= 1; ++base)
                    for (int landed = 0; landed < 2; ++landed)
                    {   /* a save made while held takes no ledger: its load lays the record whatever its age (F2), never the older pair */
                        int why = -1;
                        if (restored) continue;   /* a laid record ends the hold: never both */
                        if (mw::SaveLedgerPick(false, false, false, sm, wn, true, base, landed != 0, &why) != mw::kPickNone || why != mw::kNoneHeld)
                            Fail("T-810 fold 3: a save in a held world says none (held) - even with the loaded save's own ledger in hand");
                        if (!mw::MoneyRecordAnyAge(why)) Fail("T-810 fold 3: that save's load lays the record whatever its age");
                    }
            }
    }
    {   /* F1: THE HOLD ENDS ONLY WHEN THE RECORD IS LAID (or a late read finds it is not to be laid at all) - never by time or a purse change */
        for (int arm = -1; arm <= 1; ++arm)
        {
            if (mw::HeldEnds(arm, true) != mw::kHeldEndsLaid) Fail("T-810 fold 3: a laid record ends the hold");
            const int e = mw::HeldEnds(arm, false);
            if (arm == 0 ? e != mw::kHeldEndsNotNewer : e != mw::kHeldStays) Fail("T-810 fold 3: a record not laid keeps the hold");
        }
        /* the story (the finding): record 5000 (applied 9) newer than the save's 1000 (applied 7); the restore refused (no-player). Every money
           check for 10 minutes, the purse changing: no base each time - so no pp.money write of 1000 with the save's ledger over the record */
        int purse = 1000, writes = 0;
        for (int sec = 0; sec <= 600; sec += 5)
        {
            purse += (sec % 20 == 0) ? 3 : 0;
            if (mw::LedgerFrom(false, true, mw::kNoneNo, true) == mw::kFromHeld) continue;   /* the store: no base - OwnCatBytes writes nothing */
            ++writes;
        }
        if (writes != 0) Fail("T-810 fold 3: past any grace and any purse change, a held record is never written over");
        /* laid at last: the record's balance plus the purse's own move since the hold (1000 -> purse) */
        int v = -1; long long sb = -1;
        if (!mw::HeldLayValue(5000, 1000, purse, &v, &sb) || v != 5000 + (purse - 1000) || sb != 0) Fail("T-810 fold 3: the lay keeps the world's own move in the hold");
        if (!mw::HeldLayValue(5000, 1000, 1000, &v, &sb) || v != 5000) Fail("T-810 fold 3: no move since the hold - the record's balance");
        if (!mw::HeldLayValue(5000, 1000, 700, &v, &sb) || v != 4700) Fail("T-810 fold 3: a spend in the hold is kept");
        v = 77;
        if (!mw::HeldLayValue(100, 1000, 0, &v, &sb) || v != 0 || sb != 900) Fail("T-810 fold 4: below zero is laid as 0 with the shortfall (was: never laid)");
        v = 77; sb = -1;
        if (mw::HeldLayValue(2147483647, 0, 1, &v, &sb) || v != 77 || sb != -1) Fail("T-810 fold 3: past a purse's range is never laid");
        /* after the lay: the record's ledger pairs (applied 9) - deliveries 8 and 9 are not applied again, nothing created */
        if (mw::LedgerFrom(true, true, mw::kNoneNo, false) != mw::kFromRecord) Fail("T-810 fold 3: once laid, the record's ledger");
        std::vector<mw::Deliver> w; mw::Deliver d; d.inbox = 8; d.amount = 100; w.push_back(d); d.inbox = 9; d.amount = 200; w.push_back(d);
        long long sum = 0; unsigned long long upTo = 0; size_t repeats = 0;
        if (mw::ApplyPlan(w, 9, 5000, &sum, &upTo, &repeats) != 0 || sum != 0) Fail("T-810 fold 3: the laid record's deliveries are not added twice");
    }
    {   /* F2: A "NONE" SAVE'S LOAD LAYS THE MONEY RECORD WHATEVER ITS AGE, and the next save takes a real ledger */
        if (mw::MoneyRecordAnyAge(mw::kNoneNo)) Fail("T-810 fold 3: a save with its own ledger keeps the load's rule");
        for (int wn = mw::kNoneHeld; wn <= mw::kNoneUnwritable; ++wn)
        {
            if (!mw::MoneyRecordAnyAge(wn)) Fail("T-810 fold 3: every none reason lays the record whatever its age");
            /* before: the record (seq 40, written at 10) older than the save (mark 50, quick.save at 1000) - the load's rule refused it, so
               no ledger paired and every later save inherited the reason */
            if (coopown::RestoreGate(true, coopown::kRuleMark, 50, 0, 40, 10, true) != coopown::kRsOlder) Fail("T-810 fold 3: the mark rule refuses an older record");
            int why = -1;
            if (mw::SaveLedgerPick(false, false, false, false, wn, false, 1, true, &why) != (wn == mw::kNoneUnreadable ? mw::kPickLive : mw::kPickNone))
                Fail("T-810 fold 3: (the finding) an unlaid none load hands its reason on");
            /* now: the money category's rule is "all" */
            const int rule = mw::MoneyRecordAnyAge(wn) ? coopown::kRuleAll : coopown::kRuleMark;
            if (coopown::RestoreGate(true, rule, 50, 0, 40, 10, true) != coopown::kRsRestore) Fail("T-810 fold 3: a none save's money record is laid whatever its age (mark)");
            if (coopown::RestoreGate(true, rule, 0, 1000, 40, 10, true) != coopown::kRsRestore) Fail("T-810 fold 3: ... and whatever its age (quick.save time)");
            if (coopown::RestoreGate(false, rule, 50, 0, 0, 0, true) != coopown::kRsAbsent) Fail("T-810 fold 3: no record - nothing to lay");
            if (coopown::RestoreGate(true, rule, 50, 0, 40, 10, false) != coopown::kRsBad) Fail("T-810 fold 3: a bad record is still never laid");
            if (mw::LedgerFrom(true, false, wn) != mw::kFromRecord) Fail("T-810 fold 3: the laid record's ledger pairs");
            /* the next save: the record's ledger (not read yet) or the live one - never none */
            for (int landed = 0; landed < 2; ++landed)
            {
                why = -1;
                if (mw::SaveLedgerPick(false, false, true, false, wn, false, -1, landed != 0, &why) != mw::kPickRecord || why != mw::kNoneNo)
                    Fail("T-810 fold 3: the save after a laid none load takes the record's ledger");
                why = -1;
                if (mw::SaveLedgerPick(true, false, true, false, wn, false, 1, landed != 0, &why) != mw::kPickLive || why != mw::kNoneNo)
                    Fail("T-810 fold 3: once read, the live ledger");
            }
        }
    }
}
/* T-810 fold 4: M1 a save made while the money record was held carries the hold's starting purse and the record's seq; its load lays
   the record + that save's own purse move when the record is the same one, the record alone when another (newer); below zero -> 0 and
   the shortfall, in a load and in a hold alike */
void t_t810_money_fold4()
{
    namespace mw = moneyw;
    {   /* THE "NONE (HELD)" FILE'S ROUND TRIP WITH THE HOLD MARK */
        mw::HeldMark hm; hm.base = 1000; hm.seq = 41;
        const std::string t = mw::SaveLedgerNoneText("pp_store", "world key", "person.2", mw::kNoneHeld, &hm);
        std::string s, wk, pr; int why = -1; mw::HeldMark back;
        if (t.empty() || !mw::SaveLedgerNoneParse(t, &s, &wk, &pr, &why, &back) || s != "pp_store" || wk != "world key" || pr != "person.2"
            || why != mw::kNoneHeld || !back.ok() || back.base != 1000 || back.seq != 41)
            Fail("T-810 fold 4: the none (held) file keeps the hold's starting purse and record seq");
        std::string crlf;
        for (size_t i = 0; i < t.size(); ++i) { if (t[i] == '\n') crlf += '\r'; crlf += t[i]; }
        back = mw::HeldMark();
        if (!mw::SaveLedgerNoneParse(crlf, &s, &wk, &pr, &why, &back) || back.base != 1000 || back.seq != 41) Fail("T-810 fold 4: the mark read with CRLF line ends");
        if (mw::SaveLedgerNoneParse(t.substr(0, t.size() - 1), &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: a cut held file is refused");
        why = -1;
        if (!mw::SaveLedgerNoneParse(t, &s, &wk, &pr, &why) || why != mw::kNoneHeld) Fail("T-810 fold 4: the mark is optional to the reader");
        /* a held file with no mark (an older file, or a hold with nothing in hand): read, no mark - its load lays the record alone */
        const std::string old = mw::SaveLedgerNoneText("pp_store", "world key", "person.2", mw::kNoneHeld);
        back.base = 5; back.seq = 5;
        if (!mw::SaveLedgerNoneParse(old, &s, &wk, &pr, &why, &back) || why != mw::kNoneHeld || back.ok()) Fail("T-810 fold 4: a held file without a mark reads, with none");
        if (mw::HeldMarkApplies(back, 41) != mw::kMarkNone) Fail("T-810 fold 4: no mark - the record alone");
        /* only a held save carries one, and only a whole one */
        for (int w = mw::kNoneUnreadable; w <= mw::kNoneUnwritable; ++w)
            if (mw::SaveLedgerNoneText("a", "b", "c", w, &hm) != mw::SaveLedgerNoneText("a", "b", "c", w)) Fail("T-810 fold 4: only a held save carries the mark");
        mw::HeldMark half; half.base = 1000;
        if (mw::SaveLedgerNoneText("a", "b", "c", mw::kNoneHeld, &half) != mw::SaveLedgerNoneText("a", "b", "c", mw::kNoneHeld))
            Fail("T-810 fold 4: no record seq, no mark");
        half.base = -1; half.seq = 9;
        if (mw::SaveLedgerNoneText("a", "b", "c", mw::kNoneHeld, &half) != mw::SaveLedgerNoneText("a", "b", "c", mw::kNoneHeld)) Fail("T-810 fold 4: no starting purse, no mark");
        /* refused: a mark on another reason, a zero seq, a base past a purse's range, the lines swapped, half a mark */
        const std::string head = std::string(mw::kLedgerNoneHeader) + "\ns\ta\nw\tb\np\tc\n";
        if (mw::SaveLedgerNoneParse(head + "n\tundecided\nb\t10\nq\t4\n", &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: a mark on another reason is refused");
        if (mw::SaveLedgerNoneParse(head + "n\theld\nb\t10\nq\t0\n", &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: a zero record seq is refused");
        if (mw::SaveLedgerNoneParse(head + "n\theld\nb\t2147483648\nq\t4\n", &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: a start past a purse's range is refused");
        if (mw::SaveLedgerNoneParse(head + "n\theld\nq\t4\nb\t10\n", &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: the mark's lines in order");
        if (mw::SaveLedgerNoneParse(head + "n\theld\nb\t10\n", &s, &wk, &pr, &why, &back)) Fail("T-810 fold 4: half a mark is refused");
        back = mw::HeldMark();
        if (!mw::SaveLedgerNoneParse(head + "n\theld\nb\t0\nq\t1\n", &s, &wk, &pr, &why, &back) || !back.ok() || back.base != 0 || back.seq != 1)
            Fail("T-810 fold 4: an empty purse is a starting purse");
    }
    {   /* THE LOAD LAY RULE: the held save's mark (start 1000, record seq 41); the record's balance; the save's balance (the purse at the load) */
        mw::HeldMark hm; hm.base = 1000; hm.seq = 41;
        struct Row { int rec; unsigned long long recSeq; int live; int use; int out; long long shortBy; const char* what; };
        const Row rows[] = {
            { 5000, 41, 1300, mw::kMarkCarried, 5300,    0, "T-810 fold 4: earned 300 in the hold - kept at the load" },
            { 5000, 41,  700, mw::kMarkCarried, 4700,    0, "T-810 fold 4: spent 300 in the hold - stays spent (goods kept, money gone)" },
            { 5000, 41, 1000, mw::kMarkCarried, 5000,    0, "T-810 fold 4: no move in the hold - the record's balance" },
            { 5000, 42, 1300, mw::kMarkNewer,   5000,    0, "T-810 fold 4: a newer record already holds what followed - laid alone" },
            { 5000, 40, 1300, mw::kMarkNewer,   5000,    0, "T-810 fold 4: any other record - laid alone" },
            {  200, 41,  700, mw::kMarkCarried,    0,  100, "T-810 fold 4: below zero - laid as 0 with the shortfall" },
            {    0, 41,    0, mw::kMarkCarried,    0, 1000, "T-810 fold 4: all of it short - 0, never negative" },
        };
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        {
            const Row& r = rows[i];
            const int use = mw::HeldMarkApplies(hm, r.recSeq);
            if (use != r.use) Fail(r.what);
            const int base = use == mw::kMarkCarried ? hm.base : r.live;   /* OwnRestoreOne: no carried mark - the purse read now is the base */
            int v = -1; long long sb = -1;
            if (!mw::HeldLayValue(r.rec, base, r.live, &v, &sb) || v != r.out || sb != r.shortBy) Fail(r.what);
            /* nothing created or destroyed: what is laid is the record + the move, or 0 with exactly what is short said */
            if ((long long)v - (long long)sb != (long long)r.rec + ((long long)r.live - (long long)base)) Fail("T-810 fold 4: the lay is the record + the move, the shortfall said");
        }
        if (mw::HeldMarkApplies(mw::HeldMark(), 41) != mw::kMarkNone) Fail("T-810 fold 4: no mark, nothing carried");
        /* the chain: a held save (start 1000, record 41) at a purse of 1300, loaded and its restore refused (no-player): the new hold starts from
           the mark's 1000, not the save's own 1300; the purse later 1250 (spent 50) - laid 5000 + 250 */
        int v = -1; long long sb = -1;
        if (mw::HeldMarkApplies(hm, 41) != mw::kMarkCarried || !mw::HeldLayValue(5000, hm.base, 1250, &v, &sb) || v != 5250 || sb != 0)
            Fail("T-810 fold 4: a hold of the same record keeps both holds' moves");
    }
    {   /* IN A SESSION'S HOLD: below zero -> 0 + the shortfall (the hold ends); above a purse's range never laid, nothing set */
        int v = 77; long long sb = -1;
        if (!mw::HeldLayValue(100, 1000, 0, &v, &sb) || v != 0 || sb != 900) Fail("T-810 fold 4: below zero in a hold is laid as 0 with the shortfall");
        if (mw::HeldEnds(-1, true) != mw::kHeldEndsLaid) Fail("T-810 fold 4: a 0 laid ends the hold");
        if (!mw::HeldLayValue(100, 1000, 900, &v, &sb) || v != 0 || sb != 0) Fail("T-810 fold 4: exactly zero is no shortfall");
        v = 77; sb = -1;
        if (mw::HeldLayValue(2147483647, 0, 1, &v, &sb) || v != 77 || sb != -1) Fail("T-810 fold 4: above a purse's range never laid, nothing set");
        if (!mw::HeldLayValue(2147483647, 1, 1, &v, &sb) || v != 2147483647 || sb != 0) Fail("T-810 fold 4: the top of a purse's range is laid");
        if (!mw::HeldLayValue(0, 2147483647, 0, &v, &sb) || v != 0 || sb != 2147483647LL) Fail("T-810 fold 4: the widest shortfall is said whole");
    }
}
/* mmo3: the four records' encode/decode, the restore gate (mmo2's mark rule), the research "no finished tech -> refuse"
   rule, the record keys and the TEST poke's argument */
void t_mmo3_records()
{
    using namespace coopown;
    std::vector<char> b;
    int m = 0;
    EncodeMoney(5000, &b);
    if (!DecodeMoney(b, &m) || m != 5000) Fail("mmo3: money round trip");
    EncodeMoney(-7, &b);
    if (!DecodeMoney(b, &m) || m != -7) Fail("mmo3: a negative money round trip");
    {
        std::vector<char> c = b; c.pop_back();
        if (DecodeMoney(c, &m)) Fail("mmo3: a cut money record is refused");
        c = b; c.push_back(0);
        if (DecodeMoney(c, &m)) Fail("mmo3: a money record with trailing bytes is refused");
        EncodeMap(std::vector<MapRow>(), &c);
        if (DecodeMoney(c, &m)) Fail("mmo3: another category's record is not money");
    }
    ResearchRec r;
    r.f.push_back(std::make_pair(std::string("num finished"), 2.0f));
    r.f.push_back(std::make_pair(std::string("num currents"), 1.0f));
    r.f.push_back(std::make_pair(std::string("current prog0"), 0.25f));
    r.s.push_back(std::make_pair(std::string("finished1"), std::string("Tech B")));
    r.s.push_back(std::make_pair(std::string("finished0"), std::string("Tech A")));
    r.s.push_back(std::make_pair(std::string("current0"), std::string("Tech C")));
    SortResearch(&r);
    EncodeResearch(r, &b);
    ResearchRec r2;
    if (!DecodeResearch(b, &r2) || r2.f.size() != 3 || r2.s.size() != 3 || r2.f[0].first != "current prog0" || r2.f[0].second != 0.25f
        || r2.f[2].first != "num finished" || r2.s[1].first != "finished0" || r2.s[1].second != "Tech A")
        Fail("mmo3: research round trip (sorted by key, floats bit-exact)");
    if (ResearchFinishedUsable(r2) != 2) Fail("mmo3: two finished techs are usable");
    { ResearchRec z = r2; z.f[2].second = 0.0f; if (ResearchFinishedUsable(z) != 0) Fail("mmo3: num finished 0 -> nothing usable -> refuse (the engine would set _Default Start)"); }
    { ResearchRec z = r2; z.s.erase(z.s.begin() + 1, z.s.end()); if (ResearchFinishedUsable(z) != 0) Fail("mmo3: no finished<i> entry -> refuse"); }
    { ResearchRec z = r2; z.s[1].second.clear(); if (ResearchFinishedUsable(z) != 1) Fail("mmo3: an empty finished<i> does not count"); }
    { ResearchRec z = r2; z.f[2].second = 1.0f; z.s.erase(z.s.begin() + 1); if (ResearchFinishedUsable(z) != 0) Fail("mmo3: only finished<i> with i < num finished counts"); }
    { ResearchRec z; if (ResearchFinishedUsable(z) != 0) Fail("mmo3: an empty research record -> refuse"); }
    { std::vector<char> c = b; c[c.size() - 1] = 'x'; c.push_back('y'); if (DecodeResearch(c, &r2)) Fail("mmo3: a research record with trailing bytes is refused"); }
    std::vector<MapRow> rows;
    MapRow a; a.sid = "town-b"; a.disc = 1; a.expl = 0; rows.push_back(a);
    a.sid = "town-a"; a.disc = 1; a.expl = 1; rows.push_back(a);
    EncodeMap(rows, &b);
    std::vector<MapRow> rows2;
    if (!DecodeMap(b, &rows2) || rows2.size() != 2 || rows2[0].sid != "town-a" || rows2[0].expl != 1 || rows2[1].sid != "town-b" || rows2[1].disc != 1 || rows2[1].expl != 0)
        Fail("mmo3: map round trip, sorted by town");
    { std::vector<char> c = b; c.resize(c.size() - 1); if (DecodeMap(c, &rows2)) Fail("mmo3: a cut map record is refused"); }
    FactionRec f; f.name = "Nameless"; f.platoonIds = 41;
    FacRow fr; fr.sid = "Holy Nation"; fr.rel = -35.5f; fr.trust = 2.0f; fr.trustNeg = 1.0f; fr.relBack = -40.0f; fr.trustBack = 0.0f; fr.trustNegBack = 3.0f;
    f.rows.push_back(fr);
    fr.sid = "Ashlanders"; fr.rel = 10.0f; f.rows.push_back(fr);
    EncodeFaction(f, &b);
    FactionRec f2;
    if (!DecodeFaction(b, &f2) || f2.name != "Nameless" || f2.platoonIds != 41 || f2.rows.size() != 2 || f2.rows[0].sid != "Ashlanders"
        || f2.rows[1].sid != "Holy Nation" || f2.rows[1].rel != -35.5f || f2.rows[1].trustNegBack != 3.0f || f2.rows[1].relBack != -40.0f)
        Fail("mmo3: faction round trip, sorted by faction");
    { std::vector<char> c = b; c.resize(c.size() - 4); if (DecodeFaction(c, &f2)) Fail("mmo3: a cut faction record is refused"); }
    if (PlatoonIdsAfterRestore(50, 41) != 50 || PlatoonIdsAfterRestore(30, 41) != 41) Fail("mmo3: platoonIDs never steps back");
    if (RestoreGate(false, kRuleMark, 10, 0, 0, 0, true) != kRsAbsent) Fail("mmo3: no record -> the save's value stays");
    if (RestoreGate(true, kRuleMark, 10, 0, 11, 0, true) != kRsRestore) Fail("mmo3: a record newer than the save's mark is restored");
    if (RestoreGate(true, kRuleMark, 10, 0, 10, 999999, true) != kRsOlder) Fail("mmo3: a record at the mark is already in the save");
    if (RestoreGate(true, kRuleSaveTime, 0, 1000, 1, 1001, true) != kRsRestore || RestoreGate(true, kRuleSaveTime, 0, 1000, 99, 1000, true) != kRsOlder)
        Fail("mmo3: no mark -> quick.save's time decides");
    if (RestoreGate(true, kRuleAll, 0, 0, 1, 0, true) != kRsRestore) Fail("mmo3: no mark and no quick.save -> every record");
    if (RestoreGate(true, kRuleMark, 10, 0, 11, 0, false) != kRsBad) Fail("mmo3: a newer record whose bytes fail the CRC is refused");
    if (std::string(RestoreWhyName(kRsOlder)) != "older-than-save" || std::string(RestoreWhyName(kRsNoFinished)) != "no-finished-tech"
        || std::string(RestoreWhyName(kRsAbsent)) != "no-record")
        Fail("mmo3: refusal names");
    if (!IsSquadKey("pp.squad.x") || IsSquadKey("pp.money") || IsSquadKey("pp.faction") || std::string(CatKey(kCatMoney)) != "pp.money"
        || std::string(CatKey(kCatResearch)) != "pp.research" || std::string(CatKey(kCatMap)) != "pp.map" || std::string(CatKey(kCatFaction)) != "pp.faction")
        Fail("mmo3: record keys (never a squad key)");
    {
        IndexLine l; l.key = CatKey(kCatResearch); l.seq = 5; l.len = 10; l.crc = 1; l.writtenAt = 2; l.chars = 7;
        std::string s; IndexLine l2;
        if (!EncodeIndexLine(l, &s) || !ParseIndexLine(s, &l2) || l2.key != "pp.research" || l2.seq != 5) Fail("mmo3: a category key makes a valid index line");
    }
    int p = 0;
    if (!ParsePokeMoney("poke money 5000", &p) || p != 5000 || !ParsePokeMoney("poke money -3", &p) || p != -3) Fail("mmo3: poke money parses");
    if (ParsePokeMoney("poke money", &p) || ParsePokeMoney("poke money x", &p) || ParsePokeMoney("poke money 99999999999", &p)
        || ParsePokeMoney("poke", &p) || ParsePokeMoney("poke money 5 6", &p) || ParsePokeMoney("poke money -", &p))
        Fail("mmo3: a bad poke money argument is refused");
}
/* mmo3 fold (review-mmo3): the transient-refusal hold, the final/transient split, map max, the world check, the research
   resolve rule, the faction flags and the om3f record still reading */
void t_mmo3_fold()
{
    using namespace coopown;
    if (HeldWriteDecision(false, 0, kHoldGraceMs, 0, 0, 7, 9) != kHoldSetBase) Fail("mmo3 fold: the first read after a refusal is the baseline, not written");
    if (HeldWriteDecision(true, kHoldGraceMs - 1, kHoldGraceMs, 7, 9, 8, 9) != kHoldSkip) Fail("mmo3 fold: a change inside the 60 s grace is held");
    if (HeldWriteDecision(true, kHoldGraceMs, kHoldGraceMs, 7, 9, 7, 9) != kHoldSkip) Fail("mmo3 fold: no change after the grace is still held (the record is kept)");
    if (HeldWriteDecision(true, kHoldGraceMs, kHoldGraceMs, 7, 9, 8, 9) != kHoldRelease || HeldWriteDecision(true, 999999, kHoldGraceMs, 7, 9, 7, 10) != kHoldRelease)
        Fail("mmo3 fold: a real change after the grace releases the hold");
    const char* fin[] = { "older-than-save", "no-record", "identity", "single", "record-crc", "decode", "no-finished-tech", "no-resolvable-tech" };
    for (int i = 0; i < 8; ++i) if (!RestoreRefusalFinal(fin[i])) Fail("mmo3 fold: a final refusal writes as before");
    const char* tr[] = { "claim-busy", "fault", "no-address", "no-research", "scratch", "no-player", "no-player-faction", "newRecord", "no-town-list" };
    for (int i = 0; i < 9; ++i) if (RestoreRefusalFinal(tr[i])) Fail("mmo3 fold: a transient refusal holds the writes");
    if (MapMergeByte(1, 0) != 1 || MapMergeByte(0, 1) != 1 || MapMergeByte(0, 0) != 0 || MapMergeByte(1, 1) != 1) Fail("mmo3 fold: map bytes are max(live, record)");
    if (RestoreWorldChanged(5, 5) || !RestoreWorldChanged(5, 6)) Fail("mmo3 fold: the restore runs only into a newly loaded world");
    ResearchRec r;
    r.f.push_back(std::make_pair(std::string("num finished"), 3.0f));
    r.s.push_back(std::make_pair(std::string("finished0"), std::string("sid-a")));
    r.s.push_back(std::make_pair(std::string("finished1"), std::string("")));
    r.s.push_back(std::make_pair(std::string("finished2"), std::string("sid-c")));
    r.s.push_back(std::make_pair(std::string("finished3"), std::string("sid-d")));   /* i >= num finished: never tried */
    const std::vector<std::string> sids = ResearchFinishedSids(r);
    if (sids.size() != 2 || sids[0] != "sid-a" || sids[1] != "sid-c") Fail("mmo3 fold: the tech ids Research::load would try");
    std::set<std::string> res;
    if (ResearchRestoreVerdict(r, res) != kRsNoResolvable) Fail("mmo3 fold: no tech resolves -> refuse (no-resolvable-tech)");
    res.insert("sid-d");
    if (ResearchRestoreVerdict(r, res) != kRsNoResolvable) Fail("mmo3 fold: a tech past num finished does not count");
    res.insert("sid-c");
    if (ResearchRestoreVerdict(r, res) != kRsRestore) Fail("mmo3 fold: one resolvable tech -> restore");
    { ResearchRec z; if (ResearchRestoreVerdict(z, res) != kRsNoFinished) Fail("mmo3 fold: no finished tech -> no-finished-tech"); }
    if (std::string(RestoreWhyName(kRsNoResolvable)) != "no-resolvable-tech") Fail("mmo3 fold: refusal name");
    FactionRec f; f.name = "N"; f.platoonIds = 3;
    FacRow fr; fr.sid = "HN"; fr.rel = -50.0f; fr.flags = 2; fr.flagsBack = 3; f.rows.push_back(fr);
    std::vector<char> b;
    EncodeFaction(f, &b);
    FactionRec f2;
    if (!DecodeFaction(b, &f2) || f2.rows.size() != 1 || f2.rows[0].flags != 2 || f2.rows[0].flagsBack != 3 || f2.rows[0].rel != -50.0f)
        Fail("mmo3 fold: the faction record carries the ally / atWar flags");
    std::vector<char> v1;   /* an om3f record written by mmo3 (no flags) still reads, flags unknown */
    RecPutStr(&v1, "om3f"); RecPutStr(&v1, "N"); RecPutI32(&v1, 3); RecPutI32(&v1, 1); RecPutStr(&v1, "HN");
    for (int i = 0; i < 6; ++i) RecPutF32(&v1, (float)i);
    if (!DecodeFaction(v1, &f2) || f2.rows.size() != 1 || f2.rows[0].flags != -1 || f2.rows[0].flagsBack != -1 || f2.rows[0].trustNegBack != 5.0f)
        Fail("mmo3 fold: an om3f record reads with its flags unknown");
    v1[4] = 'x';
    if (DecodeFaction(v1, &f2)) Fail("mmo3 fold: an unknown faction tag is refused");
}
void t_mmo2_overlay_roster()
{
    using namespace coopown;
    /* the mark line (om2: store, seq, time, world, profile) and the pending marker */
    MarkInfo m, g; m.store = "cooptest-mmo2"; m.seq = 42; m.at = 1700000000; m.world = "w1"; m.profile = "p-7";
    std::string line;
    if (!EncodeMarkLine(m, &line) || line != "om2\tcooptest-mmo2\t42\t1700000000\tw1\tp-7\n") Fail("mmo2: mark line encodes");
    if (!ParseMarkLine(line, &g) || g.store != m.store || g.seq != 42 || g.at != 1700000000 || g.world != "w1" || g.profile != "p-7") Fail("mmo2: mark line round-trips");
    m.profile = "";
    if (!EncodeMarkLine(m, &line) || !ParseMarkLine(line + "rest\n", &g) || !g.profile.empty() || g.world != "w1") Fail("mmo2: an empty profile round-trips");
    m.world = "";
    if (EncodeMarkLine(m, &line)) Fail("mmo2: a mark with no world key is refused");
    if (ParseMarkLine("om1\ts\t7\t9", &g) || ParseMarkLine("om2\ts\t7\t9\t\tp", &g) || ParseMarkLine("om2\ts\tx\t9\tw\tp", &g) || ParseMarkLine("", &g))
        Fail("mmo2: a bad or old mark is refused (the caller treats it as no mark)");
    if (ParseMarkLine(std::string(MarkPending()) + "\n", &g) || !IsMarkPending(std::string(MarkPending()) + "\n") || IsMarkPending("om2\ts")) Fail("mmo2: the pending marker is no mark");
    /* the store identity */
    std::string hw, hp, head;
    if (!EncodeIndexHeader("w1", "p-7", &head) || head != "oh1\tw1\tp-7\n" || !IsIndexHeader(head)) Fail("mmo2: index header encodes");
    if (!ParseIndexHeader(head, &hw, &hp) || hw != "w1" || hp != "p-7") Fail("mmo2: index header round-trips");
    if (EncodeIndexHeader("", "p", &head) || ParseIndexHeader("o1\tpp.squad.x\t1\t2\t00000000\t3\t1", &hw, &hp)) Fail("mmo2: header refusals");
    if (!IdentityMatches("w1", "p-7", "w1", "p-7") || !IdentityMatches("w1", "", "w1", "")) Fail("mmo2: the same identity matches");
    if (IdentityMatches("w1", "p-7", "w2", "p-7") || IdentityMatches("w1", "p-7", "w1", "p-8") || IdentityMatches("", "", "", "")) Fail("mmo2: an identity mismatch refuses");
    std::vector<std::string> il; il.push_back("oh1\tw1\tp-7"); il.push_back("o1\tpp.squad.a\t9\t4\t0000000a\t5\t1");
    int badl = 0;
    if (MaxSeqOf(il, &badl) != 9 || badl != 0) Fail("mmo2: the header line is not a bad index line");
    /* the overlay rule: mark / quick.save time / neither */
    if (OverlayRule(true, true) != kRuleMark || OverlayRule(false, true) != kRuleSaveTime || OverlayRule(false, false) != kRuleAll) Fail("mmo2: rule choice");
    if (OverlayCandidate(kRuleMark, 5, 0, 5, 999) || !OverlayCandidate(kRuleMark, 5, 0, 6, 0)) Fail("mmo2: with a mark, seq > mark only");
    if (OverlayCandidate(kRuleSaveTime, 0, 1000, 99, 1000) || OverlayCandidate(kRuleSaveTime, 0, 1000, 99, 999) || !OverlayCandidate(kRuleSaveTime, 0, 1000, 1, 1001))
        Fail("mmo2: with no mark, only records written after quick.save");
    if (!OverlayCandidate(kRuleAll, 0, 0, 0, 0)) Fail("mmo2: neither known: every record");
    if (FileTimeToUnix(116444736000000000ULL) != 0 || FileTimeToUnix(116444736000000000ULL + 10000000ULL * 5) != 5 || FileTimeToUnix(1) != 0) Fail("mmo2: FILETIME to unix");
    /* the container-name check */
    if (!NameMatchesSquadFile("C:\\s\\x\\platoon\\Nameless_0.platoon", "Nameless_0") || !NameMatchesSquadFile("platoon/Nameless_0.platoon", "Nameless_0")
        || !NameMatchesSquadFile("save/x/platoon\\Holy Nation_3.platoon", "Holy Nation_3")) Fail("mmo2: the squad's own file passes");
    if (NameMatchesSquadFile("xplatoon/Nameless_0.platoon", "Nameless_0") || NameMatchesSquadFile("platoon/Nameless_01.platoon", "Nameless_0")
        || NameMatchesSquadFile("zone/Nameless_0.platoon", "Nameless_0") || NameMatchesSquadFile("Nameless_0.platoon", "Nameless_0")
        || NameMatchesSquadFile("platoon/Nameless_0.platoon", "") || NameMatchesSquadFile("platoon/aNameless_0.platoon", "Nameless_0"))
        Fail("mmo2: any other file name is refused (nameMismatch)");
    /* a .platoon file's characters and items */
    std::vector<char> buf(8192);
    PodBuf b; PodInit(&b, &buf[0], buf.size());
    PodPutGdcHeader(&b, 7, 4);
    Mmo2PutRecord(&b, 0x1E, false, 0, 0, 0, 0, 0, false);
    Mmo2PutRecord(&b, 0x24, true, 11, 1, 111, 7, 0, true);
    Mmo2PutRecord(&b, 0x24, true, 33, 1, 5, 9, 77, false);
    Mmo2PutRecord(&b, 0x42, true, 55, 5, 1, 2, 0, true);   /* not a character's main record: its own hand is an item hand */
    if (PodOverflow(&b)) Fail("mmo2: the synthetic file fits");
    GdcScanOut so;
    if (!GdcScan(&buf[0], b.len, "Nameless_0.platoon", &so) || so.chars.size() != 2 || so.charsFull.size() != 2 || so.noHandle != 0) Fail("mmo2: two 0x24 records, two ids");
    else
    {
        if (so.chars[0] != "S=11;T=1" || so.chars[1] != "S=33;T=1") Fail("mmo2: the id is the hand's serial + type, got " + so.chars[0]);
        if (so.charsFull[0] != "C=7;CS=8;I=111;S=11;TYPE=1;") Fail("mmo2: the full form, got " + so.charsFull[0]);
        if (so.items.size() != 2 || so.items.count("S=77;T=3") != 1 || so.items.count("S=55;T=5") != 1) Fail("mmo2: item hands (a prefixed hand, a non-character record's own)");
    }
    /* the identity keeps serial + type when the container fields change (a moved character) */
    std::vector<char> buf2(4096);
    PodBuf b2; PodInit(&b2, &buf2[0], buf2.size());
    PodPutGdcHeader(&b2, 8, 1);
    Mmo2PutRecord(&b2, 0x24, true, 11, 1, 999, 42, 0, false);
    GdcScanOut s2;
    if (!GdcScan(&buf2[0], b2.len, "r.rec", &s2) || s2.chars.size() != 1 || s2.chars[0] != so.chars[0] || s2.charsFull[0] == so.charsFull[0])
        Fail("mmo2: serial + type is the identity; index/container are not");
    for (size_t cut = 1; cut < b.len; cut += 7)
    {
        GdcScanOut x;
        if (GdcScan(&buf[0], cut, "c", &x)) { Fail("mmo2: a cut-short file must not parse"); break; }
    }
    PodInit(&b2, &buf2[0], buf2.size());
    PodPutGdcHeader(&b2, 7, 1);
    Mmo2PutRecord(&b2, 0x24, false, 0, 0, 0, 0, 0, true);
    if (!GdcScan(&buf2[0], b2.len, "Sq_1.platoon", &s2) || s2.chars.size() != 1 || s2.noHandle != 1 || s2.chars[0] != "?Sq_1.platoon#0")
        Fail("mmo2: a character with no hand gets a never-matching id naming its file");
    if (GdcScan(0, 0, "z", &s2)) Fail("mmo2: no bytes do not parse");
    /* the roster check: every character exactly once after the load */
    std::vector<std::string> none, fixed;
    RosterCounts rc;
    std::vector<RosterSquad> r(1);
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars.push_back("a"); r[0].saveChars.push_back("b"); r[0].recChars = r[0].saveChars;
    if (RosterResolve(&r, none, &rc) != 0 || r[0].apply != 1) Fail("mmo2: the same people pass");
    r[0].recChars.push_back("c");
    if (RosterResolve(&r, none, &rc) != 0 || r[0].apply != 1) Fail("mmo2: a new recruit passes");
    fixed.push_back("c");   /* c is in another squad of the save: the record would double him */
    if (RosterResolve(&r, fixed, &rc) != 1 || r[0].apply || !r[0].duplicate || rc.duplicate != 1 || rc.conflict != 0) Fail("mmo2: a doubled person refuses (rosterDuplicate)");
    r[0].apply = 1; r[0].duplicate = 0; r[0].recChars.clear(); r[0].recChars.push_back("a");   /* b moved to a squad the save does not have (or died) */
    if (RosterResolve(&r, none, &rc) != 0 || !r[0].apply || r[0].conflict || rc.conflict != 0 || r[0].addBack.size() != 1 || r[0].addBack[0] != "b" || rc.addBack != 1)
        Fail("mmo2 (T-1062): a person in no squad with no left note is copied back and the record applies");
    r[0].recChars.push_back("b");
    fixed.clear(); fixed.push_back("b");   /* the save itself holds b twice */
    if (RosterResolve(&r, fixed, &rc) != 0 || !r[0].apply || rc.duplicate != 0 || r[0].dupSave.size() != 1 || r[0].dupSave[0] != "b" || rc.dupSave != 1 || !r[0].addBack.empty())
        Fail("mmo2 (T-1062): a person the save already doubles keeps the record's copy and the record applies");
    r[0].apply = 1; r[0].duplicate = 0; r[0].saveChars.clear();
    if (RosterResolve(&r, none, &rc) != 1 || r[0].apply || !r[0].empty || rc.empty != 1) Fail("mmo2: a squad whose save version has no character is refused");
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars.push_back("a"); r[0].saveChars.push_back("b"); r[0].recChars.push_back("a");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars.push_back("c"); r[1].recChars.push_back("c"); r[1].recChars.push_back("b");
    if (RosterResolve(&r, none, &rc) != 0 || !r[0].apply || !r[1].apply) Fail("mmo2: a move between two overlaid squads passes");
    r[1].apply = 0;   /* Y not overlaid: b is in X's save only - X's record would lose b, so he is copied back into it (T-1062) */
    if (RosterResolve(&r, none, &rc) != 0 || !r[0].apply || r[0].conflict || r[0].addBack.size() != 1 || r[0].addBack[0] != "b")
        Fail("mmo2: a squad not overlaid counts with its save version (b copied back into X)");
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars.push_back("a"); r[0].recChars.push_back("b");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars.push_back("b"); r[1].recChars.push_back("a");
    if (RosterResolve(&r, none, &rc) != 0 || !r[0].apply || !r[1].apply) Fail("mmo2: a swap between two overlaid squads passes");
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars.push_back("a"); r[0].recChars.push_back("q");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars.push_back("q"); r[1].recChars.push_back("z");
    if (RosterResolve(&r, none, &rc) != 0 || !r[0].apply || !r[1].apply || r[0].addBack.size() != 1 || r[0].addBack[0] != "a" || !r[1].addBack.empty())
        Fail("mmo2 (T-1062): a lost person is copied back - nothing refused");
    /* one refusal at a time, to a fixed point: X doubles d (in another squad of the save) and is refused; X then loads its save
       version, which holds m, and Y's record (which took m) would double him - refused next */
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars.push_back("a"); r[0].saveChars.push_back("m"); r[0].recChars.push_back("a"); r[0].recChars.push_back("d");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars.push_back("y"); r[1].recChars.push_back("y"); r[1].recChars.push_back("m");
    fixed.clear(); fixed.push_back("d");
    if (RosterResolve(&r, fixed, &rc) != 2 || r[0].apply || r[1].apply || !r[0].duplicate || !r[1].duplicate || rc.duplicate != 2)
        Fail("mmo2: one refusal at a time, to a fixed point");
}

/* T-802: one character out of a squad file, and the roster check's moved-after-the-save outcome (src/common/ownrec.h
   GdcSpans, GdcStripChar, RosterFindMoves, RosterResolve). */
std::vector<std::string> T802V(const char* a, const char* b = 0, const char* c = 0)
{
    std::vector<std::string> v;
    if (a) v.push_back(a);
    if (b) v.push_back(b);
    if (c) v.push_back(c);
    return v;
}
/* one record in 0x6BD220's layout: a bool, a string, an own hand when handS != 0, "char count" when charCount >= 0, one
   reference list when refs is not empty, one instance per target with its state list */
void T802Rec(std::vector<char>* v, int type, const std::string& sid, int handS, int charCount, const std::vector<std::string>& refs,
             const std::vector<std::string>& targets, const std::vector<std::vector<std::string> >& states, const char* handPrefix /* = 0 */, int handTo /* = 0 */,
             const int* money /* = 0 */)
{
    using namespace coopown;
    RecPutI32(v, 0); RecPutI32(v, type); RecPutI32(v, 7); RecPutStr(v, "0"); RecPutStr(v, sid); RecPutI32(v, 0);
    if (type == 0x1E) RecPutI32(v, 0); else { RecPutI32(v, 1); RecPutStr(v, "dead"); v->push_back(0); }   /* a squad record: "char count" only */
    RecPutI32(v, 0);
    RecPutI32(v, (handS != 0 ? 2 : 0) + (charCount >= 0 ? 1 : 0) + (handPrefix != 0 ? 2 : 0) + (money != 0 ? 1 : 0));
    if (handS != 0) { RecPutStr(v, "handleS"); RecPutI32(v, handS); RecPutStr(v, "handleTYPE"); RecPutI32(v, 1); }
    if (charCount >= 0) { RecPutStr(v, "char count"); RecPutI32(v, charCount); }
    if (money != 0) { RecPutStr(v, kRecMoneyKey); RecPutI32(v, *money); }   /* T-799 D2: the group's money */
    if (handPrefix != 0) { RecPutStr(v, std::string(handPrefix) + "handleS"); RecPutI32(v, handTo); RecPutStr(v, std::string(handPrefix) + "handleTYPE"); RecPutI32(v, 1); }
    RecPutI32(v, 0); RecPutI32(v, 0);
    if (type == 0x1E) RecPutI32(v, 0); else { RecPutI32(v, 1); RecPutStr(v, "name"); RecPutStr(v, sid + "-name"); }
    RecPutI32(v, 0);
    RecPutI32(v, refs.empty() ? 0 : 1);
    if (!refs.empty())
    {
        RecPutStr(v, "refs"); RecPutI32(v, (int)refs.size());
        for (size_t k = 0; k < refs.size(); ++k) { RecPutStr(v, refs[k]); RecPutI32(v, 0); RecPutI32(v, 0); RecPutI32(v, 0); }
    }
    RecPutI32(v, (int)targets.size());
    for (size_t k = 0; k < targets.size(); ++k)
    {
        RecPutStr(v, "i-" + targets[k]); RecPutStr(v, targets[k]);
        for (int j = 0; j < 7; ++j) RecPutI32(v, 0);
        RecPutI32(v, (int)states[k].size());
        for (size_t j = 0; j < states[k].size(); ++j) RecPutStr(v, states[k][j]);
    }
}
/* a squad file as the engine writes one: A (hand 11: a24, a43, an item aItem, the inventory a29 naming it) and B (hand 22:
   b24, b43), then the squad record "sq" with one instance per character. charCount < 0: the true count. shareItem: B's
   record also names A's item. listA = false: A's instance does not name a24. variant 1: B's AI record holds a hand to A;
   2: the squad record holds one more field; 3: one more character record (x24) that no instance names; 4: the squad record carries
   the group's money, 2500 (T-799 D2). */
std::vector<char> T802Squad(bool withA, bool withB, int charCount, bool shareItem, bool listA, int variant /* = 0 */)
{
    std::vector<std::string> none;
    std::vector<std::vector<std::string> > noSt;
    std::vector<char> v;
    coopown::RecPutI32(&v, 15); coopown::RecPutI32(&v, 99);
    coopown::RecPutI32(&v, (withA ? 4 : 0) + (withB ? 2 : 0) + 1 + (variant == 3 ? 1 : 0));
    std::vector<std::string> tg;
    std::vector<std::vector<std::string> > st;
    if (withA)
    {
        T802Rec(&v, 0x24, "a24", 11, -1, none, none, noSt);
        T802Rec(&v, 0x43, "a43", 0, -1, none, none, noSt);
        T802Rec(&v, 0x2a, "aItem", 0, -1, none, none, noSt);
        std::vector<std::vector<std::string> > one(1);
        T802Rec(&v, 0x29, "a29", 0, -1, none, T802V("aItem"), one);
        tg.push_back("tmplA"); st.push_back(listA ? T802V("a24", "a43", "a29") : T802V("a43", "a29"));
    }
    if (withB)
    {
        T802Rec(&v, 0x24, "b24", 22, -1, none, none, noSt);
        T802Rec(&v, 0x43, "b43", 0, -1, shareItem ? T802V("aItem") : none, none, noSt, variant == 1 ? "target" : 0, 11);
        tg.push_back("tmplB"); st.push_back(T802V("b24", "b43"));
    }
    if (variant == 3) T802Rec(&v, 0x24, "x24", 33, -1, none, none, noSt);
    const int money4 = 2500;
    T802Rec(&v, 0x1E, "sq", 0, charCount >= 0 ? charCount : (int)tg.size(), none, tg, st, variant == 2 ? "extra" : 0, 5, variant == 4 ? &money4 : 0);
    return v;
}
void t_t802_moved_after_save()
{
    using namespace coopown;
    const std::string A = HandId(11, 1), B = HandId(22, 1);
    /* the strip: A's records and A's instance go, char count drops by one, every other byte is the same */
    std::vector<char> two = T802Squad(true, true, -1, false, true), out;
    if (GdcStripChar(&two[0], two.size(), A, &out) != kStripOk || out.empty()) Fail("t802: A strips from a squad of two");
    else
    {
        if (out != T802Squad(false, true, -1, false, true)) Fail("t802: the copy is the squad file of B alone, byte for byte");
        GdcScanOut so;
        if (!GdcScan(&out[0], out.size(), "s", &so) || so.chars.size() != 1 || so.chars[0] != B) Fail("t802: the copy parses and holds B only");
        std::vector<GdcRecSpan> sp;
        size_t end = 0;
        if (!GdcSpans(&out[0], out.size(), &end, &sp) || end != out.size() || sp.size() != 3 || !sp[2].hasCharCount || sp[2].charCount != 1 || sp[2].inst.size() != 1)
            Fail("t802: the copy's squad record counts one character and one instance");
    }
    out.clear();
    if (GdcStripChar(&two[0], two.size(), B, &out) != kStripOk || out != T802Squad(true, false, -1, false, true)) Fail("t802: B strips the same way");
    out.clear();
    if (GdcStripChar(&two[0], two.size(), HandId(99, 1), &out) != kStripNoChar || !out.empty()) Fail("t802: a person not in the file is refused, nothing written");
    std::vector<char> one = T802Squad(true, false, -1, false, true);
    if (GdcStripChar(&one[0], one.size(), A, &out) != kStripOk || out != T802Squad(false, false, -1, false, true)) Fail("t802: the last person strips to the squad record alone");
    out.clear();
    std::vector<char> sh = T802Squad(true, true, -1, true, true);
    if (GdcStripChar(&sh[0], sh.size(), A, &out) != kStripShared || !out.empty()) Fail("t802: a record another character still names is refused");
    std::vector<char> cc = T802Squad(true, true, 3, false, true);
    if (GdcStripChar(&cc[0], cc.size(), A, &out) != kStripCount || !out.empty()) Fail("t802: a char count that is not the instance count is refused");
    std::vector<char> ni = T802Squad(true, true, -1, false, false);
    if (GdcStripChar(&ni[0], ni.size(), A, &out) != kStripNoInstance || !out.empty()) Fail("t802: a character no instance names is refused");
    for (size_t cut = 1; cut < two.size(); cut += 5)
        if (GdcStripChar(&two[0], cut, A, &out) != kStripParse || !out.empty()) { Fail("t802: a cut-short file is refused"); break; }
    if (GdcStripChar(0, 0, A, &out) != kStripParse) Fail("t802: no bytes are refused");
    /* the roster: r joined X's squad after the save; the save still has r in D, which is not a candidate */
    std::vector<RosterSquad> r(1);
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars = T802V("a"); r[0].recChars = T802V("a", "r");
    std::vector<RosterFixedSquad> fx(2);
    fx[0].key = "E"; fx[0].chars = T802V("e");
    fx[1].key = "D"; fx[1].chars = T802V("r", "q");
    std::vector<RosterMove> mv;
    RosterFindMoves(r, fx, &mv);
    if (mv.size() != 1 || mv[0].cand != 0 || mv[0].from != 1 || mv[0].who != "r") Fail("t802: r moved from D into X");
    RosterCounts rc;
    std::vector<RosterSquad> today = r;
    if (RosterResolve(&today, T802V("e", "r", "q"), &rc) != 1 || today[0].apply || !today[0].duplicate) Fail("t802: without the strip X is refused as before");
    RosterSquad d;
    d.key = "D"; d.apply = 1; d.stripped = 1; d.saveChars = T802V("r", "q"); d.recChars = T802V("q"); d.movedIn = T802V("r");
    r[0].movedIn = T802V("r"); r[0].movedFrom = T802V("D");
    r.push_back(d);
    const std::vector<std::string> fixed = T802V("e");
    if (RosterResolve(&r, fixed, &rc) != 0 || !r[0].apply || !r[1].apply || rc.moved != 1 || rc.stripped != 1 || rc.duplicate != 0 || rc.conflict != 0)
        Fail("t802: the record and the stripped squad are both applied (moved after the save)");
    else
    {
        std::map<std::string, int> n;
        for (size_t k = 0; k < fixed.size(); ++k) ++n[fixed[k]];
        for (size_t i = 0; i < r.size(); ++i) for (size_t k = 0; k < r[i].recChars.size(); ++k) ++n[r[i].recChars[k]];
        if (n.size() != 4 || n["a"] != 1 || n["r"] != 1 || n["q"] != 1 || n["e"] != 1) Fail("t802: nobody doubled, nobody lost");
    }
    r[0].saveChars = T802V("a", "?X.platoon#1");   /* X refused for its own reason (a person with no hand lost - never copied back): the stripped squad goes with it */
    if (RosterResolve(&r, fixed, &rc) != 2 || r[0].apply || r[1].apply || rc.moved != 0 || rc.stripped != 0) Fail("t802: no strip stands without the record that took the person");
    /* a person truly in two candidates' records: no move, still refused */
    std::vector<RosterSquad> s2(2);
    s2[0].key = "X"; s2[0].apply = 1; s2[0].saveChars = T802V("a"); s2[0].recChars = T802V("a", "r");
    s2[1].key = "Y"; s2[1].apply = 1; s2[1].saveChars = T802V("y"); s2[1].recChars = T802V("y", "r");
    std::vector<RosterFixedSquad> fd(1);
    fd[0].key = "D"; fd[0].chars = T802V("r");
    RosterFindMoves(s2, fd, &mv);
    if (!mv.empty()) Fail("t802: a person in two records is not a move");
    if (RosterResolve(&s2, T802V("r"), &rc) != 2 || s2[0].apply || s2[1].apply) Fail("t802: a person in two records is still refused");
    /* in the candidate's own save version, in two squads of the save, or with no hand: no move */
    std::vector<RosterSquad> s1(1);
    s1[0].key = "X"; s1[0].apply = 1; s1[0].saveChars = T802V("a", "r"); s1[0].recChars = T802V("a", "r");
    RosterFindMoves(s1, fd, &mv);
    if (!mv.empty()) Fail("t802: a person in the candidate's own save version is not a move");
    s1[0].saveChars = T802V("a");
    std::vector<RosterFixedSquad> f2(2);
    f2[0].key = "D1"; f2[0].chars = T802V("r"); f2[1].key = "D2"; f2[1].chars = T802V("r");
    RosterFindMoves(s1, f2, &mv);
    if (!mv.empty()) Fail("t802: a person the save holds twice is not a move");
    s1[0].recChars = T802V("a", "?D.platoon#0");
    std::vector<RosterFixedSquad> f3(1);
    f3[0].key = "D"; f3[0].chars = T802V("?D.platoon#0");
    RosterFindMoves(s1, f3, &mv);
    if (!mv.empty()) Fail("t802: an id with no hand never moves");
    /* a move between two candidates needs no strip (today's rule passes it) */
    std::vector<RosterSquad> s3(2);
    s3[0].key = "X"; s3[0].apply = 1; s3[0].saveChars = T802V("a", "b"); s3[0].recChars = T802V("a");
    s3[1].key = "Y"; s3[1].apply = 1; s3[1].saveChars = T802V("c"); s3[1].recChars = T802V("c", "b");
    RosterFindMoves(s3, fd, &mv);
    if (!mv.empty() || RosterResolve(&s3, std::vector<std::string>(), &rc) != 0 || rc.moved != 0) Fail("t802: a move between two candidates is today's rule");
    /* fold: a kept record holding the person's hand, a layout not verified, a count that is not the 0x24 count */
    std::vector<char> hr = T802Squad(true, true, -1, false, true, 1);
    out.clear();
    if (GdcStripChar(&hr[0], hr.size(), A, &out) != kStripHandRef || !out.empty()) Fail("t802 fold: a kept record holding the person's hand refuses (hand-reference)");
    if (GdcStripChar(&hr[0], hr.size(), B, &out) != kStripOk) Fail("t802 fold: the hand goes with B's own records: B strips");
    out.clear();
    std::vector<char> ly = T802Squad(true, true, -1, false, true, 2);
    if (GdcStripChar(&ly[0], ly.size(), A, &out) != kStripLayout || !out.empty()) Fail("t802 fold: a squad record with another field refuses (layout)");
    std::vector<char> tr = two;
    tr.push_back(0);
    if (GdcStripChar(&tr[0], tr.size(), A, &out) != kStripLayout || !out.empty()) Fail("t802 fold: a byte after the records refuses (layout)");
    std::vector<char> x3 = T802Squad(true, true, -1, false, true, 3);
    if (GdcStripChar(&x3[0], x3.size(), A, &out) != kStripCount || !out.empty()) Fail("t802 fold: a char count that is not the 0x24 count refuses");
    if (std::string(StripWhyName(kStripCheck)) == "recheck") Fail("t802 fold: the strip's own check is not named like the plugin's recheck");
    /* fold: the pending-strip list (pp.strip.pending) */
    PendStrip e, g;
    e.oldSid = "Drifters_5"; e.who = "S=11;T=1"; e.seq = 42; e.newSid = "Nameless_0";
    std::string ln;
    if (!PendStripFormat(e, &ln) || ln != "ps1\tDrifters_5\tS=11;T=1\t42\tNameless_0\n") Fail("t802 fold: a pending line formats");
    if (!PendStripParse(ln, &g) || g.oldSid != e.oldSid || g.who != e.who || g.seq != 42 || g.newSid != e.newSid) Fail("t802 fold: a pending line round-trips");
    PendStrip bad = e;
    bad.oldSid = "a\tb";
    if (PendStripFormat(bad, &ln)) Fail("t802 fold: a tab in a field is refused");
    if (PendStripParse("ps1\tD\tS=1;T=1\tx\tN", &g) || PendStripParse("ps0\tD\tS=1;T=1\t4\tN", &g) || PendStripParse("ps1\tD\tS=1;T=1\t4", &g)
        || PendStripParse("ps1\t\tS=1;T=1\t4\tN", &g) || PendStripParse("", &g)) Fail("t802 fold: a bad pending line is refused");
    std::vector<PendStrip> pv;
    if (PendStripParseAll("ps1\tD\tp\t4\tN\r\njunk\n\nps1\tE\tq\t9\tN\n", &pv) != 1 || pv.size() != 2 || pv[1].oldSid != "E") Fail("t802 fold: the list reads, a bad line counted");
    pv.clear();
    PendStrip a1;
    a1.oldSid = "D"; a1.who = "p"; a1.seq = 5; a1.newSid = "N";
    if (!PendStripAdd(&pv, a1) || pv.size() != 1) Fail("t802 fold: an entry adds");
    a1.seq = 3;
    if (!PendStripAdd(&pv, a1) || pv.size() != 1 || pv[0].seq != 5) Fail("t802 fold: the same person again with an older seq keeps the newer");
    a1.seq = 8;
    if (!PendStripAdd(&pv, a1) || pv.size() != 1 || pv[0].seq != 8) Fail("t802 fold: a newer seq replaces");
    PendStrip a2 = a1;
    a2.who = "q"; a2.seq = 2;
    if (!PendStripAdd(&pv, a2) || pv.size() != 2) Fail("t802 fold: a second person of the same squad adds");
    std::vector<PendStrip> back;
    if (PendStripParseAll(PendStripText(pv), &back) != 0 || back.size() != 2 || back[0].seq != 8 || back[1].who != "q") Fail("t802 fold: the list's text round-trips");
    std::vector<PendStrip> k = pv;
    if (PendStripKeepAtLoad(&k, false, 0) != 2 || !k.empty()) Fail("t802 fold: no mark at the load: nothing pending is kept");
    k = pv;
    if (PendStripKeepAtLoad(&k, true, 5) != 1 || k.size() != 1 || k[0].who != "q") Fail("t802 fold: kept only when the save already holds the move (mark >= seq)");
    k = pv;
    if (PendStripDropGone(&k, "D", T802V("q", "z")) != 1 || k.size() != 1 || k[0].who != "q") Fail("t802 fold: a person the file no longer holds is dropped");
    k = pv;
    if (PendStripDropGone(&k, "E", std::vector<std::string>()) != 0 || k.size() != 2) Fail("t802 fold: another squad's load drops nothing");
    for (size_t i = 0; i < kPendStripCap + 3; ++i)
    {
        PendStrip c = a1;
        c.who = "w" + I((int)i);
        PendStripAdd(&pv, c);
    }
    PendStrip over = a1;
    over.who = "over";
    if (pv.size() != kPendStripCap || PendStripAdd(&pv, over)) Fail("t802 fold: the list is capped");
}

/* T-802 fold 2: a strip only when the taking squad's record is loaded with the person and still holds him; a full pending list
   refuses the move (src/common/ownrec.h PendStripDecide, PendStripNewCount, PendStripFits, PendStripDropOne). */
/* T-1062: the roster check settles per person (RosterResolve with left notes), a person copied back into a record's load copy
   (GdcAddChar), and the left notes' lines (LeftNote*). */
void t_t1062_settle_per_person()
{
    using namespace coopown;
    RosterCounts rc;
    std::map<std::string, int> left;
    left["b"] = kLeftDied;
    /* the player's report, case 1: b died just before the close - the record holds a, the save a and b */
    std::vector<RosterSquad> r(1);
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars = T802V("a", "b"); r[0].recChars = T802V("a");
    if (RosterResolve(&r, T802V("e"), &rc, &left) != 0 || !r[0].apply || r[0].leftOut != T802V("b") || r[0].leftWhy.size() != 1 || r[0].leftWhy[0] != kLeftDied
        || !r[0].addBack.empty() || rc.leftOut != 1 || rc.addBack != 0 || rc.conflict != 0)
        Fail("t1062: a death with a left note - the record applies and b stays out");
    /* the same death never noted: b is copied back - nobody is deleted (decision 1091 a) */
    if (RosterResolve(&r, T802V("e"), &rc) != 0 || !r[0].apply || r[0].addBack != T802V("b") || !r[0].leftOut.empty() || rc.addBack != 1)
        Fail("t1062: a death without a note - the record applies with b copied back");
    /* design 3.3: b moved to a squad made after the save, which this load cannot rebuild - copied back, never lost */
    std::map<std::string, int> other;
    other["z"] = kLeftDied;
    if (RosterResolve(&r, T802V("e"), &rc, &other) != 0 || !r[0].apply || r[0].addBack != T802V("b"))
        Fail("t1062: design 3.3 - a person moved to a squad the load cannot rebuild is copied back");
    /* a person in another applied record follows that record: neither copied back nor left out by a note */
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars = T802V("a", "b"); r[0].recChars = T802V("a");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars = T802V("y"); r[1].recChars = T802V("y", "b");
    if (RosterResolve(&r, T802V(0), &rc, &left) != 0 || !r[0].apply || !r[1].apply || !r[0].addBack.empty() || !r[0].leftOut.empty())
        Fail("t1062: a person in another applied record follows it");
    /* case 2: the save holds b twice (X's save version and an unrecorded squad's file): the record's copy is kept, the other
       copy is left where it is (a serial + type match is not proven the same person) */
    r.assign(1, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars = T802V("a", "b"); r[0].recChars = T802V("a", "b");
    if (RosterResolve(&r, T802V("b", "q"), &rc, &left) != 0 || !r[0].apply || r[0].dupSave != T802V("b") || rc.dupSave != 1 || rc.duplicate != 0)
        Fail("t1062: a doubling the save holds - the record applies, nobody stripped");
    /* a doubling the load would make (b joined X after the save and is still in another squad, no strip made) still refuses */
    r[0].saveChars = T802V("a");
    if (RosterResolve(&r, T802V("b", "q"), &rc, &left) != 1 || r[0].apply || !r[0].duplicate || rc.duplicate != 1)
        Fail("t1062: a doubling the load would make still refuses (never more copies than the save alone)");
    /* not settled - refuses as before: a lost person with no hand, and one whose copy-back could not be made */
    r[0].apply = 1; r[0].duplicate = 0; r[0].saveChars = T802V("a", "?X.platoon#1"); r[0].recChars = T802V("a");
    if (RosterResolve(&r, T802V(0), &rc) != 1 || r[0].apply || !r[0].conflict) Fail("t1062: a lost person with no hand refuses as before");
    r[0].apply = 1; r[0].conflict = 0; r[0].saveChars = T802V("a", "b"); r[0].noAdd = T802V("b");
    if (RosterResolve(&r, T802V(0), &rc) != 1 || r[0].apply || !r[0].conflict || !r[0].addBack.empty()) Fail("t1062: a copy-back that could not be made refuses as before");
    /* the copy-back: A back into the record's copy without him, from the save's file */
    const std::string A = HandId(11, 1), B = HandId(22, 1);
    std::vector<char> two = T802Squad(true, true, -1, false, true), onlyB, back, out;
    if (GdcStripChar(&two[0], two.size(), A, &onlyB) != kStripOk) Fail("t1062: setup - A strips from a squad of two");
    else
    {
        if (GdcAddChar(&two[0], two.size(), &onlyB[0], onlyB.size(), A, &out) != kStripOk) Fail("t1062: A is copied back from the save's file");
        else
        {
            GdcScanOut sc;
            if (!GdcScan(&out[0], out.size(), "t", &sc) || sc.chars.size() != 2 || std::count(sc.chars.begin(), sc.chars.end(), A) != 1
                || std::count(sc.chars.begin(), sc.chars.end(), B) != 1)
                Fail("t1062: the copy holds A and B once each");
            if (GdcStripChar(&out[0], out.size(), A, &back) != kStripOk || back != onlyB) Fail("t1062: A stripped again gives the record's copy back byte for byte");
        }
        out.clear();
        if (GdcAddChar(&two[0], two.size(), &two[0], two.size(), A, &out) != kAddPresent || !out.empty()) Fail("t1062: a person already in the copy is refused");
        if (GdcAddChar(&two[0], two.size(), &onlyB[0], onlyB.size(), HandId(99, 1), &out) != kStripNoChar) Fail("t1062: a person not in the save's file is refused");
        if (GdcAddChar(&two[0], two.size(), &onlyB[0], onlyB.size(), "?X.platoon#0", &out) != kStripNoChar) Fail("t1062: a person with no hand is refused");
        std::vector<char> extra = T802Squad(false, true, -1, false, true, 2);   /* one more field in the copy's squad record */
        if (GdcAddChar(&two[0], two.size(), &extra[0], extra.size(), A, &out) != kStripLayout) Fail("t1062: a copy not in the verified layout is refused");
        std::vector<char> rich = T802Squad(false, true, -1, false, true, 4), withMoney;   /* the record's copy carries the group's money */
        int money = 0;
        if (GdcAddChar(&two[0], two.size(), &rich[0], rich.size(), A, &withMoney) != kStripOk || RecordMoneyOf(&withMoney[0], withMoney.size(), &money) != kRecMoneyCarried
            || money != 2500)
            Fail("t1062: the copy-back keeps the record's money");
        if (std::string(StripWhyName(kAddPresent)) != "already-present" || std::string(StripWhyName(kAddSidClash)) != "record-name-clash") Fail("t1062: refusal names");
    }
    /* the last member back into an empty copy */
    std::vector<char> one = T802Squad(true, false, -1, false, true), none0, again;
    if (GdcStripChar(&one[0], one.size(), A, &none0) != kStripOk || GdcAddChar(&one[0], one.size(), &none0[0], none0.size(), A, &again) != kStripOk)
        Fail("t1062: the last member is copied back into an empty squad copy");
    else
    {
        GdcScanOut sc;
        if (!GdcScan(&again[0], again.size(), "t", &sc) || sc.chars != T802V(A.c_str())) Fail("t1062: the copy holds the last member alone");
    }
    /* the left notes' lines */
    LeftNote e;
    e.who = A; e.kind = kLeftDied; e.at = 1700000000;
    std::string line;
    if (!LeftNoteFormat(e, &line) || line != "pl1\t" + A + "\t1\t1700000000\n") Fail("t1062: a left note's line");
    LeftNote g;
    if (!LeftNoteParse(line, &g) || g.who != A || g.kind != kLeftDied || g.at != 1700000000) Fail("t1062: a left note's line reads back");
    if (LeftNoteParse("pl1\tS=1;T=1\t7\t5", &g) || LeftNoteParse("ps1\tS=1;T=1\t1\t5", &g) || LeftNoteParse("pl1\t\t1\t5", &g) || LeftNoteParse("pl1\tS=1;T=1\t1", &g))
        Fail("t1062: a bad note line is refused");
    std::vector<LeftNote> v;
    if (LeftNoteParseAll(line + "junk\n" + line, &v) != 1 || v.size() != 1) Fail("t1062: one note per person; a bad line is counted");
    int capDropped = 0;
    const int nAdd = (int)kLeftNoteHardCap + 4;
    for (int i = 0; i < nAdd; ++i) { LeftNote n; n.who = HandId(i + 1000, 1); n.kind = kLeftDied; n.at = i; capDropped += LeftNoteAdd(&v, n); }
    std::map<std::string, int> m;
    LeftNoteMap(v, &m);
    if (v.size() != kLeftNoteHardCap || capDropped != 5 || v.back().who != HandId(1000 + nAdd - 1, 1) || m.size() != kLeftNoteHardCap || m.count(A) != 0)
        Fail("t1062: the safety cap keeps the newest notes and counts its drops");
    std::vector<LeftNote> rt;
    int rtCap = -1;
    if (LeftNoteParseAll(LeftNoteText(v), &rt, &rtCap) != 0 || rt.size() != v.size() || rt.back().who != v.back().who || rtCap != 0) Fail("t1062: the notes' text reads back");
    /* note writing (OwnLeftNoteDeath's steps): the file's text read, the death added, the text written - a second death of the
       same person replaces his note */
    {
        std::vector<LeftNote> w;
        LeftNote d1; d1.who = B; d1.kind = kLeftDied; d1.at = 500;
        if (LeftNoteParseAll(std::string(), &w) != 0 || LeftNoteAdd(&w, d1) != 0) Fail("t1062 write: the first note into an empty file");
        std::string text = LeftNoteText(w);
        LeftNote d2 = d1; d2.at = 900;
        if (LeftNoteParseAll(text, &w) != 0 || LeftNoteAdd(&w, d2) != 0) Fail("t1062 write: the second note");
        text = LeftNoteText(w);
        std::vector<LeftNote> r2;
        if (LeftNoteParseAll(text, &r2) != 0 || r2.size() != 1 || r2[0].who != B || r2[0].at != 900) Fail("t1062 write: one note per person, the newer kept");
    }
    /* F1, the age rule: a note counts only for a save older than it (a death in the save's own second counts); unknown = all count */
    {
        std::vector<LeftNote> n1;
        LeftNote d; d.who = "b"; d.kind = kLeftDied; d.at = 100;
        n1.push_back(d);
        if (!LeftNoteHonoured(d, 50) || !LeftNoteHonoured(d, 100) || LeftNoteHonoured(d, 101) || !LeftNoteHonoured(d, -1)) Fail("t1062 F1: a note counts for an older save only");
        std::map<std::string, int> lm;
        int stale = -1;
        LeftNoteMap(n1, &lm, 200, &stale);
        if (!lm.empty() || stale != 1) Fail("t1062 F1: a note older than the loaded save is stale");
        std::vector<RosterSquad> q(1);
        q[0].key = "X"; q[0].apply = 1; q[0].saveChars = T802V("a", "b"); q[0].recChars = T802V("a");
        if (RosterResolve(&q, T802V(0), &rc, &lm) != 0 || q[0].addBack != T802V("b") || !q[0].leftOut.empty())
            Fail("t1062 F1: a person whose note predates the save (brought back by an older save's load) is copied back, not kept out");
        LeftNoteMap(n1, &lm, 50, &stale);
        if (lm.size() != 1 || stale != 0 || RosterResolve(&q, T802V(0), &rc, &lm) != 0 || q[0].leftOut != T802V("b") || !q[0].addBack.empty())
            Fail("t1062 F1: a note newer than the save keeps the dead out");
    }
    /* N3: a save of the store lands after a death and drops no note - an OLDER named save loaded next keeps the dead out (and the
       fallen list's row held); the newer save, made after the death, finds the note older than it */
    {
        std::vector<LeftNote> p;
        LeftNote d; d.who = HandId(7, 1); d.kind = kLeftDied; d.at = 100;
        LeftNoteAdd(&p, d);
        const std::string afterSave = LeftNoteText(p);   /* the file as it stands once a save at time 200 has landed */
        std::vector<LeftNote> rd;
        std::map<std::string, int> lm;
        int stale = -1;
        if (LeftNoteParseAll(afterSave, &rd) != 0 || rd.size() != 1) Fail("t1062 N3: the note is still in the file after a save landed");
        LeftNoteMap(rd, &lm, 50, &stale);
        std::vector<RosterSquad> q(1);
        q[0].key = "X"; q[0].apply = 1; q[0].saveChars = T802V("a", d.who.c_str()); q[0].recChars = T802V("a");
        if (lm.size() != 1 || stale != 0 || RosterResolve(&q, T802V(0), &rc, &lm) != 0 || q[0].leftOut != T802V(d.who.c_str()) || !q[0].addBack.empty())
            Fail("t1062 N3: an older save loaded after a later save landed - the dead stay dead");
        if (!LeftNoteHoldsDeath(rd, 7, 50)) Fail("t1062 N3: the fallen list's row stays held at that load");
        LeftNoteMap(rd, &lm, 200, &stale);
        if (!lm.empty() || stale != 1 || LeftNoteHoldsDeath(rd, 7, 200)) Fail("t1062 N3: the save made after the death finds the note older than it");
    }
    /* N4: the loaded save's time - the mark's; else quick.save's write time less the margin (a death while the save was written
       counts); else unknown */
    {
        if (LeftNoteSaveAt(true, 500, true, 900) != 500 || LeftNoteSaveAt(false, 0, true, 900) != 900 - kLeftNoteFileMargin
            || LeftNoteSaveAt(false, 0, false, 900) != -1 || LeftNoteSaveAt(false, 0, true, 50) != 0 || kLeftNoteFileMargin != 120)
            Fail("t1062 N4: the save time the notes are judged against");
        LeftNote d; d.who = "b"; d.kind = kLeftDied; d.at = 850;   /* died while quick.save (file time 900) was being written */
        if (!LeftNoteHonoured(d, LeftNoteSaveAt(false, 0, true, 900)) || LeftNoteHonoured(d, 900)) Fail("t1062 N4: a death during the save's write counts");
    }
    /* N1: a bring-back is refused while the row's own character is alive in the loaded world (swfallen::OriginalAlive) */
    {
        swfallen::Fallen f;
        f.handSerial = 7; f.templateName = "tmplA"; f.race = "";
        std::vector<swfallen::LiveChar> live(1);
        live[0].serial = 7; live[0].templateName = "tmplA";
        if (!swfallen::OriginalAlive(f, live)) Fail("t1062 N1: the row's own character alive - refused");
        live[0].serial = 8;
        if (swfallen::OriginalAlive(f, live)) Fail("t1062 N1: another serial - allowed");
        live[0].serial = 7; f.handSerial = 0;
        if (swfallen::OriginalAlive(f, live)) Fail("t1062 N1: an unknown serial - allowed");
    }
    /* F2: the ALIVE rule's exception, by hand serial */
    {
        std::vector<LeftNote> n2;
        LeftNote d; d.who = HandId(7, 1); d.kind = kLeftDied; d.at = 300;
        n2.push_back(d);
        if (!LeftNoteHoldsDeath(n2, 7, 200) || LeftNoteHoldsDeath(n2, 7, 400) || !LeftNoteHoldsDeath(n2, 7, -1) || LeftNoteHoldsDeath(n2, 0, 200)
            || LeftNoteHoldsDeath(n2, 70, 200) || LeftNoteHoldsDeath(std::vector<LeftNote>(), 7, 200))
            Fail("t1062 F2: no ALIVE only for a died note of that serial newer than the loaded save");
    }
    /* F5: the walker copies a record byte for byte with no names to replace; a fresh name is above every name of its shape */
    {
        std::vector<GdcRecSpan> sp;
        size_t e = 0;
        std::map<std::string, std::string> noRen;
        if (!GdcSpans(&two[0], two.size(), &e, &sp)) Fail("t1062 F5: setup");
        for (size_t i = 0; i < sp.size(); ++i)
        {
            std::vector<char> o;
            if (!GdcRenamedRec(&two[0], sp[i].from, sp[i].to, noRen, &o) || o != std::vector<char>(two.begin() + (long)sp[i].from, two.begin() + (long)sp[i].to))
            { Fail("t1062 F5: a record copied with nothing renamed is the same bytes"); break; }
        }
        std::set<std::string> used;
        used.insert("5812--INGAME"); used.insert("5900--INGAME"); used.insert("7-gamedata.base"); used.insert("x"); used.insert("x-1");
        if (GdcFreshName("5812--INGAME", &used) != "5901--INGAME" || GdcFreshName("5812--INGAME", &used) != "5902--INGAME" || GdcFreshName("x", &used) != "x-2"
            || used.count("5902--INGAME") != 1)
            Fail("t1062 F5: fresh names");
    }
    /* F5 + F6: the record's copy (another session) names B's records as the save names A's - A comes back with his records renamed,
       every reference following, his records just before the squad record */
    {
        std::vector<std::string> none;
        std::vector<std::vector<std::string> > noSt;
        std::vector<char> clash;
        RecPutI32(&clash, 15); RecPutI32(&clash, 99); RecPutI32(&clash, 3);
        T802Rec(&clash, 0x24, "a24", 22, -1, none, none, noSt);
        T802Rec(&clash, 0x43, "aItem", 0, -1, none, none, noSt);
        std::vector<std::vector<std::string> > st1(1, T802V("a24", "aItem"));
        T802Rec(&clash, 0x1E, "sq", 0, 1, none, T802V("tmplB"), st1);
        std::vector<char> got, back2;
        std::vector<GdcRecSpan> sp;
        size_t e = 0;
        GdcScanOut sc;
        if (GdcAddChar(&two[0], two.size(), &clash[0], clash.size(), A, &got) != kStripOk) Fail("t1062 F5: a copy-back whose names clash is made");
        else if (!GdcSpans(&got[0], got.size(), &e, &sp) || e != got.size() || sp.size() != 7) Fail("t1062 F5: the result parses");
        else
        {
            if (sp[0].sid != "a24" || sp[1].sid != "aItem" || sp[2].sid != "a24-1" || sp[3].sid != "a43" || sp[4].sid != "aItem-1" || sp[5].sid != "a29"
                || sp[6].type != 0x1E)
                Fail("t1062 F5/F6: his records renamed where they clash, placed just before the squad record");
            if (sp[5].inst.size() != 1 || sp[5].inst[0].target != "aItem-1") Fail("t1062 F5: a reference to a renamed record follows it");
            if (sp[6].inst.size() != 2 || sp[6].inst[1].states != T802V("a24-1", "a43", "a29") || sp[6].inst[0].states != T802V("a24", "aItem"))
                Fail("t1062 F5: his instance names his renamed records, B's instance is untouched");
            if (!GdcScan(&got[0], got.size(), "t", &sc) || std::count(sc.chars.begin(), sc.chars.end(), A) != 1 || std::count(sc.chars.begin(), sc.chars.end(), B) != 1)
                Fail("t1062 F5: A and B once each");
            if (GdcStripChar(&got[0], got.size(), A, &back2) != kStripOk || back2 != clash) Fail("t1062 F5: A stripped again gives the record's copy back");
        }
        /* F6 without a clash: the copy of B alone gets A's records before its squad record */
        got.clear();
        if (GdcAddChar(&two[0], two.size(), &onlyB[0], onlyB.size(), A, &got) != kStripOk || !GdcSpans(&got[0], got.size(), &e, &sp) || sp.size() != 7
            || sp[0].sid != "b24" || sp[1].sid != "b43" || sp[2].sid != "a24" || sp[5].sid != "a29" || sp[6].type != 0x1E)
            Fail("t1062 F6: his records go just before the squad record");
        else if (sp[6].inst.size() != 2 || sp[6].inst[1].name != "i-tmplA") Fail("t1062 N2: an entry name with no clash is kept");
    }
    /* N2: the record's copy already has an entry named as his ("i-tmplA") - his entry is renamed, B's untouched, nothing else moves */
    {
        std::vector<std::string> none;
        std::vector<std::vector<std::string> > noSt;
        std::vector<char> dstE, got, back3;
        RecPutI32(&dstE, 15); RecPutI32(&dstE, 99); RecPutI32(&dstE, 3);
        T802Rec(&dstE, 0x24, "b24", 22, -1, none, none, noSt);
        T802Rec(&dstE, 0x43, "b43", 0, -1, none, none, noSt);
        std::vector<std::vector<std::string> > stB(1, T802V("b24", "b43"));
        T802Rec(&dstE, 0x1E, "sq", 0, 1, none, T802V("tmplA"), stB);   /* B's entry: "i-tmplA" */
        std::vector<GdcRecSpan> sp;
        size_t e = 0;
        if (GdcAddChar(&two[0], two.size(), &dstE[0], dstE.size(), A, &got) != kStripOk) Fail("t1062 N2: a copy-back whose entry name clashes is made");
        else if (!GdcSpans(&got[0], got.size(), &e, &sp) || e != got.size() || sp.size() != 7 || sp[6].type != 0x1E) Fail("t1062 N2: the result parses");
        else
        {
            if (sp[6].inst.size() != 2 || sp[6].inst[0].name != "i-tmplA" || sp[6].inst[1].name != "i-tmplA-1" || sp[6].inst[1].target != "tmplA"
                || sp[6].inst[1].states != T802V("a24", "a43", "a29"))
                Fail("t1062 N2: his entry renamed past the clash, its target and states unchanged");
            if (GdcStripChar(&got[0], got.size(), A, &back3) != kStripOk || back3 != dstE) Fail("t1062 N2: A stripped again gives the record's copy back");
        }
    }
    /* N5: his record a43 refers to a name outside the file ("a24-1") - a24, renamed past the record's copy's a24, never takes it */
    {
        std::vector<std::string> none;
        std::vector<std::vector<std::string> > noSt;
        std::vector<char> srcR, dstR, got;
        RecPutI32(&srcR, 15); RecPutI32(&srcR, 99); RecPutI32(&srcR, 3);
        T802Rec(&srcR, 0x24, "a24", 11, -1, none, none, noSt);
        T802Rec(&srcR, 0x43, "a43", 0, -1, T802V("a24-1"), none, noSt);
        std::vector<std::vector<std::string> > stA(1, T802V("a24", "a43"));
        T802Rec(&srcR, 0x1E, "sq", 0, 1, none, T802V("tmplA"), stA);
        RecPutI32(&dstR, 15); RecPutI32(&dstR, 99); RecPutI32(&dstR, 3);
        T802Rec(&dstR, 0x24, "a24", 22, -1, none, none, noSt);
        T802Rec(&dstR, 0x43, "aItem", 0, -1, none, none, noSt);
        std::vector<std::vector<std::string> > stB(1, T802V("a24", "aItem"));
        T802Rec(&dstR, 0x1E, "sq", 0, 1, none, T802V("tmplB"), stB);
        std::vector<GdcRecSpan> sp;
        size_t e = 0;
        if (GdcAddChar(&srcR[0], srcR.size(), &dstR[0], dstR.size(), A, &got) != kStripOk) Fail("t1062 N5: the copy-back is made");
        else if (!GdcSpans(&got[0], got.size(), &e, &sp) || sp.size() != 5) Fail("t1062 N5: the result parses");
        else if (sp[2].sid != "a24-2" || sp[3].sid != "a43" || sp[3].listRefs != T802V("a24-1") || sp[4].inst.size() != 2 || sp[4].inst[1].states != T802V("a24-2", "a43"))
            Fail("t1062 N5: a fresh name avoids every name his records refer to");
    }
    /* F7: the load's last resort - nobody settled is the check's own fixed point (a re-run refuses nothing more, changes nothing) */
    {
        std::vector<RosterSquad> f(2);
        f[0].key = "X"; f[0].apply = 1; f[0].saveChars = T802V("a", "b"); f[0].recChars = T802V("a");
        f[1].key = "Y"; f[1].apply = 1; f[1].saveChars = T802V("y"); f[1].recChars = T802V("y");
        RosterNoSettle(&f);
        if (RosterResolve(&f, T802V(0), &rc, &left) != 1 || f[0].apply || !f[0].conflict || !f[1].apply || !f[0].addBack.empty() || !f[0].leftOut.empty()
            || rc.addBack != 0 || rc.leftOut != 0)
            Fail("t1062 F7: with nobody settled a lost person refuses his squad (the rule before per-person settling)");
        std::vector<RosterSquad> again2(f);
        if (RosterResolve(&again2, T802V(0), &rc, &left) != 0 || again2[0].apply || !again2[1].apply || !again2[1].addBack.empty())
            Fail("t1062 F7: the result is a fixed point");
    }
}
void t_t802_fold2_strip_rules()
{
    using namespace coopown;
    /* the taking record not applied in this load (or an older one applied): no strip, the entry kept */
    if (PendStripDecide(true, 0, 5, 1) != kPendTakerNotLoaded || !PendStripKeepAfter(kPendTakerNotLoaded)) Fail("t802 fold 2: the taking record not loaded -> no strip, entry kept");
    if (PendStripDecide(true, 4, 5, 1) != kPendTakerNotLoaded) Fail("t802 fold 2: an older taking record loaded is not the move -> no strip");
    /* the person moved back (the taking squad's current record no longer holds him): no strip, the entry dropped */
    if (PendStripDecide(true, 5, 5, 0) != kPendTakerLost || PendStripKeepAfter(kPendTakerLost)) Fail("t802 fold 2: moved back -> no strip, entry dropped");
    if (PendStripDecide(true, 0, 5, 0) != kPendTakerLost) Fail("t802 fold 2: moved back is dropped even when the taking record is not loaded");
    if (PendStripDecide(true, 5, 5, -1) != kPendTakerUnread || !PendStripKeepAfter(kPendTakerUnread)) Fail("t802 fold 2: the taking record unreadable -> no strip, entry kept");
    if (PendStripDecide(true, 5, 5, 1) != kPendApply || PendStripDecide(true, 6, 5, 1) != kPendApply) Fail("t802 fold 2: confirmed -> strip");
    if (PendStripDecide(false, 5, 5, 1) != kPendGone || PendStripKeepAfter(kPendGone)) Fail("t802 fold 2: the file no longer holds him -> entry dropped");
    std::vector<PendStrip> v;
    PendStrip a;
    a.oldSid = "D"; a.who = "p"; a.seq = 5; a.newSid = "N";
    PendStripAdd(&v, a);
    a.who = "q";
    PendStripAdd(&v, a);
    if (!PendStripDropOne(&v, "D", "p") || v.size() != 1 || v[0].who != "q" || PendStripDropOne(&v, "D", "p")) Fail("t802 fold 2: one entry dropped, once");
    /* a full list refuses the move (its record is refused as before), counted by the caller */
    v.clear();
    for (size_t i = 0; i < kPendStripCap; ++i)
    {
        PendStrip c = a;
        c.who = "w" + I((int)i);
        PendStripAdd(&v, c);
    }
    if (v.size() != kPendStripCap || PendStripNewCount(v, "D", T802V("new")) != 1 || PendStripFits(v.size(), 0, 1)) Fail("t802 fold 2: a full list refuses a new move");
    if (PendStripNewCount(v, "D", T802V("w0")) != 0 || !PendStripFits(v.size(), 0, 0)) Fail("t802 fold 2: an entry already listed takes no new room");
    if (PendStripNewCount(v, "D", T802V("x", "x")) != 1) Fail("t802 fold 2: one person twice is one entry");
    if (PendStripFits(60, 3, 2) || !PendStripFits(60, 2, 2)) Fail("t802 fold 2: slots promised earlier in the same load count");
}

/* T-802: the only person of a squad file strips too - the copy's squad record has char count 0 and no instance, every other
   byte as before, and it parses with nobody in it; the other refusals still hold for the last person. */
void t_t802_strip_last_member()
{
    using namespace coopown;
    const std::string A = HandId(11, 1), B = HandId(22, 1);
    std::vector<char> one = T802Squad(true, false, -1, false, true), out;
    if (GdcStripChar(&one[0], one.size(), A, &out) != kStripOk || out.empty()) { Fail("t802 last: the only person strips"); return; }
    if (out != T802Squad(false, false, -1, false, true)) Fail("t802 last: the copy is the squad record alone, byte for byte");
    std::vector<GdcRecSpan> sp;
    size_t end = 0;
    if (!GdcSpans(&out[0], out.size(), &end, &sp) || end != out.size() || sp.size() != 1 || sp[0].type != 0x1E || sp[0].sid != "sq" || !sp[0].hasCharCount
        || sp[0].charCount != 0 || !sp[0].inst.empty() || sp[0].fields != 1 || sp[0].lists != 0)
        Fail("t802 last: the copy holds one squad record with char count 0 and no instance");
    GdcScanOut so;
    if (!GdcScan(&out[0], out.size(), "s", &so) || !so.chars.empty() || so.noHandle != 0) Fail("t802 last: the copy parses again with no character");
    /* both people of a squad of two, one after the other: the second strip empties it the same way */
    std::vector<char> two = T802Squad(true, true, -1, false, true), mid, last;
    if (GdcStripChar(&two[0], two.size(), A, &mid) != kStripOk || mid.empty() || GdcStripChar(&mid[0], mid.size(), B, &last) != kStripOk
        || last != T802Squad(false, false, -1, false, true))
        Fail("t802 last: a squad of two stripped of both is the squad record alone");
    /* a squad file with nobody: there is no one to strip */
    std::vector<char> nobody = T802Squad(false, false, -1, false, true);
    out.clear();
    if (GdcStripChar(&nobody[0], nobody.size(), A, &out) != kStripNoChar || !out.empty()) Fail("t802 last: a squad with nobody refuses, nothing written");
    /* every other check still holds when the person is the last one */
    std::vector<char> cc = T802Squad(true, false, 2, false, true);
    if (GdcStripChar(&cc[0], cc.size(), A, &out) != kStripCount || !out.empty()) Fail("t802 last: a char count that is not the instance count still refuses");
    std::vector<char> ni = T802Squad(true, false, -1, false, false);
    if (GdcStripChar(&ni[0], ni.size(), A, &out) != kStripNoInstance || !out.empty()) Fail("t802 last: a character no instance names still refuses");
    std::vector<char> ly = T802Squad(true, false, -1, false, true, 2);
    if (GdcStripChar(&ly[0], ly.size(), A, &out) != kStripLayout || !out.empty()) Fail("t802 last: a squad record with another field still refuses (layout)");
}

/* T-823: a squad's head record (its state record less hands, hand-list counts, references and instances): keys, encode /
   decode, the field filter, the refusals, and records of other kinds (src/common/ownrec.h SquadHead*). */
void t_t823_squad_head()
{
    using namespace coopown;
    if (SquadHeadKey("Drifters_12") != "pp.sqhead.Drifters_12") Fail("t823 head: a clean id keeps its part");
    if (SquadHeadKey("a b").substr(10) != SquadKey("a b").substr(9)) Fail("t823 head: the head key's part is the squad key's part (CRC included)");
    if (!SquadHeadKey("").empty()) Fail("t823 head: an empty id has no head key");
    if (IsSquadKey(SquadHeadKey("x")) || !IsSquadHeadKey(SquadHeadKey("x")) || IsSquadHeadKey("pp.squad.x") || IsSquadHeadKey("pp.sqhead."))
        Fail("t823 head: a head key is never a squad key, and the reverse");
    if (HeadKeyOfSquad(SquadKey("a b")) != SquadHeadKey("a b") || SquadKeyOfHead(SquadHeadKey("a b")) != SquadKey("a b")
        || !HeadKeyOfSquad("pp.money").empty() || !SquadKeyOfHead("pp.squad.x").empty())
        Fail("t823 head: the head and squad keys map onto each other only");
    if (!(std::string("pp.sqhead.x") < std::string("pp.squad.x"))) Fail("t823 head: a head key sorts before its squad key (the writer commits it first)");
    /* the filter: no hand int, no hand-list count, no money (it travels by the money-known rule); everything else kept */
    if (HeadFieldKept(kHeadInt, "handleTYPE") || HeadFieldKept(kHeadInt, "hometownhandleS") || HeadFieldKept(kHeadInt, "separatedhandleI")
        || HeadFieldKept(kHeadInt, "slave count") || HeadFieldKept(kHeadInt, "owned stuff count") || HeadFieldKept(kHeadStr, ""))
        Fail("t823 head: a hand or a hand-list count is kept");
    if (HeadFieldKept(kHeadInt, kRecMoneyKey) || HeadFieldKept(kHeadFloat, "money") || HeadFieldKept(kHeadStr, "money"))
        Fail("t823 head: the squad's money is kept in a head (it would bypass the money-known rule)");
    if (!HeadFieldKept(kHeadInt, "money count") || !HeadFieldKept(kHeadInt, "char count") || !HeadFieldKept(kHeadInt, "squad index")
        || !HeadFieldKept(kHeadStr, "squad template") || !HeadFieldKept(kHeadBool, "persistent") || !HeadFieldKept(kHeadVec, "position"))
        Fail("t823 head: a plain field is dropped");
    /* round trip, fields handed over in any order */
    SquadHead h;
    h.sid = "Sq 7"; h.type = 0x22;
    h.b.push_back(std::make_pair(std::string("persistent"), 1)); h.b.push_back(std::make_pair(std::string("intact"), 0));
    h.f.push_back(std::make_pair(std::string("inventory refresh time"), 2.5f));
    h.i.push_back(std::make_pair(std::string("squad index"), 4)); h.i.push_back(std::make_pair(std::string("char count"), -12));
    std::vector<float> pos; pos.push_back(1.0f); pos.push_back(-2.0f); pos.push_back(3.5f);
    h.v.push_back(std::make_pair(std::string("position"), pos));
    std::vector<float> rot; rot.push_back(1.0f); rot.push_back(0.0f); rot.push_back(0.5f); rot.push_back(0.0f);
    h.r.push_back(std::make_pair(std::string("facing"), rot));
    h.s.push_back(std::make_pair(std::string("squad template"), std::string("tmpl-1")));
    h.s.push_back(std::make_pair(std::string("basetown"), std::string("")));
    h.file.push_back(std::make_pair(std::string("content file"), std::string("platoon/Sq 7")));
    std::vector<char> b;
    if (!EncodeSquadHead(h, &b) || b.empty()) Fail("t823 head: a head with a sid encodes");
    SquadHead o;
    if (!DecodeSquadHead(b, &o)) { Fail("t823 head: its own bytes decode"); return; }
    if (o.sid != "Sq 7" || o.type != 0x22 || o.b.size() != 2 || o.b[0].first != "intact" || o.b[0].second != 0 || o.b[1].second != 1
        || o.f.size() != 1 || o.f[0].second != 2.5f || o.i.size() != 2 || o.i[0].first != "char count" || o.i[0].second != -12 || o.i[1].second != 4
        || o.v.size() != 1 || o.v[0].second != pos || o.r.size() != 1 || o.r[0].second != rot
        || o.s.size() != 2 || o.s[0].first != "basetown" || !o.s[0].second.empty() || o.s[1].second != "tmpl-1"
        || o.file.size() != 1 || o.file[0].second != "platoon/Sq 7")
        Fail("t823 head: the round trip keeps every field, sorted by key");
    std::vector<char> b2;
    EncodeSquadHead(o, &b2);
    if (b2 != b) Fail("t823 head: the same fields make the same bytes (the writer's skip-unchanged)");
    SquadHead none;
    if (EncodeSquadHead(none, &b2) || !b2.empty()) Fail("t823 head: a head with no sid is not written");
    none.sid = "z";
    if (!EncodeSquadHead(none, &b2) || !DecodeSquadHead(b2, &o) || o.sid != "z" || !o.i.empty()) Fail("t823 head: an empty state record round-trips");
    /* refusals: never half-read */
    for (size_t cut = 0; cut < b.size(); ++cut)
    {
        std::vector<char> c(b.begin(), b.begin() + cut);
        SquadHead x; x.sid = "keep";
        if (DecodeSquadHead(c, &x) || x.sid != "keep") { Fail("t823 head: a cut-short record is refused and leaves the output alone"); break; }
    }
    { std::vector<char> c(b); c.push_back(0); if (DecodeSquadHead(c, &o)) Fail("t823 head: trailing bytes are refused"); }
    SquadHead hh(h);
    hh.i.push_back(std::make_pair(std::string("handleS"), 9));
    EncodeSquadHead(hh, &b2);
    if (DecodeSquadHead(b2, &o)) Fail("t823 head: a record holding a hand is refused");
    hh = h; hh.i.push_back(std::make_pair(std::string("char count"), 3));
    EncodeSquadHead(hh, &b2);
    if (DecodeSquadHead(b2, &o)) Fail("t823 head: a key twice in one map is refused");
    hh = h; hh.i.push_back(std::make_pair(std::string("money"), 3));
    EncodeSquadHead(hh, &b2);
    if (DecodeSquadHead(b2, &o)) Fail("t823 head: a record holding the squad's money is refused");
    /* a map over kHeadMaxFields is never written (the decoder would refuse it); exactly kHeadMaxFields is written and read */
    hh = SquadHead(); hh.sid = "big";
    for (int k = 0; k < kHeadMaxFields; ++k) { char nm[16]; _snprintf_s(nm, sizeof(nm), _TRUNCATE, "f%05d", k); hh.b.push_back(std::make_pair(std::string(nm), k & 1)); }
    if (!EncodeSquadHead(hh, &b2) || !DecodeSquadHead(b2, &o) || (int)o.b.size() != kHeadMaxFields) Fail("t823 head: a map of exactly kHeadMaxFields round-trips");
    hh.b.push_back(std::make_pair(std::string("zzzzz"), 0));
    if (EncodeSquadHead(hh, &b2) || !b2.empty()) Fail("t823 head: a map over kHeadMaxFields is not written");
    /* a filled head: a content file naming its own id's squad file, and a template */
    if (!HeadFilled(h)) Fail("t823 head: a head with content file platoon/<its id> and a template is filled");
    hh = h; hh.file[0].second = "platoon/Sq 7.platoon";
    if (!HeadFilled(hh)) Fail("t823 head: the content file with the .platoon ending is taken too");
    hh = h; hh.file[0].second = "platoon/Sq 8";
    if (HeadFilled(hh)) Fail("t823 head: a content file naming another squad's file is not filled");
    hh = h; hh.file[0].second = "";
    if (HeadFilled(hh)) Fail("t823 head: an empty content file (a state record the engine has not saved) is not filled");
    hh = h; hh.file.clear();
    if (HeadFilled(hh)) Fail("t823 head: no content file is not filled");
    hh = h; hh.s[0].second = "";
    if (HeadFilled(hh)) Fail("t823 head: an empty squad template is not filled");
    hh = h; hh.s.clear();
    if (HeadFilled(hh)) Fail("t823 head: no squad template is not filled");
    { std::vector<char> c(b); for (size_t k = 0; k + 1 < c.size(); ++k) if (c[k] == 't' && c[k + 1] == 0x01) { c[k + 1] = 0x02; break; }
      if (DecodeSquadHead(c, &o)) Fail("t823 head: a bool that is not 0 / 1 is refused"); }
    /* a record of another kind is not a head; a head is not money */
    std::vector<char> mb; EncodeMoney(5, &mb);
    if (DecodeSquadHead(mb, &o)) Fail("t823 head: a money record is not a head");
    int m = 0;
    if (DecodeMoney(b, &m)) Fail("t823 head: a head is not a money record");
    if (DecodeSquadHead(std::vector<char>(), &o)) Fail("t823 head: no bytes are not a head");
}

/* T-823: who is a restore candidate (RestoreVerdict), and the roster check with restore candidates in it (RosterResolve):
   a split's source squad passes with the split-off squad's record; without head records the check is as before. */
void t_t823_restore_roster()
{
    using namespace coopown;
    const std::string k = SquadKey("New squad 3");
    SquadHead hd;
    hd.sid = "New squad 3";
    hd.s.push_back(std::make_pair(std::string("squad template"), std::string("tmpl")));
    hd.file.push_back(std::make_pair(std::string("content file"), std::string("platoon/New squad 3")));
    SquadHead other(hd); other.sid = "Other"; other.file[0].second = "platoon/Other";
    SquadHead unfilled(hd); unfilled.file[0].second = "";
    if (RestoreVerdict(true, true, true, hd, k) != kRestoreInSave) Fail("t823 roster: a squad the save has is never a restore candidate");
    if (RestoreVerdict(false, false, false, SquadHead(), k) != kRestoreNoHead) Fail("t823 roster: an old record with no head is dropped as before");
    if (RestoreVerdict(false, true, false, SquadHead(), k) != kRestoreHeadBad) Fail("t823 roster: an unreadable head is not a candidate");
    if (RestoreVerdict(false, true, true, other, k) != kRestoreHeadOther) Fail("t823 roster: a head whose state record is another squad's is not a candidate");
    if (RestoreVerdict(false, true, true, unfilled, k) != kRestoreHeadUnfilled) Fail("t823 roster: an unfilled head (no content file of its own) is not a candidate");
    if (RestoreVerdict(false, true, true, hd, k) != kRestoreYes) Fail("t823 roster: a recorded squad missing from the save with a filled head is a candidate");
    if (std::string(RestoreVerdictName(kRestoreYes)) != "candidate" || std::string(RestoreVerdictName(kRestoreHeadUnfilled)) != "head-unfilled"
        || std::string(RestoreVerdictName(99)) != "?") Fail("t823 roster: verdict names");
    RosterCounts rc;
    /* the split: A had a and b at the save; b was split off into B, which the save does not have */
    std::vector<RosterSquad> today(1);
    today[0].key = "A"; today[0].apply = 1; today[0].saveChars = T802V("a", "b"); today[0].recChars = T802V("a");
    if (RosterResolve(&today, T802V("e"), &rc) != 0 || !today[0].apply || today[0].conflict || today[0].addBack != T802V("b"))
        Fail("t823 roster (T-1062, design 3.3): without B the source applies with b copied back - the load cannot rebuild B, so b stays in A");
    std::vector<RosterSquad> r(2);
    r[0] = RosterSquad(); r[0].key = "A"; r[0].apply = 1; r[0].saveChars = T802V("a", "b"); r[0].recChars = T802V("a");
    r[1].key = "B"; r[1].apply = 1; r[1].restore = 1; r[1].recChars = T802V("b");
    if (RosterResolve(&r, T802V("e"), &rc) != 0 || !r[0].apply || !r[1].apply || rc.restored != 1 || rc.conflict != 0 || rc.empty != 0)
        Fail("t823 roster: the split's source passes with the split-off squad as a restore candidate");
    /* the load's wiring: the live check (no restore candidates) applies the source with b copied back (T-1062); the dry run starts
       from the roster as it was before that check and sees the source pass with b in B instead - nothing copied back */
    {
        std::vector<RosterSquad> unmarked(1);
        unmarked[0].key = "A"; unmarked[0].apply = 1; unmarked[0].saveChars = T802V("a", "b"); unmarked[0].recChars = T802V("a");
        std::vector<RosterSquad> restores(1);
        restores[0].key = "B"; restores[0].apply = 1; restores[0].restore = 1; restores[0].recChars = T802V("b");
        std::vector<RosterSquad> live(unmarked), shadow;
        RosterCounts rcLive, rc2;
        RosterResolve(&live, T802V("e"), &rcLive);
        if (!live[0].apply || rcLive.conflict != 0 || rcLive.addBack != 1) Fail("t823 roster: the live check applies the split's source with b copied back");
        if (RosterShadowRun(unmarked, live, restores, T802V("e"), &shadow, &rc2) != 0 || shadow.size() != 2 || !shadow[0].apply
            || !shadow[1].apply || rc2.restored != 1 || rc2.conflict != 0 || rc2.addBack != 0 || !shadow[0].addBack.empty())
            Fail("t823 roster: the dry run sees the split's source pass with b in the restored squad (nothing copied back)");
        if (unmarked[0].conflict || !unmarked[0].apply || !unmarked[0].addBack.empty()) Fail("t823 roster: the dry run leaves the unmarked roster alone");
    }
    /* a restore candidate whose record holds nobody is refused at once */
    r.assign(1, RosterSquad());
    r[0].key = "B"; r[0].apply = 1; r[0].restore = 1;
    if (RosterResolve(&r, T802V("a"), &rc) != 1 || r[0].apply || !r[0].empty || rc.empty != 1 || rc.restored != 0) Fail("t823 roster: an empty restore candidate is refused");
    /* a squad made and merged away after the save: its record still names b, now in C's record - the squad the save has is kept */
    r.assign(2, RosterSquad());
    r[0].key = "C"; r[0].apply = 1; r[0].saveChars = T802V("c"); r[0].recChars = T802V("c", "b");
    r[1].key = "B"; r[1].apply = 1; r[1].restore = 1; r[1].recChars = T802V("b");
    std::vector<RosterSquad> r2(r);
    std::swap(r2[0], r2[1]);
    if (RosterResolve(&r, T802V("e"), &rc) != 1 || !r[0].apply || r[1].apply || !r[1].duplicate || rc.restored != 0)
        Fail("t823 roster: a stale restore candidate is refused before the squad of the save");
    if (RosterResolve(&r2, T802V("e"), &rc) != 1 || !r2[1].apply || r2[0].apply || !r2[0].duplicate)
        Fail("t823 roster: the stale restore candidate is refused whatever the order");
    /* without restore candidates the outcome is today's, case by case */
    r.assign(2, RosterSquad());
    r[0].key = "X"; r[0].apply = 1; r[0].saveChars = T802V("a"); r[0].recChars = T802V("q");
    r[1].key = "Y"; r[1].apply = 1; r[1].saveChars = T802V("q"); r[1].recChars = T802V("z");
    if (RosterResolve(&r, T802V(0), &rc) != 0 || !r[0].apply || !r[1].apply || rc.restored != 0 || r[0].addBack != T802V("a"))
        Fail("t823 roster: no restore candidate - the lost person is copied back (T-1062)");
    r.assign(1, RosterSquad());
    r[0].key = "X"; r[0].apply = 1;
    if (RosterResolve(&r, T802V(0), &rc) != 1 || !r[0].empty) Fail("t823 roster: a squad of the save with no character is still refused (empty)");
    /* a squad split off with everybody (the source left in the save, not a candidate): the person is doubled until the
       source is stripped of him - he is a move from that squad, as for any candidate */
    std::vector<RosterFixedSquad> fx(1);
    fx[0].key = "D"; fx[0].chars = T802V("r", "q");
    r.assign(1, RosterSquad());
    r[0].key = "B"; r[0].apply = 1; r[0].restore = 1; r[0].recChars = T802V("r");
    std::vector<RosterMove> mv;
    RosterFindMoves(r, fx, &mv);
    if (mv.size() != 1 || mv[0].cand != 0 || mv[0].from != 0 || mv[0].who != "r") Fail("t823 roster: a person in a restore candidate and in one squad of the save only is a move from it");
}

/* T-802 fold: the strip over every person of every squad file copied from a real save (the main checkout's
   evidence/T1000/store-copy, read only; skipped when it is not there): it parses, the person is gone, char count is one
   lower, and every other record is byte-identical. A one-person squad strips to its squad record with char count 0. */
std::string T313ReadFile(const char* path);
void t_t802_real_squad_files()
{
    using namespace coopown;
    const std::string dir = "C:\\code\\kenshi-coop\\evidence\\T1000\\store-copy\\";
    struct _finddata_t fd;
    const intptr_t h = _findfirst((dir + "*.platoon").c_str(), &fd);
    if (h == -1) { Skip("the evidence folder " + dir + " holds no squad file - nothing checked"); return; }
    int files = 0, ok = 0, oneFiles = 0, okOne = 0, clashes = 0;
    do
    {
        const std::string fn(fd.name);
        if (fn.compare(0, 5, "zone.") == 0) continue;
        const std::string b = T313ReadFile((dir + fn).c_str());
        std::vector<GdcRecSpan> rs;
        size_t end = 0;
        if (b.empty() || !GdcSpans(b.data(), b.size(), &end, &rs) || end != b.size()) { Fail("t802 real: " + fn + " does not parse"); continue; }
        ++files;
        std::map<std::string, std::string> bytesBySid;
        int chars = 0, sqCount = -1;
        for (size_t i = 0; i < rs.size(); ++i)
        {
            bytesBySid[rs[i].sid] = b.substr(rs[i].from, rs[i].to - rs[i].from);
            if (rs[i].type == 0x24) ++chars;
            if (rs[i].type == 0x1E) sqCount = rs[i].charCount;
        }
        if (chars == 1) ++oneFiles;
        GdcScanOut before;
        GdcScan(b.data(), b.size(), fn, &before);
        bool clashTried = false;
        for (size_t i = 0; i < rs.size(); ++i)
        {
            if (rs[i].type != 0x24) continue;
            std::vector<char> out;
            const int r = GdcStripChar(b.data(), b.size(), rs[i].hand, &out);
            if (r != kStripOk) { Fail("t802 real: " + fn + " " + rs[i].hand + " refused: " + StripWhyName(r)); continue; }
            ++ok;
            if (chars == 1) ++okOne;
            std::vector<GdcRecSpan> os;
            size_t oe = 0;
            GdcScanOut after;
            if (!GdcSpans(&out[0], out.size(), &oe, &os) || oe != out.size() || !GdcScan(&out[0], out.size(), fn, &after)
                || after.chars.size() + 1 != before.chars.size() || std::find(after.chars.begin(), after.chars.end(), rs[i].hand) != after.chars.end())
            {
                Fail("t802 real: " + fn + " " + rs[i].hand + ": the copy does not parse or still holds the person");
                continue;
            }
            {   /* T-1062: the person copied back from the save's file into the copy without him - he is in it once, and stripped again
                   the copy comes back byte for byte (GdcAddChar's own self-check, run here on the engine's real files) */
                std::vector<char> added;
                GdcScanOut as;
                const int ra = GdcAddChar(b.data(), b.size(), &out[0], out.size(), rs[i].hand, &added);
                if (ra != kStripOk) Fail("t1062 real: " + fn + " " + rs[i].hand + " copy-back refused: " + StripWhyName(ra));
                else if (!GdcScan(&added[0], added.size(), fn, &as) || as.chars.size() != before.chars.size()
                         || std::count(as.chars.begin(), as.chars.end(), rs[i].hand) != 1)
                    Fail("t1062 real: " + fn + " " + rs[i].hand + ": the copy-back does not hold the squad's people with him once");
            }
            if (chars >= 2 && !clashTried)
            {   /* T-1062: the record's copy written by another session - its records renamed to the person's own names in the save's
                   file (the engine's per-process counter can give both the same names): he still comes back, his records renamed,
                   just before the squad record, every name in the result once */
                clashTried = true;
                std::set<std::string> outSids;
                for (size_t j = 0; j < os.size(); ++j) outSids.insert(os[j].sid);
                std::vector<std::string> his;
                for (size_t j = 0; j < rs.size(); ++j) if (outSids.count(rs[j].sid) == 0) his.push_back(rs[j].sid);
                std::map<std::string, std::string> ren;
                size_t hk = 0;
                for (size_t j = 0; j < os.size() && hk < his.size(); ++j) if (os[j].type != 0x1E) ren[os[j].sid] = his[hk++];
                std::vector<char> clash(out.begin(), out.begin() + 12);
                bool built = !ren.empty();
                for (size_t j = 0; j < os.size() && built; ++j) built = GdcRenamedRec(&out[0], os[j].from, os[j].to, ren, &clash);
                std::vector<GdcRecSpan> cs, gs;
                size_t ce = 0, ge = 0;
                std::vector<char> got;
                GdcScanOut gsc;
                if (!built || !GdcSpans(&clash[0], clash.size(), &ce, &cs) || ce != clash.size()) Fail("t1062 real clash: " + fn + ": the clashing copy could not be built");
                else
                {
                    const int rc2 = GdcAddChar(b.data(), b.size(), &clash[0], clash.size(), rs[i].hand, &got);
                    std::set<std::string> names;
                    if (rc2 != kStripOk) Fail("t1062 real clash: " + fn + " " + rs[i].hand + " copy-back refused: " + StripWhyName(rc2));
                    else if (!GdcSpans(&got[0], got.size(), &ge, &gs) || ge != got.size() || gs.empty() || gs.back().type != 0x1E
                             || !GdcScan(&got[0], got.size(), fn, &gsc) || gsc.chars.size() != before.chars.size()
                             || std::count(gsc.chars.begin(), gsc.chars.end(), rs[i].hand) != 1)
                        Fail("t1062 real clash: " + fn + " " + rs[i].hand + ": the result does not parse, hold everyone once, or end with the squad record");
                    else
                    {
                        for (size_t j = 0; j < gs.size(); ++j) names.insert(gs[j].sid);
                        if (names.size() != gs.size()) Fail("t1062 real clash: " + fn + ": two records share a name in the result");
                        else ++clashes;
                    }
                }
            }
            for (size_t j = 0; j < os.size(); ++j)
            {
                if (os[j].type == 0x1E)
                {
                    if (os[j].charCount != sqCount - 1 || (int)os[j].inst.size() != sqCount - 1) Fail("t802 real: " + fn + " " + rs[i].hand + ": char count not lowered by one");
                    continue;
                }
                std::map<std::string, std::string>::const_iterator f = bytesBySid.find(os[j].sid);
                if (f == bytesBySid.end() || f->second != std::string(&out[os[j].from], os[j].to - os[j].from))
                {
                    Fail("t802 real: " + fn + " " + rs[i].hand + ": another record changed (" + os[j].sid + ")");
                    break;
                }
            }
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
    if (files == 0 || ok - okOne == 0) Fail("t802 real: no squad file of two or more people was stripped");
    if (clashes == 0) Fail("t1062 real clash: no clashing copy-back was made");
    if (oneFiles > 0 && okOne == 0) Fail("t802 real: no one-person squad file was stripped to an empty squad record");
}

/* mmo1c fold (review-mmo1c MED2): the shape bits and the compare trigger (src/common/ownrec.h CompareDue). */
void t_mmo1c_compare_due()
{
    using namespace coopown;
    char buf[8]; PodBuf b; b.shape = 0xFFFFu; PodInit(&b, buf, sizeof(buf));
    if (b.shape != 0) Fail("mmo1c fold: PodInit clears the shape");
    if (ShapeStrBit(15) != kShapeInlineStr || ShapeStrBit(16) != kShapeHeapStr) Fail("mmo1c fold: capacity 15 is inline, 16 is heap (the engine's own test)");
    if (ShapeMapBit(0) != 0x1u || ShapeMapBit(6) != 0x40u || ShapeMapBit(7) != 0 || ShapeMapBit(-1) != 0) Fail("mmo1c fold: map bits 0..6 only");
    const unsigned int all = 0x7Fu | kShapeRefEntries | kShapeInstances | kShapeInstStrings | kShapeInlineStr | kShapeHeapStr | kShapeTwoRecords;
    if (all != 0x1FFFu) Fail("mmo1c fold: the thirteen shape bits are distinct and contiguous");
    /* the first 3 of the process always compare */
    if (!CompareDue(0x400u, 0x400u, 5, 0, 3, 128) || !CompareDue(0x400u, 0x400u, 5, 2, 3, 128)) Fail("mmo1c fold: the first 3 compares always run");
    /* after that: only a new bit or every 128th write */
    if (CompareDue(0x400u, 0x400u, 5, 3, 3, 128) || CompareDue(0u, 0x400u, 127, 9, 3, 128) || CompareDue(0x400u, 0xC00u, 129, 9, 3, 128))
        Fail("mmo1c fold: a seen shape between the 128ths does not compare");
    if (!CompareDue(0x800u, 0x400u, 5, 3, 3, 128) || !CompareDue(0x1400u, 0x400u, 7, 9, 3, 128)) Fail("mmo1c fold: a bit no compare saw compares");
    if (!CompareDue(0x400u, 0x400u, 128, 9, 3, 128) || !CompareDue(0x400u, 0x400u, 256, 9, 3, 128)) Fail("mmo1c fold: every 128th write compares");
    if (CompareDue(0x400u, 0x400u, 0, 9, 3, 128) || CompareDue(0x400u, 0x400u, 128, 9, 3, 0)) Fail("mmo1c fold: write 0 / every=0 never trigger the periodic compare");
    /* re-armed (seen cleared): the next write's bits are all new */
    if (!CompareDue(0x400u, 0u, 1, 9, 3, 128)) Fail("mmo1c fold: after a re-arm the first write compares");
}

/* mmo1: the own-squad record writer's pure parts (src/common/ownrec.h) - the SAME header the plugin compiles. File
   naming, the index line, the write decision (skippedSame), the commit verdict (the read-back), and the pacing: the
   cursor pays out EXACTLY n turns per 30 s (not P8f's per-call round-up), the 2 s gap, the 16/s cap, the slow rule. */
void t_mmo1_ownrec()
{
    using namespace coopown;
    /* T-251: the player's own records store - one per world + profile, a name the wire accepts, never a test name */
    {
        const std::string a = PlayerStoreName("Holdout", "p1"), b = PlayerStoreName("Holdout", "p2");
        const std::string c = PlayerStoreName("Hold out", "p1"), d = PlayerStoreName("Hold_out", "p1");
        const std::string l = PlayerStoreName(std::string(80, 'w') + "/..\\x y", "id with spaces");
        if (a.empty() || a != PlayerStoreName("Holdout", "p1") || a.compare(0, 11, "mp-holdout-") != 0 || a.size() != 19)
            Fail("T-251: the player's store name is mp-<world>-<8 hex>, the same every time");
        if (a == b || c == d || c.compare(0, 12, "mp-hold_out-") != 0) Fail("T-251: two profiles / two worlds never share a store");
        {   /* T-251 fold item 5: the world key part is lower case (store folders, like save folders, ignore case) */
            const std::string u = PlayerStoreName("HOLD Out", "p1");
            bool upper = false;
            for (std::string::size_type i = 0; i < u.size(); ++i) if (u[i] >= 'A' && u[i] <= 'Z') upper = true;
            if (upper || u.compare(0, 12, "mp-hold_out-") != 0 || !restoreguard::OwnStoreNameOk(u)) Fail("T-251 fold 5: the store name is all lower case");
            if (u == c) Fail("T-251 fold 5: two world keys that differ only in case still keep their own hash");
        }
        if (!restoreguard::OwnStoreNameOk(a) || !restoreguard::OwnStoreNameOk(c) || !restoreguard::OwnStoreNameOk(l) || l.size() != 42)
            Fail("T-251: the player's store name is always a wire-legal store name, 42 characters at most");
        if (!PlayerStoreName("", "p1").empty() || !PlayerStoreName("Holdout", "").empty()) Fail("T-251: no world or no profile -> no store");
        if (!AutoStoreOpenWanted(true, true, false, a, "", false)) Fail("T-251: a decided folder opens the player's store");
        if (AutoStoreOpenWanted(false, true, false, a, "", false)) Fail("T-251: ownsave=off -> no store opens");
        if (AutoStoreOpenWanted(true, false, false, a, "", false)) Fail("T-251: no profile save folder (or single player) -> no store");
        if (AutoStoreOpenWanted(true, true, true, a, "", false)) Fail("T-251: a refusal / fault / repaired restart holds it off");
        if (AutoStoreOpenWanted(true, true, false, "", "", false)) Fail("T-251: no name -> nothing");
        if (AutoStoreOpenWanted(true, true, false, a, a, true)) Fail("T-251: already open -> not again");
        if (!AutoStoreOpenWanted(true, true, false, a, b, true)) Fail("T-251: the player's store of another profile is replaced");
        if (AutoStoreOpenWanted(true, true, false, a, "cooptest-x", false)) Fail("T-251: a test's chosen store is never replaced");
        {   /* T-251 fold item 1: THE ORDER - title (folder decided at the WELCOME, no world) -> the automatic load's overlay step ->
               the world. The decision has no world input at all: the store opens at the title, and at the overlay step
               (OwnAutoStoreBeforeLoad asks the same question) it is already open, so the records are laid over the save. */
            std::string cur; bool curAuto = false;
            if (!AutoStoreOpenWanted(true, true, false, a, cur, curAuto)) Fail("T-251 fold 1: at the title (no world yet) the decided folder opens the store NOW");
            cur = a; curAuto = true;   /* OwnAutoStoreTick opened it at the title */
            if (AutoStoreOpenWanted(true, true, false, a, cur, curAuto) || cur != a) Fail("T-251 fold 1: at the load's overlay step the store is already open - nothing left to open");
            /* the load came first (same frame): the overlay step's own ask opens it before any file is read */
            if (!AutoStoreOpenWanted(true, true, false, a, "", false)) Fail("T-251 fold 1: a load before the tick still opens the store first (at the overlay step)");
            /* restart / crash: a fresh process at the title - nothing open, folder decided -> open before the load */
            if (!AutoStoreOpenWanted(true, true, false, b, "", false)) Fail("T-251 fold 1: after a restart the store opens before the first load");
            /* another world in the same session: leave -> no folder -> closed after the drain -> the next world's folder opens its own */
            if (!AutoStoreCloseWanted(true, a, "", true, true)) Fail("T-251 fold 3: leaving the world closes the player's store once drained");
            if (!AutoStoreOpenWanted(true, true, false, c, "", false)) Fail("T-251 fold 1: the next world's store opens at its title, before its load");
            if (!AutoStoreOpenWanted(true, true, false, c, a, true)) Fail("T-251 fold 1: not closed yet -> the next world's folder still switches the store before its load");
        }
        /* T-251 fold item 3: the store closes only when the folder is gone and nothing is in flight; a test's never */
        if (AutoStoreCloseWanted(true, a, a, true, true)) Fail("T-251 fold 3: the folder still decided -> kept");
        if (AutoStoreCloseWanted(true, a, "", false, true)) Fail("T-251 fold 3: writes still queued -> not yet");
        if (AutoStoreCloseWanted(true, a, "", true, false)) Fail("T-251 fold 3: a leave save running -> not yet");
        if (AutoStoreCloseWanted(false, "cooptest-x", "", true, true)) Fail("T-251 fold 3: a test's store is never closed here");
        if (AutoStoreCloseWanted(true, "", "", true, true)) Fail("T-251 fold 3: nothing open -> nothing to close");
        {   /* T-251 fold 2 (re-check item 1): a failed load-time open holds the automatic open until the next load */
            std::string held = AutoStoreHoldKept(std::string(), a, true);   /* the load begins */
            if (AutoStoreLoadHeld(held)) Fail("T-251 fold 2: a load begins with nothing held");
            held = AutoStoreHoldAfterLoadOpen(true, false, a);              /* the open at the load failed (wait cap, folder, checkpoint) */
            if (held != a || !AutoStoreLoadHeld(held)) Fail("T-251 fold 2: a failed load-time open holds the player's store");
            held = AutoStoreHoldKept(held, a, false);                       /* the world runs, the folder still decided */
            if (held != a || AutoStoreOpenWanted(true, true, AutoStoreLoadHeld(held), a, "", false))
                Fail("T-251 fold 2: load-time open failed -> no automatic open mid-world");
            held = AutoStoreHoldKept(held, a, true);                        /* the next load */
            if (AutoStoreLoadHeld(held) || !AutoStoreOpenWanted(true, true, AutoStoreLoadHeld(held), a, "", false))
                Fail("T-251 fold 2: the next load tries the open again");
            if (!AutoStoreHoldKept(a, "", false).empty()) Fail("T-251 fold 2: leaving to the title ends the hold");
            if (!AutoStoreHoldAfterLoadOpen(true, true, a).empty()) Fail("T-251 fold 2: an open at the load holds nothing");
            if (!AutoStoreHoldAfterLoadOpen(false, false, a).empty()) Fail("T-251 fold 2: no open tried -> no hold");
            /* re-check item 2: the store opened at the gate and refused (blocked) refuses that load */
            if (!restoreguard::OwnHighLoadHeld(1, false, true, 9, 5)) Fail("T-251 fold 2: a store refused at the load-time open refuses the load");
        }
        if (MarkWanted(true, a) || MarkWanted(true, "")) Fail("T-251 fold 3: a single-player save is never marked");
        if (!MarkWanted(false, a) || MarkWanted(false, "")) Fail("T-251 fold 3: a multiplayer save is marked only with a store open");
        /* T-251 fold item 2: a save the redirect REFUSES ends the leave save at once - never retried for 30 s */
        if (TestSaveRequestResult(0, true, 0) != kReqRefused || kReqRefused >= 0) Fail("T-251 fold 2: a refused request answers kReqRefused");
        if (TestSaveRequestResult(0, false, 0) != 0 || TestSaveRequestResult(1, false, 1) != 0) Fail("T-251 fold 2: another save pending -> retry (0)");
        if (TestSaveRequestResult(0, false, 1) != 1 || TestSaveRequestResult(-1, false, 1) != -1 || TestSaveRequestResult(0, false, -1) != -1)
            Fail("T-251 fold 2: accepted / unreadable as before");
        {
            int why = kLsWhyNone;
            if (LeaveSaveStep(kLsRequest, 0, 0, TestSaveRequestResult(0, true, 0), 0, true, false, &why) != kLsFinished || why != kLsWhyRefused)
                Fail("T-251 fold 2: a refused leave save finishes on its first step (0 ms) as refused");
            if (LeaveSaveStep(kLsRequest, 0, 0, TestSaveRequestResult(0, false, 0), 0, true, false, &why) != kLsRequest)
                Fail("T-251 fold 2: a busy save manager is still retried");
        }
        /* T-251 fold item 4: the timed autosave comes back while the writer fails */
        if (WriterFailing(kWriterFailN - 1, false, 0) || !WriterFailing(kWriterFailN, false, 0)) Fail("T-251 fold 4: N failed writes in a row = failing");
        if (WriterFailing(0, true, 2u * kPeriodMs - 1) || !WriterFailing(0, true, 2u * kPeriodMs)) Fail("T-251 fold 4: queued work unwritten for two heartbeats = failing");
        if (WriterFailing(0, false, 10u * kPeriodMs)) Fail("T-251 fold 4: nothing queued (unchanged records are not rewritten) is never failing");
        if (!AutosaveSkip(true, "autosave1", true, true, false)) Fail("T-251 fold 4: a healthy writer keeps the timed autosave off");
        if (AutosaveSkip(true, "autosave1", true, !WriterFailing(kWriterFailN, false, 0), false)) Fail("T-251 fold 4: a failing writer lets the timed autosave run");
    }
    if (SquadKey("Drifters_12") != "pp.squad.Drifters_12") Fail("mmo1: a clean squad id is kept as it is");
    const std::string k1 = SquadKey("a b"), k2 = SquadKey("a_b");
    if (k1 == k2 || k1.compare(0, 12, "pp.squad.a_b") != 0 || k1.size() != 12 + 9) Fail("mmo1: a cleaned id carries its CRC, so two ids never share a file");
    if (!SquadKey("").empty()) Fail("mmo1: an empty id has no key");
    if (SquadKey(std::string(200, 'x')).size() != 9 + kKeyPartMax + 9) Fail("mmo1: a long id is cut and CRC-tagged");
    if (RecFile("pp.squad.x") != "pp.squad.x.rec" || PrevFile("pp.squad.x") != "pp.squad.x.rec.prev"
        || TmpFile("pp.squad.x", 42) != "pp.squad.x.rec.42.tmp" || std::string(IndexFile()) != "own.index")
        Fail("mmo1: file names (one file per squad, its temp, its kept previous version, the index)");

    IndexLine l; l.key = k1; l.seq = 12345678901ULL; l.len = 6183; l.crc = 0xDEADBEEFu; l.writtenAt = 1790000000LL; l.chars = 5;
    std::string s; IndexLine r;
    if (!EncodeIndexLine(l, &s) || s[s.size() - 1] != '\n' || !ParseIndexLine(s, &r) || r.key != l.key || r.seq != l.seq
        || r.len != l.len || r.crc != l.crc || r.writtenAt != l.writtenAt || r.chars != l.chars)
        Fail("mmo1: index line round trip");
    if (!ParseIndexLine(s.substr(0, s.size() - 1) + "\r\n", &r) || r.crc != 0xDEADBEEFu) Fail("mmo1: a CRLF index line parses");
    IndexLine bad = l; bad.key = "a\tb";
    const std::string before = s;
    if (EncodeIndexLine(bad, &s) || s != before) Fail("mmo1: a key with a TAB is refused and nothing is written");
    if (ParseIndexLine("o1\tk\t1\t2\t0000000g\t3\t4", &r) || ParseIndexLine("o2\tk\t1\t2\t00000000\t3\t4", &r)
        || ParseIndexLine("o1\tk\t1\t2\t00000000\t3", &r) || ParseIndexLine("o1\tk\t-1\t2\t00000000\t3\t4", &r)
        || ParseIndexLine("o1\t\t1\t2\t00000000\t3\t4", &r))
        Fail("mmo1: malformed index lines are refused");
    std::vector<std::string> lines; int badN = 0;
    lines.push_back("o1\ta\t7\t1\t00000000\t1\t1"); lines.push_back("junk");
    lines.push_back("o1\tb\t19\t1\t00000000\t1\t1\r"); lines.push_back("");
    if (MaxSeqOf(lines, &badN) != 19 || badN != 1) Fail("mmo1: the sequence counter is re-derived from the index (bad lines counted)");

    if (WriteDecision(false, false, 0, 0, 1, 1) != kWrite || WriteDecision(false, true, 5, 10, 5, 10) != kSkipSame
        || WriteDecision(false, true, 5, 10, 6, 10) != kWrite || WriteDecision(false, true, 5, 10, 5, 11) != kWrite
        || WriteDecision(true, true, 5, 10, 5, 10) != kWrite)
        Fail("mmo1: the write decision (same bytes skipped; poke always writes)");
    if (CommitVerdict(false, false, 0, 0, 10, 5) != kCommitWriteFail || CommitVerdict(true, false, 0, 0, 10, 5) != kCommitVerifyFail
        || CommitVerdict(true, true, 9, 5, 10, 5) != kCommitVerifyFail || CommitVerdict(true, true, 10, 4, 10, 5) != kCommitVerifyFail
        || CommitVerdict(true, true, 10, 5, 10, 5) != kCommitOk)
        Fail("mmo1: the commit verdict (the index line only after a matching read-back)");
    if (!RotateToPrev(true, true, 10, 5, 10, 5) || RotateToPrev(false, true, 10, 5, 10, 5) || RotateToPrev(true, false, 10, 5, 10, 5)
        || RotateToPrev(true, true, 9, 5, 10, 5) || RotateToPrev(true, true, 10, 4, 10, 5))
        Fail("mmo1b: .rec.prev is rotated only from the record the index names");

    /* pacing: over one period of 1 s calls, a ring of n gets EXACTLY n turns (n = 1..200, no cap), so each squad is
       visited once per 30 s - and with the 16/s cap no call exceeds 16. */
    unsigned int n;
    for (n = 1; n <= 200; ++n)
    {
        unsigned long long credit = 0; unsigned int turns = 0, sec;
        for (sec = 0; sec < kPeriodMs / 1000; ++sec) turns += CursorTurns(n, kPeriodMs, 1000, &credit, 0);
        if (turns != n) { Fail("mmo1: the cursor pays out exactly n turns per period"); break; }
    }
    {
        unsigned long long credit = 0; unsigned int sec, worst = 0;
        for (sec = 0; sec < 60; ++sec) { const unsigned int q = CursorTurns(1000, kPeriodMs, 1000, &credit, kMaxPerSec); if (q > worst) worst = q; }
        if (worst != kMaxPerSec) Fail("mmo1: the 16-per-second cap bounds one call");
        credit = 0;
        if (CursorTurns(5, kPeriodMs, 3600000, &credit, 0) != 5 || credit != 0) Fail("mmo1: a long stall catches up at most one lap");
        credit = 777;
        if (CursorTurns(0, kPeriodMs, 1000, &credit, 0) != 0 || credit != 0) Fail("mmo1: an empty ring banks nothing");
        credit = 0;
        if (CursorTurns(5, kPeriodMs, 1000, &credit, 0) != 0 || credit != 5000) Fail("mmo1: a fraction of a turn is carried, not rounded up");
    }
    if (!TurnDue(false, 2000, kPeriodMs, kSlowMul, kDirtyGapMs) || TurnDue(false, 1999, kPeriodMs, kSlowMul, kDirtyGapMs)
        || TurnDue(true, 149999, kPeriodMs, kSlowMul, kDirtyGapMs) || !TurnDue(true, 150000, kPeriodMs, kSlowMul, kDirtyGapMs))
        Fail("mmo1: a turn is due after the 2 s gap; a slow squad only every 150 s");
    if (!DirtyDue(true, 0, kDirtyGapMs) || DirtyDue(false, 1999, kDirtyGapMs) || !DirtyDue(false, 2000, kDirtyGapMs))
        Fail("mmo1: dirty waits 2 s, dirtyNow does not");
    if (!PerSecondAllows(15, kMaxPerSec) || PerSecondAllows(16, kMaxPerSec) || !PerSecondAllows(1000, 0)) Fail("mmo1: 16 per second");
    if (!IsSlow(4001, kSlowUs) || IsSlow(4000, kSlowUs)) Fail("mmo1: slow means over 4 ms");
    if (BytesPerSec(6183, 0) != 0 || BytesPerSec(60000, 30000) != 2000) Fail("mmo1: bytes per second");
    CostStat c; c.Add(5); c.Add(1); c.Add(9);
    if (c.mn != 1 || c.mx != 9 || c.Avg() != 5 || c.n != 3) Fail("mmo1: min/avg/max");
}

/* tickwait: EnsureDir makes each folder level below the root, shortest first (bench 2: the mirror under a missing store folder) */
void t_tickwait_dir_levels()
{
    std::vector<std::string> v;
    coopdir::DirLevels("C:\\a\\coop-store\\coop\\mirror-12", &v);
    if (v.size() != 4 || v[0] != "C:\\a" || v[1] != "C:\\a\\coop-store" || v[2] != "C:\\a\\coop-store\\coop"
        || v[3] != "C:\\a\\coop-store\\coop\\mirror-12") Fail("tickwait: drive path levels");
    coopdir::DirLevels("C:\\a\\\\b\\\\", &v);
    if (v.size() != 2 || v[0] != "C:\\a" || v[1] != "C:\\a\\\\b") Fail("tickwait: doubled and trailing separators");
    coopdir::DirLevels("C:/x/y", &v);
    if (v.size() != 2 || v[0] != "C:/x" || v[1] != "C:/x/y") Fail("tickwait: forward slashes");
    coopdir::DirLevels("C:\\", &v);
    if (!v.empty()) Fail("tickwait: a drive root is never a level");
    coopdir::DirLevels("C:", &v);
    if (!v.empty()) Fail("tickwait: a bare drive is never a level");
    coopdir::DirLevels("", &v);
    if (!v.empty()) Fail("tickwait: empty path");
    coopdir::DirLevels("\\\\srv\\share\\s\\m", &v);
    if (v.size() != 2 || v[0] != "\\\\srv\\share\\s" || v[1] != "\\\\srv\\share\\s\\m") Fail("tickwait: UNC root is server+share");
    coopdir::DirLevels("\\\\srv\\share", &v);
    if (!v.empty()) Fail("tickwait: a UNC share alone is the root");
    coopdir::DirLevels("\\\\?\\C:\\d", &v);
    if (v.size() != 1 || v[0] != "\\\\?\\C:\\d") Fail("tickwait: long-path prefix");
    coopdir::DirLevels("\\top\\sub", &v);
    if (v.size() != 2 || v[0] != "\\top" || v[1] != "\\top\\sub") Fail("tickwait: current-drive root");
    coopdir::DirLevels("rel\\dir", &v);
    if (v.size() != 2 || v[0] != "rel" || v[1] != "rel\\dir") Fail("tickwait: relative path");
}

/* T-1020 (ported from pull request 4, task 4): a save's folder - every missing level made, shortest first, before the file is
   written (a joining PC's world folder); a level that cannot be made fails the save, counted once and said once. */
namespace {
struct T1020Disk
{
    std::vector<std::string> there, refuse, made, files, unaskable;   /* files: a level that is a FILE; unaskable: a level whose query fails */
    mutable std::vector<std::string> asked;
    static bool Has(const std::vector<std::string>& v, const std::string& d) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == d) return true; return false; }
    bool Exists(const std::string& d) const { asked.push_back(d); return Has(there, d) && !Has(unaskable, d); }   /* a FILE is not in `there`: no folder */
    bool Make(const std::string& d) { if (Has(refuse, d) || Has(files, d) || Has(unaskable, d)) return false; there.push_back(d); made.push_back(d); return true; }
};
}
void t_t1020_save_folder_every_level()
{
    T1020Disk fs;
    fs.there.push_back("C:\\Kenshi"); fs.there.push_back("C:\\Kenshi\\save");
    long long failed = 0; int said = 0; std::string bad;
    const std::string f = "C:\\Kenshi\\save\\SW\\worlds\\World One\\build_handoffs_owed_ab12.txt";
    if (coopdir::EnsureFileDir(f, fs, &failed, &said, &bad) != coopdir::kDirReady) Fail("t1020: a nested missing folder is made");
    if (fs.made.size() != 3 || fs.made[0] != "C:\\Kenshi\\save\\SW" || fs.made[1] != "C:\\Kenshi\\save\\SW\\worlds"
        || fs.made[2] != "C:\\Kenshi\\save\\SW\\worlds\\World One") Fail("t1020: every missing level, shortest first, never the file");
    if (failed != 0 || said != 0 || !bad.empty()) Fail("t1020: a made folder counts no failure");
    fs.made.clear();
    if (coopdir::EnsureFileDir(f, fs, &failed, &said, &bad) != coopdir::kDirReady || !fs.made.empty()) Fail("t1020: a folder already there is left alone");

    T1020Disk g;
    g.refuse.push_back("D:\\x\\y");
    const std::string h = "D:\\x\\y\\z\\f.txt";
    if (coopdir::EnsureFileDir(h, g, &failed, &said, &bad) != coopdir::kDirFailSay) Fail("t1020: the first failure is said");
    if (failed != 1 || said != 1 || bad != "D:\\x\\y") Fail("t1020: a failure is counted once and names its level");
    if (g.made.size() != 1 || g.made[0] != "D:\\x") Fail("t1020: nothing below a failed level is tried");
    if (coopdir::EnsureFileDir(h, g, &failed, &said, &bad) != coopdir::kDirFailQuiet) Fail("t1020: a second failure is not said again");
    if (failed != 2) Fail("t1020: every failed save still counts, once each");

    T1020Disk u;
    if (coopdir::EnsureFileDir("\\\\nas\\games\\w\\f.txt", u, &failed, &said, &bad) != coopdir::kDirReady
        || u.made.size() != 1 || u.made[0] != "\\\\nas\\games\\w") Fail("t1020: a share's server and share are never made");
    T1020Disk r;
    if (coopdir::EnsureFileDir("C:\\f.txt", r, &failed, &said, &bad) != coopdir::kDirReady || coopdir::EnsureFileDir("f.txt", r, &failed, &said, &bad) != coopdir::kDirReady
        || !r.made.empty() || failed != 2) Fail("t1020: a drive root or a bare name needs no folder");

    /* T-1020 fold (review LOW 1): the deepest level first - a folder already there costs one check and nothing else */
    fs.asked.clear(); fs.made.clear();
    if (coopdir::EnsureFileDir(f, fs, &failed, &said, &bad) != coopdir::kDirReady || fs.asked.size() != 1
        || fs.asked[0] != "C:\\Kenshi\\save\\SW\\worlds\\World One" || !fs.made.empty()) Fail("t1020 fold: a folder there is asked once, the deepest level");
    T1020Disk m;   /* a middle level that cannot be queried never refuses a folder that is there */
    m.there.push_back("E:\\a"); m.there.push_back("E:\\a\\b"); m.there.push_back("E:\\a\\b\\c"); m.unaskable.push_back("E:\\a\\b");
    if (coopdir::EnsureFileDir("E:\\a\\b\\c\\f.txt", m, &failed, &said, &bad) != coopdir::kDirReady || !m.made.empty() || failed != 2)
        Fail("t1020 fold: an unqueryable middle level does not refuse a folder that is there");
    T1020Disk s;   /* forward slashes: the same levels, the same order */
    s.there.push_back("C:/K");
    if (coopdir::EnsureFileDir("C:/K/save/w/f.txt", s, &failed, &said, &bad) != coopdir::kDirReady || s.made.size() != 2
        || s.made[0] != "C:/K/save" || s.made[1] != "C:/K/save/w" || s.asked.size() != 4 || s.asked[0] != "C:/K/save/w")
        Fail("t1020 fold: forward slashes - deepest asked first, then every missing level shortest first");
    T1020Disk fl;   /* review LOW 2: a level that is a FILE is no folder - the save fails, counted once, named, said by its own *said */
    fl.there.push_back("F:\\w"); fl.files.push_back("F:\\w\\x");
    int said2 = 0; std::string bad2;
    if (coopdir::EnsureFileDir("F:\\w\\x\\y\\f.txt", fl, &failed, &said2, &bad2) != coopdir::kDirFailSay || failed != 3 || said2 != 1
        || bad2 != "F:\\w\\x" || !fl.made.empty()) Fail("t1020 fold: a level that is a file fails the save, counted once and named");
}

void t_orphan1_orphan_purge()
{
    using namespace coopor;
    int why = -1;
    /* (1) the leaf gate has no player-count input: a game alone in the world creates only where a fresh map names it, and with
       no fresh map nobody creates - the answers a game with company gets */
    if (LeafLinked(1) != kLeafLinkedAllow) Fail("leaf gate: a fresh map naming this game allows (alone too)");
    if (LeafLinked(0) != kLeafLinkedNotHere) Fail("leaf gate: a fresh map not naming this game refuses");
    if (LeafLinked(-1) != kLeafLinkedNoMap) Fail("leaf gate: no fresh map refuses (alone too - it holds back until the world server is back)");
    /* (2) cleanup: frozen / no live map / unclaimed / holder - no player-count input, so a host alone and a host with others decide alike */
    int area;
    for (area = -2; area <= 1; ++area)
    {
        Seen g = SeenNone(); g.areaOther = area;
        const int want = (area == 1) ? kActDestroy : kActNone;
        if (Decide(g, &why) != want) Fail("orphan1: removed only in an area another game holds");
        g.guarded = 1;
        if (Decide(g, &why) != ((area == 1) ? kActSkip : kActNone)) Fail("orphan1: a player-tied character is never acted on");
    }
    Seen s = SeenNone();
    if (Decide(s, &why) != kActDestroy || why != kWhyNone) Fail("orphan1: a free orphan is destroyed");
    s.known = 1; if (Decide(s, &why) != kActNone) Fail("orphan1: a registered character is someone's announcement - kept");
    s = SeenNone(); s.standIn = 1; if (Decide(s, &why) != kActNone) Fail("orphan1: a stand-in is the stand-in purge's");
    /* skip reasons, and the player-tie tests win over every action */
    s = SeenNone(); s.guarded = 1; s.inSomething = 2; s.fightNear = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyPlayerTied) Fail("orphan1: player-tied first");
    s = SeenNone(); s.beingCarried = 1; s.carrierFound = 1; s.carrierIsPlayer = 1; s.fightNear = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarriedByPlayer) Fail("orphan1: carried by a player character - skipped, never dropped");
    s = SeenNone(); s.inSomething = 2;
    if (Decide(s, &why) != kActSkip || why != kWhyCaged) Fail("orphan1: caged - skipped, never let out (possibly a player's prisoner)");
    s.fightNear = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCaged) Fail("orphan1: caged wins over the fight test");
    s = SeenNone(); s.fightNear = 1;
    if (Decide(s, &why) != kActLater || why != kWhyInCombatWithPlayer) Fail("orphan1: a fight with a player nearby - a later pass");
    s = SeenNone(); s.inSomething = -1;
    if (Decide(s, &why) != kActSkip || why != kWhyPoseUnreadable) Fail("orphan1: unreadable pose skipped");
    s = SeenNone(); s.beingCarried = 1; s.carrierFound = 1;
    if (Decide(s, &why) != kActDropCarrier) Fail("orphan1: a non-player carrier puts it down first");
    s.carrierTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarryStuck) Fail("orphan1: still carried after the drop - skipped");
    s = SeenNone(); s.beingCarried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarrierUnknown) Fail("orphan1: carried by nobody found - skipped");
    s = SeenNone(); s.inSomething = 1;
    if (Decide(s, &why) != kActBedOut) Fail("orphan1: out of bed first");
    s.bedTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyBedStuck) Fail("orphan1: still in bed - skipped");
    s = SeenNone(); s.inSomething = 3;
    if (Decide(s, &why) != kActSkip || why != kWhyInUnknown) Fail("orphan1: an unknown pose state is skipped");
    s = SeenNone(); s.carrying = 1;
    if (Decide(s, &why) != kActDropOwn) Fail("orphan1: puts down what it carries first");
    s.ownDropTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyOwnCarryStuck) Fail("orphan1: still carrying - skipped");
    /* review-orphan1: the adjacency skip and the waiting period - both before ANY action */
    s = SeenNone(); s.nearPlayer = 1;
    if (Decide(s, &why) != kActLater || why != kWhyNearPlayer) Fail("orphan1 fold: a player character within 100 units - a later pass");
    s = SeenNone(); s.nearPlayer = 1; s.dupOfLiveCopy = 1;
    if (Decide(s, &why) != kActDestroy) Fail("T-304: a duplicate of a live copy is removed although a player is near");
    s = SeenNone(); s.nearPlayer = 1; s.dupOfLiveCopy = 1; s.guarded = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyPlayerTied) Fail("T-304: a player-tied duplicate is still never touched");
    s = SeenNone(); s.nearPlayer = 1; s.dupOfLiveCopy = 1; s.fightNear = 1;
    if (Decide(s, &why) != kActLater || why != kWhyInCombatWithPlayer) Fail("T-304: a duplicate in a fight with a player waits");
    s = SeenNone(); s.nearPlayer = 1; s.dupOfLiveCopy = 1; s.notedMs = kNotedMinMs - 1;
    if (Decide(s, &why) != kActLater || why != kWhyNotedTooShort) Fail("T-304: a duplicate still waits for the noted clock");
    s = SeenNone(); s.dupOfLiveCopy = 1; s.known = 1;
    if (Decide(s, &why) != kActNone) Fail("T-304: a registered character is never a duplicate orphan");
    if (HoldWhilePending(kActLater, kWhyLinkTooFresh) != 1 || HoldWhilePending(kActLater, kWhyNotedTooShort) != 1
        || HoldWhilePending(kActLater, kWhyInCombatWithPlayer) != 0 || HoldWhilePending(kActLater, kWhyNearPlayer) != 0
        || HoldWhilePending(kActSkip, kWhyPlayerTied) != 0 || HoldWhilePending(kActDestroy, kWhyNone) != 0 || HoldWhilePending(kActNone, kWhyNone) != 0)
        Fail("T-304 fold 1: held only while the link or the noted clock is too fresh - never mid-fight, never a plain near-player wait");
    s = SeenNone(); s.nearPlayer = 1; s.dupOfLiveCopy = 1; s.notedMs = 0;
    { const int a = Decide(s, &why); if (HoldWhilePending(a, why) != 1) Fail("T-304 fold 1: a duplicate near a player, noted too briefly, is held"); }
    s = SeenNone(); s.nearPlayer = 1; s.notedMs = 0;
    { const int a = Decide(s, &why); if (HoldWhilePending(a, why) != 0) Fail("T-304 fold 1: a plain near-player wait is not held"); }
    s = SeenNone(); s.fightNear = 1; s.notedMs = 0;
    { const int a = Decide(s, &why); if (HoldWhilePending(a, why) != 0) Fail("T-304 fold 1: a character in a fight with a player is never held"); }
    if (DupNear(60.0f, 80.0f) != 1 || DupNear(60.0f, 81.0f) != 0 || DupNear(-8.0f, 0.0f) != 1) Fail("T-304 fold 1: the duplicate radius is 100 units, planar");
    s = SeenNone(); s.linkedMs = kLinkedMinMs - 1;
    if (Decide(s, &why) != kActLater || why != kWhyLinkTooFresh) Fail("orphan1 fold: the link up less than 10 s - a later pass");
    s = SeenNone(); s.notedMs = kNotedMinMs - 1;
    if (Decide(s, &why) != kActLater || why != kWhyNotedTooShort) Fail("orphan1 fold: noted less than 5 s - a later pass");
    s = SeenNone(); s.notedMs = kNotedMinMs; s.linkedMs = kLinkedMinMs;
    if (Decide(s, &why) != kActDestroy) Fail("orphan1 fold: 5 s noted and 10 s linked - removed");
    s = SeenNone(); s.notedMs = 0; s.inSomething = 1;
    if (Decide(s, &why) != kActLater || why != kWhyNotedTooShort) Fail("orphan1 fold: no bed-out before the wait is over");
    s = SeenNone(); s.notedMs = 0; s.beingCarried = 1; s.carrierFound = 1;
    if (Decide(s, &why) != kActLater || why != kWhyNotedTooShort) Fail("orphan1 fold: no carrier drop before the wait is over");
    s = SeenNone(); s.guarded = 1; s.notedMs = 0; s.linkedMs = 0;
    if (Decide(s, &why) != kActSkip || why != kWhyPlayerTied) Fail("orphan1 fold: player-tied still reported first");
    s = SeenNone(); s.areaOther = 0; s.linkedMs = 0;
    if (Decide(s, &why) != kActNone) Fail("orphan1 fold: an area nobody else holds is left alone, not waiting");
    if (NearPlayer(99.0f, 0.0f, 100.0f) != 1 || NearPlayer(0.0f, 101.0f, 100.0f) != 0) Fail("orphan1 fold: the 100-unit adjacency radius");
    if (std::string(WhyToken(kWhyNearPlayer)) != "nearPlayer" || std::string(WhyToken(kWhyNotedTooShort)) != "notedTooShort"
        || std::string(WhyToken(kWhyLinkTooFresh)) != "linkTooFresh") Fail("orphan1 fold: report tokens");
    /* the fight test and the pass budget */
    if (FightNear(100.0f, 100.0f, 300.0f, 1, 0) != 1 || FightNear(0.0f, 0.0f, 300.0f, 0, 1) != 1) Fail("orphan1: either side in combat, within the radius");
    if (FightNear(10.0f, 10.0f, 300.0f, 0, 0) != 0) Fail("orphan1: nobody in combat - no fight");
    if (FightNear(301.0f, 0.0f, 300.0f, 1, 1) != 0) Fail("orphan1: outside the radius - no fight");
    if (PassAllows(3) != 1 || PassAllows(4) != 0) Fail("orphan1: at most 4 removals per judging pass");
    if (std::string(WhyToken(kWhyInCombatWithPlayer)) != "inCombatWithPlayer" || std::string(WhyToken(kWhyCaged)) != "caged") Fail("orphan1: report tokens");
}

void t_inv7a_standin_purge()
{
    using namespace coopsp;
    int why = -1;
    if (IsStandInRecord(0) != 1 || IsStandInRecord(7) != 1 || IsStandInRecord(-2) != 1) Fail("inv7a: coop-p<n> and the legacy coop-peer are stand-ins");
    if (IsStandInRecord(-1) != 0 || IsStandInRecord(-3) != 0) Fail("inv7a: another faction / an unreadable id is not a stand-in");
    Seen s = SeenNone();
    if (Decide(s, &why) != kActNone) Fail("inv7a: a non-stand-in is left alone");
    s.recordSlot = 2;
    if (Decide(s, &why) != kActDestroy || why != kWhyNone) Fail("inv7a: a free unknown stand-in is destroyed");
    s.known = 1;
    if (Decide(s, &why) != kActNone) Fail("inv7a: a stand-in a mirror row knows (a live copy) is kept");
    s = SeenNone(); s.recordSlot = -2; s.guarded = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyGuarded) Fail("inv7a: H030 - the watched player / a player-faction character is never destroyed");
    s = SeenNone(); s.recordSlot = 1; s.inSomething = -1;
    if (Decide(s, &why) != kActSkip || why != kWhyPoseUnreadable) Fail("inv7a: an unreadable +0x2F8 is skipped");
    s = SeenNone(); s.recordSlot = 1; s.inSomething = 2;
    if (Decide(s, &why) != kActCageOut) Fail("inv7a: a caged stand-in leaves its cage first");
    s.cageQueued = 1;
    if (Decide(s, &why) != kActWaitCage) Fail("inv7a: a queued cage-out is waited for, not queued twice");
    s.cageTimedOut = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCageTimeout) Fail("inv7a: a cage-out that never took is skipped at the limit");
    s = SeenNone(); s.recordSlot = 1; s.inSomething = 2; s.cageAddr = 0;
    if (Decide(s, &why) != kActSkip || why != kWhyCageNoAddr) Fail("inv7a: no setPrisonMode address - a caged one is skipped, never destroyed in its cage");
    s = SeenNone(); s.recordSlot = 1; s.inSomething = 0;
    if (Decide(s, &why) != kActDestroy) Fail("inv7a: after the cage-out took, it is destroyed");
    s = SeenNone(); s.recordSlot = 3; s.beingCarried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarrierUnknown) Fail("inv7a: carried with no carrier found is skipped");
    s.carrierFound = 1;
    if (Decide(s, &why) != kActDropCarrier) Fail("inv7a: a carried stand-in is put down by its carrier first");
    s.carrierTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyCarryStuck) Fail("inv7a: still carried after the drop is skipped");
    s.beingCarried = 0;
    if (Decide(s, &why) != kActDestroy) Fail("inv7a: put down, it is destroyed");
    s = SeenNone(); s.recordSlot = 3; s.inSomething = 1;
    if (Decide(s, &why) != kActBedOut) Fail("inv7a: a stand-in in a bed gets up first");
    s.bedTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyBedStuck) Fail("inv7a: still in its bed after setBedMode(false) is skipped");
    s = SeenNone(); s.recordSlot = 3; s.inSomething = 5;
    if (Decide(s, &why) != kActSkip || why != kWhyInUnknown) Fail("inv7a: an unknown +0x2F8 state is skipped");
    s = SeenNone(); s.recordSlot = 3; s.carrying = 1;
    if (Decide(s, &why) != kActDropOwn) Fail("inv7a: a stand-in carrying something puts it down first");
    s.ownDropTried = 1;
    if (Decide(s, &why) != kActSkip || why != kWhyOwnCarryStuck) Fail("inv7a: still carrying after the drop is skipped");
    s = SeenNone(); s.recordSlot = 3; s.inSomething = 2; s.beingCarried = 1;
    if (Decide(s, &why) != kActCageOut) Fail("inv7a: the cage comes first (a caged body is not carried out of it)");
    if (Due(4, 3, 0) != 1 || Due(4, 4, 0) != 0 || Due(4, 3, 1) != 0 || Due(0, -1, 0) != 1) Fail("inv7a: once per world generation, only while engine writes are allowed");
    if (HoldDrain(1, 0, 5000) != 1 || HoldDrain(1, 4999, 5000) != 1 || HoldDrain(1, 5000, 5000) != 0 || HoldDrain(0, 0, 5000) != 0)
        Fail("inv7a: the queue is held only while a cage-out waits, and never past the limit");
    for (int w = 0; w <= 10; ++w) { const char* x = WhyText(w); if (x == 0 || x[0] == 0) Fail("inv7a: every skip reason has a sentence"); }
}

void t_inv7a2_purge_triggers_and_id_carry()
{
    using namespace coopsp;
    if (PassDue(0, 0) != 0 || PassDue(1, 0) != 1 || PassDue(0, kTrigWake) != 1 || PassDue(0, kTrigLinkUp) != 1) Fail("inv7a2: a pass starts on the load or on an event, never on nothing");
    if (std::string(TriggerText(kTrigLoad)) != "load") Fail("inv7a2: the load pass keeps its 'standin purge at load:' line");
    if (std::string(TriggerText(kTrigWake | kTrigLinkUp)) != "wake+link-up") Fail("inv7a2: combined triggers are named");
    for (int m = 0; m < 8; ++m) { const char* x = TriggerText(m); if (x == 0 || x[0] == 0) Fail("inv7a2: every trigger mask has a name"); }
    if (CarryIdDecide(1, 1, 0) != kCarrySkipEmpty) Fail("inv7e: no id, nothing to carry");
    if (CarryIdDecide(0, 1, 1) != kCarrySkipClash || CarryIdDecide(0, 0, 1) != kCarrySkipClash) Fail("inv7e: a clash is never carried");
    if (CarryIdDecide(0, 0, 0) != kCarrySkipNoState) Fail("inv7e: no state record, no carry");
    if (CarryIdDecide(0, 1, 0) != kCarryYes) Fail("inv7e: an id, a state record and no clash is carried");
}

struct T707Held   /* a fixed table of held ids, the shape spawn.cpp's CopyIdHolders answers in */
{
    std::map<std::string, int> m;
    int operator()(const std::string& id) const { std::map<std::string, int>::const_iterator it = m.find(id); return it == m.end() ? copysq::kHeldNone : it->second; }
};
void t_t707_copy_squad_id()
{
    using namespace copysq;
    if (IdDecide(0, kHeldNone) != kIdKeep) Fail("t707: an engine id no other squad holds is kept");
    if (IdDecide(0, kHeldOwn) != kIdRenew || IdDecide(0, kHeldOther) != kIdRenew) Fail("t707: an engine id another squad holds (own or other) is renewed");
    if (IdDecide(1, kHeldNone) != kIdUnread || IdDecide(1, kHeldOwn) != kIdUnread) Fail("t707: nothing read, nothing decided");
    if (EngineCanMint("Nameless_0") != 1 || EngineCanMint("Sem Nome_0") != 1 || EngineCanMint("Holy Nation Outlaws_65543") != 1 || EngineCanMint("A_B_12") != 1)
        Fail("t707: the engine's own '<faction>_<n>' ids are recognised");
    if (EngineCanMint("Nameless") != 0 || EngineCanMint("Nameless_") != 0 || EngineCanMint("_x") != 0 || EngineCanMint("") != 0 || EngineCanMint("Nameless_0-c1") != 0)
        Fail("t707: an id whose tail after the last '_' is not all digits is never the engine's");
    if (FreshCandidate("Sem Nome_0", 1) != "Sem Nome_0-c1" || FreshCandidate("Nameless_0", 12) != "Nameless_0-c12" || FreshCandidate("Sans-Nom_65536", 7) != "Sans-Nom_65536-c7")
        Fail("t707: a fresh candidate is the engine's id, '-c' and k");
    if (FreshCandidate("a<b>:c\"d/e\\f|g?h*_0", 3) != "a-b--c-d-e-f-g-h-_0-c3" || FreshCandidate(std::string("x\x01y_0"), 1) != "x-y_0-c1")
        Fail("t707: every character a Windows file name refuses becomes '-'");
    for (unsigned k = 1; k <= kFreshTries; ++k)
    {
        const std::string c = FreshCandidate("Nameless_0", k);
        if (EngineCanMint(c) != 0) Fail("t707: no fresh candidate is ever one the engine's counter hands out");
        for (size_t i = 0; i < c.size(); ++i) if (FileNameBad(c[i]) != 0) Fail("t707: a fresh candidate is a legal file name");
        const char last = c[c.size() - 1]; if (last < '0' || last > '9') Fail("t707: a fresh candidate ends in a digit (never a space or a dot)");
    }
    T707Held h; unsigned tries = 99;
    if (FreshId("Nameless_0", h, &tries) != "Nameless_0-c1" || tries != 1) Fail("t707: the first free candidate is taken");
    h.m["Nameless_0-c1"] = kHeldOther; h.m["Nameless_0-c2"] = kHeldOwn;
    if (FreshId("Nameless_0", h, &tries) != "Nameless_0-c3" || tries != 3) Fail("t707: a fresh candidate another squad holds is passed over");
    T707Held full;
    for (unsigned k = 1; k <= kFreshTries; ++k) full.m[FreshCandidate("X_0", k)] = (k % 2) ? kHeldOther : kHeldOwn;
    if (!FreshId("X_0", full, &tries).empty() || tries != kFreshTries) Fail("t707: every candidate held -> no fresh id, after kFreshTries");
    if (std::string(HeldText(kHeldOwn)) != "own" || std::string(HeldText(kHeldOther)) != "other") Fail("t707: the holder is named own / other");
    const std::string pre = FreshPrefix("Nameless_0");
    if (pre != "Nameless_0-c" || FreshPrefix("a:b_0") != "a-b_0-c") Fail("t707: the fresh prefix is the file-name-safe engine id and '-c'");
    if (IdConcerns("Nameless_0", "Nameless_0", pre) != 1 || IdConcerns("Nameless_0-c1", "Nameless_0", pre) != 1 || IdConcerns("Nameless_0-c64", "Nameless_0", pre) != 1)
        Fail("t707: the walk keeps the engine id and its fresh candidates");
    if (IdConcerns("Nameless_1", "Nameless_0", pre) != 0 || IdConcerns("Nameless_0-c", "Nameless_0", pre) != 0 || IdConcerns("Nameless_00", "Nameless_0", pre) != 0
        || IdConcerns("Alpha_0", "Nameless_0", pre) != 0 || IdConcerns("", "", FreshPrefix("")) != 0 || IdConcerns("-c1", "", FreshPrefix("")) != 0)
        Fail("t707: the walk leaves every other id out");
    for (unsigned k = 1; k <= kFreshTries; ++k)
        if (IdConcerns(FreshCandidate("a:b_0", k), "a:b_0", FreshPrefix("a:b_0")) != 1) Fail("t707: the walk keeps every fresh candidate of an id a file name refuses");
    if (ContentFileStem("platoon/Nameless_0.platoon") != "Nameless_0" || ContentFileStem("Nameless_0") != "Nameless_0" || ContentFileStem("a\\b/Nameless_0.platoon") != "Nameless_0"
        || ContentFileStem("a/b\\Nameless_0") != "Nameless_0" || !ContentFileStem("platoon/").empty())
        Fail("t707: a content file names the file-name part after the last slash, less '.platoon'");
    std::string cf;
    if (ContentFileRenamed("platoon/Nameless_0.platoon", "Nameless_0", "Nameless_0-c1", &cf) != 1 || cf != "platoon/Nameless_0-c1.platoon")
        Fail("t707: a content file naming the engine id's file is renamed, folder and ending kept");
    if (ContentFileRenamed("Nameless_0", "Nameless_0", "Nameless_0-c2", &cf) != 1 || cf != "Nameless_0-c2") Fail("t707: a bare content file is renamed");
    if (ContentFileRenamed("platoon\\Nameless_0.platoon", "Nameless_0", "Nameless_0-c1", &cf) != 1 || cf != "platoon\\Nameless_0-c1.platoon") Fail("t707: a backslash folder is kept");
    if (ContentFileRenamed("Nameless_0.platoon", "Nameless_0", "Nameless_0-c1", &cf) != 1 || cf != "Nameless_0-c1.platoon") Fail("t707: a bare file name with its ending is renamed");
    if (ContentFileStem(".platoon") != ".platoon") Fail("t707: a bare ending is not taken for an empty id");
    cf = "x";
    if (ContentFileRenamed("", "Nameless_0", "Nameless_0-c1", &cf) != 0 || ContentFileRenamed("platoon/", "Nameless_0", "Nameless_0-c1", &cf) != 0
        || ContentFileRenamed("platoon/Nameless_01.platoon", "Nameless_0", "Nameless_0-c1", &cf) != 0 || ContentFileRenamed("platoon/Alpha_0.platoon", "Nameless_0", "Nameless_0-c1", &cf) != 0
        || ContentFileRenamed("platoon/Nameless_0.platoon", "", "x-c1", &cf) != 0 || ContentFileRenamed("platoon/Nameless_0.platoon", "Nameless_0", "", &cf) != 0 || cf != "x")
        Fail("t707: a content file naming another file, or nothing, is left alone");
}

void t_inv7a3_own_id_supersede_and_woken_recheck()
{
    using namespace coopsp;
    if (SupersedeRefuseOwn(1, 1) != 1 || SupersedeRefuseOwn(0, 0) != 1) Fail("inv7a3: an id this game minted is never superseded");
    if (SupersedeRefuseOwn(0, 1) != 0 || SupersedeRefuseOwn(2, 1) != 0) Fail("inv7a3: another game's id is superseded as before");
    if (SupersedeRefuseOwn(-1, 1) != 0 || SupersedeRefuseOwn(1, -1) != 0 || SupersedeRefuseOwn(-1, -1) != 0) Fail("inv7a3: no block answer / no relay slot = the old rule");
    if (WokenRecheck(0, 5, 0, kWokenCapMs) != kWokenGone || WokenRecheck(0, 0, 9999, kWokenCapMs) != kWokenGone) Fail("inv7a3: a platoon no longer awake is dropped");
    if (WokenRecheck(1, 0, 100, kWokenCapMs) != kWokenWait || WokenRecheck(1, -1, 100, kWokenCapMs) != kWokenWait) Fail("inv7a3: no members yet (or unreadable) waits");
    if (WokenRecheck(1, 3, 100, kWokenCapMs) != kWokenPurge || WokenRecheck(1, 2, 9000, kWokenCapMs) != kWokenPurge) Fail("inv7a3: members exist -> a pass");
    if (WokenRecheck(1, 0, kWokenCapMs, kWokenCapMs) != kWokenGaveUp || WokenRecheck(1, -1, kWokenCapMs + 1, kWokenCapMs) != kWokenGaveUp) Fail("inv7a3: still empty at the cap -> gave up");
    for (int m = 0; m < 8; ++m) if ((m & kTrigLoad) != 0 && std::string(PassPrefix(m)) != "load") Fail("inv7a3: every load pass keeps 'standin purge at load:'");
    if (std::string(PassPrefix(kTrigWake)) != "wake" || std::string(PassPrefix(kTrigLinkUp)) != "link-up" || std::string(PassPrefix(kTrigWake | kTrigLinkUp)) != "wake+link-up") Fail("inv7a3: a pass without the load is named by its triggers");
}

void t_inv7e2_reloaded_copy_retire()
{
    using namespace coopsp;
    if (ReloadMemberClass(1, 5, 5, 1) != kMemberAdoptedThisGen) Fail("inv7e2: my sweep adoption of this link generation");
    if (ReloadMemberClass(1, 2, 5, 1) != kMemberEarlierGen || ReloadMemberClass(1, 6, 5, 1) != kMemberEarlierGen) Fail("inv7e2: another generation is 'earlier', never this one");
    if (ReloadMemberClass(0, 5, 5, 1) != kMemberOther || ReloadMemberClass(0, -1, 5, 1) != kMemberOther) Fail("inv7e2: not a sweep adoption (SPAWN from the peer / XFER) is other");
    if (ReloadMemberClass(1, 5, 5, 0) != kMemberOther || ReloadMemberClass(0, -1, 5, 0) != kMemberOther) Fail("inv7e2: a uid not mine (a puppet / handed off) is other");
    /* fold (review-inv7e2 HIGH): (readable, slotKnown, reloadedSinceDown, thisWorldLoad, earlierWorldLoad, other) */
    if (ReloadRetireDecide(1, 1, 1, 19, 0, 0) != kReloadRetire || ReloadRetireDecide(1, 1, 1, 1, 0, 0) != kReloadRetire) Fail("inv7e2: reloaded after the link-down, slot known, all members this world load's pickups -> retire");
    if (ReloadRetireDecide(1, 1, 0, 19, 0, 0) != kReloadKeepNotReloaded) Fail("inv7e2 fold: a game that did not reload since its last link-down keeps its squad (the counted duplicate)");
    if (ReloadRetireDecide(1, 0, 1, 19, 0, 0) != kReloadKeepNoSlot || ReloadRetireDecide(1, 0, 0, 19, 0, 0) != kReloadKeepNoSlot) Fail("inv7e2 fold: an unknown slot never retires (fail closed)");
    if (ReloadRetireDecide(1, 1, 1, 3, 1, 0) != kReloadKeepEarlierGen || ReloadRetireDecide(1, 1, 1, 0, 2, 0) != kReloadKeepEarlierGen || ReloadRetireDecide(1, 1, 1, 3, 1, 1) != kReloadKeepEarlierGen) Fail("inv7e2: any earlier-world-load pickup keeps the squad");
    if (ReloadRetireDecide(1, 1, 1, 3, 0, 1) != kReloadKeepSession || ReloadRetireDecide(1, 1, 1, 0, 0, 4) != kReloadKeepSession) Fail("inv7e2: any peer SPAWN / XFER member keeps the squad");
    if (ReloadRetireDecide(0, 1, 1, 5, 0, 0) != kReloadUnreadable || ReloadRetireDecide(0, 0, 0, 0, 0, 0) != kReloadUnreadable) Fail("inv7e2: unreadable members never retire");
    if (ReloadRetireDecide(1, 1, 1, 0, 0, 0) != kReloadNoSession) Fail("inv7e2: no session member read -> no reload verdict (never a retire)");
    for (int t = 0; t < 3; ++t) for (int e = 0; e < 3; ++e) for (int o = 0; o < 3; ++o)
        for (int r = 0; r < 2; ++r) for (int k = 0; k < 2; ++k) for (int d = 0; d < 2; ++d)
            if ((ReloadRetireDecide(r, k, d, t, e, o) == kReloadRetire) != (r == 1 && k == 1 && d == 1 && t > 0 && e == 0 && o == 0)) Fail("inv7e2: retire exactly when readable, slot known, reloaded since the link-down, some this-load pickups and nothing else");
    if (ReloadedSinceLinkDown(2, 1) != 1) Fail("inv7e2 fold: B left at world load 1, loaded (2), rejoined -> reloaded");
    if (ReloadedSinceLinkDown(1, 1) != 0) Fail("inv7e2 fold: A ran continuously through the peer's absence -> not reloaded");
    if (ReloadedSinceLinkDown(1, -1) != 0 || ReloadedSinceLinkDown(5, -1) != 0) Fail("inv7e2 fold: a first session (no link down yet) never counts as a reload");
}

/* restore1b1 (design s2 Q1, s4 first bullet): the OWN_HIGH refusal and its book and wire, the checkpoint tag rule, the prune
   selection, the repair copy's files and the WORLD_SAVED broadcast word. */
void t_restore1b1_ownhigh_checkpoints()
{
    using namespace restoreguard;
    /* THE REFUSAL: records below the notebook's number (an old records folder put back) are refused; no store / no number fail open */
    if (OwnHighDecide(false, 5ULL, 9ULL) != kOhAllowNoStore) Fail("restore1b1: no records store chosen -> allowNoStore");
    if (OwnHighDecide(true, 5ULL, 0ULL) != kOhAllowNoNumber) Fail("restore1b1: the notebook has no number -> fail open");
    if (OwnHighDecide(true, 5ULL, 9ULL) != kOhRefuseOlderRecords) Fail("restore1b1: records up to 5 under the notebook's 9 -> refused");
    if (OwnHighDecide(true, 0ULL, 9ULL) != kOhRefuseOlderRecords) Fail("restore1b1: an emptied records folder under a numbered player -> refused");
    if (OwnHighDecide(true, 9ULL, 9ULL) != kOhAllow || OwnHighDecide(true, 12ULL, 9ULL) != kOhAllow) Fail("restore1b1: equal or above -> allowed");
    if (std::string(OwnHighRefusalWords()) != "This game's records are older than the world remembers for you - an old records folder was put back") Fail("restore1b1: the refusal's words (T-716: the fact only)");
    {   /* ui1 (ui-polish-audit 3.2): the player's words carry no developer words; the log keeps the old ones */
        const std::string pw = OwnHighRefusalPlayerWords();
        if (pw.find("records") != std::string::npos || pw.find("folder") != std::string::npos || pw.find("notebook") != std::string::npos)
            Fail("ui1: a player's refusal words carry developer words");
        if (pw != "Your save is older than the host's world" || std::string(OwnHighRefusalHostWords()) != "Your save is older than the world")
            Fail("T-716: the older-progress refusal states the fact only");
        if (std::string(OwnHighRefusalHostWords()).find("host") != std::string::npos) Fail("words1b: the host's own refusal names no host");
    }
    /* THE BOOK: the maximum per (profile, store), never back; an old folder's report leaves the number, and the answer refuses it */
    OwnBook b;
    if (!MergeOwnHigh(&b, "abcd.2", "cooptest-a", 9ULL)) Fail("restore1b1: a first number is kept");
    if (MergeOwnHigh(&b, "abcd.2", "cooptest-a", 5ULL) || OwnHighOf(b, "abcd.2", "cooptest-a") != 9ULL) Fail("restore1b1: the book never steps back");
    if (OwnHighDecide(true, 5ULL, OwnHighOf(b, "abcd.2", "cooptest-a")) != kOhRefuseOlderRecords) Fail("restore1b1: the answer to an old folder's report refuses it");
    if (MergeOwnHigh(&b, "abcd.2", "cooptest-a", 0ULL) || MergeOwnHigh(&b, "ab cd", "cooptest-a", 3ULL) || MergeOwnHigh(&b, "abcd.2", "bad name", 3ULL)
        || MergeOwnHigh(&b, "abcd.2", "", 3ULL) || MergeOwnHigh(&b, "", "cooptest-a", 3ULL)) Fail("restore1b1: 0, a bad profile or a bad store is no number");
    MergeOwnHigh(&b, "abcd.2", "cooptest-b", 4ULL); MergeOwnHigh(&b, "abcd", "cooptest-a", 7ULL);
    if (OwnHighOf(b, "abcd", "cooptest-a") != 7ULL || OwnHighOf(b, "abcd.2", "cooptest-b") != 4ULL || OwnHighOf(b, "abcd.3", "cooptest-a") != 0ULL) Fail("restore1b1: rows are per profile and store");
    if (OwnRowsOf(b, "abcd.2").size() != 2 || OwnRowsOf(b, "abcd").size() != 1 || OwnRowsOf(b, "abc").size() != 0) Fail("restore1b1: the WELCOME push rows of one profile");
    {
        OwnBook back; int bad = 0;
        OwnBookParse(OwnBookFormat(b) + "junk line\noh1\tabcd.2\tcooptest-a\t3\noh1\tabcd.2\tcooptest-a\t0\n", &back, &bad);
        if (bad != 2 || back.high.size() != 3 || OwnHighOf(back, "abcd.2", "cooptest-a") != 9ULL || OwnHighOf(back, "abcd", "cooptest-a") != 7ULL) Fail("restore1b1: own_high.txt round trip; bad lines skipped; a repeat keeps the max");
    }
    /* THE WIRE */
    {
        OwnHighMsg m; m.kind = kOhUp; m.high = 0x100000005ULL; m.store = "cooptest-r1b";
        std::vector<char> w; EncodeOwnHigh(&w, m);
        OwnHighMsg d;
        if (w.size() != kOhWireMin + 12 || !DecodeOwnHigh(&w[0], w.size(), &d) || d.kind != kOhUp || d.high != m.high || d.store != m.store) Fail("restore1b1: OWN_HIGH round trip");
        if (DecodeOwnHigh(&w[0], w.size() - 1, &d) || DecodeOwnHigh(&w[0], 15, &d) || DecodeOwnHigh(0, 0, &d)) Fail("restore1b1: a truncated OWN_HIGH is refused");
        std::vector<char> x = w; x.push_back(0);
        if (DecodeOwnHigh(&x[0], x.size(), &d)) Fail("restore1b1: a trailing byte is refused");
        OwnHighMsg k = m; k.kind = 7; std::vector<char> y; EncodeOwnHigh(&y, k);
        if (DecodeOwnHigh(&y[0], y.size(), &d)) Fail("restore1b1: an unknown kind is refused");
        k = m; k.store = "a/b"; std::vector<char> z; EncodeOwnHigh(&z, k);
        if (DecodeOwnHigh(&z[0], z.size(), &d)) Fail("restore1b1: a store name that cannot name a row is refused");
    }
    /* THE CHECKPOINT TAG RULE */
    if (CheckpointTagWorld(3, 120ULL, "abcd.2") != "g3-s120-abcd.2" || CheckpointTagLeave(77ULL, 12ULL) != "l-s77-o12") Fail("restore1b1: the two tag spellings");
    if (!CheckpointTagWorld(0, 1ULL, "p").empty() || !CheckpointTagWorld(1, 1ULL, "a b").empty() || !CheckpointTagWorld(1, 1ULL, "").empty()
        || !CheckpointTagWorld(1, 1ULL, "a\\b").empty() || !CheckpointTagWorld(1, 1ULL, "a:b").empty()) Fail("restore1b1: gen 0 or an unnameable profile makes no tag");
    {
        CheckpointTag t;
        if (!ParseCheckpointTag("g3-s120-abcd.2", &t) || t.kind != kCpWorld || t.gen != 3 || t.seq != 120ULL || t.profile != "abcd.2") Fail("restore1b1: a world tag parses");
        if (!ParseCheckpointTag("g3-s120-a-b", &t) || t.profile != "a-b") Fail("restore1b1: a profile with a dash");
        if (!ParseCheckpointTag("l-s77-o12", &t) || t.kind != kCpLeave || t.seq != 77ULL || t.own != 12ULL) Fail("restore1b1: a leave tag parses");
        const char* const badTags[] = { "g03-s1-p", "g0-s1-p", "g3-s120-abcd.2.part", "l-s77-o12.part", "g3-s-p", "l-s7", "junk", "", "g3s1-p", "l-s07-o1", "g3-s1-", "l-s1-o" };
        for (size_t i = 0; i < sizeof(badTags) / sizeof(badTags[0]); ++i)
            if (ParseCheckpointTag(badTags[i], &t)) { Fail("restore1b1: not a tag"); std::printf("   (%s)\n", badTags[i]); }
    }
    /* THE PRUNE SELECTION: the newest 10 world tags per profile and the newest 10 leave tags stay; nothing else is touched */
    {
        std::vector<std::string> names;
        for (unsigned int g = 1; g <= 12; ++g) names.push_back(CheckpointTagWorld(g, 10ULL * g, "P.1"));
        for (unsigned int g = 1; g <= 3; ++g) names.push_back(CheckpointTagWorld(g, 5ULL * g, "Q.1"));
        for (unsigned int q = 1; q <= 11; ++q) names.push_back(CheckpointTagLeave(100ULL + q, 1ULL));
        names.push_back("g13-s130-P.1.part"); names.push_back("junk");
        const std::vector<std::string> out = PruneSelect(names, kCheckpointCap);
        const std::set<std::string> o(out.begin(), out.end());
        if (out.size() != 3 || !o.count("g1-s10-P.1") || !o.count("g2-s20-P.1") || !o.count("l-s101-o1")) Fail("restore1b1: the prune takes the oldest beyond the cap per group");
        if (!PruneSelect(names, 100).empty()) Fail("restore1b1: under the cap nothing is pruned");
        std::vector<std::string> gens; gens.push_back("g2-s5-P.1"); gens.push_back("g10-s1-P.1");
        const std::vector<std::string> g1 = PruneSelect(gens, 1);
        if (g1.size() != 1 || g1[0] != "g2-s5-P.1") Fail("restore1b1: the prune orders by number, not by text");
    }
    /* THE REPAIR COPY'S FILES AND THE BROADCAST WORD */
    {
        const std::vector<std::string> rf = RepairCopyFiles();
        const std::set<std::string> r(rf.begin(), rf.end());
        if (!r.count("world.gen") || !r.count("clock.txt") || !r.count("uniques.txt") || !r.count("options.txt") || !r.count("research_boxes.txt") || !r.count("town_bars.txt")) Fail("restore1b1: the repair copy's files");
        WorldMsg wm; wm.gen = 4; wm.seq = 55ULL; wm.numbered = 1; wm.profile = "abcd.2";
        std::vector<char> plain, bc; EncodeWorldMsg(&plain, wm); EncodeWorldBroadcast(&bc, wm);
        if (IsWorldBroadcast(&plain[0], plain.size()) || !IsWorldBroadcast(&bc[0], bc.size())) Fail("restore1b1: only the broadcast carries the word");
        WorldMsg wd;
        if (!DecodeWorldMsg(&bc[0], bc.size(), &wd) || wd.gen != 4 || wd.seq != 55ULL || wd.profile != "abcd.2") Fail("restore1b1: a broadcast still decodes as a WORLD_SAVED");
        std::vector<char> odd = bc; odd[odd.size() - 4] = 2;
        if (IsWorldBroadcast(&odd[0], odd.size())) Fail("restore1b1: another trailing word is not the broadcast");
    }
}

/* restore1b1 fold (review-restore1b items 1, 5, 6): a refusal re-judged at each WELCOME, leave checkpoints ranked by the own
   number, the world's part of the checkpoint path, and the Recycle Bin check before any prune. */
void t_restore1b1_fold()
{
    using namespace restoreguard;
    /* item 1: refused in world X (records 5 under 9); the next WELCOME (world Y, no number) re-judges and allows; a WELCOME whose
       number still refuses blocks again; an unblocked game is not flagged for a re-judge */
    int blocked = 0, rejudge = 0;
    if (!OwnHighLoadRefused(blocked, true, 5ULL, 9ULL)) Fail("restore1b1 fold: records under the number refuse the load");
    blocked = 1;
    if (!OwnHighLoadRefused(blocked, true, 5ULL, 0ULL)) Fail("restore1b1 fold: blocked on this link refuses even with no number");
    OwnHighOnWelcomeReset(&blocked, &rejudge);
    if (blocked != 0 || rejudge != 1) Fail("restore1b1 fold: a WELCOME lifts the block and asks for a re-judge");
    if (OwnHighLoadRefused(blocked, true, 5ULL, 0ULL)) Fail("restore1b1 fold: another world with no number for the records -> the load is allowed");
    if (!OwnHighLoadRefused(blocked, true, 5ULL, 9ULL)) Fail("restore1b1 fold: the same old folder under the same number is refused again");
    { int b2 = 0, r2 = 0; OwnHighOnWelcomeReset(&b2, &r2); if (b2 != 0 || r2 != 0) Fail("restore1b1 fold: an unrefused game is not re-judged"); }
    if (OwnHighLoadRefused(0, false, 0ULL, 9ULL)) Fail("restore1b1 fold: no records store chosen never refuses");
    /* item 5: leave checkpoints by the OWN number first */
    {
        std::vector<std::string> n; n.push_back(CheckpointTagLeave(200ULL, 1ULL)); n.push_back(CheckpointTagLeave(100ULL, 5ULL)); n.push_back(CheckpointTagLeave(150ULL, 5ULL));
        const std::vector<std::string> out = PruneSelect(n, 2);
        if (out.size() != 1 || out[0] != "l-s200-o1") Fail("restore1b1 fold: the leave with the lowest own number goes first, whatever its notebook number");
        const std::vector<std::string> out1 = PruneSelect(n, 1);
        const std::set<std::string> o1(out1.begin(), out1.end());
        if (out1.size() != 2 || !o1.count("l-s200-o1") || !o1.count("l-s100-o5")) Fail("restore1b1 fold: equal own numbers fall back to the notebook number");
    }
    if (!CheckpointWorldOk("default") || !CheckpointWorldOk("my world.2") || !CheckpointWorldOk("w-1_b")) Fail("restore1b1 fold: plain world names name a folder");
    if (CheckpointWorldOk("") || CheckpointWorldOk(".") || CheckpointWorldOk("..") || CheckpointWorldOk("a\\b") || CheckpointWorldOk("a/b")
        || CheckpointWorldOk("a:b") || CheckpointWorldOk("w.") || CheckpointWorldOk("w ") || CheckpointWorldOk(std::string(65, 'a'))) Fail("restore1b1 fold: a world name that cannot be one folder is refused");
    /* item 6: the bin check */
    const unsigned long long drive = 500ULL << 30;   /* 500 GiB: 1% = 5 GiB, capped at 1 GiB */
    if (!RecycleBinCanTake(true, true, 2ULL << 20, drive)) Fail("restore1b1 fold: a 2 MB folder on a fixed drive with a bin is recycled");
    if (RecycleBinCanTake(false, true, 1ULL, drive) || RecycleBinCanTake(true, false, 1ULL, drive) || RecycleBinCanTake(true, true, 1ULL, 0ULL)) Fail("restore1b1 fold: no fixed drive / no bin / unknown drive -> no recycle");
    if (RecycleBinCanTake(true, true, (1ULL << 30) + 1ULL, drive) || !RecycleBinCanTake(true, true, 1ULL << 30, drive)) Fail("restore1b1 fold: never above 1 GiB");
    if (RecycleBinCanTake(true, true, 11ULL << 20, 1ULL << 30) || !RecycleBinCanTake(true, true, 10ULL << 20, 1ULL << 30)) Fail("restore1b1 fold: within 1% of a small drive");
}

/* restore1b1 fold 2 (recheck-restore1b items 1, 3, 4): the volume GUID and the bin policy, the leave's checkpoint hold, the
   re-judge's pending hold. */
void t_restore1b1_fold2()
{
    using namespace restoreguard;
    if (VolumeGuidOf("\\\\?\\Volume{0a1b2c3d-1111-2222-3333-444455556666}\\") != "{0a1b2c3d-1111-2222-3333-444455556666}") Fail("restore1b1 fold 2: the volume GUID");
    if (!VolumeGuidOf("").empty() || !VolumeGuidOf("\\\\?\\Volume{short}\\").empty() || !VolumeGuidOf("{0a1b2c3d-1111-2222-3333-444455556666").empty()) Fail("restore1b1 fold 2: no GUID -> empty");
    if (!BinPolicyAllows(true, 0, 100, 2ULL << 20)) Fail("restore1b1 fold 2: a 2 MB folder under a 100 MB bin that recycles");
    if (BinPolicyAllows(true, 1, 100, 1ULL)) Fail("restore1b1 fold 2: a bin set to delete immediately -> never");
    if (BinPolicyAllows(false, 0, 100, 1ULL)) Fail("restore1b1 fold 2: an unreadable policy -> never");
    if (BinPolicyAllows(true, 0, 1, (1ULL << 20) + 1ULL) || !BinPolicyAllows(true, 0, 1, 1ULL << 20)) Fail("restore1b1 fold 2: never above MaxCapacity");
    if (LeaveCheckpointHoldDone(false, 4999u, 5000u) || !LeaveCheckpointHoldDone(false, 5000u, 5000u) || !LeaveCheckpointHoldDone(true, 0u, 5000u)) Fail("restore1b1 fold 2: the leave waits until settled or 5 s");
    if (!OwnHighRejudgePending(1, false, 100u, 10000u)) Fail("restore1b1 fold 2: a re-judge with no row yet is pending");
    if (OwnHighRejudgePending(1, true, 100u, 10000u) || OwnHighRejudgePending(1, false, 10000u, 10000u) || OwnHighRejudgePending(0, false, 0u, 10000u)) Fail("restore1b1 fold 2: a row, 10 s, or no re-judge ends it");
    if (!OwnHighLoadHeld(0, true, true, 9ULL, 0ULL)) Fail("restore1b1 fold 2: a pending re-judge holds the load (not fail-open)");
    if (OwnHighLoadHeld(0, false, true, 9ULL, 0ULL) || !OwnHighLoadHeld(0, false, true, 5ULL, 9ULL) || !OwnHighLoadHeld(1, false, true, 9ULL, 0ULL)) Fail("restore1b1 fold 2: otherwise the refusal rule");
}

/* restore1a (design s1, s5; fold review-restore1a): the world guard's decisions - every verdict branch, the fail-open case,
   joiner-never-refused, PER-PROFILE numbers, the key-less refusal, the adopt-higher rule, request-order stamps, the live
   notebook-behind test - and its key-file fields 5-7, the world.gen book, the WORLD_SAVED wire and the WELCOME tail. */
void t_restore1a_world_guard()
{
    using namespace restoreguard;
    WorldState nb; nb.known = true; nb.gen = 2; nb.numbered = true; nb.seqHigh = 100; nb.epoch = 0;
    SaveStamp older; older.hasGen = true; older.gen = 1; older.seq = 40;
    SaveStamp latest; latest.hasGen = true; latest.gen = 2; latest.seq = 90;
    SaveStamp ahead; ahead.hasGen = true; ahead.gen = 3; ahead.seq = 100;
    SaveStamp none;
    if (Decide(true, latest, nb) != kAllow) Fail("restore1a: the latest world save loads");
    if (Decide(true, ahead, nb) != kAllow) Fail("restore1a: a save numbered past an unanswered WORLD_SAVED (gen ahead, seq inside) loads");
    if (Decide(true, older, nb) != kRefuseOlderWorld) Fail("restore1a: an older world save is refused");
    if (Decide(true, none, nb) != kRefuseUnnumbered) Fail("restore1a: a key file with no field 5 in a numbered world is refused (ruling 2)");
    SaveStamp beyond = latest; beyond.seq = 101;
    if (Decide(true, beyond, nb) != kRefuseNotebookBehind) Fail("restore1a: nbSeq past the notebook's high-water = the notebook is older");
    SaveStamp ep = latest; ep.epoch = 1;
    if (Decide(true, ep, nb) != kRefuseNotebookBehind) Fail("restore1a: a later repair epoch = the notebook is older");
    SaveStamp both = older; both.seq = 500;
    if (Decide(true, both, nb) != kRefuseOlderWorld) Fail("restore1a: older-world is judged first");
    WorldState fresh; fresh.known = true;
    if (Decide(true, none, fresh) != kAllowNoGen) Fail("restore1a: FAIL OPEN - no field 5 and no numbered profile loads (noGen)");
    WorldState freshIdx = fresh; freshIdx.seqHigh = 50;
    if (Decide(true, none, freshIdx) != kAllowNoGen) Fail("restore1a: not numbered - no field 5 loads whatever the index holds");
    SaveStamp s1 = latest; s1.gen = 1; s1.seq = 60;
    if (Decide(true, s1, freshIdx) != kRefuseNotebookBehind) Fail("restore1a: a numbered save against an unnumbered notebook with a lower seq = notebook older");
    s1.seq = 50;
    if (Decide(true, s1, freshIdx) != kAllow) Fail("restore1a: a numbered save whose seq the notebook holds loads");
    WorldState unknown;
    if (Decide(true, older, unknown) != kAllowNoTail || DecideKeyless(true, unknown) != kAllowNoTail) Fail("restore1a: no WELCOME tail - fail open");
    /* per profile (owner ruling 1): profile 1 at gen 5, profile 2 at gen 6 - profile 1's gen-5 save still loads */
    WorldBook book;
    MergeBook(&book, "p.1", 5, 10); MergeBook(&book, "p.2", 6, 20);
    const WorldState v1 = ViewOf(RowOf(book, "p.1")), v2 = ViewOf(RowOf(book, "p.2")), v3 = ViewOf(RowOf(book, "p.3"));
    SaveStamp p1s; p1s.hasGen = true; p1s.gen = 5; p1s.seq = 10;
    if (v1.gen != 5 || v2.gen != 6 || !v1.numbered || v1.seqHigh != 20) Fail("restore1a: each profile's row carries its own number and the world's seqHigh");
    if (Decide(true, p1s, v1) != kAllow) Fail("restore1a: profile 1's gen-5 save loads though profile 2 is at 6");
    SaveStamp p1old = p1s; p1old.gen = 4;
    if (Decide(true, p1old, v1) != kRefuseOlderWorld) Fail("restore1a: profile 1's gen-4 save is older than its own 5");
    if (Decide(true, p1s, v2) != kRefuseOlderWorld) Fail("restore1a: against profile 2's number the same save would be older - so numbers must be per profile");
    if (v3.gen != 0 || v3.numbered || Decide(true, none, v3) != kAllowNoGen || DecideKeyless(true, v3) != kAllowNoGen) Fail("restore1a fold 2: 'numbered' is per profile - a profile whose own number is 0 still loads its field-less and key-less saves");
    if (NumberedCount(book) != 2 || MaxGen(book) != 6 || GenOf(book, "p.9") != 0) Fail("restore1a: the book's counts");
    /* key-less (owner ruling 2) */
    if (DecideKeyless(true, nb) != kRefuseKeyless || DecideKeyless(true, v1) != kRefuseKeyless || Decide(true, none, v1) != kRefuseUnnumbered) Fail("restore1a: key-less and field-less saves are refused once their profile is numbered");
    if (DecideKeyless(true, fresh) != kAllowNoGen) Fail("restore1a: a key-less save loads while nothing is numbered");
    /* joiners are never refused */
    const SaveStamp saves[] = { older, latest, ahead, none, beyond, ep, both };
    const WorldState nbs[] = { nb, fresh, freshIdx, unknown, v1, v2, v3 };
    for (int j = 0; j < 7; ++j)
    {
        if (DecideKeyless(false, nbs[j]) != kAllowJoiner) Fail("restore1a: a joiner's key-less save is never refused");
        for (int i = 0; i < 7; ++i)
            if (Decide(false, saves[i], nbs[j]) != kAllowJoiner || IsRefusal(Decide(false, saves[i], nbs[j]))) Fail("restore1a: a joiner is never refused by the world guard");
    }
    if (!IsRefusal(kRefuseOlderWorld) || !IsRefusal(kRefuseNotebookBehind) || !IsRefusal(kRefuseUnnumbered) || !IsRefusal(kRefuseKeyless)
        || IsRefusal(kAllow) || IsRefusal(kAllowJoiner) || IsRefusal(kAllowNoGen) || IsRefusal(kAllowNoTail))
        Fail("restore1a: IsRefusal names exactly the four refusals");
    if (std::string(restoreguard::VerdictName(kRefuseOlderWorld)) != "refuseOlderWorld" || std::string(restoreguard::VerdictName(kRefuseKeyless)) != "refuseKeyless") Fail("restore1a: verdict names");
    /* adopt a higher number (review a) */
    unsigned int pg = 0; unsigned long long ps = 0ULL;
    if (!AdoptHigher(ahead, nb, &pg, &ps) || pg != 3 || ps != 100ULL) Fail("restore1a: a save numbered above the notebook is adopted");
    if (AdoptHigher(latest, nb, &pg, &ps) || AdoptHigher(none, nb, &pg, &ps) || pg != 3) Fail("restore1a: an equal or unnumbered save is not adopted");
    pg = 7;
    if (!AdoptHigher(ahead, nb, &pg, &ps) || pg != 7) Fail("restore1a: a higher pending stamp is kept when adopting");
    /* request-order stamps (review c) */
    unsigned int r = 0;
    const unsigned int first = ReserveGen(&r, 2, 0), second = ReserveGen(&r, 2, 0);
    if (first != 3 || second != 4) Fail("restore1a: two requests get 3 then 4, in request order, whatever order they finish in");
    if (ReserveGen(&r, 9, 0) != 10 || ReserveGen(&r, 0, 12) != 13 || ReserveGen(&r, 0, 0) != 14) Fail("restore1a: a reserved number is above the notebook and the pending stamp and never repeats");
    /* notebook behind, live (review d) */
    if (!NotebookBehindLive(nb, 101, 0) || !NotebookBehindLive(nb, 0, 3) || NotebookBehindLive(nb, 100, 2)) Fail("restore1a: the live notebook-behind test");
    /* fold 2: the notebook-acked number vs this game's own - a lost report is no false alarm, and the re-send happens */
    {
        WorldState row; row.known = true; row.gen = 2; row.numbered = true; row.seqHigh = 100;
        const unsigned int acked = 2, own = 3;   /* the notebook confirmed 2; this game stamped 3 and that report was lost */
        if (NotebookBehindLive(row, 100, acked)) Fail("restore1a fold 2: a reconnect after a lost report is not an older notebook");
        if (!NotebookBehindLive(row, 100, own)) Fail("restore1a fold 2: checking against the game's own number would have raised the false alarm");
        if (!ResendDecide(true, false, own, row.gen)) Fail("restore1a fold 2: a pending number above the notebook's is re-sent");
        if (ResendDecide(true, false, 2, row.gen) || ResendDecide(false, false, own, row.gen) || ResendDecide(true, true, own, row.gen))
            Fail("restore1a fold 2: no re-send when the notebook holds it, for a joiner, or into an older notebook");
    }
    /* fold 2: the pending number recovered from the profile's key files */
    {
        std::vector<SaveStamp> found; SaveStamp a1; a1.hasGen = true; a1.gen = 4; a1.seq = 70; SaveStamp a2 = a1; a2.gen = 6; a2.seq = 90; SaveStamp a3 = a1; a3.gen = 5;
        found.push_back(a1); found.push_back(a2); found.push_back(a3); found.push_back(none);
        unsigned int pg2 = 0; unsigned long long ps2 = 0ULL;
        if (ScanAdopt(found, &pg2, &ps2) != 6 || pg2 != 6 || ps2 != 90ULL) Fail("restore1a fold 2: the highest saved number (and its seq) becomes the pending number");
        pg2 = 8;
        if (ScanAdopt(found, &pg2, &ps2) != 6 || pg2 != 8) Fail("restore1a fold 2: a higher pending number is kept");
        std::vector<SaveStamp> empty; pg2 = 0;
        if (ScanAdopt(empty, &pg2, &ps2) != 0 || pg2 != 0) Fail("restore1a fold 2: no key files, nothing adopted");
        unsigned int rr = 0;
        if (ReserveGen(&rr, 2, 6) != 7) Fail("restore1a fold 2: after the scan the next save takes 7, never a number already on disk");
    }
    /* key-file fields 5-7 */
    std::vector<std::string> f;
    f.push_back("kcworld2"); f.push_back("coop"); f.push_back("s1"); f.push_back("abc.1");
    SaveStamp st; st.hasGen = true;
    if (StampFromFields(f, &st) || st.hasGen) Fail("restore1a: a 4-field key file has no stamp (an older build's file)");
    f.push_back("7"); f.push_back("12345"); f.push_back("0");
    if (!StampFromFields(f, &st) || !st.hasGen || st.gen != 7 || st.seq != 12345ULL || st.epoch != 0) Fail("restore1a: fields 5-7 are read");
    std::vector<std::string> g = f; g[0] = "kcworld1";
    if (StampFromFields(g, &st)) Fail("restore1a: only kcworld2 carries fields 5-7");
    g = f; g[4] = "0";
    if (StampFromFields(g, &st)) Fail("restore1a: worldGen 0 is not a stamp");
    g = f; g[4] = "7x";
    if (StampFromFields(g, &st) || st.hasGen) Fail("restore1a: a non-digit field 5 is no stamp");
    g = f; g[5] = "";
    if (StampFromFields(g, &st)) Fail("restore1a: an empty field 6 is no stamp");
    g = f; g[4] = "4294967296";
    if (StampFromFields(g, &st)) Fail("restore1a: a field 5 past u32 is no stamp");
    g = f; g[5] = "18446744073709551615";
    if (!StampFromFields(g, &st) || st.seq != 18446744073709551615ULL) Fail("restore1a: the largest nbSeq is read");
    g = f; g[5] = "18446744073709551616";
    if (StampFromFields(g, &st)) Fail("restore1a: an nbSeq past u64 is no stamp");
    if (StampTail(7, 12345ULL, 0) != "\t7\t12345\t0") Fail("restore1a: the stamp tail's text");
    {
        const std::vector<std::string> h = SplitTabs(std::string("kcworld2\tcoop\ts1\tabc.1") + StampTail(9, 4000000000ULL, 0));
        if (h.size() != 7 || h[3] != "abc.1" || !StampFromFields(h, &st) || st.gen != 9 || st.seq != 4000000000ULL) Fail("restore1a: a written key line reads back; field 4 is untouched");
    }
    if (U64Text(0ULL) != "0" || U64Text(18446744073709551615ULL) != "18446744073709551615") Fail("restore1a: U64Text");
    if (!ProfileIdOk("abc.1") || ProfileIdOk("") || ProfileIdOk("a b") || ProfileIdOk(std::string(65, 'a'))) Fail("restore1a: ProfileIdOk");
    /* the book keeps the maximum and never steps back; seqHigh only from the notebook's own index */
    WorldBook m;
    if (MergeBook(&m, "a.1", 3, 10) != 3) Fail("restore1a: a first stamp raises both");
    if (MergeBook(&m, "a.1", 2, 5) != 0 || GenOf(m, "a.1") != 3 || m.seqHigh != 10) Fail("restore1a: a lower stamp never steps the book back");
    if (MergeBook(&m, "a.1", 4, 5) != 1 || GenOf(m, "a.1") != 4) Fail("restore1a: a higher gen raises that profile only");
    if (MergeBook(&m, "a.1", 4, 11) != 2 || m.seqHigh != 11) Fail("restore1a: the index high-water raises seqHigh");
    if (MergeBook(&m, "", 9, 11) != 0 || NumberedCount(m) != 1) Fail("restore1a: no profile, no row");
    /* world.gen */
    WorldBook back;
    if (!BookParse(BookFormat(book), &back) || GenOf(back, "p.1") != 5 || GenOf(back, "p.2") != 6 || back.seqHigh != 20 || back.epoch != 0) Fail("restore1a: world.gen round-trips");
    if (!BookParse("wg2\t12\t0\r\np\ta.1\t5\r\n", &back) || GenOf(back, "a.1") != 5 || back.seqHigh != 12) Fail("restore1a: a CRLF world.gen reads");
    if (!BookParse("wg2\t1\t0\n", &back) || NumberedCount(back) != 0) Fail("restore1a: a world.gen with no profile rows reads");
    if (BookParse("wg1\t5\t12\t0", &back) || BookParse("p\ta.1\t5\n", &back) || BookParse("wg2\t12\t0\np\t\t5\n", &back) || BookParse("wg2\t12\t0\np\ta.1\tx\n", &back) || BookParse("", &back))
        Fail("restore1a: a damaged world.gen is refused");
    /* WORLD_SAVED wire */
    WorldMsg w; w.gen = 9; w.seq = 0x100000005ULL; w.epoch = 0; w.numbered = 2; w.profile = "abc.1";
    std::vector<char> b; EncodeWorldMsg(&b, w);
    WorldMsg d;
    if (b.size() != kWireMin + 5 || !DecodeWorldMsg(&b[0], b.size(), &d) || d.gen != 9 || d.seq != 0x100000005ULL || d.epoch != 0 || d.numbered != 2 || d.profile != "abc.1")
        Fail("restore1a: WORLD_SAVED round-trips (a seq above 32 bits, the profile)");
    if (DecodeWorldMsg(&b[0], b.size() - 1, &d) || DecodeWorldMsg(0, 30, &d)) Fail("restore1a: a short WORLD_SAVED is refused");
    { std::vector<char> bad = b; const unsigned int big = 65; std::memcpy(&bad[20], &big, 4); if (DecodeWorldMsg(&bad[0], bad.size(), &d)) Fail("restore1a: a profile length past 64 is refused"); }
    /* the WELCOME tail */
    coopstore::StoreWelcome in; in.protocol = 52; in.authority = 1; in.recordCount = 3; in.slot = 0; in.hasWorld = true; in.world = "coop";
    in.hasMods = true; in.modsVerdict = 1; in.modsActive = 2; in.modsPlugins = 1;
    in.hasWorldGen = true; EncodeWorldMsg(&in.worldTail, w);
    std::vector<char> wb; coopstore::StoreWelcomeEncode(in, &wb);
    coopstore::StoreWelcome out;
    if (!coopstore::StoreWelcomeDecode(wb, &out) || !out.hasMods || out.modsActive != 2 || !out.hasWorldGen || out.world != "coop"
        || !DecodeWorldMsg(&out.worldTail[0], out.worldTail.size(), &d) || d.gen != 9 || d.profile != "abc.1")
        Fail("restore1a: the WELCOME tail round-trips after the mods");
    in.hasWorldGen = false; coopstore::StoreWelcomeEncode(in, &wb);
    if (!coopstore::StoreWelcomeDecode(wb, &out) || !out.hasMods || out.hasWorldGen) Fail("restore1a: a 51-shaped WELCOME has no tail");
}

void t_crash2_copy_body_decide()
{
    using namespace coopbody;
    // CopyBodyDecide(onMain, human, replicated, owned, heldBefore, entity, ragdoll, ragdollOffReady, inPass, fault) - an ANIMAL
    // copy this game does not own (human = 0, owned = 0): the stricter rule, held in ragdoll as well as inside the pass
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 1, 1, 0, 0) != kCopyBodyWait) Fail("T-511: a copy in ragdoll with a body waits");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 0, 1, 1, 0) != kCopyBodyWait) Fail("T-511: a copy's rebuild called from inside the engine's ragdoll pass waits");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 1, 1, 1, 0) != kCopyBodyWait) Fail("T-511: ragdoll and the pass together wait");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 0, 1, 0, 0) != kCopyBodyRun) Fail("T-511: outside the pass and out of ragdoll the rebuild runs at once");
    if (CopyBodyDecide(1, 0, 0, 0, 0, 1, 1, 1, 1, 0) != kCopyBodyRun) Fail("T-511: a character the mod does not replicate (uid 0) never waits");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 0, 1, 1, 1, 0) != kCopyBodyRun) Fail("T-511: no body yet -> nothing to destroy, the build runs");
    if (CopyBodyDecide(1, 0, 0, 0, 0, 0, 0, 1, 0, 1) != kCopyBodyWait || CopyBodyDecide(1, 0, 1, 0, 0, 1, 0, 1, 0, 1) != kCopyBodyWait)
        Fail("T-511: a faulting read waits on the main thread");
    if (CopyBodyDecide(0, 0, 1, 0, 0, 1, 1, 1, 1, 1) != kCopyBodyRun) Fail("T-511: off the main thread it never waits");
    if (CopyBodyDecide(7, 0, 2, 0, 0, 3, 5, 9, 0, 0) != kCopyBodyWait) Fail("T-511: any nonzero input reads as set");
    // this game's own body (owned = 1): held only when its hold carried over (began while it was a copy) or the mod's own look asks
    if (CopyBodyDecide(1, 0, 1, 1, 1, 1, 1, 1, 0, 0) != kCopyBodyWait) Fail("T-661: a copy held in ragdoll that became this game's own still waits");
    if (CopyBodyDecide(1, 0, 1, 1, 1, 1, 0, 1, 1, 0) != kCopyBodyWait) Fail("T-661: a carried-over hold inside the ragdoll pass waits");
    if (CopyBodyDecide(1, 0, 1, 1, 1, 1, 0, 1, 0, 0) != kCopyBodyRun) Fail("T-661: a carried-over hold ends once out of ragdoll and outside the pass");
    if (CopyBodyDecide(1, 0, 1, 1, 0, 1, 1, 1, 1, 0) != kCopyBodyRun) Fail("T-661: the engine's own rebuild of this game's own body is left to the engine");
    if (CopyBodyDecide(1, 0, 1, 1, 0, 1, 1, 1, 1, 1) != kCopyBodyRun) Fail("T-661: this game's own body with no carried-over hold runs even on a read fault");
    if (CopyBodyDecide(0, 0, 1, 1, 1, 1, 1, 1, 1, 0) != kCopyBodyRun) Fail("T-661: off the main thread it never waits");
    // a HUMAN (human = 1) with its animation's has-body byte set (ragdollOffReady = 1) is held only inside the engine's ragdoll
    // pass or on a read fault: its createBody ends the ragdoll before it destroys anything, and a carry waits for a
    // knocked-out copy's queued rebuild to run
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 1, 1, 0, 0) != kCopyBodyRun) Fail("T-1133: a human copy lying in ragdoll outside the pass rebuilds");
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 0, 1, 1, 0) != kCopyBodyWait) Fail("T-1133: a human copy's rebuild inside the engine's ragdoll pass waits");
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 1, 1, 1, 0) != kCopyBodyWait) Fail("T-1133: a human copy in ragdoll inside the pass waits");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 1, 1, 0, 0) != kCopyBodyWait) Fail("T-1133: an animal copy lying in ragdoll waits");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 0, 1, 1, 0) != kCopyBodyWait) Fail("T-1133: an animal copy's rebuild inside the pass waits");
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 0, 1, 0, 1) != kCopyBodyWait) Fail("T-1133: a human copy whose read faults waits");
    if (CopyBodyDecide(1, 1, 1, 1, 0, 1, 1, 1, 1, 0) != kCopyBodyRun) Fail("T-1133: a human body this game owns with no hold carried over runs");
    if (CopyBodyDecide(1, 1, 1, 1, 1, 1, 1, 1, 0, 0) != kCopyBodyRun) Fail("T-1133: a human's carried-over hold ends outside the pass even in ragdoll");
    // T-1133 fold: set-body's ragdoll switch-off (0x5D0320) works only while the animation's has-body byte (+0x88) is set - with
    // it clear (ragdollOffReady = 0) a human lying in ragdoll waits like an animal; an animal does not read the byte
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 1, 0, 0, 0) != kCopyBodyWait) Fail("T-1133 fold: a human copy in ragdoll whose has-body byte reads 0 waits");
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 1, 0, 1, 0) != kCopyBodyWait) Fail("T-1133 fold: ... and waits inside the pass too");
    if (CopyBodyDecide(1, 1, 1, 1, 1, 1, 1, 0, 0, 0) != kCopyBodyWait) Fail("T-1133 fold: a human's carried-over hold goes on in ragdoll while the byte reads 0");
    if (CopyBodyDecide(1, 1, 1, 1, 0, 1, 1, 0, 0, 0) != kCopyBodyRun) Fail("T-1133 fold: this game's own human body with no hold is left to the engine");
    if (CopyBodyDecide(1, 1, 1, 0, 0, 1, 0, 0, 0, 0) != kCopyBodyRun) Fail("T-1133 fold: out of ragdoll a human rebuilds whatever the byte");
    if (CopyBodyDecide(1, 0, 1, 0, 0, 1, 1, 0, 0, 0) != kCopyBodyWait) Fail("T-1133 fold: an animal in ragdoll waits with the byte clear");
    if (CopyBodyDecide(0, 1, 1, 0, 0, 1, 1, 0, 0, 0) != kCopyBodyRun) Fail("T-1133 fold: off the main thread it never waits");
    int seen[2] = { 0, 0 };
    for (int m = 0; m < 2; ++m) for (int u = 0; u < 2; ++u) for (int c = 0; c < 2; ++c) for (int o = 0; o < 2; ++o)
    for (int h = 0; h < 2; ++h) for (int e = 0; e < 2; ++e) for (int r = 0; r < 2; ++r) for (int y = 0; y < 2; ++y)
    for (int p = 0; p < 2; ++p) for (int f = 0; f < 2; ++f)
    {
        const int got = CopyBodyDecide(m, u, c, o, h, e, r, y, p, f);
        const int want = (m == 1 && !(o == 1 && h == 0)
                          && (f == 1 || (c == 1 && e == 1 && (p == 1 || (r == 1 && (u == 0 || y == 0))))))
                       ? kCopyBodyWait : kCopyBodyRun;
        if (got != want) Fail("T-511: the copy body-rebuild rule answers by its table for every input");
        if (o == 0 && got != CopyBodyDecide(m, u, c, 0, 1 - h, e, r, y, p, f)) Fail("T-661: for a copy a carried-over hold changes nothing");
        if (u == 1 && got == kCopyBodyWait && CopyBodyDecide(m, 0, c, o, h, e, r, y, p, f) != kCopyBodyWait)
            Fail("T-1133: an animal waits wherever a human would");
        if ((u == 0 || r == 0) && got != CopyBodyDecide(m, u, c, o, h, e, r, 1 - y, p, f))
            Fail("T-1133 fold: the has-body byte matters only to a human lying in ragdoll");
        if (got == kCopyBodyRun || got == kCopyBodyWait) ++seen[got];
    }
    if (seen[kCopyBodyRun] == 0 || seen[kCopyBodyWait] == 0) Fail("T-511: the coverage reached run and wait");
}

void t_t500_ko_look_gate()
{
    using namespace coopkolook;
    // the named branches
    if (LookGate(0, 0, true, false, false, true, true) != kLookApply)
        Fail("T-500: a standing, alive copy whose owner says KO, first look, knockdown held -> its look applies");
    if (LookGate(0, 1, true, false, false, true, true) != kLookWait)
        Fail("T-500: a lying copy keeps waiting even while its knockdown is held");
    if (LookGate(1, 0, true, false, false, true, true) != kLookNever || LookGate(1, 0, false, false, false, true, false) != kLookNever
        || LookGate(-1, 0, false, true, true, true, false) != kLookNever)
        Fail("T-500: a dead (or unreadable) copy never takes a look");
    if (LookGate(0, 0, true, false, false, true, false) != kLookWait)
        Fail("T-500: owner says KO, knockdown not held -> wait");
    if (LookGate(0, 0, true, false, false, false, true) != kLookWait)
        Fail("T-500: owner says KO, not the first look -> wait");
    if (LookGate(0, -1, true, false, false, true, true) != kLookWait)
        Fail("T-500: an unreadable pose reads as down -> wait");
    if (LookGate(0, 0, false, true, true, true, false) != kLookApply)
        Fail("T-500: a dead arrival still takes its first look alive (unchanged)");
    if (LookGate(0, 0, false, true, false, true, false) != kLookWait)
        Fail("T-500: owner says dead, no first look wanted -> wait (unchanged)");
    if (LookGate(0, 0, false, false, false, false, false) != kLookApply)
        Fail("T-500: an ordinary standing copy takes its look");
    if (LookThroughKoHold(0, 0, false, false, false, true, false) || !LookThroughKoHold(0, 0, true, false, false, true, true)
        || LookThroughKoHold(0, 1, true, false, false, true, true))
        Fail("T-500: only a look let through by the held knockdown is marked as such");
    if (LookGate(0, 0, true, true, true, true, true) != kLookApply || LookThroughKoHold(0, 0, true, true, true, true, true))
        Fail("T-500: a look the owner's 'dead' lets through (dead arrival) is not counted as the knocked-out exception");
    // the hold is live only while present, not given up and inside the one bound
    if (!KnockHoldLive(true, false, 0) || !KnockHoldLive(true, false, kFirstLookWaitMs - 1)
        || KnockHoldLive(true, false, kFirstLookWaitMs) || KnockHoldLive(true, true, 0) || KnockHoldLive(false, false, 0))
        Fail("T-500: the knockdown hold reads live only while held, not given up, inside kFirstLookWaitMs");
    if (kFirstLookWaitMs != 5000ul) Fail("T-500: the first-look bound is 5 s");
    // clothing: the same exception for the copy whose look went through
    if (!ClothingThroughKoHold(0, 0, true, false, true, true)) Fail("T-500: that copy's worn items go through while standing and held");
    if (ClothingThroughKoHold(0, 1, true, false, true, true) || ClothingThroughKoHold(1, 0, true, false, true, true)
        || ClothingThroughKoHold(0, 0, true, false, true, false) || ClothingThroughKoHold(0, 0, true, false, false, true)
        || ClothingThroughKoHold(0, 0, false, false, true, true) || ClothingThroughKoHold(0, 0, true, true, true, true))
        Fail("T-500: the clothing exception needs alive, standing, owner KO (not dead), the look through it, the hold live");
    // clothing that missed it: the look went through, alive, owner KO not dead, and the hold gone or the copy down
    if (!ClothingMissedKoHold(0, 0, true, false, true, false) || !ClothingMissedKoHold(0, 1, true, false, true, true))
        Fail("T-500: worn items that find the hold gone, or the copy down, are counted as missing the exception");
    if (ClothingMissedKoHold(0, 0, true, false, true, true) || ClothingMissedKoHold(0, 0, true, false, false, false)
        || ClothingMissedKoHold(1, 0, true, false, true, false) || ClothingMissedKoHold(0, 0, false, false, true, false)
        || ClothingMissedKoHold(0, 0, true, true, true, false))
        Fail("T-500: a pass, a look not through the exception, a dead copy, an owner awake or dead are not counted as a miss");
    for (int d = 0; d < 2; ++d) for (int w = 0; w < 2; ++w) for (int k = 0; k < 2; ++k) for (int od = 0; od < 2; ++od)
    for (int lt = 0; lt < 2; ++lt) for (int h = 0; h < 2; ++h)
        if (ClothingMissedKoHold(d, w, k != 0, od != 0, lt != 0, h != 0)
            && ClothingThroughKoHold(d, w, k != 0, od != 0, lt != 0, h != 0))
            Fail("T-500: worn items never both pass the exception and miss it");
    // the whole table, against its rule, with coverage of every verdict
    int seen[3] = { 0, 0, 0 };
    for (int d = 0; d < 2; ++d) for (int w = 0; w < 2; ++w) for (int k = 0; k < 2; ++k) for (int od = 0; od < 2; ++od)
    for (int dw = 0; dw < 2; ++dw) for (int f = 0; f < 2; ++f) for (int h = 0; h < 2; ++h)
    {
        const int got = LookGate(d, w, k != 0, od != 0, dw != 0, f != 0, h != 0);
        const int want = d ? kLookNever : w ? kLookWait : (od && !dw) ? kLookWait : (k && !(f && h)) ? kLookWait : kLookApply;
        if (got != want) Fail("T-500: the look gate answers by its table for every input");
        if (got >= 0 && got < 3) ++seen[got];
    }
    if (seen[kLookApply] == 0 || seen[kLookWait] == 0 || seen[kLookNever] == 0) Fail("T-500: the coverage reached apply, wait and never");
}

/* T-293: the human test is the appearance object's vtable pointer against the AppearanceHumanVt row. Numbers from T625's
   [P020] dump (1.0.65, image base 0x7FF7750B0000) and the RTTI vtables of both exes. */
static void T293Jump(unsigned char* b, unsigned long long from, unsigned long long to)
{
    const unsigned int rel = (unsigned int)(to - (from + 5ull));
    b[0] = 0xE9; b[1] = (unsigned char)(rel & 0xFF); b[2] = (unsigned char)((rel >> 8) & 0xFF);
    b[3] = (unsigned char)((rel >> 16) & 0xFF); b[4] = (unsigned char)((rel >> 24) & 0xFF);
}

void t_t293_human_class_by_vtable()
{
    using namespace coophuman;
    const unsigned long long base = 0x7FF7750B0000ull, human65 = 0x16E5208ull, animal65 = 0x16E5468ull, base65 = 0x16E4F08ull;
    if (HumanClassDecide(base + human65, base, human65) != kHumanYes) Fail("t293: T625's logged vtable (AppearanceHuman's) is human");
    if (HumanClassDecide(base + animal65, base, human65) != kHumanNo) Fail("t293: AppearanceAnimal's vtable is not human");
    if (HumanClassDecide(base + base65, base, human65) != kHumanNo) Fail("t293: AppearanceBase's vtable is not human");
    if (HumanClassDecide(base + 0x16E6338ull, base, 0x16E6338ull) != kHumanYes) Fail("t293: 1.0.68's row works the same way");
    if (HumanClassDecide(base + human65, base, 0x16E6338ull) != kHumanNo) Fail("t293: the other version's row does not match");
    if (HumanClassDecide(0, base, human65) != kHumanUnreadable) Fail("t293: an unreadable vtable is 'cannot tell'");
    if (HumanClassDecide(base + human65, base, 0) != kHumanUnreadable) Fail("t293: no row is 'cannot tell', never human");
    if (HumanClassDecide(base + human65, 0, human65) != kHumanUnreadable) Fail("t293: no image base is 'cannot tell'");
    unsigned char b[5];
    T293Jump(b, base + 0x4A3CCull, base + 0x52D530ull);   /* 1.0.65 slot 5 -> setGender */
    if (ThunkTarget(base + 0x4A3CCull, b) != base + 0x52D530ull) Fail("t293: slot 5's stub jumps forward to setGender");
    if (base + 0x4A3CCull == base + 0x52D530ull) Fail("t293: (the old test's miss) the slot value is the stub, not setGender");
    T293Jump(b, base + 0x532E50ull, base + 0x418D5ull);   /* 1.0.65 slot 2's second hop jumps backward */
    if (ThunkTarget(base + 0x532E50ull, b) != base + 0x418D5ull) Fail("t293: a stub jumping backward");
    b[0] = 0x48;
    if (ThunkTarget(base + 0x539440ull, b) != base + 0x539440ull) Fail("t293: a function's own first byte is not a stub");
}

/* P5t (T418): the notebook gives an unclaimed area to the lowest slot whose player is fresh and within ring 1 of it (the
   local tie-break's winner), else the reporter. */
void t_p5t_area_assign_ring1_lowest()
{
    using namespace coopstore;
    int s[3] = { 0, 1, 2 }, x[3] = { 43, 43, 50 }, y[3] = { 11, 11, 20 }; double at[3] = { 100.0, 100.0, 100.0 };
    int n = -1, why = -1;
    if (AreaAssignDecide(1, 43, 11, 0, 0, 0, 0, 0, 100.0, 5.0, &n, &why) != 1 || n != 0 || why != kAssignReporter) Fail("p5t: no player positions -> the reporter");
    if (AreaAssignDecide(1, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0 || n != 2 || why != kAssignRing1) Fail("p5t: T418 - B reports first, A (lower) beside it too -> A");
    if (AreaAssignDecide(0, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0 || why != kAssignRing1) Fail("p5t: A reports, both beside -> A");
    at[0] = 94.9;
    if (AreaAssignDecide(1, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || n != 1) Fail("p5t: A's position 5.1 s old is stale -> B, the only fresh ring-1 player");
    at[0] = 95.0;
    if (AreaAssignDecide(1, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0) Fail("p5t: exactly 5 s old is still fresh (the relay's own > test)");
    at[0] = 100.0; x[0] = 45;
    if (AreaAssignDecide(1, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || n != 1) Fail("p5t: A two areas away is not ring 1 -> B");
    x[0] = 44; y[0] = 12;
    if (AreaAssignDecide(1, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0 || n != 2) Fail("p5t: the diagonal neighbour is ring 1 (Chebyshev 1)");
    x[0] = 43; y[0] = 11; x[1] = 60;
    if (AreaAssignDecide(2, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0 || n != 1 || why != kAssignRing1) Fail("p5t: a third game reports, A beside it -> A");
    at[0] = 0.0;
    if (AreaAssignDecide(2, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 2 || n != 0 || why != kAssignReporter) Fail("p5t: nobody fresh beside it -> the reporter as before");
    s[0] = -1; at[0] = 100.0; x[1] = 43;
    if (AreaAssignDecide(2, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1) Fail("p5t: a negative slot is never chosen");
    x[1] = -1;
    if (AreaAssignDecide(2, 43, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 2) Fail("p5t: an unknown (negative) position is never ring 1");
}

/* area2 fold (review-area2): the sweep's purge request rule, the sweep-only pass, and the unconfirmed area lease. */
void t_area2_fold_sweep_request_and_unconfirmed_lease()
{
    using namespace coopsp;
    if (SweepRequestDecide(0, 0, 0, 0, 0, kSweepReqGapMs) != kSweepReqAsk) Fail("area2: a first-seen copy with no pass yet asks");
    if (SweepRequestDecide(0, 1, 0, 1, 999999, kSweepReqGapMs) != kSweepReqSeen) Fail("area2: a copy already asked for never asks again");
    if (SweepRequestDecide(1, 0, 0, 0, 0, kSweepReqGapMs) != kSweepReqFull || SweepRequestDecide(1, 1, 0, 1, 999999, kSweepReqGapMs) != kSweepReqFull)
        Fail("area2: never after a uid table FULL error");
    if (SweepRequestDecide(0, 0, 1, 1, 999999, kSweepReqGapMs) != kSweepReqWait) Fail("area2: not while a pass runs");
    if (SweepRequestDecide(0, 0, 0, 1, 4999, kSweepReqGapMs) != kSweepReqWait) Fail("area2: not within 5 s of the last pass's END");
    if (SweepRequestDecide(0, 0, 0, 1, 5000, kSweepReqGapMs) != kSweepReqAsk) Fail("area2: 5 s after the last pass ended it asks");
    if (SweepOnlyPass(kTrigSweep) != 1 || SweepOnlyPass(kTrigSweep | kTrigWake) != 0 || SweepOnlyPass(kTrigSweep | kTrigLoad) != 0 || SweepOnlyPass(kTrigLinkUp) != 0)
        Fail("area2: only a pass the sweep alone asked for skips the cage wait");
    if (std::string(TriggerText(kTrigSweep)) != "sweep" || std::string(TriggerText(kTrigWake | kTrigSweep)) != "wake+sweep") Fail("area2: the sweep trigger is named");
    if (std::string(PassPrefix(kTrigLoad | kTrigSweep)) != "load") Fail("area2: a load pass keeps its prefix");
    for (int m = 0; m < 16; ++m) { const char* x = TriggerText(m); if (x == 0 || x[0] == 0) Fail("area2: every trigger mask has a name"); }
    using namespace coopstore;
    if (UnconfirmedLeaseDecide(0, 0, 99.0, kUnconfirmedLeaseSec) != kLeaseNormal) Fail("area2: a confirmed row is on the ordinary rules");
    if (UnconfirmedLeaseDecide(1, 1, 3.0, kUnconfirmedLeaseSec) != kLeaseConfirm) Fail("area2: the assignee's own report confirms");
    if (UnconfirmedLeaseDecide(1, 0, 11.0, kUnconfirmedLeaseSec) != kLeaseHold) Fail("area2: 11 s unconfirmed is held (past the 10 s lease, inside 12 s)");
    if (UnconfirmedLeaseDecide(1, 0, 12.0, kUnconfirmedLeaseSec) != kLeaseHold) Fail("area2: the 12 s boundary itself holds");
    if (UnconfirmedLeaseDecide(1, 0, 12.1, kUnconfirmedLeaseSec) != kLeaseHandOver) Fail("area2: past 12 s unconfirmed it is handed to the reporter");
    if (UnconfirmedLeaseDecide(1, 1, 30.0, kUnconfirmedLeaseSec) != kLeaseConfirm) Fail("area2: a late report by the assignee itself still confirms");
    int s[2] = { 16, 1 }, x[2] = { 43, 50 }, y[2] = { 11, 20 }; double at[2] = { 100.0, 100.0 }; int n = -1, why = -1;
    if (AreaAssignDecide(1, 43, 11, 2, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || n != 0 || why != kAssignReporter) Fail("area2: a slot above 15 is never chosen");
}

/* mmo6: the hand-over ledger line round trip, its refusals, and the cap */
void t_mmo6_ledger()
{
    using namespace coopown;
    LedgerEntry e;
    e.peer = LedgerPeerKey(3); e.msgId = 4000000001u; e.atUnix = 1790000000LL; e.dirIn = 0; e.uid = 77; e.qty = 3; e.itemSid = "2292-gamedata.base";
    std::string line;
    if (LedgerLineFormat(e, &line) == 0) Fail("mmo6: an OUT line formats");
    if (line.compare(0, 9, "q1\t16\tp3\t") != 0 || line[line.size() - 1] != '\n') Fail("mmo6: the q1 shape, family 16, peer p3, newline");
    LedgerEntry r;
    if (LedgerLineParse(line, &r) == 0) Fail("mmo6: the OUT line parses back");
    if (r.peer != "p3" || r.msgId != 4000000001u || r.atUnix != 1790000000LL || r.dirIn != 0 || r.uid != 77 || r.qty != 3
        || r.itemSid != "2292-gamedata.base") Fail("mmo6: every field round-trips (OUT)");
    LedgerEntry in;
    in.peer = LedgerPeerKey(-1); in.msgId = 5; in.atUnix = 0; in.dirIn = 1; in.uid = 9; in.qty = 0; in.itemSid = "";
    if (LedgerLineFormat(in, &line) == 0 || LedgerLineParse(line, &r) == 0) Fail("mmo6: an IN line with no sid round-trips");
    if (r.peer != "p?" || r.dirIn != 1 || r.uid != 9 || r.qty != 0 || !r.itemSid.empty() || r.msgId != 5) Fail("mmo6: IN fields");
    {
        std::string crl = line; crl.insert(crl.size() - 1, "\r");
        if (LedgerLineParse(crl, &r) == 0) Fail("mmo6: a CRLF line still parses");
    }
    LedgerEntry bad = e; bad.peer = "p\t3";
    if (LedgerLineFormat(bad, &line) != 0) Fail("mmo6: a TAB in the peer is refused");
    bad = e; bad.peer = "";
    if (LedgerLineFormat(bad, &line) != 0) Fail("mmo6: an empty peer is refused");
    bad = e; bad.qty = -1;
    if (LedgerLineFormat(bad, &line) != 0) Fail("mmo6: a negative quantity is refused");
    if (LedgerLineParse("q1\t2\tzone\t1\t2\tabcd", &r) != 0) Fail("mmo6: an outage-queue line (family 2) is not a ledger line");
    if (LedgerLineParse("q1\t16\tp1\t1\t2\t6831", &r) != 0) Fail("mmo6: a payload without the h1 direction is refused");
    if (LedgerLineParse("q1\t16\tp1\t1\t2\t683", &r) != 0) Fail("mmo6: odd hex is refused");
    if (LedgerLineParse("q1\t16\tp1\t1\t2", &r) != 0) Fail("mmo6: five fields are refused");
    if (LedgerLineParse("q1\t16\tp1\t99999999999\t2\t68317c696e7c317c327c73", &r) != 0) Fail("mmo6: a message id over 32 bits is refused");
    if (LedgerLineParse("q1\t16\tp1\t7\t2\t68317c696e7c317c327c73", &r) == 0 || r.uid != 1 || r.qty != 2 || r.itemSid != "s" || r.dirIn != 1)
        Fail("mmo6: a hand-written h1|in|1|2|s line parses");
    {
        coopqueue::QueueEntry qe;
        std::string ql;
        e.peer = "p3";
        if (LedgerLineFormat(e, &ql) == 0 || coopqueue::QueueLineParse(ql, &qe) != 0) Fail("mmo6: the outage queue refuses a ledger line");
    }
    std::vector<std::string> lines;
    for (int i = 0; i < 600; ++i)
    {
        LedgerEntry x = e; x.msgId = (unsigned int)i;
        std::string l2;
        LedgerLineFormat(x, &l2);
        l2.erase(l2.size() - 1);
        lines.push_back(l2);
        LedgerKeepLast(&lines, kLedgerCap);
    }
    if (lines.size() != (size_t)kLedgerCap) Fail("mmo6: the cap keeps exactly 512 lines");
    if (LedgerLineParse(lines[0], &r) == 0 || r.msgId != 88) Fail("mmo6: the oldest kept is #88 (600 - 512)");
    if (LedgerLineParse(lines[lines.size() - 1], &r) == 0 || r.msgId != 599) Fail("mmo6: the newest is last");
    {
        std::vector<std::string> small; small.push_back("a"); small.push_back("b");
        if (LedgerKeepLast(&small, 1) != 1 || small.size() != 1 || small[0] != "b") Fail("mmo6: the cap drops the oldest first");
    }
    std::string file = lines[0] + "\n" + "garbage\n\n" + lines[1] + "\r\n";
    std::vector<std::string> good;
    if (LedgerSplit(file, &good) != 1 || good.size() != 2) Fail("mmo6: the file split keeps good lines and counts one bad");
    if (LedgerFile() != "pp.ledger") Fail("mmo6: the file name");
    /* mmo6 fold (review-mmo6): the rev line, the role round trip, unknown lines kept, an unreadable file refusing */
    {
        LedgerEntry rv = e; rv.dirIn = kLedgerRev; rv.role = kLedgerRoleOwner; rv.msgId = 42;
        std::string rl;
        LedgerEntry back;
        if (LedgerLineFormat(rv, &rl) == 0 || LedgerLineParse(rl, &back) == 0) Fail("mmo6 fold: a rev line round-trips");
        if (back.dirIn != kLedgerRev || back.role != kLedgerRoleOwner || back.msgId != 42 || back.uid != e.uid || back.itemSid != e.itemSid)
            Fail("mmo6 fold: rev + owner fields");
        LedgerEntry rq = e; rq.dirIn = kLedgerIn; rq.role = kLedgerRoleRequester;
        if (LedgerLineFormat(rq, &rl) == 0 || LedgerLineParse(rl, &back) == 0 || back.role != kLedgerRoleRequester || back.dirIn != kLedgerIn)
            Fail("mmo6 fold: requester IN round-trips");
        LedgerEntry bd = e; bd.role = 7;
        if (LedgerLineFormat(bd, &rl) != 0) Fail("mmo6 fold: an unknown role is refused at format");
        bd = e; bd.dirIn = 3;
        if (LedgerLineFormat(bd, &rl) != 0) Fail("mmo6 fold: an unknown direction is refused at format");
        /* "h2|in|xyz|1|2|s" - a role this build does not know */
        if (LedgerLineParse("q1\t16\tp1\t7\t2\t68327c696e7c78797a7c317c327c73", &back) != 0) Fail("mmo6 fold: an unknown role is refused at parse");
        /* "h1|in|1|2|s" still parses, role unknown */
        if (LedgerLineParse("q1\t16\tp1\t7\t2\t68317c696e7c317c327c73", &back) == 0 || back.role != kLedgerRoleUnknown) Fail("mmo6 fold: an h1 line has no role");
        std::vector<std::string> kept;
        int unk = 0;
        LedgerSplitKeep(lines[0] + "\n" + "q9\tfuture\tformat\n\n" + lines[1] + "\r\n", &kept, &unk);
        if (kept.size() != 3 || unk != 1 || kept[1] != "q9\tfuture\tformat") Fail("mmo6 fold: a line this build cannot parse is kept as it is");
        if (LedgerLoadVerdict(true, false) != kLedgerLoadEmpty) Fail("mmo6 fold: not found -> empty");
        if (LedgerLoadVerdict(false, true) != kLedgerLoadUse) Fail("mmo6 fold: read -> use");
        if (LedgerLoadVerdict(false, false) != kLedgerLoadRefuse) Fail("mmo6 fold: exists but unreadable -> the write is refused");
    }
}

/* T-251 (H-OVL-COLD): the own overlay's claim window - closes when every entry is done, else at the cap from the first frame */
void t_t251_overlay_window()
{
    using namespace coopown;
    const unsigned long cap = 90000;
    if (OverlayWindowDecide(1, 0, true, 10000, cap) != kOvWinOpen) Fail("t251: T596 life 3 - nothing claimed at +10 s: still open (the old rule closed here)");
    if (OverlayWindowDecide(1, 0, true, 14100, cap) != kOvWinOpen) Fail("t251: nothing claimed at +14.1 s: still open");
    if (OverlayWindowDecide(1, 1, true, 14200, cap) != kOvWinAllClaimed) Fail("t251: every entry claimed with a result: closes at once");
    if (OverlayWindowDecide(2, 1, true, 3000, cap) != kOvWinOpen) Fail("t251: one of two done: open");
    if (OverlayWindowDecide(1, 1, false, 0, cap) != kOvWinAllClaimed) Fail("t251: all done before any frame: closes");
    if (OverlayWindowDecide(0, 0, false, 0, cap) != kOvWinAllClaimed) Fail("t251: nothing published: nothing to wait for");
    if (OverlayWindowDecide(1, 0, false, 200000, cap) != kOvWinOpen) Fail("t251: no first frame: the cap has not started");
    if (OverlayWindowDecide(1, 0, true, 89999, cap) != kOvWinOpen) Fail("t251: just under the cap: open");
    if (OverlayWindowDecide(1, 0, true, 90000, cap) != kOvWinCap) Fail("t251: at the cap: closes (BROKEN is logged)");
    if (OverlayWindowDecide(3, 2, true, 95000, cap) != kOvWinCap) Fail("t251: past the cap with one left: closes");
    if (std::string(OverlayWindowReason(kOvWinAllClaimed)) != "allClaimed" || std::string(OverlayWindowReason(kOvWinCap)) != "cap"
        || std::string(OverlayWindowReason(kOvWinOpen)) != "open")
        Fail("t251: the reason names");
    /* T-251 review fold M1: a world change while the window is open closes it SILENTLY, ahead of every other rule */
    if (OverlayWindowDecide(3, 0, true, 5000, cap, true) != kOvWinWorldChanged) Fail("t251 fold M1: a world change closes the window (world-changed)");
    if (OverlayWindowDecide(3, 0, true, 95000, cap, true) != kOvWinWorldChanged) Fail("t251 fold M1: past the cap too - world-changed, not cap (no BROKEN)");
    if (OverlayWindowDecide(1, 1, true, 5000, cap, true) != kOvWinWorldChanged) Fail("t251 fold M1: world-changed ahead of allClaimed");
    if (OverlayWindowDecide(3, 0, true, 5000, cap, false) != kOvWinOpen) Fail("t251 fold M1: the same world: still open");
    if (std::string(OverlayWindowReason(kOvWinWorldChanged)) != "world-changed") Fail("t251 fold M1: the reason name");
    /* T-251 review fold M2: a settled (turned-down) entry is never claimable and counts as done */
    if (!OverlayEntryClaimable(kOvEntryFree) || OverlayEntryClaimable(kOvEntryClaimed) || OverlayEntryClaimable(kOvEntrySettledNotLoading)
        || OverlayEntryClaimable(kOvEntrySettledNameMismatch))
        Fail("t251 fold M2: only a free entry is claimable - a settled one never again");
    if (!OverlayEntryDone(kOvEntrySettledNotLoading, false) || !OverlayEntryDone(kOvEntrySettledNameMismatch, false))
        Fail("t251 fold M2: a settled entry counts as done");
    if (OverlayEntryDone(kOvEntryFree, false) || OverlayEntryDone(kOvEntryClaimed, false) || !OverlayEntryDone(kOvEntryClaimed, true))
        Fail("t251 fold M2: a claimed entry is done only once its result is logged");
    {
        const long st[] = { kOvEntryClaimed, kOvEntrySettledNameMismatch }; const bool lg[] = { true, false };
        long done = 0;
        for (int i = 0; i < 2; ++i) if (OverlayEntryDone(st[i], lg[i])) ++done;
        if (OverlayWindowDecide(2, done, true, 1000, cap) != kOvWinAllClaimed) Fail("t251 fold M2: one loaded + one settled -> closes at once (no 90 s wait)");
    }
    /* T596 life 3's order: -1 (title), 0 (first frame), 1 (the pause flag rises), -1, 1, 0 (the load pause ends), then play */
    const int seq[] = { -1, 0, 0, 1, -1, 1, 0, 0, 1, 0 };
    int seen = 0, ended = 0, edges = 0, edgeAt = -1;
    for (int i = 0; i < 10; ++i)
        if (OverlayLoadPauseEnded(seq[i], &seen, &ended)) { ++edges; if (edgeAt < 0) edgeAt = i; }
    if (edges != 1 || edgeAt != 6) Fail("t251: the load pause ends at the first 0 after a 1, once (a 0 before any 1 is not the end)");
    seen = 0; ended = 0;
    if (OverlayLoadPauseEnded(0, &seen, &ended) || OverlayLoadPauseEnded(-1, &seen, &ended) || ended != 0 || seen != 0)
        Fail("t251: no pause seen: no end");
}


/* ================================ E47-S2 (P8f re-prepared): THE WORLD-GROUP HEARTBEAT'S CADENCE ==================
   F724: the obvious "30-second heartbeat" (a per-group sinceLast >= 30000 test on a ring whose own cycle is already
   <= 30 s) delivers a FIFTY-second one at fifty groups and no counter says so. Case 5 measures the delivered period. */
std::string UU(unsigned int v) { char b[32]; std::sprintf(b, "%u", v); return b; }

/* (1) the quota takes elapsed milliseconds, not a count of ticks */
void t_hb_cursor_quota_takes_elapsed_ms_not_ticks()
{
    if (coophb::CursorQuota(50, 30000, 1000, 0) != 2)
        Fail("N=50, 30 s period, a 1000 ms tick must be 2 groups; got " + UU(coophb::CursorQuota(50, 30000, 1000, 0)));
    if (coophb::CursorQuota(50, 30000, 2000, 0) != 4)
        Fail("A 2000 ms TICK MUST DOUBLE THE QUOTA: want 4, got " + UU(coophb::CursorQuota(50, 30000, 2000, 0)));
    if (coophb::CursorQuota(20, 30000, 1000, 0) != 1)
        Fail("N=20 must round UP to 1, got " + UU(coophb::CursorQuota(20, 30000, 1000, 0)));
    if (coophb::CursorQuota(100, 30000, 1000, 0) != 4)
        Fail("N=100 must be ceil(3.33)=4, got " + UU(coophb::CursorQuota(100, 30000, 1000, 0)));
    if (coophb::CursorQuota(0, 30000, 1000, 0) != 0) Fail("an empty registry must be 0");
    if (coophb::CursorQuota(50, 30000, 0, 0) != 0)   Fail("no time passed must be 0");
    if (coophb::CursorQuota(50, 30000, 600000, 0) != 50)
        Fail("a 10-minute stall must clamp to N, got " + UU(coophb::CursorQuota(50, 30000, 600000, 0)));
    if (coophb::CursorQuota(50, 30000, 600000, 8) != 8)
        Fail("the per-tick cap must bound one tick's work, got " + UU(coophb::CursorQuota(50, 30000, 600000, 8)));
}

/* (2) the ring's own cycle is at most the period, for every registry size (a property, not a spot value) */
void t_hb_ring_cycle_never_exceeds_the_period()
{
    unsigned int n;
    for (n = 1; n <= 400; ++n)
    {
        const unsigned int q = coophb::CursorQuota(n, 30000, 1000, 0);
        if (q == 0) { Fail("n=" + UU(n) + ": quota 0 - a registry that never advances"); return; }
        const unsigned int cycleTicks = (n + q - 1) / q;
        if (cycleTicks > 30) { Fail("n=" + UU(n) + " quota=" + UU(q) + ": ring cycle " + UU(cycleTicks) + " ticks > the 30-tick period"); return; }
        unsigned long long sum = 0; int t;
        for (t = 0; t < 30; ++t) sum += coophb::CursorQuota(n, 30000, 1000, 0);
        if (sum < (unsigned long long)n) { Fail("n=" + UU(n) + ": one period gives only " + UU((unsigned int)sum) + " turns - a group is starved"); return; }
    }
}

/* (3) the per-turn floor and the giant rule */
void t_hb_due_on_turn_floor_and_giant_rule()
{
    if (!coophb::DueOnTurn(false, 25000, 17000, 30000, 2000, 262144, 5)) Fail("AN ORDINARY GROUP MUST PUBLISH ON ITS TURN at since=25000 (F724)");
    if (!coophb::DueOnTurn(false, 2000, 17000, 30000, 2000, 262144, 5))  Fail("exactly at the 2 s floor must publish");
    if (coophb::DueOnTurn(false, 1999, 17000, 30000, 2000, 262144, 5))   Fail("inside the 2 s floor must NOT publish");
    if (!coophb::DueOnTurn(true, 2000, 17000, 30000, 2000, 262144, 5))   Fail("a dirty group publishes at the 2 s gap");
    if (coophb::DueOnTurn(true, 1999, 17000, 30000, 2000, 262144, 5))    Fail("a dirty group inside the gap must NOT");
    if (coophb::DueOnTurn(false, 0, 0, 30000, 2000, 262144, 5))          Fail("a group registered this instant must wait");
    if (coophb::DueOnTurn(false, 30000, 288056, 30000, 2000, 262144, 5)) Fail("a 288 KB giant must NOT be due at 30 s");
    if (coophb::DueOnTurn(false, 149999, 288056, 30000, 2000, 262144, 5)) Fail("a giant must NOT be due at 149.999 s");
    if (!coophb::DueOnTurn(false, 150000, 288056, 30000, 2000, 262144, 5)) Fail("a giant IS due at 5x the period");
    if (!coophb::DueOnTurn(true, 2000, 288056, 30000, 2000, 262144, 5))  Fail("a DIRTY giant still uses the 2 s gap");
}

/* (4) the budget defers and never drops, and bytes/s is a rate */
void t_hb_budget_defers_and_never_drops()
{
    if (!coophb::BudgetAllows(0, 17000, 65536))      Fail("the first 17 KB of a second must pass");
    if (!coophb::BudgetAllows(48000, 17000, 65536))  Fail("48 KB + 17 KB fits inside 64 KB");
    if (coophb::BudgetAllows(50000, 17000, 65536))   Fail("50 KB + 17 KB must NOT fit");
    if (coophb::BudgetAllows(65536, 1, 65536))       Fail("a full second must refuse everything");
    if (!coophb::BudgetAllows(65535, 0, 65536))      Fail("an unknown size (a first publish) is allowed");
    if (!coophb::BudgetAllows(999999, 999999, 0))    Fail("budget 0 means no budget");
    unsigned int spent = 0, sent = 0, deferred = 0; int i;
    for (i = 0; i < 4; ++i) { if (coophb::BudgetAllows(spent, 38000, 65536)) { spent += 38000; ++sent; } else ++deferred; }
    if (sent != 1 || deferred != 3) Fail("38 KB records: want sent=1 deferred=3, got sent=" + UU(sent) + " deferred=" + UU(deferred));
    if (coophb::BytesPerSec(65536, 1000) != 65536) Fail("64 KB in 1 s is 65536 B/s");
    if (coophb::BytesPerSec(65536, 60000) != 1092) Fail("64 KB in 60 s is 1092 B/s, got " + UU(coophb::BytesPerSec(65536, 60000)));
    if (coophb::BytesPerSec(1, 0) != 0)            Fail("a zero-length window is 0, not a divide by zero");
}

/* (4b) the budget carries its overspend: one simulated second = roll the window (BudgetCarry), then offer the groups as the
   plugin's tick does - the owed ones first, the oldest deferral first (a tie in queue order), then the rest in queue order. A group
   the budget refuses is passed over and marked owed, and the groups after it are still offered (HeartbeatTick moves past a refused
   group). `item[i]` != 0 = the group was marked by an item move (coophb::BudgetAllowsGroup). Per-tick serialise limits are not
   modelled. Returns the second each record went out in `at` (-1 = not by `seconds`). */
void HbDebtSimItems(const std::vector<unsigned int>& sizes, const std::vector<int>& item, unsigned int budget, int seconds,
                    std::vector<int>* at, unsigned long long* sentTotal)
{
    at->assign(sizes.size(), -1); *sentTotal = 0;
    std::vector<int> owedAt(sizes.size(), -1);
    unsigned int spent = 0, second = 0; int s;
    for (s = 0; s < seconds; ++s)
    {
        if (s > 0) { spent = coophb::BudgetCarry(spent, budget, 1); second = 0; }
        std::vector<size_t> order; size_t i; int w;
        for (w = 0; w < s; ++w)
            for (i = 0; i < sizes.size(); ++i) if ((*at)[i] < 0 && owedAt[i] == w) order.push_back(i);
        for (i = 0; i < sizes.size(); ++i) if ((*at)[i] < 0 && owedAt[i] < 0) order.push_back(i);
        for (i = 0; i < order.size(); ++i)
        {
            const size_t g = order[i];
            if (coophb::BudgetAllowsGroup(item[g] != 0, spent, second, sizes[g], budget))
            {
                spent = coophb::BudgetSpend(spent, sizes[g]); second = coophb::BudgetSpend(second, sizes[g]);
                (*at)[g] = s; *sentTotal += sizes[g];
            }
            else if (owedAt[g] < 0) owedAt[g] = s;
        }
    }
}
void HbDebtSim(const std::vector<unsigned int>& sizes, unsigned int budget, int seconds, std::vector<int>* at, unsigned long long* sentTotal)
{
    const std::vector<int> none(sizes.size(), 0);
    HbDebtSimItems(sizes, none, budget, seconds, at, sentTotal);
}
void t_hb_budget_carries_its_overspend()
{
    std::vector<unsigned int> q; std::vector<int> at; unsigned long long total = 0;
    /* a 2 MB record, then small ones: the big one goes into the empty first second, and the small ones wait out its 32 s of debt */
    q.push_back(2097152); q.push_back(17000); q.push_back(17000); q.push_back(17000);
    HbDebtSim(q, 65536, 60, &at, &total);
    if (at[0] != 0) Fail("a 2 MB record must go into the empty first second, went at " + UU((unsigned int)at[0]));
    if (at[1] != 32) Fail("2 MB = 32 seconds of a 64 KB budget: the next small record must wait until second 32, went at " + UU((unsigned int)at[1]));
    if (at[2] != 32 || at[3] != 32) Fail("three 17 KB records fit one empty 64 KB second (second 32)");
    /* a record just over the budget: what it owes is less than a second, so a small one fits the next second's remainder */
    q.clear(); q.push_back(100000); q.push_back(17000); q.push_back(17000);
    HbDebtSim(q, 65536, 10, &at, &total);
    if (at[0] != 0 || at[1] != 1) Fail("100000 B owes 34464 B: a 17 KB record fits second 1 (34464 + 17000 <= 65536)");
    if (at[2] != 2) Fail("a second 17 KB record does not fit what is left of second 1 (51464 + 17000 > 65536); it goes at second 2, went at " + UU((unsigned int)at[2]));
    /* several big ones: each waits for the debt of the one before it, and the long-run rate stays within the budget */
    q.clear(); q.push_back(1048576); q.push_back(1048576); q.push_back(1048576);
    HbDebtSim(q, 65536, 60, &at, &total);
    if (at[0] != 0 || at[1] != 16 || at[2] != 32) Fail("three 1 MB records must go at seconds 0, 16 and 32");
    if (total != 3ULL * 1048576ULL) Fail("all three 1 MB records must have gone by second 60 - a deferral never drops");
    if ((unsigned long long)3 * 1048576ULL > 65536ULL * (unsigned long long)(at[2] + 1) + 1048576ULL)
        Fail("the bytes sent by the last big record's second must stay within the budget plus one record");
    /* the debt is never negative and never wraps; a window within the budget owes nothing (callers under the budget unchanged) */
    if (coophb::BudgetCarry(40000, 65536, 1) != 0)  Fail("a window under the budget must owe nothing");
    if (coophb::BudgetCarry(65536, 65536, 1) != 0)  Fail("a window exactly at the budget must owe nothing");
    if (coophb::BudgetCarry(100, 65536, 7) != 0)    Fail("several seconds past a small spend must owe nothing, not a negative");
    if (coophb::BudgetCarry(70000, 65536, 0) != 70000) Fail("no whole second passed: the window's spend is unchanged");
    if (coophb::BudgetCarry(200000, 65536, 2) != 200000 - 131072) Fail("two seconds pay two budgets");
    if (coophb::BudgetCarry(0xFFFFFFFFu, 65536, 0xFFFFFFFFu) != 0) Fail("a huge elapsed time must pay everything without overflow");
    if (coophb::BudgetSpend(0xFFFFFFF0u, 100) != 0xFFFFFFFFu) Fail("the spend must saturate, not wrap to a small number");
    {   /* debt never negative, over a long random-ish run */
        unsigned int spent = 0, i;
        for (i = 0; i < 1000; ++i)
        {
            const unsigned int sz = (i * 7919u) % 3000000u;
            if (coophb::BudgetAllows(spent, sz, 65536)) spent = coophb::BudgetSpend(spent, sz);
            const unsigned int before = spent;
            spent = coophb::BudgetCarry(spent, 65536, 1);
            if (spent > before) { Fail("the carry must never grow the spend"); break; }
            if (before >= 65536 && spent != before - 65536) { Fail("each second must pay exactly one budget while a debt remains"); break; }
        }
    }
    /* a zero budget: no budget and no debt - every record goes in the first second */
    if (coophb::BudgetCarry(5000000, 0, 1) != 0) Fail("budget 0 must carry no debt");
    q.clear(); q.push_back(5000000); q.push_back(5000000); q.push_back(1);
    HbDebtSim(q, 0, 3, &at, &total);
    if (at[0] != 0 || at[1] != 0 || at[2] != 0) Fail("budget 0 must let every record go at once");
    /* a refused group does not block the ones after it: a 17 KB record that fits goes past a 70 KB one that does not */
    q.clear(); q.push_back(100000); q.push_back(70000); q.push_back(17000);
    HbDebtSim(q, 65536, 10, &at, &total);
    if (at[0] != 0 || at[2] != 1) Fail("the 17 KB record fits second 1's remainder (34464 + 17000) and goes past the refused 70 KB one");
    if (at[1] != 2) Fail("the 70 KB record goes into the first empty second (2), went at " + UU((unsigned int)at[1]));
}

/* (4c) an item hand-over's small record is not held by a big record's debt: it is held only to its own second's sends */
void t_hb_budget_item_move_is_not_held_by_the_debt()
{
    /* a group that has waited the bound behind the debt goes into an empty second like any other; before the bound it waits */
    if (coophb::BudgetAllowsGroup(false, 3u * 65536u, 0u, 200000u, 65536u, coophb::kDebtMaxWaitMs - 1)) Fail("a group short of the wait bound went while the debt is a full budget");
    if (!coophb::BudgetAllowsGroup(false, 3u * 65536u, 0u, 200000u, 65536u, coophb::kDebtMaxWaitMs)) Fail("a group past the wait bound was held by the debt in an empty second");
    if (coophb::BudgetAllowsGroup(false, 3u * 65536u, 70000u, 200000u, 65536u, coophb::kDebtMaxWaitMs)) Fail("a group past the wait bound went into a second already over the budget");
    std::vector<unsigned int> q; std::vector<int> item, at; unsigned long long total = 0;
    /* a 5.8 MB record, then a 20 KB group an item move marked and a 20 KB group no item move marked */
    q.push_back(5800000); item.push_back(0);
    q.push_back(20000);   item.push_back(1);
    q.push_back(20000);   item.push_back(0);
    HbDebtSimItems(q, item, 65536, 120, &at, &total);
    if (at[0] != 0) Fail("the 5.8 MB record goes into the empty first second");
    if (at[1] != 1) Fail("the item move's 20 KB record must go in the next second, not after the debt; went at " + UU((unsigned int)at[1]));
    if (at[2] != 89) Fail("a group no item move marked waits out the debt (5.82 MB at 64 KB/s = second 89), went at " + UU((unsigned int)at[2]));
    /* item records still share one budget per second among themselves */
    q.clear(); item.clear();
    q.push_back(3000000); item.push_back(0);
    q.push_back(30000);   item.push_back(1);
    q.push_back(30000);   item.push_back(1);
    q.push_back(30000);   item.push_back(1);
    HbDebtSimItems(q, item, 65536, 10, &at, &total);
    if (at[1] != 1 || at[2] != 1) Fail("two 30 KB item records fit one second's own 64 KB");
    if (at[3] != 2) Fail("a third 30 KB item record does not fit that second (90000 > 65536); it goes the next, went at " + UU((unsigned int)at[3]));
    /* the exemption is for a known size under the budget only */
    if (coophb::BudgetAllowsGroup(true, 5000000, 0, 70000, 65536))     Fail("an item-marked record over the budget still pays the debt");
    if (coophb::BudgetAllowsGroup(true, 5000000, 0, 65536, 65536))     Fail("an item-marked record of exactly the budget still pays the debt");
    if (coophb::BudgetAllowsGroup(true, 5000000, 0, 0, 65536))         Fail("an item-marked record of unknown size still pays the debt");
    if (coophb::BudgetAllowsGroup(false, 5000000, 0, 1000, 65536))     Fail("a group no item move marked pays the debt");
    if (!coophb::BudgetAllowsGroup(true, 5000000, 0, 1000, 65536))     Fail("a small item-marked record goes into a second that has sent nothing");
    if (coophb::BudgetAllowsGroup(true, 5000000, 60000, 10000, 65536)) Fail("a small item-marked record must still fit this second's own sends");
    if (!coophb::BudgetAllowsGroup(false, 0, 0, 5000000, 65536))       Fail("an empty window with no debt admits any record, as before");
}

/* (4d) a wake that applied the world's record skips only its own soon visit: a giant waking waits its long wait and then publishes */
void t_hb_wake_loaded_skip_is_the_first_soon_visit_only()
{
    const unsigned int win = coophb::kWakeLoadedSkipMs;
    if (!coophb::WakeLoadedSkip(true, 100, 100, 100, 2000, win))   Fail("the wake's own visit 2 s after the load, nothing written since: skipped");
    if (!coophb::WakeLoadedSkip(true, 100, 100, 100, win, win))    Fail("a visit at the window's end is still the wake's own");
    if (coophb::WakeLoadedSkip(true, 100, 100, 100, win + 1, win)) Fail("a visit after the window publishes");
    if (coophb::WakeLoadedSkip(false, 100, 100, 100, 2000, win))   Fail("an item move since the wake publishes");
    if (coophb::WakeLoadedSkip(true, 0, 0, 0, 2000, win))          Fail("no record applied at the wake: publishes");
    if (coophb::WakeLoadedSkip(true, 100, 101, 100, 2000, win))    Fail("a newer world record since the load: publishes");
    if (coophb::WakeLoadedSkip(true, 100, 100, 0, 2000, win))      Fail("no local stamp known: publishes");
    if (coophb::WakeLoadedSkip(true, 100, 100, 99, 2000, win))     Fail("this copy wrote another record since: publishes");
    /* a woken giant: a wake alone waits the giant wait (DueOnTurn with dirty = false), and that first visit publishes */
    const unsigned int giant = 5800000;
    const unsigned int giantWait = coophb::kPeriodMs * coophb::kGiantMultiplier;
    if (coophb::DueOnTurn(false, giantWait - 1, giant, coophb::kPeriodMs, coophb::kDirtyGapMs, coophb::kGiantBytes, coophb::kGiantMultiplier))
        Fail("a woken giant waits the giant wait");
    if (!coophb::DueOnTurn(false, giantWait, giant, coophb::kPeriodMs, coophb::kDirtyGapMs, coophb::kGiantBytes, coophb::kGiantMultiplier))
        Fail("a woken giant is due when the giant wait ends");
    if (coophb::WakeLoadedSkip(true, 100, 100, 100, giantWait, win))
        Fail("the woken giant's first visit, a giant wait after the load, must publish - a skip would hold its changes for a second wait");
    if (win >= giantWait) Fail("the skip window must be shorter than the giant wait");
    if (win < coophb::kDirtyGapMs) Fail("the skip window must cover the wake's own visit after the 2 s gap");
}

/* (5) THE DECISIVE ONE: run the ring at fifty groups for four simulated minutes and measure the worst gap delivered */
void t_hb_ring_at_fifty_squads_delivers_a_thirty_second_period()
{
    const unsigned int N = 50;
    std::vector<unsigned int> lastPub(N, 0u);
    std::vector<unsigned int> pubs(N, 0u);
    unsigned int ringAt = 0, worst = 0, t;
    for (t = 1000; t <= 240000; t += 1000)
    {
        const unsigned int quota = coophb::CursorQuota(N, 30000, 1000, 8);
        unsigned int i;
        for (i = 0; i < quota; ++i)
        {
            const unsigned int idx = ringAt % N; ++ringAt;
            const unsigned int since = t - lastPub[idx];
            if (!coophb::DueOnTurn(false, since, 17000, 30000, 2000, 262144, 5)) continue;
            if (t > 60000 && since > worst) worst = since;
            if (t > 60000) ++pubs[idx];
            lastPub[idx] = t;
        }
    }
    if (worst == 0) Fail("NOTHING PUBLISHED IN 240 SIMULATED SECONDS - a zero here is an absence, not a pass");
    if (worst > 30000) Fail("THE RING DELIVERS A " + UU(worst / 1000) + "-SECOND HEARTBEAT AT FIFTY GROUPS, NOT 30 (F724)");
    unsigned int k;
    for (k = 0; k < N; ++k) if (pubs[k] == 0) { Fail("group " + UU(k) + " never published in three measured minutes"); return; }
}

/* inv7c: confirm -> escrow -> placed / re-sent placed / session-end return / put-back failure kept. */
void t_inv7c_escrow()
{
    using namespace coopesc;
    const unsigned int T = 10000, Q = kEscrowQuietMs;
    if (ExpireAction(0, 1, 0, T - 1, 0, T, Q) != kExpWait) Fail("inv7c: a TAKE row waits inside its 10 s");
    if (ExpireAction(0, 1, 0, T, 0, T, Q) != kExpEscrow) Fail("inv7c: a TAKE whose ok CONFIRM went out moves to ESCROW at 10 s - never rolled back blindly");
    if (ExpireAction(0, 0, 0, T, 0, T, Q) != kExpRollback) Fail("inv7c: a TAKE whose CONFIRM never went out still rolls back at 10 s");
    if (ExpireAction(1, 1, 0, T, 0, T, Q) != kExpStand) Fail("inv7c: a GIVE nobody revoked still stands at 10 s");
    if (ExpireAction(0, 1, 1, 4000000000u, Q - 1, T, Q) != kExpWait) Fail("inv7c: escrow waits while quiet less than the escrow period (the 10 s no longer applies)");
    if (ExpireAction(0, 1, 1, 0, Q, T, Q) != kExpReturn) Fail("inv7c: escrow quiet for the whole period is returned to the owner (D6)");
    if (PlacedKind(0, 0) != kPlLive || PlacedKind(0, 1) != kPlLive) Fail("inv7c: a PLACED inside the 10 s is the ordinary answer");
    if (PlacedKind(1, 0) != kPlEscrowPlaced) Fail("inv7c: a PLACED on the same link settles escrow as resolvedPlaced");
    if (PlacedKind(1, 1) != kPlEscrowResent) Fail("inv7c: a PLACED after a link-up settles escrow as resolvedResent");
    if (PutBackArm(1, 1) != kPbPut || PutBackArm(2, 0) != kPbPut) Fail("inv7c: a put-back that re-adds (or relocates) is done");
    if (PutBackArm(-1, 1) != kPbFaulted) Fail("inv7c: a faulted add keeps its own arm (nothing retried, nothing destroyed)");
    if (PutBackArm(0, 1) != kPbKeep) Fail("inv7c: a put-back that cannot re-add a live object STAYS IN ESCROW - never destroyed");
    if (PutBackArm(0, 0) != kPbLost) Fail("inv7c: an object that is not live any more takes the old arm");
    if (LateTakeAction(1, 0, 0, 0) != kLtPlace) Fail("inv7c: a late ok CONFIRM for a plain TAKE is placed and answered");
    if (LateTakeAction(1, 1, 0, 0) != kLtPlace) Fail("inv7c: a late purchase whose refund did not move money is placed");
    if (LateTakeAction(1, 1, 0, 1) != kLtAnswerNo) Fail("inv7c: a late purchase already refunded is answered PLACED ok=0");
    if (LateTakeAction(0, 0, 0, 0) != kLtNone || LateTakeAction(1, 0, 1, 0) != kLtNone) Fail("inv7c: a late refusal or a GIVE is not this path");
    {
        TakerList tl;
        if (tl.Note(41, 1, 1, 3, 1000) != 0 || tl.Count() != 1) Fail("inv7c: the taker lists a PLACED it sent");
        const int i41 = tl.Find(41);
        if (tl.Due(i41, 3, 1) != 0) Fail("inv7c: an answer handed to this link is not re-sent on it");
        if (tl.Prune(3, 1, 1000 + kTakerSettleMs - 1, kTakerSettleMs) != 0) Fail("inv7c: kept inside the settle time");
        if (tl.Prune(4, 0, 1000 + kTakerSettleMs, kTakerSettleMs) != 0) Fail("inv7c: a down link never drops an entry");
        if (tl.Due(i41, 5, 0) != 0) Fail("inv7c: nothing is re-sent while the link is down");
        if (tl.Due(i41, 5, 1) != 1) Fail("inv7c: its link went down - re-sent at the next link-up");
        tl.MarkSent(i41, 5, 50000);
        if (tl.Due(i41, 5, 1) != 0) Fail("inv7c: re-sent once per link");
        if (tl.Prune(5, 1, 50000 + kTakerSettleMs, kTakerSettleMs) != 1 || tl.Count() != 0) Fail("inv7c: settled on an up link - dropped");
        tl.Note(42, 0, 0, 5, 60000);
        if (tl.Due(tl.Find(42), 5, 1) != 1) Fail("inv7c: an answer that could not be sent is due as soon as the link is up");
        int over = 0;
        for (int k = 0; k < kTakerCap; ++k) over += tl.Note(100 + (unsigned int)k, 1, 1, 5, 60000);
        if (over != 1 || tl.Count() != kTakerCap) Fail("inv7c: a full list overwrites the oldest slot in turn and says so");
        tl.Clear();
        if (tl.Count() != 0) Fail("inv7c: the session end clears the taker's list");
    }
    {
        const unsigned int P1 = cooplive::RelayPeerId(1);
        DoneRing r;
        r.Note(P1, 7);
        if (r.Has(P1, 7, 1, 0) == 0 || r.Has(P1, 8, 1, 0) != 0 || r.Has(P1, 0, 1, 0) != 0) Fail("inv7c: the done ring knows a settled id and nothing else");
        for (int k = 0; k < kDoneCap; ++k) r.Note(P1, 1000 + (unsigned int)k);
        if (r.Has(P1, 7, 1, 0) != 0) Fail("inv7c: the done ring forgets the oldest");
    }
    {   /* T-355: request ids are each requesting game's own - with three players two others can send the same id. The old rule
           (id alone) said player 2's first PLACED for id 7 was a duplicate of player 1's; each must be kept distinct. */
        const unsigned int P1 = cooplive::RelayPeerId(1), P2 = cooplive::RelayPeerId(2), P7 = cooplive::RelayPeerId(7);
        DoneRing r;
        r.Note(P1, 7);
        if (r.Has(P2, 7, 1, 0) != 0) Fail("T-355: player 2's id 7 is not a duplicate of player 1's id 7");
        if (r.Has(P1, 7, 1, 0) == 0) Fail("T-355: player 1's own re-send of id 7 is still a duplicate");
        r.Note(P2, 7);
        if (r.Has(P1, 7, 1, 0) == 0 || r.Has(P2, 7, 1, 0) == 0 || r.Has(P7, 7, 1, 0) != 0) Fail("T-355: two senders, the same id - both kept, a third player matches neither");
        if (PlacedMissAction(r.Has(P7, 7, 1, 0), 0, 0) != kMissUnknown) Fail("T-355: a third player's PLACED for another's id is unknown, not a duplicate");
        if (SameSender(P1, P1, 1, 0) != 1 || SameSender(P1, P2, 1, 0) != 0) Fail("T-355: two slot keys match only when they are the same player");
        if (SameSender(5u, P2, 0, 0) != 1 || SameSender(P1, 9u, 0, 0) != 1 || SameSender(5u, 9u, 0, 0) != 1)
            Fail("T-355: with no notebook slot here, a raw sender id (no slot known: the one session peer) keeps the two-player reading - id alone");
        DoneRing legacy;
        legacy.Note(5u, 7);
        if (legacy.Has(9u, 7, 0, 0) == 0 || legacy.Has(9u, 7, 1, 0) == 0) Fail("T-355: with no slots known (no notebook) a re-send from a renumbered peer is still the duplicate it was");
        r.Clear();
        if (r.Has(P1, 7, 1, 0) != 0 || r.Has(P2, 7, 1, 0) != 0) Fail("T-355: Clear forgets every sender's ids");
    }
    {   /* T-355 fold 1 (review F1): a RAW sender key (no slot known for it) matches a slot key only while THIS game has no notebook
           slot itself - the two-game, no-notebook world where raw is the only form. With a slot of its own, raw matches raw only:
           a session peer that never said its slot can neither settle nor be shadowed by a third player's row with the same id. */
        const unsigned int P2 = cooplive::RelayPeerId(2), P3 = cooplive::RelayPeerId(3), RAW = 5u;
        if (SameSender(RAW, P2, 0, 0) != 1 || SameSender(RAW, P3, 0, 0) != 1 || SameSender(P2, RAW, 0, 0) != 1)
            Fail("T-355 F1: with no slot of its own, a raw key keeps the two-player reading against any slot key");
        if (SameSender(RAW, P2, 1, 0) != 0 || SameSender(RAW, P3, 1, 0) != 0 || SameSender(P2, RAW, 1, 0) != 0 || SameSender(P3, RAW, 1, 0) != 0)
            Fail("T-355 F1: with a slot of its own, a raw key matches neither of two different slot keys, in either direction");
        if (SameSender(RAW, 9u, 1, 0) != 1 || SameSender(RAW, 9u, 0, 0) != 1) Fail("T-355 F1: raw matches raw in both worlds (a renumbered session peer)");
        if (SameSender(P2, P2, 1, 0) != 1 || SameSender(P2, P2, 0, 0) != 1 || SameSender(P2, P3, 1, 0) != 0 || SameSender(P2, P3, 0, 0) != 0)
            Fail("T-355 F1: two slot keys are compared the same whatever the flag");
        DoneRing r;   /* the review's case: C (slot 3) settled its id 7 here; A (raw - it never sent PEER_SLOT) re-sends ITS id 7 */
        r.Note(P3, 7);
        if (r.Has(RAW, 7, 1, 0) != 0) Fail("T-355 F1: C's settled id 7 does not make A's real re-send of its id 7 a duplicate");
        if (r.Has(RAW, 7, 0, 0) != 1) Fail("T-355 F1: with no slot here the id-alone reading is unchanged");
        r.Note(RAW, 7);
        if (r.Has(RAW, 7, 1, 0) != 1 || r.Has(P3, 7, 1, 0) != 1 || r.Has(P2, 7, 1, 0) != 0) Fail("T-355 F1: A's and C's id 7 kept apart, a third player matches neither");
    }
    {   /* T-355 fold 1 (review F3): the sender-key conversion feeding the match. items.cpp ItEscSenderKey is exactly
           cooplive::PlayerKeyOf(peer, LinkPeerSlot()) - already pure - so the path is tested as PlayerKeyOf then SameSender. */
        const unsigned int RAW = 5u, P3 = cooplive::RelayPeerId(3);
        if (cooplive::PlayerKeyOf(RAW, 3) != P3) Fail("T-355 F3: the session peer's raw id folds to its slot once PEER_SLOT said it");
        if (cooplive::PlayerKeyOf(RAW, -1) != RAW) Fail("T-355 F3: before PEER_SLOT the raw id stays the key");
        if (cooplive::PlayerKeyOf(P3, -1) != P3 || cooplive::PlayerKeyOf(P3, 4) != P3) Fail("T-355 F3: a relayed sender is its own key whatever the link slot");
        if (SameSender(cooplive::PlayerKeyOf(RAW, 3), cooplive::PlayerKeyOf(P3, 3), 1, 0) != 1)
            Fail("T-355 F3: the session peer's re-send on the link matches its own row that came through the notebook");
        if (SameSender(cooplive::PlayerKeyOf(RAW, 3), cooplive::PlayerKeyOf(cooplive::RelayPeerId(4), 3), 1, 0) != 0)
            Fail("T-355 F3: the folded session peer does not match another player's slot");
        if (SameSender(cooplive::PlayerKeyOf(RAW, -1), cooplive::PlayerKeyOf(P3, -1), 1, 0) != 0)
            Fail("T-355 F3: before PEER_SLOT, with a slot here, a raw re-send NOT from the current session peer is not matched to slot 3's row");
    }
    {   /* T-355 fold 2 (re-check D1): two games with a notebook; the TAKE reached the owner through the notebook (row keyed by the
           taker's slot 2); the taker's notebook link drops (it never sends PEER_SLOT - StoreMySlot() -1), the session link reconnects,
           and its re-send arrives RAW on the session link while this game's LinkPeerSlot() is -1 and this game has a slot. */
        const unsigned int P2 = cooplive::RelayPeerId(2), P3 = cooplive::RelayPeerId(3), RAW = 5u, OTHER = 6u;
        if (RawIsSessionPeerUnslotted(RAW, 1, RAW, -1) != 1) Fail("T-355 D1: the raw current session peer with no link slot is flagged");
        if (RawIsSessionPeerUnslotted(OTHER, 1, RAW, -1) != 0) Fail("T-355 D1: a raw id that is not the current session peer is not flagged");
        if (RawIsSessionPeerUnslotted(RAW, 0, RAW, -1) != 0) Fail("T-355 D1: with no session peer up nothing is flagged");
        if (RawIsSessionPeerUnslotted(RAW, 1, RAW, 2) != 0) Fail("T-355 D1: with the link slot known nothing is flagged (the raw id folds first)");
        if (RawIsSessionPeerUnslotted(P2, 1, P2, -1) != 0) Fail("T-355 D1: a relayed sender is never flagged");
        const unsigned int from = cooplive::PlayerKeyOf(RAW, -1);   /* items.cpp ItEscSenderKey with LinkPeerSlot() -1 */
        const int sess = RawIsSessionPeerUnslotted(from, 1, RAW, -1);
        if (SameSender(P2, from, 1, sess) != 1) Fail("T-355 D1: the session peer's raw re-send settles its own row keyed by its slot (no copy on both games)");
        DoneRing r;
        r.Note(P2, 7);
        if (r.Has(from, 7, 1, sess) != 1) Fail("T-355 D1: the session peer's raw repeat of its settled id is a duplicate");
        /* the 3-player closure: a raw sender that is NOT the current session peer never matches a slot key while this game has a slot */
        const int notSess = RawIsSessionPeerUnslotted(OTHER, 1, RAW, -1);
        if (SameSender(P3, OTHER, 1, notSess) != 0 || SameSender(P2, OTHER, 1, notSess) != 0) Fail("T-355 D1: a raw non-session sender still matches no slot key");
        if (r.Has(OTHER, 7, 1, notSess) != 0) Fail("T-355 D1: a raw non-session sender's id 7 is not another player's duplicate");
        if (SameSender(OTHER, P3, 1, 1) != 0) Fail("T-355 D1: a stored raw key is never widened against an arriving slot key");
        if (SameSender(P3, P2, 1, 1) != 0) Fail("T-355 D1: two different slot keys never match, whatever the flag");
    }
    {
        std::vector<coopown::LedgerEntry> v;
        coopown::LedgerEntry e;
        e.peer = "p1"; e.msgId = 9; e.role = coopown::kLedgerRoleRequester; e.dirIn = coopown::kLedgerRev;
        v.push_back(e);
        const int In = coopown::kLedgerIn, Rev = coopown::kLedgerRev, Req = coopown::kLedgerRoleRequester;
        if (LedgerPlacedState(v, "p1", 9, In, Rev, Req) != 0) Fail("inv7c: an unpaired rev is a no-op");
        e.dirIn = In; v.push_back(e);
        if (LedgerPlacedState(v, "p1", 9, In, Rev, Req) != 1) Fail("inv7c: a requester IN line says we placed it");
        coopown::LedgerEntry o = e; o.peer = "p2"; o.dirIn = Rev; v.push_back(o);
        coopown::LedgerEntry w = e; w.role = coopown::kLedgerRoleOwner; w.dirIn = Rev; v.push_back(w);
        if (LedgerPlacedState(v, "p1", 9, In, Rev, Req) != 1) Fail("inv7c: another peer's or the owner role's line is not paired");
        e.dirIn = Rev; v.push_back(e);
        if (LedgerPlacedState(v, "p1", 9, In, Rev, Req) != -1) Fail("inv7c: a LATER rev cancels the IN");
        if (LedgerPlacedState(v, "p1", 10, In, Rev, Req) != 0) Fail("inv7c: another message id has no line");
    }
    {   /* the sequences as items.cpp drives them */
        int esc = 0, rel = 0;
        if (ExpireAction(0, 1, esc, T, 0, T, Q) == kExpEscrow) esc = 1;
        if (esc != 1 || PlacedKind(esc, rel) != kPlEscrowPlaced) Fail("inv7c seq A: confirm -> escrow -> PLACED = resolvedPlaced");
        rel = 1;   /* a link-up while in escrow */
        if (PlacedKind(esc, rel) != kPlEscrowResent) Fail("inv7c seq B: escrow -> link-up -> re-sent PLACED = resolvedResent");
        int keptThenPut = 0;
        if (ExpireAction(0, 1, esc, 0, Q, T, Q) == kExpReturn && PutBackArm(0, 1) == kPbKeep
            && ExpireAction(0, 1, esc, 0, 0, T, Q) == kExpWait && ExpireAction(0, 1, esc, 0, Q, T, Q) == kExpReturn
            && PutBackArm(1, 1) == kPbPut) keptThenPut = 1;
        if (keptThenPut != 1) Fail("inv7c seq C: quiet -> return refused -> kept in escrow -> next quiet -> returned");
    }
}



/* (6) review-s2hb fold: a record larger than the whole budget is admitted into an EMPTY second, and only into one */
void t_hb_fold_oversized_record_admitted_into_an_empty_second()
{
    if (!coophb::BudgetAllows(0, 288056, 65536)) Fail("a 288 KB record must be admitted when nothing was spent this second - else the cursor stalls behind it");
    if (coophb::BudgetAllows(1, 288056, 65536))  Fail("a 288 KB record must NOT be admitted into a second that already spent bytes");
    if (!coophb::BudgetAllows(0, 17000, 65536))  Fail("an ordinary first record of a second still passes");
}

/* (7) A GIANT DOES NOT BLOCK THE RING, THE CURSOR IS NEVER STARVED, AND A RUN OF CHANGES DOES NOT HOLD AN OWED GIANT FOR EVER.
   The plugin's rules, modelled: 3 serialises a tick; the first pass (changed groups oldest change first and owed ones oldest
   deferral first, taking turns, with the debt's share putting an owed group that waited kDebtMaxWaitMs first once every
   kDebtShareMs - coophb::PassOrder) takes at most 2 of them, so the cursor always gets one; a budget deferral moves
   past the group and marks it owed. Fifty groups: one 288 KB giant, five in a running fight (changed every second - a demand of
   2.5 serialises a tick, so without the share the giant would never meet an empty second). */
void t_hb_fold_giant_does_not_block_the_ring()
{
    const unsigned int N = 50, G = 1, CAP = 3;
    std::vector<unsigned int> lastPub(N, 0u), size(N, 17000u), pubs(N, 0u), owedAt(N, 0u);
    std::vector<int> owed(N, 0), dirty(N, 0);
    std::vector<unsigned int> markAt(N, 0u);
    size[G] = 288056;
    unsigned int ringAt = 0, worstQuiet = 0, giantWait = 0, worstFight = 0, shareAt = 0, t, k;
    for (t = 1000; t <= 300000; t += 1000)
    {
        unsigned int spent = 0, ser = 0;
        for (k = 0; k < N; ++k) if (k % 10 == 5) { if (!dirty[k]) markAt[k] = t; dirty[k] = 1; }
        std::vector<unsigned int> first;
        std::vector<coophb::PassKey> keys;
        for (k = 0; k < N; ++k)
        {
            if (!owed[k] && !dirty[k]) continue;
            coophb::PassKey c;
            c.dirty = dirty[k]; c.markAgeMs = dirty[k] ? t - markAt[k] : 0u; c.owedAgeMs = owed[k] ? t - owedAt[k] : 0u;
            const unsigned int at = coophb::PassInsertAt(keys.empty() ? 0 : &keys[0], (unsigned int)keys.size(), c);
            keys.insert(keys.begin() + at, c); first.insert(first.begin() + at, k);
        }
        std::vector<unsigned int> order(keys.size());
        const unsigned int share = keys.empty() ? 0u : coophb::PassOrder(&keys[0], (unsigned int)keys.size(), t - shareAt, &order[0]);
        const unsigned int shareK = share < keys.size() ? first[share] : N;
        { std::vector<unsigned int> sending(keys.size()); for (size_t j = 0; j < order.size(); ++j) sending[j] = first[order[j]]; first.swap(sending); }
        size_t f;
        for (f = 0; f < first.size(); ++f)
        {
            if (ser >= CAP - 1) break;   /* one serialise a tick is the cursor's */
            k = first[f];
            if (!coophb::DueOnTurn(true, t - lastPub[k], size[k], 30000, 2000, 262144, 5)) continue;
            if (!coophb::BudgetAllows(spent, size[k], 65536)) { if (!owed[k]) { owed[k] = 1; owedAt[k] = t; } continue; }
            if (owed[k] && k == G && t - owedAt[k] > giantWait) giantWait = t - owedAt[k];
            if (t > 60000 && k % 10 != 5 && k != G && t - lastPub[k] > worstQuiet) worstQuiet = t - lastPub[k];
            if (t > 60000 && k % 10 == 5 && t - markAt[k] > worstFight) worstFight = t - markAt[k];
            if (k == shareK) shareAt = t;
            owed[k] = 0; dirty[k] = 0; ++ser; spent += size[k]; lastPub[k] = t; if (t > 60000) ++pubs[k];
        }
        const unsigned int quota = coophb::CursorQuota(N, 30000, 1000, 0);
        unsigned int i;
        for (i = 0; i < quota; ++i)
        {
            if (ser >= CAP) break;   /* the cursor keeps its place */
            const unsigned int idx = ringAt % N; ++ringAt;
            const unsigned int since = t - lastPub[idx];
            if (!coophb::DueOnTurn(false, since, size[idx], 30000, 2000, 262144, 5)) continue;
            if (!coophb::BudgetAllows(spent, size[idx], 65536)) { if (!owed[idx]) { owed[idx] = 1; owedAt[idx] = t; } continue; }
            if (t > 60000 && idx % 10 != 5 && idx != G && since > worstQuiet) worstQuiet = since;
            owed[idx] = 0; dirty[idx] = 0; ++ser; spent += size[idx]; lastPub[idx] = t; if (t > 60000) ++pubs[idx];
        }
    }
    if (worstQuiet == 0) Fail("nothing published - an absence, not a pass");
    if (worstQuiet > 60000) Fail("the dirty pass starved the cursor: a quiet group waited " + UU(worstQuiet) + " ms (the first pass must leave the cursor one serialise a tick)");
    if (giantWait > coophb::kDebtMaxWaitMs + coophb::kDebtShareMs + 1000)
        Fail("a deferred giant waited " + UU(giantWait) + " ms under a run of changes - the debt's share must send it within kDebtMaxWaitMs + kDebtShareMs");
    if (giantWait == 0) Fail("the giant was never deferred and sent after a wait - the share was not exercised");
    if (worstFight > 10000) Fail("a changed group waited " + UU(worstFight) + " ms from its change to its send - seconds, not its 30 s floor");
    for (k = 0; k < N; ++k) if (pubs[k] == 0) { Fail("group " + UU(k) + (k == G ? " (the giant)" : "") + " never published in four measured minutes"); return; }
}

/* ================================ T-1084 step 4: A SQUAD EVENT MARKS ITS WORLD RECORD DUE ON THE EARLY ROAD ==========
   A member's death, a member in or out, the squad's money, its home building: the squad's record goes within the heartbeat's 2 s
   gap instead of waiting up to its 30 s floor (150 s for a giant). Each test runs the decisions store.cpp runs, from squadevent.h:
   NoteAction (SqEventNote), MarkAccepted + MarkFlags (HbMarkDirty), DueForVisit (HbDue, the wake-only rule), EntryHolds
   (SqEventMarkHolder), PotPlatoon / MoneyMarks / HomeChanged (the money and home hooks), the tagged ring (SqEventDrainTick). */

/* one tracked squad's heartbeat fields, as store.cpp's HbEntry holds them */
struct SqevEntry { int dirty, dirtyRecord, dirtyWriter, owed, slow, foreign; unsigned int lastVisit, lastBytes, publishes; };
static SqevEntry SqevFresh(unsigned int lastBytes) { SqevEntry e = { 0, 0, 0, 0, 0, 0, 0, lastBytes, 0 }; return e; }
/* HbMarkDirty: the gate, then the flags */
static void SqevMark(SqevEntry* e, int registered, int isWake) { sqevent::MarkEntry(registered, e->foreign, isWake, &e->dirty, &e->dirtyRecord); }   /* HbMarkDirty's own routing */
/* HeartbeatTick's dirty pass: a dirty entry that HbDue (DueForVisit) admits is published and its flags cleared, as HbPublishOne does */
static void SqevDirtyPass(SqevEntry* e, unsigned int now)
{
    if (e->dirty == 0 || e->foreign != 0) return;
    if (!sqevent::DueForVisit(true, e->slow, e->dirty, e->dirtyRecord, e->dirtyWriter, e->owed, now - e->lastVisit, e->lastBytes)) return;
    e->dirty = 0; e->dirtyRecord = 0; e->dirtyWriter = 0; e->owed = 0; e->lastVisit = now; ++e->publishes;
}

void t_sqev_event_marks_the_record_due()
{
    if (sqevent::NoteAction(1, 0, 0) != sqevent::kActMarkNow) Fail("t1084: an event on the main thread, with a squad tracked and no load, is marked now");
    if (sqevent::NoteAction(1, 1, 0) != sqevent::kActSkip || sqevent::NoteAction(1, 0, 1) != sqevent::kActSkip)
        Fail("t1084: on the main thread nothing is marked with nothing tracked or during a load / teardown");
    SqevEntry e = SqevFresh(300000u);   /* a giant record: its floor is 150 s */
    SqevMark(&e, 1, 0);
    if (e.dirty != 1 || e.dirtyRecord != 1) Fail("t1084: an event on a squad this game writes marks it dirty, as a change to its record");
    const bool marked = e.dirtyRecord != 0;
    SqevDirtyPass(&e, 1000);
    if (e.publishes != 0) Fail("t1084: the mark waits for the 2 s gap after the last visit");
    SqevDirtyPass(&e, 2000);
    if (e.publishes != 1 || e.dirty != 0) Fail("t1084: the marked giant publishes at the gap (its floor does not hold an event) and the visit clears the mark");
    /* the wake-only rule: a wake alone is not a change to the record, so a giant marked only by it waits as on the floor */
    SqevEntry w = SqevFresh(300000u);
    SqevMark(&w, 1, 1);
    if (w.dirty != 1 || w.dirtyRecord != 0) Fail("t1084: a wake sets dirty alone");
    SqevDirtyPass(&w, 2000);
    if (w.publishes != 0) Fail("t1084: a giant marked by a wake alone is not published at the gap");
    SqevMark(&w, 1, 0);
    SqevDirtyPass(&w, 3000);
    if (w.publishes != 1) Fail("t1084: an event after the wake makes the giant's mark a change to its record - published at the gap");
    if (!coophb::BudgetAllowsGroup(marked, 200000u, 0u, 20000u, coophb::kBudgetBytesPerSec))
        Fail("t1084: a marked squad's small record is held only to this second's own sends, not to an older record's debt");
    if (coophb::BudgetAllowsGroup(marked, 200000u, 60000u, 20000u, coophb::kBudgetBytesPerSec))
        Fail("t1084: a marked squad still keeps a second within the 64 KB/s budget");
}

void t_sqev_burst_of_events_is_one_send_per_window()
{
    SqevEntry e = SqevFresh(17000u);   /* published at t=0 */
    unsigned int t;
    for (t = 50; t <= 1950; t += 50) SqevMark(&e, 1, 0);   /* 39 deaths in a fight, inside one 2 s window */
    if (e.dirty != 1 || e.dirtyRecord != 1) Fail("t1084: N marks leave the two flags as one mark does");
    for (t = 1000; t <= 10000; t += 1000) SqevDirtyPass(&e, t);
    if (e.publishes != 1) Fail("t1084: 39 events in one window must publish once, got " + UU(e.publishes));
    for (t = 10050; t <= 10500; t += 50) SqevMark(&e, 1, 0);   /* a second burst, later */
    for (t = 11000; t <= 20000; t += 1000) SqevDirtyPass(&e, t);
    if (e.publishes != 2) Fail("t1084: a later burst publishes once more, got " + UU(e.publishes));
}

void t_sqev_member_set_changes()
{
    int c[5] = { 0, 0, 0, 0, 0 };
    void* a[3] = { &c[0], &c[1], &c[2] };
    void* b[3] = { &c[2], &c[0], &c[1] };   /* the same set in another order */
    void* d[3] = { &c[0], &c[1], &c[3] };   /* c[2] left and c[3] joined between two looks */
    void* f[2] = { &c[0], &c[1] };
    const unsigned long long pa = sqevent::MemberPrint(a, 3);
    if (sqevent::MemberPrint(b, 3) != pa) Fail("t1084: the member fingerprint does not depend on order");
    if (sqevent::MembersChanged(-1, 0, 3, pa) != 0) Fail("t1084: the first look at a squad's members only records them");
    if (sqevent::MembersChanged(3, pa, 3, pa) != 0) Fail("t1084: unchanged members are no event");
    if (sqevent::MembersChanged(3, pa, 3, sqevent::MemberPrint(d, 3)) != 1) Fail("t1084: a leave and a join between two looks (same count) is an event");
    if (sqevent::MembersChanged(3, pa, 2, sqevent::MemberPrint(f, 2)) != 1) Fail("t1084: a member out (a recruit, a transfer) is an event");
    if (sqevent::MembersChanged(3, pa, -1, 0) != 0) Fail("t1084: an unread list is no event");
    /* SqEventMarkHolder's per-entry decision */
    if (sqevent::EntryHolds(0, 1, 3, a, &c[1]) != 1) Fail("t1084: a tracked squad this game writes holds its dead member (address compare)");
    if (sqevent::EntryHolds(1, 1, 3, a, &c[1]) != 0) Fail("t1084: a squad another game writes is not marked for a death seen here");
    if (sqevent::EntryHolds(0, 0, 3, a, &c[1]) != 0 || sqevent::EntryHolds(0, 1, 3, a, &c[4]) != 0) Fail("t1084: an unread list, or a character not in it, holds nothing");
    if (sqevent::EntryHolds(0, 1, sqevent::kMaxMembers + 1, a, &c[1]) != -1) Fail("t1084: a list over kMaxMembers is not looked at (counted apart)");
}

void t_sqev_non_writer_event_sends_nothing()
{
    if (sqevent::MarkAccepted(1, 1) != 0) Fail("t1084: a squad another game writes takes no mark here");
    if (sqevent::MarkAccepted(0, 0) != 0) Fail("t1084: a squad the heartbeat does not track takes no mark");
    SqevEntry e = SqevFresh(17000u);
    e.foreign = 1;
    unsigned int t;
    for (t = 100; t <= 1500; t += 100) SqevMark(&e, 1, 0);
    for (t = 1000; t <= 20000; t += 1000) SqevDirtyPass(&e, t);
    if (e.dirty != 0 || e.publishes != 0) Fail("t1084: another game's squad's events send nothing from this game");
}

void t_sqev_money_and_home_rules()
{
    if (sqevent::MoneyMarks(1, 120) != 1 || sqevent::MoneyMarks(1, -120) != 1) Fail("t1084: money taken, or given (a negative amount), marks");
    if (sqevent::MoneyMarks(0, 120) != 0) Fail("t1084: a refused take (not enough money) changed nothing");
    if (sqevent::MoneyMarks(1, 0) != 0) Fail("t1084: a take of 0 changed nothing");
    char platoon[0x200];
    if (sqevent::PotPlatoon(platoon + 0x148) != (void*)platoon) Fail("t1084: a pot names the platoon it is embedded in, 0x148 before it");
    if (sqevent::PotPlatoon((const void*)0x100) != 0) Fail("t1084: an address below the pot's offset names no platoon");
    unsigned char before[sqevent::kHomeHandBytes], after[sqevent::kHomeHandBytes];
    std::memset(before, 0, sizeof(before)); std::memcpy(after, before, sizeof(after));
    if (sqevent::HomeChanged(before, after) != 0) Fail("t1084: a home call that changed nothing (the engine fills only an empty home) is no event");
    after[27] = 1;
    if (sqevent::HomeChanged(before, after) != 1) Fail("t1084: any of the 28 bytes of the home hand changed is an event");
}
/* T-1084: the heartbeat's dirty pass (store.cpp HeartbeatTick, hbmath.h PassAhead / PassInsertAt): every marked group, owed or not,
   goes ahead of every group only owed, so a death's record does not wait behind the routine debt an authority never empties. Marked
   groups go oldest mark first, owed-only ones oldest deferral first, equals in the order they came. Built the way the store builds it. */
void t_sqev_marked_groups_go_ahead_of_owed_ones()
{
    /* the candidates in registry order: { dirty, markAgeMs, owedAgeMs } */
    const coophb::PassKey c[6] = {
        { 0,    0u, 30000u },   /* 0: owed only, the oldest deferral */
        { 1,  500u,     0u },   /* 1: marked, not owed */
        { 1, 1200u,   900u },   /* 2: marked, then deferred by the budget (owed) - still a marked group */
        { 0,    0u,  5000u },   /* 3: owed only */
        { 1, 1200u,     0u },   /* 4: marked as long ago as 2 - after it, the order they came in */
        { 0,    0u, 30000u } }; /* 5: owed only, as old as 0 - after it */
    /* (a group marked by a wake or a writer lift alone and not owed is { 0, 0, 0 }: after every owed one - checked below) */
    std::vector<coophb::PassKey> keys; std::vector<int> order;
    for (int i = 0; i < 6; ++i)
    {
        const unsigned int at = coophb::PassInsertAt(keys.empty() ? 0 : &keys[0], (unsigned int)keys.size(), c[i]);
        keys.insert(keys.begin() + at, c[i]); order.insert(order.begin() + at, i);
    }
    const int want[6] = { 2, 4, 1, 0, 5, 3 };
    std::string got;
    bool same = order.size() == 6;
    for (size_t i = 0; i < order.size(); ++i) { got += std::string(i != 0 ? "," : "") + UU((unsigned int)order[i]); if (same && order[i] != want[i]) same = false; }
    if (!same) Fail("t1084: the dirty pass's order must be marked groups oldest mark first, then owed-only oldest deferral first (want 2,4,1,0,5,3); got " + got);
    const coophb::PassKey markedOwed = { 1, 100u, 100u }, oldOwed = { 0, 0u, 60000u }, markedNew = { 1, 10u, 0u }, markedOld = { 1, 9000u, 0u };
    if (coophb::PassAhead(markedOwed, oldOwed) != 1 || coophb::PassAhead(oldOwed, markedOwed) != 0)
        Fail("t1084: a marked group the budget deferred goes ahead of an older owed-only group");
    if (coophb::PassAhead(markedOld, markedNew) != 1 || coophb::PassAhead(markedNew, markedOld) != 0) Fail("t1084: the oldest mark goes first");
    if (coophb::PassAhead(markedOld, markedOld) != 0 || coophb::PassAhead(oldOwed, oldOwed) != 0) Fail("t1084: equals keep the order they came in");
    if (coophb::PassInsertAt(0, 0u, oldOwed) != 0u) Fail("t1084: the first candidate goes at the front of an empty pass");
    const coophb::PassKey wakeOnly = { 0, 0u, 0u }, youngOwed = { 0, 0u, 100u };
    if (coophb::PassAhead(youngOwed, wakeOnly) != 1 || coophb::PassAhead(wakeOnly, youngOwed) != 0)
        Fail("t1084: a group only woken (or lifted) and not owed goes after every owed group");
    /* the debt's share: the first unchanged group in pass order goes first once every kDebtShareMs, if it waited kDebtMaxWaitMs */
    const coophb::PassKey overdue = { 0, 0u, coophb::kDebtMaxWaitMs }, notYet = { 0, 0u, coophb::kDebtMaxWaitMs - 1u };
    const coophb::PassKey p1[3] = { markedOld, markedNew, overdue };
    if (coophb::DebtShareAt(p1, 3u, coophb::kDebtShareMs) != 2u) Fail("t1084: an owed group that waited kDebtMaxWaitMs takes the share");
    if (coophb::DebtShareAt(p1, 3u, coophb::kDebtShareMs - 1u) != 3u) Fail("t1084: the share opens only once every kDebtShareMs");
    const coophb::PassKey p2[3] = { markedOld, notYet, overdue };
    if (coophb::DebtShareAt(p2, 3u, coophb::kDebtShareMs) != 3u) Fail("t1084: the share looks only at the oldest owed group (pass order), which has not waited long enough");
    const coophb::PassKey p3[2] = { markedOld, markedNew };
    if (coophb::DebtShareAt(p3, 2u, 0xFFFFFFFFu) != 2u || coophb::DebtShareAt(0, 0u, 0xFFFFFFFFu) != 0u) Fail("t1084: no owed group, no share");
    /* the sending order: changed and the rest take turns, a changed one first; the share ahead of all */
    {
        const coophb::PassKey p[5] = { { 1, 900u, 0u }, { 1, 500u, 0u }, { 1, 100u, 0u }, { 0, 0u, 9000u }, { 0, 0u, 0u } };
        unsigned int o[5];
        std::string g;
        if (coophb::PassOrder(p, 5u, 0u, o) != 5u) Fail("t1084: no share while none is due");
        for (int i = 0; i < 5; ++i) g += UU(o[i]);
        if (g != "03142") Fail("t1084: the changed groups and the rest take turns, a changed one first (want 03142); got " + g);
        const coophb::PassKey q[4] = { { 1, 900u, 0u }, { 0, 0u, coophb::kDebtMaxWaitMs + 5u }, { 0, 0u, 9000u }, { 0, 0u, 0u } };
        unsigned int o2[4];
        g.clear();
        if (coophb::PassOrder(q, 4u, coophb::kDebtShareMs, o2) != 1u) Fail("t1084: the overdue owed group is the share");
        for (int i = 0; i < 4; ++i) g += UU(o2[i]);
        if (g != "1023") Fail("t1084: the share goes ahead of all, then the turns without it (want 1023); got " + g);
        const coophb::PassKey r[2] = { { 0, 0u, 100u }, { 0, 0u, 0u } };
        unsigned int o3[2];
        g.clear();
        coophb::PassOrder(r, 2u, 0u, o3);
        for (int i = 0; i < 2; ++i) g += UU(o3[i]);
        if (g != "01") Fail("t1084: with no changed group the rest keep their order (want 01); got " + g);
    }
}
/* T-1084: a restock note (store.cpp SqEventMatch, squadevent.h RestockNames) names the tracked squad at its address, or the tracked
   squad whose active part is at its address - address compares only; a squad another game writes is never named */
void t_sqev_restock_names_the_squad_or_its_active_part()
{
    char squad[0x200], active[0x80], other[0x40];
    if (sqevent::RestockNames(0, squad, 1, active, squad) != 2) Fail("t1084: a restock note at the tracked squad's own address names it");
    if (sqevent::RestockNames(0, squad, 0, 0, squad) != 2) Fail("t1084: the squad's own address names it without reading its active part");
    if (sqevent::RestockNames(0, squad, 1, active, active) != 1) Fail("t1084: a restock note at the tracked squad's active part names it");
    if (sqevent::RestockNames(0, squad, 0, active, active) != 0) Fail("t1084: an active part that was not read names nothing");
    if (sqevent::RestockNames(1, squad, 1, active, squad) != 0 || sqevent::RestockNames(1, squad, 1, active, active) != 0)
        Fail("t1084: a squad another game writes is never named by a restock note");
    if (sqevent::RestockNames(0, squad, 1, active, other) != 0) Fail("t1084: an unrelated address names no squad");
    if (sqevent::RestockNames(0, squad, 1, 0, 0) != 0) Fail("t1084: a null note names no squad, even one with no active part");
    if (sqevent::NoteVerdict(sqevent::kKindRestock, 1, 0) != sqevent::kVerdictMark) Fail("t1084: a restock named outside the wake window marks");
}

void t_sqev_wake_window_and_note_verdict()
{
    /* a squad woke at clock 10 (window open): a note stamped 10..12 is inside it, one stamped 9 (before the wake) is not */
    if (sqevent::WakeWindowHolds(1, 10, 1, 0, 10) != 1 || sqevent::WakeWindowHolds(1, 10, 1, 0, 12) != 1) Fail("t1084: a note taken while the wake window is open is inside it");
    if (sqevent::WakeWindowHolds(1, 10, 1, 0, 9) != 0) Fail("t1084: a note taken before the wake is not inside its window");
    /* the window closed at clock 14 (its first visit): a queued note stamped 13 was taken inside it - judged by its stamp, not the drain's time */
    if (sqevent::WakeWindowHolds(1, 10, 0, 14, 13) != 1) Fail("t1084: a queued note taken before the window closed is judged by its stamp");
    if (sqevent::WakeWindowHolds(1, 10, 0, 14, 14) != 0 || sqevent::WakeWindowHolds(1, 10, 0, 14, 20) != 0) Fail("t1084: a note after the first visit is outside the window");
    if (sqevent::WakeWindowHolds(0, 0, 0, 0, 5) != 0) Fail("t1084: a squad that never woke here has no window");
    if (sqevent::WakeWindowHolds(1, 0xFFFE, 0, 2, 0xFFFF) != 1 || sqevent::WakeWindowHolds(1, 0xFFFE, 0, 2, 1) != 1 || sqevent::WakeWindowHolds(1, 0xFFFE, 0, 2, 2) != 0)
        Fail("t1084: the 16-bit clock wraps without changing the answer");
    /* SqEventMatch's verdict (NoteVerdict): a home or a restock inside the wake window is the read - no mark, so the wake's skip of a
       just-loaded record holds; a death or money is always a change; another game's squad, or none, is never marked */
    if (sqevent::NoteVerdict(sqevent::kKindPotHome, 1, 1) != sqevent::kVerdictWakeRead || sqevent::NoteVerdict(sqevent::kKindRestock, 1, 1) != sqevent::kVerdictWakeRead)
        Fail("t1084: a home or a restock inside the wake window takes no mark");
    if (sqevent::NoteVerdict(sqevent::kKindPotHome, 1, 0) != sqevent::kVerdictMark || sqevent::NoteVerdict(sqevent::kKindRestock, 1, 0) != sqevent::kVerdictMark)
        Fail("t1084: a home or a restock outside the wake window marks");
    if (sqevent::NoteVerdict(sqevent::kKindCharacter, 1, 1) != sqevent::kVerdictMark || sqevent::NoteVerdict(sqevent::kKindPotMoney, 1, 1) != sqevent::kVerdictMark)
        Fail("t1084: a death or money inside the wake window still marks");
    if (sqevent::NoteVerdict(sqevent::kKindPotHome, 0, 0) != sqevent::kVerdictNotOurs) Fail("t1084: a squad this game does not write takes no mark");
    /* the wake-only skip survives a home read in at the wake: through the real window, verdict and mark routing, the entry keeps dirty
       without dirtyRecord, so HbDue (DueForVisit) treats it as a wake alone */
    int dirty = 0, dirtyRecord = 0;
    sqevent::WakeWindow w; sqevent::WindowReset(&w);
    sqevent::MarkEntry(1, 0, 1, &dirty, &dirtyRecord);   /* the wake mark */
    sqevent::WindowOpen(&w, 40);
    if (sqevent::NoteVerdict(sqevent::kKindPotHome, 1, sqevent::WindowHolds(&w, 41)) == sqevent::kVerdictMark) sqevent::MarkEntry(1, 0, 0, &dirty, &dirtyRecord);
    if (dirty != 1 || dirtyRecord != 0) Fail("t1084: a home read inside the wake leaves the wake's mark a wake alone");
    if (sqevent::DueForVisit(true, 0, dirty, dirtyRecord, 0, 0, 2000, 300000u)) Fail("t1084: so a giant woken and read in still waits as a wake alone");
}

void t_sqev_window_closes_on_people_and_ends_after_drain()
{
    /* while the wake's people wait runs: no visit, no prune, the window stays open */
    if (sqevent::VisitHeldForPeople(1) != 1 || sqevent::PruneKeeps(1) != 1 || sqevent::WindowClosesAtVisit(1) != 0)
        Fail("t1084: a squad whose people are still coming is not visited, not pruned, and its window is not closed by a visit");
    if (sqevent::VisitHeldForPeople(0) != 0 || sqevent::PruneKeeps(0) != 0 || sqevent::WindowClosesAtVisit(0) != 1)
        Fail("t1084: with no people wait a visit runs, a people-less squad is pruned, and the first visit closes the window");
    sqevent::WakeWindow w; sqevent::WindowReset(&w);
    if (sqevent::WindowClose(&w, 5) != 0) Fail("t1084: a squad that never woke has no window to close");
    sqevent::WindowOpen(&w, 100);
    if (sqevent::WindowHolds(&w, 160) != 1) Fail("t1084: the window holds while the people are still coming");
    if (sqevent::WindowClose(&w, 170) != 1 || sqevent::WindowClose(&w, 171) != 0) Fail("t1084: the people seen close the window once");
    if (sqevent::WindowHolds(&w, 169) != 1 || sqevent::WindowHolds(&w, 170) != 0) Fail("t1084: a closed window still judges a note queued before it closed");
    sqevent::WindowAfterDrain(&w);
    if (w.has != 0 || sqevent::WindowHolds(&w, 169) != 0) Fail("t1084: after the next drain the window is gone");
    if (sqevent::WindowHolds(&w, (100 + 0x10000u + 5u) & sqevent::kStampMask) != 0) Fail("t1084: a note 65,536 ticks later is never judged inside an old window");
}

void t_sqev_ring_reserves_before_it_fills()
{
    if (sqevent::ReserveAdmits(sqevent::kKindPotMoney, sqevent::kRingCap / 2) != 1 || sqevent::ReserveAdmits(sqevent::kKindPotMoney, sqevent::kRingCap / 2 + 1) != 0)
        Fail("t1084: a money note keeps its reservation up to exactly half the ring");
    if (sqevent::ReserveAdmits(sqevent::kKindCharacter, sqevent::kRingCap) != 1 || sqevent::ReserveAdmits(sqevent::kKindCharacter, sqevent::kRingCap + 1) != 0)
        Fail("t1084: any note keeps its reservation up to the whole ring, never past it");
    /* reservations already counted (pushes in flight on other threads) hold money back as filled slots do */
    sqevent::EventRing r; int i;
    for (i = 0; i < sqevent::kRingCap; ++i) r.slot[i] = 0;
    r.used = sqevent::kRingCap / 2; r.next = 0;
    long long obj = 0;
    if (sqevent::RingPush(&r, &obj, sqevent::kKindPotMoney) != sqevent::kPushHalfRefused || r.used != sqevent::kRingCap / 2)
        Fail("t1084: a refused reservation is backed out");
    if (sqevent::RingPush(&r, &obj, sqevent::kKindPotHome) != sqevent::kPushQueued || r.used != sqevent::kRingCap / 2 + 1)
        Fail("t1084: a home note reserves and fills a slot past the money half");
}

void t_sqev_ring_bounds_money_notes()
{
    if (sqevent::ReserveAdmits(sqevent::kKindPotMoney, sqevent::kRingCap / 2 + 1) != 0) Fail("t1084: a money note takes no slot once the ring is half full");
    if (sqevent::ReserveAdmits(sqevent::kKindPotMoney, sqevent::kRingCap / 2) != 1) Fail("t1084: a money note takes a slot below half");
    if (sqevent::ReserveAdmits(sqevent::kKindCharacter, sqevent::kRingCap) != 1 || sqevent::ReserveAdmits(sqevent::kKindPotHome, sqevent::kRingCap) != 1)
        Fail("t1084: deaths and homes keep the other half");
    sqevent::EventRing r; int i;
    for (i = 0; i < sqevent::kRingCap; ++i) r.slot[i] = 0;
    r.used = 0; r.next = 0;
    long long obj[2] = { 0, 0 };
    int queued = 0, refused = 0;
    for (i = 0; i < sqevent::kRingCap; ++i)
    {
        const int pr = sqevent::RingPush(&r, sqevent::TagAddr(&obj[0], sqevent::kKindPotMoney, (unsigned int)i), sqevent::kKindPotMoney);
        if (pr == sqevent::kPushQueued) ++queued; else if (pr == sqevent::kPushHalfRefused) ++refused;
    }
    if (queued != sqevent::kRingCap / 2 || refused != sqevent::kRingCap / 2) Fail("t1084: a flood of money notes fills half the ring, the rest are refused and counted");
    if (sqevent::RingPush(&r, sqevent::TagAddr(&obj[1], sqevent::kKindCharacter, 0), sqevent::kKindCharacter) != sqevent::kPushQueued)
        Fail("t1084: a death still finds a slot after a money flood");
    void* got[sqevent::kRingCap];
    if (sqevent::RingTake(&r, got, sqevent::kRingCap) != sqevent::kRingCap / 2 + 1 || r.used != 0) Fail("t1084: the take empties the ring and its count");
}

void t_sqev_off_thread_event_is_queued_not_applied()
{
    if (sqevent::NoteAction(0, 0, 0) != sqevent::kActQueue || sqevent::NoteAction(0, 1, 1) != sqevent::kActQueue)
        Fail("t1084: an event off the main thread is queued whatever the registry - which it does not read");
    if (sqevent::DrainUses(0, 0) != 1 || sqevent::DrainUses(1, 0) != 0 || sqevent::DrainUses(0, 1) != 0)
        Fail("t1084: the drain matches only with a world up and a squad tracked; otherwise it drops (counted)");
    sqevent::EventRing r; int i;
    for (i = 0; i < sqevent::kRingCap; ++i) r.slot[i] = 0;
    r.used = 0; r.next = 0;
    long long chars[3] = { 0, 0, 0 };   /* 8-byte aligned, as the engine's objects are */
    SqevEntry e = SqevFresh(17000u);
    if (sqevent::RingPush(&r, sqevent::TagAddr(&chars[1], sqevent::kKindPotHome, 0x1234u), sqevent::kKindPotHome) != sqevent::kPushQueued) Fail("t1084: an empty ring takes the tagged address");
    if (e.dirty != 0) Fail("t1084: queueing marks nothing - the squad map is the main thread's");
    void* got[sqevent::kRingCap];
    const int n = sqevent::RingTake(&r, got, sqevent::kRingCap);
    int kind = -1; unsigned int stamp = 0;
    if (n != 1 || sqevent::UntagAddr(got[0], &kind, &stamp) != (void*)&chars[1] || kind != sqevent::kKindPotHome || stamp != 0x1234u)
        Fail("t1084: the main thread takes the address back with its kind and the stamp it was taken at");
    if (sqevent::TagAddr((const char*)&chars[1] + 1, sqevent::kKindCharacter, 0) != 0) Fail("t1084: an unaligned address is not queued");
    if (sqevent::TagAddr(&chars[1], sqevent::kKindCount, 0) != 0) Fail("t1084: an unknown kind is not queued");
    if (sqevent::RingTake(&r, got, sqevent::kRingCap) != 0) Fail("t1084: a taken ring is empty");
    for (i = 0; i < sqevent::kRingCap; ++i) if (sqevent::RingPush(&r, &chars[i % 3], sqevent::kKindCharacter) != sqevent::kPushQueued) Fail("t1084: the ring holds kRingCap addresses");
    if (sqevent::RingPush(&r, &chars[0], sqevent::kKindCharacter) != sqevent::kPushFull) Fail("t1084: a full ring refuses (counted by the caller), it never overwrites");
    if (sqevent::RingTake(&r, got, 10) != 10 || sqevent::RingTake(&r, got, sqevent::kRingCap) != sqevent::kRingCap - 10)
        Fail("t1084: a take bounded by its room leaves the rest for the next take");
}


/* inv7c fold (review-inv7c): a live row relinked at link-up, the quiet clock paused while down, done-before-relinked ordering,
   three refusals -> the ground, the taker list's eviction. */
void t_inv7c_fold()
{
    using namespace coopesc;
    if (RelinkOnUp(0, 1, 0) != 1) Fail("inv7c fold: a LIVE take row with its ok CONFIRM out is relinked at a link-up");
    if (RelinkOnUp(0, 0, 0) != 0 || RelinkOnUp(1, 1, 0) != 0) Fail("inv7c fold: unconfirmed and give rows are not relinked");   /* p105g-f2-1: a ground row is - t_p105g_f2_ground_escrow */
    unsigned int q = 0;
    q = QuietStep(q, 1000, 1, kQuietDtCapMs);
    q = QuietStep(q, 1000, 0, kQuietDtCapMs);
    q = QuietStep(q, 900000, 0, kQuietDtCapMs);
    if (q != 1000) Fail("inv7c fold: the quiet clock does not move while the link is down");
    q = QuietStep(q, 60000, 1, kQuietDtCapMs);
    if (q != 1000 + kQuietDtCapMs) Fail("inv7c fold: a long frame gap counts only the cap");
    if (QuietStep(0xFFFFFFF0u, 100, 1, kQuietDtCapMs) != 0xFFFFFFFFu) Fail("inv7c fold: the quiet clock saturates");
    {
        unsigned int w = 0; int k;
        for (k = 0; k < 400; ++k) w = QuietStep(w, 1000, 0, kQuietDtCapMs);
        if (ExpireAction(0, 1, 1, 0, w, 10000, kEscrowQuietMs) != kExpWait) Fail("inv7c fold: an escrow held through a long link-down is not returned by the clock");
        for (k = 0; k < 300; ++k) w = QuietStep(w, 1000, 1, kQuietDtCapMs);
        if (ExpireAction(0, 1, 1, 0, w, 10000, kEscrowQuietMs) != kExpReturn) Fail("inv7c fold: 5 linked minutes of silence return it");
    }
    if (PlacedMissAction(1, 1, 1) != kMissDup) Fail("inv7c fold: an already-settled id wins over a relinked row (ids restart per process)");
    if (PlacedMissAction(0, 1, 1) != kMissAfterReturn) Fail("inv7c fold: a handed-back id is placedAfterReturn, before any relinked row");
    if (PlacedMissAction(0, 0, 1) != kMissRelinked) Fail("inv7c fold: a relinked row matches by id");
    if (PlacedMissAction(0, 0, 0) != kMissUnknown) Fail("inv7c fold: nothing -> unknown");
    if (RefusalAction(1) != kRfKeep || RefusalAction(2) != kRfKeep) Fail("inv7c fold: the first two refusals keep the row in escrow");
    if (RefusalAction(3) != kRfGround || RefusalAction(4) != kRfGround) Fail("inv7c fold: the third refusal puts the item on the ground");
    {
        TakerList tl;
        int k;
        for (k = 0; k < kTakerCap; ++k) tl.Note(200 + (unsigned int)k, 1, (k == 5) ? 1 : 0, (k == 5) ? 7 : 6, 1000 + (unsigned int)k);
        if (tl.Note(999, 1, 0, 7, 5000) != 1 || tl.Find(205) >= 0 || tl.Find(200) < 0) Fail("inv7c fold: a full list evicts the entry handed to the current link first, not the oldest unsent");
        TakerList t2;
        for (k = 0; k < kTakerCap; ++k) t2.Note(300 + (unsigned int)k, 1, 0, 6, 1000 + (unsigned int)k);
        if (t2.Note(998, 1, 0, 7, 5000) != 1 || t2.Find(300) >= 0) Fail("inv7c fold: with nothing handed over, the oldest entry is evicted");
    }
}

/* inv7c fold 2 (recheck-inv7c): the session-ready flag and its link generation. */
void t_inv7c_fold2()
{
    using namespace coopesc;
    if (ReadyFlagAction(7, 7, 1) != kRdAccept) Fail("inv7c fold 2: a flag stamped on this up link is accepted");
    if (ReadyFlagAction(6, 7, 1) != kRdAccept) Fail("inv7c fold 2: a HELLO dispatched just before the up edge is accepted");
    if (ReadyFlagAction(5, 7, 1) != kRdStale) Fail("inv7c fold 2: a flag from an older link is ignored");
    if (ReadyFlagAction(7, 7, 0) != kRdStale) Fail("inv7c fold 2: a flag taken while the link is down is ignored");
}

/* inv7d: the owner's arrival asks for its storage (writerladder.h OwnerArrive*) */
void t_inv7d_owner_arrive()
{
    using namespace coopwriter;
    /* WHEN TO ASK: a clear bit never asks; a new stamp asks once the sector has settled; the same stamp never asks twice */
    if (OwnerArriveEdge(0.0, 0.0, 9) != kArriveNone)     Fail("a clear bit must not sweep");
    if (OwnerArriveEdge(-1.0, 0.0, 9) != kArriveNone)    Fail("a negative stamp must not sweep");
    if (OwnerArriveEdge(5.0, 0.0, 1) != kArriveWait)     Fail("a new stamp on an unsettled sector must wait");
    if (OwnerArriveEdge(5.0, 0.0, kArriveSettlePolls) != kArriveSweep) Fail("a new stamp on a settled sector must sweep");
    if (OwnerArriveEdge(5.0, 5.0, 9) != kArriveNone)     Fail("a stamp already swept must not sweep again (ask ONCE)");
    if (OwnerArriveEdge(7.0, 5.0, 9) != kArriveSweep)    Fail("a later arrival (new stamp) must sweep again");
    /* THE END: no clock - the answer, the previous writer gone from the area, or a link loss; an end with no answer is said */
    int to = -1;
    if (OwnerArriveWait(0, 0, 0, &to) != 0 || to != kArriveEndNone)     Fail("an asked hand-back waits - no clock ends it");
    if (OwnerArriveWait(2, 0, 0, &to) != 0 || to != kArriveEndNone)     Fail("contents on the way wait");
    if (OwnerArriveWait(0, 1, 0, &to) != 1 || to != kArriveEndGone)     Fail("the previous writer gone ends the wait, and says so");
    if (OwnerArriveWait(2, 0, 1, &to) != 1 || to != kArriveEndLink)     Fail("a link loss ends the wait, and says so");
    if (OwnerArriveWait(0, 1, 1, &to) != 1 || to != kArriveEndLink)     Fail("a link loss is named first");
    if (OwnerArriveWait(1, 1, 1, &to) != 1 || to != kArriveEndNone)     Fail("a done hand-back writes and is never an end with no answer");
    if (OwnerArriveWait(1, 0, 0, 0) != 1)                               Fail("a null pointer must be accepted");
    if (HandbackWriterGone(3, 1, 3, 1) != 0)  Fail("the holder at the ask still holds the area: wait");
    if (HandbackWriterGone(3, 0, 3, 1) != 1)  Fail("the holder at the ask no longer has the area loaded: gone");
    if (HandbackWriterGone(3, 1, 4, 1) != 1)  Fail("another game holds the area now: the previous writer answers no contents - gone");
    if (HandbackWriterGone(3, -1, -1, -1) != 0) Fail("no fresh map is never gone");
    if (HandbackWriterGone(-1, -1, -1, 0) != 1) Fail("no holder at the ask and no other game with the area loaded: gone");
    if (HandbackWriterGone(-1, -1, -1, 1) != 0 || HandbackWriterGone(-1, -1, -1, -1) != 0) Fail("no holder at the ask, another game loaded (or no map): wait");
    /* APPLYING THE ANSWER VS THE OWNER'S STALE COPY: only while the hand-back waits */
    if (OwnerArriveApplies(0) != 1) Fail("an answer to an asked hand-back replaces the owner's copy");
    if (OwnerArriveApplies(2) != 1) Fail("contents on the way replace the owner's copy");
    if (OwnerArriveApplies(1) != 0) Fail("a done hand-back, or one ended with no answer, keeps the owner's copy (a late answer)");
    /* THE SEQUENCE with inv4b's start rule: a real arrival on a NEW live building after a done hand-back asks again */
    if (coopown::HandbackStart(7.0, 0, 1, 0) != coopown::kHbAsk) Fail("a re-arrival on a new building must ask");
    /* inv7d fold (review-inv7d MED): SWAP ONLY OVER THE BOX THAT WAS ASKED ABOUT, else re-ask */
    if (OwnerArriveSwapOrReask(2, 1, 0xAAu, 1, 0xAAu) != kArriveSwapIt) Fail("an unchanged box takes the answer");
    if (OwnerArriveSwapOrReask(2, 1, 0xAAu, 1, 0xABu) != kArriveReask)  Fail("a box changed since the ask must re-ask, not swap");
    if (OwnerArriveSwapOrReask(2, 0, 0xAAu, 1, 0xABu) != kArriveSwapIt) Fail("no recorded ask digest swaps as before");
    if (OwnerArriveSwapOrReask(2, 1, 0xAAu, 0, 0xABu) != kArriveSwapIt) Fail("an unreadable live box swaps as before");
    if (OwnerArriveSwapOrReask(0, 1, 0xAAu, 1, 0xABu) != kArriveSwapIt) Fail("not a waiting contents answer: not this rule's");
    if (OwnerArriveSwapOrReask(1, 1, 0xAAu, 1, 0xABu) != kArriveSwapIt) Fail("a done hand-back: not this rule's");
}

/* items9 (item-stats-parity.md gaps 1-3, 4): ITEM_MOVE op 4's charges tail, op 0's [BAG1][OWN1] tail, the snapshot detail stats
   text and `itemtest set`'s arguments. */
void t_items9_stat_wire()
{
    std::vector<char> b(5, 'h');   /* stands for the message head */
    if (!coopistat::EncodeChargesTail(&b, 2.5f, "medkit-sid")) { Fail("charges tail did not encode"); return; }
    float ch = -1.0f; std::string sid; size_t end = 0;
    if (coopistat::DecodeChargesTail(&b[0], b.size(), 5, &ch, &sid, &end) != coopistat::kTailOk || ch != 2.5f || sid != "medkit-sid"
        || end != b.size()) Fail("the charges tail did not round-trip");
    for (size_t cut = 5; cut < b.size(); ++cut)
        if (coopistat::DecodeChargesTail(&b[0], cut, 5, &ch, &sid, &end) != coopistat::kTailCut) { Fail("a cut charges tail must be refused"); break; }
    std::vector<char> t2(b); t2.push_back('z');
    if (coopistat::DecodeChargesTail(&t2[0], t2.size(), 5, &ch, &sid, &end) != coopistat::kTailTrailing) Fail("bytes after op 4's tail must be refused");
    std::vector<char> nb;
    if (coopistat::EncodeChargesTail(&nb, -1.0f, "x") || coopistat::EncodeChargesTail(&nb, 1.0f, "") || !nb.empty())
        Fail("negative charges / an empty sid must not encode");
    float nan = 0.0f; { const unsigned int u = 0x7FC00000u; std::memcpy(&nan, &u, 4); }
    if (coopistat::EncodeChargesTail(&nb, nan, "x") || !nb.empty()) Fail("NaN charges must not encode");
    {
        std::vector<char> hb; coopbag::BagPutU32(&hb, 0x7FC00000u); coopbag::BagPutStr(&hb, "x");
        if (coopistat::DecodeChargesTail(&hb[0], hb.size(), 0, &ch, &sid, &end) != coopistat::kTailBadValue) Fail("a NaN on the wire must be refused");
    }
    /* op 0's tail: nothing / rows only / mark only / both, in that order; cut at every length; junk refused */
    std::vector<coopbag::BagRow> rows(1);
    rows[0].section = "backpack_content"; rows[0].qty = 2; rows[0].baseSid = "food"; rows[0].charges = 3.0f; rows[0].quality = 40.0f;
    rows[0].owner.kind = 2; rows[0].owner.uid = 9; rows[0].owner.factionSid = "12-gamedata.base";
    coopmark::OwnerId own; own.kind = 1; own.uid = 77; own.factionSid = "204-gamedata.base";
    const coopmark::OwnerId clean;
    const std::vector<coopbag::BagRow> none;
    for (int shape = 0; shape < 4; ++shape)
    {
        std::vector<char> t(3, 'k');   /* stands for the box key */
        int bs = 9, os = 9;
        coopistat::EncodeAddTail(&t, (shape & 1) ? rows : none, (shape & 2) ? own : clean, &bs, &os);
        if (bs != ((shape & 1) ? 1 : 0) || os != ((shape & 2) ? 1 : 0)) { Fail("the add tail's sent flags are wrong"); continue; }
        std::vector<coopbag::BagRow> gr; coopmark::OwnerId go; size_t e2 = 0;
        const int r = coopistat::DecodeAddTail(t.empty() ? 0 : &t[0], t.size(), 3, &gr, &go, &e2);
        if (r != coopistat::kTailOk || e2 != t.size()) { Fail("an add tail did not decode"); continue; }
        if ((shape & 1) && (gr.size() != 1 || gr[0].baseSid != "food" || gr[0].qty != 2 || gr[0].charges != 3.0f || gr[0].quality != 40.0f
                            || gr[0].owner.kind != 2 || gr[0].owner.uid != 9 || gr[0].owner.factionSid != "12-gamedata.base"))
            Fail("the add tail's pack rows (and their marks) did not round-trip");
        if (!(shape & 1) && !gr.empty()) Fail("no rows sent must read as no rows");
        if ((shape & 2) && !coopmark::OwnerEqual(go, own)) Fail("the add tail's stolen mark did not round-trip");
        if (!(shape & 2) && go.kind != 0) Fail("no mark sent must read clean");
        if (shape == 3)
        {
            /* the only prefixes that are whole messages: the rows with their OWNL tail, and the rows without it */
            std::vector<char> t1(3, 'k'), tu(3, 'k'); int b1 = 0, o1 = 0;
            coopistat::EncodeAddTail(&t1, rows, clean, &b1, &o1);
            std::vector<coopbag::BagRow> ur(rows); ur[0].owner = clean;
            coopistat::EncodeAddTail(&tu, ur, clean, &b1, &o1);
            for (size_t cut = 4; cut < t.size(); ++cut)
                if (cut != t1.size() && cut != tu.size() && coopistat::DecodeAddTail(&t[0], cut, 3, &gr, &go, &e2) == coopistat::kTailOk)
                { Fail("a cut add tail must be refused"); break; }
            std::vector<char> j(t); j.push_back('j');
            if (coopistat::DecodeAddTail(&j[0], j.size(), 3, &gr, &go, &e2) != coopistat::kTailTrailing) Fail("junk after the add tail must be refused");
        }
    }
    {
        std::vector<char> t(3, 'k'); t.push_back('j'); t.push_back('j'); t.push_back('j'); t.push_back('j');
        std::vector<coopbag::BagRow> gr; coopmark::OwnerId go; size_t e2 = 0;
        if (coopistat::DecodeAddTail(&t[0], t.size(), 3, &gr, &go, &e2) != coopistat::kTailBadBag) Fail("junk where a block should be must be refused");
    }
    {   /* an over-cap pack: no rows written, the item still travels (flag -1) */
        std::vector<coopbag::BagRow> big(coopbag::kBagMaxRows + 1, rows[0]);
        std::vector<char> t; int bs = 0, os = 0;
        coopistat::EncodeAddTail(&t, big, clean, &bs, &os);
        if (bs != -1 || !t.empty()) Fail("an over-cap pack must write no rows and say so");
    }
    /* snapshot detail stats */
    const std::string d0 = coopistat::DetailStats(90.0f, 1.5f, "912-gamedata.base", "", "c1", 3, 0, 1, clean);
    if (d0 != " q=90.00 ch=1.50 co=912-gamedata.base mat=- col=c1 lvl=3 fn=0 uq=1 stolen=0") Fail("detail stats text (clean): " + d0);
    const std::string d1 = coopistat::DetailStats(5.0f, 0.0f, "", "", "", 0, 2, 0, own);
    if (d1 != " q=5.00 ch=0.00 co=- mat=- col=- lvl=0 fn=2 uq=0 stolen=1:77:204-gamedata.base") Fail("detail stats text (marked): " + d1);
    /* itemtest set's arguments */
    coopistat::SetArgs sa; std::string err;
    if (coopistat::ParseSetArgs("any q=90", &sa, &err) != 1 || sa.ref != "any" || sa.haveQ != 1 || sa.q != 90.0f || sa.haveCh != 0 || sa.isSlot != 0)
        Fail("itemtest set any q=90 did not parse");
    if (coopistat::ParseSetArgs("main:2,3 ch=1.5 q=40", &sa, &err) != 1 || sa.isSlot != 1 || sa.section != "main" || sa.x != 2 || sa.y != 3
        || sa.ch != 1.5f || sa.q != 40.0f) Fail("itemtest set <slot> ch= q= did not parse");
    if (coopistat::ParseSetArgs("sid-1  ch=2", &sa, &err) != 1 || sa.ref != "sid-1" || sa.haveCh != 1 || sa.ch != 2.0f) Fail("a sid ref did not parse");
    const char* bad[] = { "", "any", "any q=101", "any q=-1", "any ch=-2", "any q=5 q=6", "any z=1", "main:x,1 q=1", ":1,2 q=1",
                          "main:1 q=1", "any q=nan", "any q=1 ch=2 q=3", "any q=5x" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (coopistat::ParseSetArgs(bad[i], &sa, &err) != 0) Fail(std::string("itemtest set must refuse '") + bad[i] + "'");
}
/* par24 (parity P24): the world-vs-world table - who wins, what is refused, the wire and the file line */
void t_par24_world_relations_table()
{
    using namespace coopwrel;
    Table t;
    Row r; r.relation = -10.0f; r.trust = 2.5f; r.trustNeg = 1.0f; r.flags = 2;
    /* SEED: first writer wins; CHANGE: the latest wins; the same row is no change */
    if (Merge(&t, "200-gamedata.base", "1083-gamedata.base", r, kWrSeed) != 1 || t.size() != 1) Fail("a seed for a pair the table lacks is taken");
    Row r2 = r; r2.relation = 40.0f;
    if (Merge(&t, "200-gamedata.base", "1083-gamedata.base", r2, kWrSeed) != 0 || t[Key("200-gamedata.base", "1083-gamedata.base")].relation != -10.0f) Fail("a second seed for a held pair is NOT taken (first writer wins)");
    if (Merge(&t, "200-gamedata.base", "1083-gamedata.base", r2, kWrChange) != 1 || t[Key("200-gamedata.base", "1083-gamedata.base")].relation != 40.0f) Fail("a change replaces the held row (latest wins)");
    if (Merge(&t, "200-gamedata.base", "1083-gamedata.base", r2, kWrChange) != 0) Fail("an identical change is no change (nothing re-sent)");
    if (Merge(&t, "200-gamedata.base", "1083-gamedata.base", r, kWrRows) != 1 || t[Key("200-gamedata.base", "1083-gamedata.base")].relation != -10.0f) Fail("a notebook row replaces this game's copy");
    if (Merge(&t, "1083-gamedata.base", "200-gamedata.base", r, kWrSeed) != 1 || t.size() != 2) Fail("the two directions are two rows");
    /* refusals: a player / stand-in wire name, the same faction, a separator, NaN, a bad flag, an unknown kind (decision 20: players never become rows) */
    if (Merge(&t, "@slot:1:Me", "200-gamedata.base", r, kWrChange) != -1) Fail("a player's slot name is never a world row");
    if (Merge(&t, "200-gamedata.base", "@slot:2", r, kWrSeed) != -1) Fail("a stand-in's slot name is never a world row");
    if (Merge(&t, "200-gamedata.base", "200-gamedata.base", r, kWrSeed) != -1) Fail("a faction against itself is refused");
    if (Merge(&t, "a|b", "200-gamedata.base", r, kWrSeed) != -1) Fail("the key separator in a sid is refused");
    Row bad = r; bad.relation = std::numeric_limits<float>::quiet_NaN();
    if (Merge(&t, "1-gamedata.base", "2-gamedata.base", bad, kWrSeed) != -1) Fail("a NaN relation is refused");
    bad = r; bad.flags = 4;
    if (Merge(&t, "1-gamedata.base", "2-gamedata.base", bad, kWrSeed) != -1) Fail("a flag above 3 is refused");
    if (Merge(&t, "1-gamedata.base", "2-gamedata.base", r, 7) != -1) Fail("an unknown kind is refused");
    if (t.size() != 2) Fail("nothing refused reached the table");
    /* the table's cap: a NEW pair past it is refused, a held pair still changes */
    {
        Table big;
        for (unsigned i = 0; i < kWrMaxTable; ++i) { char a[32]; std::sprintf(a, "%u-x.base", i); big[Key(a, "z-x.base")] = r; }
        if (Merge(&big, "new-x.base", "z-x.base", r, kWrSeed) != -1) Fail("a new pair past the cap is refused");
        if (Merge(&big, "5-x.base", "z-x.base", r2, kWrChange) != 1) Fail("a held pair still changes at the cap");
    }
    /* the wire: round trip, a short / trailing payload refused whole, the counts */
    std::vector<WireRow> rows; WireRow w;
    w.a = "200-gamedata.base"; w.b = "1083-gamedata.base"; w.row = r; rows.push_back(w);
    w.a = "96156-rebirth.mod"; w.b = "1085-gamedata.base"; w.row = r2; w.row.flags = 1; rows.push_back(w);
    std::vector<char> m; int kind = 0; std::vector<WireRow> back;
    if (!Encode(&m, kWrChange, rows)) Fail("encode");
    if (m.empty() || Decode(&m[0], m.size(), &kind, &back) != 1 || kind != kWrChange || back.size() != 2 || back[1].a != "96156-rebirth.mod"
        || back[1].b != "1085-gamedata.base" || back[1].row.relation != 40.0f || back[1].row.flags != 1 || back[0].row.trust != 2.5f || back[0].row.trustNeg != 1.0f) Fail("round trip");
    if (Decode(&m[0], m.size() - 1, &kind, &back) != 0 || !back.empty()) Fail("a short payload is refused whole");
    std::vector<char> m2(m); m2.push_back(0);
    if (Decode(&m2[0], m2.size(), &kind, &back) != 0) Fail("a trailing byte is refused");
    std::vector<WireRow> none;
    if (Encode(&m, kWrRows, none)) Fail("an empty list is not a message");
    std::vector<WireRow> many(kWrMaxRows + 1, rows[0]);
    if (Encode(&m, kWrSeed, many)) Fail("more than 256 rows is not one message");
    std::vector<WireRow> most(kWrMaxRows, rows[0]);
    if (!Encode(&m, kWrSeed, most) || m.size() > 20000) Fail("256 rows of the usual stringIDs fit one ~15 KB message");
    std::vector<WireRow> player(1, rows[0]); player[0].a = "@slot:0:Me";
    if (Encode(&m, kWrChange, player)) Fail("a player's row does not encode");
    /* the file line: round trip exact (%.9g), and junk refused */
    Table f;
    if (ParseLine(Line("200-gamedata.base", "1083-gamedata.base", r), &f) != 1 || f.size() != 1) Fail("a file line reads back");
    const Row& fr = f[Key("200-gamedata.base", "1083-gamedata.base")];
    if (!SameRow(fr, r)) Fail("a file line reads back EXACTLY");
    Row odd; odd.relation = -33.333333f; odd.trust = 0.1f; odd.trustNeg = 1e-7f; odd.flags = 3;
    Table g;
    if (ParseLine(Line("1-a.base", "2-a.base", odd) + "\r", &g) != 1 || !SameRow(g[Key("1-a.base", "2-a.base")], odd)) Fail("an awkward float survives the file");
    if (ParseLine("v1\ta\tb\t1\t2\t3", &g) != 0) Fail("a short line is refused");
    if (ParseLine("v1\ta\tb\t1\t2\t3\t9", &g) != 0) Fail("a bad flag in the file is refused");
    if (ParseLine("v2\ta\tb\t1\t2\t3\t0", &g) != 0) Fail("another version is refused");
    if (ParseLine("v1\t@slot:1\tb\t1\t2\t3\t0", &g) != 0) Fail("a player's name in the file is refused");
    /* the key splits back */
    std::string ka, kb;
    if (!SplitKey(Key("200-gamedata.base", "1083-gamedata.base"), &ka, &kb) || ka != "200-gamedata.base" || kb != "1083-gamedata.base") Fail("the key splits back");
    /* the notebook keeps the world_relations.txt repair copy */
    const std::vector<std::string> rf = restoreguard::RepairCopyFiles();
    if (std::find(rf.begin(), rf.end(), std::string("world_relations.txt")) == rf.end()) Fail("the repair copy carries world_relations.txt");
}
/* par24 fold (review-par24 #1 #2, S2-62): a stand-in's record id is never a row; the send book; ECHO / SEED_END; the operator's seed */
void t_par24_fold_standins_echo_operator_seed()
{
    using namespace coopwrel;
    const std::string npc("200-gamedata.base"), npc2("1083-gamedata.base");
    Table t; Row r; r.relation = 5.0f;
    /* #1: an offline player's stand-in (coop-p<n>) and protocol 67's coop-peer are never world rows - at the merge, the wire, the file */
    if (SidOk("coop-p2") || SidOk("coop-p13") || SidOk("coop-peer") || !SidOk(npc)) Fail("a stand-in's record id is not a world sid");
    if (Merge(&t, npc, "coop-p2", r, kWrChange) != -1 || Merge(&t, "coop-p2", npc, r, kWrSeed) != -1 || Merge(&t, "coop-p2", "coop-p3", r, kWrChange) != -1
        || Merge(&t, "coop-peer", npc, r, kWrSeed) != -1 || Merge(&t, npc, "coop-p2", r, kWrRows) != -1 || !t.empty())
        Fail("npc|coop-p2, coop-p2|npc, coop-p2|coop-p3 and coop-peer never enter the table");
    std::vector<WireRow> v(1); v[0].a = npc; v[0].b = "coop-p2"; v[0].row = r;
    std::vector<char> m; int k = 0; std::vector<WireRow> back;
    if (Encode(&m, kWrChange, v)) Fail("a stand-in row does not encode");
    v[0].b = npc2;
    if (!Encode(&m, kWrChange, v)) Fail("encode an ordinary row");
    {
        const std::string st("coop-p0000000001xy");   /* the same length as npc2: a hand-built stand-in row on the wire */
        const std::string sm(m.begin(), m.end()); const size_t at = sm.find(npc2);
        if (at == std::string::npos || st.size() != npc2.size()) Fail("test setup");
        else { std::memcpy(&m[at], st.data(), st.size()); if (Decode(&m[0], m.size(), &k, &back) != 0) Fail("a stand-in row on the wire is refused (the notebook never takes it)"); }
    }
    Table f; int si = 0;
    if (ParseLine("v1\tcoop-p2\t200-gamedata.base\t-10\t0\t0\t0", &f, &si) != 0 || si != 1 || !f.empty()) Fail("a stand-in row in world_relations.txt is dropped and counted");
    si = 0;
    if (ParseLine("v1\t200-gamedata.base\tcoop-peer\t-10\t0\t0\t0", &f, &si) != 0 || si != 1) Fail("a coop-peer row in the file is dropped and counted");
    si = 0;
    if (ParseLine("v1\tjunk", &f, &si) != 0 || si != 0) Fail("junk is unusable, not a stand-in row");
    if (ParseLine(Line(npc, npc2, r), &f, &si) != 1 || si != 0 || f.size() != 1) Fail("an ordinary row still loads, uncounted");
    /* the two new kinds: only SEED_END may be empty; ECHO and SEED_END round trip; Merge takes neither (the callers map them) */
    std::vector<WireRow> none;
    if (!Encode(&m, kWrSeedEnd, none) || m.size() != 5 || Decode(&m[0], m.size(), &k, &back) != 1 || k != kWrSeedEnd || !back.empty()) Fail("an empty SEED_END is a message (a walk that found nothing still ends)");
    if (Encode(&m, kWrEcho, none) || Encode(&m, kWrSeed, none) || Encode(&m, kWrChange, none)) Fail("only SEED_END may be empty");
    { std::vector<char> e(5, 0); e[0] = (char)kWrChange; if (Decode(&e[0], e.size(), &k, &back) != 0) Fail("an empty CHANGE on the wire is refused"); }
    { std::vector<char> e(5, 0); e[0] = 6; if (Decode(&e[0], e.size(), &k, &back) != 0) Fail("kind 6 is unknown"); }
    if (!Encode(&m, kWrEcho, v) || Decode(&m[0], m.size(), &k, &back) != 1 || k != kWrEcho || back.size() != 1 || back[0].b != npc2) Fail("an ECHO round trip");
    if (!Encode(&m, kWrSeedEnd, v) || Decode(&m[0], m.size(), &k, &back) != 1 || k != kWrSeedEnd || back.size() != 1) Fail("a SEED_END with rows round trip");
    if (Merge(&t, npc, npc2, r, kWrEcho) != -1 || Merge(&t, npc, npc2, r, kWrSeedEnd) != -1) Fail("Merge takes only SEED / CHANGE / ROWS");
    /* #2: the send book - a row is held while this game's change is unsent (dirty) or its last send unanswered */
    SendBook b; const std::string key = Key(npc, npc2);
    if (b.Waiting(key) || !MayApply(false, b.Waiting(key))) Fail("no send: the notebook's rows apply");
    b.Sent(key); b.Sent(key);
    if (!b.Waiting(key) || MayApply(false, b.Waiting(key))) Fail("an unanswered send holds the pair's rows");
    if (b.Echo(key) || !b.Waiting(key)) Fail("the echo of the FIRST of two sends does not release the pair (it is older than the second)");
    if (!b.Echo(key) || b.Waiting(key) || b.Size() != 0) Fail("the echo of the LAST send releases the pair");
    if (!b.Echo(key) || b.Waiting(key)) Fail("an echo with nothing on the book (a send before a reset) releases and never goes negative");
    if (MayApply(true, false)) Fail("a pair dirty here (a local change not yet sent) is not overwritten");
    b.Sent(key); b.Clear(); if (b.Waiting(key)) Fail("a WELCOME / world load clears the book");
    {   /* the lost-change case: sent 10, then the engine moved it to 40 and sent that; the echo of 10 arrives first */
        Table mirror; SendBook sb; Row r10; r10.relation = 10.0f; Row r40; r40.relation = 40.0f; float engine = 40.0f;
        sb.Sent(key); sb.Sent(key);
        Merge(&mirror, npc, npc2, r10, kWrRows); sb.Echo(key);
        if (MayApply(false, sb.Waiting(key))) engine = mirror[key].relation;
        if (engine != 40.0f) Fail("the echo of an older send never overwrites the newer local change");
        Merge(&mirror, npc, npc2, r40, kWrRows); sb.Echo(key);
        if (MayApply(false, sb.Waiting(key))) engine = mirror[key].relation;
        if (engine != 40.0f || sb.Waiting(key)) Fail("the last echo releases the pair on the notebook's latest row");
    }
    /* S2-62: the world's own save (the operator's) sets the starting table */
    if (SeedDecision(true, true, false) != kSdTake || SeedDecision(true, true, true) != kSdTake || SeedDecision(true, false, false) != kSdTake) Fail("the operator's seed is taken");
    if (SeedDecision(false, true, false) != kSdHold) Fail("another game's seed is HELD while the operator is linked and has not ended its walk");
    if (SeedDecision(false, true, true) != kSdTake || SeedDecision(false, false, true) != kSdTake) Fail("after the operator's walk another game's seed is taken, first writer");
    if (SeedDecision(false, false, false) != kSdFallback) Fail("the operator not linked: first-in (fallback)");
    {   /* re-check B: the held rows merge AFTER the operator's walk - the operator's value wins, a pair its save lacks is filled (first held row in) */
        const std::string npc3("300-gamedata.base");
        Table w; Row op; op.relation = 10.0f; Row h1; h1.relation = -50.0f; Row h2; h2.relation = 70.0f; Row h3; h3.relation = 90.0f;
        if (Merge(&w, npc, npc2, op, kWrSeed) != 1) Fail("the operator's seed is taken");
        std::vector<WireRow> held, taken; long long same = 0, refused = 0;
        WireRow x; x.a = npc; x.b = npc2; x.row = h1; held.push_back(x);            /* a pair the operator's save has */
        x.a = npc; x.b = npc3; x.row = h2; held.push_back(x);                         /* a pair it lacks */
        x.row = h3; held.push_back(x);                                                /* the same gap, a later held row */
        x.a = "coop-p2"; x.b = npc; held.push_back(x);                                /* never a row */
        MergeHeld(&w, held, &taken, &same, &refused);
        if (w[Key(npc, npc2)].relation != 10.0f) Fail("a held row never overwrites the operator's value");
        if (w.count(Key(npc, npc3)) == 0 || w[Key(npc, npc3)].relation != 70.0f) Fail("a pair the operator's save lacks is filled from the first held row");
        if (taken.size() != 1 || same != 2 || refused != 1 || w.size() != 2) Fail("MergeHeld: one taken, two already held, one refused");
    }
}


/* one writer per wild resource node - a production building claimed only by its own faction mark is nobody's (the
   area rule); the build record and a player's house keep the owner rule; a worker's step runs on the writer only and travels
   as MINE_OP (kind 10) otherwise */
void t_t616_node_writer_mine_op()
{
    using namespace coopown;
    /* the faction mark alone (via 0) on a production building: nobody's, whatever the mark read */
    if (MarkOnlyNodeClass(1, 0, kOwnMe) != kOwnNone) Fail("a node marked with this game's faction stayed this game's");
    if (MarkOnlyNodeClass(1, 0, kOwnPeer) != kOwnNone) Fail("a node marked with another player's faction stayed theirs");
    if (MarkOnlyNodeClass(1, 0, kOwnShared) != kOwnNone || MarkOnlyNodeClass(1, 0, kOwnUnknown) != kOwnNone)
        Fail("a node's shared / unknown mark was kept");
    if (MarkOnlyNodeClass(1, 0, kOwnNone) != kOwnNone) Fail("an unmarked node changed class");
    if (MarkOnlyNodeClass(1, 0, kOwnUnreadable) != kOwnUnreadable) Fail("an unreadable node was made nobody's");
    /* the build record (via 2) and a player's house (via 1) keep the owner; a building that is not a production one keeps its mark */
    if (MarkOnlyNodeClass(1, 2, kOwnMe) != kOwnMe || MarkOnlyNodeClass(1, 2, kOwnPeer) != kOwnPeer) Fail("a recorded machine lost its owner");
    if (MarkOnlyNodeClass(1, 1, kOwnPeer) != kOwnPeer) Fail("a machine in a player's house lost its owner");
    if (MarkOnlyNodeClass(0, 0, kOwnMe) != kOwnMe) Fail("a non-production building's own mark was dropped");
    /* both games: whatever each game's copy is marked with, the rung steps aside to the area rule on both */
    int why = -1;
    if (OwnerRung(MarkOnlyNodeClass(1, 0, kOwnMe), 0, 1, &why) != kRungArea
        || OwnerRung(MarkOnlyNodeClass(1, 0, kOwnPeer), 1, 1, &why) != kRungArea)
        Fail("a marked node did not go to the area rule on both games");

    /* the build scan: only a production building marked foliage, that a worker operates, with no inputs and no build record, is
       a world node */
    if (WorldNodeOf(1, 1, 0, 0, 1) != 1) Fail("a world resource node (marked foliage, no record, no inputs) was registered");
    if (WorldNodeOf(1, 1, 0, 0, 0) != 0) Fail("a player's Ore Mine / Stone Mine / Well (operated, no inputs, not foliage, no record) was skipped");
    if (WorldNodeOf(1, 1, 0, 0, -1) != 0) Fail("a production building whose foliage mark did not read was skipped");
    if (WorldNodeOf(1, 1, 1, 0, 1) != 0 || WorldNodeOf(1, 1, 3, 0, 1) != 0) Fail("a player-built machine with inputs and no record was skipped");
    if (WorldNodeOf(1, 1, 0, 1, 1) != 0) Fail("a recorded production building was skipped");
    if (WorldNodeOf(0, 1, 0, 0, 1) != 0) Fail("a building that is not a production one was skipped");
    if (WorldNodeOf(1, 1, -1, 0, 1) != 0) Fail("a production building with an unreadable input count was skipped");

    /* where a worker's step runs */
    if (coopfarm::MineStepRoute(1, 1, coopfarm::kProdWriterOther) != coopfarm::kMineRun) Fail("a lone game relayed a worker's step");
    if (coopfarm::MineStepRoute(0, 0, coopfarm::kProdWriterOther) != coopfarm::kMineRun) Fail("a game with nobody else in the world relayed a step");
    if (coopfarm::MineStepRoute(0, 1, coopfarm::kProdWriterHere) != coopfarm::kMineRun) Fail("the writer relayed its own worker's step");
    if (coopfarm::MineStepRoute(0, 1, coopfarm::kProdWriterOther) != coopfarm::kMineRelay) Fail("a non-writer ran a worker's step on its own copy");
    if (coopfarm::MineStepRoute(0, 1, coopfarm::kProdWriterNone) != coopfarm::kMineRelay) Fail("an undecided game ran a worker's step locally");
    /* T-610: a copy of another game's character is neither run nor relayed; the copy test; the take step refuses it */
    if (coopfarm::MineWorkerSkip(0, 1) != 1) Fail("T-610: a copy's worker step was not skipped");
    if (coopfarm::MineWorkerSkip(0, 0) != 0) Fail("T-610: this game's own worker's step was skipped");
    if (coopfarm::MineWorkerSkip(1, 1) != 0) Fail("T-610: a lone game skipped a worker's step");
    if (coopfarm::WorkerIsCopy(7, 0, 1) != 1) Fail("T-610: a replicated character this game does not own was not a copy");
    if (coopfarm::WorkerIsCopy(7, 1, 1) != 0) Fail("T-610: this game's own character was called a copy");
    if (coopfarm::WorkerIsCopy(0, 0, 1) != 0) Fail("T-610: a character with no uid was called a copy");
    if (coopfarm::WorkerIsCopy(7, 0, 0) != 0) Fail("T-610: an inexact owned mirror's 'not mine' was taken as proof");
    if (coopfarm::SpotCopyRefused(0, 1, 0, 1) != 1) Fail("T-610: an unlisted copy took a spot");
    if (coopfarm::SpotCopyRefused(0, 1, 1, 1) != 0) Fail("T-610: a copy the runner listed was refused");
    if (coopfarm::SpotCopyRefused(0, 1, 0, 0) != 0) Fail("T-610: this game's own worker was refused as a copy");
    if (coopfarm::SpotCopyRefused(1, 1, 0, 1) != 0 || coopfarm::SpotCopyRefused(0, 0, 0, 1) != 0) Fail("T-610: a lone or unlinked game refused a take");
    if (coopfarm::MineOpAccept(coopfarm::kProdWriterHere) != coopfarm::kMineAccRun) Fail("MINE_OP: the writer did not run it");
    if (coopfarm::MineOpAccept(coopfarm::kProdWriterOther) != coopfarm::kMineAccRefuse) Fail("MINE_OP: a game another game writes for did not refuse it");
    if (coopfarm::MineOpAccept(coopfarm::kProdWriterNone) != coopfarm::kMineAccWait) Fail("MINE_OP: undecided work was not held");
    if (coopfarm::MineHoldExpired(0) != 0 || coopfarm::MineHoldExpired(coopfarm::kMinePendMs) != 0
        || coopfarm::MineHoldExpired(coopfarm::kMinePendMs + 1) != 1)
        Fail("MINE_OP hold: kept until kMinePendMs, dropped after");

    /* the wire: kind 10 round trip, refusals, no collision with the building road's kinds */
    coopfarm::FarmMsg m; m.kind = coopfarm::kMineOp; m.key = "42249-gamedata.base@43,11@545954,-924381,9819";
    m.uid = 0x80000077u; m.amount = 1.25f; m.work = 0.75f;
    std::vector<char> b;
    if (!coopfarm::EncodeFarm(&b, m)) { Fail("a MINE_OP did not encode"); return; }
    if (b.size() != 2 + m.key.size() + 12 || (unsigned char)b[0] != 10) Fail("MINE_OP layout: kind | len + key | uid | amount | work");
    if (coopfarm::IsFarmKind(&b[0], b.size()) != 1) Fail("kind 10 not recognised by the farm peek");
    coopfarm::FarmMsg o;
    if (coopfarm::DecodeFarm(&b[0], b.size(), &o) != coopfarm::kFarmDecodeOk || o.kind != coopfarm::kMineOp || o.key != m.key
        || o.uid != m.uid || o.amount != 1.25f || o.work != 0.75f)
        Fail("a MINE_OP did not round-trip (key, worker uid, amount, work)");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopfarm::DecodeFarm(&b[0], cut, &o) == coopfarm::kFarmDecodeOk) { Fail("a truncated MINE_OP was accepted"); break; }
    std::vector<char> t(b); t.push_back(0);
    if (coopfarm::DecodeFarm(&t[0], t.size(), &o) == coopfarm::kFarmDecodeOk) Fail("a MINE_OP with a trailing byte was accepted");
    coopbuild::BuildMsg bm;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &bm) == coopbuild::kBuildDecodeOk) Fail("DecodeBuild accepted a MINE_OP");
    const char k8 = 8, k9 = 9;
    if (coopfarm::IsFarmKind(&k8, 1) != 0 || coopfarm::IsFarmKind(&k9, 1) != 0) Fail("the farm peek took the building road's kinds 8 / 9");
    coopfarm::FarmMsg bad(m); bad.work = 0.0f;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_OP with no work was encodable");
    bad = m; bad.work = coopfarm::kFarmOpMax + 1.0f;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_OP above the per-message work cap was encodable");
    bad = m; bad.amount = 0.0f;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_OP with no call amount was encodable");
    bad = m; bad.amount = 0.0f / std::atof("0");
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_OP with a NaN amount was encodable");

    /* the writer's calls: never more than the sender's call amount per call at its own frame */
    float amt = 0.0f;
    const float w1 = coopfarm::FarmWorkSlice(0.75f, 1.0f / 60.0f, 1.25f, &amt);
    if (!(w1 > 0.0f) || amt > 1.25f + 1.0e-5f || w1 > 1.25f / 60.0f + 1.0e-6f) Fail("a MINE_OP slice exceeded one call's amount");
    if (coopfarm::FarmWorkSlice(0.75f, 0.0f, 1.25f, &amt) != 0.0f) Fail("a paused writer ran a MINE_OP slice");
}

}   /* namespace suite */
