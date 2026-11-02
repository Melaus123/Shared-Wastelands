/* src/common/tagstack.h - THE NAME TAG OVER ANOTHER PLAYER'S CHARACTERS: the character's own name on line 2, and the tags of one
 * player's characters standing together.
 *
 * WHAT LINE 2 SAYS.  Line 1 is still the owner's player name (src/common/nametag.h CaptionFor). Line 2 is the CHARACTER's own
 * name (the name its owner gave it, synced by names1 - src/common/namewire.h), then the faction (or the team) after a separator:
 * "Rex - Tech Hunters". The separator is " - ": the tag's font (Kenshi_StandardFont_Large, data/gui/fonts/kenshi_fonts.xml) holds
 * only the codes 32-126 and 8127-8217, so a middle dot (183) would not draw. No character name: the faction alone, as before; no
 * faction name: the character's name alone. No roster name for the player: line 1 is that whole line and there is no line 2.
 *
 * STANDING TOGETHER.  Every tag of one player (one owner slot) whose block, as it would be drawn alone, overlaps another such tag's
 * block on screen (within kTouchPx) is in one GROUP with it; the groups are closed over chains (A over B, B over C: one group).
 * In a group of two or more, ONE tag keeps line 1 - the squad leader (its squad's formal leader is that character); with no leader
 * in the group, or several, the lowest uid among them. Every other tag of the group shows line 2 alone. Then, so the line-2-only
 * tags do not sit on each other either, the group is laid out from the bottom up: the head stays where it is, and each other tag
 * (lowest on screen first, then by uid) is moved up just above whichever already-placed tag of its group it would overlap, until
 * it overlaps none. A tag on its own, or of another owner, is never touched. A tag whose line 2 is empty keeps its line 1 (it is
 * the only line it has).
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

#include "nametag.h"

#include <string>
#include <vector>
#include <algorithm>

namespace tagstack {

const char* const kSep = " - ";   /* the font has no middle dot (see above) */
const int kTouchPx = 2;           /* blocks closer than this count as overlapping: an outline touching an outline smears too */

/* Line 2: the character's own name, then the faction. */
inline std::string CharLine(const std::string& charName, const std::string& factionName)
{
    const std::string c = nametag::Trimmed(charName), f = nametag::Trimmed(factionName);
    if (c.empty()) return f;
    if (f.empty()) return c;
    return c + kSep + f;
}

/* Both lines for another player's character. */
inline nametag::Caption CaptionFor(const std::string& playerName, const std::string& charName, const std::string& factionName)
{
    return nametag::CaptionFor(playerName, CharLine(charName, factionName));
}

/* The tag with each part switched on or off (tags.cpp TagPartsSet): a part switched off is left out, as if it had no name. With
   the player's name off, line 2's words take line 1 and there is no line 2. All three off: both lines empty (the tag is hidden). */
inline nametag::Caption CaptionParts(const std::string& playerName, const std::string& charName, const std::string& factionName,
                                     bool showPlayer, bool showChar, bool showFaction)
{
    return CaptionFor(showPlayer ? playerName : std::string(), showChar ? charName : std::string(),
                      showFaction ? factionName : std::string());
}

/* One tag as drawn alone this frame: centred on cx, its block's bottom at `bottom` (screen px, +y down), line 1 (w1 x h1) over
   line 2 (w2 x h2; 0 x 0 when empty). owner: the owner's slot (< 0 = unknown - never grouped). */
struct Tag
{
    unsigned int uid;
    int owner;
    bool leader;
    int cx, bottom;
    int w1, h1, w2, h2;
};
/* What the tick does with it: line 1 shown or not, and the block moved by dy (<= 0: up). */
struct Place
{
    bool line1;
    int dy;
    int group;   /* index of the group's head in the input; -1 = alone */
};

struct Box { int l, t, r, b; };
inline bool Overlap(const Box& a, const Box& b, int touch)
{
    return a.l < b.r + touch && b.l < a.r + touch && a.t < b.b + touch && b.t < a.b + touch;
}
/* The block a tag covers: both lines, or line 2 alone. */
inline Box BlockOf(const Tag& t, bool line1, int dy)
{
    const bool one = line1 || t.h2 <= 0;
    const bool two = t.h2 > 0;
    int w = 0, h = 0;
    if (one) { w = t.w1; h += t.h1; }
    if (two) { if (t.w2 > w) w = t.w2; h += t.h2; }
    Box b; b.l = t.cx - w / 2; b.r = b.l + w; b.b = t.bottom + dy; b.t = b.b - h;
    return b;
}

namespace detail {
inline int Root(std::vector<int>& p, int i) { while (p[(size_t)i] != i) { p[(size_t)i] = p[(size_t)p[(size_t)i]]; i = p[(size_t)i]; } return i; }
struct LowerFirst   /* lowest on screen first (the larger bottom), then the lower uid */
{
    const std::vector<Tag>* in;
    bool operator()(int a, int b) const
    {
        const Tag& x = (*in)[(size_t)a]; const Tag& y = (*in)[(size_t)b];
        if (x.bottom != y.bottom) return x.bottom > y.bottom;
        return x.uid < y.uid;
    }
};
}

namespace detail {
/* p[i] leads (Root) to i's group: tags of one known owner whose blocks touch, closed over chains */
inline void Unite(const std::vector<Tag>& in, std::vector<int>* pp)
{
    std::vector<int>& p = *pp;
    const int n = (int)in.size();
    p.assign((size_t)n, 0);
    for (int i = 0; i < n; ++i) p[(size_t)i] = i;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            if (in[(size_t)i].owner < 0 || in[(size_t)i].owner != in[(size_t)j].owner) continue;
            if (!Overlap(BlockOf(in[(size_t)i], true, 0), BlockOf(in[(size_t)j], true, 0), kTouchPx)) continue;
            const int a = Root(p, i), b = Root(p, j);
            if (a != b) p[(size_t)b] = a;
        }
}
}
/* out[i] = 1 when tag i stands in a group of two or more (Plan's grouping: the tags it picks one head among), else 0 */
inline void InGroup(const std::vector<Tag>& in, std::vector<char>* out)
{
    const int n = (int)in.size();
    out->assign((size_t)n, 0);
    if (n < 2) return;
    std::vector<int> p;
    detail::Unite(in, &p);
    std::vector<int> size((size_t)n, 0);
    for (int i = 0; i < n; ++i) ++size[(size_t)detail::Root(p, i)];
    for (int i = 0; i < n; ++i) (*out)[(size_t)i] = size[(size_t)detail::Root(p, i)] >= 2 ? 1 : 0;
}

inline void Plan(const std::vector<Tag>& in, std::vector<Place>* out)
{
    const int n = (int)in.size();
    out->assign((size_t)n, Place());
    for (int i = 0; i < n; ++i) { (*out)[(size_t)i].line1 = true; (*out)[(size_t)i].dy = 0; (*out)[(size_t)i].group = -1; }
    if (n < 2) return;
    std::vector<int> p;   /* the groups (InGroup's own) */
    detail::Unite(in, &p);
    for (int r = 0; r < n; ++r)
    {
        if (detail::Root(p, r) != r) continue;
        std::vector<int> m;
        for (int i = 0; i < n; ++i) if (detail::Root(p, i) == r) m.push_back(i);
        if (m.size() < 2) continue;
        /* the head: a squad leader, lowest uid first; else the lowest uid */
        int head = -1;
        for (size_t k = 0; k < m.size(); ++k)
        {
            const Tag& t = in[(size_t)m[k]];
            if (head < 0) { head = m[k]; continue; }
            const Tag& h = in[(size_t)head];
            if ((t.leader && !h.leader) || (t.leader == h.leader && t.uid < h.uid)) head = m[k];
        }
        std::vector<int> rest;
        for (size_t k = 0; k < m.size(); ++k)
        {
            (*out)[(size_t)m[k]].group = head;
            if (m[k] == head) continue;
            if (in[(size_t)m[k]].h2 > 0) (*out)[(size_t)m[k]].line1 = false;
            rest.push_back(m[k]);
        }
        detail::LowerFirst lf; lf.in = &in;
        std::sort(rest.begin(), rest.end(), lf);
        std::vector<Box> placed(1, BlockOf(in[(size_t)head], true, 0));
        for (size_t k = 0; k < rest.size(); ++k)
        {
            const int i = rest[k];
            Place& pl = (*out)[(size_t)i];
            /* each move lands just above one placed block, so it moves at most once per placed block */
            for (size_t guard = 0; guard <= placed.size(); ++guard)
            {
                const Box me = BlockOf(in[(size_t)i], pl.line1, pl.dy);
                int hit = -1;
                for (size_t q = 0; q < placed.size(); ++q)
                    if (Overlap(me, placed[q], 0) && (hit < 0 || placed[q].t < placed[(size_t)hit].t)) hit = (int)q;
                if (hit < 0) break;
                pl.dy += placed[(size_t)hit].t - me.b;
            }
            placed.push_back(BlockOf(in[(size_t)i], pl.line1, pl.dy));
        }
    }
}

}   /* namespace tagstack */
