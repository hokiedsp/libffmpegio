#pragma once

#include "ffmpegAVFrameBufferInterfaces.h"
#include "syncpolicies.h"

#include <atomic>
#include <numeric> // for std::reduce
#include <vector>

namespace ffmpeg
{

// AVFrame queue
template <typename MutexType, typename CondVarType, typename MutexLockType>
class AVFrameStack : public IAVFrameBuffer
{
  public:
  AVFrameStack(size_t N = 0)
      : killnow(false), dynamic(N == 0), src(nullptr), dst(nullptr)
  {
    // set read/write pointers to the beginning
    p = stk.begin();

    // insert the first frame
    expand();
    if (!dynamic) // if fixed queue size, expand to the desired size
      for (int i = 1; i < N; ++i) expand();

  } // queue size

  AVFrameStack(const AVFrameStack &that)
      : src(that.src), dst(that.dst), dynamic(that.dynamic),
        stk(that.stk.size())
  {
    std::transform(that.stk.begin(), that.stk.end(), stk.begin(),
                   [](const Data_s &src) -> Data_s {
                     AVFrame *frame;
                     if (src.populated && !src.eof)
                       frame = av_frame_clone(src.frame);
                     else
                       frame = av_frame_alloc();
                     if (!frame) throw Exception("Failed to clone AVFrame.");
                     return {frame, src.eof, src.populated};
                   });
    p = stk.begin() + (that.p - that.stk.begin());
  }

  AVFrameStack(AVFrameStack &&that)
  {
    src = std::move(that.src);
    dst = std::move(that.dst);
    dynamic = std::move(that.dynamic);
    int64_t I = that.p - that.stk.begin();
    stk = std::move(that.stk);
    p = stk.begin() + I;
    that.p = that.stk.begin();
  }

  virtual ~AVFrameStack()
  {
    // release allocated memory for all the AVFrames
    for (auto &buf : stk)
      if (buf.frame) av_frame_free(&buf.frame);
  }

  AVFrameStack &operator=(const AVFrameStack &that)
  {
    src = that.src;
    dst = that.dst;
    dynamic = that.dynamic;
    stk(that.stk.size());
    std::transform(that.stk.begin(), that.stk.end(), stk.begin(),
                   [](const Data_s &src) -> Data_s {
                     AVFrame *frame;
                     if (src.populated && !src.eof)
                       frame = av_frame_clone(src.frame);
                     else
                       frame = av_frame_alloc();
                     if (!frame) throw Exception("Failed to clone AVFrame.");
                     return {frame, src.eof, src.populated};
                   });
    p = stk.begin() + (that.p - that.stk.begin());
  }

  const MediaParams &getMediaParams() const
  {
    if (src)
      return src->getMediaParams();
    else
      throw Exception(
          "Media parameters could be retrieved only if src is connected.");
  }

  IAVFrameSource &getSrc() const
  {
    if (src)
      return *src;
    else
      throw Exception("No source is connected.");
  };
  void setSrc(IAVFrameSource &buf) { src = &buf; }
  void clrSrc() { src = nullptr; }

  IAVFrameSink &getDst() const
  {
    if (dst)
      return *dst;
    else
      throw Exception("No source is connected.");
  };
  void setDst(IAVFrameSink &buf) { dst = &buf; }
  void clrDst() { dst = nullptr; }

  bool autoexpand() const { return dynamic; }

  bool ready() const { return !killnow; };
  void kill()
  {
    killnow = true;
    cv.notify_all();
  }

  void clear()
  {
    MutexLockType lock(mutex);
    for (auto it = stk.begin(); it != stk.end(); ++it)
    {
      if (it->populated)
      {
        it->populated = false;
        if (it->eof)
          it->eof = false;
        else
          av_frame_unref(it->frame);
      }
    }
    p = stk.begin();
    killnow = false;
  }

  size_t size() noexcept
  {
    MutexLockType lock(mutex);
    return p - stk.begin();
  }
  bool empty() noexcept
  {
    MutexLockType lock(mutex);
    return stk.empty() || stk.front().populated;
  }
  bool full() noexcept
  {
    if (dynamic) return false;
    MutexLockType lock(mutex);
    return stk.back().populated;
  }

  size_t capacity() const { return dynamic ? 0 : stk.size(); }
  bool isDynamic() const { return dynamic; }

  // does not support master-slave mode
  bool linkable() const { return false; }
  void follow(IAVFrameSinkBuffer &master) {}
  void lead(IAVFrameSinkBuffer &slave) {}

  ///////////////////////////////////////////////////////////////////////////////

  bool readyToPush()
  {
    MutexLockType lock(mutex);
    return readyToPush_threadunsafe();
  }

  void blockTillReadyToPush()
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this]() { return killnow || readyToPush_threadunsafe(); });
  }

  bool blockTillReadyToPush(const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    return cv.wait_for(lock, rel_time, [this] {
      return killnow || readyToPush_threadunsafe();
    }) && !killnow;
  }

  AVFrame *peekToPush()
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPush_threadunsafe(); });
    if (killnow) return nullptr;
    if (p->populated)
      throw_or_expand(); // expand if allowed or throws overflow exception
    return p->frame;
  }

  void push()
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPush_threadunsafe(); });
    if (!killnow) mark_populated_threadunsafe();
  }

  void push(AVFrame *frame)
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPush_threadunsafe(); });
    if (!killnow) push_threadunsafe(frame);
  }

  bool push(AVFrame *frame, const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    bool success = cv.wait_for(lock, rel_time, [this] {
      return killnow || readyToPush_threadunsafe();
    });
    if (success && !killnow) push_threadunsafe(frame);
    return success;
  }

  bool tryToPush(AVFrame *frame)
  {
    MutexLockType lock(mutex);
    if (readyToPush_threadunsafe())
    {
      push_threadunsafe(frame);
      return true;
    }
    else
    {
      return false;
    }
  }

  ///////////////////////////////////////////////////////////////////////////////

  bool readyToPop()
  {
    MutexLockType lock(mutex);
    return readyToPop_threadunsafe();
  }

  void blockTillReadyToPop()
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPop_threadunsafe(); });
  }

  bool blockTillReadyToPop(const std::chrono::milliseconds &rel_time)
  {
    MutexLockType lock(mutex);
    return cv.wait_for(lock, rel_time,
                       [this] { return killnow || readyToPop_threadunsafe(); });
  }

  void pop(AVFrame *frame, bool *eof = nullptr)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPop_threadunsafe(); });
    if (!killnow) pop_threadunsafe(frame, eof);
  }

  bool pop(AVFrame *frame, bool *eof, const std::chrono::milliseconds &rel_time)
  {
    if (!frame) throw Exception("frame must be non-null pointer.");
    MutexLockType lock(mutex);
    bool success = cv.wait_for(lock, rel_time, [this] {
      return killnow || readyToPop_threadunsafe();
    });
    if (success && !killnow) pop_threadunsafe(frame, eof);
    return success;
  }

  bool eof()
  {
    if (empty()) return false;
    MutexLockType lock(mutex);
    return p->eof && size() == 1;
  }

  bool hasEof() noexcept // true if buffer contains EOF
  {
    return eof();
  }

  bool tryToPop(AVFrame *frame, bool *eof)
  {
    MutexLockType lock(mutex);
    if (readyToPop_threadunsafe())
    {
      pop_threadunsafe(frame, eof);
      return true;
    }
    else
    {
      return false;
    }
  }

  AVFrame *peekToPop()
  {
    MutexLockType lock(mutex);
    cv.wait(lock, [this] { return killnow || readyToPop_threadunsafe(); });

    if (eof() || killnow)
      return nullptr;
    else
      return p->frame;
  }

  void pop()
  {
    MutexLockType lock(mutex);
    cv.wait(lock,
            [this]() -> bool { return killnow || readyToPop_threadunsafe(); });
    if (!killnow) pop_threadunsafe(nullptr, nullptr);
  }

  AVFrame *peekLastPushed() { return peekToPop(); }
  void popLastPushed() { pop(); }

  private:
  void throw_or_expand()
  {
    if (dynamic)
      expand();
    else
      throw Exception("AVFrameStack::Buffer overflow.");
  }
  void expand()
  {
    if (stk.empty())
    {
      stk.push_back({av_frame_alloc(), false, false});
      p = stk.begin();
    }
    else
    {
      int64_t I = p - stk.begin();

      // if stk is full, set access point to the new element as only push
      // operation calls expand under that condition
      if (p->populated) ++I;

      stk.push_back({av_frame_alloc(), false, false});

      p = stk.begin() + I;
    }
  }

  bool readyToPush_threadunsafe() { return dynamic || !p->populated; }
  bool readyToPop_threadunsafe() // declared in AVFrameSourceBase
  {
    return p->populated;
  }

  /**
   * \brief Implements enquing of the new frame onto the queue
   * \param[in] frame pointer to the frame data. If null, eof is pushed.
   */
  void push_threadunsafe(AVFrame *frame)
  {
    // if buffer is not available (not yet read, caught up with p ptr)
    if (p->populated)
      throw_or_expand(); // expand if allowed or throws overflow exception

    // copy the frame data
    if (frame) av_frame_ref(p->frame, frame);
    p->eof = !frame;

    // set the written flag
    p->populated = true;

    // increment write iterator
    if (p != stk.end() - 1) ++p;

    // notify the source-end for the arrival of new data
    cv.notify_one();
  }

  void mark_populated_threadunsafe()
  {
    // if buffer is not available
    if (p->populated) throw Exception("Already populated.");

    // set the written flag
    p->populated = true;

    // increment access point
    if (p != stk.end() - 1) ++p;

    // notify the source-end for the arrival of new data
    cv.notify_one();
  }

  void pop_threadunsafe(AVFrame *frame, bool *eofout)
  {
    // guaranteed readyToPop() returns true

    // grab the eof flag
    bool eof = p->eof;
    if (eofout) *eofout = eof;

    if (eof && size() == 1) return; // only eof left

    // pre-decrement to return the last frame available
    if (eof)
    {
      p->eof = false;
      p->populated = false;
      --p;
    }

    // get the frame if not eof
    if (frame)
      av_frame_move_ref(frame, p->frame);
    else // frame not returned
      av_frame_unref(p->frame);

    if (eof) // if eof already reached, mark this element as eof
      p->eof = true;
    else
      (p--)->populated = false;

    // notify the sink-end for slot opening
    cv.notify_one();
  }

  IAVFrameSource *src;
  IAVFrameSink *dst;

  MutexType mutex;
  CondVarType cv;
  std::atomic_bool killnow;

  bool dynamic; // true=>dynamically-sized buffer

  struct Data_s
  {
    AVFrame *frame;
    bool eof;       // true if eof (frame is unreferenced)
    bool populated; // true if data is available
  };

  std::vector<Data_s> stk;                  // queue containing
  typename std::vector<Data_s>::iterator p; // access position
};                                          // class AVFrameStack

typedef AVFrameStack<NullMutex, NullConditionVariable<NullMutex>,
                     NullUniqueLock<NullMutex>>
    AVFrameStackST;

typedef AVFrameStack<Cpp11Mutex, Cpp11ConditionVariable,
                     Cpp11UniqueLock<Cpp11Mutex>>
    AVFrameStackMT;

} // namespace ffmpeg
