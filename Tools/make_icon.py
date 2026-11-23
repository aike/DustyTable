"""Draws the DustyTable application icon (Resources/icon_big.png, Resources/icon_small.png).

Run with Python 3 and Pillow:  python Tools/make_icon.py
The PNGs are committed, so this is only needed when the design changes.
The small icon (used for 16-48 px) has fewer grooves and thicker lines.
"""

import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

BACKGROUND = (23, 21, 26)
PANEL = (34, 31, 38)
ACCENT = (232, 163, 61)
TEXT = (221, 213, 199)
VINYL = (13, 13, 15)
LABEL = (142, 44, 34)


def draw_icon(size, small):
    ss = 4  # supersampling
    n = size * ss
    u = n / 1024.0  # design grid: 1024 x 1024

    def P(x, y):
        return (x * u, y * u)

    def circle(d, cx, cy, r, **kw):
        d.ellipse([(cx - r) * u, (cy - r) * u, (cx + r) * u, (cy + r) * u], **kw)

    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Rounded tile.
    inset = 40 if not small else 24
    d.rounded_rectangle([inset * u, inset * u, (1024 - inset) * u, (1024 - inset) * u],
                        radius=200 * u, fill=PANEL, outline=(70, 64, 74), width=int(10 * u))

    # Record.
    cx, cy, R = 470, 560, 360 if not small else 380
    circle(d, cx, cy, R + 14, fill=(8, 8, 10))
    circle(d, cx, cy, R, fill=VINYL)

    # Sheen: two faint opposite wedges.
    sheen = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sheen)
    box = [(cx - R) * u, (cy - R) * u, (cx + R) * u, (cy + R) * u]
    for start in (-150, 30):
        sd.pieslice(box, start, start + 40, fill=(255, 255, 255, 26))
    sheen = sheen.filter(ImageFilter.GaussianBlur(18 * u))
    img.alpha_composite(sheen)
    d = ImageDraw.Draw(img)

    # Grooves.
    rings = range(165, R - 10, 14) if not small else range(190, R - 20, 46)
    for r in rings:
        circle(d, cx, cy, r, outline=(48, 45, 52), width=max(1, int((3 if not small else 9) * u)))
    # Gaps between tracks.
    for r in ((235, 300) if not small else ()):
        circle(d, cx, cy, r, outline=(5, 5, 6), width=int(6 * u))

    # Label and spindle.
    circle(d, cx, cy, 130, fill=LABEL)
    circle(d, cx, cy, 98, outline=(170, 70, 56), width=int((5 if not small else 10) * u))
    circle(d, cx, cy, 16 if not small else 24, fill=(20, 18, 20))

    # Dust on the record.
    rng = random.Random(7)
    count = 26 if not small else 7
    for _ in range(count):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(150, R - 20)
        s = rng.uniform(4, 10) if not small else rng.uniform(12, 18)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a)
        circle(d, x, y, s + 3, fill=(120, 114, 106))
        circle(d, x, y, s, fill=(216, 209, 196))

    # Tonearm: pivot top right, stylus on the outer grooves.
    pivot = (800, 250)
    head = (606, 772)
    w = 30 if not small else 52
    circle(d, pivot[0], pivot[1], 100, fill=(28, 26, 30), outline=(90, 84, 92), width=int(8 * u))
    d.line([P(*pivot), P(*head)], fill=(18, 16, 20), width=int((w + 22) * u))
    d.line([P(*pivot), P(*head)], fill=ACCENT, width=int(w * u))
    circle(d, pivot[0], pivot[1], w * 0.5, fill=ACCENT)
    # Counterweight behind the pivot.
    dx, dy = pivot[0] - head[0], pivot[1] - head[1]
    L = math.hypot(dx, dy)
    ux, uy = dx / L, dy / L
    cw = (pivot[0] + ux * 70, pivot[1] + uy * 70)
    circle(d, cw[0], cw[1], 54 if not small else 62, fill=TEXT, outline=(18, 16, 20), width=int(10 * u))
    circle(d, pivot[0], pivot[1], 26, fill=(18, 16, 20))
    # Headshell: a small block across the end of the arm.
    px, py = -uy, ux
    hl, hw = 96 if not small else 110, 52 if not small else 66
    hx, hy = head[0] - ux * 20, head[1] - uy * 20
    shell = [P(hx + px * hw - ux * hl, hy + py * hw - uy * hl), P(hx + px * hw, hy + py * hw),
             P(hx - px * hw, hy - py * hw), P(hx - px * hw - ux * hl, hy - py * hw - uy * hl)]
    d.polygon(shell, fill=TEXT, outline=(18, 16, 20), width=int(10 * u))
    # Finger lift.
    lift_a = (hx - px * hw, hy - py * hw)
    d.line([P(*lift_a), P(lift_a[0] - px * 60 + ux * 10, lift_a[1] - py * 60 + uy * 10)],
           fill=TEXT, width=int((12 if not small else 22) * u))

    return img.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Resources")
    os.makedirs(root, exist_ok=True)
    draw_icon(1024, small=False).save(os.path.join(root, "icon_big.png"))
    draw_icon(256, small=True).save(os.path.join(root, "icon_small.png"))


if __name__ == "__main__":
    main()
