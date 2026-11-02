/* src/common/adminauth.h - THE ADMIN LOGIN AND THE ADMIN SECURITY LOG. Pure: no Windows.h, no disk, no clock of its own (every call
   takes `now` in seconds), no engine; the world server (store_main.cpp), the game and the offline suite compile it. C++03 (VS2010 v100).
   Ported from public pull request 3 (author MassDivide) and changed for this project.

   WHO IS AN ADMIN NOW (the world server decides; admincmd.h holds the command rules):
     - the owner's game on the connection that proved this hosting's secret (HOST_TOKEN) - no password;
     - any other connection only while it is LOGGED IN: its person id is the owner's or is listed in admins.txt, and it gave the
       admin password with /login. The owner on another computer logs in like any admin.
   A login ends on /logout, when the connection goes, and after kIdleSec without an admin command.

   THE PASSWORD NEVER LEAVES THE GAME AT LOGIN. "/login <password>" typed in chat is taken by the game itself and never sent as chat
   or as a command. The game asks for a challenge (ASK); the server answers with a fresh random nonce, the salt and iteration count
   from admin-auth.txt and this connection's session number (CHALLENGE); the game sends back only
       key   = PBKDF2-HMAC-SHA256(password, salt, iterations, 32 bytes)
       proof = HMAC-SHA256(key, nonce || session (u32 LE) || u16 LE n || n bytes of the world id || kProofLabel)
   where the world id is the one the game was WELCOMEd with, so a proof made for one world is worth nothing on another (PROOF).
   A challenge is good for one proof within kChallengeSec on the connection it was made for.

   WRONG PASSWORDS (LockTable): kWrongMax wrong proofs lock the ADDRESS they came from (LoginLockKey: the server's own key for the
   connection, never the person id a game reports) for kLockFirstSec, doubled at each further lock, at most kLockMaxSec. A right
   proof clears it; a key with no wrong try for kForgetSec is forgotten.

   admin-auth.txt (the world folder): "adm1 <iterations> <salt hex> <key hex>" - PASSWORD-EQUIVALENT (whoever reads it can make
   proofs). Written by the world server when the owner's game sets the password (admincmd.h kVerbSetPassword, at least
   kPasswordMinChars characters, checked by the server).

   THE ADMIN SECURITY LOG (admin-security-YYYYMMDD.log in the world folder, one per UTC day): one tab-separated line per event, built
   here from its fields alone - no password ever reaches it; text fields are made safe so a player cannot forge a line. Lines a
   player can cause without limit (refused commands, /login asks over the ask allowance, proofs that answer no challenge) take from a
   budget kept PER ADDRESS (a reconnect does not refill it); a held count is written with that address's next line, when its
   connection goes and at the world server's quit. Failed login tries (FailedTry: a wrong password, a player not on the list, a try
   while locked, a lockout) come only from an ask the allowance let through and are always written, as are logins, logouts, kicks,
   bans, unbans, admin list changes, the password set and a stop (AlwaysWritten). One day file is at most kSecDayBytesMax; the
   newest kSecKeepFiles day files are kept. */
#ifndef SW_ADMINAUTH_H
#define SW_ADMINAUTH_H

#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdio>
#include "authproof.h"   /* SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, SameBytes (constant time), Hex / Unhex */
#include "storegate.h"   /* storegate::Allowance / AllowanceTake */

namespace adminauth {

const unsigned kMsgAdminLogin = 103;   /* both ways: {u8 kind, ...} - kinds below */
enum { kKindAsk = 1, kKindChallenge = 2, kKindProof = 3, kKindResult = 4, kKindLogout = 5 };
const size_t kNonceBytes = 16, kSaltBytes = 16, kKeyBytes = 32, kProofBytes = 32;
/* kIterMax: the most key rounds a CHALLENGE may ask of the game, whose main thread runs them at /login. Each round is two SHA-256
   blocks in this portable code, about 2 microseconds on a slow PC, so 200,000 rounds stay under about half a second there. The
   server writes kIterDefault and reads no admin-auth.txt past kIterMax; the game refuses a CHALLENGE past it. */
const unsigned kIterDefault = 100000, kIterMin = 100000, kIterMax = 200000;
typedef char AdminIterDefaultInRange[(kIterDefault >= kIterMin && kIterDefault <= kIterMax) ? 1 : -1];   /* a compile error here = the written default is outside the limits */
const double kChallengeSec = 30.0;     /* a challenge's life */
const double kIdleSec = 1800.0;        /* a login ends after this long without an admin command */
const double kAskBurst = 5.0, kAskPerSec = 0.05;   /* /login asks per connection: 5 at once, then one every 20 s */
const size_t kWorldIdMax = 255;        /* the world id the proof covers is cut to this many bytes */
const size_t kPasswordMinChars = 8, kPasswordMaxBytes = 128;
static const char kProofLabel[] = "SW-ADMIN-2";
static const char kAuthFile[] = "admin-auth.txt";

/* ---- the typed line (the game's chat box) ---- */
inline std::string Lower(const std::string& s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a'); return o; }
inline bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
/* the first word of a line (leading spaces skipped), lower case */
inline std::string FirstWord(const std::string& t)
{
    size_t a = 0; while (a < t.size() && Space(t[a])) ++a;
    size_t b = a; while (b < t.size() && !Space(t[b])) ++b;
    return Lower(t.substr(a, b - a));
}
/* a line whose first character past leading spaces is '/': a command, never chat */
inline bool SlashLine(const std::string& t)
{
    size_t a = 0; while (a < t.size() && Space(t[a])) ++a;
    return a < t.size() && t[a] == '/';
}
enum { kRouteChat = 0, kRouteLogin = 1, kRouteLogout = 2, kRouteCommand = 3 };
/* where a typed line goes: chat, the login, the logout, or a command (every other slash line - an unknown one is answered by the
   game, never sent as chat). The world server also drops any chat line that is a slash line (SlashLine). */
inline int SlashRoute(const std::string& t)
{
    if (!SlashLine(t)) return kRouteChat;
    const std::string w = FirstWord(t);
    if (w == "/login") return kRouteLogin;
    if (w == "/logout") return kRouteLogout;
    return kRouteCommand;
}
/* the password: everything after "/login" and one run of spaces, with trailing spaces dropped. Empty = none given. */
inline std::string LoginPassword(const std::string& t)
{
    size_t a = 0; while (a < t.size() && Space(t[a])) ++a;
    while (a < t.size() && !Space(t[a])) ++a;
    while (a < t.size() && Space(t[a])) ++a;
    size_t b = t.size(); while (b > a && Space(t[b - 1])) --b;
    return t.substr(a, b - a);
}
/* characters (UTF-8 code points: every byte that is not a continuation byte) */
inline size_t CharCount(const std::string& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) if (((unsigned char)s[i] & 0xC0) != 0x80) ++n;
    return n;
}
/* the admin password's rule, on both sides: at least kPasswordMinChars characters, at most kPasswordMaxBytes bytes, no control byte */
inline bool PasswordLongEnough(const std::string& pw)
{
    if (pw.size() > kPasswordMaxBytes || CharCount(pw) < kPasswordMinChars) return false;
    for (size_t i = 0; i < pw.size(); ++i) if ((unsigned char)pw[i] < 0x20 || (unsigned char)pw[i] == 0x7F) return false;
    return true;
}

/* ---- the key and the proof ---- */
inline void DeriveKey(const std::string& password, const unsigned char salt[kSaltBytes], unsigned iterations, unsigned char key[kKeyBytes])
{
    authproof::Pbkdf2Sha256(password.empty() ? 0 : password.data(), password.size(), salt, kSaltBytes, iterations, key, kKeyBytes);
}
/* the bytes after the nonce: session, the world id (cut to kWorldIdMax) and the label */
inline std::string ProofTail(unsigned session, const std::string& worldId)
{
    const std::string w = worldId.size() > kWorldIdMax ? worldId.substr(0, kWorldIdMax) : worldId;
    std::string t;
    for (int i = 0; i < 4; ++i) t += (char)(unsigned char)(session >> (8 * i));
    t += (char)(unsigned char)(w.size() & 0xFF); t += (char)(unsigned char)((w.size() >> 8) & 0xFF);
    t += w;
    t.append(kProofLabel, sizeof(kProofLabel) - 1);
    return t;
}
inline void MakeProof(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned session, const std::string& worldId, unsigned char proof[kProofBytes])
{
    const std::string tail = ProofTail(session, worldId);
    authproof::HmacKey hk;
    authproof::HmacPrepare(&hk, key, kKeyBytes);
    authproof::HmacWith(hk, nonce, kNonceBytes, tail.data(), tail.size(), proof);
}
inline bool ProofMatches(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned session, const std::string& worldId, const unsigned char* proof, size_t proofLen)
{
    if (proof == 0 || proofLen != kProofBytes) return false;
    unsigned char want[kProofBytes];
    MakeProof(key, nonce, session, worldId, want);
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
/* the file's three answers (the server adds a fourth, kFileUnreadable, when the file is there but cannot be read) */
enum { kFileNone = 0, kFileKey = 1, kFileBad = 2, kFileUnreadable = 3 };
/* kFileNone: no key line. kFileKey: the one key line. kFileBad: anything else that is not a comment, a second key line, or an
   iteration count outside [kIterMin, kIterMax] - the server then lets nobody log in, as with none */
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

/* ---- the wire (103): {u8 kind, ...}; u32 little-endian ----
   ASK (up)        {u8 1, u32 ask}                                               - 5 bytes; ask = the game's number for this /login
   CHALLENGE (down){u8 2, nonce 16, salt 16, u32 iterations, u32 session, u32 ask} - 45 bytes; ask = the ASK's number, echoed
   PROOF (up)      {u8 3, proof 32}                                              - 33 bytes
   RESULT (down)   {u8 4, u32 code, u32 wait seconds (kResLocked; else 0)}       - 9 bytes
   LOGOUT (up)     {u8 5} */
inline void PutU32(std::vector<char>* b, unsigned v) { for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)(v >> (8 * i))); }
inline unsigned GetU32At(const char* p) { unsigned v = 0; for (int i = 0; i < 4; ++i) v |= (unsigned)(unsigned char)p[i] << (8 * i); return v; }
inline void EncodeKind(int kind, std::vector<char>* b) { b->assign(1, (char)kind); }
inline int KindOf(const char* p, size_t n) { return (p == 0 || n == 0) ? 0 : (int)(unsigned char)p[0]; }
/* LOGOUT: the kind byte alone, nothing after it */
inline bool DecodeBare(const char* p, size_t n, int kind) { return n == 1 && KindOf(p, n) == kind; }
/* ASK: the game numbers each /login's ask; the CHALLENGE echoes the number and the game answers only the challenge for its latest
   ask, so a second /login typed before the first is answered replaces it (the older challenge is ignored: never answered with the
   newer password, so never counted as a wrong one) */
const size_t kAskLen = 5;
inline void EncodeAsk(unsigned ask, std::vector<char>* b) { EncodeKind(kKindAsk, b); PutU32(b, ask); }
inline bool DecodeAsk(const char* p, size_t n, unsigned* ask)
{
    if (n != kAskLen || KindOf(p, n) != kKindAsk) return false;
    *ask = GetU32At(p + 1);
    return true;
}
/* the game's next ask number: from 1, never 0 (0 = no ask made yet) */
inline unsigned NextAskNumber(unsigned last) { return last + 1u == 0u ? 1u : last + 1u; }
/* the game's side: a CHALLENGE is answered only when it echoes the latest ask */
inline bool ChallengeIsLatest(unsigned challengeAsk, unsigned latestAsk) { return latestAsk != 0u && challengeAsk == latestAsk; }
const size_t kChallengeLen = 1 + kNonceBytes + kSaltBytes + 12;
inline void EncodeChallenge(const unsigned char nonce[kNonceBytes], const unsigned char salt[kSaltBytes], unsigned iterations, unsigned session, unsigned ask, std::vector<char>* b)
{
    EncodeKind(kKindChallenge, b);
    b->insert(b->end(), (const char*)nonce, (const char*)nonce + kNonceBytes);
    b->insert(b->end(), (const char*)salt, (const char*)salt + kSaltBytes);
    PutU32(b, iterations); PutU32(b, session); PutU32(b, ask);
}
/* the game's side: a challenge whose iteration count is outside [kIterMin, kIterMax] is refused (a server cannot make a game spin) */
inline bool DecodeChallenge(const char* p, size_t n, unsigned char nonce[kNonceBytes], unsigned char salt[kSaltBytes], unsigned* iterations, unsigned* session, unsigned* ask)
{
    if (n != kChallengeLen || KindOf(p, n) != kKindChallenge) return false;
    std::memcpy(nonce, p + 1, kNonceBytes);
    std::memcpy(salt, p + 1 + kNonceBytes, kSaltBytes);
    *iterations = GetU32At(p + 1 + kNonceBytes + kSaltBytes);
    *session = GetU32At(p + 1 + kNonceBytes + kSaltBytes + 4);
    *ask = GetU32At(p + 1 + kNonceBytes + kSaltBytes + 8);
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
/* RESULT codes - the game picks the approved words for each */
enum { kResOk = 0,            /* logged in */
       kResNotAdmin = 1,      /* this player is not the owner and not on the admin list */
       kResWrong = 2,         /* wrong admin password */
       kResLocked = 3,        /* too many wrong passwords: wait `wait` seconds */
       kResNoPassword = 4,    /* no admin password is set for this world (or its file cannot be read): nobody can log in */
       kResNoChallenge = 5,   /* the login took too long or was not asked for - type /login again */
       kResBusy = 6,          /* too many /login asks - wait a little */
       kResLoggedOut = 7,     /* logged out */
       kResOwnerNoNeed = 8,   /* the owner's own game (it proved the hosting's secret) needs no login */
       kResCount = 9 };
const size_t kResultLen = 9;
inline void EncodeResult(unsigned code, unsigned waitSec, std::vector<char>* b) { EncodeKind(kKindResult, b); PutU32(b, code); PutU32(b, waitSec); }
inline bool DecodeResult(const char* p, size_t n, unsigned* code, unsigned* waitSec)
{
    if (n != kResultLen || KindOf(p, n) != kKindResult) return false;
    *code = GetU32At(p + 1); *waitSec = GetU32At(p + 5);
    return *code < (unsigned)kResCount;
}

/* ---- a connection's login state (the server's) ---- */
struct Session
{
    int pending;                          /* a challenge is out */
    unsigned char nonce[kNonceBytes];
    double askedAt;
    int elevated;                         /* logged in */
    double lastUse;                       /* the login, then each admin command */
    std::string person;                   /* the person id that logged in: a login holds only while the connection still names it */
    Session() : pending(0), askedAt(0.0), elevated(0), lastUse(0.0) { std::memset(nonce, 0, sizeof(nonce)); }
};
/* true = this connection is logged in now (as `person`); an idle one is ended here (*idleEnded = 1, once). A connection whose person id
   is no longer the one that logged in (or is no longer the owner's or listed: stillListed false) is logged out too. */
inline bool ElevatedNow(Session* s, double now, const std::string& person, bool stillListed, int* idleEnded)
{
    *idleEnded = 0;
    if (!s->elevated) return false;
    if (person.empty() || person != s->person || !stillListed) { s->elevated = 0; s->person.clear(); return false; }
    if (now - s->lastUse >= kIdleSec || now < s->lastUse) { s->elevated = 0; s->person.clear(); *idleEnded = 1; return false; }
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

/* ---- wrong passwords: a growing lock per address ---- */
const int kWrongMax = 5;
const double kLockFirstSec = 120.0, kLockMaxSec = 86400.0;
const double kForgetSec = 86400.0;   /* a key quiet this long (no wrong try, and its last lock over) is forgotten, its lock count too */
struct LockSlot { int wrong; int locks; double lockedUntil; double lastWrong; LockSlot() : wrong(0), locks(0), lockedUntil(0.0), lastWrong(0.0) {} };
typedef std::map<std::string, LockSlot> LockTable;
/* the lock length for the n-th lock (n from 1): kLockFirstSec doubled n - 1 times, at most kLockMaxSec */
inline double LockLength(int n)
{
    double s = kLockFirstSec;
    for (int i = 1; i < n && s < kLockMaxSec; ++i) s *= 2.0;
    return s > kLockMaxSec ? kLockMaxSec : s;
}
/* seconds left on this key's lock, rounded up; 0 = not locked */
inline unsigned LockWait(const LockTable& t, const std::string& key, double now)
{
    LockTable::const_iterator it = t.find(key);
    if (it == t.end() || it->second.lockedUntil <= now) return 0;
    const double left = it->second.lockedUntil - now;
    unsigned w = (unsigned)left; if ((double)w < left) ++w;
    return w == 0 ? 1u : w;
}
/* the key a login's wrong passwords count against: the connection's address key alone (the server's AuthLockKey - the Steam-checked
   id for a bridged joiner, else the IP address). Never the person id a game reports, so a game that names a listed admin's id
   cannot lock that admin out. */
inline std::string LoginLockKey(const std::string& addrKey) { return "admin:" + addrKey; }
/* quiet since: the later of the last wrong try and the end of the last lock */
inline double QuietSince(const LockSlot& s) { return s.lockedUntil > s.lastWrong ? s.lockedUntil : s.lastWrong; }
/* one wrong proof on a key; true = it is locked now (the kWrongMax-th wrong try since its last lock) */
inline bool NoteWrongKey(LockTable* t, const std::string& key, double now)
{
    if (key.empty()) return false;
    LockSlot& s = (*t)[key];
    if (s.lastWrong > 0.0 && now - QuietSince(s) >= kForgetSec) s = LockSlot();
    s.lastWrong = now;
    if (++s.wrong < kWrongMax) return false;
    s.wrong = 0; ++s.locks;
    s.lockedUntil = now + LockLength(s.locks);
    return true;
}
/* a right proof clears its key */
inline void NoteRight(LockTable* t, const std::string& key) { t->erase(key); }
/* forget keys quiet for kForgetSec (no wrong try, no lock running) */
inline void Expire(LockTable* t, double now)
{
    for (LockTable::iterator it = t->begin(); it != t->end();)
    {
        if (it->second.lockedUntil <= now && now - QuietSince(it->second) >= kForgetSec) t->erase(it++);
        else ++it;
    }
}

/* ---- the admin security log ---- */
enum { kEvCmdNoLogin = 1, kEvLoginNotListed = 2, kEvLoginFailed = 3, kEvLockout = 4, kEvLoginOk = 5, kEvLogout = 6,
       kEvLoginNoPassword = 7, kEvLoginLocked = 8, kEvSuppressed = 9, kEvKick = 10, kEvBan = 11, kEvUnban = 12, kEvAdminAdd = 13,
       kEvAdminRemove = 14, kEvPasswordSet = 15, kEvStop = 16, kEvCmdRefused = 17, kEvLoginBusy = 18, kEvLoginNoChallenge = 19 };
inline const char* EventName(int e)
{
    switch (e)
    {
    case kEvCmdNoLogin:      return "cmd-without-login";
    case kEvLoginNotListed:  return "login-not-admin";
    case kEvLoginFailed:     return "login-failed";
    case kEvLockout:         return "lockout";
    case kEvLoginOk:         return "login-ok";
    case kEvLogout:          return "logout";
    case kEvLoginNoPassword: return "login-no-admin-password-set";
    case kEvLoginLocked:     return "login-while-locked";
    case kEvSuppressed:      return "suppressed";
    case kEvKick:            return "kick";
    case kEvBan:             return "ban";
    case kEvUnban:           return "unban";
    case kEvAdminAdd:        return "admin-added";
    case kEvAdminRemove:     return "admin-removed";
    case kEvPasswordSet:     return "admin-password-set";
    case kEvStop:            return "stop";
    case kEvCmdRefused:      return "cmd-refused";
    case kEvLoginBusy:       return "login-too-many-asks";
    case kEvLoginNoChallenge: return "login-no-challenge";
    default:                 return "unknown";
    }
}
/* a failed login try: only an ask the allowance let through can cause one (a proof first takes that ask's challenge), and the lock
   ends a run of them */
inline bool FailedTry(int e) { return e == kEvLoginFailed || e == kEvLoginNotListed || e == kEvLoginLocked || e == kEvLockout; }
/* written whatever the address's budget: the events an admin or the owner answers for, and every failed login try */
inline bool AlwaysWritten(int e)
{
    return FailedTry(e) || e == kEvLoginOk || e == kEvLogout || e == kEvLockout || e == kEvKick || e == kEvBan || e == kEvUnban
        || e == kEvAdminAdd || e == kEvAdminRemove || e == kEvPasswordSet || e == kEvStop || e == kEvSuppressed;
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
/* a command's NAME only: its first word ("/kick"), never what follows it - so "/login <password>" is written "/login" */
inline std::string CmdField(const std::string& typed)
{
    size_t a = 0; while (a < typed.size() && Space(typed[a])) ++a;
    size_t b = a; while (b < typed.size() && !Space(typed[b])) ++b;
    return CleanField(typed.substr(a, b - a), kCmdMax);
}
/* one line: utc \t event \t addr \t person \t "name" \t cmd \t result [\t suppressed-before=N] (no newline). person = OUR player id. */
inline std::string SecLine(const std::string& utc, int event, const std::string& addr, const std::string& person, const std::string& name,
                           const std::string& cmdTyped, const std::string& result, long long suppressedBefore)
{
    std::string l = CleanField(utc, 24) + "\t" + EventName(event) + "\taddr=" + CleanField(addr, 24) + "\tperson=" + CleanField(person, 40)
        + "\tname=\"" + CleanField(name, kFieldMax) + "\"\tcmd=" + CmdField(cmdTyped) + "\tresult=" + CleanField(result, kFieldMax);
    if (suppressedBefore > 0) { char n[32]; std::sprintf(n, "%lld", suppressedBefore); l += std::string("\tsuppressed-before=") + n; }
    return l;
}
/* the budget for the lines one ADDRESS can cause: kLineBurst at once, then one every 1 / kLinePerSec seconds. Kept per address, so
   a reconnect does not refill it. A line over it is not written; the next one that is says how many were held back. */
const double kLineBurst = 20.0, kLinePerSec = 0.2;
struct LineBudget { storegate::Allowance a; long long held; LineBudget() : held(0) {} };
/* true = write this line; *heldBefore = the lines of this budget not written since the last one that was (then counted afresh).
   An AlwaysWritten event is written and takes nothing from the budget. */
inline bool LineTake(LineBudget* b, int event, double now, long long* heldBefore)
{
    *heldBefore = 0;
    if (!AlwaysWritten(event) && !storegate::AllowanceTake(&b->a, now, kLineBurst, kLinePerSec)) { ++b->held; return false; }
    *heldBefore = b->held;
    b->held = 0;
    return true;
}
const long long kSecDayBytesMax = 16LL * 1024 * 1024;   /* one day file's most */
const int kSecKeepFiles = 90;                            /* day files kept */
/* may `add` more bytes go into a day file that holds `have` bytes? A size not known (negative) is full. */
inline bool DayFileRoom(long long have, long long add, long long cap)
{
    if (have < 0 || add < 0 || cap < 0) return false;
    return add <= cap - have;
}
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

/* ---- ADMIN_STANDING (101, server -> game): a game's admin standing, as the world server holds it ----
   {u32 standing, u32 why} - 8 bytes. Sent to one game at its WELCOME (none, or owner when its connection proved this hosting's
   secret) and each time the world server changes that game's standing: a login taken, its /logout, the idle end, the admin password
   set (every login ends), its player no longer the owner or on the admin list (or the connection no longer naming the player that
   logged in). A game is an admin now while the last standing of its link says so; a new link is none until its first standing. */
const unsigned kMsgAdminStanding = 101;
enum { kStandNone = 0, kStandAdmin = 1, kStandOwner = 2, kStandCount = 3 };
enum { kWhyWelcome = 0,           /* the WELCOME: none, or owner */
       kWhyLogin = 1,             /* a right proof: admin */
       kWhyLogout = 2,            /* the player's own /logout */
       kWhyIdle = 3,              /* kIdleSec without an admin act */
       kWhyPasswordChanged = 4,   /* the admin password was set: every login ends */
       kWhyRemoved = 5,           /* no longer the owner or on the admin list, or the connection names another player */
       kWhyLinkEnded = 6,         /* the game's own: its link to the world server ended (never on the wire) */
       kWhyCount = 7 };
const size_t kStandingLen = 8;
inline void EncodeStanding(unsigned standing, unsigned why, std::vector<char>* b) { b->clear(); PutU32(b, standing); PutU32(b, why); }
/* the game's side: a known standing with a reason that fits it (owner or none at the WELCOME, admin at a login, none otherwise) */
inline bool DecodeStanding(const char* p, size_t n, unsigned* standing, unsigned* why)
{
    if (p == 0 || n != kStandingLen) return false;
    *standing = GetU32At(p); *why = GetU32At(p + 4);
    if (*standing >= (unsigned)kStandCount || *why >= (unsigned)kWhyLinkEnded) return false;
    if (*why == (unsigned)kWhyWelcome) return *standing != (unsigned)kStandAdmin;
    if (*why == (unsigned)kWhyLogin) return *standing == (unsigned)kStandAdmin;
    return *standing == (unsigned)kStandNone;
}
/* THE WORLD SERVER'S END OF A LOGIN, and the reason its game is told (standing none). passwordSet = the admin password was just set:
   every login ends (kWhyPasswordChanged) and every challenge out is dropped. Otherwise as ElevatedNow: kWhyIdle after kIdleSec
   without an admin act, kWhyRemoved when `person` is not the one that logged in or is no longer the owner's or listed (stillListed
   false). -1 = no login ended here (none was in, or it holds) - that game is told nothing. */
inline int LoginEnds(Session* s, bool passwordSet, double now, const std::string& person, bool stillListed)
{
    if (passwordSet) s->pending = 0;
    if (!s->elevated) return -1;
    if (passwordSet) { s->elevated = 0; s->person.clear(); return kWhyPasswordChanged; }
    int idle = 0;
    if (ElevatedNow(s, now, person, stillListed, &idle)) return -1;
    return idle ? kWhyIdle : kWhyRemoved;
}

}   /* namespace adminauth */

#endif
