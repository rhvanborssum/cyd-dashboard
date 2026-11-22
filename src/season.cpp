#include "season.h"

namespace {
int seasonOverride = -1;

// 4th Thursday of November
int thanksgivingDay(int year) {
  tm nov1 = {};
  nov1.tm_year = year - 1900;
  nov1.tm_mon = 10;
  nov1.tm_mday = 1;
  nov1.tm_hour = 12;
  mktime(&nov1);  // fills in tm_wday
  int firstThu = 1 + (4 - nov1.tm_wday + 7) % 7;
  return firstThu + 21;
}
}  // namespace

Season currentSeason(const tm &t) {
  if (seasonOverride >= 0) return (Season)seasonOverride;
  int tg = thanksgivingDay(t.tm_year + 1900);
  if (t.tm_mon == 10 && t.tm_mday <= tg) return SEASON_THANKSGIVING;
  if ((t.tm_mon == 10 && t.tm_mday > tg) || (t.tm_mon == 11 && t.tm_mday <= 25)) return SEASON_CHRISTMAS;
  return SEASON_NONE;
}

void setSeasonOverride(int s) { seasonOverride = s; }

const char *seasonName(Season s) {
  return s == SEASON_THANKSGIVING ? "thanksgiving" : s == SEASON_CHRISTMAS ? "christmas" : "none";
}
