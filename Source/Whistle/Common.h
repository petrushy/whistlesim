// Small helpers the physics engine uses instead of a framework's, so Source/Whistle builds with the standard
// library alone (the iPhone app uses the engine without JUCE).

#pragma once

#include <atomic>
#include <functional>
#include <numbers>
#include <thread>

namespace whistle
{

constexpr double kPi = std::numbers::pi;

// limit (lo, hi, v): v kept between lo and hi. The argument order and the comparisons are those of juce::jlimit,
// so results stay bit-identical when lo > hi.
template <typename T>
constexpr T limit (T lo, T hi, T v) noexcept
{
    return v < lo ? lo : (hi < v ? hi : v);
}

// An exact floating-point comparison, said on purpose (compilers warn about == on floats).
template <typename T>
constexpr bool exactlyEqual (T a, T b) noexcept
{
    return std::equal_to<T>() (a, b);
}

// A lock for very short sections: setGeometry hands a new configuration to the audio thread, which only try-locks.
class SpinLock
{
public:
    bool tryEnter() noexcept
    {
        bool expected = false;
        return locked.compare_exchange_strong (expected, true, std::memory_order_acquire);
    }

    void enter() noexcept
    {
        for (int spin = 0; spin < 40; ++spin)
            if (tryEnter())
                return;
        while (! tryEnter())
            std::this_thread::yield();
    }

    void exit() noexcept { locked.store (false, std::memory_order_release); }

    struct ScopedLock
    {
        explicit ScopedLock (SpinLock& l) noexcept : lock (l) { lock.enter(); }
        ~ScopedLock() { lock.exit(); }
        ScopedLock (const ScopedLock&) = delete;
        ScopedLock& operator= (const ScopedLock&) = delete;
        SpinLock& lock;
    };

    struct ScopedTryLock
    {
        explicit ScopedTryLock (SpinLock& l) noexcept : lock (l), held (l.tryEnter()) {}
        ~ScopedTryLock()
        {
            if (held)
                lock.exit();
        }
        bool isLocked() const noexcept { return held; }
        ScopedTryLock (const ScopedTryLock&) = delete;
        ScopedTryLock& operator= (const ScopedTryLock&) = delete;
        SpinLock& lock;
        const bool held;
    };

private:
    std::atomic<bool> locked { false };
};

} // namespace whistle
