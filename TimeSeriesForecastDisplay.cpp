#include <Arduino.h>
#include <stdio.h>
#include <time.h>

#include "TimeSeriesForecastDisplay.h"
#include "DisplayHardware.h"
#include "DisplayCommon.h"
#include "WeatherIcons.h"
#include "WindIcons.h"
#include "Fonts.h"

// ============================================================
// Colors
// ============================================================
// ---- 基本色 ----
static constexpr uint32_t C_BG   = 0x0B0F14;      // 背景：ほぼ黒の濃い青
static constexpr uint32_t C_LINE = 0x1F2A36;      // 区切り線・グリッド：暗い青灰色
static constexpr uint32_t C_SUB  = 0x8FA0B0;      // 明るめの補助文字：青みのあるライトグレー

// ---- 天気データの色 ----
static constexpr uint32_t C_TEMP = 0xF2B36D;      // 気温：淡いオレンジ

// ---- 更新状態の色 ----
static constexpr uint32_t C_OK   = 0x7DFF9B;      // 正常：明るい緑
static constexpr uint32_t C_WARN = 0xFFC94D;      // 注意：黄色・アンバー
static constexpr uint32_t C_BAD  = 0xFF6B6B;      // 警告・異常：赤・コーラル

// ---- 風速レベルの色 ----
static constexpr uint32_t C_WIND_CALM = 0xD0D0D0; // 風速レベル1：ライトグレー
static constexpr uint32_t C_WIND_2    = 0x00BFFF; // 風速レベル2：ディープスカイブルー
static constexpr uint32_t C_WIND_3    = 0x00FF00; // 風速レベル3：ライムグリーン
static constexpr uint32_t C_WIND_4    = 0xFFFF00; // 風速レベル4：黄色
static constexpr uint32_t C_WIND_5    = 0xFF8000; // 風速レベル5：オレンジ
static constexpr uint32_t C_WIND_6    = 0xFF2020; // 風速レベル6：鮮やかな赤

// ============================================================
// Layout
// ============================================================
static constexpr int SCREEN_W = 320;

static constexpr int WEATHER_WIND_COUNT = 10;
static constexpr int TEMPERATURE_COUNT = 11;

// ---- Static line ----
static constexpr int HEADER_LINE_X = 10;
static constexpr int HEADER_LINE_Y = 32;
static constexpr int HEADER_LINE_W = SCREEN_W - 20;

// ---- Forecast grid ----
static constexpr int GRID_X0 = 10;
static constexpr int GRID_X_STEP = 30;
static constexpr int FORECAST_TIME_Y = 48;

// Vertical grid lines
static constexpr int GRID_TOP = 54;
static constexpr int GRID_BOTTOM = 216;

// ---- Weather icons ----
static constexpr int WEATHER_Y = 64;
static constexpr int ICON_W = WEATHER_ICON_WIDTH;
static constexpr int ICON_H = WEATHER_ICON_HEIGHT;

// ---- Temperature graph ----
static constexpr int TEMP_GRAPH_TOP = 120;
static constexpr int TEMP_GRAPH_BOTTOM = 180;

// ---- Wind ----
static constexpr int WIND_Y = 204;

// ---- Footer ----
static constexpr int FOOTER_LINE_Y = 217;

// ============================================================
// Text datum
// ============================================================
using Datum = lgfx::textdatum::textdatum_t;

static constexpr Datum DATUM_BL = lgfx::textdatum::baseline_left;
static constexpr Datum DATUM_BC = lgfx::textdatum::baseline_center;
static constexpr Datum DATUM_BR = lgfx::textdatum::baseline_right;

// ============================================================
// Grid positions
// ============================================================
static int getGridX(int index)
{
    return GRID_X0 + index * GRID_X_STEP;
}

static int getIntervalCenterX(int index)
{
    return getGridX(index) + GRID_X_STEP / 2;
}

// ============================================================
// Static display
// ============================================================
static void drawStatic()
{
    tft.fillScreen(C_BG);

    tft.drawFastHLine(HEADER_LINE_X, HEADER_LINE_Y, HEADER_LINE_W, C_LINE);
}

// ============================================================
// Forecast time formatting
// ============================================================
static void formatForecastTime(time_t value, bool valid, char* buffer, size_t bufferSize)
{
    if (!valid) {
        snprintf(buffer, bufferSize, "--");
        return;
    }

    struct tm timeInfo;
    localtime_r(&value, &timeInfo);

    snprintf(buffer, bufferSize, "%02d", timeInfo.tm_hour);
}

// ============================================================
// Weather icon selection
// ============================================================
static const uint16_t* getWeatherIcon(const String &weather, time_t forecastTime)
{
    if (weather == "晴れ") {
        struct tm timeInfo;
        localtime_r(&forecastTime, &timeInfo);

        // Night: 18:00–05:59
        if (timeInfo.tm_hour >= 18 || timeInfo.tm_hour < 6) {
            return ICON_SUNNY_NIGHT;
        }
        return ICON_SUNNY_DAY;
    }

    if (weather == "くもり") {
        return ICON_CLOUDY;
    }

    if (weather == "雨") {
        return ICON_RAIN;
    }

    if (weather == "雪") {
        return ICON_SNOW;
    }

    if (weather == "雨または雪") {
        return ICON_SLEET;
    }

    return nullptr;
}

// ============================================================
// Wind direction
// ============================================================
// Degrees indicate the direction FROM which the wind blows.
// North = 0 degrees, east = 90 degrees, clockwise.
static bool getWindDirectionDegrees(const String &direction, int &degrees)
{
    if (direction == "北") {
        degrees = 0;
    } else if (direction == "北東") {
        degrees = 45;
    } else if (direction == "東") {
        degrees = 90;
    } else if (direction == "南東") {
        degrees = 135;
    } else if (direction == "南") {
        degrees = 180;
    } else if (direction == "南西") {
        degrees = 225;
    } else if (direction == "西") {
        degrees = 270;
    } else if (direction == "北西") {
        degrees = 315;
    } else {
        return false;
    }

    return true;
}

// ============================================================
// Vertical grid lines
// ============================================================
static void drawVerticalGrid()
{
    for (int i = 0; i < TEMPERATURE_COUNT; ++i) {
        int x = getGridX(i);

        tft.drawFastVLine(
            x,
            GRID_TOP,
            GRID_BOTTOM - GRID_TOP + 1,
            C_LINE
        );
    }
}

// ============================================================
// Forecast times: 11 elements
// ============================================================

static void drawForecastTimes(const TimeSeriesForecastData &data)
{
    for (int i = 0; i < TEMPERATURE_COUNT; ++i) {
        int x = getGridX(i);

        char timeBuffer[8];

        if (i < static_cast<int>(data.count)) {
            const TimeSeriesForecast &forecast = data.forecasts[i];
            formatForecastTime(forecast.datetime, forecast.datetimeValid, timeBuffer, sizeof(timeBuffer));
        } else {
            snprintf(timeBuffer, sizeof(timeBuffer), "--");
        }

        drawText(Inter_Regular_10_vlw, timeBuffer, x, FORECAST_TIME_Y, C_SUB, DATUM_BC);
    }
}

// ============================================================
// Weather icons: 10 elements
// ============================================================
static void drawWeatherIcons(const TimeSeriesForecastData &data)
{
    for (int i = 0; i < WEATHER_WIND_COUNT; ++i) {
        int x = getIntervalCenterX(i);

        if (i >= static_cast<int>(data.count)) {
            continue;
        }

        const TimeSeriesForecast &forecast = data.forecasts[i];
        
        if (!forecast.datetimeValid) {
            continue;
        }

        const uint16_t* icon = getWeatherIcon(
            forecast.weather,
            forecast.datetime
        );

        if (icon == nullptr) {
            continue;
        }

        // Black (0x0000) is the transparent color key.
        tft.pushImage(x - ICON_W / 2, WEATHER_Y, ICON_W, ICON_H, icon, 0x0000);
    }
}

// ============================================================
// Temperature graph: 11 elements
// ============================================================
static void drawTemperatureGraph(const TimeSeriesForecastData &data)
{
    int minTemp = 100;
    int maxTemp = -100;
    int validCount = 0;

    // Find temperature range.
    for (int i = 0; i < TEMPERATURE_COUNT; ++i) {
        if (i >= static_cast<int>(data.count)) {
            continue;
        }

        const TimeSeriesForecast &forecast = data.forecasts[i];

        if (!forecast.temperatureValid) {
            continue;
        }

        if (forecast.temperature < minTemp) {
            minTemp = forecast.temperature;
        }

        if (forecast.temperature > maxTemp) {
            maxTemp = forecast.temperature;
        }

        ++validCount;
    }

    if (validCount == 0) {
        return;
    }

    // Ensure that the temperature range is not zero.
    if (minTemp == maxTemp) {
        --minTemp;
        ++maxTemp;
    }

    // Add a small vertical margin.
    --minTemp;
    ++maxTemp;

    const int graphHeight = TEMP_GRAPH_BOTTOM - TEMP_GRAPH_TOP;

    int pointX[TEMPERATURE_COUNT];
    int pointY[TEMPERATURE_COUNT];
    bool pointValid[TEMPERATURE_COUNT];

    for (int i = 0; i < TEMPERATURE_COUNT; ++i) {
        // Temperature points align with vertical grid lines.
        pointX[i] = getGridX(i);
        pointY[i] = TEMP_GRAPH_BOTTOM;
        pointValid[i] = false;

        if (i >= static_cast<int>(data.count)) {
            continue;
        }

        const TimeSeriesForecast &forecast = data.forecasts[i];

        if (!forecast.temperatureValid) {
            continue;
        }

        pointValid[i] = true;

        float ratio = static_cast<float>(forecast.temperature - minTemp) / static_cast<float>(maxTemp - minTemp);

        pointY[i] = TEMP_GRAPH_BOTTOM - static_cast<int>(ratio * graphHeight);
    }

    // Draw connecting lines.
    for (int i = 0; i < TEMPERATURE_COUNT - 1; ++i) {
        if (!pointValid[i] || !pointValid[i + 1]) {
            continue;
        }
        tft.drawLine(pointX[i], pointY[i], pointX[i + 1], pointY[i + 1], C_TEMP);
    }

    // Draw points and numeric temperature labels.
    for (int i = 0; i < TEMPERATURE_COUNT; ++i) {
        if (!pointValid[i]) {
            continue;
        }

        const TimeSeriesForecast &forecast = data.forecasts[i];

        tft.fillCircle(pointX[i], pointY[i], 2, C_TEMP);

        char tempBuffer[8];

        // Numeric value only; omit the degree symbol.
        snprintf(tempBuffer, sizeof(tempBuffer), "%d", forecast.temperature);

        int labelY = pointY[i] - 9;

        drawText(Inter_Regular_10_vlw, tempBuffer, pointX[i], labelY, C_TEMP, DATUM_BC);
    }
}

// ============================================================
// Wind speed color
// ============================================================
static uint32_t getWindColor(int windSpeed)
{
    switch (windSpeed) {
        case 2:
            return C_WIND_2;
        case 3:
            return C_WIND_3;
        case 4:
            return C_WIND_4;
        case 5:
            return C_WIND_5;
        case 6:
            return C_WIND_6;
        default:
            return C_WIND_CALM;
    }
}

// ============================================================
// Wind icon rendering
// ============================================================
// Blend two RGB888 colors using an 8-bit alpha value.
static uint32_t blendColor(uint32_t bg, uint32_t fg, uint8_t alpha)
{
    const uint32_t invAlpha = 255 - alpha;

    const uint32_t r = ((((bg >> 16) & 0xFF) * invAlpha) + (((fg >> 16) & 0xFF) * alpha) + 127) / 255;
    const uint32_t g = ((((bg >>  8) & 0xFF) * invAlpha) + (((fg >>  8) & 0xFF) * alpha) + 127) / 255;
    const uint32_t b = ((((bg      ) & 0xFF) * invAlpha) + (((fg      ) & 0xFF) * alpha) + 127) / 255;

    return (r << 16) | (g << 8) | b;
}

// Draw a 16x16 mask centered at the specified position.
static void drawWindIcon(int centerX, int centerY, const uint8_t* mask, uint32_t color)
{
    const int originX = centerX - WIND_ICON_SIZE / 2;
    const int originY = centerY - WIND_ICON_SIZE / 2;

    for (int y = 0; y < WIND_ICON_SIZE; ++y) {
        for (int x = 0; x < WIND_ICON_SIZE; ++x) {
            const uint8_t alpha = mask[y * WIND_ICON_SIZE + x];

            // Transparent pixels do not need to be drawn.
            if (alpha == 0) {
                continue;
            }

            const uint32_t pixelColor = (alpha == 255) ? color : blendColor(C_BG, color, alpha);

            tft.drawPixel(originX + x, originY + y, pixelColor);
        }
    }
}

// Select the icon according to the direction FROM which
// the wind blows. The generated icon itself points TO the
// direction in which the wind travels.
static const uint8_t* getWindIconForDegrees(int degrees)
{
    degrees %= 360;

    if (degrees < 0) {
        degrees += 360;
    }

    const int direction = ((degrees + 22) / 45) % 8;

    switch (direction) {
        case 0:
            return WIND_ICON_N;
        case 1:
            return WIND_ICON_NE;
        case 2:
            return WIND_ICON_E;
        case 3:
            return WIND_ICON_SE;
        case 4:
            return WIND_ICON_S;
        case 5:
            return WIND_ICON_SW;
        case 6:
            return WIND_ICON_W;
        case 7:
            return WIND_ICON_NW;
        default:
            return WIND_ICON_N;
    }
}

// ============================================================
// Wind directions: 10 elements
// ============================================================
static void drawWindDirections(const TimeSeriesForecastData &data)
{
    for (int i = 0; i < WEATHER_WIND_COUNT; ++i) {
        const int x = getIntervalCenterX(i);

        if (i >= static_cast<int>(data.count)) {
            continue;
        }

        const TimeSeriesForecast &forecast = data.forecasts[i];

        // Do not draw wind data when it is invalid.
        if (!forecast.windSpeedValid) {
            continue;
        }

        const int windSpeed = forecast.windSpeed;

        // JMA wind-speed levels are 1 through 6.
        if (windSpeed < 1 || windSpeed > 6) {
            continue;
        }

        const uint32_t color = getWindColor(windSpeed);

        // Level 1: use the calm marker.
        if (windSpeed == 1) {
            drawWindIcon(x, WIND_Y, WIND_ICON_CALM, C_WIND_CALM);
            continue;
        }

        int windFromDegrees;

        // If the direction is unavailable (including 静穏),
        // show the calm marker in the corresponding wind color.
        if (!getWindDirectionDegrees(forecast.windDirection, windFromDegrees)) {
            drawWindIcon(x, WIND_Y, WIND_ICON_CALM, color);
            continue;
        }

        // Select the pre-generated 16x16 directional mask.
        const uint8_t* icon = getWindIconForDegrees(windFromDegrees);

        drawWindIcon(x, WIND_Y, icon, color);
    }
}

// ============================================================
// Forecast
// ============================================================
static void clearForecast()
{
    // Clear only the forecast area.
    // The header and footer remain intact.
    tft.fillRect(0, HEADER_LINE_Y + 1, SCREEN_W, FOOTER_LINE_Y - HEADER_LINE_Y - 1, C_BG);
}

static void drawForecast(const TimeSeriesForecastData &data)
{
    drawForecastTimes(data);
    drawVerticalGrid();
    drawWeatherIcons(data);
    drawTemperatureGraph(data);
    drawWindDirections(data);
}

// ============================================================
// Update forecast
// ============================================================
static void updateForecast(const TimeSeriesForecastData &data)
{
    clearForecast();
    drawForecast(data);
}

// ============================================================
// Update line color
// ============================================================
static uint32_t forecastUpdateDotColor(const struct tm &currentInfo, const TimeSeriesForecastData &data)
{
    // Report time is unknown.
    if (!data.reportValid) {
        return C_BAD;
    }

    struct tm targetInfo = currentInfo;
    targetInfo.tm_sec = 0;
    targetInfo.tm_min = 0;
    targetInfo.tm_isdst = -1;

    const int hour = currentInfo.tm_hour;

    if (hour < 6) {
        // Previous day's 17:00 report
        targetInfo.tm_mday -= 1;
        targetInfo.tm_hour = 17;
    } else if (hour < 12) {
        // Today's 05:00 report
        targetInfo.tm_hour = 5;
    } else if (hour < 18) {
        // Today's 11:00 report
        targetInfo.tm_hour = 11;
    } else {
        // Today's 17:00 report
        targetInfo.tm_hour = 17;
    }

    const time_t targetTime = mktime(&targetInfo);

    // Green when the required report or a newer report
    // has been retrieved.
    if (data.reportDatetime >= targetTime) {
        return C_OK;
    }

    return C_BAD;
}

// ============================================================
// Draw update line
// ============================================================
static void drawUpdateLine(const struct tm &currentInfo, const TimeSeriesForecastData &data)
{
    char buf[48];
    uint32_t dotColor = C_BAD;

    if (data.reportValid) {
        struct tm currentCopy = currentInfo;

        time_t currentTime = mktime(&currentCopy);
        time_t reportTime = data.reportDatetime;

        long ageSec = static_cast<long>( difftime(currentTime, reportTime) );

        if (ageSec < 0) {
            ageSec = 0;
        }

        const long ageMin = ageSec / 60;
        const long hours = ageMin / 60;
        const long minutes = ageMin % 60;

        struct tm reportInfo;
        localtime_r(&reportTime, &reportInfo);

        if (hours == 0) {
            snprintf(buf, sizeof(buf), "%02d:%02d · %ld min ago",
                     reportInfo.tm_hour, reportInfo.tm_min, minutes);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d · %ld hour%s %ld min ago",
                     reportInfo.tm_hour, reportInfo.tm_min, hours, (hours == 1) ? "" : "s", minutes);
        }

        dotColor = forecastUpdateDotColor(currentInfo, data);

    } else {
        snprintf(buf, sizeof(buf), "--:-- · Report time unknown");
    }

    displayCommonDrawUpdateLine(buf, dotColor);
}

// ============================================================
// Update line tick
// ============================================================
static int lastCurrentMinute = -1;
static time_t lastReportDatetime = 0;
static bool lastReportValid = false;

static void tickUpdateLine(const struct tm &currentInfo, const TimeSeriesForecastData &data)
{
    if (currentInfo.tm_min == lastCurrentMinute &&
        data.reportDatetime == lastReportDatetime &&
        data.reportValid == lastReportValid) {
        return;
    }

    lastCurrentMinute = currentInfo.tm_min;
    lastReportDatetime = data.reportDatetime;
    lastReportValid = data.reportValid;

    drawUpdateLine(currentInfo, data);
}

// ============================================================
// Forecast display begin
// ============================================================
void timeSeriesForecastDisplayBegin()
{
    // Reset redraw control, so the first tick draws the date and time.
    displayCommonResetDateTime();
}

// ============================================================
// Forecast display update
// ============================================================
void timeSeriesForecastDisplayUpdate(const TimeSeriesForecastData &data)
{
    updateForecast(data);
}

// ============================================================
// Forecast display tick
// ============================================================
void timeSeriesForecastDisplayTick(const struct tm &currentInfo, const TimeSeriesForecastData &data)
{
    displayCommonTickDateTime(currentInfo);
    tickUpdateLine(currentInfo, data);
}

// ============================================================
// Show forecast display
// ============================================================
void timeSeriesForecastDisplayShow(const struct tm &currentInfo, const TimeSeriesForecastData &data)
{
    // 画面全体を初期描画
    drawStatic();

    // 最新の予報データをすべて描画
    drawForecast(data);

    // 差分更新の状態をリセット
    displayCommonResetDateTime();

    lastCurrentMinute = -1;
    lastReportDatetime = 0;
    lastReportValid = false;
}
