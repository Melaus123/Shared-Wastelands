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
   THIS HOSTING'S SECRET (the token): 32 random bytes the host's game makes each time it starts its world server and passes on that
   server's command line; neither side ever logs it. The host's own game shows it in HOST_TOKEN ahead of its HELLO (that connection
   is the world's owner), and the host's Steam bridge in each STEAM_ID, which tells the world server which Steam-checked account a
   bridged connection is. Where a connection comes from proves nothing (a port forwarder on the host's PC makes any outside player
   look local); the token does (accessrule.h decides what is believed). */
#ifndef SW_AUTHPROOF_H
#define SW_AUTHPROOF_H

#include <string>
#include <vector>
#include <cstring>
#include <cstddef>

namespace authproof {

/* ---- the four messages (world-server protocol 92; store_main.cpp and store.cpp check these numbers at compile time) ---- */
const unsigned int kMsgAuthChallenge = 65;   /* server -> game: nonce (16) | salt (16) - 32 bytes */
const unsigned int kMsgAuthProof     = 66;   /* game -> server: proof (32) */
const unsigned int kMsgSteamId       = 67;   /* host's bridge -> server: token (32) | u64 Steam id, little-endian (8) - 40 bytes; id 0 = a bridged link whose id Steam did not check */
const unsigned int kMsgHostToken     = 68;   /* host's own game -> server, ahead of its HELLO: token (32) */
const unsigned int kProtocol         = 121;   /* the world-server protocol that carries the four */




const size_t kSaltBytes = 16, kNonceBytes = 16, kKeyBytes = 32, kProofBytes = 32;
const size_t kTokenBytes = 32;              /* this hosting's secret */
const size_t kChallengeBytes = kNonceBytes + kSaltBytes, kSteamIdBytes = kTokenBytes + 8, kHostTokenBytes = kTokenBytes;
const unsigned int kIterations = 10000;
const unsigned int kAuthFormat = 1;          /* auth.txt's format number */
static const char kProofLabel[] = "SW-AUTH-1";
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

/* ---- the AUTH_CHALLENGE payload ---- */
inline void ChallengeEncode(const unsigned char nonce[kNonceBytes], const unsigned char salt[kSaltBytes], std::vector<char>* out)
{
    out->assign((const char*)nonce, (const char*)nonce + kNonceBytes);
    out->insert(out->end(), (const char*)salt, (const char*)salt + kSaltBytes);
}

inline bool ChallengeDecode(const std::vector<char>& p, unsigned char nonce[kNonceBytes], unsigned char salt[kSaltBytes])
{
    if (p.size() != kChallengeBytes) return false;
    std::memcpy(nonce, &p[0], kNonceBytes);
    std::memcpy(salt, &p[kNonceBytes], kSaltBytes);
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

/* ---- the HOST_TOKEN payload: token (32) ---- */
inline void HostTokenEncode(const unsigned char token[kTokenBytes], std::vector<char>* out)
{
    out->assign((const char*)token, (const char*)token + kTokenBytes);
}

inline bool HostTokenDecode(const std::vector<char>& p, unsigned char token[kTokenBytes])
{
    if (p.size() != kHostTokenBytes) return false;
    std::memcpy(token, &p[0], kTokenBytes);
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

}   /* namespace authproof */

#endif
