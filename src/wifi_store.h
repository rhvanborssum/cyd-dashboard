#pragma once
// Remembers several Wi-Fi networks, since the ESP32 itself only keeps the last
// one. WiFiManager's phone setup still adds networks; this copies each one into
// a small list and picks whichever known network is in range at boot.

void knownWifiRememberSaved();      // copy the ESP32's saved network into the list
void knownWifiRememberConnected();  // copy the network we're connected to
bool knownWifiConnect();            // join the strongest known network in range
void knownWifiForgetAll();
int knownWifiCount();
