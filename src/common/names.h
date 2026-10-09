/* src/common/names.h - EVERY NAME THE MOD PUTS ON DISK OR SHOWS AS ITS OWN, IN ONE PLACE (owner decisions 427, 435).

   The plugin, the loader, the world server, Setup and the offline suite all read their file names, folder names and
   window texts from here. Files that cannot include a header repeat these names by hand and say so beside them:
   src/coop-plugin/build.bat, src/kenshi-loader/build.bat, src/coop-store/build.bat, src/installer/build.bat,
   src/coop-store/make_version.py, src/installer/make_icon.py, tools/makemod/Program.cs, tools/build-package.ps1 and the
   mod-package folder itself (its folder name, .mod, .info, loader marker and RE_Kenshi.json).

   The SW_* macros exist so a name can be joined to other literal text at compile time (L"" SW_NAME_W L" is installed.");
   everything else uses the constants below them. Header-only; no Windows headers; safe in the CRT-free loader. */
#ifndef SW_NAMES_H
#define SW_NAMES_H

#define SW_NAME            "Shared Wastelands"
#define SW_NAME_W          L"Shared Wastelands"
#define SW_SERVER_TITLE    SW_NAME " world server"
#define SW_SERVER_TITLE_W  SW_NAME_W L" world server"

namespace swnames {

/* The mod as Kenshi's launcher lists it: the mod folder, "<name>.mod" and "_<name>.info" (mod-package/Shared Wastelands). */
const char*    const kModName         = SW_NAME;
const char*    const kModNameLower    = "shared wastelands";        /* compared with lower-cased mods.cfg rows / mod names */

/* The data folder: %LOCALAPPDATA% + kDataSub, and the fallback <save root>\<kSaveRootSub>\{worlds,players}. */
const char*    const kDataSub         = "\\kenshi\\" SW_NAME;
const char*    const kSaveRootSub     = SW_NAME;

/* The plugin DLL (RE_Kenshi.json's PreloadPlugins entry and the loader marker's dll= line). */
const char*    const kPluginDll       = "SharedWastelands.dll";
const char*    const kPluginDllLower  = "sharedwastelands.dll";     /* compared with lower-cased RE_Kenshi.json rows */

/* The loader: the value of its Plugins_x64.cfg line (Ogre prepends PluginFolder and appends .dll), its file, its marker, its log. */
const char*    const kLoaderPlugin    = "SharedWastelandsLoader";
const wchar_t* const kLoaderDllW      = L"SharedWastelandsLoader.dll";
const char*    const kLoaderMarker    = "shared-wastelands.loader.txt";
const wchar_t* const kLoaderLogW      = L"\\SharedWastelandsLoader_log.txt";   /* appended to the game folder */
/* The loader's previous Plugins_x64.cfg value. Setup removes a line naming it on install: a line naming a DLL that is
   not there stops Kenshi starting. Nothing else reads it. */
const char*    const kOldLoaderPlugin = "KenshiMultiplayerLoader";

/* The world server: the file the MULTIPLAYER panel starts from beside the plugin, its window and router (UPnP) entry
   texts, its hidden quit window's class, and its log in each world folder. */
const char*    const kServerExe       = "SharedWastelandsServer.exe";
const char*    const kServerTitle     = SW_SERVER_TITLE;
const wchar_t* const kServerTitleW    = SW_SERVER_TITLE_W;
const char*    const kServerQuitClass = "SharedWastelandsServerQuit";
const char*    const kServerLog       = "shared-wastelands-server.log";

/* Under the data folder: the folder of each world this computer joins but does not run (<data folder>\joined\<world>~<8 hex
   of its id>), and the file in every world, joined-world and records folder that says which on-disk format it holds. */
const char*    const kJoinedSub       = "joined";
const char*    const kFormatFile      = "format.txt";

/* The DLL-side settings file (TEST-only: written by the harness) and the header line the in-game panel once wrote. */
const char*    const kTestCfg         = "shared_wastelands.cfg";
#define SW_TEST_CFG_HEADER "# shared_wastelands.cfg - written by"

/* Files beside the plugin DLL: its log, the kept copies of the two launches before this one (logrotate.h: copy 1 the
   previous launch, copy 2 the one before it), and the test harness's status and command files. */
const char*    const kLog             = "shared_wastelands_log.txt";
const wchar_t* const kLogW            = L"shared_wastelands_log.txt";
const char*    const kLogPrev         = "shared_wastelands_log.1.txt";
const char*    const kLogPrev2        = "shared_wastelands_log.2.txt";
const char*    const kStatusFile      = "shared_wastelands_status.txt";
const char*    const kCmdFile         = "shared_wastelands_cmd.txt";

/* Setup and the bug report. */
const wchar_t* const kSetupTitleW     = SW_NAME_W L" Setup";
const char*    const kBugReportTitle  = SW_NAME " bug report";
const wchar_t* const kBugReportAgentW = L"SharedWastelands-BugReport/1";
const wchar_t* const kAddrLookupAgentW = L"SharedWastelands-AddressLookup/1";
const wchar_t* const kRouterAgentW   = L"SharedWastelands-Router/1";   /* the HTTP user agent of the router ask (upnp.cpp) */

}  /* namespace swnames */

#endif
