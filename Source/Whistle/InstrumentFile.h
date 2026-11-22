// Saving and loading a whistle design as JSON: geometry (lengths in millimetres, so the file is easy to read and
// edit by hand), voicing and output level. Fields that are missing keep the value of the first preset, so
// older or hand-written files still load.

#pragma once

#include "Json.h"
#include "WhistleGeometry.h"

#include <optional>
#include <string>
#include <string_view>

namespace whistle
{

struct Instrument
{
    Geometry geometry;
    Voicing voicing;
};

json::Value toValue (const Instrument&);
// `base` gives the values a file leaves out (default: the first preset, with the default voicing).
std::optional<Instrument> fromValue (const json::Value&, std::string& error, const Instrument* base = nullptr);

// The file's text: indented, with 6 decimals (files and presets); oneLine: on one line with every digit (the
// instrument in a recording's header).
std::string toJson (const Instrument&, bool oneLine = false);
std::optional<Instrument> fromJson (std::string_view, std::string& error, const Instrument* base = nullptr);

} // namespace whistle
