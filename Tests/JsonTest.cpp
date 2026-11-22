// The engine's JSON (Source/Whistle/Json) against juce::JSON, which wrote the instrument files before the engine
// left JUCE: the same text for the same values (numbers above all), and the same values read back.

#include "Whistle/Json.h"

#include <juce_core/juce_core.h>

#include <cstdio>
#include <functional>
#include <random>

using namespace whistle;

static int failures = 0;

static void check (bool condition, const char* what, const std::string& detail)
{
    if (condition)
        return;
    if (++failures <= 20)
        std::printf ("FAILED: %s: %s\n", what, detail.c_str());
}

int main()
{
    // Numbers: every size and sign, whole numbers, values near the format's switch points, and random bit patterns.
    std::mt19937_64 random (12345);
    std::vector<double> values { 0.0, -0.0, 1.0, -1.0, 0.5, 12.0, 267.1, 0.0095, 1.0e-5, 1.0e-6, 1.5e-6, 9.99999e5, 1.0e6,
                                 123456789.0, 0.1, 0.01, 0.001, 1.0e-4, 2.0e-5, 1.0e3, 1.0e4, 1.0e5, 440.0, -15.0, 1.23456789012345 };
    std::uniform_real_distribution<double> exponent (-9.0, 9.0), mantissa (-1.0, 1.0);
    for (int i = 0; i < 1000000; ++i)
        values.push_back (mantissa (random) * std::pow (10.0, exponent (random)));
    std::uniform_int_distribution<int> places (0, 8);
    for (int i = 0; i < 1000; ++i)
        values.push_back (std::round (mantissa (random) * 1.0e5) / std::pow (10.0, places (random)));  // Few decimals

    for (const double v : values)
    {
        for (const int maxPlaces : { 0, 6 })
        {
            const auto ours = json::formatDouble (v, maxPlaces);
            const auto juces = juce::JSON::toString (juce::var (v), true, maxPlaces).toStdString();
            check (ours == juces, "number", std::to_string (v) + " (" + std::to_string (maxPlaces) + "): " + ours + " vs " + juces);
        }
    }

    // A document with every kind of value, written both ways, on one line and indented.
    auto object = json::Value::object();
    auto jobject = juce::var (new juce::DynamicObject());
    auto both = [&] (const char* key, json::Value a, juce::var b)
    {
        object.set (key, std::move (a));
        jobject.getDynamicObject()->setProperty (key, b);
    };
    const char* name = "Härjedalspipa \"quoted\" \\ tab\t newline\n";
    both ("name", name, juce::String::fromUTF8 (name));
    both ("int", 7, 7);
    both ("negative", -3, -3);
    both ("double", 267.123456789, 267.123456789);
    both ("true", true, true);
    both ("false", false, false);
    both ("emptyObject", json::Value::object(), juce::var (new juce::DynamicObject()));
    both ("emptyArray", json::Value::array(), juce::Array<juce::var>());
    auto array = json::Value::array();
    juce::Array<juce::var> jarray;
    for (double d : { 1.0, 0.25, 1.0e-7 })
    {
        array.add (d);
        jarray.add (d);
    }
    auto inner = json::Value::object();
    auto jinner = juce::var (new juce::DynamicObject());
    inner.set ("x", 1.5);
    jinner.getDynamicObject()->setProperty ("x", 1.5);
    array.add (inner);
    jarray.add (jinner);
    both ("array", array, jarray);

    for (const bool oneLine : { true, false })
    {
        for (const int maxPlaces : { 0, 6 })
        {
            const auto ours = json::write (object, oneLine, maxPlaces);
            const auto juces = juce::JSON::toString (jobject, oneLine, maxPlaces).replace ("\r\n", "\n").toStdString();
            check (ours == juces, "document", "\n" + ours + "\nvs\n" + juces);
        }
    }

    // Reading back: what we wrote parses to the same values.
    std::string error;
    const auto parsed = json::parse (json::write (object), error);
    check (parsed.has_value() && error.empty(), "parse", error);
    if (parsed)
    {
        check ((*parsed)["name"].toString() == object["name"].toString(), "string read back", (*parsed)["name"].toString());
        check ((*parsed)["int"].toInt() == 7 && (*parsed)["negative"].toInt() == -3, "integers read back", "");
        check (std::equal_to<double>() ((*parsed)["double"].toDouble(), 267.123456789), "double read back", (*parsed)["double"].toString());
        check ((*parsed)["true"].toBool() && ! (*parsed)["false"].toBool(), "booleans read back", "");
        check ((*parsed)["array"].items().size() == 4 && std::equal_to<double>() ((*parsed)["array"].items()[3]["x"].toDouble(), 1.5), "array read back", "");
        check ((*parsed)["emptyObject"].isObject() && (*parsed)["emptyArray"].isArray(), "empty containers read back", "");
    }
    check (! json::parse ("{ \"a\": 1, }", error) && ! error.empty(), "trailing comma rejected", error);
    check (! json::parse ("{ not json", error), "garbage rejected", error);
    check (json::parse ("{ \"u\": \"\\u00e4\\ud83c\\udfb5\" }", error).value_or (json::Value())["u"].toString() == "ä🎵", "\\u escapes", error);

    if (failures == 0)
        std::printf ("All JSON tests passed (%zu numbers)\n", values.size());
    else
        std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
