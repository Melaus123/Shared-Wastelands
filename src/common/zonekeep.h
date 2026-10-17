/* THE PLAYERS' PIECES IN AN AREA RECORD, read by the world server at RESET WORLD with the players' buildings kept.

   An area record on the world server (zone.<x>.<y>.platoon, recname.h) is Kenshi's data container - the format ownrec.h skips through
   for squad files: version 15, the container id, the record count; per record a header (a, type, c, name, sid, d), seven named-value
   lists (bool 1 byte, float, int, vec3, quat, string, file), the reference lists (name -> entries of (sid, int, int, int)) and the
   instances (id, target sid, position, rotation, strings). Its record 0-buildinglist lists every building and furniture piece of
   the area: an instance per piece whose target is the piece's base record and whose strings name its state record (type 0x23, the
   owner, the height, the build state, the host building, the building's doors) and, for a box, its inventory record (type 0x29, whose
   instances name item records, type 0x2A; a backpack's item record names its own inventory record the same way, and a pack inside
   it its own in turn).

   This header: ParseContainer reads the bytes into values (every read bounds-checked: a damaged or cut file is refused with a
   reason, never read past); ExtractKeptPieces takes from it the pieces a player owns, with their own state (StateOf), their box
   contents (a pack's own items with it, at any depth to kMaxPackDepth) and their doors; EncodeKept /
   DecodeKept write and read the list the reset world keeps (kKeptFile); EncodeKeptState / DecodeKeptState its laying state
   (kKeptStateFile); RecordHoldsPiece / RecordHoldsItems say whether a new area record holds a kept piece and its box contents;
   MergeUndone carries a reset world's unfinished rows into a second reset's list. Pure: no Windows call, no file touched, no CRT locale (a
   float travels as the 8 hex digits of its bits). Header-only. No C++11. */
#ifndef COOP_ZONEKEEP_H
#define COOP_ZONEKEEP_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <string.h>
#include "slotwire.h"   /* coopslot::OwnerSlotOfId - whose a piece is, as the record's writer named it */
#include "poskey.h"     /* the position key, the same bytes the game builds */

namespace zonekeep {

const int kContainerVersion = 15;
const int kTypeState = 0x23;       /* a piece's state record */
const int kTypeInventory = 0x29;   /* a box's inventory record */
const int kTypeItem = 0x2A;        /* one item record */
const char* const kBuildingList = "0-buildinglist";
const char* const kPlayerFactionSid = "204-gamedata.base";   /* the player faction: the area's writer's own player */
const char* const kKeptFile = "kept-pieces.txt";             /* in the reset world's folder */
const char* const kKeptHeader = "kept-pieces";               /* the file's first field; the second is kKeptFormat */
const int kKeptFormat = 3;   /* S / F / C lines (the piece's own state), items' uniform and crafter, J lines at any depth, doors' hardness and
                                broken; lists of format 1 (no J or D lines) and 2 (J lines one level deep, D lines of 7 fields) are read too */
const int kMaxPackDepth = 4; /* pack levels kept under a box item (a box item is depth 0; an item at this depth keeps no list of its own:
                                the items below it are kept beside it, FlattenItemsOf) */
const int kMaxPackItems = 256;   /* items one pack carries in a KEPT row (keptwire.h kMaxInner) */
const int kMaxBoxItems = 512;    /* box items one KEPT row carries (keptwire.h kMaxItems) */
const int kMaxText = 128;    /* a kept name, layout name, uniform or crafter longer than this, or holding a control character, is left out */
const int kMaxInputs = 16;   /* input stocks kept per piece ('consumption amount-N', N below this) */
/* the parts of a piece's own state its state record held (KeptPiece::has): only these are laid back */
const int kHasName = 1, kHasLayout = 2, kHasPause = 4, kHasPower = 8, kHasProduction = 16, kHasFarm = 32, kHasOwnLock = 64;
const int kHasKnown = 127;
const int kMaxDoors = 32;    /* doors read per building (the state record's fields ending 0 .. 31) */

/* ------------------------------------------------------------------ the container */
struct V3 { float v[3]; };
struct V4 { float v[4]; };
struct Ref { std::string sid; int i0, i1, i2; };
struct Inst { std::string id, target; float pos[3]; float rot[4]; std::vector<std::string> strs; };
struct Rec
{
    int a, type, c, d;
    std::string name, sid;
    std::map<std::string, int> bools, ints;
    std::map<std::string, float> floats;
    std::map<std::string, V3> vecs;
    std::map<std::string, V4> quats;
    std::map<std::string, std::string> strs, files;
    std::map<std::string, std::vector<Ref> > lists;
    std::vector<Inst> insts;
    Rec() : a(0), type(0), c(0), d(0) {}
};
struct Container
{
    int version, id;
    std::vector<Rec> recs;
    size_t end, size;   /* where the last record ended, and the buffer's size (the bytes between are the container's tail) */
    Container() : version(0), id(0), end(0), size(0) {}
};

/* One bounds-checked reader over a byte buffer: every call fails, and stays failed, once a read would pass the end. */
class Reader
{
public:
    Reader(const unsigned char* b, size_t n) : b_(b), n_(n), at_(0), bad_(false) {}
    bool Bad() const { return bad_; }
    size_t At() const { return at_; }
    size_t Left() const { return n_ - at_; }
    bool I32(int* v)
    {
        if (bad_ || n_ - at_ < 4) { bad_ = true; return false; }
        memcpy(v, b_ + at_, 4); at_ += 4; return true;
    }
    bool F32(float* v)
    {
        if (bad_ || n_ - at_ < 4) { bad_ = true; return false; }
        memcpy(v, b_ + at_, 4); at_ += 4; return true;
    }
    bool Byte(int* v)
    {
        if (bad_ || n_ - at_ < 1) { bad_ = true; return false; }
        *v = (int)b_[at_]; at_ += 1; return true;
    }
    bool Str(std::string* s)
    {
        int len = 0;
        if (!I32(&len)) return false;
        if (len < 0 || (size_t)len > n_ - at_) { bad_ = true; return false; }
        s->assign((const char*)b_ + at_, (size_t)len); at_ += (size_t)len; return true;
    }
    /* a count: not negative, and no more entries than bytes left (each entry takes at least minBytes) */
    bool Count(int* v, size_t minBytes)
    {
        if (!I32(v)) return false;
        if (*v < 0 || (size_t)*v > (n_ - at_) / (minBytes > 0 ? minBytes : 1)) { bad_ = true; return false; }
        return true;
    }
private:
    const unsigned char* b_;
    size_t n_, at_;
    bool bad_;
};

inline std::string ZkNum(long long v) { char b[32]; long long u = v < 0 ? -v : v; int n = 0; char t[32];
    do { t[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u != 0 && n < 30);
    int k = 0; if (v < 0) b[k++] = '-'; while (n > 0) b[k++] = t[--n]; b[k] = 0; return std::string(b); }

/* One record off the reader; false = cut or malformed (*why says where). */
inline bool ParseRec(Reader& r, int index, Rec* out, std::string* why)
{
    int n = 0;
    std::string k;
    if (!r.I32(&out->a) || !r.I32(&out->type) || !r.I32(&out->c) || !r.Str(&out->name) || !r.Str(&out->sid) || !r.I32(&out->d))
    { *why = "record " + ZkNum(index) + ": its header is cut short at byte " + ZkNum((long long)r.At()); return false; }
    const std::string at = "record " + ZkNum(index) + " (" + out->sid + "): ";
    if (!r.Count(&n, 5)) { *why = at + "its bool list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i) { int v = 0; if (!r.Str(&k) || !r.Byte(&v)) { *why = at + "a bool is cut short"; return false; } out->bools[k] = v; }
    if (!r.Count(&n, 8)) { *why = at + "its float list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i) { float v = 0; if (!r.Str(&k) || !r.F32(&v)) { *why = at + "a float is cut short"; return false; } out->floats[k] = v; }
    if (!r.Count(&n, 8)) { *why = at + "its int list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i) { int v = 0; if (!r.Str(&k) || !r.I32(&v)) { *why = at + "an int is cut short"; return false; } out->ints[k] = v; }
    if (!r.Count(&n, 16)) { *why = at + "its vec3 list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i)
    {
        V3 v; if (!r.Str(&k) || !r.F32(&v.v[0]) || !r.F32(&v.v[1]) || !r.F32(&v.v[2])) { *why = at + "a vec3 is cut short"; return false; }
        out->vecs[k] = v;
    }
    if (!r.Count(&n, 20)) { *why = at + "its quat list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i)
    {
        V4 v; if (!r.Str(&k) || !r.F32(&v.v[0]) || !r.F32(&v.v[1]) || !r.F32(&v.v[2]) || !r.F32(&v.v[3])) { *why = at + "a quat is cut short"; return false; }
        out->quats[k] = v;
    }
    if (!r.Count(&n, 8)) { *why = at + "its string list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i) { std::string v; if (!r.Str(&k) || !r.Str(&v)) { *why = at + "a string is cut short"; return false; } out->strs[k] = v; }
    if (!r.Count(&n, 8)) { *why = at + "its file list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i) { std::string v; if (!r.Str(&k) || !r.Str(&v)) { *why = at + "a file name is cut short"; return false; } out->files[k] = v; }
    if (!r.Count(&n, 8)) { *why = at + "its reference lists are cut or their count is not possible"; return false; }
    for (int i = 0; i < n; ++i)
    {
        int m = 0;
        if (!r.Str(&k) || !r.Count(&m, 16)) { *why = at + "a reference list is cut short"; return false; }
        std::vector<Ref>& list = out->lists[k];
        for (int j = 0; j < m; ++j)
        {
            Ref e; e.i0 = e.i1 = e.i2 = 0;
            if (!r.Str(&e.sid) || !r.I32(&e.i0) || !r.I32(&e.i1) || !r.I32(&e.i2)) { *why = at + "a reference is cut short"; return false; }
            list.push_back(e);
        }
    }
    if (!r.Count(&n, 40)) { *why = at + "its instance list is cut or its count is not possible"; return false; }
    for (int i = 0; i < n; ++i)
    {
        Inst e;
        int m = 0;
        bool ok = r.Str(&e.id) && r.Str(&e.target);
        for (int j = 0; ok && j < 3; ++j) ok = r.F32(&e.pos[j]);
        for (int j = 0; ok && j < 4; ++j) ok = r.F32(&e.rot[j]);
        ok = ok && r.Count(&m, 4);
        for (int j = 0; ok && j < m; ++j) { std::string s; ok = r.Str(&s); if (ok) e.strs.push_back(s); }
        if (!ok) { *why = at + "an instance is cut short"; return false; }
        out->insts.push_back(e);
    }
    return true;
}

/* THE CONTAINER: false = not one this build reads, cut short or malformed (*why says which and where); nothing past the buffer is
   ever read. The bytes after the last record (the container's own tail) are left unread. */
inline bool ParseContainer(const unsigned char* b, size_t n, Container* out, std::string* why)
{
    Reader r(b, n);
    int count = 0;
    out->recs.clear();
    out->size = n;
    if (!r.I32(&out->version) || !r.I32(&out->id)) { *why = "the file is shorter than its header (" + ZkNum((long long)n) + " bytes)"; return false; }
    if (out->version != kContainerVersion)
    { *why = "the container's version is " + ZkNum(out->version) + ", this build reads " + ZkNum(kContainerVersion); return false; }
    if (!r.Count(&count, 24)) { *why = "the record count is missing or not possible for a file of " + ZkNum((long long)n) + " bytes"; return false; }
    for (int i = 0; i < count; ++i)
    {
        Rec rec;
        if (!ParseRec(r, i, &rec, why)) return false;
        out->recs.push_back(rec);
    }
    out->end = r.At();
    return true;
}

/* ------------------------------------------------------------------ the kept pieces */
struct KeptMat { int index; float count; };   /* '<index>construction mats': the materials delivered to the template's index-th entry */
struct KeptItem
{
    std::string tmpl, material, company, color, section;   /* base data / material / company / color sid, the inventory section */
    std::string uniform;        /* 'uniform': the record id of the faction the item marks its wearer as (Item +0x1E8); "" = none */
    std::string crafter;        /* 'crafter': the name of the character who made it (Item +0x1F8); "" = none */
    float quality, charges;
    int quantity, level, function, unique, slotX, slotY;    /* unique 1 = the record marks it unique */
    int hasOwner;               /* 1 = the record held the stolen mark's owner ticket ('ownedby*') */
    int owner[5];               /* that ticket, raw: 'ownedbyTYPE', 'ownedbyC', 'ownedbyCS', 'ownedbyI', 'ownedbyS' - the five id words of the
                                   hand at Item +0x140 (from +0x148: type, container, container stamp, index, serial); type 11 = nobody */
    std::vector<KeptItem> inner;   /* a pack's own items (its inventory record's instances), each with its own in turn, to kMaxPackDepth */
    KeptItem() : quality(0), charges(0), quantity(0), level(0), function(0), unique(0), slotX(0), slotY(0), hasOwner(0) { for (int i = 0; i < 5; ++i) owner[i] = 0; }
};
/* one door of a building, from its state record's fields ending in the door's number (state0, locked0, ...) */
struct KeptDoor
{
    int index;                  /* the number: the door's place in the building's door list */
    int state;                  /* 'state<i>': 0 closed, 1 open, 2 opening, 3 closing (doorsync.h) */
    int locked, wantsLock;      /* 'locked<i>', 'wants lock<i>' */
    int lockLevel;              /* 'lock lvl<i>' */
    int gateCode;               /* 'gatecode<i>' */
    int ownerSlot;              /* 'owner faction ID<i>' read as the piece's owner is (slotwire.h OwnerSlotOfId); -1 = not a player's */
    float hardness;             /* 'hardness<i>': the door lock's hardness; -1 = the record held none */
    int broken;                 /* 'broken<i>': the door lock is broken (the lock's own save 0x29FF90 writes it) */
    KeptDoor() : index(0), state(0), locked(0), wantsLock(0), lockLevel(0), gateCode(-1), ownerSlot(-1), hardness(-1.0f), broken(0) {}
};
struct KeptStock { int index; float amount; };   /* 'consumption amount-<index>': one input record's stock */
struct KeptPiece
{
    int ownerSlot;              /* the player's slot number (slots.txt) */
    std::string key;            /* the position key (poskey.h) */
    std::string tmpl;           /* the piece's base record sid */
    float pos[3];               /* world x, height, z (the height from the state record's 'world Y pos') */
    float rot[4];               /* the rotation as the building list stores it (w, x, y, z) */
    int floor;                  /* 'floornum' */
    std::string hostKey;        /* furniture: the position key of the building it stands in; "" = free-standing (furniture whose host is not found is not kept) */
    float progress;             /* 'construction progress' */
    std::vector<KeptMat> mats;
    int complete, destroyed, broken;
    int has;                    /* kHas* bits: the parts of the piece's own state its state record held (StateOf) */
    std::string name;           /* 'name' (Building +0x18) */
    std::string interiorName, exteriorName;   /* 'interior layout name' / 'exterior layout name' */
    int pause;                  /* 'pause' (+0x161) */
    int getsBattery, powerOn;   /* 'gets battery' (+0x3B4), 'power on' (+0x3B5) */
    float battery;              /* 'batterycharge' (+0x3C4) */
    float production;           /* 'production amount' (*(+0x448)) */
    float growth, death, start; /* a farm's 'plant growth' (+0x518), 'plant death' (+0x51C), 'plant start' (+0x524) */
    int harvested;              /* 'plants harvested' (+0x528) */
    int lockLevel, lockLocked;  /* the piece's own lock: 'lock lvl', 'locked' */
    float lockHardness;         /* 'hardness' */
    std::vector<KeptStock> inputs;   /* the input stocks, by index */
    std::vector<KeptItem> items;
    std::vector<KeptDoor> doors;
    int hasTown;                /* 1 = its state record names a town (townTYPE ... townS): the outpost townkeep.h matches it to; read at the
                                   reset, never written to kept-pieces.txt nor carried by a KEPT row */
    int townHand[5];            /* that town's handle words: townTYPE, townC, townCS, townI, townS */
    KeptPiece() : ownerSlot(-1), floor(0), progress(0), complete(0), destroyed(0), broken(0), has(0), pause(0), getsBattery(0), powerOn(0),
                  battery(0), production(0), growth(0), death(0), start(0), harvested(0), lockLevel(0), lockLocked(0), lockHardness(0), hasTown(0)
    { pos[0] = pos[1] = pos[2] = 0; rot[0] = 1; rot[1] = rot[2] = rot[3] = 0; for (int i = 0; i < 5; ++i) townHand[i] = 0; }
};
/* What one area gave, for its log line. */
struct KeepStats
{
    int listed;          /* building-list entries */
    int kept;            /* pieces kept */
    int items;           /* items kept in them */
    int nodes;           /* world resource nodes skipped (whoever they name as owner) */
    int nobody;          /* pieces no player owns (a town's, nobody's) */
    int writerUnknown;   /* pieces marked the writer's own player when the area's writer is not known: nobody can be named */
    int ambiguous;       /* pieces marked "coop-peer" (an old two-player world's "the other player"): nobody can be named */
    int noState;         /* entries with no state record */
    int sharedState;     /* entries whose state record an earlier entry already took */
    int noKey;           /* pieces whose position key could not be made */
    int hostMissing;     /* furniture whose host building was not found (not kept: laid with no host it would stand free in the open) */
    std::vector<std::pair<std::string, int> > hostMissingList;   /* each of those: its position key and owner slot, for the reset log */
    int bagItems;        /* kept packs that hold kept items, at any depth (their items are kept with them, KeptItem::inner) */
    int innerItems;      /* the items kept inside packs, at any depth */
    int nestedBags;      /* items at the depth bound (kMaxPackDepth) that hold items of their own (those are kept beside them, FlattenItemsOf) */
    int flattened;       /* items below the depth bound, kept beside the bound's item in the same pack's list */
    int fieldsFixed;     /* fields the KEPT row cannot carry, clamped or dropped alone (FitPiece): a door's state or flags, an item's text or value */
    int packSplit;       /* items past kMaxPackItems in one pack, moved to the box (FitPiece) */
    int itemsOverCap;    /* box items past kMaxBoxItems: not carried (FitPiece) */
    int doors;           /* doors kept with their buildings */
    int leftOut;         /* names, layout names, uniforms, crafters or values the KEPT row cannot carry: left out (the rest of the piece is kept) */
    int inputsLeftOut;   /* input stocks past kMaxInputs: left out */
    std::set<std::string> worldTowns;   /* the towns (TownHandText) that buildings no player owns name: world towns, where townkeep.h
                                           never joins a kept piece to an outpost by distance nor makes an outpost (a bought house) */
    KeepStats() : listed(0), kept(0), items(0), nodes(0), nobody(0), writerUnknown(0), ambiguous(0), noState(0), sharedState(0), noKey(0),
                  hostMissing(0), bagItems(0), innerItems(0), nestedBags(0), flattened(0), fieldsFixed(0), packSplit(0), itemsOverCap(0), doors(0),
                  leftOut(0), inputsLeftOut(0) {}
};

/* The engine's world resource nodes (ore rocks): the world places them and they can carry the player faction's mark - never a
   player's piece (boxowner.h WorldNodeOf names the same three base-game templates). */
inline bool IsWorldNodeTemplate(const std::string& sid)
{
    return sid == "42248-gamedata.base" || sid == "42249-gamedata.base" || sid == "42250-gamedata.base";
}
inline int RecInt(const Rec& r, const char* k, int dflt) { std::map<std::string, int>::const_iterator i = r.ints.find(k); return i == r.ints.end() ? dflt : i->second; }
inline int RecBool(const Rec& r, const char* k) { std::map<std::string, int>::const_iterator i = r.bools.find(k); return (i != r.bools.end() && i->second != 0) ? 1 : 0; }
inline float RecFloat(const Rec& r, const char* k, float dflt) { std::map<std::string, float>::const_iterator i = r.floats.find(k); return i == r.floats.end() ? dflt : i->second; }
inline std::string RecStr(const Rec& r, const char* k) { std::map<std::string, std::string>::const_iterator i = r.strs.find(k); return i == r.strs.end() ? std::string() : i->second; }
inline bool RecHasFloat(const Rec& r, const char* k) { return r.floats.find(k) != r.floats.end(); }
/* the text and float bounds the KEPT row carries (keptwire.h TextOk / FloatOk) */
inline bool ZkTextOk(const std::string& s)
{
    if (s.size() > (size_t)kMaxText) return false;
    for (size_t i = 0; i < s.size(); ++i) { const unsigned char c = (unsigned char)s[i]; if (c < 0x20 || c == 0x7F) return false; }
    return true;
}
inline bool ZkFloatOk(float v) { return v > -1.0e7f && v < 1.0e7f; }   /* NaN fails too */
inline float ZkFloatOr0(float v, int* bad) { if (ZkFloatOk(v)) return v; ++*bad; return 0.0f; }

/* the first record an instance names that has the wanted type; -1 = none */
inline int InstRecOfType(const Container& c, const std::map<std::string, int>& bySid, const Inst& in, int type)
{
    for (size_t i = 0; i < in.strs.size(); ++i)
    {
        std::map<std::string, int>::const_iterator f = bySid.find(in.strs[i]);
        if (f != bySid.end() && c.recs[(size_t)f->second].type == type) return f->second;
    }
    return -1;
}
inline KeptItem ItemOf(const Rec& r)
{
    KeptItem it;
    it.tmpl = RecStr(r, "base data sid");
    it.material = RecStr(r, "material sid");
    it.company = RecStr(r, "company sid");
    it.color = RecStr(r, "color sid");
    it.section = RecStr(r, "section");
    it.quality = RecFloat(r, "quality", 0.0f);
    it.charges = RecFloat(r, "charges", 0.0f);
    it.quantity = RecInt(r, "quantity", 1);
    it.level = RecInt(r, "level", 0);
    it.function = RecInt(r, "item function", 0);
    it.unique = RecBool(r, "unique");
    it.slotX = RecInt(r, "inventory x", 0);
    it.slotY = RecInt(r, "inventory y", 0);
    it.uniform = RecStr(r, "uniform");   /* save 0x7610C0: the faction's record id, "" for none */
    it.crafter = RecStr(r, "crafter");   /* written only when the item has one */
    if (r.ints.find("ownedbyTYPE") != r.ints.end())   /* the item save 0x75FEA0 writes the hand through 0x6BC730 as five ints */
    {
        it.hasOwner = 1;
        it.owner[0] = RecInt(r, "ownedbyTYPE", 11); it.owner[1] = RecInt(r, "ownedbyC", 0); it.owner[2] = RecInt(r, "ownedbyCS", 0);
        it.owner[3] = RecInt(r, "ownedbyI", 0); it.owner[4] = RecInt(r, "ownedbyS", 0);
    }
    return it;
}
/* an item's uniform and crafter (and those of its own items, at every depth) that the KEPT row cannot carry: left out, counted */
inline void SanitizeItem(KeptItem* it, KeepStats* st)
{
    if (!ZkTextOk(it->uniform)) { it->uniform.clear(); ++st->leftOut; }
    if (!ZkTextOk(it->crafter)) { it->crafter.clear(); ++st->leftOut; }
    for (size_t k = 0; k < it->inner.size(); ++k) SanitizeItem(&it->inner[k], st);
}
/* does an item record's own inventory record (its instance of type 0x29) name an item record - does it hold items of its own? */
inline bool HoldsItems(const Container& c, const std::map<std::string, int>& bySid, const Rec& itr)
{
    for (size_t k = 0; k < itr.insts.size(); ++k)
    {
        std::map<std::string, int>::const_iterator iv = bySid.find(itr.insts[k].target);
        if (iv == bySid.end() || c.recs[(size_t)iv->second].type != kTypeInventory) continue;
        const Rec& inv = c.recs[(size_t)iv->second];
        for (size_t j = 0; j < inv.insts.size(); ++j)
        {
            std::map<std::string, int>::const_iterator f = bySid.find(inv.insts[j].target);
            if (f != bySid.end() && c.recs[(size_t)f->second].type == kTypeItem) return true;
        }
    }
    return false;
}
/* THE ITEMS BELOW THE DEPTH BOUND: an item at kMaxPackDepth carries no list of its own in a KEPT row, so the items its record holds -
   and theirs, at every depth below - are kept in the list it is in, after it, each with no list of its own (laid back, they go into
   that pack, else the box, else the ground, as any pack item). A record already walked is not walked again (a record that names
   itself ends there). Counts st->flattened. */
inline void FlattenItemsOf(const Container& c, const std::map<std::string, int>& bySid, const Rec& itr, std::set<size_t>* seen, std::vector<KeptItem>* out, KeepStats* st)
{
    for (size_t k = 0; k < itr.insts.size(); ++k)
    {
        std::map<std::string, int>::const_iterator iv = bySid.find(itr.insts[k].target);
        if (iv == bySid.end() || c.recs[(size_t)iv->second].type != kTypeInventory) continue;
        const Rec& inv = c.recs[(size_t)iv->second];
        for (size_t j = 0; j < inv.insts.size(); ++j)
        {
            std::map<std::string, int>::const_iterator f = bySid.find(inv.insts[j].target);
            if (f == bySid.end() || c.recs[(size_t)f->second].type != kTypeItem || !seen->insert((size_t)f->second).second) continue;
            const Rec& in = c.recs[(size_t)f->second];
            out->push_back(ItemOf(in));
            ++st->flattened;
            FlattenItemsOf(c, bySid, in, seen, out, st);
        }
    }
}
/* A PACK'S OWN ITEMS, AT ANY DEPTH: the item records its own inventory record (the item record's instance of type 0x29) names, each
   with its own items in turn. depth = the depth of the items read here (a box item's own items are depth 1); an item at depth
   kMaxPackDepth keeps no list of its own: the items its record holds are kept after it in this list (FlattenItemsOf; counted in
   st->nestedBags and st->flattened - the bound also ends a record that names itself).
   Counts st->innerItems (every item read here) and st->bagItems (each of them that keeps items of its own). */
inline void InnerItemsOf(const Container& c, const std::map<std::string, int>& bySid, const Rec& itr, int depth, std::vector<KeptItem>* out, KeepStats* st)
{
    for (size_t k = 0; k < itr.insts.size(); ++k)
    {
        std::map<std::string, int>::const_iterator iv = bySid.find(itr.insts[k].target);
        if (iv == bySid.end() || c.recs[(size_t)iv->second].type != kTypeInventory) continue;
        const Rec& inv = c.recs[(size_t)iv->second];
        for (size_t j = 0; j < inv.insts.size(); ++j)
        {
            std::map<std::string, int>::const_iterator f = bySid.find(inv.insts[j].target);
            if (f == bySid.end() || c.recs[(size_t)f->second].type != kTypeItem) continue;
            const Rec& in = c.recs[(size_t)f->second];
            KeptItem it = ItemOf(in);
            std::vector<KeptItem> below;
            if (depth < kMaxPackDepth) InnerItemsOf(c, bySid, in, depth + 1, &it.inner, st);
            else if (HoldsItems(c, bySid, in))
            {
                ++st->nestedBags;
                std::set<size_t> seen;
                seen.insert((size_t)f->second);
                FlattenItemsOf(c, bySid, in, &seen, &below, st);
            }
            if (!it.inner.empty()) ++st->bagItems;
            ++st->innerItems;
            out->push_back(it);
            for (size_t b = 0; b < below.size(); ++b) out->push_back(below[b]);   /* below the bound: beside it */
        }
    }
}
/* a building's doors: for each number 0 .. kMaxDoors-1 whose 'state<i>' the state record holds, that door's fields */
inline void DoorsOf(const Rec& r, int writerSlot, std::vector<KeptDoor>* out)
{
    for (int i = 0; i < kMaxDoors; ++i)
    {
        const std::string n = ZkNum(i);
        if (r.ints.find("state" + n) == r.ints.end()) continue;
        KeptDoor d;
        d.index = i;
        d.state = RecInt(r, ("state" + n).c_str(), 0);
        d.locked = RecBool(r, ("locked" + n).c_str());
        d.wantsLock = RecBool(r, ("wants lock" + n).c_str());
        d.lockLevel = RecInt(r, ("lock lvl" + n).c_str(), 0);
        d.gateCode = RecInt(r, ("gatecode" + n).c_str(), -1);
        const std::string hk = "hardness" + n;
        d.hardness = RecHasFloat(r, hk.c_str()) ? RecFloat(r, hk.c_str(), 0.0f) : -1.0f;
        if (!ZkFloatOk(d.hardness)) d.hardness = -1.0f;
        d.broken = RecBool(r, ("broken" + n).c_str());
        const int slot = coopslot::OwnerSlotOfId(RecStr(r, ("owner faction ID" + n).c_str()), kPlayerFactionSid, writerSlot);
        d.ownerSlot = slot >= 0 ? slot : -1;
        out->push_back(d);
    }
}
/* ONE BAD FIELD NEVER DROPS A ROW (keptwire.h RowOk): before a piece is kept, each field the KEPT row cannot carry is clamped or
   dropped alone and counted in st->fieldsFixed - a door's state outside 0 .. 3 becomes 0 (closed), a door flag outside 0 / 1 becomes
   1, a door owner slot outside the slots -1 (nobody's), a hardness that is not a finite value -1 (none kept); an item's material,
   company, color, section, uniform or crafter the row cannot carry is left out, a quality or charges that is not a finite value is 0,
   an item whose template the row cannot carry is left out (it could not be made); the progress and delivered materials that are not
   finite values are 0. The items past kMaxPackItems in one pack are moved to the box (st->packSplit); box items past kMaxBoxItems
   are not carried (st->itemsOverCap). */
inline void FitText(std::string* s, KeepStats* st) { if (!ZkTextOk(*s)) { s->clear(); ++st->fieldsFixed; } }
inline bool FitTemplateOk(const KeptItem& it) { return !it.tmpl.empty() && ZkTextOk(it.tmpl); }
inline void FitDropBadTemplates(std::vector<KeptItem>* items, KeepStats* st)
{
    std::vector<KeptItem> keep;
    for (size_t k = 0; k < items->size(); ++k) { if (FitTemplateOk((*items)[k])) keep.push_back((*items)[k]); else ++st->fieldsFixed; }
    if (keep.size() != items->size()) items->swap(keep);
}
inline void FitItemTree(KeptItem* it, std::vector<KeptItem>* moved, KeepStats* st)
{
    FitText(&it->material, st); FitText(&it->company, st); FitText(&it->color, st); FitText(&it->section, st);
    FitText(&it->uniform, st); FitText(&it->crafter, st);
    if (!ZkFloatOk(it->quality)) { it->quality = 0.0f; ++st->fieldsFixed; }
    if (!ZkFloatOk(it->charges)) { it->charges = 0.0f; ++st->fieldsFixed; }
    if (it->hasOwner != 0 && it->hasOwner != 1) { it->hasOwner = 1; ++st->fieldsFixed; }
    FitDropBadTemplates(&it->inner, st);
    if (it->inner.size() > (size_t)kMaxPackItems)
    {
        for (size_t k = (size_t)kMaxPackItems; k < it->inner.size(); ++k) moved->push_back(it->inner[k]);
        st->packSplit += (int)(it->inner.size() - (size_t)kMaxPackItems);
        it->inner.resize((size_t)kMaxPackItems);
    }
    for (size_t k = 0; k < it->inner.size(); ++k) FitItemTree(&it->inner[k], moved, st);
}
inline void FitPiece(KeptPiece* p, KeepStats* st)
{
    for (size_t j = 0; j < p->doors.size(); ++j)
    {
        KeptDoor& d = p->doors[j];
        if (d.state < 0 || d.state > 3) { d.state = 0; ++st->fieldsFixed; }
        if (d.locked != 0 && d.locked != 1) { d.locked = 1; ++st->fieldsFixed; }
        if (d.wantsLock != 0 && d.wantsLock != 1) { d.wantsLock = 1; ++st->fieldsFixed; }
        if (d.broken != 0 && d.broken != 1) { d.broken = 1; ++st->fieldsFixed; }
        if (d.ownerSlot < -1 || d.ownerSlot > coopslot::kSlotMax) { d.ownerSlot = -1; ++st->fieldsFixed; }
        if (!ZkFloatOk(d.hardness)) { d.hardness = -1.0f; ++st->fieldsFixed; }
    }
    if (!ZkFloatOk(p->progress)) { p->progress = 0.0f; ++st->fieldsFixed; }
    for (size_t j = 0; j < p->mats.size(); ++j) if (!ZkFloatOk(p->mats[j].count)) { p->mats[j].count = 0.0f; ++st->fieldsFixed; }
    FitDropBadTemplates(&p->items, st);
    size_t from = 0;
    while (from < p->items.size())   /* the items moved out of a pack are fitted in turn at the box level */
    {
        std::vector<KeptItem> moved;
        const size_t to = p->items.size();
        for (size_t i = from; i < to; ++i) FitItemTree(&p->items[i], &moved, st);
        from = to;
        for (size_t k = 0; k < moved.size(); ++k) p->items.push_back(moved[k]);
    }
    if (p->items.size() > (size_t)kMaxBoxItems)
    {
        st->itemsOverCap += (int)(p->items.size() - (size_t)kMaxBoxItems);
        p->items.resize((size_t)kMaxBoxItems);
    }
}
/* THE PIECE'S OWN STATE from its state record - the keys the saves write: 'name' (0x551890), 'interior layout name' / 'exterior layout
   name' / 'pause' (0x553D60), 'gets battery' / 'power on' / 'batterycharge' and the piece's own lock 'lock lvl' / 'hardness' / 'locked'
   (0x2A1470, the lock through 0x29FF90), 'production amount' / 'consumption amount-N' (0x2A1F60), 'plant growth' / 'plant death' /
   'plant start' / 'plants harvested' (0xE3B40). A part is marked in p->has only when the record holds it. A text or value the KEPT
   row cannot carry is left out and counted (st->leftOut; an input stock past kMaxInputs in st->inputsLeftOut). The farm's 'save
   time' (game hours) is never kept. */
inline void StateOf(const Rec& r, KeptPiece* p, KeepStats* st)
{
    int bad = 0;
    if (r.strs.find("name") != r.strs.end())
    {
        p->name = RecStr(r, "name");
        if (ZkTextOk(p->name)) p->has |= kHasName; else { p->name.clear(); ++st->leftOut; }
    }
    if (r.strs.find("interior layout name") != r.strs.end() || r.strs.find("exterior layout name") != r.strs.end())
    {
        p->interiorName = RecStr(r, "interior layout name");
        p->exteriorName = RecStr(r, "exterior layout name");
        if (ZkTextOk(p->interiorName) && ZkTextOk(p->exteriorName)) p->has |= kHasLayout;
        else { p->interiorName.clear(); p->exteriorName.clear(); ++st->leftOut; }
    }
    if (r.bools.find("pause") != r.bools.end()) { p->pause = RecBool(r, "pause"); p->has |= kHasPause; }
    if (r.bools.find("gets battery") != r.bools.end())
    {
        p->getsBattery = RecBool(r, "gets battery");
        p->powerOn = RecBool(r, "power on");
        p->battery = ZkFloatOr0(RecFloat(r, "batterycharge", 0.0f), &bad);
        p->has |= kHasPower;
    }
    if (RecHasFloat(r, "production amount")) { p->production = ZkFloatOr0(RecFloat(r, "production amount", 0.0f), &bad); p->has |= kHasProduction; }
    if (RecHasFloat(r, "plant growth"))
    {
        p->growth = ZkFloatOr0(RecFloat(r, "plant growth", 0.0f), &bad);
        p->death = ZkFloatOr0(RecFloat(r, "plant death", 0.0f), &bad);
        p->start = ZkFloatOr0(RecFloat(r, "plant start", 0.0f), &bad);
        p->harvested = RecInt(r, "plants harvested", 0);
        p->has |= kHasFarm;
    }
    if (r.ints.find("lock lvl") != r.ints.end())
    {
        p->lockLevel = RecInt(r, "lock lvl", 0);
        p->lockHardness = ZkFloatOr0(RecFloat(r, "hardness", 0.0f), &bad);
        p->lockLocked = RecBool(r, "locked");
        p->has |= kHasOwnLock;
    }
    static const char kPre[] = "consumption amount-";
    const size_t pl = sizeof(kPre) - 1;
    for (std::map<std::string, float>::const_iterator i = r.floats.begin(); i != r.floats.end(); ++i)
    {
        const std::string& k = i->first;
        if (k.size() <= pl || k.compare(0, pl, kPre) != 0) continue;
        const std::string d = k.substr(pl);
        if (d.size() > 6 || d.find_first_not_of("0123456789") != std::string::npos) continue;
        int idx = 0;
        for (size_t j = 0; j < d.size(); ++j) idx = idx * 10 + (d[j] - '0');
        if (idx >= kMaxInputs) { ++st->inputsLeftOut; continue; }
        KeptStock s; s.index = idx; s.amount = ZkFloatOr0(i->second, &bad);
        size_t at = 0;
        while (at < p->inputs.size() && p->inputs[at].index < idx) ++at;
        p->inputs.insert(p->inputs.begin() + at, s);
    }
    st->leftOut += bad;
}
/* the delivered materials: every float '<digits>construction mats' */
inline void MatsOf(const Rec& r, std::vector<KeptMat>* out)
{
    static const char kTail[] = "construction mats";
    const size_t tl = sizeof(kTail) - 1;
    for (std::map<std::string, float>::const_iterator i = r.floats.begin(); i != r.floats.end(); ++i)
    {
        const std::string& k = i->first;
        if (k.size() <= tl || k.compare(k.size() - tl, tl, kTail) != 0) continue;
        const std::string d = k.substr(0, k.size() - tl);
        if (d.empty() || d.size() > 6 || d.find_first_not_of("0123456789") != std::string::npos) continue;
        KeptMat m; m.index = 0;
        for (size_t j = 0; j < d.size(); ++j) m.index = m.index * 10 + (d[j] - '0');
        m.count = i->second;
        out->push_back(m);
    }
}
inline std::string HandleKey(int s, int i, int c) { return ZkNum(s) + "/" + ZkNum(i) + "/" + ZkNum(c); }
/* a town's five handle words (townTYPE, townC, townCS, townI, townS) as one text: the form a set of towns holds */
inline std::string TownHandText(const int* h) { return ZkNum(h[0]) + "/" + ZkNum(h[1]) + "/" + ZkNum(h[2]) + "/" + ZkNum(h[3]) + "/" + ZkNum(h[4]); }
/* the town a state record names (Building +0x90 when it was saved): 1 with its handle words in h; 0 = none (no townTYPE, or townC,
   townCS, townI and townS all zero) */
inline int TownHandOf(const Rec& r, int* h)
{
    for (int i = 0; i < 5; ++i) h[i] = 0;
    if (r.ints.find("townTYPE") == r.ints.end()) return 0;
    h[0] = RecInt(r, "townTYPE", 0); h[1] = RecInt(r, "townC", 0); h[2] = RecInt(r, "townCS", 0); h[3] = RecInt(r, "townI", 0); h[4] = RecInt(r, "townS", 0);
    return (h[1] | h[2] | h[3] | h[4]) != 0 ? 1 : 0;
}

/* THE PIECES A PLAYER OWNS in one parsed area record, appended to *out. writerSlot = the slot the area record's writer holds (its
   .meta owner field; -1 = not known): the player faction's own id in the record names that player. A piece is kept when its state
   record names a player's slot (slotwire.h OwnerSlotOfId); a world resource node, nobody's piece, a piece whose owner cannot be
   named and a state record an earlier entry already took (one state record can be listed under several entries) are not.
   A piece's position is the list's x and z with the state record's 'world Y pos' as its height (the list's height is not the
   piece's); its key is the one the game builds (poskey.h). Furniture names its host building through isInsideBuilding{S,I,C},
   which match the host state record's handle{S,I,C}; furniture whose host is not found is counted and not kept. A box's items are
   its inventory record's instances. *st counts what was kept
   and what was not, and why. An area with no building list keeps nothing and is not an error; an entry that cannot be used is
   counted, never a refusal of the area, so the answer is true (false is kept for a container this reading cannot use at all). */
/* A building's own door is not an entry of the building list: the area record writes it inside the building's state record (its
   fields ending in 0 - doa0, locked0, handle0S ...), so it is never kept as a piece of its own: its state is kept with the building
   (DoorsOf). */
inline bool ExtractKeptPieces(const Container& c, int writerSlot, std::vector<KeptPiece>* out, KeepStats* st, std::string* why)
{
    std::map<std::string, int> bySid;
    for (size_t i = 0; i < c.recs.size(); ++i) if (bySid.find(c.recs[i].sid) == bySid.end()) bySid[c.recs[i].sid] = (int)i;
    std::map<std::string, int>::const_iterator bl = bySid.find(kBuildingList);
    if (bl == bySid.end()) return true;
    const Rec& list = c.recs[(size_t)bl->second];
    /* every state record's handle, and the key of the first entry that names each state record */
    std::map<std::string, int> stateByHandle;
    for (size_t i = 0; i < c.recs.size(); ++i)
    {
        const Rec& r = c.recs[i];
        if (r.type != kTypeState || r.ints.find("handleS") == r.ints.end()) continue;
        const std::string h = HandleKey(RecInt(r, "handleS", 0), RecInt(r, "handleI", 0), RecInt(r, "handleC", 0));
        if (stateByHandle.find(h) == stateByHandle.end()) stateByHandle[h] = (int)i;
    }
    std::map<int, std::string> keyOfState;
    for (size_t i = 0; i < list.insts.size(); ++i)
    {
        const Inst& in = list.insts[i];
        const int s = InstRecOfType(c, bySid, in, kTypeState);
        if (s < 0 || keyOfState.find(s) != keyOfState.end()) continue;
        const Rec& r = c.recs[(size_t)s];
        const float y = RecHasFloat(r, "world Y pos") ? RecFloat(r, "world Y pos", 0.0f) : in.pos[1];
        char key[poskey::kKeyCap];
        const int kr = poskey::KeyOfPos(in.target.c_str(), in.pos[0], y, in.pos[2], key, poskey::kKeyCap);
        keyOfState[s] = (kr == poskey::kKeyOk || kr == poskey::kKeyHashed) ? std::string(key) : std::string();
    }
    std::set<int> takenState, takenInv;
    for (size_t i = 0; i < list.insts.size(); ++i)
    {
        const Inst& in = list.insts[i];
        ++st->listed;
        const int s = InstRecOfType(c, bySid, in, kTypeState);
        if (s < 0) { ++st->noState; continue; }
        if (!takenState.insert(s).second) { ++st->sharedState; continue; }
        const Rec& r = c.recs[(size_t)s];
        const int slot = coopslot::OwnerSlotOfId(RecStr(r, "owner faction ID"), kPlayerFactionSid, writerSlot);
        if (IsWorldNodeTemplate(in.target)) { ++st->nodes; continue; }
        if (slot == coopslot::kOwnerNoViewer) { ++st->writerUnknown; continue; }
        if (slot == coopslot::kOwnerLegacy) { ++st->ambiguous; continue; }
        if (slot < 0)
        {
            ++st->nobody;
            int th[5];
            const std::string oid = RecStr(r, "owner faction ID");
            const bool npcOwned = !oid.empty() && oid.compare(0, ::strlen(coopslot::kStandInPrefix), coopslot::kStandInPrefix) != 0;
            if (npcOwned && TownHandOf(r, th) != 0) st->worldTowns.insert(TownHandText(th));   /* a building a non-player faction owns names a world town (an empty or player-form owner id never does) */
            continue;
        }
        KeptPiece p;
        p.key = keyOfState[s];
        if (p.key.empty()) { ++st->noKey; continue; }
        p.ownerSlot = slot;
        p.tmpl = in.target;
        p.pos[0] = in.pos[0]; p.pos[2] = in.pos[2];
        p.pos[1] = RecHasFloat(r, "world Y pos") ? RecFloat(r, "world Y pos", 0.0f) : in.pos[1];
        for (int j = 0; j < 4; ++j) p.rot[j] = in.rot[j];
        p.floor = RecInt(r, "floornum", 0);
        p.progress = RecFloat(r, "construction progress", 0.0f);
        MatsOf(r, &p.mats);
        StateOf(r, &p, st);
        if (r.ints.find("townTYPE") != r.ints.end())   /* the town the building is linked to (Building +0x90 when it was saved) */
        {
            p.townHand[0] = RecInt(r, "townTYPE", 0); p.townHand[1] = RecInt(r, "townC", 0); p.townHand[2] = RecInt(r, "townCS", 0);
            p.townHand[3] = RecInt(r, "townI", 0); p.townHand[4] = RecInt(r, "townS", 0);
            p.hasTown = (p.townHand[1] | p.townHand[2] | p.townHand[3] | p.townHand[4]) != 0 ? 1 : 0;   /* all four zero: no town */
        }
        p.complete = RecBool(r, "is complete");
        p.destroyed = RecBool(r, "destroyed");
        p.broken = RecBool(r, "broken");
        if (RecBool(r, "is inside building"))
        {
            const std::string h = HandleKey(RecInt(r, "isInsideBuildingS", 0), RecInt(r, "isInsideBuildingI", 0), RecInt(r, "isInsideBuildingC", 0));
            std::map<std::string, int>::const_iterator hs = stateByHandle.find(h);
            std::map<int, std::string>::const_iterator hk = hs == stateByHandle.end() ? keyOfState.end() : keyOfState.find(hs->second);
            if (hk != keyOfState.end() && !hk->second.empty() && hs->second != s) p.hostKey = hk->second;
            else { ++st->hostMissing; st->hostMissingList.push_back(std::make_pair(p.key, slot)); continue; }   /* not kept: with no host it would be laid as a free-standing piece; the reset log names it */
        }
        const int inv = InstRecOfType(c, bySid, in, kTypeInventory);
        if (inv >= 0 && takenInv.insert(inv).second)
        {
            const Rec& ir = c.recs[(size_t)inv];
            for (size_t j = 0; j < ir.insts.size(); ++j)
            {
                std::map<std::string, int>::const_iterator f = bySid.find(ir.insts[j].target);
                if (f == bySid.end() || c.recs[(size_t)f->second].type != kTypeItem) continue;
                const Rec& itr = c.recs[(size_t)f->second];
                KeptItem it = ItemOf(itr);
                InnerItemsOf(c, bySid, itr, 1, &it.inner, st);
                if (!it.inner.empty()) ++st->bagItems;
                SanitizeItem(&it, st);
                p.items.push_back(it);
            }
        }
        DoorsOf(r, writerSlot, &p.doors);
        FitPiece(&p, st);   /* one bad field is clamped or dropped alone, never the row */
        st->doors += (int)p.doors.size();
        st->items += (int)p.items.size();
        ++st->kept;
        out->push_back(p);
    }
    if (why != 0) why->clear();
    return true;
}

/* ------------------------------------------------------------------ the list file (kKeptFile)
   Line 1: "kept-pieces<TAB>3" (lists of format 1 and 2 are read too). Then per piece a P line, an M line per delivered material, the
   S line, an F line (a farm), a C line per input stock, an I line per item with a J line per item inside it (a pack's own items after
   their I line, each pack's own after its J line, the depth naming the level) and a D line per door, each after its P:
     P <slot> <key> <template> <x> <y> <z> <rot w> <rot x> <rot y> <rot z> <floor> <host key> <progress> <complete> <destroyed> <broken>
     M <index> <count>
     S <has> <name> <interior layout name> <exterior layout name> <pause> <gets battery> <power on> <battery> <production> <lock level>
       <lock hardness> <lock locked>
     F <plant growth> <plant death> <plant start> <plants harvested>
     C <index> <stock>
     I <template> <material> <company> <color> <section> <quality> <charges> <quantity> <level> <function> <unique> <slot x> <slot y>
       <uniform> <crafter> <owner ticket kept 0|1> <owner type> <container> <container stamp> <index> <serial>
     J <depth 1..kMaxPackDepth> (the I line's fields)
     D <index> <state> <locked> <wants lock> <lock level> <gate code> <owner slot> <hardness> <lock broken>
   Format 2 had no S, F or C line, no uniform, crafter or owner ticket, J lines without a depth (one level) and D lines of 7 fields; format 1 had no
   J or D line.
   Fields are TAB-separated and lines end in LF (a CR before it is read as nothing). A float is the 8 lower-case hex digits of its
   IEEE bits (exact and locale-free); an int is decimal. In a text field a backslash is written \\, a TAB \t, an LF \n, a CR \r. */
inline std::string Esc(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char ch = s[i];
        if (ch == '\\') o += "\\\\"; else if (ch == '\t') o += "\\t"; else if (ch == '\n') o += "\\n"; else if (ch == '\r') o += "\\r"; else o += ch;
    }
    return o;
}
inline bool Unesc(const std::string& s, std::string* o)
{
    o->clear();
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '\\') { *o += s[i]; continue; }
        if (++i >= s.size()) return false;
        const char ch = s[i];
        if (ch == '\\') *o += '\\'; else if (ch == 't') *o += '\t'; else if (ch == 'n') *o += '\n'; else if (ch == 'r') *o += '\r'; else return false;
    }
    return true;
}
inline std::string FloatHex(float f)
{
    unsigned int u = 0; memcpy(&u, &f, 4);
    char b[12]; int at = poskey::AppendHex8(b, (int)sizeof b, 0, u); (void)at;
    return std::string(b);
}
inline bool HexFloat(const std::string& s, float* f)
{
    if (s.size() != 8) return false;
    unsigned int u = 0;
    for (size_t i = 0; i < 8; ++i)
    {
        const char ch = s[i];
        const int d = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
        if (d < 0) return false;
        u = (u << 4) | (unsigned int)d;
    }
    memcpy(f, &u, 4);
    return true;
}
inline bool DecInt(const std::string& s, int* v)
{
    size_t i = 0; bool neg = false; long long a = 0;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    if (i >= s.size() || s.size() - i > 10) return false;
    for (; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return false; a = a * 10 + (s[i] - '0'); }
    if (neg) a = -a;
    if (a < -2147483647LL - 1 || a > 2147483647LL) return false;
    *v = (int)a;
    return true;
}
/* an I or J line's item fields; format 3 adds the uniform, the crafter and the owner ticket (kept 0 / 1, then its five words) */
inline std::string ItemFields(const KeptItem& it, int format)
{
    std::string o = Esc(it.tmpl) + "\t" + Esc(it.material) + "\t" + Esc(it.company) + "\t" + Esc(it.color) + "\t" + Esc(it.section)
           + "\t" + FloatHex(it.quality) + "\t" + FloatHex(it.charges) + "\t" + ZkNum(it.quantity) + "\t" + ZkNum(it.level) + "\t"
           + ZkNum(it.function) + "\t" + ZkNum(it.unique) + "\t" + ZkNum(it.slotX) + "\t" + ZkNum(it.slotY);
    if (format >= 3)
    {
        o += "\t" + Esc(it.uniform) + "\t" + Esc(it.crafter) + "\t" + ZkNum(it.hasOwner);
        for (int i = 0; i < 5; ++i) o += "\t" + ZkNum(it.owner[i]);
    }
    return o;
}
/* a pack's own items as J lines, each pack's own after its line (format 2: one level, no depth field; format 1: none) */
inline void EncodeInner(const KeptItem& it, int depth, int format, std::string* o)
{
    if (format < 2 || depth > kMaxPackDepth || (format == 2 && depth > 1)) return;
    for (size_t k = 0; k < it.inner.size(); ++k)
    {
        if (format >= 3) *o += "J\t" + ZkNum(depth) + "\t" + ItemFields(it.inner[k], format) + "\n";
        else *o += "J\t" + ItemFields(it.inner[k], format) + "\n";
        EncodeInner(it.inner[k], depth + 1, format, o);
    }
}
/* the list in format 1, 2 or 3 (what an older format has no line for is not written) */
inline std::string EncodeKeptAs(const std::vector<KeptPiece>& ps, int format)
{
    std::string o = std::string(kKeptHeader) + "\t" + ZkNum(format) + "\n";
    for (size_t i = 0; i < ps.size(); ++i)
    {
        const KeptPiece& p = ps[i];
        o += "P\t" + ZkNum(p.ownerSlot) + "\t" + Esc(p.key) + "\t" + Esc(p.tmpl);
        for (int j = 0; j < 3; ++j) o += "\t" + FloatHex(p.pos[j]);
        for (int j = 0; j < 4; ++j) o += "\t" + FloatHex(p.rot[j]);
        o += "\t" + ZkNum(p.floor) + "\t" + Esc(p.hostKey) + "\t" + FloatHex(p.progress) + "\t" + ZkNum(p.complete) + "\t" + ZkNum(p.destroyed)
           + "\t" + ZkNum(p.broken) + "\n";
        for (size_t j = 0; j < p.mats.size(); ++j) o += "M\t" + ZkNum(p.mats[j].index) + "\t" + FloatHex(p.mats[j].count) + "\n";
        if (format >= 3)
        {
            o += "S\t" + ZkNum(p.has) + "\t" + Esc(p.name) + "\t" + Esc(p.interiorName) + "\t" + Esc(p.exteriorName) + "\t" + ZkNum(p.pause)
               + "\t" + ZkNum(p.getsBattery) + "\t" + ZkNum(p.powerOn) + "\t" + FloatHex(p.battery) + "\t" + FloatHex(p.production)
               + "\t" + ZkNum(p.lockLevel) + "\t" + FloatHex(p.lockHardness) + "\t" + ZkNum(p.lockLocked) + "\n";
            if ((p.has & kHasFarm) != 0)
                o += "F\t" + FloatHex(p.growth) + "\t" + FloatHex(p.death) + "\t" + FloatHex(p.start) + "\t" + ZkNum(p.harvested) + "\n";
            for (size_t j = 0; j < p.inputs.size(); ++j) o += "C\t" + ZkNum(p.inputs[j].index) + "\t" + FloatHex(p.inputs[j].amount) + "\n";
        }
        for (size_t j = 0; j < p.items.size(); ++j)
        {
            o += "I\t" + ItemFields(p.items[j], format) + "\n";
            EncodeInner(p.items[j], 1, format, &o);
        }
        if (format < 2) continue;
        for (size_t j = 0; j < p.doors.size(); ++j)
        {
            const KeptDoor& d = p.doors[j];
            o += "D\t" + ZkNum(d.index) + "\t" + ZkNum(d.state) + "\t" + ZkNum(d.locked) + "\t" + ZkNum(d.wantsLock) + "\t" + ZkNum(d.lockLevel)
               + "\t" + ZkNum(d.gateCode) + "\t" + ZkNum(d.ownerSlot);
            if (format >= 3) o += "\t" + FloatHex(d.hardness) + "\t" + ZkNum(d.broken);
            o += "\n";
        }
    }
    return o;
}
inline std::string EncodeKept(const std::vector<KeptPiece>& ps) { return EncodeKeptAs(ps, kKeptFormat); }
inline void SplitTabs(const std::string& line, std::vector<std::string>* f)
{
    f->clear();
    size_t at = 0;
    for (;;)
    {
        const size_t t = line.find('\t', at);
        if (t == std::string::npos) { f->push_back(line.substr(at)); return; }
        f->push_back(line.substr(at, t - at));
        at = t + 1;
    }
}
/* an item's fields from f[at] on: the 13 every format has, then (format 3) the uniform, the crafter, the owner ticket's kept flag
   (0 / 1) and its five words - exactly those */
inline bool ItemOfFieldsAt(const std::vector<std::string>& f, size_t at, int format, KeptItem* it)
{
    if (f.size() != at + (format >= 3 ? 21 : 13)) return false;
    bool ok = Unesc(f[at], &it->tmpl) && Unesc(f[at + 1], &it->material) && Unesc(f[at + 2], &it->company) && Unesc(f[at + 3], &it->color)
              && Unesc(f[at + 4], &it->section) && HexFloat(f[at + 5], &it->quality) && HexFloat(f[at + 6], &it->charges)
              && DecInt(f[at + 7], &it->quantity) && DecInt(f[at + 8], &it->level) && DecInt(f[at + 9], &it->function) && DecInt(f[at + 10], &it->unique)
              && DecInt(f[at + 11], &it->slotX) && DecInt(f[at + 12], &it->slotY);
    if (ok && format >= 3)
        ok = Unesc(f[at + 13], &it->uniform) && Unesc(f[at + 14], &it->crafter) && DecInt(f[at + 15], &it->hasOwner) && (it->hasOwner == 0 || it->hasOwner == 1);
    for (int i = 0; ok && format >= 3 && i < 5; ++i) ok = DecInt(f[at + 16 + (size_t)i], &it->owner[i]);
    return ok;
}
/* an I or J line of format 1 or 2: the fields after its kind */
inline bool ItemOfFields(const std::vector<std::string>& f, KeptItem* it) { return ItemOfFieldsAt(f, 1, 2, it); }
/* The list back (format 3, 2 or 1). False = not a list this build reads (*why names the line); *out then holds nothing. */
inline bool DecodeKept(const std::string& text, std::vector<KeptPiece>* out, std::string* why)
{
    out->clear();
    std::vector<KeptPiece> ps;
    std::vector<std::string> f;
    std::vector<size_t> chain;   /* the last I or J line's item: its place among the piece's items, then among each pack's own items
                                    (empty = the line before was not an I or J line) */
    size_t at = 0;
    int lineNo = 0;
    int format = 0;
    int sSeen = 0, fSeen = 0;    /* the last piece's S and F lines */
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const std::string where = "line " + ZkNum(lineNo) + ": ";
        SplitTabs(line, &f);
        if (lineNo == 1)
        {
            int v = 0;
            if (f.size() != 2 || f[0] != kKeptHeader || !DecInt(f[1], &v) || (v != kKeptFormat && v != 1 && v != 2))
            { *why = where + "not a kept-pieces list of format 1, 2 or " + ZkNum(kKeptFormat); return false; }
            format = v;
            continue;
        }
        if (line.empty()) continue;
        if (f[0] != "I" && f[0] != "J") chain.clear();
        if (f[0] == "P")
        {
            KeptPiece p;
            bool ok = f.size() == 17 && DecInt(f[1], &p.ownerSlot) && Unesc(f[2], &p.key) && Unesc(f[3], &p.tmpl);
            for (int j = 0; ok && j < 3; ++j) ok = HexFloat(f[(size_t)(4 + j)], &p.pos[j]);
            for (int j = 0; ok && j < 4; ++j) ok = HexFloat(f[(size_t)(7 + j)], &p.rot[j]);
            ok = ok && DecInt(f[11], &p.floor) && Unesc(f[12], &p.hostKey) && HexFloat(f[13], &p.progress) && DecInt(f[14], &p.complete)
                 && DecInt(f[15], &p.destroyed) && DecInt(f[16], &p.broken);
            if (!ok || p.key.empty()) { *why = where + "a piece line this build cannot read"; return false; }
            ps.push_back(p);
            sSeen = 0; fSeen = 0;
        }
        else if (f[0] == "S")
        {
            bool ok = format >= 3 && !ps.empty() && sSeen == 0 && f.size() == 13;
            if (ok)
            {
                KeptPiece& p = ps.back();
                ok = DecInt(f[1], &p.has) && (p.has & ~kHasKnown) == 0 && Unesc(f[2], &p.name) && Unesc(f[3], &p.interiorName) && Unesc(f[4], &p.exteriorName)
                     && DecInt(f[5], &p.pause) && DecInt(f[6], &p.getsBattery) && DecInt(f[7], &p.powerOn) && HexFloat(f[8], &p.battery)
                     && HexFloat(f[9], &p.production) && DecInt(f[10], &p.lockLevel) && HexFloat(f[11], &p.lockHardness) && DecInt(f[12], &p.lockLocked);
            }
            if (!ok) { *why = where + "a state line this build cannot read (or a second one for the piece)"; return false; }
            sSeen = 1;
        }
        else if (f[0] == "F")
        {
            bool ok = format >= 3 && !ps.empty() && sSeen != 0 && fSeen == 0 && (ps.back().has & kHasFarm) != 0 && f.size() == 5;
            if (ok)
            {
                KeptPiece& p = ps.back();
                ok = HexFloat(f[1], &p.growth) && HexFloat(f[2], &p.death) && HexFloat(f[3], &p.start) && DecInt(f[4], &p.harvested);
            }
            if (!ok) { *why = where + "a farm line this build cannot read (or one the state line does not name)"; return false; }
            fSeen = 1;
        }
        else if (f[0] == "C")
        {
            KeptStock s; s.index = -1; s.amount = 0.0f;
            const bool ok = format >= 3 && !ps.empty() && sSeen != 0 && f.size() == 3 && DecInt(f[1], &s.index) && s.index >= 0 && s.index < kMaxInputs
                            && HexFloat(f[2], &s.amount) && ps.back().inputs.size() < (size_t)kMaxInputs;
            if (!ok) { *why = where + "an input stock line this build cannot read"; return false; }
            ps.back().inputs.push_back(s);
        }
        else if (f[0] == "M")
        {
            KeptMat m; m.index = 0; m.count = 0;
            if (ps.empty() || f.size() != 3 || !DecInt(f[1], &m.index) || !HexFloat(f[2], &m.count)) { *why = where + "a material line this build cannot read"; return false; }
            ps.back().mats.push_back(m);
        }
        else if (f[0] == "I")
        {
            KeptItem it;
            const bool ok = !ps.empty() && ItemOfFieldsAt(f, 1, format >= 3 ? 3 : 2, &it);
            if (!ok) { *why = where + "an item line this build cannot read"; return false; }
            ps.back().items.push_back(it);
            chain.assign(1, ps.back().items.size() - 1);
        }
        else if (f[0] == "J")
        {
            KeptItem it;
            int depth = 1;
            size_t fat = 1;
            bool ok = format >= 2 && !chain.empty() && !ps.empty();
            if (ok && format >= 3) { ok = f.size() > 1 && DecInt(f[1], &depth); fat = 2; }
            ok = ok && depth >= 1 && depth <= kMaxPackDepth && (size_t)depth <= chain.size() && ItemOfFieldsAt(f, fat, format >= 3 ? 3 : 2, &it);
            if (!ok) { *why = where + "a pack item line this build cannot read (or not under an item line of the depth above it)"; return false; }
            KeptItem* q = &ps.back().items[chain[0]];
            for (int k = 1; k < depth; ++k) q = &q->inner[chain[(size_t)k]];
            q->inner.push_back(it);
            chain.resize((size_t)depth);
            chain.push_back(q->inner.size() - 1);
        }
        else if (f[0] == "D")
        {
            KeptDoor d;
            bool ok = format >= 2 && !ps.empty() && f.size() == (format >= 3 ? 10u : 8u) && DecInt(f[1], &d.index) && DecInt(f[2], &d.state) && DecInt(f[3], &d.locked)
                      && DecInt(f[4], &d.wantsLock) && DecInt(f[5], &d.lockLevel) && DecInt(f[6], &d.gateCode) && DecInt(f[7], &d.ownerSlot);
            if (ok && format >= 3) ok = HexFloat(f[8], &d.hardness) && DecInt(f[9], &d.broken);
            if (!ok) { *why = where + "a door line this build cannot read"; return false; }
            ps.back().doors.push_back(d);
        }
        else { *why = where + "a line of a kind this build does not know"; return false; }
    }
    if (lineNo == 0) { *why = "the list is empty (no header line)"; return false; }
    out->swap(ps);
    return true;
}

/* ------------------------------------------------------------------ the laying state (kKeptStateFile)
   The world server's record of how far each kept row has been laid back (keptwire.h), in the reset world beside kKeptFile.
   Line 1: "kept-state<TAB>3". Then one line per row of kKeptFile, in its order:
     v3 <row> <piece done> <items done> <owner took> <piece laid by a non-holder> <items laid by a non-holder> <piece first how>
        <piece laid by> <items laid by>
   TAB-separated, LF line ends (a CR before it is read as nothing); the done, took and laid values are 0 or 1; the first how is 0
   (none) or keptwire.h kHowMade 1 / kHowPresent 2 / kHowHandedBack 3; laid by is the slot + 1 of the game that laid a part kept laid
   (0 = none or not known, at most kKeptLaidByMax) - that game may claim its lay again (keptwire.h ClaimDecide). Files of format 1
   ("kept-state<TAB>1", v1 lines without the first how) and 2 (v2 lines without the laid by) are read too. Who holds a claim is never
   written: every claim ends with the process. */
const char* const kKeptStateFile = "kept-state.txt";
const char* const kKeptStateHeader = "kept-state";
const int kKeptStateFormat = 3;
const int kKeptLaidByMax = 65536;   /* keptwire.h kClaimMax */
struct KeptState
{
    int pieceDone, itemsDone, ownerTook, pieceLaidNonWriter, itemsLaidNonWriter;
    int pieceFirstHow;   /* the how of the piece's first lay taken in this world (keptwire.h: 1 made, 2 present, 3 handed back; 0 = none) */
    int pieceLaidBy, itemsLaidBy;   /* the slot + 1 of the game that laid a part kept laid (0 = none) */
    KeptState() : pieceDone(0), itemsDone(0), ownerTook(0), pieceLaidNonWriter(0), itemsLaidNonWriter(0), pieceFirstHow(0), pieceLaidBy(0), itemsLaidBy(0) {}
};
inline std::string EncodeKeptState(const std::vector<KeptState>& s)
{
    std::string o = std::string(kKeptStateHeader) + "\t" + ZkNum(kKeptStateFormat) + "\n";
    for (size_t i = 0; i < s.size(); ++i)
        o += "v3\t" + ZkNum((long long)i) + "\t" + ZkNum(s[i].pieceDone) + "\t" + ZkNum(s[i].itemsDone) + "\t" + ZkNum(s[i].ownerTook) + "\t"
           + ZkNum(s[i].pieceLaidNonWriter) + "\t" + ZkNum(s[i].itemsLaidNonWriter) + "\t" + ZkNum(s[i].pieceFirstHow) + "\t" + ZkNum(s[i].pieceLaidBy) + "\t"
           + ZkNum(s[i].itemsLaidBy) + "\n";
    return o;
}
/* The state back, for a list of `rows` rows: *out gets one entry per row (a row with no line is new). False = not a state file of
   this format, a line this build cannot read, a row number past the list or named twice (*why names the line); *out then holds
   `rows` new entries. */
inline bool DecodeKeptState(const std::string& text, size_t rows, std::vector<KeptState>* out, std::string* why)
{
    out->assign(rows, KeptState());
    std::vector<KeptState> st(rows);
    std::vector<char> seen(rows, 0);
    std::vector<std::string> f;
    size_t at = 0;
    int lineNo = 0, format = 0;
    while (at < text.size())
    {
        size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(at, e - at);
        at = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        const std::string where = "line " + ZkNum(lineNo) + ": ";
        SplitTabs(line, &f);
        if (lineNo == 1)
        {
            int v = 0;
            if (f.size() != 2 || f[0] != kKeptStateHeader || !DecInt(f[1], &v) || v < 1 || v > kKeptStateFormat)
            { *why = where + "not a kept-state file of format 1 to " + ZkNum(kKeptStateFormat); return false; }
            format = v;
            continue;
        }
        if (line.empty()) continue;
        int row = -1;
        KeptState k;
        /* format 1: v1 lines, no first how; format 2: v2 lines with it; format 3: v3 lines with who laid each part too */
        const size_t fields = format == 1 ? 7u : (format == 2 ? 8u : 10u);
        const char* const tag = format == 1 ? "v1" : (format == 2 ? "v2" : "v3");
        const bool ok = f.size() == fields && f[0] == tag && DecInt(f[1], &row) && DecInt(f[2], &k.pieceDone) && DecInt(f[3], &k.itemsDone)
                        && DecInt(f[4], &k.ownerTook) && DecInt(f[5], &k.pieceLaidNonWriter) && DecInt(f[6], &k.itemsLaidNonWriter)
                        && (format == 1 || DecInt(f[7], &k.pieceFirstHow)) && (format < 3 || (DecInt(f[8], &k.pieceLaidBy) && DecInt(f[9], &k.itemsLaidBy)));
        const bool bits = ok && (k.pieceDone | k.itemsDone | k.ownerTook | k.pieceLaidNonWriter | k.itemsLaidNonWriter | k.pieceFirstHow | k.pieceLaidBy | k.itemsLaidBy) >= 0
                          && k.pieceDone <= 1 && k.itemsDone <= 1 && k.ownerTook <= 1 && k.pieceLaidNonWriter <= 1 && k.itemsLaidNonWriter <= 1
                          && k.pieceFirstHow <= 3 && k.pieceLaidBy <= kKeptLaidByMax && k.itemsLaidBy <= kKeptLaidByMax;
        if (!bits) { *why = where + "a state line this build cannot read"; return false; }
        if (row < 0 || (size_t)row >= rows) { *why = where + "row " + ZkNum(row) + " is past the kept list's " + ZkNum((long long)rows) + " rows"; return false; }
        if (seen[(size_t)row] != 0) { *why = where + "row " + ZkNum(row) + " is named twice"; return false; }
        seen[(size_t)row] = 1;
        st[(size_t)row] = k;
    }
    if (lineNo == 0) { *why = "the state file is empty (no header line)"; return false; }
    out->swap(st);
    return true;
}

/* ------------------------------------------------------------------ is a kept piece in an area record
   The same piece: the same template and the same owner slot, x and z within one tenth of a unit each (the position key's tenths:
   a piece laid back drifts by less than that; the height is not compared). */
inline int WithinATenth(float a, float b)
{
    int oa = 0, ob = 0;
    const long ta = poskey::TenthsOfAUnit(a, &oa), tb = poskey::TenthsOfAUnit(b, &ob);
    if (oa == 0 || ob == 0) return 0;
    return (ta - tb >= -1 && ta - tb <= 1) ? 1 : 0;
}
inline int SamePiece(const KeptPiece& a, const KeptPiece& b)
{
    return (a.tmpl == b.tmpl && a.ownerSlot == b.ownerSlot && WithinATenth(a.pos[0], b.pos[0]) != 0 && WithinATenth(a.pos[2], b.pos[2]) != 0) ? 1 : 0;
}
/* the first of ps[0 .. count) that is the same piece as `want`; -1 = none */
inline int FindSamePiece(const std::vector<KeptPiece>& ps, size_t count, const KeptPiece& want)
{
    for (size_t i = 0; i < ps.size() && i < count; ++i) if (SamePiece(ps[i], want) != 0) return (int)i;
    return -1;
}
/* the templates of the items and of every pack's own items, at every depth */
inline void TemplatesOf(const std::vector<KeptItem>& items, std::vector<std::string>* out)
{
    for (size_t i = 0; i < items.size(); ++i) { out->push_back(items[i].tmpl); TemplatesOf(items[i].inner, out); }
}
/* THE 'HAS' RULE: does a box that holds these templates already hold an item of any kept template - a kept pack's own items counted
   too, at every depth? (1 = lay nothing) */
inline int KeptItemsHas(const std::vector<KeptItem>& kept, const std::vector<std::string>& boxTemplates)
{
    std::vector<std::string> all;
    TemplatesOf(kept, &all);
    std::set<std::string> want(all.begin(), all.end());
    for (size_t i = 0; i < boxTemplates.size(); ++i) if (want.find(boxTemplates[i]) != want.end()) return 1;
    return 0;
}
/* The pieces an area record holds, as RecordHoldsPiece / RecordHoldsItems read them: ParseContainer, then ExtractKeptPieces with
   the record's writer slot. False = the record could not be read (*why says why). */
inline bool PiecesOfRecord(const unsigned char* b, size_t n, int writerSlot, std::vector<KeptPiece>* out, std::string* why)
{
    out->clear();
    Container c;
    if (!ParseContainer(n == 0 ? (const unsigned char*)"" : b, n, &c, why)) return false;
    KeepStats st;
    return ExtractKeptPieces(c, writerSlot, out, &st, why);
}
/* does the record (its pieces, PiecesOfRecord) hold this kept piece? */
inline bool RecordHoldsPiece(const std::vector<KeptPiece>& inRecord, const KeptPiece& want)
{
    return FindSamePiece(inRecord, inRecord.size(), want) >= 0;
}
/* the quantity of each template among the items and every pack's own items, at every depth */
inline void TallyItems(const std::vector<KeptItem>& items, std::map<std::string, long long>* out)
{
    for (size_t j = 0; j < items.size(); ++j)
    {
        (*out)[items[j].tmpl] += items[j].quantity > 0 ? items[j].quantity : 1;
        TallyItems(items[j].inner, out);
    }
}
/* does the record hold this kept piece's box contents (a backpack's own items counted with them)? kHoldAll: the same piece's box
   holds, for every kept template, at least the kept quantity of it (summed over its items); kHoldHas: it holds an item of any kept
   template (a box the 'has' rule left alone). A piece that kept no items: true when the record holds the piece. */
const int kHoldAll = 0, kHoldHas = 1;
inline bool RecordHoldsItems(const std::vector<KeptPiece>& inRecord, const KeptPiece& want, int mode)
{
    for (size_t i = 0; i < inRecord.size(); ++i)
    {
        const KeptPiece& got = inRecord[i];
        if (SamePiece(got, want) == 0) continue;
        if (want.items.empty()) return true;
        if (mode == kHoldHas)
        {
            std::vector<std::string> box;
            TemplatesOf(got.items, &box);
            if (KeptItemsHas(want.items, box) != 0) return true;
            continue;
        }
        std::map<std::string, long long> need, have;
        TallyItems(want.items, &need);
        TallyItems(got.items, &have);
        bool all = true;
        for (std::map<std::string, long long>::const_iterator it = need.begin(); it != need.end() && all; ++it)
        {
            std::map<std::string, long long>::const_iterator h = have.find(it->first);
            all = h != have.end() && h->second >= it->second;
        }
        if (all) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ a second reset keeping the buildings
   The world being reset was itself a reset world: its kept list (oldList) and laying state (oldState, one entry per row - shorter
   = new) carry rows not laid back yet. *fresh is this reset's list, read from the world's area records; *freshState gets one entry
   per row of the merged list, for the reset world's kept-state.txt (the pieces read from the records start new). PART BY PART:
   - a row whose piece is done, whose box contents are laid or done and whose owner took it is dropped (its piece is in the records);
   - a row whose piece *fresh already holds (SamePiece) is dropped too; when its box contents were neither laid nor done and that
     piece in *fresh kept no items, its items move onto it;
   - any other row is carried with only its undone parts: a piece done is not laid again (its state says done), box contents laid
     or done are not carried (never laid twice), the owner part as it was while the piece is done (a piece laid again is a new
     piece, which its owner's game registers again). A row whose only undone part is the owner part is carried owner-only: nothing
     is laid, the owner's game finds its piece and registers it. MergeStats counts each outcome for the reset log, the box contents
     that may be lost among them. */
struct MergeStats
{
    int doneDropped, matchedDropped, itemsMoved, appended, pieceKeptDone, ownerOnly;
    int itemsDoneLeft;          /* carried rows whose box contents were done (in an area record) - not carried */
    int itemsLaidUnconfirmed;   /* rows (any outcome) whose box contents were laid but never confirmed in a record - not carried, they may be lost */
    int matchedDropLaid;        /* rows matched to a piece read from the records whose laid or done box contents are not carried */
    int matchedItemsKept;       /* rows matched to a piece read that holds box contents of its own: their contents not laid back are not moved */
    int ownerRetake;            /* carried rows whose owner had taken the piece and whose piece is laid again: the owner part opens again */
    MergeStats() : doneDropped(0), matchedDropped(0), itemsMoved(0), appended(0), pieceKeptDone(0), ownerOnly(0), itemsDoneLeft(0), itemsLaidUnconfirmed(0),
                   matchedDropLaid(0), matchedItemsKept(0), ownerRetake(0) {}
};
inline void MergeUndone(const std::vector<KeptPiece>& oldList, const std::vector<KeptState>& oldState, std::vector<KeptPiece>* fresh,
                        std::vector<KeptState>* freshState, MergeStats* ms)
{
    const size_t freshCount = fresh->size();
    freshState->assign(freshCount, KeptState());
    for (size_t i = 0; i < oldList.size(); ++i)
    {
        const KeptPiece& p = oldList[i];
        const KeptState s = i < oldState.size() ? oldState[i] : KeptState();
        const int itemsSettled = (s.itemsDone != 0 || s.itemsLaidNonWriter != 0 || p.items.empty()) ? 1 : 0;   /* laid or done: never laid again */
        if (!p.items.empty() && s.itemsDone == 0 && s.itemsLaidNonWriter != 0) ++ms->itemsLaidUnconfirmed;   /* not carried whatever the row's outcome */
        if (s.pieceDone != 0 && itemsSettled != 0 && s.ownerTook != 0) { ++ms->doneDropped; continue; }
        const int at = FindSamePiece(*fresh, freshCount, p);
        if (at >= 0)
        {
            KeptPiece& got = (*fresh)[(size_t)at];
            if (itemsSettled == 0 && got.items.empty()) { got.items = p.items; ++ms->itemsMoved; }
            else if (itemsSettled == 0) ++ms->matchedItemsKept;   /* the piece read holds box contents of its own: these are not moved */
            else if (!p.items.empty()) ++ms->matchedDropLaid;
            ++ms->matchedDropped;
            continue;
        }
        KeptPiece c = p;
        KeptState cs;
        cs.pieceDone = s.pieceDone != 0 ? 1 : 0;
        cs.ownerTook = (s.ownerTook != 0 && cs.pieceDone != 0) ? 1 : 0;   /* a piece laid again is a new piece: its owner's game registers it again */
        if (s.ownerTook != 0 && cs.ownerTook == 0) ++ms->ownerRetake;
        if (itemsSettled != 0) { if (!c.items.empty() && s.itemsDone != 0) ++ms->itemsDoneLeft; c.items.clear(); cs.itemsDone = 1; }
        if (cs.pieceDone != 0) ++ms->pieceKeptDone;
        if (cs.pieceDone != 0 && cs.itemsDone != 0 && cs.ownerTook == 0) ++ms->ownerOnly;
        fresh->push_back(c);
        freshState->push_back(cs);
        ++ms->appended;
    }
}

}   /* namespace zonekeep */

#endif
