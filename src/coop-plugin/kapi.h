#pragma once
/* kapi.h - THE Kenshi API, plugin side (skeleton, 2026-10-19).

   InstallKapi (coop.cpp's start, with the other installs) reads kenshi_api.cfg beside the DLL into the pure rule engine
   (src/common/kapirules.h), logs a [KAPI] summary, then walks the NAMED HOOK POINTS: each has an address-table key name;
   with no row for this Kenshi build the hook is NOT installed and the log says so. No address row is guessed here -
   finding the engine functions is later RE work. No wire message is added: every game evaluates the same local rules.

   The answers below are safe from ANY THREAD after InstallKapi (the rules are written once, during the start, and only
   read after). A race with no rules gets Kenshi's own behaviour. raceSid / raceName may be 0 or "". */

#include "../common/kapirules.h"

namespace coop
{
    void InstallKapi();

    int  KapiRuleCount();                           /* rules read from kenshi_api.cfg (0 = none or no file) */
    const kapi::Rules& KapiRules();

    bool  KapiSlotAllowed(const char* raceSid, const char* raceName, int slot);   /* slot = kapi::Slot */
    bool  KapiWeaponInHandAllowed(const char* raceSid, const char* raceName);
    float KapiSkillXpMultiplier(const char* raceSid, const char* raceName, int skill);   /* skill = kapi::SkillName order */
    float KapiStatCap(const char* raceSid, const char* raceName, int skill);            /* -1 = no cap */
    float KapiAnimSpeed(const char* raceSid, const char* raceName);
}
