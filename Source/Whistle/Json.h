// A small JSON value, reader and writer for the instrument files, with the standard library alone.
// Objects keep their members in order. The writer formats as juce::JSON did (two-space indents, numbers through
// the same shortest-form rules), so files written before and after the engine left JUCE are byte-identical.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace whistle::json
{

class Value
{
public:
    enum class Type { null, boolean, integer, number, string, array, object };

    Value() = default;
    Value (bool b) : type (Type::boolean), boolean (b) {}
    Value (int i) : type (Type::integer), integer (i) {}
    Value (std::int64_t i) : type (Type::integer), integer (i) {}
    Value (double d) : type (Type::number), number (d) {}
    Value (const char* s) : type (Type::string), text (s) {}
    Value (std::string s) : type (Type::string), text (std::move (s)) {}

    static Value array() { Value v; v.type = Type::array; return v; }
    static Value object() { Value v; v.type = Type::object; return v; }

    Type getType() const noexcept { return type; }
    bool isObject() const noexcept { return type == Type::object; }
    bool isArray() const noexcept { return type == Type::array; }
    bool isString() const noexcept { return type == Type::string; }

    // Conversions in the manner of juce::var: numbers, booleans and numeric text convert to each other; anything
    // else gives 0, false or "".
    double toDouble() const;
    int toInt() const;
    bool toBool() const;
    std::string toString() const;

    // Arrays.
    const std::vector<Value>& items() const noexcept { return elements; }
    void add (Value v) { elements.push_back (std::move (v)); }

    // Objects. operator[] gives a null value for a missing key (or a value that isn't an object).
    bool has (std::string_view key) const;
    const Value& operator[] (std::string_view key) const;
    void set (std::string_view key, Value v);  // Replaces an existing member in place, else appends
    const std::vector<std::pair<std::string, Value>>& members() const noexcept { return properties; }

private:
    Type type = Type::null;
    bool boolean = false;
    std::int64_t integer = 0;
    double number = 0.0;
    std::string text;
    std::vector<Value> elements;
    std::vector<std::pair<std::string, Value>> properties;
};

// Writes v. Multi-line output uses '\n' line endings. maxDecimalPlaces <= 0: as many as the number needs.
std::string write (const Value& v, bool oneLine = false, int maxDecimalPlaces = 0);

// Parses text; on failure returns nothing and puts the reason (with line and column) in error.
std::optional<Value> parse (std::string_view text, std::string& error);

// A double as juce::JSON wrote it ("12.0", "0.0095", "1.5e-06").
std::string formatDouble (double value, int maxDecimalPlaces = 0);

} // namespace whistle::json
