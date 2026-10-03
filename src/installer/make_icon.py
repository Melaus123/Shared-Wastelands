"""make_icon.py - draws the Shared Wastelands Setup icon (owner decision 164: a simple ORIGINAL icon, not Kenshi's
artwork) and writes, next to this file:

  SharedWastelandsSetup.ico  the icon file (16, 32, 48, 256 px; 32-bit colour)
  setup_icon.res              the same pictures as a compiled Windows resource (icon group 1), which link.exe
                              takes directly - the VS2010 toolchain here has no rc.exe, so this script writes the
                              .res bytes itself (build.bat passes it to cl, link converts it with cvtres.exe)

The picture: two flat, stylised figures side by side (an amber one in front, a teal one behind) on a dark
rounded square - "two players". No text, so it stays readable at 16 px.

Needs Python 3 + Pillow.  Usage:  python make_icon.py [preview.png]
Re-run only when the picture changes; the .ico and .res it writes are committed.
"""
import os
import struct
import sys
from io import BytesIO

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 32, 48, 256]          # 16/32/48 stored as bitmaps, 256 as PNG (the Windows Vista+ convention)
ICON_GROUP_ID = 1                  # setup_main.cpp loads MAKEINTRESOURCE(1)

BG = (38, 42, 50, 255)             # dark slate
FRONT = (232, 163, 60, 255)        # amber
BACK = (79, 179, 169, 255)         # teal


def draw(size):
    """Draw at 8x and shrink, so the edges are smooth at every size."""
    s = size * 8
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = lambda v: int(round(v * s))
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=u(0.20), fill=BG)

    def figure(cx, colour, gap):
        # head and shoulders; 'gap' draws a background-coloured outline first so the front figure stands clear
        head_r, head_y = 0.125, 0.33
        body = [cx - 0.215, 0.53, cx + 0.215, 1.06]
        if gap:
            g = 0.045
            d.ellipse([u(cx - head_r - g), u(head_y - head_r - g), u(cx + head_r + g), u(head_y + head_r + g)], fill=BG)
            d.rounded_rectangle([u(body[0] - g), u(body[1] - g), u(body[2] + g), u(body[3])], radius=u(0.19), fill=BG)
        d.ellipse([u(cx - head_r), u(head_y - head_r), u(cx + head_r), u(head_y + head_r)], fill=colour)
        d.rounded_rectangle([u(body[0]), u(body[1]), u(body[2]), u(body[3])], radius=u(0.17), fill=colour)

    figure(0.63, BACK, False)
    figure(0.37, FRONT, True)
    # the rounded square clips the bodies at the bottom edge
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=u(0.20), fill=255)
    clipped = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    clipped.paste(im, (0, 0), mask)
    return clipped.resize((size, size), Image.LANCZOS)


def dib(im):
    """An icon-resource bitmap: BITMAPINFOHEADER (height doubled), 32-bit BGRA rows bottom-up, then a 1-bit AND mask."""
    w, h = im.size
    px = im.load()
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    colour = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            colour += bytes((b, g, r, a))
    row = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(row)
        for x in range(w):
            if px[x, y][3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return header + bytes(colour) + bytes(mask)


def png(im):
    b = BytesIO()
    im.save(b, format="PNG", optimize=True)
    return b.getvalue()


def res_entry(type_id, name_id, flags, data):
    """One resource in a .res file: ordinal type and name, US English."""
    header = struct.pack("<II", len(data), 32) + struct.pack("<HHHH", 0xFFFF, type_id, 0xFFFF, name_id)
    header += struct.pack("<IHHII", 0, flags, 0x0409, 0, 0)
    pad = b"\0" * ((4 - len(data) % 4) % 4)
    return header + data + pad


def main():
    images = [(n, draw(n)) for n in SIZES]
    blobs = [(n, png(im) if n >= 256 else dib(im)) for n, im in images]

    # .ico: ICONDIR, one ICONDIRENTRY per size, then the pictures
    ico = struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    body = b""
    for n, data in blobs:
        ico += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(data), offset + len(body))
        body += data
    # the Setup file name repeats src/installer/build.bat and src/common/names.h
    with open(os.path.join(HERE, "SharedWastelandsSetup.ico"), "wb") as f:
        f.write(ico + body)

    # .res: the empty first entry every .res starts with, one RT_ICON (3) per size, then RT_GROUP_ICON (14)
    res = struct.pack("<II", 0, 32) + struct.pack("<HHHH", 0xFFFF, 0, 0xFFFF, 0) + struct.pack("<IHHII", 0, 0, 0, 0, 0)
    group = struct.pack("<HHH", 0, 1, len(blobs))
    for i, (n, data) in enumerate(blobs, 1):
        res += res_entry(3, i, 0x1010, data)
        group += struct.pack("<BBBBHHIH", n % 256, n % 256, 0, 0, 1, 32, len(data), i)
    res += res_entry(14, ICON_GROUP_ID, 0x1030, group)
    with open(os.path.join(HERE, "setup_icon.res"), "wb") as f:
        f.write(res)

    if len(sys.argv) > 1:   # a preview sheet: every size at 1x and 8x (nearest), on light and dark
        sheet = Image.new("RGBA", (1100, 560), (240, 240, 240, 255))
        x = 10
        for n, im in images:
            big = im.resize((n * 8, n * 8), Image.NEAREST) if n < 256 else im
            sheet.paste(big, (x, 10), big)
            sheet.paste(im, (x, 10 + big.size[1] + 10), im)
            x += big.size[0] + 20
        sheet.save(sys.argv[1])
    print("wrote SharedWastelandsSetup.ico (%d bytes) and setup_icon.res (%d bytes)" % (len(ico + body), len(res)))


if __name__ == "__main__":
    main()
