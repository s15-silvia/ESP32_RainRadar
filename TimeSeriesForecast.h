#pragma once

#include <Arduino.h>
#include <time.h>

constexpr size_t MAX_AREA_FORECASTS = 14;
constexpr size_t MAX_POINT_FORECASTS = 15;
constexpr size_t MAX_FORECASTS = 15;

struct TimeSeriesForecast
{
  time_t datetime;
  bool datetimeValid;

  String weather;

  int temperature;
  bool temperatureValid;

  int maxTemperature;
  bool maxTemperatureValid;

  int minTemperature;
  bool minTemperatureValid;

  String windDirection;

  int windSpeed;
  bool windSpeedValid;

  String windRange;
};

struct TimeSeriesForecastData
{
  time_t reportDatetime;
  bool reportValid;

  TimeSeriesForecast forecasts[MAX_FORECASTS];
  size_t count;
};

bool timeSeriesForecastUpdate(TimeSeriesForecastData& data);
