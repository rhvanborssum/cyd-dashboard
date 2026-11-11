#include "aquarium.h"
#include <Arduino.h>

namespace {

constexpr int W = 320, H = 240;
constexpr int BAND_H = 40;  // 320x40x16bit = 25.6 KB strip buffer
constexpr int N_FISH = 7, N_BUBBLES = 40, N_FOOD = 24, N_WEED = 8, N_PEBBLES = 45;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

TFT_eSPI *tft = nullptr;
TFT_eSprite *spr = nullptr;
int bandTop = 0;  // screen y of the band currently being rendered
uint32_t lastMs = 0;
float simT = 0;

uint16_t waterRow[H];
int sandTop[W];

float frand(float lo, float hi) { return lo + (hi - lo) * (esp_random() / 4294967295.0f); }

// True if the vertical span [y0, y1] touches the current band
inline bool inBand(float y0, float y1) { return y1 >= bandTop && y0 < bandTop + BAND_H; }
inline int Y(float y) { return (int)y - bandTop; }

// ---------- Scene objects ----------
struct Fish {
  float x, y, vx, targetY, phase, size, facing;
  uint8_t kind;  // 0 = classic, 1 = angelfish, 2 = neon tetra
  uint16_t body, belly, fin;
};
Fish fish[N_FISH];

struct Bubble { float x, y, r, phase; bool alive; };
Bubble bubbles[N_BUBBLES];

struct Food { float x, y, age; bool alive; };
Food food[N_FOOD];

struct Weed { int x, height; float phase; uint16_t col; };
Weed weeds[N_WEED];

struct Pebble { int x, y, r; uint16_t col; };
Pebble pebbles[N_PEBBLES];

const float CHEST_X = 262, CHEST_Y = 194;
float nextBurst = 2, burstEnd = 0, nextBubble = 0;

// ---------- Seasons ----------
int seasonOverride = -1;
Season season = SEASON_NONE;
bool captureNext = false;
int largestFish = 0;  // wears the pilgrim hat

struct Diver { float x, y, vx, targetY, phase, facing, nextBubble; };
Diver santa;

// 4th Thursday of November
int thanksgivingDay(int year) {
  tm nov1 = {};
  nov1.tm_year = year - 1900;
  nov1.tm_mon = 10;
  nov1.tm_mday = 1;
  nov1.tm_hour = 12;
  mktime(&nov1);  // fills in tm_wday
  int firstThu = 1 + (4 - nov1.tm_wday + 7) % 7;
  return firstThu + 21;
}

// Mirror an x offset for something facing left (-1) or right (+1)
inline int MX(float x, float dir, float ox) { return x + dir * ox; }
// Rect given in facing-relative offsets [ox0, ox1] -> screen rect
void fillRectD(float x, float dir, float ox0, float ox1, int y, int h, uint16_t c) {
  int a = MX(x, dir, ox0), b = MX(x, dir, ox1);
  spr->fillRect(min(a, b), y, abs(b - a) + 1, h, c);
}

void spawnBubble(float x, float y, float r) {
  for (auto &b : bubbles)
    if (!b.alive) { b = {x, y, r, frand(0, 6.28f), true}; return; }
}

void initScene() {
  for (int y = 0; y < H; y++) {  // water: bright near surface, deep blue below
    float k = (float)y / H;
    waterRow[y] = rgb(20 + (1 - k) * 30, 60 + (1 - k) * 90, 110 + (1 - k) * 90);
  }
  for (int x = 0; x < W; x++)
    sandTop[x] = 214 + sinf(x * 0.035f) * 3 + sinf(x * 0.11f + 1) * 1.5f;

  const uint16_t pebbleCols[] = {rgb(150, 130, 100), rgb(120, 110, 95), rgb(180, 160, 120), rgb(95, 90, 85)};
  for (auto &p : pebbles) {
    p.x = frand(0, W);
    p.y = frand(sandTop[p.x] + 4, H - 2);
    p.r = frand(1, 3);
    p.col = pebbleCols[esp_random() % 4];
  }

  const int weedX[N_WEED] = {18, 34, 70, 120, 186, 214, 300, 312};
  const uint16_t weedCols[] = {rgb(40, 160, 70), rgb(70, 190, 60), rgb(30, 130, 90)};
  for (int i = 0; i < N_WEED; i++)
    weeds[i] = {weedX[i], (int)frand(45, 120), frand(0, 6.28f), weedCols[i % 3]};

  struct Palette { uint16_t body, belly, fin; uint8_t kind; float size; };
  const Palette pals[N_FISH] = {
      {rgb(255, 140, 30), rgb(255, 210, 120), rgb(255, 90, 20), 0, 16},   // goldfish
      {rgb(255, 230, 60), rgb(255, 250, 180), rgb(240, 180, 30), 1, 15},  // yellow angel
      {rgb(240, 80, 90), rgb(255, 170, 170), rgb(200, 40, 60), 0, 12},    // red
      {rgb(60, 120, 230), rgb(170, 210, 255), rgb(40, 80, 200), 0, 14},   // blue
      {rgb(30, 200, 230), rgb(230, 50, 60), rgb(60, 90, 160), 2, 8},      // neon tetra
      {rgb(30, 200, 230), rgb(230, 50, 60), rgb(60, 90, 160), 2, 8},      // neon tetra
      {rgb(230, 230, 240), rgb(255, 255, 255), rgb(60, 60, 70), 1, 18},   // silver angel
  };
  for (int i = 0; i < N_FISH; i++) {
    Fish &f = fish[i];
    f.size = pals[i].size;
    f.kind = pals[i].kind;
    f.body = pals[i].body; f.belly = pals[i].belly; f.fin = pals[i].fin;
    f.x = frand(40, W - 40);
    f.y = f.targetY = frand(40, 190);
    f.vx = frand(18, 40) * (esp_random() & 1 ? 1 : -1);
    f.facing = f.vx > 0 ? 1 : -1;
    f.phase = frand(0, 6.28f);
    if (f.size > fish[largestFish].size) largestFish = i;
  }
  float sy = frand(70, 160), sv = esp_random() & 1 ? 16 : -16;  // start in view, so he's not missed
  santa = {frand(70, 230), sy, sv, sy, 0, sv > 0 ? 1.0f : -1.0f, 1};
  for (auto &b : bubbles) b.alive = false;
  for (auto &f : food) f.alive = false;
}

// ---------- Simulation ----------
void update(float dt) {
  simT += dt;

  for (auto &f : fish) {
    // Chase the nearest food flake, otherwise wander
    Food *best = nullptr;
    float bestD = 1e9;
    for (auto &fd : food) {
      if (!fd.alive) continue;
      float d = fabsf(fd.x - f.x) + fabsf(fd.y - f.y);
      if (d < bestD) { bestD = d; best = &fd; }
    }
    float speed = fabsf(f.vx);
    if (best) {
      float dir = best->x > f.x ? 1 : -1;
      f.vx += (dir * max(speed, 45.0f) - f.vx) * min(1.0f, dt * 3);
      f.targetY = best->y;
      if (bestD < f.size * 0.9f) best->alive = false;  // nom
    } else {
      if (frand(0, 1) < dt * 0.25f) f.targetY = frand(30, 200);
      if (frand(0, 1) < dt * 0.15f) f.vx = (f.vx > 0 ? 1 : -1) * frand(15, 40);
      if (frand(0, 1) < dt * 0.05f) f.vx = -f.vx;  // change of mind
    }

    f.x += f.vx * dt;
    float margin = f.size * 1.2f;
    if (f.x < margin && f.vx < 0) f.vx = -f.vx;
    if (f.x > W - margin && f.vx > 0) f.vx = -f.vx;

    f.y += (f.targetY - f.y) * min(1.0f, dt * 0.9f);
    f.y += sinf(f.phase * 0.5f) * 0.15f;
    f.y = constrain(f.y, 20.0f, 200.0f);

    // Animate turning: 'facing' eases between -1 and 1, squashing the body
    float want = f.vx > 0 ? 1 : -1;
    f.facing += (want - f.facing) * min(1.0f, dt * 5);
    f.phase += dt * (3 + fabsf(f.vx) * 0.08f);

    if (frand(0, 1) < dt * 0.08f) spawnBubble(f.x + f.facing * f.size, f.y - 2, 1.5f);
  }

  // Treasure chest lets out a burst of bubbles every few seconds
  if (simT > nextBurst) {
    burstEnd = simT + frand(1.0f, 2.0f);
    nextBurst = simT + frand(4.0f, 8.0f);
  }
  if (simT < burstEnd && simT > nextBubble) {
    spawnBubble(CHEST_X + 15 + frand(-3, 3), CHEST_Y - 4, frand(1.5f, 4.0f));
    nextBubble = simT + 0.08f;
  }

  if (season == SEASON_CHRISTMAS) {
    // Santa swims off one side, then comes back the other way
    Diver &d = santa;
    d.x += d.vx * dt;
    if ((d.x > W + 50 && d.vx > 0) || (d.x < -50 && d.vx < 0)) {
      d.vx = -d.vx;
      d.targetY = frand(60, 170);
    }
    d.y += (d.targetY - d.y) * min(1.0f, dt * 0.5f);
    d.phase += dt;
    float want = d.vx > 0 ? 1 : -1;
    d.facing += (want - d.facing) * min(1.0f, dt * 3);
    if (d.phase > d.nextBubble) {
      for (int i = 0; i < 3; i++) spawnBubble(MX(d.x, want, 24) + frand(-2, 2), d.y - 4 - i * 4, frand(1.5f, 3));
      d.nextBubble = d.phase + frand(1.0f, 2.0f);
    }
  }

  for (auto &b : bubbles) {
    if (!b.alive) continue;
    b.y -= (25 + b.r * 9) * dt;
    b.phase += dt * 4;
    b.x += sinf(b.phase) * 0.4f;
    if (b.y < 6) b.alive = false;
  }

  for (auto &fd : food) {
    if (!fd.alive) continue;
    fd.age += dt;
    int floor = sandTop[constrain((int)fd.x, 0, W - 1)] - 1;
    if (fd.y < floor) {
      fd.y += 14 * dt;
      fd.x += sinf(fd.age * 3 + fd.x) * 0.2f;
    }
    if (fd.age > 20) fd.alive = false;
  }
}

// ---------- Drawing (all into the current band) ----------
void drawWater() {
  for (int r = 0; r < BAND_H; r++) spr->drawFastHLine(0, r, W, waterRow[bandTop + r]);
  if (bandTop == 0) {  // shimmering surface line
    for (int x = 0; x < W; x += 2) {
      int y = 4 + sinf(x * 0.06f + simT * 2.2f) * 1.5f + sinf(x * 0.15f - simT * 3) * 1.0f;
      spr->drawFastHLine(x, y, 2, rgb(170, 225, 255));
      spr->drawFastHLine(x, y + 1, 2, rgb(110, 180, 230));
    }
  }
}

void drawWeed(const Weed &w) {
  int base = sandTop[w.x] + 3;
  if (!inBand(base - w.height - 4, base + 4)) return;
  int segs = w.height / 4;
  for (int k = 0; k <= segs; k++) {
    float sway = sinf(simT * 1.4f + w.phase + k * 0.3f) * k * 0.45f;
    float r = 3.2f - 2.2f * k / segs;
    spr->fillCircle(w.x + sway, Y(base - k * 4), r, w.col);
  }
}

void drawRocks() {
  if (!inBand(185, 225)) return;
  spr->fillEllipse(95, Y(212), 26, 14, rgb(90, 95, 105));
  spr->fillEllipse(82, Y(205), 14, 10, rgb(110, 115, 125));
  spr->fillEllipse(112, Y(214), 16, 9, rgb(75, 80, 90));
  spr->fillEllipse(160, Y(216), 12, 7, rgb(100, 100, 110));
}

void drawChest() {
  if (!inBand(CHEST_Y - 14, CHEST_Y + 24)) return;
  int x = CHEST_X, y = Y(CHEST_Y);
  uint16_t wood = rgb(130, 75, 35), dark = rgb(85, 45, 20), gold = rgb(240, 190, 60);
  // Lid propped slightly open, bobbing a touch while it bubbles
  int lift = simT < burstEnd ? 3 : 1;
  spr->fillRect(x, y, 34, 20, wood);
  spr->drawRect(x, y, 34, 20, dark);
  spr->drawFastHLine(x, y + 9, 34, dark);
  spr->fillRoundRect(x - 1, y - 10 - lift, 36, 10, 4, wood);
  spr->drawRoundRect(x - 1, y - 10 - lift, 36, 10, 4, dark);
  spr->fillRect(x + 1, y - lift, 32, lift, rgb(255, 220, 90));  // treasure glint
  spr->fillRect(x + 15, y + 6, 4, 6, gold);
  spr->fillRect(x + 6, y, 3, 20, gold);
  spr->fillRect(x + 25, y, 3, 20, gold);
}

void drawSand() {
  if (!inBand(205, H)) return;
  for (int x = 0; x < W; x++) {
    int top = sandTop[x];
    int y0 = max(top, bandTop), y1 = min(H, bandTop + BAND_H);
    if (y1 <= y0) continue;
    spr->drawFastVLine(x, y0 - bandTop, y1 - y0, rgb(215, 190, 140));
    if (top >= bandTop && top < bandTop + BAND_H) spr->drawPixel(x, top - bandTop, rgb(240, 220, 170));
  }
  for (auto &p : pebbles)
    if (inBand(p.y - p.r, p.y + p.r)) spr->fillCircle(p.x, Y(p.y), p.r, p.col);
}

void drawPilgrimHat(float hx, float baseY, float s) {
  uint16_t black = rgb(28, 26, 30), band = rgb(70, 65, 70), gold = rgb(240, 195, 60);
  int brimW = 20 * s, crownW = 12 * s, crownH = 13 * s;
  int by = Y(baseY);
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

void drawChristmasLights(const Fish &f, float L, float h, float dir, int cx, int cy, int idx) {
  const uint16_t onCol[2] = {rgb(255, 40, 40), rgb(40, 255, 70)};
  const uint16_t offCol[2] = {rgb(90, 20, 20), rgb(20, 80, 30)};
  int n = f.kind == 2 ? 3 : 4;
  int r = f.kind == 2 ? 1 : 2;
  int px[4], py[4];
  for (int i = 0; i < n; i++) {
    float t = -0.6f + 1.2f * i / (n - 1);
    px[i] = cx + dir * t * L;
    py[i] = cy - h * sqrtf(1 - t * t) * 0.85f;
  }
  for (int i = 1; i < n; i++) spr->drawLine(px[i - 1], py[i - 1], px[i], py[i], rgb(20, 60, 25));
  bool phase = ((int)(simT * 2.5f) + idx) & 1;  // the two colours alternate
  for (int i = 0; i < n; i++) {
    bool on = (i & 1) == phase;
    spr->fillCircle(px[i], py[i] + 1, r, on ? onCol[i & 1] : offCol[i & 1]);
    if (on) spr->drawPixel(px[i], py[i], TFT_WHITE);
  }
}

void drawTurkey() {
  const int tx = 176;
  int base = sandTop[tx];
  if (!inBand(base - 30, base + 8)) return;
  uint16_t roast = rgb(185, 105, 40), crisp = rgb(150, 75, 25), shine = rgb(225, 150, 70);
  uint16_t bone = rgb(245, 240, 225), plate = rgb(225, 228, 235), rim = rgb(160, 165, 180);

  // Platter
  spr->fillEllipse(tx, Y(base + 1), 30, 6, rim);
  spr->fillEllipse(tx, Y(base), 27, 5, plate);
  // Drumsticks (behind the body), with bone ends
  for (int side : {-1, 1}) {
    int x0 = tx + side * 14, y0 = Y(base - 9), x1 = tx + side * 24, y1 = Y(base - 20);
    spr->drawWideLine(x0, y0, x1, y1, 8, crisp, crisp);
    spr->fillCircle(x0, y0, 5, crisp);
    spr->fillRect(min(x1, x1 + side * 4), y1 - 2, 4, 3, bone);
    spr->fillCircle(x1 + side * 5, y1 - 3, 2, bone);
    spr->fillCircle(x1 + side * 4, y1 + 1, 2, bone);
  }
  // Body
  spr->fillEllipse(tx, Y(base - 10), 21, 12, roast);
  spr->fillEllipse(tx, Y(base - 5), 20, 6, crisp);
  spr->fillEllipse(tx - 4, Y(base - 14), 12, 5, shine);
  spr->fillEllipse(tx - 7, Y(base - 16), 4, 2, rgb(245, 190, 120));
  // Parsley garnish
  for (int i = 0; i < 5; i++)
    spr->fillCircle(tx - 24 + i * 12, Y(base + 1), 2, rgb(50, 150, 50));
}

void drawSanta() {
  const Diver &d = santa;
  if (!inBand(d.y - 20, d.y + 12) || d.x < -45 || d.x > W + 45) return;
  float dir = d.facing >= 0 ? 1 : -1;
  int x = d.x, y = Y(d.y);
  uint16_t red = rgb(215, 30, 40), white = rgb(250, 250, 250), skin = rgb(245, 200, 170);
  uint16_t black = rgb(20, 20, 25), steel = rgb(165, 170, 180), gold = rgb(240, 195, 60);
  float kick = sinf(d.phase * 6) * 3;

  // Air tank on his back
  fillRectD(x, dir, -12, 6, y - 12, 5, steel);
  spr->fillRect(MX(x, dir, 7) - 1, y - 11, 3, 3, black);
  // Legs + flippers, kicking out of phase
  for (int leg = 0; leg < 2; leg++) {
    int ly = y + (leg ? 2 - kick : -1 + kick);
    fillRectD(x, dir, -26, -9, ly, 4, red);
    spr->fillTriangle(MX(x, dir, -25), ly - 2, MX(x, dir, -25), ly + 5, MX(x, dir, -38), ly + 1 + kick, black);
  }
  // Body, belt, fur trim
  spr->fillEllipse(x, y, 14, 7, red);
  fillRectD(x, dir, -5, -3, y - 7, 15, black);
  fillRectD(x, dir, -5, -3, y - 2, 4, gold);
  fillRectD(x, dir, 10, 12, y - 6, 13, white);
  // Arm reaching forward
  spr->drawWideLine(MX(x, dir, 6), y + 3, MX(x, dir, 17), y + 7, 4, red, red);
  spr->fillCircle(MX(x, dir, 18), y + 7, 2, black);
  // Head, beard, dive mask, regulator
  spr->fillCircle(MX(x, dir, 17), y - 3, 6, skin);
  spr->fillCircle(MX(x, dir, 18), y + 2, 5, white);
  spr->fillCircle(MX(x, dir, 15), y + 3, 4, white);
  fillRectD(x, dir, 15, 23, y - 8, 6, black);
  fillRectD(x, dir, 17, 22, y - 7, 4, rgb(140, 210, 240));
  spr->drawPixel(MX(x, dir, 20), y - 5, black);
  spr->fillCircle(MX(x, dir, 23), y - 1, 2, black);
  // Floppy Santa hat trailing behind
  spr->fillTriangle(MX(x, dir, 11), y - 8, MX(x, dir, 22), y - 9, MX(x, dir, 2), y - 17, red);
  fillRectD(x, dir, 11, 23, y - 10, 3, white);
  spr->fillCircle(MX(x, dir, 1), y - 17, 3, white);
}

void drawFish(const Fish &f, int idx) {
  float h = f.kind == 1 ? f.size * 0.85f : f.kind == 2 ? f.size * 0.38f : f.size * 0.55f;
  float reach = h * (f.kind == 1 ? 1.9f : 1.5f) + 20;  // + room for hats/lights
  if (!inBand(f.y - reach, f.y + reach)) return;

  float dir = f.facing >= 0 ? 1 : -1;
  float L = f.size * max(0.3f, fabsf(f.facing));  // squashes while turning
  int cx = f.x, cy = Y(f.y);
  float wag = sinf(f.phase * 2.2f) * h * 0.35f;

  // Tail
  float bx = cx - dir * L * 0.75f, tx = cx - dir * (L * 0.75f + f.size * 0.6f);
  spr->fillTriangle(bx, cy, tx, cy - h * 0.85f + wag, tx, cy + h * 0.85f + wag, f.fin);

  // Dorsal + belly fins (angelfish get tall sail fins)
  float finH = f.kind == 1 ? h * 1.9f : h * 1.35f;
  spr->fillTriangle(cx - dir * L * 0.35f, cy - h * 0.7f, cx + dir * L * 0.25f, cy - h * 0.8f,
                    cx - dir * L * 0.25f, cy - finH, f.fin);
  if (f.kind == 1)
    spr->fillTriangle(cx - dir * L * 0.35f, cy + h * 0.7f, cx + dir * L * 0.2f, cy + h * 0.8f,
                      cx - dir * L * 0.25f, cy + finH, f.fin);

  // Body
  spr->fillEllipse(cx, cy, L, h, f.body);
  if (f.kind == 2) {  // neon tetra: red belly half, bright stripe
    spr->fillEllipse(cx - dir * L * 0.25f, cy + h * 0.35f, L * 0.6f, h * 0.5f, f.belly);
    spr->drawFastHLine(cx - L * 0.8f, cy - 1, L * 1.6f, rgb(120, 255, 255));
  } else {
    spr->fillEllipse(cx + dir * L * 0.1f, cy + h * 0.4f, L * 0.65f, h * 0.45f, f.belly);
  }
  if (f.kind == 1) {  // angelfish stripes
    for (float s : {-0.25f, 0.2f}) {
      int sx = cx + dir * s * L;
      int half = h * sqrtf(1 - s * s) * 0.9f;
      spr->drawFastVLine(sx, cy - half, half * 2, f.fin);
      spr->drawFastVLine(sx + 1, cy - half, half * 2, f.fin);
    }
  }

  // Eye
  int ex = cx + dir * L * 0.55f, ey = cy - h * 0.2f;
  float er = max(1.5f, f.size * 0.13f);
  spr->fillCircle(ex, ey, er, TFT_WHITE);
  spr->fillCircle(ex + dir * er * 0.3f, ey, max(1.0f, er * 0.55f), TFT_BLACK);

  if (season == SEASON_CHRISTMAS) drawChristmasLights(f, L, h, dir, cx, cy, idx);
  if (season == SEASON_THANKSGIVING && idx == largestFish) {
    float t = 0.3f;  // sits just behind the eye
    drawPilgrimHat(cx + dir * L * t, f.y - h * sqrtf(1 - t * t) * 0.95f + 1, f.size / 18.0f);
  }
}

void drawBubbles() {
  for (auto &b : bubbles) {
    if (!b.alive || !inBand(b.y - b.r, b.y + b.r)) continue;
    spr->drawCircle(b.x, Y(b.y), b.r, rgb(200, 235, 255));
    spr->drawPixel(b.x - b.r * 0.4f, Y(b.y - b.r * 0.4f), TFT_WHITE);
  }
}

void drawFood() {
  for (auto &f : food)
    if (f.alive && inBand(f.y - 2, f.y + 2)) spr->fillRect(f.x, Y(f.y), 2, 2, rgb(200, 110, 40));
}

void drawClockOverlay(const tm &t) {
  if (bandTop != 0) return;
  char buf[12];
  int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
  snprintf(buf, sizeof(buf), "%d:%02d %s", h12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  spr->setTextColor(rgb(225, 245, 255));  // no bg colour = transparent text
  spr->drawRightString(buf, W - 8, 10, 2);
}

}  // namespace

bool aquariumBegin(TFT_eSPI &display) {
  tft = &display;
  if (!spr) {
    spr = new TFT_eSprite(tft);
    spr->setColorDepth(16);
    if (!spr->createSprite(W, BAND_H)) {
      delete spr;
      spr = nullptr;
      return false;
    }
  }
  initScene();
  lastMs = millis();
  simT = 0;
  nextBurst = 1;
  return true;
}

void aquariumEnd() {
  if (spr) {
    spr->deleteSprite();
    delete spr;
    spr = nullptr;
  }
}

void aquariumTap(int x, int y) {
  int dropped = 0;
  for (auto &f : food) {
    if (f.alive) continue;
    f = {x + frand(-12, 12), (float)max(10, y) + frand(-6, 6), 0, true};
    if (++dropped == 6) break;
  }
}

Season aquariumSeason(const tm &t) {
  if (seasonOverride >= 0) return (Season)seasonOverride;
  int tg = thanksgivingDay(t.tm_year + 1900);
  if (t.tm_mon == 10 && t.tm_mday <= tg) return SEASON_THANKSGIVING;  // Nov 1 - Thanksgiving
  if ((t.tm_mon == 10 && t.tm_mday > tg) || (t.tm_mon == 11 && t.tm_mday <= 25))
    return SEASON_CHRISTMAS;  // day after Thanksgiving - Christmas Day
  return SEASON_NONE;
}

const char *seasonName(Season s) {
  return s == SEASON_THANKSGIVING ? "thanksgiving" : s == SEASON_CHRISTMAS ? "christmas" : "none";
}

void aquariumSetSeasonOverride(int s) { seasonOverride = s; }
void aquariumCaptureNextFrame() { captureNext = true; }

void aquariumFrame(const tm &t) {
  if (!spr) return;
  uint32_t now = millis();
  float dt = min(0.1f, (now - lastMs) / 1000.0f);
  season = aquariumSeason(t);
  update(dt);

  bool capture = captureNext;
  captureNext = false;
  if (capture) Serial.printf("\nSHOT %d %d\n", W, H);

  for (bandTop = 0; bandTop < H; bandTop += BAND_H) {
    drawWater();
    drawRocks();
    for (int i = 0; i < N_WEED; i += 2) drawWeed(weeds[i]);  // back row of plants
    drawChest();
    for (int i = 0; i < N_FISH; i++) drawFish(fish[i], i);
    if (season == SEASON_CHRISTMAS) drawSanta();
    for (int i = 1; i < N_WEED; i += 2) drawWeed(weeds[i]);  // front row
    drawBubbles();
    drawFood();
    drawSand();
    if (season == SEASON_THANKSGIVING) drawTurkey();  // sits on top of the sand
    drawClockOverlay(t);
    spr->pushSprite(0, bandTop);
    if (capture) Serial.write((const uint8_t *)spr->getPointer(), W * BAND_H * 2);
  }
  if (capture) {
    Serial.flush();
    Serial.println("\nSHOT END");
    now = millis();  // don't let the slow capture count as simulation time
  }
  lastMs = now;

  static uint32_t fpsStart = 0, frames = 0;
  frames++;
  if (now - fpsStart >= 5000) {
    if (fpsStart) Serial.printf("aquarium: %.1f fps, free heap %u\n", frames * 1000.0f / (now - fpsStart), (unsigned)ESP.getFreeHeap());
    fpsStart = now;
    frames = 0;
  }
}
