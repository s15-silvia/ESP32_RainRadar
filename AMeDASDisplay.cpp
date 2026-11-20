#include <stdio.h>
#include <math.h>
#include <time.h>
#include <LovyanGFX.hpp>

#include "AMeDASDisplay.h"
#include "DisplayHardware.h"
#include "DisplayCommon.h"
#include "WeatherData.h"
#include "Fonts.h"

void drawWindStatic();
static void drawStatic();

// ============================================================
// Colors
// ============================================================
// ---- 基本色 ----
static constexpr uint32_t C_BG   = 0x0B0F14;      // 背景：ほぼ黒の濃い青
static constexpr uint32_t C_LINE = 0x1F2A36;      // 区切り線：暗い青灰色
static constexpr uint32_t C_DIM  = 0x5B6B7A;      // 暗めの補助文字：青みのあるグレー
static constexpr uint32_t C_SUB  = 0x8FA0B0;      // 明るめの補助文字：青みのあるライトグレー
static constexpr uint32_t C_TEXT = 0xE8F1F8;      // 通常の文字：ほぼ白の青みがかった色

// ---- 天気データの色 ----
static constexpr uint32_t C_TEMP = 0xFF8A3D;      // 気温：オレンジ
static constexpr uint32_t C_HUM  = 0x3EC5C5;      // 湿度：青緑・ターコイズ
static constexpr uint32_t C_RAIN = 0x4D8DFF;      // 雨量：青
static constexpr uint32_t C_WIND = 0x7DFF9B;      // 風速・風向：明るい緑



// ---- 更新状態の色 ----
static constexpr uint32_t C_OK   = 0x7DFF9B;      // 正常：明るい緑
static constexpr uint32_t C_WARN = 0xFFC94D;      // 注意：黄色・アンバー
static constexpr uint32_t C_BAD  = 0xFF6B6B;      // 警告・異常：赤・コーラル

// ---- 風向コンパスの色 ----
static constexpr uint32_t C_SEG_OFF   = 0x223040;  // 消灯している区画：暗い青灰色
static constexpr uint32_t C_SEG_NORTH = 0x5B6B7A;  // 北の区画：青みのあるグレー
static constexpr uint32_t C_SEG_NEAR  = 0x2C7A4A;  // 風向の両隣：暗い緑


// ============================================================
// Layout
// ============================================================
// ---- Static lines ----
static constexpr int STATIC_HLINE_X = 12;
static constexpr int STATIC_HLINE_Y = 32;
static constexpr int STATIC_HLINE_W = 296;

static constexpr int STATIC_VLINE_X = 180;
static constexpr int STATIC_VLINE_Y = 44;
static constexpr int STATIC_VLINE_H = 186;

// ---- Static labels ----
static constexpr int LABEL_TEMP_X = 12;
static constexpr int LABEL_TEMP_Y = 52;

static constexpr int LABEL_HUM_X = 12;
static constexpr int LABEL_HUM_Y = 148;

static constexpr int LABEL_RAIN_X = 12;
static constexpr int LABEL_RAIN_Y = 192;

static constexpr int LABEL_WIND_X = 192;
static constexpr int LABEL_WIND_Y = 52;

// ---- Temperature ----
static constexpr int TEMP_RIGHT_X = 175;
static constexpr int TEMP_Y = 112;
static constexpr int TEMP_UNIT_GAP = 2;

// Clear
static constexpr int TEMP_CLEAR_X_OFFSET = -170;
static constexpr int TEMP_CLEAR_Y_OFFSET = -57;
static constexpr int TEMP_CLEAR_W = 172;
static constexpr int TEMP_CLEAR_H = 65;

// ---- Humidity ----
static constexpr int HUM_TEXT_X = 166;
static constexpr int HUM_TEXT_Y = 148;

static constexpr int HUM_BAR_X = 12;
static constexpr int HUM_BAR_Y = 155;
static constexpr int HUM_BAR_W = 154;
static constexpr int HUM_BAR_H = 7;

// Clear
static constexpr int HUM_TEXT_CLEAR_X_OFFSET = -61;
static constexpr int HUM_TEXT_CLEAR_Y_OFFSET = -15;
static constexpr int HUM_TEXT_CLEAR_W = 63;
static constexpr int HUM_TEXT_CLEAR_H = 20;

static constexpr int HUM_BAR_CLEAR_X_OFFSET = -2;
static constexpr int HUM_BAR_CLEAR_Y_OFFSET = -2;
static constexpr int HUM_BAR_CLEAR_W = 158;
static constexpr int HUM_BAR_CLEAR_H = 12;

// ---- Rain ----
static constexpr int RAIN_TEXT_X = 166;
static constexpr int RAIN_TEXT_Y = 192;

static constexpr int RAIN_BAR_X = 12;
static constexpr int RAIN_BAR_Y = 199;
static constexpr int RAIN_BAR_W = 154;
static constexpr int RAIN_BAR_H = 7;

// Clear
static constexpr int RAIN_TEXT_CLEAR_X_OFFSET = -66;
static constexpr int RAIN_TEXT_CLEAR_Y_OFFSET = -15;
static constexpr int RAIN_TEXT_CLEAR_W = 68;
static constexpr int RAIN_TEXT_CLEAR_H = 20;

static constexpr int RAIN_BAR_CLEAR_X_OFFSET = -2;
static constexpr int RAIN_BAR_CLEAR_Y_OFFSET = -2;
static constexpr int RAIN_BAR_CLEAR_W = 158;
static constexpr int RAIN_BAR_CLEAR_H = 12;

// ---- Wind ----
static constexpr int WIND_CX = 251;
static constexpr int WIND_CY = 132;

static constexpr int SEG_R_OUT = 51;
static constexpr int SEG_R_IN  = 45;

static constexpr int SEG_COUNT = 16;
static constexpr float SEG_STEP = 22.5f;
static constexpr float SEG_HALF = 9.75f;

// Clear
static constexpr int WIND_CLEAR_MARGIN = 2;

// Wind speed
static constexpr int WIND_SPEED_Y = 142;
static constexpr int WIND_UNIT_Y = 157;

// ---- Wind static labels ----
static constexpr int WIND_N_X = 251;
static constexpr int WIND_N_Y = 74;

static constexpr int WIND_S_X = 251;
static constexpr int WIND_S_Y = 200;

static constexpr int WIND_E_X = 312;
static constexpr int WIND_E_Y = 136;

static constexpr int WIND_W_X = 190;
static constexpr int WIND_W_Y = 136;

// ============================================================
// Text datum
// ============================================================
using Datum = lgfx::textdatum::textdatum_t;

static constexpr Datum DATUM_BL = lgfx::textdatum::baseline_left;
static constexpr Datum DATUM_BC = lgfx::textdatum::baseline_center;
static constexpr Datum DATUM_BR = lgfx::textdatum::baseline_right;

// ============================================================
// Update time
// ============================================================
static constexpr long UPD_OK_MIN = 20;
static constexpr long UPD_WARN_MIN = 40;

uint32_t updateDotColor(long ageMin)
{
  if (ageMin <= UPD_OK_MIN) {
    return C_OK;
  }

  if (ageMin < UPD_WARN_MIN) {
    return C_WARN;
  }

  return C_BAD;
}

// Draw update line
void drawUpdateLine( const struct tm &currentInfo, const struct tm &observationInfo, bool observationValid)
{
  long ageMin = 0;
  uint32_t dotColor = C_BAD;

  char buf[32];

  if (observationValid) {
    struct tm currentCopy = currentInfo;
    struct tm observationCopy = observationInfo;

    time_t currentTime     = mktime(&currentCopy);
    time_t observationTime = mktime(&observationCopy);

    long ageSec = (long)difftime(currentTime, observationTime);

    if (ageSec < 0) {
      ageSec = 0;
    }

    ageMin = ageSec / 60;

    char ageText[8];

    if (ageMin > 99) {
      strcpy(ageText, "99+");
    } else {
      snprintf(ageText, sizeof(ageText), "%ld", ageMin);
    }

    snprintf(buf, sizeof(buf), "%02d:%02d · %s min ago",
             observationInfo.tm_hour, observationInfo.tm_min, ageText);

    dotColor = updateDotColor(ageMin);

  } else {
    strcpy(buf, "--:-- · -- min ago");
    dotColor = C_BAD;
  }

  displayCommonDrawUpdateLine(buf, dotColor);
}

// Update line redraw control
int lastCurrentMinute = -1;
int lastObservationHour = -1;
int lastObservationMinute = -1;
bool lastObservationValid = false;

void tickUpdateLine(const struct tm &currentInfo, const struct tm &observationInfo, bool observationValid)
{
  bool changed = false;

  if (currentInfo.tm_min != lastCurrentMinute) {
    changed = true;
  }

  if (observationInfo.tm_hour != lastObservationHour) {
    changed = true;
  }

  if (observationInfo.tm_min != lastObservationMinute) {
    changed = true;
  }

  if (observationValid != lastObservationValid) {
    changed = true;
  }

  if (!changed) {
    return;
  }

  lastCurrentMinute     = currentInfo.tm_min;
  lastObservationHour   = observationInfo.tm_hour;
  lastObservationMinute = observationInfo.tm_min;
  lastObservationValid  = observationValid;

  drawUpdateLine(currentInfo, observationInfo, observationValid);
}

// ============================================================
// Bar
// ============================================================
void drawBar(int x, int y, int w, int h, float fraction, uint32_t color)
{
  fraction = constrain(fraction, 0.0f, 1.0f);

  tft.fillRoundRect(x, y, w, h, h / 2, C_LINE);

  int fillW = (int)(w * fraction);

  if (fillW > 0) {
    if (fillW < h) {
      fillW = h;
    }
    tft.fillRoundRect(x, y, fillW, h, h / 2, color);
  }
}

// ============================================================
// Static display
// ============================================================
void drawStatic()
{
  tft.fillScreen(C_BG);

  tft.drawFastHLine(STATIC_HLINE_X, STATIC_HLINE_Y, STATIC_HLINE_W, C_LINE);
  tft.drawFastVLine(STATIC_VLINE_X, STATIC_VLINE_Y, STATIC_VLINE_H, C_LINE);

  drawText(Inter_Regular_10_vlw, "TEMP", LABEL_TEMP_X, LABEL_TEMP_Y, C_DIM, DATUM_BL);
  drawText(Inter_Regular_10_vlw, "HUM", LABEL_HUM_X, LABEL_HUM_Y, C_DIM, DATUM_BL);
  drawText(Inter_Regular_10_vlw, "RAIN", LABEL_RAIN_X, LABEL_RAIN_Y, C_DIM, DATUM_BL);
  drawText(Inter_Regular_10_vlw, "WIND", LABEL_WIND_X, LABEL_WIND_Y, C_DIM, DATUM_BL);

  drawWindStatic();
}



// ============================================================
// Temperature
// ============================================================
void drawTemperature(const WeatherData &data)
{
  int unitW = drawText(Inter_Regular_20_vlw, "°C", TEMP_RIGHT_X, TEMP_Y, C_TEMP, DATUM_BR);

  char tempText[16];
  snprintf(tempText, sizeof(tempText), "%.1f", data.temperature);

  int tempX = TEMP_RIGHT_X - unitW - TEMP_UNIT_GAP;
  drawText(Inter_Regular_62_vlw, tempText, tempX, TEMP_Y, C_TEMP, DATUM_BR);
}

// ============================================================
// Humidity
// ============================================================
void drawHumidity(const WeatherData &data)
{
  char humText[16];

  snprintf(humText, sizeof(humText), "%d %%", data.humidity);

  drawText(Inter_Regular_16_vlw, humText, HUM_TEXT_X, HUM_TEXT_Y, C_TEXT, DATUM_BR);
  drawBar(HUM_BAR_X, HUM_BAR_Y, HUM_BAR_W, HUM_BAR_H, data.humidity / 100.0f, C_HUM);
}

// ============================================================
// Rain
// ============================================================
void drawRain(const WeatherData &data)
{
  char rainText[16];

  snprintf(rainText, sizeof(rainText), "%.1f mm", data.precipitation10m);

  drawText(Inter_Regular_16_vlw, rainText, RAIN_TEXT_X, RAIN_TEXT_Y, C_TEXT, DATUM_BR);
  drawBar(RAIN_BAR_X, RAIN_BAR_Y, RAIN_BAR_W, RAIN_BAR_H, data.precipitation10m / 10.0f, C_RAIN);
}

// ============================================================
// Wind static
// ============================================================
void drawWindStatic()
{
  drawText(Inter_Regular_10_vlw, "N", WIND_N_X, WIND_N_Y, C_SUB, DATUM_BC);
  drawText(Inter_Regular_10_vlw, "S", WIND_S_X, WIND_S_Y, C_DIM, DATUM_BC);
  drawText(Inter_Regular_10_vlw, "E", WIND_E_X, WIND_E_Y, C_DIM, DATUM_BC);
  drawText(Inter_Regular_10_vlw, "W", WIND_W_X, WIND_W_Y, C_DIM, DATUM_BC);
}

// ============================================================
// Wind
// ============================================================
static float compassToArcAngle(float compassDeg)
{
  // コンパスは12時が0度、fillArcは3時が0度なのでその変換を行う
  float a = compassDeg - 90.0f;
  a = fmodf(a, 360.0f);

  if (a < 0.0f) {
    a += 360.0f;
  }

  return a;
}

static void fillRingSegment(float a0, float a1, uint32_t color)
{
  float s = compassToArcAngle(a0);
  float e = compassToArcAngle(a1);

  if (s <= e) {
    tft.fillArc(WIND_CX, WIND_CY, SEG_R_OUT, SEG_R_IN, s, e, color);
  } else {
    tft.fillArc(WIND_CX, WIND_CY, SEG_R_OUT, SEG_R_IN, s, 360.0f, color);
    tft.fillArc(WIND_CX, WIND_CY, SEG_R_OUT, SEG_R_IN, 0.0f, e, color);
  }
}

static int windSegmentIndex(float dirDeg)
{
  float d = fmodf(dirDeg, 360.0f);

  if (d < 0.0f) {
    d += 360.0f;
  }

  return ((int)lroundf( d / SEG_STEP)) % SEG_COUNT;
}

static uint32_t windSegmentColor(int k, int active, bool calm)
{
  if (!calm) {
    if (k == active) {
      return C_WIND;
    }

    int d = (k - active + SEG_COUNT) % SEG_COUNT;

    if ( d == 1 || d == SEG_COUNT - 1) {
      return C_SEG_NEAR;
    }
  }

  return (k == 0) ? C_SEG_NORTH : C_SEG_OFF;
}

void drawWind(const WeatherData &data)
{
  int active = windSegmentIndex(data.windDirectionDeg);

  for (int k = 0; k < SEG_COUNT; k++) {
    float center = k * SEG_STEP;
    fillRingSegment(center - SEG_HALF, center + SEG_HALF, windSegmentColor(k, active, data.calm));
  }

  char speedText[16];
  snprintf(speedText, sizeof(speedText), "%.1f", data.windSpeed);

  drawText(Inter_Regular_30_vlw, speedText, WIND_CX, WIND_SPEED_Y, C_TEXT, DATUM_BC);
  drawText(Inter_Regular_10_vlw, "m/s",     WIND_CX, WIND_UNIT_Y,  C_DIM,  DATUM_BC);
}

// ============================================================
// Clear functions
// ============================================================
void clearTemperature()
{
  tft.fillRect(TEMP_RIGHT_X + TEMP_CLEAR_X_OFFSET, TEMP_Y + TEMP_CLEAR_Y_OFFSET, TEMP_CLEAR_W, TEMP_CLEAR_H, C_BG);
}

void clearHumidity()
{
  tft.fillRect(HUM_TEXT_X + HUM_TEXT_CLEAR_X_OFFSET, HUM_TEXT_Y + HUM_TEXT_CLEAR_Y_OFFSET, HUM_TEXT_CLEAR_W, HUM_TEXT_CLEAR_H, C_BG);
  tft.fillRect(HUM_BAR_X +  HUM_BAR_CLEAR_X_OFFSET,  HUM_BAR_Y +  HUM_BAR_CLEAR_Y_OFFSET,  HUM_BAR_CLEAR_W,  HUM_BAR_CLEAR_H,  C_BG);
}

void clearRain()
{
  tft.fillRect(RAIN_TEXT_X + RAIN_TEXT_CLEAR_X_OFFSET, RAIN_TEXT_Y + RAIN_TEXT_CLEAR_Y_OFFSET, RAIN_TEXT_CLEAR_W, RAIN_TEXT_CLEAR_H, C_BG);
  tft.fillRect(RAIN_BAR_X +  RAIN_BAR_CLEAR_X_OFFSET,  RAIN_BAR_Y +  RAIN_BAR_CLEAR_Y_OFFSET,  RAIN_BAR_CLEAR_W,  RAIN_BAR_CLEAR_H,  C_BG);
}

void clearWind()
{
  tft.fillCircle(WIND_CX, WIND_CY, SEG_R_OUT + WIND_CLEAR_MARGIN, C_BG);
}

// ============================================================
// Draw weather
// ============================================================
void drawWeather(const WeatherData &data)
{
  drawTemperature(data);
  drawHumidity(data);
  drawRain(data);
  drawWind(data);
}

// ============================================================
// Update weather
// ============================================================
void updateWeather(const WeatherData &data)
{
  clearTemperature();
  clearHumidity();
  clearRain();
  clearWind();

  drawWeather(data);
}

// ============================================================
// Display Update Weather
// ============================================================
void amedasDisplayUpdateWeather(const WeatherData &data)
{
  updateWeather(data);
}

// ============================================================
// Display Tick
// ============================================================
void amedasDisplayTick(const struct tm &currentInfo, const WeatherData &data)
{
  displayCommonTickDateTime(currentInfo);

  tickUpdateLine(
    currentInfo,
    data.observationInfo,
    data.observationValid
  );
}

// ============================================================
// Show AMeDAS display
// ============================================================
void amedasDisplayShow(const struct tm &currentInfo, const WeatherData &data)
{
  // 画面全体を初期描画
  drawStatic();

  // 最新の観測データをすべて描画
  drawWeather(data);

  // 差分更新の状態をリセット
  displayCommonResetDateTime();

  lastCurrentMinute     = -1;
  lastObservationHour   = -1;
  lastObservationMinute = -1;
  lastObservationValid  = false;
}
