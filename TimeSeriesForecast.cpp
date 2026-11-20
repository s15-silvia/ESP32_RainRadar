#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include "TimeSeriesForecast.h"
#include "WiFiManager.h"


const char FORECAST_AREA_CODE[] = "140010";

static String getForecastUrl()
{
  return String("https://www.jma.go.jp/bosai/jmatile/data/wdist/VPFD/") + FORECAST_AREA_CODE + ".json";
}

// Forecast detailed debug
static const bool DEBUG_FORECAST = true;

// Forecast interval
const unsigned long FORECAST_INTERVAL = 30UL * 60UL * 1000UL;  // 30分

// 前回のForecast取得開始時刻
unsigned long forecastStartTime = 0;

// NTP同期後の最初のForecast取得
bool forecastFirstRequest = true;

// Forecast initialization
static void initializeForecast(TimeSeriesForecast& forecast)
{
  forecast.datetime = 0;
  forecast.datetimeValid = false;

  forecast.weather = "";

  forecast.temperature = 0;
  forecast.temperatureValid = false;

  forecast.maxTemperature = 0;
  forecast.maxTemperatureValid = false;

  forecast.minTemperature = 0;
  forecast.minTemperatureValid = false;

  forecast.windDirection = "";

  forecast.windSpeed = 0;
  forecast.windSpeedValid = false;

  forecast.windRange = "";
}

static void initializeForecastData(TimeSeriesForecastData& data)
{
  data.reportDatetime = 0;

  // reportValid は一度でも正常取得できたら
  // true のまま保持する
  // ここでは初期化しない

  data.count = 0;

  for (size_t i = 0; i < MAX_FORECASTS; i++) {
    initializeForecast(data.forecasts[i]);
  }
}

static time_t parseForecastDateTime(const String& datetime)
{
  struct tm t = {};

  t.tm_year  = datetime.substring(0, 4).toInt() - 1900;
  t.tm_mon   = datetime.substring(5, 7).toInt() - 1;
  t.tm_mday  = datetime.substring(8, 10).toInt();
  t.tm_hour  = datetime.substring(11, 13).toInt();
  t.tm_min   = datetime.substring(14, 16).toInt();
  t.tm_sec   = datetime.substring(17, 19).toInt();
  t.tm_isdst = 0;

  return mktime(&t);
}

static int findForecastByTime(const TimeSeriesForecast* forecasts, size_t count, time_t datetime)
{
  for (size_t i = 0; i < count; i++) {
    if (!forecasts[i].datetimeValid) {
      continue;
    }

    if (forecasts[i].datetime == datetime) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

static void sortForecasts(TimeSeriesForecast* forecasts, size_t count)
{
  for (size_t i = 1; i < count; i++) {
    TimeSeriesForecast key = forecasts[i];
    size_t j = i;

    while (j > 0 && forecasts[j - 1].datetime > key.datetime) {
      forecasts[j] = forecasts[j - 1];
      j--;
    }
    forecasts[j] = key;
  }
}

static bool getForecastJson(String& payload)
{
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  http.setTimeout(30000);

  String forecastUrl = getForecastUrl();

  if (DEBUG_FORECAST) {
    Serial.print("URL: ");
    Serial.println(forecastUrl);
  }

  if (!http.begin(client, forecastUrl)) {
    Serial.println("HTTP begin failed");
    return false;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);

    Serial.print("Error: ");
    Serial.println(http.errorToString(httpCode));

    http.end();
    return false;
  }

  payload = http.getString();

  http.end();

  if (DEBUG_FORECAST) {
    Serial.print("JSON size: ");
    Serial.print(payload.length());
    Serial.println(" bytes");
  }

  return true;
}

static bool parseReportDateTime(JsonDocument& doc, TimeSeriesForecastData& data)
{
  String reportDateTime =
    doc["reportDateTime"] | "";

  if (reportDateTime.length() == 0) {
    Serial.println("Report datetime not found.");
    return false;
  }

  data.reportDatetime = parseForecastDateTime(reportDateTime);

  if (data.reportDatetime == -1) {
    Serial.println("Report datetime parse failed.");
    return false;
  }

  // reportDatetime が正常取得できたので、
  // 一度でも正常取得したことを記録する
  data.reportValid = true;

  if (DEBUG_FORECAST) {
    Serial.print("Report datetime: ");
    Serial.println(reportDateTime);
  }

  return true;
}

static size_t parseAreaTimeSeries(JsonDocument& doc, TimeSeriesForecast* areaData)
{
  JsonObject area     = doc["areaTimeSeries"];
  JsonArray areaTimes = area["timeDefines"];
  JsonArray weather   = area["weather"];
  JsonArray wind      = area["wind"];

  size_t areaCount = min(areaTimes.size(), static_cast<size_t>(MAX_AREA_FORECASTS));

  for (size_t i = 0; i < areaCount; i++) {
    String datetime = areaTimes[i]["dateTime"] | "";

    if (datetime.length() == 0) {
      continue;
    }

    areaData[i].datetime = parseForecastDateTime(datetime);
    areaData[i].datetimeValid = true;

    areaData[i].weather = weather[i] | "";

    JsonObject windData = wind[i];
    areaData[i].windDirection = windData["direction"] | "";

    if (windData["speed"].is<int>()) {
      areaData[i].windSpeed = windData["speed"].as<int>();
      areaData[i].windSpeedValid = true;
    }

    areaData[i].windRange = windData["range"] | "";
  }

  return areaCount;
}

static size_t parsePointTimeSeries(JsonDocument& doc, TimeSeriesForecast* pointData)
{
  JsonObject point         = doc["pointTimeSeries"];
  JsonArray pointTimes     = point["timeDefines"];
  JsonArray temperature    = point["temperature"];
  JsonArray maxTemperature = point["maxTemperature"];
  JsonArray minTemperature = point["minTemperature"];

  size_t pointCount = min(pointTimes.size(), static_cast<size_t>(MAX_POINT_FORECASTS));

  for (size_t i = 0; i < pointCount; i++) {
    String datetime = pointTimes[i]["dateTime"] | "";

    if (datetime.length() == 0) {
      continue;
    }

    pointData[i].datetime = parseForecastDateTime(datetime);
    pointData[i].datetimeValid = true;

    if (temperature[i].is<int>()) {
      pointData[i].temperature = temperature[i].as<int>();
      pointData[i].temperatureValid = true;
    }

    if (maxTemperature[i].is<int>()) {
      pointData[i].maxTemperature = maxTemperature[i].as<int>();
      pointData[i].maxTemperatureValid = true;
    }

    if (minTemperature[i].is<int>()) {
      pointData[i].minTemperature = minTemperature[i].as<int>();
      pointData[i].minTemperatureValid = true;
    }
  }

  return pointCount;
}

static void mergeForecastData(TimeSeriesForecastData& data, const TimeSeriesForecast* areaData,  size_t areaCount, 
                                                            const TimeSeriesForecast* pointData, size_t pointCount) {
  // First add areaTimeSeries
  for (size_t i = 0; i < areaCount; i++) {
    if (!areaData[i].datetimeValid) {
      continue;
    }

    if (data.count >= MAX_FORECASTS) {
      break;
    }

    data.forecasts[data.count] = areaData[i];
    data.count++;
  }

  // Then merge pointTimeSeries
  for (size_t i = 0; i < pointCount; i++) {
    if (!pointData[i].datetimeValid) {
      continue;
    }

    int index = findForecastByTime( data.forecasts, data.count, pointData[i].datetime);

    if (index >= 0) {
      // Existing datetime:
      // merge point data into area data
      TimeSeriesForecast& forecast  = data.forecasts[index];

      forecast.temperature         = pointData[i].temperature;
      forecast.temperatureValid    = pointData[i].temperatureValid;
      forecast.maxTemperature      = pointData[i].maxTemperature;
      forecast.maxTemperatureValid = pointData[i].maxTemperatureValid;
      forecast.minTemperature      = pointData[i].minTemperature;
      forecast.minTemperatureValid = pointData[i].minTemperatureValid;
    } else {
      // New datetime:
      // pointTimeSeries only
      if (data.count >= MAX_FORECASTS) {
        break;
      }
      data.forecasts[data.count] = pointData[i];
      data.count++;
    }
  }

  // Sort by datetime
  sortForecasts(data.forecasts, data.count);
}

static void printTimeInfo(const TimeSeriesForecastData& data)
{
  time_t currentTime = time(nullptr);

  struct tm currentInfo;
  struct tm reportInfo;

  localtime_r(&currentTime,         &currentInfo);
  localtime_r(&data.reportDatetime, &reportInfo);

  Serial.printf("Current time      : %04d/%02d/%02d %02d:%02d:%02d\n",
    currentInfo.tm_year + 1900,
    currentInfo.tm_mon + 1,
    currentInfo.tm_mday,
    currentInfo.tm_hour,
    currentInfo.tm_min,
    currentInfo.tm_sec
  );

  Serial.printf("Report time       : %04d/%02d/%02d %02d:%02d:%02d\n",
    reportInfo.tm_year + 1900,
    reportInfo.tm_mon + 1,
    reportInfo.tm_mday,
    reportInfo.tm_hour,
    reportInfo.tm_min,
    reportInfo.tm_sec
  );

  long age = currentTime - data.reportDatetime;

  if (age >= 0) {
    Serial.printf("Data age          : %ld h %ld min %ld sec\n",
      age / 3600,
      (age % 3600) / 60,
      age % 60
    );
  } else {
    Serial.printf("Data age          : -%ld sec (future)\n",
      -age
    );
  }
}

static void printForecastData(const TimeSeriesForecastData& data)
{
  Serial.print("Forecast count: ");
  Serial.println(data.count);

  for (size_t i = 0; i < data.count; i++) {
    const TimeSeriesForecast& forecast = data.forecasts[i];

    struct tm timeInfo;

    localtime_r(&forecast.datetime, &timeInfo);

    Serial.printf("%04d/%02d/%02d %02d:%02d",
      timeInfo.tm_year + 1900,
      timeInfo.tm_mon + 1,
      timeInfo.tm_mday,
      timeInfo.tm_hour,
      timeInfo.tm_min
    );

    Serial.print("  weather=");

    if (forecast.weather.length() > 0) {
      Serial.print(forecast.weather);
    } else {
      Serial.print("--");
    }

    Serial.print("  temp=");

    if (forecast.temperatureValid) {
      Serial.print(forecast.temperature);
    } else {
      Serial.print("--");
    }

    Serial.print("  max=");

    if (forecast.maxTemperatureValid) {
      Serial.print(forecast.maxTemperature);
    } else {
      Serial.print("--");
    }

    Serial.print("  min=");

    if (forecast.minTemperatureValid) {
      Serial.print(forecast.minTemperature);
    } else {
      Serial.print("--");
    }

    Serial.print("  wind=");

    if (forecast.windDirection.length() > 0) {
      Serial.print(forecast.windDirection);
    } else {
      Serial.print("--");
    }

    Serial.print(" ");

    if (forecast.windSpeedValid) {
      Serial.print(forecast.windSpeed);
    } else {
      Serial.print("--");
    }

    Serial.print("  range=");

    if (forecast.windRange.length() > 0) {
      Serial.print(forecast.windRange);
    } else {
      Serial.print("--");
    }

    Serial.println();
  }
}

static bool fetchTimeSeriesForecast(TimeSeriesForecastData& data)
{
  Serial.println();
  Serial.println("=== Time Series Forecast ===");

  // Get JSON
  String payload;

  if (!getForecastJson(payload)) {
    return false;
  }

  // JSON parse
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  if (DEBUG_FORECAST) {
    Serial.println("JSON parse OK");
  }

  // Initialize result
  initializeForecastData(data);

  // Report datetime
  if (!parseReportDateTime(doc, data)) {
    return false;
  }

  // Temporary data
  TimeSeriesForecast areaData[MAX_AREA_FORECASTS];
  TimeSeriesForecast pointData[MAX_POINT_FORECASTS];

  for (size_t i = 0; i < MAX_AREA_FORECASTS; i++) {
    initializeForecast(areaData[i]);
  }

  for (size_t i = 0; i < MAX_POINT_FORECASTS; i++) {
    initializeForecast(pointData[i]);
  }

  // Parse areaTimeSeries
  size_t areaCount = parseAreaTimeSeries(doc, areaData);

  // Parse pointTimeSeries
  size_t pointCount = parsePointTimeSeries(doc, pointData);

  // Merge
  mergeForecastData(data, areaData, areaCount, pointData, pointCount);

  // Time information
  printTimeInfo(data);

  // Debug output
  printForecastData(data);

  return true;
}

bool timeSeriesForecastUpdate(TimeSeriesForecastData& data)
{
  if (!isTimeSynchronized()) {
    return false;
  }

  if (!forecastFirstRequest && millis() - forecastStartTime < FORECAST_INTERVAL) {
    return false;
  }

  forecastStartTime = millis();

  Serial.println();
  Serial.println("Starting forecast request...");

  if (fetchTimeSeriesForecast(data)) {
    forecastFirstRequest = false;
    Serial.println("Forecast request succeeded.");
    return true;
  } else {
    Serial.println("Forecast request failed.");
    networkReconnect();
    return false;
  }
}
