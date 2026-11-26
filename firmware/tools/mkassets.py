#!/usr/bin/env python3
"""MorseOS bitmap assets: big clock digits and menu icons, as 4-bit antialiased C arrays.

  mkassets.py <morseos dir> [--preview out.png]

Writes:
  apps/face/clock_font.h   digits 0-9, 19 x 34 px, Ubuntu Condensed Bold (Ubuntu Font
                           Licence 1.0: embedding in a firmware is allowed)
  lib/icons/icons_data.h   32 x 32 px icons, drawn here at 4x and scaled down
  lib/aafont/aafont_data.h proportional text fonts for the watch face, Ubuntu Condensed Bold:
                           medium (20 px: date, typed letters), small (10 px: top bar)

Pixel levels 0 = background .. 15 = foreground, row-major, stored run-length coded for
lcd_blit_rle (lib/display), one code byte each (0.8.4, 5790 -> 3038 B, no loss):
  00nnnnnn  n+1 pixels of 0        01nnnnnn  n+1 pixels of 15
  1vvvvnnn  n+1 pixels of level v (the antialiased edges: short runs of one level)
(measured: grouping the edge levels as packed literals costs more, 3921 B, because the
groups are 1-2 pixels long; XOR with the row above helps the digits a little and hurts
the icons, whose edges move from row to row)
Needs Pillow; run it again after changing a drawing.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/ubuntu/Ubuntu[wdth,wght].ttf"
FONT_VAR = "Condensed Bold"
CELL_W = 19
ICON = 32
SS = 4  # supersampling for the icons


def pack4(im):
    px = [round(v * 15 / 255) for v in im.getdata()]
    if len(px) % 2:
        px.append(0)
    return bytes(px[i] << 4 | px[i + 1] for i in range(0, len(px), 2))


def rle(im):
    px = [round(v * 15 / 255) for v in im.getdata()]
    out, i = bytearray(), 0
    while i < len(px):
        v, j = px[i], i
        lim = 64 if v in (0, 15) else 8
        while j < len(px) and px[j] == v and j - i < lim:
            j += 1
        out.append((0x40 if v else 0) | (j - i - 1) if v in (0, 15) else 0x80 | v << 3 | (j - i - 1))
        i = j
    return bytes(out)


def unrle(data, n):  # check
    px = []
    for b in data:
        if b < 0x80:
            px += [15 if b & 0x40 else 0] * ((b & 0x3F) + 1)
        else:
            px += [b >> 3 & 15] * ((b & 7) + 1)
    assert len(px) == n, (len(px), n)
    return px


def c_array(name, data):
    out = [f"static const u8 {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        out.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    out.append("};")
    return "\n".join(out)


def digits():
    best = None
    for s in range(20, 90):
        ft = ImageFont.truetype(FONT, s)
        ft.set_variation_by_name(FONT_VAR)
        if max(ft.getbbox(c)[2] - ft.getbbox(c)[0] for c in "0123456789") > CELL_W:
            break
        best = ft
    ft = best
    top, bot = ft.getbbox("0123456789")[1], ft.getbbox("0123456789")[3]
    h = bot - top
    ims = []
    for c in "0123456789":
        b = ft.getbbox(c)
        im = Image.new("L", (CELL_W, h), 0)
        ImageDraw.Draw(im).text(((CELL_W - (b[2] - b[0])) // 2 - b[0], -top), c, font=ft, fill=255)
        ims.append(im)
    return ims, h


# ---------------------------------------------------------------- icons (drawn on 128 x 128)

def canvas():
    im = Image.new("L", (ICON * SS, ICON * SS), 0)
    return im, ImageDraw.Draw(im)


def icon_info():
    im, d = canvas()
    d.ellipse((6, 6, 122, 122), fill=255)
    d.ellipse((18, 18, 110, 110), fill=0)
    d.ellipse((55, 30, 73, 48), fill=255)
    d.rounded_rectangle((55, 56, 73, 98), 4, fill=255)
    return im


def icon_tools():
    """wrench, diagonal: open head top right, handle to the bottom left"""
    im, d = canvas()
    w = Image.new("L", im.size, 0)
    dw = ImageDraw.Draw(w)
    dw.rounded_rectangle((50, 30, 78, 128), 14, fill=255)     # handle
    dw.ellipse((24, -2, 104, 78), fill=255)                    # head
    dw.rectangle((52, -4, 76, 34), fill=0)                     # jaw opening
    dw.ellipse((56, 100, 72, 116), fill=0)                     # hole in the handle
    w = w.rotate(-45, resample=Image.BICUBIC, center=(64, 64))
    im.paste(w)
    return im


def icon_games():
    """gamepad: body with grips, cross on the left, two buttons on the right"""
    im, d = canvas()
    d.rounded_rectangle((4, 34, 124, 94), 28, fill=255)
    d.ellipse((4, 54, 50, 112), fill=255)                      # left grip
    d.ellipse((78, 54, 124, 112), fill=255)                    # right grip
    d.rectangle((24, 56, 50, 66), fill=0)                      # cross
    d.rectangle((32, 48, 42, 74), fill=0)
    d.ellipse((82, 46, 96, 60), fill=0)                        # buttons
    d.ellipse((96, 62, 110, 76), fill=0)
    return im


def icon_settings():
    """gear: 8 teeth, hole in the middle"""
    import math
    im, d = canvas()
    c = 64
    for i in range(8):
        a = i * math.pi / 4
        pts = []
        for da, r in ((-0.22, 40), (-0.16, 60), (0.16, 60), (0.22, 40)):
            pts.append((c + r * math.cos(a + da), c + r * math.sin(a + da)))
        d.polygon(pts, fill=255)
    d.ellipse((c - 44, c - 44, c + 44, c + 44), fill=255)
    d.ellipse((c - 18, c - 18, c + 18, c + 18), fill=0)
    return im


def icon_back():
    """curved back arrow (U-turn, like "return"): head on the top left pointing left, the
    shaft goes right, turns down around a half circle and comes back left at the bottom"""
    im, d = canvas()
    t = 20                                                     # stroke
    d.arc((46, 26, 122, 112), -90, 90, fill=255, width=t)      # the turn, on the right
    d.rectangle((44, 26, 84, 26 + t), fill=255)                # top shaft
    d.rectangle((26, 112 - t, 84, 112), fill=255)              # bottom shaft
    d.polygon([(2, 36), (50, 0), (50, 72)], fill=255)          # head, pointing left
    return im


TEXT_FONTS = [  # name, size, characters (upper case only: the band shows text in capitals)
    ("medium", 20, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ?!:-.%_~<"),
    ("small", 10, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ?-."),
]


def text_font(size, chars):
    """glyph cells: the advance width, from the top of the capitals to the baseline"""
    ft = ImageFont.truetype(FONT, size)
    ft.set_variation_by_name(FONT_VAR)
    top = ft.getbbox("HO0")[1]
    h = ft.getbbox("HO0")[3] - top
    glyphs = []
    for c in chars:
        w = max(1, round(ft.getlength(c)))
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((0, -top), c, font=ft, fill=255)
        glyphs.append((c, im))
    return h, glyphs


BELL = 10  # watch face mark: alarms are set


def icon_bell():
    """a small bell, drawn on 40 x 40 (4x) and scaled to 10 x 10"""
    im = Image.new("L", (BELL * 4, BELL * 4), 0)
    d = ImageDraw.Draw(im)
    d.ellipse((17, 0, 23, 6), fill=255)                        # knob
    d.pieslice((6, 3, 34, 31), 180, 360, fill=255)             # dome
    d.rectangle((6, 17, 34, 28), fill=255)                     # body
    d.polygon([(6, 26), (34, 26), (39, 32), (1, 32)], fill=255)  # flared rim
    d.ellipse((15, 32, 25, 40), fill=255)                      # clapper
    return im.resize((BELL, BELL), Image.LANCZOS)


ICONS = [
    ("INFO", "info", icon_info),
    ("STRUMENTI", "tools", icon_tools),
    ("GIOCHI", "games", icon_games),
    ("IMPOSTAZIONI", "settings", icon_settings),
    ("", "back", icon_back),
]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    root = sys.argv[1]
    preview = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--preview" else None
    me = os.path.basename(__file__)

    ims, h = digits()
    codes = [rle(i) for i in ims]
    for c, im in zip(codes, ims):
        assert unrle(c, CELL_W * h) == [round(v * 15 / 255) for v in im.getdata()]
    offs, o = [], 0
    for c in codes:
        offs.append(o)
        o += len(c)
    with open(os.path.join(root, "apps/face/clock_font.h"), "w") as f:
        f.write(f"/* generated by tools/{me}: do not edit. Ubuntu Condensed Bold digits, 4-bit\n"
                " * antialiased, run-length coded (lcd_blit_rle). Ubuntu Font Licence 1.0. */\n")
        f.write(f"#define CLOCK_W {CELL_W}\n#define CLOCK_H {h}\n")
        f.write(c_array("clock_digits", b"".join(codes)) + "\n")
        f.write("static const u16 clock_off[10] = {" + ", ".join(map(str, offs)) + "};\n")
        f.write("#define CLOCK_DIGIT(d) (clock_digits + clock_off[d])\n")

    icons = []
    for label, name, fn in ICONS:
        icons.append((label, name, fn().resize((ICON, ICON), Image.LANCZOS)))
    os.makedirs(os.path.join(root, "lib/icons"), exist_ok=True)
    with open(os.path.join(root, "lib/icons/icons_data.h"), "w") as f:
        f.write(f"/* generated by tools/{me}: do not edit. {ICON} x {ICON} icons, 4-bit, "
                "run-length coded (lcd_blit_rle) */\n")
        for label, name, im in icons:
            c = rle(im)
            assert unrle(c, ICON * ICON) == [round(v * 15 / 255) for v in im.getdata()]
            f.write(c_array(f"icon_{name}_bits", c) + "\n")
        c = rle(icon_bell())
        f.write(f"#define ICON_BELL_SIZE {BELL}\n" + c_array("icon_bell_bits", c) + "\n")
        f.write("static const struct { const char *label; const u8 *bits; } icon_table[] = {\n")
        for label, name, im in icons:
            if label:
                f.write(f'    {{"{label}", icon_{name}_bits}},\n')
        f.write("};\n")

    with open(os.path.join(root, "lib/aafont/aafont_data.h"), "w") as f:
        f.write(f"/* generated by tools/{me}: do not edit. Ubuntu Condensed Bold, 4-bit, run-length\n"
                " * coded glyphs (lcd_blit_rle). Ubuntu Font Licence 1.0. */\n")
        for name, size, chars in TEXT_FONTS:
            h, glyphs = text_font(size, chars)
            codes, offs, o = [], [], 0
            for c, im in glyphs:
                code = rle(im)
                assert unrle(code, im.width * h) == [round(v * 15 / 255) for v in im.getdata()]
                offs.append(o)
                o += len(code)
                codes.append(code)
            esc = chars.replace("\\", "\\\\").replace('"', '\\"')
            f.write(c_array(f"{name}_data", b"".join(codes)) + "\n")
            f.write(f"static const u8 {name}_w[{len(glyphs)}] = {{" +
                    ", ".join(str(im.width) for _, im in glyphs) + "};\n")
            f.write(f"static const u16 {name}_off[{len(glyphs)}] = {{" + ", ".join(map(str, offs)) + "};\n")
            f.write(f'const aafont_t aafont_{name} = {{{h}, "{esc}", {name}_w, {name}_off, {name}_data}};\n')

    if preview:
        out = Image.new("L", (CELL_W * 10 + (ICON + 4) * len(icons), max(h, ICON)), 40)
        for i, im in enumerate(ims):
            out.paste(im, (i * CELL_W, 0))
        for i, (_, _, im) in enumerate(icons):
            out.paste(im, (CELL_W * 10 + 4 + i * (ICON + 4), 0))
        out.paste(icon_bell().resize((BELL * 3, BELL * 3), Image.NEAREST), (CELL_W * 10 - 32, 2))
        out.resize((out.width * 4, out.height * 4), Image.NEAREST).save(preview)


if __name__ == "__main__":
    main()
