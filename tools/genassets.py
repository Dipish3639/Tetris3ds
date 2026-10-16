#!/usr/bin/env python3
"""Generate the HOME Menu icon, banner image and banner jingle in meta/.

Usage: python3 tools/genassets.py path/to/Inter-ExtraBold.otf
"""
import math
import struct
import sys
import wave
from PIL import Image, ImageDraw, ImageFont

MINO = {"I": (61, 217, 182), "J": (86, 98, 232), "L": (240, 138, 60), "O": (242, 203, 58),
        "S": (143, 209, 63), "T": (194, 75, 216), "Z": (235, 75, 87), "G": (110, 110, 110)}


def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def mino(draw, x, y, s, col):
    light = tuple(min(255, int(c + (255 - c) * 0.35)) for c in col)
    dark = tuple(int(c * 0.7) for c in col)
    draw.rectangle([x, y, x + s - 1, y + s - 1], fill=col)
    draw.rectangle([x, y, x + s - 1, y + max(1, s // 5) - 1], fill=light)
    draw.rectangle([x, y + s - 1, x + s - 1, y + s - 1], fill=dark)
    draw.rectangle([x + s - 1, y, x + s - 1, y + s - 1], fill=dark)


def icon():
    img = gradient(48, 48, (40, 30, 80), (10, 8, 20))
    d = ImageDraw.Draw(img)
    s = 6
    # A small stack with a T piece about to slot in.
    rows = ["ZZ.LLLJJ",
            "IIII.SSO",
            "GGG.GGGG"]
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch != ".":
                mino(d, c * s, 48 - (3 - r) * s, s, MINO[ch])
    for (c, r) in [(3, 2), (4, 2), (5, 2), (4, 3)]:
        mino(d, c * s, r * s + 4, s, MINO["T"])
    img.save("meta/icon.png")


def banner(font_path):
    w, h = 256, 128
    img = gradient(w, h, (30, 24, 60), (8, 7, 16)).convert("RGBA")
    d = ImageDraw.Draw(img)
    # Faint falling pieces in the background.
    for i, (ch, x, y) in enumerate([("I", 10, 12), ("O", 220, 20), ("S", 30, 92), ("L", 205, 88), ("T", 120, 104)]):
        overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        od = ImageDraw.Draw(overlay)
        col = MINO[ch] + (60,)
        shape = {"I": [(0, 0), (1, 0), (2, 0), (3, 0)], "O": [(0, 0), (1, 0), (0, 1), (1, 1)],
                 "S": [(1, 0), (2, 0), (0, 1), (1, 1)], "L": [(2, 0), (0, 1), (1, 1), (2, 1)],
                 "T": [(1, 0), (0, 1), (1, 1), (2, 1)]}[ch]
        for (cx, cy) in shape:
            od.rectangle([x + cx * 9, y + cy * 9, x + cx * 9 + 7, y + cy * 9 + 7], fill=col)
        img = Image.alpha_composite(img, overlay)
        d = ImageDraw.Draw(img)
    font = ImageFont.truetype(font_path, 40)
    small = ImageFont.truetype(font_path, 12)
    word, cols = "TETRIS", ["Z", "L", "O", "S", "I", "T"]
    spacing = 3
    widths = [font.getlength(c) for c in word]
    x = (w - (sum(widths) + spacing * (len(word) - 1))) / 2
    for c, wd, k in zip(word, widths, cols):
        d.text((x + 2, 34 + 3), c, font=font, fill=(0, 0, 0, 150))
        d.text((x, 34), c, font=font, fill=MINO[k])
        x += wd + spacing
    sub = "3 D S   E D I T I O N"
    d.text(((w - small.getlength(sub)) / 2, 84), sub, font=small, fill=(184, 184, 200))
    img.convert("RGB").save("meta/banner.png")


def jingle():
    rate = 22050
    notes = [(659.25, 0.12), (987.77, 0.12), (1318.5, 0.12), (1975.5, 0.36)]  # E5 B5 E6 B6
    samples = []
    for freq, dur in notes:
        n = int(rate * dur)
        for i in range(n):
            t = i / rate
            env = math.exp(-t * 6) * min(1.0, i / 200)
            v = (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t)) * env
            samples.append(int(max(-1, min(1, v * 0.5)) * 32767))
    samples += [0] * int(rate * 0.5)
    with wave.open("meta/banner.wav", "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(struct.pack("<%dh" % len(samples), *samples))


if __name__ == "__main__":
    icon()
    banner(sys.argv[1])
    jingle()
