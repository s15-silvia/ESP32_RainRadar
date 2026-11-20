#include <time.h>
#include <stdio.h>

#include "Fonts.h"
#include "DisplayCommon.h"
#include "DisplayHardware.h"

// ============================================================
// Date and time layout
// ============================================================
static constexpr uint32_t C_BG   = 0x0B0F14;      // 背景：ほぼ黒の濃い青
static constexpr uint32_t C_DIM  = 0x5B6B7A;      // 暗めの補助文字：青みのあるグレー
static constexpr uint32_t C_SUB  = 0x8FA0B0;      // 明るめの補助文字：青みのあるライトグレー

static constexpr uint32_t C_SAT = 0x4D8DFF;       // 土曜日：青
static constexpr uint32_t C_SUN = 0xFF6B6B;       // 日曜日：赤・コーラル

static constexpr int DATE_X = 12;
static constexpr int DATE_Y = 23;
static constexpr int DATE_CLEAR_X = 8;
static constexpr int DATE_CLEAR_Y = 8;
static constexpr int DATE_CLEAR_W = 115;
static constexpr int DATE_CLEAR_H = 20;

static constexpr int TIME_X = 308;
static constexpr int TIME_Y = 23;
static constexpr int TIME_CLEAR_X = 245;
static constexpr int TIME_CLEAR_Y = 8;
static constexpr int TIME_CLEAR_W = 68;
static constexpr int TIME_CLEAR_H = 20;

using Datum = lgfx::textdatum::textdatum_t;

static constexpr Datum DATUM_BL = lgfx::textdatum::baseline_left;
static constexpr Datum DATUM_BR = lgfx::textdatum::baseline_right;

// ============================================================
// Text
// ============================================================
int drawText(
    const uint8_t *font,
    const char *str,
    int x,
    int y,
    uint32_t color,
    lgfx::textdatum::textdatum_t datum
)
{
    tft.unloadFont();
    tft.loadFont(font);

    tft.setTextDatum(datum);
    tft.setTextColor(color, C_BG);

    int textW = tft.textWidth(str);

    tft.drawString(str, x, y);

    return textW;
}

// ============================================================
// Update line
// ============================================================
static constexpr int UPD_TEXT_RIGHT = 298;
static constexpr int UPD_BASE_Y = 234;

static constexpr int UPD_DOT_X = 305;
static constexpr int UPD_DOT_Y = 230;
static constexpr int UPD_DOT_R = 3;

static constexpr int UPD_CLEAR_X = 108;
static constexpr int UPD_CLEAR_Y = 218;
static constexpr int UPD_CLEAR_W = 200;
static constexpr int UPD_CLEAR_H = 20;

void displayCommonDrawUpdateLine(const char *text, uint32_t dotColor)
{
    tft.fillRect(
        UPD_CLEAR_X, UPD_CLEAR_Y,
        UPD_CLEAR_W, UPD_CLEAR_H, C_BG
    );

    drawText(
        Inter_Regular_10_vlw,
        text,
        UPD_TEXT_RIGHT,
        UPD_BASE_Y,
        C_DIM,
        DATUM_BR
    );

    tft.fillCircle(
        UPD_DOT_X,
        UPD_DOT_Y,
        UPD_DOT_R,
        dotColor
    );
}

// ============================================================
// Date
// ============================================================
static int lastDateYear = -1;
static int lastDateMonth = -1;
static int lastDateDay = -1;
static int lastDateWeekday = -1;

static void clearDate()
{
    tft.fillRect(
        DATE_CLEAR_X, DATE_CLEAR_Y,
        DATE_CLEAR_W, DATE_CLEAR_H, C_BG
    );
}

static void drawDate(const struct tm &currentInfo)
{
    char dateText[16];

    snprintf(
        dateText, sizeof(dateText),
        "%d/%d", currentInfo.tm_mon + 1, currentInfo.tm_mday
    );

    int dateW = drawText(
        Inter_Regular_18_vlw, dateText,
        DATE_X, DATE_Y, C_SUB, DATUM_BL
    );

    static const char *weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };

    int weekday = currentInfo.tm_wday;

    if (weekday < 0 || weekday > 6) {
        return;
    }

    uint32_t weekdayColor = C_SUB;

    if (weekday == 0) {
        weekdayColor = C_SUN;
    } else if (weekday == 6) {
        weekdayColor = C_SAT;
    }

    drawText(
        Inter_Regular_13_vlw, weekdays[weekday],
        DATE_X + dateW + 6, DATE_Y, weekdayColor, DATUM_BL
    );
}

static void tickDate(const struct tm &currentInfo)
{
    if (currentInfo.tm_year == lastDateYear &&
        currentInfo.tm_mon == lastDateMonth &&
        currentInfo.tm_mday == lastDateDay &&
        currentInfo.tm_wday == lastDateWeekday) {
        return;
    }

    lastDateYear = currentInfo.tm_year;
    lastDateMonth = currentInfo.tm_mon;
    lastDateDay = currentInfo.tm_mday;
    lastDateWeekday = currentInfo.tm_wday;

    clearDate();
    drawDate(currentInfo);
}

// ============================================================
// Time
// ============================================================
static int lastTimeHour = -1;
static int lastTimeMinute = -1;

static void clearTime()
{
    tft.fillRect(
        TIME_CLEAR_X, TIME_CLEAR_Y,
        TIME_CLEAR_W, TIME_CLEAR_H, C_BG
    );
}

static void drawTime(const struct tm &currentInfo)
{
    char timeText[8];

    snprintf(
        timeText, sizeof(timeText),
        "%02d:%02d", currentInfo.tm_hour, currentInfo.tm_min
    );

    drawText(
        Inter_Regular_18_vlw, timeText,
        TIME_X, TIME_Y, C_SUB, DATUM_BR
    );
}

static void tickTime(const struct tm &currentInfo)
{
    if (currentInfo.tm_hour == lastTimeHour &&
        currentInfo.tm_min == lastTimeMinute) {
        return;
    }

    lastTimeHour = currentInfo.tm_hour;
    lastTimeMinute = currentInfo.tm_min;

    clearTime();
    drawTime(currentInfo);
}

// ============================================================
// Common date and time update
// ============================================================
void displayCommonResetDateTime()
{
    lastDateYear = -1;
    lastDateMonth = -1;
    lastDateDay = -1;
    lastDateWeekday = -1;

    lastTimeHour = -1;
    lastTimeMinute = -1;
}

void displayCommonTickDateTime(const struct tm &currentInfo)
{
    tickDate(currentInfo);
    tickTime(currentInfo);
}

