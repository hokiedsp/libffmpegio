#pragma once

#include "ffmpegReaderMT.h"
#include "ffmpegTimeUtil.h"

namespace ffmpeg
{

// multi-threaded media file reader in reverse direction
// note: requires to impose uniform size for fixed-size buffers
// note: utilizes setBeforeSwapCallback() & setReachedEofCallback() features of
// AVFrameDoubleBuffer to reset the clock after each buffer is filled
template <typename BufferType> class ReaderReverse : public ReaderMT<BufferType>
{
  public:
  ReaderReverse(const std::string &url = "") : ReaderMT<BufferType>(url) {}
  virtual ~ReaderReverse() {}

  void activate();

  template <class Chrono_t>
  void seek(const Chrono_t t0, const bool exact_search = true);

  protected:
  int64_t get_pts(AVFrame *frame);

  private:
  int64_t pts_last;  // last buffer timestamp
  int64_t pts_curr;  // next buffer timestamp
  int64_t pts_delta; // expected duration covered by a full buffer
  int64_t
      pts_offset;   // block offset so that the first block contains full buffer
  BufferType *pbuf; // pointer to the primary buffer (valid after activation)
  AVRational tb;    // primary stream timebase

  void seek_pts(int64_t pts_new);
  void cb_postswap(BufferType &buf);
};

template <typename BufferType> void ReaderReverse<BufferType>::activate()
{
  if (active) return;

  // there must be exactly 1 of a fixed-size buffer
  if (prim_buf.empty())
    throw Exception(
        "Primary stream with a fixed-size buffer must be assigned.");

  if (std::reduce(bufs.begin(), bufs.end(),
                  std::reduce(filter_outbufs.begin(), filter_outbufs.end(), 0,
                              [this](auto val, const auto &buf) {
                                return val + !buf.second.autoexpand();
                              }),
                  [](auto val, const auto &buf) {
                    return val + !buf.second.autoexpand();
                  }) != 1)
    throw Exception("Only the primary stream to have a fixed-size buffer. "
                    "Others must be assgined to dynamic buffers.");

  // activate as a forward reader
  ReaderMT<BufferType>::activate();

  // record the primary stream configuration
  auto &s = getStream(prim_buf);
  pbuf = &dynamic_cast<BufferType &>(s.getSinkBuffer());
  tb = s.getTimeBase(); // record the timebase

  // evaluate the frames
  AVFrame *frame = pbuf->peekToPop();

  // compute the pts decrement factor
  pts_delta = pbuf->capacity() * frame->pkt_duration;
  int64_t pts_end = file.getDurationPts(tb);
  pts_offset = pts_end - (pts_end / pts_delta) * pts_delta;

  // NOTE: this info must be obtained from actual frame to be compatible with
  // filtergraph outputs

  // momentarily stop the reader thread
  pause();

  // set callbacks
  using namespace std::placeholders;
  pbuf->setPostSwapCallback(std::bind(&ReaderReverse::cb_postswap, this, _1));

  // set to retrieve the last block
  seek_pts(pts_end);
}

template <typename BufferType>
inline void ReaderReverse<BufferType>::seek_pts(int64_t pts_new)
{
  // expects thread has already been paused

  // clear buffers
  flush();

  // set new block starting pts to be the largest integer multiple of pts_delta
  // to include the given pts
  pts_curr = pts_last =
      ((pts_new - pts_offset) / pts_delta) * pts_delta + pts_offset;

  // call the swap callback to set the time before resuming
  cb_postswap(*pbuf);

  // restart the thread
  ThreadBase::resume();
}

template <typename BufferType>
void ReaderReverse<BufferType>::cb_postswap(BufferType &buf)
{
  AVFrame *f;

  // make sure that the new block in sndr buffer are all < pts_last
  if (pts_curr != pts_last) // skip when called from seek_pts()
  {
    // TODO: Need to do this check for all buffers
    while (!killnow && (f = buf.peekToPop()) &&
           f->best_effort_timestamp >= pts_last)
      buf.pop();
    if (killnow) return;
    buf.clrEof();
  }

  // update pts markers
  pts_last = pts_curr;

  // bof reached, stop
  if (pts_curr == 0)
  {
    // stop the reader thread
    status = THREAD_STATUS::PAUSE_RQ;

    // push "eof" (bof to be exact)
    for (auto &b : bufs) b.second.push(nullptr);
    for (auto &b : filter_outbufs) b.second.push(nullptr);

    return;
  }

  pts_curr = (pts_curr > pts_delta) ? pts_curr - pts_delta : 0;

  // seek to new pts
  file.seekPts(pts_curr, tb);

  // time finetuning
  while (!killnow && Reader::read_next_packet())
  {
    if (!killnow && (f = buf.peekLastPushed()))
    {
      if (pts_curr <= f->best_effort_timestamp) break;
      buf.popLastPushed();
    }
  }

  if (killnow) return;

  // clear all received frames from the other buffers
  for (auto &b : bufs)
  {
    if (&b.second != &buf) b.second.clearRcvr();
  }
  for (auto &b : filter_outbufs)
  {
    if (&b.second != &buf) b.second.clearRcvr();
  }
}

template <typename BufferType>
inline int64_t ReaderReverse<BufferType>::get_pts(AVFrame *frame)
{ // if no frame avail at this time, then it's due to the primary buffer full
  return frame->pkt_duration + (frame->best_effort_timestamp >= 0
                                    ? frame->best_effort_timestamp
                                    : frame->pts);
}

template <typename BufferType>
template <class Chrono_t>
inline void ReaderReverse<BufferType>::seek(const Chrono_t t0,
                                            const bool exact_search)
{
  if (!active) throw Exception("Activate before seeking.");

  // stop thread before seek
  pause();

  // set to read a frame at t0 next
  seek_pts(ffmpeg::get_pts<Chrono_t>(t0, tb));
}

} // namespace ffmpeg
