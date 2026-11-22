// A real FFT of a power-of-two size, with the standard library alone. Its layout is juce::dsp::FFT's real-only one,
// so code written for that works unchanged: the forward transform turns N real samples into the N/2 + 1 complex bins
// 0 ... N/2, interleaved (re, im) in an array of 2N floats; the inverse takes those bins and gives back the N samples
// (scaled by 1/N, so forward then inverse is the identity).
// On Apple platforms the transforms go through Accelerate (vDSP), which uses the chip's vector units; elsewhere, and
// for sizes vDSP doesn't take, through the portable code here.

#pragma once

#include <complex>
#include <vector>

namespace reverb
{

class Fft
{
public:
    explicit Fft (int order);  // Size 2^order (order >= 1)
    ~Fft();
    Fft (const Fft&) = delete;
    Fft& operator= (const Fft&) = delete;
    Fft (Fft&&) noexcept;
    Fft& operator= (Fft&&) noexcept;

    int getSize() const noexcept { return size; }

    // data: 2 * size floats; the first `size` are the input samples.
    void forward (float* data) const;
    // data: 2 * size floats; bins 0 ... size/2 interleaved; the result is in the first `size`.
    void inverse (float* data) const;

    // The same on complex bins (size/2 + 1 of them), for code that multiplies spectra.
    void forward (const float* samples, std::complex<float>* bins) const;
    void inverse (const std::complex<float>* bins, float* samples) const;

private:
    void complexTransform (std::complex<float>* data, bool inverse) const;  // In place, size/2 points, unscaled

    int size;
    int half;
    std::vector<int> bitReversed;               // For the size/2-point complex transform
    std::vector<std::complex<float>> twiddles;  // e^(-2 pi i k / (size/2)), k < size/4
    std::vector<std::complex<float>> realTwiddles;  // e^(-2 pi i k / size), k <= size/2
    mutable std::vector<std::complex<float>> scratch;  // Not shared between threads: one Fft per thread

    // Accelerate (Apple): vDSP's real DFT setups and its split-complex work arrays; null elsewhere.
    void* forwardSetup = nullptr;
    void* inverseSetup = nullptr;
    mutable std::vector<float> splitIn, splitOut;  // Real halves then imaginary halves, size/2 each
};

} // namespace reverb
