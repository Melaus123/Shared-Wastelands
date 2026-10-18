/* src/common/powerwire.h - P47. A base's POWER (switches, generators, batteries) belongs to ONE game per piece: the production
 * machines' writer (items.cpp ProdWriterDecide - the owner's game while it is online and holds the area, else the area holder).
 * Every game runs its towns' grids itself (0x92C980 -> 0x92BE90: each frame ONE town of the grid-update set TownList+0x180B8 runs
 * its grid step through vt+0x2B8, the set's members taking turns - not every loaded town every frame) and recomputes the power a piece receives from the
 * switches, the battery levels and the fuel state, so the received power is NOT carried - only what the grid reads:
 *
 *   kind u8 (16 POWER_STATE, writer -> every game) | u8 len + P7n key | u8 flags (bit 0 power on +0x3B5, bit 1 gets battery +0x3B4)
 *   | f32 battery level (+0x3C4) | u8 n | n x f32 input stock (ProductionBuilding input records +0x488, stride 0x20, stock +0)
 *     = the writer's piece as its grid reads it: the switch (set through setPowerOn 0x2973B0, vt+0x318, which also joins / leaves
 *     the town's grid), the battery level the battery charge 0x299460 raises, and the fuel stock the input step 0x29A330 lowers
 *     as it takes fuel items out of the generator's inventory (the items themselves move as ordinary box moves). n = 0 for a
 *     piece with no input records.
 *   kind u8 (17 POWER_ASK, a game that does not write the piece -> its writer) | u8 len + P7n key | u8 what | f32 value
 *     what 1 = the switch (value 0 or 1): a player on another game switched the piece; once the ask is sent that game puts its
 *     own switch back and the writer switches its piece once - its POWER_STATE then carries the result to every game. With no
 *     road to the writer the ask is not sent and the switch stays as the player set it until the writer's next POWER_STATE.
 *
 * Kinds 1-15 are taken (buildwire.h 1-5, 8, 9, 12; farmwire.h 6, 7, 10, 11, 13-15); session.cpp OnBuild peeks IsPowerKind before
 * the farm peek and DecodeBuild (test t_p47_kind_tables_apart walks every kind byte through all three).
 *
 * Pure: no engine memory, no Windows; the offline suite hits the same bytes and the same decisions. C++03 (VS2010 v100).
 */
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace powerwire {

const unsigned char kPowerState = 16;
const unsigned char kPowerAsk = 17;
const unsigned char kAskSwitch = 1;          /* POWER_ASK's what: the only one there is */
const unsigned char kFlagOn = 1, kFlagGets = 2;
const unsigned int  kPowerMaxKey = 63;
const unsigned int  kPowerMaxStocks = 16;    /* a piece's input records; a bigger message is refused, never truncated */
const float         kPowerValueLimit = 1000000.0f;   /* battery and stock: [0, 1e6] (bldset's own range) */
const unsigned int  kPowerLevelGapMs = 5000;  /* the battery level and the stocks go out at most this often while they change */
const float         kPowerLevelEps = 0.0001f; /* a level or stock change smaller than this is not a change */

const int kPowerDecodeOk = 0, kPowerDecodeTooShort = 1, kPowerDecodeBadKind = 2, kPowerDecodeBadKey = 3, kPowerDecodeBadValue = 4;

struct PowerMsg
{
    unsigned char kind;
    std::string key;
    unsigned char on, gets;     /* POWER_STATE */
    float battery;
    std::vector<float> stock;
    unsigned char what;         /* POWER_ASK */
    float value;
    PowerMsg() : kind(0), on(0), gets(0), battery(0.0f), what(0), value(0.0f) {}
};

/* MSG_BUILD's kind byte: 1 = a power kind (decoded here), 0 = not */
inline int IsPowerKind(const char* p, size_t size)
{
    if (p == 0 || size < 1) return 0;
    const unsigned char k = (unsigned char)p[0];
    return (k == kPowerState || k == kPowerAsk) ? 1 : 0;
}

inline bool PowerValueOk(float v) { return v == v && v >= 0.0f && v <= kPowerValueLimit; }

inline bool PowerEncodable(const PowerMsg& m)
{
    if (m.key.empty() || m.key.size() > kPowerMaxKey) return false;
    if (m.kind == kPowerState)
    {
        if (!PowerValueOk(m.battery) || m.stock.size() > kPowerMaxStocks) return false;
        for (size_t i = 0; i < m.stock.size(); ++i) if (!PowerValueOk(m.stock[i])) return false;
        return true;
    }
    if (m.kind == kPowerAsk) return m.what == kAskSwitch && (m.value == 0.0f || m.value == 1.0f);
    return false;
}

inline void PowerPutF(std::vector<char>* b, float v) { char t[4]; std::memcpy(t, &v, 4); b->insert(b->end(), t, t + 4); }
inline float PowerGetF(const char* p) { float v; std::memcpy(&v, p, 4); return v; }

inline bool EncodePower(std::vector<char>* b, const PowerMsg& m)
{
    b->clear();
    if (!PowerEncodable(m)) return false;
    b->push_back((char)m.kind);
    b->push_back((char)(unsigned char)m.key.size());
    b->insert(b->end(), m.key.begin(), m.key.end());
    if (m.kind == kPowerAsk) { b->push_back((char)m.what); PowerPutF(b, m.value); return true; }
    b->push_back((char)(unsigned char)((m.on != 0 ? kFlagOn : 0) | (m.gets != 0 ? kFlagGets : 0)));
    PowerPutF(b, m.battery);
    b->push_back((char)(unsigned char)m.stock.size());
    for (size_t i = 0; i < m.stock.size(); ++i) PowerPutF(b, m.stock[i]);
    return true;
}

/* exactly one message: a byte short, a byte over, an unknown flag bit, a value out of range - each refused whole */
inline int DecodePower(const char* p, size_t size, PowerMsg* out)
{
    if (p == 0 || size < 2) return kPowerDecodeTooShort;
    if (IsPowerKind(p, size) == 0) return kPowerDecodeBadKind;
    PowerMsg m;
    m.kind = (unsigned char)p[0];
    const unsigned int kl = (unsigned char)p[1];
    size_t off = 2;
    if (kl == 0 || kl > kPowerMaxKey) return kPowerDecodeBadKey;
    if (size - off < kl) return kPowerDecodeTooShort;
    m.key.assign(p + off, kl); off += kl;
    if (m.kind == kPowerAsk)
    {
        if (size - off < 5) return kPowerDecodeTooShort;
        if (size - off > 5) return kPowerDecodeBadValue;
        m.what = (unsigned char)p[off]; m.value = PowerGetF(p + off + 1);
        if (m.what != kAskSwitch || !(m.value == 0.0f || m.value == 1.0f)) return kPowerDecodeBadValue;
        *out = m;
        return kPowerDecodeOk;
    }
    if (size - off < 6) return kPowerDecodeTooShort;
    const unsigned char fl = (unsigned char)p[off];
    if ((fl & ~(kFlagOn | kFlagGets)) != 0) return kPowerDecodeBadValue;
    m.on = (fl & kFlagOn) != 0 ? 1 : 0; m.gets = (fl & kFlagGets) != 0 ? 1 : 0;
    m.battery = PowerGetF(p + off + 1);
    const unsigned int n = (unsigned char)p[off + 5];
    off += 6;
    if (!PowerValueOk(m.battery) || n > kPowerMaxStocks) return kPowerDecodeBadValue;
    if (size - off < (size_t)n * 4) return kPowerDecodeTooShort;
    if (size - off > (size_t)n * 4) return kPowerDecodeBadValue;
    for (unsigned int i = 0; i < n; ++i)
    {
        const float v = PowerGetF(p + off + (size_t)i * 4);
        if (!PowerValueOk(v)) return kPowerDecodeBadValue;
        m.stock.push_back(v);
    }
    *out = m;
    return kPowerDecodeOk;
}

/* the writer's send rule for one piece: the first look, a switch or gets-battery change, or countDiffers (a change in the number
   of input records, a stock that rose - PowerStockRose - or a stock that reached 0 - PowerStockEmptied) goes now; the battery level and a stock that fell wait until
   kPowerLevelGapMs after the last send. 1 = send now */
inline int PowerSendDue(int everSent, int switchDiffers, int countDiffers, int levelDiffers, unsigned int msSinceSent)
{
    if (everSent == 0 || switchDiffers != 0 || countDiffers != 0) return 1;
    return (levelDiffers != 0 && msSinceSent >= kPowerLevelGapMs) ? 1 : 0;
}
inline int PowerLevelDiffers(float a, float b) { const float d = a - b; return (d > kPowerLevelEps || d < -kPowerLevelEps) ? 1 : 0; }
/* a stock that rose: the input step 0x29A330 lowers a stock every frame and raises it when it takes a fuel item out of the
   generator's inventory. Sent at once, so a game that becomes the writer starts from the stock that already holds that item and
   does not take a second one. A stock that fell waits for the gap: a new writer that starts from a higher stock burns a little
   longer and takes no item. */
inline int PowerStockRose(float now, float sent) { return (now - sent > kPowerLevelEps) ? 1 : 0; }
/* a stock that reached 0 from a sent stock above 0: the engine stops a generator's fuel step once its stock is 0, so this is the
   last change that step makes - sent at once, as a count change, not held for the level gap */
inline int PowerStockEmptied(float now, float sent) { return (now <= kPowerLevelEps && sent > kPowerLevelEps) ? 1 : 0; }

/* the look queue (build.cpp PwMark): an engine step that runs off the main thread marks its piece for a look on the main thread.
   A piece's address picks a window of kPowerMarkProbe neighbouring slots in a gap table; each slot keeps the piece that holds
   it and that slot's last mark time. kind[i]: 1 = the slot holds this piece, 0 = empty, 2 = another piece; age[i] = ms since
   that slot's last mark. The slot to stamp (a look is queued), or -1 = not due:
   - the piece's own slot: due once its age reaches kPowerMarkGapMs;
   - else the first empty slot: due;
   - else the oldest slot another piece holds: taken once its age reaches half of kPowerMarkGapMs (a piece the engine steps
     stamps its own slot every gap, so the pieces of a full window take turns instead of one waiting for ever).
   force (a write the mod made) passes the gap. Without force every slot is stamped at most once per half gap (an empty one
   once), so pieces whose windows share slots never mark on every step. */
const unsigned int kPowerMarkGapMs = 250;   /* well under build.cpp kPwStaleMs (2000): a piece the engine steps keeps its row */
const int kPowerMarkProbe = 4;
inline int PowerMarkPick(const int* kind, const unsigned int* age, int n, int force)
{
    for (int i = 0; i < n; ++i)
        if (kind[i] == 1) return (force != 0 || age[i] >= kPowerMarkGapMs) ? i : -1;
    for (int i = 0; i < n; ++i)
        if (kind[i] == 0) return i;
    int o = -1;
    for (int i = 0; i < n; ++i)
        if (o < 0 || age[i] > age[o]) o = i;
    if (o < 0) return -1;
    return (force != 0 || age[o] >= kPowerMarkGapMs / 2) ? o : -1;
}
/* what one due off-main mark (PowerMarkPick >= 0) becomes in build.cpp PwMark. blocked = EngineWritesBlocked() (a pause, a load,
   a teardown: the K2 drain is skipped then, so nothing is queued - the slot is stamped and the piece comes again after the gap);
   hasKey = the piece's position key read; room = the queue has a free record. Each outcome has its own counter, so a full queue
   (kPowerMarkFull) is a real overflow, never a skipped drain */
const int kPowerMarkQueued = 0, kPowerMarkBlocked = 1, kPowerMarkNoKey = 2, kPowerMarkFull = 3;
inline int PowerMarkOutcome(int blocked, int hasKey, int room)
{
    if (blocked != 0) return kPowerMarkBlocked;
    if (hasKey == 0) return kPowerMarkNoKey;
    return room != 0 ? kPowerMarkQueued : kPowerMarkFull;
}

/* which budget a [POWER] line spends (build.cpp PwSay; kPowerSayCap lines each per process, the REPORT not counted).
   changed = the line tells of a change or an exchange (the switch or gets-battery changed, a stock reached 0, an ask sent or done,
   a POWER_STATE refused or dropped) -> kPowerSayEvent, its own budget, so the first looks never use it up. first = the first send
   or first apply of a piece with nothing changed -> kPowerSayFirst, and only for a piece a player owns (owned: this game's player
   or another player); a world piece's first look prints nothing (it is counted, as before). */
const int kPowerSayNone = 0, kPowerSayFirst = 1, kPowerSayEvent = 2;
const int kPowerSayCap = 64;
inline int PowerSayKind(int first, int changed, int owned)
{
    if (changed != 0) return kPowerSayEvent;
    return (first != 0 && owned != 0) ? kPowerSayFirst : kPowerSayNone;
}

/* the writer verdict (coopfarm::kProdWriter*: 0 none, 1 this game, 2 another game) -> what this game does with one piece */
inline int PowerGateRuns(int single, int writer) { return (single != 0 || writer != 2) ? 1 : 0; }   /* 0 = the burn / charge is the writer's */
inline int PowerStateAccept(int writer) { return writer == 2 ? 1 : 0; }    /* a POWER_STATE is written only on a game another game writes for */
/* a POWER_STATE's sender, by the MINE_STATE rule (farmwire.h coopfarm::FarmFromWriter): written only when it came from the game
   this game names the writer; the writer's slot or the sender's below 0 = not known now. -1 wait (kept), 0 refuse, 1 write */
inline int PowerStateFrom(int writer, int writerSlot, int senderSlot)
{
    if (PowerStateAccept(writer) == 0) return 0;
    if (writerSlot < 0 || senderSlot < 0) return -1;
    return writerSlot == senderSlot ? 1 : 0;
}
inline int PowerAskAccept(int writer) { return writer == 1 ? 1 : 0; }      /* a POWER_ASK is done only by the writer */
/* the asker's side: the live switch differs from the writer's last state written here -> 1 ask (and put the switch back) */
inline int PowerAskDue(int haveWriterState, int liveOn, int writerOn) { return (haveWriterState != 0 && (liveOn != 0) != (writerOn != 0)) ? 1 : 0; }
/* the writer's side: a teammate's ask. asker / owner = slots (-1 unknown); same = the two are one team. 1 = done here */
inline int PowerAskFromOk(int asker, int owner, int same) { return (asker >= 0 && owner >= 0 && (asker == owner || same != 0)) ? 1 : 0; }

/* H-P47a: the writer verdict an engine step OFF the main thread uses (build.cpp detour_pwCharge / detour_pwInputs). live =
   ProdStepWriterHere's answer there (0 none: the owner check cannot answer off the main thread for a player's piece); have / pub =
   the verdict the main thread published for the piece (build.cpp PwPublish, from PwRowFor's ProdWriterDecide) and age = ms since
   the main thread last looked at the piece. The live answer wins when it knows; else the published one while its row is live
   (age <= staleMs, build.cpp kPwStaleMs: a pointer not looked at that long starts a new row) and it names a writer; else none -
   the step runs, as before. Which one decided (kPowerFrom*): */
const int kPowerFromNone = 0, kPowerFromLive = 1, kPowerFromPublished = 2;
inline int PowerStepSource(int live, int have, int pub, unsigned int age, unsigned int staleMs)
{
    if (live != 0) return kPowerFromLive;
    if (have != 0 && pub != 0 && age <= staleMs) return kPowerFromPublished;
    return kPowerFromNone;
}
/* the verdict PowerStepSource picked (0 none) */
inline int PowerStepWriter(int source, int live, int pub) { return source == kPowerFromLive ? live : (source == kPowerFromPublished ? pub : 0); }

/* H-P47b: the mod's own completion apply (build.cpp BdApplyOne: vt+0x230 FLT_MAX, the engine's complete-now path, which switches
   the piece on). On a game another game writes the piece for (writer 2) that holds the writer's last switch (have, writerOn), the
   copy's switch is put back to the writer's, so the copy mirrors the writer and its look (PowerAskDue) asks nothing. afterOn = the
   switch read after the call (-1 unreadable). The switch to write, or -1 = leave it */
inline int PowerCompletionSwitch(int writer, int have, int writerOn, int afterOn)
{
    if (PowerStateAccept(writer) == 0 || have == 0 || afterOn < 0) return -1;
    const int w = writerOn != 0 ? 1 : 0;
    return ((afterOn != 0 ? 1 : 0) != w) ? w : -1;
}

} // namespace powerwire
