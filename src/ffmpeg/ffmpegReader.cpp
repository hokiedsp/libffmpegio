#include "ffmpegReader.h"
// #include "ffmpegAvRedefine.h"
// #include "ffmpegException.h"
// #include "ffmpegPtrs.h"

// extern "C"
// {
// #include <libavfilter/avfilter.h>
// #include <libavfilter/buffersink.h>
// #include <libavfilter/buffersrc.h>
// #include <libavutil/opt.h>
// }

using namespace ffmpeg;

////////////////////////////////////////////////////////////////////////////////////////////////

Reader::Reader(const std::string &url)
    : file(url), active(false), filter_graph(nullptr)
{
}

Reader::~Reader()
{
  for (auto &postop : postops) delete postop.second;
}

bool Reader::hasFrame()
{
  // buffer has a frame if not empty and at least one is a non-eof entry
  auto pred = [](auto &buf) {
    AVFrameQueueST &que = buf.second;
    return que.size() && !que.eof();
  };
  return std::any_of(bufs.begin(), bufs.end(), pred) &&
         std::any_of(filter_outbufs.begin(), filter_outbufs.end(), pred);
}

bool Reader::hasFrame(const std::string &spec)
{
  if (!file.atEndOfFile()) return false;
  auto &buf = get_buf(spec);
  return buf.size() && !buf.eof();
}

// returns true if all open streams have been exhausted
bool Reader::atEndOfFile()
{
  if (file.atEndOfFile())
  {
    // if all packets have already been read, EOF if all buffers are exhausted
    auto pred = [](auto &buf) {
      AVFrameQueueST &que = buf.second;
      return que.size() && que.eof();
    };
    return std::all_of(bufs.begin(), bufs.end(), pred) &&
           std::all_of(filter_outbufs.begin(), filter_outbufs.end(), pred);
  }
  else
  {
    // if still more packets to read and all buffers are empty read another
    // packet
    auto pred = [](auto &buf) { return buf.second.empty(); };
    return (std::all_of(bufs.begin(), bufs.end(), pred) &&
            std::all_of(filter_outbufs.begin(), filter_outbufs.end(), pred))
               ? !file.readNextPacket()
               : false;
  }

  // end of file if all streams reached eos
}

IAVFrameSource &Reader::getStream(std::string spec, int related_stream_id)
{
  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
    return filter_graph->getSink(spec);

  // check the input stream
  return file.getStream(spec, related_stream_id);
}

int Reader::addStream(const std::string &spec, int related_stream_id)
{
  if (active) Exception("Cannot add stream as the reader is already active.");

  // if filter graph is defined, check its output link labels first
  if (filter_graph && filter_graph->isSink(spec))
  {
    auto &buf = filter_outbufs[spec];
    filter_graph->assignSink(buf, spec);
    emplace_postop<PostOpPassThru>(buf);
    return -1;
  }

  // check the input stream
  int id = file.getStreamId(spec, related_stream_id);
  if (id == AVERROR_STREAM_NOT_FOUND || file.isStreamActive(id))
    throw InvalidStreamSpecifier(spec);
  return add_stream(id);
}

ffmpeg::InputStream *Reader::read_next_packet()
{
  auto stream = file.readNextPacket();
  if (filter_graph) filter_graph->processFrame();
  return stream; // returns true if eof
}

bool Reader::get_frame(AVFrameQueueST &buf)
{
  // read file until the target stream is reached
  while (buf.empty() && file.readNextPacket() && filter_graph &&
         filter_graph->processFrame())
    ;
  return buf.eof();
}

bool Reader::get_frame(AVFrame *frame, AVFrameQueueST &buf, const bool getmore)
{
  // if reached eof, nothing to do
  if (atEndOfFile()) return true;

  // read the next frame (read multile packets until the target buffer is
  // filled)
  if ((getmore && get_frame(buf)) || buf.empty()) return true;

  // pop the new frame from the buffer if available; return the eof flag
  return postops.at(&buf)->filter(frame);
}

Reader::AVFrameQueueST &Reader::get_buf(const std::string &spec)
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

void Reader::flush()
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

bool Reader::readNextFrame(AVFrame *frame, const std::string &spec,
                           const bool getmore)
{
  if (!active) throw Exception("Activate before read a frame.");
  // if filter graph is defined, check its output link labels first
  return get_frame(frame, get_buf(spec), getmore);
}

////////////////////

void Reader::setPixelFormat(const AVPixelFormat pix_fmt,
                            const std::string &spec)
{
  if (active)
    Exception("Cannot set pixel format as the reader is already active.");

  if (filter_graph) // if using filters, set filter
  {
    try
    {
      filter_graph->setPixelFormat(pix_fmt, spec);
      if (spec.empty()) file.setPixelFormat(pix_fmt, spec);
    }
    catch (const InvalidStreamSpecifier &)
    {
      // reaches only if spec is not found in the filter_graph
      file.setPixelFormat(pix_fmt, spec);
    }
  }
  else
  {
    file.setPixelFormat(pix_fmt, spec);
  }
}

int Reader::setFilterGraph(const std::string &desc)
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

std::string Reader::getNextInactiveStream(const std::string &last,
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

void Reader::activate()
{
  if (active) return;

  if (!file.ready()) throw Exception("Reader is not ready.");

  if (filter_graph)
  {
    // connect unused links to nullsrc/nullsink
    // then initializes the filter graph
    filter_graph->configure();
  }

  // read frames until all the buffers have at least one frame
  while (!file.atEndOfFile() &&
         std::any_of(bufs.begin(), bufs.end(),
                     [](const auto &buf) { return buf.second.empty(); }))
  {
    file.readNextPacket();
    if (filter_graph) filter_graph->processFrame();
  }

  if (filter_graph)
  {
    // also make sure all the filter graph sink buffers are filled
    while (!file.atEndOfFile() &&
           std::any_of(filter_outbufs.begin(), filter_outbufs.end(),
                       [](const auto &buf) { return buf.second.empty(); }))
    {
      file.readNextPacket();
      if (filter_graph) filter_graph->processFrame();
    }

    // then update media parameters of the sinks
    for (auto &buf : filter_outbufs)
    { dynamic_cast<filter::SinkBase &>(buf.second.getSrc()).sync(); } }

  active = true;
}
