#pragma once

#include "../ffmpegBase.h"
#include "../ffmpegFormatInput.h"
#include "ffmpegFilterSinks.h"
#include "ffmpegFilterSources.h"

// #include "mexClassHandler.h"
// #include "ffmpegPtrs.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegAVFramePtrBuffer.h"
// #include "ffmpegFrameBuffers.h"

extern "C"
{
// #include <libavcodec/avcodec.h>
// #include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
  // #include <libavutil/pixdesc.h>
}

#include <chrono>
#include <unordered_map>
#include <utility>
#include <vector>

typedef std::vector<std::string> string_vector;

using namespace std::chrono_literals;

namespace ffmpeg
{
namespace filter
{
class Graph : public ffmpeg::Base
{
  public:
  Graph(const std::string &filtdesc = "");
  ~Graph();

  /**
 * \brief Destroys the current AVFilterGraph and release all the resources
 * 
 * destroy() frees \ref graph 
 */
  void clear();

  /**
   * \briefs Destroy the current AVFilterGraph
   * 
   * purge() frees \ref graph using avfilter_graph_free() and purges all invalidated AVFilter 
   * pointers from \ref inputs and \ref outputs maps. It keeps 'filter' and 'buf' fields of 
   * \ref inputs and \ref outputs maps.
   * 
   * Use \ref clear() to release all the resources associated with the current filter graph
   * 
   * When the associated AVFilterGraph is destroyed, \ref context becomes invalid. ffmpeg::filter::Graph
   * calls this function to make its filters deassociate invalidated AVFilterContexts.
   */
  virtual void purge();

  /**
   * \brief Parse new filter graph
   * 
   * \note Step 1 in building a new filter graph 
   * \note Not thread-safe. Must be called while the object's thread is paused
   * 
   * \param[in] desc Descriptive string of a new filter graph as done in standard ffmpeg binary
   */
  void parse(const std::string &desc);

  /**
   * \brief Parse and match unassigned filter input names to the input streams of the given input files.
   * 
   * \param[in] fmts   Pointers to the input file formats
   * 
   * \throws ffmpegException if the stream has already been taken
   */
  void parseSourceStreamSpecs(const std::vector<InputFormat *> fmts);

  /**
   * \brief returns the name, its expected file and stream IDs (if assigned and requested) of filter
   *        input pad without buffer
   */
  std::string getNextUnassignedSourcePad(int *file_id = nullptr, int *stream_id = nullptr, const std::string &last = "");

  /**
   * \brief Assign a source buffer to a parsed filtergraph
   * 
   * assignSource() links the filter graph input with the given label \ref name to the given AVFrame 
   * source buffer \ref buf. This function must follow a successful \ref void parse(const std::string&) 
   * call and must be called repeatedly to all of the utilized filter graph inputs. Use 
   * \ref string_vector getInputNames() const to retrieve all the names of inputs.
   * 
   * \note Step 2/3 in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   * 
   * \param[in] buf  Refererence to an AVFrame buffer object, from which the input frames are drawn from
   * \param[in] name Name of the input label on the filer graph. Default is "in", which is used
   *                 for a single-input graph with unnamed input.
   * \returns the reference of the created Source filter object
   */
  SourceBase &assignSource(IAVFrameSourceBuffer &buf, const std::string &name = "in");

  /**
   * \brief Assign a sink buffer to a parsed filtergraph
   * 
   * assignSink() links the filter graph output with the given label \ref name to the given AVFrame sink
   * buffer \ref buf. This function must follow a successful \ref void parse(const std::string&) call, 
   * and must be called repeatedly, once for each of the utilized filter graph outputs. Use 
   * \ref string_vector getOutputNames() const to retrieve all the names of outputs.
   * 
   * \note Step 2/3 in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   * 
   * \param[in] buf  Reference to an AVFrame buffer object, to which the output frames are queued to
   * \param[in] name Name of the output label on the filer graph. Default is "out", which is used
   *                 for a single-output graph with unnamed input.
   * \returns the reference of the created Source filter object
   */
  SinkBase &assignSink(IAVFrameSinkBuffer &buf, const std::string &name = "out");

  /**
   * \brief Finalize the preparation of a new filtergraph
   * 
   * finalizeGraph instantiates all the endpoint filter elements (buffer, buffersink, abuffer, or abuffersink)
   * as assigned by user.
   * 
   * \note Lst step in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   * 
   */
  void configure();

  /**
   * \brief Returns true if the filter graph is ready to run
   * 
   * \returns true if filter graph is ready to run
   */
  bool ready();

  /**
   * \brief Update the frame data
   * 
   * finalizeGraph instantiates all the endpoint filter elements (buffer, buffersink, abuffer, or abuffersink)
   * as assigned by user.
   * 
   * \note Lst step in building a new filter graph
   * \note Not thread-safe. Must be called while the object's thread is paused
   * 
   */
  void updateParameters();

  /**
   * \brief Reset filter graph state / flush internal buffers
   * 
   * flush() resets the internal state / buffers of the filter graph. It should be called
   * when seeking or when switching to receivign AVFrames with a different parameters.
   * 
   * flush() effectively rebuilds the filter as FFmpeg API currently does not provide 
   * a way to flush an existing AVFilterGraph. Internal function may change in the future
   * when/if FFmpeg releases a "flush" function for an AVFilterGraph.
   * 
   * \note Not thread-safe. Must be called while the object's thread is paused
   */
  void flush();

  /**
   * \brief Execute the filter graph for one frame
   * 
   * processFrame() executes one filtering transaction without the threaded portion of 
   * ffmpeg::filter::Graph class. The input/source AVFrame buffers linked to the filter 
   * graph must be pre-populated. If no AVFrame could be retrieved, the function throws
   * an exception. 
   * 
   * On the other end, the output AVFrame buffers should have enough capacity
   * so that all the filtered output AVFrame could be captured. If an output buffer remains
   * full after \var rel_time milliseconds, the filtered AVFrame will be dropped.
   * 
   * \returns total number of frames filter output.
   * \returns negative value if no input frame is available
   * 
   * \throws ffmpegException if fails to retrieve any input AVFrame from input source buffers.
   * \throws ffmpegException if input buffer returns an error during frame retrieval.
   * \throws ffmpegException if output buffer returns an error during frame retrieval.
   */
  int processFrame();

  AVFilterGraph *getAVFilterGraph() const { return graph; }

  int insert_filter(AVFilterContext *&last_filter, int &pad_idx,
                    const std::string &filter_name, const std::string &args);

  std::string getFilterGraphDesc() const { return graph_desc; }
  string_vector getInputNames() const { return Graph::get_names(inputs); }
  string_vector getOutputNames() const { return Graph::get_names(outputs); }

  IAVFrameSourceBuffer *getInputBuffer(std::string name = "")
  {
    if (name.empty())
      return inputs.begin()->second.buf;
    else
      return inputs.at(name).buf;
  }
  IAVFrameSinkBuffer *getOutputBuffer(std::string name = "")
  {
    if (name.empty())
      return outputs.begin()->second.buf;
    else
      return outputs.at(name).buf;
  }

  template <typename UnaryFunction>
  void forEachInput(UnaryFunction f)
  {
    for (auto it = inputs.begin(); it != inputs.end(); ++it)
      f(it->first, it->second.buf, it->second.filter);
  }

  template <typename UnaryFunction>
  void forEachInputName(UnaryFunction f)
  {
    for (auto it = inputs.begin(); it != inputs.end(); ++it)
      f(it->first);
  }

  template <typename UnaryFunction>
  void forEachInputFilter(UnaryFunction f)
  {
    for (auto it = inputs.begin(); it != inputs.end(); ++it)
      f(it->first, it->second.filter);
  }

  template <typename UnaryFunction>
  void forEachInputBuffer(UnaryFunction f)
  {
    for (auto it = inputs.begin(); it != inputs.end(); ++it)
      f(it->first, it->second.buf);
  }

  template <typename UnaryFunction>
  void forEachOutput(UnaryFunction f)
  {
    for (auto it = outputs.begin(); it != outputs.end(); ++it)
      f(it->first, it->second.buf, it->second.filter);
  }

  template <typename UnaryFunction>
  void forEachOutputName(UnaryFunction f)
  {
    for (auto it = outputs.begin(); it != outputs.end(); ++it)
      f(it->first);
  }

  template <typename UnaryFunction>
  void forEachOutputFilter(UnaryFunction f)
  {
    for (auto it = outputs.begin(); it != outputs.end(); ++it)
      f(it->first, it->second.filter);
  }

  template <typename UnaryFunction>
  void forEachOutputBuffer(UnaryFunction f)
  {
    for (auto it = outputs.begin(); it != outputs.end(); ++it)
      f(it->first, it->second.buf);
  }

  bool isSimple() const { return inputs.size() == 1 && outputs.size() == 1; }

  bool isSource(const std::string &name)
  {
    return inputs.find(name) != inputs.end();
  }

  bool isSink(const std::string &name)
  {
    return outputs.find(name) != outputs.end();
  }

  // avfilter_graph_send_command, avfilter_graph_queue_command
  protected:
  template <typename EP, typename VEP, typename AEP, typename BUFF>
  void assign_endpoint(EP *&ep, AVMediaType type, BUFF &buf)
  {
    // if already defined, destruct existing
    if (ep)
      delete ep;

    // create new filter
    switch (type)
    {
    case AVMEDIA_TYPE_VIDEO:
      av_log(NULL, AV_LOG_ERROR, "creating video source node\n");
      ep = new VEP(*this, buf);
      break;
    case AVMEDIA_TYPE_AUDIO:
      ep = new AEP(*this, buf);
      break;
    default:
      ep = NULL;
      throw ffmpegException("Only video and audio filters are supported at this time.");
    }
    if (!ep)
      throw ffmpegException("[ffmpeg::filter::Graph::assign_endpoint] Failed to allocate a new filter endpoint.");
  }

  private:
  AVFilterGraph *graph;
  std::string graph_desc;

  typedef struct
  {
    AVFilterContext *other;
    int otherpad;
  } ConnectTo;
  typedef std::vector<ConnectTo> ConnectionList;

  typedef struct
  {
    AVMediaType type;
    int file_id;
    int stream_id;
    IAVFrameSourceBuffer *buf;
    SourceBase *filter;
    ConnectionList conns;
  } SourceInfo;
  typedef struct
  {
    AVMediaType type;
    int file_id;
    int stream_id;
    IAVFrameSinkBuffer *buf;
    SinkBase *filter;
    ConnectTo conn;
  } SinkInfo;
  std::unordered_map<std::string, SourceInfo> inputs;
  std::unordered_map<std::string, SinkInfo> outputs;

  int inmon_status; // 0:no monitoring; 1:monitor; <0 quit

  template <typename InfoMap>
  static string_vector get_names(const InfoMap &map)
  {
    string_vector names;
    for (auto p = map.begin(); p != map.end(); ++p)
      names.emplace_back(p->first);
    return names;
  }

  void parse_sources(AVFilterInOut *ins);
  void parse_sinks(AVFilterInOut *outs);

  void connect_nullsources();
  void connect_nullsinks();

  void use_src_splitter(SourceBase *src, const ConnectionList &conns);
};
} // namespace filter
} // namespace ffmpeg
