# Remote settings

A board on an update channel (set over USB with the serial command `C<name>`,
e.g. `Ccorgi`) reads `boards/<channel>.json` from this repo at power-on and
every 6 hours, right before it checks for new firmware. Commit and push the
file. No release is needed. GitHub can take a few minutes to serve the new
copy.

Every key is optional. A key that's present replaces the board's own value and
is saved on the board. A missing key leaves the board's value alone.

```json
{
  "birthday": "07-15",
  "lat": 46.15,
  "lon": -103.62,
  "tz": "CST6CDT,M3.2.0,M11.1.0"
}
```

| Key | Meaning |
|---|---|
| `birthday` | Roux's birthday as `MM-DD`. `""` clears it. |
| `lat`, `lon` | Where the board lives, for the yard's real weather. Give both. |
| `tz` | POSIX time zone. Examples: Eastern `EST5EDT,M3.2.0,M11.1.0`, Central `CST6CDT,M3.2.0,M11.1.0`, Mountain `MST7MDT,M3.2.0,M11.1.0`, Arizona `MST7`, Pacific `PST8PDT,M3.2.0,M11.1.0` |

This repo is public, so anyone can read these files. Use the nearest town
rather than an exact address, and never put names, Wi-Fi details or passwords
here.
