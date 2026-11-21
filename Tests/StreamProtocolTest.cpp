// Checks the host decoder against the firmware's packing (BitWriter mirrors SysExBitWriter in the firmware's
// Sensor_stream.ino): version 3 as in docs/WARBL_sensor_stream_protocol_v3.md (firmware 4.8), and the experimental
// firmware's versions 1 and 2.

#include "StreamProtocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace warbl;

static int failures = 0;

#define CHECK(condition)                                                    \
    do                                                                      \
    {                                                                       \
        if (! (condition))                                                  \
        {                                                                   \
            std::printf ("FAILED line %d: %s\n", __LINE__, #condition);     \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

// SysEx data (without F0/F7) as the official firmware sends it.
static std::vector<uint8_t> official (uint8_t type, const std::vector<uint8_t>& payload)
{
    auto data = makeHeader (type);
    data.insert (data.end(), payload.begin(), payload.end());
    return data;
}

// As the experimental firmware sends it (7D 57, versions 1-2).
static std::vector<uint8_t> legacy (uint8_t type, const std::vector<uint8_t>& payload, uint8_t version = 2)
{
    std::vector<uint8_t> data { kLegacyId[0], kLegacyId[1], version, type };
    data.insert (data.end(), payload.begin(), payload.end());
    return data;
}

static int16_t angleCounts (double degrees) { return (int16_t) std::lround (degrees * kAngleScale); }

int main()
{
    // The spec's packing example: the 14-bit value 0x1234 on a 7-bit boundary is 24 34.
    {
        BitWriter w;
        w.put (0x1234, 14);
        CHECK (w.bytes.size() == 2 && w.bytes[0] == 0x24 && w.bytes[1] == 0x34);
    }

    // A version 3 USB frame, packed in the spec's field order, with extreme and negative values.
    {
        BitWriter w;
        w.put (16383, 14);
        w.put (0x1FFFFF, 21);
        const uint16_t holes[9] = { 0, 1023, 512, 1, 2, 3, 4, 5, 1000 };
        for (auto h : holes)
            w.put (h, 10);
        w.put (16383, 14);
        w.put ((uint16_t) -2048, 16);                // Accel y
        w.put ((uint16_t) angleCounts (-180.0), 16);  // Roll
        w.put ((uint16_t) angleCounts (45.0), 16);    // Elevation
        w.put ((uint16_t) angleCounts (180.0), 16);   // Yaw
        w.put (0x155, 9);
        w.put (3, 7);

        CHECK (w.bytes.size() == 32);  // The spec: 219 bits, 32 packed bytes
        for (auto b : w.bytes)
            CHECK (b < 0x80);

        const auto data = official (kFrame, w.bytes);
        CHECK (data.size() + 2 == 39);  // 39 SysEx bytes with F0/F7
        const auto f = decodeFrame (data.data(), data.size());
        CHECK (f.has_value());
        if (f)
        {
            CHECK (f->version == 3 && f->hasOrientation());
            CHECK (f->seq == 16383);
            CHECK (f->timestamp == 0x1FFFFF);
            for (int i = 0; i < 9; ++i)
                CHECK (f->holes[(size_t) i] == holes[i]);
            CHECK (f->pressure == 16383);
            CHECK (f->accel[0] == 0 && f->accel[1] == -2048 && f->accel[2] == 0);
            CHECK (std::abs (f->angleDegrees (0) + 180.0f) < 0.01f);
            CHECK (std::abs (f->angleDegrees (1) - 45.0f) < 0.01f);
            CHECK (std::abs (f->angleDegrees (2) - 180.0f) < 0.01f);
            CHECK (f->elevationDegrees() && std::abs (*f->elevationDegrees() - 45.0f) < 0.01f);
            CHECK (f->gyro == (std::array<int16_t, 3> {}));
            CHECK (f->holeCovered == 0x155);
            CHECK (f->holesUpdated() && f->holesRejected());
        }

        // Truncated, foreign or unknown-version messages are rejected.
        CHECK (! decodeFrame (data.data(), data.size() - 1).has_value());
        auto foreign = data;
        foreign[2] = 0x10;
        CHECK (! decodeFrame (foreign.data(), foreign.size()).has_value());
        auto oldVersion = data;
        oldVersion[3] = 2;  // The registered ID only ever streams version 3
        CHECK (! decodeFrame (oldVersion.data(), oldVersion.size()).has_value());
        CHECK (! decodeBatch (data.data(), data.size()).size());  // A frame isn't a batch
    }

    // The experimental firmware's frames: version 2 (accel and gyro xyz), and version 1 (12-bit pressure).
    {
        BitWriter w;
        w.put (5, 14);
        w.put (6, 21);
        for (int i = 0; i < 9; ++i)
            w.put (100, 10);
        w.put (16383, 14);
        const int16_t imu[6] = { -2048, 100, -1, 0, 32767, -32768 };
        for (auto v : imu)
            w.put ((uint16_t) v, 16);
        w.put (0x155, 9);
        w.put (1, 7);
        CHECK (w.bytes.size() == 36);

        const auto data = legacy (kFrame, w.bytes);
        const auto f = decodeFrame (data.data(), data.size());
        CHECK (f && f->version == 2 && ! f->hasOrientation());
        if (f)
        {
            CHECK (f->pressure == 16383 && f->holes[8] == 100);
            CHECK (f->accel[0] == -2048 && f->accel[1] == 100 && f->accel[2] == -1);
            CHECK (f->gyro[0] == 0 && f->gyro[1] == 32767 && f->gyro[2] == -32768);
            CHECK (f->holeCovered == 0x155 && f->holesUpdated());
        }

        BitWriter v1;
        v1.put (5, 14);
        v1.put (6, 21);
        for (int i = 0; i < 9; ++i)
            v1.put (100, 10);
        v1.put (1000, 12);
        for (int i = 0; i < 6; ++i)
            v1.put (0, 16);
        v1.put (0, 9);
        v1.put (1, 7);
        const auto old = legacy (kFrame, v1.bytes, 1);
        const auto f1 = decodeFrame (old.data(), old.size());
        CHECK (f1 && f1->pressure == 4000 && f1->holes[8] == 100 && f1->holesUpdated());
        CHECK (f1 && ! f1->elevationDegrees());  // No IMU reading

        auto future = data;
        future[2] = 3;  // 7D 57 never had a version 3
        CHECK (! decodeFrame (future.data(), future.size()).has_value());
    }

    // Elevation: WARBL's own angle in version 3, from gravity before that.
    {
        SensorFrame level;
        level.version = 2;
        level.accel = { 0, 0, 8197 };
        CHECK (level.elevationDegrees() && std::abs (*level.elevationDegrees()) < 0.01f);
        SensorFrame up = level;
        up.accel = { 0, 5000, 5000 };
        CHECK (up.elevationDegrees() && std::abs (*up.elevationDegrees() - 45.0f) < 0.01f);
        SensorFrame v3;
        v3.orientation = { 0, angleCounts (-30.0), 0 };
        CHECK (v3.elevationDegrees() && std::abs (*v3.elevationDegrees() + 30.0f) < 0.01f);
    }

    // Random version 3 round trips through the firmware's field order.
    {
        std::srand (1);
        for (int t = 0; t < 10000; ++t)
        {
            SensorFrame in;
            in.seq = (uint16_t) (std::rand() & 0x3FFF);
            in.timestamp = (uint32_t) std::rand() & 0x1FFFFF;
            for (auto& h : in.holes)
                h = (uint16_t) (std::rand() & 0x3FF);
            in.pressure = (uint16_t) (std::rand() & 0x3FFF);
            in.accel[1] = (int16_t) std::rand();
            for (auto& a : in.orientation)
                a = (int16_t) std::rand();
            in.holeCovered = (uint16_t) (std::rand() & 0x1FF);
            in.flags = (uint8_t) (std::rand() & 0x7F);

            BitWriter w;
            w.put (in.seq, 14);
            w.put (in.timestamp, 21);
            for (auto h : in.holes)
                w.put (h, 10);
            w.put (in.pressure, 14);
            w.put ((uint16_t) in.accel[1], 16);
            for (auto a : in.orientation)
                w.put ((uint16_t) a, 16);
            w.put (in.holeCovered, 9);
            w.put (in.flags, 7);
            CHECK (w.bytes == encodeFramePayload (in));  // The simulator's encoder packs the same

            const auto data = official (kFrame, w.bytes);
            const auto out = decodeFrame (data.data(), data.size());
            CHECK (out && out->seq == in.seq && out->timestamp == in.timestamp && out->holes == in.holes
                   && out->pressure == in.pressure && out->accel == in.accel && out->orientation == in.orientation
                   && out->holeCovered == in.holeCovered && out->flags == in.flags);
            if (failures > 0)
                break;
        }
    }

    // Info message: the same layout in versions 2 and 3; version 1 lacks the 14-bit ambient.
    {
        BitWriter w;
        w.put (48, 16);
        for (int i = 0; i < 9; ++i)
            w.put ((uint32_t) (10 + i), 16);
        for (int i = 0; i < 9; ++i)
            w.put ((uint32_t) (500 + i), 16);
        w.put (100, 16);
        w.put (2, 16);
        w.put (0b1100, 16);
        w.put (3, 16);

        const auto old = legacy (kInfo, w.bytes, 1);
        const auto oldInfo = decodeInfo (old.data(), old.size());
        CHECK (oldInfo && oldInfo->ambientPressure == 100 * 16 && oldInfo->loopPeriodMs == 3);

        w.put (1623, 16);
        CHECK (w.bytes.size() == 55);  // The spec: 384 bits, 55 packed bytes

        for (const auto& data : { official (kInfo, w.bytes), legacy (kInfo, w.bytes) })
        {
            const auto info = decodeInfo (data.data(), data.size());
            CHECK (info.has_value());
            if (info)
            {
                CHECK (info->firmwareVersion == 48);
                CHECK (info->baseline[8] == 18 && info->covered[0] == 500);
                CHECK (info->ambientPressure == 1623);
                CHECK (info->loopPeriodMs == 3);
                CHECK (info->pressureAboveAmbient (1863) == 240);
                CHECK (info->holeCoverage (0, 10) == 0.0f && info->holeCoverage (0, 500) == 1.0f);
                CHECK (std::abs (info->gyroToDps (1000) - 70.0f) < 0.001f);
                CHECK (std::abs (info->accelToG (8197) - 1.0f) < 0.001f);
            }
        }
        CHECK (official (kInfo, w.bytes).size() + 2 == 62);  // 62 SysEx bytes with F0/F7
    }

    // Commands: the spec's examples, and the experimental firmware's format.
    {
        using Bytes = std::vector<uint8_t>;
        CHECK (makeConfig (true, false) == (Bytes { 0x00, 0x03, 0x0F, 0x03, 0x20, 0x05 }));
        CHECK (makeConfig (true, true) == (Bytes { 0x00, 0x03, 0x0F, 0x03, 0x20, 0x07 }));
        CHECK (makeConfig (false, true) == (Bytes { 0x00, 0x03, 0x0F, 0x03, 0x20, 0x00 }));
        CHECK (makeRequestInfo() == (Bytes { 0x00, 0x03, 0x0F, 0x03, 0x21 }));
        CHECK (makeConfig (true, true, Format::legacy) == (Bytes { 0x7D, 0x57, 0x01, 0x20, 0x07 }));
        CHECK (makeConfig (false, false, Format::legacy) == (Bytes { 0x7D, 0x57, 0x01, 0x20, 0x00 }));
        CHECK (makeRequestInfo (Format::legacy) == (Bytes { 0x7D, 0x57, 0x01, 0x21 }));

        const auto header = parseHeader (makeConfig (true, false).data(), 6);
        CHECK (header && header->format == Format::official && header->type == kConfig && header->size == 5);
        const auto legacyHeader = parseHeader (makeRequestInfo (Format::legacy).data(), 4);
        CHECK (legacyHeader && legacyHeader->format == Format::legacy && legacyHeader->type == kRequestInfo && legacyHeader->size == 4);
    }

    // The simulator's info encoder round-trips.
    {
        DeviceInfo info;
        info.firmwareVersion = 48;
        info.covered.fill (600);
        info.gyroRange = 0b1100;
        info.ambientPressure = 1600;
        const auto infoData = official (kInfo, encodeInfoPayload (info));
        const auto decoded = decodeInfo (infoData.data(), infoData.size());
        CHECK (decoded && decoded->covered[8] == 600 && decoded->gyroRange == 0b1100 && decoded->ambientPressure == 1600);
    }

    // Version 3 Bluetooth batches: the spec's sizes, then a round trip.
    {
        const int sizes[][2] = { { 1, 41 }, { 2, 56 }, { 3, 71 }, { 5, 101 }, { 8, 145 }, { 14, 234 }, { 16, 264 } };
        for (const auto& s : sizes)
        {
            std::vector<SensorFrame> frames ((size_t) s[0]);
            CHECK ((int) official (kBatch, encodeBatchPayload (frames)).size() + 2 == s[1]);
        }
        CHECK (batchHeaderBits (3) == 132 && batchSampleBits (3) == 104);

        std::vector<SensorFrame> frames;
        for (int i = 0; i < 14; ++i)
        {
            SensorFrame f;
            f.seq = (uint16_t) (16380 + i);  // Wraps past 14 bits
            f.timestamp = (uint32_t) ((0x1FFFF0 + i * 98) & 0x1FFFFF);  // 3 ms apart, wrapping past 21 bits
            f.pressure = (uint16_t) (1600 + 37 * i);
            for (int h = 0; h < kNumHoles; ++h)
                f.holes[(size_t) h] = (uint16_t) (h * 113 + i);
            f.accel[1] = (int16_t) (-1000 * i);
            f.orientation = { angleCounts (i), angleCounts (-i), angleCounts (2 * i) };
            f.flags = i == 3 ? 2 : 1;
            frames.push_back (f);
        }
        const auto data = official (kBatch, encodeBatchPayload (frames));
        CHECK (data.size() + 2 + 3 <= 244);  // With F0/F7 and BLE MIDI framing: one notification at MTU 247
        const auto decoded = decodeBatch (data.data(), data.size());
        CHECK (decoded.size() == 14);
        for (size_t i = 0; i < decoded.size() && i < frames.size(); ++i)
        {
            CHECK (decoded[i].version == 3);
            CHECK (decoded[i].seq == (frames[i].seq & 0x3FFF));
            CHECK (decoded[i].pressure == frames[i].pressure);
            CHECK (decoded[i].holes == frames[i].holes);  // 10 bits: exact
            const auto error = (int) (((decoded[i].timestamp - frames[i].timestamp) + 2) & 0x1FFFFF) - 2;
            CHECK (std::abs (error) <= 1);
            CHECK (decoded[i].accel == frames.back().accel && decoded[i].orientation == frames.back().orientation);
            CHECK (decoded[i].flags == 3);  // ORed over the batch
        }

        // More than 16 samples is invalid in version 3.
        std::vector<SensorFrame> tooMany (17);
        const auto big = official (kBatch, encodeBatchPayload (tooMany));
        CHECK (decodeBatch (big.data(), big.size()).empty());
        CHECK (decodeBatch (data.data(), data.size() - 1).empty());
    }

    // The experimental firmware's batch (version 2): 8-bit accel xyz and 8-bit holes.
    {
        BitWriter w;
        w.put (100, 14);
        w.put (1000, 21);
        w.put (1000 + 7 * 66, 21);
        w.put (8, 5);
        for (int a : { -32, 64, 1 })
            w.put ((uint8_t) (int8_t) a, 8);
        w.put (1, 7);
        for (int i = 0; i < 8; ++i)
        {
            w.put ((uint32_t) (2000 + i), 14);
            for (int h = 0; h < kNumHoles; ++h)
                w.put ((uint32_t) (h * 28 + i), 8);
        }
        const auto data = legacy (kBatch, w.bytes);
        const auto decoded = decodeBatch (data.data(), data.size());
        CHECK (decoded.size() == 8);
        if (decoded.size() == 8)
        {
            CHECK (decoded[7].seq == 107 && decoded[7].timestamp == 1000 + 7 * 66 && decoded[7].pressure == 2007);
            CHECK (decoded[7].holes[2] == (2 * 28 + 7) * 4 + 2);
            CHECK (decoded[0].accel[0] == -32 * 256 && decoded[0].accel[1] == 64 * 256 && decoded[0].accel[2] == 256);
            CHECK (! decoded[0].hasOrientation());
        }
    }

    // Unwrapping of the 21-bit timestamp.
    {
        Unwrapper u (kTimestampBits);
        CHECK (u.next (0x1FFFF0) == 0x1FFFF0);
        CHECK (u.next (0x000010) == 0x200010);
    }

    std::printf (failures == 0 ? "All protocol tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
