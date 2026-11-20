/* T-1360 (F1082) LOG ONLY: the lines that show what a carried, knocked-out copy looks like when its carry ends here, and what
   the owner says on each STATE for a standing, uncarried copy the owner calls unconscious. Pure: no engine reads, no state of
   its own - the plugin reads the values and keeps the counters; this file shapes the text and decides the per-launch cap. */
#ifndef SW_CARRYSTATELOG_H
#define SW_CARRYSTATELOG_H

#include <cstdio>
#include <string>

namespace swcarrystate {

const long long kDropLineCap = 20;     /* [K1] carry drop state lines per launch */
const long long kUncStandLineCap = 20; /* [K1] owner-unconscious STATE lines per launch */

/* One more line allowed? Counts every call in *seen; *logged only when the line is let through. */
inline bool Take(long long* seen, long long* logged, long long cap)
{
    ++*seen;
    if (*logged >= cap) return false;
    ++*logged;
    return true;
}

inline std::string U(unsigned int v) { char b[16]; std::sprintf(b, "%u", v); return b; }

/* The copy's lying state: pose (prone), the unconscious flag, the wake timer, the engine's inRagdoll(). -1 = unreadable;
   `lying` is the plugin's own "prone=.. unc=.. koT=.." text (spawn.cpp CarryLyingText). */
inline std::string Lying(const std::string& lying, int ragdoll)
{
    return lying + " ragdoll=" + (ragdoll < 0 ? std::string("-1") : (ragdoll ? std::string("1") : std::string("0")));
}

/* At a carry drop (the body's game let go): the copy's state just before and just after this game's drop call. */
inline std::string DropLine(unsigned int carrier, unsigned int body, const std::string& before, const std::string& after,
                            long long n)
{
    return "[K1] carry drop state #" + U((unsigned int)n) + " carrier uid=" + U(carrier) + " body uid=" + U(body)
         + " before(" + before + ") after(" + after + ") (log only, T-1360)";
}

/* A STATE for a standing, uncarried copy that its owner says is unconscious: the owner's word, then what the copy reads. */
inline std::string UncStandLine(unsigned int uid, int ownerProne, bool ownerUnc, float ownerKoT, unsigned int ownerCarries,
                                const std::string& copyNow, long long n)
{
    char b[96];
    std::sprintf(b, "prone=%d unc=%d koT=%.1f carrying=%u", ownerProne, ownerUnc ? 1 : 0, (double)ownerKoT, ownerCarries);
    return "[K1] owner says unconscious, copy standing #" + U((unsigned int)n) + " uid=" + U(uid) + " owner(" + b
         + ") copy(" + copyNow + ") (log only, T-1360)";
}

/* The REPORT token: lines written / cases seen for both lines. */
inline std::string ReportToken(long long dropLogged, long long dropSeen, long long uncLogged, long long uncSeen)
{
    char b[160];
    std::sprintf(b, " t1360[dropLogged,dropSeen,uncStandLogged,uncStandSeen]=%lld,%lld,%lld,%lld", dropLogged, dropSeen,
                 uncLogged, uncSeen);
    return b;
}

}   /* namespace swcarrystate */

#endif
