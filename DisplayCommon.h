#pragma once

#include <LovyanGFX.hpp>
#include <stdint.h>
#include <time.h>

// 共通文字描画
int drawText(const uint8_t *font, const char *str, int x, int y, uint32_t color, lgfx::textdatum::textdatum_t datum);

// 日付・時刻の共通表示
void displayCommonResetDateTime();
void displayCommonTickDateTime(const struct tm &currentInfo);

// 更新時刻とマーカーの共通描画
void displayCommonDrawUpdateLine(const char *text, uint32_t dotColor);