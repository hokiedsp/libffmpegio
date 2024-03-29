#include "ffmpegFilterSinks.h"

#include "../ffmpegException.h"
#include "ffmpegFilterGraph.h"

extern "C"
{
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
}

#include <iomanip>
#include <sstream> // std::stringstream

using namespace ffmpeg;
using namespace ffmpeg::filter;

///////////////////////////////////////////////////////////
SinkBase::SinkBase(Graph &fg, IAVFrameSinkBuffer &buf)
    : EndpointBase(fg), sink(&buf), ena(false), synced(false)
{
  frame = av_frame_alloc();
  if (!frame)
    throw Exception("Failed to allocate AVFrame buffer for a sink filter.");
}
SinkBase::~SinkBase() {}

AVFilterContext *SinkBase::configure(const std::string &name)
{
  ena = true;
  return configure_prefilter(false);
}

void SinkBase::link(AVFilterContext *other, const unsigned otherpad,
                    const unsigned pad, const bool issrc)
{
  if (issrc || pad > 0)
    throw Exception(
        "Sink filter does not have a input pad and has only one output pad.");

  EndpointBase::link(other, otherpad, prefilter_pad, issrc);
}

int SinkBase::processFrame()
{
  if (!ena) return AVERROR_EOF;
  int ret = av_buffersink_get_frame(context, frame);

  if (ret >= 0)
  {
    sink->push(frame); // new frame already placed, complete the transaction
    av_frame_unref(frame);
  }
  else if (ret == AVERROR_EOF)
  {
    ena = true;
    sink->push(nullptr);
  }
  else if (ret != AVERROR(EAGAIN)) // only push if frame was ready
  {
    throw Exception(ret);
  }
  return ret;
}

////////////////////////////////
VideoSink::VideoSink(Graph &fg, IAVFrameSinkBuffer &buf) : SinkBase(fg, buf) {}
AVFilterContext *VideoSink::configure(const std::string &name)
{ // configure the AVFilterContext
  create_context("buffersink", name);
  return SinkBase::configure();
}

void VideoSink::setPixelFormat(const AVPixelFormat pix_fmt)
{
  AVPixelFormat pix_fmts[] = {pix_fmt, AV_PIX_FMT_NONE};
  int ret = av_opt_set_int_list(context, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE,
                                AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) throw Exception("Cannot set output pixel format.");
}

bool VideoSink::sync()
{
  if (context && context->inputs[0])
  {
    VideoParams &p = *static_cast<VideoParams *>(params);
    p.time_base = av_buffersink_get_time_base(context);
    p.format = (AVPixelFormat)av_buffersink_get_format(context);
    p.width = av_buffersink_get_w(context);
    p.height = av_buffersink_get_h(context);
    p.sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(context);
    p.frame_rate = av_buffersink_get_frame_rate(context);
    synced = true;
  }
  else
  {
    {
      synced = false;
    }
  }
  return synced;
}

////////////////////////////////
AudioSink::AudioSink(Graph &fg, IAVFrameSinkBuffer &buf) : SinkBase(fg, buf) {}
AVFilterContext *AudioSink::configure(const std::string &name)
{
  // configure the filter
  create_context("abuffersink", name);
  av_opt_set_int(context, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN);

  // clears ena flag
  return SinkBase::configure();
}

bool AudioSink::sync()
{
  if (context && context->inputs[0])
  {
    AudioParams &p = *static_cast<AudioParams *>(params);

    p.time_base = av_buffersink_get_time_base(context);
    p.format = (AVSampleFormat)av_buffersink_get_format(context);
    p.channel_layout = av_buffersink_get_channel_layout(context);
    p.sample_rate = av_buffersink_get_sample_rate(context);
    synced = true;
  }
  else
  {
    synced = false;
  }
  return true;

  // if linked to a stream
  // if (st)
  // {
  //   const AVCodec *enc = st->getAVCodec();
  //   if (!enc)
  //     throw Exception("Encoder (codec %s) not found for an output stream",
  //                           avcodec_get_name(st->getAVStream()->codecpar->codec_id));

  //   if (!(enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
  //     av_buffersink_set_frame_size(context, st->getCodecFrameSize());
  // }
}
