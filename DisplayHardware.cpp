#include "DisplayHardware.h"

LGFX tft;

void displayHardwareBegin()
{
  tft.init();
  tft.setSwapBytes(true);
  tft.setRotation(1);
}
