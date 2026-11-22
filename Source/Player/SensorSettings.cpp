#include "SensorSettings.h"

namespace
{
using whistle::json::Value;

// Each setting, under the name the desktop app has always saved it as.
struct FloatSetting { const char* key; std::atomic<float> Player::* field; };
struct BoolSetting { const char* key; std::atomic<bool> Player::* field; };

// The order the desktop wrote them in (floats and switches mixed): kept, so the saved text doesn't change.
struct Entry { const char* key; std::atomic<float> Player::* number; std::atomic<bool> Player::* flag; };
const Entry kEntries[] = {
    { "breathInputMin_Pa", &Player::breathInputMin, nullptr },
    { "breathInputMax_Pa", &Player::breathInputMax, nullptr },
    { "breathCurve", &Player::breathCurve, nullptr },
    { "breathOutputMax_Pa", &Player::breathOutputMax, nullptr },
    { "holeOpenBelow", &Player::holeOpenBelow, nullptr },
    { "holeClosedAt", &Player::holeClosedAt, nullptr },
    { "holeCurve", &Player::holeCurve, nullptr },
    { "holeSCurve", nullptr, &Player::holeSCurve },
    { "holeCentre", &Player::holeCentre, nullptr },
    { "holeFingerHeight", nullptr, &Player::holeFingerHeight },
    { "holeFingerRange_mm", &Player::holeFingerRange, nullptr },
    { "thumbOwn", nullptr, &Player::thumbOwn },
    { "holdHoleJumps", nullptr, &Player::holdHoleJumps },
    { "thumbOpenBelow", &Player::thumbOpenBelow, nullptr },
    { "thumbClosedAt", &Player::thumbClosedAt, nullptr },
    { "thumbCurve", &Player::thumbCurve, nullptr },
    { "tiltEnabled", nullptr, &Player::tiltEnabled },
    { "tiltInvert", nullptr, &Player::tiltInvert },
    { "tiltZero_deg", &Player::tiltZero, nullptr },
    { "tiltDeadZone_deg", &Player::tiltDeadZone, nullptr },
    { "tiltRange_deg", &Player::tiltRange, nullptr },
    { "tiltMaxShift_mm", &Player::tiltMaxShift, nullptr },
    { "tiltMaxSpreading", &Player::tiltMaxSpreading, nullptr },
    { "tiltMaxNoise_dB", &Player::tiltMaxNoiseDb, nullptr },
};
} // namespace

Value sensorsToJson (const Player& p)
{
    auto o = Value::object();
    for (const auto& e : kEntries)
    {
        if (e.number != nullptr)
            o.set (e.key, (double) (p.*(e.number)).load());
        else
            o.set (e.key, (p.*(e.flag)).load());
    }
    return o;
}

void sensorsFromJson (const Value& o, Player& p)
{
    for (const auto& e : kEntries)
    {
        if (! o.has (e.key))
            continue;
        if (e.number != nullptr)
            (p.*(e.number)) = (float) o[e.key].toDouble();
        else
            (p.*(e.flag)) = o[e.key].toBool();
    }
}

std::string sensorsToText (const Player& p)
{
    return whistle::json::write (sensorsToJson (p), true);
}

bool sensorsFromText (const std::string& text, Player& p)
{
    std::string error;
    const auto parsed = whistle::json::parse (text, error);
    if (! parsed || ! parsed->isObject())
        return false;
    sensorsFromJson (*parsed, p);
    return true;
}
