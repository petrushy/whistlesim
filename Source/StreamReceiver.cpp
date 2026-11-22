#include "StreamReceiver.h"

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
                inputs.push_back (std::move (in));

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto out = juce::MidiOutput::openDevice (d.identifier))
                outputs.push_back (std::move (out));

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
    outputs.clear();
    forgetStreamingSource();
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
    for (auto& out : outputs)
        out->sendMessageNow (message);
}

void StreamReceiver::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (message.isSysEx())
        handleSysEx (message.getSysExData(), (size_t) message.getSysExDataSize(), nowMs, source);
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
