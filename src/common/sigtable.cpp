/* sigtable.cpp - see sigtable.h. Pure: no globals, no OS calls. C++03 (VS2010 v100). */
#include "sigtable.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace coopsigtab {

SigRow::SigRow() : kind(-1), anchor(0), a(0), b(0), c(0), d(0), rank(-1), viaCall(0), fromOff(0), refDisp(0), refEnd(0) {}

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
            const bool noPat = ok && t[3] == "-";   /* T-1453: a from-call row has no pattern of its own */
            if (ok) { r.name = t[0]; r.kind = KindOf(t[1]); ok = r.kind >= 0; }
            ok = ok && ParseLL(t[2], &anc) && (noPat || ParseMasked(t[3], &r.pat, &r.fix))
                 && ParseLL(t[4], &r.a) && ParseLL(t[5], &r.b) && ParseLL(t[6], &r.c) && ParseLL(t[7], &r.d);
            if (ok && t[8] != "-") ok = ParseMasked(t[8], &r.chk, &r.chkFix) && r.chk.size() <= (std::size_t)coopaddr::kAddrBytesMax;
            if (ok && noPat) ok = anc == 0;
            else if (ok)
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
                    else if (x.size() == 3 && x[0] == "from-call" && r.fromRow.empty())   /* T-1453 */
                    {
                        r.fromRow = x[1];
                        ok = !r.fromRow.empty() && r.fromRow != r.name && ParseLL(x[2], &r.fromOff) && r.fromOff >= 0
                             && r.fromOff < kSigPatMax;
                    }
                    else if (x.size() == 1 && x[0] == "via-call" && r.viaCall == 0) r.viaCall = 1;   /* T-1453 */
                    else if (x.size() == 4 && x[0] == "ref" && r.refRow.empty())   /* T-1453 step 3 */
                    {
                        r.refRow = x[1];
                        ok = !r.refRow.empty() && r.refRow != r.name && ParseLL(x[2], &r.refDisp) && ParseLL(x[3], &r.refEnd)
                             && r.refDisp >= 0 && r.refDisp + 4 <= r.refEnd && r.refEnd <= (long long)r.pat.size();
                    }
                    else if (x.size() == 2 && x[0] == "optional" && r.optional.empty())            /* T-1453 / T-294 */
                    {
                        ok = coopaddr::AddrFeatureOk(x[1].c_str(), x[1].size()) != 0;
                        if (ok) r.optional = x[1];
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
            /* T-1453: exactly the from-call rows have no pattern; from-call / via-call are code rows only, never both, and a
               via-call's E8 lies inside its own pattern */
            if (ok) ok = noPat == !r.fromRow.empty();
            if (ok) ok = calls.empty() || !noPat;   /* T-1453 review: a call check needs the row's own pattern */
            if (ok && !r.refRow.empty()) ok = r.kind == coopaddr::kAddrCode && !noPat && !r.viaCall;   /* T-1453 step 3 */
            if (ok && (r.viaCall || !r.fromRow.empty())) ok = r.kind == coopaddr::kAddrCode && !(r.viaCall && !r.fromRow.empty());
            if (ok && r.viaCall)
                ok = r.a >= 0 && r.a + 5 <= (long long)r.pat.size() && r.fix[(std::size_t)r.a] && r.pat[(std::size_t)r.a] == 0xE8;
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
        if (ri.pat.empty()) continue;   /* T-1453: a from-call row is not searched (count 0, never in a hash chain) */
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
    std::strncpy(e->optional, r.optional.c_str(), coopaddr::kAddrFeatureMax - 1);
    e->optional[coopaddr::kAddrFeatureMax - 1] = 0;
    const PeSection& text = pe.secs[pe.text];
    const unsigned long long textEnd = (unsigned long long)text.va + (text.vsize > text.rawSize ? text.vsize : text.rawSize);

    if (r.kind == coopaddr::kAddrCode || r.kind == coopaddr::kAddrRet)
    {
        long long rva = (long long)patRva + r.a;
        if (r.viaCall)   /* T-1453: the pattern is a call site; the row is where the call goes */
        {
            unsigned long long tg = 0;
            if (!SigCallTarget(pe, (unsigned long long)rva, &tg, why)) return 0;
            rva = (long long)tg;
        }
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
        /* T-1453 review: a code row whose pattern does not reach it (a function's END, e.g. ProcessKeysEnd) carries the bytes at
           the row itself as its check, so another build's pattern match cannot mark the wrong place */
        if (r.kind == coopaddr::kAddrCode && !r.chk.empty()
            && (r.chk.size() != (std::size_t)n || !FitsShape(q, r.chk, r.chkFix))) { *why = "the bytes at the row are not the expected code"; return 0; }
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

int SigCallTarget(const PeView& pe, unsigned long long at, unsigned long long* target, std::string* why)
{
    const unsigned char* q = PeAt(pe, at, 5);
    if (q == 0 || q[0] != 0xE8) { *why = "no direct call at " + Hx(at); return 0; }
    unsigned long long d = (unsigned long long)((long long)at + 5 + (long long)(int)U32(q + 1));
    int hop;
    for (hop = 0; hop < 4; ++hop)
    {
        const unsigned char* jq = PeAt(pe, d, 5);
        if (jq == 0 || jq[0] != 0xE9) break;
        d = (unsigned long long)((long long)d + 5 + (long long)(int)U32(jq + 1));
    }
    *target = d;
    return 1;
}

int SigFromCallEntry(const PeView& pe, const SigRow& r, unsigned int srcStart, coopaddr::AddrEntry* e, std::string* why)
{
    unsigned long long tg = 0;
    if (!SigCallTarget(pe, (unsigned long long)srcStart + (unsigned long long)r.fromOff, &tg, why))
    {
        *why = "in its source row " + r.fromRow + ": " + *why;
        return 0;
    }
    if (tg > 0xFFFFFFFFull) { *why = "the call in its source row " + r.fromRow + " lands outside the image"; return 0; }
    SigRow x = r;   /* the target is the row itself: a code row at that address (its own verify bytes and checks) */
    x.a = 0;
    x.viaCall = 0;
    x.fromRow.clear();
    return SigRowEntry(pe, x, (unsigned int)tg, e, why);
}

namespace {
/* T-1453 step 3: every match of a ref row's pattern in .text whose displacement names `want`; the last one in *at */
void RefScan(const PeView& pe, const SigRow& r, unsigned long long want, unsigned int* at, int* hits)
{
    *hits = 0;
    const PeSection& t = pe.secs[pe.text];
    const std::size_t len = (std::size_t)(t.vsize < t.rawSize ? t.vsize : t.rawSize);
    const unsigned char* base = PeAt(pe, t.va, len);
    if (base == 0 || len < r.pat.size()) return;
    const std::size_t an = (std::size_t)r.anchor;
    std::size_t p;
    for (p = 0; p + r.pat.size() <= len; ++p)
    {
        if (base[p + an] != r.pat[an] || !FitsShape(base + p, r.pat, r.fix)) continue;
        const long long tg = (long long)t.va + (long long)p + r.refEnd + (long long)(int)U32(base + p + (std::size_t)r.refDisp);
        if (tg == (long long)want) { ++*hits; *at = (unsigned int)(t.va + p); }
    }
}
bool Skipped(const std::vector<unsigned char>* skip, std::size_t i) { return skip != 0 && i < skip->size() && (*skip)[i] != 0; }
void Who(std::vector<std::pair<int, int> >* who, int x, int y) { if (who != 0) who->push_back(std::make_pair(x, y)); }
}

int SigCrossCheck(const PeView& pe, const SigFile& f, const coopaddr::AddrEntry* out, const std::vector<unsigned int>& starts,
                  const std::vector<int>& counts, const std::vector<unsigned int>& firsts, SigOutcome* oc,
                  const std::vector<unsigned char>* skip, std::vector<std::pair<int, int> >* who)
{
    const std::size_t nr = f.rows.size();
    int bad = 0;
    std::size_t i, j;
    /* (a) ORDER: within each class, sorted by rank, the addresses strictly rise (a repeated rank is already an unreadable
       line - SigParseFile - and a tie is refused here too). T-1453: a skipped (missed optional) row takes no part. */
    int cl;
    for (cl = 0; cl < 3; ++cl)
    {
        std::vector<std::size_t> ix;
        for (i = 0; i < nr; ++i) if (ClassOf(f.rows[i].kind) == cl && !Skipped(skip, i)) ix.push_back(i);
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
                Who(who, (int)ix[i - 1], (int)ix[i]);
            }
        }
    }
    /* (b) DISTINCT: no two rows at one address */
    {
        std::vector<unsigned long long> k;
        for (i = 0; i < nr; ++i) if (!Skipped(skip, i)) k.push_back(((unsigned long long)out[i].rva << 20) | (unsigned long long)i);
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
                Who(who, (int)(k[i - 1] & 0xFFFFF), (int)(k[i] & 0xFFFFF));
            }
    }
    /* (c) GLOBALS TWICE: the second referencing instruction is another one, and it names the same address */
    for (j = 0; j < f.g2.size(); ++j)
    {
        const std::size_t o = (std::size_t)f.g2Of[j];
        if (Skipped(skip, o)) continue;
        const SigRow& g = f.g2[j];
        const int cnt = counts[nr + j];
        long long t2 = 0;
        const long long ins1 = (long long)starts[o] + f.rows[o].a, ins2 = (long long)firsts[nr + j] + g.a;
        std::string why;
        if (cnt != 1) why = cnt == 0 ? std::string("its second pattern occurs nowhere") : "its second pattern occurs " + Hx((unsigned long long)cnt) + " times";
        else if (!DataTarget(pe, firsts[nr + j], g.a, g.b, g.c, &t2)) why = "its second displacement is not readable";
        else if (ins2 == ins1) why = "its second reference is the first one";
        else if (t2 != (long long)out[o].rva) why = "its second reference names " + Hx((unsigned long long)t2) + ", the first " + Hx(out[o].rva);
        if (!why.empty()) { ++bad; oc->fails.push_back("GLOBAL TWICE: " + f.rows[o].name + ": " + why); Who(who, (int)o, -1); }
    }
    /* (d) CALL TARGETS: the recorded call, through up to four jmp stubs, lands on its code row. T-1453: a check whose row or
       whose target is a missed optional row is not made (the target has no address to compare). */
    for (j = 0; j < f.calls.size(); ++j)
    {
        const SigCall& c = f.calls[j];
        if (Skipped(skip, (std::size_t)c.row)) continue;
        if (f.rows[(std::size_t)c.row].pat.empty())   /* T-1453 review: a derived row has no pattern to hold the call */
        {
            ++bad;
            oc->fails.push_back("CALL TARGET: " + f.rows[(std::size_t)c.row].name + ": a call check on a row with no pattern of its own");
            Who(who, c.row, -1);
            continue;
        }
        const unsigned long long at = (unsigned long long)starts[(std::size_t)c.row] + (unsigned long long)c.off;
        const coopaddr::AddrEntry* t = coopaddr::AddrFind(out, (int)nr, c.target.c_str());
        const int ti = t != 0 ? (int)(t - out) : -1;
        if (ti >= 0 && Skipped(skip, (std::size_t)ti))   /* T-1453 review: logged and counted (SigOutcome::skipped) */
        {
            oc->skipped.push_back("CALL TARGET check of " + f.rows[(std::size_t)c.row].name + " (pattern offset " + Hx((unsigned long long)c.off)
                                  + ") not made: the row it names, " + c.target + ", is a dropped optional row");
            continue;
        }
        std::string why;
        unsigned long long d = 0;
        int blame = ti;   /* T-1453 review: who the failure names */
        if (t == 0 || t->kind != coopaddr::kAddrCode) { why = "'" + c.target + "' is not a resolved code row"; blame = -1; }
        else if (!SigCallTarget(pe, at, &d, &why))   /* the source row's own bytes: its failure alone */
        {
            why = "no call at pattern offset " + Hx((unsigned long long)c.off);
            blame = -1;
        }
        else if (d != (unsigned long long)t->rva) why = "the call lands on " + Hx(d) + ", not on " + c.target + " at " + Hx(t->rva);
        if (!why.empty()) { ++bad; oc->fails.push_back("CALL TARGET: " + f.rows[(std::size_t)c.row].name + ": " + why); Who(who, c.row, blame); }
    }
    return bad;
}

namespace {
/* T-1453: an optional row missed - its entry written with its name, kind and feature, rva 0 and missed = 1 */
void MissOptional(const SigFile& f, std::size_t i, const std::string& why, coopaddr::AddrEntry* out, SigOutcome* oc)
{
    coopaddr::AddrEntry e;
    std::strncpy(e.name, f.rows[i].name.c_str(), coopaddr::kAddrNameMax - 1);
    e.name[coopaddr::kAddrNameMax - 1] = 0;
    e.kind = f.rows[i].kind;
    std::strncpy(e.optional, f.rows[i].optional.c_str(), coopaddr::kAddrFeatureMax - 1);
    e.optional[coopaddr::kAddrFeatureMax - 1] = 0;
    coopaddr::AddrOptionalMiss(&e);
    out[i] = e;
    oc->missedRows.push_back((int)i);
    oc->missedWhy.push_back(why);
}
}

int SigResolveAll(const PeView& pe, const SigFile& f, coopaddr::AddrEntry* out, int maxEntries, SigOutcome* oc)
{
    oc->resolved = 0;
    oc->failed = 0;
    oc->fails.clear();
    oc->missedRows.clear();
    oc->missedWhy.clear();
    oc->skipped.clear();   /* T-1453 review 2 */
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
    const std::size_t nr = f.rows.size();
    std::vector<unsigned int> starts(nr, 0u);
    std::vector<unsigned char> found(nr, 0), skip(nr, 0);
    /* pass 1: every row with a pattern of its own; pass 2 (T-1453): every from-call row, from its source row's match */
    int pass;
    for (pass = 1; pass <= 2; ++pass)
        for (i = 0; i < nr; ++i)
        {
            const SigRow& r = f.rows[i];
            if ((pass == 1) != (!r.pat.empty() && r.refRow.empty())) continue;   /* T-1453 step 3: ref rows after their global */
            std::string why;
            int ok = 0;
            if (pass == 1)
            {
                if (counts[i] != 1)
                    why = counts[i] == 0 ? std::string("the pattern occurs nowhere") : "the pattern occurs " + Hx((unsigned long long)counts[i]) + " times";
                else ok = SigRowEntry(pe, r, firsts[i], &out[i], &why);
                if (ok) starts[i] = firsts[i];
            }
            else if (!r.refRow.empty())   /* T-1453 step 3: the one match whose displacement names its global */
            {
                std::size_t s;
                for (s = 0; s < nr && f.rows[s].name != r.refRow; ++s) {}
                unsigned int at = 0;
                int hits = 0;
                if (s == nr || (f.rows[s].kind != coopaddr::kAddrVar && f.rows[s].kind != coopaddr::kAddrRo) || f.rows[s].pat.empty())
                    why = "its global '" + r.refRow + "' is not a var / ro row with a pattern";
                else if (!found[s]) why = "its global '" + r.refRow + "' was not found";
                else
                {
                    RefScan(pe, r, (unsigned long long)out[s].rva, &at, &hits);
                    if (hits != 1) why = hits == 0 ? "no match of its pattern names its global " + r.refRow
                                                   : "its pattern names its global " + r.refRow + " " + Hx((unsigned long long)hits) + " times";
                    else ok = SigRowEntry(pe, r, at, &out[i], &why);
                    if (ok) starts[i] = at;
                }
            }
            else
            {
                std::size_t s;
                for (s = 0; s < nr && f.rows[s].name != r.fromRow; ++s) {}
                if (s == nr || f.rows[s].pat.empty()) why = "its source row '" + r.fromRow + "' is not a row with a pattern of its own";
                else if (!found[s]) why = "its source row '" + r.fromRow + "' was not found";
                else if (r.fromOff + 5 > (long long)f.rows[s].pat.size() || !f.rows[s].fix[(std::size_t)r.fromOff]
                         || f.rows[s].pat[(std::size_t)r.fromOff] != 0xE8)   /* T-1453 review */
                    why = "its source row '" + r.fromRow + "' has no fixed call (E8) at pattern offset " + Hx((unsigned long long)r.fromOff);
                else ok = SigFromCallEntry(pe, r, starts[s], &out[i], &why);
            }
            if (ok) { found[i] = 1; ++oc->resolved; continue; }
            if (!r.optional.empty()) { MissOptional(f, i, why, out, oc); skip[i] = 1; continue; }   /* T-1453: only its feature off */
            ++oc->failed;
            oc->fails.push_back(r.name + ": " + why);
        }
    if (oc->failed != 0) return 0;   /* the cross-checks run only over a full set of required rows */
    std::vector<int> srcOf(nr, -1);   /* T-1453 review: a from-call row's source row; review 2: a ref row's global */
    for (i = 0; i < nr; ++i)
    {
        const std::string& src = !f.rows[i].fromRow.empty() ? f.rows[i].fromRow : f.rows[i].refRow;
        if (src.empty()) continue;
        std::size_t s;
        for (s = 0; s < nr && f.rows[s].name != src; ++s) {}
        if (s < nr) srcOf[i] = (int)s;
    }
    /* T-1453: a cross-check failure that names an optional row misses that row, and the checks run again without it; a
       failure naming only required rows refuses. Each round misses at least one more row, so this ends. */
    for (;;)
    {
        SigOutcome tmp;
        std::vector<std::pair<int, int> > who;
        if (SigCrossCheck(pe, f, out, starts, counts, firsts, &tmp, &skip, &who) == 0) { oc->skipped = tmp.skipped; break; }
        std::size_t k;
        for (k = 0; k < tmp.fails.size(); ++k)
        {
            const int x = who[k].first, y = who[k].second;
            const bool ox = x >= 0 && !f.rows[(std::size_t)x].optional.empty();
            const bool oy = y >= 0 && !f.rows[(std::size_t)y].optional.empty();
            if (!ox && !oy) { ++oc->failed; oc->fails.push_back(tmp.fails[k]); }
        }
        if (oc->failed != 0) return 0;
        for (k = 0; k < tmp.fails.size(); ++k)
        {
            const int two[2] = { who[k].first, who[k].second };
            int m;
            for (m = 0; m < 2; ++m)
            {
                const int x = two[m];
                if (x < 0 || f.rows[(std::size_t)x].optional.empty() || skip[(std::size_t)x] != 0) continue;
                MissOptional(f, (std::size_t)x, "a cross-check named it: " + tmp.fails[k], out, oc);
                skip[(std::size_t)x] = 1;
                --oc->resolved;
            }
        }
        /* T-1453 review: a from-call row was found through its source row's match - a dropped source takes it along
           (optional: dropped too; required: refused). Repeated until nothing more goes (a chain of sources). */
        bool more = true;
        while (more)
        {
            more = false;
            for (i = 0; i < nr; ++i)
            {
                if (srcOf[i] < 0 || skip[i] != 0 || skip[(std::size_t)srcOf[i]] == 0) continue;
                const std::string why = !f.rows[i].fromRow.empty() ? "its source row '" + f.rows[i].fromRow + "' was dropped"
                                                                   : "its global '" + f.rows[i].refRow + "' was dropped";
                if (f.rows[i].optional.empty()) { ++oc->failed; oc->fails.push_back(f.rows[i].name + ": " + why); continue; }
                MissOptional(f, i, why, out, oc);
                skip[i] = 1;
                --oc->resolved;
                more = true;
            }
        }
        if (oc->failed != 0) return 0;
    }
    return (int)nr;
}

int SigSubset(const SigFile& f, const char* const* names, const char* const* feature, int n, SigFile* out,
              std::vector<std::string>* absent)
{
    *out = SigFile();
    out->sigversion = f.sigversion;
    out->badLines = f.badLines;
    out->firstBadLine = f.firstBadLine;
    out->firstBad = f.firstBad;
    int missing = 0, k;
    std::vector<int> pick(f.rows.size(), -1);   /* f's row index -> its index in out, or -1 */
    for (k = 0; k < n; ++k)
    {
        const std::string want = names[k] != 0 ? names[k] : "";
        std::size_t i;
        for (i = 0; i < f.nosig.size(); ++i)
            if (f.nosig[i] == want) out->nosig.push_back(want);
        for (i = 0; i < f.rows.size() && f.rows[i].name != want; ++i) {}
        if (i == f.rows.size())
        {
            bool nosig = false;
            std::size_t s;
            for (s = 0; s < f.nosig.size(); ++s) if (f.nosig[s] == want) nosig = true;
            if (!nosig) { ++missing; if (absent != 0) absent->push_back(want); }
            continue;
        }
        if (pick[i] >= 0) continue;   /* named twice: one row */
        pick[i] = 0;
        if (feature != 0 && feature[k] != 0) pick[i] = 1;   /* marked: its feature set below, in f's order */
    }
    std::size_t i;
    std::vector<int> at(f.rows.size(), -1);
    for (i = 0; i < f.rows.size(); ++i)
    {
        if (pick[i] < 0) continue;
        SigRow r = f.rows[i];
        if (pick[i] == 1)
            for (k = 0; k < n; ++k)
                if (names[k] != 0 && r.name == names[k] && feature[k] != 0) { r.optional = feature[k]; break; }
        at[i] = (int)out->rows.size();
        out->rows.push_back(r);
    }
    for (i = 0; i < f.g2.size(); ++i)
    {
        const int o = f.g2Of[i];
        if (o < 0 || (std::size_t)o >= at.size() || at[(std::size_t)o] < 0) continue;
        out->g2.push_back(f.g2[i]);
        out->g2Of.push_back(at[(std::size_t)o]);
    }
    for (i = 0; i < f.calls.size(); ++i)
    {
        const SigCall& c = f.calls[i];
        if (c.row < 0 || (std::size_t)c.row >= at.size() || at[(std::size_t)c.row] < 0) continue;
        std::size_t t;
        for (t = 0; t < f.rows.size() && f.rows[t].name != c.target; ++t) {}
        if (t == f.rows.size() || at[t] < 0) continue;   /* its target is not among the rows: no check to make */
        SigCall m = c;
        m.row = at[(std::size_t)c.row];
        out->calls.push_back(m);
    }
    out->declaredCount = (int)out->rows.size();
    out->declaredNosig = (int)out->nosig.size();
    return missing;
}

}   /* namespace coopsigtab */
