#!/usr/bin/env python3
"""Generate the 1920x1080 24-bit test BMP used to bring up HDMI from BOLT.

Bars left->right: red, green, blue. Corner squares: top-left white,
top-right yellow, bottom-left cyan, bottom-right magenta.
Usage: make_test_bmp.py [out.bmp]   (needs Pillow)
"""
import sys
from PIL import Image, ImageDraw

W, H, S = 1920, 1080, 160
im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
d.rectangle([0, 0, W // 3 - 1, H - 1], fill=(255, 0, 0))
d.rectangle([W // 3, 0, 2 * W // 3 - 1, H - 1], fill=(0, 255, 0))
d.rectangle([2 * W // 3, 0, W - 1, H - 1], fill=(0, 0, 255))
d.rectangle([0, 0, S, S], fill=(255, 255, 255))
d.rectangle([W - 1 - S, 0, W - 1, S], fill=(255, 255, 0))
d.rectangle([0, H - 1 - S, S, H - 1], fill=(0, 255, 255))
d.rectangle([W - 1 - S, H - 1 - S, W - 1, H - 1], fill=(255, 0, 255))
im.save(sys.argv[1] if len(sys.argv) > 1 else "test1080.bmp")
