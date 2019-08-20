cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

set (PROJECT_VERSION "0.1")
project (libffmpegio VERSION ${PROJECT_VERSION})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(FFMPEG_VERSION "latest" CACHE STRING "FFmpeg version (only relevant for Win32 and Apple)")
set(LIBFFMPEGIO_SHARED TRUE CACHE BOOL "True to build libffmpegio shared library")
set(LIBFFMPEGIO_STATIC TRUE CACHE BOOL "True to build libffmpegio static library")

set(LIBFFMPEGIO_INSTALL_FFMPEG ON CACHE BOOL "ON to install FFmpeg files (Windoes & OSX only)")
set(LIBFFMPEGIO_INSTALL_DEV ON CACHE BOOL "ON to install lib & header files")
set(LIBFFMPEGIO_INSTALL_FFMPEG_BUILD_DEST "" CACHE STRING "Specify the install destination for FFMPEG build files")
set(LIBFFMPEGIO_INSTALL_RUNTIME_DEST "" CACHE STRING "Specify the install destination for runtime files")
set(LIBFFMPEGIO_INSTALL_LIBRARY_DEST "" CACHE STRING "Specify the install destination for library files")
set(LIBFFMPEGIO_INSTALL_HEADER_DEST "" CACHE STRING "Specify the install destination for header files")

include(GNUInstallDirs)

# download FFmpeg DEV & SHARED
include("${CMAKE_CURRENT_LIST_DIR}/cmake/FFmpegDownloadDev.cmake")
FFmpegDownloadDev(${FFMPEG_VERSION})

# Find dependent packages
find_package(FFMPEG REQUIRED COMPONENTS AVUTIL AVFILTER OPTIONAL_COMPONENTS AVDEVICE SWSCALE)

# Set C++ options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (FFMPEG_AVDEVICE_FOUND)
  add_compile_definitions(CONFIG_AVDEVICE)
endif()

# To build shared libraries in Windows, we set CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS to TRUE.
# See https://cmake.org/cmake/help/v3.4/variable/CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS.html
# See https://blog.kitware.com/create-dlls-on-windows-without-declspec-using-new-cmake-export-all-feature/
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Create FFmpeg interface library
add_library(libffmpeg INTERFACE)
target_link_libraries(libffmpeg INTERFACE ${FFMPEG_LIBRARIES})
target_include_directories(libffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})

if (LIBFFMPEGIO_INSTALL_FFMPEG AND (NOT UNIX OR APPLE))
  #copy entire ffmpeg shared folder
  if (LIBFFMPEGIO_INSTALL_FFMPEG_BIN_DEST)
    install(DIRECTORY ${FFMPEG_BIN_DIRECTORY}/ DESTINATION ${LIBFFMPEGIO_INSTALL_FFMPEG_BUILD_DEST})
  else()
    install(DIRECTORY ${FFMPEG_BIN_DIRECTORY}/ DESTINATION .)
  endif()

  if (LIBFFMPEGIO_INSTALL_DEV)
    if (LIBFFMPEGIO_INSTALL_LIBRARY_DEST)
      install(DIRECTORY ${FFMPEG_ROOT}/lib/ DESTINATION ${LIBFFMPEGIO_INSTALL_LIBRARY_DEST})
    else()
      install(DIRECTORY ${FFMPEG_ROOT}/lib/ TYPE LIB)
    endif()

    if (LIBFFMPEGIO_INSTALL_HEADER_DEST)
      install(DIRECTORY ${FFMPEG_ROOT}/include/ DESTINATION ${LIBFFMPEGIO_INSTALL_HEADER_DEST})
    else()
      install(DIRECTORY ${FFMPEG_ROOT}/include/ TYPE INCLUDE)
    endif()
  endif()
endif()

# Create object library target
add_library(objlib OBJECT)
if (LIBFFMPEGIO_SHARED)
  set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
endif()
target_link_libraries(objlib PUBLIC libffmpeg)
target_include_directories(objlib PUBLIC libffmpeg)

include(GenerateExportHeader)
generate_export_header(objlib)
target_include_directories(objlib PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/exports)

# CMakeLists.txt in src adds all the files to objlib
add_subdirectory("src")

# Create shared and static libraries from the object library
if (LIBFFMPEGIO_SHARED)
  add_library(libffmpegio SHARED $<TARGET_OBJECTS:objlib>)
  target_link_libraries(libffmpegio PUBLIC libffmpeg)
  set_target_properties(libffmpegio PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION})
  target_include_directories(libffmpegio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

if (LIBFFMPEGIO_STATIC)
  add_library(libffmpegio_static STATIC $<TARGET_OBJECTS:objlib>)
  target_link_libraries(libffmpegio_static PUBLIC libffmpeg)
  set_target_properties(libffmpegio_static PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION})
  target_include_directories(libffmpegio_static INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# setup install

# install shared library (no accompanying .lib)
if (LIBFFMPEGIO_INSTALL_RUNTIME_DEST)
  install(TARGETS libffmpegio RUNTIME DESTINATION ${LIBFFMPEGIO_INSTALL_RUNTIME_DEST}
                              LIBRARY DESTINATION ${LIBFFMPEGIO_INSTALL_RUNTIME_DEST})
else()
  install(TARGETS libffmpegio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                              LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if (LIBFFMPEGIO_INSTALL_DEV)
  if (LIBFFMPEGIO_INSTALL_LIBRARY_DEST)
    install(TARGETS libffmpegio libffmpegio_static ARCHIVE DESTINATION ${LIBFFMPEGIO_INSTALL_LIBRARY_DEST}
                                                   LIBRARY DESTINATION ${LIBFFMPEGIO_INSTALL_LIBRARY_DEST})
  else()
    install(TARGETS libffmpegio libffmpegio_static ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                                                   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()

endif()
 