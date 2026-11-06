/* src/common/authproof.h - THE WORLD'S PASSWORD, AS THE WORLD SERVER CHECKS IT. PURE: no Windows.h, no engine, no globals, no I/O, so
   the offline suite checks exactly what the world server and the plugin compile.

   The host types a password each time it hosts; nothing remembers it. The host's game derives a key from it and writes the world
   folder's auth.txt - one line: the format number, a fresh 16-byte salt and the 32-byte key, both in hex - and keeps the typed
   password in memory for that hosting session only. The world server never sees a password: when auth.txt holds a key, it sends
   every connection a fresh 16-byte nonce and that salt (AUTH_CHALLENGE), and the game answers with a proof (AUTH_PROOF) only the
   right password can make:
       key   = PBKDF2-HMAC-SHA256(password, salt, kIterations, 32 bytes)
       proof = HMAC-SHA256(key, nonce || kProofLabel)
   The nonce is new for every connection, so a proof seen on one connection is worth nothing on another. The proof is compared in
   constant time.
   THE OWNER AND THE ADMINS AT THE DOOR (owner 1369 b / 1379 / 1380): when the world's rules.txt holds an owner or an admin password,
   the challenge is sent even to a world with no world password, and it also carries the login keys' salt and rounds (the owner and
   admin keys share them, worldpw.h) and the world's id. A game that knows a login password for that world (the host's game the
   owner password from rules.txt; any game the one its last owner or admin /login succeeded with on a link to the same place, LoginFind) adds a login proof:
       login key   = the admin road's key (adminauth.h DeriveKey with that salt and those rounds)
       login proof = HMAC-SHA256(login key, nonce || kLoginDoorLabel)
   The server checks it against the owner key, then the admin key. A right owner proof is the owner's own game: no world password,
   access.txt, bans.txt or unreadable file keeps it out (DoorJudge). A right admin proof keeps every door check; it logs a listed
   admin in at its admission. A changed password simply fails the proof.
   THIS HOSTING'S SECRET (the token): 32 random bytes the host's game makes each time it starts its world server and passes on that
   server's command line; neither side ever logs it. The host's Steam bridge shows it in each STEAM_ID, which tells the world server
   which Steam-checked account a bridged connection is. It proves nothing about any game: the owner is proven by the owner password
   alone. Where a connection comes from proves nothing either (a port forwarder on the host's PC makes any outside player look
   local); accessrule.h decides what is believed. */
#ifndef SW_AUTHPROOF_H
#define SW_AUTHPROOF_H

#include <string>
#include <vector>
#include <cstring>
#include <cstddef>

namespace authproof {

/* ---- the three messages (world-server protocol 92; store_main.cpp and store.cpp check these numbers at compile time) ---- */
const unsigned int kMsgAuthChallenge = 65;   /* server -> game: nonce (16) | world salt (16) | u8 asks | login salt (16) | u32 login rounds | u8 world id length | world id */
const unsigned int kMsgAuthProof     = 66;   /* game -> server: u8 has | world proof (32) | login proof (32) - 65 bytes */
const unsigned int kMsgSteamId       = 67;   /* host's bridge -> server: token (32) | u64 Steam id, little-endian (8) - 40 bytes; id 0 = a bridged link whose id Steam did not check */
/* 68 is unused: HOST_TOKEN, the host's own game's door pass by this hosting's secret, was removed in T-1158 step 7b (owner 1369 b) */
const unsigned int kProtocol         = 145;   /* the world-server protocol that carries the three */







const size_t kSaltBytes = 16, kNonceBytes = 16, kKeyBytes = 32, kProofBytes = 32;
const size_t kTokenBytes = 32;              /* this hosting's secret */
const size_t kSteamIdBytes = kTokenBytes + 8;
const size_t kChallengeFixed = kNonceBytes + kSaltBytes + 1 + kSaltBytes + 4 + 1;   /* everything but the world id */
const size_t kDoorProofBytes = 1 + kProofBytes + kProofBytes;
const size_t kWorldIdMax = 255;
enum { kAskWorld = 1, kAskLogin = 2 };   /* a challenge's asks: the world password; the owner or an admin password */
enum { kHasWorld = 1, kHasLogin = 2 };   /* a proof's parts */
const unsigned int kIterations = 10000;
const unsigned int kAuthFormat = 1;          /* auth.txt's format number */
static const char kProofLabel[] = "SW-AUTH-1";
static const char kLoginDoorLabel[] = "SW-LOGIN-DOOR-1";
static const char kAuthFile[] = "auth.txt";

/* ---- SHA-256 (FIPS 180-4) ---- */
struct Sha256
{
    unsigned int h[8];
    unsigned char buf[64];
    size_t n;                  /* bytes waiting in buf */
    unsigned long long len;    /* bytes hashed in all */
};

inline unsigned int Ror32(unsigned int x, int r) { return (x >> r) | (x << (32 - r)); }

inline void Sha256Block(Sha256* s, const unsigned char* p)
{
    static const unsigned int K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
    unsigned int w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    for (int i = 16; i < 64; ++i)
    {
        const unsigned int s0 = Ror32(w[i - 15], 7) ^ Ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const unsigned int s1 = Ror32(w[i - 2], 17) ^ Ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    unsigned int a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 64; ++i)
    {
        const unsigned int S1 = Ror32(e, 6) ^ Ror32(e, 11) ^ Ror32(e, 25);
        const unsigned int ch = (e & f) ^ (~e & g);
        const unsigned int t1 = hh + S1 + ch + K[i] + w[i];
        const unsigned int S0 = Ror32(a, 2) ^ Ror32(a, 13) ^ Ror32(a, 22);
        const unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
        const unsigned int t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += hh;
}

inline void Sha256Init(Sha256* s)
{
    static const unsigned int H0[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    for (int i = 0; i < 8; ++i) s->h[i] = H0[i];
    s->n = 0; s->len = 0;
}

inline void Sha256Update(Sha256* s, const void* data, size_t len)
{
    const unsigned char* p = (const unsigned char*)data;
    s->len += len;
    while (len > 0)
    {
        if (s->n == 0 && len >= 64) { Sha256Block(s, p); p += 64; len -= 64; continue; }
        size_t take = 64 - s->n;
        if (take > len) take = len;
        std::memcpy(s->buf + s->n, p, take);
        s->n += take; p += take; len -= take;
        if (s->n == 64) { Sha256Block(s, s->buf); s->n = 0; }
    }
}

inline void Sha256Final(Sha256* s, unsigned char out[32])
{
    const unsigned long long bits = s->len * 8ULL;
    unsigned char pad = 0x80;
    Sha256Update(s, &pad, 1);
    pad = 0;
    while (s->n != 56) Sha256Update(s, &pad, 1);
    unsigned char lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = (unsigned char)(bits >> (56 - 8 * i));
    Sha256Update(s, lenBytes, 8);
    for (int i = 0; i < 8; ++i)
    {
        out[i * 4] = (unsigned char)(s->h[i] >> 24); out[i * 4 + 1] = (unsigned char)(s->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(s->h[i] >> 8); out[i * 4 + 3] = (unsigned char)s->h[i];
    }
}

/* ---- HMAC-SHA256 (RFC 2104), with the key's inner and outer states made once so PBKDF2's many rounds hash two blocks each ---- */
struct HmacKey { Sha256 inner, outer; };

inline void HmacPrepare(HmacKey* hk, const void* key, size_t keyLen)
{
    unsigned char k[64];
    std::memset(k, 0, sizeof(k));
    if (keyLen > 64) { Sha256 s; Sha256Init(&s); Sha256Update(&s, key, keyLen); Sha256Final(&s, k); }
    else if (keyLen > 0) std::memcpy(k, key, keyLen);
    unsigned char ip[64], op[64];
    for (int i = 0; i < 64; ++i) { ip[i] = (unsigned char)(k[i] ^ 0x36); op[i] = (unsigned char)(k[i] ^ 0x5c); }
    Sha256Init(&hk->inner); Sha256Update(&hk->inner, ip, 64);
    Sha256Init(&hk->outer); Sha256Update(&hk->outer, op, 64);
}

/* HMAC of (a || b) under a prepared key; either part may be empty */
inline void HmacWith(const HmacKey& hk, const void* a, size_t aLen, const void* b, size_t bLen, unsigned char out[32])
{
    Sha256 s = hk.inner;
    if (aLen > 0) Sha256Update(&s, a, aLen);
    if (bLen > 0) Sha256Update(&s, b, bLen);
    unsigned char ih[32];
    Sha256Final(&s, ih);
    Sha256 o = hk.outer;
    Sha256Update(&o, ih, 32);
    Sha256Final(&o, out);
}

inline void HmacSha256(const void* key, size_t keyLen, const void* msg, size_t msgLen, unsigned char out[32])
{
    HmacKey hk;
    HmacPrepare(&hk, key, keyLen);
    HmacWith(hk, msg, msgLen, 0, 0, out);
}

/* ---- PBKDF2-HMAC-SHA256 (RFC 8018), any output length ---- */
inline void Pbkdf2Sha256(const void* pw, size_t pwLen, const void* salt, size_t saltLen, unsigned int iterations,
                         unsigned char* out, size_t outLen)
{
    HmacKey hk;
    HmacPrepare(&hk, pw, pwLen);
    unsigned int block = 1;
    while (outLen > 0)
    {
        unsigned char be[4] = { (unsigned char)(block >> 24), (unsigned char)(block >> 16), (unsigned char)(block >> 8), (unsigned char)block };
        unsigned char u[32], t[32];
        HmacWith(hk, salt, saltLen, be, 4, u);
        std::memcpy(t, u, 32);
        for (unsigned int i = 1; i < iterations; ++i)
        {
            HmacWith(hk, u, 32, 0, 0, u);
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }
        const size_t take = outLen < 32 ? outLen : 32;
        std::memcpy(out, t, take);
        out += take; outLen -= take; ++block;
    }
}

/* ---- the world's key and a connection's proof ---- */
inline void DeriveKey(const std::string& password, const unsigned char salt[kSaltBytes], unsigned char key[kKeyBytes])
{
    Pbkdf2Sha256(password.empty() ? 0 : password.data(), password.size(), salt, kSaltBytes, kIterations, key, kKeyBytes);
}

inline void MakeProof(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned char proof[kProofBytes])
{
    HmacKey hk;
    HmacPrepare(&hk, key, kKeyBytes);
    HmacWith(hk, nonce, kNonceBytes, kProofLabel, sizeof(kProofLabel) - 1, proof);
}

/* Every byte is looked at whatever the first difference, so the time taken says nothing about where it was. */
inline bool SameBytes(const unsigned char* a, const unsigned char* b, size_t n)
{
    unsigned char d = 0;
    for (size_t i = 0; i < n; ++i) d = (unsigned char)(d | (a[i] ^ b[i]));
    return d == 0;
}

inline bool ProofMatches(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], const unsigned char* proof, size_t proofLen)
{
    if (proofLen != kProofBytes) return false;
    unsigned char want[kProofBytes];
    MakeProof(key, nonce, want);
    return SameBytes(want, proof, kProofBytes);
}

/* the login proof at the door: an owner or admin key (the admin road's, made with the challenge's login salt and rounds) over the nonce */
inline void LoginDoorProof(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], unsigned char proof[kProofBytes])
{
    HmacKey hk;
    HmacPrepare(&hk, key, kKeyBytes);
    HmacWith(hk, nonce, kNonceBytes, kLoginDoorLabel, sizeof(kLoginDoorLabel) - 1, proof);
}
inline bool LoginDoorProofMatches(const unsigned char key[kKeyBytes], const unsigned char nonce[kNonceBytes], const unsigned char proof[kProofBytes])
{
    unsigned char want[kProofBytes];
    LoginDoorProof(key, nonce, want);
    return SameBytes(want, proof, kProofBytes);
}

/* ---- the AUTH_CHALLENGE payload ---- */
struct Challenge
{
    unsigned char nonce[kNonceBytes];
    unsigned char salt[kSaltBytes];        /* the world password's salt (zeros when it is not asked) */
    unsigned asks;                         /* kAskWorld | kAskLogin */
    unsigned char ownerSalt[kSaltBytes];   /* the login keys' salt and rounds (zeros when no login is asked) */
    unsigned ownerRounds;
    std::string worldId;                   /* the world's id: a remembered login is shown only when it and the place dialled both match (LoginFind) */
    Challenge() : asks(0), ownerRounds(0) { std::memset(nonce, 0, sizeof nonce); std::memset(salt, 0, sizeof salt); std::memset(ownerSalt, 0, sizeof ownerSalt); }
};
inline void ChallengeEncode(const Challenge& c, std::vector<char>* out)
{
    out->assign((const char*)c.nonce, (const char*)c.nonce + kNonceBytes);
    out->insert(out->end(), (const char*)c.salt, (const char*)c.salt + kSaltBytes);
    out->push_back((char)(unsigned char)(c.asks & 3u));
    out->insert(out->end(), (const char*)c.ownerSalt, (const char*)c.ownerSalt + kSaltBytes);
    for (size_t i = 0; i < 4; ++i) out->push_back((char)(unsigned char)(c.ownerRounds >> (8 * i)));
    const size_t n = c.worldId.size() > kWorldIdMax ? kWorldIdMax : c.worldId.size();
    out->push_back((char)(unsigned char)n);
    out->insert(out->end(), c.worldId.begin(), c.worldId.begin() + (std::ptrdiff_t)n);
}
inline bool ChallengeDecode(const std::vector<char>& p, Challenge* c)
{
    if (p.size() < kChallengeFixed) return false;
    size_t at = 0;
    std::memcpy(c->nonce, &p[at], kNonceBytes); at += kNonceBytes;
    std::memcpy(c->salt, &p[at], kSaltBytes); at += kSaltBytes;
    c->asks = (unsigned char)p[at++];
    if ((c->asks & ~3u) != 0) return false;
    std::memcpy(c->ownerSalt, &p[at], kSaltBytes); at += kSaltBytes;
    c->ownerRounds = 0;
    for (size_t i = 0; i < 4; ++i) c->ownerRounds |= (unsigned)(unsigned char)p[at + i] << (8 * i);
    at += 4;
    const size_t n = (unsigned char)p[at++];
    if (p.size() != at + n) return false;
    c->worldId.assign(p.begin() + (std::ptrdiff_t)at, p.end());
    return true;
}

/* ---- the AUTH_PROOF payload: u8 has | world proof (32) | login proof (32); a part not given is zeros ---- */
inline void DoorProofEncode(unsigned has, const unsigned char world[kProofBytes], const unsigned char owner[kProofBytes], std::vector<char>* out)
{
    out->assign(1, (char)(unsigned char)(has & 3u));
    for (size_t i = 0; i < kProofBytes; ++i) out->push_back((has & kHasWorld) ? (char)world[i] : (char)0);
    for (size_t i = 0; i < kProofBytes; ++i) out->push_back((has & kHasLogin) ? (char)owner[i] : (char)0);
}
inline bool DoorProofDecode(const std::vector<char>& p, unsigned* has, unsigned char world[kProofBytes], unsigned char owner[kProofBytes])
{
    if (p.size() != kDoorProofBytes || ((unsigned char)p[0] & ~3u) != 0) return false;
    *has = (unsigned char)p[0];
    std::memcpy(world, &p[1], kProofBytes);
    std::memcpy(owner, &p[1 + kProofBytes], kProofBytes);
    return true;
}

/* ---- the STEAM_ID payload: token (32) | Steam id (8, little-endian) ---- */
inline void SteamIdEncode(const unsigned char token[kTokenBytes], unsigned long long id, std::vector<char>* out)
{
    out->assign((const char*)token, (const char*)token + kTokenBytes);
    for (size_t i = 0; i < 8; ++i) out->push_back((char)(unsigned char)(id >> (8 * i)));
}

inline bool SteamIdDecode(const std::vector<char>& p, unsigned char token[kTokenBytes], unsigned long long* id)
{
    if (p.size() != kSteamIdBytes) return false;
    std::memcpy(token, &p[0], kTokenBytes);
    unsigned long long v = 0;
    for (size_t i = 0; i < 8; ++i) v |= (unsigned long long)(unsigned char)p[kTokenBytes + i] << (8 * i);
    *id = v;
    return true;
}

/* ---- auth.txt: "<format> <salt hex> <key hex>"; lines starting with # are comments ---- */
inline std::string Hex(const unsigned char* p, size_t n)
{
    static const char kD[] = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += kD[p[i] >> 4]; s += kD[p[i] & 15]; }
    return s;
}

inline bool Unhex(const std::string& s, unsigned char* out, size_t n)
{
    if (s.size() != n * 2) return false;
    for (size_t i = 0; i < n * 2; ++i)
    {
        const char c = s[i];
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) return false;
        if (i % 2 == 0) out[i / 2] = (unsigned char)(v << 4); else out[i / 2] = (unsigned char)(out[i / 2] | v);
    }
    return true;
}

inline std::string AuthLine(const unsigned char salt[kSaltBytes], const unsigned char key[kKeyBytes])
{
    return "1 " + Hex(salt, kSaltBytes) + " " + Hex(key, kKeyBytes);
}

enum AuthTextVerdict { kAuthNone = 0, kAuthKey = 1, kAuthBad = 2 };

/* kAuthNone: no key line (an empty file, comments only). kAuthKey: the first key line, read whole. kAuthBad: a line that is not a
   comment and not a key line of format 1 - the world server cannot tell what the host meant, so it lets nobody in (badLines says
   how many). A key line after the first is counted as bad. */
inline int AuthTextParse(const std::string& text, unsigned char salt[kSaltBytes], unsigned char key[kKeyBytes], int* badLines)
{
    int bad = 0, found = 0;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t')) line.erase(line.size() - 1);
        size_t lead = 0;
        while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        line.erase(0, lead);
        if (line.empty() || line[0] == '#') { if (end == text.size()) break; continue; }
        std::string f[4];
        int nf = 0;
        size_t p = 0;
        while (p < line.size() && nf < 4)
        {
            while (p < line.size() && line[p] == ' ') ++p;
            if (p >= line.size()) break;
            size_t q = p;
            while (q < line.size() && line[q] != ' ') ++q;
            f[nf++] = line.substr(p, q - p);
            p = q;
        }
        unsigned char s[kSaltBytes], k[kKeyBytes];
        if (found || nf != 3 || f[0] != "1" || !Unhex(f[1], s, kSaltBytes) || !Unhex(f[2], k, kKeyBytes)) ++bad;
        else { std::memcpy(salt, s, kSaltBytes); std::memcpy(key, k, kKeyBytes); found = 1; }
        if (end == text.size()) break;
    }
    if (badLines != 0) *badLines = bad;
    if (bad > 0) return kAuthBad;
    return found ? kAuthKey : kAuthNone;
}

/* THE DOOR'S VERDICT on a connection's proof (pure; store_main.cpp OnAuthProof). The owner password first: a right owner proof is the
   owner's own game - in, whatever the world password, access.txt or bans.txt say or whether they can be read - unless the login
   road's wait for this connection's address is running (ownerLocked: the login proof is then not looked at). Anyone else is judged on the
   world password (worldState: kAuthNone / kAuthKey / kAuthBad = not known, rules.txt unread): none = in (access.txt and bans.txt
   still apply, DoorListsApply); this connection's wait running = locked; not known = refused; no world proof = it needs one; else
   right or wrong. */
enum DoorVerdict { kDoorOwner = 0, kDoorIn = 1, kDoorLocked = 2, kDoorBadFile = 3, kDoorNeeded = 4, kDoorWrong = 5 };
inline int DoorJudge(bool ownerAsked, bool ownerGiven, bool ownerLocked, bool ownerMatch, int worldState, bool worldGiven, bool worldLocked, bool worldMatch)
{
    if (ownerAsked && ownerGiven && !ownerLocked && ownerMatch) return kDoorOwner;
    if (worldState == kAuthNone) return kDoorIn;
    if (worldLocked) return kDoorLocked;
    if (worldState != kAuthKey) return kDoorBadFile;
    if (!worldGiven) return kDoorNeeded;
    return worldMatch ? kDoorIn : kDoorWrong;
}
/* a login proof that is neither the owner's nor an admin's counts toward the login road's wait (the one /login keeps for the address) */
inline bool LoginTryWrong(bool loginAsked, bool loginGiven, bool loginLocked, bool anyMatch) { return loginAsked && loginGiven && !loginLocked && !anyMatch; }
/* access.txt and bans.txt apply to every connection but one that proved the owner password at its door: a ban on the owner's player
   id keeps out anyone else carrying it, never the owner's own proven game */
inline bool DoorListsApply(bool ownerProven) { return !ownerProven; }
/* what the challenge asks for: the world password when it is set or not known yet, a login when the owner or the admin password is
   set; 0 = no challenge (every line empty: nobody is the owner or an admin and nobody needs a password) */
inline unsigned DoorAsks(int worldState, bool loginSet) { return (worldState != kAuthNone ? (unsigned)kAskWorld : 0u) | (loginSet ? (unsigned)kAskLogin : 0u); }

/* ---- THE LOGIN PASSWORDS A GAME REMEMBERS (login-passwords.txt beside the plugin; store.cpp StoreLoginRemembered) ----
   One line per place and world: "<place>\t<world id>\t<password>". The place is where the game dialled (LoginPlace): the address and
   port it was given for an address join, "steam:<the host's Steam id>" and port for a Steam join (Valve's relay connects to that
   account alone) - never anything a world server says. A remembered password is shown only on a link to the same place whose
   challenge names the same world id, so a server elsewhere that names this world's id (every challenge shows it) gets no proof to
   pass on and none to guess the password from. A line of any other shape is never shown and is dropped at the next write. */
inline void WipeText(std::string* s)
{
    volatile char* p = s->empty() ? 0 : &(*s)[0];
    for (size_t i = 0; i < s->size(); ++i) p[i] = 0;
    s->clear();
}
inline std::string LoginPlace(const std::string& address, unsigned port)
{
    if (address.empty() || port == 0 || address.find_first_of("\t\r\n") != std::string::npos) return std::string();
    char d[12]; size_t n = 0;
    for (unsigned v = port; v != 0; v /= 10) d[n++] = (char)('0' + v % 10);
    std::string out = address + ":";
    while (n > 0) out += d[--n];
    return out;
}
/* the place a remembered login is looked up under (T-1158, T1870): "" with no connection; before this connection's WELCOME - the
   world server's door, which asks before it - the place this game dialled (`dialAddr`, `dialPort`, as SetStoreServer was given them);
   after the WELCOME the link's place (`linkPlace`, "" while the link is not a link: another world, another protocol) */
inline std::string LoginLookupPlace(bool connected, bool welcomed, const std::string& dialAddr, unsigned dialPort, const std::string& linkPlace)
{
    if (!connected) return std::string();
    return welcomed ? linkPlace : LoginPlace(dialAddr, dialPort);
}
/* one remembered line: false = not three tab-separated fields with the place and the world given */
inline bool LoginLineSplit(const std::string& line, std::string* place, std::string* world, std::string* password)
{
    const size_t a = line.find('\t');
    if (a == std::string::npos || a == 0) return false;
    const size_t b = line.find('\t', a + 1);
    if (b == std::string::npos || b == a + 1) return false;
    *place = line.substr(0, a); *world = line.substr(a + 1, b - a - 1); *password = line.substr(b + 1);
    return true;
}
/* the next line of `text` from *at, its \r dropped; false = none left */
inline bool LoginNextLine(const std::string& text, size_t* at, std::string* line)
{
    if (*at >= text.size()) return false;
    size_t nl = text.find('\n', *at);
    if (nl == std::string::npos) nl = text.size();
    *line = text.substr(*at, nl - *at); *at = nl + 1;
    if (!line->empty() && (*line)[line->size() - 1] == '\r') line->erase(line->size() - 1);
    return true;
}
/* the password remembered for `place` and `world` in the file's text; "" = none (either is empty, or no line names both) */
inline std::string LoginFind(const std::string& text, const std::string& place, const std::string& world)
{
    std::string found, line, p, w, pw;
    if (place.empty() || world.empty()) return found;
    size_t at = 0;
    while (LoginNextLine(text, &at, &line))
    {
        if (LoginLineSplit(line, &p, &w, &pw) && p == place && w == world && !pw.empty()) { WipeText(&found); found = pw; }
        WipeText(&line); WipeText(&pw);
    }
    return found;
}
/* the file's text with the line for `place` and `world` replaced by one with `password` (an empty `password` forgets it); every
   other well-formed line kept as it was. An empty place or world changes nothing. */
inline std::string LoginPut(const std::string& text, const std::string& place, const std::string& world, const std::string& password)
{
    if (place.empty() || world.empty()) return text;
    std::string out, line, p, w, pw;
    size_t at = 0;
    while (LoginNextLine(text, &at, &line))
    {
        if (LoginLineSplit(line, &p, &w, &pw) && !(p == place && w == world)) { out += line; out += "\r\n"; }
        WipeText(&line); WipeText(&pw);
    }
    if (!password.empty()) { out += place; out += '\t'; out += world; out += '\t'; out += password; out += "\r\n"; }
    return out;
}

}   /* namespace authproof */

#endif
