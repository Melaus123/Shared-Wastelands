"""scan_hardcoded_rva.py - no engine address may be written into the plugin's code; every one comes from the address table.

WHY (stage 7/9 of the RE_Kenshi migration, 2026-09-29). The mod supports Steam 1.0.65 and Steam 1.0.68 (owner decision 99).
It finds engine functions and globals through per-executable address tables (src/coop-plugin/addresses/<fingerprint>.txt,
bound in code with coop::AddrReg - addresses.h). A number written straight into the code is a 1.0.65 number: on 1.0.68 it
calls, reads or compares against whatever sits there instead (rule 02-project-rules "never hardcode an RVA"). A read on
2026-09-29 found more than 30 such places. This tool finds them all, and build.bat step 1b3 FAILS the build on any hit.

WHAT IS SCANNED. Every .cpp .h .inl .c under src/coop-plugin and src/common, except src/coop-plugin/third_party (vendored
libraries - ENet, MinHook - which contain no engine addresses) and src/coop-plugin/addresses (the tables themselves).
Comments and the insides of string and character literals are blanked first: a number in prose or a log message is not
an address the code uses. Everything else is code.

THE IMAGE RANGE is read from the tables, not typed here: [0x1000, the largest SizeOfImage in any table's file name) -
the fingerprint's second field. Below 0x1000 is the PE header, which is not engine code or data.

A HIT is any of:
  H1  a hexadecimal literal in [BARE_FLOOR, image end). BARE_FLOOR is 0x10000: every table row lies above it (the tool
      asserts this against both tables), and below it a bare number cannot be told from a struct offset or a size - those
      are caught only by H2/H3, where the code itself says the number is added to the image base.
  H2  an image-base expression plus (or minus, or a pointer's distance from it compared with) an integer literal
      >= 0x1000. Image-base expressions: GetModuleHandle[A|W](0 / NULL / nullptr), Base(), and every variable that file
      assigns from one of those (found by the tool, to a fixed point - not a hand list).
  H3  an image-base expression plus an identifier that is defined somewhere in the scanned code with an integer literal
      initialiser >= 0x1000 and is not a table-bound name (the `&name` of an AddrReg). The hit is reported at the
      identifier's definition, where the literal is.
  H4  ANY integer literal (hex or decimal, any context) equal to the RVA of a row in any address table. Overrides every
      exclusion below and the floors: a number the table already knows is an address.

EXCLUSIONS (rules, not a hand list - each is a kind of number that is not an engine address):
  X1  a power of two (sizes, capacities, alignments, bit flags - and, in H2, the `base + 0x4000000` "is it inside the
      image" windows). No function, vtable or global is placed at an exact power of two >= 64 KiB in these images, and H4
      still catches one if a table row ever is.
  X2  2^n - 1 (all-ones masks: 0xFFFF, 0xFFFFFF, ...).
  X3  a decimal literal (counts, durations, limits). The project writes every engine address in hex (every table row, every
      decompile reference); a decimal-written address is still caught by H2/H3 (context) and H4 (a table value).

THE ALLOW-LIST (tools/scan_hardcoded_rva.allow) is only for a number PROVEN not to be an engine address that no rule above
covers. Each line:  <path relative to the repo> | <literal as written> | <text on the code line> | <why it is not an address>
A line with no reason, or one that matches nothing any more, fails the scan too (the list cannot rot).

THE FIX for a hit: bind the address with a table row - `unsigned long long kX = 0; static coop::AddrReg kX_reg("Row", &kX);`
and use coop::AddrAbs(kX) (or the file's image base + kX). A row that is not in the tables yet goes into BOTH tables
(1.0.65: the bytes read from the 1.0.65 exe; 1.0.68: tools/gen_table_1068.py), then python tools/check_tables.py.

Usage: python tools/scan_hardcoded_rva.py              exit 0 = no hits, 1 = hits (each printed) or a failed self-test.
       Every run first scans a built-in sample holding one of each hit form (the self-test) and fails if any is missed.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCAN = [os.path.join(ROOT, 'src', 'coop-plugin'), os.path.join(ROOT, 'src', 'common')]
SKIP = [os.path.join(ROOT, 'src', 'coop-plugin', 'third_party'), os.path.join(ROOT, 'src', 'coop-plugin', 'addresses')]
ADDR_DIR = os.path.join(ROOT, 'src', 'coop-plugin', 'addresses')
ALLOW = os.path.join(HERE, 'scan_hardcoded_rva.allow')
EXTS = ('.cpp', '.h', '.inl', '.c')
IMAGE_FLOOR = 0x1000
BARE_FLOOR = 0x10000
CONTEXT_FLOOR = 0x1000
BS = chr(92)

LIT = re.compile(r'(?<![\w.])(0[xX][0-9A-Fa-f]+|[1-9][0-9]*|0)([uUlL]*)(?![\w.])')
IDENT = r'[A-Za-z_]\w*'
CAST = r'(?:\(\s*(?:const\s+)?[A-Za-z_][\w:]*(?:\s+[A-Za-z_][\w:]*)*(?:\s*\*)*\s*\)\s*)*'   # (T), (unsigned long long), (const char*)
BASE_CALL = r'(?:::\s*)?GetModuleHandle[AW]?\s*\(\s*(?:0|NULL|nullptr)\s*\)|\bBase\s*\(\s*\)'


def strip_code(s):
    """Comments and the insides of string / char literals -> spaces (newlines kept, so line numbers stay)."""
    out = []
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if s.startswith('//', i):
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', s[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c and s[j] != '\n':
                if s[j] == BS:
                    j += 1
                j += 1
            if j < n and s[j] == c:
                out.append(c + re.sub(r'[^\n]', ' ', s[i + 1:j]) + c)
                i = j + 1
            else:                                         # unterminated on its line (an apostrophe in #error etc.)
                out.append(c + ' ' * (j - i - 1))
                i = j
        elif c == '#' and re.match(r'#\s*include\b', s[i:i + 20]):
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def read_tables():
    """-> (image end, set of every row RVA in every table, lowest row RVA)."""
    ends, rvas = [], set()
    for f in sorted(os.listdir(ADDR_DIR)):
        if not f.endswith('.txt'):
            continue
        parts = f[:-4].split('-')
        if len(parts) != 4:
            raise SystemExit('scan: table file name %s is not a 4-field fingerprint' % f)
        ends.append(int(parts[1], 16))
        for line in open(os.path.join(ADDR_DIR, f), encoding='ascii'):
            t = line.strip()
            if t and t[0] not in '#!':
                rvas.add(int(t.split()[2], 16))
    if not ends:
        raise SystemExit('scan: no address tables in %s' % ADDR_DIR)
    return max(ends), rvas, min(rvas)


def pow2(v):
    return v > 0 and (v & (v - 1)) == 0


def files():
    for top in SCAN:
        for dp, dn, fn in os.walk(top):
            if any(os.path.normcase(dp).startswith(os.path.normcase(s)) for s in SKIP):
                continue
            for f in sorted(fn):
                if f.endswith(EXTS):
                    yield os.path.join(dp, f)


def rel(p):
    return os.path.relpath(p, ROOT).replace(BS, '/')


def scan(sample=None):
    image_end, table_rvas, lowest = read_tables()
    if lowest < BARE_FLOOR:
        raise SystemExit('scan: a table row (0x%X) lies below BARE_FLOOR 0x%X - lower the floor and re-justify it' % (lowest, BARE_FLOOR))
    src = {}
    for p in (files() if sample is None else sorted(sample)):
        raw = open(p, encoding='utf-8', errors='replace').read() if sample is None else sample[p]
        src[p] = (raw.split('\n'), strip_code(raw))

    # table-bound names (the `&name` of an AddrReg) and literal-initialised names, across all files
    bound = set()
    defs = {}   # name -> [(path, line, literal text, value)]
    for p, (raw, code) in src.items():
        for m in re.finditer(r'\bAddrReg\s+' + IDENT + r'\s*\([^;]*?&\s*(' + IDENT + r')\s*\)', code):
            bound.add(m.group(1))
        for m in re.finditer(r'\b(' + IDENT + r')\s*(?:\[\s*\w*\s*\])?\s*=\s*\{?\s*' + CAST + r'(0[xX][0-9A-Fa-f]+|[1-9][0-9]*)[uUlL]*\s*[;,}]', code):
            v = int(m.group(2), 0)
            if v >= CONTEXT_FLOOR:
                ln = code.count('\n', 0, m.start(2)) + 1
                defs.setdefault(m.group(1), []).append((p, ln, m.group(2), v))

    # g_-prefixed globals assigned an image base in ANY file (a global may be set in one file and used in another)
    global_bases = set()
    while True:
        alt = BASE_CALL + ('|\\b(?:' + '|'.join(sorted(global_bases)) + ')\\b' if global_bases else '')
        new = set()
        for p, (raw, code) in src.items():
            new |= set(m.group(1) for m in re.finditer(r'\b(g_\w+)\s*=\s*' + CAST + r'(?:' + alt + r')\s*[;,)]', code))
        if new <= global_bases:
            break
        global_bases |= new

    hits = {}   # (path, line, literal) -> rule

    def hit(p, ln, lit, rule):
        hits.setdefault((p, ln, lit), rule)

    for p, (raw, code) in src.items():
        # H1 + H4: literals
        for m in LIT.finditer(code):
            text = m.group(1)
            v = int(text, 16) if text[:2] in ('0x', '0X') else int(text)
            ln = code.count('\n', 0, m.start()) + 1
            if v in table_rvas:
                hit(p, ln, text, 'H4 a table row\'s RVA')
                continue
            if not text[:2] in ('0x', '0X'):
                continue                                  # X3
            if not (BARE_FLOOR <= v < image_end):
                continue
            if pow2(v) or pow2(v + 1):
                continue                                  # X1, X2
            hit(p, ln, text, 'H1 in the image range')
        # image-base variables of this file (plus every g_-prefixed global any file assigns one), to a fixed point
        bases = set(global_bases)
        while True:
            alt = BASE_CALL + ('|\\b(?:' + '|'.join(sorted(bases)) + ')\\b' if bases else '')
            new = set(m.group(1) for m in re.finditer(r'\b(' + IDENT + r')\s*=\s*' + CAST + r'(?:' + alt + r')\s*[;,)]', code))
            if new <= bases:
                break
            bases |= new
        base = r'(?:' + BASE_CALL + ('|\\b(?:' + '|'.join(sorted(bases)) + ')\\b' if bases else '') + r')'
        operand = r'(0[xX][0-9A-Fa-f]+|[1-9][0-9]*|(?:' + IDENT + r'\s*::\s*)*' + IDENT + r')'   # ns::name counts as name
        pats = [base + r'\s*\)*\s*\+\s*\(*\s*' + CAST + operand,
                operand + r'[uUlL]*\s*\)*\s*\+\s*' + CAST + base,
                r'-\s*' + base + r'\s*\)*\s*(?:[!=]=|<=?|>=?)\s*' + CAST + operand]
        for pat in pats:
            for m in re.finditer(pat, code):
                tok = m.group(1).split('::')[-1].strip()
                ln = code.count('\n', 0, m.start(1)) + 1
                if tok[0].isdigit():
                    v = int(tok, 0) if tok[:2] in ('0x', '0X') else int(tok)
                    if v in table_rvas:
                        hit(p, ln, tok, 'H4 a table row\'s RVA')
                    elif v >= CONTEXT_FLOOR and not (pow2(v) or pow2(v + 1)):   # X1/X2: base + 2^n is a window size
                        hit(p, ln, tok, 'H2 image base + literal')
                elif tok in defs and tok not in bound:
                    for (dp, dln, dlit, dv) in defs[tok]:
                        hit(dp, dln, dlit, 'H3 image base + %s (defined here)' % tok)
    return src, hits


def load_allow():
    rows = []
    if not os.path.exists(ALLOW):
        return rows
    for i, line in enumerate(open(ALLOW, encoding='utf-8'), 1):
        t = line.strip()
        if not t or t.startswith('#'):
            continue
        f = [x.strip() for x in t.split('|')]
        if len(f) != 4 or not all(f):
            raise SystemExit('scan: %s line %d needs 4 non-empty fields: path | literal | code text | reason' % (ALLOW, i))
        rows.append([f[0], f[1], f[2], f[3], i, 0])
    return rows


SELFTEST = {   # one of each hit form: every literal in SELFTEST_WANT must be reported, and nothing else
    'selftest_a.cpp': (
        'namespace ns { const unsigned kFoo = 0x2345; }\n'                                      # H3 (qualified use)
        'static unsigned kBar = 0x1234;\n'                                                       # H3
        'const unsigned kVt = 0x16AB000;\n'                                                      # H1
        'void f() { unsigned long long g_xb = (unsigned long long)::GetModuleHandleA(0);\n'
        '  void* p = (void*)(g_xb + ns::kFoo); void* q = (void*)(Base() + kBar);\n'
        '  void* r = (void*)(g_xb + 0x3000); if ((unsigned long long)p - g_xb != 0x5000) {}\n'   # H2, H2
        '  /* 0x5C9BF1 */ const char* t = "0x5C9BF1"; unsigned w = 0x4000000; }\n'),             # none: comment, string, 2^n
}
SELFTEST_WANT = set(['0x2345', '0x1234', '0x16AB000', '0x3000', '0x5000'])


def main():
    _, th = scan(SELFTEST)
    got = set(k[2] for k in th)
    if got != SELFTEST_WANT:
        print('RVA SCAN SELF-TEST FAILED: reported %s, expected %s' % (sorted(got), sorted(SELFTEST_WANT)))
        return 1
    src, hits = scan()
    allow = load_allow()
    bad = []
    for (p, ln, lit), rule in sorted(hits.items()):
        line = src[p][0][ln - 1].strip()
        ok = False
        for a in allow:
            if a[0] == rel(p) and a[1] == lit and a[2] in line:
                a[5] += 1
                ok = True
        if not ok:
            bad.append('%s:%d: %s  [%s]  %s' % (rel(p), ln, lit, rule, line[:160]))
    stale = ['%s line %d (%s %s) matches nothing' % (ALLOW, a[4], a[0], a[1]) for a in allow if a[5] == 0]
    for b in bad:
        print('RVA SCAN HIT ' + b)
    for s in stale:
        print('RVA SCAN STALE ALLOW ' + s)
    print('rva scan: %d files, %d hit(s), %d allowed, %d stale allow line(s)'
          % (len(src), len(bad), len(hits) - len(bad), len(stale)))
    if bad or stale:
        print('  (a hardcoded engine address: bind it through the address table - see the top of tools/scan_hardcoded_rva.py)')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
