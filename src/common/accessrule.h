/* src/common/accessrule.h - WHO MAY ENTER A WORLD: the world folder's access.txt and the decision the world server makes with it at
   each admission. PURE: no Windows.h, no engine, no globals, no I/O, so the offline suite checks exactly what the world server
   compiles.

   access.txt, one rule per line, # starts a comment:
       mode open|allowlist          open (the default, and what a missing file means) or allow-list
       allow <steamid64> <note>     a Steam account let in under allow-list mode
       block <steamid64> <note>     a Steam account never let in
   A line that is none of these is counted (badLines) and skipped; it never stops the file being read. Entries are numbered in file
   order from 1 ("entry #n"), so a log can name one without naming the account.

   The identity a rule can name is a Steam id, and only a Steam joiner has one the world server can trust: the host's Steam bridge
   tells it (STEAM_ID with this hosting's secret, believed only as described at SteamIdJudge). An address joiner has no checked
   identity, so:
   - the block list applies to the Steam route only;
   - under allow-list mode an address joiner is not let in;
   - under open mode an address joiner is let in.
   The world's owner is the connection that proved this hosting's secret in HOST_TOKEN (HostTokenProves) - the host's own game. It
   has no Steam id, so no entry can name it: it is let in under both modes and is never blocked. Where a connection comes from (this
   PC or another) decides nothing. */
#ifndef SW_ACCESSRULE_H
#define SW_ACCESSRULE_H

#include <string>
#include <vector>
#include <cstdio>

namespace accessrule {

enum Mode { kModeOpen = 0, kModeAllowlist = 1 };
enum Kind { kKindAllow = 0, kKindBlock = 1 };
enum Decision { kAdmit = 0, kBlocked = 1, kNotAllowed = 2 };
static const char kAccessFile[] = "access.txt";

struct Entry
{
    int kind;
    unsigned long long id;
    std::string note;
    Entry() : kind(kKindAllow), id(0) {}
};

struct Rules
{
    int mode;
    std::vector<Entry> entries;   /* file order; entry #n is entries[n - 1] */
    int badLines;
    Rules() : mode(kModeOpen), badLines(0) {}
};

inline const char* ModeWord(int mode) { return mode == kModeAllowlist ? "allowlist" : "open"; }

inline const char* DecisionWord(int d)
{
    return d == kBlocked ? "blocked" : d == kNotAllowed ? "not on the allow list" : "admitted";
}

/* 1-20 decimal digits making a non-zero 64-bit number */
inline bool ParseId(const std::string& s, unsigned long long* id)
{
    if (s.empty() || s.size() > 20) return false;
    unsigned long long v = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        const unsigned long long d = (unsigned long long)(c - '0');
        if (v > (18446744073709551615ULL - d) / 10ULL) return false;
        v = v * 10ULL + d;
    }
    if (v == 0) return false;
    *id = v;
    return true;
}

inline std::string IdText(unsigned long long id)
{
    char b[24];
    std::sprintf(b, "%llu", id);
    return b;
}

inline Rules Parse(const std::string& text)
{
    Rules r;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::vector<std::string> f;
        size_t p = 0;
        while (p < line.size())
        {
            while (p < line.size() && (line[p] == ' ' || line[p] == '\t' || line[p] == '\r')) ++p;
            if (p >= line.size()) break;
            size_t q = p;
            while (q < line.size() && line[q] != ' ' && line[q] != '\t' && line[q] != '\r') ++q;
            if (f.size() < 2) f.push_back(line.substr(p, q - p));
            else
            {
                /* the note: the rest of the line as written */
                size_t e = line.size();
                while (e > p && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) --e;
                f.push_back(line.substr(p, e - p));
                break;
            }
            p = q;
        }
        if (f.empty()) continue;
        if (f[0] == "mode" && f.size() == 2 && (f[1] == "open" || f[1] == "allowlist"))
        {
            r.mode = f[1] == "allowlist" ? kModeAllowlist : kModeOpen;
            continue;
        }
        Entry e;
        if ((f[0] == "allow" || f[0] == "block") && f.size() >= 2 && ParseId(f[1], &e.id))
        {
            e.kind = f[0] == "block" ? kKindBlock : kKindAllow;
            if (f.size() >= 3) e.note = f[2];
            r.entries.push_back(e);
            continue;
        }
        ++r.badLines;
    }
    return r;
}

/* The file again, in the same shape (a TEST-ONLY writer and the round trip use it). */
inline std::string Text(const Rules& r)
{
    std::string s = std::string("mode ") + ModeWord(r.mode) + "\n";
    for (size_t i = 0; i < r.entries.size(); ++i)
    {
        const Entry& e = r.entries[i];
        s += (e.kind == kKindBlock ? "block " : "allow ") + IdText(e.id);
        if (!e.note.empty()) s += " " + e.note;
        s += "\n";
    }
    return s;
}

/* The first entry of `kind` naming `id`, as its entry number (1 = the first entry of the file); 0 = none. */
inline int Find(const Rules& r, int kind, unsigned long long id)
{
    for (size_t i = 0; i < r.entries.size(); ++i)
        if (r.entries[i].kind == kind && r.entries[i].id == id) return (int)i + 1;
    return 0;
}

/* THE DECISION AT ADMISSION. The owner (it proved HOST_TOKEN, and the world server never believes a STEAM_ID on the same
   connection) is let in first: it has no Steam id, so it is never blocked. A block entry wins over every Steam joiner; a blocked or
   listed account is only ever a Steam joiner (hasSteamId). Under allow-list mode the owner and listed Steam accounts are let in and
   nobody else is. *entryNo is the entry that decided (0 = none did). */
inline int Decide(const Rules& r, bool hasSteamId, unsigned long long id, bool isOwner, int* entryNo)
{
    if (entryNo != 0) *entryNo = 0;
    if (isOwner) return kAdmit;
    if (hasSteamId)
    {
        const int b = Find(r, kKindBlock, id);
        if (b > 0) { if (entryNo != 0) *entryNo = b; return kBlocked; }
    }
    if (r.mode != kModeAllowlist) return kAdmit;
    if (!hasSteamId) return kNotAllowed;
    const int a = Find(r, kKindAllow, id);
    if (a > 0) { if (entryNo != 0) *entryNo = a; return kAdmit; }
    return kNotAllowed;
}

/* STEAM_ID IS BELIEVED ONLY FROM THE HOST'S OWN BRIDGE: with this hosting's secret, as the connection's first message, alone (not
   inside a bundle), and on a connection that is neither bridged already nor the owner. The bridge sends it before the joiner can
   send anything, so one that comes any later is not the bridge's way: the world server closes that connection. One without the
   secret may be any program claiming any account: counted and ignored. The connection's address is no input. */
enum SteamIdVerdict { kSteamIdBelieve = 0, kSteamIdWrongToken = 1, kSteamIdLate = 2 };
inline int SteamIdJudge(bool tokenMatches, bool firstMessage, bool insideBundle, bool alreadyBridgedOrOwner)
{
    if (!firstMessage || insideBundle || alreadyBridgedOrOwner) return kSteamIdLate;
    return tokenMatches ? kSteamIdBelieve : kSteamIdWrongToken;
}

/* HOST_TOKEN PROVES THE OWNER: this hosting's secret, on a connection that is not bridged, before its HELLO. The connection's address
   and the player id its HELLO names are no input. */
inline bool HostTokenProves(bool tokenMatches, bool bridged, bool afterHello)
{
    return tokenMatches && !bridged && !afterHello;
}

/* ---- TEST-ONLY: the accesstest verb's edits ---- */
enum TestAction { kTestModeOpen = 0, kTestModeAllowlist = 1, kTestClear = 2 };

/* the rules after the action; *entryNo = the entry it wrote (0 = it wrote none) */
inline Rules TestApply(const Rules& in, int action, int* entryNo)
{
    Rules r = in;
    if (entryNo != 0) *entryNo = 0;
    if (action == kTestModeOpen) r.mode = kModeOpen;
    else if (action == kTestModeAllowlist) r.mode = kModeAllowlist;
    else r = Rules();
    r.badLines = 0;
    return r;
}

}   /* namespace accessrule */

#endif
