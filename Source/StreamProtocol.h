// WARBL2 sensor stream protocol. No JUCE dependency, so the same decoder can be used by the test app, a synth plugin,
// or tests.
//
// The official firmware's stream (WARBL 4.8 and later): SysEx data (without F0/F7) 00 03 0F <version 3> <type>
// <payload>, with WARBL's registered manufacturer ID. Spec: docs/WARBL_sensor_stream_protocol_v3.md; firmware:
// Sensor_stream.ino in amowry/WARBL2. Payloads are bit-packed MSB-first, 7 bits per data byte. The motion data is the
// accelerometer's y axis plus WARBL's own roll, elevation and yaw.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace warbl
{

constexpr std::array<uint8_t, 3> kWarblId { 0x00, 0x03, 0x0F };  // WARBL's registered MIDI manufacturer ID
constexpr uint8_t kProtocolVersion = 3;  // What the firmware sends (and the encoders below write)

enum MessageType : uint8_t
{
    kFrame = 0x01,
    kBatch = 0x02,  // Bluetooth: several samples in one message (see decodeBatch)
    kInfo = 0x10,
    kConfig = 0x20,
    kRequestInfo = 0x21
};

enum ConfigFlags : uint8_t
{
    kStreamOn = 0x01,
    kSuppressNotes = 0x02,
    // The host repeats the command every second; WARBL stops the stream ~3 s after it stops doing so. The spec keeps
    // this bit as an optional legacy keep-alive flag; the firmware ignores it.
    kKeepAlive = 0x04
};

constexpr int kNumHoles = 9;
constexpr double kDeviceTicksPerSecond = 32768.0;  // Frame timestamps are RTC2 ticks.
constexpr uint32_t kTimestampBits = 21;            // Wraps every 64 s.
constexpr uint32_t kSeqBits = 14;
constexpr float kAngleScale = 32767.0f / 180.0f;   // Angles: counts per degree, signed 16-bit, +-180 degrees

// Tone hole order as used by the firmware (holeCovered bit i is hole i).
inline const char* holeName (int index)
{
    static const char* names[kNumHoles] = { "Bell", "R4", "R3", "R2", "R1", "L3", "L2", "L1", "LT" };
    return index >= 0 && index < kNumHoles ? names[index] : "?";
}

struct SensorFrame
{
    uint16_t seq = 0;
    uint32_t timestamp = 0;                    // Device time, 1/32768 s, 21 bits.
    std::array<uint16_t, kNumHoles> holes {};  // 10-bit, ambient-light corrected, unsmoothed, no baseline subtracted.
    uint16_t pressure = 0;                     // 14-bit raw ADC.
    int16_t accelY = 0;                        // Raw LSM6DSOX counts along WARBL's long axis.
    std::array<int16_t, 3> orientation {};     // WARBL's roll, elevation and yaw, kAngleScale counts per degree.
    uint16_t holeCovered = 0;                  // Firmware's covered/uncovered decision (hint; not sent over Bluetooth).
    uint8_t flags = 0;

    bool holesUpdated() const { return (flags & 0x01) != 0; }
    bool holesRejected() const { return (flags & 0x02) != 0; }  // WARBL rejected a garbled tone hole transfer (kept the last)
    // Roll (0), elevation (1) or yaw (2) in degrees, -180..180.
    float angleDegrees (int axis) const { return (float) orientation[(size_t) axis] / kAngleScale; }

    // The angle of WARBL's long axis to the horizontal, in degrees: WARBL's own elevation, fused from gyro and
    // accelerometer, which rolling the instrument doesn't change.
    float elevationDegrees() const { return angleDegrees (1); }
};

struct DeviceInfo
{
    uint16_t firmwareVersion = 0;
    std::array<uint16_t, kNumHoles> baseline {};  // Reading with the hole open.
    // Reading with the hole covered, from calibration: an absolute reading like the baseline. (The v3 spec calls these thresholds "relative to baseline", but the firmware sends toneholeCovered as it is.)
    std::array<uint16_t, kNumHoles> covered {};
    uint16_t ambientPressure = 0;                 // 14-bit
    uint16_t accelRange = 0;                      // Adafruit lsm6ds_accel_range_t
    uint16_t gyroRange = 0;                       // Adafruit lsm6ds_gyro_range_t (sent, but no gyro readings are)
    uint16_t loopPeriodMs = 0;

    // 0 = open, 1 = covered as at calibration. Can go a little outside 0..1.
    float holeCoverage (int hole, uint16_t raw) const
    {
        const float span = (float) covered[(size_t) hole] - (float) baseline[(size_t) hole];
        if (span <= 0.0f)
            return 0.0f;
        return ((float) raw - (float) baseline[(size_t) hole]) / span;
    }

    // 14-bit pressure relative to the ambient reading taken at startup.
    int pressureAboveAmbient (uint16_t raw) const { return (int) raw - (int) ambientPressure; }

    // Raw accelerometer counts to g (LSM6DSOX sensitivities, range codes as in Adafruit_LSM6DS.h).
    float accelToG (int16_t raw) const
    {
        switch (accelRange)
        {
            case 0: return raw * 0.061e-3f;  // 2 g
            case 1: return raw * 0.488e-3f;  // 16 g
            case 2: return raw * 0.122e-3f;  // 4 g
            case 3: return raw * 0.244e-3f;  // 8 g
            default: return raw * 0.122e-3f;
        }
    }
};

class BitReader
{
public:
    BitReader (const uint8_t* bytes, size_t numBytes) : data (bytes), size (numBytes) {}

    bool canRead (size_t bits) const { return (pos + bits + 6) / 7 <= size; }

    uint32_t get (int bits)
    {
        uint32_t value = 0;
        for (int i = 0; i < bits; ++i, ++pos)
            value = (value << 1) | ((data[pos / 7] >> (6 - pos % 7)) & 1u);
        return value;
    }

    int16_t getSigned16() { return (int16_t) (uint16_t) get (16); }

private:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
};

// Mirrors SysExBitWriter in the firmware; used for building commands and in tests.
class BitWriter
{
public:
    void put (uint32_t value, int bits)
    {
        for (int b = bits - 1; b >= 0; --b)
        {
            if (bitsInByte == 0)
                bytes.push_back (0);
            bytes.back() |= (uint8_t) (((value >> b) & 1u) << (6 - bitsInByte));
            bitsInByte = (bitsInByte + 1) % 7;
        }
    }

    std::vector<uint8_t> bytes;

private:
    int bitsInByte = 0;
};

// The header of a WARBL stream SysEx (data without F0/F7).
struct Header
{
    uint8_t version = 0;
    uint8_t type = 0;
    size_t size = 0;  // Header bytes before the payload
};

inline std::optional<Header> parseHeader (const uint8_t* data, size_t size)
{
    if (size >= kWarblId.size() + 2 && std::equal (kWarblId.begin(), kWarblId.end(), data))
    {
        const uint8_t version = data[kWarblId.size()];
        if (version != kProtocolVersion)  // The firmware accepts commands as 1-3, but streams only version 3
            return std::nullopt;
        return Header { version, data[kWarblId.size() + 1], kWarblId.size() + 2 };
    }
    return std::nullopt;
}

// The message type of a WARBL stream SysEx (data without F0/F7), or nothing if it isn't one.
inline std::optional<uint8_t> messageType (const uint8_t* data, size_t size)
{
    if (const auto header = parseHeader (data, size))
        return header->type;
    return std::nullopt;
}

// The header bytes for a message the official firmware would send (version 3).
inline std::vector<uint8_t> makeHeader (uint8_t type)
{
    return { kWarblId[0], kWarblId[1], kWarblId[2], kProtocolVersion, type };
}

constexpr size_t kFrameBits = kSeqBits + kTimestampBits + kNumHoles * 10 + 14 + 4 * 16 + 9 + 7;
constexpr size_t kInfoValues = 1 + kNumHoles * 2 + 1 + 3 + 1;

// Bluetooth batch: the samples of one BLE connection interval in one message, so it fits one BLE packet.
// Header: first sequence number, timestamps of the first and last sample, count, the motion once per batch (accel y,
// roll, elevation, yaw, 16 bits each), flags. Per sample: pressure (14 bits), then the nine holes (10).
constexpr size_t kBatchHeaderBits = kSeqBits + 2 * kTimestampBits + 5 + 4 * 16 + 7;
constexpr size_t kBatchSampleBits = 14 + kNumHoles * 10;
constexpr int kMaxBatchSamples = 16;

inline std::optional<SensorFrame> decodeFrame (const uint8_t* data, size_t size)
{
    const auto header = parseHeader (data, size);
    if (! header || header->type != kFrame)
        return std::nullopt;

    BitReader r (data + header->size, size - header->size);
    if (! r.canRead (kFrameBits))
        return std::nullopt;

    SensorFrame f;
    f.seq = (uint16_t) r.get (kSeqBits);
    f.timestamp = r.get (kTimestampBits);
    for (auto& h : f.holes)
        h = (uint16_t) r.get (10);
    f.pressure = (uint16_t) r.get (14);
    f.accelY = r.getSigned16();
    for (auto& angle : f.orientation)
        angle = r.getSigned16();
    f.holeCovered = (uint16_t) r.get (9);
    f.flags = (uint8_t) r.get (7);
    return f;
}

// A batch as frames, with interpolated timestamps (empty if it isn't a valid batch).
inline std::vector<SensorFrame> decodeBatch (const uint8_t* data, size_t size)
{
    std::vector<SensorFrame> frames;
    const auto header = parseHeader (data, size);
    if (! header || header->type != kBatch)
        return frames;

    BitReader r (data + header->size, size - header->size);
    if (! r.canRead (kBatchHeaderBits))
        return frames;
    const auto seq = r.get (kSeqBits);
    const auto first = r.get (kTimestampBits), last = r.get (kTimestampBits);
    const int count = (int) r.get (5);
    const int16_t accelY = r.getSigned16();
    std::array<int16_t, 3> orientation {};
    for (auto& angle : orientation)
        angle = r.getSigned16();
    const auto flags = (uint8_t) r.get (7);
    if (count < 1 || count > kMaxBatchSamples || ! r.canRead (kBatchSampleBits * (size_t) count))
        return frames;

    const uint32_t span = (last - first) & ((1u << kTimestampBits) - 1);
    for (int i = 0; i < count; ++i)
    {
        SensorFrame f;
        f.seq = (uint16_t) ((seq + (uint32_t) i) & ((1u << kSeqBits) - 1));
        f.timestamp = (first + (count > 1 ? span * (uint32_t) i / (uint32_t) (count - 1) : 0)) & ((1u << kTimestampBits) - 1);
        f.pressure = (uint16_t) r.get (14);
        for (auto& h : f.holes)
            h = (uint16_t) r.get (10);
        f.accelY = accelY;  // The motion was read once, near the batch's last sample
        f.orientation = orientation;
        f.flags = flags;  // ORed over the batch
        frames.push_back (f);
    }
    return frames;
}

inline std::optional<DeviceInfo> decodeInfo (const uint8_t* data, size_t size)
{
    const auto header = parseHeader (data, size);
    if (! header || header->type != kInfo)
        return std::nullopt;

    BitReader r (data + header->size, size - header->size);
    if (! r.canRead (kInfoValues * 16))
        return std::nullopt;

    DeviceInfo info;
    info.firmwareVersion = (uint16_t) r.get (16);
    for (auto& b : info.baseline)
        b = (uint16_t) r.get (16);
    for (auto& c : info.covered)
        c = (uint16_t) r.get (16);
    r.get (16);  // The ambient pressure at 10 bits (the firmware's sensorCalibration); the 14-bit one follows
    info.accelRange = (uint16_t) r.get (16);
    info.gyroRange = (uint16_t) r.get (16);
    info.loopPeriodMs = (uint16_t) r.get (16);
    info.ambientPressure = (uint16_t) r.get (16);
    return info;
}

// Packs a frame as the official firmware does (version 3 payload, without the header). For the simulator and tests.
inline std::vector<uint8_t> encodeFramePayload (const SensorFrame& f)
{
    BitWriter w;
    w.put (f.seq & ((1u << kSeqBits) - 1), kSeqBits);
    w.put (f.timestamp & ((1u << kTimestampBits) - 1), kTimestampBits);
    for (auto h : f.holes)
        w.put (h & 0x3FFu, 10);
    w.put (f.pressure & 0x3FFFu, 14);
    w.put ((uint16_t) f.accelY, 16);
    for (auto angle : f.orientation)
        w.put ((uint16_t) angle, 16);
    w.put (f.holeCovered & 0x1FFu, 9);
    w.put (f.flags & 0x7Fu, 7);
    return w.bytes;
}

// Packs frames (at most 16) as a version 3 Bluetooth batch (payload only), as the official firmware does: the
// motion is the last frame's, the flags are ORed. For the simulator and tests.
inline std::vector<uint8_t> encodeBatchPayload (const std::vector<SensorFrame>& frames)
{
    BitWriter w;
    if (frames.empty())
        return w.bytes;
    uint8_t flags = 0;
    for (const auto& f : frames)
        flags |= f.flags;
    w.put (frames.front().seq & ((1u << kSeqBits) - 1), kSeqBits);
    w.put (frames.front().timestamp & ((1u << kTimestampBits) - 1), kTimestampBits);
    w.put (frames.back().timestamp & ((1u << kTimestampBits) - 1), kTimestampBits);
    w.put ((uint32_t) frames.size() & 0x1Fu, 5);
    w.put ((uint16_t) frames.back().accelY, 16);
    for (auto angle : frames.back().orientation)
        w.put ((uint16_t) angle, 16);
    w.put (flags & 0x7Fu, 7);
    for (const auto& f : frames)
    {
        w.put (f.pressure & 0x3FFFu, 14);
        for (auto h : f.holes)
            w.put (std::min<uint16_t> (h, 1023), 10);
    }
    return w.bytes;
}

inline std::vector<uint8_t> encodeInfoPayload (const DeviceInfo& info)
{
    BitWriter w;
    w.put (info.firmwareVersion, 16);
    for (auto b : info.baseline)
        w.put (b, 16);
    for (auto c : info.covered)
        w.put (c, 16);
    w.put ((uint32_t) info.ambientPressure >> 4, 16);
    w.put (info.accelRange, 16);
    w.put (info.gyroRange, 16);
    w.put (info.loopPeriodMs, 16);
    w.put (info.ambientPressure, 16);
    return w.bytes;
}

// Host -> WARBL commands (SysEx data without F0/F7). The host repeats an active config every second: WARBL stops a
// stream it hasn't renewed for 3 s, so a crashed or killed host can't leave it streaming.
inline std::vector<uint8_t> makeConfig (bool streamOn, bool suppressNotes)
{
    const auto flags = (uint8_t) ((streamOn ? kStreamOn : 0) | (streamOn && suppressNotes ? kSuppressNotes : 0) | (streamOn ? kKeepAlive : 0));
    auto data = makeHeader (kConfig);
    data.push_back (flags);
    return data;
}

inline std::vector<uint8_t> makeRequestInfo()
{
    return makeHeader (kRequestInfo);
}

// Extends wrapping device timestamps and sequence numbers to continuous counters.
class Unwrapper
{
public:
    explicit Unwrapper (uint32_t bits) : mask ((1u << bits) - 1) {}

    int64_t next (uint32_t value)
    {
        if (! started)
        {
            started = true;
            total = value;
        }
        else
        {
            total += (int64_t) ((value - last) & mask);
        }
        last = value;
        return total;
    }

    void reset() { started = false; }

private:
    uint32_t mask;
    uint32_t last = 0;
    int64_t total = 0;
    bool started = false;
};

} // namespace warbl
