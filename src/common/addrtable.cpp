/* src/common/addrtable.cpp - see addrtable.h. Pure: no globals, no OS, no engine headers.
 *
 * C++03 (VS2010 v100).
 */
#include "addrtable.h"

#include <cstdio>
#include <cstring>

namespace coopaddr {

namespace {

int IsSpace(char c) { return (c == ' ' || c == '\t' || c == '\r') ? 1 : 0; }

int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A whitespace-separated field out of [p, end). Returns its length and moves p past it. */
std::size_t Field(const char* p, const char* end, const char** startOut)
{
    while (p < end && IsSpace(*p)) ++p;
    *startOut = p;
    const char* q = p;
    while (q < end && !IsSpace(*q)) ++q;
    return (std::size_t)(q - p);
}

int ParseHex32(const char* p, std::size_t n, unsigned int* out)
{
    unsigned int v = 0;
    std::size_t i;
    if (n == 0 || n > 8) return 0;
    for (i = 0; i < n; ++i)
    {
        const int d = HexVal(p[i]);
        if (d < 0) return 0;
        v = (v << 4) | (unsigned int)d;
    }
    *out = v;
    return 1;
}

int ParseDec(const char* p, std::size_t n, int* out)
{
    int v = 0;
    std::size_t i;
    if (n == 0 || n > 9) return 0;
    for (i = 0; i < n; ++i)
    {
        if (p[i] < '0' || p[i] > '9') return 0;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return 1;
}

int KindOf(const char* p, std::size_t n)
{
    if (n == 4 && std::memcmp(p, "code", 4) == 0) return kAddrCode;
    if (n == 3 && std::memcmp(p, "ret", 3) == 0)  return kAddrRet;
    if (n == 2 && std::memcmp(p, "ro", 2) == 0)   return kAddrRo;
    if (n == 3 && std::memcmp(p, "ptr", 3) == 0)  return kAddrPtr;
    if (n == 3 && std::memcmp(p, "var", 3) == 0)  return kAddrVar;
    return -1;
}

/* A name is [A-Za-z0-9_] and short. Anything else is a refusal, not a truncation: a silently shortened name
   would answer a lookup for a DIFFERENT address, which is the one failure this whole file exists to stop. */
int NameOk(const char* p, std::size_t n)
{
    std::size_t i;
    if (n == 0 || n >= (std::size_t)kAddrNameMax) return 0;
    for (i = 0; i < n; ++i)
    {
        const char c = p[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

std::string Snippet(const char* p, std::size_t n)
{
    if (n > 120) n = 120;
    return std::string(p, p + n);
}

}   /* anonymous namespace */

AddrEntry::AddrEntry()
    : kind(kAddrVar), rva(0), verifyAt(0), ptrRva(0), nbytes(0)
{
    name[0] = 0;
    std::memset(bytes, 0, sizeof(bytes));
}

AddrParseResult::AddrParseResult()
    : lines(0), entries(0), badLines(0), overflow(0), declaredCount(-1), firstBadLine(0)
{
}

std::string AddrFingerprint(unsigned int timeDateStamp, unsigned int sizeOfImage,
                            unsigned int crc32First64k, unsigned int fileSizeLow32)
{
    char b[64];
    /* std::sprintf, not a stream: RE_Kenshi imbues a process-wide locale that inserts digit-group
       separators into stream output (F030), and this string becomes a FILE NAME. */
    std::sprintf(b, "%08X-%08X-%08X-%08X", timeDateStamp, sizeOfImage, crc32First64k, fileSizeLow32);
    return std::string(b);
}

int AddrReadPeHeader(const unsigned char* head, std::size_t n,
                     unsigned int* timeDateStamp, unsigned int* sizeOfImage)
{
    unsigned int e_lfanew = 0, tds = 0, soi = 0;
    unsigned short magic = 0;
    if (head == 0 || n < 0x40) return 0;
    if (head[0] != 'M' || head[1] != 'Z') return 0;
    std::memcpy(&e_lfanew, head + 0x3C, 4);
    if ((std::size_t)e_lfanew + 24 + 60 > n) return 0;
    if (std::memcmp(head + e_lfanew, "PE\0\0", 4) != 0) return 0;
    std::memcpy(&magic, head + e_lfanew + 24, 2);
    if (magic != 0x020B) return 0;                       /* PE32+ only; this game is x64 */
    std::memcpy(&tds, head + e_lfanew + 8, 4);
    std::memcpy(&soi, head + e_lfanew + 24 + 56, 4);
    if (timeDateStamp) *timeDateStamp = tds;
    if (sizeOfImage)   *sizeOfImage = soi;
    return 1;
}

int AddrParseTable(const char* text, std::size_t n,
                   AddrEntry* out, int maxEntries, AddrParseResult* result)
{
    AddrParseResult r;
    std::size_t pos = 0;
    int count = 0;
    if (text == 0 || out == 0 || maxEntries <= 0)
    {
        if (result) *result = r;
        return 0;
    }
    while (pos < n)
    {
        const char* line = text + pos;
        std::size_t len = 0;
        while (pos + len < n && text[pos + len] != '\n') ++len;
        pos += len + 1;
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) --len;
        ++r.lines;

        {
            std::size_t lead = 0;
            while (lead < len && IsSpace(line[lead])) ++lead;
            line += lead; len -= lead;
        }
        if (len == 0 || line[0] == '#') continue;

        if (line[0] == '!')
        {
            const char* k; const char* v;
            const std::size_t kn = Field(line, line + len, &k);
            const std::size_t vn = Field(k + kn, line + len, &v);
            if (kn == 12 && std::memcmp(k, "!fingerprint", 12) == 0) r.fingerprint = Snippet(v, vn);
            else if (kn == 8 && std::memcmp(k, "!version", 8) == 0)  r.version = Snippet(v, vn);
            else if (kn == 6 && std::memcmp(k, "!count", 6) == 0)
            {
                int c = 0;
                if (ParseDec(v, vn, &c)) r.declaredCount = c;
            }
            continue;
        }

        {
            const char* end = line + len;
            const char *fn, *fk, *fr, *fv, *fb;
            const std::size_t nn = Field(line, end, &fn);
            const std::size_t nk = Field(fn + nn, end, &fk);
            const std::size_t nr = Field(fk + nk, end, &fr);
            const std::size_t nv = Field(fr + nr, end, &fv);
            const std::size_t nb = Field(fv + nv, end, &fb);
            AddrEntry e;
            unsigned int rva = 0, vat = 0;
            int kind = KindOf(fk, nk);
            int ok = 1;

            if (!NameOk(fn, nn)) ok = 0;
            if (ok && kind < 0) ok = 0;
            if (ok && !ParseHex32(fr, nr, &rva)) ok = 0;
            if (ok && !ParseHex32(fv, nv, &vat)) ok = 0;
            if (ok && nb == 0) ok = 0;

            if (ok)
            {
                if (kind == kAddrVar)
                {
                    if (!(nb == 1 && fb[0] == '-')) ok = 0;
                }
                else if (kind == kAddrPtr)
                {
                    if (!ParseHex32(fb, nb, &e.ptrRva)) ok = 0;
                }
                else
                {
                    std::size_t i;
                    if (nb < 2 || (nb & 1) != 0 || nb > (std::size_t)(kAddrBytesMax * 2)) ok = 0;
                    for (i = 0; ok && i < nb; i += 2)
                    {
                        const int hi = HexVal(fb[i]), lo = HexVal(fb[i + 1]);
                        if (hi < 0 || lo < 0) { ok = 0; break; }
                        e.bytes[i / 2] = (unsigned char)((hi << 4) | lo);
                    }
                    if (ok) e.nbytes = (int)(nb / 2);
                }
            }
            if (!ok)
            {
                ++r.badLines;
                if (r.firstBad.empty()) { r.firstBad = Snippet(line, len); r.firstBadLine = r.lines; }
                continue;
            }
            if (count >= maxEntries) { ++r.overflow; continue; }
            std::memcpy(e.name, fn, nn);
            e.name[nn] = 0;
            e.kind = kind;
            e.rva = rva;
            e.verifyAt = vat;
            out[count++] = e;
        }
    }
    r.entries = count;
    if (result) *result = r;
    return count;
}

int AddrBytesMatch(const AddrEntry& e, const unsigned char* got)
{
    if (got == 0) return 0;
    if (e.nbytes <= 0 || e.nbytes > kAddrBytesMax) return 0;
    return std::memcmp(got, e.bytes, (std::size_t)e.nbytes) == 0 ? 1 : 0;
}

const AddrEntry* AddrFind(const AddrEntry* entries, int count, const char* name)
{
    int i;
    if (entries == 0 || name == 0) return 0;
    for (i = 0; i < count; ++i)
        if (std::strcmp(entries[i].name, name) == 0) return entries + i;
    return 0;
}

const char* AddrKindName(int kind)
{
    switch (kind)
    {
        case kAddrCode: return "code";
        case kAddrRet:  return "ret";
        case kAddrRo:   return "ro";
        case kAddrPtr:  return "ptr";
        case kAddrVar:  return "var";
        default:        return "??";
    }
}

void AddrClearSlots(const AddrSlot* slots, int n)
{
    int i;
    for (i = 0; i < n; ++i) *(slots[i].slot) = 0;
}

int AddrSettleSlots(const AddrEntry* entries, int count, const AddrSlot* slots, int n, int mismatched, int byPattern,
                    int* missing, int* firstMissing)
{
    int i;
    *missing = 0;
    *firstMissing = -1;
    for (i = 0; i < n; ++i)
    {
        const AddrEntry* e = AddrFind(entries, count, slots[i].name);
        if (e == 0)
        {
            if (*firstMissing < 0) *firstMissing = i;
            ++*missing;
            continue;
        }
        *(slots[i].slot) = (unsigned long long)e->rva;
    }
    if (mismatched == 0 && *missing == 0) return 1;
    if (byPattern != 0) AddrClearSlots(slots, n);
    return 0;
}

}   /* namespace coopaddr */
