#!/usr/bin/env python3
"""gen_table_1068.py - the Steam 1.0.68 address table, generated from the 1.0.65 one (mig7, T-63, owner decision 99).

OFFLINE AND READ-ONLY on both game executables: they are opened for reading, mapped into memory here, and never
written, moved or copied. Nothing is run, loaded or hooked.

    python tools/gen_table_1068.py                 generate + report; writes addresses/<1.0.68 fingerprint>.txt
    python tools/gen_table_1068.py --report-only   the per-row report, no table written
    python tools/gen_table_1068.py --reverse --rows NAME[,NAME...]
                                                   T-373: the SAME matcher run 1.0.68 -> 1.0.65 for named NEW rows of
                                                   the 1.0.68 table; prints each one's 1.0.65 line, writes nothing
                                                   (.modding/01-environment.md, 'Address source').
    python tools/gen_table_1068.py --check TABLE EXE
                                                   the offline load check: what AddrInit would do with TABLE in EXE
                                                   (fingerprint, !count, every code/ret/ro row's bytes, every ptr row's
                                                   pointer, every var row inside a writable section). Exit 0 = 100%.

HOW EACH ROW IS FOUND IN 1.0.68 (the method of study 43 section 3, made repeatable):
  code  the 1.0.65 code from the row's address to the end of its function (the exception table's end, or the first
        ret/jmp followed by int3 padding) is disassembled and every byte that only encodes "distance to something
        else" is blanked: rel32 call/jump targets, RIP-relative displacements, image-relative displacements (switch
        tables) and 64-bit immediates pointing into the image. That masked signature is searched in 1.0.68's .text,
        and grown - through the whole function, then into the code after it and the code before it - until exactly
        one place agrees. Categories:
          same        exactly one place agrees with the WHOLE function (code unchanged, only moved)
          different   no place agrees with the whole function; the best partial agreement is reported for review
          not-found   not even the signature's first bytes occur; neighbour-mapped suggestions are reported
          ambiguous   several places agree with the whole function and all the context the tool can add
  ret   the function containing the CALL is matched as above (it must be 'same'); the return address keeps its
        offset in it, and a direct call's target must be the 1.0.68 match of the 1.0.65 callee.
  var / ro / ptr
        every 1.0.65 instruction that addresses the row RIP-relatively is found; each one's function is matched
        ('same' only) and the SAME instruction's target is read in 1.0.68. All such votes must agree. A ptr row's
        vtable slot keeps its offset, and the pointer it holds must lead (through its jump stub) to the 1.0.68
        match of the 1.0.65 target. A ro row's own bytes must be equal in both builds.

A row the tool cannot resolve as 'same' is taken ONLY from HAND below, and every HAND entry is a decision written
with its evidence in .modding/investigations/table-1068.md. A HAND entry is itself checked: it must equal the
tool's own best candidate or neighbour suggestion, so a typo cannot ship.

The check columns are always read from the 1.0.68 executable, never copied. The +16 rule is kept: a code row
whose verify column is rva+16 in 1.0.65 (GameWorld_setupRecords, RE_Kenshi hooks its first bytes) is
verify rva+16 in 1.0.68 too.

GOG: the masked signatures built here are the byte patterns a no-table lookup would need (see table-1068.md).

Needs: Python 3, capstone 5 and numpy (both only for reading). C++ is not involved.
"""
import argparse
import bisect
import os
import re
import struct
import sys
import zlib

import capstone
import numpy as np
from capstone import x86

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDR_DIR = os.path.join(ROOT, 'src', 'coop-plugin', 'addresses')
TABLE65 = os.path.join(ADDR_DIR, '65D604D7-0232C000-B914BEE3-02303600.txt')
TABLE68 = os.path.join(ADDR_DIR, '6602D59D-0232D000-BBA641C8-02304800.txt')
EXE65 = r'C:\code\kenshi-coop\build\bin\kenshi_x64.exe'
EXE68 = r'C:\Program Files (x86)\Steam\steamapps\common\Kenshi\kenshi_x64_vanilla.exe'
VERSION68 = 'Steam_1.0.68'

# ---- THE HAND-DECIDED ROWS. name -> 1.0.68 rva. Each one is argued in .modding/investigations/table-1068.md. ----
HAND = {
    # GameWorld::processKeys: changed in 1.0.68 (new calls in its key handling); the mod only uses it as the start
    # of a return-address window, and its setGameSpeed x3 / userPause x1 calls sit at the same offsets.
    'ProcessKeys': 0x788100,
}

# =================================================================================================================
# the image
# =================================================================================================================
SCN_WRITE = 0x80000000


class Image(object):
    """A PE32+ file mapped the way the loader maps it (sections at their RVAs), from a read-only open."""

    def __init__(self, path):
        with open(path, 'rb') as f:
            raw = f.read()
        self.path = path
        self.raw = raw
        e = struct.unpack_from('<I', raw, 0x3C)[0]
        assert raw[:2] == b'MZ' and raw[e:e + 4] == b'PE\0\0', path
        nsec = struct.unpack_from('<H', raw, e + 6)[0]
        self.tds = struct.unpack_from('<I', raw, e + 8)[0]
        opt = e + 24
        optsize = struct.unpack_from('<H', raw, e + 20)[0]
        assert struct.unpack_from('<H', raw, opt)[0] == 0x20B, 'not PE32+'
        self.imagebase = struct.unpack_from('<Q', raw, opt + 24)[0]
        self.soi = struct.unpack_from('<I', raw, opt + 56)[0]
        ddir = opt + 112
        exc_rva, exc_size = struct.unpack_from('<II', raw, ddir + 3 * 8)
        self.mm = bytearray(self.soi)
        hdr_size = struct.unpack_from('<I', raw, opt + 60)[0]
        self.mm[:hdr_size] = raw[:hdr_size]
        self.sections = []
        sh = opt + optsize
        for i in range(nsec):
            o = sh + 40 * i
            name = raw[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', raw, o + 8)
            chars = struct.unpack_from('<I', raw, o + 36)[0]
            n = min(vsize, rsize) if vsize else rsize
            self.mm[va:va + n] = raw[rptr:rptr + n]
            self.sections.append((name, va, max(vsize, rsize), chars))
        self.mm = bytes(self.mm)
        t = [s for s in self.sections if s[0] == '.text'][0]
        self.text = (t[1], t[1] + t[2])
        self.pdata = []
        for o in range(exc_rva, exc_rva + exc_size, 12):
            b, en, _u = struct.unpack_from('<III', self.mm, o)
            if b:
                self.pdata.append((b, en))
        self.pdata.sort()
        self.pbeg = [p[0] for p in self.pdata]
        self.pbegset = set(self.pbeg)
        self.np = np.frombuffer(self.mm, dtype=np.uint8)

    def fingerprint(self):
        crc = zlib.crc32(self.raw[:65536]) & 0xFFFFFFFF
        return '%08X-%08X-%08X-%08X' % (self.tds, self.soi, crc, len(self.raw) & 0xFFFFFFFF)

    def rd(self, rva, n):
        return self.mm[rva:rva + n]

    def q(self, rva):
        return struct.unpack_from('<Q', self.mm, rva)[0]

    def section_of(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + s[2]:
                return s
        return None

    def fragment(self, rva):
        """(begin, end) of the exception-table fragment containing rva, or None."""
        i = bisect.bisect_right(self.pbeg, rva) - 1
        if i >= 0 and self.pdata[i][0] <= rva < self.pdata[i][1]:
            return self.pdata[i]
        return None


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True


def rip_target(ins):
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
            return ins.address + ins.size + op.mem.disp
    return None


def disp_pos(ins):
    """Offset of a 32-bit displacement inside ins. It is always followed only by the immediate, if any - computed
    from the end because capstone's disp_offset is wrong for some prefixed SSE forms (66 0F 6E, 66 0F 2F, ...)."""
    return ins.size - ins.imm_size - 4


def img_disp(img, ins):
    """The image-relative disp32 of a [base + index*s + RVA] operand (a switch table), or None."""
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.base != x86.X86_REG_RIP and ins.disp_size == 4:
            d = op.mem.disp & 0xFFFFFFFF
            if 0x10000 <= d < img.soi:
                return d
    return None


class Masked(object):
    __slots__ = ('sig', 'keep', 'insns', 'tables')

    def __iter__(self):                       # (sig, keep, insns) unpacking, as before
        return iter((self.sig, self.keep, self.insns))


def masked(img, start, end, stop_at_ret=False):
    """Masked bytes and keep-flags of the code [start, end).
    Blank: rel32 branch targets, RIP-relative and image-relative displacements, 64-bit immediates into the image, and
    the entries of switch jump tables (RVAs of case labels; kept aside in .tables to be compared RELATIVE to the
    function). Switch byte-index tables are kept and compared exactly. Decoding skips over the tables, stops at
    undecodable bytes, or (stop_at_ret) at a ret/jmp followed by int3 padding.
    -> Masked(sig, keep, insns=[(offset, insn)], tables=[(offset, count)])"""
    code = img.rd(start, end - start)
    n = len(code)
    keep = np.ones(n, dtype=bool)
    insns = []
    dtabs = {}                                # offset -> 'd' (dword RVA table) or 'b' (byte index table)
    tables = []
    pos = 0
    stop = n
    maxcases = 0
    while pos < n:
        if pos in dtabs:
            if dtabs[pos] == 'd':
                c = 0
                while pos + 4 * (c + 1) <= n:
                    v = struct.unpack_from('<I', code, pos + 4 * c)[0]
                    if not (img.text[0] <= v < img.text[1] and abs(v - start) < 0x40000):
                        break
                    c += 1
                keep[pos:pos + 4 * c] = False
                tables.append((pos, c))
                maxcases = max(maxcases, c)
                pos += 4 * c
            else:
                c = 0
                while pos + c < n and code[pos + c] < max(maxcases, 1) and c < 1024 and (c == 0 or (pos + c) not in dtabs):
                    c += 1
                pos += c
            if c == 0:
                stop = pos
                break
            continue
        nxt = min([t for t in dtabs if t > pos] or [n])
        advanced = False
        done = False
        rescan = False
        for ins in MD.disasm(code[pos:nxt], start + pos):
            advanced = True
            off = ins.address - start
            k = keep[off:off + ins.size]
            rip = rip_target(ins)
            imd = img_disp(img, ins)
            if rip is not None or imd is not None:
                dp = disp_pos(ins)
                k[dp:dp + 4] = False
            if imd is not None and start <= imd < end and imd - start > off:
                is_byte = ins.mnemonic.startswith('movzx') and 'byte ptr' in ins.op_str
                dtabs.setdefault(imd - start, 'b' if is_byte else 'd')
            if ins.imm_size == 4 and (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)):
                k[ins.size - 4:ins.size] = False
            if ins.imm_size == 8:
                for op in ins.operands:
                    if op.type == x86.X86_OP_IMM and img.imagebase <= (op.imm & 0xFFFFFFFFFFFFFFFF) < img.imagebase + img.soi:
                        k[ins.size - 8:ins.size] = False
            insns.append((off, ins))
            pos = off + ins.size
            if stop_at_ret and ins.mnemonic in ('ret', 'jmp') and img.mm[ins.address + ins.size] == 0xCC:
                done = True
                break
            nt = min([t for t in dtabs if t >= pos] or [n])
            if nt < nxt:                      # a table discovered just now starts before our window ends
                rescan = True
                break
        if done or not advanced:
            stop = pos
            break
        if pos < nxt and not rescan:          # the decoder gave up inside the window: undecodable bytes
            stop = pos
            break
    m = Masked()
    m.sig = bytes(code[:stop])
    m.keep = keep[:stop]
    m.insns = insns
    m.tables = [t for t in tables if t[0] + 4 * t[1] <= stop]
    return m


def body_end(img, rva):
    fr = img.fragment(rva)
    if fr is not None:
        return fr[1], False
    return rva + 0x1000, True


def agree(img, cand, sig, keep):
    """Number of leading signature bytes that agree at cand (masked bytes are free)."""
    n = len(sig)
    if cand < 0 or cand + n > len(img.mm):
        n = max(0, len(img.mm) - cand)
    if n == 0:
        return 0
    a = np.frombuffer(sig[:n], dtype=np.uint8)
    b = img.np[cand:cand + n]
    bad = (a != b) & keep[:n]
    idx = np.flatnonzero(bad)
    return int(idx[0]) if len(idx) else n


def agree_back(img, cand_end, sig, keep):
    """Number of trailing signature bytes that agree ending at cand_end."""
    n = len(sig)
    if n == 0 or cand_end - n < 0:
        return 0
    a = np.frombuffer(sig, dtype=np.uint8)
    b = img.np[cand_end - n:cand_end]
    bad = ((a != b) & keep)[::-1]
    idx = np.flatnonzero(bad)
    return int(idx[0]) if len(idx) else n


def regex_of(sig, keep, n):
    return re.compile(b''.join(re.escape(sig[i:i + 1]) if keep[i] else b'.' for i in range(min(n, len(sig)))), re.S)


class Matcher(object):
    def __init__(self, a, b):
        self.a, self.b = a, b
        self.cache = {}
        self.samecache = {}
        self.textb = b.mm[b.text[0]:b.text[1]]

    def match(self, rva):
        """-> dict(status, rva68, cands, detail, sig, keep)"""
        if rva in self.cache:
            return self.cache[rva]
        a, b = self.a, self.b
        end, linear = body_end(a, rva)
        mk = masked(a, rva, end, stop_at_ret=linear)
        sig, keep = mk.sig, mk.keep
        r = {'status': 'not-found', 'rva68': None, 'cands': [], 'detail': '', 'sig': sig, 'keep': keep,
             'context': False, 'tables': mk.tables}
        if len(sig) == 0:
            r['detail'] = 'no decodable code at the 1.0.65 address'
            self.cache[rva] = r
            return r
        pat = regex_of(sig, keep, 48)
        cands = [m.start() + b.text[0] for m in pat.finditer(self.textb)]
        if not cands:
            # the first 48 bytes do not occur: try 16 (a changed function often keeps its prologue)
            pat = regex_of(sig, keep, 16)
            cands = [m.start() + b.text[0] for m in pat.finditer(self.textb)]
        if not cands:
            r['detail'] = 'the first %d signature bytes occur nowhere in 1.0.68 .text' % min(16, len(sig))
            self.cache[rva] = r
            return r
        scored = sorted(((agree(b, c, sig, keep), c) for c in cands), reverse=True)
        full = [c for s, c in scored if s >= len(sig)]
        if len(full) == 1:
            r.update(status='same', rva68=full[0], detail='whole function (%d bytes) agrees at exactly one place' % len(sig))
        elif len(full) > 1:
            # grow into the code after the function, then the code before it
            fsig, fkeep, _ = masked(a, rva, rva + len(sig) + 0x200)
            bstart = rva - 0x100
            frb = a.fragment(rva - 1)
            if frb is not None and frb[0] >= rva - 0x400:
                bstart = frb[0]
            bsig, bkeep, _ = masked(a, bstart, rva)
            if bstart + len(bsig) != rva:            # the decode did not land on rva: no backward context
                bsig, bkeep = b'', np.array([], dtype=bool)
            sc = sorted(((agree(b, c, fsig, fkeep) + agree_back(b, c, bsig, bkeep), c) for c in full), reverse=True)
            if sc[0][0] > sc[1][0]:
                r.update(status='same', rva68=sc[0][1], context=True,
                         detail='whole function (%d bytes) agrees at %d places; neighbour context picks one '
                                '(%d context bytes, next best %d)' % (len(sig), len(full), sc[0][0], sc[1][0]))
            else:
                r.update(status='ambiguous', cands=[c for _s, c in sc[:8]],
                         detail='whole function (%d bytes) and its context agree at %d places' % (len(sig), len(full)))
        else:
            best = scored[0]
            second = scored[1][0] if len(scored) > 1 else -1
            r.update(status='different', cands=[c for _s, c in scored[:4]],
                     detail='no place agrees with the whole function (%d bytes); best agreement %d bytes at %X '
                            '(next best %d)' % (len(sig), best[0], best[1], second))
            r['best'] = best[1] if best[0] > second else None
        if r['status'] == 'same' and mk.tables:
            # the switch tables: each case label must sit at the same offset from the function in both builds
            c68 = r['rva68']
            for off, cnt in mk.tables:
                e65 = struct.unpack_from('<%dI' % cnt, a.mm, rva + off)
                e68 = struct.unpack_from('<%dI' % cnt, b.mm, c68 + off)
                if [x - rva for x in e65] != [x - c68 for x in e68]:
                    r.update(status='different', rva68=None, cands=[c68], best=c68,
                             detail=r['detail'] + '; BUT the switch table at +%X (%d cases) differs' % (off, cnt))
                    break
            else:
                r['detail'] += '; its %d switch table(s) (%s cases) agree relative to the function' % (
                    len(mk.tables), '+'.join(str(t[1]) for t in mk.tables))
        self.cache[rva] = r
        return r

    def same_code_at(self, f65, f68):
        """Does the 1.0.65 function f65's whole masked body agree at 1.0.68 f68? (a check, not a search)"""
        key = (f65, f68)
        if key not in self.samecache:
            end, linear = body_end(self.a, f65)
            mk = masked(self.a, f65, end, stop_at_ret=linear)
            self.samecache[key] = len(mk.sig) > 0 and agree(self.b, f68, mk.sig, mk.keep) >= len(mk.sig)
        return self.samecache[key]

    def callees(self, rva, rva68):
        """Every direct call/jump out of the function: the 1.0.68 instruction at the same offset must lead (through
        its jump stub) to code that agrees with the 1.0.65 callee. -> (count, [differing (t65, t68)])"""
        end, linear = body_end(self.a, rva)
        mk = masked(self.a, rva, end, stop_at_ret=linear)
        n, bad = 0, []
        seen = set()
        for off, ins in mk.insns:
            if ins.imm_size != 4 or ins.bytes[0] not in (0xE8, 0xE9):
                continue
            t65 = ins.operands[0].imm
            if rva <= t65 < rva + len(mk.sig):
                continue
            i68 = next(MD.disasm(self.b.rd(rva68 + off, 16), rva68 + off))
            t68 = i68.operands[0].imm
            f65, f68 = final_target(self.a, t65), final_target(self.b, t68)
            if (f65, f68) in seen:
                continue
            seen.add((f65, f68))
            n += 1
            if not self.same_code_at(f65, f68):
                bad.append((f65, f68))
        return n, bad

    def match_checked(self, rva):
        """match(), and a match picked only by neighbour context must ALSO be where the nearest uniquely matched
        functions before and after it say it is - otherwise it is reported ambiguous."""
        m = self.match(rva)
        if m['status'] == 'same' and m['context']:
            nb = self.neighbours(rva)
            if not nb or any(x[2] != m['rva68'] for x in nb):
                m = dict(m, status='ambiguous', cands=[m['rva68']], rva68=None,
                         detail=m['detail'] + '; the neighbours say ' + ', '.join('%X' % x[2] for x in nb))
            else:
                m = dict(m, detail=m['detail'] + '; the neighbours before and after agree (%s)' % ', '.join(
                    '%X' % x[2] for x in nb))
        return m

    def neighbours(self, rva, span=12):
        """Suggested 1.0.68 addresses from the nearest exception-table functions before/after that match 'same'."""
        a = self.a
        i = bisect.bisect_right(a.pbeg, rva) - 1
        out = []
        for step, rng in ((-1, range(i, max(-1, i - span), -1)), (1, range(i + 1, min(len(a.pbeg), i + 1 + span)))):
            for j in rng:
                f = a.pbeg[j]
                if f == rva:
                    continue
                m = self.match(f)
                if m['status'] == 'same' and not m['context']:
                    out.append((f, m['rva68'], rva + (m['rva68'] - f)))
                    break
        return out


# =================================================================================================================
# data references
# =================================================================================================================
class Xrefs(object):
    """Every RIP-relative operand in 1.0.65 .text, found by arithmetic on all 4-byte windows, confirmed by decoding."""

    def __init__(self, img):
        self.img = img
        lo, hi = img.text
        self.lo = lo
        buf = img.mm[lo:hi]
        self.views = []
        for al in range(4):
            n = (len(buf) - al) // 4
            d = np.frombuffer(buf, dtype='<i4', count=n, offset=al).astype(np.int64)
            pos = lo + al + 4 * np.arange(n, dtype=np.int64)
            self.views.append(pos + 4 + d)       # target if the displacement is the instruction's last field

    def find(self, target, limit=24):
        img = self.img
        out = []
        seen = set()
        for al, v in enumerate(self.views):          # the displacement's position is lo + al + 4*h
            for extra in (0, 1, 2, 4):              # the immediate that follows the displacement, if any
                for h in np.flatnonzero(v + extra == target):
                    p = self.lo + al + 4 * int(h)
                    fr = img.fragment(p)
                    if fr is None:
                        # a leaf function has no exception-table entry: decode the one instruction that holds the
                        # displacement and let it (and the code after it) be the signature
                        for k in range(1, 12):
                            ins = next(MD.disasm(img.rd(p - k, 16), p - k), None)
                            if ins is not None and disp_pos(ins) == k and rip_target(ins) == target                                     and ins.address not in seen:
                                out.append((ins.address, ins.address))
                                seen.add(ins.address)
                                break
                        if len(out) >= limit:
                            return out
                        continue
                    if fr[0] in seen:
                        continue
                    _s, _k, insns = masked(img, fr[0], fr[1])
                    for off, ins in insns:
                        if ins.address < p < ins.address + ins.size and ins.address + disp_pos(ins) == p \
                                and rip_target(ins) == target:
                            out.append((fr[0], ins.address))
                            seen.add(fr[0])
                            break
                    if len(out) >= limit:
                        return out
        return out


def resolve_data(M, X, target65):
    """-> (status, rva68, detail). Votes from each 'same'-matched function referencing target65."""
    a, b = M.a, M.b
    refs = X.find(target65)
    votes = {}
    used = 0
    for f65, ins65 in refs:
        m = M.match_checked(f65)
        if m['status'] != 'same':
            continue
        i68 = m['rva68'] + (ins65 - f65)
        ins = next(MD.disasm(b.rd(i68, 16), i68), None)
        if ins is None:
            continue
        t = rip_target(ins)
        if t is None:
            continue
        votes.setdefault(t, []).append(f65)
        used += 1
    if not refs:
        return 'not-found', None, 'no RIP-relative reference to it in 1.0.65 .text'
    if not votes:
        return 'not-found', None, '%d referencing functions in 1.0.65, none of them matched unchanged in 1.0.68' % len(refs)
    if len(votes) > 1:
        return 'ambiguous', None, 'referencing functions disagree: ' + ', '.join(
            '%X x%d' % (t, len(v)) for t, v in votes.items())
    t = list(votes)[0]
    return 'same', t, '%d of %d referencing functions matched unchanged, all name %X' % (used, len(refs), t)


def final_target(img, rva, hops=4):
    for _ in range(hops):
        if img.mm[rva] == 0xE9:
            rva = rva + 5 + struct.unpack_from('<i', img.mm, rva + 1)[0]
        else:
            break
    return rva


# =================================================================================================================
# the table
# =================================================================================================================
def read_table(path):
    rows = []
    header = []
    for line in open(path, 'r', encoding='ascii'):
        s = line.rstrip('\r\n')
        t = s.strip()
        if not t or t[0] == '#' or t[0] == '!':
            header.append(s)
            continue
        f = t.split()
        rows.append({'name': f[0], 'kind': f[1], 'rva': int(f[2], 16), 'at': int(f[3], 16), 'bytes': f[4]})
    return header, rows


def fmt_row(name, kind, rva, at, bytes_field):
    return '%-46s %-4s %08X %08X %s' % (name, kind, rva, at, bytes_field)


def resolve_row(M, X, row):
    """-> dict(status, rva68, at68, field, detail, hint)."""
    a, b = M.a, M.b
    k, rva, at = row['kind'], row['rva'], row['at']
    res = {'status': 'not-found', 'rva68': None, 'detail': '', 'hint': ''}
    if k == 'code':
        m = M.match_checked(rva)
        res.update(status=m['status'], rva68=m['rva68'], detail=m['detail'])
        if m['status'] == 'same':
            n, bad = M.callees(rva, m['rva68'])
            if bad:
                res['note'] = 'callee changed: ' + ', '.join('%X->%X' % x for x in bad)
                res['detail'] += '; %d direct callees, %d of them CHANGED in 1.0.68 (%s)' % (
                    n, len(bad), ', '.join('%X->%X' % x for x in bad))
            else:
                res['detail'] += '; its %d direct callees agree' % n
        if m['status'] != 'same':
            nb = M.neighbours(rva)
            res['hint'] = '; '.join('neighbour %X->%X suggests %X' % x for x in nb)
            res['cands'] = m.get('cands', [])
            res['best'] = m.get('best')
            res['suggest'] = [x[2] for x in nb]
    elif k == 'ret':
        n = len(row['bytes']) // 2
        call65 = rva - n
        fr = a.fragment(call65)
        if fr is None:
            res['detail'] = 'the call is in no exception-table function'
            return res
        m = M.match_checked(fr[0])
        if m['status'] != 'same':
            res.update(status='different' if m['status'] == 'different' else m['status'],
                       detail='its function %X is %s in 1.0.68: %s' % (fr[0], m['status'], m['detail']))
            nb = M.neighbours(fr[0])
            res['hint'] = '; '.join('neighbour %X->%X suggests function %X' % x for x in nb)
            res['func65'] = fr[0]
            res['func68s'] = ([m['rva68']] if m['rva68'] else []) + m.get('cands', []) + [x[2] for x in nb]
            return res
        rva68 = m['rva68'] + (rva - fr[0])
        c65 = next(MD.disasm(a.rd(call65, 16), call65))
        c68 = next(MD.disasm(b.rd(rva68 - n, 16), rva68 - n))
        ok = c68.size == n and c68.mnemonic == c65.mnemonic == 'call'
        det = 'its function %X matched unchanged at %X; the call keeps offset %X' % (fr[0], m['rva68'], call65 - fr[0])
        if ok and a.mm[call65] == 0xE8:
            t65 = c65.operands[0].imm
            t68 = c68.operands[0].imm
            mt = M.match_checked(t65)
            if mt['status'] == 'same' and mt['rva68'] == t68:
                det += '; the callee %X is the 1.0.68 match %X' % (t65, t68)
            elif final_target(a, t65) != t65 and M.match_checked(final_target(a, t65))['rva68'] == final_target(b, t68):
                det += '; the callee stub %X leads to the 1.0.68 match %X' % (t65, final_target(b, t68))
            else:
                ok = False
                det += '; the callee %X does NOT map to %X (%s)' % (t65, t68, mt['status'])
        elif ok:
            ok = c65.bytes == c68.bytes
            det += '; the indirect call is byte-identical' if ok else '; the indirect call differs'
        res.update(status='same' if ok else 'different', rva68=rva68 if ok else None, detail=det)
    elif k in ('var', 'ro'):
        st, t, det = resolve_data(M, X, rva)
        res.update(status=st, rva68=t, detail=det)
        if st != 'same' and k == 'ro':
            # no code path to it: where do the row's own constant bytes occur in 1.0.68's read-only data?
            want = bytes.fromhex(row['bytes'])
            sec = b.section_of(rva + 0x1000) or b.section_of(rva)
            lo, hi = sec[1], sec[1] + sec[2]
            hits = [m.start() + lo for m in re.finditer(re.escape(want), b.mm[lo:hi])]
            res['cands'] = [h - (at - rva) for h in hits]
            res['detail'] += '; its %d constant bytes occur %d time(s) in 1.0.68 %s' % (len(want), len(hits), sec[0])
        if st == 'same' and k == 'ro':
            n = len(row['bytes']) // 2
            same = a.rd(rva, n) == b.rd(t, n)
            eq = sum(1 for i in range(n) if a.mm[rva + i] == b.mm[t + i])
            if not same:
                res.update(status='different', detail=det + '; only %d of the %d constant bytes are equal' % (eq, n))
            else:
                res['detail'] = det + '; all %d constant bytes equal' % n
    elif k == 'ptr':
        st, t, det = resolve_data(M, X, rva)
        res.update(status=st, detail=det)
        if st == 'same':
            slot68 = t + (at - rva)
            p65 = a.q(at) - a.imagebase
            p68 = b.q(slot68) - b.imagebase
            f65, f68 = final_target(a, p65), final_target(b, p68)
            mf = M.match_checked(f65)
            if mf['status'] == 'same' and mf['rva68'] == f68:
                res.update(rva68=t, at68=slot68, ptr68=p68,
                           detail=det + '; slot +%X holds %X, which leads to %X = the 1.0.68 match of %X'
                           % (at - rva, p68, f68, f65))
            else:
                res.update(status='different', detail=det + '; slot +%X holds %X -> %X, but %X matches %s %s'
                           % (at - rva, p68, f68, f65, mf['status'], mf['rva68']))
    return res


def row68(b, row, rva68, res):
    """The 1.0.68 table line for a row whose 1.0.68 address is known."""
    k = row['kind']
    if k == 'code':
        at68 = rva68 + (row['at'] - row['rva'])            # the +16 rule survives
        n = len(row['bytes']) // 2                         # and so does the row's own byte count (UnloadedUpdate: 15)
        return fmt_row(row['name'], k, rva68, at68, b.rd(at68, n).hex().upper())
    if k == 'ret':
        n = len(row['bytes']) // 2
        return fmt_row(row['name'], k, rva68, rva68 - n, b.rd(rva68 - n, n).hex().upper())
    if k == 'ro':
        at68 = rva68 + (row['at'] - row['rva'])
        n = len(row['bytes']) // 2
        return fmt_row(row['name'], k, rva68, at68, b.rd(at68, n).hex().upper())
    if k == 'ptr':
        at68 = rva68 + (row['at'] - row['rva'])
        return fmt_row(row['name'], k, rva68, at68, '%08X' % (b.q(at68) - b.imagebase))
    return fmt_row(row['name'], k, rva68, rva68, '-')


def hand_ok(row, res, rva68):
    """A HAND decision must be one of the tool's own candidates or suggestions (typo guard)."""
    pool = set()
    for key in ('rva68', 'best'):
        if res.get(key):
            pool.add(res[key])
    pool.update(res.get('cands', []) or [])
    pool.update(res.get('suggest', []) or [])
    if row['kind'] == 'ret':
        n = len(row['bytes']) // 2
        for f68 in res.get('func68s', []):
            pool.add(f68 + (row['rva'] - res['func65']))
    return rva68 in pool


HEADER68 = """# kenshi-coop address table - Kenshi 1.0.68 x64 Steam (the vanilla kenshi_x64.exe, MD5 8a03c256...)
# DATA, NOT CODE. Generated by tools/gen_table_1068.py from the 1.0.65 table; do not hand-edit a number.
# Rows the tool could not match unchanged were decided by hand: .modding/investigations/table-1068.md.
# A new game build is a new file here, contributed without touching the plugin.
# fields:  <name> <kind> <rva> <verify_at> <bytes>
#   code  a function entry; <bytes> are the bytes at <verify_at> (= <rva>, or <rva>+16
#         where another mod has already hooked the entry before our plugin starts)
#   ret   an interior return address; <bytes> are the CALL that returns to <rva>
#   ro    read-only initialised data; <bytes> are the image's own constant bytes
#   ptr   a relocated pointer in read-only data; <bytes> is the RVA it must point at
#   var   a writable global; NOT VERIFIABLE at start - see build/prep-p8h.md"""


def generate(args):
    a = Image(args.exe65)
    b = Image(args.exe68)
    header, rows = read_table(args.table65)
    fp65 = [h for h in header if h.startswith('!fingerprint')][0].split()[1]
    if a.fingerprint() != fp65:
        sys.exit('the 1.0.65 exe fingerprint %s is not the table\'s %s' % (a.fingerprint(), fp65))
    M = Matcher(a, b)
    X = Xrefs(a)
    counts = {}
    lines = []
    hand_used = []
    refused = []
    for row in rows:
        res = resolve_row(M, X, row)
        st = res['status']
        counts[st] = counts.get(st, 0) + 1
        tag = st
        rva68 = res['rva68'] if st == 'same' else None
        if row['name'] in HAND:
            h = HAND[row['name']]
            if st == 'same' and h != res['rva68']:
                sys.exit('HAND %s = %X disagrees with the tool\'s unchanged match %X' % (row['name'], h, res['rva68']))
            if h is None:
                refused.append(row['name'])
                tag = st + ' -> REFUSED (hand)'
            elif not hand_ok(row, res, h):
                sys.exit('HAND %s = %X is none of the tool\'s candidates - check table-1068.md' % (row['name'], h))
            else:
                rva68 = h
                hand_used.append(row['name'])
                tag = st + ' -> hand %X' % h
        print('%-46s %-4s %08X  %-26s %s%s' % (row['name'], row['kind'], row['rva'], tag, res['detail'],
                                              ('  [' + res['hint'] + ']') if res.get('hint') else ''))
        if st != 'same' and row['name'] not in HAND:
            extra = []
            if res.get('cands'):
                extra.append('candidates ' + ' '.join('%X' % c for c in res['cands']))
            if res.get('func68s'):
                extra.append('function candidates ' + ' '.join('%X' % c for c in res['func68s']))
            if extra:
                print('    ' + '; '.join(extra))
        if rva68 is not None:
            lines.append(row68(b, row, rva68, res))
    print()
    print('rows %d: ' % len(rows) + ', '.join('%s %d' % kv for kv in sorted(counts.items())))
    print('hand-decided %d: %s' % (len(hand_used), ' '.join(hand_used)))
    if refused:
        print('REFUSED %d: %s' % (len(refused), ' '.join(refused)))
    unresolved = len(rows) - len(lines) - len(refused)
    if unresolved:
        print('UNRESOLVED %d rows - no table written' % unresolved)
        return 1
    if args.report_only:
        return 0
    fp68 = b.fingerprint()
    out = os.path.join(ADDR_DIR, fp68 + '.txt')
    text = HEADER68 + '\n!fingerprint %s\n!version %s\n!count %d\n' % (fp68, VERSION68, len(lines)) + '\n'.join(lines) + '\n'
    with open(out, 'w', encoding='ascii', newline='\n') as f:
        f.write(text)
    print('wrote %s (%d rows)' % (out, len(lines)))
    return 0


# =================================================================================================================
# the offline load check - what AddrInit does, against the file instead of the process
# =================================================================================================================
def check(table, exe):
    img = Image(exe)
    header, rows = read_table(table)
    meta = {}
    for h in header:
        t = h.strip()
        if t.startswith('!'):
            f = t.split()
            meta[f[0]] = f[1] if len(f) > 1 else ''
    fails = []
    if meta.get('!fingerprint') != img.fingerprint():
        fails.append('!fingerprint %s but the exe is %s' % (meta.get('!fingerprint'), img.fingerprint()))
    if int(meta.get('!count', -1)) != len(rows):
        fails.append('!count %s but %d rows' % (meta.get('!count'), len(rows)))
    names = set()
    per = {}
    for r in rows:
        k = r['kind']
        ok = True
        why = ''
        if r['name'] in names or not re.match(r'^[A-Za-z0-9_]{1,47}$', r['name']):
            ok, why = False, 'duplicate or bad name'
        names.add(r['name'])
        if ok and k in ('code', 'ret', 'ro'):
            want = bytes.fromhex(r['bytes'])
            got = img.rd(r['at'], len(want))
            if not (1 <= len(want) <= 16) or got != want:
                ok, why = False, 'bytes at %X are %s, table says %s' % (r['at'], got.hex().upper(), r['bytes'])
            elif k == 'code' and not (r['at'] in (r['rva'], r['rva'] + 16)):
                ok, why = False, 'verify column is neither rva nor rva+16'
            elif k == 'ret' and r['at'] + len(want) != r['rva']:
                ok, why = False, 'the call does not end at the return address'
            elif k in ('code', 'ret') and not (img.text[0] <= r['rva'] < img.text[1]):
                ok, why = False, 'outside .text'
        elif ok and k == 'ptr':
            v = img.q(r['at']) - img.imagebase
            if v != int(r['bytes'], 16):
                ok, why = False, 'the pointer at %X leads to %X, table says %s' % (r['at'], v, r['bytes'])
        elif ok and k == 'var':
            s = img.section_of(r['rva'])
            if r['bytes'] != '-' or r['at'] != r['rva'] or s is None or not (s[3] & SCN_WRITE):
                ok, why = False, 'not inside a writable section (%s)' % (s[0] if s else 'none')
        elif ok:
            ok, why = False, 'unknown kind'
        c = per.setdefault(k, [0, 0])
        c[0 if ok else 1] += 1
        if not ok:
            fails.append('%s (%s %08X): %s' % (r['name'], k, r['rva'], why))
    print('check %s against %s' % (os.path.basename(table), exe))
    print('  fingerprint %s, rows %d, ' % (img.fingerprint(), len(rows))
          + ', '.join('%s %d pass %d fail' % (k, v[0], v[1]) for k, v in sorted(per.items())))
    for f in fails:
        print('  FAIL ' + f)
    print('  RESULT: %s' % ('100% PASS' if not fails else '%d FAILURES' % len(fails)))
    return 0 if not fails else 1


def reverse(args):
    """T-373: the 1.0.65 line of named 1.0.68 rows - the same matcher, run 1.0.68 -> 1.0.65. Prints; writes nothing.

    A new row is found in the 1.0.68 exe from our own reads and written to the 1.0.68 table first; this gives its
    1.0.65 address from the 1.0.65 exe's own bytes (never from RE_Kenshi's .br). See .modding/01-environment.md."""
    a = Image(args.exe68)                                  # the source side: the 1.0.68 exe and table
    b = Image(args.exe65)                                  # the target side: the 1.0.65 exe
    header, rows = read_table(args.table68)
    fp = [h for h in header if h.startswith('!fingerprint')][0].split()[1]
    if a.fingerprint() != fp:
        sys.exit('the 1.0.68 exe fingerprint %s is not the table\'s %s' % (a.fingerprint(), fp))
    want = [n for n in args.rows.split(',') if n]
    if not want:
        sys.exit('--reverse needs --rows NAME[,NAME...] (the new 1.0.68 rows to carry to 1.0.65)')
    unknown = [n for n in want if n not in set(r['name'] for r in rows)]
    if unknown:
        sys.exit('not in %s: %s' % (os.path.basename(args.table68), ' '.join(unknown)))
    have65 = set(r['name'] for r in read_table(args.table65)[1])
    M = Matcher(a, b)
    X = Xrefs(a)
    out, bad = [], 0
    for row in [r for r in rows if r['name'] in want]:
        res = resolve_row(M, X, row)
        print('%-46s %-4s %08X  %-10s %s' % (row['name'], row['kind'], row['rva'], res['status'], res['detail']))
        if res['status'] == 'same':
            out.append(row68(b, row, res['rva68'], res) + ('   # ALREADY IN THE 1.0.65 TABLE' if row['name'] in have65 else ''))
        else:
            bad += 1
    print('\n1.0.65 lines (read from %s):' % args.exe65)
    for line in out:
        print(line)
    if bad:
        print('%d row(s) not matched unchanged - no 1.0.65 line; decide by hand from the report above' % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe65', default=EXE65)
    ap.add_argument('--exe68', default=EXE68)
    ap.add_argument('--table65', default=TABLE65)
    ap.add_argument('--report-only', action='store_true')
    ap.add_argument('--check', nargs=2, metavar=('TABLE', 'EXE'))
    ap.add_argument('--reverse', action='store_true')
    ap.add_argument('--table68', default=TABLE68)
    ap.add_argument('--rows', default='')
    args = ap.parse_args()
    if args.check:
        return check(args.check[0], args.check[1])
    if args.reverse:
        return reverse(args)
    return generate(args)


if __name__ == '__main__':
    sys.exit(main())
