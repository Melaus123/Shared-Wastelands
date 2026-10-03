/* sigtable.cpp - see sigtable.h. Pure: no globals, no OS calls. C++03 (VS2010 v100). */
#include "sigtable.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace coopsigtab {

SigRow::SigRow() : kind(-1), anchor(0), a(0), b(0), c(0), d(0), rank(-1) {}

SigFile::SigFile() : sigversion(0), declaredCount(-1), declaredNosig(-1), badLines(0), firstBadLine(0) {}

PeView::PeView() : buf(0), len(0), imageBase(0), text(-1) {}

SigOutcome::SigOutcome() : resolved(0), failed(0) {}

namespace {

int HexNib(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

/* "48??8B" -> bytes + fixed flags. 0 on any malformed character or an odd length. */
int ParseMasked(const std::string& s, std::vector<unsigned char>* pat, std::vector<unsigned char>* fix)
{
    pat->clear();
    fix->clear();
    if (s.size() % 2 != 0 || s.empty() || s.size() / 2 > (std::size_t)kSigPatMax) return 0;
    std::size_t i;
    for (i = 0; i < s.size(); i += 2)
    {
        if (s[i] == '?' && s[i + 1] == '?') { pat->push_back(0); fix->push_back(0); continue; }
        const int hi = HexNib(s[i]), lo = HexNib(s[i + 1]);
        if (hi < 0 || lo < 0) return 0;
        pat->push_back((unsigned char)(hi * 16 + lo));
        fix->push_back(1);
    }
    return 1;
}

int ParseLL(const std::string& s, long long* v)
{
    if (s.empty() || s.size() > 18) return 0;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return 0;
    long long x = 0;
    for (; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9') return 0;
        x = x * 10 + (s[i] - '0');
    }
    *v = (s[0] == '-') ? -x : x;
    return 1;
}

int KindOf(const std::string& k)
{
    if (k == "code") return coopaddr::kAddrCode;
    if (k == "ret")  return coopaddr::kAddrRet;
    if (k == "ro")   return coopaddr::kAddrRo;
    if (k == "ptr")  return coopaddr::kAddrPtr;
    if (k == "var")  return coopaddr::kAddrVar;
    return -1;
}

unsigned int U32(const unsigned char* p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

unsigned int Hash16(unsigned int v) { return (v * 2654435761u) >> 16; }

int FitsShape(const unsigned char* got, const std::vector<unsigned char>& chk, const std::vector<unsigned char>& fix)
{
    std::size_t i;
    for (i = 0; i < chk.size(); ++i)
        if (fix[i] && got[i] != chk[i]) return 0;
    return 1;
}

std::string Hx(unsigned long long v)
{
    std::ostringstream o;
    o << "0x" << std::hex << std::uppercase << v;
    return o.str();
}

const unsigned int kScnCode = 0x00000020u, kScnExec = 0x20000000u, kScnWrite = 0x80000000u;

std::vector<std::string> Split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    for (;;)
    {
        const std::size_t j = s.find(sep, i);
        out.push_back(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) return out;
        i = j + 1;
    }
}

int AnchorOk(const SigRow& r)
{
    return r.anchor >= 0 && (std::size_t)r.anchor + 4 <= r.pat.size()
        && r.fix[r.anchor] && r.fix[r.anchor + 1] && r.fix[r.anchor + 2] && r.fix[r.anchor + 3];
}

int ClassOf(int kind)
{
    if (kind == coopaddr::kAddrCode || kind == coopaddr::kAddrRet) return 0;
    if (kind == coopaddr::kAddrVar) return 1;
    return 2;
}

const char* kClassName[3] = { "code+ret", "globals", "ro+ptr" };

/* The target of the RIP-relative operand whose disp32 sits at patRva + a (b = disp-to-instruction-end, c = adjust). */
int DataTarget(const PeView& pe, unsigned int patRva, long long a, long long b, long long c, long long* rva)
{
    const unsigned char* dq = a >= 0 ? PeAt(pe, (unsigned long long)patRva + (unsigned long long)a, 4) : 0;
    if (dq == 0 || b < 4 || b > 8) return 0;
    *rva = (long long)patRva + a + b + (long long)(int)U32(dq) + c;
    return 1;
}

std::string SecName(const PeSection& s) { return std::string(s.name); }

}   /* anonymous namespace */

int SigParseFile(const char* text, std::size_t n, SigFile* out)
{
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos < n)
    {
        std::size_t e = pos;
        while (e < n && text[e] != '\n') ++e;
        std::string line(text + pos, e - pos);
        pos = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        std::vector<std::string> t;
        {
            std::size_t i = 0;
            while (i < line.size())
            {
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                std::size_t j = i;
                while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
                if (j > i) t.push_back(line.substr(i, j - i));
                i = j;
            }
        }
        if (t.empty() || t[0][0] == '#') continue;
        bool ok = true;
        if (t[0][0] == '!')
        {
            long long v = 0;
            if (t[0] == "!sigversion" && t.size() == 2 && ParseLL(t[1], &v)) out->sigversion = (int)v;   /* only 2 resolves */
            else if (t[0] == "!count" && t.size() == 2 && ParseLL(t[1], &v)) out->declaredCount = (int)v;
            else if (t[0] == "!nosigcount" && t.size() == 2 && ParseLL(t[1], &v)) out->declaredNosig = (int)v;
            else if (t[0] == "!nosig" && t.size() == 2) out->nosig.push_back(t[1]);
            else if (t[0] == "!source") {}
            else ok = false;
        }
        else
        {
            SigRow r;
            long long anc = 0, rank = -1;
            ok = t.size() == 12 && t[0].size() < (std::size_t)coopaddr::kAddrNameMax;
            if (ok) { r.name = t[0]; r.kind = KindOf(t[1]); ok = r.kind >= 0; }
            ok = ok && ParseLL(t[2], &anc) && ParseMasked(t[3], &r.pat, &r.fix)
                 && ParseLL(t[4], &r.a) && ParseLL(t[5], &r.b) && ParseLL(t[6], &r.c) && ParseLL(t[7], &r.d);
            if (ok && t[8] != "-") ok = ParseMasked(t[8], &r.chk, &r.chkFix) && r.chk.size() <= (std::size_t)coopaddr::kAddrBytesMax;
            if (ok)
            {
                ok = anc >= 0 && anc + 4 <= (long long)r.pat.size();
                if (ok) { r.anchor = (int)anc; ok = AnchorOk(r) != 0; }
            }
            /* gog1 fold: <section> <rank> <extra> - a var row carries exactly one g: item, no other row any */
            std::vector<SigRow> g2;
            std::vector<SigCall> calls;
            if (ok)
            {
                r.sec = t[9];
                ok = !r.sec.empty() && r.sec[0] == '.' && r.sec.size() <= 8 && ParseLL(t[10], &rank) && rank >= 0 && rank < 100000;
                r.rank = (int)rank;
            }
            if (ok && t[11] != "-")
            {
                const std::vector<std::string> items = Split(t[11], ',');
                std::size_t k;
                for (k = 0; ok && k < items.size(); ++k)
                {
                    const std::vector<std::string> x = Split(items[k], ':');
                    if (x.size() == 6 && x[0] == "g")
                    {
                        SigRow g;
                        long long ga = 0;
                        g.name = r.name;
                        g.kind = coopaddr::kAddrVar;
                        ok = ParseLL(x[1], &ga) && ga >= 0 && ga < kSigPatMax && ParseMasked(x[2], &g.pat, &g.fix)
                             && ParseLL(x[3], &g.a) && ParseLL(x[4], &g.b) && ParseLL(x[5], &g.c);
                        if (ok) { g.anchor = (int)ga; ok = AnchorOk(g) != 0; }
                        if (ok) g2.push_back(g);
                    }
                    else if (x.size() == 3 && x[0] == "k")
                    {
                        SigCall cl;
                        cl.row = (int)out->rows.size();
                        cl.target = x[2];
                        ok = ParseLL(x[1], &cl.off) && cl.off >= 0 && cl.off + 5 <= (long long)r.pat.size() && !cl.target.empty();
                        if (ok) calls.push_back(cl);
                    }
                    else ok = false;
                }
            }
            if (ok) ok = g2.size() == (r.kind == coopaddr::kAddrVar ? 1u : 0u);
            if (ok)   /* gog1 fold 2: a rank repeated within its class is an unreadable line (prove_signatures.py too) */
            {
                std::size_t k;
                for (k = 0; ok && k < out->rows.size(); ++k)
                    if (out->rows[k].rank == r.rank && ClassOf(out->rows[k].kind) == ClassOf(r.kind)) ok = false;
            }
            if (ok)
            {
                std::size_t k;
                for (k = 0; k < g2.size(); ++k) { out->g2.push_back(g2[k]); out->g2Of.push_back((int)out->rows.size()); }
                for (k = 0; k < calls.size(); ++k) out->calls.push_back(calls[k]);
                out->rows.push_back(r);
            }
        }
        if (!ok)
        {
            ++out->badLines;
            if (out->firstBad.empty())
            {
                out->firstBad = line.substr(0, 120);
                out->firstBadLine = lineNo;
            }
        }
    }
    return (int)out->rows.size();
}

int SigParsePe(const unsigned char* buf, std::size_t len, PeView* out)
{
    out->buf = buf;
    out->len = len;
    out->secs.clear();
    out->text = -1;
    if (buf == 0 || len < 0x40 || buf[0] != 'M' || buf[1] != 'Z') return 0;
    const std::size_t e = U32(buf + 0x3C);
    if (e > len || len - e < 24 + 112) return 0;
    if (std::memcmp(buf + e, "PE\0\0", 4) != 0) return 0;
    const unsigned int nsec = (unsigned int)buf[e + 6] | ((unsigned int)buf[e + 7] << 8);
    const unsigned int optSize = (unsigned int)buf[e + 20] | ((unsigned int)buf[e + 21] << 8);
    const std::size_t opt = e + 24;
    if ((((unsigned int)buf[opt]) | ((unsigned int)buf[opt + 1] << 8)) != 0x20B) return 0;
    out->imageBase = (unsigned long long)U32(buf + opt + 24) | ((unsigned long long)U32(buf + opt + 28) << 32);
    const std::size_t sh = opt + optSize;
    if (sh > len || (len - sh) / 40 < nsec) return 0;
    unsigned int i;
    for (i = 0; i < nsec; ++i)
    {
        const unsigned char* s = buf + sh + 40 * i;
        PeSection x;
        std::memcpy(x.name, s, 8);
        x.name[8] = 0;
        x.vsize = U32(s + 8);
        x.va = U32(s + 12);
        x.rawSize = U32(s + 16);
        x.rawPtr = U32(s + 20);
        x.chars = U32(s + 36);
        if (std::strcmp(x.name, ".text") == 0 && out->text < 0) out->text = (int)out->secs.size();
        out->secs.push_back(x);
    }
    if (out->text < 0) return 0;
    const PeSection& t = out->secs[out->text];
    if ((unsigned long long)t.rawPtr + t.rawSize > (unsigned long long)len) return 0;
    return 1;
}

const unsigned char* PeAt(const PeView& pe, unsigned long long rva, std::size_t n)
{
    std::size_t i;
    for (i = 0; i < pe.secs.size(); ++i)
    {
        const PeSection& s = pe.secs[i];
        const unsigned long long rawLen = s.rawSize < s.vsize || s.vsize == 0 ? s.rawSize : s.vsize;
        if (rva < s.va || rva + n > (unsigned long long)s.va + rawLen) continue;
        const unsigned long long off = (unsigned long long)s.rawPtr + (rva - s.va);
        if (off + n > (unsigned long long)pe.len) return 0;
        return pe.buf + off;
    }
    return 0;
}

const PeSection* PeSectionOf(const PeView& pe, unsigned long long rva)
{
    std::size_t i;
    for (i = 0; i < pe.secs.size(); ++i)
    {
        const PeSection& s = pe.secs[i];
        const unsigned long long span = s.vsize > s.rawSize ? s.vsize : s.rawSize;
        if (rva >= s.va && rva < (unsigned long long)s.va + span) return &s;
    }
    return 0;
}

void SigScanAll(const PeView& pe, const SigFile& f, std::vector<int>* counts, std::vector<unsigned int>* firstRva)
{
    const std::size_t nr = f.rows.size() + f.g2.size();
    counts->assign(nr, 0);
    firstRva->assign(nr, 0u);
    if (pe.text < 0 || nr == 0) return;
    const PeSection& t = pe.secs[pe.text];
    const std::size_t tlen = (t.vsize != 0 && t.vsize < t.rawSize) ? t.vsize : t.rawSize;
    if ((unsigned long long)t.rawPtr + tlen > (unsigned long long)pe.len || tlen < 4) return;
    const unsigned char* p = pe.buf + t.rawPtr;

    std::vector<int> head(65536, -1), next(nr, -1);
    std::vector<unsigned int> key(nr, 0u);
    std::size_t i;
    for (i = 0; i < nr; ++i)
    {
        const SigRow& ri = i < f.rows.size() ? f.rows[i] : f.g2[i - f.rows.size()];
        key[i] = U32(&ri.pat[ri.anchor]);
        const unsigned int h = Hash16(key[i]);
        next[i] = head[h];
        head[h] = (int)i;
    }
    for (i = 0; i + 4 <= tlen; ++i)
    {
        const unsigned int v = U32(p + i);
        int j;
        for (j = head[Hash16(v)]; j >= 0; j = next[j])
        {
            if (key[j] != v) continue;
            const SigRow& r = (std::size_t)j < f.rows.size() ? f.rows[j] : f.g2[j - f.rows.size()];
            if (i < (std::size_t)r.anchor) continue;
            const std::size_t s = i - r.anchor;
            if (s + r.pat.size() > tlen) continue;
            std::size_t k;
            for (k = 0; k < r.pat.size(); ++k)
                if (r.fix[k] && p[s + k] != r.pat[k]) break;
            if (k != r.pat.size()) continue;
            if ((*counts)[j] == 0) (*firstRva)[j] = t.va + (unsigned int)s;
            ++(*counts)[j];
        }
    }
}

int SigRowEntry(const PeView& pe, const SigRow& r, unsigned int patRva, coopaddr::AddrEntry* e, std::string* why)
{
    *e = coopaddr::AddrEntry();
    std::strncpy(e->name, r.name.c_str(), coopaddr::kAddrNameMax - 1);
    e->name[coopaddr::kAddrNameMax - 1] = 0;
    e->kind = r.kind;
    const PeSection& text = pe.secs[pe.text];
    const unsigned long long textEnd = (unsigned long long)text.va + (text.vsize > text.rawSize ? text.vsize : text.rawSize);

    if (r.kind == coopaddr::kAddrCode || r.kind == coopaddr::kAddrRet)
    {
        const long long rva = (long long)patRva + r.a;
        if (rva < (long long)text.va || (unsigned long long)rva >= textEnd) { *why = "resolves outside .text (" + Hx((unsigned long long)rva) + ")"; return 0; }
        if (r.sec != ".text") { *why = "a code row whose section is not .text ('" + r.sec + "')"; return 0; }
        e->rva = (unsigned int)rva;
        long long at, n;
        if (r.kind == coopaddr::kAddrCode)
        {
            if (r.b != 0 && r.b != 16) { *why = "verify offset is neither 0 nor 16"; return 0; }
            at = rva + r.b;
            n = r.c;
        }
        else
        {
            at = rva - r.b;
            n = r.b;
        }
        if (n < 1 || n > coopaddr::kAddrBytesMax) { *why = "verify length out of range"; return 0; }
        const unsigned char* q = PeAt(pe, (unsigned long long)at, (std::size_t)n);
        if (q == 0) { *why = "verify bytes at " + Hx((unsigned long long)at) + " are not in the file"; return 0; }
        if (r.kind == coopaddr::kAddrRet
            && (r.chk.size() != (std::size_t)n || !FitsShape(q, r.chk, r.chkFix))) { *why = "the bytes before the return address are not the expected call"; return 0; }
        e->verifyAt = (unsigned int)at;
        std::memcpy(e->bytes, q, (std::size_t)n);
        e->nbytes = (int)n;
        return 1;
    }

    /* a data row: the target of the RIP-relative operand whose displacement sits at P + a */
    const unsigned char* dq = PeAt(pe, (unsigned long long)patRva + (unsigned long long)r.a, 4);
    if (dq == 0 || r.a < 0 || r.b < 4 || r.b > 8) { *why = "the displacement is not readable"; return 0; }
    const long long disp = (long long)(int)U32(dq);
    const long long rva = (long long)patRva + r.a + r.b + disp + r.c;
    const PeSection* s = rva > 0 ? PeSectionOf(pe, (unsigned long long)rva) : 0;
    if (s == 0) { *why = "resolves outside every section (" + Hx((unsigned long long)rva) + ")"; return 0; }
    if (SecName(*s) != r.sec) { *why = "resolved into section '" + SecName(*s) + "', the row belongs in '" + r.sec + "'"; return 0; }
    e->rva = (unsigned int)rva;
    e->verifyAt = (unsigned int)rva;
    if (r.kind == coopaddr::kAddrVar)
    {
        if ((s->chars & kScnWrite) == 0) { *why = "a writable global resolved into a read-only section"; return 0; }
        return 1;
    }
    if ((s->chars & (kScnWrite | kScnExec | kScnCode)) != 0) { *why = "read-only data resolved into a writable or code section"; return 0; }
    if (r.kind == coopaddr::kAddrRo)
    {
        const long long at = rva + r.d;
        const std::size_t n = r.chk.size();
        const unsigned char* q = n ? PeAt(pe, (unsigned long long)at, n) : 0;
        if (q == 0 || !FitsShape(q, r.chk, r.chkFix)) { *why = "the constant bytes at " + Hx((unsigned long long)at) + " are not the expected ones"; return 0; }
        e->verifyAt = (unsigned int)at;
        std::memcpy(e->bytes, q, n);
        e->nbytes = (int)n;
        return 1;
    }
    if (r.kind == coopaddr::kAddrPtr)
    {
        const long long slot = rva + r.d;
        const unsigned char* q = PeAt(pe, (unsigned long long)slot, 8);
        if (q == 0) { *why = "the vtable slot is not in the file"; return 0; }
        const unsigned long long v = (unsigned long long)U32(q) | ((unsigned long long)U32(q + 4) << 32);
        if (v < pe.imageBase || v - pe.imageBase >= 0xFFFFFFFFull) { *why = "the vtable slot does not point into the image"; return 0; }
        const unsigned long long p = v - pe.imageBase;
        unsigned long long f = p;
        int hop;
        for (hop = 0; hop < 4; ++hop)
        {
            const unsigned char* jq = PeAt(pe, f, 5);
            if (jq == 0 || jq[0] != 0xE9) break;
            f = (unsigned long long)((long long)f + 5 + (long long)(int)U32(jq + 1));
        }
        const unsigned char* fq = PeAt(pe, f, r.chk.size());
        if (f < text.va || f >= textEnd || r.chk.empty() || fq == 0 || !FitsShape(fq, r.chk, r.chkFix))
        {
            *why = "the vtable slot's target " + Hx(f) + " is not the expected code";
            return 0;
        }
        e->verifyAt = (unsigned int)slot;
        e->ptrRva = (unsigned int)p;
        return 1;
    }
    *why = "unknown kind";
    return 0;
}

int SigCrossCheck(const PeView& pe, const SigFile& f, const coopaddr::AddrEntry* out, const std::vector<unsigned int>& starts,
                  const std::vector<int>& counts, const std::vector<unsigned int>& firsts, SigOutcome* oc)
{
    const std::size_t nr = f.rows.size();
    int bad = 0;
    std::size_t i, j;
    /* (a) ORDER: within each class, sorted by rank, the addresses strictly rise (a repeated rank is already an unreadable
       line - SigParseFile - and a tie is refused here too) */
    int cl;
    for (cl = 0; cl < 3; ++cl)
    {
        std::vector<std::size_t> ix;
        for (i = 0; i < nr; ++i) if (ClassOf(f.rows[i].kind) == cl) ix.push_back(i);
        for (i = 1; i < ix.size(); ++i)   /* insertion sort by rank: a few hundred rows, once */
        {
            const std::size_t v = ix[i];
            for (j = i; j > 0 && f.rows[ix[j - 1]].rank > f.rows[v].rank; --j) ix[j] = ix[j - 1];
            ix[j] = v;
        }
        for (i = 1; i < ix.size(); ++i)
        {
            const SigRow& x = f.rows[ix[i - 1]];
            const SigRow& y = f.rows[ix[i]];
            if (x.rank == y.rank || out[ix[i - 1]].rva >= out[ix[i]].rva)
            {
                ++bad;
                oc->fails.push_back(std::string("ORDER (") + kClassName[cl] + "): " + x.name + " (rank " + Hx((unsigned long long)x.rank)
                                    + ") at " + Hx(out[ix[i - 1]].rva) + " is not below " + y.name + " (rank " + Hx((unsigned long long)y.rank)
                                    + ") at " + Hx(out[ix[i]].rva));
            }
        }
    }
    /* (b) DISTINCT: no two rows at one address */
    {
        std::vector<unsigned long long> k;
        for (i = 0; i < nr; ++i) k.push_back(((unsigned long long)out[i].rva << 20) | (unsigned long long)i);
        for (i = 1; i < k.size(); ++i)
        {
            const unsigned long long v = k[i];
            for (j = i; j > 0 && k[j - 1] > v; --j) k[j] = k[j - 1];
            k[j] = v;
        }
        for (i = 1; i < k.size(); ++i)
            if ((k[i - 1] >> 20) == (k[i] >> 20))
            {
                ++bad;
                oc->fails.push_back("DISTINCT: " + f.rows[(std::size_t)(k[i - 1] & 0xFFFFF)].name + " and "
                                    + f.rows[(std::size_t)(k[i] & 0xFFFFF)].name + " resolve to the same address " + Hx(k[i] >> 20));
            }
    }
    /* (c) GLOBALS TWICE: the second referencing instruction is another one, and it names the same address */
    for (j = 0; j < f.g2.size(); ++j)
    {
        const std::size_t o = (std::size_t)f.g2Of[j];
        const SigRow& g = f.g2[j];
        const int cnt = counts[nr + j];
        long long t2 = 0;
        const long long ins1 = (long long)starts[o] + f.rows[o].a, ins2 = (long long)firsts[nr + j] + g.a;
        std::string why;
        if (cnt != 1) why = cnt == 0 ? std::string("its second pattern occurs nowhere") : "its second pattern occurs " + Hx((unsigned long long)cnt) + " times";
        else if (!DataTarget(pe, firsts[nr + j], g.a, g.b, g.c, &t2)) why = "its second displacement is not readable";
        else if (ins2 == ins1) why = "its second reference is the first one";
        else if (t2 != (long long)out[o].rva) why = "its second reference names " + Hx((unsigned long long)t2) + ", the first " + Hx(out[o].rva);
        if (!why.empty()) { ++bad; oc->fails.push_back("GLOBAL TWICE: " + f.rows[o].name + ": " + why); }
    }
    /* (d) CALL TARGETS: the recorded call, through up to four jmp stubs, lands on its code row */
    for (j = 0; j < f.calls.size(); ++j)
    {
        const SigCall& c = f.calls[j];
        const unsigned long long at = (unsigned long long)starts[(std::size_t)c.row] + (unsigned long long)c.off;
        const unsigned char* q = PeAt(pe, at, 5);
        const coopaddr::AddrEntry* t = coopaddr::AddrFind(out, (int)nr, c.target.c_str());
        std::string why;
        if (q == 0 || q[0] != 0xE8) why = "no call at pattern offset " + Hx((unsigned long long)c.off);
        else if (t == 0 || t->kind != coopaddr::kAddrCode) why = "'" + c.target + "' is not a resolved code row";
        else
        {
            unsigned long long d = (unsigned long long)((long long)at + 5 + (long long)(int)U32(q + 1));
            int hop;
            for (hop = 0; hop < 4; ++hop)
            {
                const unsigned char* jq = PeAt(pe, d, 5);
                if (jq == 0 || jq[0] != 0xE9) break;
                d = (unsigned long long)((long long)d + 5 + (long long)(int)U32(jq + 1));
            }
            if (d != (unsigned long long)t->rva) why = "the call lands on " + Hx(d) + ", not on " + c.target + " at " + Hx(t->rva);
        }
        if (!why.empty()) { ++bad; oc->fails.push_back("CALL TARGET: " + f.rows[(std::size_t)c.row].name + ": " + why); }
    }
    return bad;
}

int SigResolveAll(const PeView& pe, const SigFile& f, coopaddr::AddrEntry* out, int maxEntries, SigOutcome* oc)
{
    oc->resolved = 0;
    oc->failed = 0;
    oc->fails.clear();
    std::size_t i;
    for (i = 0; i < f.nosig.size(); ++i)
    {
        ++oc->failed;
        oc->fails.push_back(f.nosig[i] + ": the generator could not make a unique pattern for it");
    }
    if (f.badLines > 0) oc->fails.push_back("the file has " + Hx((unsigned long long)f.badLines) + " unreadable line(s), the first: '" + f.firstBad + "'");
    if (f.sigversion != 2) oc->fails.push_back("the file is !sigversion " + Hx((unsigned long long)f.sigversion) + ", and only 2 carries the cross-checks");
    if (f.declaredCount != (int)f.rows.size()) oc->fails.push_back("the file's !count does not match its rows");
    if (f.declaredNosig != (int)f.nosig.size()) oc->fails.push_back("the file's !nosigcount does not match its !nosig lines");
    if ((int)f.rows.size() > maxEntries) oc->fails.push_back("more rows than the entry table holds");
    if (pe.text < 0) oc->fails.push_back("the executable has no .text section");
    if (!oc->fails.empty() && oc->failed == 0) oc->failed = 1;
    if (!oc->fails.empty()) return 0;

    std::vector<int> counts;
    std::vector<unsigned int> firsts;
    SigScanAll(pe, f, &counts, &firsts);
    std::vector<unsigned int> starts(f.rows.size(), 0u);
    for (i = 0; i < f.rows.size(); ++i)
    {
        std::string why;
        if (counts[i] != 1)
            why = counts[i] == 0 ? std::string("the pattern occurs nowhere") : "the pattern occurs " + Hx((unsigned long long)counts[i]) + " times";
        else if (SigRowEntry(pe, f.rows[i], firsts[i], &out[i], &why))
        {
            starts[i] = firsts[i];
            ++oc->resolved;
            continue;
        }
        ++oc->failed;
        oc->fails.push_back(f.rows[i].name + ": " + why);
    }
    if (oc->failed == 0) oc->failed = SigCrossCheck(pe, f, out, starts, counts, firsts, oc);   /* only over a full set */
    return oc->failed == 0 ? oc->resolved : 0;
}

}   /* namespace coopsigtab */
