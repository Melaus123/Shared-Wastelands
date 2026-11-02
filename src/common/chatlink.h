/* src/common/chatlink.h - ITEM LINKS IN THE CHAT: the pure half. With the chat's typing line open, Shift + middle-click on an item in
 * any inventory window puts a link to it in the line, and every player sees it in the item's own colour. src/coop-plugin/chat.cpp
 * reads the mouse and draws the lines; src/coop-plugin/items.cpp (ItemLinkUnderMouse) finds the item under the mouse. This file is
 * the words, the colours and the bytes.
 *
 * THE BYTES. A link rides INSIDE the chat message's text (chatwire.h, unchanged; no new message, no new field, no protocol change):
 *     [<shown>]{L:<sid>:<level>:<kind>}
 *   shown  the words between the brackets, e.g. "Ninja Blade - Edge Type 1 - Specialist" (1..kMaxShown bytes of UTF-8, no
 *          control byte, no '[' ']' '{' '}'; broken UTF-8 is not a link)
 *   sid    the item's GameData string id, e.g. "924-gamedata.base" (0..kMaxSid printable ASCII bytes, no ':' '{' '}' '[' ']')
 *   level  the item's level, 0..999 in decimal (InventoryItemBase::getLevel)
 *   kind   's' a plain item (GameData type ITEM, not an artifact), 'a' an artifact item, 'g' graded by its level (everything else)
 * A game with this file draws "[<shown>]" in the item's colour; a game without it shows the whole run as plain text, which still
 * starts with "[<shown>]". Anything that does not parse exactly is plain text: a typed '[' is never a link.
 *
 * THE COLOURS are the game's own, so a link looks like the item's name in the game's inventory. The rule is the game's item-colour
 * function (1.0.68 0x1625C0): a type-ITEM item is ItemStandard, or ItemArtifact when its artifact flag (+0x180) is set; any other
 * item is ItemGrade0..5 by level, <=15 / <=35 / <=50 / <=65 / <=80 / above (the same thresholds at 0x162350). The values are
 * data/gui/colours/kenshi_colours.xml's; ItemGrade5 is not in that file and keeps the executable's own default (0.99, 0.92, 0.05).
 * THE QUALITY WORD is the game's own too (1.0.68 0x796300): level <=10 Prototype, <=20 Shoddy, <=40 Standard, <=60 High, <=80
 * Specialist, above Masterwork.
 *
 * v2 (not built): clicking a link opens the item's stat panel. The sid, level and kind are carried for it; chat.cpp marks each link
 * label (ChatLinkLabelMade) so the click can be wired there.
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef SW_COMMON_CHATLINK_H
#define SW_COMMON_CHATLINK_H

#include <cstddef>
#include <string>
#include <vector>
#include "chatwire.h"

namespace chatlink {

const size_t kMaxShown = 96;          /* bytes between the brackets */
const size_t kMaxSid = 96;            /* bytes of the string id */
const int kMaxLevel = 999;
const size_t kMaxLinksInLine = 16;    /* the links one line holds: a typing line takes no more (chat.cpp); a received line's first 16 are
                                         read as links and anything after them is plain text (Split) */
const char kKindStandard = 's';
const char kKindArtifact = 'a';
const char kKindGraded = 'g';
/* the game's GameData types this file names (gamedata.base: Ninja Blade 2, Leather Shirt 3, Bread 4) */
const int kTypeWeapon = 2;
const int kTypeArmour = 3;
const int kTypeItem = 4;

struct Link
{
    std::string shown;   /* the words between the brackets */
    std::string sid;
    int level;
    char kind;
    Link() : level(0), kind(kKindGraded) {}
};

/* ---- the words ---- */
inline const char* QualityWord(int level)
{
    if (level <= 10) return "Prototype";
    if (level <= 20) return "Shoddy";
    if (level <= 40) return "Standard";
    if (level <= 60) return "High";
    if (level <= 80) return "Specialist";
    return "Masterwork";
}
/* the quality word is shown for weapons and armour, never for a plain item (food, books, parts), and for anything else only when
   it has a level */
inline bool ShowQuality(int type, int level)
{
    if (type == kTypeWeapon || type == kTypeArmour) return true;
    if (type == kTypeItem) return false;
    return level > 0;
}
/* a weapon's model is its material record's name ("Edge Type 1" is a weapon material record in gamedata.base) */
inline bool ShowModel(int type) { return type == kTypeWeapon; }
inline char KindOf(int type, bool artifact)
{
    if (type == kTypeItem) return artifact ? kKindArtifact : kKindStandard;
    return kKindGraded;
}

/* a name part made safe to stand between the brackets: control bytes and broken UTF-8 dropped, '[' '{' read as '(' and ']' '}' as
   ')', spaces trimmed, cut to `maxBytes` (never inside a character) */
inline std::string CleanPart(const std::string& s, size_t maxBytes)
{
    std::string o;
    const unsigned char* b = (const unsigned char*)s.data();
    for (size_t i = 0; i < s.size(); )
    {
        const size_t len = chatwire::Utf8SeqLen(b, s.size(), i);
        if (len == 0) { ++i; continue; }
        if (len == 1)
        {
            char c = s[i];
            if (chatwire::IsControl((unsigned char)c)) { ++i; continue; }
            if (c == '[' || c == '{') c = '(';
            else if (c == ']' || c == '}') c = ')';
            if (o.size() + 1 > maxBytes) break;
            o += c;
        }
        else
        {
            if (o.size() + len > maxBytes) break;
            o.append(s, i, len);
        }
        i += len;
    }
    size_t a = 0, e = o.size();
    while (a < e && o[a] == ' ') ++a;
    while (e > a && o[e - 1] == ' ') --e;
    return o.substr(a, e - a);
}
inline bool SidChar(char c) { return c >= 0x20 && c <= 0x7E && c != ':' && c != '{' && c != '}' && c != '[' && c != ']'; }
/* a string id the link can carry, or "" when it cannot (too long, or a byte no id has) */
inline std::string CleanSid(const std::string& s)
{
    if (s.size() > kMaxSid) return std::string();
    for (size_t i = 0; i < s.size(); ++i) if (!SidChar(s[i])) return std::string();
    return s;
}
/* "Ninja Blade - Edge Type 1 - Specialist": the name, then the model and the quality word where they are shown; "" without a name */
inline std::string ShownOf(const std::string& name, const std::string& model, int type, int level)
{
    const std::string n = CleanPart(name, kMaxShown);
    if (n.empty()) return std::string();
    std::string s = n;
    const std::string m = ShowModel(type) ? CleanPart(model, kMaxShown) : std::string();
    if (!m.empty() && m != n) s += " - " + m;
    if (ShowQuality(type, level)) s += std::string(" - ") + QualityWord(level);
    return CleanPart(s, kMaxShown);
}
/* the whole link from what the item says; false when it has no name to show */
inline bool Make(const std::string& name, const std::string& model, const std::string& sid, int type, int level, bool artifact, Link* out)
{
    Link k;
    k.shown = ShownOf(name, model, type, level);
    if (k.shown.empty()) return false;
    k.sid = CleanSid(sid);
    k.level = level < 0 ? 0 : (level > kMaxLevel ? kMaxLevel : level);
    k.kind = KindOf(type, artifact);
    *out = k;
    return true;
}

/* ---- the colours (0xRRGGBB) ---- */
enum ColourIx { kColStandard = 0, kColArtifact = 1, kColGrade0 = 2, kColGrade1, kColGrade2, kColGrade3, kColGrade4, kColGrade5 };
inline int ColourIndex(char kind, int level)
{
    if (kind == kKindStandard) return kColStandard;
    if (kind == kKindArtifact) return kColArtifact;
    if (level <= 15) return kColGrade0;
    if (level <= 35) return kColGrade1;
    if (level <= 50) return kColGrade2;
    if (level <= 65) return kColGrade3;
    if (level <= 80) return kColGrade4;
    return kColGrade5;
}
/* r, g, b as one 0xRRGGBB number (written as three bytes so no colour reads as an engine address to tools/scan_hardcoded_rva.py) */
inline unsigned Pack(unsigned r, unsigned g, unsigned b) { return (r << 16) | (g << 8) | b; }
inline unsigned ColourRgb(int ix)
{
    switch (ix)
    {
    case kColStandard: return Pack(0xE0, 0xCC, 0xCC);   /* ItemStandard */
    case kColArtifact: return Pack(0x0D, 0xFC, 0xEB);   /* ItemArtifact */
    case kColGrade0:   return Pack(0x66, 0x66, 0x66);
    case kColGrade1:   return Pack(0xD9, 0xCC, 0xBF);
    case kColGrade2:   return Pack(0x40, 0x99, 0xB3);
    case kColGrade3:   return Pack(0x66, 0xFC, 0x80);
    case kColGrade4:   return Pack(0xFC, 0xEB, 0x0D);
    default:           return Pack(0xFC, 0xEB, 0x0D);   /* ItemGrade5: the executable's default, not in the colours file */
    }
}
inline unsigned LinkRgb(const Link& k) { return ColourRgb(ColourIndex(k.kind, k.level)); }
/* "#rrggbb", a MyGUI caption colour code */
inline std::string ColourCode(unsigned rgb)
{
    static const char* const hex = "0123456789abcdef";
    std::string o("#");
    for (int sh = 20; sh >= 0; sh -= 4) o += hex[(rgb >> sh) & 15];
    return o;
}

/* ---- the bytes ---- */
inline std::string Shown(const Link& k) { return "[" + k.shown + "]"; }
inline std::string IntText(int v) { return chatwire::IntText(v); }
inline std::string Encode(const Link& k)
{
    return Shown(k) + "{L:" + k.sid + ":" + IntText(k.level) + ":" + std::string(1, k.kind) + "}";
}
/* a link starting at text[at] ('['): *end = one past its '}'. false = not a link there */
inline bool ParseAt(const std::string& text, size_t at, Link* out, size_t* end)
{
    if (at >= text.size() || text[at] != '[') return false;
    const size_t j = text.find("]{L:", at + 1);
    if (j == std::string::npos || j == at + 1 || j - at - 1 > kMaxShown) return false;
    const unsigned char* b = (const unsigned char*)text.data();
    for (size_t i = at + 1; i < j; )
    {
        const size_t len = chatwire::Utf8SeqLen(b, j, i);   /* well-formed UTF-8 only, never running past the ']' */
        if (len == 0) return false;
        const char c = text[i];
        if (len == 1 && (c == '[' || c == ']' || c == '{' || c == '}' || chatwire::IsControl((unsigned char)c))) return false;
        i += len;
    }
    size_t k = j + 4;
    const size_t sidAt = k;
    while (k < text.size() && text[k] != ':')
    {
        if (!SidChar(text[k]) || k - sidAt >= kMaxSid) return false;
        ++k;
    }
    if (k >= text.size()) return false;
    const std::string sid = text.substr(sidAt, k - sidAt);
    ++k;
    int level = 0, digits = 0;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9')
    {
        if (++digits > 3) return false;
        level = level * 10 + (text[k] - '0');
        ++k;
    }
    if (digits == 0 || k + 2 >= text.size() || text[k] != ':') return false;   /* ':' kind '}' still to come */
    const char kind = text[k + 1];
    if (kind != kKindStandard && kind != kKindArtifact && kind != kKindGraded) return false;
    if (text[k + 2] != '}') return false;
    out->shown = text.substr(at + 1, j - at - 1);
    out->sid = sid;
    out->level = level;
    out->kind = kind;
    *end = k + 3;
    return true;
}
/* a message's text cut into plain runs and links, in order; only its first kMaxLinksInLine links are read, the rest is plain text */
struct Seg { bool link; std::string text; Link k; Seg() : link(false) {} };
inline std::vector<Seg> Split(const std::string& text)
{
    std::vector<Seg> out;
    std::string plain;
    size_t i = 0, links = 0;
    while (i < text.size())
    {
        if (links >= kMaxLinksInLine) { plain.append(text, i, std::string::npos); break; }   /* past the line's links: plain text */
        Link k;
        size_t end = 0;
        if (text[i] == '[' && ParseAt(text, i, &k, &end))
        {
            if (!plain.empty()) { Seg s; s.text = plain; out.push_back(s); plain.clear(); }
            Seg s; s.link = true; s.text = Shown(k); s.k = k; out.push_back(s);
            ++links;
            i = end;
            continue;
        }
        plain += text[i];
        ++i;
    }
    if (!plain.empty()) { Seg s; s.text = plain; out.push_back(s); }
    return out;
}
inline bool HasLink(const std::string& text)
{
    for (size_t i = text.find('['); i != std::string::npos; i = text.find('[', i + 1))
    {
        Link k; size_t end = 0;
        if (ParseAt(text, i, &k, &end)) return true;
    }
    return false;
}
/* the words a player reads: every link as "[<shown>]" */
inline std::string Plain(const std::string& text)
{
    const std::vector<Seg> segs = Split(text);
    std::string o;
    for (size_t i = 0; i < segs.size(); ++i) o += segs[i].text;
    return o;
}
/* What goes on the wire for what was typed: each link put in the typing line, the most recent first, takes the LAST "[<shown>]"
   of the typed text that is not already a link - so the same words typed by hand before the link was put in stay plain. A link
   that would take the text past `maxBytes` is left as its plain words. Words the player edited by hand never match a link. */
inline std::string Expand(const std::string& typed, const std::vector<Link>& pending, size_t maxBytes)
{
    std::string t = typed;
    for (size_t p = pending.size(); p-- > 0; )
    {
        const std::string want = Shown(pending[p]);
        const std::string tok = Encode(pending[p]);
        size_t from = std::string::npos;
        for (;;)
        {
            const size_t at = t.rfind(want, from);
            if (at == std::string::npos) break;
            Link k; size_t end = 0;
            if (ParseAt(t, at, &k, &end)) { if (at == 0) break; from = at - 1; continue; }   /* already a link: the one before it */
            if (t.size() - want.size() + tok.size() <= maxBytes) t.replace(at, want.size(), tok);
            break;
        }
    }
    return t;
}
/* The links put in the typing line whose words the line still holds, in the order they were put: for each "[<shown>]", as many
   links (the most recent kept) as the line has copies of those words. A link whose words were deleted or edited is dropped, so
   it neither counts against kMaxLinksInLine nor takes the same words typed later. */
inline std::vector<Link> Prune(const std::string& typed, const std::vector<Link>& pending)
{
    std::vector<char> keep(pending.size(), 0);
    for (size_t p = pending.size(); p-- > 0; )
    {
        const std::string want = Shown(pending[p]);
        size_t have = 0, taken = 0;
        for (size_t at = typed.find(want); at != std::string::npos; at = typed.find(want, at + 1)) ++have;
        for (size_t q = p + 1; q < pending.size(); ++q) if (keep[q] != 0 && pending[q].shown == pending[p].shown) ++taken;
        keep[p] = taken < have ? 1 : 0;
    }
    std::vector<Link> out;
    for (size_t p = 0; p < pending.size(); ++p) if (keep[p] != 0) out.push_back(pending[p]);
    return out;
}
/* what goes into the typing line: the shown words in the link's colour, then back to the line's own colour (`lineRgb`); '#'
   doubled so MyGUI never reads one as a colour code */
inline std::string Tagged(const Link& k, unsigned lineRgb)
{
    std::string w;
    const std::string s = Shown(k);
    for (size_t i = 0; i < s.size(); ++i) { w += s[i]; if (s[i] == '#') w += '#'; }
    return ColourCode(LinkRgb(k)) + w + ColourCode(lineRgb);
}
/* how many links a line's text carries as Split reads it (its first kMaxLinksInLine) and the bytes of its plain runs: chat.cpp's
   `[CHAT] in` line for each received line and the `chattest` lever report these */
inline void Count(const std::string& text, size_t* links, size_t* plainBytes)
{
    const std::vector<Seg> segs = Split(text);
    *links = 0; *plainBytes = 0;
    for (size_t i = 0; i < segs.size(); ++i) { if (segs[i].link) ++*links; else *plainBytes += segs[i].text.size(); }
}
/* TEST-ONLY (chat.cpp `chattest garbage`): one line with three broken link codes - a bad item code (kind 'x'), an unterminated
   code (no closing '}') and a code with no shown words ("[]") - none of which Split reads as a link */
inline std::string GarbageLine()
{
    return "bad [Bread]{L:924-gamedata.base:5:x} cut [Bread]{L:924-gamedata.base:5:s none []{L:924-gamedata.base:5:s}";
}

}   /* namespace chatlink */

#endif
