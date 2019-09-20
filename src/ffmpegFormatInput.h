#pragma once

#include <chrono>

#include "ffmpegStreamInput.h"
#include "ffmpegStreamIterator.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace ffmpeg
{

class InputFormat
{
  public:
  InputFormat(const std::string &filename = "");
  virtual ~InputFormat();

  bool isFileOpen() { return fmt_ctx; }
  bool atEndOfFile() { return eof; }
  bool ready() { return fmt_ctx && streams.size(); }

  /**
   * \brief Open a file at the given URL
   * \param[in] url
   * \throws if cannot open the specified URL
   * \throws if cannot retrieve stream info
   */
  void openFile(const std::string &url);
  void closeFile();

  // setting input options
  void setPixelFormat(const AVPixelFormat pix_fmt,
                      const std::string &spec = "");

  InputStream &addStream(const int wanted_stream_id, IAVFrameSinkBuffer &buf,
                         int related_stream_id = -1);
  InputStream &addStream(const AVMediaType type, IAVFrameSinkBuffer &buf,
                         int related_stream_id = -1);
  InputStream &addStream(const std::string &spec, IAVFrameSinkBuffer &buf,
                         int related_stream_id = -1);
  void clearStreams();

  bool isStreamActive(int stream_id) const
  {
    return (fmt_ctx && streams.find(stream_id) != streams.end());
  }

  InputStream &getStream(const int stream_id, const int related_stream_id = -1);
  InputStream &getStream(const AVMediaType type,
                         const int related_stream_id = -1);
  InputStream &getStream(const std::string &spec,
                         const int related_stream_id = -1);

  const InputStream &getStream(const int stream_id,
                               const int related_stream_id = -1) const;
  const InputStream &getStream(const AVMediaType type,
                               const int related_stream_id = -1) const;
  const InputStream &getStream(const std::string &spec,
                               const int related_stream_id = -1) const;

  /**
   * \brief   Get the specifier of the next inactive stream.
   *
   * \param[in] last Pass in the last name returned to go to the next
   * \param[in] type Specify to limit search to a particular media type
   *
   * \returns the index of the next unassigned stream specifier. Returns empty
   * if all have been assigned.
   */
  int getNextInactiveStream(int last = -1,
                            const AVMediaType type = AVMEDIA_TYPE_UNKNOWN);

  // iterators for active streams
  using stream_iterator = StreamIterator<InputStream>;
  using const_stream_iterator =
      StreamIterator<const InputStream,
                     std::unordered_map<int, InputStream *>::const_iterator>;
  using reverse_iterator = std::reverse_iterator<stream_iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_stream_iterator>;

  stream_iterator getStreamBegin() { return stream_iterator(streams.begin()); }
  const_stream_iterator getStreamBegin() const
  {
    return const_stream_iterator(streams.begin());
  }
  const_stream_iterator getStreamCBegin() const
  {
    return const_stream_iterator(streams.cbegin());
  }
  stream_iterator getStreamEnd() { return stream_iterator(streams.begin()); }
  const_stream_iterator getStreamEnd() const
  {
    return const_stream_iterator(streams.end());
  }
  const_stream_iterator getStreamCEnd() const
  {
    return const_stream_iterator(streams.cend());
  }

  // reads next packet from file/stream and push the decoded frame to the
  // stream's sink returns null if eof; else pointer to the stream, which was
  // contained in the packet
  InputStream *readNextPacket();

  std::string getFilePath() const;

  const AVDictionary *getMetadata() const
  {
    return fmt_ctx ? fmt_ctx->metadata : nullptr;
  }

  /**
   * \brief std::chrono duration compatible with FFmpeg time stamps with
   *        AV_TIME_BASE_Q time base
   */
  typedef std::chrono::duration<int64_t, std::ratio<1, AV_TIME_BASE>>
      av_duration;

  /**
   * \brief Returns the duration of the media file in given
   * std::chrono::duration format
   */
  template <typename Chrono_t = av_duration> Chrono_t getDuration() const
  {
    // defined in us in the format context
    if (!fmt_ctx) return Chrono_t(0);

    int64_t T = fmt_ctx->duration;
    if (T <= INT64_MAX - 5000) T += 5000;
    return get_timestamp<Chrono_t>(T, {1, AV_TIME_BASE});
  }

  int64_t getDurationPts(AVRational tb = {0, 0})
  {
    return (tb.den == 0 && tb.num == 0)
               ? fmt_ctx->duration
               : av_rescale_q(fmt_ctx->duration, {1, AV_TIME_BASE}, tb);
  }

  template <typename Chrono_t = av_duration> void seek(const Chrono_t ts)
  {
    if (!isFileOpen()) throw Exception("No file open.");
    int64_t seek_timestamp =
        std::chrono::duration_cast<av_duration>(ts).count();

    // if already at eof, reset the decoders before clearing the flag
    if (eof)
    {
      for (auto &s : streams) s.second->reset();
      eof = false;
    }

    // set new time
    // if filter graph changes frame rate -> convert it to the stream time
    if (int ret = avformat_seek_file(fmt_ctx, -1, INT64_MIN, seek_timestamp,
                                     seek_timestamp, 0) < 0)
      throw Exception("Could not seek to position: " +
                      std::to_string(seek_timestamp));
  }

  void seekPts(int64_t ts, const AVRational tb = {0, 0})
  {
    int ret;
    if ((tb.den == 0 && tb.num == 0) || (tb.den == AV_TIME_BASE && tb.num == 1))
    {
      // use the default time-base
      ret = avformat_seek_file(fmt_ctx, -1, INT64_MIN, ts, ts, 0);
    }
    else
    {
      bool found = false;
      for (auto &s : streams)
      {
        if (!av_cmp_q(tb, s.second->getTimeBase()))
        {
          ret = avformat_seek_file(fmt_ctx, s.second->getId(), INT64_MIN, ts,
                                   ts, 0);
          found = true;
          break;
        }
      }
      if (!found)
      {
        int64_t fmt_ts = av_rescale_q(ts, tb, {1, AV_TIME_BASE});
        ret = avformat_seek_file(fmt_ctx, -1, INT64_MIN, fmt_ts, fmt_ts, 0);
      }
    }

    if (ret < 0)
      throw Exception("Could not seek to position: " + std::to_string(ts));
    
    // if already at eof, reset the decoders before clearing the flag
    if (eof)
    {
      for (auto &s : streams) s.second->reset();
      eof = false;
    }
  }

  int getStreamId(const int stream_id, const int related_stream_id = -1) const;
  int getStreamId(const AVMediaType type,
                  const int related_stream_id = -1) const;
  int getStreamId(const std::string &spec,
                  const int related_stream_id = -1) const;

  AVMediaType getStreamType(const int stream_id) const
  {
    return (fmt_ctx && stream_id >= 0 && stream_id < (int)fmt_ctx->nb_streams)
               ? fmt_ctx->streams[stream_id]->codecpar->codec_type
               : AVMEDIA_TYPE_UNKNOWN;
  }
  AVMediaType getStreamType(const std::string &spec) const
  {
    return getStreamType(getStreamId(spec));
  }

  int getNumberOfStreams() const { return fmt_ctx ? fmt_ctx->nb_streams : 0; }
  size_t getNumberOfActiveStreams() const { return streams.size(); }

  // low-level functions

  AVStream *_get_stream(int stream_id, int related_stream_id = -1)
  {
    return (fmt_ctx && (stream_id < (int)fmt_ctx->nb_streams))
               ? fmt_ctx->streams[stream_id]
               : nullptr;
  }

  private:
  virtual InputStream &add_stream(const int id, IAVFrameSinkBuffer &buf);

  AVFormatContext *fmt_ctx; // FFmpeg format context
  std::unordered_map<int, InputStream *>
      streams; // media streams under decoding
  bool eof;
  AVPacket packet;
};

} // namespace ffmpeg
