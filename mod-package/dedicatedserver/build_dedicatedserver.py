"""build_dedicatedserver.py - builds dedicatedserver.mod: the six Drifter starts a dedicated server's startmode=kenshi hands out.

    python3 build_dedicatedserver.py --kmod DIR [--out FILE]

    --kmod  folder holding kmod.py (the .mod v16 reader/writer) and all.pkl (the merged vanilla records); required: --kmod DIR or the
            KMOD_WORK environment variable (the build stops with an error without one)
    --out   default: ../Shared Wastelands/dedicatedserver/dedicatedserver.mod (shipped inside the mod folder, in a SUBFOLDER: Kenshi
            registers one .mod per mod folder, so it cannot sit beside "Shared Wastelands.mod"; the plugin copies it to
            <Kenshi>/mods/dedicatedserver/ and switches it on - src/coop-plugin/kmode.cpp)

Every Drifter: one character (race and appearance chosen in the character editor), a BLANK start town (Kenshi picks a random town),
2500 cats, the lean's own outfit (worn), an iron club, a basic first aid kit, a little dried meat (no Foodcubes). Stats: strength,
toughness, dexterity and athletics 15, every other skill 5, and the lean's +5. The start ids must equal src/common/startmode.h's
kLeans (checked below), because the plugin swaps the picked start for one of them by id.

Every item is resolved from all.pkl by its EXACT vanilla name. Where a name is ambiguous the gamedata.base / rebirth record that is
a clothing item (type 3) is taken; where two pieces of an outfit share a slot, the second is swapped for the nearest vanilla piece of
a free slot (CLASH_SWAPS below). The build prints both, and refuses if any outfit still has two pieces in one slot.
"""
import argparse, copy, os, pickle, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument('--kmod', default=os.environ.get('KMOD_WORK', ''), help='folder holding kmod.py and all.pkl (or set KMOD_WORK); required')
ap.add_argument('--out', default=os.path.join(HERE, '..', 'Shared Wastelands', 'dedicatedserver', 'dedicatedserver.mod'))
ap.add_argument('--start-pos', default='', help='x,z: every Drifter starts at this world position (force start pos) instead of a random town')
args = ap.parse_args()
if not args.kmod or not os.path.isfile(os.path.join(args.kmod, 'kmod.py')) or not os.path.isfile(os.path.join(args.kmod, 'all.pkl')):
    ap.error('--kmod DIR (or the KMOD_WORK environment variable) must name the folder holding kmod.py and all.pkl; got %r' % (args.kmod,))
START_POS = tuple(int(float(v)) for v in args.start_pos.split(',')) if args.start_pos else None
sys.path.insert(0, args.kmod)
import kmod  # noqa: E402

A = pickle.load(open(os.path.join(args.kmod, 'all.pkl'), 'rb'))[0]
MOD = 'dedicatedserver.mod'
NEW = -2147483646
SLOT_NAMES = {3: 'hat', 5: 'armour', 6: 'pants', 8: 'shirt', 9: 'boots'}

BY_NAME = {}
for s, r in A.items():
    BY_NAME.setdefault(r['name'], []).append(s)


def sid_of(name, want_type):
    """The record with exactly this name and type; gamedata.base / rebirth first when there are several."""
    hits = [s for s in BY_NAME.get(name, []) if A[s]['type'] == want_type]
    if not hits:
        sys.exit('REFUSED: no vanilla %s record named %r' % ({3: 'clothing', 4: 'item', 2: 'weapon'}.get(want_type, want_type), name))
    hits.sort(key=lambda s: (0 if s.endswith('-gamedata.base') else 1 if s.endswith('-rebirth.mod') else 2, s))
    if len(hits) > 1 or len(BY_NAME[name]) > 1:
        print('  ambiguous %r: %s -> %s' % (name, ', '.join('%s (type %d)' % (s, A[s]['type']) for s in BY_NAME[name]), hits[0]))
    return hits[0]


def slot_of(sid):
    return dict(A[sid]['int']).get('slot')


def setv(r, kind, key, val):
    d = dict(r[kind]); d[key] = val; r[kind] = sorted(d.items())


# ---- the leans: the start sheet (2026-10-20) ----
LEANS = [
    # name,         start id, outfit (exact vanilla names),                                                   +5 skills (stat keys)
    ('Fighter',     7200041, ['Dark Leather Shirt', 'Leather Vest', 'Cargopants [padded]', "Drifter's Boots", 'Red Bandana'], ['attack', 'defence', 'dodge']),
    ('Scout',       7200042, ['Cloth Shirt', 'Sleeveless Dustcoat', 'Halfpants (colored)', "Drifter's Boots", 'Straw Hat'],    ['athletics', 'swimming']),
    ('Assassin',    7200043, ['Black Cloth Shirt', 'Ninja Pants', 'Wooden Sandals', 'Ninja Mask'],                             ['stealth', 'assassin']),
    ('Labourer',    7200044, ['Leather Turtleneck', "Drifter's Leather Pants", "Drifter's Boots", 'Rattan Hat'],               ['labouring', 'strength']),
    ('Medic',       7200045, ['Turtleneck', 'White Vest', 'Gi Pants', 'Wooden Sandals', 'Cap'],                                ['medic', 'cooking']),
    ('Researcher',  7200046, ['Leather Shirt', 'Longcoat', 'Samurai Clothpants', "Drifter's Boots", 'Square Goggles'],         ['science', 'engineer']),
]
# Kenshi's vests are SHIRTS (slot 8), so a vest listed over a shirt cannot be worn with it. The vest becomes the nearest vanilla
# over-layer (armour slot 5) instead; the shirt the sheet names first stays.
CLASH_SWAPS = {
    ('Fighter', 'Leather Vest'): "Drifter's Leather Jacket",   # leather, sleeveless-look light armour layer
    ('Medic', 'White Vest'): 'Sleeveless Longcoat',            # a plain light cloth over-layer
}
BASIC = ['strength', 'toughness2', 'dexterity', 'athletics']   # 15 before the lean
STAT_KEYS = [k for k, _ in A['106-gamedata.quack']['float']]  # every skill a STATS record holds (33)
WEAPON = '52295-rebirth.mod'      # Iron Club - the Wanderer's own weapon
WEAPON_MAKER = '912-gamedata.base'  # its manufacturer (the Wanderer's 'weapon level')
FIRST_AID = sid_of('Basic First Aid Kit', 4)
FOOD = sid_of('Dried Meat', 4)
MONEY = 2500
assert A[WEAPON]['type'] == 2 and A[WEAPON]['name'] == 'Iron Club'
assert 'foodcube' not in A[FOOD]['name'].lower()


def check_against_header():
    """startmode.h's kLeans must be these names and ids, in this order (the plugin swaps by id)."""
    h = os.path.join(HERE, '..', '..', 'src', 'common', 'startmode.h')
    if not os.path.isfile(h):
        print('  (src/common/startmode.h not found - id check skipped)'); return
    rows = re.findall(r'\{\s*"(Drifter \([A-Za-z]+\))",\s*"([^"]+)"\s*\}', open(h, encoding='utf-8').read())
    want = [('Drifter (%s)' % n, '%d-%s' % (i, MOD)) for n, i, _, _ in LEANS]
    if rows != want:
        sys.exit('REFUSED: src/common/startmode.h kLeans %s differ from this script %s' % (rows, want))
    print('  startmode.h kLeans match (%d starts)' % len(rows))


def stats_rec(sid_id, lean, plus):
    st = copy.deepcopy(A['458-gamedata.base'])
    st.update(name='Drifter (%s) stats' % lean, id=sid_id, sid='%d-%s' % (sid_id, MOD), flags=NEW, instcount=0)
    vals = {}
    for k in STAT_KEYS:
        vals[k] = 15.0 if k in BASIC else 5.0
    for k in plus:
        if k not in vals: sys.exit('REFUSED: no stat %r' % k)
        vals[k] += 5.0
    st['float'] = [(k, vals[k]) for k in STAT_KEYS]
    return st, vals


def outfit(lean, names):
    sids, notes = [], []
    for n in names:
        use = CLASH_SWAPS.get((lean, n), n)
        if use != n: notes.append('%s -> %s' % (n, use))
        sids.append(sid_of(use, 3))
    slots = [slot_of(s) for s in sids]
    if len(set(slots)) != len(slots) or any(s not in SLOT_NAMES for s in slots):
        sys.exit('REFUSED: %s outfit slots clash or are not wearable: %s' % (lean, [(A[s]['name'], slot_of(s)) for s in sids]))
    return sids, notes


def char_rec(sid_id, lean, stats, wear):
    c = copy.deepcopy(A['1533662-rebirth.mod'])   # the Wanderer start's character: no race ref, so the editor offers every race
    c.update(name='Drifter', id=sid_id, sid='%d-%s' % (sid_id, MOD), flags=NEW, instcount=0)
    for k, v in {'stats randomise': 0, 'combat stats': 5, 'strength': 15, 'unarmed stats': 5, 'ranged stats': 5, 'stealth stats': 5,
                 'armour grade': 1, 'money min': 0, 'money max': 0}.items():
        setv(c, 'int', k, v)
    drop = ('clothing', 'weapons', 'weapon level', 'inventory', 'stats', 'backpack')
    c['refs'] = [(k, v) for k, v in c['refs'] if k not in drop]
    c['refs'] += [('clothing', [(s, 1, 100, 0) for s in wear]),            # v0 = 1: worn at the start
                  ('weapons', [(WEAPON, 1, 0, 100)]), ('weapon level', [(WEAPON_MAKER, 0, 0, 0)]),
                  ('inventory', [(FIRST_AID, 1, 0, 0), (FOOD, 3, 0, 0)]),  # important items first (inventory overflow drops the tail)
                  ('stats', [(stats['sid'], 0, 100, 0)])]
    return c


def squad_rec(sid_id, title, ch):
    sq = copy.deepcopy(A['45550-gamedata.base'])
    sq.update(name='startoff- ' + title, id=sid_id, sid='%d-%s' % (sid_id, MOD), flags=NEW, instcount=0)
    sq['refs'] = [(k, v) for k, v in sq['refs'] if k != 'leader'] + [('leader', [(ch['sid'], 1, 0, 0)])]
    return sq


def start_rec(sid_id, title, sq, text):
    st = copy.deepcopy(A['1980-gamedata.base'])   # Wanderer
    st.update(name=title, id=sid_id, sid='%d-%s' % (sid_id, MOD), flags=NEW, instcount=0)
    setv(st, 'int', 'money', MONEY)
    setv(st, 'bool', 'force start pos', START_POS is not None)
    if START_POS is not None:
        setv(st, 'int', 'start pos X', START_POS[0]); setv(st, 'int', 'start pos Z', START_POS[1])
    setv(st, 'str', 'description', text)
    st['refs'] = [(k, [(sq['sid'], 0, 0, 0)] if k == 'squad' else v) for k, v in st['refs'] if k != 'town']   # no town: Kenshi picks one at random
    return st


PRETTY = {'attack': 'melee attack', 'defence': 'melee defence', 'dodge': 'dodge', 'athletics': 'athletics', 'swimming': 'swimming',
          'stealth': 'stealth', 'assassin': 'assassination', 'labouring': 'labouring', 'strength': 'strength', 'medic': 'field medic',
          'cooking': 'cooking', 'science': 'science', 'engineer': 'engineering'}

print('dedicatedserver.mod')
check_against_header()
recs = []
for i, (lean, start_id, names, plus) in enumerate(LEANS):
    title = 'Drifter (%s)' % lean
    stats, vals = stats_rec(7200011 + i, lean, plus)
    wear, notes = outfit(lean, names)
    ch = char_rec(7200021 + i, lean, stats, wear)
    sq = squad_rec(7200031 + i, title, ch)
    text = ('One drifter, a few cats and the clothes on their back, set down %s. Strength, toughness, dexterity and '
            'athletics 15, every other skill 5, and %s %s\'s +5 to %s. 2500 cats, an iron club, a basic first aid kit and some dried meat.'
            % ('at the server\'s start point' if START_POS else 'in a random town', 'an' if lean[0] in 'AEIOU' else 'a', lean.lower(),
               ' and '.join(PRETTY[k] for k in plus)))
    st = start_rec(start_id, title, sq, text)
    recs += [stats, ch, sq, st]
    print('  %-22s %s  outfit: %s%s' % (title, st['sid'], ', '.join('%s [%s] %s' % (A[s]['name'], SLOT_NAMES[slot_of(s)], s) for s in wear),
                                       ('  (swapped: %s)' % '; '.join(notes)) if notes else ''))
    print('  %-22s stats: %s' % ('', ', '.join('%s %d' % (k, v) for k, v in sorted(vals.items()) if v != 5.0)))

h = dict(version=1, author='Shared Wastelands', desc='Drifter starts for dedicated servers (startmode=kenshi): a random lean, a random town.',
         deps='gamedata.base', refs='Newwworld.mod,rebirth.mod', unk=7200046)
os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
kmod.write(args.out, h, recs)
h2, back = kmod.read(args.out)
assert h2['leftover'] == 0 and len(back) == len(recs), (h2['leftover'], len(back))
assert [r['sid'] for r in back] == [r['sid'] for r in recs]
print('wrote %s: %d records, %d bytes, read back clean' % (os.path.abspath(args.out), len(back), os.path.getsize(args.out)))
