/* src/coop-test/suite_part2.cpp - THE OFFLINE SUITE, part 2 of 7 (owner decision 1204).
 *
 * Older, settled tests moved here unchanged from test_main.cpp, in their old order, so that cl /MP8 compiles the
 * suite's files side by side. Each test keeps its place in the run: the order is kTests in test_main.cpp. What more
 * than one file uses is in suite.h. New tests go in test_main.cpp.
 */
#include "suite.h"

namespace suite {

/* bag23 part 2: MSG_PARITY_BOX carries packs' contents in a trailing BAGK block - round trip, absent (no pack holds anything), cut. */
void t_bag23_parity_box_packs()
{
    cooppar::ParityBox a;
    a.status = cooppar::kParityStatusContents; a.key = "box@1,2@3,4,5"; a.digest = 7;
    cooppar::ParityItem it; it.section = "main"; it.x = 0; it.y = 0; it.quantity = 1; it.baseSid = "pack";
    a.items.push_back(it);
    it.baseSid = "rock"; it.x = 3; a.items.push_back(it);
    std::vector<char> plain;
    if (!cooppar::EncodeParityBox(&plain, a)) { Fail("a box with no pack rows did not encode"); return; }
    coopbag::BagRow r; r.section = "backpack_content"; r.x = 1; r.y = 2; r.qty = 3; r.baseSid = "bread";
    a.items[0].bag.push_back(r);
    std::vector<char> b;
    if (!cooppar::EncodeParityBox(&b, a)) { Fail("a box with a pack's rows did not encode"); return; }
    if (b.size() <= plain.size()) Fail("the packs block added nothing");
    cooppar::ParityBox o;
    if (cooppar::DecodeParityBox(&b[0], b.size(), &o) != cooppar::kParityDecodeOk || o.items.size() != 2 || o.items[0].bag.size() != 1
        || o.items[0].bag[0].baseSid != "bread" || o.items[0].bag[0].qty != 3 || !o.items[1].bag.empty()) Fail("the pack rows did not round-trip");
    cooppar::ParityBox o2;
    if (cooppar::DecodeParityBox(&plain[0], plain.size(), &o2) != cooppar::kParityDecodeOk || !o2.items[0].bag.empty()) Fail("absent must read as no rows");
    cooppar::ParityBox o3;
    if (cooppar::DecodeParityBox(&b[0], b.size() - 1, &o3) == cooppar::kParityDecodeOk) Fail("a cut packs block must be refused");
    if (cooppar::ParityDigestOf(a.items) != cooppar::ParityDigestOf(o2.items)) Fail("pack rows must not change the digest");
}

/* bag23 part 1 (inv3 phase 2): the kit's packs block after the CLOTHING details - round trip after QLT1 with and without its
   OWNL tail and with no QLT1 at all, an older reader, every cut, cap+1, and the wrapper's own limits. */
static coopbag::BagRow Bag23Row(const char* sid, unsigned int x, unsigned int qty)
{
    coopbag::BagRow r;
    r.section = "backpack_content"; r.x = x; r.y = 1; r.qty = qty; r.baseSid = sid; r.quality = 0.5f; r.charges = 2.0f;
    return r;
}

/* kit2 (owner 102, protocol 83): the KCL1 cells block after the details (with and without a BAGK packs block), older readers,
   every cut, the limits, and the copy's placement rule (the owner's cell, else the section's first free cell, else none). */
static int Kit2Taken(void* ctx, int x, int y)   /* a 4x3 grid; ctx = 12 bytes, non-zero = that cell is taken (1x1 items) */
{
    return ((const char*)ctx)[y * 4 + x] != 0 ? 1 : 0;
}
void t_kit2_kit_cells_block()
{
    std::vector<coopkitcell::KitCell> cells;
    cells.push_back(coopkitcell::KitCell(0, 0)); cells.push_back(coopkitcell::KitCell(3, 1));
    cells.push_back(coopkitcell::KitCell(2, 7)); cells.push_back(coopkitcell::KitCell(4095, 4095));
    std::vector<coopgarment::GarmentDetail> d(4);
    for (size_t i = 0; i < d.size(); ++i) d[i].quantity = 1;
    d[1].owner.kind = 2; d[1].owner.uid = 5; d[1].owner.factionSid = "7-gamedata.base";
    std::vector<coopbag::KitBag> bags(1);
    bags[0].kitIndex = 2;
    bags[0].rows.push_back(Bag23Row("11-gamedata.base", 0, 3));
    /* 1: list + QLT1/OWNL + BAGK + KCL1 - every block found in turn */
    std::vector<char> b(10, 'x');
    coopgarment::EncodeGarmentDetails(&b, d);
    if (!coopbag::EncodeKitBags(&b, bags, 4)) { Fail("the packs did not encode"); return; }
    const size_t bagsEnd = b.size();
    if (!coopkitcell::EncodeKitCells(&b, cells, 4)) { Fail("four cells did not encode"); return; }
    size_t dEnd = 0, kEnd = 0, cEnd = 0;
    std::vector<coopgarment::GarmentDetail> od;
    std::vector<coopbag::KitBag> ob;
    std::vector<coopkitcell::KitCell> oc;
    if (coopgarment::DecodeGarmentDetailsEnd(&b[0], b.size(), 10, 4, &od, &dEnd) != coopgarment::kDetailOk
        || coopkitcell::KitCellsAt(&b[0], b.size(), dEnd)
        || coopbag::DecodeKitBags(&b[0], b.size(), dEnd, 4, &ob, &kEnd, 0) != coopbag::kKitBagOk || kEnd != bagsEnd
        || coopkitcell::DecodeKitCells(&b[0], b.size(), kEnd, 4, &oc, &cEnd) != coopkitcell::kKitCellOk || cEnd != b.size()
        || oc.size() != 4 || oc[1].x != 3 || oc[1].y != 1 || oc[2].y != 7 || oc[3].x != 4095 || od[1].owner.uid != 5 || ob.size() != 1)
        Fail("the cells block must round-trip LAST, after the details and the packs");
    /* 2: no packs - KCL1 right after the details; a reader that looks for KCL1 first finds it there */
    {
        std::vector<char> c(10, 'x');
        coopgarment::EncodeGarmentDetails(&c, d);
        const size_t de = c.size();
        if (!coopkitcell::EncodeKitCells(&c, cells, 4)) Fail("cells after the details did not encode");
        size_t e2 = 0, c2 = 0;
        std::vector<coopgarment::GarmentDetail> od2;
        std::vector<coopkitcell::KitCell> oc2;
        if (coopgarment::DecodeGarmentDetailsEnd(&c[0], c.size(), 10, 4, &od2, &e2) != coopgarment::kDetailOk || e2 != de
            || !coopkitcell::KitCellsAt(&c[0], c.size(), e2)
            || coopkitcell::DecodeKitCells(&c[0], c.size(), e2, 4, &oc2, &c2) != coopkitcell::kKitCellOk || c2 != c.size() || oc2.size() != 4)
            Fail("with no packs block the cells block must be found right after the details");
        std::vector<coopgarment::GarmentDetail> old;
        if (coopgarment::DecodeGarmentDetails(&c[0], c.size(), 10, 4, &old) != coopgarment::kDetailOk || old.size() != 4 || old[1].owner.uid != 5)
            Fail("an older reader must still read the details with the cells block after them");
    }
    /* 3: an older reader of a kit WITH packs reads the packs whole and never looks past them */
    {
        std::vector<coopbag::KitBag> ob3; size_t k3 = 0;
        if (coopbag::DecodeKitBags(&b[0], b.size(), dEnd, 4, &ob3, &k3, 0) != coopbag::kKitBagOk || k3 != bagsEnd || ob3.size() != 1)
            Fail("the packs block must still read whole with the cells block after it");
    }
    /* 4: nothing after the packs = absent; another block = absent (never taken for cells) */
    {
        std::vector<coopkitcell::KitCell> o4(1);
        if (coopkitcell::DecodeKitCells(&b[0], bagsEnd, bagsEnd, 4, &o4, 0) != coopkitcell::kKitCellAbsent || o4.size() != 1)
            Fail("no bytes must read as absent, the output untouched");
        if (coopkitcell::DecodeKitCells(&b[0], b.size(), dEnd, 4, &o4, 0) != coopkitcell::kKitCellAbsent)
            Fail("a BAGK block must never read as cells");
    }
    /* 5: cut at every length inside the block - refused whole, the output untouched */
    for (size_t cut = bagsEnd + 4; cut < b.size(); ++cut)
    {
        std::vector<coopkitcell::KitCell> o5;
        if (coopkitcell::DecodeKitCells(&b[0], cut, bagsEnd, 4, &o5, 0) != coopkitcell::kKitCellBad || !o5.empty())
        { Fail("a cut cells block must be refused"); break; }
    }
    /* 6: the limits - a count that is not the kit's, a cell out of range, an empty kit; the encoder writes nothing */
    {
        std::vector<coopkitcell::KitCell> o6;
        if (coopkitcell::DecodeKitCells(&b[0], b.size(), bagsEnd, 5, &o6, 0) != coopkitcell::kKitCellBad) Fail("a count not the kit's must be refused");
        std::vector<char> h;
        coopkitcell::KitCellPutU32(&h, coopkitcell::kKitCellMarker); coopkitcell::KitCellPutU32(&h, 1);
        coopkitcell::KitCellPutU32(&h, 4096); coopkitcell::KitCellPutU32(&h, 0);
        if (coopkitcell::DecodeKitCells(&h[0], h.size(), 0, 1, &o6, 0) != coopkitcell::kKitCellBad) Fail("a cell at 4096 must be refused");
        std::vector<coopkitcell::KitCell> far(cells);
        far[0].y = 4096;
        std::vector<char> e(2, 'z');
        if (coopkitcell::EncodeKitCells(&e, far, 4) || e.size() != 2) Fail("an out-of-range cell must write nothing");
        if (coopkitcell::EncodeKitCells(&e, cells, 3) || e.size() != 2) Fail("a count not the kit's must write nothing");
        std::vector<coopkitcell::KitCell> none;
        if (coopkitcell::EncodeKitCells(&e, none, 0) || e.size() != 2) Fail("an empty kit must write nothing");
    }
    /* 7: the placement rule on a 4x3 grid of 1x1 items */
    {
        char g[12]; std::memset(g, 0, sizeof g);
        g[1 * 4 + 2] = 1;   /* (2,1) taken */
        int x = 9, y = 9;
        if (coopkitcell::KitPlaceCell(4, 3, 1, 1, 3, 2, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellExact || x != 3 || y != 2)
            Fail("a free owner cell must be taken exactly");
        if (coopkitcell::KitPlaceCell(4, 3, 1, 1, 2, 1, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellRelocated || x != 0 || y != 0)
            Fail("a taken owner cell must go to the section's first free cell (y outer, x inner)");
        if (coopkitcell::KitPlaceCell(4, 3, 1, 1, 4, 0, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellRelocated || x != 0 || y != 0)
            Fail("an owner cell off the grid must relocate, never be used");
        if (coopkitcell::KitPlaceCell(4, 3, 1, 1, -1, -1, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellRelocated || x != 0 || y != 0)
            Fail("no owner cell known must be the first free cell");
        std::memset(g, 1, sizeof g);
        if (coopkitcell::KitPlaceCell(4, 3, 1, 1, 1, 1, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellNone || x != -1 || y != -1)
            Fail("a full section must be no room (kitLost)");
        std::memset(g, 0, sizeof g);
        if (coopkitcell::KitPlaceCell(4, 3, 2, 2, 3, 0, &Kit2Taken, g, &x, &y) != coopkitcell::kKitCellRelocated || x != 0 || y != 0)
            Fail("a footprint running off the grid at the owner cell must relocate");
    }
}

void t_bag23_kit_bags_block()
{
    std::vector<coopbag::KitBag> bags(2);
    bags[0].kitIndex = 1;
    bags[0].rows.push_back(Bag23Row("11-gamedata.base", 0, 3));
    bags[0].rows.push_back(Bag23Row("12-gamedata.base", 2, 1));
    bags[0].rows[1].owner.kind = 2; bags[0].rows[1].owner.uid = 9; bags[0].rows[1].owner.factionSid = "12-gamedata.base";
    bags[1].kitIndex = 3;
    bags[1].rows.push_back(Bag23Row("13-gamedata.base", 1, 7));

    /* 1: after QLT1 WITH its OWNL tail (one garment marked) */
    std::vector<coopgarment::GarmentDetail> d(4);
    for (size_t i = 0; i < d.size(); ++i) d[i].quantity = 1;
    d[2].owner.kind = 2; d[2].owner.uid = 5; d[2].owner.factionSid = "7-gamedata.base";
    std::vector<char> b(10, 'x');   /* stands for the item list */
    coopgarment::EncodeGarmentDetails(&b, d);
    const size_t detailBytes = b.size();
    if (!coopbag::EncodeKitBags(&b, bags, 4)) { Fail("two packs did not encode"); return; }
    std::vector<coopgarment::GarmentDetail> od;
    size_t dEnd = 0;
    if (coopgarment::DecodeGarmentDetailsEnd(&b[0], b.size(), 10, 4, &od, &dEnd) != coopgarment::kDetailOk || dEnd != detailBytes
        || od.size() != 4 || od[2].owner.uid != 5)
        Fail("the details' end offset must be the byte after its OWNL tail");
    std::vector<coopbag::KitBag> o;
    size_t end = 0;
    int why = -1;
    if (coopbag::DecodeKitBags(&b[0], b.size(), dEnd, 4, &o, &end, &why) != coopbag::kKitBagOk || end != b.size() || o.size() != 2
        || o[0].kitIndex != 1 || o[1].kitIndex != 3 || o[0].rows.size() != 2 || o[1].rows.size() != 1
        || coopbag::BagRowsDigest(o[0].rows) != coopbag::BagRowsDigest(bags[0].rows) || o[1].rows[0].qty != 7
        || o[0].rows[1].owner.kind != 2 || o[0].rows[1].owner.uid != 9 || o[0].rows[0].owner.kind != 0 || why != coopbag::kBagOk)
        Fail("the kit's packs block must round-trip after QLT1+OWNL, its rows' marks included");
    std::vector<coopgarment::GarmentDetail> old;
    if (coopgarment::DecodeGarmentDetails(&b[0], b.size(), 10, 4, &old) != coopgarment::kDetailOk || old.size() != 4 || old[2].owner.uid != 5)
        Fail("an older reader must still read the details with the packs block after them");

    /* 2: after QLT1 with NO owner tail */
    {
        std::vector<coopgarment::GarmentDetail> clean(4);
        for (size_t i = 0; i < clean.size(); ++i) clean[i].quantity = 1;
        std::vector<char> c(10, 'x');
        coopgarment::EncodeGarmentDetails(&c, clean);
        const size_t cd = c.size();
        if (!coopbag::EncodeKitBags(&c, bags, 4)) Fail("packs after a clean QLT1 did not encode");
        std::vector<coopgarment::GarmentDetail> od2;
        size_t e2 = 0, k2 = 0;
        std::vector<coopbag::KitBag> o2;
        if (coopgarment::DecodeGarmentDetailsEnd(&c[0], c.size(), 10, 4, &od2, &e2) != coopgarment::kDetailOk || e2 != cd
            || coopbag::DecodeKitBags(&c[0], c.size(), e2, 4, &o2, &k2, 0) != coopbag::kKitBagOk || o2.size() != 2 || k2 != c.size())
            Fail("the packs block must round-trip after a QLT1 with no OWNL tail");
    }
    /* 3: no QLT1 at all (an item list only): the details read absent, their end is the list's end, the packs follow */
    {
        std::vector<char> c(10, 'x');
        if (!coopbag::EncodeKitBags(&c, bags, 4)) Fail("packs after a bare list did not encode");
        std::vector<coopgarment::GarmentDetail> od3;
        size_t e3 = 99, k3 = 0;
        std::vector<coopbag::KitBag> o3;
        if (coopgarment::DecodeGarmentDetailsEnd(&c[0], c.size(), 10, 4, &od3, &e3) != coopgarment::kDetailAbsent || e3 != 10
            || coopbag::DecodeKitBags(&c[0], c.size(), e3, 4, &o3, &k3, 0) != coopbag::kKitBagOk || o3.size() != 2)
            Fail("with no details block the packs block must be found right after the list");
    }
    /* 4: nothing to carry - nothing written, read as absent */
    {
        std::vector<char> c(10, 'x');
        std::vector<coopbag::KitBag> none, o4;
        size_t k4 = 0;
        if (!coopbag::EncodeKitBags(&c, none, 4) || c.size() != 10) Fail("no packs must write nothing");
        if (coopbag::DecodeKitBags(&c[0], c.size(), 10, 4, &o4, &k4, 0) != coopbag::kKitBagAbsent) Fail("no bytes must read as absent");
    }
    /* 5: truncated at every length - refused whole, the output untouched */
    for (size_t cut = dEnd + 1; cut < b.size(); ++cut)
    {
        std::vector<coopbag::KitBag> o5;
        size_t k5 = 0;
        if (coopbag::DecodeKitBags(&b[0], cut, dEnd, 4, &o5, &k5, 0) != coopbag::kKitBagBad || !o5.empty())
        { Fail("a cut packs block must be refused"); break; }
    }
    /* 6: cap+1 - the encoder writes nothing; a block that says 257 is refused as over the cap; 256 goes */
    {
        std::vector<coopbag::KitBag> big(1);
        for (unsigned int i = 0; i < coopbag::kBagMaxRows + 1; ++i) big[0].rows.push_back(Bag23Row("11-gamedata.base", i % 4000u, 1));
        std::vector<char> c(3, 'y');
        if (coopbag::EncodeKitBags(&c, big, 1) || c.size() != 3) Fail("a pack over the 256-row cap must write nothing");
        big[0].rows.pop_back();
        std::vector<coopbag::KitBag> o6;
        size_t k6 = 0;
        if (!coopbag::EncodeKitBags(&c, big, 1) || coopbag::DecodeKitBags(&c[0], c.size(), 3, 1, &o6, &k6, 0) != coopbag::kKitBagOk
            || o6.size() != 1 || o6[0].rows.size() != coopbag::kBagMaxRows)
            Fail("a 256-row pack must round-trip");
        std::vector<char> h;
        coopbag::BagPutU32(&h, coopbag::kKitBagMarker); coopbag::BagPutU32(&h, 1); coopbag::BagPutU32(&h, 0);
        coopbag::BagPutU32(&h, coopbag::kBagMarker); coopbag::BagPutU32(&h, coopbag::kBagMaxRows + 1);
        int w6 = -1;
        if (coopbag::DecodeKitBags(&h[0], h.size(), 0, 1, &o6, &k6, &w6) != coopbag::kKitBagBad || w6 != coopbag::kBagOverCap)
            Fail("a pack block saying 257 rows must be refused as over the cap");
    }
    /* 7: the wrapper's limits - an index past the kit, indexes not rising, more packs than kit items, a wrong marker */
    {
        std::vector<char> c;
        if (coopbag::EncodeKitBags(&c, bags, 3) || !c.empty()) Fail("index 3 in a 3-item kit must write nothing");
        std::vector<coopbag::KitBag> rev(2);
        rev[0] = bags[1]; rev[1] = bags[0];
        if (coopbag::EncodeKitBags(&c, rev, 4) || !c.empty()) Fail("packs not in rising order must write nothing");
        std::vector<coopbag::KitBag> o7;
        size_t k7 = 0;
        if (coopbag::DecodeKitBags(&b[0], b.size(), dEnd, 3, &o7, &k7, 0) != coopbag::kKitBagBad) Fail("an index past the kit must be refused");
        if (coopbag::DecodeKitBags(&b[0], b.size(), dEnd, 1, &o7, &k7, 0) != coopbag::kKitBagBad) Fail("more packs than kit items must be refused");
        std::vector<char> wm(b);
        wm[dEnd] = 'Z';
        if (coopbag::DecodeKitBags(&wm[0], wm.size(), dEnd, 4, &o7, &k7, 0) != coopbag::kKitBagBad) Fail("a wrong marker must be refused, not read as absent");
    }
}

void t_inv3a_bag_rows()
{
    std::vector<coopbag::BagRow> rows(2);
    rows[0].section = "backpack_content"; rows[0].x = 0; rows[0].y = 0; rows[0].qty = 3; rows[0].baseSid = "2292-gamedata.base";
    rows[0].quality = 0.5f; rows[0].charges = 1.25f;
    rows[1].section = "backpack_content"; rows[1].x = 2; rows[1].y = 1; rows[1].qty = 1; rows[1].baseSid = "43951-rebirth.mod";
    rows[1].companySid = "912-gamedata.base"; rows[1].materialSid = "m"; rows[1].colorSid = "k";
    rows[1].functionKind = 5; rows[1].level = 2; rows[1].unique = 1;
    std::vector<char> b(7, 'x');   /* stands for the trade block before it */
    if (!coopbag::EncodeBagRows(&b, rows)) { Fail("two rows did not encode"); return; }
    std::vector<coopbag::BagRow> o;
    size_t end = 0;
    if (coopbag::DecodeBagRows(&b[0], b.size(), 7, &o, &end) != coopbag::kBagOk || o.size() != 2 || end != b.size()
        || o[0].section != "backpack_content" || o[0].qty != 3 || o[0].baseSid != "2292-gamedata.base" || o[0].quality != 0.5f
        || o[0].charges != 1.25f || o[1].x != 2 || o[1].y != 1 || o[1].companySid != "912-gamedata.base" || o[1].materialSid != "m"
        || o[1].colorSid != "k" || o[1].functionKind != 5 || o[1].level != 2 || o[1].unique != 1 || o[0].unique != 0)
        Fail("a BAG1 block did not round-trip");
    /* Absent: nothing after the fixed fields. */
    if (coopbag::DecodeBagRows(&b[0], 7, 7, &o, &end) != coopbag::kBagAbsent) Fail("no bytes must read as absent");
    /* Truncated at EVERY length: refused, never read as fewer rows. */
    for (size_t cut = 8; cut < b.size(); ++cut)
        if (coopbag::DecodeBagRows(&b[0], cut, 7, &o, &end) != coopbag::kBagBad) { Fail("a cut block must be refused"); break; }
    /* Wrong marker: refused (a peer on protocol 70 sends nothing when it has nothing). */
    std::vector<char> m = b; m[7] = 'Z';
    if (coopbag::DecodeBagRows(&m[0], m.size(), 7, &o, &end) != coopbag::kBagBad) Fail("another marker must be refused");
    /* The cap: 256 rows go, 257 are refused by the encoder (NOTHING written) and a count of 257 on the wire is refused. */
    std::vector<coopbag::BagRow> big(256, rows[0]);
    std::vector<char> bb;
    if (!coopbag::EncodeBagRows(&bb, big)) Fail("256 rows must encode");
    else if (coopbag::DecodeBagRows(&bb[0], bb.size(), 0, &o, &end) != coopbag::kBagOk || o.size() != 256) Fail("256 rows must decode");
    big.push_back(rows[0]);
    std::vector<char> bc;
    if (coopbag::EncodeBagRows(&bc, big) || !bc.empty()) Fail("257 rows must be refused whole, never truncated");
    if (!bb.empty())
    {
        std::vector<char> oc = bb;
        const unsigned int n257 = 257;
        std::memcpy(&oc[4], &n257, 4);
        if (coopbag::DecodeBagRows(&oc[0], oc.size(), 0, &o, &end) != coopbag::kBagOverCap) Fail("a count of 257 must be refused as over the cap");
    }
    /* Rows out of range: the encoder refuses an empty base sid and a NaN quality; the decoder refuses quantity 0. */
    std::vector<coopbag::BagRow> bad(1, rows[0]);
    bad[0].baseSid = "";
    std::vector<char> be;
    if (coopbag::EncodeBagRows(&be, bad) || !be.empty()) Fail("an empty base sid must be refused");
    bad[0] = rows[0];
    { const unsigned int nan = 0x7FC00000u; std::memcpy(&bad[0].quality, &nan, 4); }
    if (coopbag::EncodeBagRows(&be, bad) || !be.empty()) Fail("a NaN quality must be refused");
    std::vector<coopbag::BagRow> one(1, rows[0]);
    std::vector<char> ob;
    if (!coopbag::EncodeBagRows(&ob, one)) Fail("one row did not encode");
    else
    {
        const unsigned int zero = 0;
        std::memcpy(&ob[36], &zero, 4);   /* marker 4 + n 4 + len 4 + "backpack_content" 16 + x 4 + y 4 = the quantity */
        if (coopbag::DecodeBagRows(&ob[0], ob.size(), 0, &o, &end) != coopbag::kBagBad) Fail("quantity 0 must be refused");
    }
    /* The digest is order-free and sees a changed quantity. */
    std::vector<coopbag::BagRow> rev(2); rev[0] = rows[1]; rev[1] = rows[0];
    if (coopbag::BagRowsDigest(rev) != coopbag::BagRowsDigest(rows)) Fail("the digest must not depend on row order");
    rev[1].qty = 4;
    if (coopbag::BagRowsDigest(rev) == coopbag::BagRowsDigest(rows)) Fail("the digest must see a changed quantity");
    if (coopbag::BagRowsDigest(std::vector<coopbag::BagRow>()) == coopbag::BagRowsDigest(rows)) Fail("an empty pack must digest apart");
}

/* inv3a fold (review-inv3a 1b, 1c/1d): the GIVE re-check at confirm (same object, slot read, rows read, same digest - anything
   else cancels) and the all-or-nothing fill (every row created AND placed inside the pack, or the handover is refused). */
void t_inv3a_fold_decisions()
{
    std::vector<coopbag::BagRow> rows(2);
    rows[0].section = "backpack_content"; rows[0].qty = 3; rows[0].baseSid = "2292-gamedata.base";
    rows[1].section = "backpack_content"; rows[1].x = 1; rows[1].qty = 1; rows[1].baseSid = "43951-rebirth.mod";
    const unsigned int sent = coopbag::BagRowsDigest(rows);
    /* Unchanged: complete. */
    if (!coopbag::BagGiveUnchanged(true, true, true, sent, coopbag::BagRowsDigest(rows))) Fail("an unchanged pack must let the give complete");
    /* Each doubt cancels. */
    if (coopbag::BagGiveUnchanged(false, true, true, sent, sent)) Fail("an unreadable slot must cancel the give");
    if (coopbag::BagGiveUnchanged(true, false, true, sent, sent)) Fail("a different object must cancel the give");
    if (coopbag::BagGiveUnchanged(true, true, false, sent, sent)) Fail("unreadable contents must cancel the give");
    std::vector<coopbag::BagRow> added(rows); added.push_back(rows[0]); added[2].x = 2;
    if (coopbag::BagGiveUnchanged(true, true, true, sent, coopbag::BagRowsDigest(added))) Fail("an item added after sending must cancel the give");
    std::vector<coopbag::BagRow> taken(1, rows[0]);
    if (coopbag::BagGiveUnchanged(true, true, true, sent, coopbag::BagRowsDigest(taken))) Fail("an item taken out after sending must cancel the give");
    std::vector<coopbag::BagRow> fewer(rows); fewer[0].qty = 2;
    if (coopbag::BagGiveUnchanged(true, true, true, sent, coopbag::BagRowsDigest(fewer))) Fail("a changed stack size must cancel the give");
    /* An EMPTY pack sent empty and still empty completes; an empty pack that gained an item does not. */
    const unsigned int none = coopbag::BagRowsDigest(std::vector<coopbag::BagRow>());
    if (!coopbag::BagGiveUnchanged(true, true, true, none, none)) Fail("an empty pack still empty must complete");
    if (coopbag::BagGiveUnchanged(true, true, true, none, coopbag::BagRowsDigest(taken))) Fail("an empty pack that gained an item must cancel");
    /* All or nothing. */
    if (!coopbag::BagFillComplete(0, 0, 0)) Fail("no rows is a complete fill");
    if (!coopbag::BagFillComplete(2, 2, 2)) Fail("every row placed is a complete fill");
    if (coopbag::BagFillComplete(2, 2, 1)) Fail("one row not placed must refuse the whole handover");
    if (coopbag::BagFillComplete(2, 1, 1)) Fail("one row not created must refuse the whole handover");
    if (coopbag::BagFillComplete(256, 256, 255)) Fail("255 of 256 must refuse the whole handover");
}

/* crime5 (docs/design-crime.md 3 C): MSG_BOUNTY carries a character's bounty list (owner -> copies) or what a guard added to
   a copy (copy -> owner). Round trip, the caps, hostile payloads by their own reason, the change test and the additions. */
/* arrest2 (docs/design-arrest.md 3): MSG_PRISON - a guard on one game caged the other game's character. */
/* a bail paid on another game, and a slave-state change made on a copy, reach the character's owner. */
void t_t600_bail_and_slave_ask()
{
    /* the slave ask's wire: from and want survive; a bad state, from == want and a cut message are refused */
    cooprison::PrisonMsg m; m.uid = 4195329u; m.kind = cooprison::kPrisonSlaveAsk; m.slaveFrom = 1; m.slaveWant = 2;
    std::vector<char> b;
    if (!cooprison::EncodePrison(&b, m)) { Fail("a slave ask did not encode"); return; }
    cooprison::PrisonMsg o;
    if (cooprison::DecodePrison(&b[0], b.size(), &o) != cooprison::kPrisonDecodeOk || o.uid != m.uid || o.kind != cooprison::kPrisonSlaveAsk
        || o.slaveFrom != 1 || o.slaveWant != 2 || !o.cageKey.empty() || !o.outerKey.empty())
        Fail("a slave ask did not survive the wire");
    for (size_t n = 0; n < b.size(); ++n)
        if (cooprison::DecodePrison(&b[0], n, &o) == cooprison::kPrisonDecodeOk) { Fail("a cut slave ask decoded at " + I((int)n)); break; }
    std::vector<char> bad(b); bad[bad.size() - 1] = 4;
    if (cooprison::DecodePrison(&bad[0], bad.size(), &o) != cooprison::kPrisonDecodeBadSlave) Fail("a slave ask wanting state 4 was accepted");
    std::vector<char> same(b); same[same.size() - 1] = 1;
    if (cooprison::DecodePrison(&same[0], same.size(), &o) != cooprison::kPrisonDecodeBadSlave) Fail("a slave ask with from == want was accepted");
    cooprison::PrisonMsg e(m); e.slaveWant = 1;
    std::vector<char> eb;
    if (cooprison::EncodePrison(&eb, e) || !eb.empty()) Fail("a slave ask with from == want was encoded");

    /* the release modes: "@bail" clears every bounty, writes no pardon and frees the slave state; the others as before */
    if (cooprison::PrisonReleaseModeOf("@bail", false) != cooprison::kReleaseModeBail) Fail("@bail is not the bail release");
    if (cooprison::PrisonReleaseModeOf("@bail", true) != cooprison::kReleaseModeBail) Fail("@bail looked up as a faction");
    if (cooprison::PrisonReleaseModeOf("@player", false) != cooprison::kReleaseModePlayer) Fail("@player changed meaning");
    if (cooprison::PrisonReleaseModeOf("Holy Nation", true) != cooprison::kReleaseModeNamed) Fail("a found faction is not named");
    if (cooprison::PrisonReleaseModeOf("Holy Nation", false) != cooprison::kReleaseModeCage) Fail("an unknown faction is not the cage's");
    if (cooprison::PrisonReleaseModeOf("", false) != cooprison::kReleaseModeCage) Fail("an empty key is not the cage's");
    if (!cooprison::PrisonReleaseKeyIsMarker("@bail") || !cooprison::PrisonReleaseKeyIsMarker("@player") || cooprison::PrisonReleaseKeyIsMarker("bail"))
        Fail("the release markers are wrong");
    for (int mode = 0; mode <= 3; ++mode)
    {
        const bool bail = mode == cooprison::kReleaseModeBail;
        if (cooprison::PrisonReleaseClearsEveryBounty(mode) != bail || cooprison::PrisonReleasePardons(mode) == bail
            || cooprison::PrisonReleaseFreesSlave(mode) != bail)
            Fail("release mode " + I(mode) + " has the wrong bail behaviour");
    }
    cooprison::PrisonMsg r; r.uid = 7u; r.kind = cooprison::kPrisonRelease; r.cageKey = cooprison::kPrisonBailKey;
    std::vector<char> rb;
    if (!cooprison::EncodePrison(&rb, r) || cooprison::DecodePrison(&rb[0], rb.size(), &o) != cooprison::kPrisonDecodeOk
        || cooprison::PrisonReleaseModeOf(o.cageKey, false) != cooprison::kReleaseModeBail)
        Fail("a bail release did not survive the wire");

    /* who the bail tells: another game's character only */
    if (!cooprison::BailTellsOwner(4195329u, false)) Fail("a bail for another game's character is not told to its owner");
    if (cooprison::BailTellsOwner(1025u, true)) Fail("a bail for this game's own character was told");
    if (cooprison::BailTellsOwner(0u, false)) Fail("a bail for a character that is not replicated was told");

    /* the sender: only a copy, only a change, not inside the bail, not an enslavement, only from the game that runs the area */
    using namespace coopslave;
    if (SlaveAskSenderDecide(true, false, 1, 2, 0, 0) != kAskSend) Fail("the area's runner did not ask the owner about an escape");
    if (SlaveAskSenderDecide(true, false, 1, 3, 0, 0) != kAskSend) Fail("the area's runner did not ask about a freeing");
    if (SlaveAskSenderDecide(true, false, 3, 0, 0, 0) != kAskSend) Fail("the area's runner did not ask about an ex-slave's end");
    if (SlaveAskSenderDecide(true, false, 2, 1, 0, 0) != kAskEnslave) Fail("a recapture (2 -> 1) was asked outside the capture road");
    if (SlaveAskSenderDecide(true, false, 3, 1, 0, 0) != kAskEnslave) Fail("a re-enslavement (3 -> 1) was asked outside the capture road");
    if (SlaveAskSenderDecide(false, false, 1, 2, 0, 0) != kAskNotCopy) Fail("a change on our own character was asked");
    if (SlaveAskSenderDecide(true, false, 1, 1, 0, 0) != kAskSame) Fail("no change was asked");
    if (SlaveAskSenderDecide(true, true, 1, 0, 0, 0) != kAskBail) Fail("the bail's own slave change was asked separately");
    if (SlaveAskSenderDecide(true, false, 0, 1, 0, 0) != kAskEnslave) Fail("an enslavement was asked outside the capture road");
    if (SlaveAskSenderDecide(true, false, 1, 2, 1, 0) != kAskNotRunner) Fail("a game that does not run the area asked");
    if (SlaveAskSenderDecide(true, false, 1, 2, -1, 0) != kAskNoMap || SlaveAskSenderDecide(true, false, 1, 2, 0, -1) != kAskNoMap)
        Fail("an unknown area runner was taken as this game");
    if (SlaveAskSenderDecide(true, false, 1, 4, 0, 0) != kAskBad || SlaveAskSenderDecide(true, false, -1, 2, 0, 0) != kAskBad)
        Fail("a bad slave state was asked");

    /* the repeat gate */
    if (!SlaveAskDue(false, -1, 0u, 2)) Fail("a first ask was held");
    if (SlaveAskDue(true, 2, 100u, 2)) Fail("the same ask was repeated at once");
    if (!SlaveAskDue(true, 2, kSlaveAskResendMs, 2)) Fail("the same ask was not repeated after the resend period");
    if (!SlaveAskDue(true, 1, 100u, 2)) Fail("a different ask was held");

    /* the owner: ours, a real change from the copy's state, not an enslavement, from the game that runs the area */
    if (SlaveAskOwnerDecide(true, 1, 1, 2, 0, 0) != kAskApply) Fail("the owner did not apply the area runner's escape");
    if (SlaveAskOwnerDecide(false, 1, 1, 2, 0, 0) != kAskNotOwner) Fail("an ask about another game's character was applied");
    if (SlaveAskOwnerDecide(true, 2, 1, 2, 0, 0) != kAskAlready) Fail("an ask already so was applied again");
    if (SlaveAskOwnerDecide(true, 3, 1, 2, 0, 0) != kAskStale) Fail("a stale ask was applied");
    if (SlaveAskOwnerDecide(true, 0, 0, 1, 0, 0) != kAskOwnerEnslave) Fail("an enslavement was applied from the ask road");
    if (SlaveAskOwnerDecide(true, 3, 3, 1, 0, 0) != kAskOwnerEnslave || SlaveAskOwnerDecide(true, 2, 2, 1, 0, 0) != kAskOwnerEnslave)
        Fail("a re-enslavement of an escaping or freed character was applied from the ask road");
    if (SlaveAskOwnerDecide(true, 2, 2, 3, 0, 1) != kAskSenderNotHolder) Fail("the owner took an ask while it runs the area itself");

    /* one decider: while another game runs the area the owner's own periodic update defers */
    if (!SlaveOwnEngineDefers(0, 1) || SlaveOwnEngineDefers(1, 1) || SlaveOwnEngineDefers(-1, 1) || SlaveOwnEngineDefers(0, -1))
        Fail("the owner's engine deferred to the wrong game");
    /* two engines that disagree: the runner's wants ESCAPING (2) until step 6 then EX_SLAVE (3); the owner's own periodic update
       wants EX_SLAVE from step 3. With the owner deferring, the owner's state changes twice and settles; without, it flips. */
    for (int defer = 0; defer <= 1; ++defer)
    {
        int own = 1, copy = 1, changes = 0;
        for (int step = 0; step < 20; ++step)
        {
            const int runnerWants = step < 6 ? 2 : 3;
            if (copy != runnerWants && SlaveAskSenderDecide(true, false, copy, runnerWants, 0, 0) == kAskSend
                && SlaveAskOwnerDecide(true, own, copy, runnerWants, 0, 0) == kAskApply)
            { own = runnerWants; ++changes; }
            const bool ownDefers = defer != 0 && SlaveOwnEngineDefers(0, 1);
            if (!ownDefers && step >= 3 && own == 2) { own = 3; ++changes; }
            copy = own;
        }
        if (defer != 0 && (own != 3 || changes != 2)) Fail("two engines did not settle with the owner deferring: own=" + I(own) + " changes=" + I(changes));
        if (defer == 0 && changes <= 2) Fail("the disagreeing-engines model did not show the flip it guards against: changes=" + I(changes));
    }

    /* the bail from the area's runner only, and its resend loop */
    if (!cooprison::PrisonBailFromRunner(0, 0, 2) || cooprison::PrisonBailFromRunner(0, 1, 2) || cooprison::PrisonBailFromRunner(-1, 0, 2)
        || cooprison::PrisonBailFromRunner(0, -1, 2) || cooprison::PrisonBailFromRunner(-1, 1, -1))
        Fail("a bail was taken from a game that does not run the area");
    if (!cooprison::PrisonBailFromRunner(2, 1, 2)) Fail("a bail paid on another game was refused where the owner runs the area");
    if (cooprison::BailPendingStep(false, true, true, 0u, 1) != cooprison::kBailGone) Fail("a bail for a gone copy was kept");
    if (cooprison::BailPendingStep(true, false, true, 0u, 1) != cooprison::kBailDone) Fail("a bail the owner shows done was kept");
    if (cooprison::BailPendingStep(true, false, false, 100u, 1) != cooprison::kBailWait
        || cooprison::BailPendingStep(true, false, false, cooprison::kBailResendMs, 1) != cooprison::kBailResend)
        Fail("a bail paid before the owner's word showed the cage was taken as done");
    if (cooprison::BailPendingStep(true, true, true, 100u, 1) != cooprison::kBailWait) Fail("a bail was sent again at once");
    if (cooprison::BailPendingStep(true, true, true, cooprison::kBailResendMs, 1) != cooprison::kBailResend) Fail("a pending bail was not sent again");
    if (cooprison::BailPendingStep(true, true, true, cooprison::kBailResendMs, cooprison::kBailTriesMax) != cooprison::kBailGiveUp)
        Fail("a bail the owner never shows was sent without end");
    if (SlaveAskOwnerDecide(true, 1, 1, 2, 1, 0) != kAskSenderNotHolder) Fail("an ask from a game that does not run the area was applied");
    if (SlaveAskOwnerDecide(true, 1, 1, 2, -1, 0) != kAskHolderUnknown || SlaveAskOwnerDecide(true, 1, 1, 2, 0, -1) != kAskHolderUnknown)
        Fail("an ask with the area runner unknown was applied");
    if (SlaveAskOwnerDecide(true, -1, 1, 2, 0, 0) != kAskOwnerBad || SlaveAskOwnerDecide(true, 1, 1, 1, 0, 0) != kAskOwnerBad)
        Fail("an unreadable state or a no-change ask was applied");
    for (int d = 0; d < 8; ++d)
        if (std::string(SlaveAskWhy(d, true)) == "?" || std::string(SlaveAskWhy(d, false)) == "?") Fail("decision " + I(d) + " has no reason text");
}

void t_arrest2_prison_wire()
{
    cooprison::PrisonMsg m;
    m.uid = 738197505u; m.kind = cooprison::kPrisonIn;
    m.cageKey = "Prison cage|43,11|5290,23,-9493"; m.outerKey = "Police station|43,11|5301,20,-9480";
    std::vector<char> b;
    if (!cooprison::EncodePrison(&b, m)) { Fail("a prison message did not encode"); return; }
    cooprison::PrisonMsg o;
    if (cooprison::DecodePrison(&b[0], b.size(), &o) != cooprison::kPrisonDecodeOk || o.uid != m.uid || o.kind != m.kind
        || o.cageKey != m.cageKey || o.outerKey != m.outerKey)
        Fail("the prison message did not survive");
    for (int sv = 0; sv <= 2; ++sv)   /* P11 f3 (M6): kPrisonIn carries the arresting game's sentence verdict */
    {
        cooprison::PrisonMsg sm = m; sm.sentence = (unsigned char)sv;
        std::vector<char> sb;
        if (!cooprison::EncodePrison(&sb, sm) || sb.size() != b.size()) { Fail("a prison sentence verdict did not encode"); continue; }
        cooprison::PrisonMsg so;
        if (cooprison::DecodePrison(&sb[0], sb.size(), &so) != cooprison::kPrisonDecodeOk || so.sentence != (unsigned char)sv) Fail("a prison sentence verdict did not survive");
    }
    {
        std::vector<char> sb = b; sb[sb.size() - 1] = (char)3;
        cooprison::PrisonMsg so;
        if (cooprison::DecodePrison(&sb[0], sb.size(), &so) != cooprison::kPrisonDecodeBadSentence) Fail("sentence verdict 3 was accepted");
        cooprison::PrisonMsg sm = m; sm.sentence = 3;
        std::vector<char> se;
        if (cooprison::EncodePrison(&se, sm) || !se.empty()) Fail("sentence verdict 3 was encoded");
        std::vector<char> sc = b; sc.pop_back();
        if (cooprison::DecodePrison(&sc[0], sc.size(), &so) != cooprison::kPrisonDecodeTooShort) Fail("a prison IN without its verdict was accepted");
    }
    m.outerKey.clear();
    std::vector<char> c;
    if (!cooprison::EncodePrison(&c, m) || cooprison::DecodePrison(&c[0], c.size(), &o) != cooprison::kPrisonDecodeOk || !o.outerKey.empty())
        Fail("a prison message with no outer key did not survive");
    for (size_t n = 0; n < b.size(); ++n)
        if (cooprison::DecodePrison(&b[0], n, &o) == cooprison::kPrisonDecodeOk) { Fail("a cut prison message decoded at " + I((int)n)); break; }
    std::vector<char> k(b); k[4] = 10;
    if (cooprison::DecodePrison(&k[0], k.size(), &o) != cooprison::kPrisonDecodeBadKind) Fail("a bad prison kind was accepted");
    std::vector<char> e(b); e[5] = 0;
    if (cooprison::DecodePrison(&e[0], e.size(), &o) != cooprison::kPrisonDecodeBadKey) Fail("an empty cage key was accepted");
    cooprison::PrisonMsg z; z.kind = cooprison::kPrisonIn;
    std::vector<char> y;
    if (cooprison::EncodePrison(&y, z) || !y.empty()) Fail("an empty cage key was encoded");
    /* arrest3 (57): RELEASE and REFUSED carry no keys */
    for (int k = 0; k < 2; ++k)
    {
        cooprison::PrisonMsg r; r.uid = 77u; r.kind = k == 0 ? cooprison::kPrisonRelease : cooprison::kPrisonRefused;
        std::vector<char> rb;
        if (!cooprison::EncodePrison(&rb, r) || rb.size() != 7) { Fail("a keyless prison message did not encode to 7 bytes"); continue; }
        cooprison::PrisonMsg ro;
        if (cooprison::DecodePrison(&rb[0], rb.size(), &ro) != cooprison::kPrisonDecodeOk || ro.uid != 77u || ro.kind != r.kind
            || !ro.cageKey.empty() || !ro.outerKey.empty())
            Fail("a keyless prison message did not survive");
    }
    /* review-arrest3 M2: a RELEASE carries the releaser's faction (stringID or "@player") in the cage key */
    {
        cooprison::PrisonMsg r; r.uid = 78u; r.kind = cooprison::kPrisonRelease; r.cageKey = "defaultEmpireFactionSID";
        std::vector<char> rb;
        cooprison::PrisonMsg ro;
        if (!cooprison::EncodePrison(&rb, r) || cooprison::DecodePrison(&rb[0], rb.size(), &ro) != cooprison::kPrisonDecodeOk
            || ro.kind != cooprison::kPrisonRelease || ro.cageKey != "defaultEmpireFactionSID")
            Fail("a RELEASE with the releaser's faction did not survive");
        r.cageKey = std::string(cooprison::kPrisonMaxKey + 1, 'x');
        rb.clear();
        if (cooprison::EncodePrison(&rb, r)) Fail("a RELEASE faction longer than the key limit was encoded");
    }
    /* par6 fold (review-par6 #1, session 80): kind 4 DEATH - a death request with the caller's name in the cage key */
    {
        cooprison::PrisonMsg r; r.uid = 79u; r.kind = cooprison::kPrisonDeath; r.cageKey = "deathCheck";
        std::vector<char> rb;
        cooprison::PrisonMsg ro;
        if (!cooprison::EncodePrison(&rb, r) || cooprison::DecodePrison(&rb[0], rb.size(), &ro) != cooprison::kPrisonDecodeOk
            || ro.uid != 79u || ro.kind != cooprison::kPrisonDeath || ro.cageKey != "deathCheck" || !ro.outerKey.empty())
            Fail("a prison DEATH request did not survive");
    }
    std::vector<char> bk(b); bk[4] = 10;   /* P42: 7 and 8 are SHACKLE LOCK / CAGE LOCK; 9 is SLAVE ASK */
    if (cooprison::DecodePrison(&bk[0], bk.size(), &o) != cooprison::kPrisonDecodeBadKind) Fail("prison kind 10 was accepted");
}

/* P42: MSG_PRISON kinds SHACKLE LOCK (7) / CAGE LOCK (8) and the restraint-lock decisions (prisonwire.h). */
void t_p42_restraint_lock()
{
    using namespace cooprison;
    for (int k = 7; k <= 8; ++k)
    {
        PrisonMsg m; m.uid = 738197509u; m.kind = (unsigned char)k;
        m.cageKey = (k == 7) ? "boots" : "Prison cage|43,11|5290,23,-9493"; m.outerKey = (k == 7) ? "" : "Police station|43,11|5301,20,-9480";
        m.lockFlags = kLockFlagBroken; m.lockLevel = 20;
        std::vector<char> b;
        if (!EncodePrison(&b, m)) { Fail("a restraint lock message did not encode"); continue; }
        if (b.size() != 4 + 1 + 1 + m.cageKey.size() + 1 + m.outerKey.size() + 5) Fail("a restraint lock message has the wrong size");
        PrisonMsg o;
        if (DecodePrison(&b[0], b.size(), &o) != kPrisonDecodeOk || o.uid != m.uid || o.kind != m.kind || o.cageKey != m.cageKey
            || o.outerKey != m.outerKey || o.lockFlags != m.lockFlags || o.lockLevel != m.lockLevel)
            Fail("a restraint lock message did not survive");
        if (DecodePrison(&b[0], b.size() - 1, &o) != kPrisonDecodeTooShort) Fail("a cut restraint lock message decoded");
        std::vector<char> bad = b; bad[bad.size() - 5] = (char)4;   /* an unknown flag bit */
        if (DecodePrison(&bad[0], bad.size(), &o) != kPrisonDecodeBadLock) Fail("a restraint lock with an unknown flag decoded");
        bad = b; int lv = 101; std::memcpy(&bad[bad.size() - 4], &lv, 4);
        if (DecodePrison(&bad[0], bad.size(), &o) != kPrisonDecodeBadLock) Fail("a restraint lock level above 100 decoded");
        lv = -1; std::memcpy(&bad[bad.size() - 4], &lv, 4);
        if (DecodePrison(&bad[0], bad.size(), &o) != kPrisonDecodeBadLock) Fail("a negative restraint lock level decoded");
        PrisonMsg e = m; e.cageKey = "";
        std::vector<char> eb;
        if (EncodePrison(&eb, e) || !eb.empty()) Fail("a restraint lock message without a key encoded");
        e = m; e.lockLevel = 101;
        if (EncodePrison(&eb, e) || !eb.empty()) Fail("a restraint lock message with level 101 encoded");
        e = m; e.lockFlags = 0x80;
        if (EncodePrison(&eb, e) || !eb.empty()) Fail("a restraint lock message with an unknown flag encoded");
    }
    { PrisonMsg m; m.uid = 1; m.kind = 9; m.cageKey = "x"; std::vector<char> b; if (EncodePrison(&b, m)) Fail("kind 9 encoded"); }
    /* the engine's open test: not locked, broken, or level < 1 */
    if (LockOpen(kLockFlagLocked, 20)) Fail("a locked lock read open");
    if (!LockOpen(0, 20)) Fail("an unlocked lock read closed");
    if (!LockOpen(kLockFlagLocked | kLockFlagBroken, 20)) Fail("a broken lock read closed");
    if (!LockOpen(kLockFlagLocked, 0)) Fail("a level-0 lock read closed");
    /* the owner's word: first sight, a change, another key, the resend period */
    if (!RestraintWordDue(false, true, 1, 20, 1, 20, 0, 15000)) Fail("a first word was not due");
    if (RestraintWordDue(true, true, 1, 20, 1, 20, 14999, 15000)) Fail("an unchanged word was due before the resend period");
    if (!RestraintWordDue(true, true, 1, 20, 1, 20, 15000, 15000)) Fail("an unchanged word was not due at the resend period");
    if (!RestraintWordDue(true, true, 1, 20, 0, 20, 10, 15000)) Fail("a changed lock was not due");
    if (!RestraintWordDue(true, true, 1, 20, 1, 19, 10, 15000)) Fail("a changed level was not due");
    if (!RestraintWordDue(true, false, 1, 20, 1, 20, 10, 15000)) Fail("a word about another restraint was not due");
    /* a copy against the owner's word */
    if (RestraintCopyDecide(false, 1, 20, 0, 20, true, kRestraintAskNone) != kRestraintKeep) Fail("a copy with no word was changed");
    if (RestraintCopyDecide(true, 1, 20, 1, 20, false, kRestraintAskNone) != kRestraintKeep) Fail("a copy equal to the word was changed");
    /* first sight / a new word (not yet seen equal): an open copy under a closed word is REWRITTEN - a copy caged here by
       setPrisonMode has an unlocked cage, which is no one's pick */
    if (RestraintCopyDecide(true, 1, 20, 0, 20, false, kRestraintAskNone) != kRestraintRewrite) Fail("a first-sight open copy asked instead of taking the word");
    if (RestraintCopyDecide(true, 1, 20, 3, 20, false, kRestraintAskNone) != kRestraintRewrite) Fail("a first-sight broken copy asked instead of taking the word");
    if (RestraintCopyDecide(true, 1, 20, 0, 0, false, kRestraintAskNone) != kRestraintRewrite) Fail("a first-sight level-0 copy asked instead of taking the word");
    /* seen equal to the closed word, then open here: a rescuer's pick - asked once */
    if (RestraintCopyDecide(true, 1, 20, 0, 20, true, kRestraintAskNone) != kRestraintAsk) Fail("a copy seen closed and opened here did not ask");
    if (RestraintCopyDecide(true, 1, 20, 3, 20, true, kRestraintAskNone) != kRestraintAsk) Fail("a copy seen closed and broken here did not ask");
    if (RestraintCopyDecide(true, 1, 20, 0, 20, true, kRestraintAskFresh) != kRestraintKeep) Fail("a copy with a fresh ask was rewritten");
    if (RestraintCopyDecide(true, 1, 20, 0, 20, true, kRestraintAskStale) != kRestraintRewrite) Fail("a copy with a stale ask kept its opening");
    /* any other difference takes the owner's word, seen or not */
    for (int seen = 0; seen <= 1; ++seen)
    {
        if (RestraintCopyDecide(true, 0, 20, 1, 20, seen != 0, kRestraintAskNone) != kRestraintRewrite) Fail("a copy closed here under an open word was not rewritten");
        if (RestraintCopyDecide(true, 0, 20, 2, 20, seen != 0, kRestraintAskNone) != kRestraintRewrite) Fail("an open copy differing from an open word was not rewritten");
        if (RestraintCopyDecide(true, 1, 20, 1, 15, seen != 0, kRestraintAskNone) != kRestraintRewrite) Fail("a closed copy with another level was not rewritten");
    }
    /* the owner opening on a request keeps its own level: clears locked, or sets broken for a broken request */
    if (RestraintOwnerOpened(kLockFlagLocked, 0) != 0) Fail("an opening request did not clear the owner's locked byte");
    if (RestraintOwnerOpened(kLockFlagLocked, kLockFlagLocked | kLockFlagBroken) != (kLockFlagLocked | kLockFlagBroken)) Fail("a broken request did not break the owner's lock");
    if (RestraintOwnerOpened(kLockFlagLocked, kLockFlagLocked) != 0) Fail("a level-0 request did not open the owner's lock");
    if (!LockOpen(RestraintOwnerOpened(kLockFlagLocked, 0), 20)) Fail("the owner's opened lock is not open at its own level");
    /* the owner and a copy's game's request: only an opening of a closed lock */
    if (!RestraintOwnerAccepts(1, 20, 0, 20)) Fail("the owner refused to open a closed lock");
    if (!RestraintOwnerAccepts(1, 20, 3, 20)) Fail("the owner refused a broken lock");
    if (RestraintOwnerAccepts(0, 20, 0, 20)) Fail("the owner took a request for a lock already open");
    if (RestraintOwnerAccepts(1, 20, 1, 20)) Fail("the owner took a closing request");
    if (RestraintOwnerAccepts(0, 20, 1, 20)) Fail("the owner closed its lock on a request");
    /* a copy out of its cage: a rescue only with the cage open here and in the word, not ours, no guard's pardon */
    /* a copy found not restrained: a rescue window only for a cage open here under an open word */
    if (!RestraintLeftOpenRescue(true, true, true, 0, 20)) Fail("an open cage left under an open word opened no rescue window");
    if (RestraintLeftOpenRescue(false, true, true, 0, 20)) Fail("shackles taken off opened a rescue window");
    if (RestraintLeftOpenRescue(true, false, true, 0, 20)) Fail("a cage with no word opened a rescue window");
    if (RestraintLeftOpenRescue(true, true, false, 0, 20)) Fail("a cage closed here opened a rescue window");
    if (RestraintLeftOpenRescue(true, true, true, kLockFlagLocked, 20)) Fail("a cage under a closed word opened a rescue window");
    /* CageRescueRelease(sawCaged, oursOut, guardFresh, guardArrest, pardonLookOver, openHere, wordOpen) */
    if (!CageRescueRelease(true, false, false, false, false, true, true)) Fail("an open-cage rescue was not told");
    if (CageRescueRelease(true, false, false, false, false, true, false)) Fail("a rescue was told under a closed word");
    if (CageRescueRelease(true, false, false, false, false, false, true)) Fail("a rescue was told with the cage closed here");
    if (CageRescueRelease(true, true, false, false, false, true, true)) Fail("our own cage-out was told as a rescue");
    if (CageRescueRelease(true, false, true, true, true, true, true)) Fail("a guard's release was told as a rescue");
    if (CageRescueRelease(false, false, false, false, false, true, true)) Fail("a copy never seen caged was told as a rescue");
    if (CageRescueRelease(true, false, false, true, false, true, true)) Fail("a rescue was told during a jailer's pardon second look");
    if (!CageRescueRelease(true, false, false, true, true, true, true)) Fail("a rescue of this game's arrest was not told after the pardon look ran out");
}

/* MSG_PRISON kinds BED IN (5: bed key required, outer key, three finite floats) and BED REFUSED (6: the refused bed's key,
   no outer key, u8 permanent 0 / 1) */
void t_bed_prison_wire()
{
    cooprison::PrisonMsg m;
    m.uid = 738197506u; m.kind = cooprison::kPrisonBedIn;
    m.cageKey = "Bed|43,11|5290,23,-9493"; m.outerKey = "Hut|43,11|5301,20,-9480";
    m.pos[0] = 5290.5f; m.pos[1] = 23.25f; m.pos[2] = -9493.75f;
    std::vector<char> b;
    if (!cooprison::EncodePrison(&b, m)) { Fail("a BED IN did not encode"); return; }
    if (b.size() != 4 + 1 + 1 + m.cageKey.size() + 1 + m.outerKey.size() + 12) Fail("a BED IN has the wrong length " + I((int)b.size()));
    cooprison::PrisonMsg o;
    if (cooprison::DecodePrison(&b[0], b.size(), &o) != cooprison::kPrisonDecodeOk || o.uid != m.uid || o.kind != cooprison::kPrisonBedIn
        || o.cageKey != m.cageKey || o.outerKey != m.outerKey || o.pos[0] != 5290.5f || o.pos[1] != 23.25f || o.pos[2] != -9493.75f)
        Fail("the BED IN did not survive");
    for (size_t n = 0; n < b.size(); ++n)
        if (cooprison::DecodePrison(&b[0], n, &o) == cooprison::kPrisonDecodeOk) { Fail("a cut BED IN decoded at " + I((int)n)); break; }
    {   /* no outer key: the position follows the empty outer key */
        cooprison::PrisonMsg n = m; n.outerKey.clear();
        std::vector<char> nb;
        if (!cooprison::EncodePrison(&nb, n) || cooprison::DecodePrison(&nb[0], nb.size(), &o) != cooprison::kPrisonDecodeOk
            || !o.outerKey.empty() || o.pos[2] != -9493.75f)
            Fail("a BED IN with no outer key did not survive");
    }
    {   /* the bed key is required */
        cooprison::PrisonMsg z = m; z.cageKey.clear();
        std::vector<char> zb;
        if (cooprison::EncodePrison(&zb, z) || !zb.empty()) Fail("a BED IN with no bed key was encoded");
        std::vector<char> e(b); e[5] = 0;
        if (cooprison::DecodePrison(&e[0], e.size(), &o) != cooprison::kPrisonDecodeBadKey) Fail("a BED IN with an empty bed key was accepted");
    }
    {   /* every coordinate must be a finite number */
        const unsigned int bits[2] = { 0x7F800000u, 0x7FC00000u };   /* +infinity, a NaN */
        for (int k = 0; k < 2; ++k)
            for (int axis = 0; axis < 3; ++axis)
            {
                float bad; std::memcpy(&bad, &bits[k], 4);
                cooprison::PrisonMsg z = m; z.pos[axis] = bad;
                std::vector<char> zb;
                if (cooprison::EncodePrison(&zb, z) || !zb.empty()) Fail("a BED IN with a non-finite position was encoded");
                std::vector<char> e(b);
                std::memcpy(&e[e.size() - 12 + 4 * axis], &bits[k], 4);
                if (cooprison::DecodePrison(&e[0], e.size(), &o) != cooprison::kPrisonDecodeBadPos) Fail("a BED IN with a non-finite position was accepted");
            }
    }
    {   /* BED REFUSED with no key: the permanent byte follows the two empty keys */
        cooprison::PrisonMsg r; r.uid = 80u; r.kind = cooprison::kPrisonBedRefused;
        std::vector<char> rb;
        if (!cooprison::EncodePrison(&rb, r) || rb.size() != 8) { Fail("a BED REFUSED did not encode to 8 bytes"); return; }
        cooprison::PrisonMsg ro;
        if (cooprison::DecodePrison(&rb[0], rb.size(), &ro) != cooprison::kPrisonDecodeOk || ro.uid != 80u
            || ro.kind != cooprison::kPrisonBedRefused || !ro.cageKey.empty() || !ro.outerKey.empty() || ro.permanent != 0)
            Fail("a BED REFUSED did not survive");
        if (cooprison::DecodePrison(&rb[0], 7, &ro) != cooprison::kPrisonDecodeTooShort) Fail("a BED REFUSED with no permanent byte was accepted");
    }
    {   /* BED REFUSED naming the bed, for good */
        cooprison::PrisonMsg r; r.uid = 82u; r.kind = cooprison::kPrisonBedRefused; r.cageKey = m.cageKey; r.permanent = 1;
        std::vector<char> rb;
        if (!cooprison::EncodePrison(&rb, r) || rb.size() != 4 + 1 + 1 + r.cageKey.size() + 1 + 0 + 1) { Fail("a keyed BED REFUSED has the wrong length"); return; }
        cooprison::PrisonMsg ro;
        if (cooprison::DecodePrison(&rb[0], rb.size(), &ro) != cooprison::kPrisonDecodeOk || ro.uid != 82u
            || ro.kind != cooprison::kPrisonBedRefused || ro.cageKey != m.cageKey || !ro.outerKey.empty() || ro.permanent != 1)
            Fail("a keyed BED REFUSED did not survive");
        for (size_t n = 0; n < rb.size(); ++n)
            if (cooprison::DecodePrison(&rb[0], n, &ro) == cooprison::kPrisonDecodeOk) { Fail("a cut BED REFUSED decoded at " + I((int)n)); break; }
        std::vector<char> e(rb); e[e.size() - 1] = 2;
        if (cooprison::DecodePrison(&e[0], e.size(), &ro) != cooprison::kPrisonDecodeBadFlag) Fail("a BED REFUSED with permanent 2 was accepted");
        cooprison::PrisonMsg z = r; z.permanent = 2;
        std::vector<char> zb;
        if (cooprison::EncodePrison(&zb, z) || !zb.empty()) Fail("a BED REFUSED with permanent 2 was encoded");
    }
    {   /* the other kinds carry no position bytes: a cage IN keeps its old length */
        cooprison::PrisonMsg c; c.uid = 81u; c.kind = cooprison::kPrisonIn; c.cageKey = "Prison cage|1,1|0,0,0";
        std::vector<char> cb;
        if (!cooprison::EncodePrison(&cb, c) || cb.size() != 4 + 1 + 1 + c.cageKey.size() + 1 + 0 + 1) Fail("a cage IN changed its length");
    }
}

/* heal1: MSG_TREAT - a medic on one game treated the other game's character's copy. */
void t_heal1_treat_wire()
{
    cooptreat::TreatMsg m; m.uid = 0xABCD1234u; m.n = 3;
    m.bandageLevel[0] = 0.0f; m.bandageLevel[1] = 12.5f; m.bandageLevel[2] = 40.0f;
    m.splintLevel[0] = 0.0f; m.splintLevel[1] = 0.0f; m.splintLevel[2] = 7.25f;
    std::vector<char> b;
    if (!cooptreat::EncodeTreat(&b, m) || b.size() != 5 + 3 * 8) { Fail("a 3-part TREAT did not encode to 29 bytes"); return; }
    cooptreat::TreatMsg o;
    if (cooptreat::DecodeTreat(&b[0], b.size(), &o) != cooptreat::kTreatDecodeOk || o.uid != m.uid || o.n != 3
        || o.bandageLevel[1] != 12.5f || o.bandageLevel[2] != 40.0f || o.splintLevel[2] != 7.25f || o.splintLevel[0] != 0.0f)
        Fail("a TREAT did not survive the round trip");
    if (cooptreat::DecodeTreat(&b[0], b.size() - 1, &o) != cooptreat::kTreatDecodeTooShort) Fail("a short TREAT was accepted");
    std::vector<char> z(b); z[4] = 0;
    if (cooptreat::DecodeTreat(&z[0], z.size(), &o) != cooptreat::kTreatDecodeBadCount) Fail("a TREAT with 0 parts was accepted");
    std::vector<char> big(b); big[4] = (char)(cooptreat::kTreatMaxParts + 1);
    if (cooptreat::DecodeTreat(&big[0], big.size(), &o) != cooptreat::kTreatDecodeBadCount) Fail("a TREAT above the part limit was accepted");
    std::vector<char> neg(b); const float mneg = -1.0f; std::memcpy(&neg[5], &mneg, 4);
    if (cooptreat::DecodeTreat(&neg[0], neg.size(), &o) != cooptreat::kTreatDecodeBadValue) Fail("a negative bandaging was accepted");
    cooptreat::TreatMsg nan = m; const unsigned int nanBits = 0x7FC00000u; std::memcpy(&nan.bandageLevel[1], &nanBits, 4);
    std::vector<char> nb;
    if (cooptreat::EncodeTreat(&nb, nan) || !nb.empty()) Fail("a NaN treatment was encoded");
    cooptreat::TreatMsg none; std::vector<char> eb;
    if (cooptreat::EncodeTreat(&eb, none)) Fail("a TREAT with no parts was encoded");
}

/* T-1 B1 (owner 110): a copy squad is kept awake only when EVERY member is a peer copy AND one stands on loaded ground here
   AND it is not a player's squad - both branches, and each input on its own flips the answer. */
void t_t1b1_keep_awake()
{
    if (coopsquad::KeepCopySquadAwake(1, 1, 0) != 1) Fail("all copies, one on loaded ground, not a player squad: refresh");
    if (coopsquad::KeepCopySquadAwake(0, 1, 0) != 0) Fail("a member this game runs (or with no uid): no refresh");
    if (coopsquad::KeepCopySquadAwake(1, 0, 0) != 0) Fail("no member on loaded ground here: no refresh (the squad may sleep)");
    if (coopsquad::KeepCopySquadAwake(1, 1, 1) != 0) Fail("a player squad is never touched");
    if (coopsquad::KeepCopySquadAwake(0, 0, 1) != 0 || coopsquad::KeepCopySquadAwake(0, 0, 0) != 0) Fail("nothing holds: no refresh");
}

/* T-1 B1: the follow pass - "uid, else position"; a leader run here keeps the squad; a leader the other game runs takes
   the members unless an XFER for it is in flight (or was refused moments ago). */
void t_t1b1_follow_action()
{
    if (coopsquad::FollowAction(0, 0, 0) != coopsquad::kFollowByPosition) Fail("a leader with no uid falls back to position");
    if (coopsquad::FollowAction(0, 1, 1) != coopsquad::kFollowByPosition) Fail("no uid wins over the other inputs");
    if (coopsquad::FollowAction(1, 1, 0) != coopsquad::kFollowStay) Fail("a leader this game runs: the members stay");
    if (coopsquad::FollowAction(1, 1, 1) != coopsquad::kFollowStay) Fail("in flight does not matter when the leader is here");
    if (coopsquad::FollowAction(1, 0, 0) != coopsquad::kFollowSend) Fail("a leader the other game runs: the members are sent");
    if (coopsquad::FollowAction(1, 0, 1) != coopsquad::kFollowWait) Fail("an XFER in flight: wait");
}

/* T-1 B1: the receiver's take - by the SQUAD position when it runs or is taking the leader and knows the squad position,
   else by each member's own position (F448's old test). */
void t_t1b1_xfer_take()
{
    if (coopsquad::XferTakeMember(1, 1, 1, 0) != 1) Fail("squad in my ring, member outside: taken (the squad decides)");
    if (coopsquad::XferTakeMember(1, 1, 0, 1) != 0) Fail("squad outside my ring, member inside: refused (the squad decides)");
    if (coopsquad::XferTakeMember(1, 1, 1, 1) != 1) Fail("squad and member in my ring: taken");
    if (coopsquad::XferTakeMember(0, 1, 1, 0) != 0) Fail("leader neither here nor taken: the member's own position decides");
    if (coopsquad::XferTakeMember(0, 1, 0, 1) != 1) Fail("leader neither here nor taken, member inside: taken");
    if (coopsquad::XferTakeMember(1, 0, 1, 0) != 0) Fail("squad position unknown: the member's own position decides");
    if (coopsquad::XferTakeMember(1, 0, 0, 1) != 1) Fail("squad position unknown, member inside: taken");
}

/* M7a3 (T772): every engine reason the decompiles name, by class - the unload reasons, the ending reasons, and the ambiguous
   rest (incl. an unknown text, the engine's default 'undefined' and a null). */
void t_m7a3_removal_reason_class()
{
    using namespace coopremoval;
    if (EngineRemovalReasonClass("corpse unloaded") != kReasonUnload) Fail("corpse unloaded: its zone is not loaded - UNLOAD");
    if (EngineRemovalReasonClass("ZoneMapContent::deactivate") != kReasonUnload) Fail("zone deactivate - UNLOAD");
    if (EngineRemovalReasonClass("delayedSpawningChecks") != kReasonUnload) Fail("delayed spawn, zone neither loaded nor loading - UNLOAD");
    if (EngineRemovalReasonClass("eaten") != kReasonDeath) Fail("eaten - DEATH");
    if (EngineRemovalReasonClass("corpse decayed") != kReasonDeath) Fail("corpse decayed - DEATH");
    if (EngineRemovalReasonClass("createRandomSquad") != kReasonDeath) Fail("createRandomSquad discard - DESTROY");
    if (EngineRemovalReasonClass("_putTheSpecialCharactersInNewSquads") != kReasonDeath) Fail("special-character discard - DESTROY");
    if (EngineRemovalReasonClass(kDuplicateNamedRemoved) != kReasonDeath) Fail("the plugin's removal of a duplicate named body - DEATH (DESPAWN)");
    if (EngineRemovalReasonClass("duplicate named character removed") != kReasonDeath) Fail("the duplicate reason compared by value - DEATH");
    if (RemovalIsUnload(0, EngineRemovalReasonClass(kDuplicateNamedRemoved), 0) != 0) Fail("a living duplicate removed - not sent as DESPAWN");
    if (EngineRemovalReasonClass("platoon destructor") != kReasonAmbiguous) Fail("platoon destructor - ambiguous (isDead decides)");
    if (EngineRemovalReasonClass("wandered out of zone") != kReasonAmbiguous) Fail("wandered out of zone - ambiguous");
    if (EngineRemovalReasonClass("leader wandered out of zone") != kReasonAmbiguous) Fail("leader wandered out of zone - ambiguous");
    if (EngineRemovalReasonClass("ActivePlatoon::destroyCharacters") != kReasonAmbiguous) Fail("destroyCharacters - ambiguous");
    if (EngineRemovalReasonClass("undefined") != kReasonAmbiguous) Fail("the default literal - ambiguous");
    if (EngineRemovalReasonClass("death paraded") != kReasonAmbiguous) Fail("a reason not decompiled - ambiguous");
    if (EngineRemovalReasonClass(0) != kReasonAmbiguous) Fail("no reason - ambiguous");
    if (EngineRemovalReasonClass("eaten ") != kReasonAmbiguous || EngineRemovalReasonClass("eate") != kReasonAmbiguous) Fail("compared whole, not by prefix");
}

/* M7a3 (T772): UNLOAD or DESPAWN - the engine flag wins, then the class; an ambiguous reason is a death only on hasDied == 1. */
void t_m7a3_removal_is_unload()
{
    using namespace coopremoval;
    if (RemovalIsUnload(1, kReasonDeath, 1) != 1) Fail("the engine's own unload flag always means UNLOAD");
    if (RemovalIsUnload(0, kReasonUnload, 1) != 1) Fail("an unload reason: UNLOAD even for a body");
    if (RemovalIsUnload(0, kReasonDeath, 0) != 0) Fail("an ending reason: DESPAWN even when isDead reads 0 (eaten alive)");
    if (RemovalIsUnload(0, kReasonAmbiguous, 0) != 1) Fail("ambiguous, alive: UNLOAD (T772's platoon walk)");
    if (RemovalIsUnload(0, kReasonAmbiguous, 1) != 0) Fail("ambiguous, isDead confirms: DESPAWN");
    if (RemovalIsUnload(0, kReasonAmbiguous, -1) != 1) Fail("ambiguous, isDead not read / faulted: not a confirmation - UNLOAD");
}





/* M7a2 [m7a2-t0] (T803: 220 people in two owed records, each taken twice, the second ACK counted noKey): one person, one hand-over. */
void t_m7a2_handover_once()
{
    using namespace coopsquad;
    if (HandoverAddAction(0, 0, 0) != kHoAddPut) Fail("into an owed record, held nowhere else: added");
    if (HandoverAddAction(0, 1, 0) != kHoAddSkip) Fail("T803: the put-away's record of one while the squad record holds the person - not added (the squad record keeps it)");
    if (HandoverAddAction(0, 1, 1) != kHoAddSkip) Fail("held by another record and named by a flight: never a second record");
    if (HandoverAddAction(0, 0, 1) != kHoAddPut) Fail("named by a flight only: the record holds it (its UNLOAD stays held) and its resume waits for that flight");
    if (HandoverAddAction(1, 0, 0) != kHoAddPut) Fail("into an XFER, held nowhere else: sent");
    if (HandoverAddAction(1, 0, 1) != kHoAddHold) Fail("into an XFER while another XFER in flight names the person: held back - never two flights");
    if (HandoverAddAction(1, 1, 0) != kHoAddHold) Fail("the put-away flush, the person in an owed record: it stays owed there, not sent beside it");
    if (HandoverAddAction(1, 1, 1) != kHoAddHold) Fail("held by a record and a flight: held");
    /* T803's order: the pass's squad record {L, a, b} keyed on L, then the put-away of a and of b keyed on themselves (L's row gone) */
    {
        std::vector<unsigned int> rec;
        rec.push_back(101); rec.push_back(102); rec.push_back(103);
        int records = 1, entries = 3, skipped = 0;
        for (unsigned int u = 102; u <= 103; ++u)
        {
            int inOther = 0;
            for (size_t k = 0; k < rec.size(); ++k) if (rec[k] == u) inOther = 1;
            if (HandoverAddAction(0, inOther, 0) == kHoAddSkip) ++skipped; else { ++records; ++entries; }
        }
        if (records != 1 || entries != 3 || skipped != 2) Fail("T803: one record of three, two put-away adds skipped - every person sent once at the resume");
    }
}
/* M7a2 [m7a2-tc1] (T803: every LOAD-OVERRIDE fired into a new context platoon; A wrote B's 21 people at a sleep): the context-platoon
   load / wake / sleep decisions. */
void t_m7a2_context_platoon_record()
{
    using namespace coopsquad;
    if (ContextRecordApply(1) != kCtxRecSkip) Fail("load/wake: a newer record is never swapped into a context platoon (its people arrive by SPAWN)");
    if (ContextRecordApply(0) != kCtxRecApply) Fail("load/wake: any other squad takes its newer record as today");
    /* sleep: isContext, isHandedOver, known, mine */
    if (ContextSleepWrite(1, 0, 21, 0) != kCtxSleepRefuseContext) Fail("sleep: a context platoon of another game's people - refused");
    if (ContextSleepWrite(1, 0, 0, 0) != kCtxSleepRefuseContext) Fail("sleep: a context platoon whose uids are gone - refused (context wins)");
    if (ContextSleepWrite(1, 1, 5, 0) != kCtxSleepRefuseContext) Fail("sleep: context and handed over - counted as context");
    if (ContextSleepWrite(1, 0, 3, 2) != kCtxSleepGoContextWithMine) Fail("sleep: a context platoon holding people this game runs (a take-back) - NOT refused, the existing rules decide (counted contextSleepWithMine)");
    if (ContextSleepWrite(1, 1, 5, 1) != kCtxSleepGoContextWithMine) Fail("sleep: context, handed over, one person still mine - goes on to the existing rules");
    if (ContextSleepWrite(1, 0, 1, 1) != kCtxSleepGoContextWithMine) Fail("sleep: a context platoon of one person this game runs - goes on");
    if (ContextSleepWrite(0, 1, 21, 0) != kCtxSleepRefuseHandedOver) Fail("T803 A 808.376: handed over, 21 known, none mine - refused");
    if (ContextSleepWrite(0, 1, 0, 0) != kCtxSleepGo) Fail("handed over, no uid known (gone by sleep time): rule E2 decides, not this gate");
    if (ContextSleepWrite(0, 1, 4, 1) != kCtxSleepGo) Fail("handed over, one member still mine: the existing rules decide");
    if (ContextSleepWrite(0, 0, 21, 0) != kCtxSleepGo) Fail("not handed over, none mine: the existing rules decide (E2 / member rule)");
    if (ContextSleepWrite(0, 0, 0, 0) != kCtxSleepGo) Fail("an ordinary squad with no uid known: unchanged (E2)");
    if (ContextSleepWrite(0, 0, 3, 3) != kCtxSleepGo) Fail("an ordinary squad of mine: written as today");
    /* the heartbeat publish (HbOwnedAwake) reads the same table: every refusal above is a refusal there, every go-on a go-on */
    { int refusals = 0; for (int c = 0; c <= 1; ++c) for (int h = 0; h <= 1; ++h) for (int k = 0; k <= 2; ++k) for (int m = 0; m <= k; ++m)
        { const int sw = ContextSleepWrite(c, h, k, m); if (sw == kCtxSleepRefuseContext || sw == kCtxSleepRefuseHandedOver) ++refusals;
          if ((sw == kCtxSleepRefuseContext || sw == kCtxSleepRefuseHandedOver) != (m == 0 && (c != 0 || (h != 0 && k > 0)))) Fail("the table: refused exactly when none is mine and (context, or handed over with a known member)"); }
      if (refusals != 8) Fail("the table: 6 context rows with none mine (handed over or not, known 0..2) + 2 handed-over rows with a known member and none mine"); }
}

/* BlockSquadWrite: a squad numbered in this game's block whose known members are all another game's is neither written nor published as
   this game's; E2's own case (no uid known), a squad holding a person this game runs, and a squad of another block are left to the
   existing rules. */
void t_t574_block_squad_other_game()
{
    using namespace coopsquad;
    /* mineByBlock, chars, known, mine, releasedHere */
    if (BlockSquadWrite(1, 1, 1, 0, 0) != kBlockWriteRefuseOtherGame) Fail("the logged shape: my block, one member, another game's copy that arrived by SPAWN - refused");
    if (BlockSquadWrite(1, 21, 21, 0, 0) != kBlockWriteRefuseOtherGame) Fail("my block, 21 members, all another game's copies - refused");
    if (BlockSquadWrite(1, 1, 1, 0, 1) != kBlockWriteGo) Fail("released in place: my squad, its one person handed to another game (dual run / revoke / hand-over) - written");
    if (BlockSquadWrite(1, 5, 5, 0, 2) != kBlockWriteGo) Fail("released in place: two of five handed over in place, three copies - written");
    if (BlockSquadWrite(1, 3, 2, 0, 0) != kBlockWriteGo) Fail("known < chars: a member not identified (maybe this game's own) - written");
    if (BlockSquadWrite(1, 3, 0, 0, 0) != kBlockWriteGo) Fail("my block, no uid known (gone by sleep time) - E2 decides: written");
    if (BlockSquadWrite(1, 3, 3, 1, 0) != kBlockWriteGo) Fail("my block, one of mine beside two of another game's - written");
    if (BlockSquadWrite(1, 4, 4, 4, 0) != kBlockWriteGo) Fail("my block, every member mine - written");
    if (BlockSquadWrite(0, 2, 2, 0, 0) != kBlockWriteGo) Fail("not my block - the member rule decides (and counts), not this one");
    if (BlockSquadWrite(1, 2, 2, 0, -1) != kBlockWriteGo) Fail("the released read faulted (-1) - read as some: written");
    { int refusals = 0;
      for (int b = 0; b <= 1; ++b) for (int ch = 0; ch <= 3; ++ch) for (int k = 0; k <= ch; ++k) for (int m = 0; m <= k; ++m) for (int rh = -1; rh <= k - m; ++rh)
      {
          const int w = BlockSquadWrite(b, ch, k, m, rh);
          if (w == kBlockWriteRefuseOtherGame) ++refusals; else if (w != kBlockWriteGo) Fail("the table: only Go or RefuseOtherGame");
          if ((w == kBlockWriteRefuseOtherGame) != (b != 0 && k > 0 && k == ch && m == 0 && rh == 0)) Fail("the table: refused exactly when my block, every member identified, none mine, none released here");
      }
      if (refusals != 3) Fail("the table: my block, chars 1..3 all identified, none mine, none released - three rows"); }
    /* the gates in the order the sleep and the heartbeat ask them (ContextSleepWrite, then BlockSquadWrite): a squad that goes on to be
       written holds a person this game runs or ran, a member not identified, no known person at all, or is not numbered here */
    for (int c = 0; c <= 1; ++c) for (int h = 0; h <= 1; ++h) for (int b = 0; b <= 1; ++b) for (int k = 0; k <= 3; ++k) for (int m = 0; m <= k; ++m) for (int rh = 0; rh <= 1; ++rh)
    {
        const int ch = 3;
        const int sw = ContextSleepWrite(c, h, k, m);
        const bool goes = (sw == kCtxSleepGo || sw == kCtxSleepGoContextWithMine) && BlockSquadWrite(b, ch, k, m, rh) == kBlockWriteGo;
        if (goes && c == 0 && b != 0 && k == ch && m == 0 && rh == 0) Fail("both gates: a squad formed here around another game's people went on");
        if (!goes && m > 0) Fail("both gates: a squad holding a person this game runs was refused");
        if (!goes && c == 0 && h == 0 && rh != 0) Fail("both gates: a squad released in place (not marked handed over) was refused");
        if (!goes && c == 0 && h == 0 && k < ch) Fail("both gates: a squad with a member not identified was refused");
        if (c == 0 && h != 0 && k > 0 && m == 0 && sw != kCtxSleepRefuseHandedOver) Fail("both gates: a handed-over squad keeps its own refusal and counter");
    }
}

/* M7a3f3 H1 [m7a3f3-t2]: the three announce states - only an explicit withheld mark says "the other games were never told"; 0 and 1 keep
   the meaning the former 0/1 argument had. */
void t_m7a3f3_announced_three_state()
{
    using namespace coopsquad;
    if (kAnnWithheld != 0 || kAnnAnnounced != 1) Fail("withheld is 0 and announced is 1 (the former argument's values)");
    if (AnnouncedToOthers(kAnnNoEntry) != 1) Fail("no entry: the other games hold a copy (taken by hand-over / before the gate)");
    if (AnnouncedToOthers(kAnnWithheld) != 0) Fail("explicit withheld: no copy there");
    if (AnnouncedToOthers(kAnnAnnounced) != 1) Fail("explicit announced: a copy there");
}
/* M7a3f3 T-425 [m7a3f3-t3]: my own id refuses the supersede unless I handed that squad over. */
void t_m7a3f3_handed_over_supersede()
{
    using namespace coopsquad;
    if (SupersedeOwnIdAfterHandover(1, 0) != 1) Fail("my id, not handed over: my squad is the original - refused (inv7a3)");
    if (SupersedeOwnIdAfterHandover(1, 1) != 0) Fail("my id, handed over: my copy is stale - superseded (T735)");
    if (SupersedeOwnIdAfterHandover(0, 0) != 0) Fail("another game's id: superseded as before");
    if (SupersedeOwnIdAfterHandover(0, 1) != 0) Fail("another game's id, marked: superseded as before");
}
/* M7a3f3 T-425 [m7a3f3-t4]: the wake of a handed-over squad - refused only where another game holds the area (or it is frozen), a world
   faction. The rule has no player-count input: a host alone and a host with others take the same verdict for the same area answer. */
void t_m7a3f3_handed_over_wake()
{
    using namespace coopsquad;
    if (HandedOverWakeRefuse(1, 1, 1) != 1) Fail("handed over, another game holds the area, world faction: refused");
    if (HandedOverWakeRefuse(0, 1, 1) != 0) Fail("not handed over: woken");
    if (HandedOverWakeRefuse(1, 0, 1) != 0) Fail("nobody else holds the area: woken (this game runs it)");
    if (HandedOverWakeRefuse(1, -1, 1) != 0) Fail("no fresh map: woken (nothing is thrown away on a guess)");
    if (HandedOverWakeRefuse(1, -2, 1) != 1) Fail("a frozen area (its holder away - what a host alone reads for another player's area): refused, as held");
    if (HandedOverWakeRefuse(1, 1, 0) != 0) Fail("a player / peer / stand-in squad: never refused");
}
/* M7a3f3 T-425 [m7a3f3-t5]: the sweep and a rebuilt person of a handed-over squad. */
void t_m7a3f3_handed_over_sweep()
{
    using namespace coopsquad;
    if (HandedOverSweepAction(0, 1, 1) != kHoSweepAdopt) Fail("not handed over: the sweep's own rules");
    if (HandedOverSweepAction(1, 1, 0) != kHoSweepSkip) Fail("the other game's live copy here: not adopted");
    if (HandedOverSweepAction(1, 0, 1) != kHoSweepSkip) Fail("another game holds the area: not adopted");
    if (HandedOverSweepAction(1, 0, -1) != kHoSweepSkip) Fail("no fresh map: not adopted (not known to be mine alone)");
    if (HandedOverSweepAction(1, 0, 0) != kHoSweepAdoptUnmark) Fail("fresh map, nobody else, no copy here: adopted and unmarked");
}
/* M7a3f4 [m7a3f4-t4]: the awake retire of a handed-over squad's own copy is refused while a person of ours is in it. */
void t_m7a3f4_retire_refused_when_mine()
{
    using namespace coopsquad;
    if (HandedOverRetireRefuse(1, 1) != 1) Fail("a person this game runs in the platoon: not retired");
    if (HandedOverRetireRefuse(1, -1) != 1) Fail("unreadable platoon: not retired (fail closed)");
    if (HandedOverRetireRefuse(1, 0) != 0) Fail("nobody of ours in it: retired");
    if (HandedOverRetireRefuse(0, 3) != 0) Fail("not the handed-over case: this rule does not apply");
}


/* T-1 B1 / protocol 84: the XFER reason names (1 keeps "forced"), and the squad position's usability test. */
void t_t1b1_reason_and_pos()
{
    if (std::string(coopsquad::XferReasonName(coopsquad::kXferReasonForced)) != "forced") Fail("reason 1 is forced (the old meaning)");
    if (std::string(coopsquad::XferReasonName(coopsquad::kXferReasonFollowLeader)) != "follow-leader") Fail("reason 2 is follow-leader");
    if (std::string(coopsquad::XferReasonName(coopsquad::kXferReasonNone)) != "none") Fail("reason 0 is none");
    if (std::string(coopsquad::XferReasonName(7)) != "unknown") Fail("an unknown reason is named unknown");
    if (coopsquad::kXferReasonForced != 1 || coopsquad::kXferReasonFollowLeader != 2) Fail("the wire values");
    if (coopsquad::SquadPosUsable(0.0f, 0.0f, 0.0f) != 0) Fail("the untouched origin is not a squad position");
    if (coopsquad::SquadPosUsable(-12000.5f, 1500.0f, 33000.0f) != 1) Fail("a real position is usable");
    if (coopsquad::SquadPosUsable(0.0f, 0.0f, 5.0f) != 1) Fail("one non-zero axis is enough");
    const float inf = std::numeric_limits<float>::infinity();
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    if (coopsquad::SquadPosUsable(inf, 0.0f, 1.0f) != 0 || coopsquad::SquadPosUsable(1.0f, qnan, 1.0f) != 0 || coopsquad::SquadPosUsable(1.0f, 1.0f, -inf) != 0) Fail("a non-finite axis is refused");
    /* T-164 B4-3 (M4): a trader squad with a home building is decided by the home, whatever its own position; others as before */
    if (coopsquad::SquadDecisionChoice(1, 1) != coopsquad::kDecideHome) Fail("M4: a keeper squad is decided by its home");
    if (coopsquad::SquadDecisionChoice(1, 0) != coopsquad::kDecideHome) Fail("M4: the home decides even before the squad position is usable");
    if (coopsquad::SquadDecisionChoice(0, 1) != coopsquad::kDecideSquad) Fail("M4: any other squad keeps its squad position");
    if (coopsquad::SquadDecisionChoice(0, 0) != coopsquad::kDecideNone) Fail("M4: no home and no usable position -> the caller's own");
}

/* T-1 B1 fold (review H2): the agreed squad leader is SYMMETRIC (each game's inputs swapped give the same uid), each branch, and
   exactly one of the two games sends; the engine-only fallback never lets both games send. */
void t_t1b1_agreed_leader()
{
    const unsigned int v[] = { 0u, 1u, 5u, 6u, 9u, 77u, 0xFFFFFFFEu, 0xFFFFFFFFu };
    const int nv = (int)(sizeof(v) / sizeof(v[0]));
    for (int i = 0; i < nv; ++i) for (int j = 0; j < nv; ++j)
        if (coopsquad::AgreedSquadLeader(v[i], v[j]) != coopsquad::AgreedSquadLeader(v[j], v[i])) Fail("the agreed leader is not symmetric");
    if (coopsquad::AgreedSquadLeader(0, 0) != 0) Fail("nobody names a leader: unknown");
    if (coopsquad::AgreedSquadLeader(7, 0) != 7) Fail("only this game names one: that one");
    if (coopsquad::AgreedSquadLeader(0, 7) != 7) Fail("only the other game names one: that one");
    if (coopsquad::AgreedSquadLeader(6, 6) != 6) Fail("both name the same: that one");
    if (coopsquad::AgreedSquadLeader(5, 9) != 5 || coopsquad::AgreedSquadLeader(9, 5) != 5) Fail("two different leaders: the lower uid");
}

/* T-1 B2 + T-1 B2 fold (review F1/F3, owner 109): the game running the pack wearers restocks; an unrun trader is stocked by the
   area holder (as shops); a squad or position that will not read fails open (named). */
void t_t1b2_wander_restock_decide()
{
    namespace cw = coopwander;
    if (cw::WanderRestockDecide(1, 3, 2, 0, 0, cw::kWanderAreaNotMine) != cw::kWanderRun) Fail("the wearers are all this game's: restock here");
    if (cw::WanderRestockDecide(1, 3, 0, 2, 0, cw::kWanderAreaMine) != cw::kWanderSkipNotWriter) Fail("the wearers are all the other game's: skip - the area is not asked for a run squad");
    if (cw::WanderRestockDecide(1, 0, 0, 0, 3, cw::kWanderAreaMine) != cw::kWanderRunUnrunArea) Fail("unrun, this game holds the area: restock now (owner 109, as shops)");
    if (cw::WanderRestockDecide(1, 0, 0, 0, 3, cw::kWanderAreaNotMine) != cw::kWanderSkipUnrun) Fail("unrun, the other game holds the area: skip");
    if (cw::WanderRestockDecide(1, 0, 0, 0, 3, cw::kWanderAreaUnread) != cw::kWanderRunUnreadable) Fail("unrun, the squad position will not read: fail open");
    if (cw::WanderRestockDecide(1, 0, 5, 5, 5, cw::kWanderAreaNotMine) != cw::kWanderSkipUnrun) Fail("no member carries a uid: the population counts are not asked");
    if (cw::WanderRestockDecide(0, 3, 2, 0, 0, cw::kWanderAreaNotMine) != cw::kWanderRunUnreadable) Fail("an unreadable squad fails open");
    if (cw::WanderRestockDecide(0, 0, 0, 0, 0, cw::kWanderAreaNotMine) != cw::kWanderRunUnreadable) Fail("unreadable wins over every other answer");
    if (cw::WanderRestockRunsHere(cw::kWanderRun) != 1) Fail("run: runs here");
    if (cw::WanderRestockRunsHere(cw::kWanderRunUnrunArea) != 1) Fail("unrun area holder: runs here");
    if (cw::WanderRestockRunsHere(cw::kWanderRunUnreadable) != 1) Fail("unreadable: runs here (fail open)");
    if (cw::WanderRestockRunsHere(cw::kWanderSkipNotWriter) != 0) Fail("not the writer: does not run");
    if (cw::WanderRestockRunsHere(cw::kWanderSkipUnrun) != 0) Fail("unrun, not the area holder: does not run");
    if (cw::WanderRestockRunsHere(cw::kWanderSkipSplit) != 0) Fail("split: does not run");
    if (cw::WanderRestockRunsHere(-1) != 0) Fail("an unknown answer does not run");
    /* two games, one squad: a run squad restocks on exactly the game running its wearers; an unrun one on exactly the area
       holder (the area verdict answers MINE on one game) - never on both, never on neither */
    for (int n = 1; n <= 4; ++n)
    {
        const int a = cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, n, n, 0, 0, cw::kWanderAreaNotMine));
        const int b = cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, n, 0, n, 0, cw::kWanderAreaMine));
        if (a + b != 1) Fail("a run trader restocks on exactly one game");
        const int ua = cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, 0, 0, 0, n, cw::kWanderAreaMine));
        const int ub = cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, 0, 0, 0, n, cw::kWanderAreaNotMine));
        if (ua != 1 || ub != 0) Fail("an unrun trader restocks on the area holder only");
    }
    const int all[6] = { cw::kWanderRun, cw::kWanderSkipNotWriter, cw::kWanderSkipUnrun, cw::kWanderRunUnreadable, cw::kWanderRunUnrunArea, cw::kWanderSkipSplit };
    for (int i = 0; i < 6; ++i)
    {
        if (std::strcmp(cw::WanderRestockName(all[i]), "?") == 0) Fail("every answer has a name");
        for (int j = i + 1; j < 6; ++j)
            if (std::strcmp(cw::WanderRestockName(all[i]), cw::WanderRestockName(all[j])) == 0) Fail("names are distinct");
    }
}
/* T-1 B2 fold (review F1/F3): a split waits briefly; the deadline each answer writes; which members are the population. */
void t_t1b2f_wander_split_deadlines()
{
    namespace cw = coopwander;
    if (cw::WanderRestockDecide(1, 3, 1, 1, 0, cw::kWanderAreaMine) != cw::kWanderSkipSplit) Fail("wearers mine and the other's: split wait");
    if (cw::WanderRestockDecide(1, 1, 1, 0, 1, cw::kWanderAreaMine) != cw::kWanderSkipSplit) Fail("a wearer with no uid beside one with: split wait (adoption in flight)");
    if (cw::WanderRestockDecide(1, 1, 0, 0, 2, cw::kWanderAreaMine) != cw::kWanderSkipSplit) Fail("a member carries a uid, no wearer does: split wait, not the area rule");
    if (cw::WanderRestockDecide(1, 2, 0, 0, 0, cw::kWanderAreaMine) != cw::kWanderSkipSplit) Fail("an empty population is never a writer");
    if (cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, 3, 1, 1, 0, cw::kWanderAreaMine))
        + cw::WanderRestockRunsHere(cw::WanderRestockDecide(1, 3, 1, 1, 0, cw::kWanderAreaNotMine)) != 0) Fail("a split squad restocks on neither game");
    const int all[6] = { cw::kWanderRun, cw::kWanderSkipNotWriter, cw::kWanderSkipUnrun, cw::kWanderRunUnreadable, cw::kWanderRunUnrunArea, cw::kWanderSkipSplit };
    for (int i = 0; i < 6; ++i)
    {
        const int d = all[i];
        const int runs = cw::WanderRestockRunsHere(d), full = cw::WanderRestockFullDefer(d), shortStep = cw::WanderRestockShortRetry(d);
        if (runs + full + shortStep != 1) Fail("every answer does exactly one of: run, full-period push, short step");
    }
    if (cw::WanderRestockFullDefer(cw::kWanderSkipNotWriter) != 1) Fail("not the writer: a full period (the writer restocks)");
    if (cw::WanderRestockShortRetry(cw::kWanderSkipUnrun) != 1) Fail("unrun: a short step, never a full period (fold F1)");
    if (cw::WanderRestockShortRetry(cw::kWanderSkipSplit) != 1) Fail("split: a short step (fold F3)");
    if (cw::kWanderShortRetryDiv < 2) Fail("the short step is shorter than a period");
    int pm = -1, po = -1, pn = -1;
    cw::WanderPopulation(2, 0, 0, 0, 3, 1, &pm, &po, &pn);
    if (pm != 2 || po != 0 || pn != 0) Fail("wearers present: the other members are not the population");
    cw::WanderPopulation(0, 0, 0, 1, 2, 0, &pm, &po, &pn);
    if (pm != 1 || po != 2 || pn != 0) Fail("no wearer: every member is the population");
    cw::WanderPopulation(0, 0, 1, 3, 0, 0, &pm, &po, &pn);
    if (pm != 0 || po != 0 || pn != 1) Fail("a wearer with no uid is the population");
    cw::WanderPopulation(1, 2, 0, 0, 0, 0, &pm, &po, &pn);
    if (cw::WanderRestockDecide(1, 3, pm, po, pn, cw::kWanderAreaMine) != cw::kWanderSkipSplit) Fail("population into decide: split");
}
/* T-1 B3 restructure (protocol 89): MSG_SQUAD_LEAD carries the squad's cats - round trip (zero, T530's 3054, negative, the extremes),
   the carrier rule (hasCats only with the formal leader listed), every new refusal of the encoder and the decoder (nothing written). */
void t_t1b3r_squadlead_cats_wire()
{
    const int cats[5] = { 0, 3054, -1500, 2147483647, (-2147483647 - 1) };
    for (int i = 0; i < 5; ++i)
    {
        coopsquad::SquadLeadMsg m; m.squadKey = 3; m.acting = 83886400u; m.formal = 83886400u; m.seq = 11u + (unsigned int)i;
        m.members.push_back(83886400u); m.members.push_back(83886401u); m.cats = cats[i]; m.hasCats = 1;
        std::vector<char> b(1, 'x');
        if (!coopsquad::EncodeSquadLead(&b, m) || b.size() != 1 + coopsquad::kSquadLeadHead + 8) { Fail("a SQUAD_LEAD with cats is not head + 4 n (appended)"); continue; }
        coopsquad::SquadLeadMsg d;
        if (coopsquad::DecodeSquadLead(&b[1], b.size() - 1, &d) != coopsquad::kSquadLeadDecodeOk) { Fail("a SQUAD_LEAD with cats did not decode"); continue; }
        if (d.cats != m.cats || d.hasCats != 1 || d.formal != m.formal || d.members != m.members || d.seq != m.seq) Fail("the cats round trip changed a field");
    }
    if (!coopsquad::LeadCarriesCats(5, std::vector<unsigned int>(1, 5u)) || coopsquad::LeadCarriesCats(0, std::vector<unsigned int>(1, 5u))
        || coopsquad::LeadCarriesCats(6, std::vector<unsigned int>(1, 5u))) Fail("the carrier rule: the formal leader listed, never 0");
    coopsquad::SquadLeadMsg m; m.squadKey = 5; m.acting = 3; m.formal = 3; m.seq = 1; m.members.push_back(3); m.members.push_back(9); m.cats = 1500; m.hasCats = 1;
    std::vector<char> e(1, 'y');
    coopsquad::SquadLeadMsg bad(m); bad.formal = 4;
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("cats from a game that does not list the formal leader were encoded");
    bad = m; bad.hasCats = 2;
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("hasCats 2 was encoded");
    bad = m; bad.hasCats = 0;
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("cats without hasCats were encoded");
    bad.cats = 0;
    if (!coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1 + coopsquad::kSquadLeadHead + 8) Fail("no money (hasCats 0, cats 0) was refused");
    std::vector<char> b;
    if (!coopsquad::EncodeSquadLead(&b, m) || b.size() != 31) { Fail("the two-member SQUAD_LEAD with cats is not 31 bytes"); return; }
    coopsquad::SquadLeadMsg d; d.squadKey = 77; d.cats = 78;
    std::vector<char> f2(b); f2[20] = 2;
    if (coopsquad::DecodeSquadLead(&f2[0], f2.size(), &d) != coopsquad::kSquadLeadDecodeBadFlag) Fail("hasCats 2: bad flag");
    std::vector<char> nf(b); const unsigned int four = 4; std::memcpy(&nf[8], &four, 4);
    if (coopsquad::DecodeSquadLead(&nf[0], nf.size(), &d) != coopsquad::kSquadLeadDecodeBadCats) Fail("cats with the formal leader not listed: bad cats");
    std::vector<char> z(b); z[20] = 0;
    if (coopsquad::DecodeSquadLead(&z[0], z.size(), &d) != coopsquad::kSquadLeadDecodeBadCats) Fail("cats with hasCats 0: bad cats");
    if (coopsquad::DecodeSquadLead(&b[0], coopsquad::kSquadLeadHead - 1, &d) != coopsquad::kSquadLeadDecodeTooShort) Fail("22 bytes: too short");
    if (d.squadKey != 77 || d.cats != 78) Fail("a refused SQUAD_LEAD wrote its output");
}
/* T-1 B3 restructure: whose announcement is the squad's money (the game that runs its formal leader, listing it), T540's failure
   (A ran leader 83886400 at 0 cats, B's copy showed 1500 after the one write) corrected by the per-pass compare, and F2 (a former
   leader carries no money once its squad names another). */
void t_t1b3r_squadcats_book_and_copy_write()
{
    using namespace coopsquad;
    PeerLeadBook bk;
    SquadLeadMsg run; run.squadKey = 1; run.acting = 83886400u; run.formal = 83886400u; run.seq = 5; run.members.push_back(83886400u); run.members.push_back(83886401u); run.cats = 0; run.hasCats = 1;
    SquadLeadMsg split; split.squadKey = 2; split.acting = 83886400u; split.formal = 83886400u; split.seq = 6; split.members.push_back(83886402u);
    if (bk.Apply(run) != kLeadApplied || bk.Apply(split) != kLeadApplied) Fail("both announcements are stored");
    const PeerLeadEntry* e = bk.ForLeader(83886400u);
    if (e == 0 || e->cats != 0 || e->seq != 5) Fail("the game that runs the formal leader gives the squad's money");
    if (bk.ForLeader(83886401u) != 0 || bk.ForLeader(83886402u) != 0) Fail("a member is not a leader; a member-only announcement carries no money");
    std::vector<unsigned int> ls; bk.CatsLeaders(&ls);
    if (ls.size() != 1 || ls[0] != 83886400u) Fail("one squad carries money");
    if (CopyPotAction(e->cats, 1500, 0) != kPotWrite) Fail("T540: the copy's 1500 is corrected to the runner's 0");
    if (CopyPotAction(e->cats, 0, 0) != kPotEqual) Fail("equal: nothing written");
    if (CopyPotAction(e->cats, 1500, 0) != kPotWrite) Fail("T540: the copy engine's re-seed after the write is corrected on the next pass (nothing settled)");
    SquadLeadMsg nw(run); nw.seq = 7; nw.cats = 74;
    bk.Apply(nw);
    if (bk.ForLeader(83886400u) == 0 || bk.ForLeader(83886400u)->cats != 74) Fail("a newer announcement's cats replace the older");
    SquadLeadMsg lead2(run); lead2.seq = 8; lead2.formal = 83886401u; lead2.acting = 83886401u; lead2.cats = 74;
    bk.Apply(lead2);
    if (bk.ForLeader(83886400u) != 0 || bk.ForLeader(83886401u) == 0) Fail("F2: a former leader carries no money once its squad names another");
    SquadLeadMsg drop; drop.squadKey = 1; drop.seq = 9;
    bk.Apply(drop);
    if (bk.ForLeader(83886401u) != 0) Fail("n = 0: the squad's money is forgotten");
}
/* T-1 B3 restructure (F1): the hand-over order. The GIVING game sends no XFER while its money announcement is due and unsent, so the
   announcement carrying its latest cats precedes the XFER on the same ordered reliable channel and the TAKING game's book already
   holds it when the members arrive; the taker writes it as it is before announcing the squad, adds the giver's changes inside the
   round trip as deltas (never an overwrite), and the giver's n = 0 ends it. */
void t_t1b3r_handover_adoption_order()
{
    using namespace coopsquad;
    if (HandoverXferMayGo(1, 0) != 0) Fail("the giver's cats not announced yet: no XFER");
    if (HandoverXferMayGo(1, 1) != 1 || HandoverXferMayGo(0, 0) != 1) Fail("announced (or no money carried): the XFER may go");
    PeerLeadBook bk;
    SquadLeadMsg g; g.squadKey = 4; g.acting = 70u; g.formal = 70u; g.members.push_back(70u); g.members.push_back(71u); g.hasCats = 1;
    g.seq = 5; g.cats = 3000; bk.Apply(g);
    g.seq = 6; g.cats = 3054; bk.Apply(g);   /* the giver's last-second change - announced in the XFER's own pass, before it */
    const PeerLeadEntry* e = bk.ForLeader(70u);   /* the XFER arrives: the taker runs 70 now */
    long long d = 0;
    if (e == 0 || HandoverCatsAction(e->seq, e->cats, 0, 0u, 0, &d) != kCatsAbsolute || e->cats != 3054) { Fail("F1: the taker writes the giver's LAST value first"); return; }
    unsigned int recSeq = e->seq; int recCats = e->cats;
    if (HandoverCatsAction(e->seq, e->cats, 1, recSeq, recCats, &d) != kCatsNone) Fail("taken: the same announcement is not written twice");
    g.seq = 7; g.cats = 3154; bk.Apply(g);   /* a sale on the giver inside the round trip - both games run 70 until the ACK */
    e = bk.ForLeader(70u);
    if (e == 0 || HandoverCatsAction(e->seq, e->cats, 1, recSeq, recCats, &d) != kCatsDelta || d != 100) Fail("F1: the giver's change inside the round trip is added as a delta");
    recSeq = 7; recCats = 3154;
    g.seq = 8; bk.Apply(g);                  /* re-announced for its members, cats unchanged */
    e = bk.ForLeader(70u);
    if (e == 0 || HandoverCatsAction(e->seq, e->cats, 1, recSeq, recCats, &d) != kCatsDelta || d != 0) Fail("an unchanged re-announcement is a zero delta");
    SquadLeadMsg drop; drop.squadKey = 4; drop.seq = 9; bk.Apply(drop);   /* the giver's ACK came: it runs none of them */
    if (bk.ForLeader(70u) != 0) Fail("the giver's n = 0 ends the hand-over");
    if (HandoverCatsAction(2u, 2147483647, 1, 1u, (-2147483647 - 1), &d) != kCatsDelta || d != 4294967295LL) Fail("the delta is computed wide");
}
/* T-1 B3 restructure (F5): while this game's own priced requests against a copy's pot are in flight the announced value is not
   written (the local engine's predicted move stays on screen); the first pass after they settle writes it (no flag to lose). */
void t_t1b3r_inflight_hold()
{
    using namespace coopsquad;
    if (CopyPotAction(3000, 3054, 1) != kPotHold) Fail("F5: in flight - held, the local estimate stays");
    if (CopyPotAction(3054, 3054, 1) != kPotEqual) Fail("in flight but already equal: nothing to hold");
    if (CopyPotAction(3000, 3054, 0) != kPotWrite) Fail("settled (refused): the runner's value is written - the prediction undone");
    if (CopyPotAction(3054, 3054, 0) != kPotEqual) Fail("settled (accepted: the runner's value came before the CONFIRM): nothing to write");
}
/* ---- T-1 B3 fold (money review of 97c1d61, 2026-09-29) ---- */
/* M1: a squad split between the games with a different formal leader on each: each game announces its own leader's pot and must
   never write the other game's value into it (both did: two changes inside a second flapped). */
void t_t1b3f_m1_no_copy_write_into_own_pot()
{
    using namespace coopsquad;
    if (CopyPotOwnSkip(0, 0) != 0) Fail("a pure copy squad's pot is written");
    if (CopyPotOwnSkip(1, 0) != 1) Fail("M1: a pot this game announces cats for is never written from the other game's value");
    if (CopyPotOwnSkip(0, 1) != 1) Fail("M1: a squad with a member run here is never written from the other game's value");
    if (CopyPotOwnSkip(1, 1) != 1) Fail("both: skipped");
    int potA = 500, potB = 800;   /* A runs formal leader 70 (a sale took it to 500), B runs formal leader 71 of the same squad (800) */
    if (CopyPotOwnSkip(1, 1) == 0) potA = 800;
    if (CopyPotOwnSkip(1, 1) == 0) potB = 500;
    if (potA != 500 || potB != 800) Fail("M1: neither game's own pot is overwritten by the other's");
}
/* M2 (requester): the undo decided at the SEND, never at the settle. */
void t_t1b3f_m2_undo_decided_at_send()
{
    using namespace coopsquad;
    if (KeeperUndoDecision(0, 1, 1) != kUndoNone) Fail("M2: a copy keeper at the send - no undo even when this game runs it at the settle (paid twice before)");
    if (KeeperUndoDecision(1, 1, 1) != kUndoApply) Fail("ran at the send, still runs it, same pot: undone");
    if (KeeperUndoDecision(1, 0, 1) != kUndoNotRunner) Fail("M2: handed over since - nothing owed here");
    if (KeeperUndoDecision(1, 1, 0) != kUndoPotChanged) Fail("the pot is another object now: not written");
}
/* M2: the pot is announced net of this game's own in-flight predictions, so a trade in flight across a hand-over pays the keeper once
   (accepted: the holder, the runner by then, pays its real pot) or never (refused: nobody received the prediction). */
void t_t1b3f_m2_announced_net_of_inflight()
{
    using namespace coopsquad;
    int out = -1;
    if (SettledPotValue(1200, 200, &out) != 1 || out != 1000) Fail("M2: an in-flight purchase of 200 is not announced (1000, not 1200)");
    if (SettledPotValue(800, -200, &out) != 1 || out != 1000) Fail("M2: an in-flight sale of 200 is not announced");
    if (SettledPotValue(1200, 0, &out) != 1 || out != 1200) Fail("settled (accepted): the whole pot goes out");
    if (SettledPotValue(100, 200, &out) != 2 || out != 0) Fail("spent below the prediction: 0, never negative");
    if (SettledPotValue(2147483647, -1, &out) != 0) Fail("out of range: not announced");
    /* A runs the keeper at 1000; A's player buys for 200 at B's counter (A's pot 1200, announced 1000); A hands the squad to B, which
       adopts 1000. Refused: A no longer runs it (no undo), B pays nothing -> 1000. Accepted: B's holder pays B's real pot -> 1200. */
    SettledPotValue(1200, 200, &out);
    int refused = out, accepted = out;
    if (KeeperUndoDecision(1, 0, 1) == kUndoApply) refused -= 200;
    accepted += 200;
    if (refused != 1000) Fail("M2: refused across the hand-over - the keeper is paid 0 times");
    if (accepted != 1200) Fail("M2: accepted across the hand-over - the keeper is paid once");
}
/* M2 (taker): the take-over write waits for this game's own in-flight trades on that pot; the snapshot keeps the giver's value. */
void t_t1b3f_m2_takeover_held()
{
    using namespace coopsquad;
    if (TakeoverAdoptHeld(kCatsAbsolute, 1) != 1) Fail("M2: the take-over write waits while this game's priced requests on that pot are in flight");
    if (TakeoverAdoptHeld(kCatsAbsolute, 0) != 0) Fail("settled: adopted");
    if (TakeoverAdoptHeld(kCatsDelta, 1) != 0) Fail("a round-trip delta keeps the prediction: not held");
    if (TakeoverAdoptHeld(kCatsNone, 1) != 0) Fail("nothing to adopt: nothing held");
    PeerLeadBook bk;
    SquadLeadMsg g; g.squadKey = 4; g.acting = 70u; g.formal = 70u; g.members.push_back(70u); g.hasCats = 1; g.seq = 5; g.cats = 3000; bk.Apply(g);
    const PeerLeadEntry* e = bk.ForLeader(70u);
    if (e == 0) { Fail("the giver's announcement is stored"); return; }
    const unsigned int snapSeq = e->seq; const int snapCats = e->cats;   /* snapshotted at the XFER */
    SquadLeadMsg drop; drop.squadKey = 4; drop.seq = 6; bk.Apply(drop);   /* the giver's n = 0 during the hold */
    long long d = 0;
    if (bk.ForLeader(70u) != 0) Fail("n = 0 removes the book's entry");
    if (HandoverCatsAction(snapSeq, snapCats, 0, 0u, 0, &d) != kCatsAbsolute || snapCats != 3000) Fail("M2: after the hold the snapshot's value is written as it is");
}
/* M3: the giver's change inside the round trip goes as a final announcement before the release; the taker adds it as a delta. */
void t_t1b3f_m3_final_announce_before_release()
{
    using namespace coopsquad;
    if (FinalAnnounceDue(1, 3000, 1, 3054) != 1) Fail("M3: a pot moved since the last announcement is announced before the release");
    if (FinalAnnounceDue(1, 3054, 1, 3054) != 0) Fail("unchanged: nothing sent");
    if (FinalAnnounceDue(0, 0, 1, 3054) != 0) Fail("a squad not announced with its money: nothing");
    if (FinalAnnounceDue(1, 3000, 0, 0) != 0) Fail("an unreadable pot: nothing");
    long long d = 0;
    if (HandoverCatsAction(8u, 3054, 1, 7u, 3000, &d) != kCatsDelta || d != 54) Fail("M3: the taker adds it as a round-trip delta");
}
/* L1: the undo is exactly the price, raw - never refused by the afford check, never below 0 (owner 106's shortfall). */
void t_t1b3f_l1_undo_exact_price()
{
    using namespace coopsquad;
    int np = -1, sh = -1;
    if (KeeperUndoTarget(1200, 1, 200, &np, &sh) != 1 || np != 1000 || sh != 0) Fail("L1: a purchase's undo takes exactly the price back");
    if (KeeperUndoTarget(150, 1, 200, &np, &sh) != 1 || np != 0 || sh != 50) Fail("L1: below the price - to 0, 50 short, not refused");
    if (KeeperUndoTarget(0, 1, 200, &np, &sh) != 1 || np != 0 || sh != 200) Fail("an empty pot: all of it short");
    if (KeeperUndoTarget(-5, 1, 200, &np, &sh) != 1 || np != -5 || sh != 200) Fail("a pot below 0 is never raised by an undo");
    if (KeeperUndoTarget(800, 2, 200, &np, &sh) != 1 || np != 1000 || sh != 0) Fail("a sale's undo gives exactly the price back");
    if (KeeperUndoTarget(2147483600, 2, 200, &np, &sh) != 0 || np != 2147483600) Fail("out of range: nothing written");
    if (KeeperUndoTarget(100, 1, 0, &np, &sh) != 0 || np != 100) Fail("no price: nothing");
}
/* L3: after a reconnect the resends wait for the squad's re-announcement. */
void t_t1b3f_l3_resends_gated()
{
    using namespace coopsquad;
    if (ResendMayGo(0, 1, 1) != 0) Fail("L3: a pass that did not run its squads resends nothing");
    if (ResendMayGo(1, 1, HandoverXferMayGo(1, 0)) != 0) Fail("L3: its cats announcement due and unsent - the resend waits");
    if (ResendMayGo(1, 1, HandoverXferMayGo(1, 1)) != 1) Fail("announced: the resend goes");
    if (ResendMayGo(1, 0, 0) != 1) Fail("a squad not seen this pass: resent as before");
}
/* L4: releasing the formal leader clears the carries-cats record, so a quick hand-back is adopted. */
void t_t1b3f_l4_stale_cats_record_cleared()
{
    using namespace coopsquad;
    if (LeadHasAfterRelease(70u, 70u, 1) != 0) Fail("L4: releasing the formal leader clears the record's carries-cats flag");
    if (LeadHasAfterRelease(70u, 71u, 1) != 1) Fail("releasing another member keeps it");
    if (LeadHasAfterRelease(70u, 70u, 0) != 0) Fail("nothing to clear");
}
/* T-1 B1 restructure: MSG_SQUAD_LEAD round trip (n = 0, 3, 64), every refusal of the decoder (nothing written), the encoder's refusals. */
void t_t1b1_squadlead_wire()
{
    const unsigned int ns[3] = { 0u, 3u, 64u };
    for (int t = 0; t < 3; ++t)
    {
        coopsquad::SquadLeadMsg m; m.squadKey = 7; m.acting = 0xFFFFFFF0u; m.formal = 12; m.seq = 99;
        for (unsigned int i = 0; i < ns[t]; ++i) m.members.push_back(1000u + i);
        std::vector<char> b; b.push_back('x');
        if (!coopsquad::EncodeSquadLead(&b, m)) { Fail("a valid SQUAD_LEAD was not encoded"); continue; }
        if (b.size() != 1 + coopsquad::kSquadLeadHead + 4 * (size_t)ns[t]) Fail("the SQUAD_LEAD size is not head + 4 n (appended)");
        coopsquad::SquadLeadMsg d;
        if (coopsquad::DecodeSquadLead(&b[1], b.size() - 1, &d) != coopsquad::kSquadLeadDecodeOk) { Fail("a valid SQUAD_LEAD did not decode"); continue; }
        if (d.squadKey != m.squadKey || d.acting != m.acting || d.formal != m.formal || d.seq != m.seq || d.members != m.members) Fail("the SQUAD_LEAD round trip changed a field");
    }
    coopsquad::SquadLeadMsg m; m.squadKey = 5; m.acting = 3; m.formal = 4; m.seq = 1; m.members.push_back(3); m.members.push_back(9);
    std::vector<char> b;
    if (!coopsquad::EncodeSquadLead(&b, m) || b.size() != 31) { Fail("the two-member SQUAD_LEAD is not 31 bytes (protocol 89: + cats)"); return; }
    coopsquad::SquadLeadMsg d; d.squadKey = 77;
    if (coopsquad::DecodeSquadLead(&b[0], coopsquad::kSquadLeadHead - 1, &d) != coopsquad::kSquadLeadDecodeTooShort) Fail("17 bytes: too short");
    if (coopsquad::DecodeSquadLead(0, b.size(), &d) != coopsquad::kSquadLeadDecodeTooShort) Fail("no bytes: too short");
    if (coopsquad::DecodeSquadLead(&b[0], b.size() - 1, &d) != coopsquad::kSquadLeadDecodeBadSize) Fail("a member cut short: bad size");
    std::vector<char> longer(b); longer.push_back(0);
    if (coopsquad::DecodeSquadLead(&longer[0], longer.size(), &d) != coopsquad::kSquadLeadDecodeBadSize) Fail("a trailing byte: bad size");
    std::vector<char> big(b); const unsigned short n65 = 65; std::memcpy(&big[21], &n65, 2);   /* protocol 89: n after cats */
    big.resize(coopsquad::kSquadLeadHead + 4 * 65, 1);
    if (coopsquad::DecodeSquadLead(&big[0], big.size(), &d) != coopsquad::kSquadLeadDecodeBadCount) Fail("65 members: bad count");
    std::vector<char> k0(b); std::memset(&k0[0], 0, 4);
    if (coopsquad::DecodeSquadLead(&k0[0], k0.size(), &d) != coopsquad::kSquadLeadDecodeBadKey) Fail("squad key 0: bad key");
    std::vector<char> s0(b); std::memset(&s0[12], 0, 4);
    if (coopsquad::DecodeSquadLead(&s0[0], s0.size(), &d) != coopsquad::kSquadLeadDecodeBadKey) Fail("seq 0: bad key");
    std::vector<char> u0(b); std::memset(&u0[coopsquad::kSquadLeadHead + 4], 0, 4);
    if (coopsquad::DecodeSquadLead(&u0[0], u0.size(), &d) != coopsquad::kSquadLeadDecodeBadMember) Fail("a member uid 0: bad member");
    if (d.squadKey != 77 || !d.members.empty()) Fail("a refused SQUAD_LEAD wrote its output");
    std::vector<char> e(1, 'y');
    coopsquad::SquadLeadMsg bad(m); bad.squadKey = 0;
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("squad key 0 was encoded");
    bad = m; bad.seq = 0;
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("seq 0 was encoded");
    bad = m; bad.members.push_back(0);
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("a member uid 0 was encoded");
    bad = m; bad.members.clear(); for (unsigned int i = 0; i < 65; ++i) bad.members.push_back(10u + i);
    if (coopsquad::EncodeSquadLead(&e, bad) || e.size() != 1) Fail("65 members were encoded");
}

/* T-1 B1 restructure: the receiver's book - newest seq per key, members that left are dropped, n = 0 forgets the key, a uid that moved
   to another key stays with the newer key, Clear forgets everything. */
void t_t1b1_squadlead_book()
{
    coopsquad::PeerLeadBook bk;
    coopsquad::SquadLeadMsg a; a.squadKey = 1; a.acting = 10; a.formal = 11; a.seq = 5; a.members.push_back(10); a.members.push_back(12);
    if (bk.Apply(a) != coopsquad::kLeadApplied) Fail("a first announcement is applied");
    const coopsquad::PeerLeadEntry* e = bk.ForUid(12);
    if (e == 0 || e->acting != 10 || e->formal != 11 || e->seq != 5) Fail("a listed uid finds its announcement");
    if (bk.ForUid(11) != 0) Fail("an unlisted uid finds nothing");
    coopsquad::SquadLeadMsg same(a); same.acting = 99;
    if (bk.Apply(same) != coopsquad::kLeadStale || bk.ForUid(10) == 0 || bk.ForUid(10)->acting != 10) Fail("an equal seq is stale and not stored");
    same.seq = 4;
    if (bk.Apply(same) != coopsquad::kLeadStale) Fail("an older seq is stale");
    coopsquad::SquadLeadMsg b(a); b.seq = 6; b.acting = 12; b.members.clear(); b.members.push_back(12);
    if (bk.Apply(b) != coopsquad::kLeadApplied) Fail("a newer announcement is applied");
    if (bk.ForUid(10) != 0) Fail("a member the newer announcement no longer lists is dropped");
    if (bk.ForUid(12) == 0 || bk.ForUid(12)->acting != 12) Fail("the newer acting leader is stored AS IS");
    coopsquad::SquadLeadMsg c; c.squadKey = 2; c.seq = 7; c.acting = 13; c.formal = 13; c.members.push_back(12); c.members.push_back(13);
    if (bk.Apply(c) != coopsquad::kLeadApplied || bk.ForUid(12) == 0 || bk.ForUid(12)->seq != 7) Fail("a uid that moved to another squad follows it");
    coopsquad::SquadLeadMsg d1; d1.squadKey = 1; d1.seq = 8;
    if (bk.Apply(d1) != coopsquad::kLeadDropped) Fail("n = 0 drops the key");
    if (bk.ForUid(12) == 0 || bk.ForUid(12)->seq != 7 || bk.Squads() != 1) Fail("dropping the old squad keeps a uid the newer squad lists");
    coopsquad::SquadLeadMsg d2; d2.squadKey = 2; d2.seq = 9;
    if (bk.Apply(d2) != coopsquad::kLeadDropped || bk.ForUid(12) != 0 || bk.ForUid(13) != 0 || bk.Squads() != 0 || bk.Uids() != 0) Fail("dropping the last squad forgets every uid");
    a.seq = 10;
    if (bk.Apply(a) != coopsquad::kLeadApplied) Fail("a key announced again after its drop is applied");
    bk.Clear();
    if (bk.Squads() != 0 || bk.ForUid(10) != 0) Fail("Clear forgets everything (a new link hears every announcement again)");
}

/* T-1 B1 restructure (points 1-3): both games feed AgreedSquadLeader the SAME announced pair, each from its own side. A runs 1..3,
   B runs 4..6, 7 is run by neither. Over every pair of announced acting leaders both games agree on one uid; at most one game sends
   (never a swap), exactly one when a game runs the agreed leader, and neither when neither does (both use the position rule). */
void t_t1b1_announced_pair()
{
    const unsigned int v[] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u };
    const int nv = (int)(sizeof(v) / sizeof(v[0]));
    for (int i = 0; i < nv; ++i) for (int j = 0; j < nv; ++j)
    {
        const unsigned int a = v[i], b = v[j];   /* A announced a, B announced b */
        const unsigned int onA = coopsquad::AgreedSquadLeader(a, b), onB = coopsquad::AgreedSquadLeader(b, a);
        if (onA != onB) { Fail("the two games agree on different leaders from the same announced pair"); continue; }
        const int runA = (onA >= 1 && onA <= 3) ? 1 : 0, runB = (onA >= 4 && onA <= 6) ? 1 : 0;
        const int actA = coopsquad::FollowAction((runA || runB) ? 1 : 0, runA, 0), actB = coopsquad::FollowAction((runA || runB) ? 1 : 0, runB, 0);
        const int sends = (actA == coopsquad::kFollowSend ? 1 : 0) + (actB == coopsquad::kFollowSend ? 1 : 0);
        if (sends > 1) Fail("both games send their members (a swap)");
        if ((runA || runB) && sends != 1) Fail("the game not running the agreed leader does not send");
        if (!runA && !runB && (actA != coopsquad::kFollowByPosition || actB != coopsquad::kFollowByPosition)) Fail("a leader neither game runs: both use the position rule");
    }
}

/* T-1 B1 restructure (point 4): crossing follow transfers - each game takes the incoming one only when the leader it runs is the lower
   uid; with the keys swapped on the other game, exactly one transfer moves when the keys differ (the one to the lower leader) and
   none when they are equal. Never both (no swap). */
void t_t1b1_crossing()
{
    const unsigned int v[] = { 1u, 2u, 3u, 50u, 51u, 0xFFFFFFFEu, 0xFFFFFFFFu };
    const int nv = (int)(sizeof(v) / sizeof(v[0]));
    for (int i = 0; i < nv; ++i) for (int j = 0; j < nv; ++j)
    {
        const unsigned int la = v[i], lb = v[j];   /* A's XFER keyed la (run by B), B's XFER keyed lb (run by A) */
        const int takeAtB = coopsquad::CrossingTakeIncoming(la, lb), takeAtA = coopsquad::CrossingTakeIncoming(lb, la);
        if (takeAtA + takeAtB > 1) Fail("both crossing transfers were taken (a swap)");
        if (la != lb && takeAtA + takeAtB != 1) Fail("differing keys: exactly one crossing transfer proceeds");
        if (la == lb && takeAtA + takeAtB != 0) Fail("equal keys: neither crossing transfer is taken");
        if (la != lb && (takeAtB ? la : lb) != (la < lb ? la : lb)) Fail("the transfer to the game running the lower leader is the one that proceeds");
    }
    if (coopsquad::CrossingTakeIncoming(0, 5) != 0) Fail("an incoming key 0 is refused");
}

/* T-1 B1 fold (review M1): the leader the engine acts on - +0xA0 when standing, else the acting +0xA8, else none. */
void t_t1b1_engine_leader()
{
    if (coopsquad::EngineLeaderChoice(1, 0, 0) != coopsquad::kEngineLeadA0) Fail("a standing +0xA0 leads");
    if (coopsquad::EngineLeaderChoice(1, 0, 1) != coopsquad::kEngineLeadA0) Fail("a standing +0xA0 leads even with an acting leader set");
    if (coopsquad::EngineLeaderChoice(1, 1, 1) != coopsquad::kEngineLeadA8) Fail("a dead/downed +0xA0: the acting leader +0xA8");
    if (coopsquad::EngineLeaderChoice(1, 1, 0) != coopsquad::kEngineLeadNone) Fail("a dead/downed +0xA0 and no acting leader: the position rule");
    if (coopsquad::EngineLeaderChoice(0, 0, 1) != coopsquad::kEngineLeadNone || coopsquad::EngineLeaderChoice(0, 1, 0) != coopsquad::kEngineLeadNone) Fail("no +0xA0: the position rule");
}

/* owner 97a - coopsig::SigScan counts EVERY match (so the plugin can demand exactly one) and never reads past the range. */
void t_sig97a_scan()
{
    const unsigned char pat[3] = { 0xAA, 0x00, 0xCC };
    {   /* once, with the wildcard in the middle standing on a byte that differs from pat[1] */
        const unsigned char buf[8] = { 1, 2, 0xAA, 0x77, 0xCC, 3, 4, 5 };
        const coopsig::SigResult r = coopsig::SigScan(buf, 8, pat, "x?x", 3);
        if (r.count != 1 || r.first != 2) Fail("a single wildcard match was not found exactly once at offset 2");
        const coopsig::SigResult s = coopsig::SigScan(buf, 8, pat, "xxx", 3);
        if (s.count != 0 || s.first != -1) Fail("with no wildcard the middle byte must have to match");
    }
    {   /* zero */
        const unsigned char buf[6] = { 0xAA, 0x01, 0xCB, 0xAA, 0xCC, 0x00 };
        const coopsig::SigResult r = coopsig::SigScan(buf, 6, pat, "x?x", 3);
        if (r.count != 0 || r.first != -1) Fail("a range with no match reported one");
    }
    {   /* twice - the count must say 2 and first must be the earlier one */
        const unsigned char buf[9] = { 0xAA, 0x10, 0xCC, 0, 0, 0xAA, 0x20, 0xCC, 0 };
        const coopsig::SigResult r = coopsig::SigScan(buf, 9, pat, "x?x", 3);
        if (r.count != 2 || r.first != 0) Fail("two matches were not both counted, or first was not the earlier");
    }
    {   /* overlapping matches both count */
        const unsigned char aa[2] = { 0xAA, 0xAA };
        const unsigned char buf[4] = { 0xAA, 0xAA, 0xAA, 0x00 };
        const coopsig::SigResult r = coopsig::SigScan(buf, 4, aa, "xx", 2);
        if (r.count != 2 || r.first != 0) Fail("overlapping matches were not both counted");
    }
    {   /* leading wildcards: the match offset is the pattern start, not the first fixed byte */
        const unsigned char lp[3] = { 0x00, 0x00, 0x5A };
        const unsigned char buf[5] = { 0x5A, 0x11, 0x22, 0x5A, 0x33 };
        const coopsig::SigResult r = coopsig::SigScan(buf, 5, lp, "??x", 3);
        if (r.count != 1 || r.first != 1) Fail("a pattern with leading wildcards reported the wrong offset (a 0x5A before offset 2 cannot match)");
    }
    {   /* at the range end: a match ending on the last byte is found; one cut off by the end is not, and nothing past len is read */
        const unsigned char buf[7] = { 0, 0, 0, 0xAA, 0x55, 0xCC, 0xAA };
        const coopsig::SigResult r = coopsig::SigScan(buf, 6, pat, "x?x", 3);
        if (r.count != 1 || r.first != 3) Fail("a match ending on the last byte of the range was not found");
        const unsigned char cut[6] = { 0, 0, 0, 0, 0xAA, 0x55 };
        const coopsig::SigResult c = coopsig::SigScan(cut, 6, pat, "x??", 3);
        if (c.count != 0) Fail("a match running past the end of the range was reported");
        const coopsig::SigResult e = coopsig::SigScan(buf + 3, 3, pat, "x?x", 3);
        if (e.count != 1 || e.first != 0) Fail("a range exactly the pattern's length did not match");
    }
    {   /* bad input answers nothing */
        const unsigned char buf[4] = { 0xAA, 0, 0xCC, 0 };
        if (coopsig::SigScan(buf, 2, pat, "x?x", 3).count != 0) Fail("a range shorter than the pattern matched");
        if (coopsig::SigScan(buf, 4, pat, "x?", 3).count != 0) Fail("a mask shorter than the pattern was accepted");
        if (coopsig::SigScan(buf, 4, pat, "x.x", 3).count != 0) Fail("a mask character other than x or ? was accepted");
        if (coopsig::SigScan(0, 4, pat, "x?x", 3).count != 0) Fail("a null range was scanned");
        if (coopsig::SigScan(buf, 4, pat, "", 0).count != 0) Fail("an empty pattern matched");
    }
    {   /* the title-screen pattern itself: one mask character per byte, the 15-byte prologue wildcarded, and it finds
           itself (wildcards filled with other bytes) exactly once when surrounded by noise */
        const size_t n = coopsig::kTitleUpdateSigLen;
        if (std::strlen(coopsig::kTitleUpdateMask) != n || n != 98) Fail("the title pattern's mask is not 98 characters");
        size_t i, fixed = 0;
        for (i = 0; i < n && i < std::strlen(coopsig::kTitleUpdateMask); ++i)
        {
            if (i < 15 && coopsig::kTitleUpdateMask[i] != '?') Fail("the title pattern's prologue is not wildcarded");
            if (coopsig::kTitleUpdateMask[i] == 'x') ++fixed;
        }
        if (fixed < 40) Fail("the title pattern has fewer than 40 fixed bytes");
        std::vector<unsigned char> img(4096, 0x90);
        for (i = 0; i < n; ++i) img[1000 + i] = coopsig::kTitleUpdateMask[i] == 'x' ? coopsig::kTitleUpdateSig[i] : (unsigned char)(0xE0 + (i & 7));
        const coopsig::SigResult r = coopsig::SigScan(&img[0], img.size(), coopsig::kTitleUpdateSig, coopsig::kTitleUpdateMask, n);
        if (r.count != 1 || r.first != 1000) Fail("the title pattern did not find its own bytes exactly once");
    }
}

/* 2026-09-26 bug class (seen three times in one day): an address-table row added without '!count' matching the real row
   count makes addresses.cpp refuse the whole table at load ("MOD DISABLED"). Two branches that each add rows both declare
   the same new count and MERGE WITHOUT A CONFLICT, so only a check of the merged tree catches it - this one, which runs
   as step 0 of every plugin build. Same parse as the loader: blank, '#' and '!' lines are not rows. Runs from src\coop-test. */
void t_address_tables_count_matches()
{
    struct _finddata_t fd;
    intptr_t h = _findfirst("..\\coop-plugin\\addresses\\*.txt", &fd);
    if (h == -1) { Fail("no address table found under ..\\coop-plugin\\addresses (the suite runs from src\\coop-test)"); return; }
    do
    {
        const std::string path = std::string("..\\coop-plugin\\addresses\\") + fd.name;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == 0) { Fail("cannot open the address table " + path); continue; }
        char line[2048]; long entries = 0, declared = -1;
        while (std::fgets(line, sizeof line, f) != 0)
        {
            const char* q = line;
            while (*q == ' ' || *q == '\t') ++q;
            if (*q == 0 || *q == '\r' || *q == '\n' || *q == '#') continue;
            if (*q == '!') { if (std::strncmp(q, "!count", 6) == 0) declared = std::strtol(q + 6, 0, 10); continue; }
            ++entries;
        }
        std::fclose(f);
        if (declared != entries)
            Fail(path + " declares !count " + D((double)declared) + " but has " + D((double)entries) + " rows - the mod would disable itself at load");
        if (entries > coopaddr::kAddrMaxEntries)
            Fail(path + " has " + D((double)entries) + " rows, over the loader's limit kAddrMaxEntries " + D((double)coopaddr::kAddrMaxEntries) + " - the mod would disable itself at load");
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
}

void t_recruit3_mult()
{
    if (coopr::RecruitMultCode("auto") != 0 || coopr::RecruitMultCode("1") != 1 || coopr::RecruitMultCode("4") != 4) Fail("a legal recruitmult value was misread");
    if (coopr::RecruitMultValueOk("0") || coopr::RecruitMultValueOk("5") || coopr::RecruitMultValueOk("") || coopr::RecruitMultValueOk("2x") || coopr::RecruitMultValueOk("AUTO")) Fail("an illegal recruitmult value was accepted");
    if (coopr::RecruitMultEffective(0, 0) != 1 || coopr::RecruitMultEffective(0, 2) != 2 || coopr::RecruitMultEffective(0, 7) != 4 || coopr::RecruitMultEffective(3, 1) != 3) Fail("the effective multiplier is wrong");
    if (!coopr::IsHireListName("Recruits list Shek Kingdom") || !coopr::IsHireListName("shady bar recruits list travellers") || !coopr::IsHireListName("mongrel permanent recruits")) Fail("a hire list was not recognised");
    if (coopr::IsHireListName("Drifter squad bar") || coopr::IsHireListName("Shek Warrior (bar squad)") || coopr::IsHireListName("Hired Thugs") || coopr::IsHireListName("") || coopr::IsHireListName(0) || coopr::IsHireListName("recrui")) Fail("a non-hire bar squad was called a hire list");
    if (coopr::RecruitSlotCount(0) != 0 || coopr::RecruitSlotCount(0x5) != 2 || coopr::RecruitSlotCount(0xFFFF) != 16) Fail("the slot count is wrong");
}

void t_settings2_gp_values()
{
    double d = 0.0;
    if (!coopgp::GpValueOk("gp.gdm", "1.5", &d) || d != 1.5) Fail("gp.gdm 1.5 was refused");
    if (coopgp::GpValueOk("gp.cod", "0", &d) || coopgp::GpValueOk("gp.rs", "0", &d) || coopgp::GpValueOk("gt.pop", "0.009", &d) || coopgp::GtValueOk("gt.pop", "0")) Fail("a float of 0 was accepted (research speed is a divisor - review-settings2 D6)");
    if (!coopgp::GpValueOk("gp.rs", "0.01", &d) || d != 0.01 || !coopgp::GtValueOk("gt.pop", "0.01")) Fail("a float of 0.01 was refused");
    if (coopgp::GpValueOk("gp.gdm", "abc", &d) || coopgp::GpValueOk("gp.gdm", "1.5x", &d) || coopgp::GpValueOk("gp.gdm", "", &d)) Fail("a non-number gp value was accepted");
    if (coopgp::GpValueOk("gp.gdm", "-1", &d) || coopgp::GpValueOk("gp.gdm", "101", &d) || coopgp::GpValueOk("gp.gdm", "1e999", &d)) Fail("an out-of-range gp value was accepted");
    if (!coopgp::GpValueOk("gp.bl", "1", &d) || coopgp::GpValueOk("gp.bl", "2", &d) || coopgp::GpValueOk("gp.ep", "0.5", &d)) Fail("a gp bool range is wrong");
    if (coopgp::GpValueOk("gp.xyz", "1", &d) || coopgp::GpValueOk("gdm", "1", &d) || coopgp::GpValueOk("gt.pop", "1", &d)) Fail("an unknown gp key was accepted");
    if (coopgp::GpIndex("gdm") != 2 || coopgp::GpIndex("gp.dh") != 10 || coopgp::GpIndex("gp.") != -1) Fail("GpIndex is wrong");
    if (!coopgp::GtValueOk("gt.attacks", "5") || coopgp::GtValueOk("gt.attacks", "6") || !coopgp::GtValueOk("gt.limbloss", "3") || coopgp::GtValueOk("gt.limbloss", "-1")) Fail("a gt int range is wrong");
    if (!coopgp::GtValueOk("gt.pop", "0.5") || coopgp::GtValueOk("gt.pop", "nan") || coopgp::GtValueOk("gt.bogus", "1")) Fail("a gt float/key check is wrong");
    const int want[coopgp::kGpCount] = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x21, 0x22 };
    for (int i = 0; i < coopgp::kGpCount; ++i) if (coopgp::GpFields()[i].off != want[i]) Fail("a gp field offset moved");
    char b[48]; const float f = 1.1f; _snprintf_s(b, sizeof b, _TRUNCATE, "%.9g", (double)f);
    if (!coopgp::GpValueOk("gp.gdm", b, &d) || (float)d != f) Fail("a %.9g float did not read back to the same bits");
}

/* prof1: profiles in the world - the slot key, the cap, NEW / PICK / DELETE, profiles.txt, the automatic pick, the save folder
   and the lobby's wire (src/common/profiles.h). */
void t_prof1_profiles()
{
    using namespace coopprof;
    const std::string P(32, 'a'), Q(32, 'b');
    if (SlotKey(P, 1) != P || SlotKey(P, 2) != P + ".2") Fail("profile 1 keeps the bare person id, profile 2 is <person>.2");
    if (PersonOfKey(P + ".7") != P || NumOfKey(P + ".7") != 7 || NumOfKey(P) != 1 || ProfileIdOfKey(P) != P + ".1") Fail("a slot key names its person and number");
    if (!coopstore::SlotKeyOk(P) || !coopstore::SlotKeyOk(P + ".2") || coopstore::SlotKeyOk(P + ".1") || coopstore::SlotKeyOk(P + ".")
        || coopstore::SlotKeyOk(P + ".02") || coopstore::SlotKeyOk("x.2") || coopstore::SlotKeyOk(P + ".2x"))
        Fail("the slot-key shape rule");
    { std::string id; int sl = 0; long long t = 0;
      if (!coopstore::SlotsLineParse("v1\t" + P + ".3\t5\t100", &id, &sl, &t) || id != P + ".3" || sl != 5) Fail("slots.txt keeps a profile's key"); }
    if (!CapValueOk("1") || !CapValueOk("16") || CapValueOk("0") || CapValueOk("17") || CapValueOk("03") || CapValueOk("") || CapValueOk("3x"))
        Fail("profilecap accepts 1..16 only");
    if (CapFromOption("") != 3 || CapFromOption("5") != 5 || CapFromOption("99") != 3) Fail("profilecap defaults to 3");
    std::vector<Row> rows; unsigned n = 0;
    if (NewDecide(rows, P, "Alpha", 2, 0, &n) != kOk || n != 1) Fail("the first profile is number 1");
    Row a; a.person = P; a.num = 1; a.name = "Alpha"; a.faction = "Alpha"; a.created = 10; rows.push_back(a);
    if (NewDecide(rows, P, "Alpha", 2, 0, &n) != kRefusedNameTaken) Fail("a person's two profiles cannot share a name");
    if (NewDecide(rows, P, "Beta", 2, 0, &n) != kOk || n != 2) Fail("the second profile is number 2");
    Row b = a; b.num = 2; b.name = "Beta"; b.faction = "Beta"; rows.push_back(b);
    if (NewDecide(rows, P, "Gamma", 2, 0, &n) != kRefusedCap) Fail("NEW is refused at the cap");
    if (NewDecide(rows, Q, "ALPHA", 2, 0, &n) != kRefusedNameTaken) Fail("T-368: a player name is unique in the world, whoever holds it (the name rule)");
    if (NewDecide(rows, Q, "Omega", 2, 0, &n) != kOk || n != 1) Fail("the cap is per person");
    if (NewDecide(rows, P, "", 5, 0, &n) != kRefusedName) Fail("an empty name is refused");
    if (DeleteDecide(rows, P, 2, 1) != kRefusedInUse || DeleteDecide(rows, P, 9, 0) != kRefusedUnknown || DeleteDecide(rows, P, 2, 0) != kOk)
        Fail("the delete rule: not while played, not an unknown number");
    rows[1].active = 0;
    if (PickDecide(rows, P, 2) != kRefusedUnknown || PickDecide(rows, P, 1) != kOk || DeleteDecide(rows, P, 2, 0) != kRefusedUnknown)
        Fail("a deleted profile cannot be picked or deleted again");
    if (NewDecide(rows, P, "Beta", 2, 0, &n) != kOk || n != 3) Fail("a deleted profile's number stays retired, and its name is free again");
    if (NewDecide(rows, P, "Delta", 5, 2, &n) != kRefusedNumUsed) Fail("a TEST number held before is refused");
    if (NewDecide(rows, P, "Delta", 5, 7, &n) != kOk || n != 7) Fail("a TEST number never held is given");
    Row back;
    if (!RowParse(RowFormat(rows[1]), &back) || back.person != P || back.num != 2 || back.active != 0 || back.name != "Beta" || back.slot != -1)
        Fail("a profiles.txt line round-trips");
    if (!RowParse(RowFormat(rows[0]) + "\r", &back) || back.faction != "Alpha") Fail("a CRLF profiles.txt line reads");
    if (RowParse("prof1\t" + P + "\t0\t-1\t0\t0\tactive\tA\tA", &back) || RowParse("prof1\tzz\t1\t-1\t0\t0\tactive\tA\tA", &back)
        || RowParse("prof1\t" + P + "\t1\t-1\t0\t0\tgone\tA\tA", &back) || RowParse("prof1\t" + P + "\t1\t-1\t0\t0\tactive\tA", &back))
        Fail("a bad profiles.txt line is dropped");
    std::vector<Row> act; unsigned pick = 0;
    if (AutoPickDecide(act, 0, &pick) != kAutoNew || pick != 0) Fail("no profile yet: make one");
    if (AutoPickDecide(act, 2, &pick) != kAutoNew || pick != 2) Fail("TEST profile=2 missing: make number 2");
    Row r1 = a; r1.lastPlayed = 50; Row r2 = b; r2.active = 1; r2.lastPlayed = 90; act.push_back(r1); act.push_back(r2);
    if (AutoPickDecide(act, 0, &pick) != kAutoPick || pick != 2) Fail("the profile played last is picked");
    if (AutoPickDecide(act, 1, &pick) != kAutoPick || pick != 1) Fail("TEST profile=1 is picked when it exists");
    if (AutoNewName("Scott", 0) != "Scott" || AutoNewName("Scott", 2) != "Profile 2" || AutoNewName("", 0) != "Profile") Fail("the automatic profile name");
    const std::string P2 = P + ".2";
    /* the load check (review-prof1 3) */
    if (!LoadProfileMismatch(P + ".3", P2) || LoadProfileMismatch(P2, P2) || LoadProfileMismatch("", P2) || LoadProfileMismatch(P2, ""))
        Fail("a save of another profile is refused at load; a kcworld1 save is not");
    /* numbers never reused; an old world's profile 1 is not stranded (review-prof1 8) */
    {
        std::vector<Row> old; std::map<std::string, int> slots; std::map<std::string, long long> seen;
        slots[P] = 4; slots[P + ".5"] = 9; slots[Q] = 1; seen[P] = 77;
        if (AdoptSlotKeys(&old, P, slots, seen, 100) != 2 || old.size() != 2) Fail("the slot table's keys for a person are taken in");
        else
        {
            const int i1 = FindRow(old, P, 1);
            if (i1 < 0 || old[(size_t)i1].slot != 4 || old[(size_t)i1].lastPlayed != 77 || !old[(size_t)i1].active) Fail("an old world's profile 1 keeps its slot");
            if (NextNum(old, P) != 6) Fail("a number held in the slot table is never given out again");
            if (AdoptSlotKeys(&old, P, slots, seen, 100) != 0) Fail("taking in happens once");
            unsigned tn = 0, mk = 0;
            if (AutoPickDecide(old, 2, &tn) != kAutoNew || tn != 2) Fail("TEST profile=2 on an old world asks for profile 2");
            if (NewDecide(old, P, "Two", 3, 2, &mk) != kOk || mk != 2 || FindRow(old, P, 1) < 0) Fail("profile 2 is made beside the old profile 1");
            if (AutoPickDecide(old, 0, &tn) != kAutoPick || tn != 1) Fail("with no TEST number the old profile 1 (played last) is picked");
        }
    }
    if (coopui::PanelRefusalText(6, std::string()).find("already playing") == std::string::npos || coopui::PanelRefusalText(100, "X") != "X")
        Fail("the title screen says the same-person refusal and a profile dead end in words");
    std::vector<char> w; int kind = 0; unsigned num = 0; std::string nm;
    EncodeRequest(&w, kReqNew, 3, "Alpha");
    if (!DecodeRequest(w, &kind, &num, &nm) || kind != kReqNew || num != 3 || nm != "Alpha") Fail("a PROFILES request round-trips");
    { std::vector<char> t(w); t.push_back(0); if (DecodeRequest(t, &kind, &num, &nm)) Fail("a PROFILES request with a trailing byte is refused"); }
    { std::vector<char> t(w); t[0] = 9; if (DecodeRequest(t, &kind, &num, &nm)) Fail("a PROFILES request of an unknown kind is refused"); }
    Answer an; an.answers = kAnsNew; an.verdict = kRefusedCap; an.cap = 3; an.subject = 4; an.rows = act;
    std::vector<char> aw; EncodeAnswer(&aw, an); Answer got;
    if (!DecodeAnswer(aw, &got) || got.answers != kAnsNew || got.verdict != kRefusedCap || got.cap != 3 || got.subject != 4 || got.rows.size() != 2
        || got.rows[1].name != "Beta" || got.rows[1].lastPlayed != 90 || got.rows[0].slot != -1)
        Fail("a PROFILES answer round-trips");
    aw.resize(aw.size() - 1);
    if (DecodeAnswer(aw, &got)) Fail("a short PROFILES answer is refused");
    if (VerdictText(kRefusedCap, 3).find("3 profiles") == std::string::npos) Fail("the cap refusal names the cap");
    if (coopstore::StoreRefuseReasonText((unsigned)coopstore::kRefuseSamePerson, 48, 48).find("already playing") == std::string::npos) Fail("the same-person refusal says so in words");
}

/* names2a (investigations/names2-design.md Q3): PROFILES kind 3 - the faction name into the sender's own profile row. */
void t_names2a_faction_row()
{
    using namespace coopprof;
    std::vector<char> w; int kind = 0; unsigned num = 0; std::string nm;
    EncodeRequest(&w, kReqFaction, 2, "GapThree");
    if (!DecodeRequest(w, &kind, &num, &nm) || kind != kReqFaction || num != 2 || nm != "GapThree") Fail("names2a: a kind 3 FACTION request round-trips");
    std::string f;
    const std::string long30("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234");
    if (!FactionForRow(long30, &f) || f != long30.substr(0, 24)) Fail("names2a: a 30-character name is cut to its first 24");
    if (!FactionForRow(long30.substr(0, 25), &f) || f.size() != 24) Fail("names2a: a 25-character name is cut to 24");
    if (!FactionForRow(long30.substr(0, 24), &f) || f != long30.substr(0, 24)) Fail("names2a: a 24-character name is kept whole");
    if (!FactionForRow("Gap Three   ", &f) || f != "Gap Three") Fail("names2a: trailing spaces are trimmed");
    if (!FactionForRow("  Gap", &f) || f != "Gap") Fail("names2a: leading spaces are trimmed");
    if (!FactionForRow("ABCDEFGHIJKLMNOPQRSTUVW WXYZ", &f) || f != "ABCDEFGHIJKLMNOPQRSTUVW") Fail("names2a: a space the cut leaves at the end is trimmed");
    f = "old";
    if (FactionForRow("", &f) || FactionForRow("    ", &f) || f != "old") Fail("names2a: an empty name is refused and the old value kept");
    if (!FactionForRow("Caf\xC3\xA9", &f) || f != "Caf\xC3\xA9") Fail("names2a: T-368: a name beyond plain characters stands in the row unchanged");
    Row r; r.num = 24; r.slot = 3; r.faction = "x";
    if (!FactionForRow(long30 + long30, &r.faction)) Fail("names2a: a 60-character name fits the row");
    r.person = std::string(32, 'a'); r.name = "Crew";
    Row back;
    if (!RowParse(RowFormat(r), &back) || back.faction != long30.substr(0, 24)) Fail("names2a: a cut faction survives profiles.txt (the notebook keeps the profile)");
    const std::string P(32, 'a'), Q(32, 'b');
    std::vector<Row> rows;
    { Row a; a.person = P; a.num = 1; a.name = "One"; a.faction = "One"; rows.push_back(a); }
    { Row a; a.person = P; a.num = 2; a.name = "Two"; a.faction = "Two"; rows.push_back(a); }
    { Row a; a.person = P; a.num = 3; a.name = "Three"; a.faction = "Three"; a.active = 0; rows.push_back(a); }
    { Row a; a.person = Q; a.num = 1; a.name = "Q"; a.faction = "Q"; rows.push_back(a); }
    int i = -1;
    if (FactionDecide(rows, P + ".2", 2, "GapThree", &i, &f) != kFacSet || i != 1 || f != "GapThree") Fail("names2a: profile 2 sets its own row");
    if (FactionDecide(rows, P, 1, "One ", &i, &f) != kFacSame || i != 0) Fail("names2a: an unchanged name (after trimming) is SAME");
    if (FactionDecide(rows, P + ".2", 1, "GapThree", &i, &f) != kFacRefusedSender) Fail("names2a: profile 2 may not write profile 1's row");
    if (FactionDecide(rows, P, 2, "GapThree", &i, &f) != kFacRefusedSender) Fail("names2a: profile 1 may not write profile 2's row");
    if (FactionDecide(rows, std::string(), 1, "GapThree", &i, &f) != kFacRefusedSender) Fail("names2a: a connection not admitted (the lobby) is refused");
    if (FactionDecide(rows, Q, 0, "GapThree", &i, &f) != kFacRefusedSender) Fail("names2a: profile number 0 is refused");
    if (FactionDecide(rows, P + ".3", 3, "GapThree", &i, &f) != kFacRefusedUnknown) Fail("names2a: a deleted profile's row is not written");
    if (FactionDecide(rows, P + ".4", 4, "GapThree", &i, &f) != kFacRefusedUnknown) Fail("names2a: a profile with no row is refused");
    if (FactionDecide(rows, Q, 1, "   ", &i, &f) != kFacRefusedName || rows[3].faction != "Q") Fail("names2a: an empty name is refused, the row unchanged");
    if (FactionDecide(rows, Q, 1, long30, &i, &f) != kFacSet || f.size() != 24) Fail("names2a: a long name is set cut to 24");
}

/* H068 (owner 471): Kenshi's default "Nameless" at a load becomes each profile's own "Nameless <n>" - the operator's and every joiner's, played
   or not (a profile whose folder already held a save counts as played at its first admission) - and that answer shows no box. */
void t_h068_default_faction_numbers()
{
    using namespace coopprof;
    const std::string A(32, 'a'), B(32, 'b'), C(32, 'c');
    std::vector<Row> rows;
    { Row r; r.person = A; r.num = 1; r.name = "PlayerA"; r.faction = NamelessFree(rows, -1); r.created = 10; r.played = 1; rows.push_back(r); }
    { Row r; r.person = B; r.num = 1; r.name = "PlayerB"; r.faction = NamelessFree(rows, -1); r.created = 12; r.played = 1; rows.push_back(r); }
    if (rows[0].faction != "Nameless 1" || rows[1].faction != "Nameless 2") Fail("H068: profiles are made with Nameless 1, Nameless 2");
    int i = -1; std::string f;
    /* the operator's first load */
    if (FactionDecide(rows, A, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 1" || i != 0) Fail("H068: the operator's first load keeps Nameless 1");
    rows[(size_t)i].faction = f;
    if (FactionApplyDecide(kFacDefault, "Nameless", "Nameless", f) != kFacApplySilent) Fail("H068: the operator's answer shows no box");
    /* a joiner's first load */
    if (FactionDecide(rows, B, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 2" || i != 1) Fail("H068: a joiner's first load keeps Nameless 2");
    rows[(size_t)i].faction = f;
    if (FactionApplyDecide(kFacDefault, "Nameless", "Nameless", f) != kFacApplySilent) Fail("H068: a joiner's answer shows no box");
    { Row r; r.person = C; r.num = 1; r.name = "PlayerC"; r.faction = NamelessFree(rows, -1); r.created = 30; r.played = 0; rows.push_back(r); }
    if (rows[2].faction != "Nameless 3") Fail("H068: the third profile is made with Nameless 3");
    if (FactionDecide(rows, C, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 3") Fail("H068: a never-played joiner's first load keeps Nameless 3");
    rows[(size_t)i].faction = f;
    /* a second profile of the operator */
    { Row r; r.person = A; r.num = 2; r.name = "Second"; r.faction = NamelessFree(rows, -1); r.created = 40; rows.push_back(r); }
    if (rows[3].faction != "Nameless 4") Fail("H068: a second profile is made with Nameless 4");
    /* a played profile reloading a save whose faction is still Kenshi's Nameless */
    if (FactionDecide(rows, B, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 2") Fail("H068: a played profile's reload of a Nameless save keeps Nameless 2");
    /* a typed name is judged by the name rule: a free one is set, a taken one goes back */
    if (FactionDecide(rows, B, 1, "Hub Rats", &i, &f, 1) != kFacSet || f != "Hub Rats") Fail("H068: a free typed name is the player's");
    rows[1].faction = f;
    if (FactionDecide(rows, C, 1, "hub rats", &i, &f, 1) != kFacBack || f != "Nameless 3") Fail("H068: a typed name another holds goes back");
    if (FactionDecide(rows, A, 1, "Nameless 3", &i, &f, 1) != kFacBack || f != "Nameless 1") Fail("H068: a typed Nameless number another holds goes back");
    /* B, now 'Hub Rats', reloads an older save still named Nameless: its row holds no number - the lowest free one (2, freed by its rename) */
    if (FactionDecide(rows, B, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 2") Fail("H068: a row without a Nameless number takes the lowest free one at a Nameless load");
    /* no profile's faction is Kenshi's plain Nameless after these loads */
    for (size_t k = 0; k < rows.size(); ++k) if (NameSame(rows[k].faction, kEngineFactionDefault)) Fail("H068: no row holds Kenshi's plain Nameless");
}

/* H069: a faction name counts as held only while a faction carries it NOW (Kenshi's record index keeps every old name; the FACTION tab
   renames a faction without its record), and a stand-in under a clash name takes its player's name back once nothing holds it. A toy
   game: records with a record name and an optional live faction, and an index that, like Kenshi's, only ever adds names. */
struct H069Rec { std::string record, live; bool hasLive; };
struct H069Game
{
    std::vector<H069Rec> recs; std::vector<std::pair<std::string, int> > index;
    int Add(const std::string& n, bool live) { H069Rec r; r.record = n; r.live = n; r.hasLive = live; recs.push_back(r); Index(n, (int)recs.size() - 1); return (int)recs.size() - 1; }
    void Index(const std::string& n, int r) { for (size_t k = 0; k < index.size(); ++k) if (index[k].first == n) { index[k].second = r; return; } index.push_back(std::make_pair(n, r)); }
    void RenameRecord(int r, const std::string& n) { Index(n, r); recs[(size_t)r].record = n; }   /* the old name stays indexed */
    void RenameBoth(int r, const std::string& n) { recs[(size_t)r].live = n; RenameRecord(r, n); }   /* the mod's rename */
    void TabRename(int r, const std::string& n) { recs[(size_t)r].live = n; }                        /* Kenshi's FACTION tab */
    bool Held(const std::string& n, int except) const
    {
        int hit = -1; for (size_t k = 0; k < index.size(); ++k) if (index[k].first == n) hit = index[k].second;
        bool live = false; for (size_t k = 0; k < recs.size(); ++k) if ((int)k != except && recs[k].hasLive && recs[k].live == n) live = true;
        const bool ih = hit >= 0 && hit != except;
        return coopslot::FactionNameHeld(n, live, ih, ih ? recs[(size_t)hit].record : std::string(), ih && recs[(size_t)hit].hasLive, ih ? recs[(size_t)hit].live : std::string());
    }
    std::string Clash(const std::string& want, int slot, int self) const { const bool wt = Held(want, self); return coopslot::StandInClashName(want, slot, wt, wt && Held(want + " (peer)", self)); }
};
void t_h069_freed_names_are_free()
{
    /* the pure rule */
    if (!coopslot::FactionNameHeld("N1", true, false, "", false, "")) Fail("H069: a live faction carrying the name holds it");
    if (coopslot::FactionNameHeld("N1", false, true, "N4", true, "N4")) Fail("H069: an indexed record renamed away does not hold its old name");
    if (coopslot::FactionNameHeld("N1", false, true, "N1", true, "N4")) Fail("H069: a record whose faction was renamed by the tab does not hold the old name");
    if (!coopslot::FactionNameHeld("N1", false, true, "N1", false, "")) Fail("H069: a record with no live faction holds its record name");
    if (coopslot::FactionNameHeld("N1", false, false, "", false, "")) Fail("H069: no hit, not held");
    if (!coopslot::StandInAsksAgain("N4 (peer)", "N4") || coopslot::StandInAsksAgain("N4", "N4") || coopslot::StandInAsksAgain("x", "")) Fail("H069: StandInAsksAgain");
    /* a freed own old name (T965 A 314.482): my faction 'Nameless 1' renamed on the FACTION tab to 'Nameless 4'; B's stand-in asks 'Nameless 1' */
    {
        H069Game g; const int mine = g.Add("Nameless 1", true); const int sb = g.Add("Nameless 2", true); g.Add("Nameless 3", true);
        g.TabRename(mine, "Nameless 4");
        if (g.Held("Nameless 1", sb)) Fail("H069: my old name is free after a tab rename");
        if (g.Clash("Nameless 1", 1, sb) != "Nameless 1") Fail("H069: B's stand-in takes my old name plainly");
        if (!g.Held("Nameless 4", sb)) Fail("H069: my new name is held");
        g.RenameRecord(mine, "Nameless 4");   /* MyRecordFollowsName */
        if (g.Held("Nameless 1", sb) || !g.Held("Nameless 4", sb)) Fail("H069: after the record follows, the old name is free and the new one held");
    }
    /* a 40 ms transient name (T965 B 272.541-300.214): C's stand-in carries 'Nameless 4' for a moment, then 'Nameless 3' again */
    {
        H069Game g; g.Add("Nameless 2", true); const int sa = g.Add("Nameless 1", true); const int sc = g.Add("Nameless 3", true);
        g.RenameBoth(sc, "Nameless 4"); g.RenameBoth(sc, "Nameless 3");
        if (g.Held("Nameless 4", sa) || g.Clash("Nameless 4", 0, sa) != "Nameless 4") Fail("H069: a name held for a moment and left is free (no peer)");
    }
    /* the clash happens inside the moment: A's stand-in goes aside, and takes its name back as soon as C's stand-in leaves it */
    {
        H069Game g; g.Add("Nameless 2", true); const int sa = g.Add("Nameless 1", true); const int sc = g.Add("Nameless 3", true);
        g.RenameBoth(sc, "Nameless 4");
        const std::string asked("Nameless 4");
        std::string cur = g.Clash(asked, 0, sa); g.RenameBoth(sa, cur);
        if (cur != "Nameless 4 (peer)") Fail("H069: while C's stand-in carries the name, A's goes aside");
        g.RenameBoth(sc, "Nameless 3");
        if (!coopslot::StandInAsksAgain(cur, asked)) Fail("H069: the aside stand-in asks again");
        cur = g.Clash(asked, 0, sa); g.RenameBoth(sa, cur);
        if (cur != "Nameless 4" || coopslot::StandInAsksAgain(cur, asked)) Fail("H069: the freed name comes back, and the stand-in stops asking");
        if (g.Held("Nameless 4 (peer)", -1)) Fail("H069: no faction carries the clash name afterwards");
    }
}

/* T-368 (owner 265-270, 472): player names and faction names unique per world, connected or not; a new player's faction "Nameless <n>". */
void t_t368_unique_names()
{
    using namespace coopprof;
    /* the name rule */
    if (!NameSame("Anna", "anna") || !NameSame(" Anna ", "Anna") || !NameSame("Hub  Rats", "hub rats") || NameSame("Hub Rats", "HubRats")
        || NameSame("Anna", "Anne") || NameKey("  A  b ") != "a b") Fail("T-368: the name rule (case, ends, inner runs of spaces)");
    const std::string P(32, 'a'), Q(32, 'b'), R(32, 'c');
    std::vector<Row> rows;
    { Row a; a.person = P; a.num = 1; a.name = "Anna"; a.faction = "Hub Rats"; a.created = 100; a.played = 1; rows.push_back(a); }
    { Row a; a.person = Q; a.num = 1; a.name = "Ben"; a.faction = "Nameless 1"; a.created = 200; a.played = 0; rows.push_back(a); }
    { Row a; a.person = Q; a.num = 2; a.name = "Cid"; a.faction = "Sand Fleas"; a.created = 300; a.played = 1; a.active = 0; rows.push_back(a); }
    { Row a; a.person = R; a.num = 1; a.name = "Dee"; a.faction = "Nameless 3"; a.created = 400; a.played = 1; rows.push_back(a); }
    /* player names */
    unsigned n = 0;
    if (!PlayerNameTaken(rows, "ANNA") || PlayerNameTaken(rows, "Cid")) Fail("T-368: an active profile's name is taken, a deleted one's is free");
    if (NewDecide(rows, R, "anna", 5, 0, &n) != kRefusedNameTaken) Fail("T-368: NEW with another person's player name is refused");
    if (NewDecide(rows, R, "Cid", 5, 0, &n) != kOk) Fail("T-368: NEW with a deleted profile's player name is made");
    if (VerdictText(kRefusedNameTaken, 0) != "that name is already taken in this world") Fail("T-368: the approved NEW PROFILE words (owner 270 a)");
    if (coopui::NameTakenText("Anna") != "The name \"Anna\" is already taken in this world." || !coopui::NoticeIsNameTaken(coopui::NameTakenText("Anna"))
        || coopui::NoticeTitle(coopui::NameTakenText("Anna")) != "CAN'T JOIN") Fail("T-368: the CAN'T JOIN name-taken box");
    /* the lowest free Nameless <n> */
    if (NamelessFree(rows, -1) != "Nameless 2") Fail("T-368: the lowest Nameless number no active faction holds (1 and 3 held)");
    if (NamelessFree(rows, 1) != "Nameless 1") Fail("T-368: a row's own Nameless number is free for that row");
    if (!IsNamelessNumbered("Nameless 12") || IsNamelessNumbered("Nameless") || IsNamelessNumbered("Nameless 0") || IsNamelessNumbered("Nameless 01")
        || IsNamelessNumbered("Nameless 2a") || IsNamelessNumbered("nameless 2")) Fail("T-368: the Nameless <n> shape");
    /* the lists: every OTHER active profile's faction name */
    const std::vector<std::string> forP = TakenFactionsFor(rows, P, 1);
    if (forP.size() != 2 || forP[0] != "Nameless 1" || forP[1] != "Nameless 3") Fail("T-368: TAKEN for a profile leaves out its own row and deleted rows");
    if (!FactionTakenIn(forP, "  nameless   1 ") || FactionTakenIn(forP, "Hub Rats") || FactionTakenIn(forP, "Sand Fleas") || FactionTakenIn(forP, ""))
        Fail("T-368: the game's check - the name rule, its own name and freed names allowed");
    { std::vector<std::string> l; l.push_back("ABCDEFGHIJKLMNOPQRSTUVWX");
      if (!FactionTakenIn(l, "ABCDEFGHIJKLMNOPQRSTUVWXYZ")) Fail("T-368: a name longer than the row is judged as the row keeps it"); }
    /* the world's judgement of a faction name */
    int i = -1; std::string f;
    if (FactionDecide(rows, P, 1, "Sand Fleas", &i, &f, 1) != kFacSet || f != "Sand Fleas") Fail("T-368: a rename to a deleted profile's faction name is taken");
    if (FactionDecide(rows, P, 1, "nameless 3", &i, &f, 1) != kFacBack || f != "Hub Rats" || i != 0) Fail("T-368: a rename to a taken name goes back to the row's own");
    if (FactionDecide(rows, P, 1, "Nameless 3", &i, &f, 0) != kFacMoved || f != "Nameless 2") Fail("T-368: a load with a taken name takes the lowest free Nameless number");
    if (FactionDecide(rows, P, 1, "hub rats", &i, &f, 1) != kFacSet || f != "hub rats") Fail("T-368: a profile may re-case its own faction name");
    if (FactionDecide(rows, Q, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 1") Fail("T-368: a never-played profile's new game keeps its row's Nameless number");
    if (FactionDecide(rows, R, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 3" || i != 3)
        Fail("T-368: a PLAYED profile reloading a save whose faction is Kenshi's Nameless keeps its own Nameless number");
    if (FactionDecide(rows, R, 1, "Nameless", &i, &f, 1) != kFacSet || f != "Nameless") Fail("T-368: Nameless TYPED on the FACTION tab is judged as any name");
    { std::vector<Row> r2(rows); r2[1].faction = "Hub Rats";
      if (FactionDecide(r2, Q, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 1") Fail("T-368: a new game whose row holds no Nameless number gets the lowest free one");
      r2[1].faction = "Nameless 3";
      if (FactionDecide(r2, Q, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 1") Fail("T-368: a new game whose row's Nameless number another holds gets the lowest free one");
      r2[1].faction = "Nameless"; r2[1].played = 1;
      if (FactionDecide(r2, Q, 1, "Nameless", &i, &f, 0) != kFacDefault || f != "Nameless 1") Fail("T-368: a row holding Kenshi's plain Nameless takes the lowest free number at a load");
      /* a world whose names clashed before the rule: the profile made first keeps the name */
      r2[1].faction = "Hub Rats"; r2[1].played = 1;
      if (FactionDecide(r2, P, 1, "Hub Rats", &i, &f, 0) != kFacSame) Fail("T-368: of two rows holding one name, the one made first keeps it");
      if (FactionDecide(r2, Q, 1, "Hub Rats", &i, &f, 0) != kFacMoved || f != "Nameless 1") Fail("T-368: of two rows holding one name, the later one is moved at its load");
      if (FactionDecide(r2, Q, 1, "Hub Rats", &i, &f, 1) != kFacBack || f != "Hub Rats") Fail("T-368: a rename to a name another holds is refused even in a clashing world"); }
    if (FactionDecide(rows, Q + ".2", 2, "Hub Rats", &i, &f, 0) != kFacRefusedUnknown) Fail("T-368: a deleted profile's row is not judged");
    /* what the game does with the world's answer */
    if (FactionApplyDecide(kFacBack, "Nameless 3", "Nameless 3", "Hub Rats") != kFacApplyBackBox) Fail("T-368: back - the change-back box");
    if (FactionApplyDecide(kFacMoved, "Nameless 3", "Nameless 3", "Nameless 2") != kFacApplyMovedBox) Fail("T-368: moved - the load box");
    if (FactionApplyDecide(kFacDefault, "Nameless", "Nameless", "Nameless 2") != kFacApplySilent) Fail("T-368: Kenshi's default name takes the profile's Nameless number - no box");
    if (FactionApplyDecide(kFacBack, "Other", "Nameless 3", "Hub Rats") != kFacApplyDrop) Fail("T-368: an answer about a name the faction no longer has is dropped");
    if (FactionApplyDecide(kFacDefault, "Hub Rats", "Hub Rats", "Nameless 2") != kFacApplyDrop) Fail("T-368: a Nameless number is applied only to Kenshi's Nameless");
    if (FactionApplyDecide(kFacSet, "A", "A", "B") != kFacApplyDrop || FactionApplyDecide(kFacBack, "A", "A", "") != kFacApplyDrop) Fail("T-368: nothing else is applied");
    /* the words */
    if (std::string(coopui::kFactionBoxTitle) != "Faction Name"
        || coopui::FactionBackText("Hub Rats", "Sand Fleas") != "Your faction name \"Hub Rats\" is already taken in this world, so it was changed back to \"Sand Fleas\"."
        || coopui::FactionMovedText("Hub Rats", "Sand Fleas") != "Another player in this world already uses the faction name \"Hub Rats\". Your faction is now called \"Sand Fleas\".")
        Fail("T-368: the approved Faction Name box words (owner 270 a)");
    /* the wire */
    std::vector<char> w; int kind = 0; unsigned num = 0; std::string nm;
    EncodeRequest(&w, kReqRename, 2, "Hub Rats");
    if (!DecodeRequest(w, &kind, &num, &nm) || kind != kReqRename || num != 2 || nm != "Hub Rats") Fail("T-368: a kind 5 RENAME request round-trips");
    w.clear(); EncodeTaken(&w, forP);
    std::vector<std::string> back;
    if (AnswerKindOf(w) != kAnsTaken || !DecodeTaken(w, &back) || back != forP) Fail("T-368: a TAKEN answer round-trips");
    { std::vector<char> t(w); t.push_back(0); if (DecodeTaken(t, &back)) Fail("T-368: a TAKEN answer with a trailing byte is refused"); }
    { std::vector<char> t(w.begin(), w.end() - 1); if (DecodeTaken(t, &back)) Fail("T-368: a torn TAKEN answer is refused"); }
    { std::vector<char> e; EncodeTaken(&e, std::vector<std::string>()); if (!DecodeTaken(e, &back) || !back.empty()) Fail("T-368: an empty TAKEN list round-trips"); }
    FactionAnswer fa; fa.verdict = kFacMoved; fa.num = 3; fa.asked = "Hub Rats"; fa.name = "Nameless 2";
    w.clear(); EncodeFactionAnswer(&w, fa);
    FactionAnswer fb;
    if (AnswerKindOf(w) != kAnsFaction || !DecodeFactionAnswer(w, &fb) || fb.verdict != kFacMoved || fb.num != 3 || fb.asked != "Hub Rats" || fb.name != "Nameless 2")
        Fail("T-368: a FACTION answer round-trips");
    { std::vector<char> t(w); t[1] = (char)kFacSet; if (DecodeFactionAnswer(t, &fb)) Fail("T-368: a FACTION answer with a verdict that changes nothing is refused"); }
    { Answer a; std::vector<char> t(w); if (DecodeAnswer(t, &a)) Fail("T-368: the list decoder does not read a FACTION answer"); }
    if (AnswerKindOf(std::vector<char>()) != -1) Fail("T-368: an empty answer has no kind");
}

/* T-368: every faction name a player can type is unique and kept unchanged; own-name re-case; the stale stand-in moved aside; the
   answer acknowledgement; the edges of the name rule, the clash tie-break, the file reader and the wire. */
void t_t368_format_owed_reply()
{
    using namespace coopprof;
    const std::string hashy("#1; Crew");
    /* 1: a world folder whose rows may carry the '#' field is format 2 - a build that knows 1 refuses it */
    {
        Row a; a.person = std::string(32, 'a'); a.num = 1; a.name = "Anna"; a.faction = hashy; a.created = 1;
        const std::string line = RowFormat(a);
        if (line.find("\t#23313B2043726577\t") == std::string::npos || coopworld::DisplayNameOk(FactionFieldFormat(hashy), 0))
            Fail("T-368: a '#' faction field is one the display-name rule (the format-1 row reader) refuses");
        unsigned n = 0;
        const int rs = swformat::FormatParse(true, swformat::FormatText(swformat::kKindWorld, swformat::kWorldFolderFormat), swformat::kKindWorld, &n);
        if (swformat::kWorldFolderFormat != 3 || rs != swformat::kFormatRead || swformat::FormatDecide(rs, n, 1) != swformat::kFormatRefuseNewer
            || !swformat::FormatRefuses(swformat::FormatDecide(rs, n, 1)))
            Fail("T-368: a build at world format 1 refuses a folder this build wrote");
        if (swformat::FormatDecide(swformat::kFormatRead, 1, swformat::kWorldFolderFormat) != swformat::kFormatConvert
            || swformat::FormatSteps(1, swformat::kWorldFolderFormat).size() != 2 || swformat::FormatSteps(2, swformat::kWorldFolderFormat).size() != 1)
            Fail("T-368 / T-811: this build converts a format-1 world in two steps and a format-2 world in one");
        if (!swformat::FormatRefuses(swformat::FormatDecide(rs, n, 2))) Fail("T-811: a build at world format 2 does not refuse a format-3 folder");
    }
    /* 2: an accepted judgement clears what was owed; a changed name is owed instead; a refusal keeps it */
    if (FactionOwedAfter(kFacSet) != kFacOwedClear || FactionOwedAfter(kFacSame) != kFacOwedClear) Fail("T-368: an accepted name clears the owed answer");
    if (FactionOwedAfter(kFacBack) != kFacOwedReplace || FactionOwedAfter(kFacMoved) != kFacOwedReplace || FactionOwedAfter(kFacDefault) != kFacOwedReplace)
        Fail("T-368: a changed name's answer is owed in place of the old one");
    if (FactionOwedAfter(kFacRefusedSender) != kFacOwedKeep || FactionOwedAfter(kFacRefusedUnknown) != kFacOwedKeep || FactionOwedAfter(kFacRefusedName) != kFacOwedKeep)
        Fail("T-368: a refusal leaves the owed answer");
    {   /* a Moved answer is owed; the player renames to a free name before it arrives - the world accepts it and owes nothing */
        const std::string P(32, 'a'), Q(32, 'b');
        Row a; a.person = P; a.num = 1; a.name = "Anna"; a.faction = "Nameless 2"; a.created = 1; a.played = 1;
        Row b = a; b.person = Q; b.name = "Ben"; b.faction = "Hub Rats"; b.created = 2;
        std::vector<Row> rows; rows.push_back(a); rows.push_back(b);
        int i = -1; std::string fac;
        const int v = FactionDecide(rows, SlotKey(P, 1), 1, "Free Folk", &i, &fac, 1);
        if (v != kFacSet || FactionOwedAfter(v) != kFacOwedClear) Fail("T-368: a newer accepted rename clears an older owed answer");
    }
    /* 3: the game acknowledges only an answer it carries; a record holding the name sends the kept name back once */
    if (FactionApplyDecide(kFacMoved, "Nameless 2", "Hub Rats", "Nameless 2") != kFacApplyHave) Fail("T-368: an answer already applied is had");
    if (FactionApplyDecide(kFacSet, "B", "A", "B") != kFacApplyDrop) Fail("T-368: only the three answer verdicts apply");
    if (FactionReplyDecide(kFacApplyDrop, false, false) != kFacReplyNone || FactionReplyDecide(kFacApplyDrop, true, false) != kFacReplyNone)
        Fail("T-368: an old answer is neither acknowledged nor reported");
    if (FactionReplyDecide(kFacApplyHave, false, false) != kFacReplySeen || FactionReplyDecide(kFacApplyHave, true, true) != kFacReplySeen)
        Fail("T-368: an answer the faction carries is acknowledged");
    if (FactionReplyDecide(kFacApplyMovedBox, false, false) != kFacReplySeen || FactionReplyDecide(kFacApplySilent, false, true) != kFacReplySeen)
        Fail("T-368: an applied answer is acknowledged");
    if (FactionReplyDecide(kFacApplyBackBox, true, false) != kFacReplyReport) Fail("T-368: a record holding the name - the kept name is reported back");
    if (FactionReplyDecide(kFacApplyMovedBox, true, true) != kFacReplyNone) Fail("T-368: the same answer is reported back once only");
}
void t_t368_names_all_typed()
{
    using namespace coopprof;
    const std::string P(32, 'a'), Q(32, 'b'), R(32, 'c');
    const std::string cafe("Caf\xC3\xA9"), hashy("#1; Crew"), tabby("A\tB"), pct("50% Crew");
    /* 2: the row's field form */
    if (FactionFieldFormat("Hub Rats") != "Hub Rats" || FactionFieldFormat(pct) != pct) Fail("T-368: a plain faction name is written as it is");
    if (FactionFieldFormat(hashy) != "#23313B2043726577") Fail("T-368: a name with '#' / ';' is written as '#' + hex");
    {
        const char* names[] = { "Caf\xC3\xA9", "#1; Crew", "A\tB", "50% Crew", "Hub Rats", ";", "\xE6\x97\xA5\xE6\x9C\xAC" };
        for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); ++k)
        {
            std::string back;
            if (!FactionFieldParse(FactionFieldFormat(names[k]), &back) || back != names[k]) Fail(std::string("T-368: the field form gives back the name typed: ") + names[k]);
        }
    }
    {
        std::string back;
        if (FactionFieldParse("", &back) || FactionFieldParse("#", &back) || FactionFieldParse("#2", &back) || FactionFieldParse("#ZZ", &back)
            || FactionFieldParse("#20", &back) || FactionFieldParse("#41424320", &back) || FactionFieldParse(" Hub", &back)
            || FactionFieldParse("#" + std::string(50, '4'), &back)) Fail("T-368: a field that is no name (empty, torn hex, edge space, over 24 bytes) is refused");
        if (!FactionFieldParse("#41", &back) || back != "A" || !FactionFieldParse("#4a", &back) || back != "J") Fail("T-368: hex digits of either case read");
    }
    {
        Row r; r.person = P; r.num = 1; r.name = "Anna"; r.faction = hashy; r.created = 5;
        const std::string line = RowFormat(r);
        Row back;
        if (line.find(';') != std::string::npos || !RowParse(line, &back) || back.faction != hashy) Fail("T-368: a '#' / ';' faction survives profiles.txt unchanged");
        r.faction = cafe;
        if (!RowParse(RowFormat(r), &back) || back.faction != cafe) Fail("T-368: a non-ASCII faction survives profiles.txt unchanged");
        if (!RowParse("prof1\t" + P + "\t1\t-1\t5\t0\tactive\tAnna\tHub Rats\tplayed", &back) || back.faction != "Hub Rats") Fail("T-368: a row written before the field form reads unchanged");
    }
    std::string f; int i = -1;
    if (!FactionForRow(hashy, &f) || f != hashy || !FactionForRow(tabby, &f) || f != tabby) Fail("T-368: every name a player can type is judged");
    {
        std::vector<Row> rows;
        { Row a; a.person = P; a.num = 1; a.name = "Anna"; a.faction = cafe; a.created = 100; a.played = 1; rows.push_back(a); }
        { Row a; a.person = Q; a.num = 1; a.name = "Ben"; a.faction = hashy; a.created = 200; a.played = 1; rows.push_back(a); }
        { Row a; a.person = R; a.num = 1; a.name = "Cid"; a.faction = "Nameless 1"; a.created = 300; a.played = 1; rows.push_back(a); }
        if (FactionDecide(rows, R, 1, "CAF\xC3\xA9", &i, &f, 1) != kFacBack || f != "Nameless 1") Fail("T-368: a non-ASCII name another holds is refused (the name rule on the decoded text)");
        if (FactionDecide(rows, R, 1, "#1;  crew", &i, &f, 1) != kFacBack) Fail("T-368: a '#' / ';' name another holds is refused");
        if (FactionDecide(rows, R, 1, "Caf\xC3\x89", &i, &f, 1) != kFacSet) Fail("T-368: bytes beyond ASCII are compared as they are");
        const std::vector<std::string> forR = TakenFactionsFor(rows, R, 1);
        if (!FactionTakenIn(forR, " caf\xC3\xA9 ") || !FactionTakenIn(forR, "#1; CREW") || FactionTakenIn(forR, "Nameless 1")) Fail("T-368: the game's check holds every name");
        std::vector<char> w; std::vector<std::string> back;
        EncodeTaken(&w, forR);
        if (!DecodeTaken(w, &back) || back != forR) Fail("T-368: TAKEN carries every name unchanged");
    }
    /* 5: a player may re-case or re-space their OWN name, also in a world whose names clashed before the rule */
    {
        std::vector<Row> rows;
        { Row a; a.person = P; a.num = 1; a.name = "Anna"; a.faction = "Hub Rats"; a.created = 100; a.played = 1; rows.push_back(a); }
        { Row a; a.person = Q; a.num = 1; a.name = "Ben"; a.faction = "Hub Rats"; a.created = 200; a.played = 1; rows.push_back(a); }
        if (FactionDecide(rows, P, 1, "hub  RATS", &i, &f, 1) != kFacSet || f != "hub  RATS") Fail("T-368: the first holder of a clashing name may re-case it");
        if (FactionDecide(rows, Q, 1, "HUB RATS", &i, &f, 1) != kFacBack || f != "Hub Rats") Fail("T-368: the later holder of a clashing name may not take it in another case");
        if (!TakenFactionsFor(rows, P, 1).empty()) Fail("T-368: the first holder's list leaves out its own name repeated by a later row");
        const std::vector<std::string> forQ = TakenFactionsFor(rows, Q, 1);
        if (forQ.size() != 1 || forQ[0] != "Hub Rats") Fail("T-368: the later holder's list has the first holder's name");
        rows.pop_back();
        if (FactionDecide(rows, P, 1, "Hub Rats", &i, &f, 1) != kFacSame) Fail("T-368: a rename equal to the current name changes nothing");
        if (FactionDecide(rows, P, 1, "HUB RATS", &i, &f, 1) != kFacSet) Fail("T-368: a profile may re-case its own name");
        /* the tie-break: made in the same second, the row listed first holds the name */
        { Row a; a.person = Q; a.num = 1; a.name = "Ben"; a.faction = "hub rats"; a.created = 100; a.played = 1; rows.push_back(a); }
        if (!FactionHeldEarlier(rows, "Hub Rats", 1) || FactionHeldEarlier(rows, "Hub Rats", 0)) Fail("T-368: same second - the row listed first holds the name");
    }
    /* the file reader */
    {
        Row a; a.person = P; a.num = 1; a.name = "Anna"; a.faction = "Hub Rats"; a.created = 1;
        Row d = a; d.num = 2; d.name = "Gone"; d.active = 0;
        Row b = a; b.person = Q; b.name = "Ben"; b.faction = hashy;
        const std::vector<Row> got = ActiveRowsOfFile(RowFormat(a) + "\r\n" + RowFormat(d) + "\r\nnot a row\r\n\r\n" + RowFormat(b));
        if (got.size() != 2 || got[0].name != "Anna" || got[0].faction != "Hub Rats" || got[1].faction != hashy) Fail("T-368: the file reader - CRLF, deleted rows, junk, no final newline");
    }
    /* what the game does with an answer: over 24 characters, edge spaces, a name equal to the one it has */
    {
        const std::string long30("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234");
        if (FactionApplyDecide(kFacBack, "  " + long30 + " ", long30.substr(0, 24), "Old") != kFacApplyBackBox) Fail("T-368: an answer judged on the row's cut of a long name applies");
        if (FactionApplyDecide(kFacMoved, " Hub Rats ", "Hub Rats", "Nameless 2") != kFacApplyMovedBox) Fail("T-368: an answer applies through edge spaces");
        if (FactionApplyDecide(kFacMoved, "Nameless 2", "Nameless 2", "Nameless 2") != kFacApplyHave) Fail("T-368: an answer naming the name it has is had already");
    }
    /* the answers on the wire */
    {
        FactionAnswer fa, fb; fa.num = 4; fa.asked = hashy; fa.name = cafe;
        const int vs[] = { kFacBack, kFacDefault };
        for (int k = 0; k < 2; ++k)
        {
            fa.verdict = vs[k];
            std::vector<char> w; EncodeFactionAnswer(&w, fa);
            if (!DecodeFactionAnswer(w, &fb) || fb.verdict != vs[k] || fb.num != 4 || fb.asked != hashy || fb.name != cafe) Fail("T-368: changed-back / default-name answers round-trip");
            w.push_back(0);
            if (DecodeFactionAnswer(w, &fb)) Fail("T-368: an answer with a trailing byte is refused");
        }
        std::vector<std::string> many(kTakenWireMax + 5, std::string("N")), back;
        std::vector<char> w; EncodeTaken(&w, many);
        if (!DecodeTaken(w, &back) || back.size() != kTakenWireMax) Fail("T-368: a TAKEN list over the limit is cut to it");
        std::vector<char> over; over.push_back((char)kAnsTaken); over.push_back(0); WPutU32(&over, kTakenWireMax + 1);
        if (DecodeTaken(over, &back)) Fail("T-368: a TAKEN count over the limit is refused");
        std::vector<char> r; int kind = 0; unsigned num = 0; std::string nm;
        EncodeRequest(&r, kReqFactionSeen, 3, cafe);
        if (!DecodeRequest(r, &kind, &num, &nm) || kind != kReqFactionSeen || num != 3 || nm != cafe) Fail("T-368: a kind 6 FACTION SEEN round-trips");
    }
    /* 1: a stand-in record carrying the world's name moves aside; any other record keeps its name */
    {
        std::string aside;
        if (!coopslot::StandInAsideName(coopslot::StandInId(3), "Hub Rats", false, &aside) || aside != "Hub Rats (peer)") Fail("T-368: a stand-in moves aside to '<name> (peer)'");
        if (!coopslot::StandInAsideName(coopslot::StandInId(3), "Hub Rats", true, &aside) || aside != "Hub Rats (peer) (p3)") Fail("T-368: then to '<name> (peer) (p<slot>)'");
        if (coopslot::StandInAsideName("54-gamedata.base", "Hub Rats", false, &aside) || coopslot::StandInAsideName(coopslot::kLegacyPeerId, "Hub Rats", false, &aside))
            Fail("T-368: a record that is no per-slot stand-in's keeps its name");
    }
}

/* names1: MSG_NAME - the owner's character name for the other game's copy. */
void t_names1_name_wire()
{
    std::vector<char> b;
    const std::string beep("Beep \xC3\xA9");   /* UTF-8 bytes pass through untouched */
    if (!coopname::EncodeName(&b, 0x01020304u, beep) || b.size() != 5 + beep.size()) { Fail("a NAME did not encode to 5 + len bytes"); return; }
    unsigned int uid = 0; std::string name;
    if (coopname::DecodeName(&b[0], b.size(), &uid, &name) != coopname::kNameDecodeOk || uid != 0x01020304u || name != beep)
        Fail("a NAME did not survive the round trip");
    if (coopname::DecodeName(&b[0], b.size() - 1, &uid, &name) != coopname::kNameDecodeTooShort) Fail("a short NAME was accepted");
    if (coopname::DecodeName(&b[0], 4, &uid, &name) != coopname::kNameDecodeTooShort) Fail("a NAME without its length was accepted");
    std::vector<char> e;
    if (!coopname::EncodeName(&e, 7u, std::string()) || e.size() != 5
        || coopname::DecodeName(&e[0], e.size(), &uid, &name) != coopname::kNameDecodeOk || !name.empty() || uid != 7u)
        Fail("an empty NAME did not round-trip");
    const std::string max64(64, 'a'), over(65, 'a');
    std::vector<char> m;
    if (!coopname::EncodeName(&m, 1u, max64) || coopname::DecodeName(&m[0], m.size(), &uid, &name) != coopname::kNameDecodeOk || name != max64)
        Fail("a 64-byte NAME did not round-trip");
    std::vector<char> o;
    if (coopname::EncodeName(&o, 1u, over) || !o.empty()) Fail("a 65-byte NAME was encoded");
    std::vector<char> big(5 + 65, 'a'); big[4] = (char)65;
    if (coopname::DecodeName(&big[0], big.size(), &uid, &name) != coopname::kNameDecodeTooLong) Fail("a NAME above 64 bytes was accepted");
    const std::string split = std::string(63, 'a') + "\xC3\xA9";   /* 65 bytes; the cut must not keep half of the e-acute */
    if (coopname::NameCutUtf8(split.data(), split.size(), 64) != 63) Fail("the UTF-8 cut split a character");
    if (coopname::NameCutUtf8(over.data(), over.size(), 64) != 64) Fail("the ASCII cut is not 64");
    if (coopname::NameCutUtf8(beep.data(), beep.size(), 64) != beep.size()) Fail("a short name was cut");
}

/* recruit1: MSG_HIRE - REQ / OK / NO / DONE round trip and every refusal. */
void t_recruit1_hire_wire()
{
    coophire::HireMsg m; m.kind = coophire::kHireReq; m.reqId = 7; m.uid = 0x0A0B0C0Du; m.hirerUid = 42; m.price = 100; m.joinType = 18; m.faction = "Drifters";
    std::vector<char> b;
    if (!coophire::EncodeHire(&b, m) || b.size() != coophire::kHireFixedSize + 8 + coophire::kHireGenSize) { Fail("a REQ did not encode to 22 + 8 + 4 bytes (M7a A1 [a1b1-t2]: the gen tail)"); return; }
    coophire::HireMsg o;
    if (coophire::DecodeHire(&b[0], b.size(), &o) != coophire::kHireDecodeOk || o.kind != 1 || o.reqId != 7 || o.uid != 0x0A0B0C0Du || o.hirerUid != 42
        || o.price != 100 || o.joinType != 18 || o.faction != "Drifters" || o.taken != 0 || o.joined != 0) Fail("a REQ did not round-trip");
    coophire::HireMsg d; d.kind = coophire::kHireDone; d.uid = 5; d.taken = 1; d.joined = 1; d.joinType = 3; b.clear();
    if (!coophire::EncodeHire(&b, d) || coophire::DecodeHire(&b[0], b.size(), &o) != coophire::kHireDecodeOk || o.kind != 4 || o.taken != 1 || o.joined != 1 || o.joinType != 3 || !o.faction.empty())
        Fail("a DONE did not round-trip");
    if (coophire::DecodeHire(&b[0], coophire::kHireFixedSize - 1, &o) != coophire::kHireDecodeTooShort) Fail("a short HIRE was accepted");
    std::vector<char> bad(b); bad[0] = 5;
    if (coophire::DecodeHire(&bad[0], bad.size(), &o) != coophire::kHireDecodeBadKind) Fail("kind 5 was accepted");
    bad = b; bad[17] = 4;
    if (coophire::DecodeHire(&bad[0], bad.size(), &o) != coophire::kHireDecodeBadField) Fail("join type 4 was accepted");
    bad = b; bad[21] = 3;
    if (coophire::DecodeHire(&bad[0], bad.size(), &o) != coophire::kHireDecodeBadLength) Fail("a name past the end was accepted");
    std::vector<char> e; coophire::HireMsg x = m; x.joinType = 5;
    if (coophire::EncodeHire(&e, x) || !e.empty()) Fail("join type 5 was encoded");
    x = m; x.faction.assign(65, 'a');
    if (coophire::EncodeHire(&e, x) || !e.empty()) Fail("a 65-byte faction name was encoded");
    x = m; x.reason = coophire::kNoReasonMax + 1;
    if (coophire::EncodeHire(&e, x) || !e.empty()) Fail("a reason above the max was encoded");
}

/* P104 fix (protocol 104): MSG_SHOT - a bolt the other game's character fired struck our character's copy there. */
void t_melee_decided_wire()
{
    meleewire::MeleeMsg m;
    m.victimUid = 0x00400002u;
    m.attackerUid = 7u;
    m.cutDir = 3;
    m.comboId = -1;
    m.damages[0] = 1.5f; m.damages[1] = 0.0f; m.damages[2] = 22.25f; m.damages[3] = -0.5f; m.damages[4] = 2.0f; m.damages[5] = 0.25f;
    for (int tf = 0; tf <= 1; ++tf)
    {
        m.techFlag = (unsigned char)tf;
        std::vector<char> b(2, 'y');   /* appended after what is already there */
        if (!meleewire::EncodeMelee(&b, m) || b.size() != 2 + meleewire::kMeleeWireSize) { Fail("a MELEE_DECIDED did not encode to 41 bytes"); return; }
        unsigned int v = 0, a = 0;
        int cd = 0, co = 0;
        float d2 = 0.0f;
        std::memcpy(&v, &b[2], 4);
        std::memcpy(&a, &b[6], 4);
        std::memcpy(&cd, &b[10], 4);
        std::memcpy(&co, &b[14], 4);
        std::memcpy(&d2, &b[2 + 16 + 8], 4);
        if (b[0] != 'y' || v != 0x00400002u || a != 7u || cd != 3 || co != -1 || d2 != 22.25f || (unsigned char)b[2 + 40] != (unsigned char)tf)
            Fail("a MELEE_DECIDED's bytes are not victim | attacker | cutDir | comboId | Damages | techFlag, little-endian");
        meleewire::MeleeMsg d;
        if (meleewire::DecodeMelee(&b[2], b.size() - 2, &d) != meleewire::kMeleeDecodeOk || d.victimUid != m.victimUid
            || d.attackerUid != m.attackerUid || d.cutDir != m.cutDir || d.comboId != m.comboId || d.techFlag != m.techFlag
            || std::memcmp(d.damages, m.damages, sizeof(m.damages)) != 0)
            Fail("a MELEE_DECIDED did not survive the round trip");
        std::vector<char> longer(b.begin() + 2, b.end());
        longer.push_back('z');
        if (meleewire::DecodeMelee(&longer[0], longer.size(), &d) != meleewire::kMeleeDecodeOk) Fail("a longer MELEE_DECIDED was refused");
        for (size_t n = 0; n < meleewire::kMeleeWireSize; ++n)
            if (meleewire::DecodeMelee(&b[2], n, &d) != meleewire::kMeleeDecodeTooShort) { Fail("a short MELEE_DECIDED was accepted"); break; }
    }
    meleewire::MeleeMsg d;
    d.victimUid = 99u;
    if (meleewire::DecodeMelee(0, 41, &d) != meleewire::kMeleeDecodeTooShort) Fail("a MELEE_DECIDED with no bytes was accepted");
    m.techFlag = 0;
    std::vector<char> x;
    if (!meleewire::EncodeMelee(&x, m)) { Fail("a good MELEE_DECIDED did not encode"); return; }
    std::vector<char> y = x; y[40] = 2;
    if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadFlag) Fail("a MELEE_DECIDED with techFlag 2 was accepted");
    y = x; std::memset(&y[0], 0, 4);
    if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadUid) Fail("a MELEE_DECIDED with victim 0 was accepted");
    y = x; std::memset(&y[4], 0, 4);
    if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadUid) Fail("a MELEE_DECIDED with attacker 0 was accepted");
    y = x; std::memcpy(&y[4], &y[0], 4);
    if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadUid) Fail("a MELEE_DECIDED at its own attacker was accepted");
    {
        unsigned int nanBits = 0x7FC00000u, infBits = 0x7F800000u;
        y = x; std::memcpy(&y[16 + 4], &nanBits, 4);
        if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadDamage) Fail("a MELEE_DECIDED with a NaN damage was accepted");
        y = x; std::memcpy(&y[16 + 20], &infBits, 4);
        if (meleewire::DecodeMelee(&y[0], y.size(), &d) != meleewire::kMeleeDecodeBadDamage) Fail("a MELEE_DECIDED with an infinite damage was accepted");
    }
    if (d.victimUid != 99u) Fail("a refused MELEE_DECIDED wrote its output");
    meleewire::MeleeMsg bad = m;
    bad.victimUid = 0;
    std::vector<char> z(1, 'q');
    if (meleewire::EncodeMelee(&z, bad) || z.size() != 1) Fail("a MELEE_DECIDED that would not decode was encoded");
    bad = m; bad.techFlag = 3;
    if (meleewire::EncodeMelee(&z, bad) || z.size() != 1) Fail("a MELEE_DECIDED with techFlag 3 was encoded");
    if (meleewire::kMeleeHitLanded != 2 || meleewire::kMeleeHitBlocked != 5 || meleewire::kMeleeHitNone != 0)
        Fail("the melee hit's return values are not 2 landed / 5 blocked / 0 none (decomp_439150)");
}
/* Which game decides one melee blow: all 16 (attacker ours?, attacker a player?, victim ours?, victim a player?) combinations. A
   player's blow on another game's NPC is sent from the attacker's game and ignored on the NPC's; player-vs-player and every NPC
   attacker keep the victim's game deciding; an unknown (-1) side never changes the rule. */
void t_melee_decided_decide()
{
    using namespace meleewire;
    /* index = attackerMine * 8 + attackerPlayer * 4 + victimMine * 2 + victimPlayer */
    const int want[16] = {
        kMeleeKeep, kMeleeKeep, kMeleeKeep, kMeleeKeep,      /* a copy of an NPC attacks: the victim's game, as today */
        kMeleeKeep, kMeleeKeep, kMeleeIgnore, kMeleeKeep,    /* a copy of a player: on our NPC ignored; on a copy / on our player kept */
        kMeleeKeep, kMeleeKeep, kMeleeKeep, kMeleeKeep,      /* our NPC attacks: the victim's game, as today */
        kMeleeSend, kMeleeKeep, kMeleeKeep, kMeleeKeep };    /* our player: on a copy of an NPC sent; on a copy of a player (PvP) / our own kept */
    for (int i = 0; i < 16; ++i)
    {
        const bool am = (i & 8) != 0, vm = (i & 2) != 0;
        const int ap = (i & 4) ? 1 : 0, vp = (i & 1) ? 1 : 0;
        if (MeleeDecide(am, ap, vm, vp) != want[i]) { Fail("MeleeDecide: a melee blow is decided by the wrong game"); break; }
    }
    for (int i = 0; i < 4; ++i)
    {
        const bool am = (i & 2) != 0, vm = (i & 1) != 0;
        if (MeleeDecide(am, -1, vm, 0) != kMeleeKeep || MeleeDecide(am, 1, vm, -1) != kMeleeKeep || MeleeDecide(am, -1, vm, -1) != kMeleeKeep)
            { Fail("MeleeDecide: an unknown attacker or victim changed the rule"); break; }
    }
    /* THE NPC'S GAME applies a decided blow only for the pair whose own result it ignored */
    for (int i = 0; i < 16; ++i)
    {
        const bool am = (i & 8) != 0, vm = (i & 2) != 0;
        const int ap = (i & 4) ? 1 : 0, vp = (i & 1) ? 1 : 0;
        if (MeleeReceiverApplies(am, ap, vm, vp) != (want[i] == kMeleeIgnore)) { Fail("MeleeReceiverApplies: a decided blow is applied where this game's own result stands"); break; }
    }
    /* FAILS BEFORE: the NPC's game applied every decided blow, so an attacker it had not classified yet was counted twice */
    if (MeleeReceiverApplies(false, -1, true, 0) || MeleeReceiverApplies(false, 1, true, -1) || MeleeReceiverApplies(false, -1, true, -1))
        Fail("MeleeReceiverApplies: an unclassified pair applied a decided blow on top of its own result");
    /* THE ATTACKER'S GAME counts a blow it kept only because a side is not classified yet (it may land nowhere) */
    if (!MeleeKeptUnclassified(true, -1, false, 0) || !MeleeKeptUnclassified(true, 1, false, -1) || !MeleeKeptUnclassified(true, -1, false, -1))
        Fail("MeleeKeptUnclassified: a blow kept for an unclassified side is not counted");
    if (MeleeKeptUnclassified(true, 1, false, 0) || MeleeKeptUnclassified(true, 0, false, -1) || MeleeKeptUnclassified(true, -1, false, 1)
        || MeleeKeptUnclassified(false, -1, true, 0) || MeleeKeptUnclassified(true, -1, true, -1) || MeleeKeptUnclassified(false, -1, false, -1))
        Fail("MeleeKeptUnclassified: a blow that is sent, is an NPC's, is on a player or is not ours on a copy is counted");
}
/* MSG_MELEE_DECIDED waits for its victim's and attacker's copies like MSG_SHOT, and is counted in its own addr group */
void t_melee_decided_paced_and_counted()
{
    meleewire::MeleeMsg m;
    m.victimUid = 1234; m.attackerUid = 5678; m.cutDir = 2; m.comboId = 3; m.techFlag = 1;
    for (int i = 0; i < 6; ++i) m.damages[i] = 1.0f;
    std::vector<char> b;
    if (!meleewire::EncodeMelee(&b, m)) { Fail("melee paced: the message does not encode"); return; }
    std::vector<unsigned int> got;
    /* FAILS BEFORE: 77 named no character, so it was applied before the attacker's copy existed */
    const int n = spawnpace::MessageSubjects(spawnpace::kMsgMeleeDecided, (const unsigned char*)&b[0], b.size(), &got);
    if (n != 2 || got.size() != 2 || got[0] != 1234 || got[1] != 5678) Fail("melee paced: MELEE_DECIDED does not wait for its victim and attacker");
    if (spawnpace::kMsgMeleeDecided != 77 || cooplive::kInnerMeleeDecided != 77) Fail("melee paced: the message number is not 77");
    /* FAILS BEFORE: it was counted with HIT */
    if (cooplive::AddrGroupOf(cooplive::kInnerMeleeDecided) != cooplive::kAddrGroupMelee
        || std::string(cooplive::AddrGroupName(cooplive::kAddrGroupMelee)) != "melee")
        Fail("melee paced: MELEE_DECIDED is not counted in its own addr group named melee");
    if (cooplive::AddrGroupOf(cooplive::kInnerHit) != cooplive::kAddrGroupHit) Fail("melee paced: HIT left the hit addr group");
}
void t_p104_shot_wire()
{
    coopshot::ShotMsg m;
    m.victimUid = 0x00400001u;
    m.shooterUid = 1u;
    m.rec[0] = 12.5f; m.rec[1] = 3.25f; m.rec[2] = 0.75f; m.rec[3] = 69.4f;
    for (int op = 0; op <= 1; ++op)
    {
        m.onPurpose = (op != 0);
        std::vector<char> b(3, 'x');   /* appended after what is already there */
        if (!coopshot::EncodeShot(&b, m) || b.size() != 3 + coopshot::kShotWireSize) { Fail("a SHOT did not encode to 25 bytes"); return; }
        unsigned int v = 0;
        std::memcpy(&v, &b[3], 4);
        if (v != 0x00400001u || b[0] != 'x' || (unsigned char)b[3 + 24] != (unsigned char)op) Fail("a SHOT's bytes are not victim-first / flags-last");
        coopshot::ShotMsg d;
        if (coopshot::DecodeShot(&b[3], b.size() - 3, &d) != coopshot::kShotDecodeOk || d.victimUid != m.victimUid
            || d.shooterUid != m.shooterUid || d.rec[0] != m.rec[0] || d.rec[1] != m.rec[1] || d.rec[2] != m.rec[2]
            || d.rec[3] != m.rec[3] || d.onPurpose != m.onPurpose)
            Fail("a SHOT did not round-trip");
        if (coopshot::DecodeShot(&b[3], coopshot::kShotWireSize - 1, &d) != coopshot::kShotDecodeTooShort) Fail("a 24-byte SHOT was accepted");
    }
    float nanv = 0.0f, infv = 0.0f;
    { const unsigned int qn = 0x7FC00000u, pi = 0x7F800000u; std::memcpy(&nanv, &qn, 4); std::memcpy(&infv, &pi, 4); }
    const float badDmg[4] = { nanv, infv, -1.0f, coopshot::kShotDamageMax * 2.0f };
    for (int i = 0; i < 4; ++i)
    {
        coopshot::ShotMsg x = m;
        x.rec[3] = badDmg[i];
        std::vector<char> e;
        if (coopshot::EncodeShot(&e, x) || !e.empty()) Fail("a SHOT with a bad damage was encoded");
    }
    {
        coopshot::ShotMsg x = m; x.victimUid = 0u;
        std::vector<char> e;
        if (coopshot::EncodeShot(&e, x) || !e.empty()) Fail("a SHOT with victim 0 was encoded");
        x = m; x.shooterUid = 0u;
        if (coopshot::EncodeShot(&e, x) || !e.empty()) Fail("a SHOT with shooter 0 was encoded");
        x = m; x.shooterUid = x.victimUid;
        if (coopshot::EncodeShot(&e, x) || !e.empty()) Fail("a SHOT at its own shooter was encoded");
        if (coopshot::EncodeShot(0, m)) Fail("a SHOT into no buffer was encoded");
    }
    std::vector<char> g;
    m.onPurpose = true;
    if (!coopshot::EncodeShot(&g, m)) { Fail("the base SHOT did not encode"); return; }
    coopshot::ShotMsg d;
    d.victimUid = 7u; d.shooterUid = 7u;
    {
        std::vector<char> x = g; x[24] = 2;
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeBadFlags) Fail("a SHOT with flag bit 1 was accepted");
        x = g; std::memset(&x[0], 0, 4);
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeBadUid) Fail("a SHOT with victim 0 was accepted");
        x = g; std::memcpy(&x[4], &x[0], 4);
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeBadUid) Fail("a SHOT at its own shooter was accepted");
        x = g; std::memcpy(&x[20], &nanv, 4);
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeBadDamage) Fail("a SHOT with a NaN damage was accepted");
        x = g; std::memcpy(&x[20], &infv, 4);
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeBadDamage) Fail("a SHOT with an infinite damage was accepted");
        if (d.victimUid != 7u || d.shooterUid != 7u) Fail("a refused SHOT wrote its output");
        if (coopshot::DecodeShot(0, 25, &d) != coopshot::kShotDecodeTooShort) Fail("a null SHOT was accepted");
        x = g; x.push_back('z');
        if (coopshot::DecodeShot(&x[0], x.size(), &d) != coopshot::kShotDecodeOk || !d.onPurpose) Fail("a longer SHOT was refused");
    }
    float dm[6];
    coopshot::ShotDamages(69.4f, dm);
    if (dm[0] != 0.0f || dm[1] != 0.0f || dm[2] != 69.4f || dm[3] != 0.0f || dm[4] != 2.0f || dm[5] != 0.0f)
        Fail("the Damages are not iShotYou's {0, 0, dmg, 0, 2, 0}");
}

/* T-311: the victim-side reactions the owner plays after a MSG_SHOT wound - iShotYou's own conditions. */
void t_t311_shot_reactions()
{
    const int R = coopshot::kShotReactRemember, U = coopshot::kShotReactUnderRanged, H = coopshot::kShotReactHitReaction;
    if (coopshot::ShotReactions(true, true, 0, 1) != (R | U | H)) Fail("on purpose, AI side: not all three reactions");
    if (coopshot::ShotReactions(true, true, 1, 1) != (R | H)) Fail("combat +0x130 set: underRangedAttack was not skipped");
    if (coopshot::ShotReactions(true, true, -1, 1) != (R | H)) Fail("combat +0x130 unread: underRangedAttack was not skipped");
    if (coopshot::ShotReactions(true, true, 0, 2) != (R | H)) Fail("an attacker of type 2: underRangedAttack was not skipped");
    if (coopshot::ShotReactions(true, true, 0, -1) != (R | H)) Fail("an unread attacker type: underRangedAttack was not skipped");
    if (coopshot::ShotReactions(false, true, 0, 1) != H) Fail("not on purpose: only the hit reaction should run");
    if (coopshot::ShotReactions(false, true, 1, 1) != H) Fail("not on purpose, +0x130 set: only the hit reaction should run");
    if (coopshot::ShotReactions(true, false, 0, 1) != 0) Fail("no copy of the shooter: a reaction was chosen");
    if (coopshot::ShotReactions(false, false, 0, 1) != 0) Fail("no copy, not on purpose: a reaction was chosen");
    /* T-311 fold 1: each underRangedAttack skip has its own reason, and the rule runs it exactly when ShotReactions chooses it */
    if (coopshot::ShotUnderRangedSkip(true, 0, 1) != coopshot::kShotUrRun) Fail("on purpose, +0x130 clear, type 1: a skip reason was given");
    if (coopshot::ShotUnderRangedSkip(false, 0, 1) != coopshot::kShotUrSkipNotOnPurpose) Fail("not on purpose: not the notOnPurpose skip");
    if (coopshot::ShotUnderRangedSkip(true, 1, 1) != coopshot::kShotUrSkipFlag130) Fail("+0x130 set: not the flag130 skip");
    if (coopshot::ShotUnderRangedSkip(true, -1, 1) != coopshot::kShotUrSkipFlagUnread) Fail("+0x130 unread: not the flagUnread skip");
    if (coopshot::ShotUnderRangedSkip(true, 0, 2) != coopshot::kShotUrSkipNotChar) Fail("attacker type 2: not the notChar skip");
    if (coopshot::ShotUnderRangedSkip(true, 0, -1) != coopshot::kShotUrSkipNotChar) Fail("attacker type unread: not the notChar skip");
    if (coopshot::ShotUnderRangedSkip(true, 1, 2) != coopshot::kShotUrSkipFlag130) Fail("+0x130 set and type 2: flag130 is not checked first");
    for (int op = 0; op < 2; ++op)
        for (int fl = -1; fl <= 1; ++fl)
            for (int ty = -1; ty <= 2; ++ty)
            {
                const bool runs = (coopshot::ShotReactions(op != 0, true, fl, ty) & U) != 0;
                if (runs != (coopshot::ShotUnderRangedSkip(op != 0, fl, ty) == coopshot::kShotUrRun))
                    Fail("ShotUnderRangedSkip disagrees with ShotReactions on when underRangedAttack runs");
            }
}

/* slave1: MSG_SLAVE - the owner's SlaveStateEnum for the other game's copy. */
void t_slave1_slave_wire()
{
    for (int s = 0; s <= 3; ++s)
    {
        std::vector<char> b;
        if (!coopslave::EncodeSlave(&b, 0x0A0B0C0Du, s, 0x00400001u) || b.size() != 9) { Fail("a SLAVE did not encode to 9 bytes"); return; }
        unsigned int uid = 0, own = 0; int st = -1;
        if (coopslave::DecodeSlave(&b[0], b.size(), &uid, &st, &own) != coopslave::kSlaveDecodeOk || uid != 0x0A0B0C0Du || st != s
            || own != 0x00400001u)
            Fail("a SLAVE did not round-trip");
        if (coopslave::DecodeSlave(&b[0], 8, &uid, &st, &own) != coopslave::kSlaveDecodeTooShort) Fail("a SLAVE without its whole ownerUid was accepted");
        if (coopslave::DecodeSlave(&b[0], 5, &uid, &st, &own) != coopslave::kSlaveDecodeTooShort) Fail("a protocol-100 SLAVE (5 bytes) was accepted");
    }
    std::vector<char> o;
    if (coopslave::EncodeSlave(&o, 1u, 4, 0u) || !o.empty()) Fail("state 4 was encoded");
    if (coopslave::EncodeSlave(&o, 1u, -1, 0u) || !o.empty()) Fail("state -1 was encoded");
    const char bad[9] = { 1, 0, 0, 0, 4, 0, 0, 0, 0 };
    unsigned int uid = 0, own = 7; int st = -1;
    if (coopslave::DecodeSlave(bad, 9, &uid, &st, &own) != coopslave::kSlaveDecodeBadState) Fail("a SLAVE with state 4 was accepted");
    if (st != -1 || uid != 0 || own != 7) Fail("a refused SLAVE wrote its fields");
    std::vector<char> u;
    if (!coopslave::EncodeSlave(&u, 5u, 1, coopslave::kSlaveOwnerUnknown)) Fail("an unknown owner did not encode");
    else if (coopslave::DecodeSlave(&u[0], u.size(), &uid, &st, &own) != coopslave::kSlaveDecodeOk || own != 0xFFFFFFFFu) Fail("the unknown owner did not round-trip");
}

/* P11 f6: the owner's gate for a victim already a slave with an owner (coopcapture::CapSlaveGate) and the captor's send order. */
void t_p11f6_slave_gate()
{
    const int R = coopcapture::kCapGateRefuse, P = coopcapture::kCapGateProcess, C = coopcapture::kCapGateRechain;
    if (coopcapture::CapSlaveGate(false, true, 1, 1) != P) Fail("a 182 from the victim's own slaver is not processed");
    if (coopcapture::CapSlaveGate(false, true, 0, 0) != P) Fail("a 182 from the victim's own slaver (unshackled) is not processed");
    if (coopcapture::CapSlaveGate(false, false, 0, 0) != R) Fail("a 182 from another slaver is not refused");
    if (coopcapture::CapSlaveGate(true, true, 0, 0) != C) Fail("a CHAIN on the slaver's own unchained, unshackled slave is not applied");
    if (coopcapture::CapSlaveGate(true, true, 1, 0) != C) Fail("a CHAIN on the slaver's own chained but unshackled slave is not applied");
    if (coopcapture::CapSlaveGate(true, true, 0, 1) != C) Fail("a CHAIN on the slaver's own shackled but unchained slave is not applied");
    if (coopcapture::CapSlaveGate(true, true, -1, 0) != C) Fail("a CHAIN on an unshackled slave (byte unreadable) is not applied");
    if (coopcapture::CapSlaveGate(true, true, 0, -1) != C) Fail("a CHAIN on an unchained slave (shackles unreadable) is not applied");
    if (coopcapture::CapSlaveGate(true, true, 1, 1) != R) Fail("a CHAIN on the slaver's own chained, shackled slave is not refused");
    if (coopcapture::CapSlaveGate(true, true, -1, 1) != R) Fail("a CHAIN on a shackled slave (byte unreadable) is not refused");
    if (coopcapture::CapSlaveGate(true, true, 1, -1) != R) Fail("a CHAIN on a chained slave (shackles unreadable) is not refused");
    if (coopcapture::CapSlaveGate(true, true, -1, -1) != R) Fail("a CHAIN on a slave with nothing readable is not refused");
    if (coopcapture::CapSlaveGate(true, false, 0, 0) != R) Fail("a CHAIN on another slaver's slave is not refused");
    int idx[4] = { 0, 1, 2, 3 }; long seq[4] = { 7, 5, 6, 4 };
    if (coopcapture::CapOrderBySeq(idx, seq, 4) != 1 || idx[0] != 3 || idx[1] != 1 || idx[2] != 2 || idx[3] != 0)
        Fail("the ready records are not sent in the order they became ready");
    int id2[3] = { 5, 2, 9 }; long sq2[3] = { 1, 2, 3 };
    if (coopcapture::CapOrderBySeq(id2, sq2, 3) != 0 || id2[0] != 5 || id2[1] != 2 || id2[2] != 9) Fail("an ordered set was reordered");
    int id3[2] = { 0, 1 }; long sq3[2] = { (-2147483647L - 1), 2147483647L };   /* slot 0 became ready AFTER the counter wrapped */
    if (coopcapture::CapOrderBySeq(id3, sq3, 2) != 1 || id3[0] != 1 || id3[1] != 0) Fail("the send order is not wrap-safe");
    if (coopcapture::CapOrderBySeq(id3, sq3, 0) != 0) Fail("an empty set reports a reorder");
    /* M7b (T750): the captor's block after an answer, and when it lets a processing request through */
    const int BN = coopcapture::kCapBlockNone, BS = coopcapture::kCapBlockSet, BR = coopcapture::kCapBlockRetarget;
    if (coopcapture::CapBlockOnAnswer(false, coopcapture::kCapApplied, 0, false) != BS) Fail("an applied processing answer set no block");
    if (coopcapture::CapBlockOnAnswer(false, coopcapture::kCapAlreadySlave, 1, true) != BS) Fail("an already-a-slave answer set no block");
    if (coopcapture::CapBlockOnAnswer(false, coopcapture::kCapNotDown, 0, false) != BN) Fail("a refused processing answer set a block");
    if (coopcapture::CapBlockOnAnswer(true, coopcapture::kCapApplied, 1, false) != BN) Fail("a CHAIN answer created a block");
    if (coopcapture::CapBlockOnAnswer(true, coopcapture::kCapApplied, 1, true) != BR) Fail("an applied CHAIN left the block on the earlier state");
    if (coopcapture::CapBlockOnAnswer(true, coopcapture::kCapApplied, -1, true) != BN) Fail("a CHAIN with no known state moved the block");
    if (coopcapture::CapBlockOnAnswer(true, coopcapture::kCapAlreadySlave, 1, true) != BN) Fail("a refused CHAIN moved the block");
    int blockState = 0;   /* T750: the 181 answered at state 0; the CHAIN answered at 1; our copy shows 1; the 182 comes 5 s later */
    if (coopcapture::CapBlockOnAnswer(true, coopcapture::kCapApplied, 1, true) == BR) blockState = 1;
    if (!coopcapture::CapBlockClears(blockState, 1, 5000, 30000)) Fail("the 182 after the cuff is held back (T750)");
    if (coopcapture::CapBlockClears(0, 1, 5000, 30000)) Fail("a block let a request through while our copy shows another state");
    if (!coopcapture::CapBlockClears(0, 1, 30000, 30000)) Fail("a block outlived its window");
    if (coopcapture::CapBlockClears(-1, -1, 29999, 30000)) Fail("a block with no known state cleared inside its window");
    if (!coopcapture::CapBlockClears(1, 1, 0, 30000)) Fail("a block held a request back after our copy showed its state");
    /* M7b: a held request - waits (its record stays ready), then goes when the block clears; abandoned with the copy gone or no link */
    const int HS = coopcapture::kCapHeldSend, HW = coopcapture::kCapHeldWait, HA = coopcapture::kCapHeldAbandon;
    if (coopcapture::CapHeldDecide(1, 0, 2000, 30000, true, true) != HW) Fail("a held request was not kept waiting");
    if (coopcapture::CapHeldDecide(1, 0, 2016, 30000, true, true) != HW) Fail("a held request was dropped on the next frame");
    if (coopcapture::CapHeldDecide(1, 1, 2032, 30000, true, true) != HS) Fail("a held request did not go when our copy showed the state");
    if (coopcapture::CapHeldDecide(1, 0, 30000, 30000, true, true) != HS) Fail("a held request did not go when the window passed");
    if (coopcapture::CapHeldDecide(1, 0, 2000, 30000, false, true) != HA) Fail("a held request for a gone copy was not abandoned");
    if (coopcapture::CapHeldDecide(1, 1, 2000, 30000, true, false) != HA) Fail("a held request with no link was not abandoned");
    if (!coopcapture::CapAbandonUndoesShave(coopcapture::kCapFlagShave | coopcapture::kCapFlagDress)) Fail("an abandoned shave was not undone");
    if (coopcapture::CapAbandonUndoesShave(coopcapture::kCapFlagStrip | coopcapture::kCapFlagDress)) Fail("an abandoned request with no shave undid one");
}

/* P11 f5: MSG_CAPTURE flag 32 (CHAIN, task 166) and MSG_CAPTURE_DONE's chain tail (chainedNow, shackles sid / section, locked). */
void t_p11f5_chain_wire()
{
    coopcapture::CaptureReq q;
    q.reqId = 11; q.victimUid = 0x00400002u; q.slaverUid = 249; q.taskType = 166; q.flags = coopcapture::kCapFlagChain;
    std::vector<char> b;
    if (!coopcapture::EncodeCapture(&b, q) || b.size() != 18) { Fail("a CHAIN CAPTURE is not 18 bytes"); return; }
    coopcapture::CaptureReq r;
    if (coopcapture::DecodeCapture(&b[0], b.size(), &r) != coopcapture::kCapDecodeOk || r.flags != 32 || r.taskType != 166
        || r.slaverUid != 249 || r.wantState != -1 || !r.dress.empty() || !r.strip.empty())
        Fail("a CHAIN CAPTURE did not round-trip");
    coopcapture::CaptureReq all = q; all.flags = 63;
    std::vector<char> ab;
    if (!coopcapture::EncodeCapture(&ab, all) || coopcapture::DecodeCapture(&ab[0], ab.size(), &r) != coopcapture::kCapDecodeOk || r.flags != 63)
        Fail("flags 63 did not round-trip");
    all.flags = 64;
    std::vector<char> e;
    if (coopcapture::EncodeCapture(&e, all) || !e.empty()) Fail("flags 64 were encoded");

    coopcapture::CaptureDone d;
    d.reqId = 11; d.victimUid = 0x00400002u; d.result = coopcapture::kCapApplied; d.stateNow = 1; d.ownerUidNow = 249; d.shavedNow = 0;
    d.chainedNow = 1; d.shacklesSid = "SHACKLES"; d.shacklesSection = "boots"; d.shacklesLocked = 1;
    std::vector<char> db;
    if (!coopcapture::EncodeCaptureDone(&db, d) || db.size() != 34) { Fail("a chained CAPTURE_DONE is not 34 bytes"); return; }
    coopcapture::CaptureDone x;
    if (coopcapture::DecodeCaptureDone(&db[0], db.size(), &x) != coopcapture::kCapDecodeOk || x.chainedNow != 1 || x.shacklesSid != "SHACKLES"
        || x.shacklesSection != "boots" || x.shacklesLocked != 1 || x.stateNow != 1 || x.ownerUidNow != 249 || x.shavedNow != 0)
        Fail("a chained CAPTURE_DONE did not round-trip");
    for (size_t cut = 0; cut < db.size(); ++cut)
    {
        coopcapture::CaptureDone t;
        if (coopcapture::DecodeCaptureDone(cut == 0 ? 0 : &db[0], cut, &t) == coopcapture::kCapDecodeOk) { Fail("a truncated chained CAPTURE_DONE was accepted"); break; }
    }
    std::vector<char> tr = db; tr.push_back(0);
    if (coopcapture::DecodeCaptureDone(&tr[0], tr.size(), &x) != coopcapture::kCapDecodeTrailing) Fail("a chained CAPTURE_DONE with a trailing byte was accepted");
    coopcapture::CaptureDone bad = d; bad.chainedNow = 2;
    std::vector<char> be;
    if (coopcapture::EncodeCaptureDone(&be, bad) || !be.empty()) Fail("chainedNow 2 was encoded");
    bad = d; bad.shacklesLocked = 2;
    if (coopcapture::EncodeCaptureDone(&be, bad) || !be.empty()) Fail("shacklesLocked 2 was encoded");
    bad = d; bad.shacklesSid = std::string(49, 'x');
    if (coopcapture::EncodeCaptureDone(&be, bad) || !be.empty()) Fail("a 49-byte shackles sid was encoded");
    bad = d; bad.shacklesSection = std::string(33, 'x');
    if (coopcapture::EncodeCaptureDone(&be, bad) || !be.empty()) Fail("a 33-byte shackles section was encoded");
    coopcapture::CaptureDone none;
    none.reqId = 12; none.victimUid = 5;
    std::vector<char> nb;
    if (!coopcapture::EncodeCaptureDone(&nb, none) || nb.size() != 21) { Fail("an empty CAPTURE_DONE is not 21 bytes (chain)"); return; }
    if (coopcapture::DecodeCaptureDone(&nb[0], nb.size(), &x) != coopcapture::kCapDecodeOk || x.chainedNow != -1 || !x.shacklesSid.empty()
        || !x.shacklesSection.empty() || x.shacklesLocked != -1)
        Fail("an empty chain tail did not round-trip as unreadable / none");
    std::vector<char> c2 = nb; c2[17] = (char)2;   /* chainedNow 2 */
    coopcapture::CaptureDone keep; keep.reqId = 99;
    if (coopcapture::DecodeCaptureDone(&c2[0], c2.size(), &keep) != coopcapture::kCapDecodeBadValue) Fail("chainedNow 2 was accepted");
    std::vector<char> l2 = nb; l2[20] = (char)2;   /* shacklesLocked 2 */
    if (coopcapture::DecodeCaptureDone(&l2[0], l2.size(), &keep) != coopcapture::kCapDecodeBadValue) Fail("shacklesLocked 2 was accepted");
    std::vector<char> s49 = nb; s49[18] = (char)49; s49.insert(s49.begin() + 19, 49, 'x');   /* a 49-byte sid */
    if (coopcapture::DecodeCaptureDone(&s49[0], s49.size(), &keep) != coopcapture::kCapDecodeBadValue) Fail("a 49-byte shackles sid was accepted");
    if (keep.reqId != 99) Fail("a refused chained CAPTURE_DONE wrote its fields");
}

/* P11: MSG_CAPTURE / MSG_CAPTURE_DONE (src/common/capturewire.h) - round trips, caps, truncation, trailing bytes. */
void t_p11_capture_wire()
{
    coopcapture::CaptureReq q;
    q.reqId = 7; q.victimUid = 0x00400001u; q.slaverUid = 248; q.taskType = 182; q.flags = 31; q.wantState = 1;
    q.dress.push_back("shackles-sid"); q.dress.push_back("rag-sid");
    coopcapture::CapStripRow w; w.section = "hip"; w.x = 0; w.y = -2; w.sid = "katana"; w.qty = 1; q.strip.push_back(w);
    w.section = "backpack_content"; w.x = 5; w.y = 3; w.sid = "bread"; w.qty = 12; q.strip.push_back(w);
    std::vector<char> b;
    if (!coopcapture::EncodeCapture(&b, q)) { Fail("a CAPTURE did not encode"); return; }
    coopcapture::CaptureReq r;
    if (coopcapture::DecodeCapture(&b[0], b.size(), &r) != coopcapture::kCapDecodeOk) { Fail("a CAPTURE did not decode"); return; }
    if (r.reqId != 7 || r.victimUid != 0x00400001u || r.slaverUid != 248 || r.taskType != 182 || r.flags != 31 || r.wantState != 1
        || r.dress.size() != 2 || r.dress[1] != "rag-sid" || r.strip.size() != 2 || r.strip[0].y != -2 || r.strip[1].section != "backpack_content"
        || r.strip[1].qty != 12 || r.strip[0].sid != "katana")
        Fail("a CAPTURE did not round-trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
    {
        coopcapture::CaptureReq t;
        if (coopcapture::DecodeCapture(cut == 0 ? 0 : &b[0], cut, &t) == coopcapture::kCapDecodeOk) { Fail("a truncated CAPTURE was accepted"); break; }
    }
    std::vector<char> tr = b; tr.push_back(0);
    if (coopcapture::DecodeCapture(&tr[0], tr.size(), &r) != coopcapture::kCapDecodeTrailing) Fail("a CAPTURE with a trailing byte was accepted");
    coopcapture::CaptureReq big = q;
    for (int i = 0; i < 33; ++i) big.strip.push_back(w);
    std::vector<char> e;
    if (coopcapture::EncodeCapture(&e, big) || !e.empty()) Fail("33+ strip rows were encoded");
    big = q; big.dress.push_back("a"); big.dress.push_back("b"); big.dress.push_back("c");
    if (coopcapture::EncodeCapture(&e, big) || !e.empty()) Fail("5 dress sids were encoded");
    big = q; big.strip[0].sid = std::string(49, 'x');
    if (coopcapture::EncodeCapture(&e, big) || !e.empty()) Fail("a 49-byte sid was encoded");
    big = q; big.wantState = 4;
    if (coopcapture::EncodeCapture(&e, big) || !e.empty()) Fail("want state 4 was encoded");
    coopcapture::CaptureReq none = q; none.wantState = -1; none.dress.clear(); none.strip.clear();
    std::vector<char> nb;
    if (!coopcapture::EncodeCapture(&nb, none) || nb.size() != 18) Fail("an empty CAPTURE is not 18 bytes");
    else if (coopcapture::DecodeCapture(&nb[0], nb.size(), &r) != coopcapture::kCapDecodeOk || r.wantState != -1) Fail("want state 'none' did not round-trip");
    std::vector<char> bf = nb; bf[14] = (char)0x40;   /* flags 64 (P11 f5: 32 is chain) */
    if (coopcapture::DecodeCapture(&bf[0], bf.size(), &r) != coopcapture::kCapDecodeBadValue) Fail("flags 64 were accepted");

    coopcapture::CaptureDone d;
    d.reqId = 7; d.victimUid = 0x00400001u; d.result = coopcapture::kCapApplied; d.dressAdded = 2; d.stateNow = 1; d.ownerUidNow = 248; d.shavedNow = 1;
    coopcapture::CapDoneRow row; row.outcome = coopcapture::kCapRowTaken;
    row.item.baseSid = "katana"; row.item.materialSid = "steel"; row.item.quality = 0.75f; row.item.charges = 0.0f;
    row.item.level = 3; row.item.unique = 0; row.item.functionKind = 2; row.item.quantity = 1;
    d.rows.push_back(row);
    coopcapture::CapDoneRow gone; gone.outcome = coopcapture::kCapRowGone; d.rows.push_back(gone);
    std::vector<char> db;
    if (!coopcapture::EncodeCaptureDone(&db, d)) { Fail("a CAPTURE_DONE did not encode"); return; }
    coopcapture::CaptureDone x;
    if (coopcapture::DecodeCaptureDone(&db[0], db.size(), &x) != coopcapture::kCapDecodeOk) { Fail("a CAPTURE_DONE did not decode"); return; }
    if (x.reqId != 7 || x.victimUid != 0x00400001u || x.result != 0 || x.rows.size() != 2 || x.rows[0].item.baseSid != "katana"
        || x.rows[0].item.materialSid != "steel" || x.rows[0].item.quality != 0.75f || x.rows[0].item.level != 3 || x.rows[0].item.functionKind != 2
        || x.rows[1].outcome != coopcapture::kCapRowGone || x.dressAdded != 2 || x.stateNow != 1 || x.ownerUidNow != 248)
        Fail("a CAPTURE_DONE did not round-trip");
    if (x.shavedNow != 1) Fail("shavedNow 1 did not round-trip");   /* P11 f3 */
    for (size_t cut = 0; cut < db.size(); ++cut)
    {
        coopcapture::CaptureDone t;
        if (coopcapture::DecodeCaptureDone(cut == 0 ? 0 : &db[0], cut, &t) == coopcapture::kCapDecodeOk) { Fail("a truncated CAPTURE_DONE was accepted"); break; }
    }
    coopcapture::CaptureDone bad = d; bad.result = 7;   /* P11 f3: 6 is already-a-slave */
    std::vector<char> be;
    if (coopcapture::EncodeCaptureDone(&be, bad) || !be.empty()) Fail("result 7 was encoded");
    std::vector<char> bd = db; bd[8] = (char)9;   /* result 9 */
    coopcapture::CaptureDone keep; keep.reqId = 99;
    if (coopcapture::DecodeCaptureDone(&bd[0], bd.size(), &keep) != coopcapture::kCapDecodeBadValue) Fail("result 9 was accepted");
    if (keep.reqId != 99) Fail("a refused CAPTURE_DONE wrote its fields");
    coopcapture::CaptureDone un = d; un.stateNow = -1; un.rows.clear(); un.shavedNow = -1;
    std::vector<char> ub;
    if (!coopcapture::EncodeCaptureDone(&ub, un) || ub.size() != 21) Fail("an empty CAPTURE_DONE is not 21 bytes");   /* P11 f5: + chain tail 4 */
    else if (coopcapture::DecodeCaptureDone(&ub[0], ub.size(), &x) != coopcapture::kCapDecodeOk || x.stateNow != -1) Fail("stateNow 'unreadable' did not round-trip");
    /* P11 f3: shavedNow 'unreadable' / out of range, result 6, row outcome 3, MSG_CAPTURE_PLACED */
    else if (x.shavedNow != -1) Fail("shavedNow 'unreadable' did not round-trip");
    if (ub.size() == 21)
    {
        std::vector<char> us = ub; us[16] = (char)2;
        coopcapture::CaptureDone sx;
        if (coopcapture::DecodeCaptureDone(&us[0], us.size(), &sx) != coopcapture::kCapDecodeBadValue) Fail("shavedNow 2 was accepted");
    }
    {
        coopcapture::CaptureDone six = un; six.result = coopcapture::kCapAlreadySlave;
        coopcapture::CapDoneRow rf; rf.outcome = coopcapture::kCapRowRefused; six.rows.push_back(rf);
        std::vector<char> sb;
        coopcapture::CaptureDone so;
        if (!coopcapture::EncodeCaptureDone(&sb, six) || coopcapture::DecodeCaptureDone(&sb[0], sb.size(), &so) != coopcapture::kCapDecodeOk
            || so.result != 6 || so.rows.size() != 1 || so.rows[0].outcome != 3) Fail("result 6 / row outcome 3 did not round-trip");
    }
    coopcapture::CapturePlaced pl; pl.reqId = 7; pl.victimUid = 0x00400001u; pl.nRows = 2; pl.placedMask = 1u;
    std::vector<char> pb;
    if (!coopcapture::EncodeCapturePlaced(&pb, pl) || pb.size() != 13) Fail("a CAPTURE_PLACED is not 13 bytes");
    else
    {
        coopcapture::CapturePlaced po;
        if (coopcapture::DecodeCapturePlaced(&pb[0], pb.size(), &po) != coopcapture::kCapDecodeOk || po.reqId != 7 || po.victimUid != 0x00400001u
            || po.nRows != 2 || po.placedMask != 1u) Fail("a CAPTURE_PLACED did not round-trip");
        for (size_t cut = 0; cut < pb.size(); ++cut)
        {
            coopcapture::CapturePlaced t;
            if (coopcapture::DecodeCapturePlaced(cut == 0 ? 0 : &pb[0], cut, &t) == coopcapture::kCapDecodeOk) { Fail("a truncated CAPTURE_PLACED was accepted"); break; }
        }
        std::vector<char> pt = pb; pt.push_back(0);
        if (coopcapture::DecodeCapturePlaced(&pt[0], pt.size(), &po) != coopcapture::kCapDecodeTrailing) Fail("a CAPTURE_PLACED with a trailing byte was accepted");
        std::vector<char> pm = pb; pm[9] = (char)4;   /* bit 2 with 2 rows */
        coopcapture::CapturePlaced keep; keep.reqId = 99;
        if (coopcapture::DecodeCapturePlaced(&pm[0], pm.size(), &keep) != coopcapture::kCapDecodeBadValue || keep.reqId != 99) Fail("a placed bit beyond the rows was accepted");
    }
    coopcapture::CapturePlaced hi = pl; hi.placedMask = 4u;
    std::vector<char> hb;
    if (coopcapture::EncodeCapturePlaced(&hb, hi) || !hb.empty()) Fail("a placed bit beyond the rows was encoded");
    hi.placedMask = 0xFFFFFFFFu; hi.nRows = 32;
    if (!coopcapture::EncodeCapturePlaced(&hb, hi) || hb.size() != 13) Fail("32 placed rows did not encode");
    hi.nRows = 33; hb.clear();
    if (coopcapture::EncodeCapturePlaced(&hb, hi) || !hb.empty()) Fail("33 rows were encoded");
}

/* loot2b: RESEARCH_BOX round trip, truncation, first writer wins, and the research_boxes.txt lines (incl. the -1 marker). */
/* stand1: the per-slot stand-in names and the N-party zone owner swap (src/common/slotwire.h). Fails before: none of it existed -
   every other player was one "coop-peer", and two senders both named "Nameless" folded into one key (B19). */
void t_stand1_slot_wire()
{
    if (coopslot::StandInId(1) != "coop-p1" || coopslot::StandInId(1023) != "coop-p1023" || coopslot::StandInId(0) != "coop-p0") Fail("StandInId");
    if (coopslot::StandInSlotOfId("coop-p7") != 7 || coopslot::StandInSlotOfId("coop-peer") != -1 || coopslot::StandInSlotOfId("coop-p") != -1
        || coopslot::StandInSlotOfId("coop-p01") != -1 || coopslot::StandInSlotOfId("coop-p1024") != -1 || coopslot::StandInSlotOfId("204-gamedata.base") != -1)
        Fail("StandInSlotOfId");
    int n = -2; std::string name; bool has = false;
    if (!coopslot::ParseSlotWire("@slot:3:Name: with colon", &n, &name, &has) || n != 3 || name != "Name: with colon" || !has) Fail("parse with a name");
    if (!coopslot::ParseSlotWire("@slot:0", &n, &name, &has) || n != 0 || !name.empty() || has) Fail("parse bare");
    if (!coopslot::ParseSlotWire("@slot:?:Nameless", &n, &name, &has) || n != -1 || name != "Nameless" || !has) Fail("parse no slot");
    if (coopslot::ParseSlotWire("@slot:x1", &n, 0, 0) || coopslot::ParseSlotWire("@slot:", &n, 0, 0) || coopslot::ParseSlotWire("@player:Nameless", &n, 0, 0)
        || coopslot::ParseSlotWire("@peer", &n, 0, 0) || coopslot::ParseSlotWire("@slo", &n, 0, 0)) Fail("parse refuses");
    if (coopslot::SlotWire(2, "Nameless") != "@slot:2:Nameless" || coopslot::SlotWire(2, "") != "@slot:2" || coopslot::SlotWire(-1, "A") != "@slot:?:A") Fail("SlotWire");
    if (coopslot::SlotWireKey("@slot:2:Renamed") != "@slot:2" || coopslot::SlotWireKey("45-gamedata.base") != "45-gamedata.base") Fail("SlotWireKey");
    if (coopslot::SlotWireKey(coopslot::SlotWire(1, "Nameless")) == coopslot::SlotWireKey(coopslot::SlotWire(2, "Nameless"))) Fail("two slots with one name must be two keys");
    const std::string mine("204-gamedata.base"); std::string out;
    /* the reader is slot 0; the record was written by slot 2 */
    if (coopslot::ZoneOwnerSwap(mine, mine, 0, 2, &out) != coopslot::kSwapToStandIn || out != "coop-p2") Fail("the writer's own -> coop-p<writer>");
    out.clear(); if (coopslot::ZoneOwnerSwap("coop-p0", mine, 0, 2, &out) != coopslot::kSwapToMine || out != mine) Fail("coop-p<me> -> mine");
    out.clear(); if (coopslot::ZoneOwnerSwap("coop-p1", mine, 0, 2, &out) != coopslot::kSwapNone || !out.empty()) Fail("a third player's stand-in stays");
    out.clear(); if (coopslot::ZoneOwnerSwap("coop-peer", mine, 0, 2, &out) != coopslot::kSwapLegacyToMine || out != mine) Fail("legacy coop-peer -> mine");
    if (coopslot::ZoneOwnerSwap(mine, mine, 0, -1, &out) != coopslot::kSwapNoWriter || coopslot::ZoneOwnerSwap(mine, mine, 2, 2, &out) != coopslot::kSwapNoWriter) Fail("no writer slot");
    if (coopslot::ZoneOwnerSwap("45-gamedata.base", mine, 0, 2, &out) != coopslot::kSwapNone) Fail("an NPC owner stays");
    if (coopslot::ZoneOwnerSwap("coop-p0", mine, -1, 1, &out) != coopslot::kSwapNone) Fail("with no slot of mine, no coop-p<n> is claimed as mine");
    /* the two-player round trip: slot 1 holds its own base (mine on 1) and slot 0's (coop-p0 on 1); slot 0 reads it */
    std::string a, b;
    if (coopslot::ZoneOwnerSwap(mine, mine, 0, 1, &a) != coopslot::kSwapToStandIn || a != "coop-p1"
        || coopslot::ZoneOwnerSwap("coop-p0", mine, 0, 1, &b) != coopslot::kSwapToMine || b != mine) Fail("two-player round trip");
    /* stand1 fold 4c: in my OWN old record `coop-peer` is the other player's building - never remapped to mine */
    out.clear(); if (coopslot::ZoneOwnerSwap("coop-peer", mine, 0, 0, &out) != coopslot::kSwapNone || !out.empty()) Fail("own old record: coop-peer stays");
    if (coopslot::ZoneOwnerSwap("coop-peer", mine, 0, -1, &out) != coopslot::kSwapNone) Fail("unknown writer: coop-peer stays");
    /* stand1 fold 4b: no swap while MY slot is unknown */
    if (coopslot::ZoneSwapReady(-1, 2, true)) Fail("my slot unknown -> not ready");
    if (!coopslot::ZoneSwapReady(0, 2, true) || coopslot::ZoneSwapReady(0, 2, false) || coopslot::ZoneSwapReady(0, -1, true) || !coopslot::ZoneSwapReady(0, 0, false)) Fail("ZoneSwapReady");
    /* stand1 fold 1d: a name with no slot is held */
    if (!coopslot::WireLacksSlot("@slot:?:Nameless") || !coopslot::WireLacksSlot("@slot:?") || coopslot::WireLacksSlot("@slot:1:Nameless") || coopslot::WireLacksSlot("Nameless")) Fail("WireLacksSlot");
}

/* building ownership with three or more players (src/common/slotwire.h): the one player-number lookup, the swap naming the same
   player on every game, the area file's owner scan, the area read's owner gate, the stand-in cap, the protocol-67 rule */
void P106PushStr(std::string* f, const std::string& s)
{
    const unsigned int n = (unsigned int)s.size();
    f->push_back((char)(n & 0xFF)); f->push_back((char)((n >> 8) & 0xFF)); f->push_back((char)((n >> 16) & 0xFF)); f->push_back((char)((n >> 24) & 0xFF));
    *f += s;
}
void t_p106_owner_lookup()
{
    const std::string sh("204-gamedata.base");
    if (coopslot::OwnerSlotOfId(sh, sh, 3) != 3) Fail("p106: the shared id is the viewer's own player");
    if (coopslot::OwnerSlotOfId(sh, sh, -1) != coopslot::kOwnerNoViewer || coopslot::OwnerSlotOfId(sh, sh, 1024) != coopslot::kOwnerNoViewer) Fail("p106: the shared id on a game with no number");
    if (coopslot::OwnerSlotOfId("coop-p7", sh, 0) != 7 || coopslot::OwnerSlotOfId("coop-p1023", sh, 0) != 1023 || coopslot::OwnerSlotOfId("coop-p0", sh, 0) != 0) Fail("p106: coop-p<n> is player n");
    if (coopslot::OwnerSlotOfId("coop-peer", sh, 0) != coopslot::kOwnerLegacy) Fail("p106: coop-peer is the legacy owner");
    if (coopslot::OwnerSlotOfId("45-gamedata.base", sh, 0) != coopslot::kOwnerWorld || coopslot::OwnerSlotOfId("", sh, 0) != coopslot::kOwnerWorld
        || coopslot::OwnerSlotOfId("coop-p07", sh, 0) != coopslot::kOwnerWorld || coopslot::OwnerSlotOfId("coop-p1024", sh, 0) != coopslot::kOwnerWorld) Fail("p106: a world owner is nobody's");
    if (coopslot::OwnerIdForSlot(2, sh, 2) != sh || coopslot::OwnerIdForSlot(7, sh, 2) != "coop-p7" || !coopslot::OwnerIdForSlot(-1, sh, 2).empty()
        || !coopslot::OwnerIdForSlot(1024, sh, 2).empty() || !coopslot::OwnerIdForSlot(7, sh, -1).empty()) Fail("p106: OwnerIdForSlot");
    /* every number a 3-player world uses, written by player 1 and read by player 0: the swap names the same player on the reader */
    for (int s = 0; s < 3; ++s)
    {
        const std::string onWriter = coopslot::OwnerIdForSlot(s, sh, 1);
        if (coopslot::OwnerSlotOfId(onWriter, sh, 1) != s) Fail("p106: round trip on the writer");
        std::string to; const int k = coopslot::ZoneOwnerSwapN(onWriter, sh, 0, 1, true, &to);
        const std::string onReader = coopslot::OwnerIdForSlot(s, sh, 0);
        if ((k == coopslot::kSwapNone ? onWriter : to) != onReader) Fail("p106: the swap does not name the same player on the reader");
    }
    /* the two-player swap is unchanged: writer's own -> its stand-in, mine -> mine, my own record untouched */
    std::string out;
    if (coopslot::ZoneOwnerSwapN(sh, sh, 0, 1, false, &out) != coopslot::kSwapToStandIn || out != "coop-p1") Fail("p106: the writer's own building -> its stand-in");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-p0", sh, 0, 1, false, &out) != coopslot::kSwapToMine || out != sh) Fail("p106: my building in the writer's record -> mine");
    out.clear(); if (coopslot::ZoneOwnerSwapN(sh, sh, 1, 1, false, &out) != coopslot::kSwapNoWriter || !out.empty()) Fail("p106: my own record is not swapped");
    out.clear(); if (coopslot::ZoneOwnerSwapN(sh, sh, 0, -1, false, &out) != coopslot::kSwapNoWriter) Fail("p106: no writer number - not swapped");
    /* a third player's building stays under its stand-in (it needs one here - the scan below) */
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-p2", sh, 0, 1, true, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106: a third player's owner stays");
    out.clear(); if (coopslot::ZoneOwnerSwapN("45-gamedata.base", sh, 0, 1, true, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106: a town's building stays");
    /* the protocol-67 rule on the world server's count */
    if (!coopslot::LegacyPeerIsMineByWorld(1) || !coopslot::LegacyPeerIsMineByWorld(2) || coopslot::LegacyPeerIsMineByWorld(3) || coopslot::LegacyPeerIsMineByWorld(40)
        || coopslot::LegacyPeerIsMineByWorld(-1)) Fail("p106: coop-peer is mine only in a world the world server counts at most two players in");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-peer", sh, 0, 2, true, &out) != coopslot::kSwapLegacyToMine || out != sh) Fail("p106: two players: coop-peer -> mine");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-peer", sh, 0, 2, false, &out) != coopslot::kSwapLegacyInactive || !out.empty()) Fail("p106: more than two players: coop-peer kept");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-peer", sh, 0, 0, false, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106: my own old record is untouched by the rule");
    out.clear(); if (coopslot::ZoneOwnerSwap("coop-peer", sh, 0, 2, &out) != coopslot::kSwapLegacyToMine) Fail("p106: ZoneOwnerSwap is the two-player rule");
}
void t_p106_zone_names_every_player()
{
    std::string f;
    P106PushStr(&f, "owner faction ID"); P106PushStr(&f, "coop-p7");
    P106PushStr(&f, "owner faction ID"); P106PushStr(&f, "204-gamedata.base");
    P106PushStr(&f, "owner faction ID"); P106PushStr(&f, "coop-p12");
    P106PushStr(&f, "coop-p7"); f.push_back('5');   /* a digit right after the string: the length decides, so this is 7, never 75 */
    P106PushStr(&f, "coop-peer"); P106PushStr(&f, "coop-p01"); P106PushStr(&f, "coop-p1024"); P106PushStr(&f, "coop-p");
    f += "coop-p9";   /* raw text, not a length-prefixed string: never an owner */
    std::vector<int> named;
    const int hits = coopslot::ZoneFileNamedSlots((const unsigned char*)f.data(), f.size(), &named);
    if (hits != 3 || named.size() != 2 || named[0] != 7 || named[1] != 12) Fail("p106: the scan finds exactly coop-p7 (twice) and coop-p12");
    std::string cut; P106PushStr(&cut, "coop-p33"); cut.resize(cut.size() - 1);
    std::vector<int> none;
    if (coopslot::ZoneFileNamedSlots((const unsigned char*)cut.data(), cut.size(), &none) != 0 || !none.empty()) Fail("p106: a truncated string is not read");
    if (coopslot::ZoneFileNamedSlots(0, 100, &none) != 0 || coopslot::ZoneFileNamedSlots((const unsigned char*)"ab", 2, &none) != 0) Fail("p106: empty input");
    /* who needs a stand-in: the writer and every named number except me */
    std::vector<int> need; named.push_back(0);
    coopslot::SlotsNeedingStandIn(named, 0, 2, &need);
    if (need.size() != 3 || need[0] != 2 || need[1] != 7 || need[2] != 12) Fail("p106: writer 2 + named 7, 12 (not me, 0)");
    std::vector<int> need2; coopslot::SlotsNeedingStandIn(named, 2, 2, &need2);
    if (need2.size() != 3 || need2[0] != 7 || need2[1] != 12 || need2[2] != 0) Fail("p106: my own record names no writer stand-in");
    /* the area read's owner gate: the writer's stand-in alone no longer lets a record through */
    if (!coopslot::ZoneSwapReady(0, 2, true)) Fail("p106: the writer-only check passes");
    if (coopslot::ZoneOwnersReady(0, 2, true, 1)) Fail("p106: a named player with no stand-in here refuses the swap");
    if (coopslot::ZoneOwnersReady(0, 2, true, -1)) Fail("p106: an unreadable file refuses the swap");
    if (coopslot::ZoneOwnersReady(0, 2, false, 0) || coopslot::ZoneOwnersReady(-1, 2, true, 0)) Fail("p106: the writer-only conditions still hold");
    if (!coopslot::ZoneOwnersReady(0, 2, true, 0)) Fail("p106: every named player has a stand-in - the swap goes ahead");
    /* the stand-ins made before any area is read: writers first, then named owners, the cap counted */
    std::vector<int> wr, nm, outs;
    wr.push_back(2); wr.push_back(0); wr.push_back(2);
    nm.push_back(7); nm.push_back(2); nm.push_back(0); nm.push_back(12);
    const std::vector<int> noneHave;
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, noneHave, 16, &outs) != 0 || outs.size() != 3 || outs[0] != 2 || outs[1] != 7 || outs[2] != 12) Fail("p106: writers then named owners, each once, not me");
    outs.clear();
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, noneHave, 2, &outs) != 1 || outs.size() != 2 || outs[0] != 2 || outs[1] != 7) Fail("p106: the cap keeps the first two and counts one left out");
    outs.clear();
    if (coopslot::AreaStandInSlotsCapped(wr, nm, -1, noneHave, 16, &outs) != 0 || !outs.empty()) Fail("p106: none while my own number is unknown");
    outs.clear(); outs.push_back(12);
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, noneHave, 2, &outs) != 0 || outs.size() != 3 || outs[0] != 12 || outs[1] != 2 || outs[2] != 7) Fail("p106: a player already listed is not listed or counted twice");
}
/* the area read's legacy gate, the swap without this game's number, the cap that counts placeholders made, the helper slot past 254 */
void t_p106_fold1()
{
    using namespace coopbuild;
    const std::string sh("204-gamedata.base");
    std::string out;
    /* the swap on a game whose own number is not known: the writer's building -> its stand-in, never to this game's id */
    if (coopslot::ZoneOwnerSwapN(sh, sh, -1, 1, true, &out) != coopslot::kSwapToStandIn || out != "coop-p1") Fail("p106 fold1: no number of mine - the writer's building -> its stand-in");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-p3", sh, -1, 1, true, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106 fold1: no number of mine - a third player's building stays");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-p0", sh, -1, 1, true, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106 fold1: no number of mine - coop-p0 stays (never read as mine)");
    out.clear(); if (coopslot::ZoneOwnerSwapN(sh, sh, -1, -1, true, &out) != coopslot::kSwapNoWriter || !out.empty()) Fail("p106 fold1: neither number known - not swapped");
    /* the legacy branch with no writer number, and with the world server's count unknown */
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-peer", sh, 0, -1, true, &out) != coopslot::kSwapNone || !out.empty()) Fail("p106 fold1: coop-peer with no writer number stays");
    out.clear(); if (coopslot::ZoneOwnerSwapN("coop-peer", sh, 0, 2, coopslot::LegacyPeerIsMineByWorld(-1), &out) != coopslot::kSwapLegacyInactive || !out.empty()) Fail("p106 fold1: count unknown - coop-peer never handed to me");
    /* the area read's legacy gate */
    if (!coopslot::ZoneLegacyPeerReady(false, 0, 2, -1, false)) Fail("p106 fold1: a record naming no coop-peer is not this gate's");
    if (coopslot::ZoneLegacyPeerReady(true, 0, 2, -1, true) || coopslot::ZoneLegacyPeerReady(true, 0, 2, -1, false)) Fail("p106 fold1: count unknown - refused, never judged on a guess");
    if (!coopslot::ZoneLegacyPeerReady(true, 0, 2, 2, false) || !coopslot::ZoneLegacyPeerReady(true, 0, 2, 1, false)) Fail("p106 fold1: two players - coop-peer is mine, no faction needed");
    if (coopslot::ZoneLegacyPeerReady(true, 0, 2, 3, false)) Fail("p106 fold1: three players and no coop-peer faction here - refused");
    if (!coopslot::ZoneLegacyPeerReady(true, 0, 2, 3, true)) Fail("p106 fold1: three players and a coop-peer faction here - read");
    if (!coopslot::ZoneLegacyPeerReady(true, 0, -1, -1, false) || !coopslot::ZoneLegacyPeerReady(true, 2, 2, -1, false)) Fail("p106 fold1: no writer, or my own record - not this gate's");
    /* the file scan: over-long "coop-p..." strings name nobody; coop-peer is reported apart */
    std::string f;
    P106PushStr(&f, "coop-p12345"); P106PushStr(&f, "coop-p10230"); P106PushStr(&f, "coop-peerX");
    f.push_back((char)0x07); f.push_back((char)0x00); f.push_back((char)0x00); f.push_back((char)0x01); f += "coop-p5";   /* a length past the file's end */
    std::vector<int> named; bool leg = false;
    if (coopslot::ZoneFileNamedSlots((const unsigned char*)f.data(), f.size(), &named, &leg) != 0 || !named.empty() || leg) Fail("p106 fold1: an over-long or cut coop-p string named a player");
    std::string g; P106PushStr(&g, "coop-peer"); P106PushStr(&g, "coop-p4");
    named.clear(); leg = false;
    if (coopslot::ZoneFileNamedSlots((const unsigned char*)g.data(), g.size(), &named, &leg) != 1 || named.size() != 1 || named[0] != 4 || !leg) Fail("p106 fold1: coop-peer is reported, coop-p4 named");
    named.clear();
    if (coopslot::ZoneFileNamedSlots((const unsigned char*)g.data(), g.size(), &named) != 1) Fail("p106 fold1: the scan without the legacy report");
    /* the cap counts placeholders made: players with a stand-in already are skipped and never count */
    std::vector<int> wr, nm, have, outs;
    wr.push_back(2); wr.push_back(5); nm.push_back(7); nm.push_back(12);
    have.push_back(2); have.push_back(7);
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, have, 1, &outs) != 1 || outs.size() != 1 || outs[0] != 5) Fail("p106 fold1: room 1 - player 5 made, 12 left out, 2 and 7 not counted");
    outs.clear();
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, have, 2, &outs) != 0 || outs.size() != 2 || outs[0] != 5 || outs[1] != 12) Fail("p106 fold1: room 2 - both players without a stand-in");
    outs.clear(); have.push_back(5); have.push_back(12);
    if (coopslot::AreaStandInSlotsCapped(wr, nm, 0, have, 0, &outs) != 0 || !outs.empty()) Fail("p106 fold1: no room, but every player has a stand-in - none left out");
    /* a saved row neither layout reads: the two-byte layout's answer */
    BuildMsg m; m.kind = kBuildPlace; m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base"; m.needed = 5.0f; m.ownerSlot = 3;
    std::vector<char> b; EncodeBuild(&b, m);
    std::vector<char> cut(b.begin(), b.begin() + 5);
    BuildMsg o;
    const int r2 = DecodeBuild(&cut[0], cut.size(), &o);
    if (r2 == kBuildDecodeOk || DecodeBuildSaved(&cut[0], cut.size(), &o) != r2) Fail("p106 fold1: a saved row neither layout reads is refused with the wire layout's answer");
    /* the STATE's helper slots carry 0..1023 */
    BuildMsg st; st.kind = kBuildState; st.key = "1459-gamedata.base@1,2:3,4,5"; st.needed = 2.0f;
    BuildHelpAck a; a.slot = 1023; a.seq = 5; a.live = 6; a.nEx = 1; a.ex[0] = 2.0f; st.helpAcks.push_back(a);
    BuildHelpAck a2; a2.slot = 300; a2.seq = 1; a2.live = 1; st.helpAcks.push_back(a2);
    std::vector<char> sb;
    if (!EncodeBuild(&sb, st) || DecodeBuild(&sb[0], sb.size(), &o) != kBuildDecodeOk || o.helpAcks.size() != 2 || o.helpAcks[0].slot != 1023 || o.helpAcks[0].live != 6
        || o.helpAcks[1].slot != 300) Fail("p106 fold1: helper slots 1023 and 300 did not survive a STATE");
    if (DecodeBuildSaved(&sb[0], sb.size(), &o) != kBuildDecodeOk || o.helpAcks.size() != 2 || o.helpAcks[0].slot != 1023) Fail("p106 fold1: a saved STATE with two-byte helper slots");
    st.helpAcks[1].slot = 1024; std::vector<char> xb;
    if (EncodeBuild(&xb, st)) Fail("p106 fold1: a helper slot past the notebook's slots was encoded");
    BuildMsg s1; s1.kind = kBuildState; s1.key = "k"; s1.needed = 1.0f;
    BuildHelpAck a7; a7.slot = 7; a7.seq = 9; a7.live = 9; s1.helpAcks.push_back(a7);
    std::vector<char> nb; EncodeBuild(&nb, s1);
    const size_t t = nb.size() - (2 + 4 + 4 + 1) - 1;   /* the tail's row-count byte */
    if (((unsigned char)nb[t] & kBuildHelpTailWide) == 0 || (unsigned char)nb[t + 1] != 7 || nb[t + 2] != 0) Fail("p106 fold1: the helper slot is not two little-endian bytes under the wide flag");
    std::vector<char> ob(nb); ob[t] = (char)((unsigned char)ob[t] & ~kBuildHelpTailWide); ob.erase(ob.begin() + (long)(t + 2));   /* a row written with a one-byte slot */
    if (DecodeBuildSaved(&ob[0], ob.size(), &o) != kBuildDecodeOk || o.helpAcks.size() != 1 || o.helpAcks[0].slot != 7 || o.helpAcks[0].seq != 9) Fail("p106 fold1: a saved one-byte helper slot did not read");
    /* pp.help keeps an owner slot up to 1023 */
    std::vector<coopown::HelpRecRow> rows(1), got; rows[0].key = "k"; rows[0].slot = 1023;
    std::vector<char> rb; coopown::EncodeHelpRec(rows, &rb);
    if (!coopown::DecodeHelpRec(rb, &got) || got.size() != 1 || got[0].slot != 1023) Fail("p106 fold1: pp.help lost slot 1023");
    rows[0].slot = 1024; coopown::EncodeHelpRec(rows, &rb);
    if (coopown::DecodeHelpRec(rb, &got)) Fail("p106 fold1: pp.help took slot 1024");
}
/* PLAYERS carries the world server's player count after its rows */
void t_p106_world_players_wire()
{
    using namespace coopjoin;
    std::vector<RosterRow> rows(1); rows[0].slot = 2; rows[0].stage = (unsigned)kStageInWorld; rows[0].name = "B";
    std::vector<char> m;
    if (!RosterEncode(rows, 3, &m)) { Fail("p106: a roster with a world count was refused"); return; }
    std::vector<RosterRow> back; unsigned wp = 0;
    if (!RosterDecode(&m[0], m.size(), &back, &wp) || back.size() != 1 || back[0].slot != 2 || wp != 3) Fail("p106: the world count did not round-trip");
    if (!RosterDecode(&m[0], m.size(), &back, 0)) Fail("p106: the count may be ignored");
    std::vector<char> old(m.begin(), m.end() - 8);   /* the rows alone (without the world count and the vacancy word) */
    if (RosterDecode(&old[0], old.size(), &back, &wp)) Fail("p106: a roster without the world count decoded");
    std::vector<char> big; if (RosterEncode(rows, kWorldPlayersMax + 1, &big)) Fail("p106: more players than slots was encoded");
    std::vector<char> hand(old); const unsigned over = kWorldPlayersMax + 1; JsPut32(&hand, over); JsPut32(&hand, 0u);
    if (RosterDecode(&hand[0], hand.size(), &back, &wp)) Fail("p106: more players than slots decoded");
    std::vector<char> full; if (!RosterEncode(rows, kWorldPlayersMax, &full) || !RosterDecode(&full[0], full.size(), &back, &wp) || wp != kWorldPlayersMax) Fail("p106: 1024 players fit");
}
/* the build message's owner number is two bytes; rows saved with the one-byte number still read */
void t_p106_owner_two_bytes()
{
    using namespace coopbuild;
    BuildMsg m; m.kind = kBuildPlace;
    m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base"; m.needed = 5.0f;
    BuildMsg o;
    const int slots[] = { 0, 3, 254, 255, 256, 1023 };
    for (int i = 0; i < 6; ++i)
    {
        m.ownerSlot = (unsigned short)slots[i]; m.nonce = 0;
        std::vector<char> b;
        if (!EncodeBuild(&b, m) || DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.ownerSlot != slots[i]) Fail("p106: an owner number did not survive the wire");
        if ((unsigned char)b[b.size() - 2] != (slots[i] & 0xFF) || (unsigned char)b.back() != ((slots[i] >> 8) & 0xFF)) Fail("p106: the owner number is not two little-endian bytes at the end");
        m.nonce = 0x01020304u; std::vector<char> bn;
        if (!EncodeBuild(&bn, m) || bn.size() != b.size() + 4 || DecodeBuild(&bn[0], bn.size(), &o) != kBuildDecodeOk || o.ownerSlot != slots[i] || o.nonce != 0x01020304u) Fail("p106: owner + nonce did not survive the wire");
    }
    m.ownerSlot = kBuildOwnerSender; m.nonce = 0;
    std::vector<char> sb;
    if (!EncodeBuild(&sb, m) || DecodeBuild(&sb[0], sb.size(), &o) != kBuildDecodeOk || o.ownerSlot != kBuildOwnerSender) Fail("p106: the sender's own piece did not survive");
    m.ownerSlot = 1024; std::vector<char> nb;
    if (EncodeBuild(&nb, m)) Fail("p106: an owner number past the notebook's slots was encoded");
    std::vector<char> hb(sb); hb[hb.size() - 2] = 0x00; hb[hb.size() - 1] = 0x04;   /* 1024 */
    if (DecodeBuild(&hb[0], hb.size(), &o) != kBuildDecodeBadValue) Fail("p106: an owner number past the notebook's slots decoded");
    /* a row saved with the one-byte owner number: the wire refuses it, the saved-row reader takes it */
    m.ownerSlot = 3; m.nonce = 0; std::vector<char> nb3; EncodeBuild(&nb3, m);
    std::vector<char> old3(nb3.begin(), nb3.end() - 1);   /* the high byte gone: owner 3 in one byte */
    if (DecodeBuild(&old3[0], old3.size(), &o) == kBuildDecodeOk) Fail("p106: the wire took a one-byte owner number");
    if (DecodeBuildSaved(&old3[0], old3.size(), &o) != kBuildDecodeOk || o.ownerSlot != 3 || o.nonce != 0) Fail("p106: a saved one-byte owner row did not read");
    std::vector<char> oldS(old3); oldS.back() = (char)0xFF;
    if (DecodeBuildSaved(&oldS[0], oldS.size(), &o) != kBuildDecodeOk || o.ownerSlot != kBuildOwnerSender) Fail("p106: a saved one-byte 0xFF did not read as the sender");
    m.nonce = 0xA1B2C3D4u; std::vector<char> nn; EncodeBuild(&nn, m);
    std::vector<char> oldN(nn); oldN.erase(oldN.end() - 5);   /* owner 3 in one byte, then the nonce */
    if (DecodeBuildSaved(&oldN[0], oldN.size(), &o) != kBuildDecodeOk || o.ownerSlot != 3 || o.nonce != 0xA1B2C3D4u) Fail("p106: a saved one-byte owner row with a nonce did not read");
    if (DecodeBuildSaved(&nn[0], nn.size(), &o) != kBuildDecodeOk || o.ownerSlot != 3 || o.nonce != 0xA1B2C3D4u) Fail("p106: the saved-row reader takes the two-byte layout first");
    /* the copy list: a copy of player 1023's piece is a row */
    BuildCopyRec c; c.key = m.key; c.sid = m.sid; c.slot = 1023; c.nonce = 9;
    std::string rk; std::vector<char> pb, stb; BuildCopyRec back;
    if (!BuildCopyRowMake(c, &rk, &pb, &stb) || !BuildCopyRowRead(rk, std::string(pb.begin(), pb.end()), std::string(stb.begin(), stb.end()), &back) || back.slot != 1023) Fail("p106: a copy row for player 1023");
    c.slot = 1024; if (BuildCopyRowMake(c, &rk, &pb, &stb)) Fail("p106: a copy row past the notebook's slots");
    int why = -1;
    if (coopown::HouseOwnerOfNewPiece(1, coopown::kOwnPeer, 1023, 1, &why) != 1023 || why != coopown::kHoHanded) Fail("p106: a house of player 1023 is handed to player 1023");
}
/* the players a game makes a stand-in for before it reads any area record (src/common/slotwire.h AreaWriterStandInSlots), the
   placeholder name such a stand-in carries, and the one name-clash rule for a new stand-in and a rename. Fails before: none of
   it existed - a stand-in was made only when its player's characters or names arrived, after the first area read. */
void t_area_writer_standins()
{
    std::vector<int> w, out;
    w.push_back(0); w.push_back(1); w.push_back(0); w.push_back(-1); w.push_back(2); w.push_back(1024); w.push_back(2);
    coopslot::AreaWriterStandInSlots(w, 1, 32, &out);
    if (out.size() != 2 || out[0] != 0 || out[1] != 2) Fail("each other writer once; mine, an unknown writer (-1) and an out-of-range one left out");
    out.clear(); coopslot::AreaWriterStandInSlots(w, -1, 32, &out);
    if (!out.empty()) Fail("none while my own slot is unknown");
    out.clear(); coopslot::AreaWriterStandInSlots(w, 1, 1, &out);
    if (out.size() != 1 || out[0] != 0) Fail("bounded by the cap");
    out.clear(); coopslot::AreaWriterStandInSlots(std::vector<int>(3, 1), 1, 32, &out);
    if (!out.empty()) Fail("my own records need no stand-in");
    out.clear(); coopslot::AreaWriterStandInSlots(std::vector<int>(), 0, 32, &out);
    if (!out.empty()) Fail("no records, no stand-ins");
    out.clear(); out.push_back(2); coopslot::AreaWriterStandInSlots(w, 1, 32, &out);
    if (out.size() != 2 || out[0] != 2 || out[1] != 0) Fail("a slot already listed is not listed twice");
    if (coopslot::PlaceholderName(0) != "Player 0" || coopslot::PlaceholderName(12) != "Player 12") Fail("PlaceholderName");
    if (coopslot::StandInClashName("Nameless (2)", 0, false, false) != "Nameless (2)"
        || coopslot::StandInClashName("Nameless (2)", 0, true, false) != "Nameless (2) (peer)"
        || coopslot::StandInClashName("Nameless (2)", 0, true, true) != "Nameless (2) (peer) (p0)") Fail("StandInClashName");
}

/* refill1 (docs/design-refill1.md s2-s4): the pure half of "bars refill every 5 in-game days" - the due decision, missing ->
   per-entry counts, the nearby geometry, the shared record's merge, its wire form and its file line (src/common/barwire.h,
   the SAME header the plugin and the notebook compile). Every input is a literal. */
void t_copy1_keep_decisions()
{
    /* COPY1: blood is written back only on a copy, after a good read, for a finite value (negative is real) */
    if (coopkeep::BloodRestoreWanted(1, 1, 17.0f) != 1) Fail("a copy's blood is kept");
    if (coopkeep::BloodRestoreWanted(1, 1, -3.0f) != 1) Fail("negative blood (bleeding out) is kept");
    if (coopkeep::BloodRestoreWanted(0, 1, 17.0f) != 0) Fail("never on a character this game drives");
    if (coopkeep::BloodRestoreWanted(1, 0, 17.0f) != 0) Fail("a failed read writes nothing back");
    {
        float inf = 0.0f, nan = 0.0f;
        unsigned int bi = 0x7F800000u, bn = 0x7FC00000u;
        std::memcpy(&inf, &bi, 4);
        std::memcpy(&nan, &bn, 4);
        if (coopkeep::BloodRestoreWanted(1, 1, inf) != 0) Fail("an infinite read writes nothing back");
        if (coopkeep::BloodRestoreWanted(1, 1, nan) != 0) Fail("a NaN read writes nothing back");
        if (coopkeep::BloodPushDelta(10.0f, inf) != 0.0f || coopkeep::BloodPushDelta(nan, 10.0f) != 0.0f) Fail("a non-finite delta reads 0");
    }
    {
        const float d = coopkeep::BloodPushDelta(10.93f, 79.45f);   /* T378 uid 4244635885: the copy's refill */
        if (d < 68.51f || d > 68.53f) Fail("the T378 refill 10.93 -> 79.45 is a 68.52 delta");
    }
    if (coopkeep::BloodPushDelta(80.0f, 79.0f) != 1.0f) Fail("the delta is absolute");
    if (coopkeep::BloodPushDelta(5.0f, 5.0f) != 0.0f) Fail("no move reads 0");
    /* COPY2: whole-inventory add first; the owner's section on a refusal; destroy only on a clean refusal */
    if (coopkeep::KitPlacement(1, 0) != coopkeep::kKitPlacedByEngine) Fail("the engine add wins");
    if (coopkeep::KitPlacement(1, -1) != coopkeep::kKitPlacedByEngine) Fail("the section is not asked after an engine yes");
    if (coopkeep::KitPlacement(0, 1) != coopkeep::kKitPlacedBySection) Fail("a refused item placed by its own section");
    if (coopkeep::KitPlacement(0, 0) != coopkeep::kKitDestroy) Fail("both refused -> destroyed, no orphan");
    if (coopkeep::KitPlacement(0, -1) != coopkeep::kKitLeaveFaulted) Fail("a faulted section add is never destroyed");
    /* copy2: the owner's section switched ON for the add only when it was OFF; the value read is what is put back */
    if (coopkeep::KitSwitchOnForAdd(0) != 1) Fail("an OFF section (a living animal's main) is switched on for the add");
    if (coopkeep::KitSwitchOnForAdd(1) != 0) Fail("an ON section is not touched");
    if (coopkeep::KitFlagToRestore(0) != 0) Fail("an OFF section goes back OFF after the add");
    if (coopkeep::KitFlagToRestore(1) != 1) Fail("an ON section (a dead or KO'd copy's main) is never forced off");
    for (int was = 0; was <= 1; ++was)
        if (coopkeep::KitSwitchOnForAdd(was) != 0 && coopkeep::KitFlagToRestore(was) != was) Fail("the restore is the value read");
    /* copy2: the refused item's guarded destroy - every answer lands in a counter */
    if (coopkeep::KitDestroyCounter(1) != coopkeep::kKitCountDestroyed) Fail("a destroyed item counts as destroyed");
    if (coopkeep::KitDestroyCounter(0) != coopkeep::kKitCountDestroyRefused) Fail("an engine refusal is counted, not assumed destroyed");
    if (coopkeep::KitDestroyCounter(-1) != coopkeep::kKitCountDestroyFault) Fail("a faulted destroy is counted");
}
int Copy3Taken(void* ctx, int x, int y)
{
    Copy3Grid* g = (Copy3Grid*)ctx;
    ++g->asked;
    if (x + g->iw > g->maxRight) g->maxRight = x + g->iw;
    if (y + g->ih > g->maxBottom) g->maxBottom = y + g->ih;
    for (int yy = y; yy < y + g->ih; ++yy)
        for (int xx = x; xx < x + g->iw; ++xx)
            if (xx < 8 && yy < 8 && g->cell[yy][xx] != 0) return 1;
    return 0;
}
void Copy3Init(Copy3Grid* g, int iw, int ih)
{
    g->iw = iw; g->ih = ih; g->maxRight = 0; g->maxBottom = 0; g->asked = 0;
    for (int yy = 0; yy < 8; ++yy) for (int xx = 0; xx < 8; ++xx) g->cell[yy][xx] = 0;
}

void t_copy3_animal_route()
{
    /* the route: animals take the engine's own animal fill route, everything else keeps copy2's section add */
    if (coopkeep::KitSectionRoute(1) != coopkeep::kKitRouteAnimalEngine) Fail("an animal copy uses the engine's animal route");
    if (coopkeep::KitSectionRoute(0) != coopkeep::kKitRouteSectionAdd) Fail("a non-animal copy keeps the section add");
    Copy3Grid g;
    int x = 0, y = 0;
    /* empty 4x8 main (T386 wh=4x8), 1x1 item -> the top-left cell */
    Copy3Init(&g, 1, 1);
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 1, &Copy3Taken, &g, &x, &y) != 1 || x != 0 || y != 0) Fail("empty grid -> 0,0");
    /* x is the inner loop: (0,0) taken -> (1,0), not (0,1) */
    Copy3Init(&g, 1, 1);
    g.cell[0][0] = 1;
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 1, &Copy3Taken, &g, &x, &y) != 1 || x != 1 || y != 0) Fail("y outer, x inner (0x74BD70 order)");
    /* a full first row -> the next row */
    Copy3Init(&g, 1, 1);
    for (int k = 0; k < 4; ++k) g.cell[0][k] = 1;
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 1, &Copy3Taken, &g, &x, &y) != 1 || x != 0 || y != 1) Fail("a full row -> the next row");
    /* a 2x3 item with only the bottom-right 2x3 block free: found there, and no footprint ever asked outside the grid */
    Copy3Init(&g, 2, 3);
    for (int yy = 0; yy < 8; ++yy) for (int xx = 0; xx < 4; ++xx) g.cell[yy][xx] = (xx >= 2 && yy >= 5) ? 0 : 1;
    if (coopkeep::KitFirstFreeCell(4, 8, 2, 3, &Copy3Taken, &g, &x, &y) != 1 || x != 2 || y != 5) Fail("the last position is reached");
    if (g.maxRight > 4 || g.maxBottom > 8) Fail("no footprint outside the grid is ever tested (0x745BB0 calls that free)");
    if (g.asked != 3 * 6) Fail("every in-grid position is asked once: (4-2+1) x (8-3+1)");
    /* a full grid -> none; an item wider or taller than the grid -> none, nothing asked */
    Copy3Init(&g, 1, 1);
    for (int yy = 0; yy < 8; ++yy) for (int xx = 0; xx < 4; ++xx) g.cell[yy][xx] = 1;
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 1, &Copy3Taken, &g, &x, &y) != 0 || x != -1 || y != -1) Fail("a full grid has no cell");
    Copy3Init(&g, 5, 1);
    if (coopkeep::KitFirstFreeCell(4, 8, 5, 1, &Copy3Taken, &g, &x, &y) != 0 || g.asked != 0) Fail("too wide -> none, nothing asked");
    Copy3Init(&g, 1, 9);
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 9, &Copy3Taken, &g, &x, &y) != 0 || g.asked != 0) Fail("too tall -> none, nothing asked");
    if (coopkeep::KitFirstFreeCell(4, 8, 0, 1, &Copy3Taken, &g, &x, &y) != 0) Fail("a zero footprint is refused");
    if (coopkeep::KitFirstFreeCell(4, 8, 1, 1, 0, &g, &x, &y) != 0) Fail("no test function -> none");
    if (coopkeep::KitFirstFreeCell(1000, 8, 1, 1, &Copy3Taken, &g, &x, &y) != 0) Fail("an unreadable grid size is refused");
    /* the _addItem answer: one more row = placed; anything else unknown and never destroyed */
    if (coopkeep::KitEngineRouteResult(0, 1) != 1) Fail("one more row = placed");
    if (coopkeep::KitEngineRouteResult(3, 3) != -1) Fail("no new row = unknown, not refused");
    if (coopkeep::KitEngineRouteResult(3, 5) != -1) Fail("two new rows = unknown");
    if (coopkeep::KitPlacement(0, coopkeep::KitEngineRouteResult(3, 3)) != coopkeep::kKitLeaveFaulted) Fail("an unknown engine-route add is never destroyed");
    if (coopkeep::KitPlacement(0, coopkeep::KitEngineRouteResult(2, 3)) != coopkeep::kKitPlacedBySection) Fail("an engine-route add counts as placed by section");
}

/* T-959: an area's first-time flag read the engine's way (0xA07C10's ZoneMap, the pointer at it, + 0xA8) */
void t_t959_area_flag()
{
    if (loadedzones::ZoneMapAddr(0x1000, 0, 0) != 0x1000 + 200) Fail("area 0,0's ZoneMap is the manager + 200 (0xA07C10)");
    if (loadedzones::ZoneMapAddr(0x1000, 2, 3) != 0x1000 + 200 + (2ULL * 64 + 3) * 0x168) Fail("area 2,3's ZoneMap: (x*64 + y)*0x168");
    if (loadedzones::ZoneMapAddr(0x1000, 63, 63) != 0x1000 + 200 + 4095ULL * 0x168) Fail("the last area");
    if (loadedzones::ZoneMapAddr(0x1000, 64, 0) != 0 || loadedzones::ZoneMapAddr(0x1000, 0, 64) != 0) Fail("off the grid: none");
    if (loadedzones::ZoneMapAddr(0x1000, -1, 5) != 0) Fail("a negative area: none (0xA07C10 takes unsigned)");
    if (loadedzones::ZoneMapAddr(0, 1, 1) != 0) Fail("no zone manager: none");
    if (loadedzones::kZoneMapArea != 0 || loadedzones::kAreaFirstTimeFlag != 0xA8) Fail("the pointer at ZoneMap+0, then +0xA8");
    if (loadedzones::AreaFlagOf(0, 1, 0x5000, 1, 1) != loadedzones::kFlagNoZone) Fail("no ZoneMap: no zone");
    if (loadedzones::AreaFlagOf(1, 0, 0, 0, 0) != loadedzones::kFlagUnread) Fail("the pointer unread: unread");
    if (loadedzones::AreaFlagOf(1, 1, 0, 0, 0) != loadedzones::kFlagNoArea) Fail("a null area: no area (createBuilding returns none)");
    if (loadedzones::AreaFlagOf(1, 1, 0x5000, 0, 0) != loadedzones::kFlagUnread) Fail("the byte unread: unread");
    if (loadedzones::AreaFlagOf(1, 1, 0x5000, 1, 0) != 0) Fail("byte 0: clear");
    if (loadedzones::AreaFlagOf(1, 1, 0x5000, 1, 1) != 1 || loadedzones::AreaFlagOf(1, 1, 0x5000, 1, 0xBC) != 1) Fail("any non-zero byte: set");
}

/* T-1076 (owner decision 1076 c): ring 1 from ANY of this game's player characters; a party's ring 1 is judged as the followed one's */
void t_t1076_ring1_party()
{
    std::vector<unsigned char> g(loadedzones::kGrid * loadedzones::kGrid, 0);
    g[35 * loadedzones::kGrid + 18] = 1;   /* the report's shape: a second party at 35,18 ('p'), the followed character at 19,13 */
    int po = -1;
    if (loadedzones::Ring1Of(19, 13, &g[0], 35, 19, &po) != 1 || po != 1) Fail("ring1 from a second party, party-only");
    if (loadedzones::Ring1Grants(1, 0, 0) != 1) Fail("a party-only ring 1 with nobody else beside it grants");
    if (loadedzones::Ring1Of(19, 13, &g[0], 40, 40, &po) != 0 || po != 0) Fail("ring1 0 with no party near");
    if (loadedzones::Ring1Of(19, 13, &g[0], 20, 14, &po) != 1 || po != 0) Fail("the followed character's ring 1 is not party-only");
    std::vector<unsigned char> e(loadedzones::kGrid * loadedzones::kGrid, 0);
    if (loadedzones::Ring1Of(-1, -1, &e[0], 35, 19, &po) != 0 || po != 0) Fail("no followed character and no party: 0");
    if (loadedzones::Ring1Of(-1, -1, 0, 35, 19, &po) != 0) Fail("no grid: 0");
    e[0] = 1;
    if (loadedzones::Ring1Of(-1, -1, &e[0], 0, 1, &po) != 1 || po != 1) Fail("edge of the grid: a party at 0,0 is beside 0,1");
    if (loadedzones::Ring1Of(-1, -1, &e[0], -1, 0, &po) != 0) Fail("off the grid: 0");
    if (loadedzones::Ring1Grants(1, 1, 1) != 1) Fail("ring 1 (a party's or the followed character's), another player beside it, lower slot: grants");
    if (loadedzones::Ring1Grants(1, 1, 0) != 0) Fail("ring 1, another player beside it, higher slot: yields");
    if (loadedzones::Ring1Grants(1, 0, 0) != 1) Fail("ring 1, nobody else beside it, higher slot: grants");
    if (loadedzones::Ring1Grants(0, 0, 1) != 0) Fail("no ring 1: no grant");
}
/* the world server's area assignment with a slot holding two player-character areas: an area beside either goes to that slot */
void t_t1077_area_assign_list()
{
    const int s[3] = { 0, 1, 1 }, x[3] = { 10, 40, 20 }, y[3] = { 10, 40, 21 };   /* slot 0 at 10,10; slot 1 followed at 40,40 and a party at 20,21 */
    const double at[3] = { 99.0, 99.0, 99.0 };
    int n = -1, why = -1;
    if (coopstore::AreaAssignDecide(0, 20, 20, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || n != 1 || why != coopstore::kAssignRing1) Fail("t1077: an area beside slot 1's second entry goes to slot 1");
    if (coopstore::AreaAssignDecide(0, 41, 40, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || why != coopstore::kAssignRing1) Fail("t1077: and an area beside its first entry");
    if (coopstore::AreaAssignDecide(1, 11, 11, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 0 || why != coopstore::kAssignRing1) Fail("t1077: beside slot 0 only -> slot 0");
    if (coopstore::AreaAssignDecide(1, 30, 30, 3, s, x, y, at, 100.0, 5.0, &n, &why) != 1 || n != 0 || why != coopstore::kAssignReporter) Fail("t1077: beside nobody -> the reporter");
    const int s2[3] = { 1, 0, 1 }, x2[3] = { 20, 21, 50 }, y2[3] = { 20, 20, 50 };
    if (coopstore::AreaAssignDecide(1, 20, 21, 3, s2, x2, y2, at, 100.0, 5.0, &n, &why) != 0 || n != 2) Fail("t1077: slot 1's party and slot 0 both beside -> the lower slot 0");
}
/* AREAS' player-sector list (src/common/areadrop.h): {u16 m, m x (i32 x, i32 y)} after the loaded pairs */
void t_t1077_player_sector_wire()
{
    std::vector<char> b(4, (char)0);   /* u32 n = 0 loaded pairs */
    const int xy[4] = { 19, 13, 35, 18 };
    coopdrop::AreasPutPlayerSectors(&b, xy, 2);
    std::vector<int> got;
    if (b.size() != 4 + 2 + 16 || coopdrop::AreasReadPlayerSectors((const unsigned char*)&b[0], b.size(), 4, &got) != 1
        || got.size() != 4 || got[0] != 19 || got[1] != 13 || got[2] != 35 || got[3] != 18) Fail("t1077: two player sectors round trip, in order");
    std::vector<char> e(4, (char)0);
    coopdrop::AreasPutPlayerSectors(&e, 0, 0);
    if (e.size() != 6 || coopdrop::AreasReadPlayerSectors((const unsigned char*)&e[0], e.size(), 4, &got) != 1 || !got.empty()) Fail("t1077: m = 0 round trips as an empty list");
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&e[0], 5, 4, &got) != 0) Fail("t1077: no count is malformed");
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&e[0], 4, 4, &got) != 0) Fail("t1077: a report that stops after the pairs is malformed");
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&b[0], b.size() - 1, 4, &got) != 0) Fail("t1077: fewer bytes than m pairs is malformed");
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&b[0], b.size(), b.size() + 1, &got) != 0) Fail("t1077: a start past the end is malformed");
    std::vector<char> o(4, (char)0);
    o.push_back((char)(coopdrop::kAreasPlayerSectorsMax + 1)); o.push_back((char)0); o.resize(o.size() + (size_t)(coopdrop::kAreasPlayerSectorsMax + 1) * 8, (char)0);
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&o[0], o.size(), 4, &got) != 0) Fail("t1077: m over the cap is refused");
    std::vector<int> many; for (int i = 0; i < (coopdrop::kAreasPlayerSectorsMax + 4) * 2; ++i) many.push_back(i);
    std::vector<char> c(4, (char)0);
    coopdrop::AreasPutPlayerSectors(&c, &many[0], coopdrop::kAreasPlayerSectorsMax + 4);
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&c[0], c.size(), 4, &got) != 1 || (int)got.size() != coopdrop::kAreasPlayerSectorsMax * 2 || got[coopdrop::kAreasPlayerSectorsMax * 2 - 1] != coopdrop::kAreasPlayerSectorsMax * 2 - 1) Fail("t1077: the writer cuts the list at the cap");
    const int odd[2] = { -2147483647 - 1, 70 };
    std::vector<char> d(4, (char)0);
    coopdrop::AreasPutPlayerSectors(&d, odd, 1);
    if (coopdrop::AreasReadPlayerSectors((const unsigned char*)&d[0], d.size(), 4, &got) != 1 || got.size() != 2 || got[0] != (-2147483647 - 1) || got[1] != 70) Fail("t1077: off-grid values travel as sent (the world server skips them)");
}
/* the list a game sends (src/common/loadedzones.h PlayerSectorList) */
void t_t1077_player_sector_list()
{
    std::vector<loadedzones::Area> a;
    loadedzones::Area r;
    r.x = 19; r.y = 13; r.why = loadedzones::kWhyPlayer; a.push_back(r);   /* the followed character's own area, marked too */
    r.x = 35; r.y = 18; a.push_back(r);                                     /* a second party */
    r.x = 36; r.y = 18; r.why = loadedzones::kWhyTown; a.push_back(r);      /* a town keep, no player */
    r.x = 35; r.y = 18; r.why = loadedzones::kWhyPlayer | loadedzones::kWhyCamera; a.push_back(r);   /* the second party again */
    std::vector<int> xy;
    if (loadedzones::PlayerSectorList(19, 13, a, 1, 16, &xy) != 0 || xy.size() != 4 || xy[0] != 19 || xy[1] != 13 || xy[2] != 35 || xy[3] != 18) Fail("t1077: followed first, the second party once, a town keep left out");
    if (loadedzones::PlayerSectorList(-2147483647 - 1, -2147483647 - 1, a, 1, 16, &xy) != 0 || xy.size() != 4 || xy[0] != 19 || xy[2] != 35) Fail("t1077: the followed area unknown, the marked areas still go");
    if (loadedzones::PlayerSectorList(19, 13, a, 0, 16, &xy) != 0 || xy.size() != 2 || xy[0] != 19) Fail("t1077: no marks (probe fallback): the followed area alone");
    if (loadedzones::PlayerSectorList(-1, -1, a, 0, 16, &xy) != 0 || !xy.empty()) Fail("t1077: nothing known: an empty list (sent as m = 0)");
    std::vector<loadedzones::Area> big;
    for (int i = 0; i < 20; ++i) { r.x = i; r.y = 40; r.why = loadedzones::kWhyPlayer; big.push_back(r); }
    if (loadedzones::PlayerSectorList(50, 50, big, 1, 16, &xy) != 5 || xy.size() != 32 || xy[0] != 50 || xy[1] != 50 || xy[2] != 0) Fail("t1077: capped at 16, the followed area first, 5 left out");
}
/* the world server's OnAreas row update (src/common/areadrop.h AreasParse + AreasPlayerRowsReplace, through AreasPlayerRowsUpdate) */
void t_t1077_onareas_rows()
{
    coopdrop::AreasPlayerRows rows;
    std::vector<char> a;
    a.push_back((char)1); a.push_back((char)0); a.push_back((char)0); a.push_back((char)0);   /* u32 n = 1 loaded pair */
    a.resize(a.size() + 8, (char)0);                                                          /* the pair 0,0 */
    const int xy[6] = { 19, 13, 70, 2, 35, 18 };   /* the second entry is off the grid */
    coopdrop::AreasPutPlayerSectors(&a, xy, 3);
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, (const unsigned char*)&a[0], a.size(), 10.0) != 2) Fail("t1077 rows: the two on-grid entries are taken");
    coopdrop::AreasPlayerRows::const_iterator r1 = rows.find(1);
    if (r1 == rows.end() || r1->second.size() != 2 || r1->second[0].x != 19 || r1->second[0].y != 13 || r1->second[1].x != 35 || r1->second[1].y != 18
        || r1->second[0].at != 10.0) Fail("t1077 rows: in order, the off-grid entry skipped, stamped with the arrival time");
    std::vector<char> b(4, (char)0);
    const int xy2[2] = { 40, 40 };
    coopdrop::AreasPutPlayerSectors(&b, xy2, 1);
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 2, (const unsigned char*)&b[0], b.size(), 11.0) != 1) Fail("t1077 rows: slot 2's one entry");
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, (const unsigned char*)&b[0], b.size(), 12.0) != 1) Fail("t1077 rows: slot 1's new list");
    r1 = rows.find(1);
    if (r1 == rows.end() || r1->second.size() != 1 || r1->second[0].x != 40 || r1->second[0].at != 12.0) Fail("t1077 rows: a new list replaces the old one whole");
    std::vector<char> c(4, (char)0); c[0] = (char)5;   /* 5 loaded pairs promised, none there */
    coopdrop::AreasPutPlayerSectors(&c, xy2, 1);
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, (const unsigned char*)&a[0], a.size() - 1, 13.0) != -1) Fail("t1077 rows: a list short by a byte is malformed");
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, (const unsigned char*)&c[0], c.size(), 13.0) != -1) Fail("t1077 rows: loaded pairs past the end are malformed");
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, 0, 0, 13.0) != -1) Fail("t1077 rows: no payload is malformed");
    r1 = rows.find(1);
    if (r1 == rows.end() || r1->second.size() != 1 || r1->second[0].at != 12.0 || rows.size() != 2) Fail("t1077 rows: a malformed AREAS changes nothing");
    unsigned pairs = 99; std::vector<int> got;
    if (coopdrop::AreasParse((const unsigned char*)&a[0], a.size(), &pairs, &got) != coopdrop::kAreasOk || pairs != 1 || got.size() != 6) Fail("t1077 rows: AreasParse reads the pair count and the list");
    if (coopdrop::AreasParse((const unsigned char*)&c[0], c.size(), &pairs, &got) != coopdrop::kAreasBadPairs) Fail("t1077 rows: AreasParse names the pairs fault");
    if (coopdrop::AreasParse((const unsigned char*)&a[0], a.size() - 1, &pairs, &got) != coopdrop::kAreasBadList) Fail("t1077 rows: AreasParse names the list fault");
    std::vector<char> e(4, (char)0);
    coopdrop::AreasPutPlayerSectors(&e, 0, 0);
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 1, (const unsigned char*)&e[0], e.size(), 14.0) != 0 || rows.count(1) != 0 || rows.count(2) != 1) Fail("t1077 rows: m = 0 erases the slot's rows, another slot keeps its own");
    const int off[2] = { -1, 64 };
    std::vector<char> f(4, (char)0);
    coopdrop::AreasPutPlayerSectors(&f, off, 1);
    if (coopdrop::AreasPlayerRowsUpdate(&rows, 2, (const unsigned char*)&f[0], f.size(), 15.0) != 0 || rows.count(2) != 0) Fail("t1077 rows: a list with no on-grid entry erases too");
}
/* this game's own party halo is drawn from exactly the list it sends (src/common/loadedzones.h PartyHaloFromList) */
void t_t1077_party_halo_list()
{
    std::vector<loadedzones::Area> a;
    loadedzones::Area r; r.x = 35; r.y = 18; r.why = loadedzones::kWhyPlayer; a.push_back(r);
    std::vector<unsigned char> g(loadedzones::kGrid * loadedzones::kGrid, (unsigned char)7);
    std::vector<int> xy; xy.push_back(19); xy.push_back(13); xy.push_back(35); xy.push_back(18);
    loadedzones::PartyHaloFromList(xy, 1, 0, a, &g[0]);
    int on = 0; for (size_t i = 0; i < g.size(); ++i) on += g[i] != 0 ? 1 : 0;
    if (on != 2 || g[19 * loadedzones::kGrid + 13] != 1 || g[35 * loadedzones::kGrid + 18] != 1) Fail("t1077 halo: every entry sent and nothing else (the grid is cleared first)");
    loadedzones::PartyHaloFromList(xy, 1, 1, a, &g[0]);
    if (g[19 * loadedzones::kGrid + 13] != 0 || g[35 * loadedzones::kGrid + 18] != 1) Fail("t1077 halo: orphaned, the followed character's last area is left out, the other party kept");
    r.x = 19; r.y = 13; a.push_back(r);
    loadedzones::PartyHaloFromList(xy, 1, 1, a, &g[0]);
    if (g[19 * loadedzones::kGrid + 13] != 1) Fail("t1077 halo: orphaned, a live mark on the followed area keeps it");
    std::vector<int> mk; mk.push_back(35); mk.push_back(18);
    loadedzones::PartyHaloFromList(mk, 0, 1, a, &g[0]);
    if (g[35 * loadedzones::kGrid + 18] != 1) Fail("t1077 halo: no followed entry - the first mark is not left out");
    std::vector<loadedzones::Area> big;
    for (int i = 0; i < 20; ++i) { r.x = i; r.y = 40; r.why = loadedzones::kWhyPlayer; big.push_back(r); }
    std::vector<int> cl;
    loadedzones::PlayerSectorList(50, 50, big, 1, 16, &cl);
    loadedzones::PartyHaloFromList(cl, 1, 0, big, &g[0]);
    if (g[14 * loadedzones::kGrid + 40] != 1 || g[15 * loadedzones::kGrid + 40] != 0 || g[19 * loadedzones::kGrid + 40] != 0) Fail("t1077 halo: a marked area past the cap counts for nobody, this game included");
}
/* a slot with several player-sector rows is expected once (ReceiptExpectSlots), and "near any own row" (RosterHolderTick) */
void t_t1077_receipt_and_roster_near()
{
    std::vector<int> out;
    if (cooplo::SlotAddOnce(&out, 2) != 1 || cooplo::SlotAddOnce(&out, 2) != 0 || cooplo::SlotAddOnce(&out, 5) != 1) Fail("t1077: SlotAddOnce's answers");
    if (out.size() != 2 || out[0] != 2 || out[1] != 5) Fail("t1077: a slot with two rows is expected once");
    const int ox[2] = { 19, 35 }, oy[2] = { 13, 18 };
    if (cooplo::NearAnyArea(ox, oy, 2, 37, 20, 2) != 1) Fail("t1077: within 2 of the second party");
    if (cooplo::NearAnyArea(ox, oy, 2, 21, 15, 2) != 1) Fail("t1077: within 2 of the followed character");
    if (cooplo::NearAnyArea(ox, oy, 2, 27, 15, 2) != 0) Fail("t1077: near neither");
    if (cooplo::NearAnyArea(ox, oy, 0, 19, 13, 2) != 0) Fail("t1077: no own rows: near nothing");
}
/* T-1076: an area still loading is set aside as `loading`, not judged `nobody`; once loaded it is judged as before */
void t_t1076_mid_load_defer()
{
    if (townreoffer::DeferCauseAt(1, 0, 0, 0) != townreoffer::kDefLoading) Fail("mid-load, nobody holds it: set aside as loading");
    if (townreoffer::DeferCauseAt(1, 0, -2, 0) != townreoffer::kDefNobody) Fail("a frozen area (held -2) mid-load keeps its earlier label");
    if (townreoffer::DeferCauseAt(1, 0, -1, 0) != townreoffer::kDefNobody) Fail("no map row (held -1, may 0) mid-load keeps its earlier label");
    if (std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefLoading), "loading") != 0) Fail("the cause word is loading");
    if (townreoffer::DeferCauseAt(1, 0, 0, 1) != townreoffer::kDefNobody) Fail("loaded, nobody holds it: nobody, as before");
    if (townreoffer::DeferCauseAt(1, 0, 1, 0) != townreoffer::kDefNone) Fail("another game holds it: an answer, mid-load or not");
    if (townreoffer::DeferCauseAt(1, -1, -1, 0) != townreoffer::kDefNoMap) Fail("no fresh map stays no-map");
    if (townreoffer::DeferCauseAt(1, 1, 0, 0) != townreoffer::kDefNone) Fail("may invent (ring 1) mid-load: an answer");
    if (townreoffer::DeferCauseAt(0, 0, 0, 0) != townreoffer::kDefNoNotebook) Fail("no link stays no-notebook");
    if (townreoffer::Decide(1, 0, 0, 0, 0, 0) != townreoffer::kReWait) Fail("the check-up keeps a mid-load item waiting, never drained");
    if (townreoffer::Decide(1, 0, 0, 0, 1, 0) != townreoffer::kReGenerate) Fail("once loaded and this game's: generated");
}
/* T-392 (owner 335 a / 337 a): what the town gates set aside, and what the town's check-up does with it */
void t_t392_defer_cause()
{
    if (townreoffer::DeferCause(0, 1, 0) != townreoffer::kDefNoNotebook) Fail("no notebook link sets aside, whatever the view");
    if (townreoffer::DeferCause(1, -1, -1) != townreoffer::kDefNoMap) Fail("no fresh map sets aside");
    if (townreoffer::DeferCause(1, 0, 0) != townreoffer::kDefNobody) Fail("nobody holds it sets aside");
    if (townreoffer::DeferCause(1, 0, 1) != townreoffer::kDefNone) Fail("another game holds it is an answer");
    if (townreoffer::DeferCause(1, 1, 0) != townreoffer::kDefNone) Fail("this game may invent is an answer");
    if (townreoffer::DeferCause(1, -2, 0) != townreoffer::kDefNone) Fail("teardown is not a no-map deferral");
    if (std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefNoMap), "no-map") != 0) Fail("cause name no-map");
    if (std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefNobody), "nobody") != 0) Fail("cause name nobody");
}
void t_t392_reoffer_decide()
{
    /* Decide(zoneLive, filled, filledElsewhere, notebookPending, may, held) - T-392 fold: `may` is the load-time gate's own answer */
    if (townreoffer::Decide(1, 1, 0, 0, 1, 0) != townreoffer::kReFilled) Fail("a list already used up is forgotten, whatever the map says");
    if (townreoffer::Decide(0, 0, 0, 0, 1, 0) != townreoffer::kReWait) Fail("zone not live here - wait, even when this game may invent");
    if (townreoffer::Decide(1, 0, 1, 0, 1, 0) != townreoffer::kReOther) Fail("the notebook records a roll - settled, never a second one");
    if (townreoffer::Decide(1, 0, 0, 1, 1, 0) != townreoffer::kReWait) Fail("the notebook's people for the town are still to come - wait");
    if (townreoffer::Decide(1, 0, 0, 0, 1, 0) != townreoffer::kReGenerate) Fail("this game may invent there - generate");
    if (townreoffer::Decide(1, 0, 0, 0, 1, -1) != townreoffer::kReGenerate) Fail("the loaded-here / next-to-the-player presumption generates as at load");
    if (townreoffer::Decide(1, 0, 0, 0, 0, 1) != townreoffer::kReOther) Fail("another game holds it - answered-other");
    if (townreoffer::Decide(1, 0, 0, 0, 0, 0) != townreoffer::kReWait) Fail("nobody holds it and it is not this game's to presume - wait");
    if (townreoffer::Decide(1, 0, 0, 0, -1, -1) != townreoffer::kReWait) Fail("no fresh map - wait, no timer and no fallback");
    if (townreoffer::Decide(1, 0, 0, 0, -2, 1) != townreoffer::kReWait) Fail("teardown is not an answer - wait");
}
/* ---- T-581: A TOWN'S FIRST POPULATION THAT WAS SET ASIDE IS KEPT ON THE WORLD SERVER UNTIL IT IS MADE (src/common/owedpop.h) ---- */
owedpop::Row T581Row(unsigned kind, const char* key, const char* sid) { owedpop::Row r; r.kind = kind; r.key = key; r.sid = sid; r.x = -2150.5f; r.z = 9876.25f; return r; }
void t_t581_wire()
{
    std::vector<char> b; owedpop::Msg m;
    const owedpop::Row r = T581Row(owedpop::kKindResidents, "54321-rebirth.mod|31,45|-21505,98762", "Mongrel");
    if (!owedpop::EncodeAdd(&b, r) || owedpop::Decode(&b[0], b.size(), &m) != 1 || m.op != owedpop::kOpAdd || m.row.key != r.key || m.row.sid != r.sid || m.row.x != r.x || m.row.z != r.z || m.kind != owedpop::kKindResidents) Fail("ADD round trip");
    b.push_back(0);
    if (owedpop::Decode(&b[0], b.size(), &m) != 0) Fail("a trailing byte is malformed");
    if (!owedpop::EncodeKey(&b, owedpop::kOpClaim, owedpop::kKindBar, "Mongrel") || owedpop::Decode(&b[0], b.size(), &m) != 1 || m.op != owedpop::kOpClaim || m.key != "Mongrel" || m.kind != owedpop::kKindBar) Fail("CLAIM round trip");
    if (owedpop::EncodeKey(&b, owedpop::kOpDone, owedpop::kKindBar, "Mongrel")) Fail("EncodeKey carries CLAIM and RELEASE only");
    if (!owedpop::EncodeDone(&b, owedpop::kOpDone, owedpop::kKindResidents, r.key, owedpop::kWhyHas) || owedpop::Decode(&b[0], b.size(), &m) != 1 || m.op != owedpop::kOpDone || m.why != owedpop::kWhyHas) Fail("DONE round trip");
    if (owedpop::EncodeDone(&b, owedpop::kOpDone, owedpop::kKindResidents, r.key, owedpop::kWhyAway)) Fail("away is never sent up");
    if (!owedpop::EncodeDone(&b, owedpop::kOpGone, owedpop::kKindResidents, r.key, owedpop::kWhyAway) || owedpop::Decode(&b[0], b.size(), &m) != 1 || m.op != owedpop::kOpGone) Fail("GONE carries away");
    std::vector<owedpop::Row> rows; owedpop::Row a = r; a.claimant = (int)owedpop::kClaimYou; rows.push_back(a);
    owedpop::Row c = T581Row(owedpop::kKindBar, "Mongrel", "Mongrel"); c.claimant = (int)owedpop::kClaimOther; rows.push_back(c);
    if (!owedpop::EncodeRows(&b, rows) || owedpop::Decode(&b[0], b.size(), &m) != 1 || m.rows.size() != 2 || m.rows[0].claimant != (int)owedpop::kClaimYou || m.rows[1].claimant != (int)owedpop::kClaimOther || m.rows[1].kind != owedpop::kKindBar) Fail("ROWS round trip with each row's claim");
    if (owedpop::EncodeRows(&b, std::vector<owedpop::Row>())) Fail("an empty ROWS is not sent");
    owedpop::Row bad = r; bad.key = std::string("a\tb");
    if (owedpop::EncodeAdd(&b, bad)) Fail("a key with a control character is not a row");
    bad = r; bad.key = std::string(129, 'k');
    if (owedpop::EncodeAdd(&b, bad)) Fail("a key past 128 bytes is not a row");
    bad = r; bad.kind = 3;
    if (owedpop::EncodeAdd(&b, bad)) Fail("an unknown kind is not a row");
    const char junk[4] = { 9, 0, 0, 0 };
    if (owedpop::Decode(junk, 4, &m) != 0 || owedpop::Decode(0, 0, &m) != 0) Fail("an unknown op or an empty message is malformed");
}
void t_t581_table()
{
    owedpop::Table t;
    const owedpop::Row r = T581Row(owedpop::kKindResidents, "B1", "Mongrel");
    if (owedpop::TableAdd(&t, r) != 1 || owedpop::TableAdd(&t, r) != 0 || t.size() != 1) Fail("ADD stores once; a second ADD changes nothing");
    owedpop::Row bar = T581Row(owedpop::kKindBar, "B1", "Mongrel");
    if (owedpop::TableAdd(&t, bar) != 1 || t.size() != 2) Fail("a bar row and a residents row with the same key are two rows");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 1) != owedpop::kClaimGranted) Fail("the first asker is given the row");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 1) != owedpop::kClaimGranted) Fail("asking again for one's own row is still granted");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 2) != owedpop::kClaimHeld) Fail("a second game waits while the first holds it");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B9", 2) != owedpop::kClaimAbsent) Fail("no such row");
    if (owedpop::ClaimFor(1, 1) != (int)owedpop::kClaimYou || owedpop::ClaimFor(1, 2) != (int)owedpop::kClaimOther || owedpop::ClaimFor(0, 2) != (int)owedpop::kClaimNone) Fail("each game is told yours / another's / nobody's");
    if (owedpop::TableRelease(&t, owedpop::kKindResidents, "B1", 2) != 0) Fail("only the holder releases");
    if (owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 2, owedpop::kWhyFault) != owedpop::kDoneRefused) Fail("a game that does not hold the row may not say fault");
    if (owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 2, owedpop::kWhyGone) != -1) Fail("nor gone");
    if (owedpop::TableRelease(&t, owedpop::kKindResidents, "B1", 1) != 1 || owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 2) != owedpop::kClaimGranted) Fail("released, it is given to the next asker");
    if (owedpop::TablePeerGone(&t, 2).size() != 1 || t[owedpop::TableKey(owedpop::kKindResidents, "B1")].claimant != 0) Fail("a game that leaves loses the rows it held");
    if (owedpop::TableDone(&t, owedpop::kKindBar, "B1", 3, owedpop::kWhyHas) != 1 || t.size() != 1) Fail("any game may say has - the work is already there");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 3) != owedpop::kClaimGranted || owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 3, owedpop::kWhyMade) != 1 || !t.empty()) Fail("the holder's made removes the row");
    if (owedpop::TableDone(&t, owedpop::kKindResidents, "B1", 3, owedpop::kWhyMade) != 0) Fail("a second made finds no row");
    if (owedpop::TableClaim(&t, owedpop::kKindResidents, "B1", 0) != owedpop::kClaimHeld) Fail("a game with no slot is never given a row");
}
void t_t581_file()
{
    owedpop::Table t, u;
    owedpop::TableAdd(&t, T581Row(owedpop::kKindResidents, "54321-rebirth.mod|31,45|-21505,98762", "Mongrel"));
    owedpop::TableAdd(&t, T581Row(owedpop::kKindBar, "Mongrel", "Mongrel"));
    owedpop::TableClaim(&t, owedpop::kKindBar, "Mongrel", 4);
    std::string text;
    for (owedpop::Table::const_iterator it = t.begin(); it != t.end(); ++it) text += owedpop::Line(it->second);
    size_t at = 0; int ok = 0;
    while (at < text.size()) { const size_t nl = text.find('\n', at); ok += owedpop::ParseLine(text.substr(at, nl - at) + "\r", &u); at = nl + 1; }
    if (ok != 2 || u.size() != 2) Fail("both rows come back from their lines");
    const owedpop::Row& b = u[owedpop::TableKey(owedpop::kKindBar, "Mongrel")];
    if (b.claimant != 0 || b.sid != "Mongrel" || b.x != -2150.5f || b.z != 9876.25f) Fail("a row comes back nobody's, with its town and position");
    if (owedpop::ParseLine("v2\t1\tk\ts\t0\t0", &u) != 0 || owedpop::ParseLine("v1\t1\tk\ts\t0", &u) != 0 || owedpop::ParseLine("v1\t7\tk\ts\t0\t0", &u) != 0 || owedpop::ParseLine("v1\t1\tk\ts\tx\t0", &u) != 0) Fail("unusable lines are refused");
}
void t_t581_load_cause()
{
    if (owedpop::kCauseOwed != townreoffer::kDefOwed || owedpop::kCauseTestLever != townreoffer::kDefTestLever || owedpop::kCauseNone != townreoffer::kDefNone) Fail("the cause numbers agree with townreoffer.h");
    if (std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefOwed), "owed") != 0 || std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefTestLever), "test-lever") != 0) Fail("cause names owed / test-lever");
    if (owedpop::LoadCause(owedpop::kCauseNone, 1, 1, 0) != owedpop::kCauseOwed) Fail("a never-saved zone loaded again: owed residents this game would make are set aside, made under a claim");
    if (owedpop::LoadCause(owedpop::kCauseNone, 1, 0, 0) != owedpop::kCauseNone) Fail("nothing owed: made at load as before");
    if (owedpop::LoadCause(owedpop::kCauseNone, 1, 0, 1) != owedpop::kCauseTestLever) Fail("the lever sets aside what this game would make");
    if (owedpop::LoadCause(owedpop::kCauseNone, 0, 1, 1) != owedpop::kCauseNone) Fail("another game holds the area: the gate's own answer stands");
    if (owedpop::LoadCause(owedpop::kCauseNone, -2, 1, 1) != owedpop::kCauseNone) Fail("teardown: the gate's own answer stands");
    if (owedpop::LoadCause(townreoffer::kDefNoMap, -1, 1, 1) != townreoffer::kDefNoMap) Fail("a set-aside cause already given stands");
}
void t_t581_commit()
{
    using namespace owedpop;
    const int G = townreoffer::kReGenerate, O = townreoffer::kReOther, W = townreoffer::kReWait;
    const int Y = (int)kClaimYou, Nn = (int)kClaimNone, Ot = (int)kClaimOther;
    if (Commit(G, 1, 0, 0, Nn) != kActMake) Fail("work only this game knows, holder: made as before");
    if (Commit(G, 1, 0, 1, Nn) != kActClaim) Fail("an owed row nobody holds: ask for it first");
    if (Commit(G, 1, 0, 1, Ot) != kActWait) Fail("an owed row another game holds: wait");
    if (Commit(G, 1, 0, 1, Y) != kActMake) Fail("an owed row given to this game: make it");
    if (Commit(G, 1, 0, 2, Nn) != kActWait) Fail("sent and not back yet: wait for the row");
    if (Commit(G, 1, -1, 1, Y) != kActWait) Fail("residents unreadable: wait");
    if (Commit(G, 1, 1, 1, Nn) != kActDoneHas || Commit(G, 1, 1, 1, Y) != kActDoneHas || Commit(O, 1, 1, 2, Nn) != kActDoneHas) Fail("the residents are already there: DONE has, from any game");
    if (Commit(G, 1, 1, 1, Ot) != kActWait || Commit(O, 1, 1, 1, Ot) != kActWait) Fail("the residents are there but another game holds the row: wait - the holder settles it after its own save");
    if (Commit(O, 1, 1, 0, Nn) != kActForget) Fail("already there, only this game knew: settled here");
    if (Commit(O, 1, 0, 0, Nn) != kActForget || Commit(O, 1, -1, 0, Nn) != kActForget) Fail("another game holds it, only this game knew: settled as the gate settles it");
    if (Commit(O, 1, 0, 1, Nn) != kActWait || Commit(O, 1, 0, 1, Y) != kActWait) Fail("another game holds an owed row: this game never settles it - the holder makes it");
    if (Commit(W, 1, 0, 1, Y) != kActWait || Commit(W, 1, 0, 0, Nn) != kActWait) Fail("no answer about the area: wait");
    if (Commit(G, 0, -1, 0, Nn) != kActForget) Fail("a building gone, only this game knew: forgotten");
    if (Commit(G, 0, -1, 1, Y) != kActDoneGone) Fail("a building gone, the row is this game's: DONE gone");
    if (Commit(G, 0, -1, 1, Nn) != kActClaim || Commit(O, 0, -1, 1, Nn) != kActWait || Commit(G, 0, -1, 1, Ot) != kActWait) Fail("a building gone, the row is not this game's: the holder of the area says so");
    if (Commit(G, -1, -1, 1, Y) != kActWait) Fail("an incomplete search: wait");
    if (KeepClaim(1, 1, 0) != 1 || KeepClaim(0, 1, 0) != 0 || KeepClaim(1, 0, 0) != 0 || KeepClaim(1, -1, 0) != 0 || KeepClaim(1, 1, 1) != 0) Fail("a row is kept only while this game holds its area with the zone live and the lever off");
}
/* two games and the world server, through the same table and decisions both sides compile. A and B are slots 0 and 1 (claimants 1, 2). */
void t_t581_two_games()
{
    using namespace owedpop;
    const int G = townreoffer::kReGenerate, O = townreoffer::kReOther;
    Table srv;
    const std::string key = "54321-rebirth.mod|31,45|-21505,98762";
    /* A's gate set the residents aside (no fresh area map); its ADD reaches the world server */
    if (TableAdd(&srv, T581Row(kKindResidents, key.c_str(), "Mongrel")) != 1) Fail("stored");
    /* racing: both games think they hold the area (a stale map) and ask - one is given it, the other waits */
    int makes = 0;
    const int ca = TableClaim(&srv, kKindResidents, key, 1), cb = TableClaim(&srv, kKindResidents, key, 2);
    if (ca != kClaimGranted || cb != kClaimHeld) Fail("racing: the first asker is given it, the second waits");
    const int claimA = ClaimFor(srv[TableKey(kKindResidents, key)].claimant, 1), claimB = ClaimFor(srv[TableKey(kKindResidents, key)].claimant, 2);
    if (Commit(G, 1, 0, 1, claimA) == kActMake) ++makes;
    if (Commit(G, 1, 0, 1, claimB) == kActMake) ++makes;
    if (makes != 1) Fail("racing: exactly one game makes the residents");
    if (TableDone(&srv, kKindResidents, key, 1, kWhyMade) != 1 || !srv.empty()) Fail("racing: the maker's DONE removes the row");
    if (TableDone(&srv, kKindResidents, key, 2, kWhyMade) != 0) Fail("racing: nothing is left for the other game");
    /* holder change: A is given the row, its player walks away - it hands the row back; B now holds the area and makes it */
    TableAdd(&srv, T581Row(kKindResidents, key.c_str(), "Mongrel"));
    TableClaim(&srv, kKindResidents, key, 1);
    if (KeepClaim(1, 0, 0) != 0) Fail("holder change: A no longer holds the area - it hands the row back");
    TableRelease(&srv, kKindResidents, key, 1);
    if (Commit(O, 1, 0, 1, ClaimFor(srv[TableKey(kKindResidents, key)].claimant, 1)) != kActWait) Fail("holder change: A never makes it while B holds the area");
    if (Commit(G, 1, 0, 1, ClaimFor(srv[TableKey(kKindResidents, key)].claimant, 2)) != kActClaim) Fail("holder change: B asks");
    if (TableClaim(&srv, kKindResidents, key, 2) != kClaimGranted || Commit(G, 1, 0, 1, ClaimFor(srv[TableKey(kKindResidents, key)].claimant, 2)) != kActMake) Fail("holder change: B is given it and makes it");
    if (TableDone(&srv, kKindResidents, key, 1, kWhyMade) != kDoneMadeAsHas || !srv.empty()) Fail("holder change: A's late made is taken as has - the people exist, and B never makes a second population");
    TableDone(&srv, kKindResidents, key, 2, kWhyMade);
    /* a crash while holding a row: the link ends, the row is nobody's again and the other game is given it */
    TableAdd(&srv, T581Row(kKindResidents, key.c_str(), "Mongrel"));
    TableClaim(&srv, kKindResidents, key, 1);
    if (TablePeerGone(&srv, 1).size() != 1 || TableClaim(&srv, kKindResidents, key, 2) != kClaimGranted) Fail("crash: the row goes to the game still here");
    srv.clear();
    /* a zone saved without its residents, then the world reloaded later: the row survives the world server's own restart (its
       file) and the reloaded game - zone loaded from its save, nothing set aside in memory - finds it at its town check-up */
    TableAdd(&srv, T581Row(kKindResidents, key.c_str(), "Mongrel"));
    TableClaim(&srv, kKindResidents, key, 1);
    Table disk; ParseLine(Line(srv[TableKey(kKindResidents, key)]), &disk);
    if (disk.size() != 1 || disk.begin()->second.claimant != 0) Fail("reload: the row is on disk and nobody's after a restart");
    if (LoadCause(kCauseNone, 1, 1, 0) != kCauseOwed) Fail("reload: a game that loads the zone fresh sets the owed residents aside rather than make them unclaimed");
    if (Commit(G, 1, 0, 1, ClaimFor(disk.begin()->second.claimant, 1)) != kActClaim) Fail("reload: the holder asks for the row");
    if (TableClaim(&disk, kKindResidents, key, 1) != kClaimGranted || Commit(G, 1, 0, 1, (int)kClaimYou) != kActMake) Fail("reload: given it, the holder makes the residents");
    if (TableDone(&disk, kKindResidents, key, 1, kWhyMade) != 1 || !disk.empty()) Fail("reload: made once, the row is gone");
    /* the other game already shows residents (copies of the maker's) while the maker holds the row: it waits (the maker settles the
       row after its own save); once nobody holds the row, any game that sees the residents ends it */
    TableAdd(&disk, T581Row(kKindResidents, key.c_str(), "Mongrel"));
    TableClaim(&disk, kKindResidents, key, 1);
    if (Commit(O, 1, 1, 1, ClaimFor(disk.begin()->second.claimant, 2)) != kActWait) Fail("has: a game does not end a row another game holds");
    TablePeerGone(&disk, 1);
    if (Commit(O, 1, 1, 1, ClaimFor(disk.begin()->second.claimant, 2)) != kActDoneHas || TableDone(&disk, kKindResidents, key, 2, kWhyHas) != 1) Fail("has: once nobody holds the row, any game that sees the residents ends it");
}
/* T-589: MADE PEOPLE WAIT FOR THE MAKER'S OWN SAVE (src/common/owedpop.h). A and B are slots 0 and 1 (claimants 1, 2); `seq` is
   the count of save requests the engine had accepted when a make was noted, `saveReq` a finished save's own request number. */
static int T589Make(owedpop::SettleSet* s, owedpop::Table* srv, int who, int settling, int has, long seq, int* makes)
{
    using namespace owedpop;
    const std::string key = "54321-rebirth.mod|31,45|-21505,98762";
    const Table::const_iterator r = srv->find(TableKey(kKindResidents, key));
    const int claim = r == srv->end() ? (int)kClaimNone : ClaimFor(r->second.claimant, who);
    const int act = Checkup(settling, townreoffer::kReGenerate, 1, has, r == srv->end() ? 0 : 1, claim);
    if (act == kActMake) { ++*makes; SettleNote(s, kKindResidents, key, seq, claim == (int)kClaimYou ? 1 : 0); }
    return act;
}
void t_t589_settle()
{
    using namespace owedpop;
    const std::string key = "54321-rebirth.mod|31,45|-21505,98762", tk = TableKey(kKindResidents, key);
    /* the pure pieces */
    { SettleSet x; SettleNote(&x, kKindResidents, key, 2, 0); SettleNote(&x, kKindResidents, key, 7, 1);
      if (x.size() != 1 || x[tk].seq != 7 || x[tk].held != 1) Fail("a second make of one row keeps the later request count and held");
      SettleNote(&x, kKindBar, "Mongrel", 3, 0);
      if (SaveCovers(7, 7) != 0 || SaveCovers(7, 8) != 1) Fail("a save covers a make only when it was asked for after it");
      const std::vector<Settle> d = SettleDue(&x, 5);
      if (d.size() != 1 || d[0].kind != kKindBar || x.size() != 1) Fail("a finished save settles the makes it covers and leaves the later ones");
      if (SettleStep(0, 1, (int)kClaimYou) != kSetNone || SettleStep(1, 1, (int)kClaimYou) != kSetHold || SettleStep(1, 1, (int)kClaimOther) != kSetHold
          || SettleStep(1, 1, (int)kClaimNone) != kSetReclaim || SettleStep(1, 0, (int)kClaimNone) != kSetHold) Fail("settling: held wherever the player goes; a made row read nobody's is asked for again");
      if (Checkup(1, townreoffer::kReGenerate, 1, 1, 1, (int)kClaimYou) != kActWait || Checkup(1, townreoffer::kReGenerate, 1, 0, 1, (int)kClaimYou) != kActWait
          || Checkup(1, townreoffer::kReGenerate, 1, 0, 1, (int)kClaimNone) != kActWait) Fail("settling: the check-up neither says has, nor makes again, nor asks");
      if (Checkup(0, townreoffer::kReGenerate, 1, 0, 1, (int)kClaimYou) != kActMake) Fail("not settling: Commit decides"); }
    /* THE REPORTED CRASH: A makes the people at 1597 s, its last save was asked for earlier (request 4), A dies at 1665 s */
    Table srv; SettleSet a; int makes = 0;
    TableAdd(&srv, T581Row(kKindResidents, key.c_str(), "Mongrel"));
    if (TableClaim(&srv, kKindResidents, key, 1) != kClaimGranted || T589Make(&a, &srv, 1, 0, 0, 4, &makes) != kActMake) Fail("crash: A is given the row and makes it");
    if (srv.size() != 1 || srv[tk].claimant != 1 || a.size() != 1) Fail("crash: made, the row stays on the world server and A's - no DONE yet");
    if (SettleStep(1, a[tk].held, ClaimFor(srv[tk].claimant, 1)) != kSetHold || KeepClaim(0, 0, 0) != 0) Fail("crash: A keeps the row when its player walks away (KeepClaim alone would hand it back)");
    if (T589Make(&a, &srv, 1, 1, 1, 4, &makes) != kActWait || makes != 1) Fail("crash: A's next check-up leaves the settling row alone - no has, no second make");
    if (!SettleDue(&a, 4).empty() || a.size() != 1) Fail("crash: a save asked for before the make (request 4) does not settle it");
    { Table s2 = srv; SettleSet a2;   /* A dies: the world server sees it leave; A's second life has no settle set and its save has no people */
      if (TablePeerGone(&s2, 1).size() != 1 || s2[tk].claimant != 0) Fail("crash: the dead game's row is nobody's again");
      int m2 = 0;
      if (T589Make(&a2, &s2, 1, 0, 0, 0, &m2) != kActClaim || TableClaim(&s2, kKindResidents, key, 1) != kClaimGranted) Fail("crash: A, back, asks for the row and is given it");
      if (T589Make(&a2, &s2, 1, 0, 0, 0, &m2) != kActMake || T589Make(&a2, &s2, 1, 1, 1, 0, &m2) != kActWait || m2 != 1) Fail("crash: A, back, makes the people again - exactly once");
      const std::vector<Settle> d = SettleDue(&a2, 1);   /* A's first save in its second life */
      if (d.size() != 1 || TableDone(&s2, kKindResidents, key, 1, kWhyMade) != kDoneRemoved || !s2.empty()) Fail("crash: A's next finished save settles the row"); }
    /* A RELOAD IN THE SAME PROCESS (no crash): the world closes - the unsaved make is forgotten (TownGenWorldTeardown) - and the
       older save loads with no people; the row is still A's, so A makes it again, once */
    { Table s3 = srv; SettleSet a3; int m3 = 0;
      if (T589Make(&a3, &s3, 1, 0, 0, 5, &m3) != kActMake || T589Make(&a3, &s3, 1, 1, 1, 5, &m3) != kActWait || m3 != 1) Fail("reload: made again exactly once");
      if (!SettleDue(&a3, 5).empty() || SettleDue(&a3, 6).size() != 1) Fail("reload: settled by the first save asked for after the new make"); }
    /* TWO GAMES: A made and has not saved; its player walks away and B now holds the area. B never makes it again and never ends it
       (even if it shows copies of A's people); A's save settles it and B hears it gone. */
    { Table s4 = srv; SettleSet a4 = a, b4; int mb = 0;
      if (T589Make(&b4, &s4, 2, 0, 0, 9, &mb) != kActWait || T589Make(&b4, &s4, 2, 0, 1, 9, &mb) != kActWait || mb != 0) Fail("two games: B neither makes nor ends a row A holds");
      const std::vector<Settle> d = SettleDue(&a4, 5);   /* A's own save, asked for after its make */
      if (d.size() != 1 || TableDone(&s4, kKindResidents, key, 1, kWhyMade) != kDoneRemoved || !s4.empty()) Fail("two games: A's save settles the row");
    }
    /* TWO GAMES, A DIES FIRST: the row is nobody's; B holds the area: it makes the people (no copies there) or ends the row (copies there) */
    { Table s5 = srv; int mb = 0; SettleSet b5;
      TablePeerGone(&s5, 1);
      if (T589Make(&b5, &s5, 2, 0, 0, 9, &mb) != kActClaim || TableClaim(&s5, kKindResidents, key, 2) != kClaimGranted || T589Make(&b5, &s5, 2, 0, 0, 9, &mb) != kActMake || mb != 1)
          Fail("A dies first: B is given the row and makes the people once");
      Table s6 = srv; TablePeerGone(&s6, 1);
      if (Commit(townreoffer::kReGenerate, 1, 1, 1, ClaimFor(s6[tk].claimant, 2)) != kActDoneHas) Fail("A dies first: B, already showing residents there, ends the row"); }
    /* T-630 fold (review MED 1, LOW 5): A LINK CLOSE IS NOT A LEAVE. A closed game's rows stay its own through their OWN release hold
       (RowHoldDecide; kRowHoldSec = the world server's kRestoreGraceSec, 60 s - a re-dial takes about 17 s, the 10 s PLAYER_GONE hold
       does not cover one); RowHoldApply releases them only when it runs out with the player still away. A's link closes at 100 s;
       A made the people and has not saved. */
    const double t630Hold = kRowHoldSec;
    if (t630Hold != 60.0 || t630Hold < 3.0 * 17.0 || t630Hold <= cooppg::kPlayerGoneHoldSec) Fail("T-630: the row hold is 60 s - three re-dials, past the 10 s PLAYER_GONE hold");
    if (RowHoldDecide(100.0, 100.0, t630Hold, 0) != kRowHoldWait || RowHoldDecide(100.0, 159.5, t630Hold, 0) != kRowHoldWait
        || RowHoldDecide(100.0, 160.0, t630Hold, 0) != kRowHoldRelease || RowHoldDecide(100.0, 130.0, t630Hold, 1) != kRowHoldKeep)
        Fail("T-630: the row hold waits 60 s, keeps the rows for a player admitted again, releases them after");
    if (TableHeldBy(srv, 1) != 1 || TableHeldBy(srv, 2) != 0 || TableHeldBy(srv, 0) != 0) Fail("T-630: the rows a closed game holds are counted for the close's line");
    /* (a) A's link drops; the PLAYER_GONE news goes at 10 s but the rows wait; B asks during the hold and is told the row is A's, so
       B waits; A's re-dial is admitted at 34 s (its second try): kept. A's later DONE made ends the row normally */
    { Table s7 = srv; SettleSet a7 = a, b7; int m7 = 0, mb = 0;
      if (cooppg::PlayerGoneHoldDecide(100.0, 110.0, cooppg::kPlayerGoneHoldSec, false) != cooppg::kPgHoldSend) Fail("T-630 (a): the PLAYER_GONE news still goes after its own 10 s");
      int act = RowHoldDecide(100.0, 110.0, t630Hold, 0);
      if (act != kRowHoldWait || !RowHoldApply(&s7, 1, act).empty() || ClaimFor(s7[tk].claimant, 1) != (int)kClaimYou) Fail("T-630 (a): at 10 s the row hold still waits - the row is A's");
      if (T589Make(&b7, &s7, 2, 0, 0, 9, &mb) != kActWait || TableClaim(&s7, kKindResidents, key, 2, 120.0) != kClaimHeld || mb != 0) Fail("T-630 (a): B, asking during the row hold, is told the row is A's and waits");
      act = RowHoldDecide(100.0, 134.0, t630Hold, 1);
      if (act != kRowHoldKeep || !RowHoldApply(&s7, 1, act).empty() || ClaimFor(s7[tk].claimant, 1) != (int)kClaimYou) Fail("T-630 (a): A admitted again at 34 s - kept, nothing released");
      if (SettleStep(1, a7[tk].held, ClaimFor(s7[tk].claimant, 1)) != kSetHold || T589Make(&a7, &s7, 1, 1, 1, 5, &m7) != kActWait || m7 != 0) Fail("T-630 (a): A, back, still reads the row as its own - no claim again, no second make");
      const std::vector<Settle> d = SettleDue(&a7, 5);   /* A's own save, asked for after its make */
      if (d.size() != 1 || TableDone(&s7, kKindResidents, key, 1, kWhyMade) != kDoneRemoved || !s7.empty() || mb != 0) Fail("T-630 (a): A's save settles the row - DONE made ends it; B never made the people"); }
    /* (b) the row hold runs out with A still away: the row is nobody's; B is given it and makes the people once */
    { Table s8 = srv; SettleSet b8; int mb = 0;
      int act = RowHoldDecide(100.0, 159.5, t630Hold, 0);
      if (act != kRowHoldWait || !RowHoldApply(&s8, 1, act).empty() || TableHeldBy(s8, 1) != 1) Fail("T-630 (b): before the row hold runs out nothing is released");
      act = RowHoldDecide(100.0, 160.0, t630Hold, 0);
      if (act != kRowHoldRelease || RowHoldApply(&s8, 1, act).size() != 1 || s8[tk].claimant != 0) Fail("T-630 (b): released after the 60 s hold - the row is nobody's");
      if (T589Make(&b8, &s8, 2, 0, 0, 9, &mb) != kActClaim || TableClaim(&s8, kKindResidents, key, 2) != kClaimGranted || T589Make(&b8, &s8, 2, 0, 0, 9, &mb) != kActMake
          || T589Make(&b8, &s8, 2, 1, 1, 9, &mb) != kActWait || mb != 1) Fail("T-630 (b): B is given the row and makes the people once"); }
    /* (c) A crashes and its restarted game (same profile, same slot) is admitted within the hold (at 45 s): the row is still A's and
       the second life has no settle entry for it (the unsaved make died with the crash), so SettleStep says not settling and KeepClaim
       decides. Holding the area live, it makes the people once (or ends the row, its save showing them); not holding it, it hands the
       row back and B makes them once. Never two makes. */
    { Table s9 = srv; SettleSet a9, b9; int m9 = 0, mb = 0;
      const int act = RowHoldDecide(100.0, 145.0, t630Hold, 1);
      if (act != kRowHoldKeep || !RowHoldApply(&s9, 1, act).empty() || ClaimFor(s9[tk].claimant, 1) != (int)kClaimYou
          || SettleStep(0, 0, (int)kClaimYou) != kSetNone) Fail("T-630 (c): the restarted game reads the row as its own, and not as settling");
      if (KeepClaim(1, 1, 0) != 1 || T589Make(&a9, &s9, 1, 0, 0, 0, &m9) != kActMake || T589Make(&a9, &s9, 1, 1, 1, 0, &m9) != kActWait
          || T589Make(&b9, &s9, 2, 0, 0, 9, &mb) != kActWait || m9 + mb != 1) Fail("T-630 (c): holding the area, the restarted game makes the people once and B waits");
      if (Checkup(0, townreoffer::kReGenerate, 1, 1, 1, (int)kClaimYou) != kActDoneHas) Fail("T-630 (c): a restarted game whose save shows the people ends the row (DONE has) - no make");
      Table s10 = srv; SettleSet a10, b10; int m10 = 0, mb10 = 0;
      if (!RowHoldApply(&s10, 1, act).empty() || KeepClaim(0, 0, 0) != 0 || TableRelease(&s10, kKindResidents, key, 1) != 1) Fail("T-630 (c): not holding the area, the restarted game hands the row back");
      if (T589Make(&b10, &s10, 2, 0, 0, 9, &mb10) != kActClaim || TableClaim(&s10, kKindResidents, key, 2) != kClaimGranted || T589Make(&b10, &s10, 2, 0, 0, 9, &mb10) != kActMake
          || T589Make(&a10, &s10, 1, 0, 0, 0, &m10) != kActWait || m10 + mb10 != 1) Fail("T-630 (c): after the hand-back B makes the people once and A waits"); }
    /* (d) AN OUTAGE LONGER THAN THE 60 s ROW HOLD. Within it ((a) above) this is no longer a limit. Past it the world server released
       the row: when nobody took it, A, back, asks for its made row again and is given it. KNOWN LIMIT, not a pass: when B, holding the
       area, claimed it in the outage, B made the people too; A keeps waiting and its later DONE made still ends the row. This STILL
       MAKES TWO POPULATIONS (A's unsaved make and B's) - never none. */
    { Table s11 = srv;
      const int act = RowHoldDecide(100.0, 175.0, t630Hold, 0);
      if (act != kRowHoldRelease || RowHoldApply(&s11, 1, act).size() != 1) Fail("T-630 (d): past the row hold the row is released");
      if (SettleStep(1, a[tk].held, ClaimFor(s11[tk].claimant, 1)) != kSetReclaim || TableClaim(&s11, kKindResidents, key, 1) != kClaimGranted) Fail("T-630 (d): back after the hold, nobody took the row: A asks for its made row again and is given it");
      Table s12 = srv; RowHoldApply(&s12, 1, act); SettleSet b12; int mb = 0;
      if (T589Make(&b12, &s12, 2, 0, 0, 9, &mb) != kActClaim || TableClaim(&s12, kKindResidents, key, 2) != kClaimGranted || T589Make(&b12, &s12, 2, 0, 0, 9, &mb) != kActMake || mb != 1)
          Fail("T-630 (d): past the row hold B is given the row and makes the people");
      if (SettleStep(1, 1, ClaimFor(s12[tk].claimant, 1)) != kSetHold || TableDone(&s12, kKindResidents, key, 1, kWhyMade) != kDoneMadeAsHas)
          Fail("T-630 (d) KNOWN LIMIT: past the row hold A's late made still ends the row - two populations, never none"); }
    /* WHICH SAVE COUNTS: the quick.save must be this save's own; a second request to the same folder shares the first one's finish */
    { const unsigned long long req = 133000000000000000ULL;
      if (SaveIsThisOne(1, req + 50000000ULL, req) != 1 || SaveIsThisOne(1, req - 10000000ULL, req) != 1) Fail("a quick.save written after the request (2 s of slack) is this save's");
      if (SaveIsThisOne(1, req - 600000000ULL, req) != 0) Fail("a failed save leaves the old quick.save: it settles nothing");
      if (SaveIsThisOne(0, req, req) != 0 || SaveIsThisOne(1, req, 0) != 0) Fail("unreadable file or unknown request time: settles nothing");
      if (SaveCreditSeq(1, 4, 5) != 4 || SaveCreditSeq(0, 4, 5) != 5 || SaveCreditSeq(1, 0, 5) != 5) Fail("a refreshed watch row keeps the first request's number");
      if (SaveCreditFt(1, req, req + 9) != req || SaveCreditFt(0, req, req + 9) != req + 9) Fail("a refreshed watch row keeps the first request's time");
      SettleSet y; SettleNote(&y, kKindResidents, key, 4, 1);   /* made between request 4 (save 1) and request 5 (save 2, same folder) */
      if (!SettleDue(&y, SaveCreditSeq(1, 4, 5)).empty()) Fail("the shared finish is credited to the first request: the make between them waits for a later save"); }
    /* THE WORLD CLOSES while a save asked for after the make is still being written: kept until that save ends */
    { if (SettleKeep(0, 4, 0) != 1) Fail("an open world keeps its makes");
      if (SettleKeep(1, 4, 5) != 1 || SettleKeep(1, 4, 4) != 0 || SettleKeep(1, 4, 0) != 0) Fail("a closed world's make is kept only while a save asked for after it is being written");
      SettleSet z; SettleNote(&z, kKindResidents, key, 4, 1); z[tk].closed = 1; z[tk].closeSeq = 5;   /* closed while request 5 was being written */
      if (SettleKeep(z[tk].closed, z[tk].seq, 5) != 1 || SettleDue(&z, 5).size() != 1) Fail("the covering save finishes: it settles the closed world's make");
      SettleSet w; SettleNote(&w, kKindResidents, key, 4, 1); w[tk].closed = 1; w[tk].closeSeq = 5;
      if (SaveSettles(w[tk], 6) != 0 || !SettleDue(&w, 6).empty() || w.size() != 1) Fail("a save asked for after the close (another world's) never settles the closed world's make");
      if (SaveSettles(w[tk], 4) != 0 || SaveSettles(w[tk], 5) != 1) Fail("a closed world's make settles only from a save asked for between the make and the close");
      Settle open; open.seq = 4; if (SaveSettles(open, 9) != 1) Fail("an open world's make settles from any later save"); }
    /* AFTER THE SAVE: DONE made is owed until GONE - the check-up holds the row back meanwhile (no second make when the people were
       killed since), it is sent once per link, sent again on a new link while the row is still there, and dropped if it is not */
    { if (Checkup(1, townreoffer::kReGenerate, 1, 0, 1, (int)kClaimYou) != kActWait) Fail("DONE owed: the row's people killed since - still no second make");
      if (DoneStep(0, 7, 1) != kDoneSend || DoneStep(7, 7, 1) != kDoneKeep || DoneStep(7, 7, 0) != kDoneKeep) Fail("DONE owed: sent once on a link");
      if (DoneStep(7, 8, 1) != kDoneSend || DoneStep(0, 8, 1) != kDoneSend) Fail("DONE owed: a new link with the row still there - sent again (the link was down at the save, or the send was lost)");
      if (DoneStep(7, 8, 0) != kDoneDrop || DoneStep(0, 8, 0) != kDoneDrop) Fail("DONE owed: the row is gone from a new link's table - settled while away"); }
}
/* T-581 + T-580: what a residents re-run that ran leaves, from T-580's per-run counts */
void t_t581_after_rerun()
{
    using namespace owedpop;
    if (AfterRerun(2, townpending::RerunKeep(2, 0), 0, 1, 0) != kRunDoneMade) Fail("residents made under a grant: DONE made");
    if (AfterRerun(2, townpending::RerunKeep(2, 1), 0, 1, 1) != kRunDoneMade) Fail("some made, some refused: DONE made");
    if (AfterRerun(0, townpending::RerunKeep(0, 0), 0, 1, 0) != kRunDoneEmpty) Fail("the engine chose none and nothing refused: DONE empty, never made");
    if (AfterRerun(0, townpending::RerunKeep(0, 0), 0, 1, 2) != kRunRelease) Fail("none made, refused for another cause: handed back, not counted - like a lost hold");
    if (AfterRerun(0, townpending::RerunKeep(0, 3), 0, 1, 3) != kRunKeep) Fail("decision 34 refused them all: kept, the owed row stays (no DONE)");
    if (AfterRerun(0, 1, townpending::KeptGiveUp(townpending::kMaxKeptReruns), 1, 3) != kRunGiveUpRelease) Fail("given up at the bound: handed back, never removed");
    if (AfterRerun(0, 1, townpending::KeptGiveUp(townpending::kMaxKeptReruns - 1), 1, 3) != kRunKeep) Fail("under the bound: still kept");
    if (AfterRerun(0, 1, 1, 0, 1) != kRunGiveUpLocal || AfterRerun(3, 0, 0, 0, 0) != kRunLocalDone || AfterRerun(0, 0, 0, 0, 2) != kRunLocalDone) Fail("only this game knew it: nothing is sent");
    Table t; TableAdd(&t, T581Row(kKindResidents, "E1", "X"));
    if (TableDone(&t, kKindResidents, "E1", 1, kWhyEmpty, 100.0) != kDoneRefused) Fail("empty only from the game holding the row");
    TableClaim(&t, kKindResidents, "E1", 1, 100.0);
    if (TableDone(&t, kKindResidents, "E1", 1, kWhyEmpty, 100.0) != kDoneCounted || t.size() != 1) Fail("empty from the holder is counted and handed back, like gone and fault");
    if (TableClaim(&t, kKindResidents, "E1", 2, 100.0 + kRetrySpacingSec - 1.0) != kClaimLater) Fail("spaced: not given again for 5 minutes");
    if (TableClaim(&t, kKindResidents, "E1", 2, 100.0 + kRetrySpacingSec) != kClaimGranted) Fail("given again after the spacing");
    std::vector<char> b; Msg m;
    if (!EncodeDone(&b, kOpDone, kKindResidents, "E1", kWhyEmpty) || Decode(&b[0], b.size(), &m) != 1 || m.why != kWhyEmpty) Fail("DONE empty goes up");
    if (EncodeDone(&b, kOpDone, kKindResidents, "E1", kWhyFull)) Fail("full is never sent up");
    if (!EncodeDone(&b, kOpGone, kKindResidents, "E1", kWhyFull) || Decode(&b[0], b.size(), &m) != 1 || GoneKeepsLocal(m.why) != 1 || GoneKeepsLocal(kWhyMade) != 0) Fail("GONE full: the game keeps its work and makes it as before");
    if (AskAgain(1, kReaskMs - 1) != 0 || AskAgain(1, kReaskMs) != 1 || AskAgain(0, 999999) != 0) Fail("a claim with no answer is asked again after 30 s; a release never");
    if (CountedWhy(kWhyEmpty) != 1 || CountedWhy(kWhyGone) != 1 || CountedWhy(kWhyFault) != 1 || CountedWhy(kWhyMade) != 0 || CountedWhy(kWhyHas) != 0) Fail("gone, fault and empty are the counted reports");
}
/* T-581 fold 1: settling work whose row left while this game was away, gone / fault retries, the per-game cap, the bar's done test */
void t_t581_fold()
{
    using namespace owedpop;
    if (FlushStep(0, 5, 0, 0) != kFlushSend) Fail("never sent: send");
    if (FlushStep(5, 5, 0, 0) != kFlushKeep || FlushStep(-1, 5, 0, 1) != kFlushKeep) Fail("sent on this link, or not sendable: nothing");
    if (FlushStep(4, 5, 1, 1) != kFlushMark) Fail("its row is in the new table: counted as sent, not sent again");
    if (FlushStep(4, 5, 0, 1) != kFlushSettle) Fail("its row was in the table and is gone from the new one: settled, never sent again");
    if (FlushStep(4, 5, 0, 0) != kFlushSend) Fail("sent but never seen in the table (the link dropped first): sent again");
    Table t; const std::string key = "B1";
    double now = 1000.0;
    TableAdd(&t, T581Row(kKindResidents, "B1", "Mongrel"));
    for (int i = 1; i < kTriesMax; ++i)
    {
        if (TableClaim(&t, kKindResidents, key, 1, now) != kClaimGranted) Fail("handed back after a fault: given again once the spacing ran");
        if (TableDone(&t, kKindResidents, key, 1, kWhyFault, now) != kDoneCounted || t[TableKey(kKindResidents, key)].claimant != 0) Fail("a fault is counted and the row handed back, kept");
        if (TableClaim(&t, kKindResidents, key, 1, now + 1.0) != kClaimLater) Fail("a fault is retried, but not within seconds");
        now += kRetrySpacingSec;
    }
    TableClaim(&t, kKindResidents, key, 2, now);
    if (TableDone(&t, kKindResidents, key, 2, kWhyGone, now) != kDoneCounted) Fail("a gone report is counted apart from the faults");
    now += kRetrySpacingSec;
    TableClaim(&t, kKindResidents, key, 2, now);
    if (TableDone(&t, kKindResidents, key, 2, kWhyFault, now) != kDoneRemoved || !t.empty()) Fail("the third fault removes the row");
    TableAdd(&t, T581Row(kKindResidents, "B2", "Mongrel"));
    for (int i = 0; i < kTriesMax; ++i) { now += kRetrySpacingSec; TableClaim(&t, kKindResidents, "B2", 1, now); TableDone(&t, kKindResidents, "B2", 1, kWhyGone, now); }
    if (!t.empty()) Fail("the third gone removes the row");
    if (TableAddFrom(&t, T581Row(kKindResidents, "C1", "X"), 1, 2) != 1 || TableAddFrom(&t, T581Row(kKindResidents, "C2", "X"), 1, 2) != 1) Fail("rows under the cap are stored");
    if (TableAddFrom(&t, T581Row(kKindResidents, "C3", "X"), 1, 2) != kAddFull || t.size() != 2) Fail("a game at its cap is refused");
    if (TableAddFrom(&t, T581Row(kKindResidents, "C1", "X"), 1, 2) != 0) Fail("a row already stored is unchanged, not refused");
    if (TableAddFrom(&t, T581Row(kKindResidents, "C3", "X"), 2, 2) != 1) Fail("the cap is per game");
    if (kRowsPerGame != 2048 || kTriesMax != 3) Fail("the cap is 2048 rows a game and a row lives through three gone or three fault reports");
    if (BarHas(1, 0, -2, 0, 0) != 1) Fail("a list used up on this town object counts at once");
    if (BarHas(0, 1, 1, 1, 0) != 1 || BarHas(0, 1, 0, 0, 1) != 1) Fail("a fill elsewhere or a removed row counts with the zone live");
    if (BarHas(0, 0, 1, 1, 1) != 0 || BarHas(0, 1, -2, 1, 1) != 0) Fail("zone not live, or the world being freed: wait, as before");
}
/* T-438 (t438-test): a set-aside building still runs populateBuilding (faction, furniture) with its squads refused as SET ASIDE */
void t_t438_residents_aside()
{
    if (townreoffer::PopulateMode(1, 0) != townreoffer::kPopRunAside) Fail("set aside at load - the engine call still runs, squads set aside");
    if (townreoffer::PopulateMode(1, 1) != townreoffer::kPopRun) Fail("answered at load - the engine call runs as normal");
    if (townreoffer::PopulateMode(0, 1) != townreoffer::kPopRun) Fail("a re-offer the gate answers runs the engine call to make the squads");
    if (townreoffer::PopulateMode(0, 0) != townreoffer::kPopSkip) Fail("a re-offer set aside again does not run the engine call a second time");
    if (townreoffer::InBuildingSquad(1, 1) != townreoffer::kSqSetAside) Fail("flag raised in this thread's building call - refused as set aside, not as not-holder");
    if (townreoffer::InBuildingSquad(1, 0) != townreoffer::kSqNormal) Fail("flag clear - the normal creation gate decides");
    if (townreoffer::InBuildingSquad(0, 1) != townreoffer::kSqNormal) Fail("no building context on this thread - the flag is not this creation's");
    if (townreoffer::InBuildingSquad(0, 0) != townreoffer::kSqNormal) Fail("no building call - normal");
    if (std::strcmp(townreoffer::FurnitureName(2), "loaded") != 0) Fail("furniture name loaded");
    if (std::strcmp(townreoffer::FurnitureName(1), "asked") != 0) Fail("furniture name asked");
    if (std::strcmp(townreoffer::FurnitureName(0), "no-interior") != 0) Fail("furniture name no-interior");
    if (std::strcmp(townreoffer::FurnitureName(-1), "unread") != 0) Fail("furniture name unread");
    /* t438-a-test: only a re-offer that GENERATES runs populateBuilding again */
    if (townreoffer::ReofferRunsPopulate(townreoffer::kReGenerate) != 1) Fail("generate - the engine call runs again to make the squads");
    if (townreoffer::ReofferRunsPopulate(townreoffer::kReOther) != 0) Fail("another game holds it - erased, no second engine call");
    if (townreoffer::ReofferRunsPopulate(townreoffer::kReWait) != 0) Fail("waiting - no engine call");
    if (townreoffer::ReofferRunsPopulate(townreoffer::kReFilled) != 0) Fail("filled - no engine call");
    /* t438-b-test: a generate re-run undoes exactly its own add to the town's resident budget, only on an equal positive pair */
    if (townreoffer::RerunPopUndo(5, 5.0) != 5) Fail("int and float both moved by 5 - undo 5");
    if (townreoffer::RerunPopUndo(5, 5.004) != 5) Fail("T-438 r1: a float that rounded within a step still counts as the add");
    if (townreoffer::RerunPopUndo(3, 0.0) != 0) Fail("T-438 r1: the int rose but the float did not - a mismatch, nothing undone");
    if (townreoffer::RerunPopUndo(5, 4.0) != 0) Fail("int and float moved differently - mismatch, leave alone");
    if (townreoffer::RerunPopUndo(5, 5.5) != 0) Fail("float moved by a fraction - mismatch, leave alone");
    if (townreoffer::RerunPopUndo(0, 0.0) != 0) Fail("nothing added - nothing to undo");
    if (townreoffer::RerunPopUndo(-3, -3.0) != 0) Fail("the budget went down - never undo a decrease");
    /* T-515: the building-state step a set-aside squad still takes, and the re-offer's skip */
    if (townreoffer::AsideStateTemplate(1, 1, 1) != townreoffer::kStateArg) Fail("T-515: the call's own template first");
    if (townreoffer::AsideStateTemplate(1, 1, 0) != townreoffer::kStateArg) Fail("T-515: the call's own template, building has none");
    if (townreoffer::AsideStateTemplate(1, 0, 1) != townreoffer::kStateBuilding) Fail("T-515: no call template - the building's resident template");
    if (townreoffer::AsideStateTemplate(1, 0, 0) != townreoffer::kStateNone) Fail("T-515: no template at all - the engine makes nothing, no step");
    if (townreoffer::AsideStateTemplate(0, 1, 0) != townreoffer::kStateNone) Fail("T-515: no building - no building step");
    if (townreoffer::AsideStateTemplate(0, 0, 1) != townreoffer::kStateNone) Fail("T-515: no building, no step");
    if (townreoffer::RerunStateSkips(1, 1) != 1) Fail("T-515: the re-offer's building, step taken at load - skipped");
    if (townreoffer::RerunStateSkips(1, 0) != 0) Fail("T-515: the re-offer's building, step not taken at load - it runs now");
    if (townreoffer::RerunStateSkips(0, 1) != 0) Fail("T-515: another building (or no re-offer) - the engine's step runs");
    if (townreoffer::RerunStateSkips(0, 0) != 0) Fail("T-515: outside the re-offer - runs");
    if (townreoffer::RefusedSquadTakesState(1, 1, 0) != 1) Fail("T-515: refused inside a building's populate - the building's state still lands");
    if (townreoffer::RefusedSquadTakesState(1, 1, 1) != 0) Fail("T-515: refused while the engine frees the world - no step");
    if (townreoffer::RefusedSquadTakesState(0, 1, 0) != 0) Fail("T-515: refused outside a building's populate (a patrol, a bar crowd) - no step");
    if (townreoffer::RefusedSquadTakesState(1, 0, 0) != 0) Fail("T-515: no building - no step");
    /* T-591: a building's squad refused because a note of its town holds sets the building aside for the town's check-up */
    if (townreoffer::PendingSetsAside(1, 0, 0, 1) != 1) Fail("T-591: refused by a note inside a building made here - the building is set aside");
    if (townreoffer::PendingSetsAside(0, 0, 0, 1) != 0) Fail("T-591: refused by a note outside a building fill (a patrol, a roaming group) - nothing set aside");
    if (townreoffer::PendingSetsAside(1, 1, 0, 1) != 0) Fail("T-591: the building is already set aside - not recorded again");
    if (townreoffer::PendingSetsAside(1, 0, 1, 1) != 0) Fail("T-591: inside a residents re-run - the re-run's own keep rule answers it");
    if (townreoffer::PendingSetsAside(1, 0, 0, 0) != 0) Fail("T-591: another game holds the area - its squads are not this game's to set aside");
    if (townreoffer::PendingSetsAside(1, 0, 0, -2) != 0) Fail("T-591: the engine is freeing the world - nothing set aside");
    if (townreoffer::PendingSetsAside(1, 0, 0, -1) != 0) Fail("T-591: no fresh map - the gate itself sets such a building aside, not this road");
    if (std::strcmp(townreoffer::DeferCauseName(townreoffer::kDefPending), "notebook-pending") != 0) Fail("T-591: cause name notebook-pending");
    if (townreoffer::Decide(1, 0, 0, 1, 1, 0) != townreoffer::kReWait) Fail("T-591: a note of the town still holds - the set-aside building waits");
    if (townreoffer::Decide(1, 0, 0, 0, 1, 0) != townreoffer::kReGenerate) Fail("T-591: no note of the town holds any more - the building is re-offered");
    if (owedpop::Commit(townreoffer::kReGenerate, 1, 1, 0, (int)owedpop::kClaimNone) != owedpop::kActForget) Fail("T-591: the building's home residents live here (rebuilt from their records) - forgotten, never filled again");
    if (owedpop::Commit(townreoffer::kReGenerate, 1, 1, 1, (int)owedpop::kClaimYou) != owedpop::kActDoneHas) Fail("T-591: an owed building whose home residents live here - DONE has, never filled again");
    if (owedpop::Commit(townreoffer::kReGenerate, 1, -1, 0, (int)owedpop::kClaimNone) != owedpop::kActWait) Fail("T-591: the building's residents do not read - wait, never fill blind");
    if (owedpop::Commit(townreoffer::kReGenerate, 1, 0, 0, (int)owedpop::kClaimNone) != owedpop::kActMake) Fail("T-591: no home residents, no note holds, the area is made here - the building is filled");
}

void t_towns2_rebuild_decisions()
{
    /* the marker's 60% share, in the engine's own double arithmetic: it agrees with the exact integer rule for every size */
    {
        int disagree = 0;
        for (int o = 1; o <= 2000; ++o)
            for (int l = 0; l <= o; ++l)
            {
                coopt2::MarkIn g; std::memset(&g, 0, sizeof(g));
                g.counted = 1; g.whole = 1; g.canRefresh = 1; g.orig = o; g.living = l;
                int total = 0;
                const int m = coopt2::MarkOne(g, 1.0e9f, 1, &total);
                const int want = (o - l > 0 && 10 * (o - l) >= 6 * o) ? 1 : 0;
                if (m != want) ++disagree;
            }
        if (disagree != 0) Fail("the 0.6 double rule and the integer rule disagree");
    }
    /* one resident list: 6 of 10 lost is marked, 5 of 10 is not; a mark adds its missing count to the total */
    {
        coopt2::MarkIn r[3]; std::memset(r, 0, sizeof(r));
        for (int i = 0; i < 3; ++i) { r[i].counted = 1; r[i].whole = 1; r[i].canRefresh = 1; r[i].orig = 10; }
        r[0].living = 4; r[1].living = 5; r[2].living = 10;
        unsigned char kr[3], kp[1];
        const int n = coopt2::MarkerPass(r, 3, 100.0f, 0, 0, 0.0f, 0, kr, kp);
        if (n != 1 || kr[0] != 1 || kr[1] != 0 || kr[2] != 0) Fail("60% lost is marked, 50% and whole are not");
        if (coopt2::MarkerPass(r, 3, 100.0f, 0, 0, 0.0f, 1, kr, kp) != 0 || kr[0] != 0) Fail("a dead town marks nothing");
    }
    /* the budget: (int)(budget - (float)total), total = the living of BOTH lists first, then every mark's missing count */
    {
        coopt2::MarkIn r[2], p[1]; std::memset(r, 0, sizeof(r)); std::memset(p, 0, sizeof(p));
        r[0].counted = 1; r[0].whole = 1; r[0].canRefresh = 1; r[0].orig = 10; r[0].living = 2;   /* missing 8 */
        r[1].counted = 1; r[1].whole = 1; r[1].canRefresh = 1; r[1].orig = 5; r[1].living = 1;    /* missing 4 */
        p[0].counted = 1; p[0].whole = 0; p[0].canRefresh = 1; p[0].orig = 6; p[0].living = 6;    /* separated: never marked, but its living count */
        unsigned char kr[2], kp[1];
        /* total = 2 + 1 + 6 = 9; budget 21: room 12 -> r0 (8) marked, total 17; room 4 -> r1 (4) marked */
        if (coopt2::MarkerPass(r, 2, 21.0f, p, 1, 100.0f, 0, kr, kp) != 2 || kr[0] != 1 || kr[1] != 1 || kp[0] != 0) Fail("budget 21: both fit in order");
        /* budget 20: room 11 -> r0 marked, total 17; room 3 < 4 -> r1 not */
        if (coopt2::MarkerPass(r, 2, 20.0f, p, 1, 100.0f, 0, kr, kp) != 1 || kr[0] != 1 || kr[1] != 0) Fail("budget 20: the first mark uses the room");
        /* the float room is truncated toward zero: 16.9 - 9 = 7.9 -> 7 < 8 */
        if (coopt2::MarkerPass(r, 2, 16.9f, p, 1, 100.0f, 0, kr, kp) != 1 || kr[0] != 0 || kr[1] != 1) Fail("budget 16.9: 7.9 rounds down to 7 - missing 8 does not fit, missing 4 does");
        /* an uncounted node (its handle did not resolve) adds nothing to the total */
        p[0].counted = 0;
        if (coopt2::MarkerPass(r, 2, 14.0f, p, 1, 100.0f, 0, kr, kp) != 1 || kr[0] != 1) Fail("an unresolved node is not in the total (14 - 3 = 11 >= 8; counted it would be 14 - 9 = 5)");
        /* a slave group (canRefresh 0) is never marked but its living still count */
        r[1].canRefresh = 0;
        if (coopt2::MarkerPass(r, 2, 100.0f, p, 1, 100.0f, 0, kr, kp) != 1 || kr[1] != 0) Fail("a slave group is never marked");
    }
    /* regenerates: a resident is marked after ANY loss, past the share and the budget; a patrol is not */
    {
        coopt2::MarkIn r[1], p[1]; std::memset(r, 0, sizeof(r)); std::memset(p, 0, sizeof(p));
        r[0].counted = 1; r[0].whole = 1; r[0].canRefresh = 1; r[0].regenerates = 1; r[0].orig = 10; r[0].living = 9;
        p[0].counted = 1; p[0].whole = 1; p[0].canRefresh = 1; p[0].regenerates = 1; p[0].orig = 10; p[0].living = 9;
        unsigned char kr[1], kp[1];
        if (coopt2::MarkerPass(r, 1, 0.0f, p, 1, 0.0f, 0, kr, kp) != 1 || kr[0] != 1 || kp[0] != 0) Fail("a regenerating resident is marked after one loss, a patrol is not");
        r[0].living = 10;
        if (coopt2::MarkerPass(r, 1, 0.0f, p, 1, 0.0f, 0, kr, kp) != 0) Fail("a whole regenerating group is not marked");
        /* a patrol down 60% within its own list's budget is marked; the residents' marks come first */
        p[0].regenerates = 0; p[0].living = 4; r[0].living = 0;   /* total 4; resident marked (+10) -> 14 */
        if (coopt2::MarkerPass(r, 1, 0.0f, p, 1, 20.0f, 0, kr, kp) != 2 || kp[0] != 1) Fail("patrol: 20 - 14 = 6 >= 6");
        if (coopt2::MarkerPass(r, 1, 0.0f, p, 1, 19.0f, 0, kr, kp) != 1 || kp[0] != 0) Fail("patrol: 19 - 14 = 5 < 6");
    }
    /* the engine keeps a group whose leader died only for a regenerating recipe with a living home town */
    if (coopt2::EngineKeepsDeadGroup(1, 1, 0) != 1) Fail("regenerates + home town + town alive -> kept");
    if (coopt2::EngineKeepsDeadGroup(0, 1, 0) != 0 || coopt2::EngineKeepsDeadGroup(1, 0, 0) != 0 || coopt2::EngineKeepsDeadGroup(1, 1, 1) != 0) Fail("any other -> not kept");
    /* the version wait */
    if (coopt2::VersionMayChange(0, 5, 1) != 0) Fail("no other versions - never waits");
    if (coopt2::VersionMayChange(1, 5, 5) != 0) Fail("versions, nothing changed since out of range - walks");
    if (coopt2::VersionMayChange(1, 6, 5) != 1) Fail("versions and a changed state - waits");
    if (coopt2::VersionMayChange(-1, 5, 5) != 1) Fail("unread versions - waits");
    /* due: 120 in-game hours, the TEST verb skips the timer only, no clock is never due */
    if (coopt2::RebuildDue(-1.0, 10.0, 1) != -1) Fail("no clock -> -1, even when forced");
    if (coopt2::RebuildDue(50.0, -1.0, 0) != 3) Fail("never stamped on this holder -> stamp, the 5 days start");
    if (coopt2::RebuildDue(50.0, -1.0, 1) != 1) Fail("never stamped but forced -> due");
    if (coopt2::RebuildDue(129.9, 10.0, 0) != 0) Fail("119.9 h later is not due");
    if (coopt2::RebuildDue(130.0, 10.0, 0) != 1) Fail("exactly 120 h later is due");
    if (coopt2::RebuildDue(500.0, 10.0, 0) != 1) Fail("long after is due");
    if (coopt2::RebuildDue(5.0, 10.0, 0) != 2) Fail("a clock behind the stamp -> re-stamp");
    if (coopt2::RebuildDue(5.0, 10.0, 1) != 1) Fail("forced wins over a clock behind the stamp");
    /* sight */
    {
        const float px[2] = { 0.0f, 20000.0f }, pz[2] = { 0.0f, 0.0f };
        const float mx[1] = { 5000.0f }, mz[1] = { 0.0f }, fx[1] = { 5001.0f }, fz[1] = { 0.0f };
        if (coopt2::WithinAny(px, pz, 2, mx, mz, 1, coopt2::kSightUnits) != 1) Fail("a member at exactly 5000 is in sight");
        if (coopt2::WithinAny(px, pz, 1, fx, fz, 1, coopt2::kSightUnits) != 0) Fail("a member just past 5000 is not in sight");
        if (coopt2::WithinAny(px, pz, 0, mx, mz, 1, coopt2::kSightUnits) != 0) Fail("no players - nothing sees");
        if (coopt2::WithinAny(px, pz, 2, mx, mz, 0, coopt2::kSightUnits) != 0) Fail("no points - nothing seen");
        const float far2x[1] = { 19000.0f }, far2z[1] = { 0.0f };
        if (coopt2::WithinAny(px, pz, 2, far2x, far2z, 1, coopt2::kSightUnits) != 1) Fail("the second player sees it");
    }
    /* the verdict: a pick first, then every refusal */
    {
        coopt2::GroupFacts g; std::memset(&g, 0, sizeof(g));
        g.marked = 1; g.living = 3; g.awake = 1; g.held = 1;
        if (coopt2::GroupVerdict(g, 0) != coopt2::kQueue) Fail("a picked, awake, held, unseen group is queued");
        g.marked = 0; if (coopt2::GroupVerdict(g, 1) != coopt2::kNotMarked) Fail("an unpicked group is never queued, nosight or not"); g.marked = 1;
        g.dead = 1; g.awake = 0; if (coopt2::GroupVerdict(g, 0) != coopt2::kDead) Fail("dead is said before asleep"); g.dead = 0;
        if (coopt2::GroupVerdict(g, 0) != coopt2::kAsleep) Fail("a sleeping group is left to its own wake"); g.awake = 1;
        g.living = 0; if (coopt2::GroupVerdict(g, 0) != coopt2::kNoneAlive) Fail("nobody alive in a group the engine did not keep");
        g.regenerates = 1; g.homeTown = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kQueue) Fail("nobody alive in a group the engine kept - rebuilt");
        g.homeTownDead = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kNoneAlive) Fail("a dead home town keeps nothing"); g.homeTownDead = 0; g.living = 3;
        g.uniqueAlive = 1; if (coopt2::GroupVerdict(g, 1) != coopt2::kUniqueAlive) Fail("a living unique member refuses, nosight or not"); g.uniqueAlive = 0;
        g.recipeUnique = 1; if (coopt2::GroupVerdict(g, 1) != coopt2::kUniqueUnknown) Fail("a recipe unique this game would make refuses, nosight or not");
        g.uniqueAlive = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kUniqueAlive) Fail("a living unique is said before the recipe's"); g.uniqueAlive = 0; g.recipeUnique = 0;
        g.subSquads = 1; if (coopt2::GroupVerdict(g, 1) != coopt2::kSubSquads) Fail("sub-squads not all read and judged refuse, nosight or not");
        g.recipeUnique = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kUniqueUnknown) Fail("the recipe's unique is said before its sub-squads"); g.recipeUnique = 0; g.subSquads = 0;
        g.jailed = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kJailed) Fail("a jailed member refuses"); g.jailed = 0;
        g.carried = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kCarried) Fail("a carried member refuses"); g.carried = 0;
        g.player = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kPlayerGroup) Fail("a player's member refuses"); g.player = 0;
        g.held = 0; if (coopt2::GroupVerdict(g, 1) != coopt2::kNotHeld) Fail("a member in a zone this game does not hold refuses"); g.held = 1;
        g.seen = 1; if (coopt2::GroupVerdict(g, 0) != coopt2::kSeen) Fail("a seen group waits");
        if (coopt2::GroupVerdict(g, 1) != coopt2::kQueue) Fail("nosight ignores the sight rule only"); g.seen = 0;
        if (std::strcmp(coopt2::VerdictName(coopt2::kNotHeld), "notHeld") != 0 || std::strcmp(coopt2::VerdictName(99), "?") != 0) Fail("verdict names");
        if (std::strcmp(coopt2::VerdictName(coopt2::kUniqueUnknown), "uniqueUnknown") != 0 || std::strcmp(coopt2::VerdictName(coopt2::kJailed), "jailed") != 0) Fail("verdict names after uniqueUnknown");
        if (std::strcmp(coopt2::VerdictName(coopt2::kSubSquads), "subSquads") != 0) Fail("the sub-squad verdict's name");
    }
    /* the recipe's uniques: only a filled slot here lets a unique template through; a fault refuses */
    if (coopt2::RecipeUniqueBlocks(0, 0) != 0 || coopt2::RecipeUniqueBlocks(0, -1) != 0) Fail("an ordinary template never stops a rebuild");
    if (coopt2::RecipeUniqueBlocks(1, 1) != 0) Fail("a unique made or marked dead here - the game's own check refuses it, the rebuild goes on");
    if (coopt2::RecipeUniqueBlocks(1, 0) != 1) Fail("a unique with no filled entry here - another game may hold it alive - stops the rebuild");
    if (coopt2::RecipeUniqueBlocks(1, -1) != 1 || coopt2::RecipeUniqueBlocks(-1, 1) != 1) Fail("a faulted read stops the rebuild");
    /* the member lists createRandomSquad makes people from */
    if (coopt2::IsMemberList("leader", 6) != 1 || coopt2::IsMemberList("squad", 5) != 1 || coopt2::IsMemberList("squad2", 6) != 1) Fail("leader / squad / squad2 are member lists");
    if (coopt2::IsMemberList("animals", 7) != 1 || coopt2::IsMemberList("animals2", 8) != 1 || coopt2::IsMemberList("choosefrom list", 15) != 1) Fail("animals / animals2 / choosefrom list are member lists");
    if (coopt2::IsMemberList("squad", 4) != 0 || coopt2::IsMemberList("faction", 7) != 0 || coopt2::IsMemberList("slaves", 6) != 0 || coopt2::IsMemberList(0, 0) != 0) Fail("other lists and names are not");
    /* the sub-squad lists createRandomSquad runs itself again on */
    if (coopt2::IsSubSquadList("slaves", 6) != 1 || coopt2::IsSubSquadList("prisoners", 9) != 1) Fail("slaves / prisoners are sub-squad lists");
    if (coopt2::IsSubSquadList("slaves", 5) != 0 || coopt2::IsSubSquadList("squad", 5) != 0 || coopt2::IsSubSquadList("leader", 6) != 0 || coopt2::IsSubSquadList(0, 0) != 0) Fail("member lists and other names are not sub-squad lists");
    if (coopt2::SubSquadsBlock(0, 0, 0) != 0 || coopt2::SubSquadsBlock(0, 0, 3) != 0) Fail("no sub-squads - nothing to refuse");
    if (coopt2::SubSquadsBlock(2, 2, 0) != 0) Fail("every sub-squad template read, none nested - judged by its members");
    if (coopt2::SubSquadsBlock(2, 1, 0) != 1 || coopt2::SubSquadsBlock(1, 0, 0) != 1) Fail("a sub-squad template not found or not read refuses");
    if (coopt2::SubSquadsBlock(1, 1, 1) != 1) Fail("a sub-squad with sub-squads of its own refuses");
    /* the recipe read's step codes: read is 1, a fault 0, and every fail-closed case has its own name */
    if (coopt2::kRecipeFault != 0 || coopt2::kRecipeRead != 1 || coopt2::kRecipeNoTemplate != 2) Fail("recipe step codes 0 / 1 / 2 kept");
    for (int a = 0; a < coopt2::kRecipeSteps; ++a)
        for (int b = a + 1; b < coopt2::kRecipeSteps; ++b)
            if (std::strcmp(coopt2::RecipeStepName(a), coopt2::RecipeStepName(b)) == 0) Fail("recipe step names are distinct");
    if (std::strcmp(coopt2::RecipeStepName(coopt2::kRecipeOverCap), "overCap") != 0 || std::strcmp(coopt2::RecipeStepName(coopt2::kRecipeLongSid), "longSid") != 0
        || std::strcmp(coopt2::RecipeStepName(coopt2::kRecipeNoTemplate), "noTemplate") != 0 || std::strcmp(coopt2::RecipeStepName(99), "?") != 0) Fail("recipe step names");
}
void t_refill1_bar_decisions()
{
    /* due: 120 in-game hours, the TEST verb skips the timer only, no clock is never due */
    if (coopbar::RefillDue(-1.0, 10.0, 1) != -1) Fail("no clock -> -1, even when forced");
    if (coopbar::RefillDue(100.0, 0.0, 0) != 0) Fail("100 h after a fill is not due");
    if (coopbar::RefillDue(120.0, 0.0, 0) != 1) Fail("exactly 5 days is due");
    if (coopbar::RefillDue(130.5, 10.5, 0) != 1) Fail("120 h later is due");
    if (coopbar::RefillDue(20.0, 10.0, 1) != 1) Fail("refill now skips the timer");
    if (coopbar::RefillDue(5.0, -1.0, 0) != 1) Fail("never filled is due");
    if (coopbar::RefillDue(5.0, 10.0, 0) != 0) Fail("a stamp ahead of the clock is not due");
    /* refill3 (T382): a forced town's skip line - the first at once, then one per 30 s, across a wrapped tick clock */
    if (coopbar::ForcedSkipLineDue(5u, 0u, 0) != 1) Fail("the first skip line is shown");
    if (coopbar::ForcedSkipLineDue(29999u, 0u, 1) != 0) Fail("a skip line 29.999 s after the last is held");
    if (coopbar::ForcedSkipLineDue(30000u, 0u, 1) != 1) Fail("a skip line 30 s after the last is shown");
    if (coopbar::ForcedSkipLineDue(10000u, 0xFFFFD8F0u, 1) != 0) Fail("wrapped clock: 20 s after the last is held");
    if (coopbar::ForcedSkipLineDue(40000u, 0xFFFFD8F0u, 1) != 1) Fail("wrapped clock: 50 s after the last is shown");
    /* missing people -> squads wanted = ceil(missing x usualSquads / usualPeople) */
    if (coopbar::RefillSquadsWanted(20, 6, 20) != 0 || coopbar::RefillSquadsWanted(20, 6, 25) != 0) Fail("a full bar wants nothing");
    if (coopbar::RefillSquadsWanted(20, 6, 17) != 1) Fail("3 missing of 20/6 -> 1 squad");
    if (coopbar::RefillSquadsWanted(20, 6, 10) != 3) Fail("10 missing of 20/6 -> 3 squads");
    if (coopbar::RefillSquadsWanted(20, 6, 0) != 6) Fail("all 20 missing -> 6 squads");
    if (coopbar::RefillSquadsWanted(0, 0, 0) != 0 || coopbar::RefillSquadsWanted(5, 0, 0) != 0) Fail("no usual size wants nothing");
    if (coopbar::RefillSquadsWanted(4, 4, -3) != 4) Fail("a negative count reads as 0 free");
    /* squads -> per-entry counts: spread over the hire entries in order, each capped at its own count x the multiplier */
    {
        const int orig[4] = { 3, 2, 5, 4 }; const unsigned char hire[4] = { 1, 0, 1, 1 }; int out[4];
        if (coopbar::RefillEntryCounts(4, orig, hire, 4, 1, out) != 4 || out[0] != 3 || out[1] != 0 || out[2] != 1 || out[3] != 0) Fail("4 wanted x1: 3 + 1, the non-hire entry 0");
        if (coopbar::RefillEntryCounts(4, orig, hire, 4, 2, out) != 4 || out[0] != 4 || out[1] != 0 || out[2] != 0 || out[3] != 0) Fail("4 wanted x2: all from the first hire entry (cap 6)");
        if (coopbar::RefillEntryCounts(50, orig, hire, 4, 1, out) != 12 || out[0] != 3 || out[1] != 0 || out[2] != 5 || out[3] != 4) Fail("50 wanted x1 is capped at 3+5+4");
        if (coopbar::RefillEntryCounts(0, orig, hire, 4, 3, out) != 0 || out[0] != 0 || out[2] != 0 || out[3] != 0) Fail("0 wanted asks nothing");
        if (coopbar::RefillEntryCounts(3, orig, hire, 4, 0, out) != 3 || out[0] != 3 || out[2] != 0) Fail("a multiplier below 1 reads as 1");
    }
    {
        const int orig[2] = { 0, 2 }; const unsigned char hire[2] = { 1, 0 }; int out[2];
        if (coopbar::RefillEntryCounts(5, orig, hire, 2, 4, out) != 0 || out[0] != 0 || out[1] != 0) Fail("a hire entry with count 0 and a non-hire entry are never raised");
    }
    /* the nearby rule's geometry (ground plane) */
    {
        const float bx[2] = { 100.0f, -5000.0f }, bz[2] = { 200.0f, 0.0f };
        if (!coopbar::RefillNearAny(bx, bz, 2, 250.0f, 200.0f, coopbar::kRefillNearUnits)) Fail("150 from the bar is near");
        if (coopbar::RefillNearAny(bx, bz, 2, 400.0f, 200.0f, coopbar::kRefillNearUnits)) Fail("300 from the bar is not near");
        if (!coopbar::RefillNearAny(bx, bz, 2, -5100.0f, 100.0f, coopbar::kRefillNearUnits)) Fail("near the second bar");
        if (coopbar::RefillNearAny(bx, bz, 0, 100.0f, 200.0f, coopbar::kRefillNearUnits)) Fail("no bars -> never near");
    }
    /* the record's merge: the largest usual size wins (review-refill1), last-filled only forward */
    {
        coopbar::BarTable t; coopbar::BarRow a; a.usualSet = 1; a.usualPeople = 20; a.usualSquads = 6; a.lastFilled = 10.0;
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", a) != 1) Fail("a new row is stored");
        coopbar::BarRow b; b.usualSet = 1; b.usualPeople = 3; b.usualSquads = 1; b.lastFilled = 5.0;
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", b) != 0 || t["18919-Newwworld.mod"].usualPeople != 20 || t["18919-Newwworld.mod"].lastFilled != 10.0) Fail("a smaller usual size never replaces a larger one and an older fill never wins");
        coopbar::BarRow c; c.lastFilled = 130.0;
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", c) != 1 || t["18919-Newwworld.mod"].lastFilled != 130.0 || t["18919-Newwworld.mod"].usualPeople != 20) Fail("a later fill moves the stamp and keeps the usual size");
        coopbar::BarTable u; coopbar::BarRow d; d.lastFilled = 7.0;
        coopbar::BarMerge(&u, "x", d);
        coopbar::BarRow e; e.usualSet = 1; e.usualPeople = 9; e.usualSquads = 3; e.lastFilled = 7.0;
        if (coopbar::BarMerge(&u, "x", e) != 1 || u["x"].usualPeople != 9) Fail("a stamp-only row takes a usual size later");
        coopbar::BarRow bad; bad.usualSet = 2;
        if (coopbar::BarMerge(&u, "y", bad) != 0 || coopbar::BarMerge(&u, "", a) != 0 || coopbar::BarMerge(&u, "a\tb", a) != 0) Fail("bad rows and bad keys are refused");
        coopbar::BarRow big; big.usualSet = 1; big.usualPeople = 26; big.usualSquads = 8; big.lastFilled = 1.0;
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", big) != 1 || t["18919-Newwworld.mod"].usualPeople != 26 || t["18919-Newwworld.mod"].usualSquads != 8 || t["18919-Newwworld.mod"].lastFilled != 130.0) Fail("a larger usual size replaces the stored one, squads with it; the stamp stays");
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", big) != 0) Fail("the same usual size again changes nothing");
        coopbar::BarRow unset; unset.usualPeople = 99; unset.lastFilled = 1.0;
        if (coopbar::BarMerge(&t, "18919-Newwworld.mod", unset) != 0 || t["18919-Newwworld.mod"].usualPeople != 26) Fail("a row with no usual size never moves it");
    }
    /* the expected roll (review-refill1 item 3; R4: floor int(p x 0.5 x 0.01 x c) when c > 2, then d100s at or under p to the first miss) */
    {
        const double e1 = coopbar::BarExpectedSquads(4, 70);   /* floor int(1.4) = 1, then 0.7 + 0.49 + 0.343 */
        if (!(e1 > 2.533 - 1e-9 && e1 < 2.533 + 1e-9)) Fail("(4, 70%) expects 2.533 squads - the roll stops at the first miss, not 1 + 3 x 0.7");
        const double e2 = coopbar::BarExpectedSquads(2, 50);   /* no floor at c <= 2: 0.5 + 0.25 */
        if (!(e2 > 0.75 - 1e-9 && e2 < 0.75 + 1e-9)) Fail("(2, 50%) expects 0.75 squads - no floor at count 2");
        if (coopbar::BarExpectedSquads(5, 100) != 5.0 || coopbar::BarExpectedSquads(6, 150) != 6.0) Fail("chance 100 (or above, clamped) makes exactly the count");
        if (coopbar::BarExpectedSquads(3, 0) != 0.0 || coopbar::BarExpectedSquads(0, 50) != 0.0 || coopbar::BarExpectedSquads(-2, 50) != 0.0) Fail("chance 0 or no count expects nothing");
    }
    /* the usual size reported: max(the roll's own count, expected squads x people per squad) */
    {
        unsigned p = 0, q = 0;
        coopbar::BarUsualFromRoll(2, 1, 2.533, &p, &q);
        if (p != 5 || q != 3) Fail("a small roll (2 in 1 squad) is raised to the expected roll: 5 people in 3 squads");
        coopbar::BarUsualFromRoll(10, 4, 2.533, &p, &q);
        if (p != 10 || q != 4) Fail("a roll above the expected one stands");
        coopbar::BarUsualFromRoll(0, 0, 1.5, &p, &q);
        if (p != 5 || q != 2) Fail("an empty roll counts 3 people per expected squad");
        coopbar::BarUsualFromRoll(7, 3, -1.0, &p, &q);
        if (p != 7 || q != 3) Fail("no expected roll known: the count as it stands");
        coopbar::BarUsualFromRoll(0, 0, 0.0, &p, &q);
        if (p != 0 || q != 0) Fail("nothing expected and nothing counted is 0");
        coopbar::BarUsualFromRoll(1, 1, 0.1, &p, &q);
        if (p != 1 || q != 1) Fail("an expected roll under the count changes nothing");
    }
    /* refill2: a usual size from a LIVE count needs the list known first (T374: The Hub usual=1 from=live, expectedSquads=-) */
    {
        unsigned p = 9, q = 9;
        if (coopbar::BarUsualFromLive(1, 1, -1.0, &p, &q) != 0 || p != 0 || q != 0) Fail("a live count with the bar list not known yet is not recorded - it waits (usualWaitList)");
        if (coopbar::BarUsualFromLive(1, 1, 2.533, &p, &q) != 1 || p != 3 || q != 3) Fail("a live count (1 in 1 squad) below the expected roll: the expected roll wins - 3 people in 3 squads");
        if (coopbar::BarUsualFromLive(10, 4, 2.533, &p, &q) != 1 || p != 10 || q != 4) Fail("a live count above the expected roll stands");
        if (coopbar::BarUsualFromLive(4, 2, 0.0, &p, &q) != 1 || p != 4 || q != 2) Fail("a known list that expects nothing records the live count");
    }
    /* the wire: round trip, a short or long payload refused whole, an empty list is not a message */
    {
        std::vector<coopbar::BarWireRow> rows; coopbar::BarWireRow w;
        w.sid = "18919-Newwworld.mod"; w.row.usualSet = 1; w.row.usualPeople = 20; w.row.usualSquads = 6; w.row.lastFilled = 1234.5; rows.push_back(w);
        w.sid = "42-rebirth.mod"; w.row = coopbar::BarRow(); rows.push_back(w);
        std::vector<char> m;
        if (!coopbar::EncodeBarRows(&m, rows)) Fail("encode");
        std::vector<coopbar::BarWireRow> back;
        if (m.empty() || coopbar::DecodeBarRows(&m[0], m.size(), &back) != 1 || back.size() != 2 || back[0].sid != rows[0].sid || back[0].row.usualPeople != 20
            || back[0].row.usualSquads != 6 || back[0].row.lastFilled != 1234.5 || back[1].row.usualSet != 0 || back[1].row.lastFilled != -1.0) Fail("round trip");
        if (m.size() < 2 || coopbar::DecodeBarRows(&m[0], m.size() - 1, &back) != 0 || !back.empty()) Fail("a short payload is refused whole");
        std::vector<char> m2(m); m2.push_back(0);
        if (coopbar::DecodeBarRows(&m2[0], m2.size(), &back) != 0) Fail("a trailing byte is refused");
        std::vector<coopbar::BarWireRow> none;
        if (coopbar::EncodeBarRows(&m, none)) Fail("an empty list is not a message");
    }
    /* the file line */
    {
        coopbar::BarTable t; coopbar::BarRow a; a.usualSet = 1; a.usualPeople = 20; a.usualSquads = 6; a.lastFilled = 1234.5;
        const std::string line = coopbar::BarLine("18919-Newwworld.mod", a);
        if (line != "v1\t18919-Newwworld.mod\t1\t20\t6\t1234.500000\n") Fail("the file line");
        if (coopbar::BarParseLine(line, &t) != 1 || t["18919-Newwworld.mod"].usualPeople != 20 || t["18919-Newwworld.mod"].lastFilled != 1234.5) Fail("the file line reads back");
        if (coopbar::BarParseLine("v1\tx\t1\t2\n", &t) != 0 || coopbar::BarParseLine("v2\tx\t1\t2\t3\t4\n", &t) != 0 || coopbar::BarParseLine("v1\tx\t1\t-2\t3\t4\n", &t) != 0) Fail("bad lines are refused");
        if (coopbar::BarParseLine("v1\tz\t0\t0\t0\t-1.000000\r\n", &t) != 1 || t["z"].lastFilled != -1.0) Fail("a CRLF line and a never-filled stamp read back");
    }
}

void t_loot2b_research_wire()
{
    coopres::ResearchBox a; a.key = "1950-gamedata.base@34,28@112345,-138880,1021";
    coopres::ResearchRow r0; r0.index = 0; r0.sid = "43953-rebirth.mod"; r0.material = "mat"; r0.level = 2; r0.quality = 0.75f; r0.qty = 1;
    coopres::ResearchRow r1; r1.index = 1; r1.sid = "16855-nodes_otto1.mod"; r1.level = 0; r1.quality = 12.5f; r1.qty = 3;
    a.rows.push_back(r0); a.rows.push_back(r1);
    coopres::ResearchBox none; none.key = "shelf@34,28@1,2,3";
    std::vector<coopres::ResearchBox> in; in.push_back(a); in.push_back(none);
    std::vector<char> w;
    if (!coopres::EncodeResearchBoxes(&w, in)) { Fail("a RESEARCH_BOX did not encode"); return; }
    std::vector<coopres::ResearchBox> o;
    if (coopres::DecodeResearchBoxes(&w[0], w.size(), &o) != coopres::kResearchDecodeOk || o.size() != 2 || o[0].key != a.key
        || o[0].rows.size() != 2 || o[0].rows[1].sid != r1.sid || o[0].rows[1].qty != 3 || o[0].rows[0].quality != 0.75f
        || o[0].rows[0].material != "mat" || o[0].rows[0].level != 2 || !o[1].rows.empty() || o[1].key != none.key)
    { Fail("a RESEARCH_BOX did not round-trip"); return; }
    for (size_t cut = 0; cut < w.size(); ++cut)
        if (coopres::DecodeResearchBoxes(&w[0], cut, &o) == coopres::kResearchDecodeOk) { Fail("a truncated RESEARCH_BOX was accepted"); break; }
    std::vector<char> t(w); t.push_back(0);
    if (coopres::DecodeResearchBoxes(&t[0], t.size(), &o) != coopres::kResearchDecodeTrailing) Fail("a RESEARCH_BOX with a trailing byte was accepted");
    coopres::ResearchBox bad; bad.key = "a\tb"; std::vector<coopres::ResearchBox> bv(1, bad); std::vector<char> e;
    if (coopres::EncodeResearchBoxes(&e, bv) || !e.empty()) Fail("a RESEARCH_BOX key with a tab was encoded");
    /* first writer wins per box */
    coopres::ResearchTable tb;
    if (coopres::ResearchMerge(&tb, a) != 1 || coopres::ResearchMerge(&tb, none) != 1) Fail("a new box was not stored");
    coopres::ResearchBox a2 = a; a2.rows.pop_back();
    if (coopres::ResearchMerge(&tb, a2) != 0 || tb[a.key].size() != 2) Fail("a second writer replaced a stored box");
    if (coopres::ResearchRowCount(tb) != 2) Fail("the row count counted the -1 marker");
    /* the file lines read back to the same table */
    std::string text;
    for (coopres::ResearchTable::const_iterator it = tb.begin(); it != tb.end(); ++it) text += coopres::ResearchLines(it->first, it->second);
    coopres::ResearchTable back;
    size_t at = 0; int used = 0;
    while (at < text.size()) { const size_t nl = text.find('\n', at); used += coopres::ResearchParseLine(text.substr(at, nl - at) + "\r", &back); at = nl + 1; }
    if (used != 3 || back.size() != 2 || back[none.key].size() != 0 || back[a.key].size() != 2 || back[a.key][1].quality != 12.5f
        || back[a.key][0].sid != r0.sid || back[a.key][1].index != 1)
        Fail("research_boxes.txt lines did not read back to the same table");
    if (coopres::ResearchParseLine("v2\tk\t0\ts\tm\t0\t0\t1", &back) != 0) Fail("a line of another version was used");
    /* fold 2 (review-loot2b 3a): a line that cannot ride the wire is refused, not stored */
    if (coopres::ResearchParseLine("v1\tk\t0\t" + std::string(300, 's') + "\tm\t0\t0\t1", &back) != 0) Fail("a line with a 300-character sid was used");
    if (coopres::ResearchParseLine("v1\tk\t-2\ts\tm\t0\t0\t1", &back) != 0) Fail("a line with index -2 was used");
    if (coopres::ResearchParseLine("v1\tk\t0\ts\tm\t0\t0\t1\textra", &back) != 0) Fail("a line with a ninth field was used");
    {
        coopres::ResearchTable cap; int usedCap = 0;
        for (int i = 0; i <= (int)coopres::kResearchMaxRows; ++i) usedCap += coopres::ResearchParseLine("v1\tbig\t" + std::string(1, '0') + "\ts\tm\t0\t0\t1", &cap);
        if (usedCap != (int)coopres::kResearchMaxRows || cap["big"].size() != coopres::kResearchMaxRows) Fail("a box grew past kResearchMaxRows from the file");
        std::vector<coopres::ResearchBox> all; coopres::ResearchBox bb; bb.key = "big"; bb.rows = cap["big"]; all.push_back(bb); std::vector<char> ew;
        if (!coopres::EncodeResearchBoxes(&ew, all)) Fail("a capped box read from the file did not encode");
    }
    /* fold 2 (5b): a restock generation rides inside the key and round-trips */
    {
        coopres::ResearchBox g; g.key = a.key + "#g3"; g.rows.push_back(r0);
        std::vector<coopres::ResearchBox> gv(1, g); std::vector<char> gw; std::vector<coopres::ResearchBox> go;
        if (!coopres::EncodeResearchBoxes(&gw, gv) || coopres::DecodeResearchBoxes(&gw[0], gw.size(), &go) != coopres::kResearchDecodeOk
            || go.size() != 1 || go[0].key != a.key + "#g3") Fail("a generation key did not round-trip");
        coopres::ResearchTable gt; coopres::ResearchParseLine(coopres::ResearchLines(g.key, g.rows), &gt);
        if (gt.size() != 1 || gt.begin()->first != a.key + "#g3") Fail("a generation key did not read back from its line");
    }
}

/* loot2c: RESEARCH_TAKE round trip, unfit rows refused, first writer wins per (key, index, player), research_takes.txt lines. */
void t_loot2c_takes_wire()
{
    coopres::ResearchTake a; a.key = "887-gamedata.base@34,28@117222,-140468,8042"; a.index = 0; a.player = "0123456789abcdef0123456789abcdef";
    coopres::ResearchTake b = a; b.player = "fedcba9876543210fedcba9876543210";
    std::vector<coopres::ResearchTake> in; in.push_back(a); in.push_back(b);
    std::vector<char> w;
    if (!coopres::EncodeResearchTakes(&w, in)) { Fail("a RESEARCH_TAKE did not encode"); return; }
    std::vector<coopres::ResearchTake> o;
    if (coopres::DecodeResearchTakes(&w[0], w.size(), &o) != coopres::kResearchDecodeOk || o.size() != 2 || o[0].key != a.key
        || o[0].index != 0 || o[0].player != a.player || o[1].player != b.player) Fail("a RESEARCH_TAKE did not round-trip");
    if (coopres::DecodeResearchTakes(&w[0], w.size() - 1, &o) != coopres::kResearchDecodeTooShort) Fail("a truncated RESEARCH_TAKE was accepted");
    std::vector<char> w2 = w; w2.push_back(0);
    if (coopres::DecodeResearchTakes(&w2[0], w2.size(), &o) != coopres::kResearchDecodeTrailing) Fail("a RESEARCH_TAKE with trailing bytes was accepted");
    coopres::ResearchTake bad = a; bad.index = -1;
    std::vector<coopres::ResearchTake> bv(1, bad); std::vector<char> bw;
    if (coopres::EncodeResearchTakes(&bw, bv) || !bw.empty()) Fail("a take row with index -1 was encoded");
    bad = a; bad.player = ""; bv[0] = bad;
    if (coopres::EncodeResearchTakes(&bw, bv) || !bw.empty()) Fail("a take row with no player was encoded");
    bad = a; bad.key = "a\tb"; bv[0] = bad;
    if (coopres::EncodeResearchTakes(&bw, bv) || !bw.empty()) Fail("a take row with a tab in its key was encoded");
    coopres::ResearchTakeTable t;
    if (coopres::ResearchTakeMerge(&t, a) != 1 || coopres::ResearchTakeMerge(&t, a) != 0 || coopres::ResearchTakeMerge(&t, b) != 1 || t.size() != 2)
        Fail("take rows are not first-writer-wins per (key, index, player)");
    coopres::ResearchTake c = a; c.index = 1;
    if (coopres::ResearchTakeMerge(&t, c) != 1 || t.size() != 3) Fail("another row of the same box was refused");
    std::string line = coopres::ResearchTakeLine(a);
    if (line.empty() || line[line.size() - 1] != '\n') Fail("a research_takes.txt line has no newline");
    line.erase(line.size() - 1);
    coopres::ResearchTake back;
    if (coopres::ResearchTakeParseLine(line, &back) != 1 || back.key != a.key || back.index != 0 || back.player != a.player)
        Fail("a research_takes.txt line did not read back");
    if (coopres::ResearchTakeParseLine(line + "\r", &back) != 1 || back.player != a.player) Fail("a CRLF research_takes.txt line did not read back");
    if (coopres::ResearchTakeParseLine("v1\tk\t-1\tp", &back) != 0) Fail("a negative index line was read");
    if (coopres::ResearchTakeParseLine("v1\tk\t2", &back) != 0) Fail("a short take line was read");
    if (coopres::ResearchTakeParseLine("v2\tk\t2\tp", &back) != 0) Fail("another line version was read");
    if (coopres::ResearchTakeParseLine("v1\tk\t2x\tp", &back) != 0) Fail("a non-numeric index was read");
    if (coopres::ResearchTakeParseLine("v1\t\t2\tp", &back) != 0) Fail("an empty key was read");
    coopres::ResearchTake g = a; g.key = a.key + "#g2";
    std::string gl = coopres::ResearchTakeLine(g); gl.erase(gl.size() - 1);
    if (coopres::ResearchTakeParseLine(gl, &back) != 1 || back.key != g.key) Fail("a generation key did not read back from its take line");
}

/* par1: MSG_PARITY_REQ / MSG_PARITY_BOX and the order-free box digest (docs/design-loot2.md rev 2, 0.4). */
/* snap1 (user decision 2026-09-26): the snapshot digest is ORDER-FREE and sees every quantity, section and row; the sample
   instant is the next whole in-game minute. */
void t_snap1_digest()
{
    std::vector<coopsnap::SnapRow> a, b;
    a.push_back(coopsnap::SnapRow("Katana", "weapon1", 1));
    a.push_back(coopsnap::SnapRow("Rice", "main", 5));
    a.push_back(coopsnap::SnapRow("Rice", "backpack_attach/main", 3));
    b.push_back(a[2]); b.push_back(a[0]); b.push_back(a[1]);
    const unsigned int da = coopsnap::SnapDigest(a);
    if (da != coopsnap::SnapDigest(b)) Fail("the snapshot digest depends on the order of the rows");
    if (da == 0) Fail("a non-empty list digested to 0 (0 is kept for empty)");
    if (a[0].sid != "Katana") Fail("SnapDigest reordered the caller's vector");
    { std::vector<coopsnap::SnapRow> c(a); c[1].qty = 6; if (coopsnap::SnapDigest(c) == da) Fail("a changed quantity did not change the digest"); }
    { std::vector<coopsnap::SnapRow> c(a); c[1].section = "belt"; if (coopsnap::SnapDigest(c) == da) Fail("a changed section did not change the digest"); }
    { std::vector<coopsnap::SnapRow> c(a); c.pop_back(); if (coopsnap::SnapDigest(c) == da) Fail("a missing backpack row did not change the digest"); }
    { std::vector<coopsnap::SnapRow> c(a); c.push_back(a[0]); if (coopsnap::SnapDigest(c) == da) Fail("a duplicated row did not change the digest"); }
    {
        std::vector<coopsnap::SnapRow> x, y;
        x.push_back(coopsnap::SnapRow("A", "main", 1)); x.push_back(coopsnap::SnapRow("B", "main", 2));
        y.push_back(coopsnap::SnapRow("A", "main", 2)); y.push_back(coopsnap::SnapRow("B", "main", 1));
        if (coopsnap::SnapDigest(x) == coopsnap::SnapDigest(y)) Fail("swapping two quantities between items did not change the digest");
    }
    { std::vector<coopsnap::SnapRow> none; if (coopsnap::SnapDigest(none) != 0) Fail("an empty list did not digest to 0"); }
    if (std::fabs(coopsnap::SnapNextMinute(10.0) - (10.0 + 1.0 / 60.0)) > 1e-9) Fail("the next minute after a whole minute is not one minute later");
    if (std::fabs(coopsnap::SnapNextMinute(10.5 + 0.5 / 60.0) - (10.5 + 1.0 / 60.0)) > 1e-9) Fail("the next minute from mid-minute is wrong");
    if (coopsnap::SnapNextMinute(-1.0) >= 0.0) Fail("an unknown clock produced a target minute");
    /* snap2: the default target is the next whole minute AT LEAST 3 in-game minutes ahead */
    if (std::fabs(coopsnap::SnapTargetMinute(10.0) - (10.0 + 3.0 / 60.0)) > 1e-9) Fail("snap2: from a whole minute the target is not exactly 3 minutes later");
    if (std::fabs(coopsnap::SnapTargetMinute(10.0 + 0.5 / 60.0) - (10.0 + 4.0 / 60.0)) > 1e-9) Fail("snap2: from mid-minute the target is not the 4th boundary");
    if (std::fabs(coopsnap::SnapTargetMinute(10.0 + 59.9 / 60.0) - (11.0 + 3.0 / 60.0)) > 1e-9) Fail("snap2: late in a minute the target is not 3+ minutes ahead");
    if (coopsnap::SnapTargetMinute(35.8710) - 35.8710 < 3.0 / 60.0 - 1e-9) Fail("snap2: T378's A clock got a target under 3 minutes ahead");
    if (coopsnap::SnapTargetMinute(-1.0) >= 0.0) Fail("snap2: an unknown clock produced a default target");
    {
        coopsnap::SnapArgs g;
        if (!coopsnap::SnapParseArgs("", &g).empty() || g.tag != "-" || g.haveAt != 0 || g.detailUid != 0) Fail("snap2: an empty snapshot argument did not give tag '-'");
        if (!coopsnap::SnapParseArgs("a", &g).empty() || g.tag != "a" || g.haveAt != 0) Fail("snap2: a bare tag did not parse");
        if (!coopsnap::SnapParseArgs("b at 37.6667 detail 4244635744", &g).empty() || g.tag != "b" || g.haveAt != 1
            || std::fabs(g.at - 37.6667) > 1e-9 || g.detailUid != 4244635744u) Fail("snap2: tag at detail did not parse");
        if (!coopsnap::SnapParseArgs(" c  detail 7\tat 1.5 ", &g).empty() || g.tag != "c" || g.detailUid != 7u || std::fabs(g.at - 1.5) > 1e-9)
            Fail("snap2: detail before at did not parse");
        const char* bad[] = { "a at", "a at x", "a at 1.5x", "a at -1", "a detail 0", "a detail 12a", "a detail 4294967296",
                              "a foo 1", "a at 1 at 2", "a detail 1 detail 2", "a 35.9" };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
            if (coopsnap::SnapParseArgs(bad[i], &g).empty()) Fail(std::string("snap2: a bad snapshot argument was accepted: ") + bad[i]);
    }
}

void t_par1_parity_wire()
{
    cooppar::ParityReq r; r.sx = 34; r.sy = 28;
    cooppar::ParityBoxRow a; a.key = "1950-gamedata.base@34,28@112345,-138880,1021"; a.digest = 0xDEADBEEFu; a.items = 7;
    cooppar::ParityBoxRow b; b.key = "shelf@34,28@1,2,3"; b.digest = 0; b.items = 0;
    r.boxes.push_back(a); r.boxes.push_back(b);
    std::vector<char> w;
    if (!cooppar::EncodeParityReq(&w, r) || w.size() != 4 + 4 + 2 + (1 + a.key.size() + 8) + (1 + b.key.size() + 8)) { Fail("a PARITY_REQ did not encode to its exact size"); return; }
    cooppar::ParityReq o;
    if (cooppar::DecodeParityReq(&w[0], w.size(), &o) != cooppar::kParityDecodeOk || o.sx != 34 || o.sy != 28 || o.boxes.size() != 2
        || o.boxes[0].key != a.key || o.boxes[0].digest != a.digest || o.boxes[0].items != 7 || o.boxes[1].key != b.key || o.boxes[1].digest != 0)
        Fail("a PARITY_REQ did not round-trip");
    for (size_t cut = 0; cut < w.size(); ++cut)
        if (cooppar::DecodeParityReq(&w[0], cut, &o) == cooppar::kParityDecodeOk) { Fail("a truncated PARITY_REQ was accepted"); break; }
    std::vector<char> t(w); t.push_back(0);
    if (cooppar::DecodeParityReq(&t[0], t.size(), &o) != cooppar::kParityDecodeTrailing) Fail("a PARITY_REQ with a trailing byte was accepted");
    cooppar::ParityReq bad; bad.boxes.push_back(cooppar::ParityBoxRow());
    std::vector<char> e;
    if (cooppar::EncodeParityReq(&e, bad) || !e.empty()) Fail("a PARITY_REQ row with an empty key was encoded");

    cooppar::ParityBox m; m.status = cooppar::kParityStatusContents; m.key = a.key;
    cooppar::ParityItem i1; i1.section = "main"; i1.x = 2; i1.y = 3; i1.quantity = 5; i1.baseSid = "100-gamedata.base";
    i1.materialSid = "mat"; i1.quality = 0.75f; i1.charges = 1.0f; i1.functionKind = 4; i1.level = 2; i1.unique = 1;
    cooppar::ParityItem i2; i2.section = "main"; i2.x = 0; i2.y = 0; i2.quantity = 1; i2.baseSid = "200-gamedata.base"; i2.quality = 0.5f;
    m.items.push_back(i1); m.items.push_back(i2);
    m.digest = cooppar::ParityDigestOf(m.items);
    std::vector<char> bx;
    if (!cooppar::EncodeParityBox(&bx, m)) { Fail("a PARITY_BOX did not encode"); return; }
    cooppar::ParityBox mo;
    if (cooppar::DecodeParityBox(&bx[0], bx.size(), &mo) != cooppar::kParityDecodeOk || mo.status != 0 || mo.key != m.key
        || mo.digest != m.digest || mo.items.size() != 2 || mo.items[0].section != "main" || mo.items[0].x != 2 || mo.items[0].y != 3
        || mo.items[0].quantity != 5 || mo.items[0].baseSid != i1.baseSid || mo.items[0].materialSid != "mat" || mo.items[0].quality != 0.75f
        || mo.items[0].charges != 1.0f || mo.items[0].functionKind != 4 || mo.items[0].level != 2 || mo.items[0].unique != 1
        || mo.items[1].baseSid != i2.baseSid || cooppar::ParityDigestOf(mo.items) != m.digest)
        Fail("a PARITY_BOX did not round-trip");
    for (size_t cut = 0; cut < bx.size(); ++cut)
        if (cooppar::DecodeParityBox(&bx[0], cut, &mo) == cooppar::kParityDecodeOk) { Fail("a truncated PARITY_BOX was accepted"); break; }
    cooppar::ParityBox eq; eq.status = cooppar::kParityStatusEqual; eq.key = "k"; eq.items.push_back(i1);
    std::vector<char> q;
    if (cooppar::EncodeParityBox(&q, eq) || !q.empty()) Fail("an EQUAL answer carrying items was encoded");
    eq.items.clear();
    if (!cooppar::EncodeParityBox(&q, eq) || cooppar::DecodeParityBox(&q[0], q.size(), &mo) != cooppar::kParityDecodeOk || mo.status != 1 || !mo.items.empty())
        Fail("an EQUAL answer did not round-trip");
    std::vector<char> bs(q); bs[0] = 9;
    if (cooppar::DecodeParityBox(&bs[0], bs.size(), &mo) != cooppar::kParityDecodeBadStatus) Fail("a PARITY_BOX with status 9 was accepted");

    /* the digest: order-free, 0 when empty, quality to 0.01, sensitive to quantity, slot and section */
    std::vector<cooppar::ParityItem> fw, bw, none;
    fw.push_back(i1); fw.push_back(i2); bw.push_back(i2); bw.push_back(i1);
    if (cooppar::ParityDigestOf(fw) != cooppar::ParityDigestOf(bw)) Fail("the digest depends on item order");
    if (cooppar::ParityDigestOf(none) != 0) Fail("an empty box's digest is not 0");
    if (cooppar::ParityItemHash("main", 2, 3, "x", 5, 0.7549f) != cooppar::ParityItemHash("main", 2, 3, "x", 5, 0.7501f)) Fail("quality is not taken to 0.01");
    if (cooppar::ParityItemHash("main", 2, 3, "x", 5, 0.75f) == cooppar::ParityItemHash("main", 2, 3, "x", 6, 0.75f)) Fail("quantity does not change the digest");
    if (cooppar::ParityItemHash("main", 2, 3, "x", 5, 0.75f) == cooppar::ParityItemHash("main", 3, 2, "x", 5, 0.75f)) Fail("the slot does not change the digest");
    if (cooppar::ParityItemHash("main", 2, 3, "x", 5, 0.75f) == cooppar::ParityItemHash("back", 2, 3, "x", 5, 0.75f)) Fail("the section does not change the digest");
    float nan = 0.0f; const unsigned int nanBits = 0x7FC00000u; std::memcpy(&nan, &nanBits, 4);
    if (cooppar::ParityQuality100(nan) != 0) Fail("a NaN quality is not read as 0");
}

/* P15 (protocol 98): a STATE's destroyed bit (the complete byte's bit 1) and the pure destroyed-state decisions. */
void t_p15_destroyed_state()
{
    coopbuild::BuildMsg s; s.kind = coopbuild::kBuildState; s.key = "P7n|ruin"; s.progress = 0.0f; s.needed = 4.0f;
    s.complete = 1; s.destroyed = 1; s.nMats = 1; s.mats[0] = 3.0f;
    const size_t cbAt = 1 + 1 + s.key.size() + 8;
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, s) || b.size() != (size_t)(1 + 1 + s.key.size() + 4 + 4 + 1 + 1 + 4)) { Fail("a destroyed STATE did not encode to its exact size"); return; }
    if ((unsigned char)b[cbAt] != 3) Fail("complete + destroyed is not the byte 3");
    coopbuild::BuildMsg o;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.complete != 1 || o.destroyed != 1 || o.mats[0] != 3.0f)
        Fail("a destroyed STATE did not survive the round trip");
    s.complete = 0; b.clear();
    if (!coopbuild::EncodeBuild(&b, s) || (unsigned char)b[cbAt] != 2 || coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk
        || o.complete != 0 || o.destroyed != 1)
        Fail("destroyed without complete did not survive the round trip");
    s.destroyed = 0; b.clear();
    if (!coopbuild::EncodeBuild(&b, s) || (unsigned char)b[cbAt] != 0 || coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.destroyed != 0)
        Fail("a whole STATE did not read whole");
    std::vector<char> hi(b); hi[cbAt] = (char)0x82;
    if (coopbuild::DecodeBuild(&hi[0], hi.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a STATE complete byte with bit 7 set was accepted");
    s.destroyed = 2;
    if (coopbuild::BuildEncodable(s)) Fail("destroyed=2 was encodable");
    /* the copy's action on the owner's destroyed state */
    if (coopbuild::BuildDestroyedAction(1, 1, 0) != coopbuild::kBuildDestroyKeep) Fail("a STATE made from a PLACE destroyed the copy");
    if (coopbuild::BuildDestroyedAction(1, 0, 1) != coopbuild::kBuildDestroyKeep) Fail("a STATE made from a PLACE repaired the copy");
    if (coopbuild::BuildDestroyedAction(0, 1, 0) != coopbuild::kBuildDestroyApply) Fail("a ruin STATE on a whole copy is not setDestroyed(1)");
    if (coopbuild::BuildDestroyedAction(0, 1, 1) != coopbuild::kBuildDestroyAlready) Fail("a ruin STATE on a ruin copy is not mats-only");
    if (coopbuild::BuildDestroyedAction(0, 0, 1) != coopbuild::kBuildDestroyRepair) Fail("a whole STATE on a ruin copy is not the repair road");
    if (coopbuild::BuildDestroyedAction(0, 0, 0) != coopbuild::kBuildDestroyKeep) Fail("a whole STATE on a whole copy changed it");
    /* a repair order on a copy */
    if (coopbuild::BuildRepairOrderVerdict(95, 1, 1, 0) != coopbuild::kBuildRepairOrderHelp) Fail("a repair on a ruin copy is not help");
    if (coopbuild::BuildRepairOrderVerdict(95, 1, 1, 1) != coopbuild::kBuildRepairOrderRefused) Fail("a repair on a wall ruin copy is not refused");
    if (coopbuild::BuildRepairOrderVerdict(95, 1, 0, 0) != coopbuild::kBuildRepairOrderNone) Fail("a repair on a whole copy was taken by the ruin rule");
    if (coopbuild::BuildRepairOrderVerdict(95, 0, 1, 0) != coopbuild::kBuildRepairOrderNone) Fail("a repair on this game's own ruin was taken by the copy rule");
    if (coopbuild::BuildRepairOrderVerdict(2, 1, 1, 0) != coopbuild::kBuildRepairOrderNone) Fail("a build order was taken by the repair rule");
    {   /* T-903 (decision 920 a): who may help, the repair verdict on a damaged copy, the owner's repair amount */
        if (coopbuild::HelpAllowedFor(1, 0, 1) != 1 || coopbuild::HelpAllowedFor(0, 1, 1) != 1 || coopbuild::HelpAllowedFor(1, 1, 1) != 1)
            Fail("T-903: a faction-mate or a player set friendly was refused help");
        if (coopbuild::HelpAllowedFor(0, 0, 1) != 0) Fail("T-903: a player neither a faction-mate nor set friendly may help");
        if (coopbuild::HelpAllowedFor(1, 0, 0) != 1 || coopbuild::HelpAllowedFor(0, 1, 0) != 0 || coopbuild::HelpAllowedFor(0, 0, 0) != 0)
            Fail("T-903: the faction-mates-only switch is wrong");
        if (coopbuild::HelpAllowed(0, 1) != coopbuild::HelpAllowedFor(0, 1, coopbuild::kBuildHelpFriendHelps) || coopbuild::HelpAllowed(0, 0) != 0
            || coopbuild::HelpAllowed(1, 0) != 1) Fail("T-903: HelpAllowed does not follow its switch");
        /* T-903 (F1041): the helper's job verdict and the any-thread end decision - every branch */
        if (coopbuild::BuildHelpJobVerdictFor(1, 1, 1, 1) != coopbuild::kBuildHelpJobAllowed || coopbuild::BuildHelpJobVerdictFor(0, 1, 0, 1) != coopbuild::kBuildHelpJobAllowed)
            Fail("T-903 F1041: an allowed helper's job was not ALLOWED");
        if (coopbuild::BuildHelpJobVerdictFor(0, 0, 1, 1) != coopbuild::kBuildHelpJobUnknown) Fail("T-903 F1041: an unknown own slot gave a refusal");
        if (coopbuild::BuildHelpJobVerdictFor(1, 0, 0, 1) != coopbuild::kBuildHelpJobUnknown) Fail("T-903 F1041: an unread stance gave a refusal while friends may help");
        if (coopbuild::BuildHelpJobVerdictFor(1, 0, 1, 1) != coopbuild::kBuildHelpJobRefused) Fail("T-903 F1041: a known refusal was not REFUSED");
        if (coopbuild::BuildHelpJobVerdictFor(1, 0, 0, 0) != coopbuild::kBuildHelpJobRefused) Fail("T-903 F1041: faction-mates only - the stance is not needed for a refusal");
        if (coopbuild::BuildHelpJobVerdict(1, 0, 1) != coopbuild::BuildHelpJobVerdictFor(1, 0, 1, coopbuild::kBuildHelpFriendHelps)) Fail("T-903 F1041: BuildHelpJobVerdict does not follow its switch");
        if (coopbuild::BuildHelpJobEnd(1, 1, coopbuild::kBuildHelpJobRefused, 0, 2000) != 1 || coopbuild::BuildHelpJobEnd(1, 1, coopbuild::kBuildHelpJobRefused, 2000, 2000) != 1)
            Fail("T-903 F1041: a fresh refusal of this game's own helper did not end the job");
        if (coopbuild::BuildHelpJobEnd(1, 1, coopbuild::kBuildHelpJobRefused, 2001, 2000) != 0) Fail("T-903 F1041: a stale refusal ended a job");
        if (coopbuild::BuildHelpJobEnd(0, 1, coopbuild::kBuildHelpJobRefused, 0, 2000) != 0) Fail("T-903 F1041: a job on a piece no stand-in holds (this game's own) was ended");
        if (coopbuild::BuildHelpJobEnd(1, 0, coopbuild::kBuildHelpJobRefused, 0, 2000) != 0) Fail("T-903 F1041: another game's character's job was ended");
        if (coopbuild::BuildHelpJobEnd(1, 1, coopbuild::kBuildHelpJobAllowed, 0, 2000) != 0 || coopbuild::BuildHelpJobEnd(1, 1, coopbuild::kBuildHelpJobUnknown, 0, 2000) != 0)
            Fail("T-903 F1041: an allowed or unknown answer ended a job");
        /* T-903: the standing jobs removed after an ended job - only build / add materials / repair on that piece */
        const int hjx[2] = { 77, 0 };
        if (coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskBuild, 1, 0, 0) != 1 || coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskAddMaterials, 1, 0, 0) != 1
            || coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskRepair, 1, 0, 0) != 1 || coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskKeptWork, 1, 0, 0) != 1)
            Fail("T-903: a standing build / add-materials / repair / kept-work (125) job on the ended job's piece was not removed");
        if (coopbuild::BuildHelpStandingDrop(77, 1, hjx, 2) != 1) Fail("T-903: a type the engine's records name for a help job was not removed");
        if (coopbuild::BuildHelpStandingDrop(77, 1, 0, 0) != 0 || coopbuild::BuildHelpStandingDrop(78, 1, hjx, 2) != 0)
            Fail("T-903: a standing job of a type no help record names was removed");
        if (coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskBuild, 0, hjx, 2) != 0 || coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskKeptWork, 0, 0, 0) != 0)
            Fail("T-903: a standing job on another piece was removed");
        if (coopbuild::BuildHelpStandingDrop(coopbuild::kBuildTaskDismantle, 1, 0, 0) != 0 || coopbuild::BuildHelpStandingDrop(0, 1, hjx, 2) != 0 || coopbuild::BuildHelpStandingDrop(-1, 1, 0, 0) != 0)
            Fail("T-903: a standing job that is not help (dismantle, another task, unreadable, a 0 in the list) was removed");
        {   /* the kept build / repair job's building finder: equal factions as the engine; another faction only for this game's own
               character on a stand-in row whose help answer is ALLOWED and fresh, never a wall */
            const unsigned int fr = 2000;
            int v = coopbuild::BuildJobFinderVerdict(1, 0, 0, coopbuild::kBuildHelpJobUnknown, 0xFFFFFFFFu, fr, 1);
            if (v != coopbuild::kBuildJobFinderSame || coopbuild::BuildJobFinderTakes(v) != 1) Fail("job finder: equal factions were not taken as the engine takes them");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobAllowed, 0, fr, 0);
            if (v != coopbuild::kBuildJobFinderAllowed || coopbuild::BuildJobFinderTakes(v) != 1) Fail("job finder: a fresh allowed stand-in's building was not taken");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobAllowed, fr, fr, 0);
            if (v != coopbuild::kBuildJobFinderAllowed || coopbuild::BuildJobFinderTakes(v) != 1) Fail("job finder: an allowed answer exactly freshMs old was not taken");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobAllowed, fr + 1, fr, 0);
            if (v != coopbuild::kBuildJobFinderStale || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: a stale allowed answer was taken");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobUnknown, 0, fr, 0);
            if (v != coopbuild::kBuildJobFinderStale || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: an unknown answer was taken");
            v = coopbuild::BuildJobFinderVerdict(0, 0, 1, coopbuild::kBuildHelpJobAllowed, 0, fr, 0);
            if (v != coopbuild::kBuildJobFinderNotMine || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: another game's character (or an NPC) took a stand-in's building");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 0, coopbuild::kBuildHelpJobAllowed, 0, fr, 0);
            if (v != coopbuild::kBuildJobFinderNotStandIn || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: a building of a faction that is no stand-in was taken");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobRefused, 0, fr, 0);
            if (v != coopbuild::kBuildJobFinderRefused || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: a refused stand-in's building was taken");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobRefused, fr + 1, fr, 0);
            if (v != coopbuild::kBuildJobFinderRefused || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: a stale refusal was not a refusal");
            v = coopbuild::BuildJobFinderVerdict(0, 1, 1, coopbuild::kBuildHelpJobAllowed, 0, fr, 1);
            if (v != coopbuild::kBuildJobFinderWall || coopbuild::BuildJobFinderTakes(v) != 0) Fail("job finder: an allowed stand-in's grouped WALL was taken");
            if (coopbuild::BuildJobFinderTakes(-1) != 0 || coopbuild::BuildJobFinderTakes(7) != 0) Fail("job finder: an unknown verdict code was taken");
        }
        {
            unsigned int du = 0; int don = -1;
            if (!joblever::JobDoJobsParse("dojobs 35 1", &du, &don) || du != 35 || don != 1) Fail("T-903: jobtest dojobs on");
            if (!joblever::JobDoJobsParse(" dojobs 0x10 0 ", &du, &don) || du != 16 || don != 0) Fail("T-903: jobtest dojobs off, hex uid");
            if (joblever::JobDoJobsParse("dojobs 35", &du, &don) || joblever::JobDoJobsParse("dojobs 0 1", &du, &don)
                || joblever::JobDoJobsParse("dojobs 35 2", &du, &don) || joblever::JobDoJobsParse("dojobs 35 1 x", &du, &don))
                Fail("T-903: jobtest dojobs accepted a bad line");
        }
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 1, 0) != coopbuild::kBuildRepairOrderDamagedHelp) Fail("T-903: a repair on a damaged copy is not help");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 1, 1) != coopbuild::kBuildRepairOrderRefused) Fail("T-903: a repair on a damaged wall copy is not refused");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 1, 0, 0) != coopbuild::kBuildRepairOrderHelp) Fail("T-903: a repair on a ruin copy is no longer the ruin help");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 1, 0, 1) != coopbuild::kBuildRepairOrderRefused) Fail("T-903: a repair on a wall ruin copy is not refused");
        if (coopbuild::BuildRepairHelpVerdict(95, 1, 0, 0, 0) != coopbuild::kBuildRepairOrderNone) Fail("T-903: a repair on a whole copy was taken as help");
        if (coopbuild::BuildRepairHelpVerdict(95, 0, 0, 1, 0) != coopbuild::kBuildRepairOrderNone) Fail("T-903: a repair on this game's own damaged piece was taken by the copy rule");
        if (coopbuild::BuildRepairHelpVerdict(2, 1, 0, 1, 0) != coopbuild::kBuildRepairOrderNone) Fail("T-903: a build order was taken by the repair rule");
        if (coopbuild::BuildPieceDamaged(1, 0, 50.0f, 100.0f) != 1 || coopbuild::BuildPieceDamaged(1, 0, 100.0f, 100.0f) != 0
            || coopbuild::BuildPieceDamaged(0, 0, 50.0f, 100.0f) != 0 || coopbuild::BuildPieceDamaged(1, 1, 50.0f, 100.0f) != 0
            || coopbuild::BuildPieceDamaged(1, 0, 50.0f, 0.0f) != 0) Fail("T-903: the damaged test is wrong");
        if (coopbuild::BuildRepairApplyTo(40.0f, 30.0f, 100.0f) != 70.0f || coopbuild::BuildRepairApplyTo(90.0f, 30.0f, 100.0f) != 100.0f
            || coopbuild::BuildRepairApplyTo(100.0f, 5.0f, 100.0f) != 100.0f || coopbuild::BuildRepairApplyTo(40.0f, -5.0f, 100.0f) != 40.0f)
            Fail("T-903: the owner's repair amount is wrong");
        if (coopbuild::BuildRepairFinishSafe(100.0f, 100.0f, 1) != 100.0f || coopbuild::BuildRepairFinishSafe(70.0f, 100.0f, 1) != 70.0f)
            Fail("T-903 fold: a complete piece's repair health was held short of what the engine's own repair reaches");
        if (coopbuild::BuildRepairFinishSafe(100.0f, 100.0f, 0) != 100.0f * 0.999f || coopbuild::BuildRepairFinishSafe(70.0f, 100.0f, 0) != 70.0f)
            Fail("T-903 fold: a repair on a piece whose complete byte is clear could reach needed (0x558680 would finish it)");
        if (coopbuild::BuildHelpApplyVerdictFor(1, 1, 0, 0, 1) != coopbuild::kBuildHelpApplyGo || coopbuild::BuildHelpApplyVerdictFor(1, 0, 1, 1, 1) != coopbuild::kBuildHelpApplyGo
            || coopbuild::BuildHelpApplyVerdictFor(1, 1, 0, 0, 0) != coopbuild::kBuildHelpApplyGo)
            Fail("T-903 re-check: a faction-mate's or a friendly player's help was not applied on the owner's game");
        if (coopbuild::BuildHelpApplyVerdictFor(1, 0, 1, 0, 1) != coopbuild::kBuildHelpApplyRefuse || coopbuild::BuildHelpApplyVerdictFor(1, 0, 1, 1, 0) != coopbuild::kBuildHelpApplyRefuse
            || coopbuild::BuildHelpApplyVerdictFor(1, 0, 0, 0, 0) != coopbuild::kBuildHelpApplyRefuse)
            Fail("T-903 re-check: a known non-teammate not set friendly was not refused");
        if (coopbuild::BuildHelpApplyVerdictFor(0, 0, 1, 0, 1) != coopbuild::kBuildHelpApplyHold || coopbuild::BuildHelpApplyVerdictFor(0, 1, 1, 1, 1) != coopbuild::kBuildHelpApplyHold
            || coopbuild::BuildHelpApplyVerdictFor(1, 0, 0, 0, 1) != coopbuild::kBuildHelpApplyHold)
            Fail("T-903 re-check: help was judged while this game's slot or the sender's standing was unknown (must be held)");
        if (coopbuild::BuildHelpApplyVerdict(1, 0, 1, 1) != coopbuild::BuildHelpApplyVerdictFor(1, 0, 1, 1, coopbuild::kBuildHelpFriendHelps))
            Fail("T-903 re-check: BuildHelpApplyVerdict does not follow its switch");
    }
    /* T-903 (F1026): dead handles on a host's interior list count as no piece only when every condition holds */
    {
        coopbuild::BuildDeadHandleIn ok;
        ok.listed = 26; ok.n = 26; ok.cut = 0; ok.torn = 0;
        ok.typed = 26; ok.good = 13; ok.hits = 0; ok.dead = 13; ok.other = 0;
        ok.areaLoaded = 1; ok.setAge = coopbuild::kBuildDeadHandleWait; ok.regRow = 0; ok.copyListRow = 0; ok.removedMark = 0;
        if (coopbuild::BuildDeadHandlesIgnore(ok) != 1) Fail("T-903 F1026: every condition held but the dead handles still held the copy");
        coopbuild::BuildDeadHandleIn x;
        x = ok; x.listed = -1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): an unreadable list size was taken as read whole");
        x = ok; x.n = 25; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): fewer entries read than listed was taken as whole");
        x = ok; x.cut = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): a walk cut short was taken as whole");
        x = ok; x.torn = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): a torn walk was taken as whole");
        x = ok; x.good = 12; x.hits = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): a piece at or near the key was ignored");
        x = ok; x.good = 12; x.other = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a/b): an unresolved handle that is not dead (faulted, ids differ, no key) was ignored");
        x = ok; x.good = 12; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (a): counts that do not add up to typed were trusted");
        x = ok; x.good = 26; x.dead = 0; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (b): no dead handle - not this rule's case");
        x = ok; x.areaLoaded = 0; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (c): an area not fully loaded was trusted");
        x = ok; x.setAge = -1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (d): a dead set seen for the first time was trusted");
        x = ok; x.setAge = coopbuild::kBuildDeadHandleWait - 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (d): a dead set younger than the wait was trusted");
        x = ok; x.setAge = 1000; if (coopbuild::BuildDeadHandlesIgnore(x) != 1) Fail("T-903 F1026 (d): an older unchanged dead set still held the copy");
        x = ok; x.regRow = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (e): a key with a registry row here was made again");
        x = ok; x.copyListRow = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (e): a key with a copy-list row was made again");
        x = ok; x.removedMark = 1; if (coopbuild::BuildDeadHandlesIgnore(x) != 0) Fail("T-903 F1026 (e): a key marked removed was made again");
        /* the set's signature: order-free, any change shows */
        std::vector<unsigned int> t1, t2, t3;
        t1.push_back(2069); t1.push_back(387); t1.push_back(11); t1.push_back(2069); t1.push_back(552); t1.push_back(12);
        t2.push_back(2069); t2.push_back(552); t2.push_back(12); t2.push_back(2069); t2.push_back(387); t2.push_back(11);
        t3 = t1; t3[5] = 13;
        const std::string s1 = coopbuild::BuildDeadSetSig(t1);
        if (s1 != coopbuild::BuildDeadSetSig(t2) || s1 == coopbuild::BuildDeadSetSig(t3) || s1.empty() || !coopbuild::BuildDeadSetSig(std::vector<unsigned int>()).empty())
            Fail("T-903 F1026: the dead set's signature depends on the read order or misses a changed handle");
        /* the per-host memory: first sight -1, the same set ages, a changed set restarts, an empty set clears */
        int had = 0; std::string ks; unsigned int at = 0;
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, s1, 100) != -1 || had != 1 || at != 100) Fail("T-903 F1026 (d): a first-seen dead set was not remembered as new");
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, s1, 130) != 30) Fail("T-903 F1026 (d): the same dead set did not age");
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, coopbuild::BuildDeadSetSig(t3), 160) != -1 || at != 160) Fail("T-903 F1026 (d): a changed dead set did not restart the wait");
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, coopbuild::BuildDeadSetSig(t3), 189) != 29) Fail("T-903 F1026 (d): the restarted wait did not count from the change");
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, std::string(), 200) != -1 || had != 0) Fail("T-903 F1026 (d): an empty dead set was remembered");
        had = 1; ks = s1; at = 0xFFFFFFF0u;
        if (coopbuild::BuildDeadSetStep(&had, &ks, &at, s1, 0x0000000Eu) != 30) Fail("T-903 F1026 (d): the safe-point clock's wrap broke the age");
    }
    /* a copy made a ruin here */
    if (coopbuild::BuildLocalDestroyVerdict(1, 0, 1, 1, 0) != coopbuild::kBuildLocalDestroyRevert) Fail("a copy ruined here while the owner's is whole is not put back");
    if (coopbuild::BuildLocalDestroyVerdict(1, 0, 1, 1, 1) != coopbuild::kBuildLocalDestroyIgnore) Fail("a copy ruined here while the owner's is a ruin was put back");
    if (coopbuild::BuildLocalDestroyVerdict(1, 0, 1, 0, 0) != coopbuild::kBuildLocalDestroyWait) Fail("a copy ruined here with no owner STATE does not wait");
    if (coopbuild::BuildLocalDestroyVerdict(1, 1, 1, 1, 0) != coopbuild::kBuildLocalDestroyIgnore) Fail("the mod's own apply was put back");
    if (coopbuild::BuildLocalDestroyVerdict(0, 0, 1, 1, 0) != coopbuild::kBuildLocalDestroyIgnore) Fail("an own piece was taken by the copy rule");
    if (coopbuild::BuildLocalDestroyVerdict(1, 0, 0, 1, 0) != coopbuild::kBuildLocalDestroyIgnore) Fail("a copy made whole here was put back");
    /* T-221: a wall copy's action on the owner's STATE */
    {
        float wp = -1.0f;
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallProgress || std::fabs(wp - 5.0f) > 0.0001f)
            Fail("an own-record wall copy did not take the owner's share of its own needed");
        if (coopbuild::BuildWallCopyAction(1, 0, 1.0f, 8.0f, 2.0f, &wp) != coopbuild::kBuildWallProgress || std::fabs(wp - 0.25f) > 0.0001f)
            Fail("an own-record wall copy with a smaller needed was not scaled down");
        if (coopbuild::BuildWallCopyAction(1, 0, 4.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallProgress || !(wp < 10.0f) || std::fabs(wp - 9.99f) > 0.0001f)
            Fail("an own-record wall copy at the owner's full progress was not kept at 0.999 of its needed");
        if (coopbuild::BuildWallCopyAction(1, 0, 9.0f, 4.0f, 2.0f, &wp) != coopbuild::kBuildWallProgress || std::fabs(wp - 1.998f) > 0.0001f)
            Fail("an own-record wall copy went past 0.999 of its needed");
        if (coopbuild::BuildWallCopyAction(1, 0, -1.0f, 4.0f, 2.0f, &wp) != coopbuild::kBuildWallProgress || wp != 0.0f)
            Fail("a negative progress was written to a wall copy");
        if (coopbuild::BuildWallCopyAction(0, 0, 2.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallMatsOnly || wp != 0.0f)
            Fail("a shared-record wall copy took a progress write");
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, 0.0f, 10.0f, &wp) != coopbuild::kBuildWallMatsOnly)
            Fail("a STATE with no needed was scaled onto a wall copy");
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, 4.0f, 0.0f, &wp) != coopbuild::kBuildWallMatsOnly)
            Fail("a wall copy with no needed of its own took a progress write");
        if (coopbuild::BuildWallCopyAction(1, 1, 4.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete)
            Fail("a complete STATE did not complete an own-record wall copy");
        if (coopbuild::BuildWallCopyAction(0, 1, 4.0f, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallComplete)
            Fail("a complete STATE did not complete a shared-record wall copy");
        if (coopbuild::BuildWallCopyAction(1, 1, 1.0f, 4.0f, 0.0f, &wp) != coopbuild::kBuildWallComplete)
            Fail("a complete STATE on a wall copy with no readable needed was not completed");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, nan, 10.0f, &wp) != coopbuild::kBuildWallMatsOnly || wp != 0.0f)
            Fail("a NaN needed in the STATE was scaled onto a wall copy");
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, 4.0f, nan, &wp) != coopbuild::kBuildWallMatsOnly || wp != 0.0f)
            Fail("a wall copy with a NaN needed of its own took a progress write");
        if (coopbuild::BuildWallCopyAction(1, 0, nan, 4.0f, 10.0f, &wp) != coopbuild::kBuildWallProgress || wp != 0.0f)
            Fail("a NaN progress was not written to a wall copy as 0");
        if (coopbuild::BuildWallCopyAction(1, 0, 2.0f, 3.2e38f, 10.0f, &wp) != coopbuild::kBuildWallMatsOnly)
            Fail("a STATE with a needed past the float range was scaled onto a wall copy");
    }
    /* T-221: a wall's record is its alone only with its own record and no group (a group's lead holds the record at its own +0x160) */
    if (coopbuild::BuildWallKeepsOwnRecord(1, 0) != 1) Fail("an ungrouped wall with its own record was not taken as alone");
    if (coopbuild::BuildWallKeepsOwnRecord(1, 3) != 0) Fail("a group's lead was taken as keeping its record alone");
    if (coopbuild::BuildWallKeepsOwnRecord(0, 0) != 0) Fail("a group member was taken as keeping its record alone");
    if (coopbuild::BuildWallKeepsOwnRecord(-1, 0) != -1 || coopbuild::BuildWallKeepsOwnRecord(1, -1) != -1)
        Fail("an unreadable record or group count was not reported as not known");
    /* T-221: a group's members are looked for in the lead's sector and the eight around it */
    if (coopbuild::BuildWallGroupNear(10, 10, 10, 10) != 1 || coopbuild::BuildWallGroupNear(10, 10, 9, 11) != 1 || coopbuild::BuildWallGroupNear(10, 10, 11, 9) != 1)
        Fail("a piece in or beside the lead's sector was not looked at");
    if (coopbuild::BuildWallGroupNear(10, 10, 12, 10) != 0 || coopbuild::BuildWallGroupNear(10, 10, 10, 8) != 0)
        Fail("a piece two sectors from the lead was looked at");
    /* T-221: a wall copy's complete-now call is retried fast, then slowly for as long as the STATE is held */
    if (coopbuild::BuildWallCompleteRetryGap(0, 10, 30, 20) != 30 || coopbuild::BuildWallCompleteRetryGap(9, 10, 30, 20) != 30)
        Fail("the first complete-now retries were not at the plain gap");
    if (coopbuild::BuildWallCompleteRetryGap(10, 10, 30, 20) != 600 || coopbuild::BuildWallCompleteRetryGap(500, 10, 30, 20) != 600)
        Fail("the complete-now retries after the fast ones were not at the slow gap");
    /* T-221: the kept-piece lay completes this player's own wall from its own record only when that cannot finish anything else */
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 0, 0, 0, 0, -1) != 1) Fail("an ungrouped own wall recorded finished was not completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(0, 0, 1, 0, 0, 0, 0, -1) != 0) Fail("a piece that is not a wall was completed from the record");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 1, 1, 0, 0, 0, 0, -1) != 0) Fail("a ruin here was completed from the record (a free repair)");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 1, 0, 0, 0, -1) != 0) Fail("a wall the record holds as a ruin was completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 0, 0, 0, 0, 0, -1) != 0) Fail("a wall the record holds unfinished was completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 0, 1, 1, 4, 3) != 1) Fail("a group of this player's walls all recorded finished was not completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 0, 1, 0, 4, 3) != 0) Fail("a group holding a wall not recorded finished (or another player's) was completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 0, 1, 1, 3, 3) != 0) Fail("a group with a member not found was completed");
    if (coopbuild::BuildKeptOwnCompleteVerdict(1, 0, 1, 0, 1, 1, 4, -1) != 0) Fail("a group whose lead was not found was completed");
}

/* T-903: a helper's materials put into a copy off the main thread - the gain against the copy's baseline and the verdict */
void t_t903_help_harvest()
{
    const float now[3] = { 5.0f, 2.0f, 7.0f };
    const float base[3] = { 3.0f, 2.0f, 9.0f };
    const float before[3] = { 4.0f, 1.0f, 7.0f };
    float g[3] = { -1.0f, -1.0f, -1.0f };
    if (coopbuild::BuildHelpMatGain(now, 3, 1, base, -1, 0, g) != 1 || g[0] != 2.0f || g[1] != 0.0f || g[2] != 0.0f)
        Fail("T-903 F1036: the gain against a known baseline is wrong (a rise counts, no change and a drop are 0)");
    g[0] = -1.0f; g[1] = -1.0f; g[2] = -1.0f;
    if (coopbuild::BuildHelpMatGain(now, 3, 1, base, 3, before, g) != 1 || g[0] != 2.0f)
        Fail("T-903 F1036: a known baseline did not win over the amounts read before the call");
    g[0] = -1.0f; g[1] = -1.0f; g[2] = -1.0f;
    if (coopbuild::BuildHelpMatGain(now, 3, 0, base, 2, before, g) != 1 || g[0] != 1.0f || g[1] != 1.0f || g[2] != 0.0f)
        Fail("T-903 F1036: with no baseline the amounts read before the call were not used (and the rest taken as no gain)");
    g[0] = -1.0f; g[1] = -1.0f; g[2] = -1.0f;
    if (coopbuild::BuildHelpMatGain(now, 3, 0, base, -1, 0, g) != 0 || g[0] != 0.0f || g[1] != 0.0f || g[2] != 0.0f)
        Fail("T-903 F1036: with no baseline and nothing read before, a gain was counted (must be 0)");
    {
        const float tiny[1] = { 1.00005f };
        const float one[1] = { 1.0f };
        float t[1] = { -1.0f };
        if (coopbuild::BuildHelpMatGain(tiny, 1, 1, one, -1, 0, t) != 0 || t[0] != 0.0f) Fail("T-903 F1036: a gain within the epsilon counted");
        const float more[1] = { 1.001f };
        if (coopbuild::BuildHelpMatGain(more, 1, 1, one, -1, 0, t) != 1 || !(t[0] > coopbuild::kBuildHelpMatEps)) Fail("T-903 F1036: a gain past the epsilon did not count");
        if (coopbuild::BuildHelpMatGain(more, 0, 1, one, -1, 0, t) != 0) Fail("T-903 F1036: no slot read counted a gain");
    }
    if (coopbuild::BuildHelpHarvestVerdict(0, 1, 0, 0, 1, 1, 1) != coopbuild::kBuildHarvestGone) Fail("T-903 F1036: a piece not found here was harvested");
    if (coopbuild::BuildHelpHarvestVerdict(1, 0, 0, 0, 1, 1, 1) != coopbuild::kBuildHarvestNotCopy) Fail("T-903 F1036: a piece that is no stand-in's copy was harvested");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 1, 0, 1, 1, 1) != coopbuild::kBuildHarvestSkip) Fail("T-903 F1036: a grouped wall copy was harvested");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 1, 1, 1, 0) != coopbuild::kBuildHarvestSkip) Fail("T-903 F1036: a dismantled copy was harvested");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 0, 1, 1) != coopbuild::kBuildHarvestNoBase || coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 0, 0, 0) != coopbuild::kBuildHarvestNoBase)
        Fail("T-903 F1036: a copy with no baseline was measured (its delivery would be lost when the baseline moved)");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 1, 0, 0) != coopbuild::kBuildHarvestNoGain) Fail("T-903 F1036: no gain was recorded or refused");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 1, 1, 1) != coopbuild::kBuildHarvestRecord) Fail("T-903 F1036: an allowed helper's materials were not recorded as help");
    if (coopbuild::BuildHelpHarvestVerdict(1, 1, 0, 0, 1, 1, 0) != coopbuild::kBuildHarvestRefuse) Fail("T-903 F1036: a helper the help rule refuses was not refused");
    if (coopbuild::BuildHelpWholeItems(3.0f) != 3 || coopbuild::BuildHelpWholeItems(2.9995f) != 3 || coopbuild::BuildHelpWholeItems(2.5f) != 2
        || coopbuild::BuildHelpWholeItems(0.0f) != 0 || coopbuild::BuildHelpWholeItems(-1.0f) != 0) Fail("T-903 F1036: the whole items of a gain are wrong");
}

/* build1-b: MSG_BUILD PLACE - a construction one game's player placed, copied as coop-peer on the other. */
void t_build1b_place_wire()
{
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildPlace;
    m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base";
    m.pos[0] = -51234.5f; m.pos[1] = 812.25f; m.pos[2] = 1234.5f;
    m.rot[0] = 0.5f; m.rot[1] = 0.0f; m.rot[2] = 0.8660254f; m.rot[3] = 0.0f;
    m.complete = 0; m.progress = 0.0f; m.needed = 12.5f;
    std::vector<char> b;
    const size_t want = 1 + 1 + m.key.size() + 1 + m.sid.size() + 12 + 16 + 1 + 4 + 4 + 1 + 1 + 4 + 1 + 2;   /* form, floor, outside, the two-byte owner number */
    if (!coopbuild::EncodeBuild(&b, m) || b.size() != want) { Fail("a PLACE did not encode to its exact size"); return; }
    coopbuild::BuildMsg o;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.kind != 1 || o.key != m.key || o.sid != m.sid
        || o.pos[0] != m.pos[0] || o.pos[1] != m.pos[1] || o.pos[2] != m.pos[2] || o.rot[0] != 0.5f || o.rot[2] != m.rot[2]
        || o.rot[1] != 0.0f || o.rot[3] != 0.0f || o.complete != 0 || o.progress != 0.0f || o.needed != 12.5f || !o.hostKey.empty())
        Fail("a PLACE did not survive the round trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("a truncated PLACE was accepted"); break; }
    std::vector<char> k(b); k[0] = 6;   /* build1-c: kind 2 is STATE; build1-d: kind 3 is REMOVE; build1-f: kind 4 is ROSTER_REQUEST; house1b: 5 is HAND_ACK */
    if (coopbuild::DecodeBuild(&k[0], k.size(), &o) != coopbuild::kBuildDecodeBadKind) Fail("a BUILD kind 6 was accepted (a PLACE buffer)");
    {   /* house1b (protocol 75): HAND_ACK = kind 5 + the key, nothing else */
        coopbuild::BuildMsg ak; ak.kind = coopbuild::kBuildHandAck; ak.key = m.key;
        std::vector<char> ab;
        if (!coopbuild::EncodeBuild(&ab, ak) || ab.size() != 2 + m.key.size() || coopbuild::DecodeBuild(&ab[0], ab.size(), &o) != coopbuild::kBuildDecodeOk
            || o.kind != coopbuild::kBuildHandAck || o.key != m.key)
            Fail("a HAND_ACK did not survive the round trip");
        std::vector<char> at2(ab); at2.push_back(0);
        if (coopbuild::DecodeBuild(&at2[0], at2.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a HAND_ACK with a trailing byte was accepted");
        for (size_t cut = 0; cut < ab.size(); ++cut)
            if (coopbuild::DecodeBuild(&ab[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("a truncated HAND_ACK was accepted"); break; }
        coopbuild::BuildMsg ae; ae.kind = coopbuild::kBuildHandAck; std::vector<char> aeb;
        if (coopbuild::EncodeBuild(&aeb, ae)) Fail("a HAND_ACK with no key was encoded");
        const char empty2[2] = { 5, 0 };
        if (coopbuild::DecodeBuild(empty2, 2, &o) == coopbuild::kBuildDecodeOk) Fail("a HAND_ACK with an empty key was accepted");
    }
    std::vector<char> c(b); c[want - 18] = 2;   /* the complete byte (8 trailing bytes after the host key: form, floor, outside, the two-byte owner number) */
    if (coopbuild::DecodeBuild(&c[0], c.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with complete=2 was accepted");
    std::vector<char> zk(b); zk[1] = 0;
    if (coopbuild::DecodeBuild(&zk[0], zk.size(), &o) == coopbuild::kBuildDecodeOk) Fail("a PLACE with an empty key was accepted");
    const unsigned int nanBits = 0x7FC00000u;
    std::vector<char> nd(b); std::memcpy(&nd[1 + 1 + m.key.size() + 1 + m.sid.size() + 4], &nanBits, 4);
    if (coopbuild::DecodeBuild(&nd[0], nd.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with a NaN position was accepted");
    coopbuild::BuildMsg h = m; h.hostKey = "5-gamedata.base@31,22:1,2,3"; h.complete = 1; h.progress = 12.5f;
    std::vector<char> hb;
    if (!coopbuild::EncodeBuild(&hb, h) || coopbuild::DecodeBuild(&hb[0], hb.size(), &o) != coopbuild::kBuildDecodeOk
        || o.hostKey != h.hostKey || o.complete != 1 || o.progress != 12.5f || o.key != m.key)
        Fail("a PLACE with a host key did not survive the round trip");
    coopbuild::BuildMsg nan = m; std::memcpy(&nan.pos[1], &nanBits, 4);
    std::vector<char> nb;
    if (coopbuild::EncodeBuild(&nb, nan) || !nb.empty()) Fail("a NaN position was encoded");
    coopbuild::BuildMsg e = m; e.key.clear(); std::vector<char> eb;
    if (coopbuild::EncodeBuild(&eb, e)) Fail("a PLACE with no key was encoded");
    coopbuild::BuildMsg lk = m; lk.key = std::string(coopbuild::kBuildMaxKey + 1, 'k'); std::vector<char> lb;
    if (coopbuild::EncodeBuild(&lb, lk)) Fail("an over-long key was encoded");
    coopbuild::BuildMsg nk = m; nk.kind = 0; std::vector<char> kb;
    if (coopbuild::EncodeBuild(&kb, nk)) Fail("a BUILD of kind 0 was encoded");
}

/* build1-c: MSG_BUILD STATE (progress, needed, complete, delivered mats) and the review-build1b decode gaps for PLACE. */
void t_build1c_state_wire()
{
    coopbuild::BuildMsg s; s.kind = coopbuild::kBuildState;
    s.key = "1459-gamedata.base@43,11@538554,-945925,21043";
    s.progress = 0.27f; s.needed = 1.0f; s.complete = 0; s.nMats = 3;
    s.mats[0] = 2.0f; s.mats[1] = 0.0f; s.mats[2] = 1.5f;
    std::vector<char> b;
    const size_t want = 1 + 1 + s.key.size() + 4 + 4 + 1 + 1 + 3 * 4;
    if (!coopbuild::EncodeBuild(&b, s) || b.size() != want) { Fail("a STATE did not encode to its exact size"); return; }
    coopbuild::BuildMsg o;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.kind != 2 || o.key != s.key
        || o.progress != 0.27f || o.needed != 1.0f || o.complete != 0 || o.nMats != 3 || o.mats[0] != 2.0f || o.mats[1] != 0.0f
        || o.mats[2] != 1.5f || !o.sid.empty())
        Fail("a STATE did not survive the round trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("a truncated STATE was accepted"); break; }
    std::vector<char> c(b); c[1 + 1 + s.key.size() + 8] = 8;   /* the complete byte: bit 0 complete, bit 1 destroyed, bit 2 broken - bit 3 is refused */
    if (coopbuild::DecodeBuild(&c[0], c.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a STATE complete byte with bit 3 set was accepted");
    std::vector<char> n17(b); n17[1 + 1 + s.key.size() + 9] = 17; n17.resize(n17.size() + 14 * 4, 0);
    if (coopbuild::DecodeBuild(&n17[0], n17.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a STATE with 17 materials was accepted");
    std::vector<char> ek(b); ek[1] = 0;
    if (coopbuild::DecodeBuild(&ek[0], ek.size(), &o) == coopbuild::kBuildDecodeOk) Fail("a STATE with an empty key was accepted");
    const unsigned int infBits = 0x7F800000u, ninfBits = 0xFF800000u, nanBits = 0x7FC00000u;
    std::vector<char> ip(b); std::memcpy(&ip[1 + 1 + s.key.size()], &infBits, 4);
    if (coopbuild::DecodeBuild(&ip[0], ip.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a STATE with +inf progress was accepted");
    std::vector<char> nm(b); std::memcpy(&nm[1 + 1 + s.key.size() + 10 + 4], &nanBits, 4);
    if (coopbuild::DecodeBuild(&nm[0], nm.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a STATE with a NaN material was accepted");
    coopbuild::BuildMsg full = s; full.nMats = 16; full.complete = 1; full.progress = 1.0f;
    for (int i = 0; i < 16; ++i) full.mats[i] = (float)i;
    std::vector<char> fb;
    if (!coopbuild::EncodeBuild(&fb, full) || coopbuild::DecodeBuild(&fb[0], fb.size(), &o) != coopbuild::kBuildDecodeOk
        || o.nMats != 16 || o.mats[15] != 15.0f || o.complete != 1)
        Fail("a STATE with 16 materials did not survive the round trip");
    coopbuild::BuildMsg over = s; over.nMats = 17; std::vector<char> ob;
    if (coopbuild::EncodeBuild(&ob, over) || !ob.empty()) Fail("a STATE with 17 materials was encoded");
    coopbuild::BuildMsg ninf = s; std::memcpy(&ninf.needed, &ninfBits, 4); std::vector<char> nb;
    if (coopbuild::EncodeBuild(&nb, ninf)) Fail("a STATE with -inf needed was encoded");
    coopbuild::BuildMsg none = s; none.nMats = 0; std::vector<char> zb;
    if (!coopbuild::EncodeBuild(&zb, none) || zb.size() != want - 12 || coopbuild::DecodeBuild(&zb[0], zb.size(), &o) != coopbuild::kBuildDecodeOk
        || o.nMats != 0)
        Fail("a STATE with no materials did not survive the round trip");

    /* PLACE: the review-build1b decode gaps */
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildPlace;
    m.key = std::string(coopbuild::kBuildMaxKey, 'k'); m.sid = "1459-gamedata.base";
    m.pos[0] = 10.0f; m.pos[1] = 20.0f; m.pos[2] = 30.0f;
    std::vector<char> pb;
    if (!coopbuild::EncodeBuild(&pb, m) || coopbuild::DecodeBuild(&pb[0], pb.size(), &o) != coopbuild::kBuildDecodeOk
        || o.key.size() != 63)
        Fail("a PLACE with a 63-byte key did not survive the round trip");
    std::vector<char> k64(pb); k64[1] = 64; k64.insert(k64.begin() + 2, 'k');
    if (coopbuild::DecodeBuild(&k64[0], k64.size(), &o) != coopbuild::kBuildDecodeBadKey) Fail("a PLACE with a 64-byte key was accepted");
    std::vector<char> es; es.push_back(1); es.push_back(3); es.push_back('a'); es.push_back('b'); es.push_back('c'); es.push_back(0);
    es.resize(es.size() + 12 + 16 + 1 + 4 + 4 + 1, 0);
    { const float one = 1.0f; std::memcpy(&es[6 + 12], &one, 4); }
    if (coopbuild::DecodeBuild(&es[0], es.size(), &o) != coopbuild::kBuildDecodeBadKey) Fail("a PLACE with an empty sid was accepted");
    coopbuild::BuildMsg nosid = m; nosid.sid.clear(); std::vector<char> nsb;
    if (coopbuild::EncodeBuild(&nsb, nosid)) Fail("a PLACE with no sid was encoded");
    const size_t posAt = 1 + 1 + m.key.size() + 1 + m.sid.size();
    std::vector<char> pi(pb); std::memcpy(&pi[posAt], &infBits, 4);
    if (coopbuild::DecodeBuild(&pi[0], pi.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with a +inf position was accepted");
    std::vector<char> pn(pb); std::memcpy(&pn[posAt + 8], &ninfBits, 4);
    if (coopbuild::DecodeBuild(&pn[0], pn.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with a -inf position was accepted");
    std::vector<char> zq(pb); std::memset(&zq[posAt + 12], 0, 16);
    if (coopbuild::DecodeBuild(&zq[0], zq.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with a zero quaternion was accepted");
    std::vector<char> hq(pb); { const float two = 2.0f; std::memcpy(&hq[posAt + 12], &two, 4); std::memcpy(&hq[posAt + 16], &two, 4); }
    if (coopbuild::DecodeBuild(&hq[0], hq.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with |q|^2 = 8 was accepted");
    coopbuild::BuildMsg zr = m; zr.rot[0] = 0.0f; std::vector<char> zrb;
    if (coopbuild::EncodeBuild(&zrb, zr)) Fail("a PLACE with a zero quaternion was encoded");
    coopbuild::BuildMsg tilt = m; tilt.rot[0] = 0.7071068f; tilt.rot[2] = 0.7071068f; std::vector<char> tb;
    if (!coopbuild::EncodeBuild(&tb, tilt) || coopbuild::DecodeBuild(&tb[0], tb.size(), &o) != coopbuild::kBuildDecodeOk)
        Fail("a PLACE with a unit quaternion (90 degrees) was refused");
}

/* build1h H1: the PLACE owner-slot byte (protocol 74) */
void t_build1h_place_owner_slot()
{
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildPlace;
    m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base";
    m.hostKey = "57-gamedata.base@31,22:-512300,8100,12300"; m.hostForm = coopbuild::kBuildHostIndoorsOnly;
    if (m.ownerSlot != coopbuild::kBuildOwnerSender) Fail("a PLACE's owner slot must default to the sender");
    std::vector<char> b; coopbuild::BuildMsg o;
    if (!coopbuild::EncodeBuild(&b, m) || coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.ownerSlot != coopbuild::kBuildOwnerSender)
        Fail("a sender-owned PLACE did not survive the round trip");
    m.ownerSlot = 3; std::vector<char> h;
    if (!coopbuild::EncodeBuild(&h, m) || h.size() != b.size() || h[h.size() - 2] != 3 || h.back() != 0 || coopbuild::DecodeBuild(&h[0], h.size(), &o) != coopbuild::kBuildDecodeOk
        || o.ownerSlot != 3 || o.hostForm != coopbuild::kBuildHostIndoorsOnly || o.hostKey != m.hostKey)
        Fail("a handed PLACE (owner slot 3) did not survive the round trip");
    std::vector<char> cut(h.begin(), h.end() - 1);
    if (coopbuild::DecodeBuild(&cut[0], cut.size(), &o) == coopbuild::kBuildDecodeOk) Fail("a PLACE cut inside its owner number was accepted");
}

/* build1h H1: a piece placed inside another player's house is the house owner's; the shared build record decides the box writer first */
void t_build1h_house_owner()
{
    using namespace coopown;
    int why = -1;
    if (HouseOwnerOfNewPiece(0, kOwnNone, -1, 1, &why) != -1 || why != kHoNoHost) Fail("a free-standing piece stays the placer's");
    if (HouseOwnerOfNewPiece(1, kOwnNone, -1, 1, &why) != -1 || why != kHoTown) Fail("a town house: the placer keeps it (decision 5)");
    if (HouseOwnerOfNewPiece(1, kOwnMe, 1, 1, &why) != -1 || why != kHoMine) Fail("my own house: nothing changes");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, 3, 1, &why) != 3 || why != kHoHanded) Fail("slot 3's house: handed to slot 3");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, 3, -1, &why) != 3 || why != kHoHanded) Fail("slot 3's house, my slot unknown: still slot 3's");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, 1, 1, &why) != -1 || why != kHoMine) Fail("a house naming my own slot is mine");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, -1, 1, &why) != -1 || why != kHoUnresolved) Fail("owner slot unknown: the placer keeps it, unresolved");
    if (HouseOwnerOfNewPiece(1, kOwnUnknown, -1, 1, &why) != -1 || why != kHoUnresolved) Fail("unknown owner: unresolved");
    if (HouseOwnerOfNewPiece(1, kOwnShared, -1, 1, &why) != -1 || why != kHoUnresolved) Fail("shared faction: unresolved");
    if (HouseOwnerOfNewPiece(1, kOwnUnreadable, -1, 1, &why) != -1 || why != kHoUnresolved) Fail("unreadable house: unresolved");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, kHandSlotMax + 1, 1, &why) != -1 || why != kHoUnresolved) Fail("a number past the notebook's slots: unresolved");
    if (HouseOwnerOfNewPiece(1, kOwnPeer, kHandSlotMax, 1, &why) != kHandSlotMax) Fail("slot 1023 fits the owner number");
    int slot = -9, via = -9;
    if (PickOwnerRec(1, 3, 1, 1, kOwnMe, 1, kOwnMe, 1, &slot, &via) != kOwnPeer || slot != 3 || via != 2) Fail("the record beats the host");
    if (PickOwnerRec(1, 1, 1, 0, kOwnNone, -1, kOwnPeer, 3, &slot, &via) != kOwnMe || slot != 1 || via != 2) Fail("a record naming my slot is mine");
    if (PickOwnerRec(0, -1, 1, 1, kOwnPeer, 3, kOwnMe, 1, &slot, &via) != kOwnPeer || slot != 3 || via != 1) Fail("no record: the host");
    if (PickOwnerRec(1, -1, 1, 0, kOwnNone, -1, kOwnMe, 1, &slot, &via) != kOwnMe || slot != 1 || via != 0) Fail("a record with no slot falls through");
    if (PickOwnerRec(1, 2, -1, 0, kOwnNone, -1, kOwnMe, -1, &slot, &via) != kOwnPeer || slot != 2) Fail("my slot unknown: a record slot is another's");
    /* THE TWO-GAME PROPERTY for the re-check's case: B (slot 2) places a form-2 chest (no layout pointer) in A's (slot 1) house. After
       the hand-off both games hold the record 'slot 1'; the engine reads the chest's own faction: A's own on A, coop-p1 on B. */
    int sa = -1, va = -1, sb = -1, vb = -1, w = -1;
    const int ca = PickOwnerRec(1, 1, 1, 0, kOwnNone, -1, OwnerClassOf(1, 0, -1, 1, 1, 1), 1, &sa, &va);
    const int cb = PickOwnerRec(1, 1, 2, 0, kOwnNone, -1, OwnerClassOf(0, 1, 1, 0, 1, 2), 1, &sb, &vb);
    if (ca != kOwnMe || cb != kOwnPeer || sa != 1 || sb != 1) Fail("both games must name A (slot 1) the owner");
    if (OwnerRung(ca, sa, 1, &w) != kRungMine || OwnerRung(cb, sb, 1, &w) != kRungHeld) Fail("exactly one writer: A's game");
    /* without the record (after a reload) the engine read alone agrees: the piece's own faction IS the house owner now */
    const int ea = PickOwner(0, kOwnNone, -1, OwnerClassOf(1, 0, -1, 1, 1, 1), 1, &sa, &va);
    const int eb = PickOwner(0, kOwnNone, -1, OwnerClassOf(0, 1, 1, 0, 1, 2), 1, &sb, &vb);
    if (ea != kOwnMe || eb != kOwnPeer || sa != 1 || sb != 1) Fail("the fallback must agree once the piece is the house owner's");
}

/* build1-e: MSG_BUILD PLACE for furniture - the host key, the furnitureOf form, floor and outside (protocol 62). */
void t_build1e_furniture_wire()
{
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildPlace;
    m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base";
    m.hostKey = "57-gamedata.base@31,22:-512300,8100,12300";
    m.pos[0] = 17.5f; m.pos[1] = 0.0f; m.pos[2] = -35.25f;
    m.hostForm = coopbuild::kBuildHostSub; m.floor = 2; m.outside = 1; m.needed = 10.0f;
    std::vector<char> b;
    const size_t want = 1 + 1 + m.key.size() + 1 + m.sid.size() + 12 + 16 + 1 + 4 + 4 + 1 + m.hostKey.size() + 1 + 4 + 1 + 2;   /* + the two-byte owner number */
    if (!coopbuild::EncodeBuild(&b, m) || b.size() != want) { Fail("a furniture PLACE did not encode to its exact size"); return; }
    coopbuild::BuildMsg o;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.hostKey != m.hostKey || o.hostForm != 1
        || o.floor != 2 || o.outside != 1 || o.pos[0] != 17.5f || o.pos[2] != -35.25f || o.key != m.key || o.needed != 10.0f)
        Fail("a furniture PLACE did not survive the round trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("a truncated furniture PLACE was accepted"); break; }
    std::vector<char> f3(b); f3[want - 8] = 3;
    if (coopbuild::DecodeBuild(&f3[0], f3.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with host form 3 was accepted");
    std::vector<char> o2(b); o2[want - 3] = 2;
    if (coopbuild::DecodeBuild(&o2[0], o2.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with outside=2 was accepted");
    std::vector<char> fl(b); { const int big = 5000; std::memcpy(&fl[want - 7], &big, 4); }
    if (coopbuild::DecodeBuild(&fl[0], fl.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a PLACE with floor 5000 was accepted");
    coopbuild::BuildMsg free1 = m; free1.hostKey.clear(); std::vector<char> fb;
    if (coopbuild::EncodeBuild(&fb, free1)) Fail("a free-standing PLACE with a host form was encoded");
    free1.hostForm = 0; free1.floor = 0; free1.outside = 0;
    if (!coopbuild::EncodeBuild(&fb, free1) || coopbuild::DecodeBuild(&fb[0], fb.size(), &o) != coopbuild::kBuildDecodeOk
        || !o.hostKey.empty() || o.hostForm != 0)
        Fail("a free-standing PLACE did not survive the round trip");
    std::vector<char> ff(fb); ff[ff.size() - 8] = 1;
    if (coopbuild::DecodeBuild(&ff[0], ff.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a free-standing PLACE with host form 1 was accepted");
    coopbuild::BuildMsg lo = m; lo.floor = -coopbuild::kBuildMaxFloor; std::vector<char> lb;
    if (!coopbuild::EncodeBuild(&lb, lo)) Fail("a PLACE with floor -1024 was refused");
    lo.floor = -coopbuild::kBuildMaxFloor - 1; std::vector<char> lb2;
    if (coopbuild::EncodeBuild(&lb2, lo)) Fail("a PLACE with floor -1025 was encoded");
    coopbuild::BuildMsg in2 = m; in2.hostForm = coopbuild::kBuildHostIndoorsOnly; std::vector<char> ib;
    if (!coopbuild::EncodeBuild(&ib, in2) || coopbuild::DecodeBuild(&ib[0], ib.size(), &o) != coopbuild::kBuildDecodeOk || o.hostForm != 2)
        Fail("a PLACE with host form 2 (isIndoorsOf only) did not survive the round trip");
}

/* build1-f fold (review-build1f H2): MSG_BUILD kind 4 ROSTER_REQUEST - the kind byte only (protocol 63). */
void t_build1f_roster_request_wire()
{
    coopbuild::BuildMsg q; q.kind = coopbuild::kBuildRosterRequest;
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, q) || b.size() != 1 || b[0] != 4) { Fail("a ROSTER_REQUEST did not encode to its one kind byte"); return; }
    coopbuild::BuildMsg o; o.key = "x";
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.kind != 4 || !o.key.empty())
        Fail("a ROSTER_REQUEST did not survive the round trip");
    std::vector<char> t(b); t.push_back(0);
    if (coopbuild::DecodeBuild(&t[0], t.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a ROSTER_REQUEST with a trailing byte was accepted");
    coopbuild::BuildMsg k = q; k.key = "1459-gamedata.base@1,2:3,4,5"; std::vector<char> kb;
    if (coopbuild::EncodeBuild(&kb, k)) Fail("a ROSTER_REQUEST with a key was encoded");
    std::vector<char> k5(b); k5[0] = 6;   /* house1b: 5 is HAND_ACK */
    if (coopbuild::DecodeBuild(&k5[0], k5.size(), &o) != coopbuild::kBuildDecodeBadKind) Fail("a BUILD kind 6 was accepted");
}

/* par13 (parity P13): the owed-REMOVE list - a dismantle while the link is down is owed once, sent once the link is up (in order,
   at most cap per tick, a failed send keeps the rest), a key placed again leaves it, and a world load forgets it */
struct Par13Send
{
    int linkUp;
    int failAfter;   /* -1 never; else the send after this many successes fails (the link went down mid-flush) */
    std::vector<std::string> sent;
    Par13Send() : linkUp(0), failAfter(-1) {}
    bool operator()(const std::string& k)
    {
        if (linkUp == 0) return false;
        if (failAfter >= 0 && (int)sent.size() >= failAfter) return false;
        sent.push_back(k);
        return true;
    }
};
/* T-988 (owner decision 1099; fold 1): the piece self-check's rule (coopbuild::PieceCheckVerdict), its kinds, its re-check, its queue
   and per-tick cursor, and the plugin's wiring (no call into PhysXCore64) */
std::string T313ReadFile(const char* path);
void t_t988_piececheck()
{
    using namespace coopbuild;
    /* the verdict: a body is a listed actor; the shapes are not read */
    PieceCheckIn x;
    x.stateOk = 1; x.complete = 1; x.pathModeOk = 1; x.pathMode = 2; x.physOk = 1; x.entities = 1; x.bodies = 1;
    if (PieceCheckVerdict(x) != kPieceCheckHealthy) Fail("T-988: a finished piece with one body is not healthy");
    PieceCheckIn z = x;
    z.bodies = 0;
    if (PieceCheckVerdict(z) != kPieceCheckNoBody) Fail("T-988: a finished piece whose entities list no actor is not noBody");
    z.entities = 0;
    if (PieceCheckVerdict(z) != kPieceCheckNoBody) Fail("T-988: a finished piece with an empty physics list is not noBody");
    z.physOk = 0;
    if (PieceCheckVerdict(z) != kPieceCheckNoBody) Fail("T-988: a finished piece with no physics list is not noBody");
    z.physOk = -1;
    if (PieceCheckVerdict(z) != kPieceCheckFault) Fail("T-988: a faulted physics read was not a fault");
    z.pathModeOk = -1; z.physOk = 0;
    if (PieceCheckVerdict(z) != kPieceCheckNoBody) Fail("T-988: an unread path mode hid a piece with no physics");
    z.pathModeOk = 0;
    if (PieceCheckVerdict(z) != kPieceCheckNoBody) Fail("T-988: a record with no path mode hid a piece with no physics");
    PieceCheckIn ig = z;
    ig.pathModeOk = 1; ig.pathMode = 0;
    if (PieceCheckVerdict(ig) != kPieceCheckHealthy) Fail("T-988: a path mode 0 (IGNORE) piece was suspect");
    ig.physOk = -1;
    if (PieceCheckVerdict(ig) != kPieceCheckHealthy) Fail("T-988: a path mode 0 piece needed its physics read");
    PieceCheckIn u = z;
    u.physOk = 0; u.complete = 0;
    if (PieceCheckVerdict(u) != kPieceCheckSkip) Fail("T-988: an unfinished piece was checked");
    u = z; u.physOk = 0; u.destroyed = 1;
    if (PieceCheckVerdict(u) != kPieceCheckSkip) Fail("T-988: a ruin was checked");
    u = z; u.physOk = 0; u.dismantled = 1;
    if (PieceCheckVerdict(u) != kPieceCheckSkip) Fail("T-988: a dismantled piece was checked");
    u = x; u.stateOk = 0; u.complete = 0;
    if (PieceCheckVerdict(u) != kPieceCheckFault) Fail("T-988: an unreadable build state was not a fault");
    u = x; u.bodies = -1;
    if (PieceCheckVerdict(u) != kPieceCheckFault) Fail("T-988: a negative body count gave a verdict");
    u = x; u.entities = -1;
    if (PieceCheckVerdict(u) != kPieceCheckFault) Fail("T-988: a negative entity count gave a verdict");
    PieceCheckIn d;
    if (PieceCheckVerdict(d) != kPieceCheckFault) Fail("T-988: an all-zero input (nothing read) gave a verdict");
    if (std::strcmp(PieceCheckRoadWord(kPcRoadBuilt), "built") != 0 || std::strcmp(PieceCheckRoadWord(kPcRoadAreaLoad), "areaLoad") != 0
        || std::strcmp(PieceCheckRoadWord(kPcRoadOwnerState), "ownerState") != 0 || std::strcmp(PieceCheckRoadWord(0), "?") != 0
        || std::strcmp(PieceCheckRoadWord(99), "?") != 0) Fail("T-988: a road word");
    for (int r = kPcRoadPlaced; r <= kPcRoadAreaLoad; ++r) if (std::strcmp(PieceCheckRoadWord(r), "?") == 0) Fail("T-988: a road has no word");

    /* fold 1, fix 2: only a wall, a gate or a building is judged; furniture and every other kind is counted apart */
    if (PieceCheckKindOf(".?AVWallBuilding@@", 0) != kPcKindWall || PieceCheckKindOf(".?AVDoorStuff@@", 0) != kPcKindGate
        || PieceCheckKindOf(".?AVGatewayBuilding@@", 0) != kPcKindGate || PieceCheckKindOf(".?AVBuilding@@", 0) != kPcKindBuilding)
        Fail("T-988 fold 1: a wall, gate or building class was not judged as one");
    const char* const others[] = { ".?AVUseableStuff@@", ".?AVStorageBuilding@@", ".?AVCraftingBuilding@@", ".?AVTurretBuilding@@",
                                   ".?AVLightBuilding@@", ".?AVProductionBuilding@@", "", ".?AVWallBuilding" };
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i)
        if (PieceCheckKindOf(others[i], 0) != kPcKindOther) Fail(std::string("T-988 fold 1: class '") + others[i] + "' was judged as a wall / gate / building");
    if (PieceCheckKindOf(0, 0) != kPcKindOther) Fail("T-988 fold 1: an unreadable class was judged");
    if (PieceCheckKindOf(".?AVWallBuilding@@", 1) != kPcKindOther || PieceCheckKindOf(".?AVBuilding@@", 1) != kPcKindOther)
        Fail("T-988 fold 1: layout furniture was judged as a wall / building");
    if (std::strcmp(PieceCheckKindWord(kPcKindWall), "wall") != 0 || std::strcmp(PieceCheckKindWord(kPcKindGate), "gate") != 0
        || std::strcmp(PieceCheckKindWord(kPcKindBuilding), "building") != 0 || std::strcmp(PieceCheckKindWord(kPcKindOther), "other") != 0)
        Fail("T-988 fold 1: a kind word");

    /* fold 2, fix 1: furniture is the +0x238 link OR the interior-object flag OR a registry row with a host - never judged */
    if (PieceCheckFurniture(0, 0, "", 0) != 0) Fail("T-988 fold 2: a free-standing piece was taken for furniture");
    if (PieceCheckFurniture(1, 0, "", 0) != 1) Fail("T-988 fold 2: a piece with the layout-furniture link is not furniture");
    if (PieceCheckFurniture(0, 1, "", 0) != 1) Fail("T-988 fold 2: an interior object (no +0x238 link) is not furniture");
    if (PieceCheckFurniture(0, 0, "host-key", 0) != 1) Fail("T-988 fold 2: a piece whose row has a host key is not furniture");
    if (PieceCheckFurniture(0, 0, "", 2) != 1) Fail("T-988 fold 2: a piece whose row has a host form is not furniture");
    if (PieceCheckKindOf(".?AVBuilding@@", PieceCheckFurniture(0, 1, "", 0)) != kPcKindOther
        || PieceCheckKindOf(".?AVDoorStuff@@", PieceCheckFurniture(0, 1, "", 0)) != kPcKindOther
        || PieceCheckKindOf(".?AVDoorStuff@@", PieceCheckFurniture(0, 0, "host-key", 0)) != kPcKindOther
        || PieceCheckKindOf(".?AVBuilding@@", PieceCheckFurniture(0, 0, "", 1)) != kPcKindOther)
        Fail("T-988 fold 2: indoor furniture with no +0x238 link (a Building / DoorStuff) was judged as a building / gate");
    if (PieceCheckKindOf(".?AVBuilding@@", PieceCheckFurniture(0, 0, "", 0)) != kPcKindBuilding
        || PieceCheckKindOf(".?AVDoorStuff@@", PieceCheckFurniture(0, 0, "", 0)) != kPcKindGate)
        Fail("T-988 fold 2: a free-standing building / gate was no longer judged");

    /* fold 2, fix 2: the path-mode walk over the record's fields stops at its limit and gives no verdict (a fault) */
    {
        std::map<std::string, int> f;
        f["level"] = 3; f["path mode"] = 2; f["zz"] = 1;
        int pmv = -9;
        if (PieceCheckFindPathMode(f.begin(), f.end(), kPcFieldWalkMax, &pmv) != 1 || pmv != 2) Fail("T-988 fold 2: the path mode was not found");
        f.erase("path mode");
        if (PieceCheckFindPathMode(f.begin(), f.end(), kPcFieldWalkMax, &pmv) != 0) Fail("T-988 fold 2: a record with no path mode was not 0");
        std::map<std::string, int> big;
        for (size_t i = 0; i < kPcFieldWalkMax + 10; ++i) { char k[24]; std::sprintf(k, "f%05u", (unsigned)i); big[k] = (int)i; }
        if (PieceCheckFindPathMode(big.begin(), big.end(), kPcFieldWalkMax, &pmv) != kPcPathModeOverLimit)
            Fail("T-988 fold 2: a record with more fields than the limit was walked to its end");
        std::map<std::string, int> exact;
        for (size_t i = 0; i < kPcFieldWalkMax; ++i) { char k[24]; std::sprintf(k, "f%05u", (unsigned)i); exact[k] = (int)i; }
        if (PieceCheckFindPathMode(exact.begin(), exact.end(), kPcFieldWalkMax, &pmv) != 0) Fail("T-988 fold 2: a record of exactly the limit was cut");
        /* a node chain that loops back (never reaches its end): the walk stops at the limit */
        struct Ring
        {
            const std::vector<std::pair<std::string, int> >* v;
            size_t i;
            const std::pair<std::string, int>* operator->() const { return &(*v)[i]; }
            Ring& operator++() { i = (i + 1) % v->size(); return *this; }
            bool operator!=(const Ring& o) const { return i != o.i; }
        };
        std::vector<std::pair<std::string, int> > ring;
        ring.push_back(std::make_pair(std::string("a"), 1));
        ring.push_back(std::make_pair(std::string("b"), 2));
        Ring rb, re;
        rb.v = &ring; rb.i = 0; re.v = &ring; re.i = ring.size();   /* the wrap never reaches index 2 */
        if (PieceCheckFindPathMode(rb, re, kPcFieldWalkMax, &pmv) != kPcPathModeOverLimit) Fail("T-988 fold 2: a looping field chain was not stopped");
        if (kPcFieldWalkMax < 1024) Fail("T-988 fold 2: the field-walk limit is near a real record's field count");
        PieceCheckIn ol = x;
        ol.pathModeOk = kPcPathModeOverLimit; ol.pathMode = 0;
        if (PieceCheckVerdict(ol) != kPieceCheckFault) Fail("T-988 fold 2: a record walked to the limit gave a verdict (not counted in faults)");
        ol.physOk = 0;
        if (PieceCheckVerdict(ol) != kPieceCheckFault) Fail("T-988 fold 2: a record walked to the limit was judged (no body)");
        ol.complete = 0;
        if (PieceCheckVerdict(ol) != kPieceCheckSkip) Fail("T-988 fold 2: an unfinished piece was counted as a fault");
    }

    /* fold 1, fix 3: no body is checked again before it logs; only the last re-check logs SUSPECT */
    if (PieceCheckNext(kPieceCheckNoBody, 0, 2) != kPcNextRecheck || PieceCheckNext(kPieceCheckNoBody, 1, 2) != kPcNextRecheck)
        Fail("T-988 fold 1: a piece with no body logged before its re-checks");
    if (PieceCheckNext(kPieceCheckNoBody, 2, 2) != kPcNextSuspect || PieceCheckNext(kPieceCheckNoBody, 5, 2) != kPcNextSuspect)
        Fail("T-988 fold 1: a piece still with no body at its last re-check did not log");
    if (PieceCheckNext(kPieceCheckHealthy, 0, 2) != kPcNextDone || PieceCheckNext(kPieceCheckHealthy, 2, 2) != kPcNextDone
        || PieceCheckNext(kPieceCheckFault, 0, 2) != kPcNextDone || PieceCheckNext(kPieceCheckSkip, 1, 2) != kPcNextDone)
        Fail("T-988 fold 1: a piece with a body (or no verdict) was re-checked or logged");
    {   /* the re-check walk: no body at the first check and the first re-check, a body at the second - never logged */
        int tries = 0, logged = 0, rechecks = 0;
        const int verdicts[3] = { kPieceCheckNoBody, kPieceCheckNoBody, kPieceCheckHealthy };
        for (int s = 0; s < 3; ++s)
        {
            const int nx = PieceCheckNext(verdicts[s], tries, 2);
            if (nx == kPcNextRecheck) { ++rechecks; ++tries; }
            else { if (nx == kPcNextSuspect) logged = 1; break; }
        }
        if (logged != 0 || rechecks != 2) Fail("T-988 fold 1: a body built late was logged, or not re-checked twice");
    }

    /* fold 1, fixes 3 + 4: the bounded queue and the per-tick cursor */
    {
        PieceCheckQueue q(20);
        for (int i = 0; i < 20; ++i)
        {
            char k[16];
            std::sprintf(k, "k%d", i);
            if (q.Push(k, 100, kPcRoadAreaLoad, 0) != 1) Fail("T-988 fold 1: a key was not queued");
        }
        if (q.Push("k3", 50, kPcRoadBuilt, 0) != 0 || q.q.size() != 20) Fail("T-988 fold 1: a waiting key was queued twice");
        if (q.Push("more", 100, kPcRoadBuilt, 0) != -1 || q.q.size() != 20 || q.Waiting("more") != 0) Fail("T-988 fold 1: the queue grew past its cap");
        if (q.Push("", 100, kPcRoadBuilt, 0) != 0) Fail("T-988 fold 1: an empty key was queued");
        std::vector<PieceCheckDue> out;
        if (q.TakeDue(99, 8, &out) != 0 || q.q.size() != 20) Fail("T-988 fold 1: a check was taken before its time");
        if (q.TakeDue(100, 8, &out) != 8 || q.q.size() != 12 || out[0].key != "k0" || out[7].key != "k7")
            Fail("T-988 fold 1: the cursor did not take 8, the oldest first");
        if (q.Waiting("k0") != 0 || q.Waiting("k8") != 1) Fail("T-988 fold 1: a taken key still waits, or a left one does not");
        if (q.Push("k0", 90100, kPcRoadAreaLoad, 1) != 1) Fail("T-988 fold 1: a taken key could not be queued for its re-check");
        if (q.TakeDue(200, 8, &out) != 8 || q.TakeDue(200, 8, &out) != 4 || q.q.size() != 1) Fail("T-988 fold 1: the cursor did not drain 8, 8, 4");
        if (q.TakeDue(200, 8, &out) != 0) Fail("T-988 fold 1: a re-check was taken before its delay");
        if (q.TakeDue(90100, 8, &out) != 1 || out[0].tries != 1 || out[0].road != kPcRoadAreaLoad) Fail("T-988 fold 1: the re-check lost its count or road");
        q.Push("w", 0xFFFFFFF0u, kPcRoadBuilt, 0);
        if (q.TakeDue(0xFFFFFFE0u, 8, &out) != 0 || q.TakeDue(0x10u, 8, &out) != 1) Fail("T-988 fold 1: the wrapping clock");
        q.Push("c", 0, kPcRoadBuilt, 0);
        q.Clear();
        if (!q.q.empty() || q.Waiting("c") != 0) Fail("T-988 fold 1: Clear left a key");
    }

    /* fold 1, the plugin's wiring: no call into PhysXCore64, the kinds, the re-check and the cursor */
    const std::string src = T313ReadFile("../coop-plugin/build.cpp");
    if (src.empty()) { Fail("T-988 fold 1: could not read the plugin's build.cpp"); return; }
    const size_t b0 = src.find("THE PIECE SELF-CHECK - READ ONLY");
    const size_t b1 = (b0 != std::string::npos) ? src.find("std::string PcReportText()", b0) : std::string::npos;
    if (b0 == std::string::npos || b1 == std::string::npos) { Fail("T-988 fold 1: the self-check block was not found"); return; }
    const std::string blk = src.substr(b0, b1 - b0);
    const char* const banned[] = { "GetModuleHandle", "PhysXCore64.dll", "Fn)(", "kPcVtNbShapes", "kPcVtShapes", "kPcVtGroup", "PcSlot(" };
    for (size_t i = 0; i < sizeof(banned) / sizeof(banned[0]); ++i)
        if (blk.find(banned[i]) != std::string::npos) Fail(std::string("T-988 fold 1: the self-check still calls into the physics library (") + banned[i] + ")");
    if (blk.find("if (kind == coopbuild::kPcKindOther) { if (tries == 0) ++g_pcOther; return; }") == std::string::npos
        || blk.find("const int kind = PcKindOf(b, r);") == std::string::npos
        || blk.find("return coopbuild::PieceCheckKindOf(cls, furn);") == std::string::npos)
        Fail("T-988 fold 1: another kind is not counted apart before the SUSPECT line");
    /* fold 2, fix 1: the plugin reads the interior-object flag and the row's host, as its other furniture tests read them */
    if (blk.find("coopbuild::PieceCheckFurniture(BdPtrAt(b, kBFurnitureOf) != 0 ? 1 : 0, BdByteAtOr0(b, kBInteriorObj) != 0 ? 1 : 0,") == std::string::npos
        || blk.find("r.hostKey, r.hostForm);") == std::string::npos)
        Fail("T-988 fold 2: the self-check does not read the interior-object flag and the registry row's host for furniture");
    if (src.find("(BdPtrAt(b, kBFurnitureOf) != 0 || BdByteAtOr0(b, kBInteriorObj) != 0)") == std::string::npos
        || src.find("ti.furn = (!r.hostKey.empty() || r.hostForm != 0) ? 1 : 0;") == std::string::npos)
        Fail("T-988 fold 2: the furniture tests the self-check mirrors (detour_setfaction, BdTombNote) changed - re-mirror them");
    /* fold 2, fix 2: the path-mode walk is bounded (no open walk over the record's fields left in the block) */
    if (blk.find("return coopbuild::PieceCheckFindPathMode(m.begin(), m.end(), coopbuild::kPcFieldWalkMax, v);") == std::string::npos
        || blk.find("it != m.end(); ++it)") != std::string::npos)
        Fail("T-988 fold 2: the path-mode walk over the record's fields has no count limit");
    if (blk.find("coopbuild::PieceCheckNext(v, tries, kPcRechecks)") == std::string::npos
        || blk.find("g_pcDue.Push(key, (unsigned int)(::GetTickCount() + kPcRecheckMs), road, tries + 1)") == std::string::npos
        || blk.find("const DWORD kPcRecheckMs = 90000;") == std::string::npos || blk.find("const int kPcRechecks = 2;") == std::string::npos)
        Fail("T-988 fold 1: a piece with no body is not re-checked later");
    if (blk.find("const int kPcPiecesPerTick = 8;") == std::string::npos
        || blk.find("g_pcDue.TakeDue((unsigned int)now, kPcPiecesPerTick, &due);") == std::string::npos
        || blk.find("g_pcDue.Push(it->first, (unsigned int)now, coopbuild::kPcRoadAreaLoad, 0)") == std::string::npos)
        Fail("T-988 fold 1: the per-tick cursor is not wired (an area is still checked in one tick)");
    const size_t rp = src.find("pieceCheck[checked,suspect,noShapes,excludedOnly,notCarved,faults,capped,other,rechecks,lateBody,dropped]", b1);
    if (rp == std::string::npos || rp > b1 + 400) Fail("T-988 fold 1: the REPORT fields are not the old seven then the four new");
}
/* T-274: the per-zone scan's mark / clear / retry rule (coopbuild::BuildZoneScan*); T-274 fold: round-robin pick and the give-up */
void t_t274_zonescan()
{
    using namespace coopbuild;
    static unsigned char marks[kBuildZoneSectorCells], moves[kBuildZoneSectorCells], scratch[kBuildZoneSectorCells];
    for (int c = 0; c < kBuildZoneSectorCells; ++c) { marks[c] = 0; moves[c] = 0; }
    if (BuildZoneSectorIndex(0, 0) != 0 || BuildZoneSectorIndex(63, 63) != 4095 || BuildZoneSectorIndex(5, 6) != 5 * 64 + 6) Fail("T-274: sector index");
    if (BuildZoneSectorIndex(64, 0) != -1 || BuildZoneSectorIndex(0, 64) != -1 || BuildZoneSectorIndex(-1, 3) != -1 || BuildZoneSectorIndex(3, -1) != -1) Fail("T-274: an off-map sector has an index");
    int sx[4] = { 3, 5, 7, 70 }, sy[4] = { 4, 6, 8, 1 };
    int ld[4] = { 0, 1, 1, 1 };
    const int c34 = BuildZoneSectorIndex(3, 4), c56 = BuildZoneSectorIndex(5, 6), c78 = BuildZoneSectorIndex(7, 8);
    if (BuildZoneScanClearGone(marks, moves, scratch, sx, sy, 4) != 0) Fail("T-274: an empty table cleared something");
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, -1) != 1) Fail("T-274: the first loaded unmarked zone was not picked (a still-loading one was)");
    if (BuildZoneScanDone(marks, moves, 5, 6, 0) != 0 || marks[c56] != 1) Fail("T-274: a walked zone was not marked / was counted a give-up");
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, c56) != 2) Fail("T-274: ONE zone per tick - the zone after the last picked was not picked next");
    ld[0] = 1;   /* zone 3.4 finished loading */
    if (BuildZoneScanDone(marks, moves, 7, 8, 1) != 0 || marks[c78] != 0 || moves[c78] != 1) Fail("T-274: a walk that moved was marked / not counted");
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, c78) != 0) Fail("T-274 fold: round-robin - a moved zone held back the zone after it (wrapping past the off-map entry)");
    BuildZoneScanDone(marks, moves, 3, 4, 0);
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, c34) != 2) Fail("T-274 fold: round-robin - the moved zone was not walked again on its turn");
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, BuildZoneSectorIndex(9, 9)) != 2) Fail("T-274 fold: a last pick no longer active did not start from the front");
    for (int k = 2; k < kBuildZoneScanGiveUp; ++k)
        if (BuildZoneScanDone(marks, moves, 7, 8, 1) != 0 || marks[c78] != 0) Fail("T-274 fold: a zone was given up before kBuildZoneScanGiveUp moved walks in a row");
    if (BuildZoneScanDone(marks, moves, 7, 8, 1) != 1 || marks[c78] != 1 || moves[c78] != 0) Fail("T-274 fold: the kBuildZoneScanGiveUp-th moved walk in a row did not give up (mark)");
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, c78) != -1) Fail("T-274: an off-map sector, a still-loading zone or a marked (or given-up) zone was picked");
    marks[c78] = 0;
    BuildZoneScanDone(marks, moves, 7, 8, 1);
    BuildZoneScanDone(marks, moves, 7, 8, 1);
    if (BuildZoneScanDone(marks, moves, 7, 8, 0) != 0 || marks[c78] != 1 || moves[c78] != 0) Fail("T-274 fold: a whole walk did not reset the moved-walk count");
    moves[c56] = 3;
    int sx2[2] = { 3, 7 }, sy2[2] = { 4, 8 };
    if (BuildZoneScanClearGone(marks, moves, scratch, sx2, sy2, 2) != 1 || marks[c56] != 0 || moves[c56] != 0) Fail("T-274: a zone that left the active list kept its mark / its moved-walk count");
    if (marks[c34] != 1 || marks[c78] != 1) Fail("T-274: clearing touched a zone still active");
    ld[1] = -1;
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, -1) != -1) Fail("T-274: a refused loaded read was picked");
    ld[1] = 0;
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, -1) != -1) Fail("T-274: a still-loading zone was picked");
    ld[1] = 1;
    if (BuildZoneScanPick(marks, sx, sy, ld, 4, c78) != 1) Fail("T-274: a re-arrived zone was not walked again");
    if (BuildZoneScanDone(marks, moves, 70, 1, 0) != 0 || BuildZoneScanDone(marks, moves, 70, 1, 1) != 0) Fail("T-274: an off-map sector was marked / given up");
    if (BuildZoneScanPick(marks, sx, sy, ld, 0, c78) != -1) Fail("T-274: an empty active list picked something");
    if (BuildZoneScanClearGone(marks, moves, scratch, sx, sy, 0) != 2) Fail("T-274: an empty active list did not clear every mark");
}
/* world resource nodes are nobody's pieces (the scan, the restore, the copy list, a PLACE); a node's bar travels from its
   writer as MINE_STATE (kind 11); the [MINE] lines' rate */
void t_t720_world_node_mine_state()
{
    using namespace coopown;
    if (WorldNodeOf(1, 1, 0, 0, 1) != 1) Fail("a foliage-marked worker-operated production building with no inputs and no record was not a world node");
    if (WorldNodeOf(1, 1, 0, 0, 0) != 0) Fail("an operated no-input building not marked foliage (a player's Ore Mine, Stone Mine or Well) was taken for a world node");
    if (WorldNodeOf(1, 1, 1, 0, 1) != 0 || WorldNodeOf(1, 1, 4, 0, 1) != 0) Fail("a production building with inputs was taken for a world node");
    if (WorldNodeOf(1, 0, 0, 0, 1) != 0) Fail("a production building no worker operates was taken for a world node");
    if (WorldNodeOf(1, 1, 0, 1, 1) != 0) Fail("a production building with a build record was taken for a world node");
    if (WorldNodeOf(0, 1, 0, 0, 1) != 0) Fail("a building that is not a production one was taken for a world node");
    if (WorldNodeOf(-1, 1, 0, 0, 1) != 0 || WorldNodeOf(1, -1, 0, 0, 1) != 0 || WorldNodeOf(1, 1, -1, 0, 1) != 0 || WorldNodeOf(1, 1, 0, -1, 1) != 0
        || WorldNodeOf(1, 1, 0, 0, -1) != 0)
        Fail("an unreadable value made a world node");
    if (WorldNodeOf(2, 1, 0, 0, 1) != 0 || WorldNodeOf(1, 2, 0, 0, 1) != 0 || WorldNodeOf(1, 1, 0, 2, 1) != 0 || WorldNodeOf(1, 1, 0, 0, 2) != 0)
        Fail("an out-of-range value made a world node");

    /* the restore: a world node standing mine - its saved row is dropped, never a tombstone or a REMOVE */
    const int act = coopbuild::BuildRestorePresentAct(1, 1, 1, 0, 1);
    if (act != coopbuild::kBuildRsPresentDropWorld || coopbuild::BuildRestorePresentAct(1, 0, 0, 0, 1) != coopbuild::kBuildRsPresentDropWorld)
        Fail("a saved row of a world node standing mine was not dropped");
    if (coopbuild::BuildRestorePresentAct(0, 1, 1, 0, 1) != coopbuild::kBuildRsPresentNothing) Fail("another owner's world node was acted on");
    if (coopbuild::BuildRestorePresentAct(1, 0, 0, 0, 0) != coopbuild::kBuildRsPresentRegister
        || coopbuild::BuildRestorePresentAct(1, 1, 1, 0) != coopbuild::kBuildRsPresentNonce)
        Fail("a piece that is not a world node lost its present act");
    const coopbuild::BuildRsPresentPlan pl = coopbuild::BuildRsPresentPlanOf(act);
    if (pl.retireRow != 1 || pl.tombstone != 0 || pl.sendRemove != 0 || pl.registerLive != 0 || pl.takeNonce != 0)
        Fail("dropping a world node's saved row wrote a tombstone, sent a REMOVE, or registered or kept it");
    for (int a = coopbuild::kBuildRsPresentNothing; a <= coopbuild::kBuildRsPresentDropWorld; ++a)
    {
        const coopbuild::BuildRsPresentPlan q = coopbuild::BuildRsPresentPlanOf(a);
        if (q.tombstone != 0 || q.sendRemove != 0) Fail("a present act wrote a tombstone or sent a REMOVE");
    }

    /* the wire: kind 11 round trip, refusals, the building road refuses it */
    coopfarm::FarmMsg m; m.kind = coopfarm::kMineState; m.key = "42248-gamedata.base@43,11@545954,-924381,9819";
    m.prodState = 2; m.progress = 0.625f;
    std::vector<char> b;
    if (!coopfarm::EncodeFarm(&b, m)) { Fail("a MINE_STATE did not encode"); return; }
    if (b.size() != 2 + m.key.size() + 8 || (unsigned char)b[0] != 11) Fail("MINE_STATE layout: kind | len + key | state | progress");
    if (coopfarm::IsFarmKind(&b[0], b.size()) != 1) Fail("kind 11 not recognised by the farm peek");
    coopfarm::FarmMsg o;
    if (coopfarm::DecodeFarm(&b[0], b.size(), &o) != coopfarm::kFarmDecodeOk || o.kind != coopfarm::kMineState || o.key != m.key
        || o.prodState != 2 || o.progress != 0.625f)
        Fail("a MINE_STATE did not round-trip (key, state, progress)");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopfarm::DecodeFarm(&b[0], cut, &o) == coopfarm::kFarmDecodeOk) { Fail("a truncated MINE_STATE was accepted"); break; }
    std::vector<char> t(b); t.push_back(0);
    if (coopfarm::DecodeFarm(&t[0], t.size(), &o) == coopfarm::kFarmDecodeOk) Fail("a MINE_STATE with a trailing byte was accepted");
    coopbuild::BuildMsg bm;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &bm) == coopbuild::kBuildDecodeOk) Fail("DecodeBuild accepted a MINE_STATE");
    std::vector<char> far(b);
    const float big = 5000.0f;
    std::memcpy(&far[far.size() - 4], &big, 4);
    if (coopfarm::DecodeFarm(&far[0], far.size(), &o) == coopfarm::kFarmDecodeOk) Fail("a MINE_STATE with an out-of-range progress was accepted");
    coopfarm::FarmMsg bad(m); bad.progress = -0.5f;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with a negative progress was encodable");
    bad = m; bad.progress = 0.0f / std::atof("0");
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with a NaN progress was encodable");
    bad = m; bad.prodState = -1;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with a negative state was encodable");
    bad = m; bad.prodState = 3;
    if (coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with a state the game never writes (3) was encodable");
    bad = m; bad.prodState = 0;
    if (!coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with state 0 was not encodable");
    bad = m; bad.prodState = 1;
    if (!coopfarm::FarmEncodable(bad)) Fail("a MINE_STATE with state 1 was not encodable");

    /* the writer's send rate */
    using namespace coopfarm;
    if (MineStatePublishDue(0, 0, 0, 0.0f, 0, 0.1f, 2, 0) != kMineStateSend) Fail("MINE_STATE: a node just worked was not sent at once");
    if (MineStatePublishDue(100, 1, 500, 0.0f, 2, 0.5f, 2, 0) != kMineStateNone) Fail("MINE_STATE: sent twice within a second");
    if (MineStatePublishDue(100, 1, 1000, 0.0f, 2, 0.04f, 2, 0) != kMineStateNone) Fail("MINE_STATE: sent for a move under kFarmBarStep");
    if (MineStatePublishDue(100, 1, 1000, 0.0f, 2, 0.05f, 2, 0) != kMineStateSend) Fail("MINE_STATE: a move of kFarmBarStep was not sent");
    if (MineStatePublishDue(100, 1, 1000, 0.9f, 2, 0.0f, 2, 0) != kMineStateSend) Fail("MINE_STATE: the bar going back at an item was not sent");
    if (MineStatePublishDue(100, 1, 1000, 0.3f, 2, 0.3f, 1, 0) != kMineStateSend) Fail("MINE_STATE: a state change was not sent");
    if (MineStatePublishDue(kMineStateWorkedMs, 1, 1000, 0.3f, 2, 0.3f, 2, 0) != kMineStateNone) Fail("MINE_STATE: the work stopped too early");
    if (MineStatePublishDue(kMineStateWorkedMs + 1, 1, 1000, 0.3f, 2, 0.3f, 2, 0) != kMineStateFinal) Fail("MINE_STATE: no last send when the work stopped");
    if (MineStatePublishDue(kMineStateWorkedMs + 1, 1, 1000, 0.3f, 2, 0.3f, 2, 1) != kMineStateNone) Fail("MINE_STATE: a second last send");
    if (MineStatePublishDue(kMineStateWorkedMs + 1, 1, 500, 0.3f, 2, 0.3f, 2, 0) != kMineStateNone) Fail("MINE_STATE: the last send broke the one-second gap");
    if (MineStatePublishDue(kMineStateWorkedMs + 1, 0, 0, 0.0f, 0, 0.3f, 2, 0) != kMineStateNone) Fail("MINE_STATE: a node never sent got a last send");

    /* the receiver */
    if (MineStateAccept(kProdWriterOther) != 1) Fail("MINE_STATE: a game another game writes the node for did not apply it");
    if (MineStateAccept(kProdWriterHere) != 0) Fail("MINE_STATE: the writer applied another game's bar");
    if (MineStateAccept(kProdWriterNone) != 0) Fail("MINE_STATE: applied with no writer named");

    /* the [MINE] lines: the first 5, then every 100th */
    if (MineSayDue(0) != 0 || MineSayDue(1) != 1 || MineSayDue(5) != 1 || MineSayDue(6) != 0 || MineSayDue(99) != 0
        || MineSayDue(100) != 1 || MineSayDue(101) != 0 || MineSayDue(200) != 1)
        Fail("MineSayDue: not the first 5 then every 100th");
}
/* help1 (owner 172): HELP_WORK (kind 8) and the STATE's confirmation tail - round trips, truncation, refusals */
/* help1 fold 4: the owner's restart (nonce restored, confirmations kept, work held until then), the GONE verdict (no applied 0 without
   proof; the cap), the copy-row collision, a replaced row with a refund due, a refused GONE refund carried */
void t_help1_fold5()
{
    using namespace coopbuild;
    {   /* T-268: the restore finds its piece standing and mine */
        if (BuildRestorePresentAct(1, 0, 0, 0) != kBuildRsPresentRegister) Fail("help1 fold 5: a standing own piece with no row was not registered");
        if (BuildRestorePresentAct(1, 1, 1, 0) != kBuildRsPresentNonce) Fail("help1 fold 5: an own row did not take its PLACE's nonce");
        if (BuildRestorePresentAct(1, 1, 1, 1) != kBuildRsPresentNothing || BuildRestorePresentAct(1, 1, 0, 1) != kBuildRsPresentNothing) Fail("help1 fold 5: a copy row was touched");
        if (BuildRestorePresentAct(1, 1, 0, 0) != kBuildRsPresentNothing) Fail("help1 fold 5: a not-own row was touched");
        if (BuildRestorePresentAct(0, 0, 0, 0) != kBuildRsPresentNothing || BuildRestorePresentAct(0, 1, 1, 0) != kBuildRsPresentNothing) Fail("help1 fold 5: another owner's piece was registered / touched");
    }
    {   /* T-269: the owner's first real STATE after its PLACE */
        if (BuildOwnStateAnnounce(1, 0, 1, 1) != 1) Fail("help1 fold 5: an own piece's PLACE went out and its STATE was not announced");
        if (BuildOwnStateAnnounce(1, 0, 1, 0) != 0) Fail("help1 fold 5: a STATE was announced twice");
        if (BuildOwnStateAnnounce(1, 0, 0, 1) != 0) Fail("help1 fold 5: a STATE was announced before its PLACE went out");
        if (BuildOwnStateAnnounce(1, 1, 1, 1) != 0 || BuildOwnStateAnnounce(0, 0, 1, 1) != 0) Fail("help1 fold 5: a copy / not-own row announced a STATE");
    }
}
void t_help1_fold4()
{
    using namespace coopbuild;
    {   /* the restart: the load scan's own row has nonce 0; pp.build's PLACE record for the key carries N */
        const unsigned int N = 0x1234abcdu;
        if (BuildHelpNonceWait(1, 1, 0, 0u, N) != 1) Fail("help1 fold 4: work for an own row whose nonce is not known was not held");
        if (BuildAckRestoreFor(0u, N, 0) != 0) Fail("help1 fold 4: restored confirmation rows were read for a row whose nonce is not known");
        if (BuildAckRestoreFor(0u, N, 1) != 1) Fail("help1 fold 4: restored confirmation rows were left out of the pp.build write before the nonce came back");
        const unsigned int got = BuildOwnNonceRestore(0u, 1, 0, 1, N);
        if (got != N) Fail("help1 fold 4: the own row did not take its pp.build PLACE nonce");
        if (BuildAckRestoreFor(got, N, 0) != 1) Fail("help1 fold 4: the restored confirmation rows were not read once the nonce came back");
        if (BuildHelpNonceWait(1, 1, 0, got, N) != 0) Fail("help1 fold 4: work stayed held after the nonce came back");
        if (BuildHelpRowVerdict(got == N ? 1 : 0, 1, 0, 0, 0, 1) != kBuildHelpRowOk) Fail("help1 fold 4: the restored placement's work was not applied");
        if (BuildAckRestoreFor(7u, N, 1) != 0 || BuildAckRestoreFor(7u, N, 0) != 0) Fail("help1 fold 4: another placement's restored rows were used");
        if (BuildOwnNonceRestore(0u, 1, 0, 0, 0u) != 0u) Fail("help1 fold 4: a nonce was invented with no PLACE record");
        if (BuildOwnNonceRestore(7u, 1, 0, 1, N) != 7u) Fail("help1 fold 4: a row that has a nonce took the record's");
        if (BuildOwnNonceRestore(0u, 1, 1, 1, N) != 0u || BuildOwnNonceRestore(0u, 0, 0, 1, N) != 0u) Fail("help1 fold 4: a copy / not-own row took an own record's nonce");
        if (BuildHelpNonceWait(1, 1, 0, 0u, 0u) != 0) Fail("help1 fold 4: work naming no placement was held");
        if (BuildHelpNonceWait(1, 1, 1, 0u, N) != 0 || BuildHelpNonceWait(0, 1, 0, 0u, N) != 0) Fail("help1 fold 4: a copy / missing row held work");
    }
    {   /* the GONE verdict: never applied 0 without proof; at the cap only from settled rows */
        if (BuildHelpGoneVerdict(1, 0, 0, 0, 0) != kBuildGoneAnswer || BuildHelpGoneVerdict(1, 1, 1, 0, 1) != kBuildGoneAnswer) Fail("help1 fold 4: a landed record did not answer");
        if (BuildHelpGoneVerdict(2, 0, 0, 0, 0) != kBuildGoneWait || BuildHelpGoneVerdict(2, 1, 1, 1, 0) != kBuildGoneWait) Fail("help1 fold 4: a record not on disk answered");
        if (BuildHelpGoneVerdict(0, 0, 0, 0, 0) != kBuildGoneDrop) Fail("help1 fold 4: a known key with no record for the nonce was answered (GONE applied 0 without proof)");
        if (BuildHelpGoneVerdict(0, 1, 0, 0, 0) != kBuildGoneZero) Fail("help1 fold 4: the placement's own row with no helper row was not answered applied 0");
        if (BuildHelpGoneVerdict(0, 1, 1, 1, 0) != kBuildGoneAnswer) Fail("help1 fold 4: the cap - a settled row did not answer from its landed rows");
        if (BuildHelpGoneVerdict(0, 1, 1, 0, 1) != kBuildGoneWait) Fail("help1 fold 4: the cap - a row whose write is on its way answered from landed alone");
        if (BuildHelpGoneVerdict(0, 1, 1, 0, 0) != kBuildGoneDrop) Fail("help1 fold 4: the cap - a row with applied work never on disk answered from landed alone");
    }
    {   /* re-check #5: a piece over a COPY row stays that copy with its nonce */
        if (BuildNewPieceNonce(1, 0x55u, 0x99u) != 0x55u) Fail("help1 fold 4: a piece over a copy row took a fresh nonce");
        if (BuildNewPieceNonce(1, 0u, 0x99u) != 0u || BuildAdoptNonce(0u, 2, 0x55u) != 0x55u) Fail("help1 fold 4: a nonce-less copy could no longer learn the owner's");
        if (BuildNewPieceNonce(0, 0x55u, 0x99u) != 0x99u) Fail("help1 fold 4: a new own placement kept the earlier nonce");
    }
    {   /* leftover 3: a replaced row with no entries but a refund due is kept */
        if (BuildHelpOldKeep(0, 1) != 1 || BuildHelpOldKeep(3, 0) != 1 || BuildHelpOldKeep(0, 0) != 0) Fail("help1 fold 4: replaced-row keep rule");
    }
    {   /* leftover 6: a GONE refund the inventory refused rides one more entry, handed back at the next GONE */
        int whole[kBuildMaxMats], given[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) { whole[i] = 0; given[i] = 1; }
        whole[0] = 3; given[0] = 1; whole[1] = 2; given[1] = 0; whole[2] = 4; given[2] = -1;
        BuildHelpEntry e;
        if (BuildHelpGoneCarry(whole, given, 9u, 0xabu, &e) != 1 || e.seq != 9u || e.nonce != 0xabu || e.n != 2 || e.d[0] != 0.0f || e.d[1] != 2.0f || e.d[2] != 0.0f)
            Fail("help1 fold 4: the refused refund was not carried (only what the inventory refused; a faulted add never retried)");
        std::vector<BuildHelpEntry> o(1, e);
        float back[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) back[i] = 0.0f;
        int n = 0;
        if (BuildHelpGoneSum(o, 9u, 4u, back, &n) != 1 || back[1] != 2.0f) Fail("help1 fold 4: the carried refund was not handed back at the next GONE");
        whole[1] = 0;
        if (BuildHelpGoneCarry(whole, given, 10u, 0xabu, &e) != 0) Fail("help1 fold 4: an entry was carried with nothing refused");
    }
}
/* help1 fold 3: live / landed resync (finding 1), the pp.help floor rule (finding 6), GONE's refused tail (finding 4), the wire */
void t_help1_fold3()
{
    using namespace coopbuild;
    {   /* E: entries in (landed, live] were applied - kept, never renumbered; only a gap above live renumbers */
        std::vector<BuildHelpEntry> o;
        for (unsigned int q = 5; q <= 7; ++q) { BuildHelpEntry e; e.seq = q; e.dP = (float)q; o.push_back(e); }
        unsigned int nx = 8;
        if (BuildHelpResync(&o, &nx, 2, 4) != 0 || o.size() != 3 || o[0].seq != 5 || o[2].seq != 7 || nx != 8)
            Fail("help1 fold 3: entries above the live last applied were renumbered from the lagging landed one");
        std::vector<BuildHelpEntry> o2;
        const unsigned int qs[4] = { 3, 4, 8, 9 };
        for (int i = 0; i < 4; ++i) { BuildHelpEntry e; e.seq = qs[i]; e.dP = (float)qs[i]; o2.push_back(e); }
        nx = 10;
        if (BuildHelpResync(&o2, &nx, 2, 4) != 2 || o2.size() != 4 || o2[0].seq != 3 || o2[1].seq != 4 || o2[2].seq != 5 || o2[3].seq != 6
            || o2[2].dP != 8.0f || o2[3].dP != 9.0f || nx != 7)
            Fail("help1 fold 3: a gap above live was not renumbered from live + 1 (the applied ones kept)");
        if (o2[0].applied != 1 || o2[1].applied != 1 || o2[2].applied != 0 || o2[3].applied != 0) Fail("help1 fold 3: the live-applied entries were not marked (never re-sent)");
        unsigned int last = 4;
        for (size_t i = 2; i < o2.size(); ++i) { if (BuildHelpSeqVerdict(last, o2[i].seq) != kBuildHelpApply) Fail("help1 fold 3: a renumbered entry was not the owner's next"); last = o2[i].seq; }
        nx = 10;
        if (BuildHelpResync(&o2, &nx, 4, 4) != 0 || o2.size() != 2 || o2[0].seq != 5 || nx != 10) Fail("help1 fold 3: the landed confirmation did not drop the applied entries");
        std::vector<BuildHelpEntry> o3;   /* the owner lost its unlanded work (a restart): live falls back - the entries go again */
        for (unsigned int q = 3; q <= 4; ++q) { BuildHelpEntry e; e.seq = q; e.applied = 1; o3.push_back(e); }
        nx = 5;
        if (BuildHelpResync(&o3, &nx, 2, 2) != 0 || o3[0].applied != 0 || o3[1].applied != 0 || nx != 5) Fail("help1 fold 3: entries above a fallen-back live stayed marked applied");
        std::vector<BuildHelpEntry> none;
        nx = 12;
        if (BuildHelpResync(&none, &nx, 2, 4) != 0 || nx != 12) Fail("help1 fold 3: next was lowered with nothing owed");
        nx = 3;
        if (BuildHelpResync(&none, &nx, 2, 4) != 0 || nx != 5) Fail("help1 fold 3: next did not pass the live last applied with nothing owed");
        float dP = 0.0f;
        int n = 0;
        float d[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) d[i] = 0.0f;
        std::vector<BuildHelpEntry> sm(2);
        sm[0].seq = 3; sm[0].dP = 1.0f; sm[0].applied = 1; sm[1].seq = 4; sm[1].dP = 2.0f;
        BuildHelpSum(sm, &dP, &n, d);
        if (dP != 2.0f) Fail("help1 fold 3: work the owner applied was put back on the copy again");
    }
    {   /* E on the wire: the STATE tail carries landed + live */
        std::vector<BuildHelpAck> landed(1), live(2), t;
        landed[0].slot = 1; landed[0].seq = 3; landed[0].nEx = 1; landed[0].ex[0] = 2.0f;
        live[0].slot = 1; live[0].seq = 6; live[1].slot = 2; live[1].seq = 2;
        BuildHelpTailOf(&landed, &live, &t);
        if (t.size() != 2 || t[0].slot != 1 || t[0].seq != 3 || t[0].live != 6 || t[0].nEx != 1 || t[1].slot != 2 || t[1].seq != 0 || t[1].live != 2 || t[1].nEx != 0)
            Fail("help1 fold 3: the STATE tail did not carry landed + live");
        BuildMsg st; st.kind = kBuildState; st.key = "k3"; st.progress = 1.0f; st.needed = 2.0f; st.helpAcks = t;
        BuildMsg pl = st; pl.helpAcks.clear();
        std::vector<char> b, pb;
        BuildMsg o;
        if (!EncodeBuild(&b, st) || !EncodeBuild(&pb, pl)) Fail("help1 fold 3: a landed + live tail did not encode");
        else
        {
            if (DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.helpAcks.size() != 2 || o.helpAcks[0].live != 6 || o.helpAcks[0].seq != 3
                || o.helpAcks[1].seq != 0 || o.helpAcks[1].live != 2) Fail("help1 fold 3: a landed + live tail did not survive the round trip");
            std::vector<char> lb(b);
            const unsigned int one = 1;
            std::memcpy(&lb[pb.size() + 1 + 1 + 4], &one, 4);
            if (DecodeBuild(&lb[0], lb.size(), &o) != kBuildDecodeBadValue) Fail("help1 fold 3: a tail row with live below landed was accepted");
            std::vector<char> ob(pb);   /* a pp.build record written before fold 3 (no live flag): live reads as the landed seq */
            ob.push_back(1); ob.push_back(1);
            const unsigned int sq = 7;
            const char* sp = (const char*)&sq;
            ob.insert(ob.end(), sp, sp + 4);
            ob.push_back(0);
            if (DecodeBuild(&ob[0], ob.size(), &o) != kBuildDecodeOk || o.helpAcks.size() != 1 || o.helpAcks[0].seq != 7 || o.helpAcks[0].live != 7)
                Fail("help1 fold 3: an older tail (no live) did not read with live = landed");
        }
    }
    {   /* F: pp.help's goneSeq floor only for a known, equal placement; one pp.help row per (key, nonce) */
        if (BuildHelpFloorApplies(0, 0) || BuildHelpFloorApplies(0, 5) || BuildHelpFloorApplies(5, 0) || BuildHelpFloorApplies(5, 6) || !BuildHelpFloorApplies(5, 5))
            Fail("help1 fold 3: the pp.help floor rule is wrong (nonce 0 must never merge)");
        std::vector<coopown::HelpRecRow> rows(2);
        rows[0].key = "k"; rows[0].nonce = 5; rows[0].nextSeq = 3;
        rows[1].key = "k"; rows[1].nonce = 6; rows[1].nextSeq = 9;
        std::vector<char> rb;
        coopown::EncodeHelpRec(rows, &rb);
        std::vector<coopown::HelpRecRow> got;
        if (!coopown::DecodeHelpRec(rb, &got) || got.size() != 2) Fail("help1 fold 3: pp.help did not keep one row per placement at a key");
        rows[1].nonce = 5;
        coopown::EncodeHelpRec(rows, &rb);
        if (coopown::DecodeHelpRec(rb, &got)) Fail("help1 fold 3: pp.help naming a placement twice was read");
    }
    {   /* C: a GONE carries the refused tail; the helper hands back what it has not yet, plus the entries the owner did not apply */
        BuildMsg g; g.kind = kBuildHelpGone; g.key = "k1"; g.seq = 6; g.applied = 4; g.nonce = 0xBEEFu; g.nMats = 2; g.mats[0] = 3.5f; g.mats[1] = 2.0f;
        std::vector<char> b;
        BuildMsg o;
        if (!EncodeBuild(&b, g) || b.size() != 1 + 1 + 2 + 4 + 4 + 4 + 1 + 8) Fail("help1 fold 3: a HELP_GONE with a refused tail did not encode to its size");
        else
        {
            if (DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.nonce != 0xBEEFu || o.applied != 4 || o.nMats != 2 || o.mats[0] != 3.5f || o.mats[1] != 2.0f)
                Fail("help1 fold 3: a HELP_GONE's nonce and refused tail did not survive the round trip");
            for (size_t cut = 1; cut < b.size(); ++cut) if (DecodeBuild(&b[0], cut, &o) == kBuildDecodeOk) { Fail("help1 fold 3: a truncated HELP_GONE was accepted"); break; }
            std::vector<char> nv(b);
            const float neg = -1.0f;
            std::memcpy(&nv[nv.size() - 4], &neg, 4);
            if (DecodeBuild(&nv[0], nv.size(), &o) != kBuildDecodeBadValue) Fail("help1 fold 3: a negative refused amount was accepted");
        }
        int refunded[kBuildMaxMats], owe[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) refunded[i] = 0;
        refunded[0] = 1; refunded[1] = 2;
        if (BuildHelpGoneRefused(g.mats, g.nMats, refunded, owe) != 2 || owe[0] != 2 || owe[1] != 0 || owe[2] != 0) Fail("help1 fold 3: the GONE's refused tail was not handed back less what was handed back");
        std::vector<BuildHelpEntry> ow;
        for (unsigned int q = 3; q <= 6; ++q) { BuildHelpEntry e; e.seq = q; e.n = 1; e.d[0] = 1.0f; ow.push_back(e); }
        float back[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) back[i] = 0.0f;
        int n = 0;
        if (BuildHelpGoneSum(ow, g.seq, g.applied, back, &n) != 2 || back[0] != 2.0f) Fail("help1 fold 3: GONE handed back applied work");
        if ((int)(back[0] + 0.001f) + owe[0] != 4) Fail("help1 fold 3: the GONE refund (unapplied entries + refused tail) is wrong");
        refunded[0] += owe[0];
        if (BuildHelpGoneRefused(g.mats, g.nMats, refunded, owe) != 0) Fail("help1 fold 3: a GONE answered twice handed the refused tail back twice");
    }
    {   /* A: HELP_WORK carries its placement's nonce; an owed-file line written before reads with nonce 0 (padded) */
        BuildMsg w; w.kind = kBuildHelpWork; w.key = "k2"; w.seq = 3; w.progress = 0.5f; w.nMats = 1; w.mats[0] = 1.0f; w.nonce = 0x1234u;
        std::vector<char> b;
        BuildMsg o;
        if (!EncodeBuild(&b, w) || DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.nonce != 0x1234u) Fail("help1 fold 3: a HELP_WORK's nonce did not survive");
        else
        {
            std::vector<char> old(b.begin(), b.end() - 4);
            if (DecodeBuild(&old[0], old.size(), &o) == kBuildDecodeOk) Fail("help1 fold 3: a HELP_WORK without its nonce was accepted on the wire");
            old.resize(old.size() + 4, 0);
            if (DecodeBuild(&old[0], old.size(), &o) != kBuildDecodeOk || o.nonce != 0 || o.seq != 3) Fail("help1 fold 3: an older owed-file entry (nonce padded 0) did not read");
        }
    }
    {   /* B: the owner's gone marker row key names (key, nonce) and verifies; a changed row is written again */
        const std::string k(63, 'x');
        const std::string rk = BuildGoneRowKey(k, 0xA1B2C3D4u);
        unsigned int nn = 0;
        if (rk.size() > kBuildMaxKey || !BuildGoneRowOf(rk, k, &nn) || nn != 0xA1B2C3D4u) Fail("help1 fold 3: a gone marker row key did not name its placement");
        if (BuildGoneRowKey(k, 1u) == rk || BuildGoneRowOf(rk, "other", &nn) || BuildGoneRowOf(k, k, &nn)) Fail("help1 fold 3: a gone marker row key matched another placement");
        std::vector<BuildHelpAck> x(1), y;
        x[0].slot = 1; x[0].seq = 2;
        y = x;
        if (!BuildHelpAcksSame(x, y)) Fail("help1 fold 3: equal rows compared different");
        y[0].seq = 3;
        if (BuildHelpAcksSame(x, y)) Fail("help1 fold 3: changed rows compared the same");
    }
}
void t_help1_wire()
{
    coopbuild::BuildMsg w; w.kind = coopbuild::kBuildHelpWork; w.key = "P7n|help"; w.seq = 7; w.progress = 0.25f; w.nMats = 2; w.mats[0] = 3.0f; w.mats[1] = 0.5f;
    std::vector<char> b;
    coopbuild::BuildMsg o;
    if (!coopbuild::EncodeBuild(&b, w) || b.size() != 1 + 1 + w.key.size() + 4 + 4 + 1 + 1 + 8 + 4) Fail("help1 fold 3: a HELP_WORK did not encode to its size (+ u32 nonce)");
    else
    {
        if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.kind != 8 || o.key != w.key || o.seq != 7 || o.progress != 0.25f
            || o.reset != 0 || o.nMats != 2 || o.mats[0] != 3.0f || o.mats[1] != 0.5f)
            Fail("help1: a HELP_WORK did not survive the round trip");
        for (size_t cut = 1; cut < b.size(); ++cut)
            if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("help1: a truncated HELP_WORK was accepted"); break; }
        std::vector<char> t(b); t.push_back(0);
        if (coopbuild::DecodeBuild(&t[0], t.size(), &o) == coopbuild::kBuildDecodeOk) Fail("help1: a HELP_WORK with a trailing byte was accepted");
        std::vector<char> nv(b); const float neg = -1.0f; std::memcpy(&nv[1 + 1 + w.key.size() + 4], &neg, 4);
        if (coopbuild::DecodeBuild(&nv[0], nv.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("help1: a negative progress delta was accepted");
        std::vector<char> z0(b); std::memset(&z0[1 + 1 + w.key.size()], 0, 4);
        if (coopbuild::DecodeBuild(&z0[0], z0.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("help1: a HELP_WORK with seq 0 was accepted");
    }
    coopbuild::BuildMsg rs = w; rs.reset = 1; std::vector<char> rb;
    if (!coopbuild::EncodeBuild(&rb, rs) || coopbuild::DecodeBuild(&rb[0], rb.size(), &o) != coopbuild::kBuildDecodeOk || o.reset != 1) Fail("help1: a reset HELP_WORK did not survive");
    coopbuild::BuildMsg bad = w; bad.seq = 0; std::vector<char> xb;
    if (coopbuild::EncodeBuild(&xb, bad)) Fail("help1: a HELP_WORK with seq 0 was encoded");
    bad = w; bad.mats[1] = -1.0f;
    if (coopbuild::EncodeBuild(&xb, bad)) Fail("help1: a negative material delta was encoded");
    bad = w; bad.reset = 2;
    if (coopbuild::EncodeBuild(&xb, bad)) Fail("help1: reset=2 was encoded");
    bad = w; bad.progress = 2.0e6f;
    if (coopbuild::EncodeBuild(&xb, bad)) Fail("help1: a progress delta past 1e6 was encoded");
    bad = w; bad.key.clear();
    if (coopbuild::EncodeBuild(&xb, bad)) Fail("help1: a HELP_WORK with no key was encoded");

    /* STATE: the optional tail */
    coopbuild::BuildMsg s; s.kind = coopbuild::kBuildState; s.key = "P7n|help"; s.progress = 1.0f; s.needed = 2.0f; s.nMats = 1; s.mats[0] = 4.0f;
    std::vector<char> plain;
    if (!coopbuild::EncodeBuild(&plain, s)) { Fail("help1: a plain STATE did not encode"); return; }
    if (coopbuild::DecodeBuild(&plain[0], plain.size(), &o) != coopbuild::kBuildDecodeOk || !o.helpAcks.empty()) Fail("help1: a STATE with no tail (an older sender) did not decode");
    coopbuild::BuildHelpAck a; a.slot = 1; a.seq = 9; a.nEx = 2; a.ex[0] = 0.0f; a.ex[1] = 2.5f;
    s.helpAcks.push_back(a);
    std::vector<char> sb;
    if (!coopbuild::EncodeBuild(&sb, s) || sb.size() != plain.size() + 1 + 2 + 4 + 4 + 1 + 8) Fail("help1 fold 3: a STATE tail did not encode to its size (two-byte slot, + u32 live)");
    else
    {
        if (coopbuild::DecodeBuild(&sb[0], sb.size(), &o) != coopbuild::kBuildDecodeOk || o.helpAcks.size() != 1 || o.helpAcks[0].slot != 1 || o.helpAcks[0].seq != 9
            || o.helpAcks[0].nEx != 2 || o.helpAcks[0].ex[1] != 2.5f || o.mats[0] != 4.0f || o.progress != 1.0f)
            Fail("help1: a STATE tail did not survive the round trip");
        for (size_t cut = plain.size() + 1; cut < sb.size(); ++cut)
            if (coopbuild::DecodeBuild(&sb[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("help1: a truncated STATE tail was accepted"); break; }
        std::vector<char> tr(sb); tr.push_back(0);
        if (coopbuild::DecodeBuild(&tr[0], tr.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("help1: a byte after the STATE tail was accepted");
    }
    std::vector<char> e0(plain); e0.push_back(0);
    if (coopbuild::DecodeBuild(&e0[0], e0.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("help1: a STATE tail with 0 rows was accepted");
    coopbuild::BuildMsg twice = s; twice.helpAcks.push_back(a); std::vector<char> tb;
    if (coopbuild::EncodeBuild(&tb, twice)) Fail("help1: a STATE tail naming one slot twice was encoded");
    coopbuild::BuildMsg s0 = s; s0.helpAcks[0].seq = 0;
    if (coopbuild::EncodeBuild(&tb, s0)) Fail("help1: a STATE tail row with seq 0 was encoded");
    coopbuild::BuildMsg many = s; many.helpAcks.clear();
    for (unsigned char sl = 0; sl < 9; ++sl) { coopbuild::BuildHelpAck r; r.slot = sl; r.seq = 1; many.helpAcks.push_back(r); }
    if (coopbuild::EncodeBuild(&tb, many)) Fail("help1: a STATE tail past 8 helper slots was encoded");
}
/* help1 fold (review of 4dfe21f1): strictly in-order apply, the gone verdict, the start seq, the gone refund sum, HELP_GONE on the wire,
   the frequent-line cap and pp.help */
void t_help1_fold()
{
    using namespace coopbuild;
    if (BuildHelpSeqVerdict(0, 1) != kBuildHelpApply || BuildHelpSeqVerdict(5, 6) != kBuildHelpApply) Fail("help1 fold: the next seq was not applied");
    if (BuildHelpSeqVerdict(5, 7) != kBuildHelpHold || BuildHelpSeqVerdict(0, 2) != kBuildHelpHold || BuildHelpSeqVerdict(5, 9) != kBuildHelpHold)
        Fail("help1 fold: a seq past a gap was not held");
    if (BuildHelpSeqVerdict(5, 5) != kBuildHelpDup || BuildHelpSeqVerdict(5, 1) != kBuildHelpDup || BuildHelpSeqVerdict(3, 0) != kBuildHelpDup)
        Fail("help1 fold: a resent seq was not a duplicate");
    {   /* the review's case: 4 dropped (a world reload cleared it), 5 arrives first - held; 4 comes again and applies; then 5 */
        unsigned int last = 3;
        if (BuildHelpSeqVerdict(last, 5) != kBuildHelpHold) Fail("help1 fold: 5 before 4 was not held");
        if (BuildHelpSeqVerdict(last, 4) != kBuildHelpApply) Fail("help1 fold: the gap's 4 was not applied");
        last = 4;
        if (BuildHelpSeqVerdict(last, 5) != kBuildHelpApply) Fail("help1 fold: the held 5 was not applied after 4");
    }
    if (BuildHelpRowVerdict(1, 1, 0, 0, 0, 0) != kBuildHelpRowOk) Fail("help1 fold: an own live piece was not taken");
    if (BuildHelpRowVerdict(1, 1, 0, 1, 0, 0) != kBuildHelpRowGone || BuildHelpRowVerdict(1, 1, 0, 0, 1, 0) != kBuildHelpRowGone)
        Fail("help1 fold: a removed / dismantled piece was not answered gone");
    if (BuildHelpRowVerdict(1, 0, 0, 0, 0, 0) != kBuildHelpRowGone || BuildHelpRowVerdict(1, 1, 1, 0, 0, 0) != kBuildHelpRowGone)
        Fail("help1 fold: a piece no longer own was not answered gone");
    if (BuildHelpRowVerdict(0, 0, 0, 0, 0, 1) != kBuildHelpRowGone) Fail("help1 fold: a swept removed key was not answered gone");
    if (BuildHelpRowVerdict(0, 0, 0, 0, 0, 0) != kBuildHelpRowUnknown) Fail("help1 fold: an unseen key was answered");
    if (BuildHelpStartSeq(1, 0, 0) != 1 || BuildHelpStartSeq(1, 0, 7) != 8 || BuildHelpStartSeq(9, 0, 7) != 9 || BuildHelpStartSeq(3, 12, 7) != 12
        || BuildHelpStartSeq(0, 0, 0) != 1)
        Fail("help1 fold: the start seq is wrong (a fresh row must start past the owner's last applied)");
    std::vector<BuildHelpEntry> owed;
    for (unsigned int q = 4; q <= 6; ++q) { BuildHelpEntry e; e.seq = q; e.n = 2; e.d[0] = 1.0f * (float)q; e.d[1] = 0.5f; owed.push_back(e); }
    float back[kBuildMaxMats];
    for (unsigned int i = 0; i < kBuildMaxMats; ++i) back[i] = 0.0f;
    int n = 0;
    if (BuildHelpGoneSum(owed, 5, 0, back, &n) != 2 || n != 2 || back[0] != 9.0f || back[1] != 1.0f || back[2] != 0.0f) Fail("help1 fold: the gone refund sum is wrong");
    if (BuildHelpConfirm(&owed, 5) != 2 || owed.size() != 1 || owed[0].seq != 6) Fail("help1 fold: the gone entries were not dropped");
    {   /* help1 fold 2 (re-check HIGH 2): a GONE never hands back what the owner applied */
        std::vector<BuildHelpEntry> o2;
        for (unsigned int q = 3; q <= 6; ++q) { BuildHelpEntry e; e.seq = q; e.n = 1; e.d[0] = 1.0f; o2.push_back(e); }
        float bk[kBuildMaxMats];
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) bk[i] = 0.0f;
        int nn = 0;
        if (BuildHelpGoneSum(o2, 6, 4, bk, &nn) != 2 || bk[0] != 2.0f || nn != 1) Fail("help1 fold 2: GONE handed back work the owner applied");
        for (unsigned int i = 0; i < kBuildMaxMats; ++i) bk[i] = 0.0f;
        nn = 0;
        if (BuildHelpGoneSum(o2, 4, 4, bk, &nn) != 0 || bk[0] != 0.0f) Fail("help1 fold 2: a GONE at or below the last applied handed something back");
        if (BuildHelpGoneSettled(4, 6) != 6 || BuildHelpGoneSettled(6, 4) != 6 || BuildHelpGoneSettled(5, 0) != 5) Fail("help1 fold 2: the settled bound is wrong");
        if (BuildHelpConfirm(&o2, BuildHelpGoneSettled(3, 5)) != 3 || o2.size() != 1 || o2[0].seq != 6) Fail("help1 fold 2: the applied entries did not go with the GONE");
    }
    {   /* help1 fold 2 (re-check HIGH 1): a sequence gap resyncs from the owner's last applied */
        std::vector<BuildHelpEntry> o3;
        for (unsigned int q = 7; q >= 5; --q) { BuildHelpEntry e; e.seq = q; e.dP = (float)q; o3.push_back(e); }
        unsigned int nx = 8;
        if (BuildHelpResync(&o3, &nx, 2, 2) != 3 || o3.size() != 3 || o3[0].seq != 3 || o3[1].seq != 4 || o3[2].seq != 5 || nx != 6
            || o3[0].dP != 5.0f || o3[1].dP != 6.0f || o3[2].dP != 7.0f)
            Fail("help1 fold 2: a sequence gap was not renumbered from the owner's last applied (in order, work unchanged)");
        nx = 6;
        if (BuildHelpResync(&o3, &nx, 2, 2) != 0 || o3[0].seq != 3 || o3[2].seq != 5 || nx != 6) Fail("help1 fold 2: entries with no gap were renumbered");
        nx = 4;
        if (BuildHelpResync(&o3, &nx, 2, 2) != 0 || nx != 6) Fail("help1 fold 2: next fell below the unconfirmed entries");
        unsigned int last = 2;
        for (size_t i = 0; i < o3.size(); ++i)
        {
            if (BuildHelpSeqVerdict(last, o3[i].seq) != kBuildHelpApply) Fail("help1 fold 2: a renumbered entry was not the owner's next");
            last = o3[i].seq;
        }
        std::vector<BuildHelpEntry> none;
        nx = 40;
        if (BuildHelpResync(&none, &nx, 0, 0) != 0 || nx != 40) Fail("help1 fold 3: next was lowered with nothing owed (a new placement starts a fresh row instead)");
        nx = 2;
        BuildHelpResync(&none, &nx, 9, 9);
        if (nx != 10) Fail("help1 fold 2: next did not follow the owner's last applied");
        std::vector<BuildHelpEntry> o4;
        for (unsigned int q = 3; q <= 4; ++q) { BuildHelpEntry e; e.seq = q; o4.push_back(e); }
        nx = 5;
        if (BuildHelpResync(&o4, &nx, 0, 0) != 2 || o4[0].seq != 1 || o4[1].seq != 2 || nx != 3) Fail("help1 fold 2: an owner with nothing applied did not restart the entries at 1");
        if (!BuildHelpSuperseded(5, 3) || !BuildHelpSuperseded(3, 3) || BuildHelpSuperseded(2, 3)) Fail("help1 fold 2: the superseded rule is wrong");
        BuildHelpEntry bad;
        bad.seq = 4; bad.n = 2; bad.reset = 3; bad.d[0] = -1.0f; bad.d[1] = 2.0e6f;
        volatile float zero = 0.0f;
        bad.dP = zero / zero;
        if (BuildHelpEntrySanitize(&bad) != 4 || bad.dP != 0.0f || bad.d[0] != 0.0f || bad.d[1] != kBuildHelpMax || bad.reset != 1 || bad.seq != 4)
            Fail("help1 fold 2: an unencodable entry was not made encodable");
        BuildMsg wm;
        wm.kind = kBuildHelpWork; wm.key = "k1"; wm.seq = bad.seq; wm.progress = bad.dP; wm.reset = bad.reset; wm.nMats = bad.n;
        for (unsigned int i = 0; i < bad.n; ++i) wm.mats[i] = bad.d[i];
        if (!BuildEncodable(wm)) Fail("help1 fold 2: a sanitized entry does not encode");
        if (BuildHelpEntrySanitize(&bad) != 0) Fail("help1 fold 2: a clean entry was changed");
        if (BuildHelpRowStale(0, 5) || BuildHelpRowStale(5, 0) || BuildHelpRowStale(5, 5) || !BuildHelpRowStale(5, 6)) Fail("help1 fold 2: the re-placed row rule is wrong");
    }
    {   /* help1 fold 2 (re-check MED 4): an entry read back from the owed file is frozen */
        BuildHelpEntry e;
        if (!BuildHelpJoinable(e)) Fail("help1 fold 2: a fresh entry was not joinable");
        e.loaded = 1;
        if (BuildHelpJoinable(e)) Fail("help1 fold 2: new work joined an entry read back from the owed file");
        e.loaded = 0; e.sent = 1;
        if (BuildHelpJoinable(e)) Fail("help1 fold 2: new work joined an entry already sent");
    }
    {   /* help1 fold 2 (re-check LOW 7): one order verdict on every road */
        if (BuildHelpOrderVerdict(kBuildTaskBuild, 1, 1) != kBuildHelpOrderWallRefused || BuildHelpOrderVerdict(kBuildTaskAddMaterials, 1, 1) != kBuildHelpOrderWallRefused)
            Fail("help1 fold 2: a grouped wall copy order was not refused");
        if (BuildHelpOrderVerdict(kBuildTaskAddMaterials, 1, 0) != kBuildHelpOrderGiven || BuildHelpOrderVerdict(kBuildTaskRepair, 1, 1) != kBuildHelpOrderNone
            || BuildHelpOrderVerdict(kBuildTaskBuild, 0, 1) != kBuildHelpOrderNone)
            Fail("help1 fold 2: the order verdict is wrong");
        if (BuildHelpOrderVerdict(kBuildTaskBuild, 1, kBuildWallKindOwnRecord) != kBuildHelpOrderGiven || BuildHelpOrderVerdict(kBuildTaskAddMaterials, 1, kBuildWallKindOwnRecord) != kBuildHelpOrderGiven)
            Fail("walls: a build / materials order on an own-record wall copy was not help");
        if (BuildHelpOrderVerdict(kBuildTaskBuild, 1, kBuildWallKindGroup) != kBuildHelpOrderWallRefused || BuildHelpOrderVerdict(kBuildTaskAddMaterials, 1, kBuildWallKindGroup) != kBuildHelpOrderWallRefused)
            Fail("walls: a build / materials order on a grouped wall copy was not refused");
        if (BuildHelpOrderVerdict(kBuildTaskBuild, 1, kBuildWallKindNone) != kBuildHelpOrderGiven || BuildHelpOrderVerdict(kBuildTaskBuild, 0, kBuildWallKindOwnRecord) != kBuildHelpOrderNone
            || BuildHelpOrderVerdict(kBuildTaskRepair, 1, kBuildWallKindOwnRecord) != kBuildHelpOrderNone)
            Fail("walls: the order verdict changed for a non-wall, an own piece or a repair");
    }
    {   /* help1 fold 2 (re-check LOW 5 / HIGH 1): pp.help omh2 - the settled floor and the placement nonce; omh1 still reads */
        std::vector<coopown::HelpRecRow> r2(1);
        r2[0].key = "k2"; r2[0].slot = 3; r2[0].nextSeq = 12; r2[0].given[1] = 2; r2[0].goneSeq = 9; r2[0].nonce = 0xABCDu;
        std::vector<char> b2;
        coopown::EncodeHelpRec(r2, &b2);
        std::vector<coopown::HelpRecRow> g2;
        if (!coopown::DecodeHelpRec(b2, &g2) || g2.size() != 1 || g2[0].goneSeq != 9 || g2[0].nonce != 0xABCDu || g2[0].nextSeq != 12 || g2[0].given[1] != 2)
            Fail("help1 fold 2: pp.help omh2 did not survive the round trip");
        for (size_t cut = 1; cut < b2.size(); ++cut)
        {
            std::vector<char> c(b2.begin(), b2.begin() + (std::ptrdiff_t)cut);
            if (coopown::DecodeHelpRec(c, &g2)) { Fail("help1 fold 2: a truncated pp.help omh2 was read"); break; }
        }
        std::vector<char> v1;
        coopown::RecPutStr(&v1, "omh1"); coopown::RecPutI32(&v1, 1); coopown::RecPutStr(&v1, "k1"); coopown::RecPutI32(&v1, 2); coopown::RecPutI32(&v1, 5);
        for (unsigned int i = 0; i < coopown::kHelpRecMats; ++i) coopown::RecPutI32(&v1, (int)i);
        if (!coopown::DecodeHelpRec(v1, &g2) || g2.size() != 1 || g2[0].nextSeq != 5 || g2[0].given[3] != 3 || g2[0].goneSeq != 0 || g2[0].nonce != 0)
            Fail("help1 fold 2: an omh1 pp.help no longer reads");
        coopown::HelpRecRow ja, jf;
        ja.key = "k"; ja.slot = 1; ja.nonce = 7; ja.goneSeq = 2;
        jf.key = "k"; jf.slot = 1; jf.nonce = 8; jf.goneSeq = 5; jf.nextSeq = 30;
        if (coopown::HelpRecJoin(&ja, jf) != 0 || ja.goneSeq != 2 || ja.nextSeq != 1) Fail("help1 fold 2: another placement's pp.help row was joined");
        jf.nonce = 7;
        if (coopown::HelpRecJoin(&ja, jf) != 1 || ja.goneSeq != 5 || ja.nextSeq != 30) Fail("help1 fold 2: the settled floor did not join (the higher wins)");
        jf.nonce = 0; jf.goneSeq = 1;
        if (coopown::HelpRecJoin(&ja, jf) != 1 || ja.goneSeq != 5 || ja.nonce != 7) Fail("help1 fold 2: a join lowered the floor or lost the nonce");
    }
    for (long long i = 1; i <= 10; ++i) if (!BuildHelpFreqLine(i)) Fail("help1 fold: an early frequent line was hidden");
    if (BuildHelpFreqLine(11) || !BuildHelpFreqLine(30) || BuildHelpFreqLine(31) || !BuildHelpFreqLine(60) || BuildHelpFreqLine(0)) Fail("help1 fold: the frequent-line cap is wrong");
    BuildMsg g;
    g.kind = kBuildHelpGone; g.key = "k1"; g.seq = 7; g.applied = 5;
    std::vector<char> b;
    BuildMsg o;
    if (!EncodeBuild(&b, g) || b.size() != 1 + 1 + 2 + 4 + 4 + 4 + 1) Fail("help1 fold 3: a HELP_GONE did not encode to kind | key | seq | applied | nonce | n");
    else
    {
        if (DecodeBuild(&b[0], b.size(), &o) != kBuildDecodeOk || o.kind != kBuildHelpGone || o.key != "k1" || o.seq != 7 || o.applied != 5) Fail("help1 fold 2: a HELP_GONE did not survive the round trip");
        for (size_t cut = 1; cut < b.size(); ++cut) if (DecodeBuild(&b[0], cut, &o) == kBuildDecodeOk) { Fail("help1 fold: a truncated HELP_GONE was accepted"); break; }
        std::vector<char> t(b);
        t.push_back(0);
        if (DecodeBuild(&t[0], t.size(), &o) == kBuildDecodeOk) Fail("help1 fold: a HELP_GONE with a trailing byte was accepted");
        std::vector<char> z(b);
        for (size_t i = 1 + 1 + 2; i < 1 + 1 + 2 + 4; ++i) z[i] = 0;   /* fold 3: seq sits right after the key */
        if (DecodeBuild(&z[0], z.size(), &o) != kBuildDecodeBadValue) Fail("help1 fold: a HELP_GONE with seq 0 was accepted");
    }
    std::vector<char> xb;
    BuildMsg bad = g;
    bad.seq = 0;
    if (EncodeBuild(&xb, bad)) Fail("help1 fold: a HELP_GONE with seq 0 was encoded");
    bad = g;
    bad.key.clear();
    if (EncodeBuild(&xb, bad)) Fail("help1 fold: a HELP_GONE with no key was encoded");
    std::vector<coopown::HelpRecRow> rows(2);
    rows[0].key = "zz"; rows[0].slot = 1; rows[0].nextSeq = 9; rows[0].given[3] = 4;
    rows[1].key = "aa"; rows[1].slot = 0; rows[1].nextSeq = 2;
    std::vector<char> rb;
    coopown::EncodeHelpRec(rows, &rb);
    std::vector<coopown::HelpRecRow> got;
    if (!coopown::DecodeHelpRec(rb, &got) || got.size() != 2 || got[0].key != "aa" || got[1].nextSeq != 9 || got[1].given[3] != 4 || got[1].slot != 1 || got[0].slot != 0)
        Fail("help1 fold: pp.help did not survive the round trip");
    std::vector<char> tr(rb);
    tr.push_back(0);
    if (coopown::DecodeHelpRec(tr, &got) || !got.empty()) Fail("help1 fold: pp.help with a trailing byte was read");
    for (size_t cut = 1; cut < rb.size(); ++cut)
    {
        std::vector<char> c(rb.begin(), rb.begin() + (std::ptrdiff_t)cut);
        if (coopown::DecodeHelpRec(c, &got)) { Fail("help1 fold: a truncated pp.help was read"); break; }
    }
    rows[1].key = "zz";
    coopown::EncodeHelpRec(rows, &rb);
    if (coopown::DecodeHelpRec(rb, &got)) Fail("help1 fold: pp.help naming a key twice was read");
    rows[1].key = "aa"; rows[1].nextSeq = 0;
    coopown::EncodeHelpRec(rows, &rb);
    if (coopown::DecodeHelpRec(rb, &got)) Fail("help1 fold: pp.help with next seq 0 was read");
    rows[1].nextSeq = 2; rows[1].given[0] = -1;
    coopown::EncodeHelpRec(rows, &rb);
    if (coopown::DecodeHelpRec(rb, &got)) Fail("help1 fold: pp.help with a negative given count was read");
    coopown::HelpRecRow a, f;
    a.key = "k"; a.slot = 2; a.nextSeq = 5; a.given[0] = 3;
    f.slot = 2; f.nextSeq = 9; f.given[0] = 1; f.given[1] = 2;
    if (coopown::HelpRecJoin(&a, f) != 1 || a.nextSeq != 9 || a.given[0] != 3 || a.given[1] != 2) Fail("help1 fold: a joined pp.help row went lower");
    f.slot = 3; f.nextSeq = 99;
    if (coopown::HelpRecJoin(&a, f) != 0 || a.nextSeq != 9) Fail("help1 fold: another owner's pp.help row was joined");
}
/* help1: the seq / dup / cap / refund decisions and the helper's owed list */
void t_help1_decisions()
{
    using namespace coopbuild;
    if (BuildHelpSeqVerdict(0, 1) != kBuildHelpApply) Fail("help1: the first seq was not applied");
    if (BuildHelpSeqVerdict(5, 5) != kBuildHelpDup || BuildHelpSeqVerdict(5, 3) != kBuildHelpDup) Fail("help1: a resent seq was applied again");
    if (BuildHelpSeqVerdict(5, 6) != kBuildHelpApply || BuildHelpSeqVerdict(5, 9) != kBuildHelpHold) Fail("help1: a new seq was dropped (help1 fold: past a gap it is held)");
    if (BuildHelpSeqVerdict(0, 0) != kBuildHelpDup) Fail("help1: seq 0 was applied");
    float ex = -1.0f;
    if (BuildHelpMatApply(2.0f, 3.0f, 10.0f, &ex) != 5.0f || ex != 0.0f) Fail("help1: a material delta that fits was capped");
    if (BuildHelpMatApply(8.0f, 5.0f, 10.0f, &ex) != 10.0f || ex != 3.0f) Fail("help1: the part past a material's total was not split off for the refund");
    if (BuildHelpMatApply(12.0f, 2.0f, 10.0f, &ex) != 12.0f || ex != 2.0f) Fail("help1: a material already past its total took more");
    if (BuildHelpMatApply(1.0f, 4.0f, -1.0f, &ex) != 5.0f || ex != 0.0f) Fail("help1: an unread total capped the delta");
    if (BuildHelpRefundOwed(3.0f, 0) != 3 || BuildHelpRefundOwed(3.0f, 3) != 0 || BuildHelpRefundOwed(4.5f, 3) != 1 || BuildHelpRefundOwed(0.5f, 0) != 0)
        Fail("help1: the refund owed is wrong");
    if (BuildHelpRefundOwed(2.0f, 5) != 0 || BuildHelpRefundOwed(-1.0f, 0) != 0) Fail("help1: a refund went negative");
    if (BuildHelpCompletes(9.0f, 1.0f, 10.0f) != 1 || BuildHelpCompletes(9.0f, 0.5f, 10.0f) != 0 || BuildHelpCompletes(5.0f, 5.0f, 0.0f) != 0)
        Fail("help1: the completion decision is wrong");
    int capped = -1;
    if (BuildHelpCopyCap(9.995f, 10.0f, 0, &capped) != 10.0f * 0.999f || capped != 1) Fail("help1: a copy at needed was not capped below complete");
    if (BuildHelpCopyCap(12.0f, 10.0f, 0, &capped) != 10.0f * 0.999f || capped != 1) Fail("help1: a copy past needed was not capped");
    if (BuildHelpCopyCap(5.0f, 10.0f, 0, &capped) != 5.0f || capped != 0) Fail("help1: a copy below the cap was capped");
    if (BuildHelpCopyCap(12.0f, 10.0f, 1, &capped) != 12.0f || capped != 0) Fail("help1: a ruin was capped (its reset reads needed)");
    std::vector<BuildHelpEntry> owed;
    for (unsigned int q = 1; q <= 3; ++q) { BuildHelpEntry e; e.seq = q; e.dP = 0.1f * (float)q; e.n = 1; e.d[0] = (float)q; owed.push_back(e); }
    if (BuildHelpConfirm(&owed, 2) != 2 || owed.size() != 1 || owed[0].seq != 3) Fail("help1: the confirmation did not drop the confirmed work");
    if (BuildHelpConfirm(&owed, 2) != 0 || owed.size() != 1) Fail("help1: an old confirmation dropped newer work");
    float dP = 0.0f; int n = 0; float d[kBuildMaxMats];
    for (unsigned int i = 0; i < kBuildMaxMats; ++i) d[i] = 0.0f;
    BuildHelpSum(owed, &dP, &n, d);
    if (n != 1 || d[0] != 3.0f || dP < 0.29f || dP > 0.31f) Fail("help1: the unconfirmed work sum is wrong");
    if (BuildHelpNextSeq(1, 40) != 41 || BuildHelpNextSeq(50, 40) != 50) Fail("help1: the next seq re-used a confirmed one");
    std::vector<BuildHelpAck> acks;
    if (BuildHelpAckFor(&acks, 3, 0) != 0) Fail("help1: a missing ack row was found");
    BuildHelpAck* r3 = BuildHelpAckFor(&acks, 3, 1);
    if (r3 == 0 || acks.size() != 1 || BuildHelpAckFor(&acks, 3, 1) != &acks[0]) Fail("help1: an ack row was not made exactly once");
    for (unsigned char sl = 10; acks.size() < kBuildMaxHelpers; ++sl) BuildHelpAckFor(&acks, sl, 1);
    if (BuildHelpAckFor(&acks, 99, 1) != 0 || acks.size() != kBuildMaxHelpers) Fail("help1: the tail grew past its cap");
}

/* P87: pieces dismantled while the other player was away - the receiver's reconcile decision and the owner's tombstones */
void t_p87_reconcile()
{
    /* the receiver: only the sender's stand-in's piece that the owner does not list is removed */
    if (coopbuild::BuildReconcileAct(0, 1, 0, 0, 1, 0) != coopbuild::kBuildReconRemove) Fail("p87: a save-restored copy the owner no longer lists was not removed");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 1, 0, 0) != coopbuild::kBuildReconKeepOwn) Fail("p87: the receiver's OWN piece was not kept");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 1, 1, 0) != coopbuild::kBuildReconKeepOwn) Fail("p87: an own piece was not kept first");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 1, 0, 1) != coopbuild::kBuildReconKeepOwn) Fail("p87: an own dismantling piece was not kept");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 0, 0, 0) != coopbuild::kBuildReconKeepOther) Fail("p87: another owner's piece was not kept");
    for (int f = 0; f < 2; ++f)
        for (int m = 0; m < 2; ++m)
            for (int d = 0; d < 2; ++d)
                if (coopbuild::BuildReconcileAct(1, f, 1 - f, m, 1 - m, d) != coopbuild::kBuildReconListed) Fail("p87: a key the owner lists was touched");
    if (coopbuild::BuildReconcileAct(0, 0, 1, 0, 0, 0) != coopbuild::kBuildReconPark) Fail("p87: a complete empty search was not parked");
    if (coopbuild::BuildReconcileAct(0, 0, 0, 0, 0, 0) != coopbuild::kBuildReconRetry) Fail("p87: an incomplete search was not retried");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 0, 1, 1) != coopbuild::kBuildReconRetry) Fail("p87: a piece already dismantling was removed again");
    /* the owner's tombstones: once per key, a repeat is the newest, the oldest leave past the cap, a key placed again ends */
    std::vector<std::string> t, ev;
    if (coopbuild::BuildTombAdd(&t, "a", 3, &ev) != 1) Fail("p87: a dismantle was not a tombstone");
    if (coopbuild::BuildTombAdd(&t, "a", 3, &ev) != 0 || t.size() != 1) Fail("p87: a key was a tombstone twice");
    coopbuild::BuildTombAdd(&t, "b", 3, &ev); coopbuild::BuildTombAdd(&t, "c", 3, &ev);
    if (!ev.empty() || t.size() != 3) Fail("p87: a tombstone left below the cap");
    if (coopbuild::BuildTombAdd(&t, "d", 3, &ev) != 1 || ev.size() != 1 || ev[0] != "a" || t.size() != 3 || t[0] != "b" || t[2] != "d") Fail("p87: the cap did not drop the oldest tombstone");
    if (coopbuild::BuildTombAdd(&t, "b", 3, &ev) != 0 || t.size() != 3 || t[0] != "c" || t[2] != "b" || ev.size() != 1) Fail("p87: a repeat did not become the newest");
    if (coopbuild::BuildOweRemoveDrop(&t, "d") != 1 || t.size() != 2 || t[0] != "c" || t[1] != "b") Fail("p87: a key placed again stayed a tombstone");
    if (coopbuild::BuildTombAdd(&t, "e", 0, 0) != 1 || t.size() != 3) Fail("p87: cap 0 (none) dropped a tombstone");
    /* the pp.build row keys: a tombstone row is never a bare key's row nor a gone marker's, and fits the record's key limit */
    const std::string k("P7n:12.5,3.0,-40.25");
    const std::string rk = coopbuild::BuildTombRowKey(k);
    if (!coopbuild::BuildTombRowOf(rk, k)) Fail("p87: a tombstone row did not read back");
    if (coopbuild::BuildTombRowOf(rk, "P7n:12.5,3.0,-40.26")) Fail("p87: a tombstone row read back for another key");
    if (coopbuild::BuildTombRowOf(k, k)) Fail("p87: a bare key read as a tombstone row");
    unsigned int gn = 0;
    if (coopbuild::BuildGoneRowOf(rk, k, &gn)) Fail("p87: a tombstone row read as a gone marker");
    if (coopbuild::BuildTombRowOf(coopbuild::BuildGoneRowKey(k, 5u), k)) Fail("p87: a gone marker read as a tombstone row");
    const std::string lk(100, 'x');
    if (coopbuild::BuildTombRowKey(lk).size() > coopown::kBuildRecMaxKey || !coopbuild::BuildTombRowOf(coopbuild::BuildTombRowKey(lk), lk)) Fail("p87: a long key's tombstone row does not fit the record");
}

/* P87 fold 3 (T651): the one owner rule the PLACE adopt, the reconcile and the REMOVE apply share */
void t_p87f3_owner_rule()
{
    if (coopbuild::BuildOwnerClass(1, 0, 0, 0, -1, 0) != coopbuild::kBuildLiveOwnNone) Fail("p87f3: no owner was not 'none'");
    if (coopbuild::BuildOwnerClass(0, 1, 0, 0, -1, 0) != coopbuild::kBuildLiveOwnMine) Fail("p87f3: this game's own piece was not 'mine'");
    if (coopbuild::BuildOwnerClass(0, 1, 1, 1, 0, 0) != coopbuild::kBuildLiveOwnMine) Fail("p87f3: 'mine' did not win over every other answer");
    if (coopbuild::BuildOwnerClass(0, 0, 1, 1, -1, 0) != coopbuild::kBuildLiveOwnSender) Fail("p87f3: the sender's live stand-in was not the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, 0, 0) != coopbuild::kBuildLiveOwnSenderRecord) Fail("p87f3: a save's coop-p<sender slot> (T651) was not the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, 2, 0) != coopbuild::kBuildLiveOwnOtherStandIn) Fail("p87f3: another slot's stand-in record was taken as the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 1, -1, 0) != coopbuild::kBuildLiveOwnOtherStandIn) Fail("p87f3: another live stand-in was taken as the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -2, 0) != coopbuild::kBuildLiveOwnOtherStandIn) Fail("p87f3: a protocol-67 coop-peer was taken as the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, 0) != coopbuild::kBuildLiveOwnThird) Fail("p87f3: a third party was not 'other'");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -3, 0) != coopbuild::kBuildLiveOwnThird) Fail("p87f3: an unreadable record was not 'other'");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, 0, -1) != coopbuild::kBuildLiveOwnOtherStandIn) Fail("p87f3: with the sender's slot unknown a record was taken as the sender's");
    for (int c = coopbuild::kBuildLiveOwnNone; c <= coopbuild::kBuildLiveOwnThird; ++c)
    {
        const int want = (c == coopbuild::kBuildLiveOwnSender || c == coopbuild::kBuildLiveOwnSenderRecord) ? 1 : 0;
        if (coopbuild::BuildOwnerIsSender(c) != want) Fail("p87f3: BuildOwnerIsSender disagrees with the classes");
        if (coopbuild::BuildOwnerWord(c) == 0) Fail("p87f3: a class has no word");
        /* the reconcile on a found piece, no row, not dismantling: remove exactly the sender's, keep the rest */
        const int act = coopbuild::BuildReconcileAct(0, 1, 0, c == coopbuild::kBuildLiveOwnMine ? 1 : 0, coopbuild::BuildOwnerIsSender(c), 0);
        const int wantAct = (c == coopbuild::kBuildLiveOwnMine) ? coopbuild::kBuildReconKeepOwn : (want != 0 ? coopbuild::kBuildReconRemove : coopbuild::kBuildReconKeepOther);
        if (act != wantAct) Fail("p87f3: the reconcile's verdict disagrees with the owner rule");
    }
}
/* P87 root (T661): the receiver's copy list - its pp.build '~c' row, and the owner rule's first test */
/* P87 rf1: the removed mark, the empty-load count and the alias flag ride the '~c' row's STATE; eviction; the mark's retirement; the cap */
void t_p87rf1_copy_marks()
{
    coopbuild::BuildCopyRec c;
    c.key = "1459-gamedata.base@20,32@-509414,29315,30669"; c.sid = "1459-gamedata.base"; c.slot = 0; c.nonce = 0x6dd5d6c3u;
    c.pos[0] = -50941.4f; c.pos[1] = 2931.5f; c.pos[2] = 3066.9f;
    std::string rk;
    std::vector<char> pb, sb;
    coopbuild::BuildCopyRec back;
    if (!coopbuild::BuildCopyRowMake(c, &rk, &pb, &sb) || !coopbuild::BuildCopyRowRead(rk, std::string(pb.begin(), pb.end()), std::string(sb.begin(), sb.end()), &back)
        || back.removed != 0 || back.emptyLoads != 0 || back.alias != 0 || !coopbuild::BuildCopyRecSame(back, c)) Fail("p87rf1: a live copy row does not round-trip as live");
    coopbuild::BuildMsg est;   /* a live entry still writes exactly the pre-rf1 STATE: an empty STATE of the key */
    est.kind = coopbuild::kBuildState; est.key = c.key;
    std::vector<char> eb;
    if (!coopbuild::EncodeBuild(&eb, est) || eb != sb) Fail("p87rf1: a live entry's STATE is not the pre-rf1 empty STATE");
    coopbuild::BuildCopyRec m = c;
    m.removed = 1; m.emptyLoads = 2; m.alias = 1;
    std::string mrk;
    std::vector<char> mpb, msb;
    if (!coopbuild::BuildCopyRowMake(m, &mrk, &mpb, &msb) || mrk != rk
        || !coopbuild::BuildCopyRowRead(mrk, std::string(mpb.begin(), mpb.end()), std::string(msb.begin(), msb.end()), &back)
        || back.removed != 1 || back.emptyLoads != 2 || back.alias != 1 || !coopbuild::BuildCopyRecSame(back, m)) Fail("p87rf1: the removed mark / empty loads / alias did not round-trip");
    if (coopbuild::BuildCopyRecSame(m, c)) Fail("p87rf1: a marked entry compares the same as a live one");
    if (coopbuild::BuildCopyEvict(0) != 0 || coopbuild::BuildCopyEvict(coopbuild::kBuildCopyEmptyLoads - 1) != 0 || coopbuild::BuildCopyEvict(coopbuild::kBuildCopyEmptyLoads) == 0)
        Fail("p87rf1: the eviction threshold is not kBuildCopyEmptyLoads");
    if (coopbuild::BuildCopyMarkRetires(1, 1, 5ul, 5ul) != 0) Fail("p87rf1: a save asked before the removal retired the mark");
    if (coopbuild::BuildCopyMarkRetires(1, 1, 5ul, 6ul) == 0) Fail("p87rf1: a save asked after the removal did not retire the mark");
    if (coopbuild::BuildCopyMarkRetires(1, 0, 5ul, 9ul) != 0) Fail("p87rf1: an unsettled read-back mark retired");
    if (coopbuild::BuildCopyMarkRetires(0, 1, 5ul, 9ul) != 0) Fail("p87rf1: a live entry retired as a removed mark");
    /* the list's ceiling and a list far past 1,024 entries: p87_copy_list_ceiling */
}
/* P87: the copy list's ceiling - the worst-case '~c' row and a full list fit the store's record read limit with room for own rows; a list of
   3,000 copies with an own row is written as pp.build's merge, read back entry for entry, and a later write retires some of them; the
   near-key run the list lookup asks */
void t_p87_copy_list_ceiling()
{
    /* the worst-case row: wire-cap key, sid and host key, every STATE fact set */
    coopbuild::BuildCopyRec w;
    w.key = std::string(coopbuild::kBuildMaxKey, 'w'); w.sid = std::string(coopbuild::kBuildMaxSid, 's'); w.slot = 254; w.nonce = 0xffffffffu;
    w.hostKey = std::string(coopbuild::kBuildMaxKey, 'h'); w.hostForm = 1; w.floor = coopbuild::kBuildMaxFloor; w.outside = 1;
    w.removed = 1; w.alias = 1; w.emptyLoads = 255;
    std::string wrk;
    std::vector<char> wpb, wsb;
    if (!coopbuild::BuildCopyRowMake(w, &wrk, &wpb, &wsb)) { Fail("p87cl: the worst-case copy row did not encode"); return; }
    std::vector<coopown::BuildRow> one, none;
    { coopown::BuildRow r; r.key = wrk; r.place.assign(wpb.begin(), wpb.end()); r.state.assign(wsb.begin(), wsb.end()); one.push_back(r); }
    std::vector<char> rec0, rec1;
    coopown::EncodeBuildRec(none, &rec0);
    coopown::EncodeBuildRec(one, &rec1);
    const size_t worst = rec1.size() - rec0.size();
    if (worst > coopbuild::kBuildCopyRowMaxBytes) Fail("p87cl: a '~c' row can be longer than kBuildCopyRowMaxBytes");
    if (coopbuild::kBuildCopyListCap <= 1024) Fail("p87cl: the ceiling is not past 1,024 entries");
    if (coopbuild::kBuildCopyListCap * coopbuild::kBuildCopyRowMaxBytes > coopbuild::kBuildStoreRecReadMax / 2) Fail("p87cl: a full list can take more than half the record read limit");
    /* 3,000 copies of three owners (removed marks, aliases, empty-load counts among them) and one own PLACE row */
    const size_t kN = 3000;
    std::vector<coopown::BuildRow> base, live, merged, dec;
    std::vector<coopbuild::BuildCopyRec> want;
    {
        coopbuild::BuildMsg om, os;
        om.kind = coopbuild::kBuildPlace; om.key = "77-gamedata.base@1,1@100,200,300"; om.sid = "77-gamedata.base"; om.ownerSlot = coopbuild::kBuildOwnerSender;
        os.kind = coopbuild::kBuildState; os.key = om.key;
        std::vector<char> ob, osb;
        if (!coopbuild::EncodeBuild(&ob, om) || !coopbuild::EncodeBuild(&osb, os)) { Fail("p87cl: the own row did not encode"); return; }
        coopown::BuildRow r; r.key = om.key; r.place.assign(ob.begin(), ob.end()); r.state.assign(osb.begin(), osb.end());
        base.push_back(r);
    }
    for (size_t i = 0; i < kN; ++i)
    {
        coopbuild::BuildCopyRec f;
        char k[64], sd[32];
        std::sprintf(sd, "%u-gamedata.base", 1000u + (unsigned int)(i % 7));
        std::sprintf(k, "%s@%d,3@%d,%d,50", sd, (int)(i / 100), (int)(i * 10), -(int)i);
        f.key = k; f.sid = sd; f.slot = (int)(i % 3); f.nonce = 0x1000u + (unsigned int)i;
        f.pos[0] = (float)i; f.pos[1] = 5.0f; f.pos[2] = -(float)i;
        f.removed = (i % 5 == 0) ? 1 : 0; f.alias = (i % 11 == 0) ? 1 : 0; f.emptyLoads = (int)(i % 3);
        std::string frk;
        std::vector<char> fpb, fsb;
        if (!coopbuild::BuildCopyRowMake(f, &frk, &fpb, &fsb)) { Fail("p87cl: a copy row did not encode"); return; }
        coopown::BuildRow r; r.key = frk; r.place.assign(fpb.begin(), fpb.end()); r.state.assign(fsb.begin(), fsb.end());
        live.push_back(r);
        want.push_back(f);
    }
    int dropped = 0;
    coopown::MergeBuildRows(base, live, std::set<std::string>(), &merged, &dropped);
    std::vector<char> rec;
    coopown::EncodeBuildRec(merged, &rec);
    if (rec.size() > coopbuild::kBuildStoreRecReadMax) Fail("p87cl: the record is past the read limit");
    if (!coopown::DecodeBuildRec(rec, &dec) || dec.size() != kN + 1) { Fail("p87cl: 3,000 copies and an own row did not round-trip through pp.build"); return; }
    std::map<std::string, coopbuild::BuildCopyRec> got;
    size_t ownBack = 0, bad = 0;
    for (size_t i = 0; i < dec.size(); ++i)
    {
        if (!coopbuild::BuildCopyRowIs(dec[i].key)) { ++ownBack; continue; }
        coopbuild::BuildCopyRec cr;
        if (coopbuild::BuildCopyRowRead(dec[i].key, dec[i].place, dec[i].state, &cr)) got[cr.key] = cr; else ++bad;
    }
    if (ownBack != 1 || bad != 0 || got.size() != kN) Fail("p87cl: the read back is not 3,000 copies and one own row");
    for (size_t i = 0; i < want.size(); ++i)
    {
        std::map<std::string, coopbuild::BuildCopyRec>::const_iterator g = got.find(want[i].key);
        if (g == got.end() || !coopbuild::BuildCopyRecSame(g->second, want[i])) { Fail("p87cl: a copy did not read back as written"); break; }
    }
    /* the next write: the first 10 copies retired, the rest written again over the read-back base */
    std::vector<coopown::BuildRow> live2(live.begin() + 10, live.end()), merged2, dec2;
    std::set<std::string> ret;
    for (size_t i = 0; i < 10; ++i) ret.insert(live[i].key);
    coopown::MergeBuildRows(dec, live2, ret, &merged2, &dropped);
    std::vector<char> rec2;
    coopown::EncodeBuildRec(merged2, &rec2);
    if (dropped != 10 || !coopown::DecodeBuildRec(rec2, &dec2) || dec2.size() != kN - 10 + 1) Fail("p87cl: retiring 10 copies of 3,000 did not leave 2,990 and the own row");
    /* the near-key run */
    std::string pre;
    if (!coopbuild::BuildKeyNearPrefix("1459-gamedata.base@20,32@-509414,29315,30669", &pre) || pre != "1459-gamedata.base@20,32@") Fail("p87cl: the near prefix is not sid + sector");
    if (!coopbuild::BuildKeyNearPrefix("a@b@1,2@3,4", &pre) || pre != "a@b@1,2@") Fail("p87cl: a sid holding an '@' lost it from the near prefix");
    if (coopbuild::BuildKeyNearPrefix("", &pre) || coopbuild::BuildKeyNearPrefix("abc", &pre) || coopbuild::BuildKeyNearPrefix("x@1,2", &pre)
        || coopbuild::BuildKeyNearPrefix("@1,2@3,4", &pre)) Fail("p87cl: a key with no sid before two '@' gave a near prefix");
    std::map<std::string, int> run;
    run["5-r@1,1@100,200"] = 1; run["5-r@1,1@101,201,7"] = 1; run["5-r@1,10@100,200"] = 0; run["5-r@11,1@100,200"] = 0; run["5-rx@1,1@100,200"] = 0; run["5-r@1,1"] = 0;
    if (!coopbuild::BuildKeyNearPrefix("5-r@1,1@100,201", &pre)) Fail("p87cl: no near prefix for a well-formed key");
    int inRun = 0, wrong = 0;
    for (std::map<std::string, int>::const_iterator it = run.lower_bound(pre); it != run.end() && it->first.compare(0, pre.size(), pre) == 0; ++it)
    { ++inRun; if (it->second != 1) ++wrong; }
    if (inRun != 2 || wrong != 0) Fail("p87cl: the sorted run is not exactly the same-sid, same-sector keys");
    std::printf("p87cl: worst '~c' row %u bytes (bound %u); ceiling %u entries (at most %u bytes); %u copies + 1 own row = %u bytes of pp.build\n",
                (unsigned int)worst, (unsigned int)coopbuild::kBuildCopyRowMaxBytes, (unsigned int)coopbuild::kBuildCopyListCap,
                (unsigned int)(coopbuild::kBuildCopyListCap * coopbuild::kBuildCopyRowMaxBytes), (unsigned int)kN, (unsigned int)rec.size());
}
void t_p87root_copy_list()
{
    coopbuild::BuildCopyRec c;
    c.key = "1459-gamedata.base@20,32@-509416,29399,30655"; c.sid = "1459-gamedata.base"; c.slot = 0; c.nonce = 0x1234abcdu;
    c.pos[0] = -50941.6f; c.pos[1] = 2939.9f; c.pos[2] = 3065.5f;
    c.rot[0] = 0.7071f; c.rot[1] = 0.0f; c.rot[2] = 0.7071f; c.rot[3] = 0.0f;
    std::string rk;
    std::vector<char> pb, sb;
    if (!coopbuild::BuildCopyRowMake(c, &rk, &pb, &sb)) Fail("p87root: a copy row did not encode");
    if (rk != coopbuild::BuildCopyRowKey(c.key) || rk.size() > coopown::kBuildRecMaxKey || rk.substr(0, 2) != "~c") Fail("p87root: the copy row key is not '~c' + hash + key within the record's key cap");
    if (!coopbuild::BuildCopyRowIs(rk) || !coopbuild::BuildCopyRowOf(rk, c.key)) Fail("p87root: a copy row key does not read as its key's");
    if (coopbuild::BuildTombRowOf(rk, c.key) || coopbuild::BuildCopyRowOf(coopbuild::BuildTombRowKey(c.key), c.key) || coopbuild::BuildCopyRowIs(c.key))
        Fail("p87root: a copy row is confused with a tombstone row or a bare key");
    unsigned int gn = 0;
    if (coopbuild::BuildGoneRowOf(rk, c.key, &gn) || coopbuild::BuildCopyRowIs(coopbuild::BuildGoneRowKey(c.key, 5u))) Fail("p87root: a copy row is confused with a gone marker");
    coopbuild::BuildCopyRec back;
    const std::string ps(pb.begin(), pb.end()), ss(sb.begin(), sb.end());
    if (!coopbuild::BuildCopyRowRead(rk, ps, ss, &back) || !coopbuild::BuildCopyRecSame(back, c)) Fail("p87root: a copy row did not read back as written");
    /* furniture: its host, form, floor, outside ride the row */
    coopbuild::BuildCopyRec fc = c;
    fc.key = "2021-gamedata.base@20,32@-509816,29799,30596"; fc.sid = "2021-gamedata.base"; fc.slot = 3; fc.nonce = 0;
    fc.hostKey = "900-gamedata.base@20,32@-509000,29000,30000"; fc.hostForm = 1; fc.floor = 2; fc.outside = 1;
    std::string frk;
    std::vector<char> fpb, fsb;
    coopbuild::BuildCopyRec fback;
    if (!coopbuild::BuildCopyRowMake(fc, &frk, &fpb, &fsb) || !coopbuild::BuildCopyRowRead(frk, std::string(fpb.begin(), fpb.end()), std::string(fsb.begin(), fsb.end()), &fback)
        || !coopbuild::BuildCopyRecSame(fback, fc)) Fail("p87root: a furniture copy row did not round-trip");
    /* refusals: no owner slot / the sender marker / no sid; another key's row key; another key's STATE; an own PLACE under a copy row key */
    coopbuild::BuildCopyRec bad = c;
    std::string brk;
    std::vector<char> bpb, bsb;
    bad.slot = -1; if (coopbuild::BuildCopyRowMake(bad, &brk, &bpb, &bsb)) Fail("p87root: a copy with no owner slot was encoded");
    bad.slot = 1024; if (coopbuild::BuildCopyRowMake(bad, &brk, &bpb, &bsb)) Fail("p87root: a copy whose slot is past the notebook's slots was encoded");
    bad = c; bad.sid.clear(); if (coopbuild::BuildCopyRowMake(bad, &brk, &bpb, &bsb)) Fail("p87root: a copy with no sid was encoded");
    if (coopbuild::BuildCopyRowRead(frk, ps, ss, &back)) Fail("p87root: a row read under another key's row key");
    if (coopbuild::BuildCopyRowRead(rk, ps, std::string(fsb.begin(), fsb.end()), &back)) Fail("p87root: a row with another key's STATE was read");
    coopbuild::BuildMsg own;
    own.kind = coopbuild::kBuildPlace; own.key = c.key; own.sid = c.sid;   /* ownerSlot 0xFF: an own row's PLACE */
    std::vector<char> ob;
    if (!coopbuild::EncodeBuild(&ob, own)) Fail("p87root: the own PLACE did not encode");
    if (coopbuild::BuildCopyRowRead(rk, std::string(ob.begin(), ob.end()), ss, &back)) Fail("p87root: an own PLACE (slot 0xFF) was read as a copy row");
    if (coopbuild::BuildCopyRowRead(rk, std::string(), ss, &back)) Fail("p87root: an empty PLACE was read as a copy row");
    /* a key at the wire's cap still makes a row key within the record's cap, and round-trips */
    coopbuild::BuildCopyRec lc = c;
    lc.key = std::string(coopbuild::kBuildMaxKey, 'k');
    std::string lrk;
    std::vector<char> lpb, lsb;
    coopbuild::BuildCopyRec lback;
    if (!coopbuild::BuildCopyRowMake(lc, &lrk, &lpb, &lsb) || lrk.size() > coopown::kBuildRecMaxKey
        || !coopbuild::BuildCopyRowRead(lrk, std::string(lpb.begin(), lpb.end()), std::string(lsb.begin(), lsb.end()), &lback) || lback.key != lc.key)
        Fail("p87root: a long key's copy row does not fit the record or does not round-trip");
    /* pp.build: an own row, a tombstone and a copy row live side by side; retiring the copy row drops only it */
    std::vector<coopown::BuildRow> file, merged, dec;
    coopown::BuildRow r1; r1.key = "5-gamedata.base@1,1@1,1,1"; r1.place = std::string(ob.begin(), ob.end()); r1.state = ss; file.push_back(r1);
    coopown::BuildRow r2; r2.key = coopbuild::BuildTombRowKey("6-gamedata.base@1,1@2,2,2"); r2.place = "x"; r2.state = "y"; file.push_back(r2);
    coopown::BuildRow r3; r3.key = rk; r3.place = ps; r3.state = ss; file.push_back(r3);
    std::vector<char> rec;
    coopown::EncodeBuildRec(file, &rec);
    if (!coopown::DecodeBuildRec(rec, &dec) || dec.size() != 3) Fail("p87root: pp.build with a copy row did not decode");
    int seenCopy = 0;
    for (size_t i = 0; i < dec.size(); ++i)
        if (coopbuild::BuildCopyRowIs(dec[i].key)) { ++seenCopy; if (!coopbuild::BuildCopyRowRead(dec[i].key, dec[i].place, dec[i].state, &back) || !coopbuild::BuildCopyRecSame(back, c)) Fail("p87root: the copy row did not survive pp.build"); }
    if (seenCopy != 1) Fail("p87root: pp.build did not hold exactly the one copy row");
    std::set<std::string> ret;
    ret.insert(rk);
    int dropped = 0;
    coopown::MergeBuildRows(dec, std::vector<coopown::BuildRow>(), ret, &merged, &dropped);
    if (dropped != 1 || merged.size() != 2) Fail("p87root: retiring a copy row did not drop exactly it");
    /* THE OWNER RULE's first test: listed for the sender's slot with the same sid -> the sender's, whatever the owner pointer says */
    const int L = coopbuild::kBuildLiveOwnSenderList;
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, 0, 0, 1, 0) != L) Fail("p87root: T661 - a listed copy the load gave a town's faction was not the sender's");
    if (coopbuild::BuildOwnerClass(0, 1, 0, 0, -1, 0, 0, 1, 0) != L) Fail("p87root: a listed copy the load gave THIS game's faction (a player town) was not the sender's");
    if (coopbuild::BuildOwnerClass(1, 0, 0, 0, -1, 0, 0, 1, 0) != L) Fail("p87root: a listed copy with no owner was not the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 1, 1, -1, 0, 0, 1, 0) != L) Fail("p87root: the list was not asked first");
    if (coopbuild::BuildOwnerClass(0, 1, 0, 0, -1, 0, 0, 1, 1) != coopbuild::kBuildLiveOwnMine) Fail("p87root: this game's own piece at a listed key did not win");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, 0, 0, 1, 1) != coopbuild::kBuildLiveOwnThird) Fail("p87root: with an own row here the list still answered");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, 0, 0, 0, 0) != coopbuild::kBuildLiveOwnThird) Fail("p87root: a listed key whose sid differs was taken as the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, 0, 2, 1, 0) != coopbuild::kBuildLiveOwnThird) Fail("p87root: another slot's listed copy was taken as the sender's");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, -1, -1, 0, 1, 0) != coopbuild::kBuildLiveOwnThird) Fail("p87root: with the sender's slot unknown the list answered");
    if (coopbuild::BuildOwnerClass(0, 0, 0, 0, 0, 0, -1, 0, 0) != coopbuild::kBuildLiveOwnSenderRecord) Fail("p87root: fold 3's record test is not the fallback for a key not listed");
    if (coopbuild::BuildOwnerClass(0, 0, 1, 1, -1, 0) != coopbuild::kBuildLiveOwnSender) Fail("p87root: the six-input rule changed");
    if (coopbuild::BuildOwnerIsSender(L) != 1 || std::string(coopbuild::BuildOwnerWord(L)) != "copy-list") Fail("p87root: 'copy-list' is not a sender class with its word");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 0, coopbuild::BuildOwnerIsSender(L), 0) != coopbuild::kBuildReconRemove) Fail("p87root: the reconcile does not remove a listed copy");
    if (coopbuild::BuildReconcileAct(0, 1, 0, 0, coopbuild::BuildOwnerIsSender(L), 1) != coopbuild::kBuildReconRetry) Fail("p87root: the reconcile removed a listed copy mid-dismantle");
}
/* P14 (mmo8b): an own piece standing at a READ-BACK tombstone's key is removed (the records retired it after the save), not taken as
   live again; the removal's steps. Before P14 the only rule was BuildTombSendAct(readBack, verified, ownHere=1, 0) = drop. */
void t_p14_tombstone_kill()
{
    if (coopbuild::BuildTombSendAct(1, 0, 1, 0) != coopbuild::kBuildTombDrop) Fail("p14: the P87 send rule changed (an own row still ends a tombstone there)");
    if (coopbuild::BuildTombSeenAct(1, 1, 0, 0) != coopbuild::kBuildTombKill) Fail("p14: an own piece at a read-back tombstone's key was taken as live (piece back + refund kept)");
    if (coopbuild::BuildTombSeenAct(0, 1, 0, 0) != coopbuild::kBuildTombDrop) Fail("p14: a piece standing at a tombstone of THIS world was removed (a new piece)");
    if (coopbuild::BuildTombSeenAct(1, 0, 0, 0) != coopbuild::kBuildTombDrop) Fail("p14: a row written before P87 fold 1 was acted on");
    if (coopbuild::BuildTombSeenAct(1, 1, 1, 0) != coopbuild::kBuildTombDrop) Fail("p14: a live own row at the key (a new placement) was removed");
    if (coopbuild::BuildTombSeenAct(1, 1, 0, 1) != coopbuild::kBuildTombDrop) Fail("p14: a waiting PLACE at the key was overridden");
    const int M = 10;   /* build.cpp kRemoveTries */
    /* (zoneLoaded, look, own, listedCopy, dismantling, tries, misses, maxTries, keyExact, rowNamed, interior) - P14 fold 1 */
    if (coopbuild::BuildTombKillStep(0, 1, 1, 0, 0, 0, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillWait) Fail("p14: acted while the zone is not loaded");
    if (coopbuild::BuildTombKillStep(0, 1, 1, 0, 0, 0, M + 5, M, 1, 0, 0) != coopbuild::kBuildTombKillWait) Fail("p14: an unloaded zone counted toward the give-up");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, 0, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillCall) Fail("p14: the save's own piece was not dismantled");
    if (coopbuild::BuildTombKillStep(1, 0, 0, 0, 0, 1, 0, M, 0, 0, 0) != coopbuild::kBuildTombKillDone) Fail("p14: a freed piece did not finish the removal");
    if (coopbuild::BuildTombKillStep(1, 0, 0, 0, 0, 0, 0, M, 0, 0, 0) != coopbuild::kBuildTombKillDone) Fail("p14: a key with nothing standing did not finish");
    if (coopbuild::BuildTombKillStep(1, 1, 0, 0, 0, 0, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillCancel) Fail("p14: another owner's piece was dismantled");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 1, 0, 0, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillCancel) Fail("p14: a listed copy (another game's) was dismantled as own");
    if (coopbuild::BuildTombKillStep(1, 2, 0, 0, 0, 0, 0, M, 0, 0, 0) != coopbuild::kBuildTombKillMiss) Fail("p14: an incomplete lookup was not retried");
    if (coopbuild::BuildTombKillStep(1, 2, 0, 0, 0, 0, M - 1, M, 0, 0, 0) != coopbuild::kBuildTombKillGiveUp) Fail("p14: incomplete lookups never gave up");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, M, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillGiveUp) Fail("p14: dismantle calls never gave up");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, M - 1, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillCall) Fail("p14: the last allowed call was not made");
    /* F3 (deferred flag): our call took - waited (never a miss, never a second call); dismantling before any call of ours - a miss */
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 1, 1, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillWait) Fail("p14f1: a piece our call dismantled counted as a miss (deferred wall)");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 1, 1, M + 5, M, 1, 0, 0) != coopbuild::kBuildTombKillWait) Fail("p14f1: a deferred wall gave up");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 1, 0, 0, M, 1, 0, 0) != coopbuild::kBuildTombKillMiss) Fail("p14f1: a piece dismantling before our call was not a miss");
    /* F2 (key mismatch): the found piece's own key is not the tombstone's, or a registry row names it - never touched */
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, 0, 0, M, 0, 0, 0) != coopbuild::kBuildTombKillMismatch) Fail("p14f1: a piece within the window (another key) was removed");
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, 0, 0, M, 1, 1, 0) != coopbuild::kBuildTombKillMismatch) Fail("p14f1: a piece a registry row names was removed");
    /* F4: a building with an interior is left standing */
    if (coopbuild::BuildTombKillStep(1, 1, 1, 0, 0, 0, 0, M, 1, 0, 1) != coopbuild::kBuildTombKillExclude) Fail("p14f1: a building with an interior was dismantled");
}
/* P14 fold 1: the escape ledger - refund items, pickups (partial ones add up to at most the refund), the reload count, the subtraction */
void t_p14f1_ledger()
{
    std::string sid;
    if (!coopbuild::BuildGroundKeySid("G|Iron plates@3,-2@10,20,30", &sid) || sid != "Iron plates") Fail("p14f1: the ground key's sid was not read");
    if (coopbuild::BuildGroundKeySid("B|x@1,2@3,4,5", &sid) || coopbuild::BuildGroundKeySid("G|@1,2", &sid)) Fail("p14f1: a non-ground key read as one");
    coopbuild::BuildLedger L;
    const std::string k1 = "G|plate@3,-2@10,20,30", k2 = "G|plate@3,-2@11,20,30", k3 = "G|cloth@3,-2@12,20,30";
    coopbuild::BuildLedgerAddRefund(&L, "plate", k1, 3, 0);
    coopbuild::BuildLedgerAddRefund(&L, "plate", k2, 2, 0);
    coopbuild::BuildLedgerAddRefund(&L, "cloth", k3, 4, 0);
    coopbuild::BuildLedgerAddRefund(&L, "plate", k1, 1, 0);
    if (L.refund.size() != 3 || L.refund[0].qty != 4) Fail("p14f1: two refund items on one key were not merged");
    if (coopbuild::BuildLedgerOwed(L, "plate") != 6 || coopbuild::BuildLedgerOwed(L, "cloth") != 4 || coopbuild::BuildLedgerOwed(L, "rope") != -1) Fail("p14f1: the owed refund per material is wrong");
    int full = 0;
    if (coopbuild::BuildLedgerAddEsc(&L, k1, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 10, &full) != 1) Fail("p14f1: a partial pickup was not recorded");
    if (coopbuild::BuildLedgerAddEsc(&L, k1, 2, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 12, &full) != 2) Fail("p14f1: a second partial pickup was not recorded");
    if (coopbuild::BuildLedgerAddEsc(&L, k1, 5, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 14, &full) != 1) Fail("p14f1: partial pickups added up past the refund");
    if (coopbuild::BuildLedgerAddEsc(&L, k1, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 15, &full) != 0) Fail("p14f1: a used-up refund key took a pickup");
    if (coopbuild::BuildLedgerAddEsc(&L, k3, 2, coopbuild::kBuildLedgerEscPeer, "", 0, &full) != 2) Fail("p14f1: the other player's pickup was not recorded");
    if (coopbuild::BuildLedgerAddEsc(&L, "G|plate@9,9@1,1,1", 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 16, &full) != 0) Fail("p14f1: a non-refund item was recorded");
    if (coopbuild::BuildLedgerEscCounts(L.esc[0], 12) != 1 || coopbuild::BuildLedgerEscCounts(L.esc[1], 11) != 0 || coopbuild::BuildLedgerEscCounts(L.esc[0], 0) != 0)
        Fail("p14f1: an own pickup counted without its squad record applied at its seq");
    if (coopbuild::BuildLedgerEscCounts(L.esc[3], 0) != 1) Fail("p14f1: the other player's pickup did not count");
    std::vector<int> ok;
    for (size_t i = 0; i < L.esc.size(); ++i) ok.push_back(coopbuild::BuildLedgerEscCounts(L.esc[i], L.esc[i].kind == coopbuild::kBuildLedgerEscPeer ? 0 : 12));
    if (coopbuild::BuildLedgerEscaped(L, ok, "plate") != 3 || coopbuild::BuildLedgerEscaped(L, ok, "cloth") != 2) Fail("p14f1: the counted escapes are wrong (seq 14 applied only to 12)");
    if (coopbuild::BuildLedgerKeep(10.0f, 6, 3) != 3.0f) Fail("p14f1: the re-drop was not capped at the owed rest");
    if (coopbuild::BuildLedgerKeep(2.0f, 6, 3) != 2.0f) Fail("p14f1: a slot kept more than was delivered");
    if (coopbuild::BuildLedgerKeep(10.0f, 6, 6) != 0.0f || coopbuild::BuildLedgerKeep(10.0f, 6, 9) != 0.0f) Fail("p14f1: everything escaped but the refund still drops");
    if (coopbuild::BuildLedgerKeep(10.0f, -1, 3) != 0.0f || coopbuild::BuildLedgerKeep(10.0f, -1, 0) != 0.0f) Fail("p14f1: an unknown refund was re-dropped (P14 fold 2 D1: refused)");
    coopbuild::BuildLedgerAddRefund(&L, "plate", "G|plate@3,-2@50,20,30", 3, 1);
    if (coopbuild::BuildLedgerOwed(L, "plate") != 6) Fail("p14f1: a re-drop was owed again");
    L.missed = 2; L.spill = 1;
    std::string enc;
    coopbuild::BuildLedger D;
    if (!coopbuild::BuildLedgerEncode(L, &enc) || !coopbuild::BuildLedgerDecode(enc, &D)) Fail("p14f1: the ledger did not round-trip");
    if (D.refund.size() != 4 || D.esc.size() != 4 || D.refund[0].gkey != k1 || D.refund[0].sid != "plate" || D.refund[0].qty != 4 || D.refund[3].gen != 1
        || D.esc[0].rec != "pp.squad.a" || D.esc[0].seq != 10 || D.esc[3].kind != coopbuild::kBuildLedgerEscPeer || !D.esc[3].rec.empty() || D.missed != 2 || D.spill != 1)
        Fail("p14f1: the decoded ledger differs");
    if (coopbuild::BuildLedgerDecode(std::string(), &D) || coopbuild::BuildLedgerDecode(enc.substr(0, enc.size() - 1), &D) || coopbuild::BuildLedgerDecode("LG0" + enc.substr(3), &D))
        Fail("p14f1: a torn or foreign STATE read as a ledger");
    coopbuild::BuildLedger F;
    coopbuild::BuildLedgerAddRefund(&F, "plate", k1, 100, 0);
    for (int i = 0; i < 48; ++i) coopbuild::BuildLedgerAddEsc(&F, k1, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 20, &full);
    if (coopbuild::BuildLedgerAddEsc(&F, k1, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 21, &full) != 1 || full != 2 || F.esc.size() != 48 || F.esc[0].seq != 20)
        Fail("p14f1: a full ledger did not merge the pickup into an entry of its record, keeping the earlier seq (P14 fold 2 D6, fold 3)");
    if (!coopbuild::BuildLedgerEncode(F, &enc)) Fail("p14f1: a full ledger did not fit its row");
}
/* P14 fold 2: the fold-1 re-check's money defects D1-D4, D6 (each failed before the fold) */
void t_p14f2_ledger()
{
    int owed = 0, esc = 0, refused = 0, full = 0;
    std::vector<int> none;
    /* D1: a long material's hashed ground-key sid names the slot's sid */
    const std::string longSid(120, 'x');
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < longSid.size(); ++i) { h ^= (unsigned int)(unsigned char)longSid[i]; h *= 16777619u; }
    std::string hashed = "#";
    for (int i = 7; i >= 0; --i) hashed.push_back("0123456789abcdef"[(h >> (i * 4)) & 0xFu]);
    coopbuild::BuildLedger A;
    coopbuild::BuildLedgerAddRefund(&A, hashed, "G|" + hashed + "@3,-2@10,20,30", 4, 0);
    if (coopbuild::BuildLedgerOwed(A, longSid) != 4) Fail("p14f2 D1: a hashed material's first drop was not found by the slot's sid");
    if (coopbuild::BuildLedgerKeepOf(A, none, longSid, 10.0f, &owed, &esc, &refused) != 4.0f || refused != 0) Fail("p14f2 D1: a hashed material was not capped at its owed rest");
    /* D1: a material nothing names is refused, never re-dropped in full */
    if (coopbuild::BuildLedgerKeepOf(A, none, "rope", 10.0f, &owed, &esc, &refused) != 0.0f || refused != 1 || owed != -1) Fail("p14f2 D1: a material the ledger never saw was re-dropped");
    coopbuild::BuildLedger E;
    if (coopbuild::BuildLedgerKeepOf(E, none, "plate", 5.0f, &owed, &esc, &refused) != 0.0f) Fail("p14f2 D1: a tombstone with no ledger (before fold 1) re-dropped its mats");
    /* D3: the 17th refund entry is keyed (the cap was 16) */
    coopbuild::BuildLedger B;
    for (int i = 0; i < 17; ++i)
    {
        char k[64];
        _snprintf_s(k, sizeof(k), _TRUNCATE, "G|plate@3,-2@%d,20,30", 100 + i);
        if (coopbuild::BuildLedgerAddRefund(&B, "plate", k, 1, 0) != 1) Fail("p14f2 D3: a refund entry past 16 was not keyed");
    }
    if (B.refund.size() != 17 || coopbuild::BuildLedgerOwed(B, "plate") != 17 || B.missed != 0) Fail("p14f2 D3: the re-drop was cut to 16 entries");
    /* D1/D3: an item with no room for its key is owed by material; its pickups are recorded by material and subtracted */
    const std::string big = "G|cloth@3,-2@" + std::string(200, '1') + ",1,1";
    if (coopbuild::BuildLedgerAddRefund(&B, "cloth", big, 3, 0) != 2 || coopbuild::BuildLedgerOwed(B, "cloth") != 3) Fail("p14f2 D3: an unkeyed first drop was not owed by material");
    if (coopbuild::BuildLedgerAddEscMat(&B, "cloth", 2, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 30, &full) != 2) Fail("p14f2 D1: a pickup of an unkeyed material was not recorded");
    if (coopbuild::BuildLedgerAddEscMat(&B, "cloth", 5, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 31, &full) != 1) Fail("p14f2 D1: material pickups passed the unkeyed amount");
    if (coopbuild::BuildLedgerMatRoom(B, "cloth") != 0) Fail("p14f2 D1: the material's room was not used up");
    std::vector<int> all(B.esc.size(), 1);
    if (coopbuild::BuildLedgerEscaped(B, all, "cloth") != 3 || coopbuild::BuildLedgerKeepOf(B, all, "cloth", 5.0f, &owed, &esc, &refused) != 0.0f)
        Fail("p14f2 D1: the material pickups were not subtracted (everything picked, the refund dropped again)");
    std::string enc;
    coopbuild::BuildLedger D;
    if (!coopbuild::BuildLedgerEncode(B, &enc) || !coopbuild::BuildLedgerDecode(enc, &D)) Fail("p14f2: a ledger with material lines did not round-trip");
    if (D.mat.size() != 1 || D.mat[0].unkeyed != 3 || D.esc.size() != 2 || D.esc[0].ref != coopbuild::kBuildLedgerMatRef || coopbuild::BuildLedgerOwed(D, "cloth") != 3
        || coopbuild::BuildLedgerEscaped(D, all, "cloth") != 3) Fail("p14f2: the decoded material lines differ");
    coopbuild::BuildLedger G;
    coopbuild::BuildLedgerAddRefund(&G, "plate", "G|plate@3,-2@10,20,30", 2, 0);
    if (!coopbuild::BuildLedgerEncode(G, &enc) || !coopbuild::BuildLedgerDecode("LG1" + enc.substr(3, 8) + enc.substr(12, enc.size() - 13), &D) || coopbuild::BuildLedgerOwed(D, "plate") != 2)
        Fail("p14f2: a fold-1 ledger (LG1) was not read");
    /* D2: at the first look after a load, our pickups that did not count leave the ledger - a second reload never counts them */
    coopbuild::BuildLedger C;
    const std::string kc = "G|plate@3,-2@10,20,30";
    coopbuild::BuildLedgerAddRefund(&C, "plate", kc, 3, 0);
    coopbuild::BuildLedgerAddEsc(&C, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 10, &full);
    coopbuild::BuildLedgerAddEsc(&C, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 20, &full);
    coopbuild::BuildLedgerAddEsc(&C, kc, 1, coopbuild::kBuildLedgerEscPeer, "", 0, &full);
    std::vector<int> ok1;
    for (size_t i = 0; i < C.esc.size(); ++i) ok1.push_back(coopbuild::BuildLedgerEscCounts(C.esc[i], C.esc[i].kind == coopbuild::kBuildLedgerEscPeer ? 0 : 15));
    if (coopbuild::BuildLedgerPruneUncounted(&C, ok1) != 1 || C.esc.size() != 2) Fail("p14f2 D2: the own pickup that did not count stayed in the ledger");
    std::vector<int> ok2;
    for (size_t i = 0; i < C.esc.size(); ++i) ok2.push_back(coopbuild::BuildLedgerEscCounts(C.esc[i], C.esc[i].kind == coopbuild::kBuildLedgerEscPeer ? 0 : 25));
    if (coopbuild::BuildLedgerEscaped(C, ok2, "plate") != 2 || coopbuild::BuildLedgerKeepOf(C, ok2, "plate", 5.0f, &owed, &esc, &refused) != 1.0f)
        Fail("p14f2 D2: a second reload subtracted a pickup that never reached the squad");
    /* D4: once a call's refund fired, a retry zeroes the mats; D7: a piece left standing gets its mats back (0 once refunded) */
    if (coopbuild::BuildTombKillMats(0, 0) != coopbuild::kBuildTombMatsLedger || coopbuild::BuildTombKillMats(0, 1) != coopbuild::kBuildTombMatsZero
        || coopbuild::BuildTombKillMats(1, 1) != coopbuild::kBuildTombMatsRead) Fail("p14f2 D4: a retry after the refund fired did not zero the mats");
    if (coopbuild::BuildTombKillRestore(0, 2) != coopbuild::kBuildTombMatsZero || coopbuild::BuildTombKillRestore(1, 0) != coopbuild::kBuildTombMatsRead)
        Fail("p14f2 D7: a piece left standing after a refund kept its mats, or a wall run's shared state was changed");   /* P14 fold 3: no full restore */
    /* D6: a pickup into a 9th squad record with the entries full cannot be recorded - the material is BLIND and its re-drop refused */
    coopbuild::BuildLedger F;
    coopbuild::BuildLedgerAddRefund(&F, "plate", kc, 100, 0);
    for (int i = 0; i < 48; ++i)
    {
        char r[32];
        _snprintf_s(r, sizeof(r), _TRUNCATE, "pp.squad.%d", i % 8);
        coopbuild::BuildLedgerAddEsc(&F, kc, 1, coopbuild::kBuildLedgerEscOwn, r, 20, &full);
    }
    if (coopbuild::BuildLedgerAddEsc(&F, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.z", 21, &full) != 0 || full != 1 || coopbuild::BuildLedgerBlindOf(F, "plate") != 1)
        Fail("p14f2 D6: an unrecordable pickup left the material countable");
    std::vector<int> okf(F.esc.size(), 1);
    if (coopbuild::BuildLedgerKeepOf(F, okf, "plate", 5.0f, &owed, &esc, &refused) != 0.0f || refused != 1) Fail("p14f2 D6: a blind material was re-dropped");
    if (!coopbuild::BuildLedgerEncode(F, &enc) || !coopbuild::BuildLedgerDecode(enc, &D) || coopbuild::BuildLedgerBlindOf(D, "plate") != 1) Fail("p14f2 D6: the blind mark did not round-trip");
    /* D6: long keys never push the encoding past its 4000 bytes - the rest is owed by material */
    coopbuild::BuildLedger H;
    for (int i = 0; i < 64; ++i)
    {
        const std::string k = "G|" + std::string(150, 'y') + "@3,-2@" + std::string(20, '1') + ",1," + std::string(1, (char)('a' + (i % 26))) + std::string(1, (char)('a' + (i / 26)));
        coopbuild::BuildLedgerAddRefund(&H, std::string(150, 'y'), k, 1, 0);
    }
    if (!coopbuild::BuildLedgerEncode(H, &enc) || H.mat.size() != 1 || coopbuild::BuildLedgerOwed(H, std::string(150, 'y')) != 64)
        Fail("p14f2 D6: the ledger outgrew its row, or the rest was not owed by material");
}
void t_p14f3_ledger()
{
    int owed = 0, esc = 0, refused = 0, full = 0;
    std::vector<int> none;
    std::string enc;
    coopbuild::BuildLedger D;
    const std::string kc = "G|plate@3,-2@10,20,30";
    /* 1 (D6): a merged escape keeps the EARLIER seq - it counts once the earlier squad write landed (a loss of the later part, never a duplicate) */
    coopbuild::BuildLedger M;
    coopbuild::BuildLedgerAddRefund(&M, "plate", kc, 100, 0);
    for (int i = 0; i < 48; ++i)
    {
        char r[32];
        _snprintf_s(r, sizeof(r), _TRUNCATE, "pp.squad.%d", i % 8);
        coopbuild::BuildLedgerAddEsc(&M, kc, 1, coopbuild::kBuildLedgerEscOwn, r, 50, &full);
    }
    if (coopbuild::BuildLedgerAddEsc(&M, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.0", 60, &full) != 1 || full != 2 || M.esc[0].seq != 50 || M.esc[0].qty != 2)
        Fail("p14f3 1: a merged escape took the later seq");
    if (coopbuild::BuildLedgerAddEsc(&M, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.1", 40, &full) != 1 || full != 2 || M.esc[1].seq != 40)
        Fail("p14f3 1: a merged escape did not take the earlier seq");
    if (coopbuild::BuildLedgerEscCounts(M.esc[0], 50) != 1) Fail("p14f3 1: the merged escape did not count once the earlier write landed");
    /* 4 (N2): the refuse flag rides the fixed header and round-trips; an unknown flag value refuses */
    coopbuild::BuildLedger R;
    coopbuild::BuildLedgerAddRefund(&R, "plate", kc, 4, 0);
    if (!coopbuild::BuildLedgerEncode(R, &enc) || enc.compare(0, 3, "LG3") != 0 || enc.size() < 12 || enc[11] != 0 || !coopbuild::BuildLedgerDecode(enc, &D) || D.refuse != 0
        || coopbuild::BuildLedgerKeepOf(D, none, "plate", 10.0f, &owed, &esc, &refused) != 4.0f || refused != 0)
        Fail("p14f3 4: a ledger without the flag did not round-trip as countable");
    std::string unk = enc;
    unk[11] = (char)0x04;
    if (!coopbuild::BuildLedgerDecode(unk, &D) || D.refuse == 0 || coopbuild::BuildLedgerKeepOf(D, none, "plate", 10.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f3 4: an unknown flag value read as 0");
    R.refuse = 1;
    if (coopbuild::BuildLedgerEmpty(R)) Fail("p14f3 4: a refusing ledger counted as empty");
    if (!coopbuild::BuildLedgerEncode(R, &enc) || enc[11] != 1 || !coopbuild::BuildLedgerDecode(enc, &D) || D.refuse != 1 || D.refund.size() != 1
        || coopbuild::BuildLedgerKeepOf(D, none, "plate", 10.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f3 4: the refuse flag did not round-trip, or a material was re-dropped past it");
    coopbuild::BuildLedger rf;
    rf.refuse = 1;
    if (coopbuild::BuildLedgerEmpty(rf) || !coopbuild::BuildLedgerEncode(rf, &enc) || !coopbuild::BuildLedgerDecode(enc, &D) || D.refuse != 1)
        Fail("p14f3 2: the refuse-all ledger a failed encoding writes did not round-trip");
    /* 4 (N2): a blind mark that does not fit (16 material lines) sets the whole-ledger flag */
    coopbuild::BuildLedger N;
    for (int i = 0; i < 16; ++i)
    {
        char s[16];
        _snprintf_s(s, sizeof(s), _TRUNCATE, "m%d", i);
        coopbuild::BuildLedgerMat x; x.sid = s;
        N.mat.push_back(x);
    }
    if (coopbuild::BuildLedgerAddRefund(&N, "plate", kc, 100, 0) != 1) Fail("p14f3 4: the refund was not keyed");
    for (int i = 0; i < 48; ++i)
    {
        char r[32];
        _snprintf_s(r, sizeof(r), _TRUNCATE, "pp.squad.%d", i % 8);
        coopbuild::BuildLedgerAddEsc(&N, kc, 1, coopbuild::kBuildLedgerEscOwn, r, 20, &full);
    }
    if (coopbuild::BuildLedgerAddEsc(&N, kc, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.z", 21, &full) != 0 || full != 3 || N.refuse != 1)
        Fail("p14f3 4: a blind mark that did not fit left the ledger countable");
    std::vector<int> okn(N.esc.size(), 1);
    if (coopbuild::BuildLedgerKeepOf(N, okn, "plate", 5.0f, &owed, &esc, &refused) != 0.0f || refused != 1
        || coopbuild::BuildLedgerKeepOf(N, okn, "m3", 5.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f3 4: a refusing ledger re-dropped a material");
    if (!coopbuild::BuildLedgerEncode(N, &enc) || !coopbuild::BuildLedgerDecode(enc, &D) || D.refuse != 1) Fail("p14f3 4: the flag set by an unfit blind mark did not round-trip");
    /* 3 (D7): a piece left standing - 0 once a refund fired, else the amounts the removal set stay (never the full saved amounts) */
    if (coopbuild::BuildTombKillRestore(0, 0) != coopbuild::kBuildTombMatsRead || coopbuild::BuildTombKillRestore(0, 1) != coopbuild::kBuildTombMatsZero
        || coopbuild::BuildTombKillRestore(1, 0) != coopbuild::kBuildTombMatsRead || coopbuild::BuildTombKillRestore(1, 3) != coopbuild::kBuildTombMatsRead)
        Fail("p14f3 3: a piece left standing did not keep the ledger-reduced mats (or 0 once a refund fired)");
    /* 5 (N1): an unkeyed refund item that landed in another zone marks its material blind */
    coopbuild::BuildLedger S;
    const int ms = coopbuild::BuildLedgerMatAt(&S, "cloth");
    if (ms < 0) Fail("p14f3 5: no material line");
    else S.mat[(size_t)ms].unkeyed = 3;
    if (coopbuild::BuildLedgerSpillUnkeyed(&S, "cloth", 2, 0) != 0 || coopbuild::BuildLedgerSpillUnkeyed(&S, "cloth", 1, 1) != 0 || coopbuild::BuildLedgerBlindOf(S, "cloth") != 0)
        Fail("p14f3 5: a keyed or same-zone refund marked the material blind");
    if (coopbuild::BuildLedgerSpillUnkeyed(&S, "cloth", 2, 1) != 1 || coopbuild::BuildLedgerBlindOf(S, "cloth") != 1
        || coopbuild::BuildLedgerKeepOf(S, none, "cloth", 5.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f3 5: a spilled unkeyed refund left the material countable");
    if (coopbuild::BuildLedgerSpillUnkeyed(&N, "rope", 2, 1) != 2 || N.refuse != 1) Fail("p14f3 5: a spilled unkeyed refund with no line left the ledger countable");
}
void t_p14f4_folds()
{
    /* P14 fold 4 (e5): the zones count as the same only when SHOWN equal - an unparsed sector is another zone */
    if (coopbuild::BuildZoneShownSame(1, 3, 4, 1, 3, 4) != 1) Fail("p14f4 e5: equal parsed sectors were not the same zone");
    if (coopbuild::BuildZoneShownSame(1, 3, 4, 1, 3, 5) != 0 || coopbuild::BuildZoneShownSame(0, 3, 4, 1, 3, 4) != 0
        || coopbuild::BuildZoneShownSame(1, 3, 4, 0, 3, 4) != 0 || coopbuild::BuildZoneShownSame(0, 0, 0, 0, 0, 0) != 0)
        Fail("p14f4 e5: a different or unparsed sector counted as the same zone");
    int owed = 0, esc = 0, refused = 0;
    std::vector<int> none0;
    coopbuild::BuildLedger U;
    const int mu = coopbuild::BuildLedgerMatAt(&U, "cloth");
    if (mu < 0) Fail("p14f4 e5: no material line");
    else U.mat[(size_t)mu].unkeyed = 3;
    const int sameU = coopbuild::BuildZoneShownSame(0, 0, 0, 1, 0, 0);   /* the piece key's sector did not parse */
    if (coopbuild::BuildLedgerSpillUnkeyed(&U, "cloth", 2, sameU != 0 ? 0 : 1) != 1 || coopbuild::BuildLedgerBlindOf(U, "cloth") != 1
        || coopbuild::BuildLedgerKeepOf(U, none0, "cloth", 5.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f4 e5: an unkeyed refund whose sector did not parse left the material countable");
    /* P14 fold 4 (L2): two same-zone ledgers with room for one material - blind in both (one re-drop in full would be a duplicate) */
    coopbuild::BuildLedger A, B;
    const int ma = coopbuild::BuildLedgerMatAt(&A, "rope"), mb = coopbuild::BuildLedgerMatAt(&B, "rope");
    if (ma < 0 || mb < 0) Fail("p14f4 L2: no material line");
    else { A.mat[(size_t)ma].unkeyed = 2; B.mat[(size_t)mb].unkeyed = 2; }
    std::vector<coopbuild::BuildLedger*> cand;
    cand.push_back(&A); cand.push_back(&B);
    if (coopbuild::BuildLedgerBlindAll(cand, "rope") != 0 || coopbuild::BuildLedgerBlindOf(A, "rope") != 1 || coopbuild::BuildLedgerBlindOf(B, "rope") != 1)
        Fail("p14f4 L2: a material shared by two same-zone ledgers was not blind in both");
    if (coopbuild::BuildLedgerKeepOf(A, none0, "rope", 2.0f, &owed, &esc, &refused) != 0.0f || refused != 1
        || coopbuild::BuildLedgerKeepOf(B, none0, "rope", 2.0f, &owed, &esc, &refused) != 0.0f || refused != 1)
        Fail("p14f4 L2: a ledger of a shared material still re-dropped it");
    /* P14 fold 4 (L1): unreadable mats never reach the dismantle - retried, then given up after maxTries looks; a wall run is called */
    if (coopbuild::BuildTombMatsUnread(coopbuild::kBuildTombMatsLedger, 3, 0, 10) != coopbuild::kBuildTombUnreadCall
        || coopbuild::BuildTombMatsUnread(coopbuild::kBuildTombMatsZero, 0, 9, 10) != coopbuild::kBuildTombUnreadCall)
        Fail("p14f4 L1: read mats did not reach the call");
    if (coopbuild::BuildTombMatsUnread(coopbuild::kBuildTombMatsRead, -1, 9, 10) != coopbuild::kBuildTombUnreadCall)
        Fail("p14f4 L1: a wall run (its shared state never refunded) was held back");
    int unread = 0, calls = 0, step = coopbuild::kBuildTombUnreadRetry;
    while (step == coopbuild::kBuildTombUnreadRetry && unread < 50)
    {
        step = coopbuild::BuildTombMatsUnread(coopbuild::kBuildTombMatsLedger, -1, unread, 10);
        if (step == coopbuild::kBuildTombUnreadCall) ++calls;
        ++unread;
    }
    if (calls != 0 || step != coopbuild::kBuildTombUnreadGiveUp || unread != 10) Fail("p14f4 L1: unreadable ledger mats were dismantled, or the retries were not bounded at 10");
    if (coopbuild::BuildTombMatsUnread(coopbuild::kBuildTombMatsZero, -1, 0, 10) != coopbuild::kBuildTombUnreadRetry)
        Fail("p14f4 L1: unreadable mats after a refund fired (a retry zeroes them) were dismantled");
}
/* P14 fold 5 (T732): a refund item our PUT handed to the area's holder counts at reload; the decode accepts the kind */
void t_p14f5_holder()
{
    coopbuild::BuildLedger L;
    const std::string k = "G|580-gamedata.base@20,32@-509997,29352,15330";
    coopbuild::BuildLedgerAddRefund(&L, "580-gamedata.base", k, 1, 0);
    int full = 0;
    if (coopbuild::BuildLedgerAddEsc(&L, k, 1, coopbuild::kBuildLedgerEscHolder, "", 0, &full) != 1 || full != 0 || L.esc.size() != 1)
        Fail("p14f5: a refund item handed to the holder was not recorded");
    if (coopbuild::BuildLedgerEscCounts(L.esc[0], 0) != 1) Fail("p14f5: a holder escape did not count at reload (it names no squad record)");
    std::vector<int> ok(1, coopbuild::BuildLedgerEscCounts(L.esc[0], 0));
    int owed = 0, esc = 0, refused = 0;
    const float keep = coopbuild::BuildLedgerKeepOf(L, ok, "580-gamedata.base", 1.0f, &owed, &esc, &refused);
    if (keep != 0.0f || owed != 1 || esc != 1 || refused != 0) Fail("p14f5: the refund handed to the holder would drop again at reload");
    if (coopbuild::BuildLedgerPruneUncounted(&L, std::vector<int>(1, 0)) != 0 || L.esc.size() != 1) Fail("p14f5: a holder escape was pruned as an uncounted own pickup");
    std::string enc;
    coopbuild::BuildLedger D;
    if (!coopbuild::BuildLedgerEncode(L, &enc) || !coopbuild::BuildLedgerDecode(enc, &D)) Fail("p14f5: the decode refused a holder escape");
    if (D.esc.size() != 1 || D.esc[0].kind != coopbuild::kBuildLedgerEscHolder || D.esc[0].qty != 1 || !D.esc[0].rec.empty() || D.esc[0].ref != 0)
        Fail("p14f5: the decoded holder escape differs");
    D.esc[0].kind = (unsigned char)(coopbuild::kBuildLedgerEscHolder + 1);
    coopbuild::BuildLedger U;
    if (!coopbuild::BuildLedgerEncode(D, &enc) || coopbuild::BuildLedgerDecode(enc, &U) || !coopbuild::BuildLedgerEmpty(U))
        Fail("p14f5: an unknown escape kind read as a ledger (today: the whole ledger is refused, left empty)");
    coopbuild::BuildLedger M;
    coopbuild::BuildLedgerAddRefund(&M, "580-gamedata.base", k, 2, 0);
    coopbuild::BuildLedgerAddEsc(&M, k, 1, coopbuild::kBuildLedgerEscOwn, "pp.squad.a", 30, &full);
    coopbuild::BuildLedgerAddEsc(&M, k, 1, coopbuild::kBuildLedgerEscHolder, "", 0, &full);
    std::vector<int> okm;
    for (size_t i = 0; i < M.esc.size(); ++i) okm.push_back(coopbuild::BuildLedgerEscCounts(M.esc[i], 0));
    if (coopbuild::BuildLedgerEscaped(M, okm, "580-gamedata.base") != 1 || coopbuild::BuildLedgerPruneUncounted(&M, okm) != 1 || M.esc.size() != 1 || M.esc[0].kind != coopbuild::kBuildLedgerEscHolder)
        Fail("p14f5: with no squad record applied, the own pickup counted or the holder escape did not (or the prune took the wrong one)");
}
void t_p87f1_tombstones()
{
    /* the owner's re-send rule: a tombstone of this world goes as before; a read-back one only once verified; an own piece or a waiting PLACE ends either */
    if (coopbuild::BuildTombSendAct(0, 0, 0, 0) != coopbuild::kBuildTombSend) Fail("p87f1: a tombstone of this world was not sent");
    if (coopbuild::BuildTombSendAct(1, 0, 0, 0) != coopbuild::kBuildTombHold) Fail("p87f1: an unverified read-back tombstone was sent");
    if (coopbuild::BuildTombSendAct(1, 1, 0, 0) != coopbuild::kBuildTombSend) Fail("p87f1: a verified read-back tombstone was not sent");
    for (int rb = 0; rb < 2; ++rb)
        for (int v = 0; v < 2; ++v)
        {
            if (coopbuild::BuildTombSendAct(rb, v, 1, 0) != coopbuild::kBuildTombDrop) Fail("p87f1: a tombstone survived a live own piece at its key");
            if (coopbuild::BuildTombSendAct(rb, v, 0, 1) != coopbuild::kBuildTombDrop) Fail("p87f1: a tombstone survived a waiting PLACE at its key");
        }
    /* verifying a read-back tombstone: (tail, furn, walked, hostFound, look, own, dismantling) */
    if (coopbuild::BuildTombVerifyVerdict(0, 0, 1, 1, 0, 0, 0) != 0) Fail("p87f1: a row written before fold 1 (furniture unknown) was verified");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 0, 1, 0, 0, 0) != 0) Fail("p87f1: a tombstone whose zone was not walked was verified");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 1, 1, 0, 0, 0) != 1) Fail("p87f1: a walked zone with no piece at the key was not verified");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 1, 1, 2, 0, 0) != 0) Fail("p87f1: an incomplete lookup verified a tombstone");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 1, 1, 1, 1, 0) != -1) Fail("p87f1: an own piece at the key did not end the tombstone");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 1, 1, 1, 1, 1) != 0) Fail("p87f1: an own piece being dismantled was taken as absent or live");
    if (coopbuild::BuildTombVerifyVerdict(1, 0, 1, 1, 1, 0, 0) != 1) Fail("p87f1: another owner's piece at the key blocked the verification");
    if (coopbuild::BuildTombVerifyVerdict(1, 1, 1, 0, 0, 0, 0) != 0) Fail("p87f1: furniture whose host is missing was verified");
    if (coopbuild::BuildTombVerifyVerdict(1, 1, 1, 1, 0, 0, 0) != 1) Fail("p87f1: furniture absent from a walked host was not verified");
    if (coopbuild::BuildTombVerifyVerdict(1, 1, 1, 1, 1, 1, 0) != -1) Fail("p87f1: an own furniture piece in its host did not end the tombstone");
    /* the receiver's nonce skip: only a known, different placement is kept */
    if (coopbuild::BuildRemoveNonceSkip(5u, 6u) != 1) Fail("p87f1: a REMOVE of another placement touched the newer one");
    if (coopbuild::BuildRemoveNonceSkip(5u, 5u) != 0) Fail("p87f1: a REMOVE of the same placement was skipped");
    if (coopbuild::BuildRemoveNonceSkip(0u, 6u) != 0 || coopbuild::BuildRemoveNonceSkip(5u, 0u) != 0) Fail("p87f1: an unknown nonce skipped a REMOVE");
    /* L1: a never-shared blueprint placed in this world leaves no tombstone; anything else does */
    if (coopbuild::BuildTombNeeded(0, 0.0f, 1) != 0) Fail("p87f1: a never-shared blueprint was tombstoned");
    if (coopbuild::BuildTombNeeded(1, 0.0f, 1) != 1 || coopbuild::BuildTombNeeded(0, 0.5f, 1) != 1 || coopbuild::BuildTombNeeded(0, 0.0f, 0) != 1) Fail("p87f1: a piece that may have a copy was not tombstoned");
    /* the REMOVE tail: nothing written when unset (the old bytes); nonce; nonce + flags; the pp.build furniture tail */
    coopbuild::BuildMsg r; r.kind = coopbuild::kBuildRemove;
    r.key = "1459-gamedata.base@43,11@538554,-945925,21043"; r.reason = coopbuild::kBuildReasonDismantled;
    const size_t base = 1 + 1 + r.key.size() + 1;
    std::vector<char> b;
    if (!coopbuild::EncodeBuild(&b, r) || b.size() != base) Fail("p87f1: a REMOVE with no tail changed its bytes");
    coopbuild::BuildMsg o;
    r.nonce = 0x12345678u; b.clear();
    if (!coopbuild::EncodeBuild(&b, r) || b.size() != base + 4) Fail("p87f1: a REMOVE's nonce was not written as 4 bytes");
    else
    {
        if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.key != r.key || o.nonce != r.nonce || o.rmFlags != 0 || !o.hostKey.empty())
            Fail("p87f1: a REMOVE's nonce did not survive the round trip");
        if (coopbuild::DecodeBuild(&b[0], base, &o) != coopbuild::kBuildDecodeOk || o.nonce != 0) Fail("p87f1: the old REMOVE (no tail) no longer decodes");
    }
    r.rmFlags = coopbuild::kBuildRmResend; b.clear();
    if (!coopbuild::EncodeBuild(&b, r) || b.size() != base + 5
        || coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.nonce != r.nonce || o.rmFlags != coopbuild::kBuildRmResend)
        Fail("p87f1: a re-sent REMOVE's flag did not survive the round trip");
    r.nonce = 0; r.rmFlags = (unsigned char)(coopbuild::kBuildRmTombTail | coopbuild::kBuildRmFurniture); r.hostKey = "Shack@43,11@538000,-945000,21000"; b.clear();
    if (!coopbuild::BuildEncodable(r) || !coopbuild::EncodeBuild(&b, r) || b.size() != base + 4 + 1 + 1 + r.hostKey.size()) Fail("p87f1: a furniture tombstone's tail was not written");
    else
    {
        if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.nonce != 0 || o.rmFlags != r.rmFlags || o.hostKey != r.hostKey)
            Fail("p87f1: a furniture tombstone's tail did not survive the round trip");
        if (coopbuild::DecodeBuild(&b[0], b.size() - 1, &o) != coopbuild::kBuildDecodeOk || o.key != r.key || !o.hostKey.empty())
            Fail("p87f1: a cut tail refused the REMOVE or kept a partial host key");
    }
    r.hostKey = std::string(coopbuild::kBuildMaxKey + 1, 'h');
    if (coopbuild::BuildEncodable(r)) Fail("p87f1: an over-long host key was encodable");
    r.rmFlags = coopbuild::kBuildRmTombTail;
    if (!coopbuild::BuildEncodable(r)) Fail("p87f1: a host key without the furniture flag blocked the REMOVE");
    b.clear();
    if (!coopbuild::EncodeBuild(&b, r) || b.size() != base + 5) Fail("p87f1: a host key without the furniture flag was written");
}

void t_par13_owed_remove()
{
    std::vector<std::string> owed;
    /* added while the link is down: once per key */
    if (coopbuild::BuildOweRemoveAdd(&owed, "k1") != 1) Fail("par13: a dismantle while the link is down was not owed");
    if (coopbuild::BuildOweRemoveAdd(&owed, "k1") != 0 || owed.size() != 1) Fail("par13: the same key was owed twice");
    if (coopbuild::BuildOweRemoveAdd(&owed, "k2") != 1 || owed.size() != 2) Fail("par13: a second dismantle was not owed");
    Par13Send s;
    /* link still down: nothing goes, nothing is lost */
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, s) != 0 || owed.size() != 2 || !s.sent.empty()) Fail("par13: a flush with the link down sent or dropped an owed REMOVE");
    /* link up: each goes once, in order */
    s.linkUp = 1;
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, s) != 2 || !owed.empty()) Fail("par13: the link-up flush did not send every owed REMOVE");
    if (s.sent.size() != 2 || s.sent[0] != "k1" || s.sent[1] != "k2") Fail("par13: the owed REMOVEs went out of order");
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, s) != 0 || s.sent.size() != 2) Fail("par13: an owed REMOVE was sent twice");
    /* the per-tick cap: the rest wait, in order */
    coopbuild::BuildOweRemoveAdd(&owed, "a"); coopbuild::BuildOweRemoveAdd(&owed, "b"); coopbuild::BuildOweRemoveAdd(&owed, "c");
    Par13Send c; c.linkUp = 1;
    if (coopbuild::BuildOweRemoveFlush(&owed, 2, c) != 2 || owed.size() != 1 || owed[0] != "c") Fail("par13: the per-tick cap did not keep the rest in order");
    if (coopbuild::BuildOweRemoveFlush(&owed, 2, c) != 1 || !owed.empty() || c.sent.size() != 3 || c.sent[2] != "c") Fail("par13: the capped rest did not go next tick");
    /* the link goes down mid-flush: the failed one and the rest stay owed */
    coopbuild::BuildOweRemoveAdd(&owed, "x"); coopbuild::BuildOweRemoveAdd(&owed, "y"); coopbuild::BuildOweRemoveAdd(&owed, "z");
    Par13Send f; f.linkUp = 1; f.failAfter = 1;
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, f) != 1 || owed.size() != 2 || owed[0] != "y" || owed[1] != "z") Fail("par13: a failed send lost or reordered an owed REMOVE");
    f.failAfter = -1;
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, f) != 2 || !owed.empty() || f.sent.size() != 3) Fail("par13: the owed REMOVEs left after a failed send did not go");
    /* a key placed again is no longer owed */
    coopbuild::BuildOweRemoveAdd(&owed, "p"); coopbuild::BuildOweRemoveAdd(&owed, "q");
    if (coopbuild::BuildOweRemoveDrop(&owed, "p") != 1 || owed.size() != 1 || owed[0] != "q") Fail("par13: a key placed again stayed owed");
    if (coopbuild::BuildOweRemoveDrop(&owed, "p") != 0) Fail("par13: dropping a key not owed reported it owed");
    /* cleared per world: a world load forgets the list, and nothing goes after it */
    coopbuild::BuildOweRemoveAdd(&owed, "w");
    if (coopbuild::BuildOweRemoveForget(&owed) != 2 || !owed.empty()) Fail("par13: a world load did not forget the owed REMOVEs");
    Par13Send w; w.linkUp = 1;
    if (coopbuild::BuildOweRemoveFlush(&owed, 8, w) != 0 || !w.sent.empty()) Fail("par13: an owed REMOVE of the world before was sent");
    if (coopbuild::BuildOweRemoveAdd(&owed, "q") != 1) Fail("par13: a key forgotten with its world could not be owed again in the next");
}

/* house2: the order / Shift-job gate's task rule (one rule for 0x7F9280 and 0x7F4EF0) and orderKept once per commit */
void t_house2_task_gate()
{
    const int gated[4] = { 2, 71, 95, 96 };
    for (int i = 0; i < 4; ++i)
    {
        if (!coopbuild::BuildTaskGated(gated[i])) Fail("house2: a build / add-materials / repair / dismantle task was not gated");
        if (!coopbuild::BuildTaskRefused(gated[i], 1)) Fail("house2: a gated task on a handed piece was not refused");
        if (coopbuild::BuildTaskRefused(gated[i], 0)) Fail("house2: a gated task on a piece not handed away was refused");
    }
    const int use[6] = { 0, 1, 3, 57, 58, 124 };
    for (int i = 0; i < 6; ++i)
        if (coopbuild::BuildTaskGated(use[i]) || coopbuild::BuildTaskRefused(use[i], 1)) Fail("house2: a use task on a handed piece was refused");
    if (coopbuild::BuildCommitOrderKept(3, 0) != 1) Fail("house2: an ungated commit with 3 handed pieces must count orderKept ONCE");
    if (coopbuild::BuildCommitOrderKept(1, 0) != 1) Fail("house2: an ungated commit with 1 handed piece must count orderKept once");
    if (coopbuild::BuildCommitOrderKept(0, 0) != 0) Fail("house2: a commit with no handed piece counted orderKept");
    if (coopbuild::BuildCommitOrderKept(3, 1) != 0) Fail("house2: a gated commit counted orderKept");
}

/* house2 + fold (MED2): a hand-over PLACE is late only when its (key, nonce) was removed here; a new nonce at the same spot is a new
   placement; no nonce keeps the key-only rule; an ordinary PLACE after a REMOVE stays a new piece */
void t_house2_late_place()
{
    /* BuildLatePlaceRefused(ownerSlot, nonce, (key, nonce) listed, key listed with any nonce) */
    if (!coopbuild::BuildLatePlaceRefused(0, 0x1234u, 1, 1)) Fail("house2 fold: a hand-over PLACE whose (key, nonce) was removed here was not refused");
    if (!coopbuild::BuildLatePlaceRefused(3, 0x1234u, 1, 1)) Fail("house2 fold: a hand-over PLACE (slot 3) whose (key, nonce) was removed was not refused");
    if (coopbuild::BuildLatePlaceRefused(0, 0x5678u, 0, 1)) Fail("house2 fold: a NEW placement at a removed spot (a new nonce) was refused");
    if (coopbuild::BuildLatePlaceRefused(0, 0x5678u, 0, 0)) Fail("house2 fold: a hand-over PLACE for a key never removed here was refused");
    if (!coopbuild::BuildLatePlaceRefused(3, 0, 0, 1)) Fail("house2 fold: a no-nonce hand-over PLACE for a removed key was not refused (the key-only rule)");
    if (coopbuild::BuildLatePlaceRefused(3, 0, 0, 0)) Fail("house2 fold: a no-nonce hand-over PLACE for a key never removed was refused");
    if (coopbuild::BuildLatePlaceRefused(coopbuild::kBuildOwnerSender, 0x1234u, 1, 1) || coopbuild::BuildLatePlaceRefused(coopbuild::kBuildOwnerSender, 0, 0, 1))
        Fail("house2: an ordinary PLACE after a REMOVE was refused - it is a new piece at the same spot (build1-d)");
    /* fold 2: a reloaded owner's row (nonce 0) adopts the hand-over re-send's nonce; a row with a nonce keeps it */
    if (coopbuild::BuildAdoptNonce(0, 3, 0x55u) != 0x55u) Fail("house2 fold 2: a nonce-0 row did not adopt a hand-over PLACE's nonce");
    if (coopbuild::BuildAdoptNonce(7u, 3, 0x55u) != 7u) Fail("house2 fold 2: a row's own nonce was replaced");
    if (coopbuild::BuildAdoptNonce(0, coopbuild::kBuildOwnerSender, 0x55u) != 0) Fail("house2 fold 2: an ordinary PLACE's nonce was adopted");
    if (coopbuild::BuildAdoptNonce(0, 3, 0) != 0) Fail("house2 fold 2: a no-nonce PLACE changed the row");
}

/* house2 fold (MED1): a retry that finally reads the owed file merges it - an in-memory row for the same key wins */
void t_house2_owed_merge()
{
    std::map<std::string, int> mem;
    std::set<std::string> erased;
    erased.insert("k3");   /* fold 2: a REMOVE ended k3's row while the file was unread */
    mem["k1"] = 1;   /* recorded in memory while the file could not be read */
    if (coopbuild::BuildOwedMergeRow(&mem, &erased, std::string("k1"), 2) != 0 || mem["k1"] != 1) Fail("house2 fold: the file's row replaced an in-memory row for the same key");
    if (coopbuild::BuildOwedMergeRow(&mem, &erased, std::string("k2"), 3) != 1 || mem["k2"] != 3) Fail("house2 fold: a file row for a key not in memory was not merged");
    if (coopbuild::BuildOwedMergeRow(&mem, &erased, std::string("k3"), 4) != 2 || mem.find("k3") != mem.end())
        Fail("house2 fold 2: a file row a REMOVE ended while the file was unread came back at the merge");
    if (coopbuild::BuildOwedMergeRow(&mem, (const std::set<std::string>*)0, std::string("k4"), 5) != 1) Fail("house2 fold 2: a merge with no erased set refused a row");
    if (mem.size() != 3) Fail("house2 fold: the merged map does not hold exactly the kept and merged rows");
}

/* house2 fold (protocol 76): the PLACE nonce - written only when non-zero, absent = 0 (an old sender), one encoding */
void t_house2_place_nonce_wire()
{
    coopbuild::BuildMsg m; m.kind = coopbuild::kBuildPlace;
    m.key = "1459-gamedata.base@31,22:-512345,8122,12345"; m.sid = "1459-gamedata.base"; m.ownerSlot = 3; m.needed = 5.0f;
    std::vector<char> b0, b; coopbuild::BuildMsg o;
    if (!coopbuild::EncodeBuild(&b0, m)) { Fail("house2 fold: a no-nonce PLACE did not encode"); return; }
    m.nonce = 0xA1B2C3D4u;
    if (!coopbuild::EncodeBuild(&b, m) || b.size() != b0.size() + 4) { Fail("house2 fold: a nonce PLACE is not the no-nonce PLACE + 4 bytes"); return; }
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.nonce != 0xA1B2C3D4u || o.ownerSlot != 3 || o.key != m.key)
        Fail("house2 fold: a nonce PLACE did not survive the round trip");
    o.nonce = 7;
    if (coopbuild::DecodeBuild(&b0[0], b0.size(), &o) != coopbuild::kBuildDecodeOk || o.nonce != 0 || o.ownerSlot != 3)
        Fail("house2 fold: a PLACE without a nonce (an old sender / owed row) did not decode as nonce 0");
    for (size_t cut = b0.size() + 1; cut < b.size(); ++cut)
        if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("house2 fold: a PLACE with a cut nonce was accepted"); break; }
    std::vector<char> z(b); std::memset(&z[b.size() - 4], 0, 4);
    if (coopbuild::DecodeBuild(&z[0], z.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("house2 fold: a PLACE carrying a zero nonce was accepted");
    std::vector<char> x1(b); x1.push_back(1);   /* fold 2: nothing may follow the nonce */
    if (coopbuild::DecodeBuild(&x1[0], x1.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("house2 fold 2: a PLACE with a byte after its nonce was accepted");
    std::vector<char> x5(b); x5.resize(b.size() + 5, 1);
    if (coopbuild::DecodeBuild(&x5[0], x5.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("house2 fold 2: a PLACE with 5 bytes after its nonce was accepted");
}

/* build1-d: MSG_BUILD REMOVE {key, reason} - the builder dismantled or lost a piece; the other game removes its copy. */
void t_build1d_remove_wire()
{
    coopbuild::BuildMsg r; r.kind = coopbuild::kBuildRemove;
    r.key = "1459-gamedata.base@43,11@538554,-945925,21043"; r.reason = coopbuild::kBuildReasonDismantled;
    std::vector<char> b;
    const size_t want = 1 + 1 + r.key.size() + 1;
    if (!coopbuild::EncodeBuild(&b, r) || b.size() != want) { Fail("a REMOVE did not encode to its exact size"); return; }
    coopbuild::BuildMsg o;
    if (coopbuild::DecodeBuild(&b[0], b.size(), &o) != coopbuild::kBuildDecodeOk || o.kind != 3 || o.key != r.key || o.reason != 1
        || !o.sid.empty() || o.nMats != 0)
        Fail("a REMOVE did not survive the round trip");
    for (size_t cut = 0; cut < b.size(); ++cut)
        if (coopbuild::DecodeBuild(&b[0], cut, &o) == coopbuild::kBuildDecodeOk) { Fail("a truncated REMOVE was accepted"); break; }
    coopbuild::BuildMsg d = r; d.reason = coopbuild::kBuildReasonDestroyed; std::vector<char> db;
    if (!coopbuild::EncodeBuild(&db, d) || coopbuild::DecodeBuild(&db[0], db.size(), &o) != coopbuild::kBuildDecodeOk || o.reason != 2)
        Fail("a REMOVE with reason 2 (destroyed) did not survive the round trip");
    std::vector<char> z(b); z[want - 1] = 0;
    if (coopbuild::DecodeBuild(&z[0], z.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a REMOVE with reason 0 was accepted");
    std::vector<char> t(b); t[want - 1] = 3;
    if (coopbuild::DecodeBuild(&t[0], t.size(), &o) != coopbuild::kBuildDecodeBadValue) Fail("a REMOVE with reason 3 was accepted");
    std::vector<char> ek; ek.push_back(3); ek.push_back(0); ek.push_back(1);
    if (coopbuild::DecodeBuild(&ek[0], ek.size(), &o) != coopbuild::kBuildDecodeBadKey) Fail("a REMOVE with an empty key was accepted");
    std::vector<char> k64; k64.push_back(3); k64.push_back(64); k64.resize(2 + 64, 'k'); k64.push_back(1);
    if (coopbuild::DecodeBuild(&k64[0], k64.size(), &o) != coopbuild::kBuildDecodeBadKey) Fail("a REMOVE with a 64-byte key was accepted");
    coopbuild::BuildMsg nr = r; nr.reason = 0; std::vector<char> nb;
    if (coopbuild::EncodeBuild(&nb, nr) || !nb.empty()) Fail("a REMOVE with reason 0 was encoded");
    coopbuild::BuildMsg nk = r; nk.key.clear(); std::vector<char> kb;
    if (coopbuild::EncodeBuild(&kb, nk)) Fail("a REMOVE with no key was encoded");
    coopbuild::BuildMsg lk = r; lk.key = std::string(coopbuild::kBuildMaxKey + 1, 'k'); std::vector<char> lb;
    if (coopbuild::EncodeBuild(&lb, lk)) Fail("a REMOVE with an over-long key was encoded");
    std::vector<char> k4(b); k4[0] = 6;   /* build1-f: kind 4 is ROSTER_REQUEST; house1b: 5 is HAND_ACK */
    if (coopbuild::DecodeBuild(&k4[0], k4.size(), &o) != coopbuild::kBuildDecodeBadKind) Fail("a BUILD kind 6 was accepted");
}

void t_crime5_bounty_wire_and_additions()
{
    coopbounty::BountyList l;
    coopbounty::BountyEntry e; e.sid = "17-gamedata.base"; e.amount = 300; e.crimes = 1u << 3; e.claimed = 0; e.time = 1234.5;
    l.push_back(e);
    coopbounty::BountyEntry f; f.sid = "defaultEmpireFactionSID"; f.amount = 50; f.crimes = 1u << 5; f.claimed = 1; f.time = 99.25;
    l.push_back(f);
    std::vector<char> b;
    if (!coopbounty::EncodeBounty(&b, 42u, coopbounty::kBountyKindList, l)) { Fail("a list did not encode"); return; }
    unsigned int uid = 0; unsigned char kind = 9; coopbounty::BountyList o;
    if (coopbounty::DecodeBounty(&b[0], b.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeOk || uid != 42u || kind != 0 || o.size() != 2
        || o[0].sid != "17-gamedata.base" || o[0].amount != 300 || o[0].crimes != 8u || o[1].claimed != 1 || o[1].time != 99.25)
        Fail("a list did not round-trip");
    if (!coopbounty::BountyListsEqual(l, o)) Fail("a round-tripped list must equal the original");
    /* An empty list is legal (the owner's first word on a character with no bounty). */
    std::vector<char> z;
    if (!coopbounty::EncodeBounty(&z, 7u, coopbounty::kBountyKindList, coopbounty::BountyList()) || z.size() != 6) Fail("an empty list must encode as 6 bytes");
    else if (coopbounty::DecodeBounty(&z[0], z.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeOk || !o.empty()) Fail("an empty list did not round-trip");
    /* Encode refusals append nothing. */
    coopbounty::BountyList bad = l; bad[0].sid = "";
    std::vector<char> n1;
    if (coopbounty::EncodeBounty(&n1, 1u, 0, bad) || !n1.empty()) Fail("an empty sid must not encode");
    bad = l; bad[0].amount = -1;
    if (coopbounty::EncodeBounty(&n1, 1u, 0, bad) || !n1.empty()) Fail("a negative amount must not encode");
    if (coopbounty::EncodeBounty(&n1, 1u, 3, l) || !n1.empty()) Fail("kind 3 must not encode (par20: kind 2 is the clear)");
    coopbounty::BountyList many(17, e);
    if (coopbounty::EncodeBounty(&n1, 1u, 0, many) || !n1.empty()) Fail("17 entries must not encode");
    /* Hostile payloads. */
    if (coopbounty::DecodeBounty(&b[0], 5, &uid, &kind, &o) != coopbounty::kBountyDecodeTooShort) Fail("5 bytes must be too short");
    if (coopbounty::DecodeBounty(&b[0], b.size() - 1, &uid, &kind, &o) != coopbounty::kBountyDecodeTooShort) Fail("a cut entry must be too short");
    std::vector<char> h = b; h[4] = 3;
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadKind) Fail("kind 3 must be refused");
    h = b; h[5] = 17;
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeTooMany) Fail("17 entries must be refused");
    h = b; h[6] = 0;
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadSid) Fail("a zero-length sid must be refused");
    h = b; h[6] = (char)200;
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadSid) Fail("a 200-byte sid must be refused");
    h = b; { const int neg = -5; std::memcpy(&h[7 + 16], &neg, 4); }
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadAmount) Fail("a negative amount must be refused");
    h = b; { const unsigned long long nan = 0x7FF8000000000000ULL; std::memcpy(&h[7 + 16 + 9], &nan, 8); }
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadTime) Fail("a NaN time must be refused");
    /* The change test ignores the time and the order. */
    coopbounty::BountyList r; r.push_back(f); r.push_back(e); r[1].time = 5.0;
    if (!coopbounty::BountyListsEqual(l, r)) Fail("order and time must not count as a change");
    r[1].amount = 301;
    if (coopbounty::BountyListsEqual(l, r)) Fail("an amount change must count");
    /* Additions: an increase, new crime bits, a new faction; never a decrease or a missing faction. */
    coopbounty::BountyList cur = l; cur[0].amount = 450; cur[0].crimes |= 1u << 5;
    coopbounty::BountyEntry g; g.sid = "51646-Dialogue.mod"; g.amount = 20; g.crimes = 1u << 3; g.time = 1300.0; cur.push_back(g);
    cur[1].amount = 10;   /* a decrease: the engine expiring it here */
    coopbounty::BountyList add = coopbounty::BountyAdditions(l, cur);
    if (add.size() != 2) Fail("two additions expected, got " + I((int)add.size()));
    else
    {
        if (add[0].sid != "17-gamedata.base" || add[0].amount != 150 || add[0].crimes != (1u << 5)) Fail("the increase must be 150 with the new ASSAULT bit");
        if (add[1].sid != "51646-Dialogue.mod" || add[1].amount != 20 || add[1].crimes != (1u << 3)) Fail("a new faction must be sent whole");
    }
    if (!coopbounty::BountyAdditions(l, l).empty()) Fail("no change must mean no additions");
    coopbounty::BountyList fewer; fewer.push_back(e);
    if (!coopbounty::BountyAdditions(l, fewer).empty()) Fail("a missing faction must not be sent");
}

/* par20 (parity P20): a bounty claimed or cleared on the NON-owner's game reaches the owner (MSG_BOUNTY kind 2) - the wire,
   the copy's removals, the owner's decision, and the owner's next list carrying the cleared state. */
void t_par20_bounty_clear_reaches_the_owner()
{
    coopbounty::BountyList owner;   /* the owner's last list, as written onto the copy (the copy's `known`) */
    coopbounty::BountyEntry e; e.sid = "17-gamedata.base"; e.amount = 3000; e.crimes = 1u << 3; e.claimed = 0; e.time = 100.0;
    owner.push_back(e);
    coopbounty::BountyEntry f; f.sid = "defaultEmpireFactionSID"; f.amount = 500; f.crimes = 1u << 5; f.claimed = 0; f.time = 90.0;
    owner.push_back(f);
    /* 1. the wire: kind 2 round-trips with its flags byte; kind 3 is still refused */
    coopbounty::BountyList clr; coopbounty::BountyEntry c0 = e;
    c0.claimed = (unsigned char)(coopbounty::kBountyClearErased | coopbounty::kBountyClearClaimed); clr.push_back(c0);
    std::vector<char> b;
    if (!coopbounty::EncodeBounty(&b, 9u, coopbounty::kBountyKindClear, clr)) { Fail("a kind 2 clear did not encode"); return; }
    unsigned int uid = 0; unsigned char kind = 0; coopbounty::BountyList o;
    if (coopbounty::DecodeBounty(&b[0], b.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeOk || uid != 9u || kind != 2 || o.size() != 1
        || o[0].claimed != 3 || o[0].amount != 3000 || o[0].crimes != 8u)
        Fail("a kind 2 clear did not round-trip with its flags");
    std::vector<char> h = b; h[4] = 3;
    if (coopbounty::DecodeBounty(&h[0], h.size(), &uid, &kind, &o) != coopbounty::kBountyDecodeBadKind) Fail("kind 3 must still be refused");
    /* 2. the copy's side: the bounty turned in / paid off here (entry gone) is a clear of all of it - the old path sent nothing */
    coopbounty::BountyList copy; copy.push_back(f);
    if (!coopbounty::BountyAdditions(owner, copy).empty()) Fail("a removal must not be an addition");
    coopbounty::BountyList r = coopbounty::BountyRemovals(owner, copy);
    if (r.size() != 1 || r[0].sid != e.sid || r[0].amount != 3000 || r[0].crimes != 8u || r[0].claimed != coopbounty::kBountyClearErased)
        Fail("a gone entry must be sent whole, flagged gone");
    coopbounty::BountyList claimedHere = owner; claimedHere[1].claimed = 1;
    coopbounty::BountyList rc = coopbounty::BountyRemovals(owner, claimedHere);
    if (rc.size() != 1 || rc[0].sid != f.sid || rc[0].amount != 0 || rc[0].crimes != 0 || rc[0].claimed != coopbounty::kBountyClearClaimed)
        Fail("a claim must be sent as the claimed flag alone");
    coopbounty::BountyList lower = owner; lower[0].amount = 1000;
    coopbounty::BountyList rl = coopbounty::BountyRemovals(owner, lower);
    if (rl.size() != 1 || rl[0].amount != 2000 || rl[0].crimes != 0 || rl[0].claimed != 0) Fail("a lower amount must send what was taken off");
    if (!coopbounty::BountyRemovals(owner, owner).empty()) Fail("no change must mean no removals");
    coopbounty::BountyList higher = owner; higher[0].amount = 4000;
    if (!coopbounty::BountyRemovals(owner, higher).empty()) Fail("an addition must not be a removal");
    coopbounty::BountyList full;
    for (int i = 0; i < (int)coopbounty::kBountyMaxEntries; ++i) { coopbounty::BountyEntry x = f; x.sid = "x" + I(i); full.push_back(x); }
    if (!coopbounty::BountyRemovals(owner, full).empty()) Fail("a faction missing from a FULL list must not be called gone");
    /* 3. the owner's decision */
    coopbounty::BountyClearOutcome d = coopbounty::BountyClearApply(&e, r[0]);
    if (d.action != coopbounty::kBountyClearErase) Fail("the owner must erase a bounty cleared whole on the copy");
    coopbounty::BountyEntry grown = e; grown.amount = 4500; grown.crimes = (1u << 3) | (1u << 5);
    d = coopbounty::BountyClearApply(&grown, r[0]);
    if (d.action != coopbounty::kBountyClearWrite || d.entry.amount != 1500 || d.entry.crimes != (1u << 5))
        Fail("a bounty the owner added after the copy's last list must survive as the remainder");
    d = coopbounty::BountyClearApply(&f, rc[0]);
    if (d.action != coopbounty::kBountyClearWrite || d.entry.claimed != 1 || d.entry.amount != 500) Fail("a claim must stick on the owner");
    d = coopbounty::BountyClearApply(&e, rl[0]);
    if (d.action != coopbounty::kBountyClearWrite || d.entry.amount != 1000 || d.entry.crimes != 8u) Fail("a partial clear must lower the owner's amount");
    if (coopbounty::BountyClearApply(0, r[0]).action != coopbounty::kBountyClearNone) Fail("an owner with no entry has nothing to clear");
    coopbounty::BountyEntry huge = r[0]; huge.amount = coopbounty::kBountyMaxAmount; huge.claimed = 0;
    if (coopbounty::BountyClearApply(&e, huge).action != coopbounty::kBountyClearErase) Fail("taking off more than there is must erase, never go negative");
    /* 4. the owner's next list (re-sent every 30 s) carries the cleared state; the copy keeps it and sends nothing back */
    coopbounty::BountyList ownerAfter; ownerAfter.push_back(f);
    if (coopbounty::BountyListsEqual(ownerAfter, owner)) Fail("the owner's list after the clear must differ from the old one");
    if (coopbounty::BountyFind(ownerAfter, e.sid) != 0) Fail("the cleared bounty must not be in the owner's next list");
    if (!coopbounty::BountyListsEqual(ownerAfter, copy)) Fail("the owner's next list must equal the cleared copy");
    if (!coopbounty::BountyRemovals(ownerAfter, copy).empty() || !coopbounty::BountyAdditions(ownerAfter, copy).empty())
        Fail("after the owner's next list the copy must send nothing back");
}

/* T-356: crime's sight cells hold ONE FACTION PER OTHER PLAYER (the old rule held one faction: with a third player one of the two
   other players' characters was never recognised on sight). The fill rule, and the stub's bytes byte for byte. */
void t_t356_sight_cells_one_per_other_player()
{
    using namespace coopcrime;
    int a = 0, b = 0, c = 0;
    const void* A = &a; const void* B = &b; const void* C = &c;
    const void* cells[kSightCellCap];
    int dropped = -1;
    {   /* one other player: exactly the one faction the old single cell held */
        const int sl[1] = { 1 }; const void* f[1] = { A };
        if (SightCellsFill(sl, f, 1, 0, cells, kSightCellCap, &dropped) != 1 || cells[0] != A || cells[1] != 0 || dropped != 0)
            Fail("T-356: one other player - one cell, its faction");
    }
    {   /* a made-up third player (slot 7): both other players' factions are held */
        const int sl[2] = { 7, 1 }; const void* f[2] = { B, A };
        const int held = SightCellsFill(sl, f, 2, 0, cells, kSightCellCap, &dropped);
        if (held != 2) Fail("T-356: two other players - two cells (the old rule held one)");
        if (cells[0] != A || cells[1] != B || cells[2] != 0) Fail("T-356: the cells are in slot order, the rest empty");
    }
    {   /* this game's own slot, a null faction and a faction named twice (the session peer's, also in the table) */
        const int sl[5] = { 0, 1, 3, 1, -1 }; const void* f[5] = { C, A, 0, A, B };
        if (SightCellsFill(sl, f, 5, 0, cells, kSightCellCap, &dropped) != 2 || cells[0] != B || cells[1] != A)
            Fail("T-356: own slot and null skipped, a repeat held once, an unknown slot (an old save's coop-peer) first");
        if (SightCellsFill(sl, f, 5, -1, cells, kSightCellCap, &dropped) != 3)
            Fail("T-356: with no slot of our own known nothing is skipped as ours");
    }
    {   /* bounded: more distinct factions than cells - the extra is counted, never written past the end */
        static int many[kSightCellCap + 1];
        int sl[kSightCellCap + 1]; const void* f[kSightCellCap + 1];
        for (int k = 0; k <= kSightCellCap; ++k) { sl[k] = k + 1; f[k] = &many[k]; }
        if (SightCellsFill(sl, f, kSightCellCap + 1, 0, cells, kSightCellCap, &dropped) != kSightCellCap || dropped != 1)
            Fail("T-356: a full table holds kSightCellCap and counts the one that did not fit");
    }
    if (kSightCellCap < 33) Fail("T-356: the cells must cover the 32-entry stand-in table plus an old save's coop-peer");
    {   /* the stub: every displacement lands where the layout says */
        std::vector<unsigned char> p(0x1000, 0xCC);
        const int off = 0x400;
        const unsigned long long site = 0x140856365ULL - 8;
        const int len = SightStubBuild(&p[0], site, kSightCellCap, off);
        if (len != kSightStubLen || len > off) Fail("T-356: the stub is kSightStubLen long and ends before the cells");
        int d = 0;
        const int N = 14 + 13 * kSightCellCap, hit = N + 10, back = N + 22;
        if (p[0] != 0x48 || p[3] != 0x50 || p[4] != 0x02 || p[8] != 0x0F || p[9] != 0x85) Fail("T-356: the stub opens with the original test and a jne");
        std::memcpy(&d, &p[10], 4); if (14 + d != back) Fail("T-356: a player faction jumps straight back");
        for (int k = 0; k < kSightCellCap; ++k)
        {
            const int o = 14 + 13 * k;
            if (p[o] != 0x48 || p[o + 1] != 0x3B || p[o + 2] != 0x05 || p[o + 7] != 0x0F || p[o + 8] != 0x84) { Fail("T-356: cell compare " + I(k) + " bytes"); break; }
            std::memcpy(&d, &p[o + 3], 4); if (o + 7 + d != off + 8 + 8 * k) { Fail("T-356: cell compare " + I(k) + " reads its own cell"); break; }
            std::memcpy(&d, &p[o + 9], 4); if (o + 13 + d != hit) { Fail("T-356: cell compare " + I(k) + " jumps to HIT"); break; }
        }
        if (p[N] != 0x48 || p[N + 8] != 0xEB || p[N + 9] != 0x0C) Fail("T-356: NOTPEER re-does the test and jumps back");
        if (p[N + 10] != 0xF0 || p[N + 13] != 0x05) Fail("T-356: HIT is a lock inc");
        std::memcpy(&d, &p[N + 14], 4); if (N + 18 + d != off) Fail("T-356: HIT counts into hits");
        if (p[N + 18] != 0x48 || p[N + 20] != 0xF8 || p[N + 22] != 0xFF || p[N + 23] != 0x25) Fail("T-356: cmp rax,0 then jmp [rip]");
        unsigned long long to = 0; std::memcpy(&to, &p[N + 28], 8); if (to != site + 8) Fail("T-356: BACK is the je at site + 8");
        if (p[len] != 0xCC) Fail("T-356: nothing written past the stub");
    }
}

/* T-356: THE SIGHT STUB'S GOLDEN BYTES. The test above checks the jumps against the same layout table the builder uses, so a
   wrong length or encoding in both would pass, and the jump back's zero displacement was never read. These bytes come from an
   independent decode (2026-10-02): a reviewer ported SightStubBuild to Python and disassembled its output with capstone, and
   the listing was checked against the intended instructions, not against the builder's offsets. One cell, site 0x14085635D,
   cells at 0x400:
      0  cmp qword [rax+0x250], 0       8  jne 49 (BACK)
     14  cmp rax, [rip+0x3f3] = 0x408 (cell 0)    21  je 37 (HIT)
     27  cmp qword [rax+0x250], 0      35  jmp 49
     37  lock inc qword [rip+0x3d3] = 0x400 (hits)
     45  cmp rax, 0                    49  jmp [rip] -> 0x140856365 (site + 8)    63 bytes.
   Forty cells: 570 bytes; jne 556, cell 0 je 544, lock inc at 544 -> 0x400, cmp rax,0 at 552, jmp [rip] at 556 -> site + 8. */
void t_t356_sight_stub_golden_bytes()
{
    using namespace coopcrime;
    const unsigned long long site = 0x14085635DULL;
    const int off = 0x400;
    {
        static const unsigned char gold[63] = {
            0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00,   /*  0 cmp qword [rax+0x250], 0 */
            0x0F, 0x85, 0x23, 0x00, 0x00, 0x00,               /*  8 jne 49 */
            0x48, 0x3B, 0x05, 0xF3, 0x03, 0x00, 0x00,         /* 14 cmp rax, [rip+0x3f3] (0x408) */
            0x0F, 0x84, 0x0A, 0x00, 0x00, 0x00,               /* 21 je 37 */
            0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00,   /* 27 cmp qword [rax+0x250], 0 */
            0xEB, 0x0C,                                       /* 35 jmp 49 */
            0xF0, 0x48, 0xFF, 0x05, 0xD3, 0x03, 0x00, 0x00,   /* 37 lock inc qword [rip+0x3d3] (0x400) */
            0x48, 0x83, 0xF8, 0x00,                           /* 45 cmp rax, 0 */
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,               /* 49 jmp [rip] */
            0x65, 0x63, 0x85, 0x40, 0x01, 0x00, 0x00, 0x00    /* 55 site + 8 = 0x140856365 */
        };
        std::vector<unsigned char> p(0x1000, 0xCC);
        const int len = SightStubBuild(&p[0], site, 1, off);
        if (len != 63) Fail("T-356 golden: one cell is 63 bytes, got " + I(len));
        for (int i = 0; i < 63; ++i)
            if (p[i] != gold[i]) { Fail("T-356 golden: one cell, byte " + I(i) + " is " + I((int)p[i]) + ", the decode says " + I((int)gold[i])); break; }
        if (p[63] != 0xCC) Fail("T-356 golden: one cell, nothing written past byte 63");
    }
    {
        static const unsigned char head[14] = { 0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x85, 0x1E, 0x02, 0x00, 0x00 };   /* jne 556 */
        static const unsigned char cell0[13] = { 0x48, 0x3B, 0x05, 0xF3, 0x03, 0x00, 0x00, 0x0F, 0x84, 0x05, 0x02, 0x00, 0x00 };      /* cell 0, je 544 */
        static const unsigned char tail[26] = {
            0xF0, 0x48, 0xFF, 0x05, 0xD8, 0x01, 0x00, 0x00,   /* 544 lock inc qword [rip+0x1d8] (0x400) */
            0x48, 0x83, 0xF8, 0x00,                           /* 552 cmp rax, 0 */
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,               /* 556 jmp [rip] */
            0x65, 0x63, 0x85, 0x40, 0x01, 0x00, 0x00, 0x00    /* 562 site + 8 */
        };
        std::vector<unsigned char> p(0x1000, 0xCC);
        const int len = SightStubBuild(&p[0], site, 40, off);
        if (len != 570) { Fail("T-356 golden: forty cells are 570 bytes, got " + I(len)); return; }
        if (kSightCellCap == 40 && kSightStubLen != 570) Fail("T-356 golden: kSightStubLen is not 570 for 40 cells");
        if (std::memcmp(&p[0], head, 14) != 0) Fail("T-356 golden: forty cells, the opening test and jne 556");
        if (std::memcmp(&p[14], cell0, 13) != 0) Fail("T-356 golden: forty cells, cell 0's compare and je 544");
        for (int i = 0; i < 26; ++i)
            if (p[544 + i] != tail[i]) { Fail("T-356 golden: forty cells, byte " + I(544 + i) + " is " + I((int)p[544 + i]) + ", the decode says " + I((int)tail[i])); break; }
        if (p[570] != 0xCC) Fail("T-356 golden: forty cells, nothing written past byte 570");
    }
}

/* T-968: the foliage cells' placement (crimewire.h StableCellsPlace) - a faction already in a cell keeps it while it stays wanted
   (the stub, on any thread, never misses it while the set changes), a gone one's cell is cleared, a new one takes the lowest empty
   cell, the rest are counted once. */
void t_t968_foliage_cells_stay_put()
{
    using namespace coopcrime;
    int a = 0, b = 0, c = 0, d = 0;
    const void* A = &a; const void* B = &b; const void* C = &c; const void* D = &d;
    const void* none[3] = { 0, 0, 0 };
    {   /* from empty: in the order given */
        const void* want[2] = { A, B }; const void* out[3]; int dr = -1;
        const int h = StableCellsPlace(none, want, 2, out, 3, &dr);
        if (h != 2 || dr != 0 || out[0] != A || out[1] != B || out[2] != 0) Fail("T-968 cells: from empty, A and B in cells 0 and 1");
    }
    {   /* a new stand-in listed before an old save's coop-peer: the coop-peer keeps its cell */
        const void* cur[3] = { B, 0, 0 }; const void* want[2] = { A, B }; const void* out[3]; int dr = -1;
        const int h = StableCellsPlace(cur, want, 2, out, 3, &dr);
        if (h != 2 || dr != 0 || out[0] != B || out[1] != A || out[2] != 0) Fail("T-968 cells: the held faction keeps cell 0, the new one takes cell 1");
    }
    {   /* one gone, one new: the gone one's cell is reused, the staying one does not move */
        const void* cur[3] = { A, B, 0 }; const void* want[2] = { C, B }; const void* out[3]; int dr = -1;
        const int h = StableCellsPlace(cur, want, 2, out, 3, &dr);
        if (h != 2 || dr != 0 || out[0] != C || out[1] != B || out[2] != 0) Fail("T-968 cells: B stays in cell 1, C takes A's cell 0");
    }
    {   /* an empty set clears every cell */
        const void* cur[3] = { A, 0, C }; const void* out[3]; int dr = -1;
        const int h = StableCellsPlace(cur, 0, 0, out, 3, &dr);
        if (h != 0 || dr != 0 || out[0] != 0 || out[1] != 0 || out[2] != 0) Fail("T-968 cells: an empty set clears every cell");
    }
    {   /* full: the held ones keep their cells, the new one is counted once, nulls and repeats skipped */
        const void* cur[2] = { A, B }; const void* want[6] = { 0, C, A, C, B, 0 }; const void* out[2]; int dr = -1;
        const int h = StableCellsPlace(cur, want, 6, out, 2, &dr);
        if (h != 2 || dr != 1 || out[0] != A || out[1] != B) Fail("T-968 cells: full - A and B kept, C counted once");
    }
    {   /* a repeat in the cells is cleared; a new one takes the lowest empty cell */
        const void* cur[4] = { A, 0, A, D }; const void* want[3] = { D, A, B }; const void* out[4]; int dr = -1;
        const int h = StableCellsPlace(cur, want, 3, out, 4, &dr);
        if (h != 3 || dr != 0 || out[0] != A || out[1] != B || out[2] != 0 || out[3] != D) Fail("T-968 cells: the repeat cleared, B in the lowest empty cell");
    }
    {   /* the same set again: the same cells */
        const void* cur[3] = { 0, B, A }; const void* want[2] = { A, B }; const void* out[3]; int dr = -1;
        const int h = StableCellsPlace(cur, want, 2, out, 3, &dr);
        if (h != 2 || dr != 0 || out[0] != 0 || out[1] != B || out[2] != A) Fail("T-968 cells: the same set, the same cells");
    }
}

/* crime3 (docs/design-crime.md 3 A): MSG_CRIME carries the offender's current crime. Round trip, the sid cap, hostile
   payloads refused by their own reason, and the owner's send decision (a change or a re-set, never the countdown). */
void t_crime3_wire_round_trips_and_send_decision()
{
    coopcrime::CrimeState s;
    s.crime = 3; s.expiry = 20.0f; s.victimUid = 3204448269u; s.factionSid = "17-gamedata.base";
    std::vector<char> b;
    if (!coopcrime::EncodeCrime(&b, 77u, s)) { Fail("a crime did not encode"); return; }
    if (b.size() != coopcrime::kCrimeFixedBytes + 16) Fail("the encoded CRIME is " + I((int)b.size()) + " bytes, not 33");
    unsigned int uid = 0;
    coopcrime::CrimeState o;
    if (coopcrime::DecodeCrime(&b[0], b.size(), &uid, &o) != coopcrime::kCrimeDecodeOk || uid != 77u || o.crime != 3
        || o.expiry != 20.0f || o.victimUid != 3204448269u || o.factionSid != "17-gamedata.base")
        Fail("a whole CRIME did not round-trip");
    /* A cleared crime (0, no victim, no faction) is legal and short. */
    coopcrime::CrimeState none;
    std::vector<char> z;
    if (!coopcrime::EncodeCrime(&z, 5u, none) || z.size() != coopcrime::kCrimeFixedBytes) Fail("a cleared crime must encode as 17 bytes");
    else if (coopcrime::DecodeCrime(&z[0], z.size(), &uid, &o) != coopcrime::kCrimeDecodeOk || o.crime != 0 || !o.factionSid.empty())
        Fail("a cleared crime did not round-trip");
    /* Trailing bytes are ignored (a later build may append). */
    std::vector<char> t = b; t.push_back('x');
    if (coopcrime::DecodeCrime(&t[0], t.size(), &uid, &o) != coopcrime::kCrimeDecodeOk || o.factionSid != "17-gamedata.base")
        Fail("a CRIME with a trailing byte must decode");
    /* The sid cap: 96 goes, 97 does not, and nothing is appended when it does not. */
    coopcrime::CrimeState cap = s; cap.factionSid = std::string(96, 'f');
    std::vector<char> g;
    if (!coopcrime::EncodeCrime(&g, 1u, cap)) Fail("a 96-byte sid must encode");
    cap.factionSid = std::string(97, 'f');
    std::vector<char> h;
    if (coopcrime::EncodeCrime(&h, 1u, cap) || !h.empty()) Fail("a 97-byte sid must not encode, and nothing may be appended");
    /* Hostile payloads, each by its own reason. */
    if (coopcrime::DecodeCrime(&b[0], 16, &uid, &o) != coopcrime::kCrimeDecodeTooShort) Fail("16 bytes must be too short");
    if (coopcrime::DecodeCrime(0, 40, &uid, &o) != coopcrime::kCrimeDecodeTooShort) Fail("a null payload must be too short");
    if (coopcrime::DecodeCrime(&b[0], b.size() - 1, &uid, &o) != coopcrime::kCrimeDecodeSidTrunc) Fail("a cut sid must be refused");
    std::vector<char> big = b; big[16] = (char)200;
    if (coopcrime::DecodeCrime(&big[0], big.size(), &uid, &o) != coopcrime::kCrimeDecodeSidTrunc) Fail("a sid length of 200 must be refused");
    std::vector<char> bc = b; int neg = -1; std::memcpy(&bc[4], &neg, 4);
    if (coopcrime::DecodeCrime(&bc[0], bc.size(), &uid, &o) != coopcrime::kCrimeDecodeBadCrime) Fail("crime -1 must be refused");
    int huge = 65; std::memcpy(&bc[4], &huge, 4);
    if (coopcrime::DecodeCrime(&bc[0], bc.size(), &uid, &o) != coopcrime::kCrimeDecodeBadCrime) Fail("crime 65 must be refused");
    const unsigned int badBits[4] = { 0x7FC00000u, 0x7F800000u, 0xBF800000u, 0x7F7FFFFFu };   /* NaN, +inf, -1.0, FLT_MAX */
    for (int k = 0; k < 4; ++k)
    {
        std::vector<char> be = b; std::memcpy(&be[8], &badBits[k], 4);
        if (coopcrime::DecodeCrime(&be[0], be.size(), &uid, &o) != coopcrime::kCrimeDecodeBadExpiry) Fail("a bad expiry must be refused (case " + I(k) + ")");
    }
    std::vector<char> bx = b; const float over = 3601.0f; std::memcpy(&bx[8], &over, 4);
    if (coopcrime::DecodeCrime(&bx[0], bx.size(), &uid, &o) != coopcrime::kCrimeDecodeBadExpiry) Fail("an expiry of 3601 must be refused");
    /* The send decision: against the last SENT state and the last SAMPLED expiry (review-crime3 2). */
    coopcrime::CrimeState last = s, cur = s;   /* sent at 20.0 */
    cur.expiry = 19.0f;
    if (coopcrime::CrimeShouldSend(last, 20.0f, cur) != 0) Fail("the countdown must not be sent");
    cur.expiry = 5.5f;
    if (coopcrime::CrimeShouldSend(last, 6.0f, cur) != 0) Fail("the countdown late in the crime must not be sent");
    cur.expiry = 20.0f;
    if (coopcrime::CrimeShouldSend(last, 5.0f, cur) != 1) Fail("the same theft set again (5.0 -> 20.0 between samples) must be sent");
    cur.expiry = 20.0f;
    if (coopcrime::CrimeShouldSend(last, 19.6f, cur) != 0) Fail("an expiry up by 0.4 s between samples must not be sent");
    cur = s; cur.crime = 5;
    if (coopcrime::CrimeShouldSend(last, 20.0f, cur) != 1) Fail("a new crime must be sent");
    cur = s; cur.victimUid = 9u;
    if (coopcrime::CrimeShouldSend(last, 20.0f, cur) != 1) Fail("a new victim must be sent");
    cur = s; cur.factionSid = "@peer";
    if (coopcrime::CrimeShouldSend(last, 20.0f, cur) != 1) Fail("a new victim faction must be sent");
    if (coopcrime::CrimeShouldSend(last, 20.0f, none) != 1) Fail("a cleared crime must be sent");
    coopcrime::CrimeState none2; none2.expiry = 30.0f;
    if (coopcrime::CrimeShouldSend(none, 0.0f, none2) != 0) Fail("no crime with a rising expiry must not be sent");
}

/* CRIME group: MSG_CRIME kinds. Kind 0 keeps the old bytes (no kind byte) and every decoder reads a message that ends with the sid
   as kind 0; kind 1 is the same layout plus the byte 1; kind 2 carries a standing event. Hostile tails are refused by their own
   reason, and bytes after a kind's fields are ignored. */
void t_crime_kinds_wire()
{
    coopcrime::CrimeState s;
    s.crime = 5; s.expiry = 30.0f; s.victimUid = 4242u; s.factionSid = "12-gamedata.base";
    std::vector<char> k0;
    if (!coopcrime::EncodeCrime(&k0, 9u, s)) { Fail("kind 0 did not encode"); return; }
    unsigned int uid = 0; coopcrime::CrimeState o; unsigned char kind = 99; coopcrime::CrimeRelEvent rel;
    if (coopcrime::DecodeCrimeMsg(&k0[0], k0.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeOk || kind != coopcrime::kCrimeKindState
        || uid != 9u || o.crime != 5 || o.factionSid != "12-gamedata.base")
        Fail("a message that ends with the sid must read as kind 0 with its fields");
    std::vector<char> k1;
    if (!coopcrime::EncodeCrimeKind(&k1, 9u, s, coopcrime::kCrimeKindCopyWrite)) { Fail("kind 1 did not encode"); return; }
    if (k1.size() != k0.size() + 1 || k1.back() != (char)1) Fail("kind 1 must be kind 0's bytes plus the byte 1");
    kind = 99;
    if (coopcrime::DecodeCrimeMsg(&k1[0], k1.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeOk || kind != coopcrime::kCrimeKindCopyWrite
        || uid != 9u || o.crime != 5 || o.victimUid != 4242u || o.expiry != 30.0f || o.factionSid != "12-gamedata.base")
        Fail("kind 1 must round-trip");
    unsigned int u2 = 0; coopcrime::CrimeState o2;
    if (coopcrime::DecodeCrime(&k1[0], k1.size(), &u2, &o2) != coopcrime::kCrimeDecodeOk || u2 != 9u || o2.crime != 5)
        Fail("the old decoder must read kind 1's fields (the kind byte ignored)");
    std::vector<char> k1x(k1); k1x.push_back((char)0x77); k1x.push_back((char)0x01);
    if (coopcrime::DecodeCrimeMsg(&k1x[0], k1x.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeOk || kind != 1) Fail("bytes after kind 1 must be ignored");
    std::vector<char> k9(k0); k9.push_back((char)3);
    if (coopcrime::DecodeCrimeMsg(&k9[0], k9.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeBadKind) Fail("kind 3 must be refused");
    std::vector<char> kc(k1); kc.resize(kc.size() - 2);
    if (coopcrime::DecodeCrimeMsg(&kc[0], kc.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeSidTrunc) Fail("a cut sid under kind 1 must be refused as before");

    coopcrime::CrimeRelEvent e;
    e.seq = 0xDEADBEEFu; e.ev = 4; e.mult = 1.5f; e.ownerSid = "12-gamedata.base"; e.otherSid = "@slot:2";
    std::vector<char> k2;
    if (!coopcrime::EncodeCrimeRelEvent(&k2, e)) { Fail("kind 2 did not encode"); return; }
    if (k2.size() != coopcrime::kCrimeFixedBytes + 16 + 14 + 7) Fail("kind 2 is " + I((int)k2.size()) + " bytes, not 54");
    kind = 99; rel = coopcrime::CrimeRelEvent();
    if (coopcrime::DecodeCrimeMsg(&k2[0], k2.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeOk || kind != coopcrime::kCrimeKindRelEvent
        || uid != 0u || o.crime != 0 || rel.seq != 0xDEADBEEFu || rel.ev != 4 || rel.mult != 1.5f || rel.ownerSid != "12-gamedata.base" || rel.otherSid != "@slot:2")
        Fail("kind 2 must round-trip");
    if (coopcrime::DecodeCrime(&k2[0], k2.size(), &u2, &o2) != coopcrime::kCrimeDecodeOk || u2 != 0u) Fail("the old decoder must read kind 2 as uid 0 (refused by its owner test)");
    for (size_t cut = 1; cut <= 14 + 7; ++cut)
    {
        if (cut == 14 + 7) break;   /* cutting all of kind 2's tail but the kind byte is tested below */
        if (coopcrime::DecodeCrimeMsg(&k2[0], k2.size() - cut, &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeRelTrunc)
        { Fail("kind 2 cut by " + I((int)cut) + " must be refused as truncated"); break; }
    }
    std::vector<char> kb(k2); kb[k2.size() - 8] = (char)97;   /* len2 = 97 */
    if (coopcrime::DecodeCrimeMsg(&kb[0], kb.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeRelTrunc) Fail("len2 97 must be refused");
    coopcrime::CrimeRelEvent bad(e); bad.ev = 17;
    std::vector<char> kn;
    if (coopcrime::EncodeCrimeRelEvent(&kn, bad) || !kn.empty()) Fail("event 17 must not encode, and nothing may be appended");
    std::vector<char> kv(k2); int ev17 = 17; std::memcpy(&kv[coopcrime::kCrimeFixedBytes + 16 + 5], &ev17, 4);
    if (coopcrime::DecodeCrimeMsg(&kv[0], kv.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeRelBad) Fail("event 17 must be refused");
    std::vector<char> km(k2); unsigned int inf = 0x7F800000u; std::memcpy(&km[coopcrime::kCrimeFixedBytes + 16 + 9], &inf, 4);
    if (coopcrime::DecodeCrimeMsg(&km[0], km.size(), &uid, &o, &kind, &rel) != coopcrime::kCrimeDecodeRelBad) Fail("an infinite multiplier must be refused");
    coopcrime::CrimeRelEvent big(e); big.mult = 101.0f;
    if (coopcrime::EncodeCrimeRelEvent(&kn, big)) Fail("a multiplier of 101 must not encode");
    coopcrime::CrimeRelEvent noOther(e); noOther.otherSid = "";
    if (coopcrime::EncodeCrimeRelEvent(&kn, noOther)) Fail("an empty player sid must not encode");
}

/* CRIME group: who sends a copy-side crime, what the owner does with it, and the owner's clear on a copy holding one. */
void t_crime_copy_write_decisions()
{
    /* copy's game: send only a crime the owner neither holds nor has been told of */
    if (coopcrime::CopyWriteShouldSend(5, 0, 0, 1.0e9f) != 1) Fail("[1] an ASSAULT on a copy the owner knows nothing of must be sent");
    if (coopcrime::CopyWriteShouldSend(5, 0, 5, 1.0e9f) != 0) Fail("[2] the same crime already waiting for the owner must not be sent again");
    if (coopcrime::CopyWriteShouldSend(5, 5, 0, 1.0e9f) != 0) Fail("[3] a crime the owner's state already holds must not be sent");
    if (coopcrime::CopyWriteShouldSend(11, 3, 0, 1.0e9f) != 1) Fail("[4] a TRESPASS on a copy whose owner holds a theft must be sent");
    if (coopcrime::CopyWriteShouldSend(11, 0, 5, 1.0e9f) != 1) Fail("[5] a new crime while another waits must be sent");
    if (coopcrime::CopyWriteShouldSend(0, 0, 0, 0.0f) != 0) Fail("[6] no crime must not be sent");
    if (coopcrime::CopyWriteShouldSend(65, 0, 0, 0.0f) != 0) Fail("[7] a crime above the enum must not be sent");
    /* owner's game */
    if (coopcrime::OwnerCopyWriteDecide(true, 5, false, false) != coopcrime::kOwnerWriteApply) Fail("[8] the owner must apply an assault on an NPC faction");
    if (coopcrime::OwnerCopyWriteDecide(false, 5, false, false) != coopcrime::kOwnerWriteNotMine) Fail("[9] a character not ours must be refused");
    if (coopcrime::OwnerCopyWriteDecide(true, 5, true, false) != coopcrime::kOwnerWritePlayerVictim) Fail("[10] a crime against a player faction must be held back (decision 4)");
    if (coopcrime::OwnerCopyWriteDecide(true, 3, false, true) != coopcrime::kOwnerWritePlayerVictim) Fail("[11] a crime naming a player's character must be held back (decision 4)");
    if (coopcrime::OwnerCopyWriteDecide(true, 0, false, false) != coopcrime::kOwnerWriteBad) Fail("[12] crime 0 must be refused");
    if (coopcrime::OwnerCopyWriteDecide(true, 65, false, false) != coopcrime::kOwnerWriteBad) Fail("[13] crime 65 must be refused");
    /* the owner's clear on the copy */
    if (coopcrime::CopyClearKeep(true, 5, false, 5) != 1) Fail("[14] a clear must keep a live crime recorded here and not yet confirmed");
    if (coopcrime::CopyClearKeep(true, 5, true, 5) != 0) Fail("[15] a mark past its crime's expiry must not keep the crime");
    if (coopcrime::CopyClearKeep(false, 0, false, 5) != 0) Fail("[16] no mark: the owner's clear applies");
    if (coopcrime::CopyClearKeep(true, 5, false, 3) != 0) Fail("[17] the copy no longer holds the marked crime: the clear applies");
    if (coopcrime::CopyClearKeep(true, 5, false, 0) != 0) Fail("[18] the copy holds nothing: the clear applies");
    if (!coopcrime::CopyMarkMet(5, 5)) Fail("[19] the owner's state holding the crime must end the mark");
    /* a crime the copy keeps re-recording, the owner's state for it near its end */
    if (coopcrime::CopyWriteShouldSend(11, 11, 0, coopcrime::kCopyReSendLeadSec) != 1) Fail("[21] a trespass re-recorded as the owner's runs out must be sent again");
    if (coopcrime::CopyWriteShouldSend(11, 11, 0, -2.0f) != 1) Fail("[22] a trespass re-recorded after the owner's ran out must be sent again");
    if (coopcrime::CopyWriteShouldSend(11, 11, 0, coopcrime::kCopyReSendLeadSec + 0.1f) != 0) Fail("[23] the owner's fresh state must not be re-sent");
    if (coopcrime::CopyWriteShouldSend(11, 11, 11, 0.0f) != 0) Fail("[24] a re-send already waiting for the owner must not be sent again");
    if (coopcrime::CopyWriteShouldSend(5, 5, 0, 0.5f) != 1) Fail("[25] an assault re-recorded at the end of the owner's must be sent again");
    if (coopcrime::CopyMarkMet(5, 0) || coopcrime::CopyMarkMet(5, 3) || coopcrime::CopyMarkMet(0, 0)) Fail("[20] a clear or another crime must not end the mark");
}

/* CRIME fold 2: a crime the copy holds that outranks the owner's (the engine refused the owner's) goes to the owner once, unless
   the owner's previous state held it. */
void t_crime_fold2_copy_held()
{
    using namespace coopcrime;
    if (CopyHeldNotOwners(11, 11, 5, 0) != 1) Fail("[1] a trespass held here, the owner's assault refused, the owner holding nothing before: must be noted");
    if (CopyHeldNotOwners(11, 11, 5, 5) != 1) Fail("[2] the owner's next assault refused again: still noted (the pending mark stops a second send)");
    if (CopyHeldNotOwners(11, 11, 5, 11) != 0) Fail("[3] a crime the owner's previous state held must never be sent back");
    if (CopyHeldNotOwners(0, 0, 5, 0) != 0) Fail("[4] nothing held: nothing noted");
    if (CopyHeldNotOwners(5, 5, 5, 0) != 0) Fail("[5] the owner's own crime written: nothing noted");
    if (CopyHeldNotOwners(11, 3, 3, 0) != 0) Fail("[6] the engine took the owner's crime: nothing noted");
    if (CopyHeldNotOwners(65, 65, 5, 0) != 0) Fail("[7] a crime above the enum: nothing noted");
    if (CopyWriteShouldSend(11, 5, 0, 20.0f) != 1) Fail("[8] the held trespass must be sent while the owner holds an assault");
    if (CopyWriteShouldSend(11, 5, 11, 20.0f) != 0) Fail("[9] the held trespass already sent must not be sent again");
    if (CopyWriteShouldSend(11, 0, 0, -250.0f) != 1) Fail("[10] a trespass after the owner's clear must be sent");
}

/* CRIME fold: the trespass stub's looker gate byte for byte (one cell, the cells at 0x400), a load keeps kind 2 only, and a
   copy's own death raises no event 5 of its own. */
void t_crime_fold1_trespass_load_death()
{
    using namespace coopcrime;
    {
        const unsigned long long site = 0x140858D13ULL;
        const int off = 0x400;
        static const unsigned char want[115] = {
            0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00,   /*   0 cmp qword [rax+0x250], 0 */
            0x0F, 0x85, 0x57, 0x00, 0x00, 0x00,               /*   8 jne 101 */
            0x48, 0x3B, 0x05, 0xF3, 0x03, 0x00, 0x00,         /*  14 cmp rax, [rip+0x3f3] (cell 0 at 0x408) */
            0x0F, 0x84, 0x0A, 0x00, 0x00, 0x00,               /*  21 je 37 */
            0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00,   /*  27 cmp qword [rax+0x250], 0 */
            0xEB, 0x40,                                       /*  35 jmp 101 */
            0x51,                                             /*  37 push rcx */
            0x48, 0x8B, 0x8D, 0x48, 0x01, 0x00, 0x00,         /*  38 mov rcx, [rbp+0x148] */
            0x48, 0x85, 0xC9,                                 /*  45 test rcx, rcx */
            0x74, 0x26,                                       /*  48 jz 88 */
            0x48, 0x8B, 0x49, 0x10,                           /*  50 mov rcx, [rcx+0x10] */
            0x48, 0x85, 0xC9,                                 /*  54 test rcx, rcx */
            0x74, 0x1D,                                       /*  57 jz 88 */
            0x48, 0x83, 0xB9, 0x50, 0x02, 0x00, 0x00, 0x00,   /*  59 cmp qword [rcx+0x250], 0 */
            0x59,                                             /*  67 pop rcx */
            0x74, 0x13,                                       /*  68 je 89 */
            0xF0, 0x48, 0xFF, 0x05, 0xFA, 0x04, 0x00, 0x00,   /*  70 lock inc qword [rip+0x4fa] (0x548, after forty cells) */
            0x48, 0x83, 0xB8, 0x50, 0x02, 0x00, 0x00, 0x00,   /*  78 cmp qword [rax+0x250], 0 */
            0xEB, 0x0D,                                       /*  86 jmp 101 */
            0x59,                                             /*  88 pop rcx */
            0xF0, 0x48, 0xFF, 0x05, 0x9F, 0x03, 0x00, 0x00,   /*  89 lock inc qword [rip+0x39f] (0x400) */
            0x48, 0x83, 0xF8, 0x00,                           /*  97 cmp rax, 0 */
            0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,               /* 101 jmp [rip] */
            0x1B, 0x8D, 0x85, 0x40, 0x01, 0x00, 0x00, 0x00    /* 107 site + 8 */
        };
        std::vector<unsigned char> p(0x1000, 0xCC);
        const int len = TrespassStubBuild(&p[0], site, 1, off);
        if (len != 115) { Fail("trespass stub: one cell is 115 bytes, got " + I(len)); return; }
        for (int i = 0; i < 115; ++i)
            if (p[i] != want[i]) { Fail("trespass stub: byte " + I(i) + " is " + I((int)p[i]) + ", the layout says " + I((int)want[i])); break; }
        if (p[115] != 0xCC) Fail("trespass stub: nothing written past byte 115");
        std::vector<unsigned char> q(0x1000, 0xCC);
        if (TrespassStubBuild(&q[0], site, kSightCellCap, off) != kTrespassStubLen || kTrespassStubLen > off)
            Fail("trespass stub: forty cells are kTrespassStubLen long and end before the cells");
        /* the head and the cells are the sight stub's own */
        std::vector<unsigned char> s(0x1000, 0xCC);
        SightStubBuild(&s[0], site, 1, off);
        if (std::memcmp(&s[0], &p[0], 8) != 0 || std::memcmp(&s[14], &p[14], 7) != 0 || std::memcmp(&s[27], &p[27], 8) != 0)
            Fail("trespass stub: its test and cell compare differ from the sight stub's");
    }
    {
        CrimeState s; s.crime = 11; s.expiry = 3.0f; s.victimUid = 0; s.factionSid = "Holy Nation";
        std::vector<char> k0, k1, k2;
        EncodeCrime(&k0, 0x00400001u, s);
        EncodeCrimeKind(&k1, 0x00400001u, s, kCrimeKindCopyWrite);
        CrimeRelEvent e; e.seq = 7u; e.ev = 4; e.mult = 1.0f; e.ownerSid = "Holy Nation"; e.otherSid = "@slot:1";
        if (!EncodeCrimeRelEvent(&k2, e) || k0.empty() || k1.empty()) { Fail("load: the three kinds did not encode"); return; }
        if (CrimeMsgKeptThroughLoad(&k0[0], k0.size())) Fail("load: a kind 0 state must be dropped");
        if (CrimeMsgKeptThroughLoad(&k1[0], k1.size())) Fail("load: a kind 1 copy-side crime must be dropped");
        if (!CrimeMsgKeptThroughLoad(&k2[0], k2.size())) Fail("load: a kind 2 standing event must be kept");
        if (CrimeMsgKeptThroughLoad(&k2[0], 10) || CrimeMsgKeptThroughLoad(0, 0)) Fail("load: a message that does not decode must be dropped");
    }
    if (CopyDeathEvDecide(true, 5, true) != kDeathEvSkipEngine) Fail("death: a copy's event 5 on this game's player pair must be skipped");
    if (CopyDeathEvDecide(true, 5, false) != kDeathEvNotSent) Fail("death: a copy's event 5 against a stand-in must not be sent");
    if (CopyDeathEvDecide(false, 5, true) != kDeathEvPass || CopyDeathEvDecide(false, 5, false) != kDeathEvPass)
        Fail("death: event 5 outside a copy's death must pass");
    if (CopyDeathEvDecide(true, 4, true) != kDeathEvPass || CopyDeathEvDecide(true, 1, false) != kDeathEvPass)
        Fail("death: other events inside a copy's death must pass");
}

/* CRIME group: kind 2 - which events go, and the receiver's duplicate test. */
void t_crime_relev_dedupe()
{
    const int go[] = { 1, 2, 3, 4, 5, 8, 12 };
    const int no[] = { 0, 6, 7, 9, 10, 11, 13, -1 };
    for (size_t i = 0; i < sizeof(go) / sizeof(go[0]); ++i) if (!coopcrime::RelEventForwarded(go[i])) Fail("event " + I(go[i]) + " must be forwarded");
    for (size_t i = 0; i < sizeof(no) / sizeof(no[0]); ++i) if (coopcrime::RelEventForwarded(no[i])) Fail("event " + I(no[i]) + " must not be forwarded");
    coopcrime::RelEvSeen seen;
    if (seen.Has(1u, 7u)) Fail("an empty book must hold nothing");
    seen.Add(1u, 7u);
    if (!seen.Has(1u, 7u)) Fail("a seq applied must be a duplicate the second time");
    if (seen.Has(2u, 7u)) Fail("the same seq from another sender is not a duplicate");
    if (seen.Has(1u, 8u)) Fail("the next seq is not a duplicate");
    for (unsigned int s = 100; s < 100 + (unsigned int)coopcrime::kRelEvSeenCap; ++s) seen.Add(1u, s);
    if (seen.Has(1u, 7u)) Fail("the oldest pair must leave a full book");
    if (!seen.Has(1u, 100u) || !seen.Has(1u, 100u + (unsigned int)coopcrime::kRelEvSeenCap - 1)) Fail("the last kRelEvSeenCap pairs must be held");
    if (seen.n != coopcrime::kRelEvSeenCap) Fail("a full book holds exactly kRelEvSeenCap pairs");
    seen.Clear();
    if (seen.Has(1u, 100u)) Fail("a cleared book holds nothing");
}

/* P3 (read-parity3 GAP 3; user saw it T244). MSG_SAY carries the exact text the owner's engine said. Round trip
   bit-for-bit (an embedded NUL included), an empty line, the 512-byte cap on both sides, and hostile / truncated
   payloads each refused by their own reason. */
void t_p3_say_round_trips_and_refuses_hostile_lengths()
{
    std::vector<char> b;
    if (!coopsay::EncodeSay(&b, 3204448269u, "Hey, you!", 9)) { Fail("a short line did not encode"); return; }
    if (b.size() != (size_t)(4 + 2 + 9)) Fail("the encoded SAY is " + I((int)b.size()) + " bytes, not 15");
    unsigned int uid = 0;
    std::string t;
    if (coopsay::DecodeSay(&b[0], b.size(), &uid, &t) != coopsay::kSayDecodeOk || uid != 3204448269u || t != "Hey, you!")
        Fail("a whole SAY did not round-trip");
    /* An embedded NUL survives: the text is bytes, not a C string. */
    std::vector<char> z;
    if (!coopsay::EncodeSay(&z, 5u, "a\0b", 3)) Fail("a line with a NUL did not encode");
    else if (coopsay::DecodeSay(&z[0], z.size(), &uid, &t) != coopsay::kSayDecodeOk || t.size() != 3 || t[1] != '\0' || t[2] != 'b')
        Fail("a line with a NUL did not round-trip");
    /* Empty text is a legal line. */
    std::vector<char> e;
    if (!coopsay::EncodeSay(&e, 7u, "", 0) || e.size() != 6) Fail("an empty line must encode as 6 bytes");
    else if (coopsay::DecodeSay(&e[0], e.size(), &uid, &t) != coopsay::kSayDecodeOk || uid != 7u || !t.empty())
        Fail("an empty line did not round-trip");
    /* The cap: 512 goes, 513 does not, and nothing is appended when it does not. */
    const std::string at(512, 'a');
    const std::string over(513, 'a');
    std::vector<char> g;
    if (!coopsay::EncodeSay(&g, 1u, at.data(), at.size())) Fail("512 bytes must encode");
    else if (coopsay::DecodeSay(&g[0], g.size(), &uid, &t) != coopsay::kSayDecodeOk || t != at) Fail("512 bytes did not round-trip");
    std::vector<char> g2;
    if (coopsay::EncodeSay(&g2, 1u, over.data(), over.size()) || !g2.empty()) Fail("513 bytes must be refused and append nothing");
    /* Hostile lengths. */
    std::vector<char> h(b.begin(), b.begin() + 6);
    h[4] = (char)0xFF; h[5] = (char)0xFF;
    if (coopsay::DecodeSay(&h[0], h.size(), &uid, &t) != coopsay::kSayDecodeTooLong) Fail("a 65535-byte length must be refused as too long");
    std::vector<char> tr(b);
    unsigned short twenty = 20; std::memcpy(&tr[4], &twenty, 2);
    if (coopsay::DecodeSay(&tr[0], tr.size(), &uid, &t) != coopsay::kSayDecodeTextTrunc) Fail("a length past the payload's end must be refused");
    if (coopsay::DecodeSay(&b[0], 5, &uid, &t) != coopsay::kSayDecodeTooShort) Fail("5 bytes must be too short");
    if (coopsay::DecodeSay(0, 0, &uid, &t) != coopsay::kSayDecodeTooShort) Fail("an empty payload must be too short");
    /* The length test is a minimum: trailing bytes are ignored, not refused. */
    std::vector<char> tail(b); tail.push_back('X');
    if (coopsay::DecodeSay(&tail[0], tail.size(), &uid, &t) != coopsay::kSayDecodeOk || t != "Hey, you!") Fail("a trailing byte must be ignored");
}

/* P3-b (review-p3). A copy the other game drives starts no chat of its own: sendEvent runs for it only for
   EV_PLAYER_TALK_TO_ME (1); a character this game authors, or one with no uid, takes every event. */
void t_p3b_send_event_gate_blocks_a_copys_own_chats()
{
    if (coopsay::kEvPlayerTalkToMe != 1) Fail("EV_PLAYER_TALK_TO_ME must be 1 (sendEvent 0x683F00 routes 1 to startPlayerConversation)");
    if (coopsay::SendEventAllowed(1, 1) != 1) Fail("the player talking to a copy must still reach sendEvent");
    for (int ev = 0; ev < 80; ++ev)
    {
        if (coopsay::SendEventAllowed(0, ev) != 1) Fail("a non-copy must take event " + I(ev));
        if (ev != 1 && coopsay::SendEventAllowed(1, ev) != 0) Fail("a copy must NOT take event " + I(ev));
    }
}

/* P26 stage 0. Every conversation start is counted under the target's kind: this game's own player character, the other
   player's character (the stand-in faction, or a player-faction character whose uid another game owns), an NPC this
   game drives (or unreplicated), an NPC copy, none, unreadable. Events bucket 0..127 and 'other'. */
void t_p26s0_talk_target_kind_splits_mine_partner_npc()
{
    using namespace coopsay;
    if (TalkTargetKind(0, 1, 1, 1, 1, 1) != kTalkTargetNone) Fail("no character must be none whatever else is set");
    if (TalkTargetKind(1, 0, 1, 1, 1, 0) != kTalkTargetUnread) Fail("an unreadable faction must be unread");
    if (TalkTargetKind(1, 1, 1, 0, 1, 1) != kTalkTargetMine) Fail("own player faction with an own uid must be mine");
    if (TalkTargetKind(1, 1, 1, 0, 0, 0) != kTalkTargetMine) Fail("own player faction with no uid must be mine");
    if (TalkTargetKind(1, 1, 0, 1, 1, 0) != kTalkTargetPartnerCopy) Fail("the stand-in faction must be partnerCopy");
    if (TalkTargetKind(1, 1, 0, 1, 0, 0) != kTalkTargetPartnerCopy) Fail("the stand-in faction with no uid must still be partnerCopy");
    if (TalkTargetKind(1, 1, 0, 1, 1, 1) != kTalkTargetPartnerCopy) Fail("the stand-in faction wins over an own uid");
    if (TalkTargetKind(1, 1, 1, 0, 1, 0) != kTalkTargetPartnerCopy) Fail("a player-faction character another game owns must be partnerCopy");
    if (TalkTargetKind(1, 1, 0, 0, 1, 1) != kTalkTargetNpcMine) Fail("an NPC this game drives must be npcMine");
    if (TalkTargetKind(1, 1, 0, 0, 0, 0) != kTalkTargetNpcMine) Fail("an unreplicated NPC must be npcMine");
    if (TalkTargetKind(1, 1, 0, 0, 1, 0) != kTalkTargetNpcCopy) Fail("an NPC the other game drives must be npcCopy");
    for (int a = 0; a <= 5; ++a)
    {
        if (TalkTargetName(a) == 0 || std::string(TalkTargetName(a)) == "?") Fail("kind " + I(a) + " must have a name");
        for (int b = 0; b < a; ++b)
            if (std::string(TalkTargetName(a)) == TalkTargetName(b)) Fail("kinds " + I(a) + " and " + I(b) + " share a name");
    }
    if (std::string(TalkTargetName(99)) != "?") Fail("an unknown kind must read ?");
    if (TalkEventBucket(0) != 0 || TalkEventBucket(1) != 1 || TalkEventBucket(0x43) != 0x43 || TalkEventBucket(127) != 127)
        Fail("events 0..127 must each have their own bucket");
    if (TalkEventBucket(128) != kTalkEventBuckets - 1 || TalkEventBucket(-1) != kTalkEventBuckets - 1 || TalkEventBucket(100000) != kTalkEventBuckets - 1)
        Fail("anything else must land in the last (other) bucket");
}

/* P26 stage 0. `talktest [npcUid|near] [targetUid|near] [event]`: defaults near near 1; uids non-zero; event 0..255, 0x hex. */
void t_p26s0_talktest_parse()
{
    using namespace coopsay;
    unsigned int n = 9, t = 9; int e = 9;
    if (!TalkTestParse("", &n, &t, &e) || n != 0 || t != 0 || e != 1) Fail("no arguments must be near near 1");
    if (!TalkTestParse("near near", &n, &t, &e) || n != 0 || t != 0 || e != 1) Fail("near near must parse");
    if (!TalkTestParse("123 near", &n, &t, &e) || n != 123 || t != 0 || e != 1) Fail("an NPC uid must parse");
    if (!TalkTestParse("  near   456  3 ", &n, &t, &e) || n != 0 || t != 456 || e != 3) Fail("extra spaces, target uid and event must parse");
    if (!TalkTestParse("12 34 0x42", &n, &t, &e) || n != 12 || t != 34 || e != 0x42) Fail("a hex event must parse");
    if (!TalkTestParse("near near 0", &n, &t, &e) || e != 0) Fail("event 0 must parse");
    if (TalkTestParse("near near 256", &n, &t, &e)) Fail("event 256 must be refused");
    if (TalkTestParse("0 near", &n, &t, &e)) Fail("uid 0 must be refused");
    if (TalkTestParse("abc", &n, &t, &e)) Fail("a word must be refused");
    if (TalkTestParse("1 2 3 4", &n, &t, &e)) Fail("four arguments must be refused");
    if (TalkTestParse("near 4294967296", &n, &t, &e)) Fail("a uid above 32 bits must be refused");
    if (TalkTestParse("near near 0x", &n, &t, &e)) Fail("a bare 0x must be refused");
}

/* P25 T729: the name levers' grammar - talktest nearestname and playerteleport near name (spaces, '_', case, refusals). */
void t_p25_name_levers_parse()
{
    using namespace coopsay;
    if (TalkPersonKey("Bar Thug") != "bar thug" || TalkPersonKey("  BAR__thug\t") != "bar thug" || TalkPersonKey("Bar_Thug") != "bar thug")
        Fail("a name must key lower case with one space per run of spaces / tabs / '_'");
    if (TalkPersonKey(" _ ") != "" || TalkPersonKey("Samurai") != "samurai") Fail("an empty key / a one-word key");
    std::string nm; unsigned int t = 9; int ev = 9, own = 9;   /* P25 T729b: own */
    if (!TalkTestNameParse("nearestname Bar Thug 4194305 1", &nm, &t, &ev, &own) || nm != "bar thug" || t != 4194305 || ev != 1)
        Fail("a two-word name, the target and the event must parse");
    if (!TalkTestNameParse("  nearestname  bar_THUG  0x10 ", &nm, &t, &ev, &own) || nm != "bar thug" || t != 16 || ev != 1)
        Fail("'_', case and spacing; hex target; the event defaults to 1");
    if (!TalkTestNameParse("nearestname Bar Thug 5 0x2", &nm, &t, &ev, &own) || ev != 2) Fail("a hex event must parse");
    if (!TalkTestNameParse("nearestname Samurai 155", &nm, &t, &ev, &own) || nm != "samurai" || t != 155) Fail("a one-word name must parse");
    if (!TalkTestNameParse("nearestname Very Long Named Guard Of The Hub 7 3", &nm, &t, &ev, &own) || nm != "very long named guard of the hub" || ev != 3)
        Fail("a seven-word name must parse");
    if (TalkTestNameParse("nearestname 4 1", &nm, &t, &ev, &own)) Fail("no name must be refused");
    if (TalkTestNameParse("nearestname Bar Thug", &nm, &t, &ev, &own)) Fail("no target must be refused");
    if (TalkTestNameParse("nearestname Bar Thug 0 1", &nm, &t, &ev, &own)) Fail("target 0 must be refused");
    if (TalkTestNameParse("nearestname Bar Thug 5 256", &nm, &t, &ev, &own)) Fail("event 256 must be refused");
    if (TalkTestNameParse("nearestname Bar Thug 5 1 x", &nm, &t, &ev, &own) || TalkTestNameParse("nearestname Bar Thug 5 1 2", &nm, &t, &ev, &own))
        Fail("trailing words must be refused");
    if (TalkTestNameParse("nearestname Bar_Thug {uid:B} 1", &nm, &t, &ev, &own)) Fail("an unresolved placeholder must be refused, never part of the name");
    if (TalkTestNameParse("nearestname _ 5", &nm, &t, &ev, &own)) Fail("an empty name key must be refused");
    if (TalkTestNameParse("nearest Bar 5", &nm, &t, &ev, &own) || TalkTestNameParse("4 5 1", &nm, &t, &ev, &own)) Fail("only the nearestname form parses here");
    if (TalkTestNameParse("nearestname a b c d e f g h i j k l m n o p 5", &nm, &t, &ev, &own)) Fail("seventeen words must be refused");
    /* P25 T729b: [copy | mine] */
    if (!TalkTestNameParse("nearestname Bar_Thug 5 1", &nm, &t, &ev, &own) || own != -1) Fail("no copy / mine word = any owner (-1)");
    if (!TalkTestNameParse("nearestname Bar_Thug 5 1 copy", &nm, &t, &ev, &own) || own != 0 || ev != 1 || nm != "bar thug") Fail("copy must parse as 0");
    if (!TalkTestNameParse("nearestname Bar Thug 5 mine", &nm, &t, &ev, &own) || own != 1 || ev != 1 || t != 5) Fail("mine without an event must parse as 1");
    if (TalkTestNameParse("nearestname Bar_Thug 5 copy mine", &nm, &t, &ev, &own) || TalkTestNameParse("nearestname Bar_Thug 5 copy 1", &nm, &t, &ev, &own)
        || TalkTestNameParse("nearestname Bar_Thug 5 1 Copy", &nm, &t, &ev, &own)) Fail("two owner words / an event after the owner word / 'Copy' must be refused");
    int units = 9, dy = 9;
    if (!TalkTeleportNearNameParse("name Bar Thug 120 maxdy 30", &nm, &units, &dy) || nm != "bar thug" || units != 120 || dy != 30)
        Fail("name with spaces, units and maxdy must parse");
    if (!TalkTeleportNearNameParse("name BAR_thug 120", &nm, &units, &dy) || nm != "bar thug" || units != 120 || dy != -1)
        Fail("name with '_', no maxdy must parse");
    if (TalkTeleportNearNameParse("name Bar Thug", &nm, &units, &dy) || TalkTeleportNearNameParse("name 120", &nm, &units, &dy))
        Fail("no units / no name must be refused");
    if (TalkTeleportNearNameParse("name Bar Thug 0", &nm, &units, &dy) || TalkTeleportNearNameParse("name Bar Thug 2001", &nm, &units, &dy))
        Fail("units 0 / 2001 must be refused");
    if (TalkTeleportNearNameParse("name Bar Thug 120 maxdy", &nm, &units, &dy) || TalkTeleportNearNameParse("name Bar Thug 120 maxdy 0", &nm, &units, &dy)
        || TalkTeleportNearNameParse("name Bar Thug 120 maxdy 501", &nm, &units, &dy)) Fail("maxdy without n / 0 / 501 must be refused");
    if (TalkTeleportNearNameParse("name Bar Thug maxdy 30", &nm, &units, &dy)) Fail("maxdy inside the name (units missing) must be refused");
    if (TalkTeleportNearNameParse("name Bar Thug 120 30", &nm, &units, &dy)) Fail("a trailing number must be refused");
    if (TalkTeleportNearNameParse("nearest Bar_Thug 120", &nm, &units, &dy) || TalkTeleportNearNameParse("name {uid:A} 120", &nm, &units, &dy))
        Fail("the nearest form / a placeholder name must be refused here");
    unsigned int u = 9; std::string k;
    if (TalkTeleportNearParseDy("name Bar Thug 120", &u, &k, &units, &dy)) Fail("the uid / nearest parser must refuse the name form");
}

/* P26 stage 6 talksight: the name key, the talksight grammar and playerteleport near's. */
void t_p26s6_talksight_parse()
{
    using namespace coopsay;
    if (TalkNameKey("Bag check (Shek)") != "bag_check_shek") Fail("a dialogue name must key as bag_check_shek");
    if (TalkNameKey("Bag_check_Shek") != "bag_check_shek") Fail("the command token must key the same");
    if (TalkNameKey("  (x)  ") != "x" || TalkNameKey("()") != "") Fail("no separator at either end; none-alnum keys empty");
    if (TalkNameKey("9548-Dialogue.mod") != "9548_dialogue_mod") Fail("a string id must key too");
    unsigned int n = 9, t = 9; std::string k = "junk"; int s = 9, w = 9;
    if (!TalkSightParse("123 456", &n, &k, &t, &s, &w) || n != 123 || !k.empty() || t != 456 || s != 120 || w != 0) Fail("uid uid must parse, 120 s default");
    if (!TalkSightParse("nearest Bag_check_Shek 456 300 walkover", &n, &k, &t, &s, &w) || n != 0 || k != "bag_check_shek" || t != 456 || s != 300 || w != 1)
        Fail("nearest name target seconds walkover must parse");
    if (!TalkSightParse(" nearest  Bag_check_Shek 456 walkover ", &n, &k, &t, &s, &w) || s != 120 || w != 1) Fail("walkover without seconds must parse");
    if (!TalkSightParse("12 34 0x10", &n, &k, &t, &s, &w) || s != 16) Fail("hex seconds must parse");
    if (TalkSightParse("123", &n, &k, &t, &s, &w)) Fail("no target must be refused");
    if (TalkSightParse("nearest Bag_check_Shek", &n, &k, &t, &s, &w)) Fail("nearest without a target must be refused");
    if (TalkSightParse("nearest () 456", &n, &k, &t, &s, &w)) Fail("an empty name key must be refused");
    if (TalkSightParse("0 456", &n, &k, &t, &s, &w) || TalkSightParse("123 0", &n, &k, &t, &s, &w)) Fail("uid 0 must be refused");
    if (TalkSightParse("1 2 0", &n, &k, &t, &s, &w) || TalkSightParse("1 2 3601", &n, &k, &t, &s, &w)) Fail("seconds 0 / 3601 must be refused");
    if (TalkSightParse("1 2 3 walkover x", &n, &k, &t, &s, &w) || TalkSightParse("1 2 walkover 3", &n, &k, &t, &s, &w)) Fail("trailing tokens must be refused");
    if (TalkSightParse("1 2 3 4 5 6 7", &n, &k, &t, &s, &w)) Fail("seven tokens must be refused");
    unsigned int u = 9; int units = 9;
    if (!TalkTeleportNearParse("123 150", &u, &k, &units) || u != 123 || !k.empty() || units != 150) Fail("uid units must parse");
    if (!TalkTeleportNearParse("nearest Bag_check_Shek 150", &u, &k, &units) || u != 0 || k != "bag_check_shek" || units != 150) Fail("nearest name units must parse");
    if (TalkTeleportNearParse("123", &u, &k, &units) || TalkTeleportNearParse("nearest x", &u, &k, &units)) Fail("no units must be refused");
    if (TalkTeleportNearParse("123 0", &u, &k, &units) || TalkTeleportNearParse("123 2001", &u, &k, &units)) Fail("units 0 / 2001 must be refused");
    if (TalkTeleportNearParse("0 150", &u, &k, &units) || TalkTeleportNearParse("123 150 7", &u, &k, &units)) Fail("uid 0 / extra token must be refused");
    int dy = 9;   /* P26lvl maxdy */
    if (!TalkTeleportNearParseDy("nearest Bag_check_Shek 160 maxdy 8", &u, &k, &units, &dy) || u != 0 || k != "bag_check_shek" || units != 160 || dy != 8) Fail("nearest name units maxdy must parse");
    if (!TalkTeleportNearParseDy("123 70", &u, &k, &units, &dy) || u != 123 || !k.empty() || units != 70 || dy != -1) Fail("no maxdy must parse as -1");
    if (!TalkTeleportNearParseDy("123 70 maxdy 500", &u, &k, &units, &dy) || dy != 500) Fail("maxdy 500 must parse");
    if (TalkTeleportNearParseDy("123 70 maxdy", &u, &k, &units, &dy) || TalkTeleportNearParseDy("123 70 maxdy 0", &u, &k, &units, &dy)
        || TalkTeleportNearParseDy("123 70 maxdy 501", &u, &k, &units, &dy) || TalkTeleportNearParseDy("123 70 maxdy 8 x", &u, &k, &units, &dy)
        || TalkTeleportNearParseDy("123 70 dy 8", &u, &k, &units, &dy) || TalkTeleportNearParseDy("nearest x maxdy 8", &u, &k, &units, &dy)) Fail("a malformed maxdy must be refused");
    if (TalkTeleportNearParse("123 70 maxdy 8", &u, &k, &units)) Fail("the form without maxdy must refuse a maxdy");
}

/* P26 stages 1-3. MSG_TALK: PROMPT / ANSWER / END round-trip every field; bytes after the last field are ignored; every
   truncation, an unknown kind, an over-cap string and more than 10 replies are refused; the encoder refuses over-cap input
   and appends nothing. */
void t_p26s1_talkwire_round_trip_caps_and_refusals()
{
    using namespace cooptalk;
    TalkMsg p;
    p.kind = kTalkPrompt; p.convId = 7; p.npcUid = 0x00400001u; p.targetUid = 0x00800002u; p.event = 1;
    p.lineSid = "1234-rebirth.mod"; p.npcText = "Hand over your stuff.";
    p.replyIds.push_back("r1"); p.replyTexts.push_back("No.");
    p.replyIds.push_back("r2"); p.replyTexts.push_back("Fine, take it.");
    p.deadlineMs = 30000;
    std::vector<char> b;
    if (!EncodeTalk(&b, p)) { Fail("a PROMPT did not encode"); return; }
    TalkMsg q;
    if (DecodeTalk(&b[0], b.size(), &q) != kTalkDecodeOk) { Fail("a PROMPT did not decode"); return; }
    if (q.kind != kTalkPrompt || q.convId != 7 || q.npcUid != 0x00400001u || q.targetUid != 0x00800002u || q.event != 1
        || q.lineSid != p.lineSid || q.npcText != p.npcText || q.replyIds != p.replyIds || q.replyTexts != p.replyTexts
        || q.deadlineMs != 30000)
        Fail("a PROMPT must round-trip every field");
    std::vector<char> tail = b;
    tail.push_back('x'); tail.push_back('y');
    TalkMsg q2;
    if (DecodeTalk(&tail[0], tail.size(), &q2) != kTalkDecodeOk || q2.deadlineMs != 30000) Fail("bytes after the last field must be ignored");
    for (size_t n = 0; n < b.size(); ++n)
    {
        TalkMsg r;
        if (DecodeTalk(n ? &b[0] : "", n, &r) == kTalkDecodeOk) { Fail("a truncated PROMPT must not decode"); break; }
    }
    TalkMsg a;
    a.kind = kTalkAnswer; a.convId = 7; a.npcUid = 1; a.targetUid = 2; a.result = kTalkAnsReply; a.index = 1; a.replyId = "r2";
    std::vector<char> ab;
    TalkMsg qa;
    if (!EncodeTalk(&ab, a) || DecodeTalk(&ab[0], ab.size(), &qa) != kTalkDecodeOk || qa.kind != kTalkAnswer
        || qa.result != kTalkAnsReply || qa.index != 1 || qa.replyId != "r2" || qa.convId != 7)
        Fail("an ANSWER must round-trip");
    a.result = kTalkAnsClosed; a.index = -1; a.replyId = "";
    std::vector<char> ac;
    TalkMsg qc;
    if (!EncodeTalk(&ac, a) || DecodeTalk(&ac[0], ac.size(), &qc) != kTalkDecodeOk || qc.result != kTalkAnsClosed || qc.index != -1)
        Fail("a CLOSED answer must keep index -1");
    TalkMsg e;
    e.kind = kTalkEnd; e.convId = 9; e.npcUid = 3; e.targetUid = 4; e.reason = kTalkEndAnswered;
    std::vector<char> eb;
    TalkMsg qe;
    if (!EncodeTalk(&eb, e) || eb.size() != 20 || DecodeTalk(&eb[0], eb.size(), &qe) != kTalkDecodeOk || qe.kind != kTalkEnd
        || qe.reason != kTalkEndAnswered || qe.convId != 9)
        Fail("an END must be 20 bytes (P25: + reqId, why, endsType) and round-trip");
    TalkMsg big = p;
    big.npcText = std::string(513, 'a');
    std::vector<char> bb;
    if (EncodeTalk(&bb, big) || !bb.empty()) Fail("a 513-byte text must be refused and append nothing");
    big.npcText = std::string(512, 'a');
    if (!EncodeTalk(&bb, big)) Fail("a 512-byte text must encode");
    TalkMsg longId = p;
    longId.lineSid = std::string(129, 's');
    std::vector<char> lb;
    if (EncodeTalk(&lb, longId)) Fail("a 129-byte line id must be refused");
    TalkMsg many = p;
    while (many.replyIds.size() < 11) { many.replyIds.push_back("x"); many.replyTexts.push_back("y"); }
    std::vector<char> mb;
    if (EncodeTalk(&mb, many)) Fail("11 replies must be refused");
    TalkMsg odd = p;
    odd.replyTexts.pop_back();
    std::vector<char> ob;
    if (EncodeTalk(&ob, odd)) Fail("reply ids and texts of different counts must be refused");
    TalkMsg bad;
    bad.kind = 9;   /* P26 stage 5: 4 / 5 are ACT / ACT_RESULT; P25: 6 is REQUEST; T-737: 7 / 8 are CURSOR_ASK / CURSOR_ANSWER */
    std::vector<char> kb;
    if (EncodeTalk(&kb, bad)) Fail("kind 9 must be refused");
    std::vector<char> k2 = ab;
    k2[0] = 9;
    TalkMsg qk;
    if (DecodeTalk(&k2[0], k2.size(), &qk) != kTalkDecodeBadKind) Fail("an unknown kind byte must decode as BadKind");
    TalkMsg none = p;
    none.replyIds.clear(); none.replyTexts.clear();
    std::vector<char> nb;
    if (!EncodeTalk(&nb, none)) { Fail("a PROMPT with no replies must encode"); return; }
    const size_t at = 13 + 1 + 2 + none.lineSid.size() + 2 + none.npcText.size();
    if (at >= nb.size() || nb[at] != 0) { Fail("the reply count byte is not where the layout says"); return; }
    nb[at] = 11;
    TalkMsg qn;
    if (DecodeTalk(&nb[0], nb.size(), &qn) != kTalkDecodeTooMany) Fail("a reply count of 11 must decode as TooMany");
    std::vector<char> tl = ab;
    const unsigned short over = 129;
    std::memcpy(&tl[18], &over, 2);
    tl.resize(20);
    tl.insert(tl.end(), (size_t)129, 'z');
    TalkMsg qt;
    if (DecodeTalk(&tl[0], tl.size(), &qt) != kTalkDecodeTooLong) Fail("a 129-byte reply id must decode as TooLong");
    if (TalkClip(std::string(600, 'z'), 512).size() != 512 || TalkClip("abc", 512) != "abc") Fail("TalkClip must cut at the cap only");
    if (std::string(TalkAnswerName(kTalkAnsBusy)) != "BUSY" || std::string(TalkEndName(kTalkEndAnswered)) != "ANSWERED"
        || std::string(TalkKindName(kTalkPrompt)) != "PROMPT" || std::string(TalkAnswerName(99)) != "?")
        Fail("the names must read as the log shows them");
    /* P26 stage 4: the two new END reasons round-trip and read as the log shows them */
    {
        TalkMsg e4;
        e4.kind = kTalkEnd; e4.convId = 11; e4.npcUid = 5; e4.targetUid = 6; e4.reason = kTalkEndTimeout;
        std::vector<char> b4;
        TalkMsg q4;
        if (!EncodeTalk(&b4, e4) || b4.empty() || DecodeTalk(&b4[0], b4.size(), &q4) != kTalkDecodeOk || q4.reason != kTalkEndTimeout
            || q4.convId != 11)
            Fail("END TIMEOUT must round-trip");
        if (std::string(TalkEndName(kTalkEndTimeout)) != "TIMEOUT" || std::string(TalkEndName(kTalkEndNotForwardable)) != "NOT_FORWARDABLE")
            Fail("the stage-4 END names must read as the log shows them");
    }
}

/* P26 stage 5. MSG_TALK kinds 4 ACT / 5 ACT_RESULT: every field round-trips (negative values and an empty list too); bytes after the
   last field are ignored; every truncation decodes as Trunc (under 13 bytes TooShort) and leaves the output untouched; over-cap lists
   and strings, lists of different lengths and a type over 16 bits are refused by the encoder (nothing appended); an over-cap count
   decodes as TooMany; kind 6 is BadKind; the names read as the log shows them; and the per-action table puts every type where the
   _doActions reading put it (58 DA_KNOCKOUT is the NPC's own knockout - NPC-side). */
void t_p26s5_talkwire_act_round_trip_caps_and_side_table()
{
    using namespace cooptalk;
    TalkMsg a;
    a.kind = kTalkAct; a.convId = 3; a.npcUid = 0x00400001u; a.targetUid = 0x00800002u; a.seq = 17; a.lineSid = "6940-Dialogue.mod";
    a.actTypes.push_back(8); a.actValues.push_back(50);
    a.actTypes.push_back(9); a.actValues.push_back(-7);
    a.itemSids.push_back("42-gamedata.base"); a.itemValues.push_back(3);
    a.relSids.push_back("17-gamedata.base"); a.relValues.push_back(-25);
    a.carriedUid = 0x00C00003u;
    std::vector<char> b;
    if (!EncodeTalk(&b, a)) { Fail("an ACT did not encode"); return; }
    const size_t want = 13 + 4 + 2 + a.lineSid.size() + 1 + 2 * 6 + 1 + (2 + 16 + 4) + 1 + (2 + 16 + 4) + 4;
    if (b.size() != want) Fail("the ACT's size is not the layout's");
    TalkMsg q;
    if (DecodeTalk(&b[0], b.size(), &q) != kTalkDecodeOk) { Fail("an ACT did not decode"); return; }
    if (q.kind != kTalkAct || q.convId != 3 || q.npcUid != 0x00400001u || q.targetUid != 0x00800002u || q.seq != 17 || q.lineSid != a.lineSid
        || q.actTypes != a.actTypes || q.actValues != a.actValues || q.itemSids != a.itemSids || q.itemValues != a.itemValues
        || q.relSids != a.relSids || q.relValues != a.relValues || q.carriedUid != 0x00C00003u)
        Fail("an ACT must round-trip every field (a negative value too, and the carried person's uid)");
    std::vector<char> tail = b;
    tail.push_back('x');
    TalkMsg qt;
    if (DecodeTalk(&tail[0], tail.size(), &qt) != kTalkDecodeOk || qt.relValues.size() != 1 || qt.relValues[0] != -25) Fail("bytes after an ACT's last field must be ignored");
    for (size_t n = 0; n < b.size(); ++n)
    {
        TalkMsg r;
        r.seq = 99;
        const int e = DecodeTalk(n ? &b[0] : "", n, &r);
        if (e != (n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc) || r.seq != 99) { Fail("a truncated ACT must decode as Trunc and leave the output untouched"); break; }
    }
    TalkMsg e0;
    e0.kind = kTalkAct; e0.convId = 1; e0.npcUid = 1; e0.targetUid = 2; e0.seq = 1; e0.lineSid = "l";
    std::vector<char> eb;
    TalkMsg qe;
    if (!EncodeTalk(&eb, e0) || eb.size() != 13 + 4 + 3 + 3 + 4 || DecodeTalk(&eb[0], eb.size(), &qe) != kTalkDecodeOk || !qe.actTypes.empty()
        || !qe.itemSids.empty() || !qe.relSids.empty() || qe.carriedUid != 0)
        Fail("an ACT with empty lists must encode as its three zero counts and a zero carried uid, and round-trip");
    TalkMsg res;
    res.kind = kTalkActResult; res.convId = 3; res.npcUid = 1; res.targetUid = 2; res.seq = 17; res.result = kTalkActNoMoney; res.detail = 8;
    res.moneyBefore = 20; res.moneyAfter = -1; res.applied = 0; res.unsupported = 2;
    res.anyApplied = kTalkAnyUnknown;   /* P26s5 fold 1 */
    std::vector<char> rb;
    TalkMsg qr;
    if (!EncodeTalk(&rb, res) || rb.size() != 31 || DecodeTalk(&rb[0], rb.size(), &qr) != kTalkDecodeOk || qr.kind != kTalkActResult
        || qr.seq != 17 || qr.result != kTalkActNoMoney || qr.detail != 8 || qr.moneyBefore != 20 || qr.moneyAfter != -1 || qr.applied != 0
        || qr.unsupported != 2 || qr.convId != 3 || qr.anyApplied != kTalkAnyUnknown)
        Fail("an ACT_RESULT must be 31 bytes and round-trip every field (anyApplied apart from result: P26s5 fold 1)");
    for (size_t n = 0; n < rb.size(); ++n)
    {
        TalkMsg r;
        const int e = DecodeTalk(n ? &rb[0] : "", n, &r);
        if (e != (n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc)) { Fail("a truncated ACT_RESULT must decode as Trunc"); break; }
    }
    TalkMsg over = a;
    while (over.actTypes.size() < 33) { over.actTypes.push_back(9); over.actValues.push_back(1); }
    std::vector<char> ob;
    if (EncodeTalk(&ob, over) || !ob.empty()) Fail("33 actions must be refused and append nothing");
    over.actTypes.resize(32); over.actValues.resize(32);
    if (!EncodeTalk(&ob, over)) Fail("32 actions must encode");
    TalkMsg oi = a;
    while (oi.itemSids.size() < 17) { oi.itemSids.push_back("i"); oi.itemValues.push_back(1); }
    std::vector<char> oib;
    if (EncodeTalk(&oib, oi)) Fail("17 items must be refused");
    TalkMsg orl = a;
    while (orl.relSids.size() < 17) { orl.relSids.push_back("f"); orl.relValues.push_back(1); }
    std::vector<char> orb;
    if (EncodeTalk(&orb, orl)) Fail("17 relation effects must be refused");
    TalkMsg odd = a;
    odd.actValues.pop_back();
    std::vector<char> odb;
    if (EncodeTalk(&odb, odd)) Fail("action types and values of different counts must be refused");
    TalkMsg oddi = a;
    oddi.itemValues.clear();
    std::vector<char> oddb;
    if (EncodeTalk(&oddb, oddi)) Fail("item sids and values of different counts must be refused");
    TalkMsg big = a;
    big.actTypes[0] = 0x10000;
    std::vector<char> bgb;
    if (EncodeTalk(&bgb, big)) Fail("an action type over 16 bits must be refused");
    TalkMsg ls = a;
    ls.itemSids[0] = std::string(129, 's');
    std::vector<char> lsb;
    if (EncodeTalk(&lsb, ls)) Fail("a 129-byte item sid must be refused");
    std::vector<char> tm = b;
    const size_t at = 13 + 4 + 2 + a.lineSid.size();
    if (tm[at] != 2) { Fail("the action count byte is not where the layout says"); return; }
    tm[at] = 33;
    TalkMsg qm;
    if (DecodeTalk(&tm[0], tm.size(), &qm) != kTalkDecodeTooMany) Fail("an action count of 33 must decode as TooMany");
    std::vector<char> k6 = rb;
    k6[0] = 9;   /* P25: 6 is REQUEST; T-737: 7 / 8 are the cursor kinds */
    TalkMsg q6;
    if (DecodeTalk(&k6[0], k6.size(), &q6) != kTalkDecodeBadKind) Fail("kind 9 must decode as BadKind");
    if (std::string(TalkKindName(kTalkAct)) != "ACT" || std::string(TalkKindName(kTalkActResult)) != "ACT_RESULT"
        || std::string(TalkActResultName(kTalkActNoMoney)) != "NO_MONEY" || std::string(TalkActResultName(kTalkActApplied)) != "APPLIED"
        || std::string(TalkActResultName(kTalkActHireNotAsked)) != "HIRE_NOT_ASKED" || std::string(TalkActResultName(kTalkActNoCarry)) != "NO_CARRY"
        || std::string(TalkActResultName(99)) != "?")
        Fail("the stage-5 names must read as the log shows them");
    const int apply[] = { 4, 5, 8, 9, 13, 14, 23, 25, 32, 35, 49, 53 };
    const int refuse[] = { 1, 11, 24, 41, 42, 43, 44, 45, 46, 47, 48 };
    const int carried[] = { 10, 30, 40 };
    const int npc[] = { 0, 2, 6, 7, 12, 15, 16, 17, 19, 20, 21, 22, 28, 29, 33, 34, 36, 52, 54, 55, 56, 57, 58, 59, -1, 999 };
    for (size_t i = 0; i < sizeof(apply) / sizeof(apply[0]); ++i) if (TalkActSide(apply[i]) != kTalkSideApply) Fail("an APPLY type is not on the target's owner");
    for (size_t i = 0; i < sizeof(refuse) / sizeof(refuse[0]); ++i) if (TalkActSide(refuse[i]) != kTalkSideRefuse) Fail("a REFUSE type is not refused");
    for (size_t i = 0; i < sizeof(carried) / sizeof(carried[0]); ++i) if (TalkActSide(carried[i]) != kTalkSideCarried) Fail("a carried-person type is not CARRIED");
    for (size_t i = 0; i < sizeof(npc) / sizeof(npc[0]); ++i) if (TalkActSide(npc[i]) != kTalkSideNpc) Fail("an NPC-side type is not on the NPC's owner");
    if (TalkActSide(3) != kTalkSideHire || TalkActSide(18) != kTalkSideHire) Fail("3 / 18 must take the hire road");
    int sides[5] = { 0, 0, 0, 0, 0 };
    for (int ty = 0; ty < 59; ++ty) ++sides[TalkActSide(ty)];
    if (sides[kTalkSideApply] != 12 || sides[kTalkSideHire] != 2 || sides[kTalkSideRefuse] != 11 || sides[kTalkSideCarried] != 3 || sides[kTalkSideNpc] != 31)
        Fail("the table's counts over DA_NONE..DA_KNOCKOUT changed");
    {   /* the carried-person types and the conversation-enders, exactly */
        int car = 0, ends = 0;
        for (int ty = -1; ty < 60; ++ty)
        {
            car += TalkActCarried(ty); ends += TalkActEndsTalk(ty);
            if (TalkActCarried(ty) && TalkActSide(ty) != kTalkSideCarried) Fail("a carried-person type is not CARRIED in the table");
            if (TalkActEndsTalk(ty) && TalkActSide(ty) != kTalkSideRefuse) Fail("an ending type is not REFUSE");
        }
        if (car != 3 || !TalkActCarried(10) || !TalkActCarried(30) || !TalkActCarried(40)) Fail("exactly 10 / 30 / 40 act on the carried person");
        if (ends != 2 || !TalkActEndsTalk(1) || !TalkActEndsTalk(11)) Fail("1 / 11 must end the conversation");
    }
}

/* A carried-person line between games: each game runs exactly its own share. 10's engine case runs only on the talker's game (the
   reward is its player's money; on the NPC's game it would pay the copy's stand-in faction), and the NPC's game always gets the take
   (as the engine's case or as its two orders). 30 / 40's engine case runs on whichever of the two games owns the person, never on
   both; with a third game owning it neither runs it and the NPC's game still gives the take orders. The sender needs a carried
   person; the receiver needs the very same one. The hand-over mark lets the talker's own carrier go only for its own person, its
   own carrier and that NPC, inside the time window (across the tick counter's wrap too). An ACT without the carried uid (the
   previous layout) is cut short. */
void t_t430_carried_person_shares_and_hand_over()
{
    using namespace cooptalk;
    for (int person = 0; person < 3; ++person)   /* 0 the NPC's game owns the person, 1 the talker's game, 2 a third game */
    {
        const int carried[] = { 10, 30, 40 };
        for (size_t i = 0; i < 3; ++i)
        {
            const int ty = carried[i];
            const int npcEngine = TalkCarriedNpcEngine(ty, person == 0 ? 1 : 0);
            const int talkerEngine = TalkCarriedTalkerEngine(ty, person == 1 ? 1 : 0);
            const int takeOrders = npcEngine ? 0 : 1;
            if (ty == 10 && (npcEngine || !talkerEngine)) Fail("10 runs the engine's case on the talker's game only, whoever owns the person");
            if (ty != 10 && person == 0 && (!npcEngine || talkerEngine)) Fail("30 / 40 with the NPC's game owning the person: the engine's case runs there only");
            if (ty != 10 && person == 1 && (npcEngine || !talkerEngine)) Fail("30 / 40 with the talker's game owning the person: the engine's case runs there only");
            if (ty != 10 && person == 2 && (npcEngine || talkerEngine)) Fail("30 / 40 with a third game owning the person: no engine case on either game");
            if (npcEngine + takeOrders != 1) Fail("the NPC's game gives the take exactly once (the engine's case or its two orders)");
        }
    }
    if (TalkCarriedNpcEngine(29, 1) || TalkCarriedTalkerEngine(29, 1) || TalkCarriedNpcEngine(33, 1) || TalkCarriedTalkerEngine(8, 1))
        Fail("only the carried-person types have a carried share");
    if (TalkCarriedMaySend(0) != 0 || TalkCarriedMaySend(0x00400005u) != 1) Fail("the NPC's game sends a carried-person line only while a person is carried there");
    if (TalkCarriedApplyOk(5, 5) != 1 || TalkCarriedApplyOk(5, 6) != 0 || TalkCarriedApplyOk(5, 0) != 0 || TalkCarriedApplyOk(0, 0) != 0)
        Fail("the talker's game applies only when its own character carries the very person the ACT names");
    TalkHandOver h;
    h.body = 7; h.carrier = 8; h.taker = 9; h.ms = 1000;
    if (TalkHandOverLetsGo(h, 7, 8, 9, 1000) != 1 || TalkHandOverLetsGo(h, 7, 8, 9, 1000 + kTalkHandOverMs - 1) != 1)
        Fail("the hand-over mark lets the carrier go inside its window");
    if (TalkHandOverLetsGo(h, 7, 8, 9, 1000 + kTalkHandOverMs) != 0) Fail("the hand-over mark ends with its window");
    if (TalkHandOverLetsGo(h, 6, 8, 9, 1000) || TalkHandOverLetsGo(h, 7, 5, 9, 1000) || TalkHandOverLetsGo(h, 7, 8, 4, 1000))
        Fail("the hand-over mark holds only for its own person, its own carrier and that NPC");
    if (TalkHandOverExpired(h, 1000 + kTalkHandOverMs - 1) != 0 || TalkHandOverExpired(h, 1000 + kTalkHandOverMs) != 1)
        Fail("the hand-over mark runs out exactly at its window");
    TalkHandOver w;
    w.body = 7; w.carrier = 8; w.taker = 9; w.ms = 0xFFFFFF00ul;
    if (TalkHandOverLetsGo(w, 7, 8, 9, 0x100ul) != 1 || TalkHandOverExpired(w, 0x100ul) != 0) Fail("the hand-over window survives the tick counter's wrap");
    TalkHandOver z;
    z.body = 0; z.carrier = 0; z.taker = 0; z.ms = 1000;
    if (TalkHandOverLetsGo(z, 0, 0, 0, 1000) != 0) Fail("an empty mark never lets anyone go");
    TalkMsg a;
    a.kind = kTalkAct; a.convId = 1; a.npcUid = 2; a.targetUid = 3; a.seq = 4; a.lineSid = "l";
    a.actTypes.push_back(10); a.actValues.push_back(0);
    a.carriedUid = 0x00400005u;
    std::vector<char> b;
    TalkMsg q;
    if (!EncodeTalk(&b, a) || DecodeTalk(&b[0], b.size(), &q) != kTalkDecodeOk || q.carriedUid != 0x00400005u || q.actTypes.size() != 1 || q.actTypes[0] != 10)
        Fail("a carried-person ACT must round-trip its type and the carried uid");
    std::vector<char> old(b.begin(), b.end() - 4);
    TalkMsg qo;
    if (DecodeTalk(&old[0], old.size(), &qo) != kTalkDecodeTrunc) Fail("an ACT without the carried uid must decode as Trunc");
}

/* P26s1 fold 1 L8: a message cut short inside ANY field decodes as kTalkDecodeTrunc (under the 13 fixed bytes: TooShort) and leaves
   the output untouched - a PROMPT with replies (one with an empty text), a PROMPT with none, a REPLY and a CLOSED ANSWER, an END. */
void t_p26s1f1_talkwire_every_truncation_is_trunc()
{
    using namespace cooptalk;
    std::vector<TalkMsg> ms;
    TalkMsg p;
    p.kind = kTalkPrompt; p.convId = 7; p.npcUid = 1; p.targetUid = 2; p.event = 1; p.lineSid = "line-1"; p.npcText = "Hello there.";
    p.replyIds.push_back("r1"); p.replyTexts.push_back("Hi.");
    p.replyIds.push_back("r2"); p.replyTexts.push_back("");
    p.deadlineMs = 1234;
    ms.push_back(p);
    TalkMsg p0 = p;
    p0.replyIds.clear(); p0.replyTexts.clear();
    ms.push_back(p0);
    TalkMsg a;
    a.kind = kTalkAnswer; a.convId = 8; a.npcUid = 1; a.targetUid = 2; a.result = kTalkAnsReply; a.index = 1; a.replyId = "r2";
    ms.push_back(a);
    TalkMsg c = a;
    c.result = kTalkAnsClosed; c.index = -1; c.replyId = "";
    ms.push_back(c);
    TalkMsg e;
    e.kind = kTalkEnd; e.convId = 9; e.npcUid = 1; e.targetUid = 2; e.reason = kTalkEndRestarted;
    ms.push_back(e);
    for (size_t k = 0; k < ms.size(); ++k)
    {
        std::vector<char> b;
        if (!EncodeTalk(&b, ms[k])) { Fail("a fold-1 truncation sample did not encode"); return; }
        for (size_t n = 0; n < b.size(); ++n)
        {
            TalkMsg r;
            r.convId = 0xDEADBEEFu;
            const int want = n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc;
            if (DecodeTalk(n ? &b[0] : "", n, &r) != want) { Fail("a message cut inside a field must decode as Trunc (TooShort under 13 bytes)"); return; }
            if (r.convId != 0xDEADBEEFu) { Fail("a truncated message must leave the output untouched"); return; }
        }
        TalkMsg whole;
        if (DecodeTalk(&b[0], b.size(), &whole) != kTalkDecodeOk) { Fail("the uncut fold-1 sample must decode"); return; }
    }
}

/* T-737: the talk cursor - the never-talks filter, the NPC's game's answer in 0x679FC0's order, the hover's decision from the cache
   (no answer -> false; no / talk / talk(parked) / unknown), the CURSOR_ASK / CURSOR_ANSWER round trip and its refusals, the preload
   rate cap. */
void t_t737_talk_cursor_decisions_wire_and_rate()
{
    using namespace cooptalk;
    if (TalkCursorMayTalk(1, 0, 0) != 1) Fail("T-737: a living non-player NPC with an event-1 list may talk");
    if (TalkCursorMayTalk(0, 0, 0) != 0) Fail("T-737: no event-1 list never talks");
    if (TalkCursorMayTalk(1, 1, 0) != 0) Fail("T-737: a dead NPC never talks");
    if (TalkCursorMayTalk(1, 0, 1) != 0) Fail("T-737: an NPC of a player faction never talks");
    if (TalkCursorNpcAnswer(1, 0, 1, 1, 0) != kTalkCursorParked) Fail("T-737: a parked line answers first (0x679FC0's first test)");
    if (TalkCursorNpcAnswer(0, 1, 0, 0, 1) != kTalkCursorTalk) Fail("T-737: a line found is talk");
    if (TalkCursorNpcAnswer(0, 1, 0, 0, 0) != kTalkCursorNo) Fail("T-737: no line found is no");
    if (TalkCursorNpcAnswer(0, 0, 0, 0, 1) != kTalkCursorNo) Fail("T-737: no event-1 list is no, whatever the pick");
    if (TalkCursorNpcAnswer(0, 1, 1, 0, 1) != kTalkCursorNo) Fail("T-737: a dead NPC is no");
    if (TalkCursorNpcAnswer(0, 1, 0, 1, 1) != kTalkCursorNo) Fail("T-737: a player-faction NPC is no");
    if (TalkCursorHover(0, 1, kTalkCursorTalk) != kTalkHoverFiltered || TalkCursorHoverTrue(kTalkHoverFiltered))
        Fail("T-737: the never-talks filter answers false whatever the cache holds");
    if (TalkCursorHover(1, 0, kTalkCursorTalk) != kTalkHoverNoAnswer || TalkCursorHoverTrue(kTalkHoverNoAnswer))
        Fail("T-737: no cached answer is false (the ordinary cursor), never the copy's own pick");
    if (TalkCursorHover(1, 1, kTalkCursorNo) != kTalkHoverNo || TalkCursorHoverTrue(kTalkHoverNo)) Fail("T-737: an answer of 0 is false");
    if (TalkCursorHover(1, 1, kTalkCursorTalk) != kTalkHoverTalk || !TalkCursorHoverTrue(kTalkHoverTalk)) Fail("T-737: an answer of 1 is true");
    if (TalkCursorHover(1, 1, kTalkCursorParked) != kTalkHoverParked || !TalkCursorHoverTrue(kTalkHoverParked)) Fail("T-737: an answer of 2 is true");
    if (TalkCursorHover(1, 1, kTalkCursorUnknown) != kTalkHoverUnknown || TalkCursorHoverTrue(kTalkHoverUnknown)) Fail("T-737: unknown is false");
    if (std::string(TalkCursorAnswerName(kTalkCursorParked)) != "talk(parked)" || std::string(TalkCursorAnswerName(kTalkCursorUnknown)) != "unknown"
        || std::string(TalkKindName(kTalkCursorAsk)) != "CURSOR_ASK" || std::string(TalkKindName(kTalkCursorAnswer)) != "CURSOR_ANSWER")
        Fail("T-737: the names");
    if (TalkCursorKey(7, 9) != ((7ull << 32) | 9ull) || TalkCursorKey(7, 0xFFFFFFFFu) >= TalkCursorKey(8, 0)) Fail("T-737: the cache key orders by NPC");
    /* the wire */
    TalkMsg q;
    q.kind = kTalkCursorAsk; q.convId = 0; q.npcUid = 0x00400001u; q.targetUid = 0x00800002u; q.reqId = 41; q.purse = 1234;
    std::vector<char> b;
    if (!EncodeTalk(&b, q) || b.size() != 21) { Fail("T-737: a CURSOR_ASK must encode to 21 bytes"); return; }
    TalkMsg r;
    if (DecodeTalk(&b[0], b.size(), &r) != kTalkDecodeOk || r.kind != kTalkCursorAsk || r.reqId != 41 || r.purse != 1234
        || r.npcUid != 0x00400001u || r.targetUid != 0x00800002u || r.convId != 0)
        Fail("T-737: a CURSOR_ASK must round-trip every field");
    for (size_t n = 0; n < b.size(); ++n)
    {
        TalkMsg t;
        const int want = n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc;
        if (DecodeTalk(n ? &b[0] : "", n, &t) != want) { Fail("T-737: a cut CURSOR_ASK must decode as Trunc (TooShort under 13 bytes)"); break; }
    }
    TalkMsg a;
    a.kind = kTalkCursorAnswer; a.convId = 0; a.npcUid = 0x00400001u; a.targetUid = 0x00800002u; a.reqId = 41; a.result = kTalkCursorParked;
    std::vector<char> ab;
    if (!EncodeTalk(&ab, a) || ab.size() != 18) { Fail("T-737: a CURSOR_ANSWER must encode to 18 bytes"); return; }
    TalkMsg ar;
    if (DecodeTalk(&ab[0], ab.size(), &ar) != kTalkDecodeOk || ar.kind != kTalkCursorAnswer || ar.reqId != 41 || ar.result != kTalkCursorParked
        || ar.npcUid != 0x00400001u || ar.targetUid != 0x00800002u)
        Fail("T-737: a CURSOR_ANSWER must round-trip every field");
    for (size_t n = 0; n < ab.size(); ++n)
    {
        TalkMsg t;
        const int want = n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc;
        if (DecodeTalk(n ? &ab[0] : "", n, &t) != want) { Fail("T-737: a cut CURSOR_ANSWER must decode as Trunc (TooShort under 13 bytes)"); break; }
    }
    for (int k = 0; k <= kTalkCursorAnswerMax; ++k)
    {
        TalkMsg ok = a;
        ok.result = k;
        std::vector<char> kb;
        TalkMsg kr;
        if (!EncodeTalk(&kb, ok) || DecodeTalk(&kb[0], kb.size(), &kr) != kTalkDecodeOk || kr.result != k) Fail("T-737: every answer 0..3 must round-trip");
    }
    {
        TalkMsg bad = a; bad.result = 4;
        std::vector<char> x;
        if (EncodeTalk(&x, bad)) Fail("T-737: an answer over 3 must be refused");
        std::vector<char> y = ab;
        y[17] = 4;
        TalkMsg yr;
        if (DecodeTalk(&y[0], y.size(), &yr) != kTalkDecodeBadValue) Fail("T-737: an answer byte over 3 must decode as BadValue");
    }
    {
        TalkMsg z = q; z.reqId = 0;
        std::vector<char> x;
        if (EncodeTalk(&x, z)) Fail("T-737: askId 0 must be refused");
        std::vector<char> y = b;
        y[13] = 0; y[14] = 0; y[15] = 0; y[16] = 0;
        TalkMsg yr;
        if (DecodeTalk(&y[0], y.size(), &yr) != kTalkDecodeBadValue) Fail("T-737: a CURSOR_ASK with askId 0 must decode as BadValue");
        TalkMsg nz = q; nz.npcUid = 0;
        if (EncodeTalk(&x, nz)) Fail("T-737: a CURSOR_ASK with no NPC uid must be refused");
        std::vector<char> w = b;
        w[5] = 0; w[6] = 0; w[7] = 0; w[8] = 0;
        if (DecodeTalk(&w[0], w.size(), &yr) != kTalkDecodeBadValue) Fail("T-737: a CURSOR_ASK with no NPC uid must decode as BadValue");
        std::vector<char> v = ab;
        v[9] = 0; v[10] = 0; v[11] = 0; v[12] = 0;
        if (DecodeTalk(&v[0], v.size(), &yr) != kTalkDecodeBadValue) Fail("T-737: a CURSOR_ANSWER with no talker uid must decode as BadValue");
    }
    {
        std::vector<char> k9 = b;
        k9[0] = 9;
        TalkMsg q9;
        if (DecodeTalk(&k9[0], k9.size(), &q9) != kTalkDecodeBadKind) Fail("T-737: kind 9 must decode as BadKind");
    }
    /* the preload rate: 4 per 500 ms */
    unsigned long ws = 0;
    int sent = 0;
    for (int i = 0; i < kTalkCursorPreloadPerWindow; ++i)
        if (!TalkCursorPreloadMay(&ws, &sent, 1000)) Fail("T-737: the first 4 preloads of a window may go");
    if (TalkCursorPreloadMay(&ws, &sent, 1000) || TalkCursorPreloadMay(&ws, &sent, 1499)) Fail("T-737: a fifth preload in the window waits");
    if (!TalkCursorPreloadMay(&ws, &sent, 1500)) Fail("T-737: a new window lets a preload go");
    for (int i = 1; i < kTalkCursorPreloadPerWindow; ++i)
        if (!TalkCursorPreloadMay(&ws, &sent, 1600)) Fail("T-737: the new window holds 4");
    if (TalkCursorPreloadMay(&ws, &sent, 1999)) Fail("T-737: and no more");
    unsigned long wr = 0xFFFFFF00ul;
    int sw = kTalkCursorPreloadPerWindow;
    if (TalkCursorPreloadMay(&wr, &sw, 0xFFFFFFF0ul)) Fail("T-737: a full window waits before the tick count wraps");
    if (!TalkCursorPreloadMay(&wr, &sw, 0x00000100ul)) Fail("T-737: the window ends across the tick count's wrap");
    int total = 0;
    unsigned long w2 = 0;
    int s2 = 0;
    for (unsigned long t = 0; t < 10000; t += 10) total += TalkCursorPreloadMay(&w2, &s2, t);
    if (total > 4 * 20 || total < 4 * 19) Fail("T-737: at most 4 preloads per 500 ms over 10 s");
}

/* T-737 fold 1: a pair with no answer is asked (and never twice while one waits), a fresh hover re-asks; a click or the order menu
   with no answer yet - or unknown - gets the engine's own answer, a hover gets false; the fresh-hover decision from frame numbers. */
void t_t737f1_talk_cursor_ask_click_and_fresh_hover()
{
    using namespace cooptalk;
    if (TalkCursorShouldAsk(0, 0, 0) != 1) Fail("T-737f1: a pair with no answer at all is asked");
    if (TalkCursorShouldAsk(0, 1, 0) != 0) Fail("T-737f1: a held answer, not a fresh hover: no ask");
    if (TalkCursorShouldAsk(1, 1, 0) != 1) Fail("T-737f1: a fresh hover re-asks even with an answer");
    if (TalkCursorShouldAsk(1, 0, 1) != 0 || TalkCursorShouldAsk(0, 0, 1) != 0) Fail("T-737f1: never while an ASK for the pair is queued or waiting");
    if (TalkCursorUse(TalkCursorHover(1, 0, 0), 1) != kTalkUseEngine) Fail("T-737f1: a click with no answer yet gets the engine's own answer");
    if (TalkCursorUse(TalkCursorHover(1, 0, 0), 0) != kTalkUseFalse) Fail("T-737f1: a hover with no answer yet is false");
    if (TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorUnknown), 1) != kTalkUseEngine) Fail("T-737f1: a click on unknown gets the engine's own answer");
    if (TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorUnknown), 0) != kTalkUseFalse) Fail("T-737f1: a hover on unknown is false");
    if (TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorNo), 1) != kTalkUseFalse) Fail("T-737f1: a click on no is false (no talk order)");
    if (TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorTalk), 1) != kTalkUseTrue || TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorTalk), 0) != kTalkUseTrue)
        Fail("T-737f1: talk is true for a hover and a click");
    if (TalkCursorUse(TalkCursorHover(1, 1, kTalkCursorParked), 0) != kTalkUseTrue) Fail("T-737f1: talk(parked) is true");
    if (TalkCursorUse(TalkCursorHover(0, 0, 0), 1) != kTalkUseFalse) Fail("T-737f1: the never-talks filter is false even for a click");
    if (TalkCursorFreshHover(0, 10, 11) != 1) Fail("T-737f1: another character is a fresh hover");
    if (TalkCursorFreshHover(1, 10, 11) != 0) Fail("T-737f1: the same character on the next frame is the same hover");
    if (TalkCursorFreshHover(1, 10, 10) != 0) Fail("T-737f1: the same character twice in one frame is the same hover");
    if (TalkCursorFreshHover(1, 10, 12) != 1) Fail("T-737f1: the same character after a frame without it is a fresh hover");
    if (TalkCursorFreshHover(1, -10, 0) != 1) Fail("T-737f1: the first call is a fresh hover");
}

/* P25 (protocol 109): REQUEST round-trips; PROMPT's reqId and END's reqId / why / endsType are required fields (every cut is Trunc);
   kind 9 is BadKind (7 / 8: the T-737 cursor kinds); the names. */
void t_p25_talkwire_request_and_new_fields()
{
    using namespace cooptalk;
    TalkMsg q;
    q.kind = kTalkRequest; q.convId = 0; q.npcUid = 0x00400001u; q.targetUid = 0x00800002u; q.reqId = 77; q.event = 1;
    std::vector<char> b;
    if (!EncodeTalk(&b, q) || b.size() != 22) { Fail("P25: a REQUEST must encode to 22 bytes (the purse last)"); return; }
    TalkMsg r;
    if (DecodeTalk(&b[0], b.size(), &r) != kTalkDecodeOk || r.kind != kTalkRequest || r.reqId != 77 || r.event != 1
        || r.npcUid != 0x00400001u || r.targetUid != 0x00800002u || r.convId != 0)
        Fail("P25: a REQUEST must round-trip every field");
    for (size_t n = 0; n < b.size(); ++n)
    {
        TalkMsg t;
        const int want = n < 13 ? kTalkDecodeTooShort : kTalkDecodeTrunc;
        if (DecodeTalk(n ? &b[0] : "", n, &t) != want) { Fail("P25: a cut REQUEST must decode as Trunc (TooShort under 13 bytes)"); break; }
    }
    TalkMsg qb = q;
    qb.event = 256;
    std::vector<char> bb;
    if (EncodeTalk(&bb, qb)) Fail("P25: a REQUEST event over 255 must be refused");
    std::vector<char> k7 = b;
    k7[0] = 9;
    TalkMsg q7;
    if (DecodeTalk(&k7[0], k7.size(), &q7) != kTalkDecodeBadKind) Fail("P25: kind 9 must decode as BadKind");
    TalkMsg e;
    e.kind = kTalkEnd; e.convId = 0; e.npcUid = 1; e.targetUid = 2; e.reason = kTalkEndNotStarted; e.reqId = 77; e.why = kTalkWhyFar;
    std::vector<char> eb;
    TalkMsg qe;
    if (!EncodeTalk(&eb, e) || eb.size() != 20 || DecodeTalk(&eb[0], eb.size(), &qe) != kTalkDecodeOk || qe.reason != kTalkEndNotStarted
        || qe.reqId != 77 || qe.why != kTalkWhyFar || qe.endsType != 0 || qe.convId != 0)
        Fail("P25: END NOT_STARTED must carry its reqId and why");
    for (size_t n = 13; n < eb.size(); ++n)
    {
        TalkMsg t;
        if (DecodeTalk(&eb[0], n, &t) != kTalkDecodeTrunc) { Fail("P25: an END cut inside reqId / why / endsType must be Trunc"); break; }
    }
    TalkMsg et;
    et.kind = kTalkEnd; et.convId = 5; et.npcUid = 1; et.targetUid = 2; et.reason = kTalkEndEngine; et.endsType = 1;
    std::vector<char> tb;
    TalkMsg qt;
    if (!EncodeTalk(&tb, et) || DecodeTalk(&tb[0], tb.size(), &qt) != kTalkDecodeOk || qt.endsType != 1 || qt.reason != kTalkEndEngine || qt.reqId != 0)
        Fail("P25: an END must carry endsType 1 (TRADE)");
    et.endsType = 256;
    std::vector<char> tb2;
    if (EncodeTalk(&tb2, et)) Fail("P25: an endsType over 255 must be refused");
    et.endsType = 0; et.why = -1;
    std::vector<char> tb3;
    if (EncodeTalk(&tb3, et)) Fail("P25: a negative why must be refused");
    TalkMsg p;
    p.kind = kTalkPrompt; p.convId = 3; p.npcUid = 1; p.targetUid = 2; p.event = 1; p.lineSid = "l1"; p.npcText = "Hi.";
    p.replyIds.push_back("r1"); p.replyTexts.push_back("Hello.");
    p.reqId = 77;
    std::vector<char> pb;
    TalkMsg qp;
    if (!EncodeTalk(&pb, p) || DecodeTalk(&pb[0], pb.size(), &qp) != kTalkDecodeOk || qp.reqId != 77 || qp.deadlineMs != 0)
        Fail("P25: a PROMPT must carry the reqId it answers");
    TalkMsg qp2;
    if (DecodeTalk(&pb[0], pb.size() - 1, &qp2) != kTalkDecodeTrunc) Fail("P25: a PROMPT cut inside its reqId must be Trunc");
    if (std::string(TalkKindName(kTalkRequest)) != "REQUEST" || std::string(TalkEndName(kTalkEndNotStarted)) != "NOT_STARTED")
        Fail("P25: the REQUEST / NOT_STARTED names");
    for (int w = 1; w < kTalkWhyCount; ++w)
        if (std::string(TalkWhyName(w)) == "?") { Fail("P25: every why 1..12 has a name"); break; }
    if (std::string(TalkWhyName(0)) != "?" || std::string(TalkWhyName(kTalkWhyCount)) != "?") Fail("P25: why 0 / 13 are not names");
}

/* P25: the pure decisions - B's intercept, A's refusal order, B's own window after an END. */
void t_p25_intercept_request_order_and_local_windows()
{
    using namespace cooptalk;
    if (TalkP25Intercept(1, 1, 0, 0, 0) != 1) Fail("P25: this game's own player talking to a copy the other game drives must become a REQUEST");
    if (TalkP25Intercept(0, 1, 0, 0, 0) != 0) Fail("P25: with the road off the engine runs the talk");
    if (TalkP25Intercept(1, 0, 0, 0, 0) != 0) Fail("P25: a talker that is not this game's own player character is left to the engine");
    if (TalkP25Intercept(1, 1, 1, 0, 0) != 0) Fail("P25: an NPC this game drives is talked to here");
    if (TalkP25Intercept(1, 1, -1, 0, 0) != 0) Fail("P25: an NPC with no uid (the other game has no copy) is talked to here");
    if (TalkP25Intercept(1, 1, 0, 1, 0) != 0) Fail("P25: the other player's own character is left to the engine (leftover)");
    if (TalkP25Intercept(1, 1, 0, 0, 1) != 0) Fail("P25: our own apply of a PROMPT is never intercepted");
    if (TalkP25RequestWhy(1, 1, 0, 1, 0, 1, 1) != 0) Fail("P25: a request that passes every check starts");
    if (TalkP25RequestWhy(0, 0, 1, 0, 1, 0, 0) != kTalkWhyOff) Fail("P25: off is the first reason");
    if (TalkP25RequestWhy(1, 0, 1, 0, 1, 0, 0) != kTalkWhyNotOurs) Fail("P25: an NPC not this game's comes before down");
    if (TalkP25RequestWhy(1, 1, 1, 0, 1, 0, 0) != kTalkWhyDown) Fail("P25: a dead / knocked-out NPC comes before the copy");
    if (TalkP25RequestWhy(1, 1, 0, 0, 1, 0, 0) != kTalkWhyNoCopy) Fail("P25: no copy of the talker comes before the event");
    if (TalkP25RequestWhy(1, 1, 0, 1, 1, 0, 0) != kTalkWhyEngine) Fail("P25: an event other than 1 comes before busy");
    if (TalkP25RequestWhy(1, 1, 0, 1, 1, 0, 1) != kTalkWhyBusy) Fail("P25: busy comes before far");
    if (TalkP25RequestWhy(1, 1, 0, 1, 0, 0, 1) != kTalkWhyFar) Fail("P25: a talker beyond the talk gate of the NPC is far");
    if (TalkEndsLocal(1) != kTalkLocalTrade || TalkEndsLocal(11) != kTalkLocalEditor || TalkEndsLocal(0) != kTalkLocalNone
        || TalkEndsLocal(4) != kTalkLocalNone)
        Fail("P25: END endsType 1 opens the trade window, 11 the editor, anything else nothing");
    if (TalkActSide(1) == kTalkSideNpc || TalkActSide(18) == kTalkSideNpc || TalkActSide(4) == kTalkSideNpc)
        Fail("P25: the first-line hold needs 1 / 18 / 4 to be target-side");
}

/* P25 fold 2: B's no-line talk click goes to the NPC's owner; A's 'not started' reason split (noLine / engine / gate). */
void t_p25f2_click_event_and_not_started_why()
{
    using namespace cooptalk;
    if (TalkP25ClickEvent(1, 1) != 1 || TalkP25ClickEvent(1, 3) != 1 || TalkP25ClickEvent(1, -1) != 1)
        Fail("P25 fold 2: a start whose copy found a line is taken as before, whatever the event");
    if (TalkP25ClickEvent(0, 1) != 1) Fail("P25 fold 2: the talk-to event with no line on the copy still goes to the NPC's owner");
    if (TalkP25ClickEvent(0, 3) != 0 || TalkP25ClickEvent(0, 0) != 0 || TalkP25ClickEvent(0, -1) != 0)
        Fail("P25 fold 2: a no-line start of any other event (or of no known event) is left to the engine");
    if (TalkP25NotStartedWhy(0, 1, 1, 1, 1) != kTalkWhyEngine) Fail("P25 fold 2: sendEvent not run (a fault) is engine, first");
    if (TalkP25NotStartedWhy(1, 1, 0, 0, 1) != kTalkWhyNotForwarded) Fail("P25 fold 2: not forwarded comes before the start's own reasons");
    if (TalkP25NotStartedWhy(1, 0, 0, 0, 1) != kTalkWhyEngine) Fail("P25 fold 2: sendEvent returning before startPlayerConversation is engine");
    if (TalkP25NotStartedWhy(1, 0, 1, 0, 1) != kTalkWhyNoLine) Fail("P25 fold 2: a start with no line is noLine, before the gate");
    if (TalkP25NotStartedWhy(1, 0, 1, 1, 1) != kTalkWhyGate) Fail("P25 fold 2: a start with a line the gate refused is gate");
    if (TalkP25NotStartedWhy(1, 0, 1, 1, 0) != kTalkWhyEngine) Fail("P25 fold 2: a start with a line not refused by the gate is engine");
    if (std::string(TalkWhyName(kTalkWhyNoLine)) != "noLine" || kTalkWhyNoLine != kTalkWhyCount - 1 || kTalkWhyNoAnswer != 11)
        Fail("P25 fold 2: noLine is appended as the last why; the old numbers keep their meaning");
}

/* T-719: a copy's own conversation is ended at the safe point only while live reads still show it and nothing of ours holds it. */
void t_t719_copy_own_end_action_every_branch()
{
    using namespace coopsay;
    if (CopyOwnEndAction(1, 1, 1, 0x29, 0x29, 0, 0) != kCopyOwnEnd) Fail("T-719: a live copy still busy with the queued event, no bubble, not held, is ended");
    if (CopyOwnEndAction(0, 1, 1, 0x29, 0x29, 0, 0) != kCopyOwnSkipGone) Fail("T-719: a copy (or Dialogue) that is gone is left alone");
    if (CopyOwnEndAction(1, 0, 1, 0x29, 0x29, 0, 0) != kCopyOwnSkipGone) Fail("T-719: a character that is this game's now is not a copy any more");
    if (CopyOwnEndAction(0, 0, 0, 0, 0x29, 1, 1) != kCopyOwnSkipGone) Fail("T-719: gone is judged first");
    if (CopyOwnEndAction(1, 1, 1, 0x29, 0x29, 1, 1) != kCopyOwnSkipHeld) Fail("T-719: held by a forwarded / mirrored talk comes before busy, event and bubble");
    if (CopyOwnEndAction(1, 1, 0, 0x29, 0x29, 0, 1) != kCopyOwnSkipHeld) Fail("T-719: held comes before not busy");
    if (CopyOwnEndAction(1, 1, 0, 0x29, 0x29, 0, 0) != kCopyOwnSkipNotBusy) Fail("T-719: a Dialogue no longer busy is left alone");
    if (CopyOwnEndAction(1, 1, 0, 0, 0x29, 1, 0) != kCopyOwnSkipNotBusy) Fail("T-719: not busy comes before a changed event and a bubble");
    if (CopyOwnEndAction(1, 1, 1, 1, 0x29, 0, 0) != kCopyOwnSkipChanged) Fail("T-719: a newer conversation (the player's talk, event 1) is left alone");
    if (CopyOwnEndAction(1, 1, 1, 0, 0x29, 0, 0) != kCopyOwnSkipChanged) Fail("T-719: +0x188 no longer the queued event is a change");
    if (CopyOwnEndAction(1, 1, 1, 3, 0x29, 1, 0) != kCopyOwnSkipChanged) Fail("T-719: a changed event comes before a bubble");
    if (CopyOwnEndAction(1, 1, 1, 0x29, 0x29, 1, 0) != kCopyOwnSkipBubble) Fail("T-719: a showing bubble is the engine's own timer to end");
    if (CopyOwnEndAction(1, 1, 1, 7, -1, 0, 0) != kCopyOwnEnd) Fail("T-719: a start whose event is not known is not compared");
    if (CopyOwnBubbleKeep(1000, 1000) != 1 || CopyOwnBubbleKeep(1000, 30999) != 1)
        Fail("T-719: an entry skipped for a showing bubble stays queued for 30 s after its start");
    if (CopyOwnBubbleKeep(1000, 31000) != 0 || CopyOwnBubbleKeep(1000, 900000) != 0)
        Fail("T-719: 30 s after its start an entry skipped for a bubble is dropped");
    if (CopyOwnBubbleKeep(0xFFFFF000u, 0x00001000u) != 1 || CopyOwnBubbleKeep(0xFFFFF000u, 0x00001000u + 30000u) != 0)
        Fail("T-719: the 30 s bubble wait is measured across the tick counter's wrap");
}

/* T-726: the talk-order line's throttle and outcome, and the per-kind 'first 5, then every 100th' throttle. */
void t_t726_talk_order_log_due_outcome_and_nth()
{
    using namespace cooptalk;
    if (TalkOrderLogDue(0, 1000, 0) != 1 || TalkOrderLogDue(5000, 5001, 4) != 1) Fail("T-726: the first 5 talk-order lines are always written");
    if (TalkOrderLogDue(5000, 5001, 5) != 0 || TalkOrderLogDue(5000, 14999, 9) != 0) Fail("T-726: after 5, one NPC gets one line per 10 s");
    if (TalkOrderLogDue(5000, 15000, 5) != 1) Fail("T-726: 10 s after an NPC's last line the next one is written");
    if (TalkOrderLogDue(0, 123, 50) != 1) Fail("T-726: an NPC never written about is written at once");
    if (TalkOrderLogDue(0xFFFFE000u, 0x00000800u, 7) != 1 || TalkOrderLogDue(0xFFFFF000u, 0x00001000u, 7) != 0)
        Fail("T-726: the 10 s gap is measured across the tick counter's wrap");
    if (TalkOrderOutcome(kTalkOrderIntercepted, 1, 1, 0) != kTalkOrderIntercepted || TalkOrderOutcome(kTalkOrderGate, 1, 1, 0) != kTalkOrderGate)
        Fail("T-726: an outcome the click's road wrote stands");
    if (TalkOrderOutcome(0, 0, 0, 0) != kTalkOrderNotReached || TalkOrderOutcome(0, 0, 1, 1) != kTalkOrderNotReached)
        Fail("T-726: not reached is the engine's refusal before choosing a line, whatever else was written");
    if (kTalkOrderLineNotStarted != 7 || kTalkOrderPlayerSpeaker != 8 || kTalkOrderKinds != 9)
        Fail("T-726: a busy copy is not an outcome of its own; another player's character as the speaker is (9 kinds)");
    if (TalkOrderOutcome(kTalkOrderPlayerSpeaker, 1, 1, 0) != kTalkOrderPlayerSpeaker || TalkOrderOutcome(kTalkOrderPlayerSpeaker, 1, 0, 0) != kTalkOrderPlayerSpeaker)
        Fail("T-726: a start refused for another player's character as the speaker is told apart");
    if (TalkOrderOutcome(0, 1, 1, 1) != kTalkOrderStarted) Fail("T-726: a start that returned true started here");
    if (TalkOrderOutcome(0, 1, 0, 0) != kTalkOrderNoLine) Fail("T-726: reached with no line is 'no line'");
    if (TalkOrderOutcome(0, 1, 1, 0) != kTalkOrderLineNotStarted) Fail("T-726: reached with a line and not started is told apart");
    if (TalkLogNthDue(0) != 0 || TalkLogNthDue(1) != 1 || TalkLogNthDue(5) != 1 || TalkLogNthDue(6) != 0 || TalkLogNthDue(99) != 0
        || TalkLogNthDue(100) != 1 || TalkLogNthDue(101) != 0 || TalkLogNthDue(200) != 1)
        Fail("T-726: a line kind is written the first 5 times, then every 100th");
}

/* P25: `talktest nearcopy ...` - the near NPC must be a copy the other game drives; the old callers still refuse it. */
void t_p25_talktest_nearcopy_parse()
{
    using namespace coopsay;
    unsigned int n = 9, t = 9;
    int e = 0, nc = 9;
    if (!TalkTestParse("nearcopy 5 1", &n, &t, &e, &nc) || n != 0 || t != 5 || e != 1 || nc != 1) Fail("P25: nearcopy <uid> 1 must parse");
    if (!TalkTestParse("near 5", &n, &t, &e, &nc) || nc != 0 || n != 0 || t != 5) Fail("P25: near must leave nearcopy 0");
    if (TalkTestParse("5 nearcopy", &n, &t, &e, &nc)) Fail("P25: nearcopy names only the NPC");
    if (TalkTestParse("nearcopy 5", &n, &t, &e)) Fail("P25: without the out flag nearcopy is refused (the old callers)");
}

/* The carried hand-in fixture's TEST-ONLY levers: crimetest bountyset, koself name, capturetest carry name, talkcarriers ev/act and
   talkprompt answer act - argument shapes, '_' as a space, refusals. */
void t_t430_lever_shapes_parse()
{
    std::string p, l;
    int amount = 9, sec = 9, ev = 9, act = 9;
    unsigned int c = 9;
    if (!coopsay::BountySetParse("Bar_Thug Bounty_Office_Clerk 2500", &p, &l, &amount) || p != "bar thug" || l != "bounty office clerk" || amount != 2500)
        Fail("bountyset: two '_' names and an amount must parse");
    if (!coopsay::BountySetParse("  Thug\tGUARD 0x10 ", &p, &l, &amount) || p != "thug" || l != "guard" || amount != 16)
        Fail("bountyset: spacing, case and a hex amount");
    if (coopsay::BountySetParse("Thug Guard", &p, &l, &amount)) Fail("bountyset without an amount must be refused");
    if (coopsay::BountySetParse("Thug Guard 0", &p, &l, &amount)) Fail("bountyset amount 0 must be refused");
    if (coopsay::BountySetParse("Thug Guard 1000001", &p, &l, &amount)) Fail("bountyset over the amount cap must be refused");
    if (coopsay::BountySetParse("Bar Thug Guard 100", &p, &l, &amount)) Fail("bountyset: a name with a real space must be refused");
    if (coopsay::BountySetParse("{person} Guard 100", &p, &l, &amount)) Fail("bountyset: an unresolved placeholder must be refused");
    if (coopsay::BountySetParse("12 Guard 100", &p, &l, &amount)) Fail("bountyset: a number as a name must be refused");
    if (coopsay::BountySetParse("_ Guard 100", &p, &l, &amount) || !p.empty()) Fail("bountyset: an empty name key must be refused");
    if (!coopsay::KoSelfNameParse("name Bar_Thug", &p, &sec) || p != "bar thug" || sec != 0) Fail("koself name: seconds default 0");
    if (!coopsay::KoSelfNameParse(" name  Bar_Thug 120", &p, &sec) || p != "bar thug" || sec != 120) Fail("koself name with seconds");
    if (coopsay::KoSelfNameParse("name Bar_Thug 601", &p, &sec)) Fail("koself name: seconds over 600 must be refused");
    if (coopsay::KoSelfNameParse("name", &p, &sec)) Fail("koself name without a name must be refused");
    if (coopsay::KoSelfNameParse("name Bar Thug", &p, &sec)) Fail("koself name: a name with a real space must be refused");
    if (coopsay::KoSelfNameParse("4194305 30", &p, &sec)) Fail("koself <uid> is not the name form");
    if (!coopsay::CarryNameParse("carry 4194305 name Bar_Thug", &c, &p) || c != 4194305 || p != "bar thug") Fail("carry name must parse");
    if (!coopsay::CarryNameParse("carry 0x10 name thug", &c, &p) || c != 16 || p != "thug") Fail("carry name: a hex carrier");
    if (coopsay::CarryNameParse("carry 4194305 4194306", &c, &p)) Fail("carry <carrier> <body> is not the name form");
    if (coopsay::CarryNameParse("carry 0 name Thug", &c, &p)) Fail("carry name: carrier 0 must be refused");
    if (coopsay::CarryNameParse("carry 5 name Bar Thug", &c, &p)) Fail("carry name: a name with a real space must be refused");
    if (coopsay::CarryNameParse("carried 5 name Thug", &c, &p)) Fail("carried is not the carry name form");
    if (!coopsay::TalkCarriersOptParse("", &ev, &act) || ev != -1 || act != -1) Fail("talkcarriers: no options");
    if (!coopsay::TalkCarriersOptParse(" ev 1 act 10", &ev, &act) || ev != 1 || act != 10) Fail("talkcarriers: ev and act");
    if (!coopsay::TalkCarriersOptParse("act 10", &ev, &act) || ev != -1 || act != 10) Fail("talkcarriers: act alone");
    if (!coopsay::TalkCarriersOptParse("act 10 ev 0x3", &ev, &act) || ev != 3 || act != 10) Fail("talkcarriers: either order, hex");
    if (coopsay::TalkCarriersOptParse("ev", &ev, &act)) Fail("talkcarriers: ev without a value must be refused");
    if (coopsay::TalkCarriersOptParse("ev 1 ev 2", &ev, &act)) Fail("talkcarriers: ev twice must be refused");
    if (coopsay::TalkCarriersOptParse("act 256", &ev, &act)) Fail("talkcarriers: act over 255 must be refused");
    if (coopsay::TalkCarriersOptParse("foo 1", &ev, &act)) Fail("talkcarriers: an unknown option must be refused");
    if (coopsay::TalkCarriersOptParse("ev 1 act 2 x", &ev, &act)) Fail("talkcarriers: a trailing word must be refused");
    int mode = 9, idx = 9;
    unsigned int n = 9, t = 9;
    std::string sid;
    if (!cooptalk::TalkPromptParse("answer act 10", &mode, &n, &t, &sid, &idx) || mode != cooptalk::kTalkLeverAnswerAct || idx != 10)
        Fail("talkprompt answer act 10 must parse");
    if (!cooptalk::TalkPromptParse("answer 3", &mode, &n, &t, &sid, &idx) || mode != cooptalk::kTalkLeverAnswer || idx != 3)
        Fail("talkprompt answer <k> must still parse");
    if (cooptalk::TalkPromptParse("answer act", &mode, &n, &t, &sid, &idx)) Fail("answer act without a type must be refused");
    if (cooptalk::TalkPromptParse("answer act 256", &mode, &n, &t, &sid, &idx)) Fail("answer act 256 must be refused");
    if (cooptalk::TalkPromptParse("answer act x", &mode, &n, &t, &sid, &idx)) Fail("answer act x must be refused");
    if (cooptalk::TalkPromptParse("answer act 10 1", &mode, &n, &t, &sid, &idx)) Fail("answer act with a trailing word must be refused");
    if (!cooptalk::TalkPromptParse("joinact 18", &mode, &n, &t, &sid, &idx) || mode != cooptalk::kTalkLeverJoinAct || idx != 18)
        Fail("talkprompt joinact 18 must parse");
    if (cooptalk::TalkPromptParse("joinact", &mode, &n, &t, &sid, &idx)) Fail("joinact without a type must be refused");
    if (cooptalk::TalkPromptParse("joinact 256", &mode, &n, &t, &sid, &idx)) Fail("joinact 256 must be refused");
    if (cooptalk::TalkPromptParse("joinact 3x", &mode, &n, &t, &sid, &idx)) Fail("joinact 3x must be refused");
    if (cooptalk::TalkPromptParse("joinact 3 1", &mode, &n, &t, &sid, &idx)) Fail("joinact with a trailing word must be refused");
}

/* P26 stages 1-3. `talkprompt [npcUid|near] [targetUid|near] [lineSid|auto]` | `talkprompt answer <0..9>` | `talkprompt close`. */
void t_p26s1_talkprompt_parse()
{
    using namespace cooptalk;
    int mode = 9, idx = 9;
    unsigned int n = 9, t = 9;
    std::string sid = "x";
    if (!TalkPromptParse("", &mode, &n, &t, &sid, &idx) || mode != kTalkLeverStart || n != 0 || t != 0 || !sid.empty())
        Fail("no arguments must be near near auto");
    if (!TalkPromptParse("12 near 5555-rebirth.mod", &mode, &n, &t, &sid, &idx) || mode != kTalkLeverStart || n != 12 || t != 0
        || sid != "5555-rebirth.mod")
        Fail("an NPC uid and a line sid must parse");
    if (!TalkPromptParse(" near  0x22 auto ", &mode, &n, &t, &sid, &idx) || n != 0 || t != 0x22 || !sid.empty())
        Fail("a hex target uid and auto must parse");
    if (!TalkPromptParse("answer 3", &mode, &n, &t, &sid, &idx) || mode != kTalkLeverAnswer || idx != 3) Fail("answer 3 must parse");
    if (!TalkPromptParse("close", &mode, &n, &t, &sid, &idx) || mode != kTalkLeverClose) Fail("close must parse");
    if (TalkPromptParse("answer", &mode, &n, &t, &sid, &idx)) Fail("answer without an index must be refused");
    if (TalkPromptParse("answer 10", &mode, &n, &t, &sid, &idx)) Fail("answer 10 must be refused");
    if (TalkPromptParse("answer x", &mode, &n, &t, &sid, &idx)) Fail("answer x must be refused");
    if (TalkPromptParse("close now", &mode, &n, &t, &sid, &idx)) Fail("close with an argument must be refused");
    if (TalkPromptParse("0 near", &mode, &n, &t, &sid, &idx)) Fail("uid 0 must be refused");
    if (TalkPromptParse("abc", &mode, &n, &t, &sid, &idx)) Fail("a word as the NPC must be refused");
    if (TalkPromptParse("1 2 3 4", &mode, &n, &t, &sid, &idx)) Fail("four arguments must be refused");
    if (TalkPromptParse("near 4294967296", &mode, &n, &t, &sid, &idx)) Fail("a uid above 32 bits must be refused");
    if (TalkPromptParse("near near " + std::string(129, 's'), &mode, &n, &t, &sid, &idx)) Fail("a 129-byte line sid must be refused");
}

/* P3-b (review-p3 HIGH). The say() decision table: our apply first, then off-thread, then no uid, then a handled
   replay; an unhandled replay is a NEW line - sent by the owner, dropped on a copy. */
void t_p3b_say_classify_splits_pass_through_and_checks_replays()
{
    using namespace coopsay;
    /* applying wins over everything */
    if (SayClassify(1, 0, 0, 0, 1, 0) != kSayActApply) Fail("our own apply must pass whatever else is true");
    if (SayClassify(0, 0, 1, 1, 0, 0) != kSayActPassOffThread) Fail("off the main thread must pass as passOffThread");
    if (SayClassify(0, 1, 0, 0, 1, 0) != kSayActPassNoUid) Fail("no uid must pass as passNoUid, replay or not");
    if (SayClassify(0, 1, 1, 1, 1, 1) != kSayActPassReplay) Fail("an owner's handled replay must pass without a re-send");
    if (SayClassify(0, 1, 1, 0, 1, 1) != kSayActPassReplay) Fail("a copy's handled replay (our apply parked it) must pass");
    if (SayClassify(0, 1, 1, 1, 0, 0) != kSayActSend) Fail("an owner's new line must be sent");
    if (SayClassify(0, 1, 1, 1, 1, 0) != kSayActReplaySend) Fail("an owner's unhandled replay must be sent as a new line");
    if (SayClassify(0, 1, 1, 0, 0, 0) != kSayActDrop) Fail("a copy's own line must be dropped");
    if (SayClassify(0, 1, 1, 0, 1, 0) != kSayActReplayDrop) Fail("a copy's unhandled replay must be dropped");
}

/* P3-b. The handled-text marks: per Dialogue, exact text only, spent by the replay, oldest slot reused. */
void t_p3b_say_marks_match_exact_text_once()
{
    coopsay::SayMarkTable m;
    const char* a = "Hey, you!";
    const char* b = "Get lost.";
    const unsigned int ha = coopsay::SayTextHash(a, 9), hb = coopsay::SayTextHash(b, 9);
    if (ha == hb) Fail("two different lines must not hash alike");
    int d1 = 0, d2 = 0;
    m.Mark(&d1, ha, 9);
    if (m.Take(&d2, ha, 9) != 0) Fail("another Dialogue's mark must not match");
    if (m.Take(&d1, hb, 9) != 0) Fail("different text on the same Dialogue must not match");
    if (m.Take(&d1, ha, 9) != 0) Fail("a mismatched replay spends the mark - the parked text was replaced");
    m.Mark(&d1, ha, 9);
    if (m.Take(&d1, ha, 9) != 1) Fail("the handled text must match once");
    if (m.Take(&d1, ha, 9) != 0) Fail("and only once");
    m.Mark(&d1, ha, 9); m.Mark(&d1, hb, 9);
    if (m.Take(&d1, hb, 9) != 1) Fail("a re-mark of the same Dialogue must replace its text");
    m.Mark(0, ha, 9);
    if (m.Take(0, ha, 9) != 0) Fail("a null Dialogue is never marked");
    /* Full table: the oldest slot goes first. */
    char ds[coopsay::kSayMarkSlots + 1];
    for (int i = 0; i <= coopsay::kSayMarkSlots; ++i) m.Mark(&ds[i], ha, 9);
    if (m.Take(&ds[0], ha, 9) != 0) Fail("the oldest mark must be the one reused when the table is full");
    if (m.Take(&ds[coopsay::kSayMarkSlots], ha, 9) != 1) Fail("the newest mark must survive a full table");
    if (m.Take(&ds[1], ha, 9) != 1) Fail("the second-oldest mark must survive one overflow");
}

/* P7s (F553; user saw the dust-storm pose on the mirror in T244). MSG_WEATHER names its region by FCS stringID.
   Round trip bit-for-bit, and the hostile / truncated / old-layout payloads are each refused by their own reason. */
void t_p7s_weather_round_trips_by_region_name()
{
    unsigned int w[16]; for (int i = 0; i < 16; ++i) w[i] = 0xA5000000u + (unsigned int)i * 7u;
    std::vector<char> b;
    if (!coopwx::EncodeWeather(&b, "54321-rebirth.mod", 17, 3u, -12, 0xFFFFFFFFu, w)) { Fail("a good name did not encode"); return; }
    if (b.size() != (size_t)(4 + 17 + 76)) Fail("the encoded WEATHER is " + I((int)b.size()) + " bytes, not 97");
    coopwx::WxWire o;
    if (coopwx::DecodeWeather(&b[0], b.size(), &o) != coopwx::kWxDecodeOk) { Fail("a whole WEATHER did not decode"); return; }
    if (o.nameLen != 17 || std::strcmp(o.name, "54321-rebirth.mod") != 0) Fail("the region name did not round-trip");
    if (o.seasonIdx != 3u || o.endDay != -12 || o.weatherIdx != 0xFFFFFFFFu) Fail("season / end day / weather did not round-trip");
    for (int i = 0; i < 16; ++i) if (o.w[i] != w[i]) { Fail("word " + I(i) + " did not round-trip"); break; }
    /* The cap is inclusive: 96 bytes encode and decode, 97 do not encode. */
    {
        const std::string n96(coopwx::kWxNameMax, 'x'), n97(coopwx::kWxNameMax + 1, 'x');
        std::vector<char> c;
        if (!coopwx::EncodeWeather(&c, n96.c_str(), (unsigned int)n96.size(), 0, 0, 0, w) || coopwx::DecodeWeather(&c[0], c.size(), &o) != coopwx::kWxDecodeOk || o.nameLen != 96)
            Fail("a 96-byte name must round-trip");
        std::vector<char> d;
        if (coopwx::EncodeWeather(&d, n97.c_str(), (unsigned int)n97.size(), 0, 0, 0, w) || !d.empty()) Fail("a 97-byte name must not encode, and must append nothing");
        if (coopwx::EncodeWeather(&d, "", 0, 0, 0, 0, w) || coopwx::EncodeWeather(&d, "a\nb", 3, 0, 0, 0, w)) Fail("an empty or control-byte name must not encode");
    }
}
void t_p7s_weather_refuses_hostile_and_old_payloads()
{
    unsigned int w[16]; for (int i = 0; i < 16; ++i) w[i] = (unsigned int)i;
    std::vector<char> b;
    coopwx::EncodeWeather(&b, "7-gamedata.base", 15, 1, 2, 3, w);
    coopwx::WxWire o;
    if (coopwx::DecodeWeather(0, 0, &o) != coopwx::kWxDecodeTooShort) Fail("an empty payload must be too short");
    if (coopwx::DecodeWeather(&b[0], 3, &o) != coopwx::kWxDecodeTooShort) Fail("3 bytes must be too short");
    /* THE HOSTILE LENGTH: refused on the length itself, before any byte of the name is read. */
    {
        std::vector<char> h(b);
        unsigned int huge = 0xFFFFFFFFu; std::memcpy(&h[0], &huge, 4);
        if (coopwx::DecodeWeather(&h[0], h.size(), &o) != coopwx::kWxDecodeNameTooLong) Fail("a 0xFFFFFFFF name length must be refused as too long");
        huge = coopwx::kWxNameMax + 1; std::memcpy(&h[0], &huge, 4);
        if (coopwx::DecodeWeather(&h[0], h.size(), &o) != coopwx::kWxDecodeNameTooLong) Fail("a 97 name length must be refused as too long");
        unsigned int zero = 0; std::memcpy(&h[0], &zero, 4);
        if (coopwx::DecodeWeather(&h[0], h.size(), &o) != coopwx::kWxDecodeNameEmpty) Fail("a zero name length must be refused");
    }
    /* A length that lies about the size in either direction. */
    if (coopwx::DecodeWeather(&b[0], b.size() - 1, &o) != coopwx::kWxDecodeSizeMismatch) Fail("a truncated WEATHER must be refused");
    {
        std::vector<char> t(b); t.push_back(0);
        if (coopwx::DecodeWeather(&t[0], t.size(), &o) != coopwx::kWxDecodeSizeMismatch) Fail("a WEATHER with a trailing byte must be refused");
    }
    /* A PROTOCOL-43 PAYLOAD (u32 region index 5, then 76 bytes: 80 in all) is refused, never read as a 5-byte name. */
    {
        std::vector<char> old(80, 0); unsigned int five = 5; std::memcpy(&old[0], &five, 4);
        if (coopwx::DecodeWeather(&old[0], old.size(), &o) != coopwx::kWxDecodeSizeMismatch) Fail("an index-keyed (protocol 43) WEATHER must be refused");
    }
    {
        std::vector<char> c(b); c[4 + 3] = 0x01;
        if (coopwx::DecodeWeather(&c[0], c.size(), &o) != coopwx::kWxDecodeNameBadByte) Fail("a control byte in the name must be refused");
    }
    if (std::strcmp(coopwx::WeatherDecodeText(coopwx::kWxDecodeNameTooLong), "unknown") == 0) Fail("every reason must have words");
}

/* ================= W1w: WORLD NAMES, WORLD FOLDERS, DISPLAY NAMES, THE MIGRATION PLAN (decisions 58, 59) =================
 * src/common/worlddir.h. Every input is a literal; the migration tests move names around a set that stands in for the
 * folder, so "moves nothing twice", "a second plan is empty" and "the reverse restores it" are checked on the plan's own
 * output, not asserted. */
void t_w1w_world_names_refuse_what_windows_refuses()
{
    const std::string a48(coopcfg::kCfgWorldFieldMax, 'a');
    const std::string a49(coopcfg::kCfgWorldFieldMax + 1, 'a');
    const char* const good[] = { "coop", "northlands", "My-World_2", "a.b", "com0", "com10", "console", "lpt", "nul2",
                                 "mirror", "queue", "queue-x", "conin", "worlds.migrated",
                                 "New World", "north lands 2", "a b" };   /* owner decision 249 a: single spaces inside */
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
    {
        std::string why;
        if (!coopworld::WorldNameOk(good[i], &why)) Fail(std::string("'") + good[i] + "' is a fine world name, refused: " + why);
    }
    if (!coopworld::WorldNameOk(a48, 0)) Fail("a name at the cfg cap (48) must pass");
    const std::string bad[] = { "", ".", "..", ".hidden", "coop.", " coop", "coop ", "CON", "con", "con.txt", "Com1",
                                "LPT9.x", "nul", "aux.dat", "prn", "a<b", "a>b", "a:b", "a\"b", "a/b", "a\\b", "a|b",
                                "a?b", "a*b", "a\tb", std::string("a\x01" "b"), std::string("a\x7f" "b"),
                                std::string("caf\xe9"), "a  b", "New  World", " New World", "New World ", a49,   /* 249a: double, leading, trailing space */
                                /* W1w-b: control bytes at the ends (trimmed by CfgNameFieldOk), '~', store names, devices */
                                "coop\t", "con\t", "coop.\t", "\tcoop", "coop\r", "coop\n", "coop\r\n", "northl~1", "~",
                                "mirror-1", "MIRROR-x", "worlds.migrated.txt", "Worlds.Migrated.TXT", "queue-coop.txt",
                                "Queue-x.TXT", "CONIN$", "conout$", "conin$.x" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        std::string why;
        if (coopworld::WorldNameOk(bad[i], &why)) Fail("'" + bad[i] + "' must be refused as a world name");
        else if (why.empty()) Fail("the refusal of '" + bad[i] + "' must say why");
    }
    if (!coopworld::WorldNamesCollide("Coop", "coop")) Fail("'Coop' and 'coop' are ONE world (Windows folders ignore case)");
    if (coopworld::WorldNamesCollide("coop", "coop2")) Fail("'coop' and 'coop2' are two worlds");
    if (coopworld::WorldFolderName("NorthLands") != "northlands") Fail("the folder is the lower-cased name");
    if (coopworld::WorldDir("C:\\k\\save", "Coop") != "C:\\k\\save\\Shared Wastelands\\worlds\\coop")
        Fail("WorldDir = root\\Shared Wastelands\\worlds\\<folder> (read '" + coopworld::WorldDir("C:\\k\\save", "Coop") + "')");
    if (coopworld::WorldDir("C:\\k\\save\\", "coop") != "C:\\k\\save\\Shared Wastelands\\worlds\\coop") Fail("a trailing slash on the root is not doubled");
    if (coopworld::WorldDirIn("", "coop") != "") Fail("owner 244: no top folder must be no world folder, never one at the drive's root");
    /* W1w-b: the cfg world value is not checked when read, so an invalid one is the default world's folder ("new world",
       owner decision 249 a), and says so. */
    const char* const invalid[] = { "..", "coop.", "con\t", "" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        bool fb = false;
        const std::string d = coopworld::WorldDir("C:\\k\\save", invalid[i], &fb);
        if (d != "C:\\k\\save\\Shared Wastelands\\worlds\\new world" || !fb)
            Fail(std::string("world '") + invalid[i] + "' must fall back to the default world's folder and say so (read '" + d + "')");
    }
    bool fb = true;
    if (coopworld::WorldDir("C:\\k\\save", "NorthLands", &fb) != "C:\\k\\save\\Shared Wastelands\\worlds\\northlands" || fb)
        Fail("a valid world is its own folder and is not reported as a fallback");
    /* owner decision 249 a: the default world is "New World"; a spaced name is one folder, one settings line, one argument */
    if (std::string(coopworld::kDefaultWorld) != "New World") Fail("249a: the default world is 'New World'");
    if (!coopworld::WorldNameOk(coopworld::kDefaultWorld, 0)) Fail("249a: the default world must pass the world-name rule");
    if (coopworld::WorldDir("C:\\k\\save", "New World") != "C:\\k\\save\\Shared Wastelands\\worlds\\new world")
        Fail("249a: 'New World' is the folder 'new world'");
    if (coopworld::WorldOrDefault("", 0) != "New World") Fail("249a: an empty world falls back to 'New World'");
    {
        coopcfg::CfgFields f; f.world = "New World";
        coopcfg::CfgFields back; coopcfg::CfgParseText(coopcfg::CfgFormat(f), &back, 0, 0, 0, 0);
        if (back.world != "New World") Fail("249a: world=New World must read back whole (read '" + back.world + "')");
    }
    {
        /* the panel's launch line (ui.cpp, the notebook start) quotes --world; split it the way the C runtime splits argv
           (a world name can hold neither '"' nor '\\', so quotes are the only special characters) */
        const std::string w = "New World";
        if (w.find_first_of("\"\\") != std::string::npos || !coopworld::WorldNameOk(w, 0)) Fail("249a: the test name must be a valid world");
        const std::string line = "\"C:\\g\\SharedWastelandsServer.exe\" --port 27016 --world \"" + w + "\" --root \"C:\\k\\worlds\" --parent 42";
        std::vector<std::string> av; std::string cur; bool inQ = false, any = false;
        for (size_t k = 0; k <= line.size(); ++k)
        {
            const char c = k < line.size() ? line[k] : ' ';
            if (c == '"') { inQ = !inQ; any = true; continue; }
            if (c == ' ' && !inQ) { if (any) av.push_back(cur); cur.clear(); any = false; continue; }
            cur += c; any = true;
        }
        std::string got;
        for (size_t k = 0; k + 1 < av.size(); ++k) if (av[k] == "--world") got = av[k + 1];
        if (av.size() != 9 || got != w) Fail("249a: --world \"New World\" must be one argument (read '" + got + "')");
    }
    if (coopworld::WelcomeWorldDecide("New World", "new world", "") != coopworld::kWelcomeWorldConfirm)
        Fail("249a: a WELCOME naming 'new world' confirms a game on 'New World'");
    if (coopworld::WelcomeWorldDecide("", "New World", "") != coopworld::kWelcomeWorldConfirm)
        Fail("249a: an empty world is the default 'New World', which a WELCOME naming it confirms");
}

void t_w1w_display_names_and_the_playername_cfg_key()
{
    const std::string n24(coopcfg::kCfgPlayerNameFieldMax, 'x');
    const std::string n25(coopcfg::kCfgPlayerNameFieldMax + 1, 'x');
    const std::string good[] = { "A", "Bob Smith", "x=y", "Beep-2 (the shek)", n24 };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i)
        if (!coopworld::DisplayNameOk(good[i], 0)) Fail("'" + good[i] + "' is a fine display name");
    const std::string bad[] = { "", n25, " Bob", "Bob ", "a\tb", "a\nb", "a\rb", "a#b", "a;b", std::string("caf\xe9") };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        std::string why;
        if (coopworld::DisplayNameOk(bad[i], &why)) Fail("'" + bad[i] + "' must be refused as a display name");
        else if (why.empty()) Fail("the refusal of a display name must say why");
    }
    /* The cfg key round trips and is not an unknown key; the other keys are unchanged by its presence. */
    coopcfg::CfgFields f;
    f.role = coopcfg::kCfgHost; f.hostPort = 7777; f.world = "northlands"; f.playerName = "Bob Smith";
    f.playerId = "0123456789abcdef0123456789abcdef";
    coopcfg::CfgFields g; long long unknown = -1, badLines = -1;
    std::string fmtWhy = "x";
    coopcfg::CfgFormat(f, &fmtWhy);
    if (!fmtWhy.empty()) Fail("a good name writes with no complaint (read '" + fmtWhy + "')");
    coopcfg::CfgParseText(coopcfg::CfgFormat(f), &g, 0, 0, &unknown, &badLines);
    if (g.playerName != "Bob Smith") Fail("playername did not round trip (read '" + g.playerName + "')");
    if (unknown != 0 || badLines != 0) Fail("the file this build writes must have no unknown and no bad lines");
    if (g.world != f.world || g.playerId != f.playerId || g.hostPort != f.hostPort) Fail("adding playername changed another key");
    /* A name that is too long is a bad line and is dropped, not truncated. */
    coopcfg::CfgFields h;
    coopcfg::CfgParseText("playername=" + n25 + "\r\n", &h, 0, 0, 0, &badLines);
    if (!h.playerName.empty() || badLines != 1) Fail("an over-long playername must be one bad line and leave the name empty");
    /* No name typed = no line written. */
    coopcfg::CfgFields e;
    if (coopcfg::CfgFormat(e).find("playername") != std::string::npos) Fail("an empty name must write no playername line");
    /* W1w-b: a name DisplayNameOk refuses is never written, and the writer says so; the rest of the file is intact. */
    const std::string refused[] = { "Bob;x", "a#b", " Bob", "a\tb", n25 };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    {
        coopcfg::CfgFields r = f; r.playerName = refused[i];
        std::string w;
        const std::string text = coopcfg::CfgFormat(r, &w);
        if (text.find("playername") != std::string::npos) Fail("the refused name '" + refused[i] + "' must not be written");
        if (w.empty()) Fail("CfgFormat must say why '" + refused[i] + "' was not written");
        coopcfg::CfgFields back;
        coopcfg::CfgParseText(text, &back, 0, 0, 0, 0);
        if (back.world != f.world || back.playerId != f.playerId || !back.playerName.empty())
            Fail("dropping the name '" + refused[i] + "' must leave every other key as it was");
    }
}

void t_w1w_the_world_table_is_sorted_and_skips_what_is_not_a_world()
{
    const std::string id1 = "0123456789abcdef0123456789abcdef";
    const std::string id2 = "fedcba9876543210fedcba9876543210";
    std::vector<coopworld::WorldFolderInput> in;
    coopworld::WorldFolderInput c;
    c.folder = "coop"; c.worldTxt = coopworld::WorldTxtFormat("coop", 100); c.lastPlayed = "500";
    c.playerLines.push_back(coopworld::PlayerNameLineFormat(id1, "Bob"));
    c.playerLines.push_back(coopworld::PlayerNameLineFormat(id2, "alice"));
    c.playerLines.push_back(coopworld::PlayerNameLineFormat(id1, "Bobby"));   /* a later line for the same id wins */
    c.playerLines.push_back("not-an-id\tMallory");
    in.push_back(c);
    coopworld::WorldFolderInput n;
    n.folder = "northlands"; n.worldTxt = coopworld::WorldTxtFormat("NorthLands", 200); n.lastPlayed = "900";
    in.push_back(n);
    coopworld::WorldFolderInput m; m.folder = "mirror-4242"; in.push_back(m);                       /* no world.txt */
    coopworld::WorldFolderInput o; o.folder = "other"; o.worldTxt = coopworld::WorldTxtFormat("coop", 1); in.push_back(o);
    coopworld::WorldFolderInput d; d.folder = "NORTHLANDS"; d.worldTxt = n.worldTxt; in.push_back(d);   /* same world */
    std::vector<std::string> skipped;
    const std::vector<coopworld::WorldRow> rows = coopworld::BuildWorldTable(in, &skipped);
    if (rows.size() != 2) { Fail("two worlds expected, read " + I((int)rows.size())); return; }
    if (rows[0].name != "NorthLands" || rows[0].folder != "northlands" || rows[0].lastPlayedUnix != 900)
        Fail("the newest world is first and keeps its name as typed");
    if (rows[1].name != "coop" || rows[1].lastPlayedUnix != 500) Fail("coop is second");
    if (rows[1].players.size() != 2 || rows[1].players[0] != "alice" || rows[1].players[1] != "Bobby")
        Fail("coop's players are alice and Bobby, sorted ignoring case, the malformed line dropped");
    if (!rows[0].players.empty()) Fail("northlands has no players");
    if (skipped.size() != 3) Fail("the mirror folder, the mismatched folder and the case duplicate are skipped and named (" + I((int)skipped.size()) + ")");
    std::string nm; long long created = 0;
    if (!coopworld::WorldTxtParse(coopworld::WorldTxtFormat("My-World", 1726000000), &nm, &created) || nm != "My-World" || created != 1726000000)
        Fail("world.txt round trips");
    if (coopworld::WorldTxtParse("v1\tCON\t5\n", 0, 0)) Fail("a world.txt naming a device is not a world");
}

/* Reset Population in the Level Editor: refused in a multiplayer world and run unchanged in single player (fed by the same
   HandSaveBlockedDecide the hand save / load refusals use); the button greyed in a multiplayer world while lit, lit again in
   single player only where the mod greyed it; the button found by its layout name bare or after a layout prefix; the hover
   note's words exactly as approved. */
void t_t1038_editor_reset_population()
{
    using namespace coopprof;
    struct DRow { bool mp; int want; const char* what; };
    static const DRow d[] = {
        { true,  kEditorResetRefuse, "multiplayer world: refused, the original not called" },
        { false, kEditorResetRun,    "single player: the original runs unchanged" } };
    for (size_t i = 0; i < sizeof(d) / sizeof(d[0]); ++i)
        if (EditorResetDecide(d[i].mp) != d[i].want) Fail(std::string("editor reset: ") + d[i].what);
    if (EditorResetDecide(HandSaveBlockedDecide(0, true)) != kEditorResetRefuse) Fail("editor reset: title down in a multiplayer world: refused");
    if (EditorResetDecide(HandSaveBlockedDecide(0, false)) != kEditorResetRun) Fail("editor reset: title down in single player: runs");
    if (EditorResetDecide(HandSaveBlockedDecide(1, true)) != kEditorResetRun || EditorResetDecide(HandSaveBlockedDecide(-1, true)) != kEditorResetRun)
        Fail("editor reset: title up or unreadable: not refused");
    struct BRow { bool mp, byMod, enabled; int want; const char* what; };
    static const BRow b[] = {
        { true,  false, true,  kEditorBtnGrey,  "multiplayer, lit: greyed" },
        { true,  true,  true,  kEditorBtnGrey,  "multiplayer, lit again by the engine: greyed again" },
        { true,  true,  false, kEditorBtnLeave, "multiplayer, already greyed by the mod: left" },
        { true,  false, false, kEditorBtnLeave, "multiplayer, greyed by the engine: left" },
        { false, false, true,  kEditorBtnLeave, "single player, never greyed by the mod: untouched" },
        { false, false, false, kEditorBtnLeave, "single player, greyed by the engine: untouched" },
        { false, true,  false, kEditorBtnLight, "single player, greyed by the mod in an earlier world: lit again" },
        { false, true,  true,  kEditorBtnLight, "single player, the mod's mark left on a lit button: lit (the mark cleared)" } };
    for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); ++i)
        if (EditorResetButtonDecide(b[i].mp, b[i].byMod, b[i].enabled) != b[i].want) Fail(std::string("editor reset button: ") + b[i].what);
    struct NRow { const char* name; bool want; };
    static const NRow n[] = {
        { "populate", true }, { "000000001A2B3C40_populate", true }, { "towns", false }, { "0000_towns", false },
        { "populateX", false }, { "0000_populateX", false }, { "x_y_populate", false }, { "", false }, { "_populate", true } };
    for (size_t i = 0; i < sizeof(n) / sizeof(n[0]); ++i)
        if (EditorResetButtonName(n[i].name) != n[i].want) Fail(std::string("editor reset button name: '") + n[i].name + "'");
    if (std::string(kEditorResetRefusedText) != "Reset Population is turned off in a multiplayer world. It deletes your own squad, and the other players would keep the old townspeople.")
        Fail("editor reset: the hover note's words are not the approved text");
}

}   /* namespace suite */
