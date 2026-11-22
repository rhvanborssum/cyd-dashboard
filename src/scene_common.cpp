#include "scene_common.h"

#include <Arduino.h>

void drawPilgrimHat(TFT_eSprite *spr, int hx, int by, float s) {
  uint16_t black = rgb(28, 26, 30), band = rgb(70, 65, 70), gold = rgb(240, 195, 60);
  int brimW = 20 * s, crownW = 12 * s, crownH = 13 * s;
  // Crown tapers slightly toward the top
  for (int r = 0; r < crownH; r++) {
    int w = crownW - (r * 3 * s) / crownH;
    spr->drawFastHLine(hx - w / 2, by - 2 - r, w, black);
  }
  spr->fillRect(hx - brimW / 2, by - 2, brimW, max(2.0f, 3 * s), black);
  int bandH = max(2.0f, 3 * s);
  spr->fillRect(hx - crownW / 2, by - 4 - bandH, crownW, bandH, band);
  int bk = max(3.0f, 5 * s);
  spr->drawRect(hx - bk / 2, by - 4 - bandH - 1, bk, bandH + 2, gold);
  spr->drawRect(hx - bk / 2 + 1, by - 4 - bandH, bk - 2, bandH, gold);
}
