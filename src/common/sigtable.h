/* src/common/sigtable.h - EVERY ADDRESS ROW FOUND BY ITS BYTES, for an executable no table fingerprint matches
 * (T-63 stage 7, owner decision 99: "GOG 1.0.68/1.0.65 via byte-pattern function lookup, claimed after a GOG
 * player's test").
 *
 * addrtable.h's rule holds here too: nothing in this file reads a global, calls the operating system or includes
 * a Windows header. The parser, the PE-section reader and the resolver are pure functions of their arguments, so
 * the plugin and the offline test exe compile the SAME code (6a lesson 11), and tools/prove_signatures.py is a
 * line-by-line re-implementation of it in Python that runs against both Steam executables.
 *
 * THE FILE is addresses/signatures.sig beside the plugin, made by tools/gen_signatures.py from the two Steam exes
 * (!sigversion 2; any other version is refused):
 *     <name> <kind> <anchor> <pattern> <a> <b> <c> <d> <check> <section> <rank> <extra>
 * pattern  hex bytes, "??" = any byte. anchor = offset of 4 FIXED pattern bytes the scanner indexes on.
 * P below is the RVA where the pattern starts; the pattern is searched in the executable FILE's .text raw bytes
 * (the file, not the loaded image: the file cannot have been hooked by another mod first).
 *   code  rva = P + a; verifyAt = rva + b (b = 0 or 16); the c bytes at verifyAt become the row's bytes
 *   ret   rva = P + a; verifyAt = rva - b; the b bytes there must fit <check> (the CALL) and become the row's bytes
 *   var   rva = (P + a) + b + disp32 at (P + a) + c   - a RIP-relative operand's target; must be in a WRITABLE section
 *   ro    rva as var; must be in a read-only, non-code section; the bytes at rva + d must fit <check>
 *   ptr   rva as var (a vtable); read-only, non-code; slot = rva + d; the pointer in the slot, through up to four
 *         jmp stubs, must land in .text on bytes that fit <check>; ptrRva = the slot's pointer - ImageBase
 * section  the resolved row must lie in the section of exactly this name (.text / .rdata / .data / .idata ...)
 * rank     the row's position in Steam 1.0.68 address order within its class (code+ret, var, ro+ptr);
 *          unique within its class - a repeated one makes the line unreadable (plugin and prover alike)
 * extra    '-' or comma-separated: g:<anchor>:<pattern>:<a>:<b>:<c> (a var row's second referencing instruction),
 *          k:<off>:<row> (a direct CALL at pattern offset off whose target, through up to four jmp stubs, is <row>)
 * EVERY row must match exactly once; a row with no signature ('!nosig') or any row that matches 0 or 2+ times, or
 * fails its check, makes SigResolveAll return 0 and the caller refuses exactly as it does with no table.
 * THE CROSS-CHECKS (T-63 gog1 fold; the per-row check against the running process copies its expected bytes from the
 * same file that was searched, so it cannot catch a pattern that found the WRONG place - these can): after every row
 * resolved, (a) ORDER - within each class the addresses rise in rank order (0 inversions between the two Steam builds,
 * measured), so two rows whose patterns found each other's places are refused; (b) DISTINCT - no two rows share an
 * address; (c) GLOBALS TWICE - every var row resolves again, to the same address, through a second
 * referencing instruction - a DIFFERENT instruction from the row's own, which is all the check requires (it need not
 * be in another function); (d) CALL TARGETS - every recorded call lands on the row it named. Any failure refuses.
 *
 * WHAT THIS PROVES AND WHAT IT DOES NOT: it reproduces both Steam tables exactly (tools/prove_signatures.py). On any
 * other build (GOG) a unique match is a strong hint, not a proof - the per-row verification in addresses.cpp still
 * runs, and a GOG claim waits for a GOG player's test (owner 99).
 *
 * C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_SIGTABLE_H
#define COOP_COMMON_SIGTABLE_H

#include <cstddef>
#include <string>
#include <vector>

#include "addrtable.h"

namespace coopsigtab {

const int kSigPatMax = 4096;

struct SigRow
{
    std::string                name;
    int                        kind;       /* coopaddr::AddrKind */
    int                        anchor;
    std::vector<unsigned char> pat;
    std::vector<unsigned char> fix;        /* 1 = the byte must equal pat[i] */
    long long                  a, b, c, d;
    std::vector<unsigned char> chk;
    std::vector<unsigned char> chkFix;     /* empty chk = no check ('-') */
    std::string                sec;        /* the section name the row must resolve into */
    int                        rank;       /* position in 1.0.68 address order within the row's class */
    SigRow();
};

struct SigCall
{
    int         row;                       /* index into SigFile::rows */
    long long   off;                       /* offset of the E8 in that row's pattern */
    std::string target;                    /* the code row the call must land on */
};

struct SigFile
{
    std::vector<SigRow>      rows;
    std::vector<std::string> nosig;        /* rows the generator could not make a unique pattern for */
    std::vector<SigRow>      g2;           /* each var row's SECOND referencing pattern (kind var, a/b/c as the row's) */
    std::vector<int>         g2Of;         /* g2[i] belongs to rows[g2Of[i]] */
    std::vector<SigCall>     calls;
    int          sigversion;
    int          declaredCount;            /* !count, or -1 */
    int          declaredNosig;            /* !nosigcount, or -1 */
    int          badLines;
    int          firstBadLine;
    std::string  firstBad;
    SigFile();
};

/* Parses the file. Never throws, never reads past n. Returns the number of rows stored; a line it cannot parse is
   counted in badLines and the first one is named. */
int SigParseFile(const char* text, std::size_t n, SigFile* out);

/* The PE file as the plugin reads it: section headers only; every byte read is bounds-checked against len. */
struct PeSection
{
    unsigned int va, vsize, rawPtr, rawSize, chars;
    char         name[9];
};
struct PeView
{
    const unsigned char*   buf;
    std::size_t            len;
    unsigned long long     imageBase;
    std::vector<PeSection> secs;
    int                    text;           /* index of .text in secs, or -1 */
    PeView();
};
int SigParsePe(const unsigned char* buf, std::size_t len, PeView* out);   /* 1 = a PE32+ with a .text */
/* The file bytes backing [rva, rva+n) (inside one section's raw data), or 0. */
const unsigned char* PeAt(const PeView& pe, unsigned long long rva, std::size_t n);
/* The section whose [va, va + max(vsize, rawSize)) holds rva, or 0. */
const PeSection* PeSectionOf(const PeView& pe, unsigned long long rva);

/* One pass over .text for all patterns: rows first, then g2. counts[i] = matches of pattern i, firstRva[i] = the first
   match's pattern start (index rows.size() + j is g2[j]). */
void SigScanAll(const PeView& pe, const SigFile& f, std::vector<int>* counts, std::vector<unsigned int>* firstRva);

/* Turns one row's unique pattern start into its AddrEntry (with the verify bytes read from the file) and applies the
   row's checks. Returns 1, or 0 with *why filled. */
int SigRowEntry(const PeView& pe, const SigRow& r, unsigned int patRva, coopaddr::AddrEntry* e, std::string* why);

struct SigOutcome
{
    int                      resolved;
    int                      failed;       /* rows that matched 0 or 2+ times or failed a check, plus !nosig rows */
    std::vector<std::string> fails;        /* "<name>: <why>", every one */
    SigOutcome();
};
/* The four cross-checks over a fully resolved set: out[i] is rows[i]'s entry, starts[i] its pattern start, counts and
   firsts SigScanAll's (for g2). Appends one "<what>: ..." line per failure to oc->fails; returns the number of them. */
int SigCrossCheck(const PeView& pe, const SigFile& f, const coopaddr::AddrEntry* out, const std::vector<unsigned int>& starts,
                  const std::vector<int>& counts, const std::vector<unsigned int>& firsts, SigOutcome* oc);

/* Resolves every row. Returns the number of entries written to out ONLY when every row resolved and every cross-check
   passed (and the file is !sigversion 2 with no bad lines, no !nosig rows and its !count agrees); otherwise 0 and
   oc->fails says why, row by row. */
int SigResolveAll(const PeView& pe, const SigFile& f, coopaddr::AddrEntry* out, int maxEntries, SigOutcome* oc);

}   /* namespace coopsigtab */

#endif
