# The room reverb: generated room impulse responses and a zero-latency convolution reverb, with the standard library
# alone (no JUCE), so other apps (the iPhone app) can build it as it is.
#   add_subdirectory(<whistlesim>/Source/Reverb)   then link WhistleReverb; include "Reverb/RoomReverb.h" etc.

cmake_minimum_required(VERSION 3.22)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)  # Built on its own
    project(WhistleReverb LANGUAGES CXX)
endif()

add_library(WhistleReverb STATIC
    Convolver.cpp
    Fft.cpp
    ImpulseResponse.cpp
    RoomReverb.cpp)
target_compile_features(WhistleReverb PUBLIC cxx_std_20)
target_include_directories(WhistleReverb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Threads REQUIRED)  # Rooms are generated on a background thread
target_link_libraries(WhistleReverb PUBLIC Threads::Threads)
