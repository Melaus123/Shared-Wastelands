// admin.cpp - THE ADMIN LOGIN AND THE ADMIN COMMANDS, THIS GAME'S END (see admin.h).
//
// THE LOGIN. "/login <password>" typed in the chat never leaves this game as chat or as text: the game asks the world server for a
// challenge (ASK, numbered); the CHALLENGE brings a fresh nonce, the admin password's salt and iteration count, this connection's
// number and the ask's number - only the challenge for this game's latest ask is answered, so a second /login replaces the first;
// the game sends back only the proof (adminauth::MakeProof, bound to the nonce, the connection number, the world id this link's
// WELCOME named and "SW-ADMIN-2"). The typed password is held in one string until the CHALLENGE comes, or for
// adminwords::kPendingMs at most, then wiped (SecureZeroMemory), as are the key and the proof. The RESULT is said in words.
//
// THIS GAME'S STANDING (adminwords::Standing): whether this game is an admin now is the world server's word alone - ADMIN_STANDING
// right behind the WELCOME (none, or owner when the server took this game's hosting secret) and at each change it makes (a login
// taken, /logout, the idle end, the admin password set, this player taken off the admin list). A new link is none until its first
// standing. A logged-in admin that is one no longer, for any reason but its own /logout, is told "You're no longer an admin." once
// (the /logout's RESULT says those words itself).
//
// The world server alone decides who may do what; nothing here gates a command on this game's standing. MAIN THREAD throughout
// (the chat, the store drain and the in-world / title pumps).

#include "admin.h"
#include "store.h"
#include "chat.h"
#include "kapi.h"
#include "coop_log.h"
#include "../common/adminauth.h"
#include "../common/admincmd.h"
#include "../common/adminwords.h"

#include <Windows.h>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string N(long long v) { std::ostringstream o; o.imbue(std::locale::classic()); o << v; return o.str(); }
void Wipe(std::string* s) { if (!s->empty()) ::SecureZeroMemory(&(*s)[0], s->size()); s->clear(); }
void WipeBytes(std::vector<char>* b) { if (!b->empty()) ::SecureZeroMemory(&(*b)[0], b->size()); b->clear(); }

adminwords::Standing g_stand;                  /* the world server's last word on this game's admin standing, and the link it came on */
std::string g_pendingPw;                       /* the typed admin password, until its CHALLENGE (never logged) */
int g_pendingGen = -1;                         /* the link it was asked on */
unsigned g_pendingAt = 0;                      /* GetTickCount at the ASK */
unsigned g_askNo = 0;                          /* the number of this game's latest ASK (0 = none yet): only its CHALLENGE is answered */
unsigned g_nextReq = 0;                        /* ADMIN_REQ numbers (0 is the server's "answering no request") */
long long g_malformed = 0, g_unasked = 0, g_olderChallenges = 0;

bool Linked() { return coop::StoreRelayLinked() && coop::StoreWelcomedThisLink() != 0; }
void Say(const std::string& line) { if (!line.empty()) coop::ChatNoticeLine(-1, std::string(), line); }

std::string StandingName(unsigned s)
{
    if (s == (unsigned)adminauth::kStandOwner) return "the owner's own game";
    if (s == (unsigned)adminauth::kStandAdmin) return "a logged-in admin";
    return "not an admin";
}
std::string WhyName(unsigned w)
{
    switch (w)
    {
    case adminauth::kWhyWelcome:         return "its WELCOME";
    case adminauth::kWhyLogin:           return "a login";
    case adminauth::kWhyLogout:          return "its /logout";
    case adminauth::kWhyIdle:            return "no admin act for " + N((long long)adminauth::kIdleSec) + " s";
    case adminauth::kWhyPasswordChanged: return "the admin password was set";
    case adminauth::kWhyRemoved:         return "its player is no longer the owner or on the admin list";
    default:                             return "reason " + N((long long)w);
    }
}

void PendingWipe(const char* why)
{
    if (g_pendingPw.empty()) return;
    Wipe(&g_pendingPw);
    DebugLog(std::string("[ADMIN] a typed admin password was wiped unused (") + why + ")");
}
bool SendLogin(int kind)
{
    std::vector<char> b; adminauth::EncodeKind(kind, &b);
    return coop::StoreSendAdmin(adminauth::kMsgAdminLogin, b);
}

void LoginTyped(std::string* pw)
{
    if (pw->empty()) { Say(adminwords::LoginNoPasswordLine()); return; }
    if (!Linked()) { Say(adminwords::NotLinkedLine()); return; }
    PendingWipe("a new /login");
    g_askNo = adminauth::NextAskNumber(g_askNo);   /* a CHALLENGE for any earlier ask is ignored from now on */
    std::vector<char> ab; adminauth::EncodeAsk(g_askNo, &ab);
    if (!coop::StoreSendAdmin(adminauth::kMsgAdminLogin, ab)) { Say(adminwords::NotLinkedLine()); return; }
    g_pendingPw.swap(*pw);   /* the caller's copy is now empty; this one is wiped at the CHALLENGE or in AdminTick */
    g_pendingGen = coop::StoreLinkGen(); g_pendingAt = ::GetTickCount();
    DebugLog("[ADMIN] /login: a challenge asked of the world server (the password stays on this game until it comes, then only the proof goes up)");
}
void LogoutTyped()
{
    PendingWipe("/logout");
    if (!Linked()) { Say(adminwords::NotLinkedLine()); return; }
    const unsigned standing = adminwords::StandingNow(g_stand, coop::StoreLinkGen());
    const std::string line = adminwords::LogoutLine(standing);
    if (!line.empty()) { DebugLog("[ADMIN] /logout not sent - this game is " + StandingName(standing)); Say(line); return; }
    if (!SendLogin(adminauth::kKindLogout)) { Say(adminwords::NotLinkedLine()); return; }
    DebugLog("[ADMIN] /logout sent - this game's standing changes when the world server says so");
}

void OnChallenge(const char* p, size_t n)
{
    const int gen = coop::StoreLinkGen();
    if (g_pendingPw.empty() || g_pendingGen != gen)
    {
        ++g_unasked;
        if (g_unasked <= 5) DebugLog("[ADMIN] a CHALLENGE came with no /login waiting on this link - ignored (" + N(g_unasked) + ")");
        return;
    }
    unsigned char nonce[adminauth::kNonceBytes], salt[adminauth::kSaltBytes];
    unsigned iters = 0, session = 0, askNo = 0;
    if (!adminauth::DecodeChallenge(p, n, nonce, salt, &iters, &session, &askNo))
    {
        PendingWipe("its CHALLENGE did not decode");
        ++g_malformed;
        ErrorLog("[ADMIN] the world server's CHALLENGE (" + N((long long)n) + " bytes, iterations " + N((long long)iters) + ") did not decode or names an iteration count outside "
                 + N((long long)adminauth::kIterMin) + "-" + N((long long)adminauth::kIterMax) + " - no proof sent");
        Say(adminwords::ResultLine(adminauth::kResNoChallenge, 0));
        return;
    }
    if (!adminauth::ChallengeIsLatest(askNo, g_askNo))
    {   /* the answer to an earlier /login, overtaken by a newer one: the newer one's CHALLENGE comes next */
        ++g_olderChallenges;
        if (g_olderChallenges <= 5) DebugLog("[ADMIN] a CHALLENGE for an earlier /login came after a newer one was asked - ignored, the newer one's is awaited (" + N(g_olderChallenges) + ")");
        return;
    }
    unsigned char key[adminauth::kKeyBytes], proof[adminauth::kProofBytes];
    adminauth::DeriveKey(g_pendingPw, salt, iters, key);   /* the key's iterations run here, on the main thread, once per login */
    Wipe(&g_pendingPw);
    adminauth::MakeProof(key, nonce, session, coop::StoreLinkWorldId(), proof);
    ::SecureZeroMemory(key, sizeof(key));
    std::vector<char> b; adminauth::EncodeProof(proof, &b);
    ::SecureZeroMemory(proof, sizeof(proof));
    const bool sent = coop::StoreSendAdmin(adminauth::kMsgAdminLogin, b);
    WipeBytes(&b);
    if (!sent) { Say(adminwords::NotLinkedLine()); return; }
    DebugLog("[ADMIN] /login: the proof sent (" + N((long long)iters) + " key iterations)");
}

void OnResult(const char* p, size_t n)
{
    unsigned code = 0, wait = 0;
    if (!adminauth::DecodeResult(p, n, &code, &wait)) { ++g_malformed; ErrorLog("[ADMIN] a RESULT of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    PendingWipe("a RESULT came");
    DebugLog("[ADMIN] login RESULT code " + N((long long)code) + (code == adminauth::kResLocked ? " (wait " + N((long long)wait) + " s)" : std::string()));
    Say(adminwords::ResultLine(code, wait));
}

void OnStanding(const char* p, size_t n)
{
    unsigned standing = 0, why = 0;
    if (!adminauth::DecodeStanding(p, n, &standing, &why)) { ++g_malformed; ErrorLog("[ADMIN] an ADMIN_STANDING of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    std::string line;
    const bool changed = adminwords::StandingTake(&g_stand, coop::StoreLinkGen(), standing, why, &line);
    DebugLog("[ADMIN] the world server says this game is " + StandingName(standing) + " (" + WhyName(why) + ")"
             + (changed ? (adminwords::AdminBy(standing) ? " - it sets the world's rules now" : " - it no longer sets the world's rules") : std::string()));
    if (why == (unsigned)adminauth::kWhyWelcome && standing != (unsigned)adminauth::kStandOwner && coop::StoreOwnerTokenThisLink())
        ErrorLog("[ADMIN] this game showed its hosting secret (HOST_TOKEN) on this link and the world server did not take it - this game is not the world's owner here and does not set the world's rules");
    if (changed) coop::KapiAuthorityChanged("the world server's admin standing");
    Say(line);
}

void OnReply(const char* p, size_t n)
{
    admincmd::Reply r;
    if (!admincmd::DecodeReply(p, n, &r)) { ++g_malformed; ErrorLog("[ADMIN] an ADMIN_REPLY of " + N((long long)n) + " bytes did not decode - ignored"); return; }
    DebugLog("[ADMIN] answer to " + std::string(admincmd::VerbName((int)r.verb)) + " (request " + N((long long)r.req) + "): code " + N((long long)r.code));
    Say(adminwords::ReplyLine(r));
}

}   /* namespace */

namespace coop {

void AdminTyped(std::string* typed)
{
    const int route = adminauth::SlashRoute(*typed);
    if (route == adminauth::kRouteLogin)
    {
        std::string pw = adminauth::LoginPassword(*typed);
        Wipe(typed);
        LoginTyped(&pw);
        Wipe(&pw);
        return;
    }
    if (route == adminauth::kRouteLogout) { LogoutTyped(); return; }
    if (adminauth::FirstWord(*typed) == "/help") { Say(adminwords::HelpLine()); return; }
    admincmd::Cmd c;
    const int pr = admincmd::Parse(*typed, &c);
    if (pr != admincmd::kParseOk)
    {
        DebugLog("[ADMIN] a slash line answered on this game (" + std::string(pr == admincmd::kParseUnknown ? "not a command" : "a command typed wrong") + ")");
        Say(adminwords::ParseLine(pr, c.verb));
        return;
    }
    const int sent = AdminRequest((unsigned)c.verb, c.target, 0u, &c.extra);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    else if (sent < 0) Say(adminwords::UsageLine(c.verb));
}

int AdminRequest(unsigned verb, const std::string& target, unsigned flags, std::string* extra)
{
    admincmd::Req q;
    q.req = ++g_nextReq; if (q.req == 0) q.req = ++g_nextReq;
    q.verb = verb; q.flags = flags; q.target = target;
    if (extra != 0) q.extra = *extra;
    std::vector<char> b;
    const bool encoded = admincmd::EncodeReq(q, &b);
    Wipe(&q.extra);
    if (extra != 0) Wipe(extra);
    if (!encoded) { WipeBytes(&b); return -1; }
    if (!Linked()) { WipeBytes(&b); return 0; }
    const bool sent = StoreSendAdmin(admincmd::kMsgAdminReq, b);
    WipeBytes(&b);
    if (!sent) return 0;
    DebugLog("[ADMIN] " + std::string(admincmd::VerbName((int)verb)) + " sent to the world server (request " + N((long long)q.req) + ")");
    return 1;
}

void AdminArrive(unsigned type, const std::vector<char>& payload)
{
    const char* p = payload.empty() ? 0 : &payload[0];
    const size_t n = payload.size();
    if (type == admincmd::kMsgAdminReply) { OnReply(p, n); return; }
    if (type == adminauth::kMsgAdminStanding) { OnStanding(p, n); return; }
    if (type != adminauth::kMsgAdminLogin) return;
    const int kind = adminauth::KindOf(p, n);
    if (kind == adminauth::kKindChallenge) OnChallenge(p, n);
    else if (kind == adminauth::kKindResult) OnResult(p, n);
    else { ++g_malformed; if (g_malformed <= 5) ErrorLog("[ADMIN] an ADMIN_LOGIN of kind " + N((long long)kind) + " came down - ignored"); }
}

void AdminTick()
{
    const unsigned now = ::GetTickCount();
    const int gen = StoreLinkGen();
    if (!g_pendingPw.empty())
    {
        if (g_pendingGen != gen || !Linked()) PendingWipe("the link to the world server changed");
        else if (adminwords::PendingStale(g_pendingAt, now)) { PendingWipe("its challenge did not come in time"); Say(adminwords::ResultLine(adminauth::kResNoChallenge, 0)); }
    }
    if (g_stand.gen >= 0 && (g_stand.gen != gen || !Linked()))
    {
        std::string line;
        const bool changed = adminwords::StandingLinkEnd(&g_stand, &line);
        DebugLog("[ADMIN] the link to the world server changed or went down - this game is not an admin until the world server's next standing");
        if (changed) KapiAuthorityChanged("the link to the world server ended");
        Say(line);
    }
}

bool AdminNow()
{
    return Linked() && adminwords::AdminBy(adminwords::StandingNow(g_stand, StoreLinkGen()));
}

bool AdminOwnerNow()
{
    return Linked() && adminwords::StandingNow(g_stand, StoreLinkGen()) == (unsigned)adminauth::kStandOwner;
}

int AdminSetPassword(std::string* pw, const char* from)
{
    adminwords::PasswordTrim(pw);
    const int done = adminwords::PasswordDone(*pw);
    if (done != adminwords::kPwSend)
    {
        Wipe(pw);
        DebugLog(std::string("[ADMIN] the admin password not sent from ") + from + " ("
                 + (done == adminwords::kPwKeep ? "the box was empty - left as it is" : done == adminwords::kPwShort ? "under 8 characters" : "a control character or too long") + ")");
        return done;
    }
    const int sent = AdminRequest((unsigned)admincmd::kVerbSetPassword, std::string(), 0u, pw);   /* wipes *pw */
    Wipe(pw);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    DebugLog(std::string("[ADMIN] the admin password ") + (sent == 1 ? "sent" : sent == 0 ? "not sent (not linked)" : "not sent (it would not encode)") + " from " + from);
    return sent == 1 ? done : adminwords::kPwNotSent;
}

int AdminListChange(int slot, bool make, const char* from)
{
    const int sent = AdminRequest((unsigned)adminwords::AdminRowVerb(!make), N((long long)slot), admincmd::kFlagSlot, 0);
    if (sent == 0) Say(adminwords::NotLinkedLine());
    DebugLog(std::string("[ADMIN] ") + adminwords::AdminRowButton(!make) + " for slot " + N((long long)slot) + " from " + from + (sent == 1 ? " - sent" : " - not sent"));
    return sent;
}

std::string AdminTestCommand(const std::string& args)
{
    admincmd::TestAsk a;
    const std::string shown = admincmd::Trim(args);   /* fixed words only: the password is never among them */
    if (!admincmd::TestParse(args, &a))
    {
        DebugLog("[ADMIN] TEST-ONLY admintest REFUSED - not a form it knows (" + N((long long)shown.size()) + " characters, not logged: they may hold a password)");
        return "error admintest usage: setpw sample | makeadmin <slot> | removeadmin <slot> | type login right|login wrong|logout|kick <slot>|ban <slot>|ban nobody|unban <slot>|stop";
    }
    if (a.kind == admincmd::kTestSetPw)
    {
        std::string pw(admincmd::kTestPassword);
        const int done = AdminSetPassword(&pw, "the TEST-ONLY admintest lever");
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - the sample password through the MULTIPLAYER window's DONE road");
        return done == adminwords::kPwSend ? "ok admintest " + shown : "error admintest " + shown + " not-sent";
    }
    if (a.kind == admincmd::kTestMakeAdmin || a.kind == admincmd::kTestRemoveAdmin)
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - the PLAYERS tab's admin row road");
        const int sent = AdminListChange(a.slot, a.kind == admincmd::kTestMakeAdmin, "the TEST-ONLY admintest lever");
        return sent == 1 ? "ok admintest " + shown : "error admintest " + shown + " not-sent";
    }
    std::string name;
    if (admincmd::TestNeedsName(a) && StoreRosterNameOf(a.slot, &name) != 1)
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " REFUSED - slot " + N((long long)a.slot) + " has no name on this link's roster");
        return "error admintest " + shown + " no-name";
    }
    std::string typed = admincmd::TestTypedLine(a, name);
    if (typed.empty())
    {
        DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " REFUSED - the slot's name cannot be typed (a quote mark in it)");
        return "error admintest " + shown + " no-line";
    }
    DebugLog("[ADMIN] TEST-ONLY admintest " + shown + " - typed into the chat-command road");
    AdminTyped(&typed);
    Wipe(&typed);
    return "ok admintest " + shown;
}

}
