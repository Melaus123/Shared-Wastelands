"""check_names.py - the LEFTOVER CHECK for owner decision 244: nothing a player sees in the release package names the mod
by its old internal name.

  python tools/check_names.py [package folder]      (default: build/package/shared-wastelands)

It lists, and checks:
  1. every file and folder name in the package folder, the package folder's own name included (the mod folder, every
     file in it, and the manifest written beside it);
  2. every string in the version information of every .exe and .dll shipped (the keys and the values: FileDescription,
     ProductName, OriginalFilename, ...) - what Explorer's Details tab and Task Manager show;
  3. the mod list entry's text inside each .mod file (the author and description Kenshi's mod list shows).
Any of them containing an old name, in any case, is a FAIL: the old internal name, or the previous player-visible
name in any of its spellings ("Kenshi Multiplayer", "KenshiMultiplayer", "kenshi_multiplayer", "kenshi-multiplayer").
The shipped binaries' CONTENTS are not scanned (only their names and version information), so Setup's one use of the
previous loader name (src/common/names.h kOldLoaderPlugin, the old Plugins_x64.cfg line it removes) needs no exception.
Exit 0 = clean, 1 = a leftover (each one listed),
2 = the package folder is missing. Windows only (the version information is read with version.dll).
"""
import ctypes
import os
import re
import struct
import sys

BANNED = bytes([99, 111, 111, 112]).decode("ascii")   # the old internal name, matched in any case (spelled as bytes: no new text writes it)
PREVIOUS = re.compile(r"kenshi[ _-]?multiplayer", re.IGNORECASE)   # the previous player-visible name, every spelling (owner 435)
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, "..", "build", "package", "shared-wastelands")


def version_strings(path):
    """Every key and string value in the file's version resource, walked from the raw VS_VERSIONINFO block.
    [] when the file has none."""
    ver = ctypes.WinDLL("version")
    size = ver.GetFileVersionInfoSizeW(path, None)
    if not size:
        return []
    buf = ctypes.create_string_buffer(size)
    if not ver.GetFileVersionInfoW(path, 0, size, buf):
        return []
    data = buf.raw
    out = []

    def walk(off, end):
        while off + 6 <= end:
            length, vlen, vtype = struct.unpack_from("<HHH", data, off)
            if length == 0:
                break
            stop = min(off + length, end)
            k = off + 6
            key_end = k
            while key_end + 1 < stop and data[key_end:key_end + 2] != b"\0\0":
                key_end += 2
            key = data[k:key_end].decode("utf-16-le", "replace")
            out.append(("key", key))
            v = (key_end + 2 + 3) & ~3
            if vtype == 1 and vlen:
                val = data[v:v + vlen * 2].decode("utf-16-le", "replace").rstrip("\0")
                out.append((key, val))
                v += vlen * 2
            else:
                v += vlen
            v = (v + 3) & ~3
            if key in ("VS_VERSION_INFO", "StringFileInfo", "VarFileInfo") or (len(key) == 8 and vtype == 1 and vlen == 0):
                walk(v, stop)
            off = (off + length + 3) & ~3

    walk(0, min(len(data), struct.unpack_from("<H", data, 0)[0]))   # the block's own length: version.dll pads the buffer
    return out


def mod_header_text(path):
    """The author and description in a Kenshi .mod header: int type, int version, then two length-prefixed strings."""
    with open(path, "rb") as f:
        b = f.read()
    try:
        _, _, n1 = struct.unpack_from("<iii", b, 0)
        author = b[12:12 + n1].decode("latin-1")
        (n2,) = struct.unpack_from("<i", b, 12 + n1)
        desc = b[16 + n1:16 + n1 + n2].decode("latin-1")
        return [("author", author), ("description", desc)]
    except struct.error:
        return [("unreadable header", path)]


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else DEFAULT)
    if not os.path.isdir(root):
        print("check_names: NO PACKAGE at %s - build it first (tools/build-package.ps1)" % root)
        return 2
    bad = []

    def check(where, text):
        hit = BANNED in text.lower() or PREVIOUS.search(text) is not None
        print("  %-4s %s: %s" % ("FAIL" if hit else "ok", where, text))
        if hit:
            bad.append("%s: %s" % (where, text))

    print("1. file and folder names under %s" % root)
    check("package folder", os.path.basename(root))
    binaries, mods = [], []
    for d, dirs, files in os.walk(root):
        dirs.sort()
        for n in sorted(dirs):
            check("folder", os.path.relpath(os.path.join(d, n), root))
        for n in sorted(files):
            p = os.path.join(d, n)
            check("file", os.path.relpath(p, root))
            if n.lower().endswith((".exe", ".dll")):
                binaries.append(p)
            elif n.lower().endswith(".mod"):
                mods.append(p)

    print("2. version information of the shipped binaries")
    for p in binaries:
        rel = os.path.relpath(p, root)
        strings = version_strings(p)
        if not strings:
            print("  --   %s: no version information" % rel)
        for kind, text in strings:
            check("%s [%s]" % (rel, kind), text)

    print("3. the mod list entry text")
    for p in mods:
        rel = os.path.relpath(p, root)
        for kind, text in mod_header_text(p):
            check("%s [%s]" % (rel, kind), text)

    if bad:
        print("check_names: FAIL - %d leftover(s) of an old name:" % len(bad))
        for b in bad:
            print("  " + b)
        return 1
    print("check_names: PASS - no file, folder, version string or mod list text contains '%s' or '%s' (any case)" % (BANNED, PREVIOUS.pattern))
    return 0


if __name__ == "__main__":
    sys.exit(main())
