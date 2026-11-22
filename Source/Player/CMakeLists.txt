# What plays: the WARBL stream decoded (StreamCore) and turned into sound (Player: sensors to the model's controls,
# the model, the room reverb, the limiter), with the standard library alone (no JUCE), so the iPhone app plays
# exactly as the desktop app does.
#   add_subdirectory(<whistlesim>/Source/Player)   then link WhistlePlayer (it brings WhistleEngine and WhistleReverb)

cmake_minimum_required(VERSION 3.22)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)  # Built on its own
    project(WhistlePlayer LANGUAGES CXX)
endif()

if (NOT TARGET WhistleEngine)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Whistle" WhistleEngine)
endif()
if (NOT TARGET WhistleReverb)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Reverb" WhistleReverb)
endif()

add_library(WhistlePlayer STATIC
    Player.cpp
    Recorder.cpp
    SensorSettings.cpp
    StreamCore.cpp
    Workshop.cpp)
target_compile_features(WhistlePlayer PUBLIC cxx_std_20)
# Users include "Player/Player.h"; StreamProtocol.h, SensorResponse.h and SimulatedPlayer.h are in Source.
target_include_directories(WhistlePlayer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(WhistlePlayer PUBLIC WhistleEngine WhistleReverb)
