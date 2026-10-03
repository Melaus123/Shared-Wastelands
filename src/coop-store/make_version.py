"""make_version.py - writes server_version.res next to this file: the VERSION resource of SharedWastelandsServer.exe
(owner decision 244), so Explorer's Details tab and Task Manager name the world server "Shared Wastelands world server".

The VS2010 toolchain here has no rc.exe, so this script writes the compiled .res bytes itself, as
src/installer/make_icon.py does for the Setup's icon; build.bat passes server_version.res to cl and link converts it
with cvtres.exe. Standard library only.

Usage:  python make_version.py
Re-run only when a string or the version below changes; the .res it writes is committed.
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
VERSION = (1, 0, 0, 0)
# The names below repeat src/common/names.h (kServerTitle, kServerExe): change both together.
STRINGS = [   # (key, value) - the StringFileInfo table, US English / Unicode (040904B0)
    ("FileDescription", "Shared Wastelands world server"),
    ("ProductName", "Shared Wastelands"),
    ("FileVersion", "%d.%d.%d.%d" % VERSION),
    ("ProductVersion", "%d.%d.%d.%d" % VERSION),
    ("InternalName", "SharedWastelandsServer"),
    ("OriginalFilename", "SharedWastelandsServer.exe"),
]


def pad4(b):
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def node(key, value=b"", value_len=0, vtype=0, children=()):
    """One VS_VERSIONINFO-style block: wLength, wValueLength, wType, szKey, padding, Value, padding, children."""
    body = struct.pack("<HHH", 0, value_len, vtype) + (key + "\0").encode("utf-16-le")
    body = pad4(body) + value
    for c in children:
        body = pad4(body) + c
    return struct.pack("<H", len(body)) + body[2:]


def string(key, text):
    return node(key, (text + "\0").encode("utf-16-le"), len(text) + 1, 1)


def version_info():
    ms, ls = (VERSION[0] << 16) | VERSION[1], (VERSION[2] << 16) | VERSION[3]
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, ms, ls, ms, ls, 0x3F, 0, 0x00040004, 1, 0, 0, 0)
    table = node("040904B0", vtype=1, children=[string(k, v) for k, v in STRINGS])
    sfi = node("StringFileInfo", vtype=1, children=[table])
    vfi = node("VarFileInfo", vtype=1, children=[node("Translation", struct.pack("<HH", 0x0409, 0x04B0), 4, 0)])
    return node("VS_VERSION_INFO", fixed, len(fixed), 0, [sfi, vfi])


def res_entry(type_id, name_id, flags, data):
    """One resource in a .res file: ordinal type and name, US English (the layout make_icon.py writes)."""
    header = struct.pack("<II", len(data), 32) + struct.pack("<HHHH", 0xFFFF, type_id, 0xFFFF, name_id)
    header += struct.pack("<IHHII", 0, flags, 0x0409, 0, 0)
    return header + pad4(data)


def main():
    res = struct.pack("<II", 0, 32) + struct.pack("<HHHH", 0xFFFF, 0, 0xFFFF, 0) + struct.pack("<IHHII", 0, 0, 0, 0, 0)
    res += res_entry(16, 1, 0x0030, version_info())   # RT_VERSION, id 1, MOVEABLE | PURE
    out = os.path.join(HERE, "server_version.res")
    with open(out, "wb") as f:
        f.write(res)
    print("wrote %s (%d bytes)" % (out, len(res)))


if __name__ == "__main__":
    main()
