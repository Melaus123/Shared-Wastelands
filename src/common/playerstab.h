/* src/common/playerstab.h - THE PLAYERS TAB: one row per other player of this world, and this player's stance towards each.
 *
 * The tab sits in Kenshi's own management window, after AI (MAP FACTION RESEARCH CRAFTING SQUADS DIALOGUE AI PLAYERS). Its
 * table has the columns PLAYER / FACTION / YOU / THEM / STATUS; the PLAYER text is drawn in that player's name-tag
 * colour (nametag.h: the worse of the two directions). Under the table, with a row selected: "YOUR STANCE TOWARDS <FACTION>"
 * and three tick buttons ALLY / NEUTRAL / HOSTILE, exactly one ticked (this player's own side); with none selected the line
 * reads "Select a player." and the buttons are hidden. HOSTILE asks first (SET HOSTILE, CANCEL left, HOSTILE right).
 * Each player sets only their own side; the other game shows the engine's own sentences (FactionRelations 0x6B25A0's four,
 * in its order) when that side changes level by the one rule the table and the name tag use (nametag::LevelOf), once per
 * change of level however many roads bring it (NoticeBook).
 *
 * THE TAB'S NUMBER. Kenshi's code names its tabs by position (0 MAP .. 6 AI): the tab-change handler switches on it, the main
 * bar opens a tab by it, and the game's other reads compare the selected position against fixed numbers. PLAYERS is
 * appended at position 7, so the game's seven keep their numbers and none of the game's switches or comparisons names 7.
 *
 * Pure: no engine memory, no MyGUI, no Windows; the offline suite runs the same code. C++03 (VS2010 v100).
 */
#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <map>
#include "nametag.h"

namespace playerstab {

const char* const kTabCaption   = "PLAYERS";
const int kEngineTabCount       = 7;   /* MAP FACTION RESEARCH CRAFTING SQUADS DIALOGUE AI */
const int kTabIndex             = kEngineTabCount;   /* after AI: the game's seven keep their positions */
const int kFactionTab           = 1;   /* the game's FACTION tab */
/* the game's own seven tabs, by the position its code names them with */
inline const char* EngineTabCaption(int i)
{
    static const char* const t[kEngineTabCount] = { "MAP", "FACTION", "RESEARCH", "CRAFTING", "SQUADS", "DIALOGUE", "AI" };
    return (i >= 0 && i < kEngineTabCount) ? t[i] : "";
}
/* the positions the game's code compares the selected tab with or reads a tab at, besides its 0..6 switch (playerstab.cpp's
   header: 0x49AAC0 ==0, 0x49AC50 ==3 / ==5, 0x499A30 / 0x495370 / 0x494060 ==2, 0x4996A0 ==5, 0x971B80 ==3, 0x98F390 !=2 / !=3,
   getItemAt(3) 0x48B120) */
const int kEngineTabNumbersUsed = 4;
inline int EngineTabNumberUsed(int k)
{
    static const int n[kEngineTabNumbersUsed] = { 0, 2, 3, 5 };
    return (k >= 0 && k < kEngineTabNumbersUsed) ? n[k] : -1;
}

/* the column heads, left to right, and each column's share of the table's inner width (percent; the last takes the rest) */
const int kColumns = 5;
inline const char* ColumnHead(int c)
{
    static const char* const h[kColumns] = { "PLAYER", "FACTION", "YOU", "THEM", "STATUS" };
    return (c >= 0 && c < kColumns) ? h[c] : "";
}
inline int ColumnPercent(int c)
{
    static const int p[kColumns] = { 24, 32, 14, 14, 16 };
    return (c >= 0 && c < kColumns) ? p[c] : 0;
}
/* the column widths for an inner width w (none under 0: a width of 0 or less gives five 0 columns): the shares, the last column
   taking whatever the others leave */
inline void ColumnWidths(int w, int* out)
{
    if (w < 0) w = 0;
    int used = 0;
    for (int c = 0; c < kColumns - 1; ++c) { out[c] = (w * ColumnPercent(c)) / 100; used += out[c]; }
    out[kColumns - 1] = w - used > 0 ? w - used : 0;
}

/* a stance level (nametag levels) as the table shows it */
inline std::string LevelWord(int level)
{
    if (level == nametag::kFriendly) return "Ally";
    if (level == nametag::kHostile) return "Hostile";
    return "Neutral";
}
inline std::string StatusWord(bool online) { return online ? "Online" : "Offline"; }

/* The PLAYER cell's words: the name the world server's roster gives that player now, else the name it gave earlier this
   session, else the placeholder the mod already uses for a player not seen yet ("Player <n>", slotwire.h). */
inline std::string PlayerWords(const std::string& rosterName, const std::string& rememberedName, const std::string& placeholder)
{
    const std::string a = nametag::Trimmed(rosterName);
    if (!a.empty()) return a;
    const std::string b = nametag::Trimmed(rememberedName);
    if (!b.empty()) return b;
    return placeholder;
}

/* MyGUI reads "#rrggbb" in a caption as a colour change and "##" as one '#': a name is escaped, then coloured. */
inline std::string EscapeHash(const std::string& s)
{
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) { o += s[i]; if (s[i] == '#') o += '#'; }
    return o;
}
inline std::string ColourCode(int level)
{
    const nametag::Rgb c = nametag::LevelColour(level);
    const int v[3] = { (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f), (int)(c.b * 255.0f + 0.5f) };
    static const char* const hex = "0123456789abcdef";
    std::string o("#");
    for (int k = 0; k < 3; ++k) { o += hex[(v[k] >> 4) & 15]; o += hex[v[k] & 15]; }
    return o;
}
inline std::string ColouredName(const std::string& name, int level) { return ColourCode(level) + EscapeHash(name); }
/* the team blue (decision 479) as a caption colour code */
inline std::string TeamColourCode()
{
    const nametag::Rgb c = nametag::TeamColour();
    const int v[3] = { (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f), (int)(c.b * 255.0f + 0.5f) };
    static const char* const hex = "0123456789abcdef";
    std::string o("#");
    for (int k = 0; k < 3; ++k) { o += hex[(v[k] >> 4) & 15]; o += hex[v[k] & 15]; }
    return o;
}

/* One row of the table, everything it shows, built from what the plugin read. */
struct RowIn
{
    int slot;
    std::string rosterName, rememberedName, placeholder, faction;
    int you, them;     /* nametag levels of each direction; kUnknown when unread */
    bool online;
    bool teammate;     /* T-546 step 4: that player shares this player's faction (a team) - this side is held at ally */
    bool founderSets;  /* T-546 step 5 (476): this player is a member and that player is outside its team - the founder sets the stance */
    bool mateFounder;  /* a teammate who founded the faction (the YOU / THEM cells read Founder, else Member) */
    bool inTeam;       /* that player is in a faction of players that is not this player's */
    RowIn() : slot(-1), you(nametag::kUnknown), them(nametag::kUnknown), online(false), teammate(false), founderSets(false), mateFounder(false), inTeam(false) {}
};
struct RowView
{
    int slot;
    std::string cell[kColumns];   /* PLAYER is coloured (ColouredName), the others plain */
    std::string faction;          /* the faction name as shown, for the stance line, the box and the message lines */
    int you, tag;                 /* this player's own level towards them (the ticked button), and the tag's level */
    bool teammate;                /* T-546 step 4: a teammate - ALLY ticked, NEUTRAL and HOSTILE disabled */
    bool founderSets;             /* T-546 step 5 (476): the founder's stance - the founder's value ticked, every button disabled */
    bool online, inTeam;          /* the faction screens' bottom line (teamscreens.h): INVITE TO FACTION only to an online player in no faction of players */
    std::string name;             /* the player's name as this game knows it, for the faction screens' words ("" = not known) */
    RowView() : slot(-1), you(nametag::kNeutral), tag(nametag::kNeutral), teammate(false), founderSets(false), online(false), inTeam(false) {}
};
inline RowView RowFor(const RowIn& in)
{
    RowView r;
    r.slot = in.slot;
    r.teammate = in.teammate;
    r.founderSets = in.founderSets && !in.teammate;
    r.faction = nametag::Trimmed(in.faction);
    r.tag = nametag::WorseLevel(in.you, in.them);
    r.you = (in.you >= nametag::kFriendly && in.you <= nametag::kHostile) ? in.you : nametag::kNeutral;
    const int them = (in.them >= nametag::kFriendly && in.them <= nametag::kHostile) ? in.them : nametag::kNeutral;
    r.online = in.online;
    r.inTeam = in.inTeam && !in.teammate;
    r.name = PlayerWords(in.rosterName, in.rememberedName, std::string());
    const std::string shown = PlayerWords(in.rosterName, in.rememberedName, in.placeholder);
    r.cell[0] = in.teammate ? TeamColourCode() + EscapeHash(shown) : ColouredName(shown, r.tag);   /* a teammate in team blue, as its name tag */
    r.cell[1] = EscapeHash(r.faction);
    r.cell[2] = in.teammate ? std::string(in.mateFounder ? "Founder" : "Member") : LevelWord(r.you);   /* a teammate: that player's place in the faction */
    r.cell[3] = in.teammate ? std::string(in.mateFounder ? "Founder" : "Member") : LevelWord(them);
    r.cell[4] = StatusWord(in.online);
    return r;
}
/* the table's rows in slot order (a stable order between refreshes) */
inline void SortBySlot(std::vector<RowView>* rows)
{
    for (std::size_t i = 1; i < rows->size(); ++i)
        for (std::size_t j = i; j > 0 && (*rows)[j - 1].slot > (*rows)[j].slot; --j) { RowView t = (*rows)[j - 1]; (*rows)[j - 1] = (*rows)[j]; (*rows)[j] = t; }
}
/* the same players in the same order: the table is updated cell by cell; otherwise it is rebuilt */
inline bool SameSlots(const std::vector<RowView>& a, const std::vector<RowView>& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].slot != b[i].slot) return false;
    return true;
}
inline int RowOfSlot(const std::vector<RowView>& rows, int slot)
{
    for (std::size_t i = 0; i < rows.size(); ++i) if (rows[i].slot == slot) return (int)i;
    return -1;
}

/* the line under the table and whether the buttons show */
const char* const kSelectLine = "Select a player.";
inline std::string Upper(const std::string& s)
{
    std::string o(s);
    for (std::size_t i = 0; i < o.size(); ++i) if (o[i] >= 'a' && o[i] <= 'z') o[i] = (char)(o[i] - 'a' + 'A');
    return o;
}
/* every place a faction name is shown goes through MyGUI's caption reader: the name is escaped (EscapeHash) */
inline std::string StanceLine(const std::string& faction) { return "YOUR STANCE TOWARDS " + Upper(EscapeHash(nametag::Trimmed(faction))); }
/* ticked: 0 ALLY, 1 NEUTRAL, 2 HOSTILE; -1 none. locked: the selected player is a teammate (T-546 step 4) - ALLY is shown ticked
   and NEUTRAL / HOSTILE are disabled, because the pin holds this side at ally while both share the faction */
struct Bottom { bool selected; std::string line; int ticked; bool locked; bool founders; };   /* founders: T-546 step 5 - every button disabled, this side (the founder's value) ticked */
inline Bottom BottomFor(const std::vector<RowView>& rows, int selectedSlot)
{
    Bottom b; b.selected = false; b.line = kSelectLine; b.ticked = -1; b.locked = false; b.founders = false;
    const int i = selectedSlot < 0 ? -1 : RowOfSlot(rows, selectedSlot);
    if (i < 0) return b;
    b.selected = true;
    b.line = StanceLine(rows[(std::size_t)i].faction);
    b.locked = rows[(std::size_t)i].teammate;
    b.founders = rows[(std::size_t)i].founderSets;
    b.ticked = b.locked ? 0 : rows[(std::size_t)i].you;   /* nametag levels 0 friendly, 1 neutral, 2 hostile = the buttons' order */
    return b;
}
/* button k can be pressed: every button, except NEUTRAL and HOSTILE towards a teammate, and none where the founder sets the stance */
inline bool ButtonEnabled(const Bottom& b, int k) { return !b.founders && (!b.locked || k == 0); }
/* the buttons' disabled look as one number: 0 all open, 1 a teammate's row, 2 the founder's stance */
inline int LockLook(const Bottom& b) { return b.founders ? 2 : b.locked ? 1 : 0; }

/* the three buttons, in their order, and the level each sets */
inline const char* ButtonCaption(int k)
{
    static const char* const c[3] = { "ALLY", "NEUTRAL", "HOSTILE" };
    return (k >= 0 && k < 3) ? c[k] : "";
}
inline const char* LevelVerb(int k)   /* the relate verb's word for a button (relations.cpp RelateSet) */
{
    static const char* const v[3] = { "ally", "neutral", "hostile" };
    return (k >= 0 && k < 3) ? v[k] : "";
}
/* What a press does: nothing when that button is already ticked, the confirmation box for HOSTILE, else the change. */
enum Press { kPressNothing = 0, kPressConfirm = 1, kPressSet = 2 };
inline int PressFor(int button, int ticked)
{
    if (button < 0 || button > 2 || button == ticked) return kPressNothing;
    return button == 2 ? kPressConfirm : kPressSet;
}
/* why a press did nothing (the [PLAYERS] log line) */
inline const char* NothingWhy(int button, bool teammate, bool founderSets = false)
{
    if (founderSets) return "nothing (this player is a member of a faction: the founder sets its stance towards this player)";
    return (teammate && button != 0) ? "nothing (a teammate: this side is held at ally)" : "nothing (already ticked)";
}
/* a press towards a row: towards a teammate only ALLY does anything (it sets ally when the side is not there yet) */
inline int PressForRow(int button, int you, bool teammate, bool founderSets = false)
{
    if (founderSets) return kPressNothing;
    if (teammate && button != 0) return kPressNothing;
    return PressFor(button, you);
}

/* THE HOSTILE BOX (A4.3): its title, its words, its buttons (back left, action right) */
const char* const kBoxTitle   = "SET HOSTILE";
const char* const kBoxCancel  = "CANCEL";
const char* const kBoxConfirm = "HOSTILE";
inline std::string BoxText(const std::string& faction)
{
    return "Become hostile towards " + EscapeHash(nametag::Trimmed(faction)) + "? Your people will attack theirs on sight, and theirs will fight back.";
}

/* THIS player's line on Kenshi's message line after a change */
inline std::string MyLine(int button, const std::string& faction)
{
    const std::string f = EscapeHash(nametag::Trimmed(faction));
    if (button == 0) return "You are now allies of " + f + ".";
    if (button == 2) return "You are now hostile towards " + f + ".";
    return "You are now neutral towards " + f + ".";
}

/* A NEUTRAL or HOSTILE choice clears the ally flag this game holds on its own side: the flag alone answers ally (the engine's
   ally test 0x6B22E0 reads it first), so left set it would keep the row, the ticks and the other game's sentence at ally. */
inline bool ClearsAllyFlag(int button, bool flagSet) { return flagSet && (button == 1 || button == 2); }

/* THE OTHER GAME'S LINES: the engine's own four sentences (FactionRelations 0x6B25A0, Read), in its order, for a move of THEIR
   side towards this player from `before` to `after` (relation and ally flag). Both ends are judged by the one rule the table's
   THEM column and the name tag use (nametag::LevelOf: hostile at -30 or less, else ally at 50 or more or by the flag), so a
   sentence is shown exactly when that column changes level. {1} is their faction's name, escaped as every shown name is. */
inline std::string Fill(const char* text, const std::string& faction)
{
    std::string s(text);
    const std::string key("{1}");
    const std::size_t p = s.find(key);
    if (p != std::string::npos) s.replace(p, key.size(), EscapeHash(nametag::Trimmed(faction)));
    return s;
}
inline std::vector<std::string> TheirNotices(float before, bool beforeFlag, float after, bool afterFlag, const std::string& faction)
{
    std::vector<std::string> out;
    const int b = nametag::LevelOf(before, beforeFlag), a = nametag::LevelOf(after, afterFlag);
    if (b != nametag::kHostile && a == nametag::kHostile)   out.push_back(Fill("{1} are now hostile towards you", faction));
    if (b == nametag::kHostile && a != nametag::kHostile)   out.push_back(Fill("{1} are no longer hostile towards you", faction));
    if (b == nametag::kFriendly && a != nametag::kFriendly) out.push_back(Fill("{1} are no longer your ally", faction));
    if (b != nametag::kFriendly && a == nametag::kFriendly) out.push_back(Fill("{1} are now your allies", faction));
    return out;
}
/* The engine's own notice tests (0x6B25A0 on the relation value alone): whether it printed any sentence for a move from `from`
   to `to` - its changers affectRelations 0x6B2B50 / 0x6B29D0, setNoLongerEnemies 0x6B2C40, declareWar 0x6B2CB0 and setEnemy
   0x6B2E40 call it whenever the other faction is a player faction (Faction+0x250); setRelation 0x6B4A30 does not. */
const float kNoticeHostileAt = -30.0f;   /* .rdata 0x16CBCFC */
const float kNoticeAllyAt    = 50.0f;    /* .rdata 0x1682170 */
inline bool EngineNoticeCrossed(float from, float to)
{
    return (kNoticeHostileAt < from && to <= kNoticeHostileAt) || (from <= kNoticeHostileAt && kNoticeHostileAt < to)
        || (kNoticeAllyAt < from && to <= kNoticeAllyAt) || (from <= kNoticeAllyAt && kNoticeAllyAt < to);
}
/* WHAT THE PLAYER HAS BEEN TOLD about another player's side towards them, per pair key: the standing this game shows, as last
   counted. A live change is announced from it (else from the value this game held before the write), so one change of level
   gives its lines once however many roads bring it. A snapshot (Quiet), a crossing this game's own engine announced (EngineSaid:
   its changers print on the game whose engine moved the standing) and the mod's put-back of the pair to its owner's value
   (Quiet, with the value put back) move it without a line. Emptied with the world (Clear). */
class NoticeBook
{
public:
    std::vector<std::string> Live(const std::string& key, float held, bool heldFlag, float after, bool afterFlag, const std::string& faction)
    {
        float from = held; bool fromFlag = heldFlag;
        std::map<std::string, std::pair<float, bool> >::const_iterator it = told_.find(key);
        if (it != told_.end()) { from = it->second.first; fromFlag = it->second.second; }
        Quiet(key, after, afterFlag);
        return TheirNotices(from, fromFlag, after, afterFlag, faction);
    }
    void Quiet(const std::string& key, float after, bool afterFlag) { told_[key] = std::make_pair(after, afterFlag); }
    /* this game's engine moved the pair from `from` to `to` through a changer that calls its notice: true when the engine
       printed a sentence, and the book then holds `to` */
    bool EngineSaid(const std::string& key, float from, float to, bool toFlag)
    {
        if (!EngineNoticeCrossed(from, to)) return false;
        Quiet(key, to, toFlag);
        return true;
    }
    void Clear() { told_.clear(); }
    std::size_t Size() const { return told_.size(); }
private:
    std::map<std::string, std::pair<float, bool> > told_;
};

/* the TEST-ONLY lever `playerstab [faction | open | select <slot> | stance ally|neutral|hostile | action invite|remove|disband|leave
   | confirm | cancel | shown]` (bare: the report; faction: the FACTION tab, the game's own; action: the bottom line's button of
   that action, l.button = teamscreens.h kAct* 1..4; confirm / cancel: the box's right / left button, whichever box is up;
   shown: what the tab and the box show now) */
enum LeverKind { kLeverBad = 0, kLeverOpen, kLeverSelect, kLeverStance, kLeverConfirm, kLeverCancel, kLeverReport, kLeverFaction, kLeverAction, kLeverShown };
struct Lever { int kind; int slot; int button; };
inline Lever ParseLever(const std::string& args)
{
    Lever l; l.kind = kLeverBad; l.slot = -1; l.button = -1;
    std::vector<std::string> t;
    std::string cur;
    for (std::size_t i = 0; i <= args.size(); ++i)
    {
        const char ch = i < args.size() ? args[i] : ' ';
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { if (!cur.empty()) { t.push_back(cur); cur.clear(); } }
        else cur += ch;
    }
    if (t.empty()) { l.kind = kLeverReport; return l; }
    if (t[0] == "open" && t.size() == 1) l.kind = kLeverOpen;
    else if (t[0] == "faction" && t.size() == 1) l.kind = kLeverFaction;
    else if (t[0] == "confirm" && t.size() == 1) l.kind = kLeverConfirm;
    else if (t[0] == "cancel" && t.size() == 1) l.kind = kLeverCancel;
    else if (t[0] == "shown" && t.size() == 1) l.kind = kLeverShown;
    else if (t[0] == "action" && t.size() == 2)
    {
        static const char* const w[4] = { "invite", "remove", "disband", "leave" };   /* teamscreens.h ActionWord 1..4 */
        for (int k = 0; k < 4; ++k) if (t[1] == w[k]) { l.kind = kLeverAction; l.button = k + 1; }
    }
    else if (t[0] == "select" && t.size() == 2)
    {
        int v = 0; bool ok = !t[1].empty() && t[1].size() <= 4;
        for (std::size_t i = 0; ok && i < t[1].size(); ++i) { if (t[1][i] < '0' || t[1][i] > '9') ok = false; else v = v * 10 + (t[1][i] - '0'); }
        if (ok) { l.kind = kLeverSelect; l.slot = v; }
    }
    else if (t[0] == "stance" && t.size() == 2)
    {
        for (int k = 0; k < 3; ++k) if (t[1] == LevelVerb(k)) { l.kind = kLeverStance; l.button = k; }
    }
    return l;
}

}   /* namespace playerstab */
