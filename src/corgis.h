#pragma once
// Animated backyard with corgis (Roux and friends). Same strip rendering and
// lifecycle as the fish tank: tap the screen to throw a ball.
#include <TFT_eSPI.h>
#include <time.h>

#include "season.h"

bool corgiBegin(TFT_eSPI &tft);  // allocate buffer; false if out of memory
void corgiEnd();
void corgiFrame(const tm &t);
void corgiTap(int x, int y);     // throw the ball toward this spot
void corgiCaptureNextFrame();    // stream next frame over Serial (tools/screenshot.py)
