#!/usr/bin/env python3
"""gen_signatures.py - a byte pattern ("signature") for EVERY address-table row, so an executable with no table
(a GOG build, a patch nobody has tabled yet) can be resolved by searching for code instead of being refused
outright (T-63 stage 7, owner decision 99).

OFFLINE AND READ-ONLY on both game executables, exactly as tools/gen_table_1068.py: they are opened for reading
and never written, moved, copied, run or loaded.

    python tools/gen_signatures.py            generate src/coop-plugin/addresses/signatures.sig, then prove it
    python tools/gen_signatures.py --report   the per-row report only, nothing written

WHAT A SIGNATURE IS. A run of code bytes with wildcards ("??") over everything that only encodes "where something
else is" (rel32 call/jump targets, RIP-relative displacements, switch-table entries, 64-bit immediates into the
image - the masking of gen_table_1068.masked()) AND over every byte that differs between Steam 1.0.65 and Steam
1.0.68. It is built from BOTH executables at the rows their two tables give, and it is kept only if it occurs
EXACTLY ONCE in each one's .text. The pattern locates:
  code  the function entry (rva = pattern start + a)
  ret   the CALL whose return address the row is (rva = pattern start + a; b = the call's length)
  var / ro / ptr
        an instruction that addresses the row RIP-relatively; the row is that instruction's target (+ c).
Growth: forward from the located point 16, 24, 32 ... 1024 bytes, then with 16..512 bytes of context before it.
Among several referencing instructions (data rows) the shortest unique pattern wins.

THE FILE (src/coop-plugin/addresses/signatures.sig, plain text, shipped beside the tables):
    <name> <kind> <anchor> <pattern> <a> <b> <c> <d> <check>
  anchor   offset of 4 fixed pattern bytes the scanner indexes on (the rarest such window in both builds)
  code     a = rva - pattern start, b = verify offset (0, or 16 where RE_Kenshi hooks the entry), c = bytes to verify
  ret      a = rva - pattern start, b = call length;              check = the call's bytes (with ??)
  var      a = displacement offset in the pattern, b = displacement-to-instruction-end, c = target adjustment
  ro       a, b, c as var; d = verify offset;                      check = the constant bytes (with ??)
  ptr      a, b, c as var; d = the vtable slot's offset;           check = the pointed-to code's first bytes (with ??)
  section  the section both Steam builds hold the row in; the resolved row must be in the section of that NAME
  rank     the row's position, in 1.0.68 address order, within its class (code+ret / var / ro+ptr); the resolved
           addresses must rise in rank order within each class (0 inversions between the two Steam builds)
  extra    '-' or comma-separated items:
           g:<anchor>:<pattern>:<a>:<b>:<c>   a var row's SECOND referencing instruction (a different function);
                                              it must resolve to the same address as the first
           k:<off>:<row>                      a direct CALL at pattern offset <off> whose target is code row <row>
                                              in both Steam builds; after resolving it must still land on that row
Rows no unique pattern could be made for are named on '!nosig <name>' lines and counted in '!nosigcount'; the build
refuses any (tools/check_tables.py), and so does the plugin.

The proof (tools/prove_signatures.py) re-implements the plugin's resolver independently and runs it on both exes.
Needs: Python 3, capstone 5, numpy.
"""
import argparse
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_table_1068 as G  # noqa: E402  (Image, masked, Xrefs, read_table, MD, disp_pos, rip_target)
import prove_signatures as PS  # noqa: E402  (Pe: section names)

ADDR_DIR = G.ADDR_DIR
TABLE65 = G.TABLE65
TABLE68 = os.path.join(ADDR_DIR, '6602D59D-0232D000-BBA641C8-02304800.txt')
OUT = os.path.join(ADDR_DIR, 'signatures.sig')
FWD = (16, 24, 32, 40, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512, 768, 1024)
BACK = (16, 32, 64, 128, 256, 512)
MIN_FIXED = 8


def scan_range(img):
    """.text as the plugin scans it: the section's raw bytes, min(VirtualSize, SizeOfRawData) from its VA."""
    raw = img.raw
    e = struct.unpack_from('<I', raw, 0x3C)[0]
    nsec = struct.unpack_from('<H', raw, e + 6)[0]
    sh = e + 24 + struct.unpack_from('<H', raw, e + 20)[0]
    for i in range(nsec):
        o = sh + 40 * i
        if raw[o:o + 8].rstrip(b'\0') == b'.text':
            vsize, va, rsize, _rptr = struct.unpack_from('<IIII', raw, o + 8)
            return va, va + min(vsize, rsize)
    raise SystemExit('no .text in ' + img.path)


class Index(object):
    """Every 4-byte window of .text, sorted, so 'where does this u32 occur' is a binary search."""

    def __init__(self, img):
        self.img = img
        self.lo, self.hi = scan_range(img)
        self.t = img.np[self.lo:self.hi]
        n = len(self.t) - 3
        t32 = self.t[:n].astype(np.uint32) | (self.t[1:n + 1].astype(np.uint32) << 8) \
            | (self.t[2:n + 2].astype(np.uint32) << 16) | (self.t[3:n + 3].astype(np.uint32) << 24)
        self.order = np.argsort(t32, kind='stable').astype(np.int64)
        self.sorted = t32[self.order]

    def count_u32(self, v):
        v = np.uint32(v)
        return int(np.searchsorted(self.sorted, v, 'right') - np.searchsorted(self.sorted, v, 'left'))

    def matches(self, pat, keep, anchor, limit=4):
        """Pattern starts (rvas) where every fixed byte agrees; at most `limit` are returned."""
        v = np.uint32(struct.unpack_from('<I', pat, anchor)[0])
        l, r = np.searchsorted(self.sorted, v, 'left'), np.searchsorted(self.sorted, v, 'right')
        starts = self.order[l:r] - anchor
        starts = starts[(starts >= 0) & (starts + len(pat) <= len(self.t))]
        idx = np.flatnonzero(keep)
        want = np.frombuffer(pat, dtype=np.uint8)[idx]
        out = []
        for c in range(0, len(starts), 4096):
            s = starts[c:c + 4096]
            ok = np.all(self.t[s[:, None] + idx[None, :]] == want[None, :], axis=1)
            out.extend(int(x) + self.lo for x in s[ok])
            if len(out) >= limit:
                break
        return out


def best_anchor(ixs, pat, keep):
    """The fixed 4-byte window that occurs least often in the worse of the two builds, or None."""
    k = np.asarray(keep, dtype=bool)
    if len(k) < 4:
        return None
    full = k[:-3] & k[1:-2] & k[2:-1] & k[3:]
    best = None
    for j in np.flatnonzero(full):
        v = struct.unpack_from('<I', pat, int(j))[0]
        c = max(ix.count_u32(v) for ix in ixs)
        if best is None or c < best[0]:
            best = (c, int(j))
            if c <= 1:
                break
    return None if best is None else best[1]


def mview(img, start, end):
    """Masked bytes/keep of [start, end), decoded from the start of the exception-table fragment holding `start`
    (so instruction boundaries are the real ones), or from `start` itself for code with no fragment."""
    fr = img.fragment(start)
    origin = fr[0] if fr is not None else start
    key = (id(img), origin)
    c = _MV.get(key)
    if c is None or c[2] < end:
        want = max(end, (fr[1] if fr is not None else start) + 0x480, c[2] * 2 - origin if c else 0)
        mk = G.masked(img, origin, want)
        c = (mk.sig, np.array(mk.keep, dtype=bool), want)
        _MV[key] = c
    o = start - origin
    e = end - origin
    return c[0][o:e], c[1][o:e]


_MV = {}


def combined(a, b, x65, x68, back, fwd):
    """The pattern for [x-back, x+fwd) in both builds: fixed where both keep the byte and both have it equal."""
    s65, k65 = mview(a, x65 - back, x65 + fwd)
    s68, k68 = mview(b, x68 - back, x68 + fwd)
    n = min(len(s65), len(s68), back + fwd)
    if n < back + 4:
        return None, None
    s65, s68 = np.frombuffer(s65[:n], dtype=np.uint8), np.frombuffer(s68[:n], dtype=np.uint8)
    keep = k65[:n] & k68[:n] & (s65 == s68)
    pat = bytes(np.where(keep, s65, 0).astype(np.uint8))
    return pat, keep


def unique_pattern(a, b, ixs, x65, x68):
    """Shortest pattern locating x65 in 1.0.65 and x68 in 1.0.68 uniquely. -> (pat, keep, anchor, back) or None"""
    for back in (0,) + BACK:
        if back and (a.fragment(x65 - back) is None or b.fragment(x68 - back) is None):
            continue
        for fwd in FWD:
            pat, keep = combined(a, b, x65, x68, back, fwd)
            if pat is None:
                break
            if int(keep.sum()) < MIN_FIXED:
                continue
            anc = best_anchor(ixs, pat, keep)
            if anc is None:
                continue
            m65 = ixs[0].matches(pat, keep, anc, 2)
            m68 = ixs[1].matches(pat, keep, anc, 2)
            if m65 == [x65 - back] and m68 == [x68 - back]:
                return pat, keep, anc, back
            if len(pat) < back + fwd:        # the decodable code ran out: longer windows are the same window
                break
    return None


def pattern_text(pat, keep):
    return ''.join('%02X' % pat[i] if keep[i] else '??' for i in range(len(pat)))


def shape(imgs_at, n):
    """A check string: the n bytes at each (img, rva, maskedkeep) - fixed where every build agrees and keeps."""
    bs = [img.rd(at, n) for img, at, _k in imgs_at]
    out = []
    for i in range(n):
        vals = set(bb[i] for bb in bs)
        kept = all(k is None or (i < len(k) and k[i]) for _img, _at, k in imgs_at)
        out.append('%02X' % bs[0][i] if (len(vals) == 1 and kept) else '??')
    return ''.join(out)


def code_keep(img, rva, n):
    s, k = mview(img, rva, rva + max(n, 32))
    return list(k[:n]) + [False] * (n - len(k[:n]))


def data_row(a, b, ixs, X65, X68, r65, r68, want=1):
    """Every RIP-relative reference to the row in 1.0.65, paired with one in 1.0.68 at the same place in code that
    agrees; unique patterns around DIFFERENT referencing instructions, shortest first (at least `want` of them when
    they exist). -> [(pat, keep, anc, back, dispoff, tail, i65, i68), ...] or a reason string"""
    t65, t68 = r65['rva'], r68['rva']
    refs65 = X65.find(t65, limit=48)
    refs68 = X68.find(t68, limit=48)
    if not refs65 or not refs68:
        return 'no RIP-relative reference to it (%d in 1.0.65, %d in 1.0.68)' % (len(refs65), len(refs68))
    cands = []
    used68 = set()
    tried = 0

    def local(img, i):
        s, k = mview(img, i, i + 24)
        return bytes(np.where(k, np.frombuffer(s, dtype=np.uint8), 0).astype(np.uint8)) + k.tobytes()

    def enough():
        if len(cands) < want:
            return False
        fx = sorted(int(c[1].sum()) for c in cands)
        return fx[want - 1] <= 24
    loc68 = [(local(b, i68), i68) for _f68, i68 in refs68]
    for _f65, i65 in refs65:
        if tried >= 12 * want or enough():
            break
        ins65 = next(G.MD.disasm(a.rd(i65, 16), i65))
        l65 = local(a, i65)
        for l68, i68 in loc68:
            if l68 != l65 or i68 in used68 or tried >= 12 * want:
                continue
            ins68 = next(G.MD.disasm(b.rd(i68, 16), i68))
            if ins65.bytes[:G.disp_pos(ins65)] != ins68.bytes[:G.disp_pos(ins68)] or ins65.size != ins68.size:
                continue
            tried += 1
            r = unique_pattern(a, b, ixs, i65, i68)
            if r is None:
                continue
            pat, keep, anc, back = r
            cands.append((pat, keep, anc, back, back + G.disp_pos(ins65), ins65.size - G.disp_pos(ins65), i65, i68))
            used68.add(i68)
            break
    if not cands:
        return 'no unique pattern around any of %d x %d referencing instructions' % (len(refs65), len(refs68))
    cands.sort(key=lambda c: int(c[1].sum()))
    return cands


def call_checks(a, b, pat, keep, s65, s68, code65, code68):
    """'k:<off>:<row>' for every direct CALL in the pattern (fixed E8, wildcard rel32) whose target is the SAME code row
    in both builds - after resolving, the call there must still land on that row (T-63 gog1 fold, cross-check d)."""
    out = []
    for off in range(len(pat) - 4):
        if not (keep[off] and pat[off] == 0xE8) or any(keep[off + 1:off + 5]):
            continue
        tg65 = G.final_target(a, s65 + off + 5 + struct.unpack('<i', a.rd(s65 + off + 1, 4))[0])   # through jmp stubs,
        tg68 = G.final_target(b, s68 + off + 5 + struct.unpack('<i', b.rd(s68 + off + 1, 4))[0])   # as the resolver does
        n = code65.get(tg65)
        if n is not None and code68.get(tg68) == n:
            out.append('k:%d:%s' % (off, n))
    return out


CLASS = {'code': 'C', 'ret': 'C', 'var': 'G', 'ro': 'R', 'ptr': 'R'}


def generate(args):
    a, b = G.Image(args.exe65), G.Image(args.exe68)
    _h65, rows65 = G.read_table(TABLE65)
    _h68, rows68 = G.read_table(TABLE68)
    by68 = dict((r['name'], r) for r in rows68)
    pe65, pe68 = PS.Pe(args.exe65), PS.Pe(args.exe68)
    # cross-check (a): each row's rank among its class in 1.0.68 address order
    rank = {}
    for cl in 'CGR':
        names = sorted((r['name'] for r in rows68 if CLASS[r['kind']] == cl), key=lambda n: by68[n]['rva'])
        for i, n in enumerate(names):
            rank[n] = i
    code65 = dict((r['rva'], r['name']) for r in rows65 if r['kind'] == 'code')
    code68 = dict((r['rva'], r['name']) for r in rows68 if r['kind'] == 'code')
    ixs = (Index(a), Index(b))
    print('indexed', flush=True)
    X65, X68 = G.Xrefs(a), G.Xrefs(b)
    only = set(args.only.split(',')) if args.only else None
    lines, nosig = [], []
    for r65 in rows65:
        name, k = r65['name'], r65['kind']
        if only is not None and name not in only:
            continue
        r68 = by68[name]
        assert r68['kind'] == k, name
        why = None
        line = None
        extra = []
        sec65 = pe65.section_of(r65['rva'])['name'].decode('ascii')
        sec68 = pe68.section_of(r68['rva'])['name'].decode('ascii')
        if sec65 != sec68:
            why = 'the row lives in section %s in 1.0.65 and %s in 1.0.68' % (sec65, sec68)
        elif k in ('code', 'ret'):
            n = len(r65['bytes']) // 2
            x65 = r65['rva'] if k == 'code' else r65['rva'] - n
            x68 = r68['rva'] if k == 'code' else r68['rva'] - n
            if (r65['at'] - r65['rva']) != (r68['at'] - r68['rva']):
                why = 'the verify offset differs between the builds'
            else:
                r = unique_pattern(a, b, ixs, x65, x68)
                if r is None:
                    why = 'no unique pattern within %d bytes after / %d before' % (FWD[-1], BACK[-1])
                else:
                    pat, keep, anc, back = r
                    extra = call_checks(a, b, pat, keep, x65 - back, x68 - back, code65, code68)
                    if k == 'code':
                        line = (name, k, anc, pattern_text(pat, keep), back, r65['at'] - r65['rva'], n, 0, '-')
                    else:
                        chk = shape([(a, x65, None), (b, x68, None)], n)
                        if chk[:2] == 'E8':            # a direct call: its rel32 moves with the code
                            chk = 'E8' + '??' * (n - 1)
                        line = (name, k, anc, pattern_text(pat, keep), back + n, n, 0, 0, chk)
        else:
            want = 2 if k == 'var' else 1
            r = data_row(a, b, ixs, X65, X68, r65, r68, want)
            if isinstance(r, str):
                why = r
            elif len(r) < want:
                why = 'a writable global needs TWO independent referencing instructions; only %d has a unique pattern' % len(r)
            else:
                pat, keep, anc, back, doff, tail, i65, i68 = r[0]
                extra = call_checks(a, b, pat, keep, i65 - back, i68 - back, code65, code68)
                if k == 'var':
                    p2, k2, a2, bk2, doff2, tail2, _j65, _j68 = r[1]
                    extra = ['g:%d:%s:%d:%d:0' % (a2, pattern_text(p2, k2), doff2, tail2)] + extra
                d, chk = 0, '-'
                if k == 'ro':
                    d = r65['at'] - r65['rva']
                    n = len(r65['bytes']) // 2
                    chk = shape([(a, r65['at'], None), (b, r68['at'], None)], n)
                elif k == 'ptr':
                    d = r65['at'] - r65['rva']
                    if d != r68['at'] - r68['rva']:
                        why = 'the vtable slot offset differs between the builds'
                    f65 = G.final_target(a, int(r65['bytes'], 16))
                    f68 = G.final_target(b, int(r68['bytes'], 16))
                    chk = shape([(a, f65, code_keep(a, f65, 16)), (b, f68, code_keep(b, f68, 16))], 16)
                if why is None:
                    line = (name, k, anc, pattern_text(pat, keep), doff, tail, 0, d, chk)
        if line is None:
            nosig.append((name, why))
            print('%-46s %-4s NO SIGNATURE: %s' % (name, k, why), flush=True)
        else:
            lines.append(line + (sec65, rank[name], ','.join(extra) if extra else '-'))
            fixed = len(line[3]) // 2 - line[3][::2].count('?')
            print('%-46s %-4s %4d bytes (%d fixed)%s%s' % (name, k, len(line[3]) // 2, fixed,
                                                          '  context %d before' % line[4] if (k in ('code',) and line[4]) else '',
                                                          ('  ' + ' '.join(x[:40] for x in extra)) if extra else ''), flush=True)
    total = sum(len(l[3]) // 2 for l in lines)
    print('\nrows %d: signature %d, NO SIGNATURE %d; pattern bytes %d; call-target checks %d; second global patterns %d'
          % (len(lines) + len(nosig), len(lines), len(nosig), total,
             sum(l[11].count('k:') for l in lines), sum(l[11].count('g:') for l in lines)))
    if args.report or only is not None:
        return 0 if not nosig else 1
    fp65 = [h for h in _h65 if h.startswith('!fingerprint')][0].split()[1]
    fp68 = [h for h in _h68 if h.startswith('!fingerprint')][0].split()[1]
    out = [
        '# kenshi-coop address SIGNATURES - DATA, NOT CODE. Generated by tools/gen_signatures.py; do not hand-edit.',
        '# Used ONLY when no addresses/<fingerprint>.txt matches the game executable: each row is found by its byte',
        '# pattern in the executable file\'s .text, and every row must be found exactly once or the mod refuses.',
        '# Made from Steam 1.0.65 and Steam 1.0.68 (both tables); proven offline to reproduce both tables exactly',
        '# (tools/prove_signatures.py). A match on any OTHER build is unproven until tested there (owner 99).',
        '# fields: <name> <kind> <anchor> <pattern> <a> <b> <c> <d> <check> <section> <rank> <extra>',
        '#   - see tools/gen_signatures.py',
        '!sigversion 2',
        '!source %s %s' % (fp65, fp68),
        '!count %d' % len(lines),
        '!nosigcount %d' % len(nosig),
    ]
    out += ['!nosig %s' % n for n, _w in nosig]
    out += ['%s %s %d %s %d %d %d %d %s %s %d %s' % l for l in lines]
    with open(OUT, 'w', encoding='ascii', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote %s (%d bytes)' % (OUT, os.path.getsize(OUT)))
    return 0 if not nosig else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe65', default=G.EXE65)
    ap.add_argument('--exe68', default=G.EXE68)
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--only', default='', help='comma-separated row names: report on those rows only, nothing written')
    return generate(ap.parse_args())


if __name__ == '__main__':
    sys.exit(main())
