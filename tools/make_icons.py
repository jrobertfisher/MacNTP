#!/usr/bin/env python3
"""Draws MacNTP's icon (an original clock face) and writes Rez resources:
   ICN# / ics# (black & white + mask) and icl8 / ics8 (Mac 256-colour palette).

   Usage: make_icons.py <output icons.r> <output preview.png>
"""
import math
import sys
from PIL import Image, ImageDraw

SS = 16                                    # supersampling factor
ORANGE = (255, 149, 0)


def mac_palette():
    """The standard Macintosh 8-bit colour table (clut 8)."""
    pal = []
    for i in range(215):                   # 6x6x6 cube, white first, black left out
        pal.append((255 - 51 * (i // 36), 255 - 51 * ((i // 6) % 6), 255 - 51 * (i % 6)))
    ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    pal += [(v, 0, 0) for v in ramp] + [(0, v, 0) for v in ramp] + [(0, 0, v) for v in ramp]
    pal += [(v, v, v) for v in ramp]
    pal.append((0, 0, 0))
    assert len(pal) == 256
    return pal


PAL = mac_palette()


def nearest(rgb):
    r, g, b = rgb
    gray = abs(r - g) < 12 and abs(g - b) < 12
    best, bi = None, 0
    for i, (pr, pg, pb) in enumerate(PAL):
        if gray and not (pr == pg == pb):  # keep greys grey
            continue
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best is None or d < best:
            best, bi = d, i
    return bi


def draw_clock(size, colour, simple):
    """Draws the clock at size*SS and returns an RGBA image at `size`."""
    S = size * SS
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = S / 2
    px = SS                                # one final pixel

    def line(a_deg, r0, r1, width, fill, dx=0.0):
        a = math.radians(a_deg)
        x0, y0 = c + dx * px + r0 * math.sin(a), c - r0 * math.cos(a)
        x1, y1 = c + dx * px + r1 * math.sin(a), c - r1 * math.cos(a)
        d.line([(x0, y0), (x1, y1)], fill=fill, width=int(width))
        for (x, y) in ((x0, y0), (x1, y1)):           # round caps
            d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=fill)

    # Rounded-square face with a thin outline so it reads on a white desktop.
    radius = S * 0.23
    outline = (0, 0, 0) if not colour else (102, 102, 102)
    d.rounded_rectangle([0.5 * px, 0.5 * px, S - 0.5 * px, S - 0.5 * px], radius=radius,
                        fill=(255, 255, 255, 255), outline=outline, width=int(px))

    R = S * 0.5
    dark = (0, 0, 0) if not colour else (51, 51, 51)
    light = (170, 170, 170)
    if not simple:
        if colour:                                     # minute marks (colour 32px only)
            for m in range(60):
                if m % 5:
                    line(m * 6, R * 0.80, R * 0.86, px * 0.6, light)
        for h in range(12):                            # hour marks
            long_mark = h % 3 == 0
            line(h * 30, R * (0.66 if long_mark else 0.72), R * 0.86, px * (1.4 if long_mark else 1.0), dark)
    else:
        for h in range(0, 12, 3):
            line(h * 30, R * 0.66, R * 0.84, px * 1.2, dark)

    # Hands at 10:10, second hand on the 6.
    hand = (0, 0, 0)
    line(305, -R * 0.06, R * (0.46 if not simple else 0.42), px * (2.4 if not simple else 1.2), hand)    # hour
    line(60, -R * 0.06, R * (0.68 if not simple else 0.62), px * (2.0 if not simple else 1.1), hand)     # minute
    if colour:                                         # second hand: colour icons only
        line(180, -R * 0.14, R * 0.74, px * 1.0, ORANGE, dx=0.5)
        dot = px * (1.6 if not simple else 1.1)
        d.ellipse([c + 0.5 * px - dot, c - dot, c + 0.5 * px + dot, c + dot], fill=ORANGE)
    else:
        dot = px * (1.4 if not simple else 0.9)
        d.ellipse([c - dot, c - dot, c + dot, c + dot], fill=(0, 0, 0))

    return img.resize((size, size), Image.LANCZOS)


def over_white(px):
    r, g, b, a = px
    a /= 255
    return tuple(round(v * a + 255 * (1 - a)) for v in (r, g, b))


def to_bw(img):
    """1-bit icon and mask rows (lists of 0/1)."""
    w, h = img.size
    icon = [[0] * w for _ in range(h)]
    mask = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            mask[y][x] = 1 if a >= 100 else 0
            lum = 0.3 * r + 0.59 * g + 0.11 * b
            icon[y][x] = 1 if mask[y][x] and lum < 150 else 0
    return icon, mask


def to_8bit(img):
    w, h = img.size
    return [[nearest(over_white(img.getpixel((x, y)))) if img.getpixel((x, y))[3] >= 100 else 0
             for x in range(w)] for y in range(h)]


def hex_rows(rows, per_line):
    out = []
    for row in rows:
        v = 0
        for b in row:
            v = (v << 1) | b
        out.append(f'{v:0{len(row) // 4}X}')
    return [' '.join(out[i:i + per_line]) for i in range(0, len(out), per_line)]


def rez_bw(rtype, size, icon, mask):
    lines = hex_rows(icon + mask, 4 if size == 32 else 8)
    body = '\n'.join(f'    $"{l}"' for l in lines)
    return f"data '{rtype}' (MACNTP_ICON_ID, purgeable) {{\n{body}\n}};\n"


def rez_8(rtype, idx):
    flat = [v for row in idx for v in row]
    lines = [' '.join(f'{v:02X}' for v in flat[i:i + 16]) for i in range(0, len(flat), 16)]
    body = '\n'.join(f'    $"{l}"' for l in lines)
    return f"data '{rtype}' (MACNTP_ICON_ID, purgeable) {{\n{body}\n}};\n"


def main(out_r, out_png):
    big_c, small_c = draw_clock(32, True, False), draw_clock(16, True, True)
    big_bw, small_bw = draw_clock(32, False, False), draw_clock(16, False, True)
    icn, icn_m = to_bw(big_bw)
    ics, ics_m = to_bw(small_bw)
    icl8, ics8 = to_8bit(big_c), to_8bit(small_c)

    with open(out_r, 'w') as f:
        f.write('/* MacNTP icon: an original clock face (generated by tools/make_icons.py). */\n')
        f.write('#ifndef MACNTP_ICON_ID\n#define MACNTP_ICON_ID 128\n#endif\n')
        f.write(rez_bw('ICN#', 32, icn, icn_m))
        f.write(rez_bw('ics#', 16, ics, ics_m))
        f.write(rez_8('icl8', icl8))
        f.write(rez_8('ics8', ics8))

    # Preview: colour and B&W at 8x, on the classic grey desktop.
    scale, pad = 8, 24
    W = pad + 32 * scale + pad + 16 * scale + pad + 32 * scale + pad + 16 * scale + pad
    H = pad + 32 * scale + pad
    prev = Image.new('RGB', (W, H), (170, 170, 204))
    x = pad
    for idx, size in ((icl8, 32), (ics8, 16)):
        im = Image.new('RGB', (size, size))
        for yy in range(size):
            for xx in range(size):
                im.putpixel((xx, yy), PAL[idx[yy][xx]])
        mask_rows = icn_m if size == 32 else ics_m
        m = Image.new('L', (size, size))
        for yy in range(size):
            for xx in range(size):
                m.putpixel((xx, yy), 255 if mask_rows[yy][xx] else 0)
        prev.paste(im.resize((size * scale,) * 2, Image.NEAREST), (x, pad), m.resize((size * scale,) * 2, Image.NEAREST))
        x += size * scale + pad
    for icon, mask, size in ((icn, icn_m, 32), (ics, ics_m, 16)):
        im = Image.new('RGB', (size, size))
        m = Image.new('L', (size, size))
        for yy in range(size):
            for xx in range(size):
                im.putpixel((xx, yy), (0, 0, 0) if icon[yy][xx] else (255, 255, 255))
                m.putpixel((xx, yy), 255 if mask[yy][xx] else 0)
        prev.paste(im.resize((size * scale,) * 2, Image.NEAREST), (x, pad), m.resize((size * scale,) * 2, Image.NEAREST))
        x += size * scale + pad
    prev.save(out_png)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
