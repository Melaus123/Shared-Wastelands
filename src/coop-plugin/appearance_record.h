// appearance_record.h - H010b: replicate the appearance RECORD, not the fields derived
// from it.
//
// WHY THIS EXISTS, and why the obvious approach is already refuted (F160, T052).
//
// H010a captured the authority's rolled appearance - sex, hair, height - wrote those fields
// onto the peer's character and called the engine's updateAppearance() to redraw. The
// transport worked perfectly. The write did not: a readback taken INSIDE the same function,
// after updateAppearance() and before returning, already showed the defaults again.
// **AppearanceBase::updateAppearance() RE-DERIVES hair and height from `appearanceData`.**
//
// Worse, `female` DID stick, because nothing recomputes it - leaving the peer holding
// `female=1` beside `body='human_male.mesh'`. A flag that lies about what is on screen is
// worse than no write at all under this project's parity rule.
//
// That was the FOURTH time one root cause has bitten (F127/F147/F150/F160): trying to
// out-write a running derivation. The attempt cap is reached for writing derived fields.
//
// H010b writes the SOURCE instead. `appearanceData` is a GameDataCopyStandalone - a
// per-character copy - and GameData keeps its values in typed name->value maps. Replicating
// those maps and letting each side run its own updateAppearance() means both machines derive
// the same person from identical inputs. That is the two simulations genuinely AGREEING
// rather than one overwriting the other, which is what the parity register asks for and what
// none of the four field-writing attempts achieved.

#pragma once

#include <string>
#include <vector>

class Character;
class GameData;

namespace coop {

// A flat, wire-ready copy of one GameData record's values. Kept as parallel key/value lists
// rather than maps: the order does not matter, the peer looks each key up by name, and a
// vector serialises without any iteration-order assumption.
struct RecordCopy
{
    std::vector<std::string> boolKeys;   std::vector<unsigned char> boolVals;
    std::vector<std::string> intKeys;    std::vector<int>           intVals;
    std::vector<std::string> floatKeys;  std::vector<float>         floatVals;
    std::vector<std::string> strKeys;    std::vector<std::string>   strVals;
    std::vector<std::string> fileKeys;   std::vector<std::string>   fileVals;
    std::vector<std::string> vecKeys;    std::vector<float>         vecVals;   // 3 per key

    // Object references - this is where a hair or beard CHOICE lives, as a target sid plus
    // three ints. Flattened the same way: refKeys[i] names the slot, and refSids/refInts
    // carry one entry per reference with refCounts[i] saying how many belong to key i.
    std::vector<std::string> refKeys;    std::vector<int>           refCounts;
    std::vector<std::string> refSids;    std::vector<int>           refInts;   // 3 per sid

    // The TOTAL number of quaternion values in the record. None are replicated, so this is
    // also the number dropped - but it is named for what it MEASURES, not for what we do with
    // it. The previous name `quatSkipped` left `0` ambiguous between "none were present" and
    // "none were counted", which an executor correctly refused to interpret (lesson 1).
    int quatTotal;

    RecordCopy() : quatTotal(0) {}

    int Entries() const
    {
        return (int)(boolKeys.size() + intKeys.size() + floatKeys.size()
                   + strKeys.size() + fileKeys.size() + vecKeys.size() + refKeys.size());
    }
};

// Read a character's appearance record. False if it cannot be reached.
bool CaptureAppearanceRecord(::Character* c, RecordCopy* out);

// Write the values onto a character's own appearance record, then make the engine re-derive
// from them. This is the only write H010b performs: the derived fields are left alone
// entirely, on purpose (F160).
bool ApplyAppearanceRecord(::Character* c, const RecordCopy& rec);

// P10 TEST-ONLY lever (`bodydown`): hand the appearance object its OWN record again through setAppearanceData 0x5378D0 -
// the engine call our APPEARANCE apply makes - with no value changed and no updateAppearance. setAppearanceData sets
// +0x143, so the engine's next AppearanceBase::update calls createBody (vt+0x58). MAIN THREAD.
// 1 called, 0 setAppearanceData unresolved, -1 no appearance object or record.
int ReassertAppearanceData(::Character* c);

// T-556 (appearance.cpp): apply `rec` to a character THIS game drives once its body can take it, then send APPEARANCE and
// CLOTHING; within 60 s or else (apply / send failed, not applied in time) the character's own look and kit go out by the
// settle send - a record that missed 60 s stays queued and is applied and sent when the body passes the gates.
// OwnLookResult: 0 waiting, 1 applied and sent, 2 applied and a send failed, 3 not applied in time (still queued), 4 the
// apply failed, -1 gone / unknown (a final answer is given once, then the row is dropped). MAIN THREAD.
void OwnLookQueue(unsigned int uid, const RecordCopy& rec);
int OwnLookResult(unsigned int uid);
void OwnLookWorldTeardown();   // the queued records and results belong to the world being destroyed

// Wire format. Self-describing lengths throughout so a truncated payload is REFUSED rather
// than misparsed - the same rule the protocol version bump exists for.
void SerialiseRecord(const RecordCopy& rec, std::vector<char>* out);
bool DeserialiseRecord(const std::vector<char>& b, size_t at, RecordCopy* out);

// One-line summary of what a record contains, for the log.
std::string RecordSummary(const RecordCopy& rec);

} // namespace coop
