#!/usr/bin/env python3
"""Draws MacNTP's icon (an original control panel with a clock) and writes Rez resources:
   ICN# / ics# (black & white + mask) and icl8 / ics8 (Mac 256-color palette).

   Usage: make_icons.py <output icons.r> <output preview.png> [<output about.r>]
"""
import math
import sys
from PIL import Image, ImageDraw

SS = 16                                    # supersampling factor
ORANGE = (255, 149, 0)


def mac_palette():
    """The standard Macintosh 8-bit color table (clut 8)."""
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
        if gray and not (pr == pg == pb):  # keep grays gray
            continue
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if best is None or d < best:
            best, bi = d, i
    return bi


def draw_panel(size, color, simple):
    """MacNTP's icon, drawn in the style of the Mac OS 8 control panels: a
    light gray beveled panel with a slider strip along the bottom, and the
    symbol (MacNTP's clock) above it. Drawn here from scratch, pixel by
    pixel. Returns an RGBA image at `size`."""
    big = size == 32
    BLACK, WHITE = (0, 0, 0, 255), (255, 255, 255, 255)
    FACE = (221, 221, 221, 255) if color else WHITE
    MID = (136, 136, 136, 255) if color else BLACK
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    p = img.load()

    def rect(x0, y0, x1, y1, c):              # inclusive
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                p[x, y] = c

    n = size - 1
    rect(0, 0, n, n, BLACK)                    # outline
    rect(1, 1, n - 1, n - 1, FACE)
    if color:                                  # bevel: highlight top and left
        rect(1, 1, n - 1, 1, WHITE)
        rect(1, 1, 1, n - 1, WHITE)

    # the slider strip along the bottom
    if big:
        sep, ty, tx0, tx1, kx = 23, 26, 3, 28, 15
    else:
        sep, ty, tx0, tx1, kx = 11, 13, 2, 13, 7
    rect(1, sep, n - 1, sep, MID)              # divider
    if color:
        rect(1, sep + 1, n - 1, sep + 1, WHITE)
    rect(tx0, ty, tx1, ty, MID)                # groove: dark top edge ...
    if big:
        rect(tx0, ty, tx0, ty + 1, MID)
        rect(tx0 + 1, ty + 1, tx1, ty + 1, WHITE if color else FACE)
        rect(tx0, ty + 2, tx1, ty + 2, WHITE if color else BLACK)   # ... light bottom edge
    # the knob: a small raised block standing on the groove
    if big:
        rect(kx - 2, ty - 2, kx + 1, ty + 3, BLACK)
        rect(kx - 1, ty - 1, kx, ty + 2, FACE if color else WHITE)
        if color:
            rect(kx - 1, ty - 1, kx - 1, ty + 2, WHITE)
    else:
        rect(kx - 1, ty - 1, kx, ty + 1, BLACK)

    # the clock, drawn smooth and placed in the symbol area
    if big:
        area = (2, 2, n - 1, sep - 1)          # x0, y0, x1, y1 (exclusive end)
        cx, cy, R = 16.0, 12.0, 9.6
    else:
        area = (1, 1, n, sep)
        cx, cy, R = 8.0, 5.8, 4.6
    S = size * SS
    clk = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(clk)
    px = SS
    c = (cx * px, cy * px)
    Rp = R * px
    d.ellipse([c[0] - Rp, c[1] - Rp, c[0] + Rp, c[1] + Rp], fill=(255, 255, 255), outline=(0, 0, 0),
              width=int(px * (1.0 if big else 0.9)))

    def line(a_deg, r0, r1, width, fill):
        a = math.radians(a_deg)
        xa, ya = c[0] + r0 * math.sin(a), c[1] - r0 * math.cos(a)
        xb, yb = c[0] + r1 * math.sin(a), c[1] - r1 * math.cos(a)
        d.line([(xa, ya), (xb, yb)], fill=fill, width=int(width))
        for (x, y) in ((xa, ya), (xb, yb)):
            d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=fill)

    if not simple:
        for h in range(12):                    # hour marks
            long_mark = h % 3 == 0
            line(h * 30, Rp * (0.62 if long_mark else 0.72), Rp * 0.82,
                 px * (1.2 if long_mark else 0.8),
                 (0, 0, 0) if long_mark or not color else (85, 85, 85))
    line(305, -Rp * 0.06, Rp * (0.48 if not simple else 0.55), px * (1.6 if not simple else 1.0), (0, 0, 0))
    line(60, -Rp * 0.06, Rp * (0.70 if not simple else 0.75), px * (1.3 if not simple else 1.0), (0, 0, 0))
    if color and not simple:                   # orange second hand
        line(180, -Rp * 0.12, Rp * 0.74, px * 1.25, ORANGE)
        dot = px * 1.2
        d.ellipse([c[0] - dot, c[1] - dot, c[0] + dot, c[1] + dot], fill=ORANGE)
    clk = clk.resize((size, size), Image.LANCZOS)
    # place the clock over the panel, inside the symbol area only, snapping
    # each pixel to a solid color (classic icons are crisp, not smoothed)
    for y in range(area[1], area[3]):
        for x in range(area[0], area[2]):
            r, g, b, a = clk.getpixel((x, y))
            if a < 110:
                continue
            lum = 0.3 * r + 0.59 * g + 0.11 * b
            if color and r > 160 and r - b > 90 and r - g > 30:
                p[x, y] = ORANGE + (255,)
            elif lum < 95:
                p[x, y] = BLACK
            elif color and lum < 185:
                p[x, y] = (102, 102, 102, 255)
            elif not color and lum < 140:
                p[x, y] = BLACK
            else:
                p[x, y] = WHITE
    return img


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


def about_pict(icon32):
    """The About box picture: the 32-pixel color icon enlarged 4x (128x128,
    hard pixel edges like a classic icon), written by ImageMagick as a
    version 2 PICT (QuickDraw on System 7 rejects the minimal version 1
    form). Returns Rez hex."""
    import subprocess, tempfile, os
    big = Image.new('RGB', (32, 32), (255, 255, 255))
    for y in range(32):
        for x in range(32):
            big.putpixel((x, y), PAL[icon32[y][x]])
    big = big.resize((128, 128), Image.NEAREST)
    with tempfile.TemporaryDirectory() as t:
        png, pct = os.path.join(t, 'about.png'), os.path.join(t, 'about.pict')
        big.save(png)
        subprocess.check_call(['magick', png, '-colors', '256', 'PICT:' + pct])
        data = open(pct, 'rb').read()[512:]          # drop the file header
    data = (len(data) & 0xFFFF).to_bytes(2, 'big') + data[2:]   # picSize
    return data.hex().upper()


def main(out_r, out_png, out_about=None):
    big_c, small_c = draw_panel(32, True, False), draw_panel(16, True, True)
    big_bw, small_bw = draw_panel(32, False, False), draw_panel(16, False, True)
    icn, icn_m = to_bw(big_bw)
    ics, ics_m = to_bw(small_bw)
    icl8, ics8 = to_8bit(big_c), to_8bit(small_c)

    with open(out_r, 'w') as f:
        f.write('/* MacNTP icon: an original control panel with a clock (generated by tools/make_icons.py). */\n')
        f.write('#ifndef MACNTP_ICON_ID\n#define MACNTP_ICON_ID 128\n#endif\n')
        f.write(rez_bw('ICN#', 32, icn, icn_m))
        lines = hex_rows(icn, 4)                       # 'ICON': the 32x32 picture alone (dialogs)
        body = '\n'.join(f'    $"{l}"' for l in lines)
        f.write(f"data 'ICON' (MACNTP_ICON_ID, purgeable) {{\n{body}\n}};\n")
        f.write(rez_bw('ics#', 16, ics, ics_m))
        f.write(rez_8('icl8', icl8))
        f.write(rez_8('ics8', ics8))

    if out_about:
        # white outside the icon's mask, so the picture's corners match the dialog
        masked = [[icl8[y][x] if icn_m[y][x] else 0 for x in range(32)] for y in range(32)]
        hexdata = about_pict(masked)
        lines = [hexdata[i:i + 64] for i in range(0, len(hexdata), 64)]
        body = '\n'.join(f'    $"{l}"' for l in lines)
        with open(out_about, 'w') as f:
            f.write('/* The About box picture (generated by tools/make_icons.py). */\n')
            f.write(f"data 'PICT' (-4062, purgeable) {{\n{body}\n}};\n")

    # Preview: color and B&W at 8x, on the classic gray desktop.
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
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
