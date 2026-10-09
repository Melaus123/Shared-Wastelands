/* src/common/storemeta.cpp - see storemeta.h. Pure: no globals, no OS, no engine. C++03. */
#include "storemeta.h"

#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <vector>

namespace coopstore {

namespace {

/* The table is built once, lazily. Two threads racing here write the SAME 256 values into the same array and
   then both read them, so the race is idempotent rather than a data race with an observable outcome - and the
   notebook is single-threaded in any case. Stated rather than left to be discovered. */
unsigned int g_crcTable[256];
int          g_crcTableBuilt = 0;

void BuildCrcTable()
{
    unsigned int i, c;
    int k;
    for (i = 0; i < 256; ++i)
    {
        c = i;
        for (k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcTableBuilt = 1;
}

/* A field may not carry the two characters the format uses as structure, nor a CR (which a text-mode reader
   would strip and a binary one would keep - two readings of one line is the defect this refuses). */
bool FieldIsClean(const std::string& s)
{
    std::string::size_type i;
    for (i = 0; i < s.size(); ++i)
        if (s[i] == '\t' || s[i] == '\n' || s[i] == '\r') return false;
    return true;
}

void StripCr(std::string* s)
{
    if (!s->empty() && (*s)[s->size() - 1] == '\r') s->erase(s->size() - 1);
}

/* C++03 has no portable 64-bit string-to-integer, and this file may not depend on one vendor's CRT extension
   (_atoi64) any more than it may depend on Windows. Digits only, with a sign - which is exactly what the fields
   it reads contain. */
long long ParseI64(const std::string& s)
{
    std::string::size_type i = 0;
    int neg = 0;
    long long v = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = (s[i] == '-') ? 1 : 0; ++i; }
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (long long)(s[i] - '0');
    return neg ? -v : v;
}

void SplitTabs(const std::string& line, std::vector<std::string>* out)
{
    std::string::size_type at = 0;
    while (at <= line.size())
    {
        const std::string::size_type t = line.find('\t', at);
        if (t == std::string::npos) { out->push_back(line.substr(at)); break; }
        out->push_back(line.substr(at, t - at));
        at = t + 1;
    }
}

}   /* anonymous namespace */

unsigned int Crc32(const void* data, std::size_t n)
{
    /* The CRC of no bytes is 0x00000000 (0xFFFFFFFF ^ 0xFFFFFFFF), which is what an empty payload is written
       as - so "no payload" and "a zero-length payload" agree by construction rather than by a special case. */
    if (n == 0 || data == 0) return 0u;
    if (!g_crcTableBuilt) BuildCrcTable();
    const unsigned char* p = (const unsigned char*)data;
    unsigned int crc = 0xFFFFFFFFu;
    std::size_t i;
    for (i = 0; i < n; ++i) crc = g_crcTable[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

MetaLine::MetaLine()
    : version(0), writtenAt(0), posAt(0), owner(0), x(0.0f), y(0.0f), z(0.0f), len(0), crc(0), seq(0) {}

FolderFacts::FolderFacts()
    : metaState(kFileAbsent), haveMeta(0),
      prevMetaState(kFileAbsent), havePrevMeta(0),
      payloadState(kFileAbsent), payloadLen(0), payloadCrc(0),
      prevPayloadState(kFileAbsent), prevPayloadLen(0), prevPayloadCrc(0) {}

Choice::Choice() : kind(kChooseNone), refusal(kPayloadOk), hasFile(0), site(-1) {}

UpgradeChoice::UpgradeChoice() : kind(kUpgradePositionOnly) {}

unsigned int OwnerFieldFromSlot(int slot)
{
    if (slot < 0 || slot > (int)kMaxOwnerSlot) return kOwnerUnknown;
    return (unsigned int)slot;
}

unsigned int OwnerAfterUpgrade(int lineVersion, unsigned int lineOwner)
{
    return (lineVersion >= 6) ? lineOwner : kOwnerUnknown;
}

bool MetaOwnerFieldValid(const std::string& field, unsigned int* out)
{
    /* Digits only, and no sign: the field is written by this file`s own encoder from a u32, so a `-`
       or a `+` in it is a line another program wrote and we do not understand. std::atoi would have
       read "1024x" as 1024 and "" as 0, and 0 is a REAL SLOT - which is how a refusal becomes a
       silent claim of authorship. */
    if (field.empty() || field.size() > 10) return false;
    std::string::size_type i;
    for (i = 0; i < field.size(); ++i) if (field[i] < 0x30 || field[i] > 0x39) return false;
    unsigned long long v = 0;
    for (i = 0; i < field.size(); ++i) v = v * 10 + (unsigned long long)(field[i] - 0x30);
    if (v == (unsigned long long)kOwnerUnknown) { if (out) *out = kOwnerUnknown; return true; }
    if (v > (unsigned long long)kMaxOwnerSlot) return false;
    if (out) *out = (unsigned int)v;
    return true;
}

int OwnerTranslateDecide(int haveRecord, unsigned int recordOwner, unsigned int myOwner)
{
    if (!haveRecord) return kOwnerNoRecord;
    if (recordOwner == kOwnerUnknown || myOwner == kOwnerUnknown) return kOwnerTranslateUnknown;
    if (recordOwner != myOwner) return kOwnerTranslateOther;
    return kOwnerSkipOwn;
}

int RotateMetaFailCause(int metaState, int prevMetaState)
{
    if (metaState == kFileUnreadable) return kRotCauseSourceLocked;
    if (metaState == kFileReadable && prevMetaState == kFileUnreadable) return kRotCauseDestLocked;
    return kRotCauseUnknown;
}

int RotateMetaFailAction(int cause)
{
    return (cause == kRotCauseDestLocked) ? kRotActionProceedNoRotate : kRotActionRefuse;
}

int RotateFailBooking(int metaMoved, int rollbackOk)
{
    if (!metaMoved) return kRotBookNothing;
    return rollbackOk ? kRotBookRolledBack : kRotBookRollbackFailed;
}

/* B10: ONE ENCODER, TWO TAGS. The fields, their order and their formatting are identical; only the
   meaning of `owner` moved, and a meaning does not change a byte layout.
   B12: THREE tags, and v7 appends one field. `seqField` is 1 for v7 and 0 for the older tags - it is the
   caller's own tag turned into a number here rather than a std::string comparison on every write, and it
   keeps the three encoders one function (6a lesson 11). */
static bool MetaEncodeTagged(const char* tag, int seqField, const MetaLine& m, std::string* out)
{
    if (!out) return false;
    if (m.worldId.empty()) return false;
    if (!FieldIsClean(m.squadSid) || !FieldIsClean(m.factionName) ||
        !FieldIsClean(m.worldId)  || !FieldIsClean(m.town) || (seqField != 0 && !FieldIsClean(m.home))) return false;

    /* RE_Kenshi's locale mangles numbers (F030): the stream is imbued with the classic locale so a thousands
       separator can never reach a file another program parses with atof. */
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << tag << "\t" << m.writtenAt << "\t" << m.owner << "\t" << m.x << "\t" << m.y << "\t" << m.z
      << "\t" << m.squadSid << "\t" << m.factionName << "\t" << m.worldId << "\t" << m.posAt
      << "\t" << m.town << "\t" << m.len << "\t";
    char hex[16];
    std::sprintf(hex, "%08x", (unsigned int)(m.len > 0 ? m.crc : 0u));
    o << hex;
    if (seqField != 0) o << "\t" << m.seq;   /* B12: field 14, appended - see storemeta.h */
    if (seqField != 0 && !m.home.empty()) o << "\t" << m.home;   /* field 15, only when the record has a home - see storemeta.h */
    o << "\n";
    *out = o.str();
    return true;
}
bool MetaEncodeV5(const MetaLine& m, std::string* out) { return MetaEncodeTagged("v5", 0, m, out); }
bool MetaEncodeV6(const MetaLine& m, std::string* out) { return MetaEncodeTagged("v6", 0, m, out); }
bool MetaEncodeV7(const MetaLine& m, std::string* out) { return MetaEncodeTagged("v7", 1, m, out); }
std::string MetaHomeMerge(const std::string& kept, const std::string& arriving)
{
    return (!arriving.empty() && FieldIsClean(arriving)) ? arriving : kept;
}
std::string MetaHomeClean(const std::string& home)
{
    return (home.size() <= kMetaHomeCap && FieldIsClean(home)) ? home : std::string();
}

bool MetaParse(const std::string& line, MetaLine* out)
{
    if (!out) return false;
    /* THE ENCODER ENDS THE LINE WITH '\n' AND getline HANDS IT OVER WITHOUT ONE. Both are accepted here, so
       encode -> parse is a true inverse. Until the offline suite caught it, a v4 line's LAST field (the town)
       came back with the newline still attached and re-encoding it was then refused - and the v5 round trip
       hid it, because in v5 the last field is the checksum and strtoul skips trailing whitespace. */
    std::string body = line;
    while (!body.empty() && (body[body.size() - 1] == '\n' || body[body.size() - 1] == '\r')) body.erase(body.size() - 1);
    std::vector<std::string> f;
    SplitTabs(body, &f);
    if (f.size() < 10) return false;

    int version = 0;
    if      (f[0] == "v2") version = 2;
    else if (f[0] == "v3") version = 3;
    else if (f[0] == "v4") version = 4;
    else if (f[0] == "v5") version = 5;
    else if (f[0] == "v6") version = 6;   /* B10: the same 13 fields; `owner` is a notebook slot */
    else if (f[0] == "v7") version = 7;   /* B12: 14 fields - `seq` is appended (decision 52) */
    else return false;
    if (version >= 5 && f.size() < 13) return false;
    if (version >= 7 && f.size() < 14) return false;

    StripCr(&f[f.size() - 1]);

    MetaLine m;
    m.version     = version;
    m.writtenAt   = ParseI64(f[1]);
    if (!MetaOwnerFieldValid(f[2], &m.owner)) return false;   /* B10: refused, never silently zeroed */
    m.x           = (float)std::atof(f[3].c_str());
    m.y           = (float)std::atof(f[4].c_str());
    m.z           = (float)std::atof(f[5].c_str());
    m.squadSid    = f[6];
    m.factionName = f[7];
    m.worldId     = f[8];
    m.posAt       = ParseI64(f[9]);
    m.town        = f.size() >= 11 ? f[10] : std::string();
    StripCr(&m.worldId);
    StripCr(&m.town);
    if (version >= 5)
    {
        m.len = ParseI64(f[11]);
        if (m.len < 0) m.len = 0;
        m.crc = (unsigned int)std::strtoul(f[12].c_str(), 0, 16);
    }
    if (version >= 7)
    {
        /* B12: DIGITS ONLY, AND THE WHOLE FIELD. A seq read short or read as 0 from a field that is not a
           number decides a replay conflict wrongly and silently, which is the one failure decision 52's
           sequence number exists to remove - so the LINE is refused instead. */
        if (f[13].empty()) return false;
        m.seq = 0ULL;
        for (std::string::size_type i = 0; i < f[13].size(); ++i)
        {
            const char c = f[13][i];
            if (c < '0' || c > '9') return false;
            const unsigned long long d = (unsigned long long)(c - '0');
            if (m.seq > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) return false;
            m.seq = m.seq * 10ULL + d;
        }
    }
    /* field 15 of a v7 line: the home building key, optional (storemeta.h); StripCr above has run on it when it is the last field */
    if (version >= 7 && f.size() >= 15) m.home = f[14];
    if (m.worldId.empty()) return false;
    *out = m;
    return true;
}

int PayloadCheck(long long expectLen, unsigned int expectCrc,
                 int fileState, long long fileLen, unsigned int fileCrc)
{
    /* THE ORDER IS THE RULE. "The line says there is no payload" is decided before anything is asked of
       the disk, because that line is CORRECT whatever the folder holds. Then the three-way state: only
       kFileAbsent is missing, and a file we could not read is its own answer rather than a missing one
       (review-p8c C-2 - collapsing those two destroyed a committed squad). Length before checksum, so a
       short file can never reach the CRC and collide there. */
    if (expectLen <= 0) return kPayloadNoneExpected;
    if (fileState == kFileUnreadable) return kPayloadUnreadable;
    if (fileState != kFileReadable)   return kPayloadMissing;
    if (fileLen != expectLen) return kPayloadTorn;
    if (fileCrc != expectCrc) return kPayloadCrcBad;
    return kPayloadOk;
}

Choice ChooseVersion(const FolderFacts& f)
{
    Choice c;

    /* P8d, ARM ONE (review-p8c C-2, the <id>.meta half). A CURRENT INDEX LINE WE COULD NOT READ IS NOT AN
       ABSENT ONE. Every arm below this point can end in a rename or a rewrite of <id>.meta, and doing that
       to a file that is merely locked destroys a line nobody has read. The record is still INDEXED when a
       previous line is available - its position is what this notebook has always served for a record whose
       payload it will not hand out - and NOTHING IS WRITTEN, so the next start decides again with the file
       open. `haveMeta` is 0 here by construction (an unread file has no parsed line); the state is what
       tells this case apart from a genuinely missing <id>.meta, which is crash window 2 and MUST promote. */
    if (f.metaState == kFileUnreadable)
    {
        c.refusal = kPayloadUnreadable;
        c.site = kSiteMetaUnreadableNoPrev;
        if (f.havePrevMeta && f.prevMeta.version >= 5) { c.kind = kChoosePrevRefused; c.hasFile = 0; c.site = kSiteMetaUnreadablePrevIndexed; }
        return c;
    }

    if (f.haveMeta && f.meta.version >= 5)
    {
        const int v = PayloadCheck(f.meta.len, f.meta.crc,
                                   f.payloadState, f.payloadLen, f.payloadCrc);
        if (v == kPayloadOk)           { c.kind = kChooseCurrent; c.hasFile = 1; c.site = kSiteCurrentOk; return c; }
        if (v == kPayloadNoneExpected) { c.kind = kChooseCurrent; c.hasFile = 0; c.site = kSiteCurrentNoPayloadExpected; return c; }
        c.refusal = v;   /* remembered whatever happens below - the refusal is what gets counted */
        /* P8d, ARM TWO (review-p8c C-2, as measured). The line claims a payload and that payload is ON
           DISK AND UNREADABLE. P8c read that as MISSING, promoted the .prev pair over it and renamed the
           previous payload on top of the intact one at the next start - a committed squad destroyed by a
           scanner holding a file open for one second, reported as a successful recovery both times. A
           payload we cannot read is a payload we cannot RULE OUT, so nothing may be preferred over it.
           The record keeps its position, nothing is written, and the next start looks again. */
        if (v == kPayloadUnreadable) { c.kind = kChooseCurrentRefused; c.hasFile = 0; c.site = kSiteCurrentPayloadUnreadable; return c; }
    }
    else if (!f.haveMeta && !f.havePrevMeta)
    {
        c.site = kSiteNoLineAtAll;
        return c;   /* kChooseNone */
    }

    if (f.havePrevMeta && f.prevMeta.version >= 5)
    {
        /* P8d. The same rule for the pass-2 shape, where there is no <id>.meta to have caught it above:
           an unreadable <id>.platoon may not be renamed over from .platoon.prev either. */
        if (f.payloadState == kFileUnreadable)
        {
            if (c.refusal == kPayloadOk) c.refusal = kPayloadUnreadable;
        }
        else
        {
        /* Window 2 and window 4 both land here: the previous line against whatever <id>.platoon is now. In
           window 2 that file is the OLD payload and matches; in window 4 it is the NEW one and does not. */
        const int vc = PayloadCheck(f.prevMeta.len, f.prevMeta.crc,
                                    f.payloadState, f.payloadLen, f.payloadCrc);
        if (vc == kPayloadOk)           { c.kind = kChoosePrevMetaCurrent; c.hasFile = 1; c.site = kSitePrevMetaCurrentOk; return c; }
        if (vc == kPayloadNoneExpected) { c.kind = kChoosePrevMetaCurrent; c.hasFile = 0; c.site = kSitePrevMetaCurrentNoPayload; return c; }

        /* Window 3: both current names are gone or wrong, and the pair that is whole is the .prev pair.
           P8d: and an UNREADABLE .platoon.prev is not a torn one - it cannot be validated, so it cannot
           be promoted, and saying "torn" about it would be a claim we have not earned. */
        const int vp = PayloadCheck(f.prevMeta.len, f.prevMeta.crc,
                                    f.prevPayloadState, f.prevPayloadLen, f.prevPayloadCrc);
        if (vp == kPayloadOk) { c.kind = kChoosePrevPair; c.hasFile = 1; c.site = kSitePrevPairOk; return c; }

        /* P8c (review-p8b H-1). THE REFUSAL IS RECORDED HERE TOO. It used to be set only in the arm above,
           which runs `if (f.haveMeta ...)` - so for a record with no <id>.meta at all the three refusal
           counters could not move however damaged its files were, and the drop read as "nothing happened".
           A current-pair refusal already recorded is not overwritten: that is the one this Choice is about. */
        if (c.refusal == kPayloadOk) c.refusal = vp;
        }
    }

    if (f.haveMeta) { c.kind = kChooseCurrentRefused; c.hasFile = 0; c.site = kSiteCurrentRefused; return c; }
    /* P8c (review-p8b M-1). PASS 2 IS NO LONGER STRICTER THAN PASS 1. A previous line that parses as v5
       carries a position, and a position is what this notebook has always served for a record whose payload
       it will not hand out. Dropping it instead loses the group entirely - the very outcome the previous
       version exists to prevent. */
    if (f.havePrevMeta && f.prevMeta.version >= 5) { c.kind = kChoosePrevRefused; c.hasFile = 0; c.site = kSitePrevRefused; return c; }
    c.site = kSiteNothingValidates;
    return c;   /* kChooseNone - there IS a previous line and it is below v5, so it can validate nothing */
}

UpgradeChoice UpgradeDecide(const FolderFacts& f)
{
    UpgradeChoice u;

    /* (1) THE ONE THAT COST A CRITICAL, NOW OVER ALL FOUR FILES. "I could not read it" is not "it is not
       there", and the only safe answer to "I do not know what this file is" is to write nothing and look
       again next time. This comes FIRST, ahead of any promotion: a file we cannot read is a file we cannot
       rule out, so we do not get to prefer another version over it either - and the upgrade is a WRITE,
       which is what makes a wrong answer here permanent. P8c tested only <id>.platoon; review-p8c H-1
       measured a locked <id>.meta.prev blessing a five-byte torn payload as truth. */
    if (f.metaState        == kFileUnreadable ||
        f.payloadState     == kFileUnreadable ||
        f.prevMetaState    == kFileUnreadable ||
        f.prevPayloadState == kFileUnreadable) { u.kind = kUpgradeDefer; return u; }

    /* (2)(3) The previous version is consulted exactly as it is for a v5 line. A v2-v4 line is presented to
       ChooseVersion as it always was - it cannot validate anything, and the header says so - so a .prev pair
       that DOES validate wins. kChoosePrevMetaCurrent is deliberately not a promotion: it means the file
       under the current name is byte for byte the file the previous line describes, so the v2-v4 line is
       upgraded from that file and keeps its own, newer, position. */
    u.promote = ChooseVersion(f);
    if (u.promote.kind == kChoosePrevPair) { u.kind = kUpgradePromotePrev; return u; }

    /* (4) P8d (review-p8c M-1): "there is a file" is not "there are bytes". A payload that is on disk with
       ZERO BYTES in it carries no squad, so the honest v5 line for it is the position-only one - which is
       what P8c wrote anyway, while both its header and spawn-serialize.md said it never did. It is written
       on purpose now, and the caller counts it under its own name. */
    u.kind = (f.payloadState == kFileReadable && f.payloadLen > 0) ? kUpgradeFromPayload : kUpgradePositionOnly;
    return u;
}

}   /* namespace coopstore */
