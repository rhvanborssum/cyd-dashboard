#pragma once
// Bits shared by the animated scenes (fish tank, corgi yard).
#include <TFT_eSPI.h>

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Black buckled pilgrim hat. (hx, by) = brim centre, already in sprite coords.
void drawPilgrimHat(TFT_eSprite *spr, int hx, int by, float scale);
