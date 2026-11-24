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
    // pairing or a simulator under that name). Commands go to all of them, except that when a WARBL is on a USB
    // cable they go only to the USB ports: WARBL moves the stream to the port the latest command came on, so a copy
    // over Bluetooth too would pull it onto Bluetooth (3 ms loop, batches). Frames are taken from the first input
    // that streams, or from a USB input as soon as one does. Returns true if at least one input and output were opened.
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
    std::vector<bool> outputIsUsb;            // Per entry in outputs
    std::vector<juce::MidiInput*> usbInputs;  // Set while the inputs are stopped, read on the MIDI thread
    bool isUsbInput (const void*) const;

    void requestInfoLater();                 // From the MIDI thread
    std::atomic<bool> infoWanted { false };  // Ask for info again when the next frame arrives
    bool streamOn = false;                   // Message thread
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);  // Expires with the receiver, for async callbacks.
};
