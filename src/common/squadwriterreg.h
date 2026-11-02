#pragma once
/* T-1084 step 5 (owner decision 1158 a): ONE GAME WRITES EACH SQUAD'S RECORD - THE WORLD SERVER'S PER-SQUAD WRITER REGISTER.
   Pure rules only; the world server (store_main.cpp) and the game (store.cpp) do the reading and the acting.

   THE REGISTER: per squad world id (a group id "<faction>_<n>", not a zone / ground / box / piece / shop record) the world server keeps
   the WRITER - the player slot of the game whose writes it takes - and a GENERATION that rises by one at every registration. Both are
   stamped from the sending connection's slot, never read from a message, and they ride on the record's index line as field 17
   ("<slot>.<gen>", storemeta.h) so a restart of the world server keeps them. A line without field 17 (any line written before step 5)
   reads as unregistered. Beside it, in the world server's memory only, two marks per squad:
     released - the writer's game said its engine put the squad away (SQUAD_RELEASE), or the writer's own plain or discard delete was
                taken (DeleteReleases: its game no longer has the squad): it runs none of the squad's people now. A
                registration read back after a world-server restart counts as released until its writer writes it or takes it again;
                so does every squad registered to a game whose link closed (a crash or a quit sends no release).
     keeper   - the writer registered for a SLEEPING squad (a take that says "I keep it asleep", or the holder's own position note):
                it runs none of the people, it only stands for the squad's area.

   WHO RUNS THE PEOPLE DECIDES, NEVER SILENCE. A running squad writes only when something changed (a wake with no change sends nothing),
   so a writer's quiet says nothing. The writer is AWAY (WriterAway) only on an event: its game has left the world, it RELEASED the squad,
   or it is a keeper that no longer stands for the squad's area (another game holds it now).

   THE RULES (Judge), decided at the moment of the write against the world server's own area table:
     unregistered     - a full write is taken and its sender registered (the first write, or a re-push after the world server lost its
                        data); a delete is taken; a take registers its sender; a position note obeys the position rule below.
     the writer       - every write and delete of the registered writer is taken, wherever the squad now stands (a squad walking into
                        another game's area stays with the game running its people). Its full write, or its take as the runner, clears
                        its released mark (it runs the squad again). A RELEASED writer's plain delete (a DELETED_BITS number is one) is
                        HELD while another live game has the squad's area loaded - that game may run the squad now - and judged again
                        when that changes (HeldDeleteStep): applied, or refused when another game has taken the squad meanwhile.
     a take           - SQUAD_TAKE says why: RUNS (the sender runs a strict majority of the squad's living people - sent at each wake of
                        the squad in that game, and when the world server says a refusal may be answered differently now) or KEEP (a
                        sleeping squad nobody here runs, in an area the sender holds, or the off-screen authority's where nobody holds).
                        RUNS is taken when the writer is away or is only a keeper; KEEP when the writer is away and the sender stands
                        for the area. Both carry the generation the game last learned (0 = none); it is not judged - a game learns a
                        generation only from answers to its own messages, so it is out of date after every hand-over, and the live rule
                        above decides every take.
     another game's full write or delete - taken over when the writer is away or is only a keeper (the roads that send them ask the
                        game's own rules first: they write a squad whose people that game runs).
     position notes   - the off-screen authority's sleeping-position notes are taken only while nobody holds the area of the squad's
                        stored position (or from the registered writer itself); the holder's own note takes a squad whose writer is
                        away (as its keeper). A note never registers anybody otherwise.
     anything else    - refused, with a SQUAD_WRITER reply to the sender; the world server remembers who it refused, and when the
                        writer releases the squad (SQUAD_RELEASE, or its own plain or discard delete taken) or its game leaves it tells
                        each of them (ASK_AGAIN) - the refused game drops its
                        refusal and answers with a take when it runs or keeps the squad (its later writes are judged here).

   THE WIRE, on the world-server link, protocol 122 (store_main.cpp's enum; store.cpp kStoreMsgSquad*):
     84 SQUAD_TAKE    up   {u32 len, id bytes, u32 seenGen, u8 basis} - basis 1 RUNS, 2 KEEP.
     85 SQUAD_WRITER  down {u32 len, id bytes, u8 verdict, u8 kind, i32 writerSlot, u32 gen} - the answer to a TAKE (taken or refused),
                      to every refused RECORD / RECORD_GONE / DELETED_BITS number of a squad record, and ASK_AGAIN (verdict 20, kind
                      RELEASE) to each game refused since the writer released the squad or left. After a refusal of a write the world
                      server sends its stored record of that id, so the refused game's rolled-back copy is the world's version.
     86 SQUAD_RELEASE up   {u32 len, id bytes} - "my engine put this squad away; I run none of its people now". Sent after the squad's
                      sleep write on the same ordered channel, when this game's heartbeat finds another game running a strict majority
                      of the squad's living people where this game ran them, and after a re-pushed record of a squad this game does not
                      run; taken only from the registered writer. */
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>

namespace sqwreg {

const unsigned kProtocol = 134u;          /* the world-server protocol that carries SQUAD_TAKE, SQUAD_WRITER (its answers and its writer notes) and SQUAD_RELEASE */


const unsigned kMsgSquadTake = 84u;
const unsigned kMsgSquadWriter = 85u;
const unsigned kMsgSquadRelease = 86u;
const size_t kIdMax = 256;                /* a world id longer than this is not one */

/* what is being written */
enum { kKindFull = 0, kKindPosition = 1, kKindDelete = 2, kKindDiscard = 3, kKindTake = 4, kKindRelease = 5 };
/* a take's reason */
enum { kBasisNone = 0, kBasisRuns = 1, kBasisKeep = 2 };
/* the verdicts: 1-4 take the write, 10-12 refuse it; 20 is the ask-again note (neither) */
enum { kSwTaken = 1, kSwWriter = 2, kSwTakeover = 3, kSwNoHolderPos = 4,
       kSwRefuseNotWriter = 10, kSwRefuseOldGen = 11, kSwRefusePos = 12, kSwHoldDelete = 13, kSwAskAgain = 20 };   /* 11: no longer judged, the value kept on the wire; 13: kept by the world server, never sent */
inline int Takes(int v) { return (v >= kSwTaken && v <= kSwNoHolderPos) ? 1 : 0; }
inline int IsRefusal(int v) { return (v >= kSwRefuseNotWriter && v <= kSwRefusePos) ? 1 : 0; }
inline const char* VerdictName(int v)
{
    switch (v)
    {
        case kSwTaken: return "taken (unregistered)";
        case kSwWriter: return "the registered writer";
        case kSwTakeover: return "take-over";
        case kSwNoHolderPos: return "position note while nobody holds the area";
        case kSwRefuseNotWriter: return "REFUSED - another game is the registered writer and it still runs the squad (or this game neither runs nor keeps it)";
        case kSwRefuseOldGen: return "REFUSED - the take named an older generation than the registered one";
        case kSwRefusePos: return "REFUSED - a position note for a squad whose area a game holds";
        case kSwHoldDelete: return "HELD - a released writer's delete while another live game has the area loaded (judged again when that changes)";
        case kSwAskAgain: return "ASK AGAIN - the writer released the squad or left the world";
        case 21: return "NOTE - this game runs the squad now";
        case 22: return "NOTE - this game runs the squad no more";
        case 23: return "NOTE - this game left the world";
        case 24: return "NOTE - the end of the list after the WELCOME";
        default: return "?";
    }
}
inline const char* KindName(int k)
{
    switch (k)
    {
        case kKindFull: return "record";
        case kKindPosition: return "position note";
        case kKindDelete: return "delete";
        case kKindDiscard: return "discard delete";
        case kKindTake: return "take-over";
        case kKindRelease: return "release";
        case 6: return "writer note";
        default: return "?";
    }
}

/* one squad's registration: slot -1 = unregistered */
struct WriterRow { int slot; unsigned gen; WriterRow() : slot(-1), gen(0u) {} };

/* the inputs, all read by the world server at the moment of the decision */
struct JudgeIn
{
    int kind;              /* kKind* */
    int basis;             /* a TAKE's kBasis* */
    int sender;            /* the sender's player slot (from its connection, or from its player id for a held message); -1 unknown */
    int senderAuthority;   /* 1 = the sender is the off-screen authority */
    int registered;        /* 1 = the squad has a registered writer */
    int writer;            /* its slot */
    unsigned storedGen;    /* its generation */
    unsigned seenGen;      /* a TAKE's generation: the one the sender last saw (0 = none); carried, not judged */
    int holder;            /* the holder of the area of the squad's stored position (the write's own position when nothing is stored); -1 nobody */
    int holderLive;        /* 1 = a game with the holder's slot is connected (a claim lingering after its game left, or restored after a world-server restart, is nobody's) */
    int senderLoadedHere;  /* 1 = the sender's latest area report lists that area */
    int writerLive;        /* 1 = a game with the writer's slot is connected */
    int writerReleased;    /* 1 = the writer released the squad (SQUAD_RELEASE), or the registration was read back at a restart and not renewed */
    int writerKeeper;      /* 1 = the writer registered for a sleeping squad (it runs none of the people) */
    int writerAuthority;   /* 1 = the writer's game is the off-screen authority */
    int writerLoadedHere;  /* 1 = the writer has reported that area loaded within the area grace */
    int otherLoadedHere;   /* 1 = a live game other than the sender lists that area in its latest area report (read for a delete only) */
    JudgeIn() : kind(kKindFull), basis(kBasisNone), sender(-1), senderAuthority(0), registered(0), writer(-1), storedGen(0u), seenGen(0u), holder(-1), holderLive(1),
                senderLoadedHere(0), writerLive(0), writerReleased(0), writerKeeper(0), writerAuthority(0), writerLoadedHere(0), otherLoadedHere(0) {}
};

/* the holder that counts: a holder whose game is not connected is nobody */
inline int LiveHolder(const JudgeIn& in) { return (in.holder >= 0 && in.holderLive != 0) ? in.holder : -1; }
/* the sender stands for the area: it is its live holder, or no live game holds it and the sender has it loaded */
inline int SenderMayHold(const JudgeIn& in)
{
    if (in.sender < 0) return 0;
    const int h = LiveHolder(in);
    if (h >= 0) return h == in.sender ? 1 : 0;
    return in.senderLoadedHere != 0 ? 1 : 0;
}
/* the sender may keep a sleeping squad there: it stands for the area, or nobody live holds it and the sender is the off-screen authority */
inline int SenderMayKeep(const JudgeIn& in)
{
    if (SenderMayHold(in) != 0) return 1;
    return (in.sender >= 0 && LiveHolder(in) < 0 && in.senderAuthority != 0) ? 1 : 0;
}
/* a keeper writer still stands for the squad's area: it is the live holder, or nobody live holds it and it is the authority or has it loaded */
inline int WriterStandsForArea(const JudgeIn& in)
{
    const int h = LiveHolder(in);
    if (h >= 0) return h == in.writer ? 1 : 0;
    return (in.writerAuthority != 0 || in.writerLoadedHere != 0) ? 1 : 0;
}
/* the writer is away from the squad - only on an event, never on silence: its game has left; it released the squad; or it is a keeper
   that no longer stands for the squad's area */
inline int WriterAway(const JudgeIn& in)
{
    if (in.writerLive == 0) return 1;
    if (in.writerReleased != 0) return 1;
    return (in.writerKeeper != 0 && WriterStandsForArea(in) == 0) ? 1 : 0;
}
/* the writer runs none of the squad's people now: away, or only its keeper - a game that runs them (a full write, a RUNS take) takes over */
inline int WriterRunsNone(const JudgeIn& in) { return (WriterAway(in) != 0 || in.writerKeeper != 0) ? 1 : 0; }
inline int Judge(const JudgeIn& in)
{
    const int h = LiveHolder(in);
    if (in.kind == kKindPosition)
    {
        if (in.registered != 0 && in.sender >= 0 && in.sender == in.writer) return kSwWriter;
        if (in.registered != 0 && h >= 0 && in.sender == h && WriterAway(in) != 0) return kSwTakeover;   /* the holder's own note, the writer away: it keeps the squad */
        if (h < 0) return (in.senderAuthority != 0 || in.registered == 0) ? kSwNoHolderPos : kSwRefusePos;
        if (in.registered == 0 && in.sender >= 0 && h == in.sender) return kSwTaken;   /* the holder's own note of an unregistered squad */
        return kSwRefusePos;
    }
    if (in.kind == kKindTake)
    {
        if (in.sender < 0 || (in.basis != kBasisRuns && in.basis != kBasisKeep)) return kSwRefuseNotWriter;
        if (in.registered != 0 && in.sender == in.writer) return kSwWriter;
        if (in.basis == kBasisKeep && SenderMayKeep(in) == 0) return kSwRefuseNotWriter;
        if (in.registered == 0) return kSwTakeover;
        if ((in.basis == kBasisRuns ? WriterRunsNone(in) : WriterAway(in)) == 0) return kSwRefuseNotWriter;
        return kSwTakeover;   /* the take's generation (seenGen) is not judged: the live rule above decides */
    }
    /* a full write, a plain delete (the wipe-out delete is one) or a discard delete */
    if (in.registered == 0) return kSwTaken;
    if (in.sender >= 0 && in.sender == in.writer)
    {   /* a released writer runs none of the people: its plain delete does not end a squad another live game has loaded (that game may run
           it now, its take not landed yet) - it is held and judged again (HeldDeleteStep) */
        if (in.kind == kKindDelete && in.writerReleased != 0 && in.otherLoadedHere != 0) return kSwHoldDelete;
        return kSwWriter;
    }
    if (in.sender >= 0 && WriterRunsNone(in) != 0) return kSwTakeover;
    return kSwRefuseNotWriter;
}
/* the registration after a verdict: a first full write registers its sender, a take-over moves it; the generation rises by one */
inline WriterRow Apply(const WriterRow& was, int verdict, int kind, int sender)
{
    WriterRow r = was;
    if (sender < 0) return r;
    if ((verdict == kSwTaken && kind == kKindFull) || verdict == kSwTakeover) { r.slot = sender; r.gen = was.gen + 1u; }
    return r;
}
/* the keeper mark after a verdict that took the write: a KEEP take or the holder's note that registers -> 1; a registration by a full
   write, a delete or a RUNS take, or the writer's own full write or RUNS take -> 0; anything else leaves it */
inline int KeeperAfter(int verdict, int kind, int basis, int wasKeeper)
{
    if (verdict == kSwTakeover) return (kind == kKindPosition || (kind == kKindTake && basis == kBasisKeep)) ? 1 : 0;
    if (verdict == kSwTaken && kind == kKindFull) return 0;
    if (verdict == kSwWriter && (kind == kKindFull || (kind == kKindTake && basis == kBasisRuns))) return 0;
    return wasKeeper;
}
/* a plain or discard delete taken from the game that is (or by it becomes) the registered writer is that writer's release: its own game
   reports it no longer has the squad - even when the discard hold then keeps the record; the games refused for it are told ASK_AGAIN */
inline int DeleteReleases(int verdict, int kind)
{
    return ((verdict == kSwWriter || verdict == kSwTakeover) && (kind == kKindDelete || kind == kKindDiscard)) ? 1 : 0;
}
/* the released mark after a verdict that took the write: the writer's own delete sets it (DeleteReleases); a new registration clears it;
   so does the writer's own full write or RUNS take (it runs the squad again). Its position note does not (the off-screen travel of a
   sleeping squad). */
inline int ReleasedAfter(int verdict, int kind, int basis, int wasReleased)
{
    if (DeleteReleases(verdict, kind) != 0) return 1;
    if (verdict == kSwTakeover || (verdict == kSwTaken && kind == kKindFull)) return 0;
    if (verdict == kSwWriter && (kind == kKindFull || (kind == kKindTake && basis == kBasisRuns))) return 0;
    return wasReleased;
}
/* SQUAD_RELEASE: taken only from the registered writer */
inline int ReleaseTaken(int registered, int writer, int sender) { return (registered != 0 && sender >= 0 && sender == writer) ? 1 : 0; }
/* a game's link closed (a crash or a quit sends no release): every squad registered to its slot counts as released from now on, so the
   game does not block its squads when it comes back; if it still runs one, its next RUNS take (each wake asks again on a new link) or
   full write renews it */
inline int ReleasedAtLinkClose(int writer, int closedSlot, int wasReleased) { return (closedSlot >= 0 && writer == closedSlot) ? 1 : wasReleased; }
/* THE HELD DELETE (kSwHoldDelete): a released writer's plain delete or DELETED_BITS number, kept by the world server while another live game
   has the squad's area loaded, and judged again when a game's area report drops an area, when an area's holder changes, when a game's link
   closes and when a write of that record lands. At most kSwHeldDeleteCap are held; one more is refused at once (its sender's delete is
   taken back), counted. */
const size_t kSwHeldDeleteCap = 256;
inline int SwHeldDeleteAdmit(size_t held, size_t cap, int alreadyHeld) { return (alreadyHeld != 0 || held < cap) ? 1 : 0; }
/* one held delete judged again: no record any more, or the record written again since it was held -> dropped; another game is the writer
   now -> refused (the reply takes the sender's local delete back); another live game still has the area loaded -> kept; else applied */
enum { kHdKeep = 0, kHdApply = 1, kHdDrop = 2, kHdRefuse = 3 };
inline int HeldDeleteStep(int haveRecord, int writerNow, int sender, long long writtenAtNow, long long writtenAtHeld, int otherLoaded)
{
    if (haveRecord == 0) return kHdDrop;
    if (writerNow != sender) return kHdRefuse;
    if (writtenAtNow != writtenAtHeld) return kHdDrop;
    return otherLoaded != 0 ? kHdKeep : kHdApply;
}

/* ---- index line field 17 ---- */
inline std::string WriterField(const WriterRow& w)
{
    if (w.slot < 0) return std::string();
    char b[32]; std::sprintf(b, "%d.%u", w.slot, w.gen); return b;
}
/* 1 = read; 0 = empty (unregistered); -1 = not a field this build wrote (read as unregistered). A trailing CR is ignored. */
inline int WriterOfField(const std::string& fIn, WriterRow* out)
{
    *out = WriterRow();
    std::string f = fIn;
    if (!f.empty() && f[f.size() - 1] == '\r') f.erase(f.size() - 1);
    if (f.empty()) return 0;
    const size_t dot = f.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= f.size() || dot > 5 || f.size() - dot - 1 > 10) return -1;
    for (size_t i = 0; i < f.size(); ++i) if (i != dot && (f[i] < '0' || f[i] > '9')) return -1;
    long slot = 0; unsigned long long gen = 0ULL;
    for (size_t i = 0; i < dot; ++i) slot = slot * 10 + (f[i] - '0');
    for (size_t i = dot + 1; i < f.size(); ++i) gen = gen * 10ULL + (unsigned long long)(f[i] - '0');
    if (slot > 65535 || gen > 0xFFFFFFFFULL) return -1;
    out->slot = (int)slot; out->gen = (unsigned)gen;
    return 1;
}

/* ---- the wire ---- */
inline void PutU32(std::vector<char>* b, unsigned v) { const size_t at = b->size(); b->resize(at + 4); std::memcpy(&(*b)[at], &v, 4); }
inline int GetU32(const std::vector<char>& b, size_t at, unsigned* v) { if (at + 4 > b.size()) return 0; std::memcpy(v, &b[at], 4); return 1; }
inline void PutId(std::vector<char>* b, const std::string& id) { PutU32(b, (unsigned)id.size()); b->insert(b->end(), id.begin(), id.end()); }
inline int GetId(const std::vector<char>& b, size_t* at, std::string* id)
{
    unsigned n = 0;
    if (!GetU32(b, *at, &n) || n == 0 || n > kIdMax || *at + 4 + n > b.size()) return 0;
    id->assign(b.begin() + (std::ptrdiff_t)(*at + 4), b.begin() + (std::ptrdiff_t)(*at + 4 + n)); *at += 4 + n;
    return 1;
}
inline std::vector<char> EncodeTake(const std::string& id, unsigned seenGen, int basis)
{
    std::vector<char> b; PutId(&b, id); PutU32(&b, seenGen); b.push_back((char)(unsigned char)basis); return b;
}
inline int DecodeTake(const std::vector<char>& b, std::string* id, unsigned* seenGen, int* basis)
{
    size_t at = 0;
    if (!GetId(b, &at, id) || !GetU32(b, at, seenGen) || at + 5 != b.size()) return 0;
    *basis = (unsigned char)b[at + 4];
    return (*basis == kBasisRuns || *basis == kBasisKeep) ? 1 : 0;
}
inline std::vector<char> EncodeRelease(const std::string& id) { std::vector<char> b; PutId(&b, id); return b; }
inline int DecodeRelease(const std::vector<char>& b, std::string* id) { size_t at = 0; return (GetId(b, &at, id) && at == b.size()) ? 1 : 0; }
struct WriterReply { std::string id; int verdict; int kind; int writerSlot; unsigned gen; WriterReply() : verdict(0), kind(0), writerSlot(-1), gen(0u) {} };
inline std::vector<char> EncodeReply(const WriterReply& r)
{
    std::vector<char> b; PutId(&b, r.id); b.push_back((char)(unsigned char)r.verdict); b.push_back((char)(unsigned char)r.kind);
    PutU32(&b, (unsigned)r.writerSlot); PutU32(&b, r.gen); return b;
}
inline int DecodeReply(const std::vector<char>& b, WriterReply* r)
{
    size_t at = 0; unsigned ws = 0, g = 0;
    if (!GetId(b, &at, &r->id) || at + 2 + 8 != b.size()) return 0;
    r->verdict = (unsigned char)b[at]; r->kind = (unsigned char)b[at + 1]; at += 2;
    GetU32(b, at, &ws); GetU32(b, at + 4, &g);
    r->writerSlot = (int)ws; r->gen = g;
    if (r->verdict >= 21 && r->verdict <= 24) return r->kind == 6 ? 1 : 0;   /* a writer note (below): only with its own kind */
    return (Takes(r->verdict) != 0 || IsRefusal(r->verdict) != 0 || r->verdict == kSwAskAgain) && r->kind <= kKindRelease ? 1 : 0;
}

/* ---- the game's side ---- */
/* the roads that do not ask squadwriter.h NpcSquadWriter (the engine's own destroy delete, the unload discard delete, the reconnect
   re-push and the deletes re-sent at a reconnect) ask this first. squadRec: a squad group id this game would write (not a zone, ground,
   box, piece or shop record); refusedHere: the world server refused this game's write of it and no event has asked again since. Nothing
   else is guessed here: the world server judges every other send, and its refusal takes this game's delete back. */
enum { kGameSend = 1, kGameSkipRefused = 2 };
inline int GameMaySend(int squadRec, int refusedHere)
{
    if (squadRec == 0) return kGameSend;
    return refusedHere != 0 ? kGameSkipRefused : kGameSend;
}
/* the take this game may send for a squad, read live: kBasisRuns = it runs a strict majority of the squad's living people (runner ==
   mySlot); else kBasisNone when any game runs some of them here (anyRunner: a runner, or a top slot, read from a platoon of the squad
   in this game); else kBasisKeep when this game holds the squad's recorded area (mineHeld 1), or it is the off-screen authority and
   nobody holds it (otherHolds 0); else kBasisNone */
inline int TakeBasis(int mySlot, int runner, int anyRunner, int mineHeld, int otherHolds, int authority)
{
    if (mySlot >= 0 && runner == mySlot) return kBasisRuns;
    if (anyRunner != 0) return kBasisNone;
    if (mineHeld == 1) return kBasisKeep;
    return (mineHeld == 0 && otherHolds == 0 && authority != 0) ? kBasisKeep : kBasisNone;
}
/* a record this game takes over when it newly holds an area: a world NPC squad's (npcFaction: not a player's, a peer's or a stand-in
   record's faction), a live full record (usable: here, written, not gone), standing in an area this game newly holds, which this game
   may take (basis: TakeBasis - a squad another game runs here is not taken) */
inline int TakeWanted(int npcFaction, int usable, int inNewlyHeld, int basis) { return (npcFaction != 0 && usable != 0 && inNewlyHeld != 0 && basis != kBasisNone) ? 1 : 0; }
/* the RUNS take at a wake of the squad in this game (store.cpp's heartbeat visit): once per wake, while no take is out, for a world NPC
   squad's group id whose living people this game runs a strict majority of */
inline int RunsTakeDue(int squadRec, int npcFaction, int runsHere, int askedThisWake, int takeOut, int linkUp)
{
    return (squadRec != 0 && npcFaction != 0 && runsHere != 0 && askedThisWake == 0 && takeOut == 0 && linkUp != 0) ? 1 : 0;
}
/* the SQUAD_RELEASE at the engine's put-away of a squad in this game: a world NPC squad's group id this game is not refused for (the
   world server takes it only from the registered writer) */
inline int PutAwayRelease(int squadRec, int npcFaction, int refusedHere) { return (squadRec != 0 && npcFaction != 0 && refusedHere == 0) ? 1 : 0; }
/* this game's heartbeat finds another game (runner: its slot) running a strict majority of the squad's living people: this game's engine
   runs them no more - the release as at a put-away, so the new runner's RUNS take is taken. A split squad (no strict majority: runner -1)
   releases nothing - its writer keeps writing it */
inline int RunnerMovedRelease(int mySlot, int runner, int squadRec, int npcFaction, int refusedHere)
{
    return (mySlot >= 0 && runner >= 0 && runner != mySlot && PutAwayRelease(squadRec, npcFaction, refusedHere) != 0) ? 1 : 0;
}
/* a squad record re-pushed after the world server lost its data registers its sender as the writer: a release follows it on the same
   ordered channel unless this game runs the squad (basis RUNS), so the game that runs it takes it at its next wake */
inline int RepushRelease(int squadRec, int npcFaction, int basis) { return (squadRec != 0 && npcFaction != 0 && basis != kBasisRuns) ? 1 : 0; }
/* the register's answer marks this game's copy changed (written at its next heartbeat visit, the checksum of the bytes last sent
   forgotten): a refused full write (the rolled-back copy is not what the world server holds) and a take that made this game the writer
   (its changes refused meanwhile were never sent). The writer's own answer marks nothing (a write at every wake). */
inline int CopyMarkChanged(int verdict, int kind)
{
    return ((IsRefusal(verdict) != 0 && kind == kKindFull) || (verdict == kSwTakeover && kind == kKindTake)) ? 1 : 0;
}
/* a refusal is applied only when its generation is not older than the one already applied for that id (a late answer never undoes a
   newer one); an answer that takes the write is always applied - after a world-server data loss the generations start again at 1, and an
   accepting answer is the register as it now stands. (The game also forgets its generations at each new world-server link.) */
inline int ReplyFresh(int haveGen, unsigned heldGen, unsigned replyGen, int takes) { return (haveGen == 0 || takes != 0 || replyGen >= heldGen) ? 1 : 0; }
/* a refused squad, looked at at each area map this game applies: kReaskTake = send one SQUAD_TAKE - this game newly holds the squad's
   area (newlyMine), no take is out and it may take it (basis); kReaskLift = the refusal is dropped (the off-screen authority, nobody
   holds the area: its notes and writes are judged again); kReaskNone = kept. A writer becoming away has its own event (ASK_AGAIN). */
enum { kReaskNone = 0, kReaskTake = 1, kReaskLift = 2, kReaskDrop = 3 };
inline int RefusedReask(int newlyMine, int mine, int otherHolds, int authority, int takeOut, int basis)
{
    if (mine == 1) return (newlyMine != 0 && takeOut == 0 && basis != kBasisNone) ? kReaskTake : kReaskNone;
    return (mine == 0 && otherHolds == 0 && authority != 0) ? kReaskLift : kReaskNone;
}
/* the same, with the record: a refused id with no record here any more is dropped (kReaskDrop - nothing of it is written here) */
inline int RefusedReaskRec(int haveRecord, int newlyMine, int mine, int otherHolds, int authority, int takeOut, int basis)
{
    if (haveRecord == 0) return kReaskDrop;
    return RefusedReask(newlyMine, mine, otherHolds, authority, takeOut, basis);
}
/* ASK_AGAIN for a squad refused here: a take when this game runs or keeps it now and none is out */
inline int AskAgainTake(int takeOut, int basis) { return (takeOut == 0 && basis != kBasisNone) ? 1 : 0; }
/* ASK_AGAIN, the whole step: a squad refused here has its refusal dropped whether or not this game runs or keeps it now (its later
   writes - a short wake's sleep write, the release after it - are the world server's to judge), plus a take when AskAgainTake says */
enum { kAaNone = 0, kAaDrop = 1, kAaDropTake = 2 };
inline int AskAgainAct(int refusedHere, int takeOut, int basis)
{
    if (refusedHere == 0) return kAaNone;
    return AskAgainTake(takeOut, basis) != 0 ? kAaDropTake : kAaDrop;
}
/* a put-away the engine runs off the main thread (not its own create call: container set): the squad id is queued and the main thread
   sends the release SwPutAwayDone sends. At most kSwOffPutAwayCap wait; one more is not queued, counted. */
const size_t kSwOffPutAwayCap = 256;
inline int SwOffPutAwayAdmit(size_t queued, size_t cap, int container) { return (container == 0 && queued < cap) ? 1 : 0; }
/* at the main thread's drain of a queued off-thread put-away: the release goes only while this game runs no awake squad under its world id -
   neither the put-away platoon (woken again before the drain) nor another platoon here under that id; else the squad stays its writer's */
inline int OffPutAwayReleaseDue(int selfAwake, int idAwakeHere) { return (selfAwake == 0 && idAwakeHere == 0) ? 1 : 0; }

/* ---- THE WRITER NOTES [t1084i-note] ----
   The world server tells every connected game when a game starts or stops RUNNING a squad: its registered writer, while that game is
   connected, has not released the squad, is not only its keeper and the record is not gone (NoteRuns). A note rides SQUAD_WRITER (85)
   with kind 6 and the answer's bytes {u32 len, id bytes, u8 verdict, u8 kind 6, i32 writerSlot, u32 gen}:
     21 RUNS      - the game in writerSlot (registration gen) runs the squad now. Sent to every connected game but that one; the game that
                    takes over a squad another game ran is sent STOPPED for that game instead (its own note of the old runner goes).
     22 STOPPED   - the game in writerSlot runs the squad no more (released, only its keeper, its record gone). Sent to every connected
                    game but that one.
     23 SLOT GONE - the game in writerSlot left the world (its link closed): it runs none of its squads. id "*".
     24 LIST END  - right after a game's WELCOME push the world server sends one RUNS per squad another live game runs, then this. id "*".
   A note is never an answer to the receiving game's own take or write. The game keeps the squads another game runs (slot, gen), rebuilt
   from the list after each WELCOME; while another game runs a squad it builds no copy of it and wakes none, and each refused squad is
   built when that game lets it go. */
enum { kKindNote = 6 };
enum { kSwNoteRuns = 21, kSwNoteStopped = 22, kSwNoteSlotGone = 23, kSwNoteListEnd = 24 };
const char* const kNoteAllId = "*";   /* the id of a SLOT GONE and a LIST END: they name no squad */
inline int IsNote(int v) { return (v >= kSwNoteRuns && v <= kSwNoteListEnd) ? 1 : 0; }
/* the world server's side: the writer runs the squad now */
inline int NoteRuns(int registered, int writerLive, int released, int keeper, int gone)
{
    return (registered != 0 && writerLive != 0 && released == 0 && keeper == 0 && gone == 0) ? 1 : 0;
}
/* the note a change of the squad's running writer makes: was = the runner last told (slot -1 nobody), now = the runner now. 0 = none */
inline int NoteDue(int wasSlot, unsigned wasGen, int nowSlot, unsigned nowGen)
{
    if (nowSlot >= 0) return (nowSlot != wasSlot || nowGen != wasGen) ? kSwNoteRuns : 0;
    return wasSlot >= 0 ? kSwNoteStopped : 0;
}
/* what one connected game (peerSlot) is told of that change; *slot = the slot the note names. 0 = nothing for it */
inline int NoteFor(int peerSlot, int due, int wasSlot, int nowSlot, int* slot)
{
    *slot = -1;
    if (peerSlot < 0 || due == 0) return 0;
    if (due == kSwNoteRuns)
    {
        if (peerSlot != nowSlot) { *slot = nowSlot; return kSwNoteRuns; }
        if (wasSlot >= 0 && wasSlot != nowSlot) { *slot = wasSlot; return kSwNoteStopped; }   /* the new runner: its note of the old one goes */
        return 0;
    }
    if (due == kSwNoteStopped && peerSlot != wasSlot) { *slot = wasSlot; return kSwNoteStopped; }
    return 0;
}
/* the game's side: a RUNS or STOPPED note applied to its entry for the id (have, haveSlot). kNmSet = hold the note's slot and gen;
   kNmErase = drop the entry; kNmNone = unchanged. A RUNS of this game's own slot drops the entry; a STOPPED drops it only when it names
   the slot held (a later RUNS of another game is never undone by it). */
enum { kNmNone = 0, kNmSet = 1, kNmErase = 2 };
inline int NoteMapStep(int verdict, int noteSlot, int mySlot, int have, int haveSlot)
{
    if (verdict == kSwNoteRuns)
    {
        if (noteSlot >= 0 && noteSlot != mySlot) return kNmSet;
        return have != 0 ? kNmErase : kNmNone;
    }
    if (verdict == kSwNoteStopped) return (have != 0 && haveSlot == noteSlot) ? kNmErase : kNmNone;
    return kNmNone;
}
/* the note-only 'runs' mark [t1084i-f1-reg]: the writer runs the squad's people - set by a RUNS take (its own or a take-over) and by a
   full write that registers or moves the writer (a game writes a squad it took over because it runs it); cleared by a KEEP take or a
   position note that moves the writer, and by the writer's own delete. The writer's own full write leaves it: the sleep write at its
   put-away (and a re-pushed record) is a write of a squad that game no longer runs, and it must not announce RUNS. The release and the
   link close clear 'released' as before; the world server announces RUNS only while released is 0 and runs is 1. */
inline int RunsAfter(int verdict, int kind, int basis, int wasRuns)
{
    if (DeleteReleases(verdict, kind) != 0) return 0;
    if (verdict == kSwTakeover) return (kind == kKindFull || (kind == kKindTake && basis == kBasisRuns)) ? 1 : 0;
    if (verdict == kSwTaken && kind == kKindFull) return 1;
    if (verdict == kSwWriter && kind == kKindTake && basis == kBasisRuns) return 1;
    return wasRuns;
}
/* SLOT GONE waits the player-gone grace: a closed link starts a hold unless the same game (its permanent slot) is already live again on
   a newer link (an old link closing late); the hold is cancelled when that game is admitted again within the grace, else sent */
inline int SlotGoneHoldStart(int closedSlot, int liveAgainElsewhere) { return (closedSlot >= 0 && liveAgainElsewhere == 0) ? 1 : 0; }
enum { kSgWait = 0, kSgSend = 1, kSgCancel = 2 };
inline int SlotGoneHoldStep(double at, double now, double grace, int back)
{
    if (back != 0) return kSgCancel;
    return (now - at) >= grace ? kSgSend : kSgWait;
}
/* the game's list while its world-server link is down: kept through the grace (a blip is not a leave), dropped once the link has been
   down longer (the next WELCOME's list rebuilds it). *downSeen / *downAt: the outage's start (ms). 1 = drop it now */
const unsigned kOtherListGraceMs = 10000u;
inline int OtherListLinkStep(int linkUp, unsigned nowMs, int* downSeen, unsigned* downAt, int listEmpty)
{
    if (linkUp != 0) { *downSeen = 0; return 0; }
    if (*downSeen == 0) { *downSeen = 1; *downAt = nowMs; return 0; }
    return (listEmpty == 0 && nowMs - *downAt > kOtherListGraceMs) ? 1 : 0;
}
/* a wake the engine ran off the main thread, checked on the main thread: refused (put on ice and destroyed as a duplicate wake) when the
   queued platoon is still the awake platoon of that id, of a world NPC faction, and another game runs the squad */
inline int OffWakeRefuse(int stillAwakeSame, int worldFaction, int otherRuns) { return (stillAwakeSame != 0 && worldFaction != 0 && otherRuns != 0) ? 1 : 0; }
/* SLOT GONE: an entry naming that slot goes */
inline int SlotGoneDrops(int haveSlot, int goneSlot) { return (goneSlot >= 0 && haveSlot == goneSlot) ? 1 : 0; }
/* the gate on every road that builds or wakes a copy of a squad in this game: another live game runs it (an entry, not this game's slot) */
inline int OtherRunsBlocks(int have, int slot, int mySlot) { return (have != 0 && slot >= 0 && slot != mySlot) ? 1 : 0; }
/* a squad refused here while another game ran it (waiting) is built once no other game runs it */
inline int BuildAtRelease(int waiting, int otherRuns) { return (waiting != 0 && otherRuns == 0) ? 1 : 0; }
/* the build of a sleeping copy from its record (store.cpp CreateUnknownSquad) [t1084i-f2-reg]: built; failed (a gone or deleted record,
   missing references, or the engine did not take it - the roads do not try it again in this world); or refused because another live
   game runs the squad - not a failure: the id waits for that game to let the squad go. kCreateGoOn: the first step found nothing against */
enum { kCreateFailed = 0, kCreateBuilt = 1, kCreateOtherRuns = 2, kCreateGoOn = 3 };
/* the build's first step: a gone or deleted record fails before the runner is asked; another live game running the squad refuses */
inline int CreateFirstStep(int gone, int deleted, int otherRuns)
{
    if (gone != 0 || deleted != 0) return kCreateFailed;
    return otherRuns != 0 ? kCreateOtherRuns : kCreateGoOn;
}
/* 1 = the build's result puts the id on the failed-creation registry (never tried again in this world); a refusal for another runner does not */
inline int CreateMarksFailed(int result) { return result == kCreateFailed ? 1 : 0; }
/* 1 = an applied area map changed who holds an area: this game's 'I hold it' or its 'another game holds it' answer moved */
inline int AreaHeldChanged(int mineBefore, int mineAfter, int otherBefore, int otherAfter) { return (mineBefore != mineAfter || otherBefore != otherAfter) ? 1 : 0; }
/* 1 = a waiting id is asked again at an applied area map: no other game runs it now and its area's holder changed (areaChanged), or the
   map came after a no-map refusal (the caller passes 1 for every area then) */
inline int WaitReaskAtMap(int waiting, int otherRuns, int areaChanged) { return (waiting != 0 && otherRuns == 0 && areaChanged != 0) ? 1 : 0; }

}   // namespace sqwreg
