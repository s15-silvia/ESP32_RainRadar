#pragma once

#include <Arduino.h>

// JMA high-resolution precipitation nowcast times.
struct RainRadarTimes
{
  String basetime;
  String validtime;
  bool valid = false;
};

// Fetch latest radar times when the update interval expires.
// Returns true only when new times have been retrieved successfully.
bool rainRadarUpdate(RainRadarTimes& times);
