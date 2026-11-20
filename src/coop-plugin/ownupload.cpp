// ownupload.cpp - T-885 piece 1 "Upload" (docs/design-ownrec.md sections 1, 2 and 6; owner 1734 / 1745 a / 1752 a).
//
// WHAT IT DOES. Every record this game writes into the player's own records folder goes to the world server right after its
// local commit (OWNREC PUT, src/common/ownrecwire.h), and the world server answers once its copy is on its disk (OK: stored,
// duplicate, superseded or refused). The OUTBOX is not a second copy: it is the records folder itself plus own.sent, which says
// per key the W (write number) and R (the world server's receipt) the world server last confirmed. A record whose W is above
// its own.sent line - or a text file whose bytes differ from the confirmed copy - is unconfirmed, and every unconfirmed record
// is sent again at every WELCOME (a join, a reconnect). Nothing on a timer. In this piece NOTHING READS THE SERVER'S COPY.
//   own.sent missing    = nothing confirmed yet (an older world's first upload: every key is sent once);
//   own.sent unreadable = NOTHING is sent this session, said once (the class "missing and unreadable treated as one answer").
// TEST-ONLY lever `ownrec compare` (each key: local W, the W / R the world server confirmed) and `ownrec hold on|off` (nothing
// is sent while on; local writes go on and are sent at `off` or at the next WELCOME of a later life).
// T-885 PIECE 2 "Take" (design section 3): before every load (PreloadHoldsTitleLoad) the game asks the world server what it holds
// (INDEX), fetches every key the world holds newer than its own (GET / REC / END), commits them into the records folder with the
// world's W (OwnStoreEnqueue; text files whole), raises its write counter to the world's highest W, and then sets own.sent to the
// INDEX - so own.sent is corrected at every join, not only by OKs. A record whose local W is above the world's is kept and sent
// (1745 a). At the title nothing is sent on a link until its take has finished (a folder older than the world's copy must not
// send its text files first). Review fold (2026-11-17): an indexed record is judged by its committed index entry (a file locked
// just now is not a missing one); a key not taken is asked again (3 rounds), then keeps its own.sent row and is held from the outbox
// while its copy is the one the take saw; no answer for 60 s begins the take again (twice at most), then releases the load; a taken
// record keeps its own write time.
// MAIN THREAD ONLY. Plain file reads of the records folder; no engine call.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ownstore.h"
#include "coop_log.h"
#include "u8file.h"
#include "../common/ownrecwire.h"
#include "../common/fileprobe.h"

namespace coop {

bool StoreSendAdmin(unsigned type, const std::vector<char>& payload);   /* store.cpp (store.h): one message up the world-server link; false = down or not welcomed */
std::string StoreLinkWorldId();                                         /* store.cpp (store.h): "" unless the link is up and welcomed (protocol agreed) */
long StoreNotebookLinkGen();                                            /* store.cpp (store.h): +1 at every new world-server link */

namespace {

std::string g_upDir, g_upStore;                                   /* the records folder the outbox belongs to, and its name */
int g_upSentState = -1;                                           /* -1 not read yet; ownrecw::kSentNone / kSentRead / kSentUnreadable */
std::map<std::string, ownrecw::SentRow> g_upSent;                 /* own.sent in memory */
std::set<std::string> g_upPending;                                /* keys to look at and send */
ownrecw::Flight g_upFlight;                                       /* review folds 2 / 3: the copies on this link not yet answered (ONE per key, at most ownrecw::kWindow) and the keys waiting behind one */
std::map<std::string, std::pair<unsigned long long, unsigned int> > g_upSettled;    /* key -> (W, CRC) answered superseded / refused this session: not sent again */
std::map<std::string, DWORD> g_upRetry;                           /* key -> when a local read failed (tried again 1 s later) */
int g_upResendDue = 0, g_upHold = 0, g_upUnreadableSaid = 0;
long long g_upSentN = 0, g_upConfirmed = 0, g_upDuplicate = 0, g_upSuperseded = 0, g_upRefused = 0;
long long g_upResent = 0, g_upLocalUnread = 0, g_upLocalChanged = 0, g_upTooBig = 0, g_upOkMalformed = 0, g_upOkNotInFlight = 0, g_upSentWriteFail = 0;
long long g_upLines = 0, g_upWelcomes = 0;
/* T-885 piece 2: the take - one exchange per (link, store) */
enum { kTkNone = 0, kTkAsked = 1, kTkFetch = 2, kTkWrite = 3, kTkDone = 4 };
struct TakeSt
{
    int phase; unsigned int token; long linkGen; std::string store;
    ownrecw::IndexMsg idx; ownrecw::TakePlan plan; size_t next, localHad; std::vector<std::string> batch; std::set<std::string> batchGot, failed;
    std::vector<std::string> taken; DWORD startMs, progressMs; unsigned int restarts, rounds;
    TakeSt() : phase(kTkNone), token(0), linkGen(-1), next(0), localHad(0), startMs(0), progressMs(0), restarts(0), rounds(0) {}
};
TakeSt g_tk;
unsigned int g_tkToken = 0;
bool g_tkDoneValid = false; long g_tkDoneGen = -1; std::string g_tkDoneStore;
bool g_tkDoneRefused = false;   /* fold round 2: the last take ended REFUSED (ownrecw::TakeRefuses) - the load gate refuses its load; cleared when the load request goes away (OwnTakeLoadGone), so the next load takes again */
std::string g_tkNoUpStore;      /* fold round 2: a store whose take was refused - nothing of it goes up until a take of it ends clean */
DWORD g_tkHeardMs = 0; bool g_tkHeard = false;   /* fold round 2: the last time any message arrived on the world server's link (OwnTakeHeard) */
long long g_tkRestarted = 0, g_tkReleased = 0, g_tkRefetch = 0, g_tkUnread = 0, g_tkRefusedN = 0;
long long g_tkTakes = 0, g_tkRows = 0, g_tkGot = 0, g_tkSame = 0, g_tkNewer = 0, g_tkOnly = 0, g_tkFailed = 0, g_tkStale = 0, g_tkMalformed = 0, g_tkStatusBad = 0;
long long g_tkSentFixed = 0, g_tkSendFailed = 0, g_tkLastMs = -1, g_tkNotOpen = 0;

std::string N(long long v) { char b[32]; _snprintf(b, 31, "%lld", v); b[31] = 0; return b; }
std::string U(unsigned long long v) { char b[32]; _snprintf(b, 31, "%llu", v); b[31] = 0; return b; }
std::string H8(unsigned int v) { char b[16]; _snprintf(b, 15, "%08x", v); b[15] = 0; return b; }
bool LineDue() { return ownrecw::LogThis(++g_upLines, 200); }   /* the per-record lines: the first 200, then every 100th */
std::string Leaf(const std::string& dir)
{
    const std::string::size_type s = dir.find_last_of("\\/");
    return s == std::string::npos ? dir : dir.substr(s + 1);
}

/* own.sent written whole: temp file, flushed, renamed over the old one (never in place) */
bool WriteSent()
{
    if (g_upDir.empty()) return false;
    const std::string path = g_upDir + "\\" + ownrecw::kSentFile, tmp = path + "." + N((long long)::GetCurrentProcessId()) + ".tmp";
    const std::string body = ownrecw::SentText(g_upSent);
    HANDLE h = U8CreateFile(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = body.empty() ? TRUE : ::WriteFile(h, body.data(), (DWORD)body.size(), &wrote, 0);
    if (ok && wrote != (DWORD)body.size()) ok = FALSE;
    if (ok) ok = ::FlushFileBuffers(h);
    ::CloseHandle(h);
    if (!ok || !U8MoveFileEx(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { U8DeleteFile(tmp.c_str()); return false; }
    return true;
}

void LoadSent()
{
    const coopstore::FileProbe p = coopstore::ProbeFile(g_upDir + "\\" + ownrecw::kSentFile);
    g_upSentState = ownrecw::SentParse(p.kind, p.kind == coopstore::kFileReadable ? std::string(p.bytes.begin(), p.bytes.end()) : std::string(), &g_upSent);
    if (g_upSentState == ownrecw::kSentUnreadable)
    {
        if (!g_upUnreadableSaid)
            ErrorLog("[OWNREC] own.sent of store " + g_upStore + " exists but cannot be read (" + (p.kind == coopstore::kFileReadable ? std::string("a line does not parse")
                     : "Windows error " + N((long long)p.err)) + ") - NOTHING is sent to the world server this session; what is unconfirmed is judged again at the next start");
        g_upUnreadableSaid = 1;
    }
    else DebugLog("[OWNREC] store " + g_upStore + ": own.sent " + (g_upSentState == ownrecw::kSentNone ? std::string("none - nothing confirmed yet, every record will be sent")
                  : "read, " + N((long long)g_upSent.size()) + " confirmed key(s)"));
}

/* this key's local version: W, its bytes (read from the records folder) and CRC. 1 = have it, 0 = nothing to send (none committed,
   no file, the file is not what the index names - a newer commit is landing and its own note follows), -1 = a read failed (try later) */
int LocalOf(const std::string& key, unsigned long long* w, std::vector<char>* bytes, unsigned int* crc, unsigned int* people, bool readBytes, unsigned long long* at = 0)
{
    const bool text = ownrecw::IsTextKey(key);
    unsigned long long seq = 0; long long len = 0; unsigned int icrc = 0; int chars = 0; long long wat = 0;
    if (!text && !OwnStoreCommittedLine(key, &seq, &len, &icrc, &chars, &wat)) return 0;
    if (at != 0) *at = (!text && wat > 0) ? (unsigned long long)wat : 0ULL;   /* review fold (finding 5): the record's own write time goes with it */
    if (!text && !readBytes) { *w = seq; *crc = icrc; *people = (unsigned int)(chars < 0 ? 0 : chars); return 1; }
    coopstore::FileProbe p = coopstore::ProbeFile(g_upDir + "\\" + (text ? key : coopown::RecFile(key)));
    if (text && p.kind == coopstore::kFileAbsent) return 0;
    if (p.kind != coopstore::kFileReadable && p.kind != coopstore::kFileAbsent) { ++g_upLocalUnread; return -1; }
    unsigned int fcrc = p.kind == coopstore::kFileReadable ? coopstore::Crc32(p.bytes.empty() ? "" : &p.bytes[0], p.bytes.size()) : 0u;
    if (!text && (p.kind == coopstore::kFileAbsent || fcrc != icrc || (long long)p.bytes.size() != len))
    {   /* T-885 piece 2 fold round 2 (re-check fix 1 minor): <key>.rec is not the indexed copy - as the overlay's loader does, the indexed copy
           may be the kept <key>.rec.prev (a write that landed its file and not its index); sent from there. In neither file = 0 (it is gone). */
        const bool liveOther = p.kind == coopstore::kFileReadable;
        p = coopstore::ProbeFile(g_upDir + "\\" + coopown::PrevFile(key));
        if (p.kind != coopstore::kFileReadable && p.kind != coopstore::kFileAbsent) { ++g_upLocalUnread; return -1; }
        fcrc = p.kind == coopstore::kFileReadable ? coopstore::Crc32(p.bytes.empty() ? "" : &p.bytes[0], p.bytes.size()) : 0u;
        if (p.kind == coopstore::kFileAbsent || fcrc != icrc || (long long)p.bytes.size() != len) { if (liveOther) ++g_upLocalChanged; return 0; }
    }
    *w = text ? ownrecw::TextW(OwnSeqNow(), g_upSent, key) : seq;
    *crc = fcrc; *people = text ? 0u : (unsigned int)(chars < 0 ? 0 : chars);
    if (bytes != 0) bytes->swap(p.bytes);
    return 1;
}

/* 1 sent, 0 nothing to send, -1 try again later */
int SendOne(const std::string& key)
{
    const bool text = ownrecw::IsTextKey(key);
    ownrecw::Put m; m.store = g_upStore; m.key = key;
    const int lo = LocalOf(key, &m.w, &m.bytes, &m.crc, &m.people, true, &m.at);
    if (lo <= 0) return lo;
    if (!ownrecw::Unconfirmed(g_upSent, key, text, m.w, m.crc)) return 0;
    const std::pair<unsigned long long, unsigned int> ver(m.w, m.crc);
    std::map<std::string, std::pair<unsigned long long, unsigned int> >::const_iterator st = g_upSettled.find(key);
    if (st != g_upSettled.end() && st->second == ver) return 0;
    std::map<std::string, std::pair<unsigned long long, unsigned int> >::const_iterator inf = g_upFlight.inflight.find(key);
    if (inf != g_upFlight.inflight.end())
    {   /* review fold 2: one copy of a key on the wire at a time - a newer one waits for the answer, then is judged again */
        if (inf->second != ver) g_upFlight.waiting.insert(key);
        return 0;
    }
    if (m.bytes.size() > ownrecw::kMaxRecord)
    {
        ++g_upTooBig;
        ErrorLog("[OWNREC] " + key + " W=" + U(m.w) + " is " + N((long long)m.bytes.size()) + " bytes, over the 1 MB cap - not sent (counted tooBig)");
        g_upSettled[key] = ver;
        return 0;
    }
    m.len = (unsigned int)m.bytes.size();
    std::vector<char> b;
    if (!ownrecw::EncodePut(m, &b)) return 0;
    if (!StoreSendAdmin(ownrecw::kMsgOwnRec, b)) return -1;
    ownrecw::FlightSent(&g_upFlight, key, m.w, m.crc);
    ++g_upSentN;
    if (LineDue()) DebugLog("[OWNREC] sent " + key + " W=" + U(m.w) + " bytes=" + N((long long)m.len) + " crc=" + H8(m.crc) + " people=" + N((long long)m.people) + " sent=" + N(g_upSentN));
    return 1;
}

/* every key the store has, and the three text files */
void AllKeys(std::vector<std::string>* out)
{
    OwnStoreKeys(out);
    out->push_back("pp.left.notes"); out->push_back("pp.ledger"); out->push_back("pp.handover");
    std::set<std::string> seen; std::vector<std::string> u;
    for (size_t i = 0; i < out->size(); ++i) if (ownrecw::IsTravelKey((*out)[i]) && seen.insert((*out)[i]).second) u.push_back((*out)[i]);
    out->swap(u);
}

}   // namespace

void OwnUpOnWelcome()
{
    ownrecw::FlightNewLink(&g_upFlight);   /* a new link: nothing sent on the old one is waiting any more (the re-send judges every key again) */
    g_upResendDue = 1; ++g_upWelcomes;
}

/* the outbox follows the open records store (another store, or none: its own outbox) */
void UpSyncStore()
{
    const std::string dir = OwnStoreDir();
    if (dir != g_upDir)
    {   /* another store (or none): its own outbox */
        g_upDir = dir; g_upStore = Leaf(dir);
        g_upSent.clear(); g_upPending.clear(); ownrecw::FlightNewLink(&g_upFlight); g_upSettled.clear(); g_upRetry.clear();
        g_upSentState = -1; g_upUnreadableSaid = 0; g_upResendDue = 1;
        if (!dir.empty()) LoadSent();
    }
}

void OwnUpTick()
{
    std::vector<std::string> notes;
    OwnStoreUpNotes(&notes);   /* always drained */
    UpSyncStore();
    const std::string dir = g_upDir;
    if (dir.empty()) return;
    for (size_t i = 0; i < notes.size(); ++i) if (ownrecw::IsTravelKey(notes[i])) g_upPending.insert(notes[i]);
    if (g_upSentState == ownrecw::kSentUnreadable) { g_upPending.clear(); return; }
    if (g_upHold != 0) return;   /* TEST-ONLY hold: notes pile up in g_upPending */
    if (!OwnWorldLive() && !OwnTakeDoneFor(g_upStore, StoreNotebookLinkGen())) return;   /* T-885 piece 2: at the title nothing goes up on a link before its take has finished */
    if (!g_tkNoUpStore.empty() && g_tkNoUpStore == g_upStore) return;   /* fold round 2: its take was refused - nothing goes up until a take of it ends clean */
    if (StoreLinkWorldId().empty() || g_upStore != OwnAutoStoreWant()) return;   /* this link's world's own store only */
    const DWORD now = ::GetTickCount();
    for (std::map<std::string, DWORD>::iterator it = g_upRetry.begin(); it != g_upRetry.end();)
    {
        if ((DWORD)(now - it->second) >= 1000) { g_upPending.insert(it->first); g_upRetry.erase(it++); } else ++it;
    }
    if (g_upResendDue != 0)
    {
        g_upResendDue = 0;
        std::vector<std::string> keys; AllKeys(&keys);
        long long n = 0;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            unsigned long long w = 0; unsigned int crc = 0, people = 0;
            const bool text = ownrecw::IsTextKey(keys[i]);
            if (LocalOf(keys[i], &w, 0, &crc, &people, text) != 1) continue;
            if (!ownrecw::Unconfirmed(g_upSent, keys[i], text, w, crc)) continue;
            g_upPending.insert(keys[i]); ++n;
        }
        g_upResent += n;
        DebugLog("[OWNREC] outbox " + N(n) + " re-sent at WELCOME " + N(g_upWelcomes) + " (store " + g_upStore + ", " + N((long long)keys.size()) + " keys, "
                 + N((long long)g_upSent.size()) + " confirmed before" + (g_upSentState == ownrecw::kSentNone ? std::string(", no own.sent - the first upload of this store") : std::string()) + ")");
    }
    /* review fold 3: at most ownrecw::kWindow PUTs unanswered on this link - the next goes when an OK arrives; a key whose copy is in
       flight is left to that answer (SendOne marks it waiting when its bytes moved on) */
    for (std::set<std::string>::iterator it = g_upPending.begin(); it != g_upPending.end();)
    {
        const int may = ownrecw::MaySend(g_upFlight, *it);
        if (may == ownrecw::kWindowFull) break;
        const int r = SendOne(*it);
        if (r < 0) { g_upRetry[*it] = now; g_upPending.erase(it++); continue; }
        g_upPending.erase(it++);
    }
}

/* ======================= T-885 PIECE 2 "Take" (design docs/design-ownrec.md section 3) ======================= */
static bool TakeSend(const std::vector<char>& b)
{
    if (StoreSendAdmin(ownrecw::kMsgOwnRec, b)) return true;
    ++g_tkSendFailed; return false;
}
static std::string KeyList(const std::set<std::string>& s)
{
    std::string all; size_t n = 0;
    for (std::set<std::string>::const_iterator it = s.begin(); it != s.end(); ++it) { if (++n > 12) { all += " ..."; break; } all += (all.empty() ? "" : " ") + *it; }
    return all;
}
/* the take is over (any ending). Clean (an INDEX of status ok, every key taken): own.sent becomes the world's INDEX and the outbox re-judges
   every key. Fold round 2 (fail closed, ownrecw::TakeRefuses): an INDEX of status ok with any key not taken = the LOAD IS REFUSED (store.cpp's
   load gate, the older-records words), own.sent is left as it was and nothing of the store goes up until a take of it ends clean. No INDEX:
   nothing changes (the older-records check as before). The guard is told; the take is marked done for this link and store. */
static void TakeFinish()
{
    const bool ok = g_tk.idx.status == (unsigned int)ownrecw::kIdxOk;
    std::set<std::string> notTaken = g_tk.failed;
    notTaken.insert(g_tk.plan.unread.begin(), g_tk.plan.unread.end());
    const bool refused = ownrecw::TakeRefuses((int)g_tk.idx.status, notTaken.size());
    if (ok && !refused)
    {
        std::map<std::string, ownrecw::SentRow> next = g_upSent;
        const size_t changed = ownrecw::SentFromIndex(g_tk.idx.rows, &next);
        g_tkSentFixed += (long long)changed;
        g_upSent.swap(next); g_upSentState = ownrecw::kSentRead; g_upUnreadableSaid = 0;
        if (!WriteSent())
        {
            ++g_upSentWriteFail;
            ErrorLog("[OWNREC] take: own.sent of store " + g_upStore + " could not be written from the world's index - it holds in memory; at the next join the index corrects it again"
                     " (sentWriteFail=" + N(g_upSentWriteFail) + ")");
        }
        g_upResendDue = 1;   /* every key judged again against the corrected own.sent (the local-newer ones go up) */
        if (changed > 0) DebugLog("[OWNREC] take: own.sent corrected by the world's index - " + N((long long)changed) + " row(s) changed");
        if (!g_tkNoUpStore.empty() && g_tkNoUpStore == g_tk.store) { g_tkNoUpStore.clear(); DebugLog("[OWNREC] take: store " + g_tk.store + " was taken clean - its records go up again"); }
    }
    if (refused)
    {
        ++g_tkRefusedN; g_tkNoUpStore = g_tk.store;
        ErrorLog("[OWNREC] take: LOAD REFUSED - " + N((long long)notTaken.size()) + " key(s) the world holds a newer copy of were not taken after " + N((long long)g_tk.rounds)
                 + " round(s): " + KeyList(notTaken) + " - loading this PC's older copies would let the next save overwrite the world's newer ones. own.sent is left as it was"
                 " and nothing of store " + g_tk.store + " goes up until a take of it ends clean; the player's next load takes again (refused=" + N(g_tkRefusedN) + ")");
    }
    OwnTakeGuard(g_tk.store, !g_tk.taken.empty());
    g_tkLastMs = (long long)(DWORD)(::GetTickCount() - g_tk.startMs);
    if (ok)
    {
        DebugLog("[OWNREC] taken from the world: " + N((long long)g_tk.taken.size()) + " keys (local " + (g_tk.localHad == 0 ? std::string("none") : N((long long)g_tk.localHad) + " keys")
                 + ") - store " + g_tk.store + ", world rows " + N((long long)g_tk.idx.rows.size()) + ", same " + N((long long)g_tk.plan.same) + ", not taken " + N((long long)notTaken.size())
                 + ", local only " + N((long long)g_tk.plan.only.size()) + ", in " + N(g_tkLastMs) + " ms" + (refused ? std::string("; NOT every key taken - the load is refused")
                 : g_tk.idx.rows.empty() ? std::string("; the world holds nothing for this player - the older-records check stays as it was")
                 : std::string("; every key taken - the older-records check judges the folder as always")));
        if (!refused && !g_tk.plan.newer.empty()) DebugLog("[OWNREC] local newer: " + N((long long)g_tk.plan.newer.size()) + " keys - sent (unconfirmed writes; they win - owner 1745 a)");
    }
    g_tk.phase = kTkDone;
    g_tkDoneValid = true; g_tkDoneRefused = refused; g_tkDoneGen = g_tk.linkGen; g_tkDoneStore = g_tk.store;
}
static void TakeBegin(long linkGen, const std::string& want, unsigned int restarts)
{
    g_tk = TakeSt(); g_tk.token = ++g_tkToken; g_tk.linkGen = linkGen; g_tk.store = want; g_tk.startMs = g_tk.progressMs = ::GetTickCount(); g_tk.restarts = restarts;
    ownrecw::IndexAsk a; a.token = g_tk.token; a.store = want;
    std::vector<char> b;
    if (!ownrecw::EncodeIndexAsk(a, &b) || !TakeSend(b))
    {
        g_tk.phase = kTkNone;
        if (g_tkSendFailed <= 8) DebugLog("[OWNREC] take: the index ask could not be sent - the load stays held and it is asked again (sendFailed=" + N(g_tkSendFailed) + ", first 8 logged)");
        return;
    }
    g_tk.phase = kTkAsked; ++g_tkTakes;
    DebugLog("[OWNREC] take: the load is held - asking the world server what it holds for store " + want + " (token " + N((long long)g_tk.token) + ", takes=" + N(g_tkTakes)
             + (restarts != 0 ? ", restart " + N((long long)restarts) + " of " + N((long long)ownrecw::kTakeRestarts) : std::string()) + ")");
}
/* review fold (finding 4): the exchange cannot go on (nothing heard on the link for kTakeStallMs, a malformed answer, a GET that could not be
   sent) - begun again with a new exchange number at most kTakeRestarts times, then the take ends: the keys not fetched count as not taken, so
   the load is REFUSED (fold round 2, TakeFinish); no INDEX = nothing changes and the load goes with the older-records check as before */
static void TakeRestartOrRelease(const std::string& why)
{
    if (ownrecw::TakeTroubleStep(g_tk.restarts) == ownrecw::kTakeRestart)
    {
        ++g_tkRestarted;
        ErrorLog("[OWNREC] take: " + why + " - the take begins again with a new exchange number (restart " + N((long long)g_tk.restarts + 1) + " of " + N((long long)ownrecw::kTakeRestarts)
                 + ", restarted=" + N(g_tkRestarted) + ")");
        const long linkGen = g_tk.linkGen; const std::string store = g_tk.store; const unsigned int restarts = g_tk.restarts + 1;
        TakeBegin(linkGen, store, restarts);
        return;
    }
    ++g_tkReleased;
    const bool noIndex = g_tk.phase == kTkAsked || g_tk.phase == kTkNone;
    if (noIndex) { g_tk.idx = ownrecw::IndexMsg(); g_tk.idx.status = ownrecw::kIdxNoAnswer; }
    else if (g_tk.phase == kTkFetch)
    {
        for (size_t i = 0; i < g_tk.batch.size(); ++i) if (g_tk.batchGot.count(g_tk.batch[i]) == 0 && g_tk.failed.insert(g_tk.batch[i]).second) ++g_tkFailed;
        for (size_t i = g_tk.next; i < g_tk.plan.get.size(); ++i) if (g_tk.failed.insert(g_tk.plan.get[i]).second) ++g_tkFailed;
    }
    g_tk.rounds = ownrecw::kTakeFetchRounds;   /* no further round */
    g_tk.phase = kTkWrite; g_tk.progressMs = ::GetTickCount();
    ErrorLog("[OWNREC] take: " + why + " after " + N((long long)g_tk.restarts) + " restart(s) - the take ends"
             + (noIndex ? std::string(": nothing was taken and the load goes with this PC's own copies; the older-records check runs as before")
                        : ": " + N((long long)g_tk.failed.size()) + " key(s) not fetched count as not taken (the load is refused if any)") + " (released=" + N(g_tkReleased) + ")");
}
/* the next GET, or the end of the fetch */
static void TakeAskNext()
{
    g_tk.progressMs = ::GetTickCount();
    if (g_tk.next >= g_tk.plan.get.size()) { g_tk.phase = kTkWrite; return; }
    ownrecw::GetMsg g; g.token = g_tk.token;
    g_tk.next = ownrecw::NextBatch(g_tk.plan.get, g_tk.next, g_tk.idx.rows, &g.keys);
    g_tk.batch = g.keys; g_tk.batchGot.clear();
    g_tk.phase = kTkFetch;
    std::vector<char> b;
    if (!ownrecw::EncodeGet(g, &b) || !TakeSend(b)) TakeRestartOrRelease("a GET could not be sent (sendFailed=" + N(g_tkSendFailed) + ")");
}
static void TakeOnIndex(const ownrecw::IndexMsg& m)
{
    if (g_tk.phase != kTkAsked || m.token != g_tk.token) { ++g_tkStale; return; }
    g_tk.idx = m; g_tk.progressMs = ::GetTickCount();
    if (m.status != (unsigned int)ownrecw::kIdxOk)
    {
        ++g_tkStatusBad;
        ErrorLog("[OWNREC] take: the world server offers nothing for store " + g_tk.store + " (" + ownrecw::IdxStatusName((int)m.status) + ") - this game's own copies are used and the"
                 " older-records check stays as it was (statusNotOk=" + N(g_tkStatusBad) + ")");
        g_tk.phase = kTkWrite;
        return;
    }
    g_tkRows += (long long)m.rows.size();
    /* every key either side holds. Review fold (finding 1): an indexed record is judged by the store's committed index entry (W, CRC); its file
       is read only to tell whether it is still that copy (ownrecw::TakeDecide). A text file is its own local state. */
    std::vector<std::string> keys; AllKeys(&keys);
    for (std::map<std::string, ownrecw::Held>::const_iterator it = m.rows.begin(); it != m.rows.end(); ++it) keys.push_back(it->first);
    std::map<std::string, ownrecw::LocalVer> local;
    std::vector<std::string> unreadNow;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const std::string& k = keys[i];
        if (local.count(k) != 0) continue;
        ownrecw::LocalVer v; unsigned int people = 0;
        if (ownrecw::IsTextKey(k))
        {
            const int lo = LocalOf(k, &v.w, 0, &v.crc, &people, true);
            v.have = lo != 0; v.file = lo;
        }
        else
        {
            v.have = LocalOf(k, &v.w, 0, &v.crc, &people, false) == 1;
            if (v.have) { unsigned long long fw = 0; unsigned int fc = 0, fp = 0; v.file = LocalOf(k, &fw, 0, &fc, &fp, true); }
        }
        if (v.have && v.file < 0) unreadNow.push_back(k);
        local[k] = v;
        if (v.have) ++g_tk.localHad;
    }
    const std::map<std::string, ownrecw::SentRow> none;
    const std::map<std::string, ownrecw::SentRow>& sentBefore = g_upSentState == ownrecw::kSentRead ? g_upSent : none;
    ownrecw::PlanTake(local, m.rows, sentBefore, &g_tk.plan);
    g_tkSame += (long long)g_tk.plan.same; g_tkNewer += (long long)g_tk.plan.newer.size(); g_tkOnly += (long long)g_tk.plan.only.size(); g_tkUnread += (long long)g_tk.plan.unread.size();
    for (size_t i = 0; i < unreadNow.size(); ++i)
    {
        const std::string& k = unreadNow[i];
        std::map<std::string, ownrecw::Held>::const_iterator sv = m.rows.find(k);
        std::map<std::string, ownrecw::SentRow>::const_iterator sr = sentBefore.find(k);
        const int t = ownrecw::TakeDecide(local[k], sv != m.rows.end() ? sv->second : ownrecw::Held(), ownrecw::IsTextKey(k), sr != sentBefore.end() ? &sr->second : 0);
        ErrorLog("[OWNREC] take: " + k + " - its file could not be read just now (held by another program?) - " + (ownrecw::IsTextKey(k)
                 ? std::string("its local state cannot be settled, so this load is refused (fold round 2); the next load judges it again")
                 : "judged from its committed index entry W=" + U(local[k].w) + ": " + ownrecw::TakeName(t)));
    }
    OwnTakeRaise(ownrecw::CounterAfterTake(OwnSeqNow(), m.rows));   /* step 5: every later record numbered above the world's highest W */
    DebugLog("[OWNREC] take: the world holds " + N((long long)m.rows.size()) + " keys for store " + g_tk.store + " (highest W " + U(ownrecw::MaxW(m.rows)) + "); here " + N((long long)g_tk.localHad)
             + " - fetching " + N((long long)g_tk.plan.get.size()) + ", same " + N((long long)g_tk.plan.same) + ", local newer " + N((long long)g_tk.plan.newer.size()) + ", local only " + N((long long)g_tk.plan.only.size())
             + ", kept unreadable " + N((long long)g_tk.plan.unread.size()));
    for (size_t i = 0; i < g_tk.plan.get.size() && i < 40; ++i)
    {
        const std::string& k = g_tk.plan.get[i];
        const ownrecw::LocalVer& v = local[k]; const ownrecw::Held& h = g_tk.idx.rows[k];
        DebugLog("[OWNREC] take: fetch " + k + " world W=" + U(h.w) + " R=" + U(h.r) + " crc=" + H8(h.crc) + " here " + (v.have ? "W=" + U(v.w) + " crc=" + H8(v.crc) + (v.file == 0 ? std::string(" (its file is not that copy)") : std::string()) : std::string("none")));
    }
    g_tk.next = 0;
    TakeAskNext();
}
static void TakeOnRec(const ownrecw::RecMsg& m)
{
    if (g_tk.phase != kTkFetch || m.token != g_tk.token) { ++g_tkStale; return; }
    bool inBatch = false;
    for (size_t i = 0; i < g_tk.batch.size(); ++i) if (g_tk.batch[i] == m.key) { inBatch = true; break; }
    if (!inBatch || !g_tk.batchGot.insert(m.key).second) { ++g_tkStale; return; }
    g_tk.progressMs = ::GetTickCount();
    const ownrecw::Held& named = g_tk.idx.rows[m.key];
    const int why = ownrecw::RecAccept(named, m);
    if (why != ownrecw::kRecTake)
    {
        g_tk.failed.insert(m.key); ++g_tkFailed;
        ErrorLog("[OWNREC] take: " + m.key + " W=" + U(named.w) + " NOT taken (" + ownrecw::RecWhyName(why) + ") - asked again, up to " + N((long long)ownrecw::kTakeFetchRounds) + " rounds (failed=" + N(g_tkFailed) + ")");
        return;
    }
    if (ownrecw::IsTextKey(m.key))
    {
        if (!OwnTextReplace(m.key, std::string(m.bytes.begin(), m.bytes.end())))
        {
            g_tk.failed.insert(m.key); ++g_tkFailed;
            ErrorLog("[OWNREC] take: " + m.key + " could not be written into the records folder - asked again, up to " + N((long long)ownrecw::kTakeFetchRounds) + " rounds (failed=" + N(g_tkFailed) + ")");
            return;
        }
    }
    else OwnStoreEnqueue(m.key, m.bytes, named.crc, named.w, (long long)named.at, (int)named.people);   /* step 4: a committed record with the world's W and its own write time (review fold, finding 5: the overlay's save-time rule compares the real write time), by the records writer thread */
    g_tk.taken.push_back(m.key); ++g_tkGot;
}
static void TakeOnEnd(const ownrecw::EndMsg& m)
{
    if (g_tk.phase != kTkFetch || m.token != g_tk.token) { ++g_tkStale; return; }
    if (m.status != (unsigned int)ownrecw::kEndOk)
        ErrorLog("[OWNREC] take: the world server ended a GET with status " + std::string(ownrecw::EndStatusName((int)m.status)) + " - its keys count as not fetched this round");
    for (size_t i = 0; i < g_tk.batch.size(); ++i)
    {
        if (g_tk.batchGot.count(g_tk.batch[i]) != 0) continue;
        g_tk.failed.insert(g_tk.batch[i]); ++g_tkFailed;
        ErrorLog("[OWNREC] take: " + g_tk.batch[i] + " - the world server's answer ended without it - asked again, up to " + N((long long)ownrecw::kTakeFetchRounds) + " rounds (failed=" + N(g_tkFailed) + ")");
    }
    TakeAskNext();
}
void OwnTakeOnAnswer(unsigned int kind, const std::vector<char>& payload)
{
    const char* p = payload.empty() ? 0 : &payload[0];
    if (kind == (unsigned int)ownrecw::kIndex) { ownrecw::IndexMsg m; if (ownrecw::DecodeIndex(p, payload.size(), &m)) { TakeOnIndex(m); return; } }
    else if (kind == (unsigned int)ownrecw::kRec) { ownrecw::RecMsg m; if (ownrecw::DecodeRec(p, payload.size(), &m)) { TakeOnRec(m); return; } }
    else { ownrecw::EndMsg m; if (ownrecw::DecodeEnd(p, payload.size(), &m)) { TakeOnEnd(m); return; } }
    ++g_tkMalformed;
    ErrorLog("[OWNREC] take: a malformed answer (kind " + N((long long)kind) + ", " + N((long long)payload.size()) + " bytes) - ignored (malformed=" + N(g_tkMalformed) + ")");
    if (g_tk.phase == kTkAsked || g_tk.phase == kTkFetch) TakeRestartOrRelease("a malformed answer from the world server");   /* review fold (finding 4): bounded */
}
bool OwnTakeDoneFor(const std::string& store, long linkGen)
{
    return g_tkDoneValid && g_tkDoneGen == linkGen && !store.empty() && g_tkDoneStore == store;
}
/* fold round 2: the take on this link for this store ended refused - the load gate refuses the load (store.cpp WorldKeyLoadVerdict) */
bool OwnTakeRefusedFor(const std::string& store, long linkGen)
{
    return g_tkDoneRefused && OwnTakeDoneFor(store, linkGen);
}
/* fold round 2: the load request went away (refused or not) - a refused take is forgotten, so the next load takes again */
void OwnTakeLoadGone()
{
    if (!g_tkDoneValid || !g_tkDoneRefused) return;
    g_tkDoneValid = false; g_tkDoneRefused = false;
    DebugLog("[OWNREC] take: the refused load's request is gone - the next load takes again (store " + g_tkDoneStore + ")");
}
/* fold round 2: PumpLink, a message arrived on the world server's link - progress for the take's stall timer */
void OwnTakeHeard()
{
    g_tkHeardMs = ::GetTickCount(); g_tkHeard = true;
}
int OwnTakeHold(long linkGen)
{
    std::string want;
    const int ready = OwnTakeStoreReady(&want);
    if (ready == 0) return 0;   /* nothing to take: records off, no profile folder, or a test's store */
    if (OwnTakeDoneFor(want, linkGen)) return 0;
    if (ready < 0)
    {
        ++g_tkNotOpen;
        ErrorLog("[OWNREC] take: the records store '" + want + "' did not open - nothing is taken from the world server; the load goes as it would (notOpen=" + N(g_tkNotOpen) + ")");
        g_tk = TakeSt(); g_tk.phase = kTkDone; g_tk.linkGen = linkGen; g_tk.store = want; g_tk.idx.status = ownrecw::kIdxProfile;
        g_tkDoneValid = true; g_tkDoneGen = linkGen; g_tkDoneStore = want;
        return 0;
    }
    UpSyncStore();
    if (g_tk.phase == kTkNone || g_tk.phase == kTkDone || g_tk.linkGen != linkGen || g_tk.store != want)
    {   /* a new take: on this link, for this store (one begun again after an ask that could not be sent keeps its restart count) */
        const unsigned int keep = (g_tk.phase == kTkNone && g_tk.linkGen == linkGen && g_tk.store == want) ? g_tk.restarts : 0u;
        TakeBegin(linkGen, want, keep);
        return 1;
    }
    if (g_tk.phase == kTkAsked || g_tk.phase == kTkFetch)
    {   /* review fold (finding 4): the world server's answers are still arriving - unless nothing came for kTakeStallMs; fold round 2: ANY
           message heard on the link counts (a busy server answers the take only after its queued writes) */
        const DWORD nowMs = ::GetTickCount();
        const unsigned int since = ownrecw::TakeQuietMs((unsigned int)nowMs, (unsigned int)g_tk.progressMs, (unsigned int)(g_tkHeard ? g_tkHeardMs : g_tk.progressMs));
        if (!ownrecw::TakeStalled(since)) return 1;
        TakeRestartOrRelease("nothing heard from the world server for " + N((long long)since / 1000) + " s");
        if (g_tk.phase != kTkWrite) return 1;
    }
    if (g_tk.phase != kTkWrite) return 1;
    OwnStoreStat d; OwnStoreStats(&d);
    if (d.queued != 0 || d.busy != 0)
    {   /* step 4: the records writer has not committed every fetched record yet; past kTakeStallMs the ones not committed count as not taken */
        if (!ownrecw::TakeStalled((unsigned int)(DWORD)(::GetTickCount() - g_tk.progressMs))) return 1;
        ErrorLog("[OWNREC] take: the records writer has not committed the fetched records in " + N((long long)ownrecw::kTakeStallMs / 1000) + " s - the ones not committed count as not taken");
    }
    for (size_t i = 0; i < g_tk.taken.size(); ++i)
    {   /* each fetched indexed record must now be the committed one */
        const std::string& k = g_tk.taken[i];
        if (ownrecw::IsTextKey(k)) continue;
        const ownrecw::Held& h = g_tk.idx.rows[k];
        unsigned long long seq = 0; long long len = 0; unsigned int crc = 0; int chars = 0;
        if (OwnStoreCommittedLine(k, &seq, &len, &crc, &chars) && seq == h.w && crc == h.crc) continue;
        g_tk.failed.insert(k); ++g_tkFailed; --g_tkGot;
        ErrorLog("[OWNREC] take: " + k + " W=" + U(h.w) + " was fetched but the records writer did not commit it (committed seq=" + U(seq) + ") (failed=" + N(g_tkFailed) + ")");
    }
    std::vector<std::string> kept;
    for (size_t i = 0; i < g_tk.taken.size(); ++i) if (g_tk.failed.count(g_tk.taken[i]) == 0) kept.push_back(g_tk.taken[i]);
    g_tk.taken.swap(kept);
    ++g_tk.rounds;
    if (ownrecw::TakeFetchAgain(g_tk.failed.size(), g_tk.rounds))
    {   /* review fold (finding 3 a): a key not taken is asked again before the load is released (a new exchange number) */
        ++g_tkRefetch;
        DebugLog("[OWNREC] take: " + N((long long)g_tk.failed.size()) + " key(s) not taken - asked again (round " + N((long long)g_tk.rounds + 1) + " of " + N((long long)ownrecw::kTakeFetchRounds)
                 + ", token " + N((long long)g_tkToken + 1) + "): " + KeyList(g_tk.failed));
        g_tk.plan.get.assign(g_tk.failed.begin(), g_tk.failed.end()); g_tk.failed.clear(); g_tk.next = 0; g_tk.token = ++g_tkToken;
        TakeAskNext();
        return 1;
    }
    TakeFinish();
    return 0;
}

void OwnUpOnOk(const std::vector<char>& payload)
{
    {   /* T-885 piece 2: the take's answers */
        const unsigned int kind = ownrecw::KindOf(payload.empty() ? 0 : &payload[0], payload.size());
        if (kind == (unsigned int)ownrecw::kIndex || kind == (unsigned int)ownrecw::kRec || kind == (unsigned int)ownrecw::kEnd) { OwnTakeOnAnswer(kind, payload); return; }
    }
    ownrecw::Ok ok;
    if (!ownrecw::DecodeOk(payload.empty() ? 0 : &payload[0], payload.size(), &ok))
    {
        ++g_upOkMalformed;
        ErrorLog("[OWNREC] a malformed OWNREC answer of " + N((long long)payload.size()) + " bytes - ignored (okMalformed=" + N(g_upOkMalformed) + ")");
        return;
    }
    unsigned int crc = 0; bool wake = false;
    const bool mine = ownrecw::FlightAnswer(&g_upFlight, ok.key, ok.w, &crc, &wake);   /* review fold 2: the one copy of this key in flight - its CRC, never a later copy's */
    if (!mine) ++g_upOkNotInFlight;
    if (wake) g_upPending.insert(ok.key);   /* a newer copy waited behind this one: judged again now (sent if it still differs) */
    const std::string head = "[OWNREC] " + std::string(ownrecw::VerdictName((int)ok.verdict)) + " " + ok.key + " W=" + U(ok.w) + " R=" + U(ok.r);
    if (ok.verdict == ownrecw::kStored || ok.verdict == ownrecw::kDuplicate)
    {
        if (ok.verdict == ownrecw::kStored) ++g_upConfirmed; else ++g_upDuplicate;
        if (!mine) { if (LineDue()) DebugLog(head + " - not the copy in flight on this link (an earlier send); the record is judged again at the next WELCOME"); return; }
        if (ownrecw::ApplyOk(&g_upSent, ok, crc) && !WriteSent())
        {
            ++g_upSentWriteFail;
            ErrorLog("[OWNREC] own.sent of store " + g_upStore + " could not be written - the confirmation holds in memory; at the next start the record is sent again"
                     " and answered duplicate (sentWriteFail=" + N(g_upSentWriteFail) + ")");
        }
        if (LineDue()) DebugLog(head + " confirmed=" + N(g_upConfirmed) + " duplicate=" + N(g_upDuplicate));
        return;
    }
    if (mine) g_upSettled[ok.key] = std::make_pair(ok.w, crc);   /* not sent again this session; judged again at the next start */
    if (ok.verdict == ownrecw::kSuperseded)
    {
        ++g_upSuperseded;
        if (LineDue()) DebugLog(head + " - the world holds a higher W for this key; not sent again this session (superseded=" + N(g_upSuperseded) + ")");
        return;
    }
    ++g_upRefused;
    ErrorLog(head + " why=" + ownrecw::WhyName((int)ok.why) + " - the world server did not keep it; not sent again this session (refused=" + N(g_upRefused) + ")");
}

std::string OwnUpReportToken()
{
    return " ownrec[sent,confirmed,duplicate,superseded,refused]=" + N(g_upSentN) + "," + N(g_upConfirmed) + "," + N(g_upDuplicate) + "," + N(g_upSuperseded) + "," + N(g_upRefused)
         + " ownrecx[resent,pending,inflight,localUnread,localChanged,tooBig,okMalformed,okNotInFlight,sentWriteFail,hold,sentFile,confirmedKeys]=" + N(g_upResent)
         + "," + N((long long)(g_upPending.size() + g_upRetry.size())) + "," + N((long long)g_upFlight.inflight.size()) + "," + N(g_upLocalUnread) + "," + N(g_upLocalChanged)
         + "," + N(g_upTooBig) + "," + N(g_upOkMalformed) + "," + N(g_upOkNotInFlight) + "," + N(g_upSentWriteFail) + "," + N((long long)g_upHold)
         + "," + (g_upSentState == ownrecw::kSentNone ? "none" : g_upSentState == ownrecw::kSentRead ? "read" : g_upSentState == ownrecw::kSentUnreadable ? "UNREADABLE" : "notYet")
         + "," + N((long long)g_upSent.size())
         + " ownrecwin[cap,high,waiting]=" + N((long long)ownrecw::kWindow) + "," + N((long long)g_upFlight.high) + "," + N((long long)g_upFlight.waiting.size())   /* review fold 3: the window and its high-water */
         + " ownrectake[takes,rows,got,same,localNewer,localOnly,failed,stale,malformed,statusNotOk,sentFixed,sendFailed,notOpen,lastMs,phase,restarted,released,refetchRounds,unreadKept,refused]=" + N(g_tkTakes) + "," + N(g_tkRows) + "," + N(g_tkGot)
         + "," + N(g_tkSame) + "," + N(g_tkNewer) + "," + N(g_tkOnly) + "," + N(g_tkFailed) + "," + N(g_tkStale) + "," + N(g_tkMalformed) + "," + N(g_tkStatusBad) + "," + N(g_tkSentFixed)
         + "," + N(g_tkSendFailed) + "," + N(g_tkNotOpen) + "," + N(g_tkLastMs) + "," + N((long long)g_tk.phase)
         + "," + N(g_tkRestarted) + "," + N(g_tkReleased) + "," + N(g_tkRefetch) + "," + N(g_tkUnread) + "," + N(g_tkRefusedN);   /* T-885 piece 2 (+ review fold) */
}

/* TEST-ONLY: `ownrec compare` | `ownrec hold on|off` */
int OwnUpCommand(const std::string& argIn, std::string* status)
{
    std::string arg = argIn;
    while (!arg.empty() && arg[arg.size() - 1] == ' ') arg.erase(arg.size() - 1);
    if (arg == "hold on" || arg == "hold off")
    {
        g_upHold = arg == "hold on" ? 1 : 0;
        DebugLog(std::string("[OWNREC] TEST-ONLY hold ") + (g_upHold ? "ON: nothing is sent to the world server; local writes go on (" : "OFF: what waited is sent now (")
                 + N((long long)(g_upPending.size() + g_upRetry.size())) + " waiting)");
        *status = std::string("ok ownrec hold ") + (g_upHold ? "on" : "off");
        return 1;
    }
    if (arg == "compare")
    {
        if (g_upDir.empty()) { *status = "error ownrec compare no-store"; DebugLog("[OWNREC] compare: no records store is open"); return 0; }
        std::vector<std::string> keys; AllKeys(&keys);
        long long equal = 0, unequal = 0, none = 0, flying = 0;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const std::string& k = keys[i];
            const bool text = ownrecw::IsTextKey(k);
            unsigned long long w = 0; unsigned int crc = 0, people = 0;
            const int lo = LocalOf(k, &w, 0, &crc, &people, text);
            if (lo != 1) { if (lo == 0 && text) continue; ++none; DebugLog("[OWNREC] compare " + k + " local=unread"); continue; }
            std::map<std::string, ownrecw::SentRow>::const_iterator s = g_upSent.find(k);
            const bool eq = !ownrecw::Unconfirmed(g_upSent, k, text, w, crc);
            const bool inFlight = !eq && g_upFlight.inflight.count(k) != 0;   /* sent on this link, its answer not here yet: not a disagreement */
            if (eq) ++equal; else if (inFlight) ++flying; else ++unequal;
            std::map<std::string, std::pair<unsigned long long, unsigned int> >::const_iterator st = g_upSettled.find(k);
            DebugLog("[OWNREC] compare " + k + " local W=" + U(w) + (text ? " crc=" + H8(crc) : std::string()) + " server "
                     + (s == g_upSent.end() ? std::string("none") : "W=" + U(s->second.w) + " R=" + U(s->second.r) + (text ? " crc=" + H8(s->second.crc) : std::string()))
                     + (eq ? " EQUAL" : inFlight ? " IN-FLIGHT" : " UNEQUAL") + (st != g_upSettled.end() ? " (answered superseded / refused this session)" : std::string()));
        }
        DebugLog("[OWNREC] compare: store " + g_upStore + " keys=" + N(equal + unequal + none + flying) + " equal=" + N(equal) + " unequal=" + N(unequal) + " inFlight=" + N(flying) + " unread=" + N(none)
                 + OwnUpReportToken());
        *status = "ok ownrec compare keys=" + N(equal + unequal + none + flying) + " equal=" + N(equal) + " unequal=" + N(unequal) + " inFlight=" + N(flying) + " refused=" + N(g_upRefused);
        return 1;
    }
    *status = "error ownrec usage: ownrec compare | ownrec hold on|off";
    return 0;
}

}   // namespace coop
