#!/bin/sh
# Render sample screens on a PC: tools/preview.sh <outdir>
set -e
out=${1:-preview}
mkdir -p "$out"
cc -std=gnu11 -O2 -Wall -Isource tools/preview.c source/render.c source/gfx.c \
   source/game.c source/font_data.c -lm -o "$out/preview"
"$out/preview" "$out"
