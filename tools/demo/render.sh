#!/usr/bin/env bash
# Turn a handheld clip of the board into docs/demo.gif and docs/demo.mp4:
# extract frames, lock the crop onto the board, quantize to a Game Boy palette.
#
#   tools/demo/render.sh IMG_1234.mov
set -euo pipefail

INPUT=$1
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export WORK=$(mktemp -d)
CURVE="0/0 0.1/0.1 0.3/0.18 0.6/0.35 0.85/0.6 1/1"

mkdir -p "$WORK/frames" "$WORK/out" "$ROOT/docs"
ffmpeg -v error -y -i "$INPUT" -vf fps=30 "$WORK/frames/%04d.png"
uv run --quiet --with opencv-python-headless --with numpy python "$ROOT/tools/demo/track.py"

ffmpeg -v error -y \
  -f lavfi -i "color=c=0x0f380f:s=1x1" -f lavfi -i "color=c=0x1e4a1e:s=1x1" \
  -f lavfi -i "color=c=0x306230:s=1x1" -f lavfi -i "color=c=0x5a8a1e:s=1x1" \
  -f lavfi -i "color=c=0x8bac0f:s=1x1" -f lavfi -i "color=c=0x9bbc0f:s=1x1" \
  -filter_complex "[0][1][2][3][4][5]hstack=6,scale=16:16:flags=neighbor" \
  -frames:v 1 "$WORK/palette.png"

ffmpeg -v error -y -framerate 30 -i "$WORK/out/%04d.png" -i "$WORK/palette.png" \
  -filter_complex "[0]fps=15,scale=300:-1:flags=lanczos,curves=all='$CURVE'[v];[v][1]paletteuse=dither=bayer:bayer_scale=2" \
  -loop 0 "$ROOT/docs/demo.gif"

ffmpeg -v error -y -framerate 30 -i "$WORK/out/%04d.png" -i "$WORK/palette.png" \
  -filter_complex "[0]curves=all='$CURVE'[v];[v][1]paletteuse=dither=bayer:bayer_scale=2,format=yuv420p" \
  -c:v libx264 -crf 20 -preset slow -movflags +faststart -an "$ROOT/docs/demo.mp4"

ls -la "$ROOT/docs/demo.gif" "$ROOT/docs/demo.mp4"
