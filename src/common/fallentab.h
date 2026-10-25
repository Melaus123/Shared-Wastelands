/* src/common/fallentab.h - T-556: THE FALLEN TAB's pure decisions (owner 485-510; the approved screen is
 * build/pages/resurrection.html). Every word the tab, its confirm box, its caption line and the game's message line show; which
 * of its states the tab is in; and the TEST lever's forms. No engine memory, no Windows. One header compiled into the plugin
 * (fallentab.cpp, resurrect.cpp, playerstab.cpp) and the offline suite (src/coop-test/test_main.cpp).
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for.
 */
#pragma once

#include "fallenwire.h"
#include "resurrectfee.h"
#include <cmath>   /* SpotCandidates: the camera road's rings */
#include <sstream>
#include <string>
#include <vector>

namespace swtab {

const char* const kTabCaption = "FALLEN";

/* ---- the management window's tab strip ---- */
/* The tab buttons' width when `count` tabs share the strip the game's own `engineCount` filled at `engineWidth` each. */
inline int StripButtonWidth(int engineWidth, int engineCount, int count)
{
    if (engineWidth <= 0 || engineCount <= 0 || count <= engineCount) return engineWidth;
    return engineWidth * engineCount / count;
}
/* `count` buttons sharing `strip` pixels as evenly as whole pixels allow, filling it exactly: the first strip % count
   buttons are one pixel wider. */
inline std::vector<int> EqualWidths(int strip, int count)
{
    std::vector<int> w(count > 0 ? (size_t)count : 0, 0);
    if (count <= 0 || strip <= 0) return w;
    const int each = strip / count, wider = strip % count;
    for (int i = 0; i < count; ++i) w[(size_t)i] = each + (i < wider ? 1 : 0);
    return w;
}
/* Each tab button's width across `strip` pixels, from each caption's text width in the buttons' font (`text`) and the
   skin's room around the text (`pad`): equal shares (EqualWidths) while every caption fits its share whole; otherwise each
   button gets its own caption's width plus the pad and what is left of the strip is shared out evenly (the last takes the
   rounding). When the captions with their pad are wider than the strip, the pad shrinks evenly (to 0 at most). */
inline std::vector<int> StripWidths(int strip, int pad, const std::vector<int>& text)
{
    const int n = (int)text.size();
    std::vector<int> w(text.size(), 0);
    if (n == 0 || strip <= 0) return w;
    if (pad < 0) pad = 0;
    const int equal = strip / n;
    bool fits = true;
    long long need = 0, sumText = 0;
    for (int i = 0; i < n; ++i) { if (text[i] + pad > equal) fits = false; need += text[i] + pad; sumText += text[i]; }
    if (fits) return EqualWidths(strip, n);
    int each = pad;
    if (need > strip) each = sumText >= strip ? 0 : (int)((strip - sumText) / n);
    long long used = 0;
    for (int i = 0; i < n; ++i) { w[i] = text[i] + each; used += w[i]; }
    if (used < strip)
    {
        const int left = (int)(strip - used), share = left / n;
        for (int i = 0; i < n; ++i) w[i] += share;
        w[n - 1] += left - share * n;
    }
    return w;
}

/* ---- words ---- */
inline std::string Num(long long v) { std::ostringstream o; o << v; return o.str(); }
/* money as Kenshi's money bar writes it: c.1,000 */
inline std::string Money(long long v)
{
    const std::string d = Num(v < 0 ? 0 : v);
    std::string r;
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (i > 0 && (d.size() - i) % 3 == 0) r += ',';
        r += d[i];
    }
    return "c." + r;
}
inline std::string Upper(const std::string& s)
{
    std::string u(s);
    for (size_t i = 0; i < u.size(); ++i) if (u[i] >= 'a' && u[i] <= 'z') u[i] = (char)(u[i] - 'a' + 'A');
    return u;
}

/* ---- the list: NAME RACE DIED PLACE CAUSE, newest first ---- */
const int kColumns = 5;
inline const char* ColumnHead(int c)
{
    static const char* const h[kColumns] = { "NAME", "RACE", "DIED", "PLACE", "CAUSE" };
    return (c >= 0 && c < kColumns) ? h[c] : "";
}
inline int ColumnPercent(int c)
{
    static const int p[kColumns] = { 22, 18, 14, 26, 20 };
    return (c >= 0 && c < kColumns) ? p[c] : 0;
}
/* the columns' widths across `w` pixels; the last takes what rounding left */
inline void ColumnWidths(int w, int* out)
{
    int used = 0;
    for (int c = 0; c < kColumns; ++c) { out[c] = w * ColumnPercent(c) / 100; used += out[c]; }
    out[kColumns - 1] += w - used;
}
/* DIED: the in-game day as the game's clock counts it ("" for a row kept before the day was) */
inline std::string DayWords(int day) { return day >= 0 ? "Day " + Num(day) : std::string(); }
/* PLACE: "near" and the nearest town ("" when no town was near); a town named "The ..." reads "near the Hub" (the page) */
inline std::string PlaceCell(const std::string& town)
{
    if (town.empty()) return std::string();
    if (town.size() > 4 && town.compare(0, 4, "The ") == 0) return "near the " + town.substr(4);
    return "near " + town;
}
/* the DIED cell's day number ("Day 41" -> 41); -1 for an empty or any other cell */
inline int DayOfCell(const std::string& cell)
{
    if (cell.size() < 5 || cell.compare(0, 4, "Day ") != 0) return -1;
    int d = 0;
    for (size_t i = 4; i < cell.size(); ++i)
    {
        if (cell[i] < '0' || cell[i] > '9' || d > 100000000) return -1;
        d = d * 10 + (cell[i] - '0');
    }
    return d;
}
const int kDiedColumn = 2;
/* a column head's sort (MultiListBox requestOperatorLess, as ui.cpp's lists): DIED by the day's number, an empty cell before
   every day; every other column by its text */
inline bool CellLess(int column, const std::string& a, const std::string& b)
{
    if (column == kDiedColumn) return DayOfCell(a) < DayOfCell(b);
    return a < b;
}
/* CAUSE: blood loss and starvation by name; every other death, and a row kept before the snapshot read it, is injuries */
inline const char* CauseWords(int why)
{
    return why == swfallen::kWhyBloodLoss ? "Blood loss" : why == swfallen::kWhyStarvation ? "Starvation" : "Injuries";
}
/* Seto and Big Bo (owner 505): a named character whose record (or name) is one of the two */
inline int SetoOrBigBo(int unique, const std::string& templateName, const std::string& name)
{
    if (!unique) return 0;
    return (templateName == "Seto" || templateName == "Big Bo" || name == "Seto" || name == "Big Bo") ? 1 : 0;
}
struct Row
{
    unsigned int uid;      /* the dead character's uid: the row's key */
    std::string name;
    int setoOrBigBo;
    std::string cell[kColumns];
    Row() : uid(0), setoOrBigBo(0) {}
};
inline Row RowFor(const swfallen::Fallen& f)
{
    Row r;
    r.uid = f.uid;
    r.name = f.name.empty() ? f.templateName : f.name;
    r.setoOrBigBo = SetoOrBigBo(f.unique, f.templateName, f.name);
    r.cell[0] = r.name;
    r.cell[1] = (f.race.empty() || f.race == "?") ? f.templateName : f.race;
    r.cell[2] = DayWords(f.day);
    r.cell[3] = PlaceCell(f.place);
    r.cell[4] = CauseWords(f.why);
    return r;
}
inline bool SameRows(const std::vector<Row>& a, const std::vector<Row>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].uid != b[i].uid) return false;
        for (int c = 0; c < kColumns; ++c) if (a[i].cell[c] != b[i].cell[c]) return false;
    }
    return true;
}
inline int RowOfUid(const std::vector<Row>& rows, unsigned int uid)
{
    if (uid == 0) return -1;
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].uid == uid) return (int)i;
    return -1;
}
const char* const kEmptyLine = "None of your characters has fallen.";
inline std::string SelectLine() { return std::string(); }   /* nothing selected: no line */

/* ---- the price lines (owner 497, 501): the price and its sum, who is counted ---- */
/* the rule's sum: "c.1,000 x 2 x 2" (steep), "c.1,000 x 2" (steady) */
inline std::string RuleSum(const swfee::Price& p)
{
    std::string s = Money(p.amount) + " x " + Num(p.alive);
    if (p.growth == swfee::kGrowthSteep) s += " x " + Num(p.alive);
    return s;
}
/* who is counted: up to three names; with more, the first two and "and N more" */
inline std::string AliveNames(const std::vector<std::string>& names)
{
    std::string s;
    if (names.size() <= 3)
    {
        for (size_t i = 0; i < names.size(); ++i) s += (i ? ", " : "") + names[i];
        return s;
    }
    return names[0] + ", " + names[1] + " and " + Num((long long)names.size() - 2) + " more";
}
/* "1 character you brought back is alive" / "2 characters you brought back are alive" */
inline std::string AliveClause(int alive)
{
    return Num(alive) + (alive == 1 ? " character you brought back is alive" : " characters you brought back are alive");
}
const int kPriceLines = 2;   /* the price, and who is counted */
struct PriceLines { int shown; std::string line[kPriceLines]; PriceLines() : shown(0) {} };
/* `names` = the brought-back characters alive now (their count is A). Hidden while the host has resurrection off. */
inline PriceLines PriceLinesFor(int on, long long amount, int growth, const std::vector<std::string>& names)
{
    PriceLines out;
    if (!on) return out;
    out.shown = 1;
    const swfee::Price p = swfee::PriceFor(amount, growth, (int)names.size());
    if (p.amount == 0) { out.line[0] = "PRICE: free"; out.line[1] = "The host has made bringing back free."; return out; }
    if (p.alive == 0) { out.line[0] = "PRICE: free"; out.line[1] = "None of the characters you brought back is alive."; return out; }
    out.line[0] = "PRICE " + Money(p.price) + " (the host's " + swfee::GrowthName(p.growth) + " rule: " + RuleSum(p) + ")";
    out.line[1] = AliveClause(p.alive) + ": " + AliveNames(names) + ".";
    return out;
}

/* ---- the controls under the list ---- */
/* BRING BACK with the price; just BRING BACK when it is free or resurrection is off */
inline std::string ButtonCaption(int on, long long price) { return (on && price > 0) ? "BRING BACK  " + Money(price) : std::string("BRING BACK"); }
inline std::string BesideLabel(const std::string& name) { return "BRING " + Upper(name) + " BACK BESIDE"; }
/* one squadmate in the drop-down: "name  (squad name)" */
inline std::string MateItem(const std::string& name, const std::string& squad) { return squad.empty() ? name : name + "  (" + squad + ")"; }
/* where the drop-down starts (the page): on the first squadmate the player has selected in the game, else the first in the
   list; -1 for an empty list. selected[i]: 1 when squadmate i is selected in the game. */
inline int StartMate(const std::vector<int>& selected)
{
    for (size_t i = 0; i < selected.size(); ++i) if (selected[i]) return (int)i;
    return selected.empty() ? -1 : 0;
}

/* ---- the confirm box ---- */
const char* const kBoxTitle = "BRING BACK";
const char* const kBoxCancel = "CANCEL";
const char* const kBoxConfirm = "BRING BACK";
/* where: "beside <mate>" (the squadmate road) or "where your camera is looking" (the camera road) */
inline std::string BoxTextWhere(const std::string& name, const std::string& where, long long amount, int growth, int alive, int setoOrBigBo)
{
    const swfee::Price p = swfee::PriceFor(amount, growth, alive);
    const swfee::Price next = swfee::PriceFor(amount, growth, (alive < 0 ? 0 : alive) + 1);
    std::string t = "Bring " + name + " back " + where + (p.price > 0 ? " for " + Money(p.price) : std::string()) + "? "
                  + name + " returns with all limbs and no equipment. Their gear stays with their body where they fell.";
    if (p.amount > 0)
    {
        if (p.price > 0) t += " The price is " + RuleSum(p) + " because " + AliveClause(p.alive) + ".";
        else t += " It is free because none of the characters you brought back is alive.";
        t += " After this one, the next will cost " + Money(next.price) + ".";
    }
    if (setoOrBigBo) t += " What happened in the world because " + name + " died stays as it is.";
    return t;
}
inline std::string BoxText(const std::string& name, const std::string& mate, long long amount, int growth, int alive, int setoOrBigBo)
{
    return BoxTextWhere(name, "beside " + mate, amount, growth, alive, setoOrBigBo);
}
inline std::string BoxTextCamera(const std::string& name, long long amount, int growth, int alive, int setoOrBigBo)
{
    return BoxTextWhere(name, "where your camera is looking", amount, growth, alive, setoOrBigBo);
}

/* ---- while it happens, after, and the refusals ---- */
inline std::string ProgressLine(const std::string& name) { return "Bringing " + name + " back..."; }
inline std::string DoneLine(const std::string& name, const std::string& mate, long long paid)
{
    return name + " is back, beside " + mate + "." + (paid > 0 ? " Paid " + Money(paid) + "." : std::string());
}
const int kRefNone = 0;
const int kRefOff = 1;           /* the host has resurrection off */
const int kRefShort = 2;         /* not enough money (before the press, or spent while the box was open) */
const int kRefNoMate = 3;        /* no free living character to come back beside */
const int kRefEnemy = 4;         /* an enemy near the picked character */
const int kRefPriceChanged = 5;  /* the price moved while the box was open */
const int kRefMateGone = 6;      /* the picked character can no longer be picked (dead, captured, carried, knocked out ...) */
const int kRefNoWorld = 7;       /* no world-server link, or the world is loading */
const int kRefFailed = 8;        /* anything else on the road (the row gone, the engine did not make the character ...): nothing paid */
const int kRefNoSpot = 9;        /* the camera road: no safe spot where the camera is looking (its area not loaded here, no ground, by a
                                    building, a list unread, the camera's point unread); an enemy nearby never refuses there */
const int kRefAtLimit = 10;      /* the player faction's living characters are not below the game's character limit */
const int kRefCodes = 11;
inline const char* RefusalTag(int code)
{
    static const char* const t[kRefCodes] = { "none", "off", "short", "noMate", "enemy", "priceChanged", "mateGone", "noWorld", "failed",
                                             "noSpot", "atLimit" };   /* the camera road has no enemy refusal */
    return (code >= 0 && code < kRefCodes) ? t[code] : "?";
}
inline std::string RefusalWords(int code, const std::string& name, const std::string& mate, long long price, long long money)
{
    switch (code)
    {
    case kRefOff:          return "The host has turned off resurrection in this world.";
    case kRefShort:        return "You need " + Money(price) + " to bring back " + name + ". You have " + Money(money) + ".";
    case kRefNoMate:       return "You need a living character for " + name + " to come back beside.";
    case kRefEnemy:        return "Enemies are near " + mate + ".";
    case kRefPriceChanged: return "The price is now " + (price > 0 ? Money(price) : std::string("free")) + ".";
    case kRefMateGone:     return mate + " can't be picked any more.";
    case kRefNoWorld:      return "Couldn't reach the world. " + name + " was not brought back and nothing was paid.";
    case kRefFailed:       return name + " was not brought back and nothing was paid.";
    case kRefNoSpot:       return "There is no safe spot here. Move the camera and try again.";   /* the approved words */
    case kRefAtLimit:      return "You already have as many characters as you can lead.";
    default:               return std::string();
    }
}
/* the road's placement refusal (fallenwire.h kBeside*) as the tab says it */
inline int RefusalOfBeside(int besideCode) { return besideCode == swfallen::kBesideEnemyNear ? kRefEnemy : kRefMateGone; }

/* ---- the camera road: a player with no living character comes back where the camera is looking ----
   Offered only while this player has no living character at all (the people and animals of the player's faction this game
   drives); with one alive the tab is the squadmate road, as before. The window then shows no drop-down: BRING BACK makes the
   character in a new squad of the player's faction at a safe spot at the ground point the camera is looking at. The camera is
   the player's to move: this road never moves it. */
inline int CameraRoadOffered(int aliveOwn) { return aliveOwn == 0 ? 1 : 0; }
inline std::string BackLabel(const std::string& name) { return "BRING " + Upper(name) + " BACK"; }
inline std::string CameraRoadLine(const std::string& name) { return "None of your characters is alive. " + name + " comes back where your camera is looking."; }

/* ---- where the camera is looking ----
   The candidates: the camera's point itself first, then kSpotBearings spots on each ring round it, the nearest ring first. A
   spot is safe when its area is loaded on this game (the engine's active zones), the terrain gives a height there (the character
   is made standing on the ground at that height; only the terrain's height - no water level is read, so a spot in water is not
   refused), and no loaded building's position is within kSpotClearUnits of it on the ground -
   the game's cellars, labs and caves are buildings too, so the one rule keeps the spot out of buildings and out of anything
   underground. Characters nearby are not judged: the player chose where to look, so an enemy nearby never refuses a spot. The
   area list or a building list that could not be read refuses. The camera's own area must be loaded here, or no spot is taken.
   The first safe spot is taken. */
const int kSpotRingCount = 3;
const int kSpotBearings = 8;
const int kSpotCandidates = 1 + kSpotRingCount * kSpotBearings;   /* the point, then the rings */
inline float SpotRing(int k)
{
    static const float r[kSpotRingCount] = { 100.0f, 200.0f, 300.0f };   /* 10 / 20 / 30 m (10 units a metre) - a Guess, to tune */
    return (k >= 0 && k < kSpotRingCount) ? r[k] : 0.0f;
}
const float kSpotClearUnits = 300.0f;   /* 30 m from every building's position (its centre): the mod reads no building's size, so the margin is
                                           fixed and covers a building up to about 60 m across - a Guess, to tune */
struct SpotXZ { float x, z; SpotXZ() : x(0.0f), z(0.0f) {} SpotXZ(float a, float b) : x(a), z(b) {} };
inline std::vector<SpotXZ> SpotCandidates(float cx, float cz)
{
    std::vector<SpotXZ> out;
    out.push_back(SpotXZ(cx, cz));   /* the camera's point */
    const float twoPi = 6.28318531f;
    for (int k = 0; k < kSpotRingCount; ++k)
        for (int b = 0; b < kSpotBearings; ++b)
        {
            const float a = twoPi * (float)b / (float)kSpotBearings;
            out.push_back(SpotXZ(cx + SpotRing(k) * std::cos(a), cz + SpotRing(k) * std::sin(a)));
        }
    return out;
}
/* candidate i's ring (0 the camera's point, 1 the nearest ring) and its bearing on the ring (0 for the point) */
inline int SpotRingOf(int i) { return i <= 0 ? 0 : 1 + (i - 1) / kSpotBearings; }
inline int SpotBearingOf(int i) { return i <= 0 ? 0 : (i - 1) % kSpotBearings; }
/* 1 when no building in `buildings` stands within `clear` of the spot on the ground */
inline int SpotClear(const SpotXZ& s, const std::vector<SpotXZ>& buildings, float clear)
{
    for (size_t i = 0; i < buildings.size(); ++i)
    {
        const float dx = buildings[i].x - s.x, dz = buildings[i].z - s.z;
        if (dx * dx + dz * dz < clear * clear) return 0;
    }
    return 1;
}
struct SpotTry { int loaded, ground, clear; SpotTry() : loaded(0), ground(0), clear(0) {} };   /* no enemy field: never judged here */
const int kSpotOk = 0;
const int kSpotNotLoaded = 1;    /* the camera's own area is not loaded on this game */
const int kSpotNoGround = 2;     /* no loaded candidate gives a terrain height */
const int kSpotByBuilding = 3;   /* every one with ground is by a building */
const int kSpotListUnread = 4;   /* the loaded buildings, or the engine's list of active areas, could not all be read: no spot can be called clear */
const int kSpotNoCamera = 5;     /* the camera's point did not read */
const int kSpotNone = 6;         /* no candidates */
const int kSpotCodes = 7;
inline const char* SpotTag(int why)
{
    static const char* const t[kSpotCodes] = { "ok", "notLoaded", "noGround", "byBuilding", "listUnread", "noCamera", "none" };
    return (why >= 0 && why < kSpotCodes) ? t[why] : "?";
}
/* the first safe try (loaded, ground, clear), -1 none with *why the reason. tries[0] is the camera's point: its area not
   loaded refuses every candidate. buildingsWhole 0: the building list was cut short or a position did not read, so a spot with
   ground is refused as kSpotListUnread instead of being called clear. areasRead 0: the engine's list of active areas did not
   read, so no candidate's area can be judged loaded - refused as kSpotListUnread, not as an area not loaded. */
inline int FirstSafeSpot(const std::vector<SpotTry>& tries, int buildingsWhole, int* why, int areasRead = 1)
{
    if (tries.empty()) { *why = kSpotNone; return -1; }
    if (!areasRead) { *why = kSpotListUnread; return -1; }
    if (!tries[0].loaded) { *why = kSpotNotLoaded; return -1; }
    int anyGround = 0;
    for (size_t i = 0; i < tries.size(); ++i)
    {
        if (!tries[i].loaded || !tries[i].ground) continue;
        anyGround = 1;
        if (!buildingsWhole || !tries[i].clear) continue;
        *why = kSpotOk; return (int)i;   /* loaded, ground, clear of buildings: taken */
    }
    *why = !anyGround ? kSpotNoGround : !buildingsWhole ? kSpotListUnread : kSpotByBuilding;
    return -1;
}

/* ---- the game's character limit ----
   The engine's hire step refuses when the player faction's living characters are not below the game's character limit (a
   global int). BRING BACK is refused before anything is made, the button greyed. read 0: the limit did not read (no table row,
   a fault) or reads 0 or less: not judged here (-1). 1 at the limit, 0 below. */
inline int AtCharacterLimit(int read, int limit, int alive)
{
    if (!read || limit <= 0) return -1;
    return alive >= limit ? 1 : 0;
}
/* TEST-ONLY (`resurrect cap <n>`, T-556): the limit BRING BACK compares with in this game. cap 0: the game's own, as read;
   cap > 0: the smaller of cap and the game's limit, or cap alone when the game's limit did not read or reads 0 or less. It
   only ever lowers the limit; the game's own hire step is not touched. Returns 1 a limit to judge by (*out), 0 none. */
inline int LimitForRefusal(int read, int limit, int cap, int* out)
{
    if (cap > 0 && (!read || limit <= 0 || cap < limit)) { *out = cap; return 1; }
    *out = limit;
    return read ? 1 : 0;
}

/* ---- the drop-down, the press and the confirm ---- */
/* 1: the drop-down must be emptied and filled again (the camera road, which shows none, is offered now, or the other way) */
inline int DropDownSwitched(int shownCameraRoad, int offered) { return (shownCameraRoad != 0) != (offered != 0) ? 1 : 0; }
/* the entry BRING BACK asks about: none on the camera road (0), else the picked squadmate (-1 none) */
inline int PressPick(int cameraRoad, int mateIndex) { return cameraRoad ? 0 : mateIndex; }
const int kConfirmNone = 0;     /* nothing starts: no row, or nothing picked */
const int kConfirmBeside = 1;   /* the squadmate road: runs on the next frame */
const int kConfirmCamera = 2;   /* the camera road: runs on the next frame, where the camera is looking then */
inline int ConfirmRoad(unsigned int dead, unsigned int mate, int cameraRoad)
{
    if (dead == 0) return kConfirmNone;
    if (cameraRoad) return kConfirmCamera;
    return mate != 0 ? kConfirmBeside : kConfirmNone;
}

/* ---- which state the tab is in ---- */
const int kCapNone = 0;      /* the caption line is empty */
const int kCapSelect = 1;    /* SelectLine() */
const int kCapRefusal = 2;   /* RefusalWords(refusal, ...) */
const int kCapBusy = 3;      /* ProgressLine() */
const int kCapCamera = 4;    /* CameraRoadLine() */
struct ViewIn
{
    int on, rows, selected, mates, busy, note;   /* note: the refusal the last press left (kRefNone none) */
    long long price, money;
    int moneyRead;
    int boxUp;                                    /* the confirm box is open */
    int cameraRoad;                               /* no living character of this player: the camera road (CameraRoadOffered) */
    int atLimit;                                  /* 1: the faction is at the game's character limit (AtCharacterLimit) */
    ViewIn() : on(0), rows(0), selected(0), mates(0), busy(0), note(kRefNone), price(0), money(0), moneyRead(0), boxUp(0), cameraRoad(0), atLimit(0) {}
};
struct View
{
    int priceShown, emptyShown, pickShown, dropShown, pickEnabled, btnEnabled, caption, refusal;   /* pickShown: the label and the button; dropShown: the drop-down */
    View() : priceShown(0), emptyShown(0), pickShown(0), dropShown(0), pickEnabled(0), btnEnabled(0), caption(kCapNone), refusal(kRefNone) {}
};
inline View ViewFor(const ViewIn& in)
{
    View v;
    v.priceShown = in.on ? 1 : 0;
    v.emptyShown = in.rows == 0 ? 1 : 0;
    v.pickShown = in.selected ? 1 : 0;
    v.dropShown = (in.selected && !in.cameraRoad) ? 1 : 0;   /* the camera road has no drop-down */
    if (!in.on) { v.caption = kCapRefusal; v.refusal = kRefOff; return v; }
    if (!in.selected) { v.caption = in.rows > 0 ? kCapSelect : kCapNone; return v; }
    if (!in.cameraRoad && in.mates == 0) { v.caption = kCapRefusal; v.refusal = kRefNoMate; return v; }
    if (in.busy) { v.caption = kCapBusy; return v; }
    v.pickEnabled = 1;
    /* at the character limit: the button greyed (not while the box is open - BRING BACK's road refuses then) */
    if (in.atLimit == 1 && !in.boxUp) { v.caption = kCapRefusal; v.refusal = kRefAtLimit; return v; }
    /* short money: the button greyed - except while the box is open, when the sentence waits for BRING BACK (the page) */
    if (in.moneyRead && in.money < in.price && !in.boxUp) { v.caption = kCapRefusal; v.refusal = kRefShort; return v; }
    v.btnEnabled = 1;
    if (in.note != kRefNone) { v.caption = kCapRefusal; v.refusal = in.note; }
    else if (in.cameraRoad) v.caption = kCapCamera;
    return v;
}

/* ---- the TEST lever: `resurrect tab open | select <row> | mate <i> | press | confirm | cancel | report` ---- */
const int kLeverBad = 0, kLeverOpen = 1, kLeverSelect = 2, kLeverMate = 3, kLeverPress = 4, kLeverConfirm = 5, kLeverCancel = 6, kLeverReport = 7;
struct Lever { int kind; int n; Lever() : kind(kLeverBad), n(-1) {} };
inline Lever ParseLever(const std::string& args)
{
    std::istringstream is(args);
    std::string w, extra;
    Lever l;
    if (!(is >> w)) { l.kind = kLeverReport; return l; }
    if (w == "select" || w == "mate")
    {
        int n = -1;
        if (!(is >> n) || n < 0 || (is >> extra)) return l;
        l.kind = w == "select" ? kLeverSelect : kLeverMate; l.n = n;
        return l;
    }
    if (is >> extra) return l;
    if (w == "open") l.kind = kLeverOpen;
    else if (w == "press") l.kind = kLeverPress;
    else if (w == "confirm") l.kind = kLeverConfirm;
    else if (w == "cancel") l.kind = kLeverCancel;
    else if (w == "report") l.kind = kLeverReport;
    return l;
}

} /* namespace swtab */
