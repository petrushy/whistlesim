// Convolution with an impulse response, with no added latency, for any block size. The response is cut in pieces:
//  - the head (the first tailStart samples) in short partitions. Each call transforms the partly filled current block,
//    so its output is ready at once (as juce::dsp::Convolution does);
//  - the tail in longer partitions, by uniformly partitioned overlap-save: tailStart samples each up to 8 x tailStart,
//    then 8 x tailStart each to the end (a long reverb's late part needs no fine timing, and long partitions take far
//    fewer multiplications per sample). A stage of partition B starts at B into the response and its result comes one
//    partition late, which is exactly where it belongs, so it costs no latency either. Each stage's sums over its older
//    partitions are spread over the calls of a block, so the work per call stays even.
// Everything is allocated in the constructor; process() doesn't allocate or lock (audio thread).

#pragma once

#include "Fft.h"

#include <complex>
#include <vector>

namespace reverb
{

class Convolver
{
public:
    // headPartition and tailStart are powers of two, headPartition <= tailStart. Convolvers running side by side (the
    // reverb's four) get different `stagger` (0-3): each tail stage then ends its blocks a quarter of a block apart
    // from the others', so their transforms don't all fall in the same audio callback. The result is the same.
    Convolver (const float* impulse, int length, int headPartition = 128, int tailStart = 1024, int stagger = 0);

    // Replaces out[0 .. n) with in convolved with the response (in and out may be the same).
    void process (const float* in, float* out, int numSamples);

    void reset();

private:
    using Spectrum = std::vector<std::complex<float>>;

    // The head: overlap-add, the current block transformed on every call.
    struct Head
    {
        int partition = 0;                  // P: block length; transforms are 2P
        Fft fft { 1 };
        std::vector<Spectrum> response;     // Partitions of the response, transformed
        std::vector<Spectrum> history;      // The input blocks' spectra, newest at `newest`
        int newest = 0;
        Spectrum earlier;                   // Sum over partitions k >= 1: older blocks times response k
        Spectrum current;                   // Work: this block's spectrum, then the total
        std::vector<float> block;           // 2P: this block's input so far, then zeros
        std::vector<float> result;          // 2P: the inverse transform
        std::vector<float> overlap;         // P: the previous block's second half
        int position = 0;                   // Samples of this block so far

        void prepare (const float* impulse, int length, int partitionSize);
        void process (const float* in, float* out, int numSamples);  // Within one block
        void reset();
    };

    // The tail: overlap-save, a whole partition at a time, one partition late.
    struct Tail
    {
        int partition = 0;                  // B
        Fft fft { 1 };
        std::vector<Spectrum> response;
        std::vector<Spectrum> history;
        int newest = 0;
        Spectrum sum;
        std::vector<float> input;           // 2B: the previous block and this one
        std::vector<float> result;          // 2B
        std::vector<float> output;          // B: the tail's output for the samples of this block
        int position = 0;
        int summed = 0;                     // Older partitions already added into `sum` for the next result
        int startPosition = 0;              // Where in its first block it starts (staggering)

        void prepare (const float* impulse, int length, int partitionSize, int start);
        void process (const float* in, float* out, int numSamples);  // Adds to out; within one block
        void reset();
    };

    Head head;
    std::vector<Tail> tails;  // Longer partitions further in
};

} // namespace reverb
