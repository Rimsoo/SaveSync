#!/usr/bin/env python3
"""Génère l'icône de SaveSync (homebrew/icon.jpg, 256x256).

Deux flèches circulaires de synchronisation : rouge (côté Switch) et bleue
(côté Steam), sur fond sombre. Dessin suréchantillonné x4 pour l'anticrénelage.

  python tools/make_icon.py [--font chemin/vers/police.ttf]
"""

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIZE = 256
SS = 4  # suréchantillonnage
OUT = Path(__file__).resolve().parent.parent / "homebrew" / "icon.jpg"

BG_TOP = (30, 36, 48)
BG_BOTTOM = (16, 18, 24)
RED = (235, 64, 52)
BLUE = (74, 163, 255)
TEXT = (236, 238, 241)


def arc_arrow(draw, cx, cy, r, start, end, width, color):
    """Arc de cercle de `start` à `end` (degrés, sens horaire) terminé par une pointe."""
    draw.arc([cx - r, cy - r, cx + r, cy + r], start, end, fill=color, width=width)
    # Pillow dessine l'épaisseur vers l'intérieur : l'axe de l'arc est à r - width/2
    rc = r - width / 2
    a = math.radians(end)
    px, py = cx + rc * math.cos(a), cy + rc * math.sin(a)  # bout de l'arc, sur son axe
    tx, ty = -math.sin(a), math.cos(a)                      # sens du mouvement (horaire)
    nx, ny = math.cos(a), math.sin(a)                       # normale, vers l'extérieur
    half, length = width * 1.15, width * 1.5
    draw.polygon([
        (px + nx * half, py + ny * half),
        (px - nx * half, py - ny * half),
        (px + tx * length, py + ty * length),
    ], fill=color)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--font", default="/usr/share/fonts/noto/NotoSans-Bold.ttf")
    args = p.parse_args()

    s = SIZE * SS
    img = Image.new("RGB", (s, s))
    draw = ImageDraw.Draw(img)
    for y in range(s):  # dégradé vertical
        t = y / (s - 1)
        draw.line([(0, y), (s, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))

    cx, cy, r, w = s // 2, int(s * 0.43), int(s * 0.27), int(s * 0.075)
    arc_arrow(draw, cx, cy, r, 200, 320, w, RED)    # haut : vers la droite
    arc_arrow(draw, cx, cy, r, 20, 140, w, BLUE)     # bas : vers la gauche

    font = ImageFont.truetype(args.font, int(s * 0.135))
    label = "SaveSync"
    tw = draw.textlength(label, font=font)
    draw.text(((s - tw) / 2, s * 0.78), label, font=font, fill=TEXT)

    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.save(OUT, "JPEG", quality=95)
    print(f"Icône écrite : {OUT}")


if __name__ == "__main__":
    main()
