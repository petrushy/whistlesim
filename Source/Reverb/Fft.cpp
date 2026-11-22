#include "Fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <utility>

// Accelerate with Apple's compiler (GCC can't read its headers; it gets the portable code).
#if defined(__APPLE__) && defined(__clang__)
 #define WHISTLESIM_VDSP 1
 #include <Accelerate/Accelerate.h>
#endif

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

#ifdef WHISTLESIM_VDSP
    // vDSP's real-to-complex DFT, for the sizes it takes (it returns null for others: the portable code does them).
    forwardSetup = vDSP_DFT_zrop_CreateSetup (nullptr, (vDSP_Length) size, vDSP_DFT_FORWARD);
    inverseSetup = vDSP_DFT_zrop_CreateSetup ((vDSP_DFT_Setup) forwardSetup, (vDSP_Length) size, vDSP_DFT_INVERSE);
    if (forwardSetup == nullptr || inverseSetup == nullptr)
    {
        if (forwardSetup != nullptr)
            vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) forwardSetup);
        if (inverseSetup != nullptr)
            vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) inverseSetup);
        forwardSetup = inverseSetup = nullptr;
    }
    else
    {
        splitIn.resize ((size_t) size);
        splitOut.resize ((size_t) size);
    }
#endif
}

Fft::~Fft()
{
#ifdef WHISTLESIM_VDSP
    if (forwardSetup != nullptr)
        vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) forwardSetup);
    if (inverseSetup != nullptr)
        vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) inverseSetup);
#endif
}

Fft::Fft (Fft&& other) noexcept { *this = std::move (other); }

Fft& Fft::operator= (Fft&& other) noexcept
{
    if (this == &other)
        return *this;
#ifdef WHISTLESIM_VDSP
    if (forwardSetup != nullptr)
        vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) forwardSetup);
    if (inverseSetup != nullptr)
        vDSP_DFT_DestroySetup ((vDSP_DFT_Setup) inverseSetup);
#endif
    size = other.size;
    half = other.half;
    bitReversed = std::move (other.bitReversed);
    twiddles = std::move (other.twiddles);
    realTwiddles = std::move (other.realTwiddles);
    scratch = std::move (other.scratch);
    splitIn = std::move (other.splitIn);
    splitOut = std::move (other.splitOut);
    forwardSetup = std::exchange (other.forwardSetup, nullptr);
    inverseSetup = std::exchange (other.inverseSetup, nullptr);
    return *this;
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
                // The butterfly in real arithmetic (std::complex's operator* handles infinities and NaNs, slowly).
                const auto w = twiddles[(size_t) (k * stride)];
                const float wr = w.real(), wi = inverse ? -w.imag() : w.imag();
                const auto a = data[start + k], x = data[start + k + halfLength];
                const float br = x.real() * wr - x.imag() * wi, bi = x.real() * wi + x.imag() * wr;
                data[start + k] = { a.real() + br, a.imag() + bi };
                data[start + k + halfLength] = { a.real() - br, a.imag() - bi };
            }
        }
    }
}

void Fft::forward (const float* samples, std::complex<float>* bins) const
{
#ifdef WHISTLESIM_VDSP
    if (forwardSetup != nullptr)
    {
        // vDSP takes the even samples and the odd ones apart, and gives the bins scaled by 2, with bin 0's real part in
        // the first real slot and bin N/2's (also real) in the first imaginary one.
        float* evens = splitIn.data();
        float* odds = splitIn.data() + half;
        DSPSplitComplex in { evens, odds };
        vDSP_ctoz (reinterpret_cast<const DSPComplex*> (samples), 2, &in, 1, (vDSP_Length) half);
        float* re = splitOut.data();
        float* im = splitOut.data() + half;
        vDSP_DFT_Execute ((vDSP_DFT_Setup) forwardSetup, evens, odds, re, im);
        bins[0] = { 0.5f * re[0], 0.0f };
        bins[half] = { 0.5f * im[0], 0.0f };
        for (int k = 1; k < half; ++k)
            bins[k] = { 0.5f * re[k], 0.5f * im[k] };
        return;
    }
#endif
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
#ifdef WHISTLESIM_VDSP
    if (inverseSetup != nullptr)
    {
        // The same packing back; vDSP's inverse gives the samples scaled by N (even and odd apart).
        float* re = splitIn.data();
        float* im = splitIn.data() + half;
        re[0] = bins[0].real();
        im[0] = bins[half].real();
        for (int k = 1; k < half; ++k)
        {
            re[k] = bins[k].real();
            im[k] = bins[k].imag();
        }
        float* evens = splitOut.data();
        float* odds = splitOut.data() + half;
        vDSP_DFT_Execute ((vDSP_DFT_Setup) inverseSetup, re, im, evens, odds);
        DSPSplitComplex out { evens, odds };
        vDSP_ztoc (&out, 1, reinterpret_cast<DSPComplex*> (samples), 2, (vDSP_Length) half);
        const float scale = 1.0f / (float) size;
        vDSP_vsmul (samples, 1, &scale, samples, 1, (vDSP_Length) size);
        return;
    }
#endif
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
