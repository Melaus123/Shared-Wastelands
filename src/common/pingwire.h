/* src/common/pingwire.h - T-1393 (owner decisions 1725 / 1726 a / 1728, approved mock-up build/pages/ping-mockup.html): PINGS.
 * The pure half: the message's bytes, the rate limit, a ping's life and the replace rule, who may receive one, the chat words,
 * the off-screen arrow's place, the map's pixel <-> world sums, the ground under a ray, and the stripes a marker is drawn from.
 * src/coop-plugin/ping.cpp feeds these and draws.
 *
 * WHAT THE PLAYER SEES. G pings the spot under the mouse: on the world map when the map is open, otherwise the ground in the
 * world (ignored while typing in chat or any text box; the key is fixed). The player's TEAMMATES (owner 1726 a; never a hostile
 * player, owner 1725) see a yellow diamond on a short stem with the pinging player's name above it, on the spot - in the world
 * and on the map (slightly smaller there; a world ping also shows on the map) - and a chat line "<name> pinged a spot." /
 * "<name> pinged a spot on the map." with the chat message sound (switched off with chat sounds). Off screen, a yellow arrow at
 * the screen edge points towards the spot, with the name. A ping pulses gently, lasts 6 s, then fades; a new ping from the
 * same player replaces their old one. At most 3 pings in 5 s per player; extra presses do nothing, with no message. The player
 * who pings sees their own marker and chat line too.
 *
 * THE ROAD. One inner type, cooplive::kInnerSpotPing (207, liverelay.h), through the world server's LIVE relay, route SLOT - one
 * copy to each online teammate, as a faction chat line goes. 17 bytes: kind (u8: 0 world, 1 map), the sender's own count of
 * its pings (u32), x, y, z (float; y is the ground's height for a world ping and 0 for a map ping). The sender is the
 * envelope's origin slot, stamped by the world server. Anything else (another length, an unknown kind, a position that is not
 * finite or outside the world's square) is refused whole.
 *
 * Pure: no global, no OS call. C++03 (VS2010 v100).
 */
#ifndef COOP_COMMON_PINGWIRE_H
#define COOP_COMMON_PINGWIRE_H

#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace pingwire {

enum Kind { kWorld = 0, kMap = 1 };
const size_t kBytes = 17;                 /* u8 kind, u32 count, 3 x float */
const long kLifeMs = 6000;                /* "lasts 6 s" (mock-up) */
const long kFadeMs = 800;                 /* "then fades" - the fade after the 6 s */
const int kRateMax = 3;                   /* "at most 3 pings in 5 s per player" (mock-up) - the sender's own limit */
const int kRecvRateMax = 4;               /* review fold 1: the receiver times pings by ARRIVAL, so a late first ping can make a fair
                                             4th look too soon - it accepts one more per sender in 5 s than the sender may send */
const int kRateSlots = 4;                 /* the most a Rate can hold */
const unsigned long kRateWindowMs = 5000;
const float kWorldHalf = 147456.0f;       /* the map's world square -147456..+147456 (decomp_48f780:106-109) */
/* T-1437 (owner 1786 a): the ping key is Kenshi's own rebindable action 'sw_ping' - a 'Ping' row at the end of the Controls list,
   default G, saved by the game in controls.cfg as `sw_ping=<key>` (one line per key, at most two keys, as the game's own lines). */
const char* const kActionName = "sw_ping";
const char* const kRowLabel = "Ping";      /* the Controls row's text (English, as the approved design) */
const char* const kDefaultKey = "G";       /* no default action and no installed controls.cfg line uses G (F1297, Confirmed) */
/* T-1437 (T2577: 'G is not a key the game knows'): a controls.cfg key word split the way InputHandler::parseKey 0x361B30 splits it
   (decomp_361b30.txt:40-104): a modifier prefix found ANYWHERE in the word sets its bit - "Ctrl+"/"ctrl+" 0x200, "Alt+"/"alt+" 0x400,
   "Shift+"/"shift+" 0x100 - and the key's own name is what follows the furthest prefix; "Mouse" + a digit is a mouse button,
   digit << 12 (:120-134). The name itself is then matched against the game's own key names (ping.cpp GameKeyForWord), not
   through parseKey: its name table (handler+0xA8) exists only while loadConfig 0x3623D0 runs (filled :517-614, emptied :864-868).
   Returns the name (empty for a mouse button); *mods the modifier bits; *mouse the mouse button's value or 0. */
inline std::string KeyWordSplit(const std::string& word, unsigned* mods, unsigned* mouse)
{
    static const char* const kPre[6] = { "Ctrl+", "ctrl+", "Alt+", "alt+", "Shift+", "shift+" };
    static const unsigned kBit[6] = { 0x200, 0x200, 0x400, 0x400, 0x100, 0x100 };
    *mods = 0; *mouse = 0;
    size_t from = 0;
    for (int i = 0; i < 6; ++i)
    {
        const size_t at = word.find(kPre[i]);
        if (at == std::string::npos) continue;
        *mods |= kBit[i];
        const size_t end = at + std::strlen(kPre[i]);
        if (end > from) from = end;
    }
    const std::string rest = word.substr(from);
    if (rest.size() >= 6 && rest.compare(0, 5, "Mouse") == 0 && rest[5] >= '0' && rest[5] <= '9')
    {
        *mouse = (unsigned)(rest[5] - '0') << 12;
        return std::string();
    }
    return rest;
}
const int kMaxKeys = 2;                    /* the game keeps up to two keys per action (F1297) */
const float kHostileAt = -30.0f;          /* the game's own enemy test (nametag.h kHostileAt): relation <= -30 */

struct Msg
{
    int kind;
    unsigned count;
    float x, y, z;
    Msg() : kind(kWorld), count(0), x(0.0f), y(0.0f), z(0.0f) {}
};

inline bool Finite(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }
inline bool InWorldSquare(float x, float z) { return Finite(x) && Finite(z) && x >= -kWorldHalf && x <= kWorldHalf && z >= -kWorldHalf && z <= kWorldHalf; }
inline bool MsgOk(const Msg& m) { return (m.kind == kWorld || m.kind == kMap) && InWorldSquare(m.x, m.z) && Finite(m.y); }

inline bool Encode(const Msg& m, std::vector<char>* out)
{
    if (out == 0) return false;
    out->clear();
    if (!MsgOk(m)) return false;
    out->resize(kBytes);
    char* p = &(*out)[0];
    p[0] = (char)(unsigned char)m.kind;
    std::memcpy(p + 1, &m.count, 4);
    std::memcpy(p + 5, &m.x, 4); std::memcpy(p + 9, &m.y, 4); std::memcpy(p + 13, &m.z, 4);
    return true;
}
inline bool Decode(const void* data, size_t n, Msg* out)
{
    if (data == 0 || out == 0 || n != kBytes) return false;
    const char* p = (const char*)data;
    Msg m;
    m.kind = (int)(unsigned char)p[0];
    std::memcpy(&m.count, p + 1, 4);
    std::memcpy(&m.x, p + 5, 4); std::memcpy(&m.y, p + 9, 4); std::memcpy(&m.z, p + 13, 4);
    if (!MsgOk(m)) return false;
    *out = m;
    return true;
}

/* ---- the rate limit: at most `max` pings in any kRateWindowMs, per player; the sender's own presses (kRateMax) and, again,
   each sender's pings on a receiving game (kRecvRateMax). RateTake: true = this ping may go (and is counted); false = an extra,
   which does nothing. Times are GetTickCount milliseconds (unsigned, wrap-safe by subtraction). ---- */
struct Rate
{
    unsigned long at[kRateSlots];
    int n;
    Rate() : n(0) { for (int i = 0; i < kRateSlots; ++i) at[i] = 0; }
};
inline bool RateTake(Rate* r, unsigned long now, int max = kRateMax)
{
    if (r == 0) return false;
    if (max > kRateSlots) max = kRateSlots;
    int k = 0;
    for (int i = 0; i < r->n; ++i) if (now - r->at[i] < kRateWindowMs) r->at[k++] = r->at[i];
    r->n = k;
    if (r->n >= max) return false;
    r->at[r->n++] = now;
    return true;
}

/* ---- a ping's life: shown (pulsing) for kLifeMs, then fading for kFadeMs, then gone ---- */
enum Phase { kShown = 0, kFading = 1, kGone = 2 };
inline int PhaseAt(unsigned long ageMs)
{
    if (ageMs < (unsigned long)kLifeMs) return kShown;
    if (ageMs < (unsigned long)(kLifeMs + kFadeMs)) return kFading;
    return kGone;
}
/* the marker's opacity: 1 while shown, falling to 0 over the fade */
inline float AlphaAt(unsigned long ageMs)
{
    const int ph = PhaseAt(ageMs);
    if (ph == kShown) return 1.0f;
    if (ph == kGone) return 0.0f;
    return 1.0f - (float)(ageMs - (unsigned long)kLifeMs) / (float)kFadeMs;
}
/* "pulses gently" (the approved page: the diamond's opacity eases to 0.55 and back every 1.6 s, ease-in-out; its size stays) */
const unsigned long kPulseMs = 1600;
const float kPulseLow = 0.55f;
inline float PulseAlphaAt(unsigned long ageMs)
{
    const float t = (float)(ageMs % kPulseMs) / (float)kPulseMs;
    return 1.0f - (1.0f - kPulseLow) * 0.5f * (1.0f - std::cos(t * 6.2831853f));
}

/* ---- the replace rule: one live ping per player (keyed by the sender's slot); a new one replaces the old. 1 new, 2 replaced. ---- */
struct Live
{
    Msg m;
    unsigned long at;    /* when it came (GetTickCount) */
    Live() : at(0) {}
};
inline int Place(std::map<int, Live>* table, int slot, const Msg& m, unsigned long now)
{
    if (table == 0) return 0;
    const int r = table->count(slot) != 0 ? 2 : 1;
    Live& l = (*table)[slot];
    l.m = m; l.at = now;
    return r;
}

/* ---- who receives: the player's teammates only, never a hostile player (the receiver re-checks: owner 1725). The same test
   picks the sender's targets. relRead = 1 when this game's relation towards that player was read into rel ---- */
enum Verdict { kTake = 0, kDropSelf = 1, kDropNotMate = 2, kDropHostile = 3, kDropOffline = 4, kDropUnread = 5, kVerdicts = 6 };
inline int ReceiveVerdict(int origin, int me, bool sameTeam, int inWorld, int relRead, float rel)
{
    if (origin < 0 || me < 0) return kDropNotMate;
    if (origin == me) return kDropSelf;
    if (!sameTeam) return kDropNotMate;
    if (relRead != 1) return kDropUnread;   /* review fold 5: hostility that cannot be read is refused - a hostile player never sees a ping (owner 1725) */
    if (rel <= kHostileAt) return kDropHostile;
    if (inWorld != 1) return kDropOffline;
    return kTake;
}
inline const char* VerdictWord(int v)
{
    switch (v)
    {
    case kTake: return "taken";
    case kDropSelf: return "from this game itself";
    case kDropNotMate: return "not a teammate";
    case kDropHostile: return "hostile";
    case kDropOffline: return "not in the world";
    case kDropUnread: return "hostility unreadable";
    default: return "?";
    }
}

/* ---- the words (mock-up, word for word) ---- */
inline std::string ChatLine(const std::string& name, int kind)
{
    return name + (kind == kMap ? " pinged a spot on the map." : " pinged a spot.");
}
inline const char* KindWord(int kind) { return kind == kMap ? "map" : "world"; }

/* ---- the off-screen arrow: from the screen's centre along (dx, dy) (screen pixels, y down) to the edge of the screen less
   `margin`. false when there is no direction or no room. ---- */
inline bool EdgePlace(int w, int h, int margin, float dx, float dy, int* x, int* y)
{
    if (x == 0 || y == 0 || !Finite(dx) || !Finite(dy)) return false;
    const float hw = (float)w * 0.5f - (float)margin, hh = (float)h * 0.5f - (float)margin;
    if (hw <= 0.0f || hh <= 0.0f) return false;
    const float ax = dx < 0.0f ? -dx : dx, ay = dy < 0.0f ? -dy : dy;
    if (ax < 1.0e-6f && ay < 1.0e-6f) return false;
    float t = 1.0e30f;
    if (ax >= 1.0e-6f) t = hw / ax;
    if (ay >= 1.0e-6f && hh / ay < t) t = hh / ay;
    const float fx = (float)w * 0.5f + dx * t, fy = (float)h * 0.5f + dy * t;
    *x = (int)(fx >= 0.0f ? fx + 0.5f : fx - 0.5f);
    *y = (int)(fy >= 0.0f ? fy + 0.5f : fy - 0.5f);
    return true;
}
/* the screen direction towards a point from its VIEW-space position (the camera looks down -Z, +Y up): right = +x, down = -y.
   A point straight behind the camera points down. */
inline void ScreenDir(float vx, float vy, float vz, float* dx, float* dy)
{
    (void)vz;
    float x = vx, y = -vy;
    const float len = std::sqrt(x * x + y * y);
    if (!Finite(len) || len < 1.0e-6f) { *dx = 0.0f; *dy = 1.0f; return; }
    *dx = x / len; *dy = y / len;
}
/* review fold 4: the direction towards a point that PROJECTED (in front of the camera, outside the view): from the screen's
   centre to its projected place */
inline void ScreenDirFromPoint(float sx, float sy, int w, int h, float* dx, float* dy)
{
    const float x = sx - (float)w * 0.5f, y = sy - (float)h * 0.5f;
    const float len = std::sqrt(x * x + y * y);
    if (!Finite(len) || len < 1.0e-6f) { *dx = 0.0f; *dy = 1.0f; return; }
    *dx = x / len; *dy = y / len;
}
/* a projected point counts as on screen when it is inside the view less `margin` */
inline bool OnScreen(float sx, float sy, int w, int h, int margin)
{
    return Finite(sx) && Finite(sy) && sx >= (float)margin && sy >= (float)margin && sx <= (float)(w - margin) && sy <= (float)(h - margin);
}

/* ---- the map's sums. World -> canvas pixel is mapdots::CanvasPixel (the game's 0x493040); this is its inverse for the
   mouse: a canvas pixel (0..canvas size) -> world x, z, refused outside the canvas or the world's square (0x48C4E0 refuses a
   point outside the map's view). ---- */
inline bool MapWorldFromPixel(int px, int py, float originX, float originZ, float spanX, float spanZ, int canvasW, int canvasH,
                              float* wx, float* wz)
{
    if (wx == 0 || wz == 0 || canvasW <= 0 || canvasH <= 0 || px < 0 || py < 0 || px > canvasW || py > canvasH) return false;
    if (!Finite(originX) || !Finite(originZ) || !Finite(spanX) || !Finite(spanZ) || spanX == 0.0f || spanZ == 0.0f) return false;
    const float x = originX + (float)px / (float)canvasW * spanX, z = originZ + (float)py / (float)canvasH * spanZ;
    if (!InWorldSquare(x, z)) return false;
    *wx = x; *wz = z;
    return true;
}

/* ---- the ground under a ray: walked from `o` along `d` in `step`s up to `maxDist`; the first sample at or below the terrain's
   height is narrowed down between it and the sample before (16 halvings). height(ctx, x, z, &h): 1 a height, else none there.
   true = *out is the spot on the ground. ---- */
typedef int (*HeightFn)(void* ctx, float x, float z, float* h);
inline bool MarchGround(const float* o, const float* dIn, float maxDist, float step, HeightFn height, void* ctx, float* out)
{
    if (o == 0 || dIn == 0 || out == 0 || height == 0 || !(step > 0.0f) || !(maxDist > 0.0f)) return false;
    const float len = std::sqrt(dIn[0] * dIn[0] + dIn[1] * dIn[1] + dIn[2] * dIn[2]);
    if (!Finite(len) || len < 1.0e-6f) return false;
    const float d[3] = { dIn[0] / len, dIn[1] / len, dIn[2] / len };
    float prevT = -1.0f;
    for (float t = 0.0f; t <= maxDist; t += step)
    {
        const float x = o[0] + d[0] * t, y = o[1] + d[1] * t, z = o[2] + d[2] * t;
        float h = 0.0f;
        if (height(ctx, x, z, &h) != 1) { prevT = -1.0f; continue; }
        if (y > h) { prevT = t; continue; }
        float lo = prevT < 0.0f ? t : prevT, hi = t;
        for (int i = 0; i < 16 && prevT >= 0.0f; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            const float mx = o[0] + d[0] * mid, my = o[1] + d[1] * mid, mz = o[2] + d[2] * mid;
            float mh = 0.0f;
            if (height(ctx, mx, mz, &mh) == 1 && my <= mh) hi = mid; else lo = mid;
        }
        const float hx = o[0] + d[0] * hi, hz = o[2] + d[2] * hi;
        float hh = 0.0f;
        if (height(ctx, hx, hz, &hh) != 1) hh = o[1] + d[1] * hi;
        out[0] = hx; out[1] = hh; out[2] = hz;
        return true;
    }
    return false;
}
/* how far a point lies from the ray's line (a physics hit is used only when it lies on the ray: a sanity check of its space) */
inline float RayLineDistance(const float* o, const float* dIn, const float* p)
{
    const float len = std::sqrt(dIn[0] * dIn[0] + dIn[1] * dIn[1] + dIn[2] * dIn[2]);
    if (!Finite(len) || len < 1.0e-6f) return 1.0e30f;
    const float d[3] = { dIn[0] / len, dIn[1] / len, dIn[2] / len };
    const float v[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
    const float t = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
    const float q[3] = { v[0] - d[0] * t, v[1] - d[1] * t, v[2] - d[2] * t };
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}
inline float RayAlong(const float* o, const float* dIn, const float* p)
{
    const float len = std::sqrt(dIn[0] * dIn[0] + dIn[1] * dIn[1] + dIn[2] * dIn[2]);
    if (!Finite(len) || len < 1.0e-6f) return -1.0f;
    return ((p[0] - o[0]) * dIn[0] + (p[1] - o[1]) * dIn[1] + (p[2] - o[2]) * dIn[2]) / len;
}

/* ---- the shapes. A marker is drawn from flat stripes (the game's WhiteSkin, tinted), one per `rowH` pixels of a convex shape:
   each row the shape's width at the row's middle. ---- */
struct Span { int y, x0, x1; };
inline void RasterConvex(const float* px, const float* py, int n, int rowH, std::vector<Span>* out)
{
    if (out == 0) return;
    out->clear();
    if (px == 0 || py == 0 || n < 3 || rowH <= 0) return;
    float top = py[0], bot = py[0];
    for (int i = 1; i < n; ++i) { if (py[i] < top) top = py[i]; if (py[i] > bot) bot = py[i]; }
    const int y0 = (int)std::floor(top), y1 = (int)std::ceil(bot);
    for (int y = y0; y < y1; y += rowH)
    {
        const float cy = (float)y + (float)rowH * 0.5f;
        float lo = 1.0e30f, hi = -1.0e30f;
        for (int i = 0; i < n; ++i)
        {
            const int j = (i + 1) % n;
            const float ay = py[i], by = py[j];
            if ((cy < ay && cy < by) || (cy > ay && cy > by) || ay == by) continue;
            const float x = px[i] + (cy - ay) / (by - ay) * (px[j] - px[i]);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        if (hi <= lo) continue;
        Span s; s.y = y; s.x0 = (int)std::floor(lo + 0.5f); s.x1 = (int)std::floor(hi + 0.5f);
        if (s.x1 <= s.x0) s.x1 = s.x0 + 1;
        out->push_back(s);
    }
}
/* the diamond: centre (cx, cy), half its height `half` (as wide as tall) */
inline void DiamondSpans(float cx, float cy, float half, int rowH, std::vector<Span>* out)
{
    const float x[4] = { cx, cx + half, cx, cx - half };
    const float y[4] = { cy - half, cy, cy + half, cy };
    RasterConvex(x, y, 4, rowH, out);
}
/* the arrow: a triangle whose tip is at (tx, ty) pointing along the unit (dx, dy), `len` long and `half` half as wide */
inline void ArrowSpans(float tx, float ty, float dx, float dy, float len, float half, int rowH, std::vector<Span>* out)
{
    const float bx = tx - dx * len, by = ty - dy * len;
    const float x[3] = { tx, bx - dy * half, bx + dy * half };
    const float y[3] = { ty, by + dx * half, by - dx * half };
    RasterConvex(x, y, 3, rowH, out);
}

/* T-1437: the key words of every `sw_ping=<key>` line in controls.cfg's text, in file order, at most kMaxKeys. A line is split at
   its first '='; the name and the key word are trimmed of spaces, tabs and the CR of a Windows line end; a line with another name,
   no '=' or an empty key word gives nothing. The words go to the game's own key parser - nothing here decides what a key is. */
inline void CfgPingKeys(const std::string& text, std::vector<std::string>* out)
{
    out->clear();
    size_t at = 0;
    while (at <= text.size() && (int)out->size() < kMaxKeys)
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq), key = line.substr(eq + 1);
        for (int pass = 0; pass < 2; ++pass)
        {
            std::string& w = pass == 0 ? name : key;
            size_t b = 0, e = w.size();
            while (b < e && (w[b] == ' ' || w[b] == '\t' || w[b] == '\r')) ++b;
            while (e > b && (w[e - 1] == ' ' || w[e - 1] == '\t' || w[e - 1] == '\r')) --e;
            w = w.substr(b, e - b);
        }
        if (name == kActionName && !key.empty()) out->push_back(key);
    }
}

}   /* namespace pingwire */

#endif
