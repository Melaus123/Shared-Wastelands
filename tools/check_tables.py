"""check_tables.py - every address table in src/coop-plugin/addresses/ must name exactly the same rows.

Owner decision 99 (Steam 1.0.65 and 1.0.68 supported): the mod picks a table by the game's fingerprint and refuses
to start when a registered name is missing from it (addresses.cpp: addrBindMissing -> MOD DISABLED). A row added to
one table and not the other would disable the mod on that game version only, and no bench run on the other version
would notice. This check fails the build instead (mig7, 2026-09-29). It also checks each table's !count.
Usage: python tools/check_tables.py [addresses-dir]   (exit 0 = consistent, 1 = not)"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "src", "coop-plugin", "addresses")
tables = sorted(f for f in os.listdir(d) if f.endswith(".txt"))
bad = 0
names = {}
for f in tables:
    rows, count = [], None
    for line in open(os.path.join(d, f), encoding="utf-8"):
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("!count"):
            count = int(s.split()[1])
        elif not s.startswith("!"):
            rows.append(s.split()[0])
    names[f] = set(rows)
    if count != len(rows):
        print("TABLE CHECK FAILED: %s declares !count %s but has %d rows" % (f, count, len(rows)))
        bad = 1
    if len(set(rows)) != len(rows):
        print("TABLE CHECK FAILED: %s names a row twice" % f)
        bad = 1
ref = tables[0] if tables else None
for f in tables[1:]:
    only_ref = sorted(names[ref] - names[f])
    only_f = sorted(names[f] - names[ref])
    if only_ref or only_f:
        print("TABLE CHECK FAILED: %s and %s name different rows" % (ref, f))
        if only_ref:
            print("  only in %s: %s" % (ref, ", ".join(only_ref)))
        if only_f:
            print("  only in %s: %s" % (f, ", ".join(only_f)))
        print("  (a new row goes into EVERY table - for 1.0.68 re-run tools/gen_table_1068.py)")
        bad = 1
# T-63 stage 7: signatures.sig must name exactly the tables' rows (with a signature, or on a '!nosig' line), or the
# pattern road would refuse on every unknown build. Fix: re-run tools/gen_signatures.py (and tools/prove_signatures.py).
sigp = os.path.join(d, "signatures.sig")
if ref and os.path.exists(sigp):
    srows, nosig, scount = [], [], None
    for line in open(sigp, encoding="ascii"):
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "!nosig":
            nosig.append(t[1])
        elif t[0] == "!count":
            scount = int(t[1])
        elif not t[0].startswith("!"):
            srows.append(t[0])
    sn = set(srows) | set(nosig)
    if scount != len(srows) or len(sn) != len(srows) + len(nosig) or sn != names[ref]:
        print("TABLE CHECK FAILED: signatures.sig does not name exactly the tables' rows (re-run tools/gen_signatures.py)")
        if sn - names[ref]:
            print("  only in signatures.sig: " + ", ".join(sorted(sn - names[ref])))
        if names[ref] - sn:
            print("  missing from signatures.sig: " + ", ".join(sorted(names[ref] - sn)))
        bad = 1
    elif nosig:
        # gog1 fold: a '!nosig' row makes the pattern road refuse on EVERY unknown build, so it is a build failure, not
        # a count to report. Every row gets a signature (tools/gen_signatures.py), or the row is not shipped.
        print("TABLE CHECK FAILED: signatures.sig has %d row(s) with no signature: %s" % (len(nosig), ", ".join(nosig)))
        bad = 1
    elif not bad:
        print("signature check: %d rows, every one with a signature" % len(srows))
elif ref:
    print("TABLE CHECK FAILED: no signatures.sig in %s (tools/gen_signatures.py makes it)" % d)
    bad = 1
if not bad:
    print("table check: %d tables, %d rows each, same names" % (len(tables), len(names[ref]) if ref else 0))
sys.exit(bad)
