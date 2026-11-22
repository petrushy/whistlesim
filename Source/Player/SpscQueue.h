// Single-producer single-consumer queue (MIDI thread -> one consumer), no locks or allocation.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item)
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);
        const auto next = (write + 1) % kSlots;
        if (next == readIndex.load (std::memory_order_acquire))
            return false;  // Full
        buffer[write] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item)
    {
        const auto read = readIndex.load (std::memory_order_relaxed);
        if (read == writeIndex.load (std::memory_order_acquire))
            return false;  // Empty
        item = buffer[read];
        readIndex.store ((read + 1) % kSlots, std::memory_order_release);
        return true;
    }

    // Consumer side (or when neither side is running): drops everything waiting.
    void clear() { readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release); }

    int getNumReady() const
    {
        const auto write = writeIndex.load (std::memory_order_acquire), read = readIndex.load (std::memory_order_acquire);
        return (int) ((write + kSlots - read) % kSlots);
    }

private:
    static constexpr size_t kSlots = (size_t) Capacity + 1;  // One slot stays empty to tell full from empty
    std::array<T, kSlots> buffer {};
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 };
};
