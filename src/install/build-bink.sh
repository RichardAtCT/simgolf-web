#!/bin/sh
# Builds the in-browser Bink decoder (bink.js / bink.wasm) into $1 (default
# build/pages): FFmpeg (LGPL 2.1+) configured with only the Bink demuxer and
# decoders, plus src/install/bink_wasm.c. Downloads the FFmpeg source once
# into build/ffmpeg.
set -e
cd "$(dirname "$0")/../.."
root=$(pwd)
out=${1:-build/pages}
V=7.1.1
. ~/projects/emsdk/emsdk_env.sh >/dev/null 2>&1 || true
mkdir -p build/ffmpeg "$out"
if [ ! -f build/ffmpeg/out/lib/libavcodec.a ]; then
  [ -d build/ffmpeg/ffmpeg-$V ] || curl -sL https://ffmpeg.org/releases/ffmpeg-$V.tar.xz | tar xJ -C build/ffmpeg
  (cd build/ffmpeg/ffmpeg-$V && emconfigure ./configure --prefix="$root/build/ffmpeg/out" \
    --target-os=none --arch=x86_32 --enable-cross-compile --cc=emcc --cxx=em++ --ar=emar --ranlib=emranlib --nm=emnm \
    --disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-asm --disable-pthreads --disable-x86asm --disable-inline-asm \
    --disable-avdevice --disable-swscale --disable-swresample --disable-avfilter --disable-postproc \
    --enable-decoder=bink,binkaudio_rdft,binkaudio_dct --enable-demuxer=bink --enable-protocol=file \
    --extra-cflags=-O2 >/dev/null && emmake make -j8 >/dev/null && emmake make install >/dev/null)
fi
F=build/ffmpeg/out
emcc -O2 -I$F/include src/install/bink_wasm.c $F/lib/libavformat.a $F/lib/libavcodec.a $F/lib/libavutil.a \
  -sMODULARIZE=1 -sEXPORT_NAME=createBink -sENVIRONMENT=worker -sALLOW_MEMORY_GROWTH=1 \
  -lworkerfs.js -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=ccall,FS,WORKERFS,HEAPU8,HEAPF32 \
  -o "$out/bink.js"
ls -la "$out/bink.js" "$out/bink.wasm"
