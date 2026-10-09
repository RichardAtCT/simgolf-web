// Bink decoder for the in-browser installer (web/pages/install-worker.js),
// over a minimal FFmpeg build (src/install/build-bink.sh). The worker uses it
// to turn the disc's intro and closing .bik into WebM once, during setup.
//
// bk_open(path) opens a file in the module's FS; bk_next() decodes until the
// next frame and returns 1 (video: bk_plane / bk_linesize, I420), 2 (audio:
// bk_samples floats, planar per channel or interleaved in plane 0, see
// bk_audio_layout; bk_nb_samples per channel), 0 at the end, <0 on
// error. bk_pts() is the frame's time in microseconds.
#include <emscripten.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

static AVFormatContext *fmt;
static AVCodecContext *vdec, *adec;
static int vidx = -1, aidx = -1;
static AVPacket *pkt;
static AVFrame *frame;
static int draining_v, draining_a, eof;

static AVCodecContext *open_dec(int idx) {
  AVStream *st = fmt->streams[idx];
  const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
  if (!c) return NULL;
  AVCodecContext *ctx = avcodec_alloc_context3(c);
  avcodec_parameters_to_context(ctx, st->codecpar);
  ctx->pkt_timebase = st->time_base;
  if (avcodec_open2(ctx, c, NULL) < 0) { avcodec_free_context(&ctx); return NULL; }
  return ctx;
}

EMSCRIPTEN_KEEPALIVE int bk_open(const char *path) {
  if (avformat_open_input(&fmt, path, NULL, NULL) < 0) return 0;
  if (avformat_find_stream_info(fmt, NULL) < 0) return 0;
  vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (vidx < 0 || !(vdec = open_dec(vidx))) return 0;
  if (aidx >= 0 && !(adec = open_dec(aidx))) aidx = -1;
  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  return 1;
}

EMSCRIPTEN_KEEPALIVE int bk_width(void) { return vdec->width; }
EMSCRIPTEN_KEEPALIVE int bk_height(void) { return vdec->height; }
EMSCRIPTEN_KEEPALIVE double bk_fps(void) { return av_q2d(fmt->streams[vidx]->avg_frame_rate); }
EMSCRIPTEN_KEEPALIVE int bk_sample_rate(void) { return adec ? adec->sample_rate : 0; }
EMSCRIPTEN_KEEPALIVE int bk_channels(void) { return adec ? adec->ch_layout.nb_channels : 0; }
// Audio sample layout: 1 planar float, 2 interleaved float, 0 anything else
EMSCRIPTEN_KEEPALIVE int bk_audio_layout(void) {
  if (!adec) return 0;
  return adec->sample_fmt == AV_SAMPLE_FMT_FLTP ? 1 : adec->sample_fmt == AV_SAMPLE_FMT_FLT ? 2 : 0;
}

EMSCRIPTEN_KEEPALIVE uint8_t *bk_plane(int i) { return frame->data[i]; }
EMSCRIPTEN_KEEPALIVE int bk_linesize(int i) { return frame->linesize[i]; }
EMSCRIPTEN_KEEPALIVE float *bk_samples(int ch) { return (float *)frame->extended_data[ch]; }
EMSCRIPTEN_KEEPALIVE int bk_nb_samples(void) { return frame->nb_samples; }
EMSCRIPTEN_KEEPALIVE double bk_pts(void) {
  int idx = frame->nb_samples ? aidx : vidx;
  int64_t t = frame->best_effort_timestamp;
  if (t == AV_NOPTS_VALUE) return -1;
  return t * av_q2d(fmt->streams[idx]->time_base) * 1e6;
}

static int receive(AVCodecContext *c, int kind) {
  if (!c) return 0;
  int r = avcodec_receive_frame(c, frame);
  return r == 0 ? kind : 0;
}

EMSCRIPTEN_KEEPALIVE int bk_next(void) {
  for (;;) {
    int k;
    if ((k = receive(vdec, 1))) return k;
    if ((k = receive(adec, 2))) return k;
    if (eof) return 0;
    av_frame_unref(frame);
    int r = av_read_frame(fmt, pkt);
    if (r < 0) {
      eof = 1;
      avcodec_send_packet(vdec, NULL);
      if (adec) avcodec_send_packet(adec, NULL);
      continue;
    }
    if (pkt->stream_index == vidx) avcodec_send_packet(vdec, pkt);
    else if (pkt->stream_index == aidx) avcodec_send_packet(adec, pkt);
    av_packet_unref(pkt);
  }
}

EMSCRIPTEN_KEEPALIVE void bk_close(void) {
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&vdec);
  avcodec_free_context(&adec);
  avformat_close_input(&fmt);
  vidx = aidx = -1;
  eof = 0;
}
