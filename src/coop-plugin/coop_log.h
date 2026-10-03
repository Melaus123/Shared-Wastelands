#pragma once
/* OUR OWN log file.

   DebugLog/ErrorLog: global functions, one overload for std::string and one for std::wstring each, defined in
   this DLL (coop_log.cpp).

   Where: <folder of SharedWastelands.dll>\shared_wastelands_log.txt (mods\Shared Wastelands\shared_wastelands_log.txt), created
   fresh at the first log call of each game run. Just before that, the earlier launches' logs move one place older
   (shared_wastelands_log.1.txt is the previous launch, .2.txt the one before; src/common/logrotate.h), so a crashed
   launch's log survives the next start. RE_Kenshi_log.txt no longer receives our lines.

   Line format (tools rely on the tag, the "Error " prefix and the shape):
       <sec>.<msec> KenshiCoop: <message>CRLF
       Error <sec>.<msec> KenshiCoop: <message>CRLF
   time = milliseconds since this DLL's first log call. Every '\n' inside a message becomes CRLF.
   Each line is ONE WriteFile straight to the OS (no library buffer),
   so every line written before a game crash is in the file. The caller's text is copied BEFORE the lock is
   taken (a fault in a corrupt string cannot leave the lock held); under the lock only the timestamp and the
   write happen, so lines from different threads never interleave and stay in time order. If the file cannot
   be opened, that line is dropped silently and the next line tries to open it again. */

#include <string>

void DebugLog(const std::wstring& message);
void DebugLog(const std::string& message);
void DebugLog(const char* message);
void ErrorLog(const std::wstring& message);
void ErrorLog(const std::string& message);
void ErrorLog(const char* message);

/* T-461: the path of this launch's log (copy 0) or of an earlier launch's kept copy (1 = the previous launch;
   src/common/logrotate.h), beside SharedWastelands.dll; empty when the DLL's folder cannot be read. ANY THREAD. */
std::wstring LogFilePath(int copy);
