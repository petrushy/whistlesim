// Saving and loading a whistle design as JSON: geometry (lengths in millimetres, so the file is easy to read and
// edit by hand), voicing and output level. Fields that are missing keep the value of the first preset, so
// older or hand-written files still load.

#pragma once

#include "WhistleGeometry.h"

#include <optional>

namespace whistle
{

struct Instrument
{
    Geometry geometry;
    Voicing voicing;
};

juce::var toVar (const Instrument&);
// `base` gives the values a file leaves out (default: the first preset, with the default voicing).
std::optional<Instrument> fromVar (const juce::var&, juce::String& error, const Instrument* base = nullptr);

juce::String toJson (const Instrument&);
std::optional<Instrument> fromJson (const juce::String&, juce::String& error, const Instrument* base = nullptr);

} // namespace whistle
