/* src/coop-test/suite_part4.cpp - THE OFFLINE SUITE, part 4 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"

namespace suite {

/* T-609: MSG_BUILD carries two kind tables - buildwire.h's (DecodeBuild) and farmwire.h's (coopfarm::IsFarmKind, which session.cpp
   OnBuild asks first). A number in both sends a building message to the farm decoder, which drops it (run T1311: every
   DISMANTLE_ASK, 12, was taken as SPOT_ASK). Walks every kind byte 0..255 as a one-byte message: DecodeBuild's kind check comes
   right after its empty check, so kBuildDecodeBadKind means "not a building kind" and anything else means DecodeBuild takes the
   kind - a kind added to DecodeBuild's accept list is caught here with no second list to keep up to date. */
void t_t609_build_farm_kinds_apart()
{
    int nBuild = 0, nFarm = 0, nPower = 0;
    for (int k = 0; k < 256; ++k)
    {
        const char b = (char)(unsigned char)k;
        coopbuild::BuildMsg bm;
        const int isBuild = (coopbuild::DecodeBuild(&b, 1, &bm) != coopbuild::kBuildDecodeBadKind) ? 1 : 0;
        const int isFarm = (coopfarm::IsFarmKind(&b, 1) != 0) ? 1 : 0;
        const int isPower = (powerwire::IsPowerKind(&b, 1) != 0) ? 1 : 0;   /* P47: the third table (16, 17), peeked first */
        nBuild += isBuild; nFarm += isFarm; nPower += isPower;
        if (isBuild + isFarm + isPower > 1) Fail("T-609 / P47: MSG_BUILD kind " + I(k) + " is in more than one table (building " + I(isBuild) + ", farm " + I(isFarm) + ", power " + I(isPower) + ")");
    }
    if (nPower != 2) Fail("P47: the kind walk saw " + I(nPower) + " power kinds, not 2");
    const char p16 = (char)powerwire::kPowerState, p17 = (char)powerwire::kPowerAsk;
    if (coopfarm::IsFarmKind(&p16, 1) != 0 || coopfarm::IsFarmKind(&p17, 1) != 0) Fail("P47: the farm peek takes kind 16 or 17");
    /* the walk saw both tables: 8 building kinds (1-5, 8, 9, 12) and 7 farm kinds (6, 7, 10, 11, 13-15) today - more is fine */
    if (nBuild < 8) Fail("T-609: the kind walk saw fewer than 8 building kinds (" + I(nBuild) + ") - DecodeBuild no longer answers a one-byte message by kind");
    if (nFarm < 7) Fail("T-609: the kind walk saw fewer than 7 farm kinds (" + I(nFarm) + ")");
    const char dm = (char)coopbuild::kBuildDismantleAsk;
    if (coopfarm::IsFarmKind(&dm, 1) != 0) Fail("T-609: DISMANTLE_ASK is taken by the farm peek");
    const char hk = (char)coopbuild::kBuildHit;
    if (coopfarm::IsFarmKind(&hk, 1) != 0 || powerwire::IsPowerKind(&hk, 1) != 0) Fail("T-904: HIT is taken by the farm or power peek");
}

/* T-904 step 1: a wall's melee hit across games - the HIT round trip (ask and every answer) and every refusal; the hit's route, the
   owner's verdict and its memory of replayed seqs; a wall copy's complete STATE below its needed (a hit wall) and at it (whole again) */
void t_t904_wall_hit()
{
    coopbuild::BuildMsg h;
    h.kind = coopbuild::kBuildHit; h.key = "k|10|20|30"; h.seq = 77; h.hitCut = 12.5f; h.hitBlunt = 3.25f; h.reason = coopbuild::kBuildHitAsk;
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, h)) Fail("T-904: a HIT ask did not encode");
    const size_t at = 2 + h.key.size();   /* kind, key length, key */
    if (b.size() != at + 15) Fail("T-904: a HIT is not kind | key | u32 seq | f32 cut | f32 blunt | u8 verdict | i8 door | u8 repair (" + I((int)b.size()) + " bytes)");
    coopbuild::BuildMsg d;
    if (b.empty() || coopbuild::DecodeBuild(&b[0], b.size(), &d) != coopbuild::kBuildDecodeOk || d.kind != coopbuild::kBuildHit || d.key != h.key || d.seq != 77u
        || d.hitCut != 12.5f || d.hitBlunt != 3.25f || d.reason != coopbuild::kBuildHitAsk) Fail("T-904: a HIT ask did not round trip");
    for (int v = 1; v <= 6; ++v)
    {
        coopbuild::BuildMsg a;
        a.kind = coopbuild::kBuildHit; a.key = h.key; a.seq = 77; a.reason = (unsigned char)v;
        std::vector<char> ab;
        coopbuild::BuildMsg ad;
        if (!coopbuild::EncodeBuild(&ab, a) || coopbuild::DecodeBuild(&ab[0], ab.size(), &ad) != coopbuild::kBuildDecodeOk || ad.reason != (unsigned char)v || ad.seq != 77u)
            Fail("T-904: the HIT answer " + I(v) + " did not round trip");
    }
    /* refused to encode */
    {
        coopbuild::BuildMsg x = h; x.seq = 0;
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with seq 0 is encodable");
        x = h; x.hitCut = -1.0f;
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with a negative cut is encodable");
        x = h; x.hitBlunt = std::numeric_limits<float>::quiet_NaN();
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with a NaN blunt is encodable");
        x = h; x.hitCut = coopbuild::kBuildHitMax * 2.0f;
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT over kBuildHitMax is encodable");
        x = h; x.reason = 8;
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with verdict 8 is encodable");
        x = h; x.key = "";
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with no key is encodable");
        x = h; x.key = std::string(64, 'k');
        if (coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with a 64-byte key is encodable");
        x = h; x.key = std::string(63, 'k');
        if (!coopbuild::BuildEncodable(x)) Fail("T-904: a HIT with a 63-byte key is refused");
    }
    /* refused to decode */
    {
        std::vector<char> x(b.begin(), b.end() - 1);
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT one byte short decoded");
        x = b; x.push_back(0);
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT with a trailing byte decoded");
        x = b; x[at + 12] = 8;
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT with verdict 8 decoded");
        x = b; x[at + 12] = (char)coopbuild::kBuildHitResult;
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) == coopbuild::kBuildDecodeOk) Fail("T-904: an ask's bytes with verdict 7 decoded as a result (no destroyed byte)");
        x = b; x[at] = 0; x[at + 1] = 0; x[at + 2] = 0; x[at + 3] = 0;
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT with seq 0 decoded");
        const float nan = std::numeric_limits<float>::quiet_NaN(), neg = -2.0f;
        x = b; std::memcpy(&x[at + 4], &nan, 4);
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT with a NaN cut decoded");
        x = b; std::memcpy(&x[at + 8], &neg, 4);
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT with a negative blunt decoded");
        x = b; x[1] = 0; x.erase(x.begin() + 2, x.begin() + (std::ptrdiff_t)at);
        if (coopbuild::DecodeBuild(&x[0], x.size(), &d) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT with an empty key decoded");
    }
    /* the hit's route: (wall, playerWall, othersKnown, damageRead, replay) */
    if (coopbuild::BuildWallHitRoute(1, 1, 1, 1, 0) != coopbuild::kBuildHitRouteQueue) Fail("T-904: a player wall's hit was not queued for the writer ask");
    if (coopbuild::BuildWallHitRoute(0, 1, 1, 1, 0) != coopbuild::kBuildHitRouteEngine) Fail("T-904: a hit on a non-wall left the engine");
    if (coopbuild::BuildWallHitRoute(1, 0, 1, 1, 0) != coopbuild::kBuildHitRouteEngine) Fail("T-904: a town or unowned wall's hit left the engine");
    if (coopbuild::BuildWallHitRoute(1, 1, 0, 1, 0) != coopbuild::kBuildHitRouteEngine) Fail("T-904: a lone game's wall hit left the engine");
    if (coopbuild::BuildWallHitRoute(1, 1, 1, 0, 0) != coopbuild::kBuildHitRouteEngine) Fail("T-904: a hit with an unreadable damage was queued");
    if (coopbuild::BuildWallHitRoute(1, 1, 1, 1, 1) != coopbuild::kBuildHitRouteEngine) Fail("T-904: the mod's own replay was queued again");
    /* the verdict of the game an ask reached: (dup, held, loaded, destroyed, writer) */
    if (coopbuild::BuildHitVerdict(0, 1, 1, 0, 1) != coopbuild::kBuildHitApplied) Fail("T-904: a hit on the writer's loaded whole wall is not applied");
    if (coopbuild::BuildHitVerdict(1, 0, 0, 1, 0) != coopbuild::kBuildHitApplied) Fail("T-904: a resend of a replayed seq is not answered applied");
    if (coopbuild::BuildHitVerdict(0, 0, 1, 0, 1) != coopbuild::kBuildHitGone) Fail("T-904: a hit on a piece with no live row is not gone");
    if (coopbuild::BuildHitVerdict(0, 1, 0, 0, 1) != coopbuild::kBuildHitNotLoaded) Fail("T-904: a hit on an unloaded wall is not refused as not loaded");
    if (coopbuild::BuildHitVerdict(0, 1, 1, 1, 1) != coopbuild::kBuildHitRuin) Fail("T-904: a hit on a ruin is not refused");
    if (coopbuild::BuildHitVerdict(0, 1, 1, 0, 0) != coopbuild::kBuildHitNotWriter) Fail("T-904: a hit on a game that does not write the wall is not answered 'not writer'");
    if (std::string(coopbuild::BuildHitWord(6)).find("writer") == std::string::npos) Fail("T-904: verdict 6 has no word");
    /* the RESULT form: kind | key | seq | drop | needed | 7 | flags | after */
    {
        coopbuild::BuildMsg r;
        r.kind = coopbuild::kBuildHit; r.key = h.key; r.seq = 9; r.reason = coopbuild::kBuildHitResult; r.hitDrop = 2.5f; r.needed = 40.0f; r.destroyed = 1; r.hitOwed = 1; r.hitAfter = 0.0f;   /* a ruin: the engine's ruin leaves 0 */
        std::vector<char> rb;
        coopbuild::BuildMsg rd2;
        if (!coopbuild::EncodeBuild(&rb, r) || rb.size() != at + 18) Fail("T-904: a HIT result is not kind | key | seq | drop | needed | 7 | flags | after");
        else if (coopbuild::DecodeBuild(&rb[0], rb.size(), &rd2) != coopbuild::kBuildDecodeOk || rd2.reason != coopbuild::kBuildHitResult || rd2.seq != 9u
                 || rd2.hitDrop != 2.5f || rd2.needed != 40.0f || rd2.destroyed != 1 || rd2.hitOwed != 1 || rd2.hitAfter != 0.0f
                 || rd2.progress != 0.0f || rd2.hitCut != 0.0f || rd2.hitBlunt != 0.0f)
            Fail("T-904: a HIT result did not round trip");
        else
        {
            if (rb[at + 13] != 3) Fail("T-904: a HIT result's flags byte is not destroyed | owed");
            std::vector<char> y(rb.begin(), rb.end() - 1);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT result with no whole after decoded");
            y.assign(rb.begin(), rb.begin() + (std::ptrdiff_t)(at + 14));
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT result in the form without after decoded");
            y = rb; y[at + 13] = 4;
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT result with an unknown flag decoded");
            y = rb; y.push_back(0);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) == coopbuild::kBuildDecodeOk) Fail("T-904: a HIT result with a trailing byte decoded");
            const float nan = std::numeric_limits<float>::quiet_NaN(), neg = -1.0f;
            y = rb; std::memcpy(&y[at + 4], &nan, 4);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT result with a NaN drop decoded");
            y = rb; std::memcpy(&y[at + 4], &neg, 4);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT result with a negative drop decoded");
            y = rb; std::memcpy(&y[at + 8], &neg, 4);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT result with a negative needed decoded");
            y = rb; std::memcpy(&y[at + 14], &nan, 4);
            if (coopbuild::DecodeBuild(&y[0], y.size(), &rd2) != coopbuild::kBuildDecodeBadValue) Fail("T-904: a HIT result with a NaN after decoded");
        }
        coopbuild::BuildMsg z = r; z.seq = 0;
        if (coopbuild::BuildEncodable(z)) Fail("T-904: a HIT result with seq 0 is encodable");
        z = r; z.hitDrop = -1.0f;
        if (coopbuild::BuildEncodable(z)) Fail("T-904: a HIT result with a negative drop is encodable");
        z = r; z.destroyed = 0; z.hitOwed = 0;
        std::vector<char> zb;
        if (!coopbuild::EncodeBuild(&zb, z) || zb.size() != at + 18 || zb[at + 13] != 0
            || coopbuild::DecodeBuild(&zb[0], zb.size(), &rd2) != coopbuild::kBuildDecodeOk || rd2.destroyed != 0 || rd2.hitOwed != 0)
            Fail("T-904: a fresh, whole HIT result does not carry flags 0");
    }
    /* the drop a hit took, and a result where the hit did not run: (recOk, msgDestroyed, liveDestroyed, liveComplete, drop, liveP) */
    {
        const float hnan = std::numeric_limits<float>::quiet_NaN();
        if (coopbuild::BuildHitDrop(30.0f, 27.5f) != 2.5f) Fail("T-904: a hit's drop is not p0 - p1");
        if (coopbuild::BuildHitDrop(30.0f, 31.0f) != 0.0f) Fail("T-904: a rise was taken as a negative drop");
        if (coopbuild::BuildHitDrop(hnan, 1.0f) != 0.0f) Fail("T-904: an unreadable progress gave a drop");
        if (coopbuild::BuildHitDrop(5.0f, 0.0f) != 5.0f) Fail("T-904: a ruining hit's drop is not the health it had (the engine's ruin leaves 0)");
        float to = -1.0f;
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResProgress || to != 17.5f) Fail("T-904: a result's drop was not taken off the live health");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, 2.5f, 25.0f, &to) != coopbuild::kBuildHitResProgress || to != 22.5f) Fail("T-904: a repair made since the hit was overwritten by its result");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, 30.0f, 20.0f, &to) != coopbuild::kBuildHitResDestroy || to != 0.0f) Fail("T-904: a drop past 0 did not ruin the wall");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, 20.0f, 20.0f, &to) != coopbuild::kBuildHitResDestroy) Fail("T-904: a drop to exactly 0 did not ruin the wall (the engine ruins at <= 0)");
        if (coopbuild::BuildHitDropAction(0, 0, 0, 1, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResNone || to != 20.0f) Fail("T-904: a group-record copy took a result's drop");
        if (coopbuild::BuildHitDropAction(0, 1, 0, 1, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResDestroy) Fail("T-904: a group-record copy did not take a result's ruin");
        if (coopbuild::BuildHitDropAction(1, 1, 1, 1, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: a ruin took a hit result again");
        if (coopbuild::BuildHitDropAction(1, 0, 1, 1, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: a hit result made a ruin whole");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 0, 2.5f, 20.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: a hit result was written to an unfinished wall");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, hnan, 20.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: a NaN drop was written");
        if (coopbuild::BuildHitDropAction(1, 0, 0, 1, -1.0f, 20.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: a negative drop was written");
    }
    /* which results are taken: (verdict, writerSlot, from, owed, owner) */
    if (coopbuild::BuildHitResultAccept(2, 3, 3, 0, 0) != 1) Fail("T-904: a copy refused a result from its writer");
    if (coopbuild::BuildHitResultAccept(2, 3, 3, 0, 1) != 1) Fail("T-904: the owner refused a result from the area holder that writes its wall");
    if (coopbuild::BuildHitResultAccept(2, 3, 4, 0, 1) != 0) Fail("T-904: a result from a game that is not the writer was taken");
    if (coopbuild::BuildHitResultAccept(2, 3, 4, 0, 0) != 0) Fail("T-904: a copy took a result from a stale writer");
    if (coopbuild::BuildHitResultAccept(1, 0, 3, 0, 1) != 0) Fail("T-904: the owner took a result while it writes the wall itself");
    if (coopbuild::BuildHitResultAccept(0, -1, 3, 0, 0) != 0) Fail("T-904: a result was taken while no game writes the wall");
    if (coopbuild::BuildHitResultAccept(2, 3, -1, 0, 0) != 0) Fail("T-904: a result from an unknown slot was taken");
    if (coopbuild::BuildHitResultAccept(1, 0, 3, 1, 1) != 1) Fail("T-904: the owner refused an owed result for a hit made while it was away");
    if (coopbuild::BuildHitResultAccept(2, 3, 3, 1, 0) != 0) Fail("T-904: a game that is not the owner took an owed result");
    /* the owed result on the owner, compared as shares: (seen, liveP, liveN, after, n) */
    if (coopbuild::BuildHitOwedApplies(0, 30.0f, 40.0f, 22.5f, 40.0f) != 1) Fail("T-904: an owed result on an older, healthier zone record was not applied");
    if (coopbuild::BuildHitOwedApplies(0, 22.5f, 40.0f, 22.5f, 40.0f) != 0) Fail("T-904: an owed result was applied on a zone record that already carries it");
    if (coopbuild::BuildHitOwedApplies(1, 30.0f, 40.0f, 22.5f, 40.0f) != 0) Fail("T-904: an owed result was applied twice");
    if (coopbuild::BuildHitOwedApplies(0, 45.0f, 80.0f, 22.5f, 40.0f) != 0) Fail("T-904: the same share on a record with another needed (45/80 against 22.5/40) was taken as healthier");
    if (coopbuild::BuildHitOwedApplies(0, 40.0f, 80.0f, 27.5f, 40.0f) != 0) Fail("T-904: a lower share was taken as healthier because its raw health is higher");
    if (coopbuild::BuildHitOwedApplies(0, 60.0f, 80.0f, 22.5f, 40.0f) != 1) Fail("T-904: a higher share on a record with another needed was not applied");
    /* the owed result's new health: (msgDestroyed, liveComplete, liveP, liveN, drop, after, n) */
    {
        float to = -1.0f;
        if (coopbuild::BuildHitOwedTarget(0, 1, 30.0f, 40.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResProgress || to != 22.5f) Fail("T-904: an owed sum was not taken off the owner's health");
        if (coopbuild::BuildHitOwedTarget(0, 1, 25.0f, 40.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResProgress || to != 22.5f) Fail("T-904: an owed sum went past the expected health on a record carrying part of it");
        if (coopbuild::BuildHitOwedTarget(0, 1, 35.0f, 40.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResProgress || to != 27.5f) Fail("T-904: a repair made since the hits was overwritten by the owed sum");
        if (coopbuild::BuildHitOwedTarget(0, 1, 60.0f, 80.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResProgress || to != 52.5f) Fail("T-904: the raw owed drop was not taken off a record with another needed");
        if (coopbuild::BuildHitOwedTarget(0, 1, 50.0f, 80.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResProgress || to != 45.0f) Fail("T-904: an owed drop went below the expected share of a record with another needed");
        if (coopbuild::BuildHitOwedTarget(0, 1, 22.5f, 40.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: an owed sum lowered a health already at the expected share");
        if (coopbuild::BuildHitOwedTarget(0, 1, 5.0f, 40.0f, 10.0f, 0.0f, 40.0f, &to) != coopbuild::kBuildHitResDestroy || to != 0.0f) Fail("T-904: an owed sum past 0 did not ruin the wall");
        if (coopbuild::BuildHitOwedTarget(1, 1, 30.0f, 40.0f, 2.5f, 0.0f, 40.0f, &to) != coopbuild::kBuildHitResDestroy) Fail("T-904: an owed ruin did not ruin the wall");
        if (coopbuild::BuildHitOwedTarget(0, 0, 30.0f, 40.0f, 7.5f, 22.5f, 40.0f, &to) != coopbuild::kBuildHitResNone) Fail("T-904: an owed sum was written to an unfinished wall");
    }
    /* an own wall's STATE when its area loads again: (own, copyHere, removed, sentOk, isWall) */
    if (coopbuild::BuildOwnWallArriveState(1, 0, 0, 1, 1) != 1) Fail("T-904: an own wall whose area loaded again sent no STATE - a hit drop owed on it waits for one");
    if (coopbuild::BuildOwnWallArriveState(1, 1, 0, 1, 1) != 0 || coopbuild::BuildOwnWallArriveState(0, 0, 0, 1, 1) != 0) Fail("T-904: a copy / not-own wall sent an arrival STATE");
    if (coopbuild::BuildOwnWallArriveState(1, 0, 1, 1, 1) != 0) Fail("T-904: a removed own wall sent an arrival STATE");
    if (coopbuild::BuildOwnWallArriveState(1, 0, 0, 0, 1) != 0) Fail("T-904: an own wall sent an arrival STATE before its PLACE went out");
    if (coopbuild::BuildOwnWallArriveState(1, 0, 0, 1, 0) != 0) Fail("T-904: a piece that is not a wall sent an arrival STATE");
    /* the progress lever on a complete piece: (complete, destroyed, p, n, amount) */
    {
        float to = -1.0f;
        if (coopbuild::BuildTestRepairTarget(1, 0, 3.7f, 4.0f, 999.0f, &to) != coopbuild::kBuildTestRepairWrite || to != 4.0f) Fail("T-904: the progress lever took a complete wall past its needed amount");
        if (coopbuild::BuildTestRepairTarget(1, 0, 2.0f, 4.0f, 1.0f, &to) != coopbuild::kBuildTestRepairWrite || to != 3.0f) Fail("T-904: the progress lever did not add a part repair to a complete wall");
        if (coopbuild::BuildTestRepairTarget(1, 0, 4.0f, 4.0f, 999.0f, &to) != coopbuild::kBuildTestRepairNone || to != 4.0f) Fail("T-904: the progress lever changed a wall already at its needed amount");
        if (coopbuild::BuildTestRepairTarget(1, 0, 58.65f, 4.0f, 0.0f, &to) != coopbuild::kBuildTestRepairWrite || to != 4.0f) Fail("T-904: the progress lever left a complete wall above its needed amount");
        if (coopbuild::BuildTestRepairTarget(1, 0, 3.0f, 4.0f, -1.0f, &to) != coopbuild::kBuildTestRepairNone || to != 3.0f) Fail("T-904: the progress lever lowered a complete wall on a negative amount");
        if (coopbuild::BuildTestRepairTarget(0, 0, 0.0f, 4.0f, 999.0f, &to) != coopbuild::kBuildTestRepairEngine) Fail("T-904: the progress lever took an unfinished piece off the engine's routine");
        if (coopbuild::BuildTestRepairTarget(1, 1, 0.0f, 4.0f, 999.0f, &to) != coopbuild::kBuildTestRepairEngine) Fail("T-904: the progress lever took a ruin off the engine's routine");
        if (coopbuild::BuildTestRepairTarget(1, 0, 3.0f, 0.0f, 999.0f, &to) != coopbuild::kBuildTestRepairEngine) Fail("T-904: the progress lever wrote a piece whose needed amount is unknown");
    }
    /* the owner's STATE on the writer, compared as shares: (stateP, stateN, destroyed, after, n, now, sentAt, sent, seqChanged, gap) */
    if (coopbuild::BuildHitOwedOnState(22.5f, 40.0f, 0, 22.5f, 40.0f, 100, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedClear) Fail("T-904: the owner at the expected share did not clear the owed entry");
    if (coopbuild::BuildHitOwedOnState(45.0f, 80.0f, 0, 22.5f, 40.0f, 100, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedClear) Fail("T-904: the owner at the expected share of another needed did not clear the owed entry");
    if (coopbuild::BuildHitOwedOnState(40.0f, 80.0f, 0, 22.5f, 40.0f, 100, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedClear) Fail("T-904: a lower share with a higher raw health was taken as missing the hits");
    if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 1, 22.5f, 40.0f, 100, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedClear) Fail("T-904: the owner's ruin did not clear the owed entry");
    if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 0, 22.5f, 40.0f, 100, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedResend) Fail("T-904: the owed entry was not sent on the owner's healthier STATE");
    if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 0, 22.5f, 40.0f, 1000, 0, 1, 0, 5000) != coopbuild::kBuildHitOwedKeep) Fail("T-904: an unchanged owed entry was sent again inside its gap");
    if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 0, 22.5f, 40.0f, 1000, 0, 1, 1, 5000) != coopbuild::kBuildHitOwedResend) Fail("T-904: an owed entry with a hit added was not sent at once");
    if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 0, 22.5f, 40.0f, 6000, 0, 1, 0, 5000) != coopbuild::kBuildHitOwedResend) Fail("T-904: an owed entry was not sent again after its gap");
    /* the owed table: one entry per wall and owner, its hits summed */
    {
        std::vector<coopbuild::BuildHitOwedRow> t;
        coopbuild::BuildHitOwedAdd(&t, "w1", 2, 101, 2.5f, 0, 30.0f, 40.0f, 900, 4, 8);
        coopbuild::BuildHitOwedAdd(&t, "w1", 2, 102, 2.5f, 0, 99.0f, 99.0f, 901, 4, 8);
        coopbuild::BuildHitOwedAdd(&t, "w1", 2, 103, 2.5f, 0, 99.0f, 99.0f, 902, 4, 8);
        if (t.size() != 1 || t[0].sum != 7.5f || t[0].expectP != 22.5f || t[0].needN != 40.0f || t[0].seq != 902u || t[0].hitSeqs.size() != 3)
            Fail("T-904: three hits on one wall while its owner was away were not summed into one owed entry from the owner's last health");
        else
        {   /* the owner comes back on its older record (30/40): all three hits are sent and taken */
            float to = -1.0f;
            if (coopbuild::BuildHitOwedOnState(30.0f, 40.0f, 0, t[0].expectP, t[0].needN, 0, 0, 0, 1, 5000) != coopbuild::kBuildHitOwedResend
                || coopbuild::BuildHitOwedTarget(0, 1, 30.0f, 40.0f, t[0].sum, t[0].expectP, t[0].needN, &to) != coopbuild::kBuildHitResProgress || to != 22.5f)
                Fail("T-904: the owner coming back on an older record did not take all three summed hits");
        }
        coopbuild::BuildHitOwedAdd(&t, "w2", 2, 201, 1.0f, 0, 10.0f, 20.0f, 910, 4, 8);
        coopbuild::BuildHitOwedAdd(&t, "w1", 3, 301, 1.0f, 0, 10.0f, 20.0f, 920, 4, 8);
        if (t.size() != 3) Fail("T-904: owed entries of another wall or another owner were merged");
        coopbuild::BuildHitOwedMarkSent(&t[0], 100);
        if (coopbuild::BuildHitOwedAnswer(&t, "w1", 902, 2, coopbuild::kBuildHitNotWriter) != coopbuild::kBuildHitOwedKept || t.size() != 3 || t[0].sum != 7.5f)
            Fail("T-904: a 'not the writer' answer took the owed hits out");
        if (coopbuild::BuildHitOwedAnswer(&t, "w1", 101, 2, coopbuild::kBuildHitNotLoaded) != coopbuild::kBuildHitOwedKept || t[0].sum != 7.5f)
            Fail("T-904: a 'not loaded' answer took a fresh hit out");
        if (coopbuild::BuildHitOwedAnswer(&t, "w1", 101, 2, coopbuild::kBuildHitApplied) != coopbuild::kBuildHitOwedTaken || t[0].sum != 5.0f || t[0].expectP != 22.5f
            || t[0].sentCount != 2 || t[0].sentSum != 5.0f)
            Fail("T-904: a fresh hit the owner took did not leave the entry (the expected health kept)");
        coopbuild::BuildHitOwedAdd(&t, "w1", 2, 104, 2.5f, 0, 0.0f, 0.0f, 903, 4, 8);
        if (t[0].sum != 7.5f || t[0].expectP != 20.0f || t[0].seq == t[0].sentSeq) Fail("T-904: a hit after the owed send did not change the entry's seq");
        if (coopbuild::BuildHitOwedAnswer(&t, "w1", 902, 2, coopbuild::kBuildHitApplied) != coopbuild::kBuildHitOwedTaken || t[0].sum != 2.5f || t[0].hitSeqs.size() != 1)
            Fail("T-904: the owner taking the owed send did not take out exactly the hits it held");
        if (coopbuild::BuildHitOwedAnswer(&t, "w1", 104, 2, coopbuild::kBuildHitRuin) != coopbuild::kBuildHitOwedTaken || t.size() != 2) Fail("T-904: an emptied owed entry stayed");
        if (coopbuild::BuildHitOwedAnswer(&t, "w9", 1, 2, coopbuild::kBuildHitApplied) != coopbuild::kBuildHitOwedNoMatch) Fail("T-904: an answer for no owed hit matched");
        if (coopbuild::BuildHitOwedAnswer(&t, "w2", 201, 3, coopbuild::kBuildHitApplied) != coopbuild::kBuildHitOwedNoMatch) Fail("T-904: another game's answer took an owed hit out");
        std::vector<coopbuild::BuildHitOwedRow> c;
        size_t dropped = 0;
        for (int k = 0; k < 5; ++k) dropped += coopbuild::BuildHitOwedAdd(&c, std::string(1, (char)('a' + k)), 1, 10 + k, 1.0f, 0, 10.0f, 20.0f, 50 + k, 4, 8);
        if (dropped != 1 || c.size() != 4 || c[0].key != "b") Fail("T-904: the owed table did not drop its oldest wall past its cap");
        for (int k = 0; k < 10; ++k) coopbuild::BuildHitOwedAdd(&c, "e", 1, 100 + k, 1.0f, 0, 0.0f, 0.0f, 200 + k, 4, 8);
        if (c.back().hitSeqs.size() != 8 || c.back().sum != 11.0f || c.back().expectP != 0.0f) Fail("T-904: a raid on one wall did not keep its whole sum past the hit cap");
        coopbuild::BuildHitOwedAdd(&c, "b", 1, 300, 1.0f, 1, 0.0f, 0.0f, 301, 4, 8);
        if (c[0].destroyed != 1 || c[0].expectP != 0.0f) Fail("T-904: a ruining hit did not set the owed entry's expected health to 0");
    }
    /* the repair-refused marker: (wall, ownRecord, copyComplete, refundNow, ownerKnown, prevP, newP) */
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 1, 1, 1, 20.0f, 20.0f) != coopbuild::kBuildWallRepHoldSet) Fail("T-904: a refused wall repair's materials back did not set the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 1, 1, 0, 0.0f, 20.0f) != coopbuild::kBuildWallRepHoldSet) Fail("T-904: the first owner STATE with materials back did not set the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 1, 1, 1, 20.0f, 25.0f) != coopbuild::kBuildWallRepHoldClear) Fail("T-904: an applied repair (the owner's health rose) did not clear the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 1, 0, 1, 20.0f, 17.5f) != coopbuild::kBuildWallRepHoldClear) Fail("T-904: a change of the owner's health did not clear the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 1, 0, 1, 20.0f, 20.0f) != coopbuild::kBuildWallRepHoldNone) Fail("T-904: an unchanged STATE with nothing back moved the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 0, 1, 1, 1, 20.0f, 20.0f) != coopbuild::kBuildWallRepHoldNone) Fail("T-904: a group-record copy (its repair already refused) set the marker");
    if (coopbuild::BuildWallRepairHoldAct(0, 1, 1, 1, 1, 20.0f, 20.0f) != coopbuild::kBuildWallRepHoldNone) Fail("T-904: a piece that is not a wall set the marker");
    if (coopbuild::BuildWallRepairHoldAct(1, 1, 0, 1, 1, 20.0f, 20.0f) != coopbuild::kBuildWallRepHoldNone) Fail("T-904: an unfinished wall copy's refund (build work) set the marker");
    {
        std::vector<std::string> mk;
        if (coopbuild::BuildKeyMarkSet(&mk, "a", 2) != 0 || coopbuild::BuildKeyMarkHas(mk, "a") == 0) Fail("T-904: a marker was not set");
        coopbuild::BuildKeyMarkSet(&mk, "a", 2);
        if (mk.size() != 1) Fail("T-904: a marker was set twice");
        coopbuild::BuildKeyMarkSet(&mk, "b", 2);
        if (coopbuild::BuildKeyMarkSet(&mk, "c", 2) != 1 || coopbuild::BuildKeyMarkHas(mk, "a") != 0 || mk.size() != 2) Fail("T-904: the marker table did not drop its oldest past its cap");
        if (coopbuild::BuildKeyMarkClear(&mk, "b") != 1 || coopbuild::BuildKeyMarkHas(mk, "b") != 0 || coopbuild::BuildKeyMarkClear(&mk, "b") != 0) Fail("T-904: a marker was not cleared once");
    }
    /* the replayed seqs */
    {
        std::vector<unsigned int> seen;
        if (coopbuild::BuildHitSeen(seen, 5) != 0) Fail("T-904: an empty memory saw a seq");
        coopbuild::BuildHitRemember(&seen, 5, 3);
        coopbuild::BuildHitRemember(&seen, 5, 3);
        if (seen.size() != 1 || coopbuild::BuildHitSeen(seen, 5) == 0) Fail("T-904: a seq was not remembered once");
        coopbuild::BuildHitRemember(&seen, 6, 3); coopbuild::BuildHitRemember(&seen, 7, 3); coopbuild::BuildHitRemember(&seen, 8, 3);
        if (seen.size() != 3 || coopbuild::BuildHitSeen(seen, 5) != 0 || coopbuild::BuildHitSeen(seen, 8) == 0) Fail("T-904: the memory did not drop its oldest past its cap");
    }
    /* asks expire, the replayed seqs are kept per asker slot, a copy wall ruin is put back only where hits are shared */
    {
        if (coopbuild::BuildHitExpired(1000, 1000, 10000) != 0) Fail("T-904: a new ask expired");
        if (coopbuild::BuildHitExpired(10999, 1000, 10000) != 0) Fail("T-904: an ask expired before its life");
        if (coopbuild::BuildHitExpired(11000, 1000, 10000) == 0) Fail("T-904: an ask at its life did not expire");
        if (coopbuild::BuildHitExpired(4000, 0xFFFFF000UL, 10000) != 0) Fail("T-904: a tick wrap expired a young ask");
        if (coopbuild::BuildHitExpired(20000, 0xFFFFF000UL, 10000) == 0) Fail("T-904: a tick wrap kept an old ask");
        std::map<int, std::vector<unsigned int> > by;
        coopbuild::BuildHitRememberBy(&by, 2, 40, 4);
        if (coopbuild::BuildHitSeenBy(by, 2, 40) == 0) Fail("T-904: a replayed seq was not remembered for its slot");
        if (coopbuild::BuildHitSeenBy(by, 3, 40) != 0) Fail("T-904: one slot's seq was taken as another slot's");
        coopbuild::BuildHitRememberBy(&by, 3, 40, 4);
        if (coopbuild::BuildHitSeenBy(by, 3, 40) == 0 || by.size() != 2) Fail("T-904: the same seq from a second slot was not kept apart");
        for (unsigned int s = 41; s < 46; ++s) coopbuild::BuildHitRememberBy(&by, 2, s, 4);
        if (coopbuild::BuildHitSeenBy(by, 2, 40) != 0 || coopbuild::BuildHitSeenBy(by, 2, 45) == 0 || coopbuild::BuildHitSeenBy(by, 3, 40) == 0)
            Fail("T-904: one slot's cap reached another slot's seqs");
        if (coopbuild::BuildWallRuinPutBack(1, 1) != 1) Fail("T-904: an own-record wall copy ruin with the hook installed is not put back");
        if (coopbuild::BuildWallRuinPutBack(0, 1) != 0) Fail("T-904: a wall copy ruin was put back with no hit hook");
        if (coopbuild::BuildWallRuinPutBack(1, 0) != 0) Fail("T-904: a grouped wall copy ruin was put back");
        if (coopbuild::BuildWallRuinPutBack(1, -1) != 0) Fail("T-904: a wall copy ruin with an unreadable record was put back");
    }
    /* repair help on walls: both walls must hold their own progress record */
    {
        const int own = coopbuild::kBuildRepairWallOwnRecord, grp = coopbuild::kBuildRepairWallGroup, none = coopbuild::kBuildRepairWallNone;
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 1, own) != coopbuild::kBuildRepairOrderDamagedHelp) Fail("T-904: a repair on a damaged own-record wall copy is not help");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 1, 0, own) != coopbuild::kBuildRepairOrderRefused) Fail("T-904: a repair on an own-record wall ruin copy is not refused");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 1, grp) != coopbuild::kBuildRepairOrderRefused) Fail("T-904: a repair on a damaged grouped wall copy is not refused");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 1, 0, grp) != coopbuild::kBuildRepairOrderRefused) Fail("T-904: a repair on a grouped wall ruin copy is not refused");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 1, none) != coopbuild::kBuildRepairOrderDamagedHelp) Fail("T-904: a repair on a damaged non-wall copy changed");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 1, 0, none) != coopbuild::kBuildRepairOrderHelp) Fail("T-904: a repair on a non-wall ruin copy changed");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 0, own) != coopbuild::kBuildRepairOrderNone) Fail("T-904: a repair on a whole own-record wall copy was taken as help");
        if (coopbuild::BuildRepairHelpVerdict(95, 0, 0, 1, own) != coopbuild::kBuildRepairOrderNone) Fail("T-904: a repair on this game's own wall was taken by the copy rule");
        if (coopbuild::BuildRepairWallApplies(0, -1) != 1) Fail("T-904: repair work on a non-wall is not applied");
        if (coopbuild::BuildRepairWallApplies(1, 1) != 1) Fail("T-904: repair work on an own-record owner wall is not applied");
        if (coopbuild::BuildRepairWallApplies(1, 0) != 0) Fail("T-904: repair work on a grouped owner wall was applied");
        if (coopbuild::BuildRepairWallApplies(1, -1) != 0) Fail("T-904: repair work on an owner wall with an unreadable record was applied");
        /* walls: help on a partner's wall copy that holds its own progress record is help as any piece; a grouped wall's stays refused */
        if (coopbuild::kBuildWallKindNone != none || coopbuild::kBuildWallKindGroup != grp || coopbuild::kBuildWallKindOwnRecord != own) Fail("walls: the wall kinds are not the repair rule's");
        if (coopbuild::BuildWallHelpKind(0, 1) != none || coopbuild::BuildWallHelpKind(0, -1) != none) Fail("walls: a non-wall read as a wall");
        if (coopbuild::BuildWallHelpKind(1, 1) != own) Fail("walls: an own-record wall was not its own record's");
        if (coopbuild::BuildWallHelpKind(1, 0) != grp || coopbuild::BuildWallHelpKind(1, -1) != grp) Fail("walls: a grouped or unreadable wall was not a group's");
        if (coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobAllowed, 0, 2000u, 0) != coopbuild::kBuildJobFinderAllowed) Fail("walls: the job finder refused an own-record wall");
        if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 1, 1, 1) != coopbuild::kBuildHarvestRecord) Fail("walls: an own-record wall copy's materials were not recorded");
        if (coopbuild::BuildHelpHarvestVerdict(1, 1, 1, 0, 1, 1, 1) != coopbuild::kBuildHarvestSkip) Fail("walls: a grouped wall copy's materials were recorded");
        /* the off-main measurement's kinds */
        if (coopbuild::BuildHelpOffMainKind(0, 0, 0, 1) != coopbuild::kBuildOffHelpBuild) Fail("off-main: a build step was not build");
        if (coopbuild::BuildHelpOffMainKind(0, 0, 0, 0) != coopbuild::kBuildOffHelpMaterials) Fail("off-main: an amount-0 call on an unfinished piece was not materials");
        if (coopbuild::BuildHelpOffMainKind(1, 1, 0, 1) != coopbuild::kBuildOffHelpRepair || coopbuild::BuildHelpOffMainKind(1, 0, 1, 1) != coopbuild::kBuildOffHelpRepair) Fail("off-main: a ruin or damaged repair step was not repair");
        if (coopbuild::BuildHelpOffMainKind(1, 0, 0, 1) != -1 || coopbuild::BuildHelpOffMainKind(1, 0, 1, 0) != -1 || coopbuild::BuildHelpOffMainKind(0, 0, 0, -1) != -1) Fail("off-main: a whole piece, an amount-0 call on a complete piece or a negative amount was counted");
    }
    /* a wall copy's complete STATE */
    {
        float wp = -1.0f;
        if (coopbuild::BuildWallCopyAction(1, 1, 3.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallCompleteDamaged || std::fabs(wp - 7.5f) > 0.0001f)
            Fail("T-904: an own-record wall copy did not take a hit wall's share of its own needed");
        if (coopbuild::BuildWallCopyAction(1, 1, 3.999f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallCompleteDamaged || !(wp > 9.99f) || !(wp < 10.0f))
            Fail("T-904: a hit share near the top was capped like an unfinished STATE");
        if (coopbuild::BuildWallCopyAction(1, 1, -1.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallCompleteDamaged || wp != 0.0f)
            Fail("T-904: a negative complete progress was written to a wall copy");
        if (coopbuild::BuildWallCopyAction(0, 1, 3.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete || wp != 0.0f)
            Fail("T-904: a group-record wall copy took a hit share (it keeps materials only)");
        if (coopbuild::BuildWallCopyAction(1, 1, 3.0f, 0.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete)
            Fail("T-904: a complete STATE with no needed was scaled onto a wall copy");
        if (coopbuild::BuildWallCopyAction(1, 1, 4.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete || wp != 10.0f)
            Fail("T-904: a whole owner wall did not put an own-record copy back to full health");
        if (coopbuild::BuildWallCopyAction(1, 1, std::numeric_limits<float>::quiet_NaN(), 4.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete)
            Fail("T-904: a NaN complete progress was taken as a hit");
    }
}

/* T-1044: the power network test (a campfire's and a torch post's output 0 and capacity 0 are off it; a consumer, a producer and a
   battery are on it) and the send rule for a piece off it */
void t_t1044_power_network()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (powerwire::PowerOnNetwork(0.0f, 0.0f) != 0) Fail("T-1044: a piece with no output and no capacity (a campfire) taken as on the network");
    if (powerwire::PowerOnNetwork(-0.0f, 0.0f) != 0) Fail("T-1044: an output of -0 taken as on the network");
    if (powerwire::PowerOnNetwork(-4.0f, 0.0f) != 1) Fail("T-1044: a consumer (output -4, the spotlight) not taken as on the network");
    if (powerwire::PowerOnNetwork(10.0f, 0.0f) != 1) Fail("T-1044: a producer not taken as on the network");
    if (powerwire::PowerOnNetwork(0.0f, 500.0f) != 1) Fail("T-1044: a battery not taken as on the network");
    if (powerwire::PowerOnNetwork(0.0f, -1.0f) != 0) Fail("T-1044: a capacity below 0 taken as on the network");
    if (powerwire::PowerOnNetwork(0.0f, nan) != 0) Fail("T-1044: a NaN capacity taken as on the network");
    if (powerwire::PowerOnNetwork(nan, 0.0f) != 1) Fail("T-1044: a NaN output not taken as on the network (its switch must be written as before)");
    if (powerwire::PowerStateCarries(1, 0) != 1) Fail("T-1044: a piece on the network not sent");
    if (powerwire::PowerStateCarries(1, 3) != 1) Fail("T-1044: a production piece on the network not sent");
    if (powerwire::PowerStateCarries(0, 0) != 0) Fail("T-1044: a piece off the network with no stocks sent");
    if (powerwire::PowerStateCarries(0, 2) != 1) Fail("T-1044: a piece off the network with stocks not sent (its stocks must still go)");
}

/* P47: POWER_STATE / POWER_ASK round trip and every refusal (the decoder's own: key length 0 / 64, 17 stocks, NaN); the writer's
   send rule (a stock that rose goes at once); the gate, accept, sender and ask rules */
void t_p47_power()
{
    powerwire::PowerMsg m;
    m.kind = powerwire::kPowerState; m.key = "gen-sid@3,4@10,20,30"; m.on = 1; m.gets = 0; m.battery = 12.5f;
    m.stock.push_back(0.75f); m.stock.push_back(0.0f);
    std::vector<char> b;
    if (!powerwire::EncodePower(&b, m)) { Fail("P47: a POWER_STATE did not encode"); return; }
    if (b.size() != 2 + m.key.size() + 1 + 4 + 1 + 8) Fail("P47: POWER_STATE size " + I((int)b.size()));
    powerwire::PowerMsg o;
    if (powerwire::DecodePower(&b[0], b.size(), &o) != powerwire::kPowerDecodeOk) { Fail("P47: a POWER_STATE did not decode"); return; }
    if (o.kind != m.kind || o.key != m.key || o.on != 1 || o.gets != 0 || o.battery != 12.5f || o.stock.size() != 2 || o.stock[0] != 0.75f)
        Fail("P47: POWER_STATE round trip");
    std::vector<char> x = b; x.push_back(0);
    if (powerwire::DecodePower(&x[0], x.size(), &o) == powerwire::kPowerDecodeOk) Fail("P47: a byte over taken");
    x = b; x.pop_back();
    if (powerwire::DecodePower(&x[0], x.size(), &o) == powerwire::kPowerDecodeOk) Fail("P47: a byte short taken");
    x = b; x[2 + m.key.size()] = (char)4;
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadValue) Fail("P47: an unknown flag bit taken");
    powerwire::PowerMsg bad = m; bad.battery = -1.0f;
    if (powerwire::EncodePower(&b, bad)) Fail("P47: a negative battery encoded");
    bad = m; bad.stock.assign(17, 1.0f);
    if (powerwire::EncodePower(&b, bad)) Fail("P47: 17 stocks encoded");
    bad = m; bad.key = std::string(64, 'k');
    if (powerwire::EncodePower(&b, bad)) Fail("P47: a 64-byte key encoded");
    /* the decoder's own refusals, on hand-made bytes the encoder never writes */
    std::vector<char> g;
    if (!powerwire::EncodePower(&g, m)) { Fail("P47: a POWER_STATE did not encode again"); return; }
    const size_t body = 2 + m.key.size();   /* flags at body, battery at body + 1, n at body + 5, stocks from body + 6 */
    x.clear(); x.push_back((char)powerwire::kPowerState); x.push_back((char)0); x.insert(x.end(), g.begin() + body, g.end());
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadKey) Fail("P47: a key of length 0 taken");
    x.clear(); x.push_back((char)powerwire::kPowerState); x.push_back((char)64); x.insert(x.end(), (size_t)64, 'k'); x.insert(x.end(), g.begin() + body, g.end());
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadKey) Fail("P47: a key of length 64 taken");
    x = g; x[body + 5] = (char)17; x.insert(x.end(), (size_t)(15 * 4), (char)0);
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadValue) Fail("P47: 17 stocks taken");
    const unsigned int qnanBits = 0x7FC00000u;
    float qnan = 0.0f;
    std::memcpy(&qnan, &qnanBits, 4);
    x = g; std::memcpy(&x[body + 1], &qnan, 4);
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadValue) Fail("P47: a NaN battery level taken");
    x = g; std::memcpy(&x[body + 6], &qnan, 4);
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadValue) Fail("P47: a NaN stock taken");
    powerwire::PowerMsg a;
    a.kind = powerwire::kPowerAsk; a.key = "light@1,2@3,4,5"; a.what = powerwire::kAskSwitch; a.value = 1.0f;
    if (!powerwire::EncodePower(&b, a) || b.size() != 2 + a.key.size() + 5) { Fail("P47: a POWER_ASK did not encode"); return; }
    if (powerwire::DecodePower(&b[0], b.size(), &o) != powerwire::kPowerDecodeOk || o.what != 1 || o.value != 1.0f || o.key != a.key) Fail("P47: POWER_ASK round trip");
    x = b; x[2 + a.key.size()] = (char)2;
    if (powerwire::DecodePower(&x[0], x.size(), &o) != powerwire::kPowerDecodeBadValue) Fail("P47: an unknown ask taken");
    a.value = 0.5f;
    if (powerwire::EncodePower(&b, a)) Fail("P47: a switch of 0.5 encoded");
    /* the writer's send rule */
    if (powerwire::PowerSendDue(0, 0, 0, 0, 0) != 1) Fail("P47: the first look is not sent");
    if (powerwire::PowerSendDue(1, 1, 0, 0, 0) != 1 || powerwire::PowerSendDue(1, 0, 1, 0, 0) != 1) Fail("P47: a switch / count change waits");
    if (powerwire::PowerSendDue(1, 0, 0, 1, powerwire::kPowerLevelGapMs - 1) != 0) Fail("P47: a level change went before its gap");
    if (powerwire::PowerSendDue(1, 0, 0, 1, powerwire::kPowerLevelGapMs) != 1) Fail("P47: a level change held past its gap");
    if (powerwire::PowerSendDue(1, 0, 0, 0, 100000) != 0) Fail("P47: nothing changed and it was sent");
    if (powerwire::PowerLevelDiffers(1.0f, 1.00001f) != 0 || powerwire::PowerLevelDiffers(1.0f, 1.01f) != 1) Fail("P47: the level threshold");
    if (powerwire::PowerStockRose(1.0f, 0.5f) != 1 || powerwire::PowerStockRose(0.5f, 1.0f) != 0 || powerwire::PowerStockRose(1.0f, 1.00001f) != 0)
        Fail("P47: a stock that rose (a fuel item taken)");
    if (powerwire::PowerSendDue(1, 0, powerwire::PowerStockRose(1.0f, 0.2f), 1, 0) != 1) Fail("P47: a stock that rose waited for the level gap");
    /* the gate, accept and ask rules (writer 0 none, 1 here, 2 another game) */
    if (powerwire::PowerGateRuns(0, 2) != 0 || powerwire::PowerGateRuns(0, 1) != 1 || powerwire::PowerGateRuns(0, 0) != 1 || powerwire::PowerGateRuns(1, 2) != 1)
        Fail("P47: the burn / charge gate");
    if (powerwire::PowerStateAccept(2) != 1 || powerwire::PowerStateAccept(1) != 0 || powerwire::PowerStateAccept(0) != 0) Fail("P47: a state written on its writer or with no writer");
    if (powerwire::PowerStateFrom(2, 3, 3) != 1 || powerwire::PowerStateFrom(2, 3, 4) != 0 || powerwire::PowerStateFrom(2, -1, 3) != -1
        || powerwire::PowerStateFrom(2, 3, -1) != -1 || powerwire::PowerStateFrom(1, -1, -1) != 0 || powerwire::PowerStateFrom(0, 3, 3) != 0)
        Fail("P47: a POWER_STATE's sender (written from the writer only, kept while a slot is unknown)");
    if (powerwire::PowerAskAccept(1) != 1 || powerwire::PowerAskAccept(2) != 0 || powerwire::PowerAskAccept(0) != 0) Fail("P47: an ask done off the writer");
    if (powerwire::PowerAskDue(1, 1, 0) != 1 || powerwire::PowerAskDue(1, 1, 1) != 0 || powerwire::PowerAskDue(0, 1, 0) != 0) Fail("P47: the asker's switch check");
    if (powerwire::PowerAskFromOk(2, 2, 0) != 1 || powerwire::PowerAskFromOk(3, 2, 1) != 1 || powerwire::PowerAskFromOk(3, 2, 0) != 0 || powerwire::PowerAskFromOk(-1, 2, 1) != 0
        || powerwire::PowerAskFromOk(3, -1, 1) != 0) Fail("P47: an ask from a game that is not the owner's team");
}

/* the look queue's gap table in miniature: one window of kPowerMarkProbe slots shared by `pieces` pieces (ids 1..pieces), each
   stepped once per ms for `ms` ms through powerwire::PowerMarkPick; marks[id] counts the queued looks */
void P47MarkSim(int pieces, unsigned int ms, int* marks)
{
    int owner[powerwire::kPowerMarkProbe];
    unsigned int at[powerwire::kPowerMarkProbe];
    for (int i = 0; i < powerwire::kPowerMarkProbe; ++i) { owner[i] = 0; at[i] = 0; }
    for (int id = 0; id <= pieces; ++id) marks[id] = 0;
    for (unsigned int now = 1000; now < 1000 + ms; ++now)
        for (int id = 1; id <= pieces; ++id)
        {
            int kind[powerwire::kPowerMarkProbe];
            unsigned int age[powerwire::kPowerMarkProbe];
            for (int i = 0; i < powerwire::kPowerMarkProbe; ++i) { kind[i] = owner[i] == id ? 1 : (owner[i] == 0 ? 0 : 2); age[i] = now - at[i]; }
            const int pick = powerwire::PowerMarkPick(kind, age, powerwire::kPowerMarkProbe, 0);
            if (pick >= 0) { owner[pick] = id; at[pick] = now; ++marks[id]; }
        }
}

/* P47 fix: a stock that reached 0 goes at once (the engine stops the fuel step then); the look queue's gap table (an off-main
   step marks at most once per kPowerMarkGapMs per slot; pieces that share a window still throttle; a forced mark goes at once) */
void t_p47_look_queue()
{
    /* a stock that reached 0 */
    if (powerwire::PowerStockEmptied(0.0f, 0.5f) != 1) Fail("P47 fix: a stock that reached 0 from 0.5 was not seen");
    if (powerwire::PowerStockEmptied(0.00005f, 0.5f) != 1) Fail("P47 fix: a stock under the threshold is not 0");
    if (powerwire::PowerStockEmptied(0.0f, 0.0f) != 0) Fail("P47 fix: a stock already 0 when sent was sent again");
    if (powerwire::PowerStockEmptied(0.2f, 0.5f) != 0) Fail("P47 fix: a stock that fell but is not 0 went at once");
    if (powerwire::PowerStockEmptied(0.5f, 0.0f) != 0) Fail("P47 fix: a stock that rose from 0 counted as emptied");
    if (powerwire::PowerSendDue(1, 0, powerwire::PowerStockEmptied(0.0f, 0.3f), 1, 0) != 1) Fail("P47 fix: a stock of 0 waited for the level gap");
    if (powerwire::PowerSendDue(1, 0, powerwire::PowerStockEmptied(0.1f, 0.3f), 1, 0) != 0) Fail("P47 fix: a falling stock above 0 skipped the level gap");
    /* the look queue's gap table: one window (kind 1 = this piece, 0 = empty, 2 = another piece) */
    const unsigned int G = powerwire::kPowerMarkGapMs;
    {
        const int k0[4] = { 2, 0, 1, 0 }; const unsigned int a0[4] = { 0, 0, G - 1, 0 };
        if (powerwire::PowerMarkPick(k0, a0, 4, 0) != -1) Fail("P47 fix: a piece marked again inside the gap");
        if (powerwire::PowerMarkPick(k0, a0, 4, 1) != 2) Fail("P47 fix: a forced mark (the mod's own write) held by the gap");
        const unsigned int a1[4] = { 0, 0, G, 0 };
        if (powerwire::PowerMarkPick(k0, a1, 4, 0) != 2) Fail("P47 fix: a piece held past the gap");
        const int k2[4] = { 2, 2, 0, 0 }; const unsigned int a2[4] = { 0, 0, 0, 0 };
        if (powerwire::PowerMarkPick(k2, a2, 4, 0) != 2) Fail("P47 fix: a piece new to its window did not take the first empty slot");
        const int k3[4] = { 2, 2, 2, 2 }; const unsigned int a3[4] = { 10, G + 5, G - 1, G + 1 };
        if (powerwire::PowerMarkPick(k3, a3, 4, 0) != 1) Fail("P47 fix: a full window did not hand over its oldest slot");
        const unsigned int a4[4] = { 10, G / 2 - 2, G / 2 - 1, 0 };
        if (powerwire::PowerMarkPick(k3, a4, 4, 0) != -1) Fail("P47 fix: a full window inside half the gap marked anyway");
        const unsigned int a5[4] = { 10, G / 2 - 2, G / 2, 0 };
        if (powerwire::PowerMarkPick(k3, a5, 4, 0) != 2) Fail("P47 fix: a full window's slot half a gap old was not handed over");
        if (powerwire::PowerMarkPick(k3, a4, 4, 1) != 2) Fail("P47 fix: a forced mark on a full window did not take the oldest slot");
    }
    {   /* pieces stepped every ms for 1000 ms */
        const unsigned int ms = 1000;
        int marks[8];
        P47MarkSim(2, ms, marks);   /* two pieces whose addresses share a window (the old one-slot table: both marked every step) */
        if (marks[1] < 1 || marks[2] < 1) Fail("P47 fix: a piece sharing a window was never marked");
        if (marks[1] > (int)(ms / G) + 1 || marks[2] > (int)(ms / G) + 1) Fail("P47 fix: a piece sharing a window marked more than once per gap");
        P47MarkSim(6, ms, marks);   /* more pieces than the window holds: they take turns, each slot at most once per half gap */
        int total = 0;
        for (int id = 1; id <= 6; ++id)
        {
            total += marks[id];
            if (marks[id] < 1) Fail("P47 fix: a piece of a full window was never marked");
        }
        if (total > powerwire::kPowerMarkProbe * ((int)(2 * ms / G) + 1)) Fail("P47 fix: a full window marked more than once per half gap per slot");
    }
    if (!(G < 2000)) Fail("P47 fix: the mark gap is not under the row's stale time (2000 ms)");
    /* fold 2 (T1328): a due mark while engine writes are blocked is counted blocked, never as a full queue */
    if (powerwire::PowerMarkOutcome(1, 1, 0) != powerwire::kPowerMarkBlocked) Fail("P47 fold 2: a blocked mark with no room counted as a full queue");
    if (powerwire::PowerMarkOutcome(1, 1, 1) != powerwire::kPowerMarkBlocked) Fail("P47 fold 2: a mark was queued while engine writes are blocked");
    if (powerwire::PowerMarkOutcome(1, 0, 1) != powerwire::kPowerMarkBlocked) Fail("P47 fold 2: a blocked mark with no key counted as noKey");
    if (powerwire::PowerMarkOutcome(0, 0, 1) != powerwire::kPowerMarkNoKey) Fail("P47 fold 2: a mark with no key was queued");
    if (powerwire::PowerMarkOutcome(0, 1, 0) != powerwire::kPowerMarkFull) Fail("P47 fold 2: a real overflow was not counted full");
    if (powerwire::PowerMarkOutcome(0, 1, 1) != powerwire::kPowerMarkQueued) Fail("P47 fold 2: a due mark with a key and room was not queued");
    /* fold 2 (T1328): first-look / first-apply lines only for a player's piece; changes have their own budget */
    if (powerwire::PowerSayKind(1, 0, 0) != powerwire::kPowerSayNone) Fail("P47 fold 2: a world piece's first look printed a line");
    if (powerwire::PowerSayKind(1, 0, 1) != powerwire::kPowerSayFirst) Fail("P47 fold 2: a player's piece's first look printed no line");
    if (powerwire::PowerSayKind(0, 0, 1) != powerwire::kPowerSayNone) Fail("P47 fold 2: a send with no change and not the first printed a line");
    if (powerwire::PowerSayKind(0, 1, 0) != powerwire::kPowerSayEvent || powerwire::PowerSayKind(1, 1, 0) != powerwire::kPowerSayEvent
        || powerwire::PowerSayKind(1, 1, 1) != powerwire::kPowerSayEvent) Fail("P47 fold 2: a change did not spend the change budget (or a world piece's change was hidden)");
    if (powerwire::kPowerSayFirst == powerwire::kPowerSayEvent || powerwire::kPowerSayCap < 1) Fail("P47 fold 2: the two line budgets are not separate");
}

/* H-P47a: an off-main step's writer verdict - the live answer when it knows, else the main thread's published one while its row is
   live, else none (the step runs, as before); every branch, and what the charge / fuel gate then does */
void t_p47_offmain_verdict()
{
    const unsigned int S = 2000;   /* build.cpp kPwStaleMs */
    /* the live answer knows: it wins over any published verdict, fresh or not */
    if (powerwire::PowerStepSource(1, 1, 2, 0, S) != powerwire::kPowerFromLive || powerwire::PowerStepSource(2, 1, 1, 0, S) != powerwire::kPowerFromLive
        || powerwire::PowerStepSource(2, 0, 0, 0, S) != powerwire::kPowerFromLive) Fail("H-P47a: a live answer lost to the published verdict");
    if (powerwire::PowerStepWriter(powerwire::PowerStepSource(1, 1, 2, 0, S), 1, 2) != 1) Fail("H-P47a: the live verdict not used");
    /* live none: the published verdict decides while its row is live */
    if (powerwire::PowerStepSource(0, 1, 2, 0, S) != powerwire::kPowerFromPublished || powerwire::PowerStepSource(0, 1, 2, S, S) != powerwire::kPowerFromPublished)
        Fail("H-P47a: a fresh published verdict not used when the live answer is none");
    if (powerwire::PowerStepWriter(powerwire::PowerStepSource(0, 1, 2, 10, S), 0, 2) != 2) Fail("H-P47a: the published 'another game writes' not used");
    if (powerwire::PowerStepWriter(powerwire::PowerStepSource(0, 1, 1, 10, S), 0, 1) != 1) Fail("H-P47a: the published 'this game writes' not used");
    /* none known: no published verdict, a stale one, or one that names no writer */
    if (powerwire::PowerStepSource(0, 0, 2, 0, S) != powerwire::kPowerFromNone) Fail("H-P47a: a verdict used that was never published");
    if (powerwire::PowerStepSource(0, 1, 2, S + 1, S) != powerwire::kPowerFromNone) Fail("H-P47a: a stale published verdict used");
    if (powerwire::PowerStepSource(0, 1, 0, 0, S) != powerwire::kPowerFromNone) Fail("H-P47a: a published 'none' counted as published");
    if (powerwire::PowerStepWriter(powerwire::kPowerFromNone, 0, 2) != 0) Fail("H-P47a: no source gave a writer");
    /* the gate on the result: the published 'another game writes' holds the charge / burn; none still runs it (today's behaviour) */
    if (powerwire::PowerGateRuns(0, powerwire::PowerStepWriter(powerwire::PowerStepSource(0, 1, 2, 5, S), 0, 2)) != 0)
        Fail("H-P47a: the copy's charge ran with the published 'another game writes'");
    if (powerwire::PowerGateRuns(0, powerwire::PowerStepWriter(powerwire::PowerStepSource(0, 0, 0, 0, S), 0, 0)) != 1)
        Fail("H-P47a: a step with no verdict anywhere was held");
}

/* H-P47b: the copy's switch put back to the writer's last state after the mod's own completion apply - every branch */
void t_p47_completion_switch()
{
    if (powerwire::PowerCompletionSwitch(2, 1, 0, 1) != 0) Fail("H-P47b: the engine's switch-on was not put back to the writer's off");
    if (powerwire::PowerCompletionSwitch(2, 1, 1, 0) != 1) Fail("H-P47b: a switch-off was not put back to the writer's on");
    if (powerwire::PowerCompletionSwitch(2, 1, 1, 1) != -1 || powerwire::PowerCompletionSwitch(2, 1, 0, 0) != -1)
        Fail("H-P47b: a switch already the writer's was written");
    if (powerwire::PowerCompletionSwitch(2, 0, 0, 1) != -1) Fail("H-P47b: a switch written with no writer state held");
    if (powerwire::PowerCompletionSwitch(1, 1, 0, 1) != -1 || powerwire::PowerCompletionSwitch(0, 1, 0, 1) != -1)
        Fail("H-P47b: a switch written on the writer's own game or with no writer");
    if (powerwire::PowerCompletionSwitch(2, 1, 0, -1) != -1) Fail("H-P47b: a switch written after an unreadable read");
    /* the put-back switch then asks nothing (the copy mirrors the writer) */
    if (powerwire::PowerAskDue(1, powerwire::PowerCompletionSwitch(2, 1, 0, 1), 0) != 0) Fail("H-P47b: the put-back switch still asks");
}

/* par16: the farm wire (kinds 6 / 7 round trip, refusals), the hold gate, the publish rate, the apply count and the operate take */
void t_par16_farm()
{
    coopfarm::FarmMsg m;
    m.kind = coopfarm::kFarmState; m.key = "farm-sid@3,4@10,20,30";
    m.grown = 1.25f; m.died = 0.0f; m.cleared = 0.5f; m.growStart = 0.1f; m.harvested = 2; m.prodState = 3;
    m.hasProgress = 1; m.progress = 0.75f; m.marker[0] = 100.0f; m.marker[1] = -5.0f; m.marker[2] = 7.5f;
    m.ages.push_back(-10.0f); m.ages.push_back(-10.0f); m.ages.push_back(1.0f); m.ages.push_back(0.97f);
    std::vector<char> b;
    if (!coopfarm::EncodeFarm(&b, m)) { Fail("a FARM did not encode"); return; }
    coopfarm::FarmMsg o;
    if (coopfarm::IsFarmKind(&b[0], b.size()) != 1) Fail("kind 6 not recognised as a farm kind");
    if (coopfarm::DecodeFarm(&b[0], b.size(), &o) != coopfarm::kFarmDecodeOk || o.key != m.key || o.harvested != 2 || o.prodState != 3
        || o.ages.size() != 4 || o.ages[3] != 0.97f || o.grown != 1.25f || o.progress != 0.75f || o.marker[2] != 7.5f || o.hasProgress != 1)
        Fail("a FARM did not round-trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopfarm::DecodeFarm(&b[0], cut, &o) == coopfarm::kFarmDecodeOk) { Fail("a truncated FARM was accepted"); break; }
    std::vector<char> t(b); t.push_back(0);
    if (coopfarm::DecodeFarm(&t[0], t.size(), &o) == coopfarm::kFarmDecodeOk) Fail("a FARM with a trailing byte was accepted");
    coopbuild::BuildMsg bm;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &bm) == coopbuild::kBuildDecodeOk) Fail("DecodeBuild accepted a farm kind");
    coopfarm::FarmMsg bad(m); bad.harvested = 5;
    if (coopfarm::FarmEncodable(bad)) Fail("harvested above the plant count was encodable");
    bad = m; bad.ages[1] = 0.0f / std::atof("0");
    if (coopfarm::FarmEncodable(bad)) Fail("a NaN age was encodable");
    coopfarm::FarmMsg op; op.kind = coopfarm::kFarmOp; op.key = "k"; op.work = 2.5f; op.uid = 0x80000123u; op.skill = 37.5f;
    std::vector<char> ob;
    if (!coopfarm::EncodeFarm(&ob, op) || coopfarm::DecodeFarm(&ob[0], ob.size(), &o) != coopfarm::kFarmDecodeOk || o.kind != 7 || o.work != 2.5f
        || o.uid != 0x80000123u || o.skill != 37.5f)
        Fail("a FARM_OP did not round-trip (work, worker uid, skill)");
    for (size_t cut = 0; cut < ob.size(); ++cut)
        if (coopfarm::DecodeFarm(&ob[0], cut, &o) == coopfarm::kFarmDecodeOk) { Fail("a truncated FARM_OP was accepted"); break; }
    op.skill = coopfarm::kFarmSkillUnknown; if (!coopfarm::FarmEncodable(op)) Fail("a FARM_OP with an unknown skill was refused");
    op.skill = -2.0f; if (coopfarm::FarmEncodable(op)) Fail("a FARM_OP with a skill below unknown was encodable");
    op.skill = 10.0f;
    op.work = 0.0f; if (coopfarm::FarmEncodable(op)) Fail("a zero FARM_OP was encodable");
    op.work = 61.0f; if (coopfarm::FarmEncodable(op)) Fail("a FARM_OP over the cap was encodable");
    /* par16 fold #7: the water rides the FARM */
    {
        coopfarm::FarmMsg wm(m); wm.inStock.push_back(0.75f); wm.inRate.push_back(0.01f); wm.inStock.push_back(3.0f); wm.inRate.push_back(0.02f);
        std::vector<char> wb;
        if (!coopfarm::EncodeFarm(&wb, wm) || coopfarm::DecodeFarm(&wb[0], wb.size(), &o) != coopfarm::kFarmDecodeOk || o.inStock.size() != 2
            || o.inStock[0] != 0.75f || o.inRate[1] != 0.02f || o.ages.size() != 4)
            Fail("a FARM with water did not round-trip");
        for (size_t cut = 0; cut < wb.size(); ++cut)
            if (coopfarm::DecodeFarm(&wb[0], cut, &o) == coopfarm::kFarmDecodeOk) { Fail("a truncated FARM with water was accepted"); break; }
        wm.inRate.pop_back(); if (coopfarm::FarmEncodable(wm)) Fail("a FARM with unequal water lists was encodable");
        coopfarm::FarmMsg c2(m); c2.inStock.push_back(0.5f); c2.inRate.push_back(0.01f);
        coopfarm::FarmMsg c3(c2); c3.inStock[0] = 0.51f;
        if (coopfarm::FarmPublishDue(c2, c3, 1, 5000) != 0) Fail("a water move below the step was due");
        c3.inStock[0] = 0.53f;
        if (coopfarm::FarmPublishDue(c2, c3, 1, 5000) != 1) Fail("a water move of one step was not due");
    }
    /* par16 fold #2: work slices at the writer's own g_dt; a paused writer (dt 0) converts nothing */
    {
        float amt = -1.0f;
        if (coopfarm::FarmWorkSlice(1.0f, 0.0f, 0.25f, &amt) != 0.0f || amt != 0.0f) Fail("a zero g_dt converted work");
        const float w1 = coopfarm::FarmWorkSlice(1.0f, 0.02f, 0.25f, &amt);
        if (w1 < 0.00499f || w1 > 0.00501f || amt < 0.2499f || amt > 0.2501f) Fail("a work slice is not capped at chunk x dt");
        const float w2 = coopfarm::FarmWorkSlice(0.001f, 0.02f, 0.25f, &amt);
        if (w2 != 0.001f || amt < 0.0499f || amt > 0.0501f) Fail("a small work slice did not convert by the writer's dt");
    }
    /* par16 fold #3 / #8: hidden plants, and a farm past the table is held */
    if (coopfarm::FarmPlantHidden(-10.0f) != 1 || coopfarm::FarmPlantHidden(-1.0f) != 0 || coopfarm::FarmPlantHidden(0.5f) != 0) Fail("the hidden-plant test is wrong");
    if (coopfarm::FarmGate(0, 1, coopfarm::kFarmVerdictFull) != coopfarm::kFarmHold) Fail("a farm past the table ran ungated");
    if (coopfarm::FarmGate(0, 0, coopfarm::kFarmVerdictFull) != coopfarm::kFarmRun) Fail("an unlinked full farm was held");
    /* the gate (H3): on a linked multiplayer game only the named writer runs a farm - another writer AND no writer named hold */
    if (coopfarm::FarmGate(0, 1, 2) != coopfarm::kFarmHold) Fail("another writer did not hold");
    if (coopfarm::FarmGate(0, 1, 1) != coopfarm::kFarmRun) Fail("this writer held");
    if (coopfarm::FarmGate(0, 1, 0) != coopfarm::kFarmHold) Fail("H3: an undecided farm grew on a linked game");
    if (coopfarm::FarmGate(1, 1, 0) != coopfarm::kFarmRun || coopfarm::FarmGate(0, 0, 0) != coopfarm::kFarmRun) Fail("a lone / unlinked undecided farm held");
    if (coopfarm::FarmGate(1, 1, 2) != coopfarm::kFarmRun || coopfarm::FarmGate(0, 0, 2) != coopfarm::kFarmRun) Fail("a lone / unlinked game held");
    /* M3: a FARM is taken only from the game named the writer */
    if (coopfarm::FarmFromWriter(2, 2) != coopfarm::kFarmFromWriter) Fail("M3: the writer's FARM was not taken");
    if (coopfarm::FarmFromWriter(2, 3) != coopfarm::kFarmFromRefuse) Fail("M3: a third game's FARM was taken");
    if (coopfarm::FarmFromWriter(-1, 2) != coopfarm::kFarmFromWait || coopfarm::FarmFromWriter(2, -1) != coopfarm::kFarmFromWait)
        Fail("M3: a FARM whose writer or sender is not known was judged");
    /* T-674: worker spots */
    {
        using namespace coopfarm;
        if (SpotTakeStep(1, 1, 0, kProdWriterOther, kSpotNone, 0) != kSpotPass) Fail("T-674: a lone game asked for a spot");
        if (SpotTakeStep(0, 0, 0, kProdWriterOther, kSpotNone, 0) != kSpotPass) Fail("T-674: an unlinked game asked for a spot");
        if (SpotTakeStep(0, 1, 1, kProdWriterOther, kSpotNone, 0) != kSpotPass) Fail("T-674: a listed worker was refused");
        if (SpotTakeStep(0, 1, 0, kProdWriterHere, kSpotNone, 0) != kSpotPass) Fail("T-674: the runner's own worker was refused");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotNone, 0) != kSpotAskNow) Fail("T-674: an unlisted worker did not ask the runner");
        if (SpotTakeStep(0, 1, 0, kProdWriterNone, kSpotNone, 0) != kSpotAskNow) Fail("T-674: an undecided building took a spot locally");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotAsked, 100) != kSpotWait) Fail("T-674: a worker with an ask out was not refused");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotAsked, kSpotAskGiveUpMs + 1) != kSpotAskNow) Fail("T-674: an unanswered ask was never asked again");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotGranted, 0) != kSpotPass) Fail("T-674: a granted spot was refused");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotRefused, kSpotRetryMs) != kSpotWait) Fail("T-674: a full answer was not held");
        if (SpotTakeStep(0, 1, 0, kProdWriterOther, kSpotRefused, kSpotRetryMs + 1) != kSpotAskNow) Fail("T-674: a full answer was never asked again");
        if (SpotAnswerApply(kSpotAsked, kSpotYes) != kSpotGranted) Fail("T-674: yes did not grant");
        if (SpotAnswerApply(kSpotAsked, kSpotFull) != kSpotRefused || SpotAnswerApply(kSpotAsked, kSpotNoCopy) != kSpotRefused) Fail("T-674: full / no copy did not refuse");
        if (SpotAnswerApply(kSpotNone, kSpotYes) != kSpotNone || SpotAnswerApply(kSpotRefused, kSpotFull) != kSpotRefused) Fail("T-674: an answer with no ask out changed a row");
        if (SpotAnswerApply(kSpotGranted, kSpotFull) != kSpotAsked || SpotGrantLost(kSpotGranted, kSpotFull) != 1
            || SpotGrantLost(kSpotGranted, kSpotYes) != 0 || SpotGrantLost(kSpotAsked, kSpotFull) != 0)
            Fail("T-853 re-check #4: a full answer to a granted row's keep-alive did not end the grant");
        if (SpotAnswerFor(0, 1) != -1) Fail("T-674: a game that does not run the building answered");
        if (SpotAnswerFor(1, 1) != kSpotYes || SpotAnswerFor(1, 0) != kSpotFull) Fail("T-674 / T-853: the runner's answer is not its take (the engine's, or a counted spot)");
        if (SpotReleaseDue(kSpotGranted, 1) != 1) Fail("T-674: a granted worker's leaving was not released");
        if (SpotReleaseDue(kSpotGranted, 0) != 0 || SpotReleaseDue(kSpotAsked, 1) != 0 || SpotReleaseDue(kSpotNone, 1) != 0) Fail("T-674: a release went out for no granted spot");
        if (SpotRunnerFree(1, 0, 1) != 1 || SpotRunnerFree(0, 1, 1) != 1 || SpotRunnerFree(0, 0, 0) != 1) Fail("T-674: a runner spot was not freed");
        if (SpotRunnerFree(0, 0, 1) != 0) Fail("T-674: a runner spot in use was freed");
        /* T-853 */
        if (SpotAnswerApply(kSpotGranted, kSpotRevoked) != kSpotAsked) Fail("T-853 #5: a revoke did not send a granted row back to asked");
        if (SpotAnswerApply(kSpotAsked, kSpotRevoked) != kSpotAsked || SpotAnswerApply(kSpotNone, kSpotRevoked) != kSpotNone
            || SpotAnswerApply(kSpotRefused, kSpotRevoked) != kSpotRefused) Fail("T-853 #5: a revoke changed a row holding no grant");
        if (SpotRoomLeft(3, 1, 5) != 1 || SpotRoomLeft(2, 0, 3) != 1) Fail("T-853 #4: room under the limit was not seen");
        if (SpotRoomLeft(4, 1, 5) != 0 || SpotRoomLeft(5, 0, 5) != 0 || SpotRoomLeft(0, 0, 0) != 0 || SpotRoomLeft(0, -1, 5) != 0)
            Fail("T-853 #4: a spot past the limit (the counted spots included) was given");
        if (SpotReleaseRoute(kProdWriterOther) != 1 || SpotReleaseRoute(kProdWriterNone) != 0 || SpotReleaseRoute(kProdWriterHere) != -1)
            Fail("T-853 #2: a release was not kept while no runner is named");
        if (SpotGrantUnused(kSpotGranted, 0, kSpotGrantUseMs + 1) != 1) Fail("T-853 #3: an unused grant was kept");
        if (SpotGrantUnused(kSpotGranted, 1, kSpotGrantUseMs + 1) != 0 || SpotGrantUnused(kSpotGranted, 0, kSpotGrantUseMs) != 0
            || SpotGrantUnused(kSpotAsked, 0, 100000) != 0) Fail("T-853 #3: a grant in use (or too young, or no grant) was released");
        if (SpotStrayYesRelease(1, 0, kSpotNone, kSpotYes) != 0 || SpotStrayYesRelease(1, 0, kSpotRefused, kSpotYes) != 0)
            Fail("T-853 re-check #3: a late yes for another game's worker was released");
        if (SpotStrayYesRelease(1, 1, kSpotNone, kSpotYes) != 1 || SpotStrayYesRelease(1, 1, kSpotRefused, kSpotYes) != 1
            || SpotStrayYesRelease(0, 1, kSpotNone, kSpotYes) != 1) Fail("T-853 #3: a late yes nobody uses was not released");
        if (SpotStrayYesRelease(1, 1, kSpotGranted, kSpotYes) != 0) Fail("T-853 #3: a keep-alive's yes released the spot in use");
        if (SpotStrayYesRelease(0, 0, kSpotNone, kSpotYes) != 0) Fail("T-853 #3: a game released another game's worker's spot");
        if (SpotStrayYesRelease(1, 1, kSpotNone, kSpotFull) != 0 || SpotStrayYesRelease(0, 1, kSpotNone, kSpotRevoked) != 0)
            Fail("T-853 #3: a release went out for an answer that took no spot");
        if (SpotRegrantDue(kSpotGranted, 1, 2) != 1) Fail("T-853 #5: a grant from the old runner was kept");
        if (SpotRegrantDue(kSpotGranted, 1, 1) != 0 || SpotRegrantDue(kSpotGranted, -1, 2) != 0 || SpotRegrantDue(kSpotGranted, 1, -1) != 0
            || SpotRegrantDue(kSpotAsked, 1, 2) != 0) Fail("T-853 #5: a row was asked again with no change of runner known");
        if (SpotKeepAskDue(kSpotGranted, kSpotKeepMs + 1) != 1 || SpotKeepAskDue(kSpotGranted, kSpotKeepMs) != 0 || SpotKeepAskDue(kSpotAsked, 100000) != 0)
            Fail("T-853 #4: the keep-alive ask is not once per kSpotKeepMs for a granted row");
        if (SpotRunnerFreeRow(0, 1, 0, kSpotCountedForgetMs + 1, 1) != 1) Fail("T-853 #4: a counted spot no longer asked for was kept");
        if (SpotRunnerFreeRow(0, 1, 1, 0, 1) != 0) Fail("T-853 #4: a counted spot was freed for having no copy");
        if (SpotRunnerFreeRow(0, 0, 1, 0, 1) != 1 || SpotRunnerFreeRow(1, 1, 0, 0, 1) != 1 || SpotRunnerFreeRow(0, 1, 0, 0, 0) != 1)
            Fail("T-853 #4: a spot gone, released, or no longer run was kept");
        if (SpotRunnerFreeRow(0, 0, 0, 100000, 1) != 0) Fail("T-853 #4: a copy's spot was freed for not being asked for");
        {
            FarmMsg rv; rv.kind = kSpotAnswer; rv.key = "k"; rv.uid = 1; rv.spot = kSpotRevoked;
            std::vector<char> rb;
            FarmMsg ro;
            if (!EncodeFarm(&rb, rv) || DecodeFarm(&rb[0], rb.size(), &ro) != kFarmDecodeOk || ro.spot != kSpotRevoked)
                Fail("T-853 #5: a revoke did not round-trip");
        }
        if (FarmFallbackWriter(0, 0, -1, 1) != kFarmFallNone || FarmFallbackWriter(kFarmUndecidedMs, 0, -1, 1) != kFarmFallNone)
            Fail("T-853 #7: a farm decided (or undecided too briefly) fell back");
        if (FarmFallbackWriter(kFarmUndecidedMs + 1, 0, -1, 1) != kFarmFallHere) Fail("T-853 #7: the area's holder did not grow a long-undecided farm");
        if (FarmFallbackWriter(kFarmUndecidedMs + 1, 0, 2, 0) != kFarmFallOther) Fail("T-853 #7: another game holding the area was not named");
        if (FarmFallbackWriter(kFarmUndecidedMs + 1, 1, -1, 1) != kFarmFallNone) Fail("T-853 #7: a player's farm fell to the area holder");
        if (FarmFallbackWriter(kFarmUndecidedMs + 1, 0, -1, 0) != kFarmFallNone) Fail("T-853 #7: a farm fell back with no area holder known");
        /* the three kinds on the wire */
        const unsigned char kinds[3] = { kSpotAsk, kSpotAnswer, kSpotRelease };
        for (int k = 0; k < 3; ++k)
        {
            FarmMsg sm; sm.kind = kinds[k]; sm.key = "42248-gamedata.base@43,11@545954,-924381,9819"; sm.uid = 77; sm.spot = kSpotFull;
            std::vector<char> sb;
            if (!EncodeFarm(&sb, sm)) { Fail("T-674: a spot message did not encode"); continue; }
            if (IsFarmKind(&sb[0], sb.size()) != 1) Fail("T-674: a spot kind not recognised by the farm peek");
            FarmMsg so;
            if (DecodeFarm(&sb[0], sb.size(), &so) != kFarmDecodeOk || so.kind != sm.kind || so.key != sm.key || so.uid != 77
                || (sm.kind == kSpotAnswer && so.spot != kSpotFull)) Fail("T-674: a spot message did not round-trip");
            for (size_t cut = 0; cut < sb.size(); ++cut)
                if (DecodeFarm(&sb[0], cut, &so) == kFarmDecodeOk) { Fail("T-674: a truncated spot message was accepted"); break; }
            std::vector<char> st(sb); st.push_back(0);
            if (DecodeFarm(&st[0], st.size(), &so) == kFarmDecodeOk) Fail("T-674: a spot message with a trailing byte was accepted");
            sm.uid = 0;
            if (FarmEncodable(sm)) Fail("T-674: a spot message naming no worker was encodable");
        }
        FarmMsg bad; bad.kind = kSpotAnswer; bad.key = "k"; bad.uid = 1; bad.spot = 4;   /* T-853: 3 is kSpotRevoked */
        if (FarmEncodable(bad)) Fail("T-674: an answer with an unknown result was encodable");
        const char k16[2] = { 16, 1 };   /* T-609: the spot kinds are 13-15 */
        if (IsFarmKind(k16, 2) != 0) Fail("T-674: kind 16 was taken as a farm kind");
    }
    /* the rate */
    coopfarm::FarmMsg c(m);
    if (coopfarm::FarmPublishDue(m, c, 0, 0) != 1) Fail("the first state was not due");
    c.harvested = 3;
    if (coopfarm::FarmPublishDue(m, c, 1, 1999) != 0) Fail("a change inside the 2 s gap was due");
    if (coopfarm::FarmPublishDue(m, c, 1, 2000) != 1) Fail("a harvest after the gap was not due");
    c = m; c.ages[3] = 0.975f;
    if (coopfarm::FarmPublishDue(m, c, 1, 5000) != 0) Fail("a growth below the step was due");
    c.ages[3] = 0.985f;
    if (coopfarm::FarmPublishDue(m, c, 1, 5000) != 1) Fail("a growth of one step was not due");
    if (coopfarm::FarmPublishDue(m, m, 1, 29999) != 0 || coopfarm::FarmPublishDue(m, m, 1, 30000) != 2) Fail("the keep-alive is wrong");
    /* apply count and the operate take */
    if (coopfarm::FarmApplyCount(0, 4) != -1 || coopfarm::FarmApplyCount(4, 4) != 4 || coopfarm::FarmApplyCount(3, 4) != 3 || coopfarm::FarmApplyCount(6, 4) != 4)
        Fail("the apply count is wrong");
    float pend = 70.0f;
    if (coopfarm::FarmOpTake(&pend) != 60.0f || pend != 10.0f || coopfarm::FarmOpTake(&pend) != 10.0f || pend != 0.0f || coopfarm::FarmOpTake(&pend) != 0.0f)
        Fail("the operate take is wrong");
}
/* T-189 runboost (H051): src/common/runboost.h - who runs, how much faster, when the raise starts and ends. */
void t_runboost_owner_runs()
{
    using namespace cooprunboost;
    if (!OwnerRuns(999.0f, 93.46f)) Fail("RUN (999) is running");
    if (OwnerRuns(55.0f, 93.46f)) Fail("a jog (55) under the run speed is not boosted (T-195 R4: the command picks the animation, F419)");
    if (OwnerRuns(93.0f, 93.46f)) Fail("a command just under the run speed is not a run");
    if (!OwnerRuns(93.46f, 93.46f)) Fail("a command at the run speed itself is a run");
    if (OwnerRuns(27.0f, 93.46f)) Fail("a walk (27) is not running");
    if (!OwnerRuns(55.0f, 50.0f)) Fail("a slow character jogging at or above its own run speed runs (the engine clamps it to run)");
    if (OwnerRuns(0.0f, 93.0f) || OwnerRuns(-1.0f, 93.0f)) Fail("a stopped / unknown level is not running");
    const float nan = std::sqrt(-1.0f);
    if (OwnerRuns(nan, 93.0f)) Fail("a NaN level is not running");
    if (OwnerRuns(999.0f, nan) || OwnerRuns(999.0f, 0.0f)) Fail("an unreadable run speed is never a run");
}

void t_runboost_boost_speed()
{
    using namespace cooprunboost;
    if (EffectiveSpeed(999.0f, 93.46f) != 93.46f) Fail("a runner's base is its run speed");
    if (EffectiveSpeed(55.0f, 93.46f) != 55.0f) Fail("a jogger's base is its jog");
    if (EffectiveSpeed(55.0f, 0.0f) != 0.0f || EffectiveSpeed(0.0f, 93.0f) != 0.0f) Fail("an unusable base is 0");
    if (BoostSpeed(93.46f, 0.0f) != 93.46f || BoostSpeed(93.46f, -20.0f) != 93.46f) Fail("no lag / ahead gives the base");
    if (std::fabs(BoostSpeed(100.0f, 10.0f) - 112.5f) > 1e-3f) Fail("10 behind = 1.125x (runboost2)");
    if (std::fabs(BoostSpeed(100.0f, 20.0f) - 125.0f) > 1e-3f) Fail("20 behind = 1.25x (runboost2; was 1.1x)");
    if (std::fabs(BoostSpeed(100.0f, 30.0f) - 137.5f) > 1e-3f) Fail("30 behind = 1.375x");
    if (std::fabs(BoostSpeed(100.0f, 40.0f) - 150.0f) > 1e-3f) Fail("40 behind reaches the 1.5x cap exactly");
    if (std::fabs(BoostSpeed(93.46f, 20.0f) - 116.825f) > 1e-2f) Fail("a 93.46 runner 20 behind = 116.8");
    if (std::fabs(BoostSpeed(93.46f, 1.0e6f) - 140.19f) > 1e-2f) Fail("the cap is 1.5x");
    if (std::fabs(BoostSpeed(100.0f, 100.0f) - 150.0f) > 1e-3f) Fail("100 behind stays at the 1.5x cap");
    if (std::fabs(BoostSpeed(50.0f, 40.0f) - 75.0f) > 1e-3f) Fail("the cap is reached at 40 whatever the base");
    if (BoostSpeed(0.0f, 50.0f) != 0.0f) Fail("base 0 gives 0");
    float prev = 0.0f;
    for (int lag = -50; lag <= 400; lag += 5)
    {
        const float b = BoostSpeed(93.46f, (float)lag);
        if (b + 1e-4f < prev) Fail("the boost falls as the lag grows at " + I(lag));
        if (b > 93.46f * kRbMaxMul + 1e-3f || b < 93.46f - 1e-3f) Fail("the boost leaves [base, 1.5 base] at " + I(lag));
        prev = b;
    }
}

void t_runboost_decide()
{
    using namespace cooprunboost;
    if (Decide(true, false, true, true, 10.5f) != kRbEnter) Fail("a runner 10.5 behind enters");
    if (Decide(true, false, true, true, 10.0f) != kRbIdle) Fail("exactly 10 behind does not enter");
    if (Decide(true, true, true, true, 5.0f) != kRbKeep) Fail("5 behind keeps (hysteresis)");
    if (Decide(true, true, true, true, 3.0f) != kRbKeep) Fail("3 behind keeps");
    if (Decide(true, true, true, true, 2.99f) != kRbRestore) Fail("under 3 restores");
    if (Decide(true, true, true, true, -8.0f) != kRbRestore) Fail("AHEAD restores");
    if (Decide(true, false, true, true, -30.0f) != kRbIdle) Fail("ahead never enters");
    if (Decide(true, true, false, true, 50.0f) != kRbRestore) Fail("the owner no longer running restores");
    if (Decide(true, true, true, false, 50.0f) != kRbRestore) Fail("the owner stopped (no heading) restores");
    if (Decide(false, true, true, true, 50.0f) != kRbRestore) Fail("the lever off restores");
    if (Decide(false, false, true, true, 50.0f) != kRbIdle) Fail("the lever off never enters");
    if (Decide(true, false, false, true, 50.0f) != kRbIdle) Fail("a walker is never raised here (walk1's catch-up has it)");
    if (Decide(true, true, true, true, std::sqrt(-1.0f)) != kRbRestore) Fail("a NaN lag restores");
    for (int b = 0; b < 2; ++b)
        for (int e = 0; e < 2; ++e)
            for (int r = 0; r < 2; ++r)
                for (int d = 0; d < 2; ++d)
                    for (int lag = -20; lag <= 40; ++lag)
                    {
                        const int s = Decide(e != 0, b != 0, r != 0, d != 0, (float)lag);
                        if (b == 0 && (s == kRbKeep || s == kRbRestore)) Fail("an idle copy can only enter or stay idle");
                        if (b != 0 && (s == kRbEnter || s == kRbIdle)) Fail("a raised copy can only keep or restore");
                    }
}

void t_runboost_ceiling()
{
    using namespace cooprunboost;
    if (!NeedsCeiling(98.0f, 93.46f)) Fail("a runner's boost needs the ceiling raised");
    if (NeedsCeiling(93.46f, 93.46f)) Fail("the run speed itself fits under the engine's ceiling");
    if (!NeedsCeiling(BoostSpeed(93.46f, 3.0f), 93.46f)) Fail("a runner kept at 3 behind still needs the ceiling (1.0375x)");
    if (NeedsCeiling(98.0f, 0.0f) || NeedsCeiling(std::sqrt(-1.0f), 93.0f)) Fail("an unusable run speed / boost writes nothing");
    if (kRbFullLag != 40.0f || kRbMaxMul != 1.5f || kRbEnterLag != 10.0f || kRbLeaveLag != 3.0f || kRbEaseRate != 200.0f)
        Fail("the runboost2 constants changed");
}

/* runboost2 (T-195 R1): the eased restore - bounded steps down to the run speed, never below, always progressing. */
void t_runboost_ease()
{
    using namespace cooprunboost;
    if (std::fabs(EaseCeiling(140.0f, 93.46f, 0.05f) - 130.0f) > 1e-3f) Fail("a 0.05 s step falls 10 (200 u/s per s)");
    if (std::fabs(EaseCeiling(140.0f, 93.46f, 10.0f) - 130.0f) > 1e-3f) Fail("a huge dt is clamped to 0.05 s");
    if (!(EaseCeiling(140.0f, 93.46f, 0.0f) < 140.0f)) Fail("a zero dt still steps down");
    if (EaseCeiling(95.0f, 93.46f, 0.05f) != 93.46f) Fail("the last step lands on the run speed exactly");
    if (EaseCeiling(90.0f, 93.46f, 0.02f) != 93.46f) Fail("a ceiling already under the run speed restores");
    const float nan = std::sqrt(-1.0f);
    if (EaseCeiling(nan, 93.46f, 0.02f) != 93.46f) Fail("an unusable ceiling restores");
    if (EaseCeiling(140.0f, 0.0f, 0.02f) != 0.0f) Fail("an unusable run speed returns it (the caller restores)");
    float cur = BoostSpeed(93.46f, 1.0e6f);
    int frames = 0;
    while (cur > 93.46f && frames < 1000) { const float n = EaseCeiling(cur, 93.46f, 1.0f / 60.0f); if (n > cur || n < 93.46f) Fail("the ease rose or went under run"); cur = n; ++frames; }
    if (frames < 10 || frames > 20) Fail("the ease from the cap takes ~14 frames at 60 fps, took " + I(frames));
    cur = BoostSpeed(93.46f, 1.0e6f); frames = 0;
    while (cur > 93.46f && frames < 1000) { cur = EaseCeiling(cur, 93.46f, 0.0f); ++frames; }
    if (frames > 60) Fail("a zero dt still ends within 60 steps, took " + I(frames));
}

/* T-178 crawl1 (H050): src/common/getupcrawl.h - the owner up from its ragdoll, the copy ready, MOVE drives a crawler. */
void t_crawl1_owner_up()
{
    for (int prone = -1; prone <= 5; ++prone)
        for (int k = 0; k < 32; ++k)
        {
            const bool rag = (k & 1) != 0, unc = (k & 2) != 0, ko = (k & 4) != 0, car = (k & 8) != 0, pos = (k & 16) != 0;
            const bool want = (prone == 0 || prone == 2) && k == 0;
            if (coopgetup::OwnerUpFromRagdoll(prone, rag, unc, ko, car, pos) != want)
                Fail("OwnerUpFromRagdoll prone=" + I(prone) + " flags=" + I(k) + " expected " + I(want ? 1 : 0));
        }
    if (!coopgetup::OwnerUpFromRagdoll(2, false, false, false, false, false)) Fail("an owner crawling, not a ragdoll, is up");
    if (coopgetup::OwnerUpFromRagdoll(2, true, false, false, false, false)) Fail("an owner crawling but still a ragdoll is not up");
    if (coopgetup::OwnerUpFromRagdoll(1, false, false, false, false, false)) Fail("prone 1 (staying low) is not a get-up");
    if (coopgetup::OwnerUpFromRagdoll(4, false, false, false, false, false)) Fail("prone 4 (knocked out) is not up");
    if (coopgetup::OwnerUpFromRagdoll(0, false, true, false, false, false)) Fail("standing but unconscious (carried) is not up");
}

void t_crawl1_copy_ready()
{
    const unsigned char b[] = { 0, 1, 255 };
    for (int cp = -1; cp <= 5; ++cp)
        for (int op = -1; op <= 5; ++op)
            for (int u = 0; u < 3; ++u)
                for (int d = 0; d < 3; ++d)
                {
                    const bool want = cp == op && (cp == 0 || cp == 2) && b[u] == 0 && b[d] == 0;
                    if (coopgetup::CopyReadyToGetUp(cp, op, b[u], b[d]) != want)
                        Fail("CopyReadyToGetUp copy=" + I(cp) + " owner=" + I(op) + " unc=" + I(b[u]) + " dead=" + I(b[d]));
                }
    if (!coopgetup::CopyReadyToGetUp(2, 2, 0, 0)) Fail("a crippled copy of a crawling owner is ready (T531/T533: refused before)");
    if (coopgetup::CopyReadyToGetUp(2, 0, 0, 0)) Fail("a copy at prone 2 is not ready while its owner stands (prone comes from STATE)");
    if (coopgetup::CopyReadyToGetUp(0, 2, 0, 0)) Fail("a copy at prone 0 is not ready while its owner crawls");
}

void t_crawl1_move_drives()
{
    for (int prone = -1; prone <= 5; ++prone)
        for (int r = 0; r < 2; ++r)
        {
            const bool want = prone == 0 || (prone == 2 && r == 0);
            if (coopgetup::MoveDrivesProne(prone, r != 0) != want)
                Fail("MoveDrivesProne prone=" + I(prone) + " ragdoll=" + I(r));
        }
    for (int prone = -1; prone <= 5; ++prone)
        if (coopgetup::GetupProne(prone) != (prone == 0 || prone == 2)) Fail("GetupProne " + I(prone));
}

void t_crawl1_state_bit()
{
    if (coopgetup::kStateOwnerRagdollBit != (1u << 6)) Fail("the owner isRagdoll bit is bit 6");
    if ((coopgetup::kStateOwnerRagdollBit & coopgetup::kStateLatchMask) != 0) Fail("the ragdoll bit overlaps the medical latch");
    const unsigned int words[] = { 0u, coopgetup::kStateOwnerRagdollBit, coopgetup::kStateLatchMask,
                                   coopgetup::kStateLatchMask | coopgetup::kStateOwnerRagdollBit, 0x22u | coopgetup::kStateOwnerRagdollBit };
    for (int i = 0; i < 5; ++i)
    {
        const float parts[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        std::vector<char> b;
        coopstate::EncodeState(&b, 9u, parts, 1u, 8u, 1.0f, 2, 0, words[i], 0.0f, -1.0f, 0u);
        float out[8];
        coopstate::StateMsg m;
        if (coopstate::DecodeState(&b[0], b.size(), 4u, 8u, out, &m) != coopstate::kStateDecodeOk) { Fail("STATE did not decode"); continue; }
        if (m.latchBits != words[i]) Fail("latchBits word " + I((int)words[i]) + " did not round-trip");
        if (((m.latchBits & coopgetup::kStateOwnerRagdollBit) != 0) != ((words[i] & coopgetup::kStateOwnerRagdollBit) != 0))
            Fail("the owner isRagdoll bit did not round-trip");
        if (m.prone != 2) Fail("prone 2 did not round-trip");
    }
}

/* src/common/sneakwire.h - the owner's stealth mode in STATE bit 7, and what the copy's game does with it. */
void t_sneak_state_bit()
{
    if (coopsneak::kStateOwnerSneakBit != (1u << 7)) Fail("the owner stealth-mode bit is bit 7");
    if ((coopsneak::kStateOwnerSneakBit & coopgetup::kStateLatchMask) != 0) Fail("the sneak bit overlaps the medical latch");
    if ((coopsneak::kStateOwnerSneakBit & coopgetup::kStateOwnerRagdollBit) != 0) Fail("the sneak bit overlaps the ragdoll bit");
    const unsigned int others = coopgetup::kStateLatchMask | coopgetup::kStateOwnerRagdollBit;
    if (coopsneak::WithSneak(others, 1) != (others | coopsneak::kStateOwnerSneakBit)) Fail("WithSneak 1 sets bit 7 and keeps the rest");
    if (coopsneak::WithSneak(others | coopsneak::kStateOwnerSneakBit, 0) != others) Fail("WithSneak 0 clears bit 7 and keeps the rest");
    if (coopsneak::WithSneak(others | coopsneak::kStateOwnerSneakBit, -1) != others) Fail("WithSneak unreadable sends not sneaking");
    if (coopsneak::WithSneak(0u, 1) != coopsneak::kStateOwnerSneakBit) Fail("WithSneak 1 on an empty word");
    const unsigned int words[] = { 0u, coopsneak::kStateOwnerSneakBit, coopsneak::kStateOwnerSneakBit | coopgetup::kStateOwnerRagdollBit,
                                   others | coopsneak::kStateOwnerSneakBit, 0x22u };
    for (int i = 0; i < 5; ++i)
    {
        const float parts[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        std::vector<char> b;
        coopstate::EncodeState(&b, 9u, parts, 1u, 8u, 1.0f, 0, 0, words[i], 0.0f, -1.0f, 0u);
        float out[8];
        coopstate::StateMsg m;
        if (coopstate::DecodeState(&b[0], b.size(), 4u, 8u, out, &m) != coopstate::kStateDecodeOk) { Fail("STATE did not decode"); continue; }
        if (m.latchBits != words[i]) Fail("latchBits word " + I((int)words[i]) + " did not round-trip");
        if (coopsneak::SneakWanted(m.latchBits) != ((words[i] & coopsneak::kStateOwnerSneakBit) != 0))
            Fail("the owner stealth-mode bit did not round-trip");
        if ((m.latchBits & coopgetup::kStateLatchMask) != (words[i] & coopgetup::kStateLatchMask)) Fail("the medical latch changed with the sneak bit");
    }
}

void t_sneak_copy_action()
{
    if (coopsneak::SneakAction(true, 0) != coopsneak::kSneakSetOn) Fail("owner sneaking, copy upright -> set on");
    if (coopsneak::SneakAction(false, 1) != coopsneak::kSneakSetOff) Fail("owner upright, copy sneaking -> set off");
    if (coopsneak::SneakAction(true, 1) != coopsneak::kSneakNone) Fail("both sneaking -> nothing");
    if (coopsneak::SneakAction(false, 0) != coopsneak::kSneakNone) Fail("both upright -> nothing");
    if (coopsneak::SneakAction(true, -1) != coopsneak::kSneakUnreadable) Fail("copy unreadable -> nothing called (on)");
    if (coopsneak::SneakAction(false, -1) != coopsneak::kSneakUnreadable) Fail("copy unreadable -> nothing called (off)");
    if (coopsneak::SneakAction(true, 2) != coopsneak::kSneakUnreadable) Fail("a mode byte other than 0/1 is unreadable");
    if (coopsneak::SneakLogDue(0)) Fail("SneakLogDue 0");
    for (long long n = 1; n <= 8; ++n) if (!coopsneak::SneakLogDue(n)) Fail("SneakLogDue first lines");
    if (coopsneak::SneakLogDue(9) || coopsneak::SneakLogDue(99)) Fail("SneakLogDue after the first lines");
    if (!coopsneak::SneakLogDue(100) || !coopsneak::SneakLogDue(200)) Fail("SneakLogDue every 100th");
}

void t_crawl1_goal_name()
{
    if (!coopgetup::GetupGoalName("Getting up")) Fail("'Getting up' (0x41) is a get-up goal");
    if (!coopgetup::GetupGoalName("Waking up")) Fail("'Waking up' (0x34) is a get-up goal");
    const char* no[] = { "", "Aimless", "Getting u", "Getting up ", "Waking", "Waking up!", "waking up", "Getting upX" };
    for (int i = 0; i < 8; ++i) if (coopgetup::GetupGoalName(no[i])) Fail(std::string("not a get-up goal: '") + no[i] + "'");
    if (coopgetup::GetupGoalName(0)) Fail("a null goal is not a get-up goal");
}

/* installer1: the one line SharedWastelandsSetup.exe adds to / removes from Plugins_x64.cfg. */
std::string InstAdd(const std::string& t, bool* ch) { return coopinst::PcfgAdd(t, ch); }
std::string InstRem(const std::string& t, bool* ch) { return coopinst::PcfgRemove(t, ch); }

void InstExpectAdd(const char* label, const std::string& before, const std::string& after)
{
    bool ch = false;
    const std::string got = InstAdd(before, &ch);
    if (got != after) Fail(std::string(label) + ": add gave [" + got + "] wanted [" + after + "]");
    if (!ch) Fail(std::string(label) + ": add did not report a change");
    bool ch2 = true;
    if (InstAdd(got, &ch2) != got || ch2) Fail(std::string(label) + ": a second add changed the file");
    bool ch3 = false;
    const std::string back = InstRem(got, &ch3);
    if (back != before || !ch3) Fail(std::string(label) + ": remove(add(x)) is not x: [" + back + "]");
    if (!coopinst::PcfgHasOurLine(got) || coopinst::PcfgHasOurLine(back)) Fail(std::string(label) + ": has-line answer wrong");
}

void t_installer1_pcfg_add_remove()
{
    /* the Kenshi-shaped file (stage-6 read 2026-09-29): PluginFolder at the top, RE_Kenshi last, CRLF */
    const std::string k = "# Defines plugins to load\r\n\r\n# Define plugin folder\r\nPluginFolder=.\\\r\n\r\n# Define plugins\r\n"
                          "Plugin=RenderSystem_Direct3D11\r\nPlugin=Plugin_ParticleUniverse\r\nPlugin=Plugin_Terrain\r\nPlugin=RE_Kenshi\r\n";
    InstExpectAdd("kenshi crlf, RE_Kenshi last", k, k + "Plugin=SharedWastelandsLoader\r\n");
    /* LF, and a comment after the plugin list: our line goes right after the LAST Plugin= line, not at the end */
    InstExpectAdd("lf, comment after", "PluginFolder=.\\\nPlugin=A\nPlugin=RE_Kenshi\n# end\n",
                  "PluginFolder=.\\\nPlugin=A\nPlugin=RE_Kenshi\nPlugin=SharedWastelandsLoader\n# end\n");
    /* RE_Kenshi FIRST (its own installer's place): it and every other line are left alone */
    InstExpectAdd("RE_Kenshi first", "Plugin=RE_Kenshi\r\nPlugin=RenderSystem_Direct3D11\r\nPluginFolder=.\\\r\n",
                  "Plugin=RE_Kenshi\r\nPlugin=RenderSystem_Direct3D11\r\nPlugin=SharedWastelandsLoader\r\nPluginFolder=.\\\r\n");
    /* no trailing newline: the file keeps having none */
    InstExpectAdd("crlf, no final newline", "PluginFolder=.\\\r\nPlugin=A", "PluginFolder=.\\\r\nPlugin=A\r\nPlugin=SharedWastelandsLoader");
    InstExpectAdd("lf, no final newline", "Plugin=A\nPlugin=B", "Plugin=A\nPlugin=B\nPlugin=SharedWastelandsLoader");
    /* no Plugin= lines at all: at the end, in the file's own ending */
    InstExpectAdd("no plugin lines, lf", "PluginFolder=.\\\n# x\n", "PluginFolder=.\\\n# x\nPlugin=SharedWastelandsLoader\n");
    InstExpectAdd("no plugin lines, crlf, no final newline", "PluginFolder=.\\\r\n# x", "PluginFolder=.\\\r\n# x\r\nPlugin=SharedWastelandsLoader");
    InstExpectAdd("empty file", "", "Plugin=SharedWastelandsLoader\r\n");
    /* PluginFolder is not a Plugin= line */
    InstExpectAdd("pluginfolder only after", "Plugin=A\r\nPluginFolder=.\\\r\n", "Plugin=A\r\nPlugin=SharedWastelandsLoader\r\nPluginFolder=.\\\r\n");
    /* mixed endings: every other byte kept, our line takes the file's FIRST ending */
    InstExpectAdd("mixed endings", "Plugin=A\r\nPlugin=B\n# c\r\n", "Plugin=A\r\nPlugin=B\nPlugin=SharedWastelandsLoader\r\n# c\r\n");

    /* already present (any spelling Ogre would read as ours): nothing changes */
    const char* present[] = { "Plugin=A\r\nPlugin=SharedWastelandsLoader\r\n", "Plugin = sharedwastelandsloader \n", "Plugin=SharedWastelandsLoader" };
    for (int i = 0; i < 3; ++i)
    {
        bool ch = true;
        if (InstAdd(present[i], &ch) != present[i] || ch) Fail(std::string("already present was changed: ") + present[i]);
    }
    /* remove: only our line - a line the player added after ours, and RE_Kenshi, stay */
    {
        bool ch = false;
        const std::string before = "Plugin=RE_Kenshi\r\nPlugin=SharedWastelandsLoader\r\nPlugin=PlayerAdded\r\n";
        if (InstRem(before, &ch) != "Plugin=RE_Kenshi\r\nPlugin=PlayerAdded\r\n" || !ch) Fail("remove took more than our line");
        bool ch2 = true;
        if (InstRem("Plugin=RE_Kenshi\r\n", &ch2) != "Plugin=RE_Kenshi\r\n" || ch2) Fail("remove with no line of ours changed the file");
        if (InstRem("Plugin=A\nPlugin=SharedWastelandsLoader\nPlugin=SharedWastelandsLoader\n", 0) != "Plugin=A\n") Fail("remove left a copy of our line");
        if (InstRem("Plugin=SharedWastelandsLoaderX\n", 0) != "Plugin=SharedWastelandsLoaderX\n") Fail("remove took a line that is not ours");
    }
    /* install: the loader's previous line goes (any spelling Ogre reads as it), our line is added, nothing else moves */
    {
        const std::string old = std::string("Plugin=") + swnames::kOldLoaderPlugin;
        bool ch = false;
        const std::string before = "Plugin=RE_Kenshi\r\n" + old + "\r\nPlugin=PlayerAdded\r\n";
        if (coopinst::PcfgRemoveOld(before, &ch) != "Plugin=RE_Kenshi\r\nPlugin=PlayerAdded\r\n" || !ch) Fail("the old loader line was not removed alone");
        bool ch2 = true;
        if (coopinst::PcfgRemoveOld("Plugin=RE_Kenshi\r\nPlugin=SharedWastelandsLoader\r\n", &ch2) != "Plugin=RE_Kenshi\r\nPlugin=SharedWastelandsLoader\r\n" || ch2)
            Fail("removing the old loader line touched a file without it");
        if (coopinst::PcfgRemoveOld("Plugin = kenshimultiplayerloader \nPlugin=A\n", 0) != "Plugin=A\n") Fail("the old loader line in another case stayed");
        if (coopinst::PcfgRemoveOld("Plugin=KenshiMultiplayerLoaderX\n", 0) != "Plugin=KenshiMultiplayerLoaderX\n") Fail("removed a line that is not the old loader's");
        if (coopinst::PcfgRemoveOld("Plugin=A\r\n" + old, 0) != "Plugin=A") Fail("the old loader line as the last line left its newline");
        bool c3 = false, c4 = false;
        const std::string inst = InstAdd(coopinst::PcfgRemoveOld("Plugin=RE_Kenshi\r\n" + old + "\r\n", &c3), &c4);
        if (inst != "Plugin=RE_Kenshi\r\nPlugin=SharedWastelandsLoader\r\n" || !c3 || !c4) Fail("install over the old line gave [" + inst + "]");
        if (std::string(swnames::kOldLoaderPlugin) == coopinst::LoaderPluginName()) Fail("the old and new loader names are the same");
    }
}

void t_installer1_steam_paths()
{
    const std::string vdfNew = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"label\"\t\t\"\"\n"
                               "\t\t\"apps\"\n\t\t{\n\t\t\t\"233860\"\t\t\"12345\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n";
    std::vector<std::string> p = coopinst::VdfLibraryPaths(vdfNew);
    if (p.size() != 2 || p[0] != "C:\\Program Files (x86)\\Steam" || p[1] != "D:\\SteamLibrary") Fail("new-shape libraryfolders.vdf not read");
    const std::string vdfOld = "\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1600000000\"\n\t\"ContentStatsID\"\t\t\"-123\"\n\t\"1\"\t\t\"E:\\\\Games\\\\Steam\"\n}\n";
    p = coopinst::VdfLibraryPaths(vdfOld);
    if (p.size() != 1 || p[0] != "E:\\Games\\Steam") Fail("old-shape libraryfolders.vdf not read");
    if (!coopinst::VdfLibraryPaths("").empty() || !coopinst::VdfLibraryPaths("\"path\" \"relative\\\\x\"").empty()) Fail("a non-path was read as a library");

    if (coopinst::WorkshopGameFolder("D:\\SteamLibrary\\steamapps\\workshop\\content\\233860\\3001234567") != "D:\\SteamLibrary\\steamapps\\common\\Kenshi")
        Fail("the Workshop layout did not name the game folder");
    if (coopinst::WorkshopGameFolder("C:/Steam/SteamApps/Workshop/Content/233860/42") != "C:\\Steam\\steamapps\\common\\Kenshi") Fail("slashes/case in the Workshop layout");
    if (coopinst::WorkshopGameFolder("D:\\SteamLibrary\\steamapps\\workshop\\content\\233860\\") != "") Fail("no item id is not the layout");
    if (coopinst::WorkshopGameFolder("D:\\SteamLibrary\\steamapps\\workshop\\content\\294100\\1") != "") Fail("another game's Workshop folder was taken");
    if (coopinst::WorkshopGameFolder("C:\\Kenshi\\mods\\Shared Wastelands") != "") Fail("a mods folder is not the Workshop layout");
}

/* mig6 (owner 118): the loader's marker file - src/common/loadermarker.h, the code SharedWastelandsLoader.dll runs. */
bool MarkerOk(const char* text, const char* dll, const char* start)
{
    cooploader::MarkerResult r;
    cooploader::MarkerParse(text, (long)std::strlen(text), &r);
    return r.ok == 1 && std::strcmp(r.dll, dll) == 0 && std::strcmp(r.start, start) == 0 && r.why[0] == 0 && r.format == 1;
}
bool MarkerRefused(const char* text, long len, int line, const char* whyPart)
{
    cooploader::MarkerResult r;
    cooploader::MarkerParse(text, len, &r);
    return r.ok == 0 && r.badLine == line && std::strstr(r.why, whyPart) != 0 && r.dll[0] == 0 && r.start[0] == 0;
}
bool MarkerRefused(const char* text, int line, const char* whyPart) { return MarkerRefused(text, (long)std::strlen(text), line, whyPart); }

void t_mig6_marker_parse()
{
    /* the shipped file's shape: comments, LF */
    if (!MarkerOk("# shared-wastelands\nformat=1\ndll=SharedWastelands.dll\nstart=coopEarlyStart\n", "SharedWastelands.dll", "coopEarlyStart"))
        Fail("the shipped marker's shape did not parse");
    /* CRLF, a UTF-8 BOM, spaces and tabs around keys and values, blank lines, no final newline, any key order */
    if (!MarkerOk("\xEF\xBB\xBF\r\n  start = coopEarlyStart\t\r\n\r\n\tdll=\tSharedWastelands.dll \r\nformat=1", "SharedWastelands.dll", "coopEarlyStart"))
        Fail("CRLF / BOM / trimming / order / no final newline did not parse");
    /* an unknown key is IGNORED and counted (a later loader may add keys) */
    {
        cooploader::MarkerResult r;
        const char* t = "format=1\ndll=a.dll\nstart=s\nversion=2.0\nnote=hello world\n";
        cooploader::MarkerParse(t, (long)std::strlen(t), &r);
        if (r.ok != 1 || r.ignoredKeys != 2) Fail("unknown keys were not ignored-and-counted (ok " + I(r.ok) + ", ignored " + I(r.ignoredKeys) + ")");
    }
    /* .DLL in any case is a DLL name; 64 characters is the limit */
    if (!MarkerOk("format=1\ndll=Mod-Name_2.DLL\nstart=_s9\n", "Mod-Name_2.DLL", "_s9")) Fail("an upper-case .DLL name was refused");
    {
        std::string n64(60, 'a'); n64 += ".dll";
        std::string n65(61, 'a'); n65 += ".dll";
        if (!MarkerOk(("format=1\ndll=" + n64 + "\nstart=s\n").c_str(), n64.c_str(), "s")) Fail("a 64-character DLL name was refused");
        if (!MarkerRefused(("format=1\ndll=" + n65 + "\nstart=s\n").c_str(), 2, "dll=")) Fail("a 65-character DLL name was accepted");
        std::string i64(64, 'x'), i65(65, 'x');
        if (!MarkerOk(("format=1\ndll=a.dll\nstart=" + i64 + "\n").c_str(), "a.dll", i64.c_str())) Fail("a 64-character export name was refused");
        if (!MarkerRefused(("format=1\ndll=a.dll\nstart=" + i65 + "\n").c_str(), 3, "start=")) Fail("a 65-character export name was accepted");
    }
    /* the DLL must be a plain file name in the marker's own folder */
    {
        const char* bad[] = { "..\\x.dll", "../x.dll", "sub\\x.dll", "sub/x.dll", "C:x.dll", "C:\\x.dll", ".x.dll", "a..b.dll",
                              "x.exe", "x.dll.txt", "x dll.dll", "x.dl", ".dll", "", "x\x01.dll" };
        int i;
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i)
            if (!MarkerRefused((std::string("format=1\ndll=") + bad[i] + "\nstart=s\n").c_str(), 2, "dll="))
                Fail(std::string("dll='") + bad[i] + "' was not refused on line 2");
    }
    /* the export must be a C identifier */
    {
        const char* bad[] = { "9start", "start()", "a-b", "a b", "", "?startPlugin@@YAXXZ" };
        int i;
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i)
            if (!MarkerRefused((std::string("format=1\ndll=a.dll\nstart=") + bad[i] + "\n").c_str(), 3, "start="))
                Fail(std::string("start='") + bad[i] + "' was not refused on line 3");
    }
    /* format: 1 only, a number, once */
    {
        cooploader::MarkerResult r;
        const char* t = "format=2\ndll=a.dll\nstart=s\n";
        cooploader::MarkerParse(t, (long)std::strlen(t), &r);
        if (r.ok != 0 || r.format != 2 || r.badLine != 1 || std::strstr(r.why, "not 1") == 0) Fail("format=2 was not refused as a format this loader does not read");
    }
    if (!MarkerRefused("format=one\ndll=a.dll\nstart=s\n", 1, "not a number")) Fail("format=one was accepted");
    if (!MarkerRefused("format=\ndll=a.dll\nstart=s\n", 1, "not a number")) Fail("an empty format= was accepted");
    if (!MarkerRefused("format=0000001\ndll=a.dll\nstart=s\n", 1, "not a number")) Fail("a 7-digit format= was accepted");
    /* a known key twice, a line without '=', a missing key */
    if (!MarkerRefused("format=1\nformat=1\ndll=a.dll\nstart=s\n", 2, "twice")) Fail("format= twice was accepted");
    if (!MarkerRefused("format=1\ndll=a.dll\ndll=b.dll\nstart=s\n", 3, "twice")) Fail("dll= twice was accepted");
    if (!MarkerRefused("format=1\ndll=a.dll\nstart=s\nstart=t\n", 4, "twice")) Fail("start= twice was accepted");
    if (!MarkerRefused("format=1\n# ok\nSharedWastelands.dll\nstart=s\n", 3, "key=value")) Fail("a line with no '=' was accepted");
    if (!MarkerRefused("dll=a.dll\nstart=s\n", 0, "format= is missing")) Fail("a missing format= was accepted");
    if (!MarkerRefused("format=1\nstart=s\n", 0, "dll= is missing")) Fail("a missing dll= was accepted");
    if (!MarkerRefused("format=1\ndll=a.dll\n", 0, "start= is missing")) Fail("a missing start= was accepted");
    if (!MarkerRefused("", 0, "format= is missing")) Fail("an empty file was accepted");
    if (!MarkerRefused("# only a comment\r\n\r\n", 0, "format= is missing")) Fail("a comment-only file was accepted");
    /* keys are exact and case-sensitive: DLL= is an unknown key, so dll= is then missing */
    if (!MarkerRefused("format=1\nDLL=a.dll\nstart=s\n", 0, "dll= is missing")) Fail("DLL= was taken as dll=");
    /* whole-file refusals: a NUL byte, over 4096 bytes, unreadable */
    {
        const char withNul[] = "format=1\ndll=a.dll\0\nstart=s\n";
        if (!MarkerRefused(withNul, (long)(sizeof(withNul) - 1), 0, "NUL")) Fail("a NUL byte was accepted");
        std::string big = "format=1\ndll=a.dll\nstart=s\n#";
        big += std::string(4096 - big.size(), 'x');
        if (!MarkerOk(big.c_str(), "a.dll", "s")) Fail("a 4096-byte marker was refused");
        big += "x";
        if (!MarkerRefused(big.c_str(), 0, "larger than 4096")) Fail("a 4097-byte marker was accepted");
        if (!MarkerRefused(0, -1, 0, "could not be read")) Fail("an unreadable file was not refused");
    }
    /* the marker's file name is the one the package ships (mod-package\Shared Wastelands\) */
    if (std::strcmp(cooploader::MarkerFileName(), "shared-wastelands.loader.txt") != 0) Fail("the marker's file name changed");
}

/* mig6 fold (review of 7d96a7d, F1; owner 123/124): Kenshi's mod list, data\mods.cfg - src/common/modsenabled.h, the code
   BOTH roads run (SharedWastelandsLoader.dll and SharedWastelands.dll's startPluginOn/coopEarlyStart). */
bool ModsOn(const char* text, const char* name) { return coopmods::ModsCfgLists(text, text ? (long)std::strlen(text) : 0, name); }

void t_mig6_modscfg_lists()
{
    const char* n = "Shared Wastelands.mod";
    /* every bench instance's file, byte for byte */
    if (!ModsOn("Shared Wastelands.mod\r\n", n)) Fail("the bench instances' mods.cfg (CRLF) did not list Shared Wastelands.mod");
    /* LF, other mods around it, CR alone */
    if (!ModsOn("a.mod\nShared Wastelands.mod\nb.mod\n", n)) Fail("an LF file did not list it");
    if (!ModsOn("a.mod\rShared Wastelands.mod\r", n)) Fail("a CR-only file did not list it");
    if (!ModsOn("a.mod\r\n\r\nb.mod\r\nShared Wastelands.mod\r\n", n)) Fail("it was missed after a blank line");
    /* a UTF-8 BOM at the start is skipped - and only at the start */
    if (!ModsOn("\xEF\xBB\xBFShared Wastelands.mod\r\n", n)) Fail("a BOM before the first line hid it");
    if (!ModsOn("\xEF\xBB\xBF" "a.mod\r\nShared Wastelands.mod", n)) Fail("a BOM file with it second did not list it");
    if (ModsOn("a.mod\r\n\xEF\xBB\xBFShared Wastelands.mod\r\n", n)) Fail("a BOM in the middle of the file was skipped");
    /* spaces and tabs around the line */
    if (!ModsOn("  Shared Wastelands.mod \t\r\n", n)) Fail("spaces/tabs around the line hid it");
    if (!ModsOn("\tShared Wastelands.mod", n)) Fail("a leading tab hid it");
    /* case (ASCII) */
    if (!ModsOn("shared WASTELANDS.MOD\r\n", n)) Fail("a different case hid it");
    if (!ModsOn("Shared Wastelands.mod\r\n", "SHARED WASTELANDS.mod")) Fail("an upper-case name did not match");
    /* the name as PART of a line never matches */
    if (ModsOn("my-Shared Wastelands.mod\r\n", n)) Fail("a longer line ending in the name matched");
    if (ModsOn("Shared Wastelands.mod.bak\r\n", n)) Fail("a longer line starting with the name matched");
    if (ModsOn("Shared Wastelands.mod x\r\n", n)) Fail("the name followed by a word matched");
    if (ModsOn("Shared Wastelands.mo\r\n", n)) Fail("a shorter line matched");
    if (ModsOn("kenshi coop.mod\r\n", n)) Fail("a different name matched");
    /* the last line without a newline */
    if (!ModsOn("a.mod\r\nShared Wastelands.mod", n)) Fail("the last line without a newline was missed");
    /* empty, null, blank, a length that cuts the name */
    if (ModsOn("", n)) Fail("an empty file listed it");
    if (ModsOn(0, n)) Fail("a null text listed it");
    if (ModsOn("\r\n \r\n\t\n", n)) Fail("a blank-lines file listed it");
    if (coopmods::ModsCfgLists("Shared Wastelands.mod", -1, n)) Fail("a negative length listed it");
    if (coopmods::ModsCfgLists("Shared Wastelands.mod\r\n", 10, n)) Fail("text past the given length was read");
    if (ModsOn("Shared Wastelands.mod\r\n", "")) Fail("an empty name matched");
    if (ModsOn("\r\n", "")) Fail("an empty name matched an empty line");
    if (coopmods::ModsCfgLists("Shared Wastelands.mod", 15, 0)) Fail("a null name matched");
    /* non-ASCII bytes compare exactly (only ASCII letters ignore case) */
    if (!ModsOn("caf\xC3\xA9.MOD\r\n", "CAF\xC3\xA9.mod")) Fail("a UTF-8 name did not match itself");
    if (ModsOn("caf\xC3\x89.mod\r\n", "caf\xC3\xA9.mod")) Fail("a non-ASCII letter was case-folded");
    /* the size both roads refuse above (fail closed) */
    if (coopmods::kModsCfgMaxBytes != 65536) Fail("the mods.cfg size limit changed");
    {
        std::string big(40000, '\n');
        big += "Shared Wastelands.mod";
        if (!coopmods::ModsCfgLists(big.data(), (long)big.size(), n)) Fail("a large list did not find it at its end");
    }
}

/* Kenshi's mod list as both roads judge it after the launcher's OK (src/common/modsenabled.h): whether the bytes read can
   be judged at all (fail closed), and the line rules the launcher's text-mode file and hand-edited files need. */
void t_t522_modscfg_after_ok()
{
    const char* n = "Shared Wastelands.mod";
    /* fail closed: not opened, not read, larger than 64 KB */
    if (coopmods::ModsCfgReadState(false, false, 0) != coopmods::kModsCfgNotOpened) Fail("a list that did not open was usable");
    if (coopmods::ModsCfgReadState(false, true, 30) != coopmods::kModsCfgNotOpened) Fail("a list that did not open was judged on its bytes");
    if (coopmods::ModsCfgReadState(true, false, 30) != coopmods::kModsCfgReadFailed) Fail("a failed read was usable");
    if (coopmods::ModsCfgReadState(true, true, 65537) != coopmods::kModsCfgTooLarge) Fail("a list over 64 KB was usable");
    if (coopmods::ModsCfgReadState(true, true, 65536) != coopmods::kModsCfgUsable) Fail("a list of exactly 64 KB was refused");
    if (coopmods::ModsCfgReadState(true, true, 0) != coopmods::kModsCfgUsable) Fail("an empty list was not judged (it lists nothing)");
    if (coopmods::ModsCfgReadState(true, true, 23) != coopmods::kModsCfgUsable) Fail("an ordinary list was refused");
    if (std::strcmp(coopmods::ModsCfgStateWhy(coopmods::kModsCfgNotOpened), "could not be opened") != 0
        || std::strcmp(coopmods::ModsCfgStateWhy(coopmods::kModsCfgReadFailed), "could not be read") != 0
        || std::strcmp(coopmods::ModsCfgStateWhy(coopmods::kModsCfgTooLarge), "is larger than 64 KB") != 0
        || coopmods::ModsCfgStateWhy(coopmods::kModsCfgUsable)[0] != 0) Fail("a mod-list state's words changed");
    /* an empty list (every mod unticked) switches it off */
    if (ModsOn("", n)) Fail("an empty list switched it on");
    /* the launcher writes in text mode (CRLF); LF and CR alone are read the same */
    if (!ModsOn("a.mod\r\nShared Wastelands.mod\r\nb.mod\r\n", n)) Fail("the launcher's CRLF list did not list it");
    if (!ModsOn("a.mod\nShared Wastelands.mod\nb.mod\n", n)) Fail("an LF list did not list it");
    if (!ModsOn("a.mod\rShared Wastelands.mod\rb.mod\r", n)) Fail("a CR list did not list it");
    if (ModsOn("a.mod\r\nb.mod\r\n", n) || ModsOn("a.mod\nb.mod\n", n)) Fail("a list without it listed it");
    /* the name on several lines is still one switch */
    if (!ModsOn("Shared Wastelands.mod\r\na.mod\r\nShared Wastelands.mod\r\n", n)) Fail("the name on two lines did not list it");
    if (!ModsOn("shared wastelands.mod\nSHARED WASTELANDS.MOD\n", n)) Fail("the name twice in two cases did not list it");
}

/* The loader's Ogre plugin name (src/common/ogreplugin.h) is laid out by hand as VS2010's std::string: this suite is built
   with the same VS2010 library as OgreMain_x64.dll (MSVCP100), so a real std::string here has the layout Ogre reads. */
void t_t522_ogre_plugin_name_layout()
{
    if (sizeof(std::string) != sizeof(swogre::Vs2010String)) Fail("std::string is not the size of the hand-built string");
    if (offsetof(swogre::Vs2010String, size) != 16 || offsetof(swogre::Vs2010String, capacity) != 24) Fail("the hand-built string's fields moved");
    /* a real std::string's fields where the hand-built one has them */
    {
        const std::string real("SWLoader");
        const unsigned char* raw = (const unsigned char*)&real;
        unsigned long long size = 0, cap = 0;
        std::memcpy(&size, raw + 16, 8);
        std::memcpy(&cap, raw + 24, 8);
        if ((const void*)real.c_str() != (const void*)raw) Fail("a short std::string's text is not at its start");
        if (size != 8 || cap != 15) Fail("a short std::string's length / capacity are not at +16 / +24");
    }
    /* the hand-built string read AS a std::string */
    swogre::Vs2010String h;
    if (!swogre::Vs2010StringSet(&h, "SWLoader")) Fail("an 8-character name was refused");
    const std::string& asStd = *reinterpret_cast<const std::string*>(&h);
    if (asStd.size() != 8 || asStd.capacity() != 15 || asStd != "SWLoader" || asStd.c_str() != h.buf) Fail("the hand-built name does not read as std::string \"SWLoader\"");
    if (std::string("Installing plugin: ") + asStd != "Installing plugin: SWLoader") Fail("Ogre's log line would not read the name");
    if (!swogre::Vs2010StringSet(&h, "123456789012345") || asStd.size() != 15 || asStd != "123456789012345") Fail("a 15-character name was refused");
    if (swogre::Vs2010StringSet(&h, "1234567890123456") || asStd.size() != 0 || !asStd.empty()) Fail("a 16-character name was laid out in the buffer");
    if (swogre::Vs2010StringSet(&h, 0) || !asStd.empty()) Fail("a null name was laid out");
    if (!swogre::Vs2010StringSet(&h, "") || !asStd.empty()) Fail("an empty name was refused");
    /* the vtable slots and export names the loader uses */
    if (swogre::kSlotGetName != 1 || swogre::kSlotInstall != 2 || swogre::kSlotInitialise != 3 || swogre::kSlotShutdown != 4
        || swogre::kSlotUninstall != 5 || swogre::kPluginSlots != 6) Fail("an Ogre::Plugin slot moved");
    if (std::strcmp(swogre::RootInstallPluginExport(), "?installPlugin@Root@Ogre@@QEAAXPEAVPlugin@2@@Z") != 0
        || std::strcmp(swogre::RootUninstallPluginExport(), "?uninstallPlugin@Root@Ogre@@QEAAXPEAVPlugin@2@@Z") != 0
        || std::strcmp(swogre::RootGetSingletonPtrExport(), "?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ") != 0
        || std::strcmp(swogre::PluginVtableExport(), "??_7Plugin@Ogre@@6B@") != 0) Fail("an OgreMain export name changed");
}

/* The loader copies Ogre::Plugin's complete-object locator into the slot before its hand-built vtable only when
   swogre::IsObjectLocator accepts it (src/common/ogreplugin.h): a fake image with one locator in it. */
void T522SetU32(unsigned char* p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

void t_t522_ogre_object_locator()
{
    static unsigned char img[256];
    std::memset(img, 0, sizeof(img));
    const unsigned int at = 64;                       /* the locator's image-relative address */
    unsigned char* col = img + at;
    T522SetU32(col + 0, 1); T522SetU32(col + 4, 0); T522SetU32(col + 8, 0); T522SetU32(col + 12, 120); T522SetU32(col + 16, 160); T522SetU32(col + 20, at);
    if (!swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a well-formed locator was refused");
    /* the fields that make it a whole object's locator in this image */
    T522SetU32(col + 0, 0);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("signature 0 (an x86 locator) was accepted");
    T522SetU32(col + 0, 1); T522SetU32(col + 4, 8);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a locator of a base at offset 8 was accepted");
    T522SetU32(col + 4, 0); T522SetU32(col + 20, at + 8);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a locator whose self address is not its own was accepted");
    T522SetU32(col + 20, at); T522SetU32(col + 12, 0);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a locator with no type descriptor was accepted");
    T522SetU32(col + 12, 256);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a type descriptor past the image was accepted");
    T522SetU32(col + 12, 120); T522SetU32(col + 16, 300);
    if (swogre::IsObjectLocator(img, sizeof(img), col)) Fail("a hierarchy descriptor past the image was accepted");
    T522SetU32(col + 16, 160);
    if (!swogre::IsObjectLocator(img, sizeof(img), col)) Fail("the restored locator was refused");
    /* where the pointer may point: never past the image, never misaligned, never null */
    if (swogre::IsObjectLocator(img, sizeof(img), 0)) Fail("a null pointer was accepted");
    if (swogre::IsObjectLocator(0, sizeof(img), col)) Fail("a null image was accepted");
    if (swogre::IsObjectLocator(img + 8, sizeof(img) - 8, img)) Fail("a pointer before the image was accepted");
    if (swogre::IsObjectLocator(img, at + 23, col)) Fail("a locator running past the image's end was accepted");
    if (swogre::IsObjectLocator(img, 16, col)) Fail("an image shorter than a locator was accepted");
    T522SetU32(col + 20, 0);
    if (swogre::IsObjectLocator(img, sizeof(img), img + 2)) Fail("a misaligned pointer was accepted");
}

/* M15 (T-197): the world server's once-a-second jobs on a fixed schedule (src/common/looptick.h). A late pass
   does not shift the grid; a pass a whole period late skips (no catch-up burst) and counts it. */
void t_m15_fixed_schedule_keeps_its_grid()
{
    cooptick::FixedSchedule s(1000000);
    if (s.Due(0) || s.Due(999999999)) Fail("a schedule that was never started is due");
    s.Start(5000);
    if (!s.Due(5000)) Fail("the first pass is not due at start");
    long long sk = -1;
    if (s.Ran(5000, &sk) != 0 || sk != 0) Fail("an on-time first pass is late or skipped");
    if (s.Due(1004999) || !s.Due(1005000)) Fail("the next pass is not due exactly one period later");
    if (s.UsUntilDue(905000) != 100000 || s.UsUntilDue(2000000) != 0) Fail("UsUntilDue is wrong");
    if (s.Ran(1005300, &sk) != 300 || sk != 0) Fail("a pass 300 us late is not recorded as 300 us late");
    if (s.DueAt() != 2005000) Fail("a late pass shifted the grid");
    if (s.Ran(5505000, &sk) != 3500000 || sk != 3) Fail("a pass 3.5 s late did not skip the three slots it missed");
    if (s.DueAt() != 6005000) Fail("after a skip the next pass is not on the grid");
    if (s.Ran(7005000, &sk) != 1000000 || sk != 1 || s.DueAt() != 8005000) Fail("a pass exactly one period late");
    cooptick::TickStats st = s.Take();
    if (st.passes != 4 || st.lateMaxUs != 3500000 || st.lateSumUs != 300 + 3500000 + 1000000 || st.skipped != 4) Fail("the stats are wrong");
    st = s.Take();
    if (st.passes != 0 || st.lateMaxUs != 0 || st.lateSumUs != 0 || st.skipped != 0) Fail("the stats did not restart after a report line");
}
/* A pass ends at its budget or when the jobs fall due, whichever is first; the wait never reaches past it. */
void t_m15_pass_budget_ends_at_budget_or_due()
{
    cooptick::PassBudget b(50000);
    b.Begin(0, 1000000);
    if (b.WaitMs(0, 50) != 50 || b.WaitMs(0, 2) != 2) Fail("a fresh pass does not allow the full wait");
    if (b.WaitMs(49500, 50) != 1) Fail("a sub-ms remainder is not rounded up to 1 ms");
    if (b.Spent(49999) || !b.Spent(50000) || b.WaitMs(50000, 50) != 0) Fail("the pass does not end at its 50 ms budget");
    b.Begin(100000, 10000);
    if (b.WaitMs(100000, 50) != 10 || b.Spent(109999) || !b.Spent(110000)) Fail("the pass does not end when the jobs fall due");
    b.Begin(0, 0);
    if (!b.Spent(0) || b.WaitMs(0, 50) != 0) Fail("a pass begun with the jobs already due is not over at once");
    b.Begin(0, 1000000); for (int i = 0; i < 7; ++i) b.Count();
    b.Begin(0, 1000000); b.Count(); b.Count();
    if (b.Events() != 2) Fail("Begin did not restart the pass's count");
    if (b.TakeMax() != 7) Fail("eventsPerPassMax is not the largest pass");
    if (b.TakeMax() != 0) Fail("eventsPerPassMax did not restart after a report line");
}
/* The loop's shape (store_main.cpp main) against a message ALWAYS waiting, each taking 700 us to handle, and jobs
   taking 4 ms: the jobs still run once a second, each less than one message late, none skipped. Before M15 they
   would not have run at all - the network wait never came back empty. */
void t_m15_flood_cannot_starve_the_jobs()
{
    cooptick::FixedSchedule s(1000000); cooptick::PassBudget b(50000);
    long long now = 123;
    s.Start(now);
    while (now < 10000000)
    {
        if (s.Due(now)) { s.Ran(now, 0); now += 4000; }
        b.Begin(now, s.UsUntilDue(now));
        for (;;) { now += 700; b.Count(); if (b.Spent(now)) break; }
    }
    const cooptick::TickStats st = s.Take();
    if (st.passes != 10) Fail("the jobs did not run once a second under a flood");
    if (st.lateMaxUs >= 700) Fail("a pass ran later than one message's handling");
    if (st.skipped != 0) Fail("a pass was skipped under a flood");
    if (b.TakeMax() > 50000 / 700 + 1) Fail("a pass handled more events than its budget allows");
}

/* M14 (T-197): the world server's write queue (src/common/writequeue.h) - one writer, strictly in turn, and a
   failed job loses nothing queued after it. */
void t_m14_fifo_order_and_failure()
{
    coopwq::Fifo<int> q;
    if (!q.Idle() || q.Depth() != 0) Fail("a new queue is not idle");
    for (int i = 1; i <= 5; ++i) q.Push(i);
    if (q.Depth() != 5) Fail("the depth after five pushes is not 5");
    int got = 0, expect = 1;
    while (q.Pop(&got))
    {
        if (got != expect) Fail("the writer was handed a job out of queue order");
        int again = 0;
        if (q.Pop(&again)) Fail("a second job was handed out while one was still being written");
        if (q.Depth() != 6 - expect) Fail("the depth does not count the job being written");
        q.Done(got != 2);   /* job 2 fails */
        ++expect;
    }
    if (expect != 6) Fail("a failed job stopped the jobs queued after it");
    if (q.Jobs() != 5 || q.Failed() != 1) Fail("jobs/failed are not 5/1");
    if (!q.Idle()) Fail("the queue is not idle once every job is done");
    if (q.TakeMaxDepth() != 5) Fail("maxDepth is not 5");
    if (q.TakeMaxDepth() != 0) Fail("maxDepth did not restart from the current depth after a report line");
}
/* The loop's gate: one job per key out, and the messages held behind it come back oldest first. */
void t_m14_keygate_holds_in_arrival_order()
{
    coopwq::KeyGate<int> g;
    if (!g.Start("a")) Fail("a free key could not start a job");
    if (g.Start("a")) Fail("a second job started for a key that already has one out");
    if (g.Busy("b")) Fail("an untouched key reads busy");
    g.Hold("a", 1); g.Hold("a", 2); g.Hold("a", 3);
    if (g.HeldCount() != 3) Fail("three held messages do not count 3");
    int m = 0;
    if (g.TakeHeld("a", &m)) Fail("a held message was handed out while its key's job was still out");
    g.Finish("a");
    if (!g.TakeHeld("a", &m) || m != 1) Fail("the oldest held message did not come first");
    if (!g.Start("a")) Fail("the released key could not start again");   /* that message queued a job */
    if (g.TakeHeld("a", &m)) Fail("the next held message jumped the job that is out");
    g.Finish("a");
    if (!g.TakeHeld("a", &m) || m != 2) Fail("held message 2 came out of order");
    if (!g.TakeHeld("a", &m) || m != 3) Fail("held message 3 came out of order (a refused message queues nothing, so the next one follows at once)");
    if (g.TakeHeld("a", &m) || g.HeldCount() != 0 || g.BusyCount() != 0) Fail("the gate did not empty");
}
/* M14 fold (review finding 3): held POSITION-ONLY updates merge - the newer from the same connection with a
   strictly newer stamp replaces the one held right before it - and nothing else ever does: a full record, a
   delete, another game, a new connection in the slot, an equal or older stamp, or a merge across a message
   in between. The rest comes out in arrival order; held, heldBytes and heldMax count what is really held. */
void t_m14_held_position_merge()
{
    coopwq::KeyGate<int> g;
    int gameA = 0, gameB = 0;
    if (!g.Start("k")) Fail("a free key could not start a job");
    coopwq::HeldTag pa; pa.kind = coopwq::kHeldPosition; pa.sender = &gameA; pa.connect = 7; pa.bytes = 10;
    pa.stamp = 100; if (g.Hold("k", 1, pa)) Fail("the first held position merged with nothing");
    pa.stamp = 200; if (!g.Hold("k", 2, pa)) Fail("a newer position from the same connection did not replace the older one");
    if (g.HeldCount() != 1 || g.HeldBytes() != 10 || g.Merged() != 1) Fail("one merge did not leave one held message of 10 bytes");
    pa.stamp = 200; if (g.Hold("k", 3, pa)) Fail("a position with an EQUAL stamp merged (the loop would refuse it - it must be held and decided)");
    coopwq::HeldTag full; full.kind = coopwq::kHeldOther; full.sender = &gameA; full.connect = 7; full.bytes = 1000; full.stamp = 300;
    if (g.Hold("k", 4, full)) Fail("a full record merged");
    pa.stamp = 400; if (g.Hold("k", 5, pa)) Fail("a position merged across a full record");
    coopwq::HeldTag pb = pa; pb.sender = &gameB; pb.stamp = 500;
    if (g.Hold("k", 6, pb)) Fail("a position from another game merged");
    coopwq::HeldTag pc = pa; pc.connect = 8; pc.stamp = 600;
    if (g.Hold("k", 7, pc)) Fail("a position from a new connection in the same slot merged");
    pc.stamp = 550; if (g.Hold("k", 8, pc)) Fail("an OLDER position replaced a newer one");
    coopwq::HeldTag del; del.kind = coopwq::kHeldOther; del.sender = &gameA; del.connect = 7; del.bytes = 5; del.stamp = 700;
    if (g.Hold("k", 9, del)) Fail("a delete merged");
    pa.stamp = 800; if (g.Hold("k", 10, pa)) Fail("a position merged into a delete");
    pa.stamp = 900; if (!g.Hold("k", 11, pa)) Fail("a newer position did not replace the one right before it");
    if (g.HeldCount() != 9 || g.HeldBytes() != 1075 || g.Merged() != 2) Fail("held/heldBytes/merged are not 9/1075/2");
    int m = 0;
    if (g.TakeHeld("k", &m)) Fail("a held message came out while its key's job was out");
    g.Finish("k");
    const int expect[] = { 2, 3, 4, 5, 6, 7, 8, 9, 11 };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i)
        if (!g.TakeHeld("k", &m) || m != expect[i]) Fail("the held messages did not come out in arrival order after the merges");
    if (g.HeldCount() != 0 || g.HeldBytes() != 0) Fail("the gate did not empty (count or bytes)");
    if (g.TakeHeldMax() != 9) Fail("heldMax did not keep the peak (9) until the report line");
    if (g.TakeHeldMax() != 0) Fail("heldMax did not restart from the current count after a report line");
    coopwq::KeyGate<int> h;   /* the two-argument Hold (no tag) never merges */
    h.Start("x"); h.Hold("x", 1); h.Hold("x", 2);
    if (h.HeldCount() != 2 || h.Merged() != 0) Fail("an untagged hold merged");
}
/* M14 fold 2 (re-check C): two ids that share one gate key ('Ab' / 'ab', one file on Windows) are two RECORDS - a
   held position for one never replaces the other's; the same record still merges. */
void t_m14_held_merge_per_record_id()
{
    const std::string k = coopwq::GateKey("Ab");
    if (k != coopwq::GateKey("ab")) Fail("'Ab' and 'ab' do not share a gate key (the case this test is about)");
    coopwq::KeyGate<int> g;
    if (!g.Start(k)) Fail("a free key could not start a job");
    int game = 0;
    coopwq::HeldTag a; a.kind = coopwq::kHeldPosition; a.sender = &game; a.connect = 3; a.bytes = 10; a.id = "Ab";
    a.stamp = 100; if (g.Hold(k, 1, a)) Fail("the first held position merged with nothing");
    coopwq::HeldTag b = a; b.id = "ab"; b.stamp = 200;
    if (g.Hold(k, 2, b)) Fail("a position for record 'ab' replaced one held for record 'Ab' (same gate key, another record)");
    b.stamp = 300; if (!g.Hold(k, 3, b)) Fail("a newer position for the same record did not replace its own older one");
    a.stamp = 400; if (g.Hold(k, 4, a)) Fail("a position for 'Ab' replaced the one held right before it for 'ab'");
    if (g.HeldCount() != 3 || g.HeldBytes() != 30 || g.Merged() != 1) Fail("held/heldBytes/merged are not 3/30/1");
    g.Finish(k);
    int m = 0;
    const int expect[] = { 1, 3, 4 };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i)
        if (!g.TakeHeld(k, &m) || m != expect[i]) Fail("the held messages did not come out in arrival order");
    if (g.TakeHeld(k, &m) || g.HeldCount() != 0) Fail("the gate did not empty");
}
/* M14 fold 2 (re-check A): the DEFERRED events - taken while backpressure is on - come out all of them, in
   arrival order (one taken while the first ones are handled goes BEHIND them); HasFor names a slot with events
   waiting; Discard removes only one slot's events and keeps the rest in order; count, bytes, total and the peak
   are kept. */
/* lev343-tests (owner 343 b): THE `buytest keeper` AIM - only counters the trade can take stock from, stocked-here first, then the
   lowest key; keepers by squad id then leader uid, never input order; a pin hits only a counter whose key contains it. */
static keeperaim::Keeper Lev343K(int eligible, const char* id, unsigned int uid, const keeperaim::Piece* p, int n)
{
    keeperaim::Keeper k;
    k.eligible = eligible; k.loaded = 1; k.worldId = id; k.leaderUid = uid; k.pieces = p; k.nPieces = n;
    return k;
}
void t_lev343_keeper_aim()
{
    /* T787's shape: an EMPTY piece with no grid whose key sorts first, beside the stocked counter */
    const keeperaim::Piece t787[] = { { 0, 0, "#2b0cb846@43,11@530400,-935000,9767" }, { 1, 2, "#607e1d18@43,11@530499,-935034,9767" } };
    keeperaim::Keeper one[1]; one[0] = Lev343K(1, "211-a", 211, t787, 2);
    int p = -1, why = -1;
    if (keeperaim::Choose(one, 1, 0, 0, &p, &why) != 0 || p != 1) Fail("the empty non-counter piece sorting first was aimed at (T787)");
    if (why != keeperaim::kWhyStockedHere) Fail("a stocked counter was not reported as stocked-here");
    /* a stocked counter beats an unstocked counter whose key sorts first */
    const keeperaim::Piece two[] = { { 1, 0, "#aaaa" }, { 1, 3, "#zzzz" }, { 1, 5, "#mmmm" } };
    one[0] = Lev343K(1, "k", 1, two, 3);
    if (keeperaim::Choose(one, 1, 0, 0, &p, &why) != 0 || p != 2) Fail("the stocked counter with the lowest key was not preferred over an unstocked one");
    /* nothing stocked: ghost still aims (lowest key, not-stocked-here); a row / own (requireStock) misses */
    const keeperaim::Piece bare[] = { { 1, 0, "#bbbb" }, { 1, 0, "#aaab" }, { 0, 4, "#0000" } };
    one[0] = Lev343K(1, "k", 1, bare, 3);
    if (keeperaim::Choose(one, 1, 0, 0, &p, &why) != 0 || p != 1 || why != keeperaim::kWhyNotStockedHere) Fail("ghost did not aim at the lowest-key unstocked counter");
    if (keeperaim::Choose(one, 1, 0, 1, &p, &why) != -1 || p != -1) Fail("requireStock aimed at an unstocked counter (or at a stocked non-counter)");
    /* deterministic across input order: smallest squad id, then leader uid; an ineligible keeper is never chosen */
    const keeperaim::Piece ok1[] = { { 1, 1, "#k1" } };
    keeperaim::Keeper ks[4];
    ks[0] = Lev343K(1, "300", 9, ok1, 1); ks[1] = Lev343K(1, "100", 7, ok1, 1); ks[2] = Lev343K(1, "100", 5, ok1, 1); ks[3] = Lev343K(0, "000", 1, ok1, 1);
    if (keeperaim::Choose(ks, 4, 0, 0, &p, &why) != 2) Fail("the keeper was not the smallest squad id then the smallest leader uid");
    keeperaim::Keeper rs[4]; rs[0] = ks[3]; rs[1] = ks[2]; rs[2] = ks[1]; rs[3] = ks[0];
    const int r = keeperaim::Choose(rs, 4, 0, 0, &p, &why);
    if (r < 0 || rs[r].leaderUid != 5) Fail("the choice changed when the keepers came in another order");
    /* a keeper with no valid counter is passed over for a later one that has one; an empty id sorts last */
    const keeperaim::Piece none[] = { { 0, 9, "#n" } };
    ks[0] = Lev343K(1, "001", 1, none, 1); ks[1] = Lev343K(1, "", 1, ok1, 1); ks[2] = Lev343K(1, "999", 50, ok1, 1);
    if (keeperaim::Choose(ks, 3, 0, 0, &p, &why) != 2) Fail("a keeper without a valid counter, or with an empty id, won");
    /* pinned: only a counter whose key contains the text, any stock; a non-counter piece matching the pin is not aimed at */
    const keeperaim::Piece pa[] = { { 1, 4, "#1111@1,1" }, { 0, 2, "#607e0000@2,2" } };
    const keeperaim::Piece pb[] = { { 1, 0, "#607e1d18@43,11@530499" } };
    ks[0] = Lev343K(1, "001", 1, pa, 2); ks[1] = Lev343K(1, "002", 2, pb, 1);
    if (keeperaim::Choose(ks, 2, "607e", 0, &p, &why) != 1 || p != 0 || why != keeperaim::kWhyPinned) Fail("the pin did not hit the one counter whose key contains it");
    if (keeperaim::Choose(ks, 2, "nowhere", 0, &p, &why) != -1 || why != keeperaim::kWhyNone) Fail("a pin no counter matches did not miss");
    /* no valid counter anywhere -> miss; keyless counters are not aimable */
    const keeperaim::Piece nokey[] = { { 1, 3, "" }, { 1, 3, 0 } };
    ks[0] = Lev343K(1, "001", 1, none, 1); ks[1] = Lev343K(1, "002", 2, nokey, 2);
    if (keeperaim::Choose(ks, 2, 0, 0, &p, &why) != -1 || p != -1) Fail("a keeper with no valid keyed counter was aimed at");
    /* the miss line's candidates: valid counters first (keeper order), then the rest, capped */
    ks[0] = Lev343K(0, "002", 2, t787, 2); ks[1] = Lev343K(0, "001", 1, none, 1);
    int kk[3], pp[3];
    const int c = keeperaim::Candidates(ks, 2, -1, kk, pp, 3);
    if (c != 3 || kk[0] != 0 || pp[0] != 1 || kk[1] != 1 || pp[1] != 0 || kk[2] != 0 || pp[2] != 0) Fail("the candidates were not valid-counters-first in keeper order");
    if (keeperaim::Candidates(ks, 2, 1, kk, pp, 3) != 1 || kk[0] != 1) Fail("a row miss listed another keeper's pieces");
}
/* T-608 T1389: `buytest keeper` WORD FORMS - every form before T1389 keeps its answer; `npc [stack]` is the NPC purchase path. */
void t_t1389_keeper_form()
{
    using keeperaim::KeeperForm;
    if (KeeperForm(2, 0, 0, 0, 0, 0) != keeperaim::kFormPlayer) Fail("t1389: a plain keeper buy is not the player's path");
    if (KeeperForm(2, 0, 0, 1, 0, 0) != keeperaim::kFormPlayer) Fail("t1389: a keeper sale is not the player's path");
    if (KeeperForm(2, 0, 0, 1, 1, 0) != keeperaim::kFormPlayer) Fail("t1389: sell pack is not the player's path");
    if (KeeperForm(2, 0, 0, 0, 1, 0) != keeperaim::kFormBad) Fail("t1389: pack without sell was accepted");
    if (KeeperForm(2, 0, 1, 0, 0, 0) != keeperaim::kFormBad) Fail("t1389: stack without npc was accepted (the old refusal)");
    if (KeeperForm(2, 0, 1, 1, 0, 0) != keeperaim::kFormBad) Fail("t1389: stack with sell was accepted");
    if (KeeperForm(2, 1, 0, 0, 0, 0) != keeperaim::kFormNpc) Fail("t1389: keeper npc is not the NPC path");
    if (KeeperForm(2, 1, 1, 0, 0, 0) != keeperaim::kFormNpc) Fail("t1389: keeper npc stack is not the NPC path");
    if (KeeperForm(2, 1, 0, 1, 0, 0) != keeperaim::kFormBad) Fail("t1389: npc with sell was accepted");
    if (KeeperForm(2, 1, 0, 1, 1, 0) != keeperaim::kFormBad) Fail("t1389: npc with sell pack was accepted");
    if (KeeperForm(2, 1, 0, 0, 1, 0) != keeperaim::kFormBad) Fail("t1389: npc with pack was accepted");
    if (KeeperForm(2, 1, 0, 0, 0, 1) != keeperaim::kFormBad) Fail("t1389: a bad cats= was accepted with npc");
    if (KeeperForm(2, 0, 0, 0, 0, 1) != keeperaim::kFormBad) Fail("t1389: a bad cats= was accepted");
    if (KeeperForm(1, 0, 0, 0, 0, 0) != keeperaim::kFormBad || KeeperForm(3, 1, 0, 0, 0, 0) != keeperaim::kFormBad)
        Fail("t1389: a missing or extra key token was accepted");
}
/* T-164 B4-4: A SHOP PIECE IS RECOGNISED FROM THE LIST ONLY WHILE ITS SQUAD ROW IS LIVE AND NAMES THE SAME HOME. */
void t_b44_piece_recognition()
{
    if (shopstock::PieceRecognised(1, 1, 1, 900, 5000, 1) != 1) Fail("a registered piece with a live squad row naming its home is not recognised");
    if (shopstock::PieceRecognised(0, 1, 1, 900, 5000, 1) != 0) Fail("an unregistered box is recognised as a shop piece");
    if (shopstock::PieceRecognised(1, 0, 1, 900, 5000, 1) != 0) Fail("a piece whose squad row is gone is recognised");
    if (shopstock::PieceRecognised(1, 1, 0, 900, 5000, 1) != 0) Fail("a piece whose squad has no home now is recognised");
    if (shopstock::PieceRecognised(1, 1, 1, 5000, 5000, 1) != 0) Fail("a stale squad row (age == bound) still recognises the piece");
    if (shopstock::PieceRecognised(1, 1, 1, 4999, 5000, 1) != 1) Fail("a row just inside the bound is refused");
    if (shopstock::PieceRecognised(1, 1, 1, 900, 5000, 0) != 0) Fail("a squad row naming ANOTHER home (a recycled squad pointer) is recognised");
}
/* T-164 B4-4 intercept: WHICH ROAD A TRADE CALL TAKES - a shop whose home this game holds runs the original; the other game's is
   intercepted; a shop whose holder is not known is refused here; not a shop, or a trader with no home and no packs behind its window,
   is the engine's own road. T-1 B5: a caravan (no home, pack-backed) takes the same road as a town shop, held by CaravanHolder's game. */
void t_b44i_gate()
{
    if (coopshop::Gate(0, 1, 1, 2) != coopshop::kGateOriginal) Fail("a non-shop window was intercepted");
    if (coopshop::Gate(1, 0, 0, 0, 0) != coopshop::kGateOriginal) Fail("a trader with no home and no packs (nothing to trade) left the engine's road");
    if (coopshop::Gate(1, 0, 0, 0) != coopshop::kGateOriginal) Fail("the gate's caravan input does not default to none");
    if (coopshop::Gate(0, 0, 0, 2, 1) != coopshop::kGateOriginal) Fail("a non-shop window with packs was intercepted");
    if (coopshop::Gate(1, 0, 0, 1, 1) != coopshop::kGateHeldHere) Fail("a caravan this game runs did not run the original");
    if (coopshop::Gate(1, 0, 0, 2, 1) != coopshop::kGateIntercept) Fail("a caravan the other game runs was not intercepted");
    if (coopshop::Gate(1, 0, 0, 0, 1) != coopshop::kGateRefuse) Fail("a caravan with no known holder ran something");
    if (coopshop::Gate(1, 0, 0, coopshop::kCaravanMixed, 1) != coopshop::kGateRefuse) Fail("a caravan with mixed pack wearers ran something");
    if (coopshop::Gate(1, 1, 1, 1) != coopshop::kGateHeldHere) Fail("a shop held here did not run the original");
    if (coopshop::Gate(1, 1, 1, 2) != coopshop::kGateIntercept) Fail("a shop the other game holds was not intercepted");
    if (coopshop::Gate(1, 1, 1, 0) != coopshop::kGateRefuse) Fail("a shop with no known holder ran something");
    if (coopshop::Gate(1, 1, 1, 4) != coopshop::kGateRefuse) Fail("an unanswered area verdict ran something");
    if (coopshop::Gate(1, 1, 0, 1) != coopshop::kGateRefuse) Fail("a home with no key was not refused");
    if (coopshop::Gate(1, 1, 1, 1, 1) != coopshop::kGateHeldHere) Fail("a trader with a home stopped being keyed on it");
}
/* T-1 B5 (manager decision 2): A CARAVAN IS HELD BY THE GAME THAT RUNS ITS SQUAD - the keeper's uid that game's AND every pack worn by a
   member it runs; a mixed squad (a hand-over in flight) or an unknown wearer is no holder. */
/* CaravanHolder(multiplayer, keeperUid, keeperMine, leader, packs, wMine, wOther, wUnknown, wearerUid) */
/* T-1007 (J): CaravanHolderArea(multiplayer, keeperUid, keeperMine, leader, leaderUnnum, packs, wMine, wOther, wUnnum, wUnknown,
   wearerUid, area) - each unnumbered keeper / leader / wearer takes the area's one answer */
/* T-1007 (J3): the holder's records keyed by the asker's slot; the acked list before anything is applied */
void t_t1007j3_records()
{
    using namespace coopshop;
    const unsigned int sessionRaw = 0u, relay = 0x40000003u;
    if (AskerKey(3, sessionRaw) != AskerKey(3, relay)) Fail("t1007j3: one asker on two roads is two askers");
    if (SameRecord(AskerKey(3, sessionRaw), 77u, 9u, AskerKey(3, relay), 77u, 9u) != 1) Fail("t1007j3: an ack on the other road missed its record");
    if (SameRecord(AskerKey(3, sessionRaw), 77u, 9u, AskerKey(4, relay), 77u, 9u) != 0) Fail("t1007j3: another player's ack matched the record");
    if (SameRecord(AskerKey(3, sessionRaw), 77u, 9u, AskerKey(3, relay), 78u, 9u) != 0) Fail("t1007j3: another epoch matched the record");
    for (unsigned int s = 0; s < 256; ++s) if (AskerKey(-1, s) == AskerKey((int)s, 0u)) Fail("t1007j3: an unknown asker's key equals a slot");
    if (RepeatRoad(1, 1, 1) != kRepeatFromRecord) Fail("t1007j3: an unacked record lost to the acked list");
    if (RepeatRoad(0, 1, 1) != kRepeatAcked || RepeatRoad(0, 0, 1) != kRepeatAcked) Fail("t1007j3: an acked request not left alone");
    if (RepeatRoad(0, 1, 0) != kRepeatFromMemory) Fail("t1007j3: an answered request not answered from the memory");
    if (RepeatRoad(0, 0, 0) != kRepeatFresh) Fail("t1007j3: a new request not judged");
    /* T-1007 (J4): a record filed before the session peer's slot was known is re-filed under it, so its later ack matches */
    const unsigned int early = AskerKey(-1, sessionRaw);
    if (AskerIsSlot(early) != 0 || AskerIsSlot(AskerKey(5, relay)) != 1) Fail("t1007j4: a raw key read as a slot");
    if (SameRecord(AskerRefile(early, 5), 77u, 9u, AskerKey(5, relay), 77u, 9u) != 1) Fail("t1007j4: a re-filed record missed its ack");
    if (AskerRefile(AskerKey(4, relay), 5) != 4u) Fail("t1007j4: a slot key was re-filed");
    if (AskerRefile(early, -1) != early) Fail("t1007j4: re-filed with no slot known");
}
/* T-1007 (J2): the answer wait's pure rules */
void t_t1007j2_wait()
{
    using namespace coopshop;
    if (PendOnGone(3, 0, 1, 3, 0) != kPendAskedGone) Fail("t1007j2: a request asked of the player who left stays");
    if (PendOnGone(4, 1, 1, 3, 0) != kPendKeep) Fail("t1007j2: a request asked of another player settled at a leave");
    if (PendOnGone(3, 0, 1, -1, 0) != kPendKeep) Fail("t1007j2: an unknown leaver settled a request");
    if (PendOnGone(3, 1, 1, 3, 1) != kPendLinkLost) Fail("t1007j2: a session-link request sent before the edge stays at link loss");
    if (PendOnGone(3, 1, 0, 3, 1) != kPendKeep) Fail("t1007j2: a request sent after the edge settled at link loss");
    if (PendOnGone(5, 0, 1, 3, 1) != kPendKeep) Fail("t1007j2: a request asked over the world server settled at a session link loss");
    if (PendOnGone(3, 0, 1, 3, 1) != kPendKeep) Fail("t1007j2: the session peer asked over the world server settled at a session link loss");
    if (RepeatRoad(1, 1) != kRepeatFromRecord || RepeatRoad(1, 0) != kRepeatFromRecord) Fail("t1007j2: a repeat of an applied trade not answered from its record");
    if (RepeatRoad(0, 1) != kRepeatFromMemory) Fail("t1007j2: a repeat of an answered request not answered from the memory");
    if (RepeatRoad(0, 0) != kRepeatFresh) Fail("t1007j2: a new request not judged");
    if (DupAnswer(1, 1) != kDupAck) Fail("t1007j2: a repeat ok to a settled trade not acked");
    if (DupAnswer(0, 1) != kDupRevoke) Fail("t1007j2: a repeat ok to a settled refusal not revoked");
    if (DupAnswer(1, 0) != kDupNothing || DupAnswer(0, 0) != kDupNothing) Fail("t1007j2: a repeat refusal did something");
    if (RefusalShows(1, 0, 0, 0) != 1 || RefusalShows(1, 1, 0, 0) != 0) Fail("t1007j2: a click's refusal not shown once per frame");
    if (RefusalShows(0, 0, 7, 7) != 1) Fail("t1007j2: the awaited request's refusal not shown");
    if (RefusalShows(0, 0, 7, 8) != 0 || RefusalShows(0, 0, 7, 0) != 0 || RefusalShows(0, 0, 0, 0) != 0) Fail("t1007j2: an older request's refusal shown");
}
void t_t1007j_caravan_area()
{
    using namespace coopshop;
    if (CaravanHolderArea(0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, kCarAreaOther) != 1) Fail("t1007j: a single-player game is not held here");
    if (CaravanHolderArea(1, 0, 0, 0, 1, 2, 0, 0, 2, 0, 0, kCarAreaOther) != 1) Fail("t1007j: nothing adopted is not the engine's own trade");
    if (CaravanHolderArea(1, 1, 1, 1, 0, 2, 1, 0, 1, 0, 1, kCarAreaMine) != 1) Fail("t1007j: an unnumbered wearer in this game's area is not this game's");
    if (CaravanHolderArea(1, 1, 1, 1, 0, 2, 1, 0, 1, 0, 1, kCarAreaOther) != kCaravanMixed) Fail("t1007j: an unnumbered wearer in another game's area beside this game's keeper is not mixed");
    if (CaravanHolderArea(1, 1, 1, 1, 0, 2, 1, 0, 1, 0, 1, kCarAreaUnknown) != 0) Fail("t1007j: an unnumbered wearer in an unknown area is not unknown");
    if (CaravanHolderArea(1, 0, 0, 2, 0, 2, 0, 2, 0, 0, 1, kCarAreaOther) != 2) Fail("t1007j: an unnumbered keeper in another game's area is not that game's");
    if (CaravanHolderArea(1, 0, 0, 2, 0, 2, 0, 2, 0, 0, 1, kCarAreaMine) != kCaravanMixed) Fail("t1007j: an unnumbered keeper here beside the other game's squad is not mixed");
    if (CaravanHolderArea(1, 0, 0, 2, 0, 2, 0, 2, 0, 0, 1, kCarAreaUnknown) != 0) Fail("t1007j: an unnumbered keeper in an unknown area is not unknown");
    if (CaravanHolderArea(1, 1, 0, 0, 1, 2, 0, 1, 1, 0, 1, kCarAreaOther) != 2) Fail("t1007j: an unnumbered leader and wearer in the other game's area are not that game's");
    if (CaravanHolderArea(1, 1, 0, 0, 0, 2, 0, 2, 0, 0, 1, kCarAreaOther) != 0) Fail("t1007j: a leader that did not read took the area's answer");
    if (CaravanHolderArea(1, 1, 1, 1, 0, 2, 1, 0, 0, 1, 1, kCarAreaMine) != 0) Fail("t1007j: a pack with no wearer found took the area's answer");
    if (CaravanHolderArea(1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, kCarAreaMine) != 0) Fail("t1007j: no packs is held");
    for (int a = 0; a < 3; ++a)   /* a fully numbered caravan: the area changes nothing */
    {
        if (CaravanHolderArea(1, 1, 1, 1, 0, 2, 2, 0, 0, 0, 1, a) != CaravanHolder(1, 1, 1, 1, 2, 2, 0, 0, 1)) Fail("t1007j: the area changed a numbered caravan (mine)");
        if (CaravanHolderArea(1, 1, 0, 2, 0, 2, 0, 2, 0, 0, 1, a) != CaravanHolder(1, 1, 0, 2, 2, 0, 2, 0, 1)) Fail("t1007j: the area changed a numbered caravan (other)");
        if (CaravanHolderArea(1, 1, 1, 2, 0, 2, 1, 1, 0, 0, 1, a) != CaravanHolder(1, 1, 1, 2, 2, 1, 1, 0, 1)) Fail("t1007j: the area changed a numbered caravan (mixed)");
    }
}
void t_b5_caravan_holder()
{
    t_t1007j_caravan_area();   /* T-1007 (J) */
    t_t1007j2_wait();   /* T-1007 (J2) */
    t_t1007j3_records();   /* T-1007 (J3) */
    if (coopshop::CaravanHolder(1, 1, 1, 1, 2, 2, 0, 0, 1) != 1) Fail("a caravan this game runs whole is not held here");
    if (coopshop::CaravanHolder(1, 1, 0, 2, 2, 0, 2, 0, 1) != 2) Fail("a caravan the other game runs whole is not the other game's");
    if (coopshop::CaravanHolder(1, 0, 0, 2, 2, 0, 2, 0, 1) != 0) Fail("a keeper with no uid named a holder");
    if (coopshop::CaravanHolder(1, 1, 1, 1, 2, 1, 1, 0, 1) != coopshop::kCaravanMixed) Fail("our keeper with a pack the other game runs is not mixed");
    if (coopshop::CaravanHolder(1, 1, 0, 2, 2, 1, 1, 0, 1) != coopshop::kCaravanMixed) Fail("the other game's keeper with a pack we run is not mixed");
    if (coopshop::CaravanHolder(1, 1, 1, 1, 2, 1, 0, 1, 1) != 0) Fail("a pack with an unknown wearer named a holder");
    if (coopshop::CaravanHolder(1, 1, 1, 1, 0, 0, 0, 0, 1) != 0) Fail("a caravan with no packs named a holder");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 1, 1, 2, 1, 1, 0, 1), 1) != coopshop::kGateRefuse) Fail("a mixed caravan was not refused at the gate");
}
/* A SINGLE-PLAYER GAME, or NOTHING ADOPTED (keeper, leader and every wearer without a uid), is held here - the engine's own
   trade, as before B5; a mix of uid'd and uid-less keeps the old rule (no holder: refused). */
void t_b5f1_caravan_unadopted()
{
    if (coopshop::CaravanHolder(0, 0, 0, 0, 2, 0, 0, 2, 0) != 1) Fail("in a single-player game an unadopted caravan was not held here");
    if (coopshop::CaravanHolder(0, 1, 0, 2, 2, 0, 2, 0, 1) != 1) Fail("in a single-player game a caravan was not held here (the engine's own trade)");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(0, 0, 0, 0, 2, 0, 0, 2, 0), 1) != coopshop::kGateHeldHere) Fail("in a single-player game a caravan's trade did not run the original");
    if (coopshop::CaravanHolder(1, 0, 0, 0, 2, 0, 0, 2, 0) != 1) Fail("in a multiplayer game a caravan with nothing adopted was not held here");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 0, 0, 0, 2, 0, 0, 2, 0), 1) != coopshop::kGateHeldHere) Fail("an unadopted caravan's trade did not run the original");
    if (coopshop::CaravanHolder(1, 0, 0, 0, 2, 0, 0, 2, 1) != 0) Fail("a uid-less keeper whose squad has a wearer with a uid named a holder");
    if (coopshop::CaravanHolder(1, 1, 1, 0, 2, 0, 0, 2, 0) != 0) Fail("our keeper with uid-less wearers and leader named a holder");
    if (coopshop::CaravanHolder(1, 0, 0, 1, 2, 0, 0, 2, 0) != 0) Fail("a uid'd leader with a uid-less keeper and wearers named a holder");
    if (coopshop::CaravanHolder(1, 0, 0, 0, 2, 0, 2, 0, 0) != 0) Fail("packs worn by the other game's members counted as nothing adopted");
    if (coopshop::CaravanHolder(1, 0, 0, 0, 0, 0, 0, 0, 0) != 0) Fail("in a multiplayer game a caravan with no packs named a holder");
}
/* T-1 B5 fold 1 F2: THE FORMAL LEADER COUNTS - the squad pot is the leader's and only its game announces it; a split squad (keeper and
   wearers on one game, the leader on the other) and a leader with no uid are no holder. */
void t_b5f2_caravan_leader()
{
    if (coopshop::CaravanHolder(1, 1, 1, 2, 2, 2, 0, 0, 1) != coopshop::kCaravanMixed) Fail("a split squad (leader on the other game) was held here");
    if (coopshop::CaravanHolder(1, 1, 0, 1, 2, 0, 2, 0, 1) != coopshop::kCaravanMixed) Fail("a split squad (leader here) was the other game's");
    if (coopshop::CaravanHolder(1, 1, 1, 0, 2, 2, 0, 0, 1) != 0) Fail("a leader with no uid named this game the holder");
    if (coopshop::CaravanHolder(1, 1, 0, 0, 2, 0, 2, 0, 1) != 0) Fail("a leader with no uid named the other game the holder");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 1, 2, 2, 2, 0, 0, 1), 1) != coopshop::kGateRefuse) Fail("a split squad's caravan was not refused at the gate");
}
/* A CARAVAN'S HOLDER IN A MULTIPLAYER WORLD IS DECIDED BY CHARACTER NUMBERS ALONE - the function has no player-count input, so a game
   alone in the world answers as one with company: its own numbered caravan is held here, another game's is that game's (the request
   road), a partly numbered one is refused; only a single-player game takes the engine's own trade for any caravan. */
void t1007_caravan_one_road()
{
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 1, 1, 2, 2, 0, 0, 1), 1) != coopshop::kGateHeldHere) Fail("a caravan numbered by this game was not held here");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 0, 2, 2, 0, 2, 0, 1), 1) != coopshop::kGateIntercept) Fail("a multiplayer game took the engine's own trade for another game's caravan");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 1, 1, 2, 1, 0, 1, 1), 1) != coopshop::kGateRefuse) Fail("a multiplayer game ran a caravan with an unnumbered wearer");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(1, 1, 1, 0, 2, 2, 0, 0, 1), 1) != coopshop::kGateRefuse) Fail("a multiplayer game ran a caravan whose leader has no number");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(0, 1, 0, 2, 2, 0, 2, 0, 1), 1) != coopshop::kGateHeldHere) Fail("a single-player game did not take the engine's own trade");
    if (coopshop::Gate(1, 0, 0, coopshop::CaravanHolder(0, 1, 1, 1, 2, 1, 0, 1, 1), 1) != coopshop::kGateHeldHere) Fail("a single-player game refused a caravan");
}
/* the original's cheap pre-checks, answered with its own TradeResult; money and an unread count never ride this road */
void t_b44i_precheck()
{
    if (coopshop::PreCheck(0, 1, 0, 1) != 0) Fail("a clean trade did not pass");
    if (coopshop::PreCheck(1, 1, 0, 1) != 7) Fail("a locked item is not result 7");
    if (coopshop::PreCheck(0, 0, 0, 1) != 1) Fail("out of range is not result 1");
    if (coopshop::PreCheck(1, 0, 0, 1) != 7) Fail("locked must be answered before range (the original's order)");
    if (coopshop::PreCheck(0, 1, 1, 1) != 11) Fail("a money item was carried");
    if (coopshop::PreCheck(0, 1, 0, 0) != 11) Fail("an unread stack count was carried");
}
/* T-164 part 2: the drag's pick-up guard, drop route and unit count */
void t_t164p2_drag()
{
    using namespace coopshop;
    if (PickupGuarded(kGateIntercept) != 1 || PickupGuarded(kGateRefuse) != 1) Fail("a pick-up from a shop not held here was not guarded");
    if (PickupGuarded(kGateHeldHere) != 0 || PickupGuarded(kGateOriginal) != 0) Fail("a pick-up from a shop held here / no shop was guarded");
    /* a shop copy */
    if (DropRoute(1, 1, kGateIntercept, kGateIntercept, 0, 0, 1) != kDropGuardedOriginal) Fail("a copy back on its own shop did not run guarded");
    if (DropRoute(1, 0, kGateIntercept, kGateOriginal, 1, 0, 1) != kDropBuy) Fail("a copy on our window was not a buy");
    if (DropRoute(1, 0, kGateIntercept, kGateOriginal, 1, 0, 0) != kDropReturnGuarded) Fail("a copy with no money trade was bought");
    if (DropRoute(1, 0, kGateIntercept, kGateOriginal, 0, 0, 1) != kDropReturnGuarded) Fail("a copy on a window not ours was bought");
    if (DropRoute(1, 0, kGateRefuse, kGateOriginal, 1, 0, 1) != kDropReturnGuarded) Fail("a copy of a shop with no known holder was bought");
    if (DropRoute(1, 0, kGateHeldHere, kGateOriginal, 1, 0, 1) != kDropReturnGuarded) Fail("a copy whose shop became held here ran the original");
    if (DropRoute(1, 0, kGateIntercept, kGateIntercept, 1, 0, 1) != kDropReturnGuarded) Fail("a copy on another shop was bought");
    /* anything else */
    if (DropRoute(0, 0, kGateOriginal, kGateOriginal, 1, 1, 0) != kDropOriginal) Fail("an ordinary drop left the original");
    if (DropRoute(0, 0, kGateOriginal, kGateHeldHere, 0, 1, 1) != kDropOriginal) Fail("a sale to a shop held here left the original");
    if (DropRoute(0, 0, kGateOriginal, kGateIntercept, 0, 1, 1) != kDropSell) Fail("our item on a shop the other game holds was not a sale");
    if (DropRoute(0, 0, kGateOriginal, kGateIntercept, 0, 0, 1) != kDropReturnPlain) Fail("an item not ours was sold to a shop not held here");
    if (DropRoute(0, 0, kGateOriginal, kGateIntercept, 0, 1, 0) != kDropReturnPlain) Fail("a free transfer into a shop not held here ran");
    if (DropRoute(0, 0, kGateOriginal, kGateRefuse, 0, 1, 1) != kDropReturnPlain) Fail("a sale to a shop with no known holder ran");
    /* units */
    if (DragUnits(5, 10, 100) != 5) Fail("a whole cursor stack was clamped for nothing");
    if (DragUnits(5, 3, 100) != 3) Fail("the stack limit did not clamp");
    if (DragUnits(5, 10, 2) != 2) Fail("the affordable count did not clamp");
    if (DragUnits(5, 10, 0) != 0) Fail("an unaffordable drop moved units");
    if (DragUnits(5, -1, -1) != 5 || DragUnits(5, 0, -1) != 5) Fail("an unread limit clamped");
    if (DragUnits(0, 10, 10) != 0 || DragUnits(kMaxUnits + 1, -1, -1) != 0) Fail("an empty / absurd cursor stack moved units");
    /* fold H1: the cursor after a guarded drop - a shop-sourced item is adopted, never forgotten */
    if (CursorAfter(1, 1, 0) != kCurKeep || CursorAfter(1, 1, 1) != kCurKeep) Fail("the tracked copy still on the cursor was not kept");
    if (CursorAfter(1, 0, 1) != kCurAdopt) Fail("a swapped-out shop item was not adopted");
    if (CursorAfter(1, 0, 0) != kCurForget) Fail("an item not from the copy's shop was adopted");
    if (CursorAfter(0, 0, 0) != kCurForget || CursorAfter(0, 0, 1) != kCurForget) Fail("an empty cursor kept a copy");
    /* fold H2: the put-back 0x70CC90 */
    if (PutBackGuard(0, 1, 1, kGateOriginal) != kPbGuard || PutBackGuard(0, 1, 1, kGateHeldHere) != kPbGuard) Fail("the tracked copy went back unguarded");
    if (PutBackGuard(0, 0, 1, kGateIntercept) != kPbGuard || PutBackGuard(0, 0, 1, kGateRefuse) != kPbGuard) Fail("a put-back into a shop not held here was not guarded");
    if (PutBackGuard(0, 0, 1, kGateHeldHere) != kPbAsIs || PutBackGuard(0, 0, 1, kGateOriginal) != kPbAsIs) Fail("a put-back into a shop held here / a wandering trader was guarded");
    if (PutBackGuard(0, 0, 0, kGateOriginal) != kPbAsIs) Fail("a put-back into a window of ours was guarded");
    if (PutBackGuard(1, 1, 1, kGateIntercept) != kPbAsIs) Fail("a put-back already inside the guard entered it again");
    /* fold M2: the short answer by the source's side */
    if (DragShortResult(0) != kTrCantAfford || DragShortResult(1) != kTrKeeperBroke) Fail("the can't-afford answer took the wrong side");
    /* fold 2 (LOW): the tracked copy is guarded only into a stand-in */
    if (PutBackGuard(0, 1, 0, kGateOriginal) != kPbAsIs || PutBackGuard(0, 1, 0, kGateIntercept) != kPbAsIs) Fail("the tracked copy into a window with no shop was guarded");
    /* fold 2 (HIGH): a release over a character or container, 0x7118C0 */
    if (GiveRefused(1, 1, kGateOriginal) != kGiveRefuse || GiveRefused(1, 0, kGateOriginal) != kGiveRefuse) Fail("the tracked shop copy was given to a world object");
    if (GiveRefused(0, 1, kGateIntercept) != kGiveRefuse || GiveRefused(0, 1, kGateRefuse) != kGiveRefuse) Fail("an item out of a shop not held here was given to a world object");
    if (GiveRefused(0, 1, kGateHeldHere) != kGiveAsIs || GiveRefused(0, 1, kGateOriginal) != kGiveAsIs) Fail("an item out of a shop held here / a wandering trader was refused");
    if (GiveRefused(0, 0, kGateOriginal) != kGiveAsIs) Fail("an item out of a window of ours was refused");
}
/* one unit, a stack of one whole, shift = the whole stack as ONE request (owner 186); price x units with an overflow refusal */
void t_b44i_amount_price()
{
    if (coopshop::Units(5, 0) != 1) Fail("a stack of 5 without shift is not one unit");
    if (coopshop::Units(5, 1) != 5) Fail("shift did not take the whole stack");
    if (coopshop::Units(1, 0) != 1) Fail("a stack of one is not whole");
    if (coopshop::Units(0, 1) != 0 || coopshop::Units(-3, 0) != 0) Fail("an empty stack gave units");
    if (coopshop::Units(coopshop::kMaxUnits + 1, 1) != 0) Fail("an absurd stack was carried");
    if (coopshop::TotalPrice(120, 5) != 600) Fail("5 units at 120 is not 600");
    if (coopshop::TotalPrice(0, 3) != 0) Fail("a free item is not 0");
    if (coopshop::TotalPrice(-1, 1) != -1 || coopshop::TotalPrice(10, 0) != -1) Fail("a bad price or no units was priced");
    if (coopshop::TotalPrice(0x40000000, 4) != -1) Fail("an overflowing price was carried");
}
void t_b44i_dedupe()
{
    coopshop::ClickKey a = { 0x1000u, coopshop::SecHash("main"), 2, 3, 77u };
    coopshop::ClickKey b = a;
    if (coopshop::SameClick(a, b) != 1) Fail("the same click in the same frame is not a duplicate");
    b.frame = 78u; if (coopshop::SameClick(a, b) != 0) Fail("a click in the next frame was dropped");
    b = a; b.x = 4; if (coopshop::SameClick(a, b) != 0) Fail("another cell was dropped");
    b = a; b.secHash = coopshop::SecHash("backpack"); if (coopshop::SameClick(a, b) != 0) Fail("another section was dropped");
    /* B4-4 part 1 fold (review MED 8): 0x713E50's second-window retry is the same click in the same frame. A first window refused
       here (nothing sent) must not mark it; once a request was sent the retry is the duplicate, whatever window it goes to. */
    coopshop::ClickKey none = { 0, 0, 0, 0, 0xFFFFFFFFu };
    coopshop::ClickKey last = coopshop::KeyAfter(none, a, 0);
    if (coopshop::SameClick(a, last) != 0) Fail("a click refused before any send swallowed the engine's second-window retry");
    last = coopshop::KeyAfter(last, a, 1);
    if (coopshop::SameClick(a, last) != 1) Fail("the second-window retry of a sent click was not a duplicate (a double trade)");
    last = coopshop::KeyAfter(last, b, 0);
    if (coopshop::SameClick(a, last) != 1) Fail("a later refusal un-marked a sent click");
    coopshop::ClickKey nf = a; nf.frame = 78u;
    if (coopshop::SameClick(nf, last) != 0) Fail("the next frame's click was dropped after a send");
}
/* B4-4 part 1 fold (review HIGH 1) / T-450: the buyer's room for ALL n units is asked before anything moves - "No room" (result 2)
   unless the free placements x the stack limit plus every matching stack's room take n; an unreadable inventory, section or limit
   is no room (never money taken for what may not fit); a section's placements never overlap */
void t_b44i_fit()
{
    if (coopshop::BuyFit(1, 0, 0, 1) != coopshop::kFitNoRoom) Fail("a full pack with no matching stack was sent (the item would be lost)");
    if (coopshop::BuyFit(1, coopshop::CellRoom(1, 1), 0, 1) != coopshop::kFitSend) Fail("a pack with a free cell was refused");
    if (coopshop::BuyFit(1, 0, 1, 1) != coopshop::kFitSend) Fail("a full pack with a stack of the same item that takes it was refused (the engine merges)");
    if (coopshop::BuyFit(0, coopshop::CellRoom(5, 10), 0, 1) != coopshop::kFitNoRoom) Fail("an unreadable inventory was sent (T-450: money taken for what may not fit)");
    /* T-450 (run T812: 27 units bought on one free cell, 8 placed, 19 kept) */
    if (coopshop::BuyFit(1, coopshop::CellRoom(1, 8), 0, 27) != coopshop::kFitNoRoom) Fail("27 units on one free cell of limit 8 were sent (T812)");
    if (coopshop::BuyFit(1, coopshop::CellRoom(3, 8), 0, 27) != coopshop::kFitNoRoom) Fail("27 units on three cells of limit 8 (24) were sent");
    if (coopshop::BuyFit(1, coopshop::CellRoom(4, 8), 0, 27) != coopshop::kFitSend) Fail("27 units with four free cells of limit 8 were refused");
    if (coopshop::BuyFit(1, coopshop::CellRoom(3, 8), 3, 27) != coopshop::kFitSend) Fail("the cells' room and a stack's room were not summed");
    const int twoStacks = coopshop::RoomAdd(coopshop::StackRoom(10, 4), coopshop::StackRoom(10, 7));   /* 6 + 3 */
    if (twoStacks != 9) Fail("the matching stacks' room was not summed");
    if (coopshop::BuyFit(1, coopshop::CellRoom(1, 8), twoStacks, 17) != coopshop::kFitSend) Fail("one cell + two stacks (17) refused 17");
    if (coopshop::BuyFit(1, coopshop::CellRoom(1, 8), twoStacks, 18) != coopshop::kFitNoRoom) Fail("one cell + two stacks (17) sent 18");
    if (coopshop::CellRoom(3, -1) != 0 || coopshop::CellRoom(-1, 8) != 0 || coopshop::CellRoom(3, 0) != 0) Fail("an unread section or limit counted room");
    if (coopshop::BuyFit(1, coopshop::CellRoom(2, -1), coopshop::RoomAdd(-1, 0), 1) != coopshop::kFitNoRoom) Fail("an unread limit was counted as room");
    if (coopshop::CellRoom(100000, 100000) <= 0 || coopshop::RoomAdd(0x7FFFFFFF, 0x7FFFFFFF) <= 0) Fail("a large room overflowed");
    /* the placements a section gives (coopshop::CountPlacements): the engine's order, never overlapping, stopped at want */
    Copy3Grid g;
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacements(4, 2, 1, 1, &Copy3Taken, &g, 100) != 8) Fail("an empty 4x2 section did not give eight 1x1 placements");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacements(4, 2, 1, 1, &Copy3Taken, &g, 3) != 3) Fail("the count did not stop at want");
    Copy3Init(&g, 1, 1); g.cell[0][0] = 1; g.cell[1][2] = 1;
    if (coopshop::CountPlacements(4, 2, 1, 1, &Copy3Taken, &g, 100) != 6) Fail("occupied cells were counted free");
    Copy3Init(&g, 2, 2);
    if (coopshop::CountPlacements(4, 2, 2, 2, &Copy3Taken, &g, 100) != 2) Fail("a 2x2 item in a 4x2 section was counted overlapping (3 positions, 2 placements)");
    Copy3Init(&g, 2, 2);
    if (coopshop::CountPlacements(3, 3, 2, 2, &Copy3Taken, &g, 100) != 1) Fail("a 2x2 item in an empty 3x3 was counted overlapping (4 positions, 1 placement)");
    Copy3Init(&g, 2, 1); g.cell[0][1] = 1;
    if (coopshop::CountPlacements(4, 1, 2, 1, &Copy3Taken, &g, 100) != 1) Fail("a 2x1 item next to an occupied cell was counted wrong");
    Copy3Init(&g, 5, 1);
    if (coopshop::CountPlacements(4, 8, 5, 1, &Copy3Taken, &g, 100) != 0 || g.asked != 0) Fail("too wide -> no placement, nothing asked");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacements(0, 8, 1, 1, &Copy3Taken, &g, 100) != -1 || coopshop::CountPlacements(4, 8, 1, 1, 0, &g, 100) != -1
        || coopshop::CountPlacements(100, 100, 1, 1, &Copy3Taken, &g, 100) != -1 || coopshop::CountPlacements(4, 8, 0, 1, &Copy3Taken, &g, 100) != -1)
        Fail("bad sizes were not answered unreadable");
    /* T-450 fold 1 (review 1): a stack limit that READ as 0 or 1 is one unit per cell (decomp_74bd70:129-145) - never no room */
    if (coopshop::CellUnits(0) != 1 || coopshop::CellUnits(1) != 1 || coopshop::CellUnits(-3) != 1 || coopshop::CellUnits(8) != 8)
        Fail("a limit read as 0 or 1 was not one unit per cell");
    Copy3Init(&g, 1, 1);
    {
        const int per = coopshop::CellUnits(0);   /* one free cell, the limit read fine as 0 */
        const int cells = coopshop::CountPlacementsCapped(1, 1, 1, 1, &Copy3Taken, &g, (1 + per - 1) / per, 0, 0);
        if (cells != 1 || coopshop::BuyFit(1, coopshop::CellRoom(cells, per), 0, 1) != coopshop::kFitSend)
            Fail("one free cell with a limit read as 0 refused one unit (the engine places it: No room on a buy the game allows)");
        if (coopshop::BuyFit(1, coopshop::CellRoom(cells, per), 0, 2) != coopshop::kFitNoRoom) Fail("one free cell with a limit read as 0 took two units");
    }
    /* T-450 fold 1 (review 2): the section's item-count cap (+0xA8, 0x719B80) bounds the placements - cap less items, never below 0 */
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, 5, 2) != 3) Fail("a section capped at 5 items holding 2 gave other than 3 placements");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, 5, 5) != 0 || g.asked != 0) Fail("a full capped section gave a placement");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, 5, 9) != 0) Fail("an over-full capped section gave a placement");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, 0, 7) != 8) Fail("an uncapped section (cap 0) was capped");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, -1, 7) != 8) Fail("a negative cap (no cap to the engine) was capped");
    Copy3Init(&g, 1, 1);
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 2, 5, 0) != 2) Fail("the capped count did not stop at want");
    Copy3Init(&g, 1, 1); g.cell[0][0] = 1;
    if (coopshop::CountPlacementsCapped(4, 2, 1, 1, &Copy3Taken, &g, 100, 4, 1) != 3) Fail("the cap and an occupied cell were not both applied");
    if (coopshop::CountPlacementsCapped(0, 8, 1, 1, &Copy3Taken, &g, 100, 5, 5) != -1) Fail("bad sizes under a full cap were not answered unreadable");
    if (coopshop::PlaceCapLeft(0, 3) != coopshop::kPlaceGridCap || coopshop::PlaceCapLeft(4, -2) != 4 || coopshop::PlaceCapLeft(4, 4) != 0)
        Fail("the cap's placements left were wrong");
    coopshop::Desc x; x.baseSid = "sword"; x.companySid = "smith"; x.materialSid = "steel"; x.quality = 0.5f; x.level = 2;
    coopshop::Desc y = x;
    if (coopshop::SameDesc(x, y) != 1) Fail("the same variant did not match");
    y.companySid = "other"; if (coopshop::SameDesc(x, y) != 0) Fail("another maker matched (MED 4: the price was the clicked variant's)");
    y = x; y.quality = 0.75f; if (coopshop::SameDesc(x, y) != 0) Fail("another grade matched");
    y = x; y.materialSid = "iron"; if (coopshop::SameDesc(x, y) != 0) Fail("another material matched");
    y = x; y.level = 3; if (coopshop::SameDesc(x, y) != 0) Fail("another level matched");
}
/* B4-4 part 1 fold (review HIGH 2): an ok after the lapse is answered with a revoke (the holder undoes its halves); a late refusal,
   a malformed answer or an id that is not a lapsed shop row sends nothing; the undo is counted per half */
void t_b44i_late()
{
    if (coopshop::LateAnswer(1, 1, 1, coopshop::kVOk) != coopshop::kLateRevoke) Fail("a late ok did not revoke (conservation broken)");
    if (coopshop::LateAnswer(1, 0, 1, coopshop::kVSoldOut) != coopshop::kLateNothing) Fail("a late refusal was revoked");
    if (coopshop::LateAnswer(1, 1, 0, coopshop::kVOk) != coopshop::kLateNothing) Fail("a late ok with no shop block was revoked");
    if (coopshop::LateAnswer(1, 1, 1, coopshop::kVFault) != coopshop::kLateNothing) Fail("a late ok naming a fault verdict was revoked");
    if (coopshop::LateAnswer(0, 1, 1, coopshop::kVOk) != coopshop::kLateNothing) Fail("an ok for an id that is not a lapsed shop row was revoked");
    if (coopshop::UndoOutcome(1, 1) != 2 || coopshop::UndoOutcome(1, 0) != 1 || coopshop::UndoOutcome(0, 1) != 1 || coopshop::UndoOutcome(0, 0) != 0)
        Fail("the undo is not counted per half");
}
/* B4-4 fold 2 (MED 1 / MED 2 / LOW 11 / item 12): the engine's stack limit and the room check against a FULL matching stack; the
   holder's record kept until the ack (no expiry, never evicted, a full table refuses); the shop tail; a restart-safe record key */
void t_b44i_record()
{
    if (coopshop::StackMax(2, 50, 0, 0, 1.0f) != 1 || coopshop::StackMax(0x6F, 50, 0, 0, 1.0f) != 1) Fail("a never-stacking record type stacks");
    if (coopshop::StackMax(0, 10, 0, 0, 1.0f) != 10) Fail("the record's stackable is not the limit");
    if (coopshop::StackMax(0, 10, 1, 0, 1.0f) != 1) Fail("a no-stack section stacks");
    if (coopshop::StackMax(0, 1, 0, 5, 1.0f) != 5) Fail("the section's minimum does not raise the limit");
    if (coopshop::StackMax(0, 10, 0, 0, 2.5f) != 25) Fail("the section's multiplier is not applied");
    if (coopshop::StackRoom(10, 10) != 0 || coopshop::StackRoom(10, 1) != 9 || coopshop::StackRoom(1, 3) != 0) Fail("a stack's room is not max - qty");
    if (coopshop::BuyFit(1, 0, coopshop::StackRoom(10, 10), 1) != coopshop::kFitNoRoom) Fail("a FULL matching stack passed the room check (MED 1)");
    if (coopshop::BuyFit(1, 0, coopshop::StackRoom(10, 7), 5) != coopshop::kFitNoRoom) Fail("a matching stack with room 3 passed a shift-all of 5");
    if (coopshop::BuyFit(1, 0, coopshop::StackRoom(10, 1), 1) != coopshop::kFitSend) Fail("one unit onto a one-unit stack of a stackable record was refused (T-236 1)");
    if (coopshop::BuyFit(1, coopshop::CellRoom(1, 50), 0, 50) != coopshop::kFitSend) Fail("a free cell that takes all 50 was refused");
    if (coopshop::RecordOnTail(1, coopshop::kTailAck) != coopshop::kRecAckDrop) Fail("an ack did not drop the record");
    if (coopshop::RecordOnTail(0, coopshop::kTailAck) != coopshop::kRecAckMissed) Fail("an ack with no record was not counted");
    if (coopshop::RecordOnTail(1, coopshop::kTailRevoke) != coopshop::kRecUndo) Fail("a revoke did not undo its record");
    if (coopshop::RecordOnTail(0, coopshop::kTailRevoke) != coopshop::kRecRevokeMissed) Fail("a revoke with no record was not counted revokeMissed");
    if (coopshop::RecordAdmit(63, 64) != 1 || coopshop::RecordAdmit(64, 64) != 0) Fail("a full table admitted a new trade (an unacked record would be evicted)");
    if (coopshop::RecordStale(1, 7, 7) != 0 || coopshop::RecordStale(0, 7, 9) != 0 || coopshop::RecordStale(1, 7, 9) != 1) Fail("the stale-epoch rule is wrong");
    const unsigned int e1 = coopshop::MakeEpoch(1000u, 42u, 7u), e2 = coopshop::MakeEpoch(1000u, 43u, 7u);
    if (e1 == 0 || e2 == 0 || e1 == e2) Fail("the epoch is 0 or repeats across processes");
    if (coopshop::SameRecord(1, e1, 5, 1, e2, 5) != 0) Fail("request id 5 of a restarted requester matched the old record (item 12)");
    if (coopshop::SameRecord(1, e1, 5, 1, e1, 5) != 1 || coopshop::SameRecord(1, e1, 5, 2, e1, 5) != 0) Fail("the record key does not match itself / tell peers apart");
    std::vector<char> b(4, (char)0); b[0] = 1;
    coopshop::EncodeTail(&b, e1, coopshop::kTailAck);
    size_t at = 4; coopshop::ShopTail t;
    if (!coopshop::TagAt(&b[0], b.size(), 4, coopshop::kTailTag) || !coopshop::DecodeTail(&b[0], b.size(), &at, &t) || at != b.size() || t.epoch != e1 || t.kind != coopshop::kTailAck)
        Fail("the shop tail did not round-trip");
    at = 4; if (coopshop::DecodeTail(&b[0], b.size() - 1, &at, &t)) Fail("a cut shop tail decoded");
    std::vector<char> z; coopshop::EncodeTail(&z, 0, coopshop::kTailRevoke); at = 0; if (coopshop::DecodeTail(&z[0], z.size(), &at, &t)) Fail("a shop tail with epoch 0 decoded");
    if (coopshop::LateAnswer(1, 1, 1, coopshop::kVOk, 1) != coopshop::kLateSettle) Fail("a late ok after a failed give-back was revoked, not settled (LOW 11)");
    if (coopshop::LateAnswer(1, 1, 1, coopshop::kVOk, 0) != coopshop::kLateRevoke) Fail("a late ok after a done give-back was not revoked");
    if (coopshop::LateAnswer(1, 0, 1, coopshop::kVSoldOut, 1) != coopshop::kLateNothing) Fail("a late refusal after a failed give-back did something");
}
/* the reservation / escrow outcomes (owner 184 / 185): buy - money at the click, item on confirm, money back on refusal/timeout;
   sale - item in escrow at the click, money on confirm, item back on refusal/timeout; the window re-mirrors on every answer */
void t_b44i_settle()
{
    if (coopshop::Reserve(1) != coopshop::kResOk || coopshop::Reserve(0) != coopshop::kResShort || coopshop::Reserve(-1) != coopshop::kResFailed)
        Fail("the reservation answers are not ok / short / failed");
    if (coopshop::Settle(0, 1, 1) != (coopshop::kActCreate | coopshop::kActRebuild)) Fail("a confirmed buy does not create the item (and keeps the money)");
    if (coopshop::Settle(0, 0, 1) != (coopshop::kActRefund | coopshop::kActRebuild)) Fail("a refused buy does not give the reservation back");
    if (coopshop::Settle(0, 0, 0) != coopshop::kActRebuild) Fail("a refused buy with no reservation refunded money never taken");
    if (coopshop::Settle(1, 1, 1) != (coopshop::kActCredit | coopshop::kActDestroyEscrow | coopshop::kActRebuild)) Fail("a confirmed sale does not pay and drop the escrow");
    if (coopshop::Settle(1, 0, 1) != (coopshop::kActReturnEscrow | coopshop::kActRebuild)) Fail("a refused sale does not put the item back");
    if ((coopshop::Settle(1, 0, 1) & coopshop::kActCredit) != 0) Fail("a refused sale was paid");
}
/* T-447: a sale the shop's pieces took only in part - the holder sells what it took and pays back the rest's price; the seller checks
   the holder's numbers and gets back what its escrow holds beyond the units sold */
void t_t447_partial_sale()
{
    int sn = -1, sp = -1, pb = -1;
    coopshop::HolderSellSplit(5, 10, 50, 5, &sn, &sp, &pb);
    if (sn != 5 || sp != 50 || pb != 0) Fail("a whole sale was split");
    coopshop::HolderSellSplit(5, 10, 50, 2, &sn, &sp, &pb);
    if (sn != 2 || sp != 20 || pb != 30) Fail("a partial sale does not sell the units taken and pay back the rest's price");
    coopshop::HolderSellSplit(5, 10, 50, 0, &sn, &sp, &pb);
    if (sn != 0 || sp != 0 || pb != 50) Fail("a sale nothing took does not pay everything back");
    coopshop::HolderSellSplit(5, 10, 50, 7, &sn, &sp, &pb);
    if (sn != 5 || sp != 50 || pb != 0) Fail("more units than asked were sold");
    coopshop::HolderSellSplit(5, 0, 0, 2, &sn, &sp, &pb);
    if (sn != 2 || sp != 0 || pb != 0) Fail("a free partial sale paid something back");
    if (coopshop::SellerConfirmOk(5, 10, 50, 5, 50) != 1) Fail("a clean whole sale was called a mismatch");
    if (coopshop::SellerConfirmOk(5, 10, 50, 5, 40) != 0) Fail("a whole sale at another price was not counted");
    if (coopshop::SellerConfirmOk(5, 10, 50, 2, 20) != 1) Fail("a clean partial sale was called a mismatch");
    if (coopshop::SellerConfirmOk(5, 10, 50, 2, 25) != 0) Fail("a partial sale at another price was not counted");
    if (coopshop::SellerConfirmOk(5, 10, 50, 6, 60) != 0) Fail("a sale of more units than asked was not counted");
    if (coopshop::SellerConfirmOk(5, 10, 50, 0, 0) != 0) Fail("a confirm naming no units was not counted");
    if (coopshop::SellerRest(5, 5) != 0) Fail("a whole sale gave units back");
    if (coopshop::SellerRest(5, 2) != 3) Fail("a partial sale does not give the unsold units back");
    if (coopshop::SellerRest(5, 0) != 0) Fail("a confirm naming no units gave units back");
    if (coopshop::SellerRest(3, 5) != 0) Fail("an escrow short of the units sold gave units back");
}
/* the holder: its own re-checks in order, sold out, keeper broke (4), no room (2); only engine messages are shown */
void t_b44i_holder()
{
    if (coopshop::HolderCommon(1, 1, 1, 1, 0, 1) != coopshop::kVOk) Fail("a clean request was refused");
    if (coopshop::HolderCommon(1, 1, 1, 1, 0, 0) != coopshop::kVMismatch) Fail("a malformed block was applied");
    if (coopshop::HolderCommon(0, 1, 1, 1, 0, 1) != coopshop::kVNoHome) Fail("an unresolved home was applied");
    if (coopshop::HolderCommon(1, 0, 1, 1, 0, 1) != coopshop::kVNotHolder) Fail("a home this game does not hold was applied");
    if (coopshop::HolderCommon(1, 1, 0, 1, 0, 1) != coopshop::kVKeeper) Fail("a keeper that is not the home's trader was paid");
    if (coopshop::HolderCommon(1, 1, 1, 0, 0, 1) != coopshop::kVPolicy) Fail("the access policy was skipped");
    if (coopshop::HolderCommon(1, 1, 1, 1, 1, 1) != coopshop::kVRefuseNext) Fail("refusenext did not refuse");
    if (coopshop::HolderBuy(coopshop::kVOk, 2, 3) != coopshop::kVSoldOut) Fail("a buy of 3 with 2 held was applied (two buyers of the last unit)");
    if (coopshop::HolderBuy(coopshop::kVOk, 3, 3) != coopshop::kVOk) Fail("an exact stock was refused");
    if (coopshop::HolderBuy(coopshop::kVPolicy, 9, 1) != coopshop::kVPolicy) Fail("a common refusal was overridden by the stock");
    if (coopshop::HolderSell(coopshop::kVOk, 0, 1) != coopshop::kVKeeperBroke) Fail("a sale the keeper cannot pay was applied");
    if (coopshop::HolderSell(coopshop::kVOk, 1, 0) != coopshop::kVNoRoom) Fail("a sale no piece accepts was applied");
    if (coopshop::HolderSell(coopshop::kVOk, -1, 1) != coopshop::kVFault) Fail("a failed keeper pay was applied");
    if (coopshop::HolderSell(coopshop::kVOk, 1, 1) != coopshop::kVOk) Fail("a clean sale was refused");
    if (coopshop::ShowResult(coopshop::kVNoRoom) != 2 || coopshop::ShowResult(coopshop::kVKeeperBroke) != 4) Fail("reasons 2 / 4 are not the engine's messages");
    if (coopshop::ShowResult(coopshop::kVNotHolder) != 0 || coopshop::ShowResult(coopshop::kVSoldOut) != 0) Fail("a refusal with no engine text shows a message");
    int t[4];
    { const int q[] = { 0, 2, 5, 1 }; if (coopshop::TakePlan(q, 4, 4, t) != 4 || t[0] != 0 || t[1] != 2 || t[2] != 2 || t[3] != 0) Fail("the take is not first-holder-first"); }
    { const int q[] = { 1, 1 }; if (coopshop::TakePlan(q, 2, 3, t) != 2) Fail("a short stock planned more than it holds"); }
}
/* the wire: both blocks round-trip, a cut / out-of-range block refuses, and the tag test does not claim a non-block */
void t_b44i_wire()
{
    coopshop::ShopReq r; r.has = 1; r.keeperUid = 4242; r.homeKey = "B:12,34:5"; r.sid = "1234-rations.mod"; r.n = 5; r.unitPrice = 120; r.entry = coopshop::kEntryAll;
    r.epoch = 0x5EED1234u;   /* fold 2 (item 12) */
    std::vector<char> b(3, 'x');
    if (!coopshop::EncodeReq(&b, r)) { Fail("a good request block did not encode"); return; }
    if (!coopshop::TagAt(&b[0], b.size(), 3, coopshop::kReqTag) || coopshop::TagAt(&b[0], b.size(), 3, coopshop::kCfTag)) Fail("the request tag is not told apart");
    size_t at = 3; coopshop::ShopReq d;
    if (!coopshop::DecodeReq(&b[0], b.size(), &at, &d) || at != b.size() || d.keeperUid != 4242 || d.homeKey != r.homeKey || d.sid != r.sid
        || d.n != 5 || d.unitPrice != 120 || d.entry != coopshop::kEntryAll || d.has != 1 || d.epoch != 0x5EED1234u) Fail("the request block did not round-trip");
    { coopshop::ShopReq ze = r; ze.epoch = 0; std::vector<char> zeb; if (coopshop::EncodeReq(&zeb, ze)) Fail("a request with no epoch encoded (item 12)"); }
    at = 3; if (coopshop::DecodeReq(&b[0], b.size() - 1, &at, &d)) Fail("a cut request block decoded");
    coopshop::ShopReq z = r; z.n = 0; std::vector<char> zb; if (coopshop::EncodeReq(&zb, z)) Fail("a request of 0 units encoded");
    /* T-1 B5: 'SHR3' - the stock kind round-trips; an empty home key only for a caravan; the older 'SHR2' still decodes as a home's */
    if (d.stock != coopshop::kStockHome || !coopshop::ReqTagAt(&b[0], b.size(), 3)) Fail("a home's SHR3 block did not round-trip its stock kind");
    {
        coopshop::ShopReq cr = r; cr.stock = coopshop::kStockCaravan; cr.homeKey.clear();
        std::vector<char> cb3;
        if (!coopshop::EncodeReq(&cb3, cr)) Fail("a caravan's request block did not encode");
        else
        {
            size_t ca = 0; coopshop::ShopReq cd;
            if (!coopshop::DecodeReq(&cb3[0], cb3.size(), &ca, &cd) || ca != cb3.size() || cd.stock != coopshop::kStockCaravan || !cd.homeKey.empty()
                || cd.keeperUid != 4242 || cd.n != 5 || cd.epoch != 0x5EED1234u) Fail("a caravan's request block did not round-trip");
            std::vector<char> bad3(cb3); bad3[bad3.size() - 1] = 0;   /* kind 0 with no home key */
            ca = 0; if (coopshop::DecodeReq(&bad3[0], bad3.size(), &ca, &cd)) Fail("a home's block with an empty home key decoded");
            bad3[bad3.size() - 1] = 3;   /* T-608: 2 is now the NPC purchase - 3 is unknown */
            ca = 0; if (coopshop::DecodeReq(&bad3[0], bad3.size(), &ca, &cd)) Fail("an unknown stock kind decoded");
            bad3[bad3.size() - 1] = 2;
            ca = 0; if (coopshop::DecodeReq(&bad3[0], bad3.size(), &ca, &cd)) Fail("an NPC purchase with no counter key decoded");
        }
        coopshop::ShopReq h0 = r; h0.homeKey.clear(); std::vector<char> h0b;
        if (coopshop::EncodeReq(&h0b, h0)) Fail("a home's request with an empty home key encoded");
        coopshop::ShopReq c1 = cr; c1.homeKey = "B:1,2:3"; std::vector<char> c1b;
        if (coopshop::EncodeReq(&c1b, c1)) Fail("a caravan's request naming a home key encoded");
    }
    {
        std::vector<char> v2(b.begin() + 3, b.end() - 1);   /* the same block as 'SHR2': its tag, and no stock byte */
        v2[0] = 'S'; v2[1] = 'H'; v2[2] = 'R'; v2[3] = '2';
        size_t a2 = 0; coopshop::ShopReq d2; d2.stock = coopshop::kStockCaravan;
        if (!coopshop::ReqTagAt(&v2[0], v2.size(), 0) || !coopshop::DecodeReq(&v2[0], v2.size(), &a2, &d2) || a2 != v2.size()
            || d2.stock != coopshop::kStockHome || d2.homeKey != r.homeKey) Fail("an SHR2 block no longer decodes as a home's");
        if (coopshop::ReqTagAt(&b[0], b.size(), 0)) Fail("bytes that are not a request tag were taken for one");
    }
    coopshop::ShopCf c; c.has = 1; c.verdict = coopshop::kVNoRoom; c.show = 2; c.n = 0; c.price = 0; c.pieces.push_back("B:1,2:3"); c.pieces.push_back("B:4,5:6");
    std::vector<char> cb;
    if (!coopshop::EncodeCf(&cb, c)) { Fail("a good confirm block did not encode"); return; }
    at = 0; coopshop::ShopCf e;
    if (!coopshop::DecodeCf(&cb[0], cb.size(), &at, &e) || at != cb.size() || e.verdict != coopshop::kVNoRoom || e.show != 2 || e.pieces.size() != 2
        || e.pieces[1] != "B:4,5:6") Fail("the confirm block did not round-trip");
    at = 0; if (coopshop::DecodeCf(&cb[0], cb.size() - 2, &at, &e)) Fail("a cut confirm block decoded");
    for (int i = 0; i <= coopshop::kMaxPieces; ++i) c.pieces.push_back("k");
    std::vector<char> big; if (coopshop::EncodeCf(&big, c)) Fail("a confirm naming too many pieces encoded");
}
/* T-164 B4-4: THE PIECE IS CHOSEN BY ITEM RECORD - the first piece (chain order) holding the record (0x951BA0's rule); only an
   unreadable record falls back to the first piece; a readable record no piece holds is no piece; a non-piece never answers. */
void t_b44_piece_by_record()
{
    int by = -1;
    { const int ip[] = { 0, 1, 1, 1 }; const int h[] = { 1, 0, 1, 1 };
      if (shopstock::PickPieceByRecord(ip, h, 4, 1, &by) != 2 || by != 1) Fail("the first piece HOLDING the record was not chosen (a non-piece holder must not win)"); }
    { const int ip[] = { 1, 1 }; const int h[] = { 0, 0 };
      if (shopstock::PickPieceByRecord(ip, h, 2, 1, &by) != -1 || by != 0) Fail("a readable record no piece holds still named a piece (the any-section arm is back)"); }
    { const int ip[] = { 0, 1, 1 }; const int h[] = { 0, 0, 1 };
      if (shopstock::PickPieceByRecord(ip, h, 3, 0, &by) != 1 || by != 0) Fail("an unreadable record did not fall back to the first PIECE"); }
    { const int ip[] = { 0, 0 }; const int h[] = { 1, 1 };
      if (shopstock::PickPieceByRecord(ip, h, 2, 0, &by) != -1) Fail("no piece at all still named a section");
      if (shopstock::PickPieceByRecord(ip, h, 0, 1, &by) != -1) Fail("an empty chain named a section"); }
}
/* T-164 B4-4: THE KEEPER IS PAID INTO ITS SQUAD'S MONEY (R-a) OR NOT AT ALL - never a counter's faction fallback. */
/* M4 (owner 203, 2026-09-29) - A UID CARRIES THE MAKING GAME'S SLOT (src/common/uidlayout.h). */
/* The formula AllocateUid used before M4, kept here only as the fails-before case. */
unsigned int M4OldUid(unsigned int pid, unsigned int n)
{
    unsigned int p = (pid ^ (pid >> 8) ^ (pid >> 16)) & 0xFF;
    if (p == 0) p = 1;
    return (p << 24) | (n & 0x00FFFFFF);
}
void t_m4_old_prefix_collides_new_layout_does_not()
{
    /* two games whose process ids fold to the same prefix minted the SAME uids from counter 1 up */
    if (M4OldUid(0x010203u, 1) != M4OldUid(0x030201u, 1)) Fail("the pre-M4 formula no longer collides for pids 0x010203 / 0x030201 - the fails-before case is wrong");
    if (M4OldUid(0x010203u, 1) != 0x01000001u) Fail("pid 0x010203 did not fold to prefix 1");
    /* the same two games as notebook slots 0 and 1 */
    if (coopuid::UidMake(0, 1) == coopuid::UidMake(1, 1)) Fail("slots 0 and 1 mint the same first uid");
}
void t_m4_uidmake_never_collides_across_slots()
{
    static const unsigned int kN[] = { 1u, 2u, 3u, 255u, 256u, 1000u, 0x1FFFFFu, 0x200000u, 0x3FFFFEu, 0x3FFFFFu };
    const int nN = (int)(sizeof(kN) / sizeof(kN[0]));
    std::vector<unsigned int> all;
    for (unsigned int slot = 0; slot <= coopuid::UidSlotMax; ++slot)
        for (int k = 0; k < nN; ++k)
        {
            const unsigned int u = coopuid::UidMake(slot, kN[k]);
            if (u == 0) Fail("UidMake made uid 0 from a counter above 0");
            if (coopuid::UidSlot(u) != slot) Fail("UidSlot(UidMake(s, n)) != s");
            if (coopuid::UidCounter(u) != kN[k]) Fail("UidCounter(UidMake(s, n)) != n");
            all.push_back(u);
        }
    std::sort(all.begin(), all.end());
    for (size_t i = 1; i < all.size(); ++i) if (all[i] == all[i - 1]) { Fail("two (slot, counter) pairs made one uid"); break; }
    if (all.size() != (size_t)(1024 * nN)) Fail("the sweep did not cover slots 0..1023");
    if (coopuid::UidMake(1023, 0x3FFFFFu) != 0xFFFFFFFFu) Fail("slot 1023 counter max is not the top uid");
}
void t_m4_mint_refusals_are_counted_and_never_mint_zero()
{
    coopuid::UidMinter m;
    if (coopuid::UidMint(&m, false) != 0 || m.noSlotDeferred != 1 || m.count != 0 || m.last != coopuid::kUidNoSlot) Fail("not linked: minted, not counted, or the counter moved");
    if (coopuid::UidMint(&m, true) != 0 || m.noBlockDeferred != 1 || m.last != coopuid::kUidNoBlock) Fail("no seat/block: minted, or not counted as uidNoBlockDeferred");
    if (!coopuid::UidBlockGrant(&m, 0, 1, 1024)) Fail("a first grant was refused");
    const unsigned int first = coopuid::UidMint(&m, true);
    if (first == 0) Fail("seat 0's first uid is 0 - the 'unassigned' value");
    if (coopuid::UidSlot(first) != 0 || coopuid::UidCounter(first) != 1) Fail("the first uid is not seat 0, counter 1");
    if (coopuid::UidMint(&m, false) != 0 || m.noSlotDeferred != 2) Fail("a block minted without this link's WELCOME (L2)");
    if (coopuid::UidBlockGrant(&m, 1024, 1, 5)) Fail("a grant for seat 1024 was taken - a seat never exceeds 1023");
    /* the seat's last counter, then the notebook's EXHAUSTED */
    coopuid::UidMinter e;
    if (!coopuid::UidBlockGrant(&e, 5, coopuid::UidCounterMax, coopuid::UidCounterMax)) Fail("the last counter's block was refused");
    if (coopuid::UidMint(&e, true) != coopuid::UidMake(5, coopuid::UidCounterMax)) Fail("the last counter value was not minted");
    if (coopuid::UidMint(&e, true) != 0 || e.noBlockDeferred != 1) Fail("a spent block minted, or its refusal was not counted as no block");
    coopuid::UidBlockNoMore(&e, 5);
    if (coopuid::UidMint(&e, true) != 0 || e.counterExhausted != 1 || e.last != coopuid::kUidCounterExhausted) Fail("an exhausted seat minted or was not counted");
    if (coopuid::UidMint(&e, true) != 0 || e.counterExhausted != 2) Fail("a second exhausted refusal was not counted");
    if (e.noSlotDeferred != 0) Fail("an exhausted refusal was counted as another reason");
    if (coopuid::UidBlockWanted(&e, true, 0, 5000)) Fail("an exhausted game still asks for blocks");
    coopuid::UidMinter z;
    if (!coopuid::UidBlockGrant(&z, 0, 1, 5000)) Fail("a 5000-counter grant was refused");
    for (int i = 0; i < 5000; ++i) if (coopuid::UidMint(&z, true) == 0) { Fail("seat 0 minted uid 0"); break; }
}
/* M4 fold (review 2026-09-29 H1): a restarted game minted from counter 1 again and repeated the uids the other game still held.
   Two minters on the SAME seat with consecutive blocks from the notebook's book never repeat - and the book's per-seat
   high-water survives a write and a read, so the notebook grants the NEXT block after its own restart. */
void t_m4f_restarted_game_never_repeats_a_uid()
{
    coopuid::UidMinter pre1, pre2;   /* the failing case: before the fold each process counted from 1 - here, one block 1..1024 each */
    coopuid::UidBlockGrant(&pre1, 3, 1, 1024); coopuid::UidBlockGrant(&pre2, 3, 1, 1024);
    if (coopuid::UidMint(&pre1, true) != coopuid::UidMint(&pre2, true)) Fail("two processes counting from 1 on one seat did not collide - the fails-before case is wrong");
    coopuidblk::UidBlockBook book;
    unsigned int lo = 0, hi = 0;
    std::vector<unsigned int> all;
    coopuid::UidMinter a;   /* process 1 on seat 3 */
    if (coopuidblk::UidBlockGrant(&book, 3, coopuidblk::kUidBlockSize, &lo, &hi) != 1 || lo != 1 || hi != coopuidblk::kUidBlockSize) Fail("seat 3's first block is not 1..1024");
    if (!coopuid::UidBlockGrant(&a, 3, lo, hi)) Fail("process 1 refused its block");
    for (;;) { const unsigned int u = coopuid::UidMint(&a, true); if (u == 0) break; all.push_back(u); }
    if (all.size() != (size_t)coopuidblk::kUidBlockSize) Fail("process 1 did not mint exactly its block");
    /* the notebook restarts: its book is written and read back */
    coopuidblk::UidBlockBook back; int bad = -1;
    coopuidblk::UidBlockBookParse(coopuidblk::UidBlockBookFormat(book), &back, &bad);
    if (bad != 0 || coopuidblk::UidBlockHighOf(back, 3) != coopuidblk::kUidBlockSize) Fail("the per-seat high-water did not survive a write and a read");
    coopuid::UidMinter b;   /* process 2 on seat 3: the restarted game */
    if (coopuidblk::UidBlockGrant(&back, 3, coopuidblk::kUidBlockSize, &lo, &hi) != 1 || lo != 1025 || hi != 2048) Fail("after a reload the next block is not 1025..2048");
    if (!coopuid::UidBlockGrant(&b, 3, lo, hi)) Fail("process 2 refused its block");
    for (int i = 0; i < 1024; ++i) { const unsigned int u = coopuid::UidMint(&b, true); if (u == 0) { Fail("process 2 stopped inside its block"); break; } all.push_back(u); }
    std::sort(all.begin(), all.end());
    for (size_t i = 1; i < all.size(); ++i) if (all[i] == all[i - 1]) { Fail("a restarted game repeated a uid"); break; }
    if (coopuidblk::UidBlockHighOf(back, 4) != 0) Fail("seat 4 took seat 3's high-water");
    /* the file: a seat named twice keeps the higher number; unreadable lines are counted */
    coopuidblk::UidBlockBook p; int pb = 0;
    coopuidblk::UidBlockBookParse("v1\t3\t1024\r\nv1\t3\t2048\nv1\t3\t10\ngarbage\nv1\t1024\t5\nv1\t2\t4194304\nv2\t1\t1\n\n", &p, &pb);
    if (coopuidblk::UidBlockHighOf(p, 3) != 2048) Fail("a seat named twice stepped back");
    if (pb != 4 || p.high.size() != 1) Fail("unreadable uid_seats.txt lines were not counted or were taken");
}
void t_m4f_slot_1023_and_the_last_block()
{
    coopuid::UidMinter m;
    if (!coopuid::UidBlockGrant(&m, 1023, 1, 1024)) Fail("a grant for seat 1023 was refused");
    const unsigned int u = coopuid::UidMint(&m, true);
    if (u == 0 || coopuid::UidSlot(u) != 1023 || coopuid::UidCounter(u) != 1) Fail("UidMint on seat 1023 did not mint (1023 << 22) | 1");
    coopuidblk::UidBlockBook book; book.high[1023] = coopuid::UidCounterMax - 10;
    unsigned int lo = 0, hi = 0;
    if (coopuidblk::UidBlockGrant(&book, 1023, 1024, &lo, &hi) != 1 || lo != coopuid::UidCounterMax - 9 || hi != coopuid::UidCounterMax) Fail("the last block is not cut at the counter maximum");
    if (coopuidblk::UidBlockGrant(&book, 1023, 1024, &lo, &hi) != 0) Fail("a spent seat was granted another block");
    if (coopuidblk::UidBlockGrant(&book, 1024, 1024, &lo, &hi) != -1) Fail("seat 1024 was granted a block");
    if (coopuidblk::UidBlockGrant(&book, 1, 0, &lo, &hi) != -1) Fail("a block of size 0 was granted");
}
/* M4 fold 2 (re-check H-A (2)): a seat and its blocks are bound to the notebook link generation they arrived on */
void t_m4f2_blocks_bound_to_their_link()
{
    coopuid::UidMinter m;
    if (!coopuid::UidGrantOnLink(&m, 4, 1, 1024, 7)) Fail("a grant on link 7 was refused");
    if (coopuid::UidMintOnLink(&m, true, 7) != coopuid::UidMake(4, 1)) Fail("a block did not mint on its own link");
    if (coopuid::UidMintOnLink(&m, true, 8) != 0 || m.last != coopuid::kUidStaleBlock) Fail("a block of link 7 minted on link 8");
    if (m.staleBlockRefused != 1 || m.staleBlocksDropped != 1 || m.seat != coopuid::UidNoSeat) Fail("the stale block was not refused, counted and dropped");
    if (coopuid::UidMintOnLink(&m, true, 8) != 0 || m.last != coopuid::kUidNoBlock || m.staleBlockRefused != 1) Fail("after the drop the refusal is not 'no block'");
    /* a held block of an earlier link is dropped by the next grant, whose counters are then taken even below the old high */
    coopuid::UidMinter g;
    coopuid::UidGrantOnLink(&g, 2, 1025, 2048, 3);
    if (!coopuid::UidGrantOnLink(&g, 2, 5000, 6000, 4) || g.staleBlocksDropped != 1 || g.curLo != 5000 || g.nextLo != 0 || g.blockGen != 4) Fail("a grant on a new link kept the old link's block");
    /* the ask on a new link names no stale seat */
    coopuid::UidMinter w;
    coopuid::UidGrantOnLink(&w, 6, 1, 1024, 1);
    if (!coopuid::UidBlockWantedOnLink(&w, true, 2, 1000, 5000) || w.seat != coopuid::UidNoSeat) Fail("a new link's ask kept the old seat");
    /* EXHAUSTED binds too */
    coopuid::UidMinter e;
    coopuid::UidNoMoreOnLink(&e, 9, 5);
    if (coopuid::UidMintOnLink(&e, true, 5) != 0 || e.last != coopuid::kUidCounterExhausted) Fail("EXHAUSTED on its link did not refuse as exhausted");
    if (coopuid::UidMintOnLink(&e, true, 6) != 0 || e.last != coopuid::kUidStaleBlock || e.exhausted != 0) Fail("EXHAUSTED of an earlier link survived");
    /* not linked: no check, no drop */
    coopuid::UidMinter u;
    coopuid::UidGrantOnLink(&u, 1, 1, 10, 1);
    if (coopuid::UidMintOnLink(&u, false, 2) != 0 || u.last != coopuid::kUidNoSlot || u.seat != 1) Fail("an unlinked mint touched the blocks");
}
/* M4 fold 2 (re-check): a refused grant leaves the ask out - the 5 s re-ask pacing holds, no ask every frame */
void t_m4f2_refused_grant_keeps_the_ask_pacing()
{
    coopuid::UidMinter m;
    coopuid::UidBlockGrant(&m, 2, 1, 1024);
    for (int i = 0; i < 800; ++i) coopuid::UidMint(&m, true);
    if (!coopuid::UidBlockWanted(&m, true, 1000, 5000)) Fail("did not ask at 3/4");
    coopuid::UidBlockAsked(&m, 1000);
    if (coopuid::UidBlockGrant(&m, 2, 500, 600)) Fail("a stale grant was taken");
    if (coopuid::UidBlockWanted(&m, true, 1001, 5000)) Fail("a refused grant made the game ask again on the next frame");
    if (!coopuid::UidBlockWanted(&m, true, 6000, 5000)) Fail("the ask was never repeated after a refused grant");
    if (!coopuid::UidBlockGrant(&m, 2, 1025, 2048) || m.asked != 0) Fail("a taken grant did not close the ask");
}
/* M4 fold 2 (re-check L-B): the sweep counts a refusal once per stretch of the same reason, every refused tick separately */
void t_m4f2_sweep_counts_once_per_stretch()
{
    coopuid::UidMinter m; int c = 0;
    for (int i = 0; i < 10; ++i) coopuid::UidSweepProbe(&m, true, 1, &c);
    if (m.noBlockDeferred != 1 || m.sweepTicksRefused != 10 || c != 0) Fail("a blockless stretch was not counted exactly once");
    coopuid::UidSweepProbe(&m, false, 1, &c);
    if (m.noSlotDeferred != 1 || c != 1) Fail("a new reason did not start a new stretch");
    coopuid::UidGrantOnLink(&m, 3, 1, 2, 1);
    if (coopuid::UidSweepProbe(&m, true, 1, &c) != coopuid::kUidMinted || c != 0) Fail("a granted block did not end the stretch");
    coopuid::UidMint(&m, true); coopuid::UidMint(&m, true);
    for (int i = 0; i < 5; ++i) coopuid::UidSweepProbe(&m, true, 1, &c);
    if (m.noBlockDeferred != 2 || m.sweepTicksRefused != 16) Fail("the next blockless stretch was not counted once");
    /* a stale seat is its own refusal, counted, then the stretch is 'no block' */
    coopuid::UidGrantOnLink(&m, 3, 3, 9, 1);
    coopuid::UidSweepProbe(&m, true, 2, &c);
    if (m.staleBlockRefused != 1 || c != 1) Fail("a stale seat at the sweep was not counted");
    coopuid::UidSweepProbe(&m, true, 2, &c);
    if (m.noBlockDeferred != 3 || c != 1) Fail("the stretch after a stale drop was not counted");
    /* M-A: the FAILED lines' limit - the first 5 and every 100th */
    if (!coopuid::LogRefusal(5) || coopuid::LogRefusal(6) || !coopuid::LogRefusal(200) || coopuid::LogRefusal(201)) Fail("the FAILED line limit is not 5 then every 100th");
}
void t_m4f_minter_asks_at_three_quarters_and_refuses_stale_grants()
{
    coopuid::UidMinter m;
    if (coopuid::UidBlockWanted(&m, false, 1000, 5000)) Fail("a game with no WELCOME on this link asks for a block");
    if (!coopuid::UidBlockWanted(&m, true, 1000, 5000)) Fail("a linked minter with no block does not ask");
    coopuid::UidBlockAsked(&m, 1000);
    if (coopuid::UidBlockWanted(&m, true, 2000, 5000)) Fail("an unanswered ask was repeated before the retry interval");
    if (!coopuid::UidBlockWanted(&m, true, 6000, 5000)) Fail("an unanswered ask was never repeated");
    if (!coopuid::UidBlockGrant(&m, 2, 1, 1024)) Fail("a grant was refused");
    for (int i = 0; i < 767; ++i) coopuid::UidMint(&m, true);
    if (coopuid::UidBlockWanted(&m, true, 7000, 5000)) Fail("asked before 3/4 of the block was used");
    coopuid::UidMint(&m, true);
    if (!coopuid::UidBlockWanted(&m, true, 7000, 5000)) Fail("did not ask at 3/4 of the block");
    if (!coopuid::UidBlockGrant(&m, 2, 1025, 2048)) Fail("the spare block was refused");
    if (coopuid::UidBlockWanted(&m, true, 7000, 5000)) Fail("asked again while holding a spare");
    if (coopuid::UidBlockGrant(&m, 2, 500, 600)) Fail("a stale grant (below one already taken) was taken");
    if (coopuid::UidBlockGrant(&m, 2, 2049, 3072)) Fail("a third block was taken while current and spare are held");
    if (m.blocksRefused != 2 || m.blocksTaken != 2) Fail("grants were not counted");
    unsigned int u = 0;
    for (int i = 0; i < 257; ++i) u = coopuid::UidMint(&m, true);
    if (coopuid::UidCounter(u) != 1025) Fail("the spare block did not follow the spent one");
    /* the notebook moved this game to another seat (its seat was spent): the old blocks go, the new seat mints */
    if (!coopuid::UidBlockGrant(&m, 9, 1, 1024)) Fail("a grant for a new seat was refused");
    u = coopuid::UidMint(&m, true);
    if (coopuid::UidSlot(u) != 9 || coopuid::UidCounter(u) != 1) Fail("a moved game did not mint under its new seat");
    coopuid::UidBlocksForget(&m);   /* the link went down */
    if (coopuid::UidMint(&m, true) != 0 || m.last != coopuid::kUidNoBlock || m.seat != coopuid::UidNoSeat) Fail("a dropped seat still minted");
    /* the wire */
    coopuidblk::UidBlockMsg w; w.kind = coopuidblk::kUbGrant; w.slot = 1023; w.lo = 1; w.hi = 1024;
    std::vector<char> buf; coopuidblk::EncodeUidBlock(&buf, w);
    coopuidblk::UidBlockMsg r;
    if (buf.size() != 16 || !coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r) || r.kind != w.kind || r.slot != 1023 || r.lo != 1 || r.hi != 1024) Fail("UID_BLOCK did not round-trip");
    if (coopuidblk::DecodeUidBlock(&buf[0], 15, &r)) Fail("a short UID_BLOCK decoded");
    w.lo = 0; buf.clear(); coopuidblk::EncodeUidBlock(&buf, w);
    if (coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r)) Fail("a GRANT from counter 0 decoded");
    w.lo = 1; w.slot = 1024; buf.clear(); coopuidblk::EncodeUidBlock(&buf, w);
    if (coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r)) Fail("a GRANT for seat 1024 decoded");
    w.kind = coopuidblk::kUbAsk; w.slot = 0xFFFFFFFFu; w.lo = 0; w.hi = 0; buf.clear(); coopuidblk::EncodeUidBlock(&buf, w);
    if (!coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r) || r.kind != (unsigned int)coopuidblk::kUbAsk) Fail("an ASK from a game with no seat did not decode");
    w.kind = coopuidblk::kUbNoSeat; buf.clear(); coopuidblk::EncodeUidBlock(&buf, w);
    if (!coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r) || r.kind != (unsigned int)coopuidblk::kUbNoSeat) Fail("a NO_SEAT answer did not decode");
    w.slot = 1; w.kind = 9; buf.clear(); coopuidblk::EncodeUidBlock(&buf, w);
    if (coopuidblk::DecodeUidBlock(&buf[0], buf.size(), &r)) Fail("an unknown UID_BLOCK kind decoded");
}
void t_m4f_spawn_for_own_uid_refused()
{
    if (coopuid::SpawnUidDecide(true) != coopuid::kSpawnUidOwnRefused) Fail("a SPAWN naming a uid this game runs was accepted");
    if (coopuid::SpawnUidDecide(false) != coopuid::kSpawnUidAccept) Fail("a SPAWN for a uid this game does not run was refused");
}
/* OWNER DECISION 205 A: the seat is the lowest free one among the games connected now, skipping spent seats; a 1,025th game gets none */
void t_m4s_seat_pick_lowest_free_with_space()
{
    std::set<unsigned int> held; coopuidblk::UidBlockBook b;
    if (coopuidblk::SeatPick(held, b) != 0) Fail("the first game does not take seat 0");
    held.insert(0); held.insert(1);
    if (coopuidblk::SeatPick(held, b) != 2) Fail("the third game does not take the lowest free seat");
    b.high[2] = coopuid::UidCounterMax;
    if (coopuidblk::SeatPick(held, b) != 3) Fail("a spent seat was picked");
    held.erase(0);
    if (coopuidblk::SeatPick(held, b) != 0) Fail("a freed seat was not reused lowest-first");
    std::set<unsigned int> full;
    for (unsigned int s = 0; s <= coopuid::UidSlotMax; ++s) full.insert(s);
    if (coopuidblk::SeatPick(full, b) != -1) Fail("a 1,025th connected game got a seat");
    full.erase(2);   /* the only free seat is spent */
    if (coopuidblk::SeatPick(full, b) != -1) Fail("a spent seat was handed out");
    full.erase(700);
    if (coopuidblk::SeatPick(full, b) != 700) Fail("the only free seat with counters left was not picked");
}
/* a player leaves, another takes the freed seat and continues above its high-water - even across a notebook restart */
void t_m4s_freed_seat_continues_above_its_high_water()
{
    coopuidblk::UidBlockBook b; std::set<unsigned int> held; unsigned int lo = 0, hi = 0;
    std::vector<unsigned int> all;
    const int s = coopuidblk::SeatPick(held, b); held.insert((unsigned int)s);   /* player A links */
    if (coopuidblk::UidBlockGrant(&b, (unsigned int)s, coopuidblk::kUidBlockSize, &lo, &hi) != 1) Fail("A got no block");
    coopuid::UidMinter a; coopuid::UidBlockGrant(&a, (unsigned int)s, lo, hi);
    for (;;) { const unsigned int u = coopuid::UidMint(&a, true); if (u == 0) break; all.push_back(u); }
    held.erase((unsigned int)s);   /* A unlinks: the seat is freed, its high-water stays */
    coopuidblk::UidBlockBook back; int bad = -1;
    coopuidblk::UidBlockBookParse(coopuidblk::UidBlockBookFormat(b), &back, &bad);
    const int s2 = coopuidblk::SeatPick(held, back); held.insert((unsigned int)s2);   /* player B links */
    if (s2 != s) Fail("B did not take the freed seat");
    if (coopuidblk::UidBlockGrant(&back, (unsigned int)s2, coopuidblk::kUidBlockSize, &lo, &hi) != 1 || lo != coopuidblk::kUidBlockSize + 1) Fail("B's first block does not continue above A's high-water");
    coopuid::UidMinter bm; coopuid::UidBlockGrant(&bm, (unsigned int)s2, lo, hi);
    for (;;) { const unsigned int u = coopuid::UidMint(&bm, true); if (u == 0) break; all.push_back(u); }
    std::sort(all.begin(), all.end());
    for (size_t i = 1; i < all.size(); ++i) if (all[i] == all[i - 1]) { Fail("a new player on a freed seat repeated a uid"); break; }
    if (coopuidblk::SeatPick(held, back) != 1) Fail("A coming back while B holds seat 0 does not take seat 1");
}
void t_b44_keeper_pot_choice()
{
    if (shopstock::KeeperPayRoute(1, 1) != shopstock::kPaySquad) Fail("a keeper with a squad pot is not paid into its squad's money");
    if (shopstock::KeeperPayRoute(1, 0) != shopstock::kPayNoSquadPot) Fail("a keeper with no squad pot is paid somewhere");
    if (shopstock::KeeperPayRoute(0, 1) != shopstock::kPayNoKeeper) Fail("a piece with no keeper is paid");
    if (shopstock::KeeperPayRoute(0, 0) != shopstock::kPayNoKeeper) Fail("no keeper and no pot is paid");
}
void t_t1b4r_charge_sign_and_exact_undo()
{
    if (tradecharge::Charge(1000, 441) != 559) Fail("a buy of 559 is not charge 559");
    if (tradecharge::Charge(1000, 1300) != -300) Fail("a sale paying 300 is not charge -300");
    if (tradecharge::Charge(700, 700) != 0) Fail("an unchanged purse is not charge 0");
    /* ItPursePay REMOVES its amount: the undo applied to the purse after must give the purse before */
    const int cases[][2] = { { 1000, 441 }, { 1000, 1300 }, { 50, 50 }, { 0, 0 }, { 559, 0 } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const int before = cases[i][0], after = cases[i][1];
        const int c = tradecharge::Charge(before, after);
        if (after - tradecharge::UndoPay(c) != before) Fail("the undo of a charge did not restore the purse exactly");
    }
    if (tradecharge::PayToSet(1000, 5000) != -4000 || tradecharge::PayToSet(1000, 200) != 800 || tradecharge::PayToSet(7, 7) != 0)
        Fail("PayToSet is not current - target");
}
void t_t1b4r_charge_ticket_window()
{
    if (!tradecharge::SeqInWindow(10, 10, 12) || !tradecharge::SeqInWindow(11, 10, 12)) Fail("a seq rung inside [lo,hi) was missed");
    if (tradecharge::SeqInWindow(12, 10, 12) || tradecharge::SeqInWindow(9, 10, 12)) Fail("a seq outside [lo,hi) was taken");
    if (tradecharge::SeqInWindow(10, 10, 10)) Fail("an empty window claimed a seq");
    /* the ticket wraps: a window straddling the top */
    const unsigned long top = 0xFFFFFFFFUL;
    if (!tradecharge::SeqInWindow(top, top - 1, 1) || !tradecharge::SeqInWindow(0, top - 1, 1)) Fail("a window across the wrap missed a seq");
    if (tradecharge::SeqInWindow(1, top - 1, 1) || tradecharge::SeqInWindow(top - 2, top - 1, 1)) Fail("a window across the wrap took a seq outside it");
}
void t_t1b4r_charge_is_trade()
{
    if (!tradecharge::IsTrade(1, 1, 559, -1)) Fail("a drag that moved the purse was not a trade");
    if (tradecharge::IsTrade(1, 1, 0, -1)) Fail("a drag that moved no cats was logged as a trade");
    if (!tradecharge::IsTrade(1, 1, 0, 1)) Fail("a right-click money trade the buyer could not afford was not logged (charge 0)");
    if (tradecharge::IsTrade(1, 1, 0, 0)) Fail("a free right-click (not for money, nothing moved) was logged as a trade");
    if (tradecharge::IsTrade(0, 1, 5, 1) || tradecharge::IsTrade(1, 0, 5, 1)) Fail("a charge with an unread purse side was recorded");
}
void t_t1b4r_charge_claim_own_buyer()
{
    if (!tradecharge::ClaimFits(10, 99, 10, 12, 7, 7, 1)) Fail("the buyer's own buy, src in window, was not claimed");
    if (!tradecharge::ClaimFits(99, 11, 10, 12, 7, 7, 2)) Fail("the buyer's own sale, dst in window, was not claimed");
    if (tradecharge::ClaimFits(10, 11, 10, 12, 7, 8, 1)) Fail("a move of another character inside the window took the charge");
    if (tradecharge::ClaimFits(10, 11, 10, 12, 7, 7, 0)) Fail("a non-trade record took the charge");
    if (tradecharge::ClaimFits(10, 11, 10, 12, 0, 0, 1)) Fail("a charge with no buyer was claimed");
    if (tradecharge::ClaimFits(9, 12, 10, 12, 7, 7, 1)) Fail("a record with neither half in the window took the charge");
}
/* T-164 B4-2 / T-199: the refund decision table - what a refused trade gives back. ItPursePay REMOVES its amount. */
void t_t1b42_refund_pick()
{
    int pay = 7;
    /* claimed, measured: exactly the charge back */
    if (tradecharge::RefundPick(1, 1, 1, 180, tradecharge::kRouteRClick, 1, 999, &pay) != tradecharge::kRefundCharge || pay != -180)
        Fail("a claimed right-click buy of 180 did not give back exactly 180 (the price 999 must not be used)");
    if (tradecharge::RefundPick(2, 1, 1, -300, tradecharge::kRouteDrag, -1, 999, &pay) != tradecharge::kRefundCharge || pay != 300)
        Fail("a claimed drag sale of -300 did not take back exactly 300");
    const int purse[][2] = { { 1000, 820 }, { 1000, 1300 } };   /* the purse restored exactly */
    for (int i = 0; i < 2; ++i)
    {
        const int c = tradecharge::Charge(purse[i][0], purse[i][1]);
        tradecharge::RefundPick(c > 0 ? 1 : 2, 1, 1, c, tradecharge::kRouteRClick, 1, 5, &pay);
        if (purse[i][1] - pay != purse[i][0]) Fail("a refused trade's refund did not restore the purse exactly");
    }
    /* measured 0: the engine moved nothing, nothing goes back */
    if (tradecharge::RefundPick(1, 1, 1, 0, tradecharge::kRouteRClick, 1, 500, &pay) != tradecharge::kRefundNothing || pay != 0)
        Fail("a measured charge of 0 paid something");
    /* none: no record (the unclaimed lookup found nothing) */
    if (tradecharge::RefundPick(1, 0, 0, 0, tradecharge::kRouteRClick, -1, 500, &pay) != tradecharge::kRefundNoRecord || pay != 0)
        Fail("a trade with no charge record paid the price");
    /* drag-zero: a drag that cost 0 is never recorded - never fall back to the price */
    if (tradecharge::RefundPick(1, 0, 0, 0, tradecharge::kRouteDrag, -1, 500, &pay) != tradecharge::kRefundNoRecord || pay != 0)
        Fail("a drag with no recorded charge fell back to the price");
    if (tradecharge::RefundPick(1, 1, 0, 0, tradecharge::kRouteDrag, 1, 500, &pay) != tradecharge::kRefundNoRecord || pay != 0)
        Fail("an unmeasured drag fell back to the price");
    /* rclick-money-no-charge: kept unmeasured, the engine said for money - the carried price */
    if (tradecharge::RefundPick(1, 1, 0, 0, tradecharge::kRouteRClick, 1, 500, &pay) != tradecharge::kRefundPrice || pay != -500)
        Fail("an unmeasured right-click money buy did not give back its carried price");
    if (tradecharge::RefundPick(2, 1, 0, 0, tradecharge::kRouteRClick, 1, 500, &pay) != tradecharge::kRefundPrice || pay != 500)
        Fail("an unmeasured right-click money sale did not take back its carried price");
    if (tradecharge::RefundPick(1, 1, 0, 0, tradecharge::kRouteRClick, 0, 500, &pay) != tradecharge::kRefundNoRecord || pay != 0)
        Fail("an unmeasured right-click the engine did NOT call a money trade paid the price");
    if (tradecharge::RefundPick(1, 1, 0, 0, tradecharge::kRouteRClick, 1, 0, &pay) != tradecharge::kRefundNoRecord || pay != 0)
        Fail("an unmeasured right-click with no carried price paid something");
    /* not a trade */
    if (tradecharge::RefundPick(0, 1, 1, 180, tradecharge::kRouteRClick, 1, 180, &pay) != tradecharge::kRefundNothing || pay != 0)
        Fail("a non-trade record paid a refund");
}
/* T-164 B4-2 fold (T-215): the keeper's half follows the refund the purse REALLY got - never the carried price */
void t_t215_keeper_follows_refund()
{
    int kt = 9, amt = 9;
    if (tradecharge::KeeperFollow(0, &kt, &amt) != 0 || kt != 0 || amt != 0) Fail("nothing refunded, yet the keeper's half moved");
    if (tradecharge::KeeperFollow(-180, &kt, &amt) != 1 || kt != 1 || amt != 180)
        Fail("a buy refund of 180 to the purse did not take exactly 180 back out of the keeper's pot");
    if (tradecharge::KeeperFollow(300, &kt, &amt) != 1 || kt != 2 || amt != 300)
        Fail("a sale take-back of 300 from the purse did not give exactly 300 back to the keeper's pot");
    if (tradecharge::KeeperFollow(-2147483647 - 1, &kt, &amt) != 0) Fail("an unrepresentable amount moved the keeper");
    /* the reviewer's case: a price-0 request whose engine step measured a charge of 250 - the keeper follows 250, price unused */
    int pay = 0;
    tradecharge::RefundPick(1, 1, 1, 250, tradecharge::kRouteRClick, 1, 0, &pay);
    if (tradecharge::KeeperFollow(pay, &kt, &amt) != 1 || kt != 1 || amt != 250)
        Fail("a price-0 request with a measured charge of 250: the keeper's half did not follow the refund");
    /* the two halves together conserve the cats: purse + pot is unchanged by refund + undo */
    const int purse0 = 1000, pot0 = 5000, charge = 250;
    const int purse1 = purse0 - charge, pot1 = pot0 + charge;             /* the engine's trade */
    tradecharge::RefundPick(1, 1, 1, charge, tradecharge::kRouteDrag, -1, 0, &pay);
    tradecharge::KeeperFollow(pay, &kt, &amt);
    const int purse2 = purse1 - pay, pot2 = pot1 + (kt == 1 ? -amt : amt);
    if (purse2 != purse0 || pot2 != pot0) Fail("refund plus keeper undo did not restore both purse and pot");
}
/* T-215 B4-2 (money re-check 2a-2c): the keeper as known when the engine moved the money, and what its side does after a refund
   that is NEVER withheld - (i) runner, (ii) local-only, (iii) copy, (iv) unknown. */
void t_t215_keeper_class_at_move()
{
    if (tradecharge::KeeperClassAtMove(0, 1, 7, 1) != tradecharge::kKeeperNone) Fail("a non-counter trade was given a keeper side");
    if (tradecharge::KeeperClassAtMove(1, 1, 7, 1) != tradecharge::kKeeperRunner) Fail("a keeper whose uid is this game's was not (i) the runner");
    if (tradecharge::KeeperClassAtMove(1, 1, 0, 0) != tradecharge::kKeeperLocalOnly)
        Fail("a resolved keeper with no uid (orphan / refused twin) was not (ii) local-only");
    if (tradecharge::KeeperClassAtMove(1, 1, 0, 1) != tradecharge::kKeeperLocalOnly) Fail("a uid-0 keeper was read as this game's runner");
    if (tradecharge::KeeperClassAtMove(1, 1, 9, 0) != tradecharge::kKeeperCopy) Fail("another game's keeper was not (iii) a copy");
    if (tradecharge::KeeperClassAtMove(1, 0, 0, 0) != tradecharge::kKeeperUnknown) Fail("an unresolved keeper was not (iv) unknown");
}
void t_t215_refund_runner_keeper_undone()   /* (i) */
{
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperRunner, 1) != tradecharge::kSideRunnerUndo)
        Fail("(i) a refund on this game's own keeper did not undo the keeper's real pot");
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperRunner, 0) != tradecharge::kSideNothing)
        Fail("(i) the purse did not move, yet the keeper's half moved");
    /* the two halves conserve the cats: a 400 purchase refunded, the runner's pot undone by the same 400 */
    int pay = 0, kt = 0, amt = 0;
    tradecharge::RefundPick(1, 1, 1, 400, tradecharge::kRouteDrag, -1, 0, &pay);
    tradecharge::KeeperFollow(pay, &kt, &amt);
    const int purse = 600 - pay, pot = 5400 + (kt == 1 ? -amt : amt);
    if (purse != 1000 || pot != 5000) Fail("(i) refund plus runner undo did not restore purse 1000 and pot 5000");
}
void t_t215_refund_local_only_keeper_undone()   /* (ii) */
{
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperLocalOnly, 1) != tradecharge::kSideLocalUndo)
        Fail("(ii) a refund on a local-only keeper (nobody announces its pot) did not undo that local pot - money created");
    /* the local pot is undone by exactly the refunded amount, both ways: a sale of 300 taken back gives the pot its 300 back */
    int pay = 0, kt = 0, amt = 0;
    tradecharge::RefundPick(2, 1, 1, -300, tradecharge::kRouteRClick, 1, 0, &pay);
    if (pay != 300 || tradecharge::KeeperFollow(pay, &kt, &amt) != 1 || kt != 2 || amt != 300)
        Fail("(ii) a sale take-back of 300 did not give exactly 300 back to the local pot");
    if (tradecharge::KeeperPotMoved(pay) != -300) Fail("(ii) the engine's sale did not read as -300 on the keeper's pot");
}
void t_t215_refund_copy_keeper_left()   /* (iii) */
{
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperCopy, 1) != tradecharge::kSideLeftToRunner)
        Fail("(iii) a refund on a copy keeper wrote this game's copy of the pot (its runner's announcement owns it)");
    /* no money created: the runner never saw the refused charge, and the copy's local move is overwritten by the runner's value
       (SquadCatsSafePointDrain) - so the player is paid in full and nothing is written on the copy */
    int pay = 0;
    tradecharge::RefundPick(1, 1, 1, 250, tradecharge::kRouteDrag, -1, 0, &pay);
    if (750 - pay != 1000) Fail("(iii) a 250 purchase refused on a copy keeper did not give the player its 250 back");
}
void t_t215_refund_keeper_unknown_paid()   /* (iv) */
{
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperUnknown, 1) != tradecharge::kSideUnknownPaid)
        Fail("(iv) a refund with the keeper unknown was not paid-and-counted");
    if (tradecharge::KeeperSideAfterRefund(tradecharge::kKeeperNone, 1) != tradecharge::kSideNothing)
        Fail("a non-counter refund was given a keeper side");
    /* never withheld: every class pays the purse what RefundPick says - the side rule only follows a purse that moved */
    int pay = 0;
    if (tradecharge::RefundPick(1, 1, 1, 500, tradecharge::kRouteDrag, -1, 0, &pay) != tradecharge::kRefundCharge || pay != -500)
        Fail("(iv) a 500 purchase refused with the keeper unknown did not give the player its 500 back");
}
/* T-215 B4-2 (re-check 1b): the prediction KeeperPotAnnounced nets out is on the undo's basis - the measured charge, price unused */
void t_t215_announced_prediction_measured()
{
    int pay = 0;
    tradecharge::RefundPick(1, 1, 1, 250, tradecharge::kRouteRClick, 1, 0, &pay);   /* price 0, measured 250 */
    if (tradecharge::KeeperPotMoved(pay) != 250) Fail("a price-0 purchase measured at 250 was not predicted as +250 on the keeper's pot");
    tradecharge::RefundPick(1, 1, 1, 180, tradecharge::kRouteRClick, 1, 500, &pay);   /* price 500 carried, 180 measured */
    if (tradecharge::KeeperPotMoved(pay) != 180) Fail("the carried price, not the measured charge, was predicted");
    tradecharge::RefundPick(1, 1, 0, 0, tradecharge::kRouteDrag, -1, 500, &pay);      /* no record: nothing undone, nothing predicted */
    if (tradecharge::KeeperPotMoved(pay) != 0) Fail("a row with no recorded charge was predicted");
}
/* T-164 B4-2 fold (T-215): the process's first move rings kFirstTicket, and its charge must be claimable (ticket 0 never is) */
void t_t215_first_ticket_claimable()
{
    const unsigned long t0 = (unsigned long)tradecharge::kFirstTicket;
    /* fails when kFirstTicket is 0 too: ClaimFits reads a 0 seq as a cleared entry */
    if (!tradecharge::ClaimFits(t0, t0, t0, t0 + 1, 7, 7, 1)) Fail("the first move of the process could not claim its charge (first ticket 0?)");
    if (tradecharge::ClaimFits(0, 0, 0, 1, 7, 7, 1)) Fail("a ticket-0 half claimed a charge");
}
void t_t1b42_keep_unmeasured()
{
    if (!tradecharge::KeepUnmeasured(tradecharge::kRouteRClick, 0, 1, 1) || !tradecharge::KeepUnmeasured(tradecharge::kRouteRClick, 1, 0, 1))
        Fail("a right-click money trade with an unread purse side was not kept");
    if (tradecharge::KeepUnmeasured(tradecharge::kRouteRClick, 1, 1, 1)) Fail("a measured right-click was kept as unmeasured");
    if (tradecharge::KeepUnmeasured(tradecharge::kRouteRClick, 0, 1, 0) || tradecharge::KeepUnmeasured(tradecharge::kRouteRClick, 0, 1, -1))
        Fail("a right-click the engine did not call a money trade was kept");
    if (tradecharge::KeepUnmeasured(tradecharge::kRouteDrag, 0, 1, 1)) Fail("a drag was kept unmeasured");
}
void t_t1b42_claim_ticket0()
{
    const unsigned long top = 0xFFFFFFFFUL;
    if (tradecharge::ClaimFits(0, 0, top - 1, 1, 7, 7, 1)) Fail("two cleared (ticket 0) halves claimed a window across the wrap");
    if (tradecharge::ClaimFits(0, 0, 0, 5, 7, 7, 1)) Fail("two cleared (ticket 0) halves claimed a window starting at 0");
    if (!tradecharge::ClaimFits(0, 3, 0, 5, 7, 7, 1)) Fail("a real half in the window lost the claim because the other half was cleared");
    if (!tradecharge::ClaimFits(top, 0, top - 1, 1, 7, 7, 2)) Fail("a real half across the wrap lost the claim beside a cleared half");
}
void t_t1b42_gate_names()
{
    for (int g = 0; g < tradecharge::kGateCount; ++g)
        if (tradecharge::GateIndex(tradecharge::GateName(g)) != g) Fail("a refusal gate's name does not name it back");
    if (tradecharge::GateIndex("nope") != -1 || tradecharge::GateIndex("") != -1 || tradecharge::GateIndex(0) != -1
        || tradecharge::GateIndex("policyX") != -1 || tradecharge::GateIndex("polic") != -1)
        Fail("an unknown gate name was accepted");
    if (std::string(tradecharge::GateName(-1)) != "?" || std::string(tradecharge::GateName(tradecharge::kGateCount)) != "?")
        Fail("an out-of-range gate has a name");
}
void t_t1b4r_keeper_pick_cheapest_affordable()
{
    /* T549: purse 1000; stock values 1200, 559, 300, 300, 2000, unreadable */
    const int purse = 1000;
    const int have[] = { 1, 1, 1, 1, 1, 0 };
    const int val[] = { 1200, 559, 300, 300, 2000, 1 };
    int haveBest = 0, best = 0, idx = -1;
    for (int i = 0; i < 6; ++i)
        if (tradecharge::CheaperAffordable(have[i], val[i], 1, purse, haveBest, best)) { haveBest = 1; best = val[i]; idx = i; }
    if (idx != 2 || best != 300) Fail("the cheapest affordable item (first of equals) was not picked");
    haveBest = 0; idx = -1;
    for (int i = 0; i < 6; ++i)
        if (tradecharge::CheaperAffordable(have[i], val[i], 1, 100, haveBest, best)) { haveBest = 1; best = val[i]; idx = i; }
    if (idx != -1) Fail("an item dearer than the purse was picked (the caller must fall back)");
    if (tradecharge::CheaperAffordable(1, 10, 0, 1000, 0, 0)) Fail("an unreadable purse still picked");
    if (!tradecharge::CheaperAffordable(1, 1000, 1, 1000, 0, 0)) Fail("an item costing exactly the purse was refused");
    if (tradecharge::CheaperAffordable(1, -5, 1, 1000, 0, 0)) Fail("a negative value was picked");
}
void t_t1b4r_cats_parse()
{
    int v = 0;
    if (!tradecharge::ParseCats("cats=5000", &v) || v != 5000) Fail("cats=5000 did not parse");
    if (!tradecharge::ParseCats("cats=0", &v) || v != 0) Fail("cats=0 did not parse");
    if (tradecharge::ParseCats("cats=", &v) || v != -1) Fail("a bare cats= parsed");
    if (tradecharge::ParseCats("cats=-5", &v) || tradecharge::ParseCats("cats=12x", &v) || tradecharge::ParseCats("cat=5", &v))
        Fail("a malformed cats token parsed");
    if (tradecharge::ParseCats("cats=100000001", &v) || tradecharge::ParseCats(0, &v)) Fail("an over-cap or null cats token parsed");
}
/* M16 (T-197): what waits to be sent (src/common/sendbound.h) and the held positions' expiry (writequeue.h). */
coopsb::Item M16Item(int cls, unsigned int a, unsigned int uid, char tag, size_t bytes)
{
    coopsb::Item it; it.cls = cls; it.key.a = a; it.key.c = uid; it.bytes.assign(bytes, tag); it.type = a;
    return it;
}
void t_m16_direct_until_full()
{
    coopsb::Outbox b;
    const coopsb::Item rel = M16Item(coopsb::kClassMust, 37, 0, 'y', 10);   /* reliable (channel 0) */
    if (!b.Direct(0, 0, rel)) Fail("an empty box with an empty network queue did not send straight out");
    if (!b.Direct(coopsb::kNetCountMax - 1, coopsb::kNetBytesMax - 1, rel)) Fail("a network queue just under both marks was treated as full");
    if (b.Direct(coopsb::kNetCountMax, 0, rel)) Fail("a network queue at the packet mark was not full");
    if (b.Direct(0, coopsb::kNetBytesMax, rel)) Fail("a network queue at the byte mark was not full");
    b.Put(M16Item(coopsb::kClassMust, 37, 0, 'x', 10));
    if (b.Direct(0, 0, rel)) Fail("a reliable message went straight out while another waited (it would overtake it)");
    coopsb::Item o;
    if (b.Take(coopsb::kNetCountMax, 0, &o)) Fail("a waiting message was taken while the network queue was full");
    if (!b.Take(0, 0, &o) || o.bytes.size() != 10 || o.type != 37) Fail("the waiting message did not come out once there was room");
    if (!b.Direct(0, 0, rel) || b.Count() != 0 || b.Bytes() != 0) Fail("the box did not empty");
}
void t_m16_latest_keeps_newest_per_subject()
{
    coopsb::Outbox b;
    if (b.Put(M16Item(coopsb::kClassLatest, 12, 1, 'a', 36))) Fail("the first MOVE replaced nothing yet answered true");
    b.Put(M16Item(coopsb::kClassLatest, 12, 2, 'b', 36));
    b.Put(M16Item(coopsb::kClassMust, 37, 1, 'c', 50));
    b.Put(M16Item(coopsb::kClassLatest, 14, 1, 'd', 80));   /* STATE of character 1 - another subject than its MOVE */
    if (!b.Put(M16Item(coopsb::kClassLatest, 12, 1, 'e', 36))) Fail("a newer MOVE for character 1 did not replace the waiting one");
    if (!b.Put(M16Item(coopsb::kClassLatest, 14, 1, 'f', 80))) Fail("a newer STATE for character 1 did not replace the waiting one");
    if (b.Count() != 4 || b.Bytes() != 36 + 50 + 36 + 80) Fail("count/bytes after two replacements are wrong");
    const char expect[] = { 'b', 'c', 'e', 'f' };
    coopsb::Item o;
    for (int i = 0; i < 4; ++i)
        if (!b.Take(0, 0, &o) || o.bytes.empty() || o.bytes[0] != expect[i]) Fail("the waiting messages did not come out oldest first with the replaced ones at the back");
    if (b.Put(M16Item(coopsb::kClassLatest, 12, 1, 'g', 36))) Fail("a MOVE put after its subject had gone out replaced a stale entry");
}
/* M16 fold 3 (T775): MOVE / STATE ride unreliable (channel 1) on the world-server road; one that crosses a sector edge rides
   reliable (channel 0) - a newer unreliable value that replaces it while it waits must not make it unreliable. Waiting
   unreliable values go out before the reliable line. */
void t_m16_latest_keeps_reliability()
{
    coopsb::Outbox b;
    coopsb::Item rel = M16Item(coopsb::kClassLatest, 12, 1, 'a', 36); rel.channel = coopsb::kChannelReliable;
    coopsb::Item u1 = M16Item(coopsb::kClassLatest, 12, 1, 'b', 36); u1.channel = 1;
    coopsb::Item u2 = M16Item(coopsb::kClassLatest, 12, 2, 'c', 36); u2.channel = 1;
    coopsb::Item u3 = M16Item(coopsb::kClassLatest, 12, 2, 'd', 36); u3.channel = 1;
    b.Put(rel); b.Put(u2);
    if (!b.Put(u1)) Fail("a newer MOVE did not replace the waiting reliable one");
    if (!b.Put(u3)) Fail("a newer unreliable MOVE did not replace the waiting unreliable one");
    coopsb::Item o;
    if (!b.Take(0, 0, &o) || o.bytes[0] != 'd' || o.channel != 1) Fail("an unreliable value replacing an unreliable one did not stay unreliable, or did not go out first");
    if (!b.Take(0, 0, &o) || o.bytes[0] != 'b' || o.channel != coopsb::kChannelReliable) Fail("a newer unreliable MOVE made a waiting reliable one (a sector crossing) unreliable");
    if (b.Take(0, 0, &o)) Fail("more came out than was put");
}
/* An unreliable MOVE or STATE promises no order: it goes straight to the network past reliable messages that wait (a large
   zone record), waits only while the network queue itself is full (newest per character), removes an out-of-date waiting
   value of its character when it goes straight out, and merges into a waiting RELIABLE value of its character instead of
   passing it (a sector crossing). */
void t_m16_unreliable_passes_the_line()
{
    coopsb::Outbox b;
    const coopsb::Item rec = M16Item(coopsb::kClassMust, 37, 0, 'r', 100);   /* reliable */
    b.Put(rec);
    coopsb::Item mv = M16Item(coopsb::kClassLatest, 12, 1, 'm', 36); mv.channel = 1;
    if (b.Direct(0, 0, rec)) Fail("a reliable message went straight out while another reliable one waited");
    bool rep = true;
    if (!b.Direct(0, 0, mv, &rep) || rep) Fail("an unreliable MOVE did not pass a waiting reliable message");
    if (b.Direct(coopsb::kNetCountMax, 0, mv) || b.Direct(0, coopsb::kNetBytesMax, mv)) Fail("an unreliable MOVE went straight out while the network queue was full");
    coopsb::Item mu = M16Item(coopsb::kClassMust, 40, 0, 'u', 8); mu.channel = 1;
    if (!b.Direct(0, 0, mu)) Fail("an unreliable message of no character did not pass a waiting reliable message");
    if (b.Count() != 1 || b.Bytes() != 100) Fail("deciding to send straight out changed what waits");
    b.Put(mv);
    coopsb::Item mv2 = mv; mv2.bytes.assign(36, 'n');
    if (!b.Put(mv2)) Fail("a newer unreliable MOVE waiting on a full network queue did not replace the older one");
    coopsb::Item st = M16Item(coopsb::kClassLatest, 14, 1, 's', 80); st.channel = 1;
    b.Put(st);
    if (b.Count() != 3 || b.Bytes() != 100 + 36 + 80) Fail("count/bytes with unreliable values waiting are wrong");
    coopsb::Item mv3 = mv; mv3.bytes.assign(36, 'o');
    rep = false;
    if (!b.Direct(0, 0, mv3, &rep) || !rep) Fail("a newer unreliable MOVE going straight out did not remove the out-of-date waiting one");
    if (b.Count() != 2 || b.Bytes() != 100 + 80) Fail("the removed out-of-date MOVE was not taken off the counts");
    if (b.Put(mv3)) Fail("a MOVE whose waiting value was removed still had it remembered");
    coopsb::Item o;
    if (!b.Take(0, 0, &o) || o.bytes[0] != 's') Fail("a waiting unreliable STATE did not go out before the reliable line");
    if (!b.Take(0, 0, &o) || o.bytes[0] != 'o') Fail("waiting unreliable values did not go out oldest first");
    if (!b.Take(0, 0, &o) || o.bytes[0] != 'r' || o.channel != coopsb::kChannelReliable) Fail("the reliable message did not follow");
    if (b.Take(0, 0, &o)) Fail("more came out than was put");
    b.Put(M16Item(coopsb::kClassLatest, 12, 2, 'c', 36));   /* a reliable MOVE (a sector crossing) waits */
    coopsb::Item other = M16Item(coopsb::kClassLatest, 12, 3, 'x', 36); other.channel = 1;
    if (!b.Direct(0, 0, other)) Fail("another character's unreliable MOVE did not pass a waiting sector crossing");
    coopsb::Item mv4 = M16Item(coopsb::kClassLatest, 12, 2, 'd', 36); mv4.channel = 1;
    if (b.Direct(0, 0, mv4)) Fail("an unreliable MOVE passed a waiting reliable value of the same character (a sector crossing)");
    if (!b.Put(mv4)) Fail("the unreliable MOVE did not merge into the waiting sector crossing");
    if (!b.Take(0, 0, &o) || o.bytes[0] != 'd' || o.channel != coopsb::kChannelReliable) Fail("the merged MOVE did not stay reliable");
    if (!b.Empty() || b.Count() != 0 || b.Bytes() != 0) Fail("the box did not empty");
}
void t_m16_must_deliver_never_dropped()
{
    coopsb::Outbox b;
    int marks = 0;
    for (int i = 0; i < 5000; ++i)
    {
        if (b.Put(M16Item(coopsb::kClassMust, 37, 7, (char)(i & 127), 20))) Fail("a must-deliver message replaced another");
        if (b.GrowthMark()) ++marks;
    }
    if (b.Count() != 5000 || b.Bytes() != 100000) Fail("must-deliver messages were dropped or miscounted while the link was full");
    if (marks != 3) Fail("the growth line did not fire exactly at 1024, 2048 and 4096 waiting");
    coopsb::Item o; int n = 0;
    while (b.Take(0, 0, &o)) { if (o.bytes[0] != (char)(n & 127)) Fail("must-deliver messages came out of order"); ++n; }
    if (n != 5000) Fail("not every must-deliver message came out");
    b.Put(M16Item(coopsb::kClassMust, 37, 7, 'z', 1));
    if (b.GrowthMark()) Fail("the growth line did not restart after the box emptied");
}
void t_m16_classify()
{
    if (coopsb::kLiveUpHeader != cooplive::kLiveUpHeader || coopsb::kLiveDownHeader != cooplive::kLiveDownHeader
        || coopsb::kTypeMove != cooplive::kInnerMove || coopsb::kTypeState != cooplive::kInnerState || coopsb::kTypeLive != 53u)
        Fail("sendbound.h's envelope numbers disagree with liveenvelope.h");
    unsigned int uid = 0x01020304; char mv[36]; std::memset(mv, 0, sizeof(mv)); std::memcpy(mv, &uid, 4);
    coopsb::Key k, k2;
    if (coopsb::ClassOf(12, mv, 36, coopsb::kDirUp, &k) != coopsb::kClassLatest || k.c != uid) Fail("a plain MOVE is not latest-value keyed by its uid");
    if (coopsb::ClassOf(14, mv, 36, coopsb::kDirUp, &k2) != coopsb::kClassLatest || !(k < k2 || k2 < k)) Fail("a STATE shares a MOVE's subject");
    if (coopsb::ClassOf(12, mv, 3, coopsb::kDirUp, &k) != coopsb::kClassMust) Fail("a MOVE too short to name its character was not must-deliver");
    if (coopsb::ClassOf(37, mv, 36, coopsb::kDirUp, &k) != coopsb::kClassMust) Fail("an ITEM_MOVE was not must-deliver");
    std::vector<char> up1, up2, up3, dn1, dn2, dn3;
    cooplive::LiveUpEncode(&up1, cooplive::kRouteArea, 77, cooplive::kInnerMove, mv, 36);
    cooplive::LiveUpEncode(&up2, cooplive::kRouteArea, 78, cooplive::kInnerMove, mv, 36);
    cooplive::LiveUpEncode(&up3, cooplive::kRouteArea, 77, 37, mv, 36);
    if (up1.empty() || up2.empty() || up3.empty()) Fail("setup: a LIVE up envelope did not encode");
    coopsb::Key a1, a2, a3;
    if (coopsb::ClassOf(53, &up1[0], up1.size(), coopsb::kDirUp, &a1) != coopsb::kClassLatest || a1.c != uid) Fail("a LIVE MOVE going up is not latest-value keyed by its uid");
    if (coopsb::ClassOf(53, &up2[0], up2.size(), coopsb::kDirUp, &a2) != coopsb::kClassLatest || !(a1 < a2 || a2 < a1)) Fail("LIVE MOVEs to two targets share a subject");
    if (coopsb::ClassOf(53, &up3[0], up3.size(), coopsb::kDirUp, &a3) != coopsb::kClassMust) Fail("a LIVE item message going up was not must-deliver");
    if (coopsb::ClassOf(53, &up1[0], up1.size() - 1, coopsb::kDirUp, &a3) != coopsb::kClassMust) Fail("a LIVE frame whose length disagrees was not must-deliver");
    cooplive::LiveDownEncode(&dn1, 3, cooplive::kInnerState, mv, 36);
    cooplive::LiveDownEncode(&dn2, 4, cooplive::kInnerState, mv, 36);
    cooplive::LiveDownEncode(&dn3, 3, 37, mv, 36);
    if (dn1.empty() || dn2.empty() || dn3.empty()) Fail("setup: a LIVE down envelope did not encode");
    if (coopsb::ClassOf(53, &dn1[0], dn1.size(), coopsb::kDirDown, &a1) != coopsb::kClassLatest || a1.c != uid || a1.b != 3) Fail("a relayed LIVE STATE is not latest-value keyed by origin and uid");
    if (coopsb::ClassOf(53, &dn2[0], dn2.size(), coopsb::kDirDown, &a2) != coopsb::kClassLatest || !(a1 < a2 || a2 < a1)) Fail("relayed STATEs from two senders share a subject");
    if (coopsb::ClassOf(53, &dn3[0], dn3.size(), coopsb::kDirDown, &a3) != coopsb::kClassMust) Fail("a relayed LIVE item message was not must-deliver");
    if (coopsb::ClassOf(53, 0, 0, coopsb::kDirDown, &a3) != coopsb::kClassMust) Fail("no payload was not must-deliver");
}
void t_m16_link_gone_and_edges()
{
    coopsb::Outbox b;
    if (b.Edge() != 0) Fail("an empty box reported an edge");
    b.Put(M16Item(coopsb::kClassMust, 37, 0, 'x', 5)); b.Put(M16Item(coopsb::kClassLatest, 12, 9, 'y', 5));
    if (b.Edge() != 1 || b.Edge() != 0) Fail("congestion did not report one rising edge");
    coopsb::Item o;
    if (!b.TakeAny(&o) || o.bytes[0] != 'x') Fail("TakeAny did not hand out the oldest regardless of room");
    if (b.Clear() != 1 || b.Count() != 0 || b.Bytes() != 0) Fail("a gone link did not throw away (and count) what waited");
    if (b.Edge() != -1) Fail("the box emptying did not report a falling edge");
    if (b.Put(M16Item(coopsb::kClassLatest, 12, 9, 'z', 5))) Fail("a cleared latest-value subject was still remembered");
}
/* M16 fold 1: a game being disconnected gets every frame waiting for it handed to ENet first - TakeAny ignores the full
   network queue, keeps the order, includes the latest-value entries, and leaves the box empty with no subject remembered. */
void t_m16_drain_before_disconnect()
{
    coopsb::Outbox b;
    b.Put(M16Item(coopsb::kClassMust, 37, 0, 'a', 10));
    b.Put(M16Item(coopsb::kClassLatest, 12, 5, 'b', 36));
    b.Put(M16Item(coopsb::kClassMust, 40, 0, 'c', 20));
    if (!b.Put(M16Item(coopsb::kClassLatest, 12, 5, 'd', 36))) Fail("setup: the newer MOVE did not replace the waiting one");
    coopsb::Item x;
    if (b.Take(coopsb::kNetCountMax, 0, &x)) Fail("Take handed out a frame while the network queue was full");
    std::string got;
    while (b.TakeAny(&x)) { if (x.bytes.empty()) { Fail("TakeAny handed out an empty frame"); break; } got += x.bytes[0]; }
    if (got != "acd") Fail("the drain before a disconnect was not every waiting frame, in order");
    if (b.Count() != 0 || b.Bytes() != 0 || !b.Empty()) Fail("the box was not empty after the drain");
    if (b.Put(M16Item(coopsb::kClassLatest, 12, 5, 'e', 36))) Fail("a drained latest-value subject was still remembered");
}
/* M13: ONE PACKET PER PASS PER DESTINATION (src/common/sendbundle.h). */
std::vector<char> M13Frame(unsigned int type, const std::string& payload)
{
    std::vector<char> f(5 + payload.size());
    f[0] = (char)(unsigned char)type;
    const unsigned int n = (unsigned int)payload.size(); std::memcpy(&f[1], &n, 4);
    if (!payload.empty()) std::memcpy(&f[5], payload.data(), payload.size());
    return f;
}
std::string M13Key(unsigned int type, const std::string& payload) { return std::string(1, (char)(unsigned char)type) + ":" + payload; }
/* One frame as the receiving end reads it: a bundle as its messages in order, any other frame as itself. false = refused. */
bool M13Unpack(const std::vector<char>& f, std::vector<std::string>* got)
{
    if (f.size() < 5) return false;
    unsigned int n = 0; std::memcpy(&n, &f[1], 4);
    if ((size_t)n != f.size() - 5) return false;
    const unsigned int type = (unsigned char)f[0];
    const std::string body(f.begin() + 5, f.end());
    if (type != coopbundle::kMsgBundle) { got->push_back(M13Key(type, body)); return true; }
    std::vector<coopbundle::Entry> es;
    if (!coopbundle::Decode(body.data(), body.size(), &es)) return false;
    for (size_t i = 0; i < es.size(); ++i) got->push_back(M13Key(es[i].type, body.substr(es[i].at, es[i].len)));
    return true;
}
/* The budget is ENet's own one-command limit (enet_peer_send: mtu - 4-byte datagram header - 24-byte fragment command). */
void t_m13_budget_is_enet_fragment_limit()
{
    if (coopbundle::Budget(1392, false) != 1364) Fail("the budget at ENet's default MTU of 1392 is not 1364");
    if (coopbundle::Budget(1392, true) != 1360) Fail("a checksum's 4 bytes were not taken off the budget");
    if (coopbundle::Budget(100, false) != 548 || coopbundle::Budget(9000, false) != 4068) Fail("the MTU was not clamped to ENet's 576..4096");
    const std::vector<char> fits = M13Frame(12, std::string(1356, 'a')), over = M13Frame(12, std::string(1357, 'b'));
    if (fits.size() != 1361 || !coopbundle::Fits(&fits[0], fits.size(), 1364)) Fail("a 1361-byte frame (a 1364-byte bundle of one) did not fit");
    if (coopbundle::Fits(&over[0], over.size(), 1364)) Fail("a 1362-byte frame was let into a bundle that would pass 1364");
}
void t_m13_round_trip_in_order()
{
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first, out; size_t fe = 9;
    const std::vector<char> a = M13Frame(37, "abc"), b = M13Frame(53, ""), d = M13Frame(28, std::string(100, 'r'));
    if (c.Offer(coopbundle::kLaneReliable, &a[0], a.size(), 1364, &first, &fe, &tl) != coopbundle::kTaken || !first.empty() || fe != 0) Fail("a small frame was not taken");
    c.Offer(coopbundle::kLaneReliable, &b[0], b.size(), 1364, &first, &fe, &tl);
    c.Offer(coopbundle::kLaneReliable, &d[0], d.size(), 1364, &first, &fe, &tl);
    if (c.OpenPackets() != 1 || c.OpenBytes() != 5 + 6 + 3 + 103 || c.OpenEntries() != 3) Fail("the open bundle's packets/bytes/entries are wrong");
    if (c.Seal(coopbundle::kLaneReliable, &out, &tl) != 3) { Fail("the seal did not carry three messages"); return; }
    if ((unsigned int)(unsigned char)out[0] != coopbundle::kMsgBundle || out.size() != 5 + 6 + 3 + 103) Fail("the sealed frame is not a bundle of the expected size");
    std::vector<std::string> got;
    if (!M13Unpack(out, &got) || got.size() != 3 || got[0] != M13Key(37, "abc") || got[1] != M13Key(53, "") || got[2] != M13Key(28, std::string(100, 'r')))
        Fail("the bundle did not unpack to the same messages in the same order");
    if (tl.bundles != 1 || tl.bundled != 3 || tl.single != 0 || c.OpenPackets() != 0 || c.Seal(coopbundle::kLaneReliable, &out, &tl) != 0 || !out.empty())
        Fail("after the seal the lane was not empty, or the tally is wrong");
}
/* A bundle that ends up holding one message goes as that message's own frame, byte for byte. */
void t_m13_single_goes_as_itself()
{
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first, out; size_t fe = 0;
    const std::vector<char> a = M13Frame(14, std::string(36, 's'));
    c.Offer(coopbundle::kLaneUnreliable, &a[0], a.size(), 1364, &first, &fe, &tl);
    if (c.OpenBytes() != a.size()) Fail("an open bundle of one was not counted at its own frame's size");
    if (c.Seal(coopbundle::kLaneUnreliable, &out, &tl) != 1 || out != a) Fail("a single message was not sent as its own frame");
    if (tl.single != 1 || tl.bundles != 0 || tl.saved != 0) Fail("a single message was counted as a bundle or as a saving");
}
/* Many small messages split into bundles none of which passes the budget, and come out in order (before M13 there was no
   splitter: each message was its own packet). */
void t_m13_split_at_budget()
{
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first; size_t fe = 0;
    std::vector<std::vector<char> > wire; std::vector<std::string> want;
    for (int i = 0; i < 100; ++i)
    {
        std::string p(36, (char)('A' + (i % 26))); p[0] = (char)i;
        const std::vector<char> f = M13Frame(12, p); want.push_back(M13Key(12, p));
        if (c.Offer(coopbundle::kLaneUnreliable, &f[0], f.size(), 1364, &first, &fe, &tl) != coopbundle::kTaken) Fail("a MOVE-sized frame was not taken");
        if (fe != 0) wire.push_back(first);
        if (i == 9 && (c.OpenPackets() != 1 || c.OpenBytes() != 5 + 10 * 39)) Fail("ten open MOVEs are not one packet of 395 bytes");
    }
    std::vector<char> last;
    if (c.Seal(coopbundle::kLaneUnreliable, &last, &tl) != 0) wire.push_back(last);
    if (wire.size() != 3) Fail("100 MOVE-sized messages did not make three bundles at a 1364-byte budget (34 + 34 + 32)");
    std::vector<std::string> got;
    for (size_t i = 0; i < wire.size(); ++i)
    {
        if (wire[i].size() > 1364) Fail("a bundle passed the budget");
        if (!M13Unpack(wire[i], &got)) Fail("a bundle did not unpack");
    }
    if (got != want) Fail("the messages did not come out complete and in order across bundles");
    if (tl.bundles != 3 || tl.bundled != 100) Fail("the tally does not say three bundles of 100 messages");
}
/* A message too large to share a packet goes alone - after the lane's open bundle, so the guaranteed order holds. */
void t_m13_large_goes_alone_after_open()
{
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first; size_t fe = 0;
    std::vector<std::vector<char> > wire;
    const std::vector<char> s1 = M13Frame(37, "one"), s2 = M13Frame(38, "two"), big = M13Frame(28, std::string(2000, 'R')), s3 = M13Frame(39, "three");
    c.Offer(coopbundle::kLaneReliable, &s1[0], s1.size(), 1364, &first, &fe, &tl);
    c.Offer(coopbundle::kLaneReliable, &s2[0], s2.size(), 1364, &first, &fe, &tl);
    if (c.Offer(coopbundle::kLaneReliable, &big[0], big.size(), 1364, &first, &fe, &tl) != coopbundle::kAlone) Fail("a 2005-byte frame was not sent alone");
    if (fe != 2 || first.empty()) Fail("the open bundle did not go out before the large frame");
    wire.push_back(first); wire.push_back(big);
    if (c.Offer(coopbundle::kLaneReliable, &s3[0], s3.size(), 1364, &first, &fe, &tl) != coopbundle::kTaken || fe != 0) Fail("a small frame after the large one was not taken into a new bundle");
    std::vector<char> last; c.Seal(coopbundle::kLaneReliable, &last, &tl); wire.push_back(last);
    std::vector<std::string> got;
    for (size_t i = 0; i < wire.size(); ++i) if (!M13Unpack(wire[i], &got)) Fail("a frame on the wire did not unpack");
    if (got.size() != 4 || got[0] != M13Key(37, "one") || got[1] != M13Key(38, "two") || got[2] != M13Key(28, std::string(2000, 'R')) || got[3] != M13Key(39, "three"))
        Fail("the guaranteed lane's order did not hold around a large message");
    if (tl.alone != 1 || tl.bundles != 1 || tl.single != 1) Fail("the tally does not say one bundle, one alone, one single");
    const std::vector<char> edge = M13Frame(12, std::string(1357, 'e'));
    if (c.Offer(coopbundle::kLaneReliable, &edge[0], edge.size(), 1364, &first, &fe, &tl) != coopbundle::kAlone || fe != 0 || !first.empty()) Fail("a frame one byte over, with nothing open, did not go alone on its own");
}
/* The guaranteed lane and the unreliable lane never share a bundle; each keeps its own order. */
void t_m13_lanes_never_mix()
{
    if (coopbundle::LaneOf(0) != coopbundle::kLaneReliable || coopbundle::LaneOf(1) != coopbundle::kLaneUnreliable) Fail("channel 0 is not the guaranteed lane, or channel 1 not the unreliable one");
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first; size_t fe = 0;
    const std::vector<char> r1 = M13Frame(37, "r1"), u1 = M13Frame(53, "u1"), r2 = M13Frame(37, "r2"), u2 = M13Frame(53, "u2"), r3 = M13Frame(28, "r3");
    c.Offer(0, &r1[0], r1.size(), 1364, &first, &fe, &tl); c.Offer(1, &u1[0], u1.size(), 1364, &first, &fe, &tl);
    c.Offer(0, &r2[0], r2.size(), 1364, &first, &fe, &tl); c.Offer(1, &u2[0], u2.size(), 1364, &first, &fe, &tl);
    c.Offer(0, &r3[0], r3.size(), 1364, &first, &fe, &tl);
    if (c.OpenPackets() != 2 || c.OpenEntries() != 5) Fail("two lanes open did not count as two packets");
    std::vector<char> rel, unrel; std::vector<std::string> g0, g1;
    if (c.Seal(coopbundle::kLaneReliable, &rel, &tl) != 3 || !M13Unpack(rel, &g0)) { Fail("the guaranteed lane did not seal three messages"); return; }
    if (c.OpenEntries() != 2) Fail("sealing the guaranteed lane touched the unreliable one");
    if (c.Seal(coopbundle::kLaneUnreliable, &unrel, &tl) != 2 || !M13Unpack(unrel, &g1)) { Fail("the unreliable lane did not seal two messages"); return; }
    if (g0.size() != 3 || g0[0] != M13Key(37, "r1") || g0[1] != M13Key(37, "r2") || g0[2] != M13Key(28, "r3")) Fail("the guaranteed lane mixed or reordered");
    if (g1.size() != 2 || g1[0] != M13Key(53, "u1") || g1[1] != M13Key(53, "u2")) Fail("the unreliable lane mixed or reordered");
}
/* The handshake (and a bundle itself) never rides inside a bundle - another protocol's end must still read it; a frame whose
   declared length disagrees is never re-packed. */
void t_m13_handshake_alone()
{
    if (coopbundle::MayBundle(29) || coopbundle::MayBundle(30) || coopbundle::MayBundle(42) || coopbundle::MayBundle(59)) Fail("the handshake or a bundle may ride inside a bundle");
    if (!coopbundle::MayBundle(53) || !coopbundle::MayBundle(28) || !coopbundle::MayBundle(12)) Fail("an ordinary message may not ride in a bundle");
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first; size_t fe = 0;
    const std::vector<char> a = M13Frame(38, "before"), hello = M13Frame(29, "hello");
    c.Offer(0, &a[0], a.size(), 1364, &first, &fe, &tl);
    if (c.Offer(0, &hello[0], hello.size(), 1364, &first, &fe, &tl) != coopbundle::kAlone || fe != 1 || first != a) Fail("a HELLO did not go alone after what was open before it");
    std::vector<char> bad = M13Frame(37, "xyz"); bad[1] = 9;
    if (c.Offer(0, &bad[0], bad.size(), 1364, &first, &fe, &tl) != coopbundle::kAlone) Fail("a frame whose declared length disagrees was taken into a bundle");
}
/* A malformed bundle is refused whole - none of its messages is read. */
void t_m13_decode_refuses_whole()
{
    std::string ok; ok += (char)37; ok += (char)2; ok += (char)0; ok += "hi"; ok += (char)53; ok += (char)0; ok += (char)0;
    std::vector<coopbundle::Entry> es;
    if (!coopbundle::Decode(ok.data(), ok.size(), &es) || es.size() != 2 || es[0].type != 37 || es[0].len != 2 || ok.substr(es[0].at, 2) != "hi" || es[1].len != 0) Fail("a well-formed bundle (one empty message last) did not decode");
    if (coopbundle::Decode(ok.data(), ok.size() - 1, &es) || !es.empty()) Fail("a bundle cut short inside an entry header was not refused whole");
    std::string past = ok; past[1] = (char)9;
    if (coopbundle::Decode(past.data(), past.size(), &es) || !es.empty()) Fail("an entry running past the end was not refused whole");
    std::string nest = ok; nest[3 + 2] = (char)59;
    if (coopbundle::Decode(nest.data(), nest.size(), &es) || !es.empty()) Fail("a bundle inside a bundle was not refused whole");
    if (coopbundle::Decode(ok.data(), 0, &es) || coopbundle::Decode(0, 5, &es)) Fail("an empty bundle was not refused");
}
/* What a bundle saves: the frame bytes plus ENet's command per packet not sent (and, guaranteed, its acknowledgement). */
void t_m13_saved_bytes()
{
    for (int lane = 0; lane < 2; ++lane)
    {
        coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first, out; size_t fe = 0;
        const std::vector<char> mv = M13Frame(12, std::string(36, 'm'));
        for (int i = 0; i < 10; ++i) c.Offer(lane, &mv[0], mv.size(), 1364, &first, &fe, &tl);
        c.Seal(lane, &out, &tl);
        if (out.size() != 395) Fail("ten MOVE-sized messages did not make a 395-byte bundle");
        const long long want = lane == 0 ? 410 - 395 + 9 * 14 : 410 - 395 + 9 * 8;
        if (tl.saved != want) Fail(lane == 0 ? "the guaranteed lane's saving is not 141 bytes" : "the unreliable lane's saving is not 87 bytes");
    }
    if (coopbundle::SavedBytes(1, 41, 41, 0) != 0) Fail("a single message counted a saving");
}
/* A link that goes away takes its open bundles with it, counted. */
void t_m13_clear_counts_open()
{
    coopbundle::Collector c; coopbundle::Tally tl; std::vector<char> first, out; size_t fe = 0;
    const std::vector<char> f = M13Frame(37, "x");
    for (int i = 0; i < 3; ++i) c.Offer(0, &f[0], f.size(), 1364, &first, &fe, &tl);
    for (int i = 0; i < 2; ++i) c.Offer(1, &f[0], f.size(), 1364, &first, &fe, &tl);
    if (c.Clear() != 5) Fail("clearing did not count the five open messages");
    if (c.OpenPackets() != 0 || c.OpenBytes() != 0 || c.OpenEntries() != 0 || c.Seal(0, &out, &tl) != 0 || c.Seal(1, &out, &tl) != 0) Fail("a cleared collector still held something");
}
void t_m14_deferred_events_in_order()
{
    coopwq::DeferQueue<int> q;
    int sA = 0, sB = 0, sC = 0;
    q.Push(1, &sA, 10); q.Push(2, &sB, 0); q.Push(3, &sA, 5); q.Push(4, &sA, 0);
    if (q.Count() != 4 || q.Bytes() != 15 || q.Total() != 4) Fail("deferred count/bytes/total are not 4/15/4");
    if (!q.HasFor(&sA) || !q.HasFor(&sB) || q.HasFor(&sC)) Fail("HasFor did not name exactly the slots with events waiting");
    int e = 0;
    if (!q.Pop(&e) || e != 1) Fail("the first deferred event did not come out first");
    q.Push(5, &sB, 7);
    const int expect[] = { 2, 3, 4, 5 };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i)
    {
        if (!q.Pop(&e) || e != expect[i]) Fail("the deferred events did not come out in arrival order");
        if (i == 2 && q.HasFor(&sA)) Fail("a slot with no events left was still named");
    }
    if (q.Pop(&e) || q.Count() != 0 || q.Bytes() != 0 || q.HasFor(&sB)) Fail("the deferred queue did not empty (count, bytes or slots)");
    if (q.TakeMax() != 4) Fail("deferredMax did not keep the peak (4) until the report line");
    if (q.TakeMax() != 0) Fail("deferredMax did not restart from the current count after a report line");
    q.Push(6, &sA, 1); q.Push(7, &sC, 2); q.Push(8, &sA, 3); q.Push(9, &sB, 4);
    std::deque<int> gone;
    if (q.Discard(&sA, &gone) != 2 || gone.size() != 2 || gone[0] != 6 || gone[1] != 8) Fail("Discard did not remove exactly one slot's events, in order");
    if (q.HasFor(&sA) || q.Count() != 2 || q.Bytes() != 6 || q.Total() != 9) Fail("after Discard: slot, count, bytes or total wrong");
    if (!q.Pop(&e) || e != 7 || !q.Pop(&e) || e != 9) Fail("Discard changed the order of the other slots' events");
    if (q.Discard(&sB, 0) != 0) Fail("Discard of a slot with nothing waiting removed something");
}
/* A record's file name keeps ids apart that differ only in letters outside English: each byte from 0x80 up is '~' and two
   upper-case hex digits. Two Chinese faction names of four letters each (Shek Kingdom, Dust Bandits) that the earlier rule
   joined into one file get two names; an English id keeps exactly the name it had; the gate key stays one per file. */
void t_filename_non_ascii_distinct()
{
    const std::string shek = "\xE6\xB2\x99\xE5\x85\x8B\xE7\x8E\x8B\xE5\x9B\xBD";     /* Shek Kingdom, Chinese, UTF-8 */
    const std::string dust = "\xE6\xB2\x99\xE6\xBC\xA0\xE5\x9C\x9F\xE5\x8C\xAA";     /* Dust Bandits */
    const std::string grazer = "\xE9\xA3\x9F\xE8\x8D\x89\xE5\x8A\xA8\xE7\x89\xA9";   /* a four-letter animal faction name */
    const std::string a = shek + "_14", b = dust + "_14";
    if (coopwq::FileNameBefore(a) != "_____________14" || coopwq::FileNameBefore(b) != "_____________14")
        Fail("the earlier rule does not join the two Chinese ids into one name (the case this test is about)");
    if (coopwq::FileName(a) == coopwq::FileName(b)) Fail("two Chinese squad ids of two factions share one file name");
    if (coopwq::FileName(a) != "~E6~B2~99~E5~85~8B~E7~8E~8B~E5~9B~BD_14") Fail("FileName did not write each byte from 0x80 up as '~' and two upper-case hex digits");
    if (coopwq::FileName(grazer) == coopwq::FileName(shek) || coopwq::FileName(grazer) == coopwq::FileName(dust))
        Fail("two four-letter Chinese faction names share one file name");
    if (coopwq::FileName(grazer) != "~E9~A3~9F~E8~8D~89~E5~8A~A8~E7~89~A9") Fail("the animal faction's file name is not its bytes in hex");
    if (coopwq::GateKey(a) == coopwq::GateKey(b) || coopwq::GateKey(grazer) == coopwq::GateKey(shek)) Fail("two files share a gate key");
    {
        std::string own = coopwq::FileName(a);   /* one gate key per file: the key is the file name, lower-cased */
        for (size_t i = 0; i < own.size(); ++i) if (own[i] >= 'A' && own[i] <= 'Z') own[i] = (char)(own[i] - 'A' + 'a');
        if (coopwq::GateKey(a) != own) Fail("an id's gate key is not its own file name lower-cased");
    }
    if (coopwq::FileNameBefore(a).find('~') != std::string::npos) Fail("the earlier rule made a '~'");
    const char* const ascii[] = { "Holy Nation_3", "Dust Bandits_14", "Nameless_0", "zone.12.-3", "a~b" };
    const char* const named[] = { "Holy_Nation_3", "Dust_Bandits_14", "Nameless_0", "zone.12.-3", "a_b" };
    for (size_t i = 0; i < sizeof(ascii) / sizeof(ascii[0]); ++i)
    {
        if (coopwq::FileName(ascii[i]) != named[i]) Fail(std::string("an English id's file name changed: ") + ascii[i]);
        if (coopwq::FileNameBefore(ascii[i]) != named[i]) Fail(std::string("the earlier rule is not the old one for ") + ascii[i]);
        if (coopwq::EarlierNameOnly(named[i], ascii[i])) Fail(std::string("an English id's file was taken as under an earlier name: ") + ascii[i]);
    }
    if (coopwq::GateKey("Holy Nation_3") != coopwq::GateKey("holy_nation_3")) Fail("two ids that name ONE file got two gate keys");
    if (!coopwq::EarlierNameOnly("_____________14", a) || !coopwq::EarlierNameOnly("_____________14", b)) Fail("the shared earlier name is not seen as each id's earlier name");
    if (coopwq::EarlierNameOnly(coopwq::FileName(a), a)) Fail("a file under the new name was taken as under its earlier name");
    if (!coopwq::EarlierNameMaybe("_____________14") || !coopwq::EarlierNameMaybe("caf_") || coopwq::EarlierNameMaybe("HolyNation")
        || coopwq::EarlierNameMaybe(coopwq::FileName(a)))
        Fail("the sieve for earlier names chose the wrong names");
}
/* A file left under its earlier name is moved to an id's new name ONLY when its own index line names that id. */
void t_filename_earlier_carry_decide()
{
    const std::string shek = "\xE6\xB2\x99\xE5\x85\x8B\xE7\x8E\x8B\xE5\x9B\xBD", dust = "\xE6\xB2\x99\xE6\xBC\xA0\xE5\x9C\x9F\xE5\x8C\xAA";
    const std::string a = shek + "_14", b = dust + "_14", stem = coopwq::FileNameBefore(a);
    if (coopwq::EarlierNameDecide(a, stem, true, a, false) != coopwq::kEarlierCarry) Fail("an earlier-named file whose line names this id was not carried");
    if (coopwq::EarlierNameDecide(a, stem, true, b, false) != coopwq::kEarlierOtherId) Fail("an earlier-named file whose line names another id was not refused as shared");
    if (coopwq::EarlierNameDecide(a, stem, false, std::string(), false) != coopwq::kEarlierUnread) Fail("an earlier-named file whose line could not be read was not left");
    if (coopwq::EarlierNameDecide(a, stem, true, a, true) != coopwq::kEarlierNewThere) Fail("an earlier-named file would be moved over a file already under the new name");
    if (coopwq::EarlierNameDecide("Holy Nation_3", "Holy_Nation_3", true, "Holy Nation_3", false) != coopwq::kEarlierNothing) Fail("an English id's file was moved");
    if (coopwq::EarlierNameDecide(a, coopwq::FileName(a), true, a, false) != coopwq::kEarlierNothing) Fail("a file already under the new name was moved");
    if (coopwq::EarlierNameDecide(b, stem, true, b, false) != coopwq::kEarlierCarry) Fail("the shared name's file was not carried for the id its line names");
}
/* A folder held in memory for coopwq::EarlierCarryAll, answering as the world folder does: names compared with case ignored, a
   locked file unreadable and immovable, a move refused onto a taken name. A .meta file holds an id (empty = no index line). */
struct EarlierSimFile { bool locked; bool isLine; std::string id; int version; long long len; unsigned int crc; };
struct EarlierSim : public coopwq::EarlierFolder
{
    std::map<std::string, EarlierSimFile> files;
    int said, movesMade;
    std::string refuseFrom;   /* a move from this name is refused: a failure at run time, after the plan */
    EarlierSim() : said(0), movesMade(0) {}
    static std::string K(const std::string& n)
    {
        std::string k = n;
        for (size_t i = 0; i < k.size(); ++i) if (k[i] >= 'A' && k[i] <= 'Z') k[i] = (char)(k[i] - 'A' + 'a');
        return k;
    }
    void Data(const std::string& n, long long len, unsigned int crc) { EarlierSimFile f = { false, false, std::string(), 0, len, crc }; files[K(n)] = f; }
    void Meta(const std::string& n, const std::string& id, long long len, unsigned int crc) { EarlierSimFile f = { false, true, id, 7, len, crc }; files[K(n)] = f; }
    void Lock(const std::string& n) { files[K(n)].locked = true; }
    bool There(const std::string& n) const { return files.find(K(n)) != files.end(); }
    std::string IdAt(const std::string& n) const
    { std::map<std::string, EarlierSimFile>::const_iterator it = files.find(K(n)); return it == files.end() ? std::string("<none>") : it->second.id; }
    long long LenAt(const std::string& n) const
    { std::map<std::string, EarlierSimFile>::const_iterator it = files.find(K(n)); return it == files.end() ? -1 : it->second.len; }
    std::vector<std::string> Names() const
    {
        std::vector<std::string> v;
        for (std::map<std::string, EarlierSimFile>::const_iterator it = files.begin(); it != files.end(); ++it) v.push_back(it->first);
        return v;
    }
    bool Has(const std::string& n) { return There(n); }
    bool Matches(const std::string& n, long long len, unsigned int crc)
    {
        std::map<std::string, EarlierSimFile>::const_iterator it = files.find(K(n));
        return it != files.end() && !it->second.locked && !it->second.isLine && it->second.len == len && it->second.crc == crc;
    }
    coopwq::EarlierLineFacts Line(const std::string& n)
    {
        coopwq::EarlierLineFacts l;
        std::map<std::string, EarlierSimFile>::const_iterator it = files.find(K(n));
        if (it == files.end()) return l;
        if (it->second.locked) { l.state = coopwq::kEarlierFileUnreadable; return l; }
        l.state = coopwq::kEarlierFileReadable;
        if (it->second.isLine && !it->second.id.empty())
        { l.parsed = true; l.id = it->second.id; l.version = it->second.version; l.len = it->second.len; l.crc = it->second.crc; }
        return l;
    }
    bool Move(const std::string& from, const std::string& to, std::string* why)
    {
        std::map<std::string, EarlierSimFile>::iterator it = files.find(K(from));
        if (it == files.end() || it->second.locked || There(to) || K(from) == refuseFrom) { *why = "refused"; return false; }
        const EarlierSimFile f = it->second;
        files.erase(it);
        files[K(to)] = f;
        ++movesMade;
        return true;
    }
    void Said(const std::string&, const coopwq::EarlierStep&, bool, const std::string&) { ++said; }
};
/* Record files left under an earlier name, over real folder shapes: each index file goes to the names of the id its own line
   names, in a shape the index reader recovers; a second start changes nothing; English files are never touched. */
void t_filename_earlier_carry_folder()
{
    const std::string shek = "\xE6\xB2\x99\xE5\x85\x8B\xE7\x8E\x8B\xE5\x9B\xBD", dust = "\xE6\xB2\x99\xE6\xBC\xA0\xE5\x9C\x9F\xE5\x8C\xAA";
    const std::string A = shek + "_14", B = dust + "_14", S = coopwq::FileNameBefore(A), FA = coopwq::FileName(A), FB = coopwq::FileName(B);
    if (coopwq::FileNameBefore(B) != S) { Fail("the fixture's two ids do not share an earlier name"); return; }
    {   /* (a) the current pair names A; the rotation left B's whole version in the .prev pair: each to its own record */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", B, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 2 || t.left != 0) Fail("(a) two pairs are carried and none left");
        if (f.IdAt(FA + ".meta") != A || f.LenAt(FA + ".platoon") != 10) Fail("(a) A's current pair is not under A's name");
        if (f.IdAt(FB + ".meta") != B || f.LenAt(FB + ".platoon") != 20) Fail("(a) B's whole version is not B's current pair");
        if (f.files.size() != 4 || f.said != 2) Fail("(a) a file stayed under the earlier name, or an outcome was not said once");
        /* (e) a second start finds nothing to do */
        const coopwq::EarlierTotals t2 = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t2.carried != 0 || t2.left != 0 || f.movesMade != 4 || f.said != 2) Fail("(e) a second run moved or said something");
    }
    {   /* (a) B already has a current pair under its new name: B's old version goes behind it, nothing written over */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", B, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        f.Meta(FB + ".meta", B, 30, 3); f.Data(FB + ".platoon", 30, 3);
        coopwq::EarlierCarryAll(f.Names(), &f);
        if (f.LenAt(FB + ".platoon") != 30 || f.IdAt(FB + ".meta.prev") != B || f.LenAt(FB + ".platoon.prev") != 20) Fail("(a) B's old version is not behind its new one");
    }
    {   /* a stop after B's data moved and before its line: the next start puts the line beside that data, on either side */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", B, 20, 2); f.Data(FB + ".platoon.prev", 20, 2);
        coopwq::EarlierCarryAll(f.Names(), &f);
        if (f.IdAt(FB + ".meta.prev") != B || f.There(FB + ".meta")) Fail("a resumed move put the line away from its data");
        if (f.IdAt(FA + ".meta") != A || f.files.size() != 4) Fail("a resumed move did not finish the stem");
    }
    {   /* (b) an empty current .meta beside a good .prev pair: the .prev pair goes by its own id; data its line does not name stays */
        EarlierSim f;
        f.Meta(S + ".meta", std::string(), 0, 0); f.Data(S + ".platoon", 99, 9); f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || t.left != 0) Fail("(b) the .prev pair beside an empty .meta was not carried");
        if (f.IdAt(FA + ".meta.prev") != A || f.LenAt(FA + ".platoon.prev") != 20) Fail("(b) the .prev pair is not under A's .prev names");
        if (!f.There(S + ".meta") || f.LenAt(S + ".platoon") != 99 || f.There(FA + ".platoon") || f.There(FA + ".meta")) Fail("(b) a file its line does not name was moved");
    }
    {   /* (c) the old build's stop between its two renames: no .meta, the .prev line describes the data under the current name */
        EarlierSim f;
        f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon", 20, 2); f.Data(S + ".platoon.prev", 5, 5);
        coopwq::EarlierCarryAll(f.Names(), &f);
        if (f.IdAt(FA + ".meta.prev") != A || f.LenAt(FA + ".platoon") != 20 || f.LenAt(FA + ".platoon.prev") != 5 || f.files.size() != 3)
            Fail("(c) the crash shape did not keep its shape under A's names");
        coopstore::FolderFacts ff;   /* that shape, as the index reader sees it */
        ff.metaState = coopstore::kFileAbsent; ff.haveMeta = 0;
        ff.prevMetaState = coopstore::kFileReadable; ff.havePrevMeta = 1;
        ff.prevMeta.version = 7; ff.prevMeta.worldId = A; ff.prevMeta.len = 20; ff.prevMeta.crc = 2;
        ff.payloadState = coopstore::kFileReadable; ff.payloadLen = 20; ff.payloadCrc = 2;
        ff.prevPayloadState = coopstore::kFileReadable; ff.prevPayloadLen = 5; ff.prevPayloadCrc = 5;
        if (coopstore::ChooseVersion(ff).kind != coopstore::kChoosePrevMetaCurrent) Fail("(c) the carried shape is not one the index reader recovers");
    }
    {   /* (d) an unreadable .meta: nothing of the stem moves; an unreadable .meta.prev beside a good pair: only the pair moves */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Lock(S + ".meta"); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 1 || f.movesMade != 0 || f.files.size() != 4) Fail("(d) something moved past an unreadable .meta");
        EarlierSim g;
        g.Meta(S + ".meta", A, 10, 1); g.Data(S + ".platoon", 10, 1); g.Meta(S + ".meta.prev", A, 20, 2); g.Lock(S + ".meta.prev"); g.Data(S + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals u = coopwq::EarlierCarryAll(g.Names(), &g);
        if (u.carried != 1 || u.left != 1 || g.IdAt(FA + ".meta") != A || !g.There(S + ".meta.prev") || !g.There(S + ".platoon.prev"))
            Fail("(d) an unreadable .meta.prev was moved, or the good pair beside it was not");
    }
    {   /* (f) English files, "__" in their names included, are never touched */
        EarlierSim f;
        f.Meta("Holy__Nation_3.meta", "Holy  Nation_3", 10, 1); f.Data("Holy__Nation_3.platoon", 10, 1);
        f.Meta("Holy__Nation_3.meta.prev", "Holy  Nation_3", 8, 3); f.Data("Holy__Nation_3.platoon.prev", 8, 3);
        f.Meta("x__y.meta", "x  y", 1, 1); f.Meta("x__y.meta.prev", std::string(), 0, 0); f.Meta("Nest_12.meta", "Nest_12", 1, 1);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 0 || f.movesMade != 0 || f.said != 0) Fail("(f) an English record's file was moved or reported");
    }
    {   /* a current line naming an English id whose name the earlier rule shared with a Chinese id: the .prev pair goes by its own id */
        const std::string C = std::string("a") + shek, X = std::string("a") + std::string(12, ' '), T = coopwq::FileNameBefore(C);
        if (coopwq::FileName(X) != T) { Fail("the fixture's English id does not have the Chinese id's earlier name"); return; }
        EarlierSim f;
        f.Meta(T + ".meta", X, 10, 1); f.Data(T + ".platoon", 10, 1); f.Meta(T + ".meta.prev", C, 20, 2); f.Data(T + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || f.IdAt(T + ".meta") != X || f.IdAt(coopwq::FileName(C) + ".meta") != C) Fail("the Chinese id's .prev pair beside an English current line was not carried");
    }
    {   /* (g) a long Chinese id's files go to its bounded name */
        std::string L; for (int i = 0; i < 30; ++i) L += "\xE6\xB2\x99";
        L += "_1";
        EarlierSim f;
        f.Meta(coopwq::FileNameBefore(L) + ".meta", L, 3, 3); f.Data(coopwq::FileNameBefore(L) + ".platoon", 3, 3);
        coopwq::EarlierCarryAll(f.Names(), &f);
        if (f.IdAt(coopwq::FileName(L) + ".meta") != L || f.LenAt(coopwq::FileName(L) + ".platoon") != 3) Fail("(g) a long id's files are not under its bounded name");
    }
}
/* The carry-over moves a pair whole or not at all, never puts a line beside data it does not describe, looks at a stem by the
   same test the index walks pass a file over by, and says nothing of an English stem. */
void t_filename_earlier_carry_guards()
{
    const std::string shek = "\xE6\xB2\x99\xE5\x85\x8B\xE7\x8E\x8B\xE5\x9B\xBD", dust = "\xE6\xB2\x99\xE6\xBC\xA0\xE5\x9C\x9F\xE5\x8C\xAA";
    const std::string A = shek + "_14", B = dust + "_14", S = coopwq::FileNameBefore(A), FA = coopwq::FileName(A), FB = coopwq::FileName(B);
    {   /* (h) no data under the earlier name, and a .platoon under A's new name with no .meta: the line is not put beside it */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(FA + ".platoon", 99, 9);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 1 || f.movesMade != 0 || f.IdAt(S + ".meta") != A || f.There(FA + ".meta") || f.LenAt(FA + ".platoon") != 99)
            Fail("(h) a line was put beside data under the new name that it does not describe");
    }
    {   /* (h) that data IS the line's own (an earlier start moved it and stopped): the line follows it */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(FA + ".platoon", 10, 1);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || f.IdAt(FA + ".meta") != A || f.There(S + ".meta")) Fail("(h) a stopped current move was not finished");
    }
    {   /* (i) the crash shape with A's .platoon.prev name taken: nothing of it moves, at this start or the next */
        EarlierSim f;
        f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon", 20, 2); f.Data(S + ".platoon.prev", 5, 5); f.Data(FA + ".platoon.prev", 7, 7);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 1 || f.movesMade != 0 || f.files.size() != 4 || f.There(FA + ".platoon") || f.LenAt(FA + ".platoon.prev") != 7)
            Fail("(i) part of a pair moved while another of its names was taken");
        const coopwq::EarlierTotals u = coopwq::EarlierCarryAll(f.Names(), &f);
        if (u.carried != 0 || u.left != 1 || f.movesMade != 0 || f.files.size() != 4) Fail("(i) a second start split the pair");
    }
    {   /* (i) the line's move refused at run time after its data moved: the data is put back beside its line */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.refuseFrom = EarlierSim::K(S + ".meta");
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 1 || f.LenAt(S + ".platoon") != 10 || f.There(FA + ".platoon") || f.IdAt(S + ".meta") != A || f.movesMade != 2)
            Fail("(i) a refused line move left its data under the new name");
    }
    {   /* (k) an id with ONE byte from 0x80 up (not UTF-8): 'caf' + 0xE9 had the earlier name 'caf_' - carried, not only passed over */
        const std::string C = "caf\xE9";
        if (coopwq::FileNameBefore(C) != "caf_" || !coopwq::EarlierNameOnly("caf_", C)) { Fail("(k) the fixture's id does not have the earlier name 'caf_'"); return; }
        EarlierSim f;
        f.Meta("caf_.meta", C, 4, 4); f.Data("caf_.platoon", 4, 4);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || f.IdAt(coopwq::FileName(C) + ".meta") != C || f.LenAt(coopwq::FileName(C) + ".platoon") != 4 || f.There("caf_.meta"))
            Fail("(k) a file the index walks pass over was not carried");
    }
    {   /* (j) English stems holding "__" whose index files are empty, garbled or unreadable: not looked at, not said, not counted */
        EarlierSim f;
        f.Meta("Holy__Nation_3.meta", std::string(), 0, 0); f.Meta("Holy__Nation_3.meta.prev", "Holy  Nation_3", 8, 3); f.Lock("Holy__Nation_3.meta.prev");
        f.Meta("x__y.meta", "x  y", 1, 1); f.Lock("x__y.meta"); f.Meta("x__y.meta.prev", std::string(), 0, 0);
        f.Meta("z__w.meta.prev", std::string(), 0, 0);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 0 || f.said != 0 || f.movesMade != 0) Fail("(j) an English stem with a bad index file was said or counted");
    }
    {   /* side 0: the .prev pair names B, and B's current and .prev names are both taken: it is left, nothing written over */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", B, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        f.Meta(FB + ".meta", B, 30, 3); f.Data(FB + ".platoon", 30, 3); f.Meta(FB + ".meta.prev", B, 40, 4); f.Data(FB + ".platoon.prev", 40, 4);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || t.left != 1 || f.IdAt(FA + ".meta") != A || f.IdAt(S + ".meta.prev") != B || f.LenAt(S + ".platoon.prev") != 20
            || f.LenAt(FB + ".platoon") != 30 || f.LenAt(FB + ".platoon.prev") != 40)
            Fail("(side 0) another record's version with both of its places taken was moved, or something was written over");
    }
    {   /* same id, A's .prev names taken: the current pair moves, the .prev pair is left */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        f.Meta(FA + ".meta.prev", A, 50, 5);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 1 || t.left != 1 || f.IdAt(FA + ".meta") != A || f.IdAt(S + ".meta.prev") != A || f.LenAt(S + ".platoon.prev") != 20
            || f.LenAt(FA + ".meta.prev") != 50)
            Fail("(same id) a .prev pair was moved over A's .prev names");
    }
    {   /* same id, A's current names taken: neither pair moves; the .prev pair stays with its current one */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        f.Meta(FA + ".meta", A, 60, 6); f.Data(FA + ".platoon", 60, 6);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 2 || f.movesMade != 0 || f.files.size() != 6 || f.LenAt(FA + ".platoon") != 60)
            Fail("(same id) a pair moved while A's current names were taken");
    }
    {   /* a current move refused at run time (its data locked) demotes its .prev pair: neither moves */
        EarlierSim f;
        f.Meta(S + ".meta", A, 10, 1); f.Data(S + ".platoon", 10, 1); f.Lock(S + ".platoon"); f.Meta(S + ".meta.prev", A, 20, 2); f.Data(S + ".platoon.prev", 20, 2);
        const coopwq::EarlierTotals t = coopwq::EarlierCarryAll(f.Names(), &f);
        if (t.carried != 0 || t.left != 2 || f.movesMade != 0 || f.files.size() != 4 || f.IdAt(S + ".meta.prev") != A)
            Fail("(demote) a .prev pair moved ahead of a current pair that did not");
    }
}
/* A long non-English id gets a bounded name: a whole-piece front part, "~H" and the FNV-1a hash of the whole id. Two long ids with
   one front part get two names; a short one and every English id keep their whole names. */
void t_filename_long_ids_bounded()
{
    std::string front; for (int i = 0; i < 30; ++i) front += "\xE6\xB2\x99";
    const std::string l1 = front + "_1", l2 = front + "_2";
    const std::string n1 = coopwq::FileName(l1), n2 = coopwq::FileName(l2);
    if (n1 == n2 || coopwq::GateKey(l1) == coopwq::GateKey(l2)) Fail("two long ids with one front part share a file name");
    if (n1.size() > coopwq::kFileNameMax || n2.size() > coopwq::kFileNameMax) Fail("a long id's name is past the bound");
    if (!coopwq::FileNameIsBounded(n1) || !coopwq::FileNameIsBounded(n2)) Fail("a long id's name is not the bounded form");
    const size_t at = n1.find("~H");
    std::string whole; for (int i = 0; i < 30; ++i) whole += "~E6~B2~99";   /* the whole name of `front`, uncut */
    if (at == std::string::npos || n1.size() != at + coopwq::kFileNameHashTail || at % 3 != 0 || n1.compare(0, at, whole, 0, at) != 0)
        Fail("the front part is not the whole name cut at a whole '~XX'");
    {
        static const char kHex[] = "0123456789ABCDEF";
        const unsigned int h = coopwq::FileNameHash(l1);
        std::string tail = "~H"; for (int k = 7; k >= 0; --k) tail += kHex[(h >> (4 * k)) & 0x0F];
        if (at == std::string::npos || n1.substr(at) != tail) Fail("the tail is not the id's hash in 8 upper-case hex digits");
    }
    if (coopwq::FileNameHash("a") != 0xE40C292Cu || coopwq::FileNameHash(std::string()) != 2166136261u) Fail("FileNameHash is not FNV-1a 32");
    std::string w32, w33; for (int i = 0; i < 32; ++i) w32 += '\xE6';
    w33 = w32 + '\xE6';
    if (coopwq::FileNameIsBounded(coopwq::FileName(w32)) || coopwq::FileName(w32).size() != coopwq::kFileNameMax) Fail("a name exactly at the bound was cut");
    if (!coopwq::FileNameIsBounded(coopwq::FileName(w33)) || coopwq::FileName(w33).size() > coopwq::kFileNameMax) Fail("a name one piece past the bound was not cut");
    if (coopwq::FileNameIsBounded(coopwq::FileName("\xE6\xB2\x99_14"))) Fail("a short Chinese id was cut");
    const std::string ascii(200, 'a');
    if (coopwq::FileName(ascii) != ascii || coopwq::FileName(ascii + " b") != ascii + "_b") Fail("an English id was cut");
    if (!coopwq::EarlierNameOnly(coopwq::FileNameBefore(l1), l1)) Fail("a long id's earlier name is not found");
}
/* M14 fold (review finding 8): the gate key is the record's FILE name, lower-cased - every id that lands on one
   file (Windows ignores case; the file name turns other characters into '_') gets one key. */
void t_m14_gate_key_is_the_file_name()
{
    if (coopwq::FileName("Ab C.1-x_y") != "Ab_C.1-x_y") Fail("FileName is not the store's file-name rule");
    if (coopwq::GateKey("Ab C") != coopwq::GateKey("ab_c")) Fail("two ids that name ONE file got two gate keys");
    if (coopwq::GateKey("ABC") != "abc") Fail("the gate key is not lower-cased");
    if (coopwq::GateKey("x\xC3\xA9") != "x~c3~a9") Fail("a non-ASCII byte is not '~' and its hex digits (lower-cased) as in the file name");
    if (coopwq::GateKey("a1") == coopwq::GateKey("a2")) Fail("two different files share a gate key");
    if (coopwq::GateKey("Nest_12.b") != "nest_12.b") Fail("'-', '_' and '.' are not kept");
}
/* The loop and the writer together, one step at a time: messages for three files arrive interleaved, the
   writer lands at most one job per step, and one write fails. Each file's writes land in the order their
   messages arrived, each file ends holding its LAST landed write, and every job after the failure lands. */
void t_m14_loop_and_writer_per_file_order()
{
    const char* keys[] = { "a", "b", "a", "a", "b", "c", "a", "b" };
    const int vals[]   = {  1,   1,   2,   3,   2,   1,   4,   3  };
    const size_t n = sizeof(vals) / sizeof(vals[0]);
    coopwq::KeyGate<int> gate;
    coopwq::Fifo<std::pair<std::string, int> > q;
    std::deque<std::string> done;
    std::map<std::string, std::vector<int> > landed;
    std::map<std::string, int> disk;
    size_t next = 0; int steps = 0;
    while (next < n || !q.Idle() || !done.empty())
    {
        if (++steps > 1000) { Fail("the loop and the writer did not finish"); return; }
        if (next < n)   /* one message arrives */
        {
            const std::string k = keys[next]; const int v = vals[next]; ++next;
            if (gate.Busy(k)) gate.Hold(k, v);
            else { if (!gate.Start(k)) Fail("a job started for a busy key"); q.Push(std::make_pair(k, v)); }
        }
        std::pair<std::string, int> j;
        if (q.Pop(&j))   /* the writer lands one job */
        {
            const bool ok = !(j.first == "a" && j.second == 2);   /* this write fails */
            if (ok) { disk[j.first] = j.second; landed[j.first].push_back(j.second); }
            q.Done(ok);
            done.push_back(j.first);
        }
        while (!done.empty())   /* the loop applies completions and handles what was held */
        {
            const std::string k = done.front(); done.pop_front();
            gate.Finish(k);
            int v = 0;
            while (gate.TakeHeld(k, &v)) { if (!gate.Start(k)) Fail("a held message started a second job"); q.Push(std::make_pair(k, v)); }
        }
    }
    const int ea[] = { 1, 3, 4 }, eb[] = { 1, 2, 3 }, ec[] = { 1 };
    if (landed["a"] != std::vector<int>(ea, ea + 3)) Fail("file a's writes did not land 1,3,4 (2 failed) in arrival order");
    if (landed["b"] != std::vector<int>(eb, eb + 3)) Fail("file b's writes did not land 1,2,3 in arrival order");
    if (landed["c"] != std::vector<int>(ec, ec + 1)) Fail("file c's write did not land");
    if (disk["a"] != 4 || disk["b"] != 3 || disk["c"] != 1) Fail("a file does not hold its last write");
    if (q.Jobs() != 8 || q.Failed() != 1) Fail("the writer did not finish 8 jobs with 1 failed");
    if (gate.HeldCount() != 0 || gate.BusyCount() != 0) Fail("messages were left held or keys left busy");
}
/* T-164 non-shop road (owner 207 A, 208, 209 A, 212) and its fold (review of d8396d09): which moves show at once, what the answer
   does, where a refused one goes; a faulted add keeps the item, a hold the holder changed is not put back, overflow drops at the feet. */
void t_t164_show_road_decisions()
{
    using namespace coopshow;
    if (ShowWanted(0, 0, 0, 0) != 1) Fail("a plain player move shows at once");
    if (ShowWanted(1, 0, 0, 0) != 1) Fail("T-338: an AI take-one shows at once too (owner 108)");
    if (ShowWanted(0, 1, 0, 0) != 0) Fail("the loot route's give keeps today's road");
    if (ShowWanted(0, 0, 1, 0) != 0) Fail("a put-back that re-created the item keeps today's road");
    if (ShowWanted(0, 0, 0, 1) != 0 || ShowWanted(0, 0, 0, 2) != 0) Fail("a priced move keeps today's road");
    if (TakeShowKind(1, 1, 0) != kShowTakeUnits) Fail("units dragged off their stack (merged or not) are held off it");
    if (TakeShowKind(1, 0, 1) != kShowTakeItem) Fail("a split whose stack was gone lifts the clone filed in its cell");
    if (TakeShowKind(0, 0, 1) != kShowTakeItem) Fail("a whole item is lifted");
    if (TakeShowKind(0, 1, 0) != kShowNone || TakeShowKind(0, 0, 0) != kShowNone || TakeShowKind(1, 0, 0) != kShowNone)
        Fail("an unexpected object in the cell keeps today's road");
    if (SettleAction(kShowNone, 0, 0, 0) != kStNothing || SettleAction(kShowNone, 1, 1, 1) != kStNothing) Fail("nothing held, nothing to settle");
    if (SettleAction(kShowGive, 1, 1, 0) != kStDestroy) Fail("a yes to a give destroys its escrow");
    if (SettleAction(kShowGive, 0, 0, 0) != kStBack || SettleAction(kShowGive, 0, 1, 0) != kStBack) Fail("a refused / lapsed give goes back");
    if (SettleAction(kShowGive, 1, 0, 0) != kStBack) Fail("a give our half could not finish goes back");
    if (SettleAction(kShowGive, 0, 0, 1) != kStBack) Fail("a give is ours - a holder mark never stops it going back");
    if (SettleAction(kShowTakeItem, 1, 1, 0) != kStDestroy || SettleAction(kShowTakeItem, 1, 1, 1) != kStDestroy)
        Fail("a yes to a take destroys the lifted copy");
    if (SettleAction(kShowTakeItem, 1, 0, 0) != kStBack) Fail("a take that could not be built here goes back to their window");
    if (SettleAction(kShowTakeItem, 0, 0, 0) != kStBack) Fail("a refused take goes back");
    if (SettleAction(kShowTakeUnits, 1, 1, 0) != kStNothing || SettleAction(kShowTakeUnits, 1, 1, 1) != kStNothing)
        Fail("a yes to held units leaves their stack lowered");
    if (SettleAction(kShowTakeUnits, 0, 0, 0) != kStBack || SettleAction(kShowTakeUnits, 1, 0, 0) != kStBack) Fail("refused held units go back");
    /* H2: a hold the holder changed is not put back - the holder's messages are the truth */
    if (SettleAction(kShowTakeItem, 0, 0, 1) != kStHolderTruth || SettleAction(kShowTakeUnits, 0, 0, 1) != kStHolderTruth
        || SettleAction(kShowTakeItem, 1, 0, 1) != kStHolderTruth)
        Fail("a refused / lapsed take the holder changed is put back (a phantom item or extra units)");
    if (HolderMoveAction(kShowTakeItem, 1, 1, 1, 0) != kHmMark || HolderMoveAction(kShowTakeUnits, 1, 1, 0, 0) != kHmMark)
        Fail("the holder's removal at the held cell does not mark the hold");
    if (HolderMoveAction(kShowTakeUnits, 2, 1, 0, 1) != kHmMark) Fail("an absolute count on the lowered stack does not mark the hold");
    if (HolderMoveAction(kShowTakeItem, 2, 1, 1, 1) != kHmQtyOnCopy) Fail("the holder's count for a lifted item is not written onto the copy");
    if (HolderMoveAction(kShowTakeItem, 2, 1, 1, 0) != kHmMark) Fail("a count naming another record at the empty cell does not mark");
    if (HolderMoveAction(kShowTakeItem, 2, 1, 0, 1) != kHmNone) Fail("a count on what now sits in the cell touched the hold");
    if (HolderMoveAction(kShowTakeItem, 1, 0, 1, 1) != kHmNone || HolderMoveAction(kShowGive, 1, 1, 1, 1) != kHmNone
        || HolderMoveAction(kShowTakeItem, 0, 1, 1, 1) != kHmNone || HolderMoveAction(kShowTakeItem, 3, 1, 1, 1) != kHmNone
        || HolderMoveAction(kShowNone, 1, 1, 1, 1) != kHmNone)
        Fail("another cell, a give, an add, a split or no hold marked the hold");
    if (ReaskBoxAfter(kStBack, 1) != 1 || ReaskBoxAfter(kStHolderTruth, 1) != 1) Fail("a box is not re-asked on every take refusal / lapse");
    if (ReaskBoxAfter(kStBack, 0) != 0 || ReaskBoxAfter(kStDestroy, 1) != 0 || ReaskBoxAfter(kStNothing, 1) != 0)
        Fail("a character, or a yes, re-asked a box");
    /* H1: a faulted add keeps the item - never destroyed, never added again */
    if (PutOutcome(1) != kPutPlaced || PutOutcome(0) != kPutNext) Fail("a placed / refused add is misread");
    if (PutOutcome(-1) != kPutKeep) Fail("a faulted add is not kept (it would be destroyed or filed twice)");
    /* 209 A: the old cell, else any free cell; a full box re-asks; a full character keeps the copy (M3) */
    if (TakeBackAction(1, 0, 1) != kTbOldCell || TakeBackAction(1, 1, 0) != kTbOldCell) Fail("a free old cell takes it back");
    if (TakeBackAction(0, 1, 1) != kTbAnyCell || TakeBackAction(0, 1, 0) != kTbAnyCell) Fail("a filled old cell: any free cell");
    if (TakeBackAction(0, 0, 1) != kTbReaskBox) Fail("a full box: the holder is re-asked");
    if (TakeBackAction(0, 0, 0) != kTbKeptNoRoom) Fail("a full character: the copy is kept, counted (no re-sync road) - never destroyed");
    if (UnitsBackAction(1, 0) != kTbOldCell || UnitsBackAction(1, 1) != kTbOldCell) Fail("units go back on their stack");
    if (UnitsBackAction(0, 1) != kTbReaskBox || UnitsBackAction(0, 0) != kTbKeptNoRoom) Fail("a stack that is gone: re-ask / counted");
    if (GiveBackAction(1, 0) != kGbCell || GiveBackAction(1, 1) != kGbCell) Fail("a give goes back to the cell it left");
    if (GiveBackAction(0, 1) != kGbFreeCell) Fail("a filled cell: the first free cell of that section");
    if (GiveBackAction(0, 0) != kGbPack) Fail("no free cell there: the engine's own add (the pack's inventory, else the character)");
    /* M2: overflow drops at the character's feet; kept only when even the drop fails */
    if (GiveOverflowAction(1, 0) != kGbPack || GiveOverflowAction(1, 1) != kGbPack) Fail("the engine's add took it");
    if (GiveOverflowAction(0, 1) != kGbGround) Fail("no room anywhere: dropped at the character's feet");
    if (GiveOverflowAction(0, 0) != kGbKept) Fail("the drop failed too: kept alive, counted");
    /* M1: a late yes removes at the row's cell only when the row names the escrow's object and cell; otherwise the owner revokes */
    if (LateGiveRoad(1) != kLgRemoveExact || LateGiveRoad(0) != kLgRevoke) Fail("a late give's road is wrong");
}
/* t338-test (T-338, owner 108): an AI take-one from a box the other game writes shows at once - the unit leaves the box at the take;
   a TRANSFER's unit arrives on the yes, a CONSUME's stays used; a refusal, a lapse or a yes for another item puts it back on the stack. */
void t_t338_ai_take_one_shows()
{
    if (coopshow::ShowWanted(1, 0, 0, 0) != 1) Fail("an AI take-one does not show at once");
    if (coopshow::ShowWanted(1, 1, 0, 0) != 0 || coopshow::ShowWanted(1, 0, 1, 0) != 0 || coopshow::ShowWanted(1, 0, 0, 1) != 0)
        Fail("an AI take-one let a loot give, a re-created put-back or a priced move show at once");
    if (coopshow::TakeShowKind(1, 1, 0) != coopshow::kShowTakeUnits) Fail("a TRANSFER's one unit is not held off its stack");
    /* CONSUME: held when the show is wanted and the stack is the object in its cell; otherwise today's undo at the send */
    if (cooptake1::ConsumeHolds(1, 1) != 1) Fail("a CONSUME on a readable stack is not held at once");
    if (cooptake1::ConsumeHolds(0, 1) != 0 || cooptake1::ConsumeHolds(1, 0) != 0) Fail("a CONSUME that cannot be held skipped today's undo");
    /* the yes: gone for good - nothing placed, nothing raised (also after the writer's own count arrived) */
    if (coopshow::SettleAction(coopshow::kShowTakeUnits, cooptake1::ConsumeSettleOk(1, 1), 1, 0) != coopshow::kStNothing
        || coopshow::SettleAction(coopshow::kShowTakeUnits, cooptake1::ConsumeSettleOk(1, 1), 1, 1) != coopshow::kStNothing)
        Fail("a confirmed CONSUME raised the stack or placed something");
    /* a refusal / a lapse / a yes for another item (F3): the unit goes back on the stack and the box is re-asked */
    if (coopshow::SettleAction(coopshow::kShowTakeUnits, cooptake1::ConsumeSettleOk(0, 1), 1, 0) != coopshow::kStBack)
        Fail("a refused or lapsed CONSUME did not put the unit back");
    if (coopshow::SettleAction(coopshow::kShowTakeUnits, cooptake1::ConsumeSettleOk(1, 0), 1, 0) != coopshow::kStBack)
        Fail("a yes for another item (F3) did not put the unit back");
    if (coopshow::SettleAction(coopshow::kShowTakeUnits, cooptake1::ConsumeSettleOk(0, 1), 1, 1) != coopshow::kStHolderTruth)
        Fail("a refused CONSUME whose cell the writer moved meanwhile was raised over the writer's count");
    if (coopshow::UnitsBackAction(1, 1) != coopshow::kTbOldCell || coopshow::UnitsBackAction(0, 1) != coopshow::kTbReaskBox)
        Fail("a refused CONSUME's unit is not back on its stack, or its box is not re-asked when the stack is gone");
    if (coopshow::ReaskBoxAfter(coopshow::kStBack, 1) != 1) Fail("a refused CONSUME did not re-ask the box");
}
/* T-969 (owner decision 992 b): the taking game asks the access rule before an engine take-one from a box another game writes -
   only under `owner-only` / `locked` and only for a box another game writes; a refusal means the engine's take does not run. */
void t_t969_take_access_gate()
{
    using namespace cooptake1;
    if (AccessAsk(kT1PolicyShared, 1) != 0 || AccessAsk(kT1PolicyUnknown, 1) != 0) Fail("T-969: `shared` (or unknown) asked the access rule");
    if (AccessAsk(2, 0) != 0 || AccessAsk(3, 0) != 0) Fail("T-969: a box this game writes asked the access rule");
    if (AccessAsk(2, 1) != 1 || AccessAsk(3, 1) != 1) Fail("T-969: owner-only / locked did not ask before a take from another game's box");
    if (AccessTakeRuns(1, 0) != 0) Fail("T-969: a refused take still ran");
    if (AccessTakeRuns(1, 1) != 1 || AccessTakeRuns(0, 0) != 1) Fail("T-969: an allowed (or not asked) take did not run");
}
/* T-969 fold 2 (review): GateDecide - the same decision on every thread (off the main thread its inputs come from the owner table;
   never booked there = known 0). Unreadable owner allowed, teammate allowed, non-teammate refused, this game's own writing allowed. */
void t_t969_gate_decide()
{
    using namespace cooptake1;
    /* shared / unknown policy: the take runs, whatever the box */
    if (GateDecide(kT1PolicyShared, 1, kT1OwnPeer, 0, 0, 1) != kT1GateRun || GateDecide(kT1PolicyUnknown, 1, kT1OwnPeer, 0, 0, 1) != kT1GateRun)
        Fail("T-969 fold 2: shared / unknown policy refused a take");
    /* off the main thread, a box the main thread never decided (known 0): allowed, counted */
    if (GateDecide(kT1PolicyOwner, 0, kT1OwnPeer, 0, 0, 1) != kT1GateRunUnread) Fail("T-969 fold 2: an owner not known here was not allowed");
    /* an owner that cannot be read: allowed, counted - never 'owner-unreadable' refused */
    if (GateDecide(kT1PolicyOwner, 1, kT1OwnUnreadable, 0, 0, 1) != kT1GateRunUnread || GateDecide(kT1PolicyLocked, 1, kT1OwnUnknown, 0, 0, 1) != kT1GateRunUnread)
        Fail("T-969 fold 2: an unreadable owner was refused");
    /* nobody's box (a town's, a shop's), my own, a shared faction's: allowed */
    if (GateDecide(kT1PolicyOwner, 1, kT1OwnNone, 0, 0, 1) != kT1GateRun || GateDecide(kT1PolicyOwner, 1, kT1OwnMe, 0, 0, 1) != kT1GateRun
        || GateDecide(kT1PolicyOwner, 1, kT1OwnShared, 0, 0, 1) != kT1GateRun)
        Fail("T-969 fold 2: a box that is not another player's was refused");
    /* a teammate's box: allowed (member) */
    if (GateDecide(kT1PolicyOwner, 1, kT1OwnPeer, 0, 1, 1) != kT1GateRunMate || GateDecide(kT1PolicyLocked, 1, kT1OwnPeer, 0, 1, 1) != kT1GateRunMate)
        Fail("T-969 fold 2: a teammate's box was refused");
    /* a non-teammate's box another game writes: refused under owner-only; under locked only when the stand-in is a player faction */
    if (GateDecide(kT1PolicyOwner, 1, kT1OwnPeer, 0, 0, 0) != kT1GateRefuse) Fail("T-969 fold 2: owner-only let a non-teammate take");
    if (GateDecide(kT1PolicyLocked, 1, kT1OwnPeer, 0, 0, 1) != kT1GateRefuse) Fail("T-969 fold 2: locked (stand-in a player faction) let a non-teammate take");
    if (GateDecide(kT1PolicyLocked, 1, kT1OwnPeer, 0, 0, 0) != kT1GateRun || GateDecide(kT1PolicyLocked, 1, kT1OwnPeer, 0, 0, -1) != kT1GateRun)
        Fail("T-969 fold 2: locked refused where the engine's own lock decides");
    /* this game writes the box (owner away, its area here): not a box another game writes - allowed */
    if (GateDecide(kT1PolicyOwner, 1, kT1OwnPeer, 1, 0, 1) != kT1GateRun) Fail("T-969 fold 2: a box this game writes was refused");
    /* the repeat minute */
    if (GateNewMinute(0, 0u, 5u) != 1 || GateNewMinute(1, 1000u, 60999u) != 0 || GateNewMinute(1, 1000u, 61000u) != 1)
        Fail("T-969 fold 2: the repeat minute is wrong");
    if (GateNewMinute(1, 0xFFFFFF00u, 100u) != 0) Fail("T-969 fold 2: the minute broke at the tick count's wrap");
}
/* t338-f2-test (T-338 attempt 2, T791 diagnosis H-t338-1): the holder's own split of held units is absorbed, never applied on top -
   in either order against the yes; a refusal / lapse / our half failing after it raises nothing; a split that is not ours is applied
   and re-asks; a CONSUME is sent only when the stack really dropped. */
void t_t338_f2_holder_split_absorbed()
{
    using namespace coopshow;
    /* split, then yes: absorbed while the row is open; the yes leaves the units gone and opens no wait */
    if (SplitAbsorbAction(1, 1, 1, 1, 0, 0) != kAbAbsorb) Fail("the holder's split of the one held unit was not absorbed");
    if (SplitAbsorbAction(1, 1, 3, 3, 0, 0) != kAbAbsorb) Fail("the holder's split of three held units (a player's partial take) was not absorbed");
    if (SettleAction(kShowTakeUnits, 1, 1, 0, 1) != kStNothing) Fail("a yes after an absorbed split raised or placed something");
    if (AwaitSplitAfter(kShowTakeUnits, kStNothing, 1, 0) != 0) Fail("a yes after an absorbed split waited for a second split");
    if (SettleOutcome(1, 1, 0, 0, 1) != kSoAbsorbed) Fail("split-then-yes is not named absorbed");
    /* yes, then split: the yes opens a bounded wait; the wait absorbs the split */
    if (SettleAction(kShowTakeUnits, 1, 1, 0, 0) != kStNothing) Fail("a yes for held units did something to the stack");
    if (AwaitSplitAfter(kShowTakeUnits, kStNothing, 0, 0) != 1) Fail("a yes before the split kept no wait for it");
    if (SplitAbsorbAction(1, 1, 1, 1, 0, 0) != kAbAbsorb) Fail("the wait did not absorb the split");
    if (AwaitLapsed(kAwaitSplitMs - 1) != 0 || AwaitLapsed(kAwaitSplitMs) != 1 || kAwaitSplitMs != 10000) Fail("the wait is not bounded at 10 s");
    if (AwaitEndsOnHolderMove(1, 1) != 1 || AwaitEndsOnHolderMove(2, 1) != 1 || AwaitEndsOnHolderMove(3, 1) != 0 || AwaitEndsOnHolderMove(2, 0) != 0)
        Fail("a wait does not end on the holder's op 1 / op 2 on that cell only");
    if (AwaitSplitAfter(kShowTakeItem, kStNothing, 0, 0) != 0 || AwaitSplitAfter(kShowTakeUnits, kStBack, 0, 0) != 0)
        Fail("a wait opened for a lifted item or a put-back");
    /* split, then lapse / refusal / our half failing: the holder took them - nothing raised, a box re-asked */
    if (SettleAction(kShowTakeUnits, 0, 0, 0, 1) != kStHolderTruth) Fail("a lapse after an absorbed split raised the stack");
    if (SettleAction(kShowTakeUnits, 1, 0, 0, 1) != kStHolderTruth) Fail("our half failing after an absorbed split raised the stack");
    if (ReaskBoxAfter(kStHolderTruth, 1) != 1) Fail("a lapse after an absorbed split did not re-ask the box");
    if (SettleOutcome(0, 0, 1, 0, 1) != kSoLapsed || SettleOutcome(0, 0, 0, 0, 1) != kSoRefused || SettleOutcome(1, 0, 0, 0, 1) != kSoOurHalfFailed)
        Fail("the settle line names the wrong outcome after an absorbed split");
    /* a refusal with no split: the units go back (unchanged) */
    if (SettleAction(kShowTakeUnits, 0, 0, 0, 0) != kStBack) Fail("a refused take with no split did not put the units back");
    if (SettleAction(kShowTakeUnits, 0, 0, 0) != kStBack) Fail("the four-argument settle changed");
    if (SettleAction(kShowTakeItem, 0, 0, 0, 1) != kStBack) Fail("absorbed changed a lifted item's settle");
    /* a mismatched split: applied, the hold re-asks; never absorbed */
    if (SplitAbsorbAction(1, 1, 2, 1, 0, 0) != kAbApplyReask) Fail("a split of other units was absorbed");
    if (SplitAbsorbAction(1, 0, 1, 1, 0, 0) != kAbApplyReask) Fail("a split of another stack was absorbed");
    if (SplitAbsorbAction(0, 1, 1, 1, 0, 0) != kAbNone) Fail("a split on another cell touched the hold");
    if (SplitAbsorbAction(1, 1, 1, 1, 1, 0) != kAbApply) Fail("a second split after the absorbed one was absorbed too");
    if (SplitAbsorbAction(1, 1, 1, 1, 0, 1) != kAbApply) Fail("a split after the holder's absolute count overwrote the hold was absorbed");
    if (AwaitSplitAfter(kShowTakeUnits, kStNothing, 0, 1) != 0) Fail("a yes after the holder's absolute count waited for its split");
    /* the REPEAT hole: a CONSUME only when the live count is below the count before the take */
    if (ConsumeCountDropped(4, 4) != 1 || ConsumeCountDropped(3, 4) != 1) Fail("a dropped stack sent no CONSUME");
    if (ConsumeCountDropped(5, 4) != 0) Fail("a unit returned to the same stack still sent a CONSUME");
    if (ConsumeCountDropped(-1, 4) != 0) Fail("an unreadable count sent a CONSUME");
    if (SettleOutcomeName(kSoYes) == 0 || SettleOutcomeName(99) == 0) Fail("an outcome has no name");
}
/* t338-f3-test (T-338 fold 3): an absorbed split is not kept by the wrong take. Two holds T1, T2 of one unit each on one stack; T1's
   split was absorbed by T2's row; T2 is refused: the absorb moves to T1 (a pending T1 is marked absorbed; a waiting T1 settles
   absorbed) and T2's unit goes back. No other hold: the holder's truth. A yes on an absorbed row: unchanged. */
void t_t338_f3_absorb_moves_to_its_take()
{
    using namespace coopshow;
    /* T2 absorbed, refused, T1 still pending: the absorb moves to T1; T2 settles as never absorbed - its unit goes back */
    if (AbsorbMoveAction(kShowTakeUnits, 1, 0, 0, 1) != kAmToPend) Fail("a refused absorbed take did not hand its absorb to the pending take");
    if (SettleAction(kShowTakeUnits, 0, 0, 0, 0) != kStBack) Fail("the refused take whose absorb moved did not put its unit back");
    if (SettleAction(kShowTakeUnits, 1, 1, 0, 1) != kStNothing || AwaitSplitAfter(kShowTakeUnits, kStNothing, 1, 0) != 0
        || SettleOutcome(1, 1, 0, 0, 1) != kSoAbsorbed)
        Fail("the pending take that received the absorb did not settle absorbed on its yes");
    /* T2 absorbed, refused, T1's yes already waiting: T1 settles absorbed; a wait is preferred over a pending row */
    if (AbsorbMoveAction(kShowTakeUnits, 1, 0, 1, 0) != kAmToWait) Fail("a refused absorbed take did not hand its absorb to the waiting take");
    if (AbsorbMoveAction(kShowTakeUnits, 1, 0, 1, 1) != kAmToWait) Fail("a pending take was preferred over a waiting one");
    /* a lapse / our half failing moves it too */
    if (AbsorbMoveAction(kShowTakeUnits, 1, 0, 0, 1) != kAmToPend) Fail("a lapsed absorbed take kept the absorb");
    /* no other open hold: the holder's truth, nothing put back */
    if (AbsorbMoveAction(kShowTakeUnits, 1, 0, 0, 0) != kAmNoCandidate) Fail("an absorbed take with no other hold did not stay the holder's truth");
    if (SettleAction(kShowTakeUnits, 0, 0, 0, 1) != kStHolderTruth) Fail("an absorbed take with no other hold raised the stack");
    /* a yes on an absorbed row, a row that absorbed nothing, a lifted item: unchanged */
    if (AbsorbMoveAction(kShowTakeUnits, 1, 1, 1, 1) != kAmNone) Fail("a yes on an absorbed take moved its absorb");
    if (AbsorbMoveAction(kShowTakeUnits, 0, 0, 1, 1) != kAmNone) Fail("a take that absorbed nothing moved an absorb");
    if (AbsorbMoveAction(kShowTakeItem, 1, 0, 1, 1) != kAmNone || AbsorbMoveAction(kShowGive, 1, 0, 1, 1) != kAmNone)
        Fail("an absorb moved for something other than held units");
    /* who may take it: the same cell, stack and units; not absorbed itself; not overwritten by the holder's op 1 / op 2 */
    if (AbsorbMoveCandidate(1, 1, 1, 0, 0) != 1) Fail("an open hold of the same units on that cell was not a candidate");
    if (AbsorbMoveCandidate(0, 1, 1, 0, 0) != 0 || AbsorbMoveCandidate(1, 0, 1, 0, 0) != 0 || AbsorbMoveCandidate(1, 1, 0, 0, 0) != 0)
        Fail("a hold of another cell, stack or count took the absorb");
    if (AbsorbMoveCandidate(1, 1, 1, 1, 0) != 0) Fail("a hold that absorbed its own split took a second one");
    if (AbsorbMoveCandidate(1, 1, 1, 0, 1) != 0) Fail("a hold the holder's count overwrote took the absorb");
}
/* T620 fold: the TEST-ONLY refusenext lever answers with the named reason's own shop verdict; no reason keeps verdict 5. */
void t_shop_refusenext_answers_the_named_verdict()
{
    using namespace coopshop;
    if (RefuseNextVerdict("generic") != kVRefuseNext || RefuseNextVerdict(0) != kVRefuseNext) Fail("no reason must stay verdict 5");
    if (RefuseNextVerdict("keeperBroke") != kVKeeperBroke) Fail("keeperBroke does not answer the keeper-broke verdict");
    if (ShowResult(RefuseNextVerdict("keeperBroke")) != kTrKeeperBroke) Fail("keeperBroke does not reach the engine's can't-afford message");
    if (RefuseNextVerdict("place") != kVNoRoom || ShowResult(RefuseNextVerdict("place")) != kTrNoRoom) Fail("place does not answer no room");
    if (RefuseNextVerdict("policy") != kVPolicy || RefuseNextVerdict("notOwned") != kVNotHolder || RefuseNextVerdict("tradeMismatch") != kVMismatch)
        Fail("a holder-state reason does not answer its own verdict");
    if (RefuseNextVerdict("keeperIsPlayer") != kVKeeper || RefuseNextVerdict("notACounter") != kVKeeper || RefuseNextVerdict("short") != kVSoldOut
        || RefuseNextVerdict("fault") != kVFault)
        Fail("a keeper / stock / fault reason does not answer its own verdict");
    if (RefuseNextVerdict("wrongItem") != kVRefuseNext || RefuseNextVerdict("create") != kVRefuseNext) Fail("a reason with no shop verdict must stay 5");
    if (ShowResult(kVRefuseNext) != 0) Fail("verdict 5 shows an engine message");
}

/* T-290 S0 - THE STEAM RELAY PROBE'S PURE DECISIONS (src/common/steamprobe.cpp, the same unit the plugin links).
   The availability values are literals from Valve's steamnetworkingtypes.h, not the enum under test. */
void t_t290_avail_words()
{
    struct Row { int v; const char* w; };
    const Row rows[] = {
        { -102, "cannot-try" }, { -101, "failed" }, { -100, "previously" }, { -10, "retrying" }, { 0, "unknown" },
        { 1, "never-tried" }, { 2, "waiting" }, { 3, "attempting" }, { 100, "current" }, { 7, "other" }, { -1, "other" }
    };
    size_t i;
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
        if (std::string(coopsteam::AvailWord(rows[i].v)) != rows[i].w)
            Fail(std::string("AvailWord maps to ") + rows[i].w);
    if (std::string(coopsteam::RelayOutcomeWord(0)) != "waiting" || std::string(coopsteam::RelayOutcomeWord(1)) != "available"
        || std::string(coopsteam::RelayOutcomeWord(2)) != "unavailable" || std::string(coopsteam::RelayOutcomeWord(3)) != "failed")
        Fail("RelayOutcomeWord");
}

void t_t290_relay_settle_rule()
{
    using namespace coopsteam;
    /* The second argument is ELAPSED MILLISECONDS since the probe started (T621: frames are no bound). */
    const long long cap = kRelayWaitCapMs;
    if (kRelayWaitCapMs != 90000)                          Fail("the wait cap is 90 s of wall time");
    if (kRelayPumpAfterMs != 10000)                        Fail("pumping starts after 10 s unsettled");
    if (RelayDecide(100, 0, cap) != RELAY_AVAILABLE)       Fail("current at once is available");
    if (RelayDecide(100, 500000, cap) != RELAY_AVAILABLE)  Fail("current past the cap is still available (settled beats the cap)");
    if (RelayDecide(-102, 0, cap) != RELAY_UNAVAILABLE)    Fail("cannot-try is unavailable at once");
    if (RelayDecide(-101, 5, cap) != RELAY_FAILED)         Fail("failed is failed at once");
    if (RelayDecide(-100, 5, cap) != RELAY_FAILED)         Fail("previously is failed at once");
    if (RelayDecide(2, 2000, cap) != RELAY_WAIT)           Fail("T621: waiting 2 s in (3600 polls at ~1800 fps) keeps polling");
    if (RelayDecide(3, 89999, cap) != RELAY_WAIT)          Fail("attempting 1 ms below the cap keeps polling");
    if (RelayDecide(3, 90000, cap) != RELAY_UNAVAILABLE)   Fail("attempting AT the cap settles as unavailable");
    if (RelayDecide(-10, 1000, cap) != RELAY_WAIT)         Fail("retrying keeps polling");
    if (RelayDecide(1, 1000, cap) != RELAY_WAIT)           Fail("never-tried keeps polling");
    if (RelayDecide(0, 1000, cap) != RELAY_WAIT)           Fail("unknown keeps polling");
    if (RelayDecide(42, 90000, cap) != RELAY_UNAVAILABLE)  Fail("an unlisted value is not treated as settled, and the cap ends it");
    const long long after = kRelayPumpAfterMs;
    if (RelayWantsPump(2, 9999, cap, after))               Fail("no pumping before 10 s");
    if (!RelayWantsPump(2, 10000, cap, after))             Fail("waiting at 10 s starts pumping");
    if (!RelayWantsPump(3, 50000, cap, after))             Fail("attempting past 10 s wants pumping");
    if (!RelayWantsPump(-10, 10000, cap, after))           Fail("retrying is unsettled: wants pumping");
    if (RelayWantsPump(100, 20000, cap, after))            Fail("current is settled: no pumping");
    if (RelayWantsPump(-102, 20000, cap, after))           Fail("cannot-try is settled: no pumping");
    if (RelayWantsPump(-101, 20000, cap, after))           Fail("failed is settled: no pumping");
    if (RelayWantsPump(2, 90000, cap, after))              Fail("at the cap the wait ends: no pumping");
}

void t_t290_relay_line()
{
    using namespace coopsteam;
    if (RelayLine(RELAY_AVAILABLE, 100, 100, 100, 187, 4210, -1, "")
        != "[NET] relay: available status=current (config=current anyRelay=current, 187 polls, 4210 ms, callbacks not pumped)")
        Fail("available line carries the elapsed ms");
    if (RelayLine(RELAY_UNAVAILABLE, 2, 3, 2, 162000, 90000, 10016, "Attempt #1 to fetch config")
        != "[NET] relay: unavailable still-waiting (config=attempting anyRelay=waiting, 162000 polls, 90000 ms, "
           "callbacks pumped from 10016 ms) - steam says: Attempt #1 to fetch config")
        Fail("cap line says still-<word> and from when callbacks were pumped");
    if (RelayLine(RELAY_UNAVAILABLE, -102, -102, -102, 4, 3, -1, "")
        != "[NET] relay: unavailable status=cannot-try (config=cannot-try anyRelay=cannot-try, 4 polls, 3 ms, callbacks not pumped)")
        Fail("cannot-try line says status=");
    if (RelayLine(RELAY_FAILED, -101, -101, -102, 900, 31000, 10000, "No internet")
        != "[NET] relay: failed status=failed (config=failed anyRelay=cannot-try, 900 polls, 31000 ms, "
           "callbacks pumped from 10000 ms) - steam says: No internet")
        Fail("failed line");
    if (RelayPumpLine(2, 3, 2, 18000, 10016, true, "Attempt #1")
        != "[NET] relay: still-waiting after 10016 ms (config=attempting anyRelay=waiting, 18000 polls) - "
           "pumping SteamAPI_RunCallbacks once per frame until it settles - steam says: Attempt #1")
        Fail("pump line");
    if (RelayPumpLine(3, 100, 3, 50, 10000, false, "")
        != "[NET] relay: still-attempting after 10000 ms (config=current anyRelay=attempting, 50 polls) - "
           "SteamAPI_RunCallbacks not exported, not pumping; still polling")
        Fail("pump line without the export");
}

void t_t290_file_version_and_debug_text()
{
    using namespace coopsteam;
    unsigned char b[96];
    std::memset(b, 0xAB, sizeof(b));
    /* VS_FIXEDFILEINFO at offset 40: BD 04 EF FE, struc 00 00 01 00, MS 0x0003002A, LS 0x003D0042 -> 3.42.61.66 */
    const unsigned char fixedInfo[16] = { 0xBD, 0x04, 0xEF, 0xFE, 0x00, 0x00, 0x01, 0x00,
                                          0x2A, 0x00, 0x03, 0x00, 0x42, 0x00, 0x3D, 0x00 };
    std::memcpy(b + 40, fixedInfo, 16);
    unsigned int ms = 0, ls = 0;
    if (!FindFixedFileVersion(b, sizeof(b), &ms, &ls) || ms != 0x0003002Au || ls != 0x003D0042u)
        Fail("the signature at an aligned offset is found and its version read");
    if (FileVersionText(ms, ls) != "3.42.61.66")
        Fail("version text");
    if (FindFixedFileVersion(b, 50, &ms, &ls))
        Fail("a signature whose version fields run past the end is refused");
    unsigned char c[96];
    std::memset(c, 0, sizeof(c));
    std::memcpy(c + 41, fixedInfo, 16);   /* misaligned: not a VS_FIXEDFILEINFO */
    if (FindFixedFileVersion(c, sizeof(c), &ms, &ls))
        Fail("a misaligned signature is ignored");
    if (FindFixedFileVersion(0, 64, &ms, &ls))
        Fail("a null blob is refused");

    const char raw[12] = { ' ', 'O', 'K', '\n', '\t', ' ', 'r', 'e', 'l', 'a', 'y', '!' };   /* no NUL: bounded by cap */
    if (CleanDebugText(raw, sizeof(raw), 100) != "OK relay!")
        Fail("control bytes become one space, ends trimmed, no NUL needed");
    if (CleanDebugText("abcdefghij", 64, 6) != "abc...")
        Fail("a long text is cut with ...");
    if (CleanDebugText("", 64, 10) != "" || CleanDebugText(0, 64, 10) != "")
        Fail("empty and null give empty");
}

/* T-306 (owner decision 226; fold 1): an orphaned zone tick reports what a live tick reports; the only pure decision left is
   WHEN its player sector goes out as unknown - never on a live tick, never on the FIRST orphaned tick (one unreadable tick
   while alive must not erase the player row at the notebook), always from the second on. */
void t_t306_orphan_hold()
{
    if (cooporphan::kOrphanUnknownAfterTicks != 2) Fail("kOrphanUnknownAfterTicks is not 2");
    /* orphanRun, want */
    static const int rows[][2] = {
        { 0, 0 },      /* a live tick */
        { 1, 0 },      /* the first orphaned tick still sends the last live sector */
        { 2, 1 },      /* from the second: unknown */
        { 3, 1 },
        { 600, 1 },    /* a long spell stays unknown */
        { -1, 0 }      /* nonsense is not "orphaned" */
    };
    const int n = (int)(sizeof(rows) / sizeof(rows[0]));
    for (int i = 0; i < n; ++i)
    {
        const int got = cooporphan::OrphanSendUnknownSector(rows[i][0]);
        if (got != rows[i][1])
        {
            char b[128];
            _snprintf(b, 127, "OrphanSendUnknownSector(%d) = %d, want %d", rows[i][0], got, rows[i][1]);
            b[127] = 0; Fail(b);
        }
    }
    /* a spell: live, orphaned x3, live again (the run counter restarts), one orphaned blip - the blip never sends unknown */
    static const int spell[] = { 0, 1, 2, 3, 0, 1, 0 };
    static const int want[]  = { 0, 0, 1, 1, 0, 0, 0 };
    for (int i = 0; i < (int)(sizeof(spell) / sizeof(spell[0])); ++i)
        if (cooporphan::OrphanSendUnknownSector(spell[i]) != want[i]) Fail("T-306 spell: step " + std::string(1, (char)('0' + i)) + " sent the wrong player sector");
}

/* ---- M5a (T-197 piece 4): THE LIVE ENVELOPE (src/common/liveenvelope.h) ------------------------------------------------ */
std::vector<char> M5aBytes(size_t n) { std::vector<char> v(n); for (size_t i = 0; i < n; ++i) v[i] = (char)(i * 7 + 3); return v; }
bool M5aSame(const char* a, const char* b, size_t n) { for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false; return true; }
void t_m5a_live_up_round_trip()
{
    const std::vector<char> inner = M5aBytes(41);
    std::vector<char> b;
    if (!cooplive::LiveUpEncode(&b, cooplive::kRouteWorld, 0, cooplive::kInnerRelation, &inner[0], inner.size())) { Fail("a WORLD RELATION envelope was refused"); return; }
    if (b.size() != cooplive::kLiveUpHeader + inner.size()) Fail("the up envelope is not header + inner");
    cooplive::LiveUp u;
    if (cooplive::LiveUpDecode(&b[0], b.size(), &u) != cooplive::kLiveFwd) { Fail("a good up envelope did not decode"); return; }
    if (u.route != (unsigned)cooplive::kRouteWorld || u.innerType != cooplive::kInnerRelation || u.target != 0 || u.innerLen != inner.size()) Fail("the up envelope's fields changed on the way");
    if (!M5aSame(&b[u.innerAt], &inner[0], inner.size())) Fail("the inner bytes changed on the way up");
    std::vector<char> s; const char z = 0;
    if (!cooplive::LiveUpEncode(&s, cooplive::kRouteSlot, 65519u, cooplive::kInnerRelSync, &z, 1)) { Fail("a SLOT RELSYNC envelope was refused"); return; }
    if (cooplive::LiveUpDecode(&s[0], s.size(), &u) != cooplive::kLiveFwd || u.route != (unsigned)cooplive::kRouteSlot || u.target != 65519u || u.innerType != cooplive::kInnerRelSync || u.innerLen != 1) Fail("the SLOT envelope did not round-trip");
    std::vector<char> r;
    if (cooplive::LiveUpEncode(&r, 0, 0, 32, &z, 1) || cooplive::LiveUpEncode(&r, 5, 0, 32, &z, 1)) Fail("a route this wire does not know was encoded");   /* fold 1: 4 is WORLD_EXCEPT */
    if (cooplive::LiveUpEncode(&r, cooplive::kRouteWorld, 0, 256, &z, 1)) Fail("an inner type over a byte was encoded");
    const std::vector<char> big(cooplive::kLiveInnerMax + 1, 'x');
    if (cooplive::LiveUpEncode(&r, cooplive::kRouteWorld, 0, 32, &big[0], big.size())) Fail("an inner payload over the bound was encoded");
    if (!r.empty()) Fail("a refused encode appended bytes");
}
void t_m5a_live_down_round_trip()
{
    const std::vector<char> inner = M5aBytes(17);
    const int slots[] = { 0, 1, 1023, 1024, 65519, 65535 };
    for (size_t k = 0; k < sizeof(slots) / sizeof(slots[0]); ++k)
    {
        std::vector<char> b;
        if (!cooplive::LiveDownEncode(&b, slots[k], cooplive::kInnerRelation, &inner[0], inner.size())) { Fail("a down envelope with a u16 slot was refused"); continue; }
        cooplive::LiveDown d;
        if (!cooplive::LiveDownDecode(&b[0], b.size(), &d)) { Fail("a good down envelope did not decode"); continue; }
        if ((int)d.originSlot != slots[k] || d.innerType != cooplive::kInnerRelation || d.innerLen != inner.size() || !M5aSame(&b[d.innerAt], &inner[0], inner.size())) Fail("the down envelope changed on the way");
    }
    std::vector<char> r;
    if (cooplive::LiveDownEncode(&r, -1, 32, &inner[0], inner.size()) || cooplive::LiveDownEncode(&r, 65536, 32, &inner[0], inner.size())) Fail("a slot outside 0..65535 was encoded");
    if (!r.empty()) Fail("a refused down encode appended bytes");
}
void t_m5a_live_length_mismatch_refused()
{
    const std::vector<char> inner = M5aBytes(20);
    std::vector<char> b; cooplive::LiveUp u;
    cooplive::LiveUpEncode(&b, cooplive::kRouteWorld, 0, 32, &inner[0], inner.size());
    if (cooplive::LiveUpDecode(&b[0], b.size() - 1, &u) != cooplive::kLiveBadLen) Fail("a truncated up envelope was not badLen");
    std::vector<char> x(b); x.push_back(0);
    if (cooplive::LiveUpDecode(&x[0], x.size(), &u) != cooplive::kLiveBadLen) Fail("an up envelope with a trailing byte was not badLen");
    std::vector<char> w(b); w[8] = (char)(w[8] + 1);
    if (cooplive::LiveUpDecode(&w[0], w.size(), &u) != cooplive::kLiveBadLen) Fail("an up length word that disagrees with the frame was not badLen");
    std::vector<char> pd(b); pd[2] = 1;
    if (cooplive::LiveUpDecode(&pd[0], pd.size(), &u) != cooplive::kLiveBadLen) Fail("a non-zero pad byte was not badLen");
    if (cooplive::LiveUpDecode(&b[0], cooplive::kLiveUpHeader - 1, &u) != cooplive::kLiveBadLen) Fail("a frame shorter than the header was not badLen");
    std::vector<char> ov(cooplive::kLiveUpHeader + cooplive::kLiveInnerMax + 1, 0);
    ov[0] = 1; ov[1] = 32; const unsigned int big = cooplive::kLiveInnerMax + 1; std::memcpy(&ov[8], &big, 4);
    if (cooplive::LiveUpDecode(&ov[0], ov.size(), &u) != cooplive::kLiveBadLen) Fail("an inner payload over the bound decoded");
    std::vector<char> ur(b); ur[0] = 9;
    if (cooplive::LiveUpDecode(&ur[0], ur.size(), &u) != cooplive::kLiveBadRoute || u.route != 9) Fail("an unknown route was not badRoute (naming it)");
    std::vector<char> d; cooplive::LiveDown dd;
    cooplive::LiveDownEncode(&d, 3, 32, &inner[0], inner.size());
    if (cooplive::LiveDownDecode(&d[0], d.size() - 1, &dd)) Fail("a truncated down envelope decoded");
    std::vector<char> dx(d); dx.push_back(0);
    if (cooplive::LiveDownDecode(&dx[0], dx.size(), &dd)) Fail("a down envelope with a trailing byte decoded");
    if (cooplive::LiveDownDecode(&d[0], cooplive::kLiveDownHeader - 1, &dd)) Fail("a down frame shorter than the header decoded");
}
void t_m5a_live_world_excludes_the_origin_and_the_lobby()
{
    std::vector<int> c; c.push_back(0); c.push_back(1); c.push_back(2); c.push_back(-1); c.push_back(7);
    std::vector<size_t> d;
    if (cooplive::LiveRouteDecide(cooplive::kRouteWorld, 0, 1, c, &d) != cooplive::kLiveFwd) { Fail("WORLD was not forwarded"); return; }
    if (d.size() != 3 || d[0] != 0 || d[1] != 2 || d[2] != 4) Fail("WORLD did not reach exactly every other slotted game");
    std::vector<int> one; one.push_back(4);
    if (cooplive::LiveRouteDecide(cooplive::kRouteWorld, 0, 4, one, &d) != cooplive::kLiveFwd || !d.empty()) Fail("a game alone in its world was refused or sent to itself");
    /* ANY NUMBER OF GAMES: 1024 connected, from three origins - every other game once, the origin never. */
    std::vector<int> many; for (int s = 0; s < 1024; ++s) many.push_back(s);
    const int origins[] = { 0, 511, 1023 };
    for (size_t k = 0; k < 3; ++k)
    {
        cooplive::LiveRouteDecide(cooplive::kRouteWorld, 0, origins[k], many, &d);
        if (d.size() != 1023) Fail("WORLD among 1024 games did not reach the other 1023");
        for (size_t i = 0; i < d.size(); ++i) if (many[d[i]] == origins[k]) { Fail("WORLD sent a message back to its origin"); break; }
    }
}
void t_m5a_live_slot_route()
{
    std::vector<int> c; c.push_back(0); c.push_back(1); c.push_back(-1); c.push_back(7);
    std::vector<size_t> d;
    if (cooplive::LiveRouteDecide(cooplive::kRouteSlot, 7, 0, c, &d) != cooplive::kLiveFwd || d.size() != 1 || d[0] != 3) Fail("SLOT did not reach the one game holding it");
    if (cooplive::LiveRouteDecide(cooplive::kRouteSlot, 5, 0, c, &d) != cooplive::kLiveNoTarget || !d.empty()) Fail("SLOT to an unconnected slot was not noTarget");
    if (cooplive::LiveRouteDecide(cooplive::kRouteSlot, 0, 0, c, &d) != cooplive::kLiveNoTarget) Fail("SLOT to the sender itself was not noTarget");
    if (cooplive::LiveRouteDecide(cooplive::kRouteSlot, 70000u, 0, c, &d) != cooplive::kLiveNoTarget) Fail("SLOT past a u16 was not noTarget");
    if (cooplive::LiveRouteDecide(cooplive::kRouteSlot, 0xFFFFFFFFu, 0, c, &d) != cooplive::kLiveNoTarget) Fail("SLOT 0xFFFFFFFF matched the lobby's -1");
}
void t_m5a_live_area_is_refused_until_m6()
{
    std::vector<int> c; c.push_back(0); c.push_back(1);
    std::vector<size_t> d;
    if (cooplive::LiveRouteDecide(cooplive::kRouteArea, 0, 0, c, &d) != cooplive::kLiveBadRoute || !d.empty()) Fail("AREA was not refused as badRoute (owner decision 57)");
    if (cooplive::LiveRouteDecide(9, 0, 0, c, &d) != cooplive::kLiveBadRoute) Fail("an unknown route was not badRoute");
}
void t_m5a_live_lobby_connection_has_no_slot()
{
    std::vector<int> c; c.push_back(0); c.push_back(1);
    std::vector<size_t> d;
    const unsigned int routes[] = { cooplive::kRouteWorld, cooplive::kRouteSlot, cooplive::kRouteArea };
    for (size_t k = 0; k < 3; ++k)
        if (cooplive::LiveRouteDecide(routes[k], 1, -1, c, &d) != cooplive::kLiveNoSlot || !d.empty()) Fail("a sender with no slot was not noSlot");
}
void t_m5a_relay_peer_is_never_a_transport_id()
{
    for (unsigned int p = 0; p < (1u << 20); ++p) if (cooplive::IsRelayPeer(p)) { Fail("a transport-sized peer id read as a relayed sender"); break; }
    for (unsigned int s = 0; s <= cooplive::kLiveSlotMax; ++s)
    {
        const unsigned int r = cooplive::RelayPeerId(s);
        if (!cooplive::IsRelayPeer(r) || cooplive::RelayPeerSlot(r) != s || r < cooplive::kRelayPeerBase) { Fail("a relayed sender id lost its marker or its slot"); break; }
    }
}
/* ---- P25 fold 2 (T776 FAIL): the owner's INSIDE word ------------------------------------------------------------------- */
void t_p25f2_insidewire_roundtrip()
{
    using namespace p25inside;
    std::vector<char> b;
    if (!EncodeInside(&b, 7u, 1, "abc", 3) || b.size() != 9) { Fail("P25f2: an INSIDE with a 3-byte key must encode to 9 bytes"); return; }
    unsigned int uid = 0; int in = 9; std::string key;
    if (DecodeInside(&b[0], b.size(), &uid, &in, &key) != kInsideDecodeOk || uid != 7u || in != 1 || key != "abc") Fail("P25f2: INSIDE must round-trip uid, inside and key");
    for (size_t n = 0; n < b.size(); ++n)
        if (DecodeInside(n ? &b[0] : "", n, &uid, &in, &key) != kInsideDecodeTooShort) { Fail("P25f2: a cut INSIDE must decode as TooShort"); break; }
    std::vector<char> o;
    if (!EncodeInside(&o, 9u, 0, 0, 0) || o.size() != 6) { Fail("P25f2: an OUTSIDE word must be 6 bytes"); return; }
    if (DecodeInside(&o[0], o.size(), &uid, &in, &key) != kInsideDecodeOk || uid != 9u || in != 0 || !key.empty()) Fail("P25f2: OUTSIDE must round-trip with no key");
    o[4] = (char)0x03;
    if (DecodeInside(&o[0], o.size(), &uid, &in, &key) != kInsideDecodeOk || in != 1) Fail("P25f2: only bit0 of flags says inside; the rest is ignored");
    std::string longKey(kInsideKeyMax + 1, 'k');
    std::vector<char> l;
    if (EncodeInside(&l, 1u, 1, longKey.c_str(), longKey.size()) || !l.empty()) Fail("P25f2: a key over kInsideKeyMax must be refused and append nothing");
    if (EncodeInside(&l, 1u, 1, 0, 3)) Fail("P25f2: a null key with a length must be refused");
    std::vector<char> t(6, 0); t[5] = (char)(unsigned char)(kInsideKeyMax + 1); t.resize(6 + kInsideKeyMax + 1, 'k');
    if (DecodeInside(&t[0], t.size(), &uid, &in, &key) != kInsideDecodeTooLong) Fail("P25f2: a keyLen over kInsideKeyMax must decode as TooLong");
}
void t_p25f2_inside_verdict()
{
    using namespace p25inside;
    if (InsideVerdict(-1, 5, 100.0, 100.0) != kSaidNone) Fail("P25f2: never heard must be None (no keep)");
    if (InsideVerdict(1, 5, 85.0, 100.0) != kSaidIn) Fail("P25f2: an inside word exactly 15 s old must still keep");
    if (InsideVerdict(1, 5, 84.99, 100.0) != kSaidStale) Fail("P25f2: an inside word older than 15 s must be stale (release)");
    if (InsideVerdict(0, 0, 99.0, 100.0) != kSaidOut) Fail("P25f2: a fresh outside word must be Out");
    if (InsideVerdict(0, 5, 99.0, 100.0) != kSaidOut) Fail("P25f2: outside with a key is still Out");
    if (InsideVerdict(1, 0, 99.0, 100.0) != kSaidKeyMiss) Fail("P25f2: inside with no key must be KeyMiss (release)");
    if (InsideVerdict(0, 0, 50.0, 100.0) != kSaidStale) Fail("P25f2: an old outside word is stale");
    volatile double zero = 0.0; const double qn = zero / zero;
    if (InsideVerdict(1, 5, qn, 100.0) != kSaidStale) Fail("P25f2: an unreadable clock must be stale, never a keep");
}
/* ground items' shared names (groundname.h) */
coopgname::NameTable g_t693Tab;
coopgname::GoneMemory g_t693Gone;
std::string T693Key(float x, float z, float y, coopground::GroundKeyParts* parts)
{
    char k[coopground::kGroundKeyCap]; k[0] = 0;
    coopground::GroundKeyParts p;
    std::memset(&p, 0, sizeof p);
    coopground::GroundKeyCompose(k, (int)sizeof k, "2292-gamedata.base", 26, 40, x, y, z, &p);
    if (parts != 0) *parts = p;
    return std::string(k);
}
const void* T693Addr(int i) { return (const void*)(size_t)(0x10000 + (size_t)i * 16); }
coopgname::LiveItem T693Live(int a, float x, float z, float y, int qty)
{
    coopgname::LiveItem li;
    li.addr = T693Addr(a);
    T693Key(x, z, y, &li.cur);
    li.qty = qty; li.q100 = 100;
    return li;
}
void t_t693_name_freeze_at_publish()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    coopground::GroundKeyParts p1, p2, p3;
    const std::string k1 = T693Key(5460.0f, -9360.0f, 10.0f, &p1);
    const std::string k2 = T693Key(5460.4f, -9360.0f, 10.2f, &p2);
    const std::string k3 = T693Key(5461.0f, -9360.0f, 10.1f, &p3);
    char out[coopground::kGroundKeyCap];
    NameBind(&t, T693Addr(1), k1.c_str(), p1, 0);
    if (NameOf(t, T693Addr(1), p1, out, (int)sizeof out) != 1 || k1 != out) Fail("T-693: an own drop is named by its key");
    NameBind(&t, T693Addr(1), k2.c_str(), p2, 0);
    if (NameOf(t, T693Addr(1), p2, out, (int)sizeof out) != 1 || k2 != out) Fail("T-693: before its first publish the name follows the item");
    NameBind(&t, T693Addr(1), k2.c_str(), p2, 1);
    NameBind(&t, T693Addr(1), k3.c_str(), p3, 1);
    if (NameOf(t, T693Addr(1), p3, out, (int)sizeof out) != 1 || k2 != out) Fail("T-693: a published name is frozen - a later announce never renames");
    NameBind(&t, T693Addr(1), k3.c_str(), p3, 0);
    if (NameOf(t, T693Addr(1), p3, out, (int)sizeof out) != 1 || k2 != out) Fail("T-693: a frozen name survives an unfrozen bind");
    if (t.n != 1) Fail("T-693: one item, one row");
    coopground::GroundKeyParts far;
    T693Key(5480.0f, -9360.0f, 10.0f, &far);
    if (NameOf(t, T693Addr(1), far, out, (int)sizeof out) != 0) Fail("T-693: an address past the window names nothing (a reused address)");
    NameBind(&t, T693Addr(1), T693Key(5480.0f, -9360.0f, 10.0f, 0).c_str(), far, 0);
    if (NameOf(t, T693Addr(1), far, out, (int)sizeof out) != 1 || T693Key(5480.0f, -9360.0f, 10.0f, 0) != out) Fail("T-693: a reused address takes a fresh row, not the old name");
}
void t_t693_lookup_by_name_twins()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    coopground::GroundKeyParts pn, pm;
    const std::string n = T693Key(5460.0f, -9360.0f, 10.0f, &pn);
    const std::string m = T693Key(5470.0f, -9360.0f, 10.0f, &pm);
    LiveItem live[4];
    live[0] = T693Live(1, 5461.0f, -9360.0f, 10.0f, 1);   /* named n, settled 1 unit away */
    live[1] = T693Live(2, 5459.5f, -9360.0f, 10.0f, 3);   /* named n too (a twin) */
    live[2] = T693Live(3, 5470.0f, -9360.0f, 10.0f, 1);   /* named m */
    live[3] = T693Live(4, 5460.0f, -9360.0f, 10.0f, 1);   /* unnamed, exactly at n's key */
    NameBind(&t, live[0].addr, n.c_str(), live[0].cur, 1);
    NameBind(&t, live[1].addr, n.c_str(), live[1].cur, 1);
    NameBind(&t, live[2].addr, m.c_str(), live[2].cur, 1);
    int how = 0;
    if (NameLookup(t, live, 4, n.c_str(), 3, 100, 0, &how) != 1 || how != kHowName) Fail("T-693: twins under one name - the bag match picks the quantity");
    if (NameLookup(t, live, 4, n.c_str(), 1, 100, 0, &how) != 0 || how != kHowName) Fail("T-693: twins under one name - quantity 1 picks the other");
    if (NameLookup(t, live, 4, m.c_str(), 1, 100, 1, &how) != 2 || how != kHowName) Fail("T-693: found by name exactly, even exact-only");
    int idx[4];
    if (NameFindName(t, n.c_str(), idx, 4) != 2) Fail("T-693: two rows carry the twins' name");
}
void t_t693_near_refuses_other_name()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const std::string k = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    LiveItem live[3];
    live[0] = T693Live(1, 5460.3f, -9360.0f, 10.0f, 1);   /* named otherwise, 0.3 units from k */
    live[1] = T693Live(2, 5480.0f, -9360.0f, 10.0f, 1);   /* named otherwise, exactly at k2 */
    NameBind(&t, live[0].addr, T693Key(5450.0f, -9360.0f, 10.0f, 0).c_str(), live[0].cur, 1);
    NameBind(&t, live[1].addr, T693Key(5490.0f, -9360.0f, 10.0f, 0).c_str(), live[1].cur, 1);
    int how = 9;
    if (NameLookup(t, live, 2, k.c_str(), 1, 100, 0, &how) != -1 || how != kHowNone) Fail("T-693: a near match never lands on an item named otherwise");
    if (NameLookup(t, live, 2, T693Key(5480.0f, -9360.0f, 10.0f, 0).c_str(), 1, 100, 0, &how) != -1) Fail("T-693: nor does an exact-key match");
    live[2] = T693Live(3, 5460.4f, -9360.0f, 12.0f, 1);   /* unnamed, 0.4 units from k */
    if (NameLookup(t, live, 3, k.c_str(), 1, 100, 0, &how) != 2 || how != kHowNear) Fail("T-693: an unnamed item near the key is found near");
    if (NameLookup(t, live, 3, k.c_str(), 1, 100, 1, &how) != -1) Fail("T-693: exact-only finds no near item");
}
void t_t693_rebind_after_address_change()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    LiveItem was = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1);
    const std::string n = T693Key(5459.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, was.addr, n.c_str(), was.cur, 1);
    LiveItem live[2];
    live[0] = T693Live(7, 5460.2f, -9360.0f, 10.0f, 1);   /* the same item after its zone loaded again: a new address, its place */
    live[1] = T693Live(8, 5460.1f, -9360.1f, 10.0f, 1);   /* a fresh drop beside it - skipped */
    const void* skip[1]; skip[0] = live[1].addr;
    std::vector<NameChange> ch;
    if (NameRebindPlan(t, live, 2, skip, 1, 1, &ch) != 1) Fail("T-693: one orphan, one re-bind");
    if (!ch.empty()) NameApply(&t, &ch[0], (int)ch.size());
    char out[coopground::kGroundKeyCap];
    if (NameOf(t, live[0].addr, live[0].cur, out, (int)sizeof out) != 1 || n != out) Fail("T-693: the re-bound item carries its old name");
    if (NameOf(t, live[1].addr, live[1].cur, out, (int)sizeof out) != 0) Fail("T-693: a pending drop is never re-bound");
    /* another object at a bound address (another sid): its row is forgotten */
    LiveItem other = live[0];
    std::memset(&other.cur, 0, sizeof other.cur);
    coopground::GroundKeyCompose(out, (int)sizeof out, "99-other.base", 26, 40, 5460.2f, 10.0f, -9360.0f, &other.cur);
    NameRebindPlan(t, &other, 1, 0, 0, 0, &ch);
    if (!ch.empty()) NameApply(&t, &ch[0], (int)ch.size());
    if (NameAtAddr(t, live[0].addr) != -1) Fail("T-693: a bound address now holding another sid no longer names it");
    if (NameOrphanCount(t) != 1) Fail("T-693: that row is an orphan (detached), offered to the item at its place");
}
void t_t693_table_overflow()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    t.dropped = 0;
    coopground::GroundKeyParts p;
    const std::string k = T693Key(5460.0f, -9360.0f, 10.0f, &p);
    for (int i = 0; i <= kNameCap; ++i) NameBind(&t, T693Addr(100 + i), k.c_str(), p, 1);
    if (t.n != kNameCap) Fail("T-693: the table holds kNameCap rows");
    if (t.dropped != 1) Fail("T-693: one row dropped to make room, counted");
    if (NameAtAddr(t, T693Addr(100)) != -1) Fail("T-693: the oldest row is the one dropped");
    if (NameAtAddr(t, T693Addr(100 + kNameCap)) < 0 || NameAtAddr(t, T693Addr(101)) < 0) Fail("T-693: the newest and the second oldest stay");
}
void t_t693_reason_by_gone_memory()
{
    using namespace coopgname;
    GoneMemory& m = g_t693Gone;
    GoneClear(&m);
    const std::string n = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    const std::string u = T693Key(5470.0f, -9360.0f, 10.0f, 0);
    GoneNote(&m, n.c_str(), 1, 1000u);
    GoneNote(&m, u.c_str(), 0, 1000u);
    if (TakeMissReason(GoneHas(m, n.c_str(), 2000u)) != coopground::kGroundReasonNotFound) Fail("T-693: a name that left - reason 2");
    if (TakeMissReason(GoneHas(m, T693Key(5460.3f, -9360.0f, 10.0f, 0).c_str(), 2000u)) != coopground::kGroundReasonUnknownName)
        Fail("T-693: a named entry is matched exactly only - near it is reason 5");
    if (TakeMissReason(GoneHas(m, T693Key(5470.3f, -9360.0f, 11.0f, 0).c_str(), 2000u)) != coopground::kGroundReasonNotFound)
        Fail("T-693: an unnamed item that left is matched near, as before names - reason 2");
    if (TakeMissReason(GoneHas(m, T693Key(5500.0f, -9360.0f, 10.0f, 0).c_str(), 2000u)) != coopground::kGroundReasonUnknownName)
        Fail("T-693: a name not known here - reason 5");
    if (GoneHas(m, n.c_str(), 1000u + kGoneKeepMs + 1u) != 0) Fail("T-693: the memory forgets after 10 minutes");
    for (int i = 0; i < kGoneCap + 3; ++i) GoneNote(&m, T693Key(6000.0f + (float)i, -9360.0f, 10.0f, 0).c_str(), 1, 3000u);
    if (m.overwritten != 5) Fail("T-693: a full memory overwrites the oldest, counted");
}
std::string T693NameOf(const coopgname::NameTable& t, const coopgname::LiveItem& li)
{
    char out[coopground::kGroundKeyCap];
    if (coopgname::NameOf(t, li.addr, li.cur, out, (int)sizeof out) != 1) return std::string();
    return std::string(out);
}
void T693Apply(coopgname::NameTable* t, const std::vector<coopgname::NameChange>& ch)
{
    if (!ch.empty()) coopgname::NameApply(t, &ch[0], (int)ch.size());
}
void t_t693_dead_name_never_rebinds()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const LiveItem dead = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1);
    const std::string n = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, dead.addr, n.c_str(), dead.cur, 1);
    LiveItem nb = T693Live(2, 5460.3f, -9360.0f, 10.0f, 1);   /* an unnamed neighbour of the same sid 0.3 units away */
    std::vector<NameChange> ch;
    if (NameRebindPlan(t, &nb, 1, 0, 0, 0, &ch) != 0) Fail("T-693: a row whose item left the ground (no zone taken apart) is never handed to a neighbour");
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 0 || !T693NameOf(t, nb).empty()) Fail("T-693: such a row is no orphan; the neighbour stays unnamed");
    const void* held[1]; held[0] = dead.addr;
    if (NameRebindPlan(t, &nb, 1, held, 1, 1, &ch) != 0) Fail("T-693: a held / escrowed item's row is never an orphan, even after a zone was taken apart");
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 0) Fail("T-693: a held item's row is not marked");
    NameForgetAddr(&t, dead.addr);   /* the road that took the item off this ground for good forgets its row */
    if (NameRebindPlan(t, &nb, 1, 0, 0, 1, &ch) != 0) Fail("T-693: a forgotten (dead) name never re-binds, whatever was taken apart");
    /* control: the same row, missing when a zone was taken apart - an orphan, marked in one pass and handed on in a later one */
    NameBind(&t, dead.addr, n.c_str(), dead.cur, 1);
    if (NameRebindPlan(t, 0, 0, 0, 0, 1, &ch) != 0) Fail("T-693: nothing live: nothing re-bound");
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 1) Fail("T-693: a row missing after a zone was taken apart is marked an orphan");
    if (NameRebindPlan(t, &nb, 1, 0, 0, 0, &ch) != 1) Fail("T-693: a marked orphan is handed on when its zone loads again");
    T693Apply(&t, ch);
    if (T693NameOf(t, nb) != n || NameOrphanCount(t) != 0) Fail("T-693: the item back at a new address carries the old name; no orphan left");
}
void t_t693_rebind_pile_by_height()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    /* as T1714: five items stacked in one pile (one flat spot, 1.4 units apart in height), bound in drop order */
    const float h[5] = { 10.0f, 11.4f, 12.8f, 14.2f, 15.6f };
    std::string nm[5];
    for (int i = 0; i < 5; ++i)
    {
        const LiveItem was = T693Live(1 + i, 5460.0f, -9360.0f, h[i], 1);
        nm[i] = T693Key(5460.0f, -9360.0f, h[i], 0);
        NameBind(&t, was.addr, nm[i].c_str(), was.cur, 1);
    }
    std::vector<NameChange> ch;
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);   /* the zone is taken apart: all five become orphans */
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 5) Fail("T-693 pile: five orphans after the zone was taken apart");
    /* three come back at new addresses, listed in another order than they were dropped (the 5th, the 3rd, the 4th) */
    LiveItem live[3];
    live[0] = T693Live(20, 5460.0f, -9360.0f, h[4], 1);
    live[1] = T693Live(21, 5460.0f, -9360.0f, h[2], 1);
    live[2] = T693Live(22, 5460.0f, -9360.0f, h[3], 1);
    if (NameRebindPlan(t, live, 3, 0, 0, 0, &ch) != 3) Fail("T-693 pile: three items, three re-binds");
    T693Apply(&t, ch);
    if (T693NameOf(t, live[0]) != nm[4] || T693NameOf(t, live[1]) != nm[2] || T693NameOf(t, live[2]) != nm[3])
        Fail("T-693 pile: each item in a pile takes the name at its own height, not the list order");
    if (NameOrphanCount(t) != 2) Fail("T-693 pile: the two items not back keep their rows as orphans");
    /* a pile that settled 0.1 flat apart: the height still decides when the heights differ clearly */
    NameClear(&t);
    const LiveItem a = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1), b = T693Live(2, 5460.1f, -9360.0f, 11.4f, 1);
    const std::string na = T693Key(5460.0f, -9360.0f, 10.0f, 0), nb = T693Key(5460.1f, -9360.0f, 11.4f, 0);
    NameBind(&t, a.addr, na.c_str(), a.cur, 1);
    NameBind(&t, b.addr, nb.c_str(), b.cur, 1);
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);
    T693Apply(&t, ch);
    LiveItem back[2];
    back[0] = T693Live(30, 5460.1f, -9360.0f, 10.0f, 1);   /* a's height, at b's flat spot */
    back[1] = T693Live(31, 5460.0f, -9360.0f, 11.4f, 1);   /* b's height, at a's flat spot */
    if (NameRebindPlan(t, back, 2, 0, 0, 0, &ch) != 2) Fail("T-693 pile: two items, two re-binds");
    T693Apply(&t, ch);
    if (T693NameOf(t, back[0]) != na || T693NameOf(t, back[1]) != nb) Fail("T-693 pile: 0.1 flat apart, the height difference decides");
}
void t_t693_bare_key_reaches_named()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const std::string cur = T693Key(5470.0f, -9360.0f, 10.0f, 0);    /* the key the item has where it lies here */
    const std::string nm = T693Key(5455.0f, -9360.0f, 10.0f, 0);     /* its name: published 15 units away */
    const std::string nearK = T693Key(5470.3f, -9360.0f, 10.4f, 0);  /* another game's key for it: 0.3 flat, 0.4 up */
    LiveItem live[2];
    live[0] = T693Live(1, 5470.0f, -9360.0f, 10.0f, 1);
    NameBind(&t, live[0].addr, nm.c_str(), live[0].cur, 1);
    int how = -1;
    if (NameLookup(t, live, 1, cur.c_str(), 1, 100, 0, &how) != -1) Fail("T-693: a NAME id never lands on an item named otherwise");
    if (NameLookup(t, live, 1, cur.c_str(), 1, 100, 0, &how, 1) != 0 || how != kHowKey) Fail("T-693: a bare-key TAKE reaches the named item by its exact key");
    if (NameLookup(t, live, 1, nearK.c_str(), 1, 100, 0, &how, 1) != 0 || how != kHowNear) Fail("T-693: a bare-key TAKE reaches the named item within 0.5 flat");
    if (NameLookup(t, live, 1, nearK.c_str(), 1, 100, 1, &how, 1) != -1) Fail("T-693: exact-only, a bare key finds no near item");
    if (NameLookup(t, live, 1, nm.c_str(), 1, 100, 1, &how, 1) != 0 || how != kHowName) Fail("T-693: a bare id that is the item's name finds it by name");
    if (T693NameOf(t, live[0]) != nm) Fail("T-693: the matched item's name is what names it from there on (kept row, GONE)");
    /* a bare-key GONE from a holder that knows no names removes the named copy (GrFindRemovable runs this lookup, bare) */
    if (NameLookup(t, live, 1, nearK.c_str(), 1, 100, 0, &how, 1) != 0) Fail("T-693: a bare-key GONE finds the named copy");
    if (NameLookup(t, live, 1, nearK.c_str(), 1, 100, 0, &how, 0) != -1) Fail("T-693: the same key as a NAME does not");
    live[1] = T693Live(2, 5470.0f, -9360.0f, 10.0f, 1);   /* an unnamed item at the very key */
    if (NameLookup(t, live, 2, cur.c_str(), 1, 100, 0, &how, 1) != 1 || how != kHowKey) Fail("T-693: a bare key prefers the item carrying no name");
    if (NameLookup(t, live, 2, nearK.c_str(), 1, 100, 0, &how, 1) != 1 || how != kHowNear) Fail("T-693: near too, the unnamed item first");
}
void t_t693_address_swap()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const LiveItem x = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1), y = T693Live(2, 5470.0f, -9360.0f, 10.0f, 1);
    const std::string nx = T693Key(5459.0f, -9360.0f, 10.0f, 0), ny = T693Key(5469.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    NameBind(&t, y.addr, ny.c_str(), y.cur, 1);
    LiveItem live[2];
    live[0] = T693Live(1, 5470.0f, -9360.0f, 10.0f, 1);   /* the zone loaded again: x's old address now holds y */
    live[1] = T693Live(2, 5460.0f, -9360.0f, 10.0f, 1);   /* and y's holds x */
    std::vector<NameChange> ch;
    if (NameRebindPlan(t, live, 2, 0, 0, 1, &ch) != 2) Fail("T-693: two swapped addresses: both re-bound");
    T693Apply(&t, ch);
    if (T693NameOf(t, live[0]) != ny || T693NameOf(t, live[1]) != nx) Fail("T-693: after the swap each item carries its own name");
    /* another sid at a bound address: the row is detached and offered to the item lying at its place */
    NameClear(&t);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    LiveItem l2[2];
    l2[0] = x;
    char kb[coopground::kGroundKeyCap];
    coopground::GroundKeyCompose(kb, (int)sizeof kb, "99-other.base", 26, 40, 5460.0f, 10.0f, -9360.0f, &l2[0].cur);
    l2[1] = T693Live(3, 5460.1f, -9360.0f, 10.0f, 1);
    if (NameRebindPlan(t, l2, 2, 0, 0, 0, &ch) != 1) Fail("T-693: a row pushed off its address by another sid re-binds to its item");
    T693Apply(&t, ch);
    if (!T693NameOf(t, l2[0]).empty() || T693NameOf(t, l2[1]) != nx) Fail("T-693: the other-sid object carries nothing, the item at the place its name");
    /* the same sid at the address 2 units off after the row's own address went missing (marked an orphan): the 0.5 fit detaches it */
    NameClear(&t);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    LiveItem l3[2];
    l3[0] = T693Live(1, 5462.0f, -9360.0f, 10.0f, 1);
    l3[1] = T693Live(4, 5460.2f, -9360.0f, 10.0f, 1);
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 1) Fail("T-693: the row whose address went missing is marked an orphan");
    if (NameRebindPlan(t, l3, 2, 0, 0, 0, &ch) != 1) Fail("T-693: a row whose own address went missing must fit within 0.5 units");
    T693Apply(&t, ch);
    if (!T693NameOf(t, l3[0]).empty() || T693NameOf(t, l3[1]) != nx) Fail("T-693: the name went to the item at its place, not the address");
    /* an unrelated zone unload: a row whose address stayed live keeps the 5-unit window, and the pass refreshes its key */
    NameClear(&t);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    if (NameRebindPlan(t, l3, 2, 0, 0, 1, &ch) != 0) Fail("T-693: an unrelated unload: a live-address row 2 units off is not handed on");
    T693Apply(&t, ch);
    if (T693NameOf(t, l3[0]) != nx || !T693NameOf(t, l3[1]).empty()) Fail("T-693: an unrelated unload: the live-address row keeps the 5-unit window");
    {
        const int ri = NameAtAddr(t, x.addr);
        if (ri < 0 || coopground::GroundKeySame(t.row[ri].cur, l3[0].cur) == 0) Fail("T-693: the pass refreshed the live row's current key");
    }
    /* no zone taken apart: the 5-unit window keeps the item that rolled */
    NameClear(&t);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    if (NameRebindPlan(t, l3, 2, 0, 0, 0, &ch) != 0) Fail("T-693: nothing taken apart: no re-bind");
    T693Apply(&t, ch);
    if (T693NameOf(t, l3[0]) != nx || !T693NameOf(t, l3[1]).empty()) Fail("T-693: nothing taken apart: the item that rolled 2 units keeps its name");
}
void t_t693_orphan_expiry()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const LiveItem x = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1);
    const std::string nx = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    std::vector<NameChange> ch;
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);   /* its zone was taken apart and never loads again */
    if (ch.size() != 1) { Fail("T-693: one row marked"); return; }
    NameApply(&t, &ch[0], (int)ch.size(), 1000u);
    if (NameOrphanCount(t) != 1) Fail("T-693: the row is an orphan");
    if (NameExpireOrphans(&t, 1000u + kOrphanKeepMs - 1u) != 0 || NameOrphanCount(t) != 1) Fail("T-693: an orphan is kept for 10 minutes");
    if (NameExpireOrphans(&t, 1000u + kOrphanKeepMs) != 1 || NameOrphanCount(t) != 0 || t.n != 0) Fail("T-693: an orphan nobody took back in 10 minutes is forgotten");
    NameBind(&t, x.addr, nx.c_str(), x.cur, 1);
    if (NameExpireOrphans(&t, 0xFFFFFFF0u) != 0 || t.n != 1) Fail("T-693: a row that is no orphan never expires");
}
void t_t693_name_for_check()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const LiveItem a = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1), b = T693Live(2, 5470.0f, -9360.0f, 10.0f, 1);
    const std::string listed = T693Key(5460.0f, -9360.0f, 10.0f, 0), nm = T693Key(5459.5f, -9360.0f, 10.0f, 0), other = T693Key(5461.0f, -9360.0f, 10.0f, 0);
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 0) != kNfOk) Fail("T-693: NAME FOR an unnamed item, the name free: given");
    const void* sk[2]; sk[0] = b.addr; sk[1] = a.addr;
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), sk, 2, 0) != kNfSkipped) Fail("T-693: NAME FOR refused on a skip-list address");
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), sk, 1, 0) != kNfOk) Fail("T-693: another address on the skip list does not refuse");
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 1) != kNfSkipped) Fail("T-693: NAME FOR refused on this game's own unconfirmed drop");
    NameBind(&t, b.addr, nm.c_str(), b.cur, 1);
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 0) != kNfNameTaken) Fail("T-693: NAME FOR refused when another row carries the name (no second row with one name)");
    NameClear(&t);
    NameBind(&t, a.addr, other.c_str(), a.cur, 1);
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 0) != kNfOtherName) Fail("T-693: NAME FOR refused when the item carries a name other than the listed id");
    NameClear(&t);
    NameBind(&t, a.addr, listed.c_str(), a.cur, 1);
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 0) != kNfOk) Fail("T-693: NAME FOR an item carrying the listed id: given");
    NameClear(&t);
    NameBind(&t, a.addr, nm.c_str(), a.cur, 1);
    if (NameForCheck(t, a.addr, a.cur, nm.c_str(), listed.c_str(), 0, 0, 0) != kNfOk) Fail("T-693: NAME FOR an item already carrying that name: given (its own row)");
}
/* T-1129: a waiting name (orphan) left after the re-bind is forgotten once the catch-up for its area square is applied, and is then
   never handed to a returning item; an orphan does not refuse the catch-up's rename */
void t_t1129_stale_name_after_catchup()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    coopground::GroundKeyParts sp;
    T693Key(5460.0f, -9360.0f, 10.0f, &sp);   /* the area square the pile lies in */
    /* three plates in one pile; the plate at 12.8 was picked on the other game while this game was away */
    const float h[3] = { 10.0f, 11.4f, 12.8f };
    std::string nm[3];
    for (int i = 0; i < 3; ++i)
    {
        const LiveItem was = T693Live(1 + i, 5460.0f, -9360.0f, h[i], 1);
        nm[i] = T693Key(5460.0f, -9360.0f, h[i], 0);
        NameBind(&t, was.addr, nm[i].c_str(), was.cur, 1);
    }
    /* another area's orphan and this game's own unconfirmed drop's orphan, in the same square */
    const LiveItem far1 = T693Live(9, 9000.0f, -9360.0f, 10.0f, 1);
    coopground::GroundKeyParts fp = far1.cur;
    fp.sx = sp.sx + 1;   /* last seen in the next area square */
    const std::string nFar = T693Key(9000.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, far1.addr, nFar.c_str(), fp, 1);
    const LiveItem own = T693Live(8, 5440.0f, -9360.0f, 10.0f, 1);
    const std::string nOwn = T693Key(5440.0f, -9360.0f, 10.0f, 0);
    NameBind(&t, own.addr, nOwn.c_str(), own.cur, 0);
    std::vector<NameChange> ch;
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);   /* the area is taken apart: every row an orphan */
    T693Apply(&t, ch);
    if (NameOrphanCount(t) != 5) Fail("T-1129: five orphans after the area was taken apart");
    LiveItem back[2];
    back[0] = T693Live(20, 5460.0f, -9360.0f, h[0], 1);
    back[1] = T693Live(21, 5460.0f, -9360.0f, h[1], 1);
    if (NameRebindPlan(t, back, 2, 0, 0, 0, &ch) != 2) Fail("T-1129: the two plates still there take their names back");
    T693Apply(&t, ch);
    /* the stale name does not refuse the catch-up's rename of a plate here */
    const LiveItem fresh = T693Live(30, 5460.0f, -9360.0f, 13.0f, 1);
    const std::string listedFresh = T693Key(5460.0f, -9360.0f, 13.0f, 0);
    if (NameForCheck(t, fresh.addr, fresh.cur, nm[2].c_str(), listedFresh.c_str(), 0, 0, 0) != kNfOk)
        Fail("T-1129: an orphan carrying the name does not refuse the catch-up's NAME FOR");
    if (NameForCheck(t, fresh.addr, fresh.cur, nm[0].c_str(), listedFresh.c_str(), 0, 0, 0) != kNfNameTaken)
        Fail("T-1129: a live row carrying the name still refuses it");
    /* the catch-up for the square is applied: its orphans go, except this game's own unconfirmed drop and another square's */
    const char* keep[1]; keep[0] = nOwn.c_str();
    if (NameForgetOrphansIn(&t, sp.sx, sp.sy, keep, 1) != 1) Fail("T-1129: the one stale orphan of the square is forgotten");
    if (NameOrphanCount(t) != 2 || NameFindName(t, nm[2].c_str(), 0, 0) != 0) Fail("T-1129: the stale name is gone; the own drop's and the other square's orphans stay");
    if (NameForgetOrphansIn(&t, sp.sx, sp.sy, 0, 0) != 1 || NameFindName(t, nOwn.c_str(), 0, 0) != 0) Fail("T-1129: without keep the own drop's orphan goes too");
    if (NameFindName(t, nFar.c_str(), 0, 0) != 1 || (fp.sx == sp.sx && fp.sy == sp.sy)) Fail("T-1129: another square's orphan is kept");
    /* the area reloads again: the two plates and a third unnamed plate at the stale name's height - the stale name is never handed on */
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);
    T693Apply(&t, ch);
    LiveItem again[3];
    again[0] = T693Live(40, 5460.0f, -9360.0f, h[0], 1);
    again[1] = T693Live(41, 5460.0f, -9360.0f, h[1], 1);
    again[2] = T693Live(42, 5460.0f, -9360.0f, h[2], 1);
    if (NameRebindPlan(t, again, 3, 0, 0, 0, &ch) != 2) Fail("T-1129: after the catch-up only the two real names re-bind");
    T693Apply(&t, ch);
    if (T693NameOf(t, again[0]) != nm[0] || T693NameOf(t, again[1]) != nm[1] || !T693NameOf(t, again[2]).empty())
        Fail("T-1129: the stale name of a picked plate is never taken after the catch-up");
    /* the name the catch-up gives: its orphan forgotten, one name one row */
    NameClear(&t);
    NameBind(&t, T693Addr(50), nm[2].c_str(), back[0].cur, 1);
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);
    T693Apply(&t, ch);
    if (NameForgetOrphansNamed(&t, nm[1].c_str()) != 0 || NameForgetOrphansNamed(&t, nm[2].c_str()) != 1 || t.n != 0)
        Fail("T-1129: the orphan carrying the given name is forgotten, no other");
}
/* T-1129: before a square's ground listing - re-bind, then forget the waiting names left, then list. Forgetting first would strip
   the names of items that came back; an own unconfirmed drop's orphan is kept by every helper */
void t_t1129_rebind_then_forget()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    coopground::GroundKeyParts sp;
    T693Key(5460.0f, -9360.0f, 10.0f, &sp);
    const float h[3] = { 10.0f, 11.4f, 12.8f };
    std::string nm[3];
    LiveItem back[2];
    back[0] = T693Live(20, 5460.0f, -9360.0f, h[0], 1);
    back[1] = T693Live(21, 5460.0f, -9360.0f, h[1], 1);
    std::vector<NameChange> ch;
    for (int order = 0; order < 2; ++order)   /* 0: forget first (wrong); 1: re-bind first (as GrListingOrphansClose) */
    {
        NameClear(&t);
        for (int i = 0; i < 3; ++i)
        {
            const LiveItem was = T693Live(1 + i, 5460.0f, -9360.0f, h[i], 1);
            nm[i] = T693Key(5460.0f, -9360.0f, h[i], 0);
            NameBind(&t, was.addr, nm[i].c_str(), was.cur, 1);
        }
        NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);   /* the square is taken apart */
        T693Apply(&t, ch);
        if (order == 0)
        {
            if (NameForgetOrphansIn(&t, sp.sx, sp.sy, 0, 0) != 3) Fail("T-1129 order: forgetting first takes all three");
            if (NameRebindPlan(t, back, 2, 0, 0, 0, &ch) != 0) Fail("T-1129 order: forgetting first leaves the two returning items nameless");
            continue;
        }
        if (NameRebindPlan(t, back, 2, 0, 0, 0, &ch) != 2) Fail("T-1129 order: the re-bind first gives the two items their names");
        T693Apply(&t, ch);
        if (NameForgetOrphansIn(&t, sp.sx, sp.sy, 0, 0) != 1) Fail("T-1129 order: then only the one name nobody took back is forgotten");
        if (T693NameOf(t, back[0]) != nm[0] || T693NameOf(t, back[1]) != nm[1] || NameOrphanCount(t) != 0) Fail("T-1129 order: the listing then sees both names");
    }
    /* the keep list: an own unconfirmed drop's orphan holds its name against a NAME FOR and is never forgotten by name */
    NameClear(&t);
    NameBind(&t, T693Addr(60), nm[2].c_str(), back[0].cur, 0);
    NameRebindPlan(t, 0, 0, 0, 0, 1, &ch);
    T693Apply(&t, ch);
    const LiveItem fresh = T693Live(61, 5460.0f, -9360.0f, 13.0f, 1);
    const std::string listedFresh = T693Key(5460.0f, -9360.0f, 13.0f, 0);
    const char* keep[1]; keep[0] = nm[2].c_str();
    if (NameForCheck(t, fresh.addr, fresh.cur, nm[2].c_str(), listedFresh.c_str(), 0, 0, 0, keep, 1) != kNfNameTaken)
        Fail("T-1129 keep: an own drop's orphan still holds its name");
    if (NameForCheck(t, fresh.addr, fresh.cur, nm[2].c_str(), listedFresh.c_str(), 0, 0, 0) != kNfOk) Fail("T-1129 keep: without the keep list the orphan does not hold it");
    if (NameForgetOrphansNamed(&t, nm[2].c_str(), keep, 1) != 0 || t.n != 1) Fail("T-1129 keep: an own drop's orphan is not forgotten by name");
    if (NameForgetOrphansIn(&t, sp.sx, sp.sy, keep, 1) != 0 || t.n != 1) Fail("T-1129 keep: nor with its square");
}
/* T-1129: which of this game's own drops in a pile is held back from the ground listing - flat distance first (the record's height is
   from before the fall), the height difference to the item's last-seen height only when flat distances tie */
void t_t1129_drop_pick_pile()
{
    using namespace coopground;
    GroundKeyParts c[3], rec;
    T693Key(5460.0f, -9360.0f, 10.0f, &c[0]);
    T693Key(5460.0f, -9360.0f, 11.4f, &c[1]);
    T693Key(5460.0f, -9360.0f, 13.0f, &c[2]);
    T693Key(5460.0f, -9360.0f, 30.0f, &rec);   /* the record: the same flat spot, taken at hand height */
    if (GroundDropPick(c, 0, 3, rec, c[1].y10) != 1) Fail("T-1129: in a pile the item at its last-seen height is held back, not the first listed");
    unsigned char taken[3]; taken[0] = 0; taken[1] = 1; taken[2] = 0;
    if (GroundDropPick(c, taken, 3, rec, c[1].y10) != 0) Fail("T-1129: that item taken, the next nearest in height (1.4 below vs 1.6 above)");
    if (GroundDropPick(c, 0, 3, rec, rec.y10) != 2) Fail("T-1129: no last-seen height: the record's own height breaks the tie");
    GroundKeyParts d[2];
    T693Key(5460.0f, -9360.0f, 30.0f, &d[0]);   /* at the record's height but 0.1 flat off */
    d[0].x10 += 1;
    T693Key(5460.0f, -9360.0f, 10.0f, &d[1]);   /* exactly at the record's flat spot, 20 units below */
    if (GroundDropPick(d, 0, 2, rec, rec.y10) != 1) Fail("T-1129: flat distance first - a fall changes the height");
    GroundKeyParts e[2];
    T693Key(5460.6f, -9360.0f, 30.0f, &e[0]);   /* 0.6 flat: out of reach */
    e[1] = c[0];
    e[1].sid[0] = (e[1].sid[0] == 'X') ? 'Y' : 'X';   /* another sid */
    if (GroundDropPick(e, 0, 2, rec, rec.y10) != -1) Fail("T-1129: nothing past 0.5 flat or of another sid");
    if (GroundDropPickBetter(4, 100, 9, 0) != 1 || GroundDropPickBetter(4, 5, 4, 3) != 0 || GroundDropPickBetter(4, 3, 4, 5) != 1)
        Fail("T-1129: the drop pick's order");
}
void t_t693_kept_id_match()
{
    using namespace coopgname;
    const std::string nm = T693Key(5459.0f, -9360.0f, 10.0f, 0), bare = T693Key(5460.0f, -9360.0f, 10.0f, 0), far2 = T693Key(5480.0f, -9360.0f, 10.0f, 0);
    char kn[coopground::kGroundKeyCap], ka[coopground::kGroundKeyCap];
    NameCopy(kn, (int)sizeof kn, nm.c_str());
    NameCopy(ka, (int)sizeof ka, bare.c_str());
    if (KeptIdMatch(bare.c_str(), kn, ka) != 1) Fail("T-693: a bare repeat matches the kept row by the TAKE's own id");
    if (KeptIdMatch(nm.c_str(), kn, ka) != 1) Fail("T-693: a repeat by name matches the kept row's name");
    if (KeptIdMatch(far2.c_str(), kn, ka) != 0) Fail("T-693: another id matches neither");
    ka[0] = 0;
    if (KeptIdMatch(bare.c_str(), kn, ka) != 0) Fail("T-693: no own id kept: only the name matches");
}
void t_t693_nearest_pair_first()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    const LiveItem o1 = T693Live(1, 5460.0f, -9360.0f, 10.0f, 1), o2 = T693Live(2, 5460.5f, -9360.0f, 10.0f, 1);
    const std::string n1 = T693Key(5460.0f, -9360.0f, 10.0f, 0), n2 = T693Key(5460.5f, -9360.0f, 10.0f, 0);
    NameBind(&t, o1.addr, n1.c_str(), o1.cur, 1);
    NameBind(&t, o2.addr, n2.c_str(), o2.cur, 1);
    LiveItem live[2];
    live[0] = T693Live(11, 5460.3f, -9360.0f, 10.0f, 1);   /* 0.3 from o1, 0.2 from o2 - listed first */
    live[1] = T693Live(12, 5460.6f, -9360.0f, 10.0f, 1);   /* 0.1 from o2, 0.6 from o1 (out of reach) */
    std::vector<NameChange> ch;
    if (NameRebindPlan(t, live, 2, 0, 0, 1, &ch) != 2) Fail("T-693: the nearest pair first - both items re-bound");
    T693Apply(&t, ch);
    if (T693NameOf(t, live[1]) != n2 || T693NameOf(t, live[0]) != n1) Fail("T-693: the nearest pair is assigned first, not the first item listed");
}
void t_t693_more_than_16_twins()
{
    using namespace coopgname;
    NameTable& t = g_t693Tab;
    NameClear(&t);
    LiveItem live[20];
    const std::string k = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    for (int i = 0; i < 20; ++i) live[i] = T693Live(10 + i, 5460.0f, -9360.0f, 10.0f, i + 1);
    int how = -1;
    int r = NameLookup(t, live, 20, k.c_str(), 5, 100, 0, &how);
    if (r < 0 || r >= 20 || how != kHowKey || live[r].qty != 5) Fail("T-693: 20 unnamed twins - the exact key's bag match");
    r = NameLookup(t, live, 20, k.c_str(), 19, 100, 0, &how);
    if (r < 0 || r >= 16 || how != kHowKey) Fail("T-693: past 16 twins only the first 16 are weighed, one is still found");
    for (int i = 0; i < 20; ++i) NameBind(&t, live[i].addr, k.c_str(), live[i].cur, 1);
    r = NameLookup(t, live, 20, k.c_str(), 7, 100, 1, &how);
    if (r < 0 || r >= 20 || how != kHowName || live[r].qty != 7) Fail("T-693: 20 twins under one name - the name's bag match");
    r = NameLookup(t, live, 20, k.c_str(), 7, 100, 0, &how, 1);
    if (r < 0 || how != kHowName) Fail("T-693: 20 twins - a bare id that is their name finds them by name");
    std::vector<NameChange> ch;
    if (NameRebindPlan(t, live, 20, 0, 0, 1, &ch) != 0) Fail("T-693: 20 named twins, all live: nothing to re-bind");
}
void t_t693_id_kind_wire()
{
    using namespace coopgname;
    const std::string listed = T693Key(5460.0f, -9360.0f, 10.0f, 0);
    std::vector<char> b;
    IdKindPut(&b, kIdBare, std::string());
    IdKindPut(&b, kIdName, std::string());
    IdKindPut(&b, kIdNameFor, listed);
    IdKindPut(&b, kIdNameFor, std::string());   /* a NAME FOR with nothing listed goes as a NAME */
    if (b.size() != (size_t)(1 + 1 + 1 + 4 + listed.size() + 1)) Fail("T-693: the id kind's bytes");
    size_t at = 0; int k = -1; std::string f;
    if (IdKindGet(b, &at, &k, &f) != 1 || k != kIdBare || !f.empty()) Fail("T-693: BARE read back");
    if (IdKindGet(b, &at, &k, &f) != 1 || k != kIdName || !f.empty()) Fail("T-693: NAME read back");
    if (IdKindGet(b, &at, &k, &f) != 1 || k != kIdNameFor || f != listed) Fail("T-693: NAME FOR read back with its listed id");
    if (IdKindGet(b, &at, &k, &f) != 1 || k != kIdName || at != b.size()) Fail("T-693: an empty NAME FOR read back as NAME, at the end");
    if (IdKindGet(b, &at, &k, &f) != 0) Fail("T-693: no byte left: malformed");
    std::vector<char> bad(1, (char)3);
    at = 0;
    if (IdKindGet(bad, &at, &k, &f) != 0 || at != 0) Fail("T-693: an unknown kind is malformed and reads nothing");
    std::vector<char> cut;
    IdKindPut(&cut, kIdNameFor, listed);
    cut.pop_back();
    at = 0;
    if (IdKindGet(cut, &at, &k, &f) != 0) Fail("T-693: a cut NAME FOR is malformed");
    std::vector<char> zero(5, (char)0);
    zero[0] = (char)kIdNameFor;
    at = 0;
    if (IdKindGet(zero, &at, &k, &f) != 0) Fail("T-693: a NAME FOR with an empty listed id is malformed");
}
void t_t693_catchup_pairs_named()
{
    std::vector<cooppar::ParityGroundRow> holder(1), asked(1);
    holder[0].key = T693Key(5455.0f, -9360.0f, 10.0f, 0); holder[0].qty = 1; holder[0].q100 = 100; holder[0].road = 1;   /* the holder's name */
    asked[0].key = T693Key(5455.3f, -9360.0f, 10.4f, 0); asked[0].qty = 1; asked[0].q100 = 100; asked[0].road = 0;     /* a rejoined game's key for it */
    std::vector<size_t> add, gone;
    std::vector<std::pair<size_t, size_t> > pairs;
    cooppar::ParityGroundPlan(holder, asked, 0, &add, &gone, &pairs);
    if (!add.empty() || !gone.empty()) Fail("T-693: catch-up - the pair needs no ADD and no GONE");
    if (pairs.size() != 1 || pairs[0].first != 0 || pairs[0].second != 0) Fail("T-693: catch-up - the pair is reported, so the holder can name the asker's copy");
    if (holder[0].key == asked[0].key) Fail("T-693: catch-up - the pair's ids differ (the case that is named)");
}
void t_t693_reason5_never_removes()
{
    using namespace coopgshow;
    using namespace coopground;
    for (int tries = 0; tries <= 20; ++tries)
        for (int uh = 0; uh < 2; ++uh)
            for (int uw = 0; uw < 2; ++uw)
                for (int mn = 0; mn < 2; ++mn)
                    if (SettleAction(0, kGroundReasonUnknownName, uh, uw, mn, tries) == kSgRemove) Fail("T-693: reason 5 never takes the picker's copy back out");
    if (SettleAction(0, kGroundReasonUnknownName, 0, 0, 0, 0) != kSgReask) Fail("T-693: reason 5 is asked again");
    if (SettleAction(0, kGroundReasonUnknownName, 0, 0, 0, kReaskMax) != kSgKeepStop) Fail("T-693: reason 5 past the cap - the picker keeps it");
    if (CapCounts(kGroundReasonUnknownName) != 1) Fail("T-693: reason 5 counts toward the cap, as reason 4");
}

/* P105g: the shown ground pickup (groundshow.h) */
void t_p105g_pick_took()
{
    using namespace coopgshow;
    if (PickTook(0, 5, 5) != 5) Fail("P105g: off the ground - the whole stack went in");
    if (PickTook(-1, 5, 5) != 5) Fail("P105g: unreadable after the body (merged) - the whole stack");
    if (PickTook(1, 10, 7) != 3) Fail("P105g: still on the ground with less - the units taken");
    if (PickTook(1, 10, 10) != 0) Fail("P105g: nothing went in - nothing to ask");
    if (PickTook(-1, 10, 10) != 10) Fail("P105g f1: the detour could not read the item after the pickup - the whole stack as it lay is asked");
    if (PickTook(0, 0, 0) != 1) Fail("P105g: a zero stack reads as one item");
}
void t_p105g_ask_action()
{
    using namespace coopgshow;
    if (AskAction(1, 1, 1, 1) != kAkHeldHere) Fail("P105g: held here after all wins");
    if (AskAction(0, 1, 0, 0) != kAkOwnDrop) Fail("P105g f1: our own drop no PUT ever left for (never sent / local-only) - the pickup stands");
    if (AskAction(0, 1, 0, 1) != kAkSend) Fail("P105g f1: our own drop whose PUT timed out (it may have landed) - the holder is asked");
    if (AskAction(0, 1, 1, 1) != kAkSend) Fail("P105g: our own drop with a PUT in flight - ask the holder");
    if (AskAction(0, 0, 0, 0) != kAkSend) Fail("P105g: the ordinary case asks");
}
void t_p105g_settle_action()
{
    using namespace coopgshow;
    using namespace coopground;
    if (SettleAction(1, 0, 1, 1, 0, 99) != kSgGranted) Fail("P105g: a yes is granted whatever else holds");
    if (SettleAction(0, kGroundReasonNotFound, 0, 0, 0, 0) != kSgRemove) Fail("P105g: gone - taken back out");
    if (SettleAction(0, kGroundReasonNotFound, 1, 0, 0, 0) != kSgOwnDrop) Fail("P105g: gone but our own unconfirmed drop - stands");
    if (SettleAction(0, kGroundReasonNotFound, 1, 1, 0, 0) != kSgReask) Fail("P105g: gone while our PUT is in flight - ask again");
    if (SettleAction(0, kGroundReasonNotHolder, 0, 0, 0, 50) != kSgReask) Fail("P105g: not holder - always asked again, never capped");
    if (SettleAction(0, kGroundReasonNotHolder, 0, 0, 1, 0) != kSgHeldHere) Fail("P105g: not holder and ours now - the pickup stands");
    if (SettleAction(0, kGroundReasonBusy, 0, 0, 0, 50) != kSgReask) Fail("P105g: busy - always asked again");
    if (SettleAction(0, kGroundReasonUnreadable, 0, 0, 0, kReaskMax - 1) != kSgReask) Fail("P105g: unreadable under the cap - asked again");
    if (SettleAction(0, kGroundReasonUnreadable, 0, 0, 0, kReaskMax) != kSgKeepStop) Fail("P105g: unreadable at the cap - kept, counted");
    if (SettleAction(0, 0, 0, 0, 0, kReaskMax) != kSgKeepStop) Fail("P105g: an unnamed refusal at the cap - kept, counted");
}
void t_p105g_reask_and_due()
{
    using namespace coopgshow;
    if (ReaskDelayMs(0) != 1000u || ReaskDelayMs(1) != 1000u) Fail("P105g: the first re-ask waits 1 s");
    if (ReaskDelayMs(2) != 2000u || ReaskDelayMs(3) != 4000u) Fail("P105g: the delay doubles");
    if (ReaskDelayMs(40) != 30000u) Fail("P105g: the delay stops at 30 s");
    if (RowDue(1, 9000u, 10000u, 0) != kRdWait) Fail("P105g: in flight under 10 s waits");
    if (RowDue(1, 10001u, 10000u, 0) != kRdSend) Fail("P105g: in flight past 10 s is asked again");
    if (RowDue(0, 0u, 10000u, 1) != kRdWait) Fail("P105g: before its re-ask time it waits");
    if (RowDue(0, 0u, 10000u, 0) != kRdSend) Fail("P105g: at its re-ask time it is sent");
}
/* P105 build 2: the held item (holdwire.h) - every case calls the header items.cpp and session.cpp call */
void t_p105b2_trailers()
{
    using namespace coophold;
    std::vector<char> b; b.push_back('x'); b.push_back('y');
    HoldTail h; h.holdId = 0xA1B2C3D4u; h.how = kHowSwap; h.slot = 3;
    PutHoldTail(&b, h);
    if (b.size() != 2 + kHoldTailLen) Fail("P105b2: HLD1 is 13 bytes after the body");
    HoldTail g;
    if (TakeHoldTail(&b[0], b.size(), &g) != 1 || g.holdId != 0xA1B2C3D4u || g.how != kHowSwap || g.slot != 3) Fail("P105b2: HLD1 round trip");
    if (TakeHoldTail(&b[0], b.size() - 1, &g) != 0) Fail("P105b2: a cut HLD1 is refused (its tag is not where it should be)");
    std::vector<char> bad(b); bad[bad.size() - 5] = (char)9;
    if (TakeHoldTail(&bad[0], bad.size(), &g) != 0) Fail("P105b2: an HLD1 'how' out of range is refused");
    HoldTail m1; m1.slot = -1; std::vector<char> b1; PutHoldTail(&b1, m1);
    if (TakeHoldTail(&b1[0], b1.size(), &g) != 1 || g.slot != -1) Fail("P105b2: slot -1 (no notebook slot) survives the wire");
    LandTail l; l.where = kWhereFree; l.x = 7; l.y = 2; std::vector<char> lb; lb.push_back('q'); PutLandTail(&lb, l);
    LandTail lg;
    if (TakeLandTail(&lb[0], lb.size(), &lg) != 1 || lg.where != kWhereFree || lg.x != 7 || lg.y != 2) Fail("P105b2: LND1 round trip");
    if (TakeHoldTail(&lb[0], lb.size(), &g) != 0) Fail("P105b2: an LND1 is never read as an HLD1");
    std::vector<char> lbad(lb); lbad[lbad.size() - 9] = (char)5;
    if (TakeLandTail(&lbad[0], lbad.size(), &lg) != 0) Fail("P105b2: an LND1 'where' out of range is refused");
    ReqTag t; t.reqId = 42u; t.slot = 1; std::vector<char> tb; tb.push_back('z'); PutReqTag(&tb, t);
    ReqTag tg;
    if (TakeReqTag(&tb[0], tb.size(), &tg) != 1 || tg.reqId != 42u || tg.slot != 1) Fail("P105b2: RQT1 round trip");
    if (TakeReqTag(&tb[0], kReqTagLen - 1, &tg) != 0) Fail("P105b2: a cut RQT1 is refused");
    if (TakeReqTag(&lb[0], lb.size(), &tg) != 0) Fail("P105b2: an LND1 is never read as an RQT1");
}
void t_p105b2_absorb()
{
    using namespace coophold;
    if (AbsorbTagged(1, 1, 1) != 1) Fail("P105b2: a publication tagged with my slot is absorbed");
    if (AbsorbTagged(1, 0, 1) != 0) Fail("P105b2: tagged with another slot - applied as ordinary truth");
    if (AbsorbTagged(0, 1, 1) != 0) Fail("P105b2: untagged - applied");
    if (AbsorbTagged(1, -1, -1) != 1) Fail("P105b2: two games with no slot - the asker absorbs");
    if (AbsorbTagged(1, 2, -1) != 0) Fail("P105b2: a game with no slot applies another slot's tagged move");
}
void t_p105b2_serve_step()
{
    using namespace coophold;
    if (HoldServeStep(1, 0, 0, 0, 1) != kHsRepeat) Fail("P105b2: a repeat is answered from its row before anything else");
    if (HoldServeStep(0, 0, 1, 0, 0) != kReasonNotWriter) Fail("P105b2: not the writer");
    if (HoldServeStep(0, 1, 0, 0, 0) != kReasonPolicy) Fail("P105b2: the access policy refuses");
    if (HoldServeStep(0, 1, 1, kReasonGone, 0) != kReasonGone) Fail("P105b2: the test lever's refusal");
    if (HoldServeStep(0, 0, 1, kReasonGone, 0) != kReasonNotWriter) Fail("P105b2: the lever never stands in for authority");
    if (HoldServeStep(0, 1, 1, 9, 0) != kHsTake) Fail("P105b2: an unknown lever value is ignored");
    if (HoldServeStep(0, 1, 1, 0, 1) != kReasonBusy) Fail("P105b2: a full table is busy (asked again)");
    if (HoldServeStep(0, 1, 1, 0, 0) != kHsTake) Fail("P105b2: the ordinary case takes the item");
}
void t_t159_box_to_box()
{
    using namespace coophold;
    /* the asker: srcIsBox, dstIsBox, dstMine, dstHeld, dstKeyHas, dstInBag, dstFacade, srcWriterSlot, dstWriterSlot */
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 0, 0, 3, 3) != 1) Fail("T-159: two boxes the same other game writes - a LAND into the second box");
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 0, 0, -1, -1) != 1) Fail("T-159: no slot known for either box - both go to the one linked game");
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 0, 0, 3, 4) != 0) Fail("T-159: the second box is written by a third game - the old road");
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 0, 0, 3, -1) != 0) Fail("T-159: one writer unknown - never guessed equal");
    if (LandIntoOtherBox(1, 1, 1, 0, 1, 0, 0, 3, 3) != 0) Fail("T-159: this game writes the second box - the T-164 take road, not a LAND");
    if (LandIntoOtherBox(1, 1, 0, 0, 1, 0, 0, 3, 3) != 0) Fail("T-159: no holder known for the second box - no LAND");
    if (LandIntoOtherBox(1, 1, 0, 1, 0, 0, 0, 3, 3) != 0) Fail("T-159: a box with no key cannot be named");
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 1, 0, 3, 3) != 0) Fail("T-159: a square inside a pack has no LAND form");
    if (LandIntoOtherBox(1, 1, 0, 1, 1, 0, 1, 3, 3) != 0) Fail("T-159: a shop facade's half is the shop road's");
    if (LandIntoOtherBox(0, 1, 0, 1, 1, 0, 0, 3, 3) != 0) Fail("T-159: out of a character - not this road");
    if (LandIntoOtherBox(1, 0, 0, 1, 1, 0, 0, 3, 3) != 0) Fail("T-159: into a character - not this road");
    /* the writer: otherBox, targetWriter, targetAccess, targetShopPiece */
    if (LandTargetOpen(0, 0, 0, 1) != 1) Fail("T-159: a LAND inside one container is never barred here");
    if (LandTargetOpen(1, 1, 1, 0) != 1) Fail("T-159: the writer writes both boxes and the policy lets the player in - open");
    if (LandTargetOpen(1, 0, 1, 0) != 0) Fail("T-159: the writer does not write the target box - barred (the item goes home)");
    if (LandTargetOpen(1, 1, 0, 0) != 0) Fail("T-159: the target box's access policy refuses - barred");
    if (LandTargetOpen(1, 1, 1, 1) != 0) Fail("T-159: a shop piece is never filled by another player's cursor - barred");
    /* a barred target plans with no target square: back to its own square, then any square of its own box, else kept */
    if (LandPlan(kHowPlaced, 0, 0, 1, 1) != kLpOld) Fail("T-159: a barred or full target - back to its own square");
    if (LandPlan(kHowPlaced, 0, 0, 0, 1) != kLpFree) Fail("T-159: its own square taken - any free square");
    if (LandPlan(kHowPlaced, 0, 0, 0, 0) != kLpKeep || LandWhereOf(kLpKeep) != kWhereKept) Fail("T-159: no room anywhere - kept, placed when room appears");
    if (LandPlan(kHowPlaced, 1, 0, 1, 1) != kLpPlaceExact) Fail("T-159: an open target square - placed exactly there");
    /* otherBox, targetOpen, freeInTarget, freeInOwn */
    if (LandFreeFrom(0, 1, 1, 1) != kLfTarget || LandFreeFrom(0, 1, 0, 1) != kLfNone) Fail("T-159: one container - its own free squares only");
    if (LandFreeFrom(1, 1, 1, 1) != kLfTarget) Fail("T-159: into another box - a free square of the target first");
    if (LandFreeFrom(1, 1, 0, 1) != kLfOwn) Fail("T-159: the target box is full - a free square of the item's own box");
    if (LandFreeFrom(1, 0, 1, 1) != kLfOwn) Fail("T-159: a barred target is never filled - its own box");
    if (LandFreeFrom(1, 1, 0, 0) != kLfNone) Fail("T-159: both boxes full - none (kept)");
    if (LandFreeFrom(1, 0, 1, 0) != kLfNone) Fail("T-159: a barred target is never filled, even when the item's own box is full - none (kept)");
    /* the exact target square taken, the target box has room: a free square there before the way home (freeTargetFirst) */
    if (LandPlan(kHowPlaced, 0, 0, 1, 1, 0, 1) != kLpFree) Fail("T-159: the target square taken, the target box has room - its first free square, not home");
    if (LandPlan(kHowPlaced, 1, 0, 1, 1, 0, 1) != kLpPlaceExact) Fail("T-159: the target square free - placed exactly there, not on a free square");
    if (LandPlan(kHowMerged, 0, 1, 1, 1, 0, 1) != kLpMergeExact) Fail("T-159: a merge onto the target stack still comes first");
    if (LandPlan(kHowPlaced, 0, 0, 1, 0, 0, 1) != kLpOld) Fail("T-159: no free square found - home to its own square");
    if (LandPlan(kHowPlaced, 0, 0, 1, 1, 0, 0) != kLpOld) Fail("T-159: the free square is the own box's (target barred or full) - its own square first");
    /* a split that cannot land: back onto the stack it came off (mergeOldOk) before any free square, as HdPutBack */
    if (LandPlan(kHowPlaced, 0, 0, 0, 1, 1, 0) != kLpMergeOld || LandWhereOf(kLpMergeOld) != kWhereOld) Fail("T-159: a refused split merges back onto its own stack (where=old)");
    if (LandPlan(kHowPlaced, 0, 0, 0, 0, 1, 0) != kLpMergeOld) Fail("T-159: a refused split merges back even with no free square - never kept");
    if (LandPlan(kHowPlaced, 0, 0, 1, 1, 1, 0) != kLpOld) Fail("T-159: the own square free (the stack gone) - placed there");
    if (LandPlan(kHowPlaced, 0, 0, 0, 1, 0, 0) != kLpFree) Fail("T-159: the own stack full or another record - a free square");
    if (LandPlan(kHowPlaced, 0, 0, 0, 1, 1, 1) != kLpFree) Fail("T-159: a split into the target box with room there - the target's free square first");
    /* the asker's re-read of the target box: reask, otherBox, targetMineNow */
    if (LandTargetReread(0, 1, 1) != kRrNone) Fail("T-159: an exact answer - nothing re-read");
    if (LandTargetReread(1, 0, 1) != kRrNone) Fail("T-159: a LAND inside one container - no second box");
    if (LandTargetReread(1, 1, 0) != kRrAsk) Fail("T-159: the target box written by another game - the ordinary ask");
    if (LandTargetReread(1, 1, 1) != kRrPull) Fail("T-159: this game writes the target box now - pulled from the previous writer, never kept as shown");
    /* a drop into another box is named like a drop into the same container */
    if (HowAtDrop(1, 1, 0, 0) != kHowMerged || HowAtDrop(1, 0, 1, 0) != kHowSwap || HowAtDrop(1, 0, 0, 0) != kHowPlaced) Fail("T-159: placed / merged / swapped into the other box");
}
void t_p105b2_land_plan()
{
    using namespace coophold;
    if (LandPlan(kHowPlaced, 1, 0, 1, 1) != kLpPlaceExact || LandWhereOf(kLpPlaceExact) != kWhereExact) Fail("P105b2: an empty square takes it exactly");
    if (LandPlan(kHowSwap, 1, 0, 0, 0) != kLpPlaceExact) Fail("P105b2: a swap lands where the displaced item was (lifted first)");
    if (LandPlan(kHowMerged, 0, 1, 1, 1) != kLpMergeExact || LandWhereOf(kLpMergeExact) != kWhereExact) Fail("P105b2: a merge onto the same stack");
    if (LandPlan(kHowBack, 0, 1, 0, 0) != kLpMergeExact) Fail("P105b2: units back onto their own stack");
    if (LandPlan(kHowMerged, 1, 0, 1, 1) != kLpOld) Fail("P105b2: a merge whose stack is gone is never placed as if merged - its old square, reported");
    if (LandPlan(kHowPlaced, 0, 1, 1, 1) != kLpOld || LandWhereOf(kLpOld) != kWhereOld) Fail("P105b2: the square was filled meanwhile - its old square");
    if (LandPlan(kHowPlaced, 0, 0, 0, 1) != kLpFree || LandWhereOf(kLpFree) != kWhereFree) Fail("P105b2: then any free square");
    if (LandPlan(kHowPlaced, 0, 0, 0, 0) != kLpKeep || LandWhereOf(kLpKeep) != kWhereKept) Fail("P105b2: no room - kept, placed when room appears");
}
void t_p105b2_answer_action()
{
    using namespace coophold;
    if (AnswerAction(0, 1, 0, 0, 1) != kAnNone) Fail("P105b2: HOLD yes - the screen is right");
    if (AnswerAction(0, 0, kReasonGone, 0, 1) != kAnPutBackReaskBox) Fail("P105b2: HOLD gone on the cursor - put back, box re-asked");
    if (AnswerAction(0, 0, kReasonPolicy, 0, 1) != kAnPutBackReaskBox) Fail("P105b2: HOLD policy on the cursor - put back, box re-asked");
    if (AnswerAction(0, 0, kReasonGone, 0, 0) != kAnReaskBox) Fail("P105b2: HOLD gone after the drop - the box is re-asked");
    if (AnswerAction(0, 0, kReasonNotWriter, 0, 1) != kAnAskAgain || AnswerAction(0, 0, kReasonBusy, 0, 0) != kAnAskAgain) Fail("P105b2: not the writer / busy - asked again, never guessed");
    if (AnswerAction(1, 1, 0, kWhereExact, 0) != kAnNone) Fail("P105b2: LAND exact - done");
    if (AnswerAction(1, 1, 0, kWhereFree, 0) != kAnReaskBox || AnswerAction(1, 1, 0, kWhereKept, 0) != kAnReaskBox) Fail("P105b2: LAND elsewhere - the box is re-asked");
    if (AnswerAction(1, 0, kReasonGone, 0, 0) != kAnReaskBox) Fail("P105b2: LAND refused (hold unknown) - the box is re-asked");
    if (AnswerAction(1, 0, kReasonBusy, 0, 0) != kAnAskAgain) Fail("P105b2: LAND busy - asked again");
    if (AnswerAction(1, 0, 0, 0, 0) != kAnReaskBox) Fail("P105b2: an unnamed refusal is never asked again blind - the box is re-asked");
}
void t_p105b2_how_at_drop()
{
    using namespace coophold;
    if (HowAtDrop(0, 1, 1, 1) != -1) Fail("P105b2: another container - not a LAND there (back onto its own square first)");
    if (HowAtDrop(1, 0, 0, 1) != kHowBack) Fail("P105b2: the cancelled split");
    if (HowAtDrop(1, 1, 0, 0) != kHowMerged) Fail("P105b2: merged at the square");
    if (HowAtDrop(1, 0, 1, 0) != kHowSwap) Fail("P105b2: a swap");
    if (HowAtDrop(1, 0, 0, 0) != kHowPlaced) Fail("P105b2: placed");
}
/* the offline shape of run leg A5: one HOLD asked three times (the writer's delay lever held the first) - granted once, the repeats
   answered from the row; the asker's re-ask timing is groundshow's RowDue, the same call items.cpp's asker makes */
/* P105 build 2 fold 1 (HIGH-1 / MED-1 / LOW): the asker's HOLD row against its LAND, and the row table's reservation */
void t_p105b2f1_hold_row_done()
{
    using namespace coophold;
    if (HoldRowDone(1, 1, 0) != 1) Fail("P105b2 f1: a granted hold is done when its land goes out");
    if (HoldRowDone(1, 0, 0) != 0) Fail("P105b2 f1: a granted hold still on the cursor stays open");
    if (HoldRowDone(0, 1, 0) != 0) Fail("P105b2 f1: a hold never granted waits for its land's answer");
    if (HoldRowDone(0, 1, 1) != 1) Fail("P105b2 f1 HIGH-1: once its land is answered, a hold never granted is done (its box is free for resync)");
    if (HoldRowDone(0, 0, 0) != 0) Fail("P105b2 f1: a hold on the way, item still on the cursor, stays open");
}
void t_p105b2f1_late_answer()
{
    using namespace coophold;
    if (HoldLateAnswer(1, 0, 1, 1) != kLaResendLand) Fail("P105b2 f1 MED-1: a grant after its land was refused sends the land again");
    if (HoldLateAnswer(1, 1, 1, 1) != kLaSwallow) Fail("P105b2 f1: a repeat of a grant already had is swallowed");
    if (HoldLateAnswer(1, 0, 1, 0) != kLaSwallow) Fail("P105b2 f1: a land answered yes used the hold - nothing to send");
    if (HoldLateAnswer(0, 0, 1, 1) != kLaSwallow) Fail("P105b2 f1: a refusal after the land is swallowed");
    if (HoldLateAnswer(1, 0, 0, 0) != kLaSwallow) Fail("P105b2 f1: no land answered - not a late grant");
}
void t_p105b2f1_scenario_land_overtakes_hold()
{
    /* the asker's HOLD row walked through the real rules: the HOLD is sent and not answered, the item is dropped (its LAND sent), the
       LAND overtakes the HOLD and is refused gone, then the HOLD's grant arrives - twice (a repeat) */
    using namespace coophold;
    int granted = 0, landed = 1, landAnswered = 0, landRefused = 0, resends = 0;
    if (HoldRowDone(granted, landed, landAnswered) != 0) Fail("P105b2 f1 scenario: open until its land is answered");
    if (AnswerAction(1, 0, kReasonGone, 0, 0) != kAnReaskBox) Fail("P105b2 f1 scenario: the refused land re-asks the box");
    landAnswered = 1; landRefused = 1;
    if (HoldRowDone(granted, landed, landAnswered) != 1) Fail("P105b2 f1 scenario: the hold row closes at the land's answer");
    for (int k = 0; k < 2; ++k)
        if (HoldLateAnswer(1, granted, landAnswered, landRefused) == kLaResendLand) { ++resends; granted = 1; landAnswered = 0; landRefused = 0; }
    if (resends != 1) Fail("P105b2 f1 scenario: the late grant sends the land again exactly once");
    if (HoldRowDone(granted, 1, landAnswered) != 1) Fail("P105b2 f1 scenario: the land sent again closes the row");
}
void t_p105b2f1_room_for_hold()
{
    using namespace coophold;
    if (HoldRoomOk(2, 0) != 1) Fail("P105b2 f1: a hold and its land fit");
    if (HoldRoomOk(1, 0) != 0) Fail("P105b2 f1 LOW: no hold without a row for its land");
    if (HoldRoomOk(3, 2) != 0) Fail("P105b2 f1 LOW: every open hold keeps a row for its land");
    if (HoldRoomOk(4, 2) != 1) Fail("P105b2 f1: room for two open holds' lands, a new hold and its land");
}
/* P105 build 2 fold 2 (H1-H4): the ground drop's answer, the writer's remembered answers, the HOLD that arrives after its LAND */
void t_p105b2f2_ground_drop_next()
{
    using namespace coophold;
    if (GroundDropNext(1) != kGdEnded) Fail("P105b2 f2 H1: an item on the ground ends the hold");
    if (GroundDropNext(-1) != kGdEndedFault) Fail("P105b2 f2 H2: a faulted drop ends the hold (never added into the box again)");
    if (GroundDropNext(0) != kGdPutBack) Fail("P105b2 f2 H1: a drop not placed takes the put-back");
}
void t_p105b2f2_answer_final()
{
    using namespace coophold;
    if (AnswerFinal(0, 1, 0) != 1) Fail("P105b2 f2 H3: a HOLD's grant is remembered");
    if (AnswerFinal(0, 0, kReasonGone) != 1) Fail("P105b2 f2 H3: a HOLD's refusal gone is remembered");
    if (AnswerFinal(0, 0, kReasonPolicy) != 1) Fail("P105b2 f2 H3: a HOLD's policy refusal is remembered");
    if (AnswerFinal(0, 0, kReasonBusy) != 0) Fail("P105b2 f2 H3: busy is asked again - never remembered");
    if (AnswerFinal(0, 0, kReasonNotWriter) != 0) Fail("P105b2 f2 H3: not the writer is asked again - never remembered");
    if (AnswerFinal(1, 1, 0) != 1 || AnswerFinal(1, 0, kReasonGone) != 1) Fail("P105b2 f2: a LAND's grant / gone are remembered (as before)");
    if (AnswerFinal(1, 0, kReasonNotWriter) != 0 || AnswerFinal(1, 0, kReasonBusy) != 0) Fail("P105b2 f2: a LAND's notWriter / busy are not (as before)");
}
void t_p105b2f2_hold_pre_step()
{
    using namespace coophold;
    if (HoldPreStep(1, 1, 1) != kHpStep) Fail("P105b2 f2: an open row answers the repeat as before");
    if (HoldPreStep(0, 1, 1) != kHpRecord) Fail("P105b2 f2 H3: a HOLD after its hold ended is answered from the record");
    if (HoldPreStep(0, 0, 1) != kHpLandFirst) Fail("P105b2 f2 H4: a HOLD after its LAND was refused is refused, nothing taken");
    if (HoldPreStep(0, 0, 0) != kHpStep) Fail("P105b2 f2: a new HOLD takes the ordinary step");
}
void t_p105b2f2_scenario_land_back_overtakes_hold()
{
    /* the asker's edge sends a LAND back for a HOLD not answered yet; the LAND reaches the writer first (refused gone - no row), then
       the HOLD, then a duplicate of the HOLD - walked through the real rules on both sides */
    using namespace coophold;
    int landRefusedRec = 0, holdRec = 0, taken = 0, recordAnswers = 0;
    if (AnswerFinal(1, 0, kReasonGone) == 1) landRefusedRec = 1;
    for (int k = 0; k < 2; ++k)
    {
        const int pre = HoldPreStep(0, holdRec, landRefusedRec);
        if (pre == kHpRecord) { ++recordAnswers; continue; }
        if (pre == kHpLandFirst) { if (AnswerFinal(0, 0, kReasonGone) == 1) holdRec = 1; continue; }
        if (HoldServeStep(0, 1, 1, 0, 0) == kHsTake) ++taken;
    }
    if (taken != 0) Fail("P105b2 f2 H4 scenario: the writer never takes the item for a HOLD its LAND overtook");
    if (holdRec != 1 || recordAnswers != 1) Fail("P105b2 f2 H3 scenario: the duplicate HOLD is answered from the record");
    if (HoldLateAnswer(0, 0, 1, 1) != kLaSwallow) Fail("P105b2 f2 scenario: the asker swallows the refusal (its row is done)");
}
void t_p105b2_scenario_reask_once()
{
    using namespace coophold;
    int granted = 0, repeats = 0, haveRow = 0;
    for (int ask = 0; ask < 3; ++ask)
    {
        const int s = HoldServeStep(haveRow, 1, 1, 0, 0);
        if (s == kHsTake) { ++granted; haveRow = 1; }
        else if (s == kHsRepeat) ++repeats;
    }
    if (granted != 1 || repeats != 2) Fail("P105b2: three asks of one hold - granted once, two repeats answered from the row");
    if (coopgshow::RowDue(1, 10001u, 10000u, 0) != coopgshow::kRdSend) Fail("P105b2: no answer in 10 s - asked again with the same id");
    if (coopgshow::RowDue(0, 0u, 10000u, 1) != coopgshow::kRdWait) Fail("P105b2: a busy answer waits for its re-ask time");
}
/* P105 build 3: an item this game's own player lifts out of a container this game writes (ownpick.h) - every case calls the header
   items.cpp calls */
void t_p105b3_publish_at_pickup()
{
    using namespace ownpick;
    if (PublishAtPickup(1, 0) != 1) Fail("P105b3: out of a container this game writes - the removal goes out at the pickup");
    if (PublishAtPickup(0, 0) != 0) Fail("P105b3: out of a container another game writes and no HOLD - nothing published here");
    if (PublishAtPickup(0, 1) != 0) Fail("P105b3: a HOLD went to the writer - the writer publishes, not this game");
    if (PublishAtPickup(1, 1) != 0) Fail("P105b3: never both a HOLD and an own publication for one pickup");
}
void t_p105b3_deferred_remove()
{
    using namespace ownpick;
    if (DeferredRemove(1, 1) != kDrSkip) Fail("P105b3: sent at the pickup - the drop sends no second removal");
    if (DeferredRemove(1, 0) != kDrSkip) Fail("P105b3: sent at the pickup while this game wrote it - nothing more, whoever writes it now");
    if (DeferredRemove(0, 1) != kDrPublish) Fail("P105b3: not sent at the pickup (the send failed) - the drop sends it");
    if (DeferredRemove(0, 0) != kDrNone) Fail("P105b3: another game's container, never sent - nothing to publish here");
}
void t_p105b3_back_add_after_give()
{
    using namespace ownpick;
    if (BackAddAfterGive(1, 1, 1) != 1) Fail("P105b3: a give refused before its request left the item back in our container - its add is published");
    if (BackAddAfterGive(1, 1, 0) != 0) Fail("P105b3: the item left with the request (lifted into escrow) - the answer's road publishes it");
    if (BackAddAfterGive(1, 0, 1) != 0) Fail("P105b3: the removal was never sent - the other games still show it, nothing to put back");
    if (BackAddAfterGive(0, 1, 1) != 0) Fail("P105b3: a take is never this road's");
}
void t_p105b3_split_count_publish()
{
    using namespace ownpick;
    if (SplitCountPublish(1, 1, 4) != 1) Fail("P105b3: units off our own stack - its new count is published");
    if (SplitCountPublish(1, 0, 4) != 0) Fail("P105b3: the stack is no longer the object in its square - its own moves speak for it");
    if (SplitCountPublish(1, 1, 0) != 0) Fail("P105b3: an unread count is never published");
    if (SplitCountPublish(0, 1, 4) != 0) Fail("P105b3: another game's stack - its writer publishes it");
}
/* the ordinary carries walked through the rules in the order the drain asks them: what each one sends of the item's removal and add.
   removals / adds count what goes on the wire about the item for its container (a give's own request road is not counted here). */
void t_p105b3_scenario_carries()
{
    using namespace ownpick;
    struct Carry { const char* name; int sendOk; int dropKind; int stillHere; int removals; int adds; };
    /* dropKind: 0 put down in one of our own containers (or put back by the engine: an ADD rings), 1 the ground or destroyed (no ADD),
       2 a give whose request went out (the item left with it), 3 a give refused before its request (back in our container) */
    const Carry cs[] = {
        { "move inside our own box", 1, 0, 0, 1, 1 },
        { "put back by escape", 1, 0, 0, 1, 1 },
        { "dropped on the ground", 1, 1, 0, 1, 0 },
        { "given, request sent", 1, 2, 0, 1, 0 },
        { "given, refused before the request", 1, 3, 1, 1, 1 },
        { "pickup send failed, put down in our box", 0, 0, 0, 1, 1 },
    };
    for (size_t i = 0; i < sizeof(cs) / sizeof(cs[0]); ++i)
    {
        const Carry& c = cs[i];
        int removals = 0, adds = 0;
        const int pub = (PublishAtPickup(1, 0) != 0 && c.sendOk != 0) ? 1 : 0;
        if (pub != 0) ++removals;
        if (c.dropKind == 0 || c.dropKind == 1)
        {
            if (DeferredRemove(pub, 1) == kDrPublish) ++removals;
            if (c.dropKind == 0) ++adds;   /* the ADD the engine rang publishes as an ordinary move */
        }
        else if (BackAddAfterGive(1, pub, c.stillHere) != 0) ++adds;
        if (removals != c.removals || adds != c.adds) Fail(std::string("P105b3 scenario: ") + c.name + " - the removal goes out once and the add only when the item is back");
    }
}
/* units lifted off a stack this game writes, at the drop (ownpick::SplitDropRoad - the header items.cpp's ItOwnSplitDrop calls) */
void t_p105b3_split_drop_road()
{
    using namespace ownpick;
    if (SplitDropRoad(1, 0, 1) != kSdGive) Fail("P105b3: a unit off our own stack into another game's container - the give road, never an unpaired drop");
    if (SplitDropRoad(1, 1, 1) != kSdOwn) Fail("P105b3: into one of our own containers - an ordinary move of ours");
    if (SplitDropRoad(0, 1, 1) != kSdOwn) Fail("P105b3: into our own container, whoever writes the stack now - published as ours");
    if (SplitDropRoad(0, 0, 1) != kSdKept) Fail("P105b3: the stack is no longer this game's - nobody to give from");
    if (SplitDropRoad(1, 0, 0) != kSdKept) Fail("P105b3: a container that cannot be named - nobody can be asked");
}
/* a whole item of ours given onto a matching stack in another game's box (design 15.3; ItRevert's put-back calls this header) */
void t_p105b3_merged_give_split()
{
    using namespace ownpick;
    int restore = -1;
    if (MergedGiveSplit(1, 5, 15, &restore) != 1 || restore != 10) Fail("P105b3: 5 dropped onto their 10 - only our 5 come back, their 10 are rebuilt");
    if (MergedGiveSplit(0, 5, 15, &restore) != 0 || restore != 0) Fail("P105b3: no merge seen - the whole object goes back");
    if (MergedGiveSplit(1, 5, 5, &restore) != 0 || restore != 0) Fail("P105b3: the count shows no merge (a swap's shape) - the whole object goes back");
    if (MergedGiveSplit(1, 0, 15, &restore) != 0) Fail("P105b3: the carried count is unknown - never split on a guess");
    if (MergedGiveSplit(1, 5, 4, &restore) != 0) Fail("P105b3: fewer than we carried (the trade clamp) - nothing to rebuild");
    /* every ending: our box and their box on this game, against the writer's truth (their 10, our 5 until a yes) */
    int ours = 0, theirs = 0;
    const int lifted = 5, now = 15;
    if (MergedGiveSplit(1, lifted, now, &restore) != 0) { ours = lifted; theirs = restore; } else { ours = now; theirs = 0; }
    if (ours != 5 || theirs != 10 || ours + theirs != 15) Fail("P105b3 merged give: after the put-back nothing exists twice and nothing is lost");
}
/* T-1079: a move made without the cursor (the right-click; rcgive.h - the header items.cpp's ItRcGiveDrainPass / ItRcGiveRun call). Each
   case folds the destination changes the engine rings for it (decomp_713150: one unit per call; a stack copies a unit and lowers the
   source with a raw write, the last unit leaves as itself) and asks what happens to the units. */
static int RcFold(const int* ops, const int* qty, const int* before, const unsigned long long* obj, int n, rcgive::Piece* pc, int* count)
{
    int uncounted = 0;
    *count = 0;
    for (int i = 0; i < n; ++i)
        if (rcgive::AddEntry(pc, count, 4, obj[i], ops[i], qty[i], before[i], 0) == 0) ++uncounted;
    return uncounted;
}
/* a shift-click of a whole stack of `q` into an empty part of a container: a copied unit lands (op 0), the next copies and finally
   the source object itself are added onto it (op 2) */
static int RcWholeStack(int q, int* ops, int* qty, int* before, unsigned long long* obj)
{
    ops[0] = 0; qty[0] = 1; before[0] = 0; obj[0] = 0xC1;
    for (int i = 1; i < q; ++i) { ops[i] = 2; before[i] = i; qty[i] = i + 1; obj[i] = 0xC1; }
    int units = 0;
    for (int k = q; k >= 1; --k) units += rcgive::UnitsMoved(k, k > 1 ? 1 : 0, k > 1 ? k - 1 : -1);
    return units;
}
void t_t1079_units_moved()
{
    using namespace rcgive;
    if (UnitsMoved(9, 1, 8) != 1) Fail("T-1079: one unit off a stack of 9 - 1 moved");
    if (UnitsMoved(1, 0, -1) != 1) Fail("T-1079: the last unit left as itself - 1 moved");
    if (UnitsMoved(5, 1, 5) != 0) Fail("T-1079: the stack did not change - nothing moved");
    if (UnitsMoved(5, 1, -1) != 0) Fail("T-1079: an unread count after the call - nothing counted");
    if (UnitsMoved(0, 0, -1) != 0) Fail("T-1079: no count before the call - nothing counted");
}
void t_t1079_applies()
{
    using namespace rcgive;
    if (Applies(1, 0, 1, 0) != 1) Fail("T-1079: our character and a container another game writes - this road");
    if (Applies(1, 1, 1, 0) != 0) Fail("T-1079: our own box (or our own character) - everything as before");
    if (Applies(1, 0, 0, 0) != 0) Fail("T-1079: a container nobody can be asked about - everything as before");
    if (Applies(1, 0, 1, 1) != 0) Fail("T-1079: a shop's stock - the trade intercept's road, exactly as before");
    if (Applies(0, 0, 1, 0) != 0) Fail("T-1079: our character is no longer this game's - not ours to give or take");
    if (ChangeIsTheClicks(1, 0) != 0) Fail("T-1079: another item's change in the window is not the click's - left as it was");
    if (ChangeIsTheClicks(1, 1) != 1 || ChangeIsTheClicks(0, 0) != 1) Fail("T-1079: the clicked item's change (or one not read) belongs to the click");
}
void t_t1079_give_whole_stack()
{
    using namespace rcgive;
    int ops[9], qty[9], before[9]; unsigned long long obj[9];
    const int units = RcWholeStack(9, ops, qty, before, obj);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 9, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (units != 9 || un != 0 || count != 1 || pc[0].gain != 9) Fail("T-1079 give whole stack: 9 lost, one object gained 9");
    if (Decide(units, 9, un, pc, count, same) != kPair) Fail("T-1079 give whole stack: paired - the box's writer is asked for 9");
    if (PieceRoad(1, 1, 9, 9) != kPrWhole) Fail("T-1079 give whole stack: the object holds only our 9 - it comes back whole before the ask");
}
void t_t1079_give_part_stack()
{
    using namespace rcgive;
    const int ops[3] = { 0, 2, 2 }, qty[3] = { 1, 2, 3 }, before[3] = { 0, 1, 2 };
    const unsigned long long obj[3] = { 0xC1, 0xC1, 0xC1 };
    const int units = UnitsMoved(10, 1, 9) + UnitsMoved(9, 1, 8) + UnitsMoved(8, 1, 7);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 3, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(units, 3, un, pc, count, same) != kPair || pc[0].gain != 3) Fail("T-1079 give part stack: 3 of 10 - a give of 3");
    if (PieceRoad(1, 0, 3, 3) != kPrSplit) Fail("T-1079 give part stack: the units go back onto our stack before the ask");
}
void t_t1079_give_onto_stack()
{
    using namespace rcgive;
    const int ops[4] = { 2, 2, 2, 2 }, qty[4] = { 7, 8, 9, 10 }, before[4] = { 6, 7, 8, 9 };
    const unsigned long long obj[4] = { 0xB0, 0xB0, 0xB0, 0xB0 };
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 4, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(4, 4, un, pc, count, same) != kPair || pc[0].gain != 4) Fail("T-1079 give onto a stack: their 6 + our 4 - a give of 4");
    if (PieceRoad(1, 1, 10, 4) != kPrMergedGive) Fail("T-1079 give onto a stack, our stack gone: only our 4 come back, their 6 are rebuilt");
    int restore = 0;
    if (ownpick::MergedGiveSplit(1, 4, 10, &restore) != 1 || restore != 6) Fail("T-1079 give onto a stack: the put-back leaves their 6");
    if (PieceRoad(1, 0, 10, 4) != kPrSplit) Fail("T-1079 give onto a stack, ours still there: the split undo lowers theirs by 4");
}
void t_t1079_give_spread()
{
    using namespace rcgive;
    const int ops[5] = { 2, 2, 0, 2, 2 }, qty[5] = { 19, 20, 1, 2, 3 }, before[5] = { 18, 19, 0, 1, 2 };
    const unsigned long long obj[5] = { 0xB0, 0xB0, 0xC1, 0xC1, 0xC1 };
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 5, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(5, 5, un, pc, count, same) != kPair || count != 2 || pc[0].gain != 2 || pc[1].gain != 3)
        Fail("T-1079 give spread: 2 onto their stack and 3 as a new object - two asks, 5 in all");
}
void t_t1079_take_whole_stack()
{
    using namespace rcgive;
    /* shift-click on their stack of 6 onto an empty part of our character: our new object ends with all 6, theirs is gone */
    int ops[6], qty[6], before[6]; unsigned long long obj[6];
    const int units = RcWholeStack(6, ops, qty, before, obj);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 6, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(units, 6, un, pc, count, same) != kPair) Fail("T-1079 take whole stack: 6 lost there, 6 gained here - the writer is asked");
    if (PieceRoad(0, 1, 6, 6) != kPrWhole) Fail("T-1079 take whole stack: our object holds only their 6 - it goes back whole into their cell");
}
void t_t1079_take_part_and_onto()
{
    using namespace rcgive;
    /* two single right-clicks off their stack of 5 onto our stack of 3 (theirs stays at 3) */
    const int ops[2] = { 2, 2 }, qty[2] = { 4, 5 }, before[2] = { 3, 4 };
    const unsigned long long obj[2] = { 0xA0, 0xA0 };
    const int units = UnitsMoved(5, 1, 4) + UnitsMoved(4, 1, 3);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 2, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(units, 2, un, pc, count, same) != kPair || pc[0].gain != 2) Fail("T-1079 take part: 2 of their 5 onto our 3 - a take of 2");
    if (PieceRoad(0, 0, 5, 2) != kPrSplit) Fail("T-1079 take part: their stack stays - our stack is lowered by 2 and theirs raised before the ask");
    if (PieceRoad(0, 1, 5, 2) != kPrMergedTake) Fail("T-1079 take onto our stack with their object gone: only the 2 gained come off it and are asked for");
    if (BackTakesWhole(5, 2) != 0 || BackTakesWhole(2, 2) != 1) Fail("T-1079 back: our stack is lowered; an object holding only the units leaves whole");
}
void t_t1079_take_merged_last_unit()
{
    using namespace rcgive;
    /* T1581: a right-click on their last unit (their stack of 1) onto our stack of 3 - the engine merges it and their object is gone */
    const int ops[1] = { 2 }, qty[1] = { 4 }, before[1] = { 3 };
    const unsigned long long obj[1] = { 0xA0 };
    const int units = UnitsMoved(1, 0, -1);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 1, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (Decide(units, 1, un, pc, count, same) != kPair || count != 1 || pc[0].gain != 1)
        Fail("T-1079 merged take: their last unit onto our 3 - paired by record and units, a take of 1");
    if (PieceRoad(0, 1, 4, pc[0].gain) != kPrMergedTake) Fail("T-1079 merged take: sent to the box's writer, never put back unasked");
    int keep = 0, back = 0;
    if (MergedTakeSplit(4, pc[0].gain, &keep, &back) != 1 || keep != 3 || back != 1)
        Fail("T-1079 merged take: the undo leaves our 3 and makes their 1 again in their cell");
    /* units on both sides: ours before the click (3) is what the undo leaves, theirs (1) is what the request carries */
    if (keep + back != 4 || keep != 4 - units || back != units) Fail("T-1079 merged take: no unit made or lost by the undo");
}
void t_t1079_take_merged_shift_click()
{
    using namespace rcgive;
    /* shift-click on their stack of 5 onto our stack of 3: five rises on our object (3 -> 8), their object gone */
    const int ops[5] = { 2, 2, 2, 2, 2 }, qty[5] = { 4, 5, 6, 7, 8 }, before[5] = { 3, 4, 5, 6, 7 };
    const unsigned long long obj[5] = { 0xA0, 0xA0, 0xA0, 0xA0, 0xA0 };
    int units = 0;
    for (int k = 5; k >= 1; --k) units += UnitsMoved(k, k > 1 ? 1 : 0, k > 1 ? k - 1 : -1);
    Piece pc[4]; int count = 0;
    const int un = RcFold(ops, qty, before, obj, 5, pc, &count);
    const int same[4] = { 1, 1, 1, 1 };
    if (units != 5 || Decide(units, 5, un, pc, count, same) != kPair || count != 1 || pc[0].gain != 5)
        Fail("T-1079 merged shift-click: 5 lost there, 5 gained on our stack - a take of 5");
    if (PieceRoad(0, 1, 8, 5) != kPrMergedTake) Fail("T-1079 merged shift-click: only the 5 gained are split off and asked for");
    int keep = 0, back = 0;
    if (MergedTakeSplit(8, 5, &keep, &back) != 1 || keep != 3 || back != 5 || keep + back != 8)
        Fail("T-1079 merged shift-click: the undo leaves our 3 and makes their 5 again - no unit made or lost");
    if (MergedTakeSplit(5, 5, &keep, &back) != 0 || keep != 0 || back != 0)
        Fail("T-1079 merged split: an object holding only the gained units is the whole road, not a split");
    if (MergedTakeSplit(5, 0, &keep, &back) != 0) Fail("T-1079 merged split: nothing gained is nothing to split");
    if (PieceRoad(0, 1, 5, 5) != kPrWhole) Fail("T-1079 take onto an empty cell: the object goes back whole, as before");
}
void t_t1079_drag_merged_take()
{
    using namespace rcgive;
    int keep = 0, back = 0;
    /* T1586: their whole stack of 1 dragged out of their box onto our stack of 3 - the carried object now holds 4 */
    if (DragMergedTake(0, 0, 1, 4, 1, &keep, &back) != 1 || keep != 3 || back != 1 || keep + back != 4)
        Fail("T-1079 drag merged take: our 3 stay, their 1 is made again in their square and asked for - no unit made or lost");
    /* a take with no merge: dropped on an empty square, the carried object holds only what was carried */
    if (DragMergedTake(0, 0, 1, 1, 1, &keep, &back) != 0 || keep != 0 || back != 0)
        Fail("T-1079 drag take, no merge: the old undo moves the object back whole");
    /* a stack that no longer holds more than the gain (it was lowered before the drain read it) */
    if (DragMergedTake(0, 0, 1, 2, 3, &keep, &back) != 0 || DragMergedTake(0, 0, 1, 3, 3, &keep, &back) != 0)
        Fail("T-1079 drag take: a stack holding no more than the carried units is not split");
    if (DragMergedTake(0, 0, 1, 4, 0, &keep, &back) != 0) Fail("T-1079 drag take: nothing carried is nothing to split");
    /* not this road: a give, a split pickup, or another object in the drop square */
    if (DragMergedTake(1, 0, 1, 4, 1, &keep, &back) != 0) Fail("T-1079 drag merged take: a give has its own put-back (mergeRest)");
    if (DragMergedTake(0, 1, 1, 4, 1, &keep, &back) != 0) Fail("T-1079 drag merged take: a split pickup has its own undo (ItRevertSplit)");
    if (DragMergedTake(0, 0, 0, 4, 1, &keep, &back) != 0) Fail("T-1079 drag merged take: the square holds another object - not the carried one");
    /* where the carried units go back, and whether the request is sent */
    if (DragTakeBackPlace(1, 0) != kDtOwn || DragTakeBackPlace(1, 1) != kDtOwn || DragTakeAsks(kDtOwn) != 1)
        Fail("T-1079 drag merged take: their own square takes the units - the request names it and is sent");
    if (DragTakeBackPlace(0, 1) != kDtOther || DragTakeAsks(kDtOther) != 0)
        Fail("T-1079 drag merged take: their square was taken during the drag - another free square, nothing asked, the writer re-asked");
    if (DragTakeBackPlace(0, 0) != kDtNone || DragTakeAsks(kDtNone) != 0)
        Fail("T-1079 drag merged take: no free square at all - the move stands, nothing asked, the writer re-asked");
}
void t_t1079_give_merged_source_stays()
{
    using namespace rcgive;
    /* a give of 4 off our stack of 9 that stays, onto their stack of 6: unchanged - the split undo lowers theirs by 4 */
    if (PieceRoad(1, 0, 10, 4) != kPrSplit) Fail("T-1079 merged give, our stack stays: the split road as before");
    if (PieceRoad(1, 1, 10, 4) != kPrMergedGive) Fail("T-1079 merged give, our stack gone: the merged-give road as before");
    if (PieceRoad(0, 0, 10, 4) != kPrSplit) Fail("T-1079 merged take, their stack stays: the split road as before");
}
void t_t1079_mismatch_back()
{
    using namespace rcgive;
    const int same[4] = { 1, 1, 1, 1 };
    const int diff[4] = { 0, 1, 1, 1 };
    Piece pc[4]; int count = 0;
    {   /* one change did not reach the ring: 8 gained, 9 lost */
        int ops[9], qty[9], before[9]; unsigned long long obj[9];
        RcWholeStack(9, ops, qty, before, obj);
        const int un = RcFold(ops, qty, before, obj, 8, pc, &count);
        if (Decide(9, 8, un, pc, count, same) != kBack) Fail("T-1079 mismatch: 9 lost, 8 gained - back to the source, never an ask for 8");
    }
    {   /* the destination object carries another record */
        const int ops[1] = { 0 }, qty[1] = { 1 }, before[1] = { 0 };
        const unsigned long long obj[1] = { 0xC1 };
        const int un = RcFold(ops, qty, before, obj, 1, pc, &count);
        if (Decide(1, 1, un, pc, count, diff) != kBack) Fail("T-1079 mismatch: another item's record - back to the source");
    }
    {   /* an op 2 whose count before was not read */
        const int ops[2] = { 0, 2 }, qty[2] = { 1, 2 }, before[2] = { 0, 0 };
        const unsigned long long obj[2] = { 0xC1, 0xC1 };
        const int un = RcFold(ops, qty, before, obj, 2, pc, &count);
        if (un != 1 || Decide(2, 2, un, pc, count, same) != kBack) Fail("T-1079 mismatch: an uncounted change - back to the source");
    }
    {   /* something of the clicked item also left the destination inside the window */
        const int ops[2] = { 0, 1 }, qty[2] = { 1, 1 }, before[2] = { 0, 0 };
        const unsigned long long obj[2] = { 0xC1, 0xD1 };
        const int un = RcFold(ops, qty, before, obj, 2, pc, &count);
        if (Decide(1, 2, un, pc, count, same) != kBack) Fail("T-1079 mismatch: a removal in the destination - back to the source");
    }
    {   /* more objects than the pairing holds */
        int ops[5], qty[5], before[5]; unsigned long long obj[5];
        for (int i = 0; i < 5; ++i) { ops[i] = 0; qty[i] = 1; before[i] = 0; obj[i] = 0xC1 + (unsigned long long)i; }
        const int un = RcFold(ops, qty, before, obj, 5, pc, &count);
        if (Decide(5, 5, un, pc, count, same) != kBack) Fail("T-1079 mismatch: five objects - back to the source");
    }
    if (EntryGain(0, 0, 0) != -1 || EntryGain(2, 3, 5) != -1 || EntryGain(3, 1, 0) != -1) Fail("T-1079: an empty add, a fall and a split are not gains");
    if (BackRaises(0, 1) != 1 || BackRaises(1, 0) != 0 || BackRaises(0, 0) != 0) Fail("T-1079 back: raised onto the source stack only while it is in its cell");
}
void t_t1079_not_this_road()
{
    using namespace rcgive;
    const int same[4] = { 1, 1, 1, 1 };
    Piece pc[4];
    pc[0].obj = 0; pc[0].gain = 0;
    if (Decide(3, 0, 0, pc, 0, same) != kNone) Fail("T-1079: no change of the clicked item in the destination - everything as before");
    if (RefuseUnrecorded(1, -1) != 1) Fail("T-1079: a click with no record row left is refused before anything moves");
    if (RefuseUnrecorded(1, 0) != 0 || RefuseUnrecorded(0, -1) != 0) Fail("T-1079: a recorded click, or one not this road's, runs");
}
/* T-1079 fold 2: the back road's one rule - units returned == units taken out of the destination, per object and in all - and the
   guards around it (rcgive.h; the header ItRcMoveBack / ItRcGiveBack / ItRcGiveRun call). */
static int RcBackRow(const int* now, const int* gain, const int* takeOk, int pieces, int lost, int* takenOut)
{
    int lacking = lost, returned = 0;
    *takenOut = 0;
    for (int p = 0; p < pieces; ++p)
    {
        const int want = rcgive::BackWant(gain[p], lacking);
        const int got = rcgive::BackUnitsTaken(want > 0 ? takeOk[p] : 0, now[p], want);
        if (takeOk[p] != 0 && want > 0) *takenOut += (rcgive::BackTakesWhole(now[p], want) != 0) ? now[p] : want;
        returned += got;
        lacking -= got;
    }
    return returned;
}
void t_t1079_back_conservation()
{
    using namespace rcgive;
    int taken = 0;
    {   /* whole stack: one new object holding all 9 - taken out whole, 9 back */
        const int now[1] = { 9 }, gain[1] = { 9 }, ok[1] = { 1 };
        if (RcBackRow(now, gain, ok, 1, 9, &taken) != 9 || taken != 9) Fail("T-1079 back whole stack: 9 out, 9 back");
    }
    {   /* part stack: 3 of 10 as a new object */
        const int now[1] = { 3 }, gain[1] = { 3 }, ok[1] = { 1 };
        if (RcBackRow(now, gain, ok, 1, 3, &taken) != 3 || taken != 3) Fail("T-1079 back part stack: 3 out, 3 back");
    }
    {   /* onto their stack of 6: lowered by our 4, their 6 stay */
        const int now[1] = { 10 }, gain[1] = { 4 }, ok[1] = { 1 };
        if (RcBackRow(now, gain, ok, 1, 4, &taken) != 4 || taken != 4) Fail("T-1079 back onto a stack: 4 out of their 10, 4 back");
    }
    {   /* spread: 2 onto their stack, 3 as a new object */
        const int now[2] = { 20, 3 }, gain[2] = { 2, 3 }, ok[2] = { 1, 1 };
        if (RcBackRow(now, gain, ok, 2, 5, &taken) != 5 || taken != 5) Fail("T-1079 back spread: 2 + 3 out, 5 back");
    }
    {   /* spread, the second object cannot be taken out: only the 2 come back, the 3 stay in the destination */
        const int now[2] = { 20, 3 }, gain[2] = { 2, 3 }, ok[2] = { 1, 0 };
        if (RcBackRow(now, gain, ok, 2, 5, &taken) != 2 || taken != 2) Fail("T-1079 back spread, one object stuck: 2 out, 2 back - never 5");
    }
    {   /* the destination shows more of the item than the source lost (another change of the same item): never more back than lost */
        const int now[2] = { 4, 6 }, gain[2] = { 4, 6 }, ok[2] = { 1, 1 };
        const int back = RcBackRow(now, gain, ok, 2, 7, &taken);
        if (back != 7 || taken != 7) Fail("T-1079 back over-gain: 7 lost, 7 out, 7 back");
    }
    {   /* fewer listed than lost (a change missed): only what was listed and taken out comes back */
        const int now[1] = { 8 }, gain[1] = { 8 }, ok[1] = { 1 };
        if (RcBackRow(now, gain, ok, 1, 9, &taken) != 8 || taken != 8) Fail("T-1079 back under-listed: 8 out, 8 back, 1 left where it is");
    }
    if (BackUnitsTaken(0, 9, 9) != 0) Fail("T-1079 back: nothing taken out, nothing returned");
    if (BackWant(5, 3) != 3 || BackWant(2, 3) != 2 || BackWant(4, 0) != 0) Fail("T-1079 back: an object is asked for its gain, never more than the source lacks");
}
void t_t1079_guards()
{
    using namespace rcgive;
    if (RowFull(31, 32) != 0 || RowFull(32, 32) != 1) Fail("T-1079: a row closes at its call cap - every destination object stays listable");
    if (RefuseOffMain(1, 0) != 1 || RefuseOffMain(1, 1) != 0 || RefuseOffMain(0, 0) != 0) Fail("T-1079: a click of this road off the main thread is refused");
    if (FlushSourceOnDrop(1, 1) != 1 || FlushSourceOnDrop(0, 1) != 0 || FlushSourceOnDrop(1, 0) != 0)
        Fail("T-1079: a dropped record publishes the source count only when this game writes it and the stack is in its cell");
    if (PublishOriginalRemoval(1, 1, 1) != 1) Fail("T-1079: a whole-stack give that fell back publishes the original's removal");
    if (PublishOriginalRemoval(1, 0, 1) != 0 || PublishOriginalRemoval(0, 1, 1) != 0 || PublishOriginalRemoval(1, 1, 0) != 0)
        Fail("T-1079: no removal published for a stack that stays, a take, or a give that did not fall back");
    /* the whole-stack give that falls back, as the other games see it: the original (9) is removed once, what came back (9) is added -
       9 there, as here; without the removal they would hold 18 */
    const int others = 9 - ((PublishOriginalRemoval(1, 1, 1) != 0) ? 9 : 0) + 9;
    if (others != 9) Fail("T-1079: the other games end with the count this game has");
}
/* T-1079 fold 3 / 4, THE NET (rcgive.h - the functions items.cpp's ItRcNetPass, ItRcGiveJobBuild and ItRcGiveBack call). Containers:
   1 = our character, 2 = their box, 3 = a second container. rel: the object the engine's call tied to the change. */
static rcgive::NetChange NetC(int side, int container, unsigned long long obj, int op, int quantity, int qtyBefore, unsigned long long rel)
{
    rcgive::NetChange c;
    c.side = side; c.container = container; c.obj = obj; c.rel = rel; c.op = op; c.quantity = quantity; c.qtyBefore = qtyBefore; c.inPack = 0;
    return c;
}
void t_t1079_net_split_take()
{
    using namespace rcgive;
    /* T1558: 1 taken off their stack of 2 (op 3 on the stack, which stays, handing back 0xB0) lands on our character as 0xB0 (op 0, x1) */
    const NetChange c[2] = { NetC(kSideTheirs, 2, 0xA0, 3, 1, 0, 0xB0), NetC(kSideOurs, 1, 0xB0, 0, 1, 0, 0) };
    NetPlan p;
    if (NetDecide(c, 2, 32, &p) != kNetPair) { Fail("T-1079 net: T1558's split take pairs"); return; }
    if (p.give != 0 || p.units != 1 || p.gone != 0 || p.srcObj != 0xA0 || p.src != 0) Fail("T-1079 net: a take of 1 off their stack that stays");
    /* the job's roles: the split is the source's change, the landing a destination change of the record */
    if (JobRole(1, 0, 1, 3, 1, 1, 0, 0, 1, 1) != kJobSource || JobRole(1, 0, 1, 0, 0, 0, 1, 0, 1, 1) != kJobDest)
        Fail("T-1079 net: the job takes the split as the source and the landing as the destination");
    Piece pc[4]; int count = 0;
    AddEntry(pc, &count, 4, 0xB0, 0, 1, 0, 0);
    const int same[1] = { 1 };
    if (Decide(p.units, 1, 0, pc, count, same) != kPair || PieceRoad(p.give, p.gone, 1, 1) != kPrSplit)
        Fail("T-1079 net: the split take goes the request road, undone onto their stack");
}
void t_t1079_net_merge_give()
{
    using namespace rcgive;
    NetPlan p;
    /* the report: our whole object of 6 (op 1) added onto their stack of 4 (op 2, 4 -> 10, merging 0xC0 in) */
    const NetChange c[2] = { NetC(kSideOurs, 1, 0xC0, 1, 6, 0, 0), NetC(kSideTheirs, 2, 0xD0, 2, 10, 4, 0xC0) };
    if (NetDecide(c, 2, 32, &p) != kNetPair || p.give != 1 || p.units != 6 || p.gone != 1) { Fail("T-1079 net: a give onto their stack pairs"); return; }
    if (PieceRoad(p.give, p.gone, 10, 6) != kPrMergedGive) Fail("T-1079 net: only our 6 come back off their stack of 10 for the request");
    /* 2 split off our stack of 9 (op 3 handing back 0xC2, the stack stays), 0xC2 merged onto their stack (op 2, 4 -> 6) */
    const NetChange c2[2] = { NetC(kSideOurs, 1, 0xC1, 3, 2, 0, 0xC2), NetC(kSideTheirs, 2, 0xD0, 2, 6, 4, 0xC2) };
    if (NetDecide(c2, 2, 32, &p) != kNetPair || p.give != 1 || p.units != 2 || p.gone != 0 || PieceRoad(1, 0, 6, 2) != kPrSplit)
        Fail("T-1079 net: units split off our stack onto theirs pair and go back onto our stack for the request");
    /* the same split object spread over their two stacks (+2, +4) */
    const NetChange c3[3] = { NetC(kSideOurs, 1, 0xC1, 3, 6, 0, 0xC2), NetC(kSideTheirs, 2, 0xD0, 2, 6, 4, 0xC2), NetC(kSideTheirs, 2, 0xD1, 2, 9, 5, 0xC2) };
    if (NetDecide(c3, 3, 32, &p) != kNetPair || p.units != 6) Fail("T-1079 net: a give spread over two of their stacks pairs");
}
void t_t1079_net_not_paired()
{
    using namespace rcgive;
    NetPlan p;
    /* THE GUARD: our character eats 1 (an op 2 fall, no object handed on) while their box gains 1 of the record from elsewhere */
    const NetChange e1[2] = { NetC(kSideOurs, 1, 0xE0, 2, 3, 4, 0), NetC(kSideTheirs, 2, 0xF9, 0, 1, 0, 0) };
    const NetChange e2[2] = { NetC(kSideOurs, 1, 0xE0, 2, 3, 4, 0), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xF8) };
    const NetChange e3[2] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xF8) };
    const NetChange e4[2] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0) };
    if (NetDecide(e1, 2, 32, &p) != kNetUnlinked || NetDecide(e2, 2, 32, &p) != kNetUnlinked || NetDecide(e3, 2, 32, &p) != kNetUnlinked
        || NetDecide(e4, 2, 32, &p) != kNetUnlinked)
        Fail("T-1079 net: a gain the engine's calls do not tie to our loss is never paired");
    /* two of our characters each split 1 off and their box gained it: either could be the giver */
    const NetChange a[3] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideOurs, 3, 0xE1, 3, 1, 0, 0xE6), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xE5) };
    if (NetDecide(a, 3, 32, &p) != kNetAmbiguous) Fail("T-1079 net: two candidates of one record on our side are ambiguous");
    const NetChange b[3] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xE5), NetC(kSideTheirs, 3, 0xF1, 0, 1, 0, 0) };
    if (NetDecide(b, 3, 32, &p) != kNetAmbiguous) Fail("T-1079 net: two candidates on their side are ambiguous");
    const NetChange o[3] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideOursOther, 3, 0xE2, 0, 1, 0, 0), NetC(kSideTheirs, 2, 0xF0, 3, 2, 0, 0xF5) };
    if (NetDecide(o, 3, 32, &p) != kNetAmbiguous) Fail("T-1079 net: our box and our character together are ambiguous");
    /* counts that differ, a gain on both sides, losses off two objects, a change in a pack */
    const NetChange u1[2] = { NetC(kSideOurs, 1, 0xE0, 3, 2, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xE5) };
    const NetChange u2[2] = { NetC(kSideOurs, 1, 0xE0, 0, 1, 0, 0), NetC(kSideTheirs, 2, 0xF0, 0, 1, 0, 0) };
    const NetChange u3[3] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideOurs, 1, 0xE3, 1, 1, 0, 0), NetC(kSideTheirs, 2, 0xF0, 2, 6, 4, 0xE5) };
    NetChange u4[2] = { NetC(kSideOurs, 1, 0xE0, 3, 1, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0xE5) };
    u4[1].inPack = 1;
    if (NetDecide(u1, 2, 32, &p) != kNetUnbalanced || NetDecide(u2, 2, 32, &p) != kNetUnbalanced || NetDecide(u3, 3, 32, &p) != kNetUnbalanced
        || NetDecide(u4, 2, 32, &p) != kNetUnbalanced)
        Fail("T-1079 net: a shape that is not one move of equal units is not paired");
    const NetChange l1[1] = { NetC(kSideTheirs, 2, 0xF0, 2, 5, 4, 0) };
    const NetChange l2[1] = { NetC(kSideOurs, 1, 0xE0, 2, 3, 4, 0) };
    if (NetDecide(l1, 1, 32, &p) != kNetNone || NetDecide(l2, 1, 32, &p) != kNetNone) Fail("T-1079 net: a one-sided change is today's road");
    const NetChange h[2] = { NetC(kSideOursOther, 3, 0xE2, 0, 1, 0, 0), NetC(kSideTheirs, 2, 0xF0, 3, 1, 0, 0xE2) };
    if (NetDecide(h, 2, 32, &p) != kNetNotCharacter) Fail("T-1079 net: a box of ours is not the net's road");
    const NetChange m[4] = { NetC(kSideOurs, 1, 0xE0, 3, 3, 0, 0xE5), NetC(kSideTheirs, 2, 0xF0, 2, 2, 1, 0xE5), NetC(kSideTheirs, 2, 0xF1, 2, 2, 1, 0xE5),
                             NetC(kSideTheirs, 2, 0xF2, 2, 2, 1, 0xE5) };
    if (NetDecide(m, 4, 2, &p) != kNetTooMany || NetDecide(m, 4, 3, &p) != kNetPair) Fail("T-1079 net: the job's object cap is respected");
    if (NetLoss(2, 3, 0) != -1 || NetLoss(2, 5, 4) != -1 || NetLoss(2, 3, 4) != 1 || NetLoss(0, 3, 0) != -1)
        Fail("T-1079 net: a fall needs the count before it; a rise or an add is no loss");
}
void t_t1079_net_grouping()
{
    using namespace rcgive;
    /* sides and the skip list as the drain asks them */
    if (NetSideOf(1, 0, 1, 0, 0, 0) != kSideTheirs || NetSideOf(1, 0, 0, 1, 0, 0) != kSideOursOther || NetSideOf(1, 0, 0, 0, 0, 0) != -1)
        Fail("T-1079 net: a box's side is its area verdict");
    if (NetSideOf(0, 1, 0, 0, 7, 1) != kSideOurs || NetSideOf(0, 1, 0, 0, 7, 0) != kSideTheirs || NetSideOf(0, 1, 0, 0, 0, 1) != -1 || NetSideOf(0, 0, 0, 0, 7, 1) != -1)
        Fail("T-1079 net: a character's side is its uid; no uid, or a pack, is not judged");
    for (int b = 0; b < 8; ++b)
    {
        const int f[8] = { b == 0, b == 1, b == 2, b == 3, b == 4, b == 5, b == 6, b == 7 };
        if (NetSkip(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]) != 1) Fail("T-1079 net: every claim of another road keeps a change out of the net");
    }
    if (NetSkip(0, 0, 0, 0, 0, 0, 0, 0) != 0) Fail("T-1079 net: an unclaimed, named change is the net's");
    /* six changes as the drain numbers them: record 0 (k 0, 2, 5), record 1 (k 1, 4), one not live (k 3, record 0) */
    NetIn in[6];
    const int live[6] = { 1, 1, 1, 0, 1, 1 }, kind[6] = { 0, 1, 0, 0, 1, 0 }, side[6] = { 0, 1, 1, 1, 0, 1 }, cont[6] = { 0, 1, 2, 2, 0, 2 };
    for (int k = 0; k < 6; ++k)
    {
        in[k].live = live[k]; in[k].kind = kind[k]; in[k].side = side[k]; in[k].container = cont[k];
        in[k].obj = 0x100 + k; in[k].rel = 0; in[k].op = 2; in[k].quantity = 1; in[k].qtyBefore = 2; in[k].inPack = 0;
    }
    int done[6] = { 0, 0, 0, 0, 0, 0 }, grp[4], over = 0;
    NetChange out[4];
    const int g0 = NetGroup(in, 6, 0, done, grp, out, 4, &over);
    if (g0 != 3 || grp[0] != 0 || grp[1] != 2 || grp[2] != 5 || over != 0 || done[3] != 0 || done[1] != 0 || out[1].container != 2 || out[1].side != 1)
        Fail("T-1079 net: a group is every live change of one record, in drain order");
    const int g1 = NetGroup(in, 6, 1, done, grp, out, 4, &over);
    if (g1 != 2 || grp[0] != 1 || grp[1] != 4) Fail("T-1079 net: the next record's group");
    int done2[6] = { 0, 0, 0, 0, 0, 0 };
    if (NetGroup(in, 6, 0, done2, grp, out, 2, &over) != 2 || over != 1 || done2[5] != 1) Fail("T-1079 net: a group past its cap says so and is all marked");
}
void t_t1079_net_job_role()
{
    using namespace rcgive;
    /* handled by another road, or outside the window: nothing */
    if (JobRole(1, 1, 1, 0, 0, 0, 1, 0, 1, 1) != kJobNone || JobRole(1, 0, 0, 0, 0, 0, 1, 0, 1, 1) != kJobNone) Fail("T-1079 job: claimed or outside: nothing");
    /* a click's source is only the clicked object's op 1; a net row's is every change of its source object */
    if (JobRole(0, 0, 1, 3, 1, 1, 0, 0, 1, 1) != kJobNone || JobRole(0, 0, 1, 1, 1, 1, 0, 0, 1, 1) != kJobSource
        || JobRole(1, 0, 1, 2, 1, 1, 0, 0, 1, 1) != kJobSource)
        Fail("T-1079 job: the source's changes");
    /* a destination change of an unread record: a click lists it (read live with its piece), a net row does not */
    if (JobRole(0, 0, 1, 0, 0, 0, 1, 0, 0, 0) != kJobDest || JobRole(1, 0, 1, 0, 0, 0, 1, 0, 0, 0) != kJobNone)
        Fail("T-1079 job: an unread record - the click's, never the net's");
    if (JobRole(0, 0, 1, 0, 0, 0, 1, 0, 1, 0) != kJobNone || JobRole(1, 0, 1, 0, 0, 0, 1, 1, 0, 0) != kJobDest)
        Fail("T-1079 job: another record is left; an object already listed stays the row's");
}
struct NetBackFake { const int* now; const int* takenOut; int calls; };
static int NetBackFakeMove(void* ctx, int p, int want)
{
    NetBackFake* f = (NetBackFake*)ctx;
    ++f->calls;
    return rcgive::BackUnitsTaken(f->takenOut[p], f->now[p], want);
}
void t_t1079_net_refusal_units()
{
    using namespace rcgive;
    /* a net give of 6 spread +2 / +4 over their two stacks (10 and 9 now) taking the back road: exactly 6 back, nothing doubled */
    const int gain[2] = { 2, 4 }, ok[2] = { 1, 1 }, now[2] = { 10, 9 }, taken[2] = { 1, 1 };
    NetBackFake f; f.now = now; f.takenOut = taken; f.calls = 0;
    if (BackRun(6, gain, ok, 2, NetBackFakeMove, &f) != 0 || f.calls != 2) Fail("T-1079 net: the back road returns exactly the units the source lost");
    /* a piece that cannot be taken out gives nothing back: its units stay where they are and are counted as lacking, never invented */
    const int taken2[2] = { 1, 0 };
    NetBackFake f2; f2.now = now; f2.takenOut = taken2; f2.calls = 0;
    if (BackRun(6, gain, ok, 2, NetBackFakeMove, &f2) != 4) Fail("T-1079 net: a piece not taken out leaves its units lacking");
    /* a piece not listed at the pass is never asked */
    const int ok3[2] = { 0, 1 };
    NetBackFake f3; f3.now = now; f3.takenOut = taken; f3.calls = 0;
    if (BackRun(6, gain, ok3, 2, NetBackFakeMove, &f3) != 2 || f3.calls != 1) Fail("T-1079 net: an unlisted piece keeps its old road");
    /* a whole object holding no more than its share comes back whole (3 of a 3), and the next piece is asked only for what still lacks */
    const int gain4[2] = { 3, 4 }, now4[2] = { 3, 9 };
    NetBackFake f4; f4.now = now4; f4.takenOut = taken; f4.calls = 0;
    if (BackRun(5, gain4, ok, 2, NetBackFakeMove, &f4) != 0) Fail("T-1079 net: never more back in all than the source lost");
}
/* one unit off our stack of 10, dropped into a box another game writes, on every ending: what our game, the writer and a third game
   end up holding. `others` is what the other games show of our stack (the last count we published); the give road's put-back and
   escrow lift are silent, and the stack's count is published at the end of every drain that touched it (SplitCountPublish). */
void t_p105b3_scenario_split_give()
{
    using namespace ownpick;
    struct End { const char* name; int sent; int yes; int stackAfter; int boxAfter; };
    const End es[] = {
        { "given, the writer said yes", 1, 1, 9, 1 },
        { "given, the writer refused - the unit back onto the stack", 1, 0, 10, 0 },
        { "refused before the request - the put-back stands", 0, 0, 10, 0 },
    };
    for (size_t i = 0; i < sizeof(es) / sizeof(es[0]); ++i)
    {
        const End& e = es[i];
        int stack = 10, others = 10, box = 0, escrow = 0;
        stack -= 1;                                                          /* the engine's split: the clone on the cursor */
        if (SplitCountPublish(1, 1, stack) != 0) others = stack;             /* the pickup drain's flush */
        if (SplitDropRoad(1, 0, 1) != kSdGive) { Fail("P105b3 split give scenario: the drop must take the give road"); return; }
        stack += 1;                                                          /* ItRevertSplit: the clone off the box, back onto the stack */
        if (e.sent != 0) { stack -= 1; escrow = 1; }                         /* sent: the same unit into escrow, behind the guard */
        if (SplitCountPublish(1, 1, stack) != 0) others = stack;             /* the drop drain's flush, from a live read */
        if (e.sent != 0)
        {
            if (e.yes != 0) { box += 1; escrow = 0; }                        /* the writer adds it (and publishes); the escrow is destroyed */
            else { stack += escrow; escrow = 0; others = stack; }            /* the settle's engine add merges it back - rings, published */
        }
        if (stack != e.stackAfter || box != e.boxAfter || others != stack || escrow != 0 || stack + box != 10)
            Fail(std::string("P105b3 split give scenario: ") + e.name + " - the unit is on exactly one side, the same on every game");
    }
}
/* M7b slice 4 fold 1 (F1): a TALK step from the conversation's peer is the same player whichever road it took */
void t_m7bs4f1_talk_same_player()
{
    const int link = 3;
    const unsigned int raw = 77u;                              /* the session link's raw sender id */
    const unsigned int viaNb = cooplive::RelayPeerId(3);       /* the same player through the notebook */
    if (!cooplive::SamePlayer(raw, viaNb, link)) Fail("a conversation begun on the session link refused its next step through the notebook");
    if (!cooplive::SamePlayer(viaNb, raw, link)) Fail("a conversation begun through the notebook refused its next step on the session link");
    if (cooplive::SamePlayer(raw, cooplive::RelayPeerId(4), link)) Fail("another player's step passed as the conversation's peer");
    if (cooplive::SamePlayer(viaNb, cooplive::RelayPeerId(5), link)) Fail("slot 5 passed as slot 3");
    if (!cooplive::SamePlayer(raw, raw, -1)) Fail("no slot known: the same raw sender was refused");
    if (cooplive::SamePlayer(raw, viaNb, -1)) Fail("no slot known: a relayed sender passed as the raw session peer");
}
/* M7b slice 4 fold 1 (F2): the door's newest-per-publisher check keyed by the player */
void t_m7bs4f1_door_gen_by_player()
{
    const int link = 2;
    const long rawKey = (long)cooplive::PlayerKeyOf(9u, link);
    const long nbKey = (long)cooplive::PlayerKeyOf(cooplive::RelayPeerId(2), link);
    if (rawKey != nbKey) Fail("one publisher on two roads got two keys");
    if (!coopdoor::DoorGenStale(rawKey, 10, nbKey, 8)) Fail("a late older generation was accepted after a road switch");
    if (!coopdoor::DoorGenStale(rawKey, 10, nbKey, 10)) Fail("the same generation was accepted twice");
    if (coopdoor::DoorGenStale(rawKey, 10, nbKey, 11)) Fail("a newer generation was refused");
    const long other = (long)cooplive::PlayerKeyOf(cooplive::RelayPeerId(4), link);
    if (coopdoor::DoorGenStale(rawKey, 10, other, 3)) Fail("another publisher's generation was compared with this one's");
    if (coopdoor::DoorGenStale(rawKey, -1, nbKey, 0)) Fail("nothing held yet, but refused");
}
/* M7b slice 4 fold 1 (F4): three games - this one, the session peer (slot 1), a third on the notebook only (slot 2) */
void t_m7bs4f1_side_road_three_games()
{
    const unsigned int area = cooplive::CharAreaTarget(5, -1);
    if (area == cooplive::kAreaTargetNone) { Fail("test sector 5 has no AREA target"); return; }
    const unsigned int none = cooplive::kAreaTargetNone;
    cooplive::SidePlan sp = cooplive::SideRoadDecide(true, true, false, 1, area);
    if (!sp.session || !sp.live || sp.route != (unsigned int)cooplive::kRouteWorldExcept || sp.target != 1u || !sp.widened)
        Fail("peer not proven, AREA: must go on the session link AND WORLD_EXCEPT slot 1 (the third game hears it, the peer once)");
    sp = cooplive::SideRoadDecide(true, true, false, 1, none);
    if (!sp.session || !sp.live || sp.route != (unsigned int)cooplive::kRouteWorldExcept || sp.target != 1u || sp.widened)
        Fail("peer not proven, WORLD: must go on the session link AND WORLD_EXCEPT slot 1");
    sp = cooplive::SideRoadDecide(true, true, true, 1, area);
    if (sp.session || !sp.live || sp.route != (unsigned int)cooplive::kRouteArea || sp.target != area)
        Fail("peer proven, AREA: the notebook alone, AREA");
    sp = cooplive::SideRoadDecide(true, true, true, 1, none);
    if (sp.session || !sp.live || sp.route != (unsigned int)cooplive::kRouteWorld || sp.target != 0u)
        Fail("peer proven, WORLD: the notebook alone, WORLD");
    sp = cooplive::SideRoadDecide(true, false, false, -1, area);
    if (sp.session || !sp.live || sp.route != (unsigned int)cooplive::kRouteArea) Fail("no session link: the notebook alone, AREA");
    sp = cooplive::SideRoadDecide(false, true, false, 1, area);
    if (!sp.session || sp.live) Fail("notebook down: the session link alone");
    sp = cooplive::SideRoadDecide(true, true, false, -1, area);
    if (!sp.session || sp.live || !sp.slotUnknown) Fail("peer slot unknown: the session link alone (a WORLD copy could reach the peer twice)");
    sp = cooplive::SideRoadDecide(false, false, false, 1, area);
    if (sp.session || sp.live) Fail("no road: nothing");
}
void t_p105g_rest_and_shortfall()
{
    using namespace coopgshow;
    if (GrantRest(10, 3) != 7) Fail("P105g: a partial pickup leaves the rest on the ground");
    if (GrantRest(5, 5) != 0 || GrantRest(4, 5) != 0) Fail("P105g: a whole pickup leaves nothing");
    if (Shortfall(5, 5) != 0 || Shortfall(5, 9) != 0) Fail("P105g: all taken back - no shortfall");
    if (Shortfall(5, 2) != 3) Fail("P105g: the units used before the answer are the shortfall");
}
void t_p105g_f1_repeat_clock()
{
    using namespace coopgshow;
    const unsigned int sent = RepeatSentAt(0u, 9900u, 1);   /* granted at 0 ms; the repeat answered at 9 900 ms */
    if (sent != 9900u) Fail("P105g f1: an answered repeat restarts the kept row's clock");
    if (coopesc::ExpireAction(0, 1, 0, 10500u - sent, 0u, 10000u, coopesc::kEscrowQuietMs) != coopesc::kExpWait) Fail("P105g f1: after the repeat the kept ground row waits for its PLACED");
    if (coopesc::ExpireAction(0, 1, 0, 10500u, 0u, 10000u, coopesc::kEscrowQuietMs) != coopesc::kExpEscrow) Fail("P105g f1/p105g-f2-4: (control) without the restart it leaves its 10 s wait - into escrow, never back on the ground");
    if (RepeatSentAt(5u, 9900u, 0) != 5u) Fail("P105g f1: an unanswered repeat leaves the clock");
    if (RepeatMatch(1, 0, 0) != 1) Fail("P105g f1: the peer it was granted to matches by id");
    if (RepeatMatch(0, 1, 0) != 0) Fail("P105g f1: another peer naming the key, no link edge - no match");
    if (RepeatMatch(0, 1, 1) != 1) Fail("P105g f1: another peer naming the key after a link edge - matched (renumbered)");
    if (RepeatMatch(0, 0, 1) != 0) Fail("P105g f1: another peer not naming the key - never");
}
void t_p105g_f1_ring_room()
{
    using namespace coopgshow;
    if (RingRoom(0, 0, 256) != 1 || RingRoom(255, 0, 256) != 1) Fail("P105g f1: a free slot is room");
    if (RingRoom(255, 1, 256) != 0) Fail("P105g f1: the last free slot already reserved - no room, the pickup is not run");
    if (RingRoom(256, 0, 256) != 0 || RingRoom(250, 6, 256) != 0) Fail("P105g f1: a full queue - no room");
    if (RingRoom(-1, 0, 256) != 0 || RingRoom(0, -1, 256) != 0) Fail("P105g f1: an impossible count - no room");
}
void t_p105g_f1_own_drop_and_cap()
{
    using namespace coopgshow;
    using namespace coopground;
    if (SettleAction(0, kGroundReasonNotFound, 1, 0, 0, 0) != kSgOwnDrop) Fail("P105g f1: our own drop asked after an unanswered PUT, 'no such item' - it stands");
    if (SettleAction(0, kGroundReasonNotFound, 1, 1, 0, 0) != kSgReask) Fail("P105g f1: ... with its PUT row still open - asked again");
    if (CapCounts(kGroundReasonUnreadable) != 1 || CapCounts(0) != 1) Fail("P105g f1: unreadable / unnamed refusals count toward the cap");
    if (CapCounts(kGroundReasonNotHolder) != 0 || CapCounts(kGroundReasonBusy) != 0 || CapCounts(kGroundReasonNotFound) != 0) Fail("P105g f1: not holder / busy / own drop's gone never count");
    if (LeftoverRemove(1, 0, 0) != 0) Fail("P105g f1: a yes after a readable pickup removes nothing lying here");
    if (LeftoverRemove(1, 1, 0) != 1) Fail("P105g f1: a yes after an unreadable after-read removes the copy");
    if (LeftoverRemove(1, 0, 1) != 0) Fail("P105g f1: a yes never removes a partial pickup's rest");
    if (LeftoverRemove(0, 0, 1) != 1) Fail("P105g f1: a GONE removes a partial pickup's stale rest");
    if (LeftoverRemove(0, 0, 0) != 0) Fail("P105g f1: a GONE of a whole readable pickup removes nothing");
    /* p105g-r1 (re-check MED): the holder's full kept table answers BUSY - re-asked at any try count, never toward the cap */
    if (SettleAction(0, kGroundReasonBusy, 0, 0, 0, 99) != kSgReask) Fail("P105g r1: a busy (kept table full) refusal is asked again however many tries");
    if (PickTook(1, 10, -1) != 0) Fail("P105g r1: an unreadable quantity after the pickup is not counted as taken");
}
void t_p105g_f1_take_back_rank()
{
    using namespace coopgshow;
    if (TakeBackRank(1, 0, 1) >= 0 || TakeBackRank(1, 1, 1) >= 0) Fail("P105g f1: an equipped slot is never taken back");
    const int a = TakeBackRank(0, 0, 1), b = TakeBackRank(0, 1, 1), c2 = TakeBackRank(0, 0, 0), d = TakeBackRank(0, 1, 0);
    if (!(a >= 0 && a < b && b < c2 && c2 < d)) Fail("P105g f1: quality first, then own sections before a worn pack");
}
/* p105g-f2 (row T-435): a granted ground TAKE uses the TAKE escrow - the holder's row with its ok CONFIRM out is held, not put back on
   the ground; a link-up relinks it; the picker's PLACED is listed and re-sent until it settles; 5 linked silent minutes = D6. */
void t_p105g_f2_ground_escrow()
{
    using namespace coopesc;
    const unsigned int T = 10000u, Q = kEscrowQuietMs;
    if (coopgshow::KeptCfSent(0, 1, 1) != 1) Fail("P105g f2: the grant's ok CONFIRM out marks the kept ground row");
    if (coopgshow::KeptCfSent(0, 1, 0) != 0) Fail("P105g f2: a CONFIRM that could not be sent does not mark it");
    if (coopgshow::KeptCfSent(1, 1, 0) != 1) Fail("P105g f2: a repeat whose send failed keeps the grant's mark");
    if (coopgshow::KeptCfSent(0, 0, 1) != 0) Fail("P105g f2: a refusal never marks");
    const int cfSent = coopgshow::KeptCfSent(0, 1, 1);
    int esc = 0;
    if (ExpireAction(0, cfSent, esc, T, 0, T, Q) != kExpEscrow) Fail("P105g f2: a granted ground TAKE with no PLACED in 10 s is held in escrow - not put back on the ground");
    if (ExpireAction(0, coopgshow::KeptCfSent(0, 1, 0), 0, T, 0, T, Q) != kExpRollback) Fail("P105g f2: (control) a grant whose CONFIRM never went out still goes back at 10 s");
    esc = 1;
    unsigned int quiet = 0;
    quiet = QuietStep(quiet, 1000, 1, kQuietDtCapMs);
    for (int k = 0; k < 1000; ++k) quiet = QuietStep(quiet, 2000, 0, kQuietDtCapMs);
    if (ExpireAction(0, cfSent, esc, 0, quiet, T, Q) != kExpWait) Fail("P105g f2: an escrowed ground row waits through a link-down");
    if (RelinkOnUp(0, cfSent, 1) != 1) Fail("P105g f2: a ground row with its ok CONFIRM out is relinked at a link-up");
    if (RelinkOnUp(0, 0, 1) != 0) Fail("P105g f2: a ground row whose CONFIRM never went out is not");
    TakerList tl;
    tl.Note(77, 1, 1, 3, 1000, 1);
    const int i77 = tl.Find(77);
    if (i77 < 0 || tl.e[i77].ground != 1) { Fail("P105g f2: the picker lists its ground PLACED, marked ground"); return; }
    if (tl.Due(i77, 3, 1) != 0) Fail("P105g f2: not re-sent on the link it went out on");
    if (tl.Prune(4, 0, 1000 + kTakerSettleMs, kTakerSettleMs) != 0 || tl.Count() != 1) Fail("P105g f2: the link dropped - the PLACED is kept");
    if (tl.Due(i77, 5, 1) != 1) Fail("P105g f2: re-sent at the next link-up");
    tl.MarkSent(i77, 5, 40000);
    if (tl.e[i77].ground != 1) Fail("P105g f2: still a ground entry after its re-send");
    if (tl.Due(i77, 5, 1) != 0) Fail("P105g f2: re-sent once per link");
    if (tl.Prune(5, 1, 40000 + kTakerSettleMs - 1, kTakerSettleMs) != 0) Fail("P105g f2: kept inside the settle time");
    if (tl.Prune(5, 1, 40000 + kTakerSettleMs, kTakerSettleMs) != 1 || tl.Count() != 0) Fail("P105g f2: 30 s on a link that stayed up - dropped");
    if (TakerLedgerCheck(1, 1) != 0) Fail("P105g f2: a ground PLACED's re-send is not cross-checked against the hand-over ledger (it writes none)");
    if (TakerLedgerCheck(1, 0) != 1 || TakerLedgerCheck(0, 0) != 0) Fail("P105g f2: (control) a character's ok PLACED still is");
    if (PlacedMissAction(0, 0, 1) != kMissRelinked) Fail("P105g f2: a re-sent PLACED from a renumbered picker finds the relinked row");
    if (PlacedKind(esc, 1) != kPlEscrowResent) Fail("P105g f2: booked as settled by the re-send");
    if (ExpireAction(0, cfSent, esc, 0, Q, T, Q) != kExpReturn) Fail("P105g f2: 5 linked minutes of silence return it (D6, as a character's / box's)");
    TakerList t2;
    t2.Note(9, 1, 1, 1, 0);
    const int i9 = t2.Find(9);
    if (i9 < 0 || t2.e[i9].ground != 0) Fail("P105g f2: a character's PLACED is not a ground entry");
}
void t_p25f2_inside_send_due()
{
    using namespace p25inside;
    if (!InsideSendDue(false, 0, "", 0, "", 0.0, 1.0)) Fail("P25f2: the first word must be sent");
    if (InsideSendDue(true, 1, "k1", 1, "k1", 10.0, 15.0)) Fail("P25f2: an unchanged word 5 s old is not yet due");
    if (!InsideSendDue(true, 1, "k1", 1, "k1", 10.0, 15.01)) Fail("P25f2: an unchanged word older than 5 s must be re-said");
    if (!InsideSendDue(true, 1, "k1", 0, "", 10.0, 10.1)) Fail("P25f2: inside -> outside must be sent at once");
    if (!InsideSendDue(true, 0, "", 1, "k1", 10.0, 10.1)) Fail("P25f2: outside -> inside must be sent at once");
    if (!InsideSendDue(true, 1, "k1", 1, "k2", 10.0, 10.1)) Fail("P25f2: a new building must be sent at once");
    if (InsideSendDue(true, 0, 0, 0, "", 10.0, 10.1)) Fail("P25f2: a null and an empty key are the same word");
}
void t_p25f2_inside_on_live_road()
{
    if (!cooplive::LiveInnerAccepted(66)) Fail("P25f2: INSIDE (66) must be taken from the notebook");
    if (!cooplive::CharStreamNeedsOwnerGate(66)) Fail("P25f2: a relayed INSIDE must pass the relayed-owner gate (its handler has no owner test)");
    if (cooplive::AreaMissedIsOneOff(66)) Fail("P25f2: a missed INSIDE is repeating (5 s), not a one-off");
    if (cooplive::CharStreamRoute(66) != (unsigned int)cooplive::kRouteArea) Fail("P25f2: INSIDE must go AREA on the notebook road");
    if (cooplive::CharStreamIndex(66) != -1) Fail("P25f2: INSIDE is not a counted stream column (the twelve are unchanged)");
}
/* ---- M7a (T-197 piece 7a): THE CHARACTER STREAM ON THE NOTEBOOK ROAD ------------------------------------------------ */
static int M7aOldAnnounceRule(bool held, bool mineLoaded, int state)   /* worldsync.cpp AnnouncePass before M7a */
{
    if (state == 1 && (!held || !mineLoaded)) return cooplive::kAnnUnload;
    if (state == 0 && held && mineLoaded) return cooplive::kAnnSpawn;
    return cooplive::kAnnKeep;
}
void t_m7a_announce_decide_reannounces_gained_reporter()
{
    /* FAILS BEFORE (the B8 case): a character was announced while game B held its area; game C newly has the area. The one flag
       reads 1 and B still holds the area, so the pre-patch rule answers KEEP - C never hears of the character. */
    if (M7aOldAnnounceRule(true, true, 1) != cooplive::kAnnKeep) Fail("the pre-patch rule is not the one this test pins");
    if (cooplive::AnnounceDecide(true, true, true, true) != cooplive::kAnnResend) Fail("an area that gained a reporter did not re-send an announced character (B8)");
    for (int h = 0; h < 2; ++h) for (int ml = 0; ml < 2; ++ml) for (int s = 0; s < 2; ++s)
        if (cooplive::AnnounceDecide(h != 0, ml != 0, s != 0, false) != M7aOldAnnounceRule(h != 0, ml != 0, s)) Fail("with no reporter gained AnnounceDecide is not the pass's rule");
    if (cooplive::AnnounceDecide(true, false, true, true) != cooplive::kAnnUnload) Fail("a character no longer loaded here was re-sent instead of withdrawn (E9)");
    if (cooplive::AnnounceDecide(false, true, true, true) != cooplive::kAnnUnload) Fail("a character no other game holds was re-sent instead of withdrawn");
    if (cooplive::AnnounceDecide(true, true, false, true) != cooplive::kAnnSpawn) Fail("an unannounced character another game holds was not announced to the notebook");
    if (cooplive::AnnounceDecide(false, true, false, true) != cooplive::kAnnKeep) Fail("an unannounced character nobody else holds was sent");
    if (cooplive::AnnounceDecide(true, false, false, true) != cooplive::kAnnKeep) Fail("an unannounced character not loaded here was sent");
}
/* ---- M7b slice 1 (T-197): A COPY-EFFECT REQUEST GOES TO ITS TARGET'S OWNER BY PLAYER NUMBER ---------------------------- */
void t_m7b_addr_route_owner_slot()
{
    /* FAILS BEFORE: every request went g_transport->Send(0, ...) - the session peer (slot 1 here) - whoever owned the target.
       A victim owned by slot 2 (a third game) must go LIVE SLOT 2, not to the session peer. */
    const int owner2 = cooplive::AddrOwnerSlotOf(cooplive::RelayPeerId(2), 1);
    if (owner2 != 2) Fail("a victim recorded as slot 2's did not read owner slot 2");
    cooplive::AddrPlan a = cooplive::AddrRoute(owner2, 1, true, true);
    if (a.road != cooplive::kAddrLive || a.slot != 2u) Fail("a victim owned by slot 2 did not route LIVE SLOT 2 (the old rule sent it to the session peer)");
    a = cooplive::AddrRoute(cooplive::AddrOwnerSlotOf(cooplive::RelayPeerId(1), 1), 1, true, true);
    if (a.road != cooplive::kAddrSession) Fail("a victim the session peer owns did not take the session link (the two-game send)");
    a = cooplive::AddrRoute(1, 1, false, true);
    if (a.road != cooplive::kAddrLive || a.slot != 1u) Fail("the session peer's victim with the session link down did not go LIVE SLOT to it");
    if (cooplive::AddrOwnerSlotOf(0u, -1) != cooplive::kAddrOwnerSessionPeer) Fail("a raw record before PEER_SLOT did not name the session peer");
    if (cooplive::AddrOwnerSlotOf(0u, 1) != 1) Fail("a raw record did not fold to the session peer's slot once PEER_SLOT said it");
    a = cooplive::AddrRoute(cooplive::kAddrOwnerSessionPeer, -1, true, false);
    if (a.road != cooplive::kAddrSession) Fail("the session peer with its slot unknown did not take the session link");
    a = cooplive::AddrRoute(cooplive::kAddrOwnerSessionPeer, -1, false, true);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoSlot) Fail("the session peer with no slot and no session link was sent somewhere");
    a = cooplive::AddrRoute(2, 1, true, false);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoSlot) Fail("a third game's victim with the notebook road down was sent (to the session peer?)");
    a = cooplive::AddrRoute(cooplive::kAddrOwnerNone, 1, true, true);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoOwner) Fail("a target with no owner on record was sent");
    a = cooplive::AddrRoute(2, -1, true, true);
    if (a.road != cooplive::kAddrLive || a.slot != 2u) Fail("with the session peer's slot unknown a slot-2 victim did not go LIVE SLOT 2");
    /* an answer: the asker's key is a relayed slot -> LIVE SLOT to it; the session peer's key -> the session link */
    a = cooplive::AddrRoute(cooplive::AddrOwnerSlotOf(cooplive::RelayPeerId(3), 1), 1, true, true);
    if (a.road != cooplive::kAddrLive || a.slot != 3u) Fail("an answer to a relayed asker (slot 3) did not go LIVE SLOT 3");
}
void t_m7b_addr_groups_and_whitelist()
{
    const unsigned int ts[8] = { 13, 63, 49, 48, 60, 61, 62, 47 };
    const int gs[8] = { cooplive::kAddrGroupHit, cooplive::kAddrGroupShot, cooplive::kAddrGroupTreat, cooplive::kAddrGroupPrison,
                        cooplive::kAddrGroupCapture, cooplive::kAddrGroupCapture, cooplive::kAddrGroupCapture, cooplive::kAddrGroupCarry };
    for (int i = 0; i < 8; ++i)
    {
        if (cooplive::AddrGroupOf(ts[i]) != gs[i]) { Fail("a request type is counted in the wrong addr group"); break; }
        if (!cooplive::LiveInnerAccepted(ts[i])) { Fail("a request type addressed by LIVE SLOT is refused at the receiving game"); break; }
    }
    if (cooplive::AddrGroupOf(12) != -1 || cooplive::AddrGroupOf(36) != -1 || cooplive::AddrGroupOf(64) != -1) Fail("a type outside slices 1, 2 and 4 was given an addr group");   /* M7b slice 4: 42 (DOOR_STATE) has one now */
    const unsigned int its[7] = { 37, 38, 39, 40, 41, 53, 54 };   /* M7b slice 2 */
    for (int i = 0; i < 7; ++i)
        if (cooplive::AddrGroupOf(its[i]) != cooplive::kAddrGroupItem || !cooplive::LiveInnerAccepted(its[i])) { Fail("an item message is not in addr.item or is refused at the receiving game"); break; }
    if (std::string(cooplive::AddrGroupName(cooplive::kAddrGroupItem)) != "item") Fail("the item addr group is not named item");
    if (std::string(cooplive::AddrGroupName(cooplive::kAddrGroupCarry)) != "carry" || std::string(cooplive::AddrGroupName(99)) != "?") Fail("the addr group names are wrong");
}
/* ---- M7b slice 2 (T-197): THE EIGHT ITEM MESSAGES ON ONE ADDRESSED ROAD ------------------------------------------------ */
void t_m7b2_item_addr_decide()
{
    /* FAILS BEFORE (slice 2): every item message went g_transport->Send(0, ...) - the session peer (slot 1 here) - whoever held the
       box's area. A box in an area whose holder is player 2 (a third game) must go through the world server to player 2. */
    const int box2 = cooplive::ItemAddrDecide(cooplive::kItemToArea, false, 0u, 2, false, 0u, 1);
    if (box2 != 2) Fail("a box held by player 2 was not addressed to player 2");
    cooplive::AddrPlan a = cooplive::ItemTargetRoad(box2, 1, true, cooplive::kItemRoadLive, 1, true, 1);
    if (a.road != cooplive::kAddrLive || a.slot != 2u) Fail("a box held by player 2 did not go LIVE SLOT 2 (the old send went to the session peer)");
    /* fold 1 FAILS BEFORE: on the session road a third game's target was not sent at all (an answer to a relayed request lapsed) */
    a = cooplive::ItemTargetRoad(2, 1, true, cooplive::kItemRoadSession, 1, true, 1);
    if (a.road != cooplive::kAddrLive || a.slot != 2u) Fail("on the session road an answer to a third game did not go through the world server");
    a = cooplive::ItemTargetRoad(1, 1, true, cooplive::kItemRoadSession, 1, true, 1);
    if (a.road != cooplive::kAddrSession) Fail("the session peer on the session road did not take the session link (the two-game send)");
    a = cooplive::ItemTargetRoad(1, 1, true, cooplive::kItemRoadLive, 1, true, 1);
    if (a.road != cooplive::kAddrLive || a.slot != 1u) Fail("the session peer on the world road did not go LIVE SLOT to its slot");
    /* fold 1 FAILS BEFORE: the session peer dropped from the world while the session link stayed up was sent nothing */
    a = cooplive::ItemTargetRoad(1, 1, true, cooplive::kItemRoadLive, 0, true, 0);
    if (a.road != cooplive::kAddrSession) Fail("the session peer out of the world, its session link up, was not sent on the session link");
    a = cooplive::ItemTargetRoad(2, 1, true, cooplive::kItemRoadLive, 1, true, 0);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNotInWorld) Fail("a third game out of the world was sent to");
    a = cooplive::ItemTargetRoad(2, 1, true, cooplive::kItemRoadSession, 1, false, 1);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoSlot) Fail("a third game with this game's world link down was sent to the session peer");
    a = cooplive::ItemTargetRoad(2, 1, true, cooplive::kItemRoadLive, 1, true, -1);
    if (a.road != cooplive::kAddrLive || a.slot != 2u) Fail("a stale roster held a send back (the world server judges)");
    /* no holder / no ask known: the session link while it is the session peer's road (no notebook: the host holds every box) */
    const int unk = cooplive::ItemAddrDecide(cooplive::kItemToArea, false, 0u, -1, false, 0u, 1);
    if (unk != cooplive::kAddrOwnerUnknown) Fail("a box with no holder known was addressed to someone");
    a = cooplive::ItemTargetRoad(unk, 1, true, cooplive::kItemRoadSession, 1, true, -1);
    if (a.road != cooplive::kAddrSession) Fail("with no holder known on the session road a box request did not go to the session peer");
    a = cooplive::ItemTargetRoad(unk, 1, true, cooplive::kItemRoadLive, 1, true, -1);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoOwner) Fail("with no holder known on the world road a box request was sent somewhere");
    a = cooplive::ItemTargetRoad(cooplive::kAddrOwnerSessionPeer, -1, true, cooplive::kItemRoadSession, -1, false, -1);
    if (a.road != cooplive::kAddrSession) Fail("the session peer before PEER_SLOT did not take the session link");
    a = cooplive::ItemTargetRoad(cooplive::kAddrOwnerSessionPeer, -1, false, cooplive::kItemRoadLive, -1, true, -1);
    if (a.road != cooplive::kAddrNone) Fail("the session peer with no slot and no session link was sent somewhere");
    /* a character: its owner; an answer: the asker */
    if (cooplive::ItemAddrDecide(cooplive::kItemToChar, true, cooplive::RelayPeerId(3), -1, false, 0u, 1) != 3) Fail("a character owned by player 3 was not addressed to player 3");
    if (cooplive::ItemAddrDecide(cooplive::kItemToChar, false, 0u, 2, false, 0u, 1) != cooplive::kAddrOwnerNone) Fail("a character with no owner on record was addressed to someone");
    a = cooplive::ItemTargetRoad(cooplive::kAddrOwnerNone, 1, true, cooplive::kItemRoadSession, 1, true, -1);
    if (a.road != cooplive::kAddrNone || a.why != cooplive::kAddrWhyNoOwner) Fail("a character with no owner was sent");
    if (cooplive::ItemAddrDecide(cooplive::kItemToAsker, false, 0u, -1, true, cooplive::RelayPeerId(4), 1) != 4) Fail("an answer to player 4 was not addressed to player 4");
    if (cooplive::ItemAddrDecide(cooplive::kItemToAsker, false, 0u, -1, true, 0u, 1) != 1) Fail("an answer to the session peer's raw key did not resolve to its slot");
    a = cooplive::ItemTargetRoad(2, 1, false, cooplive::kItemRoadNone, -1, false, 1);
    if (a.road != cooplive::kAddrNone) Fail("an item message was sent with no road");
}
/* fold 1 (MED): every game one request id was asked of; a re-send held to the game asked until it refuses */
void t_m7b2f1_asked_set()
{
    const unsigned int A = cooplive::RelayPeerId(2), B = cooplive::RelayPeerId(3), C = cooplive::RelayPeerId(4);
    unsigned int k = 0;
    cooplive::ItemAskedSet a;
    cooplive::ItemAskedAdd(&a, A, 1);
    if (!cooplive::ItemAskedSticky(a, &k) || k != A) Fail("a re-send does not go to the game the request was asked of");
    cooplive::ItemAskedAdd(&a, A, 1);
    if (a.n != 1) Fail("a re-send to the same game was recorded twice");
    /* FAILS BEFORE: the re-send went to the area's new holder under the same id and replaced who was asked (a double ground PUT;
       the first holder's late answer refused) */
    if (cooplive::ItemAskedRelease(&a, B, 1)) Fail("a refusal from a game not asked released the re-send");
    if (!cooplive::ItemAskedRelease(&a, A, 1) || cooplive::ItemAskedSticky(a, &k)) Fail("the asked game's refusal did not release the re-send");
    cooplive::ItemAskedAdd(&a, B, 1);
    if (!cooplive::ItemAskedSticky(a, &k) || k != B) Fail("after the refusal the next send did not go to the new writer");
    if (!cooplive::ItemAskedAccept(&a, A, 1) || !cooplive::ItemAskedAccept(&a, B, 1)) Fail("an answer from a game this id was asked of was refused");
    if (cooplive::ItemAskedAccept(&a, C, 1)) Fail("an answer from a game never asked was taken");
    if (!cooplive::ItemAskedFollowUp(a, &k) || k != B) Fail("a follow-up does not go to the game whose answer was taken");
    /* MED (a): asked on the session link before the peer's slot was known (key 0), answered once it is */
    cooplive::ItemAskedSet s;
    cooplive::ItemAskedAdd(&s, 0u, -1);
    if (!cooplive::ItemAskedAccept(&s, cooplive::RelayPeerId(1), 1)) Fail("the session peer asked before its slot was known was refused answering by slot");
    if (cooplive::ItemAskedAccept(&s, C, 1)) Fail("a third game was taken for the session peer once its slot was known");
    if (!cooplive::ItemAskedAccept(&s, 9u, 1)) Fail("the session peer's answer on the session link was refused");
    if (!cooplive::ItemAskedAccept(&s, C, -1)) Fail("with the session peer's slot still unknown its relayed answer was refused (cannot be told apart)");
    cooplive::ItemAskedSet c;
    for (unsigned int i = 0; i < 6; ++i) cooplive::ItemAskedAdd(&c, cooplive::RelayPeerId(10 + i), 1);
    if (c.n != cooplive::kItemAskedKeys || !cooplive::ItemAskedSticky(c, &k) || k != cooplive::RelayPeerId(15)) Fail("the asked set is not bounded, newest kept as the re-send's game");
}
/* fold 1 (LOW): the item messages wait for a world in the arrival queue; nothing else changes */
void t_m7b2f1_items_wait_for_world()
{
    const unsigned int its[7] = { 37, 38, 39, 40, 41, 53, 54 };
    for (int i = 0; i < 7; ++i) if (!cooplive::LiveInnerWaitsForWorld(its[i])) { Fail("a relayed item message is dropped without a world (the session link queues it)"); break; }
    for (unsigned int t = 0; t < 256; ++t)
    {
        const bool item = (t >= 37 && t <= 41) || t == 53 || t == 54 || t == 59 || t == 50 || t == 42   /* M7b slice 4: TALK, BUILD, DOOR_STATE wait too */
            || t == 51 || t == 52 || t == 55   /* NAME, SLAVE, HIRE wait too */
            || t == 75   /* TOWN_PRICES waits too */
            || t == 79   /* T-889 step 2: ITEM_HANDOVER waits too - a CLOSED / VERDICT arriving during a load is kept, not lost */
            || t == 48 || t == 49;   /* PRISON and TREAT wait too */
        if (!item && cooplive::LiveInnerWaitsForWorld(t)) { Fail("a non-item relayed type now waits for a world"); break; }
    }
}
/* ---- THE WORLD SERVER IS EVERY GAME'S DOOR: the load gate open, the address dialled, NAME / SLAVE / HIRE on its road ---------- */
void t_m11flip_world_door()
{
    if (!coopjoin::kRelayCarriesLiveStream) Fail("flip: the world server does not carry the live stream - a world-road game could never load");
    if (coopjoin::JoinLoadAllowed(true, coopjoin::kRelayCarriesLiveStream, true) != coopjoin::kJoinLoadAllowed) Fail("flip: a welcomed world-road game with the operator in the world is refused its load");
    if (coopjoin::JoinLoadAllowed(false, coopjoin::kRelayCarriesLiveStream, true) == coopjoin::kJoinLoadAllowed) Fail("flip: a game not welcomed on this link may load");
    if (coopjoin::JoinLoadAllowed(true, coopjoin::kRelayCarriesLiveStream, false) == coopjoin::kJoinLoadAllowed) Fail("flip: a game may load while the operator is not in the world");
    if (coopjoin::WorldDoorPick(true, true, true) != coopjoin::kDoorStore) Fail("flip: a joining game whose settings name store= does not ring it");
    if (coopjoin::WorldDoorPick(true, false, true) != coopjoin::kDoorTyped) Fail("flip: a joining game does not ring the address typed as the world server");
    if (coopjoin::WorldDoorPick(false, true, true) != coopjoin::kDoorStore) Fail("flip: a host does not ring its world server");
    if (coopjoin::WorldDoorPick(false, false, true) != coopjoin::kDoorNone) Fail("flip: a host with no world server rings its own listen address");
    if (coopjoin::WorldDoorPick(true, false, false) != coopjoin::kDoorNone || coopjoin::WorldDoorPick(false, false, false) != coopjoin::kDoorNone) Fail("flip: a game with no address rings something");
}
void t_m11flip_name_slave_hire_roads()
{
    const unsigned int ts[3] = { 51, 52, 55 };   /* NAME, SLAVE, HIRE */
    const int gs[3] = { cooplive::kAddrGroupName, cooplive::kAddrGroupSlave, cooplive::kAddrGroupHire };
    const char* const ns[3] = { "name", "slave", "hire" };
    if (cooplive::kInnerName != 51u || cooplive::kInnerSlave != 52u || cooplive::kInnerHire != 55u) Fail("flip: NAME / SLAVE / HIRE inner numbers are not the transport's");
    for (int i = 0; i < 3; ++i)
    {
        if (cooplive::AddrGroupOf(ts[i]) != gs[i] || std::string(cooplive::AddrGroupName(gs[i])) != ns[i]) { Fail("flip: NAME / SLAVE / HIRE are not counted in their own addr group"); break; }
        /* FAILS BEFORE: refused from the world server, so with no old link a name, a slave state or a hire never arrived */
        if (!cooplive::LiveInnerAccepted(ts[i])) { Fail("flip: NAME / SLAVE / HIRE are refused from the world server"); break; }
        if (!cooplive::LiveInnerWaitsForWorld(ts[i])) { Fail("flip: a relayed NAME / SLAVE / HIRE is dropped while the world loads (the session link kept it)"); break; }
    }
    if (cooplive::SideRouteOf(51, 0, false) != cooplive::kSideRouteWorld || cooplive::SideRouteOf(52, 0, false) != cooplive::kSideRouteWorld) Fail("flip: a NAME / SLAVE change does not reach every game");
    if (cooplive::SideRouteOf(55, 0, false) != cooplive::kSideRouteSlot) Fail("flip: a HIRE request / answer is not addressed to one player");
    if (cooplive::SideRouteOf(55, 1, false) != cooplive::kSideRouteWorld) Fail("flip: an own hire's DONE (no asker) does not reach every game");
    if (cooplive::AddrGroupName(cooplive::kAddrGroupCount) != std::string("?")) Fail("flip: the addr group names run past the count");
}
/* ---- M7b slice 4 (C3) + DOOR_STATE (C4): THE SIDE MESSAGES ON THE CHARACTER STREAM'S ROAD -------------------------------- */
void t_m7b4_side_roads()
{
    const unsigned int ts[7] = { 43, 44, 45, 46, 59, 50, 42 };   /* SAY, STATS, CRIME, BOUNTY, TALK, BUILD, DOOR_STATE */
    const int gs[7] = { cooplive::kAddrGroupSay, cooplive::kAddrGroupStats, cooplive::kAddrGroupCrime, cooplive::kAddrGroupBounty,
                        cooplive::kAddrGroupTalk, cooplive::kAddrGroupBuild, cooplive::kAddrGroupDoor };
    const char* const ns[7] = { "say", "stats", "crime", "bounty", "talk", "build", "door" };
    for (int i = 0; i < 7; ++i)
    {
        if (cooplive::AddrGroupOf(ts[i]) != gs[i]) { Fail("M7b4: a side message is counted in the wrong addr group"); break; }
        if (std::string(cooplive::AddrGroupName(gs[i])) != ns[i]) { Fail("M7b4: a side message's addr group has the wrong name"); break; }
        /* FAILS BEFORE: BOUNTY, TALK, BUILD and DOOR_STATE were refused from the notebook */
        if (!cooplive::LiveInnerAccepted(ts[i])) { Fail("M7b4: a side message is refused from the notebook at the receiving game"); break; }
        if (cooplive::SideRouteOf(ts[i], 0, false) == cooplive::kSideRouteNone) { Fail("M7b4: a side message has no route"); break; }
    }
    if (cooplive::kAddrGroupCount != 18) Fail("M7b4: the addr groups are not the seven old ones, the seven side messages, NAME / SLAVE / HIRE and MELEE_DECIDED");
    /* THE ROAD: both roads up and the peer proven -> the notebook, never the session link (viaSession 0); notebook down -> the session link */
    if (cooplive::CharStreamRoad(true, true, true, 1) != cooplive::kCharRoadLive) Fail("M7b4: with both roads up a side message took the session link");
    if (cooplive::CharStreamRoad(false, true, false, 1) != cooplive::kCharRoadSession) Fail("M7b4: with the notebook down a side message did not take the session link");
    if (cooplive::CharStreamRoad(false, false, false, -1) != cooplive::kCharRoadNone) Fail("M7b4: with no road a side message was given one");
    /* THE ROUTE on the notebook */
    if (cooplive::SideRouteOf(43, 0, false) != cooplive::kSideRouteArea || cooplive::SideRouteOf(44, 0, false) != cooplive::kSideRouteArea)
        Fail("M7b4: SAY / STATS do not go AREA (the character's sector)");
    if (cooplive::SideRouteOf(45, 0, false) != cooplive::kSideRouteWorld) Fail("M7b4: CRIME does not go WORLD (state never re-sent)");
    if (cooplive::SideRouteOf(46, (int)coopbounty::kBountyKindList, false) != cooplive::kSideRouteWorld) Fail("M7b4: the owner's bounty LIST does not go WORLD");
    if (cooplive::SideRouteOf(46, (int)coopbounty::kBountyKindAdd, false) != cooplive::kSideRouteSlot
        || cooplive::SideRouteOf(46, (int)coopbounty::kBountyKindClear, false) != cooplive::kSideRouteSlot) Fail("M7b4: a bounty addition / clear does not go to the owner by SLOT");
    if (cooplive::SideRouteOf(59, 0, false) != cooplive::kSideRouteSlot) Fail("M7b4: TALK does not go by SLOT");
    if (cooplive::SideRouteOf(50, 0, false) != cooplive::kSideRouteWorld) Fail("M7b4: BUILD does not go WORLD");
    if (cooplive::SideRouteOf(42, coopdoor::kDoorOriginHolder, true) != cooplive::kSideRouteArea) Fail("M7b4: the door holder's answer does not go AREA");
    if (cooplive::SideRouteOf(42, coopdoor::kDoorOriginActor, true) != cooplive::kSideRouteSlot) Fail("M7b4: an actor report does not go to the door's holder by SLOT");
    if (cooplive::SideRouteOf(42, coopdoor::kDoorOriginActor, false) != cooplive::kSideRouteArea) Fail("M7b4: an actor report with no holder known does not go AREA");
    /* TALK: the other side of the conversation */
    if (cooplive::TalkToSlot(false, true, cooplive::RelayPeerId(3), true, true, cooplive::RelayPeerId(2), 1) != 3) Fail("M7b4: a TALK about player 3's NPC did not go to player 3");
    if (cooplive::TalkToSlot(true, true, cooplive::RelayPeerId(2), false, true, cooplive::RelayPeerId(4), 1) != 4) Fail("M7b4: a TALK from our NPC did not go to the target's owner (player 4)");
    if (cooplive::TalkToSlot(true, false, 0u, false, true, 0u, -1) != cooplive::kAddrOwnerSessionPeer) Fail("M7b4: a TALK to the session peer before its slot is known was not the session peer");
    if (cooplive::TalkToSlot(true, false, 0u, true, false, 0u, 1) != cooplive::kAddrOwnerUnknown) Fail("M7b4: a TALK with no other side was addressed");
    /* an addressed side message with both roads up goes LIVE SLOT - to the session peer too (viaSession 0) */
    cooplive::AddrPlan a = cooplive::ItemTargetRoad(1, 1, true, cooplive::kItemRoadLive, 1, true, 1);
    if (a.road != cooplive::kAddrLive || a.slot != 1u) Fail("M7b4: with both roads up an addressed side message to the session peer took the session link");
    a = cooplive::ItemTargetRoad(cooplive::kAddrOwnerUnknown, 1, true, cooplive::kItemRoadSession, 1, true, -1);
    if (a.road != cooplive::kAddrSession) Fail("M7b4: on the session road a TALK with no other side known did not fall back to the session link");
}
/* M7b slice 4 fold 2 (D1): fold 1's rule, kept here only to show the D1 cases fail on it - a slot < 0 never sent, any other by slot */
static int M7b4f2Fold1Target(int recorded) { return recorded >= 0 ? recorded : cooplive::kAddrOwnerNone; }
static int M7b4f2Road(int to, int link, bool sess, int itemRoad, int peerIn, bool live, int tgtIn)
{
    if (to == cooplive::kAddrOwnerNone) return cooplive::kAddrNone;
    return cooplive::ItemTargetRoad(to, link, sess, itemRoad, peerIn, live, tgtIn).road;
}
struct M7b4f2Case { int rec; int link; bool live; bool sess; int itemRoad; int peerIn; int tgtIn; int wantTo; int wantRoad; bool f1Fails; const char* what; };
void t_m7b4f2_build_addr()
{
    const int rdS = cooplive::kItemRoadSession, rdL = cooplive::kItemRoadLive, rdN = cooplive::kItemRoadNone, toP = cooplive::kAddrOwnerSessionPeer;
    const M7b4f2Case c[] = {
        /* FAILS BEFORE (fold 1): two games - the send before fold 1 (the session link) */
        { -1, -1, false, true,  rdS, -1, -1, toP, cooplive::kAddrSession, true,  "two games, no notebook: a HELP_WORK with the owner's slot never learned was not sent on the session link" },
        { toP,  -1, false, true,  rdS, -1, -1, toP, cooplive::kAddrSession, true,  "two games, no notebook: a HAND_ACK to the placer before PEER_SLOT was not sent on the session link" },
        { 1,  -1, false, true,  rdS, -1, -1, toP, cooplive::kAddrSession, true,  "a relink during a notebook outage: the stored slot was not sent on the session link" },
        { 1,  -1, true,  true,  rdS, -1, 0,  toP, cooplive::kAddrSession, true,  "notebook up, the session peer's slot not yet re-learned, slot 1 out of the world: not sent on the session link" },
        { -1, 1,  true,  true,  rdL, 1,  -1, toP, cooplive::kAddrSession, true,  "notebook up, the owner's slot never recorded: not sent to the session peer" },
        /* unchanged: by SLOT as fold 1 intended */
        { 2,  1,  true,  true,  rdL, 1,  1,  2, cooplive::kAddrLive,    false, "both roads up: owner slot 2 (a third game) not by SLOT" },
        { 1,  1,  true,  true,  rdS, 1,  1,  1, cooplive::kAddrSession, false, "the session peer, slot known, on the session road: not on the session link" },
        { 2,  -1, true,  false, rdL, -1, 1,  2, cooplive::kAddrLive,    false, "session link down, notebook up: not by SLOT" },
        { 2,  1,  true,  true,  rdL, 1,  0,  2, cooplive::kAddrNone,    false, "a known third game out of the world was sent to the session peer" },
        /* nobody */
        { -1, -1, true,  false, rdL, -1, -1, cooplive::kAddrOwnerNone, cooplive::kAddrNone, false, "slot unknown and no session link: sent to someone" },
        { 2,  -1, false, false, rdN, -1, -1, 2, cooplive::kAddrNone,   false, "no road at all: sent" },
    };
    for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); ++i)
    {
        const int to = cooplive::BuildAddrTarget(c[i].rec, c[i].link, c[i].live, c[i].sess, c[i].tgtIn);
        const int road = M7b4f2Road(to, c[i].link, c[i].sess, c[i].itemRoad, c[i].peerIn, c[i].live, c[i].tgtIn);
        if (to != c[i].wantTo || road != c[i].wantRoad) Fail(std::string("M7b4f2 D1: ") + c[i].what);
        const int f1 = M7b4f2Road(M7b4f2Fold1Target(c[i].rec), c[i].link, c[i].sess, c[i].itemRoad, c[i].peerIn, c[i].live, c[i].tgtIn);
        if (c[i].f1Fails && f1 == c[i].wantRoad) Fail(std::string("M7b4f2 D1: the case does not fail on fold 1's rule: ") + c[i].what);
    }
    /* D2 - FAILS BEFORE: either road taking it read as sent */
    if (cooplive::SideSpreadSent(true, false, true, true)) Fail("M7b4f2 D2: a failed session send plus a notebook send read as sent (never retried)");
    if (!cooplive::SideSpreadSent(true, true, true, false)) Fail("M7b4f2 D2: a session send that went read as not sent");
    if (!cooplive::SideSpreadSent(false, false, true, true)) Fail("M7b4f2 D2: the notebook alone, sent, read as not sent");
    if (cooplive::SideSpreadSent(false, false, true, false)) Fail("M7b4f2 D2: the notebook alone, failed, read as sent");
    if (cooplive::SideSpreadSent(false, false, false, false)) Fail("M7b4f2 D2: no road read as sent");
    /* D4 - FAILS BEFORE: any failure with a road open dropped the message */
    if (cooplive::BountyUnsentDrops(false, true, cooplive::kAddrWhyNoSlot, 2)) Fail("M7b4f2 D4: an addition to a third game with the notebook down was dropped");
    if (cooplive::BountyUnsentDrops(false, true, cooplive::kAddrWhyOk, toP)) Fail("M7b4f2 D4: an addition whose send failed was dropped");
    if (cooplive::BountyUnsentDrops(false, false, cooplive::kAddrWhyNotInWorld, 2)) Fail("M7b4f2 D4: an addition was dropped with no road at all");
    if (cooplive::BountyUnsentDrops(true, false, cooplive::kAddrWhyOk, cooplive::kAddrOwnerNone)) Fail("M7b4f2 D4: a list was dropped with no road at all");
    if (!cooplive::BountyUnsentDrops(false, true, cooplive::kAddrWhyNotInWorld, 2)) Fail("M7b4f2 D4: an addition whose owner is out of the world held the queue");
    if (!cooplive::BountyUnsentDrops(false, true, cooplive::kAddrWhyNoSlot, toP)) Fail("M7b4f2 D4: an addition whose owner's slot is unknown with no session link held the queue");
    if (!cooplive::BountyUnsentDrops(true, true, cooplive::kAddrWhyOk, cooplive::kAddrOwnerNone)) Fail("M7b4f2 D4: an unsent list held the queue (it goes again within kBountyResendMs)");
}
void t_m7b2_item_confirm_wrong_sender()
{
    const unsigned int b = cooplive::RelayPeerId(2), c = cooplive::RelayPeerId(3);
    /* FAILS BEFORE: an ITEM_CONFIRM was applied from whichever game sent it - a third game's answer settled our request */
    if (cooplive::ItemAnswerFromAsked(true, b, c, 1)) Fail("an answer from a game that was not asked was taken");
    if (!cooplive::ItemAnswerFromAsked(true, b, b, 1)) Fail("the asked game's answer was refused");
    if (!cooplive::ItemAnswerFromAsked(true, 0u, cooplive::RelayPeerId(1), 1)) Fail("the session peer, asked on the session link, was refused answering through the world server");
    if (!cooplive::ItemAnswerFromAsked(true, 0u, 7u, -1)) Fail("the session peer's answer before PEER_SLOT was refused");
    if (cooplive::ItemAnswerFromAsked(true, 0u, c, 1)) Fail("a third game's answer to a request asked of the session peer was taken");
    if (!cooplive::ItemAnswerFromAsked(false, 0u, c, 1)) Fail("an answer with no ask on record was refused here (the late / foreign handling judges it)");
}
void t_m7b2_item_move_not_owner()
{
    /* FAILS BEFORE: a move on a character copy was applied from any game, and a box move from any game but this one */
    if (cooplive::ItemMoveSenderOk(true, false, 3, -1, -1)) Fail("a move on a character from a game that does not own it was applied");
    if (!cooplive::ItemMoveSenderOk(true, true, 2, -1, -1)) Fail("the owner's move on its own character was refused");
    if (cooplive::ItemMoveSenderOk(false, false, 3, 2, -1)) Fail("a box move from a game that neither holds the area nor owns the box was applied");
    if (!cooplive::ItemMoveSenderOk(false, false, 2, 2, -1)) Fail("the area holder's box move was refused");
    if (!cooplive::ItemMoveSenderOk(false, false, 3, 2, 3)) Fail("a player-owned box's owner (the owner rule) was refused");
    if (!cooplive::ItemMoveSenderOk(false, false, 3, -1, -1)) Fail("a box move with no holder and no owner known was refused (nobody to judge by)");
    if (!cooplive::ItemMoveSenderOk(false, false, -1, 2, -1)) Fail("the session peer before PEER_SLOT was refused a box move");
}
/* M7b fold 1 (1): a LIVE SLOT send to a slot the PLAYERS roster shows NOT in the world is not delivered (the caller retries / waits). */
void t_m7b_fold1_live_slot_not_in_world()
{
    /* FAILS BEFORE: the send went anyway and returned true - counted sent, so a capture waited and a prison or treat was never retried */
    if (cooplive::AddrLiveSendGoes(0)) Fail("a send to a slot the roster shows out of the world was delivered");
    if (!cooplive::AddrLiveSendGoes(1)) Fail("a send to a slot in the world was held back");
    if (!cooplive::AddrLiveSendGoes(-1)) Fail("a send with a stale roster was held back (the world server judges)");
}
/* M7b fold 1 (2): a second guard game's cage request refuses the first asker. */
void t_m7b_fold1_prison_refuses_earlier_asker()
{
    const unsigned int b = cooplive::RelayPeerId(2), c = cooplive::RelayPeerId(3);
    if (cooplive::AddrPrisonRefusesEarlier(false, 0u, c, 1)) Fail("a first cage request refused someone");
    /* FAILS BEFORE: latest wins silently - the first asker's copy stayed caged with no answer */
    if (!cooplive::AddrPrisonRefusesEarlier(true, b, c, 1)) Fail("a second guard game's cage request did not refuse the first asker");
    if (cooplive::AddrPrisonRefusesEarlier(true, b, b, 1)) Fail("a repeat from the same asker refused itself");
    if (cooplive::AddrPrisonRefusesEarlier(true, 0u, cooplive::RelayPeerId(1), 1)) Fail("the session peer's raw key and its slot key were taken for two games");
}
/* M7b fold 1 (3): a copy with no recorded owner counts noOwner once per (uid, kind) while its sender keeps retrying. */
void t_m7b_fold1_no_owner_counts_once()
{
    std::set<unsigned long long> seen;
    const int g = cooplive::kAddrGroupPrison;
    if (!cooplive::AddrNoOwnerCountOnce(&seen, g, 77u, true)) Fail("the first owner-less send was not counted");
    /* FAILS BEFORE: every retry (one per scan) added one noOwner */
    for (int i = 0; i < 5; ++i) if (cooplive::AddrNoOwnerCountOnce(&seen, g, 77u, true)) { Fail("a retried owner-less send counted noOwner again"); break; }
    if (!cooplive::AddrNoOwnerCountOnce(&seen, cooplive::kAddrGroupTreat, 77u, true)) Fail("another kind for the same uid was not counted");
    if (!cooplive::AddrNoOwnerCountOnce(&seen, g, 78u, true)) Fail("another uid was not counted");
    if (cooplive::AddrNoOwnerCountOnce(&seen, g, 77u, false)) Fail("a send that found the owner counted noOwner");
    if (!cooplive::AddrNoOwnerCountOnce(&seen, g, 77u, true)) Fail("an owner lost again was not counted again");
    if (!cooplive::AddrNoOwnerCountOnce(&seen, g, 0u, true) || !cooplive::AddrNoOwnerCountOnce(&seen, g, 0u, true)) Fail("uid 0 was not counted every time");
    seen.clear();
    for (unsigned int u = 1; u <= (unsigned int)cooplive::kAddrNoOwnerSeenMax + 10u; ++u) cooplive::AddrNoOwnerCountOnce(&seen, g, u, true);
    if (seen.size() > cooplive::kAddrNoOwnerSeenMax) Fail("the noOwner set grew past its bound");
}
void t_m7a_char_stream_one_road()
{
    if (cooplive::CharStreamRoad(true, true, true, 3) != cooplive::kCharRoadLive) Fail("with the notebook road up and the peer proven the stream took the session link");
    if (cooplive::CharStreamRoad(true, false, false, -1) != cooplive::kCharRoadLive) Fail("the notebook road alone did not carry the stream");
    if (cooplive::CharStreamRoad(false, true, false, 3) != cooplive::kCharRoadSession) Fail("with the notebook road down the session link did not carry the stream");
    if (cooplive::CharStreamRoad(false, false, false, -1) != cooplive::kCharRoadNone) Fail("with no road a send was not refused");
}
void t_m7a_char_stream_types_and_routes()
{
    const unsigned int ts[12] = { 10, 22, 15, 16, 12, 11, 21, 14, 17, 19, 18, 27 };   /* SPAWN .. UNLOAD, the REPORT's order */
    std::set<std::string> names;
    for (int i = 0; i < 12; ++i)
    {
        if (cooplive::CharStreamIndex(ts[i]) != i) { Fail("a stream type has the wrong column"); break; }
        if (!cooplive::LiveInnerAccepted(ts[i])) { Fail("a stream type is refused at the relay"); break; }
        const unsigned int want = (i >= 10) ? (unsigned int)cooplive::kRouteWorld : (unsigned int)cooplive::kRouteArea;
        if (cooplive::CharStreamRoute(ts[i]) != want) { Fail("a stream type took the wrong route (withdrawals WORLD, the rest AREA)"); break; }
        names.insert(cooplive::CharStreamName(i));
    }
    if (names.size() != 12 || names.count("?") != 0) Fail("the stream's column names are not twelve distinct names");
    int n = 0;
    for (unsigned int t = 0; t < 256; ++t) if (cooplive::CharStreamIndex(t) >= 0) ++n;
    if (n != cooplive::kCharStreamTypes) Fail("a type outside the twelve is counted as the character stream");
}
void t_m7a_char_area_target_names_the_sector_left()
{
    const int k55 = cooplive::AreaKey(5, 5), k45 = cooplive::AreaKey(4, 5);
    int key = -2, prev = -2;
    if (!cooplive::AreaTargetDecode(cooplive::CharAreaTarget(k55, k45), &key, &prev) || key != k55 || prev != k45) Fail("a crossing did not name the sector left");
    if (!cooplive::AreaTargetDecode(cooplive::CharAreaTarget(k55, k55), &key, &prev) || key != k55 || prev != -1) Fail("a send in the same sector named a sector left");
    if (!cooplive::AreaTargetDecode(cooplive::CharAreaTarget(k55, -1), &key, &prev) || key != k55 || prev != -1) Fail("a first send named a sector left");
    if (cooplive::CharAreaTarget(-1, k45) != cooplive::kAreaTargetNone) Fail("no sector now still gave an AREA target");
}
void t_m7a_relayed_stream_owner_gate()
{
    const unsigned int gated[6] = { 15, 16, 17, 19, 21, 22 };
    for (int i = 0; i < 6; ++i) if (!cooplive::CharStreamNeedsOwnerGate(gated[i])) Fail("a stream type with no handler owner test is not gated");
    /* M7a fold (review F4): SPAWN is NOT owner-checked by its handler (OnSpawn writes the record) - it has its own relayed gate,
       cooplive::RelayedSpawnRefused (t_m7af_relayed_spawn_owner_gate); these five are owner-checked in their handlers. */
    const unsigned int own[5] = { 11, 12, 14, 18, 27 };
    for (int i = 0; i < 5; ++i) if (cooplive::CharStreamNeedsOwnerGate(own[i])) Fail("a type its handler already owner-checks is gated twice");
    if (cooplive::CharStreamNeedsOwnerGate(10)) Fail("SPAWN took the stream gate instead of its own (RelayedSpawnRefused)");
    if (cooplive::RelayedStreamGate(true, true, true) != cooplive::kStreamGateOwnedHere) Fail("a relayed write to a character this game runs passed");
    if (cooplive::RelayedStreamGate(false, true, false) != cooplive::kStreamGateOtherOwner) Fail("a relayed write from a game that does not own the character passed");
    if (cooplive::RelayedStreamGate(false, true, true) != cooplive::kStreamGatePass) Fail("the owner's own relayed write was refused");
    if (cooplive::RelayedStreamGate(false, false, false) != cooplive::kStreamGatePass) Fail("a CONTEXT ahead of its SPAWN (owner not yet known) was refused");
}
void t_m7a_relayed_move_is_a_level()
{
    if (!cooplive::RelayedIsLevel(cooplive::kInnerMove, 36)) Fail("a relayed MOVE is not a level");
    if (cooplive::RelayedIsLevel(cooplive::kInnerMove, 3)) Fail("a MOVE too short to carry its uid was keyed");
    if (cooplive::RelayedIsLevel(cooplive::kInnerState, 200) || cooplive::RelayedIsLevel(cooplive::kInnerSpawn, 200)) Fail("an edge-shaped relayed type was collapsed");
}
/* ---- M7a FOLD (review 2026-09-30): F1 the road by proof and the proof re-asked at IN_WORLD, F2 (b) the reconnect sweep, F4 --------- */
void t_m7af_char_road_follows_the_peer_proof()
{
    /* FAILS BEFORE (F1): the road read only this game's notebook link - with the session peer's own notebook link down (not proven
       reachable) the stream went through the notebook, which had forgotten that peer's area, and it froze every character of ours. */
    if (cooplive::CharStreamRoad(true, true, false, 3) != cooplive::kCharRoadSession) Fail("F1: the session peer not proven reachable - the stream did not take the session link");
    if (cooplive::CharStreamRoad(true, true, false, -1) != cooplive::kCharRoadSession) Fail("F1: the peer's slot unknown - the stream did not take the session link");
    if (cooplive::CharStreamRoad(true, true, true, 3) != cooplive::kCharRoadLive) Fail("F1: the session peer proven reachable - the stream did not take the notebook");
    if (cooplive::CharStreamRoad(true, false, false, -1) != cooplive::kCharRoadLive) Fail("F1: no session link - the stream did not take the notebook");
}
void t_m7af_relpeer_ok_learnt_for_two_in_world_games()
{
    /* FAILS BEFORE (T708, Confirmed on main): each game's proof copy went only with the ask of its own link key - A's while B was at
       the title, which the world server drops (M11a S1: live messages only to a game IN_WORLD) - and nothing re-sent it: B's relPeer
       ok stayed 0 and, with F1, the stream would have stayed on the session link for two players. */
    const long sess = 5, live = 9; const int slotA = 1, slotB = 2;
    cooplive::PeerWorldWatch wa, wb; cooplive::PeerRelayBook ba, bb;
    if (coopjoin::LiveDestAllowed(coopjoin::kStageTitle)) Fail("the world server delivers to a game at the title - the premise of this test changed");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, slotB, false) != 0) Fail("a peer at the title owed an ask");
    /* B's first roster shows A in the world: B owes an ask (sent once B's world runs); A is IN_WORLD, so A takes it as proof */
    if (cooplive::PeerInWorldAskDue(&wb, sess, live, slotA, true) != 1) Fail("B did not owe an ask for A already in the world");
    if (coopjoin::LiveDestAllowed(coopjoin::kStageInWorld)) cooplive::PeerBookRelayed(&ba, true, 1);
    if (!ba.ok) Fail("A did not learn that B reaches it through the world server");
    /* B enters the world: A's roster edge owes A an ask; B, IN_WORLD now, takes its notebook copy as proof */
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, slotB, true) != 1) Fail("A did not owe an ask when B entered the world");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, slotB, true) != 0) Fail("A owed a second ask for the same entry");
    cooplive::PeerBookRelayed(&bb, true, 1);
    if (!bb.ok) Fail("B's relPeer ok did not become 1");
    if (cooplive::CharStreamRoad(true, true, ba.ok, slotB) != cooplive::kCharRoadLive || cooplive::CharStreamRoad(true, true, bb.ok, slotA) != cooplive::kCharRoadLive)
        Fail("two proven in-world games did not stream through the world server");
    /* B's notebook link drops: its session ask says so - A clears and bars and streams on the session link; B re-dials and enters the
       world again: A owes a new ask, and B's own newer ask proves B again to A */
    cooplive::PeerBookSessionAskRelayDown(&ba, 2);
    if (ba.ok || cooplive::CharStreamRoad(true, true, ba.ok, slotB) != cooplive::kCharRoadSession) Fail("with B's notebook link down A's stream did not take the session link");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, slotB, false) != 0) Fail("B leaving the world owed an ask");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, slotB, true) != 1) Fail("B back in the world did not owe A an ask");
    cooplive::PeerBookRelayed(&ba, true, 3);
    if (!ba.ok) Fail("A did not learn again that B reaches it");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live + 1, slotB, true) != 1) Fail("a new notebook link did not owe an ask for a peer in the world");
    if (cooplive::PeerInWorldAskDue(&wa, sess, 0, slotB, true) != 0) Fail("with this game's notebook road down an ask was owed");
    if (cooplive::PeerInWorldAskDue(&wa, sess, live, -1, true) != 0) Fail("with the peer's slot unknown an ask was owed");
}
void t_m7af_reconnect_sweep_withdraws_what_was_not_resent()
{
    /* FAILS BEFORE (F2 b): the catch-up restored presence and state only - a copy whose character despawned or unloaded in the gap
       stayed a ghost on the game that came back. M7a2 [m7a2-t1]: the marks carry their sector and the END the sectors asked. */
    const unsigned int k3 = cooplive::RelayPeerId(3), k4 = cooplive::RelayPeerId(4);
    cooplive::CatchupSweep s;
    std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(10);
    uids.push_back(200); owners.push_back(k4); keys.push_back(10);
    const std::vector<int> asked(1, 10); const std::vector<unsigned int> none; const std::vector<int> noKeys;
    cooplive::CatchupSweepOpen(&s, 7, uids, owners, keys, 0.0);
    if (!cooplive::CatchupSweepSeen(&s, 100, k3)) Fail("the owner's re-sent character was not taken off the sweep");
    if (cooplive::CatchupSweepSeen(&s, 200, k3)) Fail("another player's message took a character off the sweep");
    cooplive::CatchupSweepEndResult r;
    cooplive::CatchupSweepEnd(&s, 6, k3, asked, none, &r);
    if (!r.withdraw.empty()) Fail("an END of another ask withdrew a copy");
    cooplive::CatchupSweepEnd(&s, 7, k3, asked, none, &r);
    if (r.withdraw.size() != 1 || r.withdraw[0] != 101) Fail("slot 3's END did not withdraw exactly the copy it did not re-send");
    r = cooplive::CatchupSweepEndResult(); cooplive::CatchupSweepEnd(&s, 7, k3, asked, none, &r);
    if (!r.withdraw.empty()) Fail("a repeated END withdrew again");
    r = cooplive::CatchupSweepEndResult(); cooplive::CatchupSweepEnd(&s, 7, k4, asked, none, &r);
    if (r.withdraw.size() != 1 || r.withdraw[0] != 200) Fail("slot 4's END did not withdraw its own copy it did not re-send");
    cooplive::CatchupSweepOpen(&s, 8, uids, owners, keys, 0.0);
    cooplive::CatchupSweepOpen(&s, 9, none, none, noKeys, 0.0);
    r = cooplive::CatchupSweepEndResult(); cooplive::CatchupSweepEnd(&s, 8, k3, asked, none, &r);
    if (!r.withdraw.empty()) Fail("a newer sweep kept an older sweep's marks");
}
/* ---- M7a SECOND HALF, PART 1 (T-197; game-to-game protocol 112) ---------------------------------------------------------- */
void t_m7a2_sweep_only_what_the_owner_was_asked()
{
    /* FAILS BEFORE (item 1): the END named no sectors, so an owner's END withdrew every marked copy of its - also one standing in a
       sector the notebook never asked it about (not loaded there), which it could not have re-sent. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3);
    CatchupSweep s; std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(11);
    CatchupSweepOpen(&s, 5, uids, owners, keys, 0.0);
    CatchupSweepEndResult r;
    CatchupSweepEnd(&s, 5, k3, std::vector<int>(1, 10), std::vector<unsigned int>(), &r);
    /* M7a2 fold 1 item 5 [m7a2f-t0]: an owner asked about OTHER sectors answers for every marked copy of its - the one outside is withdrawn
       too (the END does not name it held); only an END naming no sector keeps them (t_m7a2f_claims_for_not_asked_and_expired_copies). */
    if (r.withdraw.size() != 2 || r.withdraw[0] != 100 || r.withdraw[1] != 101 || r.withdrawnNotAsked != 1) Fail("fold 1 item 5: the END of an owner asked about another sector did not withdraw its copy outside it");
    if (r.keptNotAsked != 0) Fail("fold 1 item 5: a copy outside the asked sectors was kept although its owner answered for other sectors");
    if (!s.marked.empty()) Fail("item 1: the owner's END left its marks");
}
void t_m7a2_sweep_keeps_a_handover_in_flight()
{
    /* FAILS BEFORE (item 2): the owner skipped a character whose hand-over was in flight, and its END withdrew the copy anyway. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3), k4 = RelayPeerId(4);
    CatchupSweep s; std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(10);
    const std::vector<int> asked(1, 10);
    CatchupSweepOpen(&s, 6, uids, owners, keys, 0.0);
    CatchupSweepEndResult r;
    CatchupSweepEnd(&s, 6, k3, asked, std::vector<unsigned int>(1, 101), &r);
    if (r.withdraw.size() != 1 || r.withdraw[0] != 100 || r.keptHeld != 1) Fail("item 2: a character held back by a hand-over in flight was withdrawn");
    CatchupSweepOpen(&s, 7, uids, owners, keys, 0.0);
    if (!CatchupSweepRekey(&s, 101, k3, k4)) Fail("item 3: the mark was not re-keyed to the new owner");
    if (CatchupSweepRekey(&s, 100, k4, k3)) Fail("item 3: a mark was re-keyed by a player that is not its owner");
    r = CatchupSweepEndResult(); CatchupSweepEnd(&s, 7, k3, asked, std::vector<unsigned int>(), &r);
    if (r.withdraw.size() != 1 || r.withdraw[0] != 100) Fail("item 3: the old owner's END withdrew the handed-over copy");
    if (!CatchupSweepSeen(&s, 101, k4)) Fail("item 3: the new owner's re-send did not take its mark off");
    if (CatchupCharDecide(true, true, true, true) != kCatchupHeld) Fail("item 2: a hand-over in flight was not held");
    if (CatchupCharDecide(false, true, true, true) != kAnnResend) Fail("an announced character was not re-sent");
    if (CatchupCharDecide(false, true, true, false) != kAnnSpawn) Fail("an unannounced character was not announced");
    if (CatchupCharDecide(false, false, true, true) != kAnnUnload) Fail("item 5: a character nobody else holds was re-sent");
    CatchupEndMsg e; e.askNo = 9; e.count = 3; e.keys.push_back(10); e.keys.push_back(4095); e.held.push_back(101);
    std::vector<char> b;
    if (!CatchupEndEncode(&b, e)) Fail("CATCHUP_END: a good one was refused");
    CatchupEndMsg d;
    if (!CatchupEndDecode(&b[0], b.size(), &d) || d.askNo != 9 || d.count != 3 || d.keys.size() != 2 || d.keys[1] != 4095 || d.held.size() != 1 || d.held[0] != 101)
        Fail("CATCHUP_END did not round-trip");
    if (CatchupEndDecode(&b[0], b.size() - 1, &d) || CatchupEndDecode(&b[0], 15, &d)) Fail("a short CATCHUP_END decoded");
    std::vector<char> bad = b; bad[12] = (char)0x00; bad[13] = (char)0x10; if (CatchupEndDecode(&bad[0], bad.size(), &d)) Fail("a CATCHUP_END sector off the grid decoded");
    e.keys.push_back(4096); std::vector<char> b2; if (CatchupEndEncode(&b2, e) || !b2.empty()) Fail("a CATCHUP_END with a sector off the grid was encoded");
    e.keys.pop_back(); e.held.assign(kCatchupEndHeldMax + 1, 7u); if (CatchupEndEncode(&b2, e)) Fail("a CATCHUP_END over the held bound was encoded");
}
void t_m7a2_owner_moved_rule()
{
    /* FAILS BEFORE (item 3): a third game kept the OLD owner's record after a hand-over it was not part of, so it refused the new owner's
       SPAWN and its stream (RelayedSpawnRefused / RelayedStreamGate / RemoteMayWrite all compare with that record). */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3), k4 = RelayPeerId(4);
    if (!OwnerMovedTaken(false, true, true, false)) Fail("item 3: the recorded owner's OWNER_MOVED was not taken");
    if (OwnerMovedTaken(false, true, false, false)) Fail("item 3: an OWNER_MOVED from a player that is not the recorded owner was taken");
    if (OwnerMovedTaken(true, true, true, false)) Fail("item 3: an OWNER_MOVED for a character this game runs was taken");
    if (OwnerMovedTaken(false, false, false, false)) Fail("item 3: an OWNER_MOVED for a character this game has no record of was taken");
    if (OwnerMovedTaken(false, true, true, true)) Fail("item 3: an OWNER_MOVED naming its own sender was taken");
    if (!LiveInnerRoadLocal(kInnerOwnerMoved) || LiveInnerAccepted(kInnerOwnerMoved)) Fail("item 3: OWNER_MOVED is not read by the store layer alone");
    CatchupSweep s; std::vector<unsigned int> uids(1, 100), owners(1, k3); std::vector<int> keys(1, 10);
    CatchupSweepOpen(&s, 1, uids, owners, keys, 0.0);
    if (!SweepClaimKept(s, 100, k4)) Fail("item 3: another player's SPAWN for a marked copy was not kept as a claim");
    if (SweepClaimKept(s, 100, k3)) Fail("item 3: the recorded owner's own SPAWN was kept as a claim");
    if (SweepClaimKept(s, 999, k4)) Fail("item 3: a SPAWN for an unmarked copy was kept as a claim");
}
void t_m7a2_world_road_ask_held_until_in_world()
{
    /* FAILS BEFORE (item 4): a world-road game answered an ASK drained before its JOIN_STAGE IN_WORLD; the world server refuses a
       world-road game's live messages until then, so the asker's catch-up was lost. */
    using namespace cooplive;
    if (CatchupAskHoldDecide(true, false, 0.0, kCatchupAskTimeoutSec) != kAskHoldHold) Fail("item 4: a world-road ASK before IN_WORLD was not held");
    if (CatchupAskHoldDecide(true, true, 1.0, kCatchupAskTimeoutSec) != kAskHoldAnswer) Fail("item 4: a held ASK was not answered after IN_WORLD");
    if (CatchupAskHoldDecide(false, false, 0.0, kCatchupAskTimeoutSec) != kAskHoldAnswer) Fail("item 4: a session-road game held its ASK");
    if (CatchupAskHoldDecide(true, true, kCatchupAskTimeoutSec + 1.0, kCatchupAskTimeoutSec) != kAskHoldDrop) Fail("item 4: an ASK held past the timeout was answered");
}
void t_m7a2_move_held_until_its_state()
{
    /* FAILS BEFORE (item 6): a relayed MOVE for a character whose catch-up state had not arrived was refused (owner unknown) and lost. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3);
    if (RelayedMoveHoldDecide(true, false, false) != kMoveHoldHold) Fail("item 6: a relayed MOVE before its SPAWN was not held");
    if (RelayedMoveHoldDecide(true, false, true) != kMoveHoldApply) Fail("item 6: a relayed MOVE for a known copy was held");
    if (RelayedMoveHoldDecide(false, false, false) != kMoveHoldApply) Fail("item 6: a session-road MOVE was held");
    if (RelayedMoveHoldDecide(true, true, false) != kMoveHoldApply) Fail("item 6: a MOVE for this game's own character was held");
    MoveHoldBook b; const std::vector<char> m1(36, 1), m2(36, 2);
    if (MoveHoldPut(&b, 7, k3, m1, 0.0) != kMoveHoldPutNew || MoveHoldPut(&b, 7, k3, m2, 1.0) != kMoveHoldPutReplaced) Fail("item 6: the hold is not the latest per character");
    std::vector<char> out; unsigned int from = 0;
    if (!MoveHoldTake(&b, 7, &out, &from) || from != k3 || out != m2) Fail("item 6: the latest held MOVE was not the one taken");
    if (MoveHoldTake(&b, 7, &out, &from)) Fail("item 6: a held MOVE was taken twice");
    MoveHoldPut(&b, 8, k3, m1, 0.0); MoveHoldPut(&b, 8, k3, m2, kMoveHoldSec);
    if (MoveHoldExpire(&b, kMoveHoldSec - 1.0, kMoveHoldSec) != 0) Fail("item 6: a fresh hold expired");
    if (MoveHoldExpire(&b, kMoveHoldSec + 1.0, kMoveHoldSec) != 1 || !b.rows.empty()) Fail("item 6: a hold kept alive by newer MOVEs never expired");
    for (unsigned int u = 0; u < (unsigned int)kMoveHoldMax; ++u) MoveHoldPut(&b, 1000 + u, k3, m1, 0.0);
    if (MoveHoldPut(&b, 5, k3, m1, 0.0) != kMoveHoldPutFull || b.rows.size() != kMoveHoldMax) Fail("item 6: the hold grew past its bound");
}
void t_move_hold_expiry_books_at_latest_spot()
{
    /* FAILS BEFORE: a held MOVE whose character never arrived expired silently - nothing booked the character, so a character that
       walked into this game's area after its SPAWN went elsewhere never arrived. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3);
    MoveHoldBook b;
    std::vector<char> m1(40, 0), m2(40, 0), bad(40, 0);
    const float p1[3] = { 100.0f, 5.0f, 200.0f }, p2[3] = { 46000.0f, 7.0f, -3000.0f };
    std::memcpy(&m1[4], p1, 12); std::memcpy(&m2[4], p2, 12);
    float nanv = 0.0f; { unsigned int q = 0x7FC00000u; std::memcpy(&nanv, &q, 4); }
    const float pb[3] = { nanv, 0.0f, 0.0f }; std::memcpy(&bad[4], pb, 12);
    MoveHoldPut(&b, 9, k3, m1, 0.0); MoveHoldPut(&b, 9, k3, m2, 1.0);
    MoveHoldPut(&b, 10, k3, bad, 0.0);
    MoveHoldPut(&b, 11, k3, m1, kMoveHoldSec);
    std::vector<MoveHoldGone> gone;
    if (MoveHoldExpire(&b, kMoveHoldSec + 1.0, kMoveHoldSec, &gone) != 2 || gone.size() != 2) Fail("the expired holds were not handed back");
    else
    {
        if (gone[0].uid != 9 || gone[0].sender != k3 || !gone[0].hasPos || gone[0].x != 46000.0f || gone[0].z != -3000.0f)
            Fail("an expired hold was handed back without its owner or its latest spot");
        if (gone[1].uid != 10 || gone[1].hasPos != 0) Fail("a non-finite held position was taken as a spot");
    }
    if (MoveHoldExpire(&b, kMoveHoldSec + 1.5, kMoveHoldSec, &gone) != 0 || gone.size() != 2) Fail("an expired hold was handed back twice");
    if (b.rows.size() != 1 || b.rows.count(11) != 1) Fail("a hold that had not run out went");
    /* the decision when it is handed back */
    if (MoveHoldGoneDecide(false, false, k3) != kMoveHoldGoneBook) Fail("an unknown uid streamed by another game was not booked");
    if (MoveHoldGoneDecide(true, false, k3) != kMoveHoldGoneSkip) Fail("a uid this game runs was booked from a hold");
    if (MoveHoldGoneDecide(false, true, k3) != kMoveHoldGoneSkip) Fail("a uid with an owner record was booked from a hold (two roads)");
    if (MoveHoldGoneDecide(false, false, 0u) != kMoveHoldGoneSkip) Fail("a hold with no sender was booked");
    if (!MoveHoldExpiredLogThis(1) || !MoveHoldExpiredLogThis(20) || MoveHoldExpiredLogThis(21) || !MoveHoldExpiredLogThis(50)
        || MoveHoldExpiredLogThis(51) || !MoveHoldExpiredLogThis(100) || MoveHoldExpiredLogThis(0))
        Fail("the booked-hold line's budget is not the first 20, then every 50th");
}
void t_lost_book_takes_a_held_uid()
{
    /* FAILS BEFORE: the lost-copy book took only a uid with an owner record, so a uid known only from another game's MOVEs could not be
       booked, and nothing asked for it when it stood where this game keeps it. */
    using namespace lostcopy;
    unsigned int key = 0;
    if (LostRowOwner(1, 7u, 1, 9u, &key) != 1 || key != 7u) Fail("the owner record did not win over a held MOVE's sender");
    key = 0;
    if (LostRowOwner(0, 0u, 1, 9u, &key) != 1 || key != 9u) Fail("a row booked from a held MOVE did not name its sender");
    if (LostRowOwner(0, 0u, 0, 9u, &key) != 0) Fail("a row with no record and not from a held MOVE named an owner");
    if (LostRowOwner(0, 0u, 1, 0u, &key) != 0) Fail("a held MOVE with no sender named an owner");
    if (!cooplive::MoveHoldFinite(1.0f) || cooplive::MoveHoldFinite(std::numeric_limits<float>::infinity()) || cooplive::MoveHoldFinite(std::numeric_limits<float>::quiet_NaN()))
        Fail("the finite test on a held position is wrong");
    if (LostNoteAllowed(0, LostRowOwner(0, 0u, 1, 9u, 0), 0) != 1) Fail("a held uid of another game was not allowed in the book");
    if (LostNoteAllowed(0, LostRowOwner(0, 0u, 1, 9u, 0), 1) != 0) Fail("a held uid this table refused for good was allowed");
    if (LostNoteAllowed(1, LostRowOwner(0, 0u, 1, 9u, 0), 0) != 0) Fail("a uid this game runs was allowed");
    LostBook b;
    if (LostNote(&b, 5u, 9u, 1, 100.0f, 0.0f, 100.0f, 1) != 1 || b.rows.size() != 1 || b.rows[0].ownerFromMove != 1 || b.rows[0].ownerKey != 9u)
        Fail("a held uid was not booked under its sender");
    /* its spot is kept here: a new visit asks; not kept: it waits */
    if (LostLookDecide(0, 1, 1, b.rows[0]) != kLaAsk) Fail("a held uid on kept ground was not asked for");
    if (LostLookDecide(0, 1, 0, b.rows[0]) != kLaWait) Fail("a held uid on ground not kept here was asked for");
    /* later held MOVEs move the spot (another sector: the next kept look starts a new visit) */
    b.rows[0].loadedPrev = 1; b.rows[0].state = kLcAsked;
    LostNote(&b, 5u, 9u, 1, 9300.0f, 0.0f, 100.0f, 1);
    if (b.rows[0].x != 9300.0f || b.rows[0].loadedPrev != 0 || b.rows[0].ownerFromMove != 1) Fail("a held MOVE in another sector did not move the booked spot");
}
void t_spawn_not_kept_is_booked()
{
    /* FAILS BEFORE: a SPAWN made its copy wherever it stood, and where this game's engine did not keep it the engine put it away 4 s
       later ('lost copy booked' right after the SPAWN, T1145). */
    using namespace lostcopy;
    if (LostSpawnSpotRead(0, 1, 1) != -1 || LostSpawnSpotRead(0, 0, -1) != -1) Fail("no running world was read as a spot");
    if (LostSpawnSpotRead(1, 0, -1) != 0) Fail("an area not loaded here was not 'not kept'");
    if (LostSpawnSpotRead(1, -1, -1) != -1) Fail("an unreadable zone byte was taken as an answer");
    if (LostSpawnSpotRead(1, 1, 1) != 1) Fail("a kept spot was not kept");
    if (LostSpawnSpotRead(1, 1, 0) != 0) Fail("the edge strip of an unloaded side was kept");
    if (LostSpawnSpotRead(1, 1, -1) != -1) Fail("an unreadable keep test was taken as an answer");
    if (SpawnKeepDecide(0, 0, 0) != kSkBook) Fail("a SPAWN where this engine would not keep it was made");
    if (SpawnKeepDecide(0, 0, 1) != kSkMake) Fail("a SPAWN on kept ground was not made");
    if (SpawnKeepDecide(0, 0, -1) != kSkMake) Fail("a SPAWN with no live read did not go on as before");
    if (SpawnKeepDecide(1, 0, 0) != kSkMake) Fail("this game's own uid was booked");
    if (SpawnKeepDecide(0, 1, 0) != kSkMake) Fail("a repeat SPAWN of a copy here was booked");
    /* FAILS BEFORE: a SPAWN whose twin (a body of this game's own world) the creation core adopts was booked where its spot is not
       kept, so the twin was never adopted */
    if (SpawnKeepDecide(0, 0, 0, 1) != kSkMake) Fail("a SPAWN this game holds a twin for was booked");
    if (SpawnKeepDecide(0, 0, 0, 0) != kSkBook) Fail("no twin: a spot not kept was not booked");
    /* the booked SPAWN: its owner record names the owner; asked only when the spot is kept here */
    LostBook b;
    if (LostNote(&b, 1u, 4u, 1, 46000.0f, 0.0f, 0.0f) != 1 || b.rows[0].ownerFromMove != 0) Fail("a SPAWN not made was not booked under its owner record");
    if (LostLookDecide(0, 1, 0, b.rows[0]) != kLaWait) Fail("a SPAWN not made was asked for while its spot is not kept here");
    LostLookApply(&b.rows[0], 0, kLaWait, 0);
    if (LostLookDecide(0, 1, 1, b.rows[0]) != kLaAsk) Fail("a SPAWN not made was not asked for once its spot is kept here");
}
void t_one_road_per_uid()
{
    /* FAILS BEFORE: an expired hold asked the owner on a road of its own while the lost-copy book could ask for the same uid. */
    using namespace lostcopy;
    LostBook b;
    LostNote(&b, 5u, 9u, 1, 100.0f, 0.0f, 100.0f, 1);   /* booked from a held MOVE */
    LostLookApply(&b.rows[0], 1, kLaAsk, 1);              /* asked */
    if (b.rows[0].state != kLcAsked) Fail("the ask was not recorded");
    /* its owner's answer is a SPAWN on ground this game does not keep: the record now exists and the same row is refreshed */
    if (LostNote(&b, 5u, 9u, 1, 120.0f, 0.0f, 100.0f) != 0 || b.rows.size() != 1) Fail("a SPAWN not made opened a second row");
    if (b.rows[0].ownerFromMove != 0) Fail("the owner record did not take over a held-MOVE row");
    if (b.rows[0].state != kLcAsked || LostLookDecide(0, 1, 1, b.rows[0]) != kLaWait) Fail("a row awaiting its answer asked again");
    /* a hold expiring now is skipped: the record's road books it */
    if (cooplive::MoveHoldGoneDecide(false, true, 9u) != cooplive::kMoveHoldGoneSkip) Fail("a hold booked a uid the record's road holds");
}
void t_m7a2_ask_timeout_and_dedup()
{
    /* FAILS BEFORE (item 7): an ask whose owner never answered stayed open for ever (its sweep marks too), and a repeated ASK was
       answered again. */
    using namespace cooplive;
    CatchupAskBook b; CatchupAskOpen(&b, 3, 2, 0.0);
    if (CatchupAskEnd(&b, 3) != kAskEndCounted || CatchupAskEnd(&b, 3) != kAskEndDone) Fail("item 7: an ask did not end at its last owner's END");
    if (CatchupAskEnd(&b, 3) != kAskEndUnknown) Fail("item 7: a late END counted on an ended ask");
    CatchupAskOpen(&b, 4, 1, 0.0); CatchupAskOpen(&b, 5, 0, 0.0);
    if (b.rows.count(5) != 0) Fail("item 7: an ask with no owner asked waited");
    std::vector<unsigned int> ex;
    if (CatchupAskExpire(&b, kCatchupAskTimeoutSec - 1.0, kCatchupAskTimeoutSec, &ex) != 0) Fail("item 7: an ask ended before its timeout");
    if (CatchupAskExpire(&b, kCatchupAskTimeoutSec + 1.0, kCatchupAskTimeoutSec, &ex) != 1 || ex.size() != 1 || ex[0] != 4) Fail("item 7: an unanswered ask did not end at the timeout");
    for (unsigned int i = 0; i < 100; ++i) CatchupAskOpen(&b, 100 + i, 1, 0.0);
    if (b.rows.size() > kCatchupAskBookMax) Fail("item 7: the ask book grew past its bound");
    CatchupSweep s; CatchupSweepOpen(&s, 1, std::vector<unsigned int>(1, 100), std::vector<unsigned int>(1, RelayPeerId(3)), std::vector<int>(1, 10), 0.0);
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec - 1.0, kCatchupAskTimeoutSec) != 0) Fail("item 7: a sweep ended before its timeout");
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec + 1.0, kCatchupAskTimeoutSec) != 1 || !s.marked.empty()) Fail("item 7: a sweep nobody ended kept its marks past the timeout");
    std::vector<unsigned long long> rec;
    if (!CatchupAnswerFirst(&rec, 2, 9)) Fail("item 7: a first ASK was taken for a repeat");
    if (CatchupAnswerFirst(&rec, 2, 9)) Fail("item 7: a repeated ASK was answered twice");
    if (!CatchupAnswerFirst(&rec, 3, 9)) Fail("item 7: the same ask number for another game was taken for a repeat");
    for (unsigned int i = 0; i < 200; ++i) CatchupAnswerFirst(&rec, 4, i);
    if (rec.size() > kCatchupDedupMax) Fail("item 7: the dedup list grew past its bound");
}
/* ---- M7a2 FOLD 1 (review 2026-09-30) [m7a2f-t1] ------------------------------------------------------------------------------ */
void t_m7a2f_owner_moved_owed_and_announced()
{
    /* FAILS BEFORE (F1, F4): an old owner whose world link was down dropped its OWNER_MOVED silently, and a third game refused the new
       owner's own word (only the recorded owner was believed). Items 1, 2. */
    using namespace cooplive;
    if (OwnerMovedOwedDecide(false, false, true) != kOwedWait) Fail("item 1: an OWNER_MOVED owed while the world link is down was not kept");
    if (OwnerMovedOwedDecide(true, false, true) != kOwedSend) Fail("item 1: an owed OWNER_MOVED was not sent once the link was ready");
    if (OwnerMovedOwedDecide(true, true, true) != kOwedDrop) Fail("item 1: an owed OWNER_MOVED was sent for a character this game runs again");
    if (OwnerMovedOwedDecide(true, false, false) != kOwedDrop) Fail("item 1: an owed OWNER_MOVED was sent with no owner record left");
    std::vector<char> b; OwnerMovedEncode(&b, 77, 5, 3);
    unsigned int u = 0, n = 0, pv = 0;
    if (b.size() != 12 || !OwnerMovedDecode(&b[0], b.size(), 9, &u, &n, &pv) || u != 77 || n != 5 || pv != 3) Fail("item 2: OWNER_MOVED {uid, new, previous} did not round-trip");
    std::vector<char> o; LivePairEncode(&o, 78, 6);
    if (!OwnerMovedDecode(&o[0], o.size(), 4, &u, &n, &pv) || u != 78 || n != 6 || pv != 4) Fail("item 2: the 8-byte OWNER_MOVED did not name its sender as the previous owner");
    if (OwnerMovedDecode(&b[0], 10, 4, &u, &n, &pv)) Fail("item 2: a 10-byte OWNER_MOVED decoded");
    /* OwnerMovedDecide(ownedHere, ownerKnown, ownerIsSender, newIsSender, prevIsOwner, newIsOwner) */
    if (OwnerMovedDecide(false, true, true, false, true, false) != kOwnerMovedTake) Fail("item 2: the recorded owner's word was not taken");
    if (OwnerMovedDecide(false, true, false, true, true, false) != kOwnerMovedTake) Fail("item 2: the taker's announce naming the recorded owner as previous was not taken");
    if (OwnerMovedDecide(false, true, false, true, false, false) != kOwnerMovedRefuse) Fail("item 2: an announce naming another previous owner was taken");
    if (OwnerMovedDecide(false, true, false, false, true, false) != kOwnerMovedRefuse) Fail("item 2: a third player's word for another player was taken");
    if (OwnerMovedDecide(false, true, false, false, false, true) != kOwnerMovedNoop) Fail("item 2: the old owner's word after the taker's announce was not a no-op");
    if (OwnerMovedDecide(true, true, true, false, true, false) != kOwnerMovedMine) Fail("item 2: an OWNER_MOVED for a character this game runs was not its own");
    if (OwnerMovedDecide(false, false, false, true, false, false) != kOwnerMovedUnknown) Fail("item 2: an OWNER_MOVED with no record here was not unknown");
}
void t_m7a2f3_owner_moved_road_gate()
{
    /* FAILS BEFORE (T756): the notebook road's gate (store.cpp StoreLiveRoadLocal) took OWNER_MOVED only at 8 bytes (LivePairDecode),
       so every 12-byte hand-over word was dropped as malformed. [m7a2f3-t0] */
    using namespace cooplive;
    std::vector<char> b; OwnerMovedEncode(&b, 77, 5, 3);
    unsigned int a = 0, c = 0;
    if (b.size() != 12 || LivePairDecode(&b[0], b.size(), &a, &c)) Fail("fold 3: the old 8-byte gate would take the 12-byte form (the test does not show the defect)");
    if (!OwnerMovedRoadAccepted(&b[0], b.size(), 9)) Fail("fold 3: the road gate dropped a 12-byte OWNER_MOVED");
    std::vector<char> o; LivePairEncode(&o, 78, 6);
    if (!OwnerMovedRoadAccepted(&o[0], o.size(), 4)) Fail("fold 3: the road gate dropped an 8-byte legacy OWNER_MOVED");
    if (OwnerMovedRoadAccepted(&b[0], 10, 4)) Fail("fold 3: the road gate took a 10-byte OWNER_MOVED");
    if (OwnerMovedRoadAccepted(0, 0, 4)) Fail("fold 3: the road gate took an empty OWNER_MOVED");
    unsigned int u = 0, n = 0, pv = 0;
    if (!OwnerMovedDecode(&b[0], b.size(), 9, &u, &n, &pv) || u != 77 || n != 5 || pv != 3) Fail("fold 3: what the gate passed did not decode for the session layer");
}
void t_m7a2f_claims_for_not_asked_and_expired_copies()
{
    /* FAILS BEFORE (F2): a claim was taken only for a copy its owner's END withdrew - one the owner did not answer for, or a sweep nobody
       ended, dropped every claim. Item 3: the END names its not-asked copies; the timeout hands back the marks still open. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3);
    CatchupSweep s; std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(11);
    CatchupSweepOpen(&s, 5, uids, owners, keys, 0.0);
    CatchupSweepEndResult r;
    CatchupSweepEnd(&s, 5, k3, std::vector<int>(), std::vector<unsigned int>(), &r);
    if (r.notAsked.size() != 2 || r.keptNotAsked != 2 || !r.withdraw.empty()) Fail("item 3: an END that answered for no sector did not name its copies not asked");
    CatchupSweepOpen(&s, 6, uids, owners, keys, 0.0);
    std::map<unsigned int, CatchupSweepMark> left;
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec - 1.0, kCatchupAskTimeoutSec, &left) != 0 || !left.empty()) Fail("item 3: the sweep handed back marks before its timeout");
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec + 1.0, kCatchupAskTimeoutSec, &left) != 2 || left.size() != 2 || left[100].owner != k3 || !s.marked.empty())
        Fail("item 3: the sweep's timeout did not hand back its open marks with their owners");
    size_t dropped = 9;
    if (SweepClaimPick(std::vector<unsigned int>(1, RelayPeerId(4)), 0, &dropped) != 0 || dropped != 0) Fail("item 3: a lone claim was not the one taken");
}
void t_m7a2f_claim_conflict()
{
    /* FAILS BEFORE (F3): one claim per copy, overwritten by every later SPAWN - the later claim silently beat the earlier one, and a claim
       from a player that had since handed the character on stayed. Item 4. */
    using namespace cooplive;
    const unsigned int k4 = RelayPeerId(4), k5 = RelayPeerId(5);
    std::vector<unsigned int> who;
    if (SweepClaimSlot(who, k4, kSweepClaimsPerUid) != 0) Fail("item 4: a first claim was not kept");
    who.push_back(k4);
    if (SweepClaimSlot(who, k4, kSweepClaimsPerUid) != 0) Fail("item 4: a repeated claim from the same player was added beside its first");
    if (SweepClaimSlot(who, k5, kSweepClaimsPerUid) != 1) Fail("item 4: a second player's claim was not kept beside the first");
    who.push_back(k5);
    size_t dropped = 0;
    if (SweepClaimPick(who, 0, &dropped) != 0 || dropped != 1) Fail("item 4: with no player named, the later claim beat the earlier one, or the other was not counted");
    if (SweepClaimPick(who, k5, &dropped) != 1 || dropped != 1) Fail("item 4: the claim of the player the old owner named was not the one taken");
    if (SweepClaimPick(who, RelayPeerId(6), &dropped) != -1 || dropped != 2) Fail("item 4: a claim was taken although the old owner named another player");
    std::vector<unsigned int> full;
    for (size_t i = 0; i < kSweepClaimsPerUid; ++i) full.push_back(RelayPeerId(10 + (unsigned int)i));
    if (SweepClaimSlot(full, k5, kSweepClaimsPerUid) != -1) Fail("item 4: the claims on one copy grew past their bound");
}
void t_m7a2f_expired_marks_withdrawn_and_not_asked_restored()
{
    /* FAILS BEFORE (F8): a mark no END took was DROPPED at the timeout and its copy kept (a ghost when its owner was never asked); a copy
       outside the sectors its owner was asked about was kept although that owner answered for others. Item 5. */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3);
    CatchupSweep s; std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(11);
    uids.push_back(102); owners.push_back(k3); keys.push_back(11);
    CatchupSweepOpen(&s, 5, uids, owners, keys, 0.0);
    CatchupSweepEndResult r;
    CatchupSweepEnd(&s, 5, k3, std::vector<int>(1, 10), std::vector<unsigned int>(1, 102), &r);
    if (r.withdraw.size() != 2 || r.withdrawnNotAsked != 1 || r.keptNotAsked != 0 || r.keptHeld != 1) Fail("item 5: the END did not withdraw the not-asked copy (and keep only the held one)");
    CatchupSweepOpen(&s, 6, uids, owners, keys, 0.0);
    std::map<unsigned int, CatchupSweepMark> left;
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec + 0.5, kCatchupAskTimeoutSec, &left) != 3 || left.size() != 3) Fail("item 5: the marks still open at the timeout were not handed back to be withdrawn");
}
void t_m7a2f_ask_list_reset_per_link()
{
    /* FAILS BEFORE (F5): the last-64 answered list outlived the world-server link; a restarted world server numbers its asks from 0 again,
       so its first asks were taken for repeats and never answered. Item 6. */
    using namespace cooplive;
    std::vector<unsigned long long> rec; long gen = 0;
    if (!CatchupAnswerFirstOnLink(&rec, &gen, 1, 2, 0)) Fail("item 6: a first ASK was taken for a repeat");
    if (CatchupAnswerFirstOnLink(&rec, &gen, 1, 2, 0)) Fail("item 6: a repeated ASK on the same link was answered twice");
    if (!CatchupAnswerFirstOnLink(&rec, &gen, 2, 2, 0)) Fail("item 6: ask 0 from a restarted world server (a new link) was taken for a repeat");
    if (rec.size() != 1 || gen != 2) Fail("item 6: the answered list was not cleared at the new link");
}
void t_m7a2f_missed_stamp_one_off_only()
{
    /* FAILS BEFORE (F7): any dropped AREA message erased the sector's stamp - a MOVE dropped at a sector-edge flicker forced a full
       catch-up of that sector. Item 7: only a one-off kind's miss erases it. */
    using namespace cooplive;
    if (!AreaMissedIsOneOff(kInnerSpawn) || !AreaMissedIsOneOff(kInnerDespawn) || !AreaMissedIsOneOff(kInnerUnload) || !AreaMissedIsOneOff(kInnerSwing)
        || !AreaMissedIsOneOff(kInnerCombatMode) || !AreaMissedIsOneOff(kInnerOwnerMoved)) Fail("item 7: a one-off kind's miss did not erase the stamp");
    if (AreaMissedIsOneOff(kInnerMove) || AreaMissedIsOneOff(kInnerState) || AreaMissedIsOneOff(kInnerIntent)) Fail("item 7: a repeating MOVE / STATE / INTENT miss erased the stamp");
}
void t_m7a2f2_silent_owner_keeps_its_copies()
{
    /* FAILS BEFORE (fold 2, re-check of fold 1): every mark open at the timeout was withdrawn - also the copies of an owner that was never
       asked (it sends no SPAWN), which then stayed gone until the sector left and re-entered. [m7a2f2-t0] */
    using namespace cooplive;
    const unsigned int k3 = RelayPeerId(3), k4 = RelayPeerId(4), k5 = RelayPeerId(5);
    CatchupSweep s; std::vector<unsigned int> uids, owners; std::vector<int> keys;
    uids.push_back(100); owners.push_back(k3); keys.push_back(10);
    uids.push_back(101); owners.push_back(k3); keys.push_back(10);
    uids.push_back(200); owners.push_back(k4); keys.push_back(10);
    CatchupSweepOpen(&s, 5, uids, owners, keys, 0.0);
    CatchupSweepSeen(&s, 100, k3);   /* slot 3 spoke: re-sent 100, not 101 */
    std::map<unsigned int, CatchupSweepMark> left;
    if (CatchupSweepExpire(&s, kCatchupAskTimeoutSec + 1.0, kCatchupAskTimeoutSec, &left) != 2) Fail("fold 2: the timeout did not hand back the two open marks");
    if (!SweepExpiryWithdraws(s, k3)) Fail("fold 2: a heard owner's copy it did not re-send was kept at the timeout");
    if (SweepExpiryWithdraws(s, k4)) Fail("fold 2: a silent owner's copy was withdrawn at the timeout");
    CatchupSweepSeen(&s, 999, k5);
    if (!SweepExpiryWithdraws(s, k5)) Fail("fold 2: a stream message from a player was not counted as hearing it");
    CatchupSweepOpen(&s, 6, uids, owners, keys, 0.0);
    if (SweepExpiryWithdraws(s, k3)) Fail("fold 2: a new sweep kept the old sweep's heard players");
    CatchupSweepEndResult r;
    CatchupSweepEnd(&s, 6, k4, std::vector<int>(1, 10), std::vector<unsigned int>(), &r);
    if (!SweepExpiryWithdraws(s, k4)) Fail("fold 2: an owner's END did not count as hearing it");
}
void t_m7a2_missed_stamp()
{
    /* FAILS BEFORE (item 8): a sector out of a game's delivery set for under kCatchupRejoinSec kept its stamp, so on its return it was
       not newly delivered - the AREA messages the notebook dropped for that game meanwhile were never caught up. */
    using namespace cooplive;
    const int k = AreaKey(10, 10), other = AreaKey(20, 20);
    std::map<int, double> st, st2; std::set<int> loaded, del; std::vector<int> fresh;
    loaded.insert(k);
    AreaReportNewlyIn(&st, loaded, 0, 0.0, kCatchupRejoinSec, &del, &fresh); st2 = st;
    loaded.clear(); loaded.insert(other);
    AreaReportNewlyIn(&st, loaded, 0, 1.0, kCatchupRejoinSec, &del, &fresh);
    AreaReportNewlyIn(&st2, loaded, 0, 1.0, kCatchupRejoinSec, &del, &fresh);
    if (AreaStampMissed(&st, &del, other)) Fail("item 8: a sector in the delivery set was marked missed");
    if (!AreaStampMissed(&st, &del, k)) Fail("item 8: a message dropped for a recently left sector did not mark it missed");
    loaded.insert(k);
    AreaReportNewlyIn(&st, loaded, 0, 2.0, kCatchupRejoinSec, &del, &fresh);
    if (fresh.size() != 1 || fresh[0] != k) Fail("item 8: a missed sector back within the rejoin window was not caught up");
    AreaReportNewlyIn(&st2, loaded, 0, 2.0, kCatchupRejoinSec, &del, &fresh);
    if (!fresh.empty()) Fail("item 8: a sector back within the rejoin window that missed nothing was caught up (the flicker rule broke)");
}
void t_m7af_relayed_spawn_owner_gate()
{
    /* FAILS BEFORE (F4): OnSpawn wrote the owner record for any relayed SPAWN, so any game could take another player's character. */
    if (!cooplive::RelayedSpawnRefused(true, false)) Fail("F4: a relayed SPAWN from a player that is not the recorded owner passed");
    if (cooplive::RelayedSpawnRefused(true, true)) Fail("F4: the recorded owner's own re-sent SPAWN was refused");
    if (cooplive::RelayedSpawnRefused(false, false)) Fail("F4: a first SPAWN (no owner recorded) was refused");
}
/* ---- M5b (T-197 piece 5): OWNERSHIP BY PLAYER NUMBER ---------------------------------------------------------------- */
void t_m5b_relayed_owner_slot_accepted_other_refused()
{
    /* FAILS BEFORE: g_owner held the session peer's TRANSPORT id (0) and RemoteMayWrite compared it raw with the sender, so the
       owning player's own message through the notebook (0x80000000 | 3) was refused like anybody's, and the relay dispatched
       only RELATION / RELSYNC, so a MOVE never reached the check. */
    const int link = 3;                                                   /* the session peer said slot 3 (PEER_SLOT) */
    const unsigned int rec = cooplive::PlayerKeyOf(0u, link);            /* OnSpawn from the session link */
    if (rec == 0u) Fail("a session-road owner record was not keyed by the peer's slot");
    if (!cooplive::SamePlayer(rec, cooplive::RelayPeerId(3), link)) Fail("the owning slot's message through the notebook was refused");
    if (cooplive::SamePlayer(rec, cooplive::RelayPeerId(4), link)) Fail("another slot's message through the notebook passed as the owner");
    if (!cooplive::SamePlayer(rec, 0u, link)) Fail("the session peer's own message on the session link was refused");
    const unsigned int relRec = cooplive::PlayerKeyOf(cooplive::RelayPeerId(4), link);   /* a record made through the notebook from slot 4 */
    if (!cooplive::SamePlayer(relRec, cooplive::RelayPeerId(4), link)) Fail("slot 4's own relayed message was refused");
    if (cooplive::SamePlayer(relRec, 0u, link)) Fail("the session peer (slot 3) passed as the owner of slot 4's character");
    if (!cooplive::LiveInnerAccepted(cooplive::kInnerMove) || !cooplive::LiveInnerAccepted(cooplive::kInnerState)) Fail("an owner-checked character update is still refused at the relay");
}
void t_m5b_record_before_slot_known()
{
    const unsigned int rec = cooplive::PlayerKeyOf(0u, -1);
    if (rec != 0u) Fail("with no slot known the record was not the transport id");
    if (!cooplive::SamePlayer(rec, 0u, -1)) Fail("no notebook: the session peer's message was refused");
    if (cooplive::SamePlayer(rec, cooplive::RelayPeerId(0), -1)) Fail("no slot known: a relayed slot 0 passed as the session peer (transport id 0)");
    if (!cooplive::SamePlayer(rec, cooplive::RelayPeerId(7), 7)) Fail("an early record did not match its player once the slot was said");
    if (cooplive::SamePlayer(rec, cooplive::RelayPeerId(8), 7)) Fail("an early record matched another player");
}
void t_p25f1_request_only_to_session_peer()   /* [p25f1-t] */
{
    /* FAILS BEFORE: B's drain sent a REQUEST for any claimed NPC while SendTalk reaches only the session peer - a third game's NPC
       (its owner recorded through the notebook) waited 30 s for an answer that could never come. */
    if (!cooplive::IsSessionPeerKey(cooplive::PlayerKeyOf(0u, 3), 3)) Fail("slot known: the session peer's own record was not the session peer");
    if (!cooplive::IsSessionPeerKey(cooplive::RelayPeerId(3), 3)) Fail("slot known: the session slot's relayed record was not the session peer");
    if (!cooplive::IsSessionPeerKey(0u, 3)) Fail("slot known: an early raw record from the session link was not the session peer");
    if (cooplive::IsSessionPeerKey(cooplive::RelayPeerId(4), 3)) Fail("slot known: a third game's (slot 4) record passed as the session peer");
    if (!cooplive::IsSessionPeerKey(0u, -1)) Fail("no slot known: the session link's raw record was not the session peer");
    if (cooplive::IsSessionPeerKey(cooplive::RelayPeerId(0), -1)) Fail("no slot known: a relayed record passed as the session peer");
}
void t_m5b_player_keys_never_alias()
{
    /* S2-67: every slot a u16 holds has its own key; a key is never re-keyed; a slot beyond a u16 is never folded into another */
    for (unsigned int s = 0; s <= cooplive::kLiveSlotMax; ++s)
    {
        const unsigned int k = cooplive::PlayerKeyOf(0u, (int)s);
        if (k != cooplive::RelayPeerId(s) || cooplive::RelayPeerSlot(k) != s) { Fail("a slot's player key lost its number"); break; }
        if (cooplive::PlayerKeyOf(k, 5) != k) { Fail("a player key was re-keyed"); break; }
        if (s > 0 && cooplive::SamePlayer(k, cooplive::RelayPeerId(s - 1), (int)s)) { Fail("two neighbouring slots are one player"); break; }
    }
    if (cooplive::PlayerKeyOf(0u, 70000) != 0u) Fail("a slot beyond a u16 was folded into another slot's key");
    if (cooplive::PlayerKeyOf(0u, (int)cooplive::kLiveSlotMax + 1) != 0u) Fail("slot 65536 was folded into slot 0");
}
void t_m5b_relation_pair_owner_slot()
{
    /* FAILS BEFORE: ApplyRemoteRelation took any pair from any sender, so slot 5 could write slot 3's standing. */
    if (coopslot::PairOwnerSlot("@slot:3:Bob", "HolyNation") != 3) Fail("a player's own standing was not its");
    if (coopslot::PairOwnerSlot("HolyNation", "@slot:5") != 5) Fail("an NPC's standing towards a player was not that player's");
    if (coopslot::PairOwnerSlot("@slot:3:Bob", "@slot:5") != 3) Fail("a player-to-player pair was not its owner side's");
    if (coopslot::PairOwnerSlot("@slot:?:Bob", "@slot:5") != -1) Fail("an unnumbered owner side was guessed");
    if (coopslot::PairOwnerSlot("HolyNation", "Shek") != -1) Fail("a world pair named a player");
    const int sender = 5, owning = coopslot::PairOwnerSlot("@slot:3:Bob", "HolyNation");
    if (!(sender >= 0 && owning >= 0 && owning != sender)) Fail("slot 5's copy of slot 3's standing was not refused");
}
/* M5b fold 1 (review 2026-09-30 item 1): the ready stamp is the generation AFTER this Poll's edges */
void t_m5bf1_link_gen_after_batch()
{
    /* FAILS BEFORE: a HELLO dispatched in the same Poll as its CONNECT stamped SessionLinkGen() - one short of the value the
       link-up below the loop leaves - so PEER_SLOT was never sent and the escrow ready flag was counted stale. */
    if (coopgl::LinkGenAfterBatch(4, 7u, 7u, 1, 1) != 4) Fail("m5bf1: no edge pending -> the generation as it is");
    if (coopgl::LinkGenAfterBatch(4, 8u, 7u, 1, 0) != 5) Fail("m5bf1: a first CONNECT in this Poll -> one UP");
    if (coopgl::LinkGenAfterBatch(4, 8u, 7u, 1, 1) != 6) Fail("m5bf1: a DISCONNECT + CONNECT in one Poll -> DOWN and UP");
    if (coopgl::LinkGenAfterBatch(4, 7u, 7u, 0, 1) != 5) Fail("m5bf1: a plain drop -> one DOWN");
    if (coopgl::LinkGenAfterBatch(4, 8u, 7u, 0, 1) != 5) Fail("m5bf1: a counter move with the link now down -> one DOWN, not two");
    if (coopgl::LinkGenAfterBatch(4, 8u, 7u, 0, 0) != 4) Fail("m5bf1: a counter move while already down and still down -> nothing");
    if (coopgl::LinkGenAfterBatch(4, 7u, 7u, 1, 0) != 5) Fail("m5bf1: UP by state alone -> one UP");
}
void t_m5a_live_sender_is_the_notebooks_stamp()
{
    /* FAILS BEFORE: the session link's message carries no origin at all - a RELATION relayed without the stamp would name
       the notebook's link as its sender. The up frame has NO sender field; the down frame's origin is whatever the notebook
       stamped, whatever the up frame's target said. */
    const std::vector<char> inner = M5aBytes(9);
    std::vector<char> up; cooplive::LiveUp u;
    cooplive::LiveUpEncode(&up, cooplive::kRouteSlot, 9u   /* M5a fold 1: WORLD now refuses a non-zero target; a SLOT frame still names one */, 32, &inner[0], inner.size());
    if (cooplive::LiveUpDecode(&up[0], up.size(), &u) != cooplive::kLiveFwd) { Fail("the up frame did not decode"); return; }
    std::vector<char> down; cooplive::LiveDown d;
    if (!cooplive::LiveDownEncode(&down, 5, u.innerType, &up[u.innerAt], u.innerLen) || !cooplive::LiveDownDecode(&down[0], down.size(), &d)) { Fail("the stamped frame did not round-trip"); return; }
    if (d.originSlot != 5 || cooplive::RelayPeerId(d.originSlot) != (cooplive::kRelayPeerBase | 5u)) Fail("the delivered sender is not the notebook's stamp");
    if (!M5aSame(&down[d.innerAt], &inner[0], inner.size())) Fail("the inner payload changed through the notebook");
}

/* ---- M6 (T-197 piece 6): PER-AREA DELIVERY AND THE CATCH-UP (src/common/liverelay.h) ------------------------------------ */
std::set<int> M6Set(int x, int y) { std::set<int> l; l.insert(cooplive::AreaKey(x, y)); std::set<int> d; cooplive::AreaDilate(l, cooplive::kAreaMargin, &d); return d; }
void t_m6_area_route_right_games_only()
{
    /* THE ROUTING DECISION. A (slot 0) has 10,10 loaded, B (slot 1) 11,10, C (slot 2) is at the title (no report), D (slot 3)
       has 12,10. FAILS BEFORE: M5a's LiveRouteDecide refuses AREA outright, so B never got anything. */
    const std::set<int> dA = M6Set(10, 10), dB = M6Set(11, 10), dD = M6Set(12, 10);
    std::vector<int> c; c.push_back(0); c.push_back(1); c.push_back(2); c.push_back(3);
    std::vector<const std::set<int>*> s; s.push_back(&dA); s.push_back(&dB); s.push_back(0); s.push_back(&dD);
    std::vector<size_t> d;
    if (cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(10, 10), -1), 0, c, s, &d) != cooplive::kLiveFwd) { Fail("an AREA probe was refused"); return; }
    if (d.size() != 1 || d[0] != 1) Fail("A's probe at 10,10 did not go to B alone (not A itself, not C at the title, not D two squares away)");
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(11, 10), -1), 1, c, s, &d);
    if (d.size() != 2 || d[0] != 0 || d[1] != 3) Fail("B's probe at 11,10 did not go to exactly A and D");
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(40, 40), -1), 0, c, s, &d);
    if (!d.empty()) Fail("a sector nobody has in their delivery area was delivered");
    std::vector<int> lob; lob.push_back(-1); lob.push_back(1); std::vector<const std::set<int>*> ls; ls.push_back(&dB); ls.push_back(&dB);
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(11, 10), -1), 0, lob, ls, &d);
    if (d.size() != 1 || d[0] != 1) Fail("a lobby connection (no slot) was a destination");
    if (cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(0, -1), -1, c, s, &d) != cooplive::kLiveNoSlot || !d.empty()) Fail("a sender with no slot was not noSlot");
    if (cooplive::LiveRouteDecide(cooplive::kRouteArea, 0, 0, c, &d) != cooplive::kLiveBadRoute) Fail("LiveRouteDecide now decides AREA - the notebook must ask LiveAreaRouteDecide");
}
void t_m6_area_margin_and_crossing()
{
    const std::set<int> g = M6Set(20, 20);
    std::vector<int> c; c.push_back(0); c.push_back(7);
    std::vector<const std::set<int>*> s; s.push_back(&g); s.push_back(&g);
    std::vector<size_t> d;
    const int ring[8][2] = { {19,19},{20,19},{21,19},{19,20},{21,20},{19,21},{20,21},{21,21} };
    for (int i = 0; i < 8; ++i)
    {
        cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(ring[i][0], ring[i][1]), -1), 0, c, s, &d);
        if (d.size() != 1 || d[0] != 1) { Fail("a sector one ring from the loaded area was not delivered (the margin)"); return; }
    }
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(22, 20), -1), 0, c, s, &d);
    if (!d.empty()) Fail("a sector two squares out was delivered");
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(22, 20), cooplive::AreaKey(21, 20)), 0, c, s, &d);
    if (d.size() != 1 || d[0] != 1) Fail("the message that carries a character out of the margin did not reach the game it left");
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(22, 20), cooplive::AreaKey(22, 20)), 0, c, s, &d);
    if (!d.empty()) Fail("a character already outside the margin was still delivered");
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(20, 20), -1), 7, c, s, &d);
    if (d.size() != 1 || d[0] != 0) Fail("the origin was sent its own AREA message");
}
void t_m6_area_target_wire()
{
    for (int k = 0; k < cooplive::kAreaKeyCount; ++k)
    {
        int key = -9, prev = -9;
        if (!cooplive::AreaTargetDecode(cooplive::AreaTarget(k, -1), &key, &prev) || key != k || prev != -1) { Fail("a sector did not round-trip with no previous sector"); return; }
        const int pk = (k * 37) % cooplive::kAreaKeyCount;
        if (!cooplive::AreaTargetDecode(cooplive::AreaTarget(k, pk), &key, &prev) || key != k || prev != pk) { Fail("a sector and its previous one did not round-trip"); return; }
    }
    if (cooplive::AreaTarget(4096, -1) != cooplive::kAreaTargetNone || cooplive::AreaTarget(-1, -1) != cooplive::kAreaTargetNone || cooplive::AreaTarget(0, 4096) != cooplive::kAreaTargetNone
        || cooplive::AreaTarget(0, -2) != cooplive::kAreaTargetNone) Fail("a sector off the grid made a target");
    std::vector<int> c; c.push_back(0); c.push_back(1); std::vector<const std::set<int>*> s; std::vector<size_t> d;
    if (cooplive::LiveAreaRouteDecide(4096u, 0, c, s, &d) != cooplive::kLiveBadLen || cooplive::LiveAreaRouteDecide(0x10010000u, 0, c, s, &d) != cooplive::kLiveBadLen
        || cooplive::LiveAreaRouteDecide(cooplive::kAreaTargetNone, 0, c, s, &d) != cooplive::kLiveBadLen) Fail("an AREA target naming no sector was not badLen");
    if (cooplive::AreaKey(-1, 0) != -1 || cooplive::AreaKey(0, 64) != -1 || cooplive::AreaKey(64, 0) != -1 || cooplive::AreaKey(63, 63) != 4095) Fail("the sector key is not the notebook's y*64+x on 0..63");
    std::vector<char> up;
    if (!cooplive::LiveUpEncode(&up, cooplive::kRouteArea, cooplive::AreaTarget(100, 99), 200, "12345678", 8)) { Fail("an AREA envelope was refused by the M5a encoder"); return; }
    cooplive::LiveUp u;
    if (cooplive::LiveUpDecode(&up[0], up.size(), &u) != cooplive::kLiveFwd || u.route != (unsigned)cooplive::kRouteArea || u.target != cooplive::AreaTarget(100, 99)) Fail("an AREA envelope did not decode with its target");
}
void t_m6_area_dilate_edges()
{
    std::set<int> l, d; l.insert(cooplive::AreaKey(0, 0)); cooplive::AreaDilate(l, 1, &d);
    if (d.size() != 4) Fail("the corner's margin is not 4 sectors");
    l.clear(); l.insert(cooplive::AreaKey(63, 5)); cooplive::AreaDilate(l, 1, &d);
    if (d.size() != 6 || d.count(cooplive::AreaKey(0, 6)) != 0 || d.count(cooplive::AreaKey(0, 5)) != 0) Fail("the margin wrapped round the map's edge");
    l.clear(); l.insert(cooplive::AreaKey(30, 30)); l.insert(cooplive::AreaKey(31, 30)); cooplive::AreaDilate(l, 1, &d);
    if (d.size() != 12) Fail("two neighbouring loaded sectors did not make a 4x3 delivery area");
    cooplive::AreaDilate(l, 0, &d);
    if (d != l) Fail("margin 0 is not the loaded set itself");
}
void t_m6_area_slots_past_32()
{
    /* S2-67: no fixed number a real game could reach may break the game. The AREAMAP's loadedMask has 32 bits; the delivery
       sets have none - every slot up to the envelope's u16 is a destination like slot 1. */
    const std::set<int> g = M6Set(5, 5);
    const int slots[] = { 0, 31, 32, 33, 40, 255, 1023, 1024, 65520 };
    std::vector<int> c; std::vector<const std::set<int>*> s;
    for (int i = 0; i < 9; ++i) { c.push_back(slots[i]); s.push_back(&g); }
    std::vector<size_t> d;
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(5, 5), -1), 0, c, s, &d);
    if (d.size() != 8) { Fail("a slot at or past 32 was not a destination"); return; }
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(5, 5), -1), 65520, c, s, &d);
    if (d.size() != 8) Fail("a sender at slot 65520 was not routed");
    std::vector<char> b; std::vector<int> k; k.push_back(1);
    cooplive::CatchupMsg m;
    if (!cooplive::CatchupEncode(&b, cooplive::kCatchupAsk, 65520, 1, k) || !cooplive::CatchupDecode(&b[0], b.size(), &m) || m.slot != 65520) Fail("a CATCHUP ASK for slot 65520 did not round-trip");
}
void t_m6_catchup_newly_and_intersect()
{
    std::set<int> before, after; before.insert(1); before.insert(2); after.insert(2); after.insert(3); after.insert(4);
    std::vector<int> n; cooplive::AreaNewlyIn(before, after, &n);
    if (n.size() != 2 || n[0] != 3 || n[1] != 4) Fail("the sectors newly in the delivery area are not after minus before");
    std::set<int> none; cooplive::AreaNewlyIn(none, after, &n);
    if (n.size() != 3) Fail("a first report (or a stale one) did not make everything new");
    cooplive::AreaNewlyIn(after, after, &n);
    if (!n.empty()) Fail("an unchanged report asked for a catch-up");
    std::set<int> ownerLoaded; ownerLoaded.insert(4); ownerLoaded.insert(9);
    std::vector<int> fresh; fresh.push_back(3); fresh.push_back(4); std::vector<int> x;
    cooplive::AreaIntersect(fresh, ownerLoaded, &x);
    if (x.size() != 1 || x[0] != 4) Fail("an owner was asked about sectors it does not have loaded");
}
void t_m6_catchup_wire()
{
    std::vector<int> k; k.push_back(0); k.push_back(4095); k.push_back(700);
    std::vector<char> b; cooplive::CatchupMsg m;
    if (!cooplive::CatchupEncode(&b, cooplive::kCatchupAsk, 3, 77, k)) { Fail("a CATCHUP ASK was refused"); return; }
    if (b.size() != cooplive::kCatchupHeader + 6 || !cooplive::CatchupDecode(&b[0], b.size(), &m)) { Fail("a CATCHUP ASK did not decode"); return; }
    if (m.kind != (unsigned)cooplive::kCatchupAsk || m.slot != 3 || m.askNo != 77 || m.keys != k) Fail("a CATCHUP ASK's fields changed on the way");
    std::vector<char> a; std::vector<int> none;
    if (!cooplive::CatchupEncode(&a, cooplive::kCatchupAsked, 0, 78, none) || !cooplive::CatchupDecode(&a[0], a.size(), &m) || m.kind != (unsigned)cooplive::kCatchupAsked || m.slot != 0 || !m.keys.empty())
        Fail("an ASKED with no owners and no sectors did not round-trip");
    if (cooplive::CatchupDecode(&b[0], b.size() - 1, &m) || cooplive::CatchupDecode(&b[0], 11, &m)) Fail("a truncated CATCHUP decoded");
    std::vector<char> bad = b; bad[1] = 1; if (cooplive::CatchupDecode(&bad[0], bad.size(), &m)) Fail("a CATCHUP with a non-zero pad decoded");
    bad = b; bad[0] = 3; if (cooplive::CatchupDecode(&bad[0], bad.size(), &m)) Fail("an unknown CATCHUP kind decoded");
    bad = b; bad[12] = (char)0x00; bad[13] = (char)0x10; if (cooplive::CatchupDecode(&bad[0], bad.size(), &m)) Fail("a CATCHUP sector off the grid decoded");
    std::vector<char> r; std::vector<int> off; off.push_back(4096);
    if (cooplive::CatchupEncode(&r, cooplive::kCatchupAsk, 1, 1, off) || cooplive::CatchupEncode(&r, 9, 1, 1, k) || cooplive::CatchupEncode(&r, cooplive::kCatchupAsk, 65536, 1, k)) Fail("a bad CATCHUP was encoded");
    std::vector<int> many(cooplive::kCatchupKeysMax + 1, 1);
    if (cooplive::CatchupEncode(&r, cooplive::kCatchupAsk, 1, 1, many) || !r.empty()) Fail("a CATCHUP over the sector bound was encoded, or a refused encode appended bytes");
    std::vector<char> e; unsigned int x = 0, y = 0; int key = -1;
    { cooplive::CatchupEndMsg em; em.askNo = 77; em.count = 12; cooplive::CatchupEndEncode(&e, em); }   /* M7a2 [m7a2-t2]: the protocol 112 END */
    { cooplive::CatchupEndMsg dm; if (!cooplive::CatchupEndDecode(&e[0], e.size(), &dm) || dm.askNo != 77 || dm.count != 12 || cooplive::CatchupEndDecode(&e[0], 7, &dm)) Fail("CATCHUP_END did not round-trip, or a short one decoded"); }
    std::vector<char> p; cooplive::LiveProbeEncode(&p, 5, 4095);
    if (!cooplive::LiveProbeDecode(&p[0], p.size(), &x, &key) || x != 5 || key != 4095) Fail("a PROBE did not round-trip");
    std::vector<char> q; cooplive::LivePairEncode(&q, 5, 4096);
    if (cooplive::LiveProbeDecode(&q[0], q.size(), &x, &key)) Fail("a PROBE naming no sector decoded");
}
/* ---- M8 (T-197 piece 8): PER-PLAYER LEAVE (src/common/peergone.h) ------------------------------------------------------------ */
/* A = this game (two rows it authored, keyed by its raw id 0), B = slot 1 (two rows), C = slot 2 (three rows) */
cooppg::OwnerMap M8Owners(cooppg::UidSet* local)
{
    cooppg::OwnerMap o;
    local->clear();
    o[1] = 0u; o[2] = 0u; local->insert(1); local->insert(2);
    o[10] = cooplive::RelayPeerId(1); o[11] = cooplive::RelayPeerId(1);
    o[20] = cooplive::RelayPeerId(2); o[21] = cooplive::RelayPeerId(2); o[22] = cooplive::RelayPeerId(2);
    return o;
}
void t_m8_peer_gone_select_only_that_slot()
{
    cooppg::UidSet local; const cooppg::OwnerMap o = M8Owners(&local);
    std::vector<unsigned int> got; cooppg::PeerGoneSelect(o, local, 2, &got);
    if (got.size() != 3 || got[0] != 20 || got[1] != 21 || got[2] != 22) Fail("PLAYER_GONE for C (slot 2) did not select exactly C's three uids");
    std::vector<unsigned int> old; cooppg::PeerGoneSelectNotMine(o, local, &old);
    if (old.size() != 5 || old[0] != 10 || old[1] != 11) Fail("the pre-M8 rule (everything not mine) no longer selects B's and C's five uids for C leaving - the test lost its before");
    if (old == got) Fail("the pre-M8 rule and M8's agree for C leaving - B's characters would go with C's");
    std::vector<unsigned int> none; cooppg::PeerGoneSelect(o, local, 7, &none); cooppg::PeerGoneSelect(o, local, 65536, &none);
    if (!none.empty()) Fail("a slot holding nothing (or past a u16) selected uids");
    local.insert(21); std::vector<unsigned int> g2; cooppg::PeerGoneSelect(o, local, 2, &g2);
    if (g2.size() != 2) Fail("a uid this game runs was selected for another player's leave");
}
void t_m8_two_roads_remove_each_uid_once()
{
    for (int order = 0; order < 2; ++order)   /* 0: PLAYER_GONE first, then the session road; 1: the other way round */
    {
        cooppg::UidSet local; cooppg::OwnerMap o = M8Owners(&local);
        o[30] = 5u;   /* one of B's rows still keyed by the raw session-link id (its PEER_SLOT not yet applied to it) */
        cooppg::OwnerMap pending;
        const int cap = cooppg::PeerGoneSessionCapture(o, local, 1, &pending);
        if (cap != 3 || pending.size() != 3 || pending.count(10) != 1 || pending.count(11) != 1 || pending.count(30) != 1) Fail("the session DOWN edge did not capture exactly B's rows (its slot's and the raw-keyed one)");
        std::map<unsigned int, int> times; int removed = 0;
        for (int step = 0; step < 2; ++step)
        {
            if ((step == 0) == (order == 0))
            {
                std::vector<unsigned int> g; cooppg::PeerGoneSelect(o, local, 1, &g);
                for (size_t i = 0; i < g.size(); ++i) { o.erase(g[i]); pending.erase(g[i]); ++times[g[i]]; ++removed; }
            }
            else
            {
                for (cooppg::OwnerMap::const_iterator it = pending.begin(); it != pending.end(); ++it)
                    if (cooppg::PeerGoneStillTheirs(o, local, it->first, it->second)) { o.erase(it->first); ++times[it->first]; ++removed; }
                pending.clear();
            }
        }
        if (removed != 3 || times[10] != 1 || times[11] != 1 || times[30] != 1) Fail("B's rows were not removed exactly once each across the two roads");
        if (o.size() != 5 || o.count(20) != 1 || o.count(21) != 1 || o.count(22) != 1 || o.count(1) != 1 || o.count(2) != 1) Fail("the two roads removed a row that was not B's");
    }
    cooppg::UidSet local; cooppg::OwnerMap o = M8Owners(&local); cooppg::OwnerMap pending;
    cooppg::PeerGoneSessionCapture(o, local, 1, &pending);
    o[10] = cooplive::RelayPeerId(2);   /* handed to C after the DOWN edge */
    if (cooppg::PeerGoneStillTheirs(o, local, 10, pending[10])) Fail("a row handed on after the DOWN edge was still read as the departed peer's");
    if (!cooppg::PeerGoneStillTheirs(o, local, 11, pending[11])) Fail("an untouched captured row was not the departed peer's");
    cooppg::OwnerMap p2; o = M8Owners(&local); o[30] = 5u;
    cooppg::PeerGoneSessionCapture(o, local, -1, &p2);
    if (p2.size() != 1 || p2.count(30) != 1) Fail("with the link peer's slot unknown the capture took rows keyed to a player");
}
void t_m8_player_gone_wire()
{
    std::vector<char> b; unsigned int s = 0;
    if (!cooppg::PlayerGoneEncode(&b, 40000) || b.size() != cooppg::kPlayerGoneBytes || !cooppg::PlayerGoneDecode(&b[0], b.size(), &s) || s != 40000) Fail("PLAYER_GONE did not round-trip slot 40000");
    if (cooppg::PlayerGoneDecode(&b[0], 1, &s)) Fail("a short PLAYER_GONE decoded");
    std::vector<char> l = b; l.push_back(0); if (cooppg::PlayerGoneDecode(&l[0], l.size(), &s)) Fail("a long PLAYER_GONE decoded");
    std::vector<char> r; if (cooppg::PlayerGoneEncode(&r, 65536) || !r.empty()) Fail("a slot past a u16 was encoded, or a refused encode appended bytes");
    if (cooppg::kMsgPlayerGone != 57) Fail("PLAYER_GONE is not world-server message 57");
}
/* M8 review F9: the session road's take, the loaded-bit rule, the session-peer skip and the world server's hold - the real rules */
void t_m8f1_take_pending()
{
    cooppg::UidSet local; cooppg::OwnerMap o = M8Owners(&local); cooppg::OwnerMap pending;
    o[30] = 5u;
    cooppg::PeerGoneSessionCapture(o, local, 1, &pending);   /* B's 10 and 11 (slot 1) and 30 (still raw) */
    o.erase(11);                                              /* PLAYER_GONE removed 11 first */
    o[30] = cooplive::RelayPeerId(2);                         /* 30 re-claimed by C since the edge */
    std::vector<unsigned int> uids;
    const long long gone = cooppg::PeerGoneTake(o, local, &pending, &uids);
    if (uids.size() != 1 || uids[0] != 10 || gone != 2) Fail("the session road's take did not return exactly the row still keyed as captured, with the other two counted alreadyGone");
    if (!pending.empty()) Fail("the session road's take left marks unspent");
    std::vector<unsigned int> none;
    if (cooppg::PeerGoneTake(o, local, &pending, &none) != 0 || !none.empty()) Fail("a second take found rows - a mark was spent twice");
    if (cooppg::PeerGoneTake(o, local, 0, &none) != 0 || !none.empty()) Fail("a take with no pending table returned rows");
}
void t_m8f1_mask_bits()
{
    /* M9 (T-197): the rule is SEAT-indexed and 256 wide - PeerGoneMaskBits(slot, seat, 8 words) */
    unsigned int b[8];
    if (cooppg::PeerGoneMaskBits(3, 3, b) != 1 || b[0] != 8u || b[1] != 0u) Fail("a known slot at seat 3 did not clear and arm exactly bit 3");
    if (cooppg::PeerGoneMaskBits(0, 0, b) != 1 || b[0] != 1u) Fail("seat 0 did not own bit 0");
    if (cooppg::PeerGoneMaskBits(40000, 40, b) != 1 || b[0] != 0u || b[1] != 0x100u) Fail("M9: slot 40000 at seat 40 did not own bit 40 (word 1, bit 8)");
    if (cooppg::PeerGoneMaskBits(40000, 255, b) != 1 || b[7] != 0x80000000u) Fail("M9: seat 255 did not own the last bit");
    if (cooppg::PeerGoneMaskBits(7, -1, b) != 0 || b[0] != 0u || b[7] != 0u) Fail("M9: a known slot with no column yet touched a bit (it is armed by slot at its first map)");
    if (cooppg::PeerGoneMaskBits(-1, -1, b) != 256 || b[0] != 0xFFFFFFFFu || b[7] != 0xFFFFFFFFu) Fail("an unknown slot did not keep the pre-M8 rule (every bit)");
}
/* ---- M9 (T-197): "who has it loaded", 256 wide (src/common/areadrop.h) ---------------------------------------------------------- */
void t_m9_seat40_survives()
{
    /* seat 40's carried bit survives the 256-wide rule. Pre-patch: the mask was ONE u32 and the world server set a bit only for
       slot < 32, so a 41st player had no bit to carry at all. */
    unsigned eff[8], nw[8], present[8];
    coopdrop::AreaMaskClear(eff); coopdrop::AreaMaskSet(eff, 40); coopdrop::AreaMaskSet(eff, 3);
    coopdrop::AreaMaskClear(nw); coopdrop::AreaMaskSet(nw, 3);
    coopdrop::AreaMaskClear(present); coopdrop::AreaMaskSet(present, 3);          /* seat 40 absent from this map */
    coopdrop::AreaEffectiveMaskWords(eff, nw, present);
    if (!coopdrop::AreaMaskTest(eff, 40)) Fail("M9: absent seat 40 lost its carried bit");
    if (!coopdrop::AreaMaskTest(eff, 3)) Fail("M9: present seat 3 lost its bit");
    coopdrop::AreaMaskSet(present, 40); coopdrop::AreaMaskClear(nw);                /* seat 40 present, sector not listed */
    coopdrop::AreaEffectiveMaskWords(eff, nw, present);
    if (coopdrop::AreaMaskTest(eff, 40) || coopdrop::AreaMaskTest(eff, 3)) Fail("M9: a present seat was not taken exactly (its clear bit stayed set)");
    const int preSlot40HadABit = (40 < 32) ? 1 : 0;
    if (preSlot40HadABit != 0) Fail("M9: the pre-patch guard is not what this test says it was");
}
void t_m9_rekey_3_to_9()
{
    int book[256], listed[256], src[256], out[256], lost = -1;
    for (int j = 0; j < 256; ++j) { book[j] = -1; listed[j] = -1; }
    book[3] = 40000;                       /* slot 40000 held seat 3 before the world server restarted */
    book[9] = 40005;                       /* slot 40005 held seat 9 and is ABSENT from the new map */
    listed[9] = 40000;                     /* the restarted world server seats slot 40000 at 9 */
    const int moved = coopdrop::AreaEffectiveRekey(book, listed, src, out, &lost);
    if (moved != 2 || lost != 0) Fail("M9: the rekey did not move exactly two columns (40000 3->9, displaced 40005 9->0)");
    if (src[9] != 3 || out[9] != 40000) Fail("M9: slot 40000's column did not follow it from seat 3 to seat 9");
    if (src[0] != 9 || out[0] != 40005) Fail("M9: the displaced absent slot 40005 did not move to the lowest free seat");
    if (out[3] != -1 || src[3] != -1) Fail("M9: seat 3 still names a column after its slot moved away");
    unsigned cell[8], after[8];
    coopdrop::AreaMaskClear(cell); coopdrop::AreaMaskSet(cell, 3); coopdrop::AreaMaskSet(cell, 9);
    coopdrop::AreaEffectiveRekeyCell(cell, src, after);
    if (!coopdrop::AreaMaskTest(after, 9) || !coopdrop::AreaMaskTest(after, 0) || coopdrop::AreaMaskTest(after, 3))
        Fail("M9: a cell's carried bits did not move 3->9 and 9->0");
    /* pre-patch equivalent: no rekey - bit 3 stays at 3, so after a restart seat 3 (someone else now) would read as holding it */
    for (int j = 0; j < 256; ++j) { book[j] = -1; listed[j] = -1; }
    book[5] = 7; listed[5] = 7;
    if (coopdrop::AreaEffectiveRekey(book, listed, src, out, &lost) != 0 || src[5] != 5) Fail("M9: an unchanged seat table moved a column");
    for (int j = 0; j < 256; ++j) { book[j] = 1000 + j; listed[j] = 5000 + j; }   /* 256 new slots and 256 absent ones: nowhere to go */
    coopdrop::AreaEffectiveRekey(book, listed, src, out, &lost);
    if (lost != 256) Fail("M9: with every seat listed the absent columns were not counted dropped");
}
void t_m9_wire_slot40000()
{
    int book[256];
    for (int j = 0; j < 256; ++j) book[j] = -1;
    book[0] = 40000; book[1] = 40001; book[40] = 65519;
    std::vector<char> b;
    if (coopdrop::AreaMapPutSeats(&b, book) != 3) Fail("M9: the seat table did not carry three seats");
    const size_t rc = b.size(); coopdrop::AreaPutU16(&b, 0u);
    unsigned m[8]; coopdrop::AreaMaskClear(m); coopdrop::AreaMaskSet(m, 0); coopdrop::AreaMaskSet(m, 40);
    coopdrop::AreaMapPutRow(&b, 43, 11, 40001, m);
    coopdrop::AreaMapPutRow(&b, 63, 63, -1, m);
    b[rc] = 2;
    if (b.size() != 2 + 9 + 2 + 2 * (5 + 2)) Fail("M9: the AREAMAP is not 2 + 3*seats + 2 + (5 + seats) per row bytes (M9f1)");
    int listed[256], seats = 0, rows = 0; size_t at = 0;
    if (!coopdrop::AreaMapLayout((const unsigned char*)&b[0], b.size(), listed, &seats, &at, &rows) || seats != 3 || rows != 2) Fail("M9: a well-formed map was refused");
    const int mySlot = 40000;
    const int mySeat = coopdrop::AreaSeatOfSlot(listed, mySlot);
    if (mySeat != 0) Fail("M9: slot 40000 did not find its seat in the map's table");
    const unsigned preMine = (mySlot >= 0 && mySlot < 32) ? 1u : 0u;   /* the pre-patch ApplyRelayAreaMap guard */
    if (preMine != 0u) Fail("M9: the pre-patch guard is not what this test says it was");
    int x = 0, y = 0, o = 0; unsigned rm[8];
    size_t cur = at;   /* M9f1: rows are read with a cursor */
    if (!coopdrop::AreaMapNextRow((const unsigned char*)&b[0], b.size(), &cur, &x, &y, &o, rm)) Fail("M9: row 0 did not read");
    if (x != 43 || y != 11 || o != 40001 || !coopdrop::AreaMaskTest(rm, mySeat) || !coopdrop::AreaMaskTest(rm, 40) || coopdrop::AreaMaskTest(rm, 1)) Fail("M9: row 0 did not read back");
    if (!coopdrop::AreaMapNextRow((const unsigned char*)&b[0], b.size(), &cur, &x, &y, &o, rm) || cur != b.size()) Fail("M9: row 1 did not read (M9f1)");
    if (x != 63 || y != 63 || o != -1) Fail("M9: a row nobody holds did not read back as owner -1");
    if (coopdrop::AreaMapLayout((const unsigned char*)&b[0], b.size() - 1, listed, &seats, &at, &rows)) Fail("M9: a truncated map was accepted");
    std::vector<char> d(b); d[5] = 0;   /* seat 1 -> seat 0: a seat listed twice */
    if (coopdrop::AreaMapLayout((const unsigned char*)&d[0], d.size(), listed, &seats, &at, &rows)) Fail("M9: a seat listed twice was accepted");
    std::vector<char> e(b); e[6] = (char)0x40; e[7] = (char)0x9C;   /* seat 1's slot -> 40000: a slot listed twice */
    if (coopdrop::AreaMapLayout((const unsigned char*)&e[0], e.size(), listed, &seats, &at, &rows)) Fail("M9: a slot listed twice was accepted");
}
void t_m9_seat_assign_257()
{
    int book[256];
    for (int j = 0; j < 256; ++j) book[j] = -1;
    std::vector<int> listed;
    for (int k = 0; k < 257; ++k) listed.push_back(40000 + k);
    int took = 0, freed = 0;
    if (coopdrop::AreaSeatAssign(book, listed, &took, &freed) != 1 || took != 256 || freed != 0) Fail("M9: 257 slots with marks did not seat 256 and leave one waiting");
    if (book[0] != 40000 || book[255] != 40255) Fail("M9: seats were not handed out lowest first");
    listed.erase(listed.begin() + 3);   /* slot 40003's marks aged out */
    if (coopdrop::AreaSeatAssign(book, listed, &took, &freed) != 0 || took != 1 || freed != 1 || book[3] != 40256) Fail("M9: the waiting slot did not take the freed seat");
    if (book[0] != 40000 || book[4] != 40004) Fail("M9: a slot that stayed listed changed seat");
}
/* ---- M9f1 (T-197): the M9 review - variable-length rows, the pending slots, the apply core (src/common/areadrop.h) ------------ */
static void M9f1Zero(unsigned* p, int n) { for (int i = 0; i < n; ++i) p[i] = 0u; }
static void M9f1Listed(int* listed, int s0, int l0, int s1, int l1) { for (int j = 0; j < 256; ++j) listed[j] = -1; if (s0 >= 0) listed[s0] = l0; if (s1 >= 0) listed[s1] = l1; }
void t_m9f1_row_varlen()
{
    int book[256];
    for (int j = 0; j < 256; ++j) book[j] = -1;
    book[0] = 7;
    std::vector<char> b;
    coopdrop::AreaMapPutSeats(&b, book);
    const size_t rc = b.size(); coopdrop::AreaPutU16(&b, 0u);
    const int want[5] = { 0, 2, 31, 32, 256 };
    const size_t bytes[5] = { 5, 7, 36, 37, 37 };
    unsigned m[5][8];
    for (int r = 0; r < 5; ++r)
    {
        coopdrop::AreaMaskClear(m[r]);
        if (want[r] == 256) { for (int k = 0; k < 256; ++k) coopdrop::AreaMaskSet(m[r], k); }
        else for (int k = 0; k < want[r]; ++k) coopdrop::AreaMaskSet(m[r], k * 8 + 1);
        const size_t before = b.size();
        coopdrop::AreaMapPutRow(&b, r, 60 - r, r == 0 ? -1 : 40000 + r, m[r]);
        if (b.size() - before != bytes[r]) Fail("M9f1: a row is not 5 + n bytes (n <= 31 seats) or 37 (32 or more)");
    }
    b[rc] = 5;
    int listed[256], seats = 0, rows = 0; size_t at = 0;
    const unsigned char* p = (const unsigned char*)&b[0];
    if (!coopdrop::AreaMapLayout(p, b.size(), listed, &seats, &at, &rows) || rows != 5 || seats != 1) Fail("M9f1: a well-formed variable-length map was refused");
    for (int r = 0; r < 5; ++r)
    {
        int x = -1, y = -1, o = 0; unsigned rm[8];
        if (!coopdrop::AreaMapNextRow(p, b.size(), &at, &x, &y, &o, rm)) Fail("M9f1: a row did not read");
        if (x != r || y != 60 - r || o != (r == 0 ? -1 : 40000 + r)) Fail("M9f1: a row's x, y or owner did not read back");
        for (int w = 0; w < 8; ++w) if (rm[w] != m[r][w]) Fail("M9f1: a row's seats did not read back");
    }
    if (at != b.size()) Fail("M9f1: the cursor did not end at the map's end");
    for (size_t cut = 1; cut <= 40; ++cut) if (coopdrop::AreaMapLayout(p, b.size() - cut, listed, &seats, &at, &rows)) Fail("M9f1: a truncated map was accepted");
    std::vector<char> d(b.begin(), b.begin() + (long)rc);   /* one row claiming 3 seats and carrying 1 */
    coopdrop::AreaPutU16(&d, 1u); d.push_back(1); d.push_back(1); d.push_back(0); d.push_back(0); d.push_back(3); d.push_back(9);
    if (coopdrop::AreaMapLayout((const unsigned char*)&d[0], d.size(), listed, &seats, &at, &rows)) Fail("M9f1: a seat list running past the end was accepted");
    std::vector<char> e(b.begin(), b.begin() + (long)rc);   /* the mask marker with 31 mask bytes */
    coopdrop::AreaPutU16(&e, 1u); e.push_back(1); e.push_back(1); e.push_back(0); e.push_back(0); e.push_back((char)0xFF);
    for (int k = 0; k < 31; ++k) e.push_back(0);
    if (coopdrop::AreaMapLayout((const unsigned char*)&e[0], e.size(), listed, &seats, &at, &rows)) Fail("M9f1: a short mask was accepted");
    e.push_back(0);
    if (!coopdrop::AreaMapLayout((const unsigned char*)&e[0], e.size(), listed, &seats, &at, &rows)) Fail("M9f1: a full mask row was refused");
    std::vector<char> f(b.begin(), b.begin() + (long)rc);   /* 4096 empty rows pass, 4097 do not */
    coopdrop::AreaPutU16(&f, 4096u);
    for (int k = 0; k < 4096; ++k) { f.push_back((char)(k % 64)); f.push_back((char)(k / 64)); f.push_back((char)0xFF); f.push_back((char)0xFF); f.push_back(0); }
    if (!coopdrop::AreaMapLayout((const unsigned char*)&f[0], f.size(), listed, &seats, &at, &rows) || rows != 4096) Fail("M9f1: 4096 rows were refused");
    f[rc] = 1; f[rc + 1] = 0x10; f.push_back(0); f.push_back(0); f.push_back((char)0xFF); f.push_back((char)0xFF); f.push_back(0);
    if (coopdrop::AreaMapLayout((const unsigned char*)&f[0], f.size(), listed, &seats, &at, &rows)) Fail("M9f1: 4097 rows were accepted");
}
void t_m9f1_pending_unlisted_dropped()
{
    coopdrop::AreaBook bk; coopdrop::AreaBookInit(&bk);
    unsigned cells[16], nw[16], none[8]; int listed[256]; unsigned char fl[2]; coopdrop::AreaApplyTally t;
    M9f1Zero(cells, 16); M9f1Zero(none, 8);
    if (coopdrop::AreaBookForget(&bk, 7, -1, none, 100.0) != 1 || bk.pendingCount != 1) Fail("M9f1: a slot with no column did not go pending");
    M9f1Listed(listed, 0, 3, -1, 0);   /* the first non-empty map lists only slot 3 */
    M9f1Zero(nw, 16); coopdrop::AreaMaskSet(nw, 0);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 101.0, cells, nw, 2, fl, &t);
    if (t.pendingDropped != 1 || t.pendingArmed != 0 || bk.pendingCount != 0) Fail("M9f1: a pending slot the map does not list was kept (M1)");
    M9f1Listed(listed, 0, 3, 1, 7);    /* slot 7 rejoins at seat 1; cell 1 is its */
    M9f1Zero(nw, 16); coopdrop::AreaMaskSet(nw, 0); coopdrop::AreaMaskSet(nw + 8, 1);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 102.0, cells, nw, 2, fl, &t);
    if (coopdrop::AreaMaskAny(bk.dropOnAbsence)) Fail("M9f1: the old news armed the rejoined player's new seat (M1)");
    M9f1Listed(listed, 0, 3, -1, 0);   /* its next absence (a notebook blip) CARRIES its bits; pre-patch the arm dropped them */
    M9f1Zero(nw, 16); coopdrop::AreaMaskSet(nw, 0);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 103.0, cells, nw, 2, fl, &t);
    if (!coopdrop::AreaMaskTest(cells + 8, 1) || t.droppedArmed != 0 || (fl[1] & coopdrop::kAreaCellOther) == 0) Fail("M9f1: the rejoined player's bits were dropped at an absence (M1)");
    coopdrop::AreaBookForget(&bk, 9, -1, none, 104.0);   /* what the pending list is for: named gone, no column, LISTED by the next map */
    M9f1Listed(listed, 0, 3, 2, 9);
    M9f1Zero(nw, 16); coopdrop::AreaMaskSet(nw, 0); coopdrop::AreaMaskSet(nw + 8, 2);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 105.0, cells, nw, 2, fl, &t);
    if (t.pendingArmed != 1 || !coopdrop::AreaMaskTest(bk.dropOnAbsence, 2)) Fail("M9f1: a pending slot the map lists was not armed");
    M9f1Listed(listed, 0, 3, -1, 0);
    M9f1Zero(nw, 16); coopdrop::AreaMaskSet(nw, 0);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 106.0, cells, nw, 2, fl, &t);
    if (coopdrop::AreaMaskTest(cells + 8, 2) || t.droppedArmed != 1) Fail("M9f1: an armed seat's absence did not drop its bits");
    if (bk.slotOfSeat[2] != -1 || t.freed != 1 || bk.slotOfSeat[1] != 7 || !coopdrop::AreaMaskTest(cells + 8, 1)) Fail("M9f1: the columns after the drop are wrong");
}
void t_m9f1_pending_expires()
{
    coopdrop::AreaBook bk; coopdrop::AreaBookInit(&bk);
    unsigned none[8], cells[8], nw[8]; int listed[256]; unsigned char fl[1]; coopdrop::AreaApplyTally t;
    M9f1Zero(none, 8); M9f1Zero(cells, 8);
    coopdrop::AreaBookForget(&bk, 7, -1, none, 100.0);
    if (coopdrop::AreaBookExpirePending(&bk, 114.9) != 0 || bk.pendingCount != 1) Fail("M9f1: a pending slot expired before 15 s");
    if (coopdrop::AreaBookExpirePending(&bk, 115.1) != 1 || bk.pendingCount != 0) Fail("M9f1: a pending slot did not expire after 15 s");
    coopdrop::AreaBookForget(&bk, 7, -1, none, 100.0);   /* named gone at 100; only empty maps until the rejoin map at 116 */
    M9f1Listed(listed, 0, 7, -1, 0); M9f1Zero(nw, 8); coopdrop::AreaMaskSet(nw, 0);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 116.0, cells, nw, 1, fl, &t);
    if (t.pendingExpired != 1 || t.pendingArmed != 0 || coopdrop::AreaMaskAny(bk.dropOnAbsence)) Fail("M9f1: a 16 s old pending slot armed the rejoined seat");
    coopdrop::AreaBookForget(&bk, 8, -1, none, 120.0);
    M9f1Listed(listed, 0, 7, 1, 8);
    coopdrop::AreaApplyMapCore(&bk, listed, 3, 130.0, cells, nw, 1, fl, &t);
    if (t.pendingArmed != 1 || !coopdrop::AreaMaskTest(bk.dropOnAbsence, 1)) Fail("M9f1: a pending slot inside 15 s was not armed");
    coopdrop::AreaBookInit(&bk);
    for (int k = 0; k < 257; ++k) coopdrop::AreaBookForget(&bk, 1000 + k, -1, none, 200.0);
    if (bk.pendingCount != 256 || bk.pendingSlot[0] != 1001 || bk.pendingSlot[255] != 1256) Fail("M9f1: the pending list did not stop at 256, oldest out");
    if (coopdrop::AreaBookForget(&bk, 5, 4, none, 200.0) != 0 || coopdrop::AreaBookForget(&bk, -1, -1, none, 200.0) != 0) Fail("M9f1: a slot with a column (or an unknown slot) went pending");
}
void t_m9f1_restart_sequence()
{
    coopdrop::AreaBook bk; coopdrop::AreaBookInit(&bk);
    const int me = 40000, other = 40005;
    unsigned cells[24], nw[24], bits[8]; int listed[256]; unsigned char fl[3]; coopdrop::AreaApplyTally t;
    M9f1Zero(cells, 24);
    /* map 1: me seat 0, other seat 1; cell 0 both, cell 1 other, cell 2 me - plus a row bit for seat 5, which the table does not list */
    M9f1Listed(listed, 0, me, 1, other);
    M9f1Zero(nw, 24); coopdrop::AreaMaskSet(nw, 0); coopdrop::AreaMaskSet(nw, 1); coopdrop::AreaMaskSet(nw + 8, 1); coopdrop::AreaMaskSet(nw + 16, 0); coopdrop::AreaMaskSet(nw + 16, 5);
    coopdrop::AreaApplyMapCore(&bk, listed, me, 10.0, cells, nw, 3, fl, &t);
    if (coopdrop::AreaMaskTest(cells + 16, 5)) Fail("M9f1: a row bit for a seat the table does not list was taken");
    if ((fl[1] & coopdrop::kAreaCellOther) == 0 || (fl[1] & coopdrop::kAreaCellMineNow) != 0 || (fl[2] & coopdrop::kAreaCellMineNow) == 0 || (fl[2] & coopdrop::kAreaCellOther) != 0) Fail("M9f1: map 1's cells are wrong");
    cooppg::PeerGoneMaskBits(other, 1, bits);   /* other is named gone while its seat is known: seat 1 armed */
    coopdrop::AreaBookForget(&bk, other, 1, bits, 11.0);
    /* the world server restarts; its first map knows only OTHER (inside its grace), seated at 0 */
    M9f1Listed(listed, 0, other, -1, 0);
    M9f1Zero(nw, 24); coopdrop::AreaMaskSet(nw, 0); coopdrop::AreaMaskSet(nw + 8, 0);
    coopdrop::AreaApplyMapCore(&bk, listed, me, 20.0, cells, nw, 3, fl, &t);
    if (t.moved != 2 || t.lost != 0 || bk.slotOfSeat[0] != other || bk.slotOfSeat[1] != me) Fail("M9f1: the restart's rekey did not move other 1->0 and me 0->1");
    if (!coopdrop::AreaMaskTest(bk.dropOnAbsence, 0) || coopdrop::AreaMaskTest(bk.dropOnAbsence, 1)) Fail("M9f1: the arm did not move with its column");
    if ((fl[0] & coopdrop::kAreaCellMineNow) == 0 || (fl[2] & coopdrop::kAreaCellMineNow) == 0 || (fl[1] & coopdrop::kAreaCellOther) == 0 || t.absentOther != 0) Fail("M9f1: the restart's partial first map withdrew something (T240)");
    /* map 3: other's grace is over; me re-reports and is seated at 1 */
    M9f1Listed(listed, 1, me, -1, 0);
    M9f1Zero(nw, 24); coopdrop::AreaMaskSet(nw, 1); coopdrop::AreaMaskSet(nw + 16, 1);
    coopdrop::AreaApplyMapCore(&bk, listed, me, 30.0, cells, nw, 3, fl, &t);
    if (t.moved != 0 || t.droppedArmed != 1 || coopdrop::AreaMaskTest(cells, 0) || coopdrop::AreaMaskTest(cells + 8, 0)) Fail("M9f1: the departed player's bits were not dropped at its armed absence");
    if (bk.slotOfSeat[0] != -1 || t.freed != 1 || (fl[1] & coopdrop::kAreaCellOther) != 0) Fail("M9f1: the departed player's empty column was not freed");
    if ((fl[0] & coopdrop::kAreaCellMineNow) == 0 || (fl[2] & coopdrop::kAreaCellMineNow) == 0 || t.carried != 0) Fail("M9f1: my cells after the restart are wrong");
}
void t_m8f1_skip_session_peer()
{
    if (!cooppg::PlayerGoneSkipsSessionPeer(4, 4, true, -1)) Fail("PLAYER_GONE naming the session peer while the session link is up was not skipped (T240)");
    if (cooppg::PlayerGoneSkipsSessionPeer(4, 4, false, 4)) Fail("PLAYER_GONE naming the session peer was skipped with the session link down");
    if (cooppg::PlayerGoneSkipsSessionPeer(5, 4, true, 5)) Fail("PLAYER_GONE naming a third player was skipped");
    if (cooppg::PlayerGoneSkipsSessionPeer(0, -1, true, -1)) Fail("an unannounced session peer (-1) matched a slot");
    if (!cooppg::PlayerGoneSkipsSessionPeer(4, -1, true, 4)) Fail("M8 re-check 2a: the re-linked session peer (slot not yet re-announced, captured 4) was not skipped");
    if (cooppg::PlayerGoneSkipsSessionPeer(5, -1, true, 4)) Fail("M8 re-check 2a: a third player was skipped as the re-linked session peer");
}
/* ---- M11a S3: HOST-LEFT AND KEEPALIVE ON THE WORLD-SERVER LINK ---------------------------------------------------------------- */
void t_m11s3_closing_live_origin()
{
    std::vector<coopjoin::RosterRow> rows;
    coopjoin::RosterRow a; a.slot = 4; a.op = 1; a.stage = (unsigned)coopjoin::kStageInWorld; rows.push_back(a);
    coopjoin::RosterRow c; c.slot = 7; c.op = 0; c.stage = (unsigned)coopjoin::kStageInWorld; rows.push_back(c);
    const int op = coopjoin::RosterOperatorSlot(rows);
    if (op != 4) Fail("the roster's operator slot is not the row flagged operator");
    if (!coopjoin::ClosingLiveAccepted(4, op)) Fail("the operator's own SESSION_CLOSING through the world server was refused");
    if (coopjoin::ClosingLiveAccepted(7, op)) Fail("another player's SESSION_CLOSING through the world server was accepted");
    if (coopjoin::ClosingLiveAccepted(0, -1) || coopjoin::ClosingLiveAccepted(4, -1)) Fail("a SESSION_CLOSING was accepted with no operator known");
    std::vector<coopjoin::RosterRow> none; none.push_back(c);
    if (coopjoin::RosterOperatorSlot(none) != -1 || coopjoin::RosterOperatorSlot(std::vector<coopjoin::RosterRow>()) != -1) Fail("a roster with no operator row named one");
    if (!coopjoin::RosterListsSlot(rows, 7) || coopjoin::RosterListsSlot(rows, 5) || coopjoin::RosterListsSlot(rows, -1)) Fail("RosterListsSlot is not exactly the listed slots");
}
void t_m11s3_operator_gone()
{
    if (!coopjoin::OperatorGoneDecide(4, 4, false)) Fail("PLAYER_GONE for the operator's slot, not on the roster, was not operator absent");
    if (coopjoin::OperatorGoneDecide(7, 4, false)) Fail("PLAYER_GONE for another player read as the operator absent");
    if (coopjoin::OperatorGoneDecide(4, 4, true)) Fail("PLAYER_GONE for an operator the roster lists again read as absent");
    if (coopjoin::OperatorGoneDecide(4, -1, false)) Fail("PLAYER_GONE with no operator known read as the operator absent");
}
int M11s3Kind(const coopown::HostLeftFeed& f, bool seen)
{
    return coopown::HostLeftDecide(f.joiner != 0, true, seen, false, f.closingSeen != 0, f.linkUp != 0, f.dialing != 0, f.redials);
}
void t_m11s3_source_priority()
{
    using namespace coopown;
    /* ROAD SESSION - args: sJoiner, sUp, sDialing, sRedials, sClosing, worldRoad, wUp, wOpAbsent, wClosingLive */
    HostLeftFeed f = HostLeftFeedPick(true, true, false, 0, false, false, true, false, true);
    if (f.road != kHlRoadSession || f.linkUp != 1 || f.closingSeen != 0 || M11s3Kind(f, true) != kHlNone)
        Fail("double trigger: a two-road game took the close through the world server while its session link is up");
    f = HostLeftFeedPick(true, true, false, 0, true, false, true, false, true);
    if (M11s3Kind(f, true) != kHlAnnounced || HostLeftSourceOf(f, kHlAnnounced, true) != kHlSrcClosingSession) Fail("both closes seen on a two-road game: the session's is not the source");
    f = HostLeftFeedPick(true, false, true, 3, false, false, true, false, false);
    if (M11s3Kind(f, true) != kHlLost || HostLeftSourceOf(f, kHlLost, true) != kHlSrcSessionLinkLost || f.redials != 3 || f.dialing != 1)
        Fail("a session joiner's link down is not today's lost branch");
    f = HostLeftFeedPick(true, false, false, 0, false, false, true, false, true);
    if (M11s3Kind(f, true) != kHlAnnounced || HostLeftSourceOf(f, kHlAnnounced, true) != kHlSrcClosingLive)
        Fail("a session joiner whose link is down did not take the operator's close through the world server");
    f = HostLeftFeedPick(true, true, false, 0, false, true, false, true, false);
    if (f.road != kHlRoadSession || M11s3Kind(f, true) != kHlNone) Fail("a session joiner with its link up left on the world road's inputs");
    /* ROAD WORLD */
    f = HostLeftFeedPick(false, false, false, 0, false, true, true, false, false);
    if (f.road != kHlRoadWorld || f.joiner != 1 || f.linkUp != 1 || M11s3Kind(f, true) != kHlNone) Fail("a world-road game with its link up and the operator present left");
    f = HostLeftFeedPick(false, false, false, 0, false, true, true, false, true);
    if (M11s3Kind(f, true) != kHlAnnounced || HostLeftSourceOf(f, kHlAnnounced, true) != kHlSrcClosingLive) Fail("the operator's close on the world road is not announced / closingLive");
    f = HostLeftFeedPick(false, false, false, 0, false, true, true, true, false);
    if (M11s3Kind(f, true) != kHlLost || HostLeftSourceOf(f, kHlLost, true) != kHlSrcOperatorAbsent) Fail("the operator absent on the world road is not lost / operatorAbsent");
    f = HostLeftFeedPick(false, false, false, 0, false, true, false, false, false);
    if (M11s3Kind(f, true) != kHlLost || HostLeftSourceOf(f, kHlLost, false) != kHlSrcWorldLinkLost) Fail("the world-server link down is not lost / worldLinkLost");
    f = HostLeftFeedPick(false, false, false, 0, false, true, false, true, true);
    if (M11s3Kind(f, true) != kHlAnnounced || HostLeftSourceOf(f, kHlAnnounced, false) != kHlSrcClosingLive) Fail("an announced close lost to a link down that followed it");
    if (M11s3Kind(f, false) != kHlNone) Fail("a world-road edge without the host seen in this world");
    /* NEITHER ROAD (the host, the operator, a game with no world server) */
    f = HostLeftFeedPick(false, false, false, 0, true, false, false, true, true);
    if (f.road != kHlRoadNone || f.joiner != 0 || M11s3Kind(f, true) != kHlNone || HostLeftSourceOf(f, kHlLost, false) != kHlSrcNone) Fail("a game on neither road took a host-left edge");
    /* the same outcomes, so the same sentences, on either road */
    if (std::string(HostLeftSentence(kHlAnnounced)) != "The host has ended the game." || std::string(HostLeftSentence(kHlLost)) != "Reconnecting to host...") Fail("the host-left sentences moved");
    if (std::string(HostLeftSourceName(kHlSrcClosingSession)) != "closingSession" || std::string(HostLeftSourceName(kHlSrcClosingLive)) != "closingLive"
        || std::string(HostLeftSourceName(kHlSrcOperatorAbsent)) != "operatorAbsent" || std::string(HostLeftSourceName(kHlSrcWorldLinkLost)) != "worldLinkLost"
        || std::string(HostLeftSourceName(kHlSrcSessionLinkLost)) != "sessionLinkLost" || std::string(HostLeftSourceName(kHlSrcNone)) != "none") Fail("the source names on the REPORT and the edge line");
}
void t_m11s3_road_local_never_dispatched()
{
    const unsigned int ts[3] = { cooplive::kInnerPing, cooplive::kInnerPong, cooplive::kInnerHostClosing };
    for (int i = 0; i < 3; ++i)
        if (!cooplive::LiveInnerRoadLocal(ts[i]) || cooplive::LiveInnerAccepted(ts[i])) Fail("PING / PONG / SESSION_CLOSING through the world server would reach the session layer's queue");
    if (cooplive::kInnerPing != 3 || cooplive::kInnerPong != 4 || cooplive::kInnerHostClosing != 56) Fail("the S3 inner types are not the session numbers");
    if (coopgl::kGameLinkTimeoutLimit != 32 || coopgl::kGameLinkTimeoutMinimumMs != 30000 || coopgl::kGameLinkTimeoutMaximumMs != 60000) Fail("link1's terms (now also the world-server link's) moved");
}
/* ---- M11a S3 review fold (F1, F2, F8) ---- */
void t_m11s3f1_world_road_joiner()
{
    using namespace coopjoin;
    int jr = JoinRoadStep(0, kJrInWorldSent);
    if (jr != 1 || !WorldRoadJoinerDecide(JoinedByWorldRoad(jr, 2, 2), false, false, false, false)) Fail("F1: a game whose JOIN completed through the world road is not a world-road joiner");
    /* a two-player joiner right after its own leave: it never joined through the world road (its world-server link kept) */
    if (WorldRoadJoinerDecide(JoinedByWorldRoad(0, 0, 5), false, false, false, false)) Fail("F1: a former session joiner after its leave read as a world-road joiner");
    /* a single-player game */
    if (WorldRoadJoinerDecide(JoinedByWorldRoad(0, 0, 0), false, false, false, false)) Fail("F1: a single-player game read as a world-road joiner");
    /* a world-road joiner's own leave / a new host or join (the session generation moved), the next load, the title */
    if (JoinedByWorldRoad(1, 2, 3)) Fail("F1: the join through the world road held past this game's leave or a new host / join");
    if (JoinRoadStep(1, kJrWorldLoad) != 0 || JoinRoadStep(1, kJrTitle) != 0 || JoinRoadStep(1, 0) != 1 || JoinRoadStep(0, 0) != 0) Fail("F1: the join-road flag's steps");
    if (WorldRoadJoinerDecide(true, true, false, false, false) || WorldRoadJoinerDecide(true, false, true, false, false)
        || WorldRoadJoinerDecide(true, false, false, true, false) || WorldRoadJoinerDecide(true, false, false, false, true))
        Fail("F1: a session joiner, the session host or the world's operator read as a world-road joiner");
}
void t_m11s3f1_host_seen_road_switch()
{
    using namespace coopown;
    if (HostSeenAcrossRoad(1, kHlRoadWorld, kHlRoadWorld) != 1 || HostSeenAcrossRoad(1, kHlRoadSession, kHlRoadSession) != 1) Fail("F8: host seen dropped with the road unchanged");
    if (HostSeenAcrossRoad(1, kHlRoadSession, kHlRoadWorld) != 0 || HostSeenAcrossRoad(1, kHlRoadWorld, kHlRoadSession) != 0
        || HostSeenAcrossRoad(1, kHlRoadWorld, kHlRoadNone) != 0 || HostSeenAcrossRoad(1, kHlRoadSession, kHlRoadNone) != 0) Fail("F8: host seen survived a road switch");
    /* the reviewed case: a two-player joiner leaves - no longer on any road, so nothing fires */
    const HostLeftFeed f = HostLeftFeedPick(false, false, false, 0, false, false, true, false, false);
    if (f.road != kHlRoadNone || M11s3Kind(f, HostSeenAcrossRoad(1, kHlRoadSession, f.road) != 0) != kHlNone) Fail("F1: a joiner that left took a host-left edge");
    /* a host seen on the session road never makes a LOST on the world road with its link down */
    const HostLeftFeed w = HostLeftFeedPick(false, false, false, 0, false, true, false, false, false);
    if (M11s3Kind(w, HostSeenAcrossRoad(1, kHlRoadSession, w.road) != 0) != kHlNone) Fail("F8: a host seen on the session road made a LOST on the world road");
}
void t_m11s3f2_closing_live_episode()
{
    using namespace coopown;
    if (!ClosingLiveHolds(1, 4, 4)) Fail("F2: the relayed close latch did not hold in the session episode it arrived in");
    if (ClosingLiveHolds(1, 4, 5)) Fail("F2: the relayed close latch held after the session link came up or the session was left");
    if (ClosingLiveHolds(0, 4, 4)) Fail("F2: a latch that is down held");
    /* the reviewed case: a session joiner, its link down, the close arrives through the world server - announced */
    HostLeftFeed f = HostLeftFeedPick(true, false, false, 0, false, false, true, false, ClosingLiveHolds(1, 4, 4));
    if (M11s3Kind(f, true) != kHlAnnounced) Fail("F2: the relayed close in its own episode is not announced");
    /* then the session link came back UP (a new episode) and later drops: reconnecting, not the host ending the game */
    f = HostLeftFeedPick(true, false, true, 1, false, false, true, false, ClosingLiveHolds(1, 4, 5));
    if (M11s3Kind(f, true) != kHlLost) Fail("F2: a later ordinary session drop read an old relayed close as the host ending the game");
    if (coopgl::HostClosingFlagStep(1, coopgl::kHcLinkUp) != 0 || coopgl::HostClosingFlagStep(1, coopgl::kHcSessionLeft) != 0) Fail("F2: the session close flag's clearing events moved");
}
void t_m8f1_hold_rule()
{
    if (cooppg::PlayerGoneHoldDecide(100.0, 105.0, 10.0, false) != cooppg::kPgHoldWait) Fail("a hold inside its grace was decided");
    if (cooppg::PlayerGoneHoldDecide(100.0, 110.0, 10.0, false) != cooppg::kPgHoldSend) Fail("a hold at its grace with the player away was not sent");
    if (cooppg::PlayerGoneHoldDecide(100.0, 105.0, 10.0, true) != cooppg::kPgHoldCancel) Fail("a player admitted again inside the grace did not cancel the hold");
    if (cooppg::PlayerGoneHoldDecide(100.0, 130.0, 10.0, true) != cooppg::kPgHoldCancel) Fail("a player connected again when the hold is decided was still announced gone");
}
void t_m6_road_local_inner_types()
{
    for (unsigned int t = 0; t < 256; ++t)
    {
        const bool local = cooplive::LiveInnerRoadLocal(t);
        if (local != (t == 200 || t == 201 || t == 202 || t == 3 || t == 4 || t == 56 || t == 203 || t == 204 || t == 205)) { Fail("the notebook road's own inner types are not exactly 200, 201, OWNER_MOVED 202, PING 3, PONG 4, SESSION_CLOSING 56, LOG_ASK 203, LOG_PART 204 and CHAT 205"); break; }
        if (local && cooplive::LiveInnerAccepted(t)) { Fail("a road-local inner type would also be dispatched to the session layer"); break; }
    }
}

/* ---- M6 FOLD 1: THE CATCH-UP'S REJOIN WINDOW ------------------------------------------------------------------------------- */
int M6f1Report(std::map<int, double>* st, std::set<int>* del, const std::set<int>& loaded, double now, std::vector<int>* fresh)
{
    cooplive::AreaReportNewlyIn(st, loaded, 0, now, cooplive::kCatchupRejoinSec, del, fresh);
    return (int)fresh->size();
}
void t_m6f1_catchup_rejoin_window()
{
    if (cooplive::kCatchupRejoinSec != 5.0) Fail("the rejoin window is not 5 s");
    const int A = cooplive::AreaKey(10, 10), B = cooplive::AreaKey(11, 10);
    std::set<int> a; a.insert(A); std::set<int> ab = a; ab.insert(B); std::set<int> none;
    std::map<int, double> st; std::set<int> del; std::vector<int> f;
    if (M6f1Report(&st, &del, a, 0.0, &f) != 1 || f[0] != A) { Fail("the first report did not make its sector new"); return; }
    if (M6f1Report(&st, &del, a, 1.0, &f) != 0) Fail("an unchanged report asked for a catch-up");
    /* FAILS BEFORE: comparing with the previous report asked for B on every even second of this flicker (5 rounds). */
    int rounds = 0;
    for (int s = 2; s <= 11; ++s) if (M6f1Report(&st, &del, (s % 2 == 0) ? ab : a, (double)s, &f) != 0) ++rounds;
    if (rounds != 1) Fail("an edge flickering once a second asked for more than one catch-up round");
    for (int s = 12; s <= 17; ++s) M6f1Report(&st, &del, a, (double)s, &f);
    if (M6f1Report(&st, &del, ab, 18.0, &f) != 1 || f[0] != B) Fail("a sector out of the delivery set for 8 s was not caught up");
    if (M6f1Report(&st, &del, a, 19.0, &f) != 0 || M6f1Report(&st, &del, ab, 23.0, &f) != 0) Fail("a sector out for 4 s counted as new");
    if (M6f1Report(&st, &del, a, 24.0, &f) != 0 || M6f1Report(&st, &del, ab, 28.0, &f) != 0) Fail("a sector out for exactly 5 s since its last report counted as new (the window is 'more than')");
    if (M6f1Report(&st, &del, none, 29.0, &f) != 0 || !del.empty() || !st.empty()) Fail("an empty report (the game left its world) did not empty the delivery set and the stamps");
    if (M6f1Report(&st, &del, ab, 30.0, &f) != 2) Fail("after an empty report the next sectors were not all new (a reloaded game lost its copies)");
    if (M6f1Report(&st, &del, a, 60.0, &f) != 1 || st.size() != 1) Fail("a game silent past the window was not caught up in full, or old stamps were kept");
    std::set<int> one; one.insert(cooplive::AreaKey(20, 20)); std::set<int> step; step.insert(cooplive::AreaKey(21, 20));
    std::map<int, double> s2; std::set<int> d2;
    cooplive::AreaReportNewlyIn(&s2, one, cooplive::kAreaMargin, 0.0, cooplive::kCatchupRejoinSec, &d2, &f);
    if (f.size() != 9) Fail("a first report with the margin did not make its 9 sectors new");
    cooplive::AreaReportNewlyIn(&s2, step, cooplive::kAreaMargin, 1.0, cooplive::kCatchupRejoinSec, &d2, &f);
    if (f.size() != 3 || f[0] != cooplive::AreaKey(22, 19) || f[1] != cooplive::AreaKey(22, 20) || f[2] != cooplive::AreaKey(22, 21)) Fail("a one-square walk did not make exactly the new column new");
    /* the empty (leave) report's delivery set is a destination for nothing */
    cooplive::AreaReportNewlyIn(&st, none, cooplive::kAreaMargin, 70.0, cooplive::kCatchupRejoinSec, &del, &f);
    std::vector<int> c; c.push_back(0); c.push_back(1); std::vector<const std::set<int>*> ds; ds.push_back(&d2); ds.push_back(&del);
    std::vector<size_t> d;
    cooplive::LiveAreaRouteDecide(cooplive::AreaTarget(cooplive::AreaKey(21, 20), -1), 0, c, ds, &d);
    if (!d.empty()) Fail("a game that sent the empty leave report was still a destination");
}

/* ---- M5a FOLD 1: ONE DELIVERY PER DESTINATION, ANSWERS ON THE ROAD THEY CAME BY, WORLD_EXCEPT ------------------------- */
/* deliveries of one standing, given the plan: the session peer holds slot 3 (admitted by the notebook or not), the third
   game slot 6 (always admitted), this game slot 0 */
void M5aDeliver(const cooplive::RoadPlan& p, bool peerAdmitted, int* toPeer, int* toThird)
{
    *toPeer = 0; *toThird = 0;
    if (p.session) ++*toPeer;
    if (!p.live) return;
    std::vector<int> c; c.push_back(0); if (peerAdmitted) c.push_back(3); c.push_back(6);
    std::vector<size_t> d;
    if (cooplive::LiveRouteDecide(p.route, p.target, 0, c, &d) != cooplive::kLiveFwd) return;
    for (size_t i = 0; i < d.size(); ++i) { if (c[d[i]] == 6) ++*toThird; else if (c[d[i]] == 3) ++*toPeer; }
}
void t_m5af1_one_delivery_per_destination()
{
    /* FAILS BEFORE: M5a sent WORLD alone whenever the notebook road was up, so a session peer the notebook had not admitted
       got nothing (live=1, sess=1, adm=0). */
    for (int live = 0; live < 2; ++live) for (int sess = 0; sess < 2; ++sess) for (int adm = 0; adm < 2; ++adm)
    for (int known = 0; known < 2; ++known) for (int learnt = 0; learnt < 2; ++learnt)
    {
        const bool peerOk = learnt && adm && known && sess;   /* "known reachable" is only ever learnt from its relayed traffic */
        const cooplive::RoadPlan p = cooplive::LiveRoadDecide(cooplive::kAnswerNone, 0, live != 0, sess != 0, false, peerOk, known ? 3 : -1);
        int tp = 0, tt = 0; M5aDeliver(p, adm != 0, &tp, &tt);
        const bool peerReachable = sess != 0 || (live != 0 && adm != 0);
        if (tt != (live ? 1 : 0)) Fail("the third game did not get exactly one copy");
        if (p.peerSlotUnknown) { if (tp < 1) Fail("with its slot unknown the session peer got nothing"); continue; }
        if (tp != (peerReachable ? 1 : 0)) Fail("the session peer did not get exactly one copy");
    }
    /* fold 2: an ASK - every reachable peer gets exactly one copy it ANSWERS (the session peer never answers a PROOF copy).
       FAILS BEFORE: fold 1 sent an ask WORLD alone, so a session peer the notebook had not admitted got none (live=1,
       sess=1, adm=0) and never answered. */
    for (int live = 0; live < 2; ++live) for (int sess = 0; sess < 2; ++sess) for (int adm = 0; adm < 2; ++adm)
    for (int known = 0; known < 2; ++known) for (int learnt = 0; learnt < 2; ++learnt)
    {
        const bool peerOk = learnt && adm && known && sess;
        const cooplive::RoadPlan a = cooplive::LiveRoadDecide(cooplive::kAnswerNone, 0, live != 0, sess != 0, true, peerOk, known ? 3 : -1);
        int tp = 0, tt = 0; M5aDeliver(a, adm != 0, &tp, &tt);
        const int peerAnswers = (a.session ? 1 : 0) + ((tp - (a.session ? 1 : 0)) > 0 && !a.relayProof ? 1 : 0);
        const bool peerReachable = sess != 0 || (live != 0 && adm != 0);
        if (tt != (live ? 1 : 0)) Fail("an ask did not reach the third game exactly once");
        if (peerAnswers != (peerReachable ? 1 : 0)) Fail("an ask did not give the session peer exactly one copy to answer");
        if (a.relayProof && (!a.session || !a.live || a.route != (unsigned)cooplive::kRouteWorld)) Fail("a PROOF copy without its session copy, or not WORLD");
    }
}
void t_m5af1_answer_goes_back_the_road_it_came()
{
    cooplive::RoadPlan p = cooplive::LiveRoadDecide(cooplive::kAnswerSession, 0, true, true, false, true, 3);
    if (!p.session || p.live) Fail("an answer to a session ask left the session road");
    p = cooplive::LiveRoadDecide(cooplive::kAnswerSlot, 7, true, true, false, false, 3);
    if (p.session || !p.live || p.route != (unsigned)cooplive::kRouteSlot || p.target != 7) Fail("an answer to a relayed ask was not SLOT to the asker alone");
    p = cooplive::LiveRoadDecide(cooplive::kAnswerSlot, 7, false, true, false, false, 3);
    if (p.session || p.live) Fail("an answer to a relayed ask went out with the notebook road down");
    p = cooplive::LiveRoadDecide(cooplive::kAnswerSession, 0, true, false, false, false, 3);
    if (p.session || p.live) Fail("a session answer with no session link went somewhere");
    /* N games: an answer is one send, never N */
    std::vector<int> many; for (int sl = 0; sl < 64; ++sl) many.push_back(sl);
    std::vector<size_t> d;
    p = cooplive::LiveRoadDecide(cooplive::kAnswerSlot, 9, true, false, false, false, -1);
    if (cooplive::LiveRouteDecide(p.route, p.target, 0, many, &d) != cooplive::kLiveFwd || d.size() != 1 || many[d[0]] != 9) Fail("an answer among 64 games did not reach the asker alone");
}
void t_m5af1_world_except_and_world_target()
{
    std::vector<int> c; c.push_back(0); c.push_back(1); c.push_back(2); c.push_back(-1); c.push_back(7);
    std::vector<size_t> d;
    if (cooplive::LiveRouteDecide(cooplive::kRouteWorldExcept, 2, 1, c, &d) != cooplive::kLiveFwd || d.size() != 2 || d[0] != 0 || d[1] != 4) Fail("WORLD_EXCEPT did not skip the named slot and the origin");
    if (cooplive::LiveRouteDecide(cooplive::kRouteWorldExcept, 9, 1, c, &d) != cooplive::kLiveFwd || d.size() != 3) Fail("WORLD_EXCEPT an absent slot is not WORLD");
    std::vector<char> b; const char z = 0; cooplive::LiveUp u;
    if (cooplive::LiveUpEncode(&b, cooplive::kRouteWorld, 5, 32, &z, 1)) Fail("WORLD with a target was encoded");
    if (!b.empty()) { Fail("a refused encode appended bytes"); return; }
    if (!cooplive::LiveUpEncode(&b, cooplive::kRouteWorldExcept, 5, 32, &z, 1) || cooplive::LiveUpDecode(&b[0], b.size(), &u) != cooplive::kLiveFwd
        || u.route != (unsigned)cooplive::kRouteWorldExcept || u.target != 5) { Fail("WORLD_EXCEPT did not round-trip"); return; }
    std::vector<char> w(b); w[0] = (char)cooplive::kRouteWorld;   /* the same frame re-labelled WORLD, target 5 */
    if (cooplive::LiveUpDecode(&w[0], w.size(), &u) != cooplive::kLiveBadLen) Fail("WORLD with a non-zero target was not badLen");
    std::vector<char> e(b); const unsigned int big = 70000; std::memcpy(&e[4], &big, 4);
    if (cooplive::LiveUpDecode(&e[0], e.size(), &u) != cooplive::kLiveBadLen) Fail("WORLD_EXCEPT past a u16 was not badLen");
    std::vector<char> r;
    if (cooplive::LiveUpEncode(&r, cooplive::kRouteWorldExcept, 70000, 32, &z, 1)) Fail("WORLD_EXCEPT past a u16 was encoded");
}

/* ---- M5a FOLD 2: THE SESSION PEER'S "REACHABLE THROUGH THE NOTEBOOK" BOOK, AND RELSYNC'S PAYLOAD -------------------------- */
void t_m5af2_peer_relay_book()
{
    cooplive::PeerRelayBook b;
    if (cooplive::PeerBookRelayed(&b, false, 0) != 1 || !b.ok) Fail("a relayed standing from the peer did not set reachable");
    if (cooplive::PeerBookRelayed(&b, false, 0) != 0) Fail("a second relayed standing reported a change");
    if (cooplive::PeerBookSessionAskRelayDown(&b, 5) != 1 || b.ok || !b.barred || b.barSeq != 5) Fail("a session ask with the notebook road down did not clear and bar");
    /* FAILS BEFORE (fold 1): a late relayed standing re-set the flag after the clear */
    if (cooplive::PeerBookRelayed(&b, false, 0) != 0 || b.ok) Fail("a late relayed standing re-set a barred flag");
    if (cooplive::PeerBookRelayed(&b, true, 5) != 0 || b.ok || !b.barred) Fail("a relayed ask no newer than the bar re-set it");
    if (cooplive::PeerBookRelayed(&b, true, 4) != 0 || b.ok) Fail("a stale relayed ask re-set a barred flag");
    if (cooplive::PeerBookRelayed(&b, true, 6) != 1 || !b.ok || b.barred) Fail("a newer relayed ask did not re-set and unbar");
    if (cooplive::PeerBookSessionAskRelayDown(&b, 3) != 1 || b.barSeq != 5) Fail("an older barring ask lowered the bar");
    if (cooplive::PeerBookRelayed(&b, true, 5) != 0 || cooplive::PeerBookRelayed(&b, true, 6) != 1) Fail("after a second bar only an ask newer than the highest bar re-sets");
    cooplive::PeerRelayBook f;
    if (f.ok || f.barred || f.barSeq != 0) Fail("a fresh book is not empty");
}
void t_m5af2_relsync_payload()
{
    std::vector<char> b;
    cooplive::RelSyncEncode(&b, cooplive::kAskRelayUp | cooplive::kAskProofOnly, 0xABCDEF01u);
    unsigned int fl = 9, sq = 9;
    if (b.size() != 5) { Fail("the RELSYNC payload is not 5 bytes"); return; }
    cooplive::RelSyncDecode(&b[0], b.size(), &fl, &sq);
    if (fl != (unsigned)(cooplive::kAskRelayUp | cooplive::kAskProofOnly) || sq != 0xABCDEF01u) Fail("the RELSYNC payload did not round-trip");
    const char legacy = 0;
    cooplive::RelSyncDecode(&legacy, 1, &fl, &sq);
    if (fl != 0 || sq != 0) Fail("a 1-byte RELSYNC did not read as flags 0, number 0");
    cooplive::RelSyncDecode(0, 0, &fl, &sq);
    if (fl != 0 || sq != 0) Fail("an empty RELSYNC did not read as flags 0, number 0");
}

/* ---- M11 C1 (T-197, to-do M11): "IS ANOTHER PLAYER IN THIS WORLD?" AND THE AREA TIE-BREAK WITH NO HOST (src/common/presence.h) ---- */
static coopjoin::RosterRow PresRow(unsigned slot, unsigned stage) { coopjoin::RosterRow r; r.slot = slot; r.stage = stage; return r; }
void t_m11c1_presence_decide()
{
    using namespace mppresence;
    for (int o = -1; o <= 1; ++o) if (PresenceDecide(1, o) != 1) Fail("the old link up is no longer another player (two-game session runs would change)");
    if (PresenceDecide(0, 1) != 1) Fail("the old link down with another slot IN_WORLD on the roster answered 'alone' (the pre-M11 rule: every world-road game writes everything)");
    if (PresenceDecide(0, 0) != 0) Fail("no old link and nobody else IN_WORLD on the roster is not 'alone'");
    if (PresenceDecide(0, -1) != 0) Fail("no world server and no old link is not 'alone' (the no-world fail-open)");
    std::vector<coopjoin::RosterRow> rows;
    int other = 9, lowest = 9;
    RosterPresence(rows, 1, &other, &lowest); if (other != 0 || lowest != -1) Fail("an empty roster");
    rows.push_back(PresRow(1, coopjoin::kStageInWorld));
    RosterPresence(rows, 1, &other, &lowest); if (other != 0 || lowest != 1) Fail("my own row counted as another player");
    rows.push_back(PresRow(0, coopjoin::kStageLoading));
    RosterPresence(rows, 1, &other, &lowest); if (other != 0 || lowest != 1) Fail("a LOADING row counted as a player in the world");
    rows.push_back(PresRow(3, coopjoin::kStageInWorld));
    RosterPresence(rows, 1, &other, &lowest); if (other != 1 || lowest != 1) Fail("another slot IN_WORLD not seen, or the lowest is wrong");
    if (PresenceDecide(0, other) != 1) Fail("the old link down + a roster peer IN_WORLD answered 'alone' (the pre-M11 rule)");
    RosterPresence(rows, 3, &other, &lowest); if (other != 1 || lowest != 1) Fail("the other game's view of the same roster differs");
    RosterPresence(rows, -1, &other, &lowest); if (other != -1 || lowest != -1) Fail("a game with no slot read the roster (it cannot tell its own row)");
    if (PresenceDecide(0, other) != 0) Fail("no slot, no old link: not 'alone'");
}
/* src/common/presence.h OwnerInWorld / EatAnswer*: the engine's 'eaten' removal of a copy waits for that copy's OWNER alone - the
   same answer whatever else is in the world. */
void t_owner_in_world_eaten_copy()
{
    using namespace mppresence;
    if (OwnerInWorld(0, 1, 1, 1) != 0) Fail("a copy with no owner record waited for an owner");
    if (OwnerInWorld(1, 1, 0, 0) != 1) Fail("the owner's slot IN_WORLD on the roster did not count as the owner in the world");
    if (OwnerInWorld(1, 0, 0, 1) != 0) Fail("the old link up to ANOTHER player counted as this copy's owner in the world (a player-count road)");
    if (OwnerInWorld(1, 1, 0, 1) != 1) Fail("the owner IN_WORLD with another player on the old link: not in the world");
    if (OwnerInWorld(1, -1, 1, 1) != 1) Fail("the owner on the old link, the link up, no roster: not in the world");
    if (OwnerInWorld(1, 0, 1, 1) != 1) Fail("the owner on the old link, the link up, read as away because the roster lacks it");
    if (OwnerInWorld(1, -1, 1, 0) != 1) Fail("the owner's old link down and no roster: read as away though nothing shows it gone");
    if (OwnerInWorld(1, -1, 0, 0) != 1) Fail("no roster and the owner not the session peer: read as away (a game with no world server must hold back as with company)");
    if (OwnerInWorld(1, 0, 1, 0) != 0) Fail("the owner's old link down and its slot not IN_WORLD: not away");
    if (OwnerInWorld(1, 0, 0, 0) != 0) Fail("the owner's slot not IN_WORLD, no link: not away");
    if (EatAnswerOf(1) != kEatOwnerHere || EatAnswerOf(0) != kEatOwnerAway) Fail("the answer word");
    if (!EatAnswerRefuses(kEatAsked)) Fail("an unanswered ask let the engine remove a copy (only the owner removes)");
    if (!EatAnswerRefuses(kEatOwnerHere)) Fail("the owner in the world: the 'eaten' removal went through");
    if (EatAnswerRefuses(kEatOwnerAway)) Fail("the owner not in the world: the 'eaten' removal still refused");
}
void t_m11c1_tiebreak_three_games()
{
    using namespace mppresence;
    const int sets[3][3] = { {0, 1, 2}, {5, 3, 7}, {2, 0, 1} };
    for (int k = 0; k < 3; ++k)
    {
        int low = sets[k][0];
        for (int g = 1; g < 3; ++g) if (sets[k][g] < low) low = sets[k][g];
        for (int host = -1; host < 3; ++host)   /* -1: no host (a headless server, or every game on the world road); else that game is a host */
        {
            int writers = 0, who = -1;
            for (int g = 0; g < 3; ++g) if (TieBreakNoTable(0, g == host ? 1 : 0, sets[k][g], low) == 1) { ++writers; who = sets[k][g]; }
            if (writers != 1) Fail("three games, no old link: not exactly one writer for a contested area (the pre-M11 rule: the host only, nobody without one)");
            else if (who != low) Fail("three games, no old link: the writer is not the lowest IN_WORLD slot");
        }
    }
    for (int h = 0; h < 2; ++h) for (int me = -1; me < 3; ++me) for (int lo = -1; lo < 3; ++lo)
        if (TieBreakNoTable(1, h, me, lo) != h) Fail("with the old link up the tie-break is not the session role (two-game session runs would change)");
    if (TieBreakNoTable(0, 1, 2, -1) != 1 || TieBreakNoTable(0, 0, 2, -1) != 0) Fail("no IN_WORLD row on the roster: not the session role");
    if (TieBreakNoTable(0, 1, -1, 0) != 1 || TieBreakNoTable(0, 0, -1, 0) != 0) Fail("no slot of my own: not the session role");
    if (TieBreakByRoster(0, 4, 4) != 1 || TieBreakByRoster(0, 4, 2) != 0 || TieBreakByRoster(1, 2, 2) != -1 || TieBreakByRoster(0, -1, 2) != -1) Fail("TieBreakByRoster's table");
}
/* ---- M11 C2 (T-197, row M11): WHICH OTHER PLAYER HAS JUST ENTERED THE WORLD (src/common/arrivals.h) ---- */
static void ArrRoster(std::vector<coopjoin::RosterRow>* rows, unsigned slot, unsigned stage)
{
    for (size_t i = 0; i < rows->size(); ++i) if ((*rows)[i].slot == slot) { (*rows)[i].stage = stage; return; }
    coopjoin::RosterRow r; r.slot = slot; r.stage = stage; rows->push_back(r);
}
void t_m11c2_arrival_diff()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld, T = (unsigned)coopjoin::kStageTitle;
    std::vector<coopjoin::RosterRow> a, b;
    std::vector<int> arr, gone;
    ArrRoster(&a, 0, W); ArrRoster(&b, 0, W); ArrRoster(&b, 2, W); ArrRoster(&b, 3, T);
    RosterArrivals(a, b, 0, &arr, &gone);
    if (arr.size() != 1 || arr[0] != 2 || !gone.empty()) Fail("a slot newly IN_WORLD is not the one arrival (or a TITLE row counted)");
    RosterArrivals(b, b, 0, &arr, &gone);
    if (!arr.empty() || !gone.empty()) Fail("an unchanged roster produced an arrival or a leave");
    RosterArrivals(b, a, 0, &arr, &gone);
    if (!arr.empty() || gone.size() != 1 || gone[0] != 2) Fail("a slot leaving IN_WORLD is not the one leave");
    std::vector<coopjoin::RosterRow> c = b; ArrRoster(&c, 0, T);
    RosterArrivals(a, c, 0, &arr, &gone);
    if (!arr.empty()) Fail("a game not IN_WORLD itself saw an arrival (it sees the others when it enters)");
    RosterArrivals(a, b, -1, &arr, &gone);
    if (!arr.empty() || !gone.empty()) Fail("a game with no slot read arrivals off the roster");
    /* title -> in-world -> title -> in-world counts twice; the leave in between once */
    Book bk;
    std::vector<coopjoin::RosterRow> r; ArrRoster(&r, 0, W); ArrRoster(&r, 5, T);
    BookRoster(&bk, r, 0, 100);
    ArrRoster(&r, 5, W); BookRoster(&bk, r, 0, 200);
    ArrRoster(&r, 5, T); BookRoster(&bk, r, 0, 300);
    ArrRoster(&r, 5, W); BookRoster(&bk, r, 0, 400);
    if (bk.seen != 2 || bk.left != 1 || BookSlotArrivals(bk, 5) != 2) Fail("title->in-world->title->in-world did not count two arrivals and one leave");
    long cur = 0; std::vector<int> sl;
    if (BookSince(bk, &cur, &sl) != 2 || sl.size() != 2 || sl[0] != 5 || sl[1] != 5) Fail("Since did not list both arrivals of slot 5");
    if (BookSince(bk, &cur, &sl) != 0) Fail("a cursor served twice");
    if (ServeTarget(bk, 1) != 5 || ServeTarget(bk, 0) != -1) Fail("ServeTarget: one roster arrival is addressed, two go to everyone");
    /* my own leaving the world is no leave of the others */
    ArrRoster(&r, 0, T); BookRoster(&bk, r, 0, 500);
    if (bk.left != 1) Fail("this game leaving the world counted the others as leaving");
}
void t_m11c2_link_merge()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld;
    std::vector<coopjoin::RosterRow> r; ArrRoster(&r, 0, W); ArrRoster(&r, 1, W);
    {   /* link up (slot known) then the roster: one arrival */
        Book b; BookLink(&b, 1, 1, 1, 0); BookRoster(&b, r, 0, 10);
        if (b.seen != 1 || b.linkMerged != 1 || b.viaLink != 1) Fail("link up then roster IN_WORLD for the same slot is not ONE arrival");
    }
    {   /* roster first, then the link: one arrival */
        Book b; BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, 1, 10);
        if (b.seen != 1 || b.linkMerged != 1 || b.viaRoster != 1) Fail("roster IN_WORLD then link up for the same slot is not ONE arrival");
    }
    {   /* link up with its slot not known yet: counted at once (today's timing); the roster's IN_WORLD is held until the slot is known */
        Book b; BookLink(&b, 1, 1, -1, 0);
        if (b.seen != 1) Fail("a link up with no roster arrival waiting was not counted at once");
        BookRoster(&b, r, 0, 10); BookLink(&b, 1, 1, -1, 20);
        if (b.seen != 1 || b.held.size() != 1) Fail("a roster arrival during an unbound link was not held");
        BookLink(&b, 1, 1, 1, 30);
        if (b.seen != 1 || b.linkMerged != 1 || !b.held.empty()) Fail("the held roster arrival of the link's own slot was not merged");
    }
    {   /* roster first, then a link whose slot is not known: held, merged when it is */
        Book b; BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, -1, 10);
        if (b.seen != 1 || b.linkState != 2) Fail("a link up while a roster arrival waits was not held for its slot");
        BookLink(&b, 1, 1, 1, 20);
        if (b.seen != 1 || b.linkMerged != 1) Fail("roster then an unbound link: not one arrival once the slot is known");
    }
    {   /* the slot never comes: counted after kBindWaitMs */
        Book b; BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, -1, 10); BookLink(&b, 1, 1, -1, 10 + kBindWaitMs);
        if (b.seen != 2 || b.bindTimeouts != 1) Fail("a link held for its slot was not counted after kBindWaitMs");
    }
    {   /* a different slot binds: the held roster arrival is another player */
        std::vector<coopjoin::RosterRow> r3 = r; ArrRoster(&r3, 2, W);
        Book b; BookLink(&b, 1, 1, -1, 0); BookRoster(&b, r3, 0, 10); BookLink(&b, 1, 1, 2, 20);
        if (b.seen != 2 || b.linkMerged != 1 || BookSlotArrivals(b, 1) != 1) Fail("a held arrival of another slot was not counted when the link's slot became known");
    }
    {   /* no world server (no slot): link-only, every link-up counts */
        Book b; BookRoster(&b, r, -1, 0); BookLink(&b, 1, 1, -1, 0); BookLink(&b, 0, 2, -1, 5); BookLink(&b, 1, 3, -1, 9);
        if (b.seen != 2 || BookEpoch(b) != 3) Fail("a game with no slot is not link-only (two ups = two arrivals, epoch = ups + downs)");
    }
    /* one particular player */
    if (PeerByLink(0, -1, 1, 1, 1, 1) != 1 || PeerByLink(1, 1, 1, 1, 1, 1) != 1 || PeerByLink(1, 2, 1, 1, 1, 1) != 0) Fail("PeerByLink with the link up");
    if (PeerByLink(1, 2, 1, -1, -1, 1) != 1) Fail("PeerByLink: link up, slot unknown - not the session peer as before");
    if (PeerByLink(1, 1, 0, -1, 1, 1) != 1 || PeerByLink(1, 2, 0, -1, 1, 1) != 0) Fail("PeerByLink with the link down");
    if (PeerByLink(1, 2, 0, -1, -1, 0) != 0 || PeerByLink(1, 2, 0, -1, -1, 1) != 1) Fail("PeerByLink: never a link = the roster; down before its slot = as before");
    /* "the old link was ever up" is an up edge the record saw - not a session generation that moved */
    {   /* HOST or JOIN pressed twice, the old link never up: the epoch moves per leave, every other player is judged by the roster */
        std::vector<int> one; one.push_back(2);
        Book b; BookLink(&b, 0, 0, -1, 0); BookLink(&b, 0, 1, -1, 10); BookLink(&b, 0, 2, -1, 20);
        if (b.downs != 2 || BookEpoch(b) != 2) Fail("two leaves with the old link down did not each move the epoch");
        if (b.linkEverUp != 0) Fail("two leaves with the old link never up read as a link that was up");
        if (PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 0) Fail("two presses, no link ever: a roster player judged by the old link");
        if (Reachable(2, 0, 1, one, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 1 || Reachable(-1, 0, 1, one, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 1)
            Fail("two presses, no link ever: the player in the world read as unreachable");
    }
    {   /* the old link up and down before its slot was known: any relayed slot is judged by the link, as before - a later leave too */
        Book b; BookLink(&b, 0, 0, -1, 0); BookLink(&b, 1, 1, -1, 10); BookLink(&b, 0, 2, -1, 20);
        if (b.linkEverUp != 1 || b.lastLinkSlot != -1) Fail("a link up and down before its slot: not read as up once");
        if (PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 1) Fail("a link down before its slot: a relayed slot not judged by the link as before");
        BookLink(&b, 0, 3, -1, 30);
        if (b.linkEverUp != 1 || PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 1) Fail("a leave after a real link changed how a relayed slot is judged");
    }
    {   /* the old link up bound to slot 1, then down: slot 1 by the link, slot 2 by the roster, as before */
        Book b; BookLink(&b, 1, 1, 1, 0); BookLink(&b, 0, 2, -1, 10); BookLink(&b, 0, 3, -1, 20);
        if (b.linkEverUp != 1 || b.lastLinkSlot != 1) Fail("a bound link up and down: not read as up once with its last slot");
        if (PeerByLink(1, 1, b.linkUp, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 1 || PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, b.linkEverUp) != 0)
            Fail("a bound link down: its slot not by the link or another slot not by the roster");
    }
}
/* MORE THAN TWO PLAYERS: an escrow row, a taker entry, a capture hold and a capture answer are each judged by THEIR counterpart */
void t_t456a_counterpart_edges()
{
    using namespace coopesc;
    {   /* two games: the per-row keys fire exactly where the one check over every row fired (that check walked alongside) */
        const long gens[] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 5 };   /* the link generation at each check (odd = up) */
        const int makeAt[] = { 0, 2, 3, 6, 9 };                    /* a row is made after the edge, just before these checks */
        const int nRows = 5;
        int used[5] = { 0, 0, 0, 0, 0 }, keyed[5] = { 0, 0, 0, 0, 0 }, relOld[5] = { 0, 0, 0, 0, 0 }, relNew[5] = { 0, 0, 0, 0, 0 };
        int qOld[5] = { 0, 0, 0, 0, 0 }, qNew[5] = { 0, 0, 0, 0, 0 };
        long rowGen[5] = { 0, 0, 0, 0, 0 };
        long sOld = -1, linkSeen = kEscGenNone;
        for (int c = 0; c < (int)(sizeof(gens) / sizeof(gens[0])); ++c)
        {
            for (int k = 0; k < nRows; ++k) if (makeAt[k] == c) used[k] = 1;
            const long g = gens[c];
            const int up = ((g % 2) != 0) ? 1 : 0;
            if (g != sOld)
            {   /* the one check: every row at a generation change; nothing at the very first (no row can be in escrow yet then) */
                const int first = (sOld == -1) ? 1 : 0;
                sOld = g;
                for (int k = 0; k < nRows; ++k)
                {
                    if (used[k] == 0 || first != 0) continue;
                    ++qOld[k];
                    if (up != 0 && RelinkOnUp(0, 1, 0) != 0) relOld[k] = 1;
                }
            }
            for (int k = 0; k < nRows; ++k)
            {
                if (used[k] == 0) continue;
                if (keyed[k] == 0) { rowGen[k] = EscKeyFirstGen(g, linkSeen); keyed[k] = 1; }
                const int f = EscRowEdge(&rowGen[k], g, up, 0, 1, 0);
                if ((f & kEscEdQuiet) != 0) ++qNew[k];
                if ((f & kEscEdRelink) != 0) relNew[k] = 1;
            }
            linkSeen = g;
        }
        for (int k = 0; k < nRows; ++k)
            if (relOld[k] != relNew[k] || qOld[k] != qNew[k]) { Fail("two games: an escrow row's edges are not where the one link check put them"); break; }
        if (relNew[0] != 1 || relNew[1] != 1 || relNew[2] != 1 || relNew[3] != 1 || relNew[4] != 1) Fail("two games: (control) every row sees a later link-up as its relink");
        if (EscRowEdge(&rowGen[0], rowGen[0], 1, 0, 1, 0) != 0) Fail("two games: no edge without a generation change");
    }
    {   /* three games: the session peer (judged by the link, bound to slot 1) and slot 2 (by the roster) - each row's edges are its own player's */
        const unsigned W = (unsigned)coopjoin::kStageInWorld;
        std::vector<coopjoin::RosterRow> all, no2;
        ArrRoster(&all, 0, W); ArrRoster(&all, 1, W); ArrRoster(&all, 2, W);
        ArrRoster(&no2, 0, W); ArrRoster(&no2, 1, W);
        mparrive::Book b;
        mparrive::BookLink(&b, 1, 1, 1, 0);
        mparrive::BookRoster(&b, all, 0, 10);
        long rowPeer = mparrive::PeerGenIn(b, 1, -1, 1), rowS2 = mparrive::PeerGenIn(b, 0, 2, 1);
        if (rowPeer != 1 || rowS2 >= 0) Fail("three games: the session peer's key is the link generation, another player's a roster epoch (setup)");
        mparrive::BookLink(&b, 0, 2, -1, 20);   /* the session link drops */
        if (EscRowEdge(&rowPeer, mparrive::PeerGenIn(b, 1, -1, 2), 0, 0, 1, 0) != kEscEdQuiet) Fail("three games: the session peer's row did not see its link drop");
        if (EscRowEdge(&rowS2, mparrive::PeerGenIn(b, 0, 2, 2), 1, 0, 1, 0) != 0) Fail("three games: slot 2's row saw the session peer's link drop");
        mparrive::BookRoster(&b, no2, 0, 30); mparrive::BookRoster(&b, all, 0, 40);   /* slot 2 leaves the world and comes back */
        if (EscRowEdge(&rowS2, mparrive::PeerGenIn(b, 0, 2, 2), 1, 0, 1, 0) != (kEscEdQuiet | kEscEdRelink)) Fail("three games: slot 2 coming back is not its row's edge (quiet clock restarted, relinked)");
        if (EscRowEdge(&rowPeer, mparrive::PeerGenIn(b, 1, -1, 2), 0, 0, 1, 0) != 0) Fail("three games: the session peer's row saw slot 2 come back");
        long fresh = 0;
        if ((fresh = EscKeyFirstGen(mparrive::PeerGenIn(b, 0, 2, 2), 2)) != mparrive::PeerGenIn(b, 0, 2, 2)) Fail("a new row judged by the roster starts from its player's epoch now");
        if (EscKeyFirstGen(5, kEscGenNone) != 5 || EscKeyFirstGen(5, 4) != 4) Fail("a new row judged by the link starts from the previous check's generation (none yet: now)");
    }
    {   /* one key per hold: slot 2's hold made while it was judged by the roster survives a link that comes up bound to slot 2 */
        const unsigned W = (unsigned)coopjoin::kStageInWorld;
        std::vector<coopjoin::RosterRow> r;
        ArrRoster(&r, 0, W); ArrRoster(&r, 2, W);
        mparrive::Book b;
        mparrive::BookRoster(&b, r, 0, 0);   /* slot 2 arrives through the world server; no link yet */
        const int famAtHold = mparrive::PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, 0);
        const long keyAtHold = mparrive::PeerGenIn(b, famAtHold, 2, 0);
        mparrive::BookLink(&b, 1, 1, 2, 10);   /* the session link comes up bound to slot 2: the same arrival, merged */
        if (famAtHold != 0 || b.linkMerged != 1) Fail("one key per hold: setup (judged by the roster, then one merged arrival)");
        if (mparrive::PeerGenIn(b, famAtHold, 2, 1) != keyAtHold) Fail("one key per hold: the key moved when only the way its player is judged changed - a put-back (a duplicate)");
        if (mparrive::PeerByLink(1, 2, b.linkUp, b.linkSlot, b.lastLinkSlot, 1) != 1 || mparrive::PeerGenIn(b, 1, 2, 1) == keyAtHold) Fail("one key per hold: (control) judged afresh, the way and its number did change");
        if (mparrive::PeerGenIn(b, 1, -1, 7) != 7) Fail("two games: a hold judged by the link compares the link generation, as before");
    }
    {   /* the taker: the re-send gate, and each entry by its own player */
        int bad = 0;
        for (long g = 0; g < 4; ++g) for (int up = 0; up < 2; ++up) for (long rg = -1; rg < 4; ++rg)
            if (TakerResendOpen(g, up, rg) != ((up != 0 && rg == g) ? 1 : 0)) bad = 1;
        if (bad != 0) Fail("two games: the session peer's re-send gate is not the session-ready link's");
        if (TakerResendOpen(-3, 1, -1) != 1 || TakerResendOpen(-3, 0, 5) != 0) Fail("another player's entry is re-sent while it is IN_WORLD, never while it is not");
        const unsigned int P2 = cooplive::RelayPeerId(2), P3 = cooplive::RelayPeerId(3);
        TakerList tl;
        tl.Note(1, 1, 1, -2, 1000, 0, P2);
        tl.Note(2, 1, 1, -2, 1000, 0, P3);
        const int a = tl.Find(1), c = tl.Find(2);
        if (a < 0 || c < 0 || tl.e[a].peer != P2 || tl.e[c].peer != P3) { Fail("a taker entry remembers the player its answer went to"); return; }
        if (tl.Due(a, -2, 1) != 0 || tl.Due(c, -3, 1) != 1) Fail("only the entry of the player that left and came back is re-sent");
        if (tl.PruneAt(a, -2, 1, 1000 + kTakerSettleMs, kTakerSettleMs) != 1 || tl.PruneAt(c, -3, 1, 1000 + kTakerSettleMs, kTakerSettleMs) != 0)
            Fail("each entry settles by its own player");
        if (tl.PruneAt(c, -2, 0, 1000 + kTakerSettleMs, kTakerSettleMs) != 0) Fail("an entry whose player is not here is never dropped");
        TakerList t3;
        long now3[kTakerCap];
        for (int k = 0; k < kTakerCap; ++k) { t3.Note(500 + (unsigned int)k, 1, 1, (k == 7) ? -2 : -5, 1000 + (unsigned int)k, 0, P2); now3[k] = -2; }
        if (t3.Note(999, 1, 0, -9, 9000, 0, P3, now3) != 1 || t3.Find(507) >= 0 || t3.Find(500) < 0) Fail("a full list evicts an entry handed to its own player's current generation first");
    }
    {   /* the taker and THIS game's own world-server link: a roster-kind entry handed to a world link that dropped and came back
           (its player's roster epoch unchanged) is re-sent, never settled as delivered; a link-kind entry ignores that link */
        const unsigned int P2 = cooplive::RelayPeerId(2);
        const long L = 4;
        TakerList tl;
        tl.Note(1, 1, 1, -2, 1000, 0, P2, 0, L);
        const int a = tl.Find(1);
        if (a < 0 || tl.e[a].ownLink != L) { Fail("own link: a roster-kind entry records the world-server link it was handed to"); return; }
        if (tl.Due(a, -2, 1, L) != 0 || tl.PruneAt(a, -2, 1, 1000 + kTakerSettleMs - 1, kTakerSettleMs, L) != 0)
            Fail("own link: (control) the same world link - not due, not yet settled");
        if (tl.Due(a, -2, 1, L + 1) != 1) Fail("own link: a roster-kind entry handed to an older world link is not re-sent - the asker's escrow puts the item back (a duplicate)");
        if (tl.PruneAt(a, -2, 1, 1000 + 10 * kTakerSettleMs, kTakerSettleMs, L + 1) != 0 || tl.Find(1) != a)
            Fail("own link: a roster-kind entry handed to an older world link was settled as delivered");
        tl.MarkSent(a, -2, 2000, L + 1);
        if (tl.Due(a, -2, 1, L + 1) != 0) Fail("own link: re-sent on the new world link - not due again");
        if (tl.PruneAt(a, -2, 1, 2000 + kTakerSettleMs - 1, kTakerSettleMs, L + 1) != 0) Fail("own link: re-sent - not settled before the settle time");
        if (tl.PruneAt(a, -2, 1, 2000 + kTakerSettleMs, kTakerSettleMs, L + 1) != 1 || tl.Find(1) >= 0) Fail("own link: re-sent on the new world link and settled - dropped");
        TakerList tk;   /* link kind (gen >= 0): every own-link value reads as before */
        tk.Note(7, 1, 1, 3, 1000, 0, 0, 0, L);
        const int k = tk.Find(7);
        int bad = 0;
        for (long own = -1; own < 9; ++own)
        {
            if (tk.Due(k, 3, 1, own) != 0 || tk.Due(k, 4, 1, own) != 1 || tk.Due(k, 3, 0, own) != 0) bad = 1;
            if (tk.PruneAt(k, 3, 1, 1000 + kTakerSettleMs - 1, kTakerSettleMs, own) != 0 || tk.PruneAt(k, 4, 1, 1000 + kTakerSettleMs, kTakerSettleMs, own) != 0
                || tk.PruneAt(k, 3, 0, 1000 + kTakerSettleMs, kTakerSettleMs, own) != 0) bad = 1;
        }
        if (bad != 0) Fail("two games: a link-kind entry's re-send or settling read this game's world-server link");
        if (tk.PruneAt(k, 3, 1, 1000 + kTakerSettleMs, kTakerSettleMs, L + 5) != 1) Fail("two games: a link-kind entry settles on its link whatever the world-server link is");
        TakerList t3;   /* a full list: a roster-kind entry handed to an older world link is not the "likeliest delivered" */
        long now3[kTakerCap];
        for (int j = 0; j < kTakerCap; ++j) { t3.Note(500 + (unsigned int)j, 1, 1, -2, 1000 + (unsigned int)j, 0, P2, 0, (j == 9) ? L + 1 : L); now3[j] = -2; }
        if (t3.Note(999, 1, 0, -9, 9000, 0, P2, now3, L + 1) != 1 || t3.Find(509) >= 0 || t3.Find(500) < 0)
            Fail("own link: a full list evicted a roster-kind entry handed to an older world link before one handed to the current one");
    }
    {   /* a capture answer is checked against the player the request went to */
        const unsigned int P2 = cooplive::RelayPeerId(2), P3 = cooplive::RelayPeerId(3);
        if (SameCounterpart(P2, P2) != 1 || SameCounterpart(P2, P3) != 0) Fail("a capture answer from another numbered player is not this capture's");
        if (SameCounterpart(5u, P3) != 1 || SameCounterpart(P2, 5u) != 1 || SameCounterpart(5u, 6u) != 1) Fail("two games: a raw key (the session peer, slot not known) still matches");
        if (SameCounterpart(cooplive::PlayerKeyOf(0u, 2), cooplive::PlayerKeyOf(77u, 2)) != 1) Fail("two games: the session peer's raw ids fold to its known slot and match");
        if (SameCounterpart(cooplive::PlayerKeyOf(0u, 2), cooplive::PlayerKeyOf(P3, 2)) != 0) Fail("the session peer's request answered by a third player is refused");
    }
}
/* TWO GAMES: every re-pointed site fires exactly as often as today's link-up, and the epoch moves exactly as the link generation */
void t_m11c2_two_player_equivalence()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld, T = (unsigned)coopjoin::kStageTitle;
    for (int side = 0; side < 2; ++side)   /* 0: the host (slot 0, in the world); 1: the joiner (slot 1, at the title at first) */
    {
        const int me = side == 0 ? 0 : 1, peer = side == 0 ? 1 : 0;
        Book b; long cursor = 0, lastEpoch = BookEpoch(b);
        int up = 0, wasUp = 0, peerSlot = -1; long gen = 0, lastGen = 0;
        int todayEdges = 0, todayGenMoves = 0, serves = 0, epochMoves = 0;
        std::vector<coopjoin::RosterRow> r;
        ArrRoster(&r, (unsigned)me, side == 0 ? W : T); ArrRoster(&r, (unsigned)peer, side == 0 ? T : W);
        for (int t = 0; t < 400; ++t)
        {
            const unsigned now = (unsigned)t * 100u;
            if (t == 5) BookRoster(&b, r, me, now);               /* the roster before the link: the joiner is at the title - it sees nobody yet */
            if (t == 10) { up = 1; ++gen; }                       /* the session link comes up (the joiner at the title) */
            if (t == 12) peerSlot = peer;                         /* PEER_SLOT */
            if (t == 30) { ArrRoster(&r, 0, W); ArrRoster(&r, 1, W); BookRoster(&b, r, me, now); }   /* the joiner's first AREAS */
            if (t == 50) { ArrRoster(&r, (unsigned)peer, T); BookRoster(&b, r, me, now); }           /* the peer's world-server link blips */
            if (t == 52) { ArrRoster(&r, (unsigned)peer, W); BookRoster(&b, r, me, now); }
            if (t == 100) { up = 0; ++gen; peerSlot = -1; }      /* the session link drops (both stay in the world) */
            if (t == 140) { up = 1; ++gen; }                     /* and comes back */
            if (t == 141) peerSlot = peer;
            if (t == 200) { up = 0; ++gen; up = 1; ++gen; }      /* down and up between two ticks */
            if (t == 300) { up = 0; ++gen; peerSlot = -1; ArrRoster(&r, (unsigned)peer, T); BookRoster(&b, r, me, now); }   /* the peer leaves */
            if (t == 310) ++gen;                                 /* a leave with the link already down (net SessionLeave) */
            if (t == 330) { up = 1; ++gen; }                     /* it joins again: link at the title ... */
            if (t == 332) peerSlot = peer;
            if (t == 360) { ArrRoster(&r, (unsigned)peer, W); BookRoster(&b, r, me, now); }   /* ... and enters the world */
            BookLink(&b, up, gen, peerSlot, now);
            if ((up != 0 && wasUp == 0) || (up != 0 && gen != lastGen && wasUp != 0)) ++todayEdges;   /* a P site's up edge (a re-link between ticks included) */
            if (gen != lastGen) ++todayGenMoves;
            wasUp = up; lastGen = gen;
            if (BookSince(b, &cursor, 0) > 0) ++serves;
            if (BookEpoch(b) != lastEpoch) { ++epochMoves; lastEpoch = BookEpoch(b); }
        }
        if (serves != todayEdges) Fail(std::string(side == 0 ? "the host" : "the joiner") + ": a re-pointed site did not serve exactly as often as today's link-up edge");
        if (epochMoves != todayGenMoves)   /* the down+up at t 200 moves the generation by 2 in one tick: one change seen by either */
            Fail(std::string(side == 0 ? "the host" : "the joiner") + ": the epoch did not move exactly as the link generation");
        if (b.viaRoster != 0) Fail(std::string(side == 0 ? "the host" : "the joiner") + ": a two-game run counted a roster arrival (every arrival must be the link's)");
    }
    /* a third player through the world server is one new arrival, addressed to it, and the session peer is not counted again */
    Book b3; std::vector<coopjoin::RosterRow> r3; ArrRoster(&r3, 0, W); ArrRoster(&r3, 1, W);
    BookLink(&b3, 1, 1, 1, 0); BookRoster(&b3, r3, 0, 0);
    long cur = b3.seq;
    ArrRoster(&r3, 2, T); BookRoster(&b3, r3, 0, 10); ArrRoster(&r3, 2, W); BookRoster(&b3, r3, 0, 20);
    const long was = cur;
    if (BookSince(b3, &cur, 0) != 1 || ServeTarget(b3, was) != 2) Fail("a third player entering through the world server was not one arrival addressed to its slot");
}
/* [m11c2f1-5] fold 1 of M11 C2: the review's cases, on the real arrivals.h */
void t_m11c2f1_relink_after_outage()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld, T = (unsigned)coopjoin::kStageTitle;
    std::vector<coopjoin::RosterRow> r; ArrRoster(&r, 0, W); ArrRoster(&r, 1, W);
    {   /* the peer stays IN_WORLD on the roster through the old link's outage: the re-link is one arrival, served once */
        Book b; long cur = 0;
        BookLink(&b, 1, 1, 1, 0); BookRoster(&b, r, 0, 0); BookSince(b, &cur, 0);
        BookLink(&b, 0, 2, -1, 100); BookRoster(&b, r, 0, 200);
        if (BookSince(b, &cur, 0) != 0) Fail("an outage with the peer still in the world counted an arrival");
        BookLink(&b, 1, 3, 1, 300); BookLink(&b, 1, 3, 1, 400);
        if (BookSince(b, &cur, 0) != 1 || BookSince(b, &cur, 0) != 0) Fail("the re-link after an outage was not one arrival served once");
    }
    {   /* the peer re-enters on the roster while the old link is down, then the link comes back: ONE arrival for both */
        Book b; long cur = 0;
        BookLink(&b, 1, 1, 1, 0); BookRoster(&b, r, 0, 0); BookSince(b, &cur, 0);
        BookLink(&b, 0, 2, -1, 100);
        std::vector<coopjoin::RosterRow> r2 = r; ArrRoster(&r2, 1, T);
        BookRoster(&b, r2, 0, 150); BookRoster(&b, r, 0, 200);
        BookLink(&b, 1, 3, -1, 300); BookLink(&b, 1, 3, 1, 350);
        if (BookSince(b, &cur, 0) != 1 || b.linkMerged != 2 || !b.waitLink.empty()) Fail("a roster re-entry during the outage and the re-link were not ONE arrival");
    }
}
void t_m11c2f1_fresh_record_beyond_link()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld;
    std::vector<coopjoin::RosterRow> r; ArrRoster(&r, 0, W); ArrRoster(&r, 1, W);
    {   /* a fresh record: the roster shows the peer before the first link-up - one arrival */
        Book b; long cur = 0;
        BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, -1, 10); BookLink(&b, 1, 1, 1, 20);
        if (BookSince(b, &cur, 0) != 1 || b.seen != 1 || b.linkMerged != 1) Fail("the peer on the roster before the first link-up was not ONE arrival");
    }
    {   /* the same with the slot known at the edge */
        Book b; BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, 1, 10);
        if (b.seen != 1 || b.linkMerged != 1) Fail("the peer on the roster before a bound first link-up was not ONE arrival");
    }
    /* [m11c2f1-2] who is surely beyond the link (StoreOtherBeyondLinkInWorld's rule) */
    std::vector<int> none, one(1, 1), two; two.push_back(1); two.push_back(2);
    if (OthersBeyondLink(one, 0, -1, -1) != 0) Fail("before the first link-up the one other in the world counted beyond the link (it may be the session peer)");
    if (OthersBeyondLink(two, 0, -1, -1) != 1) Fail("two others in the world before any link-up: one of them is surely beyond the link");
    if (OthersBeyondLink(one, 1, -1, -1) != 0 || OthersBeyondLink(one, 1, 1, 1) != 0 || OthersBeyondLink(one, 0, -1, 1) != 0) Fail("two games: the session peer counted beyond the link");
    if (OthersBeyondLink(two, 1, 1, 1) != 1 || OthersBeyondLink(one, 1, 2, 2) != 1 || OthersBeyondLink(two, 0, -1, 2) != 1) Fail("a player other than the known session peer was not beyond the link");
    if (OthersBeyondLink(none, 1, 1, 1) != 0 || OthersBeyondLink(none, 0, -1, -1) != 0) Fail("nobody in the world counted beyond the link");
}
/* A building copy's owner is the stand-in of the slot that sent its PLACE (or the REMOVE a reconcile acts on) - never "the one
   other player" (src/common/buildwire.h BuildSenderSlotNow). */
void t_build_sender_slot_now()
{
    using namespace coopbuild;
    if (kBuildSenderSessionPeer != cooplive::kAddrOwnerSessionPeer) Fail("the sender mark for the old link's peer differs from the address table's (PlayerAddrOfKey records the latter)");
    if (BuildSenderSlotNow(3, -1) != 3 || BuildSenderSlotNow(3, 5) != 3) Fail("a relayed sender's slot was not its own slot (the old link's peer slot must not replace it)");
    if (BuildSenderSlotNow(0, 2) != 0) Fail("slot 0 is a real slot");
    if (BuildSenderSlotNow(kBuildSenderSessionPeer, 4) != 4) Fail("the old link's peer recorded before its slot was known: its slot now");
    if (BuildSenderSlotNow(kBuildSenderSessionPeer, -1) != -1) Fail("the old link's peer with its slot still unknown: not known - the copy waits");
    if (BuildSenderSlotNow(-1, 4) != -1) Fail("no sender on record: never given the old link's peer's slot");
    /* three games: B (slot 1) and C (slot 2) each place a piece; A gives each copy its own sender's stand-in */
    if (BuildSenderSlotNow(1, -1) == BuildSenderSlotNow(2, -1)) Fail("two senders' pieces given one stand-in");
}
/* The orphan clean-up's "another player present": one other player in the world is enough while the old link has never been up
   (src/common/arrivals.h OthersInWorldBeyondLink). */
void t_others_in_world_beyond_link()
{
    using namespace mparrive;
    std::vector<int> none, one(1, 1), two; two.push_back(1); two.push_back(2);
    if (OthersInWorldBeyondLink(one, 0, -1, -1, 0) != 1) Fail("two games through the world server: the one other player in the world was not counted");
    if (OthersInWorldBeyondLink(two, 0, -1, -1, 0) != 1) Fail("three games through the world server: the others were not counted");
    if (OthersInWorldBeyondLink(none, 0, -1, -1, 0) != 0) Fail("nobody else in the world counted as present");
    /* once the old link has been up, its rule stands unchanged */
    if (OthersInWorldBeyondLink(one, 1, -1, -1, 1) != 0 || OthersInWorldBeyondLink(one, 1, 1, 1, 1) != 0 || OthersInWorldBeyondLink(one, 0, -1, 1, 1) != 0) Fail("the old link's own peer counted beyond it");
    if (OthersInWorldBeyondLink(one, 0, -1, -1, 1) != OthersBeyondLink(one, 0, -1, -1)) Fail("the old link went down before its slot was known: not OthersBeyondLink's answer");
    if (OthersInWorldBeyondLink(two, 1, 1, 1, 1) != 1 || OthersInWorldBeyondLink(one, 1, 2, 2, 1) != 1) Fail("a player other than the old link's known peer was not counted");
}
/* The context map's squad-id key (src/common/ctxkey.h): two factions' squads with one id are two keys, and the id reads back whole. */
void t_ctx_key_faction()
{
    using namespace coopctx;
    const std::string b = CtxKeyMake("Nameless_0", "0x1A2B0000");
    const std::string c = CtxKeyMake("Nameless_0", "0x3C4D0000");
    if (b == c) Fail("two factions' squads with one id shared a key");
    if (CtxKeyIdOf(b) != "Nameless_0" || CtxKeyIdOf(c) != "Nameless_0") Fail("the squad id did not read back without the faction part");
    if (CtxKeyIdOf(CtxKeyMake("a|b", "0x10")) != "a|b") Fail("an id holding a bar was cut short");
    if (!CtxKeyIdOf("id|Nameless_0").empty()) Fail("a key with no faction part read as an id");
    if (!CtxKeyIdOf("squad123|5:6|town9").empty() || !CtxKeyIdOf("").empty() || !CtxKeyIdOf("id|").empty()) Fail("a key that is not an id key read as one");
}
/* A player who leaves takes only the rows kept for that player (src/common/peergone.h PlayerGoneTakesRow) - names, slave states, hire
   requests and promises, context platoons, squads handed over. */
void t_player_gone_takes_row()
{
    using namespace cooppg;
    if (!PlayerGoneTakesRow(2, 2)) Fail("the leaving player's own row stayed");
    if (PlayerGoneTakesRow(1, 2) || PlayerGoneTakesRow(0, 2)) Fail("another player's row went with the one who left");
    if (PlayerGoneTakesRow(-1, 2)) Fail("a row recorded with no slot went with a known player");
    if (!PlayerGoneTakesRow(-1, -1) || !PlayerGoneTakesRow(1, -1) || !PlayerGoneTakesRow(2, -1)) Fail("the old link's peer with no slot: not every row (the whole-link rule)");
    /* three games: B (1) leaves A; C's (2) promise, request and held name stay */
    int kept = 0; const int rows[4] = { 1, 2, 2, 1 };
    for (int i = 0; i < 4; ++i) if (!PlayerGoneTakesRow(rows[i], 1)) ++kept;
    if (kept != 2) Fail("B leaving did not take exactly B's two rows");
}
/* Where a hire's DONE goes (src/common/hirewire.h HireDoneToEveryGame): a DONE that moved the person reaches every game, so a third
   game moves its copy into the hirer's faction; one that moved nothing goes back to the asker only. */
void t_hire_done_to_every_game()
{
    using namespace coophire;
    if (HireDoneToEveryGame(1, 1) != 1) Fail("taken and joined: not every game - a third game keeps its copy in the old faction");
    if (HireDoneToEveryGame(1, 0) != 1) Fail("taken, recruit refused: not every game - the owner must still release it");
    if (HireDoneToEveryGame(0, 1) != 1) Fail("this game's own hire (joined): not every game");
    if (HireDoneToEveryGame(0, 0) != 0) Fail("an abandoned take went to every game - only the owner's promise is cleared");
    if (cooplive::SideRouteOf(cooplive::kInnerHire, 1, false) != cooplive::kSideRouteWorld) Fail("a DONE with no asker does not go WORLD");
}
/* The stale-squad sweep waits for a world and then runs whoever is in the world by then (src/common/squadwriter.h): no player-count
   input, so an arrival whose player has left before the world was ready takes the same step as one still present. */
void t_stale_drop_and_sweep()
{
    using namespace coopsquad;
    if (StaleSweepStep(0, 1) != kStaleSweepIdle) Fail("no sweep armed: it ran");
    if (StaleSweepStep(0, 0) != kStaleSweepIdle) Fail("no sweep armed, no world: not idle");
    if (StaleSweepStep(1, 0) != kStaleSweepWait) Fail("armed, the world still loading: did not wait (an arrival during the load is lost)");
    if (StaleSweepStep(1, 1) != kStaleSweepRun) Fail("armed, a world: did not run");
}
void t_reach_any_and_one()
{
    using namespace mparrive;
    std::vector<int> none, one(1, 1), two; two.push_back(1); two.push_back(2);
    /* any other game, session link up: 1 whatever the world-server road says - the old "linked" gate exactly */
    for (int live = 0; live < 2; ++live)
        for (int ever = 0; ever < 2; ++ever)
            if (Reachable(-1, 1, live, none, -1, -1, ever) != 1 || Reachable(-1, 1, live, two, 1, 1, ever) != 1)
                Fail("the session link up did not read as reachable for any other game");
    /* two games, the session link down: never reachable, the session peer by the old link alone (as before) */
    if (Reachable(-1, 0, 1, one, -1, 1, 1) != 0 || Reachable(-1, 0, 1, one, -1, -1, 1) != 0) Fail("two games: the session peer in the world read as reachable with the session link down");
    if (Reachable(1, 0, 1, one, -1, 1, 1) != 0 || Reachable(1, 0, 1, one, -1, -1, 1) != 0) Fail("two games: the session peer by slot read as reachable with the session link down");
    /* a game joined only through the world server (no session link ever) */
    if (Reachable(-1, 0, 1, one, -1, -1, 0) != 1) Fail("world road only: the one other player in the world was not reachable");
    if (Reachable(1, 0, 1, one, -1, -1, 0) != 1 || Reachable(2, 0, 1, one, -1, -1, 0) != 0) Fail("world road only: a slot read wrong (in the world 1, not in it 0)");
    if (Reachable(-1, 0, 0, one, -1, -1, 0) != 0 || Reachable(1, 0, 0, one, -1, -1, 0) != 0) Fail("world road only: reachable with the world link not welcomed");
    if (Reachable(-1, 0, 1, none, -1, -1, 0) != 0) Fail("world road only: reachable with nobody else in the world");
    /* three games, the session link down: the third player through the world server, the session peer not */
    if (Reachable(-1, 0, 1, two, -1, 1, 1) != 1 || Reachable(2, 0, 1, two, -1, 1, 1) != 1) Fail("three games: the third player in the world was not reachable");
    if (Reachable(1, 0, 1, two, -1, 1, 1) != 0) Fail("three games: the session peer read as reachable with the session link down");
    /* one player, session link up: the session peer (or a slot that may be it) by the link; a known third player by the world road */
    if (Reachable(1, 1, 0, none, 1, 1, 1) != 1 || Reachable(5, 1, 0, none, -1, -1, 1) != 1) Fail("the session peer (or a slot that may be it) was not reachable on the session link");
    if (Reachable(2, 1, 1, two, 1, 1, 1) != 1) Fail("a third player in the world was not reachable through the world server");
    if (Reachable(2, 1, 0, two, 1, 1, 1) != 0 || Reachable(3, 1, 1, two, 1, 1, 1) != 0) Fail("a third player read as reachable with no world-server road to it");
}
void t_round_target_for_arrival()
{
    using namespace mparrive;
    std::vector<int> one(1, 1), two; two.push_back(1); two.push_back(2);
    if (RoundTarget(-1, 0, 1, two, 1, 1, 1) != -1) Fail("a round for every game was addressed");
    if (RoundTarget(2, 1, 1, two, 1, 1, 1) != -1) Fail("a round already owed or running was narrowed to one newcomer");
    /* two games, the other player counted from the roster before its PEER_SLOT: the session peer - every game */
    if (RoundTarget(1, 0, 1, one, -1, -1, 1) != -1) Fail("two games: a roster arrival before PEER_SLOT was addressed (owed pieces would go twice)");
    if (RoundTarget(1, 0, 1, one, 1, 1, 1) != -1) Fail("two games: the session peer's arrival was addressed");
    if (RoundTarget(1, 0, 0, two, -1, 1, 1) != -1) Fail("the session peer (link down) was addressed");
    if (RoundTarget(1, 0, 0, one, -1, -1, 0) != -1) Fail("world road only: the only other player in the world was addressed");
    /* a third player entering: addressed to it alone */
    if (RoundTarget(2, 0, 1, two, 1, 1, 1) != 2) Fail("three games: the third player's arrival was not addressed to it");
    if (RoundTarget(2, 0, 0, two, -1, -1, 0) != 2) Fail("world road only, two others: the newcomer's round was not addressed to it");
}
void t_door_unsent_reoffer()
{
    using namespace coopdoor;
    if (DoorUnsentDecide(kDoorOriginActor, 1) != kDoorUnsentRetryTimed) Fail("an unsent actor report is not kept for a while");
    if (DoorUnsentDecide(kDoorOriginHolder, 0) != kDoorUnsentRetryUntilSent) Fail("an unsent re-offer is not kept until sent");
    if (DoorUnsentDecide(kDoorOriginHolder, 1) != kDoorUnsentDrop) Fail("an unsent holder change publish is no longer dropped (as before)");
    if (DoorRetryGivesUp(kDoorUnsentRetryTimed, 30000ul, 30000ul) != 1 || DoorRetryGivesUp(kDoorUnsentRetryTimed, 29999ul, 30000ul) != 0) Fail("the timed retry's limit moved");
    if (DoorRetryGivesUp(kDoorUnsentRetryUntilSent, 0xFFFFFFFFul, 30000ul) != 0) Fail("an until-sent retry gave up");
}
void t_m11c2f1_rebind_and_holds()
{
    using namespace mparrive;
    const unsigned W = (unsigned)coopjoin::kStageInWorld;
    std::vector<coopjoin::RosterRow> r; ArrRoster(&r, 0, W); ArrRoster(&r, 1, W);
    {   /* [m11c2f1-3] PEER_SLOT rebinds the link to a slot whose roster arrival waits: merged then, so the next real link-up to it counts */
        std::vector<coopjoin::RosterRow> r3 = r; ArrRoster(&r3, 2, W);
        Book b; BookLink(&b, 1, 1, 1, 0); BookRoster(&b, r3, 0, 0);
        if (b.seen != 2 || !SlotListHas(b.waitLink, 2)) Fail("rebind setup: slot 2's roster arrival is not counted and waiting");
        BookLink(&b, 1, 1, 2, 10);
        if (SlotListHas(b.waitLink, 2) || b.seen != 2) Fail("a PEER_SLOT rebind left the new slot's roster arrival waiting (or counted an arrival)");
        BookLink(&b, 0, 2, -1, 20); BookLink(&b, 1, 3, 2, 30);
        if (b.seen != 3) Fail("a real re-link bound to the rebound slot was merged away");
    }
    {   /* [m11c2f1-3] a link-down edge with a held roster arrival counts nothing; the next link's slot decides it */
        Book b; BookLink(&b, 1, 1, -1, 0); BookRoster(&b, r, 0, 10);
        if (b.seen != 1 || b.held.size() != 1) Fail("hold setup: the roster arrival during an unbound link is not held");
        BookLink(&b, 0, 2, -1, 20);
        if (b.seen != 1 || b.held.size() != 1) Fail("the link-down edge counted a held roster arrival");
        BookLink(&b, 1, 3, 1, 30);
        if (b.seen != 2 || !b.held.empty() || !b.waitLink.empty()) Fail("the held arrival of the next link's own slot was not one arrival with that link");
    }
    {   /* held through the down edge and no link comes back: counted once, after kBindWaitMs */
        Book b; BookLink(&b, 1, 1, -1, 0); BookRoster(&b, r, 0, 10); BookLink(&b, 0, 2, -1, 20);
        BookLink(&b, 0, 2, -1, 10 + kBindWaitMs);
        if (b.seen != 2 || !b.held.empty() || b.bindTimeouts != 1) Fail("a hold kept through the down edge was not counted after kBindWaitMs");
        BookLink(&b, 0, 2, -1, 20 + 2 * kBindWaitMs);
        if (b.seen != 2) Fail("a hold was counted twice");
    }
    {   /* [m11c2f1-3] PEER_SLOT later than kBindWaitMs: two arrivals were counted for one player (declared) and nothing stays waiting */
        Book b; BookRoster(&b, r, 0, 0); BookLink(&b, 1, 1, -1, 10); BookLink(&b, 1, 1, -1, 10 + kBindWaitMs);
        BookLink(&b, 1, 1, 1, 20 + kBindWaitMs);
        if (b.seen != 2 || b.lateBindSame != 1 || !b.waitLink.empty()) Fail("a late PEER_SLOT was not declared (lateBindSame) or left the roster arrival waiting");
        BookLink(&b, 0, 2, -1, 30 + kBindWaitMs); BookLink(&b, 1, 3, 1, 40 + kBindWaitMs);
        if (b.seen != 3) Fail("the next link-up after a late PEER_SLOT was merged away");
    }
}
/* ---- M11a S1 (T-197 piece #10): THE JOIN STAGE, THE JOIN GATE AND THE ROSTER (src/common/joinstage.h) ------------------ */
void t_m11a_stage_steps()
{
    using namespace coopjoin;
    if (StageStep(kStageConnected, kEvHelloLobby, kRoadWorld) != kStageLobby) Fail("a lobby HELLO did not put the connection in the lobby");
    if (StageStep(kStageLobby, kEvWelcome, kRoadWorld) != kStageTitle || StageStep(kStageConnected, kEvWelcome, kRoadSession) != kStageTitle) Fail("a WELCOME is not TITLE");
    if (StageStep(kStageTitle, kEvLoading, kRoadWorld) != kStageLoading || StageStep(kStageLoading, kEvInWorld, kRoadWorld) != kStageInWorld) Fail("the world road's LOADING -> IN_WORLD did not step");
    if (StageStep(kStageTitle, kEvInWorld, kRoadWorld) != kStageInWorld) Fail("IN_WORLD straight from the title was refused");
    if (StageStep(kStageInWorld, kEvInWorld, kRoadWorld) != kStageInWorld) Fail("a repeated IN_WORLD moved or was refused");
    if (StageStep(kStageTitle, kEvAreas, kRoadWorld) != -1 || StageStep(kStageLoading, kEvAreas, kRoadWorld) != -1) Fail("a world-road game's AREAS before IN_WORLD were taken");
    if (StageStep(kStageInWorld, kEvAreas, kRoadWorld) != kStageInWorld) Fail("a world-road game in the world had its AREAS refused");
    if (StageStep(kStageTitle, kEvAreas, kRoadSession) != kStageInWorld) Fail("the session road does not enter the world at its first AREAS (two-game runs would change)");
    if (StageStep(kStageConnected, kEvAreas, kRoadSession) != -1 || StageStep(kStageLobby, kEvAreas, kRoadSession) != -1) Fail("AREAS from a game not admitted were taken");
    if (StageStep(kStageConnected, kEvLoading, kRoadWorld) != -1 || StageStep(kStageLobby, kEvInWorld, kRoadWorld) != -1) Fail("a stage was reached without a WELCOME");
    if (StageStep(kStageInWorld, kEvLoading, kRoadWorld) != -1) Fail("a game in the world stepped back to LOADING");
    if (StageStep(kStageTitle, kEvWelcome, kRoadWorld) != -1 || StageStep(9, kEvAreas, kRoadSession) != -1 || StageStep(kStageTitle, 99, kRoadSession) != -1) Fail("an unknown step was taken");
    if (std::string(StageName(kStageInWorld)) != "in-world" || std::string(StageName(kStageTitle)) != "title" || std::string(StageName(7)) != "?") Fail("the stage names changed");
}
void t_m11a_join_load_allowed_table()
{
    using namespace coopjoin;
    if (!kRelayCarriesLiveStream) Fail("kRelayCarriesLiveStream is false - the world server is every game's door, so a welcomed world-road game must load");
    for (int w = 0; w < 2; ++w) for (int s = 0; s < 2; ++s) for (int o = 0; o < 2; ++o)
    {
        const int v = JoinLoadAllowed(w != 0, s != 0, o != 0);
        const int want = !w ? kJoinLoadNoWelcome : !s ? kJoinLoadNoStream : !o ? kJoinLoadNoOperator : kJoinLoadAllowed;
        if (v != want) Fail("JoinLoadAllowed's table is not welcome, then stream, then operator");
    }
    if (JoinLoadAllowed(true, kRelayCarriesLiveStream, true) != kJoinLoadAllowed) Fail("this build refuses a welcomed world-road game its load with the operator in the world");
    if (std::string(JoinGateText(kJoinLoadNoStream)) != "JOIN gate: load REFUSED - the world server does not carry the live stream yet") Fail("the gate line the proof reads changed");
}
void t_m11a_live_dest_title_never()
{
    using namespace coopjoin;
    for (int s = kStageConnected; s <= kStageInWorld; ++s)
        if (LiveDestAllowed(s) != (s == kStageInWorld)) Fail("a game not in the world is a LIVE destination (or one in it is not)");
    std::vector<RosterRow> rows(2);
    rows[0].slot = 0; rows[0].stage = kStageTitle; rows[0].op = 1;
    rows[1].slot = 1; rows[1].stage = kStageInWorld; rows[1].op = 0;
    if (RosterOperatorInWorld(rows)) Fail("an operator at the title reads as in the world");
    rows[0].stage = kStageInWorld;
    if (!RosterOperatorInWorld(rows)) Fail("an operator in the world is not seen");
}
void t_m11a_roster_round_trip()
{
    using namespace coopjoin;
    std::vector<RosterRow> rows(3);
    rows[0].slot = 2; rows[0].stage = kStageTitle; rows[0].name = "PlayerC"; rows[0].viewDist = 1500.5f;
    rows[1].slot = 0; rows[1].stage = kStageInWorld; rows[1].op = 1; rows[1].name = "PlayerA";
    rows[2].slot = 1; rows[2].stage = kStageLoading; rows[2].name = std::string(kNameMax + 10, 'n');
    RosterSortBySlot(&rows);
    if (rows[0].slot != 0 || rows[1].slot != 1 || rows[2].slot != 2) Fail("the roster is not sorted by slot");
    std::vector<char> b;
    if (!RosterEncode(rows, 3, &b)) { Fail("a three-row roster was refused"); return; }
    std::vector<RosterRow> back; unsigned wp = 0;
    if (!RosterDecode(&b[0], b.size(), &back, &wp) || back.size() != 3 || wp != 3) { Fail("the roster did not come back"); return; }
    if (back[0].name != "PlayerA" || back[0].op != 1 || back[0].stage != (unsigned)kStageInWorld) Fail("row 0 changed on the way");
    if (back[1].name.size() != kNameMax || back[1].stage != (unsigned)kStageLoading) Fail("a long name was not cut to the bound");
    if (back[2].name != "PlayerC" || back[2].viewDist != 1500.5f || back[2].slot != 2) Fail("row 2 changed on the way");
    if (RosterDecode(&b[0], b.size() - 1, &back, &wp) || RosterDecode(0, 0, &back, &wp)) Fail("a short roster decoded");
    std::vector<char> longer(b); longer.push_back(0);
    if (RosterDecode(&longer[0], longer.size(), &back, &wp)) Fail("a roster with trailing bytes decoded");
    std::vector<char> bad(b); const unsigned st = 9; std::memcpy(&bad[4 + 4], &st, 4);
    if (RosterDecode(&bad[0], bad.size(), &back, &wp)) Fail("a roster row with an unknown stage decoded");
    std::vector<char> empty; std::vector<RosterRow> none;
    if (!RosterEncode(none, 0, &empty) || !RosterDecode(&empty[0], empty.size(), &back, &wp) || !back.empty() || wp != 0) Fail("an empty roster did not round-trip");
    const std::string t = RosterText(rows, 2);
    if (t.find("slot 2 'PlayerC' title (this game)") == std::string::npos || t.find("slot 0 'PlayerA' in-world operator") == std::string::npos) Fail("the roster line does not name each player's stage");
}
void t_m11a_live_proto_decide()
{
    using namespace coopjoin;
    std::vector<unsigned> none, same(2, 106u), mixed;
    mixed.push_back(106u); mixed.push_back(107u);
    unsigned theirs = 7;
    if (LiveProtoDecide(106u, none, &theirs) != kLiveProtoAdmit || theirs != 0) Fail("an empty world refused its first game");
    if (LiveProtoDecide(106u, same, &theirs) != kLiveProtoAdmit) Fail("the same game-to-game protocol was refused");
    if (LiveProtoDecide(107u, same, &theirs) != kLiveProtoRefuseDiffers || theirs != 106u) Fail("a differing game-to-game protocol was admitted (or named wrong)");
    if (LiveProtoDecide(106u, mixed, &theirs) != kLiveProtoRefuseDiffers || theirs != 107u) Fail("a world holding another number let the joiner in");
    if (LiveProtoDecide(0u, none, &theirs) != kLiveProtoRefuseUnreadable) Fail("a HELLO with no readable live protocol was admitted");
    if (LiveProtoDecide(106u, same, 0) != kLiveProtoAdmit) Fail("a null out-pointer broke the decision");
    /* S1 fold (review M2): the host's version wins */
    std::vector<unsigned> w106(1, 106u), t107(1, 107u);
    if (LiveProtoJudge(106u, true, 0u, none, t107, &theirs) != kLiveProtoAdmit) Fail("the operator was refused by an out-of-date game at the title");
    if (LiveProtoJudge(0u, true, 0u, none, none, &theirs) != kLiveProtoRefuseUnreadable) Fail("an operator with no readable live protocol was admitted");
    if (LiveProtoJudge(107u, false, 106u, none, t107, &theirs) != kLiveProtoRefuseDiffers || theirs != 106u) Fail("a joiner was not judged against the operator's number");
    if (LiveProtoJudge(106u, false, 106u, none, t107, &theirs) != kLiveProtoAdmit) Fail("a joiner matching the operator was refused over a game at the title");
    if (LiveProtoJudge(107u, false, 0u, w106, t107, &theirs) != kLiveProtoRefuseDiffers || theirs != 106u) Fail("with no operator in, a joiner was not judged against the games in the world");
    if (LiveProtoJudge(107u, false, 0u, none, t107, &theirs) != kLiveProtoAdmit) Fail("with nobody in the world, a joiner matching the title games was refused");
    if (LiveProtoJudge(106u, false, 0u, none, t107, &theirs) != kLiveProtoRefuseDiffers || theirs != 107u) Fail("with nobody in the world, a joiner differing from the title games was admitted");
    /* S1 fold (review L6): the unreadable tail has its own sentence, and the differing one is unchanged */
    if (coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseLiveProtocol, 61u, 61u, 106u, 0u).find("no readable game-to-game protocol") == std::string::npos) Fail("an unreadable live protocol was named as protocol 0");
    if (coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseLiveProtocol, 61u, 61u, 106u, 107u).find("speaks game-to-game protocol 107") == std::string::npos) Fail("the differing live protocol sentence changed");
}
void t_m11a_hello_tail_and_join_stage_wire()
{
    using namespace coopjoin;
    HelloTail t; t.liveProto = 106u; t.road = kRoadWorld; t.name = "PlayerC"; t.viewDist = 2500.0f;
    std::vector<char> b(9, 'x');   /* the tail sits after the HELLO's earlier fields */
    HelloTailEncode(t, &b);
    HelloTail r;
    if (!HelloTailDecode(&b[0], b.size(), 9, &r) || r.liveProto != 106u || r.road != (unsigned)kRoadWorld || r.name != "PlayerC" || r.viewDist != 2500.0f) Fail("the HELLO tail did not round-trip");
    if (HelloTailDecode(&b[0], b.size() - 1, 9, &r) || HelloTailDecode(&b[0], b.size(), b.size() + 1, &r)) Fail("a short HELLO tail decoded");
    HelloTail bad = t; bad.road = 2; std::vector<char> bb; HelloTailEncode(bad, &bb);
    if (HelloTailDecode(&bb[0], bb.size(), 0, &r)) Fail("a road this wire does not know decoded");
    std::vector<char> s; unsigned st = 0;
    if (!JoinStageEncode(kStageInWorld, &s) || !JoinStageDecode(&s[0], s.size(), &st) || st != (unsigned)kStageInWorld) Fail("JOIN_STAGE IN_WORLD did not round-trip");
    s.clear();
    if (!JoinStageEncode(kStageLoading, &s) || !JoinStageDecode(&s[0], s.size(), &st) || st != (unsigned)kStageLoading) Fail("JOIN_STAGE LOADING did not round-trip");
    std::vector<char> n;
    if (JoinStageEncode(kStageTitle, &n) || JoinStageEncode(kStageLobby, &n) || !n.empty()) Fail("a stage a game may not claim was encoded");
    const unsigned title = kStageTitle; char raw[4]; std::memcpy(raw, &title, 4);
    if (JoinStageDecode(raw, 4, &st) || JoinStageDecode(&s[0], 3, &st)) Fail("a JOIN_STAGE of TITLE (or a short one) decoded");
}

/* ---- M11 C5 [m11c5] (T-197, row M11; flip preparation): IN_WORLD AGAIN ON A RE-WELCOME, AND A WORLD-ROAD GAME WITH THE WORLD
   SERVER OUT OF SIGHT IS NOT ALONE (src/common/joinstage.h, presence.h) ---- */
void t_m11c5_in_world_resend()
{
    using namespace coopjoin;
    if (!InWorldOwedDecide(true, false, true) || !InWorldOwedDecide(true, false, false)) Fail("the first IN_WORLD after this link's gated load changed");
    if (!InWorldOwedDecide(false, true, true)) Fail("a world-road game re-welcomed mid-world does not send IN_WORLD again");
    if (InWorldOwedDecide(false, true, false)) Fail("IN_WORLD sent again before the new link's WELCOME");
    if (InWorldOwedDecide(false, false, true)) Fail("IN_WORLD sent for a world not entered through the world road (the old road enters at its AREAS)");
    const int jr = JoinRoadStep(0, kJrInWorldSent);
    if (!InWorldOwedDecide(false, JoinedByWorldRoad(jr, 5, 5), true)) Fail("the world-road flag does not carry a re-dial mid-world");
    if (InWorldOwedDecide(false, JoinedByWorldRoad(jr, 5, 6), true)) Fail("IN_WORLD sent again after this game's own leave (the session generation moved)");
    if (InWorldOwedDecide(false, JoinedByWorldRoad(JoinRoadStep(jr, kJrWorldLoad), 5, 5), true) || InWorldOwedDecide(false, JoinedByWorldRoad(JoinRoadStep(jr, kJrTitle), 5, 5), true)) Fail("IN_WORLD sent again for a world that is no longer the one joined");
    if (StageStep(kStageTitle, kEvInWorld, kRoadWorld) != kStageInWorld) Fail("the world server refuses IN_WORLD from the title a re-WELCOME put the game at");
}
void t_m11c5_presence_world_road_outage()
{
    using namespace mppresence;
    if (PresenceDecideRoad(0, -1, 1) != 1) Fail("a world-road game with the world server out of sight answered 'alone' (two writers after the flip)");
    if (PresenceDecideRoad(0, -1, 0) != 0) Fail("no road, no roster and no old link is not 'alone' (the no-world fail-open changed)");
    for (int r = 0; r <= 1; ++r)
    {
        for (int o = -1; o <= 1; ++o) if (PresenceDecideRoad(1, o, r) != 1) Fail("the old link up is no longer another player");
        if (PresenceDecideRoad(0, 1, r) != 1 || PresenceDecideRoad(0, 0, r) != 0) Fail("a roster on this link no longer decides");
    }
    for (int o = -1; o <= 1; ++o) if (PresenceDecideRoad(0, o, 0) != PresenceDecide(0, o)) Fail("off the world road the C1 rule changed");
}

/* ---- M11 C5 fold 1 (owner 357 a): NO LOAD FROM A RUNNING WORLD WHILE CONNECTED TO ANOTHER PLAYER - the session link (F926) or the
   world server (its link welcomed and another slot IN_WORLD on its roster); alone on the world server, or at the title, a load goes ---- */
void t_m11c5f1_load_refused_connected()
{
    using namespace coopjoin;
    for (int w = 0; w <= 1; ++w) for (int o = -1; o <= 1; ++o)
        if (LoadWhileConnectedDecide(0, true, w != 0, o) != kLoadConnRefusedSession) Fail("a load in a running world with the session link up was not refused (F926 changed)");
    if (LoadWhileConnectedDecide(0, false, true, 1) != kLoadConnRefusedWorld) Fail("a load in a running world with another player in it through the world server was not refused");
    if (LoadWhileConnectedDecide(0, false, true, 0) != kLoadConnAllowed) Fail("a game alone on the world server was refused its load");
    if (LoadWhileConnectedDecide(0, false, true, -1) != kLoadConnAllowed) Fail("a game with no roster on its welcomed link was refused its load");
    if (LoadWhileConnectedDecide(0, false, false, 1) != kLoadConnAllowed) Fail("a load was refused on a world-server link not welcomed (a stale roster)");
    if (LoadWhileConnectedDecide(0, false, false, -1) != kLoadConnAllowed) Fail("a load with no link at all was refused");
    for (int t = -1; t <= 1; t += 2) for (int sl = 0; sl <= 1; ++sl) for (int w = 0; w <= 1; ++w) for (int o = -1; o <= 1; ++o)
        if (LoadWhileConnectedDecide(t, sl != 0, w != 0, o) != kLoadConnAllowed) Fail("a load at the title (or with the title screen unreadable) was refused - the joining load must pass");
}

/* ---- THE HELD JOIN LOAD (src/common/joinstage.h JoinHoldOnRefusal / JoinHoldDecide): a load refused for the world's operator alone
   is held, and started by the roster that shows the operator in the world while the link it was held on is still welcomed; a cancel,
   a link drop, another load or any other refusal ends it without loading ---- */
void t_join_hold_operator_only()
{
    using namespace coopjoin;
    for (int k = kHoldNone; k <= kHoldLoad; ++k)
        for (int v = kJoinLoadAllowed; v <= kJoinLoadNoOperator; ++v)
        {
            const bool want = k != kHoldNone && v == kJoinLoadNoOperator;
            if (JoinHoldOnRefusal(v, k) != want) Fail(want ? "a load refused for the operator alone was not held" : "a load was held for a reason other than the operator (or one that asked no load)");
        }
}
void t_join_hold_started_by_roster()
{
    using namespace coopjoin;
    std::vector<RosterRow> rows(2);
    rows[0].slot = 0; rows[0].op = 1; rows[0].stage = kStageLoading;
    rows[1].slot = 1; rows[1].op = 0; rows[1].stage = kStageTitle;
    const int vLoading = JoinLoadAllowed(true, kRelayCarriesLiveStream, RosterOperatorInWorld(rows));
    if (vLoading != kJoinLoadNoOperator) Fail("the T837 roster (operator still loading) was not refused for the operator alone");
    if (JoinHoldDecide(false, false, true, vLoading) != kHoldKeep) Fail("a roster with the operator still loading did not keep the hold");
    rows[0].stage = kStageInWorld;
    const int vIn = JoinLoadAllowed(true, kRelayCarriesLiveStream, RosterOperatorInWorld(rows));
    if (vIn != kJoinLoadAllowed) Fail("the roster showing the operator in the world did not open the gate");
    if (JoinHoldDecide(false, false, true, vIn) != kHoldStart) Fail("the roster showing the operator in the world did not start the held load");
    rows[0].op = 0; rows[1].op = 1; rows[1].stage = kStageTitle;   /* someone in the world, but not the operator */
    if (JoinHoldDecide(false, false, true, JoinLoadAllowed(true, kRelayCarriesLiveStream, RosterOperatorInWorld(rows))) != kHoldKeep) Fail("a non-operator in the world started the held load");
}
void t_join_hold_dropped()
{
    using namespace coopjoin;
    for (int v = kJoinLoadAllowed; v <= kJoinLoadNoOperator; ++v)
    {
        if (JoinHoldDecide(true, false, true, v) != kHoldDrop) Fail("a cancelled hold was not dropped");
        if (JoinHoldDecide(false, true, true, v) != kHoldDrop) Fail("a hold was not dropped when another load started");
        if (JoinHoldDecide(false, false, false, v) != kHoldDrop) Fail("a hold was not dropped when its world-server link went down or was re-dialled");
    }
    if (JoinHoldDecide(false, false, true, kJoinLoadNoWelcome) != kHoldDrop) Fail("a hold refused for no WELCOME was kept");
    if (JoinHoldDecide(false, false, true, kJoinLoadNoStream) != kHoldDrop) Fail("a hold refused for the live stream was kept");
}

/* ---- T-313: THE RECORD FEED (src/common/recordfeed.h; world-server protocol 63, RECORD_FEED 58) ------------------------------ */
void t_t313_feed_wire_roundtrip()
{
    coopfeed::FeedMsg m; std::vector<char> b;
    if (!coopfeed::FeedEncodeAsk(&b, "") || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.kind != (unsigned)coopfeed::kFeedAsk || !m.key.empty() || b.size() != 8) Fail("ASK(\"\") did not round-trip");
    b.clear();
    if (!coopfeed::FeedEncodeAsk(&b, "Holy Nation|42") || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.kind != (unsigned)coopfeed::kFeedAsk || m.key != "Holy Nation|42") Fail("ASK(key) did not round-trip");
    b.clear(); coopfeed::FeedEncodeOff(&b);
    if (b.size() != 4 || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.kind != (unsigned)coopfeed::kFeedOff) Fail("OFF did not round-trip");
    b.clear(); coopfeed::FeedEncodeBegin(&b, 1234, 56, 0x123456789ABCULL);
    if (b.size() != 20 || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.kind != (unsigned)coopfeed::kFeedBegin || m.records != 1234 || m.uniques != 56 || m.seqHigh != 0x123456789ABCULL) Fail("BEGIN did not round-trip");
    b.clear();
    if (!coopfeed::FeedEncodePageEnd(&b, "k9", 77, true, 99ULL) || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.kind != (unsigned)coopfeed::kFeedPageEnd || m.key != "k9" || m.sent != 77 || m.last != 1 || m.seqHigh != 99ULL) Fail("PAGE_END(last) did not round-trip");
    b.clear();
    if (!coopfeed::FeedEncodePageEnd(&b, "", 0, false, 0ULL) || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.last != 0 || m.sent != 0 || !m.key.empty()) Fail("an empty PAGE_END did not round-trip");
}
void t_t313_feed_wire_malformed()
{
    coopfeed::FeedMsg m; std::vector<char> a, o, g, e;
    coopfeed::FeedEncodeAsk(&a, "abc"); coopfeed::FeedEncodeOff(&o); coopfeed::FeedEncodeBegin(&g, 1, 2, 3ULL); coopfeed::FeedEncodePageEnd(&e, "xyz", 4, false, 5ULL);
    const std::vector<char>* all[4] = { &a, &o, &g, &e };
    for (int k = 0; k < 4; ++k)
    {
        const std::vector<char>& v = *all[k];
        for (size_t cut = 0; cut < v.size(); ++cut) if (coopfeed::FeedDecode(&v[0], cut, &m)) { Fail("a truncated RECORD_FEED decoded (kind " + I(k + 1) + ")"); return; }
        std::vector<char> t = v; t.push_back(0); if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("a RECORD_FEED with a trailing byte decoded (kind " + I(k + 1) + ")");
        t = v; t[2] = 1; if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("a RECORD_FEED with a non-zero pad decoded (kind " + I(k + 1) + ")");
    }
    std::vector<char> t = o; t[0] = 0; if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("kind 0 decoded");
    t = o; t[0] = 5; if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("kind 5 decoded");
    t = e; t[t.size() - 10] = 2; if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("a PAGE_END with last=2 decoded");   /* T-985 C2b fold 6: last sits before complete */
    t = a; t[4] = (char)0xFF; if (coopfeed::FeedDecode(&t[0], t.size(), &m)) Fail("an ASK whose key length runs past the message decoded");
    std::vector<char> big; std::string longKey(coopfeed::kFeedKeyMax + 1, 'x');
    if (coopfeed::FeedEncodeAsk(&big, longKey) || !big.empty()) Fail("an ASK over the key bound was encoded, or a refused encode appended bytes");
    if (coopfeed::FeedEncodePageEnd(&big, longKey, 1, true, 1ULL) || !big.empty()) Fail("a PAGE_END over the key bound was encoded");
    if (coopfeed::FeedDecode(0, 4, &m)) Fail("a null RECORD_FEED decoded");
    std::vector<char> mk; coopfeed::FeedMarkerEncode(&mk, 7, g); unsigned int ep = 0;
    if (coopfeed::FeedMarkerDecode(mk, &ep, &m)) Fail("a marker holding a BEGIN decoded as a PAGE_END marker");
    coopfeed::FeedMarkerEncode(&mk, 7, e);
    if (!coopfeed::FeedMarkerDecode(mk, &ep, &m) || ep != 7 || m.key != "xyz" || m.sent != 4) Fail("a PAGE_END marker did not round-trip");
}
void t_t313_feed_forward_truth_table()
{
    const unsigned int gated[2] = { 28, 36 };   /* T-313 review H1: RECORD_GONE is not gated */
    for (int t = 0; t < 2; ++t)
    {
        for (int c = 0; c <= 1; ++c) for (int snd = 0; snd <= 1; ++snd) for (int on = 0; on <= 1; ++on)
        {
            const int want = (!c || snd) ? coopfeed::kFwdSkip : (on ? coopfeed::kFwdSend : coopfeed::kFwdWithheld);
            if (coopfeed::FeedForwardDecide(gated[t], c, snd, on) != want) { Fail("the forward decision for type " + I((int)gated[t]) + " is wrong at connected=" + I(c) + " sender=" + I(snd) + " feedOn=" + I(on)); return; }
        }
    }
    const unsigned int open[6] = { 32, 33, 38, 39, 53, 56 };   /* RECORD_GONE (review H1) and DELETED_BITS above all - and the options, clock, LIVE and CATCHUP */
    for (int t = 0; t < 6; ++t)
    {
        if (coopfeed::FeedForwardDecide(open[t], 1, 0, 0) != coopfeed::kFwdSend) Fail("type " + I((int)open[t]) + " was withheld from a game that has not asked");
        if (coopfeed::FeedForwardDecide(open[t], 1, 1, 1) != coopfeed::kFwdSkip) Fail("type " + I((int)open[t]) + " went back to its sender");
    }
    if (!coopfeed::FeedGatedType(coopfeed::kMsgRecord) || coopfeed::FeedGatedType(coopfeed::kMsgRecordGone) || !coopfeed::FeedGatedType(coopfeed::kMsgUniqueState) || coopfeed::FeedGatedType(coopfeed::kMsgDeletedBits))
        Fail("the gated set is not exactly RECORD and UNIQUE_STATE");
}
void t_t313_feed_ask_decision()
{
    coopfeed::FeedAskState s;
    if (coopfeed::FeedAskDecide(s, 1, 1, 5, 3, 0) != coopfeed::kAskNone) Fail("a game with engine writes blocked (the title, a load) asked");
    if (coopfeed::FeedAskDecide(s, 0, 0, 5, 3, 0) != coopfeed::kAskNone) Fail("a game not welcomed asked");
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 3, 0) != coopfeed::kAskFirst) { Fail("a welcomed game with a world did not ask for the first page"); return; }
    coopfeed::FeedAskTaken(&s, coopfeed::kAskFirst, 5, 3);
    const unsigned int e1 = s.epoch;
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 3, 0) != coopfeed::kAskNone) Fail("the same (world, link) pair asked twice");
    if (coopfeed::FeedPageEndArrived(&s, 3, "k100", 0) != 1) { Fail("the answer to the ask was taken as stale"); return; }
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 3, coopfeed::kFeedLowWaterBytes) != coopfeed::kAskNone) Fail("the next page was asked at the low-water mark");
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 3, coopfeed::kFeedLowWaterBytes - 1) != coopfeed::kAskNext) { Fail("the next page was not asked below the low-water mark"); return; }
    if (coopfeed::FeedAskDecide(s, 1, 1, 5, 3, 0) != coopfeed::kAskNone) Fail("the next page was asked while engine writes are blocked");
    if (s.nextKey != "k100") Fail("the next page does not start after the last key");
    coopfeed::FeedAskTaken(&s, coopfeed::kAskNext, 5, 3);
    if (s.epoch != e1) Fail("a next-page ask changed the epoch");
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 3, 0) != coopfeed::kAskNone) Fail("a page was asked while one is still arriving");
    if (coopfeed::FeedPageEndArrived(&s, 3, "k200", 1) != 1 || s.wantNext) Fail("the last page left a next page wanted");
    if (coopfeed::FeedAskDecide(s, 1, 0, 6, 3, 0) != coopfeed::kAskFirst) Fail("a new world generation did not ask again");
    if (coopfeed::FeedAskDecide(s, 1, 0, 5, 4, 0) != coopfeed::kAskFirst) Fail("a new link generation did not ask again");
    /* OFF, then a new ask: the old ask's PAGE_END is stale, the new one's is not */
    coopfeed::FeedAskState t;
    coopfeed::FeedAskTaken(&t, coopfeed::kAskFirst, 1, 9);
    if (coopfeed::FeedOffTaken(&t) != 1) Fail("an OFF after an ask was not owed");
    if (coopfeed::FeedOffTaken(&t) != 0) Fail("a second OFF in one load was owed");
    if (coopfeed::FeedAskDecide(t, 1, 0, 1, 9, 0) != coopfeed::kAskFirst) Fail("after an OFF the same pair did not ask again");
    coopfeed::FeedAskTaken(&t, coopfeed::kAskFirst, 2, 9);
    if (coopfeed::FeedPageEndArrived(&t, 9, "old", 1) != 0) Fail("the PAGE_END of an ask made before the OFF was taken as current");
    if (coopfeed::FeedPageEndArrived(&t, 9, "new", 1) != 1) Fail("the PAGE_END of the latest ask was taken as stale");
    if (coopfeed::FeedPageEndArrived(&t, 10, "x", 1) != 0) Fail("a PAGE_END on another link was taken as current");
    coopfeed::FeedAskState u;
    if (coopfeed::FeedPageEndArrived(&u, -1, "x", 1) != 0) Fail("a PAGE_END with no ask out was taken as current");
}
void t_t313_feed_paging_converges()
{
    std::map<std::string, std::string> src, dst;
    char key[16];
    for (int i = 0; i < 200; ++i) { std::sprintf(key, "k%03d", i * 2); src[key] = std::string(50, (char)('a' + i % 26)); }
    /* no changes: every record exactly once, several pages, only the last says last */
    {
        std::map<std::string, std::string> got; T313Sink sink; sink.dst = &got;
        std::string after; unsigned int total = 0, sent = 0; int pages = 0; bool last = false;
        while (!last && pages < 1000) { std::string lk; last = coopfeed::FeedPageWalk(src, after, 1000, sink, &lk, &sent); total += sent; ++pages; after = lk; }
        if (got != src || total != (unsigned)src.size()) Fail("a quiet paging did not send every record exactly once");
        if (pages < 5) Fail("a 1000-byte budget did not split 200 records into pages");
    }
    /* changes between pages reach the subscribed game as live forwards: it ends equal to the source */
    {
        T313Sink sink; sink.dst = &dst;
        std::string after; unsigned int sent = 0; int pages = 0; bool last = false;
        while (!last && pages < 1000)
        {
            std::string lk; last = coopfeed::FeedPageWalk(src, after, 700, sink, &lk, &sent); ++pages; after = lk;
            /* between pages: insert before and after the cursor, delete before and after it, change one before it */
            std::sprintf(key, "k%03da", pages * 7 % 400); src[key] = "inserted"; dst[key] = "inserted";
            std::sprintf(key, "k%03d", (pages * 13 % 200) * 2);
            if (src.erase(key)) dst.erase(key);
            if (!dst.empty()) { const std::string k0 = dst.begin()->first; src[k0] = "changed"; dst[k0] = "changed"; }
        }
        if (!last) { Fail("the paging never reached the end"); return; }
        if (dst != src) Fail("after inserts, deletes and changes between pages the game does not hold exactly the source");
    }
    /* an empty world: one page, last, nothing sent, the key unchanged */
    {
        std::map<std::string, std::string> none, got; T313Sink sink; sink.dst = &got; std::string lk; unsigned int sent = 9;
        if (!coopfeed::FeedPageWalk(none, "", 1000, sink, &lk, &sent) || sent != 0 || !lk.empty()) Fail("an empty world's page was not last with nothing sent");
        if (!coopfeed::FeedPageWalk(src, src.rbegin()->first, 1000, sink, &lk, &sent) || sent != 0 || lk != src.rbegin()->first) Fail("a page after the last key was not last and empty");
        std::map<std::string, std::string> one; one["a"] = std::string(5000, 'z');
        if (!coopfeed::FeedPageWalk(one, "", 10, sink, &lk, &sent) || sent != 1) Fail("a record larger than the budget was not sent alone");
    }
}
void t_t313_feed_end_marker_arms_once()
{
    bool indexDone = false, snap = false; int repush = 0;
    if (coopfeed::FeedMarkerApply(4, 4, 0, &indexDone, &snap, &repush) != coopfeed::kMarkPage || indexDone || snap || repush) Fail("a non-last page's marker ended the snapshot");
    if (coopfeed::FeedMarkerApply(3, 4, 1, &indexDone, &snap, &repush) != coopfeed::kMarkStale || indexDone || snap || repush) Fail("a stale epoch's last marker ended the snapshot");
    if (coopfeed::FeedMarkerApply(4, 4, 1, &indexDone, &snap, &repush) != coopfeed::kMarkEndFirst || !indexDone || !snap || repush != 1) { Fail("the first last-page marker did not end the push and arm the push UP"); return; }
    repush = 0; snap = false;   /* StoreRepushUpTick consumed it; a load reset the snapshot */
    if (coopfeed::FeedMarkerApply(6, 6, 1, &indexDone, &snap, &repush) != coopfeed::kMarkEndAgain || !snap || repush != 0) Fail("a second world's snapshot re-armed the push UP (it must run once per link)");
}
std::string T313ReadFile(const char* path)
{
    std::string out; FILE* f = std::fopen(path, "rb"); if (f == 0) return out;
    char buf[65536]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}
/* The loader keeps every AddrReg binding in a fixed registry (addrtable.h kAddrMaxRegs); one binding too many disables the whole
   mod at load ("addrRegOverflow"). Counts the bindings in the plugin's sources the way they are written
   ("coop::AddrReg <name>(" or "AddrReg <name>(") and fails with room to spare. Runs from src\coop-test. */
void t_address_bindings_fit()
{
    long bindings = 0;
    const char* const pats[2] = { "../coop-plugin/*.cpp", "../coop-plugin/*.h" };
    for (int k = 0; k < 2; ++k)
    {
        struct _finddata_t fd;
        intptr_t h = _findfirst(pats[k], &fd);
        if (h == -1) { if (k == 0) Fail("no plugin source found under ../coop-plugin (the suite runs from src/coop-test)"); continue; }
        do
        {
            const std::string text = T313ReadFile((std::string("../coop-plugin/") + fd.name).c_str());
            for (size_t at = text.find("AddrReg "); at != std::string::npos; at = text.find("AddrReg ", at + 8))
            {
                size_t q = at + 8;
                while (q < text.size() && (std::isalnum((unsigned char)text[q]) || text[q] == '_')) ++q;
                if (q > at + 8 && q < text.size() && text[q] == '(') ++bindings;
            }
        } while (_findnext(h, &fd) == 0);
        _findclose(h);
    }
    if (bindings < 400) Fail("only " + D((double)bindings) + " AddrReg bindings counted - the count is not reading the plugin's sources");
    if (bindings + 64 > coopaddr::kAddrMaxRegs)
        Fail(D((double)bindings) + " AddrReg bindings against a registry of " + D((double)coopaddr::kAddrMaxRegs) + " - raise coopaddr::kAddrMaxRegs (addrtable.h) before the mod disables itself at load");
}
std::string T985Key(int i)
{
    std::string s = "rec";
    const int d[5] = { i / 10000 % 10, i / 1000 % 10, i / 100 % 10, i / 10 % 10, i % 10 };
    for (int j = 0; j < 5; ++j) s += (char)('0' + d[j]);
    return s;
}
/* the game's ApplyRemoteRecord on stamps (store.cpp): a full record is taken only with a later file time (its position time becomes
   the file's), a position only with a later position time (a key not held is made, position only) */
void T985ApplyFull(std::map<std::string, T985Row>* t, const std::string& k, long long f, unsigned int o)
{
    std::map<std::string, T985Row>::iterator it = t->find(k);
    if (it != t->end() && it->second.file >= f) return;
    (*t)[k] = T985Row(f, f, o);
}
void T985ApplyPos(std::map<std::string, T985Row>* t, const std::string& k, long long p)
{
    std::map<std::string, T985Row>::iterator it = t->find(k);
    if (it == t->end()) { (*t)[k] = T985Row(0, p, 0); return; }
    if (it->second.pos >= p) return;
    it->second.pos = p;
}
/* the game's HELD (every row of its table, FeedHeldEncodeAll) as the world server keeps it: decoded, keys it does not hold left out */
std::map<std::string, coopfeed::HeldRow> T985HeldAtServer(const std::map<std::string, T985Row>& game, const std::map<std::string, T985Srv>& srv)
{
    std::vector<coopfeed::HeldRow> rows;
    for (std::map<std::string, T985Row>::const_iterator it = game.begin(); it != game.end(); ++it) rows.push_back(coopfeed::HeldRow(it->first, it->second.file, it->second.pos, it->second.owner));
    std::vector<std::vector<char> > msgs; coopfeed::FeedHeldEncodeAll(rows, &msgs);
    std::map<std::string, coopfeed::HeldRow> out;
    for (size_t i = 0; i < msgs.size(); ++i)
    {
        unsigned int first = 0; std::vector<coopfeed::HeldRow> got;
        if (!coopfeed::FeedHeldDecode(&msgs[i][0], msgs[i].size(), &first, &got)) { Fail("a HELD message the game encodes did not decode"); return out; }
        if (first != (i == 0 ? 1u : 0u)) Fail("HELD's first flag is not on the first message alone");
        if (first) out.clear();
        for (size_t j = 0; j < got.size(); ++j) if (srv.find(got[j].key) != srv.end()) out[got[j].key] = got[j];
    }
    return out;
}
void T985Snapshot(const std::map<std::string, T985Srv>& srv, T985Wire* w)
{
    std::string after; bool last = false; int pages = 0;
    while (!last && pages < 10000) { std::string lk; unsigned int sent = 0; last = coopfeed::FeedPageWalk(srv, after, coopfeed::kFeedPageBytes, coopfeed::kFeedPageRecords, *w, &lk, &sent); after = lk; ++pages; }
    if (!last) Fail("the snapshot did not end");
}
void T985TableAsWritten(const std::map<std::string, T985Srv>& srv, std::map<std::string, T985Row>* g)
{
    for (std::map<std::string, T985Srv>::const_iterator it = srv.begin(); it != srv.end(); ++it)
    {
        if (it->second.hasFile && it->second.file > 0) { T985ApplyFull(g, it->first, it->second.file, it->second.owner); if (it->second.pos > it->second.file) T985ApplyPos(g, it->first, it->second.pos); }
        else T985ApplyPos(g, it->first, it->second.pos);
    }
}
void t_t985_feed_held_once()
{
    using namespace coopfeed;
    if (kFeedHeld <= (unsigned int)cooppre::kPreFetchEnd) Fail("HELD's kind is not after the preload's kinds");
    {   /* the wire */
        std::vector<HeldRow> rows; rows.push_back(HeldRow("a", 100, 150, 3)); rows.push_back(HeldRow("b", 0, 70, 0)); rows.push_back(HeldRow("c", -5, -1, 7));
        std::vector<std::vector<char> > m; FeedHeldEncodeAll(rows, &m);
        unsigned int first = 9; std::vector<HeldRow> got;
        if (m.size() != 1 || !FeedHeldDecode(&m[0][0], m[0].size(), &first, &got) || first != 1 || got.size() != 3) Fail("three HELD rows did not go as one first message");
        else if (got[0].key != "a" || got[0].fileStamp != 100 || got[0].posAt != 150 || got[0].owner != 3 || got[1].fileStamp != 0 || got[1].posAt != 70
                 || got[2].fileStamp != 0 || got[2].posAt != 0 || got[2].owner != 7) Fail("a HELD row did not round-trip (a time below 0 goes as 0)");
        if (m.size() == 1)
        {
            FeedMsg fm; if (FeedDecode(&m[0][0], m[0].size(), &fm)) Fail("the feed's own decode took a HELD message");
            if (cooppre::IsPreKind(&m[0][0], m[0].size())) Fail("a HELD message reads as a preload kind");
            std::vector<char> bad = m[0]; bad.push_back(0); if (FeedHeldDecode(&bad[0], bad.size(), &first, &got)) Fail("a HELD message with a byte after its last field was taken");
            bad = m[0]; bad.resize(bad.size() - 1); if (FeedHeldDecode(&bad[0], bad.size(), &first, &got)) Fail("a cut HELD message was taken");
            bad = m[0]; bad[4] = 2; if (FeedHeldDecode(&bad[0], bad.size(), &first, &got)) Fail("a HELD first flag of 2 was taken");
            bad = m[0]; bad[5] = (char)0xFF; bad[6] = (char)0xFF; if (FeedHeldDecode(&bad[0], bad.size(), &first, &got)) Fail("a HELD count over the bound was taken");
        }
    }
    {
        std::vector<HeldRow> none; std::vector<std::vector<char> > m; FeedHeldEncodeAll(none, &m);
        unsigned int first = 0; std::vector<HeldRow> got;
        if (m.size() != 1 || !FeedHeldDecode(&m[0][0], m[0].size(), &first, &got) || first != 1 || !got.empty()) Fail("an empty table did not send one first HELD message with no rows");
    }
    {   /* a long list: messages of at most kFeedHeldEntries rows, in order, the over-long key left out */
        std::vector<HeldRow> rows;
        for (int i = 0; i < 5000; ++i) rows.push_back(HeldRow(T985Key(i), i, i, 1));
        rows.push_back(HeldRow(std::string(kFeedKeyMax + 1, 'x'), 1, 1, 1));
        std::vector<std::vector<char> > m; FeedHeldEncodeAll(rows, &m);
        size_t total = 0; std::string prev;
        for (size_t i = 0; i < m.size(); ++i)
        {
            unsigned int first = 0; std::vector<HeldRow> got;
            if (!FeedHeldDecode(&m[i][0], m[i].size(), &first, &got) || first != (i == 0 ? 1u : 0u) || got.size() > kFeedHeldEntries) { Fail("a message of a long HELD list is wrong"); break; }
            for (size_t j = 0; j < got.size(); ++j) { if (!prev.empty() && got[j].key <= prev) Fail("HELD rows are not in order, or one came twice"); prev = got[j].key; ++total; }
        }
        if (m.size() != 3 || total != 5000) Fail("5000 rows were not three HELD messages of at most 2048, with the over-long key left out");
        std::vector<HeldRow> wide;
        for (int i = 0; i < 200; ++i) wide.push_back(HeldRow(T985Key(i) + std::string(3000, 'k'), 1, 1, 1));
        FeedHeldEncodeAll(wide, &m);
        if (m.size() < 2) Fail("200 rows of 3000-byte keys were not cut at the HELD byte bound");
        for (size_t i = 0; i < m.size(); ++i) if (m[i].size() > kFeedHeldBytes + 4000) Fail("a HELD message is past its byte bound by more than one row");
    }
    /* the rule */
    if (FeedHeldCovers(100, 100, 3, 1, 100, 100, 3) != 1) Fail("the same version is not held");
    if (FeedHeldCovers(99, 100, 3, 1, 100, 100, 3) != 0) Fail("an older file time was taken as held");
    if (FeedHeldCovers(120, 120, 3, 1, 100, 100, 3) != 1) Fail("a later copy by the same writer was not held");
    if (FeedHeldCovers(100, 100, 4, 1, 100, 100, 3) != 0) Fail("another writer's copy was taken as held (an area holder's copy is taken although not newer)");
    if (FeedHeldCovers(100, 100, 3, 1, 100, 150, 3) != 0 || FeedHeldCovers(100, 150, 3, 1, 100, 150, 3) != 1) Fail("a later position on the world server is not judged by the position time");
    if (FeedHeldCovers(0, 50, 9, 0, 0, 50, 3) != 1 || FeedHeldCovers(0, 40, 9, 0, 0, 50, 3) != 0) Fail("a position-only record is not judged by its position time alone");
    if (FeedHeldCovers(0, 50, 3, 1, 0, 50, 3) != 1) Fail("a file flag with no file time is not judged as a position-only record");
    if (FeedHeldCovers(100, 100, 3, 0, 0, 0, 3) != 0 || FeedHeldCovers(100, 100, 3, 1, 0, 0, 3) != 0) Fail("a record with no time was taken as held");
    if (FeedHeldHeaderStamp(1, 100, 150) != 150 || FeedHeldHeaderStamp(1, 100, 50) != 100 || FeedHeldHeaderStamp(0, 100, 50) != 50) Fail("the header's stamp is not the record's later time");
    {   /* every small combination: a record judged held leaves the game's table as the full record would have - unchanged */
        long long taken = 0, bad = 0;
        for (int hf = 0; hf < 2; ++hf) for (long long sf = 0; sf < 4; ++sf) for (long long sp = 0; sp < 5; ++sp) for (unsigned int so = 1; so < 3; ++so)
        for (long long gf = 0; gf < 4; ++gf) for (long long gp = 0; gp < 5; ++gp) for (unsigned int go = 1; go < 3; ++go)
        {
            if (FeedHeldCovers(gf, gp, go, hf, sf, sp, so) != 1) continue;
            ++taken;
            std::map<std::string, T985Row> before; before["k"] = T985Row(gf, gp, go);
            std::map<std::string, T985Row> full = before, head = before;
            if (hf && sf > 0) { T985ApplyFull(&full, "k", sf, so); if (sp > sf) T985ApplyPos(&full, "k", sp); } else T985ApplyPos(&full, "k", sp);
            T985ApplyPos(&head, "k", FeedHeldHeaderStamp(hf, sf, sp));
            if (!(full["k"] == before["k"]) || !(head["k"] == before["k"])) ++bad;
            if (hf && sf > 0 && go != so) ++bad;
        }
        if (taken == 0 || bad != 0) Fail("a record judged held would have changed the game's table in full, or its header changed it");
    }
    {   /* a join: the game fetched before its load; the world changed during the load and once more after its HELD list went */
        std::map<std::string, T985Srv> srv;
        for (int i = 0; i < 700; ++i) srv[T985Key(i)] = T985Srv(1, 1000 + i, 1000 + i + (i % 3 == 0 ? 5 : 0), (unsigned int)(i % 2 + 1), 51200);
        std::map<std::string, T985Row> b; T985TableAsWritten(srv, &b);
        b["own-unsent"] = T985Row(5000, 5000, 2);
        srv[T985Key(10)] = T985Srv(1, 9000, 9000, 1, 51200);
        srv[T985Key(20)].pos = 9100;
        srv["zz-new"] = T985Srv(1, 9200, 9200, 1, 51200);
        b[T985Key(30)] = T985Row(9300, 9300, b[T985Key(30)].owner);
        const std::map<std::string, HeldRow> held = T985HeldAtServer(b, srv);
        srv[T985Key(40)] = T985Srv(1, 9400, 9400, 2, 51200);
        T985Wire w(&b, &held); T985Snapshot(srv, &w);
        if (w.full != 4 || w.header != 697) Fail("the join did not send exactly the four records changed after the fetch in full and the rest as headers");
        for (std::map<std::string, T985Srv>::const_iterator it = srv.begin(); it != srv.end(); ++it)
        {
            const T985Row g = b[it->first];
            if (it->first == T985Key(30)) { if (g.file != 9300) Fail("the game's later copy of its own record was replaced by an older one"); continue; }
            const long long want = it->second.pos > it->second.file ? it->second.pos : it->second.file;
            if (g.file != it->second.file || g.pos != want) Fail("after the join the game's table is not the world server's: " + it->first);
        }
        if (w.headerBytes + w.fullBytes >= (size_t)700 * 51200 / 10) Fail("the join still sent most of the world's bytes");
        std::map<std::string, T985Row> a; T985TableAsWritten(srv, &a);
        const std::map<std::string, HeldRow> heldA = T985HeldAtServer(a, srv);
        T985Wire wa(&a, &heldA); T985Snapshot(srv, &wa);
        if (wa.full != 0 || wa.header != (unsigned int)srv.size()) Fail("a game holding every record as written was sent some in full");
        std::map<std::string, T985Row> c; T985Wire wc(&c, 0); T985Snapshot(srv, &wc);
        if (wc.full != (unsigned int)srv.size() || wc.header != 0) Fail("a snapshot with no HELD list did not send every record in full");
    }
    const std::string plugin = T313ReadFile("../coop-plugin/store.cpp"), server = T313ReadFile("../coop-store/store_main.cpp");
    if (plugin.find("coopfeed::FeedHeldEncodeAll(") == std::string::npos) Fail("the game does not send its HELD list");
    if (server.find("coopfeed::FeedHeldCoversKey(") == std::string::npos || server.find("coopfeed::IsFeedHeldKind(") == std::string::npos) Fail("the world server does not read HELD or judge a record held");
}

}   /* namespace suite */
