// Convolution reverb with zero added latency, with the standard library alone. Uses the generated room IRs
// (ImpulseResponse.h) or a loaded IR, e.g. a measured hall from the OpenAIR library (reading the file is the app's
// part: loadImpulseResponse takes its samples). Two sources (the whistle's window and its tube), each with its own
// room response from its own place in the room; each comes in as the signal at the two ears. Stereo out (wet only;
// the caller adds the dry signal). A new response fades in over 50 ms.

#pragma once

#include "Convolver.h"
#include "ImpulseResponse.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class RoomReverb
{
public:
    RoomReverb();
    ~RoomReverb();

    // Not while process() runs.
    void prepare (double sampleRate, int maxBlockSize);

    // Audio thread. Adds the reverb of the two sources (each as the left and right ear's signal) to the outputs.
    // Any number of samples.
    void process (const float* windowLeft, const float* windowRight, const float* bodyLeft, const float* bodyRight,
                  float* left, float* right, int numSamples);

    // Message thread. How far apart the two sources are, as the angle between them seen by the listener
    // (degrees; the window to the left, the tube to the right). Regenerates the room's IRs.
    void setSpread (float degrees);
    float getSpread() const { return spreadDegrees.load(); }
    // Where the sources are, as seen by the listener (radians, + = right): for the direct sound. Any thread.
    float sourceAzimuth (int source) const
    {
        return (source == 0 ? -0.5f : 0.5f) * spreadDegrees.load() * 3.14159265358979f / 180.0f;
    }

    // Message thread. Generates the room's IR in the background and switches to it when ready.
    void loadPreset (int index);
    // Message thread. A measured IR (any channel count; mono is used for both ears) at its own sample rate, with the
    // direct sound still in it: removed here, and the level set by it. `path` is only remembered (getCurrentFile).
    // Returns an error message, or empty on success.
    std::string loadImpulseResponse (const reverb::Buffer& measured, double sampleRate, const std::string& name, const std::string& path);

    std::atomic<float> wetDb { -6.0f };
    std::atomic<float> preDelayMs { 0.0f };

    std::string getCurrentName() const;
    int getPresetIndex() const { return presetIndex; }  // -1 when an IR file is loaded
    std::string getCurrentFile() const { return currentFile; }
    int getLatencySamples() const { return 0; }

private:
    // One response per source, each with a convolver per ear.
    struct Engine
    {
        std::array<std::unique_ptr<reverb::Convolver>, 4> convolvers;  // Window L R, body L R
    };

    void startGenerating (int index);
    void install (std::unique_ptr<Engine>, const std::string& name);  // Any thread but the audio thread
    static std::unique_ptr<Engine> makeEngine (const reverb::Buffer& window, const reverb::Buffer& body, int blockSize);
    void runJobs();

    double sampleRate = 48000.0;
    int maxBlock = 512;

    // Handing engines to the audio thread without locks: install() puts a new one in `pending`; process() takes it,
    // fades from `active` to it, and leaves the old one in `retired` for install() (or the destructor) to delete.
    std::atomic<Engine*> pending { nullptr };
    std::atomic<Engine*> retired { nullptr };
    Engine* active = nullptr;   // Audio thread
    Engine* fading = nullptr;   // Audio thread: the previous engine while the new one fades in
    int fadePosition = 0, fadeLength = 1;

    // Pre-delay: 4 channels (two per source), linear interpolation.
    std::array<std::vector<float>, 4> delayLines;
    int delayWrite = 0;

    // Wet level: a linear ramp over 50 ms to each new value, as juce::SmoothedValue.
    float wetGain = 0.0f, wetTarget = 0.0f, wetStep = 0.0f;
    int wetStepsLeft = 0;

    std::array<std::vector<float>, 4> work, faded;  // Pre-delayed inputs; the fading engine's output

    std::atomic<float> spreadDegrees { 30.0f };
    int presetIndex = -1;
    std::string currentFile;
    reverb::Buffer fileResponse;  // A loaded IR without its direct sound, at its own rate (for a new sample rate)
    double fileResponseRate = 0.0;
    std::string fileName;

    mutable std::mutex nameLock;
    std::string currentName;

    // Room generation in the background: one job slot, the newest request replacing a waiting one.
    std::mutex jobLock;
    std::condition_variable jobWaiting;
    std::function<void()> job;
    bool quitting = false;
    std::thread worker;
};
