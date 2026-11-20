#pragma once
/* crashwatch.h - T-1374 (owner 1688): the [CRASH] line (src/common/crashline.h), written when the game's crash handling starts.
   crashwatch.cpp. */
#include <string>

namespace coop {
/* Once, from startPluginOn after the address gate accepted the executable (plugin start thread = the game's main thread): the
   detour on Kenshi's main-loop crash filter (row KenshiCrashFilter), the process's unhandled-exception filter (chained) and the
   SIGABRT handler; one [CRASHWATCH] line. */
void InstallCrashWatch();
/* MAIN THREAD, every main-loop frame (coop.cpp detour_mainLoop - outside any mod __try): notes the main thread; at each world
   load sets the unhandled-exception filter again (logging which module replaced ours, when one did); the DEV build's armed
   crash test. */
void CrashWatchTick();
/* 1 once this process wrote its [CRASH] line. ANY THREAD. */
int CrashLineWritten();
/* Kenshi's save root - the folder its crash handling writes crashDump<version>_x64.zip into (row KenshiSaveRootText), widened
   from the game's text; "" when the row is not filled, the text is empty or unreadable. ANY THREAD after the address gate. */
std::wstring KenshiSaveRoot();
#ifdef SW_DEV_BUILD
/* TEST-ONLY `crashtest av-main | av-worker | throw-main | throw-worker | purecall | abort`: the main kinds fault in the next
   main-loop frame (CrashWatchTick), the worker kinds on a new thread now. Returns the status text. */
std::string CrashTestCommand(const std::string& kind);
#endif
}
