/* src/common/cmdforlog.h - a command line as the game log shows it (command_channel.cpp's `[cmd] new command:` and `[cmd] ignored
 * duplicate` lines). Header-only and pure, so the offline suite tests the plugin's own rule.
 *
 * What is hidden (everything else is shown whole):
 *   - hostpw / joinpw / steamjoin: they carry a password (steamjoin: a Steam id), so the verb alone is shown, then <hidden>;
 *   - `uitype <field> <text>` into a password box (coopui::UiDriveTypedHidden): `uitype <field> <hidden>`;
 *     (a nonce before the first bar stays for both: it names the write, not the password)
 *   - T-247: a chat line that carries a password. The mod's chat command parser (adminauth::SlashRoute) has exactly ONE such command:
 *     `/login <password>` (kRouteLogin). /logout and every other slash command (admincmd::VerbOfWord, admin.cpp AdminTyped) carry a
 *     name or nothing; the admin password is set from the MULTIPLAYER window's box, never typed in chat. Wherever a logged line holds a
 *     word that starts a line SlashRoute takes as the login - after the start, a space or a bar, behind any verb that types a line
 *     (admintest typeline ...) - or the bare word `login` right after `typeline` (admincmd::TestLineJoin puts the '/' before a first
 *     word that lacks one), that word stays and everything after it is shown as <hidden>.
 */
#pragma once
#include <string>
#include "uidrive.h"     /* coopui::UiDriveSplit, UiDriveTypedHidden */
#include "adminauth.h"   /* adminauth::SlashRoute, kRouteLogin, Space, Lower */

namespace cmdforlog {

inline std::string TrimWs(const std::string& s)
{
    const char* ws = " \t\r\n";
    const std::string::size_type b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}
inline bool IsPasswordVerb(const std::string& body)
{
    const std::string t = TrimWs(body);
    const std::string::size_type sp = t.find_first_of(" \t");
    const std::string verb = sp == std::string::npos ? t : t.substr(0, sp);
    return verb == "hostpw" || verb == "joinpw" || verb == "steamjoin";   /* steamjoin: its connect string carries a Steam id */
}
inline std::string VerbOnly(const std::string& body)
{
    const std::string t = TrimWs(body);
    const std::string::size_type sp = t.find_first_of(" \t");
    return (sp == std::string::npos ? t : t.substr(0, sp)) + " <hidden>";
}
inline bool IsHiddenType(const std::string& body, std::string* shown)
{
    std::string verb, args, field, text;
    coopui::UiDriveSplit(body, &verb, &args);
    if (verb != "uitype") return false;
    coopui::UiDriveSplit(args, &field, &text);
    if (coopui::UiDriveTypedHidden(field) == 0) return false;
    if (shown != 0) *shown = "uitype " + field + " <hidden>";
    return true;
}
/* T-247: true = the line holds a /login with something after it; *shown = the line up to and with the login word, then " <hidden>" */
inline bool LoginHide(const std::string& line, std::string* shown)
{
    std::string prev;
    std::string::size_type a = 0;
    while (a < line.size())
    {
        while (a < line.size() && (adminauth::Space(line[a]) || line[a] == '|')) ++a;
        if (a >= line.size()) break;
        std::string::size_type b = a;
        while (b < line.size() && !adminauth::Space(line[b]) && line[b] != '|') ++b;
        std::string typed = line.substr(a);
        if (typed[0] != '/' && adminauth::Lower(prev) == "typeline") typed = "/" + typed;   /* TestLineJoin's '/' */
        if (adminauth::SlashRoute(typed) == adminauth::kRouteLogin)
        {
            std::string::size_type c = b;
            while (c < line.size() && adminauth::Space(line[c])) ++c;
            if (c >= line.size()) return false;   /* a bare /login: nothing to hide */
            if (shown != 0) *shown = line.substr(0, b) + " <hidden>";
            return true;
        }
        prev = line.substr(a, b - a);
        a = b;
    }
    return false;
}
inline std::string CmdForLog(const std::string& line)
{
    std::string shown;
    if (IsPasswordVerb(line)) return VerbOnly(line);   /* the verb first: a password may itself hold a bar */
    if (IsHiddenType(line, &shown)) return shown;
    const std::string::size_type bar = line.find('|');   /* a nonce before the first bar, then the verb */
    if (bar != std::string::npos && IsPasswordVerb(line.substr(bar + 1))) return line.substr(0, bar + 1) + VerbOnly(line.substr(bar + 1));
    if (bar != std::string::npos && IsHiddenType(line.substr(bar + 1), &shown)) return line.substr(0, bar + 1) + shown;
    if (LoginHide(line, &shown)) return shown;   /* T-247: admintest typeline /login <pw>, with or without a nonce */
    return line;
}

}   /* namespace cmdforlog */
