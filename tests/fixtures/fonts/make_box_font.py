#!/usr/bin/env python3
"""Writes box.ttf: a TrueType font, "Shoal Box", whose printable ASCII glyphs are boxes.

Every glyph from ! to ~ is the same 500x700 box on a 600-unit advance, in a 1000-unit em;
the space is empty, 600 wide. Run it from this folder to write the font again.
"""

import struct

UNITS_PER_EM = 1000
ADVANCE = 600
ASCENDER = 800
DESCENDER = -200
FIRST, LAST = 0x20, 0x7E
FAMILY = "Shoal Box"


def box():
    # One contour, clockwise, every point on the curve, coordinates as 16-bit deltas.
    points = [(50, 0), (50, 700), (550, 700), (550, 0)]
    data = struct.pack(">hhhhh", 1, 50, 0, 550, 700)
    data += struct.pack(">H", len(points) - 1) + struct.pack(">H", 0)
    data += bytes([0x01] * len(points))
    x = y = 0
    xs, ys = b"", b""
    for px, py in points:
        xs += struct.pack(">h", px - x)
        ys += struct.pack(">h", py - y)
        x, y = px, py
    data += xs + ys
    return data + b"\0" * (-len(data) % 4)


# Glyph 0 is .notdef (a box), 1 the space (empty), then one box for each of ! to ~.
glyphs = [box(), b""] + [box() for _ in range(FIRST + 1, LAST + 1)]
count = len(glyphs)

glyf = b"".join(glyphs)
offsets = [0]
for glyph in glyphs:
    offsets.append(offsets[-1] + len(glyph))
loca = b"".join(struct.pack(">I", offset) for offset in offsets)

head = struct.pack(
    ">IIIIHHqqhhhhHHhhh",
    0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, UNITS_PER_EM, 0, 0,
    0, 0, 550, 700, 0, 8, 2, 1, 0)  # long loca offsets
hhea = struct.pack(
    ">IhhhHhhhhhhhhhhhH",
    0x00010000, ASCENDER, DESCENDER, 0, ADVANCE, 0, 0, 550, 1, 0, 0, 0, 0, 0, 0, 0, count)
maxp = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, count, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
hmtx = b"".join(struct.pack(">Hh", ADVANCE, 0 if i == 1 else 50) for i in range(count))
os2 = struct.pack(
    ">HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHIIhhHHH",
    4, ADVANCE, 400, 5, 0, 650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0, bytes(10),
    1, 0, 0, 0, b"SHOL", 0x0040, FIRST, LAST, ASCENDER, DESCENDER, 0, ASCENDER, -DESCENDER,
    1, 0, 500, 700, 0, 0x20, 1)
post = struct.pack(">IIhhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)

segments = [(FIRST, LAST, (1 - FIRST) & 0xFFFF), (0xFFFF, 0xFFFF, 1)]
seg_count = len(segments)
subtable = struct.pack(">HHHHHH", 4, 16 + 8 * seg_count, 0, seg_count * 2, 4, 1)
subtable += struct.pack(">H", 0)  # rangeShift
subtable += b"".join(struct.pack(">H", end) for _, end, _ in segments)
subtable += struct.pack(">H", 0)
subtable += b"".join(struct.pack(">H", start) for start, _, _ in segments)
subtable += b"".join(struct.pack(">H", delta) for _, _, delta in segments)
subtable += b"".join(struct.pack(">H", 0) for _ in segments)
cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + subtable

names = {1: FAMILY, 2: "Regular", 3: FAMILY + " Regular", 4: FAMILY + " Regular",
         6: FAMILY.replace(" ", "") + "-Regular"}
strings = b""
records = b""
for name_id, text in names.items():
    encoded = text.encode("utf-16-be")
    records += struct.pack(">HHHHHH", 3, 1, 0x409, name_id, len(encoded), len(strings))
    strings += encoded
name = struct.pack(">HHH", 0, len(names), 6 + 12 * len(names)) + records + strings

tables = {b"OS/2": os2, b"cmap": cmap, b"glyf": glyf, b"head": head, b"hhea": hhea,
          b"hmtx": hmtx, b"loca": loca, b"maxp": maxp, b"name": name, b"post": post}


def checksum(data):
    data += b"\0" * (-len(data) % 4)
    return sum(struct.unpack(f">{len(data) // 4}I", data)) & 0xFFFFFFFF


def build(tables):
    tags = sorted(tables)
    power = 1
    while power * 2 <= len(tags):
        power *= 2
    font = struct.pack(">IHHHH", 0x00010000, len(tags), power * 16, power.bit_length() - 1,
                       len(tags) * 16 - power * 16)
    offset = 12 + 16 * len(tags)
    body = b""
    for tag in tags:
        data = tables[tag]
        font += struct.pack(">4sIII", tag, checksum(data), offset + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    return font + body


font = build(tables)
adjustment = (0xB1B0AFBA - checksum(font)) & 0xFFFFFFFF
tables[b"head"] = head[:8] + struct.pack(">I", adjustment) + head[12:]
with open("box.ttf", "wb") as file:
    file.write(build(tables))
