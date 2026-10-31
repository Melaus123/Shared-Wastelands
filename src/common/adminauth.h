#pragma once
/* ADMIN TWO-FACTOR AND THE ADMIN SECURITY LOG (dedicated server, 2026-10-31). Pure: no Windows.h, no disk, no clock of its own (every
   call takes `now` in seconds), no engine; the world server (store_main.cpp), the game (admin.cpp, chat.cpp) and the offline suite
   compile it. C++03 (VS2010 v100).

   TWO FACTORS. An admin command is carried out only when BOTH hold for that connection:
     1. its Steam id is on dedicated.txt's admin= / admin-loud= list (admincmd::GateCheck, as before), AND
     2. this connection ran /login with the server's ONE admin password since it connected, and has used admin rights within kIdleSec.
   No admin password set (no admin-auth.txt) = every admin command is refused (fail closed) and the server log says how to set one.

   THE PASSWORD NEVER LEAVES THE GAME. "/login <password>" typed in the chat box is taken by the game itself (chat.cpp, before any
   chat or command road - on every world, linked or not) and never sent as chat or as a command. The game asks for a challenge (ASK);
   the server answers with a fresh random nonce, the salt and iteration count from admin-auth.txt and this connection's session number
   (CHALLENGE); the game sends back only
       key   = PBKDF2-HMAC-SHA256(password, salt, iterations, 32 bytes)
       proof = HMAC-SHA256(key, nonce || session (u32, little-endian) || kProofLabel)
   (PROOF) and wipes the password and the key. The server compares in constant time against the key in admin-auth.txt. A challenge
   is good for one proof within kChallengeSec on the connection it was made for. Wrong proofs lock the player out for a while
   (authlock.h, its own table); asks are held to a per-connection allowance. Elevation ends on /logout, on disconnect, and after
   kIdleSec without an admin command.
   admin-auth.txt (the world folder): "adm1 <iterations> <salt hex> <key hex>" - PASSWORD-EQUIVALENT (whoever reads it can make
   proofs), written by --set-admin-password, kept to the server's own account.

   THE ADMIN SECURITY LOG (admin-security-YYYYMMDD.log in the world folder, one per UTC day): every admin command tried without a
   login, every /login from an id not on the admin list, every failed /login, every lockout, every login and logout. One line each,
   tab-separated: UTC time, event, the connection's address, the claimed Steam id, the player's name, the command's NAME only, the
   result. The line is built here from those fields alone - no password or /login argument ever reaches it. Text fields are made safe
   (printable ASCII, no tabs or quotes, capped), so a player cannot forge a line. Bounded: a per-connection line budget
   (logthrottle.h LineTake; the next written line says how many were held back) and kSecDayBytesMax per day file; the newest
   kSecKeepFiles day files are kept. The player who tried is told nothing more than before ("for server admins only"). */
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include "authproof.h"   /* SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, SameBytes (constant time), Hex / Unhex */

namespace adminauth {

const unsigned kMsgAdminLogin = 103;   /* both ways: {u8 kind, ...} - kinds below */
enum { kKindAsk = 1, kKindChallenge = 2, kKindProof = 3, kKindResult = 4, kKindLogout = 5 };
const size_t kNonceBytes = 16, kSaltBytes = 16, kKeyBytes = 32, kProofBytes = 32;
const unsigned kIterDefault = 200000, kIterMin = 100000, kIterMax = 2000000;
const double kChallengeSec = 30.0;     /* a challenge's life */
const double kIdleSec = 1800.0;        /* elevation ends after this long without an admin command */
const double kAskBurst = 5.0, kAskPerSec = 0.05;   /* /login asks per connection: 5 at once, then one every 20 s */
static const char kProofLabel[] = "SW-ADMIN-1";
static const char kAuthFile[] = "admin-auth.txt";

/* ---- the typed line (the game's) ---- */
inline std::string Lower(const std::string& s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a'); return o; }
inline bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
/* the first word of a line (leading spaces skipped), lower case */
inline std::string FirstWord(const std::string& t)
{
    size_t a = 0; while (a < t.size() && Space(t[a])) ++a;
    size_t b = a; while (b < t.size() && !Space(t[b])) ++b;
    return Lower(t.substr(a, b - a));
}
/* "/login ..." - taken by the game on every world and never sent as chat; "/logout" likewise */
inline bool IsLoginLine(const std::string& t) { return FirstWord(t) == "/login"; }
inline bool IsLogoutLine(const std::string& t) { return FirstWord(t) == "/logout"; }
/* the password: everything after "/login" and one run of spaces, with trailing spaces dropped. Empty = none given. */
inline std::string LoginPassword(const std::string& t)
{
    size_t a = 0; while (a < t.size() && Space(t[a])) ++a;
    while (a < t.size() && !Space(t[a])) ++a;
    while (a < t.size() && Space(t[a])) ++a;
    size_t b = t.size(); while (b > a && Space(t[b - 1])) --b;
    return t.substr(a, b - a);
}

/* ---- the key and the proof ---- */
inline void DeriveKey(const std::string& password, const unsigned char salt[kSaltBytes], unsigned iterations, unsigned char key[kKeyBytes])
{
    authproof::Pbkdf2Sha256(password.empty() ? 0 : password.data(), password.size(), salt, kSaltBytes, iterations, key, kKeyBytes);
}
inline void MakeProof(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned session, unsigned char proof[kProofBytes])
{
    unsigned char tail[4 + sizeof(kProofLabel) - 1];
    for (int i = 0; i < 4; ++i) tail[i] = (unsigned char)(session >> (8 * i));
    std::memcpy(tail + 4, kProofLabel, sizeof(kProofLabel) - 1);
    authproof::HmacKey hk;
    authproof::HmacPrepare(&hk, key, kKeyBytes);
    authproof::HmacWith(hk, nonce, kNonceBytes, tail, sizeof(tail), proof);
}
inline bool ProofMatches(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned session, const unsigned char* proof, size_t proofLen)
{
    if (proof == 0 || proofLen != kProofBytes) return false;
    unsigned char want[kProofBytes];
    MakeProof(key, nonce, session, want);
    const bool same = authproof::SameBytes(want, proof, kProofBytes);
    std::memset(want, 0, sizeof(want));
    return same;
}

/* ---- admin-auth.txt: "adm1 <iterations> <salt hex> <key hex>"; # starts a comment ---- */
inline std::string AuthLine(unsigned iterations, const unsigned char salt[kSaltBytes], const unsigned char key[kKeyBytes])
{
    char it[16]; std::sprintf(it, "%u", iterations);
    return std::string("adm1 ") + it + " " + authproof::Hex(salt, kSaltBytes) + " " + authproof::Hex(key, kKeyBytes);
}
enum { kFileNone = 0, kFileKey = 1, kFileBad = 2 };
/* kFileNone: no key line. kFileKey: the first key line. kFileBad: anything else that is not a comment, a second key line, or an
   iteration count outside [kIterMin, kIterMax] - the server then refuses every admin command, as with none */
inline int AuthTextParse(const std::string& text, unsigned* iterations, unsigned char salt[kSaltBytes], unsigned char key[kKeyBytes])
{
    int found = 0, bad = 0;
    size_t at = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at); if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        size_t a = 0; while (a < line.size() && Space(line[a])) ++a;
        size_t b = line.size(); while (b > a && Space(line[b - 1])) --b;
        line = line.substr(a, b - a);
        if (line.empty() || line[0] == '#') continue;
        std::string f[5]; int nf = 0; size_t p = 0;
        while (p < line.size() && nf < 5)
        {
            while (p < line.size() && Space(line[p])) ++p;
            if (p >= line.size()) break;
            size_t q = p; while (q < line.size() && !Space(line[q])) ++q;
            f[nf++] = line.substr(p, q - p); p = q;
        }
        unsigned long it = 0; bool itOk = nf == 4 && !f[1].empty() && f[1].size() <= 8;
        for (size_t i = 0; itOk && i < f[1].size(); ++i) { if (f[1][i] < '0' || f[1][i] > '9') itOk = false; else it = it * 10 + (unsigned long)(f[1][i] - '0'); }
        unsigned char s[kSaltBytes], k[kKeyBytes];
        if (found || nf != 4 || f[0] != "adm1" || !itOk || it < kIterMin || it > kIterMax || !authproof::Unhex(f[2], s, kSaltBytes) || !authproof::Unhex(f[3], k, kKeyBytes)) { ++bad; continue; }
        *iterations = (unsigned)it; std::memcpy(salt, s, kSaltBytes); std::memcpy(key, k, kKeyBytes); found = 1;
    }
    if (bad) return kFileBad;
    return found ? kFileKey : kFileNone;
}

/* ---- the wire: {u8 kind, ...}; u32 little-endian ---- */
inline void PutU32(std::vector<char>* b, unsigned v) { for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)(v >> (8 * i))); }
inline unsigned GetU32At(const char* p) { unsigned v = 0; for (int i = 0; i < 4; ++i) v |= (unsigned)(unsigned char)p[i] << (8 * i); return v; }
inline void EncodeKind(int kind, std::vector<char>* b) { b->assign(1, (char)kind); }
inline int KindOf(const char* p, size_t n) { return (p == 0 || n == 0) ? 0 : (int)(unsigned char)p[0]; }
/* ASK and LOGOUT are the kind byte alone */
inline void EncodeChallenge(const unsigned char nonce[kNonceBytes], const unsigned char salt[kSaltBytes], unsigned iterations, unsigned session, std::vector<char>* b)
{
    EncodeKind(kKindChallenge, b);
    b->insert(b->end(), (const char*)nonce, (const char*)nonce + kNonceBytes);
    b->insert(b->end(), (const char*)salt, (const char*)salt + kSaltBytes);
    PutU32(b, iterations); PutU32(b, session);
}
const size_t kChallengeLen = 1 + kNonceBytes + kSaltBytes + 8;
/* the game's side: a challenge whose iteration count is outside [kIterMin, kIterMax] is refused (a server cannot make a game spin) */
inline bool DecodeChallenge(const char* p, size_t n, unsigned char nonce[kNonceBytes], unsigned char salt[kSaltBytes], unsigned* iterations, unsigned* session)
{
    if (n != kChallengeLen || KindOf(p, n) != kKindChallenge) return false;
    std::memcpy(nonce, p + 1, kNonceBytes);
    std::memcpy(salt, p + 1 + kNonceBytes, kSaltBytes);
    *iterations = GetU32At(p + 1 + kNonceBytes + kSaltBytes);
    *session = GetU32At(p + 1 + kNonceBytes + kSaltBytes + 4);
    return *iterations >= kIterMin && *iterations <= kIterMax;
}
inline void EncodeProof(const unsigned char proof[kProofBytes], std::vector<char>* b)
{
    EncodeKind(kKindProof, b);
    b->insert(b->end(), (const char*)proof, (const char*)proof + kProofBytes);
}
inline bool DecodeProof(const char* p, size_t n, unsigned char proof[kProofBytes])
{
    if (n != 1 + kProofBytes || KindOf(p, n) != kKindProof) return false;
    std::memcpy(proof, p + 1, kProofBytes);
    return true;
}
enum { kResOk = 0, kResNotAdmin = 1, kResWrong = 2, kResLocked = 3, kResNoPassword = 4, kResNoChallenge = 5, kResBusy = 6, kResLoggedOut = 7 };
inline void EncodeResult(unsigned code, std::vector<char>* b) { EncodeKind(kKindResult, b); PutU32(b, code); }
inline bool DecodeResult(const char* p, size_t n, unsigned* code) { if (n != 5 || KindOf(p, n) != kKindResult) return false; *code = GetU32At(p + 1); return true; }
/* the words the game shows. kResNotAdmin is the same sentence an admin command gets from a player who is not an admin. */
inline std::string ResultWords(unsigned code)
{
    switch (code)
    {
    case kResOk:          return "Logged in as admin. Admin commands work until you /logout, leave, or go 30 minutes without one.";
    case kResNotAdmin:    return "Sorry, /login is for server admins only.";
    case kResWrong:       return "Wrong admin password.";
    case kResLocked:      return "Too many wrong admin passwords - wait a minute and try again.";
    case kResNoPassword:  return "This server has no admin password set, so admin commands are switched off. The server's owner sets one with --set-admin-password.";
    case kResNoChallenge: return "The login took too long or was not asked for - type /login again.";
    case kResBusy:        return "Too many /login tries - wait a little and try again.";
    case kResLoggedOut:   return "Logged out: admin commands are off for you until you /login again.";
    default:              return "The login was refused.";
    }
}

/* ---- a connection's login state (the server's) ---- */
struct Session
{
    int pending;                          /* a challenge is out */
    unsigned char nonce[kNonceBytes];
    double askedAt;
    int elevated;                         /* logged in */
    double lastUse;                       /* the login, then each admin command */
    Session() : pending(0), askedAt(0.0), elevated(0), lastUse(0.0) { std::memset(nonce, 0, sizeof(nonce)); }
};
/* true = this connection holds admin rights now; an idle one is ended here (*idleEnded = 1, once) */
inline bool ElevatedNow(Session* s, double now, int* idleEnded)
{
    *idleEnded = 0;
    if (!s->elevated) return false;
    if (now - s->lastUse >= kIdleSec) { s->elevated = 0; *idleEnded = 1; return false; }
    return true;
}
/* a challenge taken for its one proof: false = none out, or too old (either way it is gone now) */
inline bool TakeChallenge(Session* s, double now, unsigned char nonce[kNonceBytes])
{
    if (!s->pending) return false;
    s->pending = 0;
    const bool fresh = now - s->askedAt >= 0.0 && now - s->askedAt <= kChallengeSec;
    std::memcpy(nonce, s->nonce, kNonceBytes);
    std::memset(s->nonce, 0, kNonceBytes);
    return fresh;
}

/* ---- the admin security log ---- */
enum { kEvCmdNoLogin = 1, kEvCmdNotListed = 2, kEvLoginNotListed = 3, kEvLoginFailed = 4, kEvLockout = 5, kEvLoginOk = 6, kEvLogout = 7,
       kEvLoginNoPassword = 8, kEvCmdNoPassword = 9, kEvLoginLocked = 10, kEvSuppressed = 11 };
inline const char* EventName(int e)
{
    switch (e)
    {
    case kEvCmdNoLogin:      return "cmd-without-login";
    case kEvCmdNotListed:    return "cmd-id-not-on-admin-list";
    case kEvLoginNotListed:  return "login-id-not-on-admin-list";
    case kEvLoginFailed:     return "login-failed";
    case kEvLockout:         return "lockout";
    case kEvLoginOk:         return "login-ok";
    case kEvLogout:          return "logout";
    case kEvLoginNoPassword: return "login-no-admin-password-set";
    case kEvCmdNoPassword:   return "cmd-no-admin-password-set";
    case kEvLoginLocked:     return "login-while-locked";
    case kEvSuppressed:      return "suppressed";
    default:                 return "unknown";
    }
}
const size_t kFieldMax = 48, kCmdMax = 24;
/* a text field made safe for one line: printable ASCII only (tab, quote and backslash become '?'), at most `cap` bytes, "-" when empty */
inline std::string CleanField(const std::string& s, size_t cap)
{
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < cap; ++i)
    {
        const unsigned char c = (unsigned char)s[i];
        o += (c >= 0x20 && c <= 0x7E && c != '"' && c != '\\') ? (char)c : '?';
    }
    if (s.size() > o.size()) o += "...";
    return o.empty() ? std::string("-") : o;
}
/* a command's NAME only: its first word ("/heal"), never what follows it - so "/login <password>" is written "/login" */
inline std::string CmdField(const std::string& typed)
{
    size_t a = 0; while (a < typed.size() && Space(typed[a])) ++a;
    size_t b = a; while (b < typed.size() && !Space(typed[b])) ++b;
    return CleanField(typed.substr(a, b - a), kCmdMax);
}
/* one line: utc \t event \t addr \t steam \t "name" \t cmd \t result [\t suppressed-before=N] (no newline) */
inline std::string SecLine(const std::string& utc, int event, const std::string& addr, unsigned long long steam, const std::string& name,
                           const std::string& cmdTyped, const std::string& result, long long suppressedBefore)
{
    char id[24]; std::sprintf(id, "%llu", steam);
    std::string l = CleanField(utc, 24) + "\t" + EventName(event) + "\taddr=" + CleanField(addr, 24) + "\tsteam=" + (steam ? std::string(id) : std::string("none"))
        + "\tname=\"" + CleanField(name, kFieldMax) + "\"\tcmd=" + CmdField(cmdTyped) + "\tresult=" + CleanField(result, kFieldMax);
    if (suppressedBefore > 0) { char n[32]; std::sprintf(n, "%lld", suppressedBefore); l += std::string("\tsuppressed-before=") + n; }
    return l;
}
const long long kSecDayBytesMax = 16LL * 1024 * 1024;   /* one day file's most */
const int kSecKeepFiles = 90;                            /* day files kept */
/* "admin-security-YYYYMMDD.log" */
inline bool IsSecLogName(const std::string& n)
{
    static const char kPre[] = "admin-security-";
    const size_t pl = sizeof(kPre) - 1;
    if (n.size() != pl + 8 + 4 || n.compare(0, pl, kPre) != 0 || n.compare(pl + 8, 4, ".log") != 0) return false;
    for (size_t i = pl; i < pl + 8; ++i) if (n[i] < '0' || n[i] > '9') return false;
    return true;
}
/* the day files past the newest `keep` (names sort by date), oldest first */
inline std::vector<std::string> SecLogPrune(const std::vector<std::string>& names, int keep)
{
    std::vector<std::string> mine;
    for (size_t i = 0; i < names.size(); ++i) if (IsSecLogName(names[i])) mine.push_back(names[i]);
    for (size_t i = 1; i < mine.size(); ++i) for (size_t j = i; j > 0 && mine[j] < mine[j - 1]; --j) mine[j].swap(mine[j - 1]);
    std::vector<std::string> gone;
    if (keep <= 0 || (int)mine.size() <= keep) return gone;
    gone.assign(mine.begin(), mine.end() - keep);
    return gone;
}

}   /* namespace adminauth */
