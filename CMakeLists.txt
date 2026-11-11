cmake_minimum_required(VERSION 3.22)

project(WhistleSim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# iOS (-G Xcode -DCMAKE_SYSTEM_NAME=iOS) builds only the app; see "iPhone and iPad" in the README.
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(WHISTLESIM_IOS ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "")
else()
    set(WHISTLESIM_IOS OFF)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# iOS: keep playing with the screen locked, pair WARBL over Bluetooth MIDI, and show the app's Documents folder
# (recordings, impulse responses) in the Files app. Landscape only while the app uses its desktop layout.
set(WHISTLESIM_IOS_OPTIONS)
if (WHISTLESIM_IOS)
    set(WHISTLESIM_IOS_OPTIONS
        BACKGROUND_AUDIO_ENABLED TRUE
        BLUETOOTH_PERMISSION_ENABLED TRUE
        BLUETOOTH_PERMISSION_TEXT "WhistleSim connects to WARBL over Bluetooth MIDI."
        FILE_SHARING_ENABLED TRUE
        PLIST_TO_MERGE "<plist><dict><key>LSSupportsOpeningDocumentsInPlace</key><true/></dict></plist>"
        STATUS_BAR_HIDDEN TRUE
        REQUIRES_FULL_SCREEN TRUE
        IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
        IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)
endif()

# The app: plays a physical model of a whistle from the WARBL2 sensor stream, and monitors the stream's timing.
# Not a WARBL product, so the company and bundle ID are the project's own.
juce_add_gui_app(WhistleSim
    PRODUCT_NAME "WhistleSim"
    COMPANY_NAME "WhistleSim"
    BUNDLE_ID "io.github.petrushy.whistlesim"
    ${WHISTLESIM_IOS_OPTIONS})

target_sources(WhistleSim PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MonitorPage.cpp
    Source/InstrumentPage.cpp
    Source/SensorsPage.cpp
    Source/ModelPage.cpp
    Source/StreamReceiver.cpp
    Source/Recorder.cpp
    Source/AudioEngine.cpp
    Source/RoomReverb.cpp
    Source/ImpulseResponse.cpp
    Source/Whistle/WhistleGeometry.cpp
    Source/Whistle/Acoustics.cpp
    Source/Whistle/WhistleModel.cpp
    Source/Whistle/InstrumentFile.cpp
    Source/Whistle/ScalePlan.cpp)

target_include_directories(WhistleSim PRIVATE Source)

target_compile_definitions(WhistleSim PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WhistleSim,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WhistleSim,JUCE_VERSION>")

target_link_libraries(WhistleSim
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (WHISTLESIM_IOS)
    # Sign with the team given at configure time (-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=...); Xcode makes the
    # certificate and provisioning profile itself.
    set_target_properties(WhistleSim PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
    return()  # The tools and tests are command-line programs: Mac, Linux and Windows only.
endif()

# Protocol unit test (no JUCE).
enable_testing()
add_executable(StreamProtocolTest Tests/StreamProtocolTest.cpp)
target_include_directories(StreamProtocolTest PRIVATE Source)
add_test(NAME StreamProtocolTest COMMAND StreamProtocolTest)

# Instrument file round trip and error handling.
juce_add_console_app(InstrumentFileTest)
target_sources(InstrumentFileTest PRIVATE Tests/InstrumentFileTest.cpp Source/Whistle/InstrumentFile.cpp
    Source/Whistle/WhistleGeometry.cpp Source/Whistle/Acoustics.cpp)
target_include_directories(InstrumentFileTest PRIVATE Source)
target_compile_definitions(InstrumentFileTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(InstrumentFileTest PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME InstrumentFileTest COMMAND InstrumentFileTest)

# Virtual WARBL for trying the app or a synth without the instrument.
juce_add_console_app(WarblSimulator PRODUCT_NAME "WARBL Simulator")
target_sources(WarblSimulator PRIVATE tools/WarblSimulator.cpp)
target_include_directories(WarblSimulator PRIVATE Source)
target_compile_definitions(WarblSimulator PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(WarblSimulator
    PRIVATE juce::juce_audio_devices juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Measures the generated room reverbs (reverberation time, direct/reverberant ratio) and renders WAVs.
juce_add_console_app(ReverbCheck PRODUCT_NAME "Reverb Check")
target_sources(ReverbCheck PRIVATE tools/ReverbCheck.cpp Source/ImpulseResponse.cpp Source/TestPhrase.cpp)
target_include_directories(ReverbCheck PRIVATE Source)
target_compile_definitions(ReverbCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ReverbCheck
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Whistle acoustics and model checks (tuning tables, offline playing tests).
juce_add_console_app(WhistleCheck PRODUCT_NAME "Whistle Check")
target_sources(WhistleCheck PRIVATE tools/WhistleCheck.cpp Source/Whistle/WhistleGeometry.cpp Source/Whistle/Acoustics.cpp Source/Whistle/WhistleModel.cpp Source/Whistle/ScalePlan.cpp
    Source/Whistle/JetStability.cpp Source/Whistle/InstrumentFile.cpp)
target_include_directories(WhistleCheck PRIVATE Source)
target_compile_definitions(WhistleCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(WhistleCheck
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The presets' stored tuning still matches the acoustics.
juce_add_console_app(PresetTuningTest)
target_sources(PresetTuningTest PRIVATE Tests/PresetTuningTest.cpp Source/Whistle/WhistleGeometry.cpp Source/Whistle/Acoustics.cpp)
target_include_directories(PresetTuningTest PRIVATE Source)
target_compile_definitions(PresetTuningTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PresetTuningTest PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME PresetTuningTest COMMAND PresetTuningTest)
