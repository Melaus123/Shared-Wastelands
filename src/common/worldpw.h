/* src/common/worldpw.h - THE WORLD'S PASSWORDS, FROM THE RULES FILE'S VALUE OR A TYPED BOX TO THE KEY. Pure: no Windows.h, no disk,
   no globals; the world server (store_main.cpp), the game (store.cpp) and the offline suite compile it. C++03 (VS2010 v100).

   T-1158 step 7 (owner 1368 a / 1371): rules.txt's `world password =` and `admin password =` lines (rulesfile.h) are the world's
   passwords. The text a key is made from is the SAME on every road, so a password written in the file and one typed in a box
   always make the same key:
     - the world password: spaces and tabs at both ends dropped - the file's value, the HOST GAME PASSWORD box (what HOST writes)
       and the JOIN password box alike (store.cpp SetHostPassword / SetJoinPassword);
     - the admin password and the owner password (owner 1369 b): as /login takes a typed one (adminauth::LoginPassword: the
       spaces at both ends dropped) - both are typed after /login, and the world server checks one proof against both keys.
   An empty result is no password. */
#ifndef SW_WORLDPW_H
#define SW_WORLDPW_H

#include <string>
#include <cstring>        /* memset */
#include "rulesfile.h"    /* the password lines, ExportText, IsTopRulesFile */
#include "authproof.h"    /* the world password's key (PBKDF2, kIterations) */
#include "adminauth.h"    /* the admin password's key (kIterDefault), Space */
#include "keeprunning.h"  /* the kept-world start verdicts */

namespace worldpw {

/* the world password a key is made from: the file's value or a box's text, spaces and tabs at both ends dropped */
inline std::string WorldPassword(const std::string& text) { return rulesfile::Trim(text); }
/* the admin password a key is made from: the file's value as /login takes a typed one (the spaces at both ends dropped) */
inline std::string AdminPassword(const std::string& text)
{
    size_t b = text.size(); while (b > 0 && adminauth::Space(text[b - 1])) --b;
    size_t a = 0; while (a < b && adminauth::Space(text[a])) ++a;
    return text.substr(a, b - a);
}
/* the owner password a key is made from: as the admin password (it is typed after /login too) */
inline std::string OwnerPassword(const std::string& text) { return AdminPassword(text); }
inline void WorldKey(const std::string& text, const unsigned char salt[authproof::kSaltBytes], unsigned char key[authproof::kKeyBytes])
{
    authproof::DeriveKey(WorldPassword(text), salt, key);
}
/* rounds: kIterDefault, or the rounds of a 0.4.0 admin key the world carries (owner 1513 a: the login salt and rounds are then that
   key's, store_main.cpp PasswordsApply) */
inline void AdminKey(const std::string& text, const unsigned char salt[adminauth::kSaltBytes], unsigned char key[adminauth::kKeyBytes],
                     unsigned rounds = adminauth::kIterDefault)
{
    adminauth::DeriveKey(AdminPassword(text), salt, rounds, key);
}
/* the owner password's key: made with the admin password's salt and rounds, so the one CHALLENGE a /login gets serves both and the
   game's one proof is checked against each key (store_main.cpp OnAdminLogin). While the world carries a 0.4.0 admin key (owner
   1513 a) the salt and rounds are that key's, so the owner key is made with them too. */
inline void OwnerKey(const std::string& text, const unsigned char salt[adminauth::kSaltBytes], unsigned char key[adminauth::kKeyBytes],
                     unsigned rounds = adminauth::kIterDefault)
{
    adminauth::DeriveKey(OwnerPassword(text), salt, rounds, key);
}
/* HOST's owner password (owner 1369 b / 1371): the value the world's rules.txt holds now when it holds one (a person's own, or the one
   an earlier HOST made - the file keeps it, so the host is logged in as owner at every start and rejoin), else the fresh one given
   (made by the caller from Windows' random source). *write = the file must get it. */
inline std::string HostOwnerPassword(const std::string& rulesText, const std::string& fresh, bool* write)
{
    const std::string had = OwnerPassword(rulesfile::Parse(rulesText, 0).pw[rulesfile::kPwOwner]);
    *write = had.empty();
    return had.empty() ? fresh : had;
}
/* the long random owner password HOST writes: 32 random bytes as 64 hex digits */
const size_t kOwnerRandomBytes = 32;
inline std::string OwnerPasswordFromRandom(const unsigned char* bytes, size_t n) { return authproof::Hex(bytes, n); }

/* THE 0.4.0 PASSWORD FILES (owner 1513 a, 2026-11-05: "can't we just move the old password to the new location?"; 1508 b: a world
   that had a password does not become open). 0.4.0 kept the world password's key in the world folder's auth.txt
   (`1 <salt> <key>`, authproof::AuthLine - the same PBKDF2-HMAC-SHA256, kIterations and 16-byte salt the door's proof uses now) and the
   admin password's key in admin-auth.txt (`adm1 <rounds> <salt> <key>`, adminauth::AuthLine). admin-auth.txt is never written,
   renamed or deleted by this version; auth.txt is also the world password's lock this version's world server saves (T-1255 B,
   owner 1517 a: LockStep below), in the same format. While rules.txt has NO line for that password (absent - an empty line is the owner's word "no password"),
   the 0.4.0 key is the world's lock for it (Carried): its text is not known, so a player types the 0.4.0 password and the proof is
   checked against the carried key. A file that is there but holds no key line (kOldBad) or cannot be read (kOldUnreadable) lets
   nobody in by that password (fail closed) until it can be read or the line is written. kOldNone: no file, or one with no key line -
   nothing is carried. */
enum { kOldNone = 0, kOldKey = 1, kOldBad = 2, kOldUnreadable = 3 };
/* auth.txt as read: readOk false = there but not readable; present false = not there. salt / key are written only for kOldKey. */
inline int OldWorldFile(bool readOk, bool present, const std::string& text, unsigned char salt[authproof::kSaltBytes], unsigned char key[authproof::kKeyBytes])
{
    if (!readOk) return kOldUnreadable;
    if (!present) return kOldNone;
    int bad = 0;
    const int v = authproof::AuthTextParse(text, salt, key, &bad);
    return v == authproof::kAuthKey ? (int)kOldKey : v == authproof::kAuthBad ? (int)kOldBad : (int)kOldNone;
}
/* admin-auth.txt as read: as OldWorldFile, with the key's rounds (adminauth::AuthTextParse: within kIterMin..kIterMax, else bad) */
inline int OldAdminFile(bool readOk, bool present, const std::string& text, unsigned* rounds, unsigned char salt[adminauth::kSaltBytes], unsigned char key[adminauth::kKeyBytes])
{
    if (!readOk) return kOldUnreadable;
    if (!present) return kOldNone;
    const int v = adminauth::AuthTextParse(text, rounds, salt, key);
    return v == adminauth::kFileKey ? (int)kOldKey : v == adminauth::kFileBad ? (int)kOldBad : (int)kOldNone;
}
/* the 0.4.0 lock is in force for one password: rules.txt has no line for it and its 0.4.0 file is not kOldNone */
inline bool Carried(bool lineThere, int oldFile) { return !lineThere && oldFile != kOldNone; }
/* the world password's state from one read of rules.txt (lineThere / lineValue: its world password line): the carried lock's -
   authproof::kAuthKey for a 0.4.0 key, kAuthBad (nobody in by the world password) for a bad or unreadable auth.txt - else the line's:
   kAuthNone when empty, kAuthKey */
inline int WorldPwState(bool lineThere, const std::string& lineValue, int oldWorld)
{
    if (Carried(lineThere, oldWorld)) return oldWorld == kOldKey ? (int)authproof::kAuthKey : (int)authproof::kAuthBad;
    return WorldPassword(lineValue).empty() ? (int)authproof::kAuthNone : (int)authproof::kAuthKey;
}
/* the admin password's state likewise: adminauth::kFileKey for a carried key, kFileUnreadable (no admin login) for a bad or
   unreadable admin-auth.txt, else the line's: kFileNone when empty, kFileKey */
inline int AdminPwState(bool lineThere, const std::string& lineValue, int oldAdmin)
{
    if (Carried(lineThere, oldAdmin)) return oldAdmin == kOldKey ? (int)adminauth::kFileKey : (int)adminauth::kFileUnreadable;
    return AdminPassword(lineValue).empty() ? (int)adminauth::kFileNone : (int)adminauth::kFileKey;
}
/* THE LINES A WRITER LEAVES OUT (rulesfile::WritePasswords / WriteBack / Fresh leaveOut): a password line the world's rules.txt does
   not have is not added while a 0.4.0 file carries a lock for it - an added empty line would be "no password" (owner 1508 b). The
   world server's writes (the missing lines it adds, a rules.txt made again) leave these out. */
inline unsigned CarryLeaveOut(int oldWorld, int oldAdmin)
{
    return (oldWorld != kOldNone ? 1u << rulesfile::kPwWorld : 0u) | (oldAdmin != kOldNone ? 1u << rulesfile::kPwAdmin : 0u);
}
/* HOST's password write (store.cpp StoreWriteWorldPassword): on a world running now (writeWorld false) the world and admin lines are
   never added - only the owner line is HOST's there (the running world server adds or carries the others itself); else the world line
   is left out when the HOST GAME PASSWORD box is empty and auth.txt carries a lock (owner 1508 b: the carried lock stays; the owner
   makes the world open only by writing an empty line by hand), and the admin line when admin-auth.txt carries one. A typed password
   is always written; a line the file already has is never taken out. */
inline unsigned HostLeaveOut(bool writeWorld, bool boxEmpty, int oldWorld, int oldAdmin)
{
    if (!writeWorld) return (1u << rulesfile::kPwWorld) | (1u << rulesfile::kPwAdmin);
    return ((boxEmpty && oldWorld != kOldNone) ? 1u << rulesfile::kPwWorld : 0u) | (oldAdmin != kOldNone ? 1u << rulesfile::kPwAdmin : 0u);
}
/* THE WORLD HAS A PASSWORD (T-1255 A / D, owner 1515 / 1516 a): a world password line that is not empty, or - no line - a saved lock that
   applies (auth.txt's key, 0.4.0's or the one this version's world server saved; or an auth.txt that cannot be used, which lets nobody in
   by the world password, so the world is not open either). */
inline bool WorldHasPassword(bool lineThere, const std::string& lineValue, int oldWorld) { return WorldPwState(lineThere, lineValue, oldWorld) != authproof::kAuthNone; }
/* T-1255 A (owner 1516 a): HOST's world password line (store.cpp StoreWriteWorldPassword) - never on a world running now (writeWorld
   false); with the KEEP RUNNING box ticked and the PASSWORD box empty, only when the world has no password at all (WorldHasPassword
   false: an empty line is written where there is none) - the world password the world already has, its line or its saved lock, is
   kept; else the box's text (a typed password replaces the line; unticked, an empty box is an open world for that hosting, as before). */
inline bool HostWritesWorld(bool writeWorld, bool keepTicked, bool boxEmpty, bool lineThere, const std::string& lineValue, int oldWorld)
{
    if (!writeWorld) return false;
    if (!keepTicked || !boxEmpty) return true;
    return !WorldHasPassword(lineThere, lineValue, oldWorld);
}

/* THE WORLD PASSWORD'S SAVED LOCK (T-1255 B, owner 1517 a: "a world that had a password shouldn't become open"; an emptied line still
   opens it on purpose). The world server saves the lock of the world password it enforces - salt and key, never the text - in the world
   folder's auth.txt, in 0.4.0's format (LockText: authproof::AuthLine, the key from WorldKey - authproof::DeriveKey, the same salt size,
   PBKDF2-SHA256 rounds and key size OldWorldFile's key is checked with at the door), so a later start whose rules.txt has NO world
   password line takes it as it takes 0.4.0's (Carried). After each read of rules.txt that sets the world password (store_main.cpp
   PasswordsApply - not while a kept world keeps its password over an emptied line, KeepsOldPassword: the lock mirrors what the server
   enforces): kLockSave for a key from rules.txt; kLockClear for no world password (the line is there and empty - nothing is carried
   then) - auth.txt is written empty, which OldWorldFile reads as kOldNone, so a later deleted line does not lock the world again;
   kLockLeave for a carried lock (it is the saved one) or a bad one. */
enum { kLockLeave = 0, kLockSave = 1, kLockClear = 2 };
inline int LockStep(int worldState, bool carried)
{
    if (worldState == authproof::kAuthKey && !carried) return kLockSave;
    if (worldState == authproof::kAuthNone) return kLockClear;
    return kLockLeave;
}
inline std::string LockText(const unsigned char salt[authproof::kSaltBytes], const unsigned char key[authproof::kKeyBytes]) { return authproof::AuthLine(salt, key) + "\n"; }

/* A KEPT WORLD'S START (owner 1371, 1508 b, 1513 a): rules.txt as the world server found it (after its retries) and, when read, its
   text; oldWorld: auth.txt's answer (OldWorldFile). keeprun::kStartServe (a world password, or the 0.4.0 lock auth.txt carries for a
   rules.txt with no world password line - Carried), kStartOpen (no world password: absent file, no line, or an empty one, and nothing
   carried - the world serves open and its window says keeprun::OpenLine), or kStartUnreadable (the file is there and could not be
   read - never taken for "no password"). */
enum { kRulesRead = 0, kRulesAbsent = 1, kRulesUnreadable = 2 };
inline int KeptStart(bool keepRunning, int rulesState, const std::string& rulesText, int oldWorld = kOldNone)
{
    if (!keepRunning) return keeprun::kStartServe;
    if (rulesState == kRulesUnreadable) return keeprun::kStartUnreadable;
    const rulesfile::Parsed p = rulesfile::Parse(rulesState == kRulesRead ? rulesText : std::string(), 0);
    if (Carried(p.pwThere[rulesfile::kPwWorld], oldWorld)) return keeprun::kStartServe;
    const bool set = rulesState == kRulesRead && !WorldPassword(p.pw[rulesfile::kPwWorld]).empty();
    return keeprun::StartDecide(true, set);
}
/* A WORLD KEPT RUNNING KEEPS ITS PASSWORD (owner 1374 (2)): a read of rules.txt that finds no world password (the line emptied, or
   the file deleted and made again) while the world, kept running, has one - the world keeps the key it had (and says
   keeprun::KeepsPasswordLine once). */
inline bool KeepsOldPassword(bool keepRunning, bool hadPassword, bool nowPassword) { return keepRunning && hadPassword && !nowPassword; }

/* A WORLD EXPORT'S COPY OF A FILE (store_main.cpp KeepExport): the bytes as read, except the top rules.txt, whose password lines are
   copied empty (rulesfile::ExportText). The manifest's size and checksum are of these bytes. */
inline std::string ExportBytes(const std::string& rel, const std::string& bytes)
{
    return rulesfile::IsTopRulesFile(rel) ? rulesfile::ExportText(bytes) : bytes;
}

/* T-247 (owner 1706 b): THE PERSON WHO LAST PROVED THE OWNER PASSWORD - kOwnerProvenFile beside rules.txt and admins.txt, one line
   "own2 <rounds> <salt hex> <check hex> <person id>", so a later start can tell whether rules.txt's owner password is still the one
   that person proved (OwnerProvenStillHolds) without keeping the password. A changed or emptied owner password clears the record;
   whoever proves the new one is recorded.
   T-247 re-check fold (item 1): the check is NEVER a login key. A login key is adminauth::DeriveKey(password, the run's login salt) and
   that key plus a CHALLENGE's nonce is a valid owner door proof (authproof::LoginDoorProof). The record's check goes the same stored-
   password road (adminauth::DeriveKey, PBKDF2-HMAC-SHA256, as admin-auth.txt's adm1 line) but with its OWN salt, fresh from Windows'
   random source at every write, and over the password with kOwnerProvenLabel in front - a text no typed or rules.txt password can be
   (it holds a line break) - so it is not the key of any password under any salt a CHALLENGE may name. An "own1" line (this branch's
   first form, which held the login key itself; never shipped) is no record. The file never travels with a world export
   (worldkeep::ExportTakes). */
const char* const kOwnerProvenFile = "owner-proven.txt";
const char* const kOwnerProvenTag = "own2";
const char kOwnerProvenLabel[] = "shared-wastelands owner-proven record\n";
/* the record's check for an owner password (rules.txt's text; the spaces at both ends dropped as everywhere) with the record's salt */
inline void OwnerProvenCheck(const std::string& ownerPwText, const unsigned char salt[adminauth::kSaltBytes], unsigned rounds, unsigned char check[adminauth::kKeyBytes])
{
    adminauth::DeriveKey(std::string(kOwnerProvenLabel) + OwnerPassword(ownerPwText), salt, rounds, check);
}
inline std::string OwnerProvenText(const std::string& person, unsigned rounds, const unsigned char salt[adminauth::kSaltBytes], const unsigned char check[adminauth::kKeyBytes])
{
    return adminauth::AuthLine(rounds, salt, check).replace(0, 4, kOwnerProvenTag) + " " + person + "\n";
}
/* the line a write puts in the file: the owner password's check under freshSalt (made by the caller from Windows' random source) */
inline std::string OwnerProvenMake(const std::string& ownerPwText, const std::string& person, const unsigned char freshSalt[adminauth::kSaltBytes])
{
    unsigned char c[adminauth::kKeyBytes];
    OwnerProvenCheck(ownerPwText, freshSalt, adminauth::kIterDefault, c);
    const std::string text = OwnerProvenText(person, adminauth::kIterDefault, freshSalt, c);
    std::memset(c, 0, sizeof c);
    return text;
}
/* true = one well-formed record line (a person id of letters and digits only); anything else - an "own1" line too - is no record */
inline bool OwnerProvenParse(const std::string& text, std::string* person, unsigned* rounds, unsigned char salt[adminauth::kSaltBytes], unsigned char check[adminauth::kKeyBytes])
{
    person->clear();
    std::string line = text.substr(0, text.find('\n'));
    while (!line.empty() && adminauth::Space(line[line.size() - 1])) line.erase(line.size() - 1);
    const size_t sp = line.rfind(' ');
    if (line.compare(0, 5, std::string(kOwnerProvenTag) + " ") != 0 || sp == std::string::npos || sp + 1 >= line.size()) return false;
    const std::string who = line.substr(sp + 1);
    for (size_t i = 0; i < who.size(); ++i)
    {
        const char c = who[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    const std::string adm = "adm1" + line.substr(4, sp - 4);
    if (adminauth::AuthTextParse(adm, rounds, salt, check) != adminauth::kFileKey) return false;
    *person = who;
    return true;
}
/* the record still names the owner: rules.txt's owner password now (its text) makes the record's check with the record's salt and rounds */
inline bool OwnerProvenStillHolds(const std::string& ownerPwText, unsigned rounds, const unsigned char salt[adminauth::kSaltBytes], const unsigned char check[adminauth::kKeyBytes])
{
    if (OwnerPassword(ownerPwText).empty()) return false;
    unsigned char k[adminauth::kKeyBytes];
    OwnerProvenCheck(ownerPwText, salt, rounds, k);
    const bool same = authproof::SameBytes(k, check, sizeof k);
    std::memset(k, 0, sizeof k);
    return same;
}

}   /* namespace worldpw */

#endif
