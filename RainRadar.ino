#include <time.h>

#include "DisplayHardware.h"
#include "AMeDAS.h"
#include "AMeDASDisplay.h"
#include "TimeSeriesForecast.h"
#include "TimeSeriesForecastDisplay.h"
#include "RainRadar.h"
#include "WeatherData.h"
#include "WiFiManager.h"

enum class DisplayPage
{
  AMeDAS,
  TimeSeriesForecast
};

static constexpr unsigned long DISPLAY_INTERVAL = 5000;

// Global variables
WeatherData            weatherData = {};
TimeSeriesForecastData timeSeriesForecastData = {};
RainRadarTimes         rainRadarTimes = {};

struct tm currentInfo;

DisplayPage currentPage = DisplayPage::AMeDAS;
unsigned long lastPageChange = 0;
bool pageChanged = true;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Initialize display hardware and display modules.
  displayHardwareBegin();
  timeSeriesForecastDisplayBegin();

  // Initialize network and time synchronization.
  networkBegin();

  lastPageChange = millis();
}

void loop()
{
  networkUpdate();

  rainRadarUpdate(rainRadarTimes);

  time_t now = time(nullptr);
  localtime_r(&now, &currentInfo);
/*
  if (amedasUpdate(weatherData)) {
    // Update the LCD only when the AMeDAS page is active.
    // If the page has just changed, the full redraw below
    // will display the latest data instead.
    if (currentPage == DisplayPage::AMeDAS && !pageChanged) {
      amedasDisplayUpdateWeather(weatherData);
      // printMemoryStatus("After AMeDAS update");
    }
  }

  if (timeSeriesForecastUpdate(timeSeriesForecastData)) {
    // Update the LCD only when the forecast page is active.
    // If the page has just changed, the full redraw below
    // will display the latest data instead.
    if (currentPage == DisplayPage::TimeSeriesForecast && !pageChanged) {
      timeSeriesForecastDisplayUpdate(timeSeriesForecastData);
      // printMemoryStatus("After forecast update");
    }
  }

  // Page switching
  unsigned long currentMillis = millis();

  if (currentMillis - lastPageChange >= DISPLAY_INTERVAL) {
    lastPageChange = currentMillis;

    if (currentPage == DisplayPage::AMeDAS) {
      currentPage = DisplayPage::TimeSeriesForecast;
    } else {
      currentPage = DisplayPage::AMeDAS;
    }
    pageChanged = true;
  }

  if (pageChanged) {
    if (currentPage == DisplayPage::AMeDAS) {
      amedasDisplayShow(currentInfo, weatherData);
    } else {
      timeSeriesForecastDisplayShow(currentInfo, timeSeriesForecastData);
    }
    pageChanged = false;
  }

  if (currentPage == DisplayPage::AMeDAS) {
    amedasDisplayTick(currentInfo, weatherData);
  } else {
    timeSeriesForecastDisplayTick(currentInfo, timeSeriesForecastData);
  }
*/

  delay(100);
}
