#!/bin/sh
# Converts the intro/outro Bink videos to WebM (VP9 + Opus) next to the
# originals in the game folder, for src/port/golf/video.c. Needs ffmpeg (it
# has a Bink decoder). The installed .bik files are 2-second placeholders; pass
# the CD's Flics folder to get the real videos:
#   tools/video/convert-bik.sh "/path/to/cd/Program Files/Flics" game/Flics
set -e
src=${1:?source folder with .bik files}
dst=${2:-game/Flics}
for f in "$src"/*.bik; do
  out="$dst/$(basename "${f%.*}").webm"
  echo "$f -> $out"
  ffmpeg -loglevel error -y -i "$f" -c:v libvpx-vp9 -b:v 1500k -row-mt 1 -deadline good -cpu-used 4 \
    -c:a libopus -b:a 96k "$out"
done
