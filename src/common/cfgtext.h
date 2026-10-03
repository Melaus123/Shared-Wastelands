/* src/common/cfgtext.h - THE TEXT LAYER OF shared_wastelands.cfg, AND IT IS PURE (P8i / U1).
 *
 * WHY THIS FILE EXISTS.  Until P8i the only thing that could produce a shared_wastelands.cfg was
 * tools/write-cfg.ps1, and the only thing that could read one was ConfigLoad's inline loop in
 * config.cpp.  The MULTIPLAYER panel now writes that file from inside the game, which would have
 * made THREE programs hold three ideas of one format - and the panel would have had to
 * re-implement <address>:<port> to refuse a bad entry before writing.  6a lesson 11: when a defect
 * comes from two things being kept in step by hand, the repair removes the hand.  So the parser,
 * the writer and every validator live here, once, and:
 *   * config.cpp calls this to READ the file (it is the same parser, moved, not a second copy),
 *   * config.cpp calls this to WRITE the file (ConfigWrite),
 *   * ui.cpp calls this to REFUSE what the player typed, in the sentences it hands to the screen,
 *   * src/coop-test/test_main.cpp compiles this same translation unit and round-trips the writer
 *     through the parser, so "what we write is what we read" is proved rather than asserted.
 *
 * PURE means: no Windows.h, no Debug.h, no engine, no globals, no file I/O.  It takes text and
 * returns text and answers.  The CALLER opens files and writes log lines.  That is what lets the
 * offline suite sweep it as step 0 of the plugin build.
 *
 * C++03 (VS2010 v100): no auto, no nullptr, no range-for, no brace-init.
 */
#ifndef COOP_CFGTEXT_H
#define COOP_CFGTEXT_H

#include <cstddef>
#include <string>
#include <vector>

namespace coopcfg {

/* THESE THREE VALUES ARE THE SAME NUMBERS coop::RoleId USES, and config.cpp asserts it at compile
   time so the two enums can never drift apart. */
enum CfgRoleId { kCfgSingle = 0, kCfgHost = 1, kCfgClient = 2 };

/* The parser's own bounds, unchanged from ConfigLoad's inline loop: a file that is not a config
   file cannot make this loop long or this process fat. */
const int    kCfgMaxLines     = 256;
const size_t kCfgMaxLineChars = 512;
const size_t kCfgMaxReadBytes = 131584;   /* 256 * (512 + 2) - what the CALLER should read at most */

/* Field length caps.  ONE place, because the panel caps its EditBoxes with setMaxTextLength using
   these and the validators below refuse using these; two numbers would let a paste through a field
   the validator then refuses for a reason the player cannot see. */
const size_t kCfgPortFieldMax  = 5;
const size_t kCfgAddrFieldMax  = 64;
const size_t kCfgWorldFieldMax = 48;
const size_t kCfgSlotFieldMax  = 16;

/* B13: the player id is exactly 32 lowercase hex characters - coopstore::PlayerIdOk in
   src/common/areaclaim.h is the one test of that, because the notebook refuses a malformed id too.
   It is NOT a panel field: nobody types it, the plugin generates it once into this file. */
const size_t kCfgPlayerIdFieldMax = 32;

/* W1w (decision 59(d)): the player's DISPLAY name, typed once in a 'Your name' box and shown in the Host screen's
   world list. coopworld::DisplayNameOk in src/common/worlddir.h is the one test of it. */
const size_t kCfgPlayerNameFieldMax = 24;

/* P8J-B2 (bench 2, F738): the longest saveroot= / storedir= value accepted. A save root is a folder the engine
   appends slot names and file names to, so it is kept well under MAX_PATH (260). */
const size_t kCfgRootFieldMax = 200;

struct CfgFields
{
    int            role;
    std::string    hostAddr;
    unsigned short hostPort;
    std::string    storeAddr;
    unsigned short storePort;
    std::string    slot;
    std::string    world;
    /* B13: THIS INSTALL'S STABLE NAME, and the only field in here no human writes. Empty means "this file
       has never carried one", which is what makes the plugin generate one and write the file back. It is
       what the notebook keys its slot numbers, its area map and its operator on, so that a notebook killed
       and restarted hands every player back what it had instead of re-deriving it from dial order (T239). */
    std::string    playerId;
    /* W1w (decision 59(d)): the name other players see. Empty = never typed. Like playerId, every writer of this
       file must carry it through, or a settings save forgets it. */
    std::string    playerName;
    /* P8J-B2 (bench 2, F738) - TWO OPTIONAL FOLDER OVERRIDES. Empty = the key is absent = today's folders exactly.
       saveRoot : the SAVE FOLDER - the one that holds the slot folders (s1, s2, ...). The mod looks for a slot's
                  multiplayer-world.key there, and derives the notebook folder as <saveRoot>\coop-store unless storeDir says otherwise.
       storeDir : the NOTEBOOK'S TOP FOLDER (what is <save folder>\coop-store by default). The per-world folders
                  (coop-store\<world>) are made INSIDE it, exactly as they are inside the default one.
       Both pass CfgRootPathOk or are dropped as a bad line. Like playerId, every writer of this file carries them. */
    std::string    saveRoot;
    std::string    storeDir;
    /* PP3: datadir= - the player's DATA FOLDER (identity, the panel's session.cfg, worlds\). A TEST key: only the DLL-side
       shared_wastelands.cfg is read for it (two instances on one PC keep separate identities); CfgFormat never writes it. */
    std::string    dataDir;
    /* settings5 S5 - TEST ONLY: modfingerprint_salt=1 adds a made-up mod (coopmods::kSaltRow) to this game's mod list, so
       a run can prove the join refusal. 0 = absent. No writer carries it: a panel save drops it, which is what a test knob wants. */
    int            modSalt;
    /* prof1 - TEST ONLY: profile=<n> (1..9999) makes a game with no profile screen pick profile n of the world it joins, and
       ask the world to make it when it is missing, so a harness run needs no screen. 0 = absent = the automatic choice (the
       profile played last, else a first one). No writer carries it, like modSalt. */
    int            profileTest;
    /* mmo1: ownsave=on|off - the own-squad record writer's lever (T-251: default ON - every multiplayer world opens the
       player's own records store by itself and writes it; ownsave=off switches it off for a test). No writer carries it. */
    int            ownSave;
    /* M11a S1 - TEST ONLY: joinvia=world makes a CLIENT join through the world server alone (no session dial at the title; its load
       waits for the join gate, which is shut until the flip). 0 = absent = the session road. No writer carries it, like modSalt. */
    int            joinViaWorld;
    /* routerport=0|1 - whether the HOST press asks the home router to forward the world server's port (upnp.cpp).
       1 = absent = ask, which is what a player gets. 0 = never ask: the test harness writes it so test runs leave the
       router alone. Also on/off, true/false, yes/no; anything else is a bad line and the field stays 1. No writer
       carries it, like ownSave. */
    int            routerPort;
    CfgFields();      /* role=single, slot="s1", ports 0, no playerId - ConfigLoad's own defaults */
};

/* One diagnostic from the parser.  bad is 1 for the lines ConfigLoad used to ErrorLog and 0 for the
   ones it DebugLogged, so the caller keeps writing exactly the lines it wrote before. */
struct CfgNote
{
    int         line;
    int         bad;
    std::string text;
};

std::string CfgTrim(const std::string& s);
std::string CfgLower(const std::string& s);

/* THE PORT RULE, on its own so the port FIELD and the port half of an ADDRESS share one
   implementation: at most 5 characters, all digits, and 1..65535.  Out of range is REFUSED and never
   clamped - a clamped port would connect somewhere nobody asked for. */
bool CfgPortTextOk(const std::string& text, unsigned short* port);

/* THE ADDRESS RULE, moved out of config.cpp's SplitAddr with its behaviour unchanged: the LAST colon
   separates address from port, so a bracketed or bare IPv6 literal does not silently lose its tail
   to the first one.  Writes nothing on a refusal. */
bool CfgSplitAddr(const std::string& v, std::string* addr, unsigned short* port);

/* Does the address HALF look like something a resolver could be handed?  NO DNS LOOKUP IS DONE and
   none may be: this runs on the title pump, on a keystroke, and a name lookup can block for seconds.
   The test is a SHAPE test only - it refuses the entries that are certainly wrong (empty, spaces,
   control characters, a character no host name or IP literal can contain, a leading or trailing dot
   or dash, a doubled dot).  It cannot and does not say the host exists. */
bool CfgHostTextPlausible(const std::string& addr);

/* Truncate to maxLen and drop every character below 0x20 - what a paste or a keystroke is reduced to
   before it is stored or written.  A cfg line with an embedded newline would be two lines on the next
   read, which is how a paste turns into a parse error nobody can see. */
std::string CfgSanitiseTyped(const std::string& raw, size_t maxLen);

/* P8J-B2: THE FOLDER RULE for saveroot= and storedir=. Yes only for an ABSOLUTE folder path: a drive letter
   followed by a separator and at least one folder name (C:\x), or a UNC path (\\server\share). Refused: empty,
   relative, a bare drive, longer than kCfgRootFieldMax, a control character, any of " < > | ? * # ; (the last two
   would start a comment when the file is read back), or a ':' anywhere but after the drive letter. Trailing
   separators are dropped, so "C:\x\" and "C:\x" are the same root. Nothing else is rewritten ('/' stays '/').
   *norm is written ONLY on a yes; *why is the reason on a no. Either pointer may be null. */
bool CfgRootPathOk(const std::string& text, std::string* norm, std::string* why);

/* ------------------------------------------------------------------------------------------------
   THE PANEL'S VALIDATORS.  Each answers yes/no AND hands back the exact sentence the status area
   shows, so the words on screen and the words the offline suite checks are the same string.
   ------------------------------------------------------------------------------------------------ */
bool CfgPortFieldOk(const std::string& text, unsigned short* port, std::string* why);
bool CfgAddrFieldOk(const std::string& text, const char* what,
                    std::string* addr, unsigned short* port, std::string* why);
bool CfgNameFieldOk(const std::string& text, const char* what, size_t maxLen, std::string* why);

/* ------------------------------------------------------------------------------------------------
   THE PARSER.  ConfigLoad reads the file into a string (bounded by kCfgMaxReadBytes) and calls this.
   notes receives one entry per line the old loop logged; lines, unknownKeys and badLines are the same
   three numbers its summary line printed.  Any out pointer may be null.
   ------------------------------------------------------------------------------------------------ */
void CfgParseText(const std::string& text, CfgFields* out, std::vector<CfgNote>* notes,
                  int* lines, long long* unknownKeys, long long* badLines);

/* THE WRITER'S TEXT.  Emits the same keys tools/write-cfg.ps1 emits, in the same order, with CRLF
   line endings, and only the lines that carry a value: host= only when hostPort != 0, store= only
   when storePort != 0, slot=/world=/playername=/playerid= only when non-empty.  B13: EVERY writer of this file
   must carry playerid through - the panel included - or a settings save would give this install a new
   name and hand its notebook slot, its areas and possibly its operator flag to somebody else.  An empty hostAddr with a non-zero hostPort
   is written as 0.0.0.0, which is what listening on every interface means and is the address half
   config.cpp's host arm ignores.  W1w-b: a playerName that fails coopworld::DisplayNameOk is NOT written (no
   line at all) and *why, when given, says so; *why is "" when everything given was written. */
std::string CfgFormat(const CfgFields& f, std::string* why = 0);

/* THE COMPLETENESS RULE - the one FallBackToSingle acts on, so the file's rule and the panel's rule
   cannot differ.  0 = complete; 1 = a non-single role with no port; 2 = role=client with no address. */
int CfgIncompleteReason(const CfgFields& f);

}   /* namespace coopcfg */

#endif   /* COOP_CFGTEXT_H */
