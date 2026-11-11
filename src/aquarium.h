#pragma once
// Animated fish tank screen. Renders into a small strip sprite and pushes the
// frame in bands, so it's flicker-free without needing a full-screen buffer.
#include <TFT_eSPI.h>
#include <time.h>

enum Season { SEASON_NONE = 0, SEASON_THANKSGIVING = 1, SEASON_CHRISTMAS = 2 };

bool aquariumBegin(TFT_eSPI &tft);  // allocate buffer; false if out of memory
void aquariumEnd();                 // free buffer (gives RAM back for HTTPS)
void aquariumFrame(const tm &t);    // advance the simulation and draw one frame
void aquariumTap(int x, int y);     // drop food at a screen position

// Seasonal decorations follow the calendar unless overridden (-1 = auto)
void aquariumSetSeasonOverride(int season);
Season aquariumSeason(const tm &t);
const char *seasonName(Season s);

// Stream the next rendered frame over Serial as raw RGB565 (tools/screenshot.py)
void aquariumCaptureNextFrame();
