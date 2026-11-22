#include "corgis.h"

#include <Arduino.h>

#include "scene_common.h"

namespace {

constexpr int W = 320, H = 240;
constexpr int BAND_H = 40;           // 320x40x16bit = 25.6 KB strip buffer
constexpr int S = 2;                 // each sprite pixel is drawn 2x2
constexpr int SW = 26, SH = 17;      // corgi sprite size (sprite pixels)
constexpr int HORIZON = 155;         // top edge of the grass
constexpr int YARD_TOP = 194, YARD_BOTTOM = 230;  // where paws can be
constexpr int N_CORGIS = 3, N_TUFTS = 70, N_CLOUDS = 4, N_STARS = 30, N_LEAVES = 18, N_SNOW = 70;
constexpr int HOUSE_X = 250, HOUSE_W = 56, HOUSE_BASE = 178;

TFT_eSPI *tft = nullptr;
TFT_eSprite *spr = nullptr;
int bandTop = 0;
uint32_t lastMs = 0;
float simT = 0;
bool captureNext = false;
Season season = SEASON_NONE;

float frand(float lo, float hi) { return lo + (hi - lo) * (esp_random() / 4294967295.0f); }
inline bool inBand(float y0, float y1) { return y1 >= bandTop && y0 < bandTop + BAND_H; }
inline int Y(float y) { return (int)y - bandTop; }

uint16_t shade(uint16_t c, float k) {
  int r = ((c >> 11) & 0x1F) * k, g = ((c >> 5) & 0x3F) * k, b = (c & 0x1F) * k;
  return (min(r, 31) << 11) | (min(g, 63) << 5) | min(b, 31);
}
uint16_t mix(uint16_t a, uint16_t b, float t) {
  int r = ((a >> 11) & 0x1F) * (1 - t) + ((b >> 11) & 0x1F) * t;
  int g = ((a >> 5) & 0x3F) * (1 - t) + ((b >> 5) & 0x3F) * t;
  int bl = (a & 0x1F) * (1 - t) + (b & 0x1F) * t;
  return (r << 11) | (g << 5) | bl;
}

// ---------- Corgi sprites (facing right) ----------
// O coat  W white  w paw  I inner ear  P tongue  E eye  N nose  C collar  T tag  . clear
const char *const BODY[14] = {
    "..................O...O...",
    ".................OI..OI...",
    ".................OIO.OIO..",
    "................OOOOOOO...",
    "...............OOOOOOOOO..",
    "...............OOOOOEOOO..",
    "...............OOOOOOWWWWN",
    "..............OOOOOWWWWWW.",
    "..OOOOOOOOOOOOOCWWWWWWP...",
    ".OOOOOOOOOOOOOOCWWWWW.....",
    ".OOOOOOOOOOOOOOCTWWW......",
    "WOOOOOOOOOOOOOWWWWW.......",
    "WWOOOOOOOOOOWWWWWW........",
    ".WWWWWWWWWWWWWWWW.........",
};
const char *const LEGS[4][3] = {
    {"..WW..WW....WW..WW........", "..WW..WW....WW..WW........", "..ww..ww....ww..ww........"},
    {"..WW..WW....WW..WW........", ".WW....WW..WW....WW.......", ".ww....ww..ww....ww......."},
    {"..WW..WW....WW..WW........", "..WW..WW....WW..WW........", "..ww..ww....ww..ww........"},
    {"...WWWW......WWWW.........", "...WWWW......WWWW.........", "...wwww......wwww........."},
};
const char *const SIT[17] = {
    "..................O...O...",
    ".................OI..OI...",
    ".................OIO.OIO..",
    "................OOOOOOO...",
    "...............OOOOOOOOO..",
    "...............OOOOOEOOO..",
    "...............OOOOOOWWWWN",
    "..............OOOOOWWWWWW.",
    "...........OOOOCWWWWWWP...",
    "..........OOOOOCWWWWW.....",
    ".........OOOOOOCTWWW......",
    "........OOOOOOOWWWWW......",
    ".......OOOOOOOOWWWW.......",
    "......OOOOOOOOOWWWW.......",
    ".....WOOOOOOOOWWWWW.......",
    ".....WWWOOOOWWW.WW........",
    ".....wwwwww.ww..ww........",
};
// Seen from behind (heading up the yard, away from us): ears, collar, fluffy rear
const char *const BACK[14] = {
    "........O......O..........",
    ".......OOO....OOO.........",
    ".......OOOO..OOOO.........",
    ".......OOOOOOOOOO.........",
    "........OOOOOOOO..........",
    "........CCCCCCCC..........",
    "......OOOOOOOOOOOO........",
    ".....OOOOOOOOOOOOOO.......",
    ".....OOOOOOOOOOOOOO.......",
    ".....OOOOOOOOOOOOOO.......",
    ".....OOOWWWWWWWWOOO.......",
    ".....OWWWWWWWWWWWWO.......",
    ".....WWWWWWWWWWWWWW.......",
    "......WWWWW..WWWWW........",
};
// Seen from the front (coming toward us): face, white chest, collar tag
const char *const FRONT[14] = {
    "........O......O..........",
    ".......OIO....OIO.........",
    ".......OIOO..OOIO.........",
    ".......OOOOOOOOOO.........",
    ".......OOEOOOOEOO.........",
    ".......OOOWWWWOOO.........",
    "........OWWNNWWO..........",
    ".........WWPPWW...........",
    "........CCCTCCCC..........",
    ".......OOWWWWWWOO.........",
    "......OOOWWWWWWOOO........",
    "......OOOWWWWWWOOO........",
    "......OOOOWWWWOOOO........",
    ".......OOOWWWWOOO.........",
};
const char *const LEGS_FB[4][3] = {
    {"........WW....WW..........", "........WW....WW..........", "........ww....ww.........."},
    {"........WW....WW..........", "........ww....WW..........", "..............ww.........."},
    {"........WW....WW..........", "........WW....WW..........", "........ww....ww.........."},
    {"........WW....WW..........", "........WW....ww..........", "........ww................"},
};
constexpr int FB_CENTER_COL = 12;  // middle of the front/back sprites

enum CoatKind { COAT_RED, COAT_TRI, COAT_SABLE };
struct Coat { CoatKind kind; uint16_t main, second, collar; };
const Coat COATS[N_CORGIS] = {
    {COAT_RED, rgb(214, 120, 48), rgb(214, 120, 48), rgb(205, 30, 40)},    // Roux
    {COAT_TRI, rgb(205, 135, 75), rgb(40, 34, 34), rgb(40, 90, 210)},      // tricolour
    {COAT_SABLE, rgb(185, 120, 62), rgb(115, 72, 40), rgb(40, 160, 80)},   // sable
};

enum State { STAND, WALK, SIT_DOWN, SPLOOT, ZOOMIES, CHASE, FETCH, PROUD };
enum View { VIEW_SIDE, VIEW_BACK, VIEW_FRONT };

bool isMoving(State s) { return s == WALK || s == ZOOMIES || s == CHASE || s == FETCH; }

struct Corgi {
  float x, y, tx, ty, speed;
  float mvx, mvy;  // smoothed velocity, drives facing and view
  int dir;         // +1 right, -1 left (side view)
  View view;
  State state;
  float stateT, stateDur, animT, labelT, blinkAt;
  const Coat *coat;
  bool isRoux, hasBall;
  int zoomLaps;
};
Corgi corgis[N_CORGIS];

struct Ball {
  bool active;
  float x, y, z, vx, vy, vz;
  Corgi *holder;
};
Ball ball;

struct Tuft { int x, y; };
Tuft tufts[N_TUFTS];
struct Cloud { float x; int y, size; float speed; };
Cloud clouds[N_CLOUDS];
struct Star { int x, y; };
Star stars[N_STARS];
struct Leaf { float x, y, vy, phase, groundY, age; uint16_t col; bool landed; };
Leaf leaves[N_LEAVES];
struct Flake { float x, y, vy, phase; int size; };
Flake snow[N_SNOW];

// ---------- Time of day ----------
float hourF = 12;   // current hour, fractional
constexpr float NIGHT_LIGHT = 0.62f;
float light = 1;    // NIGHT_LIGHT .. 1 (day)
uint16_t skyRow[HORIZON];

struct SkyKey { float h; uint16_t top, bottom; };
const SkyKey SKY[] = {
    {0, rgb(12, 16, 40), rgb(30, 38, 80)},     {5.5f, rgb(12, 16, 40), rgb(30, 38, 80)},
    {6.7f, rgb(70, 90, 170), rgb(250, 170, 130)}, {8, rgb(80, 155, 235), rgb(185, 222, 250)},
    {17, rgb(80, 155, 235), rgb(185, 222, 250)},  {18.6f, rgb(70, 80, 160), rgb(250, 150, 90)},
    {19.8f, rgb(12, 16, 40), rgb(30, 38, 80)},    {24, rgb(12, 16, 40), rgb(30, 38, 80)},
};

float snowLight() { return max(light, 0.9f); }  // snow stays bright after dark

void updateSky(const tm &t) {
  hourF = t.tm_hour + t.tm_min / 60.0f;
  int k = 0;
  while (SKY[k + 1].h < hourF) k++;
  float f = (hourF - SKY[k].h) / (SKY[k + 1].h - SKY[k].h);
  uint16_t top = mix(SKY[k].top, SKY[k + 1].top, f), bottom = mix(SKY[k].bottom, SKY[k + 1].bottom, f);
  for (int y = 0; y < HORIZON; y++) skyRow[y] = mix(top, bottom, (float)y / HORIZON);
  if (hourF < 5.5f || hourF > 20) light = NIGHT_LIGHT;
  else if (hourF < 7.5f) light = NIGHT_LIGHT + (1 - NIGHT_LIGHT) * (hourF - 5.5f) / 2;
  else if (hourF < 18) light = 1;
  else light = 1 - (1 - NIGHT_LIGHT) * (hourF - 18) / 2;
}

// ---------- Setup ----------
void pickWalkTarget(Corgi &c) {
  c.tx = frand(30, W - 30);
  c.ty = frand(YARD_TOP, YARD_BOTTOM);
}

void setState(Corgi &c, State s, float dur) {
  c.state = s;
  c.stateT = 0;
  c.stateDur = dur;
  if (!isMoving(s)) {
    c.view = VIEW_SIDE;
    c.mvx = c.mvy = 0;
  }
  if (c.isRoux && (s == SIT_DOWN || s == PROUD)) c.labelT = 3;
}

void initScene() {
  for (int i = 0; i < N_CORGIS; i++) {
    Corgi &c = corgis[i];
    c = {};
    c.coat = &COATS[i];
    c.isRoux = i == 0;
    c.x = 60 + i * 90 + frand(-15, 15);
    c.y = frand(YARD_TOP, YARD_BOTTOM);
    c.dir = esp_random() & 1 ? 1 : -1;
    c.blinkAt = frand(1, 4);
    setState(c, i == 0 ? SIT_DOWN : STAND, frand(2, 4));
  }
  ball = {};
  for (auto &t : tufts) t = {(int)frand(0, W), (int)frand(HORIZON + 4, H)};
  for (int i = 0; i < N_CLOUDS; i++) clouds[i] = {frand(0, W), (int)frand(18, 80), (int)frand(10, 18), frand(3, 8)};
  for (auto &s : stars) s = {(int)frand(0, W), (int)frand(4, 110)};
  const uint16_t leafCols[] = {rgb(220, 90, 30), rgb(200, 50, 30), rgb(240, 170, 40), rgb(170, 90, 40)};
  for (auto &l : leaves) {
    l = {frand(0, W), frand(-H, 0), frand(18, 32), frand(0, 6.28f), frand(HORIZON + 8, H - 4), 0,
         leafCols[esp_random() % 4], false};
  }
  for (auto &f : snow) f = {frand(0, W), frand(0, H), frand(14, 34), frand(0, 6.28f), esp_random() % 3 == 0 ? 2 : 1};
}

// ---------- Behaviour ----------
void chooseNext(Corgi &c) {
  float r = frand(0, 1);
  bool someoneZooming = false;
  for (auto &o : corgis) someoneZooming |= o.state == ZOOMIES;
  if (r < 0.06f && !someoneZooming) {
    setState(c, ZOOMIES, 99);
    c.zoomLaps = 3;
    c.tx = c.x < W / 2 ? W - 25 : 25;
    c.ty = frand(YARD_TOP, YARD_BOTTOM);
  } else if (r < 0.40f) {
    setState(c, WALK, 99);
    pickWalkTarget(c);
  } else if (r < 0.70f) {
    setState(c, SIT_DOWN, frand(3, 7));
  } else if (r < 0.85f) {
    setState(c, SPLOOT, frand(4, 8));
  } else {
    setState(c, STAND, frand(1, 3));
  }
}

bool moveToward(Corgi &c, float speed, float dt) {
  float dx = c.tx - c.x, dy = c.ty - c.y;
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 2) return true;
  float step = min(d, speed * dt);
  c.x += dx / d * step;
  c.y += dy / d * step;
  float k = min(1.0f, dt * 6);
  c.mvx += (dx / d * speed - c.mvx) * k;
  c.mvy += (dy / d * speed - c.mvy) * k;

  float ax = fabsf(c.mvx), ay = fabsf(c.mvy);
  // Only turn around once clearly heading the other way, so no left/right dithering
  int want = c.mvx > 0 ? 1 : -1;
  if (want != c.dir && ax > 10 && ax > 0.5f * ay + 10) c.dir = want;
  // Mostly up or down the yard: show the back (going away) or the face (coming toward us)
  if (c.view == VIEW_SIDE) {
    if (ay > 1.6f * ax && ay > 12) c.view = c.mvy < 0 ? VIEW_BACK : VIEW_FRONT;
  } else if (ax > 1.1f * ay) {
    c.view = VIEW_SIDE;
  } else if (ay > 12) {
    c.view = c.mvy < 0 ? VIEW_BACK : VIEW_FRONT;
  }
  return false;
}

// Where the ball sits when carried or dropped (ground position)
float mouthX(const Corgi &c) { return c.view == VIEW_SIDE ? c.x + c.dir * 22 : c.x - 2; }

void updateCorgi(Corgi &c, float dt) {
  c.stateT += dt;
  c.labelT -= dt;
  switch (c.state) {
    case WALK:
      c.animT += dt * 7;
      if (moveToward(c, 32, dt)) chooseNext(c);
      break;
    case ZOOMIES:
      c.animT += dt * 16;
      if (moveToward(c, 150, dt)) {
        if (--c.zoomLaps <= 0) setState(c, STAND, frand(1.5f, 3));
        else {
          c.tx = c.tx > W / 2 ? 25 : W - 25;
          c.ty = frand(YARD_TOP, YARD_BOTTOM);
        }
      }
      break;
    case CHASE:
      c.animT += dt * 14;
      if (!ball.active || ball.holder) { setState(c, STAND, 1); break; }
      // Aim at the ball itself; the old mouth-offset target flipped with the facing
      c.tx = ball.x;
      c.ty = ball.y;
      moveToward(c, 115, dt);
      if (fabsf(ball.x - mouthX(c)) < 12 && fabsf(ball.y - c.y) < 8 && ball.z < 4) {
        ball.holder = &c;
        c.hasBall = true;
        setState(c, FETCH, 99);
        c.tx = W / 2 + frand(-30, 30);
        c.ty = YARD_BOTTOM;
      }
      break;
    case FETCH:
      c.animT += dt * 8;
      if (moveToward(c, 45, dt)) {
        c.hasBall = false;
        ball.holder = nullptr;
        ball.x = mouthX(c);
        ball.y = c.y + (c.view == VIEW_FRONT ? 3 : 1);
        ball.z = 0;
        ball.vx = ball.vy = ball.vz = 0;
        c.dir = c.x < W / 2 ? 1 : -1;
        setState(c, PROUD, frand(5, 7));
      }
      break;
    default:
      if (c.stateT >= c.stateDur) chooseNext(c);
      break;
  }
  c.x = constrain(c.x, 20.0f, W - 20.0f);
  c.y = constrain(c.y, (float)YARD_TOP, (float)YARD_BOTTOM);
  if (simT > c.blinkAt + 0.15f) c.blinkAt = simT + frand(2.5f, 5);
}

void updateBall(float dt) {
  if (!ball.active || ball.holder) return;
  if (ball.z > 0 || ball.vz != 0) {
    ball.vz -= 650 * dt;
    ball.z += ball.vz * dt;
    if (ball.z <= 0) {
      ball.z = 0;
      ball.vz = -ball.vz * 0.45f;
      ball.vx *= 0.6f;
      ball.vy *= 0.6f;
      if (ball.vz < 45) ball.vz = 0;
    }
  } else {  // rolling
    ball.vx *= 1 - min(1.0f, dt * 2.5f);
    ball.vy *= 1 - min(1.0f, dt * 2.5f);
  }
  ball.x += ball.vx * dt;
  ball.y += ball.vy * dt;
  ball.x = constrain(ball.x, 30.0f, W - 30.0f);
  ball.y = constrain(ball.y, (float)YARD_TOP, (float)YARD_BOTTOM);
}

void updateWeather(float dt) {
  for (auto &cl : clouds) {
    cl.x += cl.speed * dt;
    if (cl.x - cl.size * 3 > W) cl.x = -cl.size * 3;
  }
  if (season == SEASON_THANKSGIVING) {
    for (auto &l : leaves) {
      if (!l.landed) {
        l.phase += dt * 3;
        l.y += l.vy * dt;
        l.x += sinf(l.phase) * 0.6f;
        if (l.y >= l.groundY) { l.landed = true; l.age = 0; }
      } else if ((l.age += dt) > 10) {
        l.x = frand(0, W);
        l.y = frand(-40, -5);
        l.landed = false;
      }
    }
  } else if (season == SEASON_CHRISTMAS) {
    for (auto &f : snow) {
      f.phase += dt * 2;
      f.y += f.vy * dt;
      f.x += sinf(f.phase) * 0.3f;
      if (f.y > H) { f.y = -2; f.x = frand(0, W); }
    }
  }
}

// ---------- Drawing ----------
void drawSky() {
  int y1 = min(bandTop + BAND_H, HORIZON);
  for (int y = bandTop; y < y1; y++) spr->drawFastHLine(0, y - bandTop, W, skyRow[y]);
  if (bandTop >= HORIZON) return;

  bool night = light < 0.75f;
  if (night) {
    for (auto &s : stars)
      if (inBand(s.y, s.y)) spr->drawPixel(s.x, Y(s.y), (s.x + (int)(simT * 2)) % 7 ? rgb(230, 230, 200) : rgb(120, 120, 110));
  }
  // Sun (or moon) arcs across the sky
  bool day = hourF >= 6 && hourF <= 19.8f;
  float p = day ? (hourF - 6) / 13.8f : fmodf(hourF + 4.2f, 24) / 10.2f;
  int cx = 20 + p * 280, cy = 120 - sinf(p * PI) * 95;
  if (inBand(cy - 14, cy + 14)) {
    if (day) {
      spr->fillCircle(cx, Y(cy), 12, rgb(255, 230, 120));
      spr->fillCircle(cx, Y(cy), 9, rgb(255, 245, 190));
    } else {
      spr->fillCircle(cx, Y(cy), 9, rgb(240, 238, 215));
      spr->fillCircle(cx + 4, Y(cy) - 3, 8, skyRow[max(0, min(HORIZON - 1, cy - 3))]);
    }
  }
  uint16_t cloudCol = shade(rgb(250, 250, 255), light), cloudShade = shade(rgb(215, 222, 235), light);
  for (auto &cl : clouds) {
    if (!inBand(cl.y - cl.size, cl.y + cl.size)) continue;
    int x = cl.x, y = Y(cl.y), s = cl.size;
    spr->fillEllipse(x, y + s / 3, s * 2, s * 2 / 3, cloudShade);
    spr->fillCircle(x - s, y, s * 2 / 3, cloudCol);
    spr->fillCircle(x + s / 3, y - s / 3, s, cloudCol);
    spr->fillCircle(x + s * 3 / 2, y + s / 6, s * 2 / 3, cloudCol);
  }
  // Rolling hills on the horizon
  if (inBand(110, HORIZON)) {
    spr->fillEllipse(70, Y(HORIZON + 10), 120, 48, shade(rgb(90, 160, 85), light));
    spr->fillEllipse(260, Y(HORIZON + 14), 140, 50, shade(rgb(75, 145, 75), light));
  }
}

void drawGround() {
  if (!inBand(HORIZON, H)) return;
  uint16_t top = shade(rgb(120, 200, 85), light), bottom = shade(rgb(70, 150, 55), light);
  for (int y = max(bandTop, HORIZON); y < bandTop + BAND_H; y++)
    spr->drawFastHLine(0, y - bandTop, W, mix(top, bottom, (float)(y - HORIZON) / (H - HORIZON)));
  uint16_t blade = shade(rgb(60, 130, 45), light);
  for (auto &t : tufts) {
    if (!inBand(t.y - 4, t.y)) continue;
    spr->drawLine(t.x, Y(t.y), t.x - 2, Y(t.y - 4), blade);
    spr->drawLine(t.x + 1, Y(t.y), t.x + 1, Y(t.y - 5), blade);
    spr->drawLine(t.x + 2, Y(t.y), t.x + 4, Y(t.y - 3), blade);
  }
  if (season == SEASON_CHRISTMAS) {  // a dusting of snow
    uint16_t sn = shade(rgb(245, 248, 255), snowLight());
    if (inBand(HORIZON, HORIZON + 3)) spr->fillRect(0, Y(HORIZON), W, 3, sn);
    for (int i = 0; i < N_TUFTS; i += 2)
      if (inBand(tufts[i].y, tufts[i].y + 2)) spr->fillEllipse(tufts[i].x + 8, Y(tufts[i].y + 1), 7, 2, sn);
  }
}

void drawFence() {
  if (!inBand(120, 162)) return;
  uint16_t paint = shade(rgb(245, 243, 235), light), edge = shade(rgb(190, 188, 180), light);
  spr->fillRect(0, Y(134), W, 4, edge);
  spr->fillRect(0, Y(150), W, 4, edge);
  for (int x = -2; x < W; x += 16) {
    spr->fillRect(x, Y(130), 9, 31, paint);
    spr->fillTriangle(x, Y(130), x + 8, Y(130), x + 4, Y(124), paint);
    spr->drawFastVLine(x + 8, Y(130), 31, edge);
  }
  if (season == SEASON_CHRISTMAS) {
    uint16_t sn = shade(rgb(250, 252, 255), snowLight());
    for (int x = -2; x < W; x += 16) spr->fillTriangle(x - 1, Y(127), x + 9, Y(127), x + 4, Y(122), sn);
    // String of blinking lights along the top rail
    spr->drawFastHLine(0, Y(133), W, rgb(30, 70, 35));
    for (int i = 0, x = 6; x < W; x += 14, i++) {
      bool on = ((i + (int)(simT * 2)) & 1) == 0;
      uint16_t c = (i & 1) ? (on ? rgb(255, 40, 40) : rgb(90, 20, 20)) : (on ? rgb(50, 255, 80) : rgb(20, 80, 30));
      spr->fillCircle(x, Y(136), 2, c);
      if (on) spr->drawPixel(x, Y(135), rgb(255, 255, 255));
    }
  }
}

void drawPumpkin(int px, int py, int r) {
  if (!inBand(py - r - 7, py + 2)) return;
  uint16_t pk = shade(rgb(235, 130, 30), light), rib = shade(rgb(200, 100, 20), light);
  spr->fillEllipse(px, Y(py - r * 3 / 4), r, r * 3 / 4, pk);
  spr->drawFastVLine(px - r / 3, Y(py - r * 3 / 2) + 1, r * 3 / 2 - 1, rib);
  spr->drawFastVLine(px + r / 3, Y(py - r * 3 / 2) + 1, r * 3 / 2 - 1, rib);
  spr->fillRect(px - 1, Y(py - r * 3 / 2 - 4), 3, 5, shade(rgb(70, 120, 40), light));
}

void drawDoghouse() {
  int x = HOUSE_X, base = HOUSE_BASE;
  if (!inBand(base - 62, base + 2)) return;
  uint16_t wall = shade(rgb(190, 55, 45), light), wallDark = shade(rgb(150, 40, 35), light);
  uint16_t roof = shade(rgb(95, 65, 55), light), plate = shade(rgb(245, 235, 210), light);
  spr->fillRect(x, Y(base - 36), HOUSE_W, 36, wall);
  for (int yy = base - 30; yy < base; yy += 7) spr->drawFastHLine(x, Y(yy), HOUSE_W, wallDark);
  spr->fillTriangle(x - 6, Y(base - 34), x + HOUSE_W + 6, Y(base - 34), x + HOUSE_W / 2, Y(base - 62), roof);
  if (season == SEASON_CHRISTMAS) {
    uint16_t sn = shade(rgb(250, 252, 255), snowLight());
    spr->drawWideLine(x - 6, Y(base - 35), x + HOUSE_W / 2, Y(base - 63), 3, sn, sn);
    spr->drawWideLine(x + HOUSE_W / 2, Y(base - 63), x + HOUSE_W + 6, Y(base - 35), 3, sn, sn);
  }
  // Arched door
  int dx = x + HOUSE_W / 2;
  spr->fillRect(dx - 10, Y(base - 18), 20, 18, rgb(25, 18, 18));
  spr->fillCircle(dx, Y(base - 18), 10, rgb(25, 18, 18));
  // Name plate
  spr->fillRoundRect(dx - 15, Y(base - 44), 30, 11, 2, plate);
  spr->setTextColor(shade(rgb(120, 40, 35), light));
  spr->drawCentreString("ROUX", dx, Y(base - 42), 1);
}

uint16_t pixelColor(char ch, int row, int col, const Corgi &c, bool blink, bool pant, View view) {
  const Coat &k = *c.coat;
  switch (ch) {
    case 'O': {
      bool head = view == VIEW_SIDE ? row <= 8 && col >= 14 : row <= (view == VIEW_FRONT ? 7 : 4);
      if (k.kind == COAT_TRI) return shade(head ? k.main : k.second, light);
      if (k.kind == COAT_SABLE) return shade(!head && row <= 10 ? k.second : k.main, light);
      return shade(k.main, light);
    }
    case 'W': return shade(rgb(250, 246, 238), light);
    case 'I': return shade(rgb(235, 160, 150), light);
    case 'w': return shade(rgb(205, 198, 188), light);
    case 'P': return pant ? shade(rgb(240, 105, 125), light) : shade(rgb(250, 246, 238), light);
    case 'E': return blink ? pixelColor('O', row, col, c, false, false, view) : rgb(20, 16, 16);
    case 'N': return rgb(20, 16, 16);
    case 'C': return shade(k.collar, light);
    case 'T': return c.isRoux ? rgb(250, 205, 60) : shade(k.collar, light);
  }
  return 0;
}

// Draw sprite rows [srcRow0, srcRow0 + n) with top-left at (left, top), mirrored if facing left
void drawRows(const char *const *rows, int n, int srcRow0, int left, int top, const Corgi &c, bool blink, bool pant,
              View view = VIEW_SIDE) {
  for (int i = 0; i < n; i++) {
    int sy = top + i * S;
    if (sy + S <= bandTop || sy >= bandTop + BAND_H) continue;
    const char *row = rows[i];
    int runStart = -1;
    uint16_t runCol = 0;
    for (int sc = 0; sc <= SW; sc++) {
      bool clear = true;
      uint16_t col = 0;
      if (sc < SW) {
        int srcCol = view != VIEW_SIDE || c.dir > 0 ? sc : SW - 1 - sc;  // front/back are symmetric
        char ch = row[srcCol];
        if (ch != '.') {
          clear = false;
          col = pixelColor(ch, srcRow0 + i, srcCol, c, blink, pant, view);
        }
      }
      if (runStart >= 0 && (clear || col != runCol)) {
        spr->fillRect(left + runStart * S, Y(sy), (sc - runStart) * S, S, runCol);
        runStart = -1;
      }
      if (!clear && runStart < 0) {
        runStart = sc;
        runCol = col;
      }
    }
  }
}

void drawSantaHat(int hx, int by, int dir) {
  uint16_t red = rgb(215, 30, 40), white = rgb(250, 250, 250);
  spr->fillTriangle(hx - 7, by, hx + 7, by, hx - dir * 9, by - 13, red);
  spr->fillRect(hx - 8, by - 2, 16, 4, white);
  spr->fillCircle(hx - dir * 9, by - 13, 3, white);
}

void drawLabel(int x, int y) {
  spr->fillRoundRect(x - 16, y, 32, 12, 4, rgb(255, 255, 255));
  spr->drawRoundRect(x - 16, y, 32, 12, 4, rgb(205, 30, 40));
  spr->setTextColor(rgb(60, 30, 25));
  spr->drawCentreString("Roux", x, y + 2, 1);
}

void drawBall(float x, float y) {
  int bx = x, by = Y(y);
  spr->fillCircle(bx, by, 4, rgb(205, 235, 60));
  spr->drawPixel(bx - 2, by - 2, rgb(250, 255, 200));
  spr->drawLine(bx - 3, by + 1, bx + 1, by - 3, rgb(245, 250, 230));
}

void drawCorgi(const Corgi &c) {
  int left = c.x - SW * S / 2;
  int top = c.y - SH * S;
  if (!inBand(top - 40, c.y + 4)) return;

  bool moving = isMoving(c.state);
  bool fb = moving && c.view != VIEW_SIDE;  // front or back view
  bool sitting = c.state == SIT_DOWN || c.state == PROUD;
  bool blink = simT >= c.blinkAt && simT < c.blinkAt + 0.15f;
  bool pant = !c.hasBall && (sitting || c.state == ZOOMIES || c.state == CHASE || c.state == STAND) &&
              ((int)(simT * 6) & 1);
  int frame = moving ? ((int)c.animT) % 4 : 0;
  int bob = moving && (frame == 1 || frame == 3) ? -1 : 0;
  if (c.state == ZOOMIES || c.state == CHASE) frame = frame & 1 ? 1 : 3;

  if (inBand(c.y - 3, c.y + 3)) spr->fillEllipse(c.x, Y(c.y + 1), fb ? 14 : 22, 3, shade(rgb(65, 135, 50), light));

  int rowShift = 0;
  if (sitting) {
    drawRows(SIT, SH, 0, left, top, c, blink, pant);
  } else if (c.state == SPLOOT) {
    rowShift = 3;
    drawRows(BODY, 14, 0, left, top + rowShift * S, c, blink, false);
    // Back legs stretched out behind
    int px = c.dir > 0 ? left - 2 * S : left + SW * S;
    if (inBand(c.y - 4, c.y)) spr->fillRect(px, Y(c.y - 2 * S), 2 * S, 2 * S, shade(rgb(205, 198, 188), light));
  } else if (fb) {
    drawRows(c.view == VIEW_BACK ? BACK : FRONT, 14, 0, left, top + bob * S, c, blink, pant, c.view);
    drawRows(LEGS_FB[frame], 3, 14, left, top + 14 * S, c, blink, pant, c.view);
  } else {
    drawRows(BODY, 14, 0, left, top + bob * S, c, blink, pant);
    drawRows(LEGS[frame], 3, 14, left, top + 14 * S, c, blink, pant);
    // Wiggly rear: a tuft of fluff that bounces while walking or standing
    if ((c.state == WALK || c.state == STAND) && ((int)(simT * 6) & 1)) {
      int fx = c.dir > 0 ? left - S : left + SW * S;
      int fy = top + (bob + 10) * S;
      if (inBand(fy, fy + S * 2)) spr->fillRect(fx, Y(fy), S, S * 2, shade(rgb(250, 246, 238), light));
    }
  }

  int headTop = top + (sitting ? 0 : rowShift + bob) * S + 3 * S;
  int hx = fb ? left + FB_CENTER_COL * S : left + (c.dir > 0 ? 20 : 5) * S + S / 2;
  if (season == SEASON_CHRISTMAS) drawSantaHat(hx, Y(headTop + 1), c.dir);
  else if (season == SEASON_THANKSGIVING && c.isRoux) drawPilgrimHat(spr, hx, Y(headTop + 1), 0.8f);

  if (c.hasBall && c.view == VIEW_FRONT && fb) {
    drawBall(left + FB_CENTER_COL * S, top + (bob + 8) * S);  // ball in the mouth, facing us
  } else if (c.hasBall && !fb) {
    drawBall(left + (c.dir > 0 ? 24 : 1) * S + 1, top + (bob + 9) * S);
  }  // seen from behind, the ball is hidden by the head
  if (c.isRoux && c.labelT > 0 && sitting) {
    int ly = headTop - (season != SEASON_NONE ? 30 : 18);
    if (inBand(ly, ly + 12)) drawLabel(hx, Y(ly));
  }
}

void drawLeavesAndSnow() {
  if (season == SEASON_THANKSGIVING) {
    for (auto &l : leaves) {
      if (!inBand(l.y - 3, l.y + 3)) continue;
      uint16_t c = shade(l.col, light);
      if (l.landed || (int)(l.phase * 2) & 1) spr->fillRect(l.x, Y(l.y), 4, 2, c);
      else spr->fillRect(l.x + 1, Y(l.y) - 1, 2, 4, c);
    }
  } else if (season == SEASON_CHRISTMAS) {
    for (auto &f : snow)
      if (inBand(f.y, f.y + 2)) spr->fillRect(f.x, Y(f.y), f.size, f.size, rgb(250, 252, 255));
  }
}

void drawClockOverlay(const tm &t) {
  if (bandTop != 0) return;
  char buf[12];
  int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
  snprintf(buf, sizeof(buf), "%d:%02d %s", h12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  spr->setTextColor(light > 0.8f ? rgb(25, 45, 90) : rgb(230, 235, 255));
  spr->drawRightString(buf, W - 8, 10, 2);
}

}  // namespace

bool corgiBegin(TFT_eSPI &display) {
  tft = &display;
  if (!spr) {
    spr = new TFT_eSprite(tft);
    spr->setColorDepth(16);
    if (!spr->createSprite(W, BAND_H)) {
      delete spr;
      spr = nullptr;
      return false;
    }
    // New sprites leave TFT_eSPI's free-font pointer uninitialised; this clears it.
    // Without it, drawing in font 1 reads a garbage pointer and crashes.
    spr->setTextFont(1);
  }
  initScene();
  lastMs = millis();
  simT = 0;
  return true;
}

void corgiEnd() {
  if (spr) {
    spr->deleteSprite();
    delete spr;
    spr = nullptr;
  }
}

void corgiTap(int x, int y) {
  // Throw from the viewer's side toward the tapped spot
  if (ball.holder) ball.holder->hasBall = false;
  const float T = 0.9f, G = 650;
  float gx = constrain((float)x, 30.0f, W - 30.0f), gy = constrain((float)y, (float)YARD_TOP, (float)YARD_BOTTOM);
  ball = {true, W / 2.0f, (float)YARD_BOTTOM, 25, (gx - W / 2.0f) / T, (gy - YARD_BOTTOM) / T, G * T / 2 - 25 / T, nullptr};

  // Nearest corgi gives chase; anyone who was carrying it lets go
  Corgi *best = nullptr;
  float bestD = 1e9;
  for (auto &c : corgis) {
    if (c.state == CHASE || c.state == FETCH) setState(c, STAND, frand(1, 2));
    float d = fabsf(c.x - gx) + fabsf(c.y - gy);
    if (d < bestD) { bestD = d; best = &c; }
  }
  if (best) setState(*best, CHASE, 99);
}

void corgiCaptureNextFrame() { captureNext = true; }

void corgiFrame(const tm &t) {
  if (!spr) return;
  uint32_t now = millis();
  float dt = min(0.1f, (now - lastMs) / 1000.0f);
  simT += dt;
  season = currentSeason(t);
  updateSky(t);
  for (auto &c : corgis) updateCorgi(c, dt);
  updateBall(dt);
  updateWeather(dt);

  // Back-to-front by paw position, ball included
  int order[N_CORGIS];
  for (int i = 0; i < N_CORGIS; i++) order[i] = i;
  for (int i = 1; i < N_CORGIS; i++)
    for (int j = i; j > 0 && corgis[order[j]].y < corgis[order[j - 1]].y; j--) {
      int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
    }

  bool capture = captureNext;
  captureNext = false;
  if (capture) Serial.printf("\nSHOT %d %d\n", W, H);

  for (bandTop = 0; bandTop < H; bandTop += BAND_H) {
    drawSky();
    drawGround();
    drawFence();
    drawDoghouse();
    if (season == SEASON_THANKSGIVING) {  // pumpkins along the fence, clear of the dogs
      drawPumpkin(26, HORIZON + 6, 11);
      drawPumpkin(46, HORIZON + 8, 8);
    }
    bool ballDrawn = !ball.active || ball.holder;
    if (!ballDrawn && inBand(ball.y - 2, ball.y + 2))
      spr->fillEllipse(ball.x, Y(ball.y + 1), 5, 2, shade(rgb(65, 135, 50), light));  // ball shadow
    for (int i = 0; i < N_CORGIS; i++) {
      const Corgi &c = corgis[order[i]];
      if (!ballDrawn && ball.y < c.y) {
        if (inBand(ball.y - ball.z - 10, ball.y)) drawBall(ball.x, ball.y - ball.z - 4);
        ballDrawn = true;
      }
      drawCorgi(c);
    }
    if (!ballDrawn && inBand(ball.y - ball.z - 10, ball.y)) drawBall(ball.x, ball.y - ball.z - 4);
    drawLeavesAndSnow();
    drawClockOverlay(t);
    spr->pushSprite(0, bandTop);
    if (capture) Serial.write((const uint8_t *)spr->getPointer(), W * BAND_H * 2);
  }
  if (capture) {
    Serial.flush();
    Serial.println("\nSHOT END");
    now = millis();
  }
  lastMs = now;
}
