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
#include "../common/logbudget.h"   /* the release build's quiet-logging gate */

void DebugLog(const std::wstring& message);
void DebugLog(const std::string& message);
void DebugLog(const char* message);
void ErrorLog(const std::wstring& message);
void ErrorLog(const std::string& message);
void ErrorLog(const char* message);

/* T-461: the path of this launch's log (copy 0) or of an earlier launch's kept copy (1 = the previous launch;
   src/common/logrotate.h), beside SharedWastelands.dll; empty when the DLL's folder cannot be read. ANY THREAD. */
std::wstring LogFilePath(int copy);

/* The path of one part of a launch's log (src/common/logrotate.h: cooplogrot::kPartCurrent / kPartStart / kPartPrev): the
   current part is LogFilePath(copy); ".start" holds the launch's opening 2 MB and ".prev" the part before the current one,
   once the log has grown that far. Empty when the DLL's folder cannot be read. ANY THREAD. */
std::wstring LogPartPath(int copy, int part);

/* THE BUILD LABEL: "DEV build" (built with build.bat dev - SW_DEV_BUILD, the test probes compiled in) or "release build". The
   log's first line and the bug report's report.txt carry it. */
const char* LogBuildLabel();

/* QUIET LOGGING (the release build; src/common/logbudget.h). True = write the line in full; when the gate holds back lines, this
   writes the one count line itself ("<family> [<keyText>]: n more lines like this in the last s s were not written"). ANY
   THREAD (a gate shared by two threads can only miscount). */
bool QuietLogPass(swlogbudget::Gate* gate, const char* family, unsigned long long key, const char* keyText);
/* Writes one count line for every key of every gate QuietLogPass has seen that holds lines back (the count since its last line).
   periodic = true: at most once a minute (the soak tick calls it every frame); false: now (before a bug report is packed, and at
   the game's exit). ANY THREAD. */
void QuietLogFlush(bool periodic);

/* SW_DEVLOG(msg): a test probe's line, compiled into the DEV build only - in the release build neither the line nor its text
   is built. SW_QUIETLOG(gate, family, key, keyText, msg): a repeating line - every line in the DEV build; in the release build
   the first few per key in full, then one count line per minute (QuietLogPass); the text is built only for a line written.
   SW_DEVLOG_OR_FAIL(failed, msg): a probe line that also reports a failure or a refusal - every line in the DEV build; in the
   release build written whenever `failed` is true (a failure is never a DEV-only line), its text built only then. */
#ifdef SW_DEV_BUILD
#define SW_DEVLOG(msg) DebugLog(msg)
#define SW_DEVLOG_OR_FAIL(failed, msg) DebugLog(msg)
#define SW_QUIETLOG(gate, family, key, keyText, msg) DebugLog(msg)
#else
#define SW_DEVLOG(msg) ((void)0)
#define SW_DEVLOG_OR_FAIL(failed, msg) do { if (failed) DebugLog(msg); } while (0)
#define SW_QUIETLOG(gate, family, key, keyText, msg) do { if (QuietLogPass(&(gate), (family), (key), (keyText))) DebugLog(msg); } while (0)
#endif

/* T-910 - SW_FIRSTLOG(counter, n, msg): a repeating line whose count is on a REPORT line already - every line in the DEV build; in
   the release build the first n (swlogbudget::FirstLines on `counter`, an unsigned int the caller keeps), then none. SW_FIRSTN(counter,
   n): the same test as a condition (always true in the DEV build). The text is built only for a line written. */
#ifdef SW_DEV_BUILD
#define SW_FIRSTN(counter, n) ((void)(counter), true)
#define SW_FIRSTLOG(counter, n, msg) do { (void)(counter); DebugLog(msg); } while (0)
#else
#define SW_FIRSTN(counter, n) (swlogbudget::FirstLines(&(counter), (n)))
#define SW_FIRSTLOG(counter, n, msg) do { if (swlogbudget::FirstLines(&(counter), (n))) DebugLog(msg); } while (0)
#endif

/* T-910 - THE [KEEPER] LINES OF THIS LAUNCH, for the bug report's keeper-lines.txt: every line written whose text starts "[KEEPER]"
   is also kept in memory (src/common/linekeep.h: the first 50 and the newest 200), so a cut of the log file cannot lose them.
   Returns them as one text in time order (empty when there are none) and sets *seen to how many such lines this launch wrote.
   ANY THREAD. */
std::string LogKeeperLinesText(unsigned long long* seen);
