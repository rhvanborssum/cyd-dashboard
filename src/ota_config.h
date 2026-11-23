#pragma once
// Where the board looks for updates. Filled in by `tools/release.py setup`
// once the GitHub repo exists. Empty string = self-update disabled.
// Each board type has its own manifest there (see OTA_MANIFEST_NAME in board.h);
// a board on an update channel reads manifest-<channel>.json instead.
#define OTA_RELEASES_URL "https://github.com/rhvanborssum/cyd-dashboard/releases/latest/download/"
// Remote settings for boards on a channel: boards/<channel>.json in the repo
#define OTA_SETTINGS_URL "https://raw.githubusercontent.com/rhvanborssum/cyd-dashboard/main/boards/"
