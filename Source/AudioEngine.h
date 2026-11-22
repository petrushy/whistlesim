// Audio output on the desktop (and the JUCE iOS build): runs the Player (Player/Player.h: sensor frames to the
// model's controls, the model, the room, the limiter) from a JUCE audio device, and records the output.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Player/Player.h"
#include "Player/Recorder.h"
#include "StreamReceiver.h"

class AudioEngine : public Player, private juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (StreamReceiver& receiver);
    ~AudioEngine() override;

    juce::AudioDeviceManager deviceManager;
    Recorder recorder;

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
};
