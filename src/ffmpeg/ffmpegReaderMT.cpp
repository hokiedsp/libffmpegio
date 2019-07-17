#include "ffmpegReaderMT.h"

using namespace ffmpeg;

////////////////////////////////////////////////////////////////////////////////////////////////

// inline/template implementations

inline void ReaderMT::closeFile()
{
  // stop the thread before closing
  stop();
  Reader<AVFrameDoubleBufferMT>::closeFile();
}

template <class Chrono_t>
inline void ReaderMT::seek(const Chrono_t t0, const bool exact_search)
{
  // stop thread before seek
  pause();

  // do the coarse search first
  Reader<AVFrameDoubleBufferMT>::seek<Chrono_t>(t0, false);

  // restart thread
  resume();

  // perform the exact search only after the thread has restarted
  if (exact_search) Reader<AVFrameDoubleBufferMT>::purge_until<Chrono_t>(t0);
}

/**
 * \brief Worker thread function to read frames and stuff buffers
 */
void ReaderMT::thread_fcn()
{
  std::unique_lock<std::mutex> thread_guard(thread_lock);

  while (!killnow)
  {
    switch (status)
    {
    case IDLE:
    case PAUSED:
      // wait until status goes back to ACTIVE
      thread_ready.wait(thread_guard,
                        [this]() { return killnow || status == ACTIVE; });
      break;
    case PAUSE_RQ: status = PAUSED; break;
    case ACTIVE:
      if (file.atEndOfFile()) { status = IDLE; }
      else
      {
        thread_guard.unlock();
        Reader<AVFrameDoubleBufferMT>::read_next_packet();
        thread_ready.notify_one();
        thread_guard.lock();
      }
      break;
    }
  }
}

/**
 * \brief Blocks until at least one previously empty read buffer becomes ready
 */
void ReaderMT::read_next_packet()
{
  std::unique_lock<std::mutex> thread_guard(thread_lock);

  // gather the list of empty stream and filter output buffers that are not
  // ready to pop
  std::vector<int> empty_bufs;
  std::vector<std::string> empty_fouts;
  for (auto &buf : bufs)
  {
    if (!buf.second.readyToPop()) empty_bufs.push_back(buf.first);
  }
  for (auto &buf : filter_outbufs)
  {
    if (!buf.second.readyToPop()) empty_fouts.push_back(buf.first);
  }

  // if all buffers are occupied, good to go!
  if (empty_bufs.empty() && empty_fouts.empty()) return;

  // otherwise wait till a frame becomes available on any of the buffers
  thread_ready.wait(thread_guard, [this, empty_bufs, empty_fouts]() {
    return std::any_of(empty_bufs.begin(), empty_bufs.end(),
                       [this](auto id) { return bufs.at(id).readyToPop(); }) ||
           std::any_of(empty_fouts.begin(), empty_fouts.end(),
                       [this](const auto &spec) {
                         return filter_outbufs.at(spec).readyToPop();
                       });
  });
}

void ReaderMT::activate()
{
  if (active) return;

  // ready the file & streams
  Reader<AVFrameDoubleBufferMT>::activate();

  // start the thread
  start();
}