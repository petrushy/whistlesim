#include "Convolver.h"

#include <algorithm>
#include <cassert>

namespace reverb
{

namespace
{
int orderOf (int powerOfTwo)
{
    int order = 0;
    while ((1 << order) < powerOfTwo)
        ++order;
    return order;
}

// Partitions of impulse[first, first + count) of `partition` samples each, zero-padded to 2 * partition and transformed.
std::vector<std::vector<std::complex<float>>> transformPartitions (const Fft& fft, const float* impulse, int first, int count, int partition)
{
    std::vector<std::vector<std::complex<float>>> out;
    std::vector<float> padded ((size_t) (2 * partition));
    for (int start = 0; start < count; start += partition)
    {
        std::fill (padded.begin(), padded.end(), 0.0f);
        const int n = std::min (partition, count - start);
        std::copy (impulse + first + start, impulse + first + start + n, padded.begin());
        out.emplace_back ((size_t) partition + 1);
        fft.forward (padded.data(), out.back().data());
    }
    return out;
}

// sum += a * b, element by element. Written out in real arithmetic: std::complex's operator* has to handle infinities
// and NaNs (a library call per element), and the plain form lets the compiler use the vector units.
void multiplyAdd (std::complex<float>* sum, const std::complex<float>* a, const std::complex<float>* b, size_t n)
{
    auto* s = reinterpret_cast<float*> (sum);
    const auto* x = reinterpret_cast<const float*> (a);
    const auto* y = reinterpret_cast<const float*> (b);
    for (size_t i = 0; i < n; ++i)
    {
        const float xr = x[2 * i], xi = x[2 * i + 1], yr = y[2 * i], yi = y[2 * i + 1];
        s[2 * i] += xr * yr - xi * yi;
        s[2 * i + 1] += xr * yi + xi * yr;
    }
}
} // namespace

Convolver::Convolver (const float* impulse, int length, int headPartition, int tailStart, int stagger)
{
    assert (headPartition > 0 && (headPartition & (headPartition - 1)) == 0);
    assert (tailStart >= headPartition && (tailStart & (tailStart - 1)) == 0);
    length = std::max (length, 1);
    static const float silence = 0.0f;
    if (impulse == nullptr)
        impulse = &silence, length = 1;

    head.prepare (impulse, std::min (length, tailStart), headPartition);
    // Stages: partition B from B to 8 B, then the longest partition to the end. Each starts at its own partition size,
    // so its one-partition delay puts its output where it belongs.
    constexpr int kStageFactor = 8;
    const int longest = kStageFactor * tailStart;
    for (int partition = tailStart, start = tailStart; start < length; partition *= kStageFactor)
    {
        const int end = partition >= longest ? length : std::min (length, partition * kStageFactor);
        // Staggered by quarters of its block, in whole head blocks (the calls are cut at the head's block ends).
        const int offset = (stagger % 4) * partition / 4 / headPartition * headPartition;
        tails.emplace_back();
        tails.back().prepare (impulse + start, end - start, partition, offset);
        start = end;
    }
}

void Convolver::process (const float* in, float* out, int numSamples)
{
    // In pieces that stay within the head's current block (the tail's blocks are a whole number of the head's).
    while (numSamples > 0)
    {
        const int n = std::min (numSamples, head.partition - head.position);
        for (auto& tail : tails)
            tail.process (in, nullptr, n);  // Takes the input first: `out` may be `in`
        head.process (in, out, n);
        for (auto& tail : tails)
            tail.process (nullptr, out, n);
        in += n;
        out += n;
        numSamples -= n;
    }
}

void Convolver::reset()
{
    head.reset();
    for (auto& tail : tails)
        tail.reset();
}

//==============================================================================
void Convolver::Head::prepare (const float* impulse, int length, int partitionSize)
{
    partition = partitionSize;
    fft = Fft (orderOf (2 * partition));
    response = transformPartitions (fft, impulse, 0, length, partition);
    history.assign (response.size(), Spectrum ((size_t) partition + 1));
    earlier.assign ((size_t) partition + 1, {});
    current.assign ((size_t) partition + 1, {});
    block.assign ((size_t) (2 * partition), 0.0f);
    result.assign ((size_t) (2 * partition), 0.0f);
    overlap.assign ((size_t) partition, 0.0f);
    reset();
}

void Convolver::Head::reset()
{
    for (auto& h : history)
        std::fill (h.begin(), h.end(), std::complex<float> {});
    std::fill (earlier.begin(), earlier.end(), std::complex<float> {});
    std::fill (block.begin(), block.end(), 0.0f);
    std::fill (overlap.begin(), overlap.end(), 0.0f);
    newest = 0;
    position = 0;
}

void Convolver::Head::process (const float* in, float* out, int numSamples)
{
    const size_t bins = (size_t) partition + 1;
    std::copy (in, in + numSamples, block.begin() + position);

    // This block so far (the rest of it still zeros) times the first partition, plus the older blocks' share.
    fft.forward (block.data(), history[(size_t) newest].data());
    std::copy (earlier.begin(), earlier.end(), current.begin());
    multiplyAdd (current.data(), history[(size_t) newest].data(), response[0].data(), bins);
    fft.inverse (current.data(), result.data());
    for (int i = 0; i < numSamples; ++i)
        out[i] = result[(size_t) (position + i)] + overlap[(size_t) (position + i)];
    position += numSamples;

    if (position == partition)  // The block is complete: what spills over goes into the next one.
    {
        std::copy (result.begin() + partition, result.end(), overlap.begin());
        std::fill (block.begin(), block.end(), 0.0f);
        position = 0;
        const int numPartitions = (int) response.size();
        newest = (newest + 1) % numPartitions;
        // The next block's share from the blocks before it: block (next - k) times partition k.
        std::fill (earlier.begin(), earlier.end(), std::complex<float> {});
        for (int k = 1; k < numPartitions; ++k)
            multiplyAdd (earlier.data(), history[(size_t) ((newest - k + numPartitions) % numPartitions)].data(), response[(size_t) k].data(), bins);
    }
}

//==============================================================================
void Convolver::Tail::prepare (const float* impulse, int length, int partitionSize, int start)
{
    partition = partitionSize;
    startPosition = start;
    fft = Fft (orderOf (2 * partition));
    response = transformPartitions (fft, impulse, 0, length, partition);
    history.assign (response.size(), Spectrum ((size_t) partition + 1));
    sum.assign ((size_t) partition + 1, {});
    input.assign ((size_t) (2 * partition), 0.0f);
    result.assign ((size_t) (2 * partition), 0.0f);
    output.assign ((size_t) partition, 0.0f);
    reset();
}

void Convolver::Tail::reset()
{
    for (auto& h : history)
        std::fill (h.begin(), h.end(), std::complex<float> {});
    std::fill (input.begin(), input.end(), 0.0f);
    std::fill (output.begin(), output.end(), 0.0f);
    std::fill (sum.begin(), sum.end(), std::complex<float> {});
    newest = 0;
    // A staggered stage starts part way into its first block, as if silence had come before: its block ends fall
    // elsewhere than its neighbours', and as the input before the start was silent, the result is the same.
    position = startPosition;
    summed = 0;
}

void Convolver::Tail::process (const float* in, float* out, int numSamples)
{
    if (in != nullptr)  // Input half of the step: collect the block
    {
        std::copy (in, in + numSamples, input.begin() + partition + position);
        return;
    }

    // Output half: the tail's samples for this stretch were found when the previous block completed.
    for (int i = 0; i < numSamples; ++i)
        out[i] += output[(size_t) (position + i)];
    position += numSamples;

    // The next result's older terms (partitions 1 and up, against input windows already transformed) are summed
    // a share at a time over the block, so no single call does them all.
    const size_t bins = (size_t) partition + 1;
    const int numPartitions = (int) response.size();
    const int due = (int) ((long long) (numPartitions - 1) * position / partition);
    for (; summed < due; ++summed)
    {
        const int k = summed + 1;  // Partition k against the window k blocks before the one now completing
        multiplyAdd (sum.data(), history[(size_t) ((newest + 1 - k + numPartitions) % numPartitions)].data(), response[(size_t) k].data(), bins);
    }

    if (position == partition)
    {
        // Overlap-save: the last 2B input samples, against the first partition; the rest is in `sum` already.
        newest = (newest + 1) % numPartitions;
        fft.forward (input.data(), history[(size_t) newest].data());
        multiplyAdd (sum.data(), history[(size_t) newest].data(), response[0].data(), bins);
        fft.inverse (sum.data(), result.data());
        std::copy (result.begin() + partition, result.end(), output.begin());  // The valid half
        std::copy (input.begin() + partition, input.end(), input.begin());   // This block becomes the previous one
        std::fill (sum.begin(), sum.end(), std::complex<float> {});
        summed = 0;
        position = 0;
    }
}

} // namespace reverb
