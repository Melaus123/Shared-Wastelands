/* src/common/addrtable.h - THE ADDRESS TABLE'S FORMAT AND ITS FINGERPRINT, AND NOTHING ELSE
 * (P8h, effort "executable-fingerprint address tables", approved 2026-09-04).
 *
 * THE ONE RULE THIS FILE OBEYS, and it is clockmath.h's and storemeta.h's rule for the same reason: nothing in
 * here reads a global, calls the operating system, or includes an Ogre, ENet or Windows header.
 * Everything is a pure function of its arguments, so the parser and the fingerprint arithmetic are compiled
 * into the game plugin AND into the offline test exe and cannot drift apart (6a lesson 11).
 *
 * WHAT THE TABLE IS FOR. Every address this mod uses was verified against ONE executable: the RE_Kenshi-patched
 * Kenshi 1.0.65 x64 Steam binary (`.modding/01-environment.md`). Store editions and patches are different
 * executables with different addresses. The mod is going to be public, so the plugin must be able to tell
 * "this is the build my numbers were read from" from "this is some other build", and on the second answer it
 * must install NOTHING and say so once - a refusal, never a crash (user, approved).
 *
 * THE FINGERPRINT is four 32-bit numbers taken from the executable FILE ON DISK, printed as
 *      TTTTTTTT-SSSSSSSS-CCCCCCCC-FFFFFFFF
 *   T  the PE header's TimeDateStamp   (the second the linker stamped this build)
 *   S  the PE header's SizeOfImage
 *   C  CRC-32 of the file's first 65,536 bytes
 *   F  the file's byte length, low 32 bits
 * and the table for a build is the file `addresses/<fingerprint>.txt` beside the plugin, so choosing a table is
 * one open() and never a directory sweep.
 *
 * WHY NOT SHA-256 OF THE WHOLE FILE. Measured on this machine: 28 ms for the 35 MB executable warm, against
 * 0.10-0.33 ms for the four numbers above - 100x, at every game launch, and the whole file has to be read from
 * disk cold. WHY NOT THE PE CheckSum FIELD: it is 0x00000000 in BOTH Kenshi executables on this machine
 * (the running 1.0.65 and the vanilla 1.0.68 kept beside it), so it carries no information at all here.
 *
 * WHAT THE FINGERPRINT CANNOT DO, stated plainly: it is a BUILD LABEL, not a tamper check. An executable
 * byte-patched after linking - a crack, a community patch - keeps its TimeDateStamp, its SizeOfImage and its
 * length, and unless the patch lands in the first 64 KB it keeps the CRC too, so it would select this table.
 * THAT is why every entry carries its own bytes: the fingerprint picks a table, and the per-entry byte check is
 * what actually decides whether these addresses are real in this process.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no >> template closer.
 */
#ifndef COOP_COMMON_ADDRTABLE_H
#define COOP_COMMON_ADDRTABLE_H

#include <cstddef>
#include <string>

namespace coopaddr {

/* WHAT AN ENTRY IS. Four of the five kinds carry bytes that can be compared against the loaded image; the
   fifth cannot, and says so rather than pretending. */
enum AddrKind
{
    kAddrCode = 0,   /* a function entry. bytes are read at verifyAt, which is rva, or rva+16 for an entry
                        another mod has already hooked by the time we look (RE_Kenshi patches one). */
    kAddrRet  = 1,   /* an interior return address. bytes are the CALL instruction that returns to rva,
                        so verifyAt = rva - nbytes. */
    kAddrRo   = 2,   /* read-only initialised data (a float, a threshold). bytes are the image's own. */
    kAddrPtr  = 3,   /* a relocated pointer in read-only data (a vtable slot). ptrRva is the RVA the
                        pointer must resolve to once the image's own load address is subtracted. */
    kAddrVar  = 4,   /* a WRITABLE global. There is nothing constant to compare: whatever is there is what
                        the process has written. NOT VERIFIED. Counted separately and never counted as a pass. */
    kAddrKindCount = 5
};

const int kAddrNameMax  = 48;   /* including the terminator */
const int kAddrBytesMax = 16;
const int kAddrMaxEntries = 512;

struct AddrEntry
{
    char          name[kAddrNameMax];
    int           kind;
    unsigned int  rva;
    unsigned int  verifyAt;
    unsigned int  ptrRva;                       /* kAddrPtr only */
    unsigned char bytes[kAddrBytesMax];
    int           nbytes;                       /* 0 for kAddrVar and kAddrPtr */
    AddrEntry();
};

/* The four numbers, printed. Pure; it cannot fail and it allocates one short string. */
std::string AddrFingerprint(unsigned int timeDateStamp, unsigned int sizeOfImage,
                            unsigned int crc32First64k, unsigned int fileSizeLow32);

/* Reads TimeDateStamp and SizeOfImage out of the first bytes of a PE image (file order). Returns 1 on
   success, 0 if `head` is not a 64-bit PE - in which case NOTHING is written through the pointers, so a
   caller cannot accidentally fingerprint a half-read buffer. `n` bounds every read. */
int AddrReadPeHeader(const unsigned char* head, std::size_t n,
                     unsigned int* timeDateStamp, unsigned int* sizeOfImage);

/* What the parse saw. Every field is filled even on failure - a refusal that cannot be interrogated is not a
   measurement (6a lesson 12), so this reports its INPUTS and not only its verdict. */
struct AddrParseResult
{
    int          lines;          /* lines read, comments and blanks included */
    int          entries;        /* entries stored in `out` */
    int          badLines;       /* lines that looked like an entry and were refused */
    int          overflow;       /* entries dropped because `maxEntries` was reached */
    int          declaredCount;  /* the file's own !count, or -1 if it had none */
    std::string  fingerprint;    /* the file's own !fingerprint, or "" */
    std::string  version;        /* the file's own !version, or "" */
    std::string  firstBad;       /* the first refused line, verbatim and truncated - names itself */
    int          firstBadLine;
    AddrParseResult();
};

/* Parses `n` bytes of table text into `out`. Never throws, never allocates per entry, never reads past `n`.
   Returns the number of entries stored. A line it cannot parse is COUNTED AND NAMED, never guessed at. */
int AddrParseTable(const char* text, std::size_t n,
                   AddrEntry* out, int maxEntries, AddrParseResult* result);

/* THE COMPARISON THE GATE MAKES, in one place so the plugin and the offline suite cannot hold two ideas of
   it. Returns 1 only when the entry HAS bytes and all of them are what `got` holds; 0 otherwise, and an entry
   with no bytes at all (a kAddrVar, or a damaged line) is a 0 - never a pass by default. */
int AddrBytesMatch(const AddrEntry& e, const unsigned char* got);

/* Finds an entry by name. Returns 0 when there is none - the caller decides what that means. */
const AddrEntry* AddrFind(const AddrEntry* entries, int count, const char* name);

const char* AddrKindName(int kind);

/* gog1 fold 2 - THE REGISTRY'S SLOTS (addresses.cpp's AddrReg): a name the plugin asks for and where its RVA goes.
   POD, so a zero-initialised array of them is valid before any static constructor runs. */
struct AddrSlot { const char* name; unsigned long long* slot; };

/* Every slot to 0: a refusal that leaves no address to anyone. */
void AddrClearSlots(const AddrSlot* slots, int n);

/* THE BIND AND ITS VERDICT, after the per-row check against the running game. Writes every slot whose name has an
   entry; a name with none is not written and is counted in *missing (*firstMissing = its index, or -1). Returns 1
   accepted (mismatched == 0 and nothing missing), else 0 - and on the PATTERN road (byPattern != 0) a refusal then
   clears EVERY slot, so a signature set that failed its check gives no address to anyone. On the table road a
   refusal leaves the slots filled (addresses.h, AddrAbs: a refusal does not mean 0). */
int AddrSettleSlots(const AddrEntry* entries, int count, const AddrSlot* slots, int n, int mismatched, int byPattern,
                    int* missing, int* firstMissing);

}   /* namespace coopaddr */

#endif
