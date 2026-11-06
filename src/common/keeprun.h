/* A WORLD KEPT RUNNING (HOST GAME's "Keep this world running for others"): the panel's pure decisions and words for a world server
 * that keeps running after the host's game leaves.
 *   - host.key in the world folder: this hosting's secret (the --host-token the world server was started with) and its port, written
 *     by the host's game at a kept start and read back by it, so the returning owner proves who they are without typing anything.
 *     Its read has three answers that are never one: no file, a file that could not be read, a file that does not parse.
 *   - "running now": the world lock held by a running world server (the fact) AND a usable host.key - never the key alone.
 *   - stop.request in the world folder: a kept world server stops when it appears (it removes the file itself).
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once
#include <string>
#include "keeprunning.h"   /* kStopFile: the stop file's name, shared with the world server */

namespace keeprun {

const char* const kKeyFile    = "host.key";        /* in the world folder, on the host's own disk: the owner's secret */
const unsigned int kTokenHexChars = 64;            /* the hosting's secret, 32 bytes (authproof::kTokenBytes), as hex */
const unsigned int kKeyFileMax = 1024;             /* a longer host.key is not one this game wrote */

/* host.key read back */
enum KeyState { kKeyOk = 0, kKeyMissing = 1, kKeyUnreadable = 2, kKeyMalformed = 3 };
inline const char* KeyStateName(int s)
{
    return s == kKeyOk ? "ok" : s == kKeyMissing ? "missing" : s == kKeyUnreadable ? "unreadable" : s == kKeyMalformed ? "malformed" : "unknown";
}

inline int KeyHexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool KeyTokenHexOk(const std::string& s)
{
    if (s.size() != kTokenHexChars) return false;
    for (size_t i = 0; i < s.size(); ++i) if (KeyHexVal(s[i]) < 0) return false;
    return true;
}
/* The hex secret as its bytes: false (and nothing written) unless `hex` is exactly 2 * n hex digits. */
inline bool KeyTokenBytes(const std::string& hex, unsigned char* out, unsigned int n)
{
    if (hex.size() != 2u * n) return false;
    for (size_t i = 0; i < hex.size(); ++i) if (KeyHexVal(hex[i]) < 0) return false;
    for (unsigned int i = 0; i < n; ++i) out[i] = (unsigned char)(KeyHexVal(hex[2 * i]) * 16 + KeyHexVal(hex[2 * i + 1]));
    return true;
}
inline std::string KeyTrim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
inline std::string KeyPortText(unsigned int port)
{
    std::string d;
    do { d.insert(d.begin(), (char)('0' + port % 10)); port /= 10; } while (port != 0);
    return d;
}
/* What host.key holds: plain `name = value` lines. */
inline std::string KeyFormat(const std::string& tokenHex, unsigned int port)
{
    return "token = " + tokenHex + "\r\nport = " + KeyPortText(port) + "\r\n";
}
/* host.key's text: kKeyOk with *tokenHex (64 hex digits) and *port (1..65535), else kKeyMalformed with both cleared - a missing
   token or port, either one twice, a bad value or a line with no '=' (blank lines and other names are passed over). */
inline int KeyParse(const std::string& text, std::string* tokenHex, unsigned int* port)
{
    tokenHex->clear();
    *port = 0;
    if (text.size() > kKeyFileMax) return kKeyMalformed;
    std::string tok;
    unsigned int p = 0;
    int haveTok = 0, havePort = 0;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = KeyTrim(text.substr(at, nl - at));
        at = nl + 1;
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) return kKeyMalformed;
        const std::string name = KeyTrim(line.substr(0, eq)), value = KeyTrim(line.substr(eq + 1));
        if (name == "token")
        {
            if (haveTok != 0 || !KeyTokenHexOk(value)) return kKeyMalformed;
            tok = value;
            haveTok = 1;
        }
        else if (name == "port")
        {
            if (havePort != 0 || value.empty() || value.size() > 5) return kKeyMalformed;
            unsigned int v = 0;
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] < '0' || value[i] > '9') return kKeyMalformed;
                v = v * 10 + (unsigned int)(value[i] - '0');
            }
            if (v < 1 || v > 65535) return kKeyMalformed;
            p = v;
            havePort = 1;
        }
    }
    if (haveTok == 0 || havePort == 0) return kKeyMalformed;
    *tokenHex = tok;
    *port = p;
    return kKeyOk;
}

/* "running now": a world server holds the world's lock AND host.key read back whole. A held lock with a missing, unreadable or
   malformed key is a world server this game cannot prove it owns - not "running now". */
inline int RunningNow(int lockHeld, int keyState) { return (lockHeld != 0 && keyState == kKeyOk) ? 1 : 0; }

/* THE WORDS (owner-approved mock-ups, T-1158 sections 1, 2 and 9; kKeepOpenWarn owner 1509). */
const char* const kKeepLabel   = "Keep this world running for others";
const char* const kKeepNote    = "When you leave, the world keeps running in its own window. Only players who joined by address stay."
                                 " Players who joined through Steam leave with you.";
const char* const kKeepWarn    = "A world kept running checks only its password, if it has one. Keep it on your home network or a private"
                                 " network such as Radmin or Hamachi.";   /* owner 1514 a (3) */
const char* const kKeepOpenWarn = "No password: anyone who knows this world's address can join while you are away.";
const char* const kRunningNowPlayed = "running now";   /* the worlds list's LAST PLAYED cell */
const char* const kStopBtn     = "STOP WORLD";         /* in DELETE WORLD's place, and the dialog's action */
const char* const kStopTitle   = "STOP WORLD?";
/* HOST GAME's status line when the world's lock cannot be checked (not an approved mock-up's words) */
const char* const kLockUnknownText = "Couldn't check whether this world is already running. Try again.";

/* Under the tick box: the note, and while ticked the warning under it. */
inline std::string KeepNoteText(int ticked) { return ticked != 0 ? std::string(kKeepNote) + "\n" + kKeepWarn : std::string(kKeepNote); }
inline std::string RunningNowText(const std::string& world) { return "\"" + world + "\" is still running. HOST joins it."; }
inline std::string StopDlgText(const std::string& world) { return "Stop \"" + world + "\"? Everyone in it is disconnected. The world is saved first."; }
inline std::string StoppedText(const std::string& world) { return "\"" + world + "\" stopped."; }

/* AN OLDER VERSION STILL RUNNING (T-1254, owner 1512 a): HOST on a world running now (its lock held, its host.key whole) joins it; when
   that world server answers with the version refusal (the "Version mismatch" sentence, coopui::RefusalIsVersion) it is an older
   version's - 0.4.0 kept worlds write the same host.key. The game marks that run by its folder and host.key secret (a world started
   again writes a new secret, so the mark ends with the run), and while the marked run is the pick HOST writes no password line into
   its rules.txt (owner 1508 b: a 0.4.0 world's password lock stays for the 0.4.1 world server) and its status line says this. */
const char* const kOlderRunningLine = "This world is still running an older version. Press STOP WORLD, then HOST.";
inline int OlderRunningMark(int keepJoin, int versionRefusal) { return (keepJoin != 0 && versionRefusal != 0) ? 1 : 0; }
inline int OlderRunningNow(int runningNow, const std::string& markFolder, const std::string& markToken, const std::string& pickFolder, const std::string& pickToken)
{
    return (runningNow != 0 && !markFolder.empty() && markFolder == pickFolder && markToken == pickToken) ? 1 : 0;
}

/* T-1255 D (owner 1515: "the host screen doesn't need a note at all to tell them it keeps the password"): the ticked world will really
   be open - the PASSWORD box empty and the world has no password of its own (worldHas: 1 a world password line that is not empty or a
   saved lock that applies, worldpw::WorldHasPassword; 0 none; -1 not known - no note) */
inline int KeepOpenPick(int pwEmpty, int worldHas) { return (pwEmpty != 0 && worldHas == 0) ? 1 : 0; }
/* HOST GAME's status line: a press in progress says its own line; else a picked world running now says so (an older version's says
   kOlderRunningLine: older = OlderRunningNow); else a ticked box whose world will really be open (open: KeepOpenPick) says so while its
   host is away (owner 1508 b; HOST stays lit); else the panel's own line. */
inline std::string KeepStatusPick(int busy, int runningNow, int ticked, int open, const std::string& world, const std::string& base, int older = 0)
{
    if (busy != 0) return base;
    if (runningNow != 0 && older != 0) return kOlderRunningLine;
    if (runningNow != 0) return RunningNowText(world);
    if (ticked != 0 && open != 0) return kKeepOpenWarn;
    return base;
}

}   /* namespace keeprun */
