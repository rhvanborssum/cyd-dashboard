#pragma once
// Self-update over the internet from GitHub releases, with automatic rollback.
//
// Flow: the board fetches manifest.json from the latest release
// ({"version": N, "url": ".../firmware.bin"}). If N > FW_VERSION it downloads
// the firmware over certificate-verified HTTPS into the spare app slot and
// reboots into it on "trial". If the new firmware crashes/reboots 3 times
// before running cleanly for 2 minutes, it switches back to the previous slot
// and remembers not to install that version again.
#include <TFT_eSPI.h>

void otaBootCheck();                // first thing in setup(): handles rollback
void otaMarkHealthy();              // call once the firmware has proven itself
void otaCheckForUpdate(TFT_eSPI &tft);  // may reboot into a new version
bool otaEnabled();
