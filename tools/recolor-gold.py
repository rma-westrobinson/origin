#!/usr/bin/env python3
"""Recolours the original Origin icon art (blue-purple-magenta orb) to gold and black, keeping the shape, the shading and
the white play triangle. The colourful ring pixels are mapped through a gold ramp by their luminance; the near-white
triangle and wordmark stay neutral; the dark centre goes to black. Alpha is untouched. Run ONCE on original-colour art
(committed in git before v3.0.25); running it again on gold art just shifts it a little. Then run tools/gen-icon.py for
the TV banner. Needs Pillow. The app's own colours are not touched: this is the icon only."""
import os, sys, colorsys
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FILES = ['assets/icon.png', 'assets/logo.png', 'assets/splashicon.png', 'packaging/webos/icon.png', 'packaging/webos/largeIcon.png']
# luminance -> gold: black, deep gold, gold, light gold, pale gold
RAMP = [(0.00, (0, 0, 0)), (0.10, (26, 20, 4)), (0.30, (143, 116, 20)), (0.55, (212, 175, 55)), (0.80, (243, 227, 168)), (1.00, (255, 248, 225))]


def ramp(L):
    for (a, ca), (b, cb) in zip(RAMP, RAMP[1:]):
        if L <= b:
            t = (L - a) / (b - a)
            return tuple(ca[i] + (cb[i] - ca[i]) * t for i in range(3))
    return RAMP[-1][1]


def smooth(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def recolor(im):
    im = im.convert('RGBA')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            L = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255
            _, s, _ = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            gold = ramp(L)
            grey = min(255.0, L * 255 * 1.04)  # the triangle and the wordmark: neutral white
            w = smooth(0.15, 0.40, s)
            px[x, y] = tuple(int(round(w * gold[i] + (1 - w) * grey)) for i in range(3)) + (a,)
    return im


if __name__ == '__main__':
    for f in FILES:
        p = os.path.join(ROOT, f)
        recolor(Image.open(p)).save(p, optimize=True)
        print('recoloured', f)
