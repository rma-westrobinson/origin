#!/usr/bin/env python3
"""Draws the Stremio Plus icon (a play triangle and a "+", on a deep indigo tile with a soft arc, in the manner of Disney
Plus) and writes every size the packages use. Run from anywhere: tools/gen-icon.py. Needs Pillow."""
import os, math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
S = 4  # supersampling
N = 512 * S


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def tile():
    """the 512x512 icon, rounded-square, transparent corners"""
    img = Image.new('RGBA', (N, N))
    px = ImageDraw.Draw(img)
    top, bot = (38, 22, 120), (8, 6, 36)
    for y in range(N):
        px.line([(0, y), (N, y)], fill=lerp(top, bot, y / N) + (255,))
    # soft violet glow, upper left
    glow = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([-N * 0.25, -N * 0.35, N * 0.75, N * 0.55], fill=(124, 77, 255, 90))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(N * 0.16)))
    # the arc: a wide thin ring crossing the tile low, as on the Disney Plus mark
    arc = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ad = ImageDraw.Draw(arc)
    cx, cy, r = N * 0.5, N * 1.45, N * 0.80
    for i, w in enumerate((N * 0.012, N * 0.006)):
        rr = r + i * N * 0.05
        ad.arc([cx - rr, cy - rr, cx + rr, cy + rr], 200, 340, fill=(170, 140, 255, 150 - 60 * i), width=int(w))
    img.alpha_composite(arc.filter(ImageFilter.GaussianBlur(N * 0.002)))
    # the glyph: a rounded play triangle, then the plus
    g = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    u = N / 512
    tri = [(112 * u, 160 * u), (112 * u, 352 * u), (268 * u, 256 * u)]
    gd.polygon(tri, fill=(255, 255, 255, 255))
    # round the corners: blur the shape, keep what is still more than half there
    al = g.split()[3].filter(ImageFilter.GaussianBlur(14 * u)).point(lambda v: 255 if v > 150 else 0)
    al = al.filter(ImageFilter.GaussianBlur(1.2))
    g = Image.new('RGBA', (N, N), (255, 255, 255, 0))
    g.putalpha(al)
    gd = ImageDraw.Draw(g)
    pcx, pcy, pw, pt = 376 * u, 256 * u, 128 * u, 36 * u
    gd.rounded_rectangle([pcx - pw / 2, pcy - pt / 2, pcx + pw / 2, pcy + pt / 2], radius=pt / 2, fill=(255, 255, 255, 255))
    gd.rounded_rectangle([pcx - pt / 2, pcy - pw / 2, pcx + pt / 2, pcy + pw / 2], radius=pt / 2, fill=(255, 255, 255, 255))
    sh = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    sh.paste((0, 0, 20, 140), mask=g.split()[3])
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(N * 0.012)), (0, int(6 * u)))
    img.alpha_composite(g)
    # rounded-square mask
    m = Image.new('L', (N, N), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, N - 1, N - 1], radius=int(N * 0.225), fill=255)
    out = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    out.paste(img, mask=m)
    return out.resize((512, 512), Image.LANCZOS)


def banner(icon):
    """the Android TV banner, 640x360: the tile, the name, on the same indigo"""
    W, H = 640, 360
    b = Image.new('RGBA', (W, H))
    d = ImageDraw.Draw(b)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=lerp((30, 18, 100), (8, 6, 36), y / H) + (255,))
    t = icon.resize((200, 200), Image.LANCZOS)
    b.alpha_composite(t, (60, 80))
    f = None
    for p in (os.path.join(ROOT, 'assets/fonts/OnestVariable.ttf'),):
        try:
            f = ImageFont.truetype(p, 64)
            try:
                f.set_variation_by_axes([700])
            except Exception:
                pass
        except Exception:
            pass
    f = f or ImageFont.load_default()
    d.text((290, 120), 'Stremio', font=f, fill=(255, 255, 255, 255))
    d.text((290, 192), 'Plus', font=f, fill=(190, 165, 255, 255))
    return b.convert('RGB')


if __name__ == '__main__':
    ic = tile()
    ic.save(os.path.join(ROOT, 'assets/logo.png'))
    ic.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, 'assets/icon.png'))
    ic.resize((400, 400), Image.LANCZOS).save(os.path.join(ROOT, 'assets/splashicon.png'))
    ic.resize((80, 80), Image.LANCZOS).save(os.path.join(ROOT, 'packaging/webos/icon.png'))
    ic.resize((130, 130), Image.LANCZOS).save(os.path.join(ROOT, 'packaging/webos/largeIcon.png'))
    os.makedirs(os.path.join(ROOT, 'packaging/android'), exist_ok=True)
    banner(ic).save(os.path.join(ROOT, 'packaging/android/banner.png'))
    print('icons written')
