#include "wifi_store.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace {
const int MAX_NETWORKS = 6;
const uint32_t JOIN_TIMEOUT_MS = 15000;
const char *NS = "wifi";

String ssidKey(int i) { return "s" + String(i); }
String passKey(int i) { return "p" + String(i); }

void remember(const String &ssid, const String &pass) {
  if (ssid.isEmpty()) return;
  Preferences p;
  p.begin(NS, false);
  int n = p.getInt("n", 0);
  for (int i = 0; i < n; i++) {
    if (p.getString(ssidKey(i).c_str(), "") == ssid) {
      if (p.getString(passKey(i).c_str(), "") != pass) p.putString(passKey(i).c_str(), pass);
      p.end();
      return;
    }
  }
  if (n == MAX_NETWORKS) {  // full: drop the oldest
    for (int i = 1; i < n; i++) {
      p.putString(ssidKey(i - 1).c_str(), p.getString(ssidKey(i).c_str(), ""));
      p.putString(passKey(i - 1).c_str(), p.getString(passKey(i).c_str(), ""));
    }
    n--;
  }
  p.putString(ssidKey(n).c_str(), ssid);
  p.putString(passKey(n).c_str(), pass);
  p.putInt("n", n + 1);
  p.end();
  Serial.printf("wifi: remembered \"%s\" (%d known)\n", ssid.c_str(), n + 1);
}
}  // namespace

int knownWifiCount() {
  Preferences p;
  p.begin(NS, true);
  int n = p.getInt("n", 0);
  p.end();
  return n;
}

void knownWifiRememberSaved() {
  WiFi.mode(WIFI_STA);  // the driver must be running to read its saved config
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return;
  char ssid[33] = {}, pass[65] = {};
  memcpy(ssid, conf.sta.ssid, 32);
  memcpy(pass, conf.sta.password, 64);
  remember(ssid, pass);
}

void knownWifiRememberConnected() {
  if (WiFi.isConnected()) remember(WiFi.SSID(), WiFi.psk());
}

bool knownWifiConnect() {
  Preferences p;
  p.begin(NS, true);
  int n = p.getInt("n", 0);
  String ssids[MAX_NETWORKS], passes[MAX_NETWORKS];
  for (int i = 0; i < n && i < MAX_NETWORKS; i++) {
    ssids[i] = p.getString(ssidKey(i).c_str(), "");
    passes[i] = p.getString(passKey(i).c_str(), "");
    Serial.printf("wifi: known network \"%s\"\n", ssids[i].c_str());
  }
  p.end();
  if (n == 0) return false;

  WiFi.mode(WIFI_STA);
  int found = WiFi.scanNetworks();
  bool tried[MAX_NETWORKS] = {};
  // Try known networks that are in range, strongest signal first
  for (int attempt = 0; attempt < n; attempt++) {
    int best = -1, bestRssi = -1000;
    for (int s = 0; s < found; s++) {
      for (int k = 0; k < n; k++) {
        if (!tried[k] && WiFi.SSID(s) == ssids[k] && WiFi.RSSI(s) > bestRssi) {
          best = k;
          bestRssi = WiFi.RSSI(s);
        }
      }
    }
    if (best < 0) break;
    tried[best] = true;

    Serial.printf("wifi: trying \"%s\" (%d dBm)\n", ssids[best].c_str(), bestRssi);
    WiFi.begin(ssids[best].c_str(), passes[best].isEmpty() ? nullptr : passes[best].c_str());
    uint32_t start = millis();
    while (!WiFi.isConnected() && millis() - start < JOIN_TIMEOUT_MS) delay(100);
    if (WiFi.isConnected()) {
      WiFi.scanDelete();
      return true;
    }
    WiFi.disconnect();
  }
  WiFi.scanDelete();
  return false;
}

void knownWifiForgetAll() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
}
