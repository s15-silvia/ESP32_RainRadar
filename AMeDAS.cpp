#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include "AMeDAS.h"
#include "WeatherData.h"
#include "WiFiManager.h"

// 横浜
static const char STATION[] = "46106";
static const char STATION_NAME[] = "Yokohama";

// AMeDAS詳細デバッグ
static const bool DEBUG_AMEDAS = false;

// AMeDAS取得間隔
const unsigned long AMEDAS_INTERVAL = 5UL * 60UL * 1000UL;  // 5分 = 5 x 60 x 1000 msec

// 前回のAMeDAS取得開始時刻
unsigned long amedasStartTime = 0;

// NTP同期後の最初のAMeDAS取得
bool amedasFirstRequest = true;

// → 北を0°としたコンパス角
static const float WIND_DIRECTION_DEG[] = {
    0.0f,    // 0  静穏
   22.5f,    // 1  北北東
   45.0f,    // 2  北東
   67.5f,    // 3  東北東
   90.0f,    // 4  東
  112.5f,    // 5  東南東
  135.0f,    // 6  南東
  157.5f,    // 7  南南東
  180.0f,    // 8  南
  202.5f,    // 9  南南西
  225.0f,    // 10 南西
  247.5f,    // 11 西南西
  270.0f,    // 12 西
  292.5f,    // 13 西北西
  315.0f,    // 14 北西
  337.5f,    // 15 北北西
    0.0f     // 16 北
};

static String getLatestTime()
{
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  http.setTimeout(30000);

  const char* latestUrl = "https://www.jma.go.jp/bosai/amedas/data/latest_time.txt";

  if (DEBUG_AMEDAS) {
    Serial.println("Get latest time...");
  }

  if (!http.begin(client, latestUrl)) {
    Serial.println("HTTP begin failed");
    return "";
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);

    Serial.print("Error: ");
    Serial.println(http.errorToString(httpCode));

    http.end();
    return "";
  }

  String latest = http.getString();
  latest.trim();

  http.end();

  if (DEBUG_AMEDAS) {
    Serial.print("Latest time: ");
    Serial.println(latest);
  }

  return latest;
}

static String makeTimestamp(const String& latest)
{
  // 例：
  // 2026-09-06T15:00:00+09:00
  // ↓
  // 20260906150000

  return
    latest.substring( 0,  4) +
    latest.substring( 5,  7) +
    latest.substring( 8, 10) +
    latest.substring(11, 13) +
    latest.substring(14, 16) +
    latest.substring(17, 19);
}

static bool getStationJson(const String& url, const char* station, String& stationJson)
{
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60000);

  HTTPClient http;
  http.setTimeout(60000);

  if (DEBUG_AMEDAS) {
    Serial.print("URL: ");
    Serial.println(url);
  }

  if (!http.begin(client, url)) {
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

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(60000);

  if (DEBUG_AMEDAS) {
    Serial.print("Searching station ");
    Serial.print(station);
    Serial.println("...");
  }

  String target = "\"";
  target += station;
  target += "\"";

  int matched = 0;

  bool found = false;
  bool started = false;

  int depth = 0;

  stationJson = "";
  stationJson.reserve(4096);

  uint8_t buffer[1024];

  while (http.connected()) {

    size_t available = stream->available();

    if (available) {
      size_t len = stream->readBytes(buffer, min(available, sizeof(buffer)));

      for (size_t i = 0; i < len; i++) {
        char c = buffer[i];

        // Search station number
        if (!found) {
          if (c == target[matched]) {
            matched++;

            if (matched == target.length()) {
              found = true;
              if (DEBUG_AMEDAS) {
                Serial.print("Found station ");
                Serial.print(station);
                Serial.println("!");
              }
              matched = 0;
            }
          } else {
            if (c == target[0]) {
              matched = 1;
            } else {
              matched = 0;
            }
          }
          continue;
        }

        // Find beginning of JSON object
        if (!started) {
          if (c == '{') {
            started = true;
            depth = 1;
            stationJson = "{";
          }
          continue;
        }

        // Collect station JSON
        stationJson += c;

        if (c == '{') {
          depth++;
        } else if (c == '}') {
          depth--;
          if (depth == 0) {
            if (DEBUG_AMEDAS) {
              Serial.println();
              Serial.println("----- STATION JSON -----");
              Serial.println(stationJson);
              Serial.println("----- END -----");
            }
            http.end();
            return true;
          }
        } else {
          ;
        }
      }
    } else {
      if (!http.connected()) {
        break;
      }
      delay(1);
    }
  }

  http.end();

  Serial.println("Station JSON not found");

  return false;
}

static bool parseStationData(const String& stationJson, WeatherData& data)
{
  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, stationJson);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  if (DEBUG_AMEDAS) {
    Serial.println("JSON parse OK");
  }

  data.temperature      = doc["temp"][0];
  data.humidity         = doc["humidity"][0];
  data.precipitation10m = doc["precipitation10m"][0];
  data.precipitation1h  = doc["precipitation1h"][0];
  data.precipitation3h  = doc["precipitation3h"][0];
  data.precipitation24h = doc["precipitation24h"][0];
  data.windSpeed        = doc["wind"][0];

  int windDirection = doc["windDirection"][0];
  if (windDirection >= 0 && windDirection <=16) {
    data.windDirectionDeg = WIND_DIRECTION_DEG[windDirection];
    data.calm = (windDirection == 0);
  } else {
    data.windDirectionDeg = 0.0f;
    data.calm = true;
  }

  data.sun10m = doc["sun10m"][0];
  data.sun1h  = doc["sun1h"][0];

  return true;
}

static time_t parseObservationTime(const String& latest)
{
  struct tm t = {};

  t.tm_year  = latest.substring(0, 4).toInt() - 1900;
  t.tm_mon   = latest.substring(5, 7).toInt() - 1;
  t.tm_mday  = latest.substring(8, 10).toInt();
  t.tm_hour  = latest.substring(11, 13).toInt();
  t.tm_min   = latest.substring(14, 16).toInt();
  t.tm_sec   = latest.substring(17, 19).toInt();
  t.tm_isdst = 0;

  return mktime(&t);
}

static void setObservationInfo(const String& latest, WeatherData& data)
{
  time_t observationTime = parseObservationTime(latest);
  localtime_r(&observationTime, &data.observationInfo);

  data.observationValid = true;
}

static void printTimeInfo(const String& latest)
{
  time_t currentTime     = time(nullptr);
  time_t observationTime = parseObservationTime(latest);

  struct tm currentInfo;
  struct tm observationInfo;

  localtime_r(&currentTime,     &currentInfo);
  localtime_r(&observationTime, &observationInfo);

  Serial.printf("Current time      : %04d/%02d/%02d %02d:%02d:%02d\n",
    currentInfo.tm_year + 1900,
    currentInfo.tm_mon + 1,
    currentInfo.tm_mday,
    currentInfo.tm_hour,
    currentInfo.tm_min,
    currentInfo.tm_sec
  );

  Serial.printf("Observation time  : %04d/%02d/%02d %02d:%02d:%02d\n",
    observationInfo.tm_year + 1900,
    observationInfo.tm_mon + 1,
    observationInfo.tm_mday,
    observationInfo.tm_hour,
    observationInfo.tm_min,
    observationInfo.tm_sec
  );

  long age = currentTime - observationTime;

  if (age >= 0) {
    Serial.printf("Data age          : %ld min %ld sec\n", age / 60, age % 60);
  } else {
    Serial.printf("Data age          : -%ld sec (future)\n", -age);
  }
}

static void printAmedasData(const WeatherData& data)
{
  Serial.println();

  Serial.print("=== ");
  Serial.print(STATION_NAME);
  Serial.print(" (");
  Serial.print(STATION);
  Serial.println(") ===");

  Serial.print("Temp              : ");
  Serial.print(data.temperature, 1);
  Serial.println(" ℃");

  Serial.print("Humidity          : ");
  Serial.print(data.humidity);
  Serial.println(" %");

  Serial.print("Precipitation 10m : ");
  Serial.print(data.precipitation10m, 1);
  Serial.println(" mm");

  Serial.print("Precipitation 1h  : ");
  Serial.print(data.precipitation1h, 1);
  Serial.println(" mm");

  Serial.print("Precipitation 3h  : ");
  Serial.print(data.precipitation3h, 1);
  Serial.println(" mm");

  Serial.print("Precipitation 24h : ");
  Serial.print(data.precipitation24h, 1);
  Serial.println(" mm");

  Serial.print("Wind              : ");
  Serial.print(data.windSpeed, 1);
  Serial.println(" m/s");

  Serial.print("Wind direction    : ");
  Serial.print(data.windDirectionDeg, 1);
  Serial.println(" deg");

  Serial.print("Sunshine 10m      : ");
  Serial.print(data.sun10m, 1);
  Serial.println(" h");

  Serial.print("Sunshine 1h       : ");
  Serial.print(data.sun1h, 1);
  Serial.println(" h");

  Serial.println();
}

bool fetchAMeDAS(WeatherData& data)
{
  Serial.println();
  Serial.println("=== AMeDAS ===");

  // Get latest observation time
  String latest = getLatestTime();

  if (latest.length() == 0) {
    return false;
  }

  // Create timestamp
  String timestamp = makeTimestamp(latest);

  if (DEBUG_AMEDAS) {
    Serial.print("Timestamp: ");
    Serial.println(timestamp);
  }

  // Get AMeDAS map JSON
  String url = "https://www.jma.go.jp/bosai/amedas/data/map/" + timestamp + ".json";

  String stationJson;
  if (!getStationJson(url, STATION, stationJson)) {
    return false;
  }

  // Parse JSON
  if (!parseStationData(stationJson, data)) {
    return false;
  }

  // Observation time
  setObservationInfo(latest, data);

  // Debug output
  printTimeInfo(latest);
  printAmedasData(data);

  return true;
}

bool amedasUpdate(WeatherData &weatherData)
{
  if (!isTimeSynchronized()) {
    return false;
  }

  if (!amedasFirstRequest && millis() - amedasStartTime < AMEDAS_INTERVAL) {
    return false;
  }

  amedasStartTime = millis();

  Serial.println();
  Serial.println("Starting AMeDAS request...");

  if (fetchAMeDAS(weatherData)) {
    amedasFirstRequest = false;
    Serial.println("AMeDAS request succeeded.");
    return true;
  } else {
    Serial.println("AMeDAS request failed.");
    networkReconnect();
    return false;
  }
}
