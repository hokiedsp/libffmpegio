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
  ReaderReverse(const std::string &url = "") : ReaderReverse<BufferType>(url) {}
  virtual ~ReaderReverse() {}

  void activate();

  template <class Chrono_t>
  void seek(const Chrono_t t0, const bool exact_search = true);

  private:
  int64_t pts_last;  // last buffer timestamp
  int64_t pts_delta; // expected duration covered by a full buffer

  void cb_eof(BufferType &buf);
  void cb_swap(BufferType &buf);
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

  // get the first batch of data
  auto &s = getStream(prim_buf);
  auto &pbuf = dynamic_cast<BufferType &>(s.getSinkBuffer());
  using namespace std::chrono_literals;
  if (!pbuf.blockTillReadyToPop(1000ms))
    throw Exception("Reading from file timed out.");

  // stop the reader thread
  pause();

  // evaluate the frames
  AVFrame *frame = pbuf.peekToPop();

  throw Exception("Under dev.");

  // flush();

  // // set callbacks
  // using namespace std::placeholders;
  // pbuf.setReachedEofCallback(std::bind(&ReaderReverse::cb_eof, this, _1));
  // pbuf.setBeforeSwapCallback(std::bind(&ReaderReverse::cb_swap, this, _1));

  // // set to the end

  // // start the thread
  // resume();
}

template <typename BufferType>
template <class Chrono_t>
inline void ReaderReverse<BufferType>::seek(const Chrono_t t0,
                                            const bool exact_search)
{
  // stop thread before seek
  pause();

  // do the coarse search first
  Reader<BufferType>::seek<Chrono_t>(t0, false);

  // restart thread
  resume();

  // perform the exact search only after the thread has restarted
  if (exact_search) Reader<BufferType>::purge_until<Chrono_t>(t0);
}

} // namespace ffmpeg
