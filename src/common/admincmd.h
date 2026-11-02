/* src/common/admincmd.h - THE ADMIN COMMANDS: the typed line, the request and the reply on the wire, who may do what, admins.txt
   and bans.txt. Pure: no Windows.h, no disk, no clock, no engine; the world server (store_main.cpp), the game and the offline suite
   compile it. C++03 (VS2010 v100). Ported in part from public pull request 3 (author MassDivide) and changed for this project.

   MESSAGES (world-server protocol 132). 96-103 are kept for the admin work; this build uses 97, 100, 101 and 103 (adminauth.h).
     97 ADMIN_REQ (game -> server):  {u32 req, u32 verb, u32 flags, str8 target, str8 extra}
     100 ADMIN_REPLY (server -> game): {u32 req, u32 verb, u32 code, u32 num, str8 name}
   str8 = u8 n + n bytes; u32 little-endian. req is the game's own number for the request, echoed in its reply (0 = a reply that
   answers no request). The reply carries a CODE (kRep*), never words: the game shows the approved words for each code, with `name`
   (the player it concerns, cleaned) and `num` where a code says so.

   VERBS AND THEIR FIELDS
     kVerbKick        target = a player's name, or (kFlagSlot) a slot number in decimal       extra = unused
     kVerbBan         as kick                                                                 extra = the reason (may be empty)
     kVerbUnban       target = a player id, or a name as bans.txt has it                       extra = unused
     kVerbStop        no target - the world stops cleanly (WORLD_STOPPING to every game, then the final save)
     kVerbMakeAdmin   target = (kFlagSlot) the player's slot number                              owner's game only
     kVerbRemoveAdmin target = (kFlagSlot) the player's slot number, or a listed player id       owner's game only
     kVerbSetPassword extra = the new admin password (at least adminauth::kPasswordMinChars)     owner's game only
   WHO MAY (Decide): kick, ban, unban and stop need an admin now - the owner's game that proved the hosting's secret, or a logged-in
   admin. MAKE ADMIN, REMOVE ADMIN and the password: the owner's game that proved the hosting's secret alone. Nobody kicks or bans
   themself or the owner; an admin kicks or bans another admin only when the asker is that owner's game.

   admins.txt (the world folder): one player id per line (32 lowercase hex); '#' starts a comment; blank lines are skipped; any other
   line is counted bad and kept as written when the server edits the file. bans.txt: "<player id> <name> <reason...>" per line, the
   name one word (spaces as '_'), the reason the rest of the line; comments and bad lines likewise. A player id comes from the
   player's own game, so a ban is a fence for honest games: a player who changes their id is not stopped by it. */
#ifndef SW_ADMINCMD_H
#define SW_ADMINCMD_H

#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace admincmd {

const unsigned kMsgAdminReq = 97;
const unsigned kMsgAdminReply = 100;
const unsigned kMsgReservedFirst = 96, kMsgReservedLast = 103;   /* 96, 98, 99, 102 stay unused until their own reviews */

enum Verb { kVerbNone = 0, kVerbKick = 1, kVerbBan = 2, kVerbUnban = 3, kVerbStop = 4, kVerbMakeAdmin = 5, kVerbRemoveAdmin = 6,
            kVerbSetPassword = 7, kVerbCount = 8 };
const unsigned kFlagSlot = 1u;          /* target is a slot number in decimal */
const unsigned kFlagsKnown = kFlagSlot;
const size_t kTargetMax = 64, kExtraMax = 128;
const size_t kIdLen = 32;

/* reply codes - the game shows the approved words for each */
enum Rep { kRepOk = 0,              /* done: kick / ban / unban (name = who), stop (the world is stopping), admin made / removed (name), password set */
           kRepNotAdmin = 1,        /* not an admin now: log in first (the same answer whether or not the player is listed) */
           kRepOwnerOnly = 2,       /* only the owner's own game may do this */
           kRepNoSuchPlayer = 3,    /* no player by that name (or slot) is in the world */
           kRepManyPlayers = 4,     /* more than one player in the world has that name, or (/unban by name) more than one line of bans.txt
                                       does - nothing was removed, unban by the player id: num = how many */
           kRepNotYourself = 5,     /* you cannot do this to yourself */
           kRepProtected = 6,       /* the owner cannot be kicked or banned; another admin only by the owner */
           kRepNotBanned = 7,       /* /unban: nobody by that name or id is in bans.txt */
           kRepWriteFailed = 8,     /* the world folder's file could not be written: nothing changed */
           kRepUnreadable = 9,      /* the world folder's file is there but could not be read: nothing changed */
           kRepAlready = 10,        /* already so: already an admin, already banned (still kicked) */
           kRepPasswordShort = 11,  /* the admin password is shorter than adminauth::kPasswordMinChars characters (or has a control byte) */
           kRepBadRequest = 12,     /* the request could not be read, or names no verb this server knows */
           kRepNotListed = 13,      /* REMOVE ADMIN: that player is not on the admin list */
           kRepNoId = 14,           /* the player's game named no player id the server can keep */
           kRepCount = 15 };

/* ---- the typed line (the game's): "/kick <name>", "/ban <name> [reason]", "/unban <name or id>", "/stop" ---- */
inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && IsSpace(s[a])) ++a;
    while (b > a && IsSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string LowerAscii(const std::string& s) { std::string o(s); for (size_t i = 0; i < o.size(); ++i) if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a'); return o; }
/* the verb a command word names ("/kick", any case); kVerbNone = not a typed command (the owner-only verbs come from buttons) */
inline int VerbOfWord(const std::string& w)
{
    const std::string l = LowerAscii(w);
    if (l == "/kick") return kVerbKick;
    if (l == "/ban") return kVerbBan;
    if (l == "/unban") return kVerbUnban;
    if (l == "/stop") return kVerbStop;
    return kVerbNone;
}
inline const char* VerbName(int v)
{
    switch (v)
    {
    case kVerbKick: return "/kick";
    case kVerbBan: return "/ban";
    case kVerbUnban: return "/unban";
    case kVerbStop: return "/stop";
    case kVerbMakeAdmin: return "make-admin";
    case kVerbRemoveAdmin: return "remove-admin";
    case kVerbSetPassword: return "set-admin-password";
    default: return "unknown";
    }
}
/* a name as it travels: control bytes dropped, trimmed, cut to `cap` bytes (never inside a UTF-8 character) */
inline std::string CleanText(const std::string& raw, size_t cap)
{
    std::string o;
    for (size_t i = 0; i < raw.size(); ++i) { const unsigned char c = (unsigned char)raw[i]; if (c >= 0x20 && c != 0x7F) o += (char)c; }
    o = Trim(o);
    if (o.size() > cap) { size_t n = cap; while (n > 0 && ((unsigned char)o[n] & 0xC0) == 0x80) --n; o = Trim(o.substr(0, n)); }
    return o;
}
enum { kParseOk = 0, kParseNotCommand = 1, kParseUnknown = 2, kParseNeedsName = 3, kParseExtra = 4 };
struct Cmd { int verb; std::string target, extra; Cmd() : verb(kVerbNone) {} };
/* a typed slash line: kParseOk with *out filled; kParseUnknown = '/' and another word; kParseNeedsName = /kick, /ban or /unban with
   no name; kParseExtra = /stop or /unban with words after it. A name with spaces may be quoted: /ban "Big Al" griefing. */
inline int Parse(const std::string& typed, Cmd* out)
{
    *out = Cmd();
    const std::string t = Trim(typed);
    if (t.empty() || t[0] != '/') return kParseNotCommand;
    size_t a = 0; while (a < t.size() && !IsSpace(t[a])) ++a;
    const int v = VerbOfWord(t.substr(0, a));
    if (v == kVerbNone) return kParseUnknown;
    std::string rest = Trim(t.substr(a));
    out->verb = v;
    if (v == kVerbStop) return rest.empty() ? kParseOk : kParseExtra;
    if (rest.empty()) return kParseNeedsName;
    std::string name, after;
    if (rest[0] == '"')
    {
        const size_t q = rest.find('"', 1);
        if (q == std::string::npos) { name = rest.substr(1); after.clear(); }
        else { name = rest.substr(1, q - 1); after = rest.substr(q + 1); }
    }
    else
    {
        size_t b = 0; while (b < rest.size() && !IsSpace(rest[b])) ++b;
        name = rest.substr(0, b); after = rest.substr(b);
    }
    out->target = CleanText(name, kTargetMax);
    after = Trim(after);
    if (out->target.empty()) return kParseNeedsName;
    if (v == kVerbUnban && !after.empty()) return kParseExtra;
    if (v == kVerbBan) out->extra = CleanText(after, kExtraMax);
    else if (!after.empty()) return kParseExtra;
    return kParseOk;
}

/* ---- the wire ---- */
struct Req { unsigned req, verb, flags; std::string target, extra; Req() : req(0), verb(0), flags(0) {} };
struct Reply { unsigned req, verb, code, num; std::string name; Reply() : req(0), verb(0), code(0), num(0) {} };
inline void PutU32(std::vector<char>* b, unsigned v) { for (int i = 0; i < 4; ++i) b->push_back((char)(unsigned char)(v >> (8 * i))); }
inline bool GetU32(const char* p, size_t n, size_t* at, unsigned* v)
{
    if (*at > n || n - *at < 4) return false;
    *v = 0; for (int i = 0; i < 4; ++i) *v |= (unsigned)(unsigned char)p[*at + i] << (8 * i);
    *at += 4; return true;
}
inline bool PutStr8(std::vector<char>* b, const std::string& s, size_t cap)
{
    if (s.size() > cap || s.size() > 255) return false;
    b->push_back((char)(unsigned char)s.size());
    b->insert(b->end(), s.begin(), s.end());
    return true;
}
inline bool GetStr8(const char* p, size_t n, size_t* at, size_t cap, std::string* s)
{
    if (*at >= n) return false;
    const size_t len = (size_t)(unsigned char)p[*at];
    if (len > cap || n - *at - 1 < len) return false;
    s->assign(p + *at + 1, len);
    for (size_t i = 0; i < len; ++i) if ((unsigned char)(*s)[i] < 0x20 || (unsigned char)(*s)[i] == 0x7F) return false;
    *at += 1 + len; return true;
}
inline bool EncodeReq(const Req& r, std::vector<char>* b)
{
    b->clear(); PutU32(b, r.req); PutU32(b, r.verb); PutU32(b, r.flags);
    return PutStr8(b, r.target, kTargetMax) && PutStr8(b, r.extra, kExtraMax);
}
/* the whole payload, nothing left over; a verb this build knows; known flags only; no control byte in either text */
inline bool DecodeReq(const char* p, size_t n, Req* r)
{
    size_t at = 0;
    if (p == 0 && n != 0) return false;
    if (!GetU32(p, n, &at, &r->req) || !GetU32(p, n, &at, &r->verb) || !GetU32(p, n, &at, &r->flags)) return false;
    if (!GetStr8(p, n, &at, kTargetMax, &r->target) || !GetStr8(p, n, &at, kExtraMax, &r->extra)) return false;
    if (at != n || r->verb == kVerbNone || r->verb >= (unsigned)kVerbCount || (r->flags & ~kFlagsKnown) != 0) return false;
    return true;
}
inline bool EncodeReply(const Reply& r, std::vector<char>* b)
{
    b->clear(); PutU32(b, r.req); PutU32(b, r.verb); PutU32(b, r.code); PutU32(b, r.num);
    return PutStr8(b, r.name, kTargetMax);
}
inline bool DecodeReply(const char* p, size_t n, Reply* r)
{
    size_t at = 0;
    if (p == 0 && n != 0) return false;
    if (!GetU32(p, n, &at, &r->req) || !GetU32(p, n, &at, &r->verb) || !GetU32(p, n, &at, &r->code) || !GetU32(p, n, &at, &r->num)) return false;
    if (!GetStr8(p, n, &at, kTargetMax, &r->name)) return false;
    return at == n && r->code < (unsigned)kRepCount;
}
/* a slot number in decimal (kFlagSlot): 1 to 4 digits */
inline bool SlotText(const std::string& t, int* slot)
{
    if (t.empty() || t.size() > 4) return false;
    int v = 0;
    for (size_t i = 0; i < t.size(); ++i) { if (t[i] < '0' || t[i] > '9') return false; v = v * 10 + (t[i] - '0'); }
    *slot = v; return true;
}

/* ---- who may ---- */
struct Asker { bool ownerToken; bool loggedIn; Asker() : ownerToken(false), loggedIn(false) {} };
/* ownerToken = the target is a connection that proved HOST_TOKEN (the owner's own game); owner = the target's game names the owner's
   player id without being that connection */
struct Target { bool self, owner, admin, ownerToken; Target() : self(false), owner(false), admin(false), ownerToken(false) {} };
/* is the asker an admin now (the owner's token, or logged in) */
inline bool AdminNow(const Asker& a) { return a.ownerToken || a.loggedIn; }
/* may this asker do this verb at all (before the target is known): kRepOk, kRepNotAdmin, kRepOwnerOnly, kRepBadRequest */
inline int MayAsk(int verb, const Asker& a)
{
    if (verb <= kVerbNone || verb >= kVerbCount) return kRepBadRequest;
    if (verb == kVerbMakeAdmin || verb == kVerbRemoveAdmin || verb == kVerbSetPassword) return a.ownerToken ? kRepOk : (a.loggedIn ? kRepOwnerOnly : kRepNotAdmin);
    return AdminNow(a) ? kRepOk : kRepNotAdmin;
}
/* kick / ban on a player: kRepOk, kRepNotYourself, kRepProtected. The owner's own game (the connection that proved HOST_TOKEN) is
   never kicked or banned. A game naming the owner's player id on another connection is protected from every admin but the owner's own
   game, which may kick it - never ban it, since that would put the owner's own id in bans.txt. Another admin only by the owner's
   own game. */
inline int MayActOn(int verb, const Asker& a, const Target& t)
{
    const int m = MayAsk(verb, a);
    if (m != kRepOk) return m;
    if (verb != kVerbKick && verb != kVerbBan) return kRepOk;
    if (t.self) return kRepNotYourself;
    if (t.ownerToken) return kRepProtected;
    if (t.owner && (!a.ownerToken || verb == kVerbBan)) return kRepProtected;
    if (t.admin && !a.ownerToken) return kRepProtected;
    return kRepOk;
}

/* ---- the PLAYERS roster's op bits (joinstage.h RosterRow.op) ---- */
const unsigned kOpSteward = 1u, kOpAdmin = 2u, kOpOwner = 4u, kOpBits = 7u;
/* one row's bits for one recipient: the steward bit for everyone; the admin-list and owner bits only in the copy of the owner's own
   game (it alone may MAKE / REMOVE ADMIN), so nobody else learns who is listed. A login is never marked. */
inline unsigned RosterOp(bool steward, bool listedAdmin, bool owner, bool recipientOwnerToken)
{
    unsigned op = steward ? kOpSteward : 0u;
    if (recipientOwnerToken) { if (listedAdmin) op |= kOpAdmin; if (owner) op |= kOpOwner; }
    return op;
}

/* ---- admins.txt and bans.txt ---- */
inline bool IdOk(const std::string& id)
{
    if (id.size() != kIdLen) return false;
    for (size_t i = 0; i < id.size(); ++i) { const char c = id[i]; if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; }
    return true;
}
/* the lines of a text, '\r' dropped, the last one kept even without its '\n' */
inline std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> out;
    size_t a = 0;
    while (a < text.size())
    {
        size_t e = text.find('\n', a); if (e == std::string::npos) e = text.size();
        std::string l = text.substr(a, e - a);
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        out.push_back(l);
        a = e + 1;
    }
    return out;
}
inline std::string JoinLines(const std::vector<std::string>& lines)
{
    std::string s;
    for (size_t i = 0; i < lines.size(); ++i) s += lines[i] + "\n";   /* the server's writer opens its file in text mode: each '\n' is written as CR LF */
    return s;
}
inline bool CommentOrBlank(const std::string& line) { const std::string t = Trim(line); return t.empty() || t[0] == '#'; }
/* admins.txt: the listed ids in order (a repeat once); *bad = lines that are neither a comment nor an id */
inline std::vector<std::string> AdminsParse(const std::string& text, int* bad)
{
    std::vector<std::string> ids; int nb = 0;
    const std::vector<std::string> ls = Lines(text);
    for (size_t i = 0; i < ls.size(); ++i)
    {
        if (CommentOrBlank(ls[i])) continue;
        const std::string id = Trim(ls[i]);
        if (!IdOk(id)) { ++nb; continue; }
        bool seen = false; for (size_t k = 0; k < ids.size(); ++k) if (ids[k] == id) seen = true;
        if (!seen) ids.push_back(id);
    }
    if (bad != 0) *bad = nb;
    return ids;
}
static const char kAdminsHeader[] = "# Admins of this world: one player id per line. Written by the world server when the owner makes or removes an admin.";
/* admins.txt with `id` added (add) or every line naming it removed; comments and bad lines kept as written. *changed = false when
   the id was already there (add) or not there (remove) - the text is then returned unchanged. */
inline std::string AdminsEdit(const std::string& text, const std::string& id, bool add, bool* changed)
{
    *changed = false;
    std::vector<std::string> ls = Lines(text), out;
    bool there = false;
    for (size_t i = 0; i < ls.size(); ++i) if (!CommentOrBlank(ls[i]) && Trim(ls[i]) == id) there = true;
    if (add)
    {
        if (there || !IdOk(id)) return text;
        if (ls.empty()) ls.push_back(kAdminsHeader);
        ls.push_back(id);
        *changed = true;
        return JoinLines(ls);
    }
    if (!there) return text;
    for (size_t i = 0; i < ls.size(); ++i) if (CommentOrBlank(ls[i]) || Trim(ls[i]) != id) out.push_back(ls[i]);
    *changed = true;
    return JoinLines(out);
}

struct Ban { std::string id, name, reason; };
/* a name kept as one word in bans.txt: spaces and control bytes as '_', "-" when empty */
inline std::string BanNameField(const std::string& name)
{
    std::string s = Trim(name);
    for (size_t i = 0; i < s.size(); ++i) if (IsSpace(s[i]) || (unsigned char)s[i] < 32) s[i] = '_';
    return s.empty() ? std::string("-") : s;
}
/* one bans.txt line: false = not a ban line (a comment, a blank, or bad - see BansParse) */
inline bool BanLineParse(const std::string& line, Ban* b)
{
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#') return false;
    size_t p = 0; while (p < t.size() && !IsSpace(t[p])) ++p;
    const std::string id = t.substr(0, p);
    if (!IdOk(id)) return false;
    const std::string rest = Trim(t.substr(p));
    size_t q = 0; while (q < rest.size() && !IsSpace(rest[q])) ++q;
    b->id = id; b->name = rest.substr(0, q); b->reason = Trim(rest.substr(q));
    return true;
}
inline std::vector<Ban> BansParse(const std::string& text, int* bad)
{
    std::vector<Ban> out; int nb = 0;
    const std::vector<std::string> ls = Lines(text);
    for (size_t i = 0; i < ls.size(); ++i)
    {
        if (CommentOrBlank(ls[i])) continue;
        Ban b;
        if (BanLineParse(ls[i], &b)) out.push_back(b); else ++nb;
    }
    if (bad != 0) *bad = nb;
    return out;
}
inline int BanIndexOf(const std::vector<Ban>& bans, const std::string& id)
{
    for (size_t i = 0; i < bans.size(); ++i) if (!id.empty() && bans[i].id == id) return (int)i;
    return -1;
}
static const char kBansHeader[] = "# Banned players: <player id> <name> <reason>. One per line; delete a line (or /unban) to let them back in.";
/* bans.txt with a ban added; *changed = false (text unchanged) when that id is banned already or the id is not one */
inline std::string BansAdd(const std::string& text, const Ban& b, bool* changed)
{
    *changed = false;
    if (!IdOk(b.id) || BanIndexOf(BansParse(text, 0), b.id) >= 0) return text;
    std::vector<std::string> ls = Lines(text);
    if (ls.empty()) ls.push_back(kBansHeader);
    const std::string reason = CleanText(b.reason, kExtraMax);
    ls.push_back(b.id + " " + BanNameField(b.name) + (reason.empty() ? std::string() : " " + reason));
    *changed = true;
    return JoinLines(ls);
}
/* does this ban line answer to `who`: its player id, or its name (any ASCII case, spaces as '_') */
inline bool BanMatches(const Ban& b, const std::string& who)
{
    const std::string w = Trim(who);
    if (w.empty()) return false;
    if (IdOk(w)) return b.id == w;
    return LowerAscii(b.name) == LowerAscii(BanNameField(w));
}
/* bans.txt without every ban line answering to `who`; *removed = how many; the name of the first removed in *name. By NAME, when more
   than one line answers, nothing is removed (*removed 0, text unchanged) and *many = how many answered, so the admin names the one
   meant by its player id; *many is 0 otherwise. By id, every line with that id goes. */
inline std::string BansRemove(const std::string& text, const std::string& who, int* removed, std::string* name, int* many)
{
    *removed = 0; *many = 0; name->clear();
    std::vector<std::string> ls = Lines(text), out;
    int hits = 0;
    for (size_t i = 0; i < ls.size(); ++i)
    {
        Ban b;
        if (!CommentOrBlank(ls[i]) && BanLineParse(ls[i], &b) && BanMatches(b, who)) { if (hits == 0) *name = b.name; ++hits; continue; }
        out.push_back(ls[i]);
    }
    if (hits > 1 && !IdOk(Trim(who))) { *many = hits; name->clear(); return text; }
    *removed = hits;
    return hits == 0 ? text : JoinLines(out);
}

/* one piece of a list file being read whole, kept in *out: false = the text would pass `limit` bytes, so the file is too big to read
   and *out is emptied. A list is never used cut short: written back, it would lose every line past the cut. */
inline bool ReadPieceKeep(std::string* out, const char* buf, size_t got, size_t limit)
{
    if (got > limit || out->size() > limit - got) { out->clear(); return false; }
    out->append(buf, got);
    return true;
}

/* ---- the TEST-ONLY `admintest` lever (command_channel.cpp -> admin.cpp AdminTestCommand). Fixed made-up words only, so no free
   text reaches the chat or the log. Each form has exactly the words shown:
     setpw sample                              the admin password set to kTestPassword through the MULTIPLAYER window's DONE road
     makeadmin <slot> | removeadmin <slot>     the PLAYERS tab's MAKE ADMIN / REMOVE ADMIN road
     type login right | type login wrong | type logout | type kick <slot> | type ban <slot> | type ban nobody | type unban <slot>
       | type stop                             that line through the road a typed chat line starting with '/' takes (AdminTyped),
                                               the player's name looked up from the slot ---- */
static const char kTestPassword[] = "sample-admin";        /* 12 characters, made up: the test admin password (never logged) */
static const char kTestWrongPassword[] = "sample-wrong";   /* 12 characters, made up: not the test admin password */
static const char kTestNobody[] = "Nobody-Here";           /* a made-up name for "no player by that name" */
enum { kTestNone = 0, kTestSetPw = 1, kTestMakeAdmin = 2, kTestRemoveAdmin = 3, kTestType = 4 };
enum { kTypeNone = 0, kTypeLoginRight = 1, kTypeLoginWrong = 2, kTypeLogout = 3, kTypeKick = 4, kTypeBan = 5, kTypeBanNobody = 6,
       kTypeUnban = 7, kTypeStop = 8 };
struct TestAsk { int kind, type, slot; TestAsk() : kind(kTestNone), type(kTypeNone), slot(-1) {} };
inline std::vector<std::string> TestWords(const std::string& s)
{
    std::vector<std::string> w;
    size_t a = 0;
    while (a < s.size())
    {
        while (a < s.size() && IsSpace(s[a])) ++a;
        size_t b = a; while (b < s.size() && !IsSpace(s[b])) ++b;
        if (b > a) w.push_back(s.substr(a, b - a));
        a = b;
    }
    return w;
}
/* the lever's words: true with *out filled; false = not one of the forms above */
inline bool TestParse(const std::string& args, TestAsk* out)
{
    *out = TestAsk();
    const std::vector<std::string> w = TestWords(args);
    if (w.empty()) return false;
    int slot = -1;
    if (w[0] == "setpw") { if (w.size() != 2 || w[1] != "sample") return false; out->kind = kTestSetPw; return true; }
    if (w[0] == "makeadmin" || w[0] == "removeadmin")
    {
        if (w.size() != 2 || !SlotText(w[1], &slot)) return false;
        out->kind = w[0] == "makeadmin" ? kTestMakeAdmin : kTestRemoveAdmin; out->slot = slot;
        return true;
    }
    if (w[0] != "type" || w.size() < 2) return false;
    const std::string& v = w[1];
    int t = kTypeNone;
    if (v == "login" && w.size() == 3) t = w[2] == "right" ? kTypeLoginRight : (w[2] == "wrong" ? kTypeLoginWrong : kTypeNone);
    else if (v == "logout" && w.size() == 2) t = kTypeLogout;
    else if (v == "stop" && w.size() == 2) t = kTypeStop;
    else if (v == "ban" && w.size() == 3 && w[2] == "nobody") t = kTypeBanNobody;
    else if ((v == "kick" || v == "ban" || v == "unban") && w.size() == 3 && SlotText(w[2], &slot)) t = v == "kick" ? kTypeKick : (v == "ban" ? kTypeBan : kTypeUnban);
    if (t == kTypeNone) return false;
    out->kind = kTestType; out->type = t; out->slot = slot;
    return true;
}
/* a `type` form that names a player by slot */
inline bool TestNeedsName(const TestAsk& a) { return a.kind == kTestType && (a.type == kTypeKick || a.type == kTypeBan || a.type == kTypeUnban); }
/* the line the lever types for `a`; `name` = the slot's player name where TestNeedsName. "" = no line (a name needed and none, or one
   with a quote mark). A name with a space is quoted, as a player types it. */
inline std::string TestTypedLine(const TestAsk& a, const std::string& name)
{
    if (a.kind != kTestType) return std::string();
    std::string n = name;
    if (TestNeedsName(a))
    {
        if (n.empty() || n.find('"') != std::string::npos) return std::string();
        if (n.find(' ') != std::string::npos || n.find('\t') != std::string::npos) n = "\"" + n + "\"";
    }
    switch (a.type)
    {
    case kTypeLoginRight: return std::string("/login ") + kTestPassword;
    case kTypeLoginWrong: return std::string("/login ") + kTestWrongPassword;
    case kTypeLogout:     return "/logout";
    case kTypeKick:       return "/kick " + n;
    case kTypeBan:        return "/ban " + n;
    case kTypeBanNobody:  return std::string("/ban ") + kTestNobody;
    case kTypeUnban:      return "/unban " + n;
    case kTypeStop:       return "/stop";
    default:              return std::string();
    }
}

}   /* namespace admincmd */

#endif
