cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

set (PROJECT_VERSION "0.1")
project (libffmpegio VERSION ${PROJECT_VERSION})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(FFMPEG_VERSION "latest" CACHE STRING "FFmpeg version (only relevant for Win32 and Apple)")
set(LIBFFMPEGIO_SHARED TRUE CACHE BOOL "True to build libffmpegio shared library")
set(LIBFFMPEGIO_STATIC TRUE CACHE BOOL "True to build libffmpegio static library")

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
if (NOT libffmpeg)
  add_library(libffmpeg INTERFACE)
  target_link_libraries(libffmpeg INTERFACE ${FFMPEG_LIBRARIES})
  target_include_directories(libffmpeg INTERFACE ${FFMPEG_INCLUDE_DIR})
  message(STATUS "${FFMPEG_INCLUDE_DIR}")
endif()

# Create object library target
add_library(objlib OBJECT "")
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

# Let's set compiler-specific flags
# if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
#     # G++
#     target_compile_options(libffmpegio PRIVATE -Wall -Wextra)
# elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
#     # MSVC
#     target_compile_options(libffmpegio PRIVATE /EHsc /MTd /W2 /c)
#     # Set the DLLEXPORT variable to export symbols
#     target_compile_definitions(libffmpegio PRIVATE WIN_EXPORT)
# endif()

