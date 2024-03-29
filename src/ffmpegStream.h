#pragma once

// #include "ffmpegAvRedefine.h"
#include "ffmpegAVFrameBufferInterfaces.h"
#include "ffmpegMediaHandlers.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
  // #include <libavutil/pixdesc.h>
}

typedef std::vector<AVPixelFormat> AVPixelFormats;

namespace ffmpeg
{
/**
 * \brief Class to manage AVStream
 */
class BaseStream
{
  public:
  BaseStream();
  virtual ~BaseStream();

  virtual int getId() const { return st ? st->index : -1; }

  AVMediaType getMediaType() const
  {
    return ctx ? ctx->codec_type : AVMEDIA_TYPE_UNKNOWN;
  }
  AVRational getTimeBase() const
  {
    return (st) ? st->time_base : (ctx) ? ctx->time_base : AVRational({0, 0});
  }
  virtual void setTimeBase(const AVRational &tb);

  virtual bool ready();

  virtual void close();

  virtual void reset(); // reset decoder states

  const AVCodec *getAVCodec() const { return ctx ? ctx->codec : NULL; }
  std::string getCodecName() const
  {
    return (ctx && ctx->codec && ctx->codec->name) ? ctx->codec->name : "";
  }
  std::string getCodecDescription() const
  {
    return (ctx && ctx->codec && ctx->codec->long_name) ? ctx->codec->long_name
                                                        : "";
  }
  bool getCodecFlags(const int mask) const { return ctx->flags & mask; }

  int getCodecFrameSize() const { return ctx ? ctx->frame_size : 0; }

  AVStream *getAVStream() const { return st; }
  AVCodecParameters *getAVCodecParameters() const { return st->codecpar; }

  static const AVPixelFormats
  get_compliance_unofficial_pix_fmts(AVCodecID codec_id,
                                     const AVPixelFormats default_formats);
  void choose_sample_fmt(); // should be moved to OutputAudioStream when created

  protected:
  AVStream *st;        // stream
  AVCodecContext *ctx; // stream's codec context
};

/**
 * Implements all the functions for IVideoHandler interface
 */
class VideoStream : virtual public BaseStream, public VideoHandler
{
  public:
  virtual ~VideoStream() {}

  /**
   * \brief Synchronize object's MediaParams to AVStream parameters
   *
   * \note Intended to be called when an input stream is opened
   * \note Use open() for setting up an output stream
   */
  void syncMediaParams();

  AVPixelFormat getFormat() const
  {
    return ctx ? ctx->pix_fmt : AV_PIX_FMT_NONE;
  }

  void setMediaParams(const MediaParams &new_params);

  AVMediaType getMediaType() const { return VideoHandler::getMediaType(); }
  AVRational getTimeBase() const { return VideoHandler::getTimeBase(); }

  void setTimeBase(const AVRational &tb) override;

  void setFormat(const AVPixelFormat fmt) override;
  void setWidth(const int w) override;
  void setHeight(const int h) override;
  void setSAR(const AVRational &sar) override;
  void setFrameRate(const AVRational &fs) override;
};

class AudioStream : virtual public BaseStream, public AudioHandler
{
  public:
  virtual ~AudioStream() {}

  void syncMediaParams();

  using AudioHandler::getMediaType;
  AVRational getTimeBase() const override { return AudioHandler::getTimeBase(); }
  void setTimeBase(const AVRational &tb) override;

  void setMediaParams(const MediaParams &new_params) override;
  void setFormat(const AVSampleFormat fmt) override;
  void setChannelLayout(const uint64_t layout) override;
  void setChannelLayoutByName(const std::string &name) override;
  void setSampleRate(const int fs) override;
};
} // namespace ffmpeg
