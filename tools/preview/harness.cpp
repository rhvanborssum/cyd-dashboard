// PC preview harness: runs the real scene code (src/aquarium.cpp, src/corgis.cpp)
// against an in-memory display and dumps chosen frames as raw RGB565, in the
// same format the board's serial screenshot uses. Driven by tools/preview.py.
//
// preview.exe <fish|corgis> <season a|n|t|c> <YYYY-MM-DD> <HH:MM> <seed> <out-prefix>
//             <frame,frame,...> [tap=frame:x:y ...]
#include <Arduino.h>
#include <TFT_eSPI.h>

#include <string>
#include <vector>

#include "aquarium.h"
#include "corgis.h"
#include "season.h"

#define PROGMEM
#include "glcdfont.c"

uint16_t g_screen[240 * 320];
FakeSerial Serial;
FakeESP ESP;

static uint32_t g_ms = 1000;
static uint32_t g_rng = 2463534242u;
uint32_t millis() { return g_ms; }
uint32_t esp_random() {  // xorshift32: repeatable "random" for a given seed
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

// ---------- TFT_eSprite stand-in ----------
void *TFT_eSprite::createSprite(int16_t w, int16_t h, uint8_t) {
  w_ = w;
  h_ = h;
  buf_.assign((size_t)w * h, 0);
  return buf_.data();
}

void TFT_eSprite::pushSprite(int32_t x, int32_t y) {
  for (int r = 0; r < h_; r++)
    for (int c = 0; c < w_; c++)
      if (y + r >= 0 && y + r < 240 && x + c >= 0 && x + c < 320) g_screen[(y + r) * 320 + x + c] = buf_[r * w_ + c];
}

void TFT_eSprite::drawPixel(int32_t x, int32_t y, uint32_t c) {
  if (x >= 0 && y >= 0 && x < w_ && y < h_) buf_[y * w_ + x] = (uint16_t)c;
}
void TFT_eSprite::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) {
  for (int i = 0; i < w; i++) drawPixel(x + i, y, c);
}
void TFT_eSprite::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) {
  for (int i = 0; i < h; i++) drawPixel(x, y + i, c);
}
void TFT_eSprite::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  for (int j = 0; j < h; j++) drawFastHLine(x, y + j, w, c);
}
void TFT_eSprite::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
  drawFastHLine(x, y, w, c);
  drawFastHLine(x, y + h - 1, w, c);
  drawFastVLine(x, y, h, c);
  drawFastVLine(x + w - 1, y, h, c);
}
void TFT_eSprite::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    drawPixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void TFT_eSprite::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg, uint32_t) {
  float r = wd / 2, vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
  for (int y = (int)floorf(min(ay, by) - r); y <= (int)ceilf(max(ay, by) + r); y++)
    for (int x = (int)floorf(min(ax, bx) - r); x <= (int)ceilf(max(ax, bx) + r); x++) {
      float t = len2 > 0 ? ((x - ax) * vx + (y - ay) * vy) / len2 : 0;
      t = constrain(t, 0.0f, 1.0f);
      float dx = x - (ax + t * vx), dy = y - (ay + t * vy);
      if (dx * dx + dy * dy <= r * r) drawPixel(x, y, fg);
    }
}
bool TFT_eSprite::inRoundRect(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  int cx = px < x + r ? x + r : (px >= x + w - r ? x + w - r - 1 : px);
  int cy = py < y + r ? y + r : (py >= y + h - r ? y + h - r - 1 : py);
  return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
}
void TFT_eSprite::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  for (int py = y; py < y + h; py++)
    for (int px = x; px < x + w; px++)
      if (inRoundRect(px, py, x, y, w, h, r)) drawPixel(px, py, c);
}
void TFT_eSprite::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
  for (int py = y; py < y + h; py++)
    for (int px = x; px < x + w; px++)
      if (inRoundRect(px, py, x, y, w, h, r) && !inRoundRect(px, py, x + 1, y + 1, w - 2, h - 2, max(0, r - 1)))
        drawPixel(px, py, c);
}
void TFT_eSprite::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    drawFastHLine(x0 - dx, y0 + dy, 2 * dx + 1, c);
  }
}
void TFT_eSprite::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
    for (auto &p : pts) drawPixel(x0 + p[0], y0 + p[1], c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}
void TFT_eSprite::fillEllipse(int16_t x0, int16_t y0, int32_t rx, int32_t ry, uint16_t c) {
  if (rx < 1 || ry < 1) return;
  for (int dy = -ry; dy <= ry; dy++) {
    float f = 1.0f - (float)(dy * dy) / (ry * ry);
    int dx = (int)(rx * sqrtf(max(0.0f, f)));
    drawFastHLine(x0 - dx, y0 + dy, 2 * dx + 1, c);
  }
}
void TFT_eSprite::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
  auto edge = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
  int minX = min(x0, min(x1, x2)), maxX = max(x0, max(x1, x2));
  int minY = min(y0, min(y1, y2)), maxY = max(y0, max(y1, y2));
  for (int y = minY; y <= maxY; y++)
    for (int x = minX; x <= maxX; x++) {
      int a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y), d = edge(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
    }
}

// Font 1 is TFT_eSPI's real 5x7 font; font 2 is approximated with it.
int16_t TFT_eSprite::textWidth(const char *s, uint8_t font) { return (int16_t)strlen(s) * (font == 2 ? 7 : 6); }
int16_t TFT_eSprite::drawString(const char *s, int32_t x, int32_t y, uint8_t font) {
  if (font == 1 && !fontReady_) {  // on the board this reads a garbage pointer and crashes
    fprintf(stderr, "PREVIEW ERROR: font 1 used on a sprite before setTextFont() - this crashes the board\n");
    exit(3);
  }
  int adv = font == 2 ? 7 : 6, yo = font == 2 ? 4 : 0;
  for (int i = 0; s[i]; i++) {
    const unsigned char *g = &::font[(unsigned char)s[i] * 5];
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 8; row++)
        if (g[col] & (1 << row)) drawPixel(x + i * adv + col, y + yo + row, text_);
  }
  return textWidth(s, font);
}
int16_t TFT_eSprite::drawRightString(const char *s, int32_t x, int32_t y, uint8_t font) {
  return drawString(s, x - textWidth(s, font), y, font);
}
int16_t TFT_eSprite::drawCentreString(const char *s, int32_t x, int32_t y, uint8_t font) {
  return drawString(s, x - textWidth(s, font) / 2, y, font);
}

// ---------- Driver ----------
int main(int argc, char **argv) {
  if (argc < 8) {
    fprintf(stderr, "usage: preview <fish|corgis> <season a|n|t|c> <YYYY-MM-DD> <HH:MM> <seed> <out-prefix> <frames> [tap=f:x:y ...]\n");
    return 2;
  }
  std::string scene = argv[1];
  char season = argv[2][0];
  int Y, Mo, D, h, mi;
  sscanf(argv[3], "%d-%d-%d", &Y, &Mo, &D);
  sscanf(argv[4], "%d:%d", &h, &mi);
  g_rng = (uint32_t)strtoul(argv[5], nullptr, 10) * 2654435761u + 1;
  std::string prefix = argv[6];
  std::vector<int> shots;
  for (char *p = argv[7]; *p;) {
    shots.push_back((int)strtol(p, &p, 10));
    if (*p == ',') p++;
  }
  struct Tap { int frame, x, y; };
  std::vector<Tap> taps;
  for (int i = 8; i < argc; i++) {
    Tap t;
    if (sscanf(argv[i], "tap=%d:%d:%d", &t.frame, &t.x, &t.y) == 3) taps.push_back(t);
  }

  tm t = {};
  t.tm_year = Y - 1900;
  t.tm_mon = Mo - 1;
  t.tm_mday = D;
  t.tm_hour = h;
  t.tm_min = mi;
  mktime(&t);
  setSeasonOverride(season == 'a' ? -1 : season == 'n' ? SEASON_NONE : season == 't' ? SEASON_THANKSGIVING : SEASON_CHRISTMAS);

  bool fish = scene == "fish";
  TFT_eSPI tft;
  if (!(fish ? aquariumBegin(tft) : corgiBegin(tft))) return 1;
  int last = 0;
  for (int s : shots) last = max(last, s);
  for (int f = 1; f <= last; f++) {
    g_ms += 45;  // ~22 fps, like the board
    for (auto &tp : taps)
      if (tp.frame == f) fish ? aquariumTap(tp.x, tp.y) : corgiTap(tp.x, tp.y);
    fish ? aquariumFrame(t) : corgiFrame(t);
    for (int s : shots) {
      if (s != f) continue;
      std::string path = prefix + "_" + std::to_string(f) + ".raw";
      FILE *out = fopen(path.c_str(), "wb");
      for (int i = 0; i < 240 * 320; i++) {  // big-endian, matching the board's capture
        unsigned char be[2] = {(unsigned char)(g_screen[i] >> 8), (unsigned char)(g_screen[i] & 0xFF)};
        fwrite(be, 1, 2, out);
      }
      fclose(out);
    }
  }
  return 0;
}
