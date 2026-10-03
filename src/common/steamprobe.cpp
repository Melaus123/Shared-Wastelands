/* src/common/steamprobe.cpp - T-290 S0. See steamprobe.h. PURE; C++03 (VS2010 v100). */
#include "steamprobe.h"

#include <cstring>
#include <sstream>
#include <locale>

namespace coopsteam {

const char* AvailWord(int avail)
{
    switch (avail)
    {
    case kAvailCannotTry:  return "cannot-try";
    case kAvailFailed:     return "failed";
    case kAvailPreviously: return "previously";
    case kAvailRetrying:   return "retrying";
    case kAvailUnknown:    return "unknown";
    case kAvailNeverTried: return "never-tried";
    case kAvailWaiting:    return "waiting";
    case kAvailAttempting: return "attempting";
    case kAvailCurrent:    return "current";
    default:               return "other";
    }
}

const char* RelayOutcomeWord(int outcome)
{
    switch (outcome)
    {
    case RELAY_AVAILABLE:   return "available";
    case RELAY_UNAVAILABLE: return "unavailable";
    case RELAY_FAILED:      return "failed";
    default:                return "waiting";
    }
}

int RelayDecide(int avail, long long elapsedMs, long long capMs)
{
    /* A settled status is the answer whenever it arrives, cap or no cap. */
    if (avail == kAvailCurrent)
        return RELAY_AVAILABLE;
    if (avail == kAvailCannotTry)
        return RELAY_UNAVAILABLE;
    if (avail == kAvailFailed || avail == kAvailPreviously)
        return RELAY_FAILED;
    /* retrying / never-tried / waiting / attempting / unknown / anything else: still moving. The cap is wall time. */
    if (elapsedMs >= capMs)
        return RELAY_UNAVAILABLE;
    return RELAY_WAIT;
}

bool RelayWantsPump(int avail, long long elapsedMs, long long capMs, long long afterMs)
{
    return RelayDecide(avail, elapsedMs, capMs) == RELAY_WAIT && elapsedMs >= afterMs;
}

std::string RelayLine(int outcome, int avail, int availConfig, int availAnyRelay, long long polls, long long elapsedMs,
                      long long pumpFromMs, const std::string& msg)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[NET] relay: " << RelayOutcomeWord(outcome) << " ";
    if (outcome == RELAY_UNAVAILABLE && avail != kAvailCannotTry)
        ss << "still-" << AvailWord(avail);   /* the wait cap ran out */
    else
        ss << "status=" << AvailWord(avail);
    ss << " (config=" << AvailWord(availConfig) << " anyRelay=" << AvailWord(availAnyRelay)
       << ", " << polls << " polls, " << elapsedMs << " ms, ";
    if (pumpFromMs >= 0)
        ss << "callbacks pumped from " << pumpFromMs << " ms)";
    else
        ss << "callbacks not pumped)";
    if (!msg.empty())
        ss << " - steam says: " << msg;
    return ss.str();
}

std::string RelayPumpLine(int avail, int availConfig, int availAnyRelay, long long polls, long long elapsedMs,
                          bool exported, const std::string& msg)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << "[NET] relay: still-" << AvailWord(avail) << " after " << elapsedMs << " ms (config=" << AvailWord(availConfig)
       << " anyRelay=" << AvailWord(availAnyRelay) << ", " << polls << " polls) - ";
    if (exported)
        ss << "pumping SteamAPI_RunCallbacks once per frame until it settles";
    else
        ss << "SteamAPI_RunCallbacks not exported, not pumping; still polling";
    if (!msg.empty())
        ss << " - steam says: " << msg;
    return ss.str();
}

bool FindFixedFileVersion(const unsigned char* blob, size_t len, unsigned int* versionMs, unsigned int* versionLs)
{
    if (blob == 0 || len < 16)
        return false;
    size_t off;
    for (off = 0; off + 16 <= len; off += 4)
    {
        unsigned int sig = (unsigned int)blob[off] | ((unsigned int)blob[off + 1] << 8)
                         | ((unsigned int)blob[off + 2] << 16) | ((unsigned int)blob[off + 3] << 24);
        if (sig != 0xFEEF04BDu)
            continue;
        /* dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS (little-endian DWORDs) */
        unsigned int ms = 0, ls = 0;
        int i;
        for (i = 3; i >= 0; --i) ms = (ms << 8) | blob[off + 8 + i];
        for (i = 3; i >= 0; --i) ls = (ls << 8) | blob[off + 12 + i];
        if (versionMs) *versionMs = ms;
        if (versionLs) *versionLs = ls;
        return true;
    }
    return false;
}

std::string FileVersionText(unsigned int versionMs, unsigned int versionLs)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << (versionMs >> 16) << "." << (versionMs & 65535u) << "." << (versionLs >> 16) << "." << (versionLs & 65535u);
    return ss.str();
}

std::string CleanDebugText(const char* buf, size_t cap, size_t maxOut)
{
    std::string out;
    if (buf == 0)
        return out;
    size_t i;
    for (i = 0; i < cap && buf[i] != 0; ++i)
    {
        unsigned char c = (unsigned char)buf[i];
        char k = (c < 32 || c > 126) ? ' ' : (char)c;
        if (k == ' ' && (out.empty() || out[out.size() - 1] == ' '))
            continue;
        out += k;
    }
    while (!out.empty() && out[out.size() - 1] == ' ')
        out.erase(out.size() - 1);
    if (out.size() > maxOut)
    {
        if (maxOut <= 3)
            out = out.substr(0, maxOut);
        else
            out = out.substr(0, maxOut - 3) + "...";
    }
    return out;
}

}   /* namespace coopsteam */
