#include "season.h"

namespace {
int seasonOverride = -1;
int birthdayMonth = 0, birthdayDay = 0;

time_t noonOn(int year, int month, int day) {  // month 1-12
  tm d = {};
  d.tm_year = year - 1900;
  d.tm_mon = month - 1;
  d.tm_mday = day;
  d.tm_hour = 12;
  return mktime(&d);
}

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

// Easter Sunday (Gregorian), via the anonymous "Computus" algorithm
void easter(int y, int &month, int &day) {
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  month = (h + l - 7 * m + 114) / 31;
  day = (h + l - 7 * m + 114) % 31 + 1;
}
}  // namespace

Season currentSeason(const tm &t) {
  if (seasonOverride >= 0) return (Season)seasonOverride;
  int year = t.tm_year + 1900, mon = t.tm_mon + 1, day = t.tm_mday;

  if (birthdayMonth && mon == birthdayMonth && day == birthdayDay) return SEASON_BIRTHDAY;

  int tg = thanksgivingDay(year);
  if (mon == 11 && day <= tg) return SEASON_THANKSGIVING;
  if ((mon == 11 && day > tg) || (mon == 12 && day <= 25)) return SEASON_CHRISTMAS;
  if ((mon == 12 && day >= 26) || (mon == 1 && day == 1)) return SEASON_NEW_YEAR;
  if (mon == 2 && day >= 7 && day <= 14) return SEASON_VALENTINE;
  if (mon == 3 && day >= 14 && day <= 17) return SEASON_ST_PATRICK;

  int em, ed;
  easter(year, em, ed);
  double daysToEaster = difftime(noonOn(year, em, ed), noonOn(year, mon, day)) / 86400.0;
  if (daysToEaster >= -0.5 && daysToEaster <= 6.5) return SEASON_EASTER;

  if (mon == 7 && day <= 4) return SEASON_JULY4;
  if (mon == 10) return SEASON_HALLOWEEN;
  if (mon <= 2) return SEASON_WINTER;
  if (mon <= 5) return SEASON_SPRING;
  if (mon <= 8) return SEASON_SUMMER;
  return SEASON_FALL;  // September (Oct-Dec are all holidays above)
}

void setSeasonOverride(int s) { seasonOverride = s; }

void setBirthday(int month, int day) {
  birthdayMonth = month;
  birthdayDay = day;
}

const char *seasonName(Season s) {
  static const char *const NAMES[SEASON_COUNT] = {
      "none",     "thanksgiving", "christmas", "new year", "winter", "valentine", "st patrick",
      "easter",   "spring",       "july 4",    "summer",   "halloween", "fall",   "birthday"};
  return s >= 0 && s < SEASON_COUNT ? NAMES[s] : "?";
}
