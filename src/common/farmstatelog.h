/* T-1361 (F1083) LOG ONLY: a player farm's state as each game sees it - a [FARM] state line per player farm, at an event (the first
   look, the farm's writer changed, plants harvested changed, the crop started again) and otherwise every kEveryMs; capped per launch.
   Also the own cap of the [PIECEREC] crop write / lay lines (kept out of the shared [PIECEREC] cap). Pure: values in, text out. */
#ifndef SW_FARMSTATELOG_H
#define SW_FARMSTATELOG_H

#include <cstdio>
#include <string>

namespace swfarmstate {

const unsigned int kEveryMs = 180000u;   /* a player farm's line at least this often while it is loaded here (3 minutes) */
const long long kStateLineCap = 300;     /* [FARM] state lines per launch */
const long long kCropLineCap = 80;       /* [PIECEREC] farm crop write / lay lines per launch (their own cap) */
const unsigned int kKeysCap = 256u;      /* farms remembered for the event test; past it a farm's line is periodic only */

/* What one game reads of one player farm. -1 = unreadable / not known for the ints. */
struct Vals
{
    float grown, died, water;
    int harvested, physical, verdict, writerSlot, ownerSlot, mySlot, hasRec;
    unsigned int recGen;
    float recGrown;
    Vals() : grown(0.0f), died(0.0f), water(0.0f), harvested(0), physical(-1), verdict(0), writerSlot(-1), ownerSlot(-1), mySlot(-1),
             hasRec(0), recGen(0u), recGrown(0.0f) {}
};

/* The last line written for a farm: what the next look is measured against. */
struct Last { int ever; unsigned int at; int verdict; int harvested; float grown; Last() : ever(0), at(0u), verdict(0), harvested(0), grown(0.0f) {} };

enum { kNone = 0, kFirst = 1, kWriter = 2, kHarvest = 3, kReplant = 4, kPeriodic = 5 };

inline const char* WhyName(int w)
{
    switch (w)
    {
    case kFirst: return "first";
    case kWriter: return "writer-changed";
    case kHarvest: return "harvested";
    case kReplant: return "replanted";
    case kPeriodic: return "periodic";
    default: return "none";
    }
}

/* Is a line due for this farm now? The events first, then the period. A crop that fell by more than 0.05 started again. */
inline int Due(const Last& last, const Vals& v, unsigned int now)
{
    if (last.ever == 0) return kFirst;
    if (v.verdict != last.verdict) return kWriter;
    if (v.harvested != last.harvested) return kHarvest;
    if (v.grown + 0.05f < last.grown) return kReplant;
    if (now - last.at >= kEveryMs) return kPeriodic;
    return kNone;
}

inline void Book(Last* last, const Vals& v, unsigned int now)
{
    last->ever = 1; last->at = now; last->verdict = v.verdict; last->harvested = v.harvested; last->grown = v.grown;
}

/* One more line allowed? *seen counts every due line, *logged only the ones written. */
inline bool Take(long long* seen, long long* logged, long long cap)
{
    ++*seen;
    if (*logged >= cap) return false;
    ++*logged;
    return true;
}

inline const char* VerdictWord(int v) { return v == 1 ? "this-game" : (v == 2 ? "other-game" : "undecided"); }

inline std::string Line(const std::string& key, const Vals& v, int why, long long n)
{
    char b[420];
    std::sprintf(b, "[FARM] state #%lld (%s) key=%.90s grown=%.3f died=%.3f harvested=%d water=%.2f physical=%d writer=%s writerSlot=%d"
                 " owner=%d me=%d rec=%s gen=%u recGrown=%.3f (log only, T-1361)",
                 n, WhyName(why), key.c_str(), (double)v.grown, (double)v.died, v.harvested, (double)v.water, v.physical,
                 VerdictWord(v.verdict), v.writerSlot, v.ownerSlot, v.mySlot, v.hasRec ? "yes" : "no", v.recGen, (double)v.recGrown);
    return b;
}

/* The REPORT tokens: state lines written / due and farms tracked ([FARM] REPORT); crop lines written / held back by their own cap
   ([PIECEREC] REPORT). */
inline std::string StateToken(long long stateLogged, long long stateSeen, unsigned int keys)
{
    char b[160];
    std::sprintf(b, " t1361state[logged,due,farms]=%lld,%lld,%u", stateLogged, stateSeen, keys);
    return b;
}
inline std::string CropToken(long long cropLogged, long long cropQuiet)
{
    char b[120];
    std::sprintf(b, " t1361cropLines[said,quiet]=%lld,%lld", cropLogged, cropQuiet);
    return b;
}

}   /* namespace swfarmstate */

#endif
