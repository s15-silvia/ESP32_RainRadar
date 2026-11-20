#pragma once

#include <time.h>
#include "WeatherData.h"

void amedasDisplayShow(const struct tm &currentInfo, const WeatherData &data);
void amedasDisplayUpdateWeather(const WeatherData &data);
void amedasDisplayTick(const struct tm &currentInfo, const WeatherData &data);
