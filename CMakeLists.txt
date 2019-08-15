cmake_minimum_required(VERSION 3.14) 
# for FindMatlab support (may require a later version to detect the latest Matlab release)

set (PROJECT_VERSION "0.1")
project (libffmpegio VERSION ${PROJECT_VERSION})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(FFMPEG_VERSION "latest" CACHE STRING "FFmpeg version (only relevant for Win32 and Apple)")

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

# Create object library target
add_library(objlib OBJECT "")
set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(objlib PUBLIC ${FFMPEG_LIBRARIES})
target_include_directories(objlib PUBLIC ${FFMPEG_INCLUDE_DIR})

# CMakeLists.txt in src adds all the files to objlib
add_subdirectory("src")

# Create shared and static libraries from the object library
add_library(sharedlib SHARED $<TARGET_OBJECTS:objlib>)
set_target_properties(sharedlib PROPERTIES OUTPUT_NAME libffmpegio)
target_link_libraries(sharedlib PUBLIC ${FFMPEG_LIBRARIES})

add_library(staticlib STATIC $<TARGET_OBJECTS:objlib>)
set_target_properties(staticlib PROPERTIES OUTPUT_NAME libffmpegio_static)
target_link_libraries(staticlib PUBLIC ${FFMPEG_LIBRARIES})

# This will name your output .so files "libsomething.1.0" which is pretty useful
set_target_properties(sharedlib
PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
)
set_target_properties(staticlib
PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
)

# Let's set compiler-specific flags
# if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
#     # G++
#     target_compile_options(sharedlib PRIVATE -Wall -Wextra)
# elseif(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
#     # MSVC
#     target_compile_options(sharedlib PRIVATE /EHsc /MTd /W2 /c)
#     # Set the DLLEXPORT variable to export symbols
#     target_compile_definitions(sharedlib PRIVATE WIN_EXPORT)
# endif()

