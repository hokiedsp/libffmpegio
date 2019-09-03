#pragma once

#include "ffmpegAVFrameQueue.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace ffmpeg
{

/**
 * \brief   Template AVFrame sink to manage receive & send buffers
 */
template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType = AVFrameQueueST>
class AVFrameDoubleBuffer : public IAVFrameBuffer
{
  public:
  AVFrameDoubleBuffer(size_t N = 0);
  AVFrameDoubleBuffer(const AVFrameDoubleBuffer &src) = delete;
  ~AVFrameDoubleBuffer() {}

  AVFrameDoubleBuffer &operator=(const AVFrameDoubleBuffer &src) = delete;

  bool ready() const { return !killnow; }
  void kill();

  bool autoexpand() const { return rcvr->autoexpand(); }

  IAVFrameSource &getSrc() const { return rcvr->getSrc(); }
  void setSrc(IAVFrameSource &src);
  void clrSrc();

  IAVFrameSink &getDst() const { return sndr->getDst(); }
  void setDst(IAVFrameSink &dst);
  void clrDst();

  const MediaParams &getMediaParams() const;

  void clear();
  size_t size() noexcept;
  bool empty() noexcept;
  bool full() noexcept;
  bool hasEof() noexcept; // true if buffer contains EOF

  bool isDynamic() const { return rcvr->isDynamic(); }
  bool linkable() const { return true; }
  void follow(IAVFrameSinkBuffer &master);
  void lead(IAVFrameSinkBuffer &slave);

  bool readyToPush();
  void blockTillReadyToPush();
  bool blockTillReadyToPush(const std::chrono::milliseconds &timeout_duration);
  AVFrame *peekToPush();
  void push();
  void push(AVFrame *frame);
  bool push(AVFrame *frame, const std::chrono::milliseconds &timeout_duration);
  bool tryToPush(AVFrame *frame);

  bool readyToPop();
  void blockTillReadyToPop();
  bool blockTillReadyToPop(const std::chrono::milliseconds &timeout_duration);
  AVFrame *peekToPop();
  void pop();
  void pop(AVFrame *frame, bool *eof = nullptr);
  bool pop(AVFrame *frame, bool *eof,
           const std::chrono::milliseconds &timeout_duration);
  bool tryToPop(AVFrame *frame, bool *eof = nullptr);

  bool eof();

  private:
  bool readyToPush_threadunsafe() { return !rcvr->full(); }
  bool readyToPop_threadunsafe() { return !sndr->empty(); }
  void push_swapper(MutexLockType &lock);
  void pop_swapper(MutexLockType &lock);

  /**
   * \brief swaps rcvr & sndr buffers (internal use only for slave buffers)
   *
   * \note waits
   */
  void swap();
  void swap_threadunsafe();

  typedef std::array<BufferType, 2> Buffers;
  Buffers buffers;
  typename Buffers::iterator rcvr; // receives new AVFrame
  typename Buffers::iterator sndr; // sends new stored AVFrame data

  std::vector<AVFrameDoubleBuffer *> slaves;

  MutexType mutex;
  CondVarType cv_swap;
  std::atomic_bool killnow;
};

// Predefined concrete types
typedef AVFrameDoubleBuffer<Cpp11Mutex, Cpp11ConditionVariable,
                            Cpp11UniqueLock<Cpp11Mutex>>
    AVFrameDoubleBufferMT;

typedef AVFrameDoubleBuffer<Cpp11Mutex, Cpp11ConditionVariable,
                            Cpp11UniqueLock<Cpp11Mutex>, AVFrameStackST>
    AVFrameDoubleBufferLIFOMT;

/// IMPLEMENTATION ///

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                    BufferType>::AVFrameDoubleBuffer(size_t N)
    : killnow(false), buffers({BufferType(N), BufferType(N)})
{
  // assign writer & reader
  rcvr = buffers.begin();
  sndr = rcvr + 1;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                         BufferType>::kill()
{
  killnow = true;
  for (auto &buf : buffers) buf.kill();
  cv_swap.notify_all();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::setSrc(
    IAVFrameSource &src)
{
  for (auto &buf : buffers) buf.setSrc(src);
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::clrSrc()
{
  for (auto &buf : buffers) buf.clrSrc();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::setDst(
    IAVFrameSink &dst)
{
  for (auto &buf : buffers) buf.setDst(dst);
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::clrDst()
{
  for (auto &buf : buffers) buf.clrDst();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline const MediaParams &
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                    BufferType>::getMediaParams() const
{
  return rcvr->getMediaParams();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::clear()
{
  MutexLockType lock(mutex);
  for (auto &buf : buffers) buf.clear();
  killnow = false;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline size_t AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                  BufferType>::size() noexcept
{
  MutexLockType lock(mutex);
  return sndr->size();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::empty() noexcept
{
  MutexLockType lock(mutex);
  return sndr->empty();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::full() noexcept
{
  MutexLockType lock(mutex);
  return rcvr->full();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::follow(
    IAVFrameSinkBuffer &master)
{
  if (!rcvr->isDynamic())
    throw Exception(
        "Only dynamic buffers can become a slave to another buffer.");
  AVFrameDoubleBuffer &masterDB = dynamic_cast<AVFrameDoubleBuffer &>(master);
  masterDB.slaves.push_back(this);
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::lead(
    IAVFrameSinkBuffer &slave)
{
  AVFrameDoubleBuffer &slaveDB = dynamic_cast<AVFrameDoubleBuffer &>(slave);
  if (!slaveDB.rcvr->isDynamic())
    throw Exception(
        "Only dynamic buffers can become a slave to another buffer.");
  slaves.push_back(&slaveDB);
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::readyToPush()
{
  MutexLockType lock(mutex);
  return readyToPush_threadunsafe();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::blockTillReadyToPush()
{
  MutexLockType lock(mutex);
  cv_swap.wait(lock, [this] { return killnow || readyToPush_threadunsafe(); });
  return;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::
    blockTillReadyToPush(const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  return cv_swap.wait_for(lock, timeout_duration, [this] {
    return killnow || readyToPush_threadunsafe();
  });
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline AVFrame *AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                    BufferType>::peekToPush()
{
  MutexLockType lock(mutex);
  if (rcvr->full()) // make sure rcvr buffer has room to push
    cv_swap.wait(lock,
                 [this]() { return killnow || readyToPush_threadunsafe(); });
  return killnow ? nullptr : rcvr->peekToPush();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::push()
{
  MutexLockType lock(mutex);
  if (rcvr->full()) // make sure rcvr buffer has room to push
    cv_swap.wait(lock,
                 [this]() { return killnow || readyToPush_threadunsafe(); });
  if (!killnow)
  {
    lock.unlock();
    rcvr->push();
    push_swapper(lock); // run again in case the last push filled rcvr buffer
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::push(
    AVFrame *frame)
{
  MutexLockType lock(mutex);
  if (rcvr->full()) // make sure rcvr buffer has room to push
    cv_swap.wait(lock,
                 [this]() { return killnow || readyToPush_threadunsafe(); });
  if (!killnow)
  {
    lock.unlock();
    rcvr->push(frame);
    push_swapper(lock);
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::push(
    AVFrame *frame, const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  bool success = true;
  if (rcvr->full()) // make sure rcvr buffer has room to push
    success = cv_swap.wait_for(lock, timeout_duration, [this]() {
      return killnow || readyToPush_threadunsafe();
    });
  lock.unlock();
  if (!killnow && success)
  {
    rcvr->push(frame);
    push_swapper(lock);
  }
  return success;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::push_swapper(MutexLockType &lock)
{
  lock.lock();
  if (rcvr->full() && sndr->empty()) // ready to swap if sndr is empty
  {
    swap_threadunsafe();
    cv_swap.notify_one(); // notify sndr its buffer is now available
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::tryToPush(AVFrame *frame)
{
  MutexLockType lock(mutex);
  bool success = readyToPush_threadunsafe();
  if (success) rcvr->push(frame);
  return success;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::readyToPop()
{
  MutexLockType lock(mutex);
  return readyToPop_threadunsafe();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::blockTillReadyToPop()
{
  MutexLockType lock(mutex);
  cv_swap.wait(lock, [this] { return killnow || readyToPop_threadunsafe(); });
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::
    blockTillReadyToPop(const std::chrono::milliseconds &timeout_duration)
{
  MutexLockType lock(mutex);
  return cv_swap.wait_for(lock, timeout_duration, [this] {
    return killnow || readyToPop_threadunsafe();
  });
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline AVFrame *AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                    BufferType>::peekToPop()
{
  MutexLockType lock(mutex);
  if (sndr->empty()) // swap if needed, return true if failed to swap
    cv_swap.wait(lock, [this] { return killnow || readyToPop_threadunsafe(); });
  return sndr->peekToPop();
} // namespace ffmpeg

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::pop()
{
  MutexLockType lock(mutex);
  if (sndr->empty()) // swap if needed, return true if failed to swap
    cv_swap.wait(lock,
                 [this]() { return killnow || readyToPop_threadunsafe(); });
  if (!killnow)
  {
    lock.unlock();
    sndr->pop();
    pop_swapper(lock);
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::pop(
    AVFrame *frame, bool *eof)
{
  MutexLockType lock(mutex);
  if (sndr->empty()) // swap if needed, return true if failed to swap
    cv_swap.wait(lock,
                 [this]() { return killnow || readyToPop_threadunsafe(); });
  if (!killnow)
  {
    lock.unlock();
    sndr->pop(frame, eof);
    pop_swapper(lock);
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::pop(
    AVFrame *frame, bool *eof,
    const std::chrono::milliseconds &timeout_duration)
{
  if (!frame) throw Exception("frame must be non-null pointer.");
  MutexLockType lock(mutex);
  bool success = true;
  if (sndr->empty()) // swap if needed, return true if failed to swap
    success = cv_swap.wait_for(lock, timeout_duration,
                               [this] { return readyToPop_threadunsafe(); });
  if (success && !killnow)
  {
    lock.unlock();
    sndr->pop(frame, eof);
    pop_swapper(lock);
  }
  return success;
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::pop_swapper(MutexLockType &lock)
{
  lock.lock();
  if (sndr->empty() && (rcvr->full() || rcvr->hasEof())) // ready to swap
  {
    swap_threadunsafe();
    cv_swap.notify_one(); // let receiver know it received empty buffer
  }
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::tryToPop(AVFrame *frame, bool *eof)
{
  MutexLockType lock(mutex);
  if (!readyToPop_threadunsafe()) return false;
  return sndr->tryToPop(frame, eof);
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::eof()
{
  MutexLockType lock(mutex);
  if (sndr->empty()) //
    return rcvr->eof();
  else
    return sndr->eof();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline bool AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::hasEof() noexcept
{
  MutexLockType lock(mutex);
  return rcvr->hasEof() || sndr->hasEof();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void
AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType, BufferType>::swap()
{
  MutexLockType lock(mutex);
  swap_threadunsafe();
}

template <typename MutexType, typename CondVarType, typename MutexLockType,
          typename BufferType>
inline void AVFrameDoubleBuffer<MutexType, CondVarType, MutexLockType,
                                BufferType>::swap_threadunsafe()
{
  std::swap(rcvr, sndr);
  rcvr->clear(); // make sure new rcvr buffer is empty
  for (auto slave : slaves) slave->swap();
}

} // namespace ffmpeg
