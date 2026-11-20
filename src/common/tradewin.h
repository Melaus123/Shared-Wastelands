// tradewin.h - T-1167 (owner 1633 / 1634 / 1635): the player trade window's words, exactly the approved mock-up's (decision 1635):
//     +---------------------------- TRADE ----------------------------+
//     |  SAM (you)                     |  ANA (Bo)                    |
//     |  ...                           |  ...                         |
//     |  Money: c. 500_  (digits only) |  Money: c.120                |
//     |  ACCEPTED                      |  waiting...                  |
//     |            ( ACCEPT )      ( CANCEL )                         |
//     +---------------------------------------------------------------+
// A pane's head is the character's name in capitals, then "(you)" for this player's own side or "(<player>)" - the other player's
// name as the faction screens say it (team.cpp TeamPlayerName) - for theirs; a player whose name this game does not know gets no
// brackets (no made-up name). PURE: no engine, no plugin types. The window itself: src/coop-plugin/tradewin.cpp.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "moneywire.h"   /* T-1167 piece 2: the GIVE MONEY box's amount words */
#include "tradewire.h"   /* T-1167 piece 2: the money box's check (AmountCheck) */
#include "tradelock.h"   /* T-1167 v3: the exact rows (tradel::ExRow) */
#include "chatlink.h"    /* T-1167 v3: the game's own quality word (QualityWord) for a row's grade */
namespace swtrade {
inline const char* Title()        { return "TRADE"; }
inline const char* AcceptWord()   { return "ACCEPT"; }
inline const char* AcceptedWord() { return "ACCEPTED"; }
inline const char* CancelWord()   { return "CANCEL"; }
inline const char* WaitingWord()  { return "waiting..."; }
inline const char* OutOfRange()   { return "Out of trading range"; }   /* the game's own words for the same refusal */
inline const char* MoneyWord()    { return "Money: c."; }              /* this player's side: the digits-only box follows it */
inline std::string Upper(const std::string& s)
{
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'a' && o[i] <= 'z') o[i] = (char)(o[i] - 'a' + 'A');
    return o;
}
inline std::string OwnHead(const std::string& name) { return Upper(name) + " (you)"; }
inline std::string TheirHead(const std::string& name, const std::string& player)
{
    return player.empty() ? Upper(name) : Upper(name) + " (" + player + ")";
}
/* the other side's money line, "Money: c.<n>" (a negative amount reads 0) */
inline std::string MoneyLine(long long n)
{
    char b[32];
    std::sprintf(b, "%lld", n < 0 ? 0LL : n);
    return std::string(MoneyWord()) + b;
}
/* the status line under each pane: a side that accepted reads ACCEPTED; the other side reads "waiting..." while only this player
   has accepted; otherwise the line is empty */
inline void StatusLines(bool ownAccepted, bool theirAccepted, std::string* own, std::string* theirs)
{
    *own = ownAccepted ? AcceptedWord() : "";
    *theirs = theirAccepted ? AcceptedWord() : (ownAccepted ? WaitingWord() : "");
}
/* T-1167 piece 2: the line under the other side's pane when their offer changed (both accepts were cleared): "Bo changed the offer."
   - the other player's name; a player whose name this game does not know is named by the character (the pane's head) */
inline std::string ChangedLine(const std::string& who) { return who + " changed the offer."; }
/* the other side's line: ACCEPTED / "waiting..." as StatusLines; with neither side accepted, the changed line after their offer
   changed; else empty */
inline std::string TheirLine(bool ownAccepted, bool theirAccepted, bool changed, const std::string& who)
{
    std::string own, theirs;
    StatusLines(ownAccepted, theirAccepted, &own, &theirs);
    if (theirs.empty() && changed && !who.empty()) return ChangedLine(who);
    return theirs;
}
/* the hint beside this player's money box (the GIVE MONEY box's words, moneywire.h): "(digits only)" while the amount is fine,
   "You don't have that much." over the purse, "Type an amount." for what the box cannot hold as an amount (tradew::AmountCheck) */
/* T-1167 piece 3 build 3 (owner 1637 / 1638): no '(digits only)' hint - an amount that is fine shows nothing */
inline const char* AmountHint(int amountCheck)
{
    if (amountCheck == tradew::kAmtTooMuch) return moneyw::RefusedHint(0, moneyw::kAmtTooMuch);
    if (amountCheck == tradew::kAmtBad) return moneyw::RefusedHint(0, moneyw::kAmtEmpty);
    return "";
}
/* T-1167 piece 3 build 3 (owner 1638 / 1642): an offered row reads as the approved mock-up's - the count in front, "2 x Bolts"; a count
   of 1 is the bare name ("Crossbow"); no count column */
inline std::string RowText(const std::string& name, unsigned count)
{
    if (count == 1) return name;
    char b[24];
    std::sprintf(b, "%u", count);
    return std::string(b) + " x " + name;
}
/* a list read as a sentence: "a", "a and b", "a, b and c"; none -> "nothing" */
inline std::string JoinAnd(const std::vector<std::string>& v)
{
    if (v.empty()) return "nothing";
    std::string s = v[0];
    for (size_t i = 1; i < v.size(); ++i) s += (i + 1 == v.size() ? " and " : ", ") + v[i];
    return s;
}
/* one side's goods as the chat line names them: its rows (RowText) and then its money "c.500" */
inline std::string GoodsText(const tradew::Offer& o)
{
    std::vector<std::string> v;
    for (size_t i = 0; i < o.items.size(); ++i) v.push_back(RowText(o.items[i].name, o.items[i].count));
    if (o.cats > 0) v.push_back(moneyw::Cats((long long)o.cats));
    return JoinAnd(v);
}
/* THE CHAT LINE after the trade (owner 1635): each side's own view - "You traded Crossbow, 2 x Bolts and c.500 to Bo for 3 x Dried Meat
   and c.120." (who: the other player's name, else the character's) */
inline std::string TradedLine(const tradew::Offer& mine, const std::string& who, const tradew::Offer& theirs)
{
    return "You traded " + GoodsText(mine) + " to " + who + " for " + GoodsText(theirs) + ".";
}
/* THE NO-ROOM LINE (owner 1641, mock-up 1642): under the pane of the side that has no room, on both games - "No room for Crossbow,
   2 x Bolts." - the rows of the offer that did not fit (indexes into `giving`, the offer of the OTHER side), as the offer shows them,
   joined with ", "; an index past the offer is left out; none known -> "No room." */
inline std::string NoRoomLine(const tradew::Offer& giving, const std::vector<unsigned>& rows)
{
    std::string s;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i] >= giving.items.size()) continue;
        s += (s.empty() ? "" : ", ") + RowText(giving.items[rows[i]].name, giving.items[rows[i]].count);
    }
    return s.empty() ? std::string("No room.") : "No room for " + s + ".";
}
/* T-1167 PIECE 3 BUILD 4 (owner 1638 / 1639, mock-up 1642): THE DRAG. The trading character's own inventory window opens beside the
   TRADE window (the shop's way); an item dragged from it and let go over this player's offer pane is offered - a plain drag one unit,
   the shop's modifier-drag the whole stack (the count the engine put on the cursor) - and goes straight back to its square: the item
   never leaves the inventory until the lock lifts it (design A). The engine runs its release-over-no-inventory-window code (1.0.65
   0x712AD0) for that let-go; the mod's hook asks DropDecide first. */
const int kDropEngine = 0;   /* not ours: the engine's own release runs unchanged */
const int kDropOffer  = 1;   /* offered, and the item put back on its square */
const int kDropBack   = 2;   /* put back on its square, nothing offered (the lock is open, or the item's identity did not read) */
/* the release point inside a widget's screen rectangle (left / top inclusive, right / bottom exclusive; an empty one holds nothing) */
inline bool InRect(long long x, long long y, long long left, long long top, long long w, long long h)
{
    return w > 0 && h > 0 && x >= left && y >= top && x < left + w && y < top + h;
}
/* windowOpen: the TRADE window and this player's offer pane are up and a trade is set; overWindow: the release point is over the TRADE
   window; inPane: inside this player's offer pane; fromMine: the cursor item came out of the trading character's own window; itemRead:
   its identity and count read; locking: the window takes no change (READY, a lock waiting or open, a decision being settled).
   T-1167 piece 3 fold 1 (the manager's put-back, review p3b LOW): ANY item let go over the TRADE window that is not offered - outside
   this player's pane, from another character's window, from the trader's backpack window, while the lock is open - goes back to where
   it came from (kDropBack: the engine's own MouseReturnItem), never to the ground; only a let-go outside the TRADE window is the
   engine's own release. */
inline int DropDecide(bool windowOpen, bool overWindow, bool inPane, bool fromMine, bool itemRead, bool locking)
{
    if (!windowOpen || !overWindow) return kDropEngine;
    if (!inPane || !fromMine || locking || !itemRead) return kDropBack;
    return kDropOffer;
}
/* T-1167 (owner's hands-on 2026-11-17: offering from a worn backpack did nothing): WHICH SOURCE WINDOWS COUNT. `shown` is the
   inventory the source window shows (read from the window and asked back both ways - `read`), `own` the trading character's own
   inventory, `packs` the inventories of the packs it carries (a worn backpack, a pack among its items). The character's own window
   (kSrcOwn) and the window of a pack it carries (kSrcPack, *at its index) count - as the shop lets a trader sell from a carried pack;
   another character's window or pack, a box, the ground or a window that did not read never count (kSrcNone). */
const int kSrcNone = 0;
const int kSrcOwn  = 1;
const int kSrcPack = 2;
inline int SrcWindow(bool read, const void* shown, const void* own, const std::vector<const void*>& packs, size_t* at)
{
    *at = 0;
    if (!read || shown == 0 || own == 0) return kSrcNone;
    if (shown == own) return kSrcOwn;
    for (size_t i = 0; i < packs.size(); ++i) if (packs[i] != 0 && packs[i] == shown) { *at = i; return kSrcPack; }
    return kSrcNone;
}
/* the units a drop offers: the count on the cursor (at least 1) */
inline unsigned DropUnits(int onCursor) { return onCursor > 0 ? (unsigned)onCursor : 1u; }
/* the TRADE window's left edge beside the inventory window [invLeft, invLeft + invW): to its right when it fits on the screen, else
   to its left, else 0 (the window stays where it is). 1 placed (*x set). */
inline int BesideX(int invLeft, int invW, int winW, int viewW, int gap, int* x)
{
    if (invW <= 0 || winW <= 0 || viewW <= 0 || invLeft < 0 || invLeft + invW > viewW) return 0;
    if (invLeft + invW + gap + winW <= viewW) { *x = invLeft + invW + gap; return 1; }
    if (invLeft - gap - winW >= 0) { *x = invLeft - gap - winW; return 1; }
    return 0;
}

/* ---- T-1167 v3 (owner 1677 / 1678, mock-up v3) ---- */
/* THE STATUS LINES (owner 2026-11-15): the green panel alone shows an accept - no "ACCEPTED", no "waiting..."; the only words under a
   side are the other side's "Bo changed the offer." (as before: neither side accepted, their offer changed) and the room check's line */
inline std::string OwnNote() { return std::string(); }
inline std::string TheirNote(bool ownAccepted, bool theirAccepted, bool changed, const std::string& who)
{
    return (!ownAccepted && !theirAccepted && changed && !who.empty()) ? ChangedLine(who) : std::string();
}
/* THE ROW'S NAME (mock-up v3 "Katana (Specialist)"): the engine's own name; when two rows share it, each is named with the game's own
   quality word (chatlink.h QualityWord - the game's 0x796300, by the item's level) where the game shows one (weapons and armour);
   still the same: the weapon's model (its material record's name) too; still the same: the level as a number */
inline std::string GradedName(const tradel::ExRow& r, int tier)
{
    const std::string& n = r.thing.name;
    if (tier <= 0) return n;
    std::string s;
    if (tier >= 2 && chatlink::ShowModel(r.type) && !r.model.empty()) s = r.model;
    if (chatlink::ShowQuality(r.type, r.level)) s += (s.empty() ? "" : ", ") + std::string(chatlink::QualityWord(r.level));
    if (tier >= 3 || s.empty()) { char b[24]; std::sprintf(b, "%d", r.level); s += (s.empty() ? "" : ", ") + std::string(b); }
    return n + " (" + s + ")";
}
inline std::vector<std::string> RowNames(const std::vector<tradel::ExRow>& rows)
{
    std::vector<int> tier(rows.size(), 0);
    std::vector<std::string> out(rows.size());
    for (int round = 0; round < 4; ++round)
    {
        for (size_t i = 0; i < rows.size(); ++i) out[i] = GradedName(rows[i], tier[i]);
        bool any = false;
        for (size_t i = 0; i < rows.size(); ++i)
            for (size_t k = 0; k < rows.size(); ++k)
                if (i != k && out[i] == out[k] && tier[i] < 3) { ++tier[i]; any = true; break; }
        if (!any) break;
    }
    for (size_t i = 0; i < out.size(); ++i) if (out[i].size() > tradew::kMaxName) out[i] = rows[i].thing.name;
    return out;
}
/* the offer's rows from the exact rows (the wire's form: thing, shown name, count) */
inline std::vector<tradew::Item> ExactItems(const std::vector<tradel::ExRow>& rows)
{
    const std::vector<std::string> names = RowNames(rows);
    std::vector<tradew::Item> v;
    for (size_t i = 0; i < rows.size(); ++i) { tradew::Item it = rows[i].thing; it.name = names[i]; it.count = tradel::ExCount(rows[i]); v.push_back(it); }
    return v;
}
/* THE RIGHT-CLICK (owner 1678): a right release on an item in the trading character's own inventory window while the TRADE window is
   open offers it - plain one unit, Shift or Ctrl the whole stack. kRightEngine: not ours, the engine's own right-click runs unchanged;
   kRightOffer: offered (*whole set); kRightTaken: ours but nothing offered (the lock is open, or the item did not read) - the engine's
   right-click does not run then. The engine's right-click (0x713E50) is more than a move: with another inventory window open it moves
   one unit (Shift: the stack) there; with none it USES some items on the spot (a map's locations learned - "location added to the map" -
   and two other kinds, decomp_713e50 ~:244-300). While trading, both are replaced by the offer for the trading character's own items
   (owner 1678). */
const int kRightEngine = 0;
const int kRightOffer  = 1;
const int kRightTaken  = 2;
inline int RightDecide(bool windowOpen, bool fromMine, bool itemFound, bool itemRead, bool locking, bool shift, bool ctrl, bool* whole)
{
    *whole = shift || ctrl;
    if (!windowOpen || !fromMine || !itemFound) return kRightEngine;
    if (locking || !itemRead) return kRightTaken;
    return kRightOffer;
}
/* T-1167 v3 fold 2: THE ENGINE'S SQUARE UNDER A POINT, as 0x713E50 finds it (the right-click's item). SecHit: 0x70DC90's test - the point
   in the section widget's absolute rectangle, edges in (decomp_70dc90:33-34). GridCell: 0x70FF10's sum - (point - grid corner) / slot
   size, C's truncating division, then clamped to [0, size - 1] on each axis (decomp_70ff10:68-94). */
inline bool SecHit(int x, int y, int l, int t, int w, int h)
{
    return (long long)x >= l && (long long)x <= (long long)l + w && (long long)y >= t && (long long)y <= (long long)t + h;
}
inline int GridCell(int mx, int my, int gl, int gt, int sw, int sh, int gw, int gh, int* cx, int* cy)
{
    *cx = 0; *cy = 0;
    if (sw <= 0 || sh <= 0 || gw <= 0 || gh <= 0) return 0;
    int x = (int)(((long long)mx - gl) / sw), y = (int)(((long long)my - gt) / sh);
    if (x > gw - 1) x = gw - 1;
    if (x < 0) x = 0;
    if (y > gh - 1) y = gh - 1;
    if (y < 0) y = 0;
    *cx = x; *cy = y;
    return 1;
}
/* T-1167 v3 (the fade, mock-up v3 "after a test", measured in the 2026-11-18 hands-on): THE SHADE OVER AN OFFERED ITEM - its cells on screen by GridCell's
   sum turned round: the grid corner (gl, gt: the section's own +0x30 / +0x34, decomp_70ff10:71-72) plus the item's cell (cx, cy) times
   the engine's slot size (sw, sh: DAT_142127440 / 444, the pickup's own globals), its size in cells (iw, ih) times the slot size - so
   every pixel of the box is a pixel GridCell names as one of the item's own cells. 1 placed; 0 a slot, grid or item size that is not
   positive or not plausible, or an item that is not wholly inside the grid (gw x gh cells). */
struct ShadeBox { int l, t, w, h; ShadeBox() : l(0), t(0), w(0), h(0) {} };
inline int ShadeRect(int gl, int gt, int sw, int sh, int gw, int gh, int cx, int cy, int iw, int ih, ShadeBox* out)
{
    *out = ShadeBox();
    if (sw <= 0 || sh <= 0 || gw <= 0 || gh <= 0 || iw <= 0 || ih <= 0 || sw > 512 || sh > 512 || gw > 256 || gh > 256) return 0;
    if (cx < 0 || cy < 0 || cx + iw > gw || cy + ih > gh) return 0;
    out->l = gl + cx * sw; out->t = gt + cy * sh; out->w = iw * sw; out->h = ih * sh;
    return 1;
}
/* the shade's look: mock-up v3's faded item (.it.faded { opacity: .35 }) - a near-black cover at 0.65 leaves the item at about a third
   of its strength over the window's dark cells. The engine has no per-item fade or disabled-item colour of its own to copy (T-1167). */
const float kShadeAlpha = 0.65f;
const float kShadeRed = 0.05f, kShadeGreen = 0.045f, kShadeBlue = 0.04f;
/* THE WINDOW'S SIZE (mock-up v3): about 440 x 320 at 1920 x 1080, scaled with the screen, never under the minimum, never past it */
const int kWinMinW = 380, kWinMinH = 260;
inline void DefaultSize(int vw, int vh, int* w, int* h)
{
    int a = vw * 440 / 1920, b = vh * 320 / 1080;
    if (a < kWinMinW) a = kWinMinW;
    if (b < kWinMinH) b = kWinMinH;
    if (vw > 32 && a > vw - 32) a = vw - 32 > kWinMinW ? vw - 32 : kWinMinW;
    if (vh > 32 && b > vh - 32) b = vh - 32 > kWinMinH ? vh - 32 : kWinMinH;
    *w = a; *h = b;
}
/* a resize (vw / vh: the room from the window's corner to the screen's right / bottom edge): the new size never under the minimum, never
   past the screen while there is any room (fold 1, review LOW-6: with room of the minimum or less the window stays at the minimum) */
inline void ClampSize(int vw, int vh, int* w, int* h)
{
    if (*w < kWinMinW) *w = kWinMinW;
    if (*h < kWinMinH) *h = kWinMinH;
    if (vw > 0 && *w > vw) *w = vw > kWinMinW ? vw : kWinMinW;
    if (vh > 0 && *h > vh) *h = vh > kWinMinH ? vh : kWinMinH;
}
/* THE PLACE OF EVERY WIDGET in a client area W x H (the window less its frame): per pane the head, the list (the most height), the money
   line and one status line; ACCEPT / CANCEL at the foot; the 18 px grip in the bottom-right corner. No help line (owner 2026-11-15). */
struct Box { int l, t, w, h; Box() : l(0), t(0), w(0), h(0) {} Box(int a, int b, int c, int d) : l(a), t(b), w(c), h(d) {} };
struct Places { Box head[2], list[2], money[2], status[2], accept, cancel, grip; int paneW, lineH; };
const int kGripPx = 18;
inline int PlaceAll(int W, int H, Places* p)
{
    const int pad = 6, lineH = 20, btnH = 26;
    int btnW = W / 4; if (btnW < 90) btnW = 90; if (btnW > 160) btnW = 160;
    const int paneW = (W - 3 * pad) / 2;
    const int headY = pad, listY = headY + lineH + 2;
    const int btnY = H - pad - btnH;
    const int statusY = btnY - 2 - lineH;
    const int moneyY = statusY - 2 - (lineH + 4);
    const int listH = moneyY - 4 - listY;
    p->paneW = paneW; p->lineH = lineH;
    for (int k = 0; k < 2; ++k)
    {
        const int x = pad + k * (paneW + pad);
        p->head[k] = Box(x, headY, paneW, lineH);
        p->list[k] = Box(x, listY, paneW, listH);
        p->money[k] = Box(x, moneyY + 2, paneW, lineH);
        p->status[k] = Box(x, statusY, paneW, lineH);
    }
    p->accept = Box(W / 2 - pad - btnW, btnY, btnW, btnH);
    p->cancel = Box(W / 2 + pad, btnY, btnW, btnH);
    p->grip = Box(W - kGripPx, H - kGripPx, kGripPx, kGripPx);
    return (paneW >= 140 && listH >= 60 && p->cancel.l + btnW <= W - kGripPx && p->accept.l >= 0) ? 1 : 0;
}
}
