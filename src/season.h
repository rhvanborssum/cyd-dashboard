#pragma once
// Which holiday decorations the animated scenes should show.
#include <time.h>

enum Season { SEASON_NONE = 0, SEASON_THANKSGIVING = 1, SEASON_CHRISTMAS = 2 };

// Nov 1 - Thanksgiving: Thanksgiving. Day after Thanksgiving - Dec 25: Christmas.
Season currentSeason(const tm &t);
void setSeasonOverride(int season);  // -1 = follow the calendar
const char *seasonName(Season s);
