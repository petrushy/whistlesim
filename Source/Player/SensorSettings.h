// The Player's sensor response settings (breath, holes, thumb, tilt) as JSON: how the apps save them, and the
// "sensors" line at the top of a recording (WhistleCheck replay reads it).

#pragma once

#include "Player.h"
#include "Whistle/Json.h"

#include <string>

whistle::json::Value sensorsToJson (const Player&);
void sensorsFromJson (const whistle::json::Value&, Player&);  // Settings missing from it are left as they are

std::string sensorsToText (const Player&);                    // On one line
bool sensorsFromText (const std::string&, Player&);           // false: not JSON
