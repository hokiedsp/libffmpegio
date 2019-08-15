target_sources(objlib PRIVATE
   ffmpegStream.cpp ffmpegStreamInput.cpp 
                                ffmpegFormatInput.cpp ffmpegDump.cpp ffmpegTimeUtil.h 
                                ffmpegException.cpp ffmpegReaderMT.cpp)

add_subdirectory(filter) # adds additional gather source file lists in LIBFFMPEG_FILTERSRCS
