/* src/common/cfgtext.cpp - see cfgtext.h.  PURE: no Windows.h, no Debug.h, no globals, no I/O. */
#include "cfgtext.h"
#include "names.h"   /* the on-disk names (mod folder, files, window texts) */
#include "worlddir.h"    /* W1w: coopworld::DisplayNameOk - the playername key's one rule */
#include "areaclaim.h"   /* B13: coopstore::PlayerIdOk - the notebook refuses a malformed id too, so the shape rule lives in ONE place */

#include <cstdlib>

namespace coopcfg {

CfgFields::CfgFields()
    : role(kCfgSingle), hostPort(0), storePort(0), slot("s1"), modSalt(0), profileTest(0), ownSave(1), joinViaWorld(0), routerPort(1)
{
    /* B13: playerId is deliberately EMPTY by default and never invented here. This is a text layer; the
       thing that generates an id is config.cpp, once, and it writes the file back when it does. */
}

std::string CfgTrim(const std::string& s)
{
    std::string::size_type a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string CfgLower(const std::string& s)
{
    std::string o(s);
    for (std::string::size_type i = 0; i < o.size(); ++i)
        if (o[i] >= 'A' && o[i] <= 'Z') o[i] = (char)(o[i] - 'A' + 'a');
    return o;
}

bool CfgPortTextOk(const std::string& p, unsigned short* port)
{
    if (p.empty() || p.size() > kCfgPortFieldMax) return false;
    for (std::string::size_type i = 0; i < p.size(); ++i)
        if (p[i] < '0' || p[i] > '9') return false;
    const long n = ::atol(p.c_str());
    if (n <= 0 || n > 65535) return false;
    if (port != 0) *port = (unsigned short)n;
    return true;
}

bool CfgSplitAddr(const std::string& v, std::string* addr, unsigned short* port)
{
    const std::string::size_type c = v.find_last_of(':');
    if (c == std::string::npos || c == 0 || c + 1 >= v.size()) return false;
    unsigned short p = 0;
    if (!CfgPortTextOk(v.substr(c + 1), &p)) return false;
    if (addr != 0) *addr = v.substr(0, c);
    if (port != 0) *port = p;
    return true;
}

bool CfgHostTextPlausible(const std::string& a)
{
    if (a.empty() || a.size() > kCfgAddrFieldMax) return false;
    std::string::size_type i;
    int nonDot = 0;
    for (i = 0; i < a.size(); ++i)
    {
        const char ch = a[i];
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                     || ch == '.' || ch == '-' || ch == ':' || ch == '[' || ch == ']';
        if (!ok) return false;
        if (ch != '.') ++nonDot;
        if (ch == '.' && i + 1 < a.size() && a[i + 1] == '.') return false;
    }
    if (nonDot == 0) return false;
    if (a[0] == '.' || a[0] == '-') return false;
    if (a[a.size() - 1] == '.' || a[a.size() - 1] == '-') return false;
    return true;
}

std::string CfgSanitiseTyped(const std::string& raw, size_t maxLen)
{
    std::string o;
    for (std::string::size_type i = 0; i < raw.size() && o.size() < maxLen; ++i)
    {
        const unsigned char ch = (unsigned char)raw[i];
        if (ch < 0x20 || ch == 0x7F) continue;
        o.push_back((char)ch);
    }
    return o;
}

namespace {

/* RE_Kenshi imbues a process-wide locale that inserts digit-group separators into every number
   (F030), so this file formats its own integers and never touches a stream. */
std::string NumText(unsigned long v)
{
    char tmp[16];
    char out[16];
    int n = 0;
    if (v == 0) return std::string("0");
    while (v != 0 && n < 15) { tmp[n++] = (char)('0' + (int)(v % 10ul)); v /= 10ul; }
    int i = 0;
    while (n > 0) out[i++] = tmp[--n];
    out[i] = 0;
    return std::string(out);
}

const char* RoleText(int r)
{
    return r == kCfgHost ? "host" : (r == kCfgClient ? "client" : "single");
}

}   /* anonymous namespace */

bool CfgPortFieldOk(const std::string& text, unsigned short* port, std::string* why)
{
    const std::string t = CfgTrim(text);
    if (t.empty())
    {
        if (why != 0) *why = "Enter a port (1-65535). The default is 7777.";
        return false;
    }
    if (!CfgPortTextOk(t, port))
    {
        if (why != 0) *why = "\"" + t + "\" isn't a valid port. Use a number from 1 to 65535.";
        return false;
    }
    return true;
}

bool CfgAddrFieldOk(const std::string& text, const char* what,
                    std::string* addr, unsigned short* port, std::string* why)
{
    const std::string label = (what != 0 ? std::string(what) : std::string("address"));
    const std::string t = CfgTrim(text);
    if (t.empty())
    {
        if (why != 0) *why = "Enter the host's address, for example 192.168.1.20:7777.";
        return false;
    }
    std::string a;
    unsigned short p = 0;
    if (!CfgSplitAddr(t, &a, &p))
    {
        if (why != 0) *why = "\"" + t + "\" isn't a valid " + label + ". Use the form 192.168.1.20:7777.";
        return false;
    }
    if (!CfgHostTextPlausible(a))
    {
        if (why != 0) *why = "\"" + a + "\" isn't a valid IP address or host name.";
        return false;
    }
    if (a == "0.0.0.0")
    {
        if (why != 0) *why = "0.0.0.0 can't be joined. Enter the host's IP address.";   /* it is what a host listens on */
        return false;
    }
    if (addr != 0) *addr = a;
    if (port != 0) *port = p;
    return true;
}

bool CfgNameFieldOk(const std::string& text, const char* what, size_t maxLen, std::string* why)
{
    const std::string label = (what != 0 ? std::string(what) : std::string("name"));
    const std::string t = CfgTrim(text);
    if (t.empty())
    {
        if (why != 0) *why = "Enter a " + label + ".";
        return false;
    }
    if (t.size() > maxLen)
    {
        if (why != 0) *why = "The " + label + " is too long. Keep it to " + NumText((unsigned long)maxLen) + " characters or fewer.";
        return false;
    }
    for (std::string::size_type i = 0; i < t.size(); ++i)
    {
        const char ch = t[i];
        const bool bad = (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"'
                       || ch == '<'  || ch == '>' || ch == '|' || ch == '=' || ch == '#' || ch == ';'
                       || (unsigned char)ch < 0x20);
        if (bad)
        {
            if (why != 0) *why = "The " + label + " can't contain '" + std::string(1, ch) + "'. Use letters, numbers, spaces, - and _ only.";
            return false;
        }
        /* owner decision 249 a: a space is allowed inside the name, never two in a row (the ends were trimmed off above) */
        if (ch == ' ' && i + 1 < t.size() && t[i + 1] == ' ')
        {
            if (why != 0) *why = "Use letters, numbers, spaces, - and _ only.";
            return false;
        }
    }
    return true;
}

/* P8J-B2 - see cfgtext.h. */
static bool CfgIsSep(char c) { return c == '\\' || c == '/'; }
bool CfgRootPathOk(const std::string& text, std::string* norm, std::string* why)
{
    std::string v = CfgTrim(text);
    if (v.empty()) { if (why != 0) *why = "it is empty"; return false; }
    if (v.size() > kCfgRootFieldMax) { if (why != 0) *why = "it is longer than " + NumText((unsigned long)kCfgRootFieldMax) + " characters"; return false; }
    for (size_t i = 0; i < v.size(); ++i)
    {
        const unsigned char c = (unsigned char)v[i];
        if (c < 0x20 || c == 0x7F) { if (why != 0) *why = "it contains a control character"; return false; }
        if (c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*' || c == '#' || c == ';')
        {
            if (why != 0) *why = "it contains '" + std::string(1, (char)c) + "', which a folder path here may not";
            return false;
        }
    }
    const bool drive = v.size() >= 3 && ((v[0] >= 'A' && v[0] <= 'Z') || (v[0] >= 'a' && v[0] <= 'z'))
                       && v[1] == ':' && CfgIsSep(v[2]);
    const bool unc   = v.size() >= 3 && CfgIsSep(v[0]) && CfgIsSep(v[1]) && !CfgIsSep(v[2]);
    if (!drive && !unc)
    {
        if (why != 0) *why = "it is not an absolute folder path (start it with a drive, as in C:\\folder, or with \\\\server\\share)";
        return false;
    }
    const std::string::size_type colon = v.find(':', drive ? 2 : 0);
    if (colon != std::string::npos) { if (why != 0) *why = "it has a ':' after the drive letter"; return false; }
    while (!v.empty() && CfgIsSep(v[v.size() - 1])) v.erase(v.size() - 1);
    if (drive && v.size() <= 2) { if (why != 0) *why = "a whole drive is not a folder the mod may use"; return false; }
    if (norm != 0) *norm = v;
    return true;
}

void CfgParseText(const std::string& text, CfgFields* out, std::vector<CfgNote>* notes,
                  int* linesOut, long long* unknownKeys, long long* badLines)
{
    CfgFields f;
    int lines = 0;
    long long unknown = 0, bad = 0;

    std::string::size_type pos = 0;
    while (lines < kCfgMaxLines && pos < text.size())
    {
        std::string line;
        const std::string::size_type nl = text.find('\n', pos);
        if (nl == std::string::npos) { line = text.substr(pos); pos = text.size(); }
        else                         { line = text.substr(pos, nl - pos); pos = nl + 1; }
        ++lines;

        if (line.size() > kCfgMaxLineChars) line.erase(kCfgMaxLineChars);
        const std::string::size_type h = line.find_first_of("#;");
        if (h != std::string::npos) line.erase(h);
        line = CfgTrim(line);
        if (line.empty()) continue;

        const std::string::size_type eq = line.find('=');
        if (eq == std::string::npos)
        {
            ++bad;
            if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "no '=' - ignored"; notes->push_back(nt); }
            continue;
        }
        const std::string key = CfgLower(CfgTrim(line.substr(0, eq)));
        const std::string val = CfgTrim(line.substr(eq + 1));

        if (key == "role")
        {
            const std::string v = CfgLower(val);
            if      (v == "single") f.role = kCfgSingle;
            else if (v == "host")   f.role = kCfgHost;
            else if (v == "client") f.role = kCfgClient;
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "role='" + val + "' is not single|host|client - ignored, role stays single"; notes->push_back(nt); }
            }
        }
        else if (key == "host")
        {
            if (!CfgSplitAddr(val, &f.hostAddr, &f.hostPort))
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "host='" + val + "' is not <address>:<port> - ignored"; notes->push_back(nt); }
            }
        }
        else if (key == "store")
        {
            if (!CfgSplitAddr(val, &f.storeAddr, &f.storePort))
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "store='" + val + "' is not <address>:<port> - ignored"; notes->push_back(nt); }
            }
        }
        else if (key == "slot")  { if (!val.empty()) f.slot = val; }
        else if (key == "world") { f.world = val; }
        /* B13: the stable name of this install. A malformed one is a BAD LINE and is dropped rather than
           carried: the plugin then generates a fresh id and writes the file back, which is the right answer
           to a hand-edited or half-written id. Carrying it would put a name the notebook refuses on the wire
           and leave this game unable to link at all. */
        else if (key == "playerid")
        {
            const std::string v = CfgLower(val);
            if (coopstore::PlayerIdOk(v)) f.playerId = v;
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "playerid='" + val + "' is not 32 hexadecimal characters - ignored, and a new one is generated"; notes->push_back(nt); }
            }
        }
        /* W1w (decision 59(d)): the player's display name. A malformed one is a bad line and is dropped - the box
           on the Multiplayer screen asks again - rather than carried onto the wire and into every world's list. */
        else if (key == "playername")
        {
            if (coopworld::DisplayNameOk(val, 0)) f.playerName = val;
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "playername='" + val + "' is not 1-24 plain characters - ignored"; notes->push_back(nt); }
            }
        }
        /* P8J-B2 (bench 2, F738): the two folder overrides. A refused value is a BAD LINE and the field stays
           empty, which is the default folder - a mistyped override must never move a save somewhere unasked. */
        else if (key == "profile")   /* prof1: TEST ONLY - see CfgFields::profileTest */
        {
            const std::string v = CfgTrim(val);
            int n = 0; bool ok = !v.empty() && v.size() <= 4;
            for (size_t i = 0; ok && i < v.size(); ++i) { if (v[i] < '0' || v[i] > '9') ok = false; else n = n * 10 + (v[i] - '0'); }
            if (ok) f.profileTest = n;
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "profile='" + val + "' is not a number 0-9999 - ignored"; notes->push_back(nt); }
            }
        }
        else if (key == "modfingerprint_salt")   /* settings5 S5: TEST ONLY; the fold reads false/off/no as off too */
        { const std::string lv = CfgLower(CfgTrim(val)); f.modSalt = (lv.empty() || lv == "0" || lv == "false" || lv == "off" || lv == "no") ? 0 : 1; }
        else if (key == "joinvia")   /* M11a S1: TEST ONLY - world = the world road; anything else is the session road */
        { const std::string lv = CfgLower(CfgTrim(val)); f.joinViaWorld = (lv == "world") ? 1 : 0; }
        else if (key == "ownsave")   /* mmo1: the own-squad record writer's lever; anything but on/1/true/yes is off */
        { const std::string lv = CfgLower(CfgTrim(val)); f.ownSave = (lv == "1" || lv == "on" || lv == "true" || lv == "yes") ? 1 : 0; }
        else if (key == "routerport")   /* see CfgFields::routerPort; a value it cannot read leaves the router asked */
        {
            const std::string lv = CfgLower(CfgTrim(val));
            if      (lv == "0" || lv == "off" || lv == "false" || lv == "no") f.routerPort = 0;
            else if (lv == "1" || lv == "on"  || lv == "true"  || lv == "yes") f.routerPort = 1;
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = "routerport='" + val + "' is not 0|1 - ignored, the router is asked"; notes->push_back(nt); }
            }
        }
        else if (key == "saveroot" || key == "storedir" || key == "datadir")   /* PP3: datadir= */
        {
            std::string normed, why;
            if (CfgRootPathOk(val, &normed, &why)) { if (key == "saveroot") f.saveRoot = normed; else if (key == "storedir") f.storeDir = normed; else f.dataDir = normed; }
            else
            {
                ++bad;
                if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 1; nt.text = key + "='" + val + "' is refused: " + why + " - ignored, the default folder is used"; notes->push_back(nt); }
            }
        }
        else
        {
            ++unknown;
            if (notes != 0) { CfgNote nt; nt.line = lines; nt.bad = 0; nt.text = "unknown key '" + key + "' - ignored (this build knows role, host, store, slot, world, playername, playerid, saveroot, storedir, datadir)"; notes->push_back(nt); }
        }
    }

    if (out != 0)         *out = f;
    if (linesOut != 0)    *linesOut = lines;
    if (unknownKeys != 0) *unknownKeys = unknown;
    if (badLines != 0)    *badLines = bad;
}

std::string CfgFormat(const CfgFields& f, std::string* why)
{
    if (why != 0) why->clear();
    std::string o;
    o += SW_TEST_CFG_HEADER " the in-game MULTIPLAYER panel (E43 / U1).\r\n";
    o += "# role=single means no link of any kind and plain-Kenshi behaviour; delete this file for the same effect.\r\n";
    o += "role=";
    o += RoleText(f.role);
    o += "\r\n";
    if (f.hostPort != 0)
    {
        o += "host=";
        o += (f.hostAddr.empty() ? std::string("0.0.0.0") : f.hostAddr);
        o += ":";
        o += NumText((unsigned long)f.hostPort);
        o += "\r\n";
    }
    if (f.storePort != 0)
    {
        o += "store=";
        o += (f.storeAddr.empty() ? std::string("127.0.0.1") : f.storeAddr);
        o += ":";
        o += NumText((unsigned long)f.storePort);
        o += "\r\n";
    }
    if (!f.slot.empty())  { o += "slot=";  o += f.slot;  o += "\r\n"; }
    if (!f.world.empty()) { o += "world="; o += f.world; o += "\r\n"; }
    /* P8J-B2: written only when set AND only in a form the reader takes back unchanged; absent = no line, as before. */
    if (!f.saveRoot.empty() && CfgRootPathOk(f.saveRoot, 0, 0)) { o += "saveroot="; o += f.saveRoot; o += "\r\n"; }
    if (!f.storeDir.empty() && CfgRootPathOk(f.storeDir, 0, 0)) { o += "storedir="; o += f.storeDir; o += "\r\n"; }
    if (!f.playerName.empty())   /* W1w: decision 59(d) */
    {
        /* W1w-b: never write a name the reader would cut, drop or misread (a ';' or '#' starts a comment there). */
        std::string bad;
        if (coopworld::DisplayNameOk(f.playerName, &bad)) { o += "playername="; o += f.playerName; o += "\r\n"; }
        else if (why != 0) *why = "Your name was not saved. " + bad;
    }
    /* B13: LAST, and with the line that says nobody should touch it. Written whenever it is non-empty, so
       every writer of this file - the panel included - preserves the name this install already has. */
    if (!f.playerId.empty())
    {
        o += "# playerid is this install's stable name in the notebook: do not edit it and do not copy it to\r\n";
        o += "# another machine - the notebook keys slot numbers, areas and the operator on it (B13).\r\n";
        o += "playerid=";
        o += f.playerId;
        o += "\r\n";
    }
    return o;
}

int CfgIncompleteReason(const CfgFields& f)
{
    if (f.role == kCfgSingle) return 0;
    if (f.hostPort == 0) return 1;
    if (f.role == kCfgClient && f.hostAddr.empty()) return 2;
    return 0;
}

}   /* namespace coopcfg */
