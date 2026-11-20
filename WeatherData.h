#pragma once

#include <time.h>

struct WeatherData
{
  float temperature;
  int humidity;

  float precipitation10m;
  float precipitation1h;
  float precipitation3h;
  float precipitation24h;

  float windSpeed;
  int windDirectionDeg;   // 北:0°，東:90°，．．．
  bool calm;

  float sun10m;
  float sun1h;

  struct tm observationInfo;
  bool observationValid;
};