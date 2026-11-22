#include "Fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace reverb
{

// A real transform of N points is done as a complex one of N/2 points (even samples as the real part, odd as the
// imaginary), then split into the two halves' spectra and combined.

Fft::Fft (int order) : size (1 << order), half (size / 2)
{
    assert (order >= 1);
    bitReversed.resize ((size_t) half);
    int bits = 0;
    while ((1 << bits) < half)
        ++bits;
    for (int i = 0; i < half; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            r |= ((i >> b) & 1) << (bits - 1 - b);
        bitReversed[(size_t) i] = r;
    }
    twiddles.resize ((size_t) std::max (1, half / 2));
    for (size_t k = 0; k < twiddles.size(); ++k)
    {
        const double angle = -2.0 * std::numbers::pi * (double) k / (double) half;
        twiddles[k] = { (float) std::cos (angle), (float) std::sin (angle) };
    }
    realTwiddles.resize ((size_t) half + 1);
    for (size_t k = 0; k <= (size_t) half; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * (double) k / (double) size;
        realTwiddles[k] = { (float) std::cos (angle), (float) std::sin (angle) };
    }
    scratch.resize ((size_t) half);
}

void Fft::complexTransform (std::complex<float>* data, bool inverse) const
{
    for (int i = 0; i < half; ++i)
        if (i < bitReversed[(size_t) i])
            std::swap (data[i], data[bitReversed[(size_t) i]]);

    for (int length = 2; length <= half; length <<= 1)
    {
        const int halfLength = length / 2, stride = half / length;
        for (int start = 0; start < half; start += length)
        {
            for (int k = 0; k < halfLength; ++k)
            {
                auto w = twiddles[(size_t) (k * stride)];
                if (inverse)
                    w = std::conj (w);
                const auto a = data[start + k];
                const auto b = data[start + k + halfLength] * w;
                data[start + k] = a + b;
                data[start + k + halfLength] = a - b;
            }
        }
    }
}

void Fft::forward (const float* samples, std::complex<float>* bins) const
{
    auto* z = scratch.data();
    for (int n = 0; n < half; ++n)
        z[n] = { samples[2 * n], samples[2 * n + 1] };
    complexTransform (z, false);

    // X[k] = E[k] + W^k O[k], with E and O the even and odd samples' spectra taken apart from z's.
    for (int k = 0; k <= half; ++k)
    {
        const auto zk = z[k % half], zm = std::conj (z[(half - k) % half]);
        const auto even = 0.5f * (zk + zm);
        const auto odd = std::complex<float> (0.0f, -0.5f) * (zk - zm);
        bins[k] = even + realTwiddles[(size_t) k] * odd;
    }
}

void Fft::inverse (const std::complex<float>* bins, float* samples) const
{
    auto* z = scratch.data();
    for (int k = 0; k < half; ++k)
    {
        const auto xk = bins[k], xm = std::conj (bins[half - k]);
        const auto even = 0.5f * (xk + xm);
        const auto odd = 0.5f * (xk - xm) * std::conj (realTwiddles[(size_t) k]);
        z[k] = even + std::complex<float> (0.0f, 1.0f) * odd;
    }
    complexTransform (z, true);
    const float scale = 1.0f / (float) half;
    for (int n = 0; n < half; ++n)
    {
        samples[2 * n] = z[n].real() * scale;
        samples[2 * n + 1] = z[n].imag() * scale;
    }
}

void Fft::forward (float* data) const
{
    std::vector<std::complex<float>> bins ((size_t) half + 1);
    forward (data, bins.data());
    for (int k = 0; k <= half; ++k)
    {
        data[2 * k] = bins[(size_t) k].real();
        data[2 * k + 1] = bins[(size_t) k].imag();
    }
}

void Fft::inverse (float* data) const
{
    std::vector<std::complex<float>> bins ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
        bins[(size_t) k] = { data[2 * k], data[2 * k + 1] };
    inverse (bins.data(), data);
}

} // namespace reverb
