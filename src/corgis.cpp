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

enum State { STAND, WALK, SIT_DOWN, SPLOOT, ZOOMIES, CHASE, FETCH, PROUD, SNIFF, BUTTERFLY, SWIM };
enum View { VIEW_SIDE, VIEW_BACK, VIEW_FRONT };

bool isMoving(State s) {
  return s == WALK || s == ZOOMIES || s == CHASE || s == FETCH || s == SNIFF || s == BUTTERFLY || s == SWIM;
}

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
  int target;             // egg or butterfly being pursued
  bool wet, inPool;       // summer: splashing in the pool, then wet zoomies
  float nextHeart, nextPuff;
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

// ---------- Seasonal and weather extras ----------
constexpr int N_EGGS = 8, N_BUTTERFLIES = 3, N_FLOWERS = 34, N_CLOVERS = 22, N_SPARKS = 120, N_ROCKETS = 3,
              N_HEARTS = 10, N_PUFFS = 8, N_DROPS = 70, N_BATS = 4;
constexpr float POOL_X = 92, POOL_Y = 214, POOL_RX = 32, POOL_RY = 9;

struct Egg { float x, y; uint16_t c1, c2; bool found; };
Egg eggs[N_EGGS];
float eggsRespawnAt = -1;
struct Butterfly { float x, y, vx, vy, phase; uint16_t col; };
Butterfly butterflies[N_BUTTERFLIES];
struct Flower { int x, y; uint16_t col; };
Flower flowers[N_FLOWERS];
struct Clover { int x, y; };
Clover clovers[N_CLOVERS];
struct Spark { float x, y, vx, vy, g, life, maxLife; uint16_t col; int size; };  // fireworks, splashes, sparkles
Spark sparks[N_SPARKS];
struct Rocket { float x, y, vy, burstY; uint16_t col; bool active; };
Rocket rockets[N_ROCKETS];
float nextRocket = 0;
struct Heart { float x, y, life; };
Heart hearts[N_HEARTS];
struct Puff { float x, y, life; };
Puff puffs[N_PUFFS];
struct Drop { float x, y, speed; };
Drop drops[N_DROPS];
struct Bat { float x, y, vx, phase; };
Bat bats[N_BATS];

// Real weather where the board lives (set from main via corgiSetWeather)
int weatherCode = -1;  // WMO weather code, -1 = unknown
float weatherTempF = 70;
float nextLightning = 5, lightningAt = -100;

bool isSnowing() { return (weatherCode >= 71 && weatherCode <= 77) || weatherCode == 85 || weatherCode == 86; }
bool isRaining() {
  return (weatherCode >= 51 && weatherCode <= 67) || (weatherCode >= 80 && weatherCode <= 82) || weatherCode >= 95;
}
bool isStormy() { return weatherCode >= 95; }
bool isOvercast() { return weatherCode == 3 || weatherCode == 45 || weatherCode == 48 || isRaining() || isSnowing(); }
bool snowyGround() { return season == SEASON_CHRISTMAS || season == SEASON_WINTER || isSnowing(); }
bool snowFalling() { return !isRaining() && snowyGround(); }
bool leavesFalling() { return season == SEASON_THANKSGIVING || season == SEASON_FALL || season == SEASON_HALLOWEEN; }
bool coldOut() { return season == SEASON_WINTER || (weatherCode >= 0 && weatherTempF < 45); }
bool fireworksSeason() { return season == SEASON_NEW_YEAR || season == SEASON_JULY4; }

void spawnSpark(float x, float y, float vx, float vy, float g, float life, uint16_t col, int size) {
  for (auto &sp : sparks)
    if (sp.life <= 0) {
      sp = {x, y, vx, vy, g, life, life, col, size};
      return;
    }
}

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

  // Real weather: grey skies under cloud, rain or snow, and a soft flash for lightning
  if (isOvercast()) {
    uint16_t grey = light < 0.8f ? rgb(45, 48, 58) : rgb(125, 130, 140);
    for (int y = 0; y < HORIZON; y++) skyRow[y] = mix(skyRow[y], grey, 0.55f);
  }
  if (simT - lightningAt < 0.12f)
    for (int y = 0; y < HORIZON; y++) skyRow[y] = mix(skyRow[y], rgb(225, 225, 240), 0.35f);
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

void placeEggs() {
  const uint16_t pastel[] = {rgb(250, 170, 200), rgb(170, 210, 250), rgb(250, 230, 140), rgb(180, 240, 180),
                             rgb(220, 180, 250)};
  for (auto &e : eggs)
    e = {frand(25, W - 25), frand(YARD_TOP, YARD_BOTTOM + 6), pastel[esp_random() % 5], pastel[esp_random() % 5], false};
}

void initExtras() {
  placeEggs();
  eggsRespawnAt = -1;
  const uint16_t wing[] = {rgb(250, 200, 60), rgb(240, 120, 200), rgb(130, 190, 255)};
  for (int i = 0; i < N_BUTTERFLIES; i++)
    butterflies[i] = {frand(30, W - 30), frand(150, 200), frand(-20, 20), frand(-10, 10), frand(0, 6.28f), wing[i]};
  const uint16_t petals[] = {rgb(250, 90, 120), rgb(255, 210, 60), rgb(200, 120, 250), rgb(255, 255, 255),
                             rgb(255, 150, 60)};
  for (auto &f : flowers) {
    bool byFence = esp_random() & 1;
    f = {(int)frand(4, W - 4), byFence ? (int)frand(HORIZON + 2, HORIZON + 8) : (int)frand(HORIZON + 14, H - 4),
         petals[esp_random() % 5]};
  }
  for (auto &c : clovers) c = {(int)frand(6, W - 6), (int)frand(HORIZON + 8, H - 4)};
  for (auto &sp : sparks) sp.life = 0;
  for (auto &r : rockets) r.active = false;
  for (auto &h : hearts) h.life = 0;
  for (auto &pf : puffs) pf.life = 0;
  for (auto &d : drops) d = {frand(0, W + 40), frand(0, H), frand(220, 300)};
  for (auto &b : bats) b = {frand(0, W), frand(30, 110), frand(25, 45) * (esp_random() & 1 ? 1 : -1), frand(0, 6.28f)};
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
  initExtras();
}

// ---------- Behaviour ----------
void chooseNext(Corgi &c) {
  // Seasonal pastimes win some of the time
  if (season == SEASON_EASTER && frand(0, 1) < 0.5f) {  // sniff out a hidden egg
    int best = -1;
    float bestD = 1e9;
    for (int i = 0; i < N_EGGS; i++) {
      if (eggs[i].found) continue;
      bool taken = false;
      for (auto &o : corgis) taken |= &o != &c && o.state == SNIFF && o.target == i;
      float d = fabsf(eggs[i].x - c.x) + fabsf(eggs[i].y - c.y);
      if (!taken && d < bestD) {
        bestD = d;
        best = i;
      }
    }
    if (best >= 0) {
      setState(c, SNIFF, 99);
      c.target = best;
      c.tx = eggs[best].x;
      c.ty = constrain(eggs[best].y, (float)YARD_TOP, (float)YARD_BOTTOM);
      return;
    }
  }
  if (season == SEASON_SPRING && frand(0, 1) < 0.2f) {  // chase a butterfly
    setState(c, BUTTERFLY, frand(3, 5));
    c.target = esp_random() % N_BUTTERFLIES;
    return;
  }
  if (season == SEASON_SUMMER && frand(0, 1) < 0.15f) {  // a dip in the pool
    bool busy = false;
    for (auto &o : corgis) busy |= o.state == SWIM;
    if (!busy) {
      setState(c, SWIM, 99);
      c.inPool = false;
      c.tx = POOL_X;
      c.ty = POOL_Y;
      return;
    }
  }
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
      if (c.animT >= 4) c.animT -= 4;  // leg frames cycle 0-3; keep the counter small
      if (moveToward(c, 32, dt)) chooseNext(c);
      break;
    case ZOOMIES:
      c.animT += dt * 16;
      if (c.animT >= 4) c.animT -= 4;
      if (c.wet && frand(0, 1) < dt * 20)  // wet zoomies fling water
        spawnSpark(c.x - c.dir * 20, c.y - 12, -c.dir * frand(10, 40), frand(-60, -20), 300, 0.5f, rgb(170, 215, 255), 1);
      if (moveToward(c, 150, dt)) {
        if (--c.zoomLaps <= 0) {
          c.wet = false;
          setState(c, STAND, frand(1.5f, 3));
        } else {
          c.tx = c.tx > W / 2 ? 25 : W - 25;
          c.ty = frand(YARD_TOP, YARD_BOTTOM);
        }
      }
      break;
    case CHASE: {
      c.animT += dt * 14;
      if (c.animT >= 4) c.animT -= 4;
      if (!ball.active || ball.holder) { setState(c, STAND, 1); break; }
      // Aim at the ball itself; the old mouth-offset target flipped with the facing
      c.tx = ball.x;
      c.ty = ball.y;
      moveToward(c, 115, dt);
      // Grab it if it's at the mouth OR under the dog. A dog standing on the ball
      // facing away used to wait forever, since it won't spin round for a tiny move.
      // After 10 s of chasing it grabs the ball wherever it is, just in case.
      bool atMouth = fabsf(ball.x - mouthX(c)) < 12, underfoot = fabsf(ball.x - c.x) < 16;
      if (((atMouth || underfoot) && fabsf(ball.y - c.y) < 8 && ball.z < 4) || c.stateT > 10) {
        Serial.printf("corgi: got the ball%s\n", c.stateT > 10 ? " (chase timeout)" : "");
        ball.holder = &c;
        c.hasBall = true;
        setState(c, FETCH, 99);
        c.tx = W / 2 + frand(-30, 30);
        c.ty = YARD_BOTTOM;
      }
      break;
    }
    case FETCH:
      c.animT += dt * 8;
      if (c.animT >= 4) c.animT -= 4;
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
    case SNIFF:
      c.animT += dt * 6;
      if (c.animT >= 4) c.animT -= 4;
      if (eggs[c.target].found) {
        chooseNext(c);
        break;
      }
      if (moveToward(c, 30, dt)) {
        Egg &e = eggs[c.target];
        e.found = true;
        for (int i = 0; i < 12; i++) {
          float a = frand(0, 6.28f), v = frand(20, 45);
          spawnSpark(e.x, e.y - 4, cosf(a) * v, sinf(a) * v - 20, 40, 0.8f, i & 1 ? rgb(255, 240, 120) : rgb(255, 255, 255), 1);
        }
        setState(c, PROUD, frand(3, 4));
      }
      break;
    case BUTTERFLY: {
      c.animT += dt * 12;
      if (c.animT >= 4) c.animT -= 4;
      const Butterfly &b = butterflies[c.target];
      c.tx = b.x;
      c.ty = constrain(b.y + 40, (float)YARD_TOP, (float)YARD_BOTTOM);
      moveToward(c, 75, dt);
      if (c.stateT > c.stateDur) setState(c, STAND, frand(1, 2));
      break;
    }
    case SWIM:
      if (!c.inPool) {
        c.animT += dt * 8;
        if (c.animT >= 4) c.animT -= 4;
        if (moveToward(c, 40, dt)) {
          c.inPool = true;
          c.stateT = 0;
          c.view = VIEW_SIDE;
        }
      } else {
        if (frand(0, 1) < dt * 14)  // splashing about
          spawnSpark(c.x + frand(-14, 14), c.y - 4, frand(-40, 40), frand(-110, -60), 320, 0.6f, rgb(170, 215, 255),
                     1 + (esp_random() & 1));
        if (frand(0, 1) < dt * 0.8f) c.dir = -c.dir;  // turning about in the water
        if (c.stateT > 3.5f) {
          c.inPool = false;
          c.wet = true;
          setState(c, ZOOMIES, 99);
          c.zoomLaps = 3;
          c.tx = c.x < W / 2 ? W - 25 : 25;
          c.ty = frand(YARD_TOP, YARD_BOTTOM);
        }
      }
      break;
    default:
      if (c.stateT >= c.stateDur) chooseNext(c);
      break;
  }

  // Valentine's: hearts float up from sitting dogs, faster with a friend nearby
  bool sittingNow = c.state == SIT_DOWN || c.state == PROUD;
  if (season == SEASON_VALENTINE && sittingNow && simT > c.nextHeart) {
    bool friendNear = false;
    for (auto &o : corgis) friendNear |= &o != &c && fabsf(o.x - c.x) < 70 && fabsf(o.y - c.y) < 25;
    for (auto &h : hearts)
      if (h.life <= 0) {
        h = {c.x + c.dir * 16 + frand(-4, 4), c.y - 34, 2.5f};
        break;
      }
    c.nextHeart = simT + (friendNear ? frand(0.6f, 1.2f) : frand(1.8f, 3.5f));
  }
  // Cold out: little puffs of breath
  if (coldOut() && simT > c.nextPuff) {
    float mx = c.view == VIEW_SIDE ? c.x + c.dir * 25 : c.x - 2;
    float my = c.y - (c.state == SPLOOT ? 14 : 20);
    for (auto &pf : puffs)
      if (pf.life <= 0) {
        pf = {mx, my, 1.2f};
        break;
      }
    c.nextPuff = simT + frand(2.0f, 3.5f);
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
  if (leavesFalling()) {
    for (auto &l : leaves) {
      if (!l.landed) {
        l.phase += dt * 3;
        if (l.phase > 6.2832f) l.phase -= 6.2832f;
        l.y += l.vy * dt;
        l.x += sinf(l.phase) * 0.6f;
        if (l.y >= l.groundY) { l.landed = true; l.age = 0; }
      } else if ((l.age += dt) > 10) {
        l.x = frand(0, W);
        l.y = frand(-40, -5);
        l.landed = false;
      }
    }
  }
  if (snowFalling()) {
    for (auto &f : snow) {
      f.phase += dt * 2;
      if (f.phase > 6.2832f) f.phase -= 6.2832f;
      f.y += f.vy * dt;
      f.x += sinf(f.phase) * 0.3f;
      if (f.y > H) { f.y = -2; f.x = frand(0, W); }
    }
  }

  if (isRaining())
    for (auto &d : drops) {
      d.y += d.speed * dt;
      d.x -= d.speed * 0.25f * dt;
      if (d.y > H) {
        d.y = frand(-20, 0);
        d.x = frand(0, W + 40);
      }
    }
  if (isStormy() && simT > nextLightning) {
    lightningAt = simT;
    nextLightning = simT + frand(8, 16);
  }

  if (season == SEASON_SPRING)
    for (auto &b : butterflies) {
      b.phase += dt * 18;
      if (b.phase > 6.2832f) b.phase -= 6.2832f;
      b.vx = constrain(b.vx + frand(-60, 60) * dt, -35.0f, 35.0f);
      b.vy = constrain(b.vy + frand(-50, 50) * dt, -20.0f, 20.0f);
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      if (b.x < 15 || b.x > W - 15) b.vx = -b.vx;
      if (b.y < 140 || b.y > 205) b.vy = -b.vy;
      b.x = constrain(b.x, 15.0f, W - 15.0f);
      b.y = constrain(b.y, 140.0f, 205.0f);
    }

  if (season == SEASON_EASTER) {  // a fresh hunt once every egg is found
    bool all = true;
    for (auto &e : eggs) all &= e.found;
    if (all && eggsRespawnAt < 0) eggsRespawnAt = simT + 6;
    if (eggsRespawnAt >= 0 && simT > eggsRespawnAt) {
      placeEggs();
      eggsRespawnAt = -1;
    }
  }

  if (fireworksSeason() && light < 0.75f && simT > nextRocket) {  // fireworks after dark
    const uint16_t cols[] = {rgb(255, 80, 80), rgb(255, 220, 80), rgb(120, 200, 255), rgb(150, 255, 150),
                             rgb(255, 140, 255), rgb(255, 255, 255)};
    for (auto &r : rockets)
      if (!r.active) {
        r = {frand(40, W - 40), (float)HORIZON, frand(-170, -130), frand(35, 80), cols[esp_random() % 6], true};
        break;
      }
    nextRocket = simT + frand(0.6f, 1.6f);
  }
  for (auto &r : rockets) {
    if (!r.active) continue;
    r.y += r.vy * dt;
    if (r.y <= r.burstY) {
      r.active = false;
      // Two rings (fast and slow) read as a full, round burst
      for (int i = 0; i < 40; i++) {
        float a = i * 6.2832f / 20 + frand(-0.06f, 0.06f), v = (i < 20 ? 34 : 20) * frand(0.92f, 1.08f);
        spawnSpark(r.x, r.y, cosf(a) * v, sinf(a) * v, 14, frand(1.1f, 1.5f), r.col, i < 20 ? 2 : 1);
      }
    }
  }

  for (auto &sp : sparks)
    if (sp.life > 0) {
      sp.life -= dt;
      sp.vy += sp.g * dt;
      sp.x += sp.vx * dt;
      sp.y += sp.vy * dt;
    }
  for (auto &h : hearts)
    if (h.life > 0) {
      h.life -= dt;
      h.y -= 14 * dt;
      h.x += sinf(h.life * 4) * 0.3f;
    }
  for (auto &pf : puffs)
    if (pf.life > 0) {
      pf.life -= dt;
      pf.y -= 6 * dt;
    }
  if (season == SEASON_HALLOWEEN)
    for (auto &b : bats) {
      b.phase += dt * 12;
      if (b.phase > 6.2832f) b.phase -= 6.2832f;
      b.x += b.vx * dt;
      if (b.x < -20) b.x = W + 20;
      if (b.x > W + 20) b.x = -20;
    }
}

// ---------- Drawing ----------
void drawPool() {
  if (!inBand(POOL_Y - POOL_RY - 2, POOL_Y + POOL_RY + 2)) return;
  spr->fillEllipse(POOL_X, Y(POOL_Y), POOL_RX, POOL_RY, shade(rgb(70, 150, 230), light));
  spr->fillEllipse(POOL_X, Y(POOL_Y), POOL_RX - 4, POOL_RY - 3, shade(rgb(130, 205, 250), light));
  int rip = sinf(simT * 2) * 6;
  spr->drawFastHLine(POOL_X - 12 + rip, Y(POOL_Y - 1), 8, shade(rgb(235, 250, 255), light));
  spr->drawFastHLine(POOL_X + 4 - rip, Y(POOL_Y + 2), 6, shade(rgb(235, 250, 255), light));
}

void drawCake() {
  const int cx = 214, base = 189;  // between the fence and the dogs, left of the doghouse
  if (!inBand(base - 27, base + 3)) return;
  uint16_t plate = shade(rgb(235, 235, 240), light), sponge = shade(rgb(245, 170, 200), light);
  uint16_t icing = shade(rgb(255, 250, 245), light);
  spr->fillEllipse(cx, Y(base), 16, 3, plate);
  spr->fillRect(cx - 11, Y(base - 12), 22, 12, sponge);
  spr->fillRect(cx - 11, Y(base - 14), 22, 3, icing);
  for (int i = 0; i < 5; i++) spr->fillRect(cx - 10 + i * 5, Y(base - 12), 2, 2 + (i % 2) * 2, icing);  // drips
  for (int i = -1; i <= 1; i++) {
    int x = cx + i * 6, flick = ((int)(simT * 10) + i) & 1;
    spr->fillRect(x, Y(base - 21), 2, 7, i == 0 ? rgb(120, 170, 250) : rgb(250, 220, 90));
    spr->fillCircle(x + 1, Y(base - 23 - flick), 2, rgb(255, 170, 40));
    spr->drawPixel(x + 1, Y(base - 23 - flick), rgb(255, 250, 200));
  }
}

void drawGroundExtras() {
  if (season == SEASON_ST_PATRICK) {
    uint16_t leaf = shade(rgb(40, 155, 65), light);
    for (auto &c : clovers) {
      if (!inBand(c.y - 4, c.y + 2)) continue;
      spr->fillCircle(c.x - 2, Y(c.y - 2), 1, leaf);
      spr->fillCircle(c.x + 2, Y(c.y - 2), 1, leaf);
      spr->fillCircle(c.x, Y(c.y - 4), 1, leaf);
      spr->drawFastVLine(c.x, Y(c.y - 2), 3, leaf);
    }
  }
  if (season == SEASON_SPRING) {
    uint16_t stem = shade(rgb(50, 130, 50), light);
    for (auto &f : flowers) {
      if (f.y < HORIZON + 10 || !inBand(f.y - 6, f.y)) continue;  // fence-side ones are drawn after the fence
      spr->drawFastVLine(f.x, Y(f.y - 3), 4, stem);
      spr->fillCircle(f.x, Y(f.y - 4), 2, shade(f.col, light));
      spr->drawPixel(f.x, Y(f.y - 4), rgb(255, 230, 90));
    }
  }
  if (season == SEASON_EASTER)
    for (auto &e : eggs) {
      if (e.found || !inBand(e.y - 9, e.y)) continue;
      spr->fillEllipse(e.x, Y(e.y - 4), 3, 4, shade(e.c1, light));
      spr->drawFastHLine(e.x - 3, Y(e.y - 4), 7, shade(e.c2, light));
      spr->drawPixel(e.x - 1, Y(e.y - 6), rgb(255, 255, 255));
    }
  if (season == SEASON_SUMMER) drawPool();
}

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
  if (!isOvercast() && inBand(cy - 14, cy + 14)) {
    if (day) {
      spr->fillCircle(cx, Y(cy), 12, rgb(255, 230, 120));
      spr->fillCircle(cx, Y(cy), 9, rgb(255, 245, 190));
    } else if (season == SEASON_HALLOWEEN) {  // big orange harvest moon
      spr->fillCircle(cx, Y(cy), 12, rgb(250, 190, 90));
      spr->fillCircle(cx - 4, Y(cy) - 3, 2, rgb(225, 160, 70));
      spr->fillCircle(cx + 3, Y(cy) + 4, 3, rgb(225, 160, 70));
    } else {
      spr->fillCircle(cx, Y(cy), 9, rgb(240, 238, 215));
      spr->fillCircle(cx + 4, Y(cy) - 3, 8, skyRow[max(0, min(HORIZON - 1, cy - 3))]);
    }
  }
  float cl = isOvercast() ? light * 0.78f : light;  // rain clouds are greyer
  uint16_t cloudCol = shade(rgb(250, 250, 255), cl), cloudShade = shade(rgb(215, 222, 235), cl);
  for (auto &cl : clouds) {
    if (!inBand(cl.y - cl.size, cl.y + cl.size)) continue;
    int x = cl.x, y = Y(cl.y), s = cl.size;
    spr->fillEllipse(x, y + s / 3, s * 2, s * 2 / 3, cloudShade);
    spr->fillCircle(x - s, y, s * 2 / 3, cloudCol);
    spr->fillCircle(x + s / 3, y - s / 3, s, cloudCol);
    spr->fillCircle(x + s * 3 / 2, y + s / 6, s * 2 / 3, cloudCol);
  }
  // Halloween bats from dusk on
  if (season == SEASON_HALLOWEEN && (hourF > 17 || hourF < 6.5f))
    for (auto &b : bats) {
      int x = b.x, y = b.y + sinf(b.phase * 0.25f) * 6;
      if (!inBand(y - 5, y + 2)) continue;
      int wing = sinf(b.phase) * 3;
      uint16_t col = rgb(25, 18, 32);
      spr->drawLine(x - 6, Y(y - wing), x, Y(y), col);
      spr->drawLine(x, Y(y), x + 6, Y(y - wing), col);
      spr->fillRect(x - 1, Y(y) - 1, 3, 2, col);
    }
  for (auto &r : rockets)  // fireworks on their way up
    if (r.active && inBand(r.y, r.y + 5)) spr->fillRect(r.x, Y(r.y), 1, 4, rgb(255, 230, 180));
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
  if (snowyGround()) {  // a dusting of snow
    uint16_t sn = shade(rgb(245, 248, 255), snowLight());
    if (inBand(HORIZON, HORIZON + 3)) spr->fillRect(0, Y(HORIZON), W, 3, sn);
    for (int i = 0; i < N_TUFTS; i += 2)
      if (inBand(tufts[i].y, tufts[i].y + 2)) spr->fillEllipse(tufts[i].x + 8, Y(tufts[i].y + 1), 7, 2, sn);
  }
  drawGroundExtras();
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
  if (snowyGround()) {
    uint16_t sn = shade(rgb(250, 252, 255), snowLight());
    for (int x = -2; x < W; x += 16) spr->fillTriangle(x - 1, Y(127), x + 9, Y(127), x + 4, Y(122), sn);
  }
  if (season == SEASON_CHRISTMAS) {
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
  if (snowyGround()) {
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

void drawPartyHat(int hx, int by, uint16_t col) {
  spr->fillTriangle(hx - 6, by, hx + 6, by, hx, by - 15, col);
  spr->drawLine(hx - 4, by - 3, hx + 2, by - 9, rgb(255, 255, 255));
  spr->drawLine(hx - 2, by - 1, hx + 4, by - 7, rgb(255, 230, 80));
  spr->fillCircle(hx, by - 15, 2, rgb(255, 230, 80));
}

// Neckerchief hanging from the collar; 'lean' points the tip toward the chest
void drawBandana(int nx, int ny, int lean, uint16_t col, uint16_t dot, bool flag, bool back) {
  col = shade(col, light);
  if (back) {  // only the knot shows from behind
    spr->fillTriangle(nx - 3, ny, nx + 3, ny, nx, ny + 4, col);
    return;
  }
  spr->fillTriangle(nx - 6, ny, nx + 6, ny, nx + lean, ny + 9, col);
  uint16_t white = shade(rgb(250, 250, 250), light);
  if (flag) {  // stars and stripes
    spr->drawFastHLine(nx - 4, ny + 3, 8, white);
    spr->drawFastHLine(nx - 2 + lean / 2, ny + 6, 4, white);
    spr->fillRect(nx - 6, ny, 5, 3, shade(rgb(40, 60, 160), light));
    spr->drawPixel(nx - 5, ny + 1, white);
    spr->drawPixel(nx - 3, ny + 1, white);
  } else {
    spr->drawPixel(nx - 2, ny + 2, dot);
    spr->drawPixel(nx + 2, ny + 2, dot);
    spr->drawPixel(nx + lean / 2, ny + 5, dot);
  }
}

void drawScarf(int nx, int ny, int dir, bool fb, int idx) {
  const uint16_t cols[3] = {rgb(200, 40, 50), rgb(60, 120, 210), rgb(60, 160, 90)};
  uint16_t col = shade(cols[idx], light), stripe = shade(rgb(245, 245, 245), light);
  spr->fillRect(nx - 6, ny - 1, 12, 4, col);
  spr->drawFastHLine(nx - 6, ny + 1, 12, stripe);
  int tx = fb ? nx + 2 : nx - dir * 5;  // the loose end
  spr->fillRect(tx - 1, ny + 2, 4, 9, col);
  spr->drawFastHLine(tx - 1, ny + 5, 4, stripe);
  spr->drawFastHLine(tx - 1, ny + 8, 4, stripe);
}

void drawGhostSheet(const Corgi &c, int left, int top, int bob, int rowShift, bool sitting, bool fb) {
  uint16_t sheet = shade(rgb(245, 245, 250), light), hole = rgb(20, 20, 25);
  int x0, w, y0, h;
  if (fb) { x0 = left + 5 * S; w = 14 * S; y0 = top + (bob + 2) * S; h = 12 * S; }
  else if (sitting) { x0 = left + (c.dir > 0 ? 5 : 1) * S; w = 20 * S; y0 = top + 2 * S; h = 14 * S; }
  else { x0 = left + 1 * S; w = 24 * S; y0 = top + (rowShift + bob + 3) * S; h = 11 * S; }
  spr->fillRoundRect(x0, Y(y0), w, h, 8, sheet);
  for (int bx = x0 + 4; bx < x0 + w - 2; bx += 8) spr->fillCircle(bx, Y(y0 + h), 3, sheet);  // wavy hem
  if (fb && c.view == VIEW_FRONT) {
    spr->fillEllipse(left + 9 * S + 1, Y(top + (bob + 5) * S), 2, 3, hole);
    spr->fillEllipse(left + 14 * S + 1, Y(top + (bob + 5) * S), 2, 3, hole);
  } else if (!fb) {
    int ex = left + (c.dir > 0 ? 20 : 5) * S, ey = (sitting ? top : top + (rowShift + bob) * S) + 5 * S;
    spr->fillEllipse(ex, Y(ey), 2, 3, hole);
    spr->fillEllipse(ex + c.dir * 7, Y(ey), 2, 3, hole);
  }
}

void drawBatWings(const Corgi &c, int left, int top, int bob, int rowShift, bool sitting, bool fb) {
  uint16_t wing = rgb(45, 30, 60), inner = rgb(95, 60, 115);
  float flap = sinf(simT * 9) * 3;
  if (fb) {
    int cx = left + FB_CENTER_COL * S, sy = top + (bob + 7) * S;
    for (int sd = -1; sd <= 1; sd += 2) {
      spr->fillTriangle(cx + sd * 6, Y(sy), cx + sd * 6, Y(sy + 8), cx + sd * 22, Y(sy - 8 - flap), wing);
      spr->fillTriangle(cx + sd * 6, Y(sy + 8), cx + sd * 22, Y(sy - 8 - flap), cx + sd * 18, Y(sy + 4), wing);
    }
    return;
  }
  int body = sitting ? top : top + (rowShift + bob) * S;
  int bx = left + (c.dir > 0 ? 8 : SW - 1 - 8) * S, by = body + (sitting ? 10 : 8) * S;
  spr->fillTriangle(bx - 6, Y(by), bx + 6, Y(by), bx - c.dir * 10, Y(by - 16 - flap), wing);
  spr->fillTriangle(bx - c.dir * 2, Y(by), bx + c.dir * 10, Y(by), bx + c.dir * 2, Y(by - 12 + flap), inner);
}

// Seasonal outfit on one dog. Returns how far it rises above the head (for the name label).
int drawOutfit(const Corgi &c, int left, int top, int bob, int rowShift, bool sitting, bool fb, int hx, int headTop) {
  int idx = (int)(&c - corgis);  // 0 = Roux, 1 = tricolour, 2 = sable
  int body = top + (sitting ? 0 : rowShift + bob) * S;
  int nx, ny;  // collar point for bandanas and scarves
  if (fb) {
    nx = left + FB_CENTER_COL * S;
    ny = top + (bob + (c.view == VIEW_BACK ? 5 : 8)) * S;
  } else {
    nx = left + (c.dir > 0 ? 15 : SW - 1 - 15) * S + S / 2;
    ny = body + 8 * S;
  }
  int lean = fb ? 0 : c.dir * 3;
  bool back = fb && c.view == VIEW_BACK;
  const uint16_t partyCols[3] = {rgb(230, 60, 160), rgb(60, 150, 240), rgb(250, 170, 40)};
  switch (season) {
    case SEASON_CHRISTMAS: drawSantaHat(hx, Y(headTop + 1), c.dir); return 14;
    case SEASON_THANKSGIVING:
      if (!c.isRoux) return 0;
      drawPilgrimHat(spr, hx, Y(headTop + 1), 0.8f);
      return 14;
    case SEASON_NEW_YEAR: drawPartyHat(hx, Y(headTop + 1), partyCols[idx]); return 16;
    case SEASON_BIRTHDAY:
      if (!c.isRoux) return 0;
      drawPartyHat(hx, Y(headTop + 1), partyCols[0]);
      return 16;
    case SEASON_VALENTINE:
      drawBandana(nx, Y(ny), lean, rgb(240, 110, 160), rgb(255, 255, 255), false, back);
      return 0;
    case SEASON_ST_PATRICK:
      drawBandana(nx, Y(ny), lean, rgb(40, 160, 70), rgb(250, 250, 250), false, back);
      return 0;
    case SEASON_JULY4: drawBandana(nx, Y(ny), lean, rgb(210, 40, 50), 0, true, back); return 0;
    case SEASON_WINTER: drawScarf(nx, Y(ny), c.dir, fb, idx); return 0;
    case SEASON_HALLOWEEN:  // Roux the bat, a ghost, and a pumpkin-head
      if (idx == 0) drawBatWings(c, left, top, bob, rowShift, sitting, fb);
      else if (idx == 1) drawGhostSheet(c, left, top, bob, rowShift, sitting, fb);
      else {
        drawPumpkin(hx, headTop + 4, 6);
        return 12;
      }
      return 0;
    default: return 0;
  }
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
  if (c.state == SWIM && c.inPool && inBand(POOL_Y - POOL_RY, POOL_Y + POOL_RY))  // standing in the water
    spr->fillEllipse(POOL_X, Y(POOL_Y), POOL_RX - 4, POOL_RY - 3, shade(rgb(130, 205, 250), light));
  int outfitH = drawOutfit(c, left, top, bob, rowShift, sitting, fb, hx, headTop);

  if (c.hasBall && c.view == VIEW_FRONT && fb) {
    drawBall(left + FB_CENTER_COL * S, top + (bob + 8) * S);  // ball in the mouth, facing us
  } else if (c.hasBall && !fb) {
    drawBall(left + (c.dir > 0 ? 24 : 1) * S + 1, top + (bob + 9) * S);
  }  // seen from behind, the ball is hidden by the head
  if (c.isRoux && c.labelT > 0 && sitting) {
    int ly = headTop - 18 - outfitH;
    if (inBand(ly, ly + 12)) drawLabel(hx, Y(ly));
  }
}

void drawLeavesAndSnow() {
  if (leavesFalling()) {
    for (int i = 0; i < N_LEAVES; i++) {
      const Leaf &l = leaves[i];
      if (season == SEASON_FALL && (i & 1)) continue;  // early fall: just a few
      if (!inBand(l.y - 3, l.y + 3)) continue;
      uint16_t c = shade(l.col, light);
      if (l.landed || (int)(l.phase * 2) & 1) spr->fillRect(l.x, Y(l.y), 4, 2, c);
      else spr->fillRect(l.x + 1, Y(l.y) - 1, 2, 4, c);
    }
  }
  if (snowFalling())
    for (auto &f : snow)
      if (inBand(f.y, f.y + 2)) spr->fillRect(f.x, Y(f.y), f.size, f.size, rgb(250, 252, 255));
  if (isRaining()) {
    uint16_t rain = shade(rgb(175, 195, 225), max(light, 0.8f));
    for (auto &d : drops)
      if (inBand(d.y, d.y + 5)) spr->drawLine(d.x, Y(d.y), d.x - 1, Y(d.y + 4), rain);
  }
}

void drawHeart(int x, int y, uint16_t col) {
  spr->fillCircle(x - 2, y, 2, col);
  spr->fillCircle(x + 2, y, 2, col);
  spr->fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 6, col);
}

void drawEffects() {
  if (season == SEASON_SPRING)
    for (auto &b : butterflies) {
      if (!inBand(b.y - 3, b.y + 3)) continue;
      int w = sinf(b.phase) > 0 ? 3 : 1;  // wings open and close
      uint16_t col = shade(b.col, light);
      spr->fillEllipse(b.x - 2, Y(b.y), w, 2, col);
      spr->fillEllipse(b.x + 2, Y(b.y), w, 2, col);
      spr->drawFastVLine(b.x, Y(b.y) - 1, 3, rgb(40, 30, 30));
    }
  for (auto &sp : sparks) {
    if (sp.life <= 0 || !inBand(sp.y, sp.y + sp.size)) continue;
    spr->fillRect(sp.x, Y(sp.y), sp.size, sp.size, shade(sp.col, 0.4f + 0.6f * sp.life / sp.maxLife));
  }
  for (auto &h : hearts)
    if (h.life > 0 && inBand(h.y - 3, h.y + 6)) drawHeart(h.x, Y(h.y), h.life > 0.6f ? rgb(240, 70, 110) : rgb(245, 150, 175));
  for (auto &pf : puffs) {
    if (pf.life <= 0 || !inBand(pf.y - 6, pf.y + 6)) continue;
    int r = 1 + (1.2f - pf.life) * 3;
    spr->fillCircle(pf.x, Y(pf.y), r, shade(rgb(230, 235, 245), max(light, 0.75f) * (0.6f + 0.4f * pf.life)));
  }
}

void drawJack(int px, int py, int r) {
  drawPumpkin(px, py, r);
  if (!inBand(py - r * 2, py + 2)) return;
  uint16_t face = light < 0.85f ? rgb(255, 210, 80) : rgb(70, 40, 20);  // glows after dark
  int cy = Y(py - r * 3 / 4);
  spr->fillTriangle(px - r / 2 - 2, cy - 1, px - r / 2 + 2, cy - 1, px - r / 2, cy - 4, face);
  spr->fillTriangle(px + r / 2 - 2, cy - 1, px + r / 2 + 2, cy - 1, px + r / 2, cy - 4, face);
  spr->fillRect(px - r / 2 - 1, cy + 2, r + 2, 2, face);
  spr->drawPixel(px - 1, cy + 4, face);
  spr->drawPixel(px + 2, cy + 4, face);
}

void drawBanner(const char *text) {
  if (!inBand(136, 150)) return;
  int cx = 122, w = strlen(text) * 6 + 12;
  spr->fillRoundRect(cx - w / 2, Y(137), w, 12, 3, shade(rgb(252, 246, 230), light));
  spr->drawRoundRect(cx - w / 2, Y(137), w, 12, 3, shade(rgb(200, 50, 60), light));
  spr->setTextColor(shade(rgb(200, 40, 60), light));
  spr->drawCentreString(text, cx, Y(139), 1);
}

void drawBalloons() {
  const uint16_t cols[3] = {rgb(230, 50, 60), rgb(250, 200, 50), rgb(60, 140, 240)};
  int ax = HOUSE_X - 2, ay = HOUSE_BASE - 34;  // tied to the doghouse eave
  for (int i = 0; i < 3; i++) {
    int bx = ax - 20 + i * 12 + sinf(simT * 1.3f + i * 1.7f) * 3, by = 88 + i * 7;
    if (inBand(by, ay)) spr->drawLine(bx, Y(by + 9), ax, Y(ay), shade(rgb(240, 240, 240), light));
    if (!inBand(by - 9, by + 11)) continue;
    uint16_t c = shade(cols[i], light);
    spr->fillEllipse(bx, Y(by), 6, 8, c);
    spr->fillTriangle(bx - 1, Y(by + 8), bx + 1, Y(by + 8), bx, Y(by + 10), c);
    spr->drawPixel(bx - 2, Y(by - 4), rgb(255, 255, 255));
  }
}

// Decorations on and along the fence
void drawFenceDecor() {
  if (season == SEASON_VALENTINE && inBand(124, 132))  // hearts on the pickets
    for (int x = -2; x < W; x += 32) drawHeart(x + 4, Y(126), shade(rgb(235, 60, 100), light));
  if (season == SEASON_JULY4 && inBand(133, 143)) {  // bunting along the top rail
    const uint16_t cols[3] = {rgb(210, 40, 50), rgb(250, 250, 250), rgb(40, 60, 160)};
    for (int i = 0, x = 0; x < W; x += 12, i++) spr->fillTriangle(x, Y(134), x + 11, Y(134), x + 5, Y(142), shade(cols[i % 3], light));
  }
  if (season == SEASON_SPRING) {  // flowers along the bottom of the fence
    uint16_t stem = shade(rgb(50, 130, 50), light);
    for (auto &f : flowers) {
      if (f.y >= HORIZON + 10 || !inBand(f.y - 6, f.y)) continue;
      spr->drawFastVLine(f.x, Y(f.y - 3), 4, stem);
      spr->fillCircle(f.x, Y(f.y - 4), 2, shade(f.col, light));
      spr->drawPixel(f.x, Y(f.y - 4), rgb(255, 230, 90));
    }
  }
  if (season == SEASON_HALLOWEEN) {
    drawJack(26, HORIZON + 6, 11);
    drawJack(48, HORIZON + 8, 8);
  }
  if (season == SEASON_NEW_YEAR) drawBanner("HAPPY NEW YEAR!");
  if (season == SEASON_BIRTHDAY) {
    drawBanner("HAPPY BIRTHDAY ROUX!");
    drawCake();
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

void corgiSetWeather(int wmoCode, float tempF) {
  weatherCode = wmoCode;
  weatherTempF = tempF;
}

void corgiFrame(const tm &t) {
  if (!spr) return;
  uint32_t now = millis();
  float dt = min(0.1f, (now - lastMs) / 1000.0f);
  simT += dt;
  // Running full time for weeks: wrap the clock hourly so float timing stays precise
  if (simT > 3600) {
    simT -= 3600;
    for (auto &c : corgis) {
      c.blinkAt -= 3600;
      c.nextHeart -= 3600;
      c.nextPuff -= 3600;
    }
    nextRocket -= 3600;
    nextLightning -= 3600;
    lightningAt -= 3600;
    if (eggsRespawnAt >= 0) eggsRespawnAt -= 3600;
  }
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
    drawFenceDecor();
    drawDoghouse();
    if (season == SEASON_BIRTHDAY) drawBalloons();
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
    drawEffects();
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
