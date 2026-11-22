#!/bin/sh
set -eu
PATH="$(cygpath -u "$2"):$PATH"
export PATH
if [ "$1" = make ]; then
    "$(cygpath -u "$3")" -j6
    test -s libavformat/avformat.lib && test -s libavcodec/avcodec.lib && test -s libavutil/avutil.lib &&
        test -s libswresample/swresample.lib && test -s libswscale/swscale.lib
    exit
fi
source_dir=$(cygpath -u "$3")

# Local playback and packet-copy editing. ImageWalker owns all input and
# output; do not enable network or file protocols, encoders, or programs.
exec "$source_dir/configure" \
    --toolchain=msvc --arch="$4" --disable-x86asm --disable-asm \
    --disable-all --disable-autodetect --disable-network --enable-avformat --enable-avcodec --enable-avutil \
    --enable-swscale --enable-swresample \
    --enable-static --disable-shared --disable-doc --disable-programs --disable-debug \
    --extra-cflags='-MT -Zl' --extra-ldflags='libcmt.lib' \
    --enable-demuxer=aac,aiff,asf,avi,flac,flv,matroska,mov,mp3,mpegps,mpegts,ogg,wav \
    --enable-muxer=adts,aiff,asf,avi,flac,flv,ipod,matroska,mov,mp3,mp4,mpegts,ogg,opus,tg2,tgp,wav,webm \
    --enable-parser=aac,aac_latm,ac3,dca,av1,flac,h264,hevc,mpegaudio,mpeg4video,mpegvideo,opus,vorbis,vc1,vp8,vp9 \
    --enable-decoder=h264,hevc,mpeg1video,mpeg2video,mpeg4,h263,h263i,msmpeg4v2,msmpeg4v3,vc1,wmv1,wmv2,wmv3 \
    --enable-decoder=vp8,vp9,rawvideo,mjpeg,flv,ffv1,theora,dvvideo,prores \
    --enable-decoder=aac,aac_latm,ac3,eac3,alac,dca,flac,mp2,mp3,opus,vorbis,wmav1,wmav2,wmapro,wmalossless \
    --enable-decoder=pcm_*,adpcm_ms,adpcm_ima_wav
