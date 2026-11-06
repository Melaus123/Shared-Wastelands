/* src/coop-test/suite_part5.cpp - THE OFFLINE SUITE, part 5 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"
#include "keeprunning.h"   /* the kept world's router lines (RouterLine) */

namespace suite {

/* T-985 fold 1: a ground or box record listed in HELD still goes in full (the game's ground and box tables need the whole contents);
   a squad record with matching stamps and writer still goes as its header */
void t_t985_held_ground_box_full()
{
    using namespace coopfeed;
    const std::string gid = coopgrec::GroundRecId(3, 4), bid = coopbrec::BoxRecIdOf("#0123abcd", 3, 4, 10, 20), sid = T985Key(1);
    if (!coopgrec::IsGroundRecId(gid) || !coopbrec::IsBoxRecId(bid) || coopgrec::IsGroundRecId(sid) || coopbrec::IsBoxRecId(sid)) Fail("the test's ids are not a ground, a box and a squad id");
    if (FeedHeldKeyListed(gid) || FeedHeldKeyListed(bid) || FeedHeldKeyListed("Ground.3.4") || FeedHeldKeyListed("BOX.3.4.#0123abcd.10.20")) Fail("a ground or box id may be listed as held");
    if (!FeedHeldKeyListed(sid)) Fail("a squad id may not be listed as held");
    if (FeedHeldCoversKey(gid, 100, 100, 3, 1, 100, 100, 3) != 0) Fail("a ground record with matching stamps and writer was judged held");
    if (FeedHeldCoversKey(bid, 100, 100, 3, 1, 100, 100, 3) != 0) Fail("a box record with matching stamps and writer was judged held");
    if (FeedHeldCoversKey(sid, 100, 100, 3, 1, 100, 100, 3) != 1) Fail("a squad record with matching stamps and writer was not judged held");
    if (FeedHeldCoversKey(sid, 99, 100, 3, 1, 100, 100, 3) != 0) Fail("a squad record with an older file time was judged held");
    {   /* a snapshot to a game that holds all three as written and lists all three in HELD (as a game on an older build would) */
        std::map<std::string, T985Srv> srv;
        srv[gid] = T985Srv(1, 100, 100, 3, 4000); srv[bid] = T985Srv(1, 100, 100, 3, 4000); srv[sid] = T985Srv(1, 100, 100, 3, 4000);
        std::map<std::string, T985Row> g; T985TableAsWritten(srv, &g);
        const std::map<std::string, HeldRow> held = T985HeldAtServer(g, srv);
        if (held.size() != 3) Fail("the HELD list did not carry the ground, box and squad ids");
        T985Wire w(&g, &held); T985Snapshot(srv, &w);
        if (w.full != 2 || w.header != 1) Fail("the ground and box records did not go in full while the squad record went as its header");
    }
    const std::string plugin = T313ReadFile("../coop-plugin/store.cpp"), server = T313ReadFile("../coop-store/store_main.cpp");
    if (plugin.find("if (coopfeed::FeedHeldKeyListed(it->first))") == std::string::npos) Fail("the game's HELD list does not leave ground and box records out");
    if (server.find("coopfeed::FeedHeldCoversKey(key,") == std::string::npos) Fail("the world server's page sender does not send ground and box records in full");
    if (server.find("if (nettally::Empty(w)) return;") == std::string::npos) Fail("the world server writes a TRAFFIC REPORT line when nothing moved");
}
/* ---- T-985 step 2A: AREA OF INTEREST, MEASURED (src/common/aoi.h; no wire change, world-server protocol unchanged) ---- */
/* the distance in area-squares, the rings at the grid's edges 0 and 63 (no wrap), ring K = cooplive::AreaDilate's set, and a
   position's area */
void t_t985s2a_ring_math()
{
    using namespace aoi;
    std::set<int> corner; corner.insert(cooplive::AreaKey(0, 0));
    if (RingDistance(cooplive::AreaKey(0, 0), corner) != 0) Fail("a loaded area is not at distance 0");
    if (RingDistance(cooplive::AreaKey(1, 1), corner) != 1 || RingDistance(cooplive::AreaKey(3, 0), corner) != 3 || RingDistance(cooplive::AreaKey(0, 4), corner) != 4) Fail("a distance from area 0,0 is wrong");
    if (RingDistance(cooplive::AreaKey(63, 0), corner) != 63 || RingDistance(cooplive::AreaKey(63, 63), corner) != 63) Fail("the ring wrapped at the map's edge");
    std::set<int> top; top.insert(cooplive::AreaKey(63, 63));
    if (RingDistance(cooplive::AreaKey(62, 60), top) != 3 || RingDistance(cooplive::AreaKey(0, 63), top) != 63 || RingDistance(cooplive::AreaKey(63, 62), top) != 1) Fail("a distance from area 63,63 is wrong");
    if (RingDistance(-1, corner) != -1 || RingDistance(cooplive::kAreaKeyCount, corner) != -1 || RingDistance(cooplive::AreaKey(5, 5), std::set<int>()) != -1) Fail("a bad area or an empty loaded set gave a distance");
    std::set<int> mix; mix.insert(cooplive::AreaKey(0, 0)); mix.insert(cooplive::AreaKey(63, 0)); mix.insert(cooplive::AreaKey(31, 40)); mix.insert(cooplive::AreaKey(0, 63));
    for (int ring = 1; ring <= kRingMax; ++ring)
    {
        std::set<int> dil; cooplive::AreaDilate(mix, ring, &dil);
        for (int k = 0; k < cooplive::kAreaKeyCount; ++k)
        {
            const int d = RingDistance(k, mix);
            if ((d >= 0 && d <= ring) != (dil.count(k) != 0)) { Fail("ring K disagrees with cooplive::AreaDilate (the LIVE delivery set's own widening)"); return; }
        }
    }
    if (AreaOfPos(-147455.0f, 10.0f, -147455.0f) != cooplive::AreaKey(0, 0)) Fail("a position in the first sector is not area 0,0");
    if (AreaOfPos(147455.0f, 10.0f, 147455.0f) != cooplive::AreaKey(63, 63)) Fail("a position in the last sector is not area 63,63");
    if (AreaOfPos(500000.0f, 0.0f, -500000.0f) != cooplive::AreaKey(63, 0)) Fail("a position past the grid was not clamped to its edge area, as the engine's formula does");
    if (AreaOfPos(4608.0f, 0.0f, 0.0f) != cooplive::AreaKey(33, 32)) Fail("x 4608, z 0 is not area 33,32");
    if (AreaOfPos(0.0f, 0.0f, 0.0f) != kNoArea || AreaOfPos(2.0e7f, 0.0f, 5.0f) != kNoArea || AreaOfPos(5.0f, -2.0e7f, 5.0f) != kNoArea) Fail("an unusable position was given an area");
    float nan = 0.0f; { const unsigned int bits = 0x7fc00000u; memcpy(&nan, &bits, 4); }
    if (AreaOfPos(nan, 1.0f, 1.0f) != kNoArea || AreaOfPos(1.0f, 1.0f, nan) != kNoArea) Fail("a NaN position was given an area");
}
/* in / out per ring, noArea before unknown, unknown for a game with no (or an empty) report, and the tally's records and bytes */
void t_t985s2a_classify()
{
    using namespace aoi;
    std::set<int> loaded; loaded.insert(cooplive::AreaKey(10, 10)); loaded.insert(cooplive::AreaKey(11, 10));
    const std::set<int> none;
    const int a0 = cooplive::AreaKey(10, 10), a1 = cooplive::AreaKey(12, 11), a2 = cooplive::AreaKey(13, 8), a3 = cooplive::AreaKey(14, 13), a9 = cooplive::AreaKey(20, 20);
    if (Classify(a0, &loaded, 1) != kIn || Classify(a1, &loaded, 1) != kIn || Classify(a2, &loaded, 1) != kOut || Classify(a2, &loaded, 2) != kIn
        || Classify(a3, &loaded, 2) != kOut || Classify(a3, &loaded, 3) != kIn || Classify(a9, &loaded, 3) != kOut) Fail("in / out by ring is wrong");
    if (Classify(kNoArea, &loaded, 1) != kNoAreaClass || Classify(kNoArea, 0, 1) != kNoAreaClass) Fail("a record with no position was not noArea");
    if (Classify(a0, 0, 1) != kUnknown || Classify(a0, &none, 3) != kUnknown) Fail("a game with no area report was not unknown");
    Tally t;
    t.Count(kRoadFeed, a0, &loaded, 100); t.Count(kRoadFeed, a2, &loaded, 20); t.Count(kRoadFeed, a9, &loaded, 7);
    t.Count(kRoadForward, kNoArea, &loaded, 5); t.Count(kRoadPreload, a0, 0, 9); t.Count(kRoadPreload, a0, &none, 1);
    t.Count(kRoadCount, a0, &loaded, 1000); t.Count(-1, a0, &loaded, 1000);
    if (t.total != 6) Fail("the tally's total is not the six records on known roads");
    if (t.in[kRoadFeed][0].recs != 1 || t.in[kRoadFeed][0].bytes != 100 || t.out[kRoadFeed][0].recs != 2 || t.out[kRoadFeed][0].bytes != 27) Fail("ring 1's feed counts are wrong");
    if (t.in[kRoadFeed][1].recs != 2 || t.in[kRoadFeed][1].bytes != 120 || t.out[kRoadFeed][1].recs != 1 || t.out[kRoadFeed][1].bytes != 7) Fail("ring 2's feed counts are wrong");
    if (t.in[kRoadFeed][2].recs != 2 || t.out[kRoadFeed][2].recs != 1 || t.out[kRoadFeed][2].bytes != 7) Fail("ring 3's feed counts are wrong");
    if (t.noArea[kRoadForward].recs != 1 || t.noArea[kRoadForward].bytes != 5 || t.in[kRoadForward][0].recs != 0 || t.out[kRoadForward][0].recs != 0) Fail("a noArea record was counted in or out");
    if (t.unknown[kRoadPreload].recs != 2 || t.unknown[kRoadPreload].bytes != 10 || t.in[kRoadPreload][0].recs != 0 || t.noArea[kRoadFeed].recs != 0) Fail("the unknown counts are wrong");
}
/* the ledger's add, update, cap and clear; the area index's put, move and erase; the world server's hooks on the three roads */
void t_t985s2a_ledger_index()
{
    using namespace aoi;
    Ledger l;
    if (l.Note("a", 5, 2, kRoadPreload, 100) != 1 || l.Note("b", 6, 2, kRoadFeed, 50) != 1) Fail("the ledger did not add two new ids");
    if (l.Note("a", 9, 2, kRoadForward, 30) != 2 || l.Find("a") == 0 || l.Find("a")->stamp != 9) Fail("a resend did not replace the stamp");
    if (l.Find("a")->road != kRoadPreload || l.Find("a")->bytes != 130) Fail("a resend on another road changed the first road, or its bytes were not added");
    if (l.Note("c", 7, 2, kRoadFeed, 10) != 0 || l.overflow != 1 || l.sent.size() != 2 || l.Find("c") != 0) Fail("a new id past the cap was kept, or not counted");
    if (l.Note("b", 8, 2, kRoadFeed, 5) != 2 || l.Find("b")->stamp != 8) Fail("an id already held was refused at the cap");
    if (l.adds != 2 || l.updates != 2) Fail("the ledger's add and update counts are wrong");
    if (l.sent.count(IdHash("a")) != 1 || IdHash("a") == IdHash("b") || IdHash("") != 14695981039346656037ULL || IdHash("a") != 0xaf63dc4c8601ec8cULL) Fail("the ledger is not keyed by the id's 64-bit FNV-1a hash");
    l.Clear();
    if (!l.sent.empty() || l.overflow != 0 || l.adds != 0 || l.updates != 0) Fail("a cleared ledger kept something");
    if (l.Note("c", 1, 2, kRoadFeed, 1) != 1) Fail("a cleared ledger refused a new id");
    AreaIndex ix;
    const int k1 = cooplive::AreaKey(0, 0), k2 = cooplive::AreaKey(63, 63);
    ix.Put("r1", k1); ix.Put("r2", k1); ix.Put("r3", kNoArea); ix.Put("r4", -7);
    if (ix.CountIn(k1) != 2 || ix.CountIn(kNoArea) != 2 || ix.Ids() != 4 || ix.Areas() != 1 || ix.AreaOf("r4") != kNoArea) Fail("the index's first puts are wrong");
    ix.Put("r1", k2);
    if (ix.CountIn(k1) != 1 || ix.CountIn(k2) != 1 || ix.AreaOf("r1") != k2 || ix.Areas() != 2 || ix.Ids() != 4) Fail("a record that moved was not moved in the index");
    ix.Put("r3", k2);
    if (ix.CountIn(kNoArea) != 1 || ix.CountIn(k2) != 2) Fail("a record that gained a position stayed in the noArea bucket");
    ix.Erase("r2"); ix.Erase("nope");
    if (ix.CountIn(k1) != 0 || ix.Areas() != 1 || ix.AreaOf("r2") != kNotIndexed || ix.Ids() != 3) Fail("an erased record stayed in the index");
    ix.Erase("r1"); ix.Erase("r3"); ix.Erase("r4");
    if (ix.Ids() != 0 || !ix.byArea.empty()) Fail("an emptied index kept a bucket");
    const std::string server = T313ReadFile("../coop-store/store_main.cpp");
    struct Want { const char* text; size_t n; };
    const Want want[] = { { "AoiSent(to, aoi::kRoadPreload, r, AoiFullStamp(r), b);", 1 }, { "if (aoiSentOk) AoiSent(to, aoi::kRoadFeed, r, coopfeed::FeedHeldHeaderStamp(", 1 },
                          { "AoiSent(to, aoi::kRoadFeed, r, AoiFullStamp(r), b);", 1 }, { "if (SbSendRel(p, buf))", 1 }, { "AoiSent(p, aoi::kRoadForward, r,", 1 }, { "AoiSent(", 5 },
                          { "AoiIndexPut(", 6 }, { "AoiIndexErase(", 3 }, { "AoiFirstAreas(", 2 }, { "AoiGameGone(", 2 }, { "if (aoiGoneSlot >= 0) AoiGameGone(aoiGoneSlot, p);", 1 },
                          { "+ AoiReportGroup());", 1 }, { "\"preload@firstAreas\"", 1 }, { "NowSec() - la->second.at <= kGraceSec) loaded = &la->second.loaded;", 1 } };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i)
    {
        size_t n = 0;
        for (size_t at = server.find(want[i].text); at != std::string::npos; at = server.find(want[i].text, at + 1)) ++n;
        if (n != want[i].n) Fail(std::string("the world server does not hold this the expected number of times: ") + want[i].text);
    }
}
/* the join download scored against the joiner's first area report: only entries first sent on the preload road, each by its
   record's CURRENT area (a deleted one is noArea), with the bytes sent on that road; once (scored entries are not counted again) */
void t_t985s2a_first_areas()
{
    using namespace aoi;
    AreaIndex ix;
    ix.Put("p_in", cooplive::AreaKey(10, 10)); ix.Put("p_r2", cooplive::AreaKey(12, 10)); ix.Put("p_moved", cooplive::AreaKey(40, 40)); ix.Put("p_nopos", kNoArea);
    std::vector<PreSent> pre;
    pre.push_back(PreSent("p_in", 10)); pre.push_back(PreSent("p_r2", 20)); pre.push_back(PreSent("p_moved", 40));
    pre.push_back(PreSent("p_nopos", 5)); pre.push_back(PreSent("p_gone", 7));
    ix.Put("p_moved", cooplive::AreaKey(11, 11));   /* it moved next to the loaded area after the download */
    std::set<int> loaded; loaded.insert(cooplive::AreaKey(10, 10));
    Tally none;
    if (ScoreDeferred(pre, ix, std::set<int>(), &none) != 0 || none.total != 0) Fail("an empty report scored something");
    Tally t;
    if (ScoreDeferred(pre, ix, loaded, &t) != 5) Fail("not exactly the five preload sends were scored");
    if (t.total != 5 || t.unknown[kRoadPreload].recs != 0) Fail("the deferred score counted the wrong records, or some as unknown");
    if (t.in[kRoadPreload][0].recs != 2 || t.in[kRoadPreload][0].bytes != 50 || t.out[kRoadPreload][0].recs != 1 || t.out[kRoadPreload][0].bytes != 20) Fail("ring 1 of the deferred score is wrong (the moved record by its current area)");
    if (t.in[kRoadPreload][1].recs != 3 || t.in[kRoadPreload][1].bytes != 70 || t.out[kRoadPreload][1].recs != 0) Fail("ring 2 of the deferred score is wrong");
    if (t.noArea[kRoadPreload].recs != 2 || t.noArea[kRoadPreload].bytes != 12) Fail("the no-position and deleted records were not noArea");
    if (t.in[kRoadFeed][0].recs != 0 || t.out[kRoadFeed][0].recs != 0) Fail("a preload send was scored on another road");
    Tally sum; sum.Merge(t); sum.Merge(t);
    if (sum.total != 10 || sum.in[kRoadPreload][0].bytes != 100 || sum.noArea[kRoadPreload].recs != 4) Fail("Tally::Merge did not add");
}
/* ---- T-985 step 2B: AREA_DONE (RECORD_FEED kind 10, world-server protocol 107) - each gate asks whether ITS OWN AREA is done ---- */
/* the wire: encode / decode, the bounds, every malformed form refused; kind 10 is none of the other kinds; its marker */
void t_t985s2b_area_done_wire()
{
    using namespace coopfeed;
    std::vector<unsigned short> keys; keys.push_back(0); keys.push_back(63); keys.push_back(4032); keys.push_back(4095);
    std::vector<char> b;
    if (!FeedEncodeAreaDone(&b, 7u, false, keys) || b.size() != kFeedHeader + 9 + 8) { Fail("AREA_DONE with four areas did not encode to 21 bytes"); return; }
    if ((unsigned int)(unsigned char)b[0] != kFeedAreaDone || b[1] != 0 || b[2] != 0 || b[3] != 0 || kFeedAreaDone != 10u || kFeedHeld != 9u) Fail("AREA_DONE is not kind 10 after HELD 9");
    AreaDoneMsg m;
    if (!FeedAreaDoneDecode(&b[0], b.size(), &m) || m.epoch != 7u || m.all != 0 || m.keys != keys) Fail("AREA_DONE did not decode to what was encoded");
    std::vector<char> a; const std::vector<unsigned short> none;
    if (!FeedEncodeAreaDone(&a, 3u, true, none) || a.size() != kFeedHeader + 9) { Fail("AREA_DONE all=1 did not encode to 13 bytes"); return; }
    AreaDoneMsg ma;
    if (!FeedAreaDoneDecode(&a[0], a.size(), &ma) || ma.all != 1 || !ma.keys.empty() || ma.epoch != 3u) Fail("AREA_DONE all=1 did not decode");
    std::vector<char> nb;
    std::vector<unsigned short> bad(1, (unsigned short)4096);
    if (FeedEncodeAreaDone(&nb, 1u, false, bad) || !nb.empty()) Fail("an area key out of the grid was encoded");
    std::vector<unsigned short> many(kFeedAreaDoneMax + 1, (unsigned short)1);
    if (FeedEncodeAreaDone(&nb, 1u, false, many) || !nb.empty()) Fail("more than kFeedAreaDoneMax keys were encoded");
    for (size_t n = 0; n < b.size(); ++n) if (FeedAreaDoneDecode(&b[0], n, &m)) Fail("a cut AREA_DONE decoded");
    if (FeedAreaDoneDecode(0, 13, &m)) Fail("a null AREA_DONE decoded");
    std::vector<char> t = b; t.push_back(0);
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE with a byte after its last field decoded");
    t = b; t[kFeedHeader + 4] = 2;
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE with all = 2 decoded");
    t = b; t[1] = 1;
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE with a non-zero header pad decoded");
    t = b; t[kFeedHeader + 9 + 6] = 0; t[kFeedHeader + 9 + 7] = 0x10;
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE naming area 4096 decoded");
    t = b; t[kFeedHeader + 5] = 5;
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE whose count exceeds its keys decoded");
    t = a; t[kFeedHeader + 5] = (char)0xFF; t[kFeedHeader + 6] = (char)0xFF; t[kFeedHeader + 7] = (char)0xFF; t[kFeedHeader + 8] = (char)0xFF;
    if (FeedAreaDoneDecode(&t[0], t.size(), &m)) Fail("an AREA_DONE with a huge count decoded");
    FeedMsg fm;
    if (FeedDecode(&a[0], a.size(), &fm) || IsFeedHeldKind(&a[0], a.size()) || cooppre::IsPreKind(&a[0], a.size())) Fail("AREA_DONE reads as another RECORD_FEED kind (the world server refuses one sent up as malformed)");
    std::vector<char> mk; FeedMarkerEncode(&mk, 5u, b);
    unsigned int e = 0; AreaDoneMsg mm;
    if (!IsFeedAreaDoneMarker(mk) || !FeedAreaDoneMarkerDecode(mk, &e, &mm) || e != 5u || mm.keys != keys) Fail("the AREA_DONE marker did not round-trip");
    if (FeedMarkerDecode(mk, &e, &fm)) Fail("an AREA_DONE marker read as a PAGE_END marker");
    std::vector<char> pe; FeedEncodePageEnd(&pe, "k", 1, true, 9ULL);
    std::vector<char> pmk; FeedMarkerEncode(&pmk, 5u, pe);
    if (IsFeedAreaDoneMarker(pmk) || !FeedMarkerDecode(pmk, &e, &fm)) Fail("a PAGE_END marker read as AREA_DONE");
}
/* the area key at the grid's edges and out of it: y * 64 + x (cooplive::AreaKey, zones.cpp Key()); items.cpp's x * 64 + y; zone ids;
   the stamp (generation << 32 | link) */
void t_t985s2b_area_keys()
{
    using namespace coopfeed;
    int same = 1;
    for (int y = -1; y <= 64; ++y) for (int x = -1; x <= 64; ++x) if (FeedAreaKey(x, y) != cooplive::AreaKey(x, y)) same = 0;
    if (!same) Fail("the AREA_DONE area key is not cooplive::AreaKey (y * 64 + x)");
    if (FeedAreaKey(0, 0) != 0 || FeedAreaKey(63, 0) != 63 || FeedAreaKey(0, 63) != 4032 || FeedAreaKey(63, 63) != 4095 || FeedAreaKey(64, 0) != kFeedAreaNone || FeedAreaKey(0, -1) != kFeedAreaNone)
        Fail("an area key at the grid's edge is wrong");
    if (FeedSecXYAreaKey(0) != 0 || FeedSecXYAreaKey(63) != 4032 || FeedSecXYAreaKey(64) != 1 || FeedSecXYAreaKey(4095) != 4095 || FeedSecXYAreaKey(4096) != kFeedAreaNone
        || FeedSecXYAreaKey(-1) != kFeedAreaNone || FeedSecXYAreaKey(kFeedAreaAny) != kFeedAreaNone)
        Fail("items.cpp's sector index (x * 64 + y) is not turned into the area key");
    if (FeedZoneIdAreaKey("zone.0.0") != 0 || FeedZoneIdAreaKey("zone.63.0") != 63 || FeedZoneIdAreaKey("zone.0.63") != 4032 || FeedZoneIdAreaKey("zone.12.34") != FeedAreaKey(12, 34))
        Fail("a zone id's area key is wrong");
    const char* const badIds[] = { "zone.64.0", "zone.0.64", "zone.-1.2", "zone.1", "zone.1.2x", "zone.1.2.3", "zone..2", "zone.1234.1", "Zone.1.2", "", "ground.1.2" };
    for (size_t i = 0; i < sizeof(badIds) / sizeof(badIds[0]); ++i) if (FeedZoneIdAreaKey(badIds[i]) != kFeedAreaNone) Fail(std::string("a text that is no zone id in the grid gave an area: ") + badIds[i]);
    if (FeedAreaStampMake(-1, 2) != -1 || FeedAreaStampMake(2, -1) != -1 || FeedAreaStampMake(1, 0) == FeedAreaStampMake(0, 1) || FeedAreaStampGen(FeedAreaStampMake(7, 9)) != 7 || FeedAreaStampGen(-1) != -1)
        Fail("the stamp does not carry the generation and the link apart");
    const long gen = 1, link = 4; const long long st = FeedAreaStampMake(gen, link);
    AreaDoneTable t;
    if (t.Stamp(0) != -1 || t.Stamp(4095) != -1 || t.Stamp(4096) != -1 || t.Stamp(kFeedAreaNone) != -1 || t.Stamp(kFeedAreaAny) != -1 || t.all != -1) Fail("a new table is not all -1");
    AreaDoneMsg m; m.keys.push_back(0); m.keys.push_back(4095); t.Apply(m, st);
    if (t.Stamp(0) != st || t.Stamp(4095) != st || t.Stamp(1) != -1 || t.Stamp(4096) != -1 || t.Stamp(kFeedAreaAny) != st || t.all != -1) Fail("the edge areas 0 and 4095 were not stamped alone");
    if (FeedAreaDoneOnLink(t.all, t.Stamp(4096), gen, link) != 0 || FeedAreaDoneHeld(t.all, t.Stamp(kFeedAreaNone), gen) != 0) Fail("an out-of-grid area was answered by named areas");
    FeedAreaMarkAll(&t, st);
    if (FeedAreaDoneOnLink(t.all, t.Stamp(4096), gen, link) != 1 || FeedAreaDoneHeld(t.all, t.Stamp(kFeedAreaNone), gen) != 1) Fail("an out-of-grid area is not answered by the whole table");
}
/* a stale AREA_DONE is dropped: before its snapshot's last page, from another link, from an earlier first ASK, or queued before an OFF */
void t_t985s2b_area_done_stale()
{
    using namespace coopfeed;
    FeedAskState s;
    if (FeedAreaDoneArrived(s, 5, 0) != 0) Fail("an AREA_DONE was taken before any ask");
    FeedAskTaken(&s, kAskFirst, 1, 5);
    if (s.firstAsks != 1 || FeedAreaDoneArrived(s, 5, 1) != 0) Fail("an AREA_DONE was taken while its page was still out");
    if (FeedPageEndArrived(&s, 5, "a", 0) != 1 || FeedAreaDoneArrived(s, 5, 1) != 0) Fail("an AREA_DONE was taken before the last page");
    FeedAskTaken(&s, kAskNext, 1, 5);
    if (s.firstAsks != 1) Fail("a next-page ask was counted as a first");
    if (FeedPageEndArrived(&s, 5, "b", 1) != 1 || FeedAreaDoneArrived(s, 5, 1) != 1) Fail("the AREA_DONE after the last page was not taken");
    if (FeedAreaDoneArrived(s, 6, 1) != 0 || FeedAreaDoneArrived(s, 5, 0) != 0 || FeedAreaDoneArrived(s, 5, 2) != 0) Fail("an AREA_DONE of another link or another first ask was taken");
    std::vector<char> w; const std::vector<unsigned short> none; FeedEncodeAreaDone(&w, 1u, true, none);
    std::vector<char> mk; FeedMarkerEncode(&mk, s.epoch, w);
    const unsigned int queuedAt = s.epoch;
    FeedOffTaken(&s);
    unsigned int e = 0; AreaDoneMsg m;
    if (!FeedAreaDoneMarkerDecode(mk, &e, &m) || e != queuedAt || e == s.epoch) Fail("an AREA_DONE queued before an OFF is not stale at the drain");
    FeedAskTaken(&s, kAskFirst, 2, 5);
    if (s.firstAsks != 2 || FeedAreaDoneArrived(s, 5, 1) != 0) Fail("the first snapshot's AREA_DONE was taken during the second's pages");
    if (FeedPageEndArrived(&s, 5, "", 1) != 1) Fail("the second snapshot's last page was not taken");
    if (FeedAreaDoneArrived(s, 5, 1) != 0 || FeedAreaDoneArrived(s, 5, 2) != 1) Fail("after a second first ask, the first snapshot's AREA_DONE was taken or the second's refused");
    FeedLinkDropped(&s); FeedAskTaken(&s, kAskFirst, 2, 6);
    if (s.firstAsks != 1 || s.counterLinkGen != 6) Fail("a new link did not start the first-ask count over");
}
/* area A done while area B is still pending (a partial AREA_DONE, step C): each gate answers for its own area */
void t_t985s2b_area_settled_other_pending()
{
    using namespace coopfeed;
    const int A = FeedAreaKey(10, 10), B = FeedAreaKey(11, 10);
    const long gen = 2; const long long st = FeedAreaStampMake(gen, 3);
    AreaDoneTable t; AreaDoneMsg m; m.keys.push_back((unsigned short)A); t.Apply(m, st);
    if (FeedAreaDoneOnLink(t.all, t.Stamp(A), gen, 3) != 1 || FeedAreaDoneOnLink(t.all, t.Stamp(B), gen, 3) != 0) Fail("a zone gate of area A or B answered for the other");
    if (FeedAreaDoneHeld(t.all, t.Stamp(A), gen) != 1 || FeedAreaDoneHeld(t.all, t.Stamp(B), gen) != 0 || FeedAreaDoneHeld(t.all, t.Stamp(kFeedAreaAny), gen) != 1) Fail("a snapshot gate of area A or B answered for the other");
    if (FeedAreaDoneOnLink(t.all, t.Stamp(A), gen, 4) != 0) Fail("an area done on link 3 counts on link 4");
    if (FeedAreaDoneOnLink(t.all, t.Stamp(A), gen + 1, 3) != 0 || FeedAreaDoneHeld(t.all, t.Stamp(A), gen + 1) != 0) Fail("an area done before a same-link clear still reads as done (review LOW 4)");
    AreaDoneMsg mb; mb.keys.push_back((unsigned short)B); t.Apply(mb, st);
    if (FeedAreaDoneOnLink(t.all, t.Stamp(B), gen, 3) != 1 || t.all != -1) Fail("area B done later was not stamped, or the whole table was");
}
/* THE REAL SEQUENCE (fold, review LOW 5): the last page's marker in one frame, AREA_DONE in a later one, or never; and a partial
   AREA_DONE's settle loop (store.cpp ZoneProvisionalSettle's selection) */
void t_t985s2b_marker_then_area_done()
{
    using namespace coopfeed;
    const long gen = 3, link = 2; const long long st = FeedAreaStampMake(gen, link);
    const int ks[] = { kFeedAreaAny, kFeedAreaNone, 0, 63, 4032, 4095 };
    AreaDoneTable t;
    for (size_t i = 0; i < sizeof(ks) / sizeof(ks[0]); ++i)
        if (FeedAreaDoneOnLink(t.all, t.Stamp(ks[i]), gen, link) != 0 || FeedAreaDoneHeld(t.all, t.Stamp(ks[i]), gen) != 0) Fail("an area is done before the last page's marker");
    FeedAreaMarkAll(&t, st);   /* the marker's frame: AREA_DONE has not arrived (the intake stopped at 64 messages and 2 ms) */
    for (size_t i = 0; i < sizeof(ks) / sizeof(ks[0]); ++i)
        if (FeedAreaDoneOnLink(t.all, t.Stamp(ks[i]), gen, link) != 1 || FeedAreaDoneHeld(t.all, t.Stamp(ks[i]), gen) != 1) Fail("a gate still waits for AREA_DONE after the last page's marker (MED 1)");
    /* AREA_DONE lost: nothing waits on it - the table stays done */
    if (FeedAreaDoneOnLink(t.all, t.Stamp(77), gen, link) != 1) Fail("a lost AREA_DONE leaves an area not done (MED 2)");
    /* AREA_DONE all=1 a frame later: counted only */
    if (FeedAreaDoneDecide(t.all, 1, st) != kAdAllCounted) Fail("AREA_DONE all=1 behind its marker is not counted as matching");
    if (FeedAreaDoneDecide(t.all, 1, FeedAreaStampMake(gen + 1, link)) != kAdAllMismatch || FeedAreaDoneDecide(t.all, 1, FeedAreaStampMake(gen, link + 1)) != kAdAllMismatch
        || FeedAreaDoneDecide(-1, 1, st) != kAdAllMismatch || FeedAreaDoneDecide(-1, 1, -1) != kAdAllMismatch)
        Fail("AREA_DONE all=1 naming a feed the marker did not complete is not a mismatch");
    /* step C: a partial AREA_DONE stamps its areas and settles only their provisional reads */
    AreaDoneTable p; AreaDoneMsg part; part.keys.push_back((unsigned short)FeedAreaKey(5, 5));
    if (FeedAreaDoneDecide(p.all, part.all, st) != kAdAreas) Fail("a partial AREA_DONE is not applied as named areas");
    p.Apply(part, st);
    std::map<std::string, int> prov; prov["zone.5.5"] = 1; prov["zone.6.5"] = 1; prov["zone.x"] = 1;
    int settled = 0;
    for (std::map<std::string, int>::iterator it = prov.begin(); it != prov.end(); )
    {
        if (!FeedAreaListHas(part.keys, FeedZoneIdAreaKey(it->first))) { ++it; continue; }
        ++settled; prov.erase(it++);
    }
    if (settled != 1 || prov.size() != 2 || prov.count("zone.5.5") != 0) Fail("the partial settle judged the wrong provisional reads, or kept the settled one");
    if (FeedAreaDoneOnLink(p.all, p.Stamp(FeedAreaKey(5, 5)), gen, link) != 1 || FeedAreaDoneOnLink(p.all, p.Stamp(FeedAreaKey(6, 5)), gen, link) != 0) Fail("a partial AREA_DONE answered for an area it did not name");
}
/* THE PROOF THAT 2B CHANGES NO GATE. The true old behaviour (before 2B): g_feedDoneLinkGen reset at a WELCOME only, set at the last
   page's marker; g_snapshotApplied reset at a WELCOME, a world load and a first ASK, set at the same marker. The new: the generation
   moves at a WELCOME and a world load; the marker writes the whole-table stamp; AREA_DONE all=1 only counts. Bit 1 = a zone gate
   disagrees for some area, bit 2 = a snapshot gate does. */
struct T985s2bWalk { long fd; int snap; long link; long gen; int sibling; coopfeed::AreaDoneTable t; T985s2bWalk(int sib) : fd(-1), snap(0), link(1), gen(1), sibling(sib) {} };
int T985s2bDiffer(const T985s2bWalk& o)
{
    using namespace coopfeed;
    const int oldZone = (o.fd >= 0 && o.fd == o.link) ? 1 : 0;
    int d = 0;
    for (int k = kFeedAreaAny; k < kFeedAreaKeys; ++k)
    {
        if (FeedAreaDoneOnLink(o.t.all, o.t.Stamp(k), o.gen, o.link) != oldZone) d |= 1;
        if (FeedAreaDoneHeld(o.t.all, o.t.Stamp(k), o.gen) != o.snap) d |= 2;
    }
    return d;
}
void T985s2bWelcome(T985s2bWalk* o) { o->fd = -1; o->snap = 0; ++o->gen; }
void T985s2bLoad(T985s2bWalk* o) { if (o->sibling) o->fd = -1; o->snap = 0; ++o->gen; }   /* the true old code left fd set here */
void T985s2bFirstAsk(T985s2bWalk* o) { o->snap = 0; }   /* no clear (fold LOW 3): a WELCOME or a load came before it */
void T985s2bMarker(T985s2bWalk* o)
{
    coopfeed::FeedAreaMarkAll(&o->t, coopfeed::FeedAreaStampMake(o->gen, o->link));   /* before the stamp, as store.cpp */
    o->fd = o->link; o->snap = 1;
}
int T985s2bAreaDoneAll(T985s2bWalk* o) { return coopfeed::FeedAreaDoneDecide(o->t.all, 1, coopfeed::FeedAreaStampMake(o->gen, o->link)); }   /* changes nothing */
void t_t985s2b_all_equals_old()
{
    T985s2bWalk o(0);   /* against the TRUE old behaviour - no sibling clear */
    if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree before any feed");
    T985s2bWelcome(&o); if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree at the WELCOME");
    T985s2bFirstAsk(&o); if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree at the first ASK");
    T985s2bMarker(&o); if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree at the last page's marker, AREA_DONE not yet here");
    if (T985s2bAreaDoneAll(&o) != coopfeed::kAdAllCounted || T985s2bDiffer(o) != 0) Fail("2B: AREA_DONE all=1 changed an answer or did not match its marker");
    ++o.link; if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree on a new link before its WELCOME");
    T985s2bWelcome(&o); T985s2bFirstAsk(&o); T985s2bMarker(&o);
    if (T985s2bDiffer(o) != 0) Fail("2B: the gates disagree at the new link's feed end (AREA_DONE lost)");
}
/* THE SIBLING CLEAR, its own leg: a world reloaded on the SAME link. The true old code kept g_feedDoneLinkGen set, so its zone gates
   said "done" for the reloaded world before that world's feed had ended; 2B says "not done" there until the new marker (the fix).
   The snapshot gates agree throughout; with the clear modelled (as store.cpp computes the old answer now) everything agrees. */
void t_t985s2b_sibling_clear()
{
    T985s2bWalk o(0);
    T985s2bWelcome(&o); T985s2bFirstAsk(&o); T985s2bMarker(&o);
    T985s2bLoad(&o);
    if (T985s2bDiffer(o) != 1) Fail("the sibling clear: after a same-link reload the zone gates must differ from the true old answer, and only they");
    T985s2bFirstAsk(&o); if (T985s2bDiffer(o) != 1) Fail("the sibling clear: the zone gates must still differ before the reloaded world's feed end");
    T985s2bMarker(&o); if (T985s2bDiffer(o) != 0) Fail("the sibling clear: the gates must agree again at the reloaded world's marker");
    T985s2bWalk s(1);
    T985s2bWelcome(&s); T985s2bFirstAsk(&s); T985s2bMarker(&s);
    T985s2bLoad(&s); if (T985s2bDiffer(s) != 0) Fail("with the sibling clear modelled, the gates disagree at a same-link reload");
    T985s2bFirstAsk(&s); T985s2bMarker(&s); if (T985s2bDiffer(s) != 0) Fail("with the sibling clear modelled, the gates disagree at the reloaded world's marker");
}
size_t T985s2bCount(const std::string& s, const char* text)
{
    size_t n = 0;
    for (size_t at = s.find(text); at != std::string::npos; at = s.find(text, at + 1)) ++n;
    return n;
}
/* the world server always sends AREA_DONE all=1 right after the last PAGE_END (the empty malformed last page too), numbered by the
   connection's first-page asks; the game sets the whole table at the marker before the stamp, settles there, and lets all=1 only count */
void t_t985s2b_server_sends_area_done()
{
    const std::string server = T313ReadFile("../coop-store/store_main.cpp"), game = T313ReadFile("../coop-plugin/store.cpp");
    if (server.empty() || game.empty()) { Fail("could not read the world server's store_main.cpp or the plugin's store.cpp"); return; }
    const size_t mal = server.find("coopfeed::FeedEncodePageEnd(&pe, std::string(), 0, true, seqHigh); last = true;");
    const size_t pe = server.find("SendMsg(from, MSG_RECORD_FEED, pe);");
    const size_t ad = server.find("coopfeed::FeedEncodeAreaDone(&ad, g_feedFirstAsks[from], true, noAreas);");
    if (mal == std::string::npos || pe == std::string::npos || ad == std::string::npos || !(mal < pe && pe < ad)) Fail("the world server does not encode AREA_DONE after the last PAGE_END is sent");
    else
    {
        const std::string between = server.substr(pe, ad - pe);
        if (between.find("if (last)") == std::string::npos || between.find("return") != std::string::npos || ad - pe > 400) Fail("AREA_DONE is not sent at once after every last PAGE_END");
        if (server.find("SendMsg(from, MSG_RECORD_FEED, ad); ++g_feedAreaDoneSent;", ad) == std::string::npos) Fail("the world server encodes AREA_DONE but does not send it");
    }
    if (T985s2bCount(server, "FeedEncodeAreaDone(") != 1 || T985s2bCount(game, "FeedEncodeAreaDone(") != 0) Fail("AREA_DONE is encoded somewhere else (the game never sends it)");
    if (server.find("if (m.key.empty()) ++g_feedFirstAsks[from];") == std::string::npos || server.find("g_feedFirstAsks.erase(p);") == std::string::npos)
        Fail("the world server does not number AREA_DONE by this connection's first-page asks");
    if (game.find("if (coopfeed::FeedAreaDoneArrived(g_feed, StoreNotebookLinkGen(), ad.epoch) == 0) { ++g_areaDoneStale; return; }") == std::string::npos
        || game.find("if (coopfeed::IsFeedAreaDoneMarker(payload)) { StoreAreaDoneApply(payload); return; }") == std::string::npos
        || game.find("if (epoch != g_feed.epoch) { ++g_areaDoneMarkersStale; return; }") == std::string::npos)
        Fail("the game does not drop a stale AREA_DONE at its arrival and at the drain");
    if (T985s2bCount(game, "AreaDoneClear();") != 2 || T985s2bCount(game, "::InterlockedExchange(&g_feedDoneLinkGen, -1);") != 2) Fail("the table and the feed-done stamp are not cleared at the WELCOME and a world load alone");
    const size_t all = game.find("::InterlockedExchange64(&g_areaDone.all, st);");
    const size_t stamp = game.find("::InterlockedExchange(&g_feedDoneLinkGen, ::InterlockedCompareExchange(&g_linkGen, 0, 0));");
    if (all == std::string::npos || stamp == std::string::npos || !(all < stamp) || stamp - all > 400 || T985s2bCount(game, "InterlockedExchange64(&g_areaDone.all") != 1)
        Fail("the last page's marker does not set the whole table just before the whole-feed stamp, or something else sets it (MED 1)");
    if (game.find("STEP D (filtering): this marker must stop setting all") == std::string::npos) Fail("the marker does not say step D must stop it setting the whole table");
    if (T985s2bCount(game, "ZoneProvisionalSettle(") != 3 || game.find("ZoneProvisionalSettle(0);") == std::string::npos || game.find("ZoneProvisionalSettle(&m.keys);") == std::string::npos)
        Fail("the provisional reads are not settled at the last page's marker (all) and at a partial AREA_DONE (its areas) alone (MED 2)");
    if (game.find("+ AreaDoneReportGroup()") == std::string::npos || game.find("allMismatch") == std::string::npos) Fail("gateDisagree / allMismatch are not on the [STORE] REPORT line");
}
void t_preload_aoi_ring()
{
    if (cooppre::kPreRing != areafile::kRing || cooppre::kPreGrid != areafile::kGrid || cooppre::kPreAreaMax != areafile::kMaxAreas || cooppre::kPreRing != 2)
        Fail("the load's fetch ring, grid or area bound is not the area file's (ring 2)");
    std::vector<int> xy; xy.push_back(10); xy.push_back(10);
    if (cooppre::PreAoiClass(10, 10, xy, cooppre::kPreRing) != cooppre::kPreAoiIn) Fail("the list's own area is not inside");
    if (cooppre::PreAoiClass(12, 8, xy, cooppre::kPreRing) != cooppre::kPreAoiIn || cooppre::PreAoiClass(8, 12, xy, cooppre::kPreRing) != cooppre::kPreAoiIn
        || cooppre::PreAoiClass(12, 10, xy, cooppre::kPreRing) != cooppre::kPreAoiIn) Fail("an area on the ring's edge (2 squares, straight or diagonal) is not inside");
    if (cooppre::PreAoiClass(13, 10, xy, cooppre::kPreRing) != cooppre::kPreAoiOut || cooppre::PreAoiClass(10, 7, xy, cooppre::kPreRing) != cooppre::kPreAoiOut
        || cooppre::PreAoiClass(13, 13, xy, cooppre::kPreRing) != cooppre::kPreAoiOut) Fail("an area 3 squares away is not outside");
    if (cooppre::PreAoiClass(cooppre::kPreNoArea, cooppre::kPreNoArea, xy, cooppre::kPreRing) != cooppre::kPreAoiNoArea || cooppre::PreAoiClass(64, 0, xy, cooppre::kPreRing) != cooppre::kPreAoiNoArea)
        Fail("a row with no area (or one off the grid) is not NOAREA");
    if (cooppre::PreAoiFetches(cooppre::kPreAoiIn) != 1 || cooppre::PreAoiFetches(cooppre::kPreAoiNoArea) != 1 || cooppre::PreAoiFetches(cooppre::kPreAoiNoList) != 1
        || cooppre::PreAoiFetches(cooppre::kPreAoiOut) != 0) Fail("only OUT rows are left out of the load's fetch");
    const std::vector<int> none;
    if (cooppre::PreAoiClass(13, 10, none, cooppre::kPreRing) != cooppre::kPreAoiNoList || cooppre::PreAoiClass(cooppre::kPreNoArea, cooppre::kPreNoArea, none, cooppre::kPreRing) != cooppre::kPreAoiNoList)
        Fail("with no list a row is not NOLIST (fetched)");
    xy.push_back(40); xy.push_back(40);
    if (cooppre::PreAoiClass(42, 41, xy, cooppre::kPreRing) != cooppre::kPreAoiIn || cooppre::PreAoiClass(43, 40, xy, cooppre::kPreRing) != cooppre::kPreAoiOut) Fail("the second area's ring is wrong");
    for (int x = 0; x < cooppre::kPreGrid; ++x)
        for (int y = 0; y < cooppre::kPreGrid; ++y)
            if ((cooppre::PreAoiClass(x, y, xy, cooppre::kPreRing) == cooppre::kPreAoiIn) != (areafile::InRing(x, y, xy, areafile::kRing) != 0)) { Fail("the fetch ring and the area file's measure disagree"); return; }
    /* the whole choice, as the game makes it (preload.h PreRowChoose, T-985 C2b fold 1): newer here (PreWant), no copy here, not own */
    struct Row { int ax, ay; unsigned long long file; };
    const Row rows[] = { { 11, 11, 200ULL }, { 30, 30, 200ULL }, { cooppre::kPreNoArea, cooppre::kPreNoArea, 200ULL }, { 11, 11, 0ULL } };
    int withList = 0, withoutList = 0;
    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        const int want = cooppre::PreWant(0, 0, 0, rows[i].file, 0ULL, 0, 0, 1);
        if (cooppre::PreRowChoose(cooppre::PreAoiClass(rows[i].ax, rows[i].ay, xy, cooppre::kPreRing), cooppre::kPreAoiNoArea, 0, 0, 0, 0, want).fetch != 0) ++withList;
        if (cooppre::PreRowChoose(cooppre::PreAoiClass(rows[i].ax, rows[i].ay, none, cooppre::kPreRing), cooppre::kPreAoiNoArea, 0, 0, 0, 0, want).fetch != 0) ++withoutList;
    }
    if (withList != 2) Fail("with a list the load's fetch is not the newer rows inside plus the newer row with no area");
    if (withoutList != 3) Fail("with no list the load's fetch is not every newer row");
}
/* T-985 C2b fold 1: the load's fetch choice for one row (PreRowChoose), the world server's row flags (PreRowPlace) and the
   stale-hold rule at a snapshot not seen whole (PreStaleHoldKeeps) */
void t_preload_aoi_choose()
{
    const int vIn = cooppre::kPreAoiIn, vOut = cooppre::kPreAoiOut, vNa = cooppre::kPreAoiNoArea, vNl = cooppre::kPreAoiNoList;
    cooppre::PreRowPick p;
    p = cooppre::PreRowChoose(vIn, vNa, 0, 0, 1, 0, 1);
    if (!p.fetch || !p.confirmFirst || p.confirmAfter || p.skipSet || p.skipCount || p.held || p.byLocal || p.byOwn) Fail("a newer row inside is not fetched with its kept key released first");
    p = cooppre::PreRowChoose(vIn, vNa, 0, 0, 0, 0, 0);
    if (p.fetch || !p.confirmFirst || p.skipSet || p.skipCount) Fail("a current row inside is fetched or put in the skipped list");
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 0, 0, 1);
    if (p.fetch || !p.confirmFirst || !p.skipCount || !p.skipSet || p.held || p.staleHeld) Fail("a newer row outside on both copies is not left for the feed");
    p = cooppre::PreRowChoose(vOut, vNa, 0, 0, 0, 0, 1);
    if (p.fetch || !p.skipSet) Fail("a newer row outside with no copy here is not left for the feed");
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 0, 0, 0);
    if (p.fetch || p.skipSet || p.skipCount) Fail("a row outside that is current here went into the skipped list");
    /* review HIGH 1: the server's copy outside, this game's own copy inside - fetched, counted by the local copy */
    p = cooppre::PreRowChoose(vOut, vIn, 0, 0, 0, 0, 1);
    if (!p.fetch || !p.byLocal || p.byOwn || p.skipSet || p.skipCount) Fail("a group whose copy here stands inside the ring is not fetched");
    p = cooppre::PreRowChoose(vOut, vIn, 0, 0, 1, 0, 1);
    if (!p.fetch || !p.confirmFirst || p.held) Fail("a kept key whose copy here stands inside the ring is held instead of fetched");
    /* review HIGH 2: outside on both copies, written by the asker's player - fetched, counted as own */
    p = cooppre::PreRowChoose(vOut, vOut, 1, 0, 1, 0, 1);
    if (!p.fetch || !p.byOwn || p.byLocal || p.skipSet || !p.confirmFirst) Fail("the player's own group outside the list is not fetched");
    p = cooppre::PreRowChoose(vOut, vNa, 1, 0, 0, 0, 1);
    if (!p.fetch || !p.byOwn) Fail("the player's own group with no copy here is not fetched");
    p = cooppre::PreRowChoose(vIn, vNa, 1, 0, 0, 0, 1);
    if (!p.fetch || p.byOwn) Fail("an own row inside is counted as fetched only because it is own");
    /* a held-back kept key outside, newer there: stays held and stale-held, never released here */
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 1, 0, 1);
    if (p.fetch || p.confirmFirst || p.confirmAfter || !p.held || !p.staleHeld || !p.skipSet || !p.skipCount) Fail("a held-back kept key outside with a newer server copy is released or not stale-held");
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 1, 0, 0);
    if (p.fetch || p.confirmFirst || !p.confirmAfter || p.held || p.staleHeld || p.skipSet) Fail("a held-back kept key outside that is current here is not released");
    /* review LOW 5: a kept row held beside, outside - held; in the skipped list only when the server's copy is newer */
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 1, 1, 0);
    if (p.fetch || p.confirmFirst || p.confirmAfter || !p.held || p.skipSet || p.skipCount || p.staleHeld) Fail("a kept row held beside, current there, went into the skipped list");
    p = cooppre::PreRowChoose(vOut, vOut, 0, 0, 1, 1, 1);
    if (p.fetch || p.confirmFirst || !p.held || !p.skipSet || p.skipCount || p.staleHeld) Fail("a kept row held beside, newer there, is not in the skipped list");
    /* a box or piece row: never fetched, never held - released as the index names it */
    p = cooppre::PreRowChoose(vOut, vOut, 0, 1, 1, 0, 1);
    if (p.fetch || !p.confirmFirst || p.held || p.skipSet || p.skipCount) Fail("a box or piece row is fetched, held or skipped");
    /* no area, no list: fetched */
    p = cooppre::PreRowChoose(vNa, vNa, 0, 0, 0, 0, 1);
    if (!p.fetch || p.byLocal || p.byOwn) Fail("a newer row with no area is not fetched");
    p = cooppre::PreRowChoose(vNl, vNl, 0, 0, 1, 0, 1);
    if (!p.fetch || !p.confirmFirst || p.held) Fail("with no list a newer row is not fetched");
    /* the world server's row: area and own flag */
    cooppre::IndexEntry e;
    cooppre::PreRowPlace(&e, 5, 6, 0, 1);
    if (e.ax != 5 || e.ay != 6 || e.flags != cooppre::kPreRowOwn) Fail("a group of the asker's player faction is not flagged own");
    cooppre::PreRowPlace(&e, 5, 6, 1, 1);
    if (e.ax != 5 || e.flags != 0u) Fail("a key-named record the asker's player wrote is flagged own");
    cooppre::PreRowPlace(&e, 5, 6, 0, 0);
    if (e.flags != 0u) Fail("a group not of the asker's player faction is flagged own");
    cooppre::PreRowPlace(&e, -1, -1, 0, 1);
    if (e.ax != cooppre::kPreNoArea || e.ay != cooppre::kPreNoArea || e.flags != cooppre::kPreRowOwn) Fail("an own group with no area lost its flag or got an area");
    cooppre::PreRowPlace(&e, 64, 0, 0, 0);
    if (e.ax != cooppre::kPreNoArea || e.ay != cooppre::kPreNoArea) Fail("an area off the grid is not none");
    /* T-985 C2b fold 2 (re-check MED 1) + fold 3: own = a group whose stored faction field is one of the asker's names */
    {
        std::vector<std::string> fac; fac.push_back("Nameless 2"); fac.push_back("Iron Hands");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2", fac) != 1 || cooppre::PreRowOfAskersFaction(0, "Iron Hands", fac) != 1)
            Fail("a group carrying any of the asker's names is not own");
        if (cooppre::PreRowOfAskersFaction(0, "12345-gamedata.base", fac) != 0) Fail("a world NPC group (an NPC faction's code) is own");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 3", fac) != 0) Fail("another player's group is own");
        if (cooppre::PreRowOfAskersFaction(1, "Nameless 2", fac) != 0) Fail("a key-named record of the asker's faction is own");
        if (cooppre::PreRowOfAskersFaction(0, "", fac) != 0) Fail("a record with no faction field is own");
        std::vector<std::string> none;
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2", none) != 0) Fail("an asker with no profile row gets own rows");
        std::vector<std::string> blank(1, std::string());
        if (cooppre::PreRowOfAskersFaction(0, "", blank) != 0) Fail("an empty faction name matches an empty field");
        cooppre::PreRowPlace(&e, 5, 6, 0, cooppre::PreRowOfAskersFaction(0, "Nameless 2", fac));
        if (e.flags != cooppre::kPreRowOwn) Fail("the faction rule's answer does not set the own flag");
    }
    /* T-985 C2b fold 3 (re-check MED, under-fetch): the asker's names - its profile's, and the clash and placeholder names a stand-in
       for its slot takes on another game (each leg but the last three answered 0 by fold 2's rule, which held only the row's name) */
    {
        std::vector<std::string> n; cooppre::PreAskerFactionNames("Nameless 2", 3, &n);
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2", n) != 1) Fail("fold 3: the asker's own faction name is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2 (peer)", n) != 1) Fail("fold 3: a stand-in's first clash name is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2 (peer) (p3)", n) != 1) Fail("fold 3: a stand-in's clash name for the asker's slot is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 3", n) != 1) Fail("fold 3: the asker's slot's placeholder name is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 3 (2)", n) != 1 || cooppre::PreRowOfAskersFaction(0, "Player 3 (peer)", n) != 1
            || cooppre::PreRowOfAskersFaction(0, "Player 3 (peer) (p3)", n) != 1) Fail("fold 3: a placeholder's other names for the asker's slot are not own");
        { std::vector<std::string> m; cooppre::PreAskerFactionNames("ABCDEFGHIJKLMNOPQRST", 3, &m); std::string cut;
          if (!coopprof::FactionForRow("ABCDEFGHIJKLMNOPQRST (peer)", &cut) || cooppre::PreRowOfAskersFaction(0, cut, m) != 1)
              Fail("fold 3: a long name's clash name, cut as the row's form cuts a record's field, is not own"); }
        /* fold 4 (re-check MED): the forms the stand-in naming paths also produce - each answered 0 by fold 3's set */
        if (cooppre::PreRowOfAskersFaction(0, "Player 2", n) != 1) Fail("fold 4: the name a stand-in takes from an empty wire name is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2 (2)", n) != 1 || cooppre::PreRowOfAskersFaction(0, "Nameless 2 (2) (peer)", n) != 1)
            Fail("fold 4: the player's name taken as the writing game's own faction name (and its clash form) is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 3 (2) (peer)", n) != 1 || cooppre::PreRowOfAskersFaction(0, "Player 3 (2) (peer) (p3)", n) != 1)
            Fail("fold 4: a ' (2)' placeholder's clash names are not own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 2 (2) (peer) (p3)", n) != 1) Fail("fold 4: the empty-name stand-in's clash name for the asker's slot is not own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 2 (peer) (p4)", n) != 0) Fail("fold 4: another slot's clash name of the empty-name stand-in is own");
        if (cooppre::PreRowOfAskersFaction(0, "Nameless 2 (peer) (p4)", n) != 0) Fail("fold 3: another slot's clash name is own");
        if (cooppre::PreRowOfAskersFaction(0, "Player 4", n) != 0) Fail("fold 3: another slot's placeholder name is own");
        if (cooppre::PreRowOfAskersFaction(0, "12345-gamedata.base", n) != 0) Fail("fold 3: a world NPC group (an NPC faction's code) is own");
        if (cooppre::PreRowOfAskersFaction(0, "", n) != 0) Fail("fold 3: a record with no faction field is own");
        if (cooppre::PreRowOfAskersFaction(1, "Nameless 2 (peer)", n) != 0) Fail("fold 3: a key-named record carrying a clash name is own");
        for (size_t i = 0; i < n.size(); ++i)
        {
            if (n[i].empty()) Fail("fold 3: the asker's names hold an empty name");
            for (size_t j = i + 1; j < n.size(); ++j) if (n[i] == n[j]) Fail("fold 3: the asker's names hold a name twice");
        }
        std::vector<std::string> noSlot; cooppre::PreAskerFactionNames("Nameless 2", -1, &noSlot);
        if (noSlot.size() != 1 || noSlot[0] != "Nameless 2") Fail("fold 3: with no slot, a name other than the profile's is used");
        std::vector<std::string> noRow; cooppre::PreAskerFactionNames("", 3, &noRow);
        if (cooppre::PreRowOfAskersFaction(0, "Player 3", noRow) != 1 || cooppre::PreRowOfAskersFaction(0, " (peer)", noRow) != 0
            || cooppre::PreRowOfAskersFaction(0, "Nameless 2", noRow) != 0) Fail("fold 3: with no profile name, a name other than the slot's placeholder names is used");
        std::vector<std::string> none(1, std::string("stale")); cooppre::PreAskerFactionNames("", -1, &none);
        if (!none.empty()) Fail("fold 3: no profile name and no slot give a name (or the earlier set is kept)");
    }
    /* review MED 4: held past the first snapshot only when it was not whole */
    if (cooppre::PreStaleHoldKeeps(0, 1, 1) != 1 || cooppre::PreStaleHoldKeeps(1, 1, 1) != 0 || cooppre::PreStaleHoldKeeps(0, 0, 1) != 0 || cooppre::PreStaleHoldKeeps(0, 1, 0) != 0)
        Fail("a stale-held kept key is released by a snapshot not seen whole, or held past a whole one");
}
/* ---- T-346 slice 1: THE RECORDS BEFORE THE LOAD (src/common/preload.h; world-server protocol 67, RECORD_FEED kinds 5-8) ---- */
void t_t346_wire_roundtrip()
{
    std::vector<char> b; cooppre::PreMsg m;
    if (!cooppre::PreEncodeIndexAsk(&b, 7u, "") || !cooppre::PreDecode(&b[0], b.size(), &m) || m.kind != (unsigned)cooppre::kPreIndexAsk || m.token != 7u || !m.key.empty() || b.size() != 16) Fail("INDEX_ASK(\"\") did not round-trip");
    b.clear();
    if (!cooppre::PreEncodeIndexAsk(&b, 8u, "zone.3.4") || !cooppre::PreDecode(&b[0], b.size(), &m) || m.key != "zone.3.4" || m.token != 8u) Fail("INDEX_ASK(key) did not round-trip");
    std::vector<cooppre::IndexEntry> es; es.push_back(cooppre::IndexEntry("Holy Nation|42", 1700000000ULL, 1700000050ULL)); es.push_back(cooppre::IndexEntry("zone.1.2", 1700000001ULL, 0ULL));
    b.clear();
    if (!cooppre::PreEncodeIndexPage(&b, 9u, true, es) || !cooppre::PreDecode(&b[0], b.size(), &m) || m.kind != (unsigned)cooppre::kPreIndexPage || m.last != 1 || m.entries.size() != 2
        || m.entries[0].key != "Holy Nation|42" || m.entries[0].fileStamp != 1700000000ULL || m.entries[0].posAt != 1700000050ULL || m.entries[1].key != "zone.1.2" || m.entries[1].posAt != 0ULL) Fail("INDEX_PAGE did not round-trip");
    std::vector<std::string> ks; ks.push_back("a"); ks.push_back("zone.9.9");
    b.clear();
    if (!cooppre::PreEncodeFetch(&b, 10u, false, ks) || !cooppre::PreDecode(&b[0], b.size(), &m) || m.kind != (unsigned)cooppre::kPreFetch || m.keys != ks || m.token != 10u || m.resume != 0u) Fail("FETCH did not round-trip");
    b.clear(); if (!cooppre::PreEncodeFetch(&b, 10u, true, ks) || !cooppre::PreDecode(&b[0], b.size(), &m) || m.resume != 1u || m.keys != ks) Fail("a resumed FETCH did not round-trip (T-346 fold 1)");
    b.clear(); cooppre::PreEncodeFetchEnd(&b, 11u, 2u, 1u);
    if (b.size() != 16 || !cooppre::PreDecode(&b[0], b.size(), &m) || m.kind != (unsigned)cooppre::kPreFetchEnd || m.answered != 2u || m.sent != 1u || m.token != 11u) Fail("FETCH_END did not round-trip");
    /* refusals: a trailing byte, a bad last flag, too many keys, a key over the bound, the feed's own kinds; and the feed refuses ours */
    b.clear(); cooppre::PreEncodeFetchEnd(&b, 1u, 0u, 0u); b.push_back(0);
    if (cooppre::PreDecode(&b[0], b.size(), &m)) Fail("a trailing byte was accepted");
    b.clear(); cooppre::PreEncodeIndexPage(&b, 1u, false, es); b[8] = 2;
    if (cooppre::PreDecode(&b[0], b.size(), &m)) Fail("an INDEX_PAGE last flag of 2 was accepted");
    std::vector<std::string> many(cooppre::kPreFetchBatch + 1, std::string("k")); b.clear();
    if (cooppre::PreEncodeFetch(&b, 1u, false, many) || !b.empty()) Fail("a FETCH over the batch was encoded");
    b.clear();
    if (cooppre::PreEncodeIndexAsk(&b, 1u, std::string(coopfeed::kFeedKeyMax + 1, 'x')) || !b.empty()) Fail("an INDEX_ASK key over the bound was encoded");
    std::vector<char> ask; coopfeed::FeedEncodeAsk(&ask, "");
    if (cooppre::IsPreKind(&ask[0], ask.size()) || cooppre::PreDecode(&ask[0], ask.size(), &m)) Fail("the feed's ASK was taken for a preload kind");
    b.clear(); cooppre::PreEncodeFetchEnd(&b, 1u, 0u, 0u); coopfeed::FeedMsg fm;
    if (!cooppre::IsPreKind(&b[0], b.size()) || coopfeed::FeedDecode(&b[0], b.size(), &fm)) Fail("FETCH_END is not told apart from the feed's kinds");
}
void t_t346_want_rule()
{
    /* have, localFile, localPos, srvFile, srvPos, deleted, zone, zoneOn -> want */
    if (cooppre::PreWant(0, 0, 0, 100ULL, 100ULL, 0, 0, 1) != 1) Fail("a group missing here was not fetched");
    if (cooppre::PreWant(0, 0, 0, 100ULL, 0ULL, 0, 1, 1) != 1) Fail("an area missing here was not fetched");
    if (cooppre::PreWant(0, 0, 0, 0ULL, 0ULL, 0, 0, 1) != 0) Fail("an empty index row was fetched");
    if (cooppre::PreWant(1, 100, 100, 101ULL, 101ULL, 0, 0, 1) != 1) Fail("a newer group file was not fetched");
    if (cooppre::PreWant(1, 100, 100, 100ULL, 100ULL, 0, 0, 1) != 0) Fail("an equal group was fetched");
    if (cooppre::PreWant(1, 200, 200, 100ULL, 100ULL, 0, 0, 1) != 0) Fail("an OLDER group was fetched (the apply would refuse it)");
    if (cooppre::PreWant(1, 100, 100, 100ULL, 150ULL, 0, 0, 1) != 1) Fail("a newer group POSITION was not fetched");
    if (cooppre::PreWant(1, 100, 100, 100ULL, 150ULL, 0, 1, 1) != 0) Fail("an area was fetched on a position time alone");
    if (cooppre::PreWant(1, 100, 100, 101ULL, 0ULL, 0, 1, 1) != 1) Fail("a newer area was not fetched");
    if (cooppre::PreWant(1, 100, 100, 101ULL, 0ULL, 0, 1, 0) != 0) Fail("an area was fetched with area records off");
    if (cooppre::PreWant(0, 0, 0, 101ULL, 101ULL, 1, 0, 1) != 0) Fail("a group deleted here was fetched");
    if (cooppre::PreWant(1, 0, 100, 101ULL, 0ULL, 0, 0, 1) != 1) Fail("a full record over a position-only entry was not fetched");
}
struct T346Rec { long long file, pos; T346Rec() : file(0), pos(0) {} T346Rec(long long f, long long p) : file(f), pos(p) {} };
struct T346Stamp { void operator()(const T346Rec& r, unsigned long long* f, unsigned long long* p) const { *f = (unsigned long long)r.file; *p = (unsigned long long)r.pos; } };
void t_t346_index_walk_pages()
{
    std::map<std::string, T346Rec> srv; char kb[32];
    for (int i = 0; i < 10000; ++i) { std::sprintf(kb, "k%05d", i); srv[kb] = T346Rec(1000 + i, 2000 + i); }
    T346Stamp st; std::string after; int pages = 0; bool last = false; std::vector<cooppre::IndexEntry> all;
    while (!last && pages < 100)
    {
        std::vector<cooppre::IndexEntry> es; std::string lk;
        last = cooppre::PreIndexWalk(srv, after, cooppre::kPreIndexPageEntries, cooppre::kPreIndexPageBytes, st, &es, &lk);
        if (es.size() > cooppre::kPreIndexPageEntries || es.empty()) { Fail("a page was empty or over the cap"); return; }
        std::vector<char> b; cooppre::PreMsg m;
        if (!cooppre::PreEncodeIndexPage(&b, 1u, last, es) || !cooppre::PreDecode(&b[0], b.size(), &m) || m.entries.size() != es.size()) { Fail("a page did not round-trip"); return; }
        all.insert(all.end(), es.begin(), es.end()); after = lk; ++pages;
    }
    if (pages != 3 || all.size() != srv.size()) Fail("10000 rows did not come in 3 pages of at most 4096");
    for (size_t i = 0; i < all.size(); ++i)
    {
        std::sprintf(kb, "k%05d", (int)i);
        if (all[i].key != kb || all[i].fileStamp != (unsigned long long)(1000 + i) || all[i].posAt != (unsigned long long)(2000 + i)) { Fail("a row is missing, out of order or wrong"); return; }
    }
    std::map<std::string, T346Rec> none; std::vector<cooppre::IndexEntry> es; std::string lk = "x";
    if (!cooppre::PreIndexWalk(none, std::string(), 4096, 1024, st, &es, &lk) || !es.empty() || !lk.empty()) Fail("an empty world is not one empty LAST page");
}
/* the whole exchange against a model world server: which records are fetched, a failed write stays stale, stale answers are ignored */
struct T346LocalWant
{
    std::map<std::string, T346Rec>* local;
    bool operator()(const cooppre::IndexEntry& e) const
    {
        std::map<std::string, T346Rec>::const_iterator it = local->find(e.key);
        const bool z = e.key.compare(0, 5, "zone.") == 0;
        return cooppre::PreWant(it != local->end() ? 1 : 0, it != local->end() ? it->second.file : 0, it != local->end() ? it->second.pos : 0, e.fileStamp, e.posAt, e.key == "gone-here" ? 1 : 0, z ? 1 : 0, 1) != 0;
    }
};
void t_t346_flow_fetches_exactly_the_newer()
{
    std::map<std::string, T346Rec> srv, local; char kb[32];
    for (int i = 0; i < 600; ++i) { std::sprintf(kb, "g%04d", i); srv[kb] = T346Rec(100, 100); local[kb] = T346Rec(100, 100); }   /* equal: never fetched */
    for (int i = 0; i < 300; ++i) { std::sprintf(kb, "g%04d", i * 2); srv[kb] = T346Rec(200, 200); }                               /* 300 newer there */
    srv["zone.5.5"] = T346Rec(300, 0); local["zone.5.5"] = T346Rec(250, 250);   /* newer area */
    srv["zone.6.6"] = T346Rec(300, 0);                                            /* area this game never had */
    srv["gone-here"] = T346Rec(900, 900);                                         /* deleted here: never fetched */
    local["g0001"] = T346Rec(500, 500);                                           /* newer HERE: never fetched */
    cooppre::PreState s; cooppre::PreBegin(&s, 42u, 3);
    T346Stamp st; T346LocalWant w; w.local = &local;
    std::vector<std::string> keys; int act = cooppre::kActNone; std::string after; int rounds = 0;
    /* a stale page (an older token) changes nothing */
    { cooppre::PreMsg old; old.kind = cooppre::kPreIndexPage; old.token = 41u; old.last = 1; old.entries.push_back(cooppre::IndexEntry("g0000", 999ULL, 999ULL));
      if (cooppre::PreOnIndexPage(&s, old, w, &keys) != cooppre::kActNone || !s.want.empty()) Fail("a page of another token was taken"); }
    for (;;)
    {
        std::vector<cooppre::IndexEntry> es; std::string lk;
        const bool last = cooppre::PreIndexWalk(srv, after, 100, cooppre::kPreIndexPageBytes, st, &es, &lk);   /* small pages: several */
        std::vector<char> b; cooppre::PreMsg m; cooppre::PreEncodeIndexPage(&b, 42u, last, es); cooppre::PreDecode(&b[0], b.size(), &m);
        act = cooppre::PreOnIndexPage(&s, m, w, &keys); after = lk;
        if (act != cooppre::kActAskIndex || ++rounds > 50) break;
        if (s.afterKey != lk) { Fail("the next ask is not after the last row"); return; }
    }
    if (act != cooppre::kActFetch) { Fail("the index did not end in a FETCH"); return; }
    if (s.want.size() != 302) { Fail("not exactly the 300 newer groups and the two areas were wanted"); return; }
    for (size_t i = 0; i < s.want.size(); ++i) if (s.want[i] == "g0001" || s.want[i] == "gone-here" || s.want[i] == "g0003") { Fail("a record that is not newer there was wanted"); return; }
    int batches = 0; size_t asked = 0;
    while (act == cooppre::kActFetch && batches < 10)
    {
        ++batches; asked += keys.size();
        if (keys.size() > cooppre::kPreFetchBatch) { Fail("a batch over the cap"); return; }
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == "zone.6.6") s.failed.insert(keys[i]);   /* its write failed */
        if (cooppre::PreWaitDecide(s.phase, 1, 10u, cooppre::kPreBoundMs) != cooppre::kPreWait) { Fail("the wait let the load go mid-fetch"); return; }
        cooppre::PreMsg end; end.kind = cooppre::kPreFetchEnd; end.token = 42u; end.answered = (unsigned)keys.size(); end.sent = (unsigned)keys.size();
        cooppre::PreMsg wrong = end; wrong.token = 7u;
        if (cooppre::PreOnFetchEnd(&s, wrong, &keys) != cooppre::kActNone) { Fail("a FETCH_END of another token was taken"); return; }
        act = cooppre::PreOnFetchEnd(&s, end, &keys);
    }
    if (act != cooppre::kActDone || s.phase != cooppre::kPhDone || batches != 2 || asked != 302) { Fail("the fetch did not end Done after 2 batches of all 302"); return; }
    std::set<std::string> stale; cooppre::PreStaleKeys(s, &stale);
    if (stale.size() != 1 || stale.count("zone.6.6") != 1) Fail("the stale keys are not exactly the one whose write failed");
    if (cooppre::PreWaitDecide(s.phase, 1, 10u, cooppre::kPreBoundMs) != cooppre::kPreGo) Fail("Done did not let the load go");
    /* nothing newer: the index ends Done at once, no FETCH */
    cooppre::PreState s2; cooppre::PreBegin(&s2, 1u, 1); std::map<std::string, T346Rec> same = srv; T346LocalWant w2; w2.local = &same;
    std::vector<cooppre::IndexEntry> es2; std::string lk2; cooppre::PreIndexWalk(srv, std::string(), 4096, cooppre::kPreIndexPageBytes, st, &es2, &lk2);
    cooppre::PreMsg m2; m2.kind = cooppre::kPreIndexPage; m2.token = 1u; m2.last = 1; m2.entries = es2;
    if (cooppre::PreOnIndexPage(&s2, m2, w2, &keys) != cooppre::kActDone || s2.phase != cooppre::kPhDone || !keys.empty()) Fail("an up-to-date mirror still fetched");
}
void t_t346_wait_exit_rule()
{
    const unsigned int B = cooppre::kPreBoundMs;
    if (B != 60000u) Fail("the bound is not 60 s");
    if (cooppre::PreWaitDecide(cooppre::kPhIndex, 1, 0u, B) != cooppre::kPreWait) Fail("the index wait did not hold");
    if (cooppre::PreWaitDecide(cooppre::kPhFetch, 1, B - 1, B) != cooppre::kPreWait) Fail("the fetch wait let go before the bound");
    if (cooppre::PreWaitDecide(cooppre::kPhFetch, 1, B, B) != cooppre::kPreStaleTimeout) Fail("the bound did not end the wait");
    if (cooppre::PreWaitDecide(cooppre::kPhIndex, 0, 5u, B) != cooppre::kPreStaleLink) Fail("a failed link did not end the wait at once");
    if (cooppre::PreWaitDecide(cooppre::kPhFetch, 0, B + 5, B) != cooppre::kPreStaleLink) Fail("a failed link past the bound is not named as the link");
    if (cooppre::PreWaitDecide(cooppre::kPhDone, 0, B + 5, B) != cooppre::kPreGo) Fail("Done did not win over a later link failure");
    if (cooppre::PreWaitDecide(cooppre::kPhIdle, 1, 0u, B) != cooppre::kPreGo || cooppre::PreWaitDecide(cooppre::kPhReleased, 1, 0u, B) != cooppre::kPreGo) Fail("a state not in flight held the load");
    /* a page that says "more" with no rows ends the index (it cannot ask forever) */
    cooppre::PreState s; cooppre::PreBegin(&s, 5u, 1); cooppre::PreMsg m; m.kind = cooppre::kPreIndexPage; m.token = 5u; m.last = 0; std::vector<std::string> keys;
    T346Stamp unused; (void)unused; std::map<std::string, T346Rec> l; T346LocalWant w; w.local = &l;
    if (cooppre::PreOnIndexPage(&s, m, w, &keys) != cooppre::kActDone || s.phase != cooppre::kPhDone) Fail("an empty 'more' page did not end the index");
}
/* T-346 fold 1 (review 2026-09-30, MED 1): a FETCH answer stops at ~1 MB and says how many keys it covered; the game settles only
   those and asks again (resume) for the rest - every key is eventually answered, and the world server writes out once a batch */
struct T346F1Send { size_t n; T346F1Send() : n(0) {} size_t operator()(const T346Rec& r) { ++n; return (size_t)r.file; } };
void t_t346f1_fetch_cut_by_bytes()
{
    std::map<std::string, T346Rec> srv; char kb[32];
    for (int i = 0; i < 300; ++i) { std::sprintf(kb, "g%04d", i); if (i != 7) srv[kb] = T346Rec(i == 5 ? 3000000 : 300000, 1); }   /* 300 KB each, g0005 3 MB, g0007 gone since the index */
    cooppre::PreState s; cooppre::PreBegin(&s, 9u, 1);
    for (int i = 0; i < 300; ++i) { std::sprintf(kb, "g%04d", i); s.want.push_back(kb); }
    std::vector<std::string> keys;
    if (!cooppre::PreNextBatch(&s, &keys)) { Fail("no first batch"); return; }
    int answers = 0, flushes = 0, cuts = 0; std::set<std::string> answeredKeys; int act = cooppre::kActFetch;
    while (act == cooppre::kActFetch && answers < 1000)
    {
        std::vector<char> b; cooppre::PreMsg up;
        if (!cooppre::PreEncodeFetch(&b, 9u, s.resume != 0, keys) || !cooppre::PreDecode(&b[0], b.size(), &up) || up.keys != keys) { Fail("a FETCH did not round-trip"); return; }
        if (up.resume == 0) ++flushes;
        T346F1Send snd; unsigned int sent = 0, missing = 0;
        const unsigned int ans = cooppre::PreFetchWalk(srv, up.keys, coopfeed::kFeedPageBytes, snd, &sent, &missing);
        if (ans == 0 || ans > up.keys.size() || sent + missing != ans || snd.n != sent) { Fail("an answer covered no key, too many, or miscounted"); return; }
        if (sent > 4) { Fail("an answer went past the ~1 MB cap (" + I((int)sent) + " records of 300 KB)"); return; }   /* records, not keys: a key gone since the index costs no bytes */
        if (ans < up.keys.size()) ++cuts;
        for (unsigned int i = 0; i < ans; ++i) answeredKeys.insert(up.keys[i]);
        cooppre::PreMsg end; b.clear(); cooppre::PreEncodeFetchEnd(&b, 9u, ans, sent);
        if (!cooppre::PreDecode(&b[0], b.size(), &end) || end.answered != ans) { Fail("a FETCH_END did not round-trip"); return; }
        act = cooppre::PreOnFetchEnd(&s, end, &keys); ++answers;
    }
    std::set<std::string> stale; cooppre::PreStaleKeys(s, &stale);
    if (act != cooppre::kActDone || s.phase != cooppre::kPhDone || answeredKeys.size() != 300 || !stale.empty() || s.settled.size() != 300) { Fail("not every key was answered and settled"); return; }
    if (flushes != 2) Fail("the world server would write out " + I(flushes) + " times, not once a batch (2)");
    if (cuts < 2 || (unsigned long long)cuts != s.cuts) Fail("the answers were not cut, or the cuts were not counted");
    /* an answer that covers nothing ends the fetch; its keys stay not current */
    cooppre::PreState z; cooppre::PreBegin(&z, 3u, 1); z.want.push_back("a"); z.want.push_back("b"); std::vector<std::string> k2;
    cooppre::PreNextBatch(&z, &k2);
    cooppre::PreMsg e0; e0.kind = cooppre::kPreFetchEnd; e0.token = 3u; e0.answered = 0;
    if (cooppre::PreOnFetchEnd(&z, e0, &k2) != cooppre::kActDone || z.phase != cooppre::kPhDone) { Fail("an answer of no keys did not end the fetch"); return; }
    cooppre::PreStaleKeys(z, &stale); if (stale.size() != 2) Fail("the keys of an answer of none were settled");
    /* a FETCH resume byte of 2 is refused */
    std::vector<char> b; std::vector<std::string> one(1, std::string("k")); cooppre::PreEncodeFetch(&b, 1u, true, one); cooppre::PreMsg bad; b[8] = 2;
    if (cooppre::PreDecode(&b[0], b.size(), &bad)) Fail("a FETCH resume byte of 2 was accepted");
}
/* T-346 fold 1 (MED 2): the index named a newer file but the record came with its position only (unreadable there) - not current,
   asked once more; a second position-only answer leaves it not current (never a third ask) */
void t_t346f1_position_only_not_current()
{
    cooppre::PreState s; cooppre::PreBegin(&s, 4u, 1);
    cooppre::PreMsg pg; pg.kind = cooppre::kPreIndexPage; pg.token = 4u; pg.last = 1;
    pg.entries.push_back(cooppre::IndexEntry("g1", 200ULL, 200ULL)); pg.entries.push_back(cooppre::IndexEntry("g2", 200ULL, 200ULL)); pg.entries.push_back(cooppre::IndexEntry("g3", 0ULL, 300ULL));
    std::map<std::string, T346Rec> local; local["g1"] = T346Rec(100, 100); local["g2"] = T346Rec(100, 100); local["g3"] = T346Rec(100, 100);
    T346LocalWant w; w.local = &local; std::vector<std::string> keys;
    if (cooppre::PreOnIndexPage(&s, pg, w, &keys) != cooppre::kActFetch || keys.size() != 3) { Fail("the three were not fetched"); return; }
    if (!cooppre::PrePositionOnlyShort(s, "g1", 100) || cooppre::PrePositionOnlyShort(s, "g3", 100) || cooppre::PrePositionOnlyShort(s, "g1", 200)) { Fail("the position-only rule is wrong"); return; }
    s.failed.insert("g1"); s.posOnly.insert("g1");   /* what PreloadMirrorApply does for g1 (its file unreadable there) */
    cooppre::PreMsg end; end.kind = cooppre::kPreFetchEnd; end.token = 4u; end.answered = 3;
    if (cooppre::PreOnFetchEnd(&s, end, &keys) != cooppre::kActFetch || keys.size() != 1 || keys[0] != "g1" || s.resume != 0) { Fail("g1 was not asked once more"); return; }
    std::set<std::string> stale; cooppre::PreStaleKeys(s, &stale);
    if (stale.size() != 1 || stale.count("g1") != 1) { Fail("g1 is not the one key not current between the two asks"); return; }
    s.failed.insert("g1"); s.posOnly.insert("g1");   /* unreadable again */
    end.answered = 1;
    if (cooppre::PreOnFetchEnd(&s, end, &keys) != cooppre::kActDone) { Fail("g1 was asked a third time"); return; }
    cooppre::PreStaleKeys(s, &stale);
    if (stale.size() != 1 || stale.count("g1") != 1 || s.settled.count("g2") != 1 || s.settled.count("g3") != 1) Fail("g1 does not stay not current, or g2 / g3 were not settled");
}
/* ---- T-313 review fold 1 ------------------------------------------------------------------------------------------------ */
/* H1, at the pure header's level: a game at the title holds X, Y, Z from its save; another game deletes Y (no group number, so no
   DELETED_BITS) while this one is NOT subscribed; it loads (its arrival queue drains), ASKs, is paged the survivors, and its push UP
   sends every record it holds that the pages did not name (StoreRepushUpTick; the world server keeps no tombstone). Returns 1 when
   Y is back in the world or the game. oldRule = the delete is withheld as before the fold (the control: the model must see it). */
int T313F1GoneRoad(int oldRule)
{
    std::map<std::string, std::string> world, game;
    world["X"] = "x"; world["Y"] = "y"; world["Z"] = "z"; game = world;
    std::vector<std::string> arrival;
    world.erase("Y");
    const int d = oldRule ? (int)coopfeed::kFwdWithheld : coopfeed::FeedForwardDecide(coopfeed::kMsgRecordGone, 1, 0, 0);
    if (d == coopfeed::kFwdSend) arrival.push_back("Y");   /* queued, scope SAVE: it survives the load */
    for (size_t i = 0; i < arrival.size(); ++i) game.erase(arrival[i]);   /* applied after the load, ahead of the pages */
    std::map<std::string, std::string> got; T313Sink sink; sink.dst = &got;
    std::string after; unsigned int sent = 0; bool last = false; int pages = 0;
    while (!last && pages < 100) { std::string lk; last = coopfeed::FeedPageWalk(world, after, 4, sink, &lk, &sent); after = lk; ++pages; }
    for (std::map<std::string, std::string>::const_iterator it = got.begin(); it != got.end(); ++it) game[it->first] = it->second;
    for (std::map<std::string, std::string>::const_iterator it = game.begin(); it != game.end(); ++it) if (got.find(it->first) == got.end()) world[it->first] = it->second;
    if (world != game) return 2;
    return (world.count("Y") != 0 || game.count("Y") != 0) ? 1 : 0;
}
void t_t313f1_gone_reaches_unsubscribed()
{
    if (coopfeed::FeedForwardDecide(coopfeed::kMsgRecordGone, 1, 0, 0) != coopfeed::kFwdSend) Fail("a RECORD_GONE was withheld from a connected game that has not asked");
    if (coopfeed::FeedForwardDecide(coopfeed::kMsgRecordGone, 1, 1, 0) != coopfeed::kFwdSkip || coopfeed::FeedForwardDecide(coopfeed::kMsgRecordGone, 0, 0, 1) != coopfeed::kFwdSkip) Fail("a RECORD_GONE went to its sender or to a connection that is not connected");
    const int now = T313F1GoneRoad(0), before = T313F1GoneRoad(1);
    if (now != 0) Fail("a record deleted while the game was not subscribed is back (" + I(now) + ")");
    if (before != 1) Fail("the model does not show the pre-fold loss (a withheld delete brings the record back) - the test proves nothing");
}
void t_t313f1_seq_rises_within_a_link()
{
    if (coopfeed::FeedSeqNoteDecide(10ULL, 3, 9ULL, 3) != 0) Fail("an older notebook number on the same link was written");
    if (coopfeed::FeedSeqNoteDecide(10ULL, 3, 10ULL, 3) != 1 || coopfeed::FeedSeqNoteDecide(10ULL, 3, 11ULL, 3) != 1) Fail("an equal or newer number on the same link was refused");
    if (coopfeed::FeedSeqNoteDecide(10ULL, 3, 5ULL, 4) != 1) Fail("a lower number on a NEW link (a REPAIR) was refused");
    if (coopfeed::FeedSeqNoteDecide(0ULL, -1, 5ULL, 3) != 1) Fail("a record never stamped refused its first number");
    /* the M1 shape: the game writes after its ASK; the page (seq 10) waits in the queue, the echo (seq 11) is applied at once, the page drains */
    unsigned long long seq = 0ULL; long link = -1; const long cur = 7;
    if (coopfeed::FeedSeqNoteDecide(seq, link, 11ULL, cur)) { seq = 11ULL; link = cur; }
    if (coopfeed::FeedSeqNoteDecide(seq, link, 10ULL, cur)) { seq = 10ULL; link = cur; }
    if (seq != 11ULL) Fail("the page's drain wound the echo's number back (one behind)");
}
void t_t313f1_begin_and_off_gates()
{
    coopfeed::FeedAskState t;
    if (coopfeed::FeedBeginArrived(t, 9) != 0) Fail("a BEGIN with no ask out was taken as current");
    coopfeed::FeedAskTaken(&t, coopfeed::kAskFirst, 1, 9);
    if (coopfeed::FeedBeginArrived(t, 9) != 1) Fail("the BEGIN of the latest ask was taken as stale");
    if (coopfeed::FeedBeginArrived(t, 10) != 0) Fail("a BEGIN on another link was taken as current");
    coopfeed::FeedOffTaken(&t);
    if (coopfeed::FeedBeginArrived(t, 9) != 0) Fail("a BEGIN after an OFF (no ask out) was taken as current");
    coopfeed::FeedAskTaken(&t, coopfeed::kAskFirst, 2, 9);
    if (coopfeed::FeedBeginArrived(t, 9) != 0) Fail("the BEGIN of an ask made before the OFF was taken as current");
    if (coopfeed::FeedPageEndArrived(&t, 9, "old", 1) != 0) Fail("the old ask's PAGE_END was taken as current");
    if (coopfeed::FeedBeginArrived(t, 9) != 1) Fail("the BEGIN of the ask after the OFF was taken as stale");
    if (coopfeed::FeedOffRetryDecide(-1, 3, 1) != coopfeed::kOffNone) Fail("an OFF was retried with none owed");
    {   /* a link drop takes the subscription: no OFF owed for it at the next load, and the next welcomed link asks a first page */
        coopfeed::FeedAskState d;
        coopfeed::FeedAskTaken(&d, coopfeed::kAskFirst, 4, 7);
        coopfeed::FeedLinkDropped(&d);
        if (coopfeed::FeedOffTaken(&d) != 0) Fail("an OFF was owed for a subscription the link drop erased");
        if (coopfeed::FeedAskDecide(d, 1, 0, 4, 8, 0) != coopfeed::kAskFirst) Fail("the next welcomed link did not ask a first page after a drop");
        if (coopfeed::FeedOffRetryDecide(7, 7, 0) != coopfeed::kOffNone) Fail("an owed OFF was sent before the link was welcomed");
    }
    if (coopfeed::FeedOffRetryDecide(3, 3, 1) != coopfeed::kOffRetry) Fail("an owed OFF was not retried on its link");
    if (coopfeed::FeedOffRetryDecide(3, 3, 0) != coopfeed::kOffNone) Fail("an owed OFF was sent on a link that is down");
    if (coopfeed::FeedOffRetryDecide(3, 4, 1) != coopfeed::kOffDrop) Fail("an OFF owed on an old link was not dropped");
}
/* T-336: two launches start their request serials apart; a relaunch never lands in the previous run's first serials */
void t_t336_launches_start_apart()
{
    const unsigned int a = coopreq::SerialBase(1000u, 4242u, 123456789u, 0u);
    const unsigned int b = coopreq::SerialBase(95000u, 5120u, 987654321u, 0u);
    const unsigned int c = coopreq::SerialBase(1016u, 4242u, 123456789u, 0u);   /* same process id, a tick later */
    if (a == 0 || b == 0 || c == 0 || coopreq::SerialBase(0u, 0u, 0u, 0u) == 0) Fail("a base was 0");
    if (a == 1u || b == 1u) Fail("a launch started at 1");
    if (a == b || a == c || b == c) Fail("two launches started at the same serial");
    const unsigned int kWin = 1024u;   /* more serials than the holder keeps (kGrServedCap 512) */
    const int kLaunches = 256;
    std::vector<unsigned int> bases;
    for (int i = 0; i < kLaunches; ++i)
        bases.push_back(coopreq::SerialBase(5000u + 16u * (unsigned int)i, 1000u + 4u * (unsigned int)(i % 7), 7919u * (unsigned int)i + 17u, (unsigned int)(i / 97)));
    for (int i = 0; i < kLaunches; ++i)
        for (int k = 0; k < kLaunches; ++k)
            if (i != k && bases[k] - bases[i] < kWin) { Fail("a launch's first serials fall in another launch's window"); return; }
}
/* T-336: the holder's (peer, serial) test, modelled as items.cpp keeps it - a real repeat (every PUT of one drop carries its first
   serial) is still recognised; a new drop of the same launch, and a relaunched game's first drop, are not */
struct T336Served { unsigned int peer, serial; };
int T336Has(const std::vector<T336Served>& v, unsigned int peer, unsigned int serial)
{
    for (size_t i = 0; i < v.size(); ++i) if (v[i].peer == peer && v[i].serial == serial) return 1;
    return 0;
}
void t_t336_repeat_same_launch_still_recognised()
{
    const unsigned int peer = 7u;
    std::vector<T336Served> served;
    unsigned int next = coopreq::SerialBase(273655u, 4242u, 55555u, 1u);   /* launch 1 */
    unsigned int dropSerial = 0;
    if (dropSerial == 0) dropSerial = coopreq::SerialTake(&next);   /* the first PUT of the drop */
    T336Served s; s.peer = peer; s.serial = dropSerial; served.push_back(s);   /* the holder built it */
    if (dropSerial == 0) dropSerial = coopreq::SerialTake(&next);   /* a re-send of the SAME drop keeps its serial */
    if (!T336Has(served, peer, dropSerial)) Fail("a real repeat of one drop was not recognised");
    if (T336Has(served, peer, coopreq::SerialTake(&next))) Fail("a new drop of the same launch was taken for a repeat");
    unsigned int next2 = coopreq::SerialBase(584514u, 5120u, 99991u, 1u);   /* the game restarted: same peer, new launch */
    for (int i = 0; i < 3; ++i)
        if (T336Has(served, peer, coopreq::SerialTake(&next2))) Fail("a relaunched game's new drop was taken for a repeat (T711)");
    unsigned int old1 = 1u, old2 = 1u;   /* the old start at 1: the shape T711 hit */
    if (coopreq::SerialTake(&old1) != coopreq::SerialTake(&old2)) Fail("the model does not reproduce T711's reuse");
}
/* T-336: the counter hands out every value but 0, across the wrap */
/* T-172 tests: the pure parts of the copy's cell-drift repair (src/common/celldrift.h). */
void t_t172_footprints_overlap()
{
    if (coopdrift::FootprintsOverlap(0, 0, 2, 2, 1, 1, 2, 2) != 1) Fail("two 2x2 footprints sharing cell 1,1 do not overlap");
    if (coopdrift::FootprintsOverlap(0, 0, 2, 2, 2, 0, 1, 1) != 0) Fail("a footprint just right of another overlaps it");
    if (coopdrift::FootprintsOverlap(0, 0, 2, 2, 0, 2, 1, 1) != 0) Fail("a footprint just below another overlaps it");
    if (coopdrift::FootprintsOverlap(3, 4, 1, 2, 3, 5, 4, 2) != 1) Fail("a 1x2 under a 4x2's top-left corner does not overlap it");
    if (coopdrift::FootprintsOverlap(0, 0, 0, 2, 0, 0, 2, 2) != 0) Fail("a zero-width footprint overlaps something");
    if (coopdrift::FootprintsOverlap(5, 5, 1, 1, 5, 5, 1, 1) != 1) Fail("the same cell does not overlap itself");
}
void t_t172_row_visibility()
{
    int a = 0, b = 0, c = 0;
    void* g[12];   /* 4 x 3 */
    for (int i = 0; i < 12; ++i) g[i] = 0;
    g[0] = &a; g[1] = &a; g[4] = &a; g[5] = &a;   /* a: 2x2 at 0,0, fully shown */
    g[2] = &c;                                    /* c: placed at 2,0 1x1 over b (a 2x1 at 2,0) - b still shows at 3,0 */
    g[3] = &b;
    const void* const* cg = (const void* const*)g;
    if (coopdrift::RowVisibility(cg, 4, 3, &a, 0, 0, 2, 2) != coopdrift::kRowVisible) Fail("a fully shown record is not visible");
    if (coopdrift::RowVisibility(cg, 4, 3, &b, 2, 0, 2, 1) != coopdrift::kRowVisible) Fail("a partly shown record is not visible");
    g[3] = &c;   /* now c covers all of b */
    if (coopdrift::RowVisibility(cg, 4, 3, &b, 2, 0, 2, 1) != coopdrift::kRowLeftover) Fail("a record no cell answers is not a leftover");
    int d = 0;
    if (coopdrift::RowVisibility(cg, 4, 3, &d, 1, 2, 1, 1) != coopdrift::kRowLeftover) Fail("a record over empty cells is not a leftover");
    if (coopdrift::RowVisibility(cg, 4, 3, &d, 4, 0, 1, 1) != coopdrift::kRowUnjudged) Fail("a record wholly off the grid was judged");
    if (coopdrift::RowVisibility(cg, 4, 3, &d, 0, 0, 0, 1) != coopdrift::kRowUnjudged) Fail("a record with no width was judged");
    if (coopdrift::RowVisibility(cg, 4, 3, &d, -1, 0, 1, 1) != coopdrift::kRowUnjudged) Fail("a negative cell was judged");
    if (coopdrift::RowVisibility(cg, 4, 3, 0, 0, 0, 1, 1) != coopdrift::kRowUnjudged) Fail("no record was judged");
    if (coopdrift::RowVisibility(cg, 0, 3, &a, 0, 0, 1, 1) != coopdrift::kRowUnjudged) Fail("a grid with no width was judged");
    if (coopdrift::RowVisibility(cg, 4097, 1, &a, 0, 0, 1, 1) != coopdrift::kRowUnjudged) Fail("a grid over the cell cap was judged");
    if (coopdrift::RowVisibility(cg, 4, 3, &a, 1, 1, 3, 3) != coopdrift::kRowVisible) Fail("a footprint running off the grid lost its in-grid cell");
}
void t_t172_restore_blanked()
{
    int a = 0, v = 0, w = 0;
    /* v (2x1 at 0,0) was put over the leftover a (2x2 at 0,0); w sits at 3,1. The engine's removal of a zeroes a's whole footprint. */
    void* before[8] = { &v, &v, 0, 0, &a, &a, 0, &w };   /* 4 x 2 */
    void* grid[8]   = { 0,  0,  0, 0, 0,  0,  0, &w };
    const void* live[2] = { &v, &w };   /* the section's records after the removal: a is gone */
    const int back = coopdrift::RestoreBlanked(grid, (const void* const*)before, 8, &a, live, 2);
    if (back != 2) Fail("the two cells blanked under v were not the two given back");
    if (grid[0] != &v || grid[1] != &v) Fail("v's cells were not given back");
    if (grid[4] != 0 || grid[5] != 0) Fail("the removed record's own cells came back");
    if (grid[7] != &w) Fail("a cell the removal did not touch changed");
    void* grid2[8] = { &w, 0, 0, 0, 0, 0, 0, &w };   /* a cell that now holds another item is never overwritten */
    if (coopdrift::RestoreBlanked(grid2, (const void* const*)before, 8, &a, live, 2) != 1 || grid2[0] != &w || grid2[1] != &v) Fail("a refilled cell was overwritten");
    if (coopdrift::RestoreBlanked(grid, (const void* const*)before, 8, &a, live, 2) != 0) Fail("a second restore gave back cells again");
    if (coopdrift::RestoreBlanked(0, (const void* const*)before, 8, &a, live, 2) != 0 || coopdrift::RestoreBlanked(grid, (const void* const*)before, 4097, &a, live, 2) != 0)
        Fail("a missing grid or an over-cap count was walked");
    /* review LOW 3: an item the section no longer lists is never written back into the grid */
    void* grid3[8] = { 0, 0, 0, 0, 0, 0, 0, &w };
    const void* liveNoV[1] = { &w };
    if (coopdrift::RestoreBlanked(grid3, (const void* const*)before, 8, &a, liveNoV, 1) != 0 || grid3[0] != 0 || grid3[1] != 0)
        Fail("a cell was given back to an item no longer listed");
    if (coopdrift::RestoreBlanked(grid3, (const void* const*)before, 8, &a, 0, 2) != 0 || coopdrift::RestoreBlanked(grid3, (const void* const*)before, 8, &a, live, 0) != 0
        || coopdrift::RestoreBlanked(grid3, (const void* const*)before, 8, &a, live, coopdrift::kLiveCap + 1) != 0)
        Fail("a missing, empty or over-cap live list gave cells back");
}
/* review LOW 2: after the removal, every cell still answering the removed record is emptied before its destroy */
void t_t172_null_stale()
{
    int a = 0, b = 0;
    void* g[4] = { &a, &b, &a, 0 };
    if (coopdrift::NullStale(g, 4, &a) != 2 || g[0] != 0 || g[2] != 0) Fail("the cells answering the removed record were not emptied");
    if (g[1] != &b) Fail("another item's cell was emptied");
    if (coopdrift::NullStale(g, 4, &a) != 0) Fail("a second pass emptied cells again");
    if (coopdrift::NullStale(0, 4, &a) != 0 || coopdrift::NullStale(g, 4, 0) != 0 || coopdrift::NullStale(g, 4097, &b) != 0 || g[1] != &b)
        Fail("a missing grid, no record or an over-cap count was walked");
}
/* review MED 1 / LOW 5: the owner skip, the repeating refusals, the live-list test */
void t_t172_review_decisions()
{
    if (coopdrift::SkipForOwner(5u, 1) != 1) Fail("an add on a character this game owns was not skipped");
    if (coopdrift::SkipForOwner(5u, 0) != 0) Fail("an add on another game's character was skipped");
    if (coopdrift::SkipForOwner(0u, 1) != 0) Fail("a box add (uid 0) was skipped");
    if (coopdrift::RefusalRepeats(coopdrift::kWhyGrid) != 1 || coopdrift::RefusalRepeats(coopdrift::kWhySection) != 1)
        Fail("a grid or section refusal was not called repeating");
    if (coopdrift::RefusalRepeats(coopdrift::kWhyNone) != 0 || coopdrift::RefusalRepeats(coopdrift::kWhyListed) != 0
        || coopdrift::RefusalRepeats(coopdrift::kWhyFault) != 0 || coopdrift::RefusalRepeats(coopdrift::kWhyTooMany) != 0)
        Fail("a one-off refusal was called repeating");
    int a = 0, b = 0, c = 0;
    const void* l[2] = { &a, &b };
    if (coopdrift::InList(l, 2, &b) != 1 || coopdrift::InList(l, 2, &c) != 0 || coopdrift::InList(l, 0, &a) != 0
        || coopdrift::InList(0, 2, &a) != 0 || coopdrift::InList(l, 2, 0) != 0)
        Fail("the live-list test answered wrong");
}
void t_t172_owner_add_over_stale()
{
    /* The run's shape: the copy holds stale s (1x2 at 2,0) where the owner's n (4x2) lands at 2,0; a leftover l sits under visible e. */
    int e = 0, l = 0, s = 0;
    void* g[18];   /* 6 x 3 */
    for (int i = 0; i < 18; ++i) g[i] = 0;
    g[0] = &e; g[6] = &e;   /* e: 1x2 at 0,0 */
    g[2] = &s; g[8] = &s;   /* s: 1x2 at 2,0 */
    const void* const* cg = (const void* const*)g;
    if (coopdrift::FootprintsOverlap(2, 0, 4, 2, 2, 0, 1, 2) != 1) Fail("the stale record is not in the owner's footprint");
    if (coopdrift::FootprintsOverlap(2, 0, 4, 2, 0, 0, 1, 2) != 0) Fail("the visible neighbour is in the owner's footprint");
    if (coopdrift::RowVisibility(cg, 6, 3, &l, 0, 0, 1, 2) != coopdrift::kRowLeftover) Fail("the record under e is not a leftover");
    if (coopdrift::RowVisibility(cg, 6, 3, &e, 0, 0, 1, 2) != coopdrift::kRowVisible) Fail("e is not visible");
    if (coopdrift::RowVisibility(cg, 6, 3, &s, 2, 0, 1, 2) != coopdrift::kRowVisible) Fail("the stale visible record was called a leftover");
}
void t_t336_serial_take_skips_zero()
{
    unsigned int n = 0xFFFFFFFEu;
    if (coopreq::SerialTake(&n) != 0xFFFFFFFEu) Fail("the current value was not handed out");
    if (coopreq::SerialTake(&n) != 0xFFFFFFFFu) Fail("the last value before the wrap was not handed out");
    if (n != 1u) Fail("the counter rested on 0 after the wrap");
    if (coopreq::SerialTake(&n) != 1u || n != 2u) Fail("the wrap did not continue at 1");
    unsigned int z = 0u;
    if (coopreq::SerialTake(&z) != 1u || z != 2u) Fail("a counter at 0 handed out 0");
}
void t_t313f1_page_caps()
{
    if (coopfeed::kFeedLowWaterEntries + 2 * coopfeed::kFeedPageRecords + 1 >= coopfeed::kFeedEntryBound * 3 / 4) Fail("a full page at the entry low-water mark can reach the 3/4 pressure line");
    std::map<std::string, std::string> src; char key[16];
    for (int i = 0; i < 5000; ++i) { std::sprintf(key, "k%05d", i); src[key] = "v"; }
    std::map<std::string, std::string> got; T313Sink sink; sink.dst = &got;
    std::string lk; unsigned int sent = 0;
    if (coopfeed::FeedPageWalk(src, "", (size_t)1 << 30, sink, &lk, &sent) || sent != (unsigned)coopfeed::kFeedPageRecords) Fail("a page of small records was not cut at the record cap");
    if (coopfeed::FeedPageWalk(src, "", (size_t)1 << 30, 10, sink, &lk, &sent) || sent != 10) Fail("an explicit record cap was not kept");
    coopfeed::FeedAskState s; coopfeed::FeedAskTaken(&s, coopfeed::kAskFirst, 1, 1);
    coopfeed::FeedPageEndArrived(&s, 1, "k", 0);
    if (coopfeed::FeedAskDecide(s, 1, 0, 1, 1, 0, coopfeed::kFeedLowWaterEntries) != coopfeed::kAskNone) Fail("the next page was asked at the entry low-water mark");
    if (coopfeed::FeedAskDecide(s, 1, 0, 1, 1, 0, coopfeed::kFeedLowWaterEntries - 1) != coopfeed::kAskNext) Fail("the next page was not asked below the entry low-water mark");
    std::vector<char> b; coopfeed::FeedMsg m;   /* review L2: the world server's fallback answer always encodes */
    if (!coopfeed::FeedEncodePageEnd(&b, std::string(), 0, true, 5ULL) || !coopfeed::FeedDecode(&b[0], b.size(), &m) || m.last != 1 || m.sent != 0) Fail("the empty LAST page did not round-trip");
}
/* ---- T-327 (protocol 111): MSG_EFFECT - eating another game's character is a request to its owner ---------------------------- */
cooffect::EffectMsg T327Req()
{
    cooffect::EffectMsg m;
    m.dir = cooffect::kDirRequest; m.kind = cooffect::kKindEat; m.reqId = 7u; m.targetUid = 0x00400001u; m.actorUid = 0x00100002u;
    m.rate = 1.0f; m.budget = 0.25f; m.flags = cooffect::kEatFlagTargetDead;
    return m;
}
void t_t327_effect_wire()
{
    const cooffect::EffectMsg m = T327Req();
    std::vector<char> b(2, 'x');
    if (!cooffect::EncodeEffect(&b, m) || b.size() != 2 + cooffect::kEatRequestSize) { Fail("an EAT request did not encode to 23 bytes"); return; }
    if (b[0] != 'x' || b[2] != 1 || b[3] != 1) Fail("an EAT request's bytes are not dir, kind first");
    cooffect::EffectMsg d;
    if (cooffect::DecodeEffect(&b[2], b.size() - 2, &d) != cooffect::kDecodeOk || d.dir != m.dir || d.kind != m.kind || d.reqId != m.reqId
        || d.targetUid != m.targetUid || d.actorUid != m.actorUid || d.rate != m.rate || d.budget != m.budget || d.flags != m.flags)
        Fail("an EAT request did not round-trip");
    if (cooffect::DecodeEffect(&b[2], cooffect::kEatRequestSize - 1, &d) != cooffect::kDecodeTooShort) Fail("a 22-byte request was accepted");
    cooffect::EffectMsg a = m;
    a.dir = cooffect::kDirAnswer; a.result = cooffect::kResultRefused; a.reason = cooffect::kReasonFar;
    std::vector<char> e;
    if (!cooffect::EncodeEffect(&e, a) || e.size() != cooffect::kAnswerSize) { Fail("a REFUSED answer did not encode to 16 bytes"); return; }
    if (cooffect::DecodeEffect(&e[0], e.size(), &d) != cooffect::kDecodeOk || d.result != a.result || d.reason != a.reason || d.reqId != a.reqId)
        Fail("an answer did not round-trip");
    float nanv = 0.0f; { const unsigned int qn = 0x7FC00000u; std::memcpy(&nanv, &qn, 4); }
    const float badRates[4] = { 0.0f, -1.0f, nanv, cooffect::kEatRateMax * 2.0f };
    for (int i = 0; i < 4; ++i) { cooffect::EffectMsg x = m; x.rate = badRates[i]; std::vector<char> z; if (cooffect::EncodeEffect(&z, x) || !z.empty()) Fail("a bad rate was encoded"); }
    { cooffect::EffectMsg x = m; x.budget = -0.5f; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("a negative budget was encoded"); }
    { cooffect::EffectMsg x = m; x.targetUid = x.actorUid; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("an eater eating itself was encoded"); }
    { cooffect::EffectMsg x = m; x.reqId = 0; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("request id 0 was encoded"); }
    { cooffect::EffectMsg x = m; x.kind = 2; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("a reserved kind was encoded"); }
    { cooffect::EffectMsg x = a; x.reason = cooffect::kReasonNone; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("a REFUSED with no reason was encoded"); }
    { cooffect::EffectMsg x = a; x.result = cooffect::kResultApplied; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("an APPLIED with a reason was encoded"); }
    std::vector<char> x = std::vector<char>(b.begin() + 2, b.end());
    std::vector<char> y = x; y[0] = 3;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadDir) Fail("direction 3 was accepted");
    y = x; y[1] = 5;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadKind) Fail("kind 5 (BED, reserved) was accepted");
    y = x; std::memset(&y[6], 0, 4);
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadUid) Fail("target 0 was accepted");
    y = x; y[22] = 2;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadField) Fail("flag bit 1 was accepted");
    y = x; std::memcpy(&y[14], &nanv, 4);
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadField) Fail("a NaN rate was accepted");
    std::vector<char> w = e; w[15] = (char)cooffect::kReasonCount;
    if (cooffect::DecodeEffect(&w[0], w.size(), &d) != cooffect::kDecodeBadField) Fail("an unknown reason was accepted");
    if (cooffect::DecodeEffect(&e[0], cooffect::kAnswerSize - 1, &d) != cooffect::kDecodeTooShort) Fail("a 15-byte answer was accepted");
}
void t_t327_detour_cases()
{
    /* own: 1 driven here, 0 a copy, -1 no uid */
    if (cooffect::EatDetourCase(1, 1) != cooffect::kCaseOriginal || cooffect::EatDetourCase(1, 0) != cooffect::kCaseOriginal
        || cooffect::EatDetourCase(1, -1) != cooffect::kCaseOriginal || cooffect::EatDetourCase(-1, 1) != cooffect::kCaseOriginal)
        Fail("a victim driven here (or no game's character) did not run the engine's own call");
    if (cooffect::EatDetourCase(0, 1) != cooffect::kCaseRequest) Fail("a copy eaten by our eater is not a request");
    if (cooffect::EatDetourCase(0, 0) != cooffect::kCaseSuppress) Fail("an onlooker's copy eating a copy is not suppressed");
    if (cooffect::EatDetourCase(0, -1) != cooffect::kCaseNoActor) Fail("a copy eaten by an unnamed eater does not stop");
}
void t_t327_owner_validation()
{
    const float nearSq = 100.0f * 100.0f, farSq = 151.0f * 151.0f;
    if (cooffect::EffectValidate(true, true, true, true, false, nearSq) != cooffect::kReasonNone) Fail("a good request was refused");
    if (cooffect::EffectValidate(true, true, true, true, false, cooffect::kFarUnits * cooffect::kFarUnits) != cooffect::kReasonNone) Fail("15 m exactly was refused");
    if (cooffect::EffectValidate(false, false, false, false, true, farSq) != cooffect::kReasonNotMine) Fail("NOT_MINE is not checked first");
    if (cooffect::EffectValidate(true, false, false, false, true, farSq) != cooffect::kReasonNotSenders) Fail("NOT_SENDERS is not second");
    if (cooffect::EffectValidate(true, true, false, false, true, farSq) != cooffect::kReasonGone) Fail("GONE is not third");
    if (cooffect::EffectValidate(true, true, true, false, true, farSq) != cooffect::kReasonNoActor) Fail("NO_ACTOR is not fourth");
    if (cooffect::EffectValidate(true, true, true, true, true, nearSq) != cooffect::kReasonBusy) Fail("a load or save does not refuse BUSY");
    if (cooffect::EffectValidate(true, true, true, true, false, farSq) != cooffect::kReasonFar) Fail("16 m was not refused FAR");
    if (cooffect::EffectValidate(true, true, true, true, false, -1.0f) != cooffect::kReasonFar) Fail("an unreadable position was not refused");
    if (cooffect::ReasonAnswered(cooffect::kReasonNotSenders) || !cooffect::ReasonAnswered(cooffect::kReasonFar)) Fail("the answered reasons are wrong");
    /* T-327 fold 2: no standing-victim refusal (the rule no longer knows the victim's posture, so a standing victim near is NOT refused;
       the eater's task decides when eating stops). STATE stays answered, for the owner-side eating OFF after a caught fault. */
    if (cooffect::EffectValidate(true, true, true, true, false, nearSq) == cooffect::kReasonState) Fail("a standing victim was refused STATE");
    if (!cooffect::ReasonAnswered(cooffect::kReasonState)) Fail("STATE is not answered");
    /* review fold L1: NOT_SENDERS (not answered) leaves the owner's pair; an answered refusal ends it; no refusal ends nothing */
    if (cooffect::RefusalEndsOwnerPair(cooffect::kReasonNotSenders)) Fail("a NOT_SENDERS refusal ended the owner's pair");
    if (!cooffect::RefusalEndsOwnerPair(cooffect::kReasonFar) || !cooffect::RefusalEndsOwnerPair(cooffect::kReasonState)
        || !cooffect::RefusalEndsOwnerPair(cooffect::kReasonBusy)) Fail("an answered refusal did not end the owner's pair");
    if (cooffect::RefusalEndsOwnerPair(cooffect::kReasonNone)) Fail("an accepted request ended the owner's pair");
}
void t_t327_accumulate_flush()
{
    /* 60 frames of 1/60 s at rate 1.0 is a budget of 1.0, which the owner spends back in 60 frames of the same length */
    long long micro = 0;
    for (int i = 0; i < 60; ++i) micro += cooffect::EatBiteMicro(1.0f, 1.0f / 60.0f);
    const float budget = cooffect::EatMicroToBudget(micro);
    if (budget < 0.9999f || budget > 1.0001f) Fail("60 bites of 1/60 s did not make a budget of 1.0");
    if (cooffect::EatBiteMicro(1.0f, 2.0f) != 0 || cooffect::EatBiteMicro(-1.0f, 0.016f) != 0) Fail("a garbage bite was counted");
    if (cooffect::EatMicroToBudget(-5) != 0.0f || cooffect::EatMicroToBudget(cooffect::kMicro * 5000) != cooffect::kEatBudgetMax) Fail("the budget is not clamped");
    float left = budget, r = 0.0f; int frames = 0;
    while (left > 0.0f && frames < 1000)
    {
        const float spend = cooffect::EatSpendStep(left, 1.0f, 1.0f / 30.0f, &r);   /* the owner runs at half the frame rate */
        if (!(spend > 0.0f)) break;
        left -= spend; ++frames;
        if (left < 1e-6f) left = 0.0f;
    }
    if (frames < 30 || frames > 31 || left != 0.0f) Fail("the owner did not spend the budget at the eater's pace (30 frames of 1/30 s)");
    const float s2 = cooffect::EatSpendStep(0.01f, 1.0f, 1.0f / 30.0f, &r);
    if (s2 != 0.01f || r < 0.2999f || r > 0.3001f) Fail("the last partial step does not spend exactly what is left");
    if (cooffect::EatSpendStep(0.0f, 1.0f, 0.016f, &r) != 0.0f || r != 0.0f || cooffect::EatSpendStep(1.0f, 1.0f, 0.0f, &r) != 0.0f)
        Fail("an empty budget or a zero frame spent something");
}
void t_t327_stop_rules()
{
    const unsigned int t = 100000u;
    /* a fresh pair with budget sends at once; then waits 250 ms between sends */
    if (cooffect::EatActorDecide(false, t, t, 0, 0, true, 0) != cooffect::kActorSend) Fail("a fresh pair did not send at once");
    if (cooffect::EatActorDecide(false, t + 100, t + 100, t, t, true, 0) != cooffect::kActorWait) Fail("a pair sent again inside 250 ms");
    if (cooffect::EatActorDecide(false, t + 250, t + 250, t, t, true, 0) != cooffect::kActorSend) Fail("a pair did not send after 250 ms");
    /* 3 s with no answer stops it (manager decision 3), even while it still bites */
    if (cooffect::EatActorDecide(false, t + 3001, t + 3001, t + 2750, t, true, 0) != cooffect::kActorStopTimeout) Fail("3 s of silence did not stop the eater");
    if (cooffect::EatActorDecide(false, t + 3000, t + 3000, t + 2750, t, true, 0) == cooffect::kActorStopTimeout) Fail("the eater stopped before 3 s");
    /* the eating stopped by itself: closed after 1 s once everything sent was answered */
    if (cooffect::EatActorDecide(false, t + 1001, t, t, 0, false, 0) != cooffect::kActorClose) Fail("an idle, answered pair was not closed");
    if (cooffect::EatActorDecide(false, t + 1001, t, t, t, false, 0) != cooffect::kActorWait) Fail("an idle pair with an unanswered send was closed early");
    /* a bite the detour stamped after `now` was read keeps the pair open */
    if (cooffect::EatActorDecide(false, t, t + 5, t, 0, false, 0) == cooffect::kActorClose) Fail("a bite stamped after now closed the pair");
    /* a stopped pair is held (the eater's call answers 1) for 10 s from the STOP */
    if (cooffect::EatActorDecide(true, t + 5000, t, t, 0, true, t) != cooffect::kActorHold) Fail("a stopped pair was not held");
    if (cooffect::EatActorDecide(true, t + 10001, t, t, 0, false, t) != cooffect::kActorFree) Fail("a stopped pair was never freed");
    /* review fold M1: bites still arriving on a stopped pair (lastBite 1 ms ago) do not extend the hold - it frees at 10 s */
    if (cooffect::EatActorDecide(true, t + 10001, t + 10000, t, 0, false, t) != cooffect::kActorFree) Fail("a biting stopped pair was not freed 10 s after the stop");
    if (cooffect::EatActorDecide(true, t + 9999, t + 9998, t, 0, false, t) != cooffect::kActorHold) Fail("a stopped pair was freed before 10 s");
    const unsigned int ws = 0xFFFFF000u;   /* the stop 4096 ms before the tick counter wraps */
    if (cooffect::EatActorDecide(true, ws + 10001u, ws + 10000u, ws, 0, false, ws) != cooffect::kActorFree) Fail("a wrapped stop clock never freed");
    if (cooffect::EatActorDecide(true, ws + 9000u, ws + 8999u, ws, 0, false, ws) != cooffect::kActorHold) Fail("a wrapped stop clock freed early");
    /* answers: APPLIED / ALREADY keep eating, DONE / REFUSED stop */
    if (cooffect::EatActorOnAnswer(cooffect::kResultApplied) != 0 || cooffect::EatActorOnAnswer(cooffect::kResultAlready) != 0
        || cooffect::EatActorOnAnswer(cooffect::kResultDone) != 1 || cooffect::EatActorOnAnswer(cooffect::kResultRefused) != 1)
        Fail("the answers' stop rule is wrong");
    /* the tick counter wraps */
    const unsigned int w = 0xFFFFFF00u;
    if (cooffect::EatActorDecide(false, w + 0x200u, w + 0x200u, w, w, true, 0) != cooffect::kActorSend) Fail("a wrapped clock broke the send cadence");
}
void t_t327_routing()
{
    const unsigned int slot3 = cooplive::RelayPeerId(3), slot5 = cooplive::RelayPeerId(5), raw = 0u;
    /* the session peer is slot 3 */
    if (cooffect::EffectRequestRoad(true, true, slot5, 3, true) != cooffect::kRoadLive) Fail("a third game's victim did not go LIVE SLOT");
    if (cooffect::EffectRequestRoad(false, true, slot5, 3, true) != cooffect::kRoadNone) Fail("a third game's victim went on the session link");
    if (cooffect::EffectRequestRoad(true, true, slot3, 3, true) != cooffect::kRoadLive) Fail("the session peer, proven on the notebook, did not go LIVE");
    if (cooffect::EffectRequestRoad(true, true, slot3, 3, false) != cooffect::kRoadSession) Fail("the session peer, not proven on the notebook, did not use the session link");
    if (cooffect::EffectRequestRoad(false, true, slot3, 3, true) != cooffect::kRoadSession) Fail("with the notebook down the session peer was not reached");
    if (cooffect::EffectRequestRoad(true, false, slot3, 3, false) != cooffect::kRoadLive) Fail("with the session link down the notebook was not used");
    if (cooffect::EffectRequestRoad(true, true, raw, -1, true) != cooffect::kRoadSession) Fail("a raw session owner (no slot yet) was not reached on the session link");
    if (cooffect::EffectRequestRoad(false, false, slot3, 3, true) != cooffect::kRoadNone) Fail("no road was not none");
    if (cooffect::EffectAnswerRoad(true, true, true) != cooffect::kRoadLive || cooffect::EffectAnswerRoad(false, true, true) != cooffect::kRoadSession
        || cooffect::EffectAnswerRoad(true, false, true) != cooffect::kRoadNone || cooffect::EffectAnswerRoad(false, true, false) != cooffect::kRoadNone)
        Fail("an answer did not go back on the road its request came by (and only there)");
    if (!cooplive::LiveInnerAccepted(cooplive::kInnerEffect) || cooplive::kInnerEffect != 65u) Fail("EFFECT is not taken from the notebook");
}
/* ---- MSG_EFFECT kind 3 KO - a sneak knockout (Task_StealthKO) of another game's character ---------------------------------- */
static cooffect::EffectMsg T329Req(unsigned char decided)
{
    cooffect::EffectMsg m;
    m.dir = cooffect::kDirRequest; m.kind = cooffect::kKindKo; m.reqId = 9u; m.targetUid = 0x00400001u; m.actorUid = 0x00100002u;
    m.source = cooffect::kKoSourceStealth; m.decided = decided; m.skill = 37.5f;
    return m;
}
static cooffect::EffectMsg T329Ans(unsigned char result, unsigned char reason, unsigned char outcome)
{
    cooffect::EffectMsg a = T329Req(0);
    a.dir = cooffect::kDirAnswer; a.source = 0; a.decided = 0; a.result = result; a.reason = reason; a.outcome = outcome;
    return a;
}
void t_t329_ko_wire()
{
    const unsigned char dcds[2] = { cooffect::kKoDecidedOwner, cooffect::kKoDecidedNpc };   /* a player character, an NPC */
    for (int n = 0; n < 2; ++n)
    {
        const unsigned char dcd = dcds[n];
        const cooffect::EffectMsg rq = T329Req(dcd);
        std::vector<char> rqb;
        if (!cooffect::EncodeEffect(&rqb, rq) || rqb.size() != cooffect::kKoRequestSize || rqb.size() != 20u) { Fail("a KO request did not encode to 20 bytes"); return; }
        float sk = 0.0f;
        std::memcpy(&sk, &rqb[16], 4);
        if (rqb[0] != 1 || rqb[1] != 3 || rqb[14] != 1 || rqb[15] != (char)dcd || sk != 37.5f)
            Fail("a KO request's bytes are not dir 1, kind 3, source, decided, skill");
        cooffect::EffectMsg rqd;
        if (cooffect::DecodeEffect(&rqb[0], rqb.size(), &rqd) != cooffect::kDecodeOk || rqd.dir != rq.dir || rqd.kind != cooffect::kKindKo || rqd.reqId != rq.reqId
            || rqd.targetUid != rq.targetUid || rqd.actorUid != rq.actorUid || rqd.source != cooffect::kKoSourceStealth || rqd.decided != dcd || rqd.skill != 37.5f)
            Fail("a KO request (decided 0 / 4) did not round-trip");
        if (cooffect::DecodeEffect(&rqb[0], cooffect::kKoRequestSize - 1, &rqd) != cooffect::kDecodeTooShort) Fail("a 19-byte KO request was accepted");
    }
    /* the KO's kind byte (3) is checked in the bytes above; an older game is kept off the link by the protocol number, not by this decoder */
    /* the assassin's skill: an unread skill (-1) travels; below -1 or above the cap does not */
    {
        cooffect::EffectMsg x = T329Req(cooffect::kKoDecidedNpc);
        x.skill = cooffect::kKoSkillUnread;
        std::vector<char> z;
        if (!cooffect::EncodeEffect(&z, x)) Fail("an unread skill (-1) was not encoded");
        x.skill = -1.5f;
        std::vector<char> z2;
        if (cooffect::EncodeEffect(&z2, x)) Fail("a skill below -1 was encoded");
        x.skill = cooffect::kKoSkillMax + 1.0f;
        std::vector<char> z3;
        if (cooffect::EncodeEffect(&z3, x)) Fail("a skill above the cap was encoded");
        std::vector<char> z4;
        cooffect::EncodeEffect(&z4, T329Req(cooffect::kKoDecidedNpc));
        const float big = 2000.0f;
        if (z4.size() == 20u)
        {
            std::memcpy(&z4[16], &big, 4);
            cooffect::EffectMsg zd;
            if (cooffect::DecodeEffect(&z4[0], z4.size(), &zd) != cooffect::kDecodeBadField) Fail("a skill above the cap was decoded");
        }
    }
    for (unsigned char oc = 1; oc <= 2; ++oc)
    {
        const cooffect::EffectMsg ap = T329Ans(cooffect::kResultApplied, cooffect::kReasonNone, oc);
        std::vector<char> apb;
        if (!cooffect::EncodeEffect(&apb, ap) || apb.size() != cooffect::kKoAnswerSize || apb.size() != 17u) { Fail("an APPLIED KO answer did not encode to 17 bytes"); return; }
        if (apb[0] != 2 || apb[1] != 3 || apb[14] != 1 || apb[15] != 0 || apb[16] != (char)oc) Fail("an APPLIED KO answer's bytes are wrong");
        cooffect::EffectMsg apd;
        if (cooffect::DecodeEffect(&apb[0], apb.size(), &apd) != cooffect::kDecodeOk || apd.result != ap.result || apd.outcome != oc || apd.reqId != ap.reqId)
            Fail("an APPLIED KO answer did not round-trip");
        if (cooffect::DecodeEffect(&apb[0], cooffect::kKoAnswerSize - 1, &apd) != cooffect::kDecodeTooShort) Fail("a 16-byte KO answer was accepted");
    }
    {
        std::vector<char> e;
        if (!cooffect::EncodeEffect(&e, T329Ans(cooffect::kResultAlready, cooffect::kReasonNone, 0)) || e.size() != 17u) Fail("an ALREADY KO answer did not encode");
        std::vector<char> f;
        if (!cooffect::EncodeEffect(&f, T329Ans(cooffect::kResultRefused, cooffect::kReasonFar, 0)) || f.size() != 17u) { Fail("a REFUSED KO answer did not encode"); return; }
        cooffect::EffectMsg fd;
        if (cooffect::DecodeEffect(&f[0], f.size(), &fd) != cooffect::kDecodeOk || fd.result != cooffect::kResultRefused || fd.reason != cooffect::kReasonFar || fd.outcome != 0)
            Fail("a REFUSED KO answer did not round-trip");
        std::vector<char> g = f; g[16] = 1;
        if (cooffect::DecodeEffect(&g[0], g.size(), &fd) != cooffect::kDecodeBadField) Fail("a REFUSED KO answer carrying an outcome was decoded");
        std::vector<char> h;
        cooffect::EncodeEffect(&h, T329Ans(cooffect::kResultApplied, cooffect::kReasonNone, 1));
        h[16] = 0;
        if (cooffect::DecodeEffect(&h[0], h.size(), &fd) != cooffect::kDecodeBadField) Fail("an APPLIED KO answer with no outcome was decoded");
        h[16] = 3;
        if (cooffect::DecodeEffect(&h[0], h.size(), &fd) != cooffect::kDecodeBadField) Fail("KO outcome 3 was decoded");
    }
    /* refusals: an outcome without APPLIED, APPLIED without an outcome, decided out of range, a bad source, reserved kinds */
    { std::vector<char> z; if (cooffect::EncodeEffect(&z, T329Ans(cooffect::kResultRefused, cooffect::kReasonFar, 1)) || !z.empty()) Fail("an outcome without APPLIED was encoded"); }
    { std::vector<char> z; if (cooffect::EncodeEffect(&z, T329Ans(cooffect::kResultAlready, cooffect::kReasonNone, 2))) Fail("an ALREADY with an outcome was encoded"); }
    { std::vector<char> z; if (cooffect::EncodeEffect(&z, T329Ans(cooffect::kResultApplied, cooffect::kReasonNone, 0))) Fail("an APPLIED KO answer without an outcome was encoded"); }
    { std::vector<char> z; if (cooffect::EncodeEffect(&z, T329Ans(cooffect::kResultApplied, cooffect::kReasonNone, 3))) Fail("KO outcome 3 was encoded"); }
    {   /* decided 1..3 (no longer used) and 5 are refused */
        const unsigned char bad[4] = { 1, 2, 3, 5 };
        for (int n = 0; n < 4; ++n) { std::vector<char> z; if (cooffect::EncodeEffect(&z, T329Req(bad[n]))) Fail("a KO request with an unused decided was encoded"); }
    }
    { cooffect::EffectMsg x = T329Req(0); x.source = 0; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("KO source 0 was encoded"); }
    { cooffect::EffectMsg x = T329Req(0); x.source = 2; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("KO source 2 was encoded"); }
    { cooffect::EffectMsg x = T329Req(0); x.kind = 2; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("kind 2 (HIT, reserved) was encoded"); }
    { cooffect::EffectMsg x = T329Req(0); x.kind = 4; std::vector<char> z; if (cooffect::EncodeEffect(&z, x)) Fail("kind 4 (NOTICE, reserved) was encoded"); }
    std::vector<char> b;
    cooffect::EncodeEffect(&b, T329Req(cooffect::kKoDecidedNpc));
    if (b.size() != 20u) { Fail("an NPC KO request did not encode"); return; }
    cooffect::EffectMsg d;
    std::vector<char> y = b;
    {
        const char bad[4] = { 1, 2, 3, 5 };
        for (int n = 0; n < 4; ++n)
        {
            y = b; y[15] = bad[n];
            if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadField) Fail("a KO request with an unused decided was decoded");
        }
    }
    y = b; y[14] = 0;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadField) Fail("KO source 0 was decoded");
    y = b; y[1] = 2;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadKind) Fail("kind 2 (reserved) was decoded");
    y = b; y[1] = 6;
    if (cooffect::DecodeEffect(&y[0], y.size(), &d) != cooffect::kDecodeBadKind) Fail("kind 6 (reserved) was decoded");
    if (cooffect::DecodeEffect(&b[0], cooffect::kHeaderSize, &d) != cooffect::kDecodeTooShort) Fail("a KO request cut to its header was decoded");
    /* EAT is unchanged: its answer is still 16 bytes */
    { cooffect::EffectMsg ea = T327Req(); ea.dir = cooffect::kDirAnswer; ea.result = cooffect::kResultApplied;
      std::vector<char> z; if (!cooffect::EncodeEffect(&z, ea) || z.size() != cooffect::kAnswerSize) Fail("an EAT answer is no longer 16 bytes"); }
}
void t_t329_ko_decisions()
{
    using namespace cooffect;
    /* NEW: classify; a player character and an NPC are both sent to the victim's own game; anything unreadable ends */
    if (KoActorDecide(kKoNew, kKoPcUnknown, false, true, kKoAnsNone, 0) != kKoActClassify) Fail("a NEW slot was not classified");
    if (KoActorDecide(kKoNew, 1, false, true, kKoAnsNone, 0) != kKoActSend) Fail("a player character victim was not sent to its owner");
    if (KoActorDecide(kKoNew, 0, false, true, kKoAnsNone, 0) != kKoActSend) Fail("an NPC victim was not sent to its owner");
    if (KoRequestDecided(0) != kKoDecidedNpc || KoRequestDecided(1) != kKoDecidedOwner) Fail("a request carried the wrong decided");
    if (KoActorDecide(kKoNew, -1, false, true, kKoAnsNone, 0) != kKoActEndTask) Fail("an unreadable victim did not end the task");
    if (KoActorDecide(kKoNew, -2, false, true, kKoAnsNone, 0) != kKoActEndTask) Fail("a victim with no copy did not end the task");
    if (KoActorDecide(kKoNew, kKoPcUnknown, true, true, kKoAnsNone, 0) != kKoActRunEngine) Fail("a NEW slot whose victim is driven here was not released");
    if (KoActorDecide(kKoNew, kKoPcUnknown, false, false, kKoAnsNone, 0) != kKoActEndTask) Fail("a NEW slot whose copy is gone did not end");
    /* HELD, an NPC and a player character alike: the verdict runs the task; a hand-over, a refusal, ALREADY, the copy gone or 3 s
       end it (a hand-over never rolls a second time) */
    for (int pc = 0; pc <= 1; ++pc)
    {
        if (KoActorDecide(kKoHeld, pc, false, true, kKoAnsVerdict, 100) != kKoActRunForced) Fail("a verdict did not run the task forced");
        if (KoActorDecide(kKoHeld, pc, true, true, kKoAnsVerdict, 100) != kKoActRunForced) Fail("a verdict that arrived lost to a hand-over");
        if (KoActorDecide(kKoHeld, pc, true, true, kKoAnsNone, 100) != kKoActEndTask) Fail("a hand-over while held did not end the task (a second roll)");
        if (KoActorDecide(kKoHeld, pc, true, true, kKoAnsRefused, 100) != kKoActEndTask || KoActorDecide(kKoHeld, pc, true, false, kKoAnsNone, 0) != kKoActEndTask)
            Fail("a hand-over while held with a refusal or no copy did not end the task");
        if (KoActorDecide(kKoHeld, pc, false, true, kKoAnsRefused, 100) != kKoActEndTask) Fail("a refusal did not end the task");
        if (KoActorDecide(kKoHeld, pc, false, true, kKoAnsAlready, 100) != kKoActEndTask) Fail("ALREADY did not end the task");
        if (KoActorDecide(kKoHeld, pc, false, false, kKoAnsNone, 100) != kKoActEndTask) Fail("a gone copy did not end a held task");
        if (KoActorDecide(kKoHeld, pc, false, true, kKoAnsNone, kKoAnswerMs + 1) != kKoActEndTask) Fail("3 s with no answer did not end the task");
        if (KoActorDecide(kKoHeld, pc, false, true, kKoAnsNone, kKoAnswerMs) != kKoActHold) Fail("a held task ended before 3 s");
    }
    /* the answers: APPLIED with an outcome is the verdict; ALREADY; REFUSED (and a DONE no owner sends) end the task */
    if (KoAnswerKind(kResultApplied, kKoOutcomeKnockedOut) != kKoAnsVerdict || KoAnswerKind(kResultApplied, kKoOutcomeNoticed) != kKoAnsVerdict
        || KoAnswerKind(kResultAlready, 0) != kKoAnsAlready || KoAnswerKind(kResultRefused, 0) != kKoAnsRefused || KoAnswerKind(kResultDone, 0) != kKoAnsRefused)
        Fail("an answer was recorded as the wrong kind");
    if (KoActorDecide(kKoForced, 0, false, true, kKoAnsVerdict, 0) != kKoActHold || KoActorDecide(kKoForcedDone, 1, false, true, kKoAnsVerdict, 0) != kKoActHold
        || KoActorDecide(kKoEnd, 0, false, true, kKoAnsNone, 0) != kKoActHold)
        Fail("the main thread acted on a slot the hook runs");
    /* the hook: only FORCED runs the engine's update; END ends the task; everything else waits in stage 1 */
    if (KoHookAct(kKoForced) != kKoActRunForced || KoHookAct(kKoEnd) != kKoActEndTask) Fail("the hook's running states are wrong");
    const int holdStates[4] = { kKoNew, kKoHeld, kKoEnded, kKoForcedDone };
    for (int i = 0; i < 4; ++i) if (KoHookAct(holdStates[i]) != kKoActHold) Fail("the hook ran the task in a waiting state");
    /* slots the task left */
    if (!KoSlotDone(kKoEnded, 0)) Fail("an ENDED slot was kept");
    if (KoSlotDone(kKoForcedDone, kKoGoneMs) || !KoSlotDone(kKoForcedDone, kKoGoneMs + 1)) Fail("a FORCED_DONE slot was not freed once its task left");
    if (KoSlotDone(kKoForced, kKoGoneMs) || !KoSlotDone(kKoForced, kKoGoneMs + 1)) Fail("a FORCED slot was not freed once its task left");
    if (KoSlotDone(kKoHeld, 999999u) || KoSlotDone(kKoNew, 999999u)) Fail("a slot the main thread decides was freed as idle");
    /* a lastSeen the task's update stamped after `now` was read is 0 ms old, never ~49 days */
    if (EffSinceMs(1000u, 1005u) != 0u || KoSlotDone(kKoForcedDone, EffSinceMs(1000u, 1005u)) || KoSlotDone(kKoEnd, EffSinceMs(1000u, 1005u)))
        Fail("a lastSeen ahead of now freed a live slot");
    if (EffSinceMs(5u, 0xFFFFFFF0u) != 21u || EffSinceMs(3000u, 1000u) != 2000u) Fail("the wrapping clock's elapsed time is wrong");
    /* the owner gives up before the assassin's game does */
    if (!(kKoOwnerCutMs + kKoRoundTripMs <= kKoAnswerMs)) Fail("the owner's cutoff leaves no round trip inside the assassin's wait");
    if (kKoOwnerCutMs != 1500u || kKoAnswerMs != 3000u) Fail("the owner's cut (1.5 s) or the assassin's wait (3 s) changed");
    if (KoOwnerPastCut(0) || KoOwnerPastCut(kKoOwnerCutMs) || !KoOwnerPastCut(kKoOwnerCutMs + 1)) Fail("the owner's cut is in the wrong place");
    /* a classification difference is named both ways; agreement and an unreadable owner word are not a difference */
    if (KoClassDiffer(kKoDecidedNpc, 1) != kKoClassOwnerSaysPc) Fail("an NPC request for the owner's player character was not named a difference");
    if (KoClassDiffer(kKoDecidedOwner, 0) != kKoClassOwnerSaysNpc) Fail("a player-character request for the owner's NPC was not named a difference");
    if (KoClassDiffer(kKoDecidedNpc, 0) != kKoClassSame || KoClassDiffer(kKoDecidedNpc, -1) != kKoClassSame
        || KoClassDiffer(kKoDecidedOwner, 1) != kKoClassSame || KoClassDiffer(kKoDecidedOwner, -1) != kKoClassSame)
        Fail("an agreement or an unreadable owner word was named a difference");
    /* the forced chance against the engine's roll at both ends of its range (0 and 100) */
    if (KoForcedChance(kKoOutcomeKnockedOut) != FLT_MAX || KoForcedChance(kKoOutcomeNoticed) != -FLT_MAX) Fail("the forced chances are wrong");
    const float ends[3] = { 0.0f, 99.999f, 100.0f };
    for (int i = 0; i < 3; ++i)
    {
        if (!KoRollSucceeds(KoForcedChance(kKoOutcomeKnockedOut), ends[i])) Fail("a forced KNOCKED OUT lost to a roll");
        if (KoRollSucceeds(KoForcedChance(kKoOutcomeNoticed), ends[i])) Fail("a forced NOTICED won against a roll");
    }
    if (!KoRollSucceeds(50.0f, 49.9f) || KoRollSucceeds(50.0f, 50.0f) || KoRollSucceeds(0.0f, 0.0f)) Fail("success is not chance > roll");
    /* THE VICTIM'S GAME DECIDES: the request's own check (FAR from its copy of the assassin) first, then a victim down (ALREADY),
       then the victim seeing the assassin (REFUSED STATE), then the roll */
    const int nearOk = EffectValidate(true, true, true, true, false, 100.0f * 100.0f);
    const int farR = EffectValidate(true, true, true, true, false, (kFarUnits + 1.0f) * (kFarUnits + 1.0f));
    if (nearOk != kReasonNone || farR != kReasonFar) Fail("the distance check is wrong");
    if (KoDecide(KoOwnerCheck(false, true, nearOk), 99.0f, 1.0f) != kReasonState) Fail("an NPC that sees the assassin was not refused (STATE)");
    if (KoDecide(KoOwnerCheck(false, false, farR), 99.0f, 1.0f) != kReasonFar) Fail("a victim too far from the assassin's copy was not refused (FAR)");
    if (KoDecide(KoOwnerCheck(false, true, farR), 99.0f, 1.0f) != kReasonFar) Fail("the distance check did not come before the sight check");
    if (KoDecide(KoOwnerCheck(true, true, nearOk), 99.0f, 1.0f) != kKoOwnAlready) Fail("a victim already down was not ALREADY");
    if (KoDecide(KoOwnerCheck(true, true, kReasonNotSenders), 99.0f, 1.0f) != kReasonNotSenders) Fail("the request's own check did not come first");
    if (KoOwnerCheck(false, false, nearOk) != kKoOwnRoll) Fail("a victim that does not see the assassin was not rolled for");
    if (KoDecide(kKoOwnRoll, 60.0f, 59.9f) != kKoOwnKnockedOut || KoDecidedOutcome(kKoOwnKnockedOut) != kKoOutcomeKnockedOut)
        Fail("a roll under the chance did not knock out");
    if (KoDecide(kKoOwnRoll, 60.0f, 60.0f) != kKoOwnFailed || KoDecide(kKoOwnRoll, 0.0f, 0.0f) != kKoOwnFailed
        || KoDecidedOutcome(kKoOwnFailed) != kKoOutcomeNoticed)
        Fail("a roll at or over the chance did not fail");
    if (KoDecidedOutcome(kReasonState) != kKoOutcomeNone || KoDecidedOutcome(kReasonFar) != kKoOutcomeNone || KoDecidedOutcome(kKoOwnAlready) != kKoOutcomeNone)
        Fail("a refusal or ALREADY carried an outcome");
}

/* a bought house on the friend's game: the teardown's per-piece rule and the room step's reach */
void t_houses_b2()
{
    using namespace coopbuild;
    /* the engine's own rule in 0x54EA40(interior, 1) */
    if (BuildTeardownEngineDestroys(0, 0xB) != 1) Fail("houses_b2: a building piece whose vt+0x438 hand names nothing was kept");
    if (BuildTeardownEngineDestroys(0, 0) != 0) Fail("houses_b2: a building piece whose hand names an object was destroyed");
    if (BuildTeardownEngineDestroys(0, -1) != 0) Fail("houses_b2: a piece whose hand was not read was destroyed");
    if (BuildTeardownEngineDestroys(1, 0) != 1 || BuildTeardownEngineDestroys(-1, -1) != 1) Fail("houses_b2: a piece of another data type was kept");
    /* the buyer's own purchase: the mod's teardown only for the armed house, on the main thread, under the sale rule */
    if (BuildBuyerTeardownRuns(1, 1, 1, 1) != 1) Fail("houses_b2: the armed purchase's teardown was left to the engine");
    if (BuildBuyerTeardownRuns(0, 1, 1, 1) != 0) Fail("houses_b2: a teardown no purchase armed was taken");
    if (BuildBuyerTeardownRuns(1, 0, 1, 1) != 0) Fail("houses_b2: a teardown off the main thread was taken");
    if (BuildBuyerTeardownRuns(1, 1, 0, 1) != 0) Fail("houses_b2: another interior's teardown was taken");
    if (BuildBuyerTeardownRuns(1, 1, 1, 0) != 0) Fail("houses_b2: a teardown without the sale rule was taken");
    if (BuildTeardownRoomStepHere(0) != 1) Fail("houses_b2: the friend's road skipped the room step");
    if (BuildTeardownRoomStepHere(1) != 0) Fail("houses_b2: the buyer's road ran the room step a second time");
    /* T-987 fold 1: the per-piece act, in its order - dead, the live owner, no key, a record, the engine's rule */
    if (BuildTeardownPieceAct(0, 1, 1, 1, 1) != kBuildTdDead) Fail("houses_b2: a dead hand was judged");
    if (BuildTeardownPieceAct(1, 1, 1, 0, 1) != kBuildTdKeepOwner) Fail("houses_b2: a piece a player's faction owns was removed");
    if (BuildTeardownPieceAct(1, 1, 0, 0, 1) != kBuildTdKeepOwner) Fail("houses_b2: an owned piece with no key was not kept by its owner");
    if (BuildTeardownPieceAct(1, 1, 1, 1, 1) != kBuildTdKeepOwner) Fail("houses_b2: the owner rule was not asked first");
    if (BuildTeardownPieceAct(1, 0, 0, 1, 1) != kBuildTdKeepNoKey) Fail("houses_b2: a piece with no key was removed");
    if (BuildTeardownPieceAct(1, 0, 1, 1, 1) != kBuildTdKeepPlayer) Fail("houses_b2: a piece with a record (exact or near key, host row, listed copy) was removed");
    if (BuildTeardownPieceAct(1, 0, 1, 1, 0) != kBuildTdKeepPlayer) Fail("houses_b2: a recorded piece was not counted as a player's");
    if (BuildTeardownPieceAct(1, 0, 1, 0, 1) != kBuildTdRemove) Fail("houses_b2: a town piece the engine destroys was kept");
    if (BuildTeardownPieceAct(1, 0, 1, 0, 0) != kBuildTdKeepEngine) Fail("houses_b2: a piece the engine keeps was removed");
    if (BuildTeardownPieceAct(1, 0, 1, 0, -1) != kBuildTdKeepEngine) Fail("houses_b2: a piece whose rule faulted was removed");
    /* the live owner: a faulted read keeps, a player's or a stand-in owner keeps, a town owner (or none) does not */
    if (BuildTeardownOwnerKeeps(0, 0) != 1) Fail("houses_b2: a piece whose owner could not be read was not kept");
    if (BuildTeardownOwnerKeeps(1, 1) != 1) Fail("houses_b2: a piece a player's faction or a stand-in owns was not kept");
    if (BuildTeardownOwnerKeeps(1, 0) != 0) Fail("houses_b2: a town-owned piece was kept by its owner");
    /* the owner recorded before the house's owner write: in the record, as the live rule; not in it, the key rules only */
    if (BuildTeardownOwnerBeforeKeeps(1, 0, 0) != 1) Fail("houses_b2: a recorded piece whose owner could not be read was not kept");
    if (BuildTeardownOwnerBeforeKeeps(1, 1, 1) != 1) Fail("houses_b2: a piece a player or a stand-in owned before the write was not kept");
    if (BuildTeardownOwnerBeforeKeeps(1, 1, 0) != 0) Fail("houses_b2: a piece the town owned before the write was kept by its owner");
    if (BuildTeardownOwnerBeforeKeeps(0, 0, 0) != 0 || BuildTeardownOwnerBeforeKeeps(0, 1, 1) != 0 || BuildTeardownOwnerBeforeKeeps(0, 0, 1) != 0)
        Fail("houses_b2: a piece missing from the record was kept by the owner rule");
    if (BuildTeardownPieceAct(1, BuildTeardownOwnerBeforeKeeps(0, 0, 0), 1, 0, 1) != kBuildTdRemove) Fail("houses_b2: an unrecorded town piece the engine destroys was kept");
    if (BuildTeardownPieceAct(1, BuildTeardownOwnerBeforeKeeps(0, 0, 0), 1, 1, 1) != kBuildTdKeepPlayer) Fail("houses_b2: an unrecorded piece with a record was removed");
    /* the room step frees only the users of nobody's BUILDING pieces */
    if (BuildTeardownFreesUsers(kBuildTdRemove, 0) != 1 || BuildTeardownFreesUsers(kBuildTdKeepEngine, 0) != 1) Fail("houses_b2: a town piece's user was not freed");
    if (BuildTeardownFreesUsers(kBuildTdRemove, 3) != 0 || BuildTeardownFreesUsers(kBuildTdKeepEngine, 1) != 0) Fail("houses_b2: the user of an entry of another type was freed");
    if (BuildTeardownFreesUsers(kBuildTdKeepPlayer, 0) != 1 || BuildTeardownFreesUsers(kBuildTdKeepOwner, 0) != 1 || BuildTeardownFreesUsers(kBuildTdKeepNoKey, 0) != 1)
        Fail("houses_b2: a kept building piece's user was not freed (the engine purchase's room step frees every resolved building piece's)");
    if (BuildTeardownFreesUsers(kBuildTdKeepPlayer, 2) != 0 || BuildTeardownFreesUsers(kBuildTdKeepOwner, 1) != 0) Fail("houses_b2: a kept entry of another type had its user freed");
    if (BuildTeardownFreesUsers(kBuildTdDead, 0) != 0 || BuildTeardownFreesUsers(kBuildTdKeepNoErase, 0) != 0) Fail("houses_b2: an unresolved or second-set entry's user was freed");
    /* the second set (+0x128): removed only with its erase; the keeps stand */
    if (BuildTeardownSet2Act(kBuildTdRemove, 0) != kBuildTdKeepNoErase) Fail("houses_b2: a second-set entry was removed without the set's erase");
    if (BuildTeardownSet2Act(kBuildTdRemove, 1) != kBuildTdRemove) Fail("houses_b2: a second-set entry was kept although the erase can be called");
    if (BuildTeardownSet2Act(kBuildTdKeepOwner, 0) != kBuildTdKeepOwner || BuildTeardownSet2Act(kBuildTdKeepPlayer, 1) != kBuildTdKeepPlayer || BuildTeardownSet2Act(kBuildTdDead, 1) != kBuildTdDead) Fail("houses_b2: a kept or dead second-set entry changed act");
    /* a teardown that could not judge a house that had pieces */
    if (BuildTeardownBlind(5, 0, 0, 0) != 1 || BuildTeardownBlind(5, 3, 1, 0) != 1 || BuildTeardownBlind(5, 3, 0, 1) != 1) Fail("houses_b2: an unjudged house with pieces was not counted");
    if (BuildTeardownBlind(0, 0, 0, 0) != 0 || BuildTeardownBlind(5, 5, 0, 0) != 0 || BuildTeardownBlind(-1, 0, 0, 0) != 0) Fail("houses_b2: an empty, fully read or unreadable set counted as unjudged");
}
/* P18: a building's owner change - who sends (the game whose player the building now belongs to), who applies (the other game's PLACE
   adopt through the one owner rule) and the loop refusal (the mod's own write is never sent back) */
void t_p18f1_fold()
{
    using namespace coopbuild;
    /* item 1: a fault inside a guarded owner change left the nesting count at 2 */
    int depth = 2;
    if (BuildOwnerChangeAct(1, 0, depth + 1, 0, 1) != kBuildOcNested) Fail("p18f1: the stuck count did not show the bug (a purchase judged nested)");
    if (BuildOcDepthReset(&depth) != 1 || depth != 0) Fail("p18f1: the tick did not reset a stuck nesting count");
    if (BuildOwnerChangeAct(1, 0, depth + 1, 0, 1) != kBuildOcRegister) Fail("p18f1: a purchase after the reset was not queued");
    if (BuildOcDepthReset(&depth) != 0 || depth != 0) Fail("p18f1: a clean count counted as a reset");
    /* item 4: only buildings with a key */
    if (BuildOwnerChangeQueueAct(kBuildOcRegister, 1, 0) != kBuildOcNotBuilding) Fail("p18f1: furniture was queued");
    if (BuildOwnerChangeQueueAct(kBuildOcRegister, 0, 1) != kBuildOcNotBuilding) Fail("p18f1: a door was queued");
    if (BuildOwnerChangeQueueAct(kBuildOcRegister, 0, 0) != kBuildOcRegister) Fail("p18f1: a building was refused");
    if (BuildOwnerChangeQueueAct(kBuildOcNested, 1, 1) != kBuildOcNested) Fail("p18f1: another verdict was changed");
    if (BuildOwnerChangeQueueAct(kBuildOcModWrite, 1, 0) != kBuildOcModWrite) Fail("p18f1: the loop refusal was changed");
    /* items 2 / 6: a purchase is an adopt from the world of a copy that was for sale here, to a named owner */
    if (BuildOwnerAdoptIsPurchase(1, 0, 1, 1, 1) != 1) Fail("p18f1: a bought house was not a purchase");
    if (BuildOwnerAdoptIsPurchase(1, 0, 0, 1, 1) != 0) Fail("p18f1: a building not for sale counted as a purchase");
    if (BuildOwnerAdoptIsPurchase(1, 0, -1, 1, 1) != 0) Fail("p18f1: a faulted for-sale read counted as a purchase");
    if (BuildOwnerAdoptIsPurchase(0, 0, 1, 1, 1) != 0) Fail("p18f1: an adopt not from the world counted as a purchase");
    if (BuildOwnerAdoptIsPurchase(1, 1, 1, 1, 1) != 0) Fail("p18f1: furniture counted as a purchase");
    if (BuildOwnerAdoptIsPurchase(1, 0, 1, 0, 1) != 0) Fail("p18f1: no owner change counted as a purchase");
    if (BuildOwnerAdoptIsPurchase(1, 0, 1, 1, 0) != 0) Fail("p18f1: an empty target counted as a purchase");
    if (BuildOwnerApplyOutcome(1) != kBuildOaApplied) Fail("p18f1: a written owner not applied");
    if (BuildOwnerApplyOutcome(0) != kBuildOaSkip) Fail("p18f1: a not-written owner (empty target) counted as a fault");
    if (BuildOwnerApplyOutcome(-1) != kBuildOaFaulted) Fail("p18f1: a faulted write not counted as a fault");
    /* item 2: the resident list never grows past this mod's room */
    if (BuildBuyResidentsFit(3, 2, 256) != 1) Fail("p18f1: a small owner's residents refused");
    if (BuildBuyResidentsFit(200, 60, 256) != 0) Fail("p18f1: a list that could grow was allowed");
    if (BuildBuyResidentsFit(254, 0, 256) != 1 || BuildBuyResidentsFit(255, 0, 256) != 0) Fail("p18f1: the room edge (the last branch's one more) is wrong");
    if (BuildBuyResidentsFit(0xFFFFFFFFu, 2, 256) != 0 || BuildBuyResidentsFit(2, 0xFFFFFFFFu, 256) != 0) Fail("p18f1: an absurd count wrapped");
    /* item 5: a stand-in's house is not for sale; the engine's answer is kept otherwise */
    if (BuildForSaleKeep(1, 0) != 1) Fail("p18f1: a town house was refused for sale");
    if (BuildForSaleKeep(1, 1) != 0) Fail("p18f1: a stand-in's house was for sale");
    if (BuildForSaleKeep(0, 0) != 0 || BuildForSaleKeep(0, 1) != 0) Fail("p18f1: a house the engine refuses was made for sale");
}

/* P18 fold 2 (P19 review): a town rebuild holds a saved house another player owns (its stand-in record id), and only that. Fails
   before: BuildRebuildHold did not exist and the engine's drop took every saved building not this game's player's. */
static int RbSlot(const char* id) { return coopbuild::BuildRebuildOwnerSlot(coopslot::StandInSlotOfId(id), coopslot::IsLegacyPeerId(id) ? 1 : 0); }
void t_p18f2_rebuild_hold()
{
    using namespace coopbuild;
    if (RbSlot("coop-p3") != 3 || RbSlot("coop-p0") != 0) Fail("p18f2: a stand-in record id was not read as its slot");
    if (RbSlot("coop-peer") != -2) Fail("p18f2: the protocol-67 coop-peer was not read as a stand-in");
    if (RbSlot("204-gamedata.base") != -1 || RbSlot("") != -1 || RbSlot("coop-px") != -1 || RbSlot("coop-p") != -1 || RbSlot("coop-p03") != -1)
        Fail("p18f2: an id that is no stand-in record was read as one");
    if (BuildRebuildHold(1, RbSlot("coop-p3")) != 1) Fail("p18f2: another player's saved house was dropped by a town rebuild");
    if (BuildRebuildHold(1, RbSlot("coop-peer")) != 1) Fail("p18f2: an old save's coop-peer house was dropped by a town rebuild");
    if (BuildRebuildHold(1, RbSlot("204-gamedata.base")) != 0) Fail("p18f2: an NPC / town building the engine drops was held");
    if (BuildRebuildHold(0, RbSlot("coop-p3")) != 0) Fail("p18f2: an instance whose first state is the other kind (0x53) was held");
    if (BuildRebuildHold(1, -3) != 0) Fail("p18f2: an unreadable owner was held");
}

/* a kept piece the area's fresh flag gave the town's faction gets its row's owner back - only a current, listed, settled row whose key
   names exactly one piece of its record, and never a piece of this game's player or of another player. Fails before: the rule did
   not exist and the piece stayed the town's. */
void t_t492_rebuild_restore()
{
    using namespace coopbuild;
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 1, kBuildRrOwnerTown, 1) != kBuildRrRestore) Fail("t492: a kept piece the town took was not given back");
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 1, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t492: a piece already its row's owner's was written");
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 1, kBuildRrOwnerMine, 1) != kBuildRrNotMine) Fail("t492: this game's player's piece was taken");
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 1, kBuildRrOwnerOther, 1) != kBuildRrNotMine) Fail("t492: another player's piece was taken");
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 0, kBuildRrOwnerTown, 1) != kBuildRrAmbiguous) Fail("t492: a piece of another record at the key was taken");
    if (BuildRebuildRestoreAct(1, 1, 0, -1, 1, kBuildRrOwnerTown, 1) != kBuildRrAmbiguous) Fail("t492: a key naming two pieces was acted on");
    if (BuildRebuildRestoreAct(1, 1, 0, -2, 0, kBuildRrOwnerRight, 1) != kBuildRrNotYet) Fail("t492: a key not answerable yet (cut walk, host not loaded) was made final");
    if (BuildRebuildRestoreAct(1, 1, 0, -2, 1, kBuildRrOwnerTown, 0) != kBuildRrNotYet) Fail("t492: a not-yet row was judged on an owner it could not read");
    if (BuildRebuildRestoreAct(0, 1, 0, -2, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t492: a not-yet answer kept a removed row's area walking");
    if (BuildRebuildRestoreAct(1, 1, 1, -2, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t492: a not-yet answer kept a pending row's area walking");
    if (BuildRebuildRestoreRetry(1, 0, 20) != 1 || BuildRebuildRestoreRetry(3, 19, 20) != 1) Fail("t492: an area with not-yet rows was not walked again");
    if (BuildRebuildRestoreRetry(1, 20, 20) != 0) Fail("t492: the not-yet passes were not bounded");
    if (BuildRebuildRestoreRetry(0, 0, 20) != 0) Fail("t492: an area with nothing left was walked again");
    if (BuildRebuildRestoreAct(1, 1, 0, -3, 0, kBuildRrOwnerRight, 1) != kBuildRrCut) Fail("t492: a cut walk was retried (it is cut the same way again)");
    if (BuildRebuildRestoreAct(1, 1, 1, -3, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t492: a pending row's cut walk was counted");
    /* within the second only a zone teardown wakes an area */
    if (BuildRebuildRestoreWake(kBuildRrWaitTorn | kBuildRrWaitInside | kBuildRrWaitSecond, 0, 0) != 0) Fail("t492: an area was walked again within the second with no teardown");
    if (BuildRebuildRestoreWake(kBuildRrWaitTorn, 1, 0) != 1) Fail("t492: a teardown's area was not walked again after a zone was taken apart");
    if (BuildRebuildRestoreWake(kBuildRrWaitTorn, 0, 1) != 0) Fail("t492: a teardown's area was woken by a second");
    if (BuildRebuildRestoreWake(kBuildRrWaitInside, 0, 1) != 1) Fail("t492: an inside wait was not walked again at the second");
    if (BuildRebuildRestoreWake(kBuildRrWaitInside, 1, 0) != 0) Fail("t492: a teardown woke an inside wait");
    if (BuildRebuildRestoreWake(kBuildRrWaitSecond, 0, 1) != 1) Fail("t492: an unreadable state was not looked at again at the second");
    if (BuildRebuildRestoreWake(kBuildRrWaitSecond, 1, 0) != 0) Fail("t492: an unreadable state was looked at again within the second");
    if (BuildRebuildRestoreWake(0, 1, 1) != 0) Fail("t492: an area with no waiting rows was woken");
    if (BuildRebuildRestoreAct(1, 1, 0, 0, 0, kBuildRrOwnerTown, 1) != kBuildRrNone) Fail("t492: a key naming no piece was counted");
    if (BuildRebuildRestoreAct(0, 1, 0, 1, 1, kBuildRrOwnerTown, 1) != kBuildRrNone) Fail("t492: a removed / dismantled / old-world row was acted on");
    if (BuildRebuildRestoreAct(1, 0, 0, 1, 1, kBuildRrOwnerTown, 1) != kBuildRrNone) Fail("t492: a copy row not on the copy list was acted on");
    if (BuildRebuildRestoreAct(1, 1, 1, 1, 1, kBuildRrOwnerTown, 1) != kBuildRrNone) Fail("t492: a row with a PLACE / REMOVE waiting was acted on");
    if (BuildRebuildRestoreAct(1, 1, 0, 1, 1, kBuildRrOwnerTown, 0) != kBuildRrNoOwner) Fail("t492: a row whose owner is not here was written");
    if (BuildRebuildRestoreState(0, 1, 0, 0) != kBuildRrStateOwn) Fail("t492: an own row asked for a STATE");
    if (BuildRebuildRestoreState(1, 1, 0, 0) != kBuildRrStateQueue) Fail("t492: a copy's owner's last STATE was not queued");
    if (BuildRebuildRestoreState(1, 1, 1, 0) != kBuildRrStateHeld) Fail("t492: a waiting (newer) STATE was overwritten");
    if (BuildRebuildRestoreState(1, 0, 0, 0) != kBuildRrStateUnknown) Fail("t492: a copy with no owner's state known queued one");
    if (BuildRebuildRestoreState(1, 1, 0, 1) != kBuildRrStateWallKept) Fail("t492: a wall ruin kept here was put back");
}

/* a teammate's piece its area loaded without (the area built from a record older than the piece) is made again from the copy list -
   only a current, listed (its own entry, not removed), settled row whose key answered a complete empty search and whose owner's last
   word is not a ruin; every other answer keeps the after-load restore's branches. A re-make lands only while still owed; one that lands
   starts the copy list's empty-load count again, one that cannot keeps the eviction. Fails before: the rule did not exist and the row
   was left (kBuildRrNone). */
void t_t805_remake_choose()
{
    using namespace coopbuild;
    /* Choose(rowCurrent, rowKnown, listMain, pending, found, emptyComplete, ownerGone, sidMatch, ownerClass, wantHere) */
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 0, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrRemake) Fail("t805: a complete empty search was not re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 0, 1, 0, 0, kBuildRrOwnerRight, 0) != kBuildRrRemake) Fail("t805: an owner not here yet kept the re-make from the road");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 0, 0, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: a piece being dismantled, or a found-null answer, was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, -3, 0, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrCut) Fail("t805: a cut walk was not left to the cut branch");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, -3, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrCut) Fail("t805: a cut walk was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, -2, 0, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNotYet) Fail("t805: a not-yet answer (truncated, torn, host not loaded) was not left to wait");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, -2, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNotYet) Fail("t805: a not-yet answer was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, -1, 1, 0, 0, kBuildRrOwnerTown, 1) != kBuildRrAmbiguous) Fail("t805: a key naming two pieces was re-made");
    if (BuildRebuildRestoreChoose(0, 1, 1, 0, 0, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: a removed / dismantled / old-world row was re-made");
    if (BuildRebuildRestoreChoose(1, 0, 1, 0, 0, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: a row the copy list does not know (or marks removed) was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 0, 0, 0, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: an alias entry was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 1, 0, 1, 0, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: a row with a PLACE / REMOVE waiting was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 0, 1, 1, 0, kBuildRrOwnerRight, 1) != kBuildRrNone) Fail("t805: a piece the owner's last word made a ruin was re-made");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 1, 1, 0, 1, kBuildRrOwnerTown, 1) != kBuildRrRestore) Fail("t805: a found piece the town took lost the restore");
    if (BuildRebuildRestoreChoose(1, 1, 1, 0, 1, 0, 0, 1, kBuildRrOwnerMine, 1) != kBuildRrNotMine) Fail("t805: a found piece of this game's player was not left alone");
    /* coverage: every input - the re-make exactly on its conjunction, BuildRebuildRestoreAct's answer everywhere else */
    const int founds[5] = { -3, -2, -1, 0, 1 };
    int remakes = 0, others = 0;
    for (int rc = 0; rc < 2; ++rc) for (int rk = 0; rk < 2; ++rk) for (int lm = 0; lm < 2; ++lm) for (int pe = 0; pe < 2; ++pe)
    for (int fi = 0; fi < 5; ++fi) for (int ec = 0; ec < 2; ++ec) for (int og = 0; og < 2; ++og) for (int sm = 0; sm < 2; ++sm)
    for (int oc = 0; oc < 4; ++oc) for (int wh = 0; wh < 2; ++wh)
    {
        const int f = founds[fi];
        const int got = BuildRebuildRestoreChoose(rc, rk, lm, pe, f, ec, og, sm, oc, wh);
        const int want = (rc != 0 && rk != 0 && lm != 0 && pe == 0 && f == 0 && ec != 0 && og == 0) ? kBuildRrRemake : BuildRebuildRestoreAct(rc, rk, pe, f, sm, oc, wh);
        if (got != want) { Fail("t805: the choice differs from the rule for an input"); return; }
        if (got == kBuildRrRemake) ++remakes; else ++others;
    }
    if (remakes != 2 * 4 * 2 || others == 0) Fail("t805: the coverage leg did not see both branches");
    /* still owed when the road reaches it */
    if (BuildRemakeStillOwed(1, 0, 1, 0) != 1) Fail("t805: an owed re-make was dropped");
    if (BuildRemakeStillOwed(0, 0, 1, 0) != 0) Fail("t805: a re-make with no copy row (or an own row at the key) was made");
    if (BuildRemakeStillOwed(1, 1, 1, 0) != 0) Fail("t805: a re-make whose row was removed meanwhile was made");
    if (BuildRemakeStillOwed(1, 0, 0, 0) != 0) Fail("t805: a re-make the copy list no longer knows was made");
    if (BuildRemakeStillOwed(1, 0, 1, 1) != 0) Fail("t805: a re-make with a REMOVE waiting was made");
    /* the eviction with a re-make: the landing lists the entry again as a fresh record (BdCopyListNote: emptyLoads 0), which differs
       from a counted entry, so it is written and the count starts again; an entry the re-make could not make keeps counting */
    BuildCopyRec counted;
    counted.key = "k"; counted.sid = "s"; counted.slot = 1; counted.nonce = 7; counted.emptyLoads = kBuildCopyEmptyLoads - 1;
    BuildCopyRec landed = counted;
    landed.emptyLoads = 0;
    if (BuildCopyRecSame(counted, landed)) Fail("t805: a landed re-make did not refresh a counted copy-list entry");
    if (BuildCopyEvict(landed.emptyLoads) != 0) Fail("t805: a landed re-make's entry was evicted");
    if (BuildCopyEvict(counted.emptyLoads + 1) != 1) Fail("t805: an entry the re-make could not make escaped the eviction");
    /* the host's inside: only an explicit 'loaded' answer counts, at the pass and at the run */
    if (BuildRemakeInsideOk(1, 1) != 1) Fail("t805: a loaded inside was not taken as loaded");
    if (BuildRemakeInsideOk(1, 0) != 0) Fail("t805: an inside not loaded was taken as loaded");
    if (BuildRemakeInsideOk(1, -1) != 0) Fail("t805: a faulted inside read passed as loaded");
    if (BuildRemakeInsideOk(0, 0) != 1 || BuildRemakeInsideOk(0, -1) != 1) Fail("t805: a free-standing re-make waited for an inside");
    /* the run asks the keys the pass asked */
    if (BuildRemakeKeysAnswer(1, 0, 0, 0) != kBuildRemakeKeysNone) Fail("t805: one key, complete and empty, was not made");
    if (BuildRemakeKeysAnswer(1, 1, 0, 1) != kBuildRemakeKeysNone) Fail("t805: both keys complete and empty was not made");
    if (BuildRemakeKeysAnswer(1, 1, 1, 0) != kBuildRemakeKeysFound) Fail("t805: a piece back at its own key was not taken on (a twin)");
    if (BuildRemakeKeysAnswer(1, 1, 0, 0) != kBuildRemakeKeysShort) Fail("t805: a short answer at the copy's own key was taken as empty");
    if (BuildRemakeKeysAnswer(0, 0, 0, 0) != kBuildRemakeKeysShort || BuildRemakeKeysAnswer(0, 1, 0, 1) != kBuildRemakeKeysShort) Fail("t805: a short answer at the builder's key was taken as empty");
    /* the run's gate, for every re-make */
    if (BuildRemakeRunGate(0, 1, 1, 0, 3) != kBuildRemakeMake) Fail("t805: a free-standing complete empty search was not made");
    if (BuildRemakeRunGate(1, 1, 1, 2, 3) != kBuildRemakeMake) Fail("t805: a complete answer at the last try was given up");
    if (BuildRemakeRunGate(1, 0, 1, 0, 3) != kBuildRemakeWait) Fail("t805: a re-make inside a host whose inside is not loaded was made");
    if (BuildRemakeRunGate(1, -1, 1, 0, 3) != kBuildRemakeWait) Fail("t805: a re-make inside a host whose inside did not read was made");
    if (BuildRemakeRunGate(1, 1, 0, 0, 3) != kBuildRemakeWait) Fail("t805: furniture after a search cut short was made");
    if (BuildRemakeRunGate(1, 1, 0, 2, 3) != kBuildRemakeGiveUp) Fail("t805: a furniture re-make whose search stays cut short waited on");
    if (BuildRemakeRunGate(0, 1, 0, 2, 3) != kBuildRemakeGiveUp) Fail("t805: a free-standing re-make whose search stays cut short waited on");
    if (BuildRemakeRunGate(1, 0, 1, 2, 3) != kBuildRemakeGiveUp) Fail("t805: a re-make whose host's inside never loads waited on");
    {   /* bounded: a wait that never sees a complete answer ends within cap */
        int tries = 0, g = kBuildRemakeWait;
        while (g == kBuildRemakeWait && tries < 100) { g = BuildRemakeRunGate(1, 0, 0, tries, 3); if (g == kBuildRemakeWait) ++tries; }
        if (g != kBuildRemakeGiveUp || tries != 2) Fail("t805: a re-make that never got a complete answer was not given up at the cap");
    }
    /* each ending is counted once */
    if (BuildRemakeEnds(1, 0) != 1) Fail("t805: a re-make ending with no outcome was not counted");
    if (BuildRemakeEnds(1, 1) != 0 || BuildRemakeEnds(1, 2) != 0 || BuildRemakeEnds(1, 3) != 0 || BuildRemakeEnds(1, 4) != 0) Fail("t805: a re-make with its outcome was counted again");
    if (BuildRemakeEnds(0, 0) != 0) Fail("t805: an entry that is no re-make (a PLACE merged in) was counted");
    {   /* the tokens add up: made, taken on, refused, faulted, ended by the road with no outcome (refused), dropped (a REMOVE, a merge,
           a teardown) - and a teardown after any of them counts nothing more */
        long long made = 0, foundN = 0, refused = 0, faulted = 0, dropped = 0;
        const int outs[6] = { 1, 2, 3, 4, 0, 0 };
        for (int i = 0; i < 6; ++i)
        {
            int remake = 1, out = outs[i];
            if (out == 1) ++made; else if (out == 2) ++foundN; else if (out == 3) ++refused; else if (out == 4) ++faulted;
            if (i == 4 && BuildRemakeEnds(remake, out) != 0) { ++refused; out = 3; }
            if (i == 5) { if (BuildRemakeEnds(remake, out) != 0) ++dropped; remake = 0; out = 0; }
            if (BuildRemakeEnds(remake, out) != 0) ++dropped;
        }
        if (made + foundN + refused + faulted + dropped != 6) Fail("t805: the re-make tokens do not add up to the re-makes queued");
    }
}

void t_p18_owner_change_rule()
{
    using namespace coopbuild;
    if (BuildOwnerChangeAct(1, 0, 1, 0, 1) != kBuildOcRegister) Fail("p18: a purchase (town -> this game's player) was not registered");
    if (BuildOwnerChangeAct(0, 0, 1, 0, 1) != kBuildOcOffMain) Fail("p18: an off-main change was judged");
    if (BuildOwnerChangeAct(1, 1, 1, 0, 1) != kBuildOcModWrite) Fail("p18: LOOP - the mod's own write to this game's player would be sent");
    if (BuildOwnerChangeAct(1, 1, 1, 0, 0) != kBuildOcModWrite) Fail("p18: LOOP - a received owner (the stand-in) applied here was not refused as the mod's write");
    if (BuildOwnerChangeAct(1, 0, 2, 0, 1) != kBuildOcNested) Fail("p18: the engine's recursion (doors, interior) was judged as its own change");
    if (BuildOwnerChangeAct(1, 0, 1, 0, 0) != kBuildOcNotMine) Fail("p18: a change to a town / NPC / stand-in owner would be sent");
    if (BuildOwnerChangeAct(1, 0, 1, 1, 1) != kBuildOcWasMine) Fail("p18: a building already this game's player's was queued again");
    if (BuildOwnerChangeAct(1, 0, 1, 1, 0) != kBuildOcNotMine) Fail("p18: losing a building is not this road");
    if (BuildOwnerDrainAct(1, 0, 20, 1, 1, 0, 0) != kBuildOdWait) Fail("p18: a held tick did not wait");
    if (BuildOwnerDrainAct(0, 0, 20, 0, 0, 0, 0) != kBuildOdWait) Fail("p18: an unresolved key did not wait");
    if (BuildOwnerDrainAct(0, 19, 20, 0, 0, 0, 0) != kBuildOdGiveUp) Fail("p18: an unresolved key waited past its tries");
    if (BuildOwnerDrainAct(0, 0, 20, 1, 0, 0, 0) != kBuildOdNotMine) Fail("p18: a building no longer this player's was registered");
    if (BuildOwnerDrainAct(0, 0, 20, 1, 1, 0, 1) != kBuildOdCopyRow) Fail("p18: another game's piece (a copy row) would be claimed");
    if (BuildOwnerDrainAct(0, 0, 20, 1, 1, 1, 0) != kBuildOdKnown) Fail("p18: an own piece was registered twice");
    if (BuildOwnerDrainAct(0, 0, 20, 1, 1, 0, 0) != kBuildOdScan) Fail("p18: a bought house was not registered");
    if (BuildOwnerDrainAct(0, 0, 20, 1, 1, 1, 1) != kBuildOdCopyRow) Fail("p18: a copy row did not win over the own test");
    const int town = BuildOwnerClass(0, 0, 0, 0, -1, 1);
    if (town != kBuildLiveOwnThird || BuildOwnerAdoptFromWorld(town) != 1) Fail("p18: a town's house was not adopted as an owner change on the receiver");
    if (BuildOwnerAdoptFromWorld(BuildOwnerClass(1, 0, 0, 0, -1, 1)) != 1) Fail("p18: a building nobody owns was not adopted as an owner change");
    if (BuildOwnerClass(0, 1, 0, 0, -1, 1) != kBuildLiveOwnMine || BuildOwnerAdoptFromWorld(kBuildLiveOwnMine) != 0) Fail("p18: the receiver's own building would be taken");
    if (BuildOwnerAdoptFromWorld(BuildOwnerClass(0, 0, 1, 1, -1, 1)) != 0) Fail("p18: the sender's own stand-in counted as a change");
    if (BuildOwnerAdoptFromWorld(BuildOwnerClass(0, 0, 0, 1, 2, 1)) != 0) Fail("p18: another slot's stand-in counted as a town owner");
    if (BuildOwnerAdoptFromWorld(BuildOwnerClass(0, 0, 0, 0, -1, 1, 1, 1, 0)) != 0) Fail("p18: a listed copy counted as a town owner");
}

/* PP7 (owner 162): a stamped folder is left out of LOAD GAME and is never CONTINUE's choice; an unstamped one is shown; the
   MULTIPLAYER menu's autoload, IMPORT GAME and every other use are never filtered. */
static coopprof::ContinueCand Pp7Dir(const char* name, bool stamped, bool hasSave, unsigned long long t)
{
    coopprof::ContinueCand c; c.name = name; c.stamped = stamped; c.hasSave = hasSave; c.saveTime = t; return c;
}
void t_pp7_list_hides_only_stamped_on_kenshi_lists()
{
    using namespace coopprof;
    if (!KenshiListHides(kSaveUseLoadMenu, true)) Fail("a stamped folder is shown in LOAD GAME");
    if (KenshiListHides(kSaveUseLoadMenu, false)) Fail("a single-player folder is left out of LOAD GAME");
    if (!KenshiListHides(kSaveUseContinue, true) || KenshiListHides(kSaveUseContinue, false)) Fail("CONTINUE: stamped hidden, single-player shown");
    if (KenshiListHides(kSaveUseAutoload, true) || KenshiListHides(kSaveUseAutoload, false)) Fail("the MULTIPLAYER autoload is filtered");
    if (KenshiListHides(kSaveUseImportMenu, true) || KenshiListHides(kSaveUseOther, true)) Fail("IMPORT GAME or another use is filtered");
}
void t_pp7_continue_choice()
{
    using namespace coopprof;
    std::vector<ContinueCand> d;
    d.push_back(Pp7Dir("world - Multiplayer - Scott", true, true, 900));
    d.push_back(Pp7Dir("s1", false, true, 100));
    d.push_back(Pp7Dir("quicksave", false, true, 300));
    d.push_back(Pp7Dir("empty", false, false, 999));
    if (ContinueChoose("s1", d) != "s1") Fail("a single-player choice is not Kenshi's own (it is kept even when not the newest)");
    if (ContinueChoose("", d) != "") Fail("no choice stays no choice");
    if (ContinueChoose("gone", d) != "gone") Fail("a choice naming no folder is left to the engine");
    if (ContinueChoose("WORLD - multiplayer - scott", d) != "quicksave") Fail("a stamped choice: the newest single-player save with a quick.save (case ignored)");
    d.push_back(Pp7Dir("Another", false, true, 300));
    if (ContinueChoose("world - Multiplayer - Scott", d) != "Another") Fail("equal times: the lower name");
    d.push_back(Pp7Dir("newer world", true, true, 5000));
    if (ContinueChoose("world - Multiplayer - Scott", d) != "Another") Fail("a newer stamped folder is never chosen");
    std::vector<ContinueCand> mp;
    mp.push_back(Pp7Dir("a - Multiplayer - b", true, true, 5));
    mp.push_back(Pp7Dir("nosave", false, false, 9));
    if (ContinueChoose("a - Multiplayer - b", mp) != "") Fail("only multiplayer saves: no choice (CONTINUE hidden, as with no saves)");
}

/* ---- T-650 fold 1: the games that may run a squad standing in an area, in offer order (src/common/liveowner.h AreaReceiverOrder) ---- */
void t_t650_area_receiver_order()
{
    using namespace cooplo;
    /* AreaReceiverOrder(rows, n, mapFresh, holderSlot, mySlot, tried, nTried, order, cap, notInWorld) */
    AreaRow rows[4];
    rows[0].slot = 0; rows[0].inWorld = 1; rows[0].loaded = 1;   /* this game */
    rows[1].slot = 1; rows[1].inWorld = 1; rows[1].loaded = 1;
    rows[2].slot = 2; rows[2].inWorld = 1; rows[2].loaded = 0;
    rows[3].slot = 3; rows[3].inWorld = 1; rows[3].loaded = 1;
    int order[8]; int niw = -1;
    int n = AreaReceiverOrder(rows, 4, 1, -1, 0, 0, 0, order, 8, &niw);
    if (n != 2 || order[0] != 1 || order[1] != 3 || niw != 0) Fail("no holder: every other game with the area loaded, lower slot first - never this game, never a game without it loaded");
    n = AreaReceiverOrder(rows, 4, 1, 3, 0, 0, 0, order, 8, 0);
    if (n != 2 || order[0] != 3 || order[1] != 1) Fail("the holder has the area loaded - not offered first");
    n = AreaReceiverOrder(rows, 4, 1, 2, 0, 0, 0, order, 8, 0);
    if (n != 2 || order[0] != 1 || order[1] != 3) Fail("a holder without the area loaded was offered (HIGH-1: naming the holder alone never makes a receiver)");
    n = AreaReceiverOrder(rows, 4, 1, 0, 0, 0, 0, order, 8, 0);
    if (n != 2 || order[0] != 1) Fail("this game named the holder - the squad must still go to a game with the area loaded (HIGH-1)");
    const int tried[2] = { 3, 7 };
    n = AreaReceiverOrder(rows, 4, 1, 3, 0, tried, 2, order, 8, 0);
    if (n != 1 || order[0] != 1) Fail("a tried game (the holder) was offered again");
    rows[1].inWorld = 0;
    n = AreaReceiverOrder(rows, 4, 1, -1, 0, tried, 2, order, 8, &niw);
    if (n != 0 || niw != 1) Fail("a game not IN_WORLD was offered, or its skip was not counted");
    rows[1].inWorld = 1;
    if (AreaReceiverOrder(rows, 4, 1, -1, 0, 0, 0, order, 1, 0) != 1 || order[0] != 1) Fail("cap 1 - the first one only");
    rows[1].loaded = 0; rows[3].loaded = 0;
    if (AreaReceiverOrder(rows, 4, 1, 1, 0, 0, 0, order, 8, 0) != 0) Fail("nobody else has the area loaded - a receiver came out (the forced path keeps, the RELEASE sleeps)");
    if (AreaReceiverOrder(0, 0, 1, -1, 0, 0, 0, order, 8, 0) != 0) Fail("a fresh map with no rows - not 0 (nobody)");
}
void t_t650_area_receiver_hold()
{
    using namespace cooplo;
    AreaRow rows[2];
    rows[0].slot = 0; rows[0].inWorld = 1; rows[0].loaded = 1;
    rows[1].slot = 1; rows[1].inWorld = 1; rows[1].loaded = 1;
    int order[4]; int niw = -1;
    if (AreaReceiverOrder(rows, 2, 0, 1, 0, 0, 0, order, 4, &niw) != -1 || niw != 0) Fail("a stale area map - not HOLD (the forced path keeps, the RELEASE holds)");
    if (AreaReceiverOrder(rows, 2, 1, 1, -1, 0, 0, order, 4, 0) != -1) Fail("L3: this game has no slot yet - not HOLD");
    rows[1].loaded = -1;
    if (AreaReceiverOrder(rows, 2, 1, 1, 0, 0, 0, order, 4, 0) != -1) Fail("a row read stale - not HOLD");
    rows[1].loaded = 1;
    AreaRow dup[3]; dup[0] = rows[0]; dup[1] = rows[1]; dup[2] = rows[1];
    if (AreaReceiverOrder(dup, 3, 1, -1, 0, 0, 0, order, 4, 0) != 1) Fail("one slot listed twice - offered twice");
    /* T1031 / HIGH-1 shape: A (slot 0) left 21,32's loaded list, the world server still names A the holder, B (slot 1) has it loaded -> B */
    if (AreaReceiverOrder(rows, 2, 1, 0, 0, 0, 0, order, 4, 0) != 1 || order[0] != 1) Fail("A still named the holder, B has the area loaded - not offered to B");
}
/* ---- the engine's keep rule (src/common/liveowner.h EngineKeepsAt) - T1056's Hub squads stood 94 units inside 19,32's west
   border with 18,32 loaded on neither game ---- */
void t_t650_engine_keeps_at()
{
    using namespace cooplo;
    /* EngineKeepsAt(x, z, minX, minZ, size, self, w, e, s, n, margin) */
    const float sz = kAreaSizeUnits, minX = AreaMinUnits(19), minZ = AreaMinUnits(32), m = kKeepMarginUnits;
    if (minX != -59904.0f || minZ != 0.0f) Fail("area 19,32 does not start at -59904,0 (4608 per area, origin 147456)");
    if (kEngineEdgeStrip != 160.0f || kKeepMarginUnits != 250.0f) Fail("the strip is not 160 or the margin not 250");
    if (EngineKeepsAt(-59810.0f, 2947.0f, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 0) Fail("T1056 spot, west neighbour 18,32 not loaded - kept");
    if (EngineKeepsAt(-59810.0f, 2947.0f, minX, minZ, sz, 1, 1, 1, 1, 1, m) != 1) Fail("T1056 spot, west neighbour loaded - not kept");
    if (EngineKeepsAt(-59810.0f, 2947.0f, minX, minZ, sz, 1, 0, 1, 1, 1, 0.0f) != 0) Fail("T1056 spot (94 from the border) with no margin - kept (the engine's own 160)");
    const float cx = minX + sz * 0.5f, cz = minZ + sz * 0.5f;
    if (EngineKeepsAt(cx, cz, minX, minZ, sz, 1, 0, 0, 0, 0, m) != 1) Fail("the area centre with no side loaded - not kept");
    if (EngineKeepsAt(cx, cz, minX, minZ, sz, 0, 1, 1, 1, 1, m) != 0) Fail("an area not loaded - kept");
    /* diagonals ignored: 50 units from the south-west corner with W and S loaded is kept - no diagonal area is an input */
    if (EngineKeepsAt(minX + 50.0f, minZ + 50.0f, minX, minZ, sz, 1, 1, 1, 1, 1, m) != 1) Fail("a corner spot with both sides loaded - not kept");
    if (EngineKeepsAt(minX + 50.0f, minZ + 50.0f, minX, minZ, sz, 1, 1, 0, 0, 1, m) != 0) Fail("a corner spot with the south side unloaded - kept");
    /* the boundary: exactly 160 + margin from an unloaded side is kept, one unit closer is not - every side */
    const float d = kEngineEdgeStrip + m;
    if (d != 410.0f) Fail("160 + 250 is not 410");
    if (EngineKeepsAt(minX + d, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 1) Fail("west: exactly 410 from the border - not kept");
    if (EngineKeepsAt(minX + d - 1.0f, cz, minX, minZ, sz, 1, 0, 1, 1, 1, m) != 0) Fail("west: 409 from the border - kept");
    if (EngineKeepsAt(minX + sz - d, cz, minX, minZ, sz, 1, 1, 0, 1, 1, m) != 1) Fail("east: exactly 410 - not kept");
    if (EngineKeepsAt(minX + sz - d + 1.0f, cz, minX, minZ, sz, 1, 1, 0, 1, 1, m) != 0) Fail("east: 409 - kept");
    if (EngineKeepsAt(cx, minZ + d, minX, minZ, sz, 1, 1, 1, 0, 1, m) != 1) Fail("south: exactly 410 - not kept");
    if (EngineKeepsAt(cx, minZ + d - 1.0f, minX, minZ, sz, 1, 1, 1, 0, 1, m) != 0) Fail("south: 409 - kept");
    if (EngineKeepsAt(cx, minZ + sz - d, minX, minZ, sz, 1, 1, 1, 1, 0, m) != 1) Fail("north: exactly 410 - not kept");
    if (EngineKeepsAt(cx, minZ + sz - d + 1.0f, minX, minZ, sz, 1, 1, 1, 1, 0, m) != 0) Fail("north: 409 - kept");
    /* unreadable flags: the area's own -> -1; a side's only when it would decide */
    if (EngineKeepsAt(cx, cz, minX, minZ, sz, -1, 1, 1, 1, 1, m) != -1) Fail("the area's own flag unreadable - not -1");
    if (EngineKeepsAt(minX + 100.0f, cz, minX, minZ, sz, 1, -1, 1, 1, 1, m) != -1) Fail("a deciding side unreadable - not -1");
    if (EngineKeepsAt(cx, cz, minX, minZ, sz, 1, -1, -1, -1, -1, m) != 1) Fail("unreadable sides far from the spot - not kept");
    if (EngineKeepsAt(minX + 100.0f, minZ + 100.0f, minX, minZ, sz, 1, -1, 1, 0, 1, m) != 0) Fail("an unloaded deciding side with another unreadable - not 0");
}
/* ---- AreaReceiverOrder skips a game whose engine would not keep the squad at its spot (keeps = 0) ---- */
void t_t650_area_receiver_keeps()
{
    using namespace cooplo;
    AreaRow def;
    if (def.keeps != 1) Fail("an AreaRow does not default to keeps 1 (callers with no position)");
    AreaRow rows[3];
    rows[0].slot = 0; rows[0].inWorld = 1; rows[0].loaded = 1;
    rows[1].slot = 1; rows[1].inWorld = 1; rows[1].loaded = 1; rows[1].keeps = 0;
    rows[2].slot = 2; rows[2].inWorld = 1; rows[2].loaded = 1; rows[2].keeps = 1;
    int order[4]; int niw = -1, nk = -1;
    int n = AreaReceiverOrder(rows, 3, 1, 1, 0, 0, 0, order, 4, &niw, &nk);
    if (n != 1 || order[0] != 2 || nk != 1 || niw != 0) Fail("the holder's engine would not keep it - not passed over for slot 2 (or not counted)");
    rows[2].keeps = 0; nk = -1;
    if (AreaReceiverOrder(rows, 3, 1, 1, 0, 0, 0, order, 4, &niw, &nk) != 0 || nk != 2) Fail("no game keeps it - a receiver came out (the RELEASE sleeps at once)");
    rows[1].inWorld = 0; nk = -1;
    if (AreaReceiverOrder(rows, 3, 1, 1, 0, 0, 0, order, 4, &niw, &nk) != 0 || nk != 1 || niw != 1) Fail("a not-in-world game counted as notKeep");
    rows[1].inWorld = 1; rows[2].keeps = 1;
    const int tried[1] = { 2 };
    nk = -1;
    if (AreaReceiverOrder(rows, 3, 1, 1, 0, tried, 1, order, 4, &niw, &nk) != 0 || nk != 1) Fail("a tried game counted as notKeep");
    rows[2].keeps = -1;
    if (AreaReceiverOrder(rows, 3, 1, 1, 0, 0, 0, order, 4, &niw, &nk) != -1) Fail("an unreadable keep - not HOLD");
    rows[2].keeps = 1; rows[0].keeps = 0;
    if (AreaReceiverOrder(rows, 3, 1, 1, 0, 0, 0, order, 4, 0) != 1 || order[0] != 2) Fail("this game's own row read, or the 10-argument call changed");
}
/* ---- the announce pass holds a withdrawal for the hand-over (src/common/liverelay.h AnnounceDecide, 6 inputs) ---- */
void t_t650_announce_hold()
{
    using namespace cooplive;
    /* AnnounceDecide(anyOtherLoaded, mineLoaded, announced, areaGained, otherInWorldLoaded, handoverRefused) */
    if (kAnnHoldForHandover == kAnnKeep || kAnnHoldForHandover == kAnnSpawn || kAnnHoldForHandover == kAnnUnload || kAnnHoldForHandover == kAnnResend) Fail("the hold shares a value");
    if (AnnounceDecide(true, false, true, false, true, false) != kAnnHoldForHandover) Fail("T1056 F2: announced, B has the area loaded, A does not - withdrawn before the hand-over");
    if (AnnounceDecide(true, false, true, false, true, true) != kAnnUnload) Fail("the forced hand-over was refused - still held");
    if (AnnounceDecide(true, false, true, false, false, false) != kAnnUnload) Fail("the game with the area loaded is not in the world - held");
    if (AnnounceDecide(false, false, true, false, false, false) != kAnnUnload) Fail("nobody else has the area - held");
    if (AnnounceDecide(false, true, true, false, false, false) != kAnnUnload) Fail("loaded here, nobody else - not withdrawn");
    if (AnnounceDecide(true, true, true, false, true, false) != kAnnKeep) Fail("loaded here and there - not kept");
    if (AnnounceDecide(true, false, false, false, true, false) != kAnnKeep) Fail("never announced - something sent");
    if (AnnounceDecide(true, false, true, true, true, false) != kAnnHoldForHandover) Fail("a gained reporter - the hold lost");
    for (int h = 0; h < 2; ++h) for (int ml = 0; ml < 2; ++ml) for (int s = 0; s < 2; ++s) for (int g = 0; g < 2; ++g)
    {
        const int four = AnnounceDecide(h != 0, ml != 0, s != 0, g != 0);
        if (AnnounceDecide(h != 0, ml != 0, s != 0, g != 0, false, false) != four) Fail("no other in-world game - not the 4-input rule");
        if (AnnounceDecide(h != 0, ml != 0, s != 0, g != 0, true, true) != four) Fail("refused - not the 4-input rule");
    }
}
/* ---- a retired uid this game no longer runs is settled with nothing sent (src/common/retirewithdraw.h) ---- */
void t_t650_retire_withdraw_not_mine()
{
    if (cooprw::RetireWithdrawGate(0, 1) != cooprw::kGateNotMine) Fail("a handed-over uid still announced - its UNLOAD would be sent");
    if (cooprw::RetireWithdrawGate(0, 0) != cooprw::kGateNotMine) Fail("not mine comes first, before not announced");
    if (cooprw::RetireWithdrawGate(1, 0) != cooprw::kGateNotAnnounced) Fail("mine, never announced - not settled as not announced");
    if (cooprw::RetireWithdrawGate(1, 1) != cooprw::kGateOwed) Fail("mine and announced - the UNLOAD is not owed");
    int c = -1;
    const int st = cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvRetire, &c);
    c = -1;
    const int done = cooprw::RetireWithdrawStep(st, cooprw::kEvNotMine, &c);
    if (done != cooprw::kWithdrawn || c != cooprw::kCountNotMine) Fail("a pending entry settled as not mine - not withdrawn with the notMine count");
    if (cooprw::RetireWithdrawShouldSend(done) != 0) Fail("a settled not-mine entry would still be sent");
    c = -1;
    if (cooprw::RetireWithdrawStep(done, cooprw::kEvNotMine, &c) != cooprw::kWithdrawn || c != cooprw::kCountNone) Fail("a second not-mine on a settled entry counted again");
    c = -1;
    if (cooprw::RetireWithdrawStep(cooprw::kAbsent, cooprw::kEvNotMine, &c) != cooprw::kAbsent || c != cooprw::kCountNone) Fail("not-mine on an absent entry made or counted one");
}

/* ---- M7a A1 BUILD 2 (the switch-over; design sec. 2.4-2.5, 3.2-3.4, 3.10, 3.11, 1.3; src/common/liveowner.h) [a1b2-t0] ---- */
void t_a1b2_adopt_decide()
{
    using namespace cooplo;
    /* AdoptDecide(haveLiveCopy, copyOwnerIsGiver, mineAlready, myGen, offerGen, takeRing, decisionCorner, myWorldLinkUp, writesBlocked, keepMargin) */
    if (AdoptDecide(1, 1, 0, 3, 4, 1, 0, 1, 0, 1) != kAdopt) Fail("3.2: a live copy of the giver's person, inside the ring, a newer offer - not adopted");
    if (AdoptDecide(1, 1, 0, 3, 4, 1, 0, 0, 0, 1) != kDefer) Fail("3.2: this game's world link down - not deferred (owner 334 a)");
    if (AdoptDecide(1, 1, 0, 3, 4, 1, 0, 1, 1, 1) != kDefer) Fail("3.2: engine writes blocked - not deferred");
    if (AdoptDecide(0, 0, 1, 5, 4, 0, 1, 1, 0, 1) != kAdoptDup) Fail("3.2: a repeat offer of a person already run here - not ACKed adopted again");
    if (AdoptDecide(0, 1, 0, 0, 4, 1, 0, 1, 0, 1) != kDrop) Fail("3.2: no copy here - not dropped");
    if (AdoptDecide(1, 0, 0, 0, 4, 1, 0, 1, 0, 1) != kStale) Fail("3.2: a copy recorded as ANOTHER game's - not refused as stale (that copy must stay)");
    if (AdoptDecide(1, 1, 0, 4, 4, 1, 0, 1, 0, 1) != kStale) Fail("3.2: an offer gen equal to the copy's - not stale");
    if (AdoptDecide(1, 1, 0, 5, 4, 1, 0, 1, 0, 1) != kStale) Fail("3.2: an offer gen below the copy's - not stale");
    if (AdoptDecide(1, 1, 0, 0, 1, 1, 0, 1, 0, 1) != kAdopt) Fail("3.2: an unknown copy gen (0) loses to any offer");
    if (AdoptDecide(1, 1, 0, 3, 4, 0, 0, 1, 0, 1) != kDrop) Fail("3.2: outside this game's ring - not dropped");
    if (AdoptDecide(1, 1, 0, 3, 4, 1, 1, 1, 0, 1) != kDrop) Fail("3.2: keepMargin 1 - a CORNER of this game's 3x3 was adopted (T821 P119)");
    if (AdoptDecide(1, 1, 0, 3, 4, 1, 1, 1, 0, 0) != kAdopt) Fail("3.2: keepMargin 0 - a corner inside the ring was refused");
    if (kKeepMargin != 1) Fail("design 7 item 7 / manager 2026-10-02: build 2 ships keepMargin 1 (T821 P119 showed corner put-aways)");
}
void t_a1b2_ring_pick()
{
    using namespace cooplo;
    if (RingKind(43, 11, 43, 11) != kRingSelf || RingKind(43, 11, 44, 11) != kRingSide || RingKind(43, 11, 44, 10) != kRingCorner || RingKind(43, 11, 45, 11) != kRingOut)
        Fail("RingKind: self / side / corner / out");
    /* ReceiverRingPick is retired (T-650 fold 1): the giver offers by the world server's loaded map - t_t650_area_receiver_order */
}
void t_a1b2_release_step()
{
    using namespace cooplo;
    /* ReleaseStep(haveCandidate, candidatesLeft, myLinkUp, tableFresh, candidateInWorld, candidateClockSec, defers, sinceSend) */
    if (ReleaseStep(0, 2, 0, 1, 0, 0.0, 0, 0.0) != kRsHold) Fail("3.4: this game's link down - not held (owner 334 a)");
    if (ReleaseStep(1, 0, 1, 0, 1, 9.0, 0, 9.0) != kRsHold) Fail("3.4: a stale player table - not held (no candidate clock may run) [F5]");
    if (ReleaseStep(0, 2, 1, 1, 0, 0.0, 0, 0.0) != kRsOffer) Fail("3.4: candidates and a road - not offered");
    if (ReleaseStep(0, 0, 1, 1, 0, 0.0, 0, 0.0) != kRsAsleep) Fail("3.4: no candidate left - not asleep");
    if (ReleaseStep(1, 0, 1, 1, 1, 0.5, 0, 0.5) != kRsWait) Fail("3.4: an offer half a second old - not waiting");
    if (ReleaseStep(1, 0, 1, 1, 1, 1.0, 0, 1.0) != kRsResend) Fail("3.4: an unanswered offer 1 s old - not resent");
    if (ReleaseStep(1, 0, 1, 1, 1, 4.99, 0, 0.2) != kRsWait) Fail("3.4: 4.99 s of candidate clock - moved on early");
    if (ReleaseStep(1, 0, 1, 1, 1, 5.0, 0, 0.2) != kRsNext) Fail("3.4: 5 s without an answer - not the next candidate [F5]");
    if (ReleaseStep(1, 0, 1, 1, 0, 0.0, 0, 0.0) != kRsNext) Fail("3.4: the candidate left the world - not the next candidate");
    if (ReleaseStep(1, 0, 1, 1, 1, 0.0, 3, 3.9) != kRsWait) Fail("3.4: a deferred offer re-offered before 4 s");
    if (ReleaseStep(1, 0, 1, 1, 1, 0.0, 3, 4.0) != kRsResend) Fail("3.4: a deferred offer not re-offered at 4 s");
    if (ReleaseStep(1, 0, 1, 1, 1, 0.0, 4, 0.0) != kRsNext) Fail("3.4: deferred more than 3 times - not the next candidate [F5]");
    if (PutAwayDuringFlight(1) != kPadLeaveToAck || PutAwayDuringFlight(0) != kPadCollect) Fail("3.4: a put-away during an XFER flight is that ACK's");
    if (PutAwayRoad(1, 0) != kPadUnloadSameId || PutAwayRoad(1, 1) != kPadUnloadSameId) Fail("T-1097: a same-id withdrawn person's put-away is offered (the other game already runs it) - must unload");
    if (PutAwayRoad(0, 0) != kPadCollect || PutAwayRoad(0, 1) != kPadLeaveToAck) Fail("T-1097: not same-id withdrawn - the flight rule must decide as before");
    if (FlightPutAwayRoad(1, 1) != kFprUnloadSameId || FlightPutAwayRoad(1, 0) != kFprUnloadSameId) Fail("T-1097: a same-id withdrawn person not taken in its XFER is offered at the ACK - its held UNLOAD must go");
    if (FlightPutAwayRoad(0, 1) != kFprRelease || FlightPutAwayRoad(0, 0) != kFprUnloadNoKey) Fail("T-1097: not same-id withdrawn - the flight's release / no-key rule must decide as before");
    if (ReleaseOfferGen(7, 0) != 8u || ReleaseOfferGen(7, 1) != 9u || ReleaseOfferGen(7, 2) != 10u) Fail("2.5 item 6: each later candidate a strictly higher gen [F6]");
    if (coopsquad::PassSendAfterHold(0) != coopsquad::kPassSendSkipEmpty || coopsquad::PassSendAfterHold(1) != coopsquad::kPassSendGo) Fail("kept: the pass's hold (an XFER with every member held is not sent)");
}
void t_a1b2_giver_settle()
{
    using namespace cooplo;
    if (GiverSettleOnAck(kVerdictAdopted, 9, 9, 1) != kSettleAccept) Fail("3.10: the current offer adopted - not accepted");
    if (GiverSettleOnAck(kVerdictAdopted, 8, 9, 1) != kSettleRevoke) Fail("3.10: an earlier candidate adopting late (lower gen) - not revoked [F3]");
    if (GiverSettleOnAck(kVerdictAdopted, 9, 9, 0) != kSettleRevoke) Fail("3.10: adopted after the settle / cancel - not revoked");
    if (GiverSettleOnAck(kVerdictDropped, 9, 9, 1) != kSettleNext) Fail("3.10: dropped - not the next candidate");
    if (GiverSettleOnAck(kVerdictDeferred, 9, 9, 1) != kSettleRetry) Fail("3.10: deferred - not retried later");
    if (GiverSettleOnAck(kVerdictDropped, 8, 9, 1) != kSettleIgnore || GiverSettleOnAck(kVerdictDeferred, 9, 9, 0) != kSettleIgnore) Fail("3.10: a stale drop / defer acted on");
    const unsigned int g[3] = { 8, 10, 10 };
    const int sl[3] = { 1, 4, 2 };
    if (LateAdopterPick(g, sl, 3) != 2) Fail("late adopters: the highest gen, a tie to the lower slot");
    if (LateAdopterPick(g, sl, 0) != -1) Fail("late adopters: none");
    /* RevokeAction(iRunIt, myGen, winnerGen, winnerIsMe, winnerNone) [F4] */
    if (RevokeAction(1, 8, 9, 0, 0) != kRvPuppet) Fail("4b: a late adopter running it at a lower gen - not the winner's puppet");
    if (RevokeAction(1, 8, 9, 0, 1) != kRvRemove) Fail("4b: settled asleep (no winner) - the body not removed");
    if (RevokeAction(0, 8, 9, 0, 0) != kRvIgnore) Fail("4b: not run here - acted on");
    if (RevokeAction(1, 9, 9, 1, 0) != kRvIgnore) Fail("4b: this game is the winner - acted on");
    if (RevokeAction(1, 11, 9, 0, 0) != kRvIgnore) Fail("4b: a newer gen here (a later hand-over reached this game) - revoked");
    if (SquadGivenEffective(1, 0) != 1 || SquadGivenEffective(0, 0) != 0 || SquadGivenEffective(2, 1) != 0 || SquadGivenEffective(0, 3) != 0)
        Fail("3.11: the index's handed-over answer (given and nobody of it asleep here)");
}
void t_a1b2_release_codecs()
{
    using namespace cooplo;
    ReleaseMsg m; m.id = 77; m.keyUid = 501; m.sectorKey = 10u * 64u + 44u; m.flags = kRelFlagPutAway;
    for (unsigned int i = 0; i < 64; ++i) { ReleaseRow r; r.uid = 1000 + i; r.gen = 5 + i; r.body[4] = (unsigned char)i; m.rows.push_back(r); }
    std::vector<char> b;
    if (!ReleaseEncode(&b, m) || b.size() != kReleaseHead + 64 * kReleaseRowSize) Fail("1.3: a full RELEASE is 16 + 64 x 52 bytes");
    ReleaseMsg d;
    if (!ReleaseDecode(&b[0], b.size(), &d) || d.id != 77 || d.keyUid != 501 || d.sectorKey != 10u * 64u + 44u || d.flags != kRelFlagPutAway
        || d.winnerSlot != kRelNoWinner || d.rows.size() != 64 || d.rows[63].uid != 1063 || d.rows[63].gen != 68 || d.rows[5].body[4] != 5)
        Fail("1.3: RELEASE round trip");
    if (ReleaseDecode(&b[0], b.size() - 1, &d) || ReleaseDecode(&b[0], 15, &d)) Fail("1.3: a truncated RELEASE decoded");
    {
        std::vector<char> t(b); t.push_back(0);
        if (ReleaseDecode(&t[0], t.size(), &d)) Fail("1.3: a RELEASE with a trailing byte decoded");
    }
    {
        std::vector<char> t(b); t[11] = (char)65;
        if (ReleaseDecode(&t[0], t.size(), &d)) Fail("1.3: n = 65 decoded");
        t[11] = 0;
        if (ReleaseDecode(&t[0], t.size(), &d)) Fail("1.3: n = 0 decoded");
    }
    m.rows.push_back(ReleaseRow());
    if (ReleaseEncode(&b, m)) Fail("1.3: 65 members encoded");
    ReleaseMsg rv; rv.id = 3; rv.keyUid = 9; rv.flags = kRelFlagRevoke; rv.winnerSlot = 4;
    { ReleaseRow r; r.uid = 9; r.gen = 12; rv.rows.push_back(r); }
    if (!ReleaseEncode(&b, rv) || !ReleaseDecode(&b[0], b.size(), &d) || d.flags != kRelFlagRevoke || d.winnerSlot != 4 || d.rows.size() != 1 || d.rows[0].uid != 9 || d.rows[0].gen != 12)
        Fail("1.3: REVOKE round trip (winner slot, winner gen) [F4]");
    rv.id = 0;
    if (ReleaseEncode(&b, rv)) Fail("1.3: release id 0 encoded");
    ReleaseAckMsg a; a.id = 77; a.adopted.push_back(1); a.adopted.push_back(2); a.dropped.push_back(3); a.deferred.push_back(4);
    if (!ReleaseAckEncode(&b, a) || b.size() != kReleaseAckHead + 16) Fail("1.3: RELEASE_ACK size");
    ReleaseAckMsg e;
    if (!ReleaseAckDecode(&b[0], b.size(), &e) || e.id != 77 || e.adopted.size() != 2 || e.adopted[1] != 2 || e.dropped.size() != 1 || e.dropped[0] != 3
        || e.deferred.size() != 1 || e.deferred[0] != 4) Fail("1.3: RELEASE_ACK round trip");
    if (ReleaseAckDecode(&b[0], b.size() - 4, &e)) Fail("1.3: a short RELEASE_ACK decoded");
    ReleaseAckMsg z; z.id = 5;
    if (!ReleaseAckEncode(&b, z) || !ReleaseAckDecode(&b[0], b.size(), &e) || !e.adopted.empty() || e.id != 5) Fail("1.3: an empty RELEASE_ACK round trip");
    for (unsigned int i = 0; i < 65; ++i) z.dropped.push_back(i);
    if (ReleaseAckEncode(&b, z)) Fail("1.3: a RELEASE_ACK over 64 uids encoded");
    /* MSG_HANDBACK (67) retired: never accepted on the world server's road; 68 / 69 are */
    if (cooplive::LiveInnerAccepted(67u)) Fail("HANDBACK (67) is retired - it is still accepted on the live road");
    if (cooplive::kInnerRelease != 68u || cooplive::kInnerReleaseAck != 69u) Fail("RELEASE / RELEASE_ACK inner types are their transport numbers 68 / 69");
    if (!cooplive::LiveInnerAccepted(cooplive::kInnerRelease) || !cooplive::LiveInnerAccepted(cooplive::kInnerReleaseAck)) Fail("RELEASE / RELEASE_ACK are not accepted on the live road");
}
/* the design's scenarios (b) and (c), driven by the pure functions only */
void t_a1b2_scenarios()
{
    using namespace cooplo;
    /* (b) T810 R3 / CORNER LEG, re-read under owner decision 580 (T-650 fold 1): A (slot 0) puts away a squad standing in 44,10; B (slot 1)
       stands at 43,11, so 44,10 is a corner of B's 3x3. The corner no longer decides: B is offered it exactly when the world server's
       loaded map shows 44,10 loaded on B. */
    AreaRow rows[2];
    rows[0].slot = 0; rows[0].inWorld = 1; rows[0].loaded = 0;
    rows[1].slot = 1; rows[1].inWorld = 1; rows[1].loaded = 0;
    int order[4];
    const int n = AreaReceiverOrder(rows, 2, 1, 0, 0, 0, 0, order, 4, 0);
    if (n != 0) Fail("scenario b: offered to a game without the area loaded");
    if (ReleaseStep(0, n, 1, 1, 0, 0.0, 0, 0.0) != kRsAsleep) Fail("scenario b: no candidate - the squad must sleep in A's world data (no hand-back)");
    rows[1].loaded = 1;
    if (AreaReceiverOrder(rows, 2, 1, 0, 0, 0, 0, order, 4, 0) != 1 || order[0] != 1) Fail("scenario b: B has the area loaded - not offered");
    if (AdoptDecide(1, 1, 0, 3, ReleaseOfferGen(3, 0), 1, 0, 1, 0, 0) != kAdopt) Fail("scenario b: B adopts (no corner refusal)");
    /* B's engine later puts it away: 44,10 is loaded on neither game -> asleep at B, zero hand-back */
    rows[1].loaded = 0;
    if (AreaReceiverOrder(rows, 2, 1, 0, 1, 0, 0, order, 4, 0) != 0) Fail("scenario b: B's put-away found a candidate (A does not have 44,10 loaded)");
    /* (c) three games: candidate 1 silent for 5 s, candidate 2 adopts, candidate 1 then adopts late */
    const unsigned int base = 6, g1 = ReleaseOfferGen(base, 0), g2 = ReleaseOfferGen(base, 1);
    if (ReleaseStep(1, 0, 1, 1, 1, 5.0, 0, 0.5) != kRsNext) Fail("scenario c: silent candidate 1 kept past 5 s");
    if (GiverSettleOnAck(kVerdictAdopted, g2, g2, 1) != kSettleAccept) Fail("scenario c: candidate 2 not accepted");
    if (GiverSettleOnAck(kVerdictAdopted, g1, g2, 0) != kSettleRevoke) Fail("scenario c: candidate 1's late adoption not revoked");
    if (RevokeAction(1, g1, g2, 0, 0) != kRvPuppet) Fail("scenario c: the revoked game must become the winner's puppet, never remove its body [F4]");
    if (!(g2 > g1)) Fail("scenario c: the later adopter holds the higher gen - every game resolves the pair the same way");
    /* (c') candidate 2 drops instead: the late adopter (candidate 1) is accepted rather than the sleep */
    const unsigned int lg[1] = { g1 };
    const int ls[1] = { 1 };
    if (GiverSettleOnAck(kVerdictDropped, g2, g2, 1) != kSettleNext || ReleaseStep(0, 0, 1, 1, 0, 0.0, 0, 0.0) != kRsAsleep || LateAdopterPick(lg, ls, 1) != 0)
        Fail("scenario c': no candidate left with a late adopter - it is accepted instead of the sleep");
}
void t_a1b2f2_fold2()   /* fold 2 of A1 build 2 (re-check 2026-10-02) [a1b2f2-t0] */
{
    using namespace cooplo;
    /* G1 - an adoption after this game's engine re-woke the squad: the sweep's area rule (decision 37) decides which copy lives */
    if (AdoptAfterRewake(1, 1) != kSettleAccept) Fail("G1: re-woken here, the adopter holds the area - REVOKED: the adopter removes its copy and the orphan clean-up deletes the re-woken body (nobody runs the person)");
    if (AdoptAfterRewake(1, 0) != kSettleRevoke) Fail("G1: re-woken here, this game holds the area - accepted (the sweep adopts the re-woken body too: two copies)");
    if (AdoptAfterRewake(1, -1) != kSettleAccept) Fail("G1: re-woken here, the area's holder unknown - REVOKED (a possible loss where a duplicate can still be settled)");
    for (int h = -1; h <= 1; ++h)
    {
        if (AdoptAfterRewake(0, h) != kSettleAccept) Fail("G1: not re-woken here - an adoption refused");
        /* the same read as the sweep: REVOKE exactly when the sweep adopts the re-woken body here (coopor::AnnounceArea adoptable) */
        if ((AdoptAfterRewake(1, h) == kSettleRevoke) != (coopor::AnnounceArea(h) == coopor::kAnnounceAdoptable))
            Fail("G1: the decision disagrees with the sweep's area rule - both copies deleted, or both kept by design");
    }
    if (RevokeAction(1, 9, 11, 0, 1) != kRvRemove) Fail("G1: the REVOKE sent when this game holds the area must remove the adopter's copy");
}
void t_a1b2f1_fold1()   /* fold 1 of A1 build 2 (review 2026-10-02) [a1b2f1-t0] */
{
    using namespace cooplo;
    /* F2 - the retired t_m7a3f4_take_back_keeps_mark rule, restored: a re-woken person of a squad with people only here drops the WHOLE entry */
    if (SquadRebuiltDropsEntry(2, 0, 0) != 1) Fail("F2: a re-woken person of a squad with asleep-here people - the whole entry kept (they would be deleted as orphans)");
    if (SquadRebuiltDropsEntry(0, 1, 0) != 1) Fail("F2: a kept-here person - the whole entry kept");
    if (SquadRebuiltDropsEntry(0, 0, 1) != 1) Fail("F2: an offer this re-wake cancelled - the whole entry kept");
    if (SquadRebuiltDropsEntry(0, 0, 0) != 0) Fail("F2: every person given - the entry dropped (the other game's people would be run twice)");
    /* F5 - the whole-squad consumers act only when every known person is given */
    if (SquadWholeGiven(3, 0, 0, 0, 0) != 1) Fail("F5: a wholly given squad - not handed over");
    if (SquadWholeGiven(3, 2, 0, 0, 0) != 0 || SquadWholeGiven(3, 0, 1, 0, 0) != 0 || SquadWholeGiven(3, 0, 0, 1, 0) != 0 || SquadWholeGiven(3, 0, 0, 0, 1) != 0)
        Fail("F5: a partly given squad (asleep / kept / open / live here) treated as handed over - its own people refused and thrown away");
    if (SquadWholeGiven(0, 0, 0, 0, 0) != 0) Fail("F5: nothing given - handed over");
    for (int g = 0; g < 4; ++g)
        for (int a = 0; a < 3; ++a)
            if (SquadWholeGiven(g, a, 0, 0, 0) != SquadGivenEffective(g, a)) Fail("F5: with nothing kept / open / live the rule must equal SquadGivenEffective");
    /* F3 */
    if (AdoptAfterRewake(1, 0) != kSettleRevoke || AdoptAfterRewake(0, 0) != kSettleAccept) Fail("F3: an adoption after this game's engine re-woke the squad (this game holds the area) - accepted");   /* [a1b2f2-t2] */
    if (RevokeAction(1, 9, 11, 0, 1) != kRvRemove) Fail("F3: the re-wake REVOKE (no winner, the settle gen above the offer) must remove the adopter's copy");
    /* F4 */
    if (RevokeWhen(1) != kRvNowDefer || RevokeWhen(0) != kRvNowApply) Fail("F4: a REVOKE read while engine writes are blocked - not deferred (dropped)");
    /* F6 */
    if (ReleaseFlagsValid(kRelFlagPutAway) != 1 || ReleaseFlagsValid(kRelFlagRevoke) != 1) Fail("F6: a valid flag refused");
    if (ReleaseFlagsValid(0) != 0 || ReleaseFlagsValid(3) != 0 || ReleaseFlagsValid(4) != 0) Fail("F6: flags 0 / both / unknown accepted");
    ReleaseMsg m; m.id = 5; m.keyUid = 7; m.flags = kRelFlagPutAway;
    { ReleaseRow r; r.uid = 7; r.gen = 3; m.rows.push_back(r); }
    std::vector<char> b; ReleaseMsg d;
    if (!ReleaseEncode(&b, m) || !ReleaseDecode(&b[0], b.size(), &d)) Fail("F6: a valid RELEASE refused");
    else
    {
        { std::vector<char> t(b); t[10] = 0; if (ReleaseDecode(&t[0], t.size(), &d)) Fail("F6: flags 0 decoded (taken as an offer)"); }
        { std::vector<char> t(b); t[10] = 4; if (ReleaseDecode(&t[0], t.size(), &d)) Fail("F6: an unknown flag decoded"); }
        { std::vector<char> t(b); t[10] = 3; if (ReleaseDecode(&t[0], t.size(), &d)) Fail("F6: both flags decoded"); }
        { std::vector<char> t(b); t[14] = 1; if (ReleaseDecode(&t[0], t.size(), &d)) Fail("F6: a set reserved u16 decoded"); }
    }
}
/* fold 3 of A1 build 2: an UNLOAD says why it was sent - only "the owner runs this person nowhere" takes it out of the squad index's
   `given`; a context platoon's wake is refused unless its last sleep held a person this game runs. [a1b2f3-t0] */
void t_a1b2f3_fold3()
{
    using namespace cooplo;
    std::vector<char> b; WithdrawMsg w, wo; w.uid = 175; w.seq = 9; w.gen = 3; w.sectorKey = 715; w.expect.push_back(4);
    w.why = kWdWhyAnnounce;
    if (!WithdrawEncode(&b, w) || b.size() != 18 || (unsigned char)b[15] != (unsigned char)kWdWhyAnnounce || !WithdrawDecode(&b[0], b.size(), &wo) || wo.why != (unsigned int)kWdWhyAnnounce
        || wo.uid != 175 || wo.expect.size() != 1) Fail("H059: an announce-pass UNLOAD did not round-trip its reason");
    w.why = kWdWhyGone;
    if (!WithdrawEncode(&b, w) || !WithdrawDecode(&b[0], b.size(), &wo) || wo.why != (unsigned int)kWdWhyGone) Fail("H059: a runs-it-nowhere UNLOAD did not round-trip its reason");
    w.why = kWdWhyNone;
    if (!WithdrawEncode(&b, w) || !WithdrawDecode(&b[0], b.size(), &wo) || wo.why != (unsigned int)kWdWhyNone) Fail("H059: a DESPAWN (no reason) did not round-trip");
    { std::vector<char> t(b); t[15] = 3; if (WithdrawDecode(&t[0], t.size(), &wo)) Fail("H059: an unknown reason decoded"); }
    w.why = 3; if (WithdrawEncode(&b, w)) Fail("H059: an unknown reason encoded");
    if (UnloadWhy(1) != (unsigned int)kWdWhyAnnounce || UnloadWhy(0) != (unsigned int)kWdWhyGone) Fail("H059: the sender's reason (the announce pass = the owner still runs the person)");
    if (UnloadClearsGiven(kWdWhyGone) != 1) Fail("H059: put away / reloaded / retired - the owner runs it nowhere: out of given");
    if (UnloadClearsGiven(kWdWhyAnnounce) != 0) Fail("H059 (T827): an announce-pass UNLOAD cleared given - the giver re-wakes people another game still runs");
    if (UnloadClearsGiven(kWdWhyNone) != 0) Fail("H059: an UNLOAD with no reason cleared given");
    if (UnloadClearsGiven(UnloadWhy(1)) != 0 || UnloadClearsGiven(UnloadWhy(0)) != 1) Fail("H059: sender and receiver disagree");
    if (coopsquad::ContextWake(0, 0, 1) != coopsquad::kCtxWakeGo) Fail("H060: a squad that is not a context platoon - its wake goes on as before");
    if (coopsquad::ContextWake(1, 0, 1) != coopsquad::kCtxWakeRefuse) Fail("H060 (T827 44,10): a context platoon of another game's people - its wake refused, whoever holds the area");
    if (coopsquad::ContextWake(1, 1, 1) != coopsquad::kCtxWakeGo) Fail("H060: a context platoon whose last sleep held a person this game runs (a take-back) - never refused");
    if (coopsquad::ContextWake(1, 1, 0) != coopsquad::kCtxWakeGo) Fail("H060: a take-back context platoon with no id - goes on");
    if (coopsquad::ContextWake(1, 0, 0) != coopsquad::kCtxWakeNoId) Fail("H060: no squad id - nothing to queue, counted");
    /* the wake rule reads the sleep rule's own answer: refused at the wake exactly when the last sleep was refused as another game's people */
    const int sleeps[4][2] = { { 21, 0 }, { 0, 0 }, { 5, 1 }, { 5, 5 } };
    for (int i = 0; i < 4; ++i)
    {
        const int heldMine = coopsquad::ContextSleepWrite(1, 0, sleeps[i][0], sleeps[i][1]) == coopsquad::kCtxSleepGoContextWithMine ? 1 : 0;
        if ((coopsquad::ContextWake(1, heldMine, 1) == coopsquad::kCtxWakeRefuse) != (coopsquad::ContextSleepWrite(1, 0, sleeps[i][0], sleeps[i][1]) == coopsquad::kCtxSleepRefuseContext))
            Fail("H060: the wake and sleep rules disagree about a context platoon");
    }
    /* H067 (T954 A, 101 wakes 5 s apart): a context platoon that sleeps holding none of this game's people is removed at that sleep -
       exactly the platoons whose next wake the wake rule would refuse */
    for (int i = 0; i < 4; ++i)
    {
        const int heldMine = coopsquad::ContextSleepWrite(1, 0, sleeps[i][0], sleeps[i][1]) == coopsquad::kCtxSleepGoContextWithMine ? 1 : 0;
        if (coopsquad::ContextSleepDrop(1, heldMine) != (coopsquad::ContextWake(1, heldMine, 1) == coopsquad::kCtxWakeRefuse ? 1 : 0))
            Fail("H067: the sleep removal and the wake rule disagree about a context platoon");
    }
    if (coopsquad::ContextSleepDrop(1, 0) != 1) Fail("H067 (T954 A 234.737): a player's emptied squad copy went to sleep and was kept - the engine wakes it every 5 s");
    if (coopsquad::ContextSleepDrop(1, 1) != 0) Fail("H067: a context platoon holding a person this game runs (a take-back) was removed");
    if (coopsquad::ContextSleepDrop(0, 0) != 0) Fail("H067: a squad that is not a context platoon was removed at its sleep");
    /* T-1229 (run T1944 A 597.915): the adoption sweep's answer for a rebuilt body of a context platoon */
    if (coopsquad::ContextSweepAdopt(0, 0, 0, 0, 0) != coopsquad::kCtxSweepGo) Fail("T-1229: a squad that is not a context platoon - the sweep's rules as before");
    if (coopsquad::ContextSweepAdopt(1, 0, 5, 5, 0) != coopsquad::kCtxSweepRefuse) Fail("T-1229: a context platoon of another game's people - its rebuilt body adopted (a second copy)");
    if (coopsquad::ContextSweepAdopt(1, 1, 5, 5, 5) != coopsquad::kCtxSweepTakeBack) Fail("T-1229 (T1944 A 371.546 known=5 mine=5): a taken-over group slept here holding only this game's people - its rebuilt people refused on return");
    if (coopsquad::ContextSweepAdopt(1, 1, 5, 5, 1) != coopsquad::kCtxSweepRefuse) Fail("T-1229: a mixed sleep (one of this game's, four of another's) - every rebuilt body adopted, a second copy of the other game's people");
    if (coopsquad::ContextSweepAdopt(1, 1, 5, 4, 4) != coopsquad::kCtxSweepRefuse) Fail("T-1229: a sleep with a member whose uid did not resolve - adopted as all this game's");
    if (coopsquad::ContextSweepAdopt(1, 1, 0, 0, 0) != coopsquad::kCtxSweepRefuse) Fail("T-1229: no counts kept for the sleep - adopted");
    if (coopsquad::ContextSweepAdopt(1, 0, 5, 5, 5) != coopsquad::kCtxSweepRefuse) Fail("T-1229: counts without the wake rule's held-mine fact - adopted");
    /* a take-back at the sweep is always a platoon the wake rule lets wake and the sleep rule keeps */
    for (int i = 0; i < 4; ++i)
    {
        const int heldMine = coopsquad::ContextSleepWrite(1, 0, sleeps[i][0], sleeps[i][1]) == coopsquad::kCtxSleepGoContextWithMine ? 1 : 0;
        if (coopsquad::ContextSweepAdopt(1, heldMine, sleeps[i][0], sleeps[i][0], sleeps[i][1]) == coopsquad::kCtxSweepTakeBack
            && (coopsquad::ContextWake(1, heldMine, 1) != coopsquad::kCtxWakeGo || coopsquad::ContextSleepDrop(1, heldMine) != 0))
            Fail("T-1229: the sweep takes back a context platoon the wake rule refuses or the sleep removes");
    }
    using namespace coopsquad;
    if (ContextDropStep(0, 0, 1, 0, 1, 0) != kCtxDropCancel) Fail("H067: a platoon no longer in the context map (superseded, retired, hired from) was removed");
    if (ContextDropStep(1, 1, 1, 0, 1, 0) != kCtxDropCancel) Fail("H067: a context platoon whose last sleep held a person this game runs was removed");
    if (ContextDropStep(1, 0, 0, 0, 0, 0) != kCtxDropRetry) Fail("H067: an unreadable platoon was acted on");
    if (ContextDropStep(1, 0, 1, 0, 1, 0) != kCtxDropDestroy) Fail("H067 (T954 A): the asleep context platoon in a stand-in faction was not destroyed");
    if (ContextDropStep(1, 0, 1, 0, 0, 0) != kCtxDropRetry) Fail("H067: an asleep platoon not found in its own faction's sleeping list was destroyed");
    if (ContextDropStep(1, 0, 1, 1, 0, 0) != kCtxDropRetire) Fail("H067: a refused wake's awake body was left standing");
    if (ContextDropStep(1, 0, 1, 1, 0, 1) != kCtxDropKeepLive) Fail("H067: an announcement's live copy in a reused context platoon was removed");
    if (ContextDropStep(1, 0, 1, 1, 1, 2) != kCtxDropKeepLive) Fail("H067: a reused context platoon with copies was removed");
    /* a save copy woken beside this game's live copy of the same squad */
    if (WakeDupRefuse(1, 0, 9) != 1) Fail("wake dup: a save copy woken beside an awake copy running 9 of this game's people was woken (its people built twice)");
    if (WakeDupRefuse(1, 0, 1) != 1) Fail("wake dup: one person this game runs in the live copy is enough to refuse");
    if (WakeDupRefuse(1, 0, 0) != 0) Fail("wake dup: a squad with no other awake copy here running this game's people was refused");
    if (WakeDupRefuse(1, 0, -1) != 0) Fail("wake dup: an unreadable other copy refused the wake");
    if (WakeDupRefuse(0, 0, 9) != 0) Fail("wake dup: a player's, peer's or stand-in squad was refused");
    if (WakeDupRefuse(1, 1, 9) != 0) Fail("wake dup: an area record was refused");
    /* the refused copy rides the removal road as still to be removed: awake with no registered member it is put on ice and destroyed */
    if (ContextDropStep(1, 0, 1, 1, 0, 0) != kCtxDropRetire) Fail("wake dup: the refused copy's awake body was left standing");
    /* a context platoon emptied because the game running its people restarted and answered NOT-LIVE: re-created from its record */
    if (ContextRecreateAfterRunnerRestart(0, 5000, 1, 0, 0) != 1) Fail("runner restart: a squad whose runner answered NOT-LIVE 5 s ago, with its record written by that runner, was not re-created");
    if (ContextRecreateAfterRunnerRestart(1, 5000, 1, 0, 0) != 0) Fail("runner restart: a refused duplicate wake was re-created");
    if (ContextRecreateAfterRunnerRestart(0, -1, 1, 0, 0) != 0) Fail("runner restart: a squad with no NOT-LIVE note was re-created (its people still arrive by SPAWN)");
    if (ContextRecreateAfterRunnerRestart(0, kCtxRecreateNoteMs + 1, 1, 0, 0) != 0) Fail("runner restart: a note older than the window was used");
    if (ContextRecreateAfterRunnerRestart(0, kCtxRecreateNoteMs, 1, 0, 0) != 1) Fail("runner restart: a note at the window's edge was refused");
    if (ContextRecreateAfterRunnerRestart(0, 0, 1, 2, 2) != 1) Fail("runner restart: a note just made was refused");
    if (ContextRecreateAfterRunnerRestart(0, 5000, 0, 0, 0) != 0) Fail("runner restart: a squad with no record was re-created");
    if (ContextRecreateAfterRunnerRestart(0, 5000, 1, 1, 0) != 0) Fail("runner restart: a record written by another game than the one that answered NOT-LIVE was used");
    if (ContextRecreateAfterRunnerRestart(0, 5000, 1, -1, 0) != 0) Fail("runner restart: a record with an unknown writer was used");
    if (ContextRecreateAfterRunnerRestart(0, 5000, 1, -1, -1) != 0) Fail("runner restart: an unknown answering slot matched an unknown writer");
}
/* ---- M7a A1 BUILD 1 (owner decision 347 a; src/common/liveowner.h; design .modding/investigations/m7a-a1-design-2026-10-02.md sec. 3) [a1b1-t0] ---- */
void t_a1b1_world_first_route()
{
    using namespace cooplo;
    Road r = WorldFirstRoute(4, true, 1, true, 4);
    if (r.road != kRoadLive || r.slot != 4) Fail("3.1: a ready notebook link and an IN_WORLD target did not go LIVE SLOT (the session link was preferred)");
    if (WorldFirstRoute(4, true, 0, true, 4).road != kRoadNone) Fail("3.1: a target not in the world was sent (or fell back to the session link while the notebook is up)");
    if (WorldFirstRoute(4, true, -1, true, 4).road != kRoadNone) Fail("3.1: with no roster the message went");
    r = WorldFirstRoute(4, false, -1, true, 4);
    if (r.road != kRoadSession || r.slot != 4) Fail("3.1: the notebook down, the session peer as target - not the session link");
    if (WorldFirstRoute(5, false, -1, true, 4).road != kRoadNone) Fail("3.1: the notebook down and a target that is not the session peer still went");
    if (WorldFirstRoute(4, false, -1, false, 4).road != kRoadNone) Fail("3.1: both links down and a road was given");
    if (WorldFirstRoute(-1, true, 1, true, -1).road != kRoadNone) Fail("3.1 [review F11]: no slot and a road was given");
}
void t_a1b1_fold1()   /* fold 1 of A1 build 1 (review 2026-10-02) [a1b1f1-t0] */
{
    using namespace cooplo;
    /* F1 - the XFER_ACK's road */
    if (XferAckRoad(1, 1, 4, kRoadLive, 1, 4) != kRoadSession) Fail("fold 1 F1: an XFER that came on the session link was answered by slot (a one-sided outage loses every ACK)");
    if (XferAckRoad(0, 1, 4, kRoadLive, 1, 4) != kRoadLive) Fail("fold 1 F1: a relayed XFER was not answered by slot");
    if (XferAckRoad(0, 1, 4, kRoadNone, 1, 4) != kRoadSession) Fail("fold 1 F1: no road by slot, the session link up to that game - not sent on it");
    if (XferAckRoad(0, 1, 5, kRoadNone, 1, 4) != kRoadNone) Fail("fold 1 F1: no road by slot to a game that is not the session peer - sent anyway");
    if (XferAckRoad(1, 1, 4, kRoadLive, 0, 4) != kRoadLive) Fail("fold 1 F1: the session link down since the XFER - the slot road was not used");
    if (XferAckRoad(1, 0, -1, kRoadNone, 1, -1) != kRoadSession) Fail("fold 1 F1: a session peer with no slot yet was not answered on the session link");
    if (XferAckRoad(0, 0, -1, kRoadNone, 0, -1) != kRoadNone) Fail("fold 1 F1: both links down and a road was given");
    if (XferAckRoad(0, 1, -1, kRoadNone, 1, -1) != kRoadNone) Fail("fold 1 F1: a slot key with no slot went on the session link");
    /* F3 - the area key changed counts as changed */
    if (LeadAnnounceDue(1, 1, 7, 7) != 0) Fail("fold 1 F3: an unchanged squad in the same area was announced again");
    if (LeadAnnounceDue(1, 1, 7, 8) != 1) Fail("fold 1 F3: a squad that walked into another area was not announced there");
    if (LeadAnnounceDue(1, 1, -1, 8) != 1) Fail("fold 1 F3: a squad first readable in an area after a WORLD announcement was not announced there");
    if (LeadAnnounceDue(0, 1, 7, 7) != 1 || LeadAnnounceDue(1, 0, 7, 7) != 1) Fail("fold 1 F3: nothing sent / changed content was not due");
    /* F5 - the per-sector re-announce */
    const int ks[3] = { 3, 9, 12 };
    if (LeadReannounceThis(9, ks, 3) != 1) Fail("fold 1 F5: a squad in an asked sector was not re-announced");
    if (LeadReannounceThis(4, ks, 3) != 0) Fail("fold 1 F5: a squad outside the asked sectors was re-announced");
    if (LeadReannounceThis(-1, ks, 3) != 0) Fail("fold 1 F5: a WORLD announcement was re-sent");
    if (LeadReannounceThis(9, ks, 0) != 0) Fail("fold 1 F5: an ask with no sectors re-announced");
    /* F4 - the holder's rebuild gate */
    if (RosterSectorsNeeded(1.0, 0, 0, kRosterSec) != 0) Fail("fold 1 F4: hashes rebuilt with no CHECK due");
    if (RosterSectorsNeeded(kRosterSec, 0, 0, kRosterSec) != 1 || RosterSectorsNeeded(1.0, 1, 0, kRosterSec) != 1 || RosterSectorsNeeded(1.0, 0, 1, kRosterSec) != 1)
        Fail("fold 1 F4: a due owner / an edge / a queued re-check did not rebuild");
    /* F8 - only a FULL CHECK closes a receipt row */
    if (ReceiptClosedByCheck(kRosterAllSectors, 0) != 1) Fail("fold 1 F8: a full CHECK not listing the uid did not close its row");
    if (ReceiptClosedByCheck(kRosterAllSectors, 1) != 0) Fail("fold 1 F8: a full CHECK listing the uid closed its row");
    if (ReceiptClosedByCheck(12, 0) != 0) Fail("fold 1 F8: a per-sector CHECK closed a row");
    if (ReceiptClosedByCheck(kRosterListed, 0) != 0) Fail("fold 1 F8: a listed-uids CHECK closed a row");
    /* F10 - an ANSWER's slot */
    if (AnswerSlotOk(kAnsMoved, 7, 255) != 1) Fail("fold 1 F10: a MOVED row to a live slot was refused");
    if (AnswerSlotOk(kAnsMoved, 256, 255) != 0 || AnswerSlotOk(kAnsMoved, 0xFFFF, 255) != 0) Fail("fold 1 F10: a MOVED row past the last slot was taken");
    if (AnswerSlotOk(kAnsMoved, 0xFFFF, 65535) != 0) Fail("fold 1 F10 re-check: a MOVED row naming slot 0xFFFF (no slot) was taken at the real limit");
    if (AnswerSlotOk(kAnsNotLive, 0xFFFF, 255) != 1 || AnswerSlotOk(kAnsLive, 0xFFFF, 255) != 1) Fail("fold 1 F10: a row that does not use its slot was refused");
}
void t_a1b1_roster_hash()
{
    using namespace cooplo;
    const unsigned int u[3] = { 100, 200, 300 }, g[3] = { 1, 2, 3 }, u2[3] = { 300, 100, 200 }, g2[3] = { 3, 1, 2 };
    const u64 h = RosterHashOf(u, g, 3);
    if (h != RosterHashOf(u2, g2, 3)) Fail("3.5: the hash depends on the order");
    u64 k = 0;
    for (int i = 0; i < 3; ++i) k = RosterHashStep(k, u[i], 0, g[i], 0, 1);
    if (k != h) Fail("3.5: the incremental hash differs from the whole one");
    if (RosterHashStep(h, 200, 2, 5, 1, 1) == h) Fail("3.5: a gen change left the hash unchanged");
    const unsigned int u3[2] = { 100, 300 }, g3[2] = { 1, 3 };
    if (RosterHashStep(h, 200, 2, 0, 1, 0) != RosterHashOf(u3, g3, 2)) Fail("3.5: XOR-ing a row out did not give the hash without it");
    if (RosterRowHash(100, 1) == RosterRowHash(100, 2) || RosterRowHash(100, 1) == RosterRowHash(101, 1)) Fail("3.5: two different rows hash alike");
}
void t_a1b1_roster_check_due()
{
    using namespace cooplo;
    if (RosterCheckDue(0, 1, 100.0, 1, 0, kRosterSec, kRosterQuietSec) != 0) Fail("3.6: a CHECK with no copies of that owner");
    if (RosterCheckDue(3, 0, 0.0, 1, 0, kRosterSec, kRosterQuietSec) != 1) Fail("3.6: a WELCOME edge did not check at once");
    if (RosterCheckDue(3, 1, 4.99, 0, 0, kRosterSec, kRosterQuietSec) != 0) Fail("3.6: two CHECKs of one owner within 5 s");
    if (RosterCheckDue(3, 1, 5.0, 0, 0, kRosterSec, kRosterQuietSec) != 1) Fail("3.6: a differing hash at 5.0 s did not check");
    if (RosterCheckDue(3, 0, 29.9, 0, 0, kRosterSec, kRosterQuietSec) != 0) Fail("3.6: a quiet owner checked before 30 s");
    if (RosterCheckDue(3, 0, 30.0, 0, 0, kRosterSec, kRosterQuietSec) != 1) Fail("3.6: a lost HASH was never covered (30 s quiet)");
    if (RosterCheckDue(0, 0, 5.0, 0, 1, kRosterSec, kRosterQuietSec) != 1) Fail("3.6: a pending re-check (conflict / dual run) was not sent");
    if (RosterCheckIsFull(1, 0.0, kRosterQuietSec) != 1 || RosterCheckIsFull(0, 29.0, kRosterQuietSec) != 0 || RosterCheckIsFull(0, 30.0, kRosterQuietSec) != 1) Fail("3.6: the full CHECK rule");
}
void t_a1b1_roster_answer_for()
{
    using namespace cooplo;
    Answer a = RosterAnswerFor(1, 4, 1, -1, 0, 4);
    if (a.status != kAnsPending) Fail("3.7 [review F1]: a person still being handed on was not answered PENDING");
    if (RosterAnswerFor(1, 4, 0, -1, 0, 4).status != kAnsAgree) Fail("3.7: an agreeing row was sent");
    a = RosterAnswerFor(1, 4, 0, -1, 0, 0);
    if (a.status != kAnsLive || a.gen != 4) Fail("3.7: a copy with an unknown gen was not re-stamped");
    a = RosterAnswerFor(1, 4, 0, -1, 0, 6);
    if (a.status != kAnsLive || a.gen != 4) Fail("3.7: an asker with a higher gen was not told this game's");
    a = RosterAnswerFor(0, 0, 0, 7, 5, 4);
    if (a.status != kAnsMoved || a.slot != 7 || a.gen != 5) Fail("3.7: a person given to slot 7 was not answered MOVED");
    a = RosterAnswerFor(0, 3, 0, -1, 0, 3);
    if (a.status != kAnsNotLive || a.gen != 3) Fail("3.7: a put-away person was not answered NOT-LIVE with its gen");
    a = RosterAnswerFor(0, 0, 0, -1, 0, 3);
    if (a.status != kAnsNotLive || a.gen != 0) Fail("3.7: an unknown person was not answered NOT-LIVE at gen 0");
}
void t_a1b1_roster_apply()
{
    using namespace cooplo;
    if (RosterApply(3, kAnsPending, 3, 0, 1, 0, 0) != kApKeep) Fail("3.8: PENDING did not keep the copy");
    if (RosterApply(0, kAnsLive, 4, 0, 1, 0, 0) != kApRestamp) Fail("3.8: LIVE from the recorded owner did not re-stamp");
    if (RosterApply(3, kAnsLive, 4, 0, 0, 0, 0) != kApRekey) Fail("3.8: LIVE from another game at a higher gen did not re-key");
    if (RosterApply(5, kAnsLive, 4, 0, 1, 0, 0) != kApConflict) Fail("3.8: a lower gen than the copy's was not a conflict");
    if (RosterApply(5, kAnsLive, 4, 0, 1, 0, kRosterConflictMax) != kApRestamp) Fail("3.8 [review F2]: at the cap the recorded owner's word did not stand");
    if (RosterApply(5, kAnsLive, 4, 0, 0, 0, kRosterConflictMax) != kApKeep) Fail("3.8: at the cap another game's lower word moved the copy");
    if (RosterApply(3, kAnsMoved, 4, 1, 0, 0, 0) != kApKeep) Fail("3.8: another game's MOVED naming this game moved the copy");
    if (RosterApply(3, kAnsMoved, 4, 1, 1, 0, 0) != kApWithdraw) Fail("3.8: the recorded owner's MOVED naming this game (which does not run it) left a frozen copy");
    if (RosterApply(3, kAnsMoved, 4, 0, 1, 0, 0) != kApRekey) Fail("3.8: MOVED at a higher gen did not re-key");
    if (RosterApply(5, kAnsMoved, 4, 0, 1, 0, 1) != kApConflict) Fail("3.8: MOVED at a lower gen was not a conflict");
    if (RosterApply(5, kAnsMoved, 4, 0, 1, 0, 2) != kApRekey) Fail("3.8: at the cap the recorded owner's MOVED did not stand");
    if (RosterApply(9, kAnsNotLive, 0, 0, 1, 0, 0) != kApWithdraw) Fail("3.8 [review F2]: the recorded owner's NOT-LIVE did not withdraw whatever the gens");
    if (RosterApply(9, kAnsNotLive, 0, 0, 1, 1, 0) != kApKeep) Fail("3.8: a NOT-LIVE overtaken by that owner's stream withdrew the copy");
    if (RosterApply(2, kAnsNotLive, 3, 0, 0, 0, 0) != kApWithdraw) Fail("3.8: another game's NOT-LIVE at a gen >= the copy's did not withdraw");
    if (RosterApply(4, kAnsNotLive, 3, 0, 0, 0, 0) != kApConflict) Fail("3.8: another game's stale NOT-LIVE was not a conflict");
    if (RosterApply(4, kAnsNotLive, 3, 0, 0, 0, 2) != kApKeep) Fail("3.8: at the cap another game's stale NOT-LIVE withdrew the copy");
}
void t_a1b1_dual_run_and_leader()
{
    using namespace cooplo;
    if (DualRunResolve(5, 4, 9, 1) != kIKeep || DualRunResolve(4, 5, 1, 9) != kIYield) Fail("3.9: the higher gen did not keep the person");
    if (DualRunResolve(4, 4, 1, 9) != kIKeep || DualRunResolve(4, 4, 9, 1) != kIYield) Fail("3.9: a tie did not go to the lower slot");
    if (DualRunResolve(4, 4, -1, 1) != kIYield) Fail("3.9: a game with no slot kept a tied person");
    const unsigned int none[1] = { 0 }, p3[3] = { 0, 70, 50 };
    if (AgreedSquadLeaderN(40, none, 0) != 40 || AgreedSquadLeaderN(40, none, 1) != 40) Fail("3.12: no other announcement changed the leader");
    if (AgreedSquadLeaderN(0, p3, 3) != 50) Fail("3.12: with no own leader the lowest named one was not taken");
    if (AgreedSquadLeaderN(60, p3, 3) != 50 || AgreedSquadLeaderN(45, p3, 3) != 45) Fail("3.12: the fold is not 'named beats none, the lower uid wins'");
}
void t_a1b1_receipts()
{
    using namespace cooplo;
    if (ReceiptExpected(1, 1, 3, kReceiptRadius) != 1 || ReceiptExpected(1, 1, 4, kReceiptRadius) != 0) Fail("3.13: the radius is not 3 sectors");
    if (ReceiptExpected(0, 1, 0, kReceiptRadius) != 0 || ReceiptExpected(-1, 1, 0, kReceiptRadius) != 0 || ReceiptExpected(1, 0, 0, kReceiptRadius) != 0) Fail("3.13: a game not in the world (or with no sector) was expected");
    if (Cheb(43, 11, 46, 11) != 3 || Cheb(44, 10, 43, 11) != 1) Fail("3.13: the Chebyshev distance");
    if (ReceiptAction(0.0, 1, 1, 0.0, 1, 1, 1) != kRcDoneAcked) Fail("3.14: an acknowledged row was not done");
    if (ReceiptAction(0.0, 1, 1, 0.0, 0, 1, 1) != kRcDoneRoster) Fail("3.14: the roster's word did not close the row");
    if (ReceiptAction(0.0, 1, 1, 0.0, 0, 0, 1) != kRcDoneGone) Fail("3.14: a game that left the world kept the row open");
    if (ReceiptAction(1.99, 1, 1, 10.0, 0, 0, 0) != kRcWait || ReceiptAction(2.0, 1, 1, 10.0, 0, 0, 0) != kRcResend) Fail("3.14: the first resend is not at 2 s");
    if (ReceiptAction(3.9, 2, 1, 10.0, 0, 0, 0) != kRcWait || ReceiptAction(4.0, 2, 1, 10.0, 0, 0, 0) != kRcResend) Fail("3.14: the second resend is not at 4 s");
    if (ReceiptAction(7.9, 5, 1, 10.0, 0, 0, 0) != kRcWait || ReceiptAction(8.0, 5, 1, 10.0, 0, 0, 0) != kRcResend) Fail("3.14: later resends are not every 8 s");
    if (ReceiptAction(100.0, 1, 0, 10.0, 0, 0, 0) != kRcWait) Fail("3.14: a resend with no road");
    if (ReceiptAction(8.0, 3, 1, 59.9, 0, 0, 0) != kRcResend || ReceiptAction(8.0, 3, 1, 60.0, 0, 0, 0) != kRcGiveUp) Fail("3.14: the give-up is not at 60 s of road-up time");
    if (WithdrawalStale(5, 4) != 1 || WithdrawalStale(4, 4) != 0 || WithdrawalStale(0, 4) != 0 || WithdrawalStale(5, 0) != 0) Fail("[review c]: the stale-withdrawal rule");
}
void t_a1b1_orphan_copy()
{
    using namespace cooplo;
    if (OrphanCopyAction(1, 100.0, 0, kOrphanCopySec) != kOrphanKeep) Fail("3.16: a copy with an owner record was withdrawn");
    if (OrphanCopyAction(0, 100.0, 1, kOrphanCopySec) != kOrphanKeep) Fail("3.16: a withdrawal while engine writes are blocked");
    if (OrphanCopyAction(0, 19.9, 0, kOrphanCopySec) != kOrphanKeep || OrphanCopyAction(0, 20.0, 0, kOrphanCopySec) != kOrphanWithdraw) Fail("3.16: the limit is not 20 s");
}
void t_a1b1_codecs()
{
    using namespace cooplo;
    std::vector<HashRow> hr; HashRow h; h.sectorKey = 715; h.count = 3; h.hash = 0x1122334455667788ULL; hr.push_back(h);
    std::vector<std::vector<char> > ch; RosterMsg m;
    if (!RosterHashEncode(&ch, 9, hr) || ch.size() != 1 || !RosterDecode(&ch[0][0], ch[0].size(), &m) || m.kind != kRosterHash || m.seq != 9 || m.lastChunk != 0
        || m.hashes.size() != 1 || m.hashes[0].sectorKey != 715 || m.hashes[0].count != 3 || m.hashes[0].hash != h.hash) Fail("3.15: a ROSTER HASH did not round-trip");
    if (RosterDecode(&ch[0][0], ch[0].size() - 1, &m)) Fail("3.15: a truncated HASH decoded");
    std::vector<char> bad(ch[0]); bad.push_back(0);
    if (RosterDecode(&bad[0], bad.size(), &m)) Fail("3.15: a HASH with a trailing byte decoded");
    bad = ch[0]; bad[0] = 9;
    if (RosterDecode(&bad[0], bad.size(), &m)) Fail("3.15: an unknown ROSTER kind decoded");
    std::vector<CheckRow> cr(6001); for (size_t i = 0; i < cr.size(); ++i) { cr[i].uid = (unsigned int)i + 1; cr[i].gen = 2; }
    if (!RosterCheckEncode(&ch, 77, 715, cr) || ch.size() != 2) Fail("3.15 [review F8]: 6,001 CHECK rows are not two chunks");
    for (size_t c = 0; c < ch.size(); ++c)
    {
        if (ch[c].size() >= kEnvelopeCap) Fail("3.15 [review F8]: a CHECK chunk is not under 65,536 bytes");
        if (!RosterDecode(&ch[c][0], ch[c].size(), &m) || m.kind != kRosterCheck || m.seq != 77 || m.sectorKey != 715 || m.chunk != c || m.lastChunk != 1) Fail("3.15: a CHECK chunk did not round-trip");
    }
    if (m.checks.size() != 1 || m.checks[0].uid != 6001 || m.checks[0].gen != 2) Fail("3.15: the last CHECK chunk lost its row");
    std::vector<AnswerRow> ar(5001); for (size_t i = 0; i < ar.size(); ++i) { ar[i].uid = (unsigned int)i + 1; ar[i].status = kAnsNotLive; ar[i].gen = 4; }
    ar[0].status = kAnsMoved; ar[0].slot = 12;
    if (!RosterAnswerEncode(&ch, 77, 5, ar) || ch.size() != 2 || ch[0].size() >= kEnvelopeCap) Fail("3.15 [review F8]: 5,001 ANSWER rows are not two chunks under the cap");
    if (!RosterDecode(&ch[0][0], ch[0].size(), &m) || m.kind != kRosterAnswer || m.rosterSeq != 5 || m.answers[0].status != kAnsMoved || m.answers[0].slot != 12 || m.answers[1].gen != 4) Fail("3.15: an ANSWER did not round-trip");
    bad = ch[0]; bad[20 + 4] = 9;
    if (RosterDecode(&bad[0], bad.size(), &m)) Fail("3.15: an ANSWER with an unknown status decoded");
    std::vector<AnswerRow> none;
    if (!RosterAnswerEncode(&ch, 78, 5, none) || ch.size() != 1 || !RosterDecode(&ch[0][0], ch[0].size(), &m) || !m.answers.empty()) Fail("3.15: the empty 'all agree' ANSWER");
    bad = ch[0]; bad[2] = 1;   /* chunk 1 of lastChunk 0 */
    if (RosterDecode(&bad[0], bad.size(), &m)) Fail("3.15: a chunk past lastChunk decoded");
    std::vector<unsigned int> seqs, back; seqs.push_back(5); seqs.push_back(6);
    std::vector<char> b;
    if (!ReceiptEncode(&b, seqs) || b.size() != 12 || !ReceiptDecode(&b[0], b.size(), &back) || back != seqs) Fail("3.15: a RECEIPT did not round-trip");
    if (ReceiptDecode(&b[0], b.size() - 1, &back) || ReceiptEncode(&b, std::vector<unsigned int>())) Fail("3.15: a short or empty RECEIPT");
    WithdrawMsg w, wo; w.uid = 175; w.seq = 9; w.gen = 3; w.sectorKey = 715; w.expect.push_back(4); w.expect.push_back(7);
    if (!WithdrawEncode(&b, w) || b.size() != 20 || !WithdrawDecode(&b[0], b.size(), &wo) || wo.uid != 175 || wo.seq != 9 || wo.gen != 3 || wo.sectorKey != 715
        || wo.expect.size() != 2 || !WithdrawListsSlot(wo, 7) || WithdrawListsSlot(wo, 5)) Fail("3.15 [review c]: an UNLOAD / DESPAWN did not round-trip");
    if (WithdrawDecode(&b[0], 4, &wo) || WithdrawDecode(&b[0], b.size() - 1, &wo)) Fail("3.15: the 4-byte (or a truncated) withdrawal decoded");
    unsigned int u = 0, nw = 0, pv = 0, g = 0;
    OwnerMovedEncode16(&b, 77, 5, 3, 8);
    if (b.size() != 16 || !OwnerMovedDecode16(&b[0], b.size(), &u, &nw, &pv, &g) || u != 77 || nw != 5 || pv != 3 || g != 8 || !OwnerMovedRoadOk(&b[0], b.size())) Fail("1.3: OWNER_MOVED 16 did not round-trip");
    if (OwnerMovedDecode16(&b[0], 12, &u, &nw, &pv, &g) || OwnerMovedRoadOk(&b[0], 8)) Fail("1.3: an 8 / 12-byte OWNER_MOVED passed");
    if (OwnerMovedStale(5, 4) != 1 || OwnerMovedStale(4, 4) != 0) Fail("1.3: the stale OWNER_MOVED rule");
    if (!XferLengthOk(12 + 52, 1) || XferLengthOk(12 + 48, 1) || !XferLengthOk(12 + 64 * 52, 64) || XferLengthOk(12 + 65 * 52, 65) || XferLengthOk(12, 0)) Fail("1.3: the XFER length rule (the gen tail required)");
    if (XferGenAt(2) != 12 + 96 || GenTake(3) != 4 || GenReadopt(2, 5) != 6 || GenReadopt(0, 0) != 1) Fail("2.1: the generation steps");
    coophire::HireMsg hm; hm.kind = coophire::kHireDone; hm.uid = 5; hm.taken = 1; hm.gen = 9; coophire::HireMsg ho;
    b.clear();
    if (!coophire::EncodeHire(&b, hm) || coophire::DecodeHire(&b[0], b.size(), &ho) != coophire::kHireDecodeOk || ho.gen != 9) Fail("[review F10]: the hire DONE's gen did not round-trip");
    if (coophire::DecodeHire(&b[0], b.size() - 4, &ho) != coophire::kHireDecodeBadLength) Fail("[review F10]: a hire message without its gen tail decoded");
}
/* SCENARIO (a) - T810 Q1: an owner's UNLOADs lost while its world link was dead; the holder's WELCOME-edge CHECK withdraws the copies and
   the owner's outbox closes by the roster. Driven by the pure functions only. */
void t_a1b1_scenario_t810_lost_unloads()
{
    using namespace cooplo;
    const int A = 1, B = 2;
    long long expected = 0, acked = 0, rosterClosed = 0, slotGone = 0, gaveUp = 0;
    for (unsigned int uid = 175; uid <= 189; ++uid)
    {
        ++expected;   /* B stood within 3 sectors at the UNLOAD */
        if (ReceiptAction(30.0, 1, 0, 0.0, 0, 0, 0) != kRcWait) Fail("(a): a resend while A's world link was dead");
        /* B's link came back: its WELCOME edge CHECKs every copy of A */
        if (RosterCheckDue(15, 0, 0.0, 1, 0, kRosterSec, kRosterQuietSec) != 1 || RosterCheckIsFull(1, 0.0, kRosterQuietSec) != 1) Fail("(a): no WELCOME-edge CHECK");
        const Answer a = RosterAnswerFor(0, 1, 0, -1, 0, 1);   /* A put them away: no body, no record of another owner, nothing owed */
        if (a.status != kAnsNotLive) { Fail("(a): A did not answer NOT-LIVE"); return; }
        if (RosterApply(1, a.status, a.gen, 0, 1, 0, 0) != kApWithdraw) Fail("(a): B kept a frozen copy");
        const int act = ReceiptAction(2.0, 1, 1, 5.0, 0, 1, 0);   /* B's next CHECK no longer lists it */
        if (act == kRcDoneAcked) ++acked; else if (act == kRcDoneRoster) ++rosterClosed; else if (act == kRcDoneGone) ++slotGone; else if (act == kRcGiveUp) ++gaveUp;
    }
    if (expected != acked + rosterClosed + slotGone + gaveUp || gaveUp != 0 || rosterClosed != 15) Fail("(a) [review F12]: expected != acked + rosterClosed + slotGone, or a give-up");
    (void)A; (void)B;
}
/* SCENARIO (d) [review F1] - build 1, WELCOME edge with an owed person: the CHECK is answered PENDING, the copy is kept, the owed XFER finds
   it and the taker out-ranks every record. */
void t_a1b1_scenario_welcome_pending()
{
    using namespace cooplo;
    const unsigned int giverGen = 2, copyGen = 2;
    const Answer a = RosterAnswerFor(0, giverGen, 1, -1, 0, copyGen);   /* body put away, owed XFER still to come */
    if (a.status != kAnsPending) Fail("(d): an owed person was answered NOT-LIVE - its copy would go and the owed XFER would meet 'no copy here'");
    if (RosterApply(copyGen, a.status, a.gen, 0, 1, 0, 0) != kApKeep) Fail("(d): the PENDING copy was not kept");
    if (GenTake(giverGen) <= copyGen) Fail("(d): the owed XFER's taker does not out-rank the copy records");
}
/* SCENARIO (g) - a restarted owner (no generation table: gen 0) answers NOT-LIVE: its word withdraws the copy it is recorded for
   whatever the copy's gen [review F2]; the same NOT-LIVE from a game that is NOT the recorded owner is a bounded conflict. */
void t_a1b1_scenario_restarted_owner_gen0()
{
    using namespace cooplo;
    const Answer a = RosterAnswerFor(0, 0, 0, -1, 0, 3);
    if (a.status != kAnsNotLive || a.gen != 0) { Fail("(g): the restarted owner's answer"); return; }
    if (RosterApply(3, a.status, a.gen, 0, 1, 0, 0) != kApWithdraw) Fail("(g) [review F2]: the recorded owner's NOT-LIVE at gen 0 left a frozen copy");
    if (RosterApply(3, a.status, a.gen, 0, 0, 0, 0) != kApConflict) Fail("(g): another game's NOT-LIVE at gen 0 withdrew a gen-3 copy");
}
/* SCENARIO (e) [review F2, a] - a host / join wipe clears the copy records (g_owner, g_copyGen) but not this game's own generations;
   a copy re-claimed by its owner's SPAWN keeps, an unclaimed one goes at 20 s of link-up time. */
void t_a1b1_scenario_wipe_and_orphans()
{
    using namespace cooplo;
    std::map<unsigned int, unsigned int> mine, copy; mine[10] = 4; copy[20] = 2; copy[21] = 2;
    copy.clear();   /* the wipe: g_owner.clear(); g_copyGen.clear() */
    if (mine.size() != 1 || GenReadopt(mine[10], 0) != 5) Fail("(e): this game's own generation did not survive the wipe");
    double t20 = 0.0, t21 = 0.0; int has20 = 0;
    for (int s = 0; s < 25; ++s)
    {
        if (s == 5) has20 = 1;   /* the owner's WELCOME catch-up re-SPAWNs uid 20 */
        t20 = has20 ? 0.0 : t20 + 1.0; t21 += 1.0;
        if (OrphanCopyAction(has20, t20, 0, kOrphanCopySec) != kOrphanKeep) Fail("(e): a re-claimed copy was withdrawn");
        const int act = OrphanCopyAction(0, t21, 0, kOrphanCopySec);
        if ((t21 < kOrphanCopySec) != (act == kOrphanKeep)) Fail("(e): the unclaimed copy was not withdrawn exactly at 20 s");
    }
}
/* SCENARIO (f) [review F2] - a conflict ping-pong ends: two re-checks, then the recorded owner's word, never a third. */
void t_a1b1_scenario_conflict_bounded()
{
    using namespace cooplo;
    int conflicts = 0, act = kApConflict, rounds = 0;
    while (act == kApConflict && rounds < 10) { act = RosterApply(5, kAnsMoved, 4, 0, 1, 0, conflicts); if (act == kApConflict) ++conflicts; ++rounds; }
    if (conflicts != kRosterConflictMax || act != kApRekey || rounds != 3) Fail("(f): the conflict did not end after two re-checks with the recorded owner's word");
}

/* T-417: the kept copies' names - the log itself, then ".<n>" before the extension, in the file part only */
void t_t417_log_copy_names()
{
    const std::string base = "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.txt";
    if (cooplogrot::RotatedName(base, 0) != base) Fail("copy 0 is not the log itself");
    if (cooplogrot::RotatedName(base, 1) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.1.txt") Fail("copy 1 is misnamed");
    if (cooplogrot::RotatedName(base, 2) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.2.txt") Fail("copy 2 is misnamed");
    if (cooplogrot::RotatedName(std::string("a.b.txt"), 12) != "a.b.12.txt") Fail("a two-digit copy did not go before the LAST extension");
    if (cooplogrot::RotatedName(std::string("C:\\x.y\\log"), 1) != "C:\\x.y\\log.1") Fail("a dot in a folder name was taken for the extension");
    if (cooplogrot::RotatedName(std::string("d/.log"), 1) != "d/.log.1") Fail("a leading dot was taken for the extension");
    if (cooplogrot::RotatedName(std::string("x.txt"), -1) != "x.txt") Fail("a negative copy number changed the name");
    const std::wstring w = L"shared_wastelands_log.txt";
    if (cooplogrot::RotatedName(w, 2) != L"shared_wastelands_log.2.txt") Fail("the wide name of copy 2 is misnamed");
    if ((int)cooplogrot::kLogKeep != 3) Fail("not three launches kept (the log, .1 and .2)");
    for (int a = 0; a < (int)cooplogrot::kLogKeep; ++a)
        for (int b = a + 1; b < (int)cooplogrot::kLogKeep; ++b)
            if (cooplogrot::RotatedName(w, a) == cooplogrot::RotatedName(w, b)) Fail("two kept copies share a name");
}

/* T-879: the release build's quiet-logging gate - the first kFullLines of a key in full, then one count line a minute per key */
void t_t879_log_budget()
{
    using namespace swlogbudget;
    static Gate g;   /* zeroed, as a static gate in the mod is */
    unsigned int held = 0, span = 0;
    for (int i = 0; i < (int)kFullLines; ++i)
        if (!Pass(&g, 0, 1000u + (unsigned int)i, &held, &span) || held != 0) Fail("one of the first lines of a key was not written in full");
    if (Pass(&g, 0, 2000u, &held, &span) || held != 0) Fail("the line after the first few was written, or counted out early");
    if (Pass(&g, 0, 30000u, &held, &span) || held != 0) Fail("a count line came before a minute had passed");
    if (Pass(&g, 0, 1004u + kCountGapMs, &held, &span) || held != 3 || span != kCountGapMs) Fail("the count line a minute on did not count the three lines held back");
    if (Pass(&g, 0, 1005u + kCountGapMs, &held, &span) || held != 0) Fail("the count did not start again after its line");
    if (!Pass(&g, 7, 1006u + kCountGapMs, &held, &span)) Fail("a second key shared the first key's budget");
    if (KeyOf("Beak Thing squad") == KeyOf("Beak Thing squad2") || KeyOf("x") != KeyOf("x") || KeyOf(0) != 0 || KeyOf("") != 0 || KeyOf("a") == 0)
        Fail("text keys are not stable and distinct");
    static Gate full;
    for (unsigned long long k = 1; k <= (unsigned long long)kMaxKeys + 5; ++k) Pass(&full, k, 0, &held, &span);
    if (SlotFor(&full, (unsigned long long)kMaxKeys + 99) != &full.slots[kMaxKeys - 1]) Fail("a key after a full table did not share the last slot");
    /* the flush: each key's held count once (its own count, not another key's), the span since its last written line, and
       the count started again */
    static Gate f;
    for (int i = 0; i < (int)kFullLines + 3; ++i) Pass(&f, 1, 100u, &held, &span);
    for (int i = 0; i < (int)kFullLines + 2; ++i) Pass(&f, 2, 100u, &held, &span);
    for (int i = 0; i < (int)kFullLines; ++i) Pass(&f, 3, 100u, &held, &span);   /* nothing held */
    int cursor = 0;
    unsigned long long key = 0;
    unsigned int n = 0, got1 = 0, got2 = 0;
    while (TakeHeld(&f, &cursor, 5100u, &key, &held, &span))
    {
        ++n;
        if (key == 1) got1 = held; else if (key == 2) got2 = held; else Fail("the flush gave a key that held nothing");
        if (span != 5000u) Fail("a flushed count's span is not the time since its key's last written line");
    }
    if (n != 2 || got1 != 3 || got2 != 2) Fail("the flush did not give each key's held count once");
    cursor = 0;
    if (TakeHeld(&f, &cursor, 6000u, &key, &held, &span)) Fail("a second flush with nothing new held gave a count");
    if (Pass(&f, 1, 5200u, &held, &span) || held != 0) Fail("a line just after the flush was written or counted out");
    cursor = 0;
    if (!TakeHeld(&f, &cursor, 7000u, &key, &held, &span) || key != 1 || held != 1 || span != 1900u) Fail("the flush did not start the count again");
}

/* T-879: the parts of one launch's log and their names in the kept launches */
void t_t879_log_part_names()
{
    using namespace cooplogrot;
    const std::string base = "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.txt";
    if (PartName(base, 0, kPartCurrent) != base) Fail("this launch's current part is not the log itself");
    if (PartName(base, 0, kPartStart) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.start.txt") Fail("the opening part is misnamed");
    if (PartName(base, 0, kPartPrev) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.prev.txt") Fail("the previous part is misnamed");
    if (PartName(base, 1, kPartStart) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.1.start.txt") Fail("launch 1's opening part is misnamed");
    if (PartName(base, 2, kPartPrev) != "C:\\Kenshi\\mods\\Shared Wastelands\\shared_wastelands_log.2.prev.txt") Fail("launch 2's previous part is misnamed");
    for (int n = 0; n < (int)kLogKeep; ++n)
        if (PartName(base, n, kPartCurrent) != RotatedName(base, n)) Fail("a launch's current part is not its kept copy's name");
    if (PartName(std::string("C:\\x.y\\log"), 1, kPartStart) != "C:\\x.y\\log.1.start") Fail("a dot in a folder name was taken for the extension");
    const std::wstring w = L"shared_wastelands_log.txt";
    if (PartName(w, 1, kPartPrev) != L"shared_wastelands_log.1.prev.txt") Fail("the wide name of launch 1's previous part is misnamed");
    for (int a = 0; a < (int)kLogKeep * (int)kPartKinds; ++a)
        for (int b = a + 1; b < (int)kLogKeep * (int)kPartKinds; ++b)
            if (PartName(w, a / kPartKinds, a % kPartKinds) == PartName(w, b / kPartKinds, b % kPartKinds)) Fail("two parts share a name");
}

/* T-879: the 10 MB ceiling's decision, and a whole launch written through it stays under 10 MB with the opening kept */
void t_t879_log_ceiling()
{
    using namespace cooplogrot;
    if (CeilingStep(0, kPartBytes * 3, false, 0) != kCeilKeep) Fail("an empty part was cut");
    if (CeilingStep(kOpeningBytes - 10, 10, false, 0) != kCeilKeep) Fail("a line that fits the opening part cut it");
    if (CeilingStep(kOpeningBytes - 10, 11, false, 0) != kCeilCutOpening) Fail("the opening part was not cut at 2 MB");
    if (CeilingStep(kOpeningBytes + 5, 1, true, 0) != kCeilKeep) Fail("a later part was cut at the opening part's size");
    if (CeilingStep(kPartBytes, 1, true, 0) != kCeilRollPart) Fail("a later part was not rolled at 4 MB");
    if (CeilingStep(kPartBytes, 1, true, kPartBytes / 4) != kCeilKeep) Fail("the slack after a failed rename was not honoured");
    /* simulate a launch: 300-byte lines up to 37 MB */
    unsigned long long cur = 0, start = 0, prev = 0, cut = 0, written = 0;
    bool opened = false;
    int parts = 1;
    while (written < 37ull * 1024ull * 1024ull)
    {
        const unsigned long long line = 300;
        const int step = CeilingStep(cur, line, opened, 0);
        if (step == kCeilCutOpening) { start = cur; opened = true; cur = 0; ++parts; }
        else if (step == kCeilRollPart) { cut += prev; prev = cur; cur = 0; ++parts; }
        cur += line; written += line;
        if (start + prev + cur > 10ull * 1024ull * 1024ull) { Fail("the launch's parts passed 10 MB"); return; }
    }
    if (start == 0 || start > kOpeningBytes) Fail("the opening part was not kept at 2 MB or less");
    if (prev == 0 || prev > kPartBytes || cur > kPartBytes) Fail("a later part passed 4 MB");
    if (start + prev + cur + cut != written) Fail("bytes were lost other than the cut middle");
    if (parts < 9) Fail("37 MB did not make the expected number of parts");
}

/* T-879: the release build's drift line - only a copy that disagrees with its owner's word */
void t_t879_drift_disagrees()
{
    using namespace swlogbudget;
    int far = 0;
    if (DriftDisagrees(10.0f, 20.0f, 0, 0, &far) || far != 0) Fail("a copy within the correcting distance was called off");
    if (DriftDisagrees(100.0f, 20.0f, 0, 0, &far)) Fail("one far check was called off - a copy caught mid-stride");
    if (!DriftDisagrees(100.0f, 20.0f, 0, 0, &far)) Fail("two far checks in a row were not called off");
    if (!DriftDisagrees(20.5f, 20.0f, 0, 0, &far)) Fail("a third far check in a row was not called off");
    if (DriftDisagrees(5.0f, 20.0f, 0, 0, &far) || far != 0) Fail("a near check did not end the run of far checks");
    if (DriftDisagrees(25.0f, 20.0f, 0, 0, &far)) Fail("the run of far checks did not start again after a near one");
    far = 0;
    if (DriftDisagrees(20.0f, 20.0f, 0, 0, &far) || DriftDisagrees(20.0f, 20.0f, 0, 0, &far)) Fail("a copy at exactly the correcting distance was called off");
    far = 1;
    if (DriftDisagrees(-1.0f, 20.0f, 0, 0, &far) || far != 0) Fail("an unreadable drift was called a disagreement, or kept the run of far checks");
    far = 0;
    if (!DriftDisagrees(5.0f, 20.0f, 1, 0, &far) || !DriftDisagrees(5.0f, 20.0f, 0, 1, &far)) Fail("dead here against alive by the owner's word was not called off");
    if (DriftDisagrees(5.0f, 20.0f, 1, 1, &far)) Fail("a copy dead on both sides was called off");
}

/* T-910: the release build's "the first n lines, then none" (logbudget.h FirstLines) */
void t_t910_first_lines()
{
    using namespace swlogbudget;
    unsigned int shown = 0;
    for (int i = 0; i < 10; ++i) if (!FirstLines(&shown, 10)) Fail("one of the first 10 lines was not written");
    if (FirstLines(&shown, 10) || FirstLines(&shown, 10)) Fail("a line after the first 10 was written");
    if (shown != 10) Fail("the count of lines written went past its cap");
    unsigned int none = 0;
    if (FirstLines(&none, 0) || none != 0) Fail("a cap of 0 wrote a line");
}

/* T-910: the bug report's keeper-lines.txt - the first lines and the newest lines of one kind, every line counted (linekeep.h) */
void t_t910_line_keep()
{
    using namespace swlinekeep;
    {
        LineKeep k(2, 3);
        if (!Text(k).empty() || k.seen != 0) Fail("an empty keep has text or a count");
        Keep(&k, "1", 1); Keep(&k, "2", 1); Keep(&k, "3", 1); Keep(&k, "4", 1);
        if (Text(k) != "1\r\n2\r\n3\r\n4\r\n" || k.seen != 4) Fail("a keep that lost nothing is not every line in order");
    }
    {
        LineKeep k(2, 3);
        const char* in[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
        for (int i = 0; i < 8; ++i) Keep(&k, in[i], 1);
        if (k.seen != 8) Fail("a keep did not count every line");
        if (Text(k) != "1\r\n2\r\n... not kept: 3 lines between these two (this file keeps the first 2 and the newest 3) ...\r\n6\r\n7\r\n8\r\n")
            Fail("a full keep is not its first lines, one gap line, then its newest lines in order");
        Keep(&k, "9", 1);
        if (Text(k) != "1\r\n2\r\n... not kept: 4 lines between these two (this file keeps the first 2 and the newest 3) ...\r\n7\r\n8\r\n9\r\n")
            Fail("the ring did not move on by one line");
    }
    {
        LineKeep k(1, 1);
        Keep(&k, "a", 1); Keep(&k, "b", 1); Keep(&k, "c", 1);
        if (Text(k) != "a\r\n... not kept: 1 line between these two (this file keeps the first 1 and the newest 1) ...\r\nc\r\n") Fail("one line not kept is not said as one line");
    }
    {
        LineKeep k(2, 0);
        Keep(&k, "a", 1); Keep(&k, "b", 1); Keep(&k, "c", 1);
        if (Text(k) != "a\r\nb\r\n... not kept: 1 line between these two (this file keeps the first 2 and the newest 0) ...\r\n" || k.seen != 3)
            Fail("a keep with no ring did not keep its first lines and count the rest");
    }
    {
        LineKeep k(1, 1);
        const std::string big((size_t)kKeepLineCap + 50, 'x');
        Keep(&k, big.data(), big.size());
        if (k.first.size() != 1 || k.first[0].size() != (size_t)kKeepLineCap) Fail("a line past the cap was not cut at the cap");
    }
    if (NumText(0) != "0" || NumText(18446744073709551615ull) != "18446744073709551615") Fail("NumText is wrong at its ends");
}

/* T-879: the parts of one launch's log in the report budget - the middle (.prev) is cut first, then the opening (.start), the
   newest part last */
void t_t879_budget_cut_first()
{
    std::vector<coopbug::BudgetPart> parts(3);
    for (int i = 0; i < 3; ++i) { parts[i].order = coopbug::kOrderThisLog; parts[i].segBytes.push_back(100); parts[i].segBytes.push_back(100); }
    parts[0].cutFirst = 0;   /* the newest part */
    parts[1].cutFirst = 1;   /* the opening */
    parts[2].cutFirst = 2;   /* the middle */
    coopbug::BudgetCut c = coopbug::PlanBudget(parts, 500);
    if (c.dropSegs[2] != 1 || c.dropSegs[1] != 0 || c.dropSegs[0] != 0) Fail("the middle part did not lose the first segment");
    c = coopbug::PlanBudget(parts, 300);
    if (c.dropSegs[2] != 2 || c.dropSegs[1] != 1 || c.dropSegs[0] != 0) Fail("the opening lost a segment before the middle was gone, or the newest part was cut early");
    c = coopbug::PlanBudget(parts, 100);
    if (c.dropSegs[2] != 2 || c.dropSegs[1] != 2 || c.dropSegs[0] != 1) Fail("the newest part was not cut last");
    /* the opening part is cut from its end: its first bytes stay */
    std::vector<coopbug::BudgetPart> q(1);
    q[0].order = coopbug::kOrderThisLog; q[0].cutFirst = 1; q[0].cutFromEnd = true;
    q[0].segBytes.push_back(100); q[0].segBytes.push_back(30); q[0].segBytes.push_back(20);
    c = coopbug::PlanBudget(q, 130);   /* 20 over: the last segment (20) goes, not the first (100) */
    if (c.dropSegs[0] != 1 || c.total != 130) Fail("the opening part lost its first bytes before its last");
    c = coopbug::PlanBudget(q, 100);
    if (c.dropSegs[0] != 2 || c.total != 100) Fail("the opening part did not keep its first segment while it could");
    q[0].cutFromEnd = false;
    c = coopbug::PlanBudget(q, 130);
    if (c.dropSegs[0] != 1 || c.total != 50) Fail("a part not cut from its end did not lose its oldest segment first");
    /* the floors: the newest part keeps its log's whole share (35% of the room); the opening and middle get none of it */
    std::vector<coopbug::BudgetPart> f(4);
    for (int i = 0; i < 4; ++i) { f[i].order = coopbug::kOrderThisLog; f[i].segBytes.push_back(5000); }
    f[1].cutFirst = 1; f[1].cutFromEnd = true; f[2].cutFirst = 2;
    f[3].order = coopbug::kOrderNearby;
    coopbug::SetBudgetFloors(&f, 1000);   /* room 1000: 350 this launch's newest part, 250 the nearby log, the rest 400 to the newest part */
    if (f[1].floor != 0 || f[2].floor != 0) Fail("the opening or middle part took a share of the floor");
    if (f[3].floor != 250) Fail("the nearby log's floor changed");
    if (f[0].floor != 750) Fail("the newest part did not keep its log's whole share");
}

/* ================= T-461: REPORT A BUG (src/common/bugreport.h) =================
 * The approved words, the description rule, the IP scrub, the compressor (checked by an inflater written here from RFC 1951 -
 * stored and fixed-code blocks, all the compressor writes), CRC joining, the size budget's cutting order, the zip and upload
 * layouts, the crash-file rule and the nearby-log message pair. */
struct T461BitIn
{
    const unsigned char* p; size_t n, pos; unsigned int buf; int cnt; bool bad;
    T461BitIn(const unsigned char* q, size_t m) : p(q), n(m), pos(0), buf(0), cnt(0), bad(false) {}
    unsigned int Bits(int k)
    {
        while (cnt < k) { if (pos >= n) { bad = true; return 0; } buf |= (unsigned int)p[pos++] << cnt; cnt += 8; }
        const unsigned int v = buf & ((1u << k) - 1u);
        buf >>= k; cnt -= k;
        return v;
    }
    void ToByte() { const int d = cnt % 8; buf >>= d; cnt -= d; }
};
int T461FixedLitLen(T461BitIn& b)
{
    unsigned int code = 0;
    for (int len = 1; len <= 9; ++len)
    {
        code = (code << 1) | b.Bits(1);
        if (b.bad) return -1;
        if (len == 7 && code <= 0x17u) return 256 + (int)code;
        if (len == 8 && code >= 0x30u && code <= 0xBFu) return (int)code - 0x30;
        if (len == 8 && code >= 0xC0u && code <= 0xC7u) return 280 + (int)(code - 0xC0u);
        if (len == 9 && code >= 0x190u && code <= 0x1FFu) return 144 + (int)(code - 0x190u);
    }
    return -1;
}
/* raw DEFLATE -> bytes; false on anything malformed, a dynamic-code block, or bytes left over after the last block */
bool T461Inflate(const std::vector<unsigned char>& in, std::string* out)
{
    static const int kLb[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const int kLe[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const int kDb[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const int kDe[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    out->clear();
    if (in.empty()) return false;
    T461BitIn b(&in[0], in.size());
    unsigned int last = 0;
    do
    {
        last = b.Bits(1);
        const unsigned int type = b.Bits(2);
        if (b.bad) return false;
        if (type == 0)
        {
            b.ToByte();
            const unsigned int len = b.Bits(16), nlen = b.Bits(16);
            if (b.bad || (len ^ 0xFFFFu) != nlen) return false;
            for (unsigned int k = 0; k < len; ++k) { *out += (char)b.Bits(8); if (b.bad) return false; }
        }
        else if (type == 1)
        {
            for (;;)
            {
                const int sym = T461FixedLitLen(b);
                if (sym < 0) return false;
                if (sym < 256) { *out += (char)sym; continue; }
                if (sym == 256) break;
                if (sym > 285) return false;
                const int li = sym - 257;
                const int len = kLb[li] + (int)b.Bits(kLe[li]);
                unsigned int dc = 0;
                for (int k = 0; k < 5; ++k) dc = (dc << 1) | b.Bits(1);
                if (b.bad || dc > 29u) return false;
                const int dist = kDb[dc] + (int)b.Bits(kDe[dc]);
                if (b.bad || dist <= 0 || (size_t)dist > out->size()) return false;
                for (int k = 0; k < len; ++k) *out += (*out)[out->size() - (size_t)dist];
            }
        }
        else return false;
    } while (last == 0);
    return b.pos == in.size();
}
std::string T461Lcg(size_t n, unsigned int seed)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; s[i] = (char)(seed >> 16); }
    return s;
}
std::string T461LogText(size_t lines)
{
    std::string s;
    for (size_t i = 0; i < lines; ++i)
    {
        char b[200];
        std::sprintf(b, "%u.%03u KenshiCoop: [STORE] LIVE probe #%u from slot %u sector %u,%u loadedHere=1 from 203.0.113.%u:7777\r\n",
                     (unsigned)(i / 7), (unsigned)(i % 1000), (unsigned)i, (unsigned)(i % 5), (unsigned)(i % 64), (unsigned)((i * 7) % 64), (unsigned)(i % 250));
        s += b;
    }
    return s;
}
std::string T461Segs(const std::string& raw)
{
    std::vector<unsigned char> z;
    coopbug::CompressSegment((const unsigned char*)raw.data(), raw.size(), &z);
    z.push_back(coopbug::kFinalBlock[0]); z.push_back(coopbug::kFinalBlock[1]);
    std::string back;
    if (!T461Inflate(z, &back)) return std::string("<<inflate failed>>");
    return back;
}

void t_t461_approved_words()
{
    if (std::string(coopbug::kButtonCaption) != "REPORT A BUG") Fail("button / window title");
    if (std::string(coopbug::kLabelText) != "WHAT HAPPENED?") Fail("box label");
    if (std::string(coopbug::kCancelCaption) != "CANCEL" || std::string(coopbug::kSendCaption) != "SEND") Fail("CANCEL / SEND");
    if (std::string(coopbug::kPreparingLine) != "Preparing your report...") Fail("step 1");
    if (coopbug::SendingLine(40) != "Sending your report... 40%" || coopbug::SendingLine(0) != "Sending your report... 0%"
        || coopbug::SendingLine(140) != "Sending your report... 100%") Fail("step 2: " + coopbug::SendingLine(40));
    if (coopbug::CounterText(0) != "0 / 1000" || coopbug::CounterText(71) != "71 / 1000") Fail("counter: " + coopbug::CounterText(71));
    if (std::string(coopbug::kSentTitle) != "REPORT SENT" || std::string(coopbug::kSentText) != "Thank you. Your report has been sent to the Shared Wastelands modder."
        || std::string(coopbug::kOkCaption) != "OK") Fail("REPORT SENT box");
    if (std::string(coopbug::kFailTitle) != "CAN'T SEND REPORT" || std::string(coopbug::kBackCaption) != "BACK"
        || std::string(coopbug::kTryAgainCaption) != "TRY AGAIN") Fail("CAN'T SEND REPORT box");
    if (coopbug::FailText(coopbug::kFailNoInternet, 12007) != "Couldn't send your report: no internet connection.") Fail("no internet text");
    if (coopbug::FailText(coopbug::kFailServer, 503) != "The report server isn't responding.") Fail("server text");
    if (coopbug::FailText(coopbug::kFailOther, 413) != "Couldn't send your report (error 413).") Fail("other text: " + coopbug::FailText(coopbug::kFailOther, 413));
    /* the relay address is the owner's Worker, over HTTPS only; an empty one would fail as "error 1" */
    if (std::string(coopbug::kRelayUrl) != "https://kenshi-bug-relay.allscott16.workers.dev/") Fail(std::string("the relay address is ") + coopbug::kRelayUrl);
    if (std::string(coopbug::kRelayUrl).compare(0, 8, "https://") != 0) Fail("the relay address is not https");
    if (coopbug::kErrNoRelay != 1 || coopbug::FailText(coopbug::kFailOther, coopbug::kErrNoRelay) != "Couldn't send your report (error 1).") Fail("no-relay text");
}
void t_t461_description_rule()
{
    if (coopbug::DescriptionSendable("")) Fail("an empty description is sendable");
    if (coopbug::DescriptionSendable("  \r\n\t ")) Fail("spaces and line breaks only are sendable");
    if (!coopbug::DescriptionSendable(" x ")) Fail("one letter is not sendable");
    if (!coopbug::DescriptionSendable(std::string(1000, 'a'))) Fail("1000 characters are not sendable");
    if (coopbug::DescriptionSendable(std::string(1001, 'a'))) Fail("1001 characters are sendable");
    std::string u;
    for (int i = 0; i < 1000; ++i) u += "\xC3\xA9";   /* 1000 x U+00E9, 2000 bytes */
    if (coopbug::Utf8Chars(u) != 1000 || !coopbug::DescriptionSendable(u)) Fail("1000 two-byte characters are not 1000 characters");
    if (coopbug::DescriptionSendable(u + "\xC3\xA9")) Fail("1001 two-byte characters are sendable");
    if (coopbug::CaptionToPlain("a##b") != "a#b") Fail("## is not a typed #");
    if (coopbug::CaptionToPlain("#FF0000red#ffffff") != "red") Fail("a colour tag was kept: " + coopbug::CaptionToPlain("#FF0000red#ffffff"));
    if (coopbug::CaptionToPlain("#12 and #") != "#12 and #") Fail("a short # sequence was changed");
    if (coopbug::kDescMaxChars != 1000) Fail("not 1000 characters (371)");
}
void t_t461_ip_scrub()
{
    static const char* const kCase[][2] = {
        { "connected to 203.0.113.5:7777", "connected to [ip]:7777" },
        { "lan 192.168.1.20 and 10.0.0.1", "lan [lan-ip] and [lan-ip]" },
        { "172.16.0.1 172.32.0.1 100.64.0.1 100.128.0.1 169.254.3.4", "[lan-ip] [ip] [lan-ip] [ip] [lan-ip]" },
        { "127.0.0.1 0.0.0.0 255.255.255.255", "127.0.0.1 0.0.0.0 255.255.255.255" },
        { "version 1.0.68 and 1.2.3.4.5 and 999.1.1.1", "version 1.0.68 and 1.2.3.4.5 and 999.1.1.1" },
        { "time 12:34:56 std::string Bed::Add aa:bb:cc:dd:ee:ff", "time 12:34:56 std::string Bed::Add aa:bb:cc:dd:ee:ff" },
        { "[2001:db8::8a2e:370:7334]:7777", "[[ip6]]:7777" },
        { "2001:0db8:85a3:0000:0000:8a2e:0370:7334 x", "[ip6] x" },
        { "fe80::1c2:3%4", "[ip6]%4" },
        { "::1 and ::", "::1 and ::" },
        { "::ffff:192.0.2.128", "[ip6]" },
        { "ends 10.1.2.3.", "ends [lan-ip]." },
        { "...10.0.0.5", "...[lan-ip]" },
        { "v1.2.3.4", "v[ip]" },
        { "Player Anna joined from 198.51.100.7 as Anna", "Player Anna joined from [ip] as Anna" },
    };
    const size_t n = sizeof(kCase) / sizeof(kCase[0]);
    for (size_t i = 0; i < n; ++i)
    {
        std::string out;
        coopbug::ScrubIps(kCase[i][0], &out);
        if (out != kCase[i][1]) Fail(std::string("scrub '") + kCase[i][0] + "' -> '" + out + "', want '" + kCase[i][1] + "'");
    }
    std::string out;
    if (coopbug::ScrubIps("a 10.0.0.1 b 8.8.8.8 c ::1 d fe80::1", &out) != 3) Fail("the count is not the addresses replaced (loopback kept)");
    if (coopbug::ScrubIps("", &out) != 0 || !out.empty()) Fail("empty text");
}
void t_t461_id_scrub()
{
    const std::string id = "0123456789abcdef0123456789abcdef";   /* a player identity code: 32 lowercase hex characters */
    static const char* const kCase[][2] = {
        /* a code alone, and at the start and end of the text */
        { "0123456789abcdef0123456789abcdef", "01234567..." },
        { "playerid 0123456789abcdef0123456789abcdef picked", "playerid 01234567... picked" },
        /* inside a profile id (<code>.<n>) and in quotes */
        { "profile 0123456789abcdef0123456789abcdef.1 chosen", "profile 01234567....1 chosen" },
        { "id='fedcba9876543210fedcba9876543210'", "id='fedcba98...'" },
        /* next to an IP address: both go */
        { "0123456789abcdef0123456789abcdef@203.0.113.5:7777", "01234567...@[ip]:7777" },
        { "from 10.0.0.2 as 0123456789abcdef0123456789abcdef", "from [lan-ip] as 01234567..." },
        /* upper case is cut too */
        { "ID 0123456789ABCDEF0123456789ABCDEF", "ID 01234567..." },
        /* a longer or shorter hex run is something else and stays whole */
        { "hash 0123456789abcdef0123456789abcdef0 end", "hash 0123456789abcdef0123456789abcdef0 end" },
        { "sha1 da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { "short 0123456789abcdef0123456789abcde", "short 0123456789abcdef0123456789abcde" },
        /* a hex letter joined on either side makes the run longer, so it stays */
        { "a0123456789abcdef0123456789abcdef", "a0123456789abcdef0123456789abcdef" },
        { "0123456789abcdef0123456789abcdefF", "0123456789abcdef0123456789abcdefF" },
        /* a non-hex letter is a boundary */
        { "x0123456789abcdef0123456789abcdefz", "x01234567...z" },
        /* two codes in a row with a separator */
        { "0123456789abcdef0123456789abcdef,ffffffffffffffffffffffffffffffff", "01234567...,ffffffff..." },
    };
    const size_t n = sizeof(kCase) / sizeof(kCase[0]);
    for (size_t i = 0; i < n; ++i)
    {
        std::string out;
        coopbug::ScrubIps(kCase[i][0], &out);
        if (out != kCase[i][1]) Fail(std::string("id scrub '") + kCase[i][0] + "' -> '" + out + "', want '" + kCase[i][1] + "'");
    }
    std::string out;
    size_t ids = 0;
    const size_t ips = coopbug::ScrubIps("a " + id + " b 8.8.8.8 c " + id + ".2 d", &out, &ids);
    if (ips != 1 || ids != 2) Fail("the counts are not one address and two codes: " + I((int)ips) + ", " + I((int)ids));
    if (out.find(id) != std::string::npos) Fail("a whole code survived the scrub");
    /* a packed log carries no whole code: the scrub runs on every segment */
    std::string raw;
    for (int k = 0; k < 2000; ++k) raw += "[STORE] profile picked " + id + ".1 for this world\n";
    coopbug::LogPack p;
    if (!coopbug::PackLog(raw, &p, 0)) { Fail("PackLog stopped"); return; }
    std::vector<unsigned char> z; unsigned long long rawLen = 0; unsigned int crc = 0;
    coopbug::JoinPack(p, 0, &z, &rawLen, &crc);
    std::string back;
    if (!T461Inflate(z, &back) || back.find(id) != std::string::npos || back.find("01234567....1") == std::string::npos)
        Fail("a packed log kept a whole identity code");
}
void t_t461_deflate_round_trip()
{
    const std::string cases[] = { std::string("a"), std::string("abcabcabcabcabcabc"), std::string(100000, 'x'), T461Lcg(70000, 7u),
                                  T461LogText(3000), std::string("\0\0\0\xff\xff", 5) };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (T461Segs(cases[i]) != cases[i]) Fail("case " + I((int)i) + " did not come back from the compressor (" + I((int)cases[i].size()) + " bytes)");
    /* a segment ends byte-aligned with the sync marker, and segments join: three of them + the final block are one stream */
    const std::string a = T461LogText(400), b = T461Lcg(5000, 3u), c = std::string(40000, 'q') + "end";
    std::vector<unsigned char> z;
    coopbug::CompressSegment((const unsigned char*)a.data(), a.size(), &z);
    if (z.size() < 4 || z[z.size() - 4] != 0x00 || z[z.size() - 3] != 0x00 || z[z.size() - 2] != 0xFF || z[z.size() - 1] != 0xFF) Fail("a segment does not end with 00 00 FF FF");
    coopbug::CompressSegment((const unsigned char*)b.data(), b.size(), &z);
    coopbug::CompressSegment((const unsigned char*)c.data(), c.size(), &z);
    z.push_back(coopbug::kFinalBlock[0]); z.push_back(coopbug::kFinalBlock[1]);
    std::string back;
    if (!T461Inflate(z, &back) || back != a + b + c) Fail("three joined segments are not one stream");
    /* a log compresses (the 8 MB budget rests on it) */
    std::vector<unsigned char> lz;
    const std::string log = T461LogText(5000);
    coopbug::CompressSegment((const unsigned char*)log.data(), log.size(), &lz);
    if (lz.size() * 3 > log.size()) Fail("a log compressed to more than a third of its size: " + I((int)lz.size()) + " of " + I((int)log.size()));
    /* the final block alone is an empty stream */
    std::vector<unsigned char> f(coopbug::kFinalBlock, coopbug::kFinalBlock + 2);
    if (!T461Inflate(f, &back) || !back.empty()) Fail("the final block is not an empty stream");
}
void t_t461_crc_join()
{
    const std::string a = T461Lcg(1234, 1u), b = T461Lcg(99999, 2u);
    const std::string ab = a + b;
    if (coopbug::Crc32Combine(coopstore::Crc32(a.data(), a.size()), coopstore::Crc32(b.data(), b.size()), b.size()) != coopstore::Crc32(ab.data(), ab.size()))
        Fail("crc(a)+crc(b) is not crc(ab)");
    if (coopbug::Crc32Combine(0u, coopstore::Crc32(b.data(), b.size()), b.size()) != coopstore::Crc32(b.data(), b.size())) Fail("joined to nothing");
    if (coopbug::Crc32Combine(0x1234u, 0u, 0) != 0x1234u) Fail("joined with nothing");
}
void t_t461_segments()
{
    const std::string s = "aa\nbbbb\ncc";
    const std::vector<size_t> e = coopbug::SegmentEnds(s.data(), s.size(), 3);
    if (e.size() != 3 || e[0] != 3 || e[1] != 8 || e[2] != 10) Fail("segments do not end at line ends");
    const std::string x(10, 'x');
    const std::vector<size_t> h = coopbug::SegmentEnds(x.data(), x.size(), 3);
    if (h.size() != 2 || h[0] != 6 || h[1] != 10) Fail("a line with no end is not cut at twice the target");
    if (coopbug::WindowStart("abc\ndef", 7) != 4 || coopbug::WindowStart("abc", 3) != 3) Fail("the read window does not start after a line end");
}
void t_t461_pack_log_and_cap()
{
    /* a log of several segments with an address on every line: scrubbed, compressed, joined, read back */
    const std::string raw = T461LogText(36000);   /* about 4 MB */
    coopbug::LogPack p;
    if (!coopbug::PackLog(raw, &p, 0)) Fail("PackLog stopped with no stop flag");
    if (p.segs.size() < 3) { Fail("fewer than three segments for " + I((int)raw.size()) + " bytes"); return; }
    std::string clean;
    const size_t n = coopbug::ScrubIps(raw, &clean);
    if (p.ipsScrubbed != n || n != 36000) Fail("the scrub count is not one per line: " + I((int)p.ipsScrubbed));
    std::vector<unsigned char> z; unsigned long long rawLen = 0; unsigned int crc = 0;
    coopbug::JoinPack(p, 0, &z, &rawLen, &crc);
    std::string back;
    if (!T461Inflate(z, &back) || back != clean) Fail("the joined log is not the scrubbed text");
    if (rawLen != clean.size() || crc != coopstore::Crc32(clean.data(), clean.size())) Fail("the joined length or CRC is wrong");
    /* the oldest segment goes first, and what is kept is exactly the newest part, from a line start */
    coopbug::LogPack q = p;
    const unsigned long long full = coopbug::PackBytes(q, 0) + sizeof(coopbug::kFinalBlock);
    coopbug::CapPack(&q, full - 1);
    if (q.segs.size() != p.segs.size() - 1 || q.rawDropped != p.segs[0].rawLen) Fail("the cap did not drop exactly the oldest segment");
    coopbug::JoinPack(q, 0, &z, &rawLen, &crc);
    if (!T461Inflate(z, &back) || back != clean.substr(p.segs[0].rawLen)) Fail("after the cap the log is not its newest part");
    if (clean[p.segs[0].rawLen - 1] != '\n') Fail("the cut is not at a line end");
    /* newest first, only as far back as fits: the same segments as PackLog's newest, the rest never compressed */
    coopbug::LogPack w;
    coopbug::PackLogNewest(raw, &w, full - 1);
    if (w.segs.size() != p.segs.size() - 1 || w.segs.back().bytes != p.segs.back().bytes || w.segs[0].crc != p.segs[1].crc)
        Fail("PackLogNewest did not keep exactly the newest segments in order");
    if (w.rawDropped == 0 || raw[(size_t)w.rawDropped - 1] != '\n') Fail("PackLogNewest's dropped part does not end at a line end");
    coopbug::JoinPack(w, 0, &z, &rawLen, &crc);
    if (!T461Inflate(z, &back) || back != clean.substr(p.segs[0].rawLen)) Fail("PackLogNewest's log is not the newest part");
    coopbug::LogPack all;
    coopbug::PackLogNewest(raw, &all, full);
    if (all.segs.size() != p.segs.size() || all.rawDropped != 0 || all.ipsScrubbed != p.ipsScrubbed) Fail("PackLogNewest with room for all is not PackLog");
    /* a raised stop flag ends the work */
    volatile long stop = 1;
    coopbug::LogPack r;
    if (coopbug::PackLog(raw, &r, &stop) || !r.segs.empty()) Fail("a raised stop flag did not stop PackLog");
}
void t_t461_budget_order()
{
    using namespace coopbug;
    std::vector<BudgetPart> parts(5);
    parts[0].order = kOrderThisLog;  parts[0].fixed = 100; parts[0].segBytes.push_back(50); parts[0].segBytes.push_back(50);
    parts[1].order = kOrderPrevLog;  parts[1].fixed = 100; parts[1].segBytes.push_back(40); parts[1].segBytes.push_back(30); parts[1].segBytes.push_back(20);
    parts[2].order = kOrderNearby;   parts[2].fixed = 100; parts[2].segBytes.push_back(60); parts[2].segBytes.push_back(60);
    parts[3].order = kOrderCrash;    parts[3].fixed = 300; parts[3].droppable = true;
    parts[4].order = kOrderKeep;     parts[4].fixed = 200;
    /* total 100+100 + 100+90 + 100+120 + 300 + 200 = 1110 */
    BudgetCut c = PlanBudget(parts, 2000);
    if (!c.fits || c.total != 1110 || c.dropSegs[0] || c.dropSegs[1] || c.dropSegs[2] || c.dropped[3]) Fail("a report under the limit was cut");
    c = PlanBudget(parts, 1080);   /* 30 over: the previous log's oldest segment (40) */
    if (!c.fits || c.dropSegs[1] != 1 || c.dropSegs[0] != 0 || c.dropSegs[2] != 0 || c.total != 1070) Fail("the previous log's oldest part was not cut first");
    c = PlanBudget(parts, 1010);   /* the previous log's three segments (90), then one nearby segment (60) */
    if (!c.fits || c.dropSegs[1] != 3 || c.dropSegs[2] != 1 || c.dropSegs[0] != 0 || c.total != 960) Fail("nearby logs were not next: " + I((int)c.dropSegs[2]));
    c = PlanBudget(parts, 850);    /* then the other nearby segment, then this launch's log */
    if (!c.fits || c.dropSegs[2] != 2 || c.dropSegs[0] != 1 || c.dropped[3] != 0 || c.total != 850) Fail("this launch's log was not cut after the nearby logs");
    c = PlanBudget(parts, 700);    /* every segment gone (800): the crash file goes whole */
    if (!c.fits || c.dropSegs[0] != 2 || c.dropped[3] != 1 || c.total != 500) Fail("the crash file did not go last, whole");
    c = PlanBudget(parts, 100);    /* only the never-cut parts are left: does not fit, says so */
    if (c.fits || c.total != 500 || c.dropped[4] != 0) Fail("an over-limit result did not say it does not fit");
    /* among nearby logs the one with the most kept goes first */
    std::vector<BudgetPart> nb(2);
    nb[0].order = kOrderNearby; nb[0].segBytes.push_back(10); nb[0].segBytes.push_back(10);
    nb[1].order = kOrderNearby; nb[1].segBytes.push_back(30); nb[1].segBytes.push_back(30);
    c = PlanBudget(nb, 70);
    if (c.dropSegs[1] != 1 || c.dropSegs[0] != 0) Fail("the larger nearby log was not cut first");
    if (kPackLimit != 8000000u) Fail("the limit is not under 8 MB (372)");
}
/* The one-pass budget with no floors: what PlanBudget must still give when every floor is 0. */
static coopbug::BudgetCut T835OnePass(const std::vector<coopbug::BudgetPart>& parts, unsigned long long limit)
{
    coopbug::BudgetCut c;
    c.dropSegs.assign(parts.size(), 0);
    c.dropped.assign(parts.size(), 0);
    std::vector<unsigned long long> kept(parts.size(), 0);
    c.total = 0;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        for (size_t k = 0; k < parts[i].segBytes.size(); ++k) kept[i] += parts[i].segBytes[k];
        c.total += parts[i].fixed + kept[i];
    }
    while (c.total > limit)
    {
        int best = -1;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (c.dropped[i] != 0 || parts[i].order == coopbug::kOrderKeep || c.dropSegs[i] >= parts[i].segBytes.size()) continue;
            if (best < 0 || parts[i].order < parts[(size_t)best].order
                || (parts[i].order == parts[(size_t)best].order && kept[i] > kept[(size_t)best])) best = (int)i;
        }
        if (best >= 0)
        {
            const size_t b = (size_t)best;
            const unsigned long long s = parts[b].segBytes[c.dropSegs[b]];
            ++c.dropSegs[b];
            kept[b] -= s;
            c.total -= s;
            continue;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (c.dropped[i] != 0 || !parts[i].droppable) continue;
            if (best < 0 || parts[i].order < parts[(size_t)best].order) best = (int)i;
        }
        if (best < 0) break;
        c.dropped[(size_t)best] = 1;
        c.total -= parts[(size_t)best].fixed + kept[(size_t)best];
        kept[(size_t)best] = 0;
    }
    c.fits = c.total <= limit;
    return c;
}
static bool T835SameCut(const coopbug::BudgetCut& a, const coopbug::BudgetCut& b)
{
    return a.dropSegs == b.dropSegs && a.dropped == b.dropped && a.total == b.total && a.fits == b.fits;
}
static unsigned long long T835Kept(const coopbug::BudgetPart& p, const coopbug::BudgetCut& c, size_t i)
{
    unsigned long long n = 0;
    if (c.dropped[i] != 0) return 0;
    for (size_t k = c.dropSegs[i]; k < p.segBytes.size(); ++k) n += p.segBytes[k];
    return n;
}
/* every floor 0: the budget is the one-pass budget for every limit */
void t_t835_budget_floor_zero_same()
{
    using namespace coopbug;
    std::vector<BudgetPart> a(5);
    a[0].order = kOrderThisLog;  a[0].fixed = 100; a[0].segBytes.push_back(50); a[0].segBytes.push_back(50);
    a[1].order = kOrderPrevLog;  a[1].fixed = 100; a[1].segBytes.push_back(40); a[1].segBytes.push_back(30); a[1].segBytes.push_back(20);
    a[2].order = kOrderNearby;   a[2].fixed = 100; a[2].segBytes.push_back(60); a[2].segBytes.push_back(60);
    a[3].order = kOrderCrash;    a[3].fixed = 300; a[3].droppable = true;
    a[4].order = kOrderKeep;     a[4].fixed = 200;
    std::vector<BudgetPart> b(4);
    b[0].order = kOrderThisLog;  b[0].fixed = 100; b[0].segBytes.push_back(50);
    b[1].order = kOrderPrevLog;  b[1].fixed = 100; b[1].segBytes.push_back(40); b[1].segBytes.push_back(40);
    b[2].order = kOrderOlderLog; b[2].fixed = 100; b[2].segBytes.push_back(30); b[2].segBytes.push_back(0); b[2].segBytes.push_back(30);
    b[3].order = kOrderNearby;   b[3].fixed = 10;  b[3].segBytes.push_back(25); b[3].segBytes.push_back(25);
    for (unsigned long long limit = 0; limit <= 1200; ++limit)
    {
        if (!T835SameCut(PlanBudget(a, limit), T835OnePass(a, limit))) Fail("with no floors the five-part budget changed at limit " + I((int)limit));
        if (!T835SameCut(PlanBudget(b, limit), T835OnePass(b, limit))) Fail("with no floors the four-part budget changed at limit " + I((int)limit));
    }
    /* no room left by the fixed bytes: every floor is 0 and the cut is the one-pass cut */
    SetBudgetFloors(&a, 700);
    for (size_t i = 0; i < a.size(); ++i) if (a[i].floor != 0) Fail("a floor was set with no room");
    if (!T835SameCut(PlanBudget(a, 700), T835OnePass(a, 700))) Fail("floors set with no room changed the cut");
}
/* a 10 MB current log no longer cuts the earlier launches' logs and the nearby log to nothing */
void t_t835_budget_field_case()
{
    using namespace coopbug;
    std::vector<BudgetPart> p(5);
    p[0].order = kOrderThisLog;  p[0].fixed = 100; p[0].segBytes.assign(1250, 8000);
    p[1].order = kOrderPrevLog;  p[1].fixed = 100; p[1].segBytes.assign(600, 8000);
    p[2].order = kOrderOlderLog; p[2].fixed = 100; p[2].segBytes.assign(600, 8000);
    p[3].order = kOrderNearby;   p[3].fixed = 150; p[3].segBytes.assign(190, 8000);
    p[4].order = kOrderKeep;     p[4].fixed = 20000;
    /* with no floors the three other logs are cut to nothing */
    BudgetCut old = PlanBudget(p, kPackLimit);
    if (!PartLeftOut(p, old, 1) || !PartLeftOut(p, old, 2) || !PartLeftOut(p, old, 3) || PartLeftOut(p, old, 0))
        Fail("with no floors the field case did not cut the other logs to nothing");
    /* room 8000000 - 20450 = 7979550: 3590797 / 1595910 / 797955 / 1994887 (nearby, whole size 1520000); the rest 474888 to this launch's log */
    SetBudgetFloors(&p, kPackLimit);
    if (p[0].floor != 4065685u || p[1].floor != 1595910u || p[2].floor != 797955u || p[3].floor != 1520000u || p[4].floor != 0)
        Fail("the field case's floors");
    BudgetCut c = PlanBudget(p, kPackLimit);
    if (!c.fits || c.total > kPackLimit) Fail("the field case does not fit");
    for (size_t i = 0; i < 4; ++i) if (PartLeftOut(p, c, i) || c.dropSegs[i] >= p[i].segBytes.size()) Fail("a log was cut to nothing: part " + I((int)i));
    if (c.dropSegs[2] != 501 || c.dropSegs[1] != 401 || c.dropSegs[3] != 0 || c.dropSegs[0] != 741 || c.total != 7996450u)
        Fail("the field case's cuts: " + I((int)c.dropSegs[0]) + " " + I((int)c.dropSegs[1]) + " " + I((int)c.dropSegs[2]) + " " + I((int)c.dropSegs[3]));
    const unsigned long long kThis = T835Kept(p[0], c, 0);
    if (kThis <= T835Kept(p[1], c, 1) || kThis <= T835Kept(p[2], c, 2) || kThis <= T835Kept(p[3], c, 3)) Fail("this launch's log did not keep the most");
}
/* the floor shares, a log smaller than its share, and where the rest goes */
void t_t835_budget_floor_shares()
{
    using namespace coopbug;
    std::vector<BudgetPart> p(7);
    p[0].order = kOrderThisLog;  p[0].segBytes.assign(10, 100);   /* 1000; share 900 */
    p[1].order = kOrderPrevLog;  p[1].segBytes.assign(2, 50);     /* 100; share 400 */
    p[2].order = kOrderOlderLog; p[2].segBytes.assign(5, 100);    /* 500; share 200 */
    p[3].order = kOrderNearby;   p[3].segBytes.assign(1, 30);     /* 30; share 250 */
    p[4].order = kOrderNearby;   p[4].segBytes.assign(10, 100);   /* 1000; share 250 */
    p[5].order = kOrderCrash;    p[5].droppable = true;
    p[6].order = kOrderKeep;
    p[5].floor = 7; p[6].floor = 7;   /* set again by SetBudgetFloors */
    SetBudgetFloors(&p, 2000);
    /* given 900 + 100 + 200 + 30 + 250 = 1480; the rest 520: 100 to this launch's log (its whole size), 420 to the larger nearby log */
    if (p[0].floor != 1000 || p[1].floor != 100 || p[2].floor != 200 || p[3].floor != 30 || p[4].floor != 670 || p[5].floor != 0 || p[6].floor != 0)
        Fail("the floor shares: " + I((int)p[0].floor) + " " + I((int)p[1].floor) + " " + I((int)p[2].floor) + " " + I((int)p[3].floor) + " " + I((int)p[4].floor));
    /* total 2630: the oldest log down to its floor (3 segments), then the larger nearby log until it fits, one segment under its floor */
    BudgetCut c = PlanBudget(p, 2000);
    if (!c.fits || c.total != 1930 || c.dropSegs[2] != 3 || c.dropSegs[4] != 4 || c.dropSegs[0] != 0 || c.dropSegs[1] != 0 || c.dropSegs[3] != 0)
        Fail("pass 1 did not keep to the floors: " + I((int)c.dropSegs[2]) + " " + I((int)c.dropSegs[4]) + " total " + I((int)c.total));
    /* a share with no log: with no earlier logs, the room goes to this launch's log and the nearby log */
    std::vector<BudgetPart> q(2);
    q[0].order = kOrderThisLog; q[0].fixed = 50; q[0].segBytes.assign(20, 100);
    q[1].order = kOrderNearby;  q[1].fixed = 50; q[1].segBytes.assign(20, 100);
    SetBudgetFloors(&q, 1100);   /* room 1000: 450 and 250, the rest 300 to this launch's log */
    if (q[0].floor != 750 || q[1].floor != 250) Fail("an unused share did not go to this launch's log: " + I((int)q[0].floor));
}
/* the world server's log as one more part: every large log keeps something, and the floors fill the room exactly */
void t_t838_budget_server_log()
{
    using namespace coopbug;
    std::vector<BudgetPart> p(6);
    p[0].order = kOrderThisLog;   p[0].fixed = 100; p[0].segBytes.assign(1250, 8000);
    p[1].order = kOrderServerLog; p[1].fixed = 120; p[1].segBytes.assign(900, 8000);
    p[2].order = kOrderPrevLog;   p[2].fixed = 100; p[2].segBytes.assign(600, 8000);
    p[3].order = kOrderOlderLog;  p[3].fixed = 100; p[3].segBytes.assign(600, 8000);
    p[4].order = kOrderNearby;    p[4].fixed = 150; p[4].segBytes.assign(400, 8000);
    p[5].order = kOrderKeep;      p[5].fixed = 20000;
    SetBudgetFloors(&p, kPackLimit);
    unsigned long long fixed = 0, floors = 0;
    for (size_t i = 0; i < p.size(); ++i) { fixed += p[i].fixed; floors += p[i].floor; }
    const unsigned long long room = kPackLimit - fixed;
    if (fixed + floors != kPackLimit) Fail("the floors do not fill the room: " + I((int)floors) + " of " + I((int)room));
    if (p[1].floor != room * 10u / 100u || p[2].floor != room * 20u / 100u || p[3].floor != room * 10u / 100u || p[4].floor != room * 25u / 100u)
        Fail("the floor shares with the world server's log: " + I((int)p[1].floor) + " " + I((int)p[2].floor) + " " + I((int)p[3].floor) + " " + I((int)p[4].floor));
    if (p[0].floor < room * 35u / 100u || p[5].floor != 0) Fail("this launch's floor or the never-cut part's floor");
    BudgetCut c = PlanBudget(p, kPackLimit);
    if (!c.fits || c.total > kPackLimit) Fail("the budget with the world server's log does not fit");
    for (size_t i = 0; i < 5; ++i) if (PartLeftOut(p, c, i) || T835Kept(p[i], c, i) == 0) Fail("a log was cut to nothing: part " + I((int)i));
    if (T835Kept(p[1], c, 1) + 8000u < p[1].floor) Fail("the world server's log was cut more than one segment under its floor");
    /* every segment cut: the world server's log is reported as left out like the others */
    c = PlanBudget(p, fixed);
    if (!c.fits || !PartLeftOut(p, c, 1) || !PartLeftOut(p, c, 0)) Fail("the world server's log cut to nothing was not reported as left out");
    /* no world server's log: this launch's log gets the room that log's share would have had */
    std::vector<BudgetPart> q(p);
    q.erase(q.begin() + 1);
    SetBudgetFloors(&q, kPackLimit);
    unsigned long long qf = 0, qfixed = 0;
    for (size_t i = 0; i < q.size(); ++i) { qf += q[i].floor; qfixed += q[i].fixed; }
    if (qfixed + qf != kPackLimit || q[0].floor < (kPackLimit - qfixed) * 45u / 100u) Fail("without the world server's log this launch's floor shrank");
}
/* a log cut to nothing in pass 2 is reported as left out */
void t_t835_budget_left_out()
{
    using namespace coopbug;
    std::vector<BudgetPart> p(5);
    p[0].order = kOrderThisLog;  p[0].fixed = 10; p[0].segBytes.assign(2, 100); p[0].floor = 100;
    p[1].order = kOrderOlderLog; p[1].fixed = 10; p[1].segBytes.assign(2, 100); p[1].floor = 100;
    p[2].order = kOrderNearby;   /* a log with no segments at all */
    p[3].order = kOrderKeep;
    p[4].order = kOrderCrash;    p[4].fixed = 5; p[4].droppable = true;
    /* 425 over 150. pass 1: each log down to its floor (225 left), pass 2: the oldest log's last segment (125) */
    BudgetCut c = PlanBudget(p, 150);
    if (!c.fits || c.total != 125 || c.dropSegs[1] != 2 || c.dropped[1] != 0 || c.dropSegs[0] != 1 || c.dropped[4] != 0)
        Fail("pass 2 did not cut past the floor: " + I((int)c.dropSegs[1]) + " total " + I((int)c.total));
    if (!PartLeftOut(p, c, 1)) Fail("a log that lost every segment was not reported as left out");
    if (PartLeftOut(p, c, 0) || PartLeftOut(p, c, 2) || PartLeftOut(p, c, 3) || PartLeftOut(p, c, 4)) Fail("a kept part was reported as left out");
    c = PlanBudget(p, 10);   /* every segment and the crash file go */
    if (c.dropped[4] != 1 || !PartLeftOut(p, c, 4) || !PartLeftOut(p, c, 0) || PartLeftOut(p, c, 2)) Fail("a part left out whole was not reported");
}
void t_t461_zip_and_upload()
{
    using namespace coopbug;
    std::vector<ZipEntry> es;
    es.push_back(StoredEntry("report.txt", "hello"));
    ZipEntry d;
    const std::string body = T461LogText(50);
    d.name = "nearby/slot3-Anna/shared_wastelands_log.txt"; d.method = 8; d.rawLen = (unsigned int)body.size();
    d.crc = coopstore::Crc32(body.data(), body.size());
    CompressSegment((const unsigned char*)body.data(), body.size(), &d.data);
    d.data.push_back(kFinalBlock[0]); d.data.push_back(kFinalBlock[1]);
    es.push_back(d);
    const std::vector<unsigned char> z = BuildZip(es, DosTime(13, 20, 6), DosDate(2026, 10, 2));
    unsigned int v = 0;
    std::memcpy(&v, &z[0], 4); if (v != 0x04034B50u) Fail("no local header at 0");
    const size_t end = z.size() - kZipEndBytes;
    std::memcpy(&v, &z[end], 4); if (v != 0x06054B50u) Fail("no end record");
    unsigned short cnt = 0; std::memcpy(&cnt, &z[end + 10], 2); if (cnt != 2) Fail("the end record does not count two entries");
    unsigned int cdOff = 0, cdSize = 0; std::memcpy(&cdSize, &z[end + 12], 4); std::memcpy(&cdOff, &z[end + 16], 4);
    std::memcpy(&v, &z[cdOff], 4); if (v != 0x02014B50u || cdOff + cdSize != end) Fail("the central directory is not where the end record says");
    /* the second entry: its local header's sizes and CRC, and its data inflates to the text */
    const size_t at = 30 + 10 + 5;   /* first local header + "report.txt" + "hello" */
    std::memcpy(&v, &z[at], 4); if (v != 0x04034B50u) Fail("the second local header is not after the first entry");
    unsigned short method = 0; std::memcpy(&method, &z[at + 8], 2);
    unsigned int crc = 0, csize = 0, usize = 0; std::memcpy(&crc, &z[at + 14], 4); std::memcpy(&csize, &z[at + 18], 4); std::memcpy(&usize, &z[at + 22], 4);
    if (method != 8 || crc != d.crc || csize != d.data.size() || usize != body.size()) { Fail("the deflate entry's header is wrong"); return; }
    std::vector<unsigned char> data(z.begin() + (long)(at + 30 + d.name.size()), z.begin() + (long)(at + 30 + d.name.size() + csize));
    std::string back;
    if (!T461Inflate(data, &back) || back != body) Fail("the deflate entry does not inflate to its text");
    if (ZipEntryOverhead("report.txt") != 30u + 46u + 20u) Fail("the per-entry overhead");
    if (DosDate(2026, 10, 2) != ((46u << 9) | (10u << 5) | 2u) || DosTime(13, 20, 6) != ((13u << 11) | (20u << 5) | 3u)) Fail("DOS date / time");
    /* the upload: three fields, the zip as the file, a boundary found in no field */
    const std::string seed = "----B";
    const std::string bnd = PickBoundary(seed, "my words ----B", "info", z);
    if (bnd == seed) Fail("the boundary was not changed although the description holds it");
    const std::vector<unsigned char> m = BuildMultipart(bnd, "it broke", "Game: x", z);
    const std::string ms(m.begin(), m.end());
    if (ms.compare(0, bnd.size() + 4, "--" + bnd + "\r\n") != 0) Fail("the upload does not start with its boundary");
    if (ms.find("name=\"description\"") == std::string::npos || ms.find("it broke") == std::string::npos
        || ms.find("name=\"info\"") == std::string::npos || ms.find("name=\"report\"; filename=\"bug-report.zip\"") == std::string::npos) Fail("a field is missing");
    const std::string tail = "\r\n--" + bnd + "--\r\n";
    if (ms.size() < tail.size() || ms.compare(ms.size() - tail.size(), tail.size(), tail) != 0) Fail("the upload does not end with its closing boundary");
    if (ms.find(std::string(z.begin(), z.end())) == std::string::npos) Fail("the zip is not in the upload whole");
}
void t_t461_send_outcome()
{
    using namespace coopbug;
    if (FailKindOf(12007, 0) != kFailNoInternet || FailKindOf(12029, 0) != kFailNoInternet) Fail("a name not resolved / no connection is not the player's internet");
    if (FailKindOf(12002, 0) != kFailServer || FailKindOf(12030, 0) != kFailServer || FailKindOf(12152, 0) != kFailServer) Fail("a timeout / broken answer is not the server");
    if (FailKindOf(0, 502) != kFailServer || FailKindOf(0, 500) != kFailServer || FailKindOf(0, 599) != kFailServer) Fail("a 5xx is not the server");
    if (FailKindOf(0, 413) != kFailOther || FailKindOf(0, 404) != kFailOther || FailKindOf(12175, 0) != kFailOther) Fail("another failure is not 'error n'");
    if (SendTraceText(kStepReach, 15032, 15010, 0, 5590668) != "stopped while reaching the server (finding it, connecting, the secure handshake) after 15032 ms (15010 ms in that step); 0 of 5590668 bytes uploaded")
        Fail("the send trace of a server never reached does not read as written");
    if (SendTraceText(kStepUpload, 40000, 30000, 196608, 2986814).find("stopped while uploading after 40000 ms (30000 ms in that step); 196608 of 2986814 bytes uploaded") != 0)
        Fail("the send trace of an upload cut off does not name the step and the bytes");
    if (std::string(SendStepName(kStepOpen)) == SendStepName(kStepReach) || std::string(SendStepName(kStepUpload)) == SendStepName(kStepAnswer)
        || std::string(SendStepName(kStepAnswer)) == SendStepName(kStepRead) || std::string(SendStepName(99)) != "an unknown step")
        Fail("two send steps share a name, or an unknown step is not named as one");
    if (FailCodeOf(12175, 0) != 12175 || FailCodeOf(0, 413) != 413) Fail("the error number");
    /* decision 952 a: a 12175 send logs why the secure connection was refused */
    {
        const struct { unsigned long f; const char* text; } one[] = {
            { 0x1ul, "secure connection refused: the certificate's revocation could not be checked (flags 0x1)" },
            { 0x2ul, "secure connection refused: the certificate is invalid (flags 0x2)" },
            { 0x4ul, "secure connection refused: the certificate was revoked (flags 0x4)" },
            { 0x8ul, "secure connection refused: the certificate authority is not trusted (flags 0x8)" },
            { 0x10ul, "secure connection refused: the certificate is for another name (flags 0x10)" },
            { 0x20ul, "secure connection refused: the certificate is expired or not yet valid (flags 0x20)" },
            { 0x40ul, "secure connection refused: the certificate is not meant for this use (flags 0x40)" },
            { 0x80000000ul, "secure connection refused: the secure channel failed (flags 0x80000000)" },
        };
        for (size_t i = 0; i < sizeof(one) / sizeof(one[0]); ++i)
            if (SecureFailureText(one[i].f) != one[i].text) Fail("a single secure-failure flag does not read as written");
        if (SecureFailureText(0x8ul | 0x20ul) != "secure connection refused: the certificate authority is not trusted; the certificate is expired or not yet valid (flags 0x28)")
            Fail("two secure-failure flags are not both named, joined by '; '");
        if (SecureFailureText(0x100ul) != "secure connection refused: unknown reason 0x100 (flags 0x100)") Fail("an unknown secure-failure bit is not shown as hex");
        if (SecureFailureText(0x80000000ul | 0x1000ul) != "secure connection refused: the secure channel failed; unknown reason 0x1000 (flags 0x80001000)")
            Fail("a known and an unknown secure-failure bit together");
        if (SecureFailureText(0ul) != "secure connection refused: no reason given (flags 0x0)") Fail("no secure-failure flag is not 'no reason given'");
    }
    if (!SendSucceeded(0, 200) || !SendSucceeded(0, 204) || SendSucceeded(0, 302) || SendSucceeded(12002, 200)) Fail("success is not a 2xx answer");
}
void t_t461_crash_file_rule()
{
    using namespace coopbug;
    /* the previous log's last line at 1000; this launch at 5000 */
    if (!CrashFromLastLaunch(1005, 1000, 5000)) Fail("a crash file written as the previous launch ended is not attached");
    if (!CrashFromLastLaunch(995, 1000, 5000)) Fail("a crash file a little before the last log line is not attached");
    if (CrashFromLastLaunch(1000 - 601, 1000, 5000)) Fail("an older crash (an earlier launch) is attached");
    if (CrashFromLastLaunch(1000 + 601, 1000, 5000)) Fail("a crash long after the previous launch's last line is attached");
    if (CrashFromLastLaunch(5001, 5000, 5000)) Fail("a crash file written after this launch began is attached");
    if (CrashFromLastLaunch(1000, 0, 5000)) Fail("attached with no previous log");
    if (CrashFileKind("crashDump1.0.68_x64.zip") != 2 || CrashFileKind("CRASHDUMP1.0.65_X64.DMP") != 1 || CrashFileKind("crashDump1.0.65_x64.txt") != 0
        || CrashFileKind("kenshi_x64.exe.1234.dmp") != 0) Fail("crash file names");
}
/* T-651 (owner 581): which kept earlier launch a crash file ended */
void t_t651_crash_launch()
{
    using namespace coopbug;
    const long long n = (long long)kCrashNearSec;
    /* copy 2's last line at 1000, copy 1's at 3000, this launch at 5000 */
    if (CrashLaunch(3005, 5000, 3000, 1000) != 1) Fail("a crash file written as the previous launch ended is not launch 1's");
    if (CrashLaunch(1005, 5000, 3000, 1000) != 2) Fail("a crash file written as the launch two back ended is not launch 2's");
    if (CrashLaunch(2000, 5000, 3000, 1000) != 0) Fail("a crash file near neither kept launch's end was placed");
    /* the near window's edges */
    if (CrashLaunch(3000 + n, 5000, 3000, 1000) != 1 || CrashLaunch(3000 - n, 5000, 3000, 1000) != 1) Fail("launch 1's window edges are not inside it");
    if (CrashLaunch(3000 + n + 1, 5000, 3000, 1000) != 0 || CrashLaunch(3000 - n - 1, 5000, 3000, 1000) != 0) Fail("just outside launch 1's window was placed");
    if (CrashLaunch(1000 + n, 5000, 3000, 1000) != 2 || CrashLaunch(1000 - n, 5000, 3000, 1000) != 2) Fail("launch 2's window edges are not inside it");
    if (CrashLaunch(1000 + n + 1, 5000, 3000, 1000) != 0 || CrashLaunch(1000 - n - 1, 5000, 3000, 1000) != 0) Fail("just outside launch 2's window was placed");
    /* written after this launch began: never placed */
    if (CrashLaunch(5001, 5000, 4900, 1000) != 0) Fail("a crash file written after this launch began was placed");
    if (CrashLaunch(5000, 5000, 4900, 1000) != 1) Fail("a crash file written as this launch began is not launch 1's");
    if (CrashLaunch(1005, 1004, 0, 1000) != 0) Fail("with no copy 1, a crash file written after this launch began went to launch 2");
    /* launch 2's bound is copy 1's last line: a crash file after it is not launch 2's */
    if (CrashLaunch(1050, 5000, 900, 1000) != 1) Fail("a crash file after copy 1's last line went to launch 2");
    /* report R8's shape: launch 2 crashed at 1000; launch 1 was a short session (its last line at 1185) that ended cleanly */
    if (CrashLaunch(1002, 5000, 1185, 1000) != 2) Fail("a crash two launches back, before a short clean launch, went to the short launch");
    if (CrashLaunch(1180, 5000, 1185, 1000) != 1) Fail("with both launches near, the closer last line did not win");
    if (CrashLaunch(1100, 5000, 1200, 1000) != 1) Fail("a tie did not go to launch 1");
    /* absent copies */
    if (CrashLaunch(1005, 5000, 0, 0) != 0) Fail("a crash file was placed with no kept logs");
    if (CrashLaunch(3005, 5000, 3000, 0) != 1) Fail("launch 1 not found without copy 2");
    if (CrashLaunch(1005, 5000, 0, 1000) != 2) Fail("launch 2 not found without copy 1");
    if (CrashLaunch(0, 5000, 3000, 1000) != 0 || CrashLaunch(-5, 5000, 3000, 1000) != 0) Fail("a crash file with no time was placed");
    /* the previous launch's test is launch 1 of the new one */
    if (!CrashFromLastLaunch(3005, 3000, 5000) || CrashFromLastLaunch(1005, 3000, 5000)) Fail("CrashFromLastLaunch is not CrashLaunch's launch 1");
}
/* T-651 (owner 581): the two earlier launches' logs in the size budget */
void t_t651_earlier_log_order()
{
    using namespace coopbug;
    if (!(kOrderOlderLog < kOrderPrevLog && kOrderPrevLog < kOrderNearby && kOrderNearby < kOrderThisLog
          && kOrderThisLog < kOrderCrash && kOrderCrash < kOrderKeep)) Fail("the cut order");
    if (EarlierLogOrder(1, 0) != kOrderPrevLog || EarlierLogOrder(2, 0) != kOrderOlderLog) Fail("with no crash, the log from two launches ago is not cut first");
    if (EarlierLogOrder(1, 1) != kOrderPrevLog || EarlierLogOrder(2, 1) != kOrderOlderLog) Fail("a crash in the previous launch did not keep its log ahead");
    if (EarlierLogOrder(2, 2) != kOrderPrevLog || EarlierLogOrder(1, 2) != kOrderOlderLog) Fail("a crash two launches ago did not keep its log ahead of the previous launch's");
    /* this launch's log 100+50; the previous launch's 100+40+40; two launches ago 100+30+30: total 490 */
    std::vector<BudgetPart> parts(3);
    parts[0].order = kOrderThisLog; parts[0].fixed = 100; parts[0].segBytes.push_back(50);
    parts[1].fixed = 100; parts[1].segBytes.push_back(40); parts[1].segBytes.push_back(40);
    parts[2].fixed = 100; parts[2].segBytes.push_back(30); parts[2].segBytes.push_back(30);
    /* the crash ended launch 2: the previous launch's log goes first, whole segments, before launch 2's loses one */
    parts[1].order = EarlierLogOrder(1, 2); parts[2].order = EarlierLogOrder(2, 2);
    BudgetCut c = PlanBudget(parts, 420);
    if (!c.fits || c.dropSegs[1] != 2 || c.dropSegs[2] != 0 || c.dropSegs[0] != 0 || c.total != 410) Fail("the crashed launch's log was cut before the previous launch's");
    c = PlanBudget(parts, 380);
    if (!c.fits || c.dropSegs[1] != 2 || c.dropSegs[2] != 1 || c.dropSegs[0] != 0 || c.total != 380) Fail("the crashed launch's log was not next");
    /* no crash: the log from two launches ago goes first */
    parts[1].order = EarlierLogOrder(1, 0); parts[2].order = EarlierLogOrder(2, 0);
    c = PlanBudget(parts, 420);
    if (!c.fits || c.dropSegs[2] != 2 || c.dropSegs[1] != 1 || c.dropSegs[0] != 0 || c.total != 390) Fail("with no crash, the log from two launches ago was not cut first");
}
void t_t461_nearby_messages()
{
    using namespace coopbug;
    std::vector<char> b;
    unsigned int id = 0, mx = 0;
    LogAskEncode(&b, 77, kNearbyLogMax);
    if (b.size() != kLogAskBytes || !LogAskDecode(&b[0], b.size(), &id, &mx) || id != 77 || mx != kNearbyLogMax) Fail("LOG_ASK round trip");
    if (LogAskDecode(&b[0], 7, &id, &mx)) Fail("a short LOG_ASK was read");
    LogAskEncode(&b, 0, 5);
    if (LogAskDecode(&b[0], b.size(), &id, &mx)) Fail("ask id 0 was read");
    /* a bundle: name, sizes and two segments, through parts and back */
    LogPack p;
    p.rawTotal = 5000000000ull; p.rawDropped = 123; p.ipsScrubbed = 9;
    const std::string s1 = T461LogText(2000), s2 = T461Lcg(60000, 9u);
    Seg g1; g1.rawLen = (unsigned int)s1.size(); g1.crc = coopstore::Crc32(s1.data(), s1.size()); CompressSegment((const unsigned char*)s1.data(), s1.size(), &g1.bytes);
    Seg g2; g2.rawLen = (unsigned int)s2.size(); g2.crc = coopstore::Crc32(s2.data(), s2.size()); CompressSegment((const unsigned char*)s2.data(), s2.size(), &g2.bytes);
    p.segs.push_back(g1); p.segs.push_back(g2);
    const std::vector<char> bundle = BundleEncode("Anna", p);
    const unsigned int parts = PartCountFor((unsigned int)bundle.size());
    if (parts < 2) Fail("the test bundle is not more than one part");
    PartsJoin j;
    for (unsigned int k = parts; k-- > 0;)   /* out of order, and the last one twice */
    {
        std::vector<char> part;
        LogPartEncode(&part, 77, bundle, k);
        LogPart lp;
        if (!LogPartDecode(&part[0], part.size(), &lp) || lp.askId != 77 || lp.part != k || lp.parts != parts) { Fail("LOG_PART " + I((int)k) + " did not decode"); return; }
        if (!j.Add(lp)) Fail("a good part was refused");
        if (k == parts - 1 && !j.Add(lp)) Fail("a repeated part was refused");
        if (part.size() > kLogPartHeader + kLogPartMax || part.size() + 12 > 65536) Fail("a part is over the LIVE envelope's room");
    }
    if (!j.Complete() || j.data != bundle) Fail("the parts did not join into the bundle");
    std::string name; LogPack q;
    if (!BundleDecode(j.data, &name, &q) || name != "Anna" || q.rawTotal != p.rawTotal || q.rawDropped != 123 || q.ipsScrubbed != 9 || q.segs.size() != 2
        || q.segs[1].bytes != g2.bytes || q.segs[0].crc != g1.crc) { Fail("the bundle did not come back"); return; }
    std::vector<unsigned char> z; unsigned long long rl = 0; unsigned int crc = 0;
    JoinPack(q, 0, &z, &rl, &crc);
    std::string back;
    if (!T461Inflate(z, &back) || back != s1 + s2 || crc != coopstore::Crc32(back.data(), back.size())) Fail("a received log does not inflate with its CRC");
    /* refusals */
    std::vector<char> bad = bundle; bad[0] = 'X';
    if (BundleDecode(bad, &name, &q)) Fail("a wrong magic was read");
    bad = bundle; bad.pop_back();
    if (BundleDecode(bad, &name, &q)) Fail("a short bundle was read");
    std::vector<char> part;
    LogPartEncode(&part, 77, bundle, 0);
    LogPart lp;
    std::vector<char> wrong = part; std::memset(&wrong[8], 0x7F, 1);   /* the part count */
    if (LogPartDecode(&wrong[0], wrong.size(), &lp)) Fail("a part with the wrong count was read");
    if (LogPartDecode(&part[0], part.size() - 1, &lp)) Fail("a short part was read");
    PartsJoin j2;
    if (!LogPartDecode(&part[0], part.size(), &lp) || !j2.Add(lp)) Fail("part 0 again");
    LogPart other = lp; other.total = lp.total + 1;
    if (j2.Add(other)) Fail("a part of another shape joined");
    std::vector<char> empty, ep;
    LogPartEncode(&ep, 5, empty, 0);
    if (!LogPartDecode(&ep[0], ep.size(), &lp) || lp.parts != 1 || lp.len != 0) Fail("an empty answer is not one empty part");
    /* the wait */
    if (NearbyWaitOver(4999, 0, 0) || !NearbyWaitOver(5000, 0, 0)) Fail("nobody answering is not waited for 5 s");
    if (NearbyWaitOver(8000, 2, 1) || !NearbyWaitOver(8000, 2, 2) || !NearbyWaitOver(20000, 3, 0)) Fail("a started answer is not waited for, up to 20 s");
    /* the largest answer, paced, arrives inside the wait with time left for the answering game to compress its log */
    if (PartCountFor(kNearbyLogMax + 4096u) * kPartGapMs + kNearbyFirstMs > kNearbyTotalMs) Fail("the largest answer cannot arrive inside the wait");
    if (SafeName("A/b:c*") != "A-b-c-" || SafeName("") != "player" || SafeName(std::string(40, 'z')).size() != 24 || SafeName("x. ") != "x") Fail("folder names");
    if (cooplive::kInnerLogAsk != 203 || cooplive::kInnerLogPart != 204 || !cooplive::LiveInnerRoadLocal(203) || cooplive::LiveInnerAccepted(204))
        Fail("LOG_ASK / LOG_PART are not the store layer's own (never the session layer's)");
}
void t_t461_answer_manners()
{
    using namespace coopbug;
    /* a part goes only after the gap AND while the link is nearly idle */
    if (AnswerPartDue(kPartGapMs - 1, true, 0)) Fail("a part went before the gap");
    if (!AnswerPartDue(kPartGapMs, true, 0) || !AnswerPartDue(kPartGapMs, true, kAnswerQueueRoomBytes)) Fail("a part waited on an idle link");
    if (AnswerPartDue(kPartGapMs, true, kAnswerQueueRoomBytes + 1) || AnswerPartDue(60000, true, 5000000)) Fail("a part went on a busy link");
    if (!AnswerPartDue(kPartGapMs, false, 5000000)) Fail("a link that cannot say does not keep the plain gap");
    if (kAnswerQueueRoomBytes >= (long long)kLogPartMax) Fail("the idle mark lets a whole part wait behind another");
    /* an answer nobody waits for stops */
    if (AnswerExpired(kNearbyTotalMs - 1) || !AnswerExpired(kNearbyTotalMs)) Fail("an answer does not end when the asker stops waiting");
    /* one answer per asking slot a minute */
    if (!AskerMayBeAnswered(false, 0)) Fail("a first ask was refused");
    if (AskerMayBeAnswered(true, 0) || AskerMayBeAnswered(true, kAnswerSlotGapMs - 1)) Fail("a slot was answered twice inside a minute");
    if (!AskerMayBeAnswered(true, kAnswerSlotGapMs) || kAnswerSlotGapMs != 60000) Fail("a slot was not answered again after a minute");
    /* the refusal line at most every 10 s */
    if (!RefusalLogDue(false, 0) || RefusalLogDue(true, kRefusalLogGapMs - 1) || !RefusalLogDue(true, kRefusalLogGapMs) || kRefusalLogGapMs != 10000)
        Fail("the refusal line is not written at most every 10 s");
}

/* T-53 - THE ROUTER PORT OPENING'S PURE DECISIONS (src/common/upnpplan.h, the header upnp.cpp and ui.cpp compile). */
void t_t53_upnp_text_and_start()
{
    using namespace coopupnp;
    if (std::string(kProtocol) != "UDP")                                   Fail("the world server's one port is UDP");
    if (kWindowsLeaseSeconds != 0)                                         Fail("NATUPnP static entries take no lease: 0, removed at the end");
    if (kQuitWaitMs == 0 || kQuitWaitMs > 10000)                           Fail("the exit wait is bounded and short");
    if (Description(27016) != "Shared Wastelands world server (UDP 27016)") Fail("the entry's text names the mod, the protocol and the port");
    if (Description(1) != "Shared Wastelands world server (UDP 1)")        Fail("a one-digit port");
    if (Description(65535) != "Shared Wastelands world server (UDP 65535)") Fail("the highest port");
    if (!IsOurDescription(Description(7777)))                              Fail("our own text is recognised");
    if (!IsOurDescription("Shared Wastelands world server (UDP 1234)"))    Fail("our text on another port is still ours");
    if (IsOurDescription("Shared Wastelands"))                            Fail("a shorter text is not ours");
    if (IsOurDescription("shared wastelands world server (UDP 27016)"))    Fail("the prefix is compared exactly");
    if (IsOurDescription("Minecraft server"))                              Fail("another program's entry is not ours");
    if (IsOurDescription(""))                                              Fail("an empty description is not ours");
    if (!PortOk(1) || !PortOk(65535) || PortOk(0) || PortOk(65536) || PortOk(-5)) Fail("PortOk is 1..65535");
    if (!MapOnHostStart(1, false))                                         Fail("a newly started world server asks the router");
    if (MapOnHostStart(2, false))                                          Fail("a world server that already ran asked at its own start");
    if (MapOnHostStart(0, false))                                          Fail("no world server started here: nothing to forward");
    if (std::string(OutcomeWord(kOutNoRouter)) != "noRouter" || std::string(OutcomeWord(kOutMapped)) != "mapped"
     || std::string(OutcomeWord(kOutAlready)) != "already" || std::string(OutcomeWord(kOutFailed)) != "failed"
     || std::string(OutcomeWord(99)) != "other")                           Fail("OutcomeWord");
}

/* A WORLD KEPT RUNNING HOLDS ITS OWN ROUTER ENTRY (upnpplan.h MapOwner ... NoteOfOutcome, keeprunning.h RouterLine): one
   owner per world server, the game's entry handed over and never removed by it, and one plain window line per note. */
void t_t1158_router_owner()
{
    using namespace coopupnp;
    /* who holds it */
    if (MapOwner(true) != kOwnerServer || MapOwner(false) != kOwnerGame)  Fail("a world kept running holds its own entry; any other is the game's");
    for (int started = 0; started <= 2; ++started)
        for (int kept = 0; kept <= 1; ++kept)
            if (MapOnHostStart(started, kept != 0) != (started == 1 && kept == 0)) Fail("the game asks only for a new world server that is not kept running");
    if (!ServerMapsAtStart(true, false))                                   Fail("a world kept running asks the router at its start");
    if (ServerMapsAtStart(true, true))                                     Fail("--no-router: a world kept running left the router alone");
    if (ServerMapsAtStart(false, false) || ServerMapsAtStart(false, true)) Fail("a world server its game hosts never asks for itself");
    /* a kept world's ask takes only openings with a time limit; the game's own hosting keeps its permanent fallbacks */
    const bool keptOnly = TimeLimitedOnly(MapOwner(true)), gameOnly = TimeLimitedOnly(MapOwner(false));
    if (!keptOnly || gameOnly)                                             Fail("only a world kept running takes time-limited openings only");
    const bool offered725 = (swrouter::AddFaultNext(725, kAskLeaseSeconds) == swrouter::kAddRetryPermanent);
    if (!offered725)                                                       Fail("725 to a leased ask no longer offers the permanent re-ask");
    if (AskAgainPermanent(offered725, keptOnly))                           Fail("725 on a kept world's ask was asked again as permanent");
    if (!AskAgainPermanent(offered725, gameOnly))                          Fail("725 on the game's ask is no longer asked again as permanent");
    if (AskAgainPermanent(swrouter::AddFaultNext(725, 0) == swrouter::kAddRetryPermanent, gameOnly))
                                                                           Fail("725 to a permanent ask was asked again");
    if (NoteOfOutcome(FoldOutcome(kOutNoRouter, kRrRefused)) != kNoteFailed) Fail("a refused 725 does not end as failed (open by hand)");
    /* the windows route: the game's adds / keeps / leaves as before; a kept world's adds nothing and removes an entry of ours
       it finds (a permanent one the game handed over) */
    const std::string ourDesc = Description(27016);
    if (WindowsStep(kActAdd, std::string(), gameOnly) != kWinAdd || WindowsStep(kActKeep, ourDesc, gameOnly) != kWinKeep
     || WindowsStep(kActKeep, "made by hand", gameOnly) != kWinKeep || WindowsStep(kActLeave, ourDesc, gameOnly) != kWinLeave)
                                                                           Fail("the game's windows route changed");
    if (WindowsStep(kActAdd, std::string(), keptOnly) != kWinNoAdd)       Fail("a kept world added an entry with no time limit");
    if (WindowsStep(kActKeep, ourDesc, keptOnly) != kWinRemoveOurs)          Fail("a kept world left a handed-over entry with no time limit");
    if (WindowsStep(kActKeep, "made by hand", keptOnly) != kWinKeep || WindowsStep(kActLeave, ourDesc, keptOnly) != kWinLeave)
                                                                           Fail("a kept world touched an entry that is not ours");
    {
        int out = kOutNoRouter;                                            /* nothing answered any route */
        for (int rt = FirstRoute(); rt != kRouteNone; rt = NextRoute(rt, kRrNoAnswer)) out = FoldOutcome(out, kRrNoAnswer);
        if (NoteOfOutcome(out) != kNoteNoRouter)                           Fail("a kept world no router answered does not show the no-router line");
        out = FoldOutcome(FoldOutcome(kOutNoRouter, kRrNoAnswer), kRrRefused);   /* only the windows route answered: nothing added */
        if (NoteOfOutcome(out) != kNoteFailed)                             Fail("a router that offers only permanent openings does not show the failed line");
    }
    if (GrantIsTimeLimited(0) || GrantIsTimeLimited(kLifeNoLimit) || !GrantIsTimeLimited(1) || !GrantIsTimeLimited(3600))
                                                                           Fail("GrantIsTimeLimited");
    if (GrantKept(0, keptOnly) || GrantKept(0xFFFFFFFFul, keptOnly))       Fail("a lifetime with no limit was kept as a kept world's opening");
    if (!GrantKept(3600, keptOnly) || !GrantKept(0, gameOnly) || !GrantKept(0xFFFFFFFFul, gameOnly))
                                                                           Fail("the game keeps any granted lifetime; a kept world a limited one");
    const unsigned long asked = (unsigned long)kAskLeaseSeconds;
    if (!GrantKept(asked, keptOnly) || !GrantKept(asked - 1, keptOnly))    Fail("a kept world refused a lifetime no longer than asked");
    if (GrantKept(asked + 1, keptOnly) || GrantIsTimeLimited(asked + 1))   Fail("a kept world kept a lifetime longer than asked");
    if (!GrantKept(asked + 1, gameOnly))                                   Fail("the game refused a lifetime longer than asked");
    if (!RenewRefusedShowsFailed(keptOnly, kRouteIgd, kRrRefused, false))  Fail("a kept world's refused igd renewal left the open line");
    if (RenewRefusedShowsFailed(keptOnly, kRouteIgd, kRrRefused, true))    Fail("the failed line was shown twice for one opening");
    if (RenewRefusedShowsFailed(gameOnly, kRouteIgd, kRrRefused, false))   Fail("the game showed a renewal line");
    if (RenewRefusedShowsFailed(keptOnly, kRouteIgd, kRrMapped, false) || RenewRefusedShowsFailed(keptOnly, kRouteIgd, kRrNoAnswer, false)
     || RenewRefusedShowsFailed(keptOnly, kRouteIgd, kRrElsewhere, false)) Fail("a renewal that was not refused showed the failed line");
    if (!RemoveLeftoverNow(true, kActKeep, Description(27016), kRrRefused)) Fail("our entry a kept world could not lease again was not removed");
    if (RemoveLeftoverNow(false, kActKeep, Description(27016), kRrRefused)) Fail("the game removed our entry at a refused add");
    if (RemoveLeftoverNow(true, kActKeep, "made by hand", kRrRefused))      Fail("an entry made by hand was removed");
    if (!RemoveLeftoverNow(true, kActKeep, Description(27016), kRrElsewhere)) Fail("our entry answered 718 at its re-ask was kept");
    if (RemoveLeftoverNow(true, kActKeep, Description(27016), kRrMapped) || RemoveLeftoverNow(true, kActAdd, std::string(), kRrRefused)
     || RemoveLeftoverNow(true, kActLeave, Description(27016), kRrRefused) || RemoveLeftoverNow(false, kActKeep, Description(27016), kRrElsewhere))
                                                                           Fail("RemoveLeftoverNow removed what is not a leftover of ours");
    /* the lease the router lists (read back after an add or renewal, or at a renewal answered 718) */
    if (LeaseKept(true, true, 0) || LeaseKept(true, true, kAskLeaseSeconds + 1)) Fail("a kept world kept a listed lease of none or over the hour");
    if (!LeaseKept(true, true, kAskLeaseSeconds) || !LeaseKept(true, true, 1) || !LeaseKept(true, false, 0))
                                                                           Fail("a kept world handed back a lease within the hour, or one it could not read");
    if (!LeaseKept(false, true, 0) || !LeaseKept(false, true, kAskLeaseSeconds + 1)) Fail("the game read back a lease");
    /* after a hand-back: nothing held, no stop line */
    {
        State kh;
        HostStart(&kh, 27016);
        AfterAsk(&kh, 27016, kOutFailed, false, kRoutePcp, 0, kBackDone);
        if (kh.held != 0 || NextStep(kh) != kStepIdle)                       Fail("an ask whose opening was handed back holds it");
        HostEnd(&kh);
        if (NextStep(kh) != kStepIdle || NotClosedNote(kh.held != 0, EndLease(kh)) != 0) Fail("a handed-back opening has a stop line");
        State kp;
        HostStart(&kp, 27016);
        AfterAsk(&kp, 27016, kOutMapped, true, kRoutePcp, 3600, kBackNone);
        if (kp.held != 27016 || kp.noLimit || kp.lease != 3600)               Fail("AfterAsk with nothing handed back is AfterMap");
        AfterRenewResult(&kp, kRenewHandedBack, 0);
        if (kp.held != 0 || kp.removeAtEnd || NextStep(kp) != kStepIdle)      Fail("a renewal handed back left the port held");
        HostEnd(&kp);
        if (NextStep(kp) != kStepIdle)                                      Fail("the stop after a renewal hand-back removes or forgets something");
    }
    /* not handed back: held only to be removed - the removal asked again at renewal time and at the stop, never "not ours" */
    {
        State kf;
        HostStart(&kf, 27016);
        AfterAsk(&kf, 27016, kOutFailed, true, kRouteWindows, 0, kBackFailed);
        if (kf.held != 27016 || !kf.removeAtEnd || !kf.noLimit || kf.route != kRouteWindows || kf.lease <= 0)
                                                                           Fail("an opening not handed back is not held to be removed");
        if (NoteOfOutcome(kOutFailed) != kNoteFailed)                      Fail("an opening not handed back counted as opened");
        kf.renewDue = true;
        if (NextStep(kf) != kStepRenew)                                     Fail("the removal of an opening not handed back is not asked again");
        if (RenewOfHandBack(kNoteNotRemoved) != kRenewNotHandedBack || RenewOfHandBack(kNoteRemoved) != kRenewHandedBack
         || RenewOfHandBack(kNoteGone) != kRenewHandedBack || RenewOfHandBack(kNoteLeft) != kRenewHandedBack) Fail("RenewOfHandBack");
        AfterRenewResult(&kf, RenewOfHandBack(kNoteNotRemoved), 0);
        if (kf.held != 27016 || !kf.removeAtEnd || !kf.noLimit || kf.renewDue) Fail("a removal asked again and refused let the opening go");
        if (NotClosedNote(true, EndLease(kf)) != kNoteNotRemovedNoLimit)    Fail("a stuck opening with no time limit says it closes by itself");
        HostEnd(&kf);
        if (NextStep(kf) != kStepUnmap || !MayRemove(kf, 27016))             Fail("the stop does not remove an opening not handed back");
        if (NoteOfEnd(true, kNoteNotRemoved, EndLease(kf)) != kNoteNotRemovedNoLimit) Fail("the stop's note for it says it closes by itself");
        AfterUnmap(&kf);
        if (kf.noLimit || NextStep(kf) != kStepIdle)                         Fail("AfterUnmap left the no-limit mark");
        State kq;
        HostStart(&kq, 27016);
        AfterAsk(&kq, 27016, kOutMapped, true, kRouteIgd, 3600, kBackNone);
        AfterRenewResult(&kq, kRenewNotHandedBack, 0);
        if (kq.held != 27016 || !kq.removeAtEnd || !kq.noLimit)               Fail("a renewal not handed back is not held to be removed");
        AfterRenewResult(&kq, RenewOfHandBack(kNoteRemoved), 0);
        if (kq.held != 0 || kq.noLimit)                                      Fail("a removal asked again and taken still holds the port");
        State kl;
        HostStart(&kl, 27016);
        AfterAsk(&kl, 27016, kOutMapped, true, kRouteIgd, 3600, kBackNone);
        AfterRenewResult(&kl, kRenewLost, 0);
        if (kl.held != 27016 || kl.removeAtEnd || kl.lease != 0)              Fail("AfterRenewResult lost is AfterRenewLost");
    }
    /* the hand-over: the entry on the kept world's port is forgotten, never removed; one on another port is removed */
    State s;
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutMapped, true, kRouteIgd, 3600);
    HandOver(&s, 27016);
    if (s.want != 0 || s.held != 27016 || s.removeAtEnd)                   Fail("the entry on the kept world's port was not left for it");
    if (NextStep(s) != kStepForget)                                        Fail("a handed-over entry is forgotten, not removed");
    AfterUnmap(&s);
    if (NextStep(s) != kStepIdle)                                          Fail("after the hand-over the game does nothing more");
    State o;
    HostStart(&o, 27016);
    AfterMap(&o, 27016, kOutMapped, true, kRoutePcp, 3600);
    HandOver(&o, 27020);
    if (o.want != 0 || !o.removeAtEnd || NextStep(o) != kStepUnmap)        Fail("an entry on another port than the kept world's was not removed");
    State n;
    HandOver(&n, 27016);
    if (NextStep(n) != kStepIdle)                                          Fail("a game holding nothing has nothing to hand over");
    /* the game's ask for that port was still on its router thread at the hand-over: it lands after, and is never removed */
    State a;
    HostStart(&a, 27016);
    const State asking = a;                                                /* the router thread's look: it asks */
    if (NextStep(asking) != kStepMap)                                      Fail("the game asks for its world server's port");
    HandOver(&a, 27016);
    AfterMap(&a, 27016, kOutMapped, true, kRouteIgd, 3600);
    if (a.held != 27016 || a.removeAtEnd || NextStep(a) != kStepForget)   Fail("an ask that landed after the hand-over was set to be removed");
    if (MayRemove(a, 27016))                                               Fail("an ask that landed after the hand-over may be removed");
    AfterUnmap(&a);
    if (NextStep(a) != kStepIdle)                                          Fail("after a late ask is forgotten the game does nothing more");
    /* a world server the game starts again on that port (the world kept running there stopped) is the game's own again */
    HostStart(&a, 27016);
    AfterMap(&a, 27016, kOutMapped, true, kRouteIgd, 3600);
    HostEnd(&a);
    if (NextStep(a) != kStepUnmap || !MayRemove(a, 27016))                 Fail("a later world server's entry on a once handed-over port was not removed");
    /* the game's removal of that port was chosen (a world server before it ended) when the hand-over came: the last look,
       under the port's lock, does not send it - and the world kept running asks under that same lock */
    State b;
    HostStart(&b, 27016);
    AfterMap(&b, 27016, kOutMapped, true, kRouteIgd, 3600);
    HostEnd(&b);
    const State removing = b;                                              /* the router thread's look: it removes */
    if (NextStep(removing) != kStepUnmap || !MayRemove(b, 27016))          Fail("an ended world server's entry is removed");
    HandOver(&b, 27016);
    if (MayRemove(b, 27016))                                               Fail("a removal chosen before the hand-over was sent after it");
    /* the world kept running could not be started: the hand-over is undone and the removal chosen before it is sent again */
    HandBack(&b, 27016);
    if (b.handedOver != 0 || !b.removeAtEnd || NextStep(b) != kStepUnmap || !MayRemove(b, 27016))
        Fail("an undone hand-over did not give the removal back");
    State f;                                                               /* the router thread had already forgotten it */
    HostStart(&f, 27016);
    AfterMap(&f, 27016, kOutMapped, true, kRoutePcp, 3600);
    HostEnd(&f);
    HandOver(&f, 27016);
    AfterUnmap(&f);
    HandBack(&f, 27016);
    if (f.held != 27016 || f.route != kRoutePcp || f.lease != 3600 || !MayRemove(f, 27016))
        Fail("an undone hand-over did not take back an entry already forgotten");
    State l;                                                               /* an ask that landed after the hand-over */
    HostStart(&l, 27016);
    HandOver(&l, 27016);
    AfterMap(&l, 27016, kOutMapped, true, kRouteIgd, 3600);
    HandBack(&l, 27016);
    if (!MayRemove(l, 27016))                                              Fail("an undone hand-over did not give back an ask that landed after it");
    State m;                                                               /* an entry not this game's to remove stays so */
    HostStart(&m, 27016);
    AfterMap(&m, 27016, kOutAlready, false, kRouteIgd, 0);
    HandOver(&m, 27016);
    AfterUnmap(&m);
    HandBack(&m, 27016);
    if (m.held != 27016 || m.removeAtEnd || NextStep(m) != kStepForget)    Fail("an undone hand-over made a hand-made entry removable");
    State w;                                                               /* nothing held; an undo for another port */
    HandOver(&w, 27016);
    HandBack(&w, 27020);
    if (w.handedOver != 27016)                                             Fail("an undo for another port undid this one");
    HandBack(&w, 27016);
    if (w.handedOver != 0 || NextStep(w) != kStepIdle)                     Fail("an undone hand-over with nothing held did something");
    if (NoteOfEnd(false, 0, 3600) != kNoteLeft)                            Fail("a removal not sent is noted as left");
    if (PortLockName(27016) != PortLockName(27016) || PortLockName(27016) == PortLockName(27017))
        Fail("the port's lock is one per port, the same name in the game and the world server");
    if (PortLockName(27016).compare(0, 6, "Local\\") != 0)                 Fail("the port's lock is this user session's");
    /* a hand-made entry the world kept running found there: kept, never removed - its stop says it is left open */
    State h;
    HostStart(&h, 27016);
    AfterMap(&h, 27016, kOutAlready, RemoveAtEnd(kActKeep, "my own game server"), kRouteIgd, 0);
    HostEnd(&h);
    if (NextStep(h) != kStepForget || MayRemove(h, 27016))                 Fail("a hand-made entry was set to be removed");
    if (NoteOfEnd(false, 0, h.lease) != kNoteLeft)                         Fail("a hand-made entry's stop is not noted as left open");
    /* a removal sent: what it came to; one not accepted says whether the entry ends by itself (the lease it was granted) */
    if (NoteOfEnd(true, kNoteRemoved, 3600) != kNoteRemoved || NoteOfEnd(true, kNoteGone, 0) != kNoteGone)  Fail("NoteOfEnd removed / gone");
    if (NoteOfEnd(true, kNoteNotRemoved, 3600) != kNoteNotRemoved)         Fail("a leased entry not closed");
    if (NoteOfEnd(true, kNoteNotRemoved, 0) != kNoteNotRemovedNoLimit)     Fail("an entry with no time limit not closed");
    if (NotRemovedNote(true, 0) != kNoteNotRemovedNoLimit || NotRemovedNote(true, 1800) != kNoteNotRemoved
     || NotRemovedNote(false, 0) != kNoteNotRemoved)                       Fail("NotRemovedNote");
    /* the window and the stop: the ask's line only while the world runs, the removal's until the stop's wait ended */
    for (int note = kNoteMapped; note <= kNoteNotRemovedNoLimit; ++note)
    {
        const bool removal = (note >= kNoteRemoved);
        if (IsRemovalNote(note) != removal)                                Fail("IsRemovalNote");
        if (!NoteShown(note, kStopRunning))                                Fail("a router line not shown while the world runs");
        if (NoteShown(note, kStopBegun) != removal)                        Fail("the ask's line shown after the stop began, or the removal's not");
        if (NoteShown(note, kStopWaited))                                  Fail("a router line shown after the stop's wait");
    }
    if (!NotClosedAtWaitEnd(false, false, kNoteNotRemoved) || !NotClosedAtWaitEnd(false, false, kNoteNotRemovedNoLimit)
     || NotClosedAtWaitEnd(true, false, kNoteNotRemoved) || NotClosedAtWaitEnd(false, true, kNoteNotRemoved))
        Fail("the did-not-close line at the end of the stop's wait");
    /* the wait ran out while the start-up ask was still in progress: no opening is known, so no did-not-close line */
    if (NotClosedNote(false, 0) != 0 || NotClosedNote(false, 3600) != 0 || NotClosedAtWaitEnd(false, false, NotClosedNote(false, 0)))
        Fail("a did-not-close line for an opening never known to be held");
    if (NotClosedNote(true, 0) != kNoteNotRemovedNoLimit || NotClosedNote(true, 3600) != kNoteNotRemoved)
        Fail("the did-not-close line does not follow the lease granted");
    /* a second stop door never moves the stop back */
    if (StopStageAfter(kStopRunning, kStopBegun) != kStopBegun || StopStageAfter(kStopBegun, kStopWaited) != kStopWaited
     || StopStageAfter(kStopWaited, kStopBegun) != kStopWaited || StopStageAfter(kStopBegun, kStopBegun) != kStopBegun)
        Fail("the stop's stage moved backwards");
    /* the server's stop: an entry it added is removed (the same State steps as the game's quit) */
    State v;
    HostStart(&v, 27016);
    AfterMap(&v, 27016, kOutAlready, RemoveAtEnd(kActKeep, Description(27016)), kRouteIgd, 3600);
    HostEnd(&v);
    if (NextStep(v) != kStepUnmap)                                         Fail("a crashed run's leftover the next start kept was not removed at its stop");
    /* the notes */
    if (NoteOfOutcome(kOutMapped) != kNoteMapped || NoteOfOutcome(kOutAlready) != kNoteAlready
     || NoteOfOutcome(kOutNoRouter) != kNoteNoRouter || NoteOfOutcome(kOutFailed) != kNoteFailed
     || NoteOfOutcome(kOutNone) != kNoteFailed)                            Fail("NoteOfOutcome");
    /* the window's words: one line each, naming the port and the protocol, nothing for an unknown note */
    for (int note = kNoteMapped; note <= kNoteNotRemovedNoLimit; ++note)
    {
        const std::string line = keeprun::RouterLine(note, 27016);
        if (line.empty() || line.find("port 27016 (UDP)") == std::string::npos) Fail("a router line does not name the port and the protocol");
        for (int other = kNoteMapped; other < note; ++other)
            if (keeprun::RouterLine(other, 27016) == line)                 Fail("two router notes have the same line");
    }
    if (!keeprun::RouterLine(0, 27016).empty() || !keeprun::RouterLine(99, 27016).empty()) Fail("an unknown note wrote a line");
    if (keeprun::RouterLine(kNoteRemoved, 7) != "The router's opening on port 7 (UDP) is closed.") Fail("the removal's line");
    /* every line word for word */
    const char* const words[] =
    {
        "The router is now open on port 27016 (UDP): players can join from the internet.",
        "The router was already open on port 27016 (UDP): players can join from the internet.",
        "The router did not open port 27016 (UDP). Players from the internet may not get in - open that port on the router by hand.",
        "No router answered, so port 27016 (UDP) was not opened. Players from the internet may not get in - open that port on the router by hand.",
        "The router's opening on port 27016 (UDP) is closed.",
        "The router's opening on port 27016 (UDP) had already closed.",
        "The router did not close port 27016 (UDP). It closes by itself within an hour, or the next time this world stops.",
        "The router's opening on port 27016 (UDP) was not made by this world server, so it is left open.",
        "The router did not close port 27016 (UDP). Close it on the router by hand, or it closes the next time this world stops."
    };
    for (int note = kNoteMapped; note <= kNoteNotRemovedNoLimit; ++note)
        if (keeprun::RouterLine(note, 27016) != words[note - kNoteMapped]) Fail("router line " + keeprun::RouterLine(note, 27016));
}

/* routerport= in shared_wastelands.cfg: the HOST press asks the router unless the file says 0 (the harness's default). */
void t_t53_routerport_cfg_key()
{
    if (coopcfg::CfgFields().routerPort != 1) Fail("routerport defaults to 1 (the router is asked)");
    {
        coopcfg::CfgFields o; long long bad = -1, unk = -1;
        coopcfg::CfgParseText("role=host\r\nhost=0.0.0.0:27016\r\n", &o, 0, 0, &unk, &bad);
        if (o.routerPort != 1 || bad != 0 || unk != 0) Fail("a file with no routerport line asks the router");
    }
    const char* offs[] = { "0", "off", "OFF", "false", "no", " 0 " };
    for (size_t i = 0; i < sizeof(offs) / sizeof(offs[0]); ++i)
    {
        coopcfg::CfgFields o; long long bad = -1;
        coopcfg::CfgParseText(std::string("routerport=") + offs[i] + "\r\n", &o, 0, 0, 0, &bad);
        if (o.routerPort != 0 || bad != 0) Fail(std::string("routerport=") + offs[i] + " did not switch the router off");
    }
    const char* ons[] = { "1", "on", "true", "Yes" };
    for (size_t i = 0; i < sizeof(ons) / sizeof(ons[0]); ++i)
    {
        coopcfg::CfgFields o; long long bad = -1;
        coopcfg::CfgParseText(std::string("routerport=") + ons[i] + "\r\n", &o, 0, 0, 0, &bad);
        if (o.routerPort != 1 || bad != 0) Fail(std::string("routerport=") + ons[i] + " did not leave the router on");
    }
    const char* junk[] = { "2", "maybe", "" };
    for (size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); ++i)
    {
        coopcfg::CfgFields o; long long bad = -1;
        coopcfg::CfgParseText(std::string("routerport=") + junk[i] + "\r\n", &o, 0, 0, 0, &bad);
        if (o.routerPort != 1 || bad != 1) Fail(std::string("routerport='") + junk[i] + "' must be one bad line and leave the router on");
    }
    {
        coopcfg::CfgFields o;
        coopcfg::CfgParseText("routerport=0\r\nrouterport=1\r\n", &o, 0, 0, 0, 0);
        if (o.routerPort != 1) Fail("the last routerport line wins");
    }
    {
        coopcfg::CfgFields f; f.routerPort = 0;
        if (coopcfg::CfgFormat(f).find("routerport") != std::string::npos) Fail("the panel's writer never writes routerport");
        coopcfg::CfgFields b;
        coopcfg::CfgParseText(coopdata::SessionCfgText(f), &b, 0, 0, 0, 0);
        if (b.routerPort != 1) Fail("session.cfg carries routerport");
    }
    {
        coopcfg::CfgFields o;
        coopcfg::CfgParseText(coopdata::ArmSettingsText(false, "role=host\r\nhost=0.0.0.0:27016\r\n",
                                                        "# shared_wastelands.cfg - written by tools/write-cfg.ps1\r\nrouterport=0\r\n"), &o, 0, 0, 0, 0);
        if (o.routerPort != 0) Fail("the test file's routerport=0 is lost at the panel's re-arm");
    }
}

void t_t53_upnp_find_and_existing()
{
    using namespace coopupnp;
    const long kOk = 0, kErr = (long)0x80004005L;   /* S_OK, E_FAIL */
    if (FindRouterOutcome(true, true, true, kOk, true) != kOutNone)        Fail("a router answered: go on");
    if (FindRouterOutcome(false, true, true, kOk, true) != kOutNoRouter)   Fail("no home-network address: no router to ask");
    if (FindRouterOutcome(true, false, true, kOk, true) != kOutFailed)     Fail("COM not ready is a failure, not a missing router");
    if (FindRouterOutcome(true, true, false, kErr, false) != kOutFailed)   Fail("Windows' UPnP missing is a failure");
    if (FindRouterOutcome(true, true, true, kOk, false) != kOutNoRouter)   Fail("S_OK with no collection: nobody answered the search");
    if (FindRouterOutcome(true, true, true, kErr, false) != kOutNoRouter)  Fail("the search could not run (discovery off): no router");
    if (FindRouterOutcome(true, true, true, kErr, true) != kOutNoRouter)   Fail("an error HRESULT is never read as a router");

    const std::string me = "192.168.1.20";
    if (ClassifyExisting(false, "", 0, me, 27016) != kExistNone)                       Fail("nothing there");
    if (ClassifyExisting(true, me, 27016, me, 27016) != kExistThisTarget)              Fail("already here, same port");
    if (ClassifyExisting(true, "192.168.1.21", 27016, me, 27016) != kExistOtherTarget) Fail("another computer holds the port");
    if (ClassifyExisting(true, me, 27017, me, 27016) != kExistOtherTarget)             Fail("this computer, another internal port");
    if (ClassifyExisting(true, "", 0, me, 27016) != kExistOtherTarget)                 Fail("an entry that will not say where it goes is not ours");
    if (ClassifyExisting(true, "", 27016, "", 27016) != kExistOtherTarget)             Fail("no address of ours never matches");
    if (ActionFor(kExistNone) != kActAdd || ActionFor(kExistThisTarget) != kActKeep || ActionFor(kExistOtherTarget) != kActLeave)
        Fail("none -> add, here -> keep, elsewhere -> leave (never overwritten)");
    if (!RemoveAtEnd(kActAdd, ""))                                         Fail("an entry we added is removed");
    if (!RemoveAtEnd(kActKeep, Description(27016)))                        Fail("a crashed game's leftover entry is taken over and removed");
    if (RemoveAtEnd(kActKeep, "my manual forward"))                        Fail("the player's own router entry is never removed");
    if (RemoveAtEnd(kActLeave, Description(27016)))                        Fail("an entry pointing elsewhere is never removed");
}

/* T-510 (owner 438-440) - THE INTERNET ADDRESS ROW: the router's address decides found / not found (private and shared ranges
   are not internet addresses), the mask hides the address but not the port, the row's sentences and buttons per state, the
   note's words, and what COPY says - a failed internet copy never prints the address. */
void t_t510_internet_address()
{
    struct Case { const char* ip; bool pub; const char* kind; };
    static const Case cases[] = {
        { "203.0.113.7", true, "public" }, { "81.2.3.4", true, "public" }, { "8.8.8.8", true, "public" }, { "223.255.255.255", true, "public" },
        { "10.0.0.1", false, "private" }, { "10.255.255.255", false, "private" }, { "172.16.0.1", false, "private" }, { "172.31.255.254", false, "private" },
        { "172.15.0.1", true, "public" }, { "172.32.0.1", true, "public" }, { "192.168.1.25", false, "private" }, { "192.169.0.1", true, "public" },
        { "100.64.0.1", false, "shared (carrier-grade NAT)" }, { "100.127.255.255", false, "shared (carrier-grade NAT)" },
        { "100.63.255.255", true, "public" }, { "100.128.0.1", true, "public" },
        { "0.0.0.0", false, "this-network" }, { "127.0.0.1", false, "loopback" }, { "169.254.1.1", false, "link-local" },
        { "224.0.0.1", false, "multicast or reserved" }, { "255.255.255.255", false, "multicast or reserved" },
        { "", false, "unreadable" }, { "1.2.3", false, "unreadable" }, { "1.2.3.4.5", false, "unreadable" }, { "256.1.1.1", false, "unreadable" },
        { "1.2.3.", false, "unreadable" }, { ".1.2.3", false, "unreadable" }, { "1..2.3", false, "unreadable" }, { "1.2.3.4 ", false, "unreadable" },
        { "a.b.c.d", false, "unreadable" }, { "1234.1.1.1", false, "unreadable" }, { "1.2.3.4:27016", false, "unreadable" }, { "::1", false, "unreadable" } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        std::string kind;
        const bool pub = coopupnp::RouterAddressPublic(cases[i].ip, &kind);
        if (pub != cases[i].pub || kind != cases[i].kind)
            Fail(std::string("T-510: '") + cases[i].ip + "' read as " + (pub ? "public" : "not public") + " (" + kind + ")");
        if (coopupnp::RouterAddressPublic(cases[i].ip, 0) != cases[i].pub) Fail(std::string("T-510: kind 0 changed the answer for ") + cases[i].ip);
    }
    /* the mask: the port shown, no digit of the address */
    if (coopui::PanelNetAddrMask(27016) != "***.***.***.***:27016") Fail("T-510: the mask: " + coopui::PanelNetAddrMask(27016));
    const std::string ip = "203.0.113.7";
    if (coopui::PanelNetAddrLine(coopui::kNetAddrFound, ip, 27016, 0, 0) != "***.***.***.***:27016"
     || coopui::PanelNetAddrLine(coopui::kNetAddrFound, ip, 27016, 1, 0) != "203.0.113.7:27016"
     || coopui::PanelNetAddrLine(coopui::kNetAddrAsking, "", 27016, 0, 0) != "***.***.***.***:27016"
     || coopui::PanelNetAddrLine(coopui::kNetAddrAsking, "", 27016, 0, coopui::kNetPressShow) != "Finding your internet address..."
     || coopui::PanelNetAddrLine(coopui::kNetAddrMissing, "", 27016, 0, 0) != "***.***.***.***:27016"
     || coopui::PanelNetAddrLine(coopui::kNetAddrLooking, "", 27016, 0, coopui::kNetPressCopy) != "Finding your internet address..."
     || coopui::PanelNetAddrLine(coopui::kNetAddrFailed, "", 27016, 1, 0) != "Couldn't find your internet address."
     || coopui::PanelNetAddrLine(coopui::kNetAddrFound, "", 27016, 1, 0) != "Couldn't find your internet address.")
        Fail("T-524: the INTERNET ADDRESS row's value per state");
    for (int st = 0; st <= coopui::kNetAddrFailed; ++st)
        for (int pend = 0; pend <= coopui::kNetPressCopy; ++pend)
            if (coopui::PanelNetAddrLine(st, ip, 27016, 0, pend).find("203") != std::string::npos) Fail("T-524: the hidden row shows the address in state " + I(st));
    if (coopui::PanelNetAddrButtonsOn(coopui::kNetAddrFound) != 1 || coopui::PanelNetAddrButtonsOn(coopui::kNetAddrAsking) != 1
     || coopui::PanelNetAddrButtonsOn(coopui::kNetAddrMissing) != 1 || coopui::PanelNetAddrButtonsOn(coopui::kNetAddrLooking) != 1
     || coopui::PanelNetAddrButtonsOn(coopui::kNetAddrFailed) != 0)
        Fail("T-524: SHOW / COPY are greyed exactly once no website answered");
    if (coopui::PanelNetAddrHave(coopui::kNetAddrFound, ip) != 1 || coopui::PanelNetAddrHave(coopui::kNetAddrFound, "") != 0
     || coopui::PanelNetAddrHave(coopui::kNetAddrLooking, ip) != 0)
        Fail("T-524: an address in hand only when found");
    if (coopui::PanelNetAddrShowCaption(coopui::kNetAddrFound, ip, 0) != "SHOW" || coopui::PanelNetAddrShowCaption(coopui::kNetAddrFound, ip, 1) != "HIDE"
     || coopui::PanelNetAddrShowCaption(coopui::kNetAddrLooking, "", 1) != "SHOW") Fail("T-524: SHOW / HIDE");
    /* COPY's words (owner 440) */
    const std::string netText = coopui::PanelHostCopyText(ip, 27016);
    if (coopui::PanelCopyNote(0, 1, "192.168.1.25:27016") != "Address copied."
     || coopui::PanelCopyNote(1, 1, netText) != "Address copied."
     || coopui::PanelCopyNote(0, 0, "192.168.1.25:27016") != "Couldn't copy. Your address is 192.168.1.25:27016."
     || coopui::PanelCopyNote(1, 0, netText) != "Couldn't copy."
     || coopui::PanelCopyNote(0, 0, "") != "There is no address to copy." || coopui::PanelCopyNote(1, 0, "") != "There is no address to copy.")
        Fail("T-510: COPY's words");
    static const char* const ips[] = { "203.0.113.7", "81.2.3.4", "1.1.1.1", "255.255.255.254" };
    for (size_t i = 0; i < sizeof(ips) / sizeof(ips[0]); ++i)
        for (int port = 1; port <= 65535; port += 4369)
            for (int copied = 0; copied <= 1; ++copied)
            {
                const std::string t = coopui::PanelHostCopyText(ips[i], port);
                const std::string note = coopui::PanelCopyNote(1, copied, t);
                if (note.find(ips[i]) != std::string::npos || note.find(t) != std::string::npos || note.find(coopui::PanelNum((long long)port)) != std::string::npos)
                    Fail("T-510: an internet COPY message prints the address: " + note);
            }
    if (coopui::PanelHostCopyText(ip, 27016) != "203.0.113.7:27016") Fail("T-510: the internet COPY copies <ip>:<port>");
    /* the address shown and copied is rebuilt from its four numbers (review fold 3) */
    {
        std::string kind, clean;
        if (!coopupnp::RouterAddressPublic("08.8.8.8", &kind, &clean) || clean != "8.8.8.8") Fail("T-510: 08.8.8.8 -> '" + clean + "'");
        if (!coopupnp::RouterAddressPublic("203.000.113.007", &kind, &clean) || clean != "203.0.113.7") Fail("T-510: 203.000.113.007 -> '" + clean + "'");
        if (coopupnp::RouterAddressPublic("010.0.0.1", &kind, &clean) || kind != "private" || clean != "10.0.0.1") Fail("T-510: 010.0.0.1 -> '" + clean + "' " + kind);
        if (coopupnp::RouterAddressPublic("1.2.3", &kind, &clean) || !clean.empty()) Fail("T-510: unreadable gives no clean text");
        coopupnp::RouterAddressPublic("223.255.255.255", &kind, &clean);
        if (clean != "223.255.255.255") Fail("T-510: a three-digit address kept: " + clean);
    }
    /* the row fits (review fold 6): at 1280x720 and 800x600 (and 1920x1080), with the panel's own fallback character width,
       each address button holds SHOW / HIDE / COPY, the value holds the address / mask on one line and each sentence in at
       most two, and two lines fit the row's two-row height */
    {
        const int sizes[][2] = { { 1280, 720 }, { 800, 600 }, { 1920, 1080 } };
        const char* const caps[] = { "SHOW", "HIDE", "COPY" };
        for (int q = 0; q < 3; ++q)
        {
            const coopui::PanelGeom g = coopui::PanelGeomIn(sizes[q][0], sizes[q][1]);
            const int gap = coopui::PanelGapOf(g.rowH);
            const int bw = coopui::PanelAddrBtnW(g.innerW);
            const int vw = coopui::PanelNetAddrValueW(g.innerW, gap);
            const std::string at = I(sizes[q][0]) + "x" + I(sizes[q][1]);
            for (int k = 0; k < 3; ++k)
                if ((int)std::strlen(caps[k]) * coopui::kPanelTextCharW + 2 * coopui::kPanelAddrBtnPadPx > bw)
                    Fail("T-510: " + std::string(caps[k]) + " does not fit its button (" + I(bw) + " px) at " + at);
            const int labelEnd = coopui::PanelLabelBoxIn(g.innerW, gap, coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, g.rowH), 0, 0).boxX;
            if (labelEnd + vw + 2 * (bw + gap) > g.innerW || vw < 1) Fail("T-510: the INTERNET ADDRESS row does not fit across at " + at);
            if ((int)std::string("INTERNET ADDRESS").size() * coopui::kPanelTextCharW > labelEnd - gap) Fail("T-510: the label does not fit at " + at);
            const int per = vw / coopui::kPanelTextCharW;
            const std::string one[] = { coopui::PanelNetAddrMask(65535), coopui::PanelHostCopyText("255.255.255.255", 65535) };
            for (int k = 0; k < 2; ++k)
            {
                const std::string w = coopui::PanelWrapText(one[k], per);
                if ((int)one[k].size() * coopui::kPanelTextCharW > vw || coopui::PanelNetAddrHalves(w) != 2)
                    Fail("T-510: '" + one[k] + "' is not one line in " + I(vw) + " px at " + at);
            }
            const std::string two[] = { coopui::PanelNetAddrLine(coopui::kNetAddrFailed, "", 1, 0, 0), coopui::PanelNetAddrLine(coopui::kNetAddrLooking, "", 1, 0, 0) };
            for (int k = 0; k < 2; ++k)
            {
                const std::string w = coopui::PanelWrapText(two[k], per);
                size_t start = 0;
                int lines = 0;
                while (start <= w.size())
                {
                    size_t e = w.find('\n', start);
                    if (e == std::string::npos) e = w.size();
                    if ((int)(e - start) * coopui::kPanelTextCharW > vw) Fail("T-510: a line of '" + two[k] + "' is wider than the value at " + at);
                    ++lines;
                    start = e + 1;
                }
                coopui::PanelLive v;
                v.net = coopui::PanelNetAddrHalves(w);
                const coopui::PanelBand b = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, g.rowH, v);
                if (lines > 2 || b.h < lines * coopui::kPanelTextLinePx)
                    Fail("T-510: '" + two[k] + "' needs " + I(lines) + " lines; the row is " + I(b.h) + " px at " + at);
                if (coopui::PanelWrapText(w, 1000) != w && w.find('\n') == std::string::npos) Fail("T-510: wrap changed one line");
            }
        }
        if (coopui::PanelWrapText("a bb ccc", 10) != "a bb ccc" || coopui::PanelWrapText("aaaa bbbb cccc", 10) != "aaaa bbbb\ncccc")
            Fail("T-510: PanelWrapText: '" + coopui::PanelWrapText("aaaa bbbb cccc", 10) + "'");
        coopui::PanelLive one;
        one.net = 2;
        if (coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotNetAddr, 34, one).h != coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, 34).h)
            Fail("T-510: an address makes the INTERNET ADDRESS row one row, like LOCAL ADDRESS");
    }
}

void t_t346_place_input_height()
{
    using namespace placeinput;
    float y = 123.0f;
    /* the copy that landed one ground height too high: live 3059.0 over ground 1527.0 -> input 1532.0 */
    if (InputYFromLive(3059.0f, 1, 1527.0f, &y) != 1 || y != 1532.0f)          Fail("live less ground");
    /* created again at that input, the piece stands at the same live height (the key's height) */
    if (y + 1527.0f != 3059.0f)                                                 Fail("round trip");
    y = 123.0f;
    if (InputYFromLive(3059.0f, 0, kNoGround, &y) != 0 || y != 123.0f)         Fail("no terrain answered");
    if (InputYFromLive(3059.0f, -1, 0.0f, &y) != 0 || y != 123.0f)             Fail("no row / faulted");
    if (InputYFromLive(3059.0f, 1, kNoGround, &y) != 0 || y != 123.0f)         Fail("-99 is no ground even when read");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (InputYFromLive(3059.0f, 1, nan, &y) != 0 || y != 123.0f)               Fail("NaN ground");
    if (InputYFromLive(nan, 1, 1527.0f, &y) != 0 || y != 123.0f)               Fail("NaN live");
    /* a piece below the ground height (a cellar floor) keeps a negative input */
    if (InputYFromLive(1500.0f, 1, 1527.0f, &y) != 1 || y != -27.0f)          Fail("below ground");
}

void t_t346_record_file_names()
{
    using namespace recname;
    /* the world server's folder: <id>.platoon for every record (store_main.cpp FileOf) */
    if (ServerDataFile("C:\\w", "zone.20.32") != "C:\\w\\zone.20.32.platoon")           Fail("server area file");
    if (ServerDataFile("C:\\w", "Nameless_0") != "C:\\w\\Nameless_0.platoon")           Fail("server squad file");
    /* a game's mirror: an area record under the engine's own area file name */
    if (MirrorDataFile("C:\\w\\mirror-7", "zone.20.32", true) != "C:\\w\\mirror-7\\zone.20.32.zone")         Fail("mirror area file");
    if (MirrorDataFile("C:\\w\\mirror-7", "Nameless_0", false) != "C:\\w\\mirror-7\\Nameless_0.platoon")     Fail("mirror squad file");
    if (!IsAreaId("zone.20.32") || IsAreaId("Nameless_0") || IsAreaId("zon"))                          Fail("area id");
}

void t_t53_upnp_worker_steps()
{
    using namespace coopupnp;
    State s;
    if (NextStep(s) != kStepIdle)                                          Fail("not hosting: idle");
    HostStart(&s, 0);
    if (s.want != 0 || NextStep(s) != kStepIdle)                           Fail("port 0 is not hosting");
    /* a host, mapped, then the world server stops */
    HostStart(&s, 27016);
    if (NextStep(s) != kStepMap)                                           Fail("hosting starts: ask");
    AfterMap(&s, 27016, kOutMapped, true);
    if (s.held != 27016 || NextStep(s) != kStepIdle)                       Fail("mapped: held, nothing more to do");
    HostEnd(&s);
    if (NextStep(s) != kStepUnmap)                                         Fail("hosting ended: remove");
    AfterUnmap(&s);
    if (s.held != 0 || NextStep(s) != kStepIdle)                           Fail("removed: idle");
    /* no router: asked once per world server start, not again and again */
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutNoRouter, false);
    if (s.held != 0 || NextStep(s) != kStepIdle)                           Fail("no router: not asked again for the same world server");
    HostEnd(&s);
    if (NextStep(s) != kStepIdle)                                          Fail("nothing held: nothing to remove");
    HostStart(&s, 27016);
    if (NextStep(s) != kStepMap)                                           Fail("a new world server start asks again");
    AfterMap(&s, 27016, kOutFailed, true);
    if (s.removeAtEnd || NextStep(s) != kStepIdle)                         Fail("failed: not held, nothing to remove");
    /* the player's own entry: kept, and only forgotten at the end */
    HostEnd(&s);
    HostStart(&s, 7777);
    AfterMap(&s, 7777, kOutAlready, false);
    HostEnd(&s);
    if (NextStep(s) != kStepForget)                                        Fail("an entry not ours is forgotten, never removed");
    AfterUnmap(&s);
    if (NextStep(s) != kStepIdle)                                          Fail("forgotten: idle");
    /* a new world server on another port while the first is still held: the old entry goes first, then the new port */
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutMapped, true);
    HostStart(&s, 27020);
    if (NextStep(s) != kStepUnmap)                                         Fail("another port: the old entry is removed first");
    AfterUnmap(&s);
    if (NextStep(s) != kStepMap || s.want != 27020)                        Fail("then the new port is asked");
    /* the same port again (a new world server process while the entry is still held): nothing to redo */
    AfterMap(&s, 27020, kOutMapped, true);
    HostStart(&s, 27020);
    if (NextStep(s) != kStepIdle)                                          Fail("the same port already held: idle");
    /* the game quits while an attempt is under way: the attempt lands, then it is removed */
    State q;
    HostStart(&q, 27016);
    const long asked = q.want;
    HostEnd(&q);   /* the quit arrives on the main thread during the attempt */
    AfterMap(&q, asked, kOutMapped, true);
    if (NextStep(q) != kStepUnmap)                                         Fail("an entry that landed after the quit began is removed");
    AfterUnmap(&q);
    if (NextStep(q) != kStepIdle)                                          Fail("quit: nothing left to do");
    if (std::string(StepWord(kStepUnmap)) != "unmap" || std::string(StepWord(kStepForget)) != "forget") Fail("StepWord");
}

/* ================= T-597 / T-576: THE HOME ADAPTER AND THE MOD'S OWN ROUTER ASK ================= */
/* Every router answer below is a SYNTHETIC sample written for these tests in the shape the protocols define (UDA 1.1,
   IGD WANIPConnection, RFC 6886, RFC 6887); none was recorded from a real router. */

/* The home adapter: the one Windows sends internet traffic through, never a VPN (18 of 56 player reports picked
   'Radmin VPN', and every router ask through it failed). */
void t_t597_home_adapter_pick()
{
    struct VpnCase { const char* name; const char* desc; const char* kind; };
    static const VpnCase vc[] = {
        { "Radmin VPN", "Famatech Radmin VPN Ethernet Adapter", "Radmin" },
        { "Ethernet 3", "LogMeIn Hamachi Virtual Ethernet Adapter", "Hamachi" },
        { "ZeroTier One [8056c2e21c000001]", "ZeroTier Virtual Port", "ZeroTier" },
        { "Tailscale", "Tailscale Tunnel", "Tailscale" },
        { "wg0", "WireGuard Tunnel", "WireGuard" },
        { "Local Area Connection", "TAP-Windows Adapter V9", "OpenVPN" },
        { "OpenVPN Data Channel Offload", "OpenVPN Data Channel Offload", "OpenVPN" },
        { "NordLynx", "NordLynx Tunnel", "NordVPN" },
        { "Ethernet 4", "PANGP Virtual Ethernet Adapter Secure", "GlobalProtect" },
        { "Ethernet 5", "Cisco AnyConnect Secure Mobility Client Virtual Miniport Adapter for Windows x64", "AnyConnect" },
        { "Ethernet 6", "Fortinet SSL VPN Virtual Ethernet Adapter", "VPN" },
        { "Wi-Fi", "Intel(R) Wi-Fi 6 AX201 160MHz", 0 },
        { "Ethernet", "Realtek PCIe GbE Family Controller", 0 },
        { "vEthernet (WSL)", "Hyper-V Virtual Ethernet Adapter", 0 },
        { "Ethernet 2", "VirtualBox Host-Only Ethernet Adapter", 0 } };
    for (size_t i = 0; i < sizeof(vc) / sizeof(vc[0]); ++i)
    {
        const char* k = coopui::PanelVpnKind(vc[i].name, vc[i].desc);
        if ((k == 0) != (vc[i].kind == 0) || (k != 0 && std::string(k) != vc[i].kind))
            Fail(std::string("T-597: PanelVpnKind('") + vc[i].name + "', '" + vc[i].desc + "') = " + (k ? k : "(none)"));
    }
    if (coopui::PanelVpnKind("RADMIN VPN", "") == 0) Fail("T-597: the VPN marks ignore case");

    /* The players' case: Radmin first in Windows' order, up, Ethernet, with a gateway; the real Wi-Fi is the internet route. */
    coopui::PanelNic radmin; radmin.ifType = coopui::kNicEthernet; radmin.up = 1; radmin.hasGateway = 1; radmin.vpn = 1;
    radmin.ipv4.push_back(0x1A000005ul);   /* 26.0.0.5 */
    coopui::PanelNic wifi; wifi.ifType = coopui::kNicWifi; wifi.up = 1; wifi.hasGateway = 1; wifi.best = 1;
    wifi.ipv4.push_back(0xC0A80114ul);     /* 192.168.1.20 */
    std::vector<coopui::PanelNic> nics;
    nics.push_back(radmin); nics.push_back(wifi);
    int picked = -1, why = -1;
    std::string got = coopui::PanelPickHomeNic(nics, &picked, &why);
    if (got != "192.168.1.20" || picked != 1 || why != coopui::kPickBestRoute) Fail("T-597: the internet route wins over Radmin listed first (" + got + ")");
    /* Windows named no route: Radmin is still skipped, the Wi-Fi with a router is picked */
    nics[1].best = 0;
    got = coopui::PanelPickHomeNic(nics, &picked, &why);
    if (got != "192.168.1.20" || picked != 1 || why != coopui::kPickGateway) Fail("T-597: a VPN is never picked by the router rule (" + got + ")");
    /* a full-tunnel VPN carries the internet route: it is still skipped */
    nics[0].best = 1;
    got = coopui::PanelPickHomeNic(nics, &picked, &why);
    if (got != "192.168.1.20" || picked != 1 || why != coopui::kPickGateway) Fail("T-597: a VPN holding the internet route is skipped (" + got + ")");
    /* only a VPN: nothing */
    nics.pop_back();
    if (coopui::PanelPickHomeNic(nics, &picked, &why) != "" || picked != -1 || why != coopui::kPickNone) Fail("T-597: a VPN alone gives no address");
    /* the internet route on another kind of adapter (a mobile modem, type 243) is taken */
    coopui::PanelNic wwan; wwan.ifType = 243; wwan.up = 1; wwan.hasGateway = 1; wwan.best = 1; wwan.ipv4.push_back(0x0A400001ul);
    coopui::PanelNic eth; eth.ifType = coopui::kNicEthernet; eth.up = 1; eth.hasGateway = 1; eth.ipv4.push_back(0xC0A80002ul);
    nics.clear(); nics.push_back(eth); nics.push_back(wwan);
    got = coopui::PanelPickHomeNic(nics, &picked, &why);
    if (got != "10.64.0.1" || picked != 1 || why != coopui::kPickBestRoute) Fail("T-597: the internet route of any kind wins (" + got + ")");
    /* ... but never loopback or a tunnel, nor a best route that is down or has no usable address */
    nics[1].ifType = coopui::kNicTunnel;
    got = coopui::PanelPickHomeNic(nics, &picked, &why);
    if (got != "192.168.0.2" || picked != 0 || why != coopui::kPickGateway) Fail("T-597: a tunnel is never the home network (" + got + ")");
    nics[1].ifType = coopui::kNicWifi; nics[1].up = 0;
    if (coopui::PanelPickHomeNic(nics, &picked, &why) != "192.168.0.2") Fail("T-597: a down adapter is never picked");
    nics[1].up = 1; nics[1].ipv4[0] = 0xA9FE0101ul;
    if (coopui::PanelPickHomeNic(nics, &picked, &why) != "192.168.0.2") Fail("T-597: a best route with only 169.254 is passed over");
    /* no router anywhere: the first Ethernet / Wi-Fi */
    coopui::PanelNic bare; bare.ifType = coopui::kNicEthernet; bare.up = 1; bare.ipv4.push_back(0xC0A80A0Aul);
    nics.clear(); nics.push_back(bare);
    if (coopui::PanelPickHomeNic(nics, &picked, &why) != "192.168.10.10" || why != coopui::kPickAny) Fail("T-597: no router anywhere");
    /* the old entry point gives the same pick */
    if (coopui::PanelPickHomeAddr(nics, &picked) != "192.168.10.10" || picked != 0) Fail("T-597: PanelPickHomeAddr agrees");
    if (std::string(coopui::PanelPickWhyText(coopui::kPickBestRoute)).find("internet traffic") == std::string::npos) Fail("T-597: why words");
}

/* ================= T-631: HOSTING's RADMIN / HAMACHI ADDRESS ROW (owner 570) ================= */
/* Adapter descriptions are the drivers' names as Windows lists them; the addresses are made up for these tests. */
static coopui::PanelVpnNic T631Nic(const char* desc, int up, unsigned long a)
{
    coopui::PanelVpnNic n;
    n.desc = desc;
    n.up = up;
    if (a != 0) n.ipv4.push_back(a);
    return n;
}
void t_t631_game_vpn_find()
{
    std::vector<coopui::PanelVpnNic> v;
    std::string ip = "x";
    const coopui::PanelVpnNic wifi = T631Nic("Intel(R) Wi-Fi 6 AX201 160MHz", 1, 0xC0A80114ul);        /* 192.168.1.20 */
    const coopui::PanelVpnNic eth  = T631Nic("Realtek PCIe GbE Family Controller", 1, 0x0A000002ul);   /* 10.0.0.2 */
    /* neither VPN */
    v.push_back(wifi); v.push_back(eth);
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnNone || !ip.empty()) Fail("T-631: no VPN adapter, yet a VPN (" + ip + ")");
    if (coopui::PanelGameVpnFind(std::vector<coopui::PanelVpnNic>(), 0) != coopui::kGameVpnNone) Fail("T-631: an empty adapter list");
    /* Radmin by its description */
    v.clear(); v.push_back(wifi); v.push_back(T631Nic("Famatech Radmin VPN Ethernet Adapter", 1, 0x1A0B0C0Dul));   /* 26.11.12.13 */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnRadmin || ip != "26.11.12.13") Fail("T-631: Radmin by its description (" + ip + ")");
    v[1].ipv4[0] = 0x0A090807ul;   /* 10.9.8.7: the description alone names it */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnRadmin || ip != "10.9.8.7") Fail("T-631: Radmin by its description alone (" + ip + ")");
    v[1].desc = "FAMATECH RADMIN VPN ETHERNET ADAPTER";
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnRadmin) Fail("T-631: the description is read without case");
    /* Radmin by its range */
    v.clear(); v.push_back(eth); v.push_back(T631Nic("Ethernet adapter #2", 1, 0x1A000005ul));   /* 26.0.0.5 */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnRadmin || ip != "26.0.0.5") Fail("T-631: Radmin by its range (" + ip + ")");
    /* Hamachi by its description, then by its range */
    v.clear(); v.push_back(wifi); v.push_back(T631Nic("LogMeIn Hamachi Virtual Ethernet Adapter", 1, 0x19010203ul));   /* 25.1.2.3 */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnHamachi || ip != "25.1.2.3") Fail("T-631: Hamachi by its description (" + ip + ")");
    v.clear(); v.push_back(wifi); v.push_back(T631Nic("Ethernet adapter #3", 1, 0x19636363ul));   /* 25.99.99.99 */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnHamachi || ip != "25.99.99.99") Fail("T-631: Hamachi by its range (" + ip + ")");
    /* both: RADMIN is shown, whichever Windows lists first */
    v.clear();
    v.push_back(T631Nic("LogMeIn Hamachi Virtual Ethernet Adapter", 1, 0x19010203ul));
    v.push_back(wifi);
    v.push_back(T631Nic("Famatech Radmin VPN Ethernet Adapter", 1, 0x1A0B0C0Dul));
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnRadmin || ip != "26.11.12.13") Fail("T-631: with both, Radmin first (" + ip + ")");
    /* a switched-off adapter is not in use; an adapter with only 169.254 has no address to show */
    v[2].up = 0;
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnHamachi || ip != "25.1.2.3") Fail("T-631: a switched-off Radmin is passed over (" + ip + ")");
    v.clear(); v.push_back(T631Nic("Famatech Radmin VPN Ethernet Adapter", 1, 0xA9FE0101ul));   /* 169.254.1.1 */
    if (coopui::PanelGameVpnFind(v, &ip) != coopui::kGameVpnNone || !ip.empty()) Fail("T-631: a Radmin adapter with only 169.254 (" + ip + ")");
    /* the words: the log's, the row's label, owner 570's note */
    if (std::string(coopui::PanelGameVpnWord(coopui::kGameVpnRadmin)) != "radmin" || std::string(coopui::PanelGameVpnWord(coopui::kGameVpnHamachi)) != "hamachi"
        || std::string(coopui::PanelGameVpnWord(coopui::kGameVpnNone)) != "none") Fail("T-631: the log words");
    if (std::string(coopui::PanelGameVpnLabel(coopui::kGameVpnRadmin)) != "RADMIN ADDRESS"
        || std::string(coopui::PanelGameVpnLabel(coopui::kGameVpnHamachi)) != "HAMACHI ADDRESS") Fail("T-631: the row labels");
    /* the layout: HOSTING with a VPN found has the row straight under LOCAL ADDRESS, as tall, INTERNET ADDRESS straight under
       it; 22 half-rows at its ceilings, as HOSTING's; HOSTING without one has no such row and LOCAL ADDRESS stays where it was */
    if (coopui::PanelLayoutOf(5, 0, 0, 1) != coopui::kLayHostingVpn || coopui::PanelLayoutOf(5, 0, 0, 0) != coopui::kLayHosting
        || coopui::PanelLayoutOf(1, 0, 0, 1) != coopui::kLayHost || coopui::PanelLayoutOf(2, 0, 1, 1) != coopui::kLayJoinHint)
        Fail("T-631: the screen -> layout map");
    if (coopui::PanelLayIsHosting(coopui::kLayHosting) == 0 || coopui::PanelLayIsHosting(coopui::kLayHostingVpn) == 0
        || coopui::PanelLayIsHosting(coopui::kLayHost) != 0) Fail("T-631: PanelLayIsHosting");
    if (coopui::PanelLayoutHalves(coopui::kLayHostingVpn) != 22)
        Fail("T-631: HOSTING with the VPN row is " + I(coopui::PanelLayoutHalves(coopui::kLayHostingVpn)) + " half-rows at its ceilings");
    const int rowHs[] = { 14, 27, 34, 36, 52, 60 };
    for (size_t r = 0; r < sizeof(rowHs) / sizeof(rowHs[0]); ++r)
    {
        const int rh = rowHs[r];
        const coopui::PanelBand hb = coopui::PanelSlotBand(coopui::kLayHostingVpn, coopui::kSlotHomeAddr, rh);
        const coopui::PanelBand vb = coopui::PanelSlotBand(coopui::kLayHostingVpn, coopui::kSlotVpnAddr, rh);
        const coopui::PanelBand nb = coopui::PanelSlotBand(coopui::kLayHostingVpn, coopui::kSlotNetAddr, rh);
        const coopui::PanelBand h0 = coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotHomeAddr, rh);
        if (vb.top != hb.top + hb.h || vb.h != hb.h || nb.top != vb.top + vb.h)
            Fail("T-631: the VPN row is not one LOCAL ADDRESS row between it and INTERNET ADDRESS at rowH " + I(rh));
        if (h0.top != hb.top || h0.h != hb.h) Fail("T-631: LOCAL ADDRESS moved with the VPN row at rowH " + I(rh));
        if (coopui::PanelSlotBand(coopui::kLayHosting, coopui::kSlotVpnAddr, rh).h != 0) Fail("T-631: HOSTING without a VPN has its row");
    }
    /* HOSTING with a VPN found keeps INTERNET ADDRESS's two rows for a wrapped sentence, and has no router help */
    if (coopui::PanelSlotBand(coopui::kLayHostingVpn, coopui::kSlotNetAddr, 2).h != 4)
        Fail("T-631: HOSTING with the VPN row - INTERNET ADDRESS's ceiling or a router help row");
}

/* SSDP: the search text, reading answers, the URLs. */
void t_t597_ssdp_answers()
{
    using namespace swrouter;
    const std::string m = SsdpSearchText(SsdpTarget(0));
    if (m != "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
             "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n") Fail("T-597: the M-SEARCH text: " + m);
    if (std::string(SsdpTarget(3)) != "urn:schemas-upnp-org:service:WANPPPConnection:1" || kSsdpTargetCount != 4) Fail("T-597: the search targets");
    if (kSsdpWaitMs != 3000u || kSsdpResendMs >= kSsdpWaitMs || kSsdpTtl != 2) Fail("T-597: the search's cap is MX + 1 s, the resend inside it");

    /* a synthetic router answer (shape of UDA 1.1 section 1.3.3) */
    const std::string ans = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                            "USN: uuid:3ddcd1d3-2380-45f5-b069-aabbccddeeff::urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                            "EXT:\r\nSERVER: Linux/3.14 UPnP/1.0 MiniUPnPd/2.1\r\nLOCATION: http://192.168.1.1:1900/rootDesc.xml\r\n\r\n";
    std::string loc, st, why;
    if (!SsdpAnswerRead(ans, "192.168.1.1", &loc, &st, &why) || loc != "http://192.168.1.1:1900/rootDesc.xml"
        || st != "urn:schemas-upnp-org:device:InternetGatewayDevice:1") Fail("T-597: a router's answer read: " + loc + " / " + why);
    const std::string lower = "HTTP/1.1 200 OK\r\nst: urn:schemas-upnp-org:service:WANIPConnection:1\r\nlocation:   http://10.0.0.138:49152/gatedesc.xml  \r\n\r\n";
    if (!SsdpAnswerRead(lower, "10.0.0.138", &loc, &st, &why) || loc != "http://10.0.0.138:49152/gatedesc.xml") Fail("T-597: header names without case");
    if (!SsdpAnswerRead("HTTP/1.1 200\r\nLOCATION: http://10.0.0.1/d.xml\r\n\r\n", "10.0.0.1", &loc, 0, 0)) Fail("T-597: a status line with no reason");
    if (SsdpAnswerRead(ans, "192.168.1.50", &loc, &st, &why) || why.find("another computer") == std::string::npos) Fail("T-597: a LOCATION on another device is refused: " + why);
    if (SsdpAnswerRead("HTTP/1.1 404 Not Found\r\nLOCATION: http://192.168.1.1/x.xml\r\n\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: a 404 is no answer");
    if (SsdpAnswerRead("HTTP/1.1 2000 OK\r\nLOCATION: http://192.168.1.1/x.xml\r\n\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: 2000 is not 200");
    if (SsdpAnswerRead("NOTIFY * HTTP/1.1\r\nLOCATION: http://192.168.1.1/x.xml\r\n\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: a NOTIFY is no answer");
    if (SsdpAnswerRead("HTTP/1.1 200 OK\r\nST: x\r\n\r\n", "192.168.1.1", 0, 0, &why) || why != "no LOCATION") Fail("T-597: no LOCATION");
    if (SsdpAnswerRead("HTTP/1.1 200 OK\r\nLOCATION: https://192.168.1.1/x.xml\r\n\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: https is refused");
    if (SsdpAnswerRead("HTTP/1.1 200 OK\r\nLOCATION: http://router.lan/x.xml\r\n\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: a name is refused");
    if (SsdpAnswerRead("HTTP/1.1 200 OK\r\n\r\nLOCATION: http://192.168.1.1/x.xml\r\n", "192.168.1.1", 0, 0, &why)) Fail("T-597: a LOCATION after the headers' end is not read");

    std::string host, path;
    long port = 0;
    if (!HttpUrlRead("http://192.168.1.1:5000/ctl/IPConn", &host, &port, &path) || host != "192.168.1.1" || port != 5000 || path != "/ctl/IPConn") Fail("T-597: URL with a port");
    if (!HttpUrlRead("HTTP://10.0.0.1", &host, &port, &path) || port != 80 || path != "/") Fail("T-597: URL with no port or path");
    if (!HttpUrlRead("http://010.000.000.001/x", &host, &port, &path) || host != "10.0.0.1") Fail("T-597: the host is rebuilt from its numbers");
    const char* bad[] = { "http://192.168.1.1:0/x", "http://192.168.1.1:65536/x", "http://user@192.168.1.1/x", "http://[fe80::1]/x",
                          "http://192.168.1/x", "ftp://192.168.1.1/x", "http://192.168.1.1:80 /x", "http://192.168.1.1/a b", "" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        if (HttpUrlRead(bad[i], &host, &port, &path)) Fail(std::string("T-597: refused URL read: ") + bad[i]);
    std::string out;
    if (!UrlResolve("http://192.168.1.1:1900/rootDesc.xml", "/ctl/IPConn", &out) || out != "http://192.168.1.1:1900/ctl/IPConn") Fail("T-597: an absolute path: " + out);
    if (!UrlResolve("http://192.168.1.1:1900/rootDesc.xml", "upnp/control/WANIPConn1", &out) || out != "http://192.168.1.1:1900/upnp/control/WANIPConn1") Fail("T-597: a path with no slash: " + out);
    if (!UrlResolve("http://192.168.1.1/d.xml", "http://192.168.1.1:5431/c", &out) || out != "http://192.168.1.1:5431/c") Fail("T-597: an absolute URL stays");
    if (UrlResolve("http://192.168.1.1/d.xml", "https://192.168.1.1/c", &out) || UrlResolve("http://192.168.1.1/d.xml", "", &out)) Fail("T-597: unusable control URLs");
}

/* The device description: the port-forwarding service and its control URL. */
void t_t597_device_description()
{
    using namespace swrouter;
    /* synthetic, in the nesting a typical home router uses: root device > WANDevice > WANConnectionDevice > services */
    const std::string desc =
        "<?xml version=\"1.0\"?>\r\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
        "<device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType><friendlyName>Home Router</friendlyName>"
        "<serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
        "<serviceId>urn:upnp-org:serviceId:L3Forwarding1</serviceId><controlURL>/ctl/L3F</controlURL></service></serviceList>"
        "<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType>"
        "<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType>"
        "<controlURL>/ctl/CmnIfCfg</controlURL></service></serviceList>"
        "<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>"
        "<serviceList><service>\r\n  <serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>\r\n"
        "  <serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>\r\n  <controlURL>/ctl/IPConn</controlURL>\r\n"
        "  <eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL></service></serviceList>"
        "</device></deviceList></device></deviceList></device></root>\r\n";
    std::string ctl, svc, why;
    if (!IgdControlRead(desc, "http://192.168.1.1:1900/rootDesc.xml", &ctl, &svc, &why) || ctl != "http://192.168.1.1:1900/ctl/IPConn"
        || svc != "urn:schemas-upnp-org:service:WANIPConnection:1") Fail("T-597: the WANIPConnection control URL: " + ctl + " / " + why);
    if (ServiceShort(svc) != "WANIPConnection:1") Fail("T-597: ServiceShort");
    /* URLBase, a prefixed namespace, both IP and PPP services: IGD2's WANIPConnection:2 wins; a path with no slash */
    const std::string desc2 =
        "<?xml version=\"1.0\"?><r:root xmlns:r=\"urn:schemas-upnp-org:device-1-0\"><r:URLBase>http://10.0.0.138:49152/</r:URLBase>"
        "<r:device><r:serviceList>"
        "<r:service><r:serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</r:serviceType><r:controlURL>upnp/control/WANPPPConn1</r:controlURL></r:service>"
        "<r:service><r:serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</r:serviceType><r:controlURL>upnp/control/WANIPConn2</r:controlURL></r:service>"
        "</r:serviceList></r:device></r:root>";
    if (!IgdControlRead(desc2, "http://10.0.0.138:49152/gatedesc.xml", &ctl, &svc, &why) || ctl != "http://10.0.0.138:49152/upnp/control/WANIPConn2"
        || svc != "urn:schemas-upnp-org:service:WANIPConnection:2") Fail("T-597: IGD2 first, URLBase used: " + ctl + " / " + why);
    /* only PPP */
    const std::string ppp = "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
                            "<controlURL>/upnp/control/WANPPPConn1</controlURL></service></serviceList></device></root>";
    if (!IgdControlRead(ppp, "http://192.168.0.1:5431/dyndev/uuid:0000", &ctl, &svc, &why) || ServiceShort(svc) != "WANPPPConnection:1"
        || ctl != "http://192.168.0.1:5431/upnp/control/WANPPPConn1") Fail("T-597: WANPPPConnection alone: " + ctl);
    /* a control URL on another computer is refused, even when it is the only one */
    const std::string away = "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
                             "<controlURL>http://203.0.113.9/ctl</controlURL></service></serviceList></device></root>";
    if (IgdControlRead(away, "http://192.168.1.1/d.xml", &ctl, &svc, &why) || why.find("another computer") == std::string::npos) Fail("T-597: a control URL elsewhere: " + why);
    if (IgdControlRead("<root><device><friendlyName>Printer</friendlyName></device></root>", "http://192.168.1.9/d.xml", &ctl, &svc, &why)
        || why != "the description lists no service") Fail("T-597: no service: " + why);
    const std::string printer = "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
                                "<controlURL>/cd</controlURL></service></serviceList></device></root>";
    if (IgdControlRead(printer, "http://192.168.1.9/d.xml", &ctl, &svc, &why) || why.find("no port-forwarding service") == std::string::npos) Fail("T-597: no IGD service: " + why);
    if (IgdControlRead(desc, "http://router/d.xml", &ctl, &svc, &why)) Fail("T-597: a description URL that is not an address");
    /* the element reader */
    std::string inner;
    size_t after = 0;
    if (!XmlNext("<a><serviceList><service>x</service></serviceList></a>", "service", 0, &inner, &after) || inner != "x") Fail("T-597: serviceList is not service");
    if (!XmlNext("<a><b/></a>", "b", 0, &inner, &after) || !inner.empty()) Fail("T-597: an empty element");
    if (XmlNext("<a><b>open", "b", 0, &inner, &after)) Fail("T-597: an element with no end tag");
    if (XmlValue("<e>  a &amp; b &lt;c&gt; </e>", "e") != "a & b <c>") Fail("T-597: unescaped and trimmed");
}

/* SOAP: the calls' texts and the answers' readings, incl. faults 718 and 725. */
void t_t597_soap()
{
    using namespace swrouter;
    const std::string st = "urn:schemas-upnp-org:service:WANIPConnection:1";
    if (SoapActionHeader(st, "AddPortMapping") != "\"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"") Fail("T-597: SOAPAction");
    const std::string add = SoapAddPortMapping(st, 27016, "UDP", "192.168.1.20", "Shared Wastelands world server (UDP 27016)", 3600);
    const char* order[] = { "<NewRemoteHost></NewRemoteHost>", "<NewExternalPort>27016</NewExternalPort>", "<NewProtocol>UDP</NewProtocol>",
                            "<NewInternalPort>27016</NewInternalPort>", "<NewInternalClient>192.168.1.20</NewInternalClient>",
                            "<NewEnabled>1</NewEnabled>", "<NewPortMappingDescription>Shared Wastelands world server (UDP 27016)</NewPortMappingDescription>",
                            "<NewLeaseDuration>3600</NewLeaseDuration>" };
    size_t at = 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
    {
        const size_t p = add.find(order[i], at);
        if (p == std::string::npos) Fail(std::string("T-597: AddPortMapping argument missing or out of order: ") + order[i]);
        else at = p;
    }
    if (add.find("<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">") == std::string::npos
        || add.find("<s:Envelope") == std::string::npos) Fail("T-597: the envelope");
    if (SoapAddPortMapping(st, 1, "UDP", "1.2.3.4", "a<b&c", 0).find("<NewPortMappingDescription>a&lt;b&amp;c</NewPortMappingDescription>") == std::string::npos)
        Fail("T-597: the description is escaped");
    if (SoapDeletePortMapping(st, 27016, "UDP").find("<NewExternalPort>27016</NewExternalPort><NewProtocol>UDP</NewProtocol>") == std::string::npos) Fail("T-597: delete");
    if (SoapGetSpecificEntry(st, 7777, "UDP").find("<u:GetSpecificPortMappingEntry") == std::string::npos) Fail("T-597: get entry");
    if (SoapGetExternalAddress(st).find("<u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\"></u:GetExternalIPAddress>") == std::string::npos)
        Fail("T-597: get address has no arguments");

    /* synthetic answers */
    const std::string okAdd = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                              "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                              "<u:AddPortMappingResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\"/></s:Body></s:Envelope>";
    int fault = 0;
    std::string ft;
    if (SoapAnswerRead(200, okAdd, "AddPortMapping", &fault, &ft) != kSoapOk || fault != -1) Fail("T-597: an empty success answer");
    const std::string fault718 = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault>"
                                 "<faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
                                 "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>718</errorCode>"
                                 "<errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
    if (SoapAnswerRead(500, fault718, "AddPortMapping", &fault, &ft) != kSoapFault || fault != 718 || ft != "ConflictInMappingEntry") Fail("T-597: fault 718");
    std::string fault725 = fault718;
    fault725.replace(fault725.find("718"), 3, "725");
    fault725.replace(fault725.find("ConflictInMappingEntry"), 22, "OnlyPermanentLeasesSupported");
    if (SoapAnswerRead(500, fault725, "AddPortMapping", &fault, &ft) != kSoapFault || fault != 725) Fail("T-597: fault 725");
    if (SoapAnswerRead(200, fault718, "AddPortMapping", &fault, &ft) != kSoapUnreadable) Fail("T-597: a 200 without the response element");
    if (SoapAnswerRead(500, "<html>Internal error</html>", "AddPortMapping", &fault, &ft) != kSoapUnreadable || fault != -1) Fail("T-597: a 500 with no UPnPError");
    if (SoapAnswerRead(0, "", "AddPortMapping", &fault, &ft) != kSoapUnreadable) Fail("T-597: no answer");
    if (SoapAnswerRead(200, okAdd, "DeletePortMapping", &fault, &ft) != kSoapUnreadable) Fail("T-597: another action's response is not this one's");
    if (std::string(FaultWord(718)) != "ConflictInMappingEntry" || std::string(FaultWord(725)) != "OnlyPermanentLeasesSupported"
        || std::string(FaultWord(714)) != "NoSuchEntryInArray" || std::string(FaultWord(9)) != "other") Fail("T-597: fault words");
    if (AddFaultNext(725, 3600) != kAddRetryPermanent) Fail("T-597: 725 to a lease -> ask again as permanent");
    if (AddFaultNext(725, 0) != kAddRefused) Fail("T-597: 725 to a permanent ask is a refusal (no loop)");
    if (AddFaultNext(718, 3600) != kAddHeldElsewhere || AddFaultNext(718, 0) != kAddHeldElsewhere) Fail("T-597: 718 -> another computer's entry");
    if (AddFaultNext(501, 3600) != kAddRefused || AddFaultNext(606, 3600) != kAddRefused || AddFaultNext(-1, 3600) != kAddRefused) Fail("T-597: other faults refuse");

    const std::string entry = "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
                              "<u:GetSpecificPortMappingEntryResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
                              "<NewInternalPort>27016</NewInternalPort><NewInternalClient>192.168.1.20</NewInternalClient><NewEnabled>1</NewEnabled>"
                              "<NewPortMappingDescription>Shared Wastelands world server (UDP 27016)</NewPortMappingDescription>"
                              "<NewLeaseDuration>3412</NewLeaseDuration></u:GetSpecificPortMappingEntryResponse></s:Body></s:Envelope>";
    long ip = 0;
    if (SoapAnswerRead(200, entry, "GetSpecificPortMappingEntry", &fault, &ft) != kSoapOk || XmlValue(entry, "NewInternalClient") != "192.168.1.20"
        || !ReadNum(XmlValue(entry, "NewInternalPort"), &ip) || ip != 27016
        || !coopupnp::IsOurDescription(XmlValue(entry, "NewPortMappingDescription"))) Fail("T-597: the existing entry read");
    if (coopupnp::ActionFor(coopupnp::ClassifyExisting(true, XmlValue(entry, "NewInternalClient"), ip, "192.168.1.21", 27016)) != coopupnp::kActLeave)
        Fail("T-597: an entry for another computer is never overwritten");
    const std::string ext = "<s:Envelope><s:Body><u:GetExternalIPAddressResponse xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
                            "<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress></u:GetExternalIPAddressResponse></s:Body></s:Envelope>";
    if (SoapAnswerRead(200, ext, "GetExternalIPAddress", &fault, &ft) != kSoapOk || XmlValue(ext, "NewExternalIPAddress") != "203.0.113.7") Fail("T-597: the external address");
    if (!ReadNum(" 42 ", &ip) || ip != 42 || ReadNum("4x", &ip) || ReadNum("", &ip) || ReadNum("99999999999", &ip)) Fail("T-597: ReadNum");
}

/* PCP and NAT-PMP: the request bytes and the answers' readings. */
void t_t597_pcp_natpmp()
{
    using namespace swrouter;
    unsigned char m[12];
    PmpMapRequest(m, 27016, 27016, 3600);
    const unsigned char wantMap[12] = { 0, 1, 0, 0, 0x69, 0x88, 0x69, 0x88, 0, 0, 0x0E, 0x10 };
    for (int i = 0; i < 12; ++i) if (m[i] != wantMap[i]) Fail("T-597: NAT-PMP map request byte " + I(i));
    PmpMapRequest(m, 27016, 0, 0);
    if (m[6] != 0 || m[7] != 0 || m[8] != 0 || m[11] != 0 || m[4] != 0x69) Fail("T-597: NAT-PMP removal = external port 0, lifetime 0");
    unsigned char a[2] = { 9, 9 };
    PmpAddrRequest(a);
    if (a[0] != 0 || a[1] != 0 || kPmpAddrRequestSize != 2) Fail("T-597: NAT-PMP address request");
    /* synthetic answers */
    const unsigned char mapAns[16] = { 0, 129, 0, 0, 0, 0, 0x1C, 0x20, 0x69, 0x88, 0x69, 0x88, 0, 0, 0x1C, 0x20 };
    unsigned long result = 9, life = 0;
    long inP = 0, exP = 0;
    if (!PmpMapAnswerRead(mapAns, 16, &result, &inP, &exP, &life) || result != 0 || inP != 27016 || exP != 27016 || life != 7200) Fail("T-597: NAT-PMP map answer");
    if (PmpMapAnswerRead(mapAns, 15, &result, &inP, &exP, &life)) Fail("T-597: a short answer");
    const unsigned char tcpAns[16] = { 0, 130, 0, 0, 0, 0, 0, 0, 0x69, 0x88, 0x69, 0x88, 0, 0, 0x1C, 0x20 };
    if (PmpMapAnswerRead(tcpAns, 16, &result, &inP, &exP, &life)) Fail("T-597: a TCP answer is not ours");
    const unsigned char refused[16] = { 0, 129, 0, 2, 0, 0, 0, 1, 0x69, 0x88, 0, 0, 0, 0, 0, 0 };
    if (!PmpMapAnswerRead(refused, 16, &result, &inP, &exP, &life) || result != 2 || std::string(PmpResultWord(result)).find("not authorized") == std::string::npos)
        Fail("T-597: NAT-PMP refusal");
    const unsigned char addrAns[12] = { 0, 128, 0, 0, 0, 0, 0, 5, 203, 0, 113, 7 };
    unsigned long ip = 0;
    if (!PmpAddrAnswerRead(addrAns, 12, &result, &ip) || result != 0 || Ipv4Text(ip) != "203.0.113.7") Fail("T-597: NAT-PMP address answer");

    unsigned char nonce[12];
    for (int i = 0; i < 12; ++i) nonce[i] = (unsigned char)(0xA0 + i);
    unsigned char p[60];
    PcpMapRequest(p, 0xC0A80114ul, nonce, 27016, 27016, 3600);
    if (p[0] != 2 || p[1] != 1 || p[2] != 0 || p[3] != 0 || Get32(p + 4) != 3600ul) Fail("T-597: PCP header");
    for (int i = 8; i < 18; ++i) if (p[i] != 0) Fail("T-597: PCP client address prefix zeros");
    if (p[18] != 0xFF || p[19] != 0xFF || Get32(p + 20) != 0xC0A80114ul) Fail("T-597: PCP client address as ::ffff:192.168.1.20");
    for (int i = 0; i < 12; ++i) if (p[24 + i] != nonce[i]) Fail("T-597: PCP nonce");
    if (p[36] != 17 || p[37] != 0 || Get16(p + 40) != 27016ul || Get16(p + 42) != 27016ul) Fail("T-597: PCP protocol and ports");
    if (p[54] != 0xFF || p[55] != 0xFF || Get32(p + 56) != 0ul || kPcpMapSize != 60) Fail("T-597: PCP suggested external address ::ffff:0.0.0.0");
    /* a synthetic PCP success answer */
    unsigned char r[60];
    for (int i = 0; i < 60; ++i) r[i] = 0;
    r[0] = 2; r[1] = 0x81; r[3] = 0;
    Put32(r + 4, 7200); Put32(r + 8, 12345);
    for (int i = 0; i < 12; ++i) r[24 + i] = nonce[i];
    r[36] = 17; Put16(r + 40, 27016); Put16(r + 42, 27016); r[54] = 0xFF; r[55] = 0xFF; Put32(r + 56, 0xCB007107ul);
    unsigned long extIp = 0;
    if (!PcpMapAnswerRead(r, 60, nonce, &result, &life, &exP, &extIp) || result != 0 || life != 7200 || exP != 27016 || Ipv4Text(extIp) != "203.0.113.7")
        Fail("T-597: PCP success answer");
    unsigned char other[12];
    for (int i = 0; i < 12; ++i) other[i] = nonce[i];
    other[5] ^= 1;
    if (PcpMapAnswerRead(r, 60, other, &result, &life, &exP, &extIp)) Fail("T-597: an answer to another nonce is not ours");
    r[3] = 2;
    if (!PcpMapAnswerRead(r, 60, nonce, &result, &life, &exP, &extIp) || result != 2 || std::string(PcpResultWord(result)).find("not authorized") == std::string::npos)
        Fail("T-597: PCP NOT_AUTHORIZED");
    r[3] = 0; r[36] = 6;
    if (PcpMapAnswerRead(r, 60, nonce, &result, &life, &exP, &extIp)) Fail("T-597: a TCP mapping is not ours");
    r[36] = 17;
    if (PcpMapAnswerRead(r, 59, nonce, &result, &life, &exP, &extIp)) Fail("T-597: a short PCP answer");
    /* a NAT-PMP router answering a PCP request: version 0, unsupported version */
    const unsigned char pmpSays[8] = { 0, 129, 0, 1, 0, 0, 0, 9 };
    if (!PcpAnswerIsPmp(pmpSays, 8) || PcpAnswerIsPmp(r, 60) || PcpAnswerIsPmp(pmpSays, 1)) Fail("T-597: PcpAnswerIsPmp");
    if (PmpWaitMs(0) != 250u || PmpWaitMs(3) != 2000u || kPmpSends != 4 || kPmpPort != 5351) Fail("T-597: the 5351 retry schedule");
}

/* The route order, what the routes add up to, and the lease's renewal. */
void t_t597_routes_and_lease()
{
    using namespace coopupnp;
    if (kAskLeaseSeconds != 3600 || kWindowsLeaseSeconds != 0) Fail("T-597: the asked lease is an hour; NATUPnP takes none");
    if (FirstRoute() != kRouteIgd) Fail("T-597: the mod's own UPnP ask goes first");
    if (NextRoute(kRouteIgd, kRrNoAnswer) != kRoutePcp || NextRoute(kRouteIgd, kRrRefused) != kRoutePcp || NextRoute(kRouteIgd, kRrUnavailable) != kRoutePcp)
        Fail("T-597: igd unsettled -> pcp");
    if (NextRoute(kRoutePcp, kRrOtherVersion) != kRoutePmp) Fail("T-597: a NAT-PMP-only router -> natpmp");
    if (NextRoute(kRoutePcp, kRrNoAnswer) != kRouteWindows || NextRoute(kRoutePcp, kRrRefused) != kRouteWindows) Fail("T-597: pcp silent or refused -> windows (not natpmp)");
    if (NextRoute(kRoutePmp, kRrRefused) != kRouteWindows || NextRoute(kRouteWindows, kRrNoAnswer) != kRouteNone) Fail("T-597: natpmp -> windows -> stop");
    const int settle[3] = { kRrMapped, kRrAlready, kRrElsewhere };
    const int routes[4] = { kRouteIgd, kRoutePcp, kRoutePmp, kRouteWindows };
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 4; ++k)
            if (!RouteSettles(settle[i]) || NextRoute(routes[k], settle[i]) != kRouteNone) Fail("T-597: a settling answer stops the ask");
    if (RouteSettles(kRrRefused) || RouteSettles(kRrNoAnswer) || RouteSettles(kRrUnavailable) || RouteSettles(kRrOtherVersion)) Fail("T-597: unsettled answers");
    /* the players' cases */
    int o = kOutNoRouter;
    o = FoldOutcome(o, kRrNoAnswer); o = FoldOutcome(o, kRrNoAnswer); o = FoldOutcome(o, kRrNoAnswer);
    if (o != kOutNoRouter) Fail("T-597: silence on every route is noRouter");
    o = FoldOutcome(kOutNoRouter, kRrNoAnswer); o = FoldOutcome(o, kRrUnavailable);
    if (o != kOutNoRouter) Fail("T-597: a route that could not be tried is not a failure");
    o = FoldOutcome(kOutNoRouter, kRrRefused); o = FoldOutcome(o, kRrNoAnswer); o = FoldOutcome(o, kRrNoAnswer);
    if (o != kOutFailed) Fail("T-597: a refusal stays a failure when nothing else answers");
    o = FoldOutcome(kOutNoRouter, kRrRefused); o = FoldOutcome(o, kRrMapped);
    if (o != kOutMapped) Fail("T-597: a later route that maps wins over an earlier refusal");
    if (FoldOutcome(kOutNoRouter, kRrAlready) != kOutAlready || FoldOutcome(kOutNoRouter, kRrElsewhere) != kOutFailed) Fail("T-597: already / elsewhere");
    if (std::string(RouteWord(kRoutePmp)) != "natpmp" || std::string(RouteWord(kRouteWindows)) != "windows" || std::string(RouteResultWord(kRrElsewhere)) != "heldElsewhere")
        Fail("T-597: route words");

    if (RenewAfterMs(3600, true) != 1800000ul || RenewAfterMs(0, true) != 0 || RenewAfterMs(-5, false) != 0) Fail("T-597: renew at half the lease, never a permanent entry");
    if (RenewAfterMs(3600, false) != kRenewRetryMs || RenewAfterMs(120, false) != 60000ul) Fail("T-597: a failed renewal retries sooner, never after the lease's half");
    if (RenewWaitMs(1000, 5000, 3000) != 3000ul || RenewWaitMs(1000, 5000, 7000) != 0 || RenewWaitMs(0xFFFFF000ul, 0x1800ul, 0x00000800ul) != 0ul)
        Fail("T-597: RenewWaitMs, also across the tick counter's wrap (0x1800 ms have passed)");
    if (RenewWaitMs(0xFFFFF000ul, 0x3000ul, 0x00000800ul) != 0x1800ul) Fail("T-597: RenewWaitMs across the wrap, not yet due");

    State s;
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutMapped, true, kRouteIgd, 3600);
    if (s.route != kRouteIgd || s.lease != 3600 || NextStep(s) != kStepIdle) Fail("T-597: held with a lease: idle until due");
    s.renewDue = true;
    if (NextStep(s) != kStepRenew) Fail("T-597: due -> renew");
    AfterRenew(&s, true, 1800);
    if (s.renewDue || s.lease != 1800 || NextStep(s) != kStepIdle) Fail("T-597: renewed with the router's granted lease");
    AfterRenew(&s, true, 7200);
    if (s.lease != kAskLeaseSeconds) Fail("T-597: a granted lease above the asked one is held to it");
    s.renewDue = true;
    AfterRenew(&s, false, 99);
    if (s.lease != kAskLeaseSeconds) Fail("T-597: a refused renewal keeps the lease");
    s.renewDue = true;
    HostEnd(&s);
    if (NextStep(s) != kStepUnmap) Fail("T-597: hosting ended: the removal comes before a due renewal");
    AfterUnmap(&s);
    if (s.route != kRouteNone || s.lease != 0 || s.renewDue || NextStep(s) != kStepIdle) Fail("T-597: removed: nothing held");
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutAlready, false, kRouteWindows, 0);
    s.renewDue = true;
    if (NextStep(s) != kStepIdle) Fail("T-597: a permanent entry is never renewed");
    HostStart(&s, 27020);
    AfterMap(&s, 27016, kOutFailed, true, kRouteIgd, 3600);
    if (s.route != kRouteNone || s.lease != 0) Fail("T-597: a failed ask holds no route");
    if (std::string(StepWord(kStepRenew)) != "renew") Fail("T-597: StepWord renew");
}

/* T-597 fold: the safety rules - only home-network devices, PCP's lasting nonce, router texts made safe for the log, the
   lease clamp, a lost port, the removal check, PPP adapters, and the INTERNET ADDRESS row asking the websites beside the
   router. */
void t_t597_fold_safety()
{
    using namespace swrouter;
    struct HostCase { const char* addr; const char* gw; bool home; };
    static const HostCase hc[] = {
        { "192.168.1.1", "", true }, { "10.0.0.138", "", true }, { "172.16.0.1", "", true }, { "172.31.255.254", "", true },
        { "100.64.0.1", "", true }, { "100.127.255.255", "", true }, { "100.63.255.255", "", false }, { "172.32.0.1", "", false }, { "172.15.0.1", "", false },
        { "100.128.0.1", "", false }, { "203.0.113.9", "", false }, { "8.8.8.8", "", false }, { "127.0.0.1", "", false },
        { "239.255.255.250", "", false }, { "203.0.113.9", "203.0.113.9", true }, { "203.0.113.9", "203.0.113.10", false },
        { "router", "", false }, { "", "", false } };
    for (size_t i = 0; i < sizeof(hc) / sizeof(hc[0]); ++i)
        if (HomeNetworkHost(hc[i].addr, hc[i].gw) != hc[i].home) Fail(std::string("T-597: HomeNetworkHost('") + hc[i].addr + "', '" + hc[i].gw + "')");

    unsigned char a[12], b[12], c[12];
    PcpNonceFrom("PC1|192.168.1.20|27016", a);
    PcpNonceFrom("PC1|192.168.1.20|27016", b);
    PcpNonceFrom("PC1|192.168.1.20|27017", c);
    bool same = true, differ = false, allZero = true;
    for (int i = 0; i < 12; ++i) { if (a[i] != b[i]) same = false; if (a[i] != c[i]) differ = true; if (a[i] != 0) allZero = false; }
    if (!same || !differ || allZero) Fail("T-597: the PCP nonce is the same for the same computer and port, another for another port");

    if (LogSafeText("Shared Wastelands world server (UDP 27016)", 48) != "Shared Wastelands world server (UDP 27016)") Fail("T-597: a plain text stays");
    if (LogSafeText("fwd to 192.168.1.21 port 27016", 48) != "fwd to <address> port 27016") Fail("T-597: an address is hidden");
    if (LogSafeText("a\r\nb\tc", 48) != "a  b c") Fail("T-597: line breaks become spaces");
    if (LogSafeText("v1.2 and 1.2.3", 48) != "v1.2 and <address>") Fail("T-597: two or more dots read as an address");
    if (LogSafeText(std::string(100, 'x'), 48) != std::string(48, 'x') + "...") Fail("T-597: cut to the limit");
    if (LogSafeText("host fe80::1c2:3%eth0 here", 48) != "host <address> here") Fail("T-597: an IPv6 address with its zone is hidden");
    if (LogSafeText("to 2001:DB8::5 and ::1", 48) != "to <address> and <address>") Fail("T-597: IPv6 '::' forms are hidden");
    if (LogSafeText("[::ffff:192.168.1.9]:27016", 48) != "[<address>]:27016") Fail("T-597: an IPv4-mapped IPv6 address is hidden");
    if (LogSafeText("deadbeef1.2.3 port:27016 a:b", 48) != "deadbeef<address> port:27016 a:b") Fail("T-597: one colon is no address; dots still are");
    if (LogSafeText(std::string(47, 'x') + "\xC3\xA9" + "yy", 48) != std::string(47, 'x') + "...") Fail("T-597: a 2-byte character is never cut");
    if (LogSafeText(std::string(46, 'x') + "\xE2\x82\xAC" + "yy", 48) != std::string(46, 'x') + "...") Fail("T-597: a 3-byte character is never cut");
    if (LogSafeText(std::string(46, 'x') + "\xC3\xA9", 48) != std::string(46, 'x') + "\xC3\xA9") Fail("T-597: a text that fits is not cut");
    if (HttpBodyWaitMs(0) != kHttpStepMs || HttpBodyWaitMs(kHttpWholeMs - 500u) != 500ul || HttpBodyWaitMs(kHttpWholeMs) != 0ul
        || HttpBodyWaitMs(kHttpWholeMs + 1u) != 0ul) Fail("T-597: each wait for the body is at most the time left of its cap");
    if (std::string(PcpResultWord(2)).find("another request") == std::string::npos) Fail("T-597: NOT_AUTHORIZED names both causes");
    if (kHttpWholeMs <= kHttpStepMs || kHttpWholeMs > 10000u) Fail("T-597: the whole-request cap is above one step and short");

    using namespace coopupnp;
    if (ClampLease(0) != 0 || ClampLease(-1) != 0 || ClampLease(1) != kMinLeaseSeconds || ClampLease(600) != 600
        || ClampLease(2000000000L) != kAskLeaseSeconds) Fail("T-597: ClampLease");
    if (RenewAfterMs(1, true) != 60000ul) Fail("T-597: a 1 s grant is renewed after a minute, not every half second");
    if (RenewAfterMs(2000000000L, true) != 1800000ul || RenewAfterMs(2147483647L, false) != kRenewRetryMs) Fail("T-597: a huge grant does not overflow");
    State s;
    HostStart(&s, 27016);
    AfterMap(&s, 27016, kOutMapped, true, kRoutePcp, 86400);
    if (s.lease != kAskLeaseSeconds) Fail("T-597: a mapping's granted lease is clamped");
    s.renewDue = true;
    AfterRenewLost(&s);
    if (s.held != 27016 || s.lease != 0 || s.removeAtEnd || s.renewDue || NextStep(s) != kStepIdle) Fail("T-597: a lost port is held but never renewed");
    HostEnd(&s);
    if (NextStep(s) != kStepForget) Fail("T-597: a lost port is forgotten, never removed, at hosting end");
    const std::string ours = Description(27016);
    if (DeleteAction(true, "192.168.1.20", 27016, ours, "192.168.1.20", 27016) != kDelRemove) Fail("T-597: our entry is removed");
    if (DeleteAction(false, "", 0, "", "192.168.1.20", 27016) != kDelGone) Fail("T-597: nothing there: already gone");
    if (DeleteAction(true, "192.168.1.30", 27016, ours, "192.168.1.20", 27016) != kDelNotOurs) Fail("T-597: another computer's entry with our text is left");
    if (DeleteAction(true, "192.168.1.20", 27016, "Plex", "192.168.1.20", 27016) != kDelNotOurs) Fail("T-597: the player's own entry is left");
    if (DeleteAction(true, "192.168.1.20", 27017, ours, "192.168.1.20", 27016) != kDelNotOurs) Fail("T-597: another internal port is left");
    if (DeleteCheckNext(1, -1) != kDelCheckJudge || DeleteCheckNext(0, 714) != kDelCheckGone || DeleteCheckNext(0, 501) != kDelCheckUnchecked
        || DeleteCheckNext(0, 402) != kDelCheckUnchecked || DeleteCheckNext(-1, -1) != kDelCheckNoAnswer)
        Fail("T-597: only fault 714 means gone; another fault sends the delete unchecked; no answer sends nothing");
    if (!RenewConflictStillOurs(true, "192.168.1.20", 27016, ours, "192.168.1.20", 27016)) Fail("T-597: a 718 renewal on our own entry keeps it");
    if (RenewConflictStillOurs(false, "", 0, "", "192.168.1.20", 27016)) Fail("T-597: a 718 renewal with no entry found loses the port");
    if (RenewConflictStillOurs(true, "192.168.1.30", 27016, ours, "192.168.1.20", 27016)) Fail("T-597: a 718 renewal on another computer's entry loses the port");
    if (RenewConflictStillOurs(true, "192.168.1.20", 27016, "Plex", "192.168.1.20", 27016)) Fail("T-597: a 718 renewal on the player's own entry loses the port");

    coopui::PanelNic ppp; ppp.ifType = coopui::kNicPpp; ppp.up = 1; ppp.hasGateway = 1; ppp.best = 1; ppp.ipv4.push_back(0xC0A86401ul);
    coopui::PanelNic eth; eth.ifType = coopui::kNicEthernet; eth.up = 1; eth.hasGateway = 1; eth.ipv4.push_back(0xC0A80114ul);
    std::vector<coopui::PanelNic> nics;
    nics.push_back(ppp); nics.push_back(eth);
    int picked = -1, why = -1;
    if (coopui::PanelPickHomeNic(nics, &picked, &why) != "192.168.1.20" || picked != 1) Fail("T-597: a PPP connection (Windows' own VPN) is never the home network");

    using namespace coopui;
    PanelNetStep st = PanelNetAddrPress(kNetAddrAsking, kNetPressNone, kNetPressShow);
    if (st.state != kNetAddrRacing || st.startLookup != 1 || st.pending != kNetPressShow) Fail("T-597: a press while the router is asked starts the websites beside it");
    if (PanelNetAddrLine(kNetAddrRacing, "", 1, 0, kNetPressShow) != "Finding your internet address..."
        || PanelNetAddrLine(kNetAddrRouterOnly, "", 1, 0, kNetPressShow) != "Finding your internet address...") Fail("T-597: racing lines");
    st = PanelNetAddrPress(kNetAddrRacing, kNetPressShow, kNetPressCopy);
    if (st.state != kNetAddrRacing || st.startLookup != 0 || st.pending != kNetPressCopy) Fail("T-597: a second press waits, nothing asked again");
    st = PanelNetRouterAnswer(kNetAddrRacing, kNetPressShow, 1);
    if (st.state != kNetAddrFound || st.showNow != 1) Fail("T-597: the router's address first wins");
    st = PanelNetLookupAnswer(kNetAddrRacing, kNetPressCopy, 1);
    if (st.state != kNetAddrFound || st.copyNow != 1) Fail("T-597: a website's address first wins");
    st = PanelNetLookupAnswer(st.state, kNetPressNone, 1);
    if (st.state != kNetAddrFound || st.copyNow != 0) Fail("T-597: the later answer is an old one");
    st = PanelNetRouterAnswer(kNetAddrRacing, kNetPressShow, 0);
    if (st.state != kNetAddrLooking || st.startLookup != 0 || st.pending != kNetPressShow) Fail("T-597: router none while racing: the websites decide");
    st = PanelNetLookupAnswer(kNetAddrRacing, kNetPressShow, 0);
    if (st.state != kNetAddrRouterOnly || st.pending != kNetPressShow) Fail("T-597: websites none while racing: the router decides");
    st = PanelNetRouterAnswer(kNetAddrRouterOnly, kNetPressShow, 1);
    if (st.state != kNetAddrFound || st.showNow != 1) Fail("T-597: the router's later address");
    st = PanelNetRouterAnswer(kNetAddrRouterOnly, kNetPressShow, 0);
    if (st.state != kNetAddrFailed || st.pending != kNetPressNone || PanelNetAddrButtonsOn(st.state) != 0) Fail("T-597: neither gave one: failed");
    st = PanelNetLookupAnswer(kNetAddrRouterOnly, kNetPressShow, 1);
    if (st.state != kNetAddrRouterOnly || st.showNow != 0) Fail("T-597: an old website answer while only the router is asked");
}


/* A deleted world's leftovers: every save folder its key files name (case ignored, any profile, any folder name), never the folder
   this game plays from nor its profile's records stores, and the records store names for every spelling and profile seen plus this
   person's own numbers. */
/* ================= T-490 (owner 428): THE WORLD ID; owner 429: THE FOLDER FORMAT NUMBER ================= */
static unsigned long long T490Ft(long long unixSec) { return (unsigned long long)unixSec * 10000000ULL + 116444736000000000ULL; }
coopprof::SaveDirSeen T490Dir(const char* name, const char* world, const std::string& prof, const std::string& id, long long savedUnix)
{
    coopprof::SaveDirSeen d = Pp5Dir(name, world, prof, 0, savedUnix > 0 ? T490Ft(savedUnix) : 0ULL);
    d.worldId = id;
    return d;
}
void t_t490_world_id_text()
{
    using namespace coopworld;
    const std::string id = WorldIdMake(1791234567LL, 0x3fa9c01eu);
    if (id != "1791234567-3fa9c01e" || !WorldIdOk(id)) Fail("the id text: <created>-<8 lowercase hex>");
    if (WorldIdCreated(id) != 1791234567LL || WorldIdHex(id) != "3fa9c01e") Fail("the id's parts");
    if (WorldIdOk("") || WorldIdOk("-3fa9c01e") || WorldIdOk("1791234567-3FA9C01E") || WorldIdOk("1791234567-3fa9c01")
        || WorldIdOk("17912a4567-3fa9c01e") || WorldIdOk("1234567890123456789-3fa9c01e") || WorldIdOk("1791234567-3fa9c01e0"))
        Fail("bad ids refused: empty time, upper case, short or long hex, a letter in the time, 19 digits");
    if (WorldIdMake(999999999999999999LL, 0xffffffffu).size() != kWorldIdMax) Fail("the longest id is kWorldIdMax characters");
    if (WorldIdMix(1, 2, 3) != WorldIdMix(1, 2, 3) || WorldIdMix(1, 2, 3) == WorldIdMix(1, 2, 4)) Fail("the mixer: pure, and one input changes it");
    const std::string one = WorldTxtFormat("Ridge", 1791234567LL);
    const std::string two = WorldTxtFormatWithId("Ridge", 1791234567LL, id, false);
    std::string n, got; long long c = 0; bool up = true;
    if (two.compare(0, one.size(), one) != 0) Fail("line 1 is unchanged by the id line");
    if (!WorldTxtParse(two, &n, &c) || n != "Ridge" || c != 1791234567LL) Fail("a line-1-only reader reads a two-line world.txt");
    if (!WorldTxtParseId(two, &got, &up) || got != id || up) Fail("line 2 round trip, born");
    if (!WorldTxtParseId(WorldTxtFormatWithId("Ridge", 5, id, true), &got, &up) || !up) Fail("line 2 round trip, upgraded");
    if (WorldTxtParseId(one, &got, &up)) Fail("a one-line world.txt has no id");
    std::string crlf = two; crlf.replace(crlf.find('\n'), 1, "\r\n");
    if (!WorldTxtParseId(crlf, &got, &up) || got != id) Fail("line 2 is read after a CRLF line 1");
    if (WorldTxtParseId("v1\tRidge\t5\nid\tbad\tborn\n", 0, 0) || WorldTxtParseId("v1\tRidge\t5\nid\t" + id + "\tmaybe\n", 0, 0))
        Fail("a bad line 2 is no id");
    if (FileTimeToUnix(T490Ft(1791234567LL)) != 1791234567LL || FileTimeToUnix(0) != 0) Fail("FILETIME to unix seconds");
}
void t_t490_welcome_id_round_trip()
{
    using namespace coopstore;
    coopmods::ModList w; w.known = 1; w.active.push_back("A|1|00000001");
    StoreWelcome a; a.protocol = kStoreWelcomeIdSince; a.authority = 1; a.recordCount = 4; a.slot = 2; a.hasWorld = true; a.world = "Ridge";
    a.hasWorldId = true; a.worldId = "1791234567-3fa9c01e"; a.worldIdUpgraded = true;
    a.hasMods = true; a.modsVerdict = kModsDiffers; a.modsActive = 1; a.modsPlugins = 0; a.modsWorld = w;
    a.hasWorldGen = true; a.worldTail.push_back('T'); a.worldTail.push_back('Z');
    std::vector<char> b; StoreWelcomeEncode(a, &b);
    StoreWelcome r;
    if (!StoreWelcomeDecode(b, &r) || !r.hasWorld || r.world != "Ridge" || !r.hasWorldId || r.worldId != a.worldId || !r.worldIdUpgraded || r.worldIdMalformed)
        Fail("the id and the upgraded flag ride right after the world name");
    if (!r.hasMods || r.modsVerdict != (unsigned)kModsDiffers || !r.hasModsWorld || !coopmods::ModListSame(r.modsWorld, w)
        || !r.hasWorldGen || r.worldTail.size() != 2 || r.worldTail[0] != 'T' || r.worldTail[1] != 'Z')
        Fail("after the id: the mods verdict, the world's list and the guard tail, intact");
    a.worldIdUpgraded = false; StoreWelcomeEncode(a, &b);
    if (!StoreWelcomeDecode(b, &r) || r.worldIdUpgraded || !r.hasWorldId) Fail("a born world's flag");
    a.hasWorldId = false; a.worldId.clear(); StoreWelcomeEncode(a, &b);
    if (!StoreWelcomeDecode(b, &r) || r.hasWorldId || r.worldIdMalformed || !r.hasModsWorld || !r.hasWorldGen || r.worldTail.size() != 2)
        Fail("no id: an empty id field, and everything after it still read");
    StoreWelcome o = a; o.protocol = kStoreWelcomeIdSince - 1; o.hasWorldId = true; o.worldId = "1791234567-3fa9c01e"; StoreWelcomeEncode(o, &b);
    if (!StoreWelcomeDecode(b, &r) || r.hasWorldId || !r.hasModsWorld || !r.hasWorldGen || r.worldTail.size() != 2)
        Fail("an older WELCOME carries no id field and reads as before");
    a.hasWorldId = true; a.worldId = "1791234567-3fa9c01e"; a.hasMods = false; a.hasWorldGen = false; StoreWelcomeEncode(a, &b);
    std::vector<char> torn(b.begin(), b.end() - 2);
    if (!StoreWelcomeDecode(torn, &r) || !r.worldIdMalformed || r.hasWorldId || r.hasMods) Fail("a torn id field is caught and nothing after it read");
    std::vector<char> big(b.begin(), b.begin() + 20 + 5); StoreWelcomePutU32(&big, kStoreWelcomeWorldIdMax + 1); big.resize(big.size() + 60, 'x');
    if (!StoreWelcomeDecode(big, &r) || !r.worldIdMalformed || r.hasWorldId) Fail("an id longer than the cap is not read");
}
void t_t490_identity_decide_sweep()
{
    using namespace coopworld;
    const std::string W = "1791234567-3fa9c01e", X = "1700000000-00000001";
    const long long C = 1791234567LL, S = kWorldIdAdoptSlackSec;
    if (SaveIdentityDecide("", C + 5, "", 0, false, S) != kIdUnknown || SaveIdentityDecide(X, C, "", 0, true, S) != kIdUnknown) Fail("no id in the WELCOME: unknown");
    if (SaveIdentityDecide(W, 0, W, C, false, S) != kIdSame || SaveIdentityDecide(W, 1, W, C, true, S) != kIdSame) Fail("the same id: same");
    if (SaveIdentityDecide(X, C + 5, W, C, false, S) != kIdOther || SaveIdentityDecide(X, C + 5, W, C, true, S) != kIdOther) Fail("another id: other");
    if (SaveIdentityDecide("", C + 5, W, C, true, S) != kIdAdoptLegacy) Fail("no id, upgraded world, saved after its creation: adopted");
    if (SaveIdentityDecide("", C - S, W, C, true, S) != kIdAdoptLegacy) Fail("the slack edge: exactly slack before the creation is adopted");
    if (SaveIdentityDecide("", C - S - 1, W, C, true, S) != kIdOther) Fail("one second past the slack: another world");
    if (SaveIdentityDecide("", C + 5, W, C, false, S) != kIdOther) Fail("no id, a born world: another world");
    if (SaveIdentityDecide("", 0, W, C, true, S) != kIdOther) Fail("no id and no save time: another world");
    if (!SaveIdentityMayUse(kIdSame) || !SaveIdentityMayUse(kIdAdoptLegacy) || !SaveIdentityMayUse(kIdUnknown) || SaveIdentityMayUse(kIdOther))
        Fail("only another world's save is refused");
}
void t_t490_key_field8()
{
    using namespace restoreguard;
    const std::string id = "1791234567-3fa9c01e";
    const std::string joiner = "kcworld2\tRidge\tslot\tp.1" + KeyTailWithId(std::string(), id);
    if (joiner != "kcworld2\tRidge\tslot\tp.1\t\t\t\t" + id) Fail("a joiner's line: fields 5-7 empty, then the id");
    std::vector<std::string> f = SplitTabs(joiner);
    SaveStamp st;
    if (f.size() != 8 || StampFromFields(f, &st) || st.hasGen || KeyWorldIdOf(f) != id) Fail("a joiner's line: no stamp, field 8 read");
    const std::string host = "kcworld2\tRidge\tslot\tp.1" + KeyTailWithId(StampTail(7, 99, 2), id);
    f = SplitTabs(host);
    if (!StampFromFields(f, &st) || st.gen != 7 || st.seq != 99 || st.epoch != 2 || KeyWorldIdOf(f) != id) Fail("the host's stamp and field 8");
    std::vector<std::string> seven(f.begin(), f.begin() + 7);
    if (!StampFromFields(seven, &st) || st.gen != 7) Fail("a reader of fields 1-7 reads the stamp as before");
    if (KeyWorldIdOf(SplitTabs("kcworld2\tRidge\tslot\tp.1" + StampTail(7, 99, 2))) != "" || KeyTailWithId(StampTail(1, 2, 3), "") != StampTail(1, 2, 3))
        Fail("no id: the line is exactly as before ids");
    if (KeyWorldIdOf(SplitTabs("kcworld1\tRidge\tslot\tp.1\t\t\t\t" + id)) != "") Fail("only a kcworld2 line carries field 8");
}
void t_t490_scan_two_same_name_worlds()
{
    using namespace coopprof;
    const std::string P(32, 'a');
    const std::string P1 = ProfileId(P, 1);
    const std::string NEW = "1791234567-3fa9c01e", OLD = "1700000000-00000001";
    const long long C = 1791234567LL;
    std::vector<SaveDirSeen> d;
    d.push_back(T490Dir("Ridge - Multiplayer - Bob", "Ridge", P1, OLD, C - 50000));       /* 0: the earlier Ridge's save */
    d.push_back(T490Dir("other", "Ridge", ProfileId(P, 2), NEW, C + 10));                 /* 1: this Ridge, another profile */
    WorldIdent born; born.id = NEW;
    int owned = 9, other = 9;
    if (SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &born, &other) != -1 || owned != 0 || other != 1) Fail("the earlier world's folder is never chosen; counted");
    if (SaveFolderScanChoose(d, "Ridge", P1, 1, &owned) != 0) Fail("without an identity the name alone chooses, as before ids");
    if (ProfileSaveFolderDecide("Ridge", "Bob", d, "Ridge", P1, 1, &born) != "Ridge - Multiplayer - Bob 2") Fail("the earlier world's folder holds the name: ' 2'");
    d.push_back(T490Dir("Ridge - Multiplayer - Bob 2", "Ridge", P1, NEW, C + 100));        /* 2: this Ridge's own folder */
    if (SaveFolderScanChoose(d, "Ridge", P1, 1, &owned, &born, &other) != 2 || owned != 1) Fail("the folder carrying this world's id is found");
    WorldIdent up; up.id = NEW; up.upgraded = true;
    std::vector<SaveDirSeen> e;
    e.push_back(T490Dir("legacy", "Ridge", P1, "", C + 900000));                           /* newer, no id: adoptable */
    e.push_back(T490Dir("stamped", "Ridge", P1, NEW, C + 20));                             /* older, this world's id */
    if (SaveFolderScanChoose(e, "Ridge", P1, 1, &owned, &up) != 1 || owned != 2) Fail("same beats adoptLegacy, whatever the times");
    e.pop_back();
    if (SaveFolderScanChoose(e, "Ridge", P1, 1, &owned, &up) != 0) Fail("an upgraded world adopts its own save made before ids");
    if (SaveFolderScanChoose(e, "Ridge", P1, 1, &owned, &born) != -1) Fail("a born world adopts no save without an id");
    if (ProfileSaveFolderDecide("Ridge", "Bob", e, "Ridge", P1, 1, &up) != "Ridge - Multiplayer - Bob") Fail("an adoptable folder does not take the name");
}
void t_t490_store_names_by_id()
{
    using namespace coopown;
    const std::string id = "1791234567-3fa9c01e";
    if (PlayerStoreName("Ridge", "p.1", "") != PlayerStoreName("Ridge", "p.1")) Fail("an empty id gives the name from before ids");
    const std::string a = PlayerStoreName("Ridge", "p.1", id), b = PlayerStoreName("Ridge", "p.1", "1791234567-3fa9c01f");
    if (a == PlayerStoreName("Ridge", "p.1") || a == b) Fail("the id changes the store name, and two ids differ");
    if (a.compare(0, 9, "mp-ridge-") != 0 || a.size() != PlayerStoreName("Ridge", "p.1").size() || !restoreguard::OwnStoreNameOk(a)) Fail("the same shape and a name the wire accepts");
    std::vector<std::string> w, p; w.push_back("Ridge"); p.push_back("p.1"); p.push_back("p.2");
    const std::vector<std::string> n = PlayerStoreNamesFor(w, p, id);
    if (n.size() != 2 || n[0] != a || n[1] != PlayerStoreName("Ridge", "p.2", id)) Fail("the leftovers' store names follow the id");
}
/* PERMANENT DELETE - the path-inside check, the walk's entry kinds, which saves and records stores go with a deleted world
   or profile (src/common/permdelete.h), and the approved words (panelstatus.h, owner 685). */
void t_t714_permanent_delete()
{
    using namespace swdel;
    const std::string top = "C:\\Data\\worlds";
    if (!PathInside("C:\\Data\\worlds\\Ridge", top)) Fail("a world folder in the world store is inside it");
    if (!PathInside("c:/data/WORLDS/Ridge/", "C:\\Data\\worlds\\")) Fail("case, slashes and a trailing separator do not change the answer");
    if (PathInside("C:\\Data\\worlds", top) || PathInside("C:\\Data\\worlds\\", top)) Fail("the root itself is never inside");
    if (PathInside("C:\\Data\\worldsX\\Ridge", top)) Fail("a sibling whose name starts with the root's is outside");
    if (PathInside("C:\\Data\\worlds\\..\\Saves", top) || PathInside("C:\\Data\\worlds\\.\\Ridge", top)) Fail("a . or .. part never passes");
    if (PathInside("C:\\Data\\worlds\\\\Ridge", top)) Fail("an empty part never passes");
    if (PathInside("Data\\worlds\\Ridge", "Data\\worlds")) Fail("a relative path is refused");
    if (PathInside("D:\\Data\\worlds\\Ridge", top)) Fail("another drive is outside");
    if (PathInside("C:\\Data\\worlds\\Ridge", "")) Fail("no root: nothing is inside");
    if (!PathInside("\\\\nas\\share\\saves\\Ridge - Bob", "\\\\nas\\share\\saves")) Fail("a network path inside its root");
    if (EntryKindOf(".", 0x10) != kEntrySkip || EntryKindOf("..", 0x10) != kEntrySkip || EntryKindOf("", 0x20) != kEntrySkip) Fail("dot entries are skipped");
    if (EntryKindOf("j", 0x10 | 0x400) != kEntryLink) Fail("a junction is removed as a link, never entered");
    if (EntryKindOf("l", 0x20 | 0x400) != kEntryLink) Fail("a file link is removed itself");
    if (EntryKindOf("d", 0x10) != kEntryDir || EntryKindOf("f", 0x20) != kEntryFile || EntryKindOf("r", 0x01) != kEntryFile) Fail("folders are walked, files deleted");
    if (!NameIs("World.TXT", "world.txt") || NameIs("world.txt.1", "world.txt") || NameIs("world.tx", "world.txt") || NameIs("world.txt", 0)) Fail("the last file's name compares whole, case ignored");
    const std::string id = "1791234567-3fa9c01e", joined = "1800000000-0000abcd";
    std::vector<coopprof::SaveDirSeen> d;
    d.push_back(T490Dir("Ridge - Multiplayer - Bob", "Ridge", "pers.1", id, 1791234600LL));          /* 0: this world's */
    d.push_back(T490Dir("Ridge - Multiplayer - Bob 2", "Ridge", "pers.1", joined, 1800000100LL));    /* 1: a joined world of that name */
    d.push_back(T490Dir("old unstamped", "ridge", "pers.2", "", 1700000000LL));                      /* 2: no id - made before ids */
    d.push_back(T490Dir("Playing", "Ridge", "pers.3", id, 1791234700LL));                            /* 3: this world's, played from */
    d.push_back(T490Dir("Dune - Multiplayer - Bob", "Dune", "pers.1", "", 1700000000LL));            /* 4: another world */
    d.push_back(Pp5Dir("loose", 0, "", 0, 0ULL));                                                     /* 5: no key file */
    std::vector<std::string> keep; keep.push_back("playing");
    const coopprof::WorldLeftovers w = WorldDeleteFind(d, "Ridge", id, keep, "pers", 3);
    if (w.folders.size() != 2 || w.folders[0] != 0 || w.folders[1] != 2) Fail("a deleted world takes its id's folders and the ones with no id");
    if (w.playing.size() != 1 || w.playing[0] != 3) Fail("the folder this game plays from is left");
    const coopprof::WorldLeftovers old = WorldDeleteFind(d, "Ridge", "", keep, "pers", 3);
    if (old.folders.size() != 1 || old.folders[0] != 2 || !old.playing.empty()) Fail("a world with no id takes only the folders with no id");
    if (!WorldDeleteFind(d, "", id, keep, "pers", 3).folders.empty()) Fail("no world name: nothing");
    if (!SaveDirGoesWithProfile(d[0], "Ridge", id, "pers.1", 1)) Fail("the profile's own folder goes");
    if (SaveDirGoesWithProfile(d[1], "Ridge", id, "pers.1", 1)) Fail("the same profile in another world of that name stays");
    if (!SaveDirGoesWithProfile(d[2], "Ridge", id, "pers.2", 2)) Fail("the profile's folder from before ids goes");
    if (SaveDirGoesWithProfile(d[2], "Ridge", id, "pers.1", 1)) Fail("another profile's folder stays");
    if (SaveDirGoesWithProfile(d[4], "Ridge", id, "pers.1", 1) || SaveDirGoesWithProfile(d[5], "Ridge", id, "pers.1", 1)) Fail("another world's or an unkeyed folder stays");
    const coopprof::SaveDirSeen k1 = T490Dir("kc", "Ridge", "", "", 0LL);
    if (!SaveDirGoesWithProfile(k1, "Ridge", id, "pers.1", 1) || SaveDirGoesWithProfile(k1, "Ridge", id, "pers.2", 2)) Fail("a key file naming no profile is profile 1's");
    std::vector<std::string> wk, pk; wk.push_back("Ridge"); pk.push_back("pers.1");
    const std::vector<std::string> both = StoreNamesToDelete(wk, pk, id);
    if (both.size() != 2 || both[0] != coopown::PlayerStoreName("Ridge", "pers.1", id) || both[1] != coopown::PlayerStoreName("Ridge", "pers.1"))
        Fail("the records store by id and the one named before ids");
    if (StoreNamesToDelete(wk, pk, "").size() != 1) Fail("a world with no id: the store named before ids only");
    /* a root needs two parts below its drive or server */
    if (PathInside("C:\\Data", "C:\\") || PathInside("C:\\Data", "C:") || PathInside("C:\\Data\\worlds", "C:\\Data")) Fail("a drive root, or a folder one part below it, is never a root");
    if (PathInside("\\\\nas\\share", "\\\\nas") || PathInside("\\\\nas\\share\\saves", "\\\\nas\\share") || PathInside("\\\\nas\\share\\saves", "\\\\nas\\")) Fail("a bare server or share is never a root");
    if (PathPartsBelowTop("c:\\data\\worlds") != 2 || PathPartsBelowTop("\\\\nas\\share\\saves") != 2 || PathPartsBelowTop("c:\\data") != 1
        || PathPartsBelowTop("\\\\nas\\share") != 1 || PathPartsBelowTop("c:") != 0 || PathPartsBelowTop("\\\\nas") != 0) Fail("the parts below the drive or server");
    if (!PathDirectChild("C:\\Data\\saves\\Ridge - Bob", "C:\\Data\\saves") || PathDirectChild("C:\\Data\\saves\\a\\b", "C:\\Data\\saves")
        || PathDirectChild("C:\\Data\\saves", "C:\\Data\\saves") || PathDirectChild("C:\\Other\\a", "C:\\Data\\saves")) Fail("a folder directly in the root only");
    /* the top folder's attributes: only not-found reads as already gone */
    if (TopStateOf(true, 0) != kTopPresent || TopStateOf(false, 2) != kTopGone || TopStateOf(false, 3) != kTopGone) Fail("read = there; file or path not found = gone");
    if (TopStateOf(false, 5) != kTopUnreadable || TopStateOf(false, 32) != kTopUnreadable || TopStateOf(false, 0) != kTopUnreadable) Fail("access denied, sharing or no reason = a stop");
    /* read-only is cleared on files and folders, never through a link */
    if (!ClearReadOnlyFirst(0x01) || !ClearReadOnlyFirst(0x01 | 0x10) || ClearReadOnlyFirst(0x01 | 0x400) || ClearReadOnlyFirst(0x01 | 0x10 | 0x400) || ClearReadOnlyFirst(0x20))
        Fail("read-only taken off a plain file or folder only");
    /* the order of the top folder's last files */
    if (LastRank("a.dat", "world.txt", 0) != 0 || LastRank("World.TXT", "world.txt", 0) != 1 || LastRank(kDeleteMarkFile, "world.txt", 0) != 2
        || LastRank("BEING-DELETED.TXT", 0, 0) != 2 || LastRank("x", 0, 0) != 0) Fail("world.txt after everything, the marker the very last");
    if (LastRank(kDeleteMarkFile, "world.txt", 1) != 0 || LastRank("world.txt", "world.txt", 1) != 0) Fail("below the top folder: the walk's order");
    /* an unfinished delete: a real folder holding the marker as a plain file */
    if (!UnfinishedDeleteFolder(true, 0x10, true, 0x20) || !UnfinishedDeleteFolder(true, 0x10, true, 0x80)) Fail("a marked folder is an unfinished delete");
    if (UnfinishedDeleteFolder(true, 0x10, false, 0) || UnfinishedDeleteFolder(false, 0, true, 0x20) || UnfinishedDeleteFolder(true, 0x20, true, 0x20)
        || UnfinishedDeleteFolder(true, 0x10 | 0x400, true, 0x20) || UnfinishedDeleteFolder(true, 0x10, true, 0x10) || UnfinishedDeleteFolder(true, 0x10, true, 0x20 | 0x400))
        Fail("no marker, no folder, a link, or a marker that is a folder or a link: not an unfinished delete");
    /* the unfinished-deletes list */
    PendingProfileDelete pd; pd.worldKey = "Ridge"; pd.worldId = id; pd.person = "pers"; pd.num = 12;
    if (PendingLine(pd) != "profdel1\tRidge\t" + id + "\tpers\t12\t") Fail("the list line");
    PendingProfileDelete back;
    if (!PendingParse(PendingLine(pd) + "\r", &back) || !PendingSame(back, pd) || back.num != 12 || !back.stoppedAt.empty()) Fail("a line reads back");
    pd.stoppedAt = "C:\\Saves\\Ridge - Bob";
    if (!PendingParse(PendingLine(pd), &back) || back.stoppedAt != pd.stoppedAt || !PendingSame(back, pd)) Fail("the folder it stopped at reads back");
    if (!PendingParse("profdel1\tRidge\t\tpers\t1\t", &back) || !back.worldId.empty() || back.num != 1) Fail("a world with no id is listed");
    PendingProfileDelete bad = pd; bad.worldKey = "Ri\tdge";
    PendingProfileDelete bad2 = pd; bad2.num = 0;
    PendingProfileDelete bad3 = pd; bad3.person.clear();
    PendingProfileDelete bad4 = pd; bad4.stoppedAt = "a\nb";
    if (!PendingLine(bad).empty() || !PendingLine(bad2).empty() || !PendingLine(bad3).empty() || !PendingLine(bad4).empty()) Fail("a delete that cannot be one line is not listed");
    if (PendingParse("profdel1\tRidge\t\tpers\t0\t", &back) || PendingParse("profdel1\tRidge\t\tpers\tx\t", &back) || PendingParse("profdel2\tRidge\t\tpers\t1\t", &back)
        || PendingParse("profdel1\tRidge\t\tpers\t1", &back) || PendingParse("profdel1\t\t\tpers\t1\t", &back) || PendingParse("profdel1\tRidge\t\t\t1\t", &back)
        || PendingParse("profdel1\tRidge\t\tpers\t1234567890\t", &back) || PendingParse("", &back)) Fail("a bad line is not read");
    PendingProfileDelete other = pd; other.num = 3; other.stoppedAt.clear();
    std::vector<std::string> lst; lst.push_back(PendingLine(other)); lst.push_back("junk");
    const std::vector<std::string> l2 = PendingListSet(lst, pd, true);
    if (l2.size() != 2 || l2[0] != PendingLine(other) || l2[1] != PendingLine(pd)) Fail("a delete is added after the others; an unreadable line is left out");
    PendingProfileDelete again = pd; again.stoppedAt.clear();
    const std::vector<std::string> l3 = PendingListSet(l2, again, true);
    if (l3.size() != 2 || l3[1] != PendingLine(again)) Fail("the same delete set again replaces its line");
    const std::vector<std::string> l4 = PendingListSet(l3, pd, false);
    if (l4.size() != 1 || l4[0] != PendingLine(other)) Fail("a done delete's line is dropped, the others kept");
    const std::vector<std::string> sp = PendingSplit("a\r\n\r\nb\nc");
    if (sp.size() != 3 || sp[0] != "a" || sp[1] != "b" || sp[2] != "c" || !PendingSplit("").empty()) Fail("the list file's lines");
    if (coopui::WorldDeletedText("Ridge") != "\"Ridge\" deleted."
        || coopui::WorldDeleteFailText("Ridge") != "Couldn't delete \"Ridge\": another program is using its files."
        || coopui::ProfileDeletedText("Bob") != "Profile 'Bob' deleted."
        || coopui::ProfileDeleteFailText("Bob") != "Couldn't delete profile 'Bob': another program is using its save.")
        Fail("the approved words (owner 685)");
}
void t_t490_welcome_remade_refused()
{
    using namespace coopworld;
    const std::string A = "1791234567-3fa9c01e", B = "1791239999-00000002";
    if (WelcomeWorldDecideId("Ridge", "Ridge", "ridge", A, B) != kWelcomeWorldRefuseRemade) Fail("the same name with another id after the index: refused as remade");
    if (WelcomeWorldDecideId("Ridge", "Ridge", "ridge", A, A) != kWelcomeWorldConfirm) Fail("the same id: confirmed");
    if (WelcomeWorldDecideId("Other", "Ridge", "ridge", A, B) != kWelcomeWorldRefuseRemade) Fail("an adopted name remade: refused as remade");
    if (WelcomeWorldDecideId("Ridge", "Ridge", "", "", B) != kWelcomeWorldConfirm) Fail("no index read yet: confirmed, whatever the id");
    if (WelcomeWorldDecideId("Ridge", "Ridge", "ridge", "", B) != kWelcomeWorldConfirm || WelcomeWorldDecideId("Ridge", "Ridge", "ridge", A, "") != kWelcomeWorldConfirm)
        Fail("a side without an id: the name alone, as before ids");
    if (WelcomeWorldDecideId("Ridge", "North", "ridge", A, B) != kWelcomeWorldRefuse) Fail("another name after the index: the name refusal, unchanged");
    if (WelcomeWorldDecideId("Ridge", "", "ridge", A, B) != kWelcomeWorldNone) Fail("no world named: nothing changes");
    if (coopui::PressWrongWorldBox(coopui::kWrongWorldRemade, 1, "Ridge", "Ridge") != coopui::PressRemadeWorldText("Ridge")
        || coopui::PressWrongWorldBox(coopui::kWrongWorldName, 1, "A", "B") != coopui::PressWrongWorldText("A", "B"))
        Fail("the press box: the remade-world words for a remade world, the approved name words otherwise");
}
void t_t490_joined_dir_choice()
{
    using namespace coopworld;
    const std::string id = "1791234567-3fa9c01e";
    const std::string mine = WorldTxtFormatWithId("Ridge", 1791234567LL, id, false);
    const std::string theirs = WorldTxtFormatWithId("Ridge", 1700000000LL, "1700000000-00000001", false);
    if (WorldDataDirDecide(mine, id) != kDataDirOwn) Fail("this computer runs that very world: its own folder");
    if (WorldDataDirDecide(theirs, id) != kDataDirJoined) Fail("this computer runs another world of that name: the joined folder");
    if (WorldDataDirDecide("", id) != kDataDirJoined) Fail("no world of that name here: the joined folder");
    if (WorldDataDirDecide(WorldTxtFormat("Ridge", 5), id) != kDataDirJoined) Fail("a world.txt without an id is not that world");
    if (WorldDataDirDecide(theirs, "") != kDataDirOwn) Fail("no id in the WELCOME: the folder of today");
    if (JoinedDirIn("D:\\data\\joined\\", "Ridge", id) != "D:\\data\\joined\\ridge~3fa9c01e" || JoinedDirIn("", "Ridge", id) != "" || JoinedDirIn("D:\\j", "Ridge", "") != "")
        Fail("the joined folder: <top>\\<folder>~<8 hex>; no top or no id = none");
    if (coopdata::JoinedDir("C:\\d") != "C:\\d\\joined" || coopdata::JoinedDir("") != "" || std::string(swnames::kJoinedSub) != "joined") Fail("the joined top folder, from names.h");
    if (!JoinedQueueTakeDecide(false, true, true) || JoinedQueueTakeDecide(true, true, true) || JoinedQueueTakeDecide(false, true, false) || JoinedQueueTakeDecide(false, false, true))
        Fail("the old queue moves only out of a folder that is no world here, for an upgraded world");
}
void t_t429_format_decide()
{
    using namespace swformat;
    unsigned int n = 7;
    if (FormatText(kKindWorld, 1) != "swformat\t1\tworld\n" || FormatText(kKindJoined, 3) != "swformat\t3\tjoined\n" || FormatText(kKindRecords, 1) != "swformat\t1\trecords\n")
        Fail("the line: swformat<TAB><n><TAB><kind>");
    if (FormatParse(false, "", kKindWorld, &n) != kFormatMissing || n != 0) Fail("no file: missing");
    if (FormatParse(true, FormatText(kKindWorld, 4), kKindWorld, &n) != kFormatRead || n != 4) Fail("a round trip");
    if (FormatParse(true, "swformat\t2\trecords\r\n", kKindRecords, &n) != kFormatRead || n != 2) Fail("a CRLF line reads");
    if (FormatParse(true, FormatText(kKindWorld, 1), kKindRecords, &n) != kFormatUnreadable || FormatParse(true, "", kKindWorld, &n) != kFormatUnreadable
        || FormatParse(true, "swformat\tx\tworld\n", kKindWorld, &n) != kFormatUnreadable || FormatParse(true, "swformat\t0\tworld\n", kKindWorld, &n) != kFormatUnreadable
        || FormatParse(true, "swformat\t1234567890\tworld\n", kKindWorld, &n) != kFormatUnreadable)
        Fail("unreadable: another kind, empty, not a number, zero, too long");
    if (FormatDecide(kFormatMissing, 0, 1) != kFormatConvert) Fail("missing: convert");
    if (FormatDecide(kFormatRead, 1, 3) != kFormatConvert) Fail("lower: convert");
    if (FormatDecide(kFormatRead, 1, 1) != kFormatUse) Fail("equal: use");
    if (FormatDecide(kFormatRead, 99, 1) != kFormatRefuseNewer) Fail("higher: refused");
    if (FormatDecide(kFormatUnreadable, 0, 1) != kFormatRefuseUnreadable) Fail("unreadable: refused");
    if (!FormatRefuses(kFormatRefuseNewer) || !FormatRefuses(kFormatRefuseUnreadable) || FormatRefuses(kFormatUse) || FormatRefuses(kFormatConvert)) Fail("which verdicts refuse");
    const std::vector<unsigned int> s = FormatSteps(1, 4);
    if (s.size() != 3 || s[0] != 1 || s[1] != 2 || s[2] != 3 || FormatSteps(0, 1).size() != 1 || FormatSteps(0, 1)[0] != 0 || !FormatSteps(2, 2).empty())
        Fail("the steps run in order from the found number to the known one");
    if (FolderFormatKnown(kKindWorld) != 3 || FolderFormatKnown(kKindJoined) != 1 || FolderFormatKnown(kKindRecords) != 1) Fail("world 3 (T-811: player-towns format 3), joined and records 1");
    if (std::string(swnames::kFormatFile) != "format.txt") Fail("the file name, from names.h");
}
void t_t490_t429_words()
{
    if (coopui::PressRemadeWorldText("Ridge") != "This game already opened an earlier world named \"Ridge\". The new one needs a restart.") Fail("M2, word for word");
    if (std::string(swformat::FormatHostRefusedText()) != "Couldn't host: this world was saved by a newer version of Shared Wastelands.")
        Fail("M4, word for word");
    if (std::string(swformat::FormatJoinRefusedText()) != "Couldn't join: your copy of this world was saved by a newer version of Shared Wastelands.")
        Fail("M5, word for word");
    if (swformat::FormatLoadRefusedText() != "You could not load that save. It was made by a newer version of Shared Wastelands.")
        Fail("M6, word for word");
    if (std::string(swformat::FormatLoadRefusedWords()) + "." != "It was made by a newer version of Shared Wastelands.") Fail("M6's panel line");
    if (std::string(swformat::FormatHostRefusedText()).find(swnames::kModName) == std::string::npos) Fail("the mod's name comes from names.h");
    if (coopui::NoticeTitle(swformat::FormatHostRefusedText()) != std::string("CAN'T HOST") || coopui::NoticeTitle(swformat::FormatJoinRefusedText()) != std::string("CAN'T JOIN")
        || coopui::NoticeTitle(swformat::FormatLoadRefusedText()) != std::string("CAN'T LOAD"))
        Fail("M4/M5/M6 keep the CAN'T HOST / CAN'T JOIN / CAN'T LOAD titles");
    /* M1: a load refused as another world's save says the existing automatic-load line, word for word */
    if (std::string(coopprof::kLoadAutoText) != "In multiplayer your game loads automatically."
        || coopui::NoticeTitle(coopprof::kLoadAutoText) != std::string("CAN'T LOAD"))
        Fail("M1 reuses kLoadAutoText under CAN'T LOAD");
    if (coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, swformat::kServerExitFormatRefused) != swformat::FormatHostRefusedText())
        Fail("the world server's exit code 5 shows M4");
    if (coopui::PanelNotebookText(coopui::kNbStoppedEarly, 27016, 2).find("error 2") == std::string::npos) Fail("other exit codes keep their line");
    if (coopui::PressWrongWorldBox(coopui::kWrongWorldNewerFormat, 0, "", "") != swformat::FormatHostRefusedText()
        || coopui::PressWrongWorldBox(coopui::kWrongWorldNewerFormat, 1, "", "") != swformat::FormatJoinRefusedText())
        Fail("a newer joined folder: M4 under HOST, M5 under JOIN");
}

void t_t490_fold_decisions()
{
    using namespace coopworld;
    const std::string I = "1791234567-3fa9c01e", L = "1791239999-00000002", F = "1700000000-00000001";
    /* which id a key write takes */
    if (KeyWriteWorldId(true, I, L, F) != I || KeyWriteWorldId(true, I, "", "") != I) Fail("after the index read: the index's world id");
    if (KeyWriteWorldId(true, "", L, F) != F) Fail("an index read without an id keeps the file's field 8");
    if (KeyWriteWorldId(false, "", L, F) != L) Fail("before the index read: the accepted WELCOME's id");
    if (KeyWriteWorldId(false, "", "", F) != F || KeyWriteWorldId(false, "", "", "") != "") Fail("no accepted identity: the file's own field 8, kept");
    /* a world whose creation time is unknown adopts nothing */
    if (SaveIdentityDecide("", 1791234600LL, I, 0, true, kWorldIdAdoptSlackSec) != kIdOther) Fail("created 0: no adoption");
    if (WorldIdCreated(WorldIdMake(0, 1)) != 0 || !WorldIdOk(WorldIdMake(0, 1))) Fail("an id with created 0 is valid and reads 0");
    /* the creation time a new id carries */
    bool up = true;
    if (WorldIdCreatedChoose(false, 0, 0, 1800000000LL, &up) != 1800000000LL || up) Fail("no world.txt, no records: now, born");
    if (WorldIdCreatedChoose(false, 0, 1700000000LL, 1800000000LL, &up) != 1700000000LL || !up) Fail("no world.txt, records: the oldest record, upgraded");
    if (WorldIdCreatedChoose(true, 1750000000LL, 1700000000LL, 1800000000LL, &up) != 1700000000LL || !up) Fail("line 1 after the records: the oldest record");
    if (WorldIdCreatedChoose(true, 1650000000LL, 1700000000LL, 1800000000LL, &up) != 1650000000LL || !up) Fail("line 1 before the records: line 1");
    if (WorldIdCreatedChoose(true, 1650000000LL, 0, 1800000000LL, &up) != 1650000000LL || WorldIdCreatedChoose(true, 0, 0, 1800000000LL, &up) != 0)
        Fail("no records: line 1; nothing known: 0");
    /* why a format.txt is unreadable */
    std::string why; unsigned int n = 0;
    if (swformat::FormatParse(true, "", swformat::kKindWorld, &n, &why) != swformat::kFormatUnreadable || why.empty()) Fail("unreadable: empty, with a reason");
    if (swformat::FormatParse(true, "swformat\t1\trecords\n", swformat::kKindWorld, &n, &why) != swformat::kFormatUnreadable || why.find("records") == std::string::npos)
        Fail("unreadable: the wrong kind, named");
    if (swformat::FormatParse(true, "swformat\t1\tworld\n", swformat::kKindWorld, &n, &why) != swformat::kFormatRead || !why.empty()) Fail("readable: no reason");
}

void t_t490_queue_merge_older_first()
{
    using namespace coopqueue;
    int sup1 = -1, plain = -1;
    for (int f = 0; f < kQueueFamilyCount; ++f) { if (QueueFamilySupersedes(f) != 0) { if (sup1 < 0) sup1 = f; } else if (plain < 0) plain = f; }
    if (sup1 < 0 || plain < 0) { Fail("a superseding and a plain family exist"); return; }
    QueueEntry a; a.family = sup1; a.key = "door.1"; a.seqSeen = 1; a.ownAhead = 1;
    QueueEntry b; b.family = plain; b.key = "rec.1"; b.seqSeen = 2;
    QueueEntry c; c.family = sup1; c.key = "door.1"; c.seqSeen = 9;
    QueueEntry d; d.family = plain; d.key = "rec.1"; d.seqSeen = 7;
    std::vector<QueueEntry> older, newer; older.push_back(a); older.push_back(b); newer.push_back(c); newer.push_back(d);
    long long s = 0;
    const std::vector<QueueEntry> m = QueueMergeOlderFirst(older, newer, &s);
    if (m.size() != 3 || s != 1) Fail("the superseding key is merged in place, the plain entries all kept");
    else if (m[0].seqSeen != 9 || m[0].ownAhead != 1 || m[1].seqSeen != 2 || m[2].seqSeen != 7) Fail("older first; the newer superseding entry takes the older's place and keeps ownAhead");
    if (QueueMergeOlderFirst(std::vector<QueueEntry>(), newer, &s).size() != 2 || s != 0) Fail("no older journal: the newer as it is");
}

void t_t490_lobby_folder_format()
{
    using namespace coopprof;
    const std::string id = "1791032144-aa0b0a11";
    const std::string ownOther = coopworld::WorldTxtFormatWithId("mptest-t912", 1700000000LL, "1700000000-00000001", false);
    const std::string ownSame = coopworld::WorldTxtFormatWithId("mptest-t912", 1791032144LL, id, false);
    const std::string f99j = swformat::FormatText(swformat::kKindJoined, 99), f1j = swformat::FormatText(swformat::kKindJoined, 1);
    const std::string f99w = swformat::FormatText(swformat::kKindWorld, 99), f1w = swformat::FormatText(swformat::kKindWorld, swformat::kWorldFolderFormat);
    bool joined = false; unsigned n = 0; std::string why;
    if (LinkFolderFormatDecide(ownOther, id, true, f1w, true, f99j, &n, &why, &joined) != swformat::kFormatRefuseNewer || !joined || n != 99)
        Fail("a joined folder at format 99 refuses the link (this computer runs another world of that name)");
    if (LinkFolderFormatDecide("", id, false, "", true, f99j, &n, &why, &joined) != swformat::kFormatRefuseNewer || !joined)
        Fail("a joined folder at format 99 refuses the link (no world of that name here)");
    if (LinkFolderFormatDecide("", id, false, "", true, f1j, &n, &why, &joined) != swformat::kFormatUse) Fail("a joined folder at this build's number is used");
    if (LinkFolderFormatDecide("", id, false, "", false, "", &n, &why, &joined) != swformat::kFormatConvert) Fail("no joined folder yet: converted when made");
    if (LinkFolderFormatDecide(ownSame, id, true, f99w, true, f1j, &n, &why, &joined) != swformat::kFormatRefuseNewer || joined)
        Fail("this computer runs that world: its own folder's format decides");
    if (LinkFolderFormatDecide(ownSame, id, true, f1w, true, f99j, &n, &why, &joined) != swformat::kFormatUse || joined) Fail("the joined folder is not judged for a world run here");
    if (LinkFolderFormatDecide("", id, false, "", true, "garbage", &n, &why, &joined) != swformat::kFormatRefuseUnreadable || why.empty()) Fail("an unreadable joined format refuses, with a reason");
    /* the lobby's answer carries the identity */
    Answer a; a.answers = kAnsList; a.cap = 3; a.hasWorld = true; a.world = "mptest-t912"; a.worldId = id; a.worldIdUpgraded = true;
    std::vector<char> b; EncodeAnswer(&b, a);
    Answer r;
    if (!DecodeAnswer(b, &r) || !r.hasWorld || r.world != a.world || r.worldId != id || !r.worldIdUpgraded) Fail("the PROFILES answer carries the world and its id");
    Answer o; o.cap = 3; b.clear(); EncodeAnswer(&b, o);   /* EncodeAnswer appends */
    if (!DecodeAnswer(b, &r) || r.hasWorld) Fail("an answer without the identity reads as before");
    b.clear(); EncodeAnswer(&b, a); b.resize(b.size() - 2);
    if (DecodeAnswer(b, &r)) Fail("a torn identity is not read");
}

void t_t414_world_leftovers()
{
    using namespace coopprof;
    std::vector<SaveDirSeen> d;
    d.push_back(Pp5Dir("Holdout - Multiplayer - Scott", "Holdout", "pers.1", 3, 1));   /* 0: this world, this person */
    d.push_back(Pp5Dir("renamed by hand", "HOLDOUT", "q.4", 2, 1));                    /* 1: this world, another person's profile */
    d.push_back(Pp5Dir("Other - Multiplayer - Scott", "Other", "pers.1", 1, 1));      /* 2: another world */
    d.push_back(Pp5Dir("plain single player", 0, "", 0, 0));                          /* 3: no key file */
    d.push_back(Pp5Dir("old kcworld1", "holdout", "", 0, 0));                          /* 4: this world, a key file naming no profile */
    d.push_back(Pp5Dir("Playing Now", "Holdout", "pers.2", 5, 9));                     /* 5: this world, the folder this game plays from */
    d.push_back(Pp5Dir("Holdout2", "Holdout2", "pers.1", 1, 1));                      /* 6: a longer name is another world */
    if (!SaveDirIsWorlds(d[1], "holdout") || SaveDirIsWorlds(d[2], "holdout") || SaveDirIsWorlds(d[3], "holdout") || SaveDirIsWorlds(d[6], "holdout"))
        Fail("the world test: key file world, case ignored; no key file and other names are not this world");
    std::vector<std::string> keep; keep.push_back(""); keep.push_back("PLAYING NOW");
    const WorldLeftovers lo = swdel::WorldDeleteFind(d, "holdout", "", keep, "pers", 3);
    if (lo.folders.size() != 3 || lo.folders[0] != 0 || lo.folders[1] != 1 || lo.folders[2] != 4) Fail("folders: every folder of this world, any profile, any name");
    if (lo.playing.size() != 1 || lo.playing[0] != 5) Fail("the folder this game plays from is listed apart (case ignored), never moved");
    if (lo.playingIds.size() != 1 || lo.playingIds[0] != "pers.2") Fail("the playing folder's profile is listed apart: its records stores stay");
    if (coopown::PlayerStoreNamesFor(lo.worldKeys, lo.playingIds).size() != 3) Fail("the playing profile's store under every world spelling is protected");
    if (lo.worldKeys.size() != 3 || lo.worldKeys[0] != "holdout" || lo.worldKeys[1] != "Holdout" || lo.worldKeys[2] != "HOLDOUT")
        Fail("world spellings: the name asked for, then each other spelling once");
    if (lo.profileIds.size() != 4 || lo.profileIds[0] != "pers.1" || lo.profileIds[1] != "q.4" || lo.profileIds[2] != "pers.2" || lo.profileIds[3] != "pers.3")
        Fail("profile ids: each folder's (no profile = this person's 1), then this person's numbers, each once");
    const WorldLeftovers none = swdel::WorldDeleteFind(d, "", "", keep, "pers", 3);
    if (!none.folders.empty() || !none.playing.empty() || !none.worldKeys.empty() || !none.profileIds.empty()) Fail("no world name: nothing");
    const WorldLeftovers nobody = swdel::WorldDeleteFind(d, "Holdout", "", std::vector<std::string>(), "", 3);
    if (nobody.folders.size() != 4 || nobody.profileIds.size() != 3) Fail("no person id: every folder, only the ids the key files name");
    if (!nobody.playing.empty() || !nobody.playingIds.empty()) Fail("nothing kept: no playing folder, no protected profile");
    std::vector<std::string> keepOld; keepOld.push_back("OLD KCWORLD1");
    const WorldLeftovers old = swdel::WorldDeleteFind(d, "Holdout", "", keepOld, "pers", 3);
    if (old.playingIds.size() != 1 || old.playingIds[0] != "pers.1") Fail("a playing folder whose key file names no profile protects this person's profile 1");
    if (kWorldLeftoverSweep < kCapMax) Fail("the sweep reaches at least the profile cap");
    /* the records store names */
    std::vector<std::string> w, p;
    w.push_back("Holdout"); w.push_back("holdout"); w.push_back("Holdout"); w.push_back("");
    p.push_back("pers.1"); p.push_back("q.4"); p.push_back("");
    const std::vector<std::string> n = coopown::PlayerStoreNamesFor(w, p);
    if (n.size() != 4) Fail("store names: two spellings x two ids, empties and repeats left out");
    if (n.size() == 4 && (n[0] != coopown::PlayerStoreName("Holdout", "pers.1") || n[1] != coopown::PlayerStoreName("Holdout", "q.4")
                          || n[2] != coopown::PlayerStoreName("holdout", "pers.1") || n[3] != coopown::PlayerStoreName("holdout", "q.4")))
        Fail("store names: the same names the player's own store is opened by");
    if (!coopown::PlayerStoreNamesFor(w, std::vector<std::string>()).empty()) Fail("no profile ids: no store names");
}

/* THE NAME TAG OVER ANOTHER PLAYER'S CHARACTERS (src/common/nametag.h, the header tags.cpp and relations.cpp compile). */
void t_nametag_level_of_relation()
{
    using namespace nametag;
    /* the game's own levels: ally at 50 or more, enemy at -30 or less, the edges included */
    if (LevelOf(50.0f, false) != kFriendly)        Fail("50 is an ally");
    if (LevelOf(49.99f, false) != kNeutral)        Fail("just under 50 is neutral");
    if (LevelOf(100.0f, false) != kFriendly)       Fail("100 is an ally");
    if (LevelOf(0.0f, false) != kNeutral)          Fail("0 is neutral");
    if (LevelOf(-29.99f, false) != kNeutral)       Fail("just above -30 is neutral");
    if (LevelOf(-30.0f, false) != kHostile)        Fail("-30 is an enemy");
    if (LevelOf(-100.0f, false) != kHostile)       Fail("-100 is an enemy");
    /* the ally flag makes an ally at any value above the enemy line; the enemy test ignores the flag */
    if (LevelOf(0.0f, true) != kFriendly)          Fail("the ally flag at 0 is an ally");
    if (LevelOf(-29.0f, true) != kFriendly)        Fail("the ally flag at -29 is an ally");
    if (LevelOf(-30.0f, true) != kHostile)         Fail("the ally flag does not lift -30 out of enemy");
    if (!(kFriendly < kNeutral && kNeutral < kHostile)) Fail("a higher level is worse");
}
void t_nametag_worse_of_two()
{
    using namespace nametag;
    const int lv[3] = { kFriendly, kNeutral, kHostile };
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
        {
            const int want = lv[a > b ? a : b];
            if (WorseLevel(lv[a], lv[b]) != want) Fail("WorseLevel(" + I(lv[a]) + "," + I(lv[b]) + ") != " + I(want));
        }
    /* they treat me as an enemy, I treat them as an ally - red */
    if (WorseLevel(LevelOf(100.0f, false), LevelOf(-100.0f, false)) != kHostile) Fail("one hostile direction makes the tag hostile");
    /* an unread direction does not count; none read is neutral */
    if (WorseLevel(kUnknown, kFriendly) != kFriendly)  Fail("unknown + friendly");
    if (WorseLevel(kHostile, kUnknown) != kHostile)    Fail("hostile + unknown");
    if (WorseLevel(kUnknown, kUnknown) != kNeutral)    Fail("both unknown is neutral");
    if (WorseLevel(7, -5) != kNeutral)                 Fail("out-of-range levels count as unknown");
}
void t_nametag_colours()
{
    using namespace nametag;
    struct Want { int level; int r, g, b; };
    const Want w[4] = { { kFriendly, 0x5c, 0xb4, 0x73 }, { kNeutral, 0xf0, 0xcd, 0x57 }, { kHostile, 0xe6, 0x51, 0x39 },
                        { kUnknown, 0xf0, 0xcd, 0x57 } };
    for (int i = 0; i < 4; ++i)
    {
        const Rgb c = LevelColour(w[i].level);
        const int r = (int)(c.r * 255.0f + 0.5f), g = (int)(c.g * 255.0f + 0.5f), b = (int)(c.b * 255.0f + 0.5f);
        if (r != w[i].r || g != w[i].g || b != w[i].b)
            Fail("level " + I(w[i].level) + " colour " + I(r) + "," + I(g) + "," + I(b) + " is not the approved one");
    }
}
void t_nametag_outline()
{
    using namespace nametag;
    /* one copy each way - up, down, left, right - every one exactly 1 px from the line */
    bool up = false, down = false, left = false, right = false;
    int sx = 0, sy = 0;
    for (int k = 0; k < kOutlineCopies; ++k)
    {
        int dx = 9, dy = 9;
        OutlineOffset(k, &dx, &dy);
        if (dx * dx + dy * dy != 1) Fail("outline copy " + I(k) + " is not 1 px from the line");
        if (dx == 0 && dy == -1) { if (up) Fail("two copies up"); up = true; }
        if (dx == 0 && dy == 1)  { if (down) Fail("two copies down"); down = true; }
        if (dx == -1 && dy == 0) { if (left) Fail("two copies left"); left = true; }
        if (dx == 1 && dy == 0)  { if (right) Fail("two copies right"); right = true; }
        sx += dx; sy += dy;
    }
    if (!(up && down && left && right)) Fail("the outline misses a side");
    if (sx != 0 || sy != 0)             Fail("the outline leans to one side");
    int dx = 9, dy = 9;
    OutlineOffset(kOutlineCopies, &dx, &dy);
    if (dx != 0 || dy != 0)             Fail("an index past the copies moves nothing");
}
/* T-545: THE PLAYERS TAB (src/common/playerstab.h, the header playerstab.cpp and relations.cpp compile). */
void t_playerstab_tab_place()
{
    using namespace playerstab;
    /* PLAYERS after the game's seven: none of the numbers the game's code switches on or compares with (0..6) is PLAYERS */
    if (kTabIndex != kEngineTabCount || kEngineTabCount != 7 || std::string(kTabCaption) != "PLAYERS") Fail("PLAYERS sits after AI");
    const char* order[7] = { "MAP", "FACTION", "RESEARCH", "CRAFTING", "SQUADS", "DIALOGUE", "AI" };
    for (int e = 0; e < 7; ++e)
    {
        if (std::string(EngineTabCaption(e)) != order[e]) Fail("the game's tab " + I(e) + " is " + order[e]);
        if (e == kTabIndex || std::string(EngineTabCaption(e)) == kTabCaption) Fail("game tab " + I(e) + " is on PLAYERS");
    }
    if (std::string(EngineTabCaption(kTabIndex)) != "" || std::string(EngineTabCaption(-1)) != "") Fail("PLAYERS's position is no game tab");
    for (int k = 0; k < kEngineTabNumbersUsed; ++k)
    {
        const int n = EngineTabNumberUsed(k);
        if (n < 0 || n >= kEngineTabCount || n == kTabIndex) Fail("a number the game's code uses (" + I(n) + ") is PLAYERS or no game tab");
    }
    if (EngineTabNumberUsed(kEngineTabNumbersUsed) != -1) Fail("past the list");
    if (std::string(EngineTabCaption(kFactionTab)) != "FACTION" || std::string(EngineTabCaption(3)) != "CRAFTING") Fail("FACTION / the tutorial's CRAFTING keep their positions");
}
void t_playerstab_rows()
{
    using namespace playerstab;
    RowIn in;
    in.slot = 3; in.rosterName = " Kim "; in.placeholder = "Player 3"; in.faction = "Iron Wolves";
    in.you = nametag::kFriendly; in.them = nametag::kHostile; in.online = true;
    RowView r = RowFor(in);
    /* the name in the tag's colour: the worse of the two (they are hostile -> red) */
    if (r.cell[0] != "#e65139Kim") Fail("player cell: " + r.cell[0]);
    if (r.cell[1] != "Iron Wolves" || r.cell[2] != "Ally" || r.cell[3] != "Hostile" || r.cell[4] != "Online") Fail("row cells");
    if (r.you != nametag::kFriendly || r.tag != nametag::kHostile || r.faction != "Iron Wolves" || r.slot != 3) Fail("row levels");
    /* both ally -> green; offline */
    in.them = nametag::kFriendly; in.online = false;
    r = RowFor(in);
    if (r.cell[0] != "#5cb473Kim" || r.cell[4] != "Offline") Fail("ally row: " + r.cell[0] + " " + r.cell[4]);
    /* no roster name: the name remembered this session, else the placeholder */
    in.rosterName = ""; in.rememberedName = "Kim"; r = RowFor(in);
    if (r.cell[0] != "#5cb473Kim") Fail("remembered name");
    in.rememberedName = "  "; r = RowFor(in);
    if (r.cell[0] != "#5cb473Player 3") Fail("placeholder name: " + r.cell[0]);
    /* unread directions count as neutral (the tag's rule), and the YOU column then reads Neutral */
    in.you = nametag::kUnknown; in.them = nametag::kUnknown; r = RowFor(in);
    if (r.cell[2] != "Neutral" || r.cell[3] != "Neutral" || r.you != nametag::kNeutral || r.cell[0].compare(0, 7, "#f0cd57") != 0) Fail("unread row");
    /* a '#' typed in a name or a faction is shown, never read as a colour */
    in.rosterName = "#1 Kim"; in.faction = "The #Blades"; r = RowFor(in);
    if (r.cell[0] != "#f0cd57##1 Kim" || r.cell[1] != "The ##Blades" || r.faction != "The #Blades") Fail("hash escaped: " + r.cell[0] + " / " + r.cell[1]);
    if (ColourCode(nametag::kFriendly) != "#5cb473" || ColourCode(nametag::kNeutral) != "#f0cd57" || ColourCode(nametag::kHostile) != "#e65139") Fail("tag colours");
    /* order and identity */
    std::vector<RowView> v(3);
    v[0].slot = 7; v[1].slot = 2; v[2].slot = 5;
    SortBySlot(&v);
    if (v[0].slot != 2 || v[1].slot != 5 || v[2].slot != 7) Fail("rows by slot");
    std::vector<RowView> w = v;
    if (!SameSlots(v, w)) Fail("same slots");
    w[1].slot = 6;
    if (SameSlots(v, w)) Fail("a different player is a rebuild");
    w.pop_back();
    if (SameSlots(v, w)) Fail("a player gone is a rebuild");
    if (RowOfSlot(v, 5) != 1 || RowOfSlot(v, 9) != -1) Fail("row of slot");
}
void t_playerstab_bottom_and_press()
{
    using namespace playerstab;
    std::vector<RowView> rows(2);
    rows[0].slot = 1; rows[0].faction = "Dust Kings"; rows[0].you = nametag::kNeutral;
    rows[1].slot = 4; rows[1].faction = "Iron Wolves"; rows[1].you = nametag::kHostile;
    Bottom b = BottomFor(rows, -1);
    if (b.selected || b.line != "" || b.ticked != -1) Fail("nothing selected");
    b = BottomFor(rows, 4);
    if (!b.selected || b.line != "YOUR STANCE TOWARDS IRON WOLVES" || b.ticked != 2) Fail("selected: " + b.line);
    b = BottomFor(rows, 9);
    if (b.selected || b.line != "") Fail("a selected player no longer listed");
    /* exactly one ticked, in the buttons' order ALLY NEUTRAL HOSTILE */
    if (std::string(ButtonCaption(0)) != "ALLY" || std::string(ButtonCaption(1)) != "NEUTRAL" || std::string(ButtonCaption(2)) != "HOSTILE") Fail("button captions");
    if (std::string(LevelVerb(0)) != "ally" || std::string(LevelVerb(1)) != "neutral" || std::string(LevelVerb(2)) != "hostile") Fail("relate words");
    for (int k = 0; k < 3; ++k)
        for (int t = -1; t < 3; ++t)
        {
            const int p = PressFor(k, t);
            const int want = (k == t) ? kPressNothing : (k == 2 ? kPressConfirm : kPressSet);
            if (p != want) Fail("press " + I(k) + " with " + I(t) + " ticked");
        }
    if (PressFor(3, 0) != kPressNothing || PressFor(-1, 0) != kPressNothing) Fail("no such button");
}
void t_playerstab_words()
{
    using namespace playerstab;
    if (BoxText(" Iron Wolves ") != "Become hostile towards Iron Wolves? Your people will attack theirs on sight, and theirs will fight back.") Fail("box text");
    if (std::string(kBoxTitle) != "SET HOSTILE" || std::string(kBoxCancel) != "CANCEL" || std::string(kBoxConfirm) != "HOSTILE") Fail("box words");
    if (MyLine(2, "Iron Wolves") != "You are now hostile towards Iron Wolves.") Fail("hostile line");
    if (MyLine(0, "Iron Wolves") != "You are now allies of Iron Wolves.") Fail("ally line");
    if (MyLine(1, "Iron Wolves") != "You are now neutral towards Iron Wolves.") Fail("neutral line");
    if (StanceLine("the rusty blades") != "YOUR STANCE TOWARDS THE RUSTY BLADES") Fail("stance line in capitals");
    const char* heads[5] = { "PLAYER", "FACTION", "YOU", "THEM", "STATUS" };
    for (int c = 0; c < kColumns; ++c) if (std::string(ColumnHead(c)) != heads[c]) Fail("column " + I(c));
    const int widths[6] = { 400, 657, 1200, 33, 0, -50 };
    for (int i = 0; i < 6; ++i)
    {
        int cw[kColumns];
        ColumnWidths(widths[i], cw);
        int sum = 0;
        for (int c = 0; c < kColumns; ++c) { if (cw[c] < 0) Fail("a negative column for width " + I(widths[i])); sum += cw[c]; }
        if (sum != (widths[i] > 0 ? widths[i] : 0)) Fail("columns fill the width " + I(widths[i]) + " (" + I(sum) + ")");
    }
    /* out of range: nothing */
    if (std::string(ButtonCaption(3)) != "" || std::string(ButtonCaption(-1)) != "") Fail("button caption out of range");
    if (std::string(LevelVerb(3)) != "" || std::string(LevelVerb(-1)) != "") Fail("level verb out of range");
    if (std::string(ColumnHead(kColumns)) != "" || std::string(ColumnHead(-1)) != "" || ColumnPercent(kColumns) != 0) Fail("column out of range");
    if (Fill("no key here", "F") != "no key here") Fail("Fill with no {1}");
    if (Fill("{1} and {1}", " F ") != "F and {1}") Fail("Fill fills the one {1} the engine's sentences hold");
    /* a '#' in a faction name is shown, never read as a colour, wherever the name appears */
    if (StanceLine("The #Blades") != "YOUR STANCE TOWARDS THE ##BLADES") Fail("stance line escapes: " + StanceLine("The #Blades"));
    if (BoxText("#1") != "Become hostile towards ##1? Your people will attack theirs on sight, and theirs will fight back.") Fail("box text escapes");
    if (MyLine(0, "#1") != "You are now allies of ##1." || MyLine(1, "#1") != "You are now neutral towards ##1." || MyLine(2, "#1") != "You are now hostile towards ##1.") Fail("message lines escape");
    std::vector<std::string> n = TheirNotices(0.0f, false, -100.0f, false, "#1");
    if (n.size() != 1 || n[0] != "##1 are now hostile towards you") Fail("their line escapes");
}
void t_playerstab_their_notices()
{
    using namespace playerstab;
    std::vector<std::string> n = TheirNotices(0.0f, false, -100.0f, false, "Iron Wolves");
    if (n.size() != 1 || n[0] != "Iron Wolves are now hostile towards you") Fail("neutral -> hostile");
    n = TheirNotices(-100.0f, false, 0.0f, false, "Iron Wolves");
    if (n.size() != 1 || n[0] != "Iron Wolves are no longer hostile towards you") Fail("hostile -> neutral");
    n = TheirNotices(0.0f, false, 100.0f, false, "Iron Wolves");
    if (n.size() != 1 || n[0] != "Iron Wolves are now your allies") Fail("neutral -> ally");
    n = TheirNotices(100.0f, false, 0.0f, false, "Iron Wolves");
    if (n.size() != 1 || n[0] != "Iron Wolves are no longer your ally") Fail("ally -> neutral");
    /* ally straight to hostile: both of the engine's lines, in its order */
    n = TheirNotices(100.0f, false, -100.0f, false, "Iron Wolves");
    if (n.size() != 2 || n[0] != "Iron Wolves are now hostile towards you" || n[1] != "Iron Wolves are no longer your ally") Fail("ally -> hostile");
    n = TheirNotices(-100.0f, false, 100.0f, false, "Iron Wolves");
    if (n.size() != 2 || n[0] != "Iron Wolves are no longer hostile towards you" || n[1] != "Iron Wolves are now your allies") Fail("hostile -> ally");
    /* the table's rule (nametag::LevelOf): -30 is hostile, 50 is ally - a line exactly when the THEM column changes level */
    if (TheirNotices(-29.0f, false, -30.0f, false, "F").size() != 1) Fail("-30 is the enemy line");
    if (TheirNotices(-30.0f, false, -29.0f, false, "F").size() != 1) Fail("leaving -30");
    if (TheirNotices(40.0f, false, 50.0f, false, "F").size() != 1) Fail("50 is ally, as the table shows it");
    if (TheirNotices(50.0f, false, 50.5f, false, "F").size() != 0) Fail("ally to ally is no line");
    if (TheirNotices(-100.0f, false, -100.0f, false, "F").size() != 0 || TheirNotices(0.0f, false, 10.0f, false, "F").size() != 0) Fail("no crossing, no line");
    /* the ally flag is the same rule: the table shows Ally, so the sentences say so */
    for (float v = -20.0f; v <= 40.0f; v += 10.0f)
    {
        const int lv = nametag::LevelOf(v, true);
        n = TheirNotices(v, true, v, false, "F");
        if (lv != nametag::kFriendly || n.size() != 1 || n[0] != "F are no longer your ally") Fail("flag cleared: no longer ally at " + I((int)v));
        n = TheirNotices(v, false, v, true, "F");
        if (n.size() != 1 || n[0] != "F are now your allies") Fail("flag set: now allies at " + I((int)v));
    }
    n = TheirNotices(-100.0f, true, 0.0f, true, "F");   /* the enemy test comes first: a flagged -100 is hostile, a flagged 0 ally */
    if (n.size() != 2 || n[0] != "F are no longer hostile towards you" || n[1] != "F are now your allies") Fail("flagged hostile -> flagged neutral");
    /* the engine's own tests, on the value alone */
    if (EngineNoticeCrossed(40.0f, 50.0f) || !EngineNoticeCrossed(50.0f, 50.5f) || !EngineNoticeCrossed(-29.0f, -30.0f) || !EngineNoticeCrossed(60.0f, 0.0f)) Fail("engine notice edges");
    if (EngineNoticeCrossed(0.0f, 10.0f) || EngineNoticeCrossed(-100.0f, -40.0f) || EngineNoticeCrossed(100.0f, 60.0f)) Fail("engine: no crossing, no notice");
}
/* one line per change of level the player has not been told of */
void t_playerstab_notice_book()
{
    using namespace playerstab;
    NoticeBook bk;
    /* first change: counted from the value held before the write */
    std::vector<std::string> n = bk.Live("B|A", 0.0f, false, 100.0f, false, "FB");
    if (n.size() != 1 || n[0] != "FB are now your allies") Fail("first live change");
    /* the same change again (a second road, a resend): nothing */
    if (!bk.Live("B|A", 0.0f, false, 100.0f, false, "FB").empty()) Fail("a change brought twice is shown once");
    /* this game's engine moved the pair across the lines (a fight) and printed its own sentences, and the pair stayed at the
       engine's value (no put-back): the owner's change to the same level adds nothing */
    if (!bk.EngineSaid("B|A", 100.0f, -100.0f, false)) Fail("the engine printed for 100 -> -100");
    if (!bk.Live("B|A", 100.0f, false, -100.0f, false, "FB").empty()) Fail("the engine already said it - no duplicate");
    /* an engine move with no crossing prints nothing and leaves the book */
    if (bk.EngineSaid("B|A", -100.0f, -90.0f, false)) Fail("no crossing, no engine notice");
    n = bk.Live("B|A", -100.0f, false, 0.0f, false, "FB");
    if (n.size() != 1 || n[0] != "FB are no longer hostile towards you") Fail("leaving hostile after the engine's line");
    /* a snapshot only moves what was told */
    bk.Quiet("C|A", -100.0f, false);
    if (!bk.Live("C|A", 0.0f, false, -100.0f, false, "FC").empty()) Fail("a snapshot told it already");
    n = bk.Live("C|A", -100.0f, false, 0.0f, false, "FC");
    if (n.size() != 1 || n[0] != "FC are no longer hostile towards you") Fail("after a snapshot");
    /* pairs are kept apart; a new world starts from the held values */
    if (bk.Size() != 2) Fail("two pairs told");
    bk.Clear();
    n = bk.Live("B|A", 0.0f, false, 100.0f, false, "FB");
    if (bk.Size() != 1 || n.size() != 1) Fail("a new world counts from the held value");
}
/* this game's engine moves the pair and prints, the mod puts it back to the owner's value (the book takes the value put back),
   then the owner's own change: exactly the sentences of the table's change, from the value put back to the owner's new one */
void t_playerstab_notice_putback()
{
    using namespace playerstab;
    NoticeBook bk;
    bk.Quiet("B|A", 0.0f, false);                                                   /* the owner's neutral, as the table shows it */
    if (!bk.EngineSaid("B|A", 0.0f, -100.0f, false)) Fail("the engine printed for 0 -> -100");
    bk.Quiet("B|A", 0.0f, false);                                                   /* put back to the owner's 0 */
    std::vector<std::string> n = bk.Live("B|A", 0.0f, false, -100.0f, false, "FB");  /* the owner turns hostile */
    if (n != TheirNotices(0.0f, false, -100.0f, false, "FB") || n.size() != 1 || n[0] != "FB are now hostile towards you") Fail("a real hostile after a put-back is shown");
    /* the engine moved an ally side to neutral and printed; put back to ally; the owner leaves ally: one line, not a false "allies" */
    bk.Quiet("C|A", 100.0f, false);
    if (!bk.EngineSaid("C|A", 100.0f, 0.0f, false)) Fail("the engine printed for 100 -> 0");
    bk.Quiet("C|A", 100.0f, false);
    n = bk.Live("C|A", 100.0f, false, 0.0f, false, "FC");
    if (n.size() != 1 || n[0] != "FC are no longer your ally") Fail("leaving ally after a put-back");
    /* the owner's change that stays at the level put back: nothing */
    bk.Quiet("D|A", 100.0f, false);
    if (!bk.EngineSaid("D|A", 100.0f, -50.0f, false)) Fail("the engine printed for 100 -> -50");
    bk.Quiet("D|A", 100.0f, false);
    if (!bk.Live("D|A", 100.0f, false, 80.0f, false, "FD").empty()) Fail("no change of level from the value put back");
}
/* a NEUTRAL or HOSTILE choice clears the ally flag; ALLY and an unset flag change nothing */
void t_playerstab_ally_flag()
{
    using namespace playerstab;
    if (ClearsAllyFlag(0, true) || !ClearsAllyFlag(1, true) || !ClearsAllyFlag(2, true)) Fail("which choices clear the flag");
    if (ClearsAllyFlag(1, false) || ClearsAllyFlag(2, false) || ClearsAllyFlag(3, true) || ClearsAllyFlag(-1, true)) Fail("nothing to clear / no such button");
    /* after the clear the row, the tick and the sentence agree: a flagged 0 is Ally; cleared, Neutral, and the other game says so */
    RowIn in; in.slot = 2; in.faction = "F"; in.you = nametag::LevelOf(0.0f, true); in.them = nametag::kNeutral;
    if (RowFor(in).cell[2] != "Ally") Fail("a flagged entry reads Ally");
    in.you = nametag::LevelOf(0.0f, false);
    if (RowFor(in).cell[2] != "Neutral" || RowFor(in).you != 1) Fail("cleared, it reads Neutral with NEUTRAL ticked");
    const std::vector<std::string> n = TheirNotices(0.0f, true, 0.0f, false, "F");
    if (n.size() != 1 || n[0] != "F are no longer your ally") Fail("and the other game's sentence follows");
}
/* a carried player's faction (record id coop-p<n>) travels and is keyed as "@slot:<n>" */
void t_slotwire_carried_faction()
{
    using namespace coopslot;
    if (StandInIdAsSlotWire("coop-p3", 1) != "@slot:3") Fail("coop-p3 travels as @slot:3");
    if (StandInIdAsSlotWire("coop-p3", -1) != "@slot:3") Fail("before this game has a slot");
    if (StandInIdAsSlotWire("coop-p1", 1) != "coop-p1") Fail("this game's own number is not another player");
    if (StandInIdAsSlotWire("coop-peer", 1) != "coop-peer" || StandInIdAsSlotWire("Nameless", 1) != "Nameless" || StandInIdAsSlotWire("coop-p03", 1) != "coop-p03") Fail("other ids unchanged");
    if (StandInIdAsSlotWire("@slot:3:Kim", 1) != "@slot:3:Kim") Fail("a wire name unchanged");
    /* one key for every name of one player */
    if (RelationSideKey("coop-p3", 1) != "@slot:3" || RelationSideKey("@slot:3", 1) != "@slot:3" || RelationSideKey("@slot:3:Kim", 1) != "@slot:3") Fail("one key per player");
    if (RelationSideKey("coop-p1", 1) != "coop-p1" || RelationSideKey("Nameless", 1) != "Nameless") Fail("other keys unchanged");
    /* the receiver judges the carried player's side as that player's, never the sender's */
    if (PairOwnerSlot(StandInIdAsSlotWire("coop-p3", 1), "@slot:1:Me") != 3) Fail("their side is slot 3's");
    if (PairOwnerSlot("@slot:1:Me", StandInIdAsSlotWire("coop-p3", 1)) != 1) Fail("my side towards them is mine");
    int slot = -1; bool hasName = true;
    if (!ParseSlotWire(StandInIdAsSlotWire("coop-p12", 0), &slot, 0, &hasName) || slot != 12 || hasName) Fail("@slot:12 has no name (never creates a stand-in)");
}
void t_playerstab_lever_parse()
{
    using namespace playerstab;
    Lever l = ParseLever("");
    if (l.kind != kLeverReport) Fail("bare is the report");
    l = ParseLever(" open ");
    if (l.kind != kLeverOpen) Fail("open");
    l = ParseLever("select 12");
    if (l.kind != kLeverSelect || l.slot != 12) Fail("select 12");
    l = ParseLever("stance hostile");
    if (l.kind != kLeverStance || l.button != 2) Fail("stance hostile");
    l = ParseLever("stance ally");
    if (l.kind != kLeverStance || l.button != 0) Fail("stance ally");
    if (ParseLever("confirm").kind != kLeverConfirm || ParseLever("cancel").kind != kLeverCancel) Fail("box verbs");
    if (ParseLever("faction").kind != kLeverFaction || ParseLever("faction 1").kind != kLeverBad) Fail("faction");
    l = ParseLever("select 1023");
    if (l.kind != kLeverSelect || l.slot != 1023) Fail("select 1023");
    if (ParseLever("select 12345").kind != kLeverBad || ParseLever("select 00012").kind != kLeverBad) Fail("a 5-digit select is refused");
    const char* bad[6] = { "select", "select -1", "select x", "stance friendly", "open now", "close" };
    for (int i = 0; i < 6; ++i) if (ParseLever(bad[i]).kind != kLeverBad) Fail(std::string("bad lever accepted: ") + bad[i]);
}
/* T-546 step 7: THE FACTION SCREENS (src/common/teamscreens.h) - every word from build/pages/player-factions.html */
void t_t546h_bottom_line()
{
    using namespace teamscreen;
    Sel s; s.faction = "Iron Wolves"; s.name = "Kim"; s.online = true; s.you = nametag::kHostile;
    View v = ViewFor(true, s);
    if (v.lineKind != kLineStance || v.line != "YOUR STANCE TOWARDS IRON WOLVES" || !v.stance || v.left != kActNone || v.right != kActInvite) Fail("in no faction: the stance line, the buttons, INVITE TO FACTION");
    s.online = false; v = ViewFor(true, s);
    if (v.right != kActNone || !v.stance) Fail("an offline player: no INVITE TO FACTION, the stance buttons stay");
    s.online = true; s.inTeam = true; v = ViewFor(true, s);
    if (v.right != kActNone || !v.stance) Fail("a player in another faction: no INVITE TO FACTION");
    s.inTeam = false; s.myRole = kRoleFounder; v = ViewFor(true, s);
    if (!v.stance || v.right != kActInvite || v.left != kActNone || v.lineKind != kLineStance) Fail("the founder towards a player outside: stance and INVITE");
    s.mate = true; s.name = "Sam"; v = ViewFor(true, s);
    if (v.lineKind != kLineMember || v.line != "SAM IS A MEMBER OF YOUR FACTION" || v.stance || v.left != kActDisband || v.right != kActRemove) Fail("B7.1: the founder with a member selected");
    s.myRole = kRoleMember; s.name = "Alex"; v = ViewFor(true, s);
    if (v.line != "ALEX IS A MEMBER OF YOUR FACTION" || v.stance || v.left != kActNone || v.right != kActLeave) Fail("a member with a teammate selected: LEAVE FACTION only");
    s.mate = false; s.name = "Kim"; s.founder = "Alex"; v = ViewFor(true, s);
    if (v.lineKind != kLineFounders || v.line != "STANCE TOWARDS IRON WOLVES: HOSTILE (SET BY ALEX)" || v.stance || v.right != kActLeave || v.left != kActNone) Fail("B7.2: a member towards a player outside");
    s.you = nametag::kFriendly; v = ViewFor(true, s);
    if (v.line != "STANCE TOWARDS IRON WOLVES: ALLY (SET BY ALEX)") Fail("the founder's ALLY");
    s.you = nametag::kUnknown; v = ViewFor(true, s);
    if (v.line != "STANCE TOWARDS IRON WOLVES: NEUTRAL (SET BY ALEX)") Fail("an unread level reads NEUTRAL, as the table");
    s.you = nametag::kHostile; s.founder = ""; v = ViewFor(true, s);
    if (v.line != "STANCE TOWARDS IRON WOLVES: HOSTILE") Fail("the founder's name unknown: no SET BY part");
    v = ViewFor(false, s);
    if (v.lineKind != kLineSelect || v.line != "" || v.right != kActLeave || v.stance) Fail("a member with nothing selected keeps LEAVE FACTION");
    s.myRole = kRoleFounder; v = ViewFor(false, s);
    if (v.right != kActNone || v.left != kActNone || v.stance) Fail("the founder with nothing selected: no button");
    s.myRole = kRoleNone; v = ViewFor(false, s);
    if (v.right != kActNone || v.left != kActNone) Fail("in no faction, nothing selected: no button");
    s.myRole = kRoleFounder; s.mate = true; s.name = ""; v = ViewFor(true, s);
    if (!v.line.empty() || v.right != kActNone || v.left != kActDisband) Fail("a member whose name is not known: no line, no REMOVE FROM FACTION (its box names the member), DISBAND stays");
    s.name = "  "; v = ViewFor(true, s);
    if (v.right != kActNone) Fail("a blank name is not known");
    s.myRole = kRoleNone; s.mate = true; s.name = "Sam"; v = ViewFor(true, s);
    if (v.lineKind != kLineStance) Fail("a teammate with no role here (no table) is read as anyone else");
    if (MemberLine("Sa#m") != "SA##M IS A MEMBER OF YOUR FACTION" || MemberLine("  ") != "") Fail("a name escaped, an empty name no line");
    if (FoundersStanceLine("A#B", nametag::kNeutral, "x#y") != "STANCE TOWARDS A##B: NEUTRAL (SET BY X##Y)") Fail("names escaped in the founder's line");
    if (RoleCell(true) != "Founder" || RoleCell(false) != "Member") Fail("the role cells");
    const char* caps[5] = { "", "INVITE TO FACTION", "REMOVE FROM FACTION", "DISBAND FACTION", "LEAVE FACTION" };
    for (int a = 0; a <= kActLast; ++a)
    {
        if (std::string(ActionCaption(a)) != caps[a]) Fail("action caption " + I(a));
        if (a > 0 && ActionOfWord(ActionWord(a)) != a) Fail("action word " + I(a));
    }
    if (ActionOfWord("promote") != kActNone || std::string(ActionCaption(9)) != "") Fail("no such action");
    if (BoxOfAction(kActInvite) != kBoxNone || BoxOfAction(kActRemove) != kBoxRemove || BoxOfAction(kActDisband) != kBoxDisband || BoxOfAction(kActLeave) != kBoxLeave) Fail("the box each action asks first with");
}
void t_t546h_boxes()
{
    using namespace teamscreen;
    if (std::string(BoxTitle(kBoxInvite)) != "FACTION INVITATION" || std::string(BoxLeft(kBoxInvite)) != "DECLINE" || std::string(BoxRight(kBoxInvite)) != "ACCEPT") Fail("B7.3 frame");
    if (BoxText(kBoxInvite, "The Rusty Blades", "Alex") != "Alex invites you to join The Rusty Blades. Members share research and standing with every faction, and can enter each other's bases. Your characters and money stay yours.") Fail("B7.3 words");
    if (std::string(BoxTitle(kBoxLeave)) != "LEAVE FACTION" || std::string(BoxLeft(kBoxLeave)) != "CANCEL" || std::string(BoxRight(kBoxLeave)) != "LEAVE") Fail("B7.4 frame");
    if (BoxText(kBoxLeave, "The Rusty Blades", "") != "Leave The Rusty Blades? You keep your research. Your standing with every faction goes back to what it was before you joined.") Fail("B7.4 words");
    if (std::string(BoxTitle(kBoxRemove)) != "REMOVE FROM FACTION" || std::string(BoxLeft(kBoxRemove)) != "CANCEL" || std::string(BoxRight(kBoxRemove)) != "REMOVE") Fail("B7.5 frame");
    if (BoxText(kBoxRemove, "The Rusty Blades", "Sam") != "Remove Sam from The Rusty Blades? Sam keeps their research. Their standing with every faction goes back to what it was before joining.") Fail("B7.5 words");
    if (std::string(BoxTitle(kBoxDisband)) != "DISBAND FACTION" || std::string(BoxLeft(kBoxDisband)) != "CANCEL" || std::string(BoxRight(kBoxDisband)) != "DISBAND") Fail("B7.6 frame");
    if (BoxText(kBoxDisband, "The Rusty Blades", "") != "Disband The Rusty Blades? Every member leaves the faction and keeps their research.") Fail("B7.6 words");
    if (std::string(BoxTitle(kBoxAway)) != "FACTION" || std::string(BoxLeft(kBoxAway)) != "" || std::string(BoxRight(kBoxAway)) != "OK") Fail("B7.7 frame: OK alone");
    if (BoxText(kBoxAway, "The Rusty Blades", "") != "You were removed from The Rusty Blades while you were away. You kept your research; your standing with every faction is back to what it was before you joined.") Fail("B7.7 words");
    if (std::string(BoxTitle(kBoxHostile)) != "SET HOSTILE" || std::string(BoxLeft(kBoxHostile)) != "CANCEL" || std::string(BoxRight(kBoxHostile)) != "HOSTILE"
        || BoxText(kBoxHostile, "", "Iron Wolves") != playerstab::BoxText("Iron Wolves")) Fail("SET HOSTILE through the same frame");
    if (!BoxText(kBoxInvite, "The Rusty Blades", "").empty() || !BoxText(kBoxInvite, "", "Alex").empty() || !BoxText(kBoxRemove, "T", " ").empty()
        || !BoxText(kBoxLeave, "", "").empty() || !BoxText(kBoxDisband, "", "").empty() || !BoxText(kBoxAway, "", "").empty() || !BoxText(kBoxNone, "T", "N").empty())
        Fail("a box whose words need a name not known is not shown");
    if (BoxText(kBoxRemove, "R#B", "S#m") != "Remove S##m from R##B? S##m keeps their research. Their standing with every faction goes back to what it was before joining.") Fail("names escaped in a box");
    if (std::string(BoxTitle(99)) != "" || std::string(BoxRight(-1)) != "") Fail("no such box");
    if (!InviteBoxDue(true, false, false, true) || InviteBoxDue(false, false, false, true) || InviteBoxDue(true, true, false, true) || InviteBoxDue(true, false, true, true)
        || InviteBoxDue(true, false, false, false)) Fail("the invitation box: waiting, no box of ours up, no blocking box up, the name known");
    if (!InviteBoxEnds(false, 3, 3) || !InviteBoxEnds(true, 4, 3) || InviteBoxEnds(true, 3, 3)) Fail("the invitation box ends when answered / ended or replaced");
}
void t_t546h_message_lines()
{
    using namespace teamscreen;
    const std::string T = "The Rusty Blades";
    if (NoticeLine(swteam::kEvInvite, swteam::kOk, false, "Sam", T) != "Invitation sent to Sam.") Fail("invite");
    if (NoticeLine(swteam::kEvExpired, swteam::kOk, false, "Sam", T) != "Sam did not answer." || NoticeLine(swteam::kEvExpired, swteam::kOk, true, "Sam", T) != "") Fail("expired: the founder's line; none for the invited player");
    if (NoticeLine(swteam::kEvDeclined, swteam::kOk, false, "Sam", T) != "Sam declined your invitation." || NoticeLine(swteam::kEvDeclined, swteam::kOk, true, "Sam", T) != "") Fail("declined");
    if (NoticeLine(swteam::kEvJoined, swteam::kOk, false, "Sam", T) != "Sam joined The Rusty Blades." || NoticeLine(swteam::kEvJoined, swteam::kOk, true, "Sam", T) != "You joined The Rusty Blades.") Fail("joined");
    if (NoticeLine(swteam::kEvLeft, swteam::kOk, false, "Sam", T) != "Sam left The Rusty Blades." || NoticeLine(swteam::kEvLeft, swteam::kOk, true, "Sam", T) != "You left The Rusty Blades.") Fail("left");
    if (NoticeLine(swteam::kEvRemoved, swteam::kOk, false, "Sam", T) != "Sam was removed from The Rusty Blades." || NoticeLine(swteam::kEvRemoved, swteam::kOk, true, "Sam", T) != "You were removed from The Rusty Blades.") Fail("removed");
    if (NoticeLine(swteam::kEvDisbanded, swteam::kOk, false, "Alex", T) != "The Rusty Blades was disbanded." || NoticeLine(swteam::kEvDisbanded, swteam::kOk, true, "Alex", T) != "The Rusty Blades was disbanded.") Fail("disbanded");
    if (NoticeLine(swteam::kEvJoined, swteam::kOk, true, "", T) != "You joined The Rusty Blades.") Fail("this player's own line needs no name");
    for (int e = 1; e <= swteam::kEvLast; ++e)
        for (int me = 0; me < 2; ++me)
        {
            if (e != swteam::kEvInvite && NoticeLine(e, swteam::kRefInTeam, me != 0, "Sam", T) != "") Fail("a refusal other than an invitation's has no line (event " + I(e) + ")");
            if (NoticeLine(e, swteam::kOk, me != 0, "", T).find("  ") != std::string::npos) Fail("no line with a missing name");
            if (me == 0 && NoticeLine(e, swteam::kOk, false, "", T) != "" && e != swteam::kEvDisbanded) Fail("another player's line needs the name (event " + I(e) + ")");
        }
    const int none[5] = { swteam::kEvAnswer, swteam::kEvLeave, swteam::kEvInviteGone, swteam::kEvResearchWhole, swteam::kEvResearchOwn };
    for (int k = 0; k < 5; ++k) if (NoticeLine(none[k], swteam::kOk, false, "Sam", T) != "" || NoticeLine(none[k], swteam::kOk, true, "Sam", T) != "") Fail("no page words for event " + I(none[k]));
    if (NoticeLine(swteam::kEvLeft, swteam::kOk, false, "S#m", "R#B") != "S##m left R##B.") Fail("names escaped");
    if (NoticeLine(swteam::kEvLeft, swteam::kOk, true, "Sam", "") != "") Fail("no faction name: no line");
    if (ResearchCompleteShown("Iron Plates", "Sam") != "Research complete: Iron Plates (Sam)." || ResearchCompleteShown("Iron Plates", " ") != "" || ResearchCompleteShown(" ", "Sam") != "")
        Fail("680: the research line names the teammate");
    if (ResearchCompleteShown("I#P", "S#m") != "Research complete: I##P (S##m).") Fail("680: the research line's names escaped");
    if (swteam::ResearchSharedLine(6) != "Shared research: 6 new technologies.") Fail("the shared research line");
}
/* T-546 owner 516: the words for the moments the page did not cover */
void t_t546l_invite_refused_lines()
{
    using namespace teamscreen;
    const std::string T = "The Rusty Blades";
    if (NoticeLine(swteam::kEvInvite, swteam::kRefInvitePending, false, "Sam", T) != "Sam already has an invitation.") Fail("516 a: an invitation waiting");
    if (NoticeLine(swteam::kEvInvite, swteam::kRefInTeam, false, "Sam", T) != "Sam is in another faction.") Fail("516 a: in another faction");
    if (NoticeLine(swteam::kEvInvite, swteam::kRefNotOnline, false, "Sam", T) != "Sam is offline.") Fail("516 a: offline");
    const int general[8] = { swteam::kRefDeclined, swteam::kRefUnknownPlayer, swteam::kRefSelf, swteam::kRefOwedFull, swteam::kRefTeamFull,
                             swteam::kRefTooMany, swteam::kRefNotSaved, swteam::kRefNoSnapshot };
    if (NoticeLine(swteam::kEvInvite, swteam::kRefNotFounder, false, "Sam", T) != "Only the founder can invite players." || NoticeLine(swteam::kEvInvite, swteam::kRefNotFounder, false, "", T) != "Only the founder can invite players.")
        Fail("522: a member who is not the founder is told only the founder can invite");
    for (int k = 0; k < 8; ++k)
        if (NoticeLine(swteam::kEvInvite, general[k], false, "Sam", T) != "The invitation could not be sent. Try again in a moment.") Fail("516 a: the general line for reason " + I(general[k]));
    if (NoticeLine(swteam::kEvInvite, swteam::kRefInvitePending, false, "", T) != "The invitation could not be sent. Try again in a moment.") Fail("516 a: a named reason with no name gives the general line");
    if (NoticeLine(swteam::kEvInvite, swteam::kRefNotOnline, true, "Sam", T) != "The invitation could not be sent. Try again in a moment.") Fail("516 a: a refusal naming this player itself names nobody");
    if (NoticeLine(swteam::kEvInvite, swteam::kRefInTeam, false, "S#m", T) != "S##m is in another faction.") Fail("516 a: the name escaped");
    if (NoticeLine(swteam::kEvAnswer, swteam::kRefNoInvite, true, "Sam", T) != "" || NoticeLine(swteam::kEvLeave, swteam::kRefNotInTeam, true, "Sam", T) != "") Fail("other refusals stay without a line");
    if (InviteRefusedLine(swteam::kOk, "Sam") != "") Fail("no refusal, no line");
}
void t_t546l_invitation_ended()
{
    using namespace teamscreen;
    const std::string T = "The Rusty Blades";
    if (NoticeLine(swteam::kEvExpired, swteam::kOk, true, "Sam", T, "Alex") != "The invitation from Alex has ended.") Fail("516 b: ran out");
    if (NoticeLine(swteam::kEvInviteGone, swteam::kOk, true, "Sam", T, "Alex") != "The invitation from Alex has ended.") Fail("516 b: withdrawn or the inviter left");
    if (NoticeLine(swteam::kEvExpired, swteam::kOk, false, "Sam", T, "Alex") != "Sam did not answer.") Fail("the inviter's own line is unchanged");
    if (NoticeLine(swteam::kEvInviteGone, swteam::kOk, false, "Sam", T, "Alex") != "") Fail("the inviter hears no line when its invitation is withdrawn");
    if (NoticeLine(swteam::kEvExpired, swteam::kOk, true, "Sam", T, "") != "" || NoticeLine(swteam::kEvInviteGone, swteam::kOk, true, "Sam", T, " ") != "") Fail("516 b: the inviter's name not known - no line");
    if (NoticeLine(swteam::kEvDeclined, swteam::kOk, true, "Sam", T, "Alex") != "") Fail("the invited player who declined hears nothing");
    if (NoticeLine(swteam::kEvExpired, swteam::kOk, true, "Sam", T, "A#x") != "The invitation from A##x has ended.") Fail("516 b: the name escaped");
}
void t_t546l_not_invitable_note()
{
    using namespace teamscreen;
    Sel s; s.faction = "Iron Wolves"; s.name = "Kim"; s.online = true;
    View v = ViewFor(true, s);
    if (v.right != kActInvite || !v.note.empty()) Fail("516 c: an invitable player shows INVITE TO FACTION, no words");
    s.online = false; v = ViewFor(true, s);
    if (v.right != kActNone || v.note != "Kim is offline." || !v.stance) Fail("516 c: offline");
    s.online = true; s.inTeam = true; v = ViewFor(true, s);
    if (v.right != kActNone || v.note != "Kim is in another faction.") Fail("516 c: in another faction");
    s.online = false; v = ViewFor(true, s);
    if (v.note != "Kim is offline.") Fail("516 c: offline comes first (the world server refuses it first)");
    s.myRole = kRoleFounder; v = ViewFor(true, s);
    if (v.note != "Kim is offline.") Fail("516 c: the founder's line too");
    s.name = ""; v = ViewFor(true, s);
    if (!v.note.empty()) Fail("516 c: a name not known - no words");
    s.name = "Kim"; s.myRole = kRoleMember; v = ViewFor(true, s);
    if (!v.note.empty() || v.right != kActLeave) Fail("a member's line keeps LEAVE FACTION and no words");
    s.myRole = kRoleNone; s.mate = false; v = ViewFor(false, s);
    if (!v.note.empty()) Fail("nothing selected - no words");
    if (NotInvitableNote(false, false, "K#m") != "K##m is offline." || NotInvitableNote(true, false, "Kim") != "") Fail("516 c: escaped; online and in no faction - none");
}
void t_t546l_away_disband_box()
{
    using namespace teamscreen;
    if (std::string(BoxTitle(kBoxAwayDisband)) != "FACTION" || std::string(BoxLeft(kBoxAwayDisband)) != "" || std::string(BoxRight(kBoxAwayDisband)) != "OK") Fail("516 d frame: FACTION, OK alone");
    if (BoxText(kBoxAwayDisband, "The Rusty Blades", "") != "The Rusty Blades was disbanded while you were away. You kept your research; your standing with every faction is back to what it was before you joined.")
        Fail("516 d words");
    if (!BoxText(kBoxAwayDisband, "", "").empty() || BoxText(kBoxAwayDisband, "R#B", "").find("R##B was disbanded") != 0) Fail("516 d: no name no box; escaped");
    if (AwayBoxOf(true, swteam::kWhyDisbanded, true) != kBoxAwayDisband || AwayBoxOf(true, swteam::kWhyRemoved, true) != kBoxAway) Fail("the box for each reason while away");
    if (AwayBoxOf(false, swteam::kWhyDisbanded, true) != kBoxNone || AwayBoxOf(true, swteam::kWhyDisbanded, false) != kBoxNone || AwayBoxOf(true, swteam::kWhyLeft, true) != kBoxNone)
        Fail("no box: made while connected, nothing put back, or a leave");
    if (!OkOnlyBox(kBoxAway) || !OkOnlyBox(kBoxAwayDisband) || OkOnlyBox(kBoxLeave) || OkOnlyBox(kBoxInvite)) Fail("the OK-only boxes");
}
void t_t546l_research_lines()
{
    if (swteam::ResearchSharedLine(1) != "Shared research: 1 new technology.") Fail("515: the singular line");
    if (teamscreen::ResearchCompleteShown("Iron Plates", "Sam") != "Research complete: Iron Plates (Sam).") Fail("680: the named line every time, whichever road the tech took");
}
bool T546FileRoundTrip(const swteam::Book& b);   /* below, with the T-546 book tests */
/* T-546 owner 517 a: the team's research level - the world server's book */
void t_t546l_level_book()
{
    using namespace swteam;
    Book b; Team t; t.no = 4; t.founder = "alex.1"; t.name = "The Rusty Blades";
    Member m; m.id = "sam.1"; t.members.push_back(m); b.teams.push_back(t); b.nextTeam = 5;
    unsigned before = 9, after = 9; bool stored = false;
    if (LevelDecide(&b, "kim.1", 1, &before, &after, &stored) != kRefNotInTeam) Fail("a player in no team is refused");
    if (LevelDecide(&b, "sam.1", kMaxLevel + 1, &before, &after, &stored) != kRefTooMany) Fail("a level over the cap is refused");
    if (LevelDecide(&b, "alex.1", 0, &before, &after, &stored) != kOk || before != 0 || after != 0 || !stored) Fail("the founder's 0 kept");
    if (LevelDecide(&b, "sam.1", 1, &before, &after, &stored) != kOk || before != 0 || after != 1 || !stored) Fail("the best bench sets the team's level");
    if (LevelDecide(&b, "sam.1", 1, &before, &after, &stored) != kOk || after != 1 || stored) Fail("the same report changes nothing");
    if (LevelDecide(&b, "alex.1", 2, &before, &after, &stored) != kOk || before != 1 || after != 2) Fail("a higher bench raises it");
    if (LevelDecide(&b, "alex.1", 0, &before, &after, &stored) != kOk || before != 2 || after != 1) Fail("a lower report lowers the team's level to the best still standing");
    if (b.teams[0].levels.size() != 2) Fail("one report per player");
    if (!T546FileRoundTrip(b)) Fail("L lines read back");
    if (BookFile(b).find("L 4 ") == std::string::npos) Fail("L lines written");
    /* sam leaves: its report counts no more, is not written, and is dropped at the next report */
    b.teams[0].members.clear();
    if (TeamLevelOf(b.teams[0]) != 0) Fail("a departed player's report counts no more");
    if (BookFile(b).find(Hex(std::string("sam.1"))) != std::string::npos) Fail("a departed player's report is not written");
    if (LevelDecide(&b, "alex.1", 0, &before, &after, &stored) != kOk || !stored || b.teams[0].levels.size() != 1) Fail("a departed player's report dropped at the next report");
    /* the file: an L line for a player not in the team, a duplicate, a level over the cap, no such team */
    Book r; int why = 0;
    if (!BookParseLine("T 4 " + Hex(std::string("alex.1")) + " " + Hex(std::string("X")), &r)) Fail("T line");
    if (BookParseLine("L 4 " + Hex(std::string("kim.1")) + " 1", &r)) Fail("an L line for a player not in the team");
    if (!BookParseLine("L 4 " + Hex(std::string("alex.1")) + " 3", &r)) Fail("an L line");
    if (BookParseLine("L 4 " + Hex(std::string("alex.1")) + " 3", &r, &why) || why != kLineDuplicate) Fail("a duplicate L line");
    if (BookParseLine("L 4 " + Hex(std::string("alex.1")) + " 101", &r)) Fail("an L line over the cap");
    if (BookParseLine("L 5 " + Hex(std::string("alex.1")) + " 1", &r)) Fail("an L line for no team");
    if (r.teams.empty() || TeamLevelOf(r.teams[0]) != 3) Fail("the level read");
    /* a bad L line: fields missing, an id that is not hex, an empty id, a team number that is not a number */
    if (BookParseLine("L 4", &r) || BookParseLine("L 4 " + Hex(std::string("alex.1")), &r) || BookParseLine("L 4 abc 1", &r) || BookParseLine("L 4 zz 1", &r)
        || BookParseLine("L 4 - 1", &r) || BookParseLine("L x " + Hex(std::string("alex.1")) + " 1", &r))
        Fail("a bad L line is read");
    if (r.teams.empty() || r.teams[0].levels.size() != 1 || TeamLevelOf(r.teams[0]) != 3) Fail("a bad L line changed the level read");
    /* a team that failed to load: its number's L lines are refused, even for a player in the team that holds that number */
    Book lf;
    if (!BookParseLine("T 7 " + Hex(std::string("alex.1")) + " " + Hex(std::string("X")), &lf)) Fail("T line (7)");
    if (BookParseLine("T 7 " + Hex(std::string("kim.1")) + " " + Hex(std::string("Y")), &lf, &why) || why != kLineDuplicate || lf.loadRefused.size() != 1) Fail("a refused T line is not noted");
    if (BookParseLine("L 7 " + Hex(std::string("alex.1")) + " 4", &lf) || BookParseLine("L 7 " + Hex(std::string("kim.1")) + " 4", &lf)) Fail("an L line of a team that failed to load is read");
    if (lf.teams.size() != 1 || !lf.teams[0].levels.empty() || TeamLevelOf(lf.teams[0]) != 0) Fail("a team that failed to load gave a level");
    /* a player reported twice in one team (a hand-edited file): the next report keeps one, the team's level is the new one */
    Book tw; Team t2; t2.no = 9; t2.founder = "alex.1"; t2.name = "X"; tw.teams.push_back(t2); tw.nextTeam = 10;
    tw.teams[0].levels.push_back(std::make_pair(std::string("alex.1"), 3u)); tw.teams[0].levels.push_back(std::make_pair(std::string("alex.1"), 1u));
    if (TeamLevelOf(tw.teams[0]) != 3) Fail("a player reported twice: the best report counts");
    if (LevelDecide(&tw, "alex.1", 2, &before, &after, &stored) != kOk || !stored || before != 3 || after != 2 || tw.teams[0].levels.size() != 1) Fail("a player reported twice: one report kept");
    if (BookFile(tw).find("L 9 " + Hex(std::string("alex.1")) + " 2\n") == std::string::npos) Fail("a player reported twice: the one report written");
    /* the wire */
    std::vector<char> w; Up u; Down d;
    EncodeLevelUp(&w, 2);
    if (!DecodeUp(&w[0], w.size(), &u) || u.kind != kUpLevel || u.level != 2) Fail("LEVEL up round trip");
    w.push_back(0); if (DecodeUp(&w[0], w.size(), &u)) Fail("LEVEL up with a trailing byte");
    EncodeLevelDown(&w, 4, 2, 1);
    if (!DecodeDown(&w[0], w.size(), &d) || d.kind != kDnLevel || d.lvTeam != 4 || d.lvLevel != 2 || d.lvBy != 1) Fail("LEVEL down round trip");
    if (DecodeDown(&w[0], w.size() - 1, &d)) Fail("a cut LEVEL down");
    if (DecodeUp(&w[0], w.size(), &u)) Fail("a LEVEL down decoded as up");
}
/* the game's decisions */
void t_t546l_level_game()
{
    using namespace swteam;
    if (LevelSendNow(0, 1, 0, -1, -1, 5, false, 0, 0) != kLvNone) Fail("out of a team: no report");
    if (LevelSendNow(3, -1, 0, -1, -1, 5, false, 0, 0) != kLvNone) Fail("the own level not known (no hook): no report");
    if (LevelSendNow(3, 0, 0, -1, -1, 5, false, 0, 0) != kLvSend) Fail("in a team: the first report, level 0 too");
    if (LevelSendNow(3, 1, 3, 5, 1, 5, true, 999999, 0) != kLvNone) Fail("answered: nothing more");
    if (LevelSendNow(3, 1, 3, 5, 1, 5, false, 1999, 0) != kLvNone || LevelSendNow(3, 1, 3, 5, 1, 5, false, 2000, 0) != kLvAgain) Fail("not answered: again after the growing wait");
    if (LevelSendNow(3, 1, 3, 5, 1, 5, false, 3999, 1) != kLvNone) Fail("the wait grows");
    if (LevelSendNow(3, 2, 3, 5, 1, 5, true, 0, 0) != kLvSend) Fail("a new bench level: reported");
    if (LevelSendNow(3, 1, 3, 4, 1, 5, true, 0, 0) != kLvSend || LevelSendNow(3, 1, 2, 5, 1, 5, true, 0, 0) != kLvSend) Fail("a new link or team: reported");
    if (LevelRaiseTo(3, 3, 1, 0) != 1) Fail("the engine below the team's level: raised");
    if (LevelRaiseTo(3, 3, 1, 1) != -1 || LevelRaiseTo(3, 3, 1, 2) != -1) Fail("never lowered, never raised to the same");
    if (LevelRaiseTo(3, 4, 2, 0) != -1 || LevelRaiseTo(0, 0, 2, 0) != -1) Fail("only the team this player is in now");
    if (LevelRaiseTo(3, 3, -1, 0) != -1 || LevelRaiseTo(3, 3, 1, -1) != -1) Fail("no team level held, or the engine's not readable: nothing");
    if (OwnBenchLevelAfter(0, 1) != 1 || OwnBenchLevelAfter(2, 1) != 2 || OwnBenchLevelAfter(1, 1) != 1) Fail("the own bench level is the highest bench");
}
/* the fold's rules: the box back after a refused ACCEPT, the held lines, the team's own writes quiet, the table's settle wait */
void t_t546h_fold_rules()
{
    using namespace teamscreen;
    if (!InviteBackAfterRefusal(swteam::kEvAnswer, swteam::kRefNoSnapshot, true) || !InviteBackAfterRefusal(swteam::kEvAnswer, swteam::kRefNotSaved, true)) Fail("a refusal that keeps the invitation brings the box back");
    if (InviteBackAfterRefusal(swteam::kEvAnswer, swteam::kRefNoInvite, true) || InviteBackAfterRefusal(swteam::kEvAnswer, swteam::kOk, true)
        || InviteBackAfterRefusal(swteam::kEvAnswer, swteam::kRefNotSaved, false) || InviteBackAfterRefusal(swteam::kEvInvite, swteam::kRefNotSaved, true)) Fail("no box back otherwise");
    HeldLines h;
    HoldLine(&h, "one"); HoldLine(&h, ""); HoldLine(&h, "two");
    if (!TakeHeld(&h, false).empty() || h.q.size() != 2) Fail("not ready: the lines stay held");
    std::vector<std::string> t = TakeHeld(&h, true);
    if (t.size() != 2 || t[0] != "one" || t[1] != "two" || !h.q.empty()) Fail("ready: in order, each once");
    if (!TakeHeld(&h, true).empty()) Fail("a line is shown once");
    HoldLine(&h, "three");
    PutBack(&h, t, 1);
    if (h.q.size() != 2 || h.q[0] != "two" || h.q[1] != "three") Fail("a line not shown yet goes back in front, in order");
    for (int i = 0; i < 40; ++i) HoldLine(&h, "x" + I(i));
    if (h.q.size() != kHeldLines || h.q.back() != "x39" || h.dropped != (unsigned)(42 - kHeldLines)) Fail("at most kHeldLines held, the oldest dropped");
    if (!MateQuiet(true, 0, 5000) || MateQuiet(false, 0, 5000) || !MateQuiet(false, 1000, 1000 + kMateQuietMs - 1) || MateQuiet(false, 1000, 1000 + kMateQuietMs)) Fail("the team's own writes are quiet");
    if (!MateQuiet(false, 0xFFFFFF00u, 0x00000010u)) Fail("the quiet window across the tick count's wrap");
    if (MatesSettling(0, 100) || !MatesSettling(100, 100 + kMatesSettleMs - 1) || MatesSettling(100, 100 + kMatesSettleMs)) Fail("the table's settle wait");
}
/* T-546 (owner 521 / T-634): the research-complete sound - only a tech applied through Research::load (setResearched plays its own),
   carried with its held line, played once when the line is shown */
void t_t546s_research_sound()
{
    using namespace teamscreen;
    if (swteam::ResearchLineSound(swteam::kResViaSetResearched) || !swteam::ResearchLineSound(swteam::kResViaLoad)) Fail("only the Research::load road plays the sound");
    if (std::string(swteam::kResearchSoundEvent) != "Thing_Complete") Fail("the game's own research-complete state of its Notifications sound switch");
    HeldLines h; std::vector<unsigned char> s;
    HoldLine(&h, "plain"); HoldLine(&h, "", true); HoldLine(&h, "loud", true);
    if (h.q.size() != 2 || h.snd.size() != 2) Fail("an empty line is not held, its mark neither");
    if (!TakeHeld(&h, false, &s).empty() || !s.empty() || h.q.size() != 2) Fail("not ready: lines and marks stay held");
    std::vector<std::string> t = TakeHeld(&h, true, &s);
    if (t.size() != 2 || s.size() != 2 || s[0] != 0 || s[1] != 1 || !h.q.empty() || !h.snd.empty()) Fail("ready: each line with its own mark");
    if (HeldLineSounds(s, 0, 1) || !HeldLineSounds(s, 1, 1)) Fail("a shown line plays only when marked");
    if (HeldLineSounds(s, 1, 0) || HeldLineSounds(s, 1, -1) || HeldLineSounds(s, 5, 1)) Fail("never for a line not shown");
    HoldLine(&h, "after");
    PutBack(&h, t, 1, &s);
    if (h.q.size() != 2 || h.q[0] != "loud" || h.snd.size() != 2 || h.snd[0] != 1 || h.snd[1] != 0) Fail("a line put back keeps its mark, in front");
    t = TakeHeld(&h, true, &s);
    if (s.size() != 2 || !HeldLineSounds(s, 0, 1) || HeldLineSounds(s, 1, 1)) Fail("the put-back line plays once when shown at last");
    if (!TakeHeld(&h, true, &s).empty() || !s.empty()) Fail("a line and its sound once");
    HoldLine(&h, "a"); PutBack(&h, t, 0);
    if (h.snd.size() != h.q.size() || h.snd[0] != 0) Fail("put back without marks: none marked, still one per line");
    HeldLines f; HoldLine(&f, "first", true);
    for (int i = 0; i < 40; ++i) HoldLine(&f, "x" + I(i), i == 39);
    if (f.q.size() != kHeldLines || f.snd.size() != kHeldLines || f.snd[0] != 0 || f.snd.back() != 1) Fail("the oldest dropped with its mark");
    HeldLines g; g.q.push_back("raw");   /* a line pushed without a mark reads as unmarked */
    t = TakeHeld(&g, true, &s);
    if (t.size() != 1 || s.size() != 1 || s[0] != 0) Fail("a line without a mark is silent");
}
void t_t546h_rows_and_lever()
{
    using namespace playerstab;
    RowIn in; in.slot = 2; in.rosterName = "Sam"; in.faction = "The Rusty Blades"; in.you = nametag::kFriendly; in.them = nametag::kFriendly; in.teammate = true; in.online = true;
    RowView r = RowFor(in);
    if (r.cell[2] != "Member" || r.cell[3] != "Member" || r.cell[1] != "The Rusty Blades" || r.cell[4] != "Online") Fail("a member's row: Member / Member");
    if (r.cell[0] != "#5a9be6Sam" || TeamColourCode() != "#5a9be6") Fail("a teammate's name in team blue: " + r.cell[0]);
    in.mateFounder = true; r = RowFor(in);
    if (r.cell[2] != "Founder" || r.cell[3] != "Founder") Fail("the founder's row: Founder / Founder");
    in.teammate = false; in.mateFounder = true; in.inTeam = true; in.you = nametag::kHostile; in.them = nametag::kNeutral; r = RowFor(in);
    if (r.cell[2] != "Hostile" || r.cell[3] != "Neutral" || !r.inTeam || r.cell[0] != ColouredName("Sam", nametag::kHostile)) Fail("anyone else: levels, the tag's colour, in a faction");
    in.teammate = true; r = RowFor(in);
    if (r.inTeam) Fail("a teammate is not 'in another faction'");
    in.rosterName = ""; in.rememberedName = ""; in.placeholder = "Player 2"; r = RowFor(in);
    if (!r.name.empty() || r.cell[0].find("Player 2") == std::string::npos) Fail("the faction screens' name is never the placeholder (the cell keeps it)");
    in.rememberedName = "Sam"; r = RowFor(in);
    if (r.name != "Sam") Fail("the remembered name");
    Lever l = ParseLever("action invite");
    if (l.kind != kLeverAction || l.button != teamscreen::kActInvite) Fail("action invite");
    if (ParseLever("action remove").button != teamscreen::kActRemove || ParseLever("action disband").button != teamscreen::kActDisband || ParseLever("action leave").button != teamscreen::kActLeave) Fail("action words");
    if (ParseLever("action").kind != kLeverBad || ParseLever("action promote").kind != kLeverBad || ParseLever("action leave now").kind != kLeverBad) Fail("bad action levers");
    if (ParseLever("shown").kind != kLeverShown || ParseLever("shown 1").kind != kLeverBad) Fail("shown");
    for (int a = 1; a <= teamscreen::kActLast; ++a) if (ParseLever(std::string("action ") + teamscreen::ActionWord(a)).button != a) Fail("the lever's words are teamscreens.h's");
}
void t_nametag_caption()
{
    using namespace nametag;
    Caption c = CaptionFor("Melaus", "The Dust Wolves");
    if (c.line1 != "Melaus" || c.line2 != "The Dust Wolves")        Fail("player name over the faction name");
    /* no roster name: the faction name on line 1, no second line - never a blank line or the same words twice */
    c = CaptionFor("", "The Dust Wolves");
    if (c.line1 != "The Dust Wolves" || !c.line2.empty())           Fail("fallback: the faction name alone");
    c = CaptionFor("  \t ", "Player 2");
    if (c.line1 != "Player 2" || !c.line2.empty())                  Fail("a name of only spaces is no name");
    c = CaptionFor(" Raider99 ", " Hookfang Clan ");
    if (c.line1 != "Raider99" || c.line2 != "Hookfang Clan")        Fail("the ends are trimmed");
    c = CaptionFor("PlayerC", "");
    if (c.line1 != "PlayerC" || !c.line2.empty())                   Fail("no faction name: the name alone");
    c = CaptionFor("", "");
    if (!c.line1.empty() || !c.line2.empty())                       Fail("nothing to show: both lines empty");
    /* the name is the player's, whatever the faction is called - even the same words */
    c = CaptionFor("Nameless", "Nameless");
    if (c.line1 != "Nameless" || c.line2 != "Nameless")             Fail("a player named like their faction keeps both lines");
}

/* THE PER-COPY WORD TABLES (src/common/uidslots.h; medical.cpp SlotClaim / SlotRelease / SlotOf).  A model of the plugin's
   three tables on plain arrays: one key and one value per slot, a claim writes the key then the value, a release zeroes the
   value, writes the tombstone and trims backwards.  Uids 5 + 64 * i all share one home slot in a 64-slot table (the hash
   multiplier is odd, so the low six bits decide the home). */
struct SlKeys
{
    const unsigned int* k;
    unsigned int operator()(int i) const { return k[i]; }
};
struct SlTable
{
    unsigned int key[64];
    int val[64];
    int mask, probe;
    long long full, forgotten;
    void Init(int probeLen)
    {
        for (int i = 0; i < 64; ++i) { key[i] = 0; val[i] = 0; }
        mask = 63; probe = probeLen; full = 0; forgotten = 0;
    }
    int Find(unsigned int uid) const { SlKeys k; k.k = key; return uidslot::Find(k, mask, probe, uid); }
    int Note(unsigned int uid, int v)
    {
        SlKeys k; k.k = key;
        int ex = -2;
        const int at = uidslot::InsertAt(k, mask, probe, uid, &ex);
        if (at < 0) { ++full; return -1; }
        key[at] = uid; val[at] = v;
        return at;
    }
    int Forget(unsigned int uid)
    {
        const int s = Find(uid);
        if (s < 0) return 0;
        val[s] = 0; key[s] = uidslot::kTomb;
        SlKeys k; k.k = key;
        int i = s;
        for (int n = 0; n <= mask && uidslot::Trimmable(k, mask, i); ++n) { key[i] = uidslot::kEmpty; i = (i - 1) & mask; }
        ++forgotten;
        return 1;
    }
    int Value(unsigned int uid) const { const int s = Find(uid); return s < 0 ? 0 : val[s]; }
    int Used() const { int n = 0; for (int i = 0; i <= mask; ++i) if (key[i] != uidslot::kEmpty) ++n; return n; }
    int Holds(unsigned int uid) const { int n = 0; for (int i = 0; i <= mask; ++i) if (key[i] == uid) ++n; return n; }
};
unsigned int SlSame(int i) { return 5u + 64u * (unsigned int)i; }

/* FILL: a full table refuses (counted), and forgetting one copy frees a slot for the next. */
void t_uidslots_fill_refuses_then_forget_frees_a_slot()
{
    SlTable T; T.Init(64);
    for (unsigned int u = 1; u <= 64; ++u)
        if (T.Note(u, (int)u * 10) < 0) { Fail("insert " + I((int)u) + " of 64 refused"); return; }
    if (T.Note(65, 650) >= 0 || T.full != 1)     Fail("a 65th uid in a 64-slot table is refused and counted");
    if (T.Note(7, 71) < 0 || T.Value(7) != 71)   Fail("a uid already held is updated in place even when the table is full");
    if (T.Forget(10) != 1)                        Fail("forgetting a held uid releases its slot");
    if (T.Find(10) >= 0 || T.Value(10) != 0)      Fail("a forgotten uid is not found");
    if (T.Note(65, 650) < 0 || T.Value(65) != 650) Fail("the freed slot takes the next copy");
    for (unsigned int u = 1; u <= 65; ++u)
    {
        if (u == 10) continue;
        const int want = u == 7 ? 71 : (int)u * 10;
        if (T.Value(u) != want) { Fail("uid " + I((int)u) + " lost its value after the forget/reuse"); return; }
    }
    if (T.Forget(10) != 0)                        Fail("forgetting a uid the table does not hold is a no-op");
}

/* REMOVE: a release in the middle of a colliding run keeps every later entry findable (a tombstone, not a hole); a reinsert
   reuses the first tombstone and a uid is never held twice; releasing the run's end trims tombstones back to empty. */
void t_uidslots_remove_keeps_colliding_run_findable()
{
    SlTable T; T.Init(16);
    const int h = uidslot::Home(SlSame(0), 63);
    for (int i = 0; i < 5; ++i)
        if (T.Note(SlSame(i), 100 + i) != ((h + i) & 63)) { Fail("colliding uid " + I(i) + " not placed in its run"); return; }
    T.Forget(SlSame(1));
    if (T.key[(h + 1) & 63] != uidslot::kTomb)   Fail("a release inside a run leaves a tombstone");
    for (int i = 2; i < 5; ++i)
        if (T.Value(SlSame(i)) != 100 + i) { Fail("entry " + I(i) + " behind the released slot is lost"); return; }
    if (T.Note(SlSame(3), 333) != ((h + 3) & 63) || T.Holds(SlSame(3)) != 1) Fail("an update of a held uid stays in its slot, held once");
    if (T.Note(SlSame(7), 107) != ((h + 1) & 63)) Fail("a new colliding uid reuses the first tombstone of the run");
    T.Forget(SlSame(4));                          /* the run's last entry: its slot ends the run, trimmed to empty */
    if (T.key[(h + 4) & 63] != uidslot::kEmpty)  Fail("the run's last slot is trimmed back to empty");
    T.Forget(SlSame(3)); T.Forget(SlSame(2));     /* now slots h+2, h+3 end the run: trimmed backwards */
    if (T.key[(h + 2) & 63] != uidslot::kEmpty || T.key[(h + 3) & 63] != uidslot::kEmpty) Fail("slots that end a run are trimmed back to empty");
    if (T.Value(SlSame(0)) != 100 || T.Value(SlSame(7)) != 107) Fail("the run's first entries survive the trims");
    T.Forget(SlSame(0)); T.Forget(SlSame(7));
    if (T.Used() != 0)                            Fail("an empty table holds no tombstones after its run is released: " + I(T.Used()));
}

/* REUSE: a long session's churn - 20,000 random notes and forgets over 300 uids with at most 24 copies present - never
   refuses on a 64-slot table with a 16-slot probe, every present copy reads its own value, every forgotten one reads
   nothing; without the forget (the old table) the same churn fills the table.  After such a churn every free slot may be a
   tombstone (uidslots.h: a miss then walks the whole probe); a new copy still takes one. */
void t_uidslots_churn_never_fills_and_never_reads_a_forgotten_value()
{
    SlTable T; T.Init(16);
    SlTable Old; Old.Init(16);
    const int kIds = 300, kLiveMax = 24;
    int live[kIds];
    for (int i = 0; i < kIds; ++i) live[i] = 0;
    unsigned int rng = 2468u;
    int nLive = 0;
    for (int step = 0; step < 20000; ++step)
    {
        rng = rng * 1103515245u + 12345u;
        const int n = (int)((rng >> 8) % kIds);
        const unsigned int uid = 1000u + (unsigned int)n * 7u;
        if (!live[n])
        {
            if (nLive >= kLiveMax) continue;
            if (T.Find(uid) >= 0) { Fail("a forgotten uid was still held at step " + I(step)); return; }
            if (T.Note(uid, step + 1) < 0) { Fail("insert refused at step " + I(step) + " with " + I(nLive) + " present"); return; }
            Old.Note(uid, step + 1);
            live[n] = step + 1; ++nLive;
        }
        else
        {
            if (T.Value(uid) != live[n]) { Fail("a present copy read another value at step " + I(step)); return; }
            T.Forget(uid);
            live[n] = 0; --nLive;
        }
    }
    for (int i = 0; i < kIds; ++i)
    {
        const unsigned int uid = 1000u + (unsigned int)i * 7u;
        if (live[i] && T.Value(uid) != live[i]) { Fail("present copy " + I(i) + " lost at the end"); return; }
        if (!live[i] && T.Find(uid) >= 0)      { Fail("forgotten copy " + I(i) + " still found at the end"); return; }
    }
    if (T.full != 0)  Fail("the pruned table refused " + I((int)T.full) + " inserts");
    if (Old.full == 0) Fail("the never-pruned model should have filled under the same churn");
    for (int i = 0; i < kIds; ++i) if (live[i]) T.Forget(1000u + (unsigned int)i * 7u);
    for (int i = 0; i < 64; ++i)
        if (T.key[i] != uidslot::kEmpty && T.key[i] != uidslot::kTomb) { Fail("a slot still holds a uid after every copy was forgotten"); return; }
    if (T.Note(99999u, 5) < 0 || T.Value(99999u) != 5) Fail("a new copy takes a released slot after the churn");
}

/* OWNERSHIP CHANGE: a copy's word is forgotten when the character's ownership moves here; when it moves back and this game
   holds a copy again, nothing is read until the owner's next word - never the value from the earlier stay. Also the probe
   limit and the two uids that are never stored. */
void t_uidslots_ownership_change_never_reuses_a_stale_value()
{
    SlTable T; T.Init(4);
    const unsigned int x = SlSame(0);
    T.Note(x, 42);                                /* this game holds a copy: the owner's word is 42 */
    T.Forget(x);                                  /* ownership moved to this game */
    if (T.Find(x) >= 0 || T.Value(x) != 0)        Fail("after the move here the old owner's word is gone");
    /* ownership moves back to the other game: the copy is held again, no word yet */
    if (T.Value(x) != 0)                          Fail("the copy reads nothing (the engine's own value) until the owner speaks");
    T.Note(x, 7);
    if (T.Value(x) != 7)                          Fail("the new owner's word is read");
    /* probe limit: five uids of one home in a 4-slot probe - the fifth is refused and counted, and a lookup stops at 4 */
    for (int i = 1; i < 4; ++i) T.Note(SlSame(i), i);
    if (T.Note(SlSame(4), 4) >= 0 || T.full != 1) Fail("a uid with no slot within the probe is refused and counted");
    if (T.Find(SlSame(4)) >= 0)                   Fail("a refused uid is not found");
    if (uidslot::Storable(0) || uidslot::Storable(uidslot::kTomb) || !uidslot::Storable(1)) Fail("Storable: 0 and the tombstone key are never stored");
    if (T.Note(0, 1) >= 0 || T.Note(uidslot::kTomb, 1) >= 0) Fail("uid 0 and the tombstone key are refused");
    if (T.Find(uidslot::kTomb) >= 0)             Fail("a lookup of the tombstone key finds nothing");
}

/* P17 / P43: the TEST-ONLY levers' argument shapes (joblever.h) */
/* ================= T-513: the mod's title art (src/common/titleart.h) =================
 * The cover crop at 16:9, 16:10, 4:3 and 21:9 for the shipped 1672x941 art (shape kept, no empty edge, equal cut across, top
 * kept), the art's rows on the box, and the note's place: in the band, beside the column, on screen. */
void t_titleart_cover_crop()
{
    swtitle::Rect r = swtitle::CoverCrop(1672, 941, 1280, 720);
    if (r.x != 0 || r.y != 0 || r.w != 1672 || r.h != 941) Fail("16:9 shows the whole art");
    r = swtitle::CoverCrop(1672, 941, 1280, 800);
    if (r.y != 0 || r.h != 941 || r.w != 1506 || r.x != 83) Fail("16:10: the whole height, the middle 1506 of the width");
    r = swtitle::CoverCrop(1672, 941, 1024, 768);
    if (r.y != 0 || r.h != 941 || r.w != 1255 || r.x != 208) Fail("4:3: the whole height, the middle 1255 of the width");
    r = swtitle::CoverCrop(1672, 941, 2560, 1080);
    if (r.x != 0 || r.w != 1672 || r.y != 0 || r.h != 705) Fail("21:9: the whole width, the top 705 rows (the title kept)");
    r = swtitle::CoverCrop(0, 941, 1280, 720);
    if (r.w != 0 || r.h != 0) Fail("no texture: no crop");
    r = swtitle::CoverCrop(1672, 941, 1280, 0);
    if (r.w != 0 || r.h != 0) Fail("no box: no crop");
    const int sizes[][2] = { { 1280, 720 }, { 1920, 1080 }, { 1280, 800 }, { 1680, 1050 }, { 1024, 768 }, { 1600, 1200 }, { 2560, 1080 }, { 960, 993 }, { 3840, 2160 } };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        const int bw = sizes[i][0], bh = sizes[i][1];
        r = swtitle::CoverCrop(1672, 941, bw, bh);
        if (r.x < 0 || r.y != 0 || r.x + r.w > 1672 || r.h > 941 || r.w <= 0 || r.h <= 0) Fail("the crop stays inside the art, top kept");
        if (r.w != 1672 && r.h != 941) Fail("the crop keeps one whole side of the art (no empty edge)");
        const int left = r.x, right = 1672 - r.x - r.w;
        if (left - right < -1 || left - right > 1) Fail("the same width cut from each side (to a pixel)");
        const long long a = (long long)r.w * bh, b = (long long)r.h * bw;
        if ((a > b ? a - b : b - a) > (long long)(bw > bh ? bw : bh)) Fail("the crop has the box's shape (no stretching)");
    }
}
/* The column's right edge and REPORT A BUG's rectangle on a pw x ph title screen, as the layout and bugreport.cpp make them. */
int T513ColRight(int pw) { return (int)(pw * (0.260417 + 0.15625)); }
int T513Margin(int ph) { int m = ph * 185 / 10000; return m < 8 ? 8 : (m > 40 ? 40 : m); }
swtitle::Rect T513Btn(int pw, int ph)
{
    const int m = T513Margin(ph);
    int h = ph * 335 / 10000; h = h < 26 ? 26 : (h > 60 ? 60 : h);
    int w = pw * 477 / 10000; w = w < 130 ? 130 : (w > pw / 3 ? pw / 3 : w);
    if (w < 150) w = 150;   /* the caption's width + 32, as measured at 1280x720 */
    swtitle::Rect r = { m, ph - h - m, w, h };
    return r;
}
void t_titleart_rows_and_note()
{
    swtitle::Rect c = swtitle::CoverCrop(1672, 941, 1280, 720);
    if (swtitle::ArtRowOnBox(0.0, 941, c, 720) != 0 || swtitle::ArtRowOnBox(1.0, 941, c, 720) != 720) Fail("rows at 16:9: top and bottom");
    if (swtitle::ArtRowOnBox(swtitle::kTitleBottom, 941, c, 720) != 169 || swtitle::ArtRowOnBox(swtitle::kFiguresTop, 941, c, 720) != 382)
        Fail("rows at 16:9: the band 169-382");
    c = swtitle::CoverCrop(1672, 941, 2560, 1080);
    if (swtitle::ArtRowOnBox(swtitle::kTitleBottom, 941, c, 1080) != 339) Fail("rows at 21:9: the art is scaled by width");
    const swtitle::Rect none = { 0, 0, 0, 0 };
    /* 1280x720, the mod's art, the note at size 52/56 (about 480x150): two margins right of the column, a margin under the title */
    swtitle::NoteFit f = swtitle::PlaceTitleNote(1, 1280, 720, 480, 150, 13, 533, none, 169, 382, 704);
    if (!f.ok || f.x != 559 || f.y != 182) Fail("the note at 1280x720 on the mod's art");
    /* the game's art: the right side, 0.58 down */
    f = swtitle::PlaceTitleNote(0, 1280, 720, 480, 150, 13, 533, none, 0, 0, 704);
    if (!f.ok || f.x != 1280 - 480 - 26 || f.y != 417) Fail("the note at 1280x720 on the game's art");
    /* too wide for the room right of the column: no place (never over the column) */
    f = swtitle::PlaceTitleNote(1, 1280, 720, 900, 150, 13, 533, none, 169, 382, 704);
    if (f.ok) Fail("a note wider than the room beside the column has no place");
    /* too tall for the band: no place (never over the figures or the title) */
    f = swtitle::PlaceTitleNote(1, 1280, 720, 480, 300, 13, 533, none, 169, 382, 704);
    if (f.ok) Fail("a note taller than the band has no place");
    /* over the button: no place */
    const swtitle::Rect wide = { 700, 450, 300, 60 };
    f = swtitle::PlaceTitleNote(0, 1280, 720, 480, 150, 13, 533, wide, 0, 0, 704);
    if (f.ok) Fail("a place over REPORT A BUG is refused");
    /* every window shape asked for (16:9, 16:10, 4:3, the narrow 960x993), both arts: the full-size note (480x150) fits, and
       any note that is placed - full size or the game's smaller fonts (about 330x100) - is on screen, clear of the column
       (x 26-41.7%) and of REPORT A BUG, and on the mod's art between the painted title and the figures */
    const int sizes[][2] = { { 1280, 720 }, { 1920, 1080 }, { 1280, 800 }, { 1680, 1050 }, { 1024, 768 }, { 1600, 1200 }, { 960, 993 }, { 2560, 1080 } };
    const int notes[][2] = { { 480, 150 }, { 556, 165 }, { 330, 100 } };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        const int pw = sizes[i][0], ph = sizes[i][1], m = T513Margin(ph), col = T513ColRight(pw), colLeft = (int)(pw * 0.260417);
        const swtitle::Rect btn = T513Btn(pw, ph);
        c = swtitle::CoverCrop(1672, 941, pw, ph);
        const int top = swtitle::ArtRowOnBox(swtitle::kTitleBottom, 941, c, ph), bottom = swtitle::ArtRowOnBox(swtitle::kFiguresTop, 941, c, ph);
        const int verTop = ph * 9778 / 10000;
        for (int art = 0; art < 2; ++art)
            for (size_t k = 0; k < sizeof(notes) / sizeof(notes[0]); ++k)
            {
                const int w = notes[k][0], h = notes[k][1];
                f = swtitle::PlaceTitleNote(art, pw, ph, w, h, m, col, btn, top, bottom, verTop);
                if (k == 0 && !f.ok) Fail("the full-size note has a place at every window shape asked for");
                if (!f.ok) continue;
                if (f.x < m || f.y < m || f.x + w > pw - m || f.y + h > ph - m) Fail("the note on screen");
                if (f.x < col + 2 * m && f.x + w > colLeft) Fail("the note clear of the menu column");
                if (f.x < btn.x + btn.w && btn.x < f.x + w && f.y < btn.y + btn.h && btn.y < f.y + h) Fail("the note clear of REPORT A BUG");
                if (art == 1 && (f.y < top || f.y + h > bottom)) Fail("the note between the painted title and the figures");
                if (art == 0 && f.y + h > verTop) Fail("the note above the version text");
            }
    }
}
/* T-530 (owner 464 a): the menu column on the mod's art - the first shown button under the painted title, the last one inside
   97% of the height, at every window shape asked for, with CONTINUE (8 buttons) and without (7). The game's rows come from
   the layout (Kenshi_MainMenu.layout: CONTINUE 0.138889, NEW GAME 0.25, height 0.0638889 of the screen). */
void t_titleart_menu_column()
{
    /* 1920x1080, CONTINUE shown: the title ends on row 254; top 254 + 22, the pitch closed up from the game's 120 to 100 */
    swtitle::MenuColumn m = swtitle::MenuColumnPlan(1080, 254, 8, 120, 69);
    if (!m.ok || m.top != 276 || m.pitch != 100 || m.height != 69) Fail("1920x1080 with CONTINUE: top 276, pitch 100, height 69");
    m = swtitle::MenuColumnPlan(1080, 254, 7, 120, 69);
    if (!m.ok || m.top != 276 || m.pitch != 117 || m.height != 69) Fail("1920x1080 without CONTINUE: top 276, pitch 117, height 69");
    /* 5120x1440 (32:9): the title ends on row 678; the buttons are shortened to keep a 1.2% gap */
    m = swtitle::MenuColumnPlan(1440, 678, 8, 160, 92);
    if (!m.ok || m.top != 707 || m.pitch != 85 || m.height != 68) Fail("5120x1440 with CONTINUE: top 707, pitch 85, height 68");
    /* one button: the game's pitch and height */
    m = swtitle::MenuColumnPlan(1080, 254, 1, 120, 69);
    if (!m.ok || m.top != 276 || m.pitch != 120 || m.height != 69) Fail("one button keeps the game's rows");
    /* the room never grows the pitch past the game's own */
    m = swtitle::MenuColumnPlan(1080, 10, 3, 120, 69);
    if (!m.ok || m.pitch != 120) Fail("a short column keeps the game's pitch");
    /* no plan: a screen far too wide (the title ends below the screen), buttons under half the game's height, bad numbers */
    m = swtitle::MenuColumnPlan(1000, 1587, 8, 111, 63);
    if (m.ok) Fail("the title below the screen: no plan");
    m = swtitle::MenuColumnPlan(1000, 600, 8, 111, 63);
    if (m.ok) Fail("buttons under half the game's height: no plan");
    if (swtitle::MenuColumnPlan(0, 254, 8, 120, 69).ok || swtitle::MenuColumnPlan(1080, 254, 0, 120, 69).ok
        || swtitle::MenuColumnPlan(1080, 254, 8, 0, 69).ok || swtitle::MenuColumnPlan(1080, 254, 8, 120, 0).ok
        || swtitle::MenuColumnPlan(1080, -1, 8, 120, 69).ok) Fail("a number not positive: no plan");
    /* every shape asked for: 16:9, 16:10, 21:9, 43:18, 32:9, 1280x720, the tall 960x1080, 16:9 900, 16:10 900, 1920x822 */
    const int sizes[][2] = { { 1920, 1080 }, { 1920, 1200 }, { 2560, 1080 }, { 3440, 1440 }, { 5120, 1440 }, { 1280, 720 }, { 960, 1080 },
                             { 1600, 900 }, { 1440, 900 }, { 1920, 822 } };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        const int pw = sizes[i][0], ph = sizes[i][1];
        const swtitle::Rect c = swtitle::CoverCrop(1672, 941, pw, ph);
        const int titleBottom = swtitle::ArtRowOnBox(swtitle::kTitleBottom, 941, c, ph);
        const int gamePitch = (int)(ph * 0.25) - (int)(ph * 0.138889), gameHeight = (int)(ph * 0.0638889);
        for (int shown = 7; shown <= 8; ++shown)
        {
            m = swtitle::MenuColumnPlan(ph, titleBottom, shown, gamePitch, gameHeight);
            if (!m.ok) { Fail("a plan at every shape asked for"); continue; }
            if (m.top <= titleBottom || m.top < titleBottom + (int)(ph * 0.02)) Fail("the first button under the painted title, 2% clear");
            if (m.top + (shown - 1) * m.pitch + m.height > (int)(ph * 0.97 + 0.5)) Fail("EXIT's bottom inside 97% of the height");
            if (m.pitch > gamePitch || m.height > gameHeight || m.height * 2 < gameHeight) Fail("never wider apart or taller than the game's rows");
            if (m.pitch - m.height < (int)(ph * 0.012 + 0.5)) Fail("a 1.2% gap between buttons");
            if (m.height < gameHeight && m.pitch - m.height != (int)(ph * 0.012 + 0.5)) Fail("shortened only as far as the gap needs");
        }
    }
}
/* T-609 (T1367): cursortest's box argument `placed` / `copyof:<sid>` (joblever::CursorBoxArg) and the key's record test */
void t_cursor_boxarg()
{
    std::string sid;
    if (joblever::CursorBoxArg("placed", &sid) != joblever::kBoxArgPlaced || !sid.empty()) Fail("cursor boxarg: placed");
    if (joblever::CursorBoxArg("copyof:1234-gamedata.base", &sid) != joblever::kBoxArgCopyOf || sid != "1234-gamedata.base") Fail("cursor boxarg: copyof");
    if (joblever::CursorBoxArg("copyof:", &sid) != joblever::kBoxArgBad || !sid.empty()) Fail("cursor boxarg: copyof with no sid must be refused");
    if (joblever::CursorBoxArg("copyof:Loom@3,4@1,2,3", &sid) != joblever::kBoxArgBad) Fail("cursor boxarg: a key after copyof: must be refused");
    if (joblever::CursorBoxArg("copyof:a b", &sid) != joblever::kBoxArgBad) Fail("cursor boxarg: a space in the sid must be refused");
    if (joblever::CursorBoxArg("copyof:" + std::string((size_t)poskey::kSidRoom, 'a'), &sid) != joblever::kBoxArgBad) Fail("cursor boxarg: a sid too long for a key");
    const char* keys[] = { "Placed", "placedx", "me", "peer", "4194305", "name:Bob", "Loom@31,32@1000,2000,30", "copyof" };
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k)
        if (joblever::CursorBoxArg(keys[k], &sid) != joblever::kBoxArgKey || !sid.empty()) Fail(std::string("cursor boxarg: passed on unchanged: ") + keys[k]);
    if (joblever::CursorKeyIsRecord("1234-gamedata.base@31,32@1000,-2000,30", "1234-gamedata.base") != 1) Fail("cursor key record: the same record");
    if (joblever::CursorKeyIsRecord("1234-gamedata.base@31,32@1000,-2000,30", "1235-gamedata.base") != 0) Fail("cursor key record: another record");
    if (joblever::CursorKeyIsRecord("1234-gamedata.base@31,32@1000,-2000,30", "1234-gamedata") != 0) Fail("cursor key record: a prefix is not the record");
    if (joblever::CursorKeyIsRecord("1234-gamedata.base", "1234-gamedata.base") != 0) Fail("cursor key record: not a key");
    if (joblever::CursorKeyIsRecord("1234-gamedata.base@31,32@1000,-2000,30", "") != 0) Fail("cursor key record: an empty sid");
    char h[16];
    int at = poskey::AppendStr(h, (int)sizeof h, 0, "#");
    at = poskey::AppendHex8(h, (int)sizeof h, at, poskey::SidHash32("1234-gamedata.base"));
    if (at < 0 || joblever::CursorKeyIsRecord(std::string(h) + "@31,32@1000,-2000,30", "1234-gamedata.base") != 1) Fail("cursor key record: the hashed record field");
    if (joblever::CursorKeyIsRecord(std::string(h) + "@31,32@1000,-2000,30", "1235-gamedata.base") != 0) Fail("cursor key record: the hashed field of another record");
}

/* ================= T-514: THE PAUSE MENU'S ROWS (src/common/pausemenu.h) =================
 * Kenshi_MainMenuPopupPanel.layout at 1920x1080: the panel 0.318519 of 1080 = 344 tall, the rows' position_real tops 0.0436046,
 * 0.188953, 0.334302, 0.479651, 0.625 and 0.828488 of 344 = 15, 65, 115, 165, 215, 285, each 45 tall: pitch 50, gap 20. */
pausemenu::KenshiRows PmKenshi()
{
    pausemenu::KenshiRows k;
    k.newGame = 15; k.saveGame = 65; k.loadGame = 115; k.options = 165; k.exitGame = 215; k.resume = 285; k.panelHeight = 344;
    return k;
}
bool PmSame(const pausemenu::Placement& a, const pausemenu::Placement& b)
{
    return a.hosting == b.hosting && a.newGame == b.newGame && a.saveGame == b.saveGame && a.loadGame == b.loadGame && a.options == b.options && a.exitGame == b.exitGame
        && a.multiplayer == b.multiplayer && a.bug == b.bug && a.resume == b.resume && a.panelTop == b.panelTop && a.panelHeight == b.panelHeight;
}
void t_pausemenu_kenshi_layout_outside_multiplayer()
{
    const pausemenu::KenshiRows k = PmKenshi();
    pausemenu::Placement p;
    if (!pausemenu::Place(k, false, false, false, 368, 344, &p)) { Fail("Kenshi's own layout refused"); return; }
    if (p.pitch != 50 || p.gap != 20) Fail("pitch / gap " + I(p.pitch) + " / " + I(p.gap));
    if (p.newGame != 15 || p.saveGame != 65 || p.loadGame != 115 || p.options != 165 || p.exitGame != 215 || p.resume != 285)
        Fail("rows moved outside a multiplayer world");
    if (p.panelTop != 368 || p.panelHeight != 344) Fail("panel " + I(p.panelTop) + " " + I(p.panelHeight) + " is not Kenshi's 368 344");
    /* REPORT A BUG outside a multiplayer world: its row set apart by the gap below EXIT GAME, RESUME one row and gap further. */
    if (!pausemenu::Place(k, false, true, false, 368, 344, &p)) { Fail("with the bug row refused"); return; }
    if (p.options != 165 || p.exitGame != 215 || p.bug != 285 || p.resume != 355 || p.panelHeight != 414 || p.panelTop != 333)
        Fail("bug row outside: bug " + I(p.bug) + " resume " + I(p.resume) + " panel " + I(p.panelTop) + "/" + I(p.panelHeight));
}
void t_pausemenu_closed_up_in_multiplayer()
{
    const pausemenu::KenshiRows k = PmKenshi();
    pausemenu::Placement p;
    if (!pausemenu::Place(k, true, true, false, 368, 344, &p)) { Fail("closed up refused"); return; }
    /* OPTIONS where NEW GAME stood, EXIT GAME a pitch below, REPORT A BUG a pitch and the gap below that, RESUME the same again. */
    if (p.options != 15 || p.exitGame != 65 || p.bug != 135 || p.resume != 205) Fail("closed-up rows " + I(p.options) + " " + I(p.exitGame) + " " + I(p.bug) + " " + I(p.resume));
    if (p.resume - p.bug != p.pitch + p.gap || p.bug - p.exitGame != p.pitch + p.gap || p.exitGame - p.options != p.pitch) Fail("pitch / gap not Kenshi's");
    /* The margin below RESUME is Kenshi's (344 - 285 = 59 from RESUME's top), and the centre stays: 368 + 172 = 408 + 132. */
    if (p.panelHeight != 264 || p.panelHeight - p.resume != 344 - 285) Fail("panel height " + I(p.panelHeight));
    if (p.panelTop != 408 || p.panelTop + p.panelHeight / 2 != 368 + 344 / 2) Fail("panel top " + I(p.panelTop));
    if (!pausemenu::Place(k, true, false, false, 368, 344, &p)) { Fail("closed up without the bug row refused"); return; }
    if (p.options != 15 || p.exitGame != 65 || p.resume != 135 || p.panelHeight != 194 || p.panelTop != 443) Fail("closed up, no bug row");
}
/* Each open places every row from Kenshi's positions and the panel as it stands: whichever of the two callers (REPORT A BUG's
   row, the show's own arranging) runs first, and however often, the rows end in one place, and the panel's centre never drifts
   across multiplayer / single-player / multiplayer. */
void t_pausemenu_order_and_reopen_never_move_twice()
{
    const pausemenu::KenshiRows k = PmKenshi();
    const bool closeUps[5] = { true, true, false, true, false };
    int top = 368, h = 344;
    pausemenu::Placement first[2];
    int haveFirst[2] = { 0, 0 };
    for (int open = 0; open < 5; ++open)
    {
        const bool cu = closeUps[open];
        pausemenu::Placement a, b, c, d;
        /* order 1: arranged before the bug row exists, then again with it; order 2: with it, twice */
        if (!pausemenu::Place(k, cu, false, false, top, h, &a)) { Fail("refused"); return; }
        if (!pausemenu::Place(k, cu, true, false, a.panelTop, a.panelHeight, &b)) { Fail("refused"); return; }
        if (!pausemenu::Place(k, cu, true, false, top, h, &c)) { Fail("refused"); return; }
        if (!pausemenu::Place(k, cu, true, false, c.panelTop, c.panelHeight, &d)) { Fail("refused"); return; }
        if (!PmSame(b, c) || !PmSame(c, d)) Fail("open " + I(open) + ": the order or a second call moved something");
        top = d.panelTop; h = d.panelHeight;
        const int s = cu ? 1 : 0;
        if (!haveFirst[s]) { first[s] = d; haveFirst[s] = 1; }
        else if (!PmSame(first[s], d)) Fail("open " + I(open) + ": the same state placed differently on a reopen");
        if (d.panelTop + d.panelHeight / 2 != 368 + 344 / 2) Fail("open " + I(open) + ": the centre drifted");
    }
    /* Back to Kenshi's own panel when the bug row is gone too. */
    pausemenu::Placement e;
    if (!pausemenu::Place(k, false, false, false, top, h, &e) || e.panelTop != 368 || e.panelHeight != 344) Fail("not back to Kenshi's panel");
    /* an odd height: there and back still lands on the same top */
    if (pausemenu::CentredTop(pausemenu::CentredTop(100, 345, 264), 264, 345) != 100) Fail("odd height round trip");
}
void t_pausemenu_refuses_rows_out_of_order()
{
    pausemenu::KenshiRows k = PmKenshi();
    pausemenu::Placement p;
    p.panelTop = -7;
    k.saveGame = k.newGame;
    if (pausemenu::Place(k, true, true, false, 0, 344, &p) || p.panelTop != -7) Fail("no pitch accepted");
    k = PmKenshi(); k.options = k.newGame;
    if (pausemenu::Place(k, true, true, false, 0, 344, &p)) Fail("OPTIONS not below NEW GAME accepted");
    k = PmKenshi(); k.exitGame = k.options;
    if (pausemenu::Place(k, true, true, false, 0, 344, &p)) Fail("EXIT GAME not below OPTIONS accepted");
    k = PmKenshi(); k.resume = k.exitGame + 49;
    if (pausemenu::Place(k, true, true, false, 0, 344, &p)) Fail("RESUME closer than a pitch accepted");
    k = PmKenshi(); k.resume = k.exitGame + 50;
    if (!pausemenu::Place(k, true, true, false, 0, 344, &p) || p.gap != 0) Fail("RESUME a pitch below (gap 0) refused");
    k = PmKenshi(); k.panelHeight = 0;
    if (pausemenu::Place(k, false, false, false, 0, 0, &p)) Fail("an empty panel accepted");
}

/* ================= T-524: HOSTING ON THE HOST'S PAUSE MENU, AND THE INTERNET ADDRESS FROM THE LOOKUP WEBSITES ================= */
/* HOSTING takes the top row (where NEW GAME stands), OPTIONS one pitch below it, every other pitch and gap Kenshi's, the panel
   one pitch taller around the same centre; the joiner's menu (no HOSTING) is the closed-up placement unchanged. */
void t_pausemenu_hosting_row()
{
    const pausemenu::KenshiRows k = PmKenshi();
    pausemenu::Placement p, q, r;
    if (!pausemenu::Place(k, true, true, true, 368, 344, &p)) { Fail("HOSTING refused"); return; }
    if (p.hosting != 15 || p.options != 65 || p.exitGame != 115 || p.bug != 185 || p.resume != 255)
        Fail("host rows " + I(p.hosting) + " " + I(p.options) + " " + I(p.exitGame) + " " + I(p.bug) + " " + I(p.resume));
    if (p.options - p.hosting != p.pitch || p.exitGame - p.options != p.pitch || p.bug - p.exitGame != p.pitch + p.gap || p.resume - p.bug != p.pitch + p.gap)
        Fail("host menu: pitch / gap not Kenshi's");
    if (p.panelHeight != 314 || p.panelHeight - p.resume != 344 - 285) Fail("host panel height " + I(p.panelHeight));
    if (p.panelTop != 383 || p.panelTop + p.panelHeight / 2 != 368 + 344 / 2) Fail("host panel top " + I(p.panelTop));
    if (!pausemenu::Place(k, true, true, false, 368, 344, &q) || q.options != 15 || q.resume != 205 || q.panelHeight != 264 || q.panelTop != 408)
        Fail("the joiner's menu changed");
    if (!pausemenu::Place(k, true, true, true, p.panelTop, p.panelHeight, &r) || !PmSame(p, r)) Fail("a second call on the host's menu moved something");
    if (!pausemenu::Place(k, true, true, false, p.panelTop, p.panelHeight, &r) || !PmSame(q, r)) Fail("host -> joiner placed differently");
    if (!pausemenu::Place(k, true, false, true, 368, 344, &r) || r.options != 65 || r.resume != 185 || r.panelHeight != 244) Fail("host menu without REPORT A BUG");
}
/* T-1153 (owner 1245): MULTIPLAYER directly below OPTIONS in a multiplayer world - EXIT GAME and every row below it one pitch
   lower, the panel one pitch taller around the same centre - with and without HOSTING and REPORT A BUG; the mock-up's order
   HOSTING, OPTIONS, MULTIPLAYER, EXIT GAME, REPORT A BUG, RESUME at Kenshi's pitch and gap. */
void t_pausemenu_multiplayer_row()
{
    const pausemenu::KenshiRows k = PmKenshi();
    /* REPORT A BUG?, HOSTING?; then HOSTING, OPTIONS, MULTIPLAYER, EXIT GAME, REPORT A BUG, RESUME (tops), the panel's height and top */
    static const int kWant[4][10] = {
        { 0, 0, -1, 15,  65, 115,  -1, 185, 244, 418 },
        { 1, 0, -1, 15,  65, 115, 185, 255, 314, 383 },
        { 0, 1, 15, 65, 115, 165,  -1, 235, 294, 393 },
        { 1, 1, 15, 65, 115, 165, 235, 305, 364, 358 } };
    for (int c = 0; c < 4; ++c)
    {
        const int* w = kWant[c];
        const bool bug = w[0] != 0, host = w[1] != 0;
        const std::string tag = std::string(bug ? "with" : "without") + " REPORT A BUG, " + (host ? "with" : "without") + " HOSTING: ";
        pausemenu::Placement p, q, r;
        if (!pausemenu::Place(k, true, bug, host, true, 368, 344, &p)) { Fail(tag + "refused"); continue; }
        if ((host && p.hosting != w[2]) || p.options != w[3] || p.multiplayer != w[4] || p.exitGame != w[5] || (bug && p.bug != w[6]) || p.resume != w[7])
            Fail(tag + "rows " + I(p.hosting) + " " + I(p.options) + " " + I(p.multiplayer) + " " + I(p.exitGame) + " " + I(p.bug) + " " + I(p.resume));
        if (p.panelHeight != w[8] || p.panelTop != w[9]) Fail(tag + "panel " + I(p.panelTop) + "/" + I(p.panelHeight));
        /* the mock-up's order at Kenshi's pitch, the gap before REPORT A BUG and RESUME, Kenshi's margin below RESUME, the centre kept */
        if ((host && p.options - p.hosting != p.pitch) || p.multiplayer - p.options != p.pitch || p.exitGame - p.multiplayer != p.pitch)
            Fail(tag + "MULTIPLAYER not one pitch below OPTIONS and above EXIT GAME");
        if ((bug && (p.bug - p.exitGame != p.pitch + p.gap || p.resume - p.bug != p.pitch + p.gap)) || (!bug && p.resume - p.exitGame != p.pitch + p.gap))
            Fail(tag + "the gap before REPORT A BUG / RESUME not Kenshi's");
        if (p.panelHeight - p.resume != 344 - 285 || p.panelTop + p.panelHeight / 2 != 368 + 344 / 2) Fail(tag + "margin or centre moved");
        /* a second call on the placed menu moves nothing; without MULTIPLAYER it is T-514 / T-524's placement, one pitch shorter */
        if (!pausemenu::Place(k, true, bug, host, true, p.panelTop, p.panelHeight, &q) || !PmSame(p, q)) Fail(tag + "a second call moved something");
        if (!pausemenu::Place(k, true, bug, host, false, 368, 344, &q) || !pausemenu::Place(k, true, bug, host, 368, 344, &r) || !PmSame(q, r))
            Fail(tag + "MULTIPLAYER off is not the placement without it");
        if (q.options != p.options || q.exitGame != p.exitGame - p.pitch || q.resume != p.resume - p.pitch || q.panelHeight != p.panelHeight - p.pitch)
            Fail(tag + "MULTIPLAYER is not exactly one row");
    }
}
/* T-1153 (owner 1245): tags.cfg's three keys (src/common/tagcfg.h) - every part on when the file or a key is missing, a bad line
   ignored, what is written is what is read for all eight choices, and the written text is exactly the three lines. */
void t_tagcfg_read_write()
{
    tagcfg::Parts p = tagcfg::Parse(std::string());
    if (!p.player || !p.character || !p.faction) Fail("no tags.cfg: not every part on");
    if (tagcfg::Format(tagcfg::Defaults()) != "tagPlayer=1\r\ntagCharacter=1\r\ntagFaction=1\r\n") Fail("defaults written as '" + tagcfg::Format(tagcfg::Defaults()) + "'");
    for (int m = 0; m < 8; ++m)
    {
        tagcfg::Parts a;
        a.player = (m & 1) != 0; a.character = (m & 2) != 0; a.faction = (m & 4) != 0;
        const tagcfg::Parts b = tagcfg::Parse(tagcfg::Format(a));
        if (!tagcfg::Same(a, b)) Fail("choice " + I(m) + " read back as " + tagcfg::Words(b));
    }
    p = tagcfg::Parse("tagCharacter=0\n");
    if (!p.player || p.character || !p.faction) Fail("tagCharacter=0: " + tagcfg::Words(p));
    p = tagcfg::Parse("  tagFaction = 0 \r\n");
    if (!p.player || !p.character || p.faction) Fail("spaces round the key and value: " + tagcfg::Words(p));
    p = tagcfg::Parse("tagPlayer=0");
    if (p.player) Fail("a last line with no line end not read");
    p = tagcfg::Parse("tagPlayer=0\ntagPlayer=1\n");
    if (!p.player) Fail("a key named twice: the last line does not win");
    p = tagcfg::Parse("tagPlayer=2\ntagCharacter=off\nTagFaction=0\ntagFaction\n=0\n");
    if (!p.player || !p.character || !p.faction) Fail("a bad value or an unknown key turned a part off: " + tagcfg::Words(p));
    const char junk[6] = { 't', 0, '=', '0', (char)0xFF, '\n' };
    p = tagcfg::Parse(std::string(junk, 6));
    if (!p.player || !p.character || !p.faction) Fail("junk bytes turned a part off");
    if (tagcfg::Words(tagcfg::Parse("tagPlayer=0\r\ntagFaction=0\r\n")) != "player=off character=on faction=off") Fail("the log words");
}

/* The websites, in the owner's order, each with its scheme (api.ipify.org HTTPS, the other two plain HTTP; the third the
   IPv4-only name), the limits (3 s a step, no further website after 12 s since the press, across the clock's wrap), and the
   reading of an answer: trimmed, a dotted IPv4 internet address, nothing else. */
void t_addrlookup_sites_and_answers()
{
    if (coopaddr::kLookupSiteCount != 3 || std::string(coopaddr::LookupSiteAt(0).name) != "api.ipify.org"
     || std::string(coopaddr::LookupSiteAt(1).name) != "checkip.amazonaws.com" || std::string(coopaddr::LookupSiteAt(2).name) != "ipv4.icanhazip.com")
        Fail("the lookup websites or their order");
    if (coopaddr::LookupSiteAt(0).https != 1 || coopaddr::LookupSiteAt(1).https != 0 || coopaddr::LookupSiteAt(2).https != 0)
        Fail("the lookup websites' schemes (HTTPS, HTTP, HTTP)");
    for (int i = 0; i < coopaddr::kLookupSiteCount; ++i)
    {
        const coopaddr::LookupSite& s = coopaddr::LookupSiteAt(i);
        std::string host;
        for (const wchar_t* c = s.host; *c != 0; ++c) host += (char)*c;
        if (host != s.name || std::wstring(s.path) != L"/") Fail(std::string("lookup website ") + s.name + ": host or path");
    }
    if (coopaddr::kLookupStepMs != 3000 || coopaddr::kLookupDeadlineMs != 12000UL) Fail("the lookup limits");
    if (coopaddr::LookupMayAskNext(0) != 1 || coopaddr::LookupMayAskNext(11999) != 1 || coopaddr::LookupMayAskNext(12000) != 0
     || coopaddr::LookupMayAskNext(60000) != 0)
        Fail("the lookup deadline");
    if (coopaddr::LookupElapsedMs(1000UL, 4000UL) != 3000UL || coopaddr::LookupElapsedMs(0xFFFFF000UL, 0x00000800UL) != 0x1800UL)
        Fail("the lookup's elapsed time (across the clock's wrap)");
    if (coopaddr::LookupMayAskNext(coopaddr::LookupElapsedMs(0xFFFFF000UL, 0x00001000UL)) != 1
     || coopaddr::LookupMayAskNext(coopaddr::LookupElapsedMs(0xFFFFF000UL, 12000UL)) != 0)
        Fail("the lookup deadline across the clock's wrap");
    struct C { const char* body; bool ok; const char* ip; };
    const C cases[] = {
        { "203.0.113.7", true, "203.0.113.7" }, { "203.0.113.7\n", true, "203.0.113.7" }, { " \t203.0.113.7\r\n", true, "203.0.113.7" },
        { "8.8.08.8", true, "8.8.8.8" }, { "", false, "" }, { "\r\n", false, "" }, { "203.0.113", false, "" }, { "203.0.113.7.1", false, "" },
        { "256.1.1.1", false, "" }, { "2001:db8::1", false, "" }, { "<html>203.0.113.7</html>", false, "" }, { "203.0.113.7 x", false, "" },
        { "192.168.1.20", false, "" }, { "10.0.0.1", false, "" }, { "100.64.0.1", false, "" }, { "127.0.0.1", false, "" }
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        std::string ip = "unchanged", why;
        const bool ok = coopaddr::LookupAnswer(cases[i].body, &ip, &why);
        if (ok != cases[i].ok) Fail(std::string("answer '") + cases[i].body + "' read as " + (ok ? "usable" : "not usable"));
        if (ok && ip != cases[i].ip) Fail(std::string("answer '") + cases[i].body + "' gave " + ip);
        if (!ok && (why.empty() || why.find("203") != std::string::npos || why.find("192") != std::string::npos)) Fail("why for '" + std::string(cases[i].body) + "': " + why);
    }
}
/* SHOW / COPY in every state, and the two answers: the router asked at open, the websites only after a press, a press waits
   for its address, SHOW shows / COPY copies without showing, nothing answered greys both until the window opens again. */
void t_netaddr_presses_and_answers()
{
    using namespace coopui;
    PanelNetStep s;
    s = PanelNetAddrPress(kNetAddrFound, kNetPressNone, kNetPressShow);
    if (s.state != kNetAddrFound || s.toggleShown != 1 || s.copyNow != 0 || s.startLookup != 0 || s.pending != kNetPressNone) Fail("found + SHOW");
    s = PanelNetAddrPress(kNetAddrFound, kNetPressNone, kNetPressCopy);
    if (s.copyNow != 1 || s.toggleShown != 0 || s.showNow != 0 || s.startLookup != 0) Fail("found + COPY");
    s = PanelNetAddrPress(kNetAddrAsking, kNetPressNone, kNetPressCopy);
    if (s.state != kNetAddrRacing || s.pending != kNetPressCopy || s.startLookup != 1 || s.copyNow != 0) Fail("asking + COPY: the websites are asked beside the router");
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressCopy, 1);
    if (s.state != kNetAddrFound || s.copyNow != 1 || s.showNow != 0 || s.pending != kNetPressNone) Fail("router found -> the waiting COPY");
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressShow, 1);
    if (s.showNow != 1 || s.copyNow != 0) Fail("router found -> the waiting SHOW");
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressNone, 1);
    if (s.state != kNetAddrFound || s.pending != kNetPressNone || s.showNow != 0 || s.copyNow != 0 || s.toggleShown != 0 || s.startLookup != 0)
        Fail("router found, no press waiting: something was shown, copied or asked");
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressNone, 0);
    if (s.state != kNetAddrMissing || s.startLookup != 0) Fail("router none, no press: the websites were asked");
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressShow, 0);
    if (s.state != kNetAddrLooking || s.startLookup != 1 || s.pending != kNetPressShow) Fail("router none with a waiting SHOW");
    s = PanelNetAddrPress(kNetAddrMissing, kNetPressNone, kNetPressShow);
    if (s.state != kNetAddrLooking || s.startLookup != 1 || s.pending != kNetPressShow || s.showNow != 0) Fail("missing + SHOW");
    s = PanelNetAddrPress(kNetAddrMissing, kNetPressNone, kNetPressCopy);
    if (s.state != kNetAddrLooking || s.startLookup != 1 || s.pending != kNetPressCopy || s.copyNow != 0 || s.showNow != 0 || s.toggleShown != 0)
        Fail("missing + COPY");
    s = PanelNetAddrPress(kNetAddrLooking, kNetPressShow, kNetPressCopy);
    if (s.state != kNetAddrLooking || s.pending != kNetPressCopy || s.startLookup != 0) Fail("looking + COPY");
    s = PanelNetLookupAnswer(kNetAddrLooking, kNetPressCopy, 1);
    if (s.state != kNetAddrFound || s.copyNow != 1 || s.showNow != 0) Fail("website found -> COPY without showing");
    s = PanelNetLookupAnswer(kNetAddrLooking, kNetPressShow, 1);
    if (s.state != kNetAddrFound || s.showNow != 1) Fail("website found -> SHOW");
    s = PanelNetLookupAnswer(kNetAddrLooking, kNetPressShow, 0);
    if (s.state != kNetAddrFailed || s.pending != kNetPressNone || s.showNow != 0 || s.copyNow != 0) Fail("no website answered");
    if (PanelNetAddrButtonsOn(s.state) != 0) Fail("no website answered: SHOW / COPY not greyed");
    s = PanelNetAddrPress(kNetAddrFailed, kNetPressNone, kNetPressShow);
    if (s.state != kNetAddrFailed || s.startLookup != 0 || s.showNow != 0) Fail("failed + SHOW asked again");
    for (int st = 0; st <= kNetAddrFailed; ++st)
    {
        if (st != kNetAddrAsking)
        { s = PanelNetRouterAnswer(st, kNetPressNone, 1); if (s.state != st || s.showNow || s.copyNow || s.startLookup) Fail("an old router answer acted in state " + I(st)); }
        if (st != kNetAddrLooking)
        { s = PanelNetLookupAnswer(st, kNetPressNone, 1); if (s.state != st || s.showNow || s.copyNow || s.startLookup) Fail("an old website answer acted in state " + I(st)); }
        s = PanelNetAddrPress(st, kNetPressNone, kNetPressNone);
        if (s.state != st || s.startLookup || s.showNow || s.toggleShown || s.copyNow) Fail("no press acted in state " + I(st));
    }
    s = PanelNetRouterAnswer(kNetAddrAsking, kNetPressNone, 0);
    s = PanelNetRouterAnswer(s.state, s.pending, 0);
    if (s.state == kNetAddrLooking || s.startLookup != 0) Fail("the websites were asked without a press");
}

/* ---- T-556 (t556a): the fallen list - src/common/fallenwire.h ---- */
swfallen::Fallen T556Snap(unsigned int uid, const char* name)
{
    swfallen::Fallen f;
    f.uid = uid; f.templateName = "Greenlander"; f.name = name; f.race = "Greenlander"; f.sid = "45123-rebirth.mod"; f.unique = 1; f.animal = 0; f.age = 0.0f;
    f.statsHave = 1;
    for (int i = 0; i < swfallen::kStatsCount; ++i) { float v = 1.5f * (float)i; std::memcpy(&f.stats[i], &v, 4); }
    f.stats[43] = 7u;
    const char look[] = "record\0bytes\x01\xff";
    f.look.assign(look, look + sizeof(look));
    f.x = -50978.5f; f.y = 1520.25f; f.z = 2932.75f; f.diedUnix = 1790000000ULL; f.cause = swfallen::kCauseMedical;
    f.handIndex = 4242; f.handSerial = 77;
    f.place = "The Hub";
    f.day = 41; f.why = swfallen::kWhyBloodLoss;
    return f;
}
bool T556Same(const swfallen::Fallen& a, const swfallen::Fallen& b)
{
    return a.uid == b.uid && a.templateName == b.templateName && a.name == b.name && a.race == b.race && a.sid == b.sid && a.unique == b.unique
        && a.animal == b.animal && a.age == b.age && a.statsHave == b.statsHave && std::memcmp(a.stats, b.stats, sizeof(a.stats)) == 0
        && a.look == b.look && a.x == b.x && a.y == b.y && a.z == b.z && a.diedUnix == b.diedUnix && a.cause == b.cause
        && a.handIndex == b.handIndex && a.handSerial == b.handSerial && a.place == b.place && a.day == b.day && a.why == b.why;
}
void t_t556_fallen_round_trip()
{
    using namespace swfallen;
    Fallen a = T556Snap(0x00C00011u, "Beep"), b;
    std::vector<char> bytes;
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || !T556Same(a, b)) Fail("a person's snapshot round trip");
    Fallen pet = T556Snap(0x00C00012u, "Bonedog Pup"); pet.animal = 1; pet.unique = 0; pet.age = 0.35f; pet.templateName = "Bonedog"; pet.race = "?";
    EncodeFallen(pet, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || !T556Same(pet, b) || b.animal != 1 || b.unique != 0) Fail("an animal's snapshot round trip");
    Fallen noStats = T556Snap(5, "X"); noStats.statsHave = 0;
    EncodeFallen(noStats, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.statsHave != 0 || b.stats[3] != 0) Fail("unread stats travel as zeros with the flag clear");
    EncodeFallen(a, &bytes);
    for (size_t cut = 0; cut < bytes.size(); ++cut)
        if (DecodeFallen(&bytes[0], cut, &b)) { Fail("a truncated snapshot was accepted"); break; }
    std::vector<char> more(bytes); more.push_back(0);
    if (DecodeFallen(&more[0], more.size(), &b)) Fail("trailing bytes were accepted");
    std::vector<char> ver(bytes); ver[0] = (char)(kFallenVersion + 1);
    if (DecodeFallen(&ver[0], ver.size(), &b)) Fail("another version was accepted");
    Fallen noTmpl = a; noTmpl.templateName = "";
    EncodeFallen(noTmpl, &bytes);
    if (DecodeFallen(&bytes[0], bytes.size(), &b)) Fail("an empty template name was accepted");
    Fallen longName = a; longName.name = std::string(80, 'n');
    EncodeFallen(longName, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.name.size() != kNameMax) Fail("a name over 64 bytes is cut to 64");
}
void t_t556_fallen_list_keeps_forty()
{
    using namespace swfallen;
    if (kFallenKeep != 40) Fail("the list keeps 40 (owner 510)");
    std::vector<Fallen> list;
    int cut = 0;
    for (unsigned int u = 1; u <= 45; ++u) cut += FallenPush(&list, T556Snap(u, "n"));
    if ((int)list.size() != kFallenKeep || cut != 5) Fail("45 deaths keep the newest 40 and cut 5");
    if (list[0].uid != 45 || list[39].uid != 6) Fail("the list is newest first");
    Fallen again = T556Snap(10, "again");
    if (FallenPush(&list, again) != 0 || (int)list.size() != kFallenKeep || list[0].name != "again") Fail("a second snapshot of one uid replaces the first");
    int tens = 0; for (size_t i = 0; i < list.size(); ++i) if (list[i].uid == 10) ++tens;
    if (tens != 1) Fail("one entry per uid");
    Fallen out;
    if (!FallenTake(&list, 0, &out) || out.uid != 10 || (int)list.size() != kFallenKeep - 1 || list[0].uid != 45) Fail("take 0 removes the newest");
    if (FallenTake(&list, 39, &out) || FallenTake(&list, -1, &out)) Fail("take outside the list is refused");
}
void t_t556_fallen_squad_choice()
{
    using namespace swfallen;
    std::vector<SquadRow> rows;
    SquadRow r;
    r.squad = 0xA0; r.alive = 0; rows.push_back(r);   /* row 0: squad A, dead */
    r.squad = 0xB0; r.alive = 1; rows.push_back(r);   /* row 1: squad B, alive -> squad 0 */
    r.squad = 0xA0; r.alive = 1; rows.push_back(r);   /* row 2: squad A, alive -> squad 1 */
    r.squad = 0xB0; r.alive = 1; rows.push_back(r);   /* row 3 */
    r.squad = 0;    r.alive = 1; rows.push_back(r);   /* row 4: no squad - never numbered */
    r.squad = 0xC0; r.alive = 0; rows.push_back(r);   /* row 5: squad C, nobody alive - never numbered */
    int n = 0;
    if (PickSquadAnchor(rows, -1, &n) != 1 || n != 2) Fail("the default is the squad of the first living member");
    if (PickSquadAnchor(rows, 0, &n) != 1) Fail("squad 0");
    if (PickSquadAnchor(rows, 1, &n) != 2) Fail("squad 1 is anchored on its first LIVING member");
    if (PickSquadAnchor(rows, 2, &n) != -1) Fail("a squad with no living member has no number");
    if (PickSquadAnchor(std::vector<SquadRow>(), -1, &n) != -1 || n != 0) Fail("no characters, no squad");
}
void t_t556_fallen_road()
{
    using namespace swfallen;
    if (RoadDecide(0, 2, 9, 2, 9) != kRoadCreate) Fail("fields equal to the member's: create alone");
    if (RoadDecide(0, 0, 9, 2, 9) != kRoadCreateRecruit) Fail("a movement field differs: recruit");
    if (RoadDecide(0, 2, 0, 2, 9) != kRoadCreateRecruit) Fail("the +0x1A0 field differs: recruit");
    if (RoadDecide(0, 2, 9, -1, 9) != kRoadCreateRecruit) Fail("the member unreadable: recruit");
    if (RoadDecide(1, 0, 0, 2, 9) != kRoadCreate) Fail("an animal is never recruited");
    if (RoadDecide(0, 2, 9, 2, -1) != kRoadCreateRecruit) Fail("the member's +0x1A0 field unreadable alone: recruit");
    if (std::string(RoadWord(kRoadCreateRecruit)) != "create+recruit" || std::string(CauseWord(kCauseMedical)) != "medical") Fail("words");
}
void t_t556_fallen_size_edges()
{
    using namespace swfallen;
    Fallen a = T556Snap(9, "Edge"), b;
    std::vector<char> bytes;
    a.look.assign(kLookMax, 'L');
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.look.size() != kLookMax) Fail("a record of exactly kLookMax is kept and read back");
    a.look.assign(kLookMax + 1, 'L');
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || !b.look.empty()) Fail("a record over kLookMax is not kept");
    a.look.clear();
    EncodeFallen(a, &bytes);
    const unsigned int big = (unsigned int)kLookMax + 1;   /* the last 4 bytes are the record's length */
    for (int i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = (char)((big >> (8 * i)) & 0xFF);
    if (DecodeFallen(&bytes[0], bytes.size(), &b)) Fail("a length over kLookMax is refused on decode");
    a.templateName = std::string(300, 't');
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.templateName.size() != kTextMax) Fail("a template name is cut to 255 bytes");
}
/* ---- T-556 effort 4: where a bring-back may stand, and the place of a death (src/common/fallenwire.h) ---- */
void t_t556_fallen_v2_row()
{
    using namespace swfallen;
    Fallen a = T556Snap(0x00C00051u, "Old"), b;
    a.look.clear(); a.place.clear();
    std::vector<char> bytes;
    EncodeFallen(a, &bytes);
    if (bytes[0] != (char)kFallenVersion || kFallenVersion != 4 || kFallenVersionNoDay != 3 || kFallenVersionNoPlace != 2) Fail("the snapshot's byte form is 4, the forms before it 3 and 2");
    /* with no look and no place the tail is: place length (1), day (4), how (1), record length (4) */
    std::vector<char> v3(bytes.begin(), bytes.end() - 9);
    v3.insert(v3.end(), bytes.end() - 4, bytes.end());
    v3[0] = (char)kFallenVersionNoDay;
    a.day = -1; a.why = kWhyUnknown;
    if (!DecodeFallen(&v3[0], v3.size(), &b) || !T556Same(a, b)) Fail("a row kept before the day reads, with day -1 and how unknown");
    std::vector<char> v3more(v3); v3more[0] = (char)kFallenVersion;
    if (DecodeFallen(&v3more[0], v3more.size(), &b)) Fail("a form-4 row without its day was accepted");
    /* the form before the place: no place byte either */
    std::vector<char> v2(bytes.begin(), bytes.end() - 10);
    v2.insert(v2.end(), bytes.end() - 4, bytes.end());
    v2[0] = (char)kFallenVersionNoPlace;
    if (!DecodeFallen(&v2[0], v2.size(), &b) || !T556Same(a, b) || !b.place.empty()) Fail("a row kept before the place reads, with no place");
    {   /* a fallen.txt line an earlier world server wrote (form 2) loads into the book */
        Book bk; int why = -1;
        const std::string prof = "p9.1";
        if (!BookParseLine("F " + Hex(prof.data(), prof.size()) + " " + Hex(&v2[0], v2.size()) + "\r", &bk, &why) || why != kLineOk
            || BookList(bk, prof).size() != 1 || BookList(bk, prof)[0].name != "Old" || !BookList(bk, prof)[0].place.empty())
            Fail("a form-2 fallen.txt line loads, with no place");
    }
    std::vector<char> v2more(v2); v2more[0] = (char)kFallenVersionNoDay;
    if (DecodeFallen(&v2more[0], v2more.size(), &b)) Fail("a form-3 row without its place was accepted");
    a.day = 0; a.why = kWhyStarvation;
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.day != 0 || b.why != kWhyStarvation) Fail("the day and how round trip");
    a.place = "Squin";
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.place != "Squin") Fail("the place round trip");
    a.place = std::string(300, 'p');
    EncodeFallen(a, &bytes);
    if (!DecodeFallen(&bytes[0], bytes.size(), &b) || b.place.size() != kTextMax) Fail("a place is cut to 255 bytes");
    std::vector<char> up; Up u;
    a.place = "Admag";
    EncodeAdd(&up, a);
    if (!DecodeUp(&up[0], up.size(), &u) || u.snap.place != "Admag") Fail("an ADD carries the place");
}
void t_t556_place_rules()
{
    using namespace swfallen;
    MateState m;
    if (BesideVerdict(m) != kBesideNoSuch) Fail("no character: noSuch");
    m.found = 1;
    if (BesideVerdict(m) != kBesideNotMine) Fail("another game's character: notMine");
    m.mine = 1;
    if (BesideVerdict(m) != kBesideNotOwn) Fail("another faction's character: notOwn");
    m.ownFaction = 1;
    if (BesideVerdict(m) != kBesideOk) Fail("a free own character: allowed");
    MateState d(m); d.dead = 1; d.unconscious = -1;
    if (BesideVerdict(d) != kBesideDead) Fail("dead comes before an unreadable state");
    MateState x(m); x.slaveState = -1;
    if (BesideVerdict(x) != kBesideUnreadable) Fail("an unreadable state refuses (fails closed)");
    x = m; x.dead = -1;
    if (BesideVerdict(x) != kBesideUnreadable) Fail("an unreadable death flag refuses");
    x = m; x.unconscious = 1; x.carried = 1;
    if (BesideVerdict(x) != kBesideKnockedOut) Fail("knocked out (and carried): ko first");
    x = m; x.carried = 1;
    if (BesideVerdict(x) != kBesideCarried) Fail("carried");
    x = m; x.inSomething = 2;
    if (BesideVerdict(x) != kBesideCaged) Fail("in a cage");
    x = m; x.inSomething = 1;
    if (BesideVerdict(x) != kBesideOk) Fail("in a bed is free");
    x = m; x.chained = 1;
    if (BesideVerdict(x) != kBesideChained) Fail("in chains");
    x = m; x.slaveState = 1;
    if (BesideVerdict(x) != kBesideEnslaved) Fail("a slave");
    x.slaveState = 2;
    if (BesideVerdict(x) != kBesideEnslaved) Fail("an escaping slave");
    x.slaveState = 3;
    if (BesideVerdict(x) != kBesideOk) Fail("an ex-slave is free");
    x.slaveState = 4;
    if (BesideVerdict(x) != kBesideUnreadable) Fail("a slave state past the engine's range does not read");
    /* an enemy that cannot fight */
    MateState o;
    if (AbleToFight(o) != 1) Fail("a free character can fight");
    o.unconscious = -1; o.carried = -1; o.inSomething = -1; o.chained = -1; o.slaveState = -1;
    if (AbleToFight(o) != 1) Fail("unreadable states leave an enemy able to fight");
    o = MateState(); o.unconscious = 1;
    if (AbleToFight(o) != 0) Fail("a knocked-out enemy cannot fight");
    o = MateState(); o.carried = 1;
    if (AbleToFight(o) != 0) Fail("a carried enemy cannot fight");
    o = MateState(); o.inSomething = 2;
    if (AbleToFight(o) != 0) Fail("a caged enemy cannot fight");
    o = MateState(); o.inSomething = 1;
    if (AbleToFight(o) != 1) Fail("an enemy in a bed can fight (it can get up)");
    o = MateState(); o.chained = 1;
    if (AbleToFight(o) != 0) Fail("a chained enemy cannot fight");
    o = MateState(); o.slaveState = 1;
    if (AbleToFight(o) != 0) Fail("an enslaved enemy cannot fight");
    o = MateState(); o.dead = 1;
    if (AbleToFight(o) != 0) Fail("a dead enemy cannot fight");
    if (std::string(UnableWhy(o)) != "dead") Fail("why: dead");
    o = MateState(); o.unconscious = 1; o.carried = 1;
    if (std::string(UnableWhy(o)) != "ko") Fail("why: knocked out (and carried) reads ko first");
    o.unconscious = 0;
    if (std::string(UnableWhy(o)) != "carried") Fail("why: carried");
    o = MateState(); o.inSomething = 2;
    if (std::string(UnableWhy(o)) != "caged") Fail("why: caged");
    o = MateState(); o.chained = 1;
    if (std::string(UnableWhy(o)) != "chained") Fail("why: chained");
    o = MateState(); o.slaveState = 2;
    if (std::string(UnableWhy(o)) != "enslaved") Fail("why: enslaved");
    o = MateState(); o.unconscious = -1;
    if (std::string(UnableWhy(o)) != "" || AbleToFight(o) != 1) Fail("why: an able enemy has no reason");
    /* T-562 / H070: another player's character at a hostile standing is an enemy even when isEnemyOf says no */
    if (EnemyVerdict(1, -1) != 1 || EnemyVerdict(0, 1) != 1 || EnemyVerdict(-1, 1) != 1) Fail("either test saying enemy makes an enemy");
    if (EnemyVerdict(0, 0) != 0 || EnemyVerdict(0, -1) != 0) Fail("neither saying enemy: not an enemy");
    if (EnemyVerdict(-1, 0) != -1 || EnemyVerdict(-1, -1) != -1) Fail("isEnemyOf unread and the standing not hostile: unread");
    NearRow nr; nr.dist = 100.0f; nr.enemy = 0;
    if (std::string(NotCountedWhy(nr, kEnemyNearUnits)) != "not an enemy") Fail("not counted: not an enemy");
    nr.enemy = -1;
    if (std::string(NotCountedWhy(nr, kEnemyNearUnits)) != "enemy state unreadable") Fail("not counted: unreadable");
    nr.enemy = 1; nr.able = 0;
    if (std::string(NotCountedWhy(nr, kEnemyNearUnits)) != "cannot fight") Fail("not counted: cannot fight");
    nr.able = 1;
    if (std::string(NotCountedWhy(nr, kEnemyNearUnits)) != "") Fail("a counted enemy has no not-counted reason");
    nr.enemy = 0; nr.dist = 600.0f;
    if (std::string(NotCountedWhy(nr, kEnemyNearUnits)) != "") Fail("outside the radius is not listed");
    /* the enemy rule */
    if (kEnemyNearUnits != 500.0f) Fail("the enemy radius is 50 m");
    std::vector<NearRow> rows;
    NearRow r;
    r.dist = 499.0f; r.enemy = 0; rows.push_back(r);     /* a friend near */
    r.dist = 501.0f; r.enemy = 1; rows.push_back(r);     /* an enemy just outside */
    r.dist = 100.0f; r.enemy = -1; rows.push_back(r);    /* unreadable near */
    EnemyCount e = CountEnemiesNear(rows, kEnemyNearUnits);
    if (e.within != 0 || e.unknown != 1 || e.nearestRow != -1 || e.nearest != -1.0f) Fail("no enemy within 50 m (an unreadable one only counted)");
    if (PlaceVerdict(m, e) != kBesideOk) Fail("an unreadable near character never refuses");
    r.dist = 500.0f; r.enemy = 1; rows.push_back(r);     /* an enemy at exactly 50 m */
    r.dist = 320.0f; r.enemy = 1; rows.push_back(r);
    r.dist = -1.0f; r.enemy = 1; rows.push_back(r);      /* a bad distance */
    r.dist = 50.0f; r.enemy = 1; r.able = 0; rows.push_back(r);   /* a knocked-out enemy beside it */
    e = CountEnemiesNear(rows, kEnemyNearUnits);
    if (e.within != 2 || e.nearestRow != 4 || e.nearest != 320.0f || e.unable != 1) Fail("two enemies within 50 m, the nearest at 32 m; one that cannot fight not counted");
    std::vector<NearRow> only(1); only[0].dist = 10.0f; only[0].enemy = 1; only[0].able = 0;
    if (PlaceVerdict(m, CountEnemiesNear(only, kEnemyNearUnits)) != kBesideOk) Fail("only enemies that cannot fight: allowed");
    if (PlaceVerdict(m, e) != kBesideEnemyNear) Fail("an enemy near refuses a free squadmate");
    if (PlaceVerdict(d, e) != kBesideDead) Fail("the squadmate's own reason comes before the enemy rule");
    if (std::string(BesideTag(kBesideEnemyNear)) != "enemyNear" || std::string(BesideTag(kBesideKnockedOut)) != "ko" || std::string(BesideTag(99)) != "?")
        Fail("the reason tags");
    for (int i = 0; i < kBesideCodes; ++i) if (std::string(BesideWord(i)) == "?") Fail("every reason has words");
}
/* ---- T-556 effort 2: the fallen list on the world server, and a named character brought back (src/common/fallenwire.h) ---- */
void t_t556_fallen_wire()
{
    using namespace swfallen;
    if (kMsgFallen != 61) Fail("FALLEN is not 61");
    Fallen a = T556Snap(0x00C00031u, "Ruka");
    std::vector<char> b; Up u;
    EncodeAdd(&b, a);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpAdd || u.uid != a.uid || !T556Same(u.snap, a)) Fail("an ADD round trip");
    std::vector<char> more(b); more.push_back(0);
    if (DecodeUp(&more[0], more.size(), &u)) Fail("an ADD with trailing bytes was accepted");
    for (size_t cut = 0; cut < b.size(); cut += 7) if (DecodeUp(&b[0], cut, &u)) { Fail("a truncated ADD was accepted"); break; }
    EncodeTake(&b, 0x00C00031u, 7731u);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpTake || u.uid != 0x00C00031u || u.newSerial != 7731u) Fail("a TAKE round trip");
    EncodeAsk(&b);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpAsk) Fail("an ASK round trip");
    EncodeAlive(&b, 55);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpAlive || u.uid != 55) Fail("an ALIVE round trip");
    EncodeUndo(&b, 56);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpUndo || u.uid != 56) Fail("an UNDO round trip");
    std::vector<unsigned int> ids; ids.push_back(3); ids.push_back(9);
    EncodeSaved(&b, ids);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpSaved || u.uids != ids) Fail("a SAVED round trip");
    b[1] = 41;   /* the count past 40 */
    if (DecodeUp(&b[0], b.size(), &u)) Fail("a SAVED of 41 was accepted");
    b.assign(1, (char)9);
    if (DecodeUp(&b[0], b.size(), &u)) Fail("an unknown kind was accepted");
    std::vector<Fallen> rows, back; std::vector<Pend> pend; std::vector<PendWire> pw;
    for (unsigned int i = 1; i <= 3; ++i) rows.push_back(T556Snap(i, "r"));
    rows[1].unique = 0; rows[1].sid = "";
    Pend p; p.row = T556Snap(40, "taken"); p.newSerial = 9001; pend.push_back(p);
    EncodeTable(&b, rows, pend);
    if (!DecodeTable(&b[0], b.size(), &back, &pw) || back.size() != 3 || !T556Same(back[0], rows[0]) || !T556Same(back[1], rows[1])
        || pw.size() != 1 || pw[0].uid != 40 || pw[0].newSerial != 9001 || !T556Same(pw[0].row, p.row)) Fail("a TABLE round trip, pending rows included");
    EncodeTable(&b, std::vector<Fallen>(), std::vector<Pend>());
    if (!DecodeTable(&b[0], b.size(), &back, &pw) || !back.empty() || !pw.empty()) Fail("an empty TABLE round trip");
    for (unsigned int i = 4; i <= 45; ++i) rows.push_back(T556Snap(i, "r"));
    EncodeTable(&b, rows, pend);
    if (!DecodeTable(&b[0], b.size(), &back, &pw) || (int)back.size() != kFallenKeep) Fail("a TABLE carries at most 40 rows");
    b[1] = 41; b[2] = 0; b[3] = 0; b[4] = 0;
    if (DecodeTable(&b[0], b.size(), &back, &pw)) Fail("a TABLE claiming 41 rows was accepted");
    EncodeAdd(&b, a);
    if (DecodeTable(&b[0], b.size(), &back, &pw)) Fail("an ADD read as a TABLE");
}
void t_t556_fallen_book_file()
{
    using namespace swfallen;
    Book bk;
    int cut = 0;
    for (unsigned int u = 1; u <= 45; ++u) cut += BookAdd(&bk, "p100.1", T556Snap(u, "alex"));
    BookAdd(&bk, "p200.2", T556Snap(900, "sam"));
    if ((int)BookList(bk, "p100.1").size() != kFallenKeep || cut != 5 || BookList(bk, "p100.1")[0].uid != 45) Fail("40 per profile, newest first");
    if (BookList(bk, "p200.2").size() != 1 || BookRows(bk) != 41) Fail("each profile has its own list");
    if (BookAdd(&bk, "p100.1", T556Snap(12, "again")) != 0 || BookList(bk, "p100.1")[0].name != "again" || (int)BookList(bk, "p100.1").size() != kFallenKeep) Fail("an ADD replaces an older row of the same character");
    int twelves = 0; for (size_t i = 0; i < BookList(bk, "p100.1").size(); ++i) if (BookList(bk, "p100.1")[i].uid == 12) ++twelves;
    if (twelves != 1) Fail("one row per character");
    BookAdd(&bk, "p200.2", T556Snap(12, "same uid, other profile"));
    if (BookList(bk, "p200.2").size() != 2) Fail("another profile's row of the same uid is its own");
    /* take -> pending */
    Fallen out;
    if (!BookTake(&bk, "p100.1", 12, 5012, &out) || out.name != "again" || (int)BookList(bk, "p100.1").size() != kFallenKeep - 1
        || BookPending(bk, "p100.1").size() != 1 || BookPending(bk, "p100.1")[0].newSerial != 5012) Fail("a TAKE makes the row pending");
    if (BookTake(&bk, "p100.1", 12, 5012, &out)) Fail("a second TAKE of the same row finds nothing");
    if (BookTake(&bk, "p300.1", 1, 1, &out)) Fail("a TAKE for a profile with no list finds nothing");
    /* the file, pending rows included */
    const std::string text = BookFile(bk);
    if (text.find("\nP ") == std::string::npos) Fail("a pending row is written as a P line");
    Book rd; std::istringstream f(text); std::string line; int bad = 0;
    while (std::getline(f, line)) { int why = 0; if (!BookParseLine(line + "\r", &rd, &why)) ++bad; }
    if (bad != 0 || BookFile(rd) != text) Fail("the file does not read back");
    if (BookList(rd, "p100.1")[0].uid != 45 || !T556Same(BookList(rd, "p200.2")[1], T556Snap(900, "sam")) || BookPending(rd, "p100.1")[0].newSerial != 5012) Fail("the file's order or rows differ");
    int why = 0;
    const std::string first = text.substr(0, text.find('\n'));
    if (BookParseLine(first, &rd, &why) || why != kLineDuplicate) Fail("a duplicate row is refused");
    const std::string pline = text.substr(text.find("\nP ") + 1, text.find('\n', text.find("\nP ") + 1) - text.find("\nP ") - 1);
    if (BookParseLine("F" + pline.substr(1, pline.find(' ', 2) - 1) + " " + pline.substr(pline.find(' ', pline.find(' ', 2) + 1) + 1), &rd, &why) || why != kLineDuplicate) Fail("a listed row of a pending uid is refused");
    if (BookParseLine("F zz 00", &rd, &why) || why != kLineBad) Fail("bad hex is refused");
    if (BookParseLine("P 7066 x1 00", &rd, &why) || why != kLineBad) Fail("a P line with a bad serial is refused");
    if (BookParseLine("X 00 00", &rd, &why) || BookParseLine("", &rd, &why)) Fail("a line that is not an F / P line is refused");
    Book full; for (unsigned int u = 1; u <= 41; ++u) { std::vector<char> by; EncodeFallen(T556Snap(u, "x"), &by); const std::string l = "F " + Hex("pf", 2) + " " + Hex(&by[0], by.size()); BookParseLine(l, &full, &why); }
    if (why != kLineOverKeep || (int)BookList(full, "pf").size() != kFallenKeep) Fail("a 41st row in the file is refused");
    /* saved, undo */
    Pend pd;
    if (BookSaved(&bk, "p100.1", 99, &pd)) Fail("SAVED of a row that is not pending finds nothing");
    Book undo = bk;
    if (!BookSaved(&bk, "p100.1", 12, &pd) || pd.row.uid != 12 || !BookPending(bk, "p100.1").empty() || bk.pending.count("p100.1") != 0) Fail("SAVED makes a pending row final");
    if (!BookUndo(&undo, "p100.1", 12, &pd, &cut) || cut != 0 || (int)BookList(undo, "p100.1").size() != kFallenKeep || !BookPending(undo, "p100.1").empty()) Fail("UNDO puts the row back on the list");
    if (BookList(undo, "p100.1")[kFallenKeep - 1].uid != 12) Fail("the row goes back in its place by time of death (after rows of the same time)");
    Book order; Fallen older = T556Snap(70, "older"); older.diedUnix = 100; Fallen newer = T556Snap(71, "newer"); newer.diedUnix = 300;
    BookAdd(&order, "q", older); BookAdd(&order, "q", newer);
    Fallen mid = T556Snap(72, "mid"); mid.diedUnix = 200; BookAdd(&order, "q", mid); BookTake(&order, "q", 72, 1, &out);
    if (!BookUndo(&order, "q", 72, &pd, &cut) || BookList(order, "q")[0].uid != 71 || BookList(order, "q")[1].uid != 72 || BookList(order, "q")[2].uid != 70) Fail("UNDO keeps newest first");
    {
        Book sp; Fallen first = T556Snap(80, "eve"); first.handSerial = 380; first.diedUnix = 100;
        BookAdd(&sp, "r", first); BookTake(&sp, "r", 80, 880, &out);
        Fallen again = T556Snap(81, "eve"); again.handSerial = 880; again.diedUnix = 200;   /* the brought-back eve died too */
        BookAdd(&sp, "r", again);
        std::vector<Fallen> samePerson;
        if (!BookUndo(&sp, "r", 80, &pd, &cut, &samePerson) || BookList(sp, "r").size() != 1 || BookList(sp, "r")[0].uid != 80
            || samePerson.size() != 1 || samePerson[0].uid != 81) Fail("UNDO drops the brought-back character's own later death: one person, one row");
    }
    BookTake(&order, "q", 70, 1, &out);
    BookAdd(&order, "q", T556Snap(70, "died again"));
    if (!BookPending(order, "q").empty() || BookList(order, "q")[0].name != "died again") Fail("a new death of a pending uid replaces the pending row");
    /* alive, profile deleted */
    if (!BookDrop(&bk, "p200.2", 900, &out) || BookList(bk, "p200.2").size() != 1) Fail("ALIVE drops a listed row");
    if (BookDrop(&bk, "p200.2", 900, &out)) Fail("a second ALIVE finds nothing");
    BookTake(&bk, "p200.2", 12, 3, &out);
    if (bk.lists.count("p200.2") != 0 || BookPending(bk, "p200.2").size() != 1) Fail("an emptied list goes, its pending row stays");
    if (BookProfileDeleted(&bk, "p200.2") != 1 || bk.pending.count("p200.2") != 0) Fail("a deleted profile's pending rows go");
    if (BookProfileDeleted(&bk, "p100.1") != (size_t)(kFallenKeep - 1) || BookRows(bk) != 0 || BookPendingRows(bk) != 0 || BookProfileDeleted(&bk, "p100.1") != 0) Fail("a deleted profile's list goes");
    if (BookFile(bk) != "") Fail("an empty book writes an empty file");
}
void t_t556_fallen_table_apply()
{
    using namespace swfallen;
    std::vector<Fallen> table, list; std::vector<Taken> taken;
    for (unsigned int u = 1; u <= 3; ++u) table.push_back(T556Snap(u, "t"));
    TableApply(table, &taken, &list);
    if (list.size() != 3 || !taken.empty()) Fail("a table becomes the copy");
    Taken t; t.uid = 2; t.gen = 1; t.sentMs = 1000; taken.push_back(t);
    TableApply(table, &taken, &list);
    if (list.size() != 2 || list[0].uid != 1 || list[1].uid != 3 || taken.size() != 1) Fail("a table sent before the TAKE was handled does not offer the row again");
    if (RetryDue(1, 1000, 1, 1000 + kRetryMs - 1)) Fail("not due on the same link before the wait");
    if (!RetryDue(1, 1000, 1, 1000 + kRetryMs)) Fail("due on the same link after the wait (a refused TAKE)");
    if (!RetryDue(1, 1000, 2, 1001)) Fail("due at once on a new link");
    if (!RetryDue(-1, 0, 2, 5)) Fail("a message never sent is due once a link is up");
    if (RetryDue(-1, 0, -1, 5) || RetryDue(1, 0, -1, 99999)) Fail("never due with no link");
    if (!RetryDue(1, 0xFFFFFF00u, 1, 0xFFFFFF00u + kRetryMs)) Fail("the wait survives the tick counter wrapping");
    table.erase(table.begin() + 1);   /* the server dropped row 2 */
    TableApply(table, &taken, &list);
    if (!taken.empty() || list.size() != 2) Fail("a taken row the table no longer lists is forgotten");
    TableApply(std::vector<Fallen>(), &taken, &list);
    if (!list.empty()) Fail("an empty table empties the copy");
}
swfallen::LiveChar T556Live(unsigned int serial, const char* name, const char* tmpl, const char* race)
{
    swfallen::LiveChar c; c.serial = serial; c.name = name; c.templateName = tmpl; c.race = race; return c;
}
void t_t556_fallen_reconcile()
{
    using namespace swfallen;
    std::vector<Fallen> listed; std::vector<PendWire> pend; std::vector<LiveChar> live; Reconciled out;
    Fallen alex = T556Snap(1, "alex"); alex.handSerial = 501; alex.unique = 0;
    Fallen sam = T556Snap(2, "sam"); sam.handSerial = 502; sam.unique = 0;
    Fallen noHand = T556Snap(3, "unread"); noHand.handSerial = 0; noHand.unique = 0;
    Fallen seto = T556Snap(4, "Seto"); seto.handSerial = 504; seto.unique = 1; seto.templateName = "Seto"; seto.race = "Shek";
    Fallen other = T556Snap(5, "pup"); other.handSerial = 505; other.unique = 0; other.templateName = "Bonedog"; other.race = "Bonedog";
    listed.push_back(alex); listed.push_back(sam); listed.push_back(noHand); listed.push_back(seto); listed.push_back(other);
    live.push_back(T556Live(501, "alex", "Greenlander", "Greenlander"));
    live.push_back(T556Live(504, "Seto", "Seto", "Shek"));
    live.push_back(T556Live(505, "rex", "Garru", "Garru"));   /* the pup's serial on another kind of character */
    live.push_back(T556Live(0, "zero", "Greenlander", "Greenlander"));
    Reconcile(listed, pend, live, false, &out);
    if (out.alive.size() != 1 || out.alive[0] != 1) Fail("ALIVE: same serial and kind only; never an unread handle, never a named row, never another kind on the same serial");
    if (!out.saved.empty() || !out.undo.empty() || !out.held.empty()) Fail("between loads only the ALIVE check runs");
    /* pending, once per load */
    PendWire bySerial; bySerial.uid = 10; bySerial.newSerial = 610; bySerial.row = T556Snap(10, "ana"); bySerial.row.unique = 0; bySerial.row.handSerial = 410;
    PendWire byName; byName.uid = 11; byName.newSerial = 611; byName.row = T556Snap(11, "ben"); byName.row.unique = 0; byName.row.handSerial = 411;
    PendWire onlyOriginal; onlyOriginal.uid = 12; onlyOriginal.newSerial = 612; onlyOriginal.row = T556Snap(12, "cal"); onlyOriginal.row.unique = 0; onlyOriginal.row.handSerial = 412;
    PendWire gone; gone.uid = 13; gone.newSerial = 613; gone.row = T556Snap(13, "dee"); gone.row.unique = 0; gone.row.handSerial = 413;
    PendWire namedNoProof; namedNoProof.uid = 14; namedNoProof.newSerial = 614; namedNoProof.row = T556Snap(14, "Ruka"); namedNoProof.row.unique = 1; namedNoProof.row.handSerial = 414;
    PendWire namedProof; namedProof.uid = 15; namedProof.newSerial = 615; namedProof.row = T556Snap(15, "Burn"); namedProof.row.unique = 1; namedProof.row.handSerial = 415;
    pend.push_back(bySerial); pend.push_back(byName); pend.push_back(onlyOriginal); pend.push_back(gone); pend.push_back(namedNoProof); pend.push_back(namedProof);
    live.clear();
    live.push_back(T556Live(610, "someone", "Greenlander", "Greenlander"));   /* the new serial */
    live.push_back(T556Live(9999, "ben", "Greenlander", "Greenlander"));      /* ben by name, template and race under another serial */
    live.push_back(T556Live(412, "cal", "Greenlander", "Greenlander"));       /* cal's ORIGINAL character: not the bring-back */
    live.push_back(T556Live(415, "Burn", "Greenlander", "Greenlander"));      /* Burn's original alive: the save predates the death */
    Reconcile(std::vector<Fallen>(), pend, live, true, &out);
    if (out.saved.size() != 2 || out.saved[0] != 10 || out.saved[1] != 11) Fail("a bring-back is in the loaded world by its new serial, or by name + template + race");
    if (out.undo.size() != 3 || out.undo[0] != 12 || out.undo[1] != 13 || out.undo[2] != 15) Fail("UNDO: the row's own original is not the bring-back; an absent one is undone; a named one only with its original alive");
    if (out.held.size() != 1 || out.held[0] != 14) Fail("a named bring-back with no proof the save predates it stays pending");
    /* the unanswered TAKE / ALIVE carried over a teardown */
    Taken take; take.kind = kUpTake; take.newSerial = 610;
    Taken alive; alive.kind = kUpAlive; alive.rowSerial = 412;
    Taken absent; absent.kind = kUpTake; absent.newSerial = 777;
    if (!TakenStillTrue(take, live) || !TakenStillTrue(alive, live) || TakenStillTrue(absent, live)) Fail("a carried-over TAKE / ALIVE is kept only while true in the new world");
    Taken noSerial; noSerial.kind = kUpAlive;
    if (TakenStillTrue(noSerial, live)) Fail("a carried-over message with no serial is dropped");
}
void t_t556_unique_bring_back()
{
    using namespace swfallen;
    UniqueRow r;
    if (UniqueOnGameState(false, &r, 0, 1) != kUqStored || r.state != 0 || r.playerInvolved != 1 || r.back != 0) Fail("a death is stored");
    if (UniqueOnGameState(true, &r, 1, 0) != kUqRefusedDead || r.state != 0) Fail("DEAD is final for a game's ALIVE");
    if (UniqueOnGameState(true, &r, 2, 0) != kUqRefusedDead) Fail("DEAD is final for a game's IMPRISONED");
    if (UniqueOnBringBack(true, &r) != 0 || r.state != 1 || r.playerInvolved != 0 || r.back != 1 || r.backNow != 1) Fail("a bring-back makes the row ALIVE, back 1");
    if (UniqueOnGameState(true, &r, 2, 0) != kUqStored || r.state != 2 || r.back != 1) Fail("a brought-back character's states are stored, back kept");
    if (UniqueOnGameState(true, &r, 0, 1) != kUqStored || r.state != 0 || r.back != 1) Fail("a brought-back character's death is recorded again");
    if (UniqueOnGameState(true, &r, 1, 0) != kUqRefusedDead) Fail("dead again is final again");
    if (UniqueOnBringBack(true, &r) != 0 || r.state != 1 || r.back != 2 || r.backNow != 2) Fail("a second bring-back: back 2");
    UniqueRow u = r; UniqueOnUndo(&u);
    if (u.state != 0 || u.back != 1 || u.backNow != 1) Fail("an UNDO makes it DEAD again, back - 1");
    UniqueRow zero; zero.state = 0; UniqueOnUndo(&zero);
    if (zero.back != 0 || zero.backNow != 0) Fail("an UNDO never takes back below 0");
    UniqueRow none;
    if (UniqueOnBringBack(false, &none) != -1 || none.state != 1 || none.back != 1) Fail("a bring-back with no row makes one");
    /* the file line */
    std::string sid; UniqueRow p;
    UniqueRow dead; dead.state = 0; dead.playerInvolved = 1;
    if (UniqueFileLine("42-rebirth.mod", dead) != "v1\t42-rebirth.mod\t0\t1\n") Fail("a row never brought back keeps the v1 line: " + UniqueFileLine("42-rebirth.mod", dead));
    if (UniqueFileLine("42-rebirth.mod", r) != "v2\t42-rebirth.mod\t1\t0\t2\n") Fail("a brought-back row is a v2 line");
    if (!UniqueParseFileLine("v1\t42-rebirth.mod\t0\t1\r", &sid, &p) || sid != "42-rebirth.mod" || p.state != 0 || p.playerInvolved != 1 || p.back != 0) Fail("a v1 line reads");
    if (!UniqueParseFileLine(UniqueFileLine("Seto", r), &sid, &p) || sid != "Seto" || p.state != 1 || p.back != 2 || p.backNow != 0) Fail("a v2 line reads back (backNow is never written)");
    if (UniqueParseFileLine("v1\t\t0\t0", &sid, &p) || UniqueParseFileLine("v1\tx\t3\t0", &sid, &p) || UniqueParseFileLine("v2\tx\t1\t0", &sid, &p) || UniqueParseFileLine("v3\tx\t1\t0\t1", &sid, &p))
        Fail("an empty sid, a state past 2, a short v2 line and another version are refused");
    /* the deferred merge: memory started from an empty map, so a bring-back this session has back = backNow = 1 */
    UniqueRow merged;
    UniqueRow fileDead; fileDead.state = 0;
    UniqueRow held; UniqueOnBringBack(false, &held);
    if (UniqueMergeRows(true, fileDead, held, &merged) != coopmerge::kMergeTakeDeferred || merged.state != 1 || merged.back != 1 || merged.backNow != 0) Fail("a bring-back held while the file was shut wins over the file's DEAD");
    UniqueRow fileDeadBack1; fileDeadBack1.state = 0; fileDeadBack1.back = 1;   /* brought back once before, died again */
    if (UniqueMergeRows(true, fileDeadBack1, held, &merged) != coopmerge::kMergeTakeDeferred || merged.state != 1 || merged.back != 2) Fail("a second bring-back held while the file was shut wins too, and its count adds to the file's");
    UniqueRow heldAlive; heldAlive.state = 1;
    if (UniqueMergeRows(true, fileDeadBack1, heldAlive, &merged) != coopmerge::kMergeKeepLoaded || merged.state != 0 || merged.back != 1) Fail("an ALIVE with no bring-back this session stays refused, the file's count kept");
    UniqueRow heldUndone = held; UniqueOnUndo(&heldUndone);
    if (UniqueMergeRows(true, fileDeadBack1, heldUndone, &merged) != coopmerge::kMergeTakeDeferred || merged.state != 0 || merged.back != 1) Fail("an undone bring-back never lowers the file's count");
    UniqueRow fileAliveBack3; fileAliveBack3.state = 1; fileAliveBack3.back = 3; UniqueRow heldDeath; heldDeath.state = 0;
    if (UniqueMergeRows(true, fileAliveBack3, heldDeath, &merged) != coopmerge::kMergeTakeDeferred || merged.state != 0 || merged.back != 3) Fail("a death held while shut is stored, the count kept");
    if (UniqueMergeRows(false, UniqueRow(), held, &merged) != coopmerge::kMergeInsertDeferred || merged.back != 1 || merged.backNow != 0) Fail("a row the file lacks is inserted");
    /* a game's side */
    if (!BringBackWrite(0, 1, 1, 0)) Fail("the server's bring-back is written over this game's DEAD");
    if (!BringBackWrite(0, 2, 1, 0)) Fail("an IMPRISONED brought-back character is written too");
    if (BringBackWrite(0, 1, 0, 0)) Fail("an ALIVE with no bring-back never lifts a DEAD");
    if (BringBackWrite(0, 1, 1, 1)) Fail("this game's own dead body keeps its DEAD (it died again)");
    if (BringBackWrite(1, 0, 1, 0) || BringBackWrite(2, 1, 1, 0)) Fail("only a DEAD entry is written past");
    if (DeadMayPublish(0, 1)) Fail("a DEAD read while a living own character carries the record is not published");
    if (!DeadMayPublish(0, 0) || !DeadMayPublish(1, 1)) Fail("a DEAD with no living carrier, and every other state, are published");
}
void t_t556_fallen_protocol_74()
{
    const std::string plugin = T313ReadFile("../coop-plugin/store.cpp"), server = T313ReadFile("../coop-store/store_main.cpp");
    if (plugin.empty() || server.empty()) { Fail("could not read the plugin's store.cpp or the world server's store_main.cpp"); return; }
    if (plugin.find("kStoreMsgFallen = 61;") == std::string::npos || server.find("MSG_FALLEN = 61") == std::string::npos || swfallen::kMsgFallen != 61u) Fail("FALLEN is not 61 on both sides and in fallenwire.h");
    if (server.find("PutU32(&m, r.back)") == std::string::npos) Fail("the world server's UNIQUE_STATE does not carry the bring-back count");
}

/* ---- T-556 effort 3a: the resurrection fee, its host options and A (src/common/resurrectfee.h, fallenwire.h back rows) ---- */
void t_t556_fee_price()
{
    using namespace swfee;
    Price p = PriceFor(1000, kGrowthSteep, 0);
    if (p.price != 0 || p.capped || p.alive != 0) Fail("A = 0: free");
    p = PriceFor(1000, kGrowthSteep, 1);
    if (p.price != 1000) Fail("steep, A = 1: the amount");
    p = PriceFor(1000, kGrowthSteep, 2);
    if (p.price != 4000 || p.amount != 1000 || p.alive != 2 || p.growth != kGrowthSteep) Fail("steep, A = 2: 1000 x 2 x 2 = 4000, with its parts");
    p = PriceFor(1000, kGrowthSteady, 3);
    if (p.price != 3000 || p.growth != kGrowthSteady) Fail("steady, A = 3: 1000 x 3 = 3000");
    if (PriceFor(1000, kGrowthSteep, 3).price != 9000) Fail("steep, A = 3: 9000");
    if (PriceFor(0, kGrowthSteep, 5).price != 0) Fail("an amount of 0 is free");
    if (PriceFor(-5, kGrowthSteep, 2).price != 0 || PriceFor(1000, kGrowthSteep, -2).price != 0) Fail("a negative amount or A reads as 0");
    if (PriceFor(1000, 7, 2).price != 4000 || PriceFor(1000, 7, 2).growth != kGrowthSteep) Fail("an unknown growth reads as steep");
    p = PriceFor(kMoneyMax, kGrowthSteady, 1);
    if (p.price != kMoneyMax || p.capped) Fail("the largest amount x 1 is exact, not capped");
    p = PriceFor(kMoneyMax, kGrowthSteady, 2);
    if (p.price != kMoneyMax || !p.capped) Fail("steady over the money field's max is capped there");
    p = PriceFor(100000, kGrowthSteep, 200);   /* 100000 x 200 = 2e7, x 200 = 4e9: over */
    if (p.price != kMoneyMax || !p.capped) Fail("steep over the money field's max is capped there");
    p = PriceFor(53687, kGrowthSteep, 200);    /* 53687 x 40000 = 2147480000: under */
    if (p.price != 2147480000LL || p.capped) Fail("steep just under the max is exact");
    p = PriceFor(1, kGrowthSteep, 2147483647);
    if (p.price != kMoneyMax || !p.capped) Fail("a huge A does not overflow");
    if (RuleWords(PriceFor(1000, kGrowthSteep, 2)) != "steep: 1000 x 2 x 2 = 4000") Fail("the steep rule's words");
    if (RuleWords(PriceFor(1000, kGrowthSteady, 3)) != "steady: 1000 x 3 = 3000") Fail("the steady rule's words");
    if (RuleWords(PriceFor(1000, kGrowthSteep, 0)).find("free") == std::string::npos) Fail("a free price says so");
    if (RuleWords(PriceFor(kMoneyMax, kGrowthSteady, 2)).find("capped") == std::string::npos) Fail("a capped price says so");
}
void t_t556_fee_options()
{
    using namespace swfee;
    if (std::string(kKeyOn) != "resurrect" || std::string(kKeyAmount) != "resurrectfee" || std::string(kKeyGrowth) != "resurrectgrowth") Fail("the key names");
    if (OnCode("on") != 1 || OnCode("off") != 0 || OnCode("On") != -1 || OnCode("") != -1) Fail("resurrect: on | off");
    if (GrowthCode("steady") != kGrowthSteady || GrowthCode("steep") != kGrowthSteep || GrowthCode("flat") != -1) Fail("resurrectgrowth: steady | steep");
    if (AmountCode("0") != 0 || AmountCode("1000") != 1000 || AmountCode("2147483647") != kMoneyMax) Fail("resurrectfee: 0 .. the money field's max");
    if (AmountCode("2147483648") != -1 || AmountCode("01") != -1 || AmountCode("-1") != -1 || AmountCode("") != -1 || AmountCode("1e3") != -1
        || AmountCode("12345678901") != -1 || AmountCode(" 5") != -1) Fail("resurrectfee: anything else is refused");
    if (!OptionValueOk("resurrect", "off") || !OptionValueOk("resurrectfee", "250") || !OptionValueOk("resurrectgrowth", "steady")) Fail("legal values pass");
    if (OptionValueOk("resurrect", "yes") || OptionValueOk("resurrectfee", "x") || OptionValueOk("resurrectgrowth", "") || OptionValueOk("resurrectcost", "1")) Fail("illegal values and other keys are refused");
    if (!IsOptionKey("resurrectfee") || IsOptionKey("recruitmult")) Fail("IsOptionKey");
    if (kDefaultGrowth != kGrowthSteep || kDefaultAmount != 1000) Fail("the defaults: steep, 1000");
    const std::string plugin = T313ReadFile("../coop-plugin/store.cpp"), server = T313ReadFile("../coop-store/store_main.cpp"), rs = T313ReadFile("../coop-plugin/resurrect.cpp");
    if (plugin.empty() || server.empty() || rs.empty()) { Fail("could not read store.cpp, store_main.cpp or resurrect.cpp"); return; }
    const std::string ov = T313ReadFile("../common/optionvalue.h");   /* T-1202 fold: OptionValueOk's body is the shared check */
    if (ov.find("if (swfee::IsOptionKey(key)) return swfee::OptionValueOk(key, value);") == std::string::npos || server.find("return worldopt::ValueOk(key, value);") == std::string::npos
        || !worldopt::ValueOk("resurrect", "on") || !worldopt::ValueOk("resurrectfee", "250") || worldopt::ValueOk("resurrectgrowth", "flat"))
        Fail("the world server's OptionValueOk does not accept the resurrection keys");
    if (plugin.find("if (coop::ResurrectOption(ok2, ov)) continue;") == std::string::npos || plugin.find("coop::ResurrectOptionsMapEnd(gpComplete && !rcRefusal);") == std::string::npos)
        Fail("the game's OPTIONS drain does not hand the resurrection keys to resurrect.cpp");
    if (rs.find("int g_optOn = 0,") == std::string::npos) Fail("resurrection is not off until the host's map says on");
}
void t_t556_back_rows()
{
    using namespace swfallen;
    Book bk; Fallen out; Pend pd;
    Fallen ana = T556Snap(10, "ana"); ana.unique = 0; ana.handSerial = 410;
    Fallen ben = T556Snap(11, "ben"); ben.unique = 0; ben.handSerial = 411;
    BookAdd(&bk, "p", ana); BookAdd(&bk, "p", ben);
    BookTake(&bk, "p", 10, 610, &out); BookTake(&bk, "p", 11, 611, &out);
    if (!BookBack(bk, "p").empty()) Fail("a pending bring-back is not a back row yet");
    if (!BookSavedFinal(&bk, "p", 10, &pd) || BookBack(bk, "p").size() != 1 || BookPending(bk, "p").size() != 1) Fail("SAVED makes a back row");
    const BackRow r = BookBack(bk, "p")[0];
    if (r.uid != 10 || r.newSerial != 610 || r.origSerial != 410 || r.diedUid != 0 || r.name != "ana" || r.templateName != "Greenlander" || r.race != "Greenlander") Fail("the back row's fields");
    if (BookSavedFinal(&bk, "p", 10, &pd)) Fail("a second SAVED finds nothing");
    /* ben's bring-back dies before the save finishes: SAVED marks its back row dead at once */
    Fallen ben2 = T556Snap(20, "ben"); ben2.unique = 0; ben2.handSerial = 611;
    BookAdd(&bk, "p", ben2);
    if (!BookSavedFinal(&bk, "p", 11, &pd) || BookBack(bk, "p")[0].newSerial != 611 || BookBack(bk, "p")[0].diedUid != 20) Fail("a bring-back already dead at its SAVED is marked dead");
    /* ana's bring-back dies: its back row is marked by the death */
    Fallen ana2 = T556Snap(21, "ana"); ana2.unique = 0; ana2.handSerial = 610;
    BookAdd(&bk, "p", ana2);
    if (BookBack(bk, "p")[1].diedUid != 21) Fail("a death with a back row's serial marks it dead");
    Fallen other = T556Snap(22, "zed"); other.unique = 0; other.handSerial = 999;
    BookAdd(&bk, "p", other);
    if (BookBack(bk, "p")[1].diedUid != 21 || BookBack(bk, "p")[0].diedUid != 20) Fail("another death marks nothing");
    /* ALIVE of ana's second death: the loaded save predates it - the back row is alive again */
    if (!BookAlive(&bk, "p", 21, &out) || BookBack(bk, "p")[1].diedUid != 0) Fail("ALIVE of a back row's death makes it alive again");
    if (BookAlive(&bk, "p", 21, &out)) Fail("a second ALIVE finds nothing");
    /* the file, back rows included */
    const std::string text = BookFile(bk);
    if (text.find("\nB ") == std::string::npos) Fail("a back row is written as a B line");
    Book rd2; std::istringstream f(text); std::string line; int bad = 0, why = 0;
    while (std::getline(f, line)) if (!BookParseLine(line + "\r", &rd2, &why)) ++bad;
    if (bad != 0 || BookFile(rd2) != text || BookBack(rd2, "p").size() != 2 || BookBack(rd2, "p")[0].diedUid != 20) Fail("back rows read back from the file in order");
    const size_t bAt = text.find("\nB ") + 1;
    const std::string bline = text.substr(bAt, text.find('\n', bAt) - bAt);
    if (BookParseLine(bline, &rd2, &why) || why != kLineDuplicate) Fail("a B line of a serial already kept is refused");
    if (BookParseLine("B 70 zz", &rd2, &why) || why != kLineBad || BookParseLine("B 70", &rd2, &why)) Fail("a bad B line is refused");
    /* the wire */
    std::vector<char> b; std::vector<Fallen> rows; std::vector<PendWire> pw; std::vector<BackRow> back;
    EncodeTable(&b, BookList(bk, "p"), BookPending(bk, "p"), BookBack(bk, "p"));
    if (!DecodeTable(&b[0], b.size(), &rows, &pw, &back) || back.size() != 2 || back[0].newSerial != 611 || back[1].name != "ana" || back[0].diedUid != 20) Fail("a TABLE carries the back rows");
    if (!DecodeTable(&b[0], b.size(), &rows, &pw)) Fail("a TABLE decodes without keeping its back rows");
    std::vector<char> more(b); more.push_back(0);
    if (DecodeTable(&more[0], more.size(), &rows, &pw, &back)) Fail("a TABLE with trailing bytes after the back rows was accepted");
    EncodeTable(&b, std::vector<Fallen>(), std::vector<Pend>());
    b[b.size() - 4] = (char)((kBackMax + 1) & 0xFF); b[b.size() - 3] = (char)(((kBackMax + 1) >> 8) & 0xFF);
    if (DecodeTable(&b[0], b.size(), &rows, &pw, &back)) Fail("a TABLE claiming more than kBackMax back rows was accepted");
    std::vector<char> one; EncodeBack(r, &one);
    BackRow r2;
    if (!DecodeBack(&one[0], one.size(), &r2) || r2.newSerial != r.newSerial || r2.origSerial != r.origSerial || r2.name != r.name || r2.race != r.race) Fail("a back row round trip");
    for (size_t cut = 0; cut < one.size(); ++cut) if (DecodeBack(&one[0], cut, &r2)) { Fail("a truncated back row was accepted"); break; }
    one[0] = (char)(kBackVersion + 1);
    if (DecodeBack(&one[0], one.size(), &r2)) Fail("another back row version was accepted");
    one[0] = 1;   /* the form before the TAKE's time: never written by a released build, refused */
    if (DecodeBack(&one[0], one.size(), &r2)) Fail("a version-1 back row was accepted");
    /* trimming: dead rows beyond kBackDiedKeep go, oldest first; living rows stay */
    std::vector<BackRow> v;
    for (unsigned int i = 1; i <= (unsigned int)kBackDiedKeep + 10; ++i) { BackRow d; d.newSerial = 1000 + i; d.diedUid = i; BackPush(&v, d); }
    for (unsigned int i = 1; i <= 5; ++i) { BackRow a; a.newSerial = 5000 + i; BackPush(&v, a); }
    int dead = 0, alive = 0; for (size_t i = 0; i < v.size(); ++i) { if (v[i].diedUid) ++dead; else ++alive; }
    if (dead != kBackDiedKeep || alive != 5 || v[0].newSerial != 5005 || v[v.size() - 1].newSerial != 1011) Fail("dead back rows beyond the keep go, oldest first; living ones stay");
    BackRow again; again.newSerial = 5003; again.name = "again"; BackPush(&v, again);
    int n5003 = 0; for (size_t i = 0; i < v.size(); ++i) if (v[i].newSerial == 5003) ++n5003;
    if (n5003 != 1 || v[0].name != "again") Fail("a back row of the same character replaces the older one");
    /* a deleted profile's back rows go */
    if (BookProfileDeleted(&bk, "p") == 0 || !BookBack(bk, "p").empty() || BookBackRows(bk) != 0) Fail("a deleted profile's back rows go");
}
void t_t556_count_alive()
{
    using namespace swfallen;
    std::vector<BackKey> keys; std::vector<LiveChar> live;
    BackKey k;
    k.newSerial = 610; k.origSerial = 410; k.byName = 1; k.name = "ana"; k.templateName = "Greenlander"; k.race = "Greenlander"; keys.push_back(k);   /* a back row */
    k.newSerial = 611; k.origSerial = 411; k.byName = 1; k.name = "ben"; keys.push_back(k);                                                       /* a back row; ben is dead */
    k.newSerial = 612; k.origSerial = 412; k.byName = 1; k.name = "cal"; keys.push_back(k);                                                       /* a pending row */
    BackKey t; t.newSerial = 612; keys.push_back(t);                                                                                             /* cal's own TAKE too */
    live.push_back(T556Live(610, "ana", "Greenlander", "Greenlander"));
    live.push_back(T556Live(612, "cal", "Greenlander", "Greenlander"));
    live.push_back(T556Live(700, "dee", "Greenlander", "Greenlander"));   /* never brought back */
    BackCount c = CountBackAlive(keys, live);
    if (c.alive != 2 || c.bySerial != 2 || c.byName != 0) Fail("living brought-back characters count once each; a dead one and an ordinary one do not");
    live.push_back(T556Live(9611, "ben", "Greenlander", "Greenlander"));  /* ben under another serial (a load gave it a new one) */
    c = CountBackAlive(keys, live);
    if (c.alive != 3 || c.byName != 1) Fail("a key no serial matched may match by name, template and race");
    keys[1].byName = 0;
    if (CountBackAlive(keys, live).alive != 2) Fail("a key known dead never matches by name");
    keys[1].byName = 1;
    live.back().serial = 411;   /* ben's ORIGINAL character (a save that predates the death) */
    if (CountBackAlive(keys, live).alive != 2) Fail("the dead character's own serial is not the bring-back");
    live.back().serial = 9611; live.back().race = "Shek";
    if (CountBackAlive(keys, live).alive != 2) Fail("another race does not match by name");
    live.back().race = "Greenlander";
    std::vector<LiveChar> two; two.push_back(T556Live(9001, "ana", "Greenlander", "Greenlander"));
    std::vector<BackKey> twoKeys; BackKey a1 = keys[0]; a1.newSerial = 1; BackKey a2 = keys[0]; a2.newSerial = 2; twoKeys.push_back(a1); twoKeys.push_back(a2);
    c = CountBackAlive(twoKeys, two);
    if (c.alive != 1 || c.byName != 1) Fail("one living character counts once, however many keys name it");
    std::vector<LiveChar> one; one.push_back(T556Live(610, "ana", "Greenlander", "Greenlander"));
    std::vector<BackKey> both; both.push_back(keys[0]); BackKey other = keys[0]; other.newSerial = 777; both.push_back(other);
    c = CountBackAlive(both, one);
    if (c.alive != 1 || c.bySerial != 1 || c.byName != 0) Fail("a character counted by serial is not counted again by name");
    if (CountBackAlive(std::vector<BackKey>(), live).alive != 0 || CountBackAlive(keys, std::vector<LiveChar>()).alive != 0) Fail("no keys or nobody alive: A = 0");
    BackKey zero;
    std::vector<BackKey> zk(1, zero); std::vector<LiveChar> zl(1, T556Live(0, "z", "t", "r"));
    if (CountBackAlive(zk, zl).alive != 0) Fail("a serial of 0 never matches");
}
void t_t556_table_worst_fits()
{
    using namespace swfallen;
    if (!(kTableWorstBytes < (size_t)(32u << 20))) Fail("the largest TABLE does not fit ENet's 32 MiB packet");
    std::vector<Fallen> rows; std::vector<Pend> pend; std::vector<BackRow> back;
    for (unsigned int u = 1; u <= (unsigned int)kFallenKeep; ++u)
    {
        Fallen f = T556Snap(u, "n"); f.look.assign(kLookMax, 'L'); f.name = std::string(kNameMax, 'n'); f.templateName = std::string(kTextMax, 't');
        f.race = std::string(kTextMax, 'r'); f.sid = std::string(kTextMax, 's'); f.place = std::string(kTextMax, 'p');
        rows.push_back(f);
        Pend p; p.row = f; p.row.uid = 1000 + u; p.newSerial = u; pend.push_back(p);
    }
    for (int i = 0; i < kBackMax; ++i) { BackRow r; r.newSerial = (unsigned int)i + 1; r.name = std::string(kNameMax, 'n'); r.templateName = std::string(kTextMax, 't'); r.race = std::string(kTextMax, 'r'); back.push_back(r); }
    std::vector<char> b; EncodeTable(&b, rows, pend, back);
    if (b.size() > kTableWorstBytes) Fail("a TABLE at every cap is larger than kTableWorstBytes");
    std::vector<Fallen> r2; std::vector<PendWire> p2; std::vector<BackRow> b2;
    if (!DecodeTable(&b[0], b.size(), &r2, &p2, &b2) || (int)r2.size() != kFallenKeep || (int)p2.size() != kFallenKeep || (int)b2.size() != kBackMax) Fail("a TABLE at every cap round trips");
}

/* ---- T-556 effort 5: the FALLEN tab (src/common/fallentab.h) - the words of build/pages/resurrection.html ---- */
void t_t556_tab_rows()
{
    using namespace swtab;
    if (Money(0) != "c.0" || Money(999) != "c.999" || Money(1000) != "c.1,000" || Money(361000) != "c.361,000" || Money(2147483647LL) != "c.2,147,483,647")
        Fail("money is written c.1,000");
    swfallen::Fallen f = T556Snap(7, "Ruka");
    f.race = "Shek"; f.day = 41; f.place = "Squin"; f.why = swfallen::kWhyBloodLoss; f.unique = 0;
    Row r = RowFor(f);
    if (r.uid != 7 || r.cell[0] != "Ruka" || r.cell[1] != "Shek" || r.cell[2] != "Day 41" || r.cell[3] != "near Squin" || r.cell[4] != "Blood loss") Fail("a row's cells");
    f.why = swfallen::kWhyStarvation; if (RowFor(f).cell[4] != "Starvation") Fail("starvation");
    f.why = swfallen::kWhyInjuries; if (RowFor(f).cell[4] != "Injuries") Fail("injuries");
    f.why = swfallen::kWhyUnknown; if (RowFor(f).cell[4] != "Injuries") Fail("a row kept before the cause was read says injuries");
    f.day = -1; f.place.clear(); r = RowFor(f);
    if (!r.cell[2].empty() || !r.cell[3].empty()) Fail("no day and no town: empty cells");
    f.race = "?"; f.templateName = "Bonedog"; if (RowFor(f).cell[1] != "Bonedog") Fail("an unread race shows the record name");
    f.unique = 1; f.templateName = "Seto"; f.name = "Seto"; if (!RowFor(f).setoOrBigBo) Fail("Seto gets the extra sentence");
    f.templateName = "Big Bo"; f.name = "Bo"; if (!RowFor(f).setoOrBigBo) Fail("Big Bo, renamed, gets the extra sentence");
    f.unique = 0; f.templateName = "Seto"; f.name = "Seto"; if (RowFor(f).setoOrBigBo) Fail("only a named character gets the sentence");
    if (!SelectLine().empty()) Fail("a line with nothing selected");
    if (std::string(kEmptyLine) != "None of your characters has fallen.") Fail("the empty line");
    std::vector<Row> a(1, RowFor(f)), b = a;
    if (!SameRows(a, b) || RowOfUid(a, 7) != 0 || RowOfUid(a, 8) != -1 || RowOfUid(a, 0) != -1) Fail("rows compare and are found by uid");
    b[0].cell[2] = "Day 2"; if (SameRows(a, b)) Fail("a changed cell is a change");
    int w[kColumns]; ColumnWidths(1000, w);
    int sum = 0; for (int c = 0; c < kColumns; ++c) sum += w[c];
    if (sum != 1000 || std::string(ColumnHead(0)) != "NAME" || std::string(ColumnHead(4)) != "CAUSE") Fail("the columns");
    if (StripButtonWidth(140, 7, 7) != 140 || StripButtonWidth(140, 7, 8) != 122 || StripButtonWidth(140, 7, 9) != 108) Fail("nine tabs share the strip of seven");
    using swfallen::DeathWhy;
    if (DeathWhy(swfallen::kCauseMedical, 1, 50.0f, -0.1f) != swfallen::kWhyStarvation || DeathWhy(swfallen::kCauseMedical, 1, -3.0f, 100.0f) != swfallen::kWhyBloodLoss
        || DeathWhy(swfallen::kCauseMedical, 1, 40.0f, 100.0f) != swfallen::kWhyInjuries || DeathWhy(swfallen::kCauseOther, 1, -3.0f, -1.0f) != swfallen::kWhyInjuries
        || DeathWhy(swfallen::kCauseMedical, 0, -3.0f, -1.0f) != swfallen::kWhyInjuries) Fail("how a death came");
}
void t_t556_tab_price_lines()
{
    using namespace swtab;
    std::vector<std::string> none, one(1, "Tama"), two, nine;
    two.push_back("Tama"); two.push_back("Hobbs");
    nine = two; for (int i = 0; i < 7; ++i) nine.push_back("X");
    PriceLines p = PriceLinesFor(1, 1000, swfee::kGrowthSteep, two);
    if (!p.shown || p.line[0] != "PRICE c.4,000 (the host's steep rule: c.1,000 x 2 x 2)" || p.line[1] != "2 characters you brought back are alive: Tama, Hobbs.") Fail("steep, two alive");
    p = PriceLinesFor(1, 1000, swfee::kGrowthSteady, one);
    if (p.line[0] != "PRICE c.1,000 (the host's steady rule: c.1,000 x 1)" || p.line[1] != "1 character you brought back is alive: Tama.") Fail("steady, one alive");
    p = PriceLinesFor(1, 1000, swfee::kGrowthSteep, nine);
    if (p.line[0] != "PRICE c.81,000 (the host's steep rule: c.1,000 x 9 x 9)" || p.line[1] != "9 characters you brought back are alive: Tama, Hobbs and 7 more.") Fail("nine alive: two names and 7 more");
    std::vector<std::string> three(two); three.push_back("Mira");
    if (AliveNames(three) != "Tama, Hobbs, Mira") Fail("three names are all listed");
    p = PriceLinesFor(1, 1000, swfee::kGrowthSteep, none);
    if (p.line[0] != "PRICE: free" || p.line[1] != "None of the characters you brought back is alive.") Fail("none alive: free");
    p = PriceLinesFor(1, 0, swfee::kGrowthSteep, two);
    if (p.line[0] != "PRICE: free" || p.line[1] != "The host has made bringing back free.") Fail("amount 0: free");
    p = PriceLinesFor(0, 1000, swfee::kGrowthSteep, two);
    if (p.shown || !p.line[0].empty()) Fail("off: the price lines are hidden");
    if (ButtonCaption(1, 4000) != "BRING BACK  c.4,000" || ButtonCaption(1, 0) != "BRING BACK" || ButtonCaption(0, 4000) != "BRING BACK") Fail("the button");
    if (BesideLabel("Ruka") != "BRING RUKA BACK BESIDE" || MateItem("Beep", "Hub Crew") != "Beep  (Hub Crew)" || MateItem("Beep", "") != "Beep") Fail("the label and the drop-down's items");
}
void t_t556_tab_box_and_lines()
{
    using namespace swtab;
    const std::string head = "Ruka returns with all limbs and no equipment. Their gear stays with their body where they fell.";
    if (BoxText("Ruka", "Beep", 1000, swfee::kGrowthSteep, 2, 0) != "Bring Ruka back beside Beep for c.4,000? " + head
        + " The price is c.1,000 x 2 x 2 because 2 characters you brought back are alive. After this one, the next will cost c.9,000.") Fail("the box, steep");
    if (BoxText("Ruka", "Beep", 1000, swfee::kGrowthSteady, 2, 0) != "Bring Ruka back beside Beep for c.2,000? " + head
        + " The price is c.1,000 x 2 because 2 characters you brought back are alive. After this one, the next will cost c.3,000.") Fail("the box, steady");
    if (BoxText("Ruka", "Beep", 1000, swfee::kGrowthSteep, 0, 0) != "Bring Ruka back beside Beep? " + head
        + " It is free because none of the characters you brought back is alive. After this one, the next will cost c.1,000.") Fail("the box, free");
    if (BoxText("Ruka", "Beep", 0, swfee::kGrowthSteep, 3, 0) != "Bring Ruka back beside Beep? " + head) Fail("the box, amount 0");
    if (BoxText("Seto", "Beep", 1000, swfee::kGrowthSteep, 2, 1) != "Bring Seto back beside Beep for c.4,000? Seto returns with all limbs and no equipment. Their gear stays with their body where they fell."
        " The price is c.1,000 x 2 x 2 because 2 characters you brought back are alive. After this one, the next will cost c.9,000. What happened in the world because Seto died stays as it is.") Fail("the box, Seto");
    if (std::string(kBoxTitle) != "BRING BACK" || std::string(kBoxCancel) != "CANCEL" || std::string(kBoxConfirm) != "BRING BACK") Fail("the box's title and buttons");
    if (ProgressLine("Ruka") != "Bringing Ruka back..." || DoneLine("Ruka", "Beep", 4000) != "Ruka is back, beside Beep. Paid c.4,000." || DoneLine("Ruka", "Beep", 0) != "Ruka is back, beside Beep.")
        Fail("the progress and done lines");
    if (RefusalWords(kRefOff, "Ruka", "Beep", 0, 0) != "The host has turned off resurrection in this world."
        || RefusalWords(kRefShort, "Ruka", "Beep", 4000, 1000) != "You need c.4,000 to bring back Ruka. You have c.1,000."
        || RefusalWords(kRefNoMate, "Ruka", "", 0, 0) != "You need a living character for Ruka to come back beside."
        || RefusalWords(kRefEnemy, "Ruka", "Beep", 0, 0) != "Enemies are near Beep."
        || RefusalWords(kRefPriceChanged, "Ruka", "Beep", 1000, 0) != "The price is now c.1,000."
        || RefusalWords(kRefMateGone, "Ruka", "Beep", 0, 0) != "Beep can't be picked any more."
        || RefusalWords(kRefNoWorld, "Ruka", "Beep", 0, 0) != "Couldn't reach the world. Ruka was not brought back and nothing was paid."
        || RefusalWords(kRefFailed, "Ruka", "Beep", 0, 0) != "Ruka was not brought back and nothing was paid.")
        Fail("the refusals' words");
    if (RefusalOfBeside(swfallen::kBesideEnemyNear) != kRefEnemy || RefusalOfBeside(swfallen::kBesideKnockedOut) != kRefMateGone || RefusalOfBeside(swfallen::kBesideDead) != kRefMateGone)
        Fail("the road's placement refusals as the tab says them");
}
void t_t556_tab_states()
{
    using namespace swtab;
    ViewIn in; in.on = 1; in.rows = 3; in.mates = 2; in.price = 4000; in.money = 9000; in.moneyRead = 1;
    View v = ViewFor(in);
    if (!v.priceShown || v.emptyShown || v.pickShown || v.btnEnabled || v.caption != kCapSelect) Fail("nothing selected: the select line, no picker");
    in.selected = 1; v = ViewFor(in);
    if (!v.pickShown || !v.pickEnabled || !v.btnEnabled || v.caption != kCapNone) Fail("a row selected: picker and button");
    in.note = kRefEnemy; v = ViewFor(in);
    if (!v.btnEnabled || v.caption != kCapRefusal || v.refusal != kRefEnemy) Fail("the press's refusal stays on the caption line");
    in.note = kRefNone; in.money = 1000; v = ViewFor(in);
    if (v.btnEnabled || !v.pickEnabled || v.refusal != kRefShort) Fail("short money: the button greyed, the price still shown");
    in.money = 9000; in.mates = 0; v = ViewFor(in);
    if (v.btnEnabled || v.pickEnabled || v.refusal != kRefNoMate) Fail("no one to come back beside");
    in.mates = 2; in.busy = 1; v = ViewFor(in);
    if (v.btnEnabled || v.pickEnabled || v.caption != kCapBusy) Fail("while it happens: the progress line, the button greyed");
    in.busy = 0; in.on = 0; v = ViewFor(in);
    if (v.priceShown || v.btnEnabled || v.pickEnabled || !v.pickShown || v.refusal != kRefOff) Fail("off: the list shown, price lines hidden, picker and button greyed");
    in.on = 1; in.rows = 0; in.selected = 0; v = ViewFor(in);
    if (!v.emptyShown || v.caption != kCapNone || !v.priceShown) Fail("the empty list");
    if (ParseLever("open").kind != kLeverOpen || ParseLever("select 2").n != 2 || ParseLever("mate 1").kind != kLeverMate || ParseLever("press").kind != kLeverPress
        || ParseLever("confirm").kind != kLeverConfirm || ParseLever("cancel").kind != kLeverCancel || ParseLever("").kind != kLeverReport
        || ParseLever("select").kind != kLeverBad || ParseLever("select -1").kind != kLeverBad || ParseLever("open now").kind != kLeverBad || ParseLever("fly").kind != kLeverBad)
        Fail("the lever's forms");
}
void t_t556_tab_fold1()
{
    using namespace swtab;
    std::vector<int> t;
    t.push_back(20); t.push_back(40); t.push_back(50); t.push_back(50); t.push_back(40); t.push_back(48); t.push_back(10); t.push_back(42); t.push_back(36);
    std::vector<int> w = StripWidths(413, 8, t);
    const int want[9] = { 28, 48, 58, 58, 48, 56, 18, 50, 49 };
    int sum = 0; bool same = w.size() == 9;
    for (size_t i = 0; same && i < 9; ++i) { same = w[i] == want[i]; sum += w[i]; }
    if (!same || sum != 413) Fail("nine tabs whose captions do not fit equal shares: each its caption + pad, the rest shared");
    std::vector<int> two; two.push_back(20); two.push_back(30);
    w = StripWidths(413, 4, two);
    if (w.size() != 2 || w[0] != 207 || w[1] != 206) Fail("captions that fit: equal shares filling the strip");
    w = EqualWidths(413, 9);
    sum = 0; for (size_t i = 0; i < w.size(); ++i) sum += w[i];
    if (w.size() != 9 || w[0] != 46 || w[7] != 46 || w[8] != 45 || sum != 413) Fail("equal widths share the strip's remainder: 9 x 45 + 8 fills 413");
    if (!EqualWidths(413, 0).empty() || EqualWidths(0, 3).size() != 3 || EqualWidths(0, 3)[0] != 0) Fail("equal widths of nothing");
    std::vector<int> three(3, 30);
    w = StripWidths(100, 10, three);
    if (w.size() != 3 || w[0] != 33 || w[1] != 33 || w[2] != 34) Fail("captions with pad wider than the strip: the pad shrinks");
    std::vector<int> wide(2, 30);
    w = StripWidths(50, 5, wide);
    if (w.size() != 2 || w[0] != 30 || w[1] != 30) Fail("captions wider than the strip: no pad");
    if (!StripWidths(413, 8, std::vector<int>()).empty()) Fail("no tabs");
    if (PlaceCell("The Hub") != "near the Hub" || PlaceCell("Squin") != "near Squin" || PlaceCell("Theodore") != "near Theodore" || PlaceCell("") != "")
        Fail("a town named The ... reads the mid-sentence");
    if (DayOfCell("Day 41") != 41 || DayOfCell("Day 0") != 0 || DayOfCell("Day") != -1 || DayOfCell("Day x") != -1 || DayOfCell("") != -1) Fail("the day in a DIED cell");
    if (!CellLess(kDiedColumn, "Day 9", "Day 10") || CellLess(kDiedColumn, "Day 10", "Day 9") || !CellLess(kDiedColumn, "", "Day 0")
        || !CellLess(0, "Day 10", "Day 9") || CellLess(0, "b", "a")) Fail("DIED sorts by the day's number, the rest by text");
    if (DayWords(0) != "Day 0" || DayWords(-1) != "") Fail("day 0");
    /* No row mapping to test: MyGUI's MultiListBox keeps its rows in the order they were added and sorts only how it draws
       them - every public index (getIndexSelected, setIndexSelected, getSubItemNameAt) is the order of adding, which the game's
       MyGUIEngine_x64.dll converts to the drawn order inside (convertToBack). The FALLEN list's index is its row's index. */
    std::vector<int> sel(3, 0); sel[1] = 1; sel[2] = 1;
    std::vector<int> none(2, 0);
    if (StartMate(sel) != 1 || StartMate(none) != 0 || StartMate(std::vector<int>()) != -1) Fail("the drop-down starts on the character selected in the game, else the first");
    ViewIn in; in.on = 1; in.rows = 3; in.selected = 1; in.mates = 2; in.price = 4000; in.money = 1000; in.moneyRead = 1; in.boxUp = 1;
    View v = ViewFor(in);
    if (!v.btnEnabled || !v.pickEnabled || v.caption != kCapNone) Fail("money spent while the box is open: nothing said until BRING BACK");
    in.boxUp = 0; in.moneyRead = 0; in.money = 0; v = ViewFor(in);
    if (!v.btnEnabled || v.caption != kCapNone) Fail("the purse unread: the button stays, nothing said");
    if (RefusalWords(kRefPriceChanged, "Ruka", "Beep", 0, 0) != "The price is now free.") Fail("the price is now free");
    if (std::string(RefusalTag(kRefShort)) != "short" || std::string(RefusalTag(kRefNone)) != "none" || std::string(RefusalTag(kRefFailed)) != "failed"
        || std::string(RefusalTag(kRefCodes)) != "?" || std::string(RefusalTag(-1)) != "?") Fail("the refusals' tags");
    std::vector<std::string> four; four.push_back("A"); four.push_back("B"); four.push_back("C"); four.push_back("D");
    if (AliveNames(four) != "A, B and 2 more") Fail("four names: two and 2 more");
    if (ParseLever("select 2 3").kind != kLeverBad || ParseLever("report").kind != kLeverReport || ParseLever("mate 1 2").kind != kLeverBad) Fail("the lever's extra words");
}
void t_t556_count_alive_names()
{
    using namespace swfallen;
    std::vector<BackKey> keys(2);
    keys[0].newSerial = 900;   /* found by serial */
    keys[1].newSerial = 901; keys[1].byName = 1; keys[1].name = "Hobbs"; keys[1].templateName = "Greenlander"; keys[1].race = "Greenlander";
    std::vector<LiveChar> live(3);
    live[0].serial = 12; live[0].name = "Hobbs"; live[0].templateName = "Greenlander"; live[0].race = "Greenlander";
    live[1].serial = 900; live[1].name = "Tama";
    live[2].serial = 13; live[2].name = "Beep";
    std::vector<size_t> counted;
    const BackCount c = CountBackAlive(keys, live, &counted);
    if (c.alive != 2 || counted.size() != 2 || counted[0] != 1 || counted[1] != 0) Fail("who A counts, in the order counted (by serial, then by name)");
    if (CountBackAlive(keys, live).alive != 2) Fail("the count without the list");
}

void t_t556_died_marks()
{
    using namespace swfallen;
    Book bk; Fallen out; Pend pd;
    /* a pending bring-back's death marks the pending row, and the mark outlives the death row being cut from the list */
    Fallen eve = T556Snap(30, "eve"); eve.unique = 0; eve.handSerial = 430;
    BookAdd(&bk, "p", eve); BookTake(&bk, "p", 30, 630, &out);
    Fallen eve2 = T556Snap(31, "eve"); eve2.unique = 0; eve2.handSerial = 630; eve2.diedUnix = 1;
    BookAdd(&bk, "p", eve2);
    if (BookPending(bk, "p")[0].diedUid != 31) Fail("a pending bring-back's death marks its pending row");
    for (unsigned int u = 100; u < 100 + (unsigned int)kFallenKeep; ++u) { Fallen x = T556Snap(u, "x"); x.unique = 0; x.handSerial = 9000 + u; BookAdd(&bk, "p", x); }
    if (SerialDied(630, BookList(bk, "p"))) Fail("the death row was cut from the list");
    std::vector<char> b; std::vector<Fallen> rows; std::vector<PendWire> pw;
    EncodeTable(&b, BookList(bk, "p"), BookPending(bk, "p"));
    if (!DecodeTable(&b[0], b.size(), &rows, &pw) || pw.size() != 1 || pw[0].diedUid != 31) Fail("a TABLE's pending row carries its died mark");
    const std::string text = BookFile(bk);
    if (text.find(" 630/31/0 ") == std::string::npos) Fail("a P line carries the died mark");
    Book rd2; std::istringstream f(text); std::string line; int bad = 0, why = 0;
    while (std::getline(f, line)) if (!BookParseLine(line, &rd2, &why)) ++bad;
    if (bad != 0 || BookFile(rd2) != text || BookPending(rd2, "p")[0].diedUid != 31) Fail("the died mark reads back from the file");
    if (BookParseLine("P 70 5/ 00", &rd2, &why) || BookParseLine("P 70 /5 00", &rd2, &why) || BookParseLine("P 70 5/x/1 00", &rd2, &why)
        || BookParseLine("P 70 5/1 00", &rd2, &why) || BookParseLine("P 70 5/1/2/3 00", &rd2, &why) || BookParseLine("P 70 5/1/ 00", &rd2, &why)) Fail("a bad died mark is refused");
    if (!BookSavedFinal(&bk, "p", 30, &pd) || BookBack(bk, "p")[0].diedUid != 31) Fail("SAVED carries the pending row's mark to the back row, the death row long cut");
    /* ALIVE of the death clears a pending row's mark */
    Book al; Fallen fay = T556Snap(40, "fay"); fay.unique = 0; fay.handSerial = 440;
    BookAdd(&al, "p", fay); BookTake(&al, "p", 40, 640, &out);
    Fallen fay2 = T556Snap(41, "fay"); fay2.unique = 0; fay2.handSerial = 640; BookAdd(&al, "p", fay2);
    if (!BookAlive(&al, "p", 41, &out) || BookPending(al, "p")[0].diedUid != 0) Fail("ALIVE of the death clears the pending row's mark");
    /* SAVED finds the death among the PENDING rows: the brought-back character died and was itself brought back */
    Book re; Fallen gus = T556Snap(50, "gus"); gus.unique = 0; gus.handSerial = 450;
    BookAdd(&re, "p", gus); BookTake(&re, "p", 50, 650, &out);
    Pend hand; hand.row = T556Snap(51, "gus"); hand.row.unique = 0; hand.row.handSerial = 650; hand.newSerial = 750;
    re.pending["p"].push_back(hand);   /* its death, already taken (pending), with no mark on the first row */
    if (!BookSavedFinal(&re, "p", 50, &pd) || BookBack(re, "p")[0].diedUid != 51) Fail("SAVED finds the death among the pending rows");
    /* a bring-back whose new serial is 0: the first later death of the same name, template and race marks it; no other does */
    Book z; Fallen hal = T556Snap(60, "hal"); hal.unique = 0; hal.handSerial = 460;
    BookAdd(&z, "p", hal); BookTake(&z, "p", 60, 0, &out);
    Fallen notHal = T556Snap(61, "hal"); notHal.unique = 0; notHal.race = "Shek"; BookAdd(&z, "p", notHal);
    if (BookPending(z, "p")[0].diedUid != 0) Fail("a death of another race does not mark a serial-0 bring-back");
    Fallen hal2 = T556Snap(62, "hal"); hal2.unique = 0; BookAdd(&z, "p", hal2);
    Fallen hal3 = T556Snap(63, "hal"); hal3.unique = 0; BookAdd(&z, "p", hal3);
    if (BookPending(z, "p")[0].diedUid != 62) Fail("the first later death of the same name, template and race marks a serial-0 bring-back");
    BookSavedFinal(&z, "p", 60, &pd);
    Fallen hal4 = T556Snap(64, "hal"); hal4.unique = 0; BookAdd(&z, "p", hal4);
    if (BookBack(z, "p")[0].diedUid != 62) Fail("a serial-0 back row keeps its first mark");
    if (!DiesAs(hal2, 0, 60, "hal", "Greenlander", "Greenlander", 0) || DiesAs(hal, 0, 60, "hal", "Greenlander", "Greenlander", 0) || DiesAs(hal2, 0, 60, "", "Greenlander", "Greenlander", 0)
        || !DiesAs(eve2, 630, 30, "x", "y", "z", 0) || DiesAs(eve2, 631, 30, "eve", "Greenlander", "Greenlander", 0)) Fail("DiesAs: by serial when read, else by name on another row");
    if (DiesAs(hal2, 0, 60, "hal", "Greenlander", "Greenlander", hal2.diedUnix + 1) || !DiesAs(hal2, 0, 60, "hal", "Greenlander", "Greenlander", hal2.diedUnix)) Fail("DiesAs by name: only a death at or after the bring-back");
    {   /* a serial-0 bring-back is not marked by a same-named death dated before its TAKE (an old ADD arriving late) */
        Book late; Fallen ivy = T556Snap(70, "ivy"); ivy.unique = 0; ivy.handSerial = 470; ivy.diedUnix = 100;
        BookAdd(&late, "p", ivy); BookTake(&late, "p", 70, 0, &out, 500);
        if (BookPending(late, "p")[0].takenUnix != 500) Fail("the pending row keeps the TAKE's time");
        Fallen old = T556Snap(71, "ivy"); old.unique = 0; old.diedUnix = 400; BookAdd(&late, "p", old);
        if (BookPending(late, "p")[0].diedUid != 0) Fail("a same-named death before the TAKE does not mark a serial-0 bring-back");
        Fallen after = T556Snap(72, "ivy"); after.unique = 0; after.diedUnix = 600; BookAdd(&late, "p", after);
        if (BookPending(late, "p")[0].diedUid != 72) Fail("a same-named death after the TAKE marks it");
        const std::string lt = BookFile(late);
        if (lt.find(" 0/72/500 ") == std::string::npos) Fail("a P line carries the died mark and the TAKE's time");
        Book lr; std::istringstream lf(lt); std::string ll; int lbad = 0;
        while (std::getline(lf, ll)) if (!BookParseLine(ll, &lr, &why)) ++lbad;
        if (lbad != 0 || BookFile(lr) != lt || BookPending(lr, "p")[0].takenUnix != 500) Fail("the TAKE's time reads back from the file");
        BookSavedFinal(&late, "p", 70, &pd);
        if (BookBack(late, "p")[0].takenUnix != 500 || BookBack(late, "p")[0].diedUid != 72) Fail("the back row keeps the TAKE's time and the mark");
        std::vector<char> tb; Up tu; EncodeTake(&tb, 70, 0, 500);
        if (!DecodeUp(&tb[0], tb.size(), &tu) || tu.kind != kUpTake || tu.takenUnix != 500) Fail("a TAKE carries its time");
    }
    if (SerialDied(0, BookList(z, "p"))) Fail("a serial of 0 never died");
    /* trimming: dead rows go first when over kBackMax; living rows only when no dead row is left */
    std::vector<BackRow> v;
    for (int i = 0; i < 5; ++i) { BackRow d; d.newSerial = 100000 + (unsigned int)i; d.diedUid = 1 + (unsigned int)i; v.push_back(d); }
    for (int i = 0; i < kBackMax - 5; ++i) { BackRow a; a.newSerial = 200000 + (unsigned int)i; v.push_back(a); }
    int dropped = 0;
    for (int i = 0; i < 5; ++i) { BackRow a; a.newSerial = 300000 + (unsigned int)i; dropped += BackPush(&v, a); }
    int dead = 0; for (size_t i = 0; i < v.size(); ++i) if (v[i].diedUid) ++dead;
    if ((int)v.size() != kBackMax || dead != 0 || dropped != 0) Fail("over kBackMax, dead rows go before any living row");
    BackRow last; last.newSerial = 400000; dropped = BackPush(&v, last);
    if ((int)v.size() != kBackMax || dropped != 1 || v[v.size() - 1].newSerial != 200000 + (unsigned int)(kBackMax - 7)) Fail("with no dead row left, the oldest living row goes, counted");
    /* a B line past kBackMax has its own reason */
    Book full; std::vector<char> by;
    for (int i = 0; i <= kBackMax; ++i) { BackRow r; r.newSerial = 1 + (unsigned int)i; EncodeBack(r, &by); BookParseLine("B 70 " + Hex(&by[0], by.size()), &full, &why); }
    if (why != kLineOverBack || (int)BookBack(full, "p").size() != kBackMax) Fail("a back row past kBackMax is refused as kLineOverBack");
    /* the game's side, read: known-dead pending rows never match by name; money unread refuses only a priced bring-back; 40 pending refuse */
    const std::string rs = T313ReadFile("../coop-plugin/resurrect.cpp");
    if (rs.find("k.byName = (g_pendWire[i].diedUid == 0 && !DeathKnown(g_pendWire[i].newSerial)) ? 1 : 0;") == std::string::npos) Fail("a known-dead pending row may match by name");
    if (rs.find("if (moneyRead != 1 && fee.price > 0)") == std::string::npos) Fail("an unread purse refuses a free bring-back");
    if (rs.find("PendingNow() >= swfallen::kFallenKeep") == std::string::npos) Fail("a 41st pending bring-back is not refused");
    if (rs.find("const int kRing = 256;") == std::string::npos) Fail("the noted-deaths ring holds fewer than a wiped squad");
}

/* ---- T-546 step 3 (t546a): the player-faction membership table and TEAM (60) - src/common/teamwire.h ---- */
std::vector<char> T546Snap(const char* s) { return std::vector<char>(s, s + std::strlen(s)); }
std::map<std::string, unsigned> T546Slots()
{
    std::map<std::string, unsigned> m;
    m["alex.1"] = 0; m["sam.1"] = 1; m["kim.1"] = 2; m["jo.1"] = 3; m["max.1"] = 4;
    return m;
}
/* the book written to the file and read back gives the same file text */
bool T546FileRoundTrip(const swteam::Book& b)
{
    const std::string f = swteam::BookFile(b);
    swteam::Book r; std::istringstream is(f); std::string line; int bad = 0;
    while (std::getline(is, line)) if (!line.empty() && !swteam::BookParseLine(line, &r)) ++bad;
    return bad == 0 && swteam::BookFile(r) == f;
}
void t_t546_team_wire_round_trip()
{
    using namespace swteam;
    std::vector<char> b; Up u;
    EncodeInvite(&b, 3, "The Rusty Blades");
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpInvite || u.slot != 3 || u.name != "The Rusty Blades") Fail("INVITE round trip");
    const std::vector<char> snap = T546Snap("om3g-standing-bytes");
    if (!EncodeAnswer(&b, true, 104, snap) || !DecodeUp(&b[0], b.size(), &u) || u.kind != kUpAnswer || u.accept != 1 || u.snapRows != 104 || u.snap != snap) Fail("ACCEPT round trip");
    EncodeAnswer(&b, false, 0, std::vector<char>());
    if (!DecodeUp(&b[0], b.size(), &u) || u.accept != 0 || !u.snap.empty()) Fail("DECLINE round trip");
    EncodeLeave(&b); if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpLeave) Fail("LEAVE round trip");
    EncodeRemove(&b, 2); if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpRemove || u.slot != 2) Fail("REMOVE round trip");
    EncodeDisband(&b); if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpDisband) Fail("DISBAND round trip");
    EncodeRestoreDone(&b, 7); if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpRestoreDone || u.no != 7) Fail("RESTORE_DONE round trip");
    /* refusals: a trailing byte, a cut message, an unknown kind, an accept byte that is not 0 or 1, a down kind sent up */
    EncodeRemove(&b, 2); b.push_back(0); if (DecodeUp(&b[0], b.size(), &u)) Fail("a trailing byte decoded");
    EncodeInvite(&b, 3, "Name"); if (DecodeUp(&b[0], b.size() - 1, &u)) Fail("a cut INVITE decoded");
    b.assign(1, (char)99); if (DecodeUp(&b[0], b.size(), &u)) Fail("an unknown kind decoded");
    EncodeAnswer(&b, true, 1, snap); b[1] = 2; if (DecodeUp(&b[0], b.size(), &u)) Fail("an accept byte of 2 decoded");
    EncodeInvited(&b, 0, "X", 60); if (DecodeUp(&b[0], b.size(), &u)) Fail("a down message decoded as up");
    if (DecodeUp(0, 0, &u)) Fail("nothing decoded");
    /* down */
    Down d;
    std::vector<WireTeam> t(1); t[0].no = 4; t[0].founderSlot = 0; t[0].name = "The Rusty Blades"; t[0].memberSlots.push_back(1); t[0].memberSlots.push_back(2);
    EncodeTable(&b, t);
    if (!DecodeDown(&b[0], b.size(), &d) || d.kind != kDnTable || d.table.size() != 1 || d.table[0].no != 4 || d.table[0].memberSlots.size() != 2 || d.table[0].memberSlots[1] != 2
        || TableText(d.table) != "team 4 'The Rusty Blades' founder=s0 members=s1,s2") Fail("TABLE round trip: " + TableText(d.table));
    EncodeTable(&b, std::vector<WireTeam>());
    if (!DecodeDown(&b[0], b.size(), &d) || !d.table.empty() || TableText(d.table) != "no teams") Fail("empty TABLE");
    EncodeInvited(&b, 0, "The Rusty Blades", 60);
    if (!DecodeDown(&b[0], b.size(), &d) || d.kind != kDnInvited || d.fromSlot != 0 || d.teamName != "The Rusty Blades" || d.secondsLeft != 60) Fail("INVITED round trip");
    EncodeNotice(&b, kEvInvite, kRefInTeam, 0, 2, "");
    if (!DecodeDown(&b[0], b.size(), &d) || d.kind != kDnNotice || d.event != (unsigned)kEvInvite || d.result != (unsigned)kRefInTeam || d.subjectSlot != 2) Fail("NOTICE round trip");
    Owed o; o.no = 9; o.why = kWhyRemoved; o.away = 1; o.teamName = "T"; o.snapRows = 104; o.snap = snap;
    EncodeRestore(&b, o);
    if (!DecodeDown(&b[0], b.size(), &d) || d.kind != kDnRestore || d.no != 9 || d.why != (unsigned)kWhyRemoved || d.away != 1 || d.snapRows != 104 || d.snap != snap) Fail("RESTORE round trip");
    b.push_back(0); if (DecodeDown(&b[0], b.size(), &d)) Fail("a RESTORE with a trailing byte decoded");
    EncodeLeave(&b); if (DecodeDown(&b[0], b.size(), &d)) Fail("an up message decoded as down");
    /* the wire number is the store's */
    if (kMsgTeam != 60) Fail("TEAM is not 60");
    if (std::string(ResultName(kRefFounderLeaves)) != "founder-has-members" || std::string(EventName(kEvExpired)) != "expired" || std::string(ResultName(99)) != "?") Fail("names");
    if (TableText(t).find('\n') != std::string::npos) Fail("a table line breaks");
    t[0].name = "Bad'Name\n"; if (TableText(t) != "team 4 'Bad_Name_' founder=s0 members=s1,s2") Fail("a name's quote and line break are not cleaned: " + TableText(t));
}
/* three members (the founder and two who accepted), the file kept whole */
void t_t546_team_three_members()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0;
    if (InviteDecide(&b, "alex.1", "sam.1", true, true, "The Rusty Blades", 100.0) != kOk) Fail("alex invites sam");
    if (!b.teams.empty()) Fail("an invitation made a team");
    if (AnswerDecide(&b, "sam.1", true, 104, T546Snap("sam-before"), 1000, 101.0, &v, &no) != kOk || no != 1 || v.from != "alex.1") Fail("sam accepts");
    if (InviteDecide(&b, "alex.1", "kim.1", true, true, "ignored - the team's name is used", 102.0) != kOk) Fail("alex invites kim");
    if (b.invites[0].name != "The Rusty Blades") Fail("the second invitation does not carry the team's name");
    if (AnswerDecide(&b, "kim.1", true, 98, T546Snap("kim-before"), 1001, 103.0, &v, &no) != kOk || no != 1) Fail("kim accepts");
    if (TableText(ToWire(b, T546Slots())) != "team 1 'The Rusty Blades' founder=s0 members=s1,s2") Fail("three members: " + TableText(ToWire(b, T546Slots())));
    if (TeamOf(b, "kim.1") != 0 || !IsFounder(b, "alex.1") || IsFounder(b, "sam.1") || TeamOf(b, "jo.1") != -1) Fail("who is in which team");
    if (b.teams[0].members[0].snap != T546Snap("sam-before") || b.teams[0].members[1].snapRows != 98 || b.teams[0].members[1].joinedAt != 1001) Fail("a member's pre-join standing was not kept as sent");
    if (!T546FileRoundTrip(b)) Fail("three members: the file does not read back");
    swteam::Book r; std::istringstream is(BookFile(b)); std::string line;
    while (std::getline(is, line)) BookParseLine(line, &r);
    if (TableText(ToWire(r, T546Slots())) != "team 1 'The Rusty Blades' founder=s0 members=s1,s2" || r.teams[0].members[1].snap != T546Snap("kim-before") || r.nextTeam != 2) Fail("the file read back differs");
    /* a second team, and a member who leaves keeps nothing in the team but is owed its standing back */
    if (InviteDecide(&b, "jo.1", "max.1", true, true, "Iron Wolves", 104.0) != kOk || AnswerDecide(&b, "max.1", true, 90, T546Snap("max"), 1002, 105.0, &v, &no) != kOk || no != 2) Fail("a second team");
    Owed o;
    if (LeaveDecide(&b, "sam.1", 1003, &o) != kOk || o.id != "sam.1" || o.why != kWhyLeft || o.away != 0 || o.snap != T546Snap("sam-before") || o.teamName != "The Rusty Blades") Fail("sam leaves");
    if (TableText(ToWire(b, T546Slots())) != "team 1 'The Rusty Blades' founder=s0 members=s2; team 2 'Iron Wolves' founder=s3 members=s4") Fail("after sam left: " + TableText(ToWire(b, T546Slots())));
    TeamEnd end;
    if (LeaveDecide(&b, "kim.1", 1004, &o, 0, 0, 0, 0, &end) != kOk || TableText(ToWire(b, T546Slots())) != "team 2 'Iron Wolves' founder=s3 members=s4") Fail("the last member leaves: the team ends - " + TableText(ToWire(b, T546Slots())));
    if (!end.ended || end.founder != "alex.1" || end.teamName != "The Rusty Blades" || end.teamNo != 1 || end.row.no != 0 || end.rowSkipped || TeamOf(b, "alex.1") != -1)
        Fail("the team ended with its last member: the founder in no team, no row (the founder made the team)");
    if (!T546FileRoundTrip(b)) Fail("after the team ended: the file does not read back");
    if (b.owed.size() != 2 || b.owed[0].no == b.owed[1].no) Fail("two restore rows with their own numbers");
    o = Owed();
    if (LeaveDecide(&b, "alex.1", 1005, &o) != kRefNotInTeam || o.no != 0 || b.owed.size() != 2) Fail("the founder of the ended team is in no team");
    Book lone;   /* a founder alone kept in a table from before a team ended with its last member: still leaves, the team ends */
    if (!BookParseLine("T 7 61 41", &lone) || LeaveDecide(&lone, "a", 1006, &o) != kOk || !lone.teams.empty()) Fail("a lone founder from an older table leaves");
}
void t_t546_team_refusals()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; Owed o;
    InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&b, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no);
    InviteDecide(&b, "jo.1", "max.1", true, true, "Wolves", 0.0); AnswerDecide(&b, "max.1", true, 1, T546Snap("m"), 1, 1.0, &v, &no);
    if (InviteDecide(&b, "alex.1", "max.1", true, true, "", 2.0) != kRefInTeam) Fail("an invitation to a member of another team");
    if (InviteDecide(&b, "alex.1", "jo.1", true, true, "", 2.0) != kRefInTeam) Fail("an invitation to another team's founder");
    if (InviteDecide(&b, "sam.1", "kim.1", true, true, "", 2.0) != kRefNotFounder) Fail("a member invites");
    if (InviteDecide(&b, "alex.1", "alex.1", true, true, "", 2.0) != kRefSelf) Fail("an invitation to oneself");
    if (InviteDecide(&b, "alex.1", "kim.1", true, false, "", 2.0) != kRefNotOnline) Fail("an invitation to a player who is not connected");
    if (InviteDecide(&b, "alex.1", "", false, false, "", 2.0) != kRefUnknownPlayer) Fail("an invitation to a slot nobody holds");
    if (InviteDecide(&b, "alex.1", "kim.1", true, true, "", 2.0) != kOk) Fail("alex invites kim");
    if (InviteDecide(&b, "jo.1", "kim.1", true, true, "", 3.0) != kRefInvitePending) Fail("a second invitation while one waits");
    if (LeaveDecide(&b, "alex.1", 1, &o) != kRefFounderLeaves) Fail("the founder leaves with members");
    if (LeaveDecide(&b, "kim.1", 1, &o) != kRefNotInTeam) Fail("a player in no team leaves");
    if (AnswerDecide(&b, "jo.1", true, 1, T546Snap("j"), 1, 4.0, &v, &no) != kRefNoInvite) Fail("an answer with no invitation");
    if (AnswerDecide(&b, "kim.1", true, 0, std::vector<char>(), 1, 4.0, &v, &no) != kRefNoSnapshot || InviteTo(b, "kim.1") < 0) Fail("an accept with no standing: refused and the invitation kept");
    if (RemoveDecide(&b, "alex.1", "max.1", true, true, 1, &o) != kRefNotMember) Fail("removing another team's member");
    if (RemoveDecide(&b, "sam.1", "alex.1", true, true, 1, &o) != kRefNotFounder) Fail("a member removes");
    if (RemoveDecide(&b, "kim.1", "sam.1", true, true, 1, &o) != kRefNotInTeam) Fail("a player in no team removes");
    if (RemoveDecide(&b, "alex.1", "", false, false, 1, &o) != kRefUnknownPlayer) Fail("removing a slot nobody holds");
    if (RemoveDecide(&b, "alex.1", "alex.1", true, true, 1, &o) != kRefSelf) Fail("the founder removes themself");
    std::vector<Owed> made; std::string name;
    if (DisbandDecide(&b, "sam.1", std::vector<std::string>(), 1, &made, &name) != kRefNotFounder || !made.empty()) Fail("a member disbands");
    if (DisbandDecide(&b, "kim.1", std::vector<std::string>(), 1, &made, &name) != kRefNotInTeam) Fail("a player in no team disbands");
    /* an accept after the inviter became another founder's member */
    Book c;
    InviteDecide(&c, "kim.1", "jo.1", true, true, "Kims", 0.0);
    InviteDecide(&c, "alex.1", "kim.1", true, true, "Blades", 0.0);
    AnswerDecide(&c, "kim.1", true, 1, T546Snap("k"), 1, 1.0, &v, &no);
    if (AnswerDecide(&c, "jo.1", true, 1, T546Snap("j"), 1, 2.0, &v, &no) != kRefNotFounder || TeamOf(c, "jo.1") != -1 || InviteTo(c, "jo.1") >= 0) Fail("an accept of a member's old invitation");
    /* a full team */
    Book f;
    for (unsigned i = 0; i < kMaxMembers; ++i)
    {
        const std::string id = "p" + I((int)i);
        if (InviteDecide(&f, "alex.1", id, true, true, "Big", 0.0) != kOk || AnswerDecide(&f, id, true, 1, T546Snap("x"), 1, 1.0, &v, &no) != kOk) { Fail("filling the team"); return; }
    }
    if (InviteDecide(&f, "alex.1", "late.1", true, true, "Big", 2.0) != kRefTeamFull) Fail("an invitation into a full team");
    /* the file refuses broken lines */
    Book g;
    if (BookParseLine("M 1 7a 1 1 61", &g)) Fail("a member line before its team line");
    if (BookParseLine("T 1 zz 41", &g) || BookParseLine("O 1 61 9 0 41 1 - 1", &g) || BookParseLine("X", &g) || BookParseLine("N 0 1", &g)) Fail("a broken line was taken");
    if (!BookParseLine("T 1 61 41\r", &g) || BookParseLine("T 2 61 42", &g)) Fail("a team line, then a second team for the same founder");
}
/* a member removed while away: the row waits for that player's next join and is dropped when its game answers */
void t_t546_team_offline_removal()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; Owed o;
    InviteDecide(&b, "alex.1", "kim.1", true, true, "Blades", 0.0);
    AnswerDecide(&b, "kim.1", true, 98, T546Snap("kim-before"), 50, 1.0, &v, &no);
    TeamEnd end;
    if (RemoveDecide(&b, "alex.1", "kim.1", true, false, 60, &o, 0, 0, 0, &end) != kOk) Fail("removing a member who is away");
    if (o.away != 1 || o.why != kWhyRemoved || o.snap != T546Snap("kim-before") || o.snapRows != 98 || o.teamName != "Blades" || o.madeAt != 60) Fail("the while-away row");
    if (TeamOf(b, "kim.1") != -1 || TeamOf(b, "alex.1") != -1 || !b.teams.empty() || !end.ended || end.founder != "alex.1" || end.row.no != 0 || b.owed.size() != 1)
        Fail("the last member removed while away: the team ended, the founder in no team");
    if (!T546FileRoundTrip(b)) Fail("the owed row does not survive the file");
    Book r; std::istringstream is(BookFile(b)); std::string line;
    while (std::getline(is, line)) BookParseLine(line, &r);
    if (r.owed.size() != 1 || r.owed[0].away != 1 || r.owed[0].snap != T546Snap("kim-before") || r.owed[0].id != "kim.1") Fail("the owed row read back");
    if (RestoreDone(&r, "alex.1", r.owed[0].no)) Fail("another player answered the row");
    if (RestoreDone(&r, "kim.1", r.owed[0].no + 1)) Fail("a wrong number answered the row");
    if (!RestoreDone(&r, "kim.1", o.no) || !r.owed.empty()) Fail("the answer drops the row");
    if (RestoreDone(&r, "kim.1", o.no)) Fail("a second answer found a row");
    /* the numbers keep rising across the file */
    InviteDecide(&r, "alex.1", "sam.1", true, true, "", 2.0);
    AnswerDecide(&r, "sam.1", true, 1, T546Snap("s"), 1, 3.0, &v, &no);
    Owed o2; LeaveDecide(&r, "sam.1", 70, &o2);
    if (o2.no <= o.no) Fail("a restore number was reused");
}
void t_t546_team_expiry_and_disband()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; std::vector<Invite> gone;
    InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 10.0);
    ExpireInvites(&b, 10.0 + kInviteSeconds - 0.1, &gone);
    if (!gone.empty() || InviteTo(b, "sam.1") < 0) Fail("an invitation ended early");
    ExpireInvites(&b, 10.0 + kInviteSeconds, &gone);
    if (gone.size() != 1 || gone[0].from != "alex.1" || InviteTo(b, "sam.1") >= 0) Fail("an invitation did not end at 60 s");
    if (AnswerDecide(&b, "sam.1", true, 1, T546Snap("s"), 1, 71.0, &v, &no) != kRefNoInvite) Fail("an answer after the end");
    InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 100.0);
    if (AnswerDecide(&b, "sam.1", true, 1, T546Snap("s"), 1, 100.0 + kInviteSeconds, &v, &no) != kRefNoInvite) Fail("an answer at the end itself");
    /* a connection closing ends the invitations from and to that player */
    gone.clear(); ExpireInvites(&b, 200.0, &gone);
    InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 200.0);
    InviteDecide(&b, "jo.1", "kim.1", true, true, "Wolves", 200.0);
    gone.clear(); DropInvitesOf(&b, "sam.1", &gone);
    if (gone.size() != 1 || InviteTo(b, "kim.1") < 0) Fail("a closed connection ended the wrong invitations");
    /* disband: a restore row for every member, the away flag from who is connected, the founder's invitations gone */
    Book d;
    InviteDecide(&d, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&d, "sam.1", true, 3, T546Snap("s"), 1, 1.0, &v, &no);
    InviteDecide(&d, "alex.1", "kim.1", true, true, "Blades", 0.0); AnswerDecide(&d, "kim.1", true, 4, T546Snap("k"), 1, 1.0, &v, &no);
    InviteDecide(&d, "alex.1", "jo.1", true, true, "Blades", 2.0);
    std::vector<std::string> online; online.push_back("alex.1"); online.push_back("sam.1");
    std::vector<Owed> made; std::string name;
    if (DisbandDecide(&d, "alex.1", online, 5, &made, &name) != kOk || name != "Blades") Fail("disband");
    if (made.size() != 2 || made[0].id != "sam.1" || made[0].away != 0 || made[1].id != "kim.1" || made[1].away != 1 || made[1].snap != T546Snap("k") || made[0].why != kWhyDisbanded)
        Fail("the disband's restore rows");
    if (!d.teams.empty() || InviteTo(d, "jo.1") >= 0 || d.owed.size() != 2) Fail("after the disband");
    if (TableText(ToWire(d, T546Slots())) != "no teams") Fail("the table after the disband");
    /* the disbanded founder may found again */
    if (InviteDecide(&d, "alex.1", "sam.1", true, true, "Blades", 10.0) != kOk || AnswerDecide(&d, "sam.1", true, 3, T546Snap("s"), 1, 11.0, &v, &no) != kOk || no != 2) Fail("found again after a disband");
}

/* an ACCEPT refused: in a team meanwhile, the team full, too many teams; a decline; the join's name is the team's own */
void t_t546_team_accept_refusals()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; std::string name;
    InviteDecide(&b, "alex.1", "kim.1", true, true, "Blades", 0.0);
    InviteDecide(&b, "kim.1", "jo.1", true, true, "Kims", 0.0);
    if (AnswerDecide(&b, "jo.1", true, 1, T546Snap("j"), 1, 1.0, &v, &no, &name) != kOk || name != "Kims") Fail("jo joins kim");
    if (AnswerDecide(&b, "kim.1", true, 1, T546Snap("k"), 1, 2.0, &v, &no) != kRefInTeam || InviteTo(b, "kim.1") >= 0) Fail("an accept by a player who founded a team meanwhile");
    Book f;
    for (unsigned i = 0; i + 1 < kMaxMembers; ++i)
    {
        const std::string id = "p" + I((int)i);
        if (InviteDecide(&f, "alex.1", id, true, true, "Big", 0.0) != kOk || AnswerDecide(&f, id, true, 1, T546Snap("x"), 1, 1.0, &v, &no) != kOk) { Fail("filling the team"); return; }
    }
    InviteDecide(&f, "alex.1", "a.1", true, true, "Big", 5.0); InviteDecide(&f, "alex.1", "b.1", true, true, "Big", 5.0);
    if (AnswerDecide(&f, "a.1", true, 1, T546Snap("a"), 1, 6.0, &v, &no) != kOk) Fail("the last place taken");
    if (AnswerDecide(&f, "b.1", true, 1, T546Snap("b"), 1, 6.0, &v, &no) != kRefTeamFull || InviteTo(f, "b.1") >= 0 || TeamOf(f, "b.1") >= 0) Fail("an accept into a team that filled meanwhile");
    Book m;
    InviteDecide(&m, "x.1", "y.1", true, true, "X", 0.0);
    for (unsigned i = 0; i < kMaxTeams; ++i)
    {
        const std::string fo = "f" + I((int)i), me = "g" + I((int)i);
        if (InviteDecide(&m, fo, me, true, true, "T", 0.0) != kOk || AnswerDecide(&m, me, true, 1, T546Snap("g"), 1, 1.0, &v, &no) != kOk) { Fail("making the teams"); return; }
    }
    if (AnswerDecide(&m, "y.1", true, 1, T546Snap("y"), 1, 2.0, &v, &no) != kRefTooMany || m.teams.size() != kMaxTeams || InviteTo(m, "y.1") >= 0) Fail("an accept that would make a team past the limit");
    if (InviteDecide(&m, "x.1", "z.1", true, true, "X", 3.0) != kRefTooMany) Fail("an invitation that would make a team past the limit");
    Book d;
    InviteDecide(&d, "alex.1", "sam.1", true, true, "Blades", 0.0);
    if (AnswerDecide(&d, "sam.1", false, 0, std::vector<char>(), 1, 1.0, &v, &no) != kOk || InviteTo(d, "sam.1") >= 0 || v.from != "alex.1" || !d.teams.empty()) Fail("a decline");
    if (AnswerDecide(&d, "sam.1", false, 0, std::vector<char>(), 1, 2.0, &v, &no) != kRefNoInvite) Fail("a second decline");
    Book n;
    InviteDecide(&n, "alex.1", "sam.1", true, true, "Old Name", 0.0); InviteDecide(&n, "alex.1", "kim.1", true, true, "New Name", 0.0);
    if (AnswerDecide(&n, "kim.1", true, 1, T546Snap("k"), 1, 1.0, &v, &no, &name) != kOk || name != "New Name") Fail("the first accept names the team");
    if (AnswerDecide(&n, "sam.1", true, 1, T546Snap("s"), 1, 2.0, &v, &no, &name) != kOk || name != "New Name" || v.name != "Old Name") Fail("the join's name is not the team's own: " + name);
    if (AnswerRefusalEndsInvite(kRefNoSnapshot) || AnswerRefusalEndsInvite(kRefNotSaved) || AnswerRefusalEndsInvite(kOk) || !AnswerRefusalEndsInvite(kRefNoInvite)
        || !AnswerRefusalEndsInvite(kRefOwedFull) || !AnswerRefusalEndsInvite(kRefTeamFull)) Fail("which refusals end the invitation");
    if (std::string(ResultName(kRefOwedFull)) != "restore-backlog" || std::string(ResultName(kRefNotSaved)) != "not-saved") Fail("the new refusal names");
}
/* the limits on the wire: a standing over 64 KB, a RESTORE over it or cut, a TABLE with too many teams or members, a long name; ASK */
void t_t546_team_size_limits()
{
    using namespace swteam;
    if (kMaxSnapshot != 64u * 1024u) Fail("the standing limit is not 64 KB");
    const std::vector<char> big(kMaxSnapshot + 1, 'x'), edge(kMaxSnapshot, 'x');
    std::vector<char> b; Up u; Down d;
    if (EncodeAnswer(&b, true, 1, big)) Fail("an over-limit standing was encoded");
    if (!EncodeAnswer(&b, true, 1, edge) || !DecodeUp(&b[0], b.size(), &u) || u.snap.size() != kMaxSnapshot) Fail("a standing at the limit");
    b.clear(); PutU8(&b, kUpAnswer); PutU8(&b, 1); PutU32(&b, 1); PutBytes(&b, &big[0], big.size());
    if (DecodeUp(&b[0], b.size(), &u)) Fail("an over-limit ANSWER decoded");
    Book k; Invite v; unsigned no = 0;
    InviteDecide(&k, "alex.1", "sam.1", true, true, "Blades", 0.0);
    if (AnswerDecide(&k, "sam.1", true, 1, big, 1, 1.0, &v, &no) != kRefNoSnapshot || InviteTo(k, "sam.1") < 0 || !k.teams.empty()) Fail("an over-limit standing taken");
    Owed o; o.no = 1; o.why = kWhyLeft; o.snap = big;
    EncodeRestore(&b, o); if (DecodeDown(&b[0], b.size(), &d)) Fail("an over-limit RESTORE decoded");
    o.snap = edge; EncodeRestore(&b, o);
    if (!DecodeDown(&b[0], b.size(), &d) || d.snap.size() != kMaxSnapshot) Fail("a RESTORE at the limit");
    if (DecodeDown(&b[0], b.size() - 1, &d)) Fail("a cut RESTORE decoded");
    b.clear(); PutU8(&b, kDnTable); PutU32(&b, kMaxTeams + 1);
    if (DecodeDown(&b[0], b.size(), &d)) Fail("a TABLE of too many teams decoded");
    std::vector<WireTeam> t(1); t[0].no = 1; t[0].memberSlots.assign(kMaxMembers + 1, 1u);
    EncodeTable(&b, t); if (DecodeDown(&b[0], b.size(), &d)) Fail("a TABLE team of too many members decoded");
    EncodeTable(&b, std::vector<WireTeam>()); if (DecodeDown(&b[0], b.size() - 1, &d)) Fail("a cut TABLE decoded");
    EncodeNotice(&b, kEvJoined, kOk, 0, 1, std::string(kMaxName + 1, 'n')); if (DecodeDown(&b[0], b.size(), &d)) Fail("a NOTICE with a name over the limit decoded");
    EncodeNotice(&b, kEvJoined, kOk, 0, 1, std::string(kMaxName, 'n')); if (!DecodeDown(&b[0], b.size(), &d)) Fail("a NOTICE with a name at the limit");
    EncodeAsk(&b);
    if (!DecodeUp(&b[0], b.size(), &u) || u.kind != kUpAsk) Fail("ASK round trip");
    b.push_back(0); if (DecodeUp(&b[0], b.size(), &u)) Fail("an ASK with a trailing byte decoded");
}
/* at most kMaxOwedEach restore rows per player, kMaxOwed in the world: the step that would make one more is refused, no number used */
void t_t546_team_owed_limit()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; Owed o;
    for (unsigned i = 0; i < kMaxOwedEach; ++i)
    {
        if (InviteDecide(&b, "alex.1", "kim.1", true, true, "Blades", i * 10.0) != kOk || AnswerDecide(&b, "kim.1", true, 1, T546Snap("k"), 1, i * 10.0 + 1, &v, &no) != kOk
            || LeaveDecide(&b, "kim.1", 1, &o) != kOk) { Fail("kim joins and leaves"); return; }
    }
    if (OwedCount(b, "kim.1") != kMaxOwedEach) Fail("kim's rows");
    if (InviteDecide(&b, "alex.1", "kim.1", true, true, "", 100.0) != kRefOwedFull || InviteTo(b, "kim.1") >= 0) Fail("an invitation to a player whose rows are full");
    Book c = b; c.owed.pop_back();
    if (InviteDecide(&c, "alex.1", "kim.1", true, true, "", 100.0) != kOk) Fail("an invitation below the limit");
    c.owed.push_back(b.owed.back());
    if (AnswerDecide(&c, "kim.1", true, 1, T546Snap("k"), 1, 101.0, &v, &no) != kRefOwedFull || InviteTo(c, "kim.1") >= 0 || TeamOf(c, "kim.1") >= 0) Fail("an accept with the rows full");
    Book r = b;   /* each leave above ended the team with its last member: alex founds one again with kim in it */
    { Team t; t.no = r.nextTeam++; t.founder = "alex.1"; t.name = "Blades"; t.members.push_back(Member()); t.members.back().id = "kim.1"; t.members.back().snap = T546Snap("k"); r.teams.push_back(t); }
    const unsigned next = r.nextOwed; const size_t rows = r.owed.size();
    if (LeaveDecide(&r, "kim.1", 1, &o) != kRefOwedFull) Fail("a leave with the rows full");
    if (RemoveDecide(&r, "alex.1", "kim.1", true, true, 1, &o) != kRefOwedFull) Fail("a removal with the rows full");
    std::vector<Owed> made; std::string name;
    if (DisbandDecide(&r, "alex.1", std::vector<std::string>(), 1, &made, &name) != kRefOwedFull || !made.empty()) Fail("a disband with a member's rows full");
    if (r.nextOwed != next || r.owed.size() != rows || TeamOf(r, "kim.1") != 0 || r.teams.size() != 1) Fail("a refused departure changed the table");
    Book w;
    InviteDecide(&w, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&w, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no);
    for (unsigned i = 0; i < kMaxOwed; ++i) { Owed x; x.no = 1000 + i; x.id = "g" + I((int)(i / kMaxOwedEach)); x.why = kWhyLeft; w.owed.push_back(x); }
    const unsigned wn = w.nextOwed;
    if (LeaveDecide(&w, "sam.1", 1, &o) != kRefOwedFull || w.nextOwed != wn || TeamOf(w, "sam.1") != 0) Fail("a departure past the world's limit");
    if (!RestoreDone(&w, "g0", 1000) || LeaveDecide(&w, "sam.1", 1, &o) != kOk || o.no != wn) Fail("a row answered makes room again");
}
/* a founder's team ends (a disband with no members, a lone founder leaving - a team kept from a table older than the rule that
   ends a team with its last member): every invitation they had open ends with it */
void t_t546_team_founder_ends()
{
    using namespace swteam;
    Book b; Owed o;
    if (!BookParseLine("T 1 616c65782e31 426c61646573", &b) || TeamOf(b, "alex.1") != 0 || b.teams[0].name != "Blades") { Fail("a lone founder's team line"); return; }
    InviteDecide(&b, "alex.1", "kim.1", true, true, "", 5.0);
    InviteDecide(&b, "jo.1", "max.1", true, true, "Wolves", 5.0);
    Book c = b;
    std::vector<Owed> made; std::string name; std::vector<Invite> gone;
    if (DisbandDecide(&b, "alex.1", std::vector<std::string>(), 3, &made, &name, &gone) != kOk || !made.empty() || !b.teams.empty() || name != "Blades") Fail("a disband with no members");
    if (gone.size() != 1 || gone[0].to != "kim.1" || InviteTo(b, "kim.1") >= 0 || InviteTo(b, "max.1") < 0) Fail("the disband's open invitations");
    gone.clear(); o = Owed();
    if (LeaveDecide(&c, "alex.1", 3, &o, &gone) != kOk || o.no != 0 || !c.teams.empty()) Fail("a lone founder leaves");
    if (gone.size() != 1 || gone[0].to != "kim.1" || gone[0].name != "Blades" || InviteTo(c, "kim.1") >= 0 || InviteTo(c, "max.1") < 0) Fail("the lone founder's open invitations");
}
/* teams.txt edge cases: the number line in any order, duplicate numbers, refused team lines' members, limits */
void t_t546_team_load_edges()
{
    using namespace swteam;
    Book b; int why = -1;
    if (!BookParseLine("T 5 61 41", &b) || !BookParseLine("M 5 62 1 1 -", &b) || !BookParseLine("N 2 3", &b, &why) || why != kLineOk || !BookParseLine("O 9 63 1 0 41 1 - 7", &b)) Fail("the lines");
    if (b.nextTeam != 6 || b.nextOwed != 10) Fail("a number line read after the rows lowered the next numbers: " + I((int)b.nextTeam) + " " + I((int)b.nextOwed));
    if (!BookParseLine("N 99 100", &b) || b.nextTeam != 99 || b.nextOwed != 100) Fail("a higher number line");
    if (BookParseLine("T 5 64 42", &b, &why) || why != kLineDuplicate) Fail("a duplicate team number taken");
    if (BookParseLine("M 5 65 1 1 -", &b, &why) || why != kLineBad || TeamOf(b, "e") != -1) Fail("a member line of a refused team line taken");
    if (BookParseLine("O 9 66 1 0 41 1 - 7", &b, &why) || why != kLineDuplicate) Fail("a duplicate row number taken");
    if (BookParseLine("T 0 67 41", &b) || BookParseLine("T 4294967295 67 41", &b) || BookParseLine("O 0 67 1 0 41 1 - 7", &b)) Fail("a team or row number 0 or the largest taken");
    const std::string over(2 * (kMaxSnapshot + 1), 'a'), at(2 * kMaxSnapshot, 'a');
    if (BookParseLine("T 6 68 41", &b) == false) Fail("team 6");
    if (BookParseLine("M 6 69 1 1 " + over, &b) || BookParseLine("O 20 69 1 0 41 1 " + over + " 7", &b)) Fail("a standing over the limit taken");
    if (!BookParseLine("M 6 69 1 1 " + at, &b)) Fail("a standing at the limit refused");
    if (BookParseLine("T 7 6a " + std::string(2 * (kMaxName + 1), 'a'), &b)) Fail("a name over the limit taken");
    for (unsigned i = 0; i + 1 < kMaxOwedEach; ++i) if (!BookParseLine("O " + I((int)(30 + i)) + " 63 1 0 41 1 - 7", &b)) Fail("row " + I((int)i));
    if (BookParseLine("O 50 63 1 0 41 1 - 7", &b, &why) || why != kLineBad || OwedCount(b, "c") != kMaxOwedEach) Fail("a ninth row for one player taken");
    if (TableText(ToWire(b, std::map<std::string, unsigned>())) != "no teams") Fail("players with no slot are not on the wire");
}
/* a deleted profile (owner 482 a): a member out with no row; a founder's role to the member who joined first; a lone founder's team ends */
void t_t546_team_profile_deleted()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0; Owed o;
    InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&b, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no);
    InviteDecide(&b, "alex.1", "kim.1", true, true, "Blades", 2.0); AnswerDecide(&b, "kim.1", true, 1, T546Snap("k"), 2, 3.0, &v, &no);
    InviteDecide(&b, "alex.1", "jo.1", true, true, "Blades", 4.0);
    std::vector<Invite> gone; std::string name, nf; unsigned dropped = 9;
    Book m = b;
    if (ProfileDeletedDecide(&m, "kim.1", &gone, &name, &nf, &dropped) != kDelMember || TeamOf(m, "kim.1") != -1 || !m.owed.empty() || dropped != 0 || name != "Blades"
        || InviteTo(m, "jo.1") < 0 || m.teams[0].members.size() != 1) Fail("a member's profile deleted");
    Book f = b; gone.clear(); nf.clear();
    if (ProfileDeletedDecide(&f, "alex.1", &gone, &name, &nf, &dropped) != kDelFounderPassed || nf != "sam.1" || !IsFounder(f, "sam.1") || f.teams[0].no != b.teams[0].no
        || f.teams[0].name != "Blades" || f.teams[0].members.size() != 1 || f.teams[0].members[0].id != "kim.1" || TeamOf(f, "alex.1") != -1) Fail("the founder's profile deleted");
    if (gone.size() != 1 || gone[0].to != "jo.1" || InviteTo(f, "jo.1") >= 0) Fail("the deleted founder's invitation");
    if (f.teams[0].founderSnap != T546Snap("s") || f.teams[0].founderSnapRows != 1) Fail("the promoted founder's pre-join standing was not kept");
    if (!T546FileRoundTrip(f)) Fail("the passed team does not read back");
    {
        Book r; std::istringstream is(BookFile(f)); std::string line;
        while (std::getline(is, line)) BookParseLine(line, &r);
        if (r.teams.size() != 1 || r.teams[0].founderSnap != T546Snap("s") || r.teams[0].founderSnapRows != 1 || r.teams[0].founder != "sam.1") Fail("the promoted founder's standing read back");
    }
    if (InviteDecide(&f, "sam.1", "jo.1", true, true, "", 10.0) != kOk) Fail("the new founder invites");
    std::vector<Owed> made;
    Book fd = f;   /* the promoted founder disbands with kim in: kim's row, then their own */
    if (DisbandDecide(&fd, "sam.1", std::vector<std::string>(), 12, &made, &name) != kOk || !fd.teams.empty()) Fail("the new founder disbands");
    if (made.size() != 2 || made[1].id != "sam.1" || made[1].why != kWhyDisbanded || made[1].away != 0 || made[1].snap != T546Snap("s")) Fail("the promoted founder disbands: their own restore row");
    TeamEnd end;
    if (RemoveDecide(&f, "sam.1", "kim.1", true, true, 11, &o, 0, 0, 0, &end) != kOk || o.why != kWhyRemoved || o.id != "kim.1") Fail("the new founder removes");
    if (!end.ended || !f.teams.empty() || end.founder != "sam.1" || end.row.no == 0 || end.row.id != "sam.1" || end.row.why != kWhyDisbanded || end.row.away != 0
        || end.row.snap != T546Snap("s") || end.row.snapRows != 1 || end.teamName != "Blades" || f.owed.size() != 2) Fail("the last member removed: the team ends, the promoted founder's own row as a disband's");
    if (end.gone.size() != 1 || end.gone[0].to != "jo.1" || InviteTo(f, "jo.1") >= 0) Fail("the founder's invitation ends with the team");
    /* a promoted founder with members disbands: the members' rows, then the founder's; refused whole when the founder's row has no room */
    Book d = b; gone.clear();
    InviteDecide(&d, "alex.1", "max.1", true, true, "", 5.0); AnswerDecide(&d, "max.1", true, 3, T546Snap("x"), 4, 6.0, &v, &no);
    ProfileDeletedDecide(&d, "alex.1", &gone, &name, &nf, &dropped);
    Book full = d;
    for (unsigned i = 0; i < kMaxOwedEach; ++i) { Owed x; x.no = 900 + i; x.id = "sam.1"; x.why = kWhyLeft; full.owed.push_back(x); }
    made.clear();
    if (DisbandDecide(&full, "sam.1", std::vector<std::string>(), 13, &made, &name) != kRefOwedFull || full.teams.size() != 1 || !made.empty()) Fail("a disband whose founder row has no room");
    if (RemoveDecide(&full, "sam.1", "kim.1", true, true, 13, &o) != kOk) Fail("the full founder removes kim");
    o = Owed(); end = TeamEnd();
    const size_t owedBefore = full.owed.size();
    if (RemoveDecide(&full, "sam.1", "max.1", true, true, 13, &o, 0, 0, 0, &end) != kRefOwedFull || full.teams.size() != 1 || full.teams[0].members.size() != 1 || o.no != 0 || end.ended
        || full.owed.size() != owedBefore) Fail("removing the last member when the founder's row has no room: refused whole, nothing changed");
    made.clear();
    if (DisbandDecide(&d, "sam.1", std::vector<std::string>(1, "kim.1"), 15, &made, &name) != kOk || made.size() != 3 || made[0].id != "kim.1" || made[1].id != "max.1"
        || made[2].id != "sam.1" || made[2].snap != T546Snap("s") || made[1].away != 1 || made[0].away != 0 || made[2].away != 0) Fail("a promoted founder with members disbands");
    /* the file refuses a founder standing with no team line, a second one for a team, an empty one */
    Book g; int why = -1;
    if (BookParseLine("F 3 1 61", &g)) Fail("a founder standing before its team line");
    if (!BookParseLine("T 3 61 41", &g) || BookParseLine("F 3 1 -", &g) || !BookParseLine("F 3 2 6162", &g, &why) || why != kLineOk) Fail("a founder standing line");
    if (BookParseLine("F 3 1 63", &g, &why) || why != kLineDuplicate || g.teams[0].founderSnapRows != 2) Fail("a second founder standing for one team");
    Book e;
    InviteDecide(&e, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&e, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no); LeaveDecide(&e, "sam.1", 2, &o);
    if (!e.teams.empty() || e.owed.size() != 1) Fail("sam left: the team ended with its last member");
    BookParseLine("T 5 61 41", &e);   /* a founder alone, kept in a table from before a team ended with its last member */
    gone.clear();
    if (ProfileDeletedDecide(&e, "a", &gone, &name, &nf, &dropped) != kDelTeamEnded || !e.teams.empty() || e.owed.size() != 1) Fail("a lone founder's profile deleted");
    if (ProfileDeletedDecide(&e, "sam.1", &gone, &name, &nf, &dropped) != kDelNone || dropped != 1 || !e.owed.empty()) Fail("a deleted player's restore rows");
    if (ProfileDeletedDecide(&e, "nobody.1", &gone, &name, &nf, &dropped) != kDelNone || dropped != 0) Fail("a profile in no team");
}
/* a deletion that leaves one player alone in a team ends it as a disband (never refused: a row with no room is skipped) */
void t_t546h_profile_deleted_last()
{
    using namespace swteam;
    Book p; Invite v; unsigned no = 0;
    InviteDecide(&p, "alex.1", "sam.1", true, true, "Blades", 0.0); AnswerDecide(&p, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no);
    InviteDecide(&p, "alex.1", "kim.1", true, true, "Blades", 2.0);
    std::vector<Invite> gone; std::string name, nf; unsigned dropped = 0; TeamEnd end;
    Book q = p;
    const std::vector<std::string> on(1, "alex.1");
    if (ProfileDeletedDecide(&q, "sam.1", &gone, &name, &nf, &dropped, 50, &on, &end) != kDelLastEnded || !q.teams.empty() || !end.ended || end.founder != "alex.1"
        || end.row.no != 0 || end.rowSkipped || name != "Blades") Fail("the last member's profile deleted: the team ends, no row for a founder who made it");
    if (end.gone.size() != 1 || end.gone[0].to != "kim.1" || InviteTo(q, "kim.1") >= 0) Fail("the founder's invitation ends with the team");
    q = p; end = TeamEnd(); nf.clear(); gone.clear();
    if (ProfileDeletedDecide(&q, "alex.1", &gone, &name, &nf, &dropped, 51, 0, &end) != kDelLastEnded || !q.teams.empty() || end.founder != "sam.1" || nf != "sam.1"
        || end.row.no == 0 || end.row.id != "sam.1" || end.row.why != kWhyDisbanded || end.row.away != 1 || end.row.snap != T546Snap("s") || end.row.madeAt != 51
        || q.owed.size() != 1 || q.owed[0].no != end.row.no) Fail("the founder's own profile deleted with one member: the team ends, the member's standing goes back (away)");
    if (gone.size() != 1 || gone[0].from != "alex.1") Fail("the deleted founder's invitation");
    if (!T546FileRoundTrip(q)) Fail("the ended team's row does not read back");
    q = p; end = TeamEnd();
    for (unsigned i = 0; i < kMaxOwedEach; ++i) { Owed x; x.no = 700 + i; x.id = "sam.1"; x.why = kWhyLeft; q.owed.push_back(x); }
    if (ProfileDeletedDecide(&q, "alex.1", &gone, &name, &nf, &dropped, 52, 0, &end) != kDelLastEnded || !q.teams.empty() || !end.rowSkipped || end.row.no != 0
        || q.owed.size() != kMaxOwedEach) Fail("a deletion is never refused: the team ends, the row with no room skipped");
    q = p;   /* with two members the team stays */
    InviteDecide(&q, "alex.1", "jo.1", true, true, "", 3.0); AnswerDecide(&q, "jo.1", true, 1, T546Snap("j"), 2, 4.0, &v, &no);
    end = TeamEnd();
    if (ProfileDeletedDecide(&q, "sam.1", &gone, &name, &nf, &dropped, 53, 0, &end) != kDelMember || q.teams.size() != 1 || end.ended) Fail("a member deleted from a team of three: the team stays");
}
/* a DECLINE: the same inviter's next invitation to that player waits 60 s (memory only) */
void t_t546h_decline_wait()
{
    using namespace swteam;
    Book b; Invite v; unsigned no = 0;
    if (InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 10.0) != kOk || AnswerDecide(&b, "sam.1", false, 0, std::vector<char>(), 1, 12.0, &v, &no) != kOk) Fail("sam declines");
    if (InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 12.5) != kRefDeclined || InviteTo(b, "sam.1") >= 0) Fail("a re-invite at once is refused");
    if (InviteDecide(&b, "jo.1", "sam.1", true, true, "Wolves", 13.0) != kOk) Fail("another inviter is not held back");
    if (AnswerDecide(&b, "sam.1", false, 0, std::vector<char>(), 1, 14.0, &v, &no) != kOk || b.declined.size() != 2) Fail("sam declines jo too");
    if (InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 12.0 + kDeclineWaitSeconds - 0.1) != kRefDeclined) Fail("still refused just before the wait ends");
    std::vector<Invite> gone;
    ExpireInvites(&b, 12.0 + kDeclineWaitSeconds, &gone);
    if (!gone.empty() || b.declined.size() != 1 || b.declined[0].from != "jo.1") Fail("the wait past its time dropped (not an ended invitation), jo's kept");
    if (InviteDecide(&b, "alex.1", "sam.1", true, true, "Blades", 12.0 + kDeclineWaitSeconds) != kOk) Fail("after 60 s the invitation goes");
    Book c = b; c.declined.clear();
    if (BookFile(b) != BookFile(c)) Fail("the decline wait is never in the file");
    if (AnswerDecide(&b, "sam.1", false, 0, std::vector<char>(), 1, 80.0, &v, &no) != kOk || b.declined.size() != 2) Fail("a second decline by the same pair: one wait, renewed");
    for (size_t i = 0; i < b.declined.size(); ++i) if (b.declined[i].from == "alex.1" && b.declined[i].expiresAt != 80.0 + kDeclineWaitSeconds) Fail("the renewed wait");
    if (std::string(ResultName(kRefDeclined)) != "declined-recently" || kRefLast != kRefDeclined) Fail("the refusal's name");
    Book a;   /* an ACCEPT starts no wait */
    InviteDecide(&a, "alex.1", "sam.1", true, true, "Blades", 0.0);
    if (AnswerDecide(&a, "sam.1", true, 1, T546Snap("s"), 1, 1.0, &v, &no) != kOk || !a.declined.empty()) Fail("an accept starts no wait");
}
/* a game's RESTORE rows: held until its world is loaded, a repeat of a held or answered number applies nothing, per world */
void t_t546_team_inbox()
{
    using namespace swteam;
    Inbox in; Down d; d.kind = kDnRestore; d.no = 4;
    if (InboxArrive(&in, "w1", d) != kInHeld || InboxArrive(&in, "w1", d) != kInAgainHeld || in.held.size() != 1) Fail("a row held once");
    std::vector<Down> ready; InboxTake(&in, &ready);
    if (ready.size() != 1 || ready[0].no != 4 || !in.held.empty()) Fail("the held row out");
    if (InboxArrive(&in, "w1", d) != kInAgainDone || !in.held.empty()) Fail("an answered row came again");
    if (InboxArrive(&in, "w2", d) != kInHeld) Fail("another world's row of the same number");
    for (unsigned i = 0; i < kInboxDoneKept + 5; ++i) InboxMarkDone(&in, 100 + i);
    if (in.done.size() != kInboxDoneKept || in.done.back() != 100 + kInboxDoneKept + 4) Fail("the answered numbers kept");
}

/* ---- T-546 step 5 (t546c): shared standing - the team's record (src/common/teamwire.h) and each game's part (teamstanding.h) ---- */
swteam::PreJoin T546cPreJoin()
{
    swteam::PreJoin p;
    p.npc.name = "Sam's Crew"; p.npc.platoonIds = 7;
    coopown::FacRow a; a.sid = "17-gamedata.base"; a.rel = -60.0f; a.trust = 2.0f; a.trustNeg = 1.0f; a.relBack = -55.0f; a.flags = 2; a.flagsBack = 0;
    coopown::FacRow b; b.sid = "204-gamedata.base"; b.rel = 40.0f; b.relBack = 35.5f; b.flags = 0; b.flagsBack = 0;
    p.npc.rows.push_back(a); p.npc.rows.push_back(b);
    swteam::PlayerSide s0; s0.slot = 0; s0.rel = 12.5f; s0.flags = 0;
    swteam::PlayerSide s5; s5.slot = 5; s5.rel = 60.0f; s5.flags = 1;
    p.players.push_back(s0); p.players.push_back(s5);
    return p;
}
void t_t546c_prejoin_snapshot()
{
    using namespace swteam;
    const PreJoin p = T546cPreJoin();
    std::vector<char> b; EncodePreJoin(p, &b);
    PreJoin q;
    if (DecodePreJoin(b, &q) != kPjOk) { Fail("the pre-join snapshot decodes"); return; }
    if (q.npc.name != p.npc.name || q.npc.platoonIds != 7 || q.npc.rows.size() != 2 || q.players.size() != 2) Fail("its shape survives the round trip");
    else if (q.players[0].slot != 0 || q.players[0].rel != 12.5f || q.players[1].slot != 5 || q.players[1].rel != 60.0f || q.players[1].flags != 1) Fail("each side survives, with its ally flag");
    /* a version 1 snapshot (sides without flags) still decodes: flags -1 = not recorded */
    std::vector<char> fac; coopown::EncodeFaction(p.npc, &fac);
    std::vector<char> v1; PutU32(&v1, kPreJoinMagic); PutU32(&v1, 1); PutBytes(&v1, &fac[0], fac.size()); PutU32(&v1, 1); PutU32(&v1, 3); PutF32(&v1, -40.0f);
    PreJoin o1;
    if (DecodePreJoin(v1, &o1) != kPjOk || o1.players.size() != 1 || o1.players[0].flags != -1 || o1.players[0].rel != -40.0f) Fail("a version 1 snapshot decodes, flags not recorded");
    PreJoin old;
    if (DecodePreJoin(fac, &old) != kPjOk || old.npc.rows.size() != 2 || !old.players.empty()) Fail("a plain pp.faction record still decodes, with no sides");
    PreJoin x;
    std::vector<char> v9 = b; v9[4] = 9;
    if (DecodePreJoin(v9, &x) != kPjVersion) Fail("an unknown version is HELD (kPjVersion), not called broken");
    std::vector<char> extra = b; extra.push_back(0);
    if (DecodePreJoin(extra, &x) != kPjMalformed) Fail("a trailing byte is broken");
    std::vector<char> cut(b.begin(), b.end() - 3);
    if (DecodePreJoin(cut, &x) != kPjMalformed) Fail("a cut snapshot is broken");
    std::vector<char> blob; EncodeAnswer(&blob, true, 2, b);
    Up u;
    if (!DecodeUp(&blob[0], blob.size(), &u) || u.snap != b) Fail("the snapshot travels in the ACCEPT unchanged (the world server keeps it unread)");
}
swteam::Book T546cBook()
{
    swteam::Book b;
    swteam::Team t; t.no = 3; t.founder = "pa"; t.name = "The Rusty Blades";
    swteam::Member c; c.id = "pc"; c.snapRows = 1; c.snap.push_back('x');
    swteam::Member d; d.id = "pd"; d.snapRows = 1; d.snap.push_back('y');
    t.members.push_back(c); t.members.push_back(d);
    b.teams.push_back(t); b.nextTeam = 4;
    return b;
}
swteam::TeamRec T546cRec()
{
    swteam::TeamRec r;
    swteam::RecRow x; x.sid = "17-gamedata.base"; x.rel = 10.0f; x.trust = 1.0f; x.relBack = -5.0f; x.flags = 0; x.flagsBack = 2; x.have = swteam::kHaveFwd | swteam::kHaveBack;
    r.rows.push_back(x);
    swteam::RecStance st; st.slot = 1; st.rel = -100.0f; st.flags = 0; r.stances.push_back(st);
    return r;
}
void t_t546c_record_seed_delta()
{
    using namespace swteam;
    Book b = T546cBook();
    std::vector<Delta> ds(1); ds[0].sid = "17-gamedata.base"; ds[0].dir = 0; ds[0].dRel = -30.0f; ds[0].dTrust = 2.0f;
    if (DeltaDecide(&b, "pc", ds) != kRefNoRecord) Fail("a change before the founder seeded is refused (no-record)");
    if (SeedDecide(&b, "pc", T546cRec()) != kRefNotFounder) Fail("only the founder seeds");
    if (SeedDecide(&b, "pz", T546cRec()) != kRefNotInTeam) Fail("a player in no team seeds nothing");
    if (SeedDecide(&b, "pa", T546cRec()) != kOk || !b.teams[0].rec.seeded || b.teams[0].rec.gen != 1 || b.teams[0].rec.rows.size() != 1) Fail("the founder's seed is the record (gen 1)");
    if (SeedDecide(&b, "pa", T546cRec()) != kRefSeeded) Fail("a team seeds once");
    if (DeltaDecide(&b, "pc", ds) != kOk) { Fail("a member's change is taken"); return; }
    const RecRow& r = b.teams[0].rec.rows[0];
    if (r.rel != -20.0f || r.trust != 3.0f || r.relBack != -5.0f || b.teams[0].rec.gen != 2) Fail("the member's difference added to the team's value; the other direction untouched; gen up");
    ds[0].dRel = -500.0f;
    DeltaDecide(&b, "pd", ds);
    if (b.teams[0].rec.rows[0].rel != kRecLow) Fail("the relation stays on the engine's scale");
    ds[0].dRel = 30.0f;
    DeltaDecide(&b, "pa", ds);
    if (b.teams[0].rec.rows[0].rel != -70.0f) Fail("after the floor the next change counts from the floor (the record is the truth every member writes)");
    std::vector<Delta> nd(2);
    nd[0].sid = "204-gamedata.base"; nd[0].dir = 1; nd[0].rel = 44.0f; nd[0].trust = 1.5f; nd[0].dRel = 9.0f; nd[0].flags = 2;
    nd[1].sid = "17-gamedata.base"; nd[1].dir = 1; nd[1].dRel = 5.0f; nd[1].flags = 0;
    DeltaDecide(&b, "pc", nd);
    const TeamRec& rec = b.teams[0].rec;
    if (rec.rows.size() != 2 || rec.rows[1].sid != "204-gamedata.base" || rec.rows[1].have != (unsigned)kHaveBack || rec.rows[1].relBack != 44.0f || rec.rows[1].flagsBack != 2)
        Fail("a direction the record does not hold takes the member's value (and only that direction is held)");
    if (rec.rows[0].relBack != 0.0f || rec.rows[0].flagsBack != 0) Fail("a held direction moves by the difference, its flags the member's");
    if (DeltaDecide(&b, "pz", ds) != kRefNotInTeam) Fail("a player in no team changes no record");
}
void t_t546c_record_stance_side()
{
    using namespace swteam;
    Book b = T546cBook();
    if (StanceDecide(&b, "pa", 1, false, -100.0f, 0) != kRefNoRecord) Fail("a stance before the seed is refused");
    SeedDecide(&b, "pa", T546cRec());
    if (StanceDecide(&b, "pc", 1, false, 0.0f, 0) != kOk || b.teams[0].rec.stances[0].rel != 0.0f) Fail("owner 512: a member's own side change is the team's stance (the latest wins)");
    if (StanceDecide(&b, "pa", 2, true, -100.0f, 0) != kRefInTeam) Fail("never a stance towards a member (the pin's)");
    if (StanceDecide(&b, "pa", 1, false, 30.0f, 1) != kOk || b.teams[0].rec.stances.size() != 1 || b.teams[0].rec.stances[0].rel != 30.0f || b.teams[0].rec.stances[0].flags != 1)
        Fail("the founder's stance replaces the record's towards that player");
    if (StanceDecide(&b, "pa", 9, false, -300.0f, 0) != kOk || b.teams[0].rec.stances.size() != 2 || b.teams[0].rec.stances[1].rel != kRecLow) Fail("a stance towards a new player is added, on the scale");
    if (!DropStance(&b.teams[0].rec, 9) || b.teams[0].rec.stances.size() != 1 || DropStance(&b.teams[0].rec, 9)) Fail("a joiner's slot leaves the stances once");
    bool kept = false;
    if (SideDecide(&b, "pc", 2, "pa", 0, 12.0f, 0, &kept) != kOk || !kept) Fail("a member's side before the pin is kept");
    if (SideDecide(&b, "pc", 2, "pa", 0, 100.0f, 1, &kept) != kOk || kept || b.teams[0].sides[0].rel != 12.0f) Fail("the first report is kept - a later one (after the pin) is not");
    if (SideDecide(&b, "pc", 2, "pz", 7, 1.0f, 0, &kept) != kRefNotMember || SideDecide(&b, "pc", 2, "pc", 2, 1.0f, 0, &kept) != kRefNotMember) Fail("a side only between two members of one team");
}
void t_t546c_departure_both_sides()
{
    using namespace swteam;
    Book b = T546cBook();
    bool k = false;
    SideDecide(&b, "pc", 2, "pa", 0, 10.0f, 0, &k);   /* C towards A before the pin */
    SideDecide(&b, "pa", 0, "pc", 2, -20.0f, 2, &k);  /* A towards C */
    SideDecide(&b, "pd", 4, "pc", 2, 5.0f, 0, &k);    /* D towards C */
    SideDecide(&b, "pc", 2, "pd", 4, 7.0f, 0, &k);    /* C towards D */
    SideDecide(&b, "pa", 0, "pd", 4, 1.0f, 0, &k);    /* A towards D */
    Owed made; std::vector<Owed> sideRows;
    if (LeaveDecide(&b, "pc", 100, &made, 0, &sideRows) != kOk) { Fail("C leaves"); return; }
    if (made.id != "pc" || made.snap.size() != 1 || made.sides.size() != 2 || made.sides[0].slot != 0 || made.sides[0].rel != 10.0f || made.sides[1].slot != 4)
        Fail("the leaver's row: its snapshot and its sides towards each remaining member");
    if (sideRows.size() != 2 || sideRows[0].id != "pa" || sideRows[0].sides.size() != 1 || sideRows[0].sides[0].slot != 2 || sideRows[0].sides[0].rel != -20.0f || sideRows[0].sides[0].flags != 2
        || sideRows[1].id != "pd" || sideRows[1].sides[0].rel != 5.0f || !sideRows[1].snap.empty())
        Fail("477, both sides: a row for each remaining member with its side towards the leaver only (and its flags)");
    if (b.owed.size() != 3 || b.teams[0].sides.size() != 1 || b.teams[0].sides[0].owner != "pa" || b.teams[0].sides[0].other != "pd") Fail("every side with the leaver is dropped; the others stay");
    /* a remaining member whose rows are full: its side row is not made, and the departure goes ahead (one player's backlog never
       stops another's leave); side rows name a remaining member who is away */
    Book f = T546cBook();
    SideDecide(&f, "pa", 0, "pc", 2, 1.0f, 0, &k);
    SideDecide(&f, "pd", 4, "pc", 2, 2.0f, 0, &k);
    for (unsigned i = 0; i < kMaxOwedEach; ++i) { Owed o; o.no = 50 + i; o.id = "pa"; o.why = kWhyLeft; f.owed.push_back(o); }
    std::vector<Owed> fr; std::vector<std::string> skipped; const std::vector<std::string> online(1, "pa");
    if (LeaveDecide(&f, "pc", 100, &made, 0, &fr, &online, &skipped) != kOk || f.teams[0].members.size() != 1 || skipped.size() != 1 || skipped[0] != "pa"
        || fr.size() != 1 || fr[0].id != "pd" || fr[0].away != 1)
        Fail("a full backlog skips only that player's side row (named); the leave goes ahead; an offline member's side row is marked away");
    /* the leaver's own rows full: refused, nothing changed */
    Book g = T546cBook();
    for (unsigned i = 0; i < kMaxOwedEach; ++i) { Owed o; o.no = 70 + i; o.id = "pc"; o.why = kWhyLeft; g.owed.push_back(o); }
    if (LeaveDecide(&g, "pc", 100, &made) != kRefOwedFull || g.teams[0].members.size() != 2) Fail("no room for the leaver's own row: refused");
    /* disband: every member's row carries all its sides; the founder's too */
    Book d = T546cBook();
    SideDecide(&d, "pc", 2, "pa", 0, 10.0f, 0, &k); SideDecide(&d, "pd", 4, "pc", 2, 5.0f, 0, &k); SideDecide(&d, "pa", 0, "pd", 4, 3.0f, 0, &k);
    std::vector<Owed> rows; std::string name;
    if (DisbandDecide(&d, "pa", std::vector<std::string>(1, "pc"), 100, &rows, &name) != kOk || rows.size() != 3) { Fail("disband: a row for each member and the founder (who holds a side)"); return; }
    if (rows[0].id != "pc" || rows[0].away != 0 || rows[0].sides.size() != 1 || rows[1].id != "pd" || rows[1].away != 1 || rows[1].sides[0].slot != 2 || rows[2].id != "pa" || rows[2].sides[0].slot != 4)
        Fail("disband rows carry each player's own sides, away named");
}
/* ---- T-546 step 8 (t546i): three players in one faction - a departure from a team that stays ---- */
void t_t546i_departure_stance()
{
    using namespace swteam;
    /* A founds, C (slot 2) and D (slot 4) are members; the record is seeded with a stance towards slot 1 (an outsider) */
    Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
    bool k = false;
    SideDecide(&b, "pa", 0, "pc", 2, -20.0f, 2, &k);  /* A towards C before the pin */
    SideDecide(&b, "pd", 4, "pc", 2, 5.0f, 0, &k);    /* D towards C */
    SideDecide(&b, "pc", 2, "pa", 0, 10.0f, 0, &k);   /* C towards A */
    const unsigned g0 = b.teams[0].rec.gen;
    Owed made; std::vector<Owed> sideRows; TeamEnd end;
    const std::vector<std::string> online(1, "pa");   /* C is away: removed while offline */
    if (RemoveDecide(&b, "pa", "pc", true, false, 100, &made, &sideRows, &online, 0, &end) != kOk || end.ended) { Fail("A removes C while C is away; A and D stay a team"); return; }
    if (made.id != "pc" || made.away != 1 || made.sides.size() != 1 || made.sides[0].slot != 0) Fail("C's own row is a while-away row with its side towards A");
    if (!sideRows.empty()) Fail("a team that stays: its record carries the stance towards C, so A and D get no side rows (476)");
    const TeamRec& r = b.teams[0].rec;
    size_t at = r.stances.size();
    for (size_t i = 0; i < r.stances.size(); ++i) if (r.stances[i].slot == 2) at = i;
    if (at == r.stances.size() || r.stances[at].rel != -20.0f || r.stances[at].flags != 2 || r.gen != g0 + 1)
        Fail("the team that stays takes the founder's side from before the pin as its stance towards the removed member (one new generation)");
    if (r.stances.size() != 2 || r.stances[0].slot != 1 || r.stances[0].rel != -100.0f) Fail("the other stances stay as they were");
    if (b.teams[0].sides.size() != 0) Fail("every side with the removed member is dropped (D's towards A was never recorded)");
    /* the founder holds no recorded side towards the leaver (they were allies before the pin): the record does not change */
    Book n = T546cBook(); SeedDecide(&n, "pa", T546cRec());
    SideDecide(&n, "pd", 4, "pc", 2, 5.0f, 0, &k);
    const unsigned n0 = n.teams[0].rec.gen;
    if (LeaveDecide(&n, "pc", 100, &made) != kOk || n.teams[0].rec.gen != n0 || n.teams[0].rec.stances.size() != 1) Fail("no founder's side from before the pin: no stance is made up");
    /* the stance is already the founder's side: no new generation */
    Book same = T546cBook(); SeedDecide(&same, "pa", T546cRec());
    SideDecide(&same, "pa", 0, "pc", 2, 7.0f, 0, &k);
    StanceDecide(&same, "pa", 2, false, 7.0f, 0);
    const unsigned s0 = same.teams[0].rec.gen;
    if (LeaveDecide(&same, "pc", 100, &made) != kOk || same.teams[0].rec.gen != s0) Fail("a stance already the founder's side is no new generation");
    /* a record not seeded: nothing is made */
    Book ns = T546cBook();
    SideDecide(&ns, "pa", 0, "pc", 2, -20.0f, 0, &k);
    if (LeaveDecide(&ns, "pc", 100, &made) != kOk || ns.teams[0].rec.seeded || !ns.teams[0].rec.stances.empty()) Fail("an unseeded record takes no stance");
    /* the last member's departure ends the team: no record is left to change, and the departure is not refused */
    Book two = T546cBook(); SeedDecide(&two, "pa", T546cRec());
    SideDecide(&two, "pa", 0, "pd", 4, 3.0f, 0, &k);
    if (LeaveDecide(&two, "pc", 100, &made) != kOk || LeaveDecide(&two, "pd", 100, &made, 0, 0, 0, 0, &end) != kOk || !end.ended || !two.teams.empty())
        Fail("the last member's leave ends the team as before");
    /* the stance survives the file */
    Book f = T546cBook(); SeedDecide(&f, "pa", T546cRec());
    SideDecide(&f, "pa", 0, "pc", 2, -20.0f, 2, &k);
    LeaveDecide(&f, "pc", 100, &made);
    Book back; std::istringstream in(BookFile(f)); std::string line;
    while (std::getline(in, line)) BookParseLine(line, &back);
    if (back.teams.size() != 1 || back.teams[0].rec.stances.size() != 2 || back.teams[0].rec.stances[1].slot != 2 || back.teams[0].rec.stances[1].rel != -20.0f)
        Fail("teams.txt keeps the stance towards the departed member");
    /* the game's side: the teammates who left between two tables */
    std::vector<unsigned> before; before.push_back(1); before.push_back(2); before.push_back(4);
    std::vector<unsigned> now; now.push_back(4); now.push_back(1);
    const std::vector<unsigned> gone = DepartedMates(before, now);
    if (gone.size() != 1 || gone[0] != 2) Fail("the teammate missing from the new table has departed");
    if (!DepartedMates(now, before).empty() || !DepartedMates(std::vector<unsigned>(), before).empty()) Fail("a join is no departure");
    if (DepartedMates(before, std::vector<unsigned>()).size() != 3) Fail("the team ended (or this player left): every former teammate has departed");
}
void t_t546c_record_wire_and_file()
{
    using namespace swteam;
    TeamRec r = T546cRec(); r.gen = 7; r.seeded = 1;
    std::vector<char> blob; EncodeRec(r, &blob);
    TeamRec q;
    if (!DecodeRec(blob, &q) || q.gen != 7 || !q.seeded || q.rows.size() != 1 || q.rows[0].flagsBack != 2 || q.rows[0].have != 3u || q.stances.size() != 1 || q.stances[0].rel != -100.0f) Fail("the record's round trip");
    std::vector<char> bad = blob; bad.push_back(1);
    if (DecodeRec(bad, &q)) Fail("a record with a trailing byte is refused");
    std::vector<char> m; EncodeRecord(&m, 3, r);
    Down d;
    if (!DecodeDown(&m[0], m.size(), &d) || d.kind != kDnRecord || d.recTeam != 3 || d.rec.gen != 7) Fail("RECORD down");
    std::vector<Delta> ds(1); ds[0].sid = "x"; ds[0].dir = 1; ds[0].dRel = -3.0f; ds[0].rel = 4.0f; ds[0].flags = 2;
    EncodeDeltas(&m, ds);
    Up u;
    if (!DecodeUp(&m[0], m.size(), &u) || u.kind != kUpDelta || u.deltas.size() != 1 || u.deltas[0].dir != 1 || u.deltas[0].dRel != -3.0f || u.deltas[0].flags != 2) Fail("DELTA up");
    if (!EncodeSeed(&m, r) || !DecodeUp(&m[0], m.size(), &u) || u.kind != kUpSeed || u.rec.rows.size() != 1) Fail("SEED up");
    EncodeStance(&m, 5, -40.0f, 0);
    if (!DecodeUp(&m[0], m.size(), &u) || u.kind != kUpStance || u.slot != 5 || u.rel != -40.0f) Fail("STANCE up");
    EncodeSide(&m, 6, 12.0f, 1);
    if (!DecodeUp(&m[0], m.size(), &u) || u.kind != kUpSide || u.slot != 6 || u.flags != 1) Fail("SIDE up");
    Owed o; o.no = 9; o.id = "pc"; o.why = kWhyRemoved; o.teamName = "T"; RecStance st; st.slot = 0; st.rel = 10.0f; st.flags = 1; o.sides.push_back(st);
    EncodeRestore(&m, o);
    if (!DecodeDown(&m[0], m.size(), &d) || d.kind != kDnRestore || d.sides.size() != 1 || d.sides[0].rel != 10.0f || !d.snap.empty()) Fail("RESTORE down carries the sides");
    /* the file: R, S and an O line with sides read back to the same text; an O line without sides still reads */
    Book b = T546cBook();
    SeedDecide(&b, "pa", T546cRec());
    bool k = false; SideDecide(&b, "pc", 2, "pa", 0, 10.5f, 1, &k);
    b.owed.push_back(o); b.nextOwed = 10;
    const std::string text = BookFile(b);
    Book back; std::istringstream is(text); std::string line; int lines = 0;
    while (std::getline(is, line)) { if (!BookParseLine(line, &back)) Fail("a line of the file did not read back: " + line.substr(0, 40)); ++lines; }
    if (BookFile(back) != text) Fail("teams.txt with the record, a side and a row with sides reads back to the same text");
    if (back.teams.size() != 1 || !back.teams[0].rec.seeded || back.teams[0].sides.size() != 1 || back.teams[0].sides[0].rel != 10.5f || back.owed[0].sides.size() != 1) Fail("the read-back book holds them");
    Book old;
    BookParseLine("O 4 7063 1 0 54 0 - 100", &old);
    if (old.owed.size() != 1 || !old.owed[0].sides.empty()) Fail("an O line written before the sides still reads");
    Book dup = back;
    if (BookParseLine("R 3 " + HexV(blob), &dup)) Fail("a second record line for one team is refused");
    const std::vector<std::string> ids = TeamIds(b.teams[0]);
    if (ids.size() != 3 || ids[0] != "pa" || ids[1] != "pc" || ids[2] != "pd") Fail("a RECORD goes to the founder, then the members in joining order");
}
void t_t546c_game_rules()
{
    using namespace swteam;
    std::vector<WireTeam> t(1); t[0].no = 1; t[0].founderSlot = 0; t[0].memberSlots.push_back(2); t[0].memberSlots.push_back(4);
    if (RoleOf(t, 0) != 1 || RoleOf(t, 2) != 0 || RoleOf(t, 3) != -1) Fail("role: founder, member, none");
    if (!StanceIsFounders(0, false) || StanceIsFounders(1, false) || StanceIsFounders(0, true) || StanceIsFounders(-1, false)) Fail("476: a member's stance towards a non-member is the founder's");
    std::vector<unsigned> fan = FanOutTargets(t, 1, 2);
    if (fan.size() != 2 || fan[0] != 0 || fan[1] != 4) Fail("476, either side: a stance towards a member is set towards the whole team");
    if (!FanOutTargets(t, 4, 2).empty() || !FanOutTargets(t, 1, 3).empty()) Fail("no fan-out inside one's own team, or towards a player in no team");
    if (!SeedDue(1, false, false) || SeedDue(1, true, false) || SeedDue(1, false, true) || SeedDue(0, false, false)) Fail("the founder seeds a team with no record, once a link");
    if (!RecordWriteDue(true, true, true, true) || RecordWriteDue(false, true, true, true) || RecordWriteDue(true, false, true, true) || RecordWriteDue(true, true, false, true) || RecordWriteDue(true, true, true, false))
        Fail("the record is written when owed, the world ready, in that team and seeded");
    Delta d;
    if (!DeltaOf("x", 0, Standing(-30.0f, 2.0f, 0.0f), 2, Standing(10.0f, 1.0f, 0.0f), 0, &d) || d.dRel != -40.0f || d.dTrust != 1.0f || d.rel != -30.0f || d.flags != 2) Fail("an own change: the difference from the base, and the value now");
    if (DeltaOf("x", 0, Standing(1.0f, 0, 0), 0, Standing(1.0f, 0, 0), 0, &d)) Fail("nothing moved: no change");
    if (!DeltaOf("x", 1, Standing(1.0f, 0, 0), 2, Standing(1.0f, 0, 0), 0, &d)) Fail("a flag alone is a change");
    std::vector<PlayerSide> snap = T546cPreJoin().players;
    std::vector<RecStance> row(2); row[0].slot = 5; row[0].rel = 10.0f; row[0].flags = 0; row[1].slot = 7; row[1].rel = -3.0f;
    const std::vector<PlayerSide> merged = MergeSides(snap, row);
    if (merged.size() != 3 || merged[1].slot != 5 || merged[1].rel != 60.0f || merged[1].flags != 1 || merged[2].slot != 7 || merged[2].rel != -3.0f)
        Fail("the snapshot's own side wins over the side recorded before the pin; a side the snapshot lacks comes from the row");
    std::vector<unsigned> mates(1, 5), skipped;
    const std::vector<PlayerSide> back = RestoreSides(merged, 2, mates, &skipped);
    if (back.size() != 2 || back[0].slot != 0 || back[1].slot != 7 || skipped.size() != 1 || skipped[0] != 5) Fail("a teammate now is left at ally (skipped, named)");
    if (RestoreRetryMs(1) != kRestoreRetryFirstMs || RestoreRetryMs(2) != 2 * kRestoreRetryFirstMs || RestoreRetryMs(40) != kRestoreRetryCapMs) Fail("a row not written: the wait doubles to its cap");
    if (!RestoreDue(true, true) || RestoreDue(false, true) || RestoreDue(true, false)) Fail("written only with the world ready and a table from this link");
    std::vector<int> lv(1, nametag::kFriendly); lv.push_back(nametag::kUnknown);
    if (TheirSideTowardsTeam(lv) != nametag::kFriendly) Fail("an unread side does not count");
    lv.push_back(nametag::kHostile);
    if (TheirSideTowardsTeam(lv) != nametag::kHostile) Fail("hostile towards any member: hostile towards the team");
    Inbox in; Down d1; d1.kind = kDnRestore; d1.no = 4; Down d2 = d1; d2.no = 6;
    InboxArrive(&in, "w", d1); InboxArrive(&in, "w", d2);
    if (!InboxAnswerOne(&in, 6) || in.held.size() != 1 || in.held[0].no != 4) Fail("a written row leaves the held list; the other stays held");
    if (InboxArrive(&in, "w", d2) != kInAgainDone || InboxArrive(&in, "w", d1) != kInAgainHeld) Fail("written: answered again; not written: still held");
    /* the PLAYERS tab: towards a non-member a member's buttons are all disabled, its own (the founder's) value ticked */
    std::vector<playerstab::RowView> rows(1); rows[0].slot = 3; rows[0].you = nametag::kHostile; rows[0].founderSets = true;
    const playerstab::Bottom bt = playerstab::BottomFor(rows, 3);
    if (bt.ticked != nametag::kHostile || playerstab::ButtonEnabled(bt, 0) || playerstab::ButtonEnabled(bt, 2) || playerstab::LockLook(bt) != 2) Fail("476 on the PLAYERS tab: greyed, the founder's value shown");
    if (playerstab::PressForRow(0, nametag::kHostile, false, true) != playerstab::kPressNothing) Fail("a press does nothing there");
}
void t_t546c_fold1()
{
    using namespace swteam;
    /* the record: an empty or zero change is no new generation; kDirAbsolute takes the member's value; the size cap */
    Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
    const unsigned g0 = b.teams[0].rec.gen; bool changed = true;
    if (DeltaDecide(&b, "pc", std::vector<Delta>(), &changed) != kOk || changed || b.teams[0].rec.gen != g0) Fail("an empty DELTA changes nothing");
    std::vector<Delta> z(1); z[0].sid = "17-gamedata.base"; z[0].dir = 0; z[0].flags = 0;
    if (DeltaDecide(&b, "pc", z, &changed) != kOk || changed || b.teams[0].rec.gen != g0) Fail("a zero change is no new generation");
    z[0].dir = 0 | kDirAbsolute; z[0].rel = 33.0f; z[0].trust = 4.0f;
    if (DeltaDecide(&b, "pc", z, &changed) != kOk || !changed || b.teams[0].rec.rows[0].rel != 33.0f || b.teams[0].rec.rows[0].trust != 4.0f) Fail("a change with no base here takes the member's value");
    if (StanceDecide(&b, "pa", 1, false, -100.0f, 0, &changed) != kOk || changed) Fail("the same stance again is no new generation");
    Book big = T546cBook(); TeamRec huge; for (int i = 0; i < 1300; ++i) { RecRow x; x.sid = "faction-number-" + I(i) + "-gamedata.base"; x.have = 3; huge.rows.push_back(x); }
    if (RecBytes(huge) <= kMaxSnapshot || SeedDecide(&big, "pa", huge) != kRefTooMany) Fail("a record over the 64 KB limit is refused at the seed");
    TeamRec most; for (int i = 0; i < 800; ++i) { RecRow x; x.sid = "faction-number-" + I(i) + "-gamedata.base"; x.have = 3; most.rows.push_back(x); }
    if (SeedDecide(&big, "pa", most) != kOk) { Fail("a record under the limit seeds"); return; }
    std::vector<Delta> grow;
    for (int i = 0; i < 400; ++i) { Delta d; d.sid = "another-faction-" + I(i) + "-gamedata.base"; d.dir = 0; d.rel = 1.0f; grow.push_back(d); }
    Book before = big;
    if (DeltaDecide(&big, "pc", grow) != kRefTooMany) Fail("a DELTA that would grow the record past the limit is refused (the world server keeps its copy)");
    std::vector<char> enc; EncodeRec(before.teams[0].rec, &enc);
    Book rb = T546cBook();
    if (enc.size() > kMaxSnapshot || !BookParseLine("R 3 " + HexV(enc), &rb)) Fail("a record within the limit reads back from teams.txt (the file reader agrees)");
    /* 1: a departure row's NPC part is written once, each side once; a side with no faction here has nothing to write */
    RestoreProgress pg;
    if (SideDone(pg, 2)) Fail("nothing done at first");
    pg.npcDone = true; pg.sidesDone.push_back(2);
    if (!SideDone(pg, 2) || SideDone(pg, 4)) Fail("a written side is not written again; the others still are");
    if (SideOutcome(1) != kSideWritten || SideOutcome(-1) != kSideNothing || SideOutcome(-2) != kSideRetry) Fail("no faction of that player here: nothing to write, not a retry");
    /* 2: the side reported is the first pin write's */
    if (!SideReportDue(1, false) || SideReportDue(1, true) || SideReportDue(0, false) || SideReportDue(-1, false)) Fail("only the first pin write reports a side; a side already at the pin reports nothing");
    /* 3: an old record is not written over unsent own changes */
    if (RecordWriteSafe(false, true) || !RecordWriteSafe(true, true) || !RecordWriteSafe(false, false)) Fail("no link and a base: the record waits; after a load (no base) it is written");
    if (!GatherDue(true, true, true) || GatherDue(false, true, true) || GatherDue(true, false, true) || GatherDue(true, true, false)) Fail("own changes gathered with a base, a link and a record (or the founder's SEED)");
    /* 13: stances the record lacks */
    std::vector<PlayerSide> mine(3); mine[0].slot = 1; mine[1].slot = 2; mine[2].slot = 6;
    std::vector<RecStance> rs(1); rs[0].slot = 1;
    const std::vector<unsigned> miss = StancesMissing(mine, rs, std::vector<unsigned>(1, 2), 0, std::vector<unsigned>());
    if (miss.size() != 1 || miss[0] != 6) Fail("the founder fills a stance only for a player outside the team the record lacks");
    if (!StancesMissing(mine, rs, std::vector<unsigned>(1, 2), 0, std::vector<unsigned>(1, 6)).empty()) Fail("a player who has just left the team waits for the record after the departure");
    if (!SideMoveIsTeamStance(0) || !SideMoveIsTeamStance(1) || SideMoveIsTeamStance(-1)) Fail("owner 512: a side moved on any member's game is the team's stance");
}
/* ---- T-546 step 8 fold 1: three players - the leaver's own sides, the remaining members' sides from the record, the fill's wait,
   a newer own change, the stance's caps ---- */
void t_t546i_fold1()
{
    using namespace swteam;
    bool k = false;
    /* 1: A (slot 0) founds; B (pc, slot 2) joins - its snapshot holds its own sides: towards A 12, towards C (pd, slot 4) 15; C
       joins, and B's pin towards C found A's stance (-100, written from the record), which B recorded; B leaves while C stays */
    {
        Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
        PreJoin pj = T546cPreJoin(); pj.players.clear();
        PlayerSide toA; toA.slot = 0; toA.rel = 12.0f; toA.flags = 0; pj.players.push_back(toA);
        PlayerSide toC; toC.slot = 4; toC.rel = 15.0f; toC.flags = 0; pj.players.push_back(toC);
        EncodePreJoin(pj, &b.teams[0].members[0].snap);
        SideDecide(&b, "pc", 2, "pa", 0, 12.0f, 0, &k);
        SideDecide(&b, "pc", 2, "pd", 4, -100.0f, 0, &k);
        SideDecide(&b, "pa", 0, "pc", 2, -30.0f, 2, &k);   /* A towards B before the pin */
        SideDecide(&b, "pd", 4, "pc", 2, 40.0f, 0, &k);    /* C towards B before the pin */
        const unsigned g0 = b.teams[0].rec.gen;
        Owed made; std::vector<Owed> sideRows; TeamEnd end;
        if (LeaveDecide(&b, "pc", 100, &made, 0, &sideRows, 0, 0, &end) != kOk || end.ended) { Fail("B leaves; A and C stay a team"); return; }
        PreJoin back;
        if (DecodePreJoin(made.snap, &back) != kPjOk) { Fail("B's row carries its snapshot"); return; }
        const std::vector<PlayerSide> put = MergeSides(back.players, made.sides);
        float towardsC = 0.0f, towardsA = 0.0f; int found = 0;
        for (size_t i = 0; i < put.size(); ++i) { if (put[i].slot == 4) { towardsC = put[i].rel; ++found; } if (put[i].slot == 0) { towardsA = put[i].rel; ++found; } }
        if (found != 2 || put.size() != 2 || towardsC != 15.0f || towardsA != 12.0f) Fail("477: B's side towards C comes back as B's own pre-join value (15), not A's stance the pin found (-100)");
        if (!sideRows.empty()) Fail("476: A and C take the team's stance towards B from the record - no side rows");
        const TeamRec& r = b.teams[0].rec;
        size_t at = r.stances.size();
        for (size_t i = 0; i < r.stances.size(); ++i) if (r.stances[i].slot == 2) at = i;
        if (at == r.stances.size() || r.stances[at].rel != -30.0f || r.stances[at].flags != 2 || r.gen != g0 + 1) Fail("the record's stance towards B is A's side from before the pin");
    }
    {   /* a side the snapshot lacks comes from the row */
        std::vector<PlayerSide> snap(1); snap[0].slot = 0; snap[0].rel = 12.0f; snap[0].flags = 0;
        std::vector<RecStance> row(2); row[0].slot = 0; row[0].rel = 100.0f; row[0].flags = 1; row[1].slot = 6; row[1].rel = -5.0f; row[1].flags = 2;
        const std::vector<PlayerSide> m = MergeSides(snap, row);
        if (m.size() != 2 || m[0].rel != 12.0f || m[0].flags != 0 || m[1].slot != 6 || m[1].rel != -5.0f || m[1].flags != 2) Fail("the row fills only the players the snapshot lacks");
        if (MergeSides(std::vector<PlayerSide>(), row).size() != 2) Fail("no snapshot sides (a plain pp.faction snapshot): every recorded side");
    }
    {   /* the team ends with the departure: the founder's side row is made */
        Book b = T546cBook(); b.teams[0].members.pop_back(); SeedDecide(&b, "pa", T546cRec());
        SideDecide(&b, "pa", 0, "pc", 2, -30.0f, 0, &k);
        Owed made; std::vector<Owed> sideRows; TeamEnd end;
        if (LeaveDecide(&b, "pc", 100, &made, 0, &sideRows, 0, 0, &end) != kOk || !end.ended || sideRows.size() != 1 || sideRows[0].id != "pa" || sideRows[0].sides[0].rel != -30.0f)
            Fail("a team of two ends: the founder's own side towards the leaver comes back by its row");
    }
    {   /* the team stays but the founder holds no recorded side towards the leaver: the record carries no stance - side rows */
        Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
        SideDecide(&b, "pd", 4, "pc", 2, 5.0f, 0, &k);
        Owed made; std::vector<Owed> sideRows;
        if (LeaveDecide(&b, "pc", 100, &made, 0, &sideRows) != kOk || sideRows.size() != 1 || sideRows[0].id != "pd") Fail("no stance in the record: the remaining member's own side comes back by its row");
    }
    {   /* StanceBackOnDeparture replaces a stance the record holds towards that slot */
        Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
        StanceDecide(&b, "pa", 2, false, 7.0f, 1);
        SideDecide(&b, "pa", 0, "pc", 2, -20.0f, 2, &k);
        const unsigned g0 = b.teams[0].rec.gen;
        Owed made; std::vector<Owed> sideRows;
        LeaveDecide(&b, "pc", 100, &made, 0, &sideRows);
        const TeamRec& r = b.teams[0].rec;
        if (r.stances.size() != 2 || r.stances[1].slot != 2 || r.stances[1].rel != -20.0f || r.stances[1].flags != 2 || r.gen != g0 + 1 || !sideRows.empty())
            Fail("a stance towards the leaver's slot is replaced by the founder's side (one new generation), and no side rows");
    }
    {   /* the caps: a record full of stances, or one at the size limit, takes no stance - the side rows are made instead */
        Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
        while (b.teams[0].rec.stances.size() < kMaxStances) { RecStance x; x.slot = 100 + (unsigned)b.teams[0].rec.stances.size(); b.teams[0].rec.stances.push_back(x); }
        SideDecide(&b, "pa", 0, "pc", 2, -20.0f, 0, &k); SideDecide(&b, "pd", 4, "pc", 2, 5.0f, 0, &k);
        const unsigned g0 = b.teams[0].rec.gen;
        Owed made; std::vector<Owed> sideRows;
        if (LeaveDecide(&b, "pc", 100, &made, 0, &sideRows) != kOk || b.teams[0].rec.gen != g0 || b.teams[0].rec.stances.size() != kMaxStances || sideRows.size() != 2)
            Fail("kMaxStances: no stance added, the record unchanged; A and D get their side rows");
        Book z = T546cBook();
        TeamRec big; big.seeded = 1;
        RecRow one; one.sid = "pad-1000"; one.have = 3;
        const size_t base = RecBytes(big);
        big.rows.push_back(one);
        const size_t rowBytes = RecBytes(big) - base;
        const size_t n = (kMaxSnapshot - base) / rowBytes;
        big.rows.clear();
        for (size_t i = 0; i < n; ++i) { RecRow x; x.sid = "pad-" + I(1000 + (int)i); x.have = 3; big.rows.push_back(x); }
        big.rows.back().sid += std::string(kMaxSnapshot - RecBytes(big), 'p');
        if (RecBytes(big) != kMaxSnapshot) { Fail("the padded record is exactly at the size limit"); return; }
        z.teams[0].rec = big;
        SideDecide(&z, "pa", 0, "pc", 2, -20.0f, 0, &k);
        std::vector<Owed> zr;
        if (LeaveDecide(&z, "pc", 100, &made, 0, &zr) != kOk || z.teams[0].rec.gen != big.gen || !z.teams[0].rec.stances.empty() || RecBytes(z.teams[0].rec) != kMaxSnapshot || zr.size() != 1)
            Fail("kMaxSnapshot: a stance that would grow the record past the limit is not taken; the founder gets its side row");
    }
    {   /* a deleted member's profile: the team that stays takes the founder's side as its stance */
        Book b = T546cBook(); SeedDecide(&b, "pa", T546cRec());
        SideDecide(&b, "pa", 0, "pc", 2, -20.0f, 2, &k);
        const unsigned g0 = b.teams[0].rec.gen;
        std::vector<Invite> gone; std::string name, nf; unsigned dropped = 0;
        if (ProfileDeletedDecide(&b, "pc", &gone, &name, &nf, &dropped) != kDelMember || b.teams[0].rec.stances.size() != 2 || b.teams[0].rec.stances[1].slot != 2
            || b.teams[0].rec.stances[1].rel != -20.0f || b.teams[0].rec.gen != g0 + 1 || !b.teams[0].sides.empty())
            Fail("a deleted member: StanceBackOnDeparture as for a leave, its sides dropped");
    }
    {   /* the fill waits only for the players held rows put a side back towards; a row this build cannot read holds back no other */
        std::vector<Down> held(2);
        PreJoin pj; PlayerSide a; a.slot = 0; a.rel = 1.0f; pj.players.push_back(a); a.slot = 4; pj.players.push_back(a);
        EncodePreJoin(pj, &held[0].snap);
        RecStance st; st.slot = 6; held[0].sides.push_back(st);
        PutU32(&held[1].snap, kPreJoinMagic); PutU32(&held[1].snap, 9);
        st.slot = 7; held[1].sides.push_back(st); st.slot = 4; held[1].sides.push_back(st);
        PreJoin q;
        if (DecodePreJoin(held[1].snap, &q) != kPjVersion) Fail("a snapshot of a version this build does not know");
        const std::vector<unsigned> w = HeldSideSlots(held);
        if (w.size() != 4 || w[0] != 0 || w[1] != 4 || w[2] != 6 || w[3] != 7) Fail("the held rows' players: recorded sides and readable snapshots, ascending, once each");
        std::vector<PlayerSide> mine(3); mine[0].slot = 3; mine[1].slot = 5; mine[2].slot = 7;
        const std::vector<unsigned> miss = StancesMissing(mine, std::vector<RecStance>(), std::vector<unsigned>(), 0, w);
        if (miss.size() != 2 || miss[0] != 3 || miss[1] != 5) Fail("the founder's fill goes on for every player no held row names");
        if (!HeldSideSlots(std::vector<Down>()).empty()) Fail("no held rows: nobody waits");
    }
    /* a newer own change stands */
    if (OwnSideStands(0, false, 0, 5) || !OwnSideStands(6, false, 0, 5) || OwnSideStands(5, false, 0, 5)) Fail("an own change after the row arrived stands; none, or an older one, does not");
    if (!OwnSideStands(4, true, 3, 5) || OwnSideStands(3, true, 3, 5) || OwnSideStands(4, true, 7, 5)) Fail("counted from the pin's end or the row's arrival, whichever came first");
}
/* ---- T-546 step 4 (t546b): what membership does on each game - src/common/teameffect.h ---- */
std::vector<swteam::WireTeam> T546bTable()
{
    std::vector<swteam::WireTeam> t(2);
    t[0].no = 4; t[0].founderSlot = 0; t[0].name = "The Rusty Blades"; t[0].memberSlots.push_back(2); t[0].memberSlots.push_back(1);
    t[1].no = 7; t[1].founderSlot = 3; t[1].name = "Dust Kings";
    return t;
}
std::string T546bList(const std::vector<unsigned>& v) { std::string s; for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + I((int)v[i]); return s; }
void t_t546b_teammates_and_index()
{
    using namespace swteam;
    const std::vector<WireTeam> t = T546bTable();
    if (T546bList(TeammatesOf(t, 1)) != "0,2") Fail("a member's teammates: the founder and the other member, ascending - " + T546bList(TeammatesOf(t, 1)));
    if (T546bList(TeammatesOf(t, 0)) != "1,2") Fail("the founder's teammates: its members");
    if (!TeammatesOf(t, 3).empty()) Fail("a founder alone has no teammates");
    if (!TeammatesOf(t, 9).empty() || !TeammatesOf(std::vector<WireTeam>(), 0).empty()) Fail("a player in no team, or no table, has none");
    if (!SameTeam(t, 0, 2) || !SameTeam(t, 2, 1)) Fail("two of one team");
    if (SameTeam(t, 1, 1) || SameTeam(t, 2, 3) || SameTeam(t, 3, 9) || SameTeam(t, 8, 9)) Fail("one player, two teams, or no team is not the same team");
    std::vector<WireTeam> big = t; big[1].memberSlots.push_back(kSlotIndexCap + 5);
    const std::vector<unsigned> idx = TeamIndex(big);
    if (idx.size() != kSlotIndexCap || idx[0] != 4 || idx[1] != 4 || idx[2] != 4 || idx[3] != 7 || idx[5] != 0) Fail("the slot index by team number");
    std::vector<WireTeam> zero(1); zero[0].no = 0; zero[0].founderSlot = 6;
    if (TeamIndex(zero)[6] == 0) Fail("a team numbered 0 still marks its players as in a team");
}
void t_t546b_pin_start_end()
{
    using namespace swteam;
    /* A (slot 0) invites C (slot 2), C accepts, C leaves - as C's game sees the tables */
    std::vector<WireTeam> joined(1); joined[0].no = 1; joined[0].founderSlot = 0; joined[0].memberSlots.push_back(2);
    std::vector<WireTeam> left(1); left[0].no = 1; left[0].founderSlot = 0;
    std::vector<unsigned> pinned;
    PinPlan p = PlanPins(pinned, TeammatesOf(joined, 2));
    if (T546bList(p.start) != "0" || !p.end.empty() || !p.keep.empty()) Fail("joining starts the pin towards the founder");
    pinned = TeammatesOf(joined, 2);
    p = PlanPins(pinned, TeammatesOf(joined, 2));
    if (!p.start.empty() || !p.end.empty() || T546bList(p.keep) != "0") Fail("the same table again keeps the pin and starts nothing");
    p = PlanPins(pinned, TeammatesOf(left, 2));
    if (!p.start.empty() || T546bList(p.end) != "0" || !p.keep.empty()) Fail("leaving ends the pin");
    /* the founder's side: a second member joins, the first is removed */
    std::vector<unsigned> a; a.push_back(2);
    std::vector<unsigned> now; now.push_back(1);
    p = PlanPins(a, now);
    if (T546bList(p.start) != "1" || T546bList(p.end) != "2" || !p.keep.empty()) Fail("one member in, another out");
    std::vector<unsigned> both; both.push_back(1); both.push_back(3);
    p = PlanPins(now, both);
    if (T546bList(p.start) != "3" || T546bList(p.keep) != "1" || !p.end.empty()) Fail("a third player joins");
    p = PlanPins(both, std::vector<unsigned>());
    if (T546bList(p.end) != "1,3" || !p.start.empty()) Fail("a disband ends every pin");
    /* the value */
    if (PinNeedsWrite(true, 100.0f) || PinNeedsWrite(true, 99.6f)) Fail("a side at the pin is not written");
    if (!PinNeedsWrite(true, 99.4f) || !PinNeedsWrite(true, 0.0f) || !PinNeedsWrite(true, -100.0f)) Fail("a side below the pin is written back");
    if (PinNeedsWrite(false, -100.0f)) Fail("an unread side is not written");
    /* a stance towards a teammate */
    if (!StanceAllowedTowards(true, 0) || StanceAllowedTowards(true, 1) || StanceAllowedTowards(true, 2)) Fail("towards a teammate only ally");
    if (!StanceAllowedTowards(false, 0) || !StanceAllowedTowards(false, 1) || !StanceAllowedTowards(false, 2)) Fail("towards anyone else any stance");
}
void t_t546b_tag_look()
{
    using namespace swteam;
    if (TagColourClass(true) != kTagTeam || TagColourClass(false) != kTagByStanding) Fail("the colour class");
    if (TagFactionLine("Nameless 3", false, "The Rusty Blades", "The Rusty Blades") != "Nameless 3") Fail("a player in no team shows its own faction");
    if (TagFactionLine("Nameless 3", true, "The Rusty Blades", "Old Name") != "The Rusty Blades") Fail("a player in a team shows the founder's faction name as it is now");
    if (TagFactionLine("Nameless 3", true, " \t", "Old Name") != "Old Name") Fail("no founder name here: the name the team was made with");
    if (TagFactionLine("Nameless 3", true, "", "") != "Nameless 3") Fail("no name at all: its own");
    const nametag::Rgb b = nametag::TeamColour();
    if ((int)(b.r * 255.0f + 0.5f) != 0x5a || (int)(b.g * 255.0f + 0.5f) != 0x9b || (int)(b.b * 255.0f + 0.5f) != 0xe6) Fail("team blue is #5a9be6");
    const nametag::Rgb h = nametag::TagColour(nametag::kHostile, true), r = nametag::TagColour(nametag::kHostile, false), x = nametag::LevelColour(nametag::kHostile);
    if (h.r != b.r || h.g != b.g || h.b != b.b) Fail("a teammate is blue whatever the standing reads");
    if (r.r != x.r || r.g != x.g || r.b != x.b) Fail("anyone else has the standing's colour");
}
void t_t546b_member_access()
{
    using namespace swteam;
    if (!MemberOpens(0, 2, 4, 4)) Fail("a teammate's building opens");
    if (MemberOpens(0, 0, 4, 4)) Fail("the owner is not a member's case");
    if (MemberOpens(0, 2, 4, 7) || MemberOpens(0, 2, 0, 0) || MemberOpens(0, 2, 4, 0)) Fail("different teams, or none, do not open");
    if (MemberOpens(-1, 2, 4, 4) || MemberOpens(0, -1, 4, 4)) Fail("an unknown owner or asker does not open");
    /* from a table: A founded team 4 with C; B is in none */
    const std::vector<unsigned> idx = TeamIndex(T546bTable());
    if (!MemberOpens(0, 1, idx[0], idx[1]) || MemberOpens(0, 3, idx[0], idx[3]) || MemberOpens(0, 5, idx[0], idx[5])) Fail("the index's answers");
}
void t_t546b_notice_names()
{
    using namespace swteam;
    if (PlayerWords("Sam", "Old", "Dust Kings", 1) != "Sam") Fail("the roster's name first");
    if (PlayerWords("", "Sam", "Dust Kings", 1) != "Sam") Fail("a player who left: the name the roster gave it");
    if (PlayerWords("", "", "Dust Kings", 1) != "Dust Kings") Fail("never seen this session: its faction name");
    if (PlayerWords("", "", "", 1) != "s1") Fail("nothing known: the slot");
}
/* step 4b: the box stub policy.cpp writes over buildingSelected's inline owner test (1.0.65 0x7FC241 `cmp qword [rax+0x250],0`,
   then `jne` LOOT): 16 teammate cells, the original compare first, back to the jne at site + 8 */
void t_t546b_box_stub()
{
    unsigned char p[0x400];
    std::memset(p, 0xCC, sizeof(p));
    const unsigned long long site = 0x1407FC241ull;
    const int n = coopcrime::SightStubBuild(p, site, 16, 0x400);
    if (n != 50 + 13 * 16 || n > 0x400) Fail("the 16-cell stub's length " + I(n));
    static const unsigned char test[8] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00 };
    if (std::memcmp(p, test, 8) != 0 || p[8] != 0x0F || p[9] != 0x85) Fail("the original compare first, a player faction straight back");
    const int N = 14 + 13 * 16;
    if (std::memcmp(p + N, test, 8) != 0) Fail("a non-teammate: the original compare again (ZF=1, the steal line as before)");
    unsigned long long back = 0; std::memcpy(&back, p + N + 28, 8);
    if (back != site + 8) Fail("back to the jne at site + 8");
    int disp = 0; std::memcpy(&disp, p + 14 + 13 * 15 + 3, 4);
    if (14 + 13 * 15 + 7 + disp != 0x400 + 8 + 8 * 15) Fail("cell 15's compare reads cell 15");
}
/* the PLAYERS tab towards a teammate: ALLY ticked, NEUTRAL and HOSTILE disabled and doing nothing, no box */
void t_t546b_players_tab_teammate()
{
    using namespace playerstab;
    RowIn in; in.slot = 2; in.rosterName = "Sam"; in.faction = "Dust Kings"; in.you = nametag::kNeutral; in.them = nametag::kFriendly; in.teammate = true;
    std::vector<RowView> rows; rows.push_back(RowFor(in));
    Bottom b = BottomFor(rows, 2);
    if (!b.selected || !b.locked || b.ticked != 0) Fail("a teammate's row: ALLY shown ticked, locked");
    if (!ButtonEnabled(b, 0) || ButtonEnabled(b, 1) || ButtonEnabled(b, 2)) Fail("only ALLY can be pressed towards a teammate");
    if (PressForRow(1, rows[0].you, true) != kPressNothing || PressForRow(2, rows[0].you, true) != kPressNothing) Fail("NEUTRAL / HOSTILE towards a teammate do nothing (no box)");
    if (PressForRow(0, nametag::kNeutral, true) != kPressSet || PressForRow(0, nametag::kFriendly, true) != kPressNothing) Fail("ALLY towards a teammate sets ally only when not there yet");
    if (std::string(NothingWhy(2, true)).find("teammate") == std::string::npos || std::string(NothingWhy(0, false)) != "nothing (already ticked)") Fail("the log's reason for a press that did nothing");
    in.teammate = false; rows[0] = RowFor(in); b = BottomFor(rows, 2);
    if (b.locked || b.ticked != nametag::kNeutral || !ButtonEnabled(b, 1) || !ButtonEnabled(b, 2)) Fail("anyone else: the own level ticked, every button");
    if (PressForRow(2, nametag::kNeutral, false) != kPressConfirm || PressForRow(1, nametag::kFriendly, false) != kPressSet) Fail("anyone else: the presses as before");
    b = BottomFor(rows, 9);
    if (b.locked || b.selected) Fail("nothing selected is not locked");
}

/* ---- T-546 step 5 fold 2: which entry of an NPC standing each road writes, for a player faction and for an NPC faction
   (src/common/teamstanding.h NpcDirOfWritten / NpcDirOfCall / NpcEntriesOf over relside.h) ---- */
/* T-546 step 5 fold 3: the final save at quit runs on the loop's own thread (asked of it from another thread, never before it
   starts) and saves only a file with unsaved changes that may be rewritten; a side write that failed for any reason but
   "no faction of that player here" is tried again */
/* ---- T-546 step 6 (t546d): the shared research - teamwire.h RESEARCH / Q lines / ResearchDecide, src/common/teamresearch.h ---- */
std::vector<std::string> T546dV(const char* a, const char* b = 0, const char* c = 0, const char* d = 0)
{
    std::vector<std::string> v; v.push_back(a); if (b) v.push_back(b); if (c) v.push_back(c); if (d) v.push_back(d); return v;
}
swteam::Book T546dBook()
{
    /* team 1: founder A, member B; C in no team */
    swteam::Book b; swteam::Team t; t.no = 1; t.founder = "A"; t.name = "The Rusty Blades";
    swteam::Member m; m.id = "B"; t.members.push_back(m);
    b.teams.push_back(t); b.nextTeam = 2;
    return b;
}
void t_t546d_union_decide()
{
    using namespace swteam;
    Book b = T546dBook(); std::vector<std::string> added;
    if (ResearchDecide(&b, "C", T546dV("x"), &added) != swteam::kRefNotInTeam || !added.empty()) Fail("a player in no team adds nothing (not-in-team)");
    if (ResearchDecide(&b, "A", T546dV("t3", "t1", "t2", "t1"), &added) != swteam::kOk || added != T546dV("t1", "t2", "t3")) Fail("the founder's whole list: every tech new, sorted, once");
    if (b.teams[0].research != T546dV("t1", "t2", "t3")) Fail("the team holds the founder's techs");
    if (ResearchDecide(&b, "B", T546dV("t2", "t4"), &added) != swteam::kOk || added != T546dV("t4")) Fail("the member's list adds only what the team lacks (the union)");
    if (b.teams[0].research != T546dV("t1", "t2", "t3", "t4")) Fail("the union is sorted and unique");
    if (ResearchDecide(&b, "B", T546dV("t4", "t1"), &added) != swteam::kOk || !added.empty()) Fail("techs the team holds add nothing - nothing to save or send (no echo)");
    if (ResearchDecide(&b, "A", T546dV("ok", ""), &added) != swteam::kRefTooMany || b.teams[0].research.size() != 4) Fail("an empty stringID refuses the whole list");
    std::vector<std::string> many; for (unsigned i = 0; i <= kMaxTechs; ++i) many.push_back("m" + swteam::U(i));
    if (ResearchDecide(&b, "A", many, &added) != swteam::kRefTooMany || b.teams[0].research.size() != 4) Fail("more than kMaxTechs refuses the whole list");
    /* leaving takes nothing away: the team keeps its research, the leaver's game keeps its own (nothing is sent to remove) */
    Owed made; std::vector<Invite> gone;
    { Member d2; d2.id = "D"; b.teams[0].members.push_back(d2); }   /* a second member: B's leave does not end the team */
    if (LeaveDecide(&b, "B", 100, &made, &gone) != swteam::kOk || b.teams[0].research.size() != 4) Fail("a member leaving leaves the team's research as it was");
    if (ResearchDecide(&b, "B", T546dV("t9"), &added) != swteam::kRefNotInTeam) Fail("after leaving, the player's finishes no longer reach the team");
}
void t_t546d_wire_and_file()
{
    using namespace swteam;
    std::vector<char> b; Up u; Down d;
    if (!EncodeResearchUp(&b, true, T546dV("1962-gamedata.base", "2087-gamedata.base")) || !DecodeUp(&b[0], b.size(), &u) || u.kind != kUpResearch || u.whole != 1
        || u.techs != T546dV("1962-gamedata.base", "2087-gamedata.base")) Fail("RESEARCH up (whole) round-trips");
    if (!EncodeResearchUp(&b, false, T546dV("1938-gamedata.base")) || !DecodeUp(&b[0], b.size(), &u) || u.whole != 0 || u.techs.size() != 1) Fail("RESEARCH up (own) round-trips");
    if (!EncodeResearchUp(&b, true, std::vector<std::string>()) || !DecodeUp(&b[0], b.size(), &u) || !u.techs.empty()) Fail("an empty whole list travels (a game with nothing finished)");
    if (EncodeResearchUp(&b, false, T546dV(""))) Fail("an empty stringID is not encoded");
    if (!EncodeResearchDown(&b, 7, false, 3, T546dV("t1")) || !DecodeDown(&b[0], b.size(), &d) || d.kind != kDnResearch || d.resTeam != 7 || d.resWhole != 0 || d.resBy != 3
        || d.techs != T546dV("t1")) Fail("RESEARCH down (a member's finish, with the finisher's slot) round-trips");
    if (!EncodeResearchDown(&b, 7, true, 3, T546dV("t1", "t2")) || !DecodeDown(&b[0], b.size(), &d) || d.resWhole != 1 || d.resBy != 3 || d.techs.size() != 2)
        Fail("RESEARCH down (whole) names the game it answers");
    if (!EncodeResearchDown(&b, 7, true, 0xFFFFFFFFu, T546dV("t1")) || !DecodeDown(&b[0], b.size(), &d) || d.resBy != 0xFFFFFFFFu) Fail("RESEARCH down (whole for all) names no game");
    EncodeResearchDown(&b, 7, true, 0, T546dV("t1")); b.push_back(0);
    if (DecodeDown(&b[0], b.size(), &d)) Fail("a trailing byte is malformed");
    EncodeResearchUp(&b, true, T546dV("t1")); b[1] = 2;
    if (DecodeUp(&b[0], b.size(), &u)) Fail("a whole flag other than 0 / 1 is malformed");
    /* teams.txt: Q lines after the team's T line, read back to the same union; a duplicate and an orphan are refused */
    Book k = T546dBook(); std::vector<std::string> added;
    ResearchDecide(&k, "A", T546dV("1962-gamedata.base", "2087-gamedata.base"), &added);
    const std::string text = BookFile(k);
    if (text.find("Q 1 ") == std::string::npos) Fail("the team's research is written as Q lines");
    Book r; std::istringstream in(text); std::string line; int bad = 0;
    while (std::getline(in, line)) if (!BookParseLine(line, &r)) ++bad;
    if (bad != 0 || r.teams.size() != 1 || r.teams[0].research != k.teams[0].research || BookFile(r) != text) Fail("teams.txt's research reads back the same");
    int why = swteam::kLineOk;
    if (BookParseLine("Q 1 " + Hex(std::string("1962-gamedata.base")), &r, &why) || why != swteam::kLineDuplicate) Fail("a tech written twice is a duplicate");
    if (BookParseLine("Q 9 " + Hex(std::string("x")), &r)) Fail("a Q line of no team is refused");
    if (BookParseLine("Q 1 zz", &r)) Fail("a Q line that is not hex is refused");
}
void t_t546d_game_rules()
{
    using namespace swteam;
    /* which techs are new to whom: the team's, minus mine, minus what this game's data lacks */
    std::set<std::string> known; known.insert("t1"); known.insert("t2"); known.insert("t3");
    if (ResearchNewToMe(T546dV("t3", "t1", "t2", "zz"), T546dV("t1"), known) != T546dV("t2", "t3")) Fail("new to me = the team's minus mine, known here, sorted");
    if (!ResearchNewToMe(T546dV("t1"), T546dV("t1"), known).empty()) Fail("a tech I have is not new");
    if (!ResearchNewToMe(T546dV("zz"), std::vector<std::string>(), known).empty()) Fail("a tech this game's data lacks is never loaded");
    /* THE RECORD: own fields kept, the queue unchanged, the new techs after "num finished" */
    coopown::ResearchRec own;
    own.f.push_back(std::make_pair(std::string("num currents"), 1.0f));
    own.f.push_back(std::make_pair(std::string("num finished"), 2.0f));
    own.f.push_back(std::make_pair(std::string("current prog0"), 0.4f));
    own.s.push_back(std::make_pair(std::string("current0"), std::string("q1")));
    own.s.push_back(std::make_pair(std::string("finished0"), std::string("t1")));
    own.s.push_back(std::make_pair(std::string("finished1"), std::string("t0")));
    own.s.push_back(std::make_pair(std::string("finished2"), std::string("stale")));   /* past "num finished": not a finished tech */
    coopown::SortResearch(&own);
    coopown::ResearchRec out; int added = 0, dropped = 0;
    ResearchMergeRecord(own, T546dV("t2", "t1", "t3", "t2"), &out, &added, &dropped);
    if (added != 2 || dropped != 0) Fail("two techs appended (t1 already finished, t2 once); the queued q1 is not finished and stays");
    const std::vector<std::string> fin = coopown::ResearchFinishedSids(out);
    if (fin != T546dV("t1", "t0", "t2", "t3")) Fail("finished = mine in order, then the new ones (the stale entry overwritten)");
    float nf = 0, nc = 0, prog = 0; std::string cur;
    for (size_t i = 0; i < out.f.size(); ++i) { if (out.f[i].first == "num finished") nf = out.f[i].second; if (out.f[i].first == "num currents") nc = out.f[i].second; if (out.f[i].first == "current prog0") prog = out.f[i].second; }
    for (size_t i = 0; i < out.s.size(); ++i) if (out.s[i].first == "current0") cur = out.s[i].second;
    if (nf != 4.0f) Fail("num finished raised to 4");
    if (nc != 1.0f || cur != "q1" || prog != 0.4f) Fail("the queue (num currents, current0, its progress) is kept unchanged");
    if (out.f.size() != own.f.size() || out.s.size() != own.s.size() + 1) Fail("no field is dropped; one finished entry added beside the overwritten stale one");
    std::set<std::string> res(fin.begin(), fin.end());
    if (coopown::ResearchRestoreVerdict(out, res) != coopown::kRsRestore) Fail("the merged record is one Research::load takes (no _Default Start fall-back)");
    coopown::ResearchRec same; ResearchMergeRecord(own, T546dV("t1", "t0"), &same, &added, &dropped);
    if (added != 0 || dropped != 0 || coopown::ResearchFinishedSids(same) != coopown::ResearchFinishedSids(own)) Fail("nothing new: the record is not changed");
    coopown::ResearchRec bare, bareOut; ResearchMergeRecord(bare, T546dV("t5"), &bareOut, &added, &dropped);
    if (added != 1 || coopown::ResearchFinishedSids(bareOut) != T546dV("t5")) Fail("a record with no num finished gets one");
    /* THE ECHO RULE */
    std::set<std::string> applied, held; applied.insert("fromMate"); held.insert("teamHas");
    if (ResearchOwnToSend(T546dV("mine", "fromMate", "teamHas", "mine"), applied, held) != T546dV("mine")) Fail("only this player's own finishes go up, once");
    if (!ResearchOwnToSend(T546dV("fromMate"), applied, held).empty()) Fail("a tech applied from a teammate is never re-sent as this player's own");
    /* the team the message is for; when the whole list is due */
    if (!ResearchForMyTeam(3, 3) || ResearchForMyTeam(0, 3) || ResearchForMyTeam(4, 3)) Fail("a research message is applied only to the team this player is in now");
    if (!ResearchWholeDue(3, 0, -1, 5) || ResearchWholeDue(3, 3, 5, 5) || !ResearchWholeDue(3, 3, 4, 5) || !ResearchWholeDue(3, 2, 5, 5) || ResearchWholeDue(0, 0, -1, 5))
        Fail("the whole list goes once per load, link and team, and never outside a team");
    /* the approved lines */
    if (ResearchSharedLine(6) != "Shared research: 6 new technologies.") Fail("the shared-research line is the approved one");
    if (ResearchSharedLine(1) != "Shared research: 1 new technology.") Fail("the singular shared-research line is the approved one (515)");
}
/* a research record with a queue: finished f0.., queued (sid, progress) front first */
coopown::ResearchRec T546dRec(const std::vector<std::string>& fin, const std::vector<std::pair<std::string, float> >& queue)
{
    coopown::ResearchRec r; char k[48];
    r.f.push_back(std::make_pair(std::string("num finished"), (float)fin.size()));
    r.f.push_back(std::make_pair(std::string("num currents"), (float)queue.size()));
    for (size_t i = 0; i < fin.size(); ++i) { std::sprintf(k, "finished%d", (int)i); r.s.push_back(std::make_pair(std::string(k), fin[i])); }
    for (size_t i = 0; i < queue.size(); ++i)
    {
        std::sprintf(k, "current%d", (int)i); r.s.push_back(std::make_pair(std::string(k), queue[i].first));
        std::sprintf(k, "current prog%d", (int)i); r.f.push_back(std::make_pair(std::string(k), queue[i].second));
    }
    r.s.push_back(std::make_pair(std::string("paid0"), std::string("X")));   /* another field: kept as it is */
    coopown::SortResearch(&r);
    return r;
}
std::vector<std::pair<std::string, float> > T546dQ(const char* a, float pa, const char* b = 0, float pb = 0, const char* c = 0, float pc = 0)
{
    std::vector<std::pair<std::string, float> > q; q.push_back(std::make_pair(std::string(a), pa));
    if (b) q.push_back(std::make_pair(std::string(b), pb)); if (c) q.push_back(std::make_pair(std::string(c), pc));
    return q;
}
std::string T546dQText(const std::vector<swteam::QueuedTech>& q)
{
    std::string o; char p[32];
    for (size_t i = 0; i < q.size(); ++i) { std::sprintf(p, "%.2f", q[i].prog); o += (i ? " " : "") + q[i].sid + ":" + p; }
    return o;
}
void t_t546d_fold1()
{
    using namespace swteam;
    /* THE QUEUE as Research::save writes it, front first, with each entry's progress */
    const coopown::ResearchRec r = T546dRec(T546dV("f0"), T546dQ("X", 0.5f, "Y", 0.25f, "Z", 0.0f));
    if (T546dQText(ResearchQueueOf(r)) != "X:0.50 Y:0.25 Z:0.00") Fail("the queue is read front first with its progress");
    if (!ResearchIndexKey("current12", "current") || ResearchIndexKey("current prog0", "current") || !ResearchIndexKey("current prog0", "current prog") || ResearchIndexKey("current", "current"))
        Fail("current<i> and current prog<i> are told apart");
    /* THE ROAD a teammate's tech takes, from the queue as it stands */
    const std::vector<QueuedTech> q = ResearchQueueOf(r);
    if (ResearchApplyRoute(q, "W") != kResViaSetResearched) Fail("not queued here: setResearched");
    if (ResearchApplyRoute(q, "X") != kResViaSetResearched) Fail("the queue's front: setResearched (it pops the front, the matching entry)");
    if (ResearchApplyRoute(q, "Y") != kResViaLoad) Fail("queued behind the front: Research::load (setResearched would pop X)");
    if (ResearchApplyRoute(q, "Z") != kResViaLoad) Fail("queued several behind: Research::load");
    if (ResearchApplyRoute(std::vector<QueuedTech>(), "X") != kResViaSetResearched) Fail("an empty queue: setResearched");
    std::vector<QueuedTech> twice = ResearchQueueOf(T546dRec(T546dV("f0"), T546dQ("X", 0.5f, "Y", 0.1f, "X", 0.0f)));
    if (ResearchApplyRoute(twice, "X") != kResViaLoad) Fail("at the front and again behind: Research::load (setResearched would pop twice)");
    /* the record for a tech queued behind the front: finished, out of the queue, X kept at the front with its progress */
    coopown::ResearchRec out; int added = 0, dropped = 0;
    ResearchMergeRecord(r, T546dV("Y"), &out, &added, &dropped);
    if (added != 1 || dropped != 1) Fail("Y appended as finished and taken out of the queue");
    if (coopown::ResearchFinishedSids(out) != T546dV("f0", "Y")) Fail("Y finished once, after this game's own");
    if (T546dQText(ResearchQueueOf(out)) != "X:0.50 Z:0.00") Fail("the queue keeps X at the front with its progress, then Z, renumbered");
    int stale = 0; for (size_t i = 0; i < out.s.size(); ++i) if (out.s[i].first == "current2") ++stale;
    for (size_t i = 0; i < out.f.size(); ++i) if (out.f[i].first == "current prog2") ++stale;
    if (stale != 0) Fail("no entry is left past the renumbered queue");
    bool paid = false; for (size_t i = 0; i < out.s.size(); ++i) if (out.s[i].first == "paid0" && out.s[i].second == "X") paid = true;
    if (!paid) Fail("every other field is kept");
    ResearchMergeRecord(r, T546dV("Z", "X"), &out, &added, &dropped);
    if (added != 2 || dropped != 2 || T546dQText(ResearchQueueOf(out)) != "Y:0.25") Fail("several queued techs finished: the one left is the front, its progress kept");
    /* THE JOIN: the team holds a tech this game has queued - it leaves the queue; a queue with none of the team's is untouched */
    ResearchMergeRecord(r, T546dV("t1", "Z", "t2"), &out, &added, &dropped);
    if (added != 3 || dropped != 1 || T546dQText(ResearchQueueOf(out)) != "X:0.50 Y:0.25") Fail("at the join a queued tech the team holds leaves the queue, the rest kept in order");
    ResearchMergeRecord(r, T546dV("t1"), &out, &added, &dropped);
    if (added != 1 || dropped != 0 || T546dQText(ResearchQueueOf(out)) != "X:0.50 Y:0.25 Z:0.00") Fail("no queued tech finished: the queue unchanged");
    ResearchMergeRecord(r, T546dV("X", "Y", "Z"), &out, &added, &dropped);
    float nc = -1;
    for (size_t i = 0; i < out.f.size(); ++i) if (out.f[i].first == "num currents") nc = out.f[i].second;
    if (dropped != 3 || nc != 0.0f || !ResearchQueueOf(out).empty()) Fail("every queued tech finished: an empty queue (num currents 0)");
    /* THE ANSWERS and the resend wait */
    if (ResearchDownKind(true, 2, 2) != kResDnWholeAnswer || ResearchDownKind(true, 0xFFFFFFFFu, 2) != kResDnTeamWhole || ResearchDownKind(true, 3, 2) != kResDnTeamWhole
        || ResearchDownKind(false, 2, 2) != kResDnOwnAnswer || ResearchDownKind(false, 3, 2) != kResDnMateTech || ResearchDownKind(false, 2, -1) != kResDnMateTech)
        Fail("a RESEARCH message is read as an answer to this game, the team's research, or a teammate's tech");
    if (ResearchMinus(T546dV("a", "b", "c"), T546dV("b", "x")) != T546dV("a", "c")) Fail("an answer leaves only what it did not cover");
    if (ResearchResendWaitMs(0) != 2000 || ResearchResendWaitMs(1) != 4000 || ResearchResendWaitMs(4) != 32000 || ResearchResendWaitMs(5) != 60000 || ResearchResendWaitMs(40) != 60000)
        Fail("the resend wait grows from 2 s and stops at 60 s");
    /* a refusal travels as a NOTICE with its own event */
    std::vector<char> b; Down d;
    EncodeNotice(&b, kEvResearchWhole, kRefNotSaved, 2, 2, std::string());
    if (!DecodeDown(&b[0], b.size(), &d) || d.kind != kDnNotice || d.event != (unsigned)kEvResearchWhole || d.result != (unsigned)kRefNotSaved || d.actorSlot != 2)
        Fail("a refused whole list is a NOTICE research-whole with its reason");
    if (std::string(EventName(kEvResearchWhole)) != "research-whole" || std::string(EventName(kEvResearchOwn)) != "research-own") Fail("the research refusals are named");
}

}   /* namespace suite */
