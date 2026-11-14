/* src/common/levermade.h - TEST-ONLY: the characters the `spawn` lever made, for the levers' `spawned:<Template>[#k]` and
 * `made:<label>` picks (items.cpp P105NameUid).
 *
 * `spawned:<Template>#k` used to count every [M1] spawned line of that template, so wild animals that wandered in and NPC groups a
 * game took over (their copies are made through the same create path) pushed the run's own spawns out of the list (T2125: wild
 * Skimmers 5-7 km away; T2132: a Tech Hunters squad of Drifters picked instead of the run's pair). Now only a MARKED uid enters a
 * template's list:
 *   - on the game whose `spawn` lever made it (spawn.cpp SpawnTemplate marks the uid before the creation's [M1] spawned line);
 *   - on every other game, as a copy whose SPAWN carried the lever-made bit (spawnage.h kSpawnFlagLeverMade, a spare bit of the
 *     flags byte an older build ignores; session.cpp OnSpawn marks the uid before the copy's [M1] spawned line).
 * `spawn <template> [dx] [dz] as <label>` also names the character <label> (its in-game name, which reaches the other game as every
 * character's name does - MSG_NAME after its SPAWN); `made:<label>` picks the marked character whose name keys as <label>, on both
 * games. The name key is the caller's (coopsay::TalkPersonKey: case-insensitive, '_' and a space alike).
 *
 * Pure: no engine memory, no Windows; the offline suite runs the same class. C++03 (VS2010 v100). MAIN THREAD in the plugin.
 */
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace levermade {

const int kKeep = 8;                /* per template: the newest marked uids its [M1] spawned lines named */
const size_t kTemplateCap = 256;    /* templates remembered (a new one replaces the oldest) */
const size_t kMarkCap = 4096;       /* marked uids remembered (a new one replaces the oldest mark) */
const size_t kLabelMax = 40;

/* A label for `spawn .. as <label>`: 1..kLabelMax characters of letters, digits, '_' and '-' (one word: the levers split their
   arguments on spaces; no ':' or '#', which the pick forms use). 1 = usable. */
inline int LabelOk(const std::string& s)
{
    if (s.empty() || s.size() > kLabelMax) return 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

/* `<template words> [dx] [dz] as <label>` -> *rest = the text before " as ", *label = the last word. 1 = an `as <label>` tail
   (*label may still fail LabelOk), 0 = none (*rest = s, *label empty), -1 = "as" as the last word with no label. */
inline int SplitAsLabel(const std::string& s, std::string* rest, std::string* label)
{
    *rest = s; label->clear();
    std::string t = s;
    while (!t.empty() && (t[t.size() - 1] == ' ' || t[t.size() - 1] == '\t')) t.erase(t.size() - 1);
    if (t == "as" || (t.size() > 3 && t.compare(t.size() - 3, 3, " as") == 0)) return -1;
    const std::string::size_type sp = t.find_last_of(" \t");
    if (sp == std::string::npos || sp < 3) return 0;
    std::string head = t.substr(0, sp);
    while (!head.empty() && (head[head.size() - 1] == ' ' || head[head.size() - 1] == '\t')) head.erase(head.size() - 1);
    if (head.size() < 3 || head.compare(head.size() - 2, 2, "as") != 0) return 0;
    const char before = head[head.size() - 3];
    if (before != ' ' && before != '\t') return 0;
    *label = t.substr(sp + 1);
    std::string r = head.substr(0, head.size() - 2);
    while (!r.empty() && (r[r.size() - 1] == ' ' || r[r.size() - 1] == '\t')) r.erase(r.size() - 1);
    *rest = r;
    return 1;
}

class Book
{
public:
    Book() : seq_(0) {}

    /* uid made by the spawn lever here, or a copy announced lever-made. nameKey: its label's key ("" = not known). A repeat mark
       keeps its place; a non-empty nameKey replaces the stored one. */
    void Mark(unsigned int uid, const std::string& nameKey)
    {
        if (uid == 0) return;
        std::map<unsigned int, Mk>::iterator it = marks_.find(uid);
        if (it != marks_.end()) { if (!nameKey.empty()) it->second.nameKey = nameKey; return; }
        if (marks_.size() >= kMarkCap)
        {
            std::map<unsigned int, Mk>::iterator old = marks_.begin();
            for (std::map<unsigned int, Mk>::iterator j = marks_.begin(); j != marks_.end(); ++j) if (j->second.seq < old->second.seq) old = j;
            marks_.erase(old);
        }
        Mk m; m.seq = ++seq_; m.nameKey = nameKey;
        marks_[uid] = m;
    }
    /* the lever's creation failed: the uid was never used */
    void Unmark(unsigned int uid)
    {
        marks_.erase(uid);
        for (size_t i = 0; i < rows_.size(); ++i)
            for (size_t j = 0; j < rows_[i].uid.size(); ++j) if (rows_[i].uid[j] == uid) { rows_[i].uid.erase(rows_[i].uid.begin() + j); break; }
    }
    bool IsMarked(unsigned int uid) const { return marks_.find(uid) != marks_.end(); }
    /* the character's current name key, read by the caller (a copy learns its label from its name); marked uids only */
    void SetNameKey(unsigned int uid, const std::string& nameKey)
    {
        std::map<unsigned int, Mk>::iterator it = marks_.find(uid);
        if (it != marks_.end() && !nameKey.empty()) it->second.nameKey = nameKey;
    }
    std::string NameKeyOf(unsigned int uid) const
    {
        std::map<unsigned int, Mk>::const_iterator it = marks_.find(uid);
        return (it != marks_.end()) ? it->second.nameKey : std::string();
    }

    /* an [M1] spawned line for template tmplKey named uid. 1 = noted (a marked uid: it moves to the front of the template's list,
       the oldest falls off a full list); 0 = ignored (not made by the spawn lever - a wild animal, a taken-over group, a save's) */
    int NoteSpawned(const std::string& tmplKey, unsigned int uid)
    {
        if (!IsMarked(uid)) return 0;
        TRow* r = 0;
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].key == tmplKey) { r = &rows_[i]; break; }
        if (r == 0)
        {
            if (rows_.size() >= kTemplateCap) rows_.erase(rows_.begin());
            TRow n; n.key = tmplKey; rows_.push_back(n);
            r = &rows_[rows_.size() - 1];
        }
        for (size_t j = 0; j < r->uid.size(); ++j) if (r->uid[j] == uid) { r->uid.erase(r->uid.begin() + j); break; }
        r->uid.insert(r->uid.begin(), uid);
        if (r->uid.size() > (size_t)kKeep) r->uid.resize((size_t)kKeep);
        return 1;
    }
    /* the template's noted uids, newest first (empty: none; *known = 0 when the template has no row) */
    std::vector<unsigned int> Row(const std::string& tmplKey, int* known) const
    {
        *known = 0;
        for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].key == tmplKey) { *known = 1; return rows_[i].uid; }
        return std::vector<unsigned int>();
    }
    /* every marked uid, newest mark first */
    std::vector<unsigned int> Marked() const
    {
        std::vector<std::pair<unsigned long, unsigned int> > v;
        for (std::map<unsigned int, Mk>::const_iterator it = marks_.begin(); it != marks_.end(); ++it) v.push_back(std::make_pair(it->second.seq, it->first));
        std::sort(v.begin(), v.end());   /* oldest mark first (seqs are unique) */
        std::vector<unsigned int> out;
        for (size_t i = v.size(); i > 0; --i) out.push_back(v[i - 1].second);
        return out;
    }
    /* the marked uids whose name key is nameKey, newest mark first */
    std::vector<unsigned int> Named(const std::string& nameKey) const
    {
        std::vector<unsigned int> all = Marked(), out;
        if (nameKey.empty()) return out;
        for (size_t i = 0; i < all.size(); ++i) if (NameKeyOf(all[i]) == nameKey) out.push_back(all[i]);
        return out;
    }
    size_t MarkCount() const { return marks_.size(); }

private:
    struct Mk { unsigned long seq; std::string nameKey; };
    struct TRow { std::string key; std::vector<unsigned int> uid; };
    std::map<unsigned int, Mk> marks_;
    std::vector<TRow> rows_;
    unsigned long seq_;
};

} // namespace levermade
