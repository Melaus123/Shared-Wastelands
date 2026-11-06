/* src/common/mapdots.h - T-930 (owner decision 1436, mock-up build/mockups-t930.md): TEAMMATES' CHARACTERS ON THE WORLD MAP.
 * The pure half: the message's bytes, the "moved enough to send" rule, who may ask and who may answer, and where on the map
 * canvas a world position lands. src/coop-plugin/mapdots.cpp feeds these and draws the dots.
 *
 * WHAT THE PLAYER SEES. While this player's world map is open, each character of each ONLINE teammate (a player who shares
 * this player's faction of players - the world server's team table) is a dot: the game's own "Character" map dot (resource
 * Kenshi_MapMarkers, the size of the player's own), in the name tags' team blue #5a9be6 (nametag.h TeamColour - owner rule
 * 2026-10-19: a teammate shows team blue; the player's own dots stay the game's light blue). Hovering a dot says the
 * character's name and, below it, "<player name>'s squad" (owner decision 1438: the player's name, never the squad's own
 * name, which is neither read nor sent - the viewer takes the player's name from the sender's slot). A dead character has no dot (knocked out keeps its dot); a teammate
 * who goes offline loses every dot; nobody else's characters are shown.
 *
 * THE ROAD. One inner type, cooplive::kInnerMapDots (liverelay.h), through the world server's LIVE relay, always route SLOT
 * (one player): the world server forwards any inner type and stamps the sender's slot (liveenvelope.h), so no world-server
 * message changes. Both ends check the team table: a game answers an OPEN only from a teammate in the world, and takes DOTS
 * only from a teammate it asked.
 *   OPEN   viewer -> each online teammate, when the viewer's map opens (and to a teammate who comes online while it is open)
 *   CLOSE  viewer -> each teammate it sent OPEN, when the map closes
 *   DOTS   teammate -> viewer: on OPEN its whole list once (whole = 1: the viewer drops this sender's dots not in it); then,
 *          checked about twice a second, only the characters that moved more than kMoveMinUnits since they were last sent to
 *          that viewer (or whose name changed), and the uids that are gone (dead, or left the player's squads).
 *
 * THE BYTES (little-endian):
 *     u8 version (1) | u8 kind (0 OPEN, 1 CLOSE, 2 DOTS)
 *   DOTS only, after those two:
 *     u8 whole (0 / 1) | u16 count | u16 gone
 *     count x { u32 uid | f32 x | f32 z | u8 flags (bit 0: the name follows) [ | u8 n | n bytes the character's name ] }
 *     gone x u32 uid
 *   OPEN and CLOSE are exactly 2 bytes. uid 0 is refused; count + gone <= kMaxRows; a name is at most
 *   kMaxNameBytes bytes; a position must be finite. Anything else (a short or long message, an unknown version or kind)
 *   is refused whole.
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_MAPDOTS_H
#define COOP_COMMON_MAPDOTS_H

#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mapdots {

const unsigned char kVersion = 1;
enum Kind { kOpen = 0, kClose = 1, kDots = 2 };
const size_t kMaxRows = 512;           /* dots + gone in one message (a player's whole roster fits many times over) */
const size_t kMaxNameBytes = 96;       /* a character's name, cut at a UTF-8 boundary before sending */
const float kMoveMinUnits = 50.0f;     /* "moved more than a few metres": 5 m (10 game units = 1 m, buildnear.h) */
const long kCheckMs = 500;             /* the sender's check: about twice a second (approved mock-up) */
const size_t kMaxDotsPerSlot = 256;    /* the most dots one teammate's game can put on this map (review fold 8) */
const long kCloseReplyMs = 2000;       /* a refused DOTS is answered with CLOSE at most this often per player (review fold 6) */

struct Row
{
    unsigned uid;
    float x, z;
    bool hasNames;
    std::string name;   /* the character's name only - no squad name travels (owner decision 1438) */
    Row() : uid(0), x(0.0f), z(0.0f), hasNames(false) {}
};
struct Msg
{
    int kind;
    bool whole;
    std::vector<Row> rows;
    std::vector<unsigned> gone;
    Msg() : kind(-1), whole(false) {}
};

inline bool Finite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }

/* `s` cut to at most `n` bytes, never inside a UTF-8 character */
inline std::string CutUtf8(const std::string& s, size_t n)
{
    if (s.size() <= n) return s;
    size_t k = n;
    while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) --k;
    return s.substr(0, k);
}

inline void Put8(std::vector<char>* b, unsigned v) { b->push_back((char)(unsigned char)(v & 0xFF)); }
inline void Put16(std::vector<char>* b, unsigned v) { Put8(b, v); Put8(b, v >> 8); }
inline void Put32(std::vector<char>* b, unsigned v) { Put16(b, v & 0xFFFF); Put16(b, v >> 16); }
inline void PutF(std::vector<char>* b, float f) { unsigned v = 0; std::memcpy(&v, &f, 4); Put32(b, v); }
inline void PutStr(std::vector<char>* b, const std::string& s)
{
    const std::string c = CutUtf8(s, kMaxNameBytes);
    Put8(b, (unsigned)c.size());
    b->insert(b->end(), c.begin(), c.end());
}

/* OPEN or CLOSE: 2 bytes. false for any other kind. */
inline bool EncodeSignal(int kind, std::vector<char>* out)
{
    if (out == 0 || (kind != kOpen && kind != kClose)) return false;
    out->clear();
    Put8(out, kVersion); Put8(out, (unsigned)kind);
    return true;
}
/* DOTS. false (out cleared) = too many rows, a uid 0 or a position that is not finite. */
inline bool EncodeDots(bool whole, const std::vector<Row>& rows, const std::vector<unsigned>& gone, std::vector<char>* out)
{
    if (out == 0) return false;
    out->clear();
    if (rows.size() + gone.size() > kMaxRows) return false;
    Put8(out, kVersion); Put8(out, (unsigned)kDots); Put8(out, whole ? 1u : 0u);
    Put16(out, (unsigned)rows.size()); Put16(out, (unsigned)gone.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const Row& r = rows[i];
        if (r.uid == 0 || !Finite(r.x) || !Finite(r.z)) { out->clear(); return false; }
        Put32(out, r.uid); PutF(out, r.x); PutF(out, r.z); Put8(out, r.hasNames ? 1u : 0u);
        if (r.hasNames) PutStr(out, r.name);
    }
    for (size_t i = 0; i < gone.size(); ++i)
    {
        if (gone[i] == 0) { out->clear(); return false; }
        Put32(out, gone[i]);
    }
    return true;
}

struct Reader
{
    const unsigned char* p; size_t n, at; bool ok;
    Reader(const void* d, size_t len) : p((const unsigned char*)d), n(len), at(0), ok(d != 0 || len == 0) {}
    unsigned U8() { if (!ok || at + 1 > n) { ok = false; return 0; } return p[at++]; }
    unsigned U16() { const unsigned a = U8(); const unsigned b = U8(); return a | (b << 8); }
    unsigned U32() { const unsigned a = U16(); const unsigned b = U16(); return a | (b << 16); }
    float F() { const unsigned v = U32(); float f = 0.0f; std::memcpy(&f, &v, 4); return f; }
    std::string Str()
    {
        const unsigned k = U8();
        if (!ok || k > kMaxNameBytes || at + k > n) { ok = false; return std::string(); }
        const std::string s((const char*)p + at, (size_t)k);
        at += k;
        return s;
    }
};

/* The whole message, or false (out left as a refused message). */
inline bool Decode(const void* p, size_t n, Msg* out)
{
    if (out == 0) return false;
    *out = Msg();
    Reader r(p, n);
    if (r.U8() != kVersion || !r.ok) return false;
    const unsigned kind = r.U8();
    if (!r.ok) return false;
    if (kind == (unsigned)kOpen || kind == (unsigned)kClose)
    {
        if (n != 2) return false;
        out->kind = (int)kind;
        return true;
    }
    if (kind != (unsigned)kDots) return false;
    const unsigned whole = r.U8();
    const unsigned count = r.U16(), gone = r.U16();
    if (!r.ok || whole > 1u || (size_t)count + (size_t)gone > kMaxRows) return false;
    Msg m;
    m.kind = kDots; m.whole = whole == 1u;
    for (unsigned i = 0; i < count; ++i)
    {
        Row w;
        w.uid = r.U32(); w.x = r.F(); w.z = r.F();
        const unsigned flags = r.U8();
        if (!r.ok || w.uid == 0 || flags > 1u || !Finite(w.x) || !Finite(w.z)) return false;
        w.hasNames = flags == 1u;
        if (w.hasNames) { w.name = r.Str(); if (!r.ok) return false; }
        m.rows.push_back(w);
    }
    for (unsigned i = 0; i < gone; ++i)
    {
        const unsigned u = r.U32();
        if (!r.ok || u == 0) return false;
        m.gone.push_back(u);
    }
    if (r.at != n) return false;
    *out = m;
    return true;
}

/* THE DELTA RULE: a character is sent again when it moved more than kMoveMinUnits on the ground plane since it was last
   sent to that viewer. Exactly kMoveMinUnits is not "more than". */
inline bool MovedEnough(float sentX, float sentZ, float nowX, float nowZ)
{
    const float dx = nowX - sentX, dz = nowZ - sentZ;
    return dx * dx + dz * dz > kMoveMinUnits * kMoveMinUnits;
}

/* THE TEAM FILTER, both ends. `other` = the slot on the far end, `me` = this game's slot, `sameTeam` = the team table says the
   two share a faction of players, `inWorld` = the PLAYERS roster lists `other` in the world (StoreRosterSlotInWorld == 1).
   A sender answers an OPEN (and keeps sending) only when this holds; a viewer takes DOTS only when it holds AND it asked
   that slot (asked = it sent OPEN and no CLOSE since). */
inline bool TeammateOnline(int other, int me, bool sameTeam, int inWorld)
{
    return other >= 0 && me >= 0 && other != me && sameTeam && inWorld == 1;
}
inline bool ViewerTakes(int origin, int me, bool sameTeam, int inWorld, bool asked, bool mapOpen)
{
    return mapOpen && asked && TeammateOnline(origin, me, sameTeam, inWorld);
}
/* T-930 (run T1925: after B re-dialled, A's OPEN reached B 0.7 s before B's team table came back; B refused it and A, which counts
   a teammate as asked once its OPEN went out, never asked again - A got no dots from B for that open). A sender KEEPS an OPEN it
   refused (not a teammate here yet) with the link it came on, and steps it on every main-loop tick from the real facts:
   kPendAccept - this game's team table now names the viewer an online teammate: answered as if the OPEN came now;
   kPendDrop   - this game's link changed (keptGen != gen: the viewer asks again on the new link when it sees this game come
                 back), or this link's roster says the viewer is not in the world (inWorld 0);
   kPendKeep   - else (no roster yet: inWorld -1; or still not a teammate here). The viewer's CLOSE drops it (teammap.cpp). */
enum { kPendKeep = 0, kPendAccept = 1, kPendDrop = 2 };
const size_t kMaxPendingOpens = 64;   /* OPENs kept at once (one per player slot) */
inline int PendingOpenStep(int origin, int me, bool sameTeam, int inWorld, long keptGen, long gen)
{
    if (keptGen != gen || origin < 0 || origin == me) return kPendDrop;
    if (TeammateOnline(origin, me, sameTeam, inWorld)) return kPendAccept;
    return inWorld == 0 ? kPendDrop : kPendKeep;
}
/* Whether a refused OPEN is kept (1): another player's (not this game's own slot), on a link that is up (gen != 0), room left */
inline int PendingOpenKeep(int origin, int me, long gen, size_t kept)
{
    return (origin >= 0 && me >= 0 && origin != me && gen != 0 && kept < kMaxPendingOpens) ? 1 : 0;
}

/* WHERE A DOT GOES - the map screen's own sums (1.0.65 0x493040 / 0x492380, the player's own character dots): the canvas
   pixel = (world - origin) / span x canvas size, x from the world x, y from the world z, rounded to the nearest whole pixel;
   the dot's top-left is that pixel less half its size. false = a span of 0 or a value that is not finite. */
inline int RoundPx(float v) { return v >= 0.0f ? (int)(v + 0.5f) : -(int)(-v + 0.5f); }
inline bool CanvasPixel(float wx, float wz, float originX, float originZ, float spanX, float spanZ, int canvasW, int canvasH,
                        int* px, int* py)
{
    if (px == 0 || py == 0 || spanX == 0.0f || spanZ == 0.0f || !Finite(wx) || !Finite(wz) || !Finite(originX)
        || !Finite(originZ) || !Finite(spanX) || !Finite(spanZ)) return false;
    const float fx = (wx - originX) / spanX * (float)canvasW, fy = (wz - originZ) / spanZ * (float)canvasH;
    if (!Finite(fx) || !Finite(fy) || fx > 1.0e8f || fx < -1.0e8f || fy > 1.0e8f || fy < -1.0e8f) return false;
    *px = RoundPx(fx); *py = RoundPx(fy);
    return true;
}
/* The dot's size: the image set's size for "Character" times the map's marker scale (the game's own two globals, 1.0.65
   0x213278C / 0x2132790), each kept within 4..128 pixels so an unreadable scale never draws a giant or an invisible dot. */
inline int DotSide(int imageSide, float scale)
{
    if (imageSide <= 0) imageSide = 16;
    if (!Finite(scale) || scale <= 0.0f) scale = 1.0f;
    int v = (int)((float)imageSide * scale);
    if (v < 4) v = 4;
    if (v > 128) v = 128;
    return v;
}

/* A name as a MyGUI caption shows it as written: '#' starts a colour code in a caption, so it is doubled; a line break or
   carriage return would add a line, so it is a space (review fold 9). */
inline std::string CaptionSafe(const std::string& s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '#') o += "##";
        else if (c == '\n' || c == '\r') o += ' ';
        else o += c;
    }
    return o;
}
/* The hover note's two lines (owner decision 1438, the mock-up's "Ruka" / "Sam's squad"): the character's name, then the owning
   player's name + "'s squad" - just the character's name when the player's name is not known here. Both made caption-safe. */
inline std::string HoverText(const std::string& name, const std::string& playerName)
{
    return playerName.empty() ? CaptionSafe(name) : CaptionSafe(name) + "\n" + CaptionSafe(playerName) + "'s squad";
}

/* ---- THE SENDER'S CHOICE (review fold 10): what one DOTS to one viewer carries ---- */
struct OwnRow { unsigned uid; float x, z; std::string name; OwnRow() : uid(0), x(0.0f), z(0.0f) {} };
struct SentRow { float x, z; std::string name; SentRow() : x(0.0f), z(0.0f) {} };
/* `own` = this player's living characters now; `sent` = what the viewer was last sent; `whole` = the viewer has nothing yet
   (just after its OPEN, or after a world teardown here). Rows: every character when whole, else each one not sent yet, renamed
   (its name rides along), or moved more than kMoveMinUnits; gone (never when whole - a whole list replaces the viewer's): every
   uid sent and no longer here. Over kMaxRows, gone is kept whole and the rows are cut - the cut ones were not sent, so the next
   check sends them. true = there is a message to send (a whole list always is, even empty: it clears the viewer). Nothing is
   recorded here: SentApply records it, only after the send went out, so a failed send is planned again unchanged. */
inline bool SendPlan(bool whole, const std::vector<OwnRow>& own, const std::map<unsigned, SentRow>& sent, std::vector<Row>* rows,
                     std::vector<unsigned>* gone)
{
    rows->clear(); gone->clear();
    std::set<unsigned> here;
    for (size_t i = 0; i < own.size(); ++i)
    {
        const OwnRow& o = own[i];
        if (o.uid == 0) continue;
        here.insert(o.uid);
        std::map<unsigned, SentRow>::const_iterator s = sent.find(o.uid);
        const bool fresh = whole || s == sent.end();
        const bool names = fresh || s->second.name != o.name;
        if (!fresh && !names && !MovedEnough(s->second.x, s->second.z, o.x, o.z)) continue;
        Row r; r.uid = o.uid; r.x = o.x; r.z = o.z; r.hasNames = names; r.name = o.name;
        rows->push_back(r);
    }
    if (!whole)
        for (std::map<unsigned, SentRow>::const_iterator it = sent.begin(); it != sent.end(); ++it)
            if (here.count(it->first) == 0) gone->push_back(it->first);
    if (gone->size() > kMaxRows) gone->resize(kMaxRows);
    if (rows->size() + gone->size() > kMaxRows) rows->resize(kMaxRows - gone->size());
    return whole || !rows->empty() || !gone->empty();
}
/* After the message went out: what the viewer now holds. */
inline void SentApply(bool whole, const std::vector<Row>& rows, const std::vector<unsigned>& gone, std::map<unsigned, SentRow>* sent)
{
    if (whole) sent->clear();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        SentRow& s = (*sent)[rows[i].uid];
        s.x = rows[i].x; s.z = rows[i].z;
        if (rows[i].hasNames) s.name = rows[i].name;
    }
    for (size_t i = 0; i < gone.size(); ++i) sent->erase(gone[i]);
}

/* ---- THE VIEWER'S SIDE ---- */
/* A whole list from one teammate: of the uids this game shows for it (`have`), the ones the list does not name go. */
inline void WholeDrops(const std::vector<unsigned>& have, const Msg& m, std::vector<unsigned>* drop)
{
    drop->clear();
    if (m.kind != kDots || !m.whole) return;
    std::set<unsigned> keep;
    for (size_t i = 0; i < m.rows.size(); ++i) keep.insert(m.rows[i].uid);
    for (size_t i = 0; i < have.size(); ++i) if (keep.count(have[i]) == 0) drop->push_back(have[i]);
}
/* Whether a new dot from a teammate who already has `count` dots here is taken (review fold 8). */
inline bool DotRoom(size_t count) { return count < kMaxDotsPerSlot; }
/* HOW THE DOTS GO WHEN THE MAP CLOSES (review folds 1 / 2). kDropChecked: the map was read this frame (read == 1) under the same
   holder the dots were made under - each is destroyed only when it is still one of that holder's children (else forgotten).
   kDropByName: the holder is not proven alive (the read failed, the holder changed, the map's frame stopped) - each is looked up
   by its own name first and destroyed only when found. */
enum { kDropChecked = 0, kDropByName = 1 };
inline int CloseDropMode(int read, bool sameHolder) { return (read == 1 && sameHolder) ? kDropChecked : kDropByName; }
/* Whether a refused DOTS from a player is answered with CLOSE now (review fold 6): at most once per kCloseReplyMs per player.
   `lastMs` = when that player was last answered (`have` false = never). */
inline bool CloseReplyDue(bool have, unsigned long lastMs, unsigned long nowMs) { return !have || nowMs - lastMs >= (unsigned long)kCloseReplyMs; }

}   /* namespace mapdots */

#endif
