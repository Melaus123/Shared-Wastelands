/* src/common/writerladder.h - B9. WHICH GAME WRITES THIS BOX OR THIS SHOP, when the notebook process
 * cannot answer. PURE: no engine, no locks, no globals, no OS header. Compiled into the plugin and into
 * src/coop-test's offline exe, which is the whole point - the rule that decides a container's writer is
 * tested without a game, and the plugin and the test cannot hold two ideas of it (clockmath.cpp and
 * storemeta.cpp are here for the same reason).
 *
 * WHAT IT REPLACES (build/read-host-audit.md C7, retired by decision 48). The rule was "with a stale map,
 * no notebook, or no relay, THE HOST writes every box and every shop in the world". That is a standing
 * authority assignment and not a tie-break, and it was wrong in the ordinary case: the host cannot even
 * resolve a key for a box only the other player has loaded, so the other player's chest move was reverted,
 * requested, and refused - measured as failing for the rest of the session (P7g).
 *
 * THE LADDER (build/design-e46-store.md 2.2). Read top to bottom; the FIRST rung that answers, answers.
 *
 *   R4  the notebook has been unreachable for longer than the grace  -> REFUSE, out loud.
 *       Checked FIRST, and it costs the player's move. Stated plainly rather than argued away: past the
 *       grace R1-R3 would still pick exactly one writer, so refusing buys nothing except compliance with
 *       decision 44 (a co-op world does not run without its notebook). It is built because decision 44 is
 *       a user decision and a silent unsaved world is worse than a refused chest - but the harm is real,
 *       the banner is what makes it honest, and the grace is one named constant so it can be moved after
 *       one run.
 *   R1  does THIS game have the box's own point loaded right now?  no -> HELD (revert and request: the
 *       other game has it, or nobody does). This is the LIVE engine read the box road already performs one
 *       call earlier and threw away.
 *   R2  did every other player that has it loaded report, and did none of them list this sector?  yes -> MINE. This game
 *       (M9 (T-197): the notebook's effective map is 256 seats wide - ANY other seat's bit is "another player has it")
 *       is the only one that has the box at all. M2 (decisions 32/44/54): "report" is the notebook's area
 *       map (PeerSectorLoadedTS) - the session-link MSG_ZONES report is gone, so with no notebook R2 is -1.
 *   R3  both may have it (or the other game has not reported) -> the LOWER NOTEBOOK SLOT writes.
 *       With no slot comparison (slotDecided == 0: no slot of my own, or no fresh notebook table) there is
 *       no shared ordering and the ladder answers NO-ANSWER (kWrNoAnswer, rung kRungNoAnswer) - with the
 *       notebook LINKED. M2-b (review-m2 H1, decision 52): with the notebook DOWN (inside the grace) it
 *       answers REFUSE instead (kWrRefuse, rung kRungRefusedNoNotebook), so the write goes to the outage
 *       journal and is re-decided at relink rather than dropped.
 *       M2 (decisions 32/44/54; audit B14) REMOVED the session-role fallback that stood here and its
 *       registration under docs/authority-model.md section 11 A1: a co-op world does not run without its
 *       notebook, so the role had no world to serve. The caller treats NO-ANSWER as its no-fresh-map answer.
 *
 * THE RESOLUTION ASYMMETRY IS STATED, NOT HIDDEN, AND THE BOUND IS WIDER THAN ONE SECOND (B9-b, review-b9
 * M-5 - the wording below used to say "a player standing exactly on a sector line during that second",
 * which understates it). R1 is a POINT read taken NOW. R2 is a SECTOR read whose age is TWO terms added
 * together: up to one report round (M2: the peer's AREAS reaches the notebook at 1 Hz and the AREAMAP comes
 * back at 1 Hz, so a set can be up to two seconds old when it lands) PLUS PeerSectorLoadedTS's 5 s freshness
 * grace, which keeps answering from the last map for five seconds after it lands. So for up to about SEVEN SECONDS AFTER A
 * SECTOR LOADS OR UNLOADS on the other game, R2 can answer about a loaded set that game no longer has -
 * and in that window both games can reach kWrMine over the same box (this game by R2 "the peer does not
 * have it", the other by R1/R3 over its own live read). R3 does NOT bound that case; it only orders the
 * ties it is reached for. WHAT ACTUALLY BOUNDS IT IS THE RECEIVE SIDE: boxApplyNotHolder refuses an
 * arriving move on a box whose sector THIS game holds, so the disagreement costs one refused move rather
 * than two divergent chests. The residual is a move that is lost rather than applied, and the counter is
 * the measurement of how often the window is entered.
 */
#ifndef COOP_WRITERLADDER_H
#define COOP_WRITERLADDER_H

namespace coopwriter {

/* the answer */
enum { kWrMine = 1, kWrHeld = 2, kWrRefuse = 3,
       kWrNoAnswer = 4 };   /* M2 (decisions 32/44/54): R3 with no notebook slot comparison - nobody is named */

/* WHICH RUNG ANSWERED. One counter per rung at the call site, and the identity
 *     entered == r1NotLoadedHere + r2SoleLoaded + r3TieLower + r3TieHigher + r4Refused + r3NoAnswer
 * is what says a sixth exit was not added without a counter (F172's corollary). kRungCount is the array
 * bound the call site sizes its counters with, so adding a rung here fails to compile there rather than
 * silently dropping into no bucket. */
enum { kRungNone = 0, kRungNotLoadedHere = 1, kRungSoleLoaded = 2, kRungTieLower = 3,
       kRungTieHigher = 4, kRungRefusedNoNotebook = 5, kRungNoAnswer = 6 /* M2 */, kRungCount = 7 };

/* SPANS, NOT EXITS - they say HOW the answer was reached and they do NOT sum to `entered`. They are
 * reported in their own bracket with the word Spans in the field name, because a span printed beside an
 * identity is what broke L-1's readout. Returned as bit flags in *spansOut. */
enum { kSpanPeerUnknown = 1,   /* R2 could not answer; the tie-break decided instead */
       kSpanByRole      = 2,   /* R3 answered NO-ANSWER (no slot comparison, notebook linked). M2-b: no longer reported - the r3ByRole field is retired, it always equalled r3NoAnswer */
       kSpanGracePass   = 4 }; /* the notebook was down and the grace had NOT yet run out */

/* PURE. Every input is read by the caller in ONE pass; this function reads nothing.
 *   loadedHereLive  : 1 loaded here right now, anything else = not loaded (the caller resolves "could not
 *                     ask" to 0 before calling - refusing to publish costs a move the player can make
 *                     again, and publishing into an area we may not have is what T230 measured)
 *   peerSectorLoaded: 1 the other game reported this sector loaded, 0 it reported its set and this sector
 *                     was not in it, -1 no report inside the freshness window
 *   mySlotLower     : 1 my notebook slot is lower than every other slot beside this area, else 0
 *   slotDecided     : 1 mySlotLower came from a REAL NOTEBOOK SLOT COMPARISON, 0 it came from the session
 *                     role - exception A1, counted apart as the r3ByRole span. B9-b (review-b9 M-3): this
 *                     used to be named slotKnown and the call site passed "this game has a slot", which
 *                     is NOT the same question. MySlotLower falls back to the role on TWO inputs - no
 *                     slot of my own, AND no fresh relay table to compare against (peerRing1LowestSlot
 *                     == -2) - and the second one happens with a perfectly good slot, so the span read 0
 *                     for role decisions that were really taken. The parameter now asks what it is named
 *                     after and the caller answers it with MySlotLowerEx's own flag. M2: 0 makes R3 answer
 *                     NO-ANSWER (M2-b: REFUSE inside the grace); mySlotLower is then not read at all.
 *   notebookDownMs  : how long the notebook has been unreachable, 0 while it is linked. A negative value
 *                     is read as 0 ("not known to be down"), never as an outage.
 *   refuseAfterMs   : the grace. Negative disables R4 entirely (nothing in the build passes that; it
 *                     exists so a test can hold R4 out of the way while sweeping R1-R3).
 *   forBox          : 0 a shop restock, NON-ZERO a container the caller will presume for - 1 a storage
 *                     box, 2 a door (items.cpp's kAreaForDoor). This function reads only "is it zero",
 *                     so a door takes exactly the box's road through it; the value is carried through so
 *                     the CALLER can book the three families apart.
 *
 * THE forBox NARROWING IS KEPT AND GENERALISED (P7a). A presumed restock calls
 * Inventory::clearAll(destroy) on every counter and SETS the keeper's money - it WIPES a shop, where a
 * presumed box move moves one item. So where R2 cannot answer, a box falls to the tie-break and A RESTOCK
 * DOES NOT: it declines, and it declines at the tie-break rung because that is the rung it reached.
 * r3TieHigher therefore reads "the tie-break declined" - my slot was higher, OR this is a restock with no
 * peer answer - and the r2PeerUnknown span printed beside it is what tells the two apart.
 *
 * *rungOut is always written (kRungNone is never returned). *spansOut may be 0. */
int WriterLadder(int loadedHereLive, int peerSectorLoaded, int mySlotLower, int slotDecided,
                 long long notebookDownMs, long long refuseAfterMs, int forBox,
                 int* rungOut, int* spansOut = 0);

/* ============ B10 (audit C11, design-e46-store 3.4) - WHO WRITES THE ZONE FILE ====================
   WHAT STOOD HERE. store.cpp`s ZoneWriteIsMine opened with
       if ((g_sessionLinkedCached == 0) || net::SessionIsHost()) return true;
   - "no session, or I am the host, so I write EVERY zone file in the world". That is the same
   standing authority assignment decision 48 retired for boxes (audit C7), stated about a whole
   sector`s worth of buildings instead of one chest, and decision 40 already says the opposite: the
   area`s HOLDER writes its zone file. The host arm meant a client`s save of its own town was thrown
   away whenever the host was in a session at all, and the no-session arm is the state decision 44
   abolishes.
   WHAT REPLACES IT: the notebook`s own answer where it has one, and otherwise THE SAME LADDER a box
   takes. This function carries NO RUNG LOGIC - it forwards to WriterLadder exactly once - and it
   exists so that the two short-circuits above the ladder are swept offline with it, rather than
   living in a function no test can reach.
     roleIsSingle      : 1 a never-co-op save. Vanilla: this game writes its own world, unchanged.
     mine / held       : AreaViewTS`s answers for this zone`s sector - 1 the notebook named this game
                         the holder / named the other game, 0 it named neither, -1 no fresh map.
     loadedHereSector  : THIS IS A SECTOR FLAG, NOT A POINT READ, and that is the one difference from
                         items.cpp`s call: a zone file IS a sector, so R1`s resolution question of
                         design 2.3 does not arise here. AreaViewTS`s own `loadedHere`.
     the rest          : passed straight through to WriterLadder; see the rungs above. forBox is 1 -
                         a zone file is presumed for like a container, never declined like a restock.
   *pathOut says WHICH of the four roads was taken and the four are EXCLUSIVE, so they sum to the
   zone saves that reached this decision; *rungOut is kRungNone on every road but the ladder. */
enum { kZonePathSingle = 0,      /* a never-co-op save - vanilla, and no ladder is entered */
       kZonePathHolderMine = 1,  /* the notebook named this game the holder of the sector */
       kZonePathHolderOther = 2, /* the notebook named the other game */
       kZonePathLadder = 3,      /* nobody was named: the ladder decided */
       kZonePathCount = 4 };
int ZoneWriterDecide(int roleIsSingle, int held, int mine, int loadedHereSector,
                     int peerSectorLoaded, int mySlotLower, int slotDecided,
                     long long notebookDownMs, long long refuseAfterMs,
                     int* pathOut, int* rungOut, int* spansOut = 0);

/* ============ inv7d (e47-inv7-replan 6; inv4 phase P5) - THE OWNER'S ARRIVAL ASKS FOR ITS STORAGE ============
   While the owner was away the area's holder wrote the owner's boxes (the owner rung answered "absent -> area rule"), so the
   owner's own copy is stale when it comes back. The hand-back (items.cpp ItHandbackGate) asks the holder ONCE per arrival
   stamp; inv7d drives it from the ARRIVAL EDGE (events over timers) instead of waiting for some writer decision to reach the
   box. These three rules are the whole decision; the plugin only reads the inputs.
   OwnerArriveEdge: since = when this game's own bit appeared in the sector's notebook map (0 = clear); sweptSince = the stamp
   already swept for that sector (0 none); activePolls = the one-second polls the sector has been active here. A clear bit or a
   stamp already swept -> none; a new stamp on a sector not yet settled -> wait; else sweep (ask for every own box there). */
enum { kArriveNone = 0, kArriveWait = 1, kArriveSweep = 2 };
const int kArriveSettlePolls = 2;
int OwnerArriveEdge(double since, double sweptSince, int activePolls);
/* OwnerArriveWait: a hand-back in `state` (0 asked, 2 contents on the way, 1 done) after elapsedMs of waitMs. 1 = this game
   writes the box now (done, or the wait ran out - *timedOut = 1 only then, and the caller LOGS it: no silent loss), 0 = wait. */
int OwnerArriveWait(int state, unsigned long elapsedMs, unsigned long waitMs, int* timedOut);
/* OwnerArriveApplies: the holder's contents replace the owner's stale copy only while the hand-back waits (state 0 or 2). A
   done or timed-out hand-back keeps the owner's copy (the owner already writes it); such an answer is a LATE one, counted. */
int OwnerArriveApplies(int state);
/* inv7d fold (review-inv7d MED): SWAP OR RE-ASK. A contents answer (state 2) replaces the owner's copy only if the box is
   UNCHANGED SINCE THE ASK: askDigest = this game's own digest when it asked (askHave 0 = not recorded), liveDigest = the box
   read now (liveHave 0 = unreadable). A box that changed in between (a lone-stack merge, an engine change no hook saw) would
   lose or double that change under the swap - it is asked again instead. Anything not a waiting contents answer, or with a
   side unread, swaps as before. */
enum { kArriveSwapIt = 1, kArriveReask = 2 };
int OwnerArriveSwapOrReask(int state, int askHave, unsigned int askDigest, int liveHave, unsigned int liveDigest);

}   /* namespace coopwriter */

#endif
