#pragma once
// Which holiday or season decorations the animated scenes should show.
// The fish tank only decorates for Thanksgiving and Christmas; the corgi yard
// uses all of them.
#include <time.h>

enum Season {
  SEASON_NONE = 0,
  SEASON_THANKSGIVING = 1,  // Nov 1 - Thanksgiving
  SEASON_CHRISTMAS = 2,     // day after Thanksgiving - Dec 25
  SEASON_NEW_YEAR = 3,      // Dec 26 - Jan 1
  SEASON_WINTER = 4,        // Jan 2 - end of Feb
  SEASON_VALENTINE = 5,     // Feb 7 - 14
  SEASON_ST_PATRICK = 6,    // Mar 14 - 17
  SEASON_EASTER = 7,        // the week up to Easter Sunday
  SEASON_SPRING = 8,        // Mar - May
  SEASON_JULY4 = 9,         // Jul 1 - 4
  SEASON_SUMMER = 10,       // Jun - Aug
  SEASON_HALLOWEEN = 11,    // October
  SEASON_FALL = 12,         // September
  SEASON_BIRTHDAY = 13,     // Roux's birthday, once it's set
  SEASON_COUNT
};

Season currentSeason(const tm &t);
void setSeasonOverride(int season);  // -1 = follow the calendar
void setBirthday(int month, int day);  // 1-12, 1-31; 0 = unknown
const char *seasonName(Season s);
