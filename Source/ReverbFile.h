// Loading a measured impulse response from an audio file into the room reverb (the desktop app reads files with
// JUCE; the reverb itself takes samples).

#pragma once

#include "Reverb/RoomReverb.h"

#include <juce_audio_formats/juce_audio_formats.h>

// Returns an error message, or empty on success.
inline juce::String loadReverbFile (RoomReverb& reverb, const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Can't read " + file.getFileName();
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 20.0))
        return "IR is longer than 20 s";

    // Two channels; a mono file is copied to both.
    juce::AudioBuffer<float> read (2, (int) reader->lengthInSamples);
    reader->read (&read, 0, (int) reader->lengthInSamples, 0, true, true);
    reverb::Buffer ir (2, read.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        std::copy (read.getReadPointer (ch), read.getReadPointer (ch) + read.getNumSamples(), ir.getWritePointer (ch));

    return juce::String (reverb.loadImpulseResponse (ir, reader->sampleRate, file.getFileNameWithoutExtension().toStdString(),
                                                     file.getFullPathName().toStdString()));
}
