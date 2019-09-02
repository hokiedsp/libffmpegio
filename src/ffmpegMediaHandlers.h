#pragma once

extern "C"
{
#include <libavutil/channel_layout.h> // av_get_pix_fmt_name()
#include <libavutil/pixdesc.h>        // av_get_pix_fmt_name()
}

#include "ffmpegMediaHandlerInterfaces.h"
#include "ffmpegMediaStructs.h"

namespace ffmpeg
{

/**
 * ffmpeg::MediaHandler/VideoHandler/AudioHandler
 */
class MediaHandler : virtual public IMediaHandler
{

  protected:
  MediaParams *params; // set to the allocated object by derived class,
                       // destroyed by this class
  MediaHandler(MediaParams *p = nullptr)
      : params(p) {} // only constructible via derived class

  public:
  virtual ~MediaHandler() { delete params; }

  const MediaParams &getMediaParams() const override { return *params; }
  virtual void setMediaParams(const MediaParams &new_params) override
  {
    if (params->type != new_params.type)
      throw Exception("Mismatched media type.");
    *params = new_params;
  }
  void setMediaParams(const IMediaHandler &other) override
  {
    *params = other.getMediaParams();
  }

  AVMediaType getMediaType() const override { return params->type; }
  std::string getMediaTypeString() const override
  {
    return (params->type == AVMEDIA_TYPE_UNKNOWN)
               ? "unknown"
               : av_get_media_type_string(params->type);
  }
  AVRational getTimeBase() const override { return params->time_base; }
  virtual void setTimeBase(const AVRational &tb) override
  {
    params->time_base = tb;
  }

  virtual bool ready() const override
  {
    return params->type != AVMEDIA_TYPE_UNKNOWN && params->time_base.num > 0 &&
           params->time_base.den > 0;
  }
};

struct VideoHandler : virtual public MediaHandler, public IVideoHandler
{
  VideoHandler(const AVRational &tb = {0, 0},
               const AVPixelFormat fmt = AV_PIX_FMT_NONE, const int w = 0,
               const int h = 0, const AVRational &sar = {0, 0})
  {
    if (!params) params = new VideoParams(tb, fmt, w, h, sar);
  }

  virtual void setMediaParams(const MediaParams &new_params) override
  {
    *params = static_cast<const VideoParams &>(new_params);
  }

  AVPixelFormat getFormat() const override
  {
    return static_cast<VideoParams *>(params)->format;
  }
  std::string getFormatName() const override
  {
    return av_get_pix_fmt_name(getFormat());
  }
  const AVPixFmtDescriptor &getFormatDescriptor() const override
  {
    return *av_pix_fmt_desc_get(getFormat());
  }
  int getWidth() const override
  {
    return static_cast<VideoParams *>(params)->width;
  };
  int getHeight() const override
  {
    return static_cast<VideoParams *>(params)->height;
  };
  AVRational getSAR() const override
  {
    return static_cast<VideoParams *>(params)->sample_aspect_ratio;
  }
  AVRational getFrameRate() const override
  {
    return static_cast<VideoParams *>(params)->frame_rate;
  }

  virtual void setFormat(const AVPixelFormat fmt) override
  {
    static_cast<VideoParams *>(params)->format = fmt;
  }
  virtual void setWidth(const int w) override
  {
    static_cast<VideoParams *>(params)->width = w;
  };
  virtual void setHeight(const int h) override
  {
    static_cast<VideoParams *>(params)->height = h;
  };
  virtual void setSAR(const AVRational &sar) override
  {
    static_cast<VideoParams *>(params)->sample_aspect_ratio = sar;
  }
  virtual void setFrameRate(const AVRational &fs) override
  {
    static_cast<VideoParams *>(params)->sample_aspect_ratio = fs;
  }

  virtual bool ready() const override
  {
    VideoParams &vparams = *static_cast<VideoParams *>(params);
    return MediaHandler::ready() && vparams.format != AV_PIX_FMT_NONE &&
           vparams.width != 0 && vparams.height != 0 &&
           vparams.sample_aspect_ratio.den != 0 &&
           vparams.sample_aspect_ratio.num != 0 &&
           vparams.frame_rate.den != 0 && vparams.frame_rate.num != 0;
  }
};

struct AudioHandler : virtual public MediaHandler, public IAudioHandler
{
  AudioHandler(const AVRational &tb = {0, 0},
               const AVSampleFormat fmt = AV_SAMPLE_FMT_NONE,
               const uint64_t layout = 0, const int fs = 0)
  {
    if (!params) params = new AudioParams(tb, fmt, layout, fs);
  }

  void setMediaParams(const MediaParams &new_params) override
  {
    *params = static_cast<const AudioParams &>(new_params);
  }

  AVSampleFormat getFormat() const override
  {
    return static_cast<AudioParams *>(params)->format;
  }
  std::string getFormatName() const override
  {
    return av_get_sample_fmt_name(getFormat());
  }
  int getChannels() const override
  {
    return av_get_channel_layout_nb_channels(
        static_cast<AudioParams *>(params)->channel_layout);
  };
  uint64_t getChannelLayout() const override
  {
    return static_cast<AudioParams *>(params)->channel_layout;
  };
  std::string getChannelLayoutName() const override
  {
    int nb_channels = av_get_channel_layout_nb_channels(
        static_cast<AudioParams *>(params)->channel_layout);
    if (nb_channels)
    {
      char buf[1024];
      av_get_channel_layout_string(
          buf, 1024, nb_channels,
          static_cast<AudioParams *>(params)->channel_layout);
      return buf;
    }
    else
      return "";
  }
  int getSampleRate() const override
  {
    return static_cast<AudioParams *>(params)->sample_rate;
  }

  virtual void setFormat(const AVSampleFormat fmt) override
  {
    static_cast<AudioParams *>(params)->format = fmt;
  }
  virtual void setChannelLayout(const uint64_t layout) override
  {
    static_cast<AudioParams *>(params)->channel_layout = layout;
  };
  virtual void setChannelLayoutByName(const std::string &name) override
  {
    static_cast<AudioParams *>(params)->channel_layout =
        av_get_channel_layout(name.c_str());
  }
  virtual void setSampleRate(const int fs) override
  {
    static_cast<AudioParams *>(params)->sample_rate = fs;
  }

  virtual bool ready() const override
  {
    AudioParams &aparams = *static_cast<AudioParams *>(params);
    return MediaHandler::ready() && aparams.format != AV_SAMPLE_FMT_NONE &&
           !aparams.channel_layout && aparams.sample_rate > 0;
  }
};

} // namespace ffmpeg
