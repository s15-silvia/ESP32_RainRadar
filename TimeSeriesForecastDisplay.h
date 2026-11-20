#pragma once

#include <time.h>
#include "TimeSeriesForecast.h"

void timeSeriesForecastDisplayBegin();
void timeSeriesForecastDisplayShow(const struct tm &currentInfo, const TimeSeriesForecastData &data);
void timeSeriesForecastDisplayUpdate(const TimeSeriesForecastData& data);
void timeSeriesForecastDisplayTick(const struct tm &currentInfo, const TimeSeriesForecastData &data);