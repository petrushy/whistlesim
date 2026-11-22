// Records a playing session for analysis: the app's sound (stereo 24-bit WAV, as heard) and every sensor frame with
// the settings needed to replay it through the model offline (CSV; WhistleCheck replay reads it). Standard library
// only; the app says where the files go (desktop: ~/Downloads/WhistleSim recordings; iPhone: the app's Recordings
// folder, which the Files app shows).

#pragma once

#include "StreamCore.h"

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Recorder
{
public:
    ~Recorder();

    // UI thread. Files named by the date and time, in `folder` (made if needed). `header` lines go at the top of the
    // CSV (as # comments). Returns false if a file couldn't be opened.
    bool start (const std::string& folder, double sampleRate, const std::vector<std::string>& header);
    void stop();
    bool isRecording() const { return recording.load(); }
    const std::string& getBaseName() const { return baseName; }
    const std::string& getFolder() const { return folderPath; }

    // UI thread: one sensor frame.
    void addFrame (const ReceivedFrame&);

    // Audio thread: the sound as heard (lock-free; dropped if the writer falls far behind). nowMs: the app's clock
    // (the CSV notes when the audio started, on the frames' arrival clock).
    void addAudio (const float* const* channels, int numChannels, int numSamples, double nowMs);

    double getSeconds() const { return (double) framesWritten.load() / std::max (1.0, rate); }

    // "2026-11-22T21:40:05" (local time), for the header.
    static std::string isoTime();

private:
    void writerLoop();
    void writeWavHeader (uint32_t dataBytes);

    std::atomic<bool> recording { false };
    double rate = 48000.0;
    std::string baseName, folderPath;

    std::ofstream wav, csv;
    std::mutex csvLock;  // UI thread writes, stop() closes
    uint64_t wavDataBytes = 0;

    // Audio thread to the writer: interleaved stereo, single producer, single consumer.
    static constexpr size_t kRingFrames = 1 << 18;  // ~5 s at 48 kHz
    std::vector<float> ring = std::vector<float> (2 * kRingFrames);
    std::atomic<size_t> ringWrite { 0 }, ringRead { 0 };  // Ever-increasing frame counts
    std::thread writer;
    std::atomic<bool> writerRunning { false };

    std::atomic<double> audioStartMs { 0.0 };
    std::atomic<int64_t> framesWritten { 0 };
};
