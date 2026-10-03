// stats.h - S1 (read-stats, T260): a copy carries its owner's 44 saved stat values.
//
// T260 (Confirmed): 290 of 363 characters had a different athletics on the owner and on its copy, because
// CharStats::init 0x64C0A0 re-rolls 32 fields on each game (_randomiseStats 0x643260). The owner's values
// travel on SPAWN (a trailing block) and on MSG_STATS (44); the copy writes them into its CharStats and runs
// the engine's own recalculation. The block itself: src/common/statswire.h.
//
// v1: no XP-gain hooks on copies - the owner's next values overwrite whatever the copy's own engine gained.
#pragma once

#include <string>

namespace coop {

// MAIN THREAD. The 44 values of a character this game drives (Character +0x450 -> CharStats), fault-guarded.
// False when the stats object is missing / unreadable or a float is NaN / infinite (nothing is sent then).
bool StatsReadOwned(const void* character, unsigned int* raw44);

// MAIN THREAD. A SPAWN for `uid` carried these values: counted statsSpawnCarried and remembered as "last sent",
// so the periodic sender does not repeat them at once.
void StatsNoteSpawnCarried(unsigned int uid, const unsigned int* raw44);

// An inbound SPAWN without a block (an older payload, or an owner that could not read its character) /
// with a block that did not decode (truncated, or a float NaN / infinite). Counted only.
void StatsNoteNoBlock();
void StatsNoteBadBlock();

// MAIN THREAD. Write the owner's values into this game's COPY of `uid` and run the engine's recalculation
// (CharStats slot +0x20 = _recalculateStats 0x8857E0 - an animal's 0x885E10, which calls it - then the
// medical object's slot +8 = MedicalSystem::updateStats 0x644840). Refused for a uid this game drives. `why`
// names the caller in the log.
bool ApplyRemoteStats(unsigned int uid, const unsigned int* raw44, const char* why);

// MAIN THREAD, from the command-channel pump. The owner's sender: a character whose values changed (a whole
// number, or by 0.05 or more) or were last sent 30 s ago goes out as MSG_STATS, at most kStatsMaxPerTick a tick.
// Does nothing while EngineWritesBlocked() (load / teardown), counted statsTickBlocked.
void StatsTick();

// The counters for the [P014] REPORT line, starting with a space.
std::string StatsReportFields();

// T-189 runboost (H051). MAIN THREAD; fault-guarded; the caller has validated `character` (a live copy).
// Read: run = CharStats::runSpeed (+0x17C, the run speed), animMax = AnimationClass +0x19C (the ceiling). False when
// unreadable or the run speed is not a positive finite number.
bool RunCeilingRead(const void* character, float* run, float* animMax);
// Write the ceiling the engine's way: AnimationClass::applySpeedCap(v) 0x51BED0, then CharMovement::speedCap +0xB4
// (what periodicUpdate 0x660330 copies from +0x19C). True only when +0x19C then reads v.
bool RunCeilingWrite(void* character, float v);
// Put the engine's own ceiling back: RunCeilingWrite(its own run speed) - exactly MedicalSystem's value (0x644840).
bool RunCeilingRestore(void* character);

} // namespace coop
