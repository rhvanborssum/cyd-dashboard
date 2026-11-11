#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "ca_certs.h"
#include "ota_config.h"
#include "version.h"

namespace {
const int MAX_TRIAL_BOOTS = 3;
const uint16_t C_BG = 0x0000, C_TEXT = 0xFFFF, C_ACCENT = 0x5DBF, C_BAR = 0x2C4A, C_FAIL = 0xFA8A;

void screen(TFT_eSPI &tft, const String &title, const String &line, uint16_t titleCol = C_ACCENT) {
  tft.fillScreen(C_BG);
  tft.setTextColor(titleCol, C_BG);
  tft.drawCentreString(title, 160, 70, 4);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawCentreString(line, 160, 110, 2);
}
}  // namespace

bool otaEnabled() { return sizeof(OTA_MANIFEST_URL) > 1; }

void otaBootCheck() {
  Preferences p;
  p.begin("ota", false);
  if (p.getBool("trial", false)) {
    int tries = p.getInt("tries", 0) + 1;
    p.putInt("tries", tries);
    Serial.printf("ota: trial boot %d/%d of v%d\n", tries, MAX_TRIAL_BOOTS, FW_VERSION);
    if (tries > MAX_TRIAL_BOOTS) {
      // New firmware never got healthy: go back to the slot we updated from
      const esp_partition_t *prev = esp_ota_get_next_update_partition(nullptr);
      p.putInt("bad", FW_VERSION);
      p.putBool("trial", false);
      p.end();
      if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) {
        Serial.printf("ota: v%d failed, rolling back to %s\n", FW_VERSION, prev->label);
        delay(200);
        ESP.restart();
      }
      Serial.println("ota: rollback not possible");
      return;
    }
  }
  p.end();
}

void otaMarkHealthy() {
  Preferences p;
  p.begin("ota", false);
  if (p.getBool("trial", false)) {
    p.putBool("trial", false);
    p.putInt("tries", 0);
    Serial.printf("ota: v%d is healthy, keeping it\n", FW_VERSION);
  }
  p.end();
}

void otaCheckForUpdate(TFT_eSPI &tft) {
  if (!otaEnabled() || !WiFi.isConnected()) return;

  // 1. Manifest (github.com redirects to its release-asset host; both verified)
  int latest = 0;
  String url;
  {
    WiFiClientSecure client;
    client.setCACert(OTA_ROOT_CAS);
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, OTA_MANIFEST_URL)) return;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      Serial.printf("ota: manifest fetch -> %d\n", code);
      http.end();
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) {
      Serial.println("ota: bad manifest");
      return;
    }
    latest = doc["version"] | 0;
    url = doc["url"] | "";
  }

  Preferences p;
  p.begin("ota", true);
  int bad = p.getInt("bad", 0);
  p.end();
  Serial.printf("ota: running v%d, latest v%d\n", FW_VERSION, latest);
  if (latest <= FW_VERSION || url.isEmpty()) return;
  if (latest == bad) {
    Serial.printf("ota: v%d was rolled back before, skipping\n", latest);
    return;
  }

  // 2. Download + flash into the spare slot
  screen(tft, "Updating", "Installing firmware v" + String(latest) + " - don't unplug");
  tft.drawRect(39, 139, 242, 14, C_TEXT);
  httpUpdate.onProgress([&tft](int done, int total) {
    if (total > 0) tft.fillRect(40, 140, 240L * done / total, 12, C_BAR);
  });
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFiClientSecure client;
  client.setCACert(OTA_ROOT_CAS);
  t_httpUpdate_return r = httpUpdate.update(client, url);

  if (r == HTTP_UPDATE_OK) {
    Preferences w;
    w.begin("ota", false);
    w.putBool("trial", true);
    w.putInt("tries", 0);
    w.end();
    screen(tft, "Updated", "Restarting into v" + String(latest));
    Serial.printf("ota: installed v%d, rebooting\n", latest);
    delay(1500);
    ESP.restart();
  }

  Serial.printf("ota: update failed (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  screen(tft, "Update failed", httpUpdate.getLastErrorString(), C_FAIL);
  delay(4000);
}
