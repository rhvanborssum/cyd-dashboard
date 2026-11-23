"""Publish firmware to GitHub so the CYD can update itself over the internet.

  py -3.12 tools/release.py setup
      One time, after `gh auth login`. Creates a public GitHub repo
      "cyd-dashboard", points the firmware's update URL at it, and publishes v1.
      Then flash the board ONCE over USB:  py -3.12 -m platformio run -t upload

  py -3.12 tools/release.py publish "what changed" [--only corgi,cyd]
      Bump FW_VERSION in src/version.h first. Builds, commits, pushes, and
      creates a GitHub release with the firmware and a manifest per channel.
      Boards pick it up within 6 hours, or on their next restart.
      --only limits which channels get the new version; the others keep
      pointing at the firmware they already have.

Channels (a board reads the manifest of its channel, set over USB with C<name>):
  cyd      CYD boards on the default channel (the office dashboard)
  corgi    the corgi yard gift CYD
  e32r40t  the 4" ESP32-32E board

Remote settings for a channel live in boards/<channel>.json; commit and push
that file and the board picks it up at its next check (see boards/README.md).

The repo must be public so the board can download without a password. It holds
only code: Wi-Fi details live on the board, never in the firmware.
"""
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "cyd-dashboard"
# channel: (PlatformIO env, firmware file name in the release, manifest the board reads)
CHANNELS = {
    "cyd": ("cyd", "firmware.bin", "manifest.json"),
    "corgi": ("cyd", "firmware.bin", "manifest-corgi.json"),
    "e32r40t": ("e32r40t", "firmware-e32r40t.bin", "manifest-e32r40t.json"),
}
OUT_DIR = ROOT / ".pio" / "release"
TRAILER = "\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"


def run(*cmd, check=True, capture=False):
    print("$", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=capture)
    if check and r.returncode != 0:
        if capture:
            print(r.stdout, r.stderr)
        sys.exit(f"command failed: {' '.join(cmd)}")
    return r


def out(*cmd):
    return run(*cmd, capture=True).stdout.strip()


def fw_version():
    m = re.search(r"#define\s+FW_VERSION\s+(\d+)", (ROOT / "src" / "version.h").read_text())
    return int(m.group(1))


def gh_login():
    if run("gh", "auth", "status", check=False, capture=True).returncode != 0:
        sys.exit("GitHub CLI isn't logged in. Run:  gh auth login")
    return out("gh", "api", "user", "-q", ".login"), out("gh", "api", "user", "-q", ".id")


def commit_all(message):
    run("git", "add", "-A")
    if run("git", "diff", "--cached", "--quiet", check=False).returncode != 0:
        run("git", "commit", "-q", "-m", message + TRAILER)


def build(envs):
    for env in envs:
        run(sys.executable, "-m", "platformio", "run", "-e", env)
        if not (ROOT / ".pio" / "build" / env / "firmware.bin").exists():
            sys.exit(f"build of {env} produced no firmware.bin")


def release(login, notes, only=None):
    v = fw_version()
    if run("gh", "release", "view", f"v{v}", check=False, capture=True).returncode == 0:
        sys.exit(f"Release v{v} already exists - bump FW_VERSION in src/version.h first.")
    chosen = only or list(CHANNELS)
    unknown = [c for c in chosen if c not in CHANNELS]
    if unknown:
        sys.exit(f"unknown channel(s): {', '.join(unknown)} (known: {', '.join(CHANNELS)})")
    build(sorted({CHANNELS[c][0] for c in chosen}))
    if OUT_DIR.exists():
        shutil.rmtree(OUT_DIR)
    OUT_DIR.mkdir(parents=True)
    assets = set()
    for c in chosen:
        env, fw_name, manifest_name = CHANNELS[c]
        fw = OUT_DIR / fw_name
        fw.write_bytes((ROOT / ".pio" / "build" / env / "firmware.bin").read_bytes())
        manifest = OUT_DIR / manifest_name
        url = f"https://github.com/{login}/{REPO}/releases/download/v{v}/{fw_name}"
        manifest.write_text(json.dumps({"version": v, "url": url}, indent=2) + "\n")
        assets |= {str(fw), str(manifest)}
    # Channels left out keep their current manifest (it points at an older
    # release's firmware), copied from the release that is "latest" now
    for c in CHANNELS:
        if c in chosen:
            continue
        name = CHANNELS[c][2]
        r = run("gh", "release", "download", "--pattern", name, "--dir", str(OUT_DIR), check=False, capture=True)
        if r.returncode == 0 and (OUT_DIR / name).exists():
            print(f"  {c}: keeping {json.loads((OUT_DIR / name).read_text())}")
            assets.add(str(OUT_DIR / name))
        else:
            print(f"  {c}: no current manifest, so those boards won't see an update")
    assets = sorted(assets)
    commit_all(f"Release v{v}: {notes}")
    run("git", "push", "-q", "origin", "main")
    run("gh", "release", "create", f"v{v}", *assets,
        "--title", f"v{v}", "--notes", notes, "--target", "main")
    print(f"\nPublished v{v}. The board will install it within 6 hours (or on restart).")


def setup():
    login, uid = gh_login()
    print(f"GitHub user: {login}")

    if not (ROOT / ".git").exists():
        run("git", "init", "-q", "-b", "main")
    # Repo-local identity using GitHub's private no-reply address
    run("git", "config", "user.name", login)
    run("git", "config", "user.email", f"{uid}+{login}@users.noreply.github.com")
    # Repo-local too: push using the gh login, with no password prompt
    helper = "credential.https://github.com.helper"
    run("git", "config", "--unset-all", helper, check=False)
    run("git", "config", "--add", helper, "")
    run("git", "config", "--add", helper, "!gh auth git-credential")

    cfg = ROOT / "src" / "ota_config.h"
    url = f"https://github.com/{login}/{REPO}/releases/latest/download/"
    cfg.write_text(re.sub(r'#define OTA_RELEASES_URL ".*"', f'#define OTA_RELEASES_URL "{url}"', cfg.read_text()))
    settings = f"https://raw.githubusercontent.com/{login}/{REPO}/main/boards/"
    cfg.write_text(re.sub(r'#define OTA_SETTINGS_URL ".*"', f'#define OTA_SETTINGS_URL "{settings}"', cfg.read_text()))
    print(f"Update URL: {url}")

    commit_all("CYD dashboard with seasonal fish tank and self-update")
    if run("gh", "repo", "view", f"{login}/{REPO}", check=False, capture=True).returncode != 0:
        run("gh", "repo", "create", REPO, "--public", "--source", ".", "--remote", "origin",
            "--description", "ESP32 Cheap Yellow Display dashboard: clock, weather, stock ticker, fish tank")
    elif run("git", "remote", "get-url", "origin", check=False, capture=True).returncode != 0:
        run("git", "remote", "add", "origin", f"https://github.com/{login}/{REPO}.git")
    run("git", "push", "-q", "-u", "origin", "main")

    release(login, "First self-updating release")
    print("\nNow flash the board once over USB:  py -3.12 -m platformio run -t upload")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "setup":
        setup()
    elif len(sys.argv) >= 3 and sys.argv[1] == "publish":
        only = None
        if "--only" in sys.argv:
            i = sys.argv.index("--only")
            only = [c.strip() for c in sys.argv[i + 1].split(",") if c.strip()]
        release(gh_login()[0], sys.argv[2], only)
    else:
        print(__doc__)
