// CYD live dashboard
//   - Clock + date (NTP, US Central time)
//   - Weather for Houston, TX and Williston, ND (Open-Meteo, no API key)
//   - $CHRD quote with intraday sparkline (Yahoo Finance chart endpoint)
//
// Wi-Fi is configured from a phone via WiFiManager: on first boot the board
// opens a hotspot called "CYD-Dashboard". Hold the BOOT button while powering
// on to forget the saved network and re-open the hotspot.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "board.h"
#include "aquarium.h"
#include "corgis.h"
#include "season.h"
#include "ota.h"
#include "version.h"
#include "wifi_store.h"

// ---------- Config ----------
static const char *TZ_INFO = "CST6CDT,M3.2.0,M11.1.0";  // US Central (both cities)
static const char *TICKER = "CHRD";

struct City {
  const char *name;
  float lat, lon;
};
static const City CITIES[2] = {
    {"Houston, TX", 29.7604f, -95.3698f},
    {"Williston, ND", 48.1470f, -103.6180f},
};

static const uint32_t WEATHER_INTERVAL_MS = 10UL * 60 * 1000;
static const uint32_t QUOTE_INTERVAL_MS = 2UL * 60 * 1000;
static const uint32_t RETRY_INTERVAL_MS = 30UL * 1000;
static const uint32_t YARD_WX_INTERVAL_MS = 15UL * 60 * 1000;  // real weather for the corgi yard
static const uint32_t OTA_INTERVAL_MS = 6UL * 60 * 60 * 1000;  // update check
static const uint32_t HEALTHY_AFTER_MS = 2UL * 60 * 1000;     // new firmware "proven"
static const int BOOT_BUTTON = 0;

// Stock checks only run Mon-Fri inside this window (local Central time).
// NYSE/Nasdaq regular session is 8:30-3:00 CT; the extra 5 min catches the close.
static const int MARKET_START_MIN = 8 * 60 + 30;
static const int MARKET_END_MIN = 15 * 60 + 5;

// Backlight: full brightness by day, dimmed overnight (0-255)
static const uint8_t BRIGHT_DAY = 255;
static const uint8_t BRIGHT_NIGHT = 20;
static const int NIGHT_START_HOUR = 22;  // 10 PM
static const int NIGHT_END_HOUR = 6;     // 6 AM
static const int BL_CHANNEL = 0;

// Screen cycling: dashboard for a minute, then the fish tank for a bit.
// Tap the dashboard to jump to the tank early; tap the tank to feed the fish.
static const uint32_t DASHBOARD_MS = 60UL * 1000;
static const uint32_t SCENE_MS = 20UL * 1000;

// CYD: resistive touch (XPT2046) on its own SPI bus. Calibration is in board.h.
static const int T_IRQ = 36, T_MOSI = 32, T_MISO = 39, T_CLK = 25, T_CS = 33;

// ---------- Colours ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static const uint16_t C_BG = rgb(0, 0, 0);
static const uint16_t C_CARD = rgb(24, 28, 40);
static const uint16_t C_TEXT = rgb(255, 255, 255);
static const uint16_t C_DIM = rgb(140, 148, 165);
static const uint16_t C_ACCENT = rgb(90, 180, 255);
static const uint16_t C_UP = rgb(60, 220, 110);
static const uint16_t C_DOWN = rgb(255, 80, 80);
static const uint16_t C_SUN = rgb(255, 200, 40);
static const uint16_t C_CLOUD = rgb(200, 205, 215);
static const uint16_t C_RAIN = rgb(80, 150, 255);

TFT_eSPI tft;
#if !TOUCH_ON_TFT_BUS
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(T_CS, T_IRQ);
#endif

enum Mode { MODE_DASHBOARD, MODE_SCENE };
Mode mode = MODE_DASHBOARD;
uint32_t modeStartMs = 0;

// Animated scenes take turns between dashboard stretches
struct SceneOps {
  const char *name;
  bool (*begin)(TFT_eSPI &);
  void (*end)();
  void (*frame)(const tm &);
  void (*tap)(int, int);
  void (*capture)();
};
const SceneOps SCENES[] = {
    {"fish tank", aquariumBegin, aquariumEnd, aquariumFrame, aquariumTap, aquariumCaptureNextFrame},
    {"corgis", corgiBegin, corgiEnd, corgiFrame, corgiTap, corgiCaptureNextFrame},
};
const int N_SCENES = sizeof(SCENES) / sizeof(SCENES[0]);
int scene = 0, nextScene = 0;

// Per-board settings kept in flash, so one firmware suits every board:
//   invert: colour inversion (CYD panels differ; the office board needs it on)
//   corgis: include the corgi yard in the rotation (off unless switched on)
#ifdef TFT_INVERSION_ON
static const bool INVERT_DEFAULT = true;
#else
static const bool INVERT_DEFAULT = false;
#endif
bool invertColours = INVERT_DEFAULT;
bool corgisEnabled = false;
int onlyScene = -1;  // >= 0: show just this scene, full time (no dashboard)
uint32_t lastOtaMs = 0;

// Where this board lives: real weather in the corgi yard, the clock's time zone,
// and Roux's birthday. Set over USB before a board leaves (serial L / Z / R).
float homeLat = 29.7604f, homeLon = -95.3698f;  // default: Houston
String tzInfo = TZ_INFO;
int birthMonth = 0, birthDay = 0;  // 0 = not known yet
uint32_t lastYardWxMs = 0;
bool yardWxDue = true, yardWxOverride = false;

void loadDeviceSettings() {
  Preferences p;
  p.begin("device", true);
  invertColours = p.getBool("invert", INVERT_DEFAULT);
  corgisEnabled = p.getBool("corgis", false);
  onlyScene = p.getInt("only", -1);
  if (p.isKey("lat")) homeLat = p.getFloat("lat");
  if (p.isKey("lon")) homeLon = p.getFloat("lon");
  if (p.isKey("tz")) tzInfo = p.getString("tz");
  birthMonth = p.getUChar("bmon", 0);
  birthDay = p.getUChar("bday", 0);
  p.end();
  setBirthday(birthMonth, birthDay);
}

const char *apName() { return onlyScene == 1 ? "Corgi-Yard" : "CYD-Dashboard"; }

void saveDeviceSetting(const char *key, bool value) {
  Preferences p;
  p.begin("device", false);
  p.putBool(key, value);
  p.end();
}

void saveOnlyScene(int i) {
  Preferences p;
  p.begin("device", false);
  p.putInt("only", i);
  p.end();
}

bool sceneEnabled(int i) { return i != 1 || corgisEnabled; }

int nextEnabledScene(int after) {
  for (int k = 1; k <= N_SCENES; k++) {
    int i = (after + k) % N_SCENES;
    if (sceneEnabled(i)) return i;
  }
  return 0;
}

// ---------- State ----------
struct Weather {
  bool valid = false;
  float temp, hi, lo, wind;
  int humidity, code;
  bool isDay;
};
Weather weather[2];

static const int MAX_POINTS = 64;
struct Quote {
  bool valid = false;
  float price, prevClose;
  long marketTime, openTime, closeTime;
  float points[MAX_POINTS];
  int nPoints = 0;
};
Quote quote;

uint32_t lastWeatherMs = 0, lastQuoteMs = 0;
bool weatherDue = true, quoteDue = true;
String statusMsg;

// ---------- Layout ----------
static const int CARD_Y = 70, CARD_W = 154, CARD_H = 96;
static const int CARD_X[2] = {4, 162};
static const int TICK_X = 4, TICK_Y = 172, TICK_W = 312, TICK_H = 64;

// ---------- HTTP ----------
bool httpGet(const String &url, String &body) {
  WiFiClientSecure client;
  client.setInsecure();  // public read-only data; skip cert pinning
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("Mozilla/5.0 (CYD-Dashboard)");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code == HTTP_CODE_OK) body = http.getString();
  else Serial.printf("GET %s -> %d\n", url.c_str(), code);
  http.end();
  return code == HTTP_CODE_OK;
}

bool fetchWeather() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(CITIES[0].lat, 4) + "," + String(CITIES[1].lat, 4);
  url += "&longitude=" + String(CITIES[0].lon, 4) + "," + String(CITIES[1].lon, 4);
  url += "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,is_day"
         "&daily=temperature_2m_max,temperature_2m_min"
         "&temperature_unit=fahrenheit&wind_speed_unit=mph"
         "&timezone=America%2FChicago&forecast_days=1";
  String body;
  if (!httpGet(url, body)) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() < 2) return false;
  for (int i = 0; i < 2; i++) {
    JsonObject cur = arr[i]["current"];
    JsonObject daily = arr[i]["daily"];
    weather[i].temp = cur["temperature_2m"];
    weather[i].code = cur["weather_code"];
    weather[i].humidity = cur["relative_humidity_2m"];
    weather[i].wind = cur["wind_speed_10m"];
    weather[i].isDay = cur["is_day"] | 1;
    weather[i].hi = daily["temperature_2m_max"][0];
    weather[i].lo = daily["temperature_2m_min"][0];
    weather[i].valid = true;
  }
  return true;
}

// Current conditions where the board lives, for the corgi yard
bool fetchYardWeather() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(homeLat, 4) +
               "&longitude=" + String(homeLon, 4) + "&current=temperature_2m,weather_code&temperature_unit=fahrenheit";
  String body;
  if (!httpGet(url, body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonObject cur = doc["current"];
  if (cur.isNull()) return false;
  int code = cur["weather_code"] | -1;
  float temp = cur["temperature_2m"] | 70.0f;
  corgiSetWeather(code, temp);
  Serial.printf("yard weather: code %d, %.0fF\n", code, temp);
  return true;
}

bool fetchQuote() {
  String url = String("https://query1.finance.yahoo.com/v8/finance/chart/") + TICKER +
               "?interval=15m&range=1d";
  String body;
  if (!httpGet(url, body)) return false;

  JsonDocument filter;
  JsonObject f = filter["chart"]["result"][0].to<JsonObject>();
  f["meta"]["regularMarketPrice"] = true;
  f["meta"]["chartPreviousClose"] = true;
  f["meta"]["regularMarketTime"] = true;
  f["meta"]["currentTradingPeriod"]["regular"] = true;
  f["indicators"]["quote"][0]["close"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonObject r = doc["chart"]["result"][0];
  if (r.isNull() || r["meta"]["regularMarketPrice"].isNull()) return false;

  quote.price = r["meta"]["regularMarketPrice"];
  quote.prevClose = r["meta"]["chartPreviousClose"];
  quote.marketTime = r["meta"]["regularMarketTime"];
  quote.openTime = r["meta"]["currentTradingPeriod"]["regular"]["start"];
  quote.closeTime = r["meta"]["currentTradingPeriod"]["regular"]["end"];
  quote.nPoints = 0;
  for (JsonVariant v : r["indicators"]["quote"][0]["close"].as<JsonArray>()) {
    if (v.isNull() || quote.nPoints >= MAX_POINTS) continue;
    quote.points[quote.nPoints++] = v.as<float>();
  }
  quote.valid = true;
  return true;
}

// ---------- Weather helpers ----------
const char *weatherText(int c) {
  if (c == 0) return "Clear";
  if (c == 1) return "Mostly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "--";
}

void drawSun(int cx, int cy, int r) {
  for (int a = 0; a < 360; a += 45) {
    float rad = a * DEG_TO_RAD;
    tft.drawWideLine(cx + cos(rad) * (r + 3), cy + sin(rad) * (r + 3),
                     cx + cos(rad) * (r + 7), cy + sin(rad) * (r + 7), 2, C_SUN, C_CARD);
  }
  tft.fillCircle(cx, cy, r, C_SUN);
}

void drawMoon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, C_CLOUD);
  tft.fillCircle(cx + r / 2, cy - r / 3, r - 1, C_CARD);
}

void drawCloud(int cx, int cy, uint16_t col) {
  tft.fillCircle(cx - 9, cy + 3, 7, col);
  tft.fillCircle(cx + 1, cy - 3, 10, col);
  tft.fillCircle(cx + 11, cy + 3, 7, col);
  tft.fillRect(cx - 9, cy + 3, 20, 8, col);
}

void drawWeatherIcon(int cx, int cy, int code, bool isDay) {
  if (code <= 1) {
    isDay ? drawSun(cx, cy, 10) : drawMoon(cx, cy, 11);
  } else if (code == 2) {
    isDay ? drawSun(cx + 7, cy - 7, 7) : drawMoon(cx + 7, cy - 7, 8);
    drawCloud(cx - 2, cy + 4, C_CLOUD);
  } else if (code == 3) {
    drawCloud(cx, cy, C_CLOUD);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 4; i++) tft.fillRoundRect(cx - 16 + (i % 2) * 4, cy - 10 + i * 7, 28, 3, 1, C_CLOUD);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawCloud(cx, cy - 5, C_CLOUD);
    for (int i = 0; i < 3; i++) tft.fillCircle(cx - 8 + i * 8, cy + 14 + (i % 2) * 4, 2, C_TEXT);
  } else if (code >= 95) {
    drawCloud(cx, cy - 5, C_DIM);
    tft.fillTriangle(cx + 2, cy + 4, cx - 5, cy + 14, cx + 1, cy + 14, C_SUN);
    tft.fillTriangle(cx - 1, cy + 12, cx + 5, cy + 12, cx - 3, cy + 22, C_SUN);
  } else {  // drizzle / rain / showers
    drawCloud(cx, cy - 5, C_CLOUD);
    for (int i = 0; i < 3; i++) tft.drawWideLine(cx - 6 + i * 8, cy + 10, cx - 9 + i * 8, cy + 18, 2, C_RAIN, C_CARD);
  }
}

// ---------- Drawing ----------
int lastMin = -1, lastDay = -1;  // reset to force a full clock redraw

void drawClock(const tm &t) {
  char buf[16];

  if (t.tm_min != lastMin) {
    lastMin = t.tm_min;
    int h12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h12, t.tm_min);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setTextPadding(tft.textWidth("88:88", 7));
    tft.drawRightString(buf, 8 + tft.textWidth("88:88", 7), 8, 7);
    tft.setTextPadding(0);
    tft.setTextColor(C_ACCENT, C_BG);
    tft.drawString(t.tm_hour < 12 ? "AM" : "PM", 176, 10, 4);
  }

  snprintf(buf, sizeof(buf), ":%02d", t.tm_sec);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextPadding(tft.textWidth(":88", 4));
  tft.drawString(buf, 176, 32, 4);
  tft.setTextPadding(0);

  if (t.tm_mday != lastDay) {
    lastDay = t.tm_mday;
    tft.fillRect(222, 6, 96, 56, C_BG);
    strftime(buf, sizeof(buf), "%A", &t);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawRightString(buf, 314, 12, 2);
    strftime(buf, sizeof(buf), "%b %e", &t);
    tft.setTextColor(C_TEXT, C_BG);
    tft.drawRightString(buf, 314, 32, 4);
  }
}

void drawWeatherCard(int i) {
  int x = CARD_X[i], y = CARD_Y;
  tft.fillRoundRect(x, y, CARD_W, CARD_H, 8, C_CARD);
  tft.setTextColor(C_ACCENT, C_CARD);
  tft.drawString(CITIES[i].name, x + 8, y + 5, 2);

  const Weather &w = weather[i];
  if (!w.valid) {
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString("loading...", x + 8, y + 40, 2);
    return;
  }

  drawWeatherIcon(x + 28, y + 44, w.code, w.isDay);

  // Big temperature, right-aligned, with a drawn degree symbol + F
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(w.temp));
  tft.setTextColor(C_TEXT, C_CARD);
  int right = x + CARD_W - 22;
  tft.drawRightString(buf, right, y + 22, 6);
  tft.drawCircle(right + 5, y + 27, 3, C_TEXT);
  tft.drawString("F", right + 10, y + 26, 2);

  tft.setTextColor(C_TEXT, C_CARD);
  tft.drawString(weatherText(w.code), x + 8, y + 72, 2);
  snprintf(buf, sizeof(buf), "H%d L%d", (int)lroundf(w.hi), (int)lroundf(w.lo));
  tft.setTextColor(C_DIM, C_CARD);
  tft.drawRightString(buf, x + CARD_W - 8, y + 72, 2);
}

void drawTicker() {
  tft.fillRoundRect(TICK_X, TICK_Y, TICK_W, TICK_H, 8, C_CARD);
  int x = TICK_X + 8, y = TICK_Y;

  tft.setTextColor(C_ACCENT, C_CARD);
  tft.drawString(String("$") + TICKER, x, y + 5, 2);

  if (!quote.valid) {
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString("loading...", x, y + 28, 2);
  } else {
    time_t now = time(nullptr);
    bool open = now >= quote.openTime && now < quote.closeTime;
    uint16_t badge = open ? C_UP : C_DIM;
    tft.drawRoundRect(x + 52, y + 6, 46, 14, 3, badge);
    tft.setTextColor(badge, C_CARD);
    tft.drawCentreString(open ? "OPEN" : "CLOSED", x + 75, y + 9, 1);

    float chg = quote.price - quote.prevClose;
    float pct = quote.prevClose ? chg / quote.prevClose * 100 : 0;
    uint16_t col = chg >= 0 ? C_UP : C_DOWN;

    char buf[32];
    snprintf(buf, sizeof(buf), "$%.2f", quote.price);
    tft.setTextColor(C_TEXT, C_CARD);
    tft.drawString(buf, x, y + 22, 4);
    snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)", chg, pct);
    tft.setTextColor(col, C_CARD);
    tft.drawString(buf, x, y + 46, 2);

    // Sparkline with dashed previous-close baseline
    int sx = TICK_X + 150, sy = y + 8, sw = TICK_W - 150 - 10, sh = 40;
    if (quote.nPoints >= 2) {
      float lo = quote.prevClose, hi = quote.prevClose;
      for (int i = 0; i < quote.nPoints; i++) {
        lo = min(lo, quote.points[i]);
        hi = max(hi, quote.points[i]);
      }
      if (hi - lo < 0.01f) { hi += 0.5f; lo -= 0.5f; }
      auto py = [&](float v) { return sy + sh - (int)((v - lo) / (hi - lo) * sh); };

      int by = py(quote.prevClose);
      for (int px = sx; px < sx + sw; px += 6) tft.drawFastHLine(px, by, 3, C_DIM);

      // Spread points across a full 6.5h session (26 x 15min bars)
      int slots = max(quote.nPoints, 26);
      for (int i = 1; i < quote.nPoints; i++) {
        int x0 = sx + (i - 1) * sw / (slots - 1), x1 = sx + i * sw / (slots - 1);
        tft.drawWideLine(x0, py(quote.points[i - 1]), x1, py(quote.points[i]), 2, col, C_CARD);
      }
    }
  }

  tft.setTextColor(C_DIM, C_CARD);
  tft.drawRightString(statusMsg, TICK_X + TICK_W - 8, y + TICK_H - 11, 1);
}

void setStatus() {
  char buf[40];
  struct tm t;
  getLocalTime(&t, 0);
  strftime(buf, sizeof(buf), "updated %I:%M %p", &t);
  statusMsg = buf;
}

void showMessage(const String &title, const String &l1, const String &l2 = "") {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawCentreString(title, 160, 70, 4);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawCentreString(l1, 160, 115, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawCentreString(l2, 160, 140, 2);
}

// ---------- Schedules ----------
bool inMarketWindow(const tm &t) {
  if (t.tm_wday == 0 || t.tm_wday == 6) return false;  // weekend
  int m = t.tm_hour * 60 + t.tm_min;
  return m >= MARKET_START_MIN && m < MARKET_END_MIN;
}

void setBacklight(int level) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, level);  // core 3.x addresses LEDC by pin
#else
  ledcWrite(BL_CHANNEL, level);
#endif
}

void updateBacklight(const tm &t) {
  static int current = -1;
  bool night = t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR;
  int level = night ? BRIGHT_NIGHT : BRIGHT_DAY;
  if (level != current) {
    current = level;
    setBacklight(level);
    Serial.printf("backlight -> %d (%s)\n", level, night ? "night" : "day");
  }
}

void drawDashboard() {
  tft.fillScreen(C_BG);
  tft.drawFastHLine(8, 64, 304, C_CARD);
  lastMin = lastDay = -1;
  struct tm t;
  if (getLocalTime(&t, 0)) drawClock(t);
  drawWeatherCard(0);
  drawWeatherCard(1);
  drawTicker();
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(115200);
  otaBootCheck();  // before anything else, so a crashing update can roll back
  Serial.printf("firmware v%d, self-update %s\n", FW_VERSION, otaEnabled() ? "on" : "off");
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
#ifdef AUDIO_ENABLE_PIN
  pinMode(AUDIO_ENABLE_PIN, OUTPUT);
  digitalWrite(AUDIO_ENABLE_PIN, HIGH);  // keep the speaker amp off (no hiss)
#endif
  tft.init();
  tft.setRotation(1);
  loadDeviceSettings();
  tft.invertDisplay(invertColours);
  tft.fillScreen(C_BG);
  if (UI_X || UI_Y) {
    // Bigger screen: frame the 320x240 layout and confine all drawing to it
    tft.drawRoundRect(UI_X - 3, UI_Y - 3, UI_W + 6, UI_H + 6, 6, C_CARD);
    tft.setViewport(UI_X, UI_Y, UI_W, UI_H);
  }
  Serial.printf("board: %s, invert %s, corgis %s, only %s\n", BOARD_NAME, invertColours ? "on" : "off",
                corgisEnabled ? "on" : "off", onlyScene >= 0 ? SCENES[onlyScene].name : "-");
  Serial.printf("home: %.4f, %.4f  tz: %s  birthday: %d-%d\n", homeLat, homeLon, tzInfo.c_str(), birthMonth,
                birthDay);

  // Take over the backlight pin with PWM so it can be dimmed
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(TFT_BL, 5000, 8, BL_CHANNEL);
#else
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
#endif
  setBacklight(BRIGHT_DAY);

  struct tm t = {};
#ifndef PREVIEW_OFFLINE  // preview build: artwork checks without touching Wi-Fi
  WiFiManager wm;
  if (digitalRead(BOOT_BUTTON) == LOW) {
    showMessage("Resetting Wi-Fi", "Forgetting saved network...");
    wm.resetSettings();
    knownWifiForgetAll();
    delay(1000);
  } else {
    knownWifiRememberSaved();
  }
  wm.setAPCallback([](WiFiManager *) {
    showMessage("Wi-Fi setup", String("On your phone, join Wi-Fi \"") + apName() + "\"",
                "then pick your home network");
  });
  showMessage("Connecting", "Joining saved Wi-Fi...", "firmware v" + String(FW_VERSION));
  if (!knownWifiConnect()) {
    // With networks on file, retry after a while rather than waiting in setup
    // forever, in case the router was just briefly down
    if (knownWifiCount() > 0) wm.setConfigPortalTimeout(300);
    if (!wm.autoConnect(apName())) ESP.restart();
  }
  knownWifiRememberConnected();

  showMessage("Connected", WiFi.SSID(), "Syncing time...");
  configTzTime(tzInfo.c_str(), "pool.ntp.org", "time.nist.gov");
  while (!getLocalTime(&t, 1000)) Serial.println("waiting for NTP");

  showMessage("Connected", WiFi.SSID(), "Checking for updates...");
  otaCheckForUpdate(tft);
#endif

#if !TOUCH_ON_TFT_BUS
  touchSpi.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);
#endif

  drawDashboard();
  modeStartMs = millis();
  lastOtaMs = millis();
  Serial.printf("season: %s\n", seasonName(currentSeason(t)));
}

// Returns true once per new tap, with screen coordinates
bool readTap(int &x, int &y) {
  static bool wasDown = false;
#if TOUCH_ON_TFT_BUS
  // Touch shares the display's SPI bus, so go through TFT_eSPI
  bool down = tft.getTouchRawZ() > 350;
#else
  bool down = touch.tirqTouched() && touch.touched();
#endif
  bool tap = down && !wasDown;
  wasDown = down;
  if (!tap) return false;
  int rx, ry;
#if TOUCH_ON_TFT_BUS
  uint16_t ux, uy;
  tft.getTouchRaw(&ux, &uy);
  rx = ux;
  ry = uy;
#else
  TS_Point p = touch.getPoint();
  rx = p.x;
  ry = p.y;
#endif
  int ax = TOUCH_SWAP_XY ? ry : rx, ay = TOUCH_SWAP_XY ? rx : ry;
  int sx = map(ax, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
  int sy = map(ay, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
  x = constrain(sx - UI_X, 0, UI_W - 1);  // into the 320x240 layout's coordinates
  y = constrain(sy - UI_Y, 0, UI_H - 1);
  Serial.printf("tap raw=(%d,%d) screen=(%d,%d) ui=(%d,%d)\n", rx, ry, sx, sy, x, y);
  return true;
}

void enterScene(int i) {
  if (mode == MODE_SCENE) SCENES[scene].end();
  if (!SCENES[i].begin(tft)) {
    Serial.printf("%s: not enough RAM, staying on dashboard\n", SCENES[i].name);
    mode = MODE_DASHBOARD;
    modeStartMs = millis();
    drawDashboard();
    return;
  }
  scene = i;
  nextScene = nextEnabledScene(i);
  mode = MODE_SCENE;
  modeStartMs = millis();
}

void enterDashboard() {
  if (mode == MODE_SCENE) SCENES[scene].end();  // hand the buffer's RAM back before any HTTPS calls
  mode = MODE_DASHBOARD;
  modeStartMs = millis();
  drawDashboard();
}

// Single-letter serial commands, handy for testing from the PC:
//   f = fish tank now   p = corgis (pups) now   d = dashboard now
//   h = hold current screen (toggle)   s = screenshot the scene (tools/screenshot.py)
//   a / n / t / c = season auto / none / thanksgiving / christmas
//   u = check for a firmware update now   v = print firmware version
//   i = toggle colour inversion (saved)   k = toggle corgis in the rotation (saved)
//   b = simulated tap (throw the ball / drop fish food)
// Line commands (end with Enter), saved on the board unless noted:
//   L29.7604,-95.3698  home location for real weather     Z<POSIX TZ>  time zone, e.g. ZEST5EDT,M3.2.0,M11.1.0
//   R07-15  Roux's birthday (R0 = unknown)                 S<n>  try season n from season.h, S-1 = calendar (not saved)
//   W63,55  pretend weather code,tempF (not saved)          W-1   back to real weather
//   ?  print this board's settings
//   o = toggle corgis-only mode: corgis full time, no dashboard (saved)
bool holdScreen = false;
bool otaCheckRequested = false;

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'f': if (mode != MODE_SCENE || scene != 0) enterScene(0); break;
      case 'p': if (mode != MODE_SCENE || scene != 1) enterScene(1); break;
      case 'd': if (mode != MODE_DASHBOARD) enterDashboard(); break;
      case 'h': holdScreen = !holdScreen; Serial.printf("hold: %s\n", holdScreen ? "on" : "off"); break;
      case 's':
        if (mode != MODE_SCENE) enterScene(nextScene);
        if (mode == MODE_SCENE) SCENES[scene].capture();
        break;
      case 'b':  // simulated tap near the back of the screen (throws the ball / drops food)
        if (mode == MODE_SCENE) SCENES[scene].tap(160, 196);
        break;
      case 'o':
        onlyScene = onlyScene >= 0 ? -1 : 1;
        saveOnlyScene(onlyScene);
        Serial.printf("corgis only: %s (saved)\n", onlyScene >= 0 ? "on" : "off");
        if (onlyScene >= 0) enterScene(onlyScene);
        else enterDashboard();
        break;
      case 'u': otaCheckRequested = true; break;
      case 'v': Serial.printf("firmware v%d\n", FW_VERSION); break;
      case 'i':
        invertColours = !invertColours;
        saveDeviceSetting("invert", invertColours);
        tft.invertDisplay(invertColours);
        Serial.printf("invert: %s (saved)\n", invertColours ? "on" : "off");
        break;
      case 'k':
        corgisEnabled = !corgisEnabled;
        saveDeviceSetting("corgis", corgisEnabled);
        if (!sceneEnabled(nextScene)) nextScene = nextEnabledScene(nextScene);
        Serial.printf("corgis: %s (saved)\n", corgisEnabled ? "on" : "off");
        break;
      case 'L': {
        String v = Serial.readStringUntil('\n');
        int comma = v.indexOf(',');
        if (comma < 0) {
          Serial.println("usage: L<lat>,<lon>");
          break;
        }
        homeLat = v.substring(0, comma).toFloat();
        homeLon = v.substring(comma + 1).toFloat();
        Preferences p;
        p.begin("device", false);
        p.putFloat("lat", homeLat);
        p.putFloat("lon", homeLon);
        p.end();
        yardWxDue = true;
        Serial.printf("home: %.4f, %.4f (saved)\n", homeLat, homeLon);
        break;
      }
      case 'Z': {
        String v = Serial.readStringUntil('\n');
        v.trim();
        if (!v.length()) break;
        tzInfo = v;
        Preferences p;
        p.begin("device", false);
        p.putString("tz", tzInfo);
        p.end();
        setenv("TZ", tzInfo.c_str(), 1);
        tzset();
        Serial.printf("time zone: %s (saved)\n", tzInfo.c_str());
        break;
      }
      case 'R': {
        String v = Serial.readStringUntil('\n');
        int dash = v.indexOf('-');
        int m = v.toInt(), d = dash >= 0 ? v.substring(dash + 1).toInt() : 0;
        if (m < 1 || m > 12 || d < 1 || d > 31) m = d = 0;  // "R0" (or anything invalid) clears it
        birthMonth = m;
        birthDay = d;
        Preferences p;
        p.begin("device", false);
        p.putUChar("bmon", m);
        p.putUChar("bday", d);
        p.end();
        setBirthday(m, d);
        Serial.printf("birthday: %d-%d (saved)\n", m, d);
        break;
      }
      case 'S': {
        int n = Serial.readStringUntil('\n').toInt();
        setSeasonOverride(n >= 0 && n < SEASON_COUNT ? n : -1);
        struct tm t;
        getLocalTime(&t, 0);
        Serial.printf("season: %s%s\n", seasonName(currentSeason(t)), n < 0 ? " (calendar)" : "");
        break;
      }
      case 'W': {
        String v = Serial.readStringUntil('\n');
        int code = v.toInt(), comma = v.indexOf(',');
        if (code >= 0) {
          yardWxOverride = true;
          corgiSetWeather(code, comma >= 0 ? v.substring(comma + 1).toFloat() : 70);
          Serial.printf("weather: pretending code %d\n", code);
        } else {
          yardWxOverride = false;
          yardWxDue = true;
          Serial.println("weather: real");
        }
        break;
      }
      case '?':
        Serial.printf("firmware v%d, board %s, invert %s, corgis %s, only %d\n", FW_VERSION, BOARD_NAME,
                      invertColours ? "on" : "off", corgisEnabled ? "on" : "off", onlyScene);
        Serial.printf("home: %.4f, %.4f  tz: %s  birthday: %d-%d\n", homeLat, homeLon, tzInfo.c_str(), birthMonth,
                      birthDay);
        break;
      case 'a': case 'n': case 't': case 'c': {
        setSeasonOverride(c == 'a' ? -1 : c == 'n' ? SEASON_NONE : c == 't' ? SEASON_THANKSGIVING : SEASON_CHRISTMAS);
        struct tm t;
        getLocalTime(&t, 0);
        Serial.printf("season: %s%s\n", seasonName(currentSeason(t)), c == 'a' ? " (auto)" : "");
        break;
      }
    }
  }
}

// Refresh the corgi yard's real weather every 15 minutes (only on boards that show corgis)
void updateYardWeather() {
  if (yardWxOverride || !(corgisEnabled || onlyScene == 1)) return;
  if (!yardWxDue && millis() - lastYardWxMs < YARD_WX_INTERVAL_MS) return;
  bool ok = WiFi.isConnected() && fetchYardWeather();
  lastYardWxMs = ok ? millis() : millis() - YARD_WX_INTERVAL_MS + 2 * 60 * 1000;  // retry in 2 min
  yardWxDue = false;
}

void maintainWifi(uint32_t ms) {
  static uint32_t lastReconnectMs = 0;
  if (!WiFi.isConnected() && ms - lastReconnectMs > 10000) {
    lastReconnectMs = ms;
    WiFi.reconnect();
  }
}

void loop() {
  static time_t lastSec = 0;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  uint32_t ms = millis();
  if (now != lastSec) {
    lastSec = now;
    updateBacklight(t);
  }

  handleSerial();
  static bool healthy = false;
  if (!healthy && ms > HEALTHY_AFTER_MS) {
    healthy = true;
    otaMarkHealthy();
  }

  int tx, ty;
  bool tapped = readTap(tx, ty);
  if (holdScreen) modeStartMs = ms;

  if (mode == MODE_SCENE) {
    if (tapped) SCENES[scene].tap(tx, ty);
    SCENES[scene].frame(t);
    if (onlyScene >= 0) {
      // Full-time scene: there's no dashboard stretch, so check for updates
      // and keep Wi-Fi alive from here (the brief pause is every 6 hours)
      if (otaCheckRequested || millis() - lastOtaMs >= OTA_INTERVAL_MS) {
        otaCheckRequested = false;
        lastOtaMs = millis();
        otaCheckForUpdate(tft);  // reboots if it installs something
      }
      updateYardWeather();  // a brief pause every 15 minutes
      maintainWifi(ms);
      return;
    }
    if (millis() - modeStartMs >= SCENE_MS) enterDashboard();
    return;  // no network work while animating, so frames never stall
  }

  if (onlyScene >= 0) {
    enterScene(onlyScene);
    if (mode == MODE_SCENE) return;
  }

  if (tapped || ms - modeStartMs >= DASHBOARD_MS) {
    enterScene(nextScene);
    if (mode == MODE_SCENE) return;
  }

  // Update checks run on the dashboard, when the scenes' RAM is free
  if (otaCheckRequested || millis() - lastOtaMs >= OTA_INTERVAL_MS) {
    otaCheckRequested = false;
    lastOtaMs = millis();
    otaCheckForUpdate(tft);  // reboots if it installs something
    drawDashboard();
  }

  static time_t lastClockSec = 0;
  if (now != lastClockSec) {
    lastClockSec = now;
    drawClock(t);
  }

  if (weatherDue || ms - lastWeatherMs >= WEATHER_INTERVAL_MS) {
    bool ok = WiFi.isConnected() && fetchWeather();
    Serial.printf("weather: %s\n", ok ? "ok" : "FAILED");
    // On failure, retry sooner by backdating the timer
    lastWeatherMs = ok ? ms : ms - WEATHER_INTERVAL_MS + RETRY_INTERVAL_MS;
    weatherDue = false;
    if (ok) { drawWeatherCard(0); drawWeatherCard(1); }
  }

  // Always fetch once at boot; after that only during market hours
  // (or keep retrying if we've never had a good quote).
  bool quoteWanted = inMarketWindow(t) || !quote.valid;
  if (quoteDue || (quoteWanted && ms - lastQuoteMs >= QUOTE_INTERVAL_MS)) {
    bool ok = WiFi.isConnected() && fetchQuote();
    Serial.printf("quote: %s\n", ok ? "ok" : "FAILED");
    lastQuoteMs = ok ? ms : ms - QUOTE_INTERVAL_MS + RETRY_INTERVAL_MS;
    quoteDue = false;
    if (ok) setStatus();
    else statusMsg = "quote update failed";
    drawTicker();
  }

  updateYardWeather();
  maintainWifi(ms);
  delay(20);
}
