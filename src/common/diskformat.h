/* src/common/diskformat.h - THE ON-DISK FORMAT NUMBER OF THE MOD'S OWN FOLDERS, AS PURE DECISIONS (owner decision 429).

   Every world folder (<data folder>\worlds\<world>), joined-world folder (<data folder>\joined\<world>~<hex>) and records store
   (<data folder>\players\<store>) holds one file, swnames::kFormatFile, of one line: swformat<TAB><n><TAB><world|joined|records>.
   A build knows one number per folder kind. A folder at a lower number (or with no file: made before format numbers, number 0)
   is converted step by step to this build's number and the file is written last; a folder at a HIGHER number was written by a
   newer build and is refused - nothing in it is written, not even the file; an unreadable file is refused the same way.
   Kenshi's own save folders carry no format number: their key file is versioned by its tag.

   Same charter as worlddir.h: nothing here reads a global, opens a file, or includes a Windows header. C++03 (VS2010 v100). */
#ifndef SW_COMMON_DISKFORMAT_H
#define SW_COMMON_DISKFORMAT_H
#include <cstdio>
#include <string>
#include <vector>
#include "names.h"   /* SW_NAME: the refusal words name the mod */

namespace swformat {

enum FolderKind { kKindWorld = 0, kKindJoined = 1, kKindRecords = 2 };
const unsigned int kWorldFolderFormat   = 2;   /* step 0 -> 1: world.txt gains its id line; step 1 -> 2: a profiles.txt faction field may be '#' + hex (coopprof::FactionFieldFormat), which a build at 1 cannot read - a build at 1 refuses the folder instead of dropping those rows */
const unsigned int kJoinedFolderFormat  = 1;   /* step 0 -> 1: the same-name world folder's outage journal is taken when it is this world's */
const unsigned int kRecordsFolderFormat = 1;   /* step 0 -> 1: the number alone */
const unsigned int kFormatMax = 999999999u;

inline const char* FolderKindName(int kind) { return kind == kKindWorld ? "world" : kind == kKindJoined ? "joined" : kind == kKindRecords ? "records" : "?"; }
inline unsigned int FolderFormatKnown(int kind)
{
    return kind == kKindWorld ? kWorldFolderFormat : kind == kKindJoined ? kJoinedFolderFormat : kind == kKindRecords ? kRecordsFolderFormat : 0u;
}
inline std::string FormatText(int kind, unsigned int n)
{
    char b[16]; std::sprintf(b, "%u", n);
    return std::string("swformat\t") + b + "\t" + FolderKindName(kind) + "\n";
}

/* What the file says. exists = the file is there. Read = a line of this shape for THIS folder kind, *n its number (>= 1). Unreadable:
   *why (may be null) says what is wrong, for the log - the player sees the newer-version words either way. */
enum FormatRead { kFormatMissing = 0, kFormatRead = 1, kFormatUnreadable = 2 };
inline int FormatUnreadable(std::string* why, const std::string& w) { if (why != 0) *why = w; return kFormatUnreadable; }
inline int FormatParse(bool exists, const std::string& text, int kind, unsigned int* n, std::string* why = 0)
{
    if (n != 0) *n = 0;
    if (why != 0) why->clear();
    if (!exists) return kFormatMissing;
    std::string line = text.substr(0, text.find_first_of("\r\n"));
    if (line.empty()) return FormatUnreadable(why, "empty, or the file could not be read");
    if (line.compare(0, 9, "swformat\t") != 0) return FormatUnreadable(why, "the line does not start with swformat<TAB>");
    line.erase(0, 9);
    const std::string::size_type tab = line.find('\t');
    if (tab == std::string::npos) return FormatUnreadable(why, "no folder kind after the number");
    const std::string num = line.substr(0, tab), k = line.substr(tab + 1);
    if (num.empty() || num.size() > 9 || num.find_first_not_of("0123456789") != std::string::npos) return FormatUnreadable(why, "the number is not 1 to 9 digits");
    if (k != FolderKindName(kind)) return FormatUnreadable(why, "the folder kind is '" + k + "', not '" + FolderKindName(kind) + "'");
    unsigned int v = 0;
    for (size_t i = 0; i < num.size(); ++i) v = v * 10u + (unsigned int)(num[i] - '0');
    if (v == 0) return FormatUnreadable(why, "the number is 0");
    if (n != 0) *n = v;
    return kFormatRead;
}

/* Use = this build's number; Convert = missing or lower (FormatSteps says which steps); RefuseNewer = higher;
   RefuseUnreadable = a file that cannot be read as this folder's format line. */
enum FormatVerdict { kFormatUse = 0, kFormatConvert = 1, kFormatRefuseNewer = 2, kFormatRefuseUnreadable = 3 };
inline int FormatDecide(int readState, unsigned int found, unsigned int known)
{
    if (readState == kFormatMissing) return kFormatConvert;
    if (readState != kFormatRead) return kFormatRefuseUnreadable;
    if (found < known) return kFormatConvert;
    if (found == known) return kFormatUse;
    return kFormatRefuseNewer;
}
inline bool FormatRefuses(int verdict) { return verdict == kFormatRefuseNewer || verdict == kFormatRefuseUnreadable; }
inline const char* FormatVerdictName(int v)
{
    return v == kFormatUse ? "use" : v == kFormatConvert ? "convert" : v == kFormatRefuseNewer ? "refuseNewer" : v == kFormatRefuseUnreadable ? "refuseUnreadable" : "?";
}
/* The steps a conversion runs, in order: step s takes a folder from number s to s + 1 (from = 0 for a missing file). Each step
   can be run again safely; the number is written after the last. */
inline std::vector<unsigned int> FormatSteps(unsigned int from, unsigned int known)
{
    std::vector<unsigned int> out;
    for (unsigned int s = from; s < known; ++s) out.push_back(s);
    return out;
}

/* The world server's exit code when its world folder's format refuses it (the panel says the host refusal). */
const int kServerExitFormatRefused = 5;

/* The refusal words (owner, approved): the host's (CAN'T HOST), the joiner's (CAN'T JOIN) and the load's (CAN'T LOAD). */
inline const char* FormatHostRefusedText()
{
    return "Couldn't host: this world was saved by a newer version of " SW_NAME ". Update the mod, then try again.";
}
inline const char* FormatJoinRefusedText()
{
    return "Couldn't join: your copy of this world was saved by a newer version of " SW_NAME ". Update the mod, then try again.";
}
/* The load refusal's words without the opening sentence and the last full stop: the panel line is these words and a full stop,
   the notice FormatLoadRefusedText. */
inline const char* FormatLoadRefusedWords()
{
    return "It was made by a newer version of " SW_NAME ". Update the mod, then try again";
}
inline std::string FormatLoadRefusedText() { return std::string("You could not load that save. ") + FormatLoadRefusedWords() + "."; }

}   /* namespace swformat */

#endif
