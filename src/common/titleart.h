// titleart.h - T-513 (owner 446/447): the mod's own art on Kenshi's title screen - which part of it fills the screen, where the
// menu column stands on it, and where the title note sits on it. Pure: no MyGUI, no Ogre. src/coop-plugin/titleart.cpp shows
// the art, src/coop-plugin/ui.cpp places the column, src/coop-plugin/bugreport.cpp places the note, src/coop-test/test_main.cpp
// tests the rules.
#pragma once

namespace swtitle {

/* The art's file, in the mod folder (tools/build-package.ps1 ships it). Kept here, not in names.h: the world server includes
   names.h and has no use for it. */
const char* const kTitleArtFile = "title-background.png";

/* Where things are in the art (mod-package/Shared Wastelands/title-background.png, 1672x941), as fractions of its height, read
   from the image: the painted title (SHARED WASTELANDS / MULTIPLAYER FOR KENSHI, top centre) ends 0.225 down; the four figures
   on the rock (bottom right, from 0.68 across) start 0.54 down at the tallest's hat. The band between the two is open sky,
   haze and the far horizon. */
const double kTitleBottom = 0.235;   /* a little below the subtitle's letters */
const double kFiguresTop  = 0.53;    /* a little above the figures' heads, staffs and horns */

struct Rect { int x, y, w, h; };

/* COVER: the part of a texW x texH texture that fills a boxW x boxH box with the art's shape kept and no empty edge. A box
   narrower than the art shows the art's whole height and the middle of its width (the same amount cut from each side); a
   box wider than the art shows its whole width and its TOP part (the bottom is cut, so the painted title always shows).
   All zero when a size is not positive. */
inline Rect CoverCrop(int texW, int texH, int boxW, int boxH)
{
    Rect r = { 0, 0, 0, 0 };
    if (texW <= 0 || texH <= 0 || boxW <= 0 || boxH <= 0) return r;
    if ((long long)texW * boxH > (long long)boxW * texH)
    {
        long long w = ((long long)texH * boxW * 2 + boxH) / (2LL * boxH);   /* texH * boxW / boxH, rounded */
        if (w < 1) w = 1;
        if (w > texW) w = texW;
        r.w = (int)w;
        r.h = texH;
        r.x = (texW - r.w) / 2;
    }
    else
    {
        long long h = ((long long)texW * boxH * 2 + boxW) / (2LL * boxW);   /* texW * boxH / boxW, rounded */
        if (h < 1) h = 1;
        if (h > texH) h = texH;
        r.w = texW;
        r.h = (int)h;
    }
    return r;
}

/* The box row (0 = the box's top) where the art's row `frac` of its height (0..1) shows, with crop `c` of a texture texH tall
   drawn over a box boxH tall. */
inline int ArtRowOnBox(double frac, int texH, const Rect& c, int boxH)
{
    if (c.h <= 0 || boxH <= 0) return 0;
    const double y = (frac * texH - c.y) * boxH / c.h;
    return (int)(y + (y >= 0 ? 0.5 : -0.5));
}

/* THE MENU COLUMN ON THE MOD'S ART (owner 464 a), as fractions of the screen height: the first shown button's top sits a gap
   under the painted title's bottom row, and the last button's bottom no lower than kMenuBottom; two buttons are never closer
   than kMenuMinGap. */
const double kMenuUnderTitle = 0.02;
const double kMenuBottom     = 0.97;
const double kMenuMinGap     = 0.012;

/* Where the menu column's buttons go on a title screen boxH tall whose painted title ends on row titleBottom (ArtRowOnBox(
   kTitleBottom, ...)), for `shown` buttons in a column whose game rows are gamePitch apart and gameHeight tall. The first shown
   button's top = titleBottom + 2% of the height; each next one is `pitch` lower; pitch = the game's own, or less when the
   buttons would otherwise end below 97% of the height; every button keeps the game's height unless the gap between two would
   fall under 1.2% of the height, and is then pitch - 1.2% tall. ok = 0 when a number is not positive or the buttons would be
   under half the game's height (a screen too wide for the column under the title) - the caller then keeps the game's own
   column. */
struct MenuColumn { int ok, top, pitch, height; };
inline MenuColumn MenuColumnPlan(int boxH, int titleBottom, int shown, int gamePitch, int gameHeight)
{
    MenuColumn m = { 0, 0, 0, 0 };
    if (boxH <= 0 || titleBottom < 0 || shown <= 0 || gamePitch <= 0 || gameHeight <= 0) return m;
    const int top    = titleBottom + (int)(boxH * kMenuUnderTitle + 0.5);
    const int last   = (int)(boxH * kMenuBottom + 0.5);    /* no button's bottom below this row */
    const int minGap = (int)(boxH * kMenuMinGap + 0.5);
    int pitch = gamePitch, height = gameHeight;
    if (shown > 1)
    {
        const int room = last - top - height;
        const int fit = room >= 0 ? room / (shown - 1) : -1;
        if (fit < pitch) pitch = fit;
        if (pitch - height < minGap) height = pitch - minGap;
    }
    if (pitch <= 0 || height <= 0 || height * 2 < gameHeight || top + (shown - 1) * pitch + height > last) return m;
    m.ok = 1;
    m.top = top;
    m.pitch = pitch;
    m.height = height;
    return m;
}

/* THE NOTE'S PLACE (owner 425 / T-513): a w x h note on a pw x ph title screen. It never covers the menu column or the REPORT A
   BUG button and always stays on screen: its left edge at least two margins right of the column's right edge (columnRight),
   its right edge a margin inside the screen, its rows inside [areaTop, areaBottom] and a margin inside the screen, and a margin
   clear of the button's rectangle (btn). Within that it goes as near as it can to (preferX, preferY). ok = 0 when no such
   place exists - the caller then tries a smaller font, and shows no note rather than one over a button. */
struct NoteFit { int ok, x, y; };
inline NoteFit PlaceNote(int pw, int ph, int w, int h, int margin, int columnRight, const Rect& btn,
                         int areaTop, int areaBottom, int preferX, int preferY)
{
    NoteFit f = { 0, 0, 0 };
    if (w <= 0 || h <= 0 || pw <= 0 || ph <= 0) return f;
    const int xLo = columnRight + 2 * margin, xHi = pw - margin - w;
    const int yLo = areaTop > margin ? areaTop : margin;
    const int yHi = (areaBottom < ph - margin ? areaBottom : ph - margin) - h;
    if (xHi < xLo || yHi < yLo) return f;
    f.x = preferX < xLo ? xLo : (preferX > xHi ? xHi : preferX);
    f.y = preferY < yLo ? yLo : (preferY > yHi ? yHi : preferY);
    if (btn.w > 0 && btn.h > 0
        && f.x < btn.x + btn.w + margin && btn.x - margin < f.x + w
        && f.y < btn.y + btn.h + margin && btn.y - margin < f.y + h) return f;   /* over the button: no place */
    f.ok = 1;
    return f;
}

/* Where the note goes on each art. modArt 1 (titleart.cpp drew the mod's art): in the open band under the painted title and
   above the figures (bandTop..bandBottom), as near the column as it may be and a margin under the title - the far horizon
   beside the column, not the towns at the edges. modArt 0 (Kenshi's own art): the right side, a little below the middle
   (0.58 down), above the version text (verTop). Both through PlaceNote, so neither ever covers the column or the button. */
inline NoteFit PlaceTitleNote(int modArt, int pw, int ph, int w, int h, int margin, int columnRight, const Rect& btn,
                              int bandTop, int bandBottom, int verTop)
{
    if (modArt)
        return PlaceNote(pw, ph, w, h, margin, columnRight, btn, bandTop + margin, bandBottom - margin, columnRight + 2 * margin, bandTop + margin);
    return PlaceNote(pw, ph, w, h, margin, columnRight, btn, margin, verTop - margin, pw - w - 2 * margin, ph * 58 / 100);
}

}   // namespace swtitle
