#!/usr/bin/env python3
"""prove_signatures.py - runs the plugin's signature resolver (src/common/sigtable.cpp, re-implemented here step for
step, independently of the generator) against executables and compares what it produces with the address tables.

READ-ONLY on the executables: each is read as bytes; nothing is written, run or loaded.

    python tools/prove_signatures.py                     both Steam exes against their own tables (the proof)
    python tools/prove_signatures.py EXE [TABLE]         one exe; with TABLE, compared to it row by row

For each row it prints nothing when the resolved line equals the table's line exactly (name, kind, rva, verify_at,
bytes / pointer target / '-'), and names every row that differs or does not resolve. Exit 0 only when every exe
given reproduces its table with 0 differences and 0 unresolved rows.

WHAT THIS PROVES: that on Steam 1.0.65 (the RE_Kenshi-patched file this project runs) and on Steam 1.0.68 the
pattern road gives EXACTLY the tables. It proves nothing about a GOG executable: that claim needs a GOG player's
test (owner 99).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ADDR = os.path.join(os.path.dirname(HERE), 'src', 'coop-plugin', 'addresses')
SIG = os.path.join(ADDR, 'signatures.sig')
PAIRS = [
    (r'C:\code\kenshi-coop\build\bin\kenshi_x64.exe', os.path.join(ADDR, '65D604D7-0232C000-B914BEE3-02303600.txt')),
    (r'C:\Program Files (x86)\Steam\steamapps\common\Kenshi\kenshi_x64_vanilla.exe',
     os.path.join(ADDR, '6602D59D-0232D000-BBA641C8-02304800.txt')),
]
SCN_CODE, SCN_EXEC, SCN_WRITE = 0x20, 0x20000000, 0x80000000


def masked_hex(s):
    pat, fix = bytearray(), []
    for i in range(0, len(s), 2):
        if s[i:i + 2] == '??':
            pat.append(0)
            fix.append(False)
        else:
            pat.append(int(s[i:i + 2], 16))
            fix.append(True)
    return bytes(pat), fix


def parse_sig(path):
    return parse_sig_lines(open(path, encoding='ascii'))


def parse_sig_lines(lines):
    """sigtable.cpp's SigParseFile. A rank repeated within its class (code+ret, var, ro+ptr) is refused, as there."""
    rows, nosig, meta, ranks = [], [], {}, set()
    for line in lines:
        t = line.split()
        if not t or t[0].startswith('#'):
            continue
        if t[0] == '!nosig':
            nosig.append(t[1])
        elif t[0].startswith('!'):
            meta[t[0]] = t[1:]
        else:
            assert len(t) == 12, line
            pat, fix = masked_hex(t[3])
            chk = masked_hex(t[8]) if t[8] != '-' else (b'', [])
            anc = int(t[2])
            assert all(fix[anc:anc + 4]) and len(fix[anc:anc + 4]) == 4, t[0]
            g2, calls = None, []
            for x in (t[11].split(',') if t[11] != '-' else []):
                f = x.split(':')
                if f[0] == 'g':
                    assert len(f) == 6 and t[1] == 'var' and g2 is None, line
                    p2, x2 = masked_hex(f[2])
                    a2 = int(f[1])
                    assert all(x2[a2:a2 + 4]) and len(x2[a2:a2 + 4]) == 4, t[0]
                    g2 = dict(name=t[0], kind='var', anchor=a2, pat=p2, fix=x2, a=int(f[3]), b=int(f[4]), c=int(f[5]))
                else:
                    assert f[0] == 'k' and len(f) == 3, line
                    calls.append((int(f[1]), f[2]))
            assert (g2 is not None) == (t[1] == 'var'), 'a var row without its second pattern: ' + t[0]
            key = (CLASS[t[1]], int(t[10]))
            assert key not in ranks, 'rank %s repeated in its class: %s' % (t[10], t[0])
            ranks.add(key)
            rows.append(dict(name=t[0], kind=t[1], anchor=anc, pat=pat, fix=fix, a=int(t[4]), b=int(t[5]),
                             c=int(t[6]), d=int(t[7]), chk=chk, sec=t[9], rank=int(t[10]), g2=g2, calls=calls))
    assert meta['!sigversion'] == ['2'], 'signatures.sig is not !sigversion 2'
    assert int(meta['!count'][0]) == len(rows) and int(meta['!nosigcount'][0]) == len(nosig)
    return rows, nosig


class Pe(object):
    def __init__(self, path):
        self.buf = open(path, 'rb').read()
        b = self.buf
        e = struct.unpack_from('<I', b, 0x3C)[0]
        assert b[:2] == b'MZ' and b[e:e + 4] == b'PE\0\0'
        nsec, = struct.unpack_from('<H', b, e + 6)
        opt_size, = struct.unpack_from('<H', b, e + 20)
        opt = e + 24
        assert struct.unpack_from('<H', b, opt)[0] == 0x20B
        self.image_base, = struct.unpack_from('<Q', b, opt + 24)
        self.secs = []
        self.text = None
        for i in range(nsec):
            o = opt + opt_size + 40 * i
            name = b[o:o + 8].rstrip(b'\0')
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', b, o + 8)
            chars, = struct.unpack_from('<I', b, o + 36)
            s = dict(name=name, va=va, vsize=vsize, raw=rptr, rsize=rsize, chars=chars)
            if name == b'.text' and self.text is None:
                self.text = s
            self.secs.append(s)

    def at(self, rva, n):
        for s in self.secs:
            rl = s['rsize'] if (s['rsize'] < s['vsize'] or s['vsize'] == 0) else s['vsize']
            if s['va'] <= rva and rva + n <= s['va'] + rl:
                off = s['raw'] + rva - s['va']
                return self.buf[off:off + n] if off + n <= len(self.buf) else None
        return None

    def section_of(self, rva):
        for s in self.secs:
            if s['va'] <= rva < s['va'] + max(s['vsize'], s['rsize']):
                return s
        return None


def fits(got, chk):
    pat, fix = chk
    return got is not None and len(got) == len(pat) and all(not f or got[i] == pat[i] for i, f in enumerate(fix))


def scan(pe, row):
    """All pattern starts (rvas) in .text's raw bytes where every fixed byte agrees - the anchor is found first."""
    t = pe.text
    tlen = t['rsize'] if (t['vsize'] == 0 or t['vsize'] >= t['rsize']) else t['vsize']
    text = pe.buf[t['raw']:t['raw'] + tlen]
    anc = row['anchor']
    key = row['pat'][anc:anc + 4]
    pat, fix = row['pat'], row['fix']
    fixed = [k for k in range(len(pat)) if fix[k]]
    out = []
    i = text.find(key)
    while i >= 0:
        s = i - anc
        if s >= 0 and s + len(pat) <= tlen and all(text[s + k] == pat[k] for k in fixed):
            out.append(t['va'] + s)
        i = text.find(key, i + 1)
    return out


def entry(pe, r, p):
    """(line fields) or raise ValueError(why) - sigtable.cpp's SigRowEntry."""
    t = pe.text
    tend = t['va'] + max(t['vsize'], t['rsize'])
    k = r['kind']
    if k in ('code', 'ret'):
        rva = p + r['a']
        if not (t['va'] <= rva < tend):
            raise ValueError('outside .text')
        if r['sec'] != '.text':
            raise ValueError('a code row whose section is not .text')
        if k == 'code':
            if r['b'] not in (0, 16):
                raise ValueError('verify offset')
            at, n = rva + r['b'], r['c']
        else:
            at, n = rva - r['b'], r['b']
        q = pe.at(at, n) if 1 <= n <= 16 else None
        if q is None:
            raise ValueError('verify bytes not in the file')
        if k == 'ret' and not fits(q, r['chk']):
            raise ValueError('not the expected call')
        return (k, rva, at, q.hex().upper())
    dq = pe.at(p + r['a'], 4)
    if dq is None or not (4 <= r['b'] <= 8):
        raise ValueError('displacement')
    rva = p + r['a'] + r['b'] + struct.unpack('<i', dq)[0] + r['c']
    s = pe.section_of(rva)
    if s is None:
        raise ValueError('outside every section')
    if s['name'].decode('ascii') != r['sec']:
        raise ValueError('resolved into section %s, the row belongs in %s' % (s['name'].decode('ascii'), r['sec']))
    if k == 'var':
        if not s['chars'] & SCN_WRITE:
            raise ValueError('var in a read-only section')
        return (k, rva, rva, '-')
    if s['chars'] & (SCN_WRITE | SCN_EXEC | SCN_CODE):
        raise ValueError('ro/ptr in a writable or code section')
    if k == 'ro':
        at = rva + r['d']
        q = pe.at(at, len(r['chk'][0]))
        if not r['chk'][0] or not fits(q, r['chk']):
            raise ValueError('constant bytes differ')
        return (k, rva, at, q.hex().upper())
    if k == 'ptr':
        slot = rva + r['d']
        q = pe.at(slot, 8)
        if q is None:
            raise ValueError('slot not in the file')
        v, = struct.unpack('<Q', q)
        if v < pe.image_base or v - pe.image_base >= 0xFFFFFFFF:
            raise ValueError('slot does not point into the image')
        ptr = v - pe.image_base
        f = ptr
        for _ in range(4):
            jq = pe.at(f, 5)
            if jq is None or jq[0] != 0xE9:
                break
            f = f + 5 + struct.unpack('<i', jq[1:5])[0]
        if not (t['va'] <= f < tend) or not fits(pe.at(f, len(r['chk'][0])), r['chk']):
            raise ValueError('slot target is not the expected code')
        return (k, rva, slot, '%08X' % ptr)
    raise ValueError('unknown kind')


def read_table(path):
    out = {}
    for line in open(path, encoding='ascii'):
        t = line.split()
        if not t or t[0][0] in '#!':
            continue
        out[t[0]] = (t[1], int(t[2], 16), int(t[3], 16), t[4])
    return out


CLASS = {'code': 'C', 'ret': 'C', 'var': 'G', 'ro': 'R', 'ptr': 'R'}


def cross_checks(pe, rows, got, starts):
    """sigtable.cpp's SigCrossCheck: (a) rank order within each class, (b) no two rows at one address, (c) every var
    row's second referencing instruction agrees, (d) every recorded call lands on its row. -> (fails, inversions)"""
    fails, inv = [], {}
    by = dict((r['name'], r) for r in rows)
    for cl in 'CGR':
        seq = sorted((r for r in rows if CLASS[r['kind']] == cl and r['name'] in got), key=lambda r: r['rank'])
        inv[cl] = 0
        for x, y in zip(seq, seq[1:]):
            if x['rank'] == y['rank'] or got[x['name']][1] >= got[y['name']][1]:   # a tie refuses, as in sigtable.cpp
                inv[cl] += 1
                fails.append('ORDER: %s (rank %d, %08X) is not below %s (rank %d, %08X)'
                             % (x['name'], x['rank'], got[x['name']][1], y['name'], y['rank'], got[y['name']][1]))
    seen = {}
    for n, g in got.items():
        seen.setdefault(g[1], []).append(n)
    fails += ['DISTINCT: %s resolve to the same address %08X' % (' and '.join(sorted(v)), k)
              for k, v in seen.items() if len(v) > 1]
    for r in rows:
        n = r['name']
        if n not in got:
            continue
        if r['g2'] is not None:
            m = scan(pe, r['g2'])
            if len(m) != 1:
                fails.append('GLOBAL TWICE: %s second pattern matches %d times' % (n, len(m)))
            else:
                ins2 = m[0] + r['g2']['a']
                dq = pe.at(ins2, 4)
                t2 = (ins2 + r['g2']['b'] + struct.unpack('<i', dq)[0] + r['g2']['c']) if dq else -1
                if ins2 == starts[n] + r['a']:
                    fails.append('GLOBAL TWICE: %s second reference is the first one' % n)
                elif t2 != got[n][1]:
                    fails.append('GLOBAL TWICE: %s second reference resolves to %X, the first to %08X' % (n, t2, got[n][1]))
        for off, target in r['calls']:
            at = starts[n] + off
            q = pe.at(at, 5)
            if q is None or q[0] != 0xE8 or target not in got or by[target]['kind'] != 'code':
                fails.append('CALL: %s +%d is not a call to a resolved code row %s' % (n, off, target))
                continue
            f = at + 5 + struct.unpack('<i', q[1:5])[0]
            for _ in range(4):
                jq = pe.at(f, 5)
                if jq is None or jq[0] != 0xE9:
                    break
                f = f + 5 + struct.unpack('<i', jq[1:5])[0]
            if f != got[target][1]:
                fails.append('CALL: %s +%d lands on %08X, not on %s %08X' % (n, off, f, target, got[target][1]))
    return fails, inv


def prove(exe, table, rows, nosig):
    pe = Pe(exe)
    tab = read_table(table) if table else None
    got, unresolved, differ, starts = {}, [], [], {}
    for r in rows:
        m = scan(pe, r)
        if len(m) != 1:
            unresolved.append('%s: %d matches' % (r['name'], len(m)))
            continue
        try:
            got[r['name']] = entry(pe, r, m[0])
            starts[r['name']] = m[0]
        except ValueError as e:
            unresolved.append('%s: %s' % (r['name'], e))
    unresolved += ['%s: no signature' % n for n in nosig]
    cross, inv = cross_checks(pe, rows, got, starts)
    matched = 0
    if tab is not None:
        for name, want in sorted(tab.items()):
            g = got.get(name)
            if g is None:
                continue
            if g == want:
                matched += 1
            else:
                differ.append('%s: table %s %08X %08X %s, resolver %s %08X %08X %s' % ((name,) + want + g))
        extra = sorted(set(got) - set(tab))
        differ += ['%s: resolved but not in the table' % n for n in extra]
    print('%s' % exe)
    print('  table %s' % (os.path.basename(table) if table else '(none)'))
    print('  rows %d: matched %d, differing %d, unresolved %d, nosig %d' % (len(tab) if tab else len(rows) + len(nosig),
                                                                            matched, len(differ), len(unresolved), len(nosig)))
    print('  cross-checks: order inversions code+ret %d, globals %d, ro+ptr %d; failures %d (order, distinct,'
          ' %d second global references, %d call targets)' % (inv['C'], inv['G'], inv['R'], len(cross),
                                                            sum(1 for r in rows if r['g2']), sum(len(r['calls']) for r in rows)))
    for x in differ:
        print('  DIFFERS ' + x)
    for x in unresolved:
        print('  UNRESOLVED ' + x)
    for x in cross:
        print('  CROSS-CHECK ' + x)
    return 0 if (not differ and not unresolved and not cross and not nosig and (tab is None or matched == len(tab))) else 1


def selftest():
    """gog1 fold 2: the prover refuses a rank tie exactly as the plugin does (test_main.cpp holds the plugin's side).
    Returns '' or what went wrong."""
    head = ['!sigversion 2', '!count 2', '!nosigcount 0']
    a = 'A code 0 40534883EC20C3 0 0 7 0 - .text 0 -'
    b = 'B code 0 48895C2408574883 0 0 8 0 - .text %d -'
    rows, _ = parse_sig_lines(head + [a, b % 1])
    got = {'A': ('code', 0x1100, 0x1100, ''), 'B': ('code', 0x1200, 0x1200, '')}
    if len(rows) != 2:
        return 'the good two-row file did not read'
    fails, _ = cross_checks(None, rows, got, {})
    if fails:
        return 'the good pair failed a cross-check: ' + fails[0]
    try:
        parse_sig_lines(head + [a, b % 0])
        return 'the reader accepted a rank repeated within its class'
    except AssertionError:
        pass
    rows[1]['rank'] = 0   # a tie the reader would have refused, handed straight to ORDER
    fails, _ = cross_checks(None, rows, got, {})
    if not any(x.startswith('ORDER') for x in fails):
        return 'ORDER passed a rank tie'
    return ''


def main():
    why = selftest()
    print('selftest: %s' % (why or 'PASS - a rank tie is refused by the reader and by ORDER, as in the plugin'))
    if why:
        print('PROOF: FAIL')
        return 1
    rows, nosig = parse_sig(SIG)
    print('signatures.sig: %d rows, %d without a signature, %d bytes' % (len(rows), len(nosig), os.path.getsize(SIG)))
    pairs = PAIRS if len(sys.argv) < 2 else [(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)]
    bad = 0
    for exe, table in pairs:
        bad |= prove(exe, table, rows, nosig)
    print('PROOF: %s' % ('PASS - every table reproduced exactly' if not bad else 'FAIL'))
    return bad


if __name__ == '__main__':
    sys.exit(main())
