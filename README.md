# libffmpegio: Video & Audio IO C++ Library powered by FFmpeg

FFmpeg (http://ffmpeg.org/) is a complete, cross-platform solution to record, convert and stream audio and video. Moreover, the FFmpeg shared libraries enables reading and writing media files in various formats, supporting vast number of codecs. Its downside, however, is that it is written in C (for the optimal performance) and requires multiple steps to get reader and writer going.

This library, libffmpegio, is aimed to provide C++ classes to read and write audio and video files with minimal steps. 

## DEPENDENCIES

- [FFmpeg](https://ffmpeg.org/) (need both binaries and shared libraries)
- [CMake](https://cmake.org/) for building and installing the project

(for prebuilt binaries for Windows and OSX, visit to https://ffmpeg.zeranoe.com/builds/, download both Shared and Dev "Linking" options, unzip them in a same directory)

## TIPS TO BUILDING THE PROJECT

First, the project is developed under Windows. User help is needed especially to address issues in the non-Windows OSes
