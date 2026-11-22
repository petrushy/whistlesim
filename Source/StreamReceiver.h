// Connects to WARBL over USB (or Bluetooth) MIDI with JUCE and hands each SysEx message to the stream core
// (Player/StreamCore: decoding, queues to the audio thread and the UI, statistics). Also runs the simulator's timer.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Player/StreamCore.h"

#include <atomic>

class StreamReceiver : public StreamCore, private juce::MidiInputCallback, private juce::HighResolutionTimer
{
public:
    StreamReceiver();
    ~StreamReceiver() override;

    // Opens every MIDI input and output whose name contains "WARBL" (macOS may also list an old Bluetooth
    // pairing or a simulator under that name). Commands go to all of them, and frames are taken from the first
    // input that streams. Returns true if at least one input and output were opened.
    bool connect();

    // Test runs (--snapshot) never touch WARBL: connect() then does nothing, so they can't interfere with the
    // user's own copy of the app. Set before the receiver is used.
    static inline std::atomic<bool> leaveDevicesAlone { false };
    void disconnect();
    bool isConnected() const { return ! inputs.empty() && ! outputs.empty(); }
    juce::String getPortNames() const;
    juce::String getStreamingPortName() const;  // Empty until frames arrive.

    void setStream (bool on, bool suppressNotes);
    void requestInfo();

    // Also runs the simulator's 2 ms timer (like the firmware's loop over USB), and asks WARBL for its info after.
    void setSimulating (bool shouldSimulate) override;

    // Called on the message thread when an info message arrives.
    std::function<void (const warbl::DeviceInfo&)> onInfo;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void hiResTimerCallback() override;
    void sendSysEx (const std::vector<uint8_t>&);

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);  // Expires with the receiver, for async callbacks.
};
