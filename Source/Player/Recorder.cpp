#include "Recorder.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace
{
void put16 (std::ofstream& out, uint16_t v)
{
    const char b[2] = { (char) (v & 0xff), (char) (v >> 8) };
    out.write (b, 2);
}

void put32 (std::ofstream& out, uint32_t v)
{
    const char b[4] = { (char) (v & 0xff), (char) ((v >> 8) & 0xff), (char) ((v >> 16) & 0xff), (char) (v >> 24) };
    out.write (b, 4);
}

std::string localTime (const char* format)
{
    const auto now = std::time (nullptr);
    std::tm local {};
#if defined(_WIN32)
    localtime_s (&local, &now);
#else
    localtime_r (&now, &local);
#endif
    char text[64];
    std::strftime (text, sizeof (text), format, &local);
    return text;
}

// A path from UTF-8 text (folder names may have any characters).
std::filesystem::path utf8Path (const std::string& text)
{
    return std::filesystem::path (std::u8string (text.begin(), text.end()));
}

std::string fixed3 (double v)
{
    char text[64];
    std::snprintf (text, sizeof (text), "%.3f", v);
    return text;
}
} // namespace

Recorder::~Recorder()
{
    stop();
}

std::string Recorder::isoTime() { return localTime ("%Y-%m-%dT%H:%M:%S"); }

bool Recorder::start (const std::string& folder, double sampleRate, const std::vector<std::string>& header)
{
    stop();
    rate = sampleRate;
    folderPath = folder;
    std::error_code error;
    std::filesystem::create_directories (utf8Path (folder), error);
    baseName = localTime ("%Y-%m-%d %H.%M.%S");
    const auto base = utf8Path (folder) / utf8Path (baseName);

    wav.open (std::filesystem::path (base).concat (".wav"), std::ios::binary | std::ios::trunc);
    csv.open (std::filesystem::path (base).concat (".csv"), std::ios::trunc);
    if (! wav || ! csv)
    {
        wav.close();
        csv.close();
        return false;
    }
    writeWavHeader (0);
    wavDataBytes = 0;
    for (const auto& line : header)
        csv << "# " << line << "\n";
    csv << "device_ms,arrival_ms,seq,bell,r4,r3,r2,r1,l3,l2,l1,thumb,pressure14,flags\n";

    framesWritten = 0;
    audioStartMs = 0.0;
    ringRead = ringWrite.load();
    writerRunning = true;
    writer = std::thread ([this] { writerLoop(); });
    recording = true;
    return true;
}

void Recorder::stop()
{
    if (! recording.exchange (false))
        return;
    writerRunning = false;
    if (writer.joinable())
        writer.join();  // Writes what's left
    // The sizes, now that they're known.
    writeWavHeader ((uint32_t) std::min<uint64_t> (wavDataBytes, 0xffffffffu - 36));
    wav.close();
    const std::lock_guard<std::mutex> lock (csvLock);
    csv << "# audio started at arrival_ms " << fixed3 (audioStartMs.load()) << "\n";
    csv.close();
}

void Recorder::writeWavHeader (uint32_t dataBytes)
{
    // PCM, 2 channels, 24 bits.
    wav.seekp (0);
    wav.write ("RIFF", 4);
    put32 (wav, 36 + dataBytes);
    wav.write ("WAVEfmt ", 8);
    put32 (wav, 16);
    put16 (wav, 1);
    put16 (wav, 2);
    put32 (wav, (uint32_t) std::lround (rate));
    put32 (wav, (uint32_t) std::lround (rate) * 2 * 3);
    put16 (wav, 2 * 3);
    put16 (wav, 24);
    wav.write ("data", 4);
    put32 (wav, dataBytes);
    wav.seekp (0, std::ios::end);
}

void Recorder::writerLoop()
{
    std::vector<char> bytes;
    for (;;)
    {
        const bool running = writerRunning.load();
        const size_t write = ringWrite.load (std::memory_order_acquire), read = ringRead.load (std::memory_order_relaxed);
        const size_t count = write - read;
        if (count > 0)
        {
            bytes.resize (count * 2 * 3);
            for (size_t i = 0; i < count; ++i)
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const float s = std::max (-1.0f, std::min (1.0f, ring[2 * ((read + i) % kRingFrames) + ch]));
                    const auto v = (int32_t) std::lround (s * 8388607.0f);
                    char* b = bytes.data() + (i * 2 + ch) * 3;
                    b[0] = (char) (v & 0xff);
                    b[1] = (char) ((v >> 8) & 0xff);
                    b[2] = (char) ((v >> 16) & 0xff);
                }
            ringRead.store (write, std::memory_order_release);
            wav.write (bytes.data(), (std::streamsize) bytes.size());
            wavDataBytes += bytes.size();
        }
        if (! running && count == 0)
            return;
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
}

void Recorder::addFrame (const ReceivedFrame& r)
{
    if (! recording.load())
        return;
    const std::lock_guard<std::mutex> lock (csvLock);
    if (! csv.is_open())
        return;
    csv << fixed3 (r.deviceMs) << "," << fixed3 (r.arrivalMs) << "," << (int) r.frame.seq;
    for (auto h : r.frame.holes)
        csv << "," << (int) h;
    csv << "," << (int) r.frame.pressure << "," << (int) r.frame.flags << "\n";
}

void Recorder::addAudio (const float* const* channels, int numChannels, int numSamples, double nowMs)
{
    if (! recording.load() || numChannels < 2)
        return;
    const size_t write = ringWrite.load (std::memory_order_relaxed), read = ringRead.load (std::memory_order_acquire);
    if (write - read + (size_t) numSamples > kRingFrames)
        return;  // The writer is far behind: drop this block rather than wait
    if (framesWritten.load() == 0)
        audioStartMs = nowMs;
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t at = 2 * ((write + (size_t) i) % kRingFrames);
        ring[at] = channels[0][i];
        ring[at + 1] = channels[1][i];
    }
    ringWrite.store (write + (size_t) numSamples, std::memory_order_release);
    framesWritten += numSamples;
}
