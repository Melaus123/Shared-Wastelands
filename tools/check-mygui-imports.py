#!/usr/bin/env python3
# -*- coding: ascii -*-
"""check-mygui-imports.py - THE BUILD GATE F654 was a one-off measurement of (P8i / U1, design V1h).

WHAT IT ANSWERS.  Every symbol SharedWastelands.dll imports from MyGUIEngine_x64.dll must exist BY NAME in
the MyGUIEngine_x64.dll the game actually ships.  P7z proved that by hand for the 18 imports the one
button needed.  The panel adds Window, EditBox, TextBox, InputManager and UString calls, and a
missing import is not a cosmetic failure: the DLL does not load, and every co-op feature disappears
with it.  So it is a GATE.  `src\\coop-plugin\\build.bat` runs it after the link and a miss exits
non-zero BEFORE tools\\deploy.ps1 can ever see the DLL.

IT REFUSES TO PASS VACUOUSLY.  An unexercised probe is not a passing probe (6a lesson 8): if the
import block is not found, or no exports parse out of the game's DLL, this exits non-zero and says
so rather than reporting "0 missing".

usage: check-mygui-imports.py <dumpbin-imports.txt> <dumpbin-exports.txt> [dll-name]
"""

import re
import sys


def parse_imports(text, dll):
    """The symbol names in `dumpbin /imports`'s block for `dll`."""
    want = dll.lower()
    lines = text.splitlines()
    names = []
    inblock = False
    header = re.compile(r"^\s*([A-Za-z0-9_.\-]+\.dll)\s*$", re.I)
    entry = re.compile(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$")
    for ln in lines:
        m = header.match(ln)
        if m:
            inblock = (m.group(1).lower() == want)
            continue
        if not inblock:
            continue
        m = entry.match(ln)
        if m:
            nm = m.group(1)
            if nm.lower().endswith(".dll"):
                continue
            names.append(nm)
    return names


def parse_exports(text):
    """The names in `dumpbin /exports`: `ordinal hint RVA name`, plus forwarders."""
    names = set()
    row = re.compile(r"^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)")
    fwd = re.compile(r"^\s*\d+\s+[0-9A-Fa-f]+\s+(\S+)\s+\(forwarded")
    for ln in text.splitlines():
        m = row.match(ln)
        if m:
            names.add(m.group(1))
            continue
        m = fwd.match(ln)
        if m:
            names.add(m.group(1))
    return names


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    dll = argv[3] if len(argv) > 3 else "MyGUIEngine_x64.dll"
    imports_text = open(argv[1], "r", errors="replace").read()
    exports_text = open(argv[2], "r", errors="replace").read()

    imports = parse_imports(imports_text, dll)
    exports = parse_exports(exports_text)

    if not exports:
        sys.stderr.write("IMPORT GATE FAILED: no exports parsed out of %s - the gate cannot say anything,\n"
                         "which is not the same as passing.\n" % argv[2])
        return 1
    if not imports:
        sys.stderr.write("IMPORT GATE FAILED: no %s import block found in %s. Either the DLL stopped\n"
                         "importing MyGUI (in which case the panel cannot work) or dumpbin's format changed.\n"
                         % (dll, argv[1]))
        return 1

    missing = [n for n in imports if n not in exports]
    print("=== MyGUI import gate: %d imports from %s, checked against %d exported names ==="
          % (len(imports), dll, len(exports)))
    for n in sorted(imports):
        print("  %s  %s" % ("OK  " if n in exports else "MISS", n))
    if missing:
        sys.stderr.write("IMPORT GATE FAILED: %d import(s) are NOT present by name in the game's %s:\n"
                         % (len(missing), dll))
        for n in missing:
            sys.stderr.write("    %s\n" % n)
        return 1
    print("=== MyGUI import gate: all %d present by name ===" % len(imports))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
