#pragma once

#include "ffmpegFilterEndpoints.h"

#include "../ffmpegException.h"
#include "../ffmpegStreamOutput.h"

#include <vector>

namespace ffmpeg
{
namespace filter
{

class SinkBase : public EndpointBase,
                 virtual public MediaHandler,
                 public IAVFrameSource
{
  public:
  SinkBase(Graph &fg,
           IAVFrameSinkBuffer
               &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~SinkBase();

  AVFilterContext *configure(const std::string &name = "");

  // Implementing IAVFrameSource interface
  IAVFrameSinkBuffer &getSinkBuffer() const
  {
    if (sink) return *sink;
    throw Exception("No buffer.");
  }
  void setSinkBuffer(IAVFrameSinkBuffer &buf)
  {
    if (sink) sink->clrSrc();
    sink = &buf;
    sink->setSrc(*this);
  }
  void clrSinkBuffer()
  {
    if (sink)
    {
      sink->clrSrc();
      sink = NULL;
    }
  }
  // end Implementing IAVFrameSource interface

  /**
   * \brief Links the filter to another filter
   *
   * Link this filter with another, overloads the base class function to force
   * the last 2 arguments
   *
   * \param other[inout]  Context of the other filter
   * \param otherpad[in]  The connector pad of the other filter
   * \param pad[in]  [ignore, default:0] The connector pad of this filter
   * \param issrc[in]  [ignore, default:false] Must be false (no output pad)
   *
   * \throws Exception if either \ref pad or \ref issrc arguments are
   * incorrectly given. \throws Exception if either filter context is not
   * ready. \throws Exception if filter contexts are not for the same
   * filtergraph. \throws Exception if failed to link.
   */
  void link(AVFilterContext *other, const unsigned otherpad,
            const unsigned pad = 0, const bool issrc = false);

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   * \returns true if sync success
   */
  virtual bool sync() = 0;

  /**
   * \brief Returns true if media parameters have been synced to the internal
   * AVFilterContext object
   */
  bool isSynced() { return synced; }

  /**
   * \brief Check for existence of an output AVFrame from the filter graph and
   *        if available output it to its sink buffer.
   * \returns True if new frame
   */
  int processFrame();
  // virtual int processFrame(const std::chrono::milliseconds &rel_time);

  virtual void blockTillBufferReady() { sink->blockTillReadyToPush(); }
  virtual bool blockTillBufferReady(const std::chrono::milliseconds &rel_time)
  {
    return sink->blockTillReadyToPush(rel_time);
  }

  virtual bool enabled() const { return ena; };

  protected:

  AVFrame *frame; // buffer

  IAVFrameSinkBuffer *sink;
  bool ena;

  bool synced;

  // make IMediaHandler interface read-only
  using IMediaHandler::setMediaParams;
  using IMediaHandler::setTimeBase;
};

class VideoSink : public SinkBase, public VideoHandler
{
  public:
  VideoSink(Graph &fg,
            IAVFrameSinkBuffer
                &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~VideoSink() {}

  AVFilterContext *configure(const std::string &name = "") override;

  void setPixelFormat(const AVPixelFormat pix_fmt);

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   * \returns true if sync success
   */
  bool sync() override;

  // std::string choose_pix_fmts();
  protected:
  using VideoHandler::setFormat;
  using VideoHandler::setHeight;
  using VideoHandler::setSAR;
  using VideoHandler::setWidth;
};

class AudioSink : public SinkBase, public AudioHandler
{
  public:
  AudioSink(Graph &fg,
            IAVFrameSinkBuffer
                &buf); // connected to a buffer (data from non-FFmpeg source)
  virtual ~AudioSink() {}

  AVFilterContext *configure(const std::string &name = "") override;

  /**
   * \brief Synchronize parameters to the internal AVFilterContext object
   */
  bool sync() override;

  protected: // make AudioHandler read-only
  using AudioHandler::setChannelLayout;
  using AudioHandler::setChannelLayoutByName;
  using AudioHandler::setFormat;
  using AudioHandler::setSampleRate;
};

} // namespace filter
} // namespace ffmpeg