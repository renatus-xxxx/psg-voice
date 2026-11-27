cmake_minimum_required(VERSION 3.16)
project(psg-voice-tools C)

# wav2psg: PC-side WAV -> PSG volume-step converter (C99).
# Builds on Windows, macOS (Intel / Apple silicon) and Ubuntu.
# On macOS, pass -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" to get a
# universal binary (the GitHub Actions workflow does this).

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(wav2psg src/wav2psg.c)
if(NOT WIN32)
    target_link_libraries(wav2psg PRIVATE m)
endif()
