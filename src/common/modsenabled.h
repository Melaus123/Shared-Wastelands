/* modsenabled.h - is a mod switched on in Kenshi's own mod list? Owner decisions 123 and 124 (2026-09-29).
 *
 * THE RULE (owner 123): the multiplayer mod starts ONLY when it is switched on in Kenshi's own mod list, on BOTH roads -
 * SharedWastelandsLoader.dll (the game's Plugins_x64.cfg line) and RE_Kenshi's PreloadPlugins (which loads every INSTALLED mod,
 * switched on or not). If the list cannot be read the mod stays OFF and says why (owner 124, fail closed). This header is the
 * one place the list's contents and its readability are judged; both roads call it.
 *
 * WHEN IT IS READ: after the launcher's OK. Kenshi's launcher writes the list when the player presses OK, and both roads
 * read it from inside Ogre's Root::initialise (an Ogre plugin's initialise call), which the game reaches only after the
 * launcher has closed with OK - so a tick or untick made in the launcher counts for the start it was made in.
 *
 * THE LIST: <game folder>\data\mods.cfg - one .mod file name per switched-on mod, one per line. The launcher writes it in
 * text mode, so lines end in CRLF (every bench instance's file is exactly "Shared Wastelands.mod\r\n"); a file edited by
 * hand may end its lines in LF or CR alone, and all three are accepted. A mod folder is switched on when one of the .mod
 * files in it is named on a line.
 *
 * ModsCfgReadState(opened, readOk, got): whether the bytes a road read can be judged at all - the file opened, the read
 * succeeded, and it is no larger than kModsCfgMaxBytes (a road reads up to kModsCfgMaxBytes + 1 bytes, so `got` above the
 * limit means the file is larger). Anything else is a reason to load nothing (fail closed); ModsCfgStateWhy words it.
 *
 * ModsCfgLists(text, len, modFileName): true when some line of `text` IS `modFileName`. A UTF-8 byte-order mark at the
 * start is skipped; lines end at LF, CRLF or CR; spaces and tabs around a line are ignored; the comparison ignores ASCII
 * case (other bytes must match exactly). A name that is only PART of a line does not match. Empty text, a null text, a
 * negative length or an empty name -> false.
 *
 * PURE, NO LIBRARY CALLS: SharedWastelandsLoader.dll has no C runtime (/NODEFAULTLIB) and includes this too. C++03 (VS2010).
 */
#ifndef KENSHICOOP_MODSENABLED_H
#define KENSHICOOP_MODSENABLED_H

namespace coopmods {

/* The largest mods.cfg either road reads. A bigger file is treated as unreadable (fail closed): at ~20 bytes a line that
   is some 3000 mods. */
enum { kModsCfgMaxBytes = 65536 };

/* Can the bytes a road read be judged? (fail closed: anything but kModsCfgUsable loads nothing) */
enum ModsCfgState { kModsCfgUsable = 0, kModsCfgNotOpened = 1, kModsCfgReadFailed = 2, kModsCfgTooLarge = 3 };

inline ModsCfgState ModsCfgReadState(bool opened, bool readOk, unsigned long got)
{
    if (!opened) return kModsCfgNotOpened;
    if (!readOk) return kModsCfgReadFailed;
    if (got > (unsigned long)kModsCfgMaxBytes) return kModsCfgTooLarge;
    return kModsCfgUsable;
}

/* The words for a state, after "Kenshi's mod list <path> " in a log line; "" for kModsCfgUsable. */
inline const char* ModsCfgStateWhy(ModsCfgState st)
{
    switch (st)
    {
    case kModsCfgNotOpened:  return "could not be opened";
    case kModsCfgReadFailed: return "could not be read";
    case kModsCfgTooLarge:   return "is larger than 64 KB";
    default:                 return "";
    }
}

inline char ModsLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

inline bool ModsCfgLists(const char* text, long len, const char* modFileName)
{
    if (text == 0 || len <= 0 || modFileName == 0 || modFileName[0] == 0) return false;
    long nameLen = 0;
    while (modFileName[nameLen] != 0) ++nameLen;
    long i = 0;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) i = 3;
    while (i < len)
    {
        long end = i;
        while (end < len && text[end] != '\n' && text[end] != '\r') ++end;
        long a = i, b = end;   /* the line is [a, b) */
        while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
        if (b - a == nameLen)
        {
            long k = 0;
            while (k < nameLen && ModsLower(text[a + k]) == ModsLower(modFileName[k])) ++k;
            if (k == nameLen) return true;
        }
        i = end + 1;   /* past the '\n' or '\r'; a CRLF's '\n' is then an empty line */
    }
    return false;
}

}   /* namespace coopmods */

#endif
