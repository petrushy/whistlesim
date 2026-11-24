#include "StreamReceiver.h"

#if JUCE_MAC || JUCE_IOS
 #include <CoreMIDI/CoreMIDI.h>
#endif

#include <algorithm>

namespace
{
// Whether a MIDI port is on a USB cable. Both of WARBL's ports are called "WARBL", so the name can't tell. On Apple
// platforms JUCE's identifier ends with the endpoint's CoreMIDI unique ID, and the device's driver says USB or
// Bluetooth. Elsewhere this returns false, so commands keep going to every port.
bool isUsbPort (const juce::MidiDeviceInfo& device)
{
   #if JUCE_MAC || JUCE_IOS
    const auto id = (MIDIUniqueID) device.identifier.fromLastOccurrenceOf (" ", false, false).getLargeIntValue();
    MIDIObjectRef object = 0;
    MIDIObjectType type {};
    if (MIDIObjectFindByUniqueID (id, &object, &type) != noErr || object == 0)
        return false;

    MIDIEntityRef entity = 0;
    MIDIDeviceRef midiDevice = 0;
    if (MIDIEndpointGetEntity ((MIDIEndpointRef) object, &entity) == noErr && entity != 0)
        MIDIEntityGetDevice (entity, &midiDevice);

    CFStringRef driver = nullptr;
    if (MIDIObjectGetStringProperty (midiDevice != 0 ? midiDevice : object, kMIDIPropertyDriverOwner, &driver) != noErr
        || driver == nullptr)
        return false;
    const auto owner = juce::String::fromCFString (driver);
    CFRelease (driver);
    return owner.containsIgnoreCase ("USB");  // com.apple.AppleMIDIUSBDriver
   #else
    juce::ignoreUnused (device);
    return false;
   #endif
}
}

StreamReceiver::StreamReceiver()
{
    // Info arrives on the MIDI (or timer) thread; the app wants it on the message thread.
    onInfoReceived = [this] (const warbl::DeviceInfo& info)
    {
        juce::MessageManager::callAsync ([this, weakAlive = std::weak_ptr<bool> (alive), info]
        {
            if (weakAlive.lock() != nullptr && onInfo)  // The receiver may be gone by the time this runs.
                onInfo (info);
        });
    };
}

StreamReceiver::~StreamReceiver()
{
    stopTimer();
    disconnect();
}

bool StreamReceiver::connect()
{
    disconnect();
    if (leaveDevicesAlone)
        return false;

    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto in = juce::MidiInput::openDevice (d.identifier, this))
            {
                if (isUsbPort (d))
                    usbInputs.push_back (in.get());
                inputs.push_back (std::move (in));
            }

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto out = juce::MidiOutput::openDevice (d.identifier))
            {
                outputIsUsb.push_back (isUsbPort (d));
                outputs.push_back (std::move (out));
            }

    if (! isConnected())
    {
        disconnect();
        return false;
    }

    resetStats();
    for (auto& in : inputs)
        in->start();
    return true;
}

void StreamReceiver::disconnect()
{
    for (auto& in : inputs)
        in->stop();
    inputs.clear();
    usbInputs.clear();
    outputs.clear();
    outputIsUsb.clear();
    forgetStreamingSource();
    streamOn = false;
}

bool StreamReceiver::isUsbInput (const void* in) const
{
    return std::find (usbInputs.begin(), usbInputs.end(), in) != usbInputs.end();
}

juce::String StreamReceiver::getPortNames() const
{
    juce::StringArray names;
    for (const auto& in : inputs)
        names.add (in->getName());
    return names.joinIntoString (", ");
}

juce::String StreamReceiver::getStreamingPortName() const
{
    if (isSimulating())
        return "built-in simulator";

    const auto* source = getStreamingSource();
    for (const auto& in : inputs)
        if (in.get() == source)
            return in->getName();
    return {};
}

void StreamReceiver::setStream (bool on, bool suppressNotes)
{
    if (on && ! streamOn)
        infoWanted = true;  // See requestInfoLater()
    streamOn = on;
    for (const auto& command : streamCommands (on, suppressNotes))
        sendSysEx (command);
}

void StreamReceiver::requestInfo()
{
    for (const auto& command : requestInfoCommands())
        sendSysEx (command);
}

void StreamReceiver::sendSysEx (const std::vector<uint8_t>& data)
{
    const auto message = juce::MidiMessage::createSysExMessage (data.data(), (int) data.size());
    const bool usbOnly = std::find (outputIsUsb.begin(), outputIsUsb.end(), true) != outputIsUsb.end();
    for (size_t i = 0; i < outputs.size(); ++i)
        if (! usbOnly || outputIsUsb[i])
            outputs[i]->sendMessageNow (message);
}

void StreamReceiver::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (! message.isSysEx())
        return;

    const auto* data = message.getSysExData();
    const auto size = (size_t) message.getSysExDataSize();
    const auto type = warbl::messageType (data, size);
    const bool isFrames = type && (*type == warbl::kFrame || *type == warbl::kBatch);

    // The core stays with the first port that streams; move it to a USB port when one starts streaming (WARBL
    // switching from Bluetooth to the cable).
    if (isFrames && ! isSimulating())
    {
        const auto* current = getStreamingSource();
        if (current != nullptr && current != source && isUsbInput (source) && ! isUsbInput (current))
        {
            forgetStreamingSource();
            resetStats();
            infoWanted = true;
        }
    }

    handleSysEx (data, size, nowMs, source);

    if (isFrames && getStreamingSource() == source && infoWanted.exchange (false))
        requestInfoLater();
}

// WARBL's info gives the loop period it expects (2 ms, or 3 ms with a BLE connection), but it's sent only when asked,
// and the first request goes out before the stream starts, while BLE may still be connected; a USB stream then drops
// BLE. So ask again once frames arrive (and when they move to another port), on the message thread.
void StreamReceiver::requestInfoLater()
{
    juce::MessageManager::callAsync ([this, weakAlive = std::weak_ptr<bool> (alive)]
    {
        if (weakAlive.lock() != nullptr && ! isSimulating())
            requestInfo();
    });
}

void StreamReceiver::setSimulating (bool shouldSimulate)
{
    if (shouldSimulate == isSimulating())
        return;

    if (shouldSimulate)
    {
        StreamCore::setSimulating (true);
        startTimer (2);  // Like the firmware's 2 ms loop over USB.
    }
    else
    {
        stopTimer();
        StreamCore::setSimulating (false);
        requestInfo();
    }
}

void StreamReceiver::hiResTimerCallback()
{
    simulatorTick (juce::Time::getMillisecondCounterHiRes());
}
