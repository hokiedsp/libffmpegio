#pragma once

#include <string>
extern "C"

{
#include <libavutil/frame.h> // for AVFrame
}

#include "ffmpegAVFrameQueue.h"
#include "ffmpegFormatInput.h"
#include "ffmpegTimeUtil.h"

#include "ffmpegPostOp.h"
#include "filter/ffmpegFilterGraph.h"

#undef max
#undef min

namespace ffmpeg
{

enum StreamSource
{
  FilterSink = -1,
  Unspecified,
  Decoder
};

// single-thread media file reader
template <typename AVFrameQue> class Reader
{
  public:
  Reader(const std::string &url = "");
  Reader(const Reader &src) = delete;
  virtual ~Reader(); // may need to clean up filtergraphs

  Reader &operator=(const Reader &src) = delete;

  /**
   * \brief Open a file at the given URL
   * \param[in] url
   * \throws if cannot open the specified URL
   * \throws if cannot retrieve stream info
   */
  void openFile(const std::string &url);
  void activate();
  void closeFile();

  /**
   * \brief Add a filter graph to the reader
   *
   * \param[in] desc   Filter graph description to be parsed
   */
  int setFilterGraph(const std::string &desc);

  bool isFileOpen() { return file.isFileOpen(); }

  /**
   * \brief Returns true if there is any AVFrame in its output buffers
   */
  bool hasFrame();

  /**
   * \brief Returns true if there is any AVFrame in its output buffers
   */
  bool hasFrame(const std::string &spec);

  /**
   * \brief Returns true if all streams have reached end-of-file
   */
  bool atEndOfFile();

  bool atEndOfStream(const std::string &spec);
  bool atEndOfStream(int stream_id);

  size_t getStreamCount() { return file.getNumberOfStreams(); }
  size_t getActiveStreamCount()
  {
    return file.getNumberOfActiveStreams() - filter_inbufs.size() +
           filter_outbufs.size();
  }

  int getStreamId(const int stream_id, const int related_stream_id = -1) const;
  int getStreamId(const AVMediaType type,
                  const int related_stream_id = -1) const;
  int getStreamId(const std::string &spec,
                  const int related_stream_id = -1) const;

  /**
   * \brief Activate a stream to read
   *
   * \param[in] spec              Input stream specifier or filter
   *                              output link label
   * \param[in] related_stream_id Specifies related stream of the same
   *                              program (only relevant on input stream)
   * \returns the stream id if decoder output
   */
  template <typename... Args>
  int addStream(const std::string &spec, int related_stream_id = -1,
                Args... args);
  template <typename... Args>
  int addStream(const int wanted_stream_id, int related_stream_id = -1,
                Args... args);
  template <typename... Args>
  int addStream(const AVMediaType type, int related_stream_id = -1,
                Args... args);

  void setPrimaryStream(const std::string &spec);
  void setPrimaryStream(const int stream_id);

  InputStream &getStream(int stream_id, int related_stream_id = -1);
  InputStream &getStream(AVMediaType type, int related_stream_id = -1);
  IAVFrameSource &getStream(std::string spec, int related_stream_id = -1);

  const InputStream &getStream(int stream_id, int related_stream_id = -1) const;
  const InputStream &getStream(AVMediaType type,
                               int related_stream_id = -1) const;

  /**
   * \brief   Get the specifier of the next inactive output streams or filter
   * graph sink.
   *
   * \param[in] last Pass in the last name returned to go to the next
   * \param[in] type Specify to limit search to a particular media type
   * \param[in] stream_sel Specify to stream source:
   *                          StreamSource::Decoder
   *                          StreamSource::FilterSink
   *                          StreamSource::Unspecified (default)
   *
   * \returns the name of the next unassigned stream specifier/output filter
   * label. Returns empty if all have been assigned.
   */
  std::string getNextInactiveStream(
      const std::string &last = "",
      const AVMediaType type = AVMEDIA_TYPE_UNKNOWN,
      const StreamSource stream_sel = StreamSource::Unspecified);

  /**
   * \brief Empty all the buffers and filter graph states
   */
  void flush();

  /**
   * \brief   Get next frame of the specified stream
   *
   * \param[out] frame     Pointer to a pre-allocated AVFrame object.
   * \param[in]  stream_id Media stream ID to read
   * \param[in]  getmore   (Optional) Set true to read more packets if no frame
   *                       is in buffer.
   * \returns true if failed to acquire the frame, either due to eof or empty
   *          buffer.
   */
  bool readNextFrame(AVFrame *frame, const int stream_id,
                     const bool getmore = true);
  bool readNextFrame(AVFrame *frame, const std::string &spec,
                     const bool getmore = true);

  /**
   * \brief Get the youngest time stamp in the queues
   */
  template <class Chrono_t> Chrono_t getTimeStamp();

  /**
   * \brief Get the youngest time stamp of the specified stream
   */
  template <class Chrono_t> Chrono_t getTimeStamp(const std::string &spec);
  template <class Chrono_t> Chrono_t getTimeStamp(int stream_id);

  /**
   * \brief get number of frames in the buffer
   */
  size_t getNumBufferedFrames(int stream_id);
  size_t getNumBufferedFrames(const std::string &spec);

  /**
   * \brief Adjust to the specified timestamp
   */
  template <class Chrono_t>
  void seek(const Chrono_t t0, const bool exact_search = true);

  std::string getFilePath() const { return file.getFilePath(); }

  template <typename Chrono_t = InputFormat::av_duration>
  Chrono_t getDuration() const;

  const AVDictionary *getMetadata() const { return file.getMetadata(); }

  /////////////////////////////////////////////////////////////////////////////
  /**
   * \brief Set post-filter object to retrieve the AVFrame
   *
   * \param[in] spec      Stream specifier (must be active)
   * \param[in] postfilt  Post-filter object
   */
  template <class PostOp, typename... Args>
  void setPostOp(const std::string &spec, Args... args);

  /**
   * \brief Set post-filter object to retrieve the AVFrame
   *
   * \param[in] id        Stream id (must be active)
   * \param[in] postfilt  Post-filter object (must stay valid)
   */
  template <class PostOp, typename... Args>
  void setPostOp(const int id, Args... args);

  protected:
  /**
   * \brief Read next packet
   * \returns true if at least one buffer is already full.
   */
  virtual bool read_next_packet();

  // activate and adds buffer to an input stream by its id
  template <typename... Args> int add_stream(const int stream_id, Args... args);

  /**
   * \brief Helper function for seek(): Purge all the frames earlier than t
   */
  template <class Chrono_t> void purge_until(Chrono_t t);

  // returns the AVFrame buffer given stream spec
  AVFrameQue &get_buf(const std::string &spec);

  // thread-unsafe check for
  bool ready_to_read()
  {
    return std::all_of(bufs.begin(), bufs.end(),
                       [](auto &buf) { return buf.second.readyToPush(); }) &&
           std::all_of(filter_outbufs.begin(), filter_outbufs.end(),
                       [this](auto &buf) { return buf.second.readyToPush(); });
  }

  virtual int64_t get_pts(AVFrame *frame)
  {
    return frame->best_effort_timestamp >= 0 ? frame->best_effort_timestamp
                                             : frame->pts;
  }

  // input media file
  InputFormat file;

  // filter graphs if set
  filter::Graph *filter_graph;

  bool active; // true to lock down the configuring interface, set by
               // activate() function

  std::unordered_map<int, AVFrameQue>
      bufs; // output frame buffers (one for each active stream)

  std::unordered_map<std::string, AVFrameQue>
      filter_outbufs; // filter output frame buffers (one for each active
                      // stream)

  std::string
      prim_buf; // pointer to the primary buffer, used to link secondary buffers

  private:
  // reads next set of packets from file/stream and push the decoded frame
  // to the stream's sink
  bool get_frame(AVFrame *frame, AVFrameQue &buf, const bool getmore);

  template <class Chrono_t> Chrono_t get_time_stamp(AVFrameQue &buf);

  bool at_end_of_stream(AVFrameQue &buf);

  std::unordered_map<std::string, AVFrameQueueST>
      filter_inbufs; // filter output frame buffers (one for each active
                     // stream)
  // post-op objects for all streams
  std::unordered_map<AVFrameQue *, PostOpInterface *> postops;

  template <class PostOp, typename... Args>
  void emplace_postop(AVFrameQue &buf, Args... args);
};

// inline/template implementations

template <typename AVFrameQue>
Reader<AVFrameQue>::Reader(const std::string &url)
    : file(url), active(false), filter_graph(nullptr)
{
}

template <typename AVFrameQue> Reader<AVFrameQue>::~Reader()
{
  for (auto &postop : postops) delete postop.second;
}

template <typename AVFrameQue> bool Reader<AVFrameQue>::hasFrame()
{
  // buffer has a frame if not empty and at least one is a non-eof entry
  auto pred = [](auto &buf) {
    AVFrameQue &que = buf.second;
    return que.size() && !que.eof();
  };
  return active && std::any_of(bufs.begin(), bufs.end(), pred) &&
         std::any_of(filter_outbufs.begin(), filter_outbufs.end(), pred);
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::hasFrame(const std::string &spec)
{
  if (!active || file.atEndOfFile()) return false;
  auto &buf = get_buf(spec);
  return buf.size() && !buf.eof();
}

// returns true if all open streams have been exhausted
template <typename AVFrameQue> bool Reader<AVFrameQue>::atEndOfFile()
{
  if (!active) return false;

  // if all packets have already been read, EOF if all buffers are exhausted
  auto eof = [](auto &buf) {
    AVFrameQue &que = buf.second;
    return que.size() && que.eof();
  };
  if (std::all_of(bufs.begin(), bufs.end(), eof) &&
      std::all_of(filter_outbufs.begin(), filter_outbufs.end(), eof))
    return true;

  // if all buffers empty, read another packet and try again
  auto empty = [](auto &buf) { return buf.second.empty(); };
  if (std::all_of(bufs.begin(), bufs.end(), empty) &&
      std::all_of(filter_outbufs.begin(), filter_outbufs.end(), empty))
  {
    read_next_packet(); // guarantee to push something to a buffer
    return (std::all_of(bufs.begin(), bufs.end(), eof) &&
            std::all_of(filter_outbufs.begin(), filter_outbufs.end(), eof));
  }

  // otherwise (some buffers not empty and at least one is not eof)
  return false;
}

template <typename AVFrameQue>
IAVFrameSource &Reader<AVFrameQue>::getStream(std::string spec,
                                              int related_stream_id)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
    return filter_graph->getSink(spec);

  // check the input stream
  return file.getStream(spec, related_stream_id);
}

template <typename AVFrameQue>
template <typename... Args>
int Reader<AVFrameQue>::addStream(const std::string &spec,
                                  int related_stream_id, Args... args)
{
  if (active) Exception("Cannot add a stream as the reader is already active.");

  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
  {
    auto emplace_returned = filter_outbufs.try_emplace(spec, args...);
    if (!emplace_returned.second)
      throw Exception("The specified filter sink has already been activated.");

    auto &buf = emplace_returned.first->second;

    // check for dynamically sized buffer compatibility
    if (buf.linkable() && buf.isDynamic())
    {
      if (prim_buf.empty())
      {
        filter_outbufs.erase(spec);
        throw Exception(
            "Secondary stream buffer cannot be dynamically sized if "
            "primary buffer is not set.");
      }
      else
      {
        get_buf(prim_buf).lead(buf);
      }
    }

    filter_graph->assignSink(buf, spec);
    emplace_postop<PostOpPassThru>(buf);
    return -1;
  }

  // check the input stream
  int id = file.getStreamId(spec, related_stream_id);
  if (id == AVERROR_STREAM_NOT_FOUND || file.isStreamActive(id))
    throw InvalidStreamSpecifier(spec);
  return add_stream(id, args...);
}

template <typename AVFrameQue>
inline void Reader<AVFrameQue>::setPrimaryStream(const std::string &spec)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph)
  {
    auto it = filter_outbufs.find(spec);
    if (it != filter_outbufs.end())
    {
      auto &buf = it->second;
      if (buf.linkable() && buf.isDynamic())
        throw Exception("Specified stream buffer cannot be set as the primary "
                        "buffer. Primary buffer must have a fixed size.");
      prim_buf = spec;
      return; // success
    }
  }

  // else search the input streams
  setPrimaryStream(file.getStreamId(spec));
}

template <typename AVFrameQue>
inline void Reader<AVFrameQue>::setPrimaryStream(const int stream_id)
{
  auto it = bufs.find(stream_id);
  if (it == bufs.end()) throw InvalidStreamSpecifier(stream_id);
  auto &buf = it->second;
  if (buf.linkable() && buf.isDynamic())
    throw Exception("Specified stream buffer cannot be set as the primary "
                    "buffer because it is dynamically sized.");
  prim_buf = std::to_string(stream_id);
}

template <typename AVFrameQue> bool Reader<AVFrameQue>::read_next_packet()
{
  // if primary buffer is set, only read if primary buffer is not full
  if (!ready_to_read()) return false;
  file.readNextPacket();
  if (filter_graph) filter_graph->processFrame();
  return true;
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::get_frame(AVFrame *frame, AVFrameQue &buf,
                                   const bool getmore)
{
  // if reached eof, nothing to do
  if (atEndOfFile()) return true;

  // if buffer is empty
  if (!buf.readyToPop())
  {
    // no frame without getting more packets
    if (!getmore) return true;

    // read file until buffer gets data or primary is full
    while (!buf.readyToPop() && read_next_packet())
      ;

    // nodata if buffer empty or reached EOF
    if (!buf.readyToPop() || buf.eof()) return true;
  }

  // pop the new frame from the buffer if available; return the eof flag
  return postops.at(&buf)->filter(frame);
}

template <typename AVFrameQue>
AVFrameQue &Reader<AVFrameQue>::get_buf(const std::string &spec)
{
  try
  {
    return filter_outbufs.at(spec);
  }
  catch (...)
  {
    return bufs.at(file.getStreamId(spec));
  }
}

template <typename AVFrameQue> void Reader<AVFrameQue>::flush()
{
  if (!active) return;
  for (auto &buf : bufs) buf.second.clear();
  if (filter_graph)
  {
    for (auto &buf : filter_inbufs) buf.second.clear();
    for (auto &buf : filter_outbufs) buf.second.clear();
    filter_graph->flush();
  }
}

template <typename AVFrameQue>
bool Reader<AVFrameQue>::readNextFrame(AVFrame *frame, const std::string &spec,
                                       const bool getmore)
{
  if (!active) throw Exception("Activate before read a frame.");
  // if filter graph is defined, check its output link labels first
  return get_frame(frame, get_buf(spec), getmore);
}

////////////////////

template <typename AVFrameQue>
int Reader<AVFrameQue>::setFilterGraph(const std::string &desc)
{
  if (active)
    Exception("Cannot set filter graph as the reader is already active.");

  if (filter_graph)
  {
    delete filter_graph;
    filter_graph = nullptr;
  }

  // create new filter graph
  filter::Graph *fg = new filter::Graph(desc);

  // Resolve filters' input sources (throws exception if invalid streams
  // assigned)
  fg->parseSourceStreamSpecs(std::vector<ffmpeg::InputFormat *>({&file}));

  // Link all source filters to the input streams
  int stream_id;
  std::string pad_name;
  while ((pad_name =
              fg->getNextUnassignedSourceLink(nullptr, &stream_id, pad_name))
             .size())
  {
    auto &buf = filter_inbufs[pad_name];
    file.addStream(stream_id, buf);
    fg->assignSource(buf, pad_name);
  }

  filter_graph = fg;

  return 0;
}

template <typename AVFrameQue>
std::string
Reader<AVFrameQue>::getNextInactiveStream(const std::string &last,
                                          const AVMediaType type,
                                          const StreamSource stream_sel)
{
  std::string spec;
  if (stream_sel != StreamSource::Decoder && filter_graph &&
      (spec = filter_graph->getNextUnassignedSink(last, type)).size())
    return spec;
  if (stream_sel == StreamSource::FilterSink) return "";

  int id = file.getStreamId(spec);
  return (id != AVERROR_STREAM_NOT_FOUND)
             ? std::to_string(file.getNextInactiveStream(id))
             : "";
}

template <typename AVFrameQue> void Reader<AVFrameQue>::activate()
{
  if (active) return;

  if (!file.ready()) throw Exception("Reader is not ready.");

  if (filter_graph)
  {
    // connect unused links to nullsrc/nullsink
    // then initializes the filter graph
    filter_graph->configure();
  }

  // then update media parameters of the sinks
  if (filter_graph)
  {
    for (auto &buf : filter_outbufs)
    { dynamic_cast<filter::SinkBase &>(buf.second.getSrc()).sync(); } }

  active = true;
} // namespace ffmpeg

template <typename AVFrameQue>
inline void Reader<AVFrameQue>::openFile(const std::string &url)
{
  if (file.isFileOpen()) closeFile();
  file.openFile(url);
}

template <typename AVFrameQue> inline void Reader<AVFrameQue>::closeFile()
{
  if (filter_graph)
  {
    delete (filter_graph);
    filter_graph = nullptr;
    filter_inbufs.clear();
    filter_outbufs.clear();
  }
  file.closeFile();
  active = false;
  bufs.clear();
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::atEndOfStream(const std::string &spec)
{
  return at_end_of_stream(get_buf(spec)); // frame buffer for the stream
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::atEndOfStream(int stream_id)
{
  return at_end_of_stream(bufs.at(stream_id));
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::at_end_of_stream(AVFrameQue &buf)
{
  // if file is at eof, eos if spec's buffer is exhausted
  // else, read one more packet from file
  return ((buf.readyToPop() || (read_next_packet() && buf.readyToPop())) &&
          buf.eof());
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const int stream_id,
                                           const int related_stream_id) const
{
  int id = file.getStreamId(stream_id, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const AVMediaType type,
                                           const int related_stream_id) const
{
  int id = file.getStreamId(type, related_stream_id);
  return (id == AVERROR_STREAM_NOT_FOUND ||
          (filter_graph && filter_graph->findSourceLink(0, id).size()))
             ? AVERROR_STREAM_NOT_FOUND
             : id;
}

template <typename AVFrameQue>
inline int Reader<AVFrameQue>::getStreamId(const std::string &spec,
                                           const int related_stream_id) const
{
  return file.getStreamId(spec, related_stream_id);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::addStream(const int wanted_stream_id,
                                         int related_stream_id, Args... args)
{
  int id = file.getStreamId(wanted_stream_id, related_stream_id);
  if (id < 0 || file.isStreamActive(id))
    throw InvalidStreamSpecifier(wanted_stream_id);
  return add_stream(id, args...);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::addStream(const AVMediaType type,
                                         int related_stream_id, Args... args)
{
  int id = file.getStreamId(type, related_stream_id);
  if (id < 0 || file.isStreamActive(id)) throw InvalidStreamSpecifier(type);
  return add_stream(id, args...);
}

template <typename AVFrameQue>
inline InputStream &Reader<AVFrameQue>::getStream(int stream_id,
                                                  int related_stream_id)
{
  return file.getStream(stream_id, related_stream_id);
}
template <typename AVFrameQue>
inline InputStream &Reader<AVFrameQue>::getStream(AVMediaType type,
                                                  int related_stream_id)
{
  return file.getStream(type, related_stream_id);
}

template <typename AVFrameQue>
inline const InputStream &
Reader<AVFrameQue>::getStream(int stream_id, int related_stream_id) const
{
  return file.getStream(stream_id, related_stream_id);
}
template <typename AVFrameQue>
inline const InputStream &
Reader<AVFrameQue>::getStream(AVMediaType type, int related_stream_id) const
{
  return file.getStream(type, related_stream_id);
}

template <typename AVFrameQue>
inline bool Reader<AVFrameQue>::readNextFrame(AVFrame *frame,
                                              const int stream_id,
                                              const bool getmore)
{
  return get_frame(frame, bufs.at(file.getStreamId(stream_id)), getmore);
}

template <typename AVFrameQue>
inline size_t Reader<AVFrameQue>::getNumBufferedFrames(int stream_id)
{
  if (!active || file.atEndOfFile()) return 0;
  return bufs.at(file.getStreamId(stream_id)).size();
}

template <typename AVFrameQue>
inline size_t Reader<AVFrameQue>::getNumBufferedFrames(const std::string &spec)
{
  if (!active || file.atEndOfFile()) return 0;
  return get_buf(spec).size();
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp()
{
  if (!active) throw Exception("Activate before read a frame.");

  if (prim_buf.size()) { return get_time_stamp<Chrono_t>(get_buf(prim_buf)); }
  else
  {
    Chrono_t T = getDuration();

    // find the minimum timestamp of all active streams
    auto reduce_op = [T](const Chrono_t &t,
                         const std::pair<std::string, AVFrameQue> &buf) {
      return std::min(T, get_time_stamp<Chrono_t>(buf.second));
    };
    Chrono_t t =
        std::reduce(bufs.begin(), bufs.end(), Chrono_t::max(), reduce_op);
    return std::reduce(filter_outbufs.begin(), filter_outbufs.end(), t,
                       reduce_op);
  }
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp(const std::string &spec)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(get_buf(spec));
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getTimeStamp(int stream_id)
{
  if (!active) throw Exception("Activate before read a frame.");
  return get_time_stamp<Chrono_t>(bufs.at(stream_id));
}

template <typename AVFrameQue>
template <class Chrono_t>
inline Chrono_t Reader<AVFrameQue>::get_time_stamp(AVFrameQue &buf)
{
  // if no frame avail at this time, then it's due to the primary buffer full
  if (!(buf.readyToPop() || (read_next_packet() && buf.readyToPop())))
  {
    if (!ready_to_read() && prim_buf.empty()) throw Exception("Failed to read current time stamp.");
    AVFrameQue &pbuf = get_buf(prim_buf);
    if (&pbuf==&buf) throw Exception("Failed to read current time stamp.");
    return get_time_stamp<Chrono_t>(pbuf);
  }

  AVFrame *frame = buf.peekToPop();
  return (frame) ? get_timestamp<Chrono_t>(get_pts(frame),
                                           buf.getSrc().getTimeBase())
                 : getDuration();
}

template <typename AVFrameQue>
template <class Chrono_t>
inline void Reader<AVFrameQue>::seek(const Chrono_t t0, const bool exact_search)
{
  // clear all the buffers
  flush();

  // seek (to near) the requested time
  file.seek<Chrono_t>(t0);
  if (atEndOfFile())
  {
    for (auto &buf : bufs) buf.second.push(nullptr);
    for (auto &buf : filter_outbufs) buf.second.push(nullptr);
  }
  else if (exact_search)
  {
    // throw away frames with timestamps younger than t0
    purge_until(t0);
  }
}

template <typename AVFrameQue>
template <class Chrono_t>
void Reader<AVFrameQue>::purge_until(Chrono_t t0)
{
  // helper function for seek: expects all the buffers to be empty

  auto checkNextTimeStamp = [t0](AVFrameQue &que) {
    if (!que.readyToPop()) return false; // no frame arrived yet

    AVFrame *frame;
    frame = que.peekToPop();
    if (!frame) return true; // EOF, good to go

    if (get_timestamp<Chrono_t>(frame->best_effort_timestamp,
                                que.getSrc().getTimeBase()) >= t0)
      return true;
    que.pop();
    return false;
  };

  // keep reading packet until all buffers next timestamp is t0 or later
  while (read_next_packet() &&
         !(std::all_of(bufs.begin(), bufs.end(),
                       [checkNextTimeStamp](auto &buf) {
                         return checkNextTimeStamp(buf.second);
                       }) &&
           std::all_of(filter_outbufs.begin(), filter_outbufs.end(),
                       [checkNextTimeStamp](auto &buf) {
                         return checkNextTimeStamp(buf.second);
                       })))
    ;
}

template <typename AVFrameQue>
template <typename Chrono_t>
inline Chrono_t Reader<AVFrameQue>::getDuration() const
{
  return file.getDuration<Chrono_t>();
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::setPostOp(const std::string &spec, Args... args)
{
  emplace_postop<PostOp, Args...>(get_buf(spec), args...);
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::setPostOp(const int id, Args... args)
{
  emplace_postop<PostOp, Args...>(bufs.at(id), args...);
}

template <typename AVFrameQue>
template <typename... Args>
inline int Reader<AVFrameQue>::add_stream(const int stream_id, Args... args)
{
  // stream must not be already activated (i.e., already has a buffer assigned
  // to it)
  auto emplace_returned = bufs.try_emplace(stream_id, args...);

  if (!emplace_returned.second)
    throw Exception("The specified stream has already been activated.");

  // create a new buffer
  auto &buf = emplace_returned.first->second;

  // check for dynamically sized buffer compatibility
  if (buf.linkable() && buf.isDynamic())
  {
    if (prim_buf.empty())
    {
      bufs.erase(stream_id);
      throw Exception("Secondary stream buffer cannot be dynamically sized if "
                      "primary buffer is not set.");
    }
    else
    {
      get_buf(prim_buf).lead(buf);
    }
  }

  // activate the stream with the new buffer
  auto ret = file.addStream(stream_id, buf).getId();

  // default to just pass-through the output frame
  emplace_postop<PostOpPassThru>(buf);

  // return the stream id
  return ret;
}

template <typename AVFrameQue>
template <class PostOp, typename... Args>
inline void Reader<AVFrameQue>::emplace_postop(AVFrameQue &buf, Args... args)
{
  if (postops.count(&buf))
  {
    delete postops[&buf];
    postops[&buf] = nullptr;
  }
  postops[&buf] = new PostOp(buf, args...);
}

} // namespace ffmpeg
