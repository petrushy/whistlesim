// Room impulse responses built from simple room acoustics:
//  - early reflections from mirror images of the source in a box-shaped room (to the "mixing time"), each with
//    its walls' absorption per frequency and heard by a spherical head (delay and shadow for its direction);
//  - scattered sound: what the bounces scatter leaves the mirror images and joins a noise field, starting as separate
//    arrivals in the gaps between the images and growing into the diffuse tail. The tail's level follows from the
//    room volume, its decay time is set per frequency (so highs die away sooner, as in real rooms), and the two
//    ears are correlated at low frequencies as in a real diffuse field.
// The direct sound is left out (the dry signal provides it). The IR is scaled so the direct sound would
// have amplitude 1, which keeps the physically correct direct-to-reverberant balance for the distance.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reverb
{

// Audio as one vector of samples per channel, all the same length.
struct Buffer
{
    std::vector<std::vector<float>> channels;

    Buffer() = default;
    Buffer (int numChannels, int numSamples) : channels ((size_t) numChannels, std::vector<float> ((size_t) numSamples, 0.0f)) {}
    int getNumChannels() const noexcept { return (int) channels.size(); }
    int getNumSamples() const noexcept { return channels.empty() ? 0 : (int) channels.front().size(); }
    float* getWritePointer (int channel) noexcept { return channels[(size_t) channel].data(); }
    const float* getReadPointer (int channel) const noexcept { return channels[(size_t) channel].data(); }
};

constexpr std::array<float, 8> kBandCentres { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };

struct RoomSpec
{
    std::string name;
    float width, depth, height;   // m
    std::array<float, 8> rt60;    // Reverberation time (s) in the octave bands above.
    float distance;               // Source to listener (m).
    std::array<float, 6> wallAbsorption;  // Relative to the room's average: left, right, front, back, floor, ceiling.
    float scattering;             // Fraction of a bounce's energy scattered away from the mirror direction, at 1 kHz.
};

const std::vector<RoomSpec>& presets();

// Reverberation time at any frequency, interpolated between bands on a log-frequency scale.
float rt60At (const RoomSpec& room, float hz);

// Mixing time: when reflections can be heard as diffuse (Lindau et al.: 20 V/S + 12 ms). Later in small rooms than
// the sqrt(V) ms rule of thumb, which is about the density of reflections rather than what is heard.
double mixingTimeSeconds (const RoomSpec& room);

// Stereo IR, without the direct sound. `sourceOffset` moves the source sideways (m, + = to the listener's right)
// from straight ahead of the listener; a different `seed` gives an independent diffuse tail.
Buffer generate (const RoomSpec& room, double sampleRate, uint32_t seed = 1, double sourceOffset = 0.0);

// A measured impulse response made to match the generated ones: the direct sound removed (the dry signal provides it),
// starting where it was, and scaled so the direct sound would have amplitude 1. The room's measured balance is kept.
Buffer removeDirectSound (const Buffer& measured, double sampleRate);

// An impulse response at another sample rate, with the same frequency response (windowed-sinc interpolation,
// low-passed below the lower Nyquist).
Buffer resample (const Buffer& input, double fromRate, double toRate);

} // namespace reverb
