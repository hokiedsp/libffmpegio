set(LIBFFMPEGIO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB HEADERFILES LIST_DIRECTORIES FALSE RELATIVE ${LIBFFMPEGIO_SRC_DIR} "*.h")

target_sources(objlib PRIVATE ffmpegStream.cpp ffmpegStreamInput.cpp 
                              ffmpegFormatInput.cpp ffmpegDump.cpp  
                              ffmpegException.cpp ffmpegReaderMT.cpp
                      PUBLIC ${HEADERFILES})

if (LIBFFMPEGIO_INSTALL_DEV)
  if (LIBFFMPEGIO_INSTALL_HEADER_DEST)
    install(FILES ${HEADERFILES} DESTINATION ${LIBFFMPEGIO_INSTALL_HEADER_DEST})
  else()
    install(FILES ${HEADERFILES} TYPE INCLUDE)
  endif()
endif()

add_subdirectory(filter) # adds additional gather source file lists in LIBFFMPEG_FILTERSRCS
