#include "AudioEngine.h"

AudioEngine::AudioEngine (StreamReceiver& receiver) : Player (receiver.audioQueue)
{
    deviceManager.initialiseWithDefaultDevices (0, 2);

    // Ask for a small buffer; the device may round it. An iPhone (tried on a USB-C one) plays cleanly at 128
    // samples (2.9 ms at 44.1 kHz) but not at 64.
    auto setup = deviceManager.getAudioDeviceSetup();
   #if JUCE_IOS
    setup.bufferSize = 128;
   #else
    setup.bufferSize = 64;
   #endif
    deviceManager.setAudioDeviceSetup (setup, true);

    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 2; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    if (numOutputs < 2 || outputs[0] == nullptr || outputs[1] == nullptr)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        return;
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    process (outputs[0], outputs[1], numSamples, nowMs);
    recorder.addAudio (outputs, numOutputs, numSamples, nowMs);

    if (silent)
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);
}
