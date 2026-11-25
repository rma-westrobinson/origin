#!/usr/bin/env python3
"""The icons are the original Origin icons recoloured gold and black by tools/recolor-gold.py (assets/logo.png, icon.png,
splashicon.png and packaging/webos/icon.png, largeIcon.png): this script never redraws them. All it does is lay
the logo on black to make the Android TV banner (packaging/android/banner.png, 640x360; the build
resizes it to 320x180), the way the upstream build made it. Run from anywhere: tools/gen-icon.py. Needs Pillow."""
import os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BLACK = (8, 8, 8, 255)  # the banner is gold on black (the app's own page colour is unchanged: src/core/app.h PAGE)


def banner():
    b = Image.new('RGBA', (640, 360), BLACK)
    logo = Image.open(os.path.join(ROOT, 'assets/logo.png')).convert('RGBA')
    k = min(520 / logo.width, 240 / logo.height)
    logo = logo.resize((max(1, int(logo.width * k)), max(1, int(logo.height * k))), Image.LANCZOS)
    b.alpha_composite(logo, ((640 - logo.width) // 2, (360 - logo.height) // 2))
    return b.convert('RGB')


if __name__ == '__main__':
    banner().save(os.path.join(ROOT, 'packaging/android/banner.png'))
    print('banner written')
