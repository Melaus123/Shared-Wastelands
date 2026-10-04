// doors.cpp - E45 (P8e / B8).  See doors.h for the engine facts and the design; build/read-doors.md
// (F632) is the read they come from and F633 is the observation they explain.
#include "doors.h"
#include "addresses.h"   /* P8h: kXxxRva below is filled from the address table, not hard-coded */
#include "items.h"          /* ObjectPositionKey, AreaVerdictForDoor, ObjectKeyDoorCounts - the P7n key builder and decision 40's holder rule, CALLED and not copied (lesson 11) */
#include "store.h"          /* B12 (decision 52): StoreQueueWrite - a door change refused for want of a notebook is journaled */
#include "../common/queuejournal.h"   /* B12: coopqueue::kQueueFamilyDoor */
#include "store.h"          /* EngineWritesBlocked - the one predicate every engine write asks */
#include "net/session.h"    /* SendDoorState, SessionLinked */
#include "../common/doorsync.h"   /* P8n: the last-actor rule, the terminal-state test, the churn give-up and the first-sighting rule - PURE, and swept by src/coop-test/test_main.cpp */
#include "coop_log.h"
#include "hooks.h"   /* mig1: coop::AddHook (own MinHook) */
#include "zones.h"   /* T-160: NearestBuildingWhere - the `doortest nearest` lever */
#include <Windows.h>
#include <intrin.h>
#include <cstring>
#include <cstdio>
#include <string>

namespace coop {
namespace {

/* ============================ THE ENGINE, ALL OF IT IN ONE BLOCK ============================
   Every RVA and every offset below is build/read-doors.md's, and every prologue was read out of
   build/bin/kenshi_x64.exe for THIS build rather than taken from a header (F530: header RVAs are
   wrong for this binary).  A prologue that does not match at install means the address is not the
   function this file believes it is, and the hook is REFUSED with a line that says so (F038). */
unsigned long long kSetDoorOpenAmountRva = 0; static coop::AddrReg kSetDoorOpenAmountRva_reg("SetDoorOpenAmount", &kSetDoorOpenAmountRva);   /* P8h: the address table fills this. Steam_1.0.65 0x298CD0 */   /* the funnel: the only writer of +0x37C; node + physics + navmesh */
unsigned long long kOpenDoorRva = 0; static coop::AddrReg kOpenDoorRva_reg("OpenDoor", &kOpenDoorRva);   /* P8h: the address table fills this. Steam_1.0.65 0x297000 */   /* state = OPENING if CLOSED; 6 callers; no lock check */
unsigned long long kCloseDoorRva = 0; static coop::AddrReg kCloseDoorRva_reg("CloseDoor", &kCloseDoorRva);   /* P8h: the address table fills this. Steam_1.0.65 0x298C10 */   /* state = CLOSING if OPEN and not broken; 4 callers */
unsigned long long kSetDoorStateRva = 0; static coop::AddrReg kSetDoorStateRva_reg("SetDoorState", &kSetDoorStateRva);   /* P8h: the address table fills this. Steam_1.0.65 0x298FC0 */   /* THE SETTER: +0x380 = s, then setDoorOpenAmount((float)(s==OPEN), false) */
/* setupPhysicalUT 0x29CC50 forces a finished GATE to OPEN and then calls the funnel.  The call is at
   0x29D1CD, so the RETURN ADDRESS the funnel sees for the forced open is 0x29D1D2 (Confirmed from
   the shipped exe: the bytes at 0x29D198 are the isGate call, the build-state test,
   MOV [rbx+0x380],r14d and an E8 to the funnel's incremental-link thunk 0x481DA).  The function's
   other two calls into the funnel return to 0x29CCCD and 0x29D1EF. */
/* THE LOCK CALLS (Confirmed from the 1.0.65 bytes; doorsync.h names what each one does).  lockButton is
   hooked (the player's press) and called only by the doortest lever, never by the applier - its sound is
   posted on the fixed object 0x6E and heard everywhere.  Its DataPanelLine argument is never read - its body
   overwrites RDX before any read of it - so it is called with 0.  lockDoor and unlockDoor dereference DoorStuff+0x370 (the DoorLock) with no test, so
   they are called only on a door whose DoorLock pointer was read as non-zero. */
unsigned long long kLockDoorRva = 0; static coop::AddrReg kLockDoorRva_reg("LockDoor", &kLockDoorRva);   /* Steam_1.0.65 0x2969B0: locked = 1 if CLOSED and settled; wantsToLock = 1.  2 callers: its thunk and DoorStuff::update's re-lock when a closing door lands */
unsigned long long kUnlockDoorRva = 0; static coop::AddrReg kUnlockDoorRva_reg("UnlockDoor", &kUnlockDoorRva);   /* Steam_1.0.65 0x569940: locked = 0, gate code recomputed.  No caller in the image */
unsigned long long kUpdateGateCodeRva = 0; static coop::AddrReg kUpdateGateCodeRva_reg("UpdateGateCodeState", &kUpdateGateCodeRva);   /* Steam_1.0.65 0x297250: the door's gate code recomputed (the route-finding's view of its lock).  lockButton calls it after its lock; unlockDoor ends in it */
unsigned long long kLockButtonRva = 0; static coop::AddrReg kLockButtonRva_reg("LockButton", &kLockButtonRva);   /* Steam_1.0.65 0x5465D0: the player's lock button - sound, wantsToLock flipped, the lock follows it */
static unsigned long long kSetupForcedOpenRet = 0; static coop::AddrReg kSetupForcedOpenRet_reg("SetupForcedOpenRet", &kSetupForcedOpenRet);   /* stage 7/9: the address table fills this. Steam_1.0.65 0x29D1D2 */

const size_t kDoorState   = 0x380;   /* int  DoorState */
const size_t kDoorOpenAmt = 0x37C;   /* float 0.0 shut .. 1.0 open */
const size_t kDoorLockPtr = 0x370;   /* DoorLock* */
const size_t kDoorWants   = 0x384;   /* bool wantsToLock */
const size_t kDoorBroken  = 0x3EC;   /* bool _isBroken */
const size_t kLockLocked  = 0x20;    /* DoorLock::locked */
const size_t kVtGetPos    = 0x40;    /* RootObjectBase::getPosition - slot 8, three 4-byte copies out of +0x48 */
const size_t kVtIsGate    = 0x3D8;   /* Building::isGate  -> GatewayBuilding* or 0 */
/* THE DOOR'S OWN BUILDING, AS A PLAIN FIELD.  DoorStuff::isGate's whole body is
   `mov rcx,[rcx+0x368]; mov rax,[rcx]; jmp [rax+0x3D8]` (the bytes are quoted at DoorGatePod below),
   so +0x368 is the parent Building a door hangs on: a gate's door names its GatewayBuilding, a house
   door names its house.  Read, not Confirmed - and it is used for ONE thing only, as a second
   candidate for the active-zone walk test, where a wrong pointer can only fail to match. */
const size_t kDoorParent  = 0x368;
/* THE PARENT'S DOOR ARRAY - the other half of +0x368, and the only building -> door route this tree
   has.  `lektor<Building*> doors` at Building+0x1B8 is an empty allocator base, then count, maxSize
   and the element pointer.  Confirmed in the decompile by three independent walkers: Building::getDoor
   0xF7040 (count at +0x1C0, elements at +0x1C8), Building::hasAnOpenDoor 0x548F10 (the same loop,
   stride 8) and Building::destroyDoors 0x547DA0, which empties the count AND nulls each door's +0x368
   - that last one is what makes the parent round trip in DoorResolveByKey a real test.  The element
   type is Building* and the engine converts it with the virtual +0x3C8; THIS ROAD NEVER MAKES THAT
   CALL - it treats an element as a candidate door and proves it with DoorPlausible and DoorReadPod. */
const size_t kBldDoorsCount = 0x1C0;   /* uint32 count */
const size_t kBldDoorsStuff = 0x1C8;   /* Building** stuff, stride 8 */
/* AN ABSURDITY CLAMP, NOT A BUDGET.  lektor push_back (0xDA030) REALLOCATES the element pointer when
   the array grows, so a count read off an array that is growing can be anything at all; a building
   claiming more doors than this is refused whole rather than read. */
const int kDoorArrayMax = 256;

const unsigned char kPrologueFunnel[8]    = { 0x40, 0x56, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83 };
const unsigned char kPrologueOpenDoor[8]  = { 0x48, 0x83, 0xEC, 0x48, 0x83, 0xB9, 0x80, 0x03 };
const unsigned char kPrologueCloseDoor[8] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B };
const unsigned char kPrologueSetState[8]  = { 0x33, 0xC0, 0x83, 0xFA, 0x01, 0x89, 0x91, 0x80 };
const unsigned char kPrologueLockDoor[8]   = { 0x83, 0xB9, 0x80, 0x03, 0x00, 0x00, 0x00, 0x75 };
const unsigned char kPrologueUnlockDoor[8] = { 0x48, 0x8B, 0x81, 0x70, 0x03, 0x00, 0x00, 0xC6 };
const unsigned char kPrologueLockButton[8] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 };
const unsigned char kPrologueUpdateGateCode[9] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 };

typedef void          (*SetDoorOpenAmountFn)(void* door, float amount, unsigned char force);
typedef unsigned char (*OpenCloseDoorFn)(void* door);
typedef void          (*SetDoorStateFn)(void* door, int state);
typedef void*         (*GetPositionFn)(void* obj, float* out);
typedef void*         (*IsGateFn)(void* obj);
typedef void          (*LockDoorFn)(void* door);
typedef void          (*LockButtonFn)(void* door, void* panelLine);

SetDoorOpenAmountFn orig_setDoorOpenAmount = 0;
OpenCloseDoorFn     orig_openDoor = 0;
OpenCloseDoorFn     orig_closeDoor = 0;
SetDoorStateFn      g_setDoorState = 0;   /* CALLED, never hooked - the applier */
LockDoorFn          orig_lockDoor = 0;
LockButtonFn        orig_lockButton = 0;
/* THE ENGINE ENTRY POINTS THE APPLIER CALLS (g_lockButton: the doortest lever only), each set only when its
   prologue matched at install.  They
   are the ENTRIES and not the trampolines, so a call goes through this file's own detour exactly as an
   engine caller's would, and the row records what the call did (as this game's own write). */
OpenCloseDoorFn     g_playOpen = 0;
OpenCloseDoorFn     g_playClose = 0;
LockDoorFn          g_lockDoor = 0;
LockDoorFn          g_unlockDoor = 0;
LockButtonFn        g_lockButton = 0;
LockDoorFn          g_updateGateCode = 0;   /* updateGateCodeState, after each lock-making step of the applier */
uintptr_t           g_base = 0;

bool  g_on = true;
int   g_hooked = 0;            /* how many of the five detours armed */
int   g_setterOk = 0;          /* the applier's prologue matched */
DWORD g_mainThread = 0;

/* ============================ THE COUNTERS ============================
   Every early return has one and no two events share a number (lesson 1 / lesson 12). */
long long g_doorPublished = 0;               /* DOOR_STATE messages this game put on the wire */
long long g_doorApplied = 0;                 /* engine writes made from the holder's state - THE TOTAL; the three below are SPANS inside it */
long long g_doorAppliedOnArrival = 0;        /*   ... the first time this game hears about a door and disagrees */
long long g_doorReapplyAfterForcedOpen = 0;  /*   ... setupPhysicalUT forced this game's gate open under a holder that says shut */
long long g_doorRevertedNotHolder = 0;       /*   ... RETIRED by P8n and ABSENT from the readout (F172): under the last-actor rule the applier never reverts an engine writer, so this span cannot fire.  The declaration stays as the obituary the next reader of the readout needs. */
long long g_doorKeyUnbuildable = 0;          /* a change was seen and the door could not be NAMED - items.cpp splits the builder's five reasons apart */
long long g_doorSeenChanges = 0;             /* state CHANGES seen at the funnel or at openDoor/closeDoor */
long long g_doorSuppressedSameState = 0;     /* funnel calls that were not a change (the ramp calls it every frame) */
long long g_doorRowsOverflow = 0;            /* a door was seen and the registry was full */
long long g_doorHeldOverflow = 0;            /* a holder state arrived and the table was full */
long long g_doorOutOverflow = 0;             /* a change could not be queued for the sender */
long long g_doorReadFault = 0;               /* a guarded read of a door faulted */
long long g_doorRetUnreadable = 0;           /* _ReturnAddress landed outside the game image */
long long g_doorLinesSuppressed = 0;         /* P060 lines over the budget */
long long g_doorPublishNotHolder = 0;        /* a local change on a door this game does not hold - not published */
long long g_doorPublishNoLink = 0;           /* a change with no session link to send it on */
long long g_doorApplyNoSetter = 0;           /* an arriving state could not be applied: setDoorState's prologue did not match at install */
long long g_doorApplyBlocked = 0;            /* EngineWritesBlocked() at the drain - nothing is written and the next tick tries again */
long long g_doorApplyUnresolved = 0;         /* a holder state whose key names no door this game has seen */
long long g_doorApplyIneffective = 0;        /* the write was made and the state did not change (lesson 14) */
long long g_doorGaveUp = 0;                  /* three ineffective corrections on one door - stopped trying */
/* ---- T-160: THE PLAYED SWING AND THE APPLIED LOCK ---- */
long long g_doorPlayStarted = 0;             /* applies made by openDoor / closeDoor (a SPAN of doorApplied) */
long long g_doorSnapped = 0;                 /* applies made by setDoorState (the other SPAN): played + snapped = doorApplied */
long long g_doorPlayRefused = 0;             /* openDoor / closeDoor left the door unmoved - snapped instead */
long long g_doorPlayLanded = 0;              /* played swings that reached the holder's state */
long long g_doorPlayInterrupted = 0;         /* played swings another writer moved before they landed */
long long g_doorPlayOverdue = 0;             /* played swings not landed inside kDoorPlayLandMs - finished with a snap */
long long g_doorPlayFrames = 0;              /* funnel changes that were a played swing's own frames - never reported */
long long g_doorRelockOurs = 0;              /* the engine's re-lock at the landing of a swing this game played - its own write */
long long g_doorLockApplied = 0;             /* visits that brought this game's lock word to the holder's */
long long g_doorLockLocks = 0, g_doorLockUnlocks = 0;   /* lockDoor / unlockDoor calls the applier made */
long long g_doorLockWantsCleared = 0;        /* the applier's direct write wantsToLock = 0, as lockButton writes it */
long long g_doorLockSetLocked = 0;           /* the applier's direct write DoorLock::locked = 1, as the NPC lock action 0x337630 writes it */
long long g_doorLockIneffective = 0;         /* a lock write that left the lock word unchanged */
long long g_doorLockGaveUp = 0;              /* rows that stopped being lock-corrected after kDoorLockGiveUpN ineffective visits */
long long g_doorLockNoLockObject = 0;        /* lock disagreements on a door with no DoorLock - once per event */
long long g_doorLockNoCall = 0;              /* lock disagreements needing lockDoor / unlockDoor whose prologue did not match - once per event */
long long g_doorLockUnseenLocal = 0;         /* a live lock word no detour saw and this game did not write - its own world's, reported or adopted */
long long g_doorPlayLandReported = 0;        /* played swings that landed with a change of this game's own inside them - reported at the landing */
long long g_doorRepublishedOnLink = 0;       /* rows re-offered at the session link-up edge */
long long g_doorRowRecycled = 0;             /* a registry row whose key no longer rebuilds - freed, never written */
long long g_doorMsgMalformed = 0;
long long g_doorOffThread = 0;               /* funnel calls that were not on the main thread */
long long g_doorSelfWrite = 0;               /* the funnel firing because OUR OWN applier called setDoorState */
/* ---- review-p8e C-1: THE ENGINE'S OWN ACTIVE-ZONE WALK, AND WHAT IT REFUSES ---- */
long long g_doorDroppedInactive = 0;         /* rows the engine's active-zone walk no longer lists - dropped BEFORE any call through them */
long long g_doorActiveViaDoor = 0;           /* the walk test passed on the DOOR object itself */
long long g_doorActiveViaOwner = 0;          /* ... on the door's parent building (a gate's GatewayBuilding, or the house) */
long long g_doorZoneDeactivations = 0;       /* ZoneMapContent::deactivate fan-outs this registry saw, any thread */
long long g_doorPurgeSweeps = 0;             /* main-thread sweeps that ran because of one */
/* ---- P8n: THE LAST-ACTOR RULE, AND WHAT IT REPLACED ----
   Every one of these exists because build/read-door-churn.md could not answer a question with the
   counters that were there.  doorRevertedNotHolder is RETIRED: under the last-actor rule the applier
   never reverts an engine writer, so that span can no longer fire and a counter that cannot fire is
   a silent zero (F172).  Its slot on the verdict line is taken by appliedHolderWord. */
long long g_doorAppliedHolderWord = 0;       /* ... a state the HOLDER published arrived and this game followed it */
long long g_doorActorReported = 0;           /* engine-originated changes SENT to the holder as an actor report */
long long g_doorActorReceived = 0;           /* actor reports that arrived here */
long long g_doorAdopted = 0;                 /* ... that this game, as the holder, adopted and republished */
long long g_doorAdoptedLocal = 0;            /* this game, as the holder, adopted its OWN actor's change */
long long g_doorActorIgnoredNotHolder = 0;   /* ... an actor report for a door this game does not hold */
long long g_doorActorUnnameable = 0;         /* ... an actor report whose key names no door here */
long long g_doorActorSuperseded = 0;         /* ... an actor report replaced by a newer one before a tick could consume it */
long long g_doorLastActorKept = 0;           /* the applier declined to revert: an engine actor moved this door */
long long g_doorMidSwingSkipped = 0;         /* the door was OPENING or CLOSING - left alone, re-checked next tick */
long long g_doorUndone = 0;                  /* corrections a NON-self writer reversed before the next agreement */
long long g_doorGaveUpChurn = 0;             /* rows that stopped being corrected because of that churn */
long long g_doorPublishDroppedInactive = 0;  /* a SPAN inside droppedInactive: the drain's drop, which also loses the queued publish */
long long g_doorRowsForgotten = 0;          /* P15 fold 1: rows dropped by DoorsForgetOwner - a setDestroyed flip on their building was about to run */
long long g_doorOwnerRouteRefused = 0;      /* P15 fold 1: the owner is still active but its door array no longer lists the row's door - the owner route refused */
long long g_doorFirstSighting = 0;           /* a row registering with was = -1 - NOT a local writer */
/* ---- P8n-b: WHAT review-p8n's TEN FINDINGS COST IN NUMBERS.  Every one of these exists because a
   path that used to book nothing now books something, and a counter that cannot fire is a silent
   zero (F172). ---- */
long long g_doorActorRowUnusable = 0;        /* ... an actor report resolved without adoption: no applier, the row had given up, or its read faulted */
long long g_doorChurnReArmed = 0;            /* rows that gave up on churn and were re-armed by 30 s of continuous agreement */
long long g_doorHolderWordRefusedOwnKey = 0; /* a HOLDER publish refused because THIS game holds that door's patch of map */
long long g_doorOriginUnknown = 0;           /* MSG_DOOR_STATE whose trailing origin byte was neither 0 nor 1 - ignored whole */
long long g_doorReportRetried = 0;           /* actor reports re-queued because the holder's word had not landed */
long long g_doorReportAbandoned = 0;         /* ... and reports dropped when the fifteen-second bound expired */
long long g_doorReportNoHolder = 0;          /* an actor report sent while THIS game's area picture named nobody for the door */
/* ---- P8n-c: WHAT review-p8n-b's SEVEN FINDINGS COST IN NUMBERS.  Every path that stores or drops a
   holder's word, or declines to re-open an abandoned report, now books something (F172). ---- */
long long g_doorReportSuppressedAbandoned = 0;    /* M-1: a report verdict on a row whose report was already abandoned - no new episode */
long long g_doorReportDroppedTeardown = 0;        /* reports still pending when the world was torn down and the registry cleared */
/* ---- P8o (T236g, 2026-09-18): THE KEY THE HOLDER COULD NOT NAME, RESOLVED THROUGH THE PARENT
   BUILDING.  asked == found + missedNoBuilding + missedNoDoor + refusedChecks,
   exactly: every exit of DoorResolveByKey books ONE of them and returns.  gateMultiLeaf, recycledRow
   and deferred are SIDE COUNTS and not outcomes - the first two happen inside a resolve that went on to
   answer, and a DEFERRED row was never asked (asked is not incremented for it), so none of the three
   belongs in the identity.  P8o-b (review-p8o M-1) RETIRED refusedTeardown: the apply loop this is
   reached from has already returned on EngineWritesBlocked(), so it could never fire, and F172 says a
   span that cannot fire is retired from the line rather than printed as zero. ---- */
long long g_doorResolveAsked = 0;              /* an actor report for a key no row answers, and the walk was asked */
long long g_doorResolveFound = 0;              /* ... a live door was named and a row was made for it */
long long g_doorResolveMissedNoBuilding = 0;   /* the walk listed no building at all, or no gateway building with that key */
long long g_doorResolveMissedNoDoor = 0;       /* buildings were read and none of their doors rebuilt this key */
long long g_doorResolveRefusedChecks = 0;      /* a candidate door was refused by a check, or the row table was full, and nothing matched */
long long g_doorResolveGateMultiLeaf = 0;      /* a gate key was answered doors[0] of a gateway with more than one leaf - ambiguous */
long long g_doorResolveRecycledRow = 0;        /* RowFor answered a row whose key was a door that used to live at this address - dropped and remade */
long long g_doorResolveDeferred = 0;           /* a second key needed a resolve on the same tick and was left pending for a later one */
long long g_doorHolderWordStoredLoading = 0;      /* M-2: a holder word STORED because this game's area picture had not settled */
long long g_doorHolderWordStoredUnanswerable = 0; /* L-4: ... and stored because the holder question could not be asked at all */
long g_doorWorstUndone = 0;
char g_doorWorstKey[64] = { 0 };
volatile LONG g_doorPurgePending = 0;
/* P8n-b (review-p8n L-9): g_doorLines is RETIRED and ABSENT - it was incremented on every P060 line
   and read by nothing at all, so it was never a measurement of anything.  The number that matters is
   what the budget REFUSED (p060Suppressed), and that one is printed.  Its obituary stays here for the
   next reader of the readout (F172).  Note also that the per-key budget r->lines is reset when a row
   is re-created, so a door whose zone unloads and reloads gets a fresh twenty lines. */
volatile LONG g_doorWorstScanPending = 0;    /* P8n-b (L-7): the funnel asks; the MAIN TICK writes the worst key */
volatile LONG g_doorGiveUpLines = 0;         /* P8n-b (L-7): the churn give-up log's own budget */
/* P8n-c (review-p8n-b L-5): THE REFUSAL LINE IS PER ROW, WHICH IS WHAT THE DOCS ALREADY CLAIMED.
   g_doorRefusedOwnKeyLogged was ONE global latch, so the first refused door in the run silenced the
   line for every other door - and the docs said "logged once per run BY KEY".  The latch is now
   r->refusedLogged, and the total is bounded by this budget so many refusing rows cannot flood. */
volatile LONG g_doorRefusedOwnKeyLines = 0;
/* P8n-c (review-p8n-b L-7): ONE THREAD AT A TIME INSIDE DebugLog ON THIS ROAD.  A per-row latch and a
   global budget both let two threads giving up on two DIFFERENT rows pass together and interleave
   inside DebugLog, which is not thread-safe.  This is claimed by compare-exchange around the CALL. */
volatile LONG g_doorLogInFlight = 0;
/* P8n-c (review-p8n-b M-3): 1 = some row is given up on churn, so the once-a-second re-arm sweep has
   work.  The sweep clears it on a pass that finds none, so the idle cost is one interlocked read. */
volatile LONG g_doorChurnWatch = 0;
volatile LONG g_doorLockSweepPending = 0;    /* some row's lockSweep is set (detour_closeDoor); DoorsTick takes it */
volatile LONG g_dirty = 0;                   /* the tick's whole pre-check: nothing to do while this is 0 */
volatile LONG g_gen = 0;                     /* this game's monotonic publish generation */
/* B12-a / B12 (decision 52): THE DOOR ROAD'S OWN NO-NOTEBOOK NUMBERS, which did not exist before this
   build - the verdict was collapsed into "nobody holds it" and an actor report went out anyway.
   queued is a change written into the outage journal; payloadRefused is one the journal would not take, and
   is the number that says a door change was lost. said is 1 once the text below has been printed.
   B12-c (review-b12 H-2): they are booked at the R4 REFUSAL - the moment the area gate answers "past the
   grace with no notebook" - and NOT in place of the drain's answer. A session-link actor report is not a
   durable write and keeps flowing with or without a notebook (decision 47); what is journaled is the
   holder's own state write, re-published with a fresh generation when the notebook returns. */
long long g_doorQueuedNoNotebook = 0, g_doorQueuePayloadRefused = 0;
long long g_doorNoNotebookSaid = 0;
/* OUR OWN APPLY RE-ENTERS THE FUNNEL, and it must not be mistaken for the world moving a door.
   setDoorState tail-jumps into setDoorOpenAmount, so every correction we make rings our own detour.
   Without this flag the applier would (a) queue its own write for publication, and (b) mark the row
   as locally written, which is the term the three apply reasons are told apart by - so the number
   for "this game had never heard about this door" could never be reached and would read 0 forever
   (F172: a counter that cannot fire is a silent zero).  Set on the MAIN thread around the setter
   only; a worker inside the funnel at the same instant reads it as set, which costs at worst one
   attributed write and never a wrong engine action. */
volatile LONG g_applying = 0;
/* THE DOOR WHOSE PLAYED CLOSE HAS JUST LANDED.  DoorStuff::update 0x298FF0 writes CLOSED through the
   funnel and then, in the same call, calls lockDoor when wantsToLock is set (the call at 0x2990A4, after
   the funnel call at 0x29905F - Confirmed from the 1.0.65 bytes).  That re-lock is part of the close this
   game played, not something its world did, so the lockDoor detour takes it as this game's own write
   when it names this door.  Main thread only; DoorsTick clears it, so it never outlives the frame. */
void* volatile g_playLandedDoor = 0;
/* THE UNPAUSED CLOCK the landing bound of a played swing is measured on (coopdoor::DoorPlayOverdue).  The
   engine's door update does not move a door while the pause byte is set, so DoorsTick adds a frame's time
   only while it is clear.  One frame adds at most kDoorClockStepCapMs, so a load hitch is not counted as a
   swing's time either.  Written on the main thread only. */
unsigned long long kDoorPauseByteRva = 0; static coop::AddrReg kDoorPauseByteRva_reg("PauseByte", &kDoorPauseByteRva);   /* Steam_1.0.65 0x2133969 (the same row store.cpp reads) */
volatile long g_doorUnpausedMs = 0;
unsigned long g_doorClockLastMs = 0;
const unsigned long kDoorClockStepCapMs = 1000;

/* ============================ THE REGISTRY ============================
   A door this game has SEEN through the funnel.  Fixed arrays, claimed with interlocked writes: the
   funnel runs on a thread build/read-doors.md 7.5 could not name, so the discipline is worldgen.cpp's
   - no allocation, no CRT formatting outside a fixed buffer, nothing that can block.
   WHY THE FUNNEL POPULATES IT: setupPhysicalUT calls the funnel UNCONDITIONALLY for every door whose
   physics body is created (the call at 0x29D1EA), so a door this game's own engine has set up is in
   this table from that moment on.
   P8o (T236g) CORRECTS THE REST OF WHAT STOOD HERE.  It said an arriving key could therefore be
   resolved without a building walk, and that is false: the purge sweep FREES the row of a door whose
   zone deactivated, so a door that still exists can have no row at all - 24 actor reports for one
   such door were booked unnameable and dropped.  A key with no row is now resolved through the
   parent building's door array (DoorResolveByKey below).  The funnel is still what CREATES rows; the
   walk is the fallback, asked only for a key somebody has actually reported. */
const int kDoorKeyCap  = 64;
const int kDoorRows    = 256;
const int kDoorHeldCap = 256;
/* AS WIDE AS THE REGISTRY, deliberately: the link-up re-offer queues every row this game has seen
   in one pass, and a mailbox narrower than the registry would drop most of them the moment a peer
   joined - which is exactly the case the re-offer exists for.  An overflow is still counted. */
const int kDoorOutCap  = 256;

struct DoorRow
{
    void* door;                 /* claimed with InterlockedCompareExchangePointer; 0 = free */
    /* THE BUILDING THIS DOOR HANGS ON, recorded at row creation - when the pointer was handed to us
       by the engine and was live by construction - and NEVER re-read afterwards.  It is the second
       candidate for the active-zone walk test; see DoorRowStillActive. */
    void* owner;
    volatile long ready;        /* 1 = the key is written and the row may be read */
    char key[kDoorKeyCap];
    volatile long state;        /* the state we last saw (0..3), -1 unknown */
    volatile long locked;
    volatile long gate;
    volatile long forcedOpen;   /* setupPhysicalUT forced this door open since the last correction */
    volatile long localWrite;   /* something OTHER than our own applier moved this door since the last correction */
    volatile long ineffective;  /* consecutive corrections that did not take */
    volatile long gaveUp;
    /* ---- P8n ---- */
    volatile long corrected;    /* our applier wrote this door and no agreement has been reached since */
    volatile long undone;       /* undone corrections inside the current five-second window */
    volatile long undoneTotal;  /* ... and the count that is never reset, which is what worstChurn reports */
    volatile long churnFirstMs; /* when that window opened (GetTickCount, wraps harmlessly - the maths is unsigned) */
    volatile long gaveUpChurn;  /* this row now follows the last actor and this game stops correcting it */
    volatile long holderWord;   /* a state the HOLDER published arrived for this key since the last agreement */
    volatile long applies;      /* engine writes this game has made on this door */
    volatile long src;          /* who last moved it here: 0 self (own world), 1 holder (our applier), 2 actor */
    volatile long lastChangeMs;
    volatile long lastRet;      /* the return address of the last non-self writer - who it was, by number */
    volatile long lines;        /* P060 lines spent on THIS key (the budget is per key, not global) */
    /* ---- P8n-b ---- */
    volatile long churnAgreeing;  /* the re-arm clock is running on this row */
    volatile long churnAgreeMs;   /* ... and this is when the agreement started */
    volatile long churnLogged;    /* the give-up line for this row has been claimed (interlocked, L-7) */
    volatile long reportPending;  /* an actor report for this row is out and the holder's word has not landed */
    volatile long reportFirstMs;  /* when that report was first made - the abandon bound measures from here */
    volatile long reportLastMs;   /* ... and when it was last queued - the retry period measures from here */
    /* ---- P8n-c ---- */
    volatile long reportAbandoned;/* M-1: the fifteen-second bound expired and this door has not AGREED since */
    volatile long refusedLogged;  /* L-5: the own-key refusal line for THIS row has been spent */
    /* ---- T-160 ---- */
    volatile long playTarget;     /* the terminal state a swing this game played is heading for, -1 none */
    volatile long playStartMs;    /* when that swing was started - the landing bound measures from here */
    volatile long lockIneffective;/* consecutive lock visits that did not move the lock word */
    volatile long lockGaveUp;     /* this row is not lock-corrected until the two games agree about its lock */
    volatile long lockStuck;      /* this lock disagreement could not be applied and has been counted - once per event */
    volatile long lockSweep;      /* a close that did nothing on this door: the next tick compares its lock word */
};
DoorRow g_rows[kDoorRows];

struct DoorHeldRow
{
    volatile long used;
    char key[kDoorKeyCap];
    volatile long state, locked;
    volatile long gen;
    volatile long fromPeer;
    /* ---- P8n: AN ACTOR REPORT IS A ONE-SHOT AND IT DOES NOT OVERWRITE THE HOLDER'S ANSWER UNTIL
       THIS GAME HAS ESTABLISHED THAT IT IS THE HOLDER.  The question needs the door's position,
       which is a virtual call through a stored pointer, so it can only be asked on the main thread
       after the active-zone walk has passed - i.e. in DoorsTick and never at message arrival. ---- */
    volatile long pendingActor;
    volatile long actorState;
    volatile long actorLocked;   /* the reporting game's lock word - it becomes `locked` only if this game adopts the report */
};
DoorHeldRow g_held[kDoorHeldCap];

/* P8n: `reportIfNotHolder` is what tells an ordinary local change from the link-up re-offer.  An
   ordinary change on a door this game does not hold is SENT to the holder as an actor report - that
   is the whole last-actor rule.  A re-offer is the weather pattern's "tell the late joiner what I
   hold" and must stay refused when this game is not the holder, or a joining game would push its own
   stale local states at the holder and win. */
struct DoorOutRow { volatile long used; char key[kDoorKeyCap]; long state, locked, reportIfNotHolder;
                    long retries; unsigned long firstFailMs; };   /* M7b slice 4 fold 1 (F8): a failed actor report's retries */
DoorOutRow g_out[kDoorOutCap];

/* ============================ GUARDED READS (POD ONLY - C2712) ============================ */
int DoorPlausible(const void* p)
{
    if (p == 0 || g_base == 0) return 0;
    __try
    {
        const uintptr_t v = *(const uintptr_t*)p;                 /* the vtable pointer */
        return (v > g_base && v < g_base + 0x10000000u) ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* state, doorOpenAmount, locked, wantsToLock and broken in ONE frame, so a fault cannot leave half of
   them read and half stale.  0 = the read faulted, or the state is not one of the four the enum has -
   which is itself the cheapest test that this pointer is a DoorStuff. */
int DoorReadPod(void* door, int* state, int* doaTenths, int* locked, int* wants, int* broken)
{
    __try
    {
        const int s = *(const int*)((const char*)door + kDoorState);
        if (s < 0 || s > 3) return 0;
        {
            const float a = *(const float*)((const char*)door + kDoorOpenAmt);
            const void* lk = *(void* const*)((const char*)door + kDoorLockPtr);
            int t = (int)(a * 10.0f);
            int lo = 0;
            if (t < 0) t = 0; else if (t > 10) t = 10;
            if (lk != 0) lo = (*(const unsigned char*)((const char*)lk + kLockLocked) != 0) ? 1 : 0;
            *state = s; *doaTenths = t; *locked = lo;
            *wants  = (*(const unsigned char*)((const char*)door + kDoorWants) != 0) ? 1 : 0;
            *broken = (*(const unsigned char*)((const char*)door + kDoorBroken) != 0) ? 1 : 0;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* RootObjectBase::getPosition through vtable slot 8 - the same read the box key builder makes, and
   the one lesson 15's trap does not apply to (three 4-byte copies out of +0x48, no layer choice). */
int DoorPosPod(void* obj, float* pos)
{
    __try
    {
        void** vt = *(void***)obj;
        float v[3];
        v[0] = 0; v[1] = 0; v[2] = 0;
        ((GetPositionFn)vt[kVtGetPos / 8])(obj, v);
        pos[0] = v[0]; pos[1] = v[1]; pos[2] = v[2];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* Building::isGate through the vtable.  On a DoorStuff it forwards to the PARENT's slot (the
   bytes at 0x2ADB80 are mov rcx,[rcx+0x368]; mov rax,[rcx]; jmp [rax+0x3D8]), so asking the DOOR
   answers about the gate it belongs to - which is exactly the object whose key names it. */
void* DoorGatePod(void* door)
{
    __try
    {
        void** vt = *(void***)door;
        return ((IsGateFn)vt[kVtIsGate / 8])(door);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* The parent Building, as a guarded PLAIN READ - no virtual call, nothing dereferenced beyond the one
   field.  Read once, at row creation, off a pointer the engine has just handed us. */
int DoorParentPod(void* door, void** out)
{
    *out = 0;
    __try { *out = *(void* const*)((const char*)door + kDoorParent); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = 0; return 0; }
}
/* P8o: THE PARENT'S DOOR ARRAY, COPIED WHOLE IN ONE SEH FRAME.  The count, the element pointer AND
   the elements come out inside a single frame, so a push_back that reallocates the array mid-read
   cannot leave this road holding a pointer into freed storage - what it walks afterwards is a private
   copy.  `rawCountOut` is the count AS READ, before the buffer cap, because "this gateway has more
   than one leaf" is a fact about the engine's array and not about our buffer.  1 = read, and a count
   of 0 is the ordinary answer for a building with no doors; 0 = the read faulted or the count was
   absurd.  POD ONLY, no temporaries - this function has a __try (C2712). */
int DoorArrayPod(void* building, void** out, int cap, int* countOut, int* rawCountOut)
{
    *countOut = 0;
    *rawCountOut = 0;
    __try
    {
        const unsigned int n = *(const unsigned int*)((const char*)building + kBldDoorsCount);
        if (n > (unsigned int)kDoorArrayMax) return 0;
        *rawCountOut = (int)n;
        if (n == 0) return 1;
        {
            void** const stuff = *(void** const*)((const char*)building + kBldDoorsStuff);
            unsigned int k;
            if (stuff == 0) return 0;
            for (k = 0; k < n && k < (unsigned int)cap; ++k) out[k] = stuff[k];
            *countOut = (int)k;
        }
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { *countOut = 0; *rawCountOut = 0; return 0; }
}
/* P15 fold 1 (review-p15 CRASH 1b): DOES THE BUILDING'S DOOR ARRAY LIST THIS DOOR RIGHT NOW?  1 = yes; 0 = no, an empty array, an
   absurd count or a faulted read.  A pointer compare per element and nothing read out of any element, so a freed door is never
   touched here.  The array is the one DoorArrayPod copies (count +0x1C0, elements +0x1C8, stride 8), and the resolver below already
   treats an element as the DoorStuff pointer itself (parent == b round trip).  POD ONLY - this function has a __try (C2712). */
int DoorOwnerListsPod(void* building, void* door)
{
    __try
    {
        const unsigned int n = *(const unsigned int*)((const char*)building + kBldDoorsCount);
        if (n == 0 || n > (unsigned int)kDoorArrayMax) return 0;
        {
            void* const* const stuff = *(void* const* const*)((const char*)building + kBldDoorsStuff);
            unsigned int k;
            if (stuff == 0) return 0;
            for (k = 0; k < n; ++k)
                if (stuff[k] == door) return 1;
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* P8n: ONE NAME FOR THE CLOCK the churn window measures.  GetTickCount is a millisecond counter that
   wraps every 49.7 days; every comparison made of it here is an UNSIGNED difference, which is correct
   across the wrap.  It is not a game clock and is not used as one - it bounds a five-second window
   and nothing else (principle 1: the recurring check is the tick, this is only the window). */
unsigned long GetCurrentTickMs() { return (unsigned long)::GetTickCount(); }

int PrologueMatches(uintptr_t rva, const unsigned char* want, int n)
{
    __try
    {
        const unsigned char* p = (const unsigned char*)(g_base + rva);
        for (int i = 0; i < n; ++i) if (p[i] != want[i]) return 0;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void ApplySetterPodInner(void* door, int state)
{
    __try { g_setDoorState(door, state); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}
/* The flag is raised OUTSIDE the __try frame and lowered after it, so a fault inside the engine
   cannot leave this game permanently unable to tell its own writes from the world's. */
void ApplySetterPod(void* door, int state)
{
    ::InterlockedExchange(&g_applying, 1);
    ApplySetterPodInner(door, state);
    ::InterlockedExchange(&g_applying, 0);
}

/* 1 = the engine's pause byte is set; 0 when it is clear, has no address row, or the read faults. */
int DoorGamePausedPod()
{
    if (kDoorPauseByteRva == 0 || g_base == 0) return 0;
    __try { return (*(volatile const unsigned char*)(g_base + (uintptr_t)kDoorPauseByteRva) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* MAIN THREAD, once per DoorsTick: the unpaused clock moves on by this frame's time unless the game is paused. */
void DoorClockAdvance()
{
    const unsigned long now = GetCurrentTickMs();
    if (g_doorClockLastMs != 0)
    {
        unsigned long d = now - g_doorClockLastMs;
        if (d > kDoorClockStepCapMs) d = kDoorClockStepCapMs;
        if (DoorGamePausedPod() == 0) ::InterlockedExchangeAdd(&g_doorUnpausedMs, (long)d);
    }
    g_doorClockLastMs = now;
}

/* T-160: the engine's own open / close and lock calls, and the two direct lock writes the engine's own code
   makes where no call produces the word, each inside the same g_applying bracket as the setter, so the
   detours they pass through record them as this game's own write. */
void ApplyPlayPodInner(OpenCloseDoorFn fn, void* door)
{
    __try { fn(door); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}
void ApplyPlayPod(OpenCloseDoorFn fn, void* door)
{
    ::InterlockedExchange(&g_applying, 1);
    ApplyPlayPodInner(fn, door);
    ::InterlockedExchange(&g_applying, 0);
}
void ApplyLockPodInner(int step, void* door)
{
    __try
    {
        if (step == coopdoor::kDoorLockStepLock) g_lockDoor(door);
        else if (step == coopdoor::kDoorLockStepUnlock) g_unlockDoor(door);
        else if (step == coopdoor::kDoorLockStepClearWants)
            *(volatile unsigned char*)((char*)door + kDoorWants) = 0;   /* as lockButton 0x5465D0 writes it */
        else if (step == coopdoor::kDoorLockStepSetLocked)
        {
            unsigned char* lk = *(unsigned char* const*)((const char*)door + kDoorLockPtr);
            if (lk != 0) *(volatile unsigned char*)(lk + kLockLocked) = 1;   /* as the NPC lock action 0x337630 writes it */
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}
void ApplyLockPod(int step, void* door)
{
    ::InterlockedExchange(&g_applying, 1);
    ApplyLockPodInner(step, door);
    ::InterlockedExchange(&g_applying, 0);
}
/* 1 = the door's DoorLock pointer reads non-zero - the three lock calls dereference it with no test. */
int DoorHasLockPod(void* door)
{
    __try { return (*(void* const*)((const char*)door + kDoorLockPtr) != 0) ? 1 : 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
/* After a lock-making step of the applier (lockDoor, or the direct locked = 1): the engine's updateGateCodeState
   0x297250 on the door, as lockButton 0x5465D0 calls it after its lock and DoorStuff::update 0x298FF0 recomputes
   the same after its re-lock, so the door's gate code matches its lock.  unlockDoor 0x569940 ends in this same
   function, so the unlock step needs nothing more. */
void DoorGateCodePod(void* door)
{
    __try { g_updateGateCode(door); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

/* ============================ THE KEY ============================
   THE P7n POSITION KEY, built by the ONE builder the box road uses (items.cpp's ItBoxKeyBuild,
   reached through ObjectPositionKey), so a door and a box can never be named by two different
   formats.  A house door IS a Building and gets its own key; a town GATE's state lives on a child
   DoorStuff, so the key is the GATEWAY BUILDING's with "gate" appended - which also makes it
   impossible for a gate's door key to collide with the gate building's own box key.
   1 = built, 0 = not, and the reason is counted inside items.cpp (population 2 = the door road, so
   a door's key failure can never be read as an item-layer one). */
int DoorKeyBuild(void* door, char* out, int cap, int* isGateOut, void** ownerOut)
{
    void* gate = DoorGatePod(door);
    *isGateOut = (gate != 0) ? 1 : 0;
    if (ownerOut != 0)
    {
        void* parent = 0;
        /* THE BUILDING THE WALK TEST WILL ASK ABOUT.  A gate names its GatewayBuilding, which
           isGate has just returned; anything else names whatever +0x368 holds.  0 is a legal
           answer and simply means the row has one candidate rather than two. */
        if (gate != 0) *ownerOut = gate;
        else if (DoorParentPod(door, &parent) != 0) *ownerOut = parent;
        else *ownerOut = 0;
    }
    /* P8o-b: mode 0 - the VIRTUAL position, which is right here and stays: a detour was handed this
       pointer by the engine itself.  Only the resolver, which finds its own pointers, reads the field. */
    if (gate != 0) return ObjectPositionKey(gate, out, cap, "gate", 2, 0);
    return ObjectPositionKey(door, out, cap, "", 2, 0);
}

/* ============================ THE TABLES ============================ */
DoorRow* RowFor(void* door, const char* keyIfNew, int gate, void* owner)
{
    int i;
    for (i = 0; i < kDoorRows; ++i)
        if (g_rows[i].door == door && g_rows[i].ready != 0) return &g_rows[i];
    if (keyIfNew == 0) return 0;
    for (i = 0; i < kDoorRows; ++i)
    {
        if (g_rows[i].door != 0) continue;
        if (::InterlockedCompareExchangePointer(&g_rows[i].door, door, 0) != 0) continue;
        std::strncpy(g_rows[i].key, keyIfNew, kDoorKeyCap - 1);
        g_rows[i].key[kDoorKeyCap - 1] = 0;
        g_rows[i].owner = owner;
        g_rows[i].state = -1; g_rows[i].locked = -1; g_rows[i].gate = gate;
        g_rows[i].forcedOpen = 0; g_rows[i].localWrite = 0; g_rows[i].ineffective = 0; g_rows[i].gaveUp = 0;
        g_rows[i].corrected = 0; g_rows[i].undone = 0; g_rows[i].undoneTotal = 0;
        g_rows[i].churnFirstMs = 0; g_rows[i].gaveUpChurn = 0; g_rows[i].holderWord = 0;
        g_rows[i].applies = 0; g_rows[i].src = 0; g_rows[i].lastChangeMs = (long)::GetTickCount();
        g_rows[i].lastRet = 0; g_rows[i].lines = 0;
        g_rows[i].churnAgreeing = 0; g_rows[i].churnAgreeMs = 0; g_rows[i].churnLogged = 0;
        g_rows[i].reportPending = 0; g_rows[i].reportFirstMs = 0; g_rows[i].reportLastMs = 0;
        g_rows[i].reportAbandoned = 0; g_rows[i].refusedLogged = 0;
        g_rows[i].playTarget = -1; g_rows[i].playStartMs = 0; g_rows[i].lockIneffective = 0; g_rows[i].lockGaveUp = 0; g_rows[i].lockStuck = 0;
        g_rows[i].lockSweep = 0;
        ::InterlockedExchange(&g_rows[i].ready, 1);
        return &g_rows[i];
    }
    ::InterlockedIncrement64(&g_doorRowsOverflow);
    return 0;
}
DoorRow* RowByKey(const char* key)
{
    for (int i = 0; i < kDoorRows; ++i)
        if (g_rows[i].ready != 0 && std::strcmp(g_rows[i].key, key) == 0) return &g_rows[i];
    return 0;
}
/* The live row of this door pointer, or 0.  A pointer compare only - nothing is read through it. */
DoorRow* RowByDoor(void* door)
{
    for (int i = 0; i < kDoorRows; ++i)
        if (g_rows[i].ready != 0 && g_rows[i].door == door) return &g_rows[i];
    return 0;
}
/* P8o: DOES THIS KEY NAME A GATE?  DoorKeyBuild appends the word "gate" VERBATIM to the gateway
   building's key (ObjectPositionKey(gate, ..., "gate", 2, 0) in DoorKeyBuild above), and every other
   door key ends in
   the last digit of the position, so the suffix is the whole discriminator. */
int DoorKeyIsGate(const char* key)
{
    const size_t n = std::strlen(key);
    if (n < 4) return 0;
    return (std::strcmp(key + (n - 4), "gate") == 0) ? 1 : 0;
}
DoorHeldRow* HeldFor(const char* key, int makeIt)
{
    int i;
    for (i = 0; i < kDoorHeldCap; ++i)
        if (g_held[i].used != 0 && std::strcmp(g_held[i].key, key) == 0) return &g_held[i];
    if (makeIt == 0) return 0;
    for (i = 0; i < kDoorHeldCap; ++i)
    {
        if (g_held[i].used != 0) continue;
        std::strncpy(g_held[i].key, key, kDoorKeyCap - 1);
        g_held[i].key[kDoorKeyCap - 1] = 0;
        g_held[i].state = -1; g_held[i].locked = -1; g_held[i].gen = -1; g_held[i].fromPeer = -1;
        g_held[i].pendingActor = 0; g_held[i].actorState = -1; g_held[i].actorLocked = -1;
        g_held[i].used = 1;
        return &g_held[i];
    }
    ++g_doorHeldOverflow;
    return 0;
}
/* M7b slice 4 fold 1 (review 2026-10-02 F8): AN ACTOR REPORT THAT WAS NOT SENT STAYS QUEUED. The drain frees an entry before it
   sends; an actor report SendDoorState refused (the holder's game not in the world, no road) is queued again with its retry count,
   for at most kDoorReportRetryMs from its first failure (then given up, counted). A newer entry for the same door supersedes it - an
   older state is never sent after a newer one. */
const unsigned long kDoorReportRetryMs = 30000;
long long g_doorReportSendRetried = 0, g_doorReportRetryGaveUp = 0, g_doorReportRetrySuperseded = 0, g_doorReportRetryFull = 0;
int DoorOutPendingFor(const char* key, int skip)
{
    for (int j = 0; j < kDoorOutCap; ++j)
        if (j != skip && g_out[j].used != 0 && std::strncmp(g_out[j].key, key, kDoorKeyCap) == 0) return 1;
    return 0;
}
void DoorOutRetry(const char* key, long st, long lk, long rep, long tries, unsigned long firstFail, int how)
{
    const unsigned long now = (unsigned long)::GetTickCount();
    const unsigned long first = (tries > 0) ? firstFail : now;
    if (coopdoor::DoorRetryGivesUp(how, now - first, kDoorReportRetryMs) != 0) { ++g_doorReportRetryGaveUp; return; }
    if (DoorOutPendingFor(key, -1) != 0) { ++g_doorReportRetrySuperseded; return; }
    for (int i = 0; i < kDoorOutCap; ++i)
    {
        if (g_out[i].used != 0) continue;
        if (::InterlockedCompareExchange(&g_out[i].used, 1, 0) != 0) continue;
        std::strncpy(g_out[i].key, key, kDoorKeyCap - 1);
        g_out[i].key[kDoorKeyCap - 1] = 0;
        g_out[i].state = st; g_out[i].locked = lk; g_out[i].reportIfNotHolder = rep;
        g_out[i].retries = tries + 1; g_out[i].firstFailMs = first;
        ::InterlockedExchange(&g_dirty, 1);
        ++g_doorReportSendRetried;   /* F8: not g_doorReportRetried (line ~152, the holder-word re-queue) */
        return;
    }
    ++g_doorReportRetryFull;
}
void QueueOut(const char* key, int state, int locked, int reportIfNotHolder)
{
    for (int i = 0; i < kDoorOutCap; ++i)
    {
        if (g_out[i].used != 0) continue;
        if (::InterlockedCompareExchange(&g_out[i].used, 1, 0) != 0) continue;
        std::strncpy(g_out[i].key, key, kDoorKeyCap - 1);
        g_out[i].key[kDoorKeyCap - 1] = 0;
        g_out[i].state = state; g_out[i].locked = locked;
        g_out[i].reportIfNotHolder = reportIfNotHolder;
        g_out[i].retries = 0; g_out[i].firstFailMs = 0;   /* fold 1 (F8) */
        ::InterlockedExchange(&g_dirty, 1);
        return;
    }
    ::InterlockedIncrement64(&g_doorOutOverflow);
}

/* ============================ P060 ============================
   ONE LINE PER STATE CHANGE, never per call: the ramp calls the funnel every frame and a settled door
   costs nothing.  `ret` is _ReturnAddress() - base, so the six openDoor callers and the four
   closeDoor callers of build/read-doors.md 2.3 are told apart BY NUMBER rather than by guesswork -
   that is what answers "who opened the gate".  DebugLog is not thread-safe and this can run on a
   worker, so the line budget is claimed with an interlocked increment exactly as towngen's P025 and
   P027 do, and what it refused is a number rather than a silence. */
/* P8n: THE BUDGET IS PER KEY, NOT GLOBAL.  In T236d one runaway door spent the whole global 400
   between t=22.4 s and t=157.2 s and blinded the log for every other door for the remaining 232 s of
   the run - the tail of the very loop the lines were printed to show could not be seen.  Twenty
   lines per key is enough to show a door's shape and cannot be spent by somebody else's door.  The
   ceiling is therefore kDoorRows x kP060PerKeyBudget = 5,120 lines rather than 400, and what the
   budget refused is still a number (p060Suppressed) rather than a silence. */
const LONG kP060PerKeyBudget = 20;
/* P8n-b (review-p8n L-7): the churn give-up line's own budget.  One line per row is already latched
   per row, but a world with many rows churning could still spend an unbounded number of DebugLog
   calls off the main thread, so the total is bounded and what it refuses is counted. */
const LONG kDoorGiveUpLineBudget = 32;
/* P8n-c (review-p8n-b L-5): the own-key refusal line's budget.  It is latched per ROW now rather than
   once for the run, so the total needs a ceiling of its own; sixteen names sixteen different doors,
   which is far more than a diagnosis needs and far less than a registry-full of them. */
const LONG kDoorRefusedOwnKeyLineBudget = 16;
void P060Line(DoorRow* r, const char* key, int was, int now, int doaTenths, int locked, int wants, int broken,
              int gate, const char* hold, uintptr_t retRva, const char* where)
{
    if (r == 0 || ::InterlockedIncrement(&r->lines) > kP060PerKeyBudget)
    { ::InterlockedIncrement64(&g_doorLinesSuppressed); return; }
    {
        char b[352];
        _snprintf(b, 351, "[P060] door key=%s was=%d now=%d doa=%d locked=%d wants=%d broken=%d gate=%d hold=%s ret=0x%llX at=%s",
                  key, was, now, doaTenths, locked, wants, broken, gate, hold,
                  (unsigned long long)retRva, where);
        b[351] = 0;
        DebugLog(b);
    }
}
/* The holder verdict, asked ONLY where it can be asked: items.cpp's AreaVerdictForDoor is the box
   road's own ItAreaVerdictAt and one of its arms reads main-thread state.  Off the main thread the
   probe prints `unknown` rather than a number it is not entitled to (lesson 12: report the input you
   actually had, not the answer you wish you had). */
const char* HoldWord(void* door, int onMain)
{
    if (onMain == 0) return "unknown";
    {
        float pos[3];
        pos[0] = 0; pos[1] = 0; pos[2] = 0;
        if (DoorPosPod(door, pos) == 0) return "unreadable";
        {
            const int v = AreaVerdictForDoor(pos);
            if (v == 1) return "me";
            if (v == 2) return "peer";
            return "none";
        }
    }
}

/* THE ONE PLACE A CHANGE IS RECORDED, so the funnel and the two edge detours cannot drift apart. */
/* `ours` = 1: the caller already knows this change is this game's own write (the re-lock at the landing of
   a swing this game played). */
void NoteDoor(void* door, uintptr_t retRva, const char* where, int ours)
{
    if (!g_on) return;
    if (DoorPlausible(door) == 0) { ::InterlockedIncrement64(&g_doorReadFault); return; }
    {
        int state = 0, doa = 0, locked = 0, wants = 0, broken = 0;
        if (DoorReadPod(door, &state, &doa, &locked, &wants, &broken) == 0)
        { ::InterlockedIncrement64(&g_doorReadFault); return; }
        {
            char key[kDoorKeyCap];
            int gate = 0;
            /* The building this door hangs on, read HERE - the engine has just handed us `door`, so
               this is the one moment the pointer is live by construction (review-p8e C-1). */
            void* owner = 0;
            const int onMain = (::GetCurrentThreadId() == g_mainThread) ? 1 : 0;
            key[0] = 0;
            if (onMain == 0) ::InterlockedIncrement64(&g_doorOffThread);
            if (DoorKeyBuild(door, key, kDoorKeyCap, &gate, &owner) == 0)
            { ::InterlockedIncrement64(&g_doorKeyUnbuildable); return; }
            {
                DoorRow* r = RowFor(door, key, gate, owner);
                if (r == 0) return;
                /* A forced open from physics setup is remembered whatever else is true of it: it is
                   the one write the holder's answer has to survive (F633). */
                if (kSetupForcedOpenRet != 0 && retRva == kSetupForcedOpenRet)
                { ::InterlockedExchange(&r->forcedOpen, 1); ::InterlockedExchange(&g_dirty, 1); }
                /* THE LOCK WORD IS PART OF WHAT CHANGES.  A lock pressed on a door that does not move
                   reaches this function only from the lock detours, and it is as much a change to share
                   as a swing is. */
                const int word = coopdoor::DoorLockWord(locked, wants);
                if (r->state == (long)state && r->locked == (long)word)
                { ::InterlockedIncrement64(&g_doorSuppressedSameState); return; }
                {
                    const int was = (int)r->state;
                    const int applying = (::InterlockedCompareExchange(&g_applying, 0, 0) != 0) ? 1 : 0;
                    /* A SWING THIS GAME PLAYED: its frames are its own write until it lands
                       (coopdoor::DoorPlayNote).  Only a STATE change is classified - a lock word moving
                       on its own during a swing is somebody else's. */
                    int played = 0, landReport = 0;
                    if (applying == 0 && ours == 0 && r->state != (long)state)
                    {
                        const int v = coopdoor::DoorPlayNote((int)r->playTarget, state);
                        if (v == coopdoor::kDoorPlayOwned) played = 1;
                        else if (v == coopdoor::kDoorPlayLanded)
                        {
                            played = 1;
                            landReport = coopdoor::DoorPlayLandReport((int)r->localWrite, (int)r->locked, word);
                            ::InterlockedExchange(&r->playTarget, -1);
                            ::InterlockedIncrement64(&g_doorPlayLanded);
                            if (state == coopdoor::kDoorClosed && onMain != 0)
                                ::InterlockedExchangePointer((void* volatile*)&g_playLandedDoor, door);
                        }
                        else if (v == coopdoor::kDoorPlayBroken)
                        {
                            ::InterlockedExchange(&r->playTarget, -1);
                            ::InterlockedIncrement64(&g_doorPlayInterrupted);
                        }
                    }
                    const int self = (applying != 0 || ours != 0 || played != 0) ? 1 : 0;
                    const unsigned long nowMs = GetCurrentTickMs();
                    ::InterlockedExchange(&r->state, (long)state);
                    ::InterlockedExchange(&r->locked, (long)word);
                    ::InterlockedExchange(&r->lastChangeMs, (long)nowMs);
                    P060Line(r, key, was, state, doa, locked, wants, broken, gate,
                             self != 0 ? "self" : HoldWord(door, onMain), retRva,
                             applying != 0 ? "apply" : (played != 0 ? "play" : (ours != 0 ? "relock" : where)));
                    /* OUR OWN CORRECTION IS NOT THE WORLD MOVING A DOOR.  It is recorded above (the
                       row must track what the door actually says) and then it stops here: it is not
                       a change to publish, it is not a local writer to revert, and counting it as
                       either would make both numbers mean two things. */
                    if (self != 0)
                    {
                        ::InterlockedExchange(&r->src, 1);
                        ::InterlockedIncrement64(&g_doorSelfWrite);
                        if (played != 0) { ::InterlockedIncrement64(&g_doorPlayFrames); ::InterlockedExchange(&g_dirty, 1); }
                        /* A SWING THAT LANDS WITH THIS GAME'S OWN CHANGE INSIDE IT (coopdoor::DoorPlayLandReport):
                           a lock pressed, or written by the NPC lock action, while the door was mid-way was not
                           queued - only a terminal state is - so it goes out now, on the holder as its word. */
                        if (landReport != 0)
                        {
                            ::InterlockedExchange(&r->localWrite, 1);
                            ::InterlockedIncrement64(&g_doorPlayLandReported);
                            QueueOut(key, state, word, 1);
                        }
                        return;
                    }
                    ::InterlockedExchange(&r->lastRet, (long)(retRva & 0xFFFFFFFFu));
                    /* ---- P8n: A CORRECTION THAT A NON-SELF WRITER HAS JUST UNDONE.  This is the
                       number the shipped guard could not produce: it measured whether our own write
                       LANDED (it always did), never whether it STAYED landed.  The consume is one
                       interlocked exchange so a second thread inside the funnel cannot count the same
                       correction twice.  The churn window itself is a plain read-modify-write of the
                       row and can under-count under a race, which costs a later give-up and never a
                       wrong engine action. ---- */
                    if (::InterlockedExchange(&r->corrected, 0) != 0)
                    {
                        coopdoor::DoorChurn c;
                        c.undone = (long)r->undone;
                        c.firstMs = (unsigned long)(long)r->churnFirstMs;
                        c.gaveUp = (int)r->gaveUpChurn;
                        /* P8n-b (M-4): an undone correction IS a disagreement, so it resets the
                           re-arm clock on this row rather than letting a stale one run through it. */
                        c.agreeing = 0; c.agreeSinceMs = 0;
                        if (coopdoor::DoorChurnNoteUndone(&c, nowMs) != 0)
                        {
                            /* P8n-b (review-p8n L-7): AN INTERLOCKED LATCH AND THE FILE'S OWN BUDGET.
                               This runs on the funnel's thread, DebugLog is NOT thread-safe, and what
                               made the line "once" was a plain read-modify-write of the row - two
                               threads could pass it together and interleave inside DebugLog.  The
                               claim is now one compare-exchange, and the line is spent out of a
                               budget exactly as P060Line's is, with the refusal counted. */
                            /* P8n-c (review-p8n-b L-7): AND THE CALL ITSELF IS CLAIMED, NOT ONLY
                               THE ROW AND THE BUDGET.  Neither of those is mutual exclusion: two
                               threads giving up on two DIFFERENT rows each pass their own row latch,
                               each take a slot out of the budget, and both are inside DebugLog - which
                               is NOT thread-safe - at the same instant.  One global compare-exchange
                               around the call makes that impossible; a thread that finds it held drops
                               its line and counts the refusal rather than racing for it. */
                            if (::InterlockedCompareExchange(&r->churnLogged, 1, 0) == 0)
                            {
                                if (::InterlockedIncrement(&g_doorGiveUpLines) <= kDoorGiveUpLineBudget
                                    && ::InterlockedCompareExchange(&g_doorLogInFlight, 1, 0) == 0)
                                {
                                    char gb[288];
                                    _snprintf(gb, 287,
                                              "[DOOR] giving up on '%s' - %ld corrections inside %lu ms were each UNDONE by a"
                                              " local writer, so this game stops correcting it and the door follows the LAST"
                                              " ACTOR until thirty seconds of agreement re-arm it.  ret of the last writer = 0x%lX",
                                              key, (long)c.undone, (unsigned long)(nowMs - c.firstMs),
                                              (unsigned long)(retRva & 0xFFFFFFFFu));
                                    gb[287] = 0;
                                    DebugLog(gb);
                                    ::InterlockedExchange(&g_doorLogInFlight, 0);
                                }
                                else ::InterlockedIncrement64(&g_doorLinesSuppressed);
                            }
                            ::InterlockedIncrement64(&g_doorGaveUpChurn);
                            /* P8n-c (M-3): the re-arm sweep has work now. */
                            ::InterlockedExchange(&g_doorChurnWatch, 1);
                        }
                        ::InterlockedExchange(&r->undone, c.undone);
                        ::InterlockedExchange(&r->churnFirstMs, (long)c.firstMs);
                        ::InterlockedExchange(&r->gaveUpChurn, (long)c.gaveUp);
                        ::InterlockedIncrement64(&g_doorUndone);
                        ::InterlockedIncrement(&r->undoneTotal);
                        ::InterlockedExchange(&r->churnAgreeing, 0);
                        /* P8n-b (review-p8n L-7): THE WORST KEY IS NAMED ON THE MAIN TICK AND NOWHERE
                           ELSE.  This ran on the funnel's thread and did a plain compare followed by a
                           64-byte strncpy into two shared globals, so the readout could print a torn
                           key beside somebody else's total - a number that reads as a measurement and
                           is not one.  The detour raises a flag; DoorsTick takes the max and the name
                           from the rows' own interlocked totals. */
                        ::InterlockedExchange(&g_doorWorstScanPending, 1);
                        ::InterlockedExchange(&g_dirty, 1);
                    }
                    /* ---- P8n: A FIRST SIGHTING IS NOT A WRITER.  A row freed by the liveness test is
                       re-created by the door's next funnel call with was = -1, and the shipped code
                       booked that as a local writer - so an apply that was this game hearing about the
                       door for the first time was attributed to "a local writer moved it". ---- */
                    if (coopdoor::DoorSightingIsLocalWrite(was) == 0)
                    {
                        ::InterlockedExchange(&r->src, 0);
                        ::InterlockedIncrement64(&g_doorFirstSighting);
                    }
                    else
                    {
                        ::InterlockedExchange(&r->src, 2);
                        ::InterlockedExchange(&r->localWrite, 1);
                    }
                    ::InterlockedExchange(&r->ineffective, 0);
                    ::InterlockedIncrement64(&g_doorSeenChanges);
                    /* OPENING and CLOSING NEVER GO ON THE WIRE - they are animation and the receiver
                       derives them.  Only a terminal state is queued, and under the last-actor rule
                       that queued change is ALSO the actor report a non-holder sends to the holder:
                       the drain asks who holds the door at the moment it sends. */
                    if (state == 0 || state == 1) QueueOut(key, state, word, 1);
                    else ::InterlockedExchange(&g_dirty, 1);
                }
            }
        }
    }
}

uintptr_t RetRva(void* ret)
{
    const uintptr_t r = (uintptr_t)ret;
    if (g_base == 0 || r < g_base || r > g_base + 0x10000000u)
    { ::InterlockedIncrement64(&g_doorRetUnreadable); return 0; }
    return r - g_base;
}

/* ============================ THE THREE DETOURS ============================ */
void detour_setDoorOpenAmount(void* door, float amount, unsigned char force)
{
    const uintptr_t ret = RetRva(_ReturnAddress());
    if (orig_setDoorOpenAmount != 0) orig_setDoorOpenAmount(door, amount, force);
    NoteDoor(door, ret, "funnel", 0);
}
unsigned char detour_openDoor(void* door)
{
    const uintptr_t ret = RetRva(_ReturnAddress());
    const unsigned char rc = (orig_openDoor != 0) ? orig_openDoor(door) : 0;
    NoteDoor(door, ret, "openDoor", 0);
    return rc;
}
unsigned char detour_closeDoor(void* door)
{
    const uintptr_t ret = RetRva(_ReturnAddress());
    const unsigned char rc = (orig_closeDoor != 0) ? orig_closeDoor(door) : 0;
    NoteDoor(door, ret, "closeDoor", 0);
    /* A close that did nothing is how the NPC lock action 0x337630 begins on a door that is already shut; its
       lock write follows and no detour sees it, so the door's row is marked and the next tick compares its lock
       word (DoorLockSweepRow).  Only a door with a DoorLock can be locked and only a door with a live row can be
       compared, so a no-op close on any other door marks nothing and costs no tick. */
    if (rc == 0 && ::InterlockedCompareExchange(&g_applying, 0, 0) == 0 && DoorHasLockPod(door) != 0)
    {
        DoorRow* r = RowByDoor(door);
        if (r != 0)
        {
            ::InterlockedExchange(&r->lockSweep, 1);
            ::InterlockedExchange(&g_doorLockSweepPending, 1);
            ::InterlockedExchange(&g_dirty, 1);
        }
    }
    return rc;
}
/* T-160: THE TWO ENGINE PATHS THAT CHANGE A LOCK WITHOUT MOVING THE DOOR.  lockButton is the player's
   lock press; lockDoor is DoorStuff::update's re-lock when a closing door lands with wantsToLock set.
   Without them a lock pressed on a shut door would never be seen at all - the funnel only runs when a
   door moves. */
void detour_lockDoor(void* door)
{
    const uintptr_t ret = RetRva(_ReturnAddress());
    const int ours = (door != 0 && ::InterlockedCompareExchangePointer((void* volatile*)&g_playLandedDoor, 0, door) == door) ? 1 : 0;
    if (orig_lockDoor != 0) orig_lockDoor(door);
    if (ours != 0) ::InterlockedIncrement64(&g_doorRelockOurs);
    NoteDoor(door, ret, "lockDoor", ours);
}
void detour_lockButton(void* door, void* panelLine)
{
    const uintptr_t ret = RetRva(_ReturnAddress());
    if (orig_lockButton != 0) orig_lockButton(door, panelLine);
    NoteDoor(door, ret, "lockButton", 0);
}

/* ============================ IS THE ENGINE STILL HOLDING THIS DOOR? ============================
   review-p8e C-1.  items.cpp settled this for the box road and the door road did not have it: a
   __try-guarded read-back of a Building* is NOT enough, because a building the engine has DEACTIVATED
   but not destroyed still reads back its own instance id and passes every such test (F544 measured a
   resolve of a box in a zone that had unloaded 56 s earlier).  The only test that says THE ENGINE
   STILL HAS THIS OBJECT is membership in the engine's own walk of active zones, and it dereferences
   nothing - a pointer compare.  The door registry makes TWO virtual calls through every stored
   pointer on every dirty tick (isGate, vtable +0x3D8, and getPosition, +0x40, both inside the key
   re-derivation), so the walk test comes FIRST, before any of them.
   TWO CANDIDATES, AND THAT IS NOT BELT AND BRACES.  containers.md "Doors and gates" records that
   whether a DoorStuff appears in the ZoneMapContent::things walk AT ALL is NOT ESTABLISHED, and the
   safe enumeration the engine itself uses is parent building -> its doors array.  So a row carries
   the building it was born on as well, and either pointer being listed proves the door is in a live
   zone.  `activeVia[door,owner]` is what settles that open question, in one run, for free: door > 0
   says a DoorStuff IS walked; door == 0 with owner > 0 says only the parent is.
   A ROW THAT FAILS BOTH IS DROPPED, NOT WRITTEN.  Same rule as the box cache's `boxCacheNotListed`:
   the walk can also fall short (LoadedBuildings truncates at 64 zones, 20,000 things or 4096
   buildings, and says so through `walkTruncated`), and a row refused for that reason is a door this
   game stops correcting until its next funnel call re-registers it - which is the price of never
   calling through a pointer the engine has let go. */
int DoorRowStillActive(DoorRow* r)
{
    if (ObjectStillActive(r->door) != 0) { ++g_doorActiveViaDoor; return 1; }
    {
        void* owner = r->owner;
        if (owner != 0 && owner != r->door && ObjectStillActive(owner) != 0)
        {
            /* P15 fold 1 (review-p15 CRASH 1b): A LIVE OWNER IS NOT A LIVE DOOR.  setDestroyed(1) deletes a building's doors while the
               building itself stays active (GameWorld::destroy 0x798F50, "door in setDestroyed"), so the owner route used to pass a row
               whose door had just been freed, and DoorPlausible passes on a just-freed object.  The owner route now holds only while
               the owner's own door array still lists this door.  DoorsForgetOwner drops such rows before the flip; this is the second
               line for a flip it did not see. */
            if (DoorOwnerListsPod(owner, r->door) != 0) { ++g_doorActiveViaOwner; return 1; }
            ++g_doorOwnerRouteRefused;
        }
    }
    return 0;
}
/* Free the row.  `ready` falls first, so no other reader can take the key while the pointer is being
   cleared, and the pointer is cleared with the interlocked store RowFor's claim expects. */
void DropRow(DoorRow* r)
{
    ::InterlockedExchange(&r->ready, 0);
    ::InterlockedExchangePointer(&r->door, 0);
    r->owner = 0;
}

/* ============================ P8o: KEY -> DOOR, THE LONG WAY ROUND (T236g) ============================
   A row used to be created only by this game's own setDoorState detours - before this function, NoteDoor
   was RowFor's only caller - so a holder whose zone had reloaded had NO row for the key its peer
   reported: 24 reports were booked lastActor.unnameable and dropped, and the two games disagreed about
   that door for about 100 s.  This is the capability that was missing - name a live door from a key
   alone - and it is asked ONLY when an actor report has arrived for a key no row answers.
   MAIN THREAD, AND IT MAKES NO VIRTUAL CALL AT ALL (P8o-b, review-p8o H-1).  It walks a MEMO, and the
   off-thread zoneDeactivate detour in items.cpp documents the window where a worker frees an object the
   main thread's still-current-looking list contains; SEH catches a fault but NOT a call through a mapped
   garbage vtable slot, which is the use-after-free class this project has paid for twice.  So: the
   position comes from the +0x48 field through the one key builder's POD mode, and whether a key names a
   gate is read off THE KEY and never asked of the object.  Every read here is a plain read under SEH.
   IT ADDS NO WALK.  ObjectActiveBuildingCount is the box road's own memoised loaded-buildings list -
   the same one DoorRowStillActive tests against - so at most one walk happens per generation however
   many keys ask.  THE COST, HONESTLY: per building ONE POD snapshot of the door array, and the count is
   0 for most of them; per candidate door four checks and, for a NON-GATE key, one key build and one
   strcmp.  FOR A GATE KEY IT IS ONE KEY BUILD PER LISTED BUILDING - the gate key is the gateway
   building's own key plus a word, so each building has to be named to be ruled out - and that loop stops
   at the building that matches (review-p8o L-2).  The tick asks for at most one key (review-p8o M-3).
   EVERY SPECULATIVE KEY BUILD PASSES countIt = 0 (review-p8o M-4), so doorKeyUnbuildable, the builder's
   own refusal split and keySidHashed keep counting real doors at the detours and not candidates here.
   ON SUCCESS THE ROW IS MADE AS NoteDoor MAKES IT - RowFor with the key, the gate flag and the owner
   building - and NOTHING ELSE IS DECIDED HERE: the caller's existing adopt path (DoorRecvDecide,
   ApplyToRow) runs unchanged on the row this returns.
   IT SITS HERE, below DropRow, and not beside RowByKey where it started: it needs DropRow for the
   recycled-address row (review-p8o M-2), and this tree does not forward-declare a thing inside the
   namespace that defines it.
   EVERY EXIT BOOKS ONE COUNTER, so asked == found + missedNoBuilding + missedNoDoor + refusedChecks,
   and the readout can check it. */
DoorRow* DoorResolveByKey(const char* key)
{
    int i, n, isGateKey;
    int sawRefusedCheck = 0;
    int gateBuildingSeen = 0;
    ++g_doorResolveAsked;
    if (key == 0 || key[0] == 0) { ++g_doorResolveRefusedChecks; return 0; }
    isGateKey = DoorKeyIsGate(key);
    n = ObjectActiveBuildingCount();
    if (n <= 0) { ++g_doorResolveMissedNoBuilding; return 0; }
    for (i = 0; i < n; ++i)
    {
        void* b = ObjectActiveBuildingAt(i);
        void* elems[kDoorArrayMax];
        int cnt = 0, raw = 0, lim, j;
        /* review-p8o L-2: A GATE KEY NAMES ONE BUILDING, so the walk stops at it - the previous pass
           set this flag and there is nothing left for the remaining buildings to answer. */
        if (isGateKey != 0 && gateBuildingSeen != 0) break;
        if (b == 0) continue;
        if (isGateKey != 0)
        {
            /* A GATE KEY IS THE GATEWAY BUILDING'S OWN KEY PLUS THE WORD, so the building is named
               directly - and doors[0] is then the only answer the key can have, because every leaf of
               one gateway shares that single key.  A gateway with more than one leaf is counted as
               ambiguous below rather than guessed at twice.  THE BUILDING'S KEY IS BUILT IN POD MODE
               AND UNCOUNTED: a listed building is exactly the kind of pointer H-1 is about. */
            char bkey[kDoorKeyCap];
            bkey[0] = 0;
            if (ObjectPositionKey(b, bkey, kDoorKeyCap, "gate", 3, 1) == 0) continue;   /* P8o-c: countIt 3 books nothing */
            if (std::strcmp(bkey, key) != 0) continue;
            gateBuildingSeen = 1;
        }
        if (DoorArrayPod(b, elems, kDoorArrayMax, &cnt, &raw) == 0) continue;
        if (cnt <= 0) continue;
        lim = (isGateKey != 0) ? 1 : cnt;
        for (j = 0; j < lim; ++j)
        {
            void* d = elems[j];
            void* parent = 0;
            char again[kDoorKeyCap];
            int st = 0, doa = 0, lk = 0, wants = 0, broken = 0;
            int plausible, stateOk, parentOk, keyEqual, verdict;
            if (d == 0) continue;
            /* THE ORDER MATTERS AND IT IS THE FIRST RULE OF THIS PROJECT: nothing is read out of this
               pointer until DoorPlausible has looked at its vtable slot (review-p8e C-1), and the key
               is built last because it is the most work. */
            plausible = DoorPlausible(d);
            stateOk   = (plausible != 0 && DoorReadPod(d, &st, &doa, &lk, &wants, &broken) != 0) ? 1 : 0;
            parentOk  = (stateOk != 0 && DoorParentPod(d, &parent) != 0 && parent == b) ? 1 : 0;
            again[0] = 0;
            if (isGateKey != 0)
            {
                /* THE GATE BUILDING'S KEY HAS ALREADY MATCHED, EXACTLY, and for a gate that key IS the
                   door's key - DoorKeyBuild builds it from the gateway and never from the leaf.  So
                   nothing is asked of the door here, and isGate (a virtual) is not called. */
                keyEqual = parentOk;
                if (keyEqual != 0)
                { std::strncpy(again, key, kDoorKeyCap - 1); again[kDoorKeyCap - 1] = 0; }
            }
            else
            {
                /* A NON-GATE KEY IS BUILT FROM THE DOOR ITSELF, in the non-gate shape, for every
                   candidate - and a gate door's non-gate-shaped key can never equal a non-gate key, so
                   asking the object whether it is a gate buys nothing and costs a virtual call. */
                keyEqual = (parentOk != 0
                            && ObjectPositionKey(d, again, kDoorKeyCap, "", 3, 1) != 0   /* P8o-c: countIt 3 books nothing */
                            && std::strcmp(again, key) == 0) ? 1 : 0;
            }
            verdict = coopdoor::DoorResolveAccept(plausible, stateOk, parentOk, keyEqual);
            if (verdict == coopdoor::kDoorResolveRefusedCheck) { sawRefusedCheck = 1; continue; }
            if (verdict != coopdoor::kDoorResolveAccept) continue;   /* a live door, but not this key's */
            {
                DoorRow* r = RowFor(d, again, isGateKey, b);
                /* review-p8o M-2: RowFor MATCHES AN EXISTING ROW BY POINTER FIRST, and an address the
                   engine has recycled can carry a live row whose key is the door that USED to be here.
                   Such a row would be adopted under the wrong name, so it is dropped - the same answer
                   ApplyToRow gives a row whose key no longer rebuilds - and remade for this key. */
                if (r != 0 && std::strcmp(r->key, key) != 0)
                {
                    ++g_doorResolveRecycledRow;
                    DropRow(r);
                    r = RowFor(d, again, isGateKey, b);
                    if (r != 0 && std::strcmp(r->key, key) != 0) r = 0;
                }
                /* A ROW THAT COULD NOT BE SEATED IS A REFUSAL OF THIS CANDIDATE and is booked as one;
                   a full table stays separable because overflow.rows counts that cause alone. */
                if (r == 0) { sawRefusedCheck = 1; continue; }
                if (isGateKey != 0 && raw > 1) ++g_doorResolveGateMultiLeaf;
                ++g_doorResolveFound;
                ::InterlockedExchange(&g_dirty, 1);
                return r;
            }
        }
    }
    if (isGateKey != 0 && gateBuildingSeen == 0) { ++g_doorResolveMissedNoBuilding; return 0; }
    if (sawRefusedCheck != 0) { ++g_doorResolveRefusedChecks; return 0; }
    ++g_doorResolveMissedNoDoor;
    return 0;
}

/* ============================ THE APPLY ============================
   MAIN THREAD, and behind EngineWritesBlocked() - the same predicate ClockWorldLive() asks, so a
   state arriving inside a world load or a teardown WAITS instead of writing into a world that is
   being built or freed.  setDoorState 0x298FC0 writes +0x380 and tail-jumps into the funnel, which
   snaps the amount, moves the Ogre node and every physics actor and crosses the openness threshold -
   so NavMesh::setDoorState 0x3A5F30 fires and the receiving game's PATHING follows (read-doors 5.2).
   IT SNAPS: no swing, no sound.  The two-tier animated applier read-doors 5.3 describes is NOT in
   this build - the REFUSED list in build/prep-p8e.md says why.
   `reason`: 1 = the first time this game hears about the door, 2 = a forced open from physics setup,
   3 = a local writer moved a door this game does not hold.  Three events, three numbers, one apply. */
/* P8n: WHO HOLDS THIS DOOR'S PATCH OF MAP.  A VIRTUAL CALL through the stored pointer (getPosition,
   vtable +0x40) followed by items.cpp's own area verdict, so it may only be asked AFTER
   DoorRowStillActive has passed (review-p8e C-1), and it is asked only when the answer changes what
   happens - never on the agreement path, which must stay free.
   1 = this game holds it, 0 = it does not, -1 = the position could not be read. */
int DoorWeHold(DoorRow* r)
{
    float pos[3];
    pos[0] = 0; pos[1] = 0; pos[2] = 0;
    if (DoorPosPod(r->door, pos) == 0) return -1;
    return (AreaVerdictForDoor(pos) == 1) ? 1 : 0;
}

/* P8n-c (review-p8n-b M-2): WHICH OF THE FOUR ANSWERS THIS GAME HAS ABOUT THE DOOR A HOLDER WORD
   NAMES.  Same discipline as DoorWeHold - the engine's own active-zone walk first, then the guarded
   virtual read of the position, then items.cpp's own verdict - but it reports a LOADING picture as
   loading instead of as a settled "mine", which is the whole of M-2(a).  Every way of failing to ask
   the question is one answer, kDoorAreaUnanswerable, and the caller stores the word for it. */
int DoorAreaForRow(DoorRow* rr)
{
    float rpos[3];
    if (rr == 0 || DoorRowStillActive(rr) == 0 || DoorPlausible(rr->door) == 0)
        return coopdoor::kDoorAreaUnanswerable;
    rpos[0] = 0; rpos[1] = 0; rpos[2] = 0;
    if (DoorPosPod(rr->door, rpos) == 0) return coopdoor::kDoorAreaUnanswerable;
    return AreaVerdictForDoorSettled(rpos);
}

/* P8n-b (review-p8n M-2): TAKE THE PENDING REPORT, ONCE.  1 = this call is the one that consumed it,
   so exactly one counter is raised for exactly one arriving report. */
int DoorTakePending(DoorHeldRow* h)
{
    return (::InterlockedExchange(&h->pendingActor, 0) != 0) ? 1 : 0;
}

/* P8n-b (review-p8n M-2): EVERY EXIT ABOVE THE ADOPT BLOCK RESOLVES THE PENDING REPORT, and each one
   resolves it TERMINALLY - a report left pending on a path that books nothing is a report the
   `received` identity cannot account for, and re-deciding it next tick on the same unusable row would
   have booked it twice or never.  Which bucket, and why, per exit:
     - the applier was REFUSED at install (its prologue did not match) - that is true for the whole
       run, so the report can never be adopted here: rowUnusable;
     - the row gave up after three corrections that did not take - the engine is refusing our writes
       on this door, so adopting a state we cannot write is a claim we cannot keep: rowUnusable;
     - a guarded read of the door faulted - this game cannot act on the row this tick and has no
       standing to serialise anybody's report: rowUnusable;
     - the engine's own active-zone walk no longer lists the row, or the key no longer rebuilds: the
       row is DROPPED, which is exactly what `unnameable` means - this game can no longer name that
       door - and it is the same book the tick loop makes when RowByKey finds nothing. */
/* T-160: THE HOLDER'S LOCK WORD, APPLIED WITH THE ENGINE'S OWN LOCK WRITES.  Called only once the two
   games agree about OPEN / CLOSED, the row has passed the active-zone walk and the key re-derivation, and
   coopdoor::DoorLockDecide said kDoorActLock.  One write per step (coopdoor::DoorLockStep), the door re-read
   after each and the row given the word the write left - so the unseen-lock test never takes this game's own
   apply for a local write.  A step that leaves the word unchanged ends the visit as ineffective, and
   kDoorLockGiveUpN such visits in a row stop the lock correction on this row until the two games agree about
   the lock again (lesson 14).  Returns a coopdoor::kDoorLockStuck* reason; kDoorLockStuckNone = applied or
   still on its way. */
int DoorApplyLock(DoorRow* r, DoorHeldRow* h)
{
    int n, lockRefused = 0;
    if (DoorHasLockPod(r->door) == 0) return coopdoor::kDoorLockStuckNoLock;
    for (n = 0; n < coopdoor::kDoorLockStepsPerVisit; ++n)
    {
        int s = 0, d = 0, l = 0, w = 0, b = 0;
        if (DoorReadPod(r->door, &s, &d, &l, &w, &b) == 0) { ++g_doorReadFault; return coopdoor::kDoorLockStuckNone; }
        {
            const int before = coopdoor::DoorLockWord(l, w);
            const int step = coopdoor::DoorLockStepAfterRefusal(coopdoor::DoorLockStep(s, before, (int)h->locked), s, lockRefused);
            if (step == coopdoor::kDoorLockStepNone)
            {
                ++g_doorLockApplied;
                ::InterlockedExchange(&r->lockIneffective, 0);
                return coopdoor::kDoorLockStuckNone;
            }
            if (step == coopdoor::kDoorLockStepWait) { ::InterlockedExchange(&g_dirty, 1); return coopdoor::kDoorLockStuckNone; }
            if ((step == coopdoor::kDoorLockStepLock && g_lockDoor == 0)
                || (step == coopdoor::kDoorLockStepUnlock && g_unlockDoor == 0))
                return coopdoor::kDoorLockStuckNoCall;
            ApplyLockPod(step, r->door);
            if ((step == coopdoor::kDoorLockStepLock || step == coopdoor::kDoorLockStepSetLocked) && g_updateGateCode != 0)
                DoorGateCodePod(r->door);
            if (step == coopdoor::kDoorLockStepLock) ++g_doorLockLocks;
            else if (step == coopdoor::kDoorLockStepUnlock) ++g_doorLockUnlocks;
            else if (step == coopdoor::kDoorLockStepClearWants) ++g_doorLockWantsCleared;
            else ++g_doorLockSetLocked;
            {
                int s2 = 0, d2 = 0, l2 = 0, w2 = 0, b2 = 0;
                if (DoorReadPod(r->door, &s2, &d2, &l2, &w2, &b2) == 0) { ++g_doorReadFault; return coopdoor::kDoorLockStuckNone; }
                ::InterlockedExchange(&r->locked, (long)coopdoor::DoorLockWord(l2, w2));
                if (coopdoor::DoorLockWord(l2, w2) == before)
                {
                    /* lockDoor refused a CLOSED door that has not settled: the next step is the direct lock write
                       (coopdoor::DoorLockStepAfterRefusal), not an ineffective visit. */
                    if (step == coopdoor::kDoorLockStepLock && s2 == coopdoor::kDoorClosed && lockRefused == 0)
                    { lockRefused = 1; continue; }
                    ++g_doorLockIneffective;
                    if (::InterlockedIncrement(&r->lockIneffective) >= coopdoor::kDoorLockGiveUpN)
                    {
                        ::InterlockedExchange(&r->lockGaveUp, 1);
                        ::InterlockedExchange(&r->lockIneffective, 0);
                        ++g_doorLockGaveUp;
                        DebugLog(std::string("[DOOR] the lock of '") + r->key + "' did not move for the engine's"
                                 " own lock writes three visits in a row - left as it is until the two games agree"
                                 " about it (doorLockGaveUp)");
                        return coopdoor::kDoorLockStuckGaveUp;
                    }
                    ::InterlockedExchange(&g_dirty, 1);   /* revisited until it takes, gives up or is stuck */
                    return coopdoor::kDoorLockStuckNone;
                }
            }
        }
    }
    ::InterlockedExchange(&g_dirty, 1);
    return coopdoor::kDoorLockStuckNone;
}

/* THE LOCK SWEEP OF ONE LIVE ROW (coopdoor::DoorLockSweepQueues): a row a no-op close marked, whether or not the
   other game has ever named the door.  The same order as ApplyToRow - the active-zone walk first, then the read,
   and the key re-derived (two virtual calls) only for a row whose word moved - and the same writes as its
   unseen-lock branch, so a door the other game has named is not queued twice. */
void DoorLockSweepRow(DoorRow* r)
{
    int state = 0, doa = 0, locked = 0, wants = 0, broken = 0, word;
    if (DoorRowStillActive(r) == 0) { ++g_doorDroppedInactive; DropRow(r); return; }
    if (DoorPlausible(r->door) == 0) return;
    if (DoorReadPod(r->door, &state, &doa, &locked, &wants, &broken) == 0) { ++g_doorReadFault; return; }
    word = coopdoor::DoorLockWord(locked, wants);
    if (coopdoor::DoorLockSweepQueues(state, (int)r->state, word, (int)r->locked) == 0) return;
    {
        char again[kDoorKeyCap];
        int gate = 0;
        again[0] = 0;
        if (DoorKeyBuild(r->door, again, kDoorKeyCap, &gate, 0) == 0 || std::strcmp(again, r->key) != 0)
        { ++g_doorRowRecycled; DropRow(r); return; }
    }
    ::InterlockedExchange(&r->locked, (long)word);
    ::InterlockedExchange(&r->localWrite, 1);
    ++g_doorLockUnseenLocal;
    QueueOut(r->key, state, word, 1);
}

int ApplyToRow(DoorRow* r, DoorHeldRow* h, int reason)
{
    if (g_setterOk == 0 || g_setDoorState == 0)
    { ++g_doorApplyNoSetter; if (DoorTakePending(h) != 0) ++g_doorActorRowUnusable; return 0; }
    if (r->gaveUp != 0) { if (DoorTakePending(h) != 0) ++g_doorActorRowUnusable; return 0; }
    /* FIRST, AND BEFORE EVERY READ AND EVERY CALL BELOW (review-p8e C-1).  The key re-derivation
       further down is itself the unsafe operation - it makes two virtual calls through this stored
       pointer - so it can never be what establishes the pointer is safe to use. */
    if (DoorRowStillActive(r) == 0)
    {
        ++g_doorDroppedInactive;
        if (DoorTakePending(h) != 0) ++g_doorActorUnnameable;
        DropRow(r);
        return 0;
    }
    if (DoorPlausible(r->door) == 0)
    { if (DoorTakePending(h) != 0) ++g_doorActorRowUnusable; return 0; }
    {
        int state = 0, doa = 0, locked = 0, wants = 0, broken = 0;
        if (DoorReadPod(r->door, &state, &doa, &locked, &wants, &broken) == 0)
        { ++g_doorReadFault; if (DoorTakePending(h) != 0) ++g_doorActorRowUnusable; return 0; }
        /* AND THEN the key is re-derived and compared, as a SECOND test and not the first one: the
           walk test above has already established that the engine still has this object, so the two
           virtual calls in here are safe to make.  It answers the other question - is this the same
           DOOR - which membership in the walk cannot: an address recycled by another live building
           passes the walk and fails this.  A row whose key no longer rebuilds is FREED, never
           written. */
        {
            char again[kDoorKeyCap];
            int gate = 0;
            again[0] = 0;
            if (DoorKeyBuild(r->door, again, kDoorKeyCap, &gate, 0) == 0 || std::strcmp(again, r->key) != 0)
            {
                ++g_doorRowRecycled;
                if (DoorTakePending(h) != 0) ++g_doorActorUnnameable;
                DropRow(r);
                return 0;
            }
        }
        /* ---- P8n STEP 1: AN ACTOR REPORT FROM THE PEER, CONSUMED EXACTLY ONCE.  Only the holder
           serialises, so the report is adopted here or it is dropped here, and either way the
           pending flag falls - a report that stayed pending would be re-decided every tick. ---- */
        if (h->pendingActor != 0)
        {
            const int weHoldIt = DoorWeHold(r);
            const int what = coopdoor::DoorRecvDecide(coopdoor::kDoorOriginActor, weHoldIt);
            ::InterlockedExchange(&h->pendingActor, 0);
            if (what == coopdoor::kDoorRecvAdopt)
            {
                /* THE HOLDER ADOPTS AND REPUBLISHES.  Adopting makes the reported state this game's
                   own answer for the door; the apply below then makes this game's engine agree, and
                   the republish is what lets every other game - the reporter included - stop. */
                ::InterlockedExchange(&h->state, (long)h->actorState);
                if (coopdoor::DoorLockWordValid((int)h->actorLocked) != 0)
                    ::InterlockedExchange(&h->locked, h->actorLocked);
                ::InterlockedExchange(&r->localWrite, 0);
                ::InterlockedExchange(&r->holderWord, 1);
                ::InterlockedExchange(&r->lockGaveUp, 0);
                ::InterlockedExchange(&r->lockStuck, 0);
                QueueOut(r->key, (int)h->actorState, (int)h->locked, 0);
                ++g_doorAdopted;
                /* P8n-b (review-p8n M-1): AND THE APPLY BELOW IS BOOKED AS THE HOLDER'S WORD.  `reason`
                   is computed by the caller from r->holderWord BEFORE this adopt can set it, so every
                   holder adopt-apply was landing in the arrival span and appliedHolderWord could not
                   see an adoption at all.  A forced open from physics setup still outranks it (F633),
                   which is why reason 2 is left alone.  The identity
                   doorApplied = onArrival + reapplyAfterForcedOpen + appliedHolderWord therefore holds
                   with an adoption counted as the holder's word, which is what the docs say it is. */
                if (reason != 2) reason = 3;
            }
            else ++g_doorActorIgnoredNotHolder;
        }
        /* P8n: NOTHING HAS BEEN HEARD ABOUT THIS DOOR YET.  The row was visited only so the actor
           report above could be consumed - there is no holder answer to compare a live state against,
           and the terms the apply reasons are told apart by must survive untouched until there is. */
        if (h->state != 0 && h->state != 1) return 0;
        /* ---- P8n STEP 2: THE DECISION, made by src/common/doorsync.h and not by this file. ---- */
        {
            coopdoor::DoorDecideIn in;
            int act = 0;
            in.liveState   = state;
            in.holderState = (int)h->state;
            in.localWrite  = (int)r->localWrite;
            in.forcedOpen  = (int)r->forcedOpen;
            in.weHold      = -1;
            in.gaveUp      = (int)r->gaveUp;
            in.gaveUpChurn = (int)r->gaveUpChurn;
            act = coopdoor::DoorDecide(in);
            /* kDoorActReport is the ONLY verdict that depends on who holds the door, so the verdict
               is asked there and nowhere else rather than being computed up front and carried. */
            if (act == coopdoor::kDoorActReport)
            {
                in.weHold = DoorWeHold(r);
                act = coopdoor::DoorDecide(in);
            }
            /* T-160: THE STATES AGREE - NOW THE LOCK.  The same last-actor rule decides it
               (coopdoor::DoorLockDecide), and agreement is only agreement when the lock agrees too. */
            if (act == coopdoor::kDoorActNothing && state == (int)h->state)
            {
                const int word = coopdoor::DoorLockWord(locked, wants);
                /* A LOCK NO DETOUR SAW (coopdoor::DoorLockUnseenWrite).  The row holds the word this game last
                   saw or wrote, so a live word that differs is this game's own world - the NPC lock action
                   0x337630 locking a door that was already shut is the known writer.  It is a local change
                   like a hooked one: queued (for the holder, or as the holder's word) and never undone. */
                if (coopdoor::DoorLockUnseenWrite(word, (int)r->locked) != 0)
                {
                    ::InterlockedExchange(&r->locked, (long)word);
                    ::InterlockedExchange(&r->localWrite, 1);
                    ++g_doorLockUnseenLocal;
                    QueueOut(r->key, state, word, 1);
                }
                act = coopdoor::DoorLockDecide(word, (int)h->locked, (int)r->localWrite, -1);
                if (act == coopdoor::kDoorActReport)
                    act = coopdoor::DoorLockDecide(word, (int)h->locked, (int)r->localWrite, DoorWeHold(r));
                if (act == coopdoor::kDoorActLock)
                {
                    int why = coopdoor::kDoorLockStuckGaveUp;
                    if (r->lockGaveUp == 0)
                    {
                        why = DoorApplyLock(r, h);
                        if (why == coopdoor::kDoorLockStuckNone) return 0;
                    }
                    /* THE LOCK CANNOT BE APPLIED HERE (no DoorLock, no lock call, or a lock that gave up) and the
                       open / closed state agrees: the row takes the agreement clears below as for full
                       agreement, and the event is counted once - the latch falls when the locks agree, when this
                       game adopts a word, or when a new holder word lands. */
                    if (::InterlockedExchange(&r->lockStuck, 1) == 0)
                    {
                        if (why == coopdoor::kDoorLockStuckNoLock) ++g_doorLockNoLockObject;
                        else if (why == coopdoor::kDoorLockStuckNoCall) ++g_doorLockNoCall;
                    }
                    act = coopdoor::kDoorActNothing;
                }
                else if (act == coopdoor::kDoorActNothing)
                {
                    ::InterlockedExchange(&r->lockGaveUp, 0);
                    ::InterlockedExchange(&r->lockStuck, 0);
                }
            }
            if (act == coopdoor::kDoorActGaveUp)
            {
                /* P8n-b (review-p8n M-4, MANAGER RULING): a row given up on churn is not dead for the
                   run - thirty seconds of continuous agreement re-arms it.
                   P8n-c (review-p8n-b M-3, MANAGER RULING): AND THAT CLOCK IS NO LONGER READ HERE.
                   Reading it here meant holding g_dirty up for the whole thirty seconds so the tick
                   would keep running, which ran the WHOLE of DoorsTick - a 256-slot publish scan, a
                   256-slot apply scan, DoorRowStillActive's linear walk-list test per row and a key
                   re-derivation with two virtual calls - at frame rate for thirty seconds because ONE
                   row had given up.  DoorChurnReArmSweep now reads it at most once a second and only
                   while g_doorChurnWatch says some row is given up.  Nothing else is done here. */
                /* T-160: THE LANDING BOUND OF A SWING THIS GAME PLAYED.  A row given up on churn never reaches
                   the waiting arm below, so a swing that has not landed inside the bound stops being this game's
                   own write here - a later change to the door is the world's again.  No snap: the row is given
                   up. */
                if (coopdoor::DoorPlayOverdue((int)r->playTarget, (unsigned long)(long)r->playStartMs, (unsigned long)g_doorUnpausedMs) != 0)
                {
                    ::InterlockedExchange(&r->playTarget, -1);
                    ++g_doorPlayOverdue;
                }
                return 0;
            }
            if (act == coopdoor::kDoorActNothing)
            {
                /* AGREEMENT IS THE COMMON CASE AND IT MUST BE FREE.  It also CLEARS every term the
                   apply reasons are told apart by: whatever moved this door last, the two games agree
                   about it now, so the next disagreement is a new event and not a continuation. */
                ::InterlockedExchange(&r->forcedOpen, 0);
                ::InterlockedExchange(&r->localWrite, 0);
                ::InterlockedExchange(&r->ineffective, 0);
                ::InterlockedExchange(&r->holderWord, 0);
                ::InterlockedExchange(&r->corrected, 0);
                ::InterlockedExchange(&r->undone, 0);
                /* P8n-b (L-6): agreement is what an outstanding actor report was waiting for. */
                ::InterlockedExchange(&r->reportPending, 0);
                /* P8n-c (M-1): AND AGREEMENT IS THE ONLY THING THAT CLEARS THE ABANDON.  This is the
                   terminal-state compare - the two games agree about this door now - so whatever made
                   this game give up reporting it is over and a LATER disagreement is a new event
                   entitled to its own fifteen seconds.  Nothing else clears it, which is what makes
                   "a door nobody holds does not report for ever" true rather than claimed. */
                ::InterlockedExchange(&r->reportAbandoned, 0);
                return 0;
            }
            if (act == coopdoor::kDoorActWaitMidSwing)
            {
                /* THE DOOR IS SWINGING.  h->state is only ever 0 or 1, so a door at 2 OPENING or
                   3 CLOSING can never equal it - which is exactly how the shipped code came to snap
                   an animating door every tick (T236d: every revert read was=2 now=0).  Leave it,
                   and come back: the ramp's landing writes a terminal state through the funnel, which
                   is a change, which marks the tick dirty on its own. */
                ++g_doorMidSwingSkipped;
                ::InterlockedExchange(&g_dirty, 1);
                /* ... unless it is a swing THIS game played that has not landed inside the bound: it is
                   finished with a snap to the holder's state, which is what the swing was for. */
                if (coopdoor::DoorPlayOverdue((int)r->playTarget, (unsigned long)(long)r->playStartMs, (unsigned long)g_doorUnpausedMs) == 0)
                    return 0;
                ::InterlockedExchange(&r->playTarget, -1);
                ++g_doorPlayOverdue;
                DebugLog(std::string("[DOOR] a swing this game played on '") + r->key + "' did not land inside "
                         + "the bound - finished with a snap to the holder's state (doorPlayOverdue)");
            }
            if (act == coopdoor::kDoorActReport)
            {
                /* LAST ACTOR WINS.  An engine writer moved this door on this game and this game does
                   not hold it: the change was already queued for the holder at the funnel (NoteDoor's
                   QueueOut) and the applier's whole job here is to NOT put it back.  This is the
                   number that used to be doorRevertedNotHolder = 2652. */
                ++g_doorLastActorKept;
                /* P8n-b (review-p8n L-6): AND THE REPORT IS RECURRENCE-COVERED.  The change was queued
                   for the holder ONCE, at the funnel, and this verdict did not even mark the tick
                   dirty - so a report lost on the wire, or one whose holder never adopted it, left the
                   two games disagreeing with nothing ever asking again.  The row now carries the
                   report until the holder's word lands (the agreement and the apply both clear it) or
                   the bound expires, and both outcomes are numbers. */
                /* P8n-c (review-p8n-b M-1): AND THE ABANDON IS STICKY.  P8n-b cleared reportPending
                   on the abandon and left nothing on the row, so the next dirty tick saw pending == 0
                   and opened a fresh fifteen-second episode - the bound the docs state was never
                   enforced past one episode.  The latch is the enforcement, and only agreement lifts
                   it (the terminal-state arm above). */
                {
                    const unsigned long nowMs = GetCurrentTickMs();
                    const int what = coopdoor::DoorReportDecide(
                        (int)r->reportAbandoned, (int)r->reportPending, nowMs,
                        (unsigned long)(long)r->reportFirstMs, (unsigned long)(long)r->reportLastMs);
                    if (what == coopdoor::kDoorReportSuppressed)
                    {
                        /* No new episode, no send, and the tick is NOT marked dirty - there is
                           nothing left to come back for until the two games agree again. */
                        ++g_doorReportSuppressedAbandoned;
                        return 0;
                    }
                    if (what == coopdoor::kDoorReportAbandon)
                    {
                        ::InterlockedExchange(&r->reportPending, 0);
                        ::InterlockedExchange(&r->reportAbandoned, 1);
                        ++g_doorReportAbandoned;
                        return 0;
                    }
                    if (what == coopdoor::kDoorReportOpen)
                    {
                        ::InterlockedExchange(&r->reportPending, 1);
                        ::InterlockedExchange(&r->reportFirstMs, (long)nowMs);
                        ::InterlockedExchange(&r->reportLastMs, (long)nowMs);
                    }
                    else if (what == coopdoor::kDoorReportRetry)
                    {
                        QueueOut(r->key, state, (int)r->locked, 1);
                        ::InterlockedExchange(&r->reportLastMs, (long)nowMs);
                        ++g_doorReportRetried;
                    }
                    ::InterlockedExchange(&g_dirty, 1);
                }
                return 0;
            }
            if (act == coopdoor::kDoorActAdoptLocal)
            {
                /* ... and on the holder the same event makes this game's own state the answer.  The
                   change is already on its way out as a holder publish; all that is needed here is to
                   stop this game reverting its own door on the next tick. */
                ::InterlockedExchange(&h->state, (long)state);
                ::InterlockedExchange(&h->locked, (long)coopdoor::DoorLockWord(locked, wants));
                ::InterlockedExchange(&r->localWrite, 0);
                ::InterlockedExchange(&r->holderWord, 0);
                ::InterlockedExchange(&r->lockGaveUp, 0);
                ::InterlockedExchange(&r->lockStuck, 0);
                ++g_doorAdoptedLocal;
                return 0;
            }
        }
        /* T-160: PLAYED OR SNAPPED (coopdoor::DoorApplyHow).  A played swing is measured by what the
           engine call left behind - OPENING or CLOSING - and a call that left the door unmoved falls
           through to the snap on the same visit. */
        int expect = (int)h->state;
        {
            const int target = (int)h->state;
            OpenCloseDoorFn call = (target == coopdoor::kDoorOpen) ? g_playOpen : g_playClose;
            int how = coopdoor::DoorApplyHow(reason, state, target, call != 0 ? 1 : 0);
            if (how == coopdoor::kDoorApplyPlay)
            {
                int s1 = 0, d1 = 0, l1 = 0, w1 = 0, b1 = 0;
                ::InterlockedExchange(&r->playTarget, (long)target);
                ::InterlockedExchange(&r->playStartMs, g_doorUnpausedMs);
                ApplyPlayPod(call, r->door);
                if (DoorReadPod(r->door, &s1, &d1, &l1, &w1, &b1) != 0 && s1 == coopdoor::DoorPlayStartedState(target))
                {
                    expect = s1;
                    ++g_doorPlayStarted;
                    ::InterlockedExchange(&g_dirty, 1);   /* the WaitMidSwing arm watches the landing bound */
                }
                else
                {
                    ::InterlockedExchange(&r->playTarget, -1);
                    ++g_doorPlayRefused;
                    how = coopdoor::kDoorApplySnap;
                }
            }
            if (how == coopdoor::kDoorApplySnap)
            {
                ApplySetterPod(r->door, target);
                ++g_doorSnapped;
            }
        }
        {
            /* LESSON 14: A CORRECTIVE MEASURES ITS OWN EFFECT AND GIVES UP.  A fix that cannot fail
               visibly retries forever - 22,375 times on one character, in this project's own
               history - and "it ran 22,000 times" is not evidence it did anything. */
            int s2 = 0, d2 = 0, l2 = 0, w2 = 0, b2 = 0;
            if (DoorReadPod(r->door, &s2, &d2, &l2, &w2, &b2) == 0 || s2 != expect)
            {
                ++g_doorApplyIneffective;
                if (::InterlockedIncrement(&r->ineffective) >= 3)
                {
                    ::InterlockedExchange(&r->gaveUp, 1);
                    ++g_doorGaveUp;
                    DebugLog(std::string("[DOOR] giving up on '") + r->key + "' - three corrections in a row"
                             " did not change the state.  The door stays as this game's own writers left it.");
                }
                return 0;
            }
            ::InterlockedExchange(&r->ineffective, 0);
            ::InterlockedExchange(&r->state, (long)expect);
        }
        ++g_doorApplied;
        if (reason == 2) ++g_doorReapplyAfterForcedOpen;
        else if (reason == 3) ++g_doorAppliedHolderWord;
        else ++g_doorAppliedOnArrival;
        ::InterlockedIncrement(&r->applies);
        /* P8n: THE CORRECTION IS NOW ON THE RECORD AS A CORRECTION.  If a non-self writer moves this
           door before the two games agree, NoteDoor consumes this flag and counts an `undone` - which
           is the measurement the shipped give-up was missing. */
        ::InterlockedExchange(&r->corrected, 1);
        ::InterlockedExchange(&r->forcedOpen, 0);
        ::InterlockedExchange(&r->localWrite, 0);
        ::InterlockedExchange(&r->holderWord, 0);
        ::InterlockedExchange(&r->reportPending, 0);   /* P8n-b (L-6): the holder's word landed */
        return 1;
    }
}

/* ============ P8n-c (review-p8n-b M-3): THE RE-ARM CLOCK, ON ITS OWN CADENCE ============
   MAIN THREAD, above the idle gate, at most once every kDoorReArmSweepMs and only while
   g_doorChurnWatch says at least one row has given up on churn - so the idle cost is one interlocked
   read per tick and the busy cost is one 256-slot pass per SECOND over rows that have given up, not a
   whole DoorsTick per FRAME for thirty seconds.  It makes no engine WRITE: it reads the door's own
   state through the guarded POD read, after the engine's own active-zone walk has listed the row, and
   asks src/common/doorsync.h whether thirty seconds of continuous agreement have passed.  A row it
   re-arms marks the tick dirty ONCE, so the applier gets a look at a row it may now correct again. */
void DoorChurnReArmSweep(unsigned long nowMs)
{
    int any = 0;
    int i;
    for (i = 0; i < kDoorRows; ++i)
    {
        DoorRow* r = &g_rows[i];
        if (r->door == 0 || r->ready == 0) continue;
        if (r->gaveUpChurn == 0 || r->gaveUp != 0) continue;
        any = 1;                                  /* a row still given up keeps the sweep armed */
        if (DoorRowStillActive(r) == 0) continue; /* never a call through a pointer the engine let go */
        {
            const DoorHeldRow* h = HeldFor(r->key, 0);
            coopdoor::DoorChurn c;
            int s2 = 0, d2 = 0, l2 = 0, w2 = 0, b2 = 0;
            c.undone = (long)r->undone;
            c.firstMs = (unsigned long)(long)r->churnFirstMs;
            c.gaveUp = 1;
            c.agreeing = (int)r->churnAgreeing;
            c.agreeSinceMs = (unsigned long)(long)r->churnAgreeMs;
            if (h != 0 && (h->state == 0 || h->state == 1)
                && DoorReadPod(r->door, &s2, &d2, &l2, &w2, &b2) != 0
                && coopdoor::DoorStateIsTerminal(s2) != 0 && s2 == (int)h->state)
            {
                if (coopdoor::DoorChurnNoteAgreement(&c, nowMs) != 0) ++g_doorChurnReArmed;
            }
            else coopdoor::DoorChurnNoteDisagreement(&c);
            ::InterlockedExchange(&r->undone, c.undone);
            ::InterlockedExchange(&r->churnFirstMs, (long)c.firstMs);
            ::InterlockedExchange(&r->gaveUpChurn, (long)c.gaveUp);
            ::InterlockedExchange(&r->churnAgreeing, (long)c.agreeing);
            ::InterlockedExchange(&r->churnAgreeMs, (long)c.agreeSinceMs);
            if (c.gaveUp == 0)
            {
                ::InterlockedExchange(&r->churnLogged, 0);
                ::InterlockedExchange(&g_dirty, 1);
            }
        }
    }
    if (any == 0) ::InterlockedExchange(&g_doorChurnWatch, 0);
}

} // namespace

/* ============================ THE PUBLIC HALF ============================ */

/* P15 fold 1 (review-p15 CRASH 1a), MAIN THREAD: see doors.h.  The building is live (the caller is about to call setDestroyed on it),
   so its door array is read once, POD, before the flip; a row is dropped when its recorded owner IS the building, or its door is one
   the array lists (a row registered with no owner).  A row still being claimed (ready 0) is left to its claimer, as the purge sweep
   does - the owner-route test above refuses it later if its door is gone.  The holder table is keyed by KEY and is kept: the doors a
   repair makes under the same keys take the holder's state as ordinary new rows. */
void DoorsForgetOwner(void* building)
{
    void* elems[kDoorArrayMax];
    int cnt = 0, raw = 0, i, j;
    if (building == 0) return;
    if (DoorArrayPod(building, elems, kDoorArrayMax, &cnt, &raw) == 0) cnt = 0;
    for (i = 0; i < kDoorRows; ++i)
    {
        DoorRow* r = &g_rows[i];
        void* d = r->door;
        int mine;
        if (d == 0 || r->ready == 0) continue;
        mine = (r->owner == building) ? 1 : 0;
        for (j = 0; mine == 0 && j < cnt; ++j)
            if (elems[j] == d) mine = 1;
        if (mine == 0) continue;
        ++g_doorRowsForgotten;
        DropRow(r);
    }
}

void InstallDoors()
{
    g_base = (uintptr_t)::GetModuleHandleA(0);
    g_mainThread = ::GetCurrentThreadId();   /* provisional (preload); DoorsTick replaces it */
    std::memset(g_rows, 0, sizeof(g_rows));
    std::memset(g_held, 0, sizeof(g_held));
    std::memset(g_out, 0, sizeof(g_out));

    /* THE APPLIER IS VERIFIED BEFORE IT IS EVER CALLED.  It is not hooked, so nothing else would
       ever notice a wrong address: 29 bytes that write +0x380 and tail-jump into the funnel. */
    g_setterOk = PrologueMatches(kSetDoorStateRva, kPrologueSetState, 8);
    if (g_setterOk != 0) g_setDoorState = (SetDoorStateFn)(g_base + kSetDoorStateRva);
    else ErrorLog("[DOOR] setDoorState 0x298FC0 does not carry the prologue read-doors read out of this exe -"
                  " NO door state is applied on this game, and doorApplyNoSetter counts every refusal");

    if (PrologueMatches(kSetDoorOpenAmountRva, kPrologueFunnel, 8) == 0)
        ErrorLog("[DOOR] setDoorOpenAmount 0x298CD0 prologue mismatch - the funnel is NOT hooked, so no door change is seen");
    else if (coop::AddHook((void*)(g_base + kSetDoorOpenAmountRva), (void*)&detour_setDoorOpenAmount, (void**)&orig_setDoorOpenAmount) != coop::SUCCESS)
        ErrorLog("[DOOR] AddHook setDoorOpenAmount 0x298CD0 FAILED - no door change is seen");
    else ++g_hooked;

    /* The applier's open / close calls are judged on the engine's own bytes, read here BEFORE the detours below patch them;
       they then enter through those detours, inside the applying bracket, as the lock calls do. */
    const int openBytesOk = (kOpenDoorRva != 0 && PrologueMatches(kOpenDoorRva, kPrologueOpenDoor, 8) != 0) ? 1 : 0;
    const int closeBytesOk = (kCloseDoorRva != 0 && PrologueMatches(kCloseDoorRva, kPrologueCloseDoor, 8) != 0) ? 1 : 0;
    if (openBytesOk == 0)
        ErrorLog("[DOOR] openDoor 0x297000 prologue mismatch - the OPENING edge is not named by its originator");
    else if (coop::AddHook((void*)(g_base + kOpenDoorRva), (void*)&detour_openDoor, (void**)&orig_openDoor) != coop::SUCCESS)
        ErrorLog("[DOOR] AddHook openDoor 0x297000 FAILED");
    else ++g_hooked;

    if (closeBytesOk == 0)
        ErrorLog("[DOOR] closeDoor 0x298C10 prologue mismatch - the CLOSING edge is not named by its originator");
    else if (coop::AddHook((void*)(g_base + kCloseDoorRva), (void*)&detour_closeDoor, (void**)&orig_closeDoor) != coop::SUCCESS)
        ErrorLog("[DOOR] AddHook closeDoor 0x298C10 FAILED");
    else ++g_hooked;

    /* T-160: THE CALLS THE APPLIER MAKES, each taken only when its prologue matched.  A refused open /
       close leaves that direction SNAPPED (DoorApplyHow); a refused lock call leaves that step undone and
       counted (doorLockNoCall). */
    if (openBytesOk != 0) g_playOpen = (OpenCloseDoorFn)(g_base + kOpenDoorRva);
    if (closeBytesOk != 0) g_playClose = (OpenCloseDoorFn)(g_base + kCloseDoorRva);
    if (kUnlockDoorRva != 0 && PrologueMatches(kUnlockDoorRva, kPrologueUnlockDoor, 8) != 0) g_unlockDoor = (LockDoorFn)(g_base + kUnlockDoorRva);
    else ErrorLog("[DOOR] unlockDoor 0x569940 prologue mismatch - the holder's unlock is not applied on this game");
    if (kUpdateGateCodeRva != 0 && PrologueMatches(kUpdateGateCodeRva, kPrologueUpdateGateCode, 9) != 0) g_updateGateCode = (LockDoorFn)(g_base + kUpdateGateCodeRva);
    else ErrorLog("[DOOR] updateGateCodeState 0x297250 prologue mismatch - a lock this game applies leaves the door's gate code as it was");

    if (kLockDoorRva == 0 || PrologueMatches(kLockDoorRva, kPrologueLockDoor, 8) == 0)
        ErrorLog("[DOOR] lockDoor 0x2969B0 prologue mismatch - the re-lock of a closing door is not seen and the holder's lock is not applied");
    else
    {
        g_lockDoor = (LockDoorFn)(g_base + kLockDoorRva);
        if (coop::AddHook((void*)(g_base + kLockDoorRva), (void*)&detour_lockDoor, (void**)&orig_lockDoor) != coop::SUCCESS)
            ErrorLog("[DOOR] AddHook lockDoor 0x2969B0 FAILED - the re-lock of a closing door is not seen");
        else ++g_hooked;
    }
    if (kLockButtonRva == 0 || PrologueMatches(kLockButtonRva, kPrologueLockButton, 8) == 0)
        ErrorLog("[DOOR] lockButton 0x5465D0 prologue mismatch - a lock press is not seen");
    else
    {
        g_lockButton = (LockButtonFn)(g_base + kLockButtonRva);
        if (coop::AddHook((void*)(g_base + kLockButtonRva), (void*)&detour_lockButton, (void**)&orig_lockButton) != coop::SUCCESS)
            ErrorLog("[DOOR] AddHook lockButton 0x5465D0 FAILED - a lock press is not seen");
        else ++g_hooked;
    }

    {
        char b[288];
        _snprintf(b, 287, "[DOOR] E45 installed: %d of 5 detours armed, applier %s, swing calls %d/2, lock calls %d/2"
                          " (decision 40 - a door belongs to the game that HOLDS its patch of map)",
                  g_hooked, g_setterOk != 0 ? "ready" : "REFUSED",
                  (g_playOpen != 0 ? 1 : 0) + (g_playClose != 0 ? 1 : 0),
                  (g_lockDoor != 0 ? 1 : 0) + (g_unlockDoor != 0 ? 1 : 0));
        b[287] = 0;
        DebugLog(b);
    }
}

/* review-p8e C-1.  ANY THREAD, INSIDE THE ENGINE'S OWN ZONE TEARDOWN.  Two interlocked integer writes
   and nothing else - no walk, no log, no allocation, no lock - which is the only thing an off-thread
   detour may safely do here (the same rule P7a wrote for the box cache's half of this hook).  The
   sweep itself is main-thread work and happens on the next DoorsTick; the `dirty` mark is what makes
   that tick happen when nothing else has changed.  AN INVALIDATION MUST NOT BE SKIPPED, so this runs
   before the deactivate detour's own thread test and again on the way out. */
void DoorsOnZoneDeactivate()
{
    ::InterlockedIncrement64(&g_doorZoneDeactivations);
    ::InterlockedExchange(&g_doorPurgePending, 1);
    ::InterlockedExchange(&g_dirty, 1);
}

/* P8n-b (review-p8n L-8).  session.cpp's OnDoorState refuses a message whose trailing origin byte is
   neither 0 nor 1 - it used to fall through to the HOLDER arm, so a corrupted byte, or a future
   build's third origin, was taken as authority.  The counter lives here with the rest of the door
   road's numbers rather than in the session layer. */
void NoteDoorOriginUnknown() { ::InterlockedIncrement64(&g_doorOriginUnknown); }

void SetDoorsOn(bool on) { g_on = on; DebugLog(std::string("[DOOR] doors ") + (on ? "on" : "off")); }

void DoorsWorldTeardown()
{
    /* P8n-c (review-p8n-b): THE MEMSET BELOW DROPS EVERY REPORT STILL OUT, AND IT SAID NOTHING.
       An actor report the holder never answered was neither retried nor abandoned nor agreed with -
       it simply ceased to exist with the world, and the `report` identities could not account for it
       (F172).  Counted before the tables are cleared, which is the only moment it can be counted. */
    {
        int i;
        for (i = 0; i < kDoorRows; ++i)
            if (g_rows[i].door != 0 && g_rows[i].ready != 0 && g_rows[i].reportPending != 0)
                ++g_doorReportDroppedTeardown;
    }
    /* The registry names doors in the world being destroyed and the holder table names keys that
       belong to it: a recycled address or a re-used key must never answer for the next world. */
    std::memset(g_rows, 0, sizeof(g_rows));
    std::memset(g_held, 0, sizeof(g_held));
    std::memset(g_out, 0, sizeof(g_out));
    ::InterlockedExchange(&g_dirty, 0);
    ::InterlockedExchange(&g_doorLockSweepPending, 0);
    ::InterlockedExchange(&g_doorPurgePending, 0);   /* the rows it would have swept are gone with the world */
    ::InterlockedExchange(&g_doorChurnWatch, 0);     /* P8n-c: and no row survives to be re-armed */
}

/* ============ B12 (decision 52): ONE DOOR CHANGE, SAVED AND READ BACK ============
   Three fields - the key, the terminal state and the lock - and nothing else: `gen` is deliberately NOT
   saved, because a replay must take a FRESH one (ApplyDoorState filters an old gen out per publisher,
   doors.cpp:1194, so a replayed old number would be silently dropped by the receiver). The key cannot
   carry a tab, a CR or an LF or the line would split, so such a key is refused rather than written. */
int DoorStateToBytes(const char* key, int state, int locked, std::vector<char>* out)
{
    if (key == 0 || out == 0 || key[0] == 0) return 0;
    if (state != coopdoor::kDoorClosed && state != coopdoor::kDoorOpen) return 0;
    std::string o = "d1\t";
    for (int i = 0; key[i] != 0; ++i)
    {
        if (key[i] == '\t' || key[i] == '\r' || key[i] == '\n') return 0;
        o += key[i];
    }
    o += '\t';
    o += (state == coopdoor::kDoorOpen) ? '1' : '0';
    o += '\t';
    o += (char)('0' + (coopdoor::DoorLockWordValid(locked) != 0 ? locked : 0));
    out->assign(o.begin(), o.end());
    return 1;
}
int DoorsQueueReplay(const std::vector<char>& payload)
{
    std::string s(payload.begin(), payload.end());
    std::vector<std::string> f; std::string::size_type at = 0;
    while (at <= s.size())
    {
        const std::string::size_type t = s.find('\t', at);
        if (t == std::string::npos) { f.push_back(s.substr(at)); break; }
        f.push_back(s.substr(at, t - at)); at = t + 1;
    }
    if (f.size() != 4 || f[0] != "d1" || f[1].empty() || f[1].size() >= (size_t)kDoorKeyCap
        || (f[2] != "0" && f[2] != "1") || f[3].size() != 1 || f[3][0] < '0' || f[3][0] > '3')
    {
        ++g_doorQueuePayloadRefused;
        ErrorLog("[DOOR] outage queue: a journaled door change could not be read back and is DROPPED - that"
                 " change is lost. This is a defect and not a state: the journal is written and read by one"
                 " build and the whole file is rewritten on every change.");
        return -1;   /* B12-c (review-b12 M-4): a BAD ENTRY, not a replay. It leaves the file and is counted as what it is */
    }
    /* B12-d (recheck-b12): THE VERDICT IS ASKED HERE, NOW, before anything is published - the journal
       could only see 'no notebook', which says nothing about who holds the door. With the notebook back
       this game's own area picture answers; only a SETTLED 'mine' earns the holder's word. Anything else
       is dropped and counted as not-mine-now (the store books droppedNotMineNow on the -2 below), and
       the other game's own state stands - exactly what the zone arm does (decision 40 asked at the write).
       THE GEN IS TAKEN NOW, not saved with the entry - see DoorStateToBytes above. */
    {
        DoorRow* rr = RowByKey(f[1].c_str());
        const int area = DoorAreaForRow(rr);
        if (area != coopdoor::kDoorAreaMine)
        {
            char vb[16]; _snprintf(vb, 15, "%d", area); vb[15] = 0;
            DebugLog("[DOOR] outage queue: the queued state for '" + f[1] + "' is DROPPED - now that the notebook"
                     " is back this game's area picture does not say it holds that door (verdict " + std::string(vb)
                     + ": 0 loading, 2 other, 3 nobody, 4 unanswerable), so the other game's own state stands"
                     " (decision 40 asked at the write; counted as droppedNotMineNow).");
            return -2;
        }
    }
    if (!net::SendDoorState(f[1], (f[2] == "1") ? coopdoor::kDoorOpen : coopdoor::kDoorClosed,
                            (int)(f[3][0] - '0'),
                            (unsigned int)::InterlockedIncrement(&g_gen), coopdoor::kDoorOriginHolder,
                            0))   /* M7b slice 4 (C4): no position read here - WORLD on the notebook road (sideNoArea) */
        return 0;
    ++g_doorPublished;
    DebugLog("[DOOR] outage queue: re-published '" + f[1] + "' state=" + f[2] + " lockWord=" + f[3]
             + " with a fresh generation - it was refused while the notebook was unreachable and is written"
             " now (decision 52)");
    return 1;
}

void DoorsOnLinkUp()
{
    /* The weather pattern's other half: a game that joins late has been told nothing.  Every door
       this game has seen in a terminal state is re-offered; the drain refuses the ones this game
       does not hold, so a non-holder's re-offer costs one counter and no traffic. */
    for (int i = 0; i < kDoorRows; ++i)
    {
        if (g_rows[i].door == 0 || g_rows[i].ready == 0) continue;
        if (g_rows[i].state != 0 && g_rows[i].state != 1) continue;
        /* P8n: reportIfNotHolder = 0.  A re-offer is "here is what I hold", never "here is what my
           own world did" - a joining game must not push its stale local states at the holder. */
        QueueOut(g_rows[i].key, (int)g_rows[i].state, (int)g_rows[i].locked, 0);
        ++g_doorRepublishedOnLink;
    }
}

void ApplyDoorState(const std::string& key, int state, int locked, unsigned int gen, unsigned int fromPeer,
                    int origin)
{
    if (key.empty() || key.size() >= (size_t)kDoorKeyCap || (state != 0 && state != 1)
        || coopdoor::DoorLockWordValid(locked) == 0)
    { ++g_doorMsgMalformed; return; }
    {
        DoorHeldRow* h = HeldFor(key.c_str(), 1);
        if (h == 0) return;
        /* NEWEST WINS, and a change of PUBLISHER resets the comparison: an area can change hands and
           the new holder's own counter is not the old one's.  Actor reports and holder publishes come
           off the SAME per-game counter, so one ordering covers both. */
        /* M7b slice 4 fold 1 (F2): the publisher keyed as a PLAYER - a road switch keeps the age check */
        if (coopdoor::DoorGenStale(h->fromPeer, h->gen, (long)net::PlayerKeyNow(fromPeer), (long)gen) != 0) return;
        if (origin == coopdoor::kDoorOriginActor)
        {
            /* P8n-c (review-p8n-b M-2b): the ordering state is taken by a message that is ACCEPTED.
               An actor report always is - the holder rule decides it later, in DoorsTick. */
            h->gen = (long)gen; h->fromPeer = (long)net::PlayerKeyNow(fromPeer);
            /* ... and its lock word waits beside its state: it becomes this game's answer only if this
               game holds the door and adopts the report. */
            ::InterlockedExchange(&h->actorLocked, (long)locked);
            /* P8n: AN ACTOR REPORT DOES NOT BECOME THE HOLDER'S ANSWER HERE.  Whether this game holds
               the door needs the door's position - a virtual call through a stored pointer - so the
               question waits for DoorsTick, where the active-zone walk has already passed. */
            ++g_doorActorReceived;
            ::InterlockedExchange(&h->actorState, (long)state);
            if (::InterlockedExchange(&h->pendingActor, 1) != 0) ++g_doorActorSuperseded;
        }
        else
        {
            /* THE HOLDER HAS SPOKEN, AND THAT SUPERSEDES A LOCAL ACTOR'S CLAIM.  This is the whole of
               the conflict rule: two actors inside one round trip resolve to the holder's most recent
               adopted state, because the holder is the serialisation point and nothing else is. */
            DoorRow* rr = RowByKey(key.c_str());
            /* ---- P8n-b (review-p8n M-5, MANAGER RULING): A HOLDER PUBLISH FOR A DOOR **THIS** GAME
               HOLDS IS REFUSED.  The area picture is two local answers and not one agreed one, so both
               games can believe they hold one door - and under the last-actor rule that state is a
               MUTUAL REVERT with only the churn give-up as a backstop.
               P8n-c (review-p8n-b L-6) CORRECTS WHAT STOOD HERE.  It said "the peer converges on the
               actor report it receives", and in the both-hold case that is false: each game holds the
               door in its own picture, so each SENDS a holder word and each REFUSES the other's, and
               NO actor report exists on either side to converge on - a holder does not report, it
               publishes.  What this refusal actually buys is that neither game overwrites the other,
               which is strictly better than the mutual revert; the two backstops that end the state
               are the churn give-up (three undone corrections in five seconds stops this game
               correcting the row) and this refusal itself, which costs one counter per message
               instead of an engine write per tick.  The state ends for real when the area pictures
               agree again.
               The question is asked ONLY where it is legal to ask it: main thread, and after the
               engine's OWN active-zone walk still lists the row (review-p8e C-1).
               P8n-c (review-p8n-b M-2a): AND ONLY A SETTLED ANSWER EARNS A REFUSAL.  The verdict this
               used to read collapses a LOADING area picture into "mine" (items.cpp's kPicLoading arm,
               entered while engine writes are blocked OR no area map has arrived for this world yet),
               so before the first AREAMAP landed this game refused EVERY holder publish it received.
               Loading stores the word, exactly as an unanswerable question does, and both are counted
               (L-4: an arm that books nothing is a silent zero). ---- */
            const int area = DoorAreaForRow(rr);
            const int what = coopdoor::DoorHolderWordDecide(area);
            /* P8n-c (M-2b): AND A REFUSED MESSAGE TAKES NOTHING.  The three lines below used to run
               BEFORE the refusal returned, so a discarded word consumed the holder's generation: the
               holder's next publish was no longer newer than what this game held, the ordering filter
               dropped it, and the holder had no reason to resend.  Refusing a word and eating its
               sequence number is how a refusal becomes permanent. */
            if (coopdoor::DoorWordAdvancesGen(what) != 0)
            { h->locked = locked; h->gen = (long)gen; h->fromPeer = (long)net::PlayerKeyNow(fromPeer); }
            if (what == coopdoor::kDoorWordRefuse)
            {
                ++g_doorHolderWordRefusedOwnKey;
                /* P8n-c (L-5): the latch is PER ROW, which is what the docs already claimed, and the
                   total is budgeted so many refusing doors cannot flood the log. */
                if (rr != 0 && ::InterlockedCompareExchange(&rr->refusedLogged, 1, 0) == 0)
                {
                    if (::InterlockedIncrement(&g_doorRefusedOwnKeyLines) <= kDoorRefusedOwnKeyLineBudget)
                    {
                        char rb[352];
                        _snprintf(rb, 351,
                                  "[DOOR] REFUSING a holder publish for '%s' - this game's area picture is SETTLED"
                                  " and says this game holds that door's patch of map, so the other game's word is"
                                  " not authority for it.  Both games believing they hold one door is the"
                                  " mutual-revert state review-p8n named: each publishes, each refuses, and neither"
                                  " overwrites the other until the pictures agree.  The message's generation is NOT"
                                  " consumed, so the holder's next publish is not filtered out.  Counted as"
                                  " holderWordRefusedOwnKey and logged once per door.",
                                  key.c_str());
                        rb[351] = 0;
                        DebugLog(rb);
                    }
                    else ::InterlockedIncrement64(&g_doorLinesSuppressed);
                }
                return;
            }
            if (what == coopdoor::kDoorWordStoreLoading) ++g_doorHolderWordStoredLoading;
            else if (what == coopdoor::kDoorWordStoreUnanswerable) ++g_doorHolderWordStoredUnanswerable;
            h->state = state;
            if (rr != 0)
            {
                ::InterlockedExchange(&rr->localWrite, 0);
                ::InterlockedExchange(&rr->holderWord, 1);
                ::InterlockedExchange(&rr->lockGaveUp, 0);
                ::InterlockedExchange(&rr->lockStuck, 0);
            }
        }
        ::InterlockedExchange(&g_dirty, 1);
    }
}

void DoorsTick()
{
    ::InterlockedExchangePointer((void* volatile*)&g_playLandedDoor, 0);
    DoorClockAdvance();
    if (!g_on) return;
    /* THE LINK-UP EDGE IS ABOVE THE IDLE GATE, because a re-offer is exactly the work that has to
       happen when nothing local has changed - the peer has just arrived and has been told nothing. */
    {
        static long s_arrCursor = 0;   /* M11 C2: a player ENTERING THE WORLD (the old link's up edge is the session peer's) - every door re-offered */
        if (StoreArrivalsSince(kArrServeDoors, &s_arrCursor, 0) > 0) DoorsOnLinkUp();
    }
    /* ---- P8n-c (review-p8n-b M-3, MANAGER RULING): THE CHURN RE-ARM CLOCK, JUDGED ONCE A SECOND.
       It sits ABOVE the idle gate because a row that has given up produces no events at all - that is
       what giving up means - so a clock that only runs on a dirty tick would never run.  It is
       ALSO not allowed to hold the tick busy, which is what P8n-b did.  The cost when nothing has
       given up is the one interlocked read below; when something has, it is one 256-slot pass per
       SECOND over the rows that gave up, and no engine write is made on it.  Skipped while engine
       writes are blocked for the same reason the apply loop is: a world being built or freed. ---- */
    {
        static unsigned long s_lastReArmMs = 0;
        if (::InterlockedCompareExchange(&g_doorChurnWatch, 0, 0) != 0 && !EngineWritesBlocked())
        {
            const unsigned long nowMs = GetCurrentTickMs();
            if (s_lastReArmMs == 0 || (unsigned long)(nowMs - s_lastReArmMs) >= coopdoor::kDoorReArmSweepMs)
            {
                s_lastReArmMs = nowMs;
                DoorChurnReArmSweep(nowMs);
            }
        }
    }
    /* THE IDLE COST IS THEN ONE INTERLOCKED READ.  Every event that could need work sets the flag -
       a change at a detour, a forced open, an arriving state, the link edge above - so this is
       events over timers rather than a sweep looking for something to do. */
    if (::InterlockedCompareExchange(&g_dirty, 0, 0) == 0) return;
    ::InterlockedExchange(&g_dirty, 0);
    g_mainThread = ::GetCurrentThreadId();

    /* ---- 0. THE ZONE-DEACTIVATE PURGE (review-p8e C-1).  The engine has taken a zone apart since the
       last tick, so every row is re-tested against its active-zone walk and the ones it no longer
       lists are dropped.  This is the box cache's belt-and-braces half and it does the same job: the
       per-row test at every use is what makes the guarantee TRUE, and this is what keeps obviously
       dead rows from being asked about at all - including rows for a door nothing will ever touch
       again, which no per-use test would ever reach.  It costs one shared walk, memoised for the
       generation the deactivation itself just bumped. ---- */
    if (::InterlockedExchange(&g_doorPurgePending, 0) != 0)
    {
        ++g_doorPurgeSweeps;
        for (int p = 0; p < kDoorRows; ++p)
        {
            if (g_rows[p].door == 0 || g_rows[p].ready == 0) continue;
            if (DoorRowStillActive(&g_rows[p]) != 0) continue;
            ++g_doorDroppedInactive;
            DropRow(&g_rows[p]);
        }
    }

    /* ---- P8n-b (review-p8n L-7).  THE WORST-CHURN KEY, TAKEN HERE AND NOWHERE ELSE.  NoteDoor runs
       on a thread build/read-doors.md could not name; it now only raises a flag, and the max and the
       name are read on this thread out of the rows' own interlocked totals. ---- */
    if (::InterlockedExchange(&g_doorWorstScanPending, 0) != 0)
    {
        for (int w = 0; w < kDoorRows; ++w)
        {
            if (g_rows[w].door == 0 || g_rows[w].ready == 0) continue;
            if ((long)g_rows[w].undoneTotal <= g_doorWorstUndone) continue;
            g_doorWorstUndone = (long)g_rows[w].undoneTotal;
            std::strncpy(g_doorWorstKey, g_rows[w].key, kDoorKeyCap - 1);
            g_doorWorstKey[kDoorKeyCap - 1] = 0;
        }
    }

    /* ---- THE LOCK SWEEP: the rows a no-op close marked (detour_closeDoor), before the publish so a lock found
       here goes out this tick.  Left pending while engine writes are blocked; the apply step keeps the tick dirty. ---- */
    if (!EngineWritesBlocked() && ::InterlockedExchange(&g_doorLockSweepPending, 0) != 0)
    {
        for (int q = 0; q < kDoorRows; ++q)
            if (g_rows[q].door != 0 && g_rows[q].ready != 0 && ::InterlockedExchange(&g_rows[q].lockSweep, 0) != 0)
                DoorLockSweepRow(&g_rows[q]);
    }

    /* ---- 1. PUBLISH.  The holder, and only the holder, puts a change on the wire. ---- */
    for (int i = 0; i < kDoorOutCap; ++i)
    {
        if (g_out[i].used == 0) continue;
        {
            char key[kDoorKeyCap];
            long st, lk, rep;
            std::strncpy(key, g_out[i].key, kDoorKeyCap - 1);
            key[kDoorKeyCap - 1] = 0;
            st = g_out[i].state; lk = g_out[i].locked; rep = g_out[i].reportIfNotHolder;
            const long outTries = g_out[i].retries; const unsigned long outFirstFail = g_out[i].firstFailMs;   /* fold 1 (F8) */
            ::InterlockedExchange(&g_out[i].used, 0);
            if (outTries > 0 && DoorOutPendingFor(key, i) != 0) { ++g_doorReportRetrySuperseded; continue; }   /* fold 1 (F8): a newer state for the door is queued */
            if (!net::SideRoadOpen()) { ++g_doorPublishNoLink; continue; }   /* M7b slice 4 (C4): the character stream's road - the notebook or the session link */
            {
                /* Decision 40's holder rule, asked of the DOOR'S OWN position - not the player's -
                   through the same function the box road asks (lesson 11). */
                DoorRow* r = RowByKey(key);
                float pos[3];
                pos[0] = 0; pos[1] = 0; pos[2] = 0;
                if (r == 0 || DoorPlausible(r->door) == 0) { ++g_doorPublishNotHolder; continue; }
                /* review-p8e C-1: DoorPosPod below is a VIRTUAL CALL through a stored pointer, so the
                   engine's active-zone walk is asked here for the same reason it is asked in
                   ApplyToRow - a queued change can outlive the zone whose door it names.
                   P8n: THE DROP IS NO LONGER SILENT.  It loses the queued publish AND frees the row,
                   and only droppedInactive - which also covers the apply and the sweep - said so.
                   58 of the holder's ~98 terminal changes went this way in T236d. */
                if (DoorRowStillActive(r) == 0)
                { ++g_doorDroppedInactive; ++g_doorPublishDroppedInactive; DropRow(r); continue; }
                if (DoorPosPod(r->door, pos) == 0) { ++g_doorPublishNotHolder; continue; }
                {
                    /* P8n: the holder question is asked LIVE, at the moment of commitment (principle
                       2), and it decides what KIND of message this is rather than whether there is
                       one: the holder publishes, anybody else reports to the holder. */
                    const int verdict = AreaVerdictForDoor(pos);
                    /* B12-c (review-b12 H-2), decision 52 AND decision 47 together. THE R4 REFUSAL IS WHERE
                       THE JOURNAL GOES, AND THE DRAIN BELOW IS UNTOUCHED BY IT. Past decision 44's grace
                       the ladder refuses the durable write, so the holder's own state write is written down
                       here and re-published with a FRESH generation when the notebook returns. What is NOT
                       queued and never was is the ACTOR REPORT the drain may send below: that is a
                       game-to-game message from a non-holder to the holder (M7b slice 4: on the character
                       stream's road), it is not a durable write, and it must keep flowing whether or not there is one. B12-a
                       suppressed it and was wrong to; that arm is retired. */
                    /* B12-d (recheck-b12): ONLY A REAL LOCAL CHANGE IS JOURNALED (rep != 0). A re-offer
                       (DoorsOnLinkUp's QueueOut with reportIfNotHolder = 0) is not a change and must never
                       push stale local state at the holder through the journal either; and whether this game
                       may publish the entry is asked AGAIN at the replay, where the notebook is back (the
                       no-notebook verdict here says nothing about who holds the door). */
                    if (verdict == coopdoor::kDoorAreaNoNotebook && rep != 0)
                    {
                        int queued = 0;
                        {
                            std::vector<char> pay;
                            if (DoorStateToBytes(key, (int)st, (int)lk, &pay) != 0
                                && coop::StoreQueueWrite(coopqueue::kQueueFamilyDoor, std::string(key), pay) != 0)
                            { ++g_doorQueuedNoNotebook; queued = 1; }
                            else ++g_doorQueuePayloadRefused;
                        }
                        if (g_doorNoNotebookSaid == 0)
                        {
                            ++g_doorNoNotebookSaid;
                            DebugLog(std::string("[DOOR] NO NOTEBOOK: past decision 44's grace this game"
                                     " writes no durable door state (the door road takes the box road's own"
                                     " ladder). The holder's state write for '") + key
                                     + "' and every one after it while the notebook is away is "
                                     + (queued ? std::string("QUEUED until it returns and re-published then"
                                                             " with a fresh generation (decision 52)")
                                               : std::string("LOST - it could not be written into the outage"
                                                             " journal either, which is a defect"
                                                             " (decision 52)"))
                                     + ". The actor report to the area holder is NOT affected and"
                                     " still goes out (decision 47). The numbers are"
                                     " doorOutageQueue[queued,payloadRefused] in the report line below.");
                        }
                    }
                    const int weHoldIt = (verdict == 1) ? 1 : 0;
                    const int how = coopdoor::DoorDrainDecide(weHoldIt, (int)rep);
                    if (how == coopdoor::kDoorSendRefuse) { ++g_doorPublishNotHolder; continue; }
                    /* P8n-b (review-p8n M-5): THE OTHER HALF OF THE OPEN POINT, as far as this game can
                       see it.  A report sent while this game's own area picture names NOBODY for the
                       door may reach no holder at all, and nothing would then adopt it.  Whether the
                       PEER holds it cannot be known from here - only the half that can be is counted. */
                    if (how == coopdoor::kDoorSendActor && verdict == 0) ++g_doorReportNoHolder;
                    {
                        const int origin = (how == coopdoor::kDoorSendActor)
                                           ? coopdoor::kDoorOriginActor : coopdoor::kDoorOriginHolder;
                        if (net::SendDoorState(std::string(key), (int)st, (int)lk,
                                               (unsigned int)::InterlockedIncrement(&g_gen), origin, pos))   /* M7b slice 4 (C4): routed by the door's sector */
                        {
                            if (origin == coopdoor::kDoorOriginActor) ++g_doorActorReported;
                            else ++g_doorPublished;
                        }
                        else
                        {   /* not sent: an actor report is kept for a while, a re-offer until it is sent (coopdoor::DoorUnsentDecide) */
                            const int how = coopdoor::DoorUnsentDecide(origin, (int)rep);
                            if (how != coopdoor::kDoorUnsentDrop) DoorOutRetry(key, st, lk, rep, outTries, outFirstFail, how);
                        }
                    }
                }
            }
        }
    }

    /* ---- 2. APPLY.  Every door this game has that the holder has spoken about. ---- */
    if (EngineWritesBlocked()) { ++g_doorApplyBlocked; ::InterlockedExchange(&g_dirty, 1); return; }
    /* review-p8o M-3: THE TICK'S WHOLE RESOLVE BUDGET.  One key per tick may be looked up the long
       way; the loop below leaves the others pending rather than consuming them. */
    int resolvedThisTick = 0;
    /* P8o-c (recheck-p8o): THE BUDGET ROTATES.  The scan used to start at slot 0 every tick, so one
       key that a peer keeps re-reporting and that never resolves took the whole budget every tick and
       every higher slot stayed deferred for ever.  The scan now starts one past the slot that spent
       last tick's attempt, so every pending key gets its turn in at most kDoorHeldCap ticks. */
    static int s_heldCursor = 0;
    for (int k = 0; k < kDoorHeldCap; ++k)
    {
        const int i = (s_heldCursor + k) % kDoorHeldCap;
        if (g_held[i].used == 0) continue;
        /* P8n: a row whose holder state is still unknown is visited ONLY when it carries an actor
           report waiting to be adopted - which is the ordinary first event on a door the holder has
           never published about, and which the old gate would have left pending for ever. */
        if (g_held[i].state < 0 && g_held[i].pendingActor == 0) continue;
        {
            DoorRow* r = RowByKey(g_held[i].key);
            /* P8o (T236g): BEFORE GIVING UP ON THE NAME, ASK THE PARENT BUILDING.  A row is created by
               this game's own setDoorState detours, so a reloaded zone leaves the holder unable to
               name a door its peer keeps reporting - 24 reports dropped here and about 100 s of
               visible disagreement.  Asked only for a key carrying a PENDING ACTOR REPORT.
               ONE RESOLVE PER TICK (review-p8o M-3): the walk reads every listed building and, for a
               gate key, names each one, so a burst of unnameable keys must not multiply it.  The
               first key that needs it gets it; the rest KEEP their pending report - nothing is
               consumed and nothing is booked unnameable for them - and the tick is left dirty so a
               later one tries them.  The pending-report consume below is what bounds a key to one
               attempt per report, so no latch is needed (review-p8o L-1). */
            if (r == 0 && g_held[i].pendingActor != 0)
            {
                if (resolvedThisTick != 0)
                {
                    ++g_doorResolveDeferred;
                    ::InterlockedExchange(&g_dirty, 1);
                    continue;
                }
                resolvedThisTick = 1;
                s_heldCursor = (i + 1) % kDoorHeldCap;   /* P8o-c: next tick starts past this slot */
                r = DoorResolveByKey(g_held[i].key);
            }
            if (r == 0)
            {
                ++g_doorApplyUnresolved;
                /* P8n: AN ACTOR REPORT FOR A DOOR THIS GAME CANNOT NAME IS CONSUMED, NOT LEFT
                   PENDING - so `received` stays an exact sum of what happened to each report. */
                if (::InterlockedExchange(&g_held[i].pendingActor, 0) != 0) ++g_doorActorUnnameable;
                continue;
            }
            {
                /* THE THREE REASONS, AND EACH ONE IS REACHABLE.  A forced open from physics setup
                   beats everything (it is the F633 mechanism); otherwise a state the HOLDER published
                   has arrived since the last agreement and this game follows it; and a disagreement
                   with neither of those behind it is this game hearing about the door for the first
                   time.  P8n retired the old third arm ("a local writer moved a door this game does
                   not hold, and we put it back"): under the last-actor rule the applier never reverts
                   an engine writer, so that arm could no longer fire at all. */
                const int reason = (r->forcedOpen != 0) ? 2 : ((r->holderWord != 0) ? 3 : 1);
                ApplyToRow(r, &g_held[i], reason);
            }
        }
    }
}

void ReportDoors()
{
    long long keyUnbuildable = 0, keyHashed = 0;
    ObjectKeyDoorCounts(&keyUnbuildable, &keyHashed);
    {
        char b[3584];   /* P8o + B12: resolve[...] and doorOutageQueue[...] both ride here; grown so no token can truncate the tail (F172) */
        _snprintf(b, 3583,
            "[DOOR] doors=%s hooks=%d/5 applier=%d doorPublished=%lld doorApplied=%lld"
            " applySpans[onArrival,reapplyAfterForcedOpen,appliedHolderWord]=%lld,%lld,%lld"
            " doorKeyUnbuildable=%lld keyBuilderRefusals=%lld keySidHashed=%lld"
            " seenChanges=%lld suppressedSameState=%lld selfWrite=%lld offThread=%lld readFault=%lld retUnreadable=%lld"
            " publish[notHolder,noLink,republishedOnLink]=%lld,%lld,%lld"
            " apply[noSetter,blocked,unresolved,ineffective,gaveUp,rowRecycled]=%lld,%lld,%lld,%lld,%lld,%lld"
            " overflow[rows,held,out]=%lld,%lld,%lld msgMalformed=%lld p060Suppressed=%lld"
            " droppedInactive=%lld activeVia[door,owner]=%lld,%lld zoneDeactivations=%lld purgeSweeps=%lld"
            " lastActor[reported,received,adopted,adoptedLocal,ignoredNotHolder,unnameable,superseded,kept,midSwingSkipped,rowUnusable]"
            "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
            " churn[undone,gaveUpChurn,reArmed,worstUndone]=%lld,%lld,%lld,%ld worstChurnKey=%s"
            " holderWordRefusedOwnKey=%lld holderWordStored[loading,unanswerable]=%lld,%lld originUnknown=%lld"
            " report[retried,abandoned,suppressedAbandoned,noHolder,droppedTeardown]=%lld,%lld,%lld,%lld,%lld"
            " resolve[asked,found,missedNoBuilding,missedNoDoor,refusedChecks,gateMultiLeaf,recycledRow,deferred]"
            "=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
            " publishDroppedInactive=%lld firstSighting=%lld"
            " doorOutageQueue[queued,payloadRefused]=%lld,%lld",
            g_on ? "on" : "off", g_hooked, g_setterOk,
            g_doorPublished, g_doorApplied,
            g_doorAppliedOnArrival, g_doorReapplyAfterForcedOpen, g_doorAppliedHolderWord,
            g_doorKeyUnbuildable, keyUnbuildable, keyHashed,
            g_doorSeenChanges, g_doorSuppressedSameState, g_doorSelfWrite, g_doorOffThread, g_doorReadFault, g_doorRetUnreadable,
            g_doorPublishNotHolder, g_doorPublishNoLink, g_doorRepublishedOnLink,
            g_doorApplyNoSetter, g_doorApplyBlocked, g_doorApplyUnresolved, g_doorApplyIneffective,
            g_doorGaveUp, g_doorRowRecycled,
            g_doorRowsOverflow, g_doorHeldOverflow, g_doorOutOverflow,
            g_doorMsgMalformed, g_doorLinesSuppressed,
            g_doorDroppedInactive, g_doorActiveViaDoor, g_doorActiveViaOwner,
            g_doorZoneDeactivations, g_doorPurgeSweeps,
            g_doorActorReported, g_doorActorReceived, g_doorAdopted, g_doorAdoptedLocal,
            g_doorActorIgnoredNotHolder, g_doorActorUnnameable, g_doorActorSuperseded,
            g_doorLastActorKept, g_doorMidSwingSkipped,
            g_doorActorRowUnusable,
            g_doorUndone, g_doorGaveUpChurn, g_doorChurnReArmed, g_doorWorstUndone,
            g_doorWorstKey[0] != 0 ? g_doorWorstKey : "none",
            g_doorHolderWordRefusedOwnKey,
            g_doorHolderWordStoredLoading, g_doorHolderWordStoredUnanswerable, g_doorOriginUnknown,
            g_doorReportRetried, g_doorReportAbandoned, g_doorReportSuppressedAbandoned,
            g_doorReportNoHolder, g_doorReportDroppedTeardown,
            g_doorResolveAsked, g_doorResolveFound, g_doorResolveMissedNoBuilding,
            g_doorResolveMissedNoDoor, g_doorResolveRefusedChecks, g_doorResolveGateMultiLeaf,
            g_doorResolveRecycledRow, g_doorResolveDeferred,
            g_doorPublishDroppedInactive, g_doorFirstSighting,
            g_doorQueuedNoNotebook, g_doorQueuePayloadRefused);
        b[3583] = 0;
        DebugLog(b);
    }
    {   /* T-160 */
        char t1[480];
        _snprintf(t1, 479, "[DOOR] swing[played,snapped,refused,landed,interrupted,overdue,frames,relockOurs,landReported]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld"
                  " lock[applied,locks,unlocks,wantsCleared,lockedSet,ineffective,gaveUp,noLockObject,noCall,unseenLocal]=%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld",
                  g_doorPlayStarted, g_doorSnapped, g_doorPlayRefused, g_doorPlayLanded, g_doorPlayInterrupted,
                  g_doorPlayOverdue, g_doorPlayFrames, g_doorRelockOurs, g_doorPlayLandReported,
                  g_doorLockApplied, g_doorLockLocks, g_doorLockUnlocks, g_doorLockWantsCleared, g_doorLockSetLocked,
                  g_doorLockIneffective, g_doorLockGaveUp, g_doorLockNoLockObject, g_doorLockNoCall, g_doorLockUnseenLocal);
        t1[479] = 0;
        DebugLog(t1);
    }
    {   /* M7b slice 4 fold 1 (F8) */
        char f8[200];
        _snprintf(f8, 199, "[DOOR] reportRetry[retried,gaveUp,superseded,full]=%lld,%lld,%lld,%lld",
                  g_doorReportSendRetried, g_doorReportRetryGaveUp, g_doorReportRetrySuperseded, g_doorReportRetryFull);
        f8[199] = 0;
        DebugLog(f8);
    }
    {   /* P15 fold 1 */
        char f1[320];
        _snprintf(f1, 319, "[DOOR] ownerForget[rowsForgotten,ownerRouteRefused]=%lld,%lld (rows dropped before a setDestroyed flip on their"
                  " building; owner-route passes refused because the owner's door array no longer lists the door)",
                  g_doorRowsForgotten, g_doorOwnerRouteRefused);
        f1[319] = 0;
        DebugLog(f1);
    }
    DebugLog("[DOOR]   |  B12 / decision 52: doorOutageQueue[queued,payloadRefused] counts the HOLDER'S OWN"
             " state writes refused past decision 44's grace with no notebook. `queued` are in the outage"
             " journal beside the notebook's folder and are re-published with a FRESH generation at the next"
             " link-up (an old generation would be filtered out by the receiver); `payloadRefused` is a write"
             " the journal would not take, and is a door change LOST. SESSION-LINK DOOR MESSAGES ARE NEVER"
             " QUEUED: the actor report a non-holder sends the holder is not a durable write, reaches no"
             " notebook, and keeps flowing with or without one (decision 47) - B12-a suppressed it for one"
             " build and B12-c retired that arm. Both numbers read 0 by construction on every run before"
             " B12-c rather than by absence, because the verdict was collapsed before the drain saw it.");
    DebugLog("[DOOR]   |  doorApplied = onArrival + reapplyAfterForcedOpen + appliedHolderWord (SPANS inside it,"
             " not extra buckets).  doorKeyUnbuildable is the door road's own count at the detour and"
             " keyBuilderRefusals is items.cpp's count of the same events split by the builder's five reasons -"
             " they measure the SAME event from two sides and are expected to agree.  E45 SHARES THE STATE AND NOT"
             " THE CAUSE: OPENING and CLOSING never go on the wire.  A holder's change to a settled door is PLAYED"
             " with the engine's own openDoor / closeDoor (swing[played], the engine's own swing and sound) and"
             " everything nobody watched move is SNAPPED (swing[snapped]): played + snapped = doorApplied.  The"
             " holder's LOCK WORD (locked + wantsToLock) is applied once the states agree, with the engine's own"
             " lockDoor / unlockDoor and the two direct writes the engine's own code makes (lock[...]), silently; a"
             " live lock word no detour saw is this game's own (unseenLocal).  E45 IS STILL A COMPENSATION with a row in docs/parity-register.md, but"
             " P8n REWROTE THE RULE: the five AI task actions and the player's own click are let through locally"
             " and are NOT REVERTED BY THE APPLIER - the change is reported to the area holder, which adopts and"
             " republishes it (see the P8n footer below).  The span that counted the old revert is retired and absent from the"
             " verdict line rather than printed as zero (F172).");
    DebugLog("[DOOR]   |  P8e-b (review-p8e C-1): NO CALL IS MADE THROUGH A STORED DOOR POINTER UNTIL THE ENGINE'S"
             " OWN ACTIVE-ZONE WALK STILL LISTS IT.  droppedInactive is rows dropped by that test - at the apply,"
             " at the publish, and in the sweep a ZoneMapContent::deactivate schedules - and each one is a door"
             " this game stops correcting until its next funnel call re-registers it, never a call through a"
             " pointer the engine has let go.  activeVia[door,owner] says WHICH pointer the walk listed and"
             " settles the question containers.md leaves open: door > 0 means a DoorStuff is itself walked;"
             " door = 0 with owner > 0 means only the parent building is, and the row is alive on that alone."
             " zoneDeactivations is the fan-out from items.cpp's ONE hook on ZoneMapContent::deactivate (two"
             " subscribers, not two detours) and purgeSweeps the main-thread sweeps they caused.  The walk is"
             " SHARED with the box road, list, memo and generation, so items' boxResolveWalks counts walks"
             " caused by either road.");
    DebugLog("[DOOR]   |  P8o (T236g): A HOLDER NOW NAMES A DOOR ITS OWN ENGINE HAS NEVER MOVED."
             " A DoorRow is created by this game's setDoorState detours, so a holder whose zone had"
             " reloaded held no row for the key its peer kept reporting - 24 reports booked unnameable and"
             " dropped, about 100 s of visible disagreement.  An actor report for a key no row answers now"
             " resolves through the PARENT BUILDING: the SAME memoised loaded-buildings walk the liveness"
             " test uses, each building's door array (count at +0x1C0, elements at +0x1C8, stride 8, copied"
             " whole inside one SEH frame), and the key rebuilt FROM THE DOOR by the one builder the"
             " detours use, compared EXACTLY - there is no nearest-position fallback, because a swinging"
             " door never moves the field the key is built from.  P8o-b: THE WALK MAKES NO VIRTUAL CALL."
             " It reads a memo a worker's zone teardown can outdate, and SEH catches a fault but not a call"
             " through a garbage vtable slot - so the position comes from the +0x48 field through the key"
             " builder's POD mode and the gate test is read off the KEY, never asked of the object.  Its"
             " key builds are UNCOUNTED, so doorKeyUnbuildable and the builder's refusal split still count"
             " real doors at the detours.  IDENTITY: resolve.asked = found + missedNoBuilding +"
             " missedNoDoor + refusedChecks.  missedNoBuilding is the walk listing no building at all, or"
             " no gateway building whose own key matched a gate key; missedNoDoor is buildings read and no"
             " door of theirs rebuilding this key, i.e. the door is not loaded here; refusedChecks is a"
             " candidate refused by DoorPlausible, by a state outside 0..3 or by the parent round trip - or"
             " a row that could not be seated, which overflow.rows counts on its own - with nothing"
             " matched.  THREE SIDE COUNTS, none of them in the identity: gateMultiLeaf is inside found,"
             " because every leaf of one gateway shares ONE key, so a two-leaf gateway can only ever be"
             " answered doors[0] and that answer is ambiguous; recycledRow is inside found too - RowFor"
             " matched an existing row by POINTER whose key was the door that used to live at that address,"
             " and it was dropped and remade under this key; deferred is a key that needed the walk on a"
             " tick that had already spent it (ONE resolve per tick) and that KEPT its pending report for a"
             " later tick, so asked was never incremented for it.  LASTACTOR.UNNAMEABLE HAS THREE BOOKING"
             " SITES and is not an identity with the resolve misses: the row was no longer in the active"
             " zone walk, the row's key no longer rebuilds, or the resolve above could not name the key."
             " P8o names the door; it does not change the adopt rule - a named door whose area verdict is"
             " not ours still books ignoredNotHolder.");
    DebugLog("[DOOR]   |  P8n: DOOR STATE FOLLOWS THE LAST ACTOR AND THE AREA HOLDER SERIALISES CONFLICTS."
             " An ENGINE-originated change on either game is NOT REVERTED BY THE APPLIER; a later holder word or"
             " an adopted report may supersede it - CONVERGENT, NOT FROZEN (P8n-b, review-p8n M-3: two deliberate"
             " paths do supersede an engine change - the applier's holder arm clears localWrite when the holder"
             " publishes, and the holder consumes a pending actor report before it reads the live state)."
             " On a game that does not hold the"
             " door it is SENT to the holder (lastActor.reported), the holder ADOPTS it (lastActor.adopted),"
             " applies it and republishes, and everyone converges on the last actor.  lastActor.kept is the"
             " applier declining to revert - it is the number that read 2652 as doorRevertedNotHolder in T236d,"
             " and THAT SPAN IS RETIRED FROM THIS LINE rather than printed as zero (F172); its slot is"
             " appliedHolderWord.  IDENTITIES: doorApplied = onArrival + reapplyAfterForcedOpen +"
             " appliedHolderWord, WITH A HOLDER'S ADOPTION COUNTED AS appliedHolderWord (P8n-b, M-1 - the adopt"
             " IS the holder's word for that door); received = adopted + ignoredNotHolder + unnameable +"
             " superseded + rowUnusable, and rowUnusable is P8n-b's M-2 bucket for a report resolved without"
             " adoption because this game's row for the door could not be used at all.  churn.reArmed is M-4's"
             " ruling: a row given up on churn is re-armed by thirty seconds of continuous agreement, so no door"
             " is dead for the run.  holderWordRefusedOwnKey is M-5's: a HOLDER publish for a door THIS game"
             " holds is refused, which is what removes the both-games-hold mutual revert; report.noHolder is the"
             " half of the neither-holds case this game can see (its own picture named nobody).  originUnknown is"
             " a DOOR_STATE whose trailing origin byte was neither 0 nor 1 - IGNORED whole rather than taken as"
             " the holder's word (L-8).  report.retried/abandoned is the actor report's own recurrence: it is"
             " re-queued every 2 s until the holder's word lands and dropped after 15 s (L-6).  publishDroppedInactive is"
             " a SPAN inside droppedInactive (the other two sites are the apply and the sweep); worstUndone is"
             " the largest per-key undone total and worstChurnKey names that door.  A door mid-swing (2 OPENING"
             " or 3 CLOSING) is LEFT ALONE and counted as midSwingSkipped - the holder's state is only ever 0"
             " or 1, so comparing it against an animating door is what made every revert read was=2 now=0."
             " churn.undone counts corrections a NON-self writer reversed before the next agreement, and 3 of"
             " them inside 5 s stops this game correcting that row (gaveUpChurn, logged once by key).  The P060"
             " budget is now 20 lines PER KEY, so one runaway door cannot blind the log for every other door.");
    DebugLog("[DOOR]   |  P8n-c (review-p8n-b): AN ABANDONED REPORT STAYS ABANDONED UNTIL THE DOOR AGREES."
             " report.abandoned used to clear the pending flag and leave nothing on the row, so the next dirty"
             " tick opened a fresh 15 s episode and the documented bound held for one episode only.  A per-row"
             " latch now suppresses every later report verdict until the terminal-state compare AGREES - counted"
             " as report.suppressedAbandoned - and a disagreement after an agreement gets the full bound again."
             " report.droppedTeardown is reports still pending when a world was torn down, which the memset used"
             " to swallow.  holderWordRefusedOwnKey is now earned ONLY by a SETTLED area picture: the verdict the"
             " refusal reads answers MINE for a LOADING picture by design (a game must act on its own world while"
             " it loads), so before the first area map arrived this game refused EVERY holder publish - those are"
             " holderWordStored.loading now, stored exactly as before, and holderWordStored.unanswerable is the"
             " arm where the question could not be asked at all.  A REFUSED message no longer consumes the"
             " sender's generation, so the holder's next publish is not filtered out by the ordering test - a"
             " refusal that ate the sequence number was a refusal that never ended.  The churn re-arm clock is"
             " read by a sweep at most once a SECOND while some row is given up, instead of holding the tick"
             " dirty (and therefore running the whole 256-slot tick every frame) for thirty seconds.");
    /* ---- P8n: THE PER-KEY STATE DUMP.  build/read-door-churn.md had to reconstruct per-key state from
       a truncated line stream and could not bracket the event; one line per row at report time settles
       in one readout what that read needed 400 log lines and arithmetic to guess at. ---- */
    DebugLog("[DOOR]   |  per-key state follows.  now = the last state the detours saw (the funnel sees every"
             " write, so it is the live value and no engine call is made at report time); src = who last moved"
             " it HERE - self this game's own world at first sighting, actor an engine writer here, holder our"
             " applier writing the area holder's answer; t = SECONDS.TENTHS since that change; applies = engine writes"
             " this game made on this door; undone = corrections a local writer reversed; held = the holder"
             " state this game is holding it to, -1 = nothing heard; ret = the last non-self writer's return"
             " address, which is what names WHO moved it.");
    {
        int i;
        int shown = 0;
        const unsigned long nowMs = GetCurrentTickMs();
        for (i = 0; i < kDoorRows; ++i)
        {
            if (g_rows[i].door == 0 || g_rows[i].ready == 0) continue;
            {
                const DoorHeldRow* h = HeldFor(g_rows[i].key, 0);
                const unsigned long dt = nowMs - (unsigned long)(long)g_rows[i].lastChangeMs;
                const long srcv = g_rows[i].src;
                char sb[352];
                ++shown;
                _snprintf(sb, 351,
                    "[DOOR] state key=%s now=%ld src=%s t=%lu.%lu applies=%ld undone=%ld held=%ld"
                    " gaveUp=%ld,%ld ret=0x%lX",
                    g_rows[i].key, (long)g_rows[i].state,
                    srcv == 1 ? "holder" : (srcv == 2 ? "actor" : "self"),
                    (unsigned long)(dt / 1000u), (unsigned long)((dt % 1000u) / 100u),
                    (long)g_rows[i].applies, (long)g_rows[i].undoneTotal,
                    h != 0 ? (long)h->state : (long)-1,
                    (long)g_rows[i].gaveUp, (long)g_rows[i].gaveUpChurn,
                    (unsigned long)(long)g_rows[i].lastRet);
                sb[351] = 0;
                DebugLog(sb);
            }
        }
        {
            char cb[160];
            _snprintf(cb, 159, "[DOOR] state rows=%d of %d", shown, kDoorRows);
            cb[159] = 0;
            DebugLog(cb);
        }
    }
}

/* PROBE P068 (B1): a door's state and open amount for `nearlist`, through the same guarded read the door road uses. */
int DoorOpenStatePod(void* door, int* state, int* openTenths)
{
    int lk = 0, wants = 0, broken = 0;
    return DoorReadPod(door, state, openTenths, &lk, &wants, &broken);
}

/* ============ T-160: TEST LEVER `doortest` (TEST-ONLY) ============
   doortest open|close|lock <nearest|last|key>  - this game moves one door the way its own world would: open / close
       through the engine's DoorStuff::openDoor / closeDoor (the calls a character's open uses), lock through
       DoorStuff::lockButton (the player's lock press - it flips the lock).  Not inside the applier's bracket, so the
       detours see an ordinary change and the door road shares it exactly as it shares a character's.
   doortest npclock <nearest|last|key>  - what the NPC lock action 0x337630 does: closeDoor, then DoorLock::locked = 1
       written directly (wantsToLock untouched) - a lock no detour sees.  Its Character_Lock sound is not played.
   doortest show <nearest|next|last|key|held>  - a read: the door's state, open amount and lock word; `held` lists
       every holder word this game has received, beside the live state of its door here.
   `nearest` = a door of the building nearest the watched player that has one (zones.cpp NearestBuildingWhere); `next`
   = the same, skipping the building of the door the previous doortest named; `last` = the door the previous doortest
   named, found again by its key in this game's active zones (as a key target is), never by a stored pointer.  MAIN
   THREAD, behind EngineWritesBlocked(). */
namespace {
char  g_testKey[kDoorKeyCap] = { 0 };
void* g_testSkipBuilding = 0;   /* `next`: set only for the length of its NearestBuildingWhere call */
/* The building's first door that is a plausible DoorStuff naming this building as its parent, or 0. */
void* DoorTestFirstDoor(void* building)
{
    void* elems[8];
    int cnt = 0, raw = 0, k;
    if (DoorArrayPod(building, elems, 8, &cnt, &raw) == 0) return 0;
    for (k = 0; k < cnt; ++k)
    {
        void* parent = 0;
        int s = 0, d = 0, l = 0, w = 0, b = 0;
        if (DoorPlausible(elems[k]) == 0) continue;
        if (DoorParentPod(elems[k], &parent) == 0 || parent != building) continue;
        if (DoorReadPod(elems[k], &s, &d, &l, &w, &b) == 0) continue;
        return elems[k];
    }
    return 0;
}
int DoorTestHasDoor(void* building) { return (DoorTestFirstDoor(building) != 0) ? 1 : 0; }
int DoorTestHasOtherDoor(void* building) { return (building != g_testSkipBuilding && DoorTestHasDoor(building) != 0) ? 1 : 0; }
/* The live row of a key in this game's active zones: the row the funnel made, or the parent-building walk's
   (DoorResolveByKey); 0 when neither names it or its zone is no longer active. */
DoorRow* DoorTestRowByKey(const char* key)
{
    DoorRow* r = RowByKey(key);
    if (r == 0) r = DoorResolveByKey(key);
    if (r == 0 || DoorRowStillActive(r) == 0) return 0;
    return r;
}
void DoorTestPressLockPod(void* door)
{
    __try { g_lockButton(door, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}
std::string DoorTestLine(const char* what, void* door, const char* key)
{
    int s = 0, d = 0, l = 0, w = 0, b = 0;
    char o[320];
    if (DoorReadPod(door, &s, &d, &l, &w, &b) == 0) return std::string("error doortest: the door read faulted");
    _snprintf(o, 319, "%s key=%s state=%d doa=%d lockWord=%d (locked=%d wants=%d) broken=%d hold=%s",
              what, key, s, d, coopdoor::DoorLockWord(l, w), l, w, b, HoldWord(door, 1));
    o[319] = 0;
    return std::string(o);
}
} // namespace

std::string DoorTestCommand(const std::string& argIn)
{
    std::string op, target;
    {
        const size_t sp = argIn.find(' ');
        op = argIn.substr(0, sp);
        target = (sp == std::string::npos) ? std::string() : argIn.substr(sp + 1);
        while (!target.empty() && target[0] == ' ') target.erase(0, 1);
    }
    if (op != "open" && op != "close" && op != "lock" && op != "npclock" && op != "show")
        return "error doortest: usage `doortest open|close|lock|npclock|show <nearest|next|last|key>` or `doortest show held`";
    if (::GetCurrentThreadId() != g_mainThread) return "error doortest: not on the main thread";
    if (EngineWritesBlocked()) return "error doortest: engine writes blocked (a load or a teardown)";
    if (op == "show" && target == "held")
    {
        int n = 0;
        for (int i = 0; i < kDoorHeldCap; ++i)
        {
            if (g_held[i].used == 0) continue;
            DoorRow* r = RowByKey(g_held[i].key);
            char hb[200];
            _snprintf(hb, 199, "[DOORTEST] held key=%s holderState=%ld holderLockWord=%ld", g_held[i].key,
                      (long)g_held[i].state, (long)g_held[i].locked);
            hb[199] = 0;
            if (r != 0 && DoorRowStillActive(r) != 0) DebugLog(std::string(hb) + " | " + DoorTestLine("here", r->door, r->key));
            else DebugLog(std::string(hb) + " | here: no live row");
            ++n;
        }
        char nb[64];
        _snprintf(nb, 63, "ok doortest show held: %d", n);
        nb[63] = 0;
        return std::string(nb);
    }
    void* door = 0;
    std::string key;
    if (target == "nearest")
    {
        std::string err;
        double dist = -1;
        void* b = NearestBuildingWhere(&DoorTestHasDoor, &dist, &err);
        if (b == 0) return "error doortest nearest: " + err;
        door = DoorTestFirstDoor(b);
        if (door == 0) return "error doortest nearest: the building's door did not read back";
    }
    else if (target == "next")
    {
        std::string err;
        double dist = -1;
        void* b = 0;
        DoorRow* r = (g_testKey[0] != 0) ? DoorTestRowByKey(g_testKey) : 0;
        if (r == 0) return "error doortest next: no door named yet, or it is not in this game's active zones";
        g_testSkipBuilding = r->owner;
        b = NearestBuildingWhere(&DoorTestHasOtherDoor, &dist, &err);
        g_testSkipBuilding = 0;
        if (b == 0) return "error doortest next: " + err;
        door = DoorTestFirstDoor(b);
        if (door == 0) return "error doortest next: the building's door did not read back";
    }
    else
    {
        const std::string want = (target == "last") ? std::string(g_testKey) : target;
        if (want.empty()) return "error doortest last: no door named yet";
        DoorRow* r = DoorTestRowByKey(want.c_str());
        if (r == 0) return "error doortest: no door with that key in this game's active zones: " + want;
        door = r->door;
        key = r->key;
    }
    if (DoorPlausible(door) == 0) return "error doortest: the door is not plausible";
    {
        char kb[kDoorKeyCap];
        int gate = 0;
        kb[0] = 0;
        if (DoorKeyBuild(door, kb, kDoorKeyCap, &gate, 0) == 0) return "error doortest: the door's key could not be built";
        if (!key.empty() && key != kb) return "error doortest: the door no longer carries the key " + key;
        key = kb;
    }
    std::strncpy(g_testKey, key.c_str(), kDoorKeyCap - 1);
    g_testKey[kDoorKeyCap - 1] = 0;
    if (op == "show")
    {
        const std::string line = DoorTestLine("[DOORTEST] show", door, key.c_str());
        DebugLog(line);
        return "ok " + line;
    }
    {
        const std::string before = DoorTestLine("before", door, key.c_str());
        if (before.compare(0, 6, "error ") == 0) return before;
        if (op == "open")
        {
            if (g_playOpen == 0) return "error doortest: openDoor did not pass its prologue check";
            ApplyPlayPodInner(g_playOpen, door);
        }
        else if (op == "close")
        {
            if (g_playClose == 0) return "error doortest: closeDoor did not pass its prologue check";
            ApplyPlayPodInner(g_playClose, door);
        }
        else if (op == "lock")
        {
            if (g_lockButton == 0 || DoorHasLockPod(door) == 0) return "error doortest: no lock button call, or the door has no lock";
            DoorTestPressLockPod(door);
        }
        else
        {
            if (g_playClose == 0 || DoorHasLockPod(door) == 0) return "error doortest: no closeDoor call, or the door has no lock";
            ApplyPlayPodInner(g_playClose, door);
            ApplyLockPodInner(coopdoor::kDoorLockStepSetLocked, door);   /* outside the applier's bracket: the world's write */
        }
        {
            const std::string line = "[DOORTEST] " + op + " " + before + " -> " + DoorTestLine("after", door, key.c_str());
            DebugLog(line);
            return "ok " + line;
        }
    }
}

/* P18 fold 1: the openDoor address (the table's OpenDoor row) for a bought house's doors (build.cpp); 0 = no row */
unsigned long long DoorsOpenDoorRva() { return kOpenDoorRva; }

} // namespace coop
