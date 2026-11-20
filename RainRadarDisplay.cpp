#include <Arduino.h>

#include "RainRadarDisplay.h"
#include "DisplayHardware.h"

// Display settings
static constexpr int TFT_WIDTH  = 320;
static constexpr int TFT_HEIGHT = 240;

static uint16_t rgb332To565(uint8_t value)
{
  const uint8_t r = (value >> 5) & 0x07;
  const uint8_t g = (value >> 2) & 0x07;
  const uint8_t b = value & 0x03;

  const uint16_t r5 = (r << 2) | (r >> 1);
  const uint16_t g6 = (g << 3) | g;
  const uint16_t b5 = (b << 3) | (b << 1) | (b >> 1);

  return static_cast<uint16_t>( (r5 << 11) | (g6 << 5) | b5 );
}

void rainRadarDisplayShow(const uint8_t* image, int width, int height, int displayY)
{
  if (image == nullptr) {
    Serial.println("Display error: image buffer is null.");
    return;
  }

  if (width != TFT_WIDTH ||
      height <= 0 ||
      displayY < 0 ||
      displayY + height > TFT_HEIGHT) {
    Serial.println("Display error: invalid image dimensions or position.");
    return;
  }

  // One RGB565 scanline: 320 * 2 = 640 bytes.
  uint16_t lineBuffer[TFT_WIDTH];

  tft.startWrite();

  for (int y = 0; y < height; ++y) {
    const size_t rowOffset = static_cast<size_t>(y) * width;

    for (int x = 0; x < width; ++x) {
      lineBuffer[x] = rgb332To565(image[rowOffset + x]);
    }
    tft.pushImage(0, displayY + y, width, 1, lineBuffer);
  }

  tft.endWrite();

  Serial.println("Rain radar composite displayed.");
}
