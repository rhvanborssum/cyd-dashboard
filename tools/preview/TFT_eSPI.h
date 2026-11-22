#pragma once
// PC stand-in for TFT_eSPI: just the sprite drawing calls the scenes use,
// rendering into memory so tools/preview.py can save PNGs. Shapes follow
// TFT_eSPI closely; anti-aliased lines and font 2 are approximations.
#include <Arduino.h>

#include <vector>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

extern uint16_t g_screen[240 * 320];

class TFT_eSPI {
 public:
  virtual ~TFT_eSPI() {}
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI *) {}
  void *setColorDepth(int8_t) { return nullptr; }
  void *createSprite(int16_t w, int16_t h, uint8_t = 1);
  void deleteSprite() { buf_.clear(); }
  void *getPointer() { return buf_.data(); }
  void pushSprite(int32_t x, int32_t y);

  void drawPixel(int32_t x, int32_t y, uint32_t c);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c);
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t bg = 0x00FFFFFF);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c);
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c);
  void fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t c);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c);

  void setTextFont(uint8_t) { fontReady_ = true; }
  void setTextColor(uint16_t c) { text_ = c; }
  void setTextColor(uint16_t c, uint16_t) { text_ = c; }
  int16_t drawString(const char *s, int32_t x, int32_t y, uint8_t font);
  int16_t drawRightString(const char *s, int32_t x, int32_t y, uint8_t font);
  int16_t drawCentreString(const char *s, int32_t x, int32_t y, uint8_t font);
  int16_t textWidth(const char *s, uint8_t font);

 private:
  bool inRoundRect(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r);
  int w_ = 0, h_ = 0;
  uint16_t text_ = TFT_WHITE;
  bool fontReady_ = false;  // mirrors the board: font 1 crashes until setTextFont()
  std::vector<uint16_t> buf_;
};
