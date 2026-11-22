#include "Json.h"
#include "Common.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace whistle::json
{

namespace
{
const Value& nullValue()
{
    static const Value v;
    return v;
}

std::string trimmed (const std::string& s)
{
    const auto first = s.find_first_not_of (" \t\r\n");
    if (first == std::string::npos)
        return {};
    return s.substr (first, s.find_last_not_of (" \t\r\n") - first + 1);
}

std::string printfDouble (const char* format, int places, double value)
{
    char buffer[400];
    std::snprintf (buffer, sizeof (buffer), format, places, value);
    return buffer;
}

// Trailing zeros off the fraction (one kept after the point) and off the exponent: a port of JUCE's
// reduceLengthOfFloatString, step for step, so the results match it in every case.
std::string reduceLength (const std::string& s)
{
    const size_t end = s.size();
    size_t trimStart = end, trimEnd = end, exponentStart = end, exponentEnd = end;
    char current = '\0';

    for (size_t c = end - 1; c > 0; --c)
    {
        current = s[c];
        if (current == '0' && c + 1 == trimStart)
        {
            --trimStart;
        }
        else if (current == '.')
        {
            if (trimStart == c + 1 && trimStart != end && s[trimStart] == '0')
                ++trimStart;
            break;
        }
        else if (current == 'e' || current == 'E')
        {
            size_t next = c + 1;
            if (next != end)
            {
                if (s[next] == '-')
                    ++next;
                exponentStart = next;
                if (next != end && s[next] == '+')
                    ++next;
                exponentEnd = next;
            }
            while (next != end)
            {
                const bool zero = s[next] == '0';
                ++next;
                if (! zero)
                    break;
                exponentEnd = next;
            }
            if (exponentEnd == end)
                exponentStart = c;
            trimStart = c;
            trimEnd = trimStart;
        }
    }

    if ((trimStart != trimEnd && current == '.') || exponentStart != exponentEnd)
    {
        if (trimStart == trimEnd)
            return s.substr (0, exponentStart) + s.substr (exponentEnd);
        if (exponentStart == exponentEnd)
            return s.substr (0, trimStart) + s.substr (trimEnd);
        if (trimEnd == exponentStart)
            return s.substr (0, trimStart) + s.substr (exponentEnd);
        return s.substr (0, trimStart) + s.substr (trimEnd, exponentStart - trimEnd) + s.substr (exponentEnd);
    }
    return s;
}

void writeString (std::string& out, const std::string& s)
{
    out += '"';
    for (const char ch : s)
    {
        const auto c = (unsigned char) ch;
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            default:
                if (c < 0x20 || c == 0x7f)
                {
                    char escaped[8];
                    std::snprintf (escaped, sizeof (escaped), "\\u%04x", c);
                    out += escaped;
                }
                else
                {
                    out += (char) c;  // UTF-8 passes through
                }
        }
    }
    out += '"';
}

void writeValue (std::string& out, const Value& v, bool oneLine, int maxDecimalPlaces, int indent)
{
    constexpr int kIndentSize = 2;
    switch (v.getType())
    {
        case Value::Type::null: out += "null"; return;
        case Value::Type::boolean: out += v.toBool() ? "true" : "false"; return;
        case Value::Type::integer: out += v.toString(); return;
        case Value::Type::number:
        {
            const double d = v.toDouble();
            out += std::isfinite (d) ? formatDouble (d, maxDecimalPlaces) : "null";
            return;
        }
        case Value::Type::string: writeString (out, v.toString()); return;
        case Value::Type::array:
        case Value::Type::object:
        {
            const bool isObject = v.isObject();
            const size_t count = isObject ? v.members().size() : v.items().size();
            out += isObject ? '{' : '[';
            // An empty array is "[]"; an empty object still gets its line break and indent, as JUCE wrote it.
            if (isObject || count > 0)
                if (! oneLine)
                    out += '\n';
            for (size_t i = 0; i < count; ++i)
            {
                if (! oneLine)
                    out.append ((size_t) (indent + kIndentSize), ' ');
                if (isObject)
                {
                    writeString (out, v.members()[i].first);
                    out += ": ";
                }
                writeValue (out, isObject ? v.members()[i].second : v.items()[i], oneLine, maxDecimalPlaces, indent + kIndentSize);
                if (i + 1 < count)
                    out += oneLine ? ", " : ",\n";
                else if (! oneLine)
                    out += '\n';
            }
            if (! oneLine && (isObject || count > 0))
                out.append ((size_t) indent, ' ');
            out += isObject ? '}' : ']';
            return;
        }
    }
}

class Parser
{
public:
    explicit Parser (std::string_view t) : text (t) {}

    std::optional<Value> parseDocument (std::string& error)
    {
        Value v;
        if (! parseValue (v) || (skipSpace(), pos != text.size() && fail ("Unexpected text after the JSON value")))
        {
            error = message;
            return std::nullopt;
        }
        return v;
    }

private:
    std::string_view text;
    size_t pos = 0;
    std::string message;
    int depth = 0;

    bool fail (const char* what)
    {
        if (message.empty())
        {
            int line = 1, column = 1;
            for (size_t i = 0; i < pos && i < text.size(); ++i)
            {
                if (text[i] == '\n') { ++line; column = 1; }
                else ++column;
            }
            message = "Line " + std::to_string (line) + ", column " + std::to_string (column) + ": " + what;
        }
        return false;
    }

    void skipSpace()
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n'))
            ++pos;
    }

    bool literal (std::string_view word)
    {
        if (text.substr (pos, word.size()) != word)
            return fail ("Unexpected text");
        pos += word.size();
        return true;
    }

    bool parseValue (Value& out)
    {
        skipSpace();
        if (pos >= text.size())
            return fail ("Unexpected end of input");
        switch (text[pos])
        {
            case '{': return parseObject (out);
            case '[': return parseArray (out);
            case '"':
            {
                std::string s;
                if (! parseString (s))
                    return false;
                out = Value (std::move (s));
                return true;
            }
            case 't': out = Value (true); return literal ("true");
            case 'f': out = Value (false); return literal ("false");
            case 'n': out = Value(); return literal ("null");
            default: return parseNumber (out);
        }
    }

    bool parseNumber (Value& out)
    {
        const size_t start = pos;
        bool isDouble = false;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
            ++pos;
        const size_t digits = pos;
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c >= '0' && c <= '9')
                ++pos;
            else if (c == '.' || c == 'e' || c == 'E' || ((c == '-' || c == '+') && (text[pos - 1] == 'e' || text[pos - 1] == 'E')))
                { isDouble = true; ++pos; }
            else
                break;
        }
        if (pos == digits)
            return fail ("Expected a value");
        const std::string number (text.substr (start, pos - start));
        char* end = nullptr;
        if (! isDouble)
        {
            const long long i = std::strtoll (number.c_str(), &end, 10);
            if (end == number.c_str() + number.size())
            {
                out = Value ((std::int64_t) i);
                return true;
            }
        }
        const double d = std::strtod (number.c_str(), &end);
        if (end != number.c_str() + number.size())
            return fail ("Badly formed number");
        out = Value (d);
        return true;
    }

    static void appendUtf8 (std::string& s, unsigned int c)
    {
        if (c < 0x80) s += (char) c;
        else if (c < 0x800) { s += (char) (0xc0 | (c >> 6)); s += (char) (0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { s += (char) (0xe0 | (c >> 12)); s += (char) (0x80 | ((c >> 6) & 0x3f)); s += (char) (0x80 | (c & 0x3f)); }
        else { s += (char) (0xf0 | (c >> 18)); s += (char) (0x80 | ((c >> 12) & 0x3f)); s += (char) (0x80 | ((c >> 6) & 0x3f)); s += (char) (0x80 | (c & 0x3f)); }
    }

    bool parseHex4 (unsigned int& value)
    {
        if (pos + 4 > text.size())
            return fail ("Badly formed \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text[pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= (unsigned int) (c - '0');
            else if (c >= 'a' && c <= 'f') value |= (unsigned int) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= (unsigned int) (c - 'A' + 10);
            else return fail ("Badly formed \\u escape");
        }
        return true;
    }

    bool parseString (std::string& out)
    {
        ++pos;  // The opening quote
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos >= text.size())
                break;
            const char e = text[pos++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned int c1 = 0;
                    if (! parseHex4 (c1))
                        return false;
                    if (c1 >= 0xd800 && c1 < 0xdc00 && text.substr (pos, 2) == "\\u")  // A UTF-16 surrogate pair
                    {
                        pos += 2;
                        unsigned int c2 = 0;
                        if (! parseHex4 (c2))
                            return false;
                        c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                    }
                    appendUtf8 (out, c1);
                    break;
                }
                default: out += e;
            }
        }
        return fail ("Unterminated string");
    }

    bool parseArray (Value& out)
    {
        if (++depth > 100)
            return fail ("Nested too deeply");
        ++pos;
        out = Value::array();
        skipSpace();
        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            --depth;
            return true;
        }
        for (;;)
        {
            Value item;
            if (! parseValue (item))
                return false;
            out.add (std::move (item));
            skipSpace();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == ']') { ++pos; --depth; return true; }
            return fail ("Expected ',' or ']'");
        }
    }

    bool parseObject (Value& out)
    {
        if (++depth > 100)
            return fail ("Nested too deeply");
        ++pos;
        out = Value::object();
        skipSpace();
        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            --depth;
            return true;
        }
        for (;;)
        {
            skipSpace();
            if (pos >= text.size() || text[pos] != '"')
                return fail ("Expected a property name");
            std::string key;
            if (! parseString (key))
                return false;
            skipSpace();
            if (pos >= text.size() || text[pos] != ':')
                return fail ("Expected ':'");
            ++pos;
            Value item;
            if (! parseValue (item))
                return false;
            out.set (key, std::move (item));
            skipSpace();
            if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
            if (pos < text.size() && text[pos] == '}') { ++pos; --depth; return true; }
            return fail ("Expected ',' or '}'");
        }
    }
};
} // namespace

double Value::toDouble() const
{
    switch (type)
    {
        case Type::boolean: return boolean ? 1.0 : 0.0;
        case Type::integer: return (double) integer;
        case Type::number: return number;
        case Type::string: return std::strtod (text.c_str(), nullptr);
        case Type::null:
        case Type::array:
        case Type::object: break;
    }
    return 0.0;
}

int Value::toInt() const
{
    switch (type)
    {
        case Type::boolean: return boolean ? 1 : 0;
        case Type::integer: return (int) integer;
        case Type::number: return (int) number;
        case Type::string: return (int) std::strtol (text.c_str(), nullptr, 10);
        case Type::null:
        case Type::array:
        case Type::object: break;
    }
    return 0;
}

bool Value::toBool() const
{
    switch (type)
    {
        case Type::boolean: return boolean;
        case Type::integer: return integer != 0;
        case Type::number: return ! exactlyEqual (number, 0.0);
        case Type::string:
        {
            const auto t = trimmed (text);
            return std::strtol (t.c_str(), nullptr, 10) != 0 || t == "true" || t == "True" || t == "yes" || t == "Yes";
        }
        case Type::null:
        case Type::array:
        case Type::object: break;
    }
    return false;
}

std::string Value::toString() const
{
    switch (type)
    {
        case Type::boolean: return boolean ? "true" : "false";
        case Type::integer: return std::to_string (integer);
        case Type::number: return formatDouble (number);
        case Type::string: return text;
        case Type::null:
        case Type::array:
        case Type::object: break;
    }
    return {};
}

bool Value::has (std::string_view key) const
{
    for (const auto& [name, value] : properties)
        if (name == key)
            return true;
    return false;
}

const Value& Value::operator[] (std::string_view key) const
{
    for (const auto& [name, value] : properties)
        if (name == key)
            return value;
    return nullValue();
}

void Value::set (std::string_view key, Value v)
{
    for (auto& [name, value] : properties)
    {
        if (name == key)
        {
            value = std::move (v);
            return;
        }
    }
    properties.emplace_back (std::string (key), std::move (v));
}

std::string formatDouble (double value, int maxDecimalPlaces)
{
    // juce::serialiseDouble: scientific form far from 1, one decimal for whole numbers, else fixed with enough places
    // for the number's size (or maxDecimalPlaces), then trailing zeros removed.
    const double magnitude = std::abs (value);
    if (magnitude >= 1.0e6 || magnitude <= 1.0e-5)
        return reduceLength (printfDouble ("%.*e", maxDecimalPlaces > 0 ? maxDecimalPlaces : 15, value));

    if (exactlyEqual ((double) (int) value, value))
        return printfDouble ("%.*f", 1, value);

    int places = maxDecimalPlaces;
    if (places <= 0)
    {
        if (magnitude < 1.0)
            places = magnitude >= 1.0e-1 ? 16 : magnitude >= 1.0e-2 ? 17 : magnitude >= 1.0e-3 ? 18 : magnitude >= 1.0e-4 ? 19 : 20;
        else if (magnitude < 1.0e3)
            places = magnitude < 1.0e1 ? 15 : magnitude < 1.0e2 ? 14 : 13;
        else
            places = magnitude < 1.0e4 ? 12 : magnitude < 1.0e5 ? 11 : 10;
    }
    return reduceLength (printfDouble ("%.*f", places, value));
}

std::string write (const Value& v, bool oneLine, int maxDecimalPlaces)
{
    std::string out;
    writeValue (out, v, oneLine, maxDecimalPlaces, 0);
    return out;
}

std::optional<Value> parse (std::string_view text, std::string& error)
{
    return Parser (text).parseDocument (error);
}

} // namespace whistle::json
