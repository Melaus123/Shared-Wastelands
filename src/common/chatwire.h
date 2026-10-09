/* src/common/chatwire.h - IN-GAME TEXT CHAT, as the approved mock-up build/pages/text-chat-mockup.html shows it. The pure half: the chat message's bytes, the words every line says, the colours, the
 * TO choice's Tab order and search, the kept lines, the fade, which road a message takes, and chat.cfg's text.
 * src/coop-plugin/chat.cpp draws the window and feeds these.
 *
 * THE MESSAGE (inner type cooplive::kInnerChat, through the world server's relay; the server stamps the sender's slot, so the
 * sender's name is never read out of the message):
 *     u8 version (1) | u8 kind (0 everyone, 1 faction, 2 private) | u16 target, little-endian (the receiving player's slot for
 *     a private line, 0xFFFF for everyone and faction) | u16 n, little-endian | n bytes of UTF-8 text
 * with 1 <= n <= kMaxBytes, well-formed UTF-8 (no overlong form, no surrogate, nothing past U+10FFFF) and no NUL or other
 * control byte in the text. Anything else is refused whole.
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_CHATWIRE_H
#define COOP_COMMON_CHATWIRE_H

#include <cstddef>
#include <string>
#include <vector>

namespace chatwire {

enum Kind { kEveryone = 0, kFaction = 1, kPrivate = 2 };
enum Dir { kIn = 0, kOut = 1 };                       /* a private line: received ("From Bob") or sent ("To Bob") */
enum LineKind { kLineTalk = 0, kLineJoin = 1, kLineLeave = 2, kLineGone = 3, kLineNotConnected = 4 };
/* the TO choice: everyone, my faction, or a player's slot (0 and up) */
const int kToEveryone = -1;
const int kToFaction = -2;

const unsigned char kVersion = 1;
const size_t kMaxChars = 200;        /* the text line takes up to 200 characters (mock-up screen 2) */
const size_t kMaxBytes = 800;        /* 200 characters of at most 4 UTF-8 bytes each */
const size_t kHeader = 6;
const unsigned kNoTarget = 0xFFFF;   /* the target of an everyone or faction line */
const size_t kKeepLines = 100;       /* the window keeps the last 100 lines */
const long kFadeMs = 10000;          /* a line shows 10 seconds in the closed feed ... */
const long kFadeOutMs = 1000;        /* ... then fades out over one more second */

/* ---- the words (mock-up, every one approved) ---- */
inline const char* WindowTitle()   { return "CHAT"; }
inline const char* ToLabel()       { return "TO"; }
inline const char* EveryoneWord()  { return "EVERYONE"; }
inline const char* FactionWord()   { return "MY FACTION"; }
inline const char* SendWord()      { return "SEND"; }
inline const char* OpacityWord()   { return "OPACITY"; }
inline const char* TypeHint()      { return "type a message..."; }
inline const char* SearchHint()    { return "search players..."; }
inline const char* MessageWord()   { return "MESSAGE"; }
inline const char* SeparatorWord() { return "----------"; }

inline std::string JoinLine(const std::string& name)  { return name + " joined the world."; }
inline std::string LeaveLine(const std::string& name) { return name + " left the world."; }
inline std::string GoneLine(const std::string& name)  { return name + " isn't in the world any more."; }
inline std::string NotConnectedLine()                 { return "Not connected - your message wasn't sent."; }

/* A talk line in three parts - the words before the name, the name (its own clickable label), and the rest - so the window can
   make the name a label of its own. LineText is the three joined. */
inline void LineParts(int kind, int dir, const std::string& name, const std::string& text,
                      std::string* pre, std::string* nm, std::string* post)
{
    *nm = name;
    pre->clear();
    if (kind == kFaction) *post = " (faction): " + text;
    else if (kind == kPrivate) { *pre = dir == kOut ? "To " : "From "; *post = ": " + text; }
    else *post = ": " + text;
}
inline std::string LineText(int kind, int dir, const std::string& name, const std::string& text)
{
    std::string a, b, c;
    LineParts(kind, dir, name, text, &a, &b, &c);
    return a + b + c;
}

/* ---- the colours: everyone white, faction green, private pink, join / leave and errors grey ---- */
struct Rgb { float r, g, b; };
inline Rgb MakeRgb(float r, float g, float b) { Rgb c; c.r = r; c.g = g; c.b = b; return c; }
enum Colour { kWhite = 0, kGreen = 1, kPink = 2, kGrey = 3 };
inline int LineColour(int lineKind, int kind)
{
    if (lineKind != kLineTalk) return kGrey;
    if (kind == kFaction) return kGreen;
    if (kind == kPrivate) return kPink;
    return kWhite;
}
inline Rgb ColourRgb(int colour)
{
    switch (colour)
    {
    case kGreen: return MakeRgb(0.56f, 0.86f, 0.46f);
    case kPink:  return MakeRgb(0.96f, 0.62f, 0.82f);
    case kGrey:  return MakeRgb(0.66f, 0.66f, 0.66f);
    default:     return MakeRgb(1.0f, 1.0f, 1.0f);
    }
}

/* ---- UTF-8 ---- */
inline bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }
inline size_t Utf8Chars(const std::string& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) if (!IsCont((unsigned char)s[i])) ++n;
    return n;
}
inline bool IsControl(unsigned char c) { return c < 0x20 || c == 0x7F; }
/* the length of the well-formed UTF-8 character starting at b[i] (1 to 4), 0 = none starts there: a continuation byte, a lead
   byte no character has (C0, C1, F5..FF), a missing continuation byte, an overlong form, a surrogate (U+D800..U+DFFF) or a
   character past U+10FFFF */
const unsigned int kMaxCodePoint = 1114111u;   /* U+10FFFF, the highest Unicode code point */
inline size_t Utf8SeqLen(const unsigned char* b, size_t n, size_t i)
{
    const unsigned char c = b[i];
    if (c < 0x80) return 1;
    size_t len = 0;
    unsigned long cp = 0;
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
    else return 0;
    if (n - i < len) return 0;
    for (size_t k = 1; k < len; ++k)
    {
        if (!IsCont(b[i + k])) return 0;
        cp = (cp << 6) | (unsigned long)(b[i + k] & 0x3F);
    }
    if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
    if (len == 4 && (cp < 0x10000 || cp > kMaxCodePoint)) return 0;
    return len;
}
/* every byte is part of a well-formed UTF-8 character */
inline bool Utf8Valid(const char* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; )
    {
        const size_t len = Utf8SeqLen(b, n, i);
        if (len == 0) return false;
        i += len;
    }
    return true;
}

/* What the player typed, made ready to send: control characters and every byte that is not part of a well-formed UTF-8
   character dropped, spaces trimmed from both ends, cut to kMaxChars characters (never in the middle of a character).
   "" = nothing to send. */
inline std::string CleanTyped(const std::string& typed)
{
    std::string s;
    s.reserve(typed.size());
    const unsigned char* t = (const unsigned char*)typed.data();
    for (size_t i = 0; i < typed.size(); )
    {
        const size_t len = Utf8SeqLen(t, typed.size(), i);
        if (len == 0) { ++i; continue; }
        if (!(len == 1 && IsControl(t[i]))) s.append(typed, i, len);
        i += len;
    }
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') ++a;
    while (b > a && s[b - 1] == ' ') --b;
    s = s.substr(a, b - a);
    size_t chars = 0, i = 0;
    for (; i < s.size(); ++i)
    {
        if (IsCont((unsigned char)s[i])) continue;
        if (chars == kMaxChars) break;
        ++chars;
    }
    s.erase(i);
    while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
    return s;
}

/* ---- the bytes ---- */
/* kind, target (the receiving player's slot, 0..0xFFFE, for a private line; an everyone or faction line carries kNoTarget
   whatever is passed) and the text */
inline bool Encode(int kind, int target, const std::string& text, std::vector<char>* out)
{
    out->clear();
    if (kind < kEveryone || kind > kPrivate || text.empty() || text.size() > kMaxBytes) return false;
    if (kind == kPrivate && (target < 0 || target >= (int)kNoTarget)) return false;
    for (size_t i = 0; i < text.size(); ++i) if (IsControl((unsigned char)text[i])) return false;
    if (!Utf8Valid(text.data(), text.size())) return false;
    const unsigned to = kind == kPrivate ? (unsigned)target : kNoTarget;
    out->push_back((char)kVersion);
    out->push_back((char)(unsigned char)kind);
    out->push_back((char)(unsigned char)(to & 0xFF));
    out->push_back((char)(unsigned char)((to >> 8) & 0xFF));
    out->push_back((char)(unsigned char)(text.size() & 0xFF));
    out->push_back((char)(unsigned char)((text.size() >> 8) & 0xFF));
    out->insert(out->end(), text.begin(), text.end());
    return true;
}
/* *target = the slot a private line names, -1 for an everyone or faction line */
inline bool Decode(const void* p, size_t len, int* kind, int* target, std::string* text)
{
    text->clear();
    const unsigned char* b = (const unsigned char*)p;
    if (b == 0 || len < kHeader + 1) return false;
    if (b[0] != kVersion || b[1] > (unsigned char)kPrivate) return false;
    const unsigned to = (unsigned)b[2] | ((unsigned)b[3] << 8);
    if ((b[1] == (unsigned char)kPrivate) != (to != kNoTarget)) return false;   /* a private line names its player; the others name none */
    const size_t n = (size_t)b[4] | ((size_t)b[5] << 8);
    if (n == 0 || n > kMaxBytes || len != kHeader + n) return false;
    for (size_t i = 0; i < n; ++i) if (IsControl(b[kHeader + i])) return false;
    if (!Utf8Valid((const char*)b + kHeader, n)) return false;
    *kind = (int)b[1];
    *target = b[1] == (unsigned char)kPrivate ? (int)to : -1;
    text->assign((const char*)b + kHeader, n);
    return true;
}

/* ---- what a received line may do ---- */
enum InVerdict { kInTake = 0, kInNotMate = 1, kInNotMine = 2 };
/* the kind is the sender's word: a faction line is kept only from a member of this player's faction, a private line only when
   it names this game's slot */
inline int AcceptIn(int kind, int target, int mySlot, bool senderIsMate)
{
    if (kind == kFaction) return senderIsMate ? kInTake : kInNotMate;
    if (kind == kPrivate) return (mySlot >= 0 && target == mySlot) ? kInTake : kInNotMine;
    return kInTake;
}
/* at most kInPerSecond lines in any one-second window from one sender: true = this line is kept */
const int kInPerSecond = 5;
struct InRate { long long winMs; int n; InRate() : winMs(0), n(0) {} };
inline bool RateTake(InRate* r, long long now)
{
    if (r->n == 0 || now < r->winMs || now - r->winMs >= 1000) { r->winMs = now; r->n = 0; }
    if (r->n >= kInPerSecond) return false;
    ++r->n;
    return true;
}

/* ---- picking who to write to ---- */
/* Tab while typing: EVERYONE -> MY FACTION -> the last player messaged (when there is one) -> EVERYONE */
inline int TabNext(int cur, int lastSlot)
{
    if (cur == kToEveryone) return kToFaction;
    if (cur == kToFaction) return lastSlot >= 0 ? lastSlot : kToEveryone;
    return kToEveryone;
}
inline char LowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
/* the TO list's search line: a case-insensitive part of the name (an empty search matches every name) */
inline bool SearchMatch(const std::string& name, const std::string& search)
{
    if (search.empty()) return true;
    if (search.size() > name.size()) return false;
    for (size_t i = 0; i + search.size() <= name.size(); ++i)
    {
        size_t k = 0;
        while (k < search.size() && LowerAscii(name[i + k]) == LowerAscii(search[k])) ++k;
        if (k == search.size()) return true;
    }
    return false;
}

/* ---- the kept lines ---- */
struct Entry
{
    int lineKind;     /* LineKind */
    int kind;         /* Kind (talk lines) */
    int dir;          /* Dir (private lines) */
    int slot;         /* the player the line names (-1 none); its name is clickable when it is another player */
    std::string name; /* the name as shown */
    std::string text; /* talk: the message; other lines: the whole sentence */
    long long ms;     /* when it arrived (the process's millisecond clock) */
    long long seq;    /* 1, 2, 3 ... in arrival order */
    Entry() : lineKind(kLineTalk), kind(kEveryone), dir(kIn), slot(-1), ms(0), seq(0) {}
};
/* the last kKeepLines lines, oldest first */
struct Ring
{
    std::vector<Entry> lines;
    long long nextSeq;
    Ring() : nextSeq(1) {}
    void Push(const Entry& e)
    {
        Entry c = e;
        c.seq = nextSeq++;
        lines.push_back(c);
        while (lines.size() > kKeepLines) lines.erase(lines.begin());
    }
    void Clear() { lines.clear(); }
};
/* the whole line as one sentence (talk lines through LineText) */
inline std::string EntryText(const Entry& e)
{
    if (e.lineKind == kLineTalk) return LineText(e.kind, e.dir, e.name, e.text);
    return e.text;
}

/* a closed feed's line: fully shown for kFadeMs, then fading to nothing over kFadeOutMs */
inline float FadeAlpha(long long ageMs)
{
    if (ageMs < 0) return 1.0f;
    if (ageMs <= kFadeMs) return 1.0f;
    if (ageMs >= kFadeMs + kFadeOutMs) return 0.0f;
    return 1.0f - (float)(ageMs - kFadeMs) / (float)kFadeOutMs;
}

/* ---- which road a message takes ---- */
enum Plan { kPlanSend = 0, kPlanSelfOnly = 1, kPlanGone = 2, kPlanNotConnected = 3 };
struct Route { int route; int target; };   /* cooplive::kRouteWorld (1) target 0, or kRouteSlot (2) target = slot */
const int kRouteWorldNo = 1, kRouteSlotNo = 2;  /* liveenvelope.h's kRouteWorld / kRouteSlot, as this header does not include it */
/* kind and target from the TO choice; mates = how many other members this player's faction has on this game's table (in the
   world or not); matesIn = those of them in the world now; targetInWorld = the chosen player is in the world now; listKnown =
   this game has the world's player list on its current link (it has none for a moment after the link comes back);
   liveReady = this game's link to the world server is up. Every message needs the link; a faction or private one also needs
   the list (for a faction message that includes this game's faction table, which is forgotten while the link is down) -
   without them it is "not connected", never "gone". A faction with no other member on a known table only shows in this
   player's own chat. */
inline int SendPlan(int kind, int target, int mates, const std::vector<int>& matesIn, bool targetInWorld, bool listKnown,
                    bool liveReady, std::vector<Route>* out)
{
    out->clear();
    if (!liveReady) return kPlanNotConnected;
    if (kind == kFaction)
    {
        if (!listKnown) return kPlanNotConnected;
        if (mates <= 0 || matesIn.empty()) return kPlanSelfOnly;
        for (size_t i = 0; i < matesIn.size(); ++i) { Route r; r.route = kRouteSlotNo; r.target = matesIn[i]; out->push_back(r); }
        return kPlanSend;
    }
    if (kind == kPrivate)
    {
        if (!listKnown) return kPlanNotConnected;
        if (target < 0 || !targetInWorld) return kPlanGone;
        Route r; r.route = kRouteSlotNo; r.target = target; out->push_back(r);
        return kPlanSend;
    }
    Route r; r.route = kRouteWorldNo; r.target = 0; out->push_back(r);
    return kPlanSend;
}

/* ---- chat.cfg: the window's place, size and opacity ---- */
struct Cfg { int x, y, w, h, opacity; Cfg() : x(0), y(0), w(0), h(0), opacity(60) {} };
const int kMinW = 320, kMinH = 180;
const int kDefaultOpacity = 60;
inline std::string IntText(int v)
{
    char b[16]; int i = 15; b[i] = 0;
    const bool neg = v < 0;
    unsigned u = neg ? (unsigned)(-(long long)v) : (unsigned)v;
    do { b[--i] = (char)('0' + u % 10); u /= 10; } while (u != 0 && i > 1);
    if (neg) b[--i] = '-';
    return std::string(b + i);
}
inline std::string CfgFormat(const Cfg& c)
{
    return "x=" + IntText(c.x) + "\r\ny=" + IntText(c.y) + "\r\nw=" + IntText(c.w) + "\r\nh=" + IntText(c.h)
         + "\r\nopacity=" + IntText(c.opacity) + "\r\n";
}
inline bool ParseInt(const std::string& s, int* v)
{
    if (s.empty() || s.size() > 7) return false;
    size_t i = 0; bool neg = false;
    if (s[0] == '-') { neg = true; i = 1; if (s.size() == 1) return false; }
    int n = 0;
    for (; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; n = n * 10 + (s[i] - '0'); }
    *v = neg ? -n : n;
    return true;
}
/* true = x, y, w and h were all read (opacity is optional, 0..100, else the default) */
inline bool CfgParse(const std::string& text, Cfg* out)
{
    Cfg c;
    int have = 0;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == ' ')) line.erase(line.size() - 1);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        int v = 0;
        if (!ParseInt(line.substr(eq + 1), &v)) continue;
        if (k == "x") { c.x = v; have |= 1; }
        else if (k == "y") { c.y = v; have |= 2; }
        else if (k == "w") { c.w = v; have |= 4; }
        else if (k == "h") { c.h = v; have |= 8; }
        else if (k == "opacity" && v >= 0 && v <= 100) c.opacity = v;
    }
    if (have != 15) return false;
    *out = c;
    return true;
}
/* The window on screen at any resolution: no smaller than kMinW x kMinH, no larger than the screen, wholly inside it. */
inline Cfg FitRect(const Cfg& in, int vw, int vh)
{
    Cfg c = in;
    if (vw < kMinW) vw = kMinW;
    if (vh < kMinH) vh = kMinH;
    if (c.w < kMinW) c.w = kMinW;
    if (c.h < kMinH) c.h = kMinH;
    if (c.w > vw) c.w = vw;
    if (c.h > vh) c.h = vh;
    if (c.x > vw - c.w) c.x = vw - c.w;
    if (c.y > vh - c.h) c.y = vh - c.h;
    if (c.x < 0) c.x = 0;
    if (c.y < 0) c.y = 0;
    if (c.opacity < 0) c.opacity = 0;
    if (c.opacity > 100) c.opacity = 100;
    return c;
}
/* the first place: the left side, just above the squad portraits (mock-up screen 1) */
inline Cfg DefaultRect(int vw, int vh)
{
    Cfg c;
    c.w = vw * 30 / 100; if (c.w < 360) c.w = 360; if (c.w > 640) c.w = 640;
    c.h = vh * 30 / 100; if (c.h < 220) c.h = 220; if (c.h > 420) c.h = 420;
    c.x = 10;
    c.y = vh - c.h - vh * 22 / 100;
    c.opacity = kDefaultOpacity;
    return FitRect(c, vw, vh);
}

/* ---- the window's layout: every control's place from the window's size and the measured widths of its words ----
   The header: CHAT at the left, then OPACITY and its slider, then the close X where Kenshi's header has it. Below the lines, two
   rows of controls: TO and its button, then the typing box and SEND at the bottom. The resize grip holds the bottom-right corner,
   so SEND ends clear of it. */
const int kWinHeadH = 38, kWinPad = 8, kWinCtlH = 30, kWinGap = 6;
const int kGripSize = 18;
const int kSliderLeast = 80;
const float kLineBandAlpha = 0.45f;   /* the dark band behind each row of a chat line, at the line's full alpha: black at 45% */
struct WinBox { int x, y, w, h; };
inline void SetWinBox(WinBox* b, int x, int y, int w, int h) { b->x = x; b->y = y; b->w = w; b->h = h; }
struct WinLayout { WinBox head, title, opWord, opBar, close, view, toWord, toBtn, edit, hint, send, grip; };
/* titleW, opWordW, toCapW, sendWordW: the measured widths of CHAT, OPACITY, the TO button's caption and SEND.
   The layout is written through out, not returned by value: VS2010's x64 optimiser stores a returned struct of 16 bytes or more
   with aligned SSE moves (movaps), taking the caller's return slot to be 16-byte aligned while the caller may place it at an
   8-byte boundary, and the store then faults. A plain pointer carries no such assumption, so every box is stored field by field. */
inline void WindowLayout(int W, int H, int titleW, int opWordW, int toCapW, int sendWordW, WinLayout* out)
{
    WinLayout& L = *out;
    SetWinBox(&L.head, 0, 0, W, kWinHeadH);
    SetWinBox(&L.close, W - 40, 5, 31, 32);   /* Kenshi_WindowCX's close X: 31 x 32 at 5 from the top */
    /* the header's gaps: roomy, or tight when the roomy ones would leave the slider under its least width; the X never moves */
    static const int kGaps[2][4] = { { 10, 14, 6, 8 }, { 4, 6, 4, 4 } };   /* title's margin, title-OPACITY, OPACITY-slider, slider-X */
    for (int k = 0; k < 2; ++k)
    {
        SetWinBox(&L.title, kGaps[k][0], 3, titleW + 12, kWinHeadH - 4);
        SetWinBox(&L.opWord, L.title.x + L.title.w + kGaps[k][1], (kWinHeadH - kWinCtlH) / 2, opWordW + 10, kWinCtlH);
        const int barX = L.opWord.x + L.opWord.w + kGaps[k][2];
        SetWinBox(&L.opBar, barX, (kWinHeadH - 22) / 2, L.close.x - kGaps[k][3] - barX, 22);
        if (L.opBar.w >= kSliderLeast) break;
    }
    if (L.opBar.w < 1) L.opBar.w = 1;
    const int row2 = H - kWinPad - kWinCtlH;
    const int row1 = row2 - kWinGap - kWinCtlH;
    int sendW = sendWordW + 32;
    if (sendW < 64) sendW = 64;
    SetWinBox(&L.send, W - kGripSize - 2 - sendW, row2, sendW, kWinCtlH);
    SetWinBox(&L.edit, kWinPad, row2, L.send.x - kWinGap - kWinPad, kWinCtlH);
    SetWinBox(&L.hint, L.edit.x + 8, row2, L.edit.w - 16, kWinCtlH);
    SetWinBox(&L.toWord, kWinPad, row1, 34, kWinCtlH);
    int toW = toCapW + 40;   /* the caption ends in "  v": room for it and the button's edges */
    const int toMost = W / 2 - kWinPad;
    if (toW > toMost) toW = toMost;
    if (toW < 90) toW = 90;
    SetWinBox(&L.toBtn, L.toWord.x + L.toWord.w, row1, toW, kWinCtlH);
    const int viewTop = kWinHeadH + 4;
    SetWinBox(&L.view, kWinPad, viewTop, W - 2 * kWinPad, row1 - kWinGap - viewTop);
    SetWinBox(&L.grip, W - kGripSize, H - kGripSize, kGripSize, kGripSize);
}
inline bool WinBoxesOverlap(const WinBox& a, const WinBox& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

/* ---- the test lever's fixed sentences (the lever never takes free text: every command is logged) ---- */
const int kSampleCount = 4;
inline const char* Sample(int i)
{
    static const char* const kSample[kSampleCount] = {
        "heading to the Hub, need anything?",
        "bring bread",
        "can you hold the gate?",
        "on my way"
    };
    return (i >= 1 && i <= kSampleCount) ? kSample[i - 1] : "";
}

}   /* namespace chatwire */

#endif
