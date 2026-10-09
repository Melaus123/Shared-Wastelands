/* src/common/saywire.h - P3 (read-parity3 GAP 3; the user saw it in T244): THE MSG_SAY PAYLOAD.
 *
 * NPC speech bubbles showed on the game driving a character and never on its copy.  The engine picks the
 * line and its text variant with the C runtime's rand() (read-parity3 GAP 3, Confirmed), so a copy cannot
 * reproduce the line; the driving game sends the exact text it said instead.
 *
 *   uid u32 | len u16 (0..kSayMaxText) | len bytes of text
 *
 * Built and parsed here only, so the offline suite (src/coop-test/test_main.cpp) hits the SAME bytes
 * net/session.cpp sends and reads (lesson 11).  The length test on decode is a MINIMUM: bytes after the
 * text are ignored, so a later build may append a field without a protocol bump.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace coopsay {

const unsigned int kSayMaxText = 512;   /* bytes; a longer line is not sent (sayTooLong) */

const int kSayDecodeOk        = 0;
const int kSayDecodeTooShort  = 1;   /* fewer than the 6 fixed bytes */
const int kSayDecodeTooLong   = 2;   /* the length field names more than kSayMaxText bytes */
const int kSayDecodeTextTrunc = 3;   /* the payload ends before the text does */

/* false (and nothing appended) when the text is longer than kSayMaxText. */
inline bool EncodeSay(std::vector<char>* b, unsigned int uid, const char* text, size_t len)
{
    if (b == 0 || len > (size_t)kSayMaxText || (len > 0 && text == 0)) return false;
    const unsigned short n = (unsigned short)len;
    const size_t at = b->size();
    b->resize(at + 6 + len);
    std::memcpy(&(*b)[at], &uid, 4);
    std::memcpy(&(*b)[at + 4], &n, 2);
    if (len > 0) std::memcpy(&(*b)[at + 6], text, len);
    return true;
}

/* Every test is `size - off < n` with `off` already bounded by `size`, so a hostile length cannot wrap. */
inline int DecodeSay(const char* p, size_t size, unsigned int* uid, std::string* text)
{
    unsigned int u = 0;
    unsigned short n = 0;
    if (p == 0 || size < 6) return kSayDecodeTooShort;
    std::memcpy(&u, p, 4);
    std::memcpy(&n, p + 4, 2);
    if ((unsigned int)n > kSayMaxText) return kSayDecodeTooLong;
    if (size - 6 < (size_t)n) return kSayDecodeTextTrunc;
    if (uid) *uid = u;
    if (text) text->assign(p + 6, (size_t)n);
    return kSayDecodeOk;
}

/* ---- P3-b (review-p3): the two decisions speech.cpp makes, pure so the offline suite hits them. ---- */

/* EventTriggerEnum EV_PLAYER_TALK_TO_ME (Dialogue::sendEvent 0x683F00 sends event 1 to
   startPlayerConversation 0x683890 and every other event to startConversation 0x683500 - Read). */
const int kEvPlayerTalkToMe = 1;

/* Dialogue::sendEvent gate: 1 = run the engine's sendEvent, 0 = skip it.  A copy driven by the other game
   never starts a chat of its own; the player talking to it is the one event it still takes. */
inline int SendEventAllowed(int speakerIsPuppet, int ev)
{
    return (speakerIsPuppet == 0 || ev == kEvPlayerTalkToMe) ? 1 : 0;
}

/* What the say() detour does with one call. */
const int kSayActApply        = 0;   /* our own apply: the original, counted at the apply */
const int kSayActPassOffThread = 1;  /* not the main thread: the original, untouched */
const int kSayActPassNoUid    = 2;   /* an unreplicated speaker: the original, untouched */
const int kSayActPassReplay   = 3;   /* a parked line the plugin already handled: the original, not re-sent */
const int kSayActSend         = 4;   /* authored here, a new line: the original, then MSG_SAY */
const int kSayActReplaySend   = 5;   /* authored here, a parked line the plugin never handled: the original, then MSG_SAY */
const int kSayActDrop         = 6;   /* a copy's own engine-chosen line: dropped */
const int kSayActReplayDrop   = 7;   /* a copy's parked line the plugin never handled: dropped */

inline int SayClassify(int applying, int onMainThread, int hasUid, int mine, int replay, int handled)
{
    if (applying) return kSayActApply;
    if (!onMainThread) return kSayActPassOffThread;
    if (!hasUid) return kSayActPassNoUid;
    if (replay && handled) return kSayActPassReplay;
    if (mine) return replay ? kSayActReplaySend : kSayActSend;
    return replay ? kSayActReplayDrop : kSayActDrop;
}

/* FNV-1a over the text's bytes. */
inline unsigned int SayTextHash(const char* p, size_t n)
{
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 16777619u; }
    return h;
}

/* WHICH PARKED LINES THE PLUGIN HANDLED.  say() parks its text in Dialogue +0x58 when it cannot show it yet,
   and Dialogue::update re-says it later.  A replay is ours only if the plugin handled THAT text on THAT
   Dialogue (sent it, or applied it); anything else parked there (an off-thread call) is a new line.  Fixed
   size, no allocation, MAIN THREAD only; the oldest slot is reused when full. */
const int kSayMarkSlots = 64;
struct SayMarkTable
{
    const void*  dlg[kSayMarkSlots];
    unsigned int hash[kSayMarkSlots];
    unsigned int len[kSayMarkSlots];
    int          next;
    SayMarkTable() : next(0) { for (int i = 0; i < kSayMarkSlots; ++i) { dlg[i] = 0; hash[i] = 0; len[i] = 0; } }
    void Mark(const void* d, unsigned int h, unsigned int n)
    {
        if (d == 0) return;
        int at = -1;
        for (int i = 0; i < kSayMarkSlots; ++i) if (dlg[i] == d) { at = i; break; }
        if (at < 0) { at = next; next = (next + 1) % kSayMarkSlots; }
        dlg[at] = d; hash[at] = h; len[at] = n;
    }
    /* 1 if `d` holds exactly this text, and the mark is spent; 0 otherwise (the mark, if any, is spent too:
       whatever the Dialogue held is being replaced by this replay). */
    int Take(const void* d, unsigned int h, unsigned int n)
    {
        if (d == 0) return 0;
        for (int i = 0; i < kSayMarkSlots; ++i)
            if (dlg[i] == d)
            {
                const int hit = (hash[i] == h && len[i] == n) ? 1 : 0;
                dlg[i] = 0; hash[i] = 0; len[i] = 0;
                return hit;
            }
        return 0;
    }
};

/* ---- P26 stage 0 (.modding/investigations/p26-npc-dialogue.md): who a conversation start is aimed at. speech.cpp's
   log-only detours on Dialogue::startConversation 0x683500 / startPlayerConversation 0x683890 log and count every start
   under one of these kinds, for the speaker and for the target. Pure, so the offline suite hits it. ---- */
const int kTalkTargetNone        = 0;   /* no character */
const int kTalkTargetMine        = 1;   /* this game's own player character */
const int kTalkTargetPartnerCopy = 2;   /* the other player's character - a copy here (the coop-peer stand-in faction) */
const int kTalkTargetNpcMine     = 3;   /* a non-player character this game drives, or one with no uid */
const int kTalkTargetNpcCopy     = 4;   /* a non-player character the other game drives (a copy here) */
const int kTalkTargetUnread      = 5;   /* a character whose faction could not be read */

/* present: a character pointer at all; factionRead: its faction was read; playerFaction: this game's own player faction
   (Faction +0x250 != 0); peerFaction: the other player's stand-in faction (coop-p<n>); hasUid / uidMine: it is replicated,
   and this game owns that uid.  The stand-in faction wins; a player-faction character whose uid another game owns is the
   partner's too. */
inline int TalkTargetKind(int present, int factionRead, int playerFaction, int peerFaction, int hasUid, int uidMine)
{
    if (!present) return kTalkTargetNone;
    if (!factionRead) return kTalkTargetUnread;
    const int othersUid = (hasUid && !uidMine) ? 1 : 0;
    if (peerFaction) return kTalkTargetPartnerCopy;
    if (playerFaction) return othersUid ? kTalkTargetPartnerCopy : kTalkTargetMine;
    return othersUid ? kTalkTargetNpcCopy : kTalkTargetNpcMine;
}

inline const char* TalkTargetName(int k)
{
    switch (k)
    {
    case kTalkTargetNone:        return "none";
    case kTalkTargetMine:        return "mine";
    case kTalkTargetPartnerCopy: return "partnerCopy";
    case kTalkTargetNpcMine:     return "npcMine";
    case kTalkTargetNpcCopy:     return "npcCopy";
    case kTalkTargetUnread:      return "unread";
    default:                     return "?";
    }
}

/* Per-event counters: EventTriggerEnum 0..127 each, then one bucket for anything else (a negative or larger value). */
const int kTalkEventBuckets = 129;
inline int TalkEventBucket(int ev) { return (ev >= 0 && ev < kTalkEventBuckets - 1) ? ev : kTalkEventBuckets - 1; }

/* One token as an unsigned number: decimal, or hex with 0x. 1 parsed (<= 0xFFFFFFFF), 0 not a number. */
inline int TalkParseNumber(const std::string& s, unsigned long long* out)
{
    size_t i = 0;
    unsigned int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    if (i >= s.size()) return 0;
    unsigned long long v = 0;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        unsigned int d = 0;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else return 0;
        v = v * base + d;
        if (v > 0xFFFFFFFFull) return 0;
    }
    *out = v;
    return 1;
}

/* `talktest [npcUid|near] [targetUid|near] [event]` (TEST-ONLY lever): 1 parsed, 0 malformed. A uid of 0 means `near`
   (a uid itself must be non-zero); the event is 0..255, decimal or 0x hex, default 1 (EV_PLAYER_TALK_TO_ME). */
/* P25: nearCopy (optional) - the NPC token `nearcopy` = the nearest NPC that is a COPY the other game drives (1); without the out
   flag `nearcopy` is refused as before. */
inline int TalkTestParse(const std::string& arg, unsigned int* npcUid, unsigned int* targetUid, int* ev, int* nearCopy = 0)
{
    *npcUid = 0; *targetUid = 0; *ev = kEvPlayerTalkToMe;
    if (nearCopy != 0) *nearCopy = 0;   /* P25 */
    std::string tok[3];
    int n = 0;
    size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        size_t j = i;
        while (j < arg.size() && arg[j] != ' ' && arg[j] != '\t') ++j;
        if (n == 3) return 0;
        tok[n++] = arg.substr(i, j - i);
        i = j;
    }
    for (int k = 0; k < n && k < 2; ++k)
    {
        if (tok[k] == "near") continue;
        if (k == 0 && nearCopy != 0 && tok[k] == "nearcopy") { *nearCopy = 1; continue; }   /* P25 */
        unsigned long long v = 0;
        if (!TalkParseNumber(tok[k], &v) || v == 0) return 0;
        if (k == 0) *npcUid = (unsigned int)v; else *targetUid = (unsigned int)v;
    }
    if (n == 3)
    {
        unsigned long long v = 0;
        if (!TalkParseNumber(tok[2], &v) || v > 255) return 0;
        *ev = (int)v;
    }
    return 1;
}

/* P26 stage 6 talksight: a dialogue name as a command token - lower case; every run of characters other than a-z / 0-9 becomes one
   '_', none at either end ("Bag check (Shek)" -> "bag_check_shek"; the harness's argument shape has no spaces or brackets). */
inline std::string TalkNameKey(const std::string& s)
{
    std::string out;
    int gap = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char ch = s[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) { gap = 1; continue; }
        if (gap && !out.empty()) out += '_';
        gap = 0;
        out += ch;
    }
    return out;
}

/* arg split on spaces / tabs into tok[0..cap); the count, -1 = more than cap tokens. */
inline int TalkSplit(const std::string& arg, std::string* tok, int cap)
{
    int n = 0;
    size_t i = 0;
    while (i < arg.size())
    {
        while (i < arg.size() && (arg[i] == ' ' || arg[i] == '\t')) ++i;
        if (i >= arg.size()) break;
        size_t j = i;
        while (j < arg.size() && arg[j] != ' ' && arg[j] != '\t') ++j;
        if (n == cap) return -1;
        tok[n++] = arg.substr(i, j - i);
        i = j;
    }
    return n;
}

/* `talksight <npcUid | nearest <dialogueName>> <targetUid> [seconds] [walkover]` (TEST-ONLY lever): 1 parsed, 0 malformed.
   nearest: *npcUid = 0 and *npcKey = TalkNameKey(name) (never empty); uids non-zero; seconds 1..3600, default 120; walkover = keep
   trying past a started conversation whose line does not walk over. */
const int kTalkSightDefaultSeconds = 120, kTalkSightMaxSeconds = 3600;
inline int TalkSightParse(const std::string& arg, unsigned int* npcUid, std::string* npcKey, unsigned int* targetUid, int* seconds, int* untilWalk)
{
    *npcUid = 0; npcKey->clear(); *targetUid = 0; *seconds = kTalkSightDefaultSeconds; *untilWalk = 0;
    std::string tok[6];
    const int n = TalkSplit(arg, tok, 6);
    if (n < 2) return 0;
    int k = 0;
    unsigned long long v = 0;
    if (tok[0] == "nearest")
    {
        *npcKey = TalkNameKey(tok[1]);
        if (npcKey->empty()) return 0;
        k = 2;
    }
    else
    {
        if (!TalkParseNumber(tok[0], &v) || v == 0) return 0;
        *npcUid = (unsigned int)v;
        k = 1;
    }
    if (k >= n || !TalkParseNumber(tok[k], &v) || v == 0) return 0;
    *targetUid = (unsigned int)v;
    ++k;
    if (k < n && tok[k] != "walkover")
    {
        if (!TalkParseNumber(tok[k], &v) || v < 1 || v > (unsigned long long)kTalkSightMaxSeconds) return 0;
        *seconds = (int)v;
        ++k;
    }
    if (k < n && tok[k] == "walkover") { *untilWalk = 1; ++k; }
    return k == n ? 1 : 0;
}

/* `playerteleport near <uid | nearest <dialogueName>> <units>` (TEST-ONLY): 1 parsed, 0 malformed. units 1..2000. */
const int kTalkNearMaxUnits = 2000;
/* P26lvl maxdy: `playerteleport near <uid | nearest <dialogueName>> <units> [maxdy <n>]` (TEST-ONLY): 1 parsed, 0 malformed.
   units 1..2000; *maxdy = n (1..500) when given, else -1. */
const int kTalkNearMaxDy = 500;
inline int TalkTeleportNearParseDy(const std::string& arg, unsigned int* uid, std::string* key, int* units, int* maxdy)
{
    *uid = 0; key->clear(); *units = 0; *maxdy = -1;
    std::string tok[6];
    const int n = TalkSplit(arg, tok, 6);
    if (n < 2) return 0;
    int k = 0;
    unsigned long long v = 0;
    if (tok[0] == "nearest")
    {
        if (n < 3) return 0;
        *key = TalkNameKey(tok[1]);
        if (key->empty()) return 0;
        k = 2;
    }
    else
    {
        if (!TalkParseNumber(tok[0], &v) || v == 0) return 0;
        *uid = (unsigned int)v;
        k = 1;
    }
    if (!TalkParseNumber(tok[k], &v) || v < 1 || v > (unsigned long long)kTalkNearMaxUnits) return 0;
    *units = (int)v;
    ++k;
    if (k < n)
    {
        if (tok[k] != "maxdy" || k + 1 >= n || !TalkParseNumber(tok[k + 1], &v) || v < 1 || v > (unsigned long long)kTalkNearMaxDy) return 0;
        *maxdy = (int)v;
        k += 2;
    }
    return k == n ? 1 : 0;
}
/* the form without maxdy (a maxdy is refused here) */
inline int TalkTeleportNearParse(const std::string& arg, unsigned int* uid, std::string* key, int* units)
{
    int dy = -1;
    return (TalkTeleportNearParseDy(arg, uid, key, units, &dy) && dy < 0) ? 1 : 0;
}

/* P25 T729 (TEST-ONLY name levers): a character's display name as a comparison key - lower case; every run of spaces, tabs and '_'
   becomes one space, none at either end ("Bar Thug" and the harness token "Bar_Thug" both key as "bar thug"). */
inline std::string TalkPersonKey(const std::string& s)
{
    std::string out;
    int gap = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char ch = s[i];
        if (ch == ' ' || ch == '\t' || ch == '_') { gap = 1; continue; }
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (gap && !out.empty()) out += ' ';
        gap = 0;
        out += ch;
    }
    return out;
}

/* P25 T729: the name words tok[*k..] up to (not including) the first word that is a number; *name = their TalkPersonKey, *k = the
   number's index. 1 = at least one name word, a non-empty key and a number after it; 0 otherwise (a word with '{' or '}' - an
   unresolved harness placeholder - is refused, never taken as part of a name). */
inline int TalkNameUpToNumber(const std::string* tok, int n, int* k, std::string* name)
{
    std::string joined;
    unsigned long long v = 0;
    int i = *k;
    for (; i < n && !TalkParseNumber(tok[i], &v); ++i)
    {
        if (tok[i].find('{') != std::string::npos || tok[i].find('}') != std::string::npos) return 0;
        if (!joined.empty()) joined += ' ';
        joined += tok[i];
    }
    if (i == *k || i >= n) return 0;
    *name = TalkPersonKey(joined);
    if (name->empty()) return 0;
    *k = i;
    return 1;
}

const int kTalkNameMaxTokens = 16;
/* P25 T729: `talktest nearestname <npc name> <targetUid> [event]` (TEST-ONLY): 1 parsed, 0 malformed. The name is every word up
   to the first number (a name cannot itself hold a number-only word); *name = its TalkPersonKey; target non-zero; event 0..255,
   decimal or 0x hex, default 1 (EV_PLAYER_TALK_TO_ME). P25 T729b: a last word `copy` (*own = 0: only a copy the other game drives)
   or `mine` (*own = 1: only this game's own); *own = -1 without it (any). */
inline int TalkTestNameParse(const std::string& arg, std::string* name, unsigned int* targetUid, int* ev, int* own)
{
    name->clear(); *targetUid = 0; *ev = kEvPlayerTalkToMe; *own = -1;
    std::string tok[kTalkNameMaxTokens];
    const int n = TalkSplit(arg, tok, kTalkNameMaxTokens);
    if (n < 3 || tok[0] != "nearestname") return 0;
    int k = 1;
    if (!TalkNameUpToNumber(tok, n, &k, name)) return 0;
    unsigned long long v = 0;
    if (!TalkParseNumber(tok[k], &v) || v == 0) return 0;
    *targetUid = (unsigned int)v;
    ++k;
    if (k < n && TalkParseNumber(tok[k], &v))
    {
        if (v > 255) return 0;
        *ev = (int)v;
        ++k;
    }
    if (k < n && (tok[k] == "copy" || tok[k] == "mine")) { *own = (tok[k] == "mine") ? 1 : 0; ++k; }   /* P25 T729b */
    return k == n ? 1 : 0;
}

/* `talktest nearestfaction <faction name> <targetUid> [event] [copy | mine]` (TEST-ONLY): as TalkTestNameParse, the faction name in
   place of the NPC's name (*name = its TalkPersonKey). 1 parsed, 0 malformed. */
inline int TalkTestFactionParse(const std::string& arg, std::string* name, unsigned int* targetUid, int* ev, int* own)
{
    const std::string w = "nearestfaction";
    const size_t at = arg.find(w);
    if (at == std::string::npos || arg.find_first_not_of(" \t") != at)
    {
        name->clear(); *targetUid = 0; *ev = kEvPlayerTalkToMe; *own = -1;
        return 0;
    }
    return TalkTestNameParse("nearestname" + arg.substr(at + w.size()), name, targetUid, ev, own);
}

/* P25 T729: `playerteleport near name <npc name> <units> [maxdy <n>]` (TEST-ONLY): 1 parsed, 0 malformed. The name as
   TalkTestNameParse's (the word maxdy inside it is refused: units are missing); units 1..2000; *maxdy = n (1..500), else -1. */
inline int TalkTeleportNearNameParse(const std::string& arg, std::string* name, int* units, int* maxdy)
{
    name->clear(); *units = 0; *maxdy = -1;
    std::string tok[kTalkNameMaxTokens];
    const int n = TalkSplit(arg, tok, kTalkNameMaxTokens);
    if (n < 3 || tok[0] != "name") return 0;
    int k = 1;
    if (!TalkNameUpToNumber(tok, n, &k, name)) return 0;
    if ((" " + *name + " ").find(" maxdy ") != std::string::npos) return 0;
    unsigned long long v = 0;
    if (!TalkParseNumber(tok[k], &v) || v < 1 || v > (unsigned long long)kTalkNearMaxUnits) return 0;
    *units = (int)v;
    ++k;
    if (k < n)
    {
        if (tok[k] != "maxdy" || k + 1 >= n || !TalkParseNumber(tok[k + 1], &v) || v < 1 || v > (unsigned long long)kTalkNearMaxDy) return 0;
        *maxdy = (int)v;
        k += 2;
    }
    return k == n ? 1 : 0;
}

/* The carried hand-in fixture's TEST-ONLY levers (crimetest bountyset, koself name, capturetest carry name, talkcarriers ev/act).
   A name is ONE token with '_' standing for a space, compared as its TalkPersonKey; "" = not a usable name (an empty key, a number,
   or an unresolved harness placeholder holding '{' or '}'). */
inline std::string TalkLeverNameKey(const std::string& tok)
{
    unsigned long long v = 0;
    if (tok.find('{') != std::string::npos || tok.find('}') != std::string::npos || TalkParseNumber(tok, &v)) return std::string();
    return TalkPersonKey(tok);
}

const int kLeverBountyMaxAmount = 1000000;
/* `crimetest bountyset <personName> <lawNpcName> <amount>` - arg is what follows `bountyset`: two name tokens, then the amount
   1..kLeverBountyMaxAmount (decimal or 0x hex). 1 parsed, 0 malformed. */
inline int BountySetParse(const std::string& arg, std::string* person, std::string* law, int* amount)
{
    person->clear(); law->clear(); *amount = 0;
    std::string tok[4];
    unsigned long long v = 0;
    if (TalkSplit(arg, tok, 4) != 3) return 0;
    const std::string p = TalkLeverNameKey(tok[0]), l = TalkLeverNameKey(tok[1]);
    if (p.empty() || l.empty() || !TalkParseNumber(tok[2], &v) || v < 1 || v > (unsigned long long)kLeverBountyMaxAmount) return 0;
    *person = p; *law = l; *amount = (int)v;
    return 1;
}

const int kLeverKoMaxSeconds = 600;
/* `koself name <name> [seconds]` - arg is what follows `koself`: the word name, one name token, then seconds 0..600 (default 0 =
   the engine's own wake-up clock, as `koself <uid>`). 1 parsed, 0 not this form or malformed. */
inline int KoSelfNameParse(const std::string& arg, std::string* name, int* seconds)
{
    name->clear(); *seconds = 0;
    std::string tok[4];
    unsigned long long v = 0;
    const int n = TalkSplit(arg, tok, 4);
    if (n < 2 || n > 3 || tok[0] != "name") return 0;
    const std::string k = TalkLeverNameKey(tok[1]);
    if (k.empty()) return 0;
    if (n == 3 && (!TalkParseNumber(tok[2], &v) || v > (unsigned long long)kLeverKoMaxSeconds)) return 0;
    *name = k; *seconds = (int)v;
    return 1;
}

/* `capturetest carry <carrierUid> name <name>` - arg is the whole capturetest argument. 1 parsed (carrier non-zero), 0 not this
   form or malformed. */
inline int CarryNameParse(const std::string& arg, unsigned int* carrier, std::string* name)
{
    *carrier = 0; name->clear();
    std::string tok[5];
    unsigned long long v = 0;
    if (TalkSplit(arg, tok, 5) != 4 || tok[0] != "carry" || tok[2] != "name" || !TalkParseNumber(tok[1], &v) || v == 0) return 0;
    const std::string k = TalkLeverNameKey(tok[3]);
    if (k.empty()) return 0;
    *carrier = (unsigned int)v; *name = k;
    return 1;
}

/* `talkcarriers <x> <z> <radius> [ev <n>] [act <type>]` - rest is what follows the radius: each option at most once, in either
   order, n and type 0..255 (decimal or 0x hex). *ev / *act = -1 when absent. 1 parsed, 0 malformed. */
inline int TalkCarriersOptParse(const std::string& rest, int* ev, int* act)
{
    *ev = -1; *act = -1;
    std::string tok[5];
    const int n = TalkSplit(rest, tok, 5);
    if (n < 0 || (n % 2) != 0) return 0;
    int e = -1, a = -1;
    for (int k = 0; k < n; k += 2)
    {
        unsigned long long v = 0;
        if (!TalkParseNumber(tok[k + 1], &v) || v > 255) return 0;
        int* slot = tok[k] == "ev" ? &e : (tok[k] == "act" ? &a : 0);
        if (slot == 0 || *slot >= 0) return 0;
        *slot = (int)v;
    }
    *ev = e; *act = a;
    return 1;
}

}   /* namespace coopsay */
